#include "CalibrationWizardPage.hpp"
#include "I18N.hpp"
#include "Widgets/Label.hpp"
#include "MsgDialog.hpp"

#include <wx/bmpbuttn.h>

#include "DeviceCore/DevFilaSystem.h"
#include "DeviceCore/DevNozzleSystem.h"
#include "CalibUtils.hpp"

namespace Slic3r { namespace GUI {

wxDEFINE_EVENT(EVT_CALI_ACTION, wxCommandEvent);
wxDEFINE_EVENT(EVT_CALI_TRAY_CHANGED, wxCommandEvent);
wxDEFINE_EVENT(EVT_SUBAMS_CLICK, wxCommandEvent);


CalibrationStyle get_cali_style(MachineObject* obj)
{
    if (!obj) return CalibrationStyle::CALI_STYLE_DEFAULT;

    if (obj->get_printer_series() == PrinterSeries::SERIES_X1)
        return CalibrationStyle::CALI_STYLE_X1;
    else if (obj->get_printer_series() == PrinterSeries::SERIES_P1P)
        return CalibrationStyle::CALI_STYLE_P1P;

    return CalibrationStyle::CALI_STYLE_DEFAULT;
}

wxString get_cali_mode_caption_string(CalibMode mode)
{
    if (mode == CalibMode::Calib_PA_Line)
        return _L("Flow Dynamics Calibration");
    if (mode == CalibMode::Calib_Flow_Rate)
        return _L("Flow Rate Calibration");
    if (mode == CalibMode::Calib_Vol_speed_Tower)
        return _L("Max Volumetric Speed Calibration");
    return "no cali_mode_caption";
}

wxString get_calibration_wiki_page(CalibMode cali_mode)
{
    std::string language = wxGetApp().app_config->get("language");
    wxString    region   = "en";
    if (language.find("zh") == 0)
        region = "zh";

    switch (cali_mode) {
    case CalibMode::Calib_PA_Line:
        return wxString::Format("https://wiki.bambulab.com/%s/software/bambu-studio/calibration_pa", region);
    case CalibMode::Calib_Flow_Rate:
        return wxString::Format("https://wiki.bambulab.com/%s/software/bambu-studio/calibration_flow_rate", region);
    case CalibMode::Calib_Vol_speed_Tower:
        return wxString::Format("https://wiki.bambulab.com/%s/software/bambu-studio/calibration_volumetric", region);
    case CalibMode::Calib_Temp_Tower:
        return wxString::Format("https://wiki.bambulab.com/%s/software/bambu-studio/calibration_temperature", region);
    case CalibMode::Calib_Retraction_tower:
        return wxString::Format("https://wiki.bambulab.com/%s/software/bambu-studio/calibration_retraction", region);
    default:
        return "";
    }
}

CalibrationFilamentMode get_cali_filament_mode(MachineObject* obj, CalibMode mode)
{
    // default
    if (!obj) return CalibrationFilamentMode::CALI_MODEL_SINGLE;


    if (mode == CalibMode::Calib_PA_Line) {
        if (obj->get_printer_series() == PrinterSeries::SERIES_X1 || obj->get_printer_arch() == PrinterArch::ARCH_I3)
            return CalibrationFilamentMode::CALI_MODEL_MULITI;
        else if (obj->get_printer_series() == PrinterSeries::SERIES_P1P)
            return CalibrationFilamentMode::CALI_MODEL_SINGLE;
    }
    else if (mode == CalibMode::Calib_Flow_Rate) {
        if (obj->get_printer_series() == PrinterSeries::SERIES_X1)
            return CalibrationFilamentMode::CALI_MODEL_SINGLE;
        else if (obj->get_printer_series() == PrinterSeries::SERIES_P1P)
            return CalibrationFilamentMode::CALI_MODEL_SINGLE;
    }

    return CalibrationFilamentMode::CALI_MODEL_SINGLE;
}

CalibMode get_obj_calibration_mode(const MachineObject* obj)
{
    CalibrationMethod method;
    int cali_stage;
    return get_obj_calibration_mode(obj, method, cali_stage);
}

CalibMode get_obj_calibration_mode(const MachineObject* obj, int& cali_stage)
{
    CalibrationMethod method;
    return get_obj_calibration_mode(obj, method, cali_stage);
}

CalibMode get_obj_calibration_mode(const MachineObject* obj, CalibrationMethod& method, int& cali_stage)
{
    method = CalibrationMethod::CALI_METHOD_MANUAL;

    if (!obj) return CalibMode::Calib_None;

    if (boost::contains(obj->m_gcode_file, "auto_filament_cali")) {
        method = CalibrationMethod::CALI_METHOD_AUTO;
        return CalibMode::Calib_PA_Line;
    }
    if (boost::contains(obj->m_gcode_file, "user_cali_manual_pa")) {
        method = CalibrationMethod::CALI_METHOD_MANUAL;
        return CalibMode::Calib_PA_Line;
    }
    if (boost::contains(obj->m_gcode_file, "extrusion_cali")) {
        method = CalibrationMethod::CALI_METHOD_MANUAL;
        return CalibMode::Calib_PA_Line;
    }

    if (boost::contains(obj->m_gcode_file, "abs_flowcalib_cali")) {
        method = CalibrationMethod::CALI_METHOD_AUTO;
        return CalibMode::Calib_Flow_Rate;
    }

    if (obj->get_printer_series() == PrinterSeries::SERIES_P1P) {
        if (boost::contains(obj->subtask_name, "auto_filament_cali")) {
            method = CalibrationMethod::CALI_METHOD_AUTO;
            return CalibMode::Calib_PA_Line;
        }
        if (boost::contains(obj->subtask_name, "user_cali_manual_pa")) {
            method = CalibrationMethod::CALI_METHOD_MANUAL;
            return CalibMode::Calib_PA_Line;
        }
        if (boost::contains(obj->subtask_name, "extrusion_cali")) {
            method = CalibrationMethod::CALI_METHOD_MANUAL;
            return CalibMode::Calib_PA_Line;
        }

        if (boost::contains(obj->subtask_name, "abs_flowcalib_cali")) {
            method = CalibrationMethod::CALI_METHOD_AUTO;
            return CalibMode::Calib_Flow_Rate;
        }
    }

    CalibMode cali_mode = CalibUtils::get_calib_mode_by_name(obj->subtask_name, cali_stage);
    if (cali_mode == CalibMode::Calib_PA_Line && cali_stage == 2) {
        method = CalibrationMethod::CALI_METHOD_NEW_AUTO;
    }
    else if (cali_mode != CalibMode::Calib_None) {
        method = CalibrationMethod::CALI_METHOD_MANUAL;
    }
    return cali_mode;
}


CaliPageButton::CaliPageButton(wxWindow* parent, CaliPageActionType type, wxString text)
    : m_action_type(type),
    Button(parent, text)
{
    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));

    StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal));

    StateColor btn_bd_green(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Enabled));

    StateColor btn_bd_white(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));

    StateColor btn_text_green(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Enabled));

    StateColor btn_text_white(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));

    switch (m_action_type)
    {
    case CaliPageActionType::CALI_ACTION_MANAGE_RESULT:
        this->SetLabel(_L("Manage Result"));
        break;
    case CaliPageActionType::CALI_ACTION_MANUAL_CALI:
        this->SetLabel(_L("Manual Calibration"));
        this->SetToolTip(_L("Result can be read by human eyes."));
        break;
    case CaliPageActionType::CALI_ACTION_AUTO_CALI:
        this->SetLabel(_L("Auto-Calibration"));
        this->SetToolTip(_L("The printer will automatically read the calibration results."));
        break;
    case CaliPageActionType::CALI_ACTION_START:
        this->SetLabel(_L("Start Calibration"));
        break;
    case CaliPageActionType::CALI_ACTION_PREV:
        this->SetLabel(_L("Prev"));
        break;
    case CaliPageActionType::CALI_ACTION_RECALI:
        this->SetLabel(_L("Recalibration"));
        break;
    case CaliPageActionType::CALI_ACTION_NEXT:
        this->SetLabel(_L("Next"));
        break;
    case CaliPageActionType::CALI_ACTION_CALI_NEXT:
        this->SetLabel(_L("Next"));
        break;
    case CaliPageActionType::CALI_ACTION_CALI:
        this->SetLabel(_L("Calibrate"));
        break;
    case CaliPageActionType::CALI_ACTION_FLOW_CALI_STAGE_2:
        this->SetLabel(_L("Calibrate"));
        break;
    case CaliPageActionType::CALI_ACTION_PA_SAVE:
        this->SetLabel(_L("Finish"));
        break;
    case CaliPageActionType::CALI_ACTION_FLOW_SAVE:
        this->SetLabel(_L("Finish"));
        break;
    case CaliPageActionType::CALI_ACTION_FLOW_COARSE_SAVE:
        this->SetLabel(_L("Finish"));
        break;
    case CaliPageActionType::CALI_ACTION_FLOW_FINE_SAVE:
        this->SetLabel(_L("Finish"));
        break;
    case CaliPageActionType::CALI_ACTION_COMMON_SAVE:
        this->SetLabel(_L("Finish"));
        break;
    default:
        this->SetLabel("Unknown");
        break;
    }

    switch (m_action_type)
    {
    case CaliPageActionType::CALI_ACTION_PREV:
    case CaliPageActionType::CALI_ACTION_RECALI:
        SetBackgroundColor(btn_bg_white);
        SetBorderColor(btn_bd_white);
        SetTextColor(btn_text_white);
        break;
    case CaliPageActionType::CALI_ACTION_START:
    case CaliPageActionType::CALI_ACTION_NEXT:
    case CaliPageActionType::CALI_ACTION_CALI:
    case CaliPageActionType::CALI_ACTION_CALI_NEXT:
    case CaliPageActionType::CALI_ACTION_FLOW_CALI_STAGE_2:
    case CaliPageActionType::CALI_ACTION_PA_SAVE:
    case CaliPageActionType::CALI_ACTION_FLOW_SAVE:
    case CaliPageActionType::CALI_ACTION_FLOW_COARSE_SAVE:
    case CaliPageActionType::CALI_ACTION_FLOW_FINE_SAVE:
    case CaliPageActionType::CALI_ACTION_COMMON_SAVE:
        SetBackgroundColor(btn_bg_green);
        SetBorderColor(btn_bd_green);
        SetTextColor(btn_text_green);
        break;
    default:
        break;
    }

    SetBackgroundColour(*wxWHITE);
    SetFont(Label::Body_13);
    SetMinSize(wxSize(-1, FromDIP(24)));
    SetCornerRadius(FromDIP(12));
}

void CaliPageButton::msw_rescale()
{
    SetMinSize(wxSize(-1, FromDIP(24)));
    SetCornerRadius(FromDIP(12));
    Rescale();
}


FilamentComboBox::FilamentComboBox(wxWindow* parent, const wxPoint& pos, const wxSize& size)
    : wxPanel(parent, wxID_ANY, pos, size, wxTAB_TRAVERSAL)
{
    SetBackgroundColour(*wxWHITE);

    wxBoxSizer* main_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_comboBox = new CalibrateFilamentComboBox(this);
    m_comboBox->SetSize(CALIBRATION_FILAMENT_COMBOX_SIZE);
    m_comboBox->SetMinSize(CALIBRATION_FILAMENT_COMBOX_SIZE);
    main_sizer->Add(m_comboBox->clr_picker, 0, wxALIGN_CENTER | wxRIGHT, FromDIP(8));
    main_sizer->Add(m_comboBox, 0, wxALIGN_CENTER);

    m_nozzle_bmp = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("cali_nozzle", this, 20), wxDefaultPosition, wxDefaultSize, 0);
    m_nozzle_bmp->Hide();
    main_sizer->Add(m_nozzle_bmp, 0, wxALIGN_CENTER | wxLEFT, FromDIP(18));

    m_nozzle_combo = new ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, CALIBRATION_FILAMENT_COMBOX_SIZE, 0, nullptr, wxCB_READONLY);
    m_nozzle_combo->SetSelection(0);
    m_nozzle_combo->SetSize(CALIBRATION_FILAMENT_COMBOX_SIZE);
    m_nozzle_combo->SetMinSize(CALIBRATION_FILAMENT_COMBOX_SIZE);
    m_nozzle_combo->Hide();
    main_sizer->Add(m_nozzle_combo, 0, wxALIGN_CENTER | wxLEFT, FromDIP(8));

    this->SetSizer(main_sizer);
    this->Layout();
    main_sizer->Fit(this);
}

void FilamentComboBox::UpdateNozzleCombo(const std::vector<std::pair<wxString, int>> &nozzle_list)
{
    m_nozzle_combo->Clear();

    for (auto &pair : nozzle_list) { m_nozzle_combo->Append(pair.first, wxNullBitmap, new int{pair.second}); }

    m_nozzle_combo->SetSelection(0);
}

int FilamentComboBox::GetNozzlePosId() const
{
    if (!m_nozzle_combo) return -1;
    auto sel = m_nozzle_combo->GetSelection();
    if (sel != wxNOT_FOUND) return *(reinterpret_cast<int*>(m_nozzle_combo->GetClientData(sel)));

    return -1;
}

void FilamentComboBox::ShowNozzleCombo()
{
    m_nozzle_bmp->Show();
    m_nozzle_combo->Show();
    Layout();
}

void FilamentComboBox::HideNozzleCombo()
{
    m_nozzle_bmp->Hide();
    m_nozzle_combo->Hide();
    Layout();
}

void FilamentComboBox::ShowPanel()
{
    this->Show();
    set_select_mode(m_mode);
}

void FilamentComboBox::HidePanel()
{
    this->Hide();
    m_radioBox->Hide();
    m_checkBox->Hide();
}

void FilamentComboBox::set_select_mode(CalibrationFilamentMode mode)
{
    m_mode = mode;
    if (m_checkBox)
        m_checkBox->Show(m_mode == CalibrationFilamentMode::CALI_MODEL_MULITI);
    if (m_radioBox)
        m_radioBox->Show(m_mode == CalibrationFilamentMode::CALI_MODEL_SINGLE);

    Layout();
}

void FilamentComboBox::load_tray_from_ams(int id, const DynamicPrintConfig& tray)
{
    m_comboBox->load_tray(tray);

    m_tray_id = id;
    m_tray_name = m_comboBox->get_tray_name();
    m_is_bbl_filamnet = DevFilaSystem::IsBBL_Filament(m_comboBox->get_tag_uid());
    Enable(m_comboBox->is_tray_exist());

    if (m_comboBox->is_tray_exist()) {
        if (!m_comboBox->is_compatible_with_printer()) {
            SetValue(false);
        }

        if (m_radioBox || m_checkBox) { Enable(m_comboBox->is_compatible_with_printer()); }
    }

    if (m_nozzle_combo->IsShown() && m_nozzle_combo->GetCount() == 0) { Enable(false); }

    // check compatibility
    wxCommandEvent event(EVT_CALI_TRAY_CHANGED);
    event.SetEventObject(GetParent());
    wxPostEvent(GetParent(), event);
}

