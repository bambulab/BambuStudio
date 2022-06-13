#include "UpgradePanel.hpp"
#include <slic3r/GUI/Widgets/Label.hpp>
#include <slic3r/GUI/I18N.hpp>
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "libslic3r/Thread.hpp"

namespace Slic3r {
namespace GUI {

static const wxColour TEXT_NORMAL_CLR = wxColour(0, 174, 66);
static const wxColour TEXT_FAILED_CLR = wxColour(255, 111, 0);

MachineInfoPanel::MachineInfoPanel(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style, const wxString& name)
    :wxPanel(parent, id, pos, size, style)
{
    this->SetBackgroundColour(wxColour(255, 255, 255));

    init_bitmaps();

    wxBoxSizer *m_top_sizer = new wxBoxSizer(wxVERTICAL);

    m_panel_caption = create_caption_panel(this);

    m_top_sizer->Add(m_panel_caption, 0, wxEXPAND | wxALL, 0);

    wxBoxSizer *m_main_sizer = new wxBoxSizer(wxHORIZONTAL);

    wxBoxSizer *m_main_left_sizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer *m_ota_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_printer_img = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(200, 200), 0);
    m_printer_img->SetBitmap(create_scaled_bitmap("monitor_upgrade_printer", nullptr, 200));
    m_ota_sizer->Add(m_printer_img, 0, wxALIGN_CENTER_VERTICAL | wxALL, 0);

    wxBoxSizer *m_ota_content_sizer = new wxBoxSizer(wxVERTICAL);

    m_ota_content_sizer->Add(0, 0, 1, wxEXPAND, 0);

    wxFlexGridSizer *m_ota_info_sizer = new wxFlexGridSizer(0, 2, 0, 0);
    m_ota_info_sizer->AddGrowableCol(1);
    m_ota_info_sizer->SetFlexibleDirection(wxHORIZONTAL);
    m_ota_info_sizer->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);

