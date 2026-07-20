#include "AMSMaterialsSetting.hpp"
#include "ExtrusionCalibration.hpp"
#include "MsgDialog.hpp"
#include "GUI_App.hpp"
#include "libslic3r/Preset.hpp"
#include "I18N.hpp"
#include <boost/log/trivial.hpp>
#include <iterator>
#include <wx/colordlg.h>
#include <wx/dcgraph.h>
#include "CalibUtils.hpp"
#include "../Utils/ColorSpaceConvert.hpp"
#include "EncodedFilament.hpp"


#include "DeviceCore/DevConfig.h"
#include "DeviceCore/DevConfigUtil.h"
#include "DeviceCore/DevExtruderSystem.h"
#include "DeviceCore/DevFilaBlackList.h"
#include "DeviceCore/DevFilaSystem.h"

#include "fila_manager/wgtFilaManagerStore.h"
#include "fila_manager/wgtFilaManagerCloudDispatcher.h"
#include "Widgets/Label.hpp"

#include <wx/dcmemory.h>
#include <wx/graphics.h>
#include <algorithm>
#include <cmath>

#define FILAMENT_MAX_TEMP       300
#define FILAMENT_MIN_TEMP       120

namespace Slic3r { namespace GUI {

wxDEFINE_EVENT(EVT_SELECTED_COLOR, wxCommandEvent);

static std::string float_to_string_with_precision(float value, int precision = 3)
{
    if (value < 0)
        return std::string();

    std::stringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
}

static std::string colour_to_ams_string(const wxColour& color)
{
    char col_buf[10];
    sprintf(col_buf, "%02X%02X%02X%02X", (int)color.Red(), (int)color.Green(), (int)color.Blue(), (int)color.Alpha());
    return col_buf;
}

static int filament_color_type_to_ams_ctype(FilamentColor::ColorType type, size_t color_count)
{
    if (color_count < 2) return 2;
    return type == FilamentColor::ColorType::GRADIENT_CLR ? 0 : 1;
}

static wxColour mix_colour(const wxColour& left, const wxColour& right, double ratio)
{
    ratio = std::max(0.0, std::min(1.0, ratio));
    auto mix_channel = [ratio](unsigned char l, unsigned char r) {
        return static_cast<unsigned char>(std::round(l + (r - l) * ratio));
    };
    return wxColour(mix_channel(left.Red(), right.Red()),
                    mix_channel(left.Green(), right.Green()),
                    mix_channel(left.Blue(), right.Blue()),
                    mix_channel(left.Alpha(), right.Alpha()));
}

static bool is_light_colour_for_border(const wxColour& c)
{
    return c.Red() > 224 && c.Green() > 224 && c.Blue() > 224;
}

static bool is_dark_colour_for_border(const wxColour& c)
{
    return c.Red() < 45 && c.Green() < 45 && c.Blue() < 45;
}

static std::vector<ColorPickerPopup::ColorItem> collect_ams_color_items(DevFilaSystem* fila_system)
{
    std::vector<ColorPickerPopup::ColorItem> items;
    if (!fila_system) return items;

    for (const auto& ams_pair : fila_system->GetAmsList()) {
        DevAms* ams = ams_pair.second;
        if (!ams) continue;

        for (const auto& tray_pair : ams->GetTrays()) {
            DevAmsTray* tray = tray_pair.second;
            if (!tray || !tray->is_tray_info_ready()) continue;

            ColorPickerPopup::ColorItem item;
            for (const std::string& col : tray->cols) {
                item.colors.emplace_back(DevAmsTray::decode_color(col));
            }
            if (item.colors.empty() && !tray->color.empty()) {
                item.colors.emplace_back(DevAmsTray::decode_color(tray->color));
            }
            if (item.colors.empty()) continue;

            item.ctype = item.colors.size() > 1 ? static_cast<int>(tray->ctype) : 2;
            items.emplace_back(std::move(item));
        }
    }

    return items;
}

AMSMaterialsSetting::AMSMaterialsSetting(wxWindow *parent, wxWindowID id)
    : DPIDialog(parent, id, _L("AMS Materials Setting"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
    , m_color_picker_popup(ColorPickerPopup(this))
{
    create();
    wxGetApp().UpdateDlgDarkUI(this);
}

void AMSMaterialsSetting::create()
{
    SetBackgroundColour(*wxWHITE);
    wxBoxSizer *m_sizer_main = new wxBoxSizer(wxVERTICAL);

    m_panel_normal = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    create_panel_normal(m_panel_normal);
    m_panel_kn = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    create_panel_kn(m_panel_kn);

    wxBoxSizer *m_sizer_button = new wxBoxSizer(wxHORIZONTAL);

    m_sizer_button->Add(0, 0, 1, wxEXPAND, 0);

    m_button_confirm = new Button(this, _L("Confirm"));
    m_btn_bg_green   = StateColor(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
                            std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
    m_button_confirm->SetBackgroundColor(m_btn_bg_green);
    m_button_confirm->SetBorderColor(wxColour(0, 174, 66));
    m_button_confirm->SetTextColor(wxColour("#FFFFFE"));
    m_button_confirm->SetMinSize(AMS_MATERIALS_SETTING_BUTTON_SIZE);
    m_button_confirm->SetCornerRadius(FromDIP(12));
    m_button_confirm->Bind(wxEVT_BUTTON, &AMSMaterialsSetting::on_select_ok, this);

    m_button_reset = new Button(this, _L("Reset"));
    m_btn_bg_gray = StateColor(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed), std::pair<wxColour, int>(*wxWHITE, StateColor::Focused),
        std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
        std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));
    m_button_reset->SetBackgroundColor(m_btn_bg_gray);
    m_button_reset->SetBorderColor(AMS_MATERIALS_SETTING_GREY900);
    m_button_reset->SetTextColor(AMS_MATERIALS_SETTING_GREY900);
    m_button_reset->SetMinSize(AMS_MATERIALS_SETTING_BUTTON_SIZE);
    m_button_reset->SetCornerRadius(FromDIP(12));
    m_button_reset->Bind(wxEVT_BUTTON, &AMSMaterialsSetting::on_select_reset, this);

    m_button_close = new Button(this, _L("Close"));
    m_button_close->SetBackgroundColor(m_btn_bg_gray);
    m_button_close->SetBorderColor(AMS_MATERIALS_SETTING_GREY900);
    m_button_close->SetTextColor(AMS_MATERIALS_SETTING_GREY900);
    m_button_close->SetMinSize(AMS_MATERIALS_SETTING_BUTTON_SIZE);
    m_button_close->SetCornerRadius(FromDIP(12));
    m_button_close->Bind(wxEVT_BUTTON, &AMSMaterialsSetting::on_select_close, this);

    m_sizer_button->Add(m_button_confirm, 0, wxALIGN_CENTER | wxRIGHT, FromDIP(20));
    m_sizer_button->Add(m_button_reset, 0, wxALIGN_CENTER | wxRIGHT, FromDIP(20));
    m_sizer_button->Add(m_button_close, 0, wxALIGN_CENTER, 0);

    m_sizer_main->Add(m_panel_normal, 0, wxALL, FromDIP(2));

    m_sizer_main->Add(m_panel_kn, 0, wxALL, FromDIP(2));

    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(24));
    m_sizer_main->Add(m_sizer_button, 0,  wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));
    m_sizer_main->Add(0, 0, 0,  wxTOP, FromDIP(16));

    SetSizer(m_sizer_main);
    Layout();
    Fit();

    m_input_nozzle_min->GetTextCtrl()->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& e) {
        warning_text->Hide();
        Layout();
        Fit();
        e.Skip();
        });
    m_input_nozzle_min->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent& e) {
        input_min_finish();
        e.Skip();
        });
    m_input_nozzle_min->GetTextCtrl()->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) {
        input_min_finish();
        e.Skip();
        });

    m_input_nozzle_max->GetTextCtrl()->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& e) {
        warning_text->Hide();
        Layout();
        Fit();
        e.Skip();
        });
    m_input_nozzle_max->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent& e) {
        input_max_finish();
        e.Skip();
        });
    m_input_nozzle_max->GetTextCtrl()->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) {
        input_max_finish();
        e.Skip();
        });

    Bind(wxEVT_PAINT, &AMSMaterialsSetting::paintEvent, this);
    Bind(EVT_SELECTED_COLOR, &AMSMaterialsSetting::on_picker_color, this);
     m_comboBox_filament->Connect(wxEVT_COMMAND_COMBOBOX_SELECTED, wxCommandEventHandler(AMSMaterialsSetting::on_select_filament), NULL, this);

    m_comboBox_cali_result->Connect(wxEVT_COMMAND_COMBOBOX_SELECTED, wxCommandEventHandler(AMSMaterialsSetting::on_select_cali_result), NULL, this);
}

void AMSMaterialsSetting::create_panel_normal(wxWindow* parent)
{
    auto sizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer* m_sizer_filament = new wxBoxSizer(wxHORIZONTAL);

    m_title_filament = new wxStaticText(parent, wxID_ANY, _L("Filament"), wxDefaultPosition, wxSize(AMS_MATERIALS_SETTING_LABEL_WIDTH, -1), 0);
    m_title_filament->SetFont(::Label::Body_13);
    m_title_filament->SetForegroundColour(AMS_MATERIALS_SETTING_GREY800);
    m_title_filament->Wrap(-1);
    m_sizer_filament->Add(m_title_filament, 0, wxALIGN_CENTER, 0);

    m_sizer_filament->Add(0, 0, 0, wxEXPAND, 0);

    m_comboBox_filament = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, AMS_MATERIALS_SETTING_COMBOX_WIDTH, 0, nullptr, wxCB_READONLY);

    m_sizer_filament->Add(m_comboBox_filament, 1, wxALIGN_CENTER, 0);

    // make the style the same with disable m_input_k_val, FIXME
    m_readonly_filament = new TextInput(parent, wxEmptyString, "", "", wxDefaultPosition, AMS_MATERIALS_SETTING_COMBOX_WIDTH, wxTE_CENTRE | wxTE_PROCESS_ENTER);
    m_readonly_filament->SetBorderColor(StateColor(std::make_pair(0xDBDBDB, (int)StateColor::Focused), std::make_pair(0x00AE42, (int)StateColor::Hovered),
        std::make_pair(0xDBDBDB, (int)StateColor::Normal)));
    m_readonly_filament->SetFont(::Label::Body_14);
    m_readonly_filament->SetLabelColor(AMS_MATERIALS_SETTING_GREY800);
    m_readonly_filament->GetTextCtrl()->Bind(wxEVT_SET_FOCUS, [](auto& e) {});
    m_readonly_filament->GetTextCtrl()->Hide();
    m_readonly_filament->Disable();
    m_sizer_filament->Add(m_readonly_filament, 1, wxALIGN_CENTER, 0);
    m_readonly_filament->Hide();

    wxBoxSizer* m_sizer_colour = new wxBoxSizer(wxHORIZONTAL);

    m_title_colour = new wxStaticText(parent, wxID_ANY, _L("Colour"), wxDefaultPosition, wxSize(AMS_MATERIALS_SETTING_LABEL_WIDTH, -1), 0);
    m_title_colour->SetFont(::Label::Body_13);
    m_title_colour->SetForegroundColour(AMS_MATERIALS_SETTING_GREY800);
    m_title_colour->Wrap(-1);
    m_sizer_colour->Add(m_title_colour, 0, wxALIGN_CENTER, 0);

    m_sizer_colour->Add(0, 0, 0, wxEXPAND, 0);

    m_clr_picker = new ColorPicker(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize);
    m_clr_picker->set_show_full(true);
    m_clr_picker->SetBackgroundColour(*wxWHITE);


    m_clr_picker->Bind(wxEVT_LEFT_DOWN, &AMSMaterialsSetting::on_clr_picker, this);
    m_sizer_colour->Add(m_clr_picker, 0, 0, 0);
    m_clr_name = new Label(parent, wxEmptyString);
    m_clr_name->SetForegroundColour(*wxBLACK);
    m_clr_name->SetBackgroundColour(*wxWHITE);
    m_clr_name->SetFont(Label::Body_13);
    m_sizer_colour->Add(m_clr_name, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(10));

    wxBoxSizer* m_sizer_temperature = new wxBoxSizer(wxHORIZONTAL);
    m_title_temperature = new wxStaticText(parent, wxID_ANY, _L("Nozzle\nTemperature"), wxDefaultPosition, wxSize(AMS_MATERIALS_SETTING_LABEL_WIDTH, -1), 0);
    m_title_temperature->SetFont(::Label::Body_13);
    m_title_temperature->SetForegroundColour(AMS_MATERIALS_SETTING_GREY800);
    m_title_temperature->Wrap(-1);
    m_sizer_temperature->Add(m_title_temperature, 0, wxALIGN_CENTER, 0);

    m_sizer_temperature->Add(0, 0, 0, wxEXPAND, 0);

    wxBoxSizer* sizer_other = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* sizer_tempinput = new wxBoxSizer(wxHORIZONTAL);

    m_input_nozzle_max = new ::TextInput(parent, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, AMS_MATERIALS_SETTING_INPUT_SIZE, wxTE_CENTRE | wxTE_PROCESS_ENTER);
    m_input_nozzle_min = new ::TextInput(parent, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, AMS_MATERIALS_SETTING_INPUT_SIZE, wxTE_CENTRE | wxTE_PROCESS_ENTER);
    m_input_nozzle_max->Enable(false);
    m_input_nozzle_min->Enable(false);

    m_input_nozzle_max->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    m_input_nozzle_max->GetTextCtrl()->SetSize(wxSize(-1, FromDIP(20)));
    m_input_nozzle_min->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    m_input_nozzle_min->GetTextCtrl()->SetSize(wxSize(-1, FromDIP(20)));

    degree            = new ScalableBitmap(parent, "degree", 16);
    bitmap_max_degree = new wxStaticBitmap(parent, -1, degree->bmp(), wxDefaultPosition, wxDefaultSize);
    bitmap_min_degree = new wxStaticBitmap(parent, -1, degree->bmp(), wxDefaultPosition, wxDefaultSize);

    sizer_tempinput->Add(m_input_nozzle_max, 1, wxALIGN_CENTER, 0);
    sizer_tempinput->Add(bitmap_min_degree, 0, wxALIGN_CENTER, 0);
    sizer_tempinput->Add(FromDIP(10), 0, 0, 0);
    sizer_tempinput->Add(m_input_nozzle_min, 1, wxALIGN_CENTER, 0);
    sizer_tempinput->Add(bitmap_max_degree, 0, wxALIGN_CENTER, 0);

    wxBoxSizer* sizer_temp_txt = new wxBoxSizer(wxHORIZONTAL);
    auto m_title_max = new wxStaticText(parent, wxID_ANY, _L("max"), wxDefaultPosition, AMS_MATERIALS_SETTING_INPUT_SIZE);
    m_title_max->SetForegroundColour(AMS_MATERIALS_SETTING_GREY800);
    m_title_max->SetFont(::Label::Body_13);
    auto m_title_min = new wxStaticText(parent, wxID_ANY, _L("min"), wxDefaultPosition, AMS_MATERIALS_SETTING_INPUT_SIZE);
    m_title_min->SetForegroundColour(AMS_MATERIALS_SETTING_GREY800);
    m_title_min->SetFont(::Label::Body_13);
    sizer_temp_txt->Add(m_title_max, 1, wxALIGN_CENTER, 0);
    sizer_temp_txt->Add(FromDIP(10), 0, 0, 0);
    sizer_temp_txt->Add(m_title_min, 1, wxALIGN_CENTER | wxRIGHT, FromDIP(16));


    sizer_other->Add(sizer_temp_txt, 0, wxALIGN_CENTER, 0);
    sizer_other->Add(sizer_tempinput, 0, wxALIGN_CENTER, 0);

    m_sizer_temperature->Add(sizer_other, 0, wxALL | wxALIGN_CENTER, 0);
    m_sizer_temperature->AddStretchSpacer();

    wxString warning_string = wxString::FromUTF8(
        (boost::format(_u8L("The input value should be greater than %1% and less than %2%")) % FILAMENT_MIN_TEMP % FILAMENT_MAX_TEMP).str());
    warning_text = new wxStaticText(parent, wxID_ANY, warning_string, wxDefaultPosition, wxDefaultSize, 0);
    warning_text->SetFont(::Label::Body_13);
    warning_text->SetForegroundColour(wxColour(255, 111, 0));

    warning_text->Wrap(AMS_MATERIALS_SETTING_BODY_WIDTH);
    warning_text->SetMinSize(wxSize(AMS_MATERIALS_SETTING_BODY_WIDTH, -1));
    warning_text->Hide();

    m_panel_SN = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    wxBoxSizer* m_sizer_SN = new wxBoxSizer(wxVERTICAL);
    m_sizer_SN->AddSpacer(FromDIP(16));

    wxBoxSizer* m_sizer_SN_inside = new wxBoxSizer(wxHORIZONTAL);

    auto m_title_SN = new wxStaticText(m_panel_SN, wxID_ANY, _L("SN"), wxDefaultPosition, wxSize(AMS_MATERIALS_SETTING_LABEL_WIDTH, -1), 0);
    m_title_SN->SetFont(::Label::Body_13);
    m_title_SN->SetForegroundColour(AMS_MATERIALS_SETTING_GREY800);
    m_title_SN->Wrap(-1);
    m_sizer_SN_inside->Add(m_title_SN, 0, wxALIGN_CENTER, 0);

    m_sizer_SN_inside->Add(0, 0, 0, wxEXPAND, 0);

    m_sn_number = new wxStaticText(m_panel_SN, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize);
    m_sn_number->SetForegroundColour(*wxBLACK);
    m_sizer_SN_inside->Add(m_sn_number, 0, wxALIGN_CENTER, 0);
    m_sizer_SN->Add(m_sizer_SN_inside);

    m_panel_SN->SetSizer(m_sizer_SN);
    m_panel_SN->Layout();
    m_panel_SN->Fit();

    wxBoxSizer* m_tip_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_tip_readonly = new Label(parent, _L(""));
    m_tip_readonly->SetForegroundColour(*wxBLACK);
    m_tip_readonly->SetBackgroundColour(*wxWHITE);
    m_tip_readonly->SetMinSize(wxSize(FromDIP(380), -1));
    m_tip_readonly->SetMaxSize(wxSize(FromDIP(380), -1));
    m_tip_readonly->Hide();
    m_tip_sizer->Add(m_tip_readonly, 0, wxALIGN_CENTER | wxRIGHT, FromDIP(20));

    sizer->Add(0, 0, 0, wxTOP, FromDIP(16));
    sizer->Add(m_sizer_filament, 0, wxLEFT | wxRIGHT, FromDIP(20));
    sizer->Add(0, 0, 0, wxTOP, FromDIP(16));
    sizer->Add(m_sizer_colour, 0, wxLEFT | wxRIGHT, FromDIP(20));
    sizer->Add(0, 0, 0, wxTOP, FromDIP(16));
    sizer->Add(m_sizer_temperature, 0, wxLEFT | wxRIGHT, FromDIP(20));
    sizer->Add(0, 0, 0, wxTOP, FromDIP(5));
    sizer->Add(warning_text, 0, wxLEFT | wxRIGHT, FromDIP(20));
    sizer->Add(m_panel_SN, 0, wxLEFT, FromDIP(20));
    sizer->Add(0, 0, 0, wxTOP, FromDIP(24));
    sizer->Add(m_tip_sizer, 0, wxLEFT, FromDIP(20));
    parent->SetSizer(sizer);
}

