#include "AMSMaterialsSetting.hpp"
#include "GUI_App.hpp"
#include "libslic3r/Preset.hpp"
#include "I18N.hpp"

namespace Slic3r { namespace GUI {

AMSMaterialsSetting::AMSMaterialsSetting(wxWindow *parent, wxWindowID id): wxPopupTransientWindow(parent, id)
{
    create();
}

void AMSMaterialsSetting::create()
{
    SetBackgroundColour(*wxWHITE);
    wxBoxSizer *m_sizer_main = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *m_sizer_body = new wxBoxSizer(wxVERTICAL);

    m_panel_body                 = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(AMS_MATERIALS_SETTING_BODY_WIDTH, -1), wxTAB_TRAVERSAL);
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


    m_panel_SN = new wxPanel(m_panel_body, wxID_ANY);
    wxBoxSizer *m_sizer_SN = new wxBoxSizer(wxHORIZONTAL);

    auto m_title_SN = new wxStaticText(m_panel_SN, wxID_ANY, _L("SN"), wxDefaultPosition, wxSize(AMS_MATERIALS_SETTING_LABEL_WIDTH, -1), 0);
    m_title_SN->SetFont(::Label::Body_13);
    m_title_SN->SetForegroundColour(AMS_MATERIALS_SETTING_GREY800);
    m_title_SN->Wrap(-1);
    m_sizer_SN->Add(m_title_SN, 0, wxALIGN_CENTER, 0);

    m_sizer_SN->Add(0, 0, 0, wxEXPAND, 0);

    m_sn_number = new wxStaticText(m_panel_SN, wxID_ANY, wxEmptyString, wxDefaultPosition, AMS_MATERIALS_SETTING_COMBOX_WIDTH);
    m_sizer_SN->Add(m_sn_number, 0, wxALIGN_CENTER, 0);
    m_panel_SN->SetSizer(m_sizer_SN);
    m_panel_SN->Layout();
    m_panel_SN->Fit();

    wxBoxSizer *m_sizer_temperature = new wxBoxSizer(wxHORIZONTAL);
    m_title_temperature             = new wxStaticText(m_panel_body, wxID_ANY, _L("Nozzle\nTemperature"), wxDefaultPosition, wxSize(AMS_MATERIALS_SETTING_LABEL_WIDTH, -1), 0);
    m_title_temperature->SetFont(::Label::Body_13);
    m_title_temperature->SetForegroundColour(AMS_MATERIALS_SETTING_GREY800);
    m_title_temperature->Wrap(-1);
    m_sizer_temperature->Add(m_title_temperature, 0, wxALIGN_CENTER, 0);

    m_sizer_temperature->Add(0, 0, 0, wxEXPAND, 0);

    wxBoxSizer *sizer_other           = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *sizer_tempinput_other = new wxBoxSizer(wxHORIZONTAL);
    m_input_other                     = new ::TextInput(m_panel_body, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, AMS_MATERIALS_SETTING_INPUT_SIZE,
                                    wxTE_CENTRE | wxTE_PROCESS_ENTER);
    m_input_other->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    auto bitmapother = new wxStaticBitmap(m_panel_body, -1, create_scaled_bitmap("degree", nullptr, 16), wxDefaultPosition, wxDefaultSize);
    sizer_tempinput_other->Add(m_input_other, 0, wxALIGN_CENTER, 0);
    sizer_tempinput_other->Add(bitmapother, 0, wxALIGN_CENTER, 0);
    sizer_other->Add(sizer_tempinput_other, 0, wxALIGN_CENTER, 0);

    m_sizer_temperature->Add(sizer_other, 0, wxALL | wxALIGN_CENTER, 0);
    m_sizer_temperature->AddStretchSpacer();

    wxString warning_string = wxString::FromUTF8(
        (boost::format(_u8L("The input value should be greater than %1% and less than %2%")) % FILAMENT_MIN_TEMP % FILAMENT_MAX_TEMP).str());
    warning_text = new wxStaticText(m_panel_body, wxID_ANY, warning_string, wxDefaultPosition, wxDefaultSize, 0);
    warning_text->SetFont(::Label::Body_13);
    warning_text->SetForegroundColour(wxColour(255, 111, 0));

