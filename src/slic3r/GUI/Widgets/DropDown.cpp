#include "DropDown.hpp"
#include "Label.hpp"

#include <wx/dcgraph.h>

BEGIN_EVENT_TABLE(DropDown, wxPanel)

EVT_LEFT_DOWN(DropDown::mouseDown)
EVT_LEFT_UP(DropDown::mouseReleased)
EVT_MOTION(DropDown::mouseMove)

// catch paint events
EVT_PAINT(DropDown::paintEvent)

END_EVENT_TABLE()

/*
 * Called by the system of by wxWidgets when the panel needs
 * to be redrawn. You can also trigger this call by
 * calling Refresh()/Update().
 */

DropDown::DropDown(wxWindow *             parent,
                   std::vector<wxString> &texts,
                   std::vector<wxBitmap> &icons,
                   long           style)
    : wxPopupTransientWindow(parent)
    , texts(texts)
    , icons(icons)
    , state_handler(this)
    , border_color(0xDBDBDB)
    , text_color(0x363636)
    , background_color(std::make_pair(0xEEEEEE, (int) StateColor::Hovered),
                       std::make_pair(*wxWHITE, (int) StateColor::Normal))
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    state_handler.attach({&border_color, &text_color, &background_color});
    state_handler.update_binds();
    state_handler.Bind(EVT_STATE_CHANGED, [this](auto &e) { paintNow(); });
    check_bitmap = ScalableBitmap(this, "checked", 16);

    // BBS set default font
    SetFont(Label::Body_14);
#ifdef __WXOSX__
    Bind(wxEVT_ACTIVATE, [this](auto e) {
        if (!e.GetActive())
            Hide();
    });
#endif
}

void DropDown::Invalidate(bool clear)
{
    if (clear)
        selection = -1;
    need_sync = true;
}

void DropDown::SetSelection(int n) { selection = n; }

wxString DropDown::GetValue() const
{
    return selection >= 0 ? texts[selection] : wxString();
}

void DropDown::SetValue(const wxString &value)
{
    auto i = std::find(texts.begin(), texts.end(), value);
    selection = i == texts.end() ? -1 : std::distance(texts.begin(), i);
}

void DropDown::SetCornerRadius(double radius)
{
    this->radius = radius;
    paintNow();
}

bool DropDown::SetForegroundColour(wxColour const &color)
{
    text_color = StateColor(color);
    state_handler.update_binds();
    return true;
}

bool DropDown::SetBackgroundColour(wxColour const& color)
{
    background_color = StateColor(color);
    state_handler.update_binds();
    return true;
}

void DropDown::SetBorderColor(StateColor const &color)
{
    border_color = color;
    state_handler.update_binds();
    paintNow();
}

void DropDown::SetForegroundColor(StateColor const &color)
{
    text_color = color;
    state_handler.update_binds();
    paintNow();
}

void DropDown::SetBackgroundColor(StateColor const &color)
{
    background_color = color;
    state_handler.update_binds();
    paintNow();
}

void DropDown::Rescale()
{
    need_sync = true;
    messureSize();
}

bool DropDown::HasDismissLongTime()
{
    auto now = boost::posix_time::microsec_clock::universal_time();
    return !IsShown() &&
        (now - dismissTime).total_milliseconds() >= 200;
}

void DropDown::paintEvent(wxPaintEvent& evt)
{
    // depending on your system you may need to look at double-buffered dcs
    wxBufferedPaintDC dc(this);
    render(dc);
}

/*
 * Alternatively, you can use a clientDC to paint on the panel
 * at any time. Using this generally does not free you from
 * catching paint events, since it is possible that e.g. the window
 * manager throws away your drawing when the window comes to the
 * background, and expects you will redraw it when the window comes
 * back (by sending a paint event).
 */
void DropDown::paintNow()
{
    // depending on your system you may need to look at double-buffered dcs
    //wxClientDC dc(this);
    //render(dc);
    Refresh();
}

/*
 * Here we do the actual rendering. I put it in a separate
 * method so that it can work no matter what type of DC
 * (e.g. wxPaintDC or wxClientDC) is used.
 */
