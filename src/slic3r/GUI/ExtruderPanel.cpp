#include "ExtruderPanel.hpp"

#include <wx/bitmap.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/settings.h>
#include <wx/dcbuffer.h>
#include <wx/timer.h>

#include <algorithm>

#include <boost/log/trivial.hpp>

#include "I18N.hpp"
#include "GUI_App.hpp"
#include "ExtruderHelpers.hpp"
#include "libslic3r/PresetBundle.hpp" // project_config, for reading multi-diameter mode
#include "wxExtensions.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/StateColor.hpp"
#include "wx/graphics.h"

namespace Slic3r::GUI {

static const wxColour SelectedBgColor      = ThemeColor::BrandGreen;
static const wxColour SelectedBorderColor  = SelectedBgColor;
static const wxColour SelectedTextColor    = ThemeColor::White;
static const wxColour HasNozzleBgColor     = ThemeColor::White;
static const wxColour HasNozzleBorderColor = ThemeColor::Grey400;
static const wxColour HasNozzleTextColor   = ThemeColor::TextPrimary;
static const wxColour NoNozzleBgColor      = ThemeColor::White;
static const wxColour NoNozzleBorderColor  = ThemeColor::Grey300;
static const wxColour NoNozzleTextColor    = ThemeColor::Grey400;

static const wxColour DiameterNormalColor   = ThemeColor::TextMuted;
static const wxColour FlowNormalColor       = ThemeColor::Grey500;
static const wxColour DiameterDisabledColor = ThemeColor::Grey400;
static const wxColour FlowDisabledColor     = ThemeColor::Grey350;

DiameterButtonPanel::DiameterButtonPanel(wxWindow *parent, const std::vector<wxString> &diameter_choices, bool show_title,
                                         DisabledTooltipContext tooltip_context, bool show_multi_diameter_mode)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
    , m_choices(diameter_choices)
    , m_show_title(show_title)
    , m_show_multi_diameter_mode(show_multi_diameter_mode)
    , m_tooltip_context(tooltip_context)
{
    SetBackgroundColour(ThemeColor::White);
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

    // Production nozzle-diameter mode selector (was a debug SwitchButton). Item 0 = single, 1 = multi.
    if (m_show_multi_diameter_mode) {
        m_multi_diameter_mode_combo = new ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, FromDIP(wxSize(150, 24)), 0, nullptr, wxCB_READONLY);
        m_multi_diameter_mode_combo->Append(_L("Single-diameter mode"), {});
        m_multi_diameter_mode_combo->Append(_L("Multi-diameter mode"), {});
        m_multi_diameter_mode_combo->SetSelection(m_multi_diameter_mode_enabled ? 1 : 0);
        m_multi_diameter_mode_combo->Show(m_mode_selector_visible);
        m_multi_diameter_mode_combo->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent &event) {
            m_multi_diameter_mode_enabled = (m_multi_diameter_mode_combo->GetSelection() == 1);
            UpdateSelectorVisibility();
            if (m_multi_diameter_mode_callback)
                m_multi_diameter_mode_callback(m_multi_diameter_mode_enabled);
            event.Skip();
        });
        main_sizer->Add(m_multi_diameter_mode_combo, 0, wxALIGN_LEFT | wxBOTTOM, FromDIP(4));
    }

    m_title_sizer = nullptr;
    if (m_show_title) {
        auto title = new Label(this, _L("Set nozzle diameter"));
        title->SetFont(Label::Body_13);
        title->SetForegroundColour(ThemeColor::Grey500);

        m_title_sizer = new wxBoxSizer(wxHORIZONTAL);
        m_title_sizer->Add(title, 0, wxALIGN_CENTER_VERTICAL);
        main_sizer->Add(m_title_sizer, 0, wxEXPAND | wxBOTTOM, FromDIP(4));
    }
    m_button_sizer = new wxBoxSizer(wxHORIZONTAL);
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
        m_button_sizer->Add(btn, 0, wxRIGHT, FromDIP(4));
    }

    main_sizer->Add(m_button_sizer, 0, wxALIGN_LEFT);
    SetSizer(main_sizer);
    UpdateSelectorVisibility();
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

