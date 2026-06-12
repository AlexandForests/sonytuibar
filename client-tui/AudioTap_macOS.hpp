#pragma once
// System-audio capture for the visualizer (CoreAudio process taps, macOS 14.2+).
// No audio crosses the MDR link, so the visualizer taps the Mac's output mix.
// Apple headers stay in the .mm (they leak legacy globals like `bold` that
// collide with ftxui); this header is plain C++.
#include <cstddef>
#include <memory>
#include <vector>

namespace tui
{
    // Taps the whole system output (mono mixdown) into a lock-free ring buffer.
    // The CoreAudio IO thread only writes the ring buffer — libmdr and FTXUI
    // stay strictly on the main thread, unchanged.
    class AudioTap
    {
    public:
        AudioTap();
        ~AudioTap();
        AudioTap(const AudioTap&) = delete;
        AudioTap& operator=(const AudioTap&) = delete;

        // Creates the tap + private aggregate device and starts capture.
        // The first call system-wide triggers the "System Audio Recording"
        // permission prompt (attributed to the terminal app for a CLI binary).
        bool Start();
        void Stop();
        bool Active() const;
        float SampleRate() const;

        // Copy the newest n mono samples into out; false until enough captured.
        bool Latest(float* out, size_t n) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };

    // 1024-pt vDSP FFT folded into log-spaced bands with peak decay, in [0,1].
    class SpectrumAnalyzer
    {
    public:
        static constexpr int kBands = 24;

        SpectrumAnalyzer();
        ~SpectrumAnalyzer();

        // Call once per frame while the tap is active.
        const std::vector<float>& Compute(const AudioTap& tap);

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}
