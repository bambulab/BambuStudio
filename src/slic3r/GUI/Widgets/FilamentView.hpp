#pragma once

#include <vector>
#include <wx/window.h>
#include <wx/colour.h>
#include <wx/string.h>

namespace Slic3r::GUI {

/**
 * \brief A filament colour badge with a label — the same data drawn in different shapes.
 *
 * Shows a filament's colour with a short label on it (a project index, or a nozzle/AMS slot
 * label). The colour area renders a single colour, a multi-colour split, or a gradient the same
 * way as the shared filament bitmap helper, so opacity and gradients are consistent everywhere.
 * The appearance is chosen with \ref Style; the data (colours + label) is identical across shapes.
 *
 * \note The label is supplied by the caller (formatted elsewhere); the widget only displays it.
 */
class FilamentView : public wxWindow
{
public:
    /// Appearance variants over the same colour+label data.
    enum class Style {
        Spool,   ///< spool/reel shape (destination AMS slot)
        Square,  ///< rounded colour square (source index badge)
    };

    /**
     * \brief A filament icon in the given style.
     *
     * \param parent  the parent window
     * \param style   the drawn shape
     * \param size    desired icon size; wxDefaultSize uses the widget's best size
     */
    FilamentView(wxWindow* parent, Style style = Style::Spool, const wxSize& size = wxDefaultSize);

    /**
     * \brief Set the filament colour(s) shown in the centre.
     *
     * \param colors    one colour, or several for a multi-colour filament
     * \param gradient  render the colours as a gradient instead of a discrete split
     */
    void set_colors(const std::vector<wxColour>& colors, bool gradient = false);

    /**
     * \brief Set the label drawn over the colour.
     *
     * \param label  the text; empty hides it
     */
    void set_label(const wxString& label);

    /**
     * \brief Switch the drawn shape without changing the data.
     *
     * \param style  the new shape
     */
    void set_style(Style style);

private:
    void on_paint(wxPaintEvent& evt);

    Style                 m_style{Style::Spool};
    std::vector<wxColour> m_colors;
    bool                  m_gradient{false};
    wxString              m_label;
};

} // namespace Slic3r::GUI
