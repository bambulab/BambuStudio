#include "AMSControl.hpp"
#include "Label.hpp"
#include "../BitmapCache.hpp"
#include "../I18N.hpp"
#include "../GUI_App.hpp"

#include <wx/simplebook.h>
#include <wx/dcgraph.h>

#include <boost/log/trivial.hpp>

#include "CalibUtils.hpp"

namespace Slic3r { namespace GUI {

#define AMS_CANS_SIZE wxSize(FromDIP(284), -1)
#define AMS_CANS_WINDOW_SIZE wxSize(FromDIP(264), -1)
AMSControl::AMSControl(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size)
    : wxSimplebook(parent, wxID_ANY, pos, size)
    , m_Humidity_tip_popup(AmsHumidityTipPopup(this))
    , m_ams_introduce_popup(AmsIntroducePopup(this))
{
    Slic3r::DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (dev) {
        MachineObject *obj = dev->get_selected_machine();
        parse_object(obj);
    }

    SetBackgroundColour(*wxWHITE);
    // normal mode
    Freeze();
    wxBoxSizer *m_sizer_body = new wxBoxSizer(wxVERTICAL);
    m_amswin                 = new wxWindow(this, wxID_ANY);
    m_amswin->SetBackgroundColour(*wxWHITE);
    //m_amswin->SetBackgroundColour(wxColour(0x00CED1));
    m_amswin->SetSize(wxSize(FromDIP(578), -1));
    m_amswin->SetMinSize(wxSize(FromDIP(578), -1));


    m_sizer_ams_items = new wxBoxSizer(wxHORIZONTAL);

    /*right items*/
    m_panel_items_left = new wxPanel(m_amswin, wxID_ANY);
    m_panel_items_left->SetSize(AMS_ITEMS_PANEL_SIZE);
    m_panel_items_left->SetMinSize(AMS_ITEMS_PANEL_SIZE);
    //m_panel_items_left->SetBackgroundColour(0x4169E1);
    m_panel_items_left->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);
    m_sizer_items_left = new wxBoxSizer(wxHORIZONTAL);
    m_panel_items_left->SetSizer(m_sizer_items_left);
    m_panel_items_left->Layout();
    //m_sizer_items_left->Fit(m_panel_items_left);

    /*right items*/
    m_panel_items_right = new wxPanel(m_amswin, wxID_ANY);
    m_panel_items_right->SetSize(AMS_ITEMS_PANEL_SIZE);
    m_panel_items_right->SetMinSize(AMS_ITEMS_PANEL_SIZE);
    //m_panel_items_right->SetBackgroundColour(0x4169E1);
    m_panel_items_right->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);
    m_sizer_items_right = new wxBoxSizer(wxHORIZONTAL);
    m_panel_items_right->SetSizer(m_sizer_items_right);
    m_panel_items_right->Layout();
    //m_sizer_items_right->Fit(m_panel_items_right);

    /*m_sizer_ams_items->Add(m_panel_items_left, 0, wxALIGN_CENTER|wxLEFT|wxRIGHT, FromDIP(5));
    m_sizer_ams_items->Add(m_panel_items_right, 0, wxALIGN_CENTER|wxLEFT|wxRIGHT, FromDIP(5));*/
    m_sizer_ams_items->Add(m_panel_items_left, 0, wxLEFT, FromDIP(5));
    m_sizer_ams_items->Add(m_panel_items_right, 0, wxLEFT, FromDIP(5));

    //m_panel_items_right->Hide();

    //m_sizer_ams_body = new wxBoxSizer(wxHORIZONTAL);

    m_sizer_ams_body = new wxBoxSizer(wxHORIZONTAL);

    //ams tip
//    m_sizer_ams_tips = new wxBoxSizer(wxHORIZONTAL);
//    m_ams_tip = new Label(m_amswin, _L("AMS"));
//    m_ams_tip->SetFont(::Label::Body_12);
//    m_ams_tip->SetBackgroundColour(*wxWHITE);
//    m_img_amsmapping_tip = new wxStaticBitmap(m_amswin, wxID_ANY, create_scaled_bitmap("enable_ams", this, 16), wxDefaultPosition, wxSize(FromDIP(16), FromDIP(16)), 0);
//    m_img_amsmapping_tip->SetBackgroundColour(*wxWHITE);
//
//    m_sizer_ams_tips->Add(m_ams_tip, 0, wxTOP, FromDIP(5));
//    m_sizer_ams_tips->Add(m_img_amsmapping_tip, 0, wxALL, FromDIP(3));
//
//    m_img_amsmapping_tip->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {
//         wxPoint img_pos = m_img_amsmapping_tip->ClientToScreen(wxPoint(0, 0));
//         wxPoint popup_pos(img_pos.x, img_pos.y + m_img_amsmapping_tip->GetRect().height);
//         m_ams_introduce_popup.set_mode(true);
//         m_ams_introduce_popup.Position(popup_pos, wxSize(0, 0));
//         m_ams_introduce_popup.Popup();
//
//#ifdef __WXMSW__
//         wxCommandEvent close_event(EVT_CLEAR_SPEED_CONTROL);
//         wxPostEvent(this, close_event);
//#endif // __WXMSW__
//    });
//    m_img_amsmapping_tip->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) {
//         m_ams_introduce_popup.Dismiss();
//    });
//
//
   

    //ams area
    /*m_panel_left_ams_area = new StaticBox(m_amswin, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_panel_left_ams_area->SetMaxSize(AMS_CANS_SIZE);
    m_panel_left_ams_area->SetMinSize(AMS_CANS_SIZE);
    m_panel_left_ams_area->SetCornerRadius(FromDIP(10));
    m_panel_left_ams_area->SetBackgroundColor(StateColor(std::pair<wxColour, int>(AMS_CONTROL_DEF_BLOCK_BK_COLOUR, StateColor::Normal)));

    m_panel_right_ams_area = new StaticBox(m_amswin, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_panel_right_ams_area->SetMaxSize(AMS_CANS_SIZE);
    m_panel_right_ams_area->SetMinSize(AMS_CANS_SIZE);
    m_panel_right_ams_area->SetCornerRadius(FromDIP(10));
    m_panel_right_ams_area->SetBackgroundColor(StateColor(std::pair<wxColour, int>(AMS_CONTROL_DEF_BLOCK_BK_COLOUR, StateColor::Normal)));*/

    m_sizer_ams_area_left = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_ams_area_right = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_down_road = new wxBoxSizer(wxHORIZONTAL);

    m_simplebook_ams_left = new wxSimplebook(m_amswin, wxID_ANY, wxDefaultPosition, AMS_CANS_WINDOW_SIZE, 0);
    m_simplebook_ams_left->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);
    m_sizer_ams_area_left->Add(m_simplebook_ams_left, 0, wxLEFT | wxRIGHT, FromDIP(10));

    m_simplebook_ams_right = new wxSimplebook(m_amswin, wxID_ANY, wxDefaultPosition, AMS_CANS_WINDOW_SIZE, 0);
    m_simplebook_ams_right->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);
    m_sizer_ams_area_right->Add(m_simplebook_ams_right, 0, wxLEFT | wxRIGHT, FromDIP(10));

    m_panel_down_road = new wxPanel(m_amswin, wxID_ANY, wxDefaultPosition, AMS_DOWN_ROAD_SIZE, 0);
    m_panel_down_road->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);

    m_down_road = new AMSRoadDownPart(m_panel_down_road, wxID_ANY, wxDefaultPosition, AMS_DOWN_ROAD_SIZE);
    m_sizer_down_road->Add(m_panel_down_road, 0, wxALIGN_CENTER, 0);

    // ams mode
    //m_simplebook_ams_left = new wxSimplebook(m_simplebook_ams_left, wxID_ANY, wxDefaultPosition, AMS_CANS_WINDOW_SIZE, 0);
    //m_simplebook_ams_left->SetBackgroundColour(*wxGREEN);
    //m_simplebook_ams_left->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);
    //m_simplebook_ams_right = new wxSimplebook(m_simplebook_ams_right, wxID_ANY, wxDefaultPosition, AMS_CANS_WINDOW_SIZE, 0);
    m_simplebook_ams_right->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);


    //extra ams mode
    //m_simplebook_extra_cans_left = new wxSimplebook(m_simplebook_ams_left, wxID_ANY, wxDefaultPosition, AMS_CANS_WINDOW_SIZE, 0);
    //m_simplebook_extra_cans_left->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);
   // m_simplebook_extra_cans_right = new wxSimplebook(m_simplebook_ams_right, wxID_ANY, wxDefaultPosition, AMS_CANS_WINDOW_SIZE, 0);
    //m_simplebook_extra_cans_right->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);

    //m_simplebook_ams_left->AddPage(m_simplebook_ams_left, wxEmptyString, false);
    //m_simplebook_ams_left->AddPage(m_simplebook_extra_cans_left, wxEmptyString, false);
    //m_simplebook_ams_right->AddPage(m_simplebook_ams_right, wxEmptyString, false);
    //m_simplebook_ams_right->AddPage(m_simplebook_extra_cans_right, wxEmptyString, false);

    m_sizer_ams_area_left->Layout();
    m_sizer_ams_area_right->Layout();


    m_sizer_ams_option = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_option_left = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_option_mid = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_option_right = new wxBoxSizer(wxHORIZONTAL);

    /*m_sizer_option_left->SetMinSize( wxSize( AMS_CANS_SIZE.x,-1 ) );
    m_sizer_option_right->SetMinSize( wxSize( AMS_CANS_SIZE.x,-1 ) );*/
    /*m_sizer_option_left->SetMinSize(wxSize(FromDIP(239), -1));
    m_sizer_option_right->SetMinSize(wxSize(FromDIP(239), -1));*/
    m_sizer_option_left->SetMinSize(wxSize(FromDIP(180), -1));
    //m_sizer_option_right->SetMinSize(wxSize(FromDIP(120), -1));

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
    m_button_auto_refill = new Button(m_amswin, _L("Auto-refill"));
    m_button_auto_refill->SetBackgroundColor(btn_bg_white);
    m_button_auto_refill->SetBorderColor(btn_bd_white);
    m_button_auto_refill->SetTextColor(btn_text_white);
    m_button_auto_refill->SetFont(Label::Body_13);
    // m_img_ams_backup = new wxStaticBitmap(m_amswin, wxID_ANY, create_scaled_bitmap("automatic_material_renewal", this, 16), wxDefaultPosition, wxSize(FromDIP(16), FromDIP(16)), 0);
    // m_img_ams_backup->SetBackgroundColour(*wxWHITE);
    // m_img_ams_backup->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {SetCursor(wxCURSOR_HAND); });
    // m_img_ams_backup->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) {SetCursor(wxCURSOR_ARROW); });
    // m_img_ams_backup->Bind(wxEVT_LEFT_DOWN, [this](auto& e) {post_event(SimpleEvent(EVT_AMS_FILAMENT_BACKUP)); });
    m_sizer_option_left->Add(m_button_auto_refill, 0, wxLEFT, 0);

    m_button_ams_setting_normal = ScalableBitmap(this, "ams_setting_normal", 24);
    m_button_ams_setting_hover = ScalableBitmap(this, "ams_setting_hover", 24);
    m_button_ams_setting_press = ScalableBitmap(this, "ams_setting_press", 24);

    m_button_ams_setting = new wxStaticBitmap(m_amswin, wxID_ANY, m_button_ams_setting_normal.bmp(), wxDefaultPosition, wxSize(FromDIP(24), FromDIP(24)));
    m_button_ams_setting->SetBackgroundColour(m_amswin->GetBackgroundColour());
    m_sizer_option_left->Add(m_button_ams_setting, 0, wxLEFT, FromDIP(5));


    /*option mid*/
    m_extruder = new AMSextruder(m_amswin, wxID_ANY, wxDefaultPosition, AMS_EXTRUDER_SIZE);
    m_sizer_option_mid->Add( m_extruder, 0, wxALIGN_CENTER, 0 );


    /*option right*/



    m_button_extruder_feed = new Button(m_amswin, _L("Load"));
    m_button_extruder_feed->SetFont(Label::Body_13);

    m_button_extruder_feed->SetBackgroundColor(btn_bg_green);
    m_button_extruder_feed->SetBorderColor(btn_bd_green);
    m_button_extruder_feed->SetTextColor(btn_text_green);


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

    m_button_extruder_back = new Button(m_amswin, _L("Unload"));
    m_button_extruder_back->SetBackgroundColor(btn_bg_white);
    m_button_extruder_back->SetBorderColor(btn_bd_white);
    m_button_extruder_back->SetTextColor(btn_text_white);
    m_button_extruder_back->SetFont(Label::Body_13);

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
    m_sizer_option_right->Add(m_button_extruder_back, 0, wxLEFT, FromDIP(60));
    m_sizer_option_right->Add(m_button_extruder_feed, 0, wxLEFT, FromDIP(5));


    m_sizer_ams_option->Add(m_sizer_option_left, 0, wxEXPAND, 0);
    //m_sizer_ams_option->Add(m_sizer_option_mid, 1, wxEXPAND, 0);
    m_sizer_ams_option->Add(m_sizer_option_mid, 1, wxEXPAND, 0);
    //m_sizer_ams_option->Add(m_sizer_option_right, 0, wxEXPAND, 0);
    m_sizer_ams_option->Add(m_sizer_option_right, 0, wxLEFT, 0);



    //virtual ams
    //m_panel_virtual = new StaticBox(m_amswin, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    //m_panel_virtual->SetBackgroundColor(StateColor(std::pair<wxColour, int>(AMS_CONTROL_DEF_BLOCK_BK_COLOUR, StateColor::Normal)));
    //m_panel_virtual->SetMinSize(wxSize(AMS_CAN_LIB_SIZE.x + FromDIP(16), AMS_CANS_SIZE.y));
    //m_panel_virtual->SetMaxSize(wxSize(AMS_CAN_LIB_SIZE.x + FromDIP(16), AMS_CANS_SIZE.y));

    //m_vams_info.material_state = AMSCanType::AMS_CAN_TYPE_VIRTUAL;
    //m_vams_info.can_id = wxString::Format("%d", VIRTUAL_TRAY_MAIN_ID).ToStdString();

    //auto vams_panel = new wxWindow(m_panel_virtual, wxID_ANY);
    //vams_panel->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);

    //m_vams_lib = new AMSLib(vams_panel, m_vams_info);
    //m_vams_road = new AMSRoad(vams_panel, wxID_ANY, m_vams_info, -1, -1, wxDefaultPosition, AMS_CAN_ROAD_SIZE);

    //m_vams_lib->Bind(wxEVT_LEFT_DOWN, [this](auto& e) {
    //    //clear all selected
    //    m_current_ams = m_vams_info.can_id;
    //    m_vams_lib->OnSelected();

    //    SwitchAms(m_current_ams);
    //    for (auto i = 0; i < m_ams_item_list.GetCount(); i++) {
    //        AmsCansWindow* cans = m_ams_item_list[i];
    //        cans->amsCans->SelectCan(m_current_ams);
    //    }

    //    e.Skip();
    //    });

    //Bind(EVT_AMS_UNSELETED_VAMS, [this](wxCommandEvent& e) {
    //    /*if (m_current_ams == e.GetString().ToStdString()) {
    //        return;
    //    }*/
    //    m_current_ams = e.GetString().ToStdString();
    //    SwitchAms(m_current_ams);
    //    m_vams_lib->UnSelected();
    //    e.Skip();
    //});

    //wxBoxSizer* m_vams_top_sizer = new wxBoxSizer(wxVERTICAL);

    //m_vams_top_sizer->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(14));
    //m_vams_top_sizer->Add(0, 0, 0, wxEXPAND | wxTOP, AMS_REFRESH_SIZE.y);
    //m_vams_top_sizer->Add(m_vams_lib, 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, FromDIP(4));
    //m_vams_top_sizer->Add(m_vams_road, 0, wxALL, 0);

    //Bind(EVT_AMS_UNSELETED_VAMS, [this](wxCommandEvent& e) {
    //    /*if (m_current_ams == e.GetString().ToStdString()) {
    //        return;
    //    }*/
    //    m_current_ams = e.GetString().ToStdString();
    //    SwitchAms(m_current_ams);
    //    m_vams_lib->UnSelected();
    //    e.Skip();
    //});

    ////extra road

    //vams_panel->SetSizer(m_vams_top_sizer);
    //vams_panel->Layout();
    //vams_panel->Fit();

    //wxBoxSizer* m_sizer_vams_panel = new wxBoxSizer(wxVERTICAL);

    //m_sizer_vams_panel->Add(vams_panel, 0, wxALIGN_CENTER_HORIZONTAL, 0);
    //m_panel_virtual->SetSizer(m_sizer_vams_panel);
    //m_panel_virtual->Layout();
    //m_panel_virtual->Fit();

    //m_vams_sizer =  new wxBoxSizer(wxVERTICAL);
    //m_sizer_vams_tips = new wxBoxSizer(wxHORIZONTAL);

