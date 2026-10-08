// beamer — plug-and-play HDMI / projector handling for Wayland tiling compositors.

#include "audio.hpp"
#include "backend.hpp"
#include "common.hpp"
#include "layout.hpp"
#include "mirror.hpp"

#include <nlohmann/json.hpp>

#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <format>
#include <print>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

extern char** environ;

using namespace std::chrono_literals;

namespace beamer {

using json = nlohmann::json;

static constexpr std::string_view USAGE = R"(beamer — plug-and-play HDMI / projector handling for Wayland compositors.

Usage:
  beamer ui                    open / close the display popup (Quickshell)
  beamer mirror                laptop screen mirrored on the external screen
  beamer extend [WHERE...]     external screen as a second desktop
  beamer external              only the external screen, laptop panel off
  beamer internal              only the laptop screen
  beamer cycle                 mirror -> extend -> external -> internal -> mirror ...
  beamer keep | revert         confirm or undo a change made with --revert=SECONDS
  beamer status [--json]       show monitors, current mode and audio sink
  beamer watch                 print the state as a JSON line on every change
  beamer daemon                listen for hotplug events and auto-configure

WHERE places external screens relative to the laptop and is remembered per display:
  right | left | above | below            all external screens
  above:120                               ... shifted 120 px along the edge
  HDMI-A-1=left                           one screen only

Any mode accepts --revert=SECONDS: the daemon switches back to the previous mode
unless `beamer keep` arrives in time (the popup uses this for "Projector only").
)";

// ---------------------------------------------------------------- requests

struct Spec {
    std::string output; // empty = every external screen
    Placement placement;
};

static std::optional<Spec> parse_spec(std::string_view s) {
    Spec spec;
    if (auto eq = s.find('='); eq != std::string_view::npos) {
        spec.output = s.substr(0, eq);
        s.remove_prefix(eq + 1);
    }
    auto colon = s.find(':');
    auto side = parse_side(s.substr(0, colon));
    if (!side) return std::nullopt;
    spec.placement.side = *side;
    if (colon != std::string_view::npos) {
        auto num = s.substr(colon + 1);
        auto [end, ec] = std::from_chars(num.data(), num.data() + num.size(), spec.placement.offset);
        if (ec != std::errc{} || end != num.data() + num.size()) return std::nullopt;
    }
    return spec;
}

// Removes `--revert=SECONDS` from the arguments and returns the seconds.
static std::optional<int> take_revert(std::vector<std::string>& args) {
    std::optional<int> secs;
    std::erase_if(args, [&](const std::string& a) {
        if (!a.starts_with("--revert=")) return false;
        auto num = std::string_view(a).substr(9);
        int v = 0;
        auto [end, ec] = std::from_chars(num.data(), num.data() + num.size(), v);
        if (ec == std::errc{} && end == num.data() + num.size() && v > 0) secs = v;
        return true;
    });
    return secs;
}

static std::optional<std::vector<Spec>> parse_specs(const std::vector<std::string>& args) {
    std::vector<Spec> specs;
    for (auto& a : args) {
        auto s = parse_spec(a);
        if (!s) return std::nullopt;
        specs.push_back(*s);
    }
    return specs;
}

// ---------------------------------------------------------------- session

static std::string key_of(const Output& o) { return o.description.empty() ? o.name : o.description; }

// A scale that divides the preferred resolution evenly.
static double scale_for(const Output& o) { return o.pref_width >= 3200 ? 1.5 : 1.0; }

static bool in_path(std::string_view exe) {
    const char* path = std::getenv("PATH");
    if (!path) return false;
    std::string_view p(path);
    for (size_t start = 0; start <= p.size();) {
        auto end = std::min(p.find(':', start), p.size());
        if (access(std::format("{}/{}", p.substr(start, end - start), exe).c_str(), X_OK) == 0) return true;
        start = end + 1;
    }
    return false;
}

struct Screens {
    std::optional<Output> internal; // missing when the lid is closed or on a desktop
    std::vector<Output> externals;
};

class Session {
public:
    explicit Session(bool daemon) : daemon_(daemon), cfg_(load_config()), backend_(make_backend(cfg_)) {}

    Config& cfg() { return cfg_; }
    Backend& backend() { return *backend_; }

