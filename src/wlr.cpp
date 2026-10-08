// Generic backend: wlr-output-management-unstable-v1, implemented by sway,
// niri, river, labwc, wayfire, dwl and most other wlroots-style compositors.
// Heads appearing/disappearing give hotplug; a configuration applies layout.

#include "backend.hpp"

#include "wlr-output-management-unstable-v1-client-protocol.h"
#include <wayland-client.h>

#include <algorithm>
#include <print>
#include <utility>

namespace beamer {

namespace {

struct Head;

struct Mode {
    zwlr_output_mode_v1* proxy;
    Head* head;
    uint32_t version;
    int width = 0, height = 0, refresh = 0; // refresh in mHz
    bool preferred = false;
};

struct Head {
    zwlr_output_head_v1* proxy;
    std::string name{}, description{};
    bool enabled = false;
    int x = 0, y = 0;
    double scale = 1;
    zwlr_output_mode_v1* current = nullptr;
    std::vector<std::unique_ptr<Mode>> modes{};

    Mode* find(zwlr_output_mode_v1* p) {
        for (auto& m : modes)
            if (m->proxy == p) return m.get();
        return nullptr;
    }
    Mode* preferred() {
        for (auto& m : modes)
            if (m->preferred) return m.get();
        if (auto* m = find(current)) return m;
        return modes.empty() ? nullptr : modes.front().get();
    }
};

class Wlr final : public Backend {
public:
    Wlr() {
        dpy_ = wl_display_connect(nullptr);
        if (!dpy_) {
            std::println(stderr, "cannot connect to the Wayland display");
            return;
        }
        registry_ = wl_display_get_registry(dpy_);
        wl_registry_add_listener(registry_, &registry_listener, this);
        wl_display_roundtrip(dpy_); // globals
        if (!manager_) {
            std::println(stderr, "this compositor does not support wlr-output-management");
            return;
        }
        wl_display_roundtrip(dpy_); // heads + modes
        wl_display_roundtrip(dpy_);
        changed_ = false;
    }

    ~Wlr() override {
        if (!dpy_) return;
        for (auto& h : heads_) release(h.get());
        if (manager_) zwlr_output_manager_v1_destroy(manager_);
        if (registry_) wl_registry_destroy(registry_);
        wl_display_disconnect(dpy_);
    }

    std::string_view name() const override { return "wlr"; }
    bool native_mirror() const override { return false; }

    std::vector<Output> outputs() override {
        std::vector<Output> out;
        if (!manager_) return out;
        wl_display_roundtrip(dpy_);
        for (auto& h : heads_) {
            Output o;
            o.name = h->name;
            o.description = h->description;
            o.enabled = h->enabled;
            o.x = h->x;
            o.y = h->y;
            o.scale = h->scale;
            auto* cur = h->enabled ? h->find(h->current) : nullptr;
            auto* pref = h->preferred();
            if (!cur) cur = pref;
            if (cur) o.width = cur->width, o.height = cur->height, o.refresh = cur->refresh / 1000.0;
            if (pref) o.pref_width = pref->width, o.pref_height = pref->height;
            out.push_back(std::move(o));
        }
        return out;
    }

