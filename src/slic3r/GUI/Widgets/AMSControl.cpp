#include "AMSControl.hpp"
#include "Label.hpp"
#include "../BitmapCache.hpp"
#include "../I18N.hpp"
#include "../GUI_App.hpp"

#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/DeviceTab/uiAmsHumidityPopup.h"

#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevFilaSwitch.h"

#include <wx/simplebook.h>
#include <wx/dcgraph.h>

#include <boost/log/trivial.hpp>

#include "CalibUtils.hpp"

namespace Slic3r { namespace GUI {

#define AMS_CANS_SIZE wxSize(FromDIP(284), -1)
#define AMS_CANS_WINDOW_SIZE wxSize(FromDIP(264), -1)
#define SINGLE_SLOT_AMS_PANEL_SIZE wxSize(FromDIP(264), FromDIP(160))


AMSControl::AMSControl(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size)
    : wxSimplebook(parent, wxID_ANY, pos, size)
    , m_Humidity_tip_popup(AmsHumidityTipPopup(this))
    , m_percent_humidity_dry_popup(new uiAmsPercentHumidityDryPopup(this))
    , m_ams_introduce_popup(AmsIntroducePopup(this))
    , m_ams_dry_ctr_win(new AMSDryCtrWin(this))
{
    Slic3r::DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (dev) {
        MachineObject *obj = dev->get_selected_machine();
        parse_object(obj);
    }

    SetBackgroundColour(*wxWHITE);
    // normal mode
    //Freeze();
    m_sizer_body = new wxBoxSizer(wxVERTICAL);
    m_amswin                 = new wxWindow(this, wxID_ANY);
    m_amswin->SetBackgroundColour(*wxWHITE);
    m_amswin->SetSize(wxSize(FromDIP(578), -1));
    m_amswin->SetMinSize(wxSize(FromDIP(578), -1));


    m_sizer_ams_items = new wxBoxSizer(wxHORIZONTAL);

    /*right items*/
    m_panel_prv_left = new wxScrolledWindow(m_amswin, wxID_ANY);
    m_panel_prv_left->SetScrollRate(10, 0);
    m_panel_prv_left->SetSize(AMS_ITEMS_PANEL_SIZE);
    m_panel_prv_left->SetMinSize(AMS_ITEMS_PANEL_SIZE);
    //m_panel_prv_left->SetBackgroundColour(0x4169E1);
    m_panel_prv_left->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);
    m_sizer_prv_left = new wxBoxSizer(wxHORIZONTAL);
    m_panel_prv_left->SetSizer(m_sizer_prv_left);
    m_panel_prv_left->Layout();
    //m_sizer_items_left->Fit(m_panel_prv_left);

    /*right items*/
    m_panel_prv_right = new wxScrolledWindow(m_amswin, wxID_ANY);
    m_panel_prv_right->SetScrollRate(10, 0);
    m_panel_prv_right->SetSize(AMS_ITEMS_PANEL_SIZE);
    m_panel_prv_right->SetMinSize(AMS_ITEMS_PANEL_SIZE);
    //m_panel_prv_right->SetBackgroundColour(0x4169E1);
    m_panel_prv_right->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);
    m_sizer_prv_right = new wxBoxSizer(wxHORIZONTAL);
    m_panel_prv_right->SetSizer(m_sizer_prv_right);
    m_panel_prv_right->Layout();
    //m_sizer_items_right->Fit(m_panel_prv_right);

    /*m_sizer_ams_items->Add(m_panel_prv_left, 0, wxALIGN_CENTER|wxLEFT|wxRIGHT, FromDIP(5));
    m_sizer_ams_items->Add(m_panel_prv_right, 0, wxALIGN_CENTER|wxLEFT|wxRIGHT, FromDIP(5));*/
    m_sizer_ams_items->Add(m_panel_prv_left, 0, wxLEFT | wxRIGHT, FromDIP(5));
    m_sizer_ams_items->Add(m_panel_prv_right, 0, wxLEFT | wxRIGHT, FromDIP(5));

    //m_panel_prv_right->Hide();

    //m_sizer_ams_body = new wxBoxSizer(wxHORIZONTAL);

    m_sizer_ams_body = new wxBoxSizer(wxHORIZONTAL);

    //ams area
    m_sizer_ams_area_left = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_ams_area_right = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_down_road = new wxBoxSizer(wxHORIZONTAL);