    warning_text->Wrap(AMS_MATERIALS_SETTING_BODY_WIDTH);
    warning_text->SetMinSize(wxSize(AMS_MATERIALS_SETTING_BODY_WIDTH, -1));
    warning_text->Hide();

    m_input_other->GetTextCtrl()->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent &e) {
        warning_text->Hide();
        Layout();
        Fit();
        e.Skip();
    });
    m_input_other->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent &e) {
        input_finish();
        e.Skip();
    });
    m_input_other->GetTextCtrl()->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent &e) {
        input_finish();
        e.Skip();
    });

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



    m_button_cancel = new Button(m_panel_body, _L("Cancel"));
    StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
                            std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
                            std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));
    StateColor btn_bd_white(std::pair<wxColour, int>(*wxWHITE, StateColor::Disabled), std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));
    m_button_cancel->SetBackgroundColor(btn_bg_white);
    m_button_cancel->SetBorderColor(btn_bd_white);
    m_button_cancel->SetTextColor(wxColour(38, 46, 48));
    m_button_cancel->SetMinSize(AMS_MATERIALS_SETTING_BUTTON_SIZE);
    m_button_cancel->SetCornerRadius(12);
    m_button_cancel->Bind(wxEVT_LEFT_DOWN, &AMSMaterialsSetting::on_select_cancel, this);
    m_sizer_button->Add(0, 0, 0, wxLEFT, 10);
    m_sizer_button->Add(m_button_cancel, 0, wxALIGN_CENTER, 0);

    m_sizer_body->Add(m_sizer_filament, 0, wxEXPAND, 0);
    m_sizer_body->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(16));
    m_sizer_body->Add(m_sizer_colour, 0, wxEXPAND, 0);
    m_sizer_body->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(16));
    m_sizer_body->Add(m_panel_SN, 0, wxEXPAND, 0);
    m_sizer_body->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(10));
    m_sizer_body->Add(m_sizer_temperature, 0, wxEXPAND, 0);
    m_sizer_body->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(5));
    m_sizer_body->Add(warning_text, 0, wxEXPAND, 0);
    m_sizer_body->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(24));
    m_sizer_body->Add(m_sizer_button, 0, wxEXPAND, 0);

    m_panel_body->SetSizer(m_sizer_body);
    m_panel_body->Layout();
    m_sizer_main->Add(m_panel_body, 0, wxALL | wxEXPAND, 24);

    this->SetSizer(m_sizer_main);
    this->Layout();
    m_sizer_main->Fit(this);

    this->Centre(wxBOTH);

     Bind(wxEVT_PAINT, &AMSMaterialsSetting::paintEvent, this);
    m_comboBox_filament->Connect(wxEVT_COMMAND_COMBOBOX_SELECTED, wxCommandEventHandler(AMSMaterialsSetting::on_select_filament), NULL, this);
}

void AMSMaterialsSetting::paintEvent(wxPaintEvent &evt) 
{
    auto      size = GetSize();
    wxPaintDC dc(this);
    dc.SetPen(wxPen(wxColour(38, 46, 48), 1, wxSOLID));
    dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
    dc.DrawRectangle(0, 0, size.x, size.y);
}

AMSMaterialsSetting::~AMSMaterialsSetting()
{
    m_comboBox_filament->Disconnect(wxEVT_COMMAND_COMBOBOX_SELECTED, wxCommandEventHandler(AMSMaterialsSetting::on_select_filament), NULL, this);
}


void AMSMaterialsSetting::input_finish() 
{
    if (m_input_other->GetTextCtrl()->GetValue().empty())return;
    auto val = std::atoi(m_input_other->GetTextCtrl()->GetValue().c_str());

    if (val < FILAMENT_MIN_TEMP || val > FILAMENT_MAX_TEMP) {
        warning_text->Show();
    } else {
        warning_text->Hide();
    }
    Layout();
    Fit();
}
void AMSMaterialsSetting::on_select_cancel(wxMouseEvent &event)
{
    wxPopupTransientWindow::Dismiss();
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
            if (it->name.compare(m_comboBox_filament->GetValue().ToStdString()) == 0) {
                ams_filament_id = it->filament_id;
            }
        }
    }
    if (ams_filament_id.empty() || nozzle_temp.empty()) {
        BOOST_LOG_TRIVIAL(trace) << "Invalid Setting id";
    } else {
        if (obj) {
            obj->command_ams_filament_settings(ams_id, tray_id, ams_filament_id, std::string(col_buf), nozzle_temp_int);
        }
    }
    wxPopupTransientWindow::Dismiss();
}