//    auto m_vams_tip = new wxStaticText(m_amswin, wxID_ANY, _L("Ext Spool"), wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
//    m_vams_tip->SetMaxSize(wxSize(FromDIP(66), -1));
//    m_vams_tip->SetFont(::Label::Body_12);
//    m_vams_tip->SetBackgroundColour(*wxWHITE);
//    m_img_vams_tip = new wxStaticBitmap(m_amswin, wxID_ANY, create_scaled_bitmap("enable_ams", this, 16), wxDefaultPosition, wxSize(FromDIP(16), FromDIP(16)), 0);
//    m_img_vams_tip->SetBackgroundColour(*wxWHITE);
//    m_img_vams_tip->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {
//        wxPoint img_pos = m_img_vams_tip->ClientToScreen(wxPoint(0, 0));
//        wxPoint popup_pos(img_pos.x, img_pos.y + m_img_vams_tip->GetRect().height);
//        m_ams_introduce_popup.set_mode(false);
//        m_ams_introduce_popup.Position(popup_pos, wxSize(0, 0));
//        m_ams_introduce_popup.Popup();
//
//#ifdef __WXMSW__
//        wxCommandEvent close_event(EVT_CLEAR_SPEED_CONTROL);
//        wxPostEvent(this, close_event);
//#endif // __WXMSW__
//    });
//
//    m_img_vams_tip->Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& e) {
//        m_ams_introduce_popup.Dismiss();
//    });
//
//    m_sizer_vams_tips->Add(m_vams_tip, 0, wxTOP, FromDIP(5));
//    m_sizer_vams_tips->Add(m_img_vams_tip, 0, wxALL, FromDIP(3));

    //m_vams_extra_road = new AMSVirtualRoad(m_amswin, wxID_ANY);
    //m_vams_extra_road->SetMinSize(wxSize(m_panel_virtual->GetSize().x + FromDIP(16), -1));

    //m_vams_sizer->Add(m_sizer_vams_tips, 0, wxALIGN_CENTER, 0);
    //m_vams_sizer->Add(m_panel_virtual, 0, wxALIGN_CENTER, 0);
    //m_vams_sizer->Add(m_vams_extra_road, 1, wxEXPAND, 0);


    //Right
    /*
        addaddaddaddaddaaddaddaddadd
    */

     /*
    wxBoxSizer *m_sizer_right = new wxBoxSizer(wxVERTICAL);
    m_simplebook_right        = new wxSimplebook(m_amswin, wxID_ANY);
    m_simplebook_right->SetMinSize(wxSize(AMS_STEP_SIZE.x, AMS_STEP_SIZE.y + FromDIP(19)));
    m_simplebook_right->SetMaxSize(wxSize(AMS_STEP_SIZE.x, AMS_STEP_SIZE.y + FromDIP(19)));
    m_simplebook_right->SetBackgroundColour(*wxWHITE);

    m_sizer_right->Add(m_simplebook_right, 0, wxALL, 0);

    auto tip_right    = new wxPanel(m_simplebook_right, wxID_ANY, wxDefaultPosition, AMS_STEP_SIZE, wxTAB_TRAVERSAL);
    m_sizer_right_tip = new wxBoxSizer(wxVERTICAL);

    m_tip_right_top   = new wxStaticText(tip_right, wxID_ANY, _L("Tips"), wxDefaultPosition, wxDefaultSize, 0);
    m_tip_right_top->SetFont(::Label::Head_13);
    m_tip_right_top->SetForegroundColour(AMS_CONTROL_BRAND_COLOUR);
    m_tip_right_top->Wrap(AMS_STEP_SIZE.x);


    m_tip_load_info = new ::Label(tip_right, wxEmptyString);
    m_tip_load_info->SetFont(::Label::Body_13);
    m_tip_load_info->SetBackgroundColour(*wxWHITE);
    m_tip_load_info->SetForegroundColour(AMS_CONTROL_GRAY700);

    m_sizer_right_tip->Add(m_tip_right_top, 0, 0, 0);
    m_sizer_right_tip->Add(0, 0, 0, wxEXPAND, FromDIP(10));
    m_sizer_right_tip->Add(m_tip_load_info, 0, 0, 0);

    tip_right->SetSizer(m_sizer_right_tip);
    tip_right->Layout();

    m_filament_load_step = new ::StepIndicator(m_simplebook_right, wxID_ANY);
    m_filament_load_step->SetMinSize(AMS_STEP_SIZE);
    m_filament_load_step->SetMaxSize(AMS_STEP_SIZE);
    m_filament_load_step->SetBackgroundColour(*wxWHITE);

    m_filament_unload_step = new ::StepIndicator(m_simplebook_right, wxID_ANY);
    m_filament_unload_step->SetMinSize(AMS_STEP_SIZE);
    m_filament_unload_step->SetMaxSize(AMS_STEP_SIZE);
    m_filament_unload_step->SetBackgroundColour(*wxWHITE);

    m_filament_vt_load_step = new ::StepIndicator(m_simplebook_right, wxID_ANY);
    m_filament_vt_load_step->SetMinSize(AMS_STEP_SIZE);
    m_filament_vt_load_step->SetMaxSize(AMS_STEP_SIZE);
    m_filament_vt_load_step->SetBackgroundColour(*wxWHITE);

    m_simplebook_right->AddPage(tip_right, wxEmptyString, false);
    m_simplebook_right->AddPage(m_filament_load_step, wxEmptyString, false);
    m_simplebook_right->AddPage(m_filament_unload_step, wxEmptyString, false);
    m_simplebook_right->AddPage(m_filament_vt_load_step, wxEmptyString, false);




    m_button_guide = new Button(m_amswin, _L("Guide"));
    m_button_guide->SetFont(Label::Body_13);
    if (wxGetApp().app_config->get("language") == "de_DE") m_button_guide->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "fr_FR") m_button_guide->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "ru_RU") m_button_guide->SetLabel("Guide");
    if (wxGetApp().app_config->get("language") == "nl_NL") m_button_guide->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "hu_HU") m_button_guide->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "ja_JP") m_button_guide->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "sv_SE") m_button_guide->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "cs_CZ") m_button_guide->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "uk_UA") m_button_guide->SetFont(Label::Body_9);

    m_button_guide->SetCornerRadius(FromDIP(12));
    m_button_guide->SetBorderColor(btn_bd_white);
    m_button_guide->SetTextColor(btn_text_white);
    m_button_guide->SetMinSize(wxSize(-1, FromDIP(24)));
    m_button_guide->SetBackgroundColor(btn_bg_white);

    m_button_retry = new Button(m_amswin, _L("Retry"));
    m_button_retry->SetFont(Label::Body_13);

    if (wxGetApp().app_config->get("language") == "de_DE") m_button_retry->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "fr_FR") m_button_retry->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "ru_RU") m_button_retry->SetLabel("Retry");
    if (wxGetApp().app_config->get("language") == "nl_NL") m_button_retry->SetLabel("Retry");
    if (wxGetApp().app_config->get("language") == "tr_TR") m_button_retry->SetLabel("Retry");
    if (wxGetApp().app_config->get("language") == "hu_HU") m_button_retry->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "ja_JP") m_button_retry->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "sv_SE") m_button_retry->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "cs_CZ") m_button_retry->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "uk_UA") m_button_retry->SetFont(Label::Body_9);

    m_button_retry->SetCornerRadius(FromDIP(12));
    m_button_retry->SetBorderColor(btn_bd_white);
    m_button_retry->SetTextColor(btn_text_white);
    m_button_retry->SetMinSize(wxSize(-1, FromDIP(24)));
    m_button_retry->SetBackgroundColor(btn_bg_white);

    m_sizer_right_bottom->Add(m_button_ams_setting, 0);
    m_sizer_right_bottom->Add(m_button_guide, 0, wxLEFT, FromDIP(10));
    m_sizer_right_bottom->Add(m_button_retry, 0, wxLEFT, FromDIP(10));
    m_sizer_right->Add(m_sizer_right_bottom, 0, wxEXPAND | wxTOP, FromDIP(20));*/


    /*
        addaddaddaddaddaaddaddaddadd
    */

    m_sizer_ams_body->Add(m_sizer_ams_area_left, wxLEFT|wxRIGHT, FromDIP(5));
    //m_sizer_ams_body->Add(0, 0, 0, wxLEFT, FromDIP(15));
    m_sizer_ams_body->Add(m_sizer_ams_area_right, wxLEFT|wxRIGHT, FromDIP(5));

    //m_sizer_ams_body->Add(m_sizer_right, 0, wxEXPAND, FromDIP(0));

    m_sizer_body->Add(m_sizer_ams_items, 0, wxALIGN_CENTER, 0);
    m_sizer_body->Add(0, 0, 1, wxEXPAND | wxTOP, FromDIP(10));
    m_sizer_body->Add(m_sizer_ams_body, 0, wxALIGN_CENTER, 0);
    m_sizer_body->Add(m_sizer_down_road, 0, wxALIGN_CENTER, 0);
    //m_sizer_body->Add(m_sizer_ams_body, 0, wxEXPAND, 0);
    //m_sizer_body->Add(m_sizer_ams_option, 0, wxEXPAND, 0);
    m_sizer_body->Add(m_sizer_ams_option, 0, wxALIGN_CENTER, 0);

    m_amswin->SetSizer(m_sizer_body);
    m_amswin->Layout();
    m_amswin->Fit();
    Thaw();

    SetSize(m_amswin->GetSize());
    SetMinSize(m_amswin->GetSize());



    AddPage(m_amswin, wxEmptyString, false);

    UpdateStepCtrl(false);

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

        wxPoint img_pos = ClientToScreen(wxPoint(0, 0));
        wxPoint popup_pos(img_pos.x - m_Humidity_tip_popup.GetSize().GetWidth() + FromDIP(150), img_pos.y - FromDIP(80));
        m_Humidity_tip_popup.Position(popup_pos, wxSize(0, 0));
        if (m_ams_info.size() > 0) {
            for (auto i = 0; i < m_ams_info.size(); i++) {
                if (m_ams_info[i].ams_id == m_current_show_ams_left || m_ams_info[i].ams_id == m_current_show_ams_right) {
                    m_Humidity_tip_popup.set_humidity_level(m_ams_info[i].ams_humidity);
                }
            }

        }
        m_Humidity_tip_popup.Popup();
    });


    /* m_button_guide->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
         post_event(wxCommandEvent(EVT_AMS_GUIDE_WIKI));
         });
     m_button_retry->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
         post_event(wxCommandEvent(EVT_AMS_RETRY));
         });*/

    //CreateAms();
    //CreateAmsNew();
    //EnterNoneAMSMode();

}

