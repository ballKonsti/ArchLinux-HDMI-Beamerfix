# hdmi-beamer

Plug-and-play HDMI for Wayland tiling compositors. Plug in a projector or
monitor and the picture shows up mirrored at a scale that fits, with sound
going over HDMI. Unplug it and everything goes back to the laptop.

Press **`SUPER+P`** for a small popup to switch between mirror and extend,
and **drag the projector** to wherever it physically stands.

```
┌──────────────────────── Display ────────────────────────┐
│  EPSON PJ · 1280×800                                    │
│ ┌─────────┐ ┌─────────┐ ┌──────────────┐ ┌───────────┐  │
│ │ Mirror  │ │▓Extend▓▓│ │Projector only│ │Laptop only│  │
│ └─────────┘ └─────────┘ └──────────────┘ └───────────┘  │
│                    ┌──────────┐                         │
│                    │ HDMI-A-1 │  ← drag me              │
│                  ┌─┴──────────┴─┐                       │
│                  │    Laptop    │                       │
│                  └──────────────┘                       │
│   Drag the screen to place it · ←↑→↓ · 1–4 · Esc        │
└─────────────────────────────────────────────────────────┘
```

Made for the "walk into a classroom, plug in the beamer, it just works" case.
Written in C++. It talks to the compositor and the sound server directly,
without spawning `hyprctl` or `pactl`, so a command takes about 15 ms.

## Features

- **Auto-configure on hotplug.** A small daemon applies your default mode as soon as a screen is connected, and goes back to the laptop when it's unplugged.
- **Display popup** (Quickshell) on `SUPER+P`:
  - **Modes:** Mirror, Extend, Projector only, Laptop only.
  - **Drag to arrange:** drag the projector around the laptop. It snaps flush to an edge and to start, center or end along it. The position is **remembered per display**, so the classroom projector keeps its spot "above".
  - **Keyboard:** `1`–`4` or `Tab` switch modes, arrow keys move the screen, `Esc` closes.
- **Safe "Projector only".** Choosing it from the popup asks you to confirm on the projector. If you don't confirm within 15 s (say the projector shows nothing), it switches back by itself.
- **Safe scaling.** The laptop keeps its fractional scale. External screens get a scale that divides their resolution evenly (`1`, or `1.5` for 4K), so old 1024×768 projectors don't break.
- **Audio follows the screen.** The default sink moves to the HDMI sink that is actually plugged, and back when you unplug.
- **Notifications** on every change (optional).

## Supported compositors

| Compositor | How | Mirror | Keybind set up by installer |
|---|---|---|---|
| **Hyprland** (Lua config) | IPC socket, `hl.monitor` rules | native | ✅ |
| **sway** / SwayFX | wlr-output-management | via `wl-mirror` | ✅ |
| **niri** | wlr-output-management | via `wl-mirror` | prints a snippet to paste |
| **river**, labwc, wayfire, dwl, … | wlr-output-management | via `wl-mirror` | prints a snippet to paste |

Hyprland is tested daily. The wlr-output-management backend (all other
compositors) builds and follows the protocol, but hasn't been tested on those
compositors yet. Reports welcome.

X11 window managers (i3, bspwm, awesome) are not supported.

## Install

```sh
git clone https://github.com/ballKonsti/ArchLinux-HDMI-Beamerfix.git ~/Projects/hdmi-beamer
cd ~/Projects/hdmi-beamer
./install.sh
```

The installer detects your compositor and does everything:

1. **Dependencies.** On Arch it installs missing packages with `pacman` (asks for sudo): `libpulse wayland nlohmann-json tomlplusplus quickshell libnotify`, plus `wl-mirror` on compositors without native mirroring. Elsewhere it lists what to install.
2. **Builds** the binary and installs it to `~/.local/bin/beamer`.
3. **Installs the popup** to `~/.local/share/beamer/ui`.
4. **Creates the config** `~/.config/beamer/config.toml` if there isn't one yet.
5. **Adds the keybinds and autostart:**
   - **Hyprland:** writes `~/.config/hypr/beamer.lua` and adds `require("beamer")` to `hyprland.lua`. It also replaces `monitors.lua` (backup: `monitors.lua.bak-beamer`) so external screens get a valid scale.
   - **sway:** writes `~/.config/sway/config.d/beamer.conf` and includes it from your config.
   - **niri / river:** prints the lines to paste into your config.
6. **Starts the daemon**, so you don't need to log out.

After pulling changes, run `./install.sh` again. It's safe to re-run.

> The shipped `monitors.lua` assumes the laptop panel is `eDP-1` at scale `1.25`.
> If yours is different, change `internal` / `internal_scale` in the config (and `monitors.lua` on Hyprland).

### Uninstall

```sh
./uninstall.sh
```

This removes the binary, the popup, the keybinds and autostart, and restores
your old `monitors.lua`. Your settings in `~/.config/beamer/` are kept.

## Usage

