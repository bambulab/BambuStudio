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

    dc.SetFont(Label::Body_13);
    const wxSize diameter_extent = m_diameter.IsEmpty() ? wxSize(0, 0) : dc.GetTextExtent(m_diameter);
    dc.SetFont(typeFont());
    const wxSize type_extent = m_type.IsEmpty() ? wxSize(0, 0) : dc.GetTextExtent(m_type);
    const int    spacing     = (!m_diameter.IsEmpty() && !m_type.IsEmpty()) ? FromDIP(3) : 0;
    const int    content_w   = diameter_extent.GetWidth() + spacing + type_extent.GetWidth();

    const wxSize size = GetSize();
    int          x    = std::max(0, (size.GetWidth() - content_w) / 2);
    dc.SetTextForeground(m_text_color);
    if (!m_diameter.IsEmpty()) {
        dc.SetFont(Label::Body_13);
        dc.DrawText(m_diameter, x, (size.GetHeight() - diameter_extent.GetHeight()) / 2);
        x += diameter_extent.GetWidth() + spacing;
    }
    if (!m_type.IsEmpty()) {
        dc.SetFont(typeFont());
        dc.DrawText(m_type, x, (size.GetHeight() - type_extent.GetHeight()) / 2);
    }
}