SelectedFilamentInfo FilamentComboBox::get_selected_filament_info() const
{
    SelectedFilamentInfo info;
    info.tray_id         = m_tray_id;
    info.tray_name       = m_tray_name;
    info.is_bbl_filament = m_is_bbl_filamnet;
    info.extruder_role   = m_extuder_role;
    info.model_index     = m_index;
    info.extruder_id     = m_extuder_role == ExtruderRole::DEPUTY_EXTRUDER ? DEPUTY_EXTRUDER_ID : MAIN_EXTRUDER_ID;
    info.nozzle_pos_id   = GetNozzlePosId();
    if (info.nozzle_pos_id == -1) info.nozzle_pos_id = info.extruder_id;
    if (m_comboBox) {
        info.preset = const_cast<Preset*>(m_comboBox->get_selected_preset());
    }
    return info;
}

void FilamentComboBox::SetChipSize(const wxSize& size)
{
    if (m_comboBox) m_comboBox->SetChipSize(size);
}

void FilamentComboBox::update_from_preset()
{
    m_comboBox->update();
    m_nozzle_combo->SetSelection(0);
}

bool FilamentComboBox::Show(bool show)
{
    return wxPanel::Show(show);
}

bool FilamentComboBox::Enable(bool enable) {
    if (!enable)
        SetValue(false);

    if (m_radioBox)
        enable ? m_radioBox->Enable() : m_radioBox->Disable();
    if (m_checkBox)
        m_checkBox->Enable(enable);
    if (m_comboBox)
        m_comboBox->Enable(enable);
    if (m_nozzle_combo){
        m_nozzle_combo->Enable(enable);
        if (!enable) m_nozzle_combo->SetValue(wxEmptyString);
    }

    return wxPanel::Enable(enable);
}

void FilamentComboBox::SetValue(bool value, bool send_event) {
    if (m_radioBox) {
        if (value == m_radioBox->GetValue()) {
            if (m_checkBox) {
                if (value == m_checkBox->GetValue())
                    return;
            }
            else {
                return;
            }
        }
    }
    if (m_radioBox)
        m_radioBox->SetValue(value);
    if (m_checkBox)
        m_checkBox->SetValue(value);
}

void FilamentComboBox::msw_rescale()
{
    if (m_checkBox) m_checkBox->Rescale();
    if (m_radioBox) m_radioBox->Rescale();

    m_comboBox->SetSize(CALIBRATION_FILAMENT_COMBOX_SIZE);
    m_comboBox->SetMinSize(CALIBRATION_FILAMENT_COMBOX_SIZE);
    m_comboBox->msw_rescale();

    m_nozzle_bmp->SetBitmap(create_scaled_bitmap("cali_nozzle", this, 20));

    m_nozzle_combo->SetSize(CALIBRATION_FILAMENT_COMBOX_SIZE);
    m_nozzle_combo->SetMinSize(CALIBRATION_FILAMENT_COMBOX_SIZE);
    m_nozzle_combo->Rescale();
}

FilamentExtendComboBox::FilamentExtendComboBox(wxWindow* parent, int nozzle_id, const wxPoint& pos, const wxSize& size)
    : FilamentComboBox(parent, pos, size)
    , m_nozzle_id(nozzle_id)
{
    if (m_nozzle_bmp)   m_nozzle_bmp->Hide();
    if (m_nozzle_combo) m_nozzle_combo->Hide();
    if (m_comboBox)     m_comboBox->SetChipSize(CALIBRATION_FILAMENT_CHIP_SIZE);

    wxSizer* old_sizer = this->GetSizer();
    if (old_sizer) {
        old_sizer->Clear(false);
    }
    this->SetSizer(nullptr, true);

    wxBoxSizer* main_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_id_label = new Label(this, wxString::Format("%d", m_nozzle_id + 1));
    m_id_label->SetBackgroundColour(*wxWHITE);
    m_id_label->SetForegroundColour(wxColour("#7d7d7d"));
    main_sizer->Add(m_id_label, 0, wxALIGN_CENTER | wxRIGHT, CALIBRATION_NOZZLE_ITEM_GAP);

    m_nozzle_info_label = new Label(this, _L("Nozzle Type and Diameter"));
    m_nozzle_info_label->SetBackgroundColour(*wxWHITE);
    main_sizer->Add(m_nozzle_info_label, 0, wxALIGN_CENTER | wxRIGHT, CALIBRATION_NOZZLE_ITEM_GAP);

    m_diameter_combo = new ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, CALIBRATION_NOZZLE_COMBO_SIZE, 0, nullptr, wxCB_READONLY);
    m_diameter_combo->SetMinSize(CALIBRATION_NOZZLE_COMBO_SIZE);
    m_diameter_combo->Disable();
    main_sizer->Add(m_diameter_combo, 0, wxALIGN_CENTER | wxRIGHT, CALIBRATION_NOZZLE_ITEM_GAP);

    m_flow_type_combo = new ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, CALIBRATION_NOZZLE_COMBO_SIZE, 0, nullptr, wxCB_READONLY);
    m_flow_type_combo->SetMinSize(CALIBRATION_NOZZLE_COMBO_SIZE);
    m_flow_type_combo->Disable();
    main_sizer->Add(m_flow_type_combo, 0, wxALIGN_CENTER | wxRIGHT, CALIBRATION_NOZZLE_SECTION_GAP);

    if (m_comboBox) {
        if (m_comboBox->clr_picker)
            main_sizer->Add(m_comboBox->clr_picker, 0, wxALIGN_CENTER | wxRIGHT, CALIBRATION_NOZZLE_ITEM_GAP);
        main_sizer->Add(m_comboBox, 0, wxALIGN_CENTER);
    }

    // subams placeholder (replaces clr_picker + comboBox visually when active)
    const wxSize subams_panel_size(CALIBRATION_FILAMENT_CHIP_SIZE.x + FromDIP(8) + CALIBRATION_FILAMENT_COMBOX_SIZE.x, CALIBRATION_FILAMENT_COMBOX_SIZE.y);
    m_subams_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, subams_panel_size, wxBORDER_NONE);
    m_subams_panel->SetBackgroundColour(*wxWHITE);
    m_subams_panel->SetMinSize(subams_panel_size);
    m_subams_panel->Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
        wxPaintDC dc(m_subams_panel);
        const wxSize sz = m_subams_panel->GetClientSize();
        const wxColour border = (m_subams_enabled && m_subams_hover) ? wxColour(0x00, 0xAE, 0x42)
                                                                      : wxColour(0xDB, 0xDB, 0xDB);
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.SetPen(wxPen(border, 1));
        dc.DrawRectangle(0, 0, sz.x, sz.y);
    });
    m_subams_panel->Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent& evt) { SetSubAmsHover(true);  evt.Skip(); });
    m_subams_panel->Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& evt) { SetSubAmsHover(false); evt.Skip(); });
    m_subams_panel->Bind(wxEVT_LEFT_DOWN,    [this](wxMouseEvent& evt) { NotifySubAmsClick();   evt.Skip(); });
    auto* subams_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_subams_picker_sizer = new wxBoxSizer(wxHORIZONTAL);
    /* Owner-drawn Button instead of a native wxStaticText: its text is painted via dc.DrawText,
       so the disabled state renders crisply (grey) rather than MSW's blurry embossed static text. */
    m_subams_label = new Button(m_subams_panel, wxEmptyString, "", wxBORDER_NONE);
    m_subams_label->SetBackgroundColor(StateColor(*wxWHITE));
    m_subams_label->SetBorderWidth(0);
    m_subams_label->SetCornerRadius(0);
    m_subams_label->SetCanFocus(false);
    m_subams_label->SetPaddingSize(wxSize(0, 0));
    m_subams_label->SetTextColor(StateColor(
        std::make_pair(wxColour(0xAC, 0xAC, 0xAC), (int) StateColor::Disabled),
        std::make_pair(wxColour("#262E30"),        (int) StateColor::Normal)));
    /* Clicking the text opens the popup, same as clicking the panel; disabled Button won't fire.
       Hovering the centred label keeps the panel border highlighted; the panel's own leave clears it. */
    m_subams_label->Bind(wxEVT_BUTTON,       [this](wxCommandEvent&)  { NotifySubAmsClick(); });
    m_subams_label->Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent& evt) { SetSubAmsHover(true); evt.Skip(); });
    subams_sizer->AddStretchSpacer();
    subams_sizer->Add(m_subams_label, 0, wxALIGN_CENTER_VERTICAL);
    subams_sizer->Add(m_subams_picker_sizer, 0, wxALIGN_CENTER_VERTICAL);
    subams_sizer->AddStretchSpacer();
    m_subams_panel->SetSizer(subams_sizer);
    m_subams_panel->Hide();
    main_sizer->Add(m_subams_panel, 0, wxALIGN_CENTER);

    this->SetSizer(main_sizer);
    this->Layout();
    main_sizer->Fit(this);
}

