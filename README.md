# sonytuibar

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
![Platform: macOS](https://img.shields.io/badge/platform-macOS-lightgrey.svg)

A btop-style terminal client and a native macOS menu bar app for Sony WH-1000XM5 headphones.

![Sony Headphones Bar — the macOS menu bar app](assets/menubar.png)

A modified version of [Plutoberth's original SonyHeadphonesClient](https://github.com/Plutoberth/SonyHeadphonesClient)
and [mos9527 & Amr Satrio's SonyHeadphonesClient rewrite](https://github.com/mos9527/SonyHeadphonesClient) —
made for my specific needs and platforms.

I'll keep updating it as I work on it, but it's been working well for me, so I'll probably
leave it as-is for a while.

## Roadmap

- Linux support + Wayland bar (Waybar / Hyprland module)
- Legacy (`v1` protocol) device support — WH-1000XM4 / XM3

## Compatibility

- macOS — terminal (TUI)
- macOS — menu bar app

---

> Everything below this line is maintainer/developer notes.

## Install & use (macOS)

Build the release binaries, then run the install script:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DMDR_ENABLE_CODEGEN=OFF -DMDR_BUILD_CLIENT=OFF
cmake --build build-release --target SonyHeadphonesClientTUI SonyHeadphonesMenuBar
./scripts/install-macos.sh
```

This installs:

- `sonytui` on your `PATH` — run it from any terminal.
- **Sony Headphones TUI.app** in `~/Applications` (launches the TUI in Terminal).
- **Sony Headphones Bar.app** in `~/Applications` — the menu bar app.

Keybinds and per-client behavior live in the sub-READMEs:
[`client-tui/README.md`](client-tui/README.md) and [`client-menubar/README.md`](client-menubar/README.md).

## What's in here

- `client-tui/` — the FTXUI terminal client (`sonytui`) + the CoreAudio system-audio visualizer.
- `client-menubar/` — the native NSMenu menu bar app (`Sony Headphones Bar`).
- `libmdr/`, `client/` — upstream's MDR protocol engine and reference ImGui/SDL client.

## Quirks & notes

- **EQ presets** on the XM5 are limited to the working set (Off + the firmware presets that
  actually apply); some bands the protocol exposes don't take effect on this model.
- **No power-off over MDR** — the XM5 firmware ignores it, so there's no power control.
- **Never poll battery.** The clients rely on the headphones' push notifications; actively
  polling battery desyncs the protocol sequence numbers and wedges the link.
- **Visualizer permissions:** the visualizer captures system audio, so it needs *System Audio
  Recording* permission. Terminal.app declares the usage string and prompts for it; some
  terminals (Ghostty, WezTerm) don't, so it silently does nothing until you grant the terminal
  that permission manually in **Privacy & Security → Screen & System Audio Recording**.
- **One client at a time.** The headphones speak to a single MDR client — don't run the TUI and
  the menu bar app against them simultaneously.

## Credits

- [Plutoberth](https://github.com/Plutoberth/SonyHeadphonesClient) — the original SonyHeadphonesClient.
- [mos9527](https://github.com/mos9527) & [Amr Satrio (@Amrsatrio)](https://github.com/Amrsatrio) —
  the SonyHeadphonesClient rewrite, the `libmdr` engine, and the reference client this stands on.
- [FTXUI](https://github.com/ArthurSonzogni/FTXUI) (ArthurSonzogni) — the terminal UI library
  behind the TUI.

## License

MIT — see [`LICENSE`](LICENSE). Upstream copyright (mos9527, Amr Satrio) is retained; my additions
(`client-tui`, `client-menubar`) are released under the same license.
