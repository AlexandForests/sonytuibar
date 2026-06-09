#include "Panels.hpp"

#include <string>
#include <vector>

#include <ftxui/dom/elements.hpp>

#include <mdr/ProtocolV2T1.hpp>

using namespace ftxui;
namespace v2t1 = mdr::v2::t1;
using F1 = mdr::v2::MessageMdrV2FunctionType_Table1;

namespace tui
{
    namespace
    {
        // --- Small enum formatters (subset copied from client/Client.cpp) ---

        const char* CodecName(v2t1::AudioCodec c)
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

        const char* UpscalingName(v2t1::UpscalingType t)
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

        const char* ChargingName(v2t1::BatteryChargingStatus s)
        {
            using enum v2t1::BatteryChargingStatus;
            switch (s)
            {
            case CHARGING: return "charging";
            case CHARGED: return "charged";
            default: return "";
            }
        }

        const char* EqPresetName(v2t1::EqPresetId id)
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

        // --- Helpers ---

        Color LevelColor(int pct)
        {
            if (pct > 50) return Color::Green;
            if (pct > 20) return Color::Yellow;
            return Color::Red;
        }

        Element Panel(const std::string& title, Element body)
        {
            return window(text(" " + title + " ") | bold, body);
        }

        bool Supports(const mdr::MDRHeadphones& d, F1 f)
        {
            return d.mSupport.contains(f);
        }

        Element BatteryRow(const std::string& label, const mdr::MDRHeadphones::BatteryState& b)
        {
            int pct = b.level;
            const char* charge = ChargingName(b.charging);
            return hbox({
                text(label) | size(WIDTH, EQUAL, 6),
                gauge(pct / 100.0f) | color(LevelColor(pct)) | flex,
                text(" " + std::to_string(pct) + "%") | size(WIDTH, EQUAL, 5),
                text(*charge ? std::string("(") + charge + ")" : "") | dim,
            });
        }

        // --- Panels ---

        Element HeaderPanel(const mdr::MDRHeadphones& d)
        {
            Elements badges;
            if (Supports(d, F1::CODEC_INDICATOR))
                badges.push_back(text(" " + std::string(CodecName(d.mAudioCodec)) + " ") |
                                 bgcolor(Color::Blue) | color(Color::White));
            if (d.mUpscalingEnabled.current)
            {
                if (!badges.empty()) badges.push_back(text(" "));
                badges.push_back(text(" " + std::string(UpscalingName(d.mUpscalingType)) + " ") |
                                 bgcolor(Color::Magenta) | color(Color::White));
            }

            std::string model = d.mModelName.empty() ? "Sony Headphones" : d.mModelName;
            std::string fw = d.mFWVersion.empty() ? "" : "  fw " + d.mFWVersion;

            return hbox({
                text(model) | bold,
                text(fw) | dim,
                filler(),
                hbox(std::move(badges)),
            });
        }

        Element BatteryPanel(const mdr::MDRHeadphones& d)
        {
            bool single = Supports(d, F1::BATTERY_LEVEL_INDICATOR) ||
                          Supports(d, F1::BATTERY_LEVEL_WITH_THRESHOLD);
            bool lr = Supports(d, F1::LEFT_RIGHT_BATTERY_LEVEL_INDICATOR) ||
                      Supports(d, F1::LR_BATTERY_LEVEL_WITH_THRESHOLD);
            bool casing = Supports(d, F1::CRADLE_BATTERY_LEVEL_INDICATOR) ||
                          Supports(d, F1::CRADLE_BATTERY_LEVEL_WITH_THRESHOLD);

            if (!single && !lr && !casing)
                return nullptr;

            Elements rows;
            if (lr && d.mBatteryL.threshold && d.mBatteryR.threshold)
            {
                rows.push_back(BatteryRow("L", d.mBatteryL));
                rows.push_back(BatteryRow("R", d.mBatteryR));
            }
            else if (single && d.mBatteryL.threshold)
            {
                rows.push_back(BatteryRow("Batt", d.mBatteryL));
            }
            if (casing && d.mBatteryCase.threshold)
                rows.push_back(BatteryRow("Case", d.mBatteryCase));

            if (rows.empty())
                rows.push_back(text("(querying...)") | dim);

            return Panel("Battery", vbox(std::move(rows)));
        }

