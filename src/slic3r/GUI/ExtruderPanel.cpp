#include "ExtruderPanel.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/settings.h>
#include <wx/dcbuffer.h>

#include "I18N.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "wxExtensions.hpp"
#include "Widgets/Label.hpp"
#include "DeviceCore/DevManager.h"
#include "wx/graphics.h"

namespace Slic3r {
namespace GUI {

static const wxColour SelectedBgColor    = wxColour("#00AE42");
static const wxColour SelectedBorderColor = SelectedBgColor;
static const wxColour SelectedTextColor  = wxColour("#FFFFFE");
static const wxColour HasNozzleBgColor   = wxColour("#FFFFFF");
static const wxColour HasNozzleBorderColor = wxColour("#CECECE");
static const wxColour HasNozzleTextColor = wxColour("#262E30");
static const wxColour NoNozzleBgColor    = wxColour("#FFFFFF");
static const wxColour NoNozzleBorderColor  = wxColour("#EEEEEE");
static const wxColour NoNozzleTextColor  = wxColour("#CECECE");

static const wxColour DiameterNormalColor = wxColour("#6B6B6B");
static const wxColour FlowNormalColor     = wxColour("#999999");
static const wxColour DiameterDisabledColor = wxColour("#CECECE");
static const wxColour FlowDisabledColor     = wxColour("#DCDCDC");

DiameterButtonPanel::DiameterButtonPanel(wxWindow *parent, const std::vector<wxString> &diameter_choices, bool show_title)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE), m_choices(diameter_choices), m_show_title(show_title)
{
    SetBackgroundColour(*wxWHITE);
    if (!m_choices.empty()) {
        m_selected_diameter = m_choices[0];
    }

    CreateLayout();

    wxGetApp().UpdateDarkUIWin(this);
}

void DiameterButtonPanel::CreateLayout()
{
    if (GetSizer()) { GetSizer()->Clear(true); }
    auto main_sizer = new wxBoxSizer(wxVERTICAL);
    if (m_show_title) {
        auto title = new Label(this, _L("Set nozzle diameter"));
        title->SetFont(Label::Body_13);
        title->SetForegroundColour(wxColour("#999999"));
        main_sizer->Add(title, 0, wxALIGN_LEFT | wxBOTTOM, FromDIP(4));
    }
    auto button_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_buttons.clear();
    for (const auto &diameter : m_choices) {
        auto btn = new Button(this, diameter);
        btn->SetMinSize(wxSize(FromDIP(54), FromDIP(24)));
        btn->SetMaxSize(wxSize(FromDIP(54), FromDIP(24)));
        btn->SetFont(Label::Body_12);
        btn->SetCornerRadius(FromDIP(6));

        UpdateSingleButtonState(btn, diameter);
        btn->Bind(wxEVT_BUTTON, &DiameterButtonPanel::OnButtonClicked, this);
        m_buttons.push_back(btn);
        button_sizer->Add(btn, 0, wxRIGHT, FromDIP(4));
    }

    main_sizer->Add(button_sizer, 0, wxALIGN_LEFT);
    SetSizer(main_sizer);
    Layout();
}

void DiameterButtonPanel::RefreshLayout(const std::vector<wxString> &choices)
{
    if (choices == m_choices) { return; }
    m_choices            = choices;
    bool selected_exists = std::find(m_choices.begin(), m_choices.end(), m_selected_diameter) != m_choices.end();
    if (!selected_exists && !m_choices.empty()) { m_selected_diameter = m_choices[0]; }

    CreateLayout();

    Refresh();
    Update();
}

void DiameterButtonPanel::OnButtonClicked(wxCommandEvent &event)
{
    Button *clicked_btn = dynamic_cast<Button *>(event.GetEventObject());
    if (!clicked_btn) return;
    if (clicked_btn->IsGrayed()) return;

    wxString diameter = clicked_btn->GetLabel();
    SetSelectedDiameter(diameter);
    event.Skip();
}

void DiameterButtonPanel::SetSelectedDiameter(const wxString &diameter)
{
    if (m_selected_diameter == diameter) {
        UpdateButtonStates();
        return;
    }

    wxString original_diameter = m_selected_diameter;
    m_selected_diameter        = diameter;
    UpdateButtonStates();

    /*wxCommandEvent evt(EVT_NOZZLE_DIAMETER_SELECTED, GetId());
    evt.SetEventObject(this);
    evt.SetString(diameter);
    evt.SetClientData(new wxString(original_diameter));
    GetEventHandler()->ProcessEvent(evt);*/
}

void DiameterButtonPanel::UpdateButtonStates()
{
    assert(m_buttons.size() == m_choices.size());
    for (size_t i = 0; i < m_buttons.size() && i < m_choices.size(); ++i) {
        UpdateSingleButtonState(m_buttons[i], m_choices[i]);
    }
}

void DiameterButtonPanel::Rescale()
{
    for (auto *btn : m_buttons) {
        if (btn) {
            btn->SetMinSize(wxSize(FromDIP(54), FromDIP(24)));
            btn->SetMaxSize(wxSize(FromDIP(54), FromDIP(24)));
            btn->SetFont(Label::Body_12);
            btn->SetCornerRadius(FromDIP(6));
            btn->Rescale();
            btn->SetCenter(true);
            btn->InvalidateBestSize();
            btn->SetSize(btn->GetMinSize());
            btn->Layout();
            btn->Refresh();
        }
    }
    if (GetSizer()) {
        GetSizer()->Layout();
    }
    Layout();
    Refresh();
}

void DiameterButtonPanel::UpdateSingleButtonState(Button *btn, const wxString &diameter)
{
    if (!btn) return;

    ButtonState state = NoNozzle;

    if (diameter == m_selected_diameter) {
        state = Selected;
    } else {
        if (m_nozzle_query_callback) {
            bool has_nozzle = m_nozzle_query_callback(diameter);
            state           = has_nozzle ? HasNozzle : NoNozzle;
        } else {
            state = NoNozzle;
        }
    }

    StateColor bg_color, text_color, border_color;

    switch (state) {
    case Selected:
        bg_color.append(SelectedBgColor, StateColor::Normal);
        text_color.append(SelectedTextColor, StateColor::Normal);
        border_color.append(SelectedBorderColor, StateColor::Normal);
        break;
    case HasNozzle:
        bg_color.append(HasNozzleBgColor, StateColor::Normal);
        text_color.append(HasNozzleTextColor, StateColor::Normal);
        border_color.append(HasNozzleBorderColor, StateColor::Normal);
        break;
    case NoNozzle:
        bg_color.append(NoNozzleBgColor, StateColor::Normal);
        text_color.append(NoNozzleTextColor, StateColor::Normal);
        border_color.append(NoNozzleBorderColor, StateColor::Normal);
        break;
    }

    btn->SetBackgroundColor(bg_color);
    btn->SetTextColor(text_color);
    btn->SetBorderColor(border_color);
    btn->SetGrayed(state == NoNozzle);

    wxString tip = wxString::Format(_L("Please set a %smm diameter nozzle for the printer in the section below first before using this diameter for slicing."), btn->GetLabel());
    state == NoNozzle ? btn->SetToolTip(tip) : btn->UnsetToolTip();
    btn->Refresh();
}

DiameterButtonPanel::ButtonState DiameterButtonPanel::GetButtonState(const wxString &diameter) const
{
    for (size_t i = 0; i < m_choices.size(); ++i) {
        if (m_choices[i] == diameter && i < m_buttons.size()) {
            Button *btn = m_buttons[i];

            if (i == m_selected_index) { return ButtonState::Selected; }

            if (m_nozzle_query_callback && m_nozzle_query_callback(diameter)) {
                return ButtonState::HasNozzle;
            } else {
                return ButtonState::NoNozzle;
            }
        }
    }

    return ButtonState::NoNozzle;
}

HorizontalScrollablePanel::HorizontalScrollablePanel(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size) : wxScrolledWindow(parent, id, pos, size, wxHSCROLL)
{
    auto sizer = new wxBoxSizer(wxHORIZONTAL);
    SetSizer(sizer);

    SetScrollRate(FromDIP(1), 0);
    SetBackgroundColour(wxColour("#F7F7F7"));
}

void HorizontalScrollablePanel::Clear()
{
    GetSizer()->Clear(true);
    //UpdateScrollbars();
}

ExtruderPanel::ExtruderPanel(wxWindow *parent, GroupType type)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE), m_type(type)
{
    SetForegroundColour(wxColour(206, 206, 206));
    SetBackgroundColour(wxColour("#F7F7F7"));

    m_scroll_timer = new wxTimer(this);
    Bind(wxEVT_TIMER, &ExtruderPanel::OnScrollTimer, this);

    CreateLayout();
}

