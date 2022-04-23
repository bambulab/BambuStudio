#include "AMSMaterialsSetting.hpp"
#include "GUI_App.hpp"
#include "libslic3r/Preset.hpp"
#include "I18N.hpp"

namespace Slic3r { namespace GUI {

AMSMaterialsSetting::AMSMaterialsSetting(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size, long style)
    : wxPopupTransientWindow(parent, wxBORDER_NONE)
{
    create();
}
AMSMaterialsSetting::~AMSMaterialsSetting() {}

void AMSMaterialsSetting::create()
{
    wxBoxSizer *m_sizer_main = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *m_sizer_body = new wxBoxSizer(wxVERTICAL);

    m_panel_body = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(AMS_MATERIALS_SETTING_BODY_WIDTH, -1), wxTAB_TRAVERSAL);
    wxBoxSizer *m_sizer_filament = new wxBoxSizer(wxHORIZONTAL);

    m_title_filament = new wxStaticText(m_panel_body, wxID_ANY, _L("Filament"), wxDefaultPosition, wxSize(AMS_MATERIALS_SETTING_LABEL_WIDTH, -1), 0);
    m_title_filament->SetFont(::Label::Body_13);
    m_title_filament->SetForegroundColour(AMS_MATERIALS_SETTING_GREY800);
    m_title_filament->Wrap(-1);
    m_sizer_filament->Add(m_title_filament, 0, wxALIGN_CENTER, 0);

    m_sizer_filament->Add(0, 0, 0, wxEXPAND, 0);

    m_comboBox_filament = new ::ComboBox(m_panel_body, wxID_ANY, wxEmptyString, wxDefaultPosition, AMS_MATERIALS_SETTING_COMBOX_WIDTH, 0, nullptr, wxCB_READONLY);
    m_sizer_filament->Add(m_comboBox_filament, 0, wxALIGN_CENTER, 0);

    wxBoxSizer *m_sizer_colour = new wxBoxSizer(wxHORIZONTAL);

    m_title_colour = new wxStaticText(m_panel_body, wxID_ANY, _L("Colour"), wxDefaultPosition, wxSize(AMS_MATERIALS_SETTING_LABEL_WIDTH, -1), 0);
    m_title_colour->SetFont(::Label::Body_13);
    m_title_colour->SetForegroundColour(AMS_MATERIALS_SETTING_GREY800);
    m_title_colour->Wrap(-1);
    m_sizer_colour->Add(m_title_colour, 0, wxALIGN_CENTER, 0);

    m_sizer_colour->Add(0, 0, 0, wxEXPAND, 0);

    m_colourPicker1 = new wxColourPickerCtrl(m_panel_body, wxID_ANY, *wxBLACK, wxDefaultPosition, wxDefaultSize, wxCLRP_DEFAULT_STYLE);
    m_sizer_colour->Add(m_colourPicker1, 0, 0, 0);

    wxBoxSizer *m_sizer_temperature = new wxBoxSizer(wxHORIZONTAL);
    m_title_temperature = new wxStaticText(m_panel_body, wxID_ANY, _L("Nozzle\nTemperature"), wxDefaultPosition, wxSize(AMS_MATERIALS_SETTING_LABEL_WIDTH, -1), 0);
    m_title_temperature->SetFont(::Label::Body_13);
    m_title_temperature->SetForegroundColour(AMS_MATERIALS_SETTING_GREY800);
    m_title_temperature->Wrap(-1);
    m_sizer_temperature->Add(m_title_temperature, 0, wxALIGN_CENTER, 0);

    m_sizer_temperature->Add(0, 0, 0, wxEXPAND, 0);

    wxBoxSizer *sizer_other = new wxBoxSizer(wxVERTICAL);
      //m_label_other = new wxStaticText(m_panel_body, wxID_ANY, _L("Others"), wxDefaultPosition, wxDefaultSize, 0);
      //m_label_other->SetFont(::Label::Body_13);
      //m_label_other->SetForegroundColour(AMS_MATERIALS_SETTING_GREY300);
      //m_label_other->Wrap(-1);
      //sizer_other->Add(m_label_other, 0, wxALIGN_CENTER, 0);

    wxBoxSizer * sizer_tempinput_other= new wxBoxSizer(wxHORIZONTAL);;
    m_input_other = new TextInput(m_panel_body, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, AMS_MATERIALS_SETTING_INPUT_SIZE, wxTE_CENTER|wxTE_PROCESS_ENTER);
    m_input_other->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    auto bitmapother = new wxStaticBitmap(m_panel_body, -1, create_scaled_bitmap("degree", nullptr, 16), wxDefaultPosition, wxDefaultSize);
    sizer_tempinput_other->Add(m_input_other, 0, wxALIGN_CENTER, 0);
    sizer_tempinput_other->Add(bitmapother, 0, wxALIGN_CENTER, 0);
    sizer_other->Add(sizer_tempinput_other, 0, wxALIGN_CENTER, 0);

    m_sizer_temperature->Add(sizer_other, 0, wxALL | wxALIGN_CENTER, 0);
    m_sizer_temperature->AddStretchSpacer();

    auto warning_string = _L("The minmum temperature should not be less than " + wxString::Format("%d", 100));
    warning_text = new wxStaticText(m_panel_body, wxID_ANY, warning_string, wxDefaultPosition, wxDefaultSize, 0);
    warning_text->SetFont(::Label::Body_13);
    warning_text->SetForegroundColour(wxColour(255,111,0));
    warning_text->Wrap(-1);
   
    wxBoxSizer *m_sizer_button = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_button->Add(0, 0, 1, wxEXPAND, 0);