    bool configure(const std::vector<OutputConfig>& cfgs) override {
        if (!manager_) return false;
        // a configuration is cancelled if outputs changed under it; retry with a fresh serial
        for (int attempt = 0; attempt < 3; ++attempt) {
            wl_display_roundtrip(dpy_);
            auto* conf = zwlr_output_manager_v1_create_configuration(manager_, serial_);
            std::vector<zwlr_output_configuration_head_v1*> conf_heads;

            // the protocol requires every head to be listed
            for (auto& h : heads_) {
                auto it = std::ranges::find(cfgs, h->name, &OutputConfig::name);
                bool enable = it != cfgs.end() ? it->enabled : h->enabled;
                if (!enable) {
                    zwlr_output_configuration_v1_disable_head(conf, h->proxy);
                    continue;
                }
                auto* ch = zwlr_output_configuration_v1_enable_head(conf, h->proxy);
                conf_heads.push_back(ch);
                if (it == cfgs.end()) {
                    // untouched: restate what it has
                    if (auto* m = h->find(h->current)) zwlr_output_configuration_head_v1_set_mode(ch, m->proxy);
                    zwlr_output_configuration_head_v1_set_position(ch, h->x, h->y);
                    zwlr_output_configuration_head_v1_set_scale(ch, wl_fixed_from_double(h->scale));
                    continue;
                }
                if (auto* m = h->preferred()) zwlr_output_configuration_head_v1_set_mode(ch, m->proxy);
                zwlr_output_configuration_head_v1_set_position(ch, it->x, it->y);
                zwlr_output_configuration_head_v1_set_scale(ch, wl_fixed_from_double(it->scale));
            }

            int result = 0; // 1 succeeded, 2 failed, 3 cancelled
            zwlr_output_configuration_v1_add_listener(conf, &config_listener, &result);
            zwlr_output_configuration_v1_apply(conf);
            while (result == 0 && wl_display_dispatch(dpy_) >= 0) {}
            zwlr_output_configuration_v1_destroy(conf);
            for (auto* ch : conf_heads) zwlr_output_configuration_head_v1_destroy(ch);

            if (result == 1) return true;
            if (result != 3) break;
        }
        std::println(stderr, "the compositor rejected the output configuration");
        return false;
    }

    int event_fd() override { return dpy_ ? wl_display_get_fd(dpy_) : -1; }

    void before_poll() override {
        wl_display_dispatch_pending(dpy_);
        wl_display_flush(dpy_);
    }

    bool read() override { return wl_display_dispatch(dpy_) >= 0 && manager_; }

    bool changed() override { return std::exchange(changed_, false); }

private:
    void release(Head* h) {
        for (auto& m : h->modes) release(m.get());
        if (version_ >= 3) zwlr_output_head_v1_release(h->proxy);
        else zwlr_output_head_v1_destroy(h->proxy);
    }
    void release(Mode* m) {
        if (version_ >= 3) zwlr_output_mode_v1_release(m->proxy);
        else zwlr_output_mode_v1_destroy(m->proxy);
    }

    // defined after the class: the lambdas need Wlr to be complete
    static const wl_registry_listener registry_listener;
    static const zwlr_output_manager_v1_listener manager_listener;
    static const zwlr_output_head_v1_listener head_listener;
    static const zwlr_output_mode_v1_listener mode_listener;
    static const zwlr_output_configuration_v1_listener config_listener;

