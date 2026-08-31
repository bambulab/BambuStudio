#include "AMSRFIDMaterialView.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MsgDialog.hpp"

#include "DeviceCore/DevConfig.h"
#include "DeviceCore/DevExtruderSystem.h"
#include "DeviceCore/DevFilaSystem.h"

namespace Slic3r { namespace GUI {

AMSRFIDMaterialView::AMSRFIDMaterialView(wxWindow* parent, wxWindowID id)
    : AMSPaEditBase(parent, id, _L("AMS Materials Setting"),
                      wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    create();
    wxGetApp().UpdateDlgDarkUI(this);
}

bool AMSRFIDMaterialView::should_show_kn_section() const
{
    if (!obj) return false;
    if (ams_id == VIRTUAL_TRAY_MAIN_ID || ams_id == VIRTUAL_TRAY_DEPUTY_ID) return true;
    if (obj->ams_support_virtual_tray || (obj->GetCalib() && obj->GetCalib()->IsVersionInited())) return true;
    return false;
}

void AMSRFIDMaterialView::create()
{
    SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));

    auto* sizer_main = new wxBoxSizer(wxVERTICAL);

    // Top card: colour swatch + brand / colour-name
    auto* panel_top = new wxPanel(this, wxID_ANY);
    panel_top->SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
    auto* sizer_top = new wxBoxSizer(wxHORIZONTAL);

