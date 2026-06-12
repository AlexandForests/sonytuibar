#include "AudioTap_macOS.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

#include <Accelerate/Accelerate.h>
#include <CoreAudio/AudioHardware.h>
#import <CoreAudio/AudioHardwareTapping.h>
#import <CoreAudio/CATapDescription.h>
#import <Foundation/Foundation.h>

namespace tui
{
    struct AudioTap::Impl
    {
        AudioObjectID tap = kAudioObjectUnknown;
        AudioObjectID aggregate = kAudioObjectUnknown;
        AudioDeviceIOProcID proc = nullptr;
        float sampleRate = 48000.0f;

        static constexpr size_t kRingSize = 8192; // power of two
        std::array<float, kRingSize> ring{};
        std::atomic<uint64_t> written{0};
    };

    AudioTap::AudioTap() : mImpl(std::make_unique<Impl>()) {}
    AudioTap::~AudioTap() { Stop(); }
    bool AudioTap::Active() const { return mImpl->aggregate != kAudioObjectUnknown; }
    float AudioTap::SampleRate() const { return mImpl->sampleRate; }

    bool AudioTap::Start()
    {
        if (Active())
            return true;
        Impl* im = mImpl.get();

        @autoreleasepool
        {
            // Mono mixdown of every process playing to the default output.
            CATapDescription* desc =
                [[CATapDescription alloc] initMonoGlobalTapButExcludeProcesses:@[]];
            desc.name = @"sonytui visualizer tap";
            desc.privateTap = YES;

            if (AudioHardwareCreateProcessTap(desc, &im->tap) != noErr ||
                im->tap == kAudioObjectUnknown)
            {
                im->tap = kAudioObjectUnknown;
                return false;
            }

            // Tap format follows the output device; we only need the sample rate.
            AudioStreamBasicDescription fmt{};
            UInt32 size = sizeof(fmt);
            AudioObjectPropertyAddress addr{kAudioTapPropertyFormat,
                                            kAudioObjectPropertyScopeGlobal,
                                            kAudioObjectPropertyElementMain};
            if (AudioObjectGetPropertyData(im->tap, &addr, 0, nullptr, &size, &fmt) == noErr &&
                fmt.mSampleRate > 0)
                im->sampleRate = static_cast<float>(fmt.mSampleRate);

            // Private aggregate device wrapping the tap so we can attach an IOProc.
            NSDictionary* aggDesc = @{
                @kAudioAggregateDeviceNameKey : @"sonytui-tap",
                @kAudioAggregateDeviceUIDKey :
                    [NSString stringWithFormat:@"local.sonytui.tap.%@", desc.UUID.UUIDString],
                @kAudioAggregateDeviceIsPrivateKey : @YES,
                @kAudioAggregateDeviceTapAutoStartKey : @YES,
                @kAudioAggregateDeviceTapListKey : @[ @{
                    @kAudioSubTapUIDKey : desc.UUID.UUIDString,
                    @kAudioSubTapDriftCompensationKey : @YES,
                } ],
            };
            if (AudioHardwareCreateAggregateDevice((__bridge CFDictionaryRef)aggDesc,
                                                   &im->aggregate) != noErr)
            {
                im->aggregate = kAudioObjectUnknown;
                Stop();
                return false;
            }

            OSStatus err = AudioDeviceCreateIOProcIDWithBlock(
                &im->proc, im->aggregate, nullptr,
                ^(const AudioTimeStamp*, const AudioBufferList* inData,
                  const AudioTimeStamp*, AudioBufferList*, const AudioTimeStamp*)
                {
                    // CoreAudio IO thread: touch nothing but the ring buffer.
                    for (UInt32 b = 0; b < inData->mNumberBuffers; ++b)
                    {
                        const AudioBuffer& buf = inData->mBuffers[b];
                        const float* s = static_cast<const float*>(buf.mData);
                        size_t count = buf.mDataByteSize / sizeof(float);
                        uint64_t w = im->written.load(std::memory_order_relaxed);
                        for (size_t i = 0; i < count; ++i)
                            im->ring[(w + i) % Impl::kRingSize] = s[i];
                        im->written.store(w + count, std::memory_order_release);
                    }
                });
            if (err != noErr || AudioDeviceStart(im->aggregate, im->proc) != noErr)
            {
                Stop();
                return false;
            }
        }
        return true;
    }

