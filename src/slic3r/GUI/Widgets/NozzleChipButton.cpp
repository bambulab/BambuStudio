#include "NozzleChipButton.hpp"
#include "Label.hpp"

#include <algorithm>

#include <wx/dcclient.h>

BEGIN_EVENT_TABLE(NozzleChipButton, Button)
EVT_PAINT(NozzleChipButton::paintEvent)
END_EVENT_TABLE()

NozzleChipButton::NozzleChipButton(wxWindow *parent)
    : Button(parent, wxEmptyString, "", wxBORDER_NONE)
    , m_padding(FromDIP(6), FromDIP(2))
{}

void NozzleChipButton::SetChipTexts(const wxString &diameter, const wxString &type, bool type_smaller)
{
    m_diameter     = diameter;
    m_type         = type;
    m_type_smaller = type_smaller;
    updateMinSize();
    Refresh();
}

void NozzleChipButton::SetTextColorNormal(wxColor const &color)
{
    m_text_color = color;
    Refresh();
}

wxFont NozzleChipButton::typeFont() const { return m_type_smaller ? Label::Body_10 : Label::Body_13; }

void NozzleChipButton::updateMinSize()
{
    wxClientDC dc(this);
    dc.SetFont(Label::Body_13);
    const wxSize diameter_extent = m_diameter.IsEmpty() ? wxSize(0, 0) : dc.GetTextExtent(m_diameter);
    dc.SetFont(typeFont());
    const wxSize type_extent = m_type.IsEmpty() ? wxSize(0, 0) : dc.GetTextExtent(m_type);
    const int    spacing     = (!m_diameter.IsEmpty() && !m_type.IsEmpty()) ? FromDIP(3) : 0;
    const int    content_w   = diameter_extent.GetWidth() + spacing + type_extent.GetWidth();
    const int    content_h   = std::max(diameter_extent.GetHeight(), type_extent.GetHeight());
    SetMinSize(wxSize(content_w + m_padding.GetWidth() * 2, content_h + m_padding.GetHeight() * 2));
}

void NozzleChipButton::paintEvent(wxPaintEvent & /*evt*/)
{
    wxPaintDC dc(this);
    // Background/border/corner-radius fill, driven by the same StateColor set via
    // SetBackgroundColor()/SetCornerRadius() by the owner -- Button::render() calls this same
    // StaticBox::render() first before drawing its own (single-font) text.
    StaticBox::render(dc);
    if (m_diameter.IsEmpty() && m_type.IsEmpty())
        return;

    // Descent is read alongside the extent because the two runs share a baseline rather than each
    // being centred on its own box. Centring independently lines up the boxes' centres, but a
    // smaller font has both a shorter box and a smaller descent, so its baseline lands higher than
    // the larger run's -- which is exactly the "TPU285 sits above 0.4" look. Only the flow run can
    // differ in size (TPU285 renders one step down), so the taller run defines the baseline and
    // nothing changes for the single-font case.
    wxCoord diameter_w = 0, diameter_h = 0, diameter_descent = 0;
    dc.SetFont(Label::Body_13);
    if (!m_diameter.IsEmpty())
        dc.GetTextExtent(m_diameter, &diameter_w, &diameter_h, &diameter_descent);
    wxCoord type_w = 0, type_h = 0, type_descent = 0;
    dc.SetFont(typeFont());
    if (!m_type.IsEmpty())
        dc.GetTextExtent(m_type, &type_w, &type_h, &type_descent);
    const int spacing   = (!m_diameter.IsEmpty() && !m_type.IsEmpty()) ? FromDIP(3) : 0;
    const int content_w = diameter_w + spacing + type_w;

    const wxSize size = GetSize();
    const int    tall_h       = std::max<int>(diameter_h, type_h);
    const int    tall_descent = diameter_h >= type_h ? diameter_descent : type_descent;
    const int    baseline     = (size.GetHeight() - tall_h) / 2 + (tall_h - tall_descent);

    int x = std::max(0, (size.GetWidth() - content_w) / 2);
    dc.SetTextForeground(m_text_color);
    if (!m_diameter.IsEmpty()) {
        dc.SetFont(Label::Body_13);
        dc.DrawText(m_diameter, x, baseline - (diameter_h - diameter_descent));
        x += diameter_w + spacing;
    }
    if (!m_type.IsEmpty()) {
        dc.SetFont(typeFont());
        dc.DrawText(m_type, x, baseline - (type_h - type_descent));
    }
}