void ExtruderPanel::CreateLayout()
{
    SetMinSize(wxSize(-1, FromDIP(40)));
    m_main_sizer = new wxBoxSizer(wxHORIZONTAL);
    icon = create_scaled_bitmap("dev_rack_nozzle_error", this, 24);
    m_nozzle_icon = new wxStaticBitmap(this, wxID_ANY, icon);

    wxBoxSizer *label_sizer = new wxBoxSizer(wxVERTICAL);
    m_diameter_label        = new Label(this, "0.4");
    m_diameter_label->SetFont(Label::Body_12);
    m_diameter_label->SetForegroundColour(DiameterNormalColor);
    m_diameter_label->SetBackgroundColour(wxColour("#F7F7F7"));

    m_flow_label = new Label(this, "Std");
    m_flow_label->SetFont(Label::Body_12);
    m_flow_label->SetForegroundColour(FlowNormalColor);
    m_flow_label->SetBackgroundColour(wxColour("#F7F7F7"));

    label_sizer->Add(m_diameter_label, 0, wxALIGN_LEFT | wxBOTTOM, FromDIP(2));
    label_sizer->Add(m_flow_label, 0, wxALIGN_LEFT);

    m_nozzle_number_label = new Label(this, "x1");
    m_nozzle_number_label->SetFont(Label::Body_15);
    m_nozzle_number_label->SetForegroundColour(DiameterNormalColor);
    m_nozzle_number_label->SetBackgroundColour(wxColour("#F7F7F7"));
    m_nozzle_number_label->Show(m_type == SingleExtruder);

    m_nozzle_index_label = new Label(this, "No Available Nozzle");
    m_nozzle_index_label->SetFont(Label::Body_12);
    m_nozzle_index_label->SetForegroundColour(DiameterNormalColor);
    m_nozzle_index_label->SetBackgroundColour(wxColour("#F7F7F7"));
    m_nozzle_index_label->Show(m_type == SingleExtruder);

    m_disabled_icon = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("warning", this, 20), wxDefaultPosition, wxSize(FromDIP(20), FromDIP(20)), 0);
    m_disabled_icon->Hide();

    m_main_sizer->Add(m_nozzle_icon, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(8));
    m_main_sizer->Add(label_sizer, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    m_main_sizer->Add(m_nozzle_number_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

    if (m_type == SingleExtruder) {
        m_main_sizer->Add(m_nozzle_index_label, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    } else {
        m_scroll = new HorizontalScrollablePanel(this);
        m_scroll->SetMinSize(wxSize(FromDIP(50), FromDIP(40)));
        m_ams_sizer = new wxBoxSizer(wxHORIZONTAL);
        m_scroll->SetSizer(m_ams_sizer);

        for (size_t i = 0; i < 4; ++i) {
            AMSinfo info;
            info.ams_type = DevAmsType::AMS;
            auto preview = new AMSPreview(m_scroll, wxID_ANY, info);
            preview->Close();
            m_ams_previews.push_back(preview);
            m_ams_sizer->Add(preview, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(2));
        }

        for (size_t i = 0; i < 8; ++i) {
            AMSinfo info;
            info.ams_type = DevAmsType::N3S;
            auto preview = new AMSPreview(m_scroll, wxID_ANY, info);
            preview->Close();
            m_ams_previews.push_back(preview);
            m_ams_sizer->Add(preview, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(2));
        }

        m_left_scroll  = new ScalableButton(this, wxID_ANY, "left_arrow", wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, true, 12);
        m_right_scroll = new ScalableButton(this, wxID_ANY, "right_arrow", wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, true, 12);

        m_left_scroll->Bind(wxEVT_BUTTON, &ExtruderPanel::OnLeftScrollClick, this);
        m_right_scroll->Bind(wxEVT_BUTTON, &ExtruderPanel::OnRightScrollClick, this);

        m_left_scroll->Bind(wxEVT_LEFT_DOWN, &ExtruderPanel::OnLeftScrollDown, this);
        m_left_scroll->Bind(wxEVT_LEFT_UP, &ExtruderPanel::OnLeftScrollUp, this);
        m_left_scroll->Bind(wxEVT_LEAVE_WINDOW, &ExtruderPanel::OnLeftScrollUp, this);

        m_right_scroll->Bind(wxEVT_LEFT_DOWN, &ExtruderPanel::OnRightScrollDown, this);
        m_right_scroll->Bind(wxEVT_LEFT_UP, &ExtruderPanel::OnRightScrollUp, this);
        m_right_scroll->Bind(wxEVT_LEAVE_WINDOW, &ExtruderPanel::OnRightScrollUp, this);

        m_left_scroll->Show(false);
        m_right_scroll->Show(false);

        m_main_sizer->Add(m_scroll, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
        m_main_sizer->Add(m_left_scroll, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(0));
        m_main_sizer->Add(m_right_scroll, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    }
    m_main_sizer->Add(m_disabled_icon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

    SetSizer(m_main_sizer);
    Layout();

    wxGetApp().UpdateDarkUIWin(this);
    CallAfter([this]() { update_ams(); });
}

void ExtruderPanel::ShowEnable(bool enable)
{
    m_disabled = !enable;
    wxColour text_color = enable ? DiameterNormalColor : DiameterDisabledColor;
    wxColour flow_color = enable ? FlowNormalColor : DiameterDisabledColor;
    if (m_disabled_icon) {
        m_disabled_icon->Show(!enable);
    }
    if (m_left_scroll && m_right_scroll) {
        if (enable) {
            UpdateScrollButtons();
        } else {
            m_left_scroll->Show(false);
            m_right_scroll->Show(false);
        }
    }

    auto tooltip = _L("BambuStudio does not support slicing with mixed diameters. Extruders with diffenrent diameters from the printing diameter are not available. If you want to use this extruder, you can either modify nozzle diameter or click to adjust extruder settings.");

    std::function<void(wxWindow*)> set_children_tooltip = [this, &tooltip, enable, &set_children_tooltip](wxWindow *window) {
        if (!window) return;

        enable ? window->UnsetToolTip() : window->SetToolTip(tooltip);
        for (auto *child : window->GetChildren()) {
            set_children_tooltip(child);
        }
    };

    if (m_diameter_label) {
        m_diameter_label->SetForegroundColour(text_color);
    }
    if (m_flow_label) {
        m_flow_label->SetForegroundColour(flow_color);
    }
    if (m_nozzle_number_label) {
        m_nozzle_number_label->SetForegroundColour(text_color);
    }
    if (m_nozzle_index_label) {
        m_nozzle_index_label->SetForegroundColour(text_color);
    }
    set_children_tooltip(this);
    Layout();
    Refresh();
    wxGetApp().UpdateDarkUIWin(this);
}

void ExtruderPanel::SetDiameter(const wxString& diameter)
{
    m_diameter_label->SetLabel(diameter);
}

void ExtruderPanel::SetFlow(const wxString& flow)
{
    m_flow_label->SetLabel(flow);
}

void ExtruderPanel::SetNozzleNumber(int num)
{
    wxString text = wxString::Format("x%d", num);
    m_nozzle_number_label->SetLabel(text);
    m_nozzle_number_label->Show(num > 1 || (num == 0 && m_type == RightExtruder));
}

void ExtruderPanel::SetNozzleIndex(std::vector<int> indices)
{
    if (indices.empty()) {
        m_nozzle_index_label->SetLabel(_L("No Available Nozzle"));
        return;
    }
    wxString text = _L("Indices: ");
    for (size_t i = 0; i < indices.size(); ++i) {
        text += wxString::Format("%d", indices[i]);
        if (i != indices.size() - 1) {
            text += ", ";
        }
    }
    m_nozzle_index_label->SetLabel(text);
}

void ExtruderPanel::Rescale()
{
    for (auto* ams : m_ams_previews) {
        if (ams) {
            ams->msw_rescale();
        }
    }

    for (auto* ext : m_ext_previews) {
        if (ext) {
            ext->msw_rescale();
        }
    }

    if (m_scroll) {
        m_scroll->SetScrollRate(FromDIP(5), FromDIP(5));
        m_scroll->FitInside();
    }

    if (GetSizer()) {
        GetSizer()->Layout();
    }

    Layout();
    Refresh();
}

void ExtruderPanel::set_ams_count(int n4, int n1)
{
    if (n4 == m_ams_n4 && n1 == m_ams_n1)
        return;

    m_ams_n4 = n4;
    m_ams_n1 = n1;
    update_ams();
}

void ExtruderPanel::update_ams()
{
    if (!m_ams_sizer) return;

    static AMSinfo info4;
    static AMSinfo info1;
    static AMSinfo info_ext;
    if (info4.cans.empty()) {
        for (size_t i = 0; i < 4; ++i) {
            info4.cans.push_back({});
        }
        info1.ams_type = DevAmsType::N3S;
        info1.cans.push_back({});

        info_ext.ams_type = DevAmsType::EXT_SPOOL;
        info_ext.cans.push_back({});
    }

    m_ams_sizer->Clear(false);

    for (auto preview : m_ams_previews) {
        preview->Close();
    }

    size_t preview_idx = 0;

    // update 4-slot AMS（first 4 previews）
    for (size_t i = 0; i < m_ams_n4 && i < 4; ++i) {
        AMSinfo &ams_info = i < m_ams_4.size() ? m_ams_4[i] : info4;

        m_ams_previews[preview_idx]->Update(ams_info);
        m_ams_previews[preview_idx]->Refresh();
        m_ams_previews[preview_idx]->Open();

        m_ams_sizer->Add(m_ams_previews[preview_idx], 0, wxALL, FromDIP(2));
        preview_idx++;
    }

    // update 1-slot AMS（last 8 previews）
    size_t ams1_start_idx = 4;
    for (size_t i = 0; i < m_ams_n1 && i < 8; ++i) {
        AMSinfo &ams_info = i < m_ams_1.size() ? m_ams_1[i] : info1;

        size_t idx = ams1_start_idx + i;
        m_ams_previews[idx]->Update(ams_info);
        m_ams_previews[idx]->Refresh();
        m_ams_previews[idx]->Open();

        m_ams_sizer->Add(m_ams_previews[idx], 0, wxALL, FromDIP(2));
    }

    wxWindow *parent_window = m_scroll ? m_scroll : m_ams_sizer->GetContainingWindow();
    if (!m_ext.empty()) {
        while (m_ext_previews.size() < m_ext.size()) {
            AMSinfo info;
            info.ams_type = DevAmsType::N3S;
            auto ext_preview = new AMSPreview(parent_window, wxID_ANY, info);
            ext_preview->Close();
            m_ext_previews.push_back(ext_preview);
        }

        for (size_t i = 0; i < m_ext.size(); ++i) {
            if (i < m_ext_previews.size()) {
                AMSinfo &ext_info = m_ext[i];
                m_ext_previews[i]->Update(ext_info);
                m_ext_previews[i]->Refresh();
                m_ext_previews[i]->Open();

                m_ams_sizer->Add(m_ext_previews[i], 0, wxALL, FromDIP(2));
            }
        }
    } else {
        if (m_ext_previews.empty()) {
            AMSinfo info;
            info.ams_type = DevAmsType::EXT_SPOOL;
            auto ext_preview = new AMSPreview(parent_window, wxID_ANY, info);
            ext_preview->Close();
            m_ext_previews.push_back(ext_preview);
        }

        m_ext_previews[0]->Update(info_ext);
        m_ext_previews[0]->Refresh();
        m_ext_previews[0]->Open();
        m_ams_sizer->Add(m_ext_previews[0], 0, wxALL, FromDIP(2));
    }

    Layout();

    if (m_scroll) {
        m_scroll->FitInside();
        int virtual_width = m_scroll->GetVirtualSize().GetWidth();
        int client_width = m_scroll->GetClientSize().GetWidth();

        if (virtual_width > client_width) {
            m_scroll->SetScrollbar(wxHORIZONTAL, 0, client_width, virtual_width, false);
        }

#ifdef __WXMSW__
        HWND hwnd = (HWND)m_scroll->GetHandle();
        if (hwnd) {
            ShowScrollBar(hwnd, SB_HORZ, FALSE);
            ShowScrollBar(hwnd, SB_VERT, FALSE);
        }
#endif
    }

    Refresh();

    CallAfter([this]() {
        UpdateScrollButtons();
    });
}

void ExtruderPanel::OnLeftScrollClick(wxCommandEvent& evt)
{
    if (!m_scroll) return;

    int scroll_unit = FromDIP(5);
    int current_pos = m_scroll->GetScrollPos(wxHORIZONTAL);
    int new_pos     = std::max(0, current_pos - scroll_unit);

    m_scroll->Scroll(new_pos, -1);
    UpdateScrollButtons();
}

void ExtruderPanel::OnRightScrollClick(wxCommandEvent& evt)
{
    if (!m_scroll) return;

    int scroll_unit = FromDIP(5);
    int current_pos = m_scroll->GetScrollPos(wxHORIZONTAL);
    int max_pos     = m_scroll->GetScrollRange(wxHORIZONTAL);
    int new_pos     = std::min(max_pos, current_pos + scroll_unit);

    m_scroll->Scroll(new_pos, -1);
    UpdateScrollButtons();
}

void ExtruderPanel::OnScrollSizeChanged(wxSizeEvent& evt)
{
    UpdateScrollButtons();
    evt.Skip();
}

void ExtruderPanel::UpdateScrollButtons()
{
    if (!m_scroll || !m_left_scroll || !m_right_scroll) return;

    int virtual_width = m_scroll->GetVirtualSize().GetWidth();
    int client_width  = m_scroll->GetClientSize().GetWidth();

    bool need_scroll = virtual_width > client_width;

    if (!need_scroll) {
        m_left_scroll->Show(false);
        m_right_scroll->Show(false);

        Layout();
        Refresh();
        return;
    }

    int current_pos = m_scroll->GetScrollPos(wxHORIZONTAL);
    int max_pos     = m_scroll->GetScrollRange(wxHORIZONTAL) - m_scroll->GetScrollThumb(wxHORIZONTAL);

    //m_left_scroll->Show(current_pos > 0);
    //m_left_scroll->Enable(current_pos > 0);

    //m_right_scroll->Show(current_pos < max_pos);
    //m_right_scroll->Enable(current_pos < max_pos);

    m_scroll->SetScrollbar(wxHORIZONTAL, m_scroll->GetScrollPos(wxHORIZONTAL), client_width, virtual_width, false);

#ifdef __WXMSW__
    HWND hwnd = (HWND) m_scroll->GetHandle();
    if (hwnd) {
        ShowScrollBar(hwnd, SB_HORZ, FALSE);
        ShowScrollBar(hwnd, SB_VERT, FALSE);
    }
#endif

    m_left_scroll->Show(!m_disabled);
    m_right_scroll->Show(!m_disabled);

    Layout();
    Refresh();
}

void ExtruderPanel::OnLeftScrollDown(wxMouseEvent& evt)
{
    m_scroll_left = true;
    m_scroll_right = false;

    if (m_scroll) {
        int scroll_unit = FromDIP(5);
        int current_pos = m_scroll->GetScrollPos(wxHORIZONTAL);
        int new_pos = std::max(0, current_pos - scroll_unit);
        m_scroll->Scroll(new_pos, -1);
        UpdateScrollButtons();
    }

    m_scroll_timer->Start(50);
    evt.Skip();
}

void ExtruderPanel::OnLeftScrollUp(wxMouseEvent& evt)
{
    m_scroll_left = false;
    if (!m_scroll_right) {
        m_scroll_timer->Stop();
    }
    evt.Skip();
}

void ExtruderPanel::OnRightScrollDown(wxMouseEvent& evt)
{
    m_scroll_right = true;
    m_scroll_left = false;

    if (m_scroll) {
        int scroll_unit = FromDIP(5);
        int current_pos = m_scroll->GetScrollPos(wxHORIZONTAL);
        int max_pos = m_scroll->GetScrollRange(wxHORIZONTAL);
        int new_pos = std::min(max_pos, current_pos + scroll_unit);
        m_scroll->Scroll(new_pos, -1);
        UpdateScrollButtons();
    }

    m_scroll_timer->Start(50);
    evt.Skip();
}

void ExtruderPanel::OnRightScrollUp(wxMouseEvent& evt)
{
    m_scroll_right = false;
    if (!m_scroll_left) {
        m_scroll_timer->Stop();
    }
    evt.Skip();
}

void ExtruderPanel::OnScrollTimer(wxTimerEvent& evt)
{
    if (!m_scroll) return;

    int scroll_unit = FromDIP(5);
    int current_pos = m_scroll->GetScrollPos(wxHORIZONTAL);

    if (m_scroll_left) {
        int new_pos = std::max(0, current_pos - scroll_unit);
        if (new_pos != current_pos) {
            m_scroll->Scroll(new_pos, -1);
            UpdateScrollButtons();
        } else {
            m_scroll_timer->Stop();
            m_scroll_left = false;
        }
    } else if (m_scroll_right) {
        int max_pos = m_scroll->GetScrollRange(wxHORIZONTAL);
        int new_pos = std::min(max_pos, current_pos + scroll_unit);
        if (new_pos != current_pos) {
            m_scroll->Scroll(new_pos, -1);
            UpdateScrollButtons();
        } else {
            m_scroll_timer->Stop();
            m_scroll_right = false;
        }
    }
}

ExtruderDialogPanel::ExtruderDialogPanel(wxWindow *parent, GroupType type, std::vector<NozzleConfig> cfg)
    : ExtruderPanel(parent, type), m_nozzle_configs(cfg)
{
#ifdef __WXMSW__
    Bind(wxEVT_PAINT, &ExtruderDialogPanel::OnPaint, this);
#endif
#ifdef __WXOSX__
    Bind(wxEVT_SIZE, &ExtruderDialogPanel::OnSize, this);
#endif
    if (GetSizer()) {
        DestroyChildren();
        GetSizer()->Clear(true);
    }
    m_ams_sizer    = nullptr;
    m_scroll    = nullptr;
    m_ams_previews.clear();

    m_warning_bitmap = create_scaled_bitmap("warning", this, 16);
    m_warning_serious_bitmap = create_scaled_bitmap("warning_serious", this, 16);

    m_main_sizer = new wxBoxSizer(wxVERTICAL);
    wxString title = type == LeftExtruder ? _L("Left Extruder") : type == RightExtruder ? _L("Right Extruder") : _L("Extruder");
    m_title_label = new Label(this, title);
    m_title_label->SetFont(Label::Body_14.Bold());
    m_title_label->Show(type != SingleExtruder);
    m_main_sizer->Add(m_title_label, 0, wxALIGN_LEFT | wxBOTTOM, FromDIP(8));

    int index = type == RightExtruder ? 1 : 0;
    auto preset_bundle = wxGetApp().preset_bundle;
    auto printer_preset = wxGetApp().preset_bundle->printers.get_edited_preset();
    auto extruder_variants         = printer_preset.config.option<ConfigOptionStrings>("extruder_variant_list");
    auto extruder_max_nozzle_count = printer_preset.config.option<ConfigOptionIntsNullable>("extruder_max_nozzle_count");
    auto extruders_def             = printer_preset.config.def()->get("extruder_type");
    auto extruders                 = printer_preset.config.option<ConfigOptionEnumsGeneric>("extruder_type");
    std::vector<std::string> extruder_variant_list = extruder_variants->values;
    std::vector<int>         max_nozzle_count_list = extruder_max_nozzle_count->values;
    std::vector<int>         extruder_type_list    = extruders->values;
    bool is_single = type == SingleExtruder;

    auto nozzle_volumes_def = preset_bundle->project_config.def()->get("nozzle_volume_type");
    auto nozzle_volumes     = preset_bundle->project_config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type");
    auto diameters          = preset_bundle->printers.diameters_of_selected_printer();
    auto diameter           = printer_preset.config.opt_string("printer_variant");
    bool has_multiple_nozzles = extruder_max_nozzle_count->values[index] > 1;

    m_add_btn = new ScalableButton(this, wxID_ANY, "add_filament", wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, true, 12);
    m_add_btn->Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent &evt) {
        ShowAMSCountPopup();
        evt.Skip();
    });

    m_add_btn->Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &evt) {
        wxTimer *timer = new wxTimer();

        timer->Bind(wxEVT_TIMER, [this, timer](wxTimerEvent &) {
            if (m_ams_popup) {
                wxPoint mouse_pos  = wxGetMousePosition();
                wxRect  popup_rect = m_ams_popup->GetScreenRect();
                wxRect  btn_rect   = m_add_btn->GetScreenRect();

                wxRect safe_zone = btn_rect;
                safe_zone.Union(popup_rect);
                safe_zone.Inflate(FromDIP(10));

                if (!safe_zone.Contains(mouse_pos)) {
                    m_ams_popup->Dismiss();
                    m_ams_popup = nullptr;
                }
            }

            timer->Stop();
            delete timer;
        });

        timer->StartOnce(300);

        evt.Skip();
    });

    auto ams_container_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    ams_container_panel->SetBackgroundColour(wxColour("#F7F7F7"));
    ams_container_panel->SetMinSize(wxSize(-1, FromDIP(40)));
    ams_container_panel->SetMaxSize(wxSize(FromDIP(450), -1));

    m_ams_sizer = new wxBoxSizer(wxHORIZONTAL);
    ams_container_panel->SetSizer(m_ams_sizer);

    for (size_t i = 0; i < 4; ++i) {
        AMSinfo info;
        info.ams_type = DevAmsType::AMS;
        auto preview = new AMSPreview(ams_container_panel, wxID_ANY, info);
        preview->Close();
        preview->Bind(wxEVT_ENTER_WINDOW, [this, preview](wxMouseEvent &evt) { ShowAMSDeletePopup(preview); });
        m_ams_previews.push_back(preview);
        m_ams_sizer->Add(preview, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, FromDIP(2));
    }

    for (size_t i = 0; i < 8; ++i) {
        AMSinfo info;
        info.ams_type = DevAmsType::N3S;
        auto preview = new AMSPreview(ams_container_panel, wxID_ANY, info);
        preview->Close();
        preview->Bind(wxEVT_ENTER_WINDOW, [this, preview](wxMouseEvent &evt) { ShowAMSDeletePopup(preview); });
        m_ams_previews.push_back(preview);
        m_ams_sizer->Add(preview, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, FromDIP(2));
    }

    for (int i = 0; i < m_nozzle_configs.size(); ++i) {
        NozzleInternal nozzle_internal;
        wxBoxSizer    *nozzle_internal_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto           warning_icon          = new wxStaticBitmap(this, wxID_ANY, m_warning_bitmap, wxDefaultPosition, wxSize(FromDIP(16), FromDIP(16)), 0);
        nozzle_internal.warning_icon         = warning_icon;
        auto nozzle_label                    = new Label(this, extruder_max_nozzle_count->values[index] > 1 ? wxString::Format(_L("Nozzle %d "), i + 1) : _L("Nozzle"));
        nozzle_internal.nozzle_label         = nozzle_label;
        warning_icon->Hide();

        nozzle_internal.diameter_combo       = new ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(110), FromDIP(24)), 0, nullptr, wxCB_READONLY);
        int select = -1;
        for (int j = 0; j < diameters.size(); ++j) {
            if (diameters[j] == m_nozzle_configs[i].diameter.ToStdString()) {
                select = nozzle_internal.diameter_combo->GetCount();
            }
            nozzle_internal.diameter_combo->Append(diameters[j] + " mm", {});
        }
        if (has_multiple_nozzles && !is_single) {
            nozzle_internal.diameter_combo->Append(_L("Empty"), {});
        }
        if (m_nozzle_configs[i].is_empty() && m_nozzle_configs[i].status == NozzleStatus::nsNormal && has_multiple_nozzles && !is_single) {
            nozzle_internal.diameter_combo->SetSelection(nozzle_internal.diameter_combo->GetCount() - 1);
        } else {
            if (select == -1 && is_single) {
                for (int j = 0; j < diameters.size(); ++j) {
                    if (diameters[j] == "0.4") {
                        select = j;
                        break;
                    }
                }
                if (select == -1 && !diameters.empty()) {
                    select = 0;
                }
            }
            nozzle_internal.diameter_combo->SetSelection(select);
        }

        nozzle_internal.flow_combo = new ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(110), FromDIP(24)), 0, nullptr, wxCB_READONLY);
        auto flow = m_nozzle_configs[i].volume;
        auto update_flow_combo     = [this, nozzle_internal, nozzle_volumes_def, nozzle_volumes, index, flow](const wxString &diameter) {
            nozzle_internal.flow_combo->Clear();

            auto printer_preset = wxGetApp().preset_bundle->get_similar_printer_preset({}, diameter.ToStdString());
            auto extruder_variants = printer_preset->config.option<ConfigOptionStrings>("extruder_variant_list");
            auto extruders_def          = printer_preset->config.def()->get("extruder_type");
            auto extruders         = printer_preset->config.option<ConfigOptionEnumsGeneric>("extruder_type");
            auto extruder_max_nozzle_count = printer_preset->config.option<ConfigOptionIntsNullable>("extruder_max_nozzle_count");

            auto type   = extruders_def->enum_labels[extruders->values[index]];
            int  select = -1;
            for (int j = 0; j < nozzle_volumes_def->enum_labels.size(); ++j) {
                if (boost::algorithm::contains(extruder_variants->values[index], type + " " + nozzle_volumes_def->enum_labels[j]) ||
                    extruder_max_nozzle_count->values[index] > 1 && nozzle_volumes_def->enum_keys_map->at(nozzle_volumes_def->enum_values[j]) == nvtHybrid) {
                    if (diameter.ToStdString() == "0.2" && nozzle_volumes_def->enum_keys_map->at(nozzle_volumes_def->enum_values[j]) == NozzleVolumeType::nvtHighFlow)
                        continue;
                    if (nozzle_volumes_def->enum_keys_map->at(nozzle_volumes_def->enum_values[j]) == NozzleVolumeType::nvtHybrid)
                        continue;
                    if (flow == j)
                        select = nozzle_internal.flow_combo->GetCount();
                    nozzle_internal.flow_combo->Append(_L(nozzle_volumes_def->enum_labels[j]), {}, (void *)(intptr_t)(j + 1));
                }
            }
            if (select == -1)
                select = nozzle_internal.flow_combo->GetCount() - 1;
            nozzle_internal.flow_combo->SetSelection(select);
        };

        update_flow_combo(m_nozzle_configs[i].diameter);
        nozzle_internal.diameter_combo->Bind(wxEVT_COMBOBOX, [this, nozzle_internal, update_flow_combo, diameters, has_multiple_nozzles, is_single](wxCommandEvent &event) {
            int select = nozzle_internal.diameter_combo->GetSelection();
            wxString diameter;
            if (!is_single && has_multiple_nozzles && select == nozzle_internal.diameter_combo->GetCount() - 1) {
                // Empty selected
                diameter = _L("Empty");
            } else {
                diameter = wxString::FromUTF8(diameters[select]);
            }
            update_flow_combo(diameter);
            event.Skip();
        });

        if (is_single) {
            auto title = new Label(this, wxString::Format(_L("%d Nozzle Type"), i + 1));
            nozzle_internal.diameter_flow_label = new Label(this, _L("Diameter && Volume"));
            nozzle_internal.type_combo = new ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(110), FromDIP(24)), 0, nullptr, wxCB_READONLY);
            nozzle_internal.type_combo->Append(_L("Standard Nozzle"), {});
            nozzle_internal.type_combo->Append(_L("Empty"), {});
            if (m_nozzle_configs[i].is_empty()) {
                nozzle_internal.type_combo->SetSelection(nozzle_internal.type_combo->GetCount() - 1);
            } else {
                nozzle_internal.type_combo->SetSelection(0);
            }

            nozzle_internal.type_combo->Bind(wxEVT_COMBOBOX, [this, nozzle_internal](wxCommandEvent &event) {
                int sel = nozzle_internal.type_combo->GetSelection();
                bool is_empty = sel == nozzle_internal.type_combo->GetCount() - 1; // Empty option
                nozzle_internal.diameter_flow_label->Show(!is_empty);
                nozzle_internal.flow_combo->Show(!is_empty);
                nozzle_internal.diameter_combo->Show(!is_empty);

                event.Skip();
            });

            nozzle_internal.material_color_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
            nozzle_internal.material_color_panel->SetBackgroundColour(wxColour("#F7F7F7"));
            nozzle_internal.material_label = new Label(nozzle_internal.material_color_panel, "PLA Basic");
            nozzle_internal.material_color_box = new StaticBox(nozzle_internal.material_color_panel, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(14), FromDIP(14)), wxBORDER_NONE);
            nozzle_internal.material_color_box->SetCornerRadius(FromDIP(8));
            nozzle_internal.material_color_box->SetBackgroundColour(wxColour("#2B3CFF"));
            auto sizer = new wxBoxSizer(wxHORIZONTAL);
            auto tt    = new Label(nozzle_internal.material_color_panel, _L("Filament"));
            sizer->Add(tt, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
            sizer->Add(nozzle_internal.material_color_box, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
            sizer->Add(nozzle_internal.material_label, 0, wxALIGN_CENTER_VERTICAL);
            nozzle_internal.material_color_panel->SetSizer(sizer);

            nozzle_internal_sizer->Add(warning_icon, 0, wxRESERVE_SPACE_EVEN_IF_HIDDEN | wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
            nozzle_internal_sizer->Add(title, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
            nozzle_internal_sizer->Add(nozzle_internal.type_combo, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
            nozzle_internal_sizer->Add(nozzle_internal.diameter_flow_label, 0, wxRESERVE_SPACE_EVEN_IF_HIDDEN | wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
            nozzle_internal_sizer->Add(nozzle_internal.diameter_combo, 0, wxRESERVE_SPACE_EVEN_IF_HIDDEN | wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
            nozzle_internal_sizer->Add(nozzle_internal.flow_combo, 0, wxRESERVE_SPACE_EVEN_IF_HIDDEN | wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
            nozzle_internal_sizer->Add(nozzle_internal.material_color_panel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
            nozzle_internal.nozzle_label->Hide();

            if (i == 0) {
                wxBoxSizer *filament_sizer = new wxBoxSizer(wxHORIZONTAL);
                m_filament_label = new Label(this, _L("AMS"));
                m_filament_label->SetFont(Label::Body_14);
                filament_sizer->Add(m_filament_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
                filament_sizer->Add(ams_container_panel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
                filament_sizer->Add(m_add_btn, 0, wxALIGN_CENTER_VERTICAL);

                nozzle_internal_sizer->Add(filament_sizer, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
            }
        } else {
            nozzle_internal_sizer->Add(warning_icon, 0, wxRESERVE_SPACE_EVEN_IF_HIDDEN | wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
            nozzle_internal_sizer->Add(nozzle_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
            nozzle_internal_sizer->Add(nozzle_internal.diameter_combo, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
            nozzle_internal_sizer->Add(nozzle_internal.flow_combo, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));

            nozzle_internal.error_label = new Label(this, _L("Error"));
            nozzle_internal.error_label->SetForegroundColour(wxColour("#D01B1B"));
            nozzle_internal.unknown_label = new Label(this, _L("Unknown"));
            nozzle_internal.unknown_label->SetForegroundColour(wxColour("#D01B1B"));
            nozzle_internal_sizer->Add(nozzle_internal.error_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
            nozzle_internal_sizer->Add(nozzle_internal.unknown_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
            nozzle_internal.error_label->Hide();
            nozzle_internal.unknown_label->Hide();
        }

        m_main_sizer->Add(nozzle_internal_sizer, 0, wxALIGN_LEFT | wxBOTTOM, FromDIP(8));
        m_nozzles.push_back(nozzle_internal);
    }

    if (!is_single) {
        wxBoxSizer *filament_sizer = new wxBoxSizer(wxHORIZONTAL);
        m_filament_label = new Label(this, _L("AMS"));
        m_filament_label->SetFont(Label::Body_14);
        filament_sizer->Add(m_filament_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
        filament_sizer->Add(ams_container_panel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
        filament_sizer->Add(m_add_btn, 0, wxALIGN_CENTER_VERTICAL);

        m_main_sizer->AddSpacer(FromDIP(8));
        m_main_sizer->Add(filament_sizer, 0, wxALIGN_LEFT | wxLEFT, FromDIP(20));
    }

    SetSizer(m_main_sizer);
    SetMinSize(wxSize(-1, -1));
    Layout();
    Fit();

    ShowBadge(true);
    wxGetApp().UpdateDarkUIWin(this);
    CallAfter([this]() {
        update_ams();
    });
}

void ExtruderDialogPanel::UpdateWarningStates(const wxString &selected_diameter)
{
    bool title_not_change = false;
    for (int i = 0; i < m_nozzles.size(); ++i) {
        auto &nozzle = m_nozzles[i];
        if (!nozzle.diameter_combo || !nozzle.warning_icon) continue;

        if (nozzle.diameter_combo->GetSelection() != -1 && nozzle.flow_combo->GetSelection() != -1 && m_nozzle_configs[i].status != NozzleStatus::nsNormal) {
            m_nozzle_configs[i].status = NozzleStatus::nsNormal;
        }

        if (nozzle.type_combo) {
            if (nozzle.type_combo->GetValue() == _L("Empty")) {
                nozzle.warning_icon->SetBitmap(m_warning_serious_bitmap);
                nozzle.warning_icon->Show();
                nozzle.is_serious_warning = true;

                nozzle.diameter_flow_label->Show(false);
                nozzle.diameter_combo->Show(false);
                nozzle.flow_combo->Show(false);
                continue;
            } else {
                nozzle.diameter_flow_label->Show(true);
                nozzle.diameter_combo->Show(true);
                nozzle.flow_combo->Show(true);
            }
        }

        if (m_nozzle_configs[i].status == NozzleStatus::nsError || m_nozzle_configs[i].status == NozzleStatus::nsUnknown) {
            nozzle.warning_icon->SetBitmap(m_warning_serious_bitmap);
            nozzle.warning_icon->Show();
            nozzle.is_serious_warning = true;
            if (nozzle.error_label) nozzle.error_label->Show(m_nozzle_configs[i].status == NozzleStatus::nsError);
            if (nozzle.unknown_label) nozzle.unknown_label->Show(m_nozzle_configs[i].status == NozzleStatus::nsUnknown);
            nozzle.nozzle_label->SetForegroundColour(wxColour("#D01B1B"));

            wxString tip = m_nozzle_configs[i].status == NozzleStatus::nsError ? _L("Hotend status is abnormal and not available. Click to update firmware.")
                                                                               : _L("Hotend information is unknown. Please refresh nozzle information before use.");
            nozzle.warning_icon->SetToolTip(tip);
            nozzle.nozzle_label->SetToolTip(tip);
            if (m_nozzle_configs[i].status == NozzleStatus::nsError) {
                nozzle.nozzle_label->SetCursor(wxCursor(wxCURSOR_HAND));
                nozzle.nozzle_label->Bind(wxEVT_LEFT_DOWN, &ExtruderDialogPanel::OnNozzleErrorClick, this);
            }

            continue;
        } else {
            if (nozzle.error_label) nozzle.error_label->Show(false);
            if (nozzle.unknown_label) nozzle.unknown_label->Show(false);
            nozzle.warning_icon->UnsetToolTip();
            nozzle.nozzle_label->UnsetToolTip();
            nozzle.nozzle_label->SetCursor(wxCursor(wxCURSOR_ARROW));
            nozzle.nozzle_label->Unbind(wxEVT_LEFT_DOWN, &ExtruderDialogPanel::OnNozzleErrorClick, this);
        }

        wxString diameter = nozzle.diameter_combo->GetValue();
        if (diameter == _L("Empty")) {
            nozzle.warning_icon->SetBitmap(m_warning_serious_bitmap);
            nozzle.warning_icon->Show();
            nozzle.is_serious_warning = true;
            nozzle.nozzle_label->SetForegroundColour(wxColour("#D01B1B"));

            nozzle.flow_combo->Show(false);
            continue;
        } else {
            nozzle.flow_combo->Show(true);
        }

        nozzle.warning_icon->SetBitmap(m_warning_bitmap);
        nozzle.is_serious_warning = false;
        nozzle.nozzle_label->SetForegroundColour(wxColour("#262E30"));
        diameter.Replace(" mm", "");

        if (diameter == selected_diameter)
            title_not_change = true;

        nozzle.warning_icon->Show(diameter != selected_diameter);
    }

    m_title_label->SetForegroundColour(title_not_change ? wxColour("#262E30") : wxColour("#FF6F00"));

    Layout();
    Refresh();
    wxGetApp().UpdateDarkUIWin(this);
}

void ExtruderDialogPanel::OnNozzleErrorClick(wxMouseEvent& evt)
{
    MessageDialog dlg(this,
        _L("Hotend status is abnormal. Please update the firmware."),
        _L("Update Firmware"),
        wxCANCEL | wxOK);

    if (dlg.ShowModal() == wxID_OK) {
        wxWindow* dialog = this;
        while (dialog && !dynamic_cast<ExtruderSettingsDialog*>(dialog)) {
            dialog = dialog->GetParent();
        }
        if (dialog) {
            auto d = dynamic_cast<ExtruderSettingsDialog *>(dialog);
            d->EndModal(wxID_CANCEL);
        }
        wxGetApp().mainframe->jump_to_monitor();
        wxGetApp().mainframe->m_monitor->jump_to_Upgrade();
    }
}

void ExtruderDialogPanel::sync_ams_from_panel(const ExtruderPanel *source_panel)
{
    if (!source_panel || !m_ams_sizer) return;

    m_ams_n4 = source_panel->m_ams_n4;
    m_ams_n1 = source_panel->m_ams_n1;

    m_ams_4 = source_panel->m_ams_4;
    m_ams_1 = source_panel->m_ams_1;
    m_ext   = source_panel->m_ext;

    update_ams();
}

void ExtruderDialogPanel::update_ams()
{
    ExtruderPanel::update_ams();

    bool has_ams = (m_ams_n4 + m_ams_n1) > 0;
    if (m_type == SingleExtruder) {
        m_filament_label->Show(!has_ams);
        auto nozzle = m_nozzles.front();
        if (nozzle.material_color_panel && nozzle.material_label) {
            if (nozzle.material_color_panel->IsShown()) {
                nozzle.material_label->Show(!has_ams);
            }
        }
    }

    wxWindow *current = GetParent();
    while (current) {
        ExtruderSettingsDialog *dialog = dynamic_cast<ExtruderSettingsDialog *>(current);
        if (dialog) {
            dialog->OnAMSCountChanged();
            break;
        }
        current = current->GetParent();
    }
}

Slic3r::NozzleVolumeType get_volume_type_from_configs(wxString diameter, const std::vector<NozzleConfig>& configs)
{
    if (configs.empty())
        return Slic3r::NozzleVolumeType::nvtStandard;

    std::set<Slic3r::NozzleVolumeType> volume_types;
    for (const auto &cfg : configs) {
        if (!cfg.diameter.IsEmpty() && cfg.diameter == diameter)
            volume_types.insert(cfg.volume);
    }

    if (volume_types.size() == 1) {
        return *volume_types.begin();
    } else if (volume_types.size() > 1) {
        return Slic3r::NozzleVolumeType::nvtHybrid;
    }

    return Slic3r::NozzleVolumeType::nvtStandard;
}

std::vector<NozzleConfig> ExtruderDialogPanel::ExtractConfig()
{
    std::vector<NozzleConfig> configs;
    auto internals = GetNozzleConfigs();

    for (const auto &internal : internals) {
        NozzleConfig cfg;

        if (m_type == ExtruderPanel::SingleExtruder && internal.type_combo) {
            wxString type_sel = internal.type_combo->GetValue();
            if (type_sel == _L("Empty")) {
                cfg.diameter = wxEmptyString;
                cfg.status = NozzleStatus::nsNormal;
                configs.push_back(cfg);
                continue;
            }
        }

        if (internal.diameter_combo) {
            wxString diameter_text = internal.diameter_combo->GetValue();
            if (diameter_text == _L("Empty")) {
                cfg.diameter = wxEmptyString;
            } else {
                diameter_text.Replace(" mm", "");
                cfg.diameter = diameter_text;
            }
        }

        if (internal.flow_combo) {
            int sel = internal.flow_combo->GetSelection();
            if (sel != wxNOT_FOUND) {
                void *data = internal.flow_combo->GetClientData(sel);
                if (data) {
                    intptr_t index = reinterpret_cast<intptr_t>(data) - 1;
                    cfg.volume = static_cast<NozzleVolumeType>(index);
                }
            }
        }

        cfg.status = internal.error_label && internal.error_label->IsShown() ? NozzleStatus::nsError :
                     internal.unknown_label && internal.unknown_label->IsShown() ? NozzleStatus::nsUnknown :
                                                                                   NozzleStatus::nsNormal;
        configs.push_back(cfg);
    }

    return configs;
}

ExtruderNozzleStat ExtruderDialogPanel::BuildExtruderNozzleStat(wxString diameter, const std::vector<NozzleConfig> &configs)
{
    ExtruderNozzleStat stat;
    std::map<NozzleVolumeType, int> volume_counts;
    for (const auto& cfg : configs) {
        if (!cfg.diameter.IsEmpty() && cfg.diameter == diameter) {
            volume_counts[cfg.volume]++;
        }
    }
    auto type = get_volume_type_from_configs(diameter, configs);
    int  extruder_id = m_type == LeftExtruder ? 0 : m_type == RightExtruder ? 1 : 0;
    if (type == nvtHybrid) {
        stat.set_extruder_nozzle_count(extruder_id, nvtStandard, volume_counts[nvtStandard], true);
        stat.set_extruder_nozzle_count(extruder_id, nvtHighFlow, volume_counts[nvtHighFlow], true);
    } else {
        stat.set_extruder_nozzle_count(extruder_id, type, volume_counts[type], false);
    }

    return stat;
}

bool ExtruderDialogPanel::is_extruder_synced(wxString diameter)
{
    int target_extruder_id = m_type == LeftExtruder ? 0 : m_type == RightExtruder ? 1 : 0;
    using namespace MultiNozzleUtils;
    Slic3r::DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return false;

    MachineObject* obj = dev->get_selected_machine();
    if (!obj)
        return false;

    PresetBundle* preset_bundle = wxGetApp().preset_bundle;
    bool is_synced = true;

    auto nozzle_volume_values = preset_bundle->project_config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type")->values;
    auto nozzle_diameter_values = preset_bundle->printers.get_edited_preset().config.option<ConfigOptionFloatsNullable>("nozzle_diameter")->values;
    auto extruder_stat          = BuildExtruderNozzleStat(diameter, ExtractConfig());
    auto extruder_nozzle_stats  = extruder_stat.get_raw_stat();

    std::vector<std::vector<NozzleGroupInfo>> preset_nozzle_infos(nozzle_diameter_values.size());

    for (size_t extruder_id = 0; extruder_id < nozzle_diameter_values.size(); ++extruder_id) {
        NozzleVolumeType preset_volume_type = NozzleVolumeType(nozzle_volume_values[extruder_id]);
        std::string      preset_diameter    = diameter.ToStdString();

        if (preset_volume_type == nvtHybrid) {
            if (extruder_id < extruder_nozzle_stats.size()) {
                for (auto& elem : extruder_nozzle_stats[extruder_id]) {
                    NozzleVolumeType type = elem.first;
                    int              count = elem.second;
                    NozzleGroupInfo  preset_info(preset_diameter, type, extruder_id, count);
                    preset_nozzle_infos[extruder_id].emplace_back(preset_info);
                }
            }
        }
        else {
            NozzleGroupInfo preset_info(preset_diameter, preset_volume_type, extruder_id, extruder_stat.get_extruder_nozzle_count(extruder_id, preset_volume_type));
            preset_nozzle_infos[extruder_id].emplace_back(preset_info);
        }
    }

    auto printer_groups = obj->GetNozzleSystem()->GetNozzleGroups();
    std::vector<std::vector<NozzleGroupInfo>> target_nozzle_groups;
    if (target_extruder_id == -1)
        target_nozzle_groups = preset_nozzle_infos;
    else
        target_nozzle_groups = { preset_nozzle_infos[target_extruder_id] };

    for (auto preset_groups : target_nozzle_groups) {
        for (auto& preset_group : preset_groups) {
            if (preset_group.nozzle_count == 0) {
                if (std::find_if(printer_groups.begin(), printer_groups.end(), [&preset_group](const NozzleGroupInfo& elem) { return preset_group.is_same_type(elem); }) !=
                    printer_groups.end()) {
                    is_synced = false;
                    break;
                }
            }
            else if (std::find(printer_groups.begin(), printer_groups.end(), preset_group) == printer_groups.end()) {
                is_synced = false;
                break;
            }
        }
    }
    return is_synced;
}

void ExtruderDialogPanel::Rescale()
{
    ExtruderPanel::Rescale();

    if (m_add_btn) {
        m_add_btn->msw_rescale();
    }

    for (auto& nozzle : m_nozzles) {
        // Update warning icon bitmap
        if (nozzle.warning_icon) {
            if (nozzle.warning_icon->IsShown()) {
                // Preserve the current bitmap type (warning or warning_serious)
                wxBitmap current_bmp = nozzle.warning_icon->GetBitmap();
                nozzle.warning_icon->SetBitmap(nozzle.is_serious_warning ? m_warning_serious_bitmap : m_warning_bitmap);
            }
            nozzle.warning_icon->SetMinSize(wxSize(FromDIP(16), FromDIP(16)));
        }

        // Update nozzle label font
        if (nozzle.nozzle_label) {
            nozzle.nozzle_label->SetFont(Label::Body_12);
        }

        // Update diameter/flow label
        if (nozzle.diameter_flow_label) {
            nozzle.diameter_flow_label->SetFont(Label::Body_12);
        }

        // Update combo boxes
        if (nozzle.type_combo) {
            nozzle.type_combo->SetMinSize(wxSize(FromDIP(110), FromDIP(24)));
        }
        if (nozzle.diameter_combo) {
            nozzle.diameter_combo->SetMinSize(wxSize(FromDIP(110), FromDIP(24)));
        }
        if (nozzle.flow_combo) {
            nozzle.flow_combo->SetMinSize(wxSize(FromDIP(110), FromDIP(24)));
        }

        // Update material panel
        if (nozzle.material_label) {
            nozzle.material_label->SetFont(Label::Body_12);
        }
        if (nozzle.material_color_box) {
            nozzle.material_color_box->SetMinSize(wxSize(FromDIP(14), FromDIP(14)));
            nozzle.material_color_box->SetCornerRadius(FromDIP(8));
        }

        // Update error labels
        if (nozzle.error_label) {
            nozzle.error_label->SetFont(Label::Body_12);
        }
        if (nozzle.unknown_label) {
            nozzle.unknown_label->SetFont(Label::Body_12);
        }
    }

    Layout();
    Fit();
    Refresh();
}

void ExtruderDialogPanel::ShowBadge(bool show)
{
#ifdef __WXMSW__
    if (show && m_badge.name() != "badge") {
        m_badge = ScalableBitmap(this, "badge", 18);
        Refresh();
    } else if (!show && !m_badge.name().empty()) {
        m_badge = ScalableBitmap{};
        Refresh();
    }
#endif
#ifdef __WXOSX__
    if (show && m_badge == nullptr) {
        m_badge = new ScalableButton(this, wxID_ANY, "badge", wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, false, 18);
        m_badge->SetSize(m_badge->GetBestSize());
        m_badge->SetBackgroundColour("#F7F7F7");

        LayoutBadge();
    }
    if (m_badge) { m_badge->Show(show); }
#endif
}

#ifdef __WXOSX__
void ExtruderDialogPanel::LayoutBadge()
{
    if (!m_badge) return;

    wxSize panel_size = GetSize();
    wxSize badge_size = m_badge->GetBestSize();

    int x = panel_size.x - badge_size.x - FromDIP(8);
    int y = FromDIP(8);

    m_badge->SetPosition(wxPoint(x, y));
}

void ExtruderDialogPanel::OnSize(wxSizeEvent &evt)
{
    LayoutBadge();
    evt.Skip();
}
#endif

#ifdef __WXMSW__
void ExtruderDialogPanel::OnPaint(wxPaintEvent &evt)
{
    wxPanel::OnPaint(evt);

    if (m_badge.bmp().IsOk()) {
        auto      s = m_badge.bmp().GetScaledSize();
        wxPaintDC dc(this);
        dc.DrawBitmap(m_badge.bmp(), GetSize().x - s.x - 4, 0);
    }
}
#endif

ExtruderSettingsDialog::ExtruderSettingsDialog(wxWindow                        *parent,
                                               bool                             is_dual_extruder,
                                               wxString                        &diameter,
                                               const std::vector<NozzleConfig> &left_configs,
                                               const std::vector<NozzleConfig> &right_configs,
                                               ExtruderPanel                   *left_source,
                                               ExtruderPanel                   *right_source,
                                               CustomSeparator::State           state)
    : DPIDialog(parent, wxID_ANY, _L("Extruder Settings"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    , m_is_dual_extruder(is_dual_extruder)
    , m_diameter(diameter)
    , m_left_configs(left_configs)
    , m_right_configs(right_configs)
{
    SetBackgroundColour(*wxWHITE);
    SaveAMSInitialState(left_source, right_source);
    CreateLayout();
    if (m_separator) {
        m_separator->SetState(state);
    }

    if (left_source && m_left_extruder_panel) {
        m_left_extruder_panel->sync_ams_from_panel(left_source);
    }

    if (right_source && m_right_extruder_panel) {
        m_right_extruder_panel->sync_ams_from_panel(right_source);
    }

    CentreOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

void ExtruderSettingsDialog::CreateLayout()
{
    auto main_sizer = new wxBoxSizer(wxVERTICAL);
    std::string icon_path  = (boost::format("%1%/images/BambuStudioTitle.ico") % Slic3r::resources_dir()).str();
    SetIcon(wxIcon(Slic3r::encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    auto diameter_label = new Label(this, _L("Select Nozzle Diameter"));
    diameter_label->SetFont(Label::Body_14.Bold());
    main_sizer->Add(diameter_label, 0, wxALIGN_LEFT | wxLEFT | wxTOP, FromDIP(20));
    // top DiameterButtonPanel
    auto diameters = wxGetApp().preset_bundle->printers.diameters_of_selected_printer();
    std::vector<wxString> diameter_choices;
    for (const auto &d : diameters) {
        diameter_choices.push_back(wxString::FromUTF8(d));
    }
    m_diameter_panel = new DiameterButtonPanel(this, diameter_choices, false);
    m_diameter_panel->SetSelectedDiameter(m_diameter);
    main_sizer->Add(m_diameter_panel, 0, wxEXPAND | wxLEFT, FromDIP(20));

    // extruder panels
    auto nozzle_label    = new Label(this, _L("Configure Nozzle Settings"));
    nozzle_label->SetFont(Label::Body_14.Bold());
    main_sizer->Add(nozzle_label, 0, wxALIGN_LEFT | wxLEFT | wxTOP, FromDIP(20));
    m_extruder_container = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_extruder_container->SetBackgroundColour(wxColour("#F7F7F7"));

    auto container_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto left_type       = m_is_dual_extruder ? ExtruderPanel::LeftExtruder : ExtruderPanel::SingleExtruder;
    m_left_extruder_panel = new ExtruderDialogPanel(m_extruder_container, left_type, m_left_configs);
    container_sizer->Add(m_left_extruder_panel, 1, wxEXPAND | wxLEFT | wxTOP | wxBOTTOM, FromDIP(20));

    if (m_is_dual_extruder) {
        m_separator = new CustomSeparator(m_extruder_container, false);
        container_sizer->Add(m_separator, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(20));

        m_right_extruder_panel = new ExtruderDialogPanel(m_extruder_container, ExtruderPanel::RightExtruder, m_right_configs);
        container_sizer->Add(m_right_extruder_panel, 1, wxEXPAND | wxRIGHT | wxTOP | wxBOTTOM, FromDIP(20));
    }
    m_extruder_container->SetSizer(container_sizer);
    main_sizer->Add(m_extruder_container, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));

    // warning label
    m_warning_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_warning_panel->SetBackgroundColour(*wxWHITE);
    auto warning_sizer  = new wxBoxSizer(wxVERTICAL);
    auto warning_label1 = new Label(m_warning_panel, _L("Note: BambuStudio does not support mixed-diameter slicing."));
    warning_label1->SetFont(Label::Body_13);
    warning_label1->SetForegroundColour(wxColour("#FF6F00"));
    warning_sizer->Add(warning_label1, 0, wxALIGN_LEFT | wxTOP | wxBOTTOM, FromDIP(4));
    auto warning_icon   = new wxStaticBitmap(m_warning_panel, wxID_ANY, create_scaled_bitmap("warning", this, 16), wxDefaultPosition, wxSize(FromDIP(16), FromDIP(16)), 0);
    auto warning_label2 = new Label(m_warning_panel, _L("Nozzles have different diameter from print setting and cannot be used during slicing/printing."));
    warning_label2->SetFont(Label::Body_13);
    warning_label2->SetForegroundColour(wxColour("#FF6F00"));
    auto temp_sizer     = new wxBoxSizer(wxHORIZONTAL);
    temp_sizer->Add(warning_icon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
    temp_sizer->Add(warning_label2, 1, wxALIGN_CENTER_VERTICAL | wxEXPAND);
    warning_sizer->Add(temp_sizer, 0, wxEXPAND | wxALIGN_LEFT | wxBOTTOM, FromDIP(4));
    m_warning_panel->SetSizer(warning_sizer);
    main_sizer->Add(m_warning_panel, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));
    m_warning_panel->Hide();

    m_warning_serious_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_warning_serious_panel->SetBackgroundColour(*wxWHITE);
    auto warning_serious_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto warning_serious_icon  = new wxStaticBitmap(m_warning_serious_panel, wxID_ANY, create_scaled_bitmap("warning_serious", this, 16), wxDefaultPosition,
                                                    wxSize(FromDIP(16), FromDIP(16)), 0);
    auto warning_serious_label = new Label(m_warning_serious_panel, _L("Empty nozzles cannot be used during slicing/printing."));
    warning_serious_label->SetFont(Label::Body_13);
    warning_serious_label->SetForegroundColour(wxColour("#D01B1B"));
    warning_serious_sizer->Add(warning_serious_icon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
    warning_serious_sizer->Add(warning_serious_label, 0, wxALIGN_CENTER_VERTICAL);
    m_warning_serious_panel->SetSizer(warning_serious_sizer);
    main_sizer->Add(m_warning_serious_panel, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));
    m_warning_serious_panel->Hide();

    auto btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    btn_sizer->AddStretchSpacer();

    // OK and Cancel buttons
    StateColor ok_btn_bg(std::pair<wxColour, int>(wxColour("#1B8844"), StateColor::Pressed), std::pair<wxColour, int>(wxColour("#3DCB73"), StateColor::Hovered),
                         std::pair<wxColour, int>(wxColour("#00AE42"), StateColor::Normal));
    StateColor ok_btn_text(std::pair<wxColour, int>(wxColour("#FFFFFE"), StateColor::Normal));
    StateColor cancel_btn_bg(std::pair<wxColour, int>(wxColour("#CECECE"), StateColor::Pressed), std::pair<wxColour, int>(wxColour("#EEEEEE"), StateColor::Hovered),
                             std::pair<wxColour, int>(wxColour("#FFFFFF"), StateColor::Normal));
    StateColor cancel_btn_bd(std::pair<wxColour, int>(wxColour("#262E30"), StateColor::Normal));
    StateColor cancel_btn_text(std::pair<wxColour, int>(wxColour("#262E30"), StateColor::Normal));

    auto ok_btn = new Button(this, _L("Confirm"));
    ok_btn->SetMinSize(wxSize(FromDIP(62), FromDIP(24)));
    ok_btn->SetCornerRadius(FromDIP(12));
    ok_btn->SetBackgroundColor(ok_btn_bg);
    ok_btn->SetFont(Label::Body_12);
    ok_btn->SetBorderColor(wxColour("#00AE42"));
    ok_btn->SetTextColor(ok_btn_text);
    ok_btn->SetId(wxID_OK);
    m_confirm_btn = ok_btn;

    auto cancel_btn = new Button(this, _L("Cancel"));
    cancel_btn->SetMinSize(wxSize(FromDIP(62), FromDIP(24)));
    cancel_btn->SetCornerRadius(FromDIP(12));
    cancel_btn->SetBackgroundColor(cancel_btn_bg);
    cancel_btn->SetFont(Label::Body_12);
    cancel_btn->SetBorderColor(cancel_btn_bd);
    cancel_btn->SetTextColor(cancel_btn_text);
    cancel_btn->SetId(wxID_CANCEL);

    cancel_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &event) {
        RestoreAMSInitialState();
        EndModal(wxID_CANCEL);
        event.Skip();
    });
    m_cancel_btn = cancel_btn;

    auto refresh_btn = new Button(this, _L("Refresh"));
    refresh_btn->SetMinSize(wxSize(FromDIP(62), FromDIP(24)));
    refresh_btn->SetCornerRadius(FromDIP(12));
    refresh_btn->SetBackgroundColor(cancel_btn_bg);
    refresh_btn->SetFont(Label::Body_12);
    refresh_btn->SetBorderColor(cancel_btn_bd);
    refresh_btn->SetTextColor(cancel_btn_text);
    
    refresh_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &event) {
        RestoreAMSInitialState();
        EndModal(wxID_CANCEL);

        event.Skip();
    });
    m_refresh_btn = refresh_btn;
    m_refresh_btn->Hide();

    btn_sizer->Add(m_confirm_btn, 0, wxRIGHT, FromDIP(12));
    btn_sizer->Add(m_refresh_btn, 0, wxRIGHT, FromDIP(12));
    btn_sizer->Add(m_cancel_btn, 0);

    main_sizer->Add(btn_sizer, 0, wxEXPAND | wxALL, FromDIP(20));

    SetSizer(main_sizer);
    Layout();
    Fit();

    SetupCallbacks();
    UpdateDialogState();

    if (m_is_dual_extruder && m_separator) {
        CallAfter([this]() {
            AlignSeparatorIcon();
        });
    }
}

wxString ExtruderSettingsDialog::GetSelectedDiameter() const
{
    return m_diameter_panel ? m_diameter_panel->GetSelectedDiameter() : wxString();
}

std::vector<NozzleConfig> ExtruderSettingsDialog::ExtractConfigsFromPanel(ExtruderDialogPanel *panel) const
{
    if (!panel) return {};

    std::vector<NozzleConfig> configs;
    auto internals = panel->GetNozzleConfigs();

    for (const auto &internal : internals) {
        NozzleConfig cfg;

        if (panel->GetType() == ExtruderPanel::SingleExtruder && internal.type_combo) {
            wxString type_sel = internal.type_combo->GetValue();
            if (type_sel == _L("Empty")) {
                cfg.diameter = wxEmptyString;
                cfg.status = NozzleStatus::nsNormal;
                configs.push_back(cfg);
                continue;
            }
        }

        if (internal.diameter_combo) {
            wxString diameter_text = internal.diameter_combo->GetValue();
            if (diameter_text == _L("Empty")) {
                cfg.diameter = wxEmptyString;
            } else {
                diameter_text.Replace(" mm", "");
                cfg.diameter = diameter_text;
            }
        }

        if (internal.flow_combo) {
            int sel = internal.flow_combo->GetSelection();
            if (sel != wxNOT_FOUND) {
                void *data = internal.flow_combo->GetClientData(sel);
                if (data) {
                    intptr_t index = reinterpret_cast<intptr_t>(data) - 1;
                    cfg.volume = static_cast<NozzleVolumeType>(index);
                }
            }
        }

        cfg.status = internal.error_label && internal.error_label->IsShown() ? NozzleStatus::nsError :
                     internal.unknown_label && internal.unknown_label->IsShown() ? NozzleStatus::nsUnknown :
                                                                                   NozzleStatus::nsNormal;
        configs.push_back(cfg);
    }

    return configs;
}

std::vector<NozzleConfig> ExtruderSettingsDialog::GetExtruderConfigs(int index) const
{
    if (index < 0 || index > 1) {
        return {};
    }

    if (!m_is_dual_extruder && index != 0) {
        return {};
    }

    ExtruderDialogPanel *panel = (index == 0) ? m_left_extruder_panel : m_right_extruder_panel;

    return panel->ExtractConfig();
}

void ExtruderSettingsDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    const int &em = em_unit();

    msw_buttons_rescale(this, em, {wxID_OK, wxID_CANCEL});

    const wxSize &size = wxSize(70 * em, 32 * em);
    SetMinSize(size);

    m_diameter_panel->Rescale();

    if (m_left_extruder_panel) {
        m_left_extruder_panel->Rescale();
    }
    if (m_right_extruder_panel) {
        m_right_extruder_panel->Rescale();
    }

    auto btn_rescale = [this](Button* btn) {
        if (!btn) return;
            btn->SetMinSize(wxSize(FromDIP(62), FromDIP(24)));
            btn->SetMaxSize(wxSize(FromDIP(62), FromDIP(24)));
            btn->SetFont(Label::Body_12);
            btn->SetCornerRadius(FromDIP(12));
            btn->Rescale();
            btn->SetCenter(true);
            btn->InvalidateBestSize();
            btn->SetSize(btn->GetMinSize());
            btn->Layout();
            btn->Refresh();
    };

    btn_rescale(m_confirm_btn);
    btn_rescale(m_cancel_btn);

    Layout();
    Fit();
    Refresh();
}

void ExtruderSettingsDialog::OnAMSCountChanged()
{
    m_extruder_container->Layout();
    Layout();

    Fit();

    wxSize current_size = GetSize();
    wxSize min_size = GetMinSize();

    if (current_size.x < min_size.x) {
        current_size.x = min_size.x;
    }
    if (current_size.y < min_size.y) {
        current_size.y = min_size.y;
    }

    SetSize(current_size);
    Refresh();
}

void ExtruderSettingsDialog::SaveAMSInitialState(ExtruderPanel *left_source, ExtruderPanel *right_source)
{
    if (left_source) {
        m_initial_left_ams.ams_n4 = left_source->m_ams_n4;
        m_initial_left_ams.ams_n1 = left_source->m_ams_n1;
        m_initial_left_ams.ams_4  = left_source->m_ams_4;
        m_initial_left_ams.ams_1  = left_source->m_ams_1;
        m_initial_left_ams.ext    = left_source->m_ext;
    }

    if (right_source) {
        m_initial_right_ams.ams_n4 = right_source->m_ams_n4;
        m_initial_right_ams.ams_n1 = right_source->m_ams_n1;
        m_initial_right_ams.ams_4  = right_source->m_ams_4;
        m_initial_right_ams.ams_1  = right_source->m_ams_1;
        m_initial_right_ams.ext    = right_source->m_ext;
    }
}

void ExtruderSettingsDialog::RestoreAMSInitialState()
{
    if (m_left_extruder_panel) {
        m_left_extruder_panel->m_ams_n4 = m_initial_left_ams.ams_n4;
        m_left_extruder_panel->m_ams_n1 = m_initial_left_ams.ams_n1;
        m_left_extruder_panel->m_ams_4  = m_initial_left_ams.ams_4;
        m_left_extruder_panel->m_ams_1  = m_initial_left_ams.ams_1;
        m_left_extruder_panel->m_ext    = m_initial_left_ams.ext;
    }

    if (m_right_extruder_panel) {
        m_right_extruder_panel->m_ams_n4 = m_initial_right_ams.ams_n4;
        m_right_extruder_panel->m_ams_n1 = m_initial_right_ams.ams_n1;
        m_right_extruder_panel->m_ams_4  = m_initial_right_ams.ams_4;
        m_right_extruder_panel->m_ams_1  = m_initial_right_ams.ams_1;
        m_right_extruder_panel->m_ext    = m_initial_right_ams.ext;
    }
}

void ExtruderSettingsDialog::AlignSeparatorIcon()
{
    if (!m_separator || !m_right_extruder_panel) return;

    Label* filament_label = m_right_extruder_panel->GetFilamentLabel();
    wxWindow* ams_row = nullptr;

    if (filament_label && filament_label->IsShown()) {
        ams_row = filament_label;
    }

    if (!ams_row) return;

    wxPoint ams_screen_pos = ams_row->GetScreenPosition();
    wxPoint separator_screen_pos = m_separator->GetScreenPosition();

    int ams_center_y = ams_screen_pos.y + ams_row->GetSize().y / 2;
    int separator_top_y = separator_screen_pos.y;
    int offset_y = ams_center_y - separator_top_y;

    m_separator->SetIconYOffset(offset_y);
}

void ExtruderSettingsDialog::SetupCallbacks()
{
    if (m_diameter_panel) {
        m_diameter_panel->SetNozzleQueryCallback([this](const wxString &diameter) -> bool {
            bool has_nozzle = false;

            if (m_left_extruder_panel) {
                auto nozzles = m_left_extruder_panel->GetNozzleConfigs();
                for (const auto &nozzle : nozzles) {
                    if (nozzle.diameter_combo) {
                        wxString nozzle_diameter = nozzle.diameter_combo->GetValue();
                        nozzle_diameter.Replace(" mm", "");
                        if (nozzle_diameter == diameter) {
                            has_nozzle = true;
                            break;
                        }
                    }
                }
            }

            if (!has_nozzle && m_right_extruder_panel) {
                auto nozzles = m_right_extruder_panel->GetNozzleConfigs();
                for (const auto &nozzle : nozzles) {
                    if (nozzle.diameter_combo) {
                        wxString nozzle_diameter = nozzle.diameter_combo->GetValue();
                        nozzle_diameter.Replace(" mm", "");
                        if (nozzle_diameter == diameter) {
                            has_nozzle = true;
                            break;
                        }
                    }
                }
            }

            return has_nozzle;
        });

        m_diameter_panel->Bind(wxEVT_BUTTON, [this](wxCommandEvent &event) {
            event.Skip();
            CallAfter([this]() {
                UpdateDialogState();
            });
        });
    }

    auto bind_combos = [this](ExtruderDialogPanel *panel) {
        if (!panel) return;
        auto nozzles = panel->GetNozzleConfigs();
        for (auto &nozzle : nozzles) {
            if (nozzle.diameter_combo) {
                nozzle.diameter_combo->Bind(wxEVT_COMBOBOX, [this, nozzle](wxCommandEvent &event) {
                    event.Skip();
                    CallAfter([this]() {
                        UpdateDialogState();
                    });
                });
            }

            if (nozzle.type_combo) {
                nozzle.type_combo->Bind(wxEVT_COMBOBOX, [this, nozzle](wxCommandEvent &event) {
                    event.Skip();
                    CallAfter([this]() {
                        UpdateDialogState();
                    });
                });
            }
        }
    };
    bind_combos(m_left_extruder_panel);
    bind_combos(m_right_extruder_panel);
}

void ExtruderSettingsDialog::UpdateDialogState()
{
    if (!m_diameter_panel) return;

    wxString selected_diameter = m_diameter_panel->GetSelectedDiameter();
    bool has_warning = false;
    bool has_empty = false;

    if (m_left_extruder_panel) {
        m_left_extruder_panel->UpdateWarningStates(selected_diameter);
        bool left_synced = m_left_extruder_panel->is_extruder_synced(selected_diameter);
        m_left_extruder_panel->ShowBadge(left_synced);

        auto nozzles = m_left_extruder_panel->GetNozzleConfigs();
        for (const auto &nozzle : nozzles) {
            if (nozzle.diameter_combo) {
                wxString nozzle_diameter = nozzle.diameter_combo->GetValue();
                nozzle_diameter.Replace(" mm", "");
                if (nozzle_diameter == _L("Empty")) {
                    has_empty = true;
                } else if (nozzle.type_combo && nozzle.type_combo->GetValue() == _L("Empty")) {
                    has_empty = true;
                } else if (nozzle_diameter != selected_diameter) {
                    has_warning = true;
                }
                if (has_empty && has_warning) {
                    break;
                }
            }
        }
    }

    if (m_right_extruder_panel) {
        m_right_extruder_panel->UpdateWarningStates(selected_diameter);
        bool right_synced = m_right_extruder_panel->is_extruder_synced(selected_diameter);
        m_right_extruder_panel->ShowBadge(right_synced);

        if (!has_warning || !has_empty) {
            auto nozzles = m_right_extruder_panel->GetNozzleConfigs();
            for (const auto &nozzle : nozzles) {
                if (nozzle.diameter_combo && nozzle.warning_icon) {
                    wxString nozzle_diameter = nozzle.diameter_combo->GetValue();
                    nozzle_diameter.Replace(" mm", "");
                    if (nozzle_diameter == _L("Empty")) {
                        has_empty = true;
                    } else if (nozzle.type_combo && nozzle.type_combo->GetValue() == _L("Empty")) {
                        has_empty = true;
                    } else if (nozzle_diameter != selected_diameter) {
                        has_warning = true;
                    }
                    if (has_empty && has_warning) {
                        break;
                    }
                }
            }
        }
    }

    if (m_warning_panel) {
        m_warning_panel->Show(has_warning);
    }
    if (m_warning_serious_panel) {
        m_warning_serious_panel->Show(has_empty);
    }

    m_diameter_panel->UpdateButtonStates();
    Layout();
    Fit();
}

ExtruderPanelGroup::ExtruderPanelGroup(wxWindow                        *parent,
                                       PanelType                        type,
                                       const std::vector<NozzleConfig> &left_configs,
                                       const std::vector<NozzleConfig> &right_configs,
                                       const std::vector<NozzleConfig> &single_configs)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
    , m_panel_type(type)
    , m_left_configs(left_configs)
    , m_right_configs(right_configs)
    , m_single_configs(single_configs)
{
    SetBackgroundColour(wxColour("#F7F7F7"));
    Bind(wxEVT_PAINT, &ExtruderPanelGroup::OnPaint, this);

    CreateLayout();
    BindClickEvents();
    ShowBadge(false);

    wxGetApp().UpdateDarkUIWin(this);
}

void ExtruderPanelGroup::OnPaint(wxPaintEvent &evt)
{
    wxAutoBufferedPaintDC dc(this);
    wxGraphicsContext    *gc = wxGraphicsContext::Create(dc);
    wxSize                size = GetSize();
    if (gc) {
        dc.Clear();
        gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);
        wxGraphicsPath path   = gc->CreatePath();
        double         radius = FromDIP(2);

        path.AddRoundedRectangle(0, 0, size.x - 1, size.y - 1, radius);

        //gc->SetBrush(StateColor::darkModeColorFor(wxColour("#FF0000")));
        //gc->FillPath(path);

        auto  color = StateColor::darkModeColorFor(wxColour("#CECECE"));
        wxPen pen(color, FromDIP(1));
        gc->SetPen(pen);
        gc->StrokePath(path);

        delete gc;
    }

#ifdef __WXMSW__
    if (m_badge.bmp().IsOk()) {
        auto s = m_badge.bmp().GetScaledSize();
        dc.DrawBitmap(m_badge.bmp(), GetSize().x - s.x, 0);
    }
#endif
}

void ExtruderPanelGroup::CreateLayout()
{
    m_main_sizer = new wxBoxSizer(wxHORIZONTAL);

    if (m_panel_type == Dual) {
        m_left_panel = new ExtruderPanel(this, ExtruderPanel::LeftExtruder);
        m_main_sizer->Add(m_left_panel, 1, wxEXPAND | wxALL, FromDIP(2));

        m_separator = new CustomSeparator(this, true);
        m_main_sizer->Add(m_separator, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(4));

        m_right_panel = new ExtruderPanel(this, ExtruderPanel::RightExtruder);
        m_main_sizer->Add(m_right_panel, 1, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, FromDIP(2));
    } else {
        m_single_panel = new ExtruderPanel(this, ExtruderPanel::SingleExtruder);
        m_main_sizer->Add(m_single_panel, 1, wxEXPAND | wxALL, FromDIP(2));
    }

    auto separator2 = new wxPanel(this, wxID_ANY);
    separator2->SetBackgroundColour(wxColour("#CECECE"));
    separator2->SetMinSize(wxSize(FromDIP(1), -1));
    m_main_sizer->Add(separator2, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(4));

    auto arrow = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("arrow_right", this, 20), wxDefaultPosition, wxSize(FromDIP(20), FromDIP(20)), 0);
    arrow->SetMinSize(wxSize(FromDIP(20), FromDIP(20)));
    m_main_sizer->Add(arrow, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(10));

    SetSizer(m_main_sizer);
    Layout();
    Fit();
}

void ExtruderPanelGroup::BindClickEvents() {
    std::function<void(wxWindow*)> bind_recursive = [this, &bind_recursive](wxWindow* window) {
        if (!window) return;

        if (dynamic_cast<wxButton*>(window) ||
            dynamic_cast<wxComboBox*>(window) ||
            dynamic_cast<ScalableButton*>(window) ||
            dynamic_cast<wxScrollBar*>(window) ||
            dynamic_cast<wxTextCtrl*>(window) ||
            dynamic_cast<wxSpinCtrl*>(window)) {
            return;
        }

        window->Bind(wxEVT_LEFT_DOWN, &ExtruderPanelGroup::OnPanelClick, this);

        for (auto* child : window->GetChildren()) {
            bind_recursive(child);
        }
    };

    bind_recursive(this);
}

void ExtruderPanelGroup::OnPanelClick(wxMouseEvent &evt)
{
    if (m_on_click) {
        bool is_dual = (m_panel_type == Dual);
        m_on_click(is_dual);
    }
}

ExtruderPanel *ExtruderPanelGroup::GetPanel(int index) const
{
    if (m_panel_type == Dual) {
        if (index == 0) {
            return m_left_panel;
        } else if (index == 1) {
            return m_right_panel;
        }
    } else if (m_panel_type == Single && index == 0) {
        return m_single_panel;
    }
    return nullptr;
}

void ExtruderPanelGroup::SetSeparatorState(CustomSeparator::State state)
{
    if (m_separator) {
        m_separator->SetState(state);
    }
}

void ExtruderPanelGroup::ShowSeparatorIcon(bool show)
{
    if (m_separator) {
        m_separator->SetState(show ? CustomSeparator::Normal : CustomSeparator::Hidden);
    }
}

void ExtruderPanelGroup::Rescale()
{
    if (m_left_panel) {
        m_left_panel->Rescale();
    }
    if (m_right_panel) {
        m_right_panel->Rescale();
    }
    if (m_single_panel) {
        m_single_panel->Rescale();
    }

    Layout();
    Fit();
    Refresh();
}

void ExtruderPanelGroup::ShowBadge(bool show)
{
#ifdef __WXMSW__
    if (show && m_badge.name() != "badge") {
        m_badge = ScalableBitmap(this, "badge", 18);
        Refresh();
    } else if (!show && !m_badge.name().empty()) {
        m_badge = ScalableBitmap{};
        Refresh();
    }
#endif
#ifdef __WXOSX__
    if (show && m_badge == nullptr) {
        m_badge = new ScalableButton(this, wxID_ANY, "badge", wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, false, 18);
        m_badge->SetSize(m_badge->GetBestSize());
        m_badge->SetBackgroundColour("#F7F7F7");

        LayoutBadge();
    }
    if (m_badge) { m_badge->Show(show); }
#endif
}

#ifdef __WXOSX__
void ExtruderPanelGroup::LayoutBadge()
{
    if (!m_badge) return;

    wxSize panel_size = GetSize();
    wxSize badge_size = m_badge->GetBestSize();

    int x = panel_size.x - badge_size.x - FromDIP(8);
    int y = FromDIP(8);

    m_badge->SetPosition(wxPoint(x, y));
}

void ExtruderPanelGroup::OnSize(wxSizeEvent &evt)
{
    LayoutBadge();
    evt.Skip();
}
#endif

CustomSeparator::CustomSeparator(wxWindow *parent, bool is_small) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
{
    SetBackgroundColour(wxColour("#F7F7F7"));
    int size = is_small ? 9 : 20;

    SetMinSize(wxSize(FromDIP(size * 2), -1));

    m_normal_bitmap = ScalableBitmap(this, "fila_switch", size);
    m_error_bitmap  = ScalableBitmap(this, "fila_switch_error", size);

    CreateLayout();
}

void CustomSeparator::CreateLayout()
{
    auto main_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_separator_line = new wxPanel(this, wxID_ANY);
    m_separator_line->SetBackgroundColour(wxColour("#CECECE"));
    m_separator_line->SetSize(wxSize(FromDIP(1), -1));

    m_icon = new wxStaticBitmap(this, wxID_ANY, m_normal_bitmap.bmp(),
                                wxDefaultPosition, m_normal_bitmap.GetBmpSize());
    m_icon->SetBackgroundColour(GetBackgroundColour());
    m_icon->Hide();

    auto overlay_sizer = new wxBoxSizer(wxVERTICAL);
    overlay_sizer->AddStretchSpacer();
    overlay_sizer->Add(m_icon, 0, wxALIGN_CENTER_HORIZONTAL);
    overlay_sizer->AddStretchSpacer();

    main_sizer->AddStretchSpacer(1);
    main_sizer->Add(m_separator_line, 0, wxEXPAND);
    main_sizer->AddStretchSpacer(1);

    SetSizer(main_sizer);

    Bind(wxEVT_SIZE, [this](wxSizeEvent& evt) {
        UpdatePosition();
        evt.Skip();
    });
}

void CustomSeparator::SetState(State state)
{
    if (m_state == state) return;

    m_state = state;

    switch (state) {
    case Normal:
        m_icon->SetBitmap(m_normal_bitmap.bmp());
        m_icon->Show();
        Show();
        break;

    case Error:
        m_icon->SetBitmap(m_error_bitmap.bmp());
        m_icon->Show();
        Show();
        break;

    case Hidden:
        m_icon->Hide();
        Show();
        break;
    }

    UpdatePosition();
    Refresh();
}

void CustomSeparator::UpdatePosition()
{
    if (!m_icon) return;

    wxSize panel_size = GetSize();
    wxSize icon_size  = m_icon->GetSize();

    int x = (panel_size.x - icon_size.x) / 2;
    int y;
    if (m_icon_y_offset < 0) {
        y = (panel_size.y - icon_size.y) / 2;
    } else {
        y = m_icon_y_offset - icon_size.y / 2;
    }

    y = std::max(0, std::min(y, panel_size.y - icon_size.y));

    m_icon->SetPosition(wxPoint(x, y));
    m_icon->Raise();
}

}}