void AMSControl::on_retry()
{
    post_event(wxCommandEvent(EVT_AMS_RETRY));
}

AMSControl::~AMSControl() {
    /*m_simplebook_ams_left->DeleteAllPages();
    m_simplebook_ams_right->DeleteAllPages();*/
}

std::string AMSControl::GetCurentAms(bool right_panel) {
    if (right_panel){
        return m_current_ams_right;
    }
    else{
        return m_current_ams_left;
    }
}
std::string AMSControl::GetCurentShowAms(bool right_panel) {
    if (right_panel){
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
        if (item->m_info.ams_id == amsid) {
            current_can = item->GetCurrentCan();
            return current_can;
        }
    }
    return current_can;
}

bool AMSControl::IsAmsInRightPanel(std::string ams_id) {
    if (m_nozzle_num == 2){
        if (m_ams_item_list[ams_id]->m_info.nozzle_id == 0){
            return true;
        }
        else{
            return false;
        }
    }
    else{
        for (auto id : m_item_ids[1]){
            if (id == ams_id){
                return true;
            }
        }
        return false;
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

void AMSControl::SetActionState(bool button_status[])
{
    if (button_status[ActionButton::ACTION_BTN_LOAD]) m_button_extruder_feed->Enable();
    else m_button_extruder_feed->Disable();

    if (button_status[ActionButton::ACTION_BTN_UNLOAD]) m_button_extruder_back->Enable();
    else m_button_extruder_back->Disable();
}

void AMSControl::EnterNoneAMSMode()
{
    //m_vams_lib->m_ams_model = m_ext_model;
    if(m_is_none_ams_mode == AMSModel::NO_AMS) return;
    m_panel_items_left->Hide();

    m_simplebook_ams_left->SetSelection(0);
    m_extruder->no_ams_mode(true);
    //m_button_ams_setting->Hide();
    //m_button_guide->Hide();
    //m_button_extruder_feed->Show();
    //m_button_extruder_back->Show();

    ShowFilamentTip(false);
    m_amswin->Layout();
    m_amswin->Fit();
    Layout();
    m_is_none_ams_mode = AMSModel::NO_AMS;
}

void AMSControl::EnterGenericAMSMode()
{
    //m_vams_lib->m_ams_model = m_ext_model;
    if(m_is_none_ams_mode == AMSModel::GENERIC_AMS) return;
    //m_panel_items_left->Show();

    //m_vams_lib->m_ams_model = AMSModel::GENERIC_AMS;
    //m_ams_tip->SetLabel(_L("AMS"));
    //m_img_vams_tip->SetBitmap(create_scaled_bitmap("enable_ams", this, 16));
    //m_img_vams_tip->Enable();
    //m_img_amsmapping_tip->SetBitmap(create_scaled_bitmap("enable_ams", this, 16));
    //m_img_amsmapping_tip->Enable();

    //m_simplebook_ams_left->SetSelection(0);
    m_extruder->no_ams_mode(false);
    /*m_button_ams_setting->Show();
    m_button_guide->Show();
    m_button_retry->Show();
    m_button_extruder_feed->Show();
    m_button_extruder_back->Show();
    ShowFilamentTip(true);*/
    m_amswin->Layout();
    m_amswin->Fit();
    Layout();
    m_is_none_ams_mode = AMSModel::GENERIC_AMS;
}

void AMSControl::EnterExtraAMSMode()
{
    //m_vams_lib->m_ams_model = m_ext_model;
    if(m_is_none_ams_mode == AMSModel::EXTRA_AMS) return;
    m_panel_items_left->Hide();


    //m_vams_lib->m_ams_model = AMSModel::EXTRA_AMS;
    //m_ams_tip->SetLabel(wxEmptyString);
    //m_img_vams_tip->SetBitmap(create_scaled_bitmap("enable_ams_disable", this, 16));
    //m_img_vams_tip->Disable();
    //m_img_amsmapping_tip->SetBitmap(create_scaled_bitmap("enable_ams_disable", this, 16));
    //m_img_amsmapping_tip->Disable();

    m_simplebook_ams_left->SetSelection(2);
    m_extruder->no_ams_mode(false);
    /*m_button_ams_setting->Show();
    m_button_guide->Show();
    m_button_retry->Show();
    m_button_extruder_feed->Show();
    m_button_extruder_back->Show();
    ShowFilamentTip(true);*/
    m_amswin->Layout();
    m_amswin->Fit();
    Layout();
    Refresh(true);
    m_is_none_ams_mode = AMSModel::EXTRA_AMS;

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
    m_vams_extra_road->msw_rescale();

    m_button_extruder_feed->SetMinSize(wxSize(-1, FromDIP(24)));
    m_button_extruder_back->SetMinSize(wxSize(-1, FromDIP(24)));
    m_button_auto_refill->SetMinSize(wxSize(-1, FromDIP(24)));
    m_button_ams_setting->SetMinSize(wxSize(FromDIP(25), FromDIP(24)));
    m_button_guide->SetMinSize(wxSize(-1, FromDIP(24)));
    m_button_retry->SetMinSize(wxSize(-1, FromDIP(24)));
    m_vams_lib->msw_rescale();


    for (auto ams_item : m_ams_item_list) {
        ams_item.second->msw_rescale();
    }

    Layout();
    Refresh();
}

void AMSControl::UpdateStepCtrl(bool is_extrusion)
{
    /*wxString FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_COUNT] = {
            _L("Idling..."),
            _L("Heat the nozzle"),
            _L("Cut filament"),
            _L("Pull back current filament"),
            _L("Push new filament into extruder"),
            _L("Purge old filament"),
            _L("Feed Filament"),
            _L("Confirm extruded"),
            _L("Check filament location")
    };

    m_filament_load_step->DeleteAllItems();
    m_filament_unload_step->DeleteAllItems();
    m_filament_vt_load_step->DeleteAllItems();

    if (m_ams_model == AMSModel::GENERIC_AMS || m_ext_model == AMSModel::GENERIC_AMS) {
        if (is_extrusion) {
            m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_HEAT_NOZZLE]);
            m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_CUT_FILAMENT]);
            m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PULL_CURR_FILAMENT]);
            m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PUSH_NEW_FILAMENT]);
            m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PURGE_OLD_FILAMENT]);
        }
        else {
            m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_HEAT_NOZZLE]);
            m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PUSH_NEW_FILAMENT]);
            m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PURGE_OLD_FILAMENT]);
        }

        m_filament_vt_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_HEAT_NOZZLE]);
        m_filament_vt_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PUSH_NEW_FILAMENT]);
        m_filament_vt_load_step->AppendItem(_L("Grab new filament"));
        m_filament_vt_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PURGE_OLD_FILAMENT]);

        m_filament_unload_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_HEAT_NOZZLE]);
        m_filament_unload_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_CUT_FILAMENT]);
        m_filament_unload_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PULL_CURR_FILAMENT]);
    }


    if (m_ams_model == AMSModel::EXTRA_AMS || m_ext_model == AMSModel::EXTRA_AMS) {
        m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_HEAT_NOZZLE]);
        m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_CHECK_POSITION]);
        m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_CUT_FILAMENT]);
        m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PULL_CURR_FILAMENT]);
        m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PUSH_NEW_FILAMENT]);
        m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PURGE_OLD_FILAMENT]);

        m_filament_vt_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_HEAT_NOZZLE]);
        m_filament_vt_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_CHECK_POSITION]);
        m_filament_vt_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_CUT_FILAMENT]);
        m_filament_vt_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PULL_CURR_FILAMENT]);
        m_filament_vt_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PUSH_NEW_FILAMENT]);
        m_filament_vt_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PURGE_OLD_FILAMENT]);

        m_filament_unload_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_HEAT_NOZZLE]);
        m_filament_unload_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_CHECK_POSITION]);
        m_filament_unload_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_CUT_FILAMENT]);
        m_filament_unload_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PULL_CURR_FILAMENT]);
    }*/
}

void AMSControl::UpdatePassRoad(string ams_id, AMSPassRoadType type, AMSPassRoadSTEP step) {
    bool left = false;
    int len = -1;
    for (auto id : m_item_ids[0])
    {
        if (ams_id == id)
        {
            left = true;
            break;
        }
    }
    if (m_ams_item_list[ams_id]->m_info.cans.size() == 4)
    {
        len = 133;
    }
    else
    {
        for (auto pairId : pair_id)
        {
            if (pairId.first == ams_id) {
                len = 72;
                break;
            }
            if (pairId.second == ams_id)
            {
                len = 188;
                break;
            }
        }
    }
    if (len == -1)
    {
        if (left)
        {
            len = 213;
        }
        else
        {
            len = 72;
        }
    }

    //std::vector<int>                 m_item_nums = { 0, 0 };
    //std::vector<std::vector<string>> m_item_ids = { {},{} };
}


void AMSControl::CreateAms()
{
    auto caninfo0_0 = Caninfo{"def_can_0", (""), *wxWHITE, AMSCanType::AMS_CAN_TYPE_VIRTUAL};
    auto caninfo0_1 = Caninfo{"def_can_1", (""), *wxWHITE, AMSCanType::AMS_CAN_TYPE_VIRTUAL };
    auto caninfo0_2 = Caninfo{"def_can_2", (""), *wxWHITE, AMSCanType::AMS_CAN_TYPE_VIRTUAL };
    auto caninfo0_3 = Caninfo{"def_can_3", (""), *wxWHITE, AMSCanType::AMS_CAN_TYPE_VIRTUAL };

    auto caninfo1_0 = Caninfo{ "def_can_0", (""), *wxWHITE, AMSCanType::AMS_CAN_TYPE_VIRTUAL };
    auto caninfo1_1 = Caninfo{ "def_can_1", (""), *wxWHITE, AMSCanType::AMS_CAN_TYPE_VIRTUAL };
    auto caninfo1_2 = Caninfo{ "def_can_2", (""), *wxWHITE, AMSCanType::AMS_CAN_TYPE_VIRTUAL };
    auto caninfo1_3 = Caninfo{ "def_can_3", (""), *wxWHITE, AMSCanType::AMS_CAN_TYPE_VIRTUAL };

    AMSinfo                        ams1 = AMSinfo{"0", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}, 0};
    AMSinfo                        ams2 = AMSinfo{"1", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}, 0 };
    AMSinfo                        ams3 = AMSinfo{"2", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}, 0 };
    AMSinfo                        ams4 = AMSinfo{"3", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}, 0 };

    AMSinfo                        ams5 = AMSinfo{ "4", std::vector<Caninfo>{caninfo1_0, caninfo1_1, caninfo1_2, caninfo1_3}, 1 };
    AMSinfo                        ams6 = AMSinfo{ "5", std::vector<Caninfo>{caninfo1_0, caninfo1_1, caninfo1_2, caninfo1_3}, 1 };
    AMSinfo                        ams7 = AMSinfo{ "6", std::vector<Caninfo>{caninfo1_0, caninfo1_1, caninfo1_2, caninfo1_3}, 1 };
    AMSinfo                        ams8 = AMSinfo{ "7", std::vector<Caninfo>{caninfo1_0, caninfo1_1, caninfo1_2, caninfo1_3}, 1 };
    std::vector<AMSinfo>           ams_info{ams1, ams2, ams3, ams4, ams5, ams6, ams7, ams8 };
    std::vector<AMSinfo>::iterator it;
    Freeze();
    for (it = ams_info.begin(); it != ams_info.end(); it++) {
        AddAmsPreview(*it, AMSModel::GENERIC_AMS);
        AddAms(*it);
        //AddExtraAms(*it);
        m_ams_info.push_back(*it);
    }
    if (m_single_nozzle_no_ams)
    {
        m_simplebook_ams_left->Hide();
    }
    else {
        m_sizer_items_left->Layout();
        m_sizer_items_right->Layout();
    }
    Thaw();
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

    m_ams_item_list.clear();
    m_sizer_items_right->Clear();
    m_sizer_items_left->Clear();
}