void AMSMaterialsSetting::create_panel_kn(wxWindow* parent)
{
    auto sizer = new wxBoxSizer(wxVERTICAL);
    auto cali_title_sizer = new wxBoxSizer(wxHORIZONTAL);
    // title
    m_ratio_text   = new wxStaticText(parent, wxID_ANY, _L("Factors of Flow Dynamics Calibration"));
    m_ratio_text->SetForegroundColour(wxColour(50, 58, 61));
    m_ratio_text->SetFont(Label::Head_14);

    std::string language = wxGetApp().app_config->get("language");
    wxString    region   = "en";
    if (language.find("zh") == 0)
        region = "zh";
    wxString link_url = wxString::Format("https://wiki.bambulab.com/%s/software/bambu-studio/calibration_pa", region);
    m_wiki_ctrl = new wxHyperlinkCtrl(parent, wxID_ANY, "Wiki", link_url);
    m_wiki_ctrl->SetNormalColour(*wxBLUE);
    m_wiki_ctrl->SetHoverColour(wxColour(0, 0, 200));
    m_wiki_ctrl->SetVisitedColour(*wxBLUE);
    m_wiki_ctrl->SetFont(Label::Head_14);
    cali_title_sizer->Add(m_ratio_text, 0, wxALIGN_CENTER_VERTICAL);
    cali_title_sizer->Add(m_wiki_ctrl, 0, wxALIGN_CENTER_VERTICAL);

    wxBoxSizer *m_sizer_nozzle_type = new wxBoxSizer(wxHORIZONTAL);
    // Nozzle Type
    m_title_nozzle_type = new wxStaticText(parent, wxID_ANY, _L("Nozzle Type"), wxDefaultPosition, wxSize(AMS_MATERIALS_SETTING_LABEL_WIDTH, -1), 0);
    m_title_nozzle_type->SetMinSize(wxSize(FromDIP(80), -1));
    m_title_nozzle_type->SetMaxSize(wxSize(FromDIP(80), -1));
    m_title_nozzle_type->SetFont(::Label::Body_13);
    m_title_nozzle_type->SetForegroundColour(AMS_MATERIALS_SETTING_GREY800);
    m_title_nozzle_type->Wrap(-1);
    m_sizer_nozzle_type->Add(m_title_nozzle_type, 0, wxALIGN_CENTER, 0);
    m_sizer_nozzle_type->Add(0, 0, 0, wxEXPAND, 0);

    m_comboBox_nozzle_type = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, AMS_MATERIALS_SETTING_COMBOX_WIDTH, 0, nullptr, wxCB_READONLY);
    m_comboBox_nozzle_type->Bind(wxEVT_COMMAND_COMBOBOX_SELECTED, &AMSMaterialsSetting::on_select_nozzle_pos_id, this);
    m_sizer_nozzle_type->Add(m_comboBox_nozzle_type, 1, wxALIGN_CENTER, 0);


    wxBoxSizer *m_sizer_cali_resutl = new wxBoxSizer(wxHORIZONTAL);
    // pa profile
    m_title_pa_profile = new wxStaticText(parent, wxID_ANY, _L("PA Profile"), wxDefaultPosition, wxSize(AMS_MATERIALS_SETTING_LABEL_WIDTH, -1), 0);
    m_title_pa_profile->SetMinSize(wxSize(FromDIP(80), -1));
    m_title_pa_profile->SetMaxSize(wxSize(FromDIP(80), -1));
    m_title_pa_profile->SetFont(::Label::Body_13);
    m_title_pa_profile->SetForegroundColour(AMS_MATERIALS_SETTING_GREY800);
    m_title_pa_profile->Wrap(-1);
    m_sizer_cali_resutl->Add(m_title_pa_profile, 0, wxALIGN_CENTER, 0);
    m_sizer_cali_resutl->Add(0, 0, 0, wxEXPAND, 0);

    m_comboBox_cali_result = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, AMS_MATERIALS_SETTING_COMBOX_WIDTH, 0, nullptr, wxCB_READONLY);
    m_sizer_cali_resutl->Add(m_comboBox_cali_result, 1, wxALIGN_CENTER, 0);

    auto kn_val_sizer = new wxFlexGridSizer(0, 2, 0, 0);
    kn_val_sizer->SetFlexibleDirection(wxBOTH);
    kn_val_sizer->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);
    kn_val_sizer->AddGrowableCol(1);

    // k params input
    m_k_param = new wxStaticText(parent, wxID_ANY, _L("Factor K"), wxDefaultPosition, wxDefaultSize, 0);
    m_k_param->SetMinSize(wxSize(FromDIP(80), -1));
    m_k_param->SetMaxSize(wxSize(FromDIP(80), -1));
    m_k_param->SetFont(::Label::Body_13);
    m_k_param->SetForegroundColour(wxColour(50, 58, 61));
    m_k_param->Wrap(-1);
    kn_val_sizer->Add(m_k_param, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(0));

    m_input_k_val = new TextInput(parent, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_CENTRE | wxTE_PROCESS_ENTER);
    m_input_k_val->SetMinSize(wxSize(FromDIP(245), -1));
    m_input_k_val->SetMaxSize(wxSize(FromDIP(245), -1));
    m_input_k_val->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    kn_val_sizer->Add(m_input_k_val, 0, wxALL | wxEXPAND | wxALIGN_CENTER_VERTICAL, FromDIP(0));

    // n params input
    wxBoxSizer* n_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_n_param = new wxStaticText(parent, wxID_ANY, _L("Factor N"), wxDefaultPosition, wxDefaultSize, 0);
    m_n_param->SetFont(::Label::Body_13);
    m_n_param->SetForegroundColour(wxColour(50, 58, 61));
    m_n_param->Wrap(-1);
    kn_val_sizer->Add(m_n_param, 1, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    m_input_n_val = new TextInput(parent, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_CENTRE | wxTE_PROCESS_ENTER);
    m_input_n_val->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    kn_val_sizer->Add(m_input_n_val, 0, wxALL | wxEXPAND | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    m_n_param->Hide();
    m_input_n_val->Hide();

    sizer->Add(0, 0, 0, wxTOP, FromDIP(10));
    sizer->Add(cali_title_sizer, 0, wxLEFT | wxRIGHT | wxEXPAND, FromDIP(20));
    sizer->Add(0, 0, 0, wxTOP, FromDIP(12));
    sizer->Add(m_sizer_nozzle_type, 0, wxLEFT | wxRIGHT, FromDIP(20));
    sizer->Add(0, 0, 0, wxTOP, FromDIP(10));
    sizer->Add(m_sizer_cali_resutl, 0, wxLEFT | wxRIGHT, FromDIP(20));
    sizer->Add(0, 0, 0, wxTOP, FromDIP(10));
    sizer->Add(kn_val_sizer, 0, wxLEFT | wxRIGHT | wxEXPAND, FromDIP(20));
    sizer->Add(0, 0, 0, wxTOP, FromDIP(10));
    parent->SetSizer(sizer);
}

void AMSMaterialsSetting::paintEvent(wxPaintEvent &evt)
{
    auto      size = GetSize();
    wxPaintDC dc(this);
    dc.SetPen(wxPen(StateColor::darkModeColorFor(wxColour("#000000")), 1, wxPENSTYLE_SOLID));
    dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
    dc.DrawRectangle(0, 0, size.x, size.y);
}

AMSMaterialsSetting::~AMSMaterialsSetting()
{
    m_comboBox_filament->Disconnect(wxEVT_COMMAND_COMBOBOX_SELECTED, wxCommandEventHandler(AMSMaterialsSetting::on_select_filament), NULL, this);
    m_comboBox_cali_result->Disconnect(wxEVT_COMMAND_COMBOBOX_SELECTED, wxCommandEventHandler(AMSMaterialsSetting::on_select_cali_result), NULL, this);
}

void AMSMaterialsSetting::input_min_finish()
{
    if (m_input_nozzle_min->GetTextCtrl()->GetValue().empty()) return;

    auto val = std::atoi(m_input_nozzle_min->GetTextCtrl()->GetValue().c_str());

    if (val < FILAMENT_MIN_TEMP || val > FILAMENT_MAX_TEMP) {
        warning_text->Show();
    } else {
        warning_text->Hide();
    }
    Layout();
    Fit();
}

void AMSMaterialsSetting::input_max_finish()
{
    if (m_input_nozzle_max->GetTextCtrl()->GetValue().empty()) return;

    auto val = std::atoi(m_input_nozzle_max->GetTextCtrl()->GetValue().c_str());

    if (val < FILAMENT_MIN_TEMP || val > FILAMENT_MAX_TEMP) {
        warning_text->Show();
    }
    else {
        warning_text->Hide();
    }
    Layout();
    Fit();
}

void AMSMaterialsSetting::update()
{
    if (obj) {
        update_widgets();
        update_filament_editing(obj->is_in_printing() || obj->can_resume());
    }
}

void AMSMaterialsSetting::update_filament_editing(bool is_printing)
{
    if (is_printing) {
        m_comboBox_filament->Enable(obj->is_support_filament_setting_inprinting);
        m_comboBox_nozzle_type->Enable(obj->is_support_filament_setting_inprinting);
        m_comboBox_cali_result->Enable(obj->is_support_filament_setting_inprinting);
        m_button_confirm->Show(obj->is_support_filament_setting_inprinting);
        m_button_reset->Show(obj->is_support_filament_setting_inprinting);
    }
    else {
        m_comboBox_filament->Enable(true);
        m_comboBox_nozzle_type->Enable(true);
        m_comboBox_cali_result->Enable(true);
        m_button_reset->Show(true);
        m_button_confirm->Show(true);
    }

    if (!m_is_third) {
        m_tip_readonly->SetLabelText(wxEmptyString);
        m_tip_readonly->Hide();
    }
    else {
        if (!obj->is_support_filament_setting_inprinting) {
            if (!is_virtual_tray()) {
                m_tip_readonly->SetLabelText(_L("Setting AMS slot information while printing is not supported"));
            } else {
                m_tip_readonly->SetLabelText(_L("Setting Virtual slot information while printing is not supported"));
            }
        } else {
            m_tip_readonly->SetLabelText(wxEmptyString);
        }

        m_tip_readonly->Wrap(FromDIP(380));
        m_tip_readonly->Show(is_printing);
    }

    // View-only mode (e.g. 2D laser/cut): keep the dialog inspectable but lock
    // every editable control and hide the apply/reset buttons so nothing can be
    // committed, regardless of m_is_third.
    if (m_view_only) {
        m_comboBox_filament->Enable(false);
        m_comboBox_nozzle_type->Enable(false);
        m_comboBox_cali_result->Enable(false);
        m_input_k_val->Enable(false);
        m_input_n_val->Enable(false);
        m_button_confirm->Hide();
        m_button_reset->Hide();
    }
}

std::vector<ColorPickerPopup::ColorItem> AMSMaterialsSetting::get_preset_color_items(const std::string& filament_id) const
{
    std::vector<ColorPickerPopup::ColorItem> items;
    if (filament_id.empty()) return items;

    auto* clr_query = GUI::wxGetApp().get_filament_color_code_query();
    if (!clr_query) return items;

    FilamentColorCodes* color_codes = clr_query->GetFilaInfoMap(wxString::FromUTF8(filament_id));
    if (!color_codes || !color_codes->GetFilamentColor2CodeMap()) return items;

    std::set<std::string> seen;
    for (const auto& color_pair : *color_codes->GetFilamentColor2CodeMap()) {
        const FilamentColor& fila_color = color_pair.first;
        FilamentColorCode* color_code = color_pair.second;
        if (!color_code || fila_color.m_colors.empty()) continue;

        ColorPickerPopup::ColorItem item;
        item.ctype = filament_color_type_to_ams_ctype(fila_color.m_color_type, fila_color.ColorCount());
        item.name = color_code->GetFilaColorName();

        std::string key = std::to_string(item.ctype);
        for (const wxColour& color : fila_color.m_colors) {
            wxColour solid_color(color.Red(), color.Green(), color.Blue(), 255);
            item.colors.emplace_back(solid_color);
            key += "|" + colour_to_ams_string(solid_color);
        }

        if (item.colors.empty() || !seen.insert(key).second) continue;
        items.emplace_back(std::move(item));
    }

    return items;
}

void AMSMaterialsSetting::on_select_reset(wxCommandEvent& event) {
    // View-only mode never commits changes.
    if (m_view_only) {
        return;
    }

    MessageDialog msg_dlg(nullptr, _L("Are you sure you want to clear the filament information?"), wxEmptyString, wxICON_WARNING | wxOK | wxCANCEL);
    auto result = msg_dlg.ShowModal();
    if (result != wxID_OK)
        return;

    m_input_nozzle_min->GetTextCtrl()->SetValue("");
    m_input_nozzle_max->GetTextCtrl()->SetValue("");
    ams_filament_id = "";
    ams_setting_id = "";
    m_filament_selection = -1;
    wxString k_text = "0.000";
    wxString n_text = "0.000";
    m_filament_type = "";
    long nozzle_temp_min_int = 0;
    long nozzle_temp_max_int = 0;
    wxColour color = *wxWHITE;
    char col_buf[10];
    sprintf(col_buf, "%02X%02X%02X00", (int)color.Red(), (int)color.Green(), (int)color.Blue());
    std::string color_str;  // reset use empty string

    std::string   selected_ams_id;
    PresetBundle *preset_bundle = wxGetApp().preset_bundle;
    if (preset_bundle) {
        for (auto it = preset_bundle->filaments.begin(); it != preset_bundle->filaments.end(); it++) {
            auto        filament_item = map_filament_items[m_comboBox_filament->GetValue().ToStdString()];
            std::string filament_id   = filament_item.filament_id;
            if (it->filament_id.compare(filament_id) == 0) {
                selected_ams_id = it->filament_id;
                break;
            }
        }
    }

    if (obj) {
        if(m_is_third){
            obj->command_ams_filament_settings(ams_id, slot_id, ams_filament_id, ams_setting_id, std::string(col_buf), m_filament_type, nozzle_temp_min_int,
                                               nozzle_temp_max_int);
        }

        // set k / n value
        if (!obj->GetCalib()->IsVersionInited() && obj->get_printer_series() == PrinterSeries::SERIES_P1P) {
            // set extrusion cali ratio
            int cali_tray_id = obj->GetFilaSystem()->GetTrayIdByAmsSlotId(ams_id, slot_id); //ams_id * 4 + slot_id;

            double k = 0.0;
            double n = 0.0;
            try {
                k_text.ToDouble(&k);
                n_text.ToDouble(&n);
            } catch (...) {
                BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " Convert k/n value failed, k_text: " << k_text << ", n_text: " << n_text;
            }

            obj->command_extrusion_cali_set(cali_tray_id, "", "", k, n);
        }
        else {
            PACalibIndexInfo select_index_info;
            select_index_info.tray_id         = obj->GetFilaSystem()->GetTrayIdByAmsSlotId(ams_id, slot_id);
            select_index_info.ams_id          = ams_id;
            select_index_info.slot_id         = slot_id;
            select_index_info.nozzle_diameter = obj->GetExtderSystem()->GetNozzleDiameter(0);
            select_index_info.cali_idx        = -1;
            select_index_info.filament_id     = selected_ams_id;
            CalibUtils::select_PA_calib_result(select_index_info);
        }
    }
    Close();
}


static DevFilaBlacklist::CheckResult
sCheckFilamentInfo(PresetBundle* preset_bundle,
                   MachineObject* obj,
                   int ams_id, int slot_id,
                   const std::string& filament_id,
                   std::string& ams_filament_id,
                   std::string& ams_setting_id)
{
    DevFilaBlacklist::CheckResult result;
    if (!preset_bundle) {
        return result;
    }

    auto it = preset_bundle->get_filament_by_filament_id(filament_id);
    if (!it.has_value()) {
        return result;
    }

    if (wxGetApp().app_config->get("skip_ams_blacklist_check") != "true") {
        DevFilaBlacklist::CheckFilamentInfo check_info;
        check_info.dev_id = obj->get_dev_id();
        check_info.model_id = obj->printer_type;
        check_info.fila_id = it->filament_id;
        check_info.fila_type = it->filament_type;

        auto option = GUI::wxGetApp().preset_bundle->get_filament_by_filament_id(check_info.fila_id);
        check_info.fila_name = option ? option->filament_name : "";
        check_info.fila_vendor = option ? option->vendor : "";
        check_info.has_filament_switch = obj->GetFilaSwitch()->IsInstalled();

        check_info.ams_id = ams_id;
        check_info.slot_id = slot_id;
        check_info.extruder_id = obj->GetFilaSystem()->GetExtruderIdByAmsId(std::to_string(ams_id));

        if (check_info.extruder_id == MAIN_EXTRUDER_ID && obj->GetNozzleRack()->IsSupported()) {
            ;// the extruder have serval nozzles, do nothing here
        } else {
            check_info.nozzle_flow = obj->GetFilaSystem()->GetNozzleFlowStringByAmsId(std::to_string(ams_id));
            auto nozzle = obj->GetNozzleSystem()->GetNozzleByPosId(check_info.extruder_id.value_or(-1));
            if (!nozzle.IsEmpty()) {
                check_info.nozzle_diameter = nozzle.GetNozzleDiameter();
            }
        }

        result = DevFilaBlacklist::check_filaments_in_blacklist(check_info);
    }

    ams_filament_id = it->filament_id;
    ams_setting_id = it->setting_id;
    return result;
}

namespace {

void remember_ams_recent_filament_preset(
    const wxString& selected);

} // namespace

void AMSMaterialsSetting::on_select_ok(wxCommandEvent& event)
{
    if (!obj) {
        return;
    }

    // View-only mode never commits changes.
    if (m_view_only) {
        return;
    }

    //get filament id
    ams_filament_id = "";
    ams_setting_id = "";

    // the combobox item
    auto filament_item = map_filament_items[into_u8(m_comboBox_filament->GetValue())];
    // For Filament Manager spool entries the ComboBox text is wxEmptyString, so
    // map_filament_items lookup yields nothing. Re-populate from the spool directly.
    if (!m_selected_spool_id.empty() && filament_item.filament_id.empty()) {
        auto* store = wxGetApp().fila_manager_store();
        const FilamentSpool* sp = store ? store->get_spool(m_selected_spool_id) : nullptr;
        if (sp) {
            filament_item.filament_id = sp->setting_id;
            filament_item.setting_id  = sp->setting_id;
            filament_item.spool_id    = sp->spool_id;
        }
    }


    // check filament info
    PresetBundle* preset_bundle = wxGetApp().preset_bundle;
    const auto& fila_check_res = sCheckFilamentInfo(preset_bundle, obj, ams_id, slot_id, filament_item.filament_id, ams_filament_id, ams_setting_id);
    bool can_set_fila = fila_check_res.get_items_by_action("prohibition").empty();

    // check if need to set usr_has_setup_tpu
    auto fila_item = preset_bundle ? preset_bundle->get_filament_by_filament_id(filament_item.filament_id) : std::nullopt;
    if (fila_item.has_value() && can_set_fila && DevPrinterConfigUtil::support_user_first_setup_tpu_check(obj->printer_type)) {
        if ((fila_item->filament_type == "TPU" || fila_item->filament_type == "TPU-AMS") &&
            wxGetApp().app_config->get("usr_has_setup_tpu") != "true") {

            MessageDialog dlg(this, _L("TPU needs a different feeding path and loading procedure. Otherwise clogging or jam may happen. Please read the tutorial before using TPU."),
                              SLIC3R_APP_NAME + _L("Info"), wxICON_INFORMATION);
            dlg.AddButton(wxID_CANCEL, _L("Cancel"), false);
            dlg.AddButton(wxID_OK, _L("Go to Check"), true);
            int rtn = dlg.ShowModal();
            if (rtn != wxID_OK) {
                return;
            }

            wxGetApp().app_config->set("usr_has_setup_tpu", "true");

            auto tpu_check_url = DevPrinterConfigUtil::support_user_first_setup_tpu_check_url(obj->printer_type);
            if (!tpu_check_url.empty()) {
                wxLaunchDefaultBrowser(tpu_check_url);
            }
        };
    }

    if (!fila_check_res.action_items.empty()) {
        if (const auto& prohibit_items = fila_check_res.get_items_by_action("prohibition"); !prohibit_items.empty()) {
            wxString info_msg;
            for (auto item : prohibit_items) {
                info_msg += item.info_msg + "\n";
            }

            MessageDialog msg_wingow(nullptr, info_msg, _L("Error"), wxICON_WARNING | wxOK);
            msg_wingow.ShowModal();
            return;
        }

        if (const auto& warning_items = fila_check_res.get_items_by_action("warning"); !warning_items.empty()) {
            std::vector<FilamentWarningInfo> infos;

            for (auto item : warning_items) {
                FilamentWarningInfo info;
                info.info_msg = item.info_msg;
                info.wiki_url = item.wiki_url;
                infos.emplace_back(info);
            }

            FilamentWarningDialog msg_window(nullptr, _("Warning"), infos);
            msg_window.ShowModal();
        }
    }

    wxString nozzle_temp_min = m_input_nozzle_min->GetTextCtrl()->GetValue();
    auto     filament = m_comboBox_filament->GetValue();

    wxString nozzle_temp_max = m_input_nozzle_max->GetTextCtrl()->GetValue();

    long nozzle_temp_min_int, nozzle_temp_max_int;
    nozzle_temp_min.ToLong(&nozzle_temp_min_int);
    nozzle_temp_max.ToLong(&nozzle_temp_max_int);
    wxColour color = m_clr_picker->m_colour;
    std::string tray_color = colour_to_ams_string(color);
    std::vector<std::string> tray_colors;
    int tray_ctype = 2;
    // Only printers that advertise manual multi-color editing (fun2[23]) receive the
    // cols/ctype fields; otherwise fall back to the legacy single-color command.
    if (obj->is_support_filament_manual_multi_color) {
        for (const wxColour& selected_color : m_clr_picker->m_cols) {
            tray_colors.emplace_back(colour_to_ams_string(selected_color));
        }
        if (tray_colors.empty()) {
            tray_colors.emplace_back(tray_color);
        }
        tray_ctype = tray_colors.size() > 1 ? m_clr_picker->ctype : 2;
    }

    if (ams_filament_id.empty() || nozzle_temp_min.empty() || nozzle_temp_max.empty() || m_filament_type.empty()) {
        BOOST_LOG_TRIVIAL(trace) << "Invalid Setting id";
        MessageDialog msg_dlg(nullptr, _L("You need to select the material type and color first."), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }


    // set filament
    if (m_is_third) {
        obj->command_ams_filament_settings(ams_id, slot_id, ams_filament_id, ams_setting_id, tray_color, m_filament_type, nozzle_temp_min_int, nozzle_temp_max_int,
                                           tray_colors, tray_ctype);

        // Filament Manager entries are identified by their spool ID. Only
        // remember actual system-preset aliases in the shared recent list.
        if (m_selected_spool_id.empty()) {
            remember_ams_recent_filament_preset(
                m_comboBox_filament->GetValue());
        }
    }

    // Optimistic local update: write the new color into the tray object immediately
    // so the AMS view refreshes on the next 1-second timer tick without waiting for
    // the MQTT ACK round-trip (typically 1–3 s on cloud connections).
    if (ams_id != 255) {
        if (auto* ams_obj = obj->GetFilaSystem()->GetAmsById(std::to_string(ams_id))) {
            auto tray_it = ams_obj->GetTrays().find(std::to_string(slot_id));
            if (tray_it != ams_obj->GetTrays().end() && tray_it->second) {
                DevAmsTray* tray = tray_it->second;
                tray->color = tray_color;
                tray->cols  = tray_colors;
                tray->ctype = static_cast<DevFilaColorType>(tray_ctype);
                tray->set_hold_count();
            }
        }
    } else if (!obj->vt_slot.empty()) {
        // virtual tray (ams_id == 255)
        DevAmsTray& vt = obj->vt_slot[0];
        vt.color = tray_color;
        vt.cols  = tray_colors;
        vt.ctype = static_cast<DevFilaColorType>(tray_ctype);
        vt.set_hold_count();
    }

    //reset param
    wxString k_text = m_input_k_val->GetTextCtrl()->GetValue();
    wxString n_text = m_input_n_val->GetTextCtrl()->GetValue();

    if (!obj->GetCalib()->IsVersionInited() && (obj->get_printer_series() != PrinterSeries::SERIES_X1) && !ExtrusionCalibration::check_k_validation(k_text)) {
        wxString k_tips = wxString::Format(_L("Please input a valid value (K in %.1f~%.1f)"), MIN_PA_K_VALUE, MAX_PA_K_VALUE);
        wxString kn_tips = wxString::Format(_L("Please input a valid value (K in %.1f~%.1f, N in %.1f~%.1f)"), MIN_PA_K_VALUE, MAX_PA_K_VALUE, 0.6, 2.0);
        MessageDialog msg_dlg(nullptr, k_tips, wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }

    double k = 0.0;
    double n = 0.0;
    try {
        k_text.ToDouble(&k);
        n_text.ToDouble(&n);
    }
    catch (...) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " Convert k/n value failed, k_text: " << k_text << ", n_text: " << n_text;
    }

    // set k / n value
    if (is_virtual_tray()) {
        auto vt_tray_id = obj->GetFilaSystem()->GetTrayIdByAmsSlotId(ams_id, 0);

        if (obj->GetCalib()->IsVersionInited()) {
            PACalibIndexInfo select_index_info;
            select_index_info.tray_id = vt_tray_id;
            select_index_info.ams_id = ams_id;
            select_index_info.slot_id = 0;
            select_index_info.nozzle_diameter = obj->GetExtderSystem()->GetNozzleDiameter(0);

            auto cali_select_id = m_comboBox_cali_result->GetSelection();
            if (m_pa_profile_items.size() > 0 && cali_select_id >= 0) {
                select_index_info.cali_idx = m_pa_profile_items[cali_select_id].cali_idx;
                select_index_info.filament_id = m_pa_profile_items[cali_select_id].filament_id;
            }
            else { // default item
                select_index_info.cali_idx = -1;
                select_index_info.filament_id = ams_filament_id;
            }

            CalibUtils::select_PA_calib_result(select_index_info);
        }
        else {
            obj->command_extrusion_cali_set(vt_tray_id, "", "", k, n);
        }
    }
    else {
        int cali_tray_id = obj->GetFilaSystem()->GetTrayIdByAmsSlotId(ams_id, slot_id); //ams_id * 4 + slot_id;
        if (obj->GetCalib()->IsVersionInited()) {
            PACalibIndexInfo select_index_info;
            select_index_info.tray_id = cali_tray_id;
            select_index_info.ams_id = ams_id;
            select_index_info.slot_id = slot_id;
            select_index_info.nozzle_diameter = obj->GetExtderSystem()->GetNozzleDiameter(0);

            auto cali_select_id = m_comboBox_cali_result->GetSelection();
            if (m_pa_profile_items.size() > 0 && cali_select_id > 0) {
                select_index_info.cali_idx = m_pa_profile_items[cali_select_id].cali_idx;
                select_index_info.filament_id = m_pa_profile_items[cali_select_id].filament_id;
            }
            else { // default item
                select_index_info.cali_idx    = -1;
                select_index_info.filament_id = ams_filament_id;
            }

            CalibUtils::select_PA_calib_result(select_index_info);
        }
        else {
            obj->command_extrusion_cali_set(cali_tray_id, "", "", k, n);
        }
    }
    // When the user picked a Filament Manager spool, immediately update its
    // in-printer snapshot without waiting for the next MQTT push_status.
    if (!m_selected_spool_id.empty()) {
        auto* store = wxGetApp().fila_manager_store();
        if (store) {
            // Resolve ams_type from the device object.
            int resolved_ams_type = -1;
            if (auto* ams_obj = obj->GetFilaSystem()->GetAmsById(std::to_string(ams_id)))
                resolved_ams_type = static_cast<int>(ams_obj->GetAmsType());

            if (store->force_mount_spool(m_selected_spool_id,
                                         obj->get_dev_id(),
                                         obj->get_dev_name(),
                                         ams_id,
                                         resolved_ams_type,
                                         std::to_string(slot_id))) {
                store->save();
                // Push in-printer snapshot to cloud.
                if (auto* disp = wxGetApp().fila_manager_cloud_disp()) {
                    const FilamentSpool* s = store->get_spool(m_selected_spool_id);
                    if (s) {
                        nlohmann::json patch = {
                            {"in_printer",  s->in_printer},
                            {"dev_id",      s->dev_id},
                            {"ams_sn",      s->ams_sn},
                            {"ams_id",      s->ams_id},
                            {"ams_type",    s->ams_type},
                            {"slot_id",     s->slot_id},
                            {"device_name", s->device_name}
                        };
                        disp->enqueue_push_update(m_selected_spool_id, patch);
                    }
                }
            }
        }
    }

    Close();
}

void AMSMaterialsSetting::on_select_close(wxCommandEvent &event)
{
    Close();
}

void AMSMaterialsSetting::set_color(wxColour color)
{
    //m_clrData->SetColour(color);
    m_clr_picker->is_empty(false);
    m_clr_picker->set_color(color);

    FilamentColor fila_color;
    fila_color.m_colors.insert(color);
    fila_color.EndSet(m_clr_picker->ctype);
    auto clr_query = GUI::wxGetApp().get_filament_color_code_query();
    m_clr_name->SetLabelText(clr_query->GetFilaColorName(ams_filament_id, fila_color));
}

void AMSMaterialsSetting::set_empty_color(wxColour color)
{
    m_clr_picker->is_empty(true);
    m_clr_picker->set_color(color);
    m_clr_picker->set_colors({ color });
    m_clr_name->SetLabelText(wxEmptyString);
}

void AMSMaterialsSetting::set_colors(std::vector<wxColour> colors)
{
    //m_clrData->SetColour(color);
    m_clr_picker->set_colors(colors);

    if (!colors.empty())
    {
        FilamentColor fila_color;
        for (const auto& clr : colors) { fila_color.m_colors.insert(clr); }
        fila_color.EndSet(m_clr_picker->ctype);
        auto clr_query = GUI::wxGetApp().get_filament_color_code_query();
        m_clr_name->SetLabelText(clr_query->GetFilaColorName(ams_filament_id, fila_color));
    }
}

void AMSMaterialsSetting::set_ctype(int ctype)
{
    m_clr_picker->ctype = ctype;
}

void AMSMaterialsSetting::on_picker_color(wxCommandEvent& event)
{
    std::vector<wxColour> colors = m_color_picker_popup.get_selected_colours();
    if (colors.empty()) {
        unsigned int color_num = event.GetInt();
        colors.emplace_back(color_num >> 24 & 0xFF, color_num >> 16 & 0xFF, color_num >> 8 & 0xFF, color_num & 0xFF);
    }

    set_ctype(m_color_picker_popup.get_selected_ctype());
    set_color(colors.front());
    set_colors(colors);
}

void AMSMaterialsSetting::on_clr_picker(wxMouseEvent &event)
{
    if (m_view_only)
        return;

    if(!m_is_third)
        return;

    if (!m_selected_spool_id.empty())
        return;

    if (obj->is_in_printing() || obj->can_resume()) {
        if (!obj->is_support_filament_setting_inprinting) {
            return;
        }
    }

    std::vector<ColorPickerPopup::ColorItem> ams_colors = collect_ams_color_items(obj->GetFilaSystem().get());

    m_color_picker_popup.set_ams_colours(ams_colors);
    m_color_picker_popup.set_preset_colours(get_preset_color_items(ams_filament_id));
    m_color_picker_popup.set_def_colour(m_clr_picker->m_colour, m_clr_picker->m_cols, m_clr_picker->ctype);

    wxPoint img_pos = m_clr_picker->ClientToScreen(wxPoint(0, 0));
    wxPoint popup_pos(img_pos.x - m_color_picker_popup.GetSize().x - FromDIP(95), img_pos.y - FromDIP(65));
    m_color_picker_popup.Position(popup_pos, wxSize(0, 0));
    m_color_picker_popup.Popup();
}

bool AMSMaterialsSetting::is_virtual_tray()
{
    if (ams_id == VIRTUAL_TRAY_MAIN_ID || ams_id == VIRTUAL_TRAY_DEPUTY_ID)
        return true;
    return false;
}

void AMSMaterialsSetting::update_widgets()
{
    if (obj && obj->get_printer_series() == PrinterSeries::SERIES_X1 && !obj->GetCalib()->IsVersionInited()) {
        // Low version firmware does not display k value
        m_panel_normal->Show();
        m_panel_kn->Hide();
    }
    else if(is_virtual_tray()) // virtual tray
    {
        if (obj)
            m_panel_normal->Show();
        else
            m_panel_normal->Hide();
        m_panel_kn->Show();
    } else if (obj && (obj->ams_support_virtual_tray || obj->GetCalib()->IsVersionInited())) {
        m_panel_normal->Show();
        m_panel_kn->Show();
    } else {
        m_panel_normal->Show();
        m_panel_kn->Hide();
    }
    Layout();
}

bool AMSMaterialsSetting::Show(bool show)
{
    if (show) {
        m_button_confirm->SetMinSize(AMS_MATERIALS_SETTING_BUTTON_SIZE);
        m_input_nozzle_max->GetTextCtrl()->SetSize(wxSize(-1, FromDIP(20)));
        m_input_nozzle_min->GetTextCtrl()->SetSize(wxSize(-1, FromDIP(20)));
        //m_clr_picker->set_color(m_clr_picker->GetParent()->GetBackgroundColour());

        m_ratio_text->Show();
        m_wiki_ctrl->Show();
        m_k_param->Show();
        m_input_k_val->Show();
        Layout();
        Fit();
        wxGetApp().UpdateDlgDarkUI(this);
    }
    return DPIDialog::Show(show);
}

// "RRGGBB" / "#RRGGBB" → wxColour. Falls back to grey on empty / invalid.
static wxColour _parse_hex_color(const std::string& hex)
{
    if (hex.empty()) return wxColour(0x63, 0x63, 0x63);
    wxString s = wxString::FromUTF8(hex);
    if (!s.StartsWith("#")) s = "#" + s;
    wxColour c(s);
    return c.IsOk() ? c : wxColour(0x63, 0x63, 0x63);
}

// ---------------------------------------------------------------------------
// Helpers for the two-section grouped ComboBox (Filament Manager + System
// presets).
// ---------------------------------------------------------------------------

// colors[] with precedence over single color_code.
static std::vector<wxColour> _spool_colors(const Slic3r::GUI::FilamentSpool& sp)
{
    std::vector<wxColour> out;
    if (!sp.colors.empty()) {
        out.reserve(sp.colors.size());
        for (const std::string& hex : sp.colors)
            out.push_back(_parse_hex_color(hex));
        return out;
    }
    if (!sp.color_code.empty())
        out.push_back(_parse_hex_color(sp.color_code));
    return out;
}

// 36×36 DIP rounded-square colour chip matching SpoolColorChip.tsx:
//   single  → flat fill
//   multi(1)→ hard-edge equal-width vertical strips clipped by the rounded rect
//   grad(0) → horizontal linear gradient clipped by the rounded rect
//   inset 1px ring: dark mode white@10%, light mode black@18%
static wxBitmap _make_spool_color_chip(wxWindow* ctx, const Slic3r::GUI::FilamentSpool& sp)
{
    const int size_px = ctx ? ctx->FromDIP(36) : 36;
    const double radius = std::max(2.0, std::round(size_px / 6.0));

    wxBitmap bmp(size_px, size_px);
    
#if defined(__WXMSW__) || defined(__WXOSX__)
    bmp.UseAlpha();
#endif

    wxMemoryDC dc(bmp);
    dc.SetBackground(*wxTRANSPARENT_BRUSH);
    dc.Clear();

    wxGraphicsContext* gc = wxGraphicsContext::Create(dc);
    if (!gc) { dc.SelectObject(wxNullBitmap); return bmp; }
    gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

    wxGraphicsPath chip_path = gc->CreatePath();
    chip_path.AddRoundedRectangle(0, 0, size_px, size_px, radius);

    const auto colors = _spool_colors(sp);
    const bool is_multicolor = (sp.color_type == 1) && colors.size() > 1;
    const bool is_gradient   = (sp.color_type == 0) && colors.size() > 1;

    if (is_multicolor) {
        const int n = static_cast<int>(colors.size());
        const int base_strip = size_px / n;
        int x = 0;
        for (int i = 0; i < n; ++i) {
            const int w = (i == n - 1) ? (size_px - base_strip * (n - 1)) : base_strip;
            gc->PushState();
            gc->Clip(x, 0, w, size_px);
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->SetBrush(wxBrush(colors[i]));
            gc->FillPath(chip_path);
            gc->ResetClip();
            gc->PopState();
            x += w;
        }
    } else if (is_gradient) {
        const int n = static_cast<int>(colors.size());
        if (n == 2) {
            gc->PushState();
            gc->Clip(0, 0, size_px, size_px);
            wxGraphicsBrush gb = gc->CreateLinearGradientBrush(0, 0, size_px, 0, colors[0], colors[1]);
            gc->SetBrush(gb);
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->FillPath(chip_path);
            gc->ResetClip();
            gc->PopState();
        } else {
            const double seg_w = static_cast<double>(size_px) / (n - 1);
            for (int i = 0; i < n - 1; ++i) {
                const double x0 = i * seg_w;
                const double x1 = (i == n - 2) ? size_px : (i + 1) * seg_w;
                gc->PushState();
                gc->Clip(x0, 0, x1 - x0, size_px);
                wxGraphicsBrush gb = gc->CreateLinearGradientBrush(x0, 0, x1, 0, colors[i], colors[i + 1]);
                gc->SetBrush(gb);
                gc->SetPen(*wxTRANSPARENT_PEN);
                gc->FillPath(chip_path);
                gc->ResetClip();
                gc->PopState();
            }
        }
    } else {
        const wxColour fill = colors.empty() ? wxColour(0x88, 0x88, 0x88) : colors.front();
        gc->SetBrush(wxBrush(fill));
        gc->SetPen(*wxTRANSPARENT_PEN);
        gc->FillPath(chip_path);
    }

    // Inset 1px ring (stroked on slightly inset path so it stays inside).
    const bool dark = wxGetApp().dark_mode();
    const wxColour ring_color = dark ? wxColour(255, 255, 255, 26) : wxColour(0, 0, 0, 46);
    wxGraphicsPath ring_path = gc->CreatePath();
    ring_path.AddRoundedRectangle(0.5, 0.5, size_px - 1.0, size_px - 1.0, radius);
    gc->SetBrush(*wxTRANSPARENT_BRUSH);
    gc->SetPen(wxPen(ring_color, 1));
    gc->StrokePath(ring_path);

    delete gc;
    dc.SelectObject(wxNullBitmap);
    return bmp;
}

static wxString _spool_display_name(const Slic3r::GUI::FilamentSpool& sp)
{
    if (!sp.series.empty())        return wxString::FromUTF8(sp.series);
    if (!sp.material_type.empty()) return wxString::FromUTF8(sp.material_type);
    if (!sp.color_name.empty())    return wxString::FromUTF8(sp.color_name);
    return _L("Filament");
}

static wxString _spool_note_line(const Slic3r::GUI::FilamentSpool& sp)
{
    wxString note = sp.note.empty() ? "--" : wxString::FromUTF8(sp.note);
    constexpr size_t kMaxNote = 30;
    if (note.length() > kMaxNote)
        note = note.SubString(0, kMaxNote - 3) + "...";
    return _L("Remark") + ": " + note;
}

// Pre-render one spool row as a full-width wxBitmap:
//   [pad][36×36 chip][gap][bold name ...]              [right: 200g]
//                         [grey smaller: Note: <note>]
static wxBitmap _render_spool_row_bitmap(wxWindow* ctx,
                                         const Slic3r::GUI::FilamentSpool& sp,
                                         int width_px,
                                         bool dimmed = false,
                                         bool* note_truncated = nullptr)
{
    const int pad_x     = ctx->FromDIP(8);
    const int pad_y     = ctx->FromDIP(6);
    const int icon_px   = ctx->FromDIP(36);
    const int gap       = ctx->FromDIP(8);
    const int row_h     = ctx->FromDIP(52);
    const int line_h    = ctx->FromDIP(20);
    const int right_pad = ctx->FromDIP(12);

    const int total_w = std::max(width_px, ctx->FromDIP(260));
    const int total_h = row_h;

    wxBitmap bmp(total_w, total_h);
#ifdef __WXOSX__
    bmp.UseAlpha();
#endif
    wxMemoryDC dc(bmp);
    const bool dark_row = wxGetApp().dark_mode();
    dc.SetBackground(wxBrush(dark_row ? wxColour(0x2D, 0x2D, 0x31) : *wxWHITE));
    dc.Clear();
    dc.SetPen(*wxTRANSPARENT_PEN);

    wxBitmap icon = _make_spool_color_chip(ctx, sp);
    if (icon.IsOk())
        dc.DrawBitmap(icon, pad_x, (total_h - icon_px) / 2, true);

    const int text_x     = pad_x + icon_px + gap;
    const int weight_col = ctx->FromDIP(60);
    const int text_max_w = std::max(total_w - right_pad - weight_col - text_x, ctx->FromDIP(40));

    int remain_g = 0;
    const double total_net = sp.effective_total_net_weight();
    if (total_net > 0.0) {
        const int pct = std::max(0, std::min(100, sp.remain_percent));
        remain_g = static_cast<int>(total_net * pct / 100.0);
    }
    const wxString weight_str = wxString::Format("%dg", remain_g);

    wxFont name_font = ::Label::Body_14;
    wxFont note_font = ::Label::Body_14;
    note_font.SetPointSize(std::max(8, note_font.GetPointSize() - 2));

    const int top_y    = pad_y;
    const int bottom_y = pad_y + line_h;

    // Name
    dc.SetFont(name_font);
    dc.SetTextForeground(dimmed ? wxColour(0xB0, 0xB0, 0xB0)
                                : (dark_row ? wxColour(0xE5, 0xE5, 0xE6) : wxColour(0x26, 0x2E, 0x30)));
    wxString name = _spool_display_name(sp);
    if (dc.GetTextExtent(name).GetWidth() > text_max_w)
        name = wxControl::Ellipsize(name, dc, wxELLIPSIZE_END, text_max_w);
    dc.DrawText(name, text_x, top_y);

    // Weight (right-aligned, same font as name)
    const wxSize w_sz = dc.GetTextExtent(weight_str);
    dc.DrawText(weight_str, total_w - right_pad - w_sz.GetWidth(), top_y);

    // Note
    dc.SetFont(note_font);
    dc.SetTextForeground(dimmed ? wxColour(0xC0, 0xC0, 0xC0) : wxColour(0x90, 0x90, 0x90));
    constexpr size_t kNoteHardCap = 30;
    const bool note_hardcap_cut = !sp.note.empty() &&
        wxString::FromUTF8(sp.note).length() > kNoteHardCap;
    wxString note = _spool_note_line(sp);
    const int note_max_w = std::max(total_w - text_x - right_pad, ctx->FromDIP(40));
    bool is_note_truncated = note_hardcap_cut || dc.GetTextExtent(note).GetWidth() > note_max_w;
    if (dc.GetTextExtent(note).GetWidth() > note_max_w)
        note = wxControl::Ellipsize(note, dc, wxELLIPSIZE_END, note_max_w);
    if (note_truncated) *note_truncated = is_note_truncated;
    dc.DrawText(note, text_x, bottom_y);

    if (dimmed) {
        if (wxGraphicsContext* gc = wxGraphicsContext::Create(dc)) {
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->SetBrush(wxBrush(dark_row ? wxColour(0, 0, 0, 100) : wxColour(255, 255, 255, 140)));
            gc->DrawRectangle(0, 0, total_w, total_h);
            delete gc;
        }
    }

    dc.SelectObject(wxNullBitmap);
    return bmp;
}


// Fill `combo` with a two-section grouped layout:
//
//   ── Filament Manager ──
//      <brand> (N)  ▶  → [chip] name                   Ng   (DISABLED)
//                         Note: <note>
//   ── System presets ──
//      <vendor>     ▶  → <preset alias>...
//
// The "(N)" suffix on Filament Manager group keys prevents DropDown's
// top-level de-duplication from merging same-brand rows across sections.
namespace {

constexpr size_t AMS_RECENT_FILAMENT_PRESETS_MAX = 5;
constexpr const char* AMS_RECENT_FILAMENT_PRESETS_KEY =
    "ams_recent_filament_presets";

std::vector<wxString> parse_ams_recent_filament_presets(
    const std::string& value)
{
    std::vector<wxString> result;
    std::stringstream stream(value);
    std::string item;

    while (std::getline(stream, item, '\n')) {
        if (!item.empty())
            result.emplace_back(from_u8(item));
    }

    return result;
}

std::string serialize_ams_recent_filament_presets(
    const std::vector<wxString>& items)
{
    std::string value;

    for (const wxString& item : items) {
        if (item.empty())
            continue;

        if (!value.empty())
            value += '\n';

        value += into_u8(item);
    }

    return value;
}

std::vector<wxString> load_ams_recent_filament_presets()
{
    return parse_ams_recent_filament_presets(
        wxGetApp().app_config->get(
            AMS_RECENT_FILAMENT_PRESETS_KEY));
}

void remember_ams_recent_filament_preset(
    const wxString& selected)
{
    if (selected.empty())
        return;

    std::vector<wxString> items =
        load_ams_recent_filament_presets();

    items.erase(
        std::remove(items.begin(), items.end(), selected),
        items.end());

    items.insert(items.begin(), selected);

    if (items.size() > AMS_RECENT_FILAMENT_PRESETS_MAX)
        items.resize(AMS_RECENT_FILAMENT_PRESETS_MAX);

    wxGetApp().app_config->set(
        AMS_RECENT_FILAMENT_PRESETS_KEY,
        serialize_ams_recent_filament_presets(items));
}

} // namespace

static void _populate_filament_combobox_grouped(
    ::ComboBox*                                    combo,
    const wxArrayString&                           filament_items,
    const std::unordered_map<wxString, wxString>&  query_filament_vendors,
    const std::unordered_map<wxString, wxString>&  query_filament_types,
    const std::vector<wxString>&                   recent_filament_items,
    std::map<int, std::string>*                    out_idx_to_spool_id = nullptr)
{
    if (!combo) return;

    const wxString other_bucket = _L("Other");
    static const std::vector<wxString> priority_brands{ "Bambu Lab", "Generic", "Polymaker" };
    static const std::vector<wxString> sorted_types{ "PLA", "PETG", "ABS", "TPU" };

    // ── Section 2 (System presets): bucket aliases by vendor ──
    std::map<wxString, std::vector<wxString>> brand_to_aliases;
    for (const wxString& alias : filament_items) {
        wxString vendor;
        auto it = query_filament_vendors.find(alias);
        if (it != query_filament_vendors.end()) vendor = it->second;
        if (vendor.IsEmpty()) vendor = other_bucket;
        brand_to_aliases[vendor].push_back(alias);
    }
    auto _intra_bucket_sorter = [&query_filament_types](const wxString& l, const wxString& r) -> bool {
        {
            const std::vector<std::string>& sorted_names = get_filament_orders();
            auto i1 = std::find(sorted_names.cbegin(), sorted_names.cend(), l);
            auto i2 = std::find(sorted_names.cbegin(), sorted_names.cend(), r);
            if (i1 != i2) return std::distance(i1, i2) > 0;
        }
        {
            wxString lt, rt;
            auto il = query_filament_types.find(l);
            auto ir = query_filament_types.find(r);
            if (il != query_filament_types.end()) lt = il->second;
            if (ir != query_filament_types.end()) rt = ir->second;
            static const std::vector<wxString> st{ "PLA", "PETG", "ABS", "TPU" };
            auto i1 = std::find(st.begin(), st.end(), lt);
            auto i2 = std::find(st.begin(), st.end(), rt);
            if (i1 != i2) return i1 < i2;
        }
        return l < r;
    };
    for (auto& kv : brand_to_aliases)
        std::sort(kv.second.begin(), kv.second.end(), _intra_bucket_sorter);

    // Keep recently used system presets in their shared recency order and
    // remove them from the vendor buckets to avoid duplicate entries.
    std::vector<wxString> recent_aliases;

    for (const wxString& recent : recent_filament_items) {
        for (auto& [vendor, aliases] : brand_to_aliases) {
            auto alias_it =
                std::find(aliases.begin(), aliases.end(), recent);

            if (alias_it == aliases.end())
                continue;

            recent_aliases.emplace_back(*alias_it);
            aliases.erase(alias_it);
            break;
        }
    }

    for (auto it = brand_to_aliases.begin();
         it != brand_to_aliases.end();) {
        if (it->second.empty())
            it = brand_to_aliases.erase(it);
        else
            ++it;
    }

    std::vector<wxString> ordered_brands;
    for (const wxString& b : priority_brands)
        if (brand_to_aliases.count(b)) ordered_brands.push_back(b);
    for (const auto& kv : brand_to_aliases) {
        if (kv.first == other_bucket) continue;
        if (std::find(priority_brands.begin(), priority_brands.end(), kv.first) != priority_brands.end()) continue;
        ordered_brands.push_back(kv.first);
    }
    auto tail_begin = ordered_brands.begin() + (int)std::count_if(
        priority_brands.begin(), priority_brands.end(),
        [&](const wxString& b){ return brand_to_aliases.count(b) > 0; });
    std::sort(tail_begin, ordered_brands.end());
    if (brand_to_aliases.count(other_bucket)) ordered_brands.push_back(other_bucket);

    // ── Section 1 (Filament Manager): bucket spools by brand ──
    std::map<wxString, std::vector<Slic3r::GUI::FilamentSpool>> lib_brand_to_spools;
    std::vector<Slic3r::GUI::FilamentSpool> unsupported_spools;
    if (auto* store = wxGetApp().fila_manager_store()) {
        BOOST_LOG_TRIVIAL(info) << "[AMSMaterialsSetting] fila_manager_store spool count=" << store->all_spool_ids().size();
        const auto* bundle = wxGetApp().preset_bundle;
        for (const auto& spool_id : store->all_spool_ids()) {
            const Slic3r::GUI::FilamentSpool* sp = store->get_spool(spool_id);
            if (!sp) continue;
            bool has_preset = bundle &&
                bundle->get_filament_by_filament_id(sp->setting_id).has_value();
            if (has_preset) {
                wxString brand = sp->brand.empty() ? other_bucket : wxString::FromUTF8(sp->brand);
                lib_brand_to_spools[brand].push_back(*sp);
            } else {
                unsupported_spools.push_back(*sp);
            }
        }
    }
    for (auto& kv : lib_brand_to_spools)
        std::sort(kv.second.begin(), kv.second.end(),
            [](const Slic3r::GUI::FilamentSpool& a, const Slic3r::GUI::FilamentSpool& b){
                return a.series < b.series; });

    std::vector<wxString> ordered_lib_brands;
    for (const wxString& b : priority_brands)
        if (lib_brand_to_spools.count(b)) ordered_lib_brands.push_back(b);
    for (const auto& kv : lib_brand_to_spools) {
        if (kv.first == other_bucket) continue;
        if (std::find(priority_brands.begin(), priority_brands.end(), kv.first) != priority_brands.end()) continue;
        ordered_lib_brands.push_back(kv.first);
    }
    auto lib_tail = ordered_lib_brands.begin() + (int)std::count_if(
        priority_brands.begin(), priority_brands.end(),
        [&](const wxString& b){ return lib_brand_to_spools.count(b) > 0; });
    std::sort(lib_tail, ordered_lib_brands.end());
    if (lib_brand_to_spools.count(other_bucket)) ordered_lib_brands.push_back(other_bucket);

    combo->Clear();

    // Section 1
    combo->Append(_L("Filament Manager"), wxNullBitmap, DD_ITEM_STYLE_SPLIT_ITEM);
    if (!ordered_lib_brands.empty() || !unsupported_spools.empty()) {
        const int row_width = std::min(
            std::max(combo->GetSize().GetWidth(), combo->FromDIP(260)),
            combo->FromDIP(360)
        );
        for (const wxString& brand : ordered_lib_brands) {
            const auto& spools = lib_brand_to_spools[brand];
            const wxString group_key = wxString::Format("%s (%d)", brand,
                                                        static_cast<int>(spools.size()));
            for (const Slic3r::GUI::FilamentSpool& sp : spools) {
                bool note_truncated = false;
                wxBitmap row_bmp = _render_spool_row_bitmap(combo, sp, row_width, sp.in_printer, &note_truncated);
                const int spool_style = sp.in_printer ? DD_ITEM_STYLE_DISABLED : 0;
                wxString spool_display_text = _spool_display_name(sp);
                if (!sp.color_name.empty())
                    spool_display_text += " " + wxString::FromUTF8(sp.color_name);
                int idx = combo->Append(spool_display_text, row_bmp, group_key, nullptr, spool_style);
                if (idx >= 0) {
                    if (out_idx_to_spool_id)
                        (*out_idx_to_spool_id)[idx] = sp.spool_id;
                    wxString tip;
                    if (sp.in_printer) {
                        wxString dev_display;
                        if (!sp.device_name.empty()) {
                            dev_display = wxString::FromUTF8(sp.device_name);
                        } else if (!sp.dev_id.empty()) {
                            const wxString id = wxString::FromUTF8(sp.dev_id);
                            dev_display = id.length() > 5
                                ? id.Left(3) + "***" + id.Right(2)
                                : "***";
                        }
                        tip = dev_display.empty()
                            ? _L("In printer")
                            : wxString::Format(_L("In printer: %s"), dev_display);
                        if (!sp.slot_id.empty()) {
                            wxString slot_label;
                            if (sp.ams_id >= 0) {
                                try {
                                    int tray_id = sp.ams_id * 4 + std::stoi(sp.slot_id);
                                    slot_label = wxGetApp().transition_tridid(tray_id);
                                } catch (...) {}
                            }
                            if (slot_label.empty())
                                slot_label = wxString::FromUTF8(sp.slot_id);
                            tip += wxString::Format(", %s", slot_label);
                        }
                    }
                    if (note_truncated && !sp.note.empty()) {
                        if (!tip.empty()) tip += "\n";
                        tip += wxString(_CTX_utf8(L_CONTEXT("Note","Filament Manager"),"Filament Manager")) + ": " + wxString::FromUTF8(sp.note);
                    }
                    if (!tip.empty())
                        combo->SetItemTooltip(static_cast<unsigned int>(idx), tip);
                }
            }
        }
        const wxString unsupported_group = _L("Unsupported Filaments");
        for (const Slic3r::GUI::FilamentSpool& sp : unsupported_spools) {
            bool note_truncated = false;
            const bool is_unsupported = true;
            wxBitmap row_bmp = _render_spool_row_bitmap(combo, sp, row_width, sp.in_printer || is_unsupported, &note_truncated);
            const int spool_style = DD_ITEM_STYLE_DISABLED;
            wxString unsupported_display_text = _spool_display_name(sp);
            if (!sp.color_name.empty())
                unsupported_display_text += " " + wxString::FromUTF8(sp.color_name);
            int idx = combo->Append(unsupported_display_text, row_bmp, unsupported_group, nullptr, spool_style);
            if (idx >= 0) {
                wxString tip;
                if (sp.in_printer) {
                    wxString dev_display;
                    if (!sp.device_name.empty()) {
                        dev_display = wxString::FromUTF8(sp.device_name);
                    } else if (!sp.dev_id.empty()) {
                        const wxString id = wxString::FromUTF8(sp.dev_id);
                        dev_display = id.length() > 5
                            ? id.Left(3) + "***" + id.Right(2)
                            : "***";
                    }
                    tip = dev_display.empty()
                        ? _L("In printer")
                        : wxString::Format(_L("In printer: %s"), dev_display);
                    if (!sp.slot_id.empty()) {
                        wxString slot_label;
                        if (sp.ams_id >= 0) {
                            try {
                                int tray_id = sp.ams_id * 4 + std::stoi(sp.slot_id);
                                slot_label = wxGetApp().transition_tridid(tray_id);
                            } catch (...) {}
                        }
                        if (slot_label.empty())
                            slot_label = wxString::FromUTF8(sp.slot_id);
                        tip += wxString::Format(", %s", slot_label);
                    }
                }
                if (note_truncated && !sp.note.empty()) {
                    if (!tip.empty()) tip += "\n";
                    tip += wxString(_CTX_utf8(L_CONTEXT("Note","Unsupported Filaments"),"Unsupported Filaments")) + ": " + wxString::FromUTF8(sp.note);
                }
                if (!tip.empty())
                    combo->SetItemTooltip(static_cast<unsigned int>(idx), tip);
            }
        }
        if (unsupported_spools.empty())
            combo->Append(_L("No unsupported filaments"),
                          wxNullBitmap, unsupported_group, nullptr, DD_ITEM_STYLE_DISABLED);
    } else {
        combo->Append(_L("No filaments"),
                      wxNullBitmap, wxEmptyString, nullptr, DD_ITEM_STYLE_DISABLED);
    }

    // Section 2
    if (!recent_aliases.empty() || !ordered_brands.empty()) {
        combo->Append(
            _L("System presets"),
            wxNullBitmap,
            DD_ITEM_STYLE_SPLIT_ITEM);

        if (!recent_aliases.empty()) {
            const wxString recent_group = _L("Recently used");

            for (const wxString& alias : recent_aliases) {
                combo->Append(
                    alias,
                    wxNullBitmap,
                    recent_group,
                    nullptr,
                    0);
            }
        }

        for (const wxString& brand : ordered_brands) {
            for (const wxString& alias : brand_to_aliases[brand]) {
                combo->Append(
                    alias,
                    wxNullBitmap,
                    brand,
                    nullptr,
                    0);
            }
        }
    }
}

static void _collect_filament_info(const wxString& shown_name,
                                   const Preset& filament,
                                   std::unordered_map<wxString, wxString>& query_filament_vendors,
                                   std::unordered_map<wxString, wxString>& query_filament_types)
{
    query_filament_vendors[shown_name] = filament.config.get_filament_vendor();
    query_filament_types[shown_name] = filament.config.get_filament_type();
}

void AMSMaterialsSetting::get_filaments_info(const MachineObject*                     obj,
                                             const std::string&                       nozzle_diameter_str,
                                             wxArrayString&                           filament_items,
                                             std::map<std::string, FilamentInfos>&    map_filament_items,
                                             std::unordered_map<wxString, wxString>&  query_filament_vendors,
                                             std::unordered_map<wxString, wxString>&  query_filament_types)
{
    if (!obj) return;

    PresetBundle *  preset_bundle = wxGetApp().preset_bundle;
    if (!preset_bundle) return;

    auto& filaments = preset_bundle->filaments;
    BOOST_LOG_TRIVIAL(trace) << "system_preset_bundle filament number=" << filaments.size();

    auto printer_names = preset_bundle->get_printer_names_by_printer_type_and_nozzle(
        DevPrinterConfigUtil::get_printer_display_name(obj->printer_type), nozzle_diameter_str);

    auto collect_pass = [&](bool want_system) {
        std::set<std::string> filament_id_set; // Deduplicate by filament_id.
        for (auto filament_it = filaments.begin(); filament_it != filaments.end(); ++filament_it) {
            Preset& preset = *filament_it;
            // Skip non-root presets.
            if (filaments.get_preset_base(preset) != &preset) continue;
            // First pass: only system presets. Second pass: only user presets, and

            // skip user fila in system preset mode
            if (want_system && !preset.is_system) continue;
            // skip system fila in user preset mode
            if (!want_system && preset.is_system) continue;

            ConfigOption*        printer_opt  = filament_it->config.option("compatible_printers");
            ConfigOptionStrings* printer_strs = dynamic_cast<ConfigOptionStrings*>(printer_opt);
            if (!printer_strs) continue;

            for (const auto& printer_str : printer_strs->values) {
                if (printer_names.find(printer_str) == printer_names.end()) continue;
                // This preset is compatible with the current printer. Append it to the out containers, if not duplicated.
                if (filament_it->filament_id.empty()) break;
                if (!filament_id_set.insert(filament_it->filament_id).second) break;

                auto fialment_alias = filaments.get_preset_alias(preset, true);
                if (fialment_alias.empty()) break;

                filament_items.push_back(from_u8(fialment_alias));
                _collect_filament_info(fialment_alias, preset, query_filament_vendors, query_filament_types);

                FilamentInfos filament_infos;
                filament_infos.filament_id         = filament_it->filament_id;
                filament_infos.setting_id          = filament_it->setting_id;
                map_filament_items[fialment_alias] = filament_infos;
                break;
            }
        }
    };

    // First pass: system presets.
    collect_pass(true);
    // Second pass: user presets.
    if (obj->is_support_user_preset) {
        collect_pass(false);
    }
}

Preset* AMSMaterialsSetting::get_filament_by_id(const std::string& filament_id, bool is_system)
{
    PresetBundle *  preset_bundle = wxGetApp().preset_bundle;
    if (!preset_bundle) return nullptr;

    if (filament_id.empty()) return nullptr;
    auto& filaments = preset_bundle->filaments;
    for (auto it = filaments.begin(); it != filaments.end(); ++it) {
        Preset& preset = *it;
        if (filaments.get_preset_base(preset) != &preset) continue;
        if (is_system && !preset.is_system) continue;
        if (preset.filament_id == filament_id) return &preset;
    }
    return nullptr;
}

void AMSMaterialsSetting::Popup(wxString filament, wxString sn, wxString temp_min, wxString temp_max, wxString k, wxString n)
{
    if (!obj) return;
    update_widgets();
    // set default value
    if (k.IsEmpty())
        k = "0.000";
    if (n.IsEmpty())
        n = "0.000";

    m_input_k_val->GetTextCtrl()->SetValue(k);
    m_input_n_val->GetTextCtrl()->SetValue(n);

    wxArrayString filament_items;
    wxString bambu_filament_name;
    wxString hint_filament_name; // the hint type to be selected
    std::unordered_map<wxString, wxString> query_filament_vendors;// some information for sort
    std::unordered_map<wxString, wxString> query_filament_types;  //

    PresetBundle *        preset_bundle = wxGetApp().preset_bundle;
    std::ostringstream    stream;
    // TODO: fila_switcher broken the connection of ams->extruder
    int extruder_id = obj->GetFilaSystem()->GetExtruderIdByAmsId(std::to_string(ams_id));
    if (!obj->GetExtderSystem()->GetExtderById(extruder_id))
    {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " get extruder id failed";
        extruder_id = 0;
    }
    stream << std::fixed << std::setprecision(1) << obj->GetExtderSystem()->GetNozzleDiameter(extruder_id);
    std::string nozzle_diameter_str = stream.str();

    get_filaments_info(obj, nozzle_diameter_str, filament_items, map_filament_items, query_filament_vendors, query_filament_types);

    if (Preset* ams_fila_it = get_filament_by_id(ams_filament_id, !m_is_third)) {
        auto fialment_alias = preset_bundle->filaments.get_preset_alias(*ams_fila_it, true);
        if (!fialment_alias.empty()) {
            hint_filament_name  = from_u8(fialment_alias);
            bambu_filament_name = from_u8(fialment_alias);
        }

        // update if nozzle_temperature_range is found
        if (ConfigOption *opt_min = ams_fila_it->config.option("nozzle_temperature_range_low")) {
            ConfigOptionInts *opt_min_ints = dynamic_cast<ConfigOptionInts *>(opt_min);
            if (opt_min_ints) {
                wxString text_nozzle_temp_min = wxString::Format("%d", opt_min_ints->get_at(0));
                m_input_nozzle_min->GetTextCtrl()->SetValue(text_nozzle_temp_min);
            }
        }
        if (ConfigOption *opt_max = ams_fila_it->config.option("nozzle_temperature_range_high")) {
            ConfigOptionInts *opt_max_ints = dynamic_cast<ConfigOptionInts *>(opt_max);
            if (opt_max_ints) {
                wxString text_nozzle_temp_max = wxString::Format("%d", opt_max_ints->get_at(0));
                m_input_nozzle_max->GetTextCtrl()->SetValue(text_nozzle_temp_max);
            }
        }
    }

    if (!sn.empty()) {
        m_sn_number->SetLabel(sn);
        m_panel_SN->Show();
    }
    else {
        m_panel_SN->Hide();
    }

    if (obj) {
        if (!m_is_third) {
            m_comboBox_filament->Hide();
            m_readonly_filament->Show();
            if (bambu_filament_name.empty()) {
                m_readonly_filament->SetLabel("Bambu " + filament);
            }
            else {
                m_readonly_filament->SetLabel(bambu_filament_name);
            }

            m_input_nozzle_min->GetTextCtrl()->SetValue(temp_min);
            m_input_nozzle_max->GetTextCtrl()->SetValue(temp_max);
        }
        else {
            m_comboBox_filament->Show();
            m_readonly_filament->Hide();
        }

        if (obj->GetCalib()->IsVersionInited()) {
            m_title_pa_profile->Show();
            m_comboBox_cali_result->Show();
            m_input_k_val->Disable();
        }
        else {
            m_title_pa_profile->Hide();
            m_comboBox_cali_result->Hide();
            m_input_k_val->Enable();
        }

        m_button_reset->Show();
        //m_button_confirm->Show();
    }


    // Windows/Linux: two-section grouped layout ── Filament Manager / System presets.
    // _populate_filament_combobox_grouped fills the ComboBox directly; the
    // caller (on_select_filament) uses map_filament_items[GetValue()] to look
    // up the selected preset, so we must still find the selection index for
    // the hint via a linear scan of the alias list.
    m_combo_idx_to_spool_id.clear();
    m_selected_spool_id.clear();

    const std::vector<wxString> recent_filament_items =
        load_ams_recent_filament_presets();

    _populate_filament_combobox_grouped(m_comboBox_filament,
                                        filament_items,
                                        query_filament_vendors,
                                        query_filament_types,
                                        recent_filament_items,
                                        &m_combo_idx_to_spool_id);

    // If the current tray belongs to a Filament Manager spool, prefer selecting
    // that spool's combo entry (identified via tag_uid) over the preset-alias
    // hint.  The spool label is "series color_name" which never matches
    // hint_filament_name (the preset alias), so without this the selection falls
    // back to -1 and the color picker is never updated from the spool data.
    int selection_idx = -1;
    if (auto tray_opt = obj->get_tray(std::to_string(ams_id), std::to_string(slot_id))) {
        auto* store = wxGetApp().fila_manager_store();
        const FilamentSpool* sp = nullptr;
        // Prefer exact RFID match; fall back to setting_id+color for cloud-only spools.
        if (!tray_opt->tag_uid.empty() && store)
            sp = store->find_by_tag_uid(tray_opt->tag_uid);
        if (!sp && store && !tray_opt->setting_id.empty())
            sp = store->find_by_setting_and_color(tray_opt->setting_id, tray_opt->color);
        if (sp) {
            for (const auto& kv : m_combo_idx_to_spool_id) {
                if (kv.second == sp->spool_id) {
                    selection_idx = kv.first;
                    break;
                }
            }
        }
    }
    // Fall back to preset-alias hint for non-spool (system preset) selections.
    if (selection_idx < 0) {
        for (int i = 0; i < (int)m_comboBox_filament->GetCount(); i++) {
            if (m_comboBox_filament->GetString(i) == hint_filament_name) {
                selection_idx = i;
                break;
            }
        }
    }
    m_comboBox_filament->SetSelection(selection_idx);

    post_select_event(selection_idx);

    if (selection_idx < 0) {
        m_comboBox_filament->SetValue(wxEmptyString);
    }

    // Synchronously set the colour picker's editable state so it is correct
    // before ShowModal() paints the dialog — don't rely on the async posted event.
    {
        const bool is_spool = (selection_idx >= 0 &&
                               m_combo_idx_to_spool_id.count(selection_idx) > 0);
        m_clr_picker->Enable(!is_spool);
    }

    // Set the flag whether to open the filament setting dialog from the device page
    m_comboBox_filament->SetClientData(new int(1));

    // Request PA calibration history if not loaded yet — needed for k-profile dropdown
    if (obj->GetCalib()->IsVersionInited() && !obj->GetCalib()->IsPAHistoryReady()) {
        PACalibExtruderInfo cali_info;
        int ext_id = obj->GetFilaSystem()->GetExtruderIdByAmsId(std::to_string(ams_id));
        if (ext_id > 0) {
            cali_info.nozzle_diameter = obj->GetExtderSystem()->GetNozzleDiameter(ext_id);
            cali_info.use_extruder_id = false;
            cali_info.use_nozzle_volume_type = false;
            CalibUtils::emit_get_PA_calib_infos(cali_info);
            m_pa_data_pending = true;
        }
    } else {
        m_pa_data_pending = false;
    }

    update();
    Layout();
    Fit();
    ShowModal();
}

void AMSMaterialsSetting::post_select_event(int index) {
    wxCommandEvent event(wxEVT_COMBOBOX);
    event.SetInt(index);
    event.SetEventObject(m_comboBox_filament);
    wxPostEvent(m_comboBox_filament, event);
}

void AMSMaterialsSetting::TryRefreshPAProfiles()
{
    if (!m_pa_data_pending || !obj) return;
    if (!obj->GetCalib()->IsPAHistoryReady()) return;

    m_pa_data_pending = false;

    // Re-trigger filament selection to repopulate PA profile dropdown with real data
    int sel = m_comboBox_filament->GetSelection();
    if (sel >= 0) {
        m_comboBox_filament->SetClientData(new int(1));
        post_select_event(sel);
    }
}

void AMSMaterialsSetting::on_select_cali_result(wxCommandEvent &evt)
{
    m_pa_cali_select_id = evt.GetSelection();
    if (m_pa_cali_select_id >= 0 && m_pa_profile_items.size() > m_pa_cali_select_id) {
        m_input_k_val->GetTextCtrl()->SetValue(float_to_string_with_precision(m_pa_profile_items[m_pa_cali_select_id].k_value));
        m_input_n_val->GetTextCtrl()->SetValue(float_to_string_with_precision(m_pa_profile_items[m_pa_cali_select_id].n_coef));
    }
    else{
        m_input_k_val->GetTextCtrl()->SetValue(std::to_string(0.00));
        m_input_n_val->GetTextCtrl()->SetValue(std::to_string(0.00));
    }
}

void AMSMaterialsSetting::on_select_nozzle_pos_id(wxCommandEvent &evt)
{
    int selected_id = evt.GetSelection();

    if(selected_id == -1){
        m_comboBox_cali_result->Disable();
    } else{
        m_comboBox_cali_result->Enable();
        update_pa_profile_items();
    }
}

void AMSMaterialsSetting::update_pa_profile_items()
{
    if (!obj || !obj->GetNozzleSystem()) return;

    wxArrayString items;
    {
        m_pa_profile_items.clear();
        m_comboBox_cali_result->SetValue(wxEmptyString);
        m_input_k_val->GetTextCtrl()->SetValue(wxEmptyString);
        // add default item
        PACalibResult default_item;
        default_item.cali_idx    = -1;
        default_item.filament_id = ams_filament_id;
        if (obj->GetConfig()->SupportCalibrationPA_FlowAuto()) {
            default_item.k_value = -1;
            default_item.n_coef  = -1;
        } else {
            get_default_k_n_value(ams_filament_id, default_item.k_value, default_item.n_coef);
        }
        m_pa_profile_items.emplace_back(default_item);
        items.push_back(_L("Default"));
    }

    auto rack = obj->GetNozzleSystem()->GetNozzleRack();
    auto switcher = obj->GetFilaSwitch();

    std::vector<PACalibResult> cali_history = obj->GetCalib()->GetPAHistory();
    std::sort(cali_history.begin(), cali_history.end(), [](const PACalibResult &left, const PACalibResult &right) { return left.nozzle_pos_id < right.nozzle_pos_id; });

    {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " AMS Calibration Histtory";
        for (auto& cali_item : cali_history) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " cali_item: cali_idx=" << cali_item.cali_idx
                                       << ", name=" << cali_item.name
                                       << ", filament_id=" << cali_item.filament_id
                                       << ", k_value=" << cali_item.k_value
                                       << ", n_coef=" << cali_item.n_coef
                                       << ", nozzle_diameter=" << cali_item.nozzle_diameter
                                       << ", nozzle_pos_id=" << cali_item.nozzle_pos_id
                                       << ", extruder_id=" << cali_item.extruder_id;
        }
    }
    std::set<int> extruder_ids;
    if (ams_id == VIRTUAL_TRAY_MAIN_ID) {
        extruder_ids.insert(MAIN_EXTRUDER_ID);
    } else if (ams_id == VIRTUAL_TRAY_DEPUTY_ID) {
        extruder_ids.insert(DEPUTY_EXTRUDER_ID);
    } else if (DevAms* curr_ams = obj->GetFilaSystem()->GetAmsById(std::to_string(ams_id))) {
        auto extruder_id_set = curr_ams->GetBindedExtruderSet();
        extruder_ids.insert(extruder_id_set.begin(), extruder_id_set.end());
    }

    for (int extruder_id : extruder_ids) {
        NozzleFlowType   nozzle_flow_type   = obj->GetExtderSystem()->GetNozzleFlowType(extruder_id);
        float            nozzle_diameter    = obj->GetExtderSystem()->GetNozzleDiameter(extruder_id);

        if (rack->IsSupported() && (extruder_id == MAIN_EXTRUDER_ID || switcher->IsInstalled())) {
            if (int sel = m_comboBox_nozzle_type->GetSelection(); sel != wxNOT_FOUND) {
                auto sel_pair = (std::pair<NozzleDiameterType, NozzleFlowType>*)m_comboBox_nozzle_type->GetClientData(sel);
                auto nozzle_diameter_type = sel_pair->first;
                if (nozzle_diameter_type != NozzleDiameterType::NONE_DIAMETER_TYPE) {
                    nozzle_diameter = DevNozzle::ToNozzleDiameterFloat(nozzle_diameter_type);
                }
                nozzle_flow_type = sel_pair->second;
            }
        }

        for (auto cali_item : cali_history) {
            // filter avaliable cali_item (PA)
            if (cali_item.filament_id == ams_filament_id
            && cali_item.nozzle_volume_type == DevNozzle::ToNozzleVolumeType(nozzle_flow_type)
            && is_approx(cali_item.nozzle_diameter, nozzle_diameter)
            ) {
                if (obj->is_multi_extruders() && extruder_id != cali_item.extruder_id) {
                    continue;
                }

                if(rack->IsSupported() && extruder_id == MAIN_EXTRUDER_ID)
                {
                    if(cali_item.nozzle_pos_id == 0) {
                        items.push_back(wxString::Format("R | %s", from_u8(cali_item.name)));
                    } else if(cali_item.nozzle_pos_id == 1) {
                        items.push_back(wxString::Format("L | %s", from_u8(cali_item.name)));
                    } else if(cali_item.nozzle_pos_id >= 0x10){
                        items.push_back(wxString::Format("%d | %s", (cali_item.nozzle_pos_id & 0x0f) + 1, from_u8(cali_item.name)));
                    } else {
                        items.push_back(wxString::Format("N/A | %s", from_u8(cali_item.name)));
                        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << "Nozzle position id is -1 or invalid.";
                    }
                } else {
                    items.push_back(wxString::Format("%s", from_u8(cali_item.name)));
                }

                m_pa_profile_items.push_back(cali_item);
            }
        }
    }

    m_comboBox_cali_result->Set(items);
    m_comboBox_cali_result->SetSelection(0);
}

void AMSMaterialsSetting::update_nozzle_combo(MachineObject* obj){
    if(!obj || !obj->GetNozzleSystem()) return;

    auto rack = obj->GetNozzleSystem()->GetNozzleRack();
    auto switcher = obj->GetFilaSwitch();

    std::set<int> extruder_ids;
    if (ams_id == VIRTUAL_TRAY_MAIN_ID) {
        extruder_ids.insert(MAIN_EXTRUDER_ID);
    } else if (ams_id == VIRTUAL_TRAY_DEPUTY_ID && !switcher->IsInstalled()) {
        extruder_ids.insert(DEPUTY_EXTRUDER_ID);
    } else if (DevAms* curr_ams = obj->GetFilaSystem()->GetAmsById(std::to_string(ams_id))) {
        auto extruder_id_set = curr_ams->GetBindedExtruderSet();
        extruder_ids.insert(extruder_id_set.begin(), extruder_id_set.end());
    }

    std::set<int> allow_extruder_ids{MAIN_EXTRUDER_ID};
    if (switcher->IsInstalled()) {
        allow_extruder_ids.insert(extruder_ids.begin(), extruder_ids.end());
    }

    if(rack->IsSupported() && extruder_ids == allow_extruder_ids) {
        int r_nozzle_id = obj->GetExtderSystem()->GetExtderById(MAIN_EXTRUDER_ID)->GetNozzleId();
        auto r_nozzle = obj->GetNozzleSystem()->GetExtNozzle(r_nozzle_id);
        auto nozzle_map = rack->GetRackNozzles();

        std::set<std::pair<NozzleDiameterType, NozzleFlowType>> nozzle_type_set;
        if(r_nozzle.IsNormal()){ // Add the nozzle of the right toolhead
            nozzle_type_set.insert(std::make_pair(r_nozzle.GetNozzleDiameterType(), r_nozzle.GetNozzleFlowType()));
        }
        if (switcher->IsInstalled() && ams_id != VIRTUAL_TRAY_MAIN_ID) { // Add the nozzle of the left toolhead
            int l_nozzle_id = obj->GetExtderSystem()->GetExtderById(DEPUTY_EXTRUDER_ID)->GetNozzleId();;
            auto l_nozzle   = obj->GetNozzleSystem()->GetExtNozzle(l_nozzle_id);
            if (l_nozzle.IsNormal())
                nozzle_type_set.insert(std::make_pair(l_nozzle.GetNozzleDiameterType(), l_nozzle.GetNozzleFlowType()));
        }
        for (auto &nozzle : nozzle_map) { // Add the nozzle of the rack
            if (nozzle.second.IsNormal()) {
                nozzle_type_set.insert(std::make_pair(nozzle.second.GetNozzleDiameterType(), nozzle.second.GetNozzleFlowType()));
            }
        }

        std::vector<std::pair<NozzleDiameterType, NozzleFlowType>> nozzle_type_vec(nozzle_type_set.begin(), nozzle_type_set.end());
        std::sort(nozzle_type_vec.begin(), nozzle_type_vec.end(), [](const std::pair<NozzleDiameterType, NozzleFlowType> & left, const std::pair<NozzleDiameterType, NozzleFlowType>& right) -> bool {
            if(left.first == right.first) {
                return left.second < right.second;
            } else {
                return left.first < right.first;
            }
        });

        /* make nozzle type combobox item */
        m_comboBox_nozzle_type->Clear();
        for(auto pair : nozzle_type_vec){
            wxString item = DevNozzle::ToNozzleDiameterStr(pair.first);
            item += " ";
            item += DevNozzle::GetNozzleFlowTypeStr(pair.second);
            m_comboBox_nozzle_type->Append(item, wxNullBitmap, new std::pair<NozzleDiameterType, NozzleFlowType>(pair));
        }
        m_title_nozzle_type->Show();
        m_comboBox_nozzle_type->Show();

        /* set nozzle pos tooltip */
        auto font = m_title_pa_profile->GetFont();
        font.SetUnderlined(true);
        m_title_pa_profile->SetFont(font);
        {
            std::string ams_mat_pt = wxGetApp().preset_bundle->printers.get_edited_preset().get_printer_type(wxGetApp().preset_bundle);
            wxString ams_ext_name = _L(DevPrinterConfigUtil::get_toolhead_display_name(ams_mat_pt, MAIN_EXTRUDER_ID, ToolHeadComponent::Extruder, ToolHeadNameCase::LowerCase));
            m_title_pa_profile->SetToolTip(wxString::Format(_L("Note: The hotend number on the %s is tied to the holder. When the hotend is moved to a new holder, its number will update automatically."), ams_ext_name));
        }
    } else{
        m_title_nozzle_type->Hide();
        m_comboBox_nozzle_type->Hide();

        auto font = m_title_pa_profile->GetFont();
        font.SetUnderlined(false);
        m_title_pa_profile->SetFont(font);
        m_title_pa_profile->SetToolTip(wxEmptyString);
    }

    Layout();
    Fit();
}

int AMSMaterialsSetting::get_nozzle_combo_id_code() const{
    auto sel = m_comboBox_nozzle_type->GetSelection();
    if (sel != wxNOT_FOUND) return *(reinterpret_cast<int*>(m_comboBox_nozzle_type->GetClientData(sel)));

    return -1;
}

int AMSMaterialsSetting::get_nozzle_sel_by_sn(MachineObject* obj, const std::string& sn){
    if(!obj) return -1;

    auto nozzle = obj->get_nozzle_by_sn(sn);

    for(unsigned int i = 0; i<m_comboBox_nozzle_type->GetCount(); i++){
        auto sel_pair = (std::pair<NozzleDiameterType, NozzleFlowType>*)m_comboBox_nozzle_type->GetClientData(i);

        if(sel_pair->first == nozzle.GetNozzleDiameterType() && sel_pair->second == nozzle.GetNozzleFlowType())
            return i;
    }

    return -1;
}

int AMSMaterialsSetting::get_cali_index_by_ams_slot(MachineObject *obj, int ams_id, int slot_id)
{
    if (!obj) return -1;

    // Get the flag whether to open the filament setting dialog from the device page
    int *from_printer = static_cast<int *>(m_comboBox_filament->GetClientData());
    if (!from_printer || (*from_printer != 1)) return -1;

    if (obj->GetCalib()->IsVersionInited()) {
        if (ams_id == VIRTUAL_TRAY_MAIN_ID || ams_id == VIRTUAL_TRAY_DEPUTY_ID) {
            for (auto slot : obj->vt_slot) {
                if (slot.id == std::to_string(ams_id)) return slot.cali_idx;
            }
        } else {
            DevAmsTray *selected_tray = obj->GetFilaSystem()->GetAmsTray(std::to_string(ams_id), std::to_string(slot_id));
            if (!selected_tray) {
                return -1;
            } else {
                return selected_tray->cali_idx;
            }
        }
    }

    return -1;
}

void AMSMaterialsSetting::on_select_filament(wxCommandEvent &evt)
{
    // Get the flag whether to open the filament setting dialog from the device page
    int* from_printer = static_cast<int*>(m_comboBox_filament->GetClientData());

    m_filament_type = "";
    PresetBundle* preset_bundle = wxGetApp().preset_bundle;
    if (preset_bundle) {
        std::ostringstream stream;
        if (obj)
            stream << std::fixed << std::setprecision(1) << obj->GetExtderSystem()->GetNozzleDiameter(0);
        std::string nozzle_diameter_str = stream.str();
        std::set<std::string> printer_names = preset_bundle->get_printer_names_by_printer_type_and_nozzle(DevPrinterConfigUtil::get_printer_display_name(obj->printer_type),
                                                                                                          nozzle_diameter_str);
        for (auto it = preset_bundle->filaments.begin(); it != preset_bundle->filaments.end(); it++) {
            if (!m_comboBox_filament->GetValue().IsEmpty()) {
                auto filament_item = map_filament_items[into_u8(m_comboBox_filament->GetValue())];
                std::string filament_id   = filament_item.filament_id;

                if (it->filament_id.compare(filament_id) == 0) {
                    bool has_compatible_printer = false;
                    ConfigOption* printer_opt = it->config.option("compatible_printers");
                    ConfigOptionStrings* fila_compatible_printer_strs = dynamic_cast<ConfigOptionStrings*>(printer_opt);
                    if (fila_compatible_printer_strs) {
                        for (const auto& fila_compatible_printer_str : fila_compatible_printer_strs->values) {
                            for (std::string printer_name : printer_names) {
                                if (fila_compatible_printer_str.find(printer_name) != std::string::npos) {
                                    has_compatible_printer = true;
                                    break;
                                }
                            }

                            if (has_compatible_printer) {
                                break;
                            }
                        }
                    }

                    if (!it->is_system && !has_compatible_printer) continue;
                    // ) if nozzle_temperature_range is found
                    ConfigOption* opt_min = it->config.option("nozzle_temperature_range_low");
                    if (opt_min) {
                        ConfigOptionInts* opt_min_ints = dynamic_cast<ConfigOptionInts*>(opt_min);
                        if (opt_min_ints) {
                            wxString text_nozzle_temp_min = wxString::Format("%d", opt_min_ints->get_at(0));
                            m_input_nozzle_min->GetTextCtrl()->SetValue(text_nozzle_temp_min);
                        }
                    }
                    ConfigOption* opt_max = it->config.option("nozzle_temperature_range_high");
                    if (opt_max) {
                        ConfigOptionInts* opt_max_ints = dynamic_cast<ConfigOptionInts*>(opt_max);
                        if (opt_max_ints) {
                            wxString text_nozzle_temp_max = wxString::Format("%d", opt_max_ints->get_at(0));
                            m_input_nozzle_max->GetTextCtrl()->SetValue(text_nozzle_temp_max);
                        }
                    }
                    ConfigOption* opt_type = it->config.option("filament_type");
                    bool found_filament_type = false;
                    if (opt_type) {
                        ConfigOptionStrings* opt_type_strs = dynamic_cast<ConfigOptionStrings*>(opt_type);
                        if (opt_type_strs) {
                            found_filament_type = true;
                            //m_filament_type = opt_type_strs->get_at(0);
                            std::string display_filament_type;
                            m_filament_type = it->config.get_filament_type(display_filament_type);
                        }
                    }
                    if (!found_filament_type)
                        m_filament_type = "";

                    break;
                }
            }
        }
    }
    if (m_input_nozzle_min->GetTextCtrl()->GetValue().IsEmpty()) {
         m_input_nozzle_min->GetTextCtrl()->SetValue("0");
    }
    if (m_input_nozzle_max->GetTextCtrl()->GetValue().IsEmpty()) {
         m_input_nozzle_max->GetTextCtrl()->SetValue("0");
    }

    m_filament_selection = evt.GetSelection();

    // Track whether the selected item is a Filament Manager spool.
    {
        auto it = m_combo_idx_to_spool_id.find(m_filament_selection);
        m_selected_spool_id = (it != m_combo_idx_to_spool_id.end()) ? it->second : std::string{};
    }
    // Color is owned by the filament manager spool — disable editing.
    m_clr_picker->Enable(m_selected_spool_id.empty());

    //reset cali
    int cali_select_idx = -1;

    update_nozzle_combo(obj);

    if ( !this->obj || m_filament_selection < 0) {
        m_input_k_val->Enable(false);
        m_input_n_val->Enable(false);
        m_button_confirm->Disable();
        m_button_confirm->SetBackgroundColor(wxColour(0x90, 0x90, 0x90));
        m_button_confirm->SetBorderColor(wxColour(0x90, 0x90, 0x90));
        m_comboBox_cali_result->Clear();
        m_comboBox_cali_result->SetValue(wxEmptyString);
        m_comboBox_nozzle_type->Clear();
        m_comboBox_nozzle_type->SetValue(wxEmptyString);
        m_input_k_val->GetTextCtrl()->SetValue(wxEmptyString);
        m_input_n_val->GetTextCtrl()->SetValue(wxEmptyString);
        m_comboBox_filament->SetClientData(new int(0));
        return;
    }
    else {
        m_button_confirm->SetBackgroundColor(m_btn_bg_green);
        m_button_confirm->SetBorderColor(wxColour(0, 174, 66));
        m_button_confirm->SetTextColor(wxColour("#FFFFFE"));
        m_button_confirm->Enable(true);
    }

    //filament id
    ams_filament_id = "";
    ams_setting_id = "";

    // Fast path for Filament Manager spool: look up preset via spool's setting_id
    // instead of relying on GetValue() (which returns wxEmptyString for bitmap-only items).
    if (!m_selected_spool_id.empty()) {
        auto* store = wxGetApp().fila_manager_store();
        const FilamentSpool* sp = store ? store->get_spool(m_selected_spool_id) : nullptr;
        if (sp && preset_bundle) {
            auto fila_info = preset_bundle->get_filament_by_filament_id(sp->setting_id);
            if (fila_info.has_value()) {
                ams_filament_id = fila_info->filament_id;
                ams_setting_id  = fila_info->setting_id;
                m_input_nozzle_min->GetTextCtrl()->SetValue(
                    wxString::Format("%d", fila_info->nozzle_temp_range_low));
                m_input_nozzle_max->GetTextCtrl()->SetValue(
                    wxString::Format("%d", fila_info->nozzle_temp_range_high));
                m_filament_type = fila_info->filament_type;
            }
            // Use spool's stored color (user-recorded), not preset default.
            // Use _parse_hex_color to tolerate both "#RRGGBB" and bare "RRGGBB"
            // storage formats, since cloud/AMS sync paths may omit the '#' prefix.
            set_ctype(sp->color_type);
            if (!sp->colors.empty()) {
                std::vector<wxColour> cols;
                cols.reserve(sp->colors.size());
                for (const auto& hex : sp->colors)
                    cols.push_back(_parse_hex_color(hex));
                set_colors(cols);
            } else if (!sp->color_code.empty()) {
                set_color(_parse_hex_color(sp->color_code));
            }
        }
        // Skip the generic GetValue()-keyed lookup below — it won't find anything
        // for bitmap-only spool items whose text is wxEmptyString.
    } else if (preset_bundle) {
        for (auto it = preset_bundle->filaments.begin(); it != preset_bundle->filaments.end(); it++) {
            auto itor = map_filament_items.find(into_u8(m_comboBox_filament->GetValue()));
            if ( itor != map_filament_items.end()) {
                ams_filament_id = itor->second.filament_id;
                ams_setting_id  = itor->second.setting_id;
                break;
            }

            if (it->alias.compare(into_u8(m_comboBox_filament->GetValue())) == 0) {
                ams_filament_id = it->filament_id;
                ams_setting_id = it->setting_id;
                break;
            }
        }
    }

    auto get_cali_index = [this](const std::string& str) -> int{
        for (int i = 0; i < int(m_pa_profile_items.size()); ++i) {
            if (m_pa_profile_items[i].name == str)
                return i;
        }
        return 0;
    };

    if (auto ams_item = obj->GetFilaSystem()->GetAmsById(std::to_string(ams_id))) {
        if (ams_item->GetBindedExtruderSet().size() == 1) {
            int extruder_id = *ams_item->GetBindedExtruderSet().begin();
            if (obj->is_nozzle_flow_type_supported() && (obj->GetExtderSystem()->GetNozzleFlowType(extruder_id) == NozzleFlowType::NONE_FLOWTYPE)) {
                MessageDialog dlg(nullptr, _L("The nozzle flow is not set. Please set the nozzle flow rate before editing the filament.\n'Device -> Print parts'"), _L("Warning"), wxICON_WARNING | wxOK);
                dlg.ShowModal();
            }
        }
    }

    std::vector<PACalibResult> cali_history = obj->GetCalib()->GetPAHistory();
    int cur_cali_idx = get_cali_index_by_ams_slot(obj, ams_id, slot_id); // calib_idx == -1 is select default
    auto iter = std::find_if(cali_history.begin(), cali_history.end(), [cur_cali_idx](const PACalibResult& item){
        return item.cali_idx == cur_cali_idx;
    });
    if (iter != cali_history.end() && !iter->nozzle_sn.empty() && iter->nozzle_sn != "N/A") {
        int sel = get_nozzle_sel_by_sn(obj, iter->nozzle_sn);
        m_comboBox_nozzle_type->SetSelection(sel);
    } else {
        m_comboBox_nozzle_type->SetSelection(-1);
        m_comboBox_nozzle_type->SetValue(wxEmptyString);
    }

    if (obj->GetCalib()->IsVersionInited()) {
        update_pa_profile_items();

        int cali_idx = get_cali_index_by_ams_slot(obj, ams_id, slot_id);
        /* get sel_idx of cali idex in pa_profile_items */
        cali_select_idx = CalibUtils::get_selected_calib_idx(m_pa_profile_items, cali_idx);

        if(from_printer && (*from_printer == 1)) {
            if (cali_select_idx >= 0) {
                m_comboBox_cali_result->SetSelection(cali_select_idx);
            } else {
                m_comboBox_cali_result->SetSelection(0);
                BOOST_LOG_TRIVIAL(info) << "extrusion_cali_status_error: cannot find pa profile"
                                        << ", ams_id = " << ams_id
                                        << ", slot_id = " << slot_id
                                        << ", cali_idx = " << cali_idx;
            }
        } else {
#ifdef __APPLE__
                cali_select_idx = get_cali_index(m_comboBox_filament->GetValue().ToStdString());
#else
                cali_select_idx = get_cali_index(m_comboBox_filament->GetLabel().ToStdString());
#endif
                m_comboBox_cali_result->SetSelection(cali_select_idx);
        }

        if (cali_select_idx >= 0) {
            m_input_k_val->GetTextCtrl()->SetValue(float_to_string_with_precision(m_pa_profile_items[cali_select_idx].k_value));
            m_input_n_val->GetTextCtrl()->SetValue(float_to_string_with_precision(m_pa_profile_items[cali_select_idx].n_coef));
        }
        else {
            m_input_k_val->GetTextCtrl()->SetValue(float_to_string_with_precision(m_pa_profile_items[0].k_value));
            m_input_n_val->GetTextCtrl()->SetValue(float_to_string_with_precision(m_pa_profile_items[0].n_coef));
        }
    }
    else {
        //m_input_k_val->GetTextCtrl()->SetValue("0.00");
        m_input_k_val->Enable(!ams_filament_id.empty());
    }

    m_comboBox_filament->SetClientData(new int(0));
}

void AMSMaterialsSetting::on_dpi_changed(const wxRect &suggested_rect)
{
    m_input_nozzle_max->GetTextCtrl()->SetSize(wxSize(-1, FromDIP(20)));
    m_input_nozzle_min->GetTextCtrl()->SetSize(wxSize(-1, FromDIP(20)));
    m_input_k_val->GetTextCtrl()->SetSize(wxSize(-1, FromDIP(20)));
    m_clr_picker->msw_rescale();
    degree->msw_rescale();
    bitmap_max_degree->SetBitmap(degree->bmp());
    bitmap_min_degree->SetBitmap(degree->bmp());
    m_button_reset->SetMinSize(AMS_MATERIALS_SETTING_BUTTON_SIZE);
    m_button_reset->SetCornerRadius(FromDIP(12));
    m_button_confirm->SetMinSize(AMS_MATERIALS_SETTING_BUTTON_SIZE);
    m_button_confirm->SetCornerRadius(FromDIP(12));
    m_button_close->SetMinSize(AMS_MATERIALS_SETTING_BUTTON_SIZE);
    m_button_close->SetCornerRadius(FromDIP(12));
    this->Refresh();
}

ColorPicker::ColorPicker(wxWindow* parent, wxWindowID id, const wxPoint& pos /*= wxDefaultPosition*/, const wxSize& size /*= wxDefaultSize*/)
{
    wxWindow::Create(parent, id, pos, size);

    SetSize(wxSize(FromDIP(25), FromDIP(25)));
    SetMinSize(wxSize(FromDIP(25), FromDIP(25)));
    SetMaxSize(wxSize(FromDIP(25), FromDIP(25)));

    Bind(wxEVT_PAINT, &ColorPicker::paintEvent, this);

    m_bitmap_border = create_scaled_bitmap("color_picker_border", nullptr, 25);
    m_bitmap_border_dark = create_scaled_bitmap("color_picker_border_dark", nullptr, 25);
    m_bitmap_transparent_def = ScalableBitmap(this, "transparent_color_picker", 25);
    m_bitmap_transparent = create_scaled_bitmap("transparent_color_picker", nullptr, 25);
}

ColorPicker::~ColorPicker(){}

void ColorPicker::msw_rescale()
{
    m_bitmap_border = create_scaled_bitmap("color_picker_border", nullptr, 25);
    m_bitmap_border_dark = create_scaled_bitmap("color_picker_border_dark", nullptr, 25);
    m_bitmap_transparent_def.msw_rescale();

    Refresh();
}

void ColorPicker::set_color(wxColour col)
{
    if (m_colour != col && col.Alpha() != 0 && col.Alpha() != 255 && col.Alpha() != 254) {
        transparent_changed = true;
    }
    m_colour = col;
    // Keep m_cols in sync so doRender's `m_cols.size() > 1` branch does not
    // paint a stale multi-color pie after switching to a single-color spool.
    m_cols = { col };
    Refresh();
}

void ColorPicker::set_colors(std::vector<wxColour> cols)
{
    m_cols = cols;
    if (!cols.empty())
        m_colour = cols[0];
    Refresh();
}

void ColorPicker::paintEvent(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    render(dc);
}

void ColorPicker::render(wxDC& dc)
{
#ifdef __WXMSW__
    wxSize     size = GetSize();
    wxMemoryDC memdc;
    wxBitmap   bmp(size.x, size.y);
    memdc.SelectObject(bmp);
    memdc.Blit({ 0, 0 }, size, &dc, { 0, 0 });

    {
        wxGCDC dc2(memdc);
        doRender(dc2);
    }

    memdc.SelectObject(wxNullBitmap);
    dc.DrawBitmap(bmp, 0, 0);
#else
    doRender(dc);
#endif
}

void ColorPicker::doRender(wxDC& dc)
{
    wxSize     size = GetSize();
    auto alpha = m_colour.Alpha();
    auto radius = m_show_full ? size.x / 2 - FromDIP(1) : size.x / 2;
    if (m_selected) radius -= FromDIP(1);

    auto draw_state = [&]() {
        if (m_selected) {
            dc.SetPen(wxPen(m_colour));
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.DrawCircle(size.x / 2, size.y / 2, size.x / 2);
        }

        if (m_show_full) {
            dc.SetPen(wxPen(wxColour("#6B6B6B")));
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.DrawCircle(size.x / 2, size.y / 2, radius);
        }

        if (m_is_empty) {
            dc.SetTextForeground(*wxBLACK);
            auto tsize = dc.GetTextExtent("?");
            auto pot = wxPoint((size.x - tsize.x) / 2, (size.y - tsize.y) / 2);
            dc.DrawText("?", pot);
        }
    };

    if (m_cols.size() > 1) {
        if (ctype == 0) {
            const double center_x = size.x / 2.0;
            const double center_y = size.y / 2.0;
            const double draw_radius = radius;
            for (int x = 0; x < size.x; ++x) {
                const double dx = x + 0.5 - center_x;
                if (std::abs(dx) > draw_radius) continue;

                const double half_height = std::sqrt(std::max(0.0, draw_radius * draw_radius - dx * dx));
                const int top = std::max(0, static_cast<int>(std::ceil(center_y - half_height)));
                const int bottom = std::min(size.y - 1, static_cast<int>(std::floor(center_y + half_height)));
                const double pos = size.x > 1 ? static_cast<double>(x) / (size.x - 1) : 0.0;
                const double scaled = pos * (m_cols.size() - 1);
                const size_t idx = std::min(static_cast<size_t>(scaled), m_cols.size() - 2);
                const double ratio = scaled - idx;

                dc.SetPen(wxPen(mix_colour(m_cols[idx], m_cols[idx + 1], ratio)));
                dc.DrawLine(x, top, x, bottom + 1);
            }
        }
        else {
            float ev_angle = 360.0 / m_cols.size();
            const float overlap = 2.0f;
            wxPoint center(size.x / 2, size.y / 2);
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(m_cols.front()));
            dc.DrawCircle(center.x, center.y, radius);
            for (int i = 0; i < m_cols.size(); i++) {
                dc.SetBrush(m_cols[i]);
                float startAngle = 270.0f + i * ev_angle;
                float endAngle = startAngle + ev_angle;
                dc.DrawEllipticArc(center.x - radius, center.y - radius, 2 * radius, 2 * radius, startAngle - overlap, endAngle + overlap);
            }
        }

        draw_state();
        return;
    }

    if (alpha == 0) {
        wxSize bmp_size = m_bitmap_transparent_def.GetBmpSize();
        int center_x = (size.x - bmp_size.x) / 2;
        int center_y = (size.y - bmp_size.y) / 2;
        dc.DrawBitmap(m_bitmap_transparent_def.bmp(), center_x, center_y);
    }
    else if (alpha != 254 && alpha != 255) {
        if (transparent_changed) {
            std::string rgb = (m_colour.GetAsString(wxC2S_HTML_SYNTAX)).ToStdString();
            if (rgb.size() == 9) {
                //delete alpha value
                rgb = rgb.substr(0, rgb.size() - 2);
            }
            float alpha_f = 0.7 * m_colour.Alpha() / 255.0;
            std::vector<std::string> replace;
            replace.push_back(rgb);
            std::string fill_replace = "fill-opacity=\"" + std::to_string(alpha_f);
            replace.push_back(fill_replace);
            m_bitmap_transparent = ScalableBitmap(this, "transparent_color_picker", 25, false, false, true, replace).bmp();
            transparent_changed = false;
        }
            wxSize bmp_size = m_bitmap_transparent.GetSize();
            int center_x = (size.x - bmp_size.x) / 2;
            int center_y = (size.y - bmp_size.y) / 2;
            dc.DrawBitmap(m_bitmap_transparent, center_x, center_y);
    }
    else {
        dc.SetPen(wxPen(m_colour));
        dc.SetBrush(wxBrush(m_colour));
        dc.DrawCircle(size.x / 2, size.y / 2, radius);

        if (!m_show_full) {
            bool is_dark_mode = wxGetApp().dark_mode();
            if (!is_dark_mode && is_light_colour_for_border(m_colour)) {
                dc.SetPen(wxPen(wxColour(130, 130, 128), 1, wxPENSTYLE_SOLID));
                dc.SetBrush(*wxTRANSPARENT_BRUSH);
                dc.DrawCircle(size.x / 2, size.y / 2, radius);
            } else if (is_dark_mode && is_dark_colour_for_border(m_colour)) {
                dc.SetPen(wxPen(wxColour(207, 207, 207), 1, wxPENSTYLE_SOLID));
                dc.SetBrush(*wxTRANSPARENT_BRUSH);
                dc.DrawCircle(size.x / 2, size.y / 2, radius);
            }
        }
    }

    draw_state();
}

