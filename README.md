# PW-menu - player widget menu

> A minimalist GTK menu for controlling your music player — and not only that.

---

![logo](./.photo/logo.svg)

---

![screenshot](./.photo/screenshot.jpg)

---

![License](https://img.shields.io/badge/license-MIT-green)
![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20termux%20(Android)-blue)
![Language](https://img.shields.io/badge/language-C%2B%2B17-orange)

---

## Features

- Spinning disc
- Track title and artist in real time
- Playback controls (previous, play/pause, next)
- Timeline with seeking
- Volume control
- Player switcher (switch between running players on the fly)
- Settings window with live theme switching
- Floating window mode with drag support
- Dark gradient background with rounded corners
- Custom background image support
- HTTP album art support (via curl + ffmpeg)
- Customizable button icons

---

## Themes

| Name      | Accent color |
|-----------|--------------|
| `dark`    | #a4c639      |
| `green`   | #39c639      |
| `nord`    | #88c0d0      |
| `gruvbox` | #d79921      |

---

## Audio Driver Support

| Flag          | Driver          | Tool     |
|---------------|-----------------|----------|
| `--alsa`      | ALSA (default)  | `amixer` |
| `--pipewire`  | PipeWire        | `wpctl`  |
| `--pulse`     | PulseAudio      | `pactl`  |

---

## Dependencies

* Arch / Manjaro
```sh
pacman -S gtk3 cairo playerctl curl ffmpeg gcc pkg-config
```

* Gentoo
```sh
emerge gtk+:3 cairo playerctl curl ffmpeg gcc pkg-config
```

* Termux (Android)
```sh
pkg install clang pkg-config gtk3 cairo curl ffmpeg gcc git
```
playerctl is not in the official Termux repositories, build it manually:
```sh
git clone https://github.com/altdesktop/playerctl && cd playerctl && make && make install
```
mpv-mpris (optional, for mpv support):
```sh
git clone https://github.com/hoyon/mpv-mpris && cd mpv-mpris && make && make install
```

* Debian
```sh
apt install build-essential pkg-config libgtk-3-dev libcairo2-dev libgdk-pixbuf-2.0-dev playerctl curl ffmpeg
```

---

## Build

```bash
git clone https://github.com/kuzmak161-creator/PW-menu
cd PW-menu
make
```

---

## Install

```bash
make install
```

Installs to:
- `/usr/local/bin` on Linux
- `$PREFIX/bin` on Android (Termux)

Optional — install mpv-mpris (prompted automatically):
```bash
make mpv-mpris
```

---

## Usage

* Default (ALSA)
```sh
pw-menu
```

* PipeWire
```sh
pw-menu --pipewire
```

* PulseAudio
```sh
pw-menu --pulse
```

* Floating window
```sh
pw-menu --swim
```

* Open settings
```sh
pw-menu --settings
```
or
```sh
pw-menu -s
```

* Specific player
```sh
pw-menu --player audacious
```

* Help
```sh
pw-menu --help
```
or
```sh
pw-menu -h
```
* version 
```sh
pw-menu -v
```
or
```sh
pw-menu -version 
```

---

## Settings

Settings are available via `pw-menu --settings` or the ⚙ button inside the widget.

| Option           | Description                        |
|------------------|------------------------------------|
| Theme            | dark / green / nord / gruvbox      |
| Visualizer       | Spinning disc or square cover art  |
| Audio driver     | ALSA / PipeWire / PulseAudio       |
| Background image | Custom image path                  |
| Button icons     | Custom prev / play / next symbols  |

Config is saved to `~/.config/pw-menu/config.json`.

---

## i3blocks Integration

Add to `~/.config/i3blocks/config`:

```ini
[PW-menu]
command=~/.config/i3blocks/pw-menu_btn.sh
interval=2
color=#a4c639
```

Create `~/.config/i3blocks/pw-menu_btn.sh`:

```bash
#!/bin/bash
if [ "$BLOCK_BUTTON" = "1" ]; then
    if pgrep -x "pw-menu" > /dev/null; then
        pkill -x "pw-menu"
    else
        pw-menu &
    fi
fi

STATUS=$(playerctl status 2>/dev/null)
TITLE=$(playerctl metadata title 2>/dev/null | cut -c1-20)

if [ "$STATUS" = "Playing" ]; then
    echo "* $TITLE"
elif [ "$STATUS" = "Paused" ]; then
    echo "|| $TITLE"
else
    echo "[PW-menu]"
fi
```

```bash
chmod +x ~/.config/i3blocks/pw-menu_btn.sh
```

---

## Project Structure

```
PW-menu/
├── common.h       — shared structures, declarations
├── main.cpp       — window, event loop, settings dialog
├── player.cpp     — playerctl listener, cover loading, config
├── widget.cpp     — UI layout, drawing, visualizer
├── Makefile
└── README.md
```

---

## Tested On

| Platform | Architecture | Status  |
|----------|--------------|---------|
| Termux   | aarch64      | works ✅ |
| Gentoo   | x86_64       | works ✅ |
| Debian   | aarch64      | works ✅ |

---

## License

MIT