void AMSControl::CreateAmsNew()
{
    /*m_ams_item_list.clear();
    m_ams_generic_item_list.clear();
    m_ams_extra_item_list.clear();*/
    AMSRoadShowMode left_init_mode = AMSRoadShowMode::AMS_ROAD_MODE_SINGLE;
    AMSRoadShowMode right_init_mode = AMSRoadShowMode::AMS_ROAD_MODE_SINGLE;
    bool first_left_page = true, first_right_page = true;

    std::vector<AMSinfo> single_info_left;
    std::vector<AMSinfo> single_info_right;

    Freeze();
    for (auto ams_info = m_ams_info.begin(); ams_info != m_ams_info.end(); ams_info++)
    {

        if (first_left_page && ams_info->nozzle_id == 1) {
            first_left_page = false;
            left_init_mode = ams_info->cans.size() == 4 ? AMSRoadShowMode::AMS_ROAD_MODE_FOUR : AMSRoadShowMode::AMS_ROAD_MODE_SINGLE;
        }
        if (ams_info->cans.size() == 4)
        {
            if (first_right_page && ams_info->nozzle_id == 0) {
                first_right_page = false;
                right_init_mode = AMSRoadShowMode::AMS_ROAD_MODE_FOUR;
            }
            if (first_left_page && ams_info->nozzle_id == 1) {
                first_left_page = false;
                left_init_mode = AMSRoadShowMode::AMS_ROAD_MODE_FOUR;
            }
            AddAmsPreview(*ams_info, ams_info->ams_type);
            AddAms(*ams_info);
        }
        else if (ams_info->cans.size() == 1)
        {
            AddAmsPreview(*ams_info, ams_info->ams_type);
            if (ams_info->nozzle_id == 0)
            {
                single_info_right.push_back(*ams_info);
                if (single_info_right.size() == 2)
                {
                    if (first_right_page) {
                        first_right_page = false;
                        right_init_mode = AMSRoadShowMode::AMS_ROAD_MODE_DOUBLE;
                    }
                    AddAms(single_info_right);
                    pair_id.push_back(std::make_pair(single_info_right[0].ams_id, single_info_right[1].ams_id));
                    single_info_right.clear();
                }
            }
            else if (ams_info->nozzle_id == 1)
            {
                single_info_left.push_back(*ams_info);
                if (single_info_left.size() == 2)
                {
                    if (first_left_page) {
                        first_left_page = false;
                        left_init_mode = AMSRoadShowMode::AMS_ROAD_MODE_DOUBLE;
                    }
                    AddAms(single_info_left);
                    pair_id.push_back(std::make_pair(single_info_left[0].ams_id, single_info_left[1].ams_id));
                    single_info_left.clear();
                }
            }
        }
    }
    if (m_ext_info.size() <= 1) {
        BOOST_LOG_TRIVIAL(trace) << "vt_slot empty!";
        return;
    }
    AMSinfo ext_info;
    for (auto info : m_ext_info){
        if (info.ams_id == std::to_string(VIRTUAL_TRAY_MAIN_ID)){
            ext_info = info;
        }
    }
    single_info_right.push_back(ext_info);
    //wait add
    if (single_info_right.size() == 2)
    {
        if (first_right_page) {
            first_right_page = false;
            right_init_mode = AMSRoadShowMode::AMS_ROAD_MODE_DOUBLE;
        }
        pair_id.push_back(std::make_pair(single_info_right[0].ams_id, single_info_right[1].ams_id));
    }
    AddAmsPreview(ext_info, AMSModel::NO_AMS);
    AddAms(single_info_right);
    single_info_right.clear();

    for (auto info : m_ext_info) {
        if (info.ams_id == std::to_string(VIRTUAL_TRAY_DEPUTY_ID)) {
            ext_info = info;
        }
    }
    single_info_left.push_back(ext_info);
    //wait add
    if (single_info_left.size() == 2)
    {
        if (first_left_page) {
            first_left_page = false;
            left_init_mode = AMSRoadShowMode::AMS_ROAD_MODE_DOUBLE;
        }
        pair_id.push_back(std::make_pair(single_info_left[0].ams_id, single_info_left[1].ams_id));
    }
    AddAmsPreview(ext_info, AMSModel::NO_AMS);
    AddAms(single_info_left);
    single_info_left.clear();

    if (m_nozzle_num <= 1)
    {
        m_simplebook_ams_left->Hide();
        m_panel_items_left->Hide();
    }
    else if(m_nozzle_num > 1) {
        m_sizer_items_left->Layout();
        m_sizer_items_right->Layout();
        m_simplebook_ams_left->Show();
        m_panel_items_left->Show();
        m_simplebook_ams_right->Show();
        m_panel_items_right->Show();
        m_simplebook_ams_left->SetSelection(0);
        m_simplebook_ams_right->SetSelection(0);
        m_down_road->UpdateLeft(2, left_init_mode);
        m_down_road->UpdateRight(2, right_init_mode);
    }
    m_extruder->update(2);
    auto it = m_ams_item_list.begin();
    m_down_road->UpdatePassRoad(0, true, -1, it->second->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    m_down_road->UpdatePassRoad(0, false, -1, (++it)->second->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    //Refresh();
    //Freeze();
    Thaw();
}

void AMSControl::CreateAmsSingleNozzle()
{
    std::vector<int>m_item_nums{0,0};
    std::vector<AMSinfo> single_info;

    Freeze();

    bool left = true;
    AMSRoadShowMode left_init_mode = AMSRoadShowMode::AMS_ROAD_MODE_NONE;
    AMSRoadShowMode right_init_mode = AMSRoadShowMode::AMS_ROAD_MODE_NONE;
    for (auto ams_info = m_ams_info.begin(); ams_info != m_ams_info.end(); ams_info++){
        if (ams_info->cans.size() == 4){
            if (m_item_nums[0] <= m_item_nums[1]){
                if (m_item_nums[0] == 0) left_init_mode = AMSRoadShowMode::AMS_ROAD_MODE_FOUR;
                left = true;
                m_item_ids[0].push_back(ams_info->ams_id);
                m_item_nums[0]++;
            }
            else{
                if (m_item_nums[1] == 0) right_init_mode = AMSRoadShowMode::AMS_ROAD_MODE_FOUR;
                left = false;
                m_item_ids[1].push_back(ams_info->ams_id);
                m_item_nums[1]++;
            }
            AddAmsPreview(*ams_info, ams_info->ams_type);
            AddAms(*ams_info, left);
            //AddExtraAms(*ams_info);
        }
        else if (ams_info->cans.size() == 1){
            AddAmsPreview(*ams_info, ams_info->ams_type);
            single_info.push_back(*ams_info);
            if (single_info.size() == 2){
                if (m_item_nums[0] <= m_item_nums[1]){
                    if (m_item_nums[0] == 0) left_init_mode = AMSRoadShowMode::AMS_ROAD_MODE_DOUBLE;
                    left = true;
                    m_item_ids[0].push_back(single_info[0].ams_id);
                    m_item_ids[0].push_back(single_info[1].ams_id);
                    m_item_nums[0]++;
                }
                else{
                    if (m_item_nums[1] == 0) right_init_mode = AMSRoadShowMode::AMS_ROAD_MODE_DOUBLE;
                    left = false;
                    m_item_ids[1].push_back(single_info[0].ams_id);
                    m_item_ids[1].push_back(single_info[1].ams_id);
                    m_item_nums[1]++;
                }
                pair_id.push_back(std::make_pair(single_info[0].ams_id, single_info[1].ams_id));
                AddAms(single_info, left);
                single_info.clear();
            }
        }
    }
    //ext_info.cans[0].material_colour =
    if (m_ext_info.size() <= 0){
        BOOST_LOG_TRIVIAL(trace) << "vt_slot empty!";
        return;
    }
    single_info.push_back(m_ext_info[0]);
    if (m_item_nums[0] <= m_item_nums[1]){
        if (m_item_nums[0] == 0){
            if (single_info.size() == 2){
                left_init_mode = AMSRoadShowMode::AMS_ROAD_MODE_DOUBLE;
            }
            else{
                left_init_mode = AMSRoadShowMode::AMS_ROAD_MODE_SINGLE;
            }
        }
        left = true;
        for (auto it : single_info){
            m_item_ids[0].push_back(it.ams_id);
        }
        m_item_nums[0]++;

    }
    else{
        if (m_item_nums[1] == 0){
            if (single_info.size() == 2){
                right_init_mode = AMSRoadShowMode::AMS_ROAD_MODE_DOUBLE;
            }
            else{
                right_init_mode = AMSRoadShowMode::AMS_ROAD_MODE_SINGLE;
            }
        }
        left = false;
        for (auto it : single_info){
            m_item_ids[1].push_back(it.ams_id);
        }
        m_item_nums[1]++;
    }
    //wait add
    if (single_info.size() == 2){
        pair_id.push_back(std::make_pair(single_info[0].ams_id, single_info[1].ams_id));
    }
    AddAmsPreview(m_ext_info[0], AMSModel::NO_AMS);
    AddAms(single_info, left);

    m_panel_items_left->Hide();
    if (!m_item_nums[0] || !m_item_nums[1]){
        /*m_simplebook_ams_right->Hide();
        m_panel_items_right->Hide();*/
        //m_simplebook_ams_left->Hide();
        m_simplebook_ams_right->Hide();
        m_panel_items_right->Hide();
        m_simplebook_ams_left->SetSelection(0);
        m_down_road->UpdateLeft(1, left_init_mode);
        m_down_road->UpdateRight(1, right_init_mode);
    }
    else {
        m_sizer_items_left->Layout();
        if (m_item_nums[0] <= 1 && m_item_nums[1] <= 1){
            m_panel_items_right->Hide();
        }
        else{
            m_panel_items_right->Show();
        }
        /*m_simplebook_ams_left->Show();
        m_simplebook_ams_right->Show();*/
        m_simplebook_ams_left->Show();
        m_simplebook_ams_right->Show();

        m_simplebook_ams_left->SetSelection(0);
        m_simplebook_ams_right->SetSelection(0);
        m_down_road->UpdateLeft(1, left_init_mode);
        m_down_road->UpdateRight(1, right_init_mode);
    }
    m_extruder->update(1);
    auto it = m_ams_item_list.begin();
    m_down_road->UpdatePassRoad("0", true, -1, it->second->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    if ((++it) != m_ams_item_list.end()){
        m_down_road->UpdatePassRoad("0", false, -1, it->second->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    }

    //Refresh();
    Thaw();
}


void AMSControl::Reset()
{
    /*auto caninfo0_0 = Caninfo{"0", "", *wxWHITE, AMSCanType::AMS_CAN_TYPE_NONE};
    auto caninfo0_1 = Caninfo{"1", "", *wxWHITE, AMSCanType::AMS_CAN_TYPE_NONE};
    auto caninfo0_2 = Caninfo{"2", "", *wxWHITE, AMSCanType::AMS_CAN_TYPE_NONE};
    auto caninfo0_3 = Caninfo{"3", "", *wxWHITE, AMSCanType::AMS_CAN_TYPE_NONE};

    AMSinfo ams1 = AMSinfo{"0", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}};
    AMSinfo ams2 = AMSinfo{"1", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}};
    AMSinfo ams3 = AMSinfo{"2", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}};
    AMSinfo ams4 = AMSinfo{"3", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}};

    std::vector<AMSinfo>           ams_info{ams1, ams2, ams3, ams4};
    std::vector<AMSinfo>::iterator it;*/

    /*Slic3r::DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (dev) {
        MachineObject* obj = dev->get_selected_machine();
        parse_object(obj);
    }

    UpdateAms(m_ams_info, true);
    m_current_show_ams  = "";
    m_current_ams       = "";
    m_current_select    = "";*/
}

void AMSControl::show_noams_mode()
{
    show_vams(true);
    //m_sizer_ams_tips->Show(true);

    /*if (m_ams_model == AMSModel::NO_AMS) {
        EnterNoneAMSMode();
    } else if(m_ams_model == AMSModel::GENERIC_AMS){
        EnterGenericAMSMode();
    } else if (m_ams_model == AMSModel::EXTRA_AMS) {
        EnterExtraAMSMode();
    }*/
    EnterGenericAMSMode();
}

void AMSControl::show_auto_refill(bool show)
{
    //m_img_ams_backup->Show(show);
    m_amswin->Layout();
    m_amswin->Fit();
}

void AMSControl::show_vams(bool show)
{
    //m_panel_virtual->Show(show);
    //m_vams_sizer->Show(show);
    //m_vams_extra_road->Show(show);
    /*m_extruder->has_ams(show);
    show_vams_kn_value(show);
    Layout();

    if (show && m_is_none_ams_mode) {
        if (m_current_ams == "") {
            wxMouseEvent event(wxEVT_LEFT_DOWN);
            event.SetEventObject(m_vams_lib);
            wxPostEvent(m_vams_lib, event);
        }
    }*/
}

void AMSControl::show_vams_kn_value(bool show)
{
    //m_vams_lib->show_kn_value(show);
}

void AMSControl::update_vams_kn_value(AmsTray tray, MachineObject* obj)
{
    //m_vams_lib->m_obj = obj;
    //if (obj->cali_version >= 0) {
    //    float k_value = 0;
    //    float n_value = 0;
    //    CalibUtils::get_pa_k_n_value_by_cali_idx(obj, tray.cali_idx, k_value, n_value);
    //    m_vams_info.k        = k_value;
    //    m_vams_info.n        = n_value;
    //    m_vams_lib->m_info.k = k_value;
    //    m_vams_lib->m_info.n = n_value;
    //}
    //else { // the remaining printer types
    //    m_vams_info.k        = tray.k;
    //    m_vams_info.n        = tray.n;
    //    m_vams_lib->m_info.k = tray.k;
    //    m_vams_lib->m_info.n = tray.n;
    //}
    //m_vams_info.material_name = tray.get_display_filament_type();
    //m_vams_info.material_colour = tray.get_color();
    //m_vams_lib->m_info.material_name = tray.get_display_filament_type();
    //auto col= tray.get_color();
    //if (col.Alpha() != 0 && col.Alpha() != 255 && col.Alpha() != 254 && m_vams_lib->m_info.material_colour != col) {
    //    m_vams_lib->transparent_changed = true;
    //}
    //m_vams_lib->m_info.material_colour = tray.get_color();
    //m_vams_lib->Refresh();
}

void AMSControl::reset_vams()
{
    /*m_vams_lib->m_info.k = 0;
    m_vams_lib->m_info.n = 0;
    m_vams_lib->m_info.material_name = wxEmptyString;
    m_vams_lib->m_info.material_colour = AMS_CONTROL_WHITE_COLOUR;
    m_vams_lib->m_info.cali_idx = -1;
    m_vams_lib->m_info.filament_id = "";
    m_vams_info.material_name = wxEmptyString;
    m_vams_info.material_colour = AMS_CONTROL_WHITE_COLOUR;
    m_vams_lib->Refresh();*/
}


void AMSControl::ReadExtInfo(MachineObject* obj) {
    m_ext_info.clear();
    if (!obj){
        return;
    }
    AMSinfo ext_info;
    for (auto slot : obj->vt_slot){
        ext_info.ams_id = slot.id;
        Caninfo can;
        can.can_id = std::to_string(0);
        can.material_name = slot.filament_setting_id;
        ext_info.cans.push_back(can);
        if (slot.id == std::to_string(VIRTUAL_TRAY_MAIN_ID)){
            ext_info.nozzle_id = 0;
        }
        else{
            ext_info.nozzle_id = 1;
        }
        ext_info.cans[0].material_state = AMSCanType::AMS_CAN_TYPE_VIRTUAL;
        ext_info.cans[0].material_colour = slot.decode_color(slot.color);
        ext_info.cans[0].material_remain = slot.remain;
        ext_info.cans[0].material_name = slot.type;

        m_ext_info.push_back(ext_info);
    }
}

void AMSControl::UpdateAms(std::vector<AMSinfo> ams_info, std::vector<AMSinfo>ext_info, std::string dev_id, bool is_reset, bool test)
{
    if (!test){
        /*std::string curr_ams_id = GetCurentAms();
        std::string curr_can_id = GetCurrentCan(curr_ams_id);*/

        int nozzle_num = ext_info.size();

        // update item
        bool fresh = false;
        if (m_ams_info.size() == ams_info.size() && m_nozzle_num == nozzle_num && m_dev_id == dev_id){
            for (int i = 0; i < m_ams_info.size(); i++){
                if (m_ams_info[i].ams_id != ams_info[i].ams_id){
                    fresh = true;
                }
            }
        }
        else{
            fresh = true;
        }
        m_ams_info.clear();
        m_ams_info = ams_info;
        m_ext_info.clear();
        m_ext_info = ext_info;
        m_nozzle_num = nozzle_num;
        m_dev_id = dev_id;
        if (fresh){
            //m_ams_generic_item_list.clear();
            for (auto it : m_ams_preview_list){
                delete it.second;
            }
            m_ams_preview_list.clear();
            ClearAms();
            m_left_page_index = 0;
            m_right_page_index = 0;
            if (m_nozzle_num >= 2){
                CreateAmsNew();
            }else{
                /*m_panel_items_right->ClearBackground();
                m_panel_items_left->ClearBackground();*/
                m_item_ids = { {}, {} };
                pair_id.clear();
                CreateAmsSingleNozzle();
            }
            /*m_amswin->Layout();
            m_amswin->Fit();
            */
            SetSize(wxSize(FromDIP(578), -1));
            SetMinSize(wxSize(FromDIP(578), -1));


        }
        // update cans

        for (auto ams_item : m_ams_item_list) {
            std::string ams_id = ams_item.second->m_info.ams_id;
            AmsItem* cans = ams_item.second;
            if (cans->m_info.ams_id == std::to_string(VIRTUAL_TRAY_MAIN_ID) || cans->m_info.ams_id == std::to_string(VIRTUAL_TRAY_DEPUTY_ID)){
                for (auto ifo : m_ext_info) {
                    if (ifo.ams_id == ams_id) {
                        cans->m_info = ifo;
                        cans->Update(ifo);
                        cans->show_sn_value(m_ams_model == AMSModel::EXTRA_AMS ? false : true);
                    }
                }
            }
            else{
                for (auto ifo : m_ams_info) {
                    if (ifo.ams_id == ams_id) {
                        cans->m_info = ifo;
                        cans->Update(ifo);
                        cans->show_sn_value(m_ams_model == AMSModel::EXTRA_AMS ? false : true);
                    }
                }
            }
        }

        /*if (m_current_show_ams.empty() && !is_reset) {
            if (ext_info.size() > 0) {
                SwitchAms(ext_info[0].ams_id);
            }
        }*/

        //m_simplebook_ams_left->SetSelection(m_simplebook_ams_left->m_first);
    }
    else
    {
        static bool first_time = true;
        static std::vector<AMSinfo>ams_info;
        int nozzle_num = 2;
        if (first_time)
        {
            auto caninfo0_0 = Caninfo{ "def_can_0", (""), *wxRED, AMSCanType::AMS_CAN_TYPE_VIRTUAL };
            auto caninfo0_1 = Caninfo{ "def_can_1", (""), *wxGREEN, AMSCanType::AMS_CAN_TYPE_VIRTUAL };
            auto caninfo0_2 = Caninfo{ "def_can_2", (""), *wxBLUE, AMSCanType::AMS_CAN_TYPE_VIRTUAL };
            auto caninfo0_3 = Caninfo{ "def_can_3", (""), *wxYELLOW, AMSCanType::AMS_CAN_TYPE_VIRTUAL };

            auto caninfo1_0 = Caninfo{ "def_can_0", (""), wxColour(255, 255, 0), AMSCanType::AMS_CAN_TYPE_VIRTUAL };
            auto caninfo1_1 = Caninfo{ "def_can_1", (""), wxColour(255, 0, 255), AMSCanType::AMS_CAN_TYPE_VIRTUAL };
            auto caninfo1_2 = Caninfo{ "def_can_2", (""), wxColour(0, 255, 255), AMSCanType::AMS_CAN_TYPE_VIRTUAL };
            auto caninfo1_3 = Caninfo{ "def_can_3", (""), wxColour(200, 80, 150), AMSCanType::AMS_CAN_TYPE_VIRTUAL };

            AMSinfo                        ams1 = AMSinfo{ "0", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}, 0 };
            AMSinfo                        ams2 = AMSinfo{ "1", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}, 0 };
            AMSinfo                        ams3 = AMSinfo{ "2", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}, 0 };
            AMSinfo                        ams4 = AMSinfo{ "3", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}, 0 };

            AMSinfo                        singleams1 = AMSinfo{ "0", std::vector<Caninfo>{caninfo0_0}, 0 };
            AMSinfo                        singleams2 = AMSinfo{ "1", std::vector<Caninfo>{caninfo0_1}, 0 };
            AMSinfo                        singleams3 = AMSinfo{ "2", std::vector<Caninfo>{caninfo0_2}, 0 };
            AMSinfo                        singleams4 = AMSinfo{ "3", std::vector<Caninfo>{caninfo0_3}, 0 };

            AMSinfo                        ams5 = AMSinfo{ "4", std::vector<Caninfo>{caninfo1_0, caninfo1_1, caninfo1_2, caninfo1_3}, 1 };
            AMSinfo                        ams6 = AMSinfo{ "5", std::vector<Caninfo>{caninfo1_0, caninfo1_1, caninfo1_2, caninfo1_3}, 1 };
            AMSinfo                        ams7 = AMSinfo{ "6", std::vector<Caninfo>{caninfo1_0, caninfo1_1, caninfo1_2, caninfo1_3}, 1 };
            AMSinfo                        ams8 = AMSinfo{ "7", std::vector<Caninfo>{caninfo1_0, caninfo1_1, caninfo1_2, caninfo1_3}, 1 };

            AMSinfo                        singleams5 = AMSinfo{ "4", std::vector<Caninfo>{caninfo1_0}, 1 };
            AMSinfo                        singleams6 = AMSinfo{ "5", std::vector<Caninfo>{caninfo1_1}, 1 };
            AMSinfo                        singleams7 = AMSinfo{ "6", std::vector<Caninfo>{caninfo1_2}, 1 };
            AMSinfo                        singleams8 = AMSinfo{ "7", std::vector<Caninfo>{caninfo1_3}, 1 };
            std::vector<AMSinfo>generic_ams = { ams1, ams2, ams3, ams4, ams5, ams6, ams7, ams8 };
            std::vector<AMSinfo>single_ams = { singleams1, singleams2, singleams3, singleams4, singleams5, singleams6, singleams7, singleams8 };
            ams_info = { ams1, singleams1, ams3, singleams3, ams5, singleams5, ams7, singleams7 };
            first_time = false;
        }

        Freeze();

        // update item
        bool fresh = false;
        if (m_ams_info.size() == ams_info.size() && m_nozzle_num == nozzle_num){
            for (int i = 0; i < m_ams_info.size(); i++)
            {
                if (m_ams_info[i].ams_id != ams_info[i].ams_id){
                    fresh = true;
                }
            }
        }
        else{
            fresh = true;
        }
        m_ams_info.clear();
        m_ams_info = ams_info;
        m_ext_info.clear();
        m_ext_info.push_back(ext_info[0]);
        m_ext_info.push_back(ext_info[0]);
        m_ext_info[0].ams_id = std::to_string(VIRTUAL_TRAY_MAIN_ID);
        m_ext_info[1].ams_id = std::to_string(VIRTUAL_TRAY_DEPUTY_ID);
        m_nozzle_num = nozzle_num;
        if (fresh){

            for (auto it : m_ams_preview_list) {
                delete it.second;
            }
            m_ams_preview_list.clear();
            ClearAms();
            m_left_page_index = 0;
            m_right_page_index = 0;
            if (m_nozzle_num >= 2) {
                CreateAmsNew();
            }
            else {
                /*m_panel_items_right->ClearBackground();
                m_panel_items_left->ClearBackground();*/
                m_item_ids = { {}, {} };
                pair_id.clear();
                CreateAmsSingleNozzle();
            }
            /*m_amswin->Layout();
            m_amswin->Fit();
            */
            SetSize(wxSize(FromDIP(578), -1));
            SetMinSize(wxSize(FromDIP(578), -1));
        }
        Thaw();

        // update cans

        for (auto ams_item : m_ams_item_list) {
            std::string ams_id = ams_item.first;
            AmsItem* cans = ams_item.second;
            if (atoi(cans->m_info.ams_id.c_str()) >= VIRTUAL_TRAY_DEPUTY_ID) {
                for (auto ifo : m_ext_info) {
                    if (ifo.ams_id == ams_id) {
                        cans->m_info = ifo;
                        cans->Update(ifo);
                        cans->show_sn_value(m_ams_model == AMSModel::EXTRA_AMS ? false : true);
                    }
                }
            }
            else {
                for (auto ifo : m_ams_info) {
                    if (ifo.ams_id == ams_id) {
                        cans->m_info = ifo;
                        cans->Update(ifo);
                        cans->show_sn_value(m_ams_model == AMSModel::EXTRA_AMS ? false : true);
                    }
                }
            }
        }

        /*if (m_current_show_ams.empty() && !is_reset) {
            if (ams_info.size() > 0) {
                SwitchAms(ams_info[0].ams_id);
            }
        }*/
    }


}

