#pragma once
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <mdr/Headphones.hpp>

namespace tui
{
    // Handle a keypress while connected. Mutates device .desired values (the main
    // loop commits dirty props). Returns true if the event was consumed.
    bool HandleControl(const ftxui::Event& e, mdr::MDRHeadphones& device);

    // One-line keybind bar, gated to the device's supported actions.
    ftxui::Element Footer(const mdr::MDRHeadphones& device);
}