void FilamentExtendComboBox::SetSubAmsMode(bool enable, const wxString& text)
{
    m_subams_mode = enable;
    if (enable) {
        if (m_comboBox) {
            if (m_comboBox->clr_picker) m_comboBox->clr_picker->Hide();
            m_comboBox->Hide();
        }
        RefreshSubAmsLabel(text);
        if (m_subams_panel) {
            m_subams_panel->Show();
            m_subams_panel->Layout();
        }
    } else {
        if (m_subams_panel) m_subams_panel->Hide();
        if (m_comboBox) {
            if (m_comboBox->clr_picker) m_comboBox->clr_picker->Show();
            m_comboBox->Show();
        }
    }
    SyncSelectionEnableState();
    Layout();
    if (auto* sizer = GetSizer()) sizer->Fit(this);
}

void FilamentExtendComboBox::RefreshSubAmsLabel(const wxString& empty_fallback)
{
    if (!m_subams_label || !m_subams_picker_sizer) return;

    m_subams_picker_sizer->Clear(false);
    for (auto* picker : m_subams_picker_buttons) {
        if (picker) picker->Show(false);
    }
    if (m_subams_ellipsis_picker) m_subams_ellipsis_picker->Show(false);

    if (m_subams_selection.empty()) {
        m_subams_label->SetLabel(empty_fallback);
        m_subams_label->Show(true);
        if (m_subams_panel) m_subams_panel->Layout();
        return;
    }

    m_subams_label->Show(false);

    const int picker_w   = CALIBRATION_FILAMENT_CHIP_SIZE.x;
    const int picker_h   = CALIBRATION_FILAMENT_CHIP_SIZE.y;
    const int picker_gap = FromDIP(4);
    int available_width = m_subams_panel ? m_subams_panel->GetClientSize().GetWidth() : 0;
    if (available_width <= 0 && m_subams_panel) available_width = m_subams_panel->GetMinSize().GetWidth();
    if (available_width <= 0) available_width = picker_w;

    int max_pickers = std::max(1, (available_width + picker_gap) / (picker_w + picker_gap));
    const bool overflow = static_cast<int>(m_subams_selection.size()) > max_pickers;
    int visible_count = overflow ? std::max(0, max_pickers - 1) : max_pickers;

    auto forward_click = [this](wxWindow* window) {
        window->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) { NotifySubAmsClick(); evt.Skip(); });
    };

    auto ensure_picker = [&](size_t index) -> wxBitmapButton* {
        while (m_subams_picker_buttons.size() <= index) {
            auto* picker = new wxBitmapButton(m_subams_panel, wxID_ANY, wxNullBitmap, wxDefaultPosition,
                                              wxSize(picker_w, picker_h),
                                              wxBU_EXACTFIT | wxBU_AUTODRAW | wxBORDER_NONE);
            picker->SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
            forward_click(picker);
            m_subams_picker_buttons.push_back(picker);
        }
        return m_subams_picker_buttons[index];
    };

    int added = 0;
    for (const auto& [tray_id, info] : m_subams_selection) {
        if (added >= visible_count) break;

        auto* picker = ensure_picker(added);
        picker->SetMinSize(wxSize(picker_w, picker_h));
        picker->SetSize(wxSize(picker_w, picker_h));
        if (auto it = m_subams_picker_chips.find(tray_id); it != m_subams_picker_chips.end()) {
            if (wxBitmap* bmp = get_extruder_color_icon(it->second.first, it->second.second, picker_w, picker_h)) {
                picker->SetBitmap(*bmp);
                picker->SetBitmapDisabled(*bmp);
            }
        }
        picker->Show(true);
        m_subams_picker_sizer->Add(picker, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, picker_gap);
        ++added;
    }

    if (overflow) {
        if (!m_subams_ellipsis_picker) {
            m_subams_ellipsis_picker = new wxButton(m_subams_panel, wxID_ANY, "...", wxDefaultPosition,
                                                    wxSize(picker_w, picker_h),
                                                    wxBU_EXACTFIT | wxBU_AUTODRAW | wxBORDER_NONE);
            m_subams_ellipsis_picker->SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
            forward_click(m_subams_ellipsis_picker);
        }
        m_subams_ellipsis_picker->SetMinSize(wxSize(picker_w, picker_h));
        m_subams_ellipsis_picker->SetSize(wxSize(picker_w, picker_h));
        m_subams_ellipsis_picker->Show(true);
        m_subams_picker_sizer->Add(m_subams_ellipsis_picker, 0, wxALIGN_CENTER_VERTICAL);
    }

    if (m_subams_panel) m_subams_panel->Layout();
}

bool FilamentExtendComboBox::GetSubAmsSelected(int tray_id) const
{
    return m_subams_selection.find(tray_id) != m_subams_selection.end();
}

void FilamentExtendComboBox::SetSubAmsSelected(int tray_id, const SelectedFilamentInfo& info, const CalibrateFilamentComboBox* chip_src)
{
    m_subams_selection[tray_id] = info;
    if (chip_src)
        m_subams_picker_chips[tray_id] = {chip_src->get_chip_color(), chip_src->get_tray_name()};
    SyncSelectionEnableState();
}

void FilamentExtendComboBox::ClearSubAmsSelected(int tray_id)
{
    m_subams_selection.erase(tray_id);
    m_subams_picker_chips.erase(tray_id);
    SyncSelectionEnableState();
}

void FilamentExtendComboBox::ClearSubAmsSelection(bool refresh)
{
    m_subams_selection.clear();
    m_subams_picker_chips.clear();
    SyncSelectionEnableState();
    if (refresh)
        RefreshSubAmsLabel(_L("Not Selected"));
}

void FilamentExtendComboBox::init_diameter_combo(MachineObject* obj, const NozzleVolumeType& volume_type)
{
    if (!m_diameter_combo || !obj) return;
    m_diameter_combo->Clear();

    auto supported_volumes_and_diameters = CalibUtils::get_supported_nozzle_volume_and_diameters(obj);
    if (supported_volumes_and_diameters.find(volume_type) != supported_volumes_and_diameters.end()) {
        for (const auto& diameter_type : supported_volumes_and_diameters[volume_type]) {
            m_diameter_combo->Append(wxString::Format("%1.1f mm", DevNozzle::ToNozzleDiameterFloat(diameter_type)), wxNullBitmap, new int{diameter_type});
        }
    }

    m_diameter_combo->SetSelection(-1);
}

void FilamentExtendComboBox::init_flow_type_combo(MachineObject* obj)
{
    if (!m_flow_type_combo || !obj) return;
    m_flow_type_combo->Clear();

    auto supported_volumes_and_diameters = CalibUtils::get_supported_nozzle_volume_and_diameters(obj);
    for (const auto& [vol, _diameters] : supported_volumes_and_diameters) {
        m_flow_type_combo->Append(DevNozzle::GetNozzleVolumeTypeStr(vol), wxNullBitmap, new int{DevNozzle::ToNozzleFlowType(vol)});
    }

    m_flow_type_combo->SetSelection(-1);
}