void DiameterButtonPanel::SetMultiDiameterMode(bool enabled)
{
    m_multi_diameter_mode_enabled = enabled;
    if (m_multi_diameter_mode_combo)
        m_multi_diameter_mode_combo->SetSelection(enabled ? 1 : 0);
    UpdateSelectorVisibility();
}

void DiameterButtonPanel::ShowModeSelector(bool show)
{
    m_mode_selector_visible = show; // remembered so a later CreateLayout rebuild keeps the state
    if (!m_multi_diameter_mode_combo || m_multi_diameter_mode_combo->IsShown() == show)
        return;
    m_multi_diameter_mode_combo->Show(show);
    if (GetSizer()) GetSizer()->Layout();
    Layout();
    if (wxWindow *parent = GetParent()) parent->Layout();
}

void DiameterButtonPanel::UpdateSelectorVisibility()
{
    if (!GetSizer()) return;
    // Only the sidebar panel (the one carrying the mode selector) drops its diameter buttons in
    // multi-diameter mode; every other context keeps them visible.
    const bool show = !(m_show_multi_diameter_mode && m_multi_diameter_mode_enabled);
    if (m_title_sizer) GetSizer()->Show(m_title_sizer, show, true);
    if (m_button_sizer) GetSizer()->Show(m_button_sizer, show, true);
    GetSizer()->Layout();
    Layout();
    if (wxWindow *parent = GetParent()) parent->Layout();
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
    if (m_multi_diameter_mode_combo)
        m_multi_diameter_mode_combo->SetMinSize(FromDIP(wxSize(150, 24)));
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
    SetBackgroundColour(ThemeColor::Grey200);
}

void HorizontalScrollablePanel::Clear()
{
    GetSizer()->Clear(true);
    //UpdateScrollbars();
}

/**
 * \brief One nozzle's summary tile: icon + diameter + flow + count.
 *
 * Stamped once for a single-nozzle extruder, or repeated per nozzle for a
 * single-extruder multi-nozzle setup.
 */
