# PW-memu - player widget  menu

> A minimalist GTK widget for controlling your music player — and not only that.

![License](https://img.shields.io/badge/license-MIT-green)
![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20Android%20(Termux)-blue)
![Language](https://img.shields.io/badge/language-C%2B%2B17-orange)

---

## Features

- Spinning disc with album art
- Track title and artist in real time
- Playback controls (previous, play/pause, next)
- Timeline with seeking
- Volume control
- Floating window mode with drag support (`--swim`)
- Dark gradient background with rounded corners
- HTTP album art support (via curl + ffmpeg)

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
pacman -S gtk3 cairo playerctl curl ffmpeg
```

* Gentoo
```sh
emerge gtk+:3 cairo playerctl curl ffmpeg
```
* Termux (Android)
```sh
pkg install gtk3 cairo playerctl curl ffmpeg
```

---

## Build

```bash
git clone https://github.com/kuzmak161-creator/PW-menu
cd PW-menu
make
```

Or manually:

```bash
g++ -o build/PW-menu player.cpp \
    $(pkg-config --cflags --libs gtk+-3.0 cairo) \
    -std=c++17 -pthread -O2
```

---

## Install

```bash
make install
```

Installs to:
- `/usr/local/bin` on Linux
- `$PREFIX/bin` on Android (Termux)

---

## Usage
* Default (ALSA)
```bash
PW-menu
```

* Floating window + PipeWire
```sh
PW-menu --swim --pipewire
```
* PulseAudio
```sh
PW-menu --pulse
```

* Help
```sh
PW-menu --help
```

---

## i3blocks Integration

Add to `~/.config/i3blocks/config`:

```ini
[PW-menu]
command=$HOME/.config/i3blocks/PW-menu_btn.sh
interval=2
color=#a4c639
```

Create `~/.config/i3blocks/PW-menu_btn.sh`:

```bash
#!/bin/bash
if [ "$BLOCK_BUTTON" = "1" ]; then
    if pgrep -x "PW-menu" > /dev/null; then
        pkill -x "PW-menu"
    else
        PW-menu &
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
chmod +x ~/.config/i3blocks/wpc_btn.sh
```

---

## Tested On

| Platform             | Status |
|----------------------|--------|
| Termux               | works✅|
| Gentoo               | works✅|


---

## License

MIT