    m_clr_picker = new ColorPicker(panel_top, wxID_ANY);
    m_clr_picker->set_show_full(true);
    m_clr_picker->SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
    m_clr_picker->SetMinSize(FromDIP(wxSize(48, 48)));
    m_clr_picker->SetMaxSize(FromDIP(wxSize(48, 48)));
    m_clr_picker->SetSize(FromDIP(wxSize(48, 48)));
    // Read-only: no click binding
    sizer_top->Add(m_clr_picker, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));

    auto* sizer_names = new wxBoxSizer(wxVERTICAL);
    m_lbl_brand = new wxStaticText(panel_top, wxID_ANY, wxEmptyString);
    m_lbl_brand->SetFont(Label::Body_14);
    m_lbl_brand->SetForegroundColour(AMS_MATERIALS_SETTING_GREY900);

    m_lbl_color_name = new wxStaticText(panel_top, wxID_ANY, wxEmptyString);
    m_lbl_color_name->SetFont(Label::Body_13);
    m_lbl_color_name->SetForegroundColour(AMS_MATERIALS_SETTING_GREY700);

    sizer_names->Add(m_lbl_brand,      0, wxBOTTOM, FromDIP(2));
    sizer_names->Add(m_lbl_color_name, 0, 0, 0);
    sizer_top->Add(sizer_names, 1, wxALIGN_CENTER_VERTICAL, 0);

    panel_top->SetSizer(sizer_top);

    // Middle info panel: temp + SN
    m_panel_info = new StaticBox(this);
    m_panel_info->SetCornerRadius(FromDIP(10));
    m_panel_info->SetBackgroundColor(StateColor(std::make_pair(wxColour(248, 248, 248), (int)StateColor::Normal)));
    m_panel_info->SetBorderColor(StateColor(std::make_pair(wxColour(248, 248, 248), (int)StateColor::Normal)));
    m_panel_info->SetMinSize(wxSize(AMS_MATERIALS_SETTING_BODY_WIDTH, -1));
    auto* sizer_info = new wxBoxSizer(wxVERTICAL);

    m_lbl_temp = new wxStaticText(m_panel_info, wxID_ANY, wxEmptyString);
    m_lbl_temp->SetFont(Label::Body_13);
    m_lbl_temp->SetForegroundColour(AMS_MATERIALS_SETTING_GREY700);
    m_lbl_temp->SetBackgroundColour(StateColor::darkModeColorFor(wxColour(248, 248, 248)));

    m_panel_sn = new wxPanel(m_panel_info, wxID_ANY);
    m_panel_sn->SetBackgroundColour(StateColor::darkModeColorFor(wxColour(248, 248, 248)));
    auto* sizer_sn = new wxBoxSizer(wxHORIZONTAL);
    auto* lbl_sn_title = new wxStaticText(m_panel_sn, wxID_ANY, _L("SN") + ": ");
    lbl_sn_title->SetFont(Label::Body_13);
    lbl_sn_title->SetForegroundColour(AMS_MATERIALS_SETTING_GREY700);
    m_lbl_sn = new wxStaticText(m_panel_sn, wxID_ANY, wxEmptyString);
    m_lbl_sn->SetFont(Label::Body_13);
    m_lbl_sn->SetForegroundColour(AMS_MATERIALS_SETTING_GREY700);
    sizer_sn->Add(lbl_sn_title, 0, wxALIGN_CENTER_VERTICAL, 0);
    sizer_sn->Add(m_lbl_sn,     0, wxALIGN_CENTER_VERTICAL, 0);
    m_panel_sn->SetSizer(sizer_sn);

    sizer_info->Add(0, 0, 0, wxTOP, FromDIP(14));
    sizer_info->Add(m_lbl_temp, 0, wxLEFT | wxRIGHT, FromDIP(16));
    sizer_info->Add(0, 0, 0, wxTOP, FromDIP(8));
    sizer_info->Add(m_panel_sn, 0, wxLEFT | wxRIGHT, FromDIP(16));
    sizer_info->Add(0, 0, 0, wxTOP, FromDIP(14));
    m_panel_info->SetSizer(sizer_info);

    // Flow dynamics calibration section
    m_panel_kn = new wxPanel(this, wxID_ANY);
    m_panel_kn->SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));

    std::string language = wxGetApp().app_config->get("language");
    wxString region = "en";
    if (language.find("zh") == 0) region = "zh";
    wxString link_url = wxString::Format("https://wiki.bambulab.com/%s/software/bambu-studio/calibration_pa", region);

    // Left column: title + wiki link
    auto* lbl_kn_title = new wxStaticText(m_panel_kn, wxID_ANY, _L("Factors of Flow Dynamics Calibration"));
    lbl_kn_title->SetFont(Label::Head_14);
    lbl_kn_title->SetForegroundColour(wxColour(50, 58, 61));
    lbl_kn_title->Wrap(FromDIP(160));

    bool is_zh = (region == "zh");
    wxString wiki_label = is_zh ? wxString::FromUTF8("\xe8\xaf\xa6\xe6\x83\x85\xe6\x9f\xa5\xe7\x9c\x8bwiki") : _L("Click to learn more");
    wxColour wiki_colour = wxColour("#00AE42");
    auto* wiki_ctrl = new Label(m_panel_kn, wiki_label);
    wiki_ctrl->SetFont(Label::Body_13);
    wiki_ctrl->SetForegroundColour(wiki_colour);
    wiki_ctrl->Bind(wxEVT_ENTER_WINDOW, [wiki_ctrl](wxMouseEvent &e) { e.Skip(); wiki_ctrl->SetCursor(wxCURSOR_HAND); });
    wiki_ctrl->Bind(wxEVT_LEAVE_WINDOW, [wiki_ctrl](wxMouseEvent &e) { e.Skip(); wiki_ctrl->SetCursor(wxCURSOR_ARROW); });
    wiki_ctrl->Bind(wxEVT_LEFT_UP, [link_url](wxMouseEvent &) { wxLaunchDefaultBrowser(link_url); });

    auto* sizer_left = new wxBoxSizer(wxVERTICAL);
    sizer_left->Add(lbl_kn_title, 0, wxBOTTOM, FromDIP(4));
    sizer_left->Add(wiki_ctrl,    0, 0, 0);

    // Right column: PA Profile row + Factor K row
    auto* lbl_pa_title = new wxStaticText(m_panel_kn, wxID_ANY, _L("PA Profile"),
        wxDefaultPosition, wxSize(AMS_MATERIALS_SETTING_LABEL_WIDTH, -1));
    lbl_pa_title->SetFont(Label::Body_13);
    lbl_pa_title->SetForegroundColour(AMS_MATERIALS_SETTING_GREY800);
    lbl_pa_title->SetMinSize(wxSize(FromDIP(80), -1));
    lbl_pa_title->SetMaxSize(wxSize(FromDIP(80), -1));

    // Assign to base-class pointer so PA logic can operate on it
    m_comboBox_cali_result = new ComboBox(m_panel_kn, wxID_ANY, wxEmptyString,
        wxDefaultPosition, AMS_MATERIALS_SETTING_COMBOX_WIDTH, 0, nullptr, wxCB_READONLY);
    m_comboBox_cali_result->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent& evt){ on_select_cali_result(evt); });

    auto* sizer_pa = new wxBoxSizer(wxHORIZONTAL);
    sizer_pa->Add(lbl_pa_title,          0, wxALIGN_CENTER_VERTICAL, 0);
    sizer_pa->Add(m_comboBox_cali_result, 1, wxALIGN_CENTER_VERTICAL, 0);

    auto* lbl_k_title = new wxStaticText(m_panel_kn, wxID_ANY, _L("Factor K"),
        wxDefaultPosition, wxDefaultSize);
    lbl_k_title->SetFont(Label::Body_13);
    lbl_k_title->SetForegroundColour(wxColour(50, 58, 61));
    lbl_k_title->SetMinSize(wxSize(FromDIP(80), -1));
    lbl_k_title->SetMaxSize(wxSize(FromDIP(80), -1));

    // Assign to base-class pointer
    m_input_k_val = new TextInput(m_panel_kn, wxEmptyString, wxEmptyString, wxEmptyString,
        wxDefaultPosition, wxDefaultSize, wxTE_CENTRE | wxTE_PROCESS_ENTER);
    m_input_k_val->SetMinSize(wxSize(FromDIP(245), -1));
    m_input_k_val->SetMaxSize(wxSize(FromDIP(245), -1));
    m_input_k_val->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));

    auto* sizer_k = new wxBoxSizer(wxHORIZONTAL);
    sizer_k->Add(lbl_k_title,   0, wxALIGN_CENTER_VERTICAL, 0);
    sizer_k->Add(m_input_k_val, 1, wxALIGN_CENTER_VERTICAL, 0);

    auto* sizer_right = new wxBoxSizer(wxVERTICAL);
    sizer_right->Add(sizer_pa, 0, wxEXPAND | wxBOTTOM, FromDIP(10));
    sizer_right->Add(sizer_k,  0, wxEXPAND, 0);

    auto* sizer_kn = new wxBoxSizer(wxHORIZONTAL);
    sizer_kn->Add(sizer_left,  0, wxALIGN_TOP | wxRIGHT, FromDIP(20));
    sizer_kn->Add(sizer_right, 1, wxALIGN_TOP, 0);

    auto* sizer_kn_outer = new wxBoxSizer(wxVERTICAL);
    sizer_kn_outer->Add(0, 0, 0, wxTOP, FromDIP(14));
    sizer_kn_outer->Add(sizer_kn, 0, wxLEFT | wxRIGHT | wxEXPAND, FromDIP(24));
    sizer_kn_outer->Add(0, 0, 0, wxTOP, FromDIP(14));
    m_panel_kn->SetSizer(sizer_kn_outer);

    // Bottom buttons: [Reset]  [Confirm]
    auto* sizer_btn = new wxBoxSizer(wxHORIZONTAL);
    sizer_btn->Add(0, 0, 1, wxEXPAND, 0);

    m_button_reset = new Button(this, _L("Reset"));
    m_btn_bg_gray = StateColor(
        std::pair<wxColour, int>(AMS_MATERIALS_SETTING_GREY700, StateColor::Pressed),
        std::pair<wxColour, int>(AMS_MATERIALS_SETTING_GREY200, StateColor::Hovered),
        std::pair<wxColour, int>(AMS_MATERIALS_SETTING_GREY200, StateColor::Normal));
    m_button_reset->SetBackgroundColor(m_btn_bg_gray);
    m_button_reset->SetBorderColor(AMS_MATERIALS_SETTING_GREY900);
    m_button_reset->SetTextColor(AMS_MATERIALS_SETTING_GREY900);
    m_button_reset->SetMinSize(AMS_MATERIALS_SETTING_BUTTON_SIZE);
    m_button_reset->SetCornerRadius(FromDIP(12));
    m_button_reset->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_reset(); });
    sizer_btn->Add(m_button_reset, 0, wxALIGN_CENTER | wxRIGHT, FromDIP(20));

    m_button_confirm = new Button(this, _L("Confirm"));
    m_btn_bg_green = StateColor(
        std::pair<wxColour, int>(wxColour(27,  136, 68),  StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(61,  203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0,   174, 66),  StateColor::Normal));
    m_button_confirm->SetBackgroundColor(m_btn_bg_green);
    m_button_confirm->SetBorderColor(wxColour(0, 174, 66));
    m_button_confirm->SetTextColor(wxColour("#FFFFFF"));
    m_button_confirm->SetMinSize(AMS_MATERIALS_SETTING_BUTTON_SIZE);
    m_button_confirm->SetCornerRadius(FromDIP(12));
    m_button_confirm->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_confirm(); });
    sizer_btn->Add(m_button_confirm, 0, wxALIGN_CENTER, 0);

    // Assemble main sizer
    sizer_main->Add(0, 0, 0, wxTOP, FromDIP(20));
    sizer_main->Add(panel_top,    0, wxLEFT | wxRIGHT | wxEXPAND, FromDIP(24));
    sizer_main->Add(0, 0, 0, wxTOP, FromDIP(20));
    sizer_main->Add(m_panel_info, 0, wxLEFT | wxRIGHT | wxEXPAND, FromDIP(24));
    sizer_main->Add(0, 0, 0, wxTOP, FromDIP(16));
    sizer_main->Add(m_panel_kn,   0, wxEXPAND, 0);
    sizer_main->Add(sizer_btn,    0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(24));
    sizer_main->Add(0, 0, 0, wxTOP, FromDIP(16));

    SetSizer(sizer_main);
    Layout();
    apply_fixed_size();
}

