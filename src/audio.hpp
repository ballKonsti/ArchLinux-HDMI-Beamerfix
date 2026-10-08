#pragma once

#include "common.hpp"

#include <string>

namespace beamer {

// Make the plugged HDMI sink (or the first non-HDMI one) the default sink.
// Waits up to ~3 s for HDMI audio to appear.
void set_audio(const Config& cfg, bool to_hdmi);
std::string default_sink(); // "" if no sound server

} // namespace beamer