    m_button_confirm = new Button(m_panel_body, _L("Confirm"));
    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
                            std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
    m_button_confirm->SetBackgroundColor(btn_bg_green);
    m_button_confirm->SetBorderColor(wxColour(0, 174, 66));
    m_button_confirm->SetTextColor(AMS_MATERIALS_SETTING_GREY200);
    m_button_confirm->SetMinSize(AMS_MATERIALS_SETTING_BUTTON_SIZE);
    m_button_confirm->SetCornerRadius(12);
    m_button_confirm->Bind(wxEVT_LEFT_DOWN, &AMSMaterialsSetting::on_select_ok, this);
    m_sizer_button->Add(m_button_confirm, 0, wxALIGN_CENTER, 0);


    m_sizer_body->Add(m_sizer_filament, 0, wxEXPAND, 0);
    m_sizer_body->Add(0,0,0,wxEXPAND | wxTOP, 16);
    m_sizer_body->Add(m_sizer_colour, 0, wxEXPAND, 0);
    m_sizer_body->Add(0,0,0,wxEXPAND | wxTOP, 16);
    m_sizer_body->Add(m_sizer_temperature, 0, wxEXPAND, 0);
    m_sizer_body->Add(0,0,0,wxEXPAND | wxTOP, 5);
    m_sizer_body->Add(warning_text, 0, wxEXPAND, 0);
    m_sizer_body->Add(0,0,0,wxEXPAND | wxTOP, 24);
    m_sizer_body->Add(m_sizer_button, 0, wxEXPAND, 0);

    m_panel_body->SetSizer(m_sizer_body);
    m_panel_body->Layout();
    m_sizer_main->Add(m_panel_body, 0, wxALL | wxEXPAND, 24);

    this->SetSizer(m_sizer_main);
    this->Layout();
    m_sizer_main->Fit(this);

    this->Centre(wxBOTH);

    m_comboBox_filament->Bind(wxEVT_COMBOBOX, &AMSMaterialsSetting::on_select_filament, this);
}

void AMSMaterialsSetting::on_select_ok(wxMouseEvent &event)
{
    wxString nozzle_temp  = m_input_other->GetTextCtrl()->GetValue();
    auto filament         = m_comboBox_filament->GetValue();

    long nozzle_temp_int;
    nozzle_temp.ToLong(&nozzle_temp_int);

    wxColour color = m_colourPicker1->GetColour();
    char col_buf[10];
    sprintf(col_buf, "%02X%02X%02XFF", (int) color.Red(), (int) color.Green(), (int) color.Blue());

    PresetBundle *preset_bundle = wxGetApp().preset_bundle;
    if (preset_bundle) {
        for (auto it = preset_bundle->filaments.begin(); it != preset_bundle->filaments.end(); it++) {
            if (it->alias.compare(m_comboBox_filament->GetValue().ToStdString()) == 0) {
                ams_filament_id = it->filament_id;
            }
        }
    }
    if (ams_filament_id.empty() || nozzle_temp.empty()) {
        //TODO warning
        BOOST_LOG_TRIVIAL(trace) << "Invalid Setting id";
    } else {
        if (obj) {
            obj->command_ams_filament_settings(ams_id, tray_id, ams_filament_id, std::string(col_buf), nozzle_temp_int);
        }
    }

    Dismiss();
}

void AMSMaterialsSetting::set_color(wxColour color)
{
    m_colourPicker1->SetColour(color);
}

void AMSMaterialsSetting::Popup(bool show)
{
    int selection_idx = -1, idx = 0;
    wxArrayString filament_items;
    if (show) {
        PresetBundle* preset_bundle = wxGetApp().preset_bundle;
        if (preset_bundle) {
            for (auto it = preset_bundle->filaments.begin(); it != preset_bundle->filaments.end(); it++) {
                filament_items.push_back(it->alias);
                if (it->filament_id == ams_filament_id) {
                    selection_idx = idx;
                    ConfigOption* opt = it->config.option("nozzle_temperature");
                    if (opt) {
                        ConfigOptionStrings *opt_strs = dynamic_cast<ConfigOptionStrings *>(opt);
                        if (opt_strs) {
                            opt_strs->get_at(0);
                            wxString text_nozzle_temp = wxString::Format("%s", opt_strs->get_at(0));
                            //m_input_other->GetTextCtrl()->SetValue(text_nozzle_temp);
                        }
                    }
                }
                idx++;
            }
            m_comboBox_filament->Set(filament_items);
            if (selection_idx >= 0 && selection_idx < filament_items.size()) {
                m_comboBox_filament->SetSelection(selection_idx);
            }
        }
    }
    wxPopupTransientWindow::Popup();
}

void AMSMaterialsSetting::SetMaxTemp(int max) 
{ 
    max_temp = max; 
}

//void AMSMaterialsSetting::on_dpi_changed(const wxRect &suggested_rect) 
//{
//    m_button_confirm->SetMinSize(AMS_MATERIALS_SETTING_BUTTON_SIZE);
//}

void AMSMaterialsSetting::on_select_filament(wxCommandEvent &evt)
{
    PresetBundle* preset_bundle = wxGetApp().preset_bundle;
    if (preset_bundle) {
        for (auto it = preset_bundle->filaments.begin(); it != preset_bundle->filaments.end(); it++) {
            if (it->alias.compare(m_comboBox_filament->GetValue().ToStdString()) == 0) {
                ConfigOption *opt = it->config.option("nozzle_temperature");
                if (opt) {
                    ConfigOptionInts *opt_strs = dynamic_cast<ConfigOptionInts *>(opt);
                    if (opt_strs) {
                        wxString text_nozzle_temp = wxString::Format("%d", opt_strs->get_at(0));
                        m_input_other->GetTextCtrl()->SetValue(text_nozzle_temp);
                    }
                }
            }
        }
    }
}

}} // namespace Slic3r::GUI