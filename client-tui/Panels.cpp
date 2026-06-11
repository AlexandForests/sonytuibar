#include "Panels.hpp"

#include <cmath>
#include <string>
#include <vector>

#include <ftxui/dom/canvas.hpp>
#include <ftxui/dom/elements.hpp>

#include <mdr/ProtocolV2T1.hpp>

using namespace ftxui;
namespace v2t1 = mdr::v2::t1;
using F1 = mdr::v2::MessageMdrV2FunctionType_Table1;

namespace tui
{
    namespace
    {
        // --- btop-ish truecolor palette (FTXUI degrades gracefully on non-truecolor terms) ---
        const Color kAccentBattery = Color::RGB(166, 227, 161); // green
        const Color kAccentNoise = Color::RGB(137, 220, 235);   // sky
        const Color kAccentEq = Color::RGB(203, 166, 247);      // mauve
        const Color kAccentPlay = Color::RGB(137, 180, 250);    // blue
        const Color kAccentHeader = Color::RGB(245, 194, 231);  // pink
        const Color kDimText = Color::RGB(127, 132, 156);

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
            if (pct > 50) return Color::RGB(166, 227, 161);
            if (pct > 20) return Color::RGB(249, 226, 175);
            return Color::RGB(243, 139, 168);
        }

        // btop-style panel: rounded accent border with the title embedded in the top edge.
        Element Panel(const std::string& title, Element body, const Color& accent)
        {
            return dbox({
                       body | borderStyled(ROUNDED, accent),
                       vbox({
                           hbox({
                               text("╴") | color(accent),
                               text(title) | bold | color(accent),
                               text("╶") | color(accent),
                           }) | hcenter,
                           filler(),
                       }),
                   }) |
                   yflex_grow;
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
                text(*charge ? std::string("(") + charge + ")" : "") | color(kDimText),
            });
        }

        // --- Panels ---

        Element HeaderPanel(const mdr::MDRHeadphones& d)
        {
            Elements badges;
            if (Supports(d, F1::CODEC_INDICATOR))
                badges.push_back(text(" " + std::string(CodecName(d.mAudioCodec)) + " ") |
                                 bgcolor(kAccentPlay) | color(Color::RGB(30, 30, 46)) | bold);
            if (d.mUpscalingEnabled.current)
            {
                if (!badges.empty()) badges.push_back(text(" "));
                badges.push_back(text(" " + std::string(UpscalingName(d.mUpscalingType)) + " ") |
                                 bgcolor(kAccentEq) | color(Color::RGB(30, 30, 46)) | bold);
            }

            std::string model = d.mModelName.empty() ? "Sony Headphones" : d.mModelName;
            std::string fw = d.mFWVersion.empty() ? "" : "  fw " + d.mFWVersion;

            return hbox({
                text(" ♪ ") | color(kAccentHeader) | bold,
                text(model) | bold,
                text(fw) | color(kDimText),
                filler(),
                hbox(std::move(badges)),
                text(" "),
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
                rows.push_back(text("(querying...)") | color(kDimText));

            return Panel("battery", vbox(std::move(rows)), kAccentBattery);
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

        Element NoiseModeChip(const std::string& label, bool active, const Color& accent)
        {
            if (active)
                return text(" " + label + " ") | bgcolor(accent) | color(Color::RGB(30, 30, 46)) | bold;
            return text(" " + label + " ") | color(kDimText);
        }

        Element NcAsmPanel(const mdr::MDRHeadphones& d)
        {
            if (!SupportsNcAsm(d))
                return nullptr;

            bool off = !d.mNcAsmEnabled.current;
            bool nc = !off && d.mNcAsmMode.current == v2t1::NcAsmMode::NC;
            bool amb = !off && !nc;

            Elements rows;
            rows.push_back(hbox({
                NoiseModeChip("OFF", off, kDimText),
                text(" "),
                NoiseModeChip("NC", nc, kAccentNoise),
                text(" "),
                NoiseModeChip("AMBIENT", amb, kAccentBattery),
            }));

            int lvl = d.mNcAsmAmbientLevel.current;
            rows.push_back(hbox({
                text("Level ") | size(WIDTH, EQUAL, 6) | color(amb ? Color::Default : kDimText),
                gauge(lvl / 20.0f) | color(amb ? kAccentBattery : kDimText) | flex,
                text(" " + std::to_string(lvl) + "/20") | size(WIDTH, EQUAL, 6),
            }));

            if (d.mNcAsmFocusOnVoice.current)
                rows.push_back(text("voice passthrough on") | color(kDimText));

            return Panel("noise control", vbox(std::move(rows)), kAccentNoise);
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
            rows.push_back(hbox({
                text("Preset ") | color(kDimText),
                text(EqPresetName(d.mEqPresetId.current)) | bold | color(kAccentEq),
            }));

            if (labels)
            {
                // Vertical braille bars, btop-style. Canvas pixels: 2 per cell wide, 4 tall.
                constexpr int kBarPx = 6, kGapPx = 2, kColPx = kBarPx + kGapPx; // 4 cells per band
                constexpr int kHeightPx = 24;                                  // 6 cell rows
                const int span = hi - lo;
                // canvas() invokes the draw fn lazily at screen-render time, after
                // this frame returns — capture by value or the locals dangle.
                auto eq = canvas(n * kColPx, kHeightPx, [bands, n, lo, span](Canvas& cv)
                {
                    for (int i = 0; i < n; ++i)
                    {
                        float frac = span ? static_cast<float>(bands[i] - lo) / span : 0.5f;
                        int h = std::max(1, static_cast<int>(std::lround(frac * kHeightPx)));
                        Color barColor = Color::Interpolate(
                            n > 1 ? static_cast<float>(i) / (n - 1) : 0.0f, kAccentNoise, kAccentEq);
                        for (int x = i * kColPx; x < i * kColPx + kBarPx; ++x)
                            for (int y = kHeightPx - h; y < kHeightPx; ++y)
                                cv.DrawPoint(x, y, true, barColor);
                    }
                });

                Elements labelCells, valueCells;
                for (int i = 0; i < n; ++i)
                {
                    labelCells.push_back(text(labels[i]) | hcenter | color(kDimText) |
                                         size(WIDTH, EQUAL, kColPx / 2));
                    valueCells.push_back(text((bands[i] > 0 ? "+" : "") + std::to_string(bands[i])) |
                                         hcenter | size(WIDTH, EQUAL, kColPx / 2));
                }

                rows.push_back(eq | hcenter);
                rows.push_back(hbox(std::move(valueCells)) | hcenter);
                rows.push_back(hbox(std::move(labelCells)) | hcenter);
            }

            return Panel("equalizer", vbox(std::move(rows)), kAccentEq);
        }

        Element PlaybackPanel(const mdr::MDRHeadphones& d)
        {
            Elements rows;

            int vol = d.mPlayVolume.current;
            rows.push_back(hbox({
                text("Vol ") | size(WIDTH, EQUAL, 6),
                gauge(vol / 30.0f) | color(kAccentPlay) | flex,
                text(" " + std::to_string(vol) + "/30") | size(WIDTH, EQUAL, 6),
            }));

            // Track metadata only arrives if the source relays it over MDR;
            // macOS doesn't, so only render the rows when something shows up.
            bool playing = d.mPlayPause == v2t1::PlaybackStatus::PLAY;
            if (!d.mPlayTrackTitle.empty() || !d.mPlayTrackArtist.empty())
            {
                rows.push_back(hbox({
                    text(playing ? "▶ " : "⏸ ") | color(kAccentPlay),
                    text(d.mPlayTrackTitle.empty() ? "(unknown)" : d.mPlayTrackTitle) | bold,
                }));
                if (!d.mPlayTrackArtist.empty())
                    rows.push_back(text("  " + d.mPlayTrackArtist) | color(kDimText));
            }

            return Panel("playback", vbox(std::move(rows)), kAccentPlay);
        }

        // Lay a pair of panels out side by side; either may be null.
        Element Row(Element a, Element b)
        {
            if (a && b) return hbox({a | flex, b | flex});
            if (a) return a;
            if (b) return b;
            return nullptr;
        }
    }

    Element RenderDashboard(const mdr::MDRHeadphones& d)
    {
        Elements rows;
        rows.push_back(HeaderPanel(d));

        if (Element r1 = Row(BatteryPanel(d), NcAsmPanel(d)))
            rows.push_back(r1);
        if (Element r2 = Row(EqPanel(d), PlaybackPanel(d)))
            rows.push_back(r2);

        return vbox(std::move(rows));
    }
}
