#pragma once
#include <vector>

#include <ftxui/dom/elements.hpp>
#include <mdr/Headphones.hpp>

namespace tui
{
    // Composes the read-only btop dashboard from the device's .current state.
    // Each panel is support-gated against device.mSupport and simply omits
    // itself when the device doesn't advertise the function.
    // spectrum: optional visualizer band levels in [0,1] (macOS audio tap);
    // when non-null they render as bars in the playback panel.
    ftxui::Element RenderDashboard(const mdr::MDRHeadphones& device,
                                   const std::vector<float>* spectrum = nullptr);
}
