#pragma once

#include "common.hpp"

#include <memory>
#include <string_view>
#include <vector>

namespace beamer {

// A compositor we can query and reconfigure.
class Backend {
public:
    virtual ~Backend() = default;

    virtual std::string_view name() const = 0;
    // true if the compositor can mirror outputs itself (otherwise wl-mirror is used)
    virtual bool native_mirror() const = 0;

    // All connected outputs, including disabled ones.
    virtual std::vector<Output> outputs() = 0;
    // Apply a full set of output configs atomically where possible.
    virtual bool configure(const std::vector<OutputConfig>& cfgs) = 0;

    // Hotplug notifications for the daemon. event_fd() is polled; when it is
    // readable call read() (false = compositor went away). changed() reports
    // and clears whether outputs may have been added or removed.
    virtual int event_fd() = 0;
    virtual void before_poll() {}
    virtual bool read() = 0;
    virtual bool changed() = 0;
};

// Picks the backend from config (`backend = "auto"` looks at the environment).
std::unique_ptr<Backend> make_backend(const Config& cfg);

std::unique_ptr<Backend> make_hyprland();
std::unique_ptr<Backend> make_wlr();

} // namespace beamer
