#!/usr/bin/env bash
# Installs beamer: command in ~/.local/bin, config, and Hyprland hook.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
hypr="${XDG_CONFIG_HOME:-$HOME/.config}/hypr"
conf="${XDG_CONFIG_HOME:-$HOME/.config}/beamer"

mkdir -p "$HOME/.local/bin" "$conf"
ln -sf "$here/beamer" "$HOME/.local/bin/beamer"
[[ -f "$conf/config.toml" ]] || cp "$here/config.example.toml" "$conf/config.toml"

# monitors.lua: the old catch-all rule forced scale 1.25 onto every external screen
if ! grep -q 'scale = 1 is valid' "$hypr/monitors.lua" 2>/dev/null; then
    [[ -f "$hypr/monitors.lua" ]] && cp "$hypr/monitors.lua" "$hypr/monitors.lua.bak-beamer"
    cp "$here/monitors.lua" "$hypr/monitors.lua"
fi

ln -sf "$here/hypr/beamer.lua" "$hypr/beamer.lua"
grep -q 'require("beamer")' "$hypr/hyprland.lua" || echo 'require("beamer")' >> "$hypr/hyprland.lua"

hyprctl reload >/dev/null 2>&1 || true
pgrep -f "python3 .*beamer daemon" >/dev/null || (setsid -f "$HOME/.local/bin/beamer" daemon >/dev/null 2>&1)
echo "beamer installed. Plug in HDMI, or press SUPER+P to cycle modes."
