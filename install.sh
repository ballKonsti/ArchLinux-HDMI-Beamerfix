#!/usr/bin/env bash
# Builds and installs beamer: binary, Quickshell popup, config, and the
# SUPER+P keybind + autostart for your compositor. Safe to re-run to update.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
cfg_home="${XDG_CONFIG_HOME:-$HOME/.config}"
bin="$HOME/.local/bin/beamer"
ui="${XDG_DATA_HOME:-$HOME/.local/share}/beamer/ui"

say()  { printf '\033[1;34m::\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m!!\033[0m %s\n' "$*"; }

# ---------------------------------------------------------------- compositor

if [[ -n "${HYPRLAND_INSTANCE_SIGNATURE:-}" ]]; then wm=hyprland
elif [[ -n "${NIRI_SOCKET:-}" ]]; then wm=niri
elif [[ -n "${SWAYSOCK:-}" ]]; then wm=sway
elif [[ "${XDG_CURRENT_DESKTOP:-}" == *river* ]]; then wm=river
else wm=other
fi
say "compositor: $wm"

# ---------------------------------------------------------------- dependencies

missing=()
command -v make >/dev/null || missing+=(make)
command -v g++ >/dev/null || missing+=(gcc)
command -v pkg-config >/dev/null || missing+=(pkgconf)
command -v wayland-scanner >/dev/null || missing+=(wayland)
pkg-config --exists libpulse 2>/dev/null || missing+=(libpulse)
pkg-config --exists wayland-client 2>/dev/null || missing+=(wayland)
[[ -f /usr/include/nlohmann/json.hpp ]] || missing+=(nlohmann-json)
[[ -f /usr/include/toml++/toml.hpp ]] || missing+=(tomlplusplus)
command -v qs >/dev/null || missing+=(quickshell)
command -v notify-send >/dev/null || missing+=(libnotify)
# compositors without native mirroring use wl-mirror
[[ $wm != hyprland ]] && ! command -v wl-mirror >/dev/null && missing+=(wl-mirror)

if ((${#missing[@]})); then
    mapfile -t missing < <(printf '%s\n' "${missing[@]}" | sort -u)
    if command -v pacman >/dev/null; then
        say "installing: ${missing[*]}"
        sudo pacman -S --needed "${missing[@]}"
    else
        warn "please install: ${missing[*]} (names are Arch packages), then re-run"
        exit 1
    fi
fi

# ---------------------------------------------------------------- build + install

say "building"
make -C "$here" -j"$(nproc)" >/dev/null

say "installing binary to $bin"
pkill -x beamer 2>/dev/null || true                   # running daemon (C++)
pkill -f "python3 .*beamer daemon" 2>/dev/null || true # pre-C++ daemon
pkill -f "qs -p $ui" 2>/dev/null || true              # popup, so it reloads
rm -f "$bin" # may be a symlink from an older install
install -Dm755 "$here/build/beamer" "$bin"

say "installing popup to $ui"
rm -rf "$ui"
mkdir -p "$ui"
cp "$here"/ui/*.qml "$ui/"

mkdir -p "$cfg_home/beamer"
[[ -f "$cfg_home/beamer/config.toml" ]] || cp "$here/config.example.toml" "$cfg_home/beamer/config.toml"

# a snippet with the binary path filled in
render() { sed "s|@BIN@|$bin|g" "$here/$1"; }

# ---------------------------------------------------------------- keybind + autostart

case $wm in
hyprland)
    hypr="$cfg_home/hypr"
    # the old catch-all monitor rule forced scale 1.25 onto every external screen
    if ! grep -q 'scale = 1 is valid' "$hypr/monitors.lua" 2>/dev/null; then
        [[ -f "$hypr/monitors.lua" ]] && cp "$hypr/monitors.lua" "$hypr/monitors.lua.bak-beamer"
        cp "$here/monitors.lua" "$hypr/monitors.lua"
    fi
    rm -f "$hypr/beamer.lua"
    render hypr/beamer.lua > "$hypr/beamer.lua"
    grep -q 'require("beamer")' "$hypr/hyprland.lua" || echo 'require("beamer")' >> "$hypr/hyprland.lua"
    hyprctl reload >/dev/null 2>&1 || true
    say "SUPER+P opens the popup (added $hypr/beamer.lua)"
    ;;
sway)
    mkdir -p "$cfg_home/sway/config.d"
    render sway/beamer.conf > "$cfg_home/sway/config.d/beamer.conf"
    line="include $cfg_home/sway/config.d/beamer.conf"
    grep -qF "$line" "$cfg_home/sway/config" || echo "$line" >> "$cfg_home/sway/config"
    swaymsg reload >/dev/null 2>&1 || true
    say "SUPER+P opens the popup (added $cfg_home/sway/config.d/beamer.conf)"
    ;;
niri)
    render niri/beamer.kdl > "$cfg_home/niri/beamer.kdl"
    warn "niri: copy the binds from $cfg_home/niri/beamer.kdl into your config.kdl"
    ;;
river)
    warn "river: add these lines to ~/.config/river/init:"
    render river/beamer.sh
    ;;
*)
    warn "unknown compositor: bind SUPER+P to '$bin ui' and autostart '$bin daemon' yourself"
    ;;
esac

# start now, so there's no need to log out
setsid -f "$bin" daemon >/dev/null 2>&1
say "done. Plug in HDMI, or press SUPER+P."