ColorPickerPopup::ColorPickerPopup(wxWindow* parent)
    :PopupWindow(parent, wxBORDER_NONE)
{
    m_def_colors.clear();
    m_def_colors.push_back(wxColour("#FFFFFF"));
    m_def_colors.push_back(wxColour("#fff144"));
    m_def_colors.push_back(wxColour("#DCF478"));
    m_def_colors.push_back(wxColour("#0ACC38"));
    m_def_colors.push_back(wxColour("#057748"));
    m_def_colors.push_back(wxColour("#0d6284"));
    m_def_colors.push_back(wxColour("#0EE2A0"));
    m_def_colors.push_back(wxColour("#76D9F4"));
    m_def_colors.push_back(wxColour("#46a8f9"));
    m_def_colors.push_back(wxColour("#2850E0"));
    m_def_colors.push_back(wxColour("#443089"));
    m_def_colors.push_back(wxColour("#A03CF7"));
    m_def_colors.push_back(wxColour("#F330F9"));
    m_def_colors.push_back(wxColour("#D4B1DD"));
    m_def_colors.push_back(wxColour("#f95d73"));
    m_def_colors.push_back(wxColour("#f72323"));
    m_def_colors.push_back(wxColour("#7c4b00"));
    m_def_colors.push_back(wxColour("#f98c36"));
    m_def_colors.push_back(wxColour("#fcecd6"));
    m_def_colors.push_back(wxColour("#D3C5A3"));
    m_def_colors.push_back(wxColour("#AF7933"));
    m_def_colors.push_back(wxColour("#898989"));
    m_def_colors.push_back(wxColour("#BCBCBC"));
    m_def_colors.push_back(wxColour("#161616"));


    SetBackgroundColour(wxColour(*wxWHITE));

    wxBoxSizer* m_sizer_main = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* m_sizer_box = new wxBoxSizer(wxVERTICAL);

    m_def_color_box = new StaticBox(this);
    wxBoxSizer* m_sizer_ams = new wxBoxSizer(wxHORIZONTAL);
    auto m_title_ams = new wxStaticText(m_def_color_box, wxID_ANY, _L("AMS"), wxDefaultPosition, wxDefaultSize, 0);
    m_title_ams->SetFont(::Label::Body_14);
    m_title_ams->SetBackgroundColour(wxColour(238, 238, 238));
    m_sizer_ams->Add(m_title_ams, 0, wxALL, 5);
    auto ams_line = new wxPanel(m_def_color_box, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    ams_line->SetBackgroundColour(wxColour("#CECECE"));
    ams_line->SetMinSize(wxSize(-1, 1));
    ams_line->SetMaxSize(wxSize(-1, 1));
    m_sizer_ams->Add(ams_line, 1, wxALIGN_CENTER, 0);


    m_def_color_box->SetCornerRadius(FromDIP(10));
    m_def_color_box->SetBackgroundColor(StateColor(std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Normal)));
    m_def_color_box->SetBorderColor(StateColor(std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Normal)));

    //ams
    m_ams_fg_sizer = new wxFlexGridSizer(0, 8, 0, 0);
    m_ams_fg_sizer->SetFlexibleDirection(wxBOTH);
    m_ams_fg_sizer->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);

    //other
    m_other_fg_sizer = new wxFlexGridSizer(0, 8, 0, 0);
    m_other_fg_sizer->SetFlexibleDirection(wxBOTH);
    m_other_fg_sizer->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);


    for (wxColour col : m_def_colors) {
        auto cp = new ColorPicker(m_def_color_box, wxID_ANY, wxDefaultPosition, wxDefaultSize);
        cp->set_color(col);
        cp->set_colors({ col });
        cp->set_selected(false);
        cp->SetBackgroundColour(StateColor::darkModeColorFor(wxColour(238,238,238)));
        m_color_pickers.push_back(cp);
        m_default_color_pickers.push_back(cp);
        m_other_fg_sizer->Add(cp, 0, wxALL, FromDIP(3));
        cp->Bind(wxEVT_LEFT_DOWN, [this, cp](auto& e) {
            set_def_colour(cp->m_colour, cp->m_cols, cp->ctype);

            wxCommandEvent evt(EVT_SELECTED_COLOR);
            unsigned long g_col = ((cp->m_colour.Red() & 0xff) << 24) + ((cp->m_colour.Green() & 0xff) << 16) + ((cp->m_colour.Blue() & 0xff) << 8) + (cp->m_colour.Alpha() & 0xff);
            evt.SetInt(g_col);
            wxPostEvent(GetParent(), evt);
        });
    }

    wxBoxSizer* m_sizer_other = new wxBoxSizer(wxHORIZONTAL);
    auto m_title_other = new wxStaticText(m_def_color_box, wxID_ANY, _L("Other Color"), wxDefaultPosition, wxDefaultSize, 0);
    m_title_other->SetFont(::Label::Body_14);
    m_title_other->SetBackgroundColour(wxColour(238, 238, 238));
    m_sizer_other->Add(m_title_other, 0, wxALL, 5);
    auto other_line = new wxPanel(m_def_color_box, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    other_line->SetMinSize(wxSize(-1, 1));
    other_line->SetMaxSize(wxSize(-1, 1));
    other_line->SetBackgroundColour(wxColour("#CECECE"));
    m_sizer_other->Add(other_line, 1, wxALIGN_CENTER, 0);

    //custom color
    wxBoxSizer* m_sizer_custom = new wxBoxSizer(wxHORIZONTAL);
    auto m_title_custom = new wxStaticText(m_def_color_box, wxID_ANY, _L("Custom Color"), wxDefaultPosition, wxDefaultSize, 0);
    m_title_custom->SetFont(::Label::Body_14);
    m_title_custom->SetBackgroundColour(wxColour(238, 238, 238));
    auto custom_line = new wxPanel(m_def_color_box, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    custom_line->SetBackgroundColour(wxColour("#CECECE"));
    custom_line->SetMinSize(wxSize(-1, 1));
    custom_line->SetMaxSize(wxSize(-1, 1));
    m_sizer_custom->Add(m_title_custom, 0, wxALL, 5);
    m_sizer_custom->Add(custom_line, 1, wxALIGN_CENTER, 0);

    m_custom_cp =  new StaticBox(m_def_color_box);
    m_custom_cp->SetSize(FromDIP(60), FromDIP(25));
    m_custom_cp->SetMinSize(wxSize(FromDIP(60), FromDIP(25)));
    m_custom_cp->SetMaxSize(wxSize(FromDIP(60), FromDIP(25)));
    m_custom_cp->SetBorderColor(StateColor(std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Normal)));
    m_custom_cp->Bind(wxEVT_LEFT_DOWN, &ColorPickerPopup::on_custom_clr_picker, this);
    m_custom_cp->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {
        SetCursor(wxCURSOR_HAND);
    });
    m_custom_cp->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) {
        SetCursor(wxCURSOR_ARROW);
    });

    m_ts_bitmap_custom = ScalableBitmap(this, "ts_custom_color_picker", 25);
    m_ts_stbitmap_custom = new wxStaticBitmap(m_custom_cp, wxID_ANY, m_ts_bitmap_custom.bmp());

    m_ts_stbitmap_custom->Bind(wxEVT_LEFT_DOWN, &ColorPickerPopup::on_custom_clr_picker, this);
    m_ts_stbitmap_custom->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {
        SetCursor(wxCURSOR_HAND);
        });
    m_ts_stbitmap_custom->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) {
        SetCursor(wxCURSOR_ARROW);
        });

    auto sizer_custom = new wxBoxSizer(wxVERTICAL);
    m_custom_cp->SetSizer(sizer_custom);
    sizer_custom->Add(m_ts_stbitmap_custom, 0, wxEXPAND, 0);
    m_custom_cp->Layout();

    m_clrData = new wxColourData();
    m_clrData->SetChooseFull(true);
    m_clrData->SetChooseAlpha(false);


    m_sizer_box->Add(0, 0, 0, wxTOP, FromDIP(10));
    m_sizer_box->Add(m_sizer_ams, 1, wxEXPAND|wxLEFT|wxRIGHT, FromDIP(10));
    m_sizer_box->Add(m_ams_fg_sizer, 0, wxEXPAND|wxLEFT|wxRIGHT, FromDIP(10));
    m_sizer_box->Add(m_sizer_other, 1, wxEXPAND|wxLEFT|wxRIGHT, FromDIP(10));
    m_sizer_box->Add(m_other_fg_sizer, 0, wxEXPAND|wxLEFT|wxRIGHT, FromDIP(10));
    m_sizer_box->Add(m_sizer_custom, 0, wxEXPAND|wxLEFT|wxRIGHT, FromDIP(10));
    m_sizer_box->Add(m_custom_cp, 0, wxEXPAND|wxLEFT|wxRIGHT, FromDIP(16));
    m_sizer_box->Add(0, 0, 0, wxTOP, FromDIP(10));


    m_def_color_box->SetSizer(m_sizer_box);
    m_def_color_box->Layout();
    m_def_color_box->Fit();

    m_sizer_main->Add(m_def_color_box, 0, wxALL | wxEXPAND, 10);
    SetSizer(m_sizer_main);
    Layout();
    Fit();

    Bind(wxEVT_PAINT, &ColorPickerPopup::paintEvent, this);
    wxGetApp().UpdateDarkUIWin(this);
}