void FilamentExtendComboBox::SetNozzleInfo(const DevNozzle& nozzle, MachineObject* obj)
{
    m_nozzle_id     = nozzle.GetNozzleId();
    m_nozzle_pos_id = nozzle.GetNozzlePosId();
    // If the nozzle is on the extruder, we need to adjust the nozzle position ID by adding 0x10 to it.
    if (!nozzle.IsOnRack() && nozzle.GetAddrId()) {
        m_nozzle_pos_id = nozzle.GetAddrId().value() + 0x10;
    }

    if (m_id_label && nozzle.GetAddrId().has_value())
        m_id_label->SetLabel(wxString::Format("%d", nozzle.GetAddrId().value() + 1));

    const bool nozzle_info_available = (nozzle.IsExist() || nozzle.IsOnRack()) && nozzle.IsNormal() && nozzle.IsHotend();

    /* SetLabel, not SetSelection(-1): Clear() left selection at -1, so SetSelection won't refresh. */
    init_flow_type_combo(obj);
    if (m_flow_type_combo) {
        if (nozzle_info_available)
            switch_combox_to_target(m_flow_type_combo, nozzle.GetNozzleFlowType());
        else
            m_flow_type_combo->SetLabel(wxEmptyString);
    }

    init_diameter_combo(obj, DevNozzle::ToNozzleVolumeType(nozzle.GetNozzleFlowType()));
    if (m_diameter_combo) {
        if (nozzle_info_available)
            switch_combox_to_target(m_diameter_combo, nozzle.GetNozzleDiameterType());
        else
            m_diameter_combo->SetLabel(wxEmptyString);
    }
}

NozzleDiameterType FilamentExtendComboBox::GetSelectedDiameterType() const
{
    return get_combox_selected_value<NozzleDiameterType>(m_diameter_combo, NONE_DIAMETER_TYPE);
}

NozzleFlowType FilamentExtendComboBox::GetSelectedFlowType() const
{
    return get_combox_selected_value<NozzleFlowType>(m_flow_type_combo, NONE_FLOWTYPE);
}

void FilamentExtendComboBox::NotifySubAmsClick()
{
    if (!m_subams_panel || !m_subams_enabled) return;
    wxCommandEvent event(EVT_SUBAMS_CLICK);
    event.SetEventObject(this);
    wxPostEvent(this, event);
}

void FilamentExtendComboBox::SetSubAmsHover(bool hover)
{
    if (!m_subams_enabled) hover = false;
    if (m_subams_hover == hover) return;
    m_subams_hover = hover;
    if (m_subams_panel) m_subams_panel->Refresh();
}

void FilamentExtendComboBox::SyncSelectionEnableState()
{
    const bool can_select = m_nozzle_selectable && (!m_subams_mode || !m_subams_selection.empty());

    if (!can_select) SetValue(false);
    if (m_radioBox) can_select ? m_radioBox->Enable() : m_radioBox->Disable();
    if (m_checkBox) m_checkBox->Enable(can_select);
}

bool FilamentExtendComboBox::Enable(bool enable)
{
    /* Disable interactive children individually instead of disabling the whole panel. */
    m_nozzle_selectable = enable;
    SyncSelectionEnableState();

    if (m_comboBox)  m_comboBox->Enable(enable);
    if (m_nozzle_combo) {
        m_nozzle_combo->Enable(enable);
        if (!enable) m_nozzle_combo->SetValue(wxEmptyString);
    }

    /* m_diameter_combo / m_flow_type_combo are display-only and stay disabled. */
    if (m_subams_mode && m_subams_panel) {
        m_subams_enabled = enable;
        if (m_subams_label) m_subams_label->Enable(enable);
        if (!enable) SetSubAmsHover(false);
        m_subams_panel->Refresh();
    }
    return true;
}

void FilamentExtendComboBox::msw_rescale()
{
    FilamentComboBox::msw_rescale();
    // m_chip_size is stored in pixels, so the base rescale reuses the old-DPI chip.
    if (m_comboBox) m_comboBox->SetChipSize(CALIBRATION_FILAMENT_CHIP_SIZE);

    if (m_diameter_combo) {
        m_diameter_combo->SetSize(CALIBRATION_NOZZLE_COMBO_SIZE);
        m_diameter_combo->SetMinSize(CALIBRATION_NOZZLE_COMBO_SIZE);
        m_diameter_combo->Rescale();
    }
    if (m_flow_type_combo) {
        m_flow_type_combo->SetSize(CALIBRATION_NOZZLE_COMBO_SIZE);
        m_flow_type_combo->SetMinSize(CALIBRATION_NOZZLE_COMBO_SIZE);
        m_flow_type_combo->Rescale();
    }

    /* A sizer border is resolved to pixels in Add(), so it still holds the old-DPI value;
       re-apply the same macros the constructor used. */
    if (wxSizer* sizer = GetSizer()) {
        auto set_border = [sizer](wxWindow* window, int border) {
            if (!window) return;
            if (wxSizerItem* item = sizer->GetItem(window))
                item->SetBorder(border);
        };
        set_border(m_id_label, CALIBRATION_NOZZLE_ITEM_GAP);
        set_border(m_nozzle_info_label, CALIBRATION_NOZZLE_ITEM_GAP);
        set_border(m_diameter_combo, CALIBRATION_NOZZLE_ITEM_GAP);
        set_border(m_flow_type_combo, CALIBRATION_NOZZLE_SECTION_GAP);
        set_border(m_comboBox ? m_comboBox->clr_picker : nullptr, CALIBRATION_NOZZLE_ITEM_GAP);
    }

    if (m_subams_panel) {
        const wxSize subams_panel_size(CALIBRATION_FILAMENT_CHIP_SIZE.x + FromDIP(8) + CALIBRATION_FILAMENT_COMBOX_SIZE.x, CALIBRATION_FILAMENT_COMBOX_SIZE.y);
        m_subams_panel->SetMinSize(subams_panel_size);
        m_subams_panel->SetSize(subams_panel_size);
        if (m_subams_label) m_subams_label->Rescale();
        RefreshSubAmsLabel(_L("Not Selected"));
        m_subams_panel->Refresh();
    }

    Layout();
    if (auto* sizer = GetSizer()) sizer->Fit(this);
}

