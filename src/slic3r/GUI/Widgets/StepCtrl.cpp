#include "StepCtrl.hpp"
#include "Label.hpp"

wxDEFINE_EVENT( EVT_STEP_CHANGING, wxCommandEvent );
wxDEFINE_EVENT( EVT_STEP_CHANGED, wxCommandEvent );

BEGIN_EVENT_TABLE(StepCtrl, StaticBox)
EVT_LEFT_DOWN(StepCtrl::mouseDown)
EVT_MOTION(StepCtrl::mouseMove)
EVT_LEFT_UP(StepCtrl::mouseUp)
END_EVENT_TABLE()

StepCtrl::StepCtrl(wxWindow *      parent,
                   wxWindowID      id,
                   const wxPoint & pos,
                   const wxSize &  size,
                   long            style)
    : StaticBox(parent, id, pos, size, style)
    , font_tip(Label::Body_16)
    , clr_bar(0xACACAC)
    , clr_text(std::make_pair(0x00AE42, (int) StateColor::Checked), 
            std::make_pair(0x6B6B6B, (int) StateColor::Normal))
    , clr_tip(0x828280)
    , bmp_thumb(this, "step_thumb", 36)
{
    SetFont(Label::Body_14);
    border_color     = StateColor(*wxLIGHT_GREY);
    StaticBox::radius = 0;
    border_width = 3;
    //wxString reason;
    //IsTransparentBackgroundSupported(&reason);
}

StepCtrl::~StepCtrl()
{
}

int StepCtrl::GetSelection() const { return step; }

void StepCtrl::SelectItem(int item)
{
    if (item == step || !sendStepCtrlEvent(true))
        return;
    step = item;
    sendStepCtrlEvent();
    Refresh();
}

void StepCtrl::Rescale()
{
    bmp_thumb.msw_rescale();
}

bool StepCtrl::SetTipFont(wxFont const& font)
{
    font_tip = font;
    return true;
}

int StepCtrl::AppendItem(const wxString &item, wxString const & tip)
{
    steps.push_back(item);
    tips.push_back(tip);
    return steps.size() - 1;
}

void StepCtrl::DeleteAllItems()
{
    steps.clear();
    tips.clear();
    if (step >= 0) {
        step = -1;
        sendStepCtrlEvent();
    }
}

unsigned int StepCtrl::GetCount() const { return steps.size(); }

wxString StepCtrl::GetItemText(unsigned int item) const
{
    return item < steps.size() ? steps[item] : wxString{};
}

void StepCtrl::SetItemText(unsigned int item, wxString const &value)
{
    if (item >= steps.size()) return;
    steps[item] = value;
}

void StepCtrl::mouseDown(wxMouseEvent &event)
{
    wxPoint pt;
    event.GetPosition(&pt.x, &pt.y);
    wxSize size      = GetSize();
    int    itemWidth = size.x / steps.size();
    wxRect rcBar     = {0, (size.y - 60) / 2, size.x, 60};
    int    circleX   = itemWidth / 2+ itemWidth * step;
    wxRect rcThumb   = { {circleX, size.y / 2}, bmp_thumb.GetBmpSize()};
    rcThumb.x -= rcThumb.width / 2;
    rcThumb.y -= rcThumb.height / 2;
    if (rcThumb.Contains(pt)) {
        pos_thumb   = wxPoint { circleX, size.y / 2 };
        drag_offset = pos_thumb - pt;
    } else if (rcBar.Contains(pt)) {
        if (pt.x < circleX) { if (step > 0) SelectItem(step - 1); }
        else { if (step < steps.size() - 1) SelectItem(step + 1); }
    }
}

void StepCtrl::mouseMove(wxMouseEvent &event)
{
    if (pos_thumb == wxPoint{0, 0})
        return;
    wxPoint pt;
    event.GetPosition(&pt.x, &pt.y);
    pos_thumb.x = pt.x + drag_offset.x;
    Refresh();
}

void StepCtrl::mouseUp(wxMouseEvent &event)
{
    if (pos_thumb == wxPoint{0, 0})
        return;
    wxSize size      = GetSize();
    int    itemWidth = size.x / steps.size();
    int index = pos_thumb.x / itemWidth;
    pos_thumb        = {0, 0};
    SelectItem(index < steps.size() ? index : steps.size() - 1);
}

void StepCtrl::doRender(wxDC& dc)
{
    if (steps.empty()) return;

    StaticBox::doRender(dc);

    wxSize size = GetSize();
    int states = state_handler.states();

    int itemWidth = size.x / steps.size();
    wxRect rcBar = {itemWidth / 2, (size.y - bar_width) / 2, size.x - itemWidth, bar_width};

    dc.SetPen(wxPen(clr_bar.colorForStates(states)));
    dc.SetBrush(wxBrush(clr_bar.colorForStates(states)));
    dc.DrawRectangle(rcBar);
    int circleX = itemWidth / 2;
    int circleY = size.y / 2;
    for (int i = 0; i < steps.size(); ++i) {
        bool check = pos_thumb == wxPoint{0, 0} ? step == i 
                : (pos_thumb.x >= circleX - itemWidth / 2 && pos_thumb.x < circleX + itemWidth / 2);
        dc.DrawEllipse(circleX - radius, circleY - radius, radius * 2, radius * 2);
        dc.SetTextForeground(clr_text.colorForStates(states | (check ? StateColor::Checked : 0)));
        wxSize sz = dc.GetTextExtent(steps[i]);
        dc.DrawText(steps[i], circleX - sz.x / 2, circleY + 20);
        if (check) {
            dc.SetTextForeground(clr_tip.colorForStates(states));
            wxSize sz = dc.GetTextExtent(tips[i]);
            dc.DrawText(tips[i], circleX - sz.x / 2, circleY - 20 - sz.y);
            sz = bmp_thumb.GetBmpSize();
            dc.DrawBitmap(bmp_thumb.bmp(), circleX - sz.x / 2, circleY - sz.y / 2);
        }
        circleX += itemWidth;
    }
}

bool StepCtrl::sendStepCtrlEvent(bool changing)
{
    wxCommandEvent event(changing ? EVT_STEP_CHANGING : EVT_STEP_CHANGED, GetId());
    event.SetEventObject(this);
    event.SetInt(step);
    GetEventHandler()->ProcessEvent(event);
    return true;
}