void AMSControl::AddAmsPreview(AMSinfo info, AMSModel type)
{
    AMSPreview *ams_prv = nullptr;

    if (info.nozzle_id == 0)
    {
        ams_prv = new AMSPreview(m_panel_items_right, wxID_ANY, info, type);
        m_sizer_items_right->Add(ams_prv, 0, wxALIGN_CENTER | wxRIGHT, 6);
    }
    else if (info.nozzle_id == 1)
    {
        ams_prv = new AMSPreview(m_panel_items_left, wxID_ANY, info, type);
        m_sizer_items_left->Add(ams_prv, 0, wxALIGN_CENTER | wxRIGHT, 6);
    }
    ams_prv->Bind(wxEVT_LEFT_DOWN, [this, ams_prv](wxMouseEvent& e) {
        SwitchAms(ams_prv->m_amsinfo.ams_id);
        e.Skip();
        });

    m_ams_preview_list[info.ams_id] = ams_prv;

}

void AMSControl::AddAms(AMSinfo info, bool left)
{
    AmsItem* ams_item;
    if (m_nozzle_num > 1)
    {
        if (info.nozzle_id == 0)
        {
            ams_item = new AmsItem(m_simplebook_ams_right, info, info.ams_type);
            //m_simplebook_ams_right->RemovePage(m_right_page_index);
            m_simplebook_ams_right->InsertPage(m_right_page_index, ams_item, wxEmptyString, true);
            ams_item->m_selection = m_right_page_index;
            m_right_page_index++;
        }
        else if (info.nozzle_id == 1)
        {
            ams_item = new AmsItem(m_simplebook_ams_left, info, info.ams_type);
            //m_simplebook_ams_left->RemovePage(m_left_page_index);
            m_simplebook_ams_left->InsertPage(m_left_page_index, ams_item, wxEmptyString, true);
            ams_item->m_selection = m_left_page_index;
            m_left_page_index++;
        }
    }
    else if (m_nozzle_num == 1)
    {
        if (left)
        {
            ams_item = new AmsItem(m_simplebook_ams_left, info, info.ams_type);
            //m_simplebook_ams_left->RemovePage(m_left_page_index);
            m_simplebook_ams_left->InsertPage(m_left_page_index, ams_item, wxEmptyString, true);
            ams_item->m_selection = m_left_page_index;
            m_left_page_index++;
        }
        else {
            ams_item = new AmsItem(m_simplebook_ams_right, info, info.ams_type);
            //m_simplebook_ams_right->RemovePage(m_right_page_index);
            m_simplebook_ams_right->InsertPage(m_right_page_index, ams_item, wxEmptyString, true);
            ams_item->m_selection = m_right_page_index;
            m_right_page_index++;
            //if (m_item_nums[1] == 1) m_simplebook_ams_right->SetSelection(m_simplebook_ams_left->GetSelection());
        }
    }
    m_ams_item_list[info.ams_id] = ams_item;
    m_simplebook_ams_left->Layout();
    m_simplebook_ams_right->Layout();
    m_simplebook_ams_left->Refresh();
    m_simplebook_ams_right->Refresh();

}