        bool SupportsNcAsm(const mdr::MDRHeadphones& d)
        {
            return Supports(d, F1::NOISE_CANCELLING_ONOFF) ||
                   Supports(d, F1::NOISE_CANCELLING_ONOFF_AND_AMBIENT_SOUND_MODE_ONOFF) ||
                   Supports(d, F1::NOISE_CANCELLING_ONOFF_AND_AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT) ||
                   Supports(d, F1::NOISE_CANCELLING_DUAL_SINGLE_OFF_AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT) ||
                   Supports(d, F1::AMBIENT_SOUND_MODE_ONOFF) ||
                   Supports(d, F1::AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT) ||
                   Supports(d, F1::AMBIENT_SOUND_CONTROL_MODE_SELECT) ||
                   Supports(d, F1::MODE_NC_ASM_NOISE_CANCELLING_DUAL_AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT) ||
                   Supports(d, F1::MODE_NC_ASM_NOISE_CANCELLING_DUAL_SINGLE_AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT) ||
                   Supports(d, F1::MODE_NC_ASM_NOISE_CANCELLING_DUAL_AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT_NOISE_ADAPTATION);
        }

        Element NcAsmPanel(const mdr::MDRHeadphones& d)
        {
            if (!SupportsNcAsm(d))
                return nullptr;

            Elements rows;
            if (!d.mNcAsmEnabled.current)
            {
                rows.push_back(text("Off") | bold | dim);
            }
            else if (d.mNcAsmMode.current == v2t1::NcAsmMode::NC)
            {
                rows.push_back(text("Noise Cancelling") | bold | color(Color::Cyan));
            }
            else
            {
                rows.push_back(text("Ambient Sound") | bold | color(Color::Green));
                int lvl = d.mNcAsmAmbientLevel.current;
                rows.push_back(hbox({
                    text("Level ") | size(WIDTH, EQUAL, 6),
                    gauge(lvl / 20.0f) | flex,
                    text(" " + std::to_string(lvl) + "/20") | size(WIDTH, EQUAL, 6),
                }));
            }
            if (d.mNcAsmFocusOnVoice.current)
                rows.push_back(text("Voice passthrough: on") | dim);

            return Panel("Noise Control", vbox(std::move(rows)));
        }

        Element EqPanel(const mdr::MDRHeadphones& d)
        {
            if (!d.mEqAvailable.current)
                return nullptr;

            const std::vector<int>& bands = d.mEqConfig.current;
            int n = static_cast<int>(bands.size());

            const char* const* labels = nullptr;
            int lo = 0, hi = 0;
            static const char* k5[] = {"400", "1k", "2.5k", "6.3k", "16k"};
            static const char* k10[] = {"31", "63", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"};
            if (n == 5) { labels = k5; lo = -10; hi = 10; }
            else if (n == 10) { labels = k10; lo = -6; hi = 6; }

            Elements rows;
            rows.push_back(text(std::string("Preset: ") + EqPresetName(d.mEqPresetId.current)) | bold);

            if (labels)
            {
                int span = hi - lo;
                for (int i = 0; i < n; ++i)
                {
                    int v = bands[i];
                    float frac = span ? static_cast<float>(v - lo) / span : 0.5f;
                    rows.push_back(hbox({
                        text(labels[i]) | size(WIDTH, EQUAL, 5),
                        gauge(frac) | flex,
                        text((v > 0 ? "+" : "") + std::to_string(v)) | size(WIDTH, EQUAL, 4),
                    }));
                }
            }

            return Panel("Equalizer", vbox(std::move(rows)));
        }

        Element PlaybackPanel(const mdr::MDRHeadphones& d)
        {
            Elements rows;

            int vol = d.mPlayVolume.current;
            rows.push_back(hbox({
                text("Vol ") | size(WIDTH, EQUAL, 6),
                gauge(vol / 30.0f) | color(Color::Cyan) | flex,
                text(" " + std::to_string(vol) + "/30") | size(WIDTH, EQUAL, 6),
            }));

            bool playing = d.mPlayPause == v2t1::PlaybackStatus::PLAY;
            if (!d.mPlayTrackTitle.empty() || !d.mPlayTrackArtist.empty())
            {
                rows.push_back(hbox({
                    text(playing ? "▶ " : "⏸ "),
                    text(d.mPlayTrackTitle.empty() ? "(unknown)" : d.mPlayTrackTitle),
                }));
                if (!d.mPlayTrackArtist.empty())
                    rows.push_back(text("  " + d.mPlayTrackArtist) | dim);
            }

            return Panel("Playback", vbox(std::move(rows)));
        }
    }

    Element RenderDashboard(const mdr::MDRHeadphones& d)
    {
        Elements panels;
        panels.push_back(HeaderPanel(d));
        panels.push_back(separator());

        for (Element e : {BatteryPanel(d), NcAsmPanel(d), EqPanel(d), PlaybackPanel(d)})
            if (e) panels.push_back(std::move(e));

        return vbox(std::move(panels));
    }
}
