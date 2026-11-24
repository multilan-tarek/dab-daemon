# Demo Application Setup

## Requirements

- Linux with a graphical desktop session
- Python 3.11 or newer
- PyQt6 6.7 or newer
- dbus-next 0.2.3 or newer
- mpv available in PATH (only for audio playback)

## Virtual environment

Create a venv in the project root folder:

```sh
python -m venv .venv
```

Activate the venv:

```sh
source .venv/bin/activate
```

**OR** if you use fish:

```sh
source .venv/bin/activate.fish
```

## Install requirements

Install the python requirements to the venv:

```sh
pip install tools/requirements.txt
```

If you need audio, `mpv` is required.

Arch:

```sh
sudo pacman -S mpv
```

Debian/Ubuntu:
```sh
sudo apt install mpv
```

Fedora:
```sh
sudo dnf install mpv
```