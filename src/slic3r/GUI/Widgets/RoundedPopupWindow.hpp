#pragma once

#include <wx/colour.h>

#include "slic3r/GUI/Widgets/PopupWindow.hpp"
#include "slic3r/GUI/WindowShadow.hpp"

class wxPaintEvent;

namespace Slic3r::GUI {

/**
 * \brief Transient popup rendered as a rounded rectangle with a themed card fill and 1px border.
 *
 * Clipped to the rounded silhouette, plus an optional soft drop shadow. A reusable base for menus,
 * tooltips and small overlays: build content into it with a sizer (children parent to the popup),
 * then call Finalize() before Popup(). Colours are given as light-mode values and mapped for dark mode.
 */
class RoundedPopupWindow : public PopupWindow
{
public:
    explicit RoundedPopupWindow(wxWindow *parent, int corner_radius = 8, bool with_shadow = false);

    void SetBorderColour(const wxColour &light) { m_border_light = light; }
    void SetCornerRadius(int radius) { m_radius = radius; }

    /**
     * \brief Stroke width in device pixels. 0 keeps the default one-DIP stroke.
     *
     * A design that specifies a 0.5px hairline wants one device pixel, which FromDIP(1)
     * overshoots once the window is scaled.
     */
    void SetBorderWidthPx(int px) { m_border_width_px = px; }

    /** \brief Size the popup to its content and clip it to the rounded shape. Call after building the sizer. */
    void Finalize();

protected:
    void OnPaint(wxPaintEvent &evt);
    void ApplyShape();

    int          m_radius{8};
    bool         m_with_shadow{false};
    int          m_border_width_px{0};
    wxColour     m_border_light;
    WindowShadow m_shadow;
};

} // namespace Slic3r::GUI