    m_staticText_model_id = new wxStaticText(this, wxID_ANY, _L("Model:"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_model_id->Wrap(-1);
    m_staticText_model_id->SetFont(Label::Head_14);
    m_ota_info_sizer->Add(m_staticText_model_id, 0, wxALIGN_RIGHT | wxALL, 5);

    m_staticText_model_id_val = new wxStaticText(this, wxID_ANY, "-", wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_model_id_val->Wrap(-1);
    m_ota_info_sizer->Add(m_staticText_model_id_val, 0, wxALL | wxEXPAND, 5);

    m_staticText_sn = new wxStaticText(this, wxID_ANY, _L("Serial:"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_sn->Wrap(-1);
    m_staticText_sn->SetFont(Label::Head_14);
    m_ota_info_sizer->Add(m_staticText_sn, 0, wxALIGN_RIGHT | wxALL | wxEXPAND, 5);

    m_staticText_sn_val = new wxStaticText(this, wxID_ANY, "-", wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_sn_val->Wrap(-1);
    m_ota_info_sizer->Add(m_staticText_sn_val, 0, wxALL | wxEXPAND, 5);

    wxBoxSizer *m_ota_ver_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_ota_ver_sizer->Add(0, 0, 1, wxEXPAND, 0);

    m_ota_new_version_img = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(5, 5), 0);
    m_ota_new_version_img->SetBitmap(upgrade_green_icon);
    m_ota_ver_sizer->Add(m_ota_new_version_img, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);

    m_staticText_ver = new wxStaticText(this, wxID_ANY, _L("Version:"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_ver->Wrap(-1);
    m_staticText_ver->SetFont(Label::Head_14);
    m_ota_ver_sizer->Add(m_staticText_ver, 0, wxALIGN_RIGHT | wxALL, 5);

    m_ota_info_sizer->Add(m_ota_ver_sizer, 0, wxEXPAND, 0);

    m_staticText_ver_val = new wxStaticText(this, wxID_ANY, "-", wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_ver_val->Wrap(-1);
    m_ota_info_sizer->Add(m_staticText_ver_val, 0, wxALL | wxEXPAND, 5);

    m_ota_content_sizer->Add(m_ota_info_sizer, 0, wxEXPAND, 0);

    m_ota_content_sizer->Add(0, 0, 1, wxEXPAND, 0);

    m_ota_sizer->Add(m_ota_content_sizer, 1, wxEXPAND, 0);

    m_main_left_sizer->Add(m_ota_sizer, 0, wxEXPAND, 0);

    m_staticline = new wxStaticLine(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL);
    m_staticline->SetBackgroundColour(wxColour(206,206,206));
    m_staticline->Show(false);
    m_main_left_sizer->Add(m_staticline, 0, wxEXPAND | wxLEFT, 40);

    m_ams_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_ams_img = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(200, 200), 0);
    m_ams_img->SetBitmap(create_scaled_bitmap("monitor_upgrade_ams", nullptr, 200));
    m_ams_sizer->Add(m_ams_img, 0, wxALIGN_TOP | wxALL, 5);

    wxBoxSizer *m_ams_content_sizer;
    m_ams_content_sizer = new wxBoxSizer(wxVERTICAL);

    m_ams_content_sizer->Add(0, 20, 0, wxEXPAND, 5);

    wxGridSizer *m_ams_info_sizer = new wxGridSizer(0, 2, 0, 0);

    wxFlexGridSizer *m_ams0_sizer = create_ams_sizer();

    m_ams_info_sizer->Add(m_ams0_sizer, 1, wxEXPAND, 5);

    wxFlexGridSizer *m_ams1_sizer;
    m_ams1_sizer = new wxFlexGridSizer(0, 2, 0, 0);
    m_ams1_sizer->SetFlexibleDirection(wxBOTH);
    m_ams1_sizer->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);

    m_ams_info_sizer->Add(m_ams1_sizer, 1, wxEXPAND, 5);

    m_ams_content_sizer->Add(m_ams_info_sizer, 1, wxEXPAND, 0);

    m_ams_sizer->Add(m_ams_content_sizer, 1, wxEXPAND, 0);

    m_main_left_sizer->Add(m_ams_sizer, 0, wxEXPAND, 0);

    //Hide ams
    show_ams(false, true);

    m_main_sizer->Add(m_main_left_sizer, 1, wxEXPAND, 0);

    wxBoxSizer *m_main_right_sizer = new wxBoxSizer(wxVERTICAL);

    m_main_right_sizer->SetMinSize(wxSize(137, -1));

    m_main_right_sizer->Add(0, 50, 0, wxEXPAND, 5);

    m_button_upgrade_firmware = new Button(this, _L("Upgrade firmware"));
    StateColor btn_bg(std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Disabled), std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed),
                      std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered), std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Enabled),
                      std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
    StateColor btn_bd(std::pair<wxColour, int>(wxColour(144, 144, 144), StateColor::Disabled), std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Enabled));
    StateColor btn_text(std::pair<wxColour, int>(wxColour(144, 144, 144), StateColor::Disabled), std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Enabled));
    m_button_upgrade_firmware->SetBackgroundColor(btn_bg);
    m_button_upgrade_firmware->SetBorderColor(btn_bd);
    m_button_upgrade_firmware->SetTextColor(btn_text);
    m_button_upgrade_firmware->SetFont(Label::Body_10);
    m_button_upgrade_firmware->SetMinSize(wxSize(FromDIP(-1), FromDIP(24)));
    m_button_upgrade_firmware->SetCornerRadius(FromDIP(12));
    m_main_right_sizer->Add(m_button_upgrade_firmware, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, 5);

    m_staticText_upgrading_info = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_upgrading_info->Wrap(-1);
    m_main_right_sizer->Add(m_staticText_upgrading_info, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, 5);

    m_upgrading_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_upgrading_sizer->Add(0, 0, 1, wxEXPAND, 0);

    m_upgrade_progress = new ProgressBar(this, wxID_ANY, 100, wxDefaultPosition, wxDefaultSize);
    m_upgrade_progress->SetValue(0);
    m_upgrade_progress->SetSize(wxSize(54, 14));
    m_upgrade_progress->SetMinSize(wxSize(54, 14));
    m_upgrading_sizer->Add(m_upgrade_progress, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);

    m_staticText_upgrading_percent = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_upgrading_percent->Wrap(-1);
    m_upgrading_sizer->Add(m_staticText_upgrading_percent, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);

    m_upgrade_retry_img = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    m_upgrading_sizer->Add(m_upgrade_retry_img, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);

    m_upgrading_sizer->Add(0, 0, 1, wxEXPAND, 0);

    m_main_right_sizer->Add(m_upgrading_sizer, 0, wxEXPAND, 0);

    m_staticText_release_note = new wxStaticText(this, wxID_ANY, _L("Relase Note"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_release_note->Wrap(-1);
    m_staticText_release_note->SetForegroundColour(wxColour(31, 142, 234));

    m_main_right_sizer->Add(m_staticText_release_note, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, 0);

    m_main_right_sizer->Add(0, 0, 1, wxEXPAND, 0);

    m_main_sizer->Add(m_main_right_sizer, 0, wxEXPAND, 0);

    m_top_sizer->Add(m_main_sizer, 1, wxEXPAND, 0);

    this->SetSizer(m_top_sizer);
    this->Layout();

    // Connect Events
    m_upgrade_retry_img->Bind(wxEVT_LEFT_UP, [this](auto &e) {
        upgrade_firmware_internal();
        });
    m_button_upgrade_firmware->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MachineInfoPanel::on_upgrade_firmware), NULL, this);
}

wxPanel *MachineInfoPanel::create_caption_panel(wxWindow *parent)
{
    auto caption_panel = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    caption_panel->SetBackgroundColour(wxColour(248, 248, 248));
    caption_panel->SetMinSize(wxSize(FromDIP(850), FromDIP(36)));

    wxBoxSizer *m_caption_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_caption_sizer->Add(17, 0, 0, wxEXPAND, 0);

    m_upgrade_status_img = new wxStaticBitmap(caption_panel, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(5, 5), 0);
    m_upgrade_status_img->SetBitmap(upgrade_gray_icon);
    m_caption_sizer->Add(m_upgrade_status_img, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);

    m_caption_text = new wxStaticText(caption_panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
    m_caption_text->Wrap(-1);
    m_caption_sizer->Add(m_caption_text, 1, wxALIGN_CENTER_VERTICAL | wxALL, 5);

    caption_panel->SetSizer(m_caption_sizer);
    caption_panel->Layout();
    m_caption_sizer->Fit(caption_panel);

    return caption_panel;
}

wxFlexGridSizer* MachineInfoPanel::create_ams_sizer()
{
    auto ams_sizer = new wxFlexGridSizer(0, 2, 0, 0);
    ams_sizer->AddGrowableCol(1);
    ams_sizer->SetFlexibleDirection(wxHORIZONTAL);
    ams_sizer->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);

    m_staticText_ams = new wxStaticText(this, wxID_ANY, _L("AMS"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_ams->SetFont(Label::Head_14);
    m_staticText_ams->Wrap(-1);
    ams_sizer->Add(m_staticText_ams, 0, wxALIGN_RIGHT | wxALL, 5);

    ams_sizer->Add(0, 0, 1, wxEXPAND, 5);

    m_staticText_ams_sn = new wxStaticText(this, wxID_ANY, _L("Serial:"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_ams_sn->Wrap(-1);
    m_staticText_ams_sn->SetFont(Label::Head_14);
    ams_sizer->Add(m_staticText_ams_sn, 0, wxALIGN_RIGHT | wxALL, 5);

    m_staticText_ams_sn_val = new wxStaticText(this, wxID_ANY, "-", wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_ams_sn_val->Wrap(-1);
    ams_sizer->Add(m_staticText_ams_sn_val, 0, wxALL | wxEXPAND, 5);

    wxBoxSizer *m_ams_ver_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_ams_ver_sizer->Add(0, 0, 1, wxEXPAND, 0);

    m_ams_new_version_img = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(5, 5), 0);
    m_ams_new_version_img->SetBitmap(upgrade_green_icon);
    //m_ams_new_version_img->Hide();
    m_ams_ver_sizer->Add(m_ams_new_version_img, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);

    m_staticText_ams_ver = new wxStaticText(this, wxID_ANY, _L("Version:"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_ams_ver->Wrap(-1);
    m_staticText_ams_ver->SetFont(Label::Head_14);
    m_ams_ver_sizer->Add(m_staticText_ams_ver, 0, wxALIGN_RIGHT | wxALL, 5);

    ams_sizer->Add(m_ams_ver_sizer, 1, wxEXPAND, 5);

    m_staticText_ams_ver_val = new wxStaticText(this, wxID_ANY, "-", wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_ams_ver_val->Wrap(-1);
    ams_sizer->Add(m_staticText_ams_ver_val, 0, wxALL | wxEXPAND, 5);

    ams_sizer->Add(0, 0, 1, wxEXPAND, 0);
    return ams_sizer;
}


void MachineInfoPanel::init_bitmaps()
{
    upgrade_green_icon   = create_scaled_bitmap("monitor_upgrade_online", nullptr, 5);
    upgrade_gray_icon    = create_scaled_bitmap("monitor_upgrade_offline", nullptr, 5);
    upgrade_yellow_icon  = create_scaled_bitmap("monitor_upgrade_busy", nullptr, 5);
}

MachineInfoPanel::~MachineInfoPanel()
{
    // Disconnect Events
    m_button_upgrade_firmware->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MachineInfoPanel::on_upgrade_firmware), NULL, this);
}

void MachineInfoPanel::update(MachineObject* obj)
{
    m_obj = obj;
    if (obj) {
        this->Freeze();
        //update online status img
        m_panel_caption->Freeze();
        if (!obj->is_connected()) {
            m_upgrade_status_img->SetBitmap(upgrade_gray_icon);
            wxString caption_text = wxString::Format("%s(%s)", from_u8(obj->dev_name), _L("Offline"));
            m_caption_text->SetLabelText(caption_text);
            show_status(MachineObject::UpgradingDisplayState::UpgradingUnavaliable);
        } else {
            show_status(obj->upgrade_display_state);
            if (obj->upgrade_display_state == (int) MachineObject::UpgradingDisplayState::UpgradingUnavaliable) {
                if (obj->can_abort()) {
                    wxString caption_text = wxString::Format("%s(%s)", from_u8(obj->dev_name), _L("Printing"));
                    m_caption_text->SetLabelText(caption_text);
                } else {
                    wxString caption_text = wxString::Format("%s", from_u8(obj->dev_name));
                    m_caption_text->SetLabelText(caption_text);
                }
                m_upgrade_status_img->SetBitmap(upgrade_yellow_icon);
            } else {
                wxString caption_text = wxString::Format("%s(%s)", from_u8(obj->dev_name), _L("Idle"));
                m_caption_text->SetLabelText(caption_text);
                m_upgrade_status_img->SetBitmap(upgrade_green_icon);
            }
        }
        m_panel_caption->Layout();
        m_panel_caption->Thaw();

        // update version
        update_version_text(obj);

        // update ams
        update_ams(obj);

        //update progress
        int upgrade_percent = obj->get_upgrade_percent();
        if (obj->upgrade_display_state == (int) MachineObject::UpgradingDisplayState::UpgradingInProgress) {
            m_upgrade_progress->SetValue(upgrade_percent);
            m_staticText_upgrading_percent->SetLabelText(wxString::Format("%d%%", upgrade_percent));
        } else if (obj->upgrade_display_state == (int) MachineObject::UpgradingDisplayState::UpgradingFinished) {
            wxString result_text = obj->get_upgrade_result_str(obj->upgrade_err_code);
            m_staticText_upgrading_info->SetLabelText(result_text);
            m_upgrade_progress->SetValue(upgrade_percent);
            m_staticText_upgrading_percent->SetLabelText(wxString::Format("%d%%", upgrade_percent));
        }

        wxString model_id_text = obj->get_printer_type_display_str();
        m_staticText_model_id_val->SetLabelText(model_id_text);
        wxString sn_text = obj->dev_id;
        m_staticText_sn_val->SetLabelText(sn_text.MakeUpper());

        this->Layout();
        this->Thaw();
    }
}

void MachineInfoPanel::update_version_text(MachineObject* obj)
{
    if (obj->upgrade_display_state == (int)MachineObject::UpgradingDisplayState::UpgradingInProgress) {
        m_staticText_ver_val->SetLabelText("-");
        m_staticText_ams_ver_val->SetLabelText("-");
        m_ota_new_version_img->Hide();
    } else {
        // update version text
        auto it = obj->module_vers.find("ota");
        if (obj->upgrade_new_version
            && !obj->ota_new_version_number.empty()) {
            if (it != obj->module_vers.end()) {
                wxString ver_text = wxString::Format("%s->%s", it->second.sw_ver, obj->ota_new_version_number);
                m_staticText_ver_val->SetLabelText(ver_text);
            }
            else {
                m_staticText_ver_val->SetLabelText("-");
            }
            m_ota_new_version_img->Show();
        }
        else {
            if (it != obj->module_vers.end()) {
                wxString ver_text = wxString::Format("%s(%s)", it->second.sw_ver, _L("Lastest version"));
                m_staticText_ver_val->SetLabelText(ver_text);
            }
            else {
                m_staticText_ver_val->SetLabelText("-");
            }
            m_ota_new_version_img->Hide();
        }
    }
}

void MachineInfoPanel::update_ams(MachineObject *obj)
{
    if (obj->ams_exist_bits != 0) {
        show_ams(true);
        std::map<int, MachineObject::ModuleVersionInfo> ver_list = obj->get_ams_version();
        
        for (int i = 0; i < 1; i++) {
            auto it = ver_list.find(i);
            if (it == ver_list.end()) {
                // hide this ams
            } else {
                // update ams img
                wxString ams_text = wxString::Format("AMS%s", std::to_string(i+1));
                m_staticText_ams->SetLabelText(ams_text);
                if (obj->upgrade_new_version
                    && !obj->ams_new_version_number.empty()
                    && obj->ams_new_version_number.compare(it->second.sw_ver) != 0) { 
                    m_ams_new_version_img->Show();
                    wxString ver_text = wxString::Format("%s->%s", it->second.sw_ver, obj->ams_new_version_number);
                    m_staticText_ams_ver_val->SetLabelText(ver_text);
                } else {
                    m_ams_new_version_img->Show(false);
                    wxString ver_text = wxString::Format("%s(%s)", it->second.sw_ver, _L("Lastest version"));
                    m_staticText_ams_ver_val->SetLabelText(ver_text);
                }
                // update ams sn
                if (it->second.sn.empty()) {
                    m_staticText_ams_sn_val->SetLabelText("-");
                } else {
                    wxString sn_text = it->second.sn;
                    m_staticText_ams_sn_val->SetLabelText(sn_text.MakeUpper());
                }
            }
        }
    } else {
        show_ams(false);
    }
    this->Layout();
}

void MachineInfoPanel::show_status(int status)
{
    if (last_status == status)
        return;
    last_status = status;

    BOOST_LOG_TRIVIAL(trace) << "MachineInfoPanel: show_status = " << status;

    Freeze();
    
    if (status == (int)MachineObject::UpgradingDisplayState::UpgradingUnavaliable) {
        m_button_upgrade_firmware->Show();
        m_button_upgrade_firmware->Disable();
        for (size_t i = 0; i < m_upgrading_sizer->GetItemCount(); i++) {
            m_upgrading_sizer->Show(false);
        }
        m_upgrade_retry_img->Hide();
        m_staticText_upgrading_info->Hide();
        m_staticText_upgrading_percent->Hide();
    } else if (status == (int) MachineObject::UpgradingDisplayState::UpgradingAvaliable) {
        m_button_upgrade_firmware->Show();
        m_button_upgrade_firmware->Enable();
        for (size_t i = 0; i < m_upgrading_sizer->GetItemCount(); i++) { m_upgrading_sizer->Show(false); }
        m_upgrade_retry_img->Hide();
        m_staticText_upgrading_info->Hide();
        m_staticText_upgrading_percent->Hide();
    } else if (status == (int) MachineObject::UpgradingDisplayState::UpgradingInProgress) {
        m_button_upgrade_firmware->Hide();
        for (size_t i = 0; i < m_upgrading_sizer->GetItemCount(); i++) { m_upgrading_sizer->Show(true); }
        m_upgrade_retry_img->Hide();
        m_staticText_upgrading_info->Show();
        m_staticText_upgrading_info->SetLabel(_L("Upgrading"));
        m_staticText_upgrading_info->SetForegroundColour(TEXT_NORMAL_CLR);
        m_staticText_upgrading_percent->SetForegroundColour(TEXT_NORMAL_CLR);
        m_staticText_upgrading_percent->Show();
    } else if (status == (int) MachineObject::UpgradingDisplayState::UpgradingFinished) {
        if (true) {
            for (size_t i = 0; i < m_upgrading_sizer->GetItemCount(); i++) { m_upgrading_sizer->Show(true); }
            m_button_upgrade_firmware->Hide();
            m_staticText_upgrading_info->SetLabel(_L("Upgrading success"));
            m_staticText_upgrading_info->Show();
            m_staticText_upgrading_info->SetForegroundColour(TEXT_NORMAL_CLR);
            m_staticText_upgrading_percent->SetForegroundColour(TEXT_NORMAL_CLR);
            m_upgrade_retry_img->Hide();
        } else {
            m_staticText_upgrading_info->SetLabel(_L("Upgrading failed"));
            m_staticText_upgrading_info->SetForegroundColour(TEXT_FAILED_CLR);
            for (size_t i = 0; i < m_upgrading_sizer->GetItemCount(); i++) { m_upgrading_sizer->Show(true); }
            m_button_upgrade_firmware->Hide();
            m_staticText_upgrading_info->Show();
            m_staticText_upgrading_percent->Hide();
            m_upgrade_retry_img->Show();
        }
    } else {
        ;
    }
    Layout();
    Thaw();

}

void MachineInfoPanel::show_ams(bool show, bool force_update)
{
    if (m_last_ams_show != show || force_update) {
        m_ams_sizer->Show(show);
        m_staticline->Show(show);
        BOOST_LOG_TRIVIAL(trace) << "upgrade: show_ams = " << show;
    }
    m_last_ams_show = show;
}

void MachineInfoPanel::upgrade_firmware_internal() {
    if (!m_obj)
        return;
    if (panel_type == ptOtaPanel) {
        m_obj->command_upgrade_firmware(m_ota_info);
    } else if (panel_type == ptAmsPanel) {
        m_obj->command_upgrade_firmware(m_ams_info);
    } else if (panel_type == ptPushPanel) {
        m_obj->command_upgrade_confirm();
    }
}

void MachineInfoPanel::on_upgrade_firmware(wxCommandEvent &event)
{
    if (m_obj)
        m_obj->command_upgrade_confirm();
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

void UpgradePanel::clean_push_upgrade_panel()
{
    if (m_push_upgrade_panel) {
        delete m_push_upgrade_panel;
        m_push_upgrade_panel = nullptr;
    }
}

void UpgradePanel::refresh_version_and_firmware(MachineObject* obj)
{
    BOOST_LOG_TRIVIAL(trace) << "refresh version";
    if (obj) {
        m_obj->command_get_version();
        boost::thread update_info_thread = Slic3r::create_thread([this] {
            m_obj->get_firmware_info();
            int count = 0;
            while (count < 100) {
                if (!m_obj->module_vers.empty()) break;
                boost::this_thread::sleep_for(boost::chrono::milliseconds(100));
                count++;
            }
            if (count == 100)
                BOOST_LOG_TRIVIAL(trace) << "get_firmware_info timeout";
            m_initialized = true;
        });
    }
}

void UpgradePanel::update(MachineObject *obj)
{
    if (m_obj != obj) {
        m_obj = obj;
        refresh_version_and_firmware(obj);
    }

    Freeze();
    // init after imitialized
    if (m_initialized) {
        clean_push_upgrade_panel();
        m_push_upgrade_panel = new MachineInfoPanel(m_scrolledWindow);
        m_machine_list_sizer->Add(m_push_upgrade_panel, 0, wxTOP | wxALIGN_CENTER_HORIZONTAL, FromDIP(8));
        m_initialized = false;
    }

    //update panels
    if (m_push_upgrade_panel) {
        m_push_upgrade_panel->update(obj);
    }

    if (!obj)
        clean_push_upgrade_panel();
    this->Layout();
    Thaw();

    m_obj = obj;
}

bool UpgradePanel::Show(bool show)
{
    if (show) {
        DeviceManager* dev = wxGetApp().getDeviceManager();
        if (dev) {
            MachineObject* obj = dev->get_default_machine();
            refresh_version_and_firmware(obj);
        }
    }
    return wxPanel::Show(show);
}

}
}