    m_simplebook_ams_left = new wxSimplebook(m_amswin, wxID_ANY, wxDefaultPosition, AMS_CANS_WINDOW_SIZE, 0);
    m_simplebook_ams_left->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);
    //m_sizer_ams_area_left->Add(m_simplebook_ams_left, 0, wxLEFT | wxRIGHT, FromDIP(5));
    m_sizer_ams_area_left->Add(m_simplebook_ams_left, 0, wxALIGN_CENTER, 0);

    m_simplebook_ams_right = new wxSimplebook(m_amswin, wxID_ANY, wxDefaultPosition, AMS_CANS_WINDOW_SIZE, 0);
    m_simplebook_ams_right->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);
    //m_sizer_ams_area_right->Add(m_simplebook_ams_right, 0, wxLEFT | wxRIGHT, FromDIP(5));
    m_sizer_ams_area_right->Add(m_simplebook_ams_right, 0, wxALIGN_CENTER, 0);

    m_panel_down_road = new wxPanel(m_amswin, wxID_ANY, wxDefaultPosition, AMS_DOWN_ROAD_SIZE, 0);
    m_panel_down_road->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);

    m_down_road = new AMSRoadDownPart(m_panel_down_road, wxID_ANY, wxDefaultPosition, AMS_DOWN_ROAD_SIZE);
    m_sizer_down_road->Add(m_panel_down_road, 0, wxTOP, 0);

    // ams mode
    //
    m_simplebook_ams_right->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);


    m_sizer_ams_area_left->Layout();
    m_sizer_ams_area_right->Layout();


    m_sizer_ams_option = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_option_left = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_option_mid = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_option_right = new wxBoxSizer(wxHORIZONTAL);

    auto m_panel_option_left    = new wxPanel(m_amswin);
    auto m_panel_option_right   = new wxPanel(m_amswin);

    m_panel_option_left->SetBackgroundColour(*wxWHITE);
    m_panel_option_right->SetBackgroundColour(*wxWHITE);

    m_panel_option_left->SetSizer(m_sizer_option_left);
    m_panel_option_right->SetSizer(m_sizer_option_right);

    m_panel_option_left->SetMinSize(wxSize(FromDIP(180), -1));
    m_panel_option_left->SetMaxSize(wxSize(FromDIP(180), -1));

    m_panel_option_right->SetMinSize(wxSize(FromDIP(180), -1));
    m_panel_option_right->SetMaxSize(wxSize(FromDIP(180), -1));

    StateColor btn_bg_green(std::pair<wxColour, int>(AMS_CONTROL_DISABLE_COLOUR, StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));

    StateColor btn_bg_white(std::pair<wxColour, int>(AMS_CONTROL_DISABLE_COLOUR, StateColor::Disabled),
        std::pair<wxColour, int>(AMS_CONTROL_DISABLE_COLOUR, StateColor::Pressed),
        std::pair<wxColour, int>(AMS_CONTROL_DEF_BLOCK_BK_COLOUR, StateColor::Hovered),
        std::pair<wxColour, int>(AMS_CONTROL_WHITE_COLOUR, StateColor::Normal));

    StateColor btn_bd_green(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
        std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Enabled));

    StateColor btn_bd_white(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));

    StateColor btn_text_green(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Enabled));

    StateColor btn_text_white(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));


    /*option left*/
    m_button_auto_refill = new Button(m_panel_option_left, _L("Auto-refill"));
    m_button_auto_refill->SetBackgroundColor(btn_bg_white);
    m_button_auto_refill->SetBorderColor(btn_bd_white);
    m_button_auto_refill->SetTextColor(btn_text_white);
    m_button_auto_refill->SetFont(Label::Body_13);
    m_button_auto_refill->SetMinSize(wxSize(FromDIP(80), FromDIP(34)));
    m_button_auto_refill->SetMaxSize(wxSize(FromDIP(80), FromDIP(34)));

    m_button_ams_setting_normal = ScalableBitmap(this, "ams_setting_normal", 24);
    m_button_ams_setting_hover = ScalableBitmap(this, "ams_setting_hover", 24);
    m_button_ams_setting_press = ScalableBitmap(this, "ams_setting_press", 24);

    m_button_ams_setting = new wxStaticBitmap(m_panel_option_left, wxID_ANY, m_button_ams_setting_normal.bmp(), wxDefaultPosition, wxSize(FromDIP(24), FromDIP(24)));
    m_sizer_option_left->Add(m_button_auto_refill, 0, wxALIGN_CENTER, 0);
    m_sizer_option_left->Add(0, 0, 0, wxLEFT, FromDIP(20));
    m_sizer_option_left->Add(m_button_ams_setting, 0, wxALIGN_CENTER, 0);


    /*option mid*/
    m_extruder = new AMSextruder(m_amswin, wxID_ANY, m_total_ext_count, wxDefaultPosition, AMS_EXTRUDER_SIZE);
    m_sizer_option_mid->Add( m_extruder, 0, wxALIGN_CENTER, 0 );


    /*option right*/
    m_button_extruder_feed = new Button(m_panel_option_right, _L("Load"));
    m_button_extruder_feed->SetFont(Label::Body_13);
    m_button_extruder_feed->SetBackgroundColor(btn_bg_green);
    m_button_extruder_feed->SetBorderColor(btn_bd_green);
    m_button_extruder_feed->SetTextColor(btn_text_green);
    m_button_extruder_feed->SetMinSize(wxSize(FromDIP(80),FromDIP(34)));
    m_button_extruder_feed->SetMaxSize(wxSize(FromDIP(80),FromDIP(34)));
    m_button_extruder_feed->EnableTooltipEvenDisabled();


    if (wxGetApp().app_config->get("language") == "de_DE") m_button_extruder_feed->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "fr_FR") m_button_extruder_feed->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "ru_RU") m_button_extruder_feed->SetLabel("Load");
    if (wxGetApp().app_config->get("language") == "nl_NL") m_button_extruder_feed->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "hu_HU") m_button_extruder_feed->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "ja_JP") m_button_extruder_feed->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "sv_SE") m_button_extruder_feed->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "cs_CZ") m_button_extruder_feed->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "uk_UA") m_button_extruder_feed->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "pt_BR") m_button_extruder_feed->SetLabel("Load");

    m_button_extruder_back = new Button(m_panel_option_right, _L("Unload"));
    m_button_extruder_back->SetBackgroundColor(btn_bg_white);
    m_button_extruder_back->SetBorderColor(btn_bd_white);
    m_button_extruder_back->SetTextColor(btn_text_white);
    m_button_extruder_back->SetFont(Label::Body_13);
    m_button_extruder_back->SetMinSize(wxSize(FromDIP(80), FromDIP(34)));
    m_button_extruder_back->SetMaxSize(wxSize(FromDIP(80), FromDIP(34)));
    m_button_extruder_back->EnableTooltipEvenDisabled();

    if (wxGetApp().app_config->get("language") == "de_DE") m_button_extruder_back->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "fr_FR") m_button_extruder_back->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "ru_RU") m_button_extruder_back->SetLabel("Unload");
    if (wxGetApp().app_config->get("language") == "nl_NL") m_button_extruder_back->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "hu_HU") m_button_extruder_back->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "ja_JP") m_button_extruder_back->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "sv_SE") m_button_extruder_back->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "cs_CZ") m_button_extruder_back->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "uk_UA") m_button_extruder_back->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "pt_BR") m_button_extruder_back->SetLabel("Unload");


    //m_sizer_option_right->Add(0, 0, 1, wxEXPAND, 0);
    m_sizer_option_right->Add(m_button_extruder_back, 0, wxLEFT, FromDIP(0));
    m_sizer_option_right->Add(m_button_extruder_feed, 0, wxLEFT, FromDIP(20));

    m_panel_option_left->Layout();
    m_panel_option_right->Layout();

    m_sizer_ams_option->Add(m_panel_option_left, 0, wxALIGN_TOP, 0);
    m_sizer_ams_option->Add( 0, 0, 1, wxEXPAND, 0);
    m_sizer_ams_option->Add(m_sizer_option_mid, 0, wxALIGN_TOP, 0);
    m_sizer_ams_option->Add( 0, 0, 1, wxEXPAND, 0);
    m_sizer_ams_option->Add(m_panel_option_right, 0, wxALIGN_TOP, 0);


    m_sizer_ams_body->Add(m_sizer_ams_area_left, wxALIGN_CENTER, 0);
    m_sizer_ams_body->AddSpacer(FromDIP(10));
    m_sizer_ams_body->Add(m_sizer_ams_area_right, wxALIGN_CENTER, 0);

    m_sizer_body->Add(m_sizer_ams_items, 0, wxALIGN_CENTER, 0);
    m_sizer_body->Add(0, 0, 1, wxEXPAND | wxTOP, FromDIP(10));
    m_sizer_body->Add(m_sizer_ams_body, 0, wxALIGN_CENTER, 0);
    m_sizer_body->Add(m_sizer_down_road, 0, wxALIGN_CENTER, 0);
    m_sizer_body->Add(m_sizer_ams_option, 0, wxEXPAND, 0);

    m_amswin->SetSizer(m_sizer_body);
    m_amswin->Layout();
    m_amswin->Fit();
    //Thaw();

    SetSize(m_amswin->GetSize());
    SetMinSize(m_amswin->GetSize());


    AddPage(m_amswin, wxEmptyString, false);


    m_button_extruder_feed->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(AMSControl::on_filament_load), NULL, this);
    m_button_extruder_back->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(AMSControl::on_filament_unload), NULL, this);
    m_button_auto_refill->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(AMSControl::auto_refill), NULL, this);

    m_button_ams_setting->Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent& e) {
        m_button_ams_setting->SetBitmap(m_button_ams_setting_hover.bmp());
        e.Skip();
    });
    m_button_ams_setting->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        m_button_ams_setting->SetBitmap(m_button_ams_setting_press.bmp());
        on_ams_setting_click(e);
        e.Skip();
    });

    m_button_ams_setting->Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& e) {
        m_button_ams_setting->SetBitmap(m_button_ams_setting_normal.bmp());
        e.Skip();
    });

    Bind(EVT_AMS_SHOW_HUMIDITY_TIPS, [this](wxCommandEvent& evt) {
        uiAmsHumidityInfo *info    = (uiAmsHumidityInfo *) evt.GetClientData();
        if (info)
        {
            Slic3r::DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
            MachineObject *obj = nullptr;
            if (dev) {
                obj = dev->get_selected_machine();
            }

            if (info->ams_type == DevAmsType::AMS)
            {
                wxPoint img_pos = ClientToScreen(wxPoint(0, 0));
                wxPoint popup_pos(img_pos.x - m_Humidity_tip_popup.GetSize().GetWidth() + FromDIP(150), img_pos.y - FromDIP(80));
                m_Humidity_tip_popup.Position(popup_pos, wxSize(0, 0));

                int humidity_value = info->humidity_display_idx;
                if (humidity_value > 0 && humidity_value <= 5) { m_Humidity_tip_popup.set_humidity_level(humidity_value); }
                m_Humidity_tip_popup.Popup();
            } else if (obj && obj->is_support_remote_dry && (info->ams_type == DevAmsType::N3F || info->ams_type == DevAmsType::N3S)){
                m_ams_dry_ctr_win->set_ams_id(info->ams_id);

                wxPoint img_pos = ClientToScreen(wxPoint(0, 0));
                wxPoint popup_pos(img_pos.x - m_ams_dry_ctr_win->GetSize().GetWidth() + FromDIP(150), img_pos.y - FromDIP(80));
                m_ams_dry_ctr_win->Move(popup_pos);
                m_ams_dry_ctr_win->ShowModal();
            } else {
                m_percent_humidity_dry_popup->Update(info);

                wxPoint img_pos = ClientToScreen(wxPoint(0, 0));
                wxPoint popup_pos(img_pos.x - m_percent_humidity_dry_popup->GetSize().GetWidth() + FromDIP(150), img_pos.y - FromDIP(80));
                m_percent_humidity_dry_popup->Move(popup_pos);
                m_percent_humidity_dry_popup->ShowModal();
            }
        }

        delete info;
    });
    Bind(EVT_AMS_ON_SELECTED, &AMSControl::AmsSelectedSwitch, this);
}

void AMSControl::on_retry()
{
    post_event(wxCommandEvent(EVT_AMS_RETRY));
}

AMSControl::~AMSControl() 
{
    if (m_ams_dry_ctr_win) {
        delete m_ams_dry_ctr_win;
    }
}

