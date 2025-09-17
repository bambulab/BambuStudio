#include "StaticGroup.hpp"
#include "Label.hpp"


class HoverLabel : public wxPanel
{
public:
    HoverLabel(wxWindow* parent, const wxString& label):wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
    {
        SetBackgroundColour(*wxWHITE);

        auto sizer = new wxBoxSizer(wxHORIZONTAL);

        m_label = new wxStaticText(this, wxID_ANY, label, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
        m_label->SetFont(Label::Body_13);
        m_label->SetForegroundColour("#6B6B6B");
        m_count = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
        m_count->SetFont(Label::Body_13.Bold());
        m_count->SetForegroundColour("#262E30");

        m_brace_left = new wxStaticText(this, wxID_ANY, "(", wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
        m_brace_left->SetFont(Label::Body_13);
        m_brace_left->SetForegroundColour("#262E30");

        m_brace_right = new wxStaticText(this, wxID_ANY, ")", wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
        m_brace_right->SetFont(Label::Body_13);
        m_brace_right->SetForegroundColour("#262E30");

        auto hover_icon = create_scaled_bitmap("dot");
        m_hover_btn = new wxBitmapButton(this, wxID_ANY, hover_icon, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
#ifdef __WXOSX__
        m_hover_btn->SetBackgroundColour("#F7F7F7");
#else
        m_hover_btn->SetBackgroundColour(*wxWHITE);
#endif
        sizer->Add(m_label, 0, wxALIGN_CENTER_VERTICAL);
        sizer->Add(m_brace_left, 0, wxALIGN_CENTER_VERTICAL);
        sizer->Add(m_count, 0, wxALIGN_CENTER_VERTICAL);
        sizer->Add(m_brace_right, 0, wxALIGN_CENTER_VERTICAL);
        sizer->Add(m_hover_btn, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 5);

        SetSizerAndFit(sizer);
        Layout();
        m_hover_btn->Bind(wxEVT_BUTTON, [this](auto &evt) {
            if(m_enabled)
                m_hover_on_click();
        });
    }


    void EnableEdit(bool enable){
        m_enabled = enable;
        if(enable){
            m_hover_btn->SetBitmap(create_scaled_bitmap("edit"));
        }
        else{
            m_hover_btn->SetBitmap(create_scaled_bitmap("dot"));
        }
    }
    void SetOnHoverClick(std::function<void()> on_click) { 
        m_hover_on_click = on_click;
    }

    void SetCount(int count) {
        if(count == -1){
            m_brace_left->Hide();
            m_brace_right->Hide();
            m_count->Hide();
        }
        else{
            if(!m_count->IsShown()){
                m_brace_left->Show();
                m_count->Show();
                m_brace_right->Show();
            }
            wxString count_str = wxString::Format("%d", count);
            m_count->SetLabel(count_str);
        }
        Layout();
        Fit();
    }

private:
    wxStaticText* m_label;
    wxBitmapButton* m_hover_btn;
    wxStaticText* m_count;
    wxStaticText* m_brace_left;
    wxStaticText* m_brace_right;
    std::function<void()> m_hover_on_click;
    bool m_enabled;

};

StaticGroup::StaticGroup(wxWindow *parent, wxWindowID id, const wxString &label)
    : wxStaticBox(parent, id,"")
{
    hoverLabel_ = new HoverLabel(this, label);
    this->m_labelWin = hoverLabel_;
    SetBackgroundColour(*wxWHITE);
    SetForegroundColour("#CECECE");
    borderColor_ = wxColour("#CECECE");
#ifdef __WXMSW__
    Bind(wxEVT_PAINT, &StaticGroup::OnPaint, this);
#endif
}

void StaticGroup_layoutBadge(void * group, void * badge);

void StaticGroup::SetEditEnabled(bool enable)
{
    if (hoverLabel_) {
        hoverLabel_->EnableEdit(enable);
    }
}

void StaticGroup::SetCount(int count)
{
    if (hoverLabel_) {
        hoverLabel_->SetCount(count);
    }
}

void StaticGroup::SetOnHoverClick(std::function<void()>on_click)
{
    if (hoverLabel_) {
        hoverLabel_->SetOnHoverClick(on_click);
    }
}


void StaticGroup::ShowBadge(bool show)
{
#ifdef __WXMSW__
    if (show && badge.name() != "badge") {
        badge = ScalableBitmap(this, "badge", 18);
        Refresh();
    } else if (!show && !badge.name().empty()) {
        badge = ScalableBitmap{};
        Refresh();
    }
#endif
#ifdef __WXOSX__
    if (show && badge == nullptr) {
        badge = new ScalableButton(this, wxID_ANY, "badge", wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, false, 18);
        badge->SetSize(badge->GetBestSize());
        badge->SetBackgroundColour("#F7F7F7");
        StaticGroup_layoutBadge(GetHandle(), badge->GetHandle());
    }
    if (badge && badge->IsShown() != show)
        badge->Show(show);
#endif
}

void StaticGroup::SetBorderColor(const wxColour &color)
{
    borderColor_ = color;
}

#ifdef __WXMSW__
void StaticGroup::OnPaint(wxPaintEvent &evt)
{
    wxStaticBox::OnPaint(evt);
    if (badge.bmp().IsOk()) {
        auto s = badge.bmp().GetScaledSize();
        wxPaintDC dc(this);
        dc.DrawBitmap(badge.bmp(), GetSize().x - s.x, 8);
    }
}

void StaticGroup::PaintForeground(wxDC &dc, const struct tagRECT &rc)
{
    wxStaticBox::PaintForeground(dc, rc);
    auto mdc = dynamic_cast<wxMemoryDC *>(&dc);
    auto image = mdc->GetSelectedBitmap().ConvertToImage();
    // Found border coords
    int top = 0;
    int left = 0;
    int right = rc.right - 1;
    int bottom = rc.bottom - 1;
    auto blue  = GetBackgroundColour().Blue();
    while (image.GetBlue(0, top) == blue && top < bottom) ++top;
    while (image.GetBlue(left, top) != blue && left < right) ++left; // --left; // fix start
    while (image.GetBlue(right, top) != blue && right > 0) --right;
    ++right;
    while (image.GetBlue(0, bottom) == blue && bottom > 0) --bottom;
    // Draw border with foreground color
    wxPoint polygon[] = { {left, top}, {0, top}, {0, bottom}, {rc.right - 1, bottom}, {rc.right - 1, top}, {right, top} };
    dc.SetPen(wxPen(borderColor_, 1));
    for (int i = 1; i < 6; ++i) {
        if (i == 4) // fix bottom right corner
            ++polygon[i - 1].y;
        dc.DrawLine(polygon[i - 1], polygon[i]);
    }
}

#endif
