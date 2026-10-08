// Sound follows the screen: libpulse directly (works with pipewire-pulse).

#include "audio.hpp"

#include <pulse/pulseaudio.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace beamer {

namespace {

class Pulse {
public:
    Pulse() {
        loop_ = pa_mainloop_new();
        ctx_ = pa_context_new(pa_mainloop_get_api(loop_), "beamer");
        if (pa_context_connect(ctx_, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0) return;
        for (;;) {
            auto st = pa_context_get_state(ctx_);
            if (st == PA_CONTEXT_READY) { ok_ = true; return; }
            if (!PA_CONTEXT_IS_GOOD(st)) return;
            pa_mainloop_iterate(loop_, 1, nullptr);
        }
    }
    ~Pulse() {
        pa_context_disconnect(ctx_);
        pa_context_unref(ctx_);
        pa_mainloop_free(loop_);
    }
    Pulse(const Pulse&) = delete;
    Pulse& operator=(const Pulse&) = delete;

    explicit operator bool() const { return ok_; }

    struct Sink {
        std::string name;
        bool plugged; // no port reports "not available"
    };

    std::vector<Sink> sinks() {
        std::vector<Sink> out;
        wait(pa_context_get_sink_info_list(ctx_, [](pa_context*, const pa_sink_info* i, int eol, void* ud) {
            if (eol || !i) return;
            bool plugged = i->n_ports == 0;
            for (uint32_t k = 0; k < i->n_ports; ++k)
                plugged |= i->ports[k]->available != PA_PORT_AVAILABLE_NO;
            static_cast<std::vector<Sink>*>(ud)->push_back({i->name, plugged});
        }, &out));
        return out;
    }

    std::string default_sink() {
        std::string out;
        wait(pa_context_get_server_info(ctx_, [](pa_context*, const pa_server_info* i, void* ud) {
            if (i && i->default_sink_name) *static_cast<std::string*>(ud) = i->default_sink_name;
        }, &out));
        return out;
    }

    void set_default_sink(const std::string& name) {
        wait(pa_context_set_default_sink(ctx_, name.c_str(), nullptr, nullptr));
    }

private:
    void wait(pa_operation* op) {
        if (!op) return;
        while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
            if (pa_mainloop_iterate(loop_, 1, nullptr) < 0) break;
        pa_operation_unref(op);
    }

    pa_mainloop* loop_ = nullptr;
    pa_context* ctx_ = nullptr;
    bool ok_ = false;
};

static bool is_hdmi(std::string_view name) {
    std::string lower(name);
    std::ranges::transform(lower, lower.begin(), ::tolower);
    return lower.contains("hdmi");
}

std::mutex audio_mutex; // the daemon switches audio from background threads

} // namespace

void set_audio(const Config& cfg, bool to_hdmi) {
    if (!cfg.switch_audio) return;
    std::lock_guard lock(audio_mutex);
    Pulse pa;
    if (!pa) return;
    // HDMI audio jacks often report a moment after the display does
    std::string sink;
    for (int attempt = 0; attempt < (to_hdmi ? 6 : 1); ++attempt) {
        for (auto& s : pa.sinks()) {
            if (to_hdmi ? (is_hdmi(s.name) && s.plugged) : !is_hdmi(s.name)) { sink = s.name; break; }
        }
        if (!sink.empty()) break;
        std::this_thread::sleep_for(500ms);
    }
    if (!sink.empty() && pa.default_sink() != sink) pa.set_default_sink(sink);
}

std::string default_sink() {
    Pulse pa;
    return pa ? pa.default_sink() : "";
}

} // namespace beamer
