#pragma once

#include <sys/types.h>

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace beamer {

namespace fs = std::filesystem;

inline constexpr std::array<std::string_view, 4> MODES = {"mirror", "extend", "external", "internal"};

inline bool valid_mode(std::string_view m) {
    for (auto x : MODES)
        if (x == m) return true;
    return false;
}

fs::path config_path();
fs::path state_dir();
fs::path runtime_dir();
fs::path data_dir();

struct Config {
    std::string backend = "auto";        // auto | hyprland | wlr
    std::string internal = "eDP-1";      // laptop panel connector
    double internal_scale = 1.25;        // scale of the laptop panel
    std::string default_mode = "mirror"; // mode applied when a screen is plugged in
    bool remember_mode = false;          // reuse the last mode picked with `beamer <mode>` instead of default_mode
    bool switch_audio = true;            // move sound to HDMI while connected, back afterwards
    bool notify = true;                  // desktop notification on changes
};

Config load_config();

std::optional<std::string> read_state();
void write_state(std::string_view mode);

int connect_unix(const fs::path& path); // -1 on failure

// Fire-and-forget child process; returns false if it could not be started.
bool spawn(const std::vector<std::string>& argv, pid_t* pid = nullptr);
void notify(const Config& cfg, const std::string& msg);

// One connected output as the compositor reports it.
struct Output {
    std::string name, description;
    bool enabled = true;
    int x = 0, y = 0;
    int width = 0, height = 0; // current mode, pixels
    double refresh = 0;
    double scale = 1;
    int pref_width = 0, pref_height = 0; // preferred mode, pixels
    std::string mirror_of;
};

// What we want an output to look like.
struct OutputConfig {
    std::string name{};
    bool enabled = true;
    int x = 0, y = 0;
    bool auto_position = false;
    double scale = 1;
    std::string mirror{}; // only honoured by backends with native mirroring
};

} // namespace beamer