void ColorPickerPopup::on_custom_clr_picker(wxMouseEvent& event)
{
    std::vector<std::string> colors = wxGetApp().app_config->get_custom_color_from_config();
    for (int i = 0; i < colors.size(); i++) {
        m_clrData->SetCustomColour(i, string_to_wxColor(colors[i]));
    }
    auto clr_dialog = new wxColourDialog(nullptr, m_clrData);
    wxColour picker_color;

    if (clr_dialog->ShowModal() == wxID_OK) {
        m_clrData = &(clr_dialog->GetColourData());
        if (colors.size() != CUSTOM_COLOR_COUNT) {
            colors.resize(CUSTOM_COLOR_COUNT);
        }
        for (int i = 0; i < CUSTOM_COLOR_COUNT; i++) {
            colors[i] = color_to_string(m_clrData->GetCustomColour(i));
        }
        wxGetApp().app_config->save_custom_color_to_config(colors);

        picker_color = wxColour(
            m_clrData->GetColour().Red(),
            m_clrData->GetColour().Green(),
            m_clrData->GetColour().Blue(),
            255
        );

        if (picker_color.Alpha() == 0) {
             m_ts_stbitmap_custom->Show();
        }
        else {
            m_ts_stbitmap_custom->Hide();
            m_custom_cp->SetBackgroundColor(picker_color);
        }

        set_def_colour(picker_color, { picker_color }, 2);
        wxCommandEvent evt(EVT_SELECTED_COLOR);
        unsigned long g_col = ((picker_color.Red() & 0xff) << 24) + ((picker_color.Green() & 0xff) << 16) + ((picker_color.Blue() & 0xff) << 8) + (picker_color.Alpha() & 0xff);
        evt.SetInt(g_col);
        wxPostEvent(GetParent(), evt);
    }
}