CaliPageCaption::CaliPageCaption(wxWindow* parent, CalibMode cali_mode,
    wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : wxPanel(parent, id, pos, size, style)
{
    init_bitmaps();

    SetBackgroundColour(*wxWHITE);

    auto top_sizer = new wxBoxSizer(wxVERTICAL);
    auto caption_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_prev_btn = new ScalableButton(this, wxID_ANY, "cali_page_caption_prev",
        wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, true, 30);
    m_prev_btn->SetBackgroundColour(*wxWHITE);
    caption_sizer->Add(m_prev_btn, 0, wxALIGN_CENTER | wxRIGHT, FromDIP(10));

    wxString title = get_cali_mode_caption_string(cali_mode);
    Label* title_text = new Label(this, title);
    title_text->SetFont(Label::Head_20);
    title_text->Wrap(-1);
    caption_sizer->Add(title_text, 0, wxALIGN_CENTER | wxRIGHT, FromDIP(10));

    m_help_btn = new ScalableButton(this, wxID_ANY, "cali_page_caption_help",
        wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, true, 30);
    m_help_btn->Hide();
    m_help_btn->SetBackgroundColour(*wxWHITE);
    caption_sizer->Add(m_help_btn, 0, wxALIGN_CENTER);

    caption_sizer->AddStretchSpacer();

    m_wiki_url = get_calibration_wiki_page(cali_mode);
    create_wiki(this);
    caption_sizer->Add(m_wiki_text, 0);

    top_sizer->Add(caption_sizer, 1, wxEXPAND);
    top_sizer->AddSpacer(FromDIP(35));
    this->SetSizer(top_sizer);
    top_sizer->Fit(this);

    // hover effect
    //m_prev_btn->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {
    //    m_prev_btn->SetBitmap(m_prev_bmp_hover.bmp());
    //});

    //m_prev_btn->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) {
    //    m_prev_btn->SetBitmap(m_prev_bmp_normal.bmp());
    //});

    // hover effect
    //m_help_btn->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {
    //    m_help_btn->SetBitmap(m_help_bmp_hover.bmp());
    //    });

    //m_help_btn->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) {
    //    m_help_btn->SetBitmap(m_help_bmp_normal.bmp());
    //    });

    // send event
    m_prev_btn->Bind(wxEVT_BUTTON, [this](auto& e) {
        wxCommandEvent event(EVT_CALI_ACTION);
        event.SetEventObject(m_parent);
        event.SetInt((int)(CaliPageActionType::CALI_ACTION_GO_HOME));
        wxPostEvent(m_parent, event);
        });

#ifdef __linux__
    wxGetApp().CallAfter([this, title_text]() {
        title_text->SetMinSize(title_text->GetSize() + wxSize{ FromDIP(150), title_text->GetCharHeight() / 2 });
        Layout();
        Fit();
        });
#endif
}

void CaliPageCaption::init_bitmaps() {
    m_prev_bmp_normal = ScalableBitmap(this, "cali_page_caption_prev", 30);
    m_prev_bmp_hover = ScalableBitmap(this, "cali_page_caption_prev_hover", 30);
    m_help_bmp_normal = ScalableBitmap(this, "cali_page_caption_help", 30);
    m_help_bmp_hover = ScalableBitmap(this, "cali_page_caption_help_hover", 30);
}

void CaliPageCaption::create_wiki(wxWindow* parent)
{
    m_wiki_text = new Label(parent, _L("Wiki"));
    m_wiki_text->SetFont(Label::Head_14);
    m_wiki_text->SetForegroundColour({ 0, 88, 220 });
    m_wiki_text->Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent& e) {
        e.Skip();
        SetCursor(wxCURSOR_HAND);
        });
    m_wiki_text->Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& e) {
        e.Skip();
        SetCursor(wxCURSOR_ARROW);
        });
    m_wiki_text->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& e) {
        if (!m_wiki_url.empty())
            wxLaunchDefaultBrowser(m_wiki_url);
        });
}

void CaliPageCaption::show_prev_btn(bool show)
{
    m_prev_btn->Show(show);
}

void CaliPageCaption::show_help_icon(bool show)
{
    //m_help_btn->Show(show);
    m_help_btn->Hide();
}

void CaliPageCaption::on_sys_color_changed()
{
    m_prev_btn->msw_rescale();
}

void CaliPageCaption::msw_rescale()
{
    m_prev_btn->msw_rescale();
}

