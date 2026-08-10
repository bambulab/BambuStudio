#include "ExtruderPanel.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/settings.h>
#include <wx/dcbuffer.h>

#include "I18N.hpp"
#include "GUI_App.hpp"
#include "wxExtensions.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/SwitchButton.hpp"
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

// Extract the leading numeric part of a diameter label (e.g. "0.4 mm" -> "0.4"),
// stopping at the first character that is not a digit or decimal point.
static wxString parse_diameter_value(const wxString &label)
{
    wxString value;
    for (const auto &ch : label) {
        if (wxIsdigit(ch) || ch == '.')
            value += ch;
        else
            break;
    }
    return value;
}

DiameterButtonPanel::DiameterButtonPanel(wxWindow *parent, const std::vector<wxString> &diameter_choices, bool show_title,
                                         DisabledTooltipContext tooltip_context, bool show_multi_nozzle_mode)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
    , m_choices(diameter_choices)
    , m_show_title(show_title)
    , m_show_multi_nozzle_mode(show_multi_nozzle_mode)
    , m_tooltip_context(tooltip_context)
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

        auto title_sizer = new wxBoxSizer(wxHORIZONTAL);
        title_sizer->Add(title, 0, wxALIGN_CENTER_VERTICAL);
        if (m_show_multi_nozzle_mode) {
            title_sizer->AddStretchSpacer();
            m_multi_nozzle_mode_switch = new SwitchButton(this);
            m_multi_nozzle_mode_switch->SetValue(m_multi_nozzle_mode_enabled);
            m_multi_nozzle_mode_switch->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent &event) {
                m_multi_nozzle_mode_enabled = m_multi_nozzle_mode_switch->GetValue();
                if (m_multi_nozzle_mode_callback)
                    m_multi_nozzle_mode_callback(m_multi_nozzle_mode_enabled);
                event.Skip();
            });
            title_sizer->Add(m_multi_nozzle_mode_switch, 0, wxALIGN_CENTER_VERTICAL);

            auto mode_label = new Label(this, _L("Multi-nozzle mode"));
            mode_label->SetFont(Label::Body_12);
            title_sizer->Add(mode_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(4));
        }
        main_sizer->Add(title_sizer, 0, wxEXPAND | wxBOTTOM, FromDIP(4));
    }
    auto button_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_buttons.clear();
    for (const auto &diameter : m_choices) {
        auto btn = new Button(this, diameter);
        const int button_width = m_tooltip_context == FilamentContext ? 40 : 54;
        btn->SetMinSize(wxSize(FromDIP(button_width), FromDIP(24)));
        btn->SetMaxSize(wxSize(FromDIP(button_width), FromDIP(24)));
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

void DiameterButtonPanel::SetMultiNozzleMode(bool enabled)
{
    m_multi_nozzle_mode_enabled = enabled;
    if (m_multi_nozzle_mode_switch)
        m_multi_nozzle_mode_switch->SetValue(enabled);
}

void DiameterButtonPanel::OnButtonClicked(wxCommandEvent &event)
{
    Button *clicked_btn = dynamic_cast<Button *>(event.GetEventObject());
    if (!clicked_btn) return;

    wxString diameter = clicked_btn->GetLabel();

    // A grey (NoNozzle) button has no nozzle configured for this diameter. Instead of
    // switching to it, open the extruder settings dialog so the user can set one up.
    if (clicked_btn->IsGrayed()) {
        if (m_no_nozzle_click_callback) m_no_nozzle_click_callback(diameter);
        return;
    }

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
    if (m_multi_nozzle_mode_switch)
        m_multi_nozzle_mode_switch->Rescale();
    for (auto *btn : m_buttons) {
        if (btn) {
            const int button_width = m_tooltip_context == FilamentContext ? 40 : 54;
            btn->SetMinSize(wxSize(FromDIP(button_width), FromDIP(24)));
            btn->SetMaxSize(wxSize(FromDIP(button_width), FromDIP(24)));
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

    wxString tip = m_tooltip_context == FilamentContext
        ? wxString::Format(_L("This filament does not support the %smm nozzle, or no matching nozzle is configured."), btn->GetLabel())
        : wxString::Format(_L("Please set a %smm diameter nozzle for the printer in the section below first before using this diameter for slicing."), btn->GetLabel());
    state == NoNozzle ? btn->SetToolTip(tip) : btn->UnsetToolTip();
    btn->Refresh();
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
    m_disabled = false;
    wxColour text_color = DiameterNormalColor;
    wxColour flow_color = FlowNormalColor;
    if (m_disabled_icon) {
        m_disabled_icon->Show(false);
    }
    if (m_left_scroll && m_right_scroll) {
        UpdateScrollButtons();
    }

    auto tooltip = enable
        ? wxString()
        : _L("This extruder has a different nozzle diameter from the selected printing diameter. "
             "Printing with mixed nozzle diameters is experimental.");

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
    wxString text = _L("Nozzle NO. ");
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

    // wxWindow *parent_window = m_scroll ? m_scroll : m_ams_sizer->GetContainingWindow();
    // if (!m_ext.empty()) {
    //     while (m_ext_previews.size() < m_ext.size()) {
    //         AMSinfo info;
    //         info.ams_type = DevAmsType::N3S;
    //         auto ext_preview = new AMSPreview(parent_window, wxID_ANY, info);
    //         ext_preview->Close();
    //         m_ext_previews.push_back(ext_preview);
    //     }

    //     for (size_t i = 0; i < m_ext.size(); ++i) {
    //         if (i < m_ext_previews.size()) {
    //             AMSinfo &ext_info = m_ext[i];
    //             m_ext_previews[i]->Update(ext_info);
    //             m_ext_previews[i]->Refresh();
    //             m_ext_previews[i]->Open();

    //             m_ams_sizer->Add(m_ext_previews[i], 0, wxALL, FromDIP(2));
    //         }
    //     }
    // } else {
    //     if (m_ext_previews.empty()) {
    //         AMSinfo info;
    //         info.ams_type    = DevAmsType::EXT_SPOOL;
    //         auto ext_preview = new AMSPreview(parent_window, wxID_ANY, info);
    //         ext_preview->Close();
    //         m_ext_previews.push_back(ext_preview);
    //     }

    //     m_ext_previews[0]->Update(info_ext);
    //     m_ext_previews[0]->Refresh();
    //     m_ext_previews[0]->Open();
    //     m_ams_sizer->Add(m_ext_previews[0], 0, wxALL, FromDIP(2));
    // }

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