    Screens screens() {
        Screens s;
        for (auto& o : backend_->outputs()) {
            if (o.name == cfg_.internal) s.internal = o;
            else s.externals.push_back(o);
        }
        return s;
    }

    std::string current_mode() const { return read_state().value_or(cfg_.default_mode); }

    // Configure every screen for `mode`; returns the message shown to the user.
    std::string apply(std::string_view mode, const std::vector<Spec>& specs = {}, bool quiet = false) {
        auto [internal, ext] = screens();

        LayoutStore store;
        if (!specs.empty()) {
            for (auto& spec : specs)
                for (auto& e : ext)
                    if (spec.output.empty() || spec.output == e.name) store.set(key_of(e), spec.placement);
            store.save();
        }

        std::vector<OutputConfig> cfgs;
        Size lap = internal ? logical(internal->pref_width, internal->pref_height, cfg_.internal_scale) : Size{};
        auto lap_at = [&](int x, int y) {
            if (internal) cfgs.push_back({.name = internal->name, .x = x, .y = y, .scale = cfg_.internal_scale});
        };
        // external screens side by side, starting at x
        auto row = [&](int x) {
            for (auto& e : ext) {
                cfgs.push_back({.name = e.name, .x = x, .y = 0, .scale = scale_for(e)});
                x += logical(e.pref_width, e.pref_height, scale_for(e)).w;
            }
        };

        bool wl_mirror = false;
        std::string msg;
        if (ext.empty()) {
            lap_at(0, 0);
            msg = "No external screen connected";
        } else if (!internal) {
            row(0);
        } else if (mode == "mirror") {
            lap_at(0, 0);
            for (auto& e : ext) {
                if (backend_->native_mirror())
                    cfgs.push_back({.name = e.name, .auto_position = true, .scale = 1, .mirror = internal->name});
                else
                    cfgs.push_back({.name = e.name, .x = lap.w, .y = 0, .scale = 1});
            }
            wl_mirror = !backend_->native_mirror();
        } else if (mode == "extend") {
            std::vector<std::pair<Size, Placement>> placed;
            for (auto& e : ext) placed.push_back({logical(e.pref_width, e.pref_height, scale_for(e)), store.get(key_of(e))});
            auto pos = arrange(lap, placed);
            lap_at(pos[0].x, pos[0].y);
            for (size_t i = 0; i < ext.size(); ++i)
                cfgs.push_back({.name = ext[i].name, .x = pos[i + 1].x, .y = pos[i + 1].y, .scale = scale_for(ext[i])});
        } else if (mode == "external") {
            cfgs.push_back({.name = internal->name, .enabled = false});
            row(0);
        } else { // internal
            lap_at(0, 0);
            for (auto& e : ext) cfgs.push_back({.name = e.name, .enabled = false});
        }

        bool ok = backend_->configure(cfgs);
        if (wl_mirror) {
            if (!mirror_start(internal->name, ext.front().name))
                msg = "Mirroring needs wl-mirror on this compositor — screens extended instead";
        } else {
            mirror_stop();
        }

        if (msg.empty()) {
            std::string names;
            for (auto& e : ext) names += (names.empty() ? "" : ", ") + e.name;
            std::string label(mode);
            label[0] = static_cast<char>(std::toupper(label[0]));
            msg = ok ? std::format("{} ({})", label, names) : std::format("Could not switch to {} ({})", mode, names);
        }

        bool to_hdmi = !ext.empty() && mode != "internal";
        if (daemon_) std::thread([cfg = cfg_, to_hdmi] { set_audio(cfg, to_hdmi); }).detach();
        else set_audio(cfg_, to_hdmi);

        if (!quiet) notify(cfg_, msg);
        return msg;
    }