//void AMSControl::AddExtraAms(AMSinfo info)
//{
//    auto ams_item = new AmsItem(m_simplebook_extra_cans_left, info, AMSModel::EXTRA_AMS);
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

void AMSControl::AddAms(std::vector<AMSinfo>single_info, bool left) {
    AmsItem* ams_item;
    AMSModel mode;
    wxBoxSizer *sizer = new wxBoxSizer(wxHORIZONTAL);

    if (single_info.size() == 0){
        return;
    }
    else if (single_info.size() == 1){
        mode = AMSModel::NO_AMS;
        int w = 30;
        if (m_nozzle_num == 2)
        {
            if (single_info[0].nozzle_id == 0)
            {
                wxPanel*  book_panel = new wxPanel(m_simplebook_ams_right);
                book_panel->SetBackgroundColour(AMS_CONTROL_DEF_LIB_BK_COLOUR);
                ams_item = new AmsItem(book_panel, single_info[0], mode);
                sizer->Add(ams_item, 0, wxLEFT, FromDIP(30));
                book_panel->SetSize(wxSize(FromDIP(264), FromDIP(150)));
                book_panel->SetMinSize(wxSize(FromDIP(264), FromDIP(150)));
                book_panel->SetSizer(sizer);
                book_panel->Layout();
                book_panel->Fit();
                //m_simplebook_ams_right->RemovePage(m_right_page_index);
                m_simplebook_ams_right->InsertPage(m_right_page_index, book_panel, wxEmptyString, true);
                ams_item->m_selection = m_right_page_index;
                m_right_page_index++;
            }
            else if (single_info[0].nozzle_id == 1)
            {
                wxPanel*  book_panel = new wxPanel(m_simplebook_ams_left);
                book_panel->SetBackgroundColour(AMS_CONTROL_DEF_LIB_BK_COLOUR);
                ams_item = new AmsItem(book_panel, single_info[0], mode);
                sizer->Add(ams_item, 0, wxLEFT, FromDIP(30));
                book_panel->SetSize(wxSize(FromDIP(264), FromDIP(150)));
                book_panel->SetMinSize(wxSize(FromDIP(264), FromDIP(150)));
                book_panel->SetSizer(sizer);
                book_panel->Layout();
                book_panel->Fit();
                //m_simplebook_ams_left->RemovePage(m_left_page_index);
                m_simplebook_ams_left->InsertPage(m_left_page_index, book_panel, wxEmptyString, true);
                ams_item->m_selection = m_left_page_index;
                m_left_page_index++;
            }
        }
        else if (m_nozzle_num == 1)
        {
            if (!left)
            {
                wxPanel*  book_panel = new wxPanel(m_simplebook_ams_right);
                book_panel->SetBackgroundColour(AMS_CONTROL_DEF_LIB_BK_COLOUR);
                ams_item = new AmsItem(book_panel, single_info[0], mode);
                sizer->Add(ams_item, 0, wxLEFT, FromDIP(30));
                book_panel->SetSize(wxSize(FromDIP(264), FromDIP(150)));
                book_panel->SetMinSize(wxSize(FromDIP(264), FromDIP(150)));
                book_panel->SetSizer(sizer);
                book_panel->Layout();
                book_panel->Fit();
                //m_simplebook_ams_right->RemovePage(m_right_page_index);
                m_simplebook_ams_right->InsertPage(m_right_page_index, book_panel, wxEmptyString, true);
                ams_item->m_selection = m_right_page_index;
                m_right_page_index++;
            }
            else
            {
                wxPanel* book_panel = new wxPanel(m_simplebook_ams_left);
                book_panel->SetBackgroundColour(AMS_CONTROL_DEF_LIB_BK_COLOUR);
                ams_item = new AmsItem(book_panel, single_info[0], mode);
                sizer->Add(ams_item, 0, wxLEFT, FromDIP(30));
                book_panel->SetSize(wxSize(FromDIP(264), FromDIP(150)));
                book_panel->SetMinSize(wxSize(FromDIP(264), FromDIP(150)));
                book_panel->SetSizer(sizer);
                book_panel->Layout();
                book_panel->Fit();
                //m_simplebook_ams_left->RemovePage(m_left_page_index);
                m_simplebook_ams_left->InsertPage(m_left_page_index, book_panel, wxEmptyString, true);
                ams_item->m_selection = m_left_page_index;
                m_left_page_index++;
            }
        }

        ams_item->SetBackgroundColour(AMS_CONTROL_DEF_LIB_BK_COLOUR);
        m_ams_item_list[single_info[0].ams_id] = ams_item;
    }
    else if (single_info.size() == 2)
    {
        AmsItem* ext_item;
        wxBoxSizer* book_sizer = new wxBoxSizer(wxVERTICAL);
        if (single_info[1].ams_id == std::to_string(VIRTUAL_TRAY_MAIN_ID) || single_info[1].ams_id == std::to_string(VIRTUAL_TRAY_DEPUTY_ID))
        {
            mode = AMSModel::NO_AMS;
        }
        else
        {
            mode = AMSModel::SINGLE_AMS;
        }
        if (m_nozzle_num == 2)
        {
            if (single_info[1].nozzle_id == 0)
            {
                wxPanel* book_panel = new wxPanel(m_simplebook_ams_right);
                book_panel->SetBackgroundColour(AMS_CONTROL_DEF_LIB_BK_COLOUR);
                book_panel->SetSize(wxSize(FromDIP(264), FromDIP(150)));
                book_panel->SetMinSize(wxSize(FromDIP(264), FromDIP(150)));
                ams_item = new AmsItem(book_panel, single_info[0], AMSModel::SINGLE_AMS);
                ext_item = new AmsItem(book_panel, single_info[1], mode);
                book_sizer->Add(ams_item);
                book_sizer->Add(ext_item);
                book_panel->SetSizer(book_sizer);
                book_panel->Layout();
                book_panel->Fit();
                //m_simplebook_ams_right->RemovePage(m_right_page_index);
                m_simplebook_ams_right->InsertPage(m_right_page_index, book_panel, wxEmptyString, true);
                ams_item->m_selection = m_right_page_index;
                ext_item->m_selection = m_right_page_index;
                m_right_page_index++;
            }
            else if (single_info[1].nozzle_id == 1)
            {
                wxPanel* book_panel = new wxPanel(m_simplebook_ams_left);
                book_panel->SetBackgroundColour(AMS_CONTROL_DEF_LIB_BK_COLOUR);
                book_panel->SetSize(wxSize(FromDIP(264), FromDIP(150)));
                book_panel->SetMinSize(wxSize(FromDIP(264), FromDIP(150)));
                ams_item = new AmsItem(book_panel, single_info[0], AMSModel::SINGLE_AMS);
                ext_item = new AmsItem(book_panel, single_info[1], mode);
                book_sizer->Add(ams_item);
                book_sizer->Add(ext_item);
                book_panel->SetSizer(book_sizer);
                //m_simplebook_ams_left->RemovePage(m_left_page_index);
                m_simplebook_ams_left->InsertPage(m_left_page_index, book_panel, wxEmptyString, true);
                ams_item->m_selection = m_left_page_index;
                ext_item->m_selection = m_left_page_index;
                m_left_page_index++;
            }
        }
        else {
            if (!left)
            {
                wxPanel* book_panel = new wxPanel(m_simplebook_ams_right);
                book_panel->SetBackgroundColour(AMS_CONTROL_DEF_LIB_BK_COLOUR);
                book_panel->SetSize(wxSize(FromDIP(264), FromDIP(150)));
                book_panel->SetMinSize(wxSize(FromDIP(264), FromDIP(150)));
                ams_item = new AmsItem(book_panel, single_info[0], AMSModel::SINGLE_AMS);
                ext_item = new AmsItem(book_panel, single_info[1], mode);
                book_sizer->Add(ams_item);
                book_sizer->Add(ext_item);
                book_panel->SetSizer(book_sizer);
                book_panel->Layout();
                book_panel->Fit();
                //m_simplebook_ams_right->RemovePage(m_right_page_index);
                m_simplebook_ams_right->InsertPage(m_right_page_index, book_panel, wxEmptyString, true);
                ams_item->m_selection = m_right_page_index;
                ext_item->m_selection = m_right_page_index;
                m_right_page_index++;
            }
            else
            {
                wxPanel* book_panel = new wxPanel(m_simplebook_ams_left);
                book_panel->SetBackgroundColour(AMS_CONTROL_DEF_LIB_BK_COLOUR);
                book_panel->SetSize(wxSize(FromDIP(264), FromDIP(150)));
                book_panel->SetMinSize(wxSize(FromDIP(264), FromDIP(150)));
                ams_item = new AmsItem(book_panel, single_info[0], AMSModel::SINGLE_AMS);
                ext_item = new AmsItem(book_panel, single_info[1], mode);
                book_sizer->Add(ams_item);
                book_sizer->Add(ext_item);
                book_panel->SetSizer(book_sizer);
                //m_simplebook_ams_left->RemovePage(m_left_page_index);
                m_simplebook_ams_left->InsertPage(m_left_page_index, book_panel, wxEmptyString, true);
                ams_item->m_selection = m_left_page_index;
                ext_item->m_selection = m_left_page_index;
                m_left_page_index++;
            }
        }

        m_ams_item_list[single_info[0].ams_id] = ams_item;
        m_ams_item_list[single_info[1].ams_id] = ext_item;
    }
    m_simplebook_ams_left->Layout();
    m_simplebook_ams_right->Layout();
    m_simplebook_ams_left->Refresh();
    m_simplebook_ams_right->Refresh();
}