void AMSRFIDMaterialView::apply_fixed_size()
{
    SetMinSize(wxSize(FromDIP(526), -1));
    SetMaxSize(wxSize(FromDIP(526), -1));
    Fit();
}

void AMSRFIDMaterialView::Popup(MachineObject* obj_, int ams_id_, int slot_id_,
                                 const wxString& brand_name, const wxString& color_name,
                                 const wxColour& color, const std::vector<wxColour>& cols, int ctype,
                                 const wxString& temp_min, const wxString& temp_max,
                                 const wxString& sn, const wxString& k_val)
{
    obj      = obj_;
    ams_id   = ams_id_;
    slot_id  = slot_id_;

    // resolve filament id from tray for PA profile filtering
    ams_filament_id.clear();
    if (obj) {
        if (auto* tray = obj->GetFilaSystem()->GetAmsTray(std::to_string(ams_id), std::to_string(slot_id)))
            ams_filament_id = tray->setting_id;
        else if (!obj->vt_slot.empty() && (ams_id == VIRTUAL_TRAY_MAIN_ID || ams_id == VIRTUAL_TRAY_DEPUTY_ID)) {
            int vt_idx = (ams_id == VIRTUAL_TRAY_DEPUTY_ID && obj->vt_slot.size() > 1) ? DEPUTY_EXTRUDER_ID : MAIN_EXTRUDER_ID;
            ams_filament_id = obj->vt_slot[vt_idx].setting_id;
        }
    }

    // colour swatch
    m_clr_picker->ctype = ctype;
    if (!cols.empty())
        m_clr_picker->set_colors(cols);
    else
        m_clr_picker->set_color(color);

    // brand / colour name
    m_lbl_brand->SetLabel(brand_name.IsEmpty() ? _L("Unknown") : brand_name);
    m_lbl_color_name->SetLabel(color_name);

    // nozzle temperature
    if (!temp_min.IsEmpty() && !temp_max.IsEmpty()) {
        m_lbl_temp->SetLabel(
            _L("Nozzle Temperature") + ": " + temp_min + "~" + temp_max + wxString::FromUTF8(" \xe2\x84\x83"));
    } else {
        m_lbl_temp->SetLabel(_L("Nozzle Temperature") + ": --");
    }

    // SN row
    if (!sn.empty()) {
        m_lbl_sn->SetLabel(sn);
        m_panel_sn->Show();
    } else {
        m_panel_sn->Hide();
    }

    // flow dynamics section
    if (should_show_kn_section()) {
        if (obj && obj->GetCalib()->IsVersionInited()) {
            // Set pending flag before populating — PA history may not be ready yet
            m_pa_data_pending = !obj->GetCalib()->IsPAHistoryReady();
            update_pa_profile_items();
            // Restore tray's current cali_idx selection
            int cur_cali_idx = -1;
            if (!obj->vt_slot.empty() && (ams_id == VIRTUAL_TRAY_MAIN_ID || ams_id == VIRTUAL_TRAY_DEPUTY_ID)) {
                int vt_idx = (ams_id == VIRTUAL_TRAY_DEPUTY_ID && obj->vt_slot.size() > 1) ? DEPUTY_EXTRUDER_ID : MAIN_EXTRUDER_ID;
                cur_cali_idx = obj->vt_slot[vt_idx].cali_idx;
            } else if (auto* tray = obj->GetFilaSystem()->GetAmsTray(std::to_string(ams_id), std::to_string(slot_id)))
                cur_cali_idx = tray->cali_idx;
            int sel = CalibUtils::get_selected_calib_idx(m_pa_profile_items, cur_cali_idx);
            if (sel < 0) sel = 0;
            m_comboBox_cali_result->SetSelection(sel);
            float k = m_pa_profile_items.empty() ? -1.0f : m_pa_profile_items[sel].k_value;
            if (m_input_k_val)
                m_input_k_val->GetTextCtrl()->SetValue(k >= 0.0f ? wxString::Format("%.3f", k) : wxString());
        } else {
            // Old firmware: only Default entry, show tray k directly
            m_pa_data_pending = false;
            m_comboBox_cali_result->Clear();
            m_comboBox_cali_result->Append(_L("Default"));
            m_comboBox_cali_result->SetSelection(0);
            if (m_input_k_val)
                m_input_k_val->GetTextCtrl()->SetValue(k_val.IsEmpty() ? "0.000" : k_val);
            update_kval_editability();
        }
        m_panel_kn->Show();
    } else {
        m_panel_kn->Hide();
    }

    Layout();
    apply_fixed_size();
    // Re-apply dark UI at show time: the native title bar's dark attribute set in
    // the ctor doesn't stick until the window is shown (mirrors AMSMaterialsSetting::Show).
    wxGetApp().UpdateDlgDarkUI(this);
    ShowModal();
}