    // Everything the UI needs to draw the popup.
    json state() {
        auto [internal, ext] = screens();
        LayoutStore store;
        json j = {
            {"backend", backend_->name()},
            {"mode", current_mode()},
            {"defaultMode", cfg_.default_mode},
            {"nativeMirror", backend_->native_mirror()},
            {"mirrorAvailable", backend_->native_mirror() || in_path("wl-mirror")},
            {"internal", nullptr},
            {"externals", json::array()},
        };
        if (internal) {
            auto l = logical(internal->pref_width, internal->pref_height, cfg_.internal_scale);
            j["internal"] = {
                {"name", internal->name},       {"description", internal->description},
                {"enabled", internal->enabled}, {"width", internal->pref_width},
                {"height", internal->pref_height}, {"scale", cfg_.internal_scale},
                {"logicalWidth", l.w},          {"logicalHeight", l.h},
            };
        }
        for (auto& e : ext) {
            auto l = logical(e.pref_width, e.pref_height, scale_for(e));
            auto p = store.get(key_of(e));
            j["externals"].push_back({
                {"name", e.name},           {"description", e.description},
                {"enabled", e.enabled},     {"width", e.pref_width},
                {"height", e.pref_height},  {"refresh", e.refresh},
                {"scale", scale_for(e)},    {"logicalWidth", l.w},
                {"logicalHeight", l.h},     {"side", side_name(p.side)},
                {"offset", p.offset},       {"mirrorOf", e.mirror_of},
            });
        }
        return j;
    }

private:
    bool daemon_;
    Config cfg_;
    std::unique_ptr<Backend> backend_;
};

// ---------------------------------------------------------------- control socket

static fs::path control_path() {
    const char* display = std::getenv("WAYLAND_DISPLAY");
    return runtime_dir() / std::format("beamer-{}.sock", display && *display ? display : "wayland-0");
}

static bool send_all(int fd, std::string_view data) {
    while (!data.empty()) {
        ssize_t n = send(fd, data.data(), data.size(), MSG_NOSIGNAL);
        if (n <= 0) return false;
        data.remove_prefix(n);
    }
    return true;
}

static std::string join(const std::vector<std::string>& args) {
    std::string s;
    for (auto& a : args) s += (s.empty() ? "" : "\t") + a;
    return s + '\n';
}

// Sends a request to a running daemon and streams its reply to stdout.
// Returns false if no daemon is listening.
static bool ask_daemon(const std::vector<std::string>& args) {
    int fd = connect_unix(control_path());
    if (fd < 0) return false;
    send_all(fd, join(args));
    char buf[8192];
    for (ssize_t n; (n = read(fd, buf, sizeof(buf))) > 0;) {
        fwrite(buf, 1, n, stdout);
        fflush(stdout);
    }
    close(fd);
    return true;
}

// ---------------------------------------------------------------- daemon

static std::string plug_mode(const Config& cfg) {
    return (cfg.remember_mode ? read_state() : std::nullopt).value_or(cfg.default_mode);
}

static int listen_unix(const fs::path& path) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    auto p = path.string();
    if (fd < 0 || p.size() >= sizeof(addr.sun_path)) return -1;
    std::ranges::copy(p, addr.sun_path);
    unlink(p.c_str());
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 || listen(fd, 8) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

class Daemon {
public:
    int run() {
        std::signal(SIGCHLD, SIG_IGN); // reap notify-send / wl-mirror automatically
        std::signal(SIGPIPE, SIG_IGN);

        if (int fd = connect_unix(control_path()); fd >= 0) {
            close(fd);
            std::println(stderr, "beamer daemon is already running");
            return 1;
        }
        listen_ = listen_unix(control_path());
        int events = session_.backend().event_fd();
        if (listen_ < 0 || events < 0) {
            std::println(stderr, "cannot start: no control socket or compositor connection");
            return 1;
        }

        auto& cfg = session_.cfg();
        if (!cfg.remember_mode) write_state(cfg.default_mode);
        auto s = session_.screens();
        known_ = names(s);
        // a screen may already be connected at login
        if (!s.externals.empty()) session_.apply(plug_mode(cfg));

        for (;;) {
            auto& be = session_.backend();
            be.before_poll();
            if (be.changed()) {
                hotplug();
                continue;
            }
            std::vector<pollfd> fds{{events, POLLIN, 0}, {listen_, POLLIN, 0}};
            for (int w : watchers_) fds.push_back({w, POLLIN, 0});
            int timeout = -1;
            if (revert_to_)
                timeout = std::max<int>(0, std::chrono::ceil<std::chrono::milliseconds>(revert_at_ - std::chrono::steady_clock::now()).count());
            if (poll(fds.data(), fds.size(), timeout) < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (revert_to_ && std::chrono::steady_clock::now() >= revert_at_) {
                revert();
                continue;
            }
            if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
                if (!be.read()) break; // compositor exited
            }
            if (fds[1].revents & POLLIN) client();
            // watchers never send anything; readable means they hung up
            for (size_t i = 2; i < fds.size(); ++i)
                if (fds[i].revents) drop(fds[i].fd);
        }
        unlink(control_path().c_str());
        return 0;
    }

private:
    static std::set<std::string> names(const Screens& s) {
        std::set<std::string> n;
        for (auto& e : s.externals) n.insert(e.name);
        return n;
    }

