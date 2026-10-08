#!/usr/bin/env bash
# Removes beamer. Your settings in ~/.config/beamer are kept.
set -euo pipefail
cfg_home="${XDG_CONFIG_HOME:-$HOME/.config}"
ui="${XDG_DATA_HOME:-$HOME/.local/share}/beamer"

pkill -x beamer 2>/dev/null || true
pkill -f "qs -p $ui/ui" 2>/dev/null || true
rm -f "$HOME/.local/bin/beamer"
rm -rf "$ui"

hypr="$cfg_home/hypr"
if [[ -f "$hypr/hyprland.lua" ]]; then
    rm -f "$hypr/beamer.lua"
    sed -i '/require("beamer")/d' "$hypr/hyprland.lua"
    [[ -f "$hypr/monitors.lua.bak-beamer" ]] && mv "$hypr/monitors.lua.bak-beamer" "$hypr/monitors.lua"
    hyprctl reload >/dev/null 2>&1 || true
fi
if [[ -f "$cfg_home/sway/config" ]]; then
    rm -f "$cfg_home/sway/config.d/beamer.conf"
    sed -i '\|config.d/beamer.conf|d' "$cfg_home/sway/config"
    swaymsg reload >/dev/null 2>&1 || true
fi
rm -f "$cfg_home/niri/beamer.kdl"

echo "beamer removed (settings in ~/.config/beamer kept)."