void ColorPickerPopup::set_ams_colours(const std::vector<ColorItem>& ams)
{
    if (m_ams_color_pickers.size() > 0) {
        for (ColorPicker* col_pick:m_ams_color_pickers) {

            std::vector<ColorPicker*>::iterator iter = find(m_color_pickers.begin(), m_color_pickers.end(), col_pick);
            if (iter != m_color_pickers.end()) {
                col_pick->Destroy();
                m_color_pickers.erase(iter);
            }
        }

        m_ams_color_pickers.clear();
    }


    m_ams_color_items = ams;
    for (const ColorItem& item : m_ams_color_items) {
        if (item.colors.empty()) continue;

        auto cp = new ColorPicker(m_def_color_box, wxID_ANY, wxDefaultPosition, wxDefaultSize);
        cp->set_color(item.colors.front());
        cp->set_colors(item.colors);
        cp->ctype = item.colors.size() > 1 ? item.ctype : 2;
        cp->set_selected(false);
        cp->SetBackgroundColour(StateColor::darkModeColorFor(wxColour(238,238,238)));
        m_color_pickers.push_back(cp);
        m_ams_color_pickers.push_back(cp);
        m_ams_fg_sizer->Add(cp, 0, wxALL, FromDIP(3));
        cp->Bind(wxEVT_LEFT_DOWN, [this, cp](auto& e) {
            set_def_colour(cp->m_colour, cp->m_cols, cp->ctype);

            wxCommandEvent evt(EVT_SELECTED_COLOR);
            unsigned long g_col = ((cp->m_colour.Red() & 0xff) << 24) + ((cp->m_colour.Green() & 0xff) << 16) + ((cp->m_colour.Blue() & 0xff) << 8) + (cp->m_colour.Alpha() & 0xff);
            evt.SetInt(g_col);
            wxPostEvent(GetParent(), evt);
        });
    }
    m_ams_fg_sizer->Layout();
    Layout();
    Fit();
}

