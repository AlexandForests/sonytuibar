#pragma once
#include <ftxui/dom/elements.hpp>
#include <mdr/Headphones.hpp>

namespace tui
{
    // Composes the read-only btop dashboard from the device's .current state.
    // Each panel is support-gated against device.mSupport and simply omits
    // itself when the device doesn't advertise the function.
    ftxui::Element RenderDashboard(const mdr::MDRHeadphones& device);
}
