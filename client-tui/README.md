# SonyHeadphonesClient — TUI

A btop-style terminal client for Sony headphones, built on the same `libmdr`
protocol engine as the reference ImGui client. macOS and Linux only.

![panels: battery, noise control, equalizer, playback]

## Build

Requires CMake ≥ 3.31 and a C++20 compiler. Generated protocol sources are
committed, so LLVM/codegen is not needed:

```sh
cmake -S . -B build -DMDR_ENABLE_CODEGEN=OFF -DMDR_BUILD_CLIENT=OFF
cmake --build build --target SonyHeadphonesClientTUI
```

Linux additionally needs `libbluetooth-dev` and `libdbus-1-dev`.

## Run

Pair the headphones with the OS first, then:

```sh
./build/client-tui/sonytui
```

Pick the device and Connect. On macOS the first run may prompt for
Bluetooth permission for your terminal app.

## Install (macOS)

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DMDR_ENABLE_CODEGEN=OFF -DMDR_BUILD_CLIENT=OFF
cmake --build build-release --target SonyHeadphonesClientTUI
./scripts/install-macos.sh tui
```

The `tui` mode installs just this client. This installs `sonytui` to
`/usr/local/bin` (sudo for the copy) and a
double-clickable **Sony Headphones TUI.app** launcher into `~/Applications`
that opens Terminal running the TUI. Permissions (Bluetooth, audio capture)
attach to Terminal, not the bundle.

## Install (Linux — planned)

Deferred. Intended shape for Arch: a PKGBUILD building `sonytui` plus a
`.desktop` entry (`Exec=xdg-terminal-exec sonytui`, falling back to
`kitty -e sonytui`) so it shows up in launchers under Hyprland.

## Keybinds

| Key | Action |
|-----|--------|
| `n` | cycle noise mode: Off → NC → Ambient |
| `[` `]` | ambient sound level − / + (1–20) |
| `-` `+` | volume − / + (0–30) |
| `,` `.` | EQ preset prev / next |
| `v` | toggle system-audio visualizer (macOS; first use prompts for System Audio Recording — Terminal.app only; Ghostty/WezTerm need a manual grant, see docs/visualizer.md) |
| `q` / Esc | quit |

All controls are support-gated: keys (and their footer hints) only exist if
the connected device advertises the function.

## Supported devices

Anything speaking MDR protocol **v2** (e.g. WH-1000XM5). v1-only devices
(e.g. WF-1000XM4) are not supported by `libmdr` and won't work here until
v1 lands upstream.

Device-tested notes (WH-1000XM5):

- Only EQ presets Off / Bright / Excited / Mellow / Relaxed / Vocal /
  Treble / Bass / Speech are applied by the firmware; the older
  Rock/Pop/Jazz set is silently rejected (same in the reference client),
  so the cycle is limited to the working set.
- Power-off over MDR is not honored — use the physical button (the control
  was removed here on purpose).

## Design notes

- Single-threaded: UI and `libmdr` are interleaved on the main thread via
  `ftxui::Loop::RunOnce()`. On macOS, IOBluetooth delivers RFCOMM callbacks
  on the main thread's run loop, which `Poll()` services — running libmdr
  on a worker thread hangs the connect.
- No periodic battery polling: battery is synced once after init, then
  updated from the device's push notifications. Polling `POWER_GET_STATUS`
  can race a post-change notification and desync the protocol sequence
  number, wedging the link (the reference client avoids it for the same
  reason).
