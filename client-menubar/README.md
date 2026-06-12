# Sony Headphones Bar — macOS menu bar client

A native NSMenu menu bar app on the same `libmdr` engine as the TUI: live
battery % in the bar, dropdown with noise control (Off / NC / Ambient +
level slider), EQ presets, volume slider, device picker, launch at login.

## Build + install

Built as part of the normal tree (`MDR_BUILD_CLIENT_MENUBAR`, macOS only):

```sh
cmake --build build-release --target SonyHeadphonesMenuBar
./scripts/install-macos.sh   # copies it to ~/Applications
```

First connect prompts for Bluetooth permission for "Sony Headphones Bar"
(it's a real bundle, unlike the TUI's Terminal-launcher .app — permissions
are its own). Ad-hoc signed: a rebuild may re-prompt once after reinstall.

## Behavior

- Pick your headphones once under **Device** — remembered across launches,
  auto-connects on start, auto-reconnects every ~15 s if the link drops
  (headphones out of range / powered off → icon dims, % disappears).
- **Disconnect** stops auto-reconnect for the session; picking a device
  re-enables it.
- Controls are support-gated like the TUI; XM5 notes apply (EQ limited to
  the working preset set, no power-off over MDR).
- Don't run two clients (TUI + bar) against the headphones at once — the
  device speaks to one MDR client at a time.

## Design notes

- Objective-C++ talking to `mdr::MDRHeadphones` directly (the C FFI lacks
  settings, same reason the TUI is C++). Reuses `client-tui/Connection.*`.
- Single-threaded like the TUI: AppKit's main run loop delivers IOBluetooth
  callbacks; an NSTimer drives the libmdr pump. The timer is scheduled in
  `NSRunLoopCommonModes` — default-mode timers pause while a menu is open,
  which is exactly when settings change and commits must flush.
- Same protocol rules as the TUI: sync once after init, never poll battery
  (sequence-number desync), commit only when dirty.
