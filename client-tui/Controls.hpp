#pragma once
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <mdr/Headphones.hpp>

namespace tui
{
    // Transient UI state for controls that need confirmation.
    struct ControlState
    {
        bool confirmPowerOff = false;
    };

    // Handle a keypress while connected. Mutates device .desired values (the main
    // loop commits dirty props) or cs. Returns true if the event was consumed.
    bool HandleControl(const ftxui::Event& e, mdr::MDRHeadphones& device, ControlState& cs);

    // One-line keybind bar, gated to the device's supported actions.
    ftxui::Element Footer(const mdr::MDRHeadphones& device, const ControlState& cs);
}
