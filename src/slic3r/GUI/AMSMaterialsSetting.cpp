#include "AMSMaterialsSetting.hpp"
#include "I18N.hpp"

namespace Slic3r { namespace GUI {

AMSMaterialsSetting::AMSMaterialsSetting(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size, long style) : DPIDialog(parent, id, wxEmptyString, pos, size, style) { create(); }
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
    m_title_temperature = new wxStaticText(m_panel_body, wxID_ANY, _L("Extruder\nTemperature"), wxDefaultPosition, wxSize(AMS_MATERIALS_SETTING_LABEL_WIDTH, -1), 0);
    m_title_temperature->SetFont(::Label::Body_13);
    m_title_temperature->SetForegroundColour(AMS_MATERIALS_SETTING_GREY800);
    m_title_temperature->Wrap(-1);
    m_sizer_temperature->Add(m_title_temperature, 0, wxALIGN_CENTER, 0);

    m_sizer_temperature->Add(0, 0, 0, wxEXPAND, 0);

    wxBoxSizer *sizer_firstlayer = new wxBoxSizer(wxVERTICAL);
    m_label_firstlayer = new wxStaticText(m_panel_body, wxID_ANY, _L("First layer"), wxDefaultPosition, wxDefaultSize, 0);
    m_label_firstlayer->SetFont(::Label::Body_13);
    m_label_firstlayer->SetForegroundColour(AMS_MATERIALS_SETTING_GREY300);
    m_label_firstlayer->Wrap(-1);
    sizer_firstlayer->Add(m_label_firstlayer, 0, wxALIGN_CENTER, 0);

    wxBoxSizer * sizer_tempinput_first_layer = new wxBoxSizer(wxHORIZONTAL);;
    m_input_firstlayer = new TextInput(m_panel_body, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, AMS_MATERIALS_SETTING_INPUT_SIZE, wxTE_CENTER|wxTE_PROCESS_ENTER);
    m_input_firstlayer->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    auto bitmapfl        = new wxStaticBitmap(m_panel_body, -1, create_scaled_bitmap("degree", nullptr, 16), wxDefaultPosition, wxDefaultSize);
    sizer_tempinput_first_layer->Add(m_input_firstlayer, 0, wxALIGN_CENTER, 0);
    sizer_tempinput_first_layer->Add(bitmapfl, 0, wxALIGN_CENTER, 0);
    sizer_firstlayer->Add(sizer_tempinput_first_layer, 0, wxALIGN_CENTER, 0);


    wxBoxSizer *sizer_other = new wxBoxSizer(wxVERTICAL);
    m_label_other = new wxStaticText(m_panel_body, wxID_ANY, _L("Others"), wxDefaultPosition, wxDefaultSize, 0);
    m_label_other->SetFont(::Label::Body_13);
    m_label_other->SetForegroundColour(AMS_MATERIALS_SETTING_GREY300);
    m_label_other->Wrap(-1);
    sizer_other->Add(m_label_other, 0, wxALIGN_CENTER, 0);

    wxBoxSizer * sizer_tempinput_other= new wxBoxSizer(wxHORIZONTAL);;
    m_input_other = new TextInput(m_panel_body, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, AMS_MATERIALS_SETTING_INPUT_SIZE, wxTE_CENTER|wxTE_PROCESS_ENTER);
    m_input_other->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    auto bitmapother = new wxStaticBitmap(m_panel_body, -1, create_scaled_bitmap("degree", nullptr, 16), wxDefaultPosition, wxDefaultSize);
    sizer_tempinput_other->Add(m_input_other, 0, wxALIGN_CENTER, 0);
    sizer_tempinput_other->Add(bitmapother, 0, wxALIGN_CENTER, 0);
    sizer_other->Add(sizer_tempinput_other, 0, wxALIGN_CENTER, 0);


    m_sizer_temperature->Add(sizer_firstlayer, 0, wxALIGN_CENTER, 0);
    m_sizer_temperature->Add(0,0,0,wxLEFT,30);
    m_sizer_temperature->Add(sizer_other, 0, wxALIGN_CENTER, 0);

   
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
    m_sizer_body->Add(0,0,0,wxEXPAND | wxTOP, 24);
    m_sizer_body->Add(m_sizer_button, 0, wxEXPAND, 0);

    m_panel_body->SetSizer(m_sizer_body);
    m_panel_body->Layout();
    m_sizer_main->Add(m_panel_body, 0, wxALL | wxEXPAND, 24);

    this->SetSizer(m_sizer_main);
    this->Layout();
    m_sizer_main->Fit(this);

    this->Centre(wxBOTH);
}

void AMSMaterialsSetting::on_select_ok(wxMouseEvent &event)
{
    // TO DO
    event.Skip();


    auto first_layer_temp = m_input_firstlayer->GetTextCtrl()->GetValue();
    auto other_temp = m_input_other->GetTextCtrl()->GetValue();
    auto filament         = m_comboBox_filament->GetValue();


    EndModal(true);
}

void AMSMaterialsSetting::on_dpi_changed(const wxRect &suggested_rect) 
{
    m_button_confirm->SetMinSize(AMS_MATERIALS_SETTING_BUTTON_SIZE);
}

}} // namespace Slic3r::GUI