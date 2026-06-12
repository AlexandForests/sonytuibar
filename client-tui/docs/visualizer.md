# Visualizer viability — macOS + Linux

## Why this can't come from the headphones

The MDR protocol carries control/state only — no PCM, no spectrum, and (on
macOS at least) not even track metadata. A visualizer therefore has to tap the
**source**: the computer's own audio output. That makes it platform-specific
code, fully independent of `libmdr`.

## macOS — viable, shipped as a spike

**API:** CoreAudio process taps (macOS 14.2+):
`CATapDescription` (`initMonoGlobalTapButExcludeProcesses:@[]` = mono mixdown
of the whole system output) → `AudioHardwareCreateProcessTap` → wrap the tap
in a private aggregate device (`kAudioAggregateDeviceTapListKey` +
`kAudioSubTapDriftCompensationKey`) → `AudioDeviceCreateIOProcIDWithBlock` +
`AudioDeviceStart` deliver float samples on a CoreAudio IO thread.

**Permission:** "System Audio Recording" TCC prompt, once. For an unbundled
CLI binary the prompt is attributed to the **terminal app**, not `sonytui`
(same as our Bluetooth permission). Managed in System Settings → Privacy &
Security → Screen & System Audio Recording.

**Threading:** the IO thread writes only a lock-free ring buffer
(single-writer/single-reader, tearing harmless for visualization). libmdr and
FTXUI stay strictly on the main thread — the design constraint from Phase 1 is
untouched.

**Analysis:** main thread drains the newest 1024 samples per frame, Hann
window + `vDSP_fft_zrip` (Accelerate), squared magnitudes folded into 24
log-spaced bands (40 Hz–16 kHz), dB-mapped to [0,1] with per-frame peak decay.
Cost is microseconds per frame; the dominant cost is the ~30 fps TUI redraw
while the visualizer is active (the dashboard otherwise redraws only on state
change since Phase 5).

**Implementation:** `AudioTap_macOS.{hpp,mm}` (`AudioTap`, `SpectrumAnalyzer`),
rendered as braille bars in the playback panel, toggled with `v` (off by
default so the permission prompt never appears unrequested).

Gotcha for future Apple-header use: CoreAudio/Accelerate pull in `MacTypes.h`,
whose legacy globals (`bold`, `normal`, …) collide with `ftxui` under
`using namespace ftxui` — keep Apple includes out of headers (pimpl).

References: Apple "Capturing system audio with Core Audio taps",
insidegui/AudioCap, AudioTee.

## Linux (Arch + Hyprland) — viable, deferred

**API:** PipeWire `pw_stream` capture with `PW_KEY_STREAM_CAPTURE_SINK`
(record the monitor of the default sink). Hyprland setups are PipeWire-native,
so no PulseAudio fallback is needed for this deployment.

**Permission:** none — desktop Linux has no TCC equivalent for local capture.

**Effort:** an `AudioTap` twin (~150 lines C) on `pw_thread_loop`; the
`SpectrumAnalyzer` needs a portable FFT (kissfft single-header, or a 60-line
radix-2) since Accelerate is Apple-only. Band-folding/render code is shared
as-is. Lands together with the deferred Arch packaging phase (PKGBUILD +
`.desktop`).

## Verdict

| | macOS | Linux (Arch) |
|---|---|---|
| System-audio capture | CoreAudio process tap (14.2+) | PipeWire sink monitor |
| Permission | one-time prompt (terminal app) | none |
| FFT | Accelerate vDSP | kissfft / hand-rolled |
| Status | **spike shipped** (`v` key) | deferred with Linux packaging |