std::string AMSControl::GetCurentAms() {
    return m_current_ams;
}
std::string AMSControl::GetCurentShowAms(AMSPanelPos pos) {
    if (pos == AMSPanelPos::RIGHT_PANEL){
        return m_current_show_ams_right;
    }
    else{
        return m_current_show_ams_left;
    }
}

std::string AMSControl::GetCurrentCan(std::string amsid)
{
    std::string current_can;
    for (auto ams_item : m_ams_item_list) {
        AmsItem* item = ams_item.second;
        if (item == nullptr){
            continue;
        }
        if (item->get_ams_id() == amsid) {
            current_can = item->GetCurrentCan();
            return current_can;
        }
    }
    return current_can;
}

std::tuple<bool, bool> AMSControl::isFilaSwitchReady()
{
    // return false;
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return {false, false};
    MachineObject* obj = dev->get_selected_machine();
    if (!obj) return {false, false};
    std::shared_ptr<Slic3r::DevFilaSwitch> filaSwitch = obj->GetFilaSwitch();
    if (filaSwitch)
    {
        return {filaSwitch->IsInstalled(), filaSwitch->IsReady()};
    }
    return {false, false};
}

void AMSControl::AmsSelectedSwitch(wxCommandEvent& event) {
    std::string ams_id_selected = std::to_string(event.GetInt());
    if (m_current_ams != ams_id_selected){
        m_current_ams = ams_id_selected;
    }
    if (m_current_show_ams_left != ams_id_selected && m_current_show_ams_left != "") {
        auto item = m_ams_item_list[m_current_show_ams_left];
        if (!item) return;
        try{
            const auto& can_lib_list = item->get_can_lib_list();
            for (auto can : can_lib_list) {
                can.second->UnSelected();
            }
        }
        catch (...){
            ;
        }
    }
    else if (m_current_show_ams_right != ams_id_selected && m_current_show_ams_right != "") {
        auto item = m_ams_item_list[m_current_show_ams_right];
        if (!item) return;
        try {
            const auto &can_lib_list = item->get_can_lib_list();
            for (auto can : can_lib_list) {
                can.second->UnSelected();
            }
        }
        catch (...) {
            ;
        }
    }
}

wxColour AMSControl::GetCanColour(std::string amsid, std::string canid)
{
    wxColour col = *wxWHITE;
    for (auto i = 0; i < m_ams_info.size(); i++) {
        if (m_ams_info[i].ams_id == amsid) {
            for (auto o = 0; o < m_ams_info[i].cans.size(); o++) {
                if (m_ams_info[i].cans[o].can_id == canid) {
                    col = m_ams_info[i].cans[o].material_colour;
                }
            }
        }
    }
    return col;
}

void AMSControl::EnableLoadFilamentBtn(bool enable, const std::string& ams_id, const std::string& can_id,const wxString& tips)
{
    m_button_extruder_feed->Enable(enable);
    if (m_button_extruder_feed->GetToolTipText() != tips) {
        BOOST_LOG_TRIVIAL(info) << "ams_id=" << ams_id << ", can_id=" << can_id << "  Set Load Filament Button ToolTip : " << tips.ToUTF8();
        m_button_extruder_feed->SetToolTip(tips);
    }
}

void AMSControl::EnableUnLoadFilamentBtn(bool enable, const std::string& ams_id, const std::string& can_id,const wxString& tips)
{
    m_button_extruder_back->Enable(enable);
    if (m_button_extruder_back->GetToolTipText() != tips) {
        BOOST_LOG_TRIVIAL(info) << "ams_id=" << ams_id << ", can_id=" << can_id << "  Set Unload Filament Button ToolTip : " << tips.ToUTF8();
        m_button_extruder_back->SetToolTip(tips);
    }
}

void AMSControl::EnterNoneAMSMode()
{
    //m_vams_lib->m_ams_model = m_ext_model;
    if(m_is_none_ams_mode == DevAmsType::EXT_SPOOL) return;
    m_panel_prv_left->Hide();

    m_simplebook_ams_left->SetSelection(0);
    m_extruder->no_ams_mode(true);
    //m_button_ams_setting->Hide();
    //m_button_extruder_feed->Show();
    //m_button_extruder_back->Show();

    ShowFilamentTip(false);
    m_amswin->Layout();
    m_amswin->Fit();
    Layout();
    m_is_none_ams_mode = DevAmsType::EXT_SPOOL;
}

void AMSControl::EnterGenericAMSMode()
{
    if(m_is_none_ams_mode == DevAmsType::AMS) return;
    m_extruder->no_ams_mode(false);
    m_amswin->Layout();
    m_amswin->Fit();
    Layout();
    m_is_none_ams_mode = DevAmsType::AMS;
}

void AMSControl::EnterExtraAMSMode()
{
    //m_vams_lib->m_ams_model = m_ext_model;
    if(m_is_none_ams_mode == DevAmsType::AMS_LITE) return;
    m_panel_prv_left->Hide();

    m_simplebook_ams_left->SetSelection(2);
    m_extruder->no_ams_mode(false);
    m_amswin->Layout();
    m_amswin->Fit();
    Layout();
    Refresh(true);
    m_is_none_ams_mode = DevAmsType::AMS_LITE;

}

void AMSControl::PlayRridLoading(wxString amsid, wxString canid)
{
    auto iter = m_ams_item_list.find(amsid.ToStdString());

    if (iter != m_ams_item_list.end()) {
        AmsItem* cans = iter->second;
        cans->PlayRridLoading(canid);
    }
}

void AMSControl::StopRridLoading(wxString amsid, wxString canid)
{
    auto iter = m_ams_item_list.find(amsid.ToStdString());

    if (iter != m_ams_item_list.end()) {
        AmsItem* cans = iter->second;
        cans->StopRridLoading(canid);
    }
}

void AMSControl::msw_rescale()
{
    m_button_ams_setting_normal.msw_rescale();
    m_button_ams_setting_hover.msw_rescale();
    m_button_ams_setting_press.msw_rescale();
    m_button_ams_setting->SetBitmap(m_button_ams_setting_normal.bmp());

    m_extruder->msw_rescale();

    if (m_button_extruder_feed) m_button_extruder_feed->SetMinSize(wxSize(FromDIP(80), FromDIP(34)));
    if (m_button_extruder_feed) m_button_extruder_feed->SetMaxSize(wxSize(FromDIP(80), FromDIP(34)));
    if (m_button_extruder_back) m_button_extruder_back->SetMinSize(wxSize(FromDIP(80), FromDIP(34)));
    if (m_button_extruder_back) m_button_extruder_back->SetMaxSize(wxSize(FromDIP(80), FromDIP(34)));
    if (m_button_auto_refill) m_button_auto_refill->SetMinSize(wxSize(FromDIP(80), FromDIP(34)));
    if (m_button_auto_refill) m_button_auto_refill->SetMaxSize(wxSize(FromDIP(80), FromDIP(34)));
    if (m_button_ams_setting) m_button_ams_setting->SetMinSize(wxSize(FromDIP(25), FromDIP(24)));


    for (auto ams_item : m_ams_item_list) {
        if (ams_item.second){
            ams_item.second->msw_rescale();
        }
    }
    for (auto ams_prv : m_ams_preview_list) {
        if (ams_prv.second){
            ams_prv.second->msw_rescale();
        }
    }
    for (auto ext_img : m_ext_image_list) {
        if (ext_img.second) {
            ext_img.second->msw_rescale();
        }
    }
    if (m_down_road){
        m_down_road->msw_rescale();
    }

    if (m_percent_humidity_dry_popup){
        m_percent_humidity_dry_popup->msw_rescale();
    }

    if (m_ams_dry_ctr_win) {
        m_ams_dry_ctr_win->msw_rescale();
    }

    m_Humidity_tip_popup.msw_rescale();

    Layout();
    Refresh();
}


void AMSControl::ClearAms() {
    m_simplebook_ams_right->DeleteAllPages();
    m_simplebook_ams_left->DeleteAllPages();
    m_simplebook_ams_right->DestroyChildren();
    m_simplebook_ams_left->DestroyChildren();
    m_simplebook_ams_right->Layout();
    m_simplebook_ams_left->Layout();
    m_simplebook_ams_right->Refresh();
    m_simplebook_ams_left->Refresh();

    for (auto it : m_ams_preview_list) {
        delete it.second;
    }
    m_ams_preview_list.clear();
    m_ext_image_list.clear();

    m_left_page_index = 0;
    m_right_page_index = 0;

    m_ams_item_list.clear();
    m_sizer_prv_right->Clear();
    m_sizer_prv_left->Clear();
    m_item_ids = { {}, {} };
    pair_id.clear();
}

