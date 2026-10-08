// Hyprland (Lua config): rules are set at runtime with `eval hl.monitor({...})`
// on the command socket, hotplug comes from the event socket.

#include "backend.hpp"

#include <nlohmann/json.hpp>

#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <format>
#include <print>

namespace beamer {

using json = nlohmann::json;

static fs::path hypr_socket(const char* name) {
    const char* sig = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
    return runtime_dir() / "hypr" / (sig ? sig : "") / name;
}

static std::string lua_quote(std::string_view s) {
    std::string r = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') r += '\\';
        r += c;
    }
    return r + '"';
}

class Hyprland final : public Backend {
public:
    ~Hyprland() override {
        if (events_ >= 0) close(events_);
    }

    std::string_view name() const override { return "hyprland"; }
    bool native_mirror() const override { return true; }

    std::vector<Output> outputs() override {
        std::vector<Output> out;
        auto j = json::parse(request("j/monitors all"), nullptr, false);
        if (!j.is_array()) return out;
        for (auto& m : j) {
            Output o;
            o.name = m.value("name", "");
            o.description = m.value("description", "");
            o.enabled = !m.value("disabled", false);
            o.x = m.value("x", 0);
            o.y = m.value("y", 0);
            o.width = m.value("width", 0);
            o.height = m.value("height", 0);
            o.refresh = m.value("refreshRate", 0.0);
            o.scale = m.value("scale", 1.0);
            o.pref_width = o.width;
            o.pref_height = o.height;
            // the first advertised mode is the preferred one, e.g. "1920x1080@60.00Hz"
            if (auto it = m.find("availableModes"); it != m.end() && it->is_array() && !it->empty()) {
                auto mode = it->front().get<std::string>();
                if (std::sscanf(mode.c_str(), "%dx%d", &o.pref_width, &o.pref_height) != 2)
                    o.pref_width = o.width, o.pref_height = o.height;
            }
            if (auto of = m.value("mirrorOf", "none"); of != "none") o.mirror_of = of;
            out.push_back(std::move(o));
        }
        return out;
    }

    bool configure(const std::vector<OutputConfig>& cfgs) override {
        // all rules in one eval: a single round trip, applied together
        std::string lua;
        for (auto& c : cfgs) {
            lua += std::format("hl.monitor({{ output = {}", lua_quote(c.name));
            if (!c.enabled) {
                lua += ", disabled = true })\n";
                continue;
            }
            auto pos = c.auto_position ? std::string("auto") : std::format("{}x{}", c.x, c.y);
            lua += std::format(", mode = \"preferred\", position = \"{}\", scale = {}, disabled = false", pos, c.scale);
            if (!c.mirror.empty()) lua += std::format(", mirror = {}", lua_quote(c.mirror));
            lua += " })\n";
        }
        if (lua.empty()) return true;
        auto out = request("/eval " + lua);
        if (out != "ok") std::println(stderr, "hyprland eval failed: '{}' for\n{}", out, lua);
        return out == "ok";
    }

    int event_fd() override {
        if (events_ < 0) events_ = connect_unix(hypr_socket(".socket2.sock"));
        return events_;
    }

    bool read() override {
        char chunk[4096];
        ssize_t n = ::read(events_, chunk, sizeof(chunk));
        if (n <= 0) return false;
        buf_.append(chunk, n);
        size_t start = 0;
        for (size_t nl; (nl = buf_.find('\n', start)) != std::string::npos; start = nl + 1) {
            std::string_view line(buf_.data() + start, nl - start);
            auto event = line.substr(0, line.find(">>"));
            // enabling/disabling a screen also fires these; the daemon diffs the output list
            if (event == "monitoradded" || event == "monitorremoved") changed_ = true;
        }
        buf_.erase(0, start);
        return true;
    }

    bool changed() override { return std::exchange(changed_, false); }

private:
    // One request/response round trip on Hyprland's command socket.
    static std::string request(std::string_view req) {
        int fd = connect_unix(hypr_socket(".socket.sock"));
        if (fd < 0) return {};
        std::string out;
        if (write(fd, req.data(), req.size()) == static_cast<ssize_t>(req.size())) {
            char buf[16384];
            for (ssize_t n; (n = ::read(fd, buf, sizeof(buf))) > 0;) out.append(buf, n);
        }
        close(fd);
        return out;
    }

    int events_ = -1;
    std::string buf_;
    bool changed_ = false;
};

std::unique_ptr<Backend> make_hyprland() { return std::make_unique<Hyprland>(); }

std::unique_ptr<Backend> make_backend(const Config& cfg) {
    if (cfg.backend == "hyprland") return make_hyprland();
    if (cfg.backend == "wlr") return make_wlr();
    if (std::getenv("HYPRLAND_INSTANCE_SIGNATURE")) return make_hyprland();
    return make_wlr();
}

} // namespace beamer