void DropDown::render(wxDC &dc)
{
    if (texts.size() == 0) return;
    int states = state_handler.states();
    dc.SetPen(wxPen(border_color.colorForStates(states)));
    dc.SetBrush(wxBrush(
        background_color.colorForStates(states & ~StateColor::Hovered)));
    // if (GetWindowStyle() & wxBORDER_NONE)
    //    dc.SetPen(wxNullPen);

    // draw background
    wxSize size = GetSize();
    if (radius == 0)
        dc.DrawRectangle(0, 0, size.x, size.y);
    else
        dc.DrawRoundedRectangle(0, 0, size.x, size.y, radius);

    // draw hover background
    wxRect rcContent = {{0, offset.y}, rowSize};
    if (hover_item >= 0 && (states & StateColor::Hovered)) {
        dc.SetBrush(wxBrush(background_color.colorForStates(states)));
        rcContent.y += rowSize.y * hover_item;
        if (rcContent.GetBottom() > 0 && rcContent.y < size.y)
            dc.DrawRectangle(rcContent);
        rcContent.y = offset.y;
    }
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    {
        wxSize offset = (rowSize - textSize) / 2;
        rcContent.Deflate(0, offset.y);
    }

    // draw position bar
    if (rowSize.y * texts.size() > size.y) {
        int    height = rowSize.y * texts.size();
        wxRect rect = {size.x - 8, -offset.y * size.y / height, 8,
                       size.y * size.y / height};
        dc.SetBrush(wxBrush(*wxLIGHT_GREY));
        dc.DrawRoundedRectangle(rect, 4);
        rcContent.width -= 8;
    }

    // draw foreground
    rcContent.x += 5;
    rcContent.width -= 10;
    if (check_bitmap.bmp().IsOk()) {
        auto szBmp = check_bitmap.bmp().GetSize();
        if (selection >= 0) {
            wxPoint pt = rcContent.GetLeftTop();
            pt.y += (rcContent.height - szBmp.y) / 2;
            pt.y += rowSize.y * selection;
            if (pt.y + szBmp.y > 0 && pt.y < size.y)
                dc.DrawBitmap(check_bitmap.bmp(), pt);
        }
        rcContent.x += szBmp.x + 5;
        rcContent.width -= szBmp.x + 5;
    }
    dc.SetTextForeground(text_color.colorForStates(states));
    for (int i = 0; i < texts.size(); ++i) {
        if (rcContent.GetBottom() < 0) {
            rcContent.y += rowSize.y;
            continue;
        }
        if (rcContent.y > size.y) break;
        wxPoint pt   = rcContent.GetLeftTop();
        auto &  icon = icons[i];
        if (iconSize.x > 0) {
            if (icon.IsOk()) {
                pt.y += (rcContent.height - icon.GetSize().y) / 2;
                dc.DrawBitmap(icon, pt);
            }
            pt.x += iconSize.x + 5;
            pt.y = rcContent.y;
        }
        auto &text = texts[i];
        if (!text.IsEmpty()) {
            wxSize tSize   = GetTextExtent(text);
            dc.SetClippingRegion(pt, wxSize(rcContent.GetRight() - pt.x, rcContent.height));
            if (hover_item == i && pt.x + tSize.x > rcContent.GetRight()) {
                pt.x = rcContent.GetRight() - tSize.x;
            }
            pt.y += (rcContent.height - textSize.y) / 2;
            dc.SetFont(GetFont());
            dc.DrawText(text, pt);
            dc.DestroyClippingRegion();
        }
        rcContent.y += rowSize.y;
    }
}

void DropDown::messureSize()
{
    if (!need_sync) return;
    textSize = wxSize();
    for (auto & text : texts) {
        wxSize size = GetTextExtent(text);
        if (size.x > textSize.x) textSize = size;
    }
    iconSize = wxSize();
    for (auto &icon : icons) {
        if (icon.IsOk()) {
            wxSize size = icon.GetSize();
            if (size.x > iconSize.x) iconSize = size;
        }
    }
    wxSize szContent = textSize;
    if (iconSize.x > 0) {
        szContent.x += iconSize.x + 5;
        if (iconSize.y > szContent.y) szContent.y = iconSize.y;
    }
    szContent.y += 10;
    if (GetParent()) szContent.x = GetParent()->GetSize().x;
    rowSize = szContent;
    szContent.y *= texts.size();
    wxWindow::SetSize(szContent);
    need_sync = false;
}

void DropDown::autoPosition()
{
    messureSize();
    wxPoint pos = GetParent()->ClientToScreen(wxPoint(0, -6));
    wxPoint old = GetPosition();
    wxSize size = GetSize();
    Position(pos, {0, GetParent()->GetSize().y + 12});
    if (old != GetPosition()) {
        size = rowSize;
        size.y *= texts.size();
        offset = wxPoint();
        wxWindow::SetSize(size);
    }
    if (GetPosition().y > pos.y) {
        // may exceed
        wxSize dsize = wxDisplay(wxDisplay::GetFromWindow(this))
                           .GetClientArea()
                           .GetSize();
        if (GetPosition().y + size.y > dsize.y) {
            size.y = dsize.y - GetPosition().y;
            wxWindow::SetSize(size);
            if (selection >= 0) {
                if (offset.y + rowSize.y * (selection + 1) > size.y)
                    offset.y = size.y - rowSize.y * (selection + 1);
                else if (offset.y + rowSize.y * selection < 0)
                    offset.y = -rowSize.y * selection;
            }
        }
    }
}

void DropDown::mouseDown(wxMouseEvent& event)
{
    pressedDown = true;
    CaptureMouse();
    dragStart   = event.GetPosition();
}

void DropDown::mouseReleased(wxMouseEvent& event)
{
    if (pressedDown) {
        ReleaseMouse();
        dragStart = wxPoint();
        pressedDown = false;
        if (hover_item >= 0) // not moved
            sendDropDownEvent();
    }
}

void DropDown::mouseMove(wxMouseEvent &event)
{
    if (pressedDown) {
        wxPoint pt  = event.GetPosition();
        wxPoint pt2 = offset + pt - dragStart;
        dragStart = pt;
        if (pt2.y > 0)
            pt2.y = 0;
        else if (pt2.y + rowSize.y * texts.size() < GetSize().y)
            pt2.y = GetSize().y - rowSize.y * texts.size();
        if (pt2.y != offset.y) {
            offset = pt2;
            hover_item = -1; // moved
        } else {
            return;
        }
    }
    if (!pressedDown || hover_item >= 0) {
        int hover = event.GetPosition().y / rowSize.y;
        if (hover == hover_item) return;
        hover_item = hover;
    }
    paintNow();
}

// currently unused events
void DropDown::sendDropDownEvent()
{
    selection = hover_item;
    wxCommandEvent event(wxEVT_COMBOBOX, GetId());
    event.SetEventObject(this);
    event.SetInt(selection);
    event.SetString(GetValue());
    GetEventHandler()->ProcessEvent(event);
}

void DropDown::OnDismiss()
{
    dismissTime = boost::posix_time::microsec_clock::universal_time();
    hover_item  = -1;
}
