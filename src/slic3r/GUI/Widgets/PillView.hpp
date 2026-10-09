#pragma once

#include <wx/colour.h>
#include <wx/window.h>

namespace Slic3r::GUI {

/**
 * \brief A small rounded text chip, filled and/or outlined. Sizes to its text in the window font
 * (Body_11 unless SetFont() changes it).
 *
 * Pass the colour of the surface it sits on: the parent's background colour is not reliable when the
 * parent paints its own fill (e.g. RoundedRectangle).
 */
class PillView : public wxWindow
{
public:
    /**
     * \param parent   the parent window
     * \param surface  colour behind the pill, shown at its rounded corners
     */
    PillView(wxWindow *parent, const wxColour &surface);

    void set_text(const wxString &text);
    /**
     * \brief Text, fill and border colours; pass wxTransparentColour for no fill or no border.
     */
    void set_colors(const wxColour &fg, const wxColour &fill, const wxColour &border);

    enum class Shape { Rounded, Capsule }; ///< Capsule: fully round ends at any height
    void set_shape(Shape shape);

protected:
    wxSize DoGetBestSize() const override;

private:
    void on_paint(wxPaintEvent &);

    wxString m_text;
    wxColour m_fg, m_fill, m_border;
    Shape    m_shape{Shape::Rounded};
};

} // namespace Slic3r::GUI