    wl_display* dpy_ = nullptr;
    wl_registry* registry_ = nullptr;
    zwlr_output_manager_v1* manager_ = nullptr;
    uint32_t version_ = 0;
    uint32_t serial_ = 0;
    std::vector<std::unique_ptr<Head>> heads_;
    bool changed_ = false;
};

static Head* head_of(zwlr_output_head_v1* p) { return static_cast<Head*>(zwlr_output_head_v1_get_user_data(p)); }

const wl_registry_listener Wlr::registry_listener = {
    .global =
        [](void* data, wl_registry* reg, uint32_t id, const char* iface, uint32_t version) {
            auto* self = static_cast<Wlr*>(data);
            if (std::string_view(iface) != zwlr_output_manager_v1_interface.name) return;
            self->version_ = std::min<uint32_t>(version, 4);
            self->manager_ = static_cast<zwlr_output_manager_v1*>(
                wl_registry_bind(reg, id, &zwlr_output_manager_v1_interface, self->version_));
            zwlr_output_manager_v1_add_listener(self->manager_, &manager_listener, self);
        },
    .global_remove = [](void*, wl_registry*, uint32_t) {},
};

const zwlr_output_manager_v1_listener Wlr::manager_listener = {
    .head =
        [](void* data, zwlr_output_manager_v1*, zwlr_output_head_v1* proxy) {
            auto* self = static_cast<Wlr*>(data);
            auto& h = self->heads_.emplace_back(std::make_unique<Head>(Head{.proxy = proxy}));
            zwlr_output_head_v1_add_listener(proxy, &head_listener, self);
            zwlr_output_head_v1_set_user_data(proxy, h.get());
        },
    .done =
        [](void* data, zwlr_output_manager_v1*, uint32_t serial) {
            auto* self = static_cast<Wlr*>(data);
            self->serial_ = serial;
            self->changed_ = true;
        },
    .finished =
        [](void* data, zwlr_output_manager_v1* mgr) {
            auto* self = static_cast<Wlr*>(data);
            zwlr_output_manager_v1_destroy(mgr);
            self->manager_ = nullptr;
        },
};

const zwlr_output_head_v1_listener Wlr::head_listener = {
    .name = [](void*, zwlr_output_head_v1* p, const char* v) { head_of(p)->name = v; },
    .description = [](void*, zwlr_output_head_v1* p, const char* v) { head_of(p)->description = v; },
    .physical_size = [](void*, zwlr_output_head_v1*, int32_t, int32_t) {},
    .mode =
        [](void* data, zwlr_output_head_v1* p, zwlr_output_mode_v1* mp) {
            auto* self = static_cast<Wlr*>(data);
            auto* h = head_of(p);
            auto& m = h->modes.emplace_back(std::make_unique<Mode>(Mode{.proxy = mp, .head = h, .version = self->version_}));
            zwlr_output_mode_v1_add_listener(mp, &mode_listener, m.get());
        },
    .enabled = [](void*, zwlr_output_head_v1* p, int32_t v) { head_of(p)->enabled = v; },
    .current_mode = [](void*, zwlr_output_head_v1* p, zwlr_output_mode_v1* m) { head_of(p)->current = m; },
    .position = [](void*, zwlr_output_head_v1* p, int32_t x, int32_t y) { head_of(p)->x = x, head_of(p)->y = y; },
    .transform = [](void*, zwlr_output_head_v1*, int32_t) {},
    .scale = [](void*, zwlr_output_head_v1* p, wl_fixed_t s) { head_of(p)->scale = wl_fixed_to_double(s); },
    .finished =
        [](void* data, zwlr_output_head_v1* p) {
            auto* self = static_cast<Wlr*>(data);
            auto it = std::ranges::find_if(self->heads_, [p](auto& h) { return h->proxy == p; });
            if (it == self->heads_.end()) return;
            self->release(it->get());
            self->heads_.erase(it);
            self->changed_ = true;
        },
    .make = [](void*, zwlr_output_head_v1*, const char*) {},
    .model = [](void*, zwlr_output_head_v1*, const char*) {},
    .serial_number = [](void*, zwlr_output_head_v1*, const char*) {},
    .adaptive_sync = [](void*, zwlr_output_head_v1*, uint32_t) {},
};

const zwlr_output_mode_v1_listener Wlr::mode_listener = {
    .size = [](void* d, zwlr_output_mode_v1*, int32_t w, int32_t h) { static_cast<Mode*>(d)->width = w, static_cast<Mode*>(d)->height = h; },
    .refresh = [](void* d, zwlr_output_mode_v1*, int32_t r) { static_cast<Mode*>(d)->refresh = r; },
    .preferred = [](void* d, zwlr_output_mode_v1*) { static_cast<Mode*>(d)->preferred = true; },
    .finished =
        [](void* d, zwlr_output_mode_v1* p) {
            auto* m = static_cast<Mode*>(d);
            auto* h = m->head;
            if (h->current == p) h->current = nullptr;
            if (m->version >= 3) zwlr_output_mode_v1_release(p);
            else zwlr_output_mode_v1_destroy(p);
            std::erase_if(h->modes, [p](auto& x) { return x->proxy == p; });
        },
};

const zwlr_output_configuration_v1_listener Wlr::config_listener = {
    .succeeded = [](void* d, zwlr_output_configuration_v1*) { *static_cast<int*>(d) = 1; },
    .failed = [](void* d, zwlr_output_configuration_v1*) { *static_cast<int*>(d) = 2; },
    .cancelled = [](void* d, zwlr_output_configuration_v1*) { *static_cast<int*>(d) = 3; },
};

} // namespace

std::unique_ptr<Backend> make_wlr() { return std::make_unique<Wlr>(); }

} // namespace beamer
