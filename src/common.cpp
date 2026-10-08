#include "common.hpp"

#include <toml++/toml.hpp>

#include <spawn.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <fstream>
#include <print>

extern char** environ;

namespace beamer {

static fs::path xdg_dir(const char* var, const char* fallback) {
    if (const char* v = std::getenv(var); v && *v) return v;
    const char* home = std::getenv("HOME");
    return fs::path(home ? home : "") / fallback;
}

fs::path config_path() { return xdg_dir("XDG_CONFIG_HOME", ".config") / "beamer" / "config.toml"; }
fs::path state_dir() { return xdg_dir("XDG_STATE_HOME", ".local/state") / "beamer"; }
fs::path data_dir() { return xdg_dir("XDG_DATA_HOME", ".local/share") / "beamer"; }

fs::path runtime_dir() {
    if (const char* v = std::getenv("XDG_RUNTIME_DIR"); v && *v) return v;
    return std::format("/run/user/{}", getuid());
}

Config load_config() {
    Config cfg;
    auto path = config_path();
    if (!fs::exists(path)) return cfg;
    try {
        auto t = toml::parse_file(path.string());
        cfg.backend = t["backend"].value_or(cfg.backend);
        cfg.internal = t["internal"].value_or(cfg.internal);
        cfg.internal_scale = t["internal_scale"].value_or(cfg.internal_scale);
        cfg.default_mode = t["default_mode"].value_or(cfg.default_mode);
        cfg.remember_mode = t["remember_mode"].value_or(cfg.remember_mode);
        cfg.switch_audio = t["switch_audio"].value_or(cfg.switch_audio);
        cfg.notify = t["notify"].value_or(cfg.notify);
    } catch (const toml::parse_error& e) {
        std::println(stderr, "{}: {} — using defaults", path.string(), e.description());
    }
    if (!valid_mode(cfg.default_mode)) cfg.default_mode = "mirror";
    return cfg;
}

std::optional<std::string> read_state() {
    std::ifstream f(state_dir() / "mode");
    std::string mode;
    if (f >> mode && valid_mode(mode)) return mode;
    return std::nullopt;
}

void write_state(std::string_view mode) {
    std::error_code ec;
    fs::create_directories(state_dir(), ec);
    std::ofstream(state_dir() / "mode", std::ios::trunc) << mode;
}

bool spawn(const std::vector<std::string>& args, pid_t* out) {
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    pid_t pid;
    if (posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.data(), environ) != 0) return false;
    if (out) *out = pid;
    return true;
}

void notify(const Config& cfg, const std::string& msg) {
    if (cfg.notify) spawn({"notify-send", "-a", "beamer", "-i", "video-display", "Display", msg});
    std::println("{}", msg);
    std::fflush(stdout);
}

int connect_unix(const fs::path& path) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::string p = path.string();
    if (p.size() >= sizeof(addr.sun_path)) { close(fd); return -1; }
    std::ranges::copy(p, addr.sun_path);
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) { close(fd); return -1; }
    return fd;
}

} // namespace beamer
