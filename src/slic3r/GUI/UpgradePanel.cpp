#include "UpgradePanel.hpp"
#include <slic3r/GUI/Widgets/Label.hpp>
#include <slic3r/GUI/I18N.hpp>
#include "GUI.hpp"

namespace Slic3r {
namespace GUI {


MachineInfoPanel::MachineInfoPanel(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style, const wxString& name)
    :wxPanel(parent, id, pos, size, style)
{
    wxBoxSizer* sizer;
    sizer = new wxBoxSizer(wxVERTICAL);

    m_panel_caption = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_panel_caption->SetBackgroundColour(wxColour(248, 248, 248));

    wxBoxSizer* bSizer_machine_caption;
    bSizer_machine_caption = new wxBoxSizer(wxHORIZONTAL);

    bSizer_machine_caption->SetMinSize(wxSize(FromDIP(718), FromDIP(36)));

    bSizer_machine_caption->Add(FromDIP(12), 0, 0, wxEXPAND, 0);

    m_staticText_machine_name = new wxStaticText(m_panel_caption, wxID_ANY, _L("N/A"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_machine_name->Wrap(-1);
    bSizer_machine_caption->Add(m_staticText_machine_name, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));


    m_panel_caption->SetSizer(bSizer_machine_caption);
    m_panel_caption->Layout();
    bSizer_machine_caption->Fit(m_panel_caption);
    sizer->Add(m_panel_caption, 0, wxALL | wxEXPAND, 0);

    m_panel_content = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_panel_content->SetBackgroundColour(wxColour(255, 255, 255));

    wxBoxSizer* bSizer_machine_content = new wxBoxSizer(wxVERTICAL);

    bSizer_machine_content->Add(0, FromDIP(40), 0, wxEXPAND, 0);

    wxBoxSizer* content_middle_sizer = new wxBoxSizer(wxHORIZONTAL);

    content_middle_sizer->Add(FromDIP(40), 0, 0, 0, 0);

    m_bitmap_machine = new wxStaticBitmap(m_panel_content, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    m_bitmap_machine->SetBitmap(create_scaled_bitmap("monitor_upgrade_machine", nullptr, 115));

    content_middle_sizer->Add(m_bitmap_machine, 0, wxBOTTOM, 10);


    content_middle_sizer->Add(FromDIP(40), 0, 0, 0, 0);

    wxBoxSizer* text_sizier = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer* model_text_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_staticText_model_title = new wxStaticText(m_panel_content, wxID_ANY, _L("Model:"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_model_title->Wrap(-1);
    m_staticText_model_title->SetFont(Label::Head_14);
    model_text_sizer->Add(m_staticText_model_title, 0, wxALL, FromDIP(5));

    m_staticText_model = new wxStaticText(m_panel_content, wxID_ANY, _L("N/A"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_model->Wrap(-1);
    model_text_sizer->Add(m_staticText_model, 0, wxALL, FromDIP(5));


    text_sizier->Add(model_text_sizer, 0, wxEXPAND, 0);

    wxBoxSizer* sn_text_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_staticText_serial_number_title = new wxStaticText(m_panel_content, wxID_ANY, _L("Serial Number:"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_serial_number_title->Wrap(-1);
    m_staticText_serial_number_title->SetFont(Label::Head_14);
    sn_text_sizer->Add(m_staticText_serial_number_title, 0, wxALL, FromDIP(5));

    m_staticText_serial_number = new wxStaticText(m_panel_content, wxID_ANY, _L("N/A"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_serial_number->Wrap(-1);
    sn_text_sizer->Add(m_staticText_serial_number, 0, wxALL, FromDIP(5));


    text_sizier->Add(sn_text_sizer, 0, wxEXPAND, 0);

    wxBoxSizer* version_text_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_staticText_software_version_title = new wxStaticText(m_panel_content, wxID_ANY, _L("Software Version:"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_software_version_title->Wrap(-1);
    m_staticText_software_version_title->SetFont(Label::Head_14);
    version_text_sizer->Add(m_staticText_software_version_title, 0, wxALL, FromDIP(5));

    m_staticText_software_version = new wxStaticText(m_panel_content, wxID_ANY, _L("N/A"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_software_version->Wrap(-1);
    version_text_sizer->Add(m_staticText_software_version, 0, wxALL, FromDIP(5));


    text_sizier->Add(version_text_sizer, 0, wxEXPAND, 0);


    content_middle_sizer->Add(text_sizier, 1, wxEXPAND, 0);

    m_button_upgrade_firmware = new Button(m_panel_content, _L("Upgrade firmware"));
    m_button_upgrade_firmware->SetCornerRadius(FromDIP(12));
    StateColor btn_bg(
        std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Enabled),
        std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
    StateColor btn_bd(
        std::pair<wxColour, int>(wxColour(144, 144, 144), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Enabled));
    StateColor btn_text(
        std::pair<wxColour, int>(wxColour(144, 144, 144), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Enabled));
    m_button_upgrade_firmware->SetBackgroundColor(btn_bg);
    m_button_upgrade_firmware->SetBorderColor(btn_bd);
    m_button_upgrade_firmware->SetTextColor(btn_text);
    m_button_upgrade_firmware->SetFont(Label::Body_10);
    m_button_upgrade_firmware->SetMinSize(wxSize(FromDIP(-1), FromDIP(24)));
    content_middle_sizer->Add(m_button_upgrade_firmware, 0, wxALL | wxALIGN_BOTTOM, 0);

    content_middle_sizer->Add(FromDIP(56), 0, 0, 0, 0);


    bSizer_machine_content->Add(content_middle_sizer, 0, wxEXPAND, 0);


    bSizer_machine_content->Add(0, FromDIP(25), 0, wxEXPAND, 0);


    m_panel_content->SetSizer(bSizer_machine_content);
    m_panel_content->Layout();
    bSizer_machine_content->Fit(m_panel_content);
    sizer->Add(m_panel_content, 1, wxEXPAND | wxALL, 0);

    this->SetSizerAndFit(sizer);

    // Connect Events
    m_button_upgrade_firmware->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MachineInfoPanel::upgrade_firmware), NULL, this);
}

MachineInfoPanel::~MachineInfoPanel()
{
    // Disconnect Events
    m_button_upgrade_firmware->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MachineInfoPanel::upgrade_firmware), NULL, this);
}

void MachineInfoPanel::update(MachineObject* obj, FirmwareInfo info)
{
    m_obj = obj;
    m_info = info;

    m_staticText_machine_name->SetLabel(from_u8(obj->dev_name));
    m_staticText_model->SetLabel(obj->get_printer_type_string());
    m_staticText_serial_number->SetLabel(obj->dev_id);

    wxString text_ver = wxString::Format("Ver %s", info.version);
    m_staticText_software_version->SetLabel(text_ver);

    if (!obj->upgrade_new_version) {
        m_button_upgrade_firmware->Enable(false);
    }

    Layout();
}

void MachineInfoPanel::upgrade_firmware(wxCommandEvent &event)
{
    if (m_obj) {
        ;//
    }
}

UpgradePanel::UpgradePanel(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size, long style)
    :wxPanel(parent, id, pos, size, style)
{
    this->SetBackgroundColour(wxColour(238, 238, 238));

    auto m_main_sizer = new wxBoxSizer(wxVERTICAL);

    m_scrolledWindow = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    m_scrolledWindow->SetScrollRate(5, 5);

    m_machine_list_sizer = new wxBoxSizer(wxVERTICAL);

    m_scrolledWindow->SetSizerAndFit(m_machine_list_sizer);

    m_main_sizer->Add(m_scrolledWindow, 1, wxALIGN_CENTER_HORIZONTAL | wxEXPAND, 0);

    this->SetSizerAndFit(m_main_sizer);


    Layout();
}

UpgradePanel::~UpgradePanel()
{

}

void UpgradePanel::clean_machine_info_list() {
    for (int i = 0; i < m_machine_info_panels.size(); i++) {
        delete m_machine_info_panels[i];
    }
    m_machine_info_panels.clear();
}

void UpgradePanel::update_machine_info_list(std::vector<MachineObject*>& obj_list) {

    Freeze();
    clean_machine_info_list();

    std::vector<MachineObject*>::iterator it;
    for (it = obj_list.begin(); it != obj_list.end(); it++)
    {
        for (auto item = (*it)->firmware_list.begin(); item != (*it)->firmware_list.end(); item++) {
            MachineInfoPanel *panel = new MachineInfoPanel(m_scrolledWindow);
            m_machine_info_panels.push_back(panel);
            panel->update(*it, (*item));
            panel->Layout();
            m_machine_list_sizer->Add(panel, 0, wxTOP | wxALIGN_CENTER_HORIZONTAL, FromDIP(8));
        }
    }

    Layout();
    Thaw();
}

void UpgradePanel::updata_machine_firmware_info(MachineObject *obj)
{
    Freeze();
    clean_machine_info_list();
    if (!obj) {
        ;
    } else {
        // for (auto item = obj->firmware_list.begin(); item != obj->firmware_list.end(); item++) {
        if (obj->firmware_list.size() > 0) {
            FirmwareInfo      info  = obj->firmware_list[0];
            MachineInfoPanel *panel = new MachineInfoPanel(m_scrolledWindow);
            m_machine_info_panels.push_back(panel);
            panel->update(obj, info);
            panel->Layout();
            m_machine_list_sizer->Add(panel, 0, wxTOP | wxALIGN_CENTER_HORIZONTAL, FromDIP(8));
        }
    }

    Layout();
    Thaw();
}

void UpgradePanel::update(MachineObject *obj_)
{
    if (obj != obj_) {
        if (obj_) {
            if (obj_->get_firmware_info()) {
                updata_machine_firmware_info(obj);
            }
        }
    } else {
        updata_machine_firmware_info(obj);
    }
    obj = obj_;
}

bool UpgradePanel::Show(bool show)
{
    return wxPanel::Show(show);
}

}
}