//void AMSControl::AddExtAms(int ams_id) {
//    if (m_ams_item_list.find(std::to_string(ams_id)) != m_ams_item_list.end())
//    {
//        //mode = AMSModel::EXTRA_AMS;
//        AmsItem* ams_item;
//        AMSinfo ext_info;
//
//        if (ams_id == VIRTUAL_TRAY_MAIN_ID)
//        {
//            ext_info.ams_id = std::to_string(VIRTUAL_TRAY_MAIN_ID);
//            ext_info.nozzle_id = 0;
//            ams_item = new AmsItem(m_simplebook_ams_right, ext_info, AMSModel::EXTRA_AMS);
//            m_simplebook_ams_right->AddPage(ams_item, wxEmptyString, false);
//            ams_item->m_selection = m_simplebook_ams_right->GetPageCount() - 1;
//        }
//        else if (ams_id == VIRTUAL_TRAY_DEPUTY_ID)
//        {
//            ext_info.ams_id = std::to_string(VIRTUAL_TRAY_DEPUTY_ID);
//            ext_info.nozzle_id = 1;
//            ams_item = new AmsItem(m_simplebook_ams_left, ext_info, AMSModel::EXTRA_AMS);
//            m_simplebook_ams_left->AddPage(ams_item, wxEmptyString, false);
//            ams_item->m_selection = m_simplebook_ams_left->GetPageCount() - 1;
//        }
//        m_ams_generic_item_list[std::to_string(ams_id)] = ams_item;
//    }
//}

void AMSControl::AddAmsPreview(std::vector<AMSinfo>single_info) {

}