    // Outputs may have appeared or vanished. Enabling/disabling a screen
    // also triggers this on some compositors, so diff against what we knew.
    void hotplug() {
        session_.cfg() = load_config();
        auto& cfg = session_.cfg();
        auto s = session_.screens();
        auto now = names(s);
        std::vector<std::string> added, removed;
        std::ranges::set_difference(now, known_, std::back_inserter(added));
        std::ranges::set_difference(known_, now, std::back_inserter(removed));
        known_ = now;
        if (!added.empty() || !removed.empty()) revert_to_.reset();

        if (!added.empty()) {
            std::this_thread::sleep_for(500ms); // let the output settle before re-ruling it
            auto mode = plug_mode(cfg);
            write_state(mode);
            session_.apply(mode);
        } else if (!removed.empty()) {
            if (s.externals.empty()) {
                session_.apply("internal", {}, true);
                notify(cfg, std::format("{} disconnected — back to laptop screen", removed.front()));
            } else {
                session_.apply(session_.current_mode(), {}, true);
            }
        }
        broadcast();
    }

    void client() {
        int fd = accept4(listen_, nullptr, nullptr, SOCK_CLOEXEC);
        if (fd < 0) return;
        timeval tv{1, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        std::string line;
        char c;
        while (line.size() < 4096 && read(fd, &c, 1) == 1 && c != '\n') line += c;
        std::vector<std::string> args;
        for (size_t start = 0; start <= line.size();) {
            auto end = std::min(line.find('\t', start), line.size());
            if (end > start) args.push_back(line.substr(start, end - start));
            start = end + 1;
        }
        if (args.empty()) { close(fd); return; }

        if (args[0] == "watch") {
            if (send_all(fd, state().dump() + '\n')) watchers_.push_back(fd);
            else close(fd);
            return;
        }
        if (args[0] == "keep" || args[0] == "revert") {
            bool pending = revert_to_.has_value();
            std::string reply = !pending ? "nothing to confirm" : args[0] == "keep" ? "kept" : "reverted";
            if (pending && args[0] == "keep") revert_to_.reset();
            send_all(fd, reply + '\n');
            close(fd);
            if (pending && args[0] == "revert") revert();
            else broadcast();
            return;
        }

        std::vector<std::string> rest(args.begin() + 1, args.end());
        auto secs = take_revert(rest);
        auto specs = parse_specs(rest);
        if (!valid_mode(args[0]) || !specs) {
            send_all(fd, "error: invalid request\n");
            close(fd);
            return;
        }
        session_.cfg() = load_config();
        auto previous = session_.current_mode();
        revert_to_.reset();
        if (secs && previous != args[0]) {
            revert_to_ = previous;
            revert_at_ = std::chrono::steady_clock::now() + std::chrono::seconds(*secs);
            revert_epoch_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::system_clock::now().time_since_epoch()).count() + *secs * 1000LL;
        }
        write_state(args[0]);
        send_all(fd, session_.apply(args[0], *specs) + '\n');
        close(fd);
        broadcast();
    }

    // Undo an unconfirmed change (the screen may be black, so no questions asked).
    void revert() {
        if (!revert_to_) return;
        auto mode = *std::exchange(revert_to_, std::nullopt);
        write_state(mode);
        session_.apply(mode);
        broadcast();
    }

    json state() {
        auto j = session_.state();
        j["revertTo"] = revert_to_ ? json(*revert_to_) : json(nullptr);
        j["revertAt"] = revert_to_ ? json(revert_epoch_ms_) : json(nullptr); // epoch ms
        return j;
    }

    void broadcast() {
        if (watchers_.empty()) return;
        auto line = state().dump() + '\n';
        for (int w : std::vector(watchers_))
            if (!send_all(w, line)) drop(w);
    }

    void drop(int fd) {
        close(fd);
        std::erase(watchers_, fd);
    }

    Session session_{true};
    int listen_ = -1;
    std::set<std::string> known_;
    std::vector<int> watchers_;
    std::optional<std::string> revert_to_;
    std::chrono::steady_clock::time_point revert_at_;
    long long revert_epoch_ms_ = 0;
};