CaliPageStepGuide::CaliPageStepGuide(wxWindow* parent, wxArrayString steps,
    wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : wxPanel(parent, id, pos, size, style),
    m_steps(steps)
{
    SetBackgroundColour(*wxWHITE);

    auto top_sizer = new wxBoxSizer(wxVERTICAL);

    m_step_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_step_sizer->AddSpacer(FromDIP(90));
    for (int i = 0; i < m_steps.size(); i++) {
        Label* step_text = new Label(this, m_steps[i]);
        step_text->SetForegroundColour(wxColour(206, 206, 206));
        m_text_steps.push_back(step_text);
        m_step_sizer->Add(step_text, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT, FromDIP(15));
        if (i != m_steps.size() - 1) {
            auto line = new wxPanel(this, wxID_ANY, wxDefaultPosition);
            line->SetBackgroundColour(*wxBLACK);
            m_step_sizer->Add(line, 1, wxALIGN_CENTER);
        }
    }
    m_step_sizer->AddSpacer(FromDIP(90));

    top_sizer->Add(m_step_sizer, 0, wxEXPAND);
    top_sizer->AddSpacer(FromDIP(30));
    this->SetSizer(top_sizer);
    top_sizer->Fit(this);

    wxGetApp().UpdateDarkUIWin(this);
}

void CaliPageStepGuide::set_steps(int index)
{
    for (Label* text_step : m_text_steps) {
        text_step->SetForegroundColour(wxColour(206, 206, 206));
    }
    m_text_steps[index]->SetForegroundColour(*wxBLACK);

    wxGetApp().UpdateDarkUIWin(this);
}

void CaliPageStepGuide::set_steps_string(wxArrayString steps)
{
    if (m_steps == steps) return;

    m_steps.Clear();
    m_text_steps.clear();
    m_step_sizer->Clear(true);
    m_steps = steps;
    m_step_sizer->AddSpacer(FromDIP(90));
    for (int i = 0; i < m_steps.size(); i++) {
        Label* step_text = new Label(this, m_steps[i]);
        step_text->SetForegroundColour(wxColour(206, 206, 206));
        m_text_steps.push_back(step_text);
        m_step_sizer->Add(step_text, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT, FromDIP(15));
        if (i != m_steps.size() - 1) {
            auto line = new wxPanel(this, wxID_ANY, wxDefaultPosition);
            line->SetBackgroundColour(*wxBLACK);
            m_step_sizer->Add(line, 1, wxALIGN_CENTER);
        }
    }
    m_step_sizer->AddSpacer(FromDIP(90));

    wxGetApp().UpdateDarkUIWin(this);

    Layout();
}


CaliPagePicture::CaliPagePicture(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : wxPanel(parent, id, pos, size, style)
{
    SetBackgroundColour(wxColour(206, 206, 206));
    auto top_sizer = new wxBoxSizer(wxHORIZONTAL);
    top_sizer->AddStretchSpacer();
    m_img = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap);
    top_sizer->Add(m_img);
    top_sizer->AddStretchSpacer();
    this->SetSizer(top_sizer);
    top_sizer->Fit(this);
}

void CaliPagePicture::set_bmp(const ScalableBitmap& bmp)
{
    m_bmp = bmp;
    m_img->SetBitmap(m_bmp.bmp());
}

void CaliPagePicture::msw_rescale()
{
    m_bmp.msw_rescale();
    m_img->SetBitmap(m_bmp.bmp());
}


PAPageHelpPanel::PAPageHelpPanel(wxWindow* parent, bool ground_panel, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : wxPanel(parent, id, pos, size, style)
{
    if (ground_panel)
        SetBackgroundColour(wxColour(238, 238, 238));
    else
        SetBackgroundColour(parent->GetBackgroundColour());
    int left_align_padding = ground_panel ? FromDIP(20) : 0;

    wxBoxSizer* top_sizer = new wxBoxSizer(wxVERTICAL);
    top_sizer->AddSpacer(FromDIP(10));

    auto help_text_title = new Label(this, _L("How to use calibration result?"));
    help_text_title->SetFont(Label::Head_14);
    top_sizer->Add(help_text_title, 0, wxLEFT | wxRIGHT, left_align_padding);

    wxBoxSizer* help_text_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto help_text = new Label(this, _L("You could change the Flow Dynamics Calibration Factor in material editing"));
    help_text->SetFont(Label::Body_14);
    m_help_btn = new ScalableButton(this, wxID_ANY, "cali_page_caption_help", wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, false, 24);
    m_help_btn->SetBackgroundColour(m_help_btn->GetParent()->GetBackgroundColour());
    help_text_sizer->Add(help_text, 0, wxALIGN_CENTER | wxLEFT, left_align_padding);
    help_text_sizer->Add(m_help_btn, 0, wxALIGN_CENTER | wxLEFT, FromDIP(8));
    help_text_sizer->AddSpacer(FromDIP(20));

    top_sizer->Add(help_text_sizer);

    top_sizer->AddSpacer(FromDIP(6));

    create_pop_window();

    SetSizer(top_sizer);
    top_sizer->Fit(this);
}

void PAPageHelpPanel::msw_rescale()
{
    m_help_btn->msw_rescale();
    m_bmp.msw_rescale();
    m_img->SetBitmap(m_bmp.bmp());
}

void PAPageHelpPanel::create_pop_window()
{
    m_pop_win = new PopupWindow(this);
    m_pop_win->SetBackgroundColour(*wxWHITE);
    wxBoxSizer* pop_sizer = new wxBoxSizer(wxVERTICAL);
    m_pop_win->SetSizer(pop_sizer);

    m_img = new wxStaticBitmap(m_pop_win, wxID_ANY, wxNullBitmap);
    m_bmp = ScalableBitmap(this, "cali_fdc_editing_diagram", 206);
    m_img->SetBitmap(m_bmp.bmp());
    pop_sizer->Add(m_img, 1, wxEXPAND | wxALL, FromDIP(20));

    m_pop_win->Layout();
    m_pop_win->Fit();

    m_pop_win->Bind(wxEVT_PAINT, [this](auto&) {
        wxPaintDC dc(m_pop_win);
        dc.SetPen({ 0xACACAC });
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRectangle({ 0, 0 }, m_pop_win->GetSize());
        });

    m_help_btn->Bind(wxEVT_ENTER_WINDOW, [this](auto&) {
        wxPoint pop_pos = m_help_btn->ClientToScreen(wxPoint(0, 0));
        pop_pos.x -= FromDIP(60);
        pop_pos.y -= m_pop_win->GetSize().y + FromDIP(10);

        m_pop_win->Position(pop_pos, wxSize(0, 0));
        m_pop_win->Popup();
        });
    m_help_btn->Bind(wxEVT_LEAVE_WINDOW, [this](auto&) {
        m_pop_win->Dismiss();
        });
}


CaliPageActionPanel::CaliPageActionPanel(wxWindow* parent,
    CalibMode cali_mode,
    CaliPageType page_type,
    wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : wxPanel(parent, id, pos, size, style)
{
    m_parent = parent;

    wxWindow* btn_parent = this;

    if (cali_mode == CalibMode::Calib_PA_Line) {
        if (page_type == CaliPageType::CALI_PAGE_START) {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_MANAGE_RESULT));
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_MANUAL_CALI));
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_AUTO_CALI));
        }
        else if (page_type == CaliPageType::CALI_PAGE_PRESET) {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_CALI));
        }
        else if (page_type == CaliPageType::CALI_PAGE_CALI) {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_CALI_NEXT));
        }
        else if (page_type == CaliPageType::CALI_PAGE_PA_SAVE) {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_PA_SAVE));
        }
    }
    else if (cali_mode == CalibMode::Calib_Flow_Rate) {
        if (page_type == CaliPageType::CALI_PAGE_START) {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_MANUAL_CALI));
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_AUTO_CALI));
        }
        else if (page_type == CaliPageType::CALI_PAGE_PRESET) {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_CALI));
        }
        else if (page_type == CaliPageType::CALI_PAGE_CALI) {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_CALI_NEXT));
        }
        else if (page_type == CaliPageType::CALI_PAGE_COARSE_SAVE) {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_FLOW_COARSE_SAVE));
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_FLOW_CALI_STAGE_2));
        }
        else if (page_type == CaliPageType::CALI_PAGE_FINE_SAVE) {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_FLOW_FINE_SAVE));
        }
        else if (page_type == CaliPageType::CALI_PAGE_FLOW_SAVE) {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_FLOW_SAVE));
        }
    }
    else {
        if (page_type == CaliPageType::CALI_PAGE_START) {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_START));
        }
        else if (page_type == CaliPageType::CALI_PAGE_PRESET) {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_CALI));
        }
        else if (page_type == CaliPageType::CALI_PAGE_CALI) {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_CALI_NEXT));
        }
        else if (page_type == CaliPageType::CALI_PAGE_COMMON_SAVE) {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_COMMON_SAVE));
        }
        else {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_PREV));
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_NEXT));
        }

    }

    auto top_sizer = new wxBoxSizer(wxHORIZONTAL);

    top_sizer->Add(0, 0, 1, wxEXPAND, 0);
    for (int i = 0; i < m_action_btns.size(); i++) {
        top_sizer->Add(m_action_btns[i], 0, wxALL, FromDIP(5));

        m_action_btns[i]->Bind(wxEVT_BUTTON,
            [this, i](wxCommandEvent& evt) {
                wxCommandEvent event(EVT_CALI_ACTION);
                event.SetEventObject(m_parent);
                event.SetInt((int)m_action_btns[i]->get_action_type());
                wxPostEvent(m_parent, event);
            });
    }
    top_sizer->Add(0, 0, 1, wxEXPAND, 0);

    this->SetSizer(top_sizer);
    top_sizer->Fit(this);
}

void CaliPageActionPanel::bind_button(CaliPageActionType action_type, bool is_block)
{
    for (int i = 0; i < m_action_btns.size(); i++) {
        if (m_action_btns[i]->get_action_type() == action_type) {

            if (is_block) {
                m_action_btns[i]->Bind(wxEVT_BUTTON,
                    [this](wxCommandEvent& evt) {
                        MessageDialog msg(nullptr, _L("The current firmware version of the printer does not support calibration.\nPlease upgrade the printer firmware."), _L("Calibration not supported"), wxOK | wxICON_WARNING);
                        msg.ShowModal();
                    });
            }
            else {
                m_action_btns[i]->Bind(wxEVT_BUTTON,
                    [this, i](wxCommandEvent& evt) {
                        wxCommandEvent event(EVT_CALI_ACTION);
                        event.SetEventObject(m_parent);
                        event.SetInt((int)m_action_btns[i]->get_action_type());
                        wxPostEvent(m_parent, event);
                    });
            }
        }
    }

}

void CaliPageActionPanel::show_button(CaliPageActionType action_type, bool show)
{
    for (int i = 0; i < m_action_btns.size(); i++) {
        if (m_action_btns[i]->get_action_type() == action_type) {
            m_action_btns[i]->Show(show);
        }
    }
    Layout();
}

void CaliPageActionPanel::enable_button(CaliPageActionType action_type, bool enable)
{
    for (int i = 0; i < m_action_btns.size(); i++) {
        if (m_action_btns[i]->get_action_type() == action_type) {
            m_action_btns[i]->Enable(enable);
        }
    }
}

void CaliPageActionPanel::msw_rescale()
{
    for (int i = 0; i < m_action_btns.size(); i++) {
        m_action_btns[i]->msw_rescale();
    }
}

