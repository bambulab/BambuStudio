#include "TabCtrl.hpp"

wxDEFINE_EVENT( wxEVT_TAB_SEL_CHANGING, wxCommandEvent );
wxDEFINE_EVENT( wxEVT_TAB_SEL_CHANGED, wxCommandEvent );

BEGIN_EVENT_TABLE(TabCtrl, StaticBox)

// catch paint events
END_EVENT_TABLE()

/*
 * Called by the system of by wxWidgets when the panel needs
 * to be redrawn. You can also trigger this call by
 * calling Refresh()/Update().
 */

TabCtrl::TabCtrl(wxWindow *      parent,
                   wxWindowID      id,
                   const wxPoint & pos,
                   const wxSize &  size,
                   long            style)
    : StaticBox(parent, id, pos, size, style)
{
    radius = 5;
    SetBorderColor(0xcecece);
    sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->AddSpacer(5);
    auto sizer2 = new wxBoxSizer(wxVERTICAL);
    sizer2->Add(sizer, 1, wxEXPAND | wxTOP | wxBOTTOM, 6);
    SetSizer(sizer2);
    Bind(wxEVT_COMMAND_BUTTON_CLICKED, &TabCtrl::buttonClicked, this);
    //wxString reason;
    //IsTransparentBackgroundSupported(&reason);
}

TabCtrl::~TabCtrl()
{
    delete images;
}

int TabCtrl::GetSelection() const { return sel; }

void TabCtrl::SelectItem(int item)
{
    if (item == sel || !sendTabCtrlEvent(true))
        return;
    if (sel >= 0) {
        wxCommandEvent e(wxEVT_CHECKBOX);
        auto b = btns[sel];
        e.SetEventObject(b);
        b->GetEventHandler()->ProcessEvent(e);
    }
    sel = item;
    if (sel >= 0) {
        wxCommandEvent e(wxEVT_CHECKBOX);
        auto b = btns[sel];
        e.SetEventObject(b);
        b->GetEventHandler()->ProcessEvent(e);
    }
    sendTabCtrlEvent();
    GetSizer()->Layout();
    Refresh();
}

void TabCtrl::Unselect()
{
    SelectItem(-1);
}

void TabCtrl::Rescale()
{
    for (auto & b : btns)
        b->Rescale();
}

bool TabCtrl::SetFont(wxFont const& font)
{
    StaticBox::SetFont(font);
    bold = font.Bold();
    for (size_t i = 0; i < btns.size(); ++i)
        btns[i]->SetFont(i == sel ? bold : font);
    return true;
}

int TabCtrl::AppendItem(const wxString &item,
                     int image, int selImage,
                     void * clientData)
{
    Button * btn = new Button();
    btn->Create(this, item, "", wxBORDER_NONE);
    btn->SetFont(GetFont());
    //btn->SetTextColor(StateColor(
    //    std::make_pair(*wxBLACK, (int) StateColor::Checked),
    //    std::make_pair(*wxBLACK, (int) StateColor::Hovered),
    //    std::make_pair(*wxLIGHT_GREY, (int) StateColor::Normal)));
    btn->SetBackgroundColor(GetBackgroundColour());
    btn->SetCornerRadius(0);
    btn->SetPaddingSize({0, 0});
    btns.push_back(btn);
    sizer->Add(btn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 10);
    return btns.size() - 1;
}

bool TabCtrl::DeleteItem(int item)
{
    return false;
}

void TabCtrl::DeleteAllItems()
{
    sizer->Clear(true);
    btns.clear();
    if (sel >= 0) {
        sel = -1;
        sendTabCtrlEvent();
    }
}

unsigned int TabCtrl::GetCount() const { return btns.size(); }

wxString TabCtrl::GetItemText(unsigned int item) const
{
    return item < btns.size() ? btns[item]->GetLabel() : wxString{};
}

void TabCtrl::SetItemText(unsigned int item, wxString const &value)
{
    if (item >= btns.size()) return;
    btns[item]->SetLabel(value);
}

bool TabCtrl::GetItemBold(unsigned int item) const
{
    if (item >= btns.size()) return false;
    return btns[item]->GetFont() == bold;
}

void TabCtrl::SetItemBold(unsigned int item, bool bold)
{
    if (item >= btns.size()) return;
    btns[item]->SetFont(bold ? this->bold : GetFont());
    btns[item]->Rescale();
}

void* TabCtrl::GetItemData(unsigned int item) const
{
    if (item >= btns.size()) return nullptr;
    return btns[item]->GetClientData();
}

void TabCtrl::SetItemData(unsigned int item, void* clientData)
{
    if (item >= btns.size()) return;
    btns[item]->SetClientData(clientData);
}

void TabCtrl::AssignImageList(wxImageList* imageList)
{
    if (images == imageList) return;
    delete images;
    images = imageList;
}

void TabCtrl::SetItemTextColour(unsigned int item, const wxColour& col)
{
    if (item >= btns.size()) return;
    btns[item]->SetTextColor(col);
}

int TabCtrl::GetFirstVisibleItem() const
{
    return btns.size() == 0 ? -1 : 0;
}

int TabCtrl::GetNextVisible(int item) const
{
    return ++item < btns.size() ? item : -1;
}

bool TabCtrl::IsVisible(unsigned int item) const
{
    return true;
}

void TabCtrl::buttonClicked(wxCommandEvent& event)
{
    auto btn = event.GetEventObject();
    auto iter = std::find(btns.begin(), btns.end(), btn);
    SelectItem(iter == btns.end() ? -1 : iter - btns.begin());
}

void TabCtrl::doRender(wxDC& dc)
{
    wxSize size = GetSize();
    int states = state_handler.states();
    dc.SetPen(wxPen(border_color.colorForStates(states), border_width));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    
    if (sel < 0) { return; }

    auto x1 = btns[sel]->GetPosition().x;
    auto x2 = x1 + btns[sel]->GetSize().x;
    x1 -= 5; x2 += 5;
    const int BS = border_width / 2;
    const int BS2 = (1 + border_width) / 2;
    dc.DrawLine(0, size.y - BS2, x1, size.y - BS2);
    dc.DrawArc(x1 - radius, size.y, x1, size.y - radius, x1 - radius, size.y - radius);
    dc.DrawLine(x1, size.y - radius, x1, radius);
    dc.DrawArc(x1 + radius, 0, x1, radius, x1 + radius, radius);
    dc.DrawLine(x1 + radius, BS, x2 - radius, BS);
    dc.DrawArc(x2, radius, x2 - radius, 0, x2 - radius, radius);
    dc.DrawLine(x2, radius, x2, size.y - radius);
    dc.DrawArc(x2, size.y - radius, x2 + radius, size.y, x2 + radius, size.y - radius);
    dc.DrawLine(x2 + radius, size.y - BS2, size.x, size.y - BS2);
}

bool TabCtrl::sendTabCtrlEvent(bool changing)
{
    wxCommandEvent event(changing ? wxEVT_TAB_SEL_CHANGING : wxEVT_TAB_SEL_CHANGED, GetId());
    event.SetEventObject(this);
    event.SetInt(sel);
    GetEventHandler()->ProcessEvent(event);
    return true;
}