void AMSControl::CreateAmsDoubleNozzle(const std::string &series_name, const std::string &printer_type)
{
    static int s_extruder_count_double = 2;
    std::vector<AMSinfo> single_info_left;
    std::vector<AMSinfo> single_info_right;

    //Freeze();
    for (auto ams_info = m_ams_info.begin(); ams_info != m_ams_info.end(); ams_info++){
        if (ams_info->binded_extruder_set.empty()) {
            continue;
        }

        AMSPanelPos panel_pos = ams_info->GetDefaultPanelPos(s_extruder_count_double);
        if (ams_info->cans.size() == GENERIC_AMS_SLOT_NUM){
            if (panel_pos == AMSPanelPos::RIGHT_PANEL) {
                m_item_ids[MAIN_EXTRUDER_ID].push_back(ams_info->ams_id);
            } else if (panel_pos == AMSPanelPos::LEFT_PANEL) {
                m_item_ids[DEPUTY_EXTRUDER_ID].push_back(ams_info->ams_id);
            } else {
                continue;
            }
            
            AddAmsPreview(*ams_info, panel_pos);
            AddAms(*ams_info, panel_pos);
        }
        else if (ams_info->cans.size() == 1){
            if (panel_pos == AMSPanelPos::RIGHT_PANEL){
                single_info_right.push_back(*ams_info);
                if (single_info_right.size() == 2){
                    m_item_ids[MAIN_EXTRUDER_ID].push_back(single_info_right[0].ams_id);
                    m_item_ids[MAIN_EXTRUDER_ID].push_back(single_info_right[1].ams_id);
                    AddAms(single_info_right, series_name, printer_type, panel_pos);
                    AddAmsPreview(single_info_right, panel_pos);
                    pair_id.push_back(std::make_pair(single_info_right[0].ams_id, single_info_right[1].ams_id));
                    single_info_right.clear();
                }
            }
            else if (panel_pos == AMSPanelPos::LEFT_PANEL){
                single_info_left.push_back(*ams_info);
                if (single_info_left.size() == 2){
                    m_item_ids[DEPUTY_EXTRUDER_ID].push_back(single_info_left[0].ams_id);
                    m_item_ids[DEPUTY_EXTRUDER_ID].push_back(single_info_left[0].ams_id);
                    AddAms(single_info_left, series_name, printer_type, panel_pos);
                    AddAmsPreview(single_info_left, panel_pos);
                    pair_id.push_back(std::make_pair(single_info_left[0].ams_id, single_info_left[1].ams_id));
                    single_info_left.clear();
                }
            }
        }
    }

    for (const auto& info : m_ext_info) {
        auto panel_pos = info.GetDefaultPanelPos(s_extruder_count_double);
        if (panel_pos == AMSPanelPos::RIGHT_PANEL) {
            single_info_right.push_back(info);
            if (single_info_right.size() == 2) {
                AddAms(single_info_right, series_name, printer_type, panel_pos);
                AddAmsPreview(single_info_right, panel_pos);
                pair_id.push_back(std::make_pair(single_info_right[0].ams_id, single_info_right[1].ams_id));
                single_info_right.clear();
            }
        } else if (panel_pos == AMSPanelPos::LEFT_PANEL) {
            single_info_left.push_back(info);
            if (single_info_left.size() == 2) {
                AddAms(single_info_left, series_name, printer_type, panel_pos);
                AddAmsPreview(single_info_left, panel_pos);
                pair_id.push_back(std::make_pair(single_info_left[0].ams_id, single_info_left[1].ams_id));
                single_info_left.clear();
            }
        };
    }

    if (single_info_right.size() > 0) {
        AddAms(single_info_right, series_name, printer_type, AMSPanelPos::RIGHT_PANEL);
        AddAmsPreview(single_info_right, AMSPanelPos::RIGHT_PANEL);
        single_info_right.clear();
    }

    if (single_info_left.size() > 0) {
        AddAms(single_info_left, series_name, printer_type, AMSPanelPos::LEFT_PANEL);
        AddAmsPreview(single_info_left, AMSPanelPos::LEFT_PANEL);
        single_info_left.clear();
    }

    m_sizer_prv_left->Layout();
    m_sizer_prv_right->Layout();
    m_simplebook_ams_left->Show();
    m_simplebook_ams_right->Show();
    if (m_ams_info.size() > 0){
        m_panel_prv_left->Show();
        m_panel_prv_right->Show();
    }
    else{
        m_panel_prv_left->Hide();
        m_panel_prv_right->Hide();
    }
    m_simplebook_ams_left->SetSelection(0);
    m_simplebook_ams_right->SetSelection(0);

    m_down_road->UpdateLeft(2, findFirstMode(AMSPanelPos::LEFT_PANEL));
    m_down_road->UpdateRight(2, findFirstMode(AMSPanelPos::RIGHT_PANEL));

    m_extruder->updateNozzleNum(2);

    m_current_show_ams_left = m_item_ids[DEPUTY_EXTRUDER_ID].size() > 0 ? m_item_ids[DEPUTY_EXTRUDER_ID][0] : "";
    m_current_show_ams_right = m_item_ids[MAIN_EXTRUDER_ID].size() > 0 ? m_item_ids[MAIN_EXTRUDER_ID][0] : "";
    UpdateAmsPreviewSelection();

    m_current_ams = "";
    m_down_road->UpdatePassRoad(AMSPanelPos::LEFT_PANEL, -1, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    m_extruder->OnAmsLoading(false, DEPUTY_EXTRUDER_ID);
    m_down_road->UpdatePassRoad(AMSPanelPos::RIGHT_PANEL, -1, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    m_extruder->OnAmsLoading(false, MAIN_EXTRUDER_ID);

    m_amswin->Layout();
    m_amswin->Fit();

    //Thaw();
}

void AMSControl::CreateAmsSingleNozzle(const std::string &series_name, const std::string &printer_type)
{
    static int s_extruder_count_single = 1;

    std::vector<int>m_item_nums{0,0};
    std::vector<AMSinfo> single_info;

    //Freeze();

    //add ams data
    for (auto ams_info = m_ams_info.begin(); ams_info != m_ams_info.end(); ams_info++) {
        auto panel_pos = ams_info->GetDefaultPanelPos(s_extruder_count_single);
        if (ams_info->cans.size() == GENERIC_AMS_SLOT_NUM) {
            m_item_ids[DEPUTY_EXTRUDER_ID].push_back(ams_info->ams_id);
            AddAmsPreview(*ams_info, panel_pos);
            AddAms(*ams_info, panel_pos);
        }
        else if (ams_info->cans.size() == 1) {
            m_item_ids[DEPUTY_EXTRUDER_ID].push_back(ams_info->ams_id);
            AddAmsPreview(*ams_info, panel_pos);
            AddAms(*ams_info, panel_pos);
        }
    }
    if (single_info.size() > 0){
        m_item_ids[DEPUTY_EXTRUDER_ID].push_back(single_info[0].ams_id);
        m_item_nums[DEPUTY_EXTRUDER_ID]++;
        AddAms(single_info, series_name, printer_type, AMSPanelPos::LEFT_PANEL);
        AddAmsPreview(single_info, AMSPanelPos::LEFT_PANEL);
        single_info.clear();
    }

    // data ext data
    if (m_ext_info.size() <= 0){
        BOOST_LOG_TRIVIAL(trace) << "vt_slot empty!";
        return;
    }

    single_info.push_back(m_ext_info[0]);
    m_item_ids[MAIN_EXTRUDER_ID].push_back(single_info[0].ams_id);
    AddAms(single_info, series_name, printer_type, AMSPanelPos::RIGHT_PANEL);
    auto left_init_mode = findFirstMode(AMSPanelPos::LEFT_PANEL);
    auto right_init_mode = findFirstMode(AMSPanelPos::RIGHT_PANEL);

    m_panel_prv_right->Hide();
    m_panel_prv_left->Hide();
    if (m_ams_info.size() > 0){
        m_simplebook_ams_left->Show();
        m_simplebook_ams_right->Show();
        m_simplebook_ams_left->SetSelection(0);
        m_simplebook_ams_right->SetSelection(0);

        if (m_ams_info.size() > 1){
            m_sizer_prv_right->Layout();
            m_panel_prv_right->Show();
        }
        m_down_road->UpdateLeft(1, left_init_mode);
        m_down_road->UpdateRight(1, right_init_mode);
    }
    else {
        m_panel_prv_left->Hide();
        m_panel_prv_right->Hide();
        m_simplebook_ams_left->Hide();
        m_simplebook_ams_right->Show();

        m_simplebook_ams_right->SetSelection(0);
        m_down_road->UpdateLeft(1, left_init_mode);
        m_down_road->UpdateRight(1, right_init_mode);
    }
    m_current_show_ams_left = m_item_ids[DEPUTY_EXTRUDER_ID].size() > 0 ? m_item_ids[DEPUTY_EXTRUDER_ID][0] : "";
    m_current_show_ams_right = m_item_ids[MAIN_EXTRUDER_ID].size() > 0 ? m_item_ids[MAIN_EXTRUDER_ID][0] : "";
    m_current_ams = "";

    m_down_road->UpdatePassRoad(AMSPanelPos::LEFT_PANEL, -1, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    m_down_road->UpdatePassRoad(AMSPanelPos::RIGHT_PANEL, -1, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    m_extruder->updateNozzleNum(1);
    m_extruder->OnAmsLoading(false, MAIN_EXTRUDER_ID);

    m_amswin->Layout();
    m_amswin->Fit();

    //Refresh();
    //Thaw();
}

void AMSControl::Reset()
{
    m_ams_info.clear();
    m_ext_info.clear();
    m_dev_id.clear();
    ClearAms();

    Layout();
}

void AMSControl::show_noams_mode()
{
    EnterGenericAMSMode();
}

void AMSControl::show_switcher_status(bool show)
{
    if (tipPanel == nullptr)
    {
        m_sizer_body->Add(0, 0, 1, wxEXPAND | wxTOP, FromDIP(5));
        tipPanel = new wxPanel(m_amswin);
        tipPanel->SetBackgroundColour(wxColour(255, 153, 0));
        tipSizer = new wxBoxSizer(wxHORIZONTAL);
        tipPanel->SetSizer(tipSizer);
        icon = new wxStaticBitmap(tipPanel, wxID_ANY,
            wxArtProvider::GetBitmap(wxART_INFORMATION, wxART_MESSAGE_BOX, wxSize(FromDIP(16), FromDIP(16))));
        tipSizer->Add(icon, 0, wxALL, FromDIP(8));
        tipText = new wxStaticText(tipPanel, wxID_ANY,
            _L("The consumables changer has not been calibrated. \nPlease calibrate it on the printer before using."));
        tipText->SetForegroundColour(wxColour(255, 255, 255));
        tipText->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
        tipText->Wrap(-1);
        tipText->SetMinSize(wxSize(-1, -1));
        tipSizer->Add(tipText, 0, wxALL | wxALIGN_CENTER_VERTICAL | wxEXPAND, FromDIP(8));
        m_sizer_body->Add(tipPanel, 1, wxEXPAND, 0);
    }
    if (tipPanel->IsShown() == show)
    {
        return;
    }
    tipPanel->Show(show);
    m_amswin->Layout();
    m_amswin->Fit();
}

void AMSControl::show_auto_refill(bool show)
{
    if (m_button_auto_refill->IsShown() == show)
    {
        return;
    }

    m_button_auto_refill->Show(show);
    m_amswin->Layout();
    m_amswin->Fit();
}

void AMSControl::enable_ams_setting(bool en)
{
    m_button_ams_setting->Enable(en);
}

void AMSControl::show_vams_kn_value(bool show)
{
    //m_vams_lib->show_kn_value(show);
}

void AMSControl::UpdateAmsDryControl(MachineObject* obj)
{
    if (!m_ams_dry_ctr_win->IsShown()) {
        return;
    }

    if (!obj || !obj->GetFilaSystem()) {
        m_ams_dry_ctr_win->Close();
        return;
    }

    std::weak_ptr<DevFilaSystem> weak_fila_system = obj->GetFilaSystem();
    
    if (auto locaked_fila_system = weak_fila_system.lock()) {
        m_ams_dry_ctr_win->update(locaked_fila_system, obj);
    } else {
        m_ams_dry_ctr_win->Close();
        return;
    }
}

void AMSControl::UpdateAms(const std::string   &series_name,
                           const std::string   &printer_type,
                           std::vector<AMSinfo> ams_info,
                           std::vector<AMSinfo> ext_info,
                           DevExtderSystem           data,
                           std::string          dev_id,
                           bool                 is_reset,
                           bool                 test)
{
    if (!test){
        // update item
        bool fresh = false;

        // basic check
        if (m_ams_info.size() == ams_info.size() && m_total_ext_count == data.GetTotalExtderCount() && m_dev_id == dev_id && m_ext_info.size() == ext_info.size())
        {
            for (int i = 0; i < m_ams_info.size(); i++){
                if (m_ams_info[i].ams_id != ams_info[i].ams_id){
                    fresh = true;
                }

                if (m_ams_info[i].GetDefaultPanelPos(m_total_ext_count) != ams_info[i].GetDefaultPanelPos(m_total_ext_count)) {
                    fresh = true;
                }
            }
        }
        else{
            fresh = true;
        }

        m_ams_info.clear();
        m_ams_info = ams_info;
        m_total_ext_count = data.GetTotalExtderCount();
        m_ext_info.clear();
        m_ext_info = ext_info;
        m_dev_id = dev_id;
        if (fresh){
            ClearAms();
            if (m_total_ext_count >= 2){
                CreateAmsDoubleNozzle(series_name, printer_type);
            }else{
                CreateAmsSingleNozzle(series_name, printer_type);
            }
            SetSize(wxSize(FromDIP(578), -1));
            SetMinSize(wxSize(FromDIP(578), -1));
            Layout();
        }
        // update cans

        for (auto ams_item : m_ams_item_list) {
            if (ams_item.second == nullptr){
                continue;
            }
            std::string ams_id = ams_item.second->get_ams_id();
            AmsItem* cans = ams_item.second;
            if (cans->get_ams_id() == std::to_string(VIRTUAL_TRAY_MAIN_ID) || cans->get_ams_id() == std::to_string(VIRTUAL_TRAY_DEPUTY_ID)) {
                for (auto ifo : m_ext_info) {
                    if (ifo.ams_id == ams_id) {
                        cans->Update(ifo);
                        cans->show_sn_value(m_ams_model == DevAmsType::AMS_LITE ? false : true);
                    }
                }
            }
            else{
                for (auto ifo : m_ams_info) {
                    if (ifo.ams_id == ams_id) {
                        cans->Update(ifo);
                        cans->show_sn_value(m_ams_model == DevAmsType::AMS_LITE ? false : true);
                    }
                }
            }
        }

        for (auto ams_prv : m_ams_preview_list) {
            std::string id = ams_prv.second->get_ams_id();
            auto item = m_ams_item_list.find(id);
            if (item != m_ams_item_list.end())
            { ams_prv.second->Update(item->second->get_ams_info());
            }
        }
    }

    /*update humidity popup*/
    if (m_percent_humidity_dry_popup->IsShown())
    {
        string target_id = m_percent_humidity_dry_popup->get_owner_ams_id();
        for (const auto& the_info : ams_info)
        {
            if (target_id == the_info.ams_id)
            {
                uiAmsHumidityInfo humidity_info;
                humidity_info.ams_id = the_info.ams_id;
                humidity_info.humidity_display_idx = the_info.get_humidity_display_idx();
                humidity_info.humidity_percent = the_info.ams_humidity_percent;
                humidity_info.left_dry_time = the_info.left_dray_time;
                humidity_info.current_temperature = the_info.current_temperature;
                m_percent_humidity_dry_popup->Update(&humidity_info);
                break;
            }
        }
    }

    /*update ams extruder*/
    if (m_extruder->updateNozzleNum(m_total_ext_count, series_name))
    {
        m_amswin->Layout();
    }

    /*update switch status*/
    const auto[install, ready] = isFilaSwitchReady();
    show_switcher_status(install && (!ready));
}

void AMSControl::AddAmsPreview(AMSinfo info, AMSPanelPos pos)
{
    AMSPreview *ams_prv = nullptr;

    if (pos == AMSPanelPos::RIGHT_PANEL)
    {
        ams_prv = new AMSPreview(m_panel_prv_right, wxID_ANY, info);
        m_sizer_prv_right->Add(ams_prv, 0, wxALIGN_CENTER | wxLEFT, FromDIP(6));
    }
    else if (pos == AMSPanelPos::LEFT_PANEL)
    {
        ams_prv = new AMSPreview(m_panel_prv_left, wxID_ANY, info);
        m_sizer_prv_left->Add(ams_prv, 0, wxALIGN_CENTER | wxLEFT, FromDIP(6));
    }

    if (ams_prv){
        ams_prv->Bind(wxEVT_LEFT_DOWN, [this, ams_prv](wxMouseEvent &e) {
            SwitchAms(ams_prv->get_ams_id());
            e.Skip();
        });
        m_ams_preview_list[info.ams_id] = ams_prv;
    }
}

void AMSControl::createAms(wxSimplebook* parent, int& idx, AMSinfo info, AMSPanelPos pos) {
    auto ams_item = new AmsItem(parent, info, info.ams_type, pos);
    parent->InsertPage(idx, ams_item, wxEmptyString, true);
    ams_item->set_parent_book_idx(parent, idx);
    idx++;

    m_ams_item_list[info.ams_id] = ams_item;
}

AMSRoadShowMode AMSControl::findFirstMode(AMSPanelPos pos) {
    auto init_mode = AMSRoadShowMode::AMS_ROAD_MODE_NONE;

    std::string ams_id = "";
    for (const auto& [idx, ams_item] : m_ams_item_list) {
        if(ams_item->get_panel_pos() == pos){
            ams_id = idx;
            break;
        }
    }

    auto item = m_ams_item_list.find(ams_id);
    if (ams_id.empty() || item == m_ams_item_list.end()) return init_mode;

    if (item->second->get_can_count() == GENERIC_AMS_SLOT_NUM) {
        if (item->second->get_ams_model() == DevAmsType::AMS_LITE) return AMSRoadShowMode::AMS_ROAD_MODE_AMS_LITE;
        if (item->second->get_ams_model() == DevAmsType::EXT_SPOOL && item->second->get_ext_type() == AMSModelOriginType::LITE_EXT) return AMSRoadShowMode::AMS_ROAD_MODE_AMS_LITE;
        return AMSRoadShowMode::AMS_ROAD_MODE_FOUR;
    }
    else{
        if (IsInSlotPair(ams_id)) return AMSRoadShowMode::AMS_ROAD_MODE_DOUBLE;
        if (item->second->get_ams_model() == DevAmsType::EXT_SPOOL && item->second->get_ext_type() == AMSModelOriginType::LITE_EXT) return AMSRoadShowMode::AMS_ROAD_MODE_AMS_LITE;
        if (item->second->get_ams_model() == DevAmsType::N3S) return AMSRoadShowMode::AMS_ROAD_MODE_SINGLE_N3S;
        return AMSRoadShowMode::AMS_ROAD_MODE_SINGLE;
    }
}

void AMSControl::createAmsPanel(wxSimplebook *parent, int &idx, std::vector<AMSinfo> infos, const std::string &series_name, const std::string &printer_type, AMSPanelPos pos, int total_ext_num)
{
    if (infos.size() <= 0) return;

    wxPanel* book_panel = new wxPanel(parent);
    wxBoxSizer* book_sizer = new wxBoxSizer(wxHORIZONTAL);
    book_panel->SetBackgroundColour(StateColor::darkModeColorFor(AMS_CONTROL_DEF_LIB_BK_COLOUR));
    book_panel->SetSize(AMS_PANEL_SIZE);
    book_panel->SetMinSize(AMS_PANEL_SIZE);
    book_panel->SetSizer(book_sizer);

    AmsItem* ams1 = nullptr, * ams2 = nullptr;
    ams1 = new AmsItem(book_panel, infos[0], infos[0].ams_type, pos);
    ams1->SetBackgroundColour(StateColor::darkModeColorFor(AMS_CONTROL_DEF_LIB_BK_COLOUR));
    ams1->set_parent_book_idx(parent, idx);
    m_ams_item_list[infos[0].ams_id] = ams1;

    if (infos.size() == MAX_AMS_NUM_IN_PANEL) {    //n3s and ? in a panel
        ams2 = new AmsItem(book_panel, infos[1], infos[1].ams_type, pos);
        ams2->SetBackgroundColour(StateColor::darkModeColorFor(AMS_CONTROL_DEF_LIB_BK_COLOUR));
        ams2->set_parent_book_idx(parent, idx);
        m_ams_item_list[infos[1].ams_id] = ams2;

        if (pos == AMSPanelPos::LEFT_PANEL) {
            book_sizer->Add(ams1, 0, wxLEFT, FromDIP(4));
            book_sizer->Add(ams2, 0, wxLEFT, FromDIP(30));
        }
        else {
            book_sizer->Add(ams1, 0, wxLEFT, FromDIP(72));
            book_sizer->Add(ams2, 0, wxLEFT, FromDIP(30));
        }
    } else { // only an ext in a panel
        if (ams1->get_ams_model() == DevAmsType::EXT_SPOOL) {
            book_sizer->Add(ams1, 0, wxLEFT, (book_panel->GetSize().x - ams1->GetSize().x) / 2);
        }
    }

    book_panel->Layout();
    book_panel->Fit();

    parent->InsertPage(idx, book_panel, wxEmptyString, true);
    idx++;
}

void AMSControl::AddAms(AMSinfo info, AMSPanelPos pos)
{
    if (m_total_ext_count > 1){
        if (pos == AMSPanelPos::RIGHT_PANEL){
            createAms(m_simplebook_ams_right, m_right_page_index, info, pos);
        }
        else if (pos == AMSPanelPos::LEFT_PANEL){
            createAms(m_simplebook_ams_left, m_left_page_index, info, pos);
        }
    }
    else if (m_total_ext_count == 1){
        createAms(m_simplebook_ams_left, m_left_page_index, info, AMSPanelPos::LEFT_PANEL);
    }
    m_simplebook_ams_left->Layout();
    m_simplebook_ams_right->Layout();
    m_simplebook_ams_left->Refresh();
    m_simplebook_ams_right->Refresh();

}

//void AMSControl::AddExtraAms(AMSinfo info)
//{
//    auto ams_item = new AmsItem(m_simplebook_extra_cans_left, info, DevAmsType::EXTRA_AMS);
//    m_ams_item_list[info.ams_id] = ams_item;
//
//    if (info.nozzle_id == 1)
//    {
//        m_simplebook_extra_cans_left->AddPage(ams_item, wxEmptyString, false);
//        ams_item->m_selection = m_simplebook_extra_cans_left->GetPageCount() - 1;
//    }
//    else if (info.nozzle_id == 0)
//    {
//        m_simplebook_extra_cans_right->AddPage(ams_item, wxEmptyString, false);
//        ams_item->m_selection = m_simplebook_extra_cans_right->GetPageCount() - 1;
//    }
//
//}

void AMSControl::AddAms(std::vector<AMSinfo> single_info,
                        const std::string &series_name,
                        const std::string &printer_type,
                        AMSPanelPos pos)
{
     if (single_info.size() <= 0){
        return;
    }
    if (m_total_ext_count == 2) {
        if (pos == AMSPanelPos::RIGHT_PANEL) {
            createAmsPanel(m_simplebook_ams_right, m_right_page_index, single_info, series_name, printer_type, pos, m_total_ext_count);
        }
        else if (pos == AMSPanelPos::LEFT_PANEL) {
            createAmsPanel(m_simplebook_ams_left, m_left_page_index, single_info, series_name, printer_type, pos, m_total_ext_count);
        }
    }
    else if (m_total_ext_count == 1) {
        if (pos == AMSPanelPos::RIGHT_PANEL) {
            createAmsPanel(m_simplebook_ams_right, m_right_page_index, single_info, series_name, printer_type, AMSPanelPos::RIGHT_PANEL, m_total_ext_count);
        }
        else {
            createAmsPanel(m_simplebook_ams_left, m_left_page_index, single_info, series_name, printer_type, AMSPanelPos::LEFT_PANEL, m_total_ext_count);
        }
    }

    m_simplebook_ams_left->Layout();
    m_simplebook_ams_right->Layout();
    m_simplebook_ams_left->Refresh();
    m_simplebook_ams_right->Refresh();

    for (const auto& info : single_info) {
        m_item_ids[(int)pos].push_back(info.ams_id);
        m_item_ids[(int)pos].push_back(info.ams_id);
    }
}

//void AMSControl::AddExtAms(int ams_id) {
//    if (m_ams_item_list.find(std::to_string(ams_id)) != m_ams_item_list.end())
//    {
//        //mode = DevAmsType::EXTRA_AMS;
//        AmsItem* ams_item;
//        AMSinfo ext_info;
//
//        if (ams_id == VIRTUAL_TRAY_MAIN_ID)
//        {
//            ext_info.ams_id = std::to_string(VIRTUAL_TRAY_MAIN_ID);
//            ext_info.nozzle_id = 0;
//            ams_item = new AmsItem(m_simplebook_ams_right, ext_info, DevAmsType::EXTRA_AMS);
//            m_simplebook_ams_right->AddPage(ams_item, wxEmptyString, false);
//            ams_item->m_selection = m_simplebook_ams_right->GetPageCount() - 1;
//        }
//        else if (ams_id == VIRTUAL_TRAY_DEPUTY_ID)
//        {
//            ext_info.ams_id = std::to_string(VIRTUAL_TRAY_DEPUTY_ID);
//            ext_info.nozzle_id = 1;
//            ams_item = new AmsItem(m_simplebook_ams_left, ext_info, DevAmsType::EXTRA_AMS);
//            m_simplebook_ams_left->AddPage(ams_item, wxEmptyString, false);
//            ams_item->m_selection = m_simplebook_ams_left->GetPageCount() - 1;
//        }
//        m_ams_generic_item_list[std::to_string(ams_id)] = ams_item;
//    }
//}

void AMSControl::AddAmsPreview(std::vector<AMSinfo>single_info, AMSPanelPos pos) {
    if (single_info.size() <= 0) return;

    AMSPreview* ams_prv = nullptr;
    AMSPreview* ams_prv2 = nullptr;
    if (pos == AMSPanelPos::RIGHT_PANEL){
        ams_prv = new AMSPreview(m_panel_prv_right, wxID_ANY, single_info[0]);
        m_sizer_prv_right->Add(ams_prv, 0, wxALIGN_CENTER | wxLEFT, FromDIP(6));
        if (single_info.size() == 2)
        {
            ams_prv2 = new AMSPreview(m_panel_prv_right, wxID_ANY, single_info[1]);
            m_sizer_prv_right->Add(ams_prv2, 0, wxALIGN_CENTER | wxLEFT, 0);
        }
    }
    else
    {
        ams_prv = new AMSPreview(m_panel_prv_left, wxID_ANY, single_info[0]);
        m_sizer_prv_left->Add(ams_prv, 0, wxALIGN_CENTER | wxLEFT, FromDIP(6));
        if (single_info.size() == 2)
        {
            ams_prv2 = new AMSPreview(m_panel_prv_left, wxID_ANY, single_info[1]);
            m_sizer_prv_left->Add(ams_prv2, 0, wxALIGN_CENTER | wxLEFT, 0);
        }
    }

    if (ams_prv) {
        ams_prv->Bind(wxEVT_LEFT_DOWN, [this, ams_prv](wxMouseEvent& e) {
            SwitchAms(ams_prv->get_ams_id());
            e.Skip();
            });
        m_ams_preview_list[single_info[0].ams_id] = ams_prv;
    }
    if (ams_prv2) {
        ams_prv2->Bind(wxEVT_LEFT_DOWN, [this, ams_prv2](wxMouseEvent& e) {
            SwitchAms(ams_prv2->get_ams_id());
            e.Skip();
            });
        m_ams_preview_list[single_info[1].ams_id] = ams_prv2;
    }
}

void AMSControl::SwitchAms(std::string ams_id)
{
    if (ams_id == m_current_show_ams_left || ams_id == m_current_show_ams_right) {
        return;
    }

    const auto& iter = m_ams_item_list.find(ams_id);
    if (iter == m_ams_item_list.end()) {
        return;
    }

    // Change the buffered ams id for current panel
    const auto& panel_pos = iter->second->get_panel_pos();
    if (panel_pos == AMSPanelPos::RIGHT_PANEL) {
        m_current_show_ams_right = ams_id;
    } else if (panel_pos == AMSPanelPos::LEFT_PANEL) {
        m_current_show_ams_left = ams_id;
    }

    // Switch ams preview selection display
    UpdateAmsPreviewSelection();

    // clear pass road when switch ams
    if (panel_pos == AMSPanelPos::RIGHT_PANEL) {
        m_down_road->UpdatePassRoad(AMSPanelPos::RIGHT_PANEL, -1, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    } else if (panel_pos == AMSPanelPos::LEFT_PANEL) {
        m_down_road->UpdatePassRoad(AMSPanelPos::LEFT_PANEL, -1, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    }

    if (const auto& iter = m_ams_item_list.find(ams_id); iter != m_ams_item_list.end()) {
        AmsItem* ams_item = iter->second;
        if (ams_item->get_parent_book() && ams_item->get_parent_book_index().has_value()) {
            ams_item->get_parent_book()->SetSelection(ams_item->get_parent_book_index().value());
        }

        const auto& panel_pos = ams_item->get_panel_pos();
        if (ams_item->get_can_count() == GENERIC_AMS_SLOT_NUM) {
            if (ams_item->get_ams_model() == DevAmsType::AMS_LITE) {
                if (panel_pos == AMSPanelPos::LEFT_PANEL) {
                    m_down_road->UpdateLeft(m_total_ext_count, AMSRoadShowMode::AMS_ROAD_MODE_AMS_LITE);
                } else {
                    m_down_road->UpdateRight(m_total_ext_count, AMSRoadShowMode::AMS_ROAD_MODE_AMS_LITE);
                }
            } else {
                if (panel_pos == AMSPanelPos::LEFT_PANEL) {
                    m_down_road->UpdateLeft(m_total_ext_count, AMSRoadShowMode::AMS_ROAD_MODE_FOUR);
                } else {
                    m_down_road->UpdateRight(m_total_ext_count, AMSRoadShowMode::AMS_ROAD_MODE_FOUR);
                }
            }
        } else {
            AMSRoadShowMode mode = AMSRoadShowMode::AMS_ROAD_MODE_SINGLE;
            if (IsInSlotPair(ams_id)) {
                mode = AMSRoadShowMode::AMS_ROAD_MODE_DOUBLE;
            } else if(ams_item->get_ams_model() == DevAmsType::N3S){
                mode = AMSRoadShowMode::AMS_ROAD_MODE_SINGLE_N3S;
            } 

            if (panel_pos == AMSPanelPos::LEFT_PANEL) {
                m_down_road->UpdateLeft(m_total_ext_count, mode);
            } else {
                m_down_road->UpdateRight(m_total_ext_count, mode);
            }
        }
    }

    post_event(SimpleEvent(EVT_AMS_SWITCH));
}

void AMSControl::ShowFilamentTip(bool hasams)
{
    //m_simplebook_right->SetSelection(0);
    if (hasams) {
        m_tip_right_top->Show();
        m_tip_load_info->SetLabelText(_L("Choose an AMS slot then press \"Load\" or \"Unload\" button to automatically load or unload filaments."));
    } else {
        // m_tip_load_info->SetLabelText(_L("Before loading, please make sure the filament is pushed into toolhead."));
        m_tip_right_top->Hide();
        m_tip_load_info->SetLabelText(wxEmptyString);
    }

    m_tip_load_info->SetMinSize(AMS_STEP_SIZE);
    m_tip_load_info->Wrap(AMS_STEP_SIZE.x - FromDIP(5));
    m_sizer_right_tip->Layout();
}

bool AMSControl::Enable(bool enable)
{
    for (auto prv_it : m_ams_preview_list) {
        AMSPreview* prv = prv_it.second;
        prv->Enable(enable);
    }

    for (auto item_it : m_ams_item_list) {
        AmsItem* item = item_it.second;
        item->Enable(enable);
    }

    m_button_extruder_feed->Enable(enable);
    m_button_extruder_back->Enable(enable);
    m_button_auto_refill->Enable(enable);
    m_button_ams_setting->Enable(enable);

    m_filament_load_step->Enable(enable);
    return wxWindow::Enable(enable);
}

void AMSControl::SetExtruder(bool on_off, int nozzle_id, std::string ams_id, std::string slot_id)
{
    AmsItem *item = nullptr;
    if (m_ams_item_list.find(ams_id) != m_ams_item_list.end()) { item = m_ams_item_list[ams_id]; }

    if (on_off && item) {
        auto col = item->GetTagColr(slot_id);
        m_extruder->OnAmsLoading(true, nozzle_id, col);
    }
    else {
        m_extruder->OnAmsLoading(false, nozzle_id);
    }
}

void AMSControl::SetAmsStep(std::string ams_id, std::string canid, int extruder_id, AMSPassRoadType type, AMSPassRoadSTEP step)
{
    AmsItem* ams = nullptr;
    if (auto amsit = m_ams_item_list.find(ams_id); amsit != m_ams_item_list.end()) {
        ams = amsit->second;
    }
    if (ams == nullptr) return;
    if (canid.empty()) return;

    int can_index = 0;
    try {
        can_index = atoi(canid.c_str());
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "[dev][error] e=:" << e.what();
        return;
    }

    bool in_same_page = ams->IsShown();
    const auto& pos = ams->get_panel_pos();
    const auto& left = (pos == AMSPanelPos::LEFT_PANEL);
    const auto& model = ams->get_ams_model();
    auto length = -1;
    auto in_pair = IsInSlotPair(ams_id);

    if (ams->get_can_count() == GENERIC_AMS_SLOT_NUM) {
        length = left ? 129 : 145;
    } else if (ams->get_can_count() == 1) {
        for (auto it : pair_id){
            if (it.first == ams_id){
                length = left ? 218 : 124;
                in_pair = true;
                break;
            }
            else if (it.second == ams_id){
                length = left ? 110 : 232;
                in_pair = true;
                break;
            }
        }

        if (!in_pair && model == DevAmsType::N3S) {
            length = left ? 129 : 232;
        }
    }

    if (model == DevAmsType::AMS_LITE){
        length = left ? 145 : 45;
    }
    if (model == DevAmsType::EXT_SPOOL && ams->get_ext_type() == AMSModelOriginType::LITE_EXT) {

       if (m_ams_info.size() == 0 && m_ext_info.size() == 1) {
           length = 13;
       } else {
           length = 145;
       }
    }

    if (model == DevAmsType::EXT_SPOOL && ams->get_ext_type() == AMSModelOriginType::GENERIC_EXT) {
        if (m_ams_info.size() == 0 && m_ext_info.size() == 1) {
            length = 50;
        } else {
            /*check in pair*/
            if (in_pair) {
                length = left ? 110 : 232;
            } else {
                length = left ? 192 : 82;
            }
        }
    }

    for (auto i = 0; i < m_ams_info.size(); i++) {
        if (m_ams_info[i].ams_id == ams_id) {
            m_ams_info[i].current_step = step;
            m_ams_info[i].current_can_id = canid;
        }
    }
    for (auto i = 0; i < m_ext_info.size(); i++) {
        if (m_ext_info[i].ams_id == ams_id) {
            m_ext_info[i].current_step = step;
            m_ext_info[i].current_can_id = canid;
        }
    }

    const auto& info = ams->get_ams_info();
    if (can_index >= 0 && can_index < info.cans.size())
    {
        m_down_road->SetPassRoadColour(left, info.cans[can_index].material_colour);
    }

    if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_NONE) {
        //cans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
        ams->SetAmsStep(ams_id, canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
        if (in_same_page) {
            m_down_road->UpdatePassRoad(pos, -1, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
            m_extruder->OnAmsLoading(false, extruder_id);
        }
    }

    if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP1) {
        ams->SetAmsStep(ams_id, canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_1);
        if (in_same_page) {
            m_down_road->UpdatePassRoad(pos, length, AMSPassRoadSTEP::AMS_ROAD_STEP_1);
            m_extruder->OnAmsLoading(false, extruder_id);
        }
        else
        {
            m_down_road->UpdatePassRoad(pos, -1, AMSPassRoadSTEP::AMS_ROAD_STEP_1);
        }
    }

    if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP2) {
        ams->SetAmsStep(ams_id, canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_2);
        if (in_same_page) {
            m_down_road->UpdatePassRoad(pos, length, AMSPassRoadSTEP::AMS_ROAD_STEP_2);
            m_extruder->OnAmsLoading(true, extruder_id, ams->GetTagColr(canid));
        }
        else
        {
            m_down_road->UpdatePassRoad(pos, -1, AMSPassRoadSTEP::AMS_ROAD_STEP_2);
        }
    }
    if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP3) {
        ams->SetAmsStep(ams_id, canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_3);
        if (in_same_page)
        {
            m_down_road->UpdatePassRoad(pos, length, AMSPassRoadSTEP::AMS_ROAD_STEP_3);
            m_extruder->OnAmsLoading(true, extruder_id, ams->GetTagColr(canid));
        }
        else
        {
            m_down_road->UpdatePassRoad(pos, -1, AMSPassRoadSTEP::AMS_ROAD_STEP_3);
        }
    }
}

void AMSControl::on_filament_load(wxCommandEvent &event)
{
    /*If the filament is unknown, show warning*/
    const auto& filament_id = get_filament_id(m_current_ams, GetCurrentCan(m_current_ams));
    if (filament_id.empty())
    {
        MessageDialog msg_dlg(nullptr, _L("Filament type is unknown which is required to perform this action. Please set target filament's informations."),
                              wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }

    for (auto i = 0; i < m_ams_info.size(); i++) {
        if (m_ams_info[i].ams_id == m_current_ams) { m_ams_info[i].current_action = AMSAction::AMS_ACTION_LOAD; }
    }
    post_event(SimpleEvent(EVT_AMS_LOAD));
}

void AMSControl::on_extrusion_cali(wxCommandEvent &event)
{
    for (auto i = 0; i < m_ams_info.size(); i++) {
        if (m_ams_info[i].ams_id == m_current_ams) { m_ams_info[i].current_action = AMSAction::AMS_ACTION_CALI; }
    }
    post_event(SimpleEvent(EVT_AMS_EXTRUSION_CALI));
}

void AMSControl::on_filament_unload(wxCommandEvent &event)
{
    for (auto i = 0; i < m_ams_info.size(); i++) {
        if (m_ams_info[i].ams_id == m_current_ams) { m_ams_info[i].current_action = AMSAction::AMS_ACTION_UNLOAD; }
    }
    post_event(SimpleEvent(EVT_AMS_UNLOAD));
}

void AMSControl::auto_refill(wxCommandEvent& event)
{
    post_event(SimpleEvent(EVT_AMS_FILAMENT_BACKUP));
}

void AMSControl::on_ams_setting_click(wxMouseEvent &event)
{
    post_event(SimpleEvent(EVT_AMS_SETTINGS));
}

void AMSControl::parse_object(MachineObject* obj) {
    if (!obj || obj->GetFilaSystem()->GetAmsList().size() == 0)
    {
        return;
    }
    m_ams_info.clear();
    for (auto ams : obj->GetFilaSystem()->GetAmsList())
    {
        AMSinfo info;
        info.parse_ams_info(obj, ams.second);
        m_ams_info.push_back(info);
    }
}

std::string AMSControl::get_filament_id(const std::string& ams_id, const std::string& can_id)
{
    for (const auto& ams_info : m_ams_info)
    {
        if (ams_info.ams_id == m_current_ams)
        {
            bool found = false;
            const auto& can_info = ams_info.get_caninfo(this->GetCurrentCan(m_current_ams), found);
            if (found)
            {
                return can_info.filament_id;
            }
        }
    }

    for (const auto& ext_info : m_ext_info)
    {
        if (ext_info.ams_id == m_current_ams)
        {
            bool found = false;
            const auto& can_info = ext_info.get_caninfo(this->GetCurrentCan(m_current_ams), found);
            if (found)
            {
                return can_info.filament_id;
            }
        }
    }

    return std::string();
}

void AMSControl::on_clibration_again_click(wxMouseEvent &event) { post_event(SimpleEvent(EVT_AMS_CLIBRATION_AGAIN)); }

void AMSControl::on_clibration_cancel_click(wxMouseEvent &event) { post_event(SimpleEvent(EVT_AMS_CLIBRATION_CANCEL)); }

void AMSControl::post_event(wxEvent &&event)
{
    event.SetEventObject(m_parent);
    wxPostEvent(m_parent, event);
}

bool AMSControl::IsInSlotPair(const std::string& ams_id) const
{
    for (auto ids : pair_id) {
        if (ids.first == ams_id || ids.second == ams_id) {
            return true;
        }
    }

    return false;
}

void AMSControl::UpdateAmsPreviewSelection()
{
    // Switch ams preview selection display
    for (auto prv_it : m_ams_preview_list) {
        AMSPreview* prv = prv_it.second;
        if (prv->get_ams_id() == m_current_show_ams_left || prv->get_ams_id() == m_current_show_ams_right) {
            prv->OnSelected();
        } else {
            prv->UnSelected();
        }
    }
}

}} // namespace Slic3r::GUI