void AMSMaterialsSetting::set_color(wxColour color)
{
    m_colourPicker1->SetColour(color);
}

void AMSMaterialsSetting::Dismiss() 
{ 

    //Destroy();
    //wxPopupTransientWindow::Dismiss();
    
}

void AMSMaterialsSetting::Popup(bool show, bool third, wxString filament, wxColour colour, wxString sn, wxString tep)
{
    if (!m_is_third) {
        m_panel_SN->Show();
        m_comboBox_filament->SetValue(filament);
        m_sn_number->SetLabelText(sn);
        m_input_other->GetTextCtrl()->SetValue(tep);
        m_colourPicker1->SetColour(colour);
        m_comboBox_filament->Disable();
        m_input_other->Disable();
        wxPopupTransientWindow::Popup();
        Layout();
        return;
    }
    m_panel_SN->Hide();
    Layout();
    Fit();

    int selection_idx = -1, idx = 0;
    wxArrayString filament_items;
    if (show) {
        PresetBundle* preset_bundle = wxGetApp().preset_bundle;
        if (preset_bundle) {
            for (auto filament_it = preset_bundle->filaments.begin(); filament_it != preset_bundle->filaments.end(); filament_it++) {
                // filter by system preset
                if (!filament_it->is_system) continue;

                for (auto printer_it = preset_bundle->printers.begin(); printer_it != preset_bundle->printers.end(); printer_it++) {
                    // filter by system preset
                    if (!printer_it->is_system) continue;
                    // get printer_model
                    ConfigOption* printer_model_opt = printer_it->config.option("printer_model");
                    ConfigOptionString* printer_model_str = dynamic_cast<ConfigOptionString*>(printer_model_opt);
                    if (!printer_model_str || !obj)
                        continue;

                    // use printer_model as printer type
                    if (printer_model_str->value != MachineObject::get_preset_printer_model_name(obj->printer_type))
                        continue;
                    ConfigOption* printer_opt = filament_it->config.option("compatible_printers");
                    ConfigOptionStrings* printer_strs = dynamic_cast<ConfigOptionStrings*>(printer_opt);
                    for (auto printer_str : printer_strs->values) {
                        if (printer_it->name == printer_str) {
                            // name matched
                            filament_items.push_back(filament_it->name);
                            if (filament_it->filament_id == ams_filament_id) {
                                selection_idx = idx;
                                ConfigOption* opt = filament_it->config.option("nozzle_temperature");
                                if (opt) {
                                    ConfigOptionStrings* opt_strs = dynamic_cast<ConfigOptionStrings*>(opt);
                                    if (opt_strs) {
                                        opt_strs->get_at(0);
                                        wxString text_nozzle_temp = wxString::Format("%s", opt_strs->get_at(0));
                                        m_input_other->GetTextCtrl()->SetValue(text_nozzle_temp);
                                    }
                                }
                            }
                            idx++;
                        }
                    }
                }
            }
        }
        m_comboBox_filament->Set(filament_items);
        if (selection_idx >= 0 && selection_idx < filament_items.size()) {
            m_comboBox_filament->SetSelection(selection_idx);
        }
        else {
            m_comboBox_filament->SetSelection(selection_idx);
        }
    }
    wxPopupTransientWindow::Popup();
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
            if (it->name.compare(m_comboBox_filament->GetValue().ToStdString()) == 0) {
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
    if (m_input_other->GetTextCtrl()->GetValue().IsEmpty()) {
        m_input_other->GetTextCtrl()->SetValue("220");
    }
}

}} // namespace Slic3r::GUI
