/**
 * \file IconView.hpp
 * \brief A static SVG icon that keeps itself in step with the theme and the screen scale.
 */
#pragma once

#include <string>

#include <wx/bitmap.h>
#include <wx/window.h>

namespace Slic3r::GUI {

/**
 * \brief Draws a named SVG asset centred on its parent's background.
 *
 * The icon follows dark/light switches and DPI changes by itself, so owners do not need to reload
 * it. Use it in place of a wxStaticBitmap built from create_scaled_bitmap, which keeps the colours
 * of the theme it was made in.
 *
 * Caution: the area around the icon is filled with the parent's background colour. Do not put it on
 * a parent that paints a different fill of its own.
 */
class IconView : public wxWindow
{
public:
    /**
     * \param parent the parent window
     * \param icon   asset name in the images folder, without extension
     * \param px     icon height in DIP
     */
    IconView(wxWindow *parent, const std::string &icon, int px);

protected:
    wxSize DoGetBestSize() const override;

private:
    const wxBitmap &fresh_bitmap() const;
    void            on_paint(wxPaintEvent &);

    std::string m_icon;
    int         m_px;

    // Rasterized on demand: a size query or a paint can be the first to see a new theme or scale.
    mutable wxBitmap m_bmp;
    mutable bool     m_bmp_dark{false};
    mutable int      m_bmp_px{0};
};

} // namespace Slic3r::GUI