void AMSRFIDMaterialView::on_confirm()
{
    save_pa_profile_selection();
    EndModal(wxID_OK);
}

void AMSRFIDMaterialView::on_reset()
{
    MessageDialog msg_dlg(nullptr,
        _L("Are you sure you want to clear the filament information?"),
        wxEmptyString, wxICON_WARNING | wxOK | wxCANCEL);
    if (msg_dlg.ShowModal() != wxID_OK) return;

    reset_calibration(ams_filament_id);

    if (should_show_kn_section() && m_comboBox_cali_result) {
        int sel = CalibUtils::get_selected_calib_idx(m_pa_profile_items, -1);
        if (sel < 0) sel = 0;
        m_comboBox_cali_result->SetSelection(sel);
        float k = m_pa_profile_items.empty() ? -1.0f : m_pa_profile_items[sel].k_value;
        if (m_input_k_val)
            m_input_k_val->GetTextCtrl()->SetValue(k >= 0.0f ? wxString::Format("%.3f", k) : wxString());
    }
}

void AMSRFIDMaterialView::on_pa_history_ready()
{
    if (!obj) return;
    update_pa_profile_items();
    int cur_cali_idx = -1;
    if (!obj->vt_slot.empty() && (ams_id == VIRTUAL_TRAY_MAIN_ID || ams_id == VIRTUAL_TRAY_DEPUTY_ID)) {
        int vt_idx = (ams_id == VIRTUAL_TRAY_DEPUTY_ID && obj->vt_slot.size() > 1) ? DEPUTY_EXTRUDER_ID : MAIN_EXTRUDER_ID;
        cur_cali_idx = obj->vt_slot[vt_idx].cali_idx;
    } else if (auto* tray = obj->GetFilaSystem()->GetAmsTray(std::to_string(ams_id), std::to_string(slot_id)))
        cur_cali_idx = tray->cali_idx;
    int sel = CalibUtils::get_selected_calib_idx(m_pa_profile_items, cur_cali_idx);
    if (sel < 0) sel = 0;
    m_comboBox_cali_result->SetSelection(sel);
    float k = m_pa_profile_items.empty() ? -1.0f : m_pa_profile_items[sel].k_value;
    if (m_input_k_val)
        m_input_k_val->GetTextCtrl()->SetValue(k >= 0.0f ? wxString::Format("%.3f", k) : wxString());
}

void AMSRFIDMaterialView::on_dpi_changed(const wxRect& /*suggested_rect*/)
{
    m_button_reset->SetMinSize(AMS_MATERIALS_SETTING_BUTTON_SIZE);
    m_button_confirm->SetMinSize(AMS_MATERIALS_SETTING_BUTTON_SIZE);
    m_clr_picker->SetMinSize(FromDIP(wxSize(48, 48)));
    m_clr_picker->SetMaxSize(FromDIP(wxSize(48, 48)));
    m_clr_picker->SetSize(FromDIP(wxSize(48, 48)));
    apply_fixed_size();
}

}} // namespace Slic3r::GUI
