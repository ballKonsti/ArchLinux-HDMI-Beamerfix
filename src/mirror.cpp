// Mirroring for compositors without native support: a fullscreen wl-mirror
// window on the external output. Its pid lives in a file so any beamer
// process (daemon or one-shot command) can stop it.

#include "mirror.hpp"

#include <csignal>
#include <format>
#include <fstream>
#include <string>

namespace beamer {

static fs::path pid_path() { return runtime_dir() / "beamer-mirror.pid"; }

static bool is_wl_mirror(pid_t pid) {
    std::ifstream comm(std::format("/proc/{}/comm", pid));
    std::string name;
    return comm >> name && name == "wl-mirror";
}

void mirror_stop() {
    std::ifstream f(pid_path());
    pid_t pid = 0;
    if (f >> pid && pid > 0 && is_wl_mirror(pid)) kill(pid, SIGTERM);
    std::error_code ec;
    fs::remove(pid_path(), ec);
}

bool mirror_start(const std::string& source, const std::string& target) {
    mirror_stop();
    pid_t pid;
    if (!spawn({"wl-mirror", "--fullscreen-output", target, source}, &pid)) return false;
    std::ofstream(pid_path(), std::ios::trunc) << pid;
    return true;
}

} // namespace beamer