void ColorPickerPopup::set_preset_colours(const std::vector<ColorItem>& preset_colors)
{
    if (!m_preset_color_pickers.empty()) {
        for (ColorPicker* col_pick : m_preset_color_pickers) {
            auto iter = std::find(m_color_pickers.begin(), m_color_pickers.end(), col_pick);
            if (iter != m_color_pickers.end()) {
                col_pick->Destroy();
                m_color_pickers.erase(iter);
            }
        }
        m_preset_color_pickers.clear();
    }

    const bool show_default_colours = preset_colors.empty();
    for (ColorPicker* cp : m_default_color_pickers) {
        cp->Show(show_default_colours);
    }

    for (const ColorItem& item : preset_colors) {
        if (item.colors.empty()) continue;

        auto cp = new ColorPicker(m_def_color_box, wxID_ANY, wxDefaultPosition, wxDefaultSize);
        cp->set_color(item.colors.front());
        cp->set_colors(item.colors);
        cp->ctype = item.colors.size() > 1 ? item.ctype : 2;
        cp->set_selected(false);
        cp->SetBackgroundColour(StateColor::darkModeColorFor(wxColour(238,238,238)));
        if (!item.name.empty()) {
            cp->SetToolTip(item.name);
        }

        m_color_pickers.push_back(cp);
        m_preset_color_pickers.push_back(cp);
        m_other_fg_sizer->Add(cp, 0, wxALL, FromDIP(3));
        cp->Bind(wxEVT_LEFT_DOWN, [this, cp](auto& e) {
            set_def_colour(cp->m_colour, cp->m_cols, cp->ctype);

            wxCommandEvent evt(EVT_SELECTED_COLOR);
            unsigned long g_col = ((cp->m_colour.Red() & 0xff) << 24) + ((cp->m_colour.Green() & 0xff) << 16) + ((cp->m_colour.Blue() & 0xff) << 8) + (cp->m_colour.Alpha() & 0xff);
            evt.SetInt(g_col);
            wxPostEvent(GetParent(), evt);
        });
    }

    m_other_fg_sizer->Layout();
    Layout();
    Fit();
}

