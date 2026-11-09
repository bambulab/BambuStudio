#ifndef slic3r_GUI_NozzleChipButton_hpp_
#define slic3r_GUI_NozzleChipButton_hpp_

#include "Button.hpp"

// The small "0.4 Std" chip at the right end of a filament row, opening the nozzle picker.
//
// Same look and behaviour as a plain Button (StaticBox background/hover/press fill, click ->
// wxEVT_BUTTON) -- only wxEVT_PAINT is overridden, via this class's own event table entry, which
// wx dispatches ahead of Button's (the search stops at the first match walking derived -> base),
// so Button's mouse and key handling is untouched.
//
// Needed because Button draws its whole label in one font: the diameter ("0.6") and the flow-type
// text after it ("TPU285", shown in full rather than abbreviated) must render at two different
// sizes, which a single wxString label cannot do.
class NozzleChipButton : public Button
{
public:
    explicit NozzleChipButton(wxWindow *parent);

    // `type_smaller` renders the type run at Label::Body_10 instead of Body_13 -- used for
    // TPU285's long full name so it does not read as oversized next to the short abbreviations
    // ("Std"/"HF"/...) every other flow type still uses.
    void SetChipTexts(const wxString &diameter, const wxString &type, bool type_smaller);

    // Hides Button's own (unused here -- paintEvent never reads Button's private text_color).
    void SetTextColorNormal(wxColor const &color);

private:
    wxFont typeFont() const;
    void   updateMinSize();
    void   paintEvent(wxPaintEvent &evt);

    wxString m_diameter, m_type;
    bool     m_type_smaller = false;
    wxColour m_text_color   = *wxBLACK;
    // Horizontal padding gives the hover fill some room around the glyphs; the vertical inset
    // keeps that fill from touching the row's border.
    wxSize   m_padding;

    DECLARE_EVENT_TABLE()
};

#endif // !slic3r_GUI_NozzleChipButton_hpp_