CaliPageSendingPanel::CaliPageSendingPanel(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : wxPanel(parent, id, pos, size, style)
{
    SetBackgroundColour(*wxWHITE);
    SetMinSize({ FromDIP(475), FromDIP(200) });
    SetMaxSize({ FromDIP(475), FromDIP(200) });

    create(this);

    Layout();
    Fit();

    Bind(EVT_SHOW_ERROR_INFO_SEND, [this](auto& e) {
        show_send_failed_info(true);
        });
}

void CaliPageSendingPanel::create(wxWindow* parent)
{
    auto panel_sizer = new wxBoxSizer(wxVERTICAL);
    parent->SetSizer(panel_sizer);

    m_send_progress_bar = std::shared_ptr<BBLStatusBarSend>(new BBLStatusBarSend(parent));
    panel_sizer->Add(m_send_progress_bar->get_panel(), 0, wxEXPAND);

    m_sw_print_failed_info = new wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(380), FromDIP(125)), wxVSCROLL);
    m_sw_print_failed_info->SetBackgroundColour(*wxWHITE);
    m_sw_print_failed_info->SetScrollRate(0, 5);
    m_sw_print_failed_info->SetMinSize(wxSize(FromDIP(380), FromDIP(125)));
    m_sw_print_failed_info->SetMaxSize(wxSize(FromDIP(380), FromDIP(125)));

    m_sw_print_failed_info->Hide();

    panel_sizer->Add(m_sw_print_failed_info, 0, wxEXPAND);

    // create error info panel
    wxBoxSizer* sizer_print_failed_info = new wxBoxSizer(wxVERTICAL);
    m_sw_print_failed_info->SetSizer(sizer_print_failed_info);

    wxBoxSizer* sizer_error_code = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer* sizer_error_desc = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer* sizer_extra_info = new wxBoxSizer(wxHORIZONTAL);

    auto st_title_error_code = new Label(m_sw_print_failed_info, _L("Error code"));
    auto st_title_error_code_doc = new Label(m_sw_print_failed_info, ": ");
    m_st_txt_error_code = new Label(m_sw_print_failed_info, wxEmptyString);
    st_title_error_code->SetForegroundColour(0x909090);
    st_title_error_code_doc->SetForegroundColour(0x909090);
    m_st_txt_error_code->SetForegroundColour(0x909090);
    st_title_error_code->SetFont(::Label::Body_13);
    st_title_error_code_doc->SetFont(::Label::Body_13);
    m_st_txt_error_code->SetFont(::Label::Body_13);
    st_title_error_code->SetMinSize(wxSize(FromDIP(74), -1));
    st_title_error_code->SetMaxSize(wxSize(FromDIP(74), -1));
    m_st_txt_error_code->SetMinSize(wxSize(FromDIP(260), -1));
    m_st_txt_error_code->SetMaxSize(wxSize(FromDIP(260), -1));
    sizer_error_code->Add(st_title_error_code, 0, wxALL, 0);
    sizer_error_code->Add(st_title_error_code_doc, 0, wxALL, 0);
    sizer_error_code->Add(m_st_txt_error_code, 0, wxALL, 0);

    auto st_title_error_desc = new Label(m_sw_print_failed_info, _L("Error desc"));
    auto st_title_error_desc_doc = new Label(m_sw_print_failed_info, ": ");
    m_st_txt_error_desc = new Label(m_sw_print_failed_info, wxEmptyString);
    st_title_error_desc->SetForegroundColour(0x909090);
    st_title_error_desc_doc->SetForegroundColour(0x909090);
    m_st_txt_error_desc->SetForegroundColour(0x909090);
    st_title_error_desc->SetFont(::Label::Body_13);
    st_title_error_desc_doc->SetFont(::Label::Body_13);
    m_st_txt_error_desc->SetFont(::Label::Body_13);
    st_title_error_desc->SetMinSize(wxSize(FromDIP(74), -1));
    st_title_error_desc->SetMaxSize(wxSize(FromDIP(74), -1));
    m_st_txt_error_desc->SetMinSize(wxSize(FromDIP(260), -1));
    m_st_txt_error_desc->SetMaxSize(wxSize(FromDIP(260), -1));
    sizer_error_desc->Add(st_title_error_desc, 0, wxALL, 0);
    sizer_error_desc->Add(st_title_error_desc_doc, 0, wxALL, 0);
    sizer_error_desc->Add(m_st_txt_error_desc, 0, wxALL, 0);

    auto st_title_extra_info = new Label(m_sw_print_failed_info, _L("Extra info"));
    auto st_title_extra_info_doc = new Label(m_sw_print_failed_info, ": ");
    m_st_txt_extra_info = new Label(m_sw_print_failed_info, wxEmptyString);
    st_title_extra_info->SetForegroundColour(0x909090);
    st_title_extra_info_doc->SetForegroundColour(0x909090);
    m_st_txt_extra_info->SetForegroundColour(0x909090);
    st_title_extra_info->SetFont(::Label::Body_13);
    st_title_extra_info_doc->SetFont(::Label::Body_13);
    m_st_txt_extra_info->SetFont(::Label::Body_13);
    st_title_extra_info->SetMinSize(wxSize(FromDIP(74), -1));
    st_title_extra_info->SetMaxSize(wxSize(FromDIP(74), -1));
    m_st_txt_extra_info->SetMinSize(wxSize(FromDIP(260), -1));
    m_st_txt_extra_info->SetMaxSize(wxSize(FromDIP(260), -1));
    sizer_extra_info->Add(st_title_extra_info, 0, wxALL, 0);
    sizer_extra_info->Add(st_title_extra_info_doc, 0, wxALL, 0);
    sizer_extra_info->Add(m_st_txt_extra_info, 0, wxALL, 0);

    sizer_print_failed_info->Add(sizer_error_code, 0, wxLEFT, 5);
    sizer_print_failed_info->Add(0, 0, 0, wxTOP, FromDIP(3));
    sizer_print_failed_info->Add(sizer_error_desc, 0, wxLEFT, 5);
    sizer_print_failed_info->Add(0, 0, 0, wxTOP, FromDIP(3));
    sizer_print_failed_info->Add(sizer_extra_info, 0, wxLEFT, 5);
}

void CaliPageSendingPanel::update_print_error_info(int code, const std::string& msg, const std::string& extra)
{
    m_print_error_code = code;
    m_print_error_msg = msg;
    m_print_error_extra = extra;
}

void CaliPageSendingPanel::show_send_failed_info(bool show, int code, wxString description, wxString extra)
{
    if (show) {
        if (!m_sw_print_failed_info->IsShown()) {
            m_sw_print_failed_info->Show(true);

            m_st_txt_error_code->SetLabelText(wxString::Format("%d", m_print_error_code));
            m_st_txt_error_desc->SetLabelText(wxGetApp().filter_string(m_print_error_msg));
            m_st_txt_extra_info->SetLabelText(wxGetApp().filter_string(m_print_error_extra));

            m_st_txt_error_code->Wrap(FromDIP(260));
            m_st_txt_error_desc->Wrap(FromDIP(260));
            m_st_txt_extra_info->Wrap(FromDIP(260));
        }
        else {
            m_sw_print_failed_info->Show(false);
        }
        Layout();
        Fit();
    }
    else {
        if (!m_sw_print_failed_info->IsShown()) { return; }
        m_sw_print_failed_info->Show(false);
        m_st_txt_error_code->SetLabelText(wxEmptyString);
        m_st_txt_error_desc->SetLabelText(wxEmptyString);
        m_st_txt_extra_info->SetLabelText(wxEmptyString);
        Layout();
        Fit();
    }
}

std::shared_ptr<BBLStatusBarSend> CaliPageSendingPanel::get_sending_progress_bar()
{
    return m_send_progress_bar;
}

void CaliPageSendingPanel::reset()
{
    m_send_progress_bar->reset();
    m_sw_print_failed_info->Show(false);
}

CalibrationWizardPage::CalibrationWizardPage(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : wxPanel(parent, id, pos, size, style)
    , m_parent(parent)
{
    SetBackgroundColour(*wxWHITE);
    SetMinSize({ MIN_CALIBRATION_PAGE_WIDTH, -1 });
}

void CalibrationWizardPage::msw_rescale()
{
    m_page_caption->msw_rescale();
    m_action_panel->msw_rescale();
}

void CalibrationWizardPage::on_sys_color_changed()
{
    m_page_caption->on_sys_color_changed();
}

}
}