// ---------------------------------------------------------------- commands

static int cmd_set(const std::vector<std::string>& args) {
    std::vector<std::string> rest(args.begin() + 1, args.end());
    take_revert(rest); // only the daemon can time a revert
    auto specs = parse_specs(rest);
    if (!specs) {
        std::println(stderr, "invalid position, expected right|left|above|below[:OFFSET] or OUTPUT=SIDE[:OFFSET]");
        return 1;
    }
    if (ask_daemon(args)) return 0;
    Session s(false);
    write_state(args[0]);
    s.apply(args[0], *specs);
    return 0;
}

static int cmd_cycle() {
    auto cur = Session(false).current_mode();
    auto i = std::ranges::find(MODES, cur) - MODES.begin();
    return cmd_set({std::string(MODES[(i + 1) % MODES.size()])});
}

static int cmd_status(bool as_json) {
    Session s(false);
    auto st = s.state();
    auto sink = default_sink();
    if (as_json) {
        st["audio"] = sink;
        std::println("{}", st.dump());
        return 0;
    }
    std::println("backend: {}   mode: {}   (default on plug-in: {})", st["backend"].get<std::string>(),
                 st["mode"].get<std::string>(), st["defaultMode"].get<std::string>());
    for (auto& o : s.backend().outputs()) {
        std::string state = o.enabled ? std::format("{}x{}@{:.0f} scale {} at {},{}", o.width, o.height, o.refresh, o.scale, o.x, o.y)
                                      : "disabled";
        std::string mirror = o.mirror_of.empty() ? "" : " mirrors " + o.mirror_of;
        std::println("  {:10} {}{}  [{}]", o.name, state, mirror, o.description);
    }
    std::println("audio: {}", sink.empty() ? "(no sound server)" : sink);
    return 0;
}

static int cmd_watch() {
    if (!ask_daemon({"watch"})) {
        std::println(stderr, "beamer daemon is not running");
        return 1;
    }
    return 0;
}

// Runs a command, waits for it, and returns its exit status (output discarded).
static int run_quiet(const std::vector<std::string>& args, bool new_session = false) {
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    if (new_session) posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);
    pid_t pid;
    int rc = posix_spawnp(&pid, argv[0], &fa, &attr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&attr);
    if (rc != 0) return -1;
    if (new_session) return 0;
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static int cmd_ui() {
    const char* env = std::getenv("BEAMER_UI_DIR");
    fs::path dir = env && *env ? fs::path(env) : data_dir() / "ui";
    if (!fs::exists(dir / "shell.qml")) {
        std::println(stderr, "{} not found — run ./install.sh", (dir / "shell.qml").string());
        return 1;
    }
    std::error_code ec;
    auto self = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) setenv("BEAMER_BIN", self.c_str(), 1);

    // the popup talks to the daemon; make sure one is running
    if (int fd = connect_unix(control_path()); fd >= 0) close(fd);
    else if (!ec) run_quiet({self.string(), "daemon"}, true);

    if (run_quiet({"qs", "-p", dir.string(), "ipc", "call", "beamer", "toggle"}) == 0) return 0;
    if (run_quiet({"qs", "-p", dir.string(), "-d"}) == 0) return 0;
    std::println(stderr, "could not start Quickshell (qs) — is it installed?");
    return 1;
}

} // namespace beamer

int main(int argc, char** argv) {
    using namespace beamer;
    std::vector<std::string> args(argv + 1, argv + argc);
    std::string_view cmd = args.empty() ? "status" : std::string_view(args[0]);

    if (valid_mode(cmd)) return cmd_set(args);
    if (cmd == "cycle") return cmd_cycle();
    if (cmd == "status") return cmd_status(args.size() > 1 && args[1] == "--json");
    if (cmd == "watch") return cmd_watch();
    if (cmd == "keep" || cmd == "revert") {
        if (ask_daemon({std::string(cmd)})) return 0;
        std::println(stderr, "beamer daemon is not running");
        return 1;
    }
    if (cmd == "daemon") return Daemon().run();
    if (cmd == "ui") return cmd_ui();
    std::print("{}", USAGE);
    return cmd == "-h" || cmd == "--help" || cmd == "help" ? 0 : 1;
}
