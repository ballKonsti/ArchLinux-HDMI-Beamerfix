#!/usr/bin/env bash
set -euo pipefail
hypr="${XDG_CONFIG_HOME:-$HOME/.config}/hypr"
pkill -f "python3 .*beamer daemon" || true
rm -f "$HOME/.local/bin/beamer" "$hypr/beamer.lua"
sed -i '/require("beamer")/d' "$hypr/hyprland.lua"
[[ -f "$hypr/monitors.lua.bak-beamer" ]] && mv "$hypr/monitors.lua.bak-beamer" "$hypr/monitors.lua"
hyprctl reload >/dev/null 2>&1 || true
echo "beamer removed (config in ~/.config/beamer kept)."
