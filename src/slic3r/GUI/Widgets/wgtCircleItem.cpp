#include "wgtCircleItem.hpp"

#include "Label.hpp"
#include "StateColor.hpp"

#include <algorithm>
#include <wx/dc.h>
#include <wx/pen.h>

namespace {
constexpr int CIRCLE_ITEM_DEFAULT_DIAMETER_DIP = 24;
}

wgtCircleItem::wgtCircleItem()
    : StaticBox()
    // Use dark-map colors so dark mode keeps contrast: #DBDBDB -> #4A4A51,
    // TextSecondary (#323A3D) -> #E5E5E4. Plain #EDEDED is not remapped and
    // becomes light-on-light when text flips to the dark-mode secondary color.
    , m_circle_color(std::make_pair(wxColour("#DBDBDB"), (int) StateColor::Disabled), std::make_pair(wxColour("#DBDBDB"), (int) StateColor::Normal))
    , m_text_color(std::make_pair(ThemeColor::TextDisabled, (int) StateColor::Disabled), std::make_pair(ThemeColor::TextSecondary, (int) StateColor::Normal))
    , m_text_font(Label::Body_12)
{
    radius       = 0;
    border_width = 0;
}

wgtCircleItem::wgtCircleItem(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size, long style) : wgtCircleItem() { Create(parent, id, pos, size, style); }

bool wgtCircleItem::Create(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size, long style)
{
    if (!StaticBox::Create(parent, id, pos, size, style)) return false;

    SetBackgroundColour(GetParentBackgroundColor(parent));
    const int diameter = FromDIP(CIRCLE_ITEM_DEFAULT_DIAMETER_DIP);
    SetMinSize(wxSize(diameter, diameter));
    return true;
}

void wgtCircleItem::SetValue(int value)
{
    if (m_value == value) return;
    m_value = value;
    Refresh();
}

int wgtCircleItem::GetValue() const { return m_value; }

void wgtCircleItem::SetCircleColor(const StateColor &color)
{
    m_circle_color = color;
    Refresh();
}

void wgtCircleItem::SetTextColor(const StateColor &color)
{
    m_text_color = color;
    Refresh();
}

void wgtCircleItem::doRender(wxDC &dc)
{
    StaticBox::doRender(dc);

    const wxSize size = GetSize();
    if (size.x <= 0 || size.y <= 0) return;

    int states = state_handler.states();
    if (!IsEnabled()) states = StateColor::Disabled;

    const int diameter = std::min(size.x, size.y);
    const int left     = (size.x - diameter) / 2;
    const int top      = (size.y - diameter) / 2;

    const wxColor circle_color = m_circle_color.colorForStates(states);
    dc.SetPen(wxPen(circle_color));
    dc.SetBrush(wxBrush(circle_color));
    dc.DrawEllipse(left, top, diameter, diameter);

    dc.SetFont(m_text_font);
    dc.SetTextForeground(m_text_color.colorForStates(states));
    const wxString text      = wxString::Format("%d", m_value);
    const wxSize   text_size = dc.GetTextExtent(text);
    dc.DrawText(text, left + (diameter - text_size.x) / 2, top + (diameter - text_size.y) / 2);
}