    void AudioTap::Stop()
    {
        Impl* im = mImpl.get();
        if (im->proc && im->aggregate != kAudioObjectUnknown)
        {
            AudioDeviceStop(im->aggregate, im->proc);
            AudioDeviceDestroyIOProcID(im->aggregate, im->proc);
        }
        im->proc = nullptr;
        if (im->aggregate != kAudioObjectUnknown)
            AudioHardwareDestroyAggregateDevice(im->aggregate);
        im->aggregate = kAudioObjectUnknown;
        if (im->tap != kAudioObjectUnknown)
            AudioHardwareDestroyProcessTap(im->tap);
        im->tap = kAudioObjectUnknown;
    }

    bool AudioTap::Latest(float* out, size_t n) const
    {
        const Impl* im = mImpl.get();
        uint64_t w = im->written.load(std::memory_order_acquire);
        if (w < n)
            return false;
        for (size_t i = 0; i < n; ++i)
            out[i] = im->ring[(w - n + i) % Impl::kRingSize];
        return true;
    }

    namespace
    {
        constexpr int kFftSize = 1024;
        constexpr int kLog2Fft = 10;
    }

    struct SpectrumAnalyzer::Impl
    {
        FFTSetup fft = nullptr;
        std::vector<float> window;
        std::vector<float> scratch; // windowed samples + split-complex halves
        std::vector<float> bands;
    };

    SpectrumAnalyzer::SpectrumAnalyzer() : mImpl(std::make_unique<Impl>())
    {
        mImpl->window.resize(kFftSize);
        mImpl->scratch.resize(kFftSize * 2);
        mImpl->bands.assign(kBands, 0.0f);
        mImpl->fft = vDSP_create_fftsetup(kLog2Fft, kFFTRadix2);
        vDSP_hann_window(mImpl->window.data(), kFftSize, vDSP_HANN_NORM);
    }

    SpectrumAnalyzer::~SpectrumAnalyzer()
    {
        if (mImpl->fft)
            vDSP_destroy_fftsetup(mImpl->fft);
    }

    const std::vector<float>& SpectrumAnalyzer::Compute(const AudioTap& tap)
    {
        constexpr float kDecay = 0.05f;  // per-frame fall (~0.7s full drop @30fps)
        constexpr float kFloorDb = 0.0f; // squared-magnitude dB mapped to bar 0
        constexpr float kCeilDb = 50.0f; // ... and bar 1.0; tuned live

        Impl* im = mImpl.get();
        float samples[kFftSize];
        if (!im->fft || !tap.Latest(samples, kFftSize))
        {
            for (float& b : im->bands)
                b = std::max(0.0f, b - kDecay);
            return im->bands;
        }

        // Hann window, then in-place real FFT (vDSP packed split-complex form).
        float* windowed = im->scratch.data();
        vDSP_vmul(samples, 1, im->window.data(), 1, windowed, 1, kFftSize);
        DSPSplitComplex split{windowed + kFftSize, windowed + kFftSize + kFftSize / 2};
        vDSP_ctoz(reinterpret_cast<const DSPComplex*>(windowed), 2, &split, 1, kFftSize / 2);
        vDSP_fft_zrip(im->fft, &split, 1, kLog2Fft, FFT_FORWARD);
        float mags[kFftSize / 2];
        vDSP_zvmags(&split, 1, mags, 1, kFftSize / 2);

        // Fold bins into log-spaced bands (40 Hz .. 16 kHz), peak per band.
        const float binHz = tap.SampleRate() / kFftSize;
        const float fLo = 40.0f;
        const float fHi = std::min(16000.0f, tap.SampleRate() * 0.5f);
        for (int i = 0; i < kBands; ++i)
        {
            float a = fLo * std::pow(fHi / fLo, static_cast<float>(i) / kBands);
            float b = fLo * std::pow(fHi / fLo, static_cast<float>(i + 1) / kBands);
            int lo = std::max(1, static_cast<int>(a / binHz));
            int hi = std::min(kFftSize / 2, std::max(lo + 1, static_cast<int>(b / binHz)));
            float peak = 0.0f;
            for (int k = lo; k < hi; ++k)
                peak = std::max(peak, mags[k]);

            float db = 10.0f * std::log10(peak + 1e-9f);
            float level = std::clamp((db - kFloorDb) / (kCeilDb - kFloorDb), 0.0f, 1.0f);
            im->bands[i] = std::max(level, im->bands[i] - kDecay);
        }
        return im->bands;
    }
}