void AMSControl::SwitchAms(std::string ams_id)
{
    if(ams_id == m_current_show_ams_left || ams_id == m_current_show_ams_right){return;}

    bool is_in_right = IsAmsInRightPanel(ams_id);
    if (is_in_right){
        if (m_current_show_ams_right != ams_id) {
            m_current_show_ams_right = ams_id;
            m_extruder->OnAmsLoading(false);
        }
    }
    else{
        m_current_show_ams_left = ams_id;
        m_extruder->OnAmsLoading(false);
        if (m_nozzle_num > 1) m_extruder->OnAmsLoading(false, 1);
    }


    for (auto prv_it : m_ams_preview_list) {
        AMSPreview* prv = prv_it.second;
        if (prv->m_amsinfo.ams_id == m_current_show_ams_left || prv->m_amsinfo.ams_id == m_current_show_ams_right) {
            prv->OnSelected();
            m_current_select = ams_id;

            bool ready_selected = false;
            for (auto item_it : m_ams_item_list) {
                AmsItem* item = item_it.second;
                if (item->m_info.ams_id == ams_id) {
                    for (auto lib_it : item->m_can_lib_list) {
                        AMSLib* lib = lib_it.second;
                        if (lib->is_selected()) {
                            ready_selected = true;
                        }
                    }
                }
            }
            if (is_in_right){
                m_current_ams_right = ams_id;
            }
            else{
                m_current_ams_left = ams_id;
            }

        } else {
            prv->UnSelected();
        }

        if (prv->m_amsinfo.nozzle_id == 1) {
            m_sizer_items_left->Layout();
            m_panel_items_left->Fit();
        }
        else if (prv->m_amsinfo.nozzle_id == 0)
        {
            m_sizer_items_right->Layout();
            m_panel_items_right->Fit();
        }

    }

    for (auto ams_item : m_ams_item_list) {
        AmsItem* item = ams_item.second;
        if (item->m_info.ams_id == ams_id) {
            if (m_nozzle_num == 2) {
                if (item->m_info.nozzle_id == 1){
                    m_simplebook_ams_left->SetSelection(item->m_selection);
                    if (item->m_info.cans.size() == 4){
                        m_down_road->UpdateLeft(m_nozzle_num, AMSRoadShowMode::AMS_ROAD_MODE_FOUR);
                    }
                    else {
                        AMSRoadShowMode mode = AMSRoadShowMode::AMS_ROAD_MODE_SINGLE;
                        for (auto it : pair_id){
                            if (it.first == ams_id || it.second == ams_id){
                                mode = AMSRoadShowMode::AMS_ROAD_MODE_DOUBLE;
                                break;
                            }
                        }
                        m_down_road->UpdateLeft(m_nozzle_num, mode);
                        m_down_road->UpdatePassRoad(item->m_info.current_can_id, true, -1, item->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
                    }
                }
                else if (item->m_info.nozzle_id == 0){
                    m_simplebook_ams_right->SetSelection(item->m_selection);
                    if (item->m_info.cans.size() == 4){
                        m_down_road->UpdateRight(m_nozzle_num, AMSRoadShowMode::AMS_ROAD_MODE_FOUR);
                    }
                    else {
                        AMSRoadShowMode mode = AMSRoadShowMode::AMS_ROAD_MODE_SINGLE;
                        for (auto it : pair_id){
                            if (it.first == ams_id || it.second == ams_id){
                                mode = AMSRoadShowMode::AMS_ROAD_MODE_DOUBLE;
                                break;
                            }
                        }
                        m_down_road->UpdateRight(m_nozzle_num, mode);
                        m_down_road->UpdatePassRoad(item->m_info.current_can_id, false, -1, item->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
                    }
                }

            }
            else if (m_nozzle_num == 1) {
                for (auto id : m_item_ids[0]){
                    if (id == item->m_info.ams_id){
                        m_simplebook_ams_left->SetSelection(item->m_selection);
                        if (item->m_info.cans.size() == 4){
                            m_down_road->UpdateLeft(m_nozzle_num, AMSRoadShowMode::AMS_ROAD_MODE_FOUR);
                        }
                        else {
                            AMSRoadShowMode mode = AMSRoadShowMode::AMS_ROAD_MODE_SINGLE;
                            for (auto it : pair_id){
                                if (it.first == ams_id || it.second == ams_id){
                                    mode = AMSRoadShowMode::AMS_ROAD_MODE_DOUBLE;
                                    break;
                                }
                            }
                            m_down_road->UpdateLeft(m_nozzle_num, mode);
                            m_down_road->UpdatePassRoad(item->m_info.current_can_id, true, -1, item->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
                        }
                    }
                }
                for (auto id : m_item_ids[1])
                {
                    if (id == item->m_info.ams_id){
                        m_simplebook_ams_right->SetSelection(item->m_selection);
                        if (item->m_info.cans.size() == 4){
                            m_down_road->UpdateRight(m_nozzle_num, AMSRoadShowMode::AMS_ROAD_MODE_FOUR);
                        }
                        else {
                            AMSRoadShowMode mode = AMSRoadShowMode::AMS_ROAD_MODE_SINGLE;
                            for (auto it : pair_id)
                            {
                                if (it.first == ams_id || it.second == ams_id){
                                    mode = AMSRoadShowMode::AMS_ROAD_MODE_DOUBLE;
                                    break;
                                }
                            }
                            m_down_road->UpdateRight(m_nozzle_num, mode);
                            m_down_road->UpdatePassRoad(item->m_info.current_can_id, false, -1, item->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
                        }
                    }
                }
            }
        }
    }

    //update extruder
    for (auto i = 0; i < m_ams_info.size(); i++) {
        if (m_ams_info[i].ams_id == ams_id) {
            switch (m_ams_info[i].current_step) {
            case AMSPassRoadSTEP::AMS_ROAD_STEP_NONE: m_extruder->TurnOff(); break;

            case AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP1: m_extruder->TurnOff(); break;

            case AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP2: m_extruder->TurnOn(GetCanColour(ams_id, m_ams_info[i].current_can_id)); break;

            case AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP3: m_extruder->TurnOn(GetCanColour(ams_id, m_ams_info[i].current_can_id)); break;
            }
            SetAmsStep(ams_id, m_ams_info[i].current_can_id, AMSPassRoadType::AMS_ROAD_TYPE_LOAD, m_ams_info[i].current_step);
        }
    }
    for (auto i = 0; i < m_ext_info.size(); i++) {
        if (m_ext_info[i].ams_id == ams_id) {
            SetAmsStep(ams_id, "0", AMSPassRoadType::AMS_ROAD_TYPE_LOAD, m_ext_info[i].current_step);
        }
    }
}

void AMSControl::SetFilamentStep(int item_idx, FilamentStepType f_type)
{/*
    wxString FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_COUNT] = {
        _L("Idling..."),
        _L("Heat the nozzle"),
        _L("Cut filament"),
        _L("Pull back current filament"),
        _L("Push new filament into extruder"),
        _L("Purge old filament"),
        _L("Feed Filament"),
        _L("Confirm extruded"),
        _L("Check filament location")
    };


    if (item_idx == FilamentStep::STEP_IDLE) {
        m_simplebook_right->SetSelection(0);
        m_filament_load_step->Idle();
        m_filament_unload_step->Idle();
        m_filament_vt_load_step->Idle();
        return;
    }

    wxString step_str = wxEmptyString;
    if (item_idx < FilamentStep::STEP_COUNT) {
        step_str = FILAMENT_CHANGE_STEP_STRING[item_idx];
    }

    if (f_type == FilamentStepType::STEP_TYPE_LOAD) {
        if (item_idx > 0 && item_idx < FilamentStep::STEP_COUNT) {
            if (m_simplebook_right->GetSelection() != 1) {
                m_simplebook_right->SetSelection(1);
            }

            m_filament_load_step->SelectItem( m_filament_load_step->GetItemUseText(step_str) );
        } else {
            m_filament_load_step->Idle();
        }
    } else if (f_type == FilamentStepType::STEP_TYPE_UNLOAD) {
        if (item_idx > 0 && item_idx < FilamentStep::STEP_COUNT) {
            if (m_simplebook_right->GetSelection() != 2) {
                m_simplebook_right->SetSelection(2);
            }
            m_filament_unload_step->SelectItem( m_filament_unload_step->GetItemUseText(step_str) );
        }
        else {
            m_filament_unload_step->Idle();
        }
    } else if (f_type == FilamentStepType::STEP_TYPE_VT_LOAD) {
        m_simplebook_right->SetSelection(3);
        if (item_idx > 0 && item_idx < FilamentStep::STEP_COUNT) {
            if (item_idx == STEP_CONFIRM_EXTRUDED) {
                m_filament_vt_load_step->SelectItem(2);
            }
            else {
                m_filament_vt_load_step->SelectItem( m_filament_vt_load_step->GetItemUseText(step_str) );
            }
        }
        else {
            m_filament_vt_load_step->Idle();
        }
    } else {
        if (item_idx > 0 && item_idx < FilamentStep::STEP_COUNT) {
            m_simplebook_right->SetSelection(1);
            m_filament_load_step->SelectItem( m_filament_load_step->GetItemUseText(step_str) );
        }
        else {
            m_filament_load_step->Idle();
        }
    }*/
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

void AMSControl::SetExtruder(bool on_off, bool is_vams, std::string ams_now, wxColour col)
{
    AmsItem* item = nullptr;
    if (m_ams_item_list.find(ams_now) != m_ams_item_list.end()){
        item = m_ams_item_list[ams_now];
    }
    if (m_ams_model == AMSModel::GENERIC_AMS || m_ext_model == AMSModel::GENERIC_AMS ) {
        if (!on_off) {
            m_extruder->TurnOff();
            m_vams_extra_road->OnVamsLoading(false);
            m_extruder->OnVamsLoading(false);
            m_vams_road->OnVamsLoading(false);
        }
        else {
            m_extruder->TurnOn(col);
            if (item){
                if (ams_now != GetCurentShowAms()) {
                    m_extruder->OnAmsLoading(false, item->m_info.nozzle_id, col);
                }
                else {
                    m_extruder->OnAmsLoading(true, item->m_info.nozzle_id, col);
                }
            }
        }

        /*if (is_vams && on_off) {
            m_extruder->OnAmsLoading(false);
            m_vams_extra_road->OnVamsLoading(true, col);
            m_extruder->OnVamsLoading(true, col);
            m_vams_road->OnVamsLoading(true, col);
        }
        else {
            m_vams_extra_road->OnVamsLoading(false);
            m_extruder->OnVamsLoading(false);
            m_vams_road->OnVamsLoading(false);
        }*/
    }
    else if (m_ams_model == AMSModel::EXTRA_AMS || m_ext_model == AMSModel::EXTRA_AMS) {
        if (!on_off) {
            m_extruder->TurnOff();
            m_extruder->OnAmsLoading(false);
            /*m_vams_extra_road->OnVamsLoading(false);
            m_vams_road->OnVamsLoading(false);*/
        }
        else {
            m_extruder->TurnOn(col);
            m_extruder->OnAmsLoading(true, item->m_info.nozzle_id, col);
        }
    }
}

//void AMSControl::SetAmsStep(std::string ams_id, std::string canid, AMSPassRoadType type, AMSPassRoadSTEP step)
//{
//    AmsItem *cans = nullptr;
//    auto cansit = m_ams_item_list.find(ams_id);
//    bool           notfound = true;
//
//
//    if (cansit != m_ams_item_list.end()) {
//        cans =  cansit->second;
//    }
//    else {
//        notfound = false;
//    }
//
//
//
//    if (ams_id != m_last_ams_id || m_last_tray_id != canid) {
//        SetAmsStep(m_last_ams_id, m_last_tray_id, AMSPassRoadType::AMS_ROAD_TYPE_UNLOAD, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
//        //m_down_road->UpdatePassRoad(m_last_ams_id, m_last_tray_id, AMSPassRoadType::AMS_ROAD_TYPE_UNLOAD, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
//        m_vams_extra_road->OnVamsLoading(false);
//        m_extruder->OnVamsLoading(false);
//        m_vams_road->OnVamsLoading(false);
//    }
//
//    if (notfound) return;
//    if (cans == nullptr) return;
//
//
//    m_last_ams_id = ams_id;
//    m_last_tray_id = canid;
//
//
//    if (m_ams_model == AMSModel::GENERIC_AMS) {
//        if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_NONE) {
//            //cans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
//            m_extruder->OnAmsLoading(false);
//        }
//
//        if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP1) {
//            //cans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_1);
//            m_extruder->OnAmsLoading(false);
//        }
//
//        if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP2) {
//            //cans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_1);
//            //cans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_2);
//            if (m_current_show_ams == ams_id) {
//                m_extruder->OnAmsLoading(true, cans->GetTagColr(canid));
//            }
//        }
//
//        if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP3) {
//            //cans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_1);
//            //cans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_2);
//            //cans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_3);
//            m_extruder->OnAmsLoading(true, cans->GetTagColr(canid));
//        }
//    }
//    else if (m_ams_model == AMSModel::EXTRA_AMS) {
//        //cans->SetAmsStepExtra(canid, type, step);
//        if (step != AMSPassRoadSTEP::AMS_ROAD_STEP_NONE) {
//            m_extruder->OnAmsLoading(true, cans->GetTagColr(canid));
//        }
//        else {
//            m_extruder->OnAmsLoading(false);
//        }
//    }
//
//    for (auto i = 0; i < m_ams_info.size(); i++) {
//        if (m_ams_info[i].ams_id == ams_id) {
//            m_ams_info[i].current_step   = step;
//            m_ams_info[i].current_can_id = canid;
//        }
//    }
//}


void AMSControl::SetAmsStep(std::string ams_id, std::string canid, AMSPassRoadType type, AMSPassRoadSTEP step)
{
    AmsItem* ams = nullptr;
    auto amsit = m_ams_item_list.find(ams_id);
    bool           notfound = false;

    if (amsit != m_ams_item_list.end()) {
        ams = amsit->second;
    }
    else {
        notfound = true;
    }

    //if (ams_id != m_last_ams_id || m_last_tray_id != canid) {
    //    m_down_road->UpdatePassRoad(m_last_ams_id, true, -1, ams->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    //    m_down_road->UpdatePassRoad(m_last_ams_id, false, -1, ams->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    //    //m_down_road->UpdatePassRoad(m_last_ams_id, m_last_tray_id, AMSPassRoadType::AMS_ROAD_TYPE_UNLOAD, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    //    //m_vams_extra_road->OnVamsLoading(false);
    //    m_extruder->OnVamsLoading(false);
    //    //m_vams_road->OnVamsLoading(false);
    //}

    if (notfound) return;
    if (ams == nullptr) return;

    m_last_ams_id = ams_id;
    m_last_tray_id = canid;
    auto model = AMSModel::EXTRA_AMS;

    bool left = !IsAmsInRightPanel(ams_id);

    int length = -1;

    if (ams->m_info.cans.size() == 4){
        length = left ? 135 : 149;
        model = ams->m_info.ams_type;
    }
    else if (ams->m_info.cans.size() == 1){
        for (auto it : pair_id){
            if (it.first == ams_id){
                length = left ? 150 : 50;
                break;
            }
            else if (it.second == ams_id){
                length = left ? 50 : 150;
                break;
            }
        }
        model = AMSModel::SINGLE_AMS;
    }
    if (model == AMSModel::EXTRA_AMS){
        length = left ? 150 : 50;
    }


    if (model == AMSModel::GENERIC_AMS || model == AMSModel::N3F_AMS || model == AMSModel::EXTRA_AMS) {
        if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_NONE) {
            //cans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
            m_down_road->UpdatePassRoad(canid, left, -1, ams->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
            ams->SetAmsStep(ams_id, canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
            m_extruder->OnAmsLoading(false, ams->m_info.nozzle_id);
        }

        if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP1) {
            m_down_road->UpdatePassRoad(canid, left, length, ams->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_1);
            ams->SetAmsStep(ams_id, canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_1);
            m_extruder->OnAmsLoading(false, ams->m_info.nozzle_id);
        }

        if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP2) {
            m_down_road->UpdatePassRoad(canid, left, length, ams->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_2);
            ams->SetAmsStep(ams_id, canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_2);
            if (m_current_show_ams_left == ams_id || m_current_show_ams_right == ams_id) {
                m_extruder->OnAmsLoading(true, ams->m_info.nozzle_id, ams->GetTagColr(canid));
            }
        }
        if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP3) {
            m_down_road->UpdatePassRoad(canid, left, length, ams->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_3);
            ams->SetAmsStep(ams_id, canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_3);
            m_extruder->OnAmsLoading(true, ams->m_info.nozzle_id, ams->GetTagColr(canid));
        }
    }
    else if(model == AMSModel::NO_AMS || model == AMSModel::SINGLE_AMS) {
        if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_NONE) {
            //cans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
            m_down_road->UpdatePassRoad(canid, left, -1, ams->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
            ams->SetAmsStep(ams_id, canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
            m_extruder->OnAmsLoading(false);
        }

        if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP1) {
            m_down_road->UpdatePassRoad(canid, left, length, ams->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_1);
            ams->SetAmsStep(ams_id, canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_1);
            m_extruder->OnAmsLoading(false);
        }

        if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP2) {
            m_down_road->UpdatePassRoad(canid, left, length, ams->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_2);
            ams->SetAmsStep(ams_id, canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_2);
            if (m_current_show_ams_left == ams_id || m_current_show_ams_right == ams_id) {
                m_extruder->OnAmsLoading(true, ams->m_info.nozzle_id, ams->GetTagColr(canid));
            }
        }

        if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP3) {
            m_down_road->UpdatePassRoad(canid, left, length, ams->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_3);
            ams->SetAmsStep(ams_id, canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_3);
            m_extruder->OnAmsLoading(true, ams->m_info.nozzle_id, ams->GetTagColr(canid));
        }
    }

    //if (m_ams_model == AMSModel::GENERIC_AMS) {
    //    if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_NONE) {
    //        //cans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    //        m_down_road->UpdatePassRoad(canid, true, -1, ams->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    //        m_down_road->UpdatePassRoad(canid, false, -1, ams->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    //        m_extruder->OnAmsLoading(false);
    //    }

    //    if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP1) {
    //        //cans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_1);
    //        m_extruder->OnAmsLoading(false);
    //    }

    //    if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP2) {
    //        //cans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_1);
    //        //cans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_2);
    //        if (m_current_show_ams == ams_id) {
    //            m_extruder->OnAmsLoading(true, cans->GetTagColr(canid));
    //        }
    //    }

    //    if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP3) {
    //        //cans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_1);
    //        //cans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_2);
    //        //cans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_3);
    //        m_extruder->OnAmsLoading(true, cans->GetTagColr(canid));
    //    }
    //}
    //else if (m_ams_model == AMSModel::EXTRA_AMS) {
    //    //cans->SetAmsStepExtra(canid, type, step);
    //    if (step != AMSPassRoadSTEP::AMS_ROAD_STEP_NONE) {
    //        m_extruder->OnAmsLoading(true, cans->GetTagColr(canid));
    //    }
    //    else {
    //        m_extruder->OnAmsLoading(false);
    //    }
    //}

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
}

void AMSControl::on_filament_load(wxCommandEvent &event)
{
    m_button_extruder_back->Disable();
    for (auto i = 0; i < m_ams_info.size(); i++) {
        if (m_ams_info[i].ams_id == m_current_ams_left || m_ams_info[i].ams_id == m_current_ams_right) { m_ams_info[i].current_action = AMSAction::AMS_ACTION_LOAD; }
    }
    post_event(SimpleEvent(EVT_AMS_LOAD));
}

void AMSControl::on_extrusion_cali(wxCommandEvent &event)
{
    for (auto i = 0; i < m_ams_info.size(); i++) {
        if (m_ams_info[i].ams_id == m_current_ams_left || m_ams_info[i].ams_id == m_current_ams_right) { m_ams_info[i].current_action = AMSAction::AMS_ACTION_CALI; }
    }
    post_event(SimpleEvent(EVT_AMS_EXTRUSION_CALI));
}

void AMSControl::on_filament_unload(wxCommandEvent &event)
{
    m_button_extruder_feed->Disable();
    for (auto i = 0; i < m_ams_info.size(); i++) {
        if (m_ams_info[i].ams_id == m_current_ams_left || m_ams_info[i].ams_id == m_current_ams_right) { m_ams_info[i].current_action = AMSAction::AMS_ACTION_UNLOAD; }
    }
    post_event(SimpleEvent(EVT_AMS_UNLOAD));
}

void AMSControl::auto_refill(wxCommandEvent& event)
{
    post_event(SimpleEvent(EVT_AMS_FILAMENT_BACKUP));
}

void AMSControl::on_ams_setting_click(wxMouseEvent &event)
{
    for (auto i = 0; i < m_ams_info.size(); i++) {
        if (m_ams_info[i].ams_id == m_current_ams_left || m_ams_info[i].ams_id == m_current_ams_right) { m_ams_info[i].current_action = AMSAction::AMS_ACTION_CALI; }
    }
    post_event(SimpleEvent(EVT_AMS_SETTINGS));
}

void AMSControl::parse_object(MachineObject* obj) {
    if (!obj || obj->amsList.size() == 0)
    {
        return;
    }
    m_ams_info.clear();
    for (auto ams : obj->amsList)
    {
        AMSinfo info;
        info.parse_ams_info(obj, ams.second);
        m_ams_info.push_back(info);
    }
}

void AMSControl::on_clibration_again_click(wxMouseEvent &event) { post_event(SimpleEvent(EVT_AMS_CLIBRATION_AGAIN)); }

void AMSControl::on_clibration_cancel_click(wxMouseEvent &event) { post_event(SimpleEvent(EVT_AMS_CLIBRATION_CANCEL)); }

void AMSControl::post_event(wxEvent &&event)
{
    event.SetEventObject(m_parent);
    wxPostEvent(m_parent, event);
}

}} // namespace Slic3r::GUI
