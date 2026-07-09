#include "Controls.hpp"

#include <algorithm>
#include <string>

#include <mdr/ProtocolV2T1.hpp>

#include "Capabilities.hpp"

using namespace ftxui;
namespace v2t1 = mdr::v2::t1;

namespace tui
{
    namespace
    {
        // Cycle noise mode: Off -> NC -> Ambient -> Off (skipping modes the device lacks).
        void CycleNoise(mdr::MDRHeadphones& d)
        {
            using enum v2t1::NcAsmMode;
            bool nc = SupportsNc(d), asm_ = SupportsAsm(d);

            // Current effective state from .desired so repeated presses advance.
            int state; // 0=Off, 1=NC, 2=ASM
            if (!d.mNcAsmEnabled.desired) state = 0;
            else state = (d.mNcAsmMode.desired == ASM) ? 2 : 1;

            for (int step = 0; step < 3; ++step)
            {
                state = (state + 1) % 3;
                if (state == 1 && !nc) continue;
                if (state == 2 && !asm_) continue;
                break;
            }

            if (state == 0)
            {
                d.mNcAsmEnabled.desired = false;
            }
            else if (state == 1)
            {
                d.mNcAsmEnabled.desired = true;
                d.mNcAsmMode.desired = NC;
            }
            else
            {
                d.mNcAsmEnabled.desired = true;
                d.mNcAsmMode.desired = ASM;
                if (d.mNcAsmAmbientLevel.desired == 0)
                    d.mNcAsmAmbientLevel.desired = 20;
            }
        }
    }

    bool HandleControl(const Event& e, mdr::MDRHeadphones& d)
    {
        // Volume -/+
        if (e == Event::Character('-') || e == Event::Character('_'))
        {
            d.mPlayVolume.desired = std::max(0, d.mPlayVolume.desired - 1);
            return true;
        }
        if (e == Event::Character('+') || e == Event::Character('='))
        {
            d.mPlayVolume.desired = std::min(30, d.mPlayVolume.desired + 1);
            return true;
        }

        // Noise mode cycle
        if (e == Event::Character('n') && (SupportsNc(d) || SupportsAsm(d)))
        {
            CycleNoise(d);
            return true;
        }

        // Ambient level [ / ] (only meaningful in Ambient mode)
        if (e == Event::Character('[') && SupportsAsm(d))
        {
            d.mNcAsmAmbientLevel.desired = std::max(1, d.mNcAsmAmbientLevel.desired - 1);
            return true;
        }
        if (e == Event::Character(']') && SupportsAsm(d))
        {
            d.mNcAsmAmbientLevel.desired = std::min(20, d.mNcAsmAmbientLevel.desired + 1);
            return true;
        }

        // EQ preset cycle , / .
        if (e == Event::Character('.') && d.mEqAvailable.current)
        {
            CycleEq(d, +1);
            return true;
        }
        if (e == Event::Character(',') && d.mEqAvailable.current)
        {
            CycleEq(d, -1);
            return true;
        }

        return false;
    }

    Element Footer(const mdr::MDRHeadphones& d)
    {
        auto key = [](const std::string& k, const std::string& label)
        {
            return hbox({text(k) | bold | color(Color::Cyan), text(":" + label + "  ") | dim});
        };

        Elements items;
        if (SupportsNc(d) || SupportsAsm(d)) items.push_back(key("n", "noise"));
        if (SupportsAsm(d)) items.push_back(key("[ ]", "ambient"));
        items.push_back(key("- +", "vol"));
        if (d.mEqAvailable.current) items.push_back(key(", .", "eq"));
        items.push_back(key("q", "quit"));

        return hbox(std::move(items));
    }
}
