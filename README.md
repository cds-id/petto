# petto

[![Open source by CDS](https://img.shields.io/badge/Open_Source_by-CDS-F97316?style=for-the-badge)](https://open.ciptadusa.com)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue?style=for-the-badge)](LICENSE)

A lightweight X11 desktop pet that reacts as you type, with a built-in Pomodoro timer.

> An open source project by **Cipta Dua Saudara (CDS)**.
> Authored and maintained by Indra Gunanda &lt;indra.gunanda@ciptadusa.com&gt;.

Pick a **rocket**, **cat**, or **Jarvis-style HUD**. Each idles with its own
animation and reacts to your keystrokes globally (any window). The rocket even
launches to the moon if you type too fast, then respawns. Enable the Pomodoro
timer for focus/break cycles with a full-screen break overlay that nudges you
to step away.

## Features

- Three pet types with distinct idle + typing-reaction animations
  - **rocket** — flame thrust, vibration, "type too fast → launch to moon → reset"
  - **cat** — blink + breathing idle, startled hop on typing
  - **jarvis** — procedural circular HUD: rotating arcs + pulsing core (cyan → amber with activity)
- Global keystroke reaction via the X RECORD extension (reacts to typing in any app)
- Drag the pet anywhere; double-click to open settings
- First-run onboarding dialog
- Pomodoro timer: focus / short break / long break cycles
- Full-screen break block screen (keyboard-grabbed) with gradient backdrop,
  depleting progress ring, and live countdown
- Settings + onboarding rendered with cairo/X11 (no GTK dependency)
- Persistent config at `~/.config/petto/config`

## Build

Requires X11 and a compositor (for transparency).

```sh
sudo apt-get install build-essential pkg-config \
  libx11-dev libxext-dev libxfixes-dev libxrender-dev libxtst-dev libcairo2-dev

make
./petto
```

## Usage

```
petto [--type rocket|cat|jarvis] [--pomodoro|--no-pomodoro]
      [--work MIN] [--short MIN] [--long MIN] [--long-every N]
      [--settings]
```

- Double-click the pet to open the settings dialog.
- CLI flags override the saved config for that run.
- `--settings` opens the dialog on launch.

## Install from .deb

```sh
./packaging/build-deb.sh
sudo dpkg -i dist/petto_*.deb
```

Or grab the `.deb` from the GitHub release (built by CI on tagged versions).

## Configuration

Settings persist to `~/.config/petto/config` (`$XDG_CONFIG_HOME` honored):

```
pet_type=rocket
pomodoro=0
work_min=25
short_min=5
long_min=15
long_every=4
spawn_x=200
spawn_y=200
onboarded=1
```

## Notes

- Requires an X11 session. On Wayland it runs under XWayland; global key
  reactions need the RECORD extension (available under XWayland/X11).
- The pet uses an ARGB visual for true transparency when a compositor is
  running, and falls back to an XShape mask otherwise.

## License

MIT — see [LICENSE](LICENSE).

## Author

Authored and maintained by **Indra Gunanda** &lt;indra.gunanda@ciptadusa.com&gt;.

---

<p align="center">
  An open source project by <a href="https://open.ciptadusa.com"><strong>Cipta Dua Saudara (CDS)</strong></a><br/>
  <a href="https://open.ciptadusa.com">open.ciptadusa.com</a> · <a href="https://ciptadusa.com">ciptadusa.com</a>
</p>