| Key / command | Effect |
|---|---|
| plug in HDMI | applies `default_mode` (mirror by default) |
| unplug HDMI | back to the laptop screen and laptop audio |
| `SUPER+P` / `beamer ui` | open / close the display popup |
| `SUPER+SHIFT+P` / `beamer mirror` | jump straight back to mirror |
| `beamer extend [WHERE]` | second desktop. `WHERE` is `right`, `left`, `above` or `below`, optionally with an offset (`above:120`) or for one screen (`HDMI-A-1=left`) |
| `beamer external` | projector only, laptop panel off |
| `beamer internal` | laptop only, projector off |
| `beamer cycle` | mirror → extend → external → internal → … |
| `beamer status [--json]` | backend, mode, screens, audio output |
| `beamer watch` | the state as a JSON line on every change (used by the popup) |
| `beamer keep` / `beamer revert` | confirm or undo a change made with `--revert=SECONDS` |
| `beamer daemon` | the hotplug listener (autostarted) |

Every mode accepts `--revert=SECONDS`. The daemon switches back to the previous
mode unless `beamer keep` arrives in time. The popup uses this for "Projector only".

## Configuration

`~/.config/beamer/config.toml` (see [`config.example.toml`](config.example.toml)).
It is re-read on every hotplug and every command, so changes apply without restarting anything.

| Key | Default | Meaning |
|---|---|---|
| `backend` | `"auto"` | `hyprland`, `wlr`, or `auto` to detect it |
| `internal` | `"eDP-1"` | Connector name of the laptop panel |
| `internal_scale` | `1.25` | Scale of the laptop panel only |
| `default_mode` | `"mirror"` | Mode applied on plug-in: `mirror`, `extend`, `external` or `internal` |
| `remember_mode` | `false` | `true`: on plug-in, reuse the last mode you picked instead of `default_mode` |
| `switch_audio` | `true` | Move sound to HDMI while connected, and back afterwards |
| `notify` | `true` | Show a desktop notification on every change |

Remembered positions are stored in `~/.local/state/beamer/layouts.json`, keyed by the display's name (e.g. `"EPSON PJ"`).

## How it works

```
SUPER+P ─▶ beamer ui ─▶ Quickshell popup ◀── beamer watch (JSON on every change)
                              │ beamer extend HDMI-A-1=above:128
                              ▼
            beamer daemon ── control socket $XDG_RUNTIME_DIR/beamer-$WAYLAND_DISPLAY.sock
              ├─ Hyprland: .socket2.sock events, one `eval hl.monitor(...)` per change
              ├─ others:   wlr-output-management (hotplug + atomic apply), wl-mirror for mirroring
              └─ audio:    libpulse, default sink → plugged HDMI port
```

- **Hotplug.** The daemon diffs the list of connected outputs on every event. Turning a screen off (which some compositors report as "removed") is not mistaken for unplugging it, and turning it back on isn't mistaken for a new plug-in.
- **Positions** are computed by beamer in logical pixels (resolution ÷ scale) from the side and offset you chose. They're then normalised so no coordinate is negative.
- **Single round trip.** On Hyprland, all monitor rules go in one `eval` request on the command socket. That's what `hyprctl eval` does, and `hyprctl keyword` doesn't work with the Lua config. On the other compositors, one wlr-output-management configuration is applied atomically.
- **Popup safety.** The popup closes itself *before* any mode that turns a screen off. Hyprland 0.56 crashes if the monitor under a focused layer surface is disabled.

## Troubleshooting

**`SUPER+P` does nothing.**
- Run `beamer ui` in a terminal to see the error. Usually `qs` (Quickshell) is missing.
- The popup's own log is at `/run/user/$UID/quickshell/`.

**Projector stays black.**
- Run `beamer status`. Is the output listed at all? If not, it's the cable, the adapter or the projector's input source. Check with `cat /sys/class/drm/card*-HDMI-A-1/status`.
- Some older projectors reject the preferred mode. On Hyprland, force one:
  ```sh
  hyprctl eval 'hl.monitor({ output = "HDMI-A-1", mode = "1024x768@60", position = "auto", scale = 1 })'
  ```

**Nothing happens on plug-in.** Check the daemon with `pgrep -a beamer`. If it isn't running, run `beamer daemon` in a terminal to see its output.

**Sound doesn't switch.** Run `pactl list sinks short` with the cable plugged in. If no HDMI sink shows up, pick another card profile in `pavucontrol` → Configuration.

**Mirror shows a warning on sway/niri.** Install `wl-mirror`.

## Development

```sh
make          # build/beamer
make test     # layout math tests
```

```
src/main.cpp        CLI, modes, daemon, control socket
src/hyprland.cpp    Hyprland backend
src/wlr.cpp         wlr-output-management backend (sway, niri, river, …)
src/layout.cpp      placement math + remembered positions
src/audio.cpp       libpulse sink switching
src/mirror.cpp      wl-mirror for compositors without native mirroring
ui/                 Quickshell popup (shell.qml, Popup.qml, Arrange.qml, …)
protocol/           vendored wlr-output-management XML
hypr/ sway/ niri/ river/   keybind + autostart snippets (@BIN@ is filled in by install.sh)
tests/              layout tests
```
