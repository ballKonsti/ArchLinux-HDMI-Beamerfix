# hdmi-beamer

Plug-and-play HDMI for Hyprland (Lua config) on Arch. Plug in a beamer or monitor
and it just works: the picture appears mirrored, the scale fits, the sound goes over HDMI.
Unplug it and everything returns to the laptop.

## What was wrong

| Symptom | Cause | Fix |
|---|---|---|
| Beamer shows nothing / broken picture | `monitors.lua` forced `scale = 1.25` on **every** screen; 1.25 doesn't divide typical beamer resolutions (1024×768, 1280×800) | 1.25 only for `eDP-1`, external screens get 1 (1.5 for 4K in extend mode) |
| Beamer shows an empty desktop | `position = "auto"` **extends** the desktop | defaults to **mirror** on plug-in |
| Sound stays on the laptop | PipeWire doesn't switch automatically | default sink moves to the plugged HDMI sink and back |
| Laptop stays black after unplugging (in "external only") | panel was disabled | daemon re-enables the panel on unplug |

## Install

```sh
./install.sh
```

The installer:
- links `beamer` to `~/.local/bin/beamer`
- creates `~/.config/beamer/config.toml`
- replaces `~/.config/hypr/monitors.lua` (backup: `monitors.lua.bak-beamer`)
- adds `require("beamer")` to `hyprland.lua` (autostarts the daemon, adds keybinds)

Remove with `./uninstall.sh`.

## Use

| Key / command | Effect |
|---|---|
| plug in HDMI | applies `default_mode` (mirror) |
| `SUPER+P` / `beamer cycle` | mirror → extend → external only → laptop only |
| `SUPER+SHIFT+P` / `beamer mirror` | back to mirror |
| `beamer extend` / `external` / `internal` | set a mode directly |
| `beamer status` | show screens, mode, audio output |

Settings: `~/.config/beamer/config.toml` (see `config.example.toml`).

## How it works

`beamer daemon` listens on Hyprland's event socket (`.socket2.sock`) for
`monitoradded` / `monitorremoved` and sets monitor rules at runtime with
`hyprctl eval 'hl.monitor({...})'` (`hyprctl keyword` doesn't work with the Lua config).
Audio is switched with `pactl`, choosing the HDMI sink whose port is actually plugged.

## Still black?

- Run `beamer status` — is the HDMI output listed at all? If not, check
  `cat /sys/class/drm/card1-HDMI-A-1/status` (cable/adapter/beamer input source).
- Some older beamers reject the advertised preferred mode. Force one:
  `hyprctl eval 'hl.monitor({ output = "HDMI-A-1", mode = "1024x768@60", position = "auto", scale = 1 })'`