class NozzleSummary : public wxPanel
{
public:
    explicit NozzleSummary(wxWindow *parent) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
    {
        SetBackgroundColour(ThemeColor::Grey200);

        auto *main_sizer = new wxBoxSizer(wxHORIZONTAL);

        auto  icon        = create_scaled_bitmap("dev_rack_nozzle_error", this, 24);
        auto *nozzle_icon = new wxStaticBitmap(this, wxID_ANY, icon);

        auto *label_sizer = new wxBoxSizer(wxVERTICAL);
        m_diameter        = new Label(this, "0.4");
        m_diameter->SetFont(Label::Body_12);
        m_diameter->SetForegroundColour(DiameterNormalColor);
        m_diameter->SetBackgroundColour(ThemeColor::Grey200);

        m_flow = new Label(this, "Std");
        m_flow->SetFont(Label::Body_12);
        m_flow->SetForegroundColour(FlowNormalColor);
        m_flow->SetBackgroundColour(ThemeColor::Grey200);

        label_sizer->Add(m_diameter, 0, wxALIGN_LEFT | wxBOTTOM, FromDIP(2));
        label_sizer->Add(m_flow, 0, wxALIGN_LEFT);

        m_nozzle_number = new Label(this, "x1");
        m_nozzle_number->SetFont(Label::Body_15);
        m_nozzle_number->SetForegroundColour(DiameterNormalColor);
        m_nozzle_number->SetBackgroundColour(ThemeColor::Grey200);

        main_sizer->Add(nozzle_icon, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(8));
        main_sizer->Add(label_sizer, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
        main_sizer->Add(m_nozzle_number, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

        SetSizer(main_sizer);
        Layout();
    }

    void SetDiameter(const wxString &diameter) { m_diameter->SetLabel(diameter); }
    void SetFlow(const wxString &flow) { m_flow->SetLabel(flow); }
    void SetNozzleNumber(int num) { m_nozzle_number->SetLabel(wxString::Format("x%d", num)); }
    void ShowNozzleNumber(bool show) { m_nozzle_number->Show(show); }

private:
    Label *m_diameter{};
    Label *m_flow{};
    Label *m_nozzle_number{};
};

// ----------------------------------------------------------------------------
// ExtruderPanel (abstract base)
// ----------------------------------------------------------------------------

ExtruderPanel::ExtruderPanel(wxWindow *parent, GroupType type) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE), m_type(type)
{
    SetForegroundColour(ThemeColor::Grey400);
    SetBackgroundColour(ThemeColor::Grey200);
}

void ExtruderPanel::Build()
{
    SetMinSize(wxSize(-1, FromDIP(40)));
    m_main_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_summary_sizer = new wxBoxSizer(wxHORIZONTAL); // NozzleSummary tiles, filled by UpdateNozzles
    m_main_sizer->Add(m_summary_sizer, 0, wxALIGN_CENTER_VERTICAL);

    CreateContent(m_main_sizer);

    // Trailing warning icon; UpdateNozzles shows it when the selected diameter has no matching nozzle.
    m_warning_icon = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("warning", this, 20), wxDefaultPosition, wxSize(FromDIP(20), FromDIP(20)), 0);
    m_warning_icon->Hide();
    m_main_sizer->Add(m_warning_icon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

    SetSizer(m_main_sizer);
    Layout();

    wxGetApp().UpdateDarkUIWin(this);
    CallAfter([this]() { update_ams(); });
}

// Set (or clear) a tooltip on a window and its whole descendant tree.
static void set_tooltip_recursive(wxWindow *window, const wxString &tooltip, bool clear)
{
    if (!window) return;
    clear ? window->UnsetToolTip() : window->SetToolTip(tooltip);
    for (auto *child : window->GetChildren()) set_tooltip_recursive(child, tooltip, clear);
}

static wxString volume_short(NozzleVolumeType volume) { return wxString::FromUTF8(DevNozzle::ToNozzleVolumeShortString(volume)); }

void ExtruderPanel::add_summary_tile(const wxString &diameter, const wxString &flow, int count, bool show_count)
{
    auto *tile = new NozzleSummary(this);
    tile->SetDiameter(diameter);
    tile->SetFlow(flow);
    tile->SetNozzleNumber(count);
    tile->ShowNozzleNumber(show_count);
    m_summary_sizer->Add(tile, 0, wxALIGN_CENTER_VERTICAL);
    m_summaries.push_back(tile);
    wxGetApp().UpdateDarkUIWin(tile); // tiles are created after Build()'s one-time dark-mode pass
}

void ExtruderPanel::finish_projection(bool enabled)
{
    if (m_warning_icon) m_warning_icon->Show(!enabled);
    UpdateScrollButtons();

    auto tooltip = enabled ? wxString() :
                             _L("This extruder has a different nozzle diameter from the selected printing diameter. "
                                "Printing with mixed nozzle diameters is experimental.");
    set_tooltip_recursive(this, tooltip, enabled);

    Layout();
    Refresh();
}

// Single-diameter mode: the default projection for every extruder. Shows the nozzle(s) at the
// selected diameter as one tile. SingleExtruderPanel overrides this to add the experimental multi mode.
void ExtruderPanel::UpdateNozzles(const std::vector<NozzleConfig> &configs, const wxString &selected_diameter)
{
    m_summary_sizer->Clear(true); // destroy the tiles from the previous projection
    m_summaries.clear();

    // One pass to count and characterize the nozzles at the selected diameter.
    int              matching     = 0;
    NozzleVolumeType first_volume{};
    bool             mixed_volume = false;
    for (const auto &c : configs) {
        if (c.diameter != selected_diameter) continue;
        if (matching == 0)
            first_volume = c.volume;
        else if (c.volume != first_volume)
            mixed_volume = true;
        matching++;
    }

    if (configs.size() == 1) {
        // A single-nozzle extruder shows its own diameter/volume, unfiltered.
        add_summary_tile(configs[0].diameter, volume_short(configs[0].volume), 0, false);
    } else {
        wxString flow = matching == 0 ? wxString("--") : mixed_volume ? wxString("Mix") : volume_short(first_volume);
        add_summary_tile(selected_diameter, flow, matching, matching > 1 || (matching == 0 && m_type == RightExtruder));
    }

    finish_projection(matching > 0);
}

void ExtruderPanel::Rescale()
{
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

// ----------------------------------------------------------------------------
// DualExtruderPanel — one side (left/right) of a dual card: scroll + AMS previews
// ----------------------------------------------------------------------------

void DualExtruderPanel::CreateContent(wxBoxSizer *main_sizer)
{
    m_scroll_timer = new wxTimer(this);
    Bind(wxEVT_TIMER, &DualExtruderPanel::OnScrollTimer, this);

    m_scroll = new HorizontalScrollablePanel(this);
    m_scroll->SetMinSize(wxSize(FromDIP(50), FromDIP(40)));
    m_ams_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_scroll->SetSizer(m_ams_sizer);

    for (size_t i = 0; i < 4; ++i) {
        AMSinfo info;
        info.ams_type = DevAmsType::AMS;
        auto preview  = new AMSPreview(m_scroll, wxID_ANY, info);
        preview->Close();
        m_ams_previews.push_back(preview);
        m_ams_sizer->Add(preview, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(2));
    }

    for (size_t i = 0; i < 8; ++i) {
        AMSinfo info;
        info.ams_type = DevAmsType::N3S;
        auto preview  = new AMSPreview(m_scroll, wxID_ANY, info);
        preview->Close();
        m_ams_previews.push_back(preview);
        m_ams_sizer->Add(preview, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(2));
    }

    m_left_scroll  = new ScalableButton(this, wxID_ANY, "left_arrow", wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, true, 12);
    m_right_scroll = new ScalableButton(this, wxID_ANY, "right_arrow", wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, true, 12);

    m_left_scroll->Bind(wxEVT_BUTTON, &DualExtruderPanel::OnLeftScrollClick, this);
    m_right_scroll->Bind(wxEVT_BUTTON, &DualExtruderPanel::OnRightScrollClick, this);

    m_left_scroll->Bind(wxEVT_LEFT_DOWN, &DualExtruderPanel::OnLeftScrollDown, this);
    m_left_scroll->Bind(wxEVT_LEFT_UP, &DualExtruderPanel::OnLeftScrollUp, this);
    m_left_scroll->Bind(wxEVT_LEAVE_WINDOW, &DualExtruderPanel::OnLeftScrollUp, this);

    m_right_scroll->Bind(wxEVT_LEFT_DOWN, &DualExtruderPanel::OnRightScrollDown, this);
    m_right_scroll->Bind(wxEVT_LEFT_UP, &DualExtruderPanel::OnRightScrollUp, this);
    m_right_scroll->Bind(wxEVT_LEAVE_WINDOW, &DualExtruderPanel::OnRightScrollUp, this);

    m_left_scroll->Show(false);
    m_right_scroll->Show(false);

    main_sizer->Add(m_scroll, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    main_sizer->Add(m_left_scroll, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(0));
    main_sizer->Add(m_right_scroll, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
}

void DualExtruderPanel::update_ams()
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

void DualExtruderPanel::OnLeftScrollClick(wxCommandEvent &evt)
{
    if (!m_scroll) return;

    int scroll_unit = FromDIP(5);
    int current_pos = m_scroll->GetScrollPos(wxHORIZONTAL);
    int new_pos     = std::max(0, current_pos - scroll_unit);

    m_scroll->Scroll(new_pos, -1);
    UpdateScrollButtons();
}

void DualExtruderPanel::OnRightScrollClick(wxCommandEvent &evt)
{
    if (!m_scroll) return;

    int scroll_unit = FromDIP(5);
    int current_pos = m_scroll->GetScrollPos(wxHORIZONTAL);
    int max_pos     = m_scroll->GetScrollRange(wxHORIZONTAL);
    int new_pos     = std::min(max_pos, current_pos + scroll_unit);

    m_scroll->Scroll(new_pos, -1);
    UpdateScrollButtons();
}

void DualExtruderPanel::OnScrollSizeChanged(wxSizeEvent &evt)
{
    UpdateScrollButtons();
    evt.Skip();
}

void DualExtruderPanel::UpdateScrollButtons()
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

    m_left_scroll->Show(true);
    m_right_scroll->Show(true);

    Layout();
    Refresh();
}

void DualExtruderPanel::OnLeftScrollDown(wxMouseEvent &evt)
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

void DualExtruderPanel::OnLeftScrollUp(wxMouseEvent &evt)
{
    m_scroll_left = false;
    if (!m_scroll_right) {
        m_scroll_timer->Stop();
    }
    evt.Skip();
}

void DualExtruderPanel::OnRightScrollDown(wxMouseEvent &evt)
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

void DualExtruderPanel::OnRightScrollUp(wxMouseEvent &evt)
{
    m_scroll_right = false;
    if (!m_scroll_left) {
        m_scroll_timer->Stop();
    }
    evt.Skip();
}

void DualExtruderPanel::OnScrollTimer(wxTimerEvent &evt)
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

void DualExtruderPanel::Rescale()
{
    for (auto *ams : m_ams_previews) {
        if (ams) { ams->msw_rescale(); }
    }

    for (auto *ext : m_ext_previews) {
        if (ext) { ext->msw_rescale(); }
    }

    if (m_scroll) {
        m_scroll->SetScrollRate(FromDIP(5), FromDIP(5));
        m_scroll->FitInside();
    }

    ExtruderPanel::Rescale();
}

void DualExtruderPanel::sync_ams(
    MachineObject const *obj, std::vector<DevAms *> const &ams_internal, std::vector<DevAms *> const &ams4, std::vector<DevAms *> const &ams1, std::vector<DevAmsTray> const &ext)
{
    if (m_ams_4.empty() && ams4.empty() && m_ams_1.empty() && ams1.empty() && m_ext.empty() && ext.empty()) return;
    auto sync = [obj](std::vector<AMSinfo> &infos, std::vector<DevAms *> const &ams) -> bool {
        std::vector<AMSinfo> infos2;
        for (auto a : ams) {
            AMSinfo ams_info;
            ams_info.parse_ams_info(const_cast<MachineObject *>(obj), a, obj->GetFilaSystem()->IsDetectRemainEnabled(), obj->is_support_ams_humidity);
            infos2.push_back(ams_info);
        }
        if (infos == infos2) return false;

        for (size_t ams_index = 0; ams_index < infos2.size(); ++ams_index) {
            auto ams_info = infos2[ams_index];
            BOOST_LOG_TRIVIAL(info) << "DualExtruderPanel::sync_ams - AMS Info: "
                                    << "ams_id = \"" << ams_info.ams_id << "\""
                                    << ", ams_type = " << static_cast<int>(ams_info.ams_type) << ", cans_count = " << ams_info.cans.size();

            for (size_t can_index = 0; can_index < ams_info.cans.size(); ++can_index) {
                const auto &can_info = ams_info.cans[can_index];

                BOOST_LOG_TRIVIAL(info) << "DualExtruderPanel::sync_ams - Can[" << can_index << "]: "
                                        << "can_id = \"" << can_info.can_id << "\""
                                        << ", material_name = \"" << can_info.material_name.ToStdString() << "\""
                                        << ", material_colour = " << can_info.material_colour.GetAsString(2).ToStdString()
                                        << ", material_state = " << static_cast<int>(can_info.material_state) << ", multi_colours_count = " << can_info.material_cols.size();

                if (can_info.material_cols.size() > 1) {
                    std::string multi_colors_str;
                    for (size_t i = 0; i < can_info.material_cols.size(); ++i) {
                        if (i > 0) multi_colors_str += ",";
                        const auto &color = can_info.material_cols[i];
                        multi_colors_str += color.GetAsString(2).ToStdString();
                    }
                    BOOST_LOG_TRIVIAL(info) << "DualExtruderPanel::sync_ams - Can[" << can_index << "] MultiColors: " << multi_colors_str;
                }
            }
        }

        infos.swap(infos2);
        return true;
    };

    auto sync_ext = [obj](std::vector<AMSinfo> &infos, std::vector<DevAmsTray> const &trays) -> bool {
        std::vector<AMSinfo> infos2;
        for (auto tray : trays) {
            AMSinfo ext_info;
            ext_info.parse_ext_info(const_cast<MachineObject *>(obj), tray);
            infos2.push_back(ext_info);
        }
        if (infos == infos2) return false;

        BOOST_LOG_TRIVIAL(info) << "DualExtruderPanel::sync_ams - External material count: " << infos2.size();
        for (size_t i = 0; i < infos2.size(); ++i) {
            const auto &ext_info = infos2[i];
            BOOST_LOG_TRIVIAL(info) << "DualExtruderPanel::sync_ams - Ext[" << i << "]: "
                                    << "tray_id = \"" << ext_info.ams_id << "\"";
            if (!ext_info.cans.empty()) {
                const auto &can = ext_info.cans[0];
                BOOST_LOG_TRIVIAL(info) << "  material_name = \"" << can.material_name.ToStdString() << "\""
                                        << ", colour = " << can.material_colour.GetAsString(2).ToStdString();
            }
        }

        infos.swap(infos2);
        return true;
    };

    if (sync(m_ams_4, ams4) || sync(m_ams_1, ams1) || sync(m_ams_internal, ams_internal) || sync_ext(m_ext, ext)) update_ams();
}

// ----------------------------------------------------------------------------
// SingleExtruderPanel — single head with multiple nozzles: nozzle-index label
// ----------------------------------------------------------------------------

// The single-extruder card adds the nozzle-index label; the NozzleSummary tiles come from the base.
// Proportion 1 so it fills the middle and pushes the trailing warning icon to the right end.
void SingleExtruderPanel::CreateContent(wxBoxSizer *main_sizer)
{
    m_nozzle_index_label = new Label(this, _L("No Available Nozzle"));
    m_nozzle_index_label->SetFont(Label::Body_12);
    m_nozzle_index_label->SetForegroundColour(DiameterNormalColor);
    m_nozzle_index_label->SetBackgroundColour(ThemeColor::Grey200);
    main_sizer->Add(m_nozzle_index_label, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
}

// "Nozzle NO. <1-based positions matching the selected diameter>", or "No Available Nozzle" if none.
static wxString nozzle_index_text(const std::vector<NozzleConfig> &configs, const wxString &selected_diameter)
{
    wxString positions;
    for (int i = 0; i < static_cast<int>(configs.size()); ++i) {
        if (configs[i].diameter != selected_diameter) continue;
        if (!positions.IsEmpty()) positions += ", ";
        positions += wxString::Format("%d", i + 1);
    }
    return positions.IsEmpty() ? _L("No Available Nozzle") : _L("Nozzle NO. ") + positions;
}

// Multi-diameter mode is experimental and only enabled for single-extruder multi-nozzle
// printers, so the multi-tile projection lives here. Single-diameter mode defers to the base.
void SingleExtruderPanel::UpdateNozzles(const std::vector<NozzleConfig> &configs, const wxString &selected_diameter)
{
    if (!multi_nozzle_diameter_mode_enabled(wxGetApp().preset_bundle->project_config)) {
        // Single-diameter mode: the base builds the tile + warning; add the nozzle-index label on top.
        ExtruderPanel::UpdateNozzles(configs, selected_diameter);
        m_nozzle_index_label->SetLabel(nozzle_index_text(configs, selected_diameter));
        m_nozzle_index_label->Show(true);
        Layout();
        return;
    }

    // Multi-diameter mode: one tile per (diameter, volume) group; the index label is hidden.
    m_summary_sizer->Clear(true); // destroy the tiles from the previous projection
    m_summaries.clear();
    m_nozzle_index_label->Hide();

    std::vector<std::pair<std::pair<wxString, NozzleVolumeType>, int>> groups;
    for (const auto &c : configs) {
        if (c.diameter.IsEmpty()) continue;
        auto key = std::make_pair(c.diameter, c.volume);
        auto it  = std::find_if(groups.begin(), groups.end(), [&](const auto &e) { return e.first == key; });
        if (it == groups.end())
            groups.push_back({key, 1});
        else
            it->second++;
    }
    for (const auto &g : groups) add_summary_tile(g.first.first, volume_short(g.first.second), g.second, true);

    finish_projection(true); // every configured diameter is valid in multi mode
}

CustomSeparator::CustomSeparator(wxWindow *parent, bool is_small) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
{
    SetBackgroundColour(ThemeColor::Grey200);
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
    m_separator_line->SetBackgroundColour(ThemeColor::Grey400);
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

} // namespace Slic3r::GUI