void ColorPickerPopup::set_def_colour(wxColour col, std::vector<wxColour> cols, int ctype)
{
    m_def_col = col;
    if (cols.empty()) {
        cols.push_back(col);
    }
    m_def_cols = cols;
    m_def_ctype = m_def_cols.size() > 1 ? ctype : 2;

    for (ColorPicker* cp : m_color_pickers) {
        if (cp->m_selected) {
            cp->set_selected(false);
        }
    }

    for (ColorPicker* cp : m_color_pickers) {
        if (!cp->IsShown()) continue;
        if (cp->ctype == m_def_ctype && cp->m_cols == m_def_cols) {
            cp->set_selected(true);
            break;
        }
    }

    if (m_def_col.Alpha() == 0) {
        m_ts_stbitmap_custom->Show();
    }
    else {
        m_ts_stbitmap_custom->Hide();
        m_custom_cp->SetBackgroundColor(m_def_col);
    }

    Dismiss();
}

void ColorPickerPopup::paintEvent(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    dc.SetPen(wxColour(0xAC, 0xAC, 0xAC));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRoundedRectangle(0, 0, GetSize().x, GetSize().y, 0);
}

void ColorPickerPopup::OnDismiss() {}

void ColorPickerPopup::Popup()
{
    PopupWindow::Popup();
}

