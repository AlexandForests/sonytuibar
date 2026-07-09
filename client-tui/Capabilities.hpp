#pragma once

// UI-agnostic capability checks + enum formatters shared by client-tui
// (Controls.cpp/Panels.cpp) and client-menubar (MenuController.mm). No
// FTXUI or AppKit dependency here — only mdr headers — so both an FTXUI
// TU and an Objective-C++ TU can include it. Everything is `inline` so
// the header can be included from multiple translation units without
// violating ODR.

#include <algorithm>
#include <array>

#include <mdr/Headphones.hpp>
#include <mdr/ProtocolV2T1.hpp>

namespace tui
{
    namespace v2t1 = mdr::v2::t1;
    using F1 = mdr::v2::MessageMdrV2FunctionType_Table1;

    inline bool Has(const mdr::MDRHeadphones& d, F1 f) { return d.mSupport.contains(f); }

    inline bool SupportsNc(const mdr::MDRHeadphones& d)
    {
        return Has(d, F1::NOISE_CANCELLING_ONOFF) ||
               Has(d, F1::NOISE_CANCELLING_ONOFF_AND_AMBIENT_SOUND_MODE_ONOFF) ||
               Has(d, F1::NOISE_CANCELLING_ONOFF_AND_AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT) ||
               Has(d, F1::NOISE_CANCELLING_DUAL_SINGLE_OFF_AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT) ||
               Has(d, F1::MODE_NC_ASM_NOISE_CANCELLING_DUAL_AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT) ||
               Has(d, F1::MODE_NC_ASM_NOISE_CANCELLING_DUAL_SINGLE_AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT) ||
               Has(d, F1::MODE_NC_ASM_NOISE_CANCELLING_DUAL_AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT_NOISE_ADAPTATION);
    }

    inline bool SupportsAsm(const mdr::MDRHeadphones& d)
    {
        return Has(d, F1::NOISE_CANCELLING_ONOFF_AND_AMBIENT_SOUND_MODE_ONOFF) ||
               Has(d, F1::NOISE_CANCELLING_ONOFF_AND_AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT) ||
               Has(d, F1::NOISE_CANCELLING_DUAL_SINGLE_OFF_AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT) ||
               Has(d, F1::AMBIENT_SOUND_MODE_ONOFF) ||
               Has(d, F1::AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT) ||
               Has(d, F1::AMBIENT_SOUND_CONTROL_MODE_SELECT) ||
               Has(d, F1::MODE_NC_ASM_NOISE_CANCELLING_DUAL_AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT) ||
               Has(d, F1::MODE_NC_ASM_NOISE_CANCELLING_DUAL_SINGLE_AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT) ||
               Has(d, F1::MODE_NC_ASM_NOISE_CANCELLING_DUAL_AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT_NOISE_ADAPTATION);
    }

    inline bool SupportsNcAsm(const mdr::MDRHeadphones& d)
    {
        return SupportsNc(d) || SupportsAsm(d);
    }

    // --- Small enum formatters (subset copied from client/Client.cpp) ---

    inline const char* CodecName(v2t1::AudioCodec c)
    {
        using enum v2t1::AudioCodec;
        switch (c)
        {
        case SBC: return "SBC";
        case AAC: return "AAC";
        case LDAC: return "LDAC";
        case APT_X: return "aptX";
        case APT_X_HD: return "aptX HD";
        case LC3: return "LC3";
        case UNSETTLED: return "...";
        default: return "?";
        }
    }

    inline const char* UpscalingName(v2t1::UpscalingType t)
    {
        using enum v2t1::UpscalingType;
        switch (t)
        {
        case DSEE_HX: return "DSEE HX";
        case DSEE: return "DSEE";
        case DSEE_HX_AI: return "DSEE HX AI";
        case DSEE_ULTIMATE: return "DSEE ULTIMATE";
        default: return "DSEE";
        }
    }

    inline const char* ChargingName(v2t1::BatteryChargingStatus s)
    {
        using enum v2t1::BatteryChargingStatus;
        switch (s)
        {
        case CHARGING: return "charging";
        case CHARGED: return "charged";
        default: return "";
        }
    }

    inline const char* EqPresetName(v2t1::EqPresetId id)
    {
        using enum v2t1::EqPresetId;
        switch (id)
        {
        case OFF: return "Off";
        case ROCK: return "Rock";
        case POP: return "Pop";
        case JAZZ: return "Jazz";
        case DANCE: return "Dance";
        case EDM: return "EDM";
        case R_AND_B_HIP_HOP: return "R&B/Hip-Hop";
        case ACOUSTIC: return "Acoustic";
        case BRIGHT: return "Bright";
        case EXCITED: return "Excited";
        case MELLOW: return "Mellow";
        case RELAXED: return "Relaxed";
        case VOCAL: return "Vocal";
        case TREBLE: return "Treble";
        case BASS: return "Bass";
        case SPEECH: return "Speech";
        case CUSTOM: return "Custom";
        case USER_SETTING1: return "User 1";
        case USER_SETTING2: return "User 2";
        case USER_SETTING3: return "User 3";
        case USER_SETTING4: return "User 4";
        case USER_SETTING5: return "User 5";
        default: return "Custom";
        }
    }

    // Only the presets the WH-1000XM5 actually applies: OFF plus IDs 0x10..0x17.
    // The older Rock/Pop/Jazz... set (0x01..0x07) is rejected by the device
    // firmware — it echoes the current preset back — so we don't offer them.
    inline constexpr std::array<v2t1::EqPresetId, 9> kEqPresets = {
        v2t1::EqPresetId::OFF,
        v2t1::EqPresetId::BRIGHT, v2t1::EqPresetId::EXCITED, v2t1::EqPresetId::MELLOW,
        v2t1::EqPresetId::RELAXED, v2t1::EqPresetId::VOCAL, v2t1::EqPresetId::TREBLE,
        v2t1::EqPresetId::BASS, v2t1::EqPresetId::SPEECH,
    };

    inline void CycleEq(mdr::MDRHeadphones& d, int dir)
    {
        auto it = std::find(kEqPresets.begin(), kEqPresets.end(), d.mEqPresetId.desired);
        int n = static_cast<int>(kEqPresets.size());
        int idx;
        if (it == kEqPresets.end())
            idx = (dir > 0) ? 0 : n - 1; // unknown/unsupported current -> first valid
        else
            idx = (static_cast<int>(it - kEqPresets.begin()) + dir + n) % n;
        d.mEqPresetId.desired = kEqPresets[idx];
    }
}
