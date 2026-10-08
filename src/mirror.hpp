#pragma once

#include "common.hpp"

#include <string>

namespace beamer {

// Show `source` fullscreen on `target` with wl-mirror (false if it isn't installed).
bool mirror_start(const std::string& source, const std::string& target);
void mirror_stop();

} // namespace beamer