bool ColorPickerPopup::ProcessLeftDown(wxMouseEvent& event) {
    return PopupWindow::ProcessLeftDown(event);
}

// ---- AMSNewOfficialFilamentDlg ------------------------------------------

AMSNewOfficialFilamentDlg::AMSNewOfficialFilamentDlg(wxWindow* parent)
    : DPIDialog(parent, wxID_ANY, wxEmptyString,
                wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    create();
    wxGetApp().UpdateDlgDarkUI(this);
}

void AMSNewOfficialFilamentDlg::create()
{
    SetBackgroundColour(*wxWHITE);
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    auto* label = new wxStaticText(this, wxID_ANY,
        _L("AMS detected a new official filament. Add it to Filament Manager?"),
        wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    label->Wrap(FromDIP(360));
    sizer->Add(label, 0, wxALL, FromDIP(12));

    m_radio_record_new    = new RadioBox(this);
    m_radio_link_existing = new RadioBox(this);
    m_radio_skip          = new RadioBox(this);

    auto _row = [&](RadioBox* rb, const wxString& text) {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(rb, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
        row->Add(new wxStaticText(this, wxID_ANY, text), 0, wxALIGN_CENTER_VERTICAL);
        sizer->Add(row, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
    };
    _row(m_radio_record_new,    _L("Add as new filament"));
    _row(m_radio_link_existing, _L("Link to existing filament"));

    // Dropdown row — shown only when "Link to existing filament" is selected
    m_combo_link = new ::ComboBox(this, wxID_ANY, wxEmptyString,
                                  wxDefaultPosition, wxSize(FromDIP(360), FromDIP(30)),
                                  0, nullptr, wxCB_READONLY);
    m_combo_row = new wxBoxSizer(wxHORIZONTAL);
    m_combo_row->AddSpacer(FromDIP(22));  // indent under radio button
    m_combo_row->Add(m_combo_link, 1, wxEXPAND);
    sizer->Add(m_combo_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
    sizer->Hide(m_combo_row, true);

    m_combo_link->Bind(wxEVT_COMMAND_COMBOBOX_SELECTED,
                       &AMSNewOfficialFilamentDlg::on_combo_selected, this);

    _row(m_radio_skip, _L("Not now"));
    m_radio_record_new->SetValue(true);

    auto select_radio = [this](RadioBox* chosen) {
        m_radio_record_new   ->SetValue(chosen == m_radio_record_new);
        m_radio_link_existing->SetValue(chosen == m_radio_link_existing);
        m_radio_skip         ->SetValue(chosen == m_radio_skip);
        const bool show_combo = (chosen == m_radio_link_existing);
        GetSizer()->Show(m_combo_row, show_combo, true);
        // Confirm is enabled only when: not in link mode, or a real spool is selected
        m_btn_confirm->Enable(chosen != m_radio_link_existing ||
                              m_combo_idx_to_spool_id.count(m_combo_link->GetSelection()) > 0);
        Layout();
        Fit();
    };
    m_radio_record_new->Bind(wxEVT_TOGGLEBUTTON, [this, select_radio](wxCommandEvent&) {
        select_radio(m_radio_record_new);
    });
    m_radio_link_existing->Bind(wxEVT_TOGGLEBUTTON, [this, select_radio](wxCommandEvent&) {
        select_radio(m_radio_link_existing);
    });
    m_radio_skip->Bind(wxEVT_TOGGLEBUTTON, [this, select_radio](wxCommandEvent&) {
        select_radio(m_radio_skip);
    });

    auto* btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    btn_sizer->AddStretchSpacer();

    m_btn_confirm = new Button(this, _L("Confirm"));
    m_btn_confirm->SetMinSize(AMS_MATERIALS_SETTING_BUTTON_SIZE);
    m_btn_confirm->SetCornerRadius(FromDIP(12));
    m_btn_confirm->SetBackgroundColor(StateColor(
        std::pair<wxColour, int>(wxColour(27, 136, 68),  StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0, 174, 66),   StateColor::Normal)
    ));
    m_btn_confirm->SetBorderColor(wxColour(0, 174, 66));
    m_btn_confirm->SetTextColor(wxColour("#FFFFFE"));
    m_btn_confirm->Bind(wxEVT_BUTTON, &AMSNewOfficialFilamentDlg::on_confirm, this);

    m_btn_cancel = new Button(this, _L("Cancel"));
    m_btn_cancel->SetMinSize(AMS_MATERIALS_SETTING_BUTTON_SIZE);
    m_btn_cancel->SetCornerRadius(FromDIP(12));
    m_btn_cancel->Bind(wxEVT_BUTTON, &AMSNewOfficialFilamentDlg::on_cancel, this);

    btn_sizer->Add(m_btn_cancel,  0, wxRIGHT, FromDIP(8));
    btn_sizer->Add(m_btn_confirm, 0);
    sizer->Add(btn_sizer, 0, wxEXPAND | wxALL, FromDIP(12));

    SetSizer(sizer);
    Fit();
    Centre();
}

void AMSNewOfficialFilamentDlg::on_confirm(wxCommandEvent&)
{
    if (m_radio_record_new->GetValue()) {
        m_choice = Choice::RecordNew;
    } else if (m_radio_link_existing->GetValue()) {
        const int sel = m_combo_link->GetSelection();
        auto it = m_combo_idx_to_spool_id.find(sel);
        if (it == m_combo_idx_to_spool_id.end() || it->second.empty())
            return;  // nothing selected — keep dialog open
        m_choice = Choice::LinkExisting;
        m_selected_link_spool_id = it->second;
    } else {
        m_choice = Choice::Skip;
    }
    EndModal(wxID_OK);
}

void AMSNewOfficialFilamentDlg::on_cancel(wxCommandEvent&)
{
    m_choice = Choice::Skip;
    EndModal(wxID_CANCEL);
}

void AMSNewOfficialFilamentDlg::on_combo_selected(wxCommandEvent& evt)
{
    const int sel = evt.GetSelection();
    m_btn_confirm->Enable(m_combo_idx_to_spool_id.count(sel) > 0);

    // ComboBox::SetSelection stuffed the 52px row bitmap into the header.
    // Override with the compact "name  weight" text + drop-down arrow icon.
    m_combo_link->SetIcon("drop_down");
    auto it = m_combo_header_texts.find(sel);
    m_combo_link->SetLabel(it != m_combo_header_texts.end() ? it->second : _L("Please select"));
}

void AMSNewOfficialFilamentDlg::SetTrayContext(MachineObject* obj,
                                               const std::string& ams_id,
                                               const std::string& slot_id)
{
    m_obj     = obj;
    m_ams_id  = ams_id;
    m_slot_id = slot_id;
    populate_link_combo();
    // Reset to default state
    m_radio_record_new->SetValue(true);
    m_radio_link_existing->SetValue(false);
    m_radio_skip->SetValue(false);
    GetSizer()->Show(m_combo_row, false, true);
    m_selected_link_spool_id.clear();
    // Initial placeholder in the combo header before user picks anything.
    m_combo_link->SetIcon("drop_down");
    m_combo_link->SetLabel(_L("Please select"));
    m_btn_confirm->Enable(true);
    Layout();
    Fit();
}

void AMSNewOfficialFilamentDlg::populate_link_combo()
{
    m_combo_link->Clear();
    m_combo_idx_to_spool_id.clear();
    m_combo_header_texts.clear();
    m_selected_link_spool_id.clear();
    if (!m_obj) return;

    DevAmsTray* tray = m_obj->get_ams_tray(m_ams_id, m_slot_id);
    if (!tray) return;

    auto* store = wxGetApp().fila_manager_store();
    if (!store) return;

    // Normalize tray color: strip leading '#', uppercase 6 hex chars
    auto normalize_color = [](std::string c) -> std::string {
        if (!c.empty() && c[0] == '#') c = c.substr(1);
        std::transform(c.begin(), c.end(), c.begin(), ::toupper);
        return c.substr(0, std::min<size_t>(6, c.size()));
    };
    const std::string tray_color = normalize_color(tray->color);

    // Note: tray->sub_brands actually holds the SERIES name (e.g. "PLA Basic"),
    // not the brand. Official AMS filaments are always Bambu Lab, so match on
    // brand="Bambu Lab" + series==tray.sub_brands + material_type + color.
    std::vector<FilamentSpool> candidates;
    for (const auto& id : store->all_spool_ids()) {
        const FilamentSpool* sp = store->get_spool(id);
        if (!sp) continue;
        if (sp->in_printer) continue;
        if (!sp->tag_uid.empty()) continue;
        if (sp->brand         != "Bambu Lab")            continue;
        if (sp->series        != tray->sub_brands)       continue;
        if (sp->material_type != tray->m_fila_type)      continue;
        if (normalize_color(sp->color_code) != tray_color) continue;
        candidates.push_back(*sp);
    }

    // Use the same 52px row-bitmap pattern as _populate_filament_combobox_grouped:
    // full row rendered into a bitmap (chip + name + note + weight), passed with
    // wxEmptyString so DropDown row height is driven entirely by iconSize.
    //
    // DropDown reserves ~30px on the left of each row for check icon + padding
    // (DropDown.cpp: rcContent.x += 5, then check_bmp.width + 5 more). Subtract
    // that from the bitmap width so the right-aligned weight column is not clipped.
    const int check_reserve = m_combo_link->FromDIP(30);
    const int row_width = std::max(
        m_combo_link->GetSize().GetWidth() - check_reserve,
        m_combo_link->FromDIP(230));

    if (candidates.empty()) {
        m_combo_link->Append(_L("No matching filament"), wxNullBitmap, DD_ITEM_STYLE_DISABLED);
        return;
    }

    for (const auto& sp : candidates) {
        bool note_truncated = false;
        wxBitmap row_bmp = _render_spool_row_bitmap(m_combo_link, sp, row_width,
                                                    /*dimmed=*/false, &note_truncated);
        int idx = m_combo_link->Append(wxEmptyString, row_bmp);
        if (idx < 0) continue;
        m_combo_idx_to_spool_id[idx] = sp.spool_id;

        // If the note was truncated to fit the row, expose the full text via tooltip.
        if (note_truncated && !sp.note.empty()) {
            m_combo_link->SetItemTooltip(static_cast<unsigned int>(idx),
                _L("Remark") + ": " + wxString::FromUTF8(sp.note));
        }

        // Header text shown in the combo box when this item is selected.
        wxString header = _spool_display_name(sp);
        const double total_nw = sp.effective_total_net_weight();
        if (total_nw > 0.0) {
            const int remain_g = static_cast<int>(
                total_nw * std::max(0, std::min(100, sp.remain_percent)) / 100.0);
            if (remain_g > 0)
                header += wxString::Format("  %dg", remain_g);
        }
        m_combo_header_texts[idx] = header;
    }
}

}} // namespace Slic3r::GUI
