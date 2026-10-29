#include "CameraPopup.hpp"

#include "I18N.hpp"
#include "MediaPlayCtrl.h"
#include "Widgets/Label.hpp"
#include "libslic3r/Utils.hpp"
#include "BitmapCache.hpp"
#include "wxExtensions.hpp"
#include <wx/progdlg.h>
#include <wx/clipbrd.h>
#include <wx/dcgraph.h>
#include <wx/dcmemory.h>
#include "GUI_App.hpp"
#include <slic3r/GUI/StatusPanel.hpp>

#include "DeviceCore/DevManager.h"
#include "DeviceCore/DevStorage.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Slic3r {
namespace GUI {

wxIMPLEMENT_CLASS(CameraPopup, PopupWindow);

wxBEGIN_EVENT_TABLE(CameraPopup, PopupWindow)
    EVT_MOUSE_EVENTS(CameraPopup::OnMouse )
    EVT_SIZE(CameraPopup::OnSize)
    EVT_SET_FOCUS(CameraPopup::OnSetFocus )
    EVT_KILL_FOCUS(CameraPopup::OnKillFocus )
wxEND_EVENT_TABLE()

wxDEFINE_EVENT(EVT_VCAMERA_SWITCH, wxCommandEvent);

#define CAMERAPOPUP_CLICK_INTERVAL 20

const wxColour TEXT_COL = wxColour(43, 52, 54);

CameraPopup::CameraPopup(wxWindow *parent)
   : PopupWindow(parent, wxBORDER_NONE | wxPU_CONTAINS_CONTROLS)
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif
    m_panel = new wxScrolledWindow(this, wxID_ANY);
    m_panel->ShowScrollbars(wxSHOW_SB_NEVER, wxSHOW_SB_NEVER);
    m_panel->SetBackgroundColour(*wxWHITE);
    m_panel->SetMinSize(wxSize(FromDIP(180),-1));
    m_panel->Bind(wxEVT_MOTION, &CameraPopup::OnMouse, this);

    main_sizer = new wxBoxSizer(wxVERTICAL);
    auto make_row_grid = [this]() {
        wxFlexGridSizer* g = new wxFlexGridSizer(0, 2, 0, FromDIP(50));
        g->AddGrowableCol(0);
        g->SetFlexibleDirection(wxBOTH);
        g->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);
        return g;
    };
    wxFlexGridSizer* grid_recording = make_row_grid();
    wxFlexGridSizer* grid_per_cam   = make_row_grid();
    wxFlexGridSizer* grid_rest      = make_row_grid();

    //recording
    m_text_recording = new wxStaticText(m_panel, wxID_ANY, _L("Live Camera Recording"));
    m_text_recording->Wrap(-1);
    m_text_recording->SetFont(Label::Head_14);
    m_text_recording->SetForegroundColour(TEXT_COL);
    m_switch_recording = new SwitchButton(m_panel);

    //no-storage hint for Auto-record Monitoring
    m_text_recording_hint = new wxStaticText(m_panel, wxID_ANY, wxEmptyString);
    m_text_recording_hint->SetFont(Label::Body_14);
    m_text_recording_hint->SetForegroundColour(wxColour(0x90, 0x90, 0x90));
    m_text_recording_hint->Hide();
    m_wrap_hints.push_back({m_text_recording_hint, _L("External storage is not installed. Recording is not supported."), 30});

    //vcamera
    m_text_vcamera = new wxStaticText(m_panel, wxID_ANY, _L("Go Live"));
    m_text_vcamera->Wrap(-1);
    m_text_vcamera->SetFont(Label::Head_14);
    m_text_vcamera->SetForegroundColour(TEXT_COL);
    m_switch_vcamera = new SwitchButton(m_panel);

    //live camera recording
    m_text_live_cam_rec = new wxStaticText(m_panel, wxID_ANY, _L("Live Camera Recording"));
    m_text_live_cam_rec->Wrap(-1);
    m_text_live_cam_rec->SetFont(Label::Head_14);
    m_text_live_cam_rec->SetForegroundColour(TEXT_COL);
    m_switch_live_cam_rec = new SwitchButton(m_panel);
    m_text_live_cam_rec->Hide();
    m_switch_live_cam_rec->Hide();

    //visual enhancement camera recording
    m_text_visual_cam_rec = new wxStaticText(m_panel, wxID_ANY, _L("BirdsEye Camera Recording"));
    m_text_visual_cam_rec->Wrap(-1);
    m_text_visual_cam_rec->SetFont(Label::Head_14);
    m_text_visual_cam_rec->SetForegroundColour(TEXT_COL);
    m_switch_visual_cam_rec = new SwitchButton(m_panel);
    m_text_visual_cam_rec->Hide();
    m_switch_visual_cam_rec->Hide();

    //no-storage hint for per-camera recording (placed under BirdsEye Camera Recording)
    m_text_cam_rec_hint = new wxStaticText(m_panel, wxID_ANY, wxEmptyString);
    m_text_cam_rec_hint->SetFont(Label::Body_14);
    m_text_cam_rec_hint->SetForegroundColour(wxColour(0x90, 0x90, 0x90));
    m_text_cam_rec_hint->Hide();
    m_wrap_hints.push_back({m_text_cam_rec_hint, _L("External storage is not installed. Recording is not supported."), 30});

    grid_recording->Add(m_text_recording, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));
    grid_recording->Add(m_switch_recording, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT | wxALL, FromDIP(5));

    grid_per_cam->Add(m_text_live_cam_rec, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));
    grid_per_cam->Add(m_switch_live_cam_rec, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT | wxALL, FromDIP(5));
    grid_per_cam->Add(m_text_visual_cam_rec, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));
    grid_per_cam->Add(m_switch_visual_cam_rec, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT | wxALL, FromDIP(5));

    grid_rest->Add(m_text_vcamera, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));
    grid_rest->Add(m_switch_vcamera, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT | wxALL, FromDIP(5));

#if !BBL_RELEASE_TO_PUBLIC
    m_text_liveview_retry = new wxStaticText(m_panel, wxID_ANY, _L("Liveview Retry"));
    m_text_liveview_retry->Wrap(-1);
    m_text_liveview_retry->SetFont(Label::Head_14);
    m_text_liveview_retry->SetForegroundColour(TEXT_COL);
    m_switch_liveview_retry = new SwitchButton(m_panel);
    bool auto_retry         = wxGetApp().app_config->get("liveview", "auto_retry") != "false";
    m_switch_liveview_retry->SetValue(auto_retry);

    grid_rest->Add(m_text_liveview_retry, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));
    grid_rest->Add(m_switch_liveview_retry, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT | wxALL, FromDIP(5));

    m_switch_liveview_retry->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent &e) {
        wxGetApp().app_config->set("liveview", "auto_retry", e.IsChecked());
        e.Skip();
    });
#endif

    //resolution
    m_text_resolution = new wxStaticText(m_panel, wxID_ANY, _L("Resolution"));
    m_text_resolution->Wrap(-1);
    m_text_resolution->SetFont(Label::Head_14);
    m_text_resolution->SetForegroundColour(TEXT_COL);
    grid_rest->Add(m_text_resolution, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));
    grid_rest->Add(0, 0, wxALL, 0);
    for (int i = 0; i < (int)RESOLUTION_OPTIONS_NUM; ++i)
    {
        m_resolution_options[i] = create_item_radiobox(to_resolution_label_string(CameraResolution(i)), m_panel, wxEmptyString, FromDIP(10));
        grid_rest->Add(m_resolution_options[i], 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));
        grid_rest->Add(0, 0, wxALL, 0);
    }

    m_text_storage = new wxStaticText(m_panel, wxID_ANY, _L("External Storage"));
    m_text_storage->Wrap(-1);
    m_text_storage->SetFont(Label::Head_14);
    m_text_storage->SetForegroundColour(TEXT_COL);
    m_text_storage_value = new wxStaticText(m_panel, wxID_ANY, wxEmptyString);
    m_text_storage_value->Wrap(-1);
    m_text_storage_value->SetFont(Label::Body_14);
    m_text_storage_value->SetForegroundColour(ThemeColor::TextMuted);

    grid_rest->Add(m_text_storage, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));
    grid_rest->Add(m_text_storage_value, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT | wxALL, FromDIP(5));

    main_sizer->Add(grid_recording, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(10));
    main_sizer->Add(m_text_recording_hint, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(15));
    main_sizer->Add(grid_per_cam, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(10));
    main_sizer->Add(m_text_cam_rec_hint, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(15));
    main_sizer->Add(grid_rest, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));

    auto url = wxString::Format(L"https://wiki.bambulab.com/%s/software/bambu-studio/virtual-camera", L"en");
    auto text = _L("Show \"Live Video\" guide page.");

    wxBoxSizer* link_sizer = new wxBoxSizer(wxVERTICAL);
    vcamera_guide_link = new Label(m_panel, text);
    vcamera_guide_link->Wrap(-1);
    vcamera_guide_link->SetForegroundColour(wxColour(0x1F, 0x8E, 0xEA));
    auto text_size = vcamera_guide_link->GetTextExtent(text);
    vcamera_guide_link->Bind(wxEVT_LEFT_DOWN, [this, url](wxMouseEvent& e) {wxLaunchDefaultBrowser(url); });

    link_underline = new wxPanel(m_panel, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    link_underline->SetBackgroundColour(wxColour(0x1F, 0x8E, 0xEA));
    link_underline->SetSize(wxSize(text_size.x, 1));
    link_underline->SetMinSize(wxSize(text_size.x, 1));

    vcamera_guide_link->Hide();
    link_underline->Hide();
    link_sizer->Add(vcamera_guide_link, 0, wxALL, 0);
    link_sizer->Add(link_underline, 0, wxALL, 0);

    main_sizer->Add(link_sizer, 0, wxALL, FromDIP(15));

    //timelapse hint
    wxBoxSizer* timelapse_hint_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto timelapse_hint_icon = new wxStaticBitmap(m_panel, wxID_ANY, create_scaled_bitmap("liveview_timelapse_warning", m_panel, 14));
    m_text_timelapse_hint = new wxStaticText(m_panel, wxID_ANY, wxEmptyString);
    m_text_timelapse_hint->SetFont(Label::Body_14);
    m_text_timelapse_hint->SetForegroundColour(wxColour(0x90, 0x90, 0x90));
    m_wrap_hints.push_back({m_text_timelapse_hint, _L("Timelapse can only be set when starting a task."), 50});
    timelapse_hint_sizer->Add(timelapse_hint_icon, 0, wxALIGN_TOP | wxTOP, FromDIP(2));
    timelapse_hint_sizer->Add(m_text_timelapse_hint, 1, wxLEFT, FromDIP(6));

    main_sizer->Add(timelapse_hint_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(15));

    m_panel->SetSizer(main_sizer);
    m_panel->Layout();

    main_sizer->Fit(m_panel);

    for (SwitchButton* sw : {m_switch_recording, m_switch_live_cam_rec, m_switch_visual_cam_rec}) {
        wxBitmap bmp = sw->GetBitmapLabel();
        if (bmp.IsOk()) sw->SetBitmapDisabled(bmp.ConvertToDisabled());
    }

    wrap_hints();
    m_panel->Layout();
    main_sizer->Fit(m_panel);
    SetClientSize(m_panel->GetSize());
    m_switch_recording->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) {
        on_switch_recording(e);
    });
    m_switch_live_cam_rec->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) {
        on_switch_live_cam_rec(e);
    });
    m_switch_visual_cam_rec->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) {
        on_switch_visual_cam_rec(e);
    });
    m_switch_vcamera->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) {
        auto now = std::chrono::steady_clock::now();
        if (now - m_last_vcamera_click < std::chrono::milliseconds(300)) return;
        m_last_vcamera_click = now;

        wxCommandEvent evt(EVT_VCAMERA_SWITCH);
        evt.SetEventObject(this);
        GetEventHandler()->ProcessEvent(evt);
    });
    #ifdef __APPLE__
    m_panel->Bind(wxEVT_LEFT_UP, &CameraPopup::OnLeftUp, this);
    #endif //APPLE

    this->Bind(wxEVT_TIMER, &CameraPopup::stop_interval, this);
    m_interval_timer = new wxTimer();
    m_interval_timer->SetOwner(this);

    wxGetApp().UpdateDarkUIWin(this);
}

void CameraPopup::on_switch_recording(wxMouseEvent& event)
{
    if (!m_obj) return;
    if (!m_switch_recording->IsThisEnabled()) return;

    auto now = std::chrono::steady_clock::now();
    if (now - m_last_recording_click < std::chrono::milliseconds(300)) return;
    m_last_recording_click = now;

    bool value = m_switch_recording->GetValue();
    m_switch_recording->SetValue(!value);
    m_obj->command_ipcam_record(!value);
}

void CameraPopup::on_switch_live_cam_rec(wxMouseEvent& event)
{
    if (!m_obj) return;
    if (!m_switch_live_cam_rec->IsThisEnabled()) return;

    auto now = std::chrono::steady_clock::now();
    if (now - m_last_live_cam_click < std::chrono::milliseconds(300)) return;
    m_last_live_cam_click = now;

    bool value = m_switch_live_cam_rec->GetValue();
    m_switch_live_cam_rec->SetValue(!value);
    m_obj->command_ipcam_record_cam(static_cast<int>(LiveviewCameraType::CAMERA_MAIN), !value);
}

void CameraPopup::on_switch_visual_cam_rec(wxMouseEvent& event)
{
    if (!m_obj) return;
    if (!m_switch_visual_cam_rec->IsThisEnabled()) return;

    auto now = std::chrono::steady_clock::now();
    if (now - m_last_visual_cam_click < std::chrono::milliseconds(300)) return;
    m_last_visual_cam_click = now;

    bool value = m_switch_visual_cam_rec->GetValue();
    m_switch_visual_cam_rec->SetValue(!value);
    m_obj->command_ipcam_record_cam(static_cast<int>(LiveviewCameraType::CAMERA_DEPUTY), !value);
}

void CameraPopup::on_set_resolution()
{
    if (!m_obj) return;

    m_obj->command_ipcam_resolution_set(to_resolution_msg_string(curr_sel_resolution));
}

void CameraPopup::Popup(wxWindow *WXUNUSED(focus))
{
    wxPoint curr_position = this->GetPosition();
    wxSize win_size = this->GetSize();
    curr_position.x -= win_size.x;
    this->SetPosition(curr_position);

    if (!m_is_in_interval)
        PopupWindow::Popup();
}

wxWindow* CameraPopup::create_item_radiobox(wxString title, wxWindow* parent, wxString tooltip, int padding_left)
{
    wxWindow *item = new wxWindow(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(20)));
    item->SetBackgroundColour(*wxWHITE);

    RadioBox *radiobox = new RadioBox(item);
    radiobox->SetPosition(wxPoint(padding_left, (item->GetSize().GetHeight() - radiobox->GetSize().GetHeight()) / 2));
    resolution_rbtns.push_back(radiobox);
    int btn_idx = resolution_rbtns.size() - 1;
    radiobox->Bind(wxEVT_LEFT_DOWN, [this, btn_idx](wxMouseEvent &e) {
        if (m_obj && allow_alter_resolution) {
            select_curr_radiobox(btn_idx);
            on_set_resolution();
        }
        });

    wxStaticText *text = new wxStaticText(item, wxID_ANY, title, wxDefaultPosition, wxDefaultSize);
    text->SetForegroundColour(*wxBLACK);
    resolution_texts.push_back(text);
    text->SetPosition(wxPoint(padding_left + radiobox->GetSize().GetWidth() + 10, (item->GetSize().GetHeight() - text->GetSize().GetHeight()) / 2));
    text->SetFont(Label::Body_13);
    text->SetForegroundColour(0x6B6B6B);
    text->Bind(wxEVT_LEFT_DOWN, [this, btn_idx](wxMouseEvent &e) {
        if (m_obj && allow_alter_resolution) {
            select_curr_radiobox(btn_idx);
            on_set_resolution();
        }
        });

    radiobox->SetToolTip(tooltip);
    text->SetToolTip(tooltip);
    return item;
}

void CameraPopup::select_curr_radiobox(int btn_idx)
{
    if (!m_obj) return;

    int len = resolution_rbtns.size();
    for (int i = 0; i < len; ++i) {
        if (i == btn_idx) {
            curr_sel_resolution = CameraResolution(i);
            resolution_rbtns[i]->SetValue(true);
        }
        else {
            resolution_rbtns[i]->SetValue(false);
        }
    }
}

void CameraPopup::sync_resolution_setting(std::string resolution)
{
    if (resolution == "") {
        reset_resolution_setting();
        return;
    }
    int res = 0;
    for (CameraResolution i = RESOLUTION_720P; i < RESOLUTION_OPTIONS_NUM; i = CameraResolution(i+1)){
        if (resolution == to_resolution_msg_string(i)) {
            res = int(i);
            break;
        }
    }
    select_curr_radiobox(res);
}

void CameraPopup::reset_resolution_setting()
{
    int len = resolution_rbtns.size();
    for (int i = 0; i < len; ++i) {
         resolution_rbtns[i]->SetValue(false);
    }
    curr_sel_resolution = RESOLUTION_OPTIONS_NUM;
}

void CameraPopup::sync_vcamera_state(bool show_vcamera)
{
    is_vcamera_show = show_vcamera;
    if (is_vcamera_show) {
        m_switch_vcamera->SetValue(true);
        vcamera_guide_link->Show();
        link_underline->Show();
    }
    else {
        m_switch_vcamera->SetValue(false);
        vcamera_guide_link->Hide();
        link_underline->Hide();
    }

    rescale();
}

void CameraPopup::check_func_supported(MachineObject *obj2)
{
    m_obj = obj2;
    if (m_obj == nullptr)
        return;

    // per-camera recording capability
    auto& srcs = m_obj->support_record_cams;
    bool support_cam_main   = std::find(srcs.begin(), srcs.end(),
        static_cast<int>(LiveviewCameraType::CAMERA_MAIN))   != srcs.end();
    bool support_cam_deputy = std::find(srcs.begin(), srcs.end(),
        static_cast<int>(LiveviewCameraType::CAMERA_DEPUTY)) != srcs.end();
    bool support_per_cam_rec = support_cam_main || support_cam_deputy;

    // function supported: Auto-record Monitoring only visible when per-camera recording is unsupported
    if (m_obj->has_ipcam && !support_per_cam_rec) {
        m_text_recording->Show();
        m_switch_recording->Show();
    } else {
        m_text_recording->Hide();
        m_switch_recording->Hide();
    }

    if (m_obj->virtual_camera && m_obj->has_ipcam) {
        m_text_vcamera->Show();
        m_switch_vcamera->Show();
        if (is_vcamera_show) {
            vcamera_guide_link->Show();
            link_underline->Show();
        }
    } else {
        m_text_vcamera->Hide();
        m_switch_vcamera->Hide();
        vcamera_guide_link->Hide();
        link_underline->Hide();
    }

    allow_alter_resolution = ( (m_obj->camera_resolution_supported.size() > 1?true:false) && m_obj->has_ipcam);

    //check u2 version
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return;
    MachineObject* obj = dev->get_selected_machine();
    if (!obj) return;

    //resolution supported
    std::vector<std::string> resolution_supported = m_obj->get_resolution_supported();
    auto support_count = resolution_supported.size();
    for (int i = 0; i < (int)RESOLUTION_OPTIONS_NUM; ++i){
        auto curr_res = to_resolution_msg_string(CameraResolution(i));
        std::vector <std::string> ::iterator it = std::find(resolution_supported.begin(), resolution_supported.end(), curr_res);
        if ((it == resolution_supported.end())||(support_count <= 1) || !obj->is_support_1080dpi)
            m_resolution_options[i]->Hide();
        else {
            m_resolution_options[i]->Show();
            if (m_obj->camera_resolution == curr_res) {
                resolution_rbtns[i]->SetValue(true);
            }
        }
    }
    //hide resolution if there is only one choice
    if (support_count <= 1 || !obj->is_support_1080dpi) {
        m_text_resolution->Hide();
    }
    else {
        m_text_resolution->Show();
    }

    // per-camera recording buttons
    m_text_live_cam_rec->Show(support_cam_main);
    m_switch_live_cam_rec->Show(support_cam_main);
    m_text_visual_cam_rec->Show(support_cam_deputy);
    m_switch_visual_cam_rec->Show(support_cam_deputy);
    rescale();
}

void CameraPopup::update(bool vcamera_streaming)
{
    if (!m_obj) return;

    bool has_storage = m_obj->GetStorage()->get_sdcard_state() == DevStorage::SdcardState::HAS_SDCARD_NORMAL;

    m_switch_recording->SetValue(has_storage && m_obj->camera_recording_when_printing);

    auto& erc = m_obj->enable_record_cams;
    m_switch_live_cam_rec->SetValue(has_storage &&
        std::find(erc.begin(), erc.end(),
            static_cast<int>(LiveviewCameraType::CAMERA_MAIN)) != erc.end());
    m_switch_visual_cam_rec->SetValue(has_storage &&
        std::find(erc.begin(), erc.end(),
            static_cast<int>(LiveviewCameraType::CAMERA_DEPUTY)) != erc.end());

    // No external storage: grey out recording switches and show hint under the relevant switch.
    m_switch_recording->Enable(has_storage);
    m_switch_live_cam_rec->Enable(has_storage);
    m_switch_visual_cam_rec->Enable(has_storage);
    m_text_recording_hint->Show(!has_storage && m_switch_recording->IsShown());
    m_text_cam_rec_hint->Show(!has_storage &&
        (m_switch_live_cam_rec->IsShown() || m_switch_visual_cam_rec->IsShown()));

    switch (m_obj->GetStorage()->get_sdcard_state()) {
    case DevStorage::SdcardState::NO_SDCARD: m_text_storage_value->SetLabel(_L("Not Installed")); break;
    case DevStorage::SdcardState::HAS_SDCARD_ABNORMAL: m_text_storage_value->SetLabel(_L("Abnormal")); break;
    default: m_text_storage_value->SetLabel(_L("Installed")); break;
    }
    m_text_storage_value->InvalidateBestSize();

    sync_resolution_setting(m_obj->camera_resolution);
    sync_vcamera_state(vcamera_streaming);
}

wxString CameraPopup::to_resolution_label_string(CameraResolution resolution) {
    switch (resolution) {
    case RESOLUTION_720P:
        return "720p";
    case RESOLUTION_1080P:
        return "1080p";
    default:
        return "";
    }
    return "";
}

std::string CameraPopup::to_resolution_msg_string(CameraResolution resolution) {
    switch (resolution) {
    case RESOLUTION_720P:
        return std::string("720p");
    case RESOLUTION_1080P:
        return std::string("1080p");
    default:
        return "";
    }
    return "";
}

void CameraPopup::wrap_hints()
{
    for (auto& h : m_wrap_hints) {
        if (h.text) h.text->SetMinSize(wxSize(1, -1));
    }

    const int target = main_sizer->CalcMin().x;

    unsigned shown = 0;
    for (size_t i = 0; i < m_wrap_hints.size(); ++i) {
        if (m_wrap_hints[i].text && m_wrap_hints[i].text->IsShown())
            shown |= (1u << i);
    }

    if (target == m_content_width && shown == m_wrap_last_shown)
        return;

    wxMemoryDC dc;
    wxBitmap   bmp(1, 1);
    dc.SelectObject(bmp);

    for (size_t i = 0; i < m_wrap_hints.size(); ++i) {
        auto& h = m_wrap_hints[i];
        if (!h.text) continue;
        if (shown & (1u << i)) {
            const int w = target - FromDIP(h.reserved_dip);
            if (w > 0) {
                dc.SetFont(h.text->GetFont());
                wxString wrapped;
                Label::split_lines(dc, w, h.original, wrapped);
                h.text->SetLabel(wrapped);
            } else {
                h.text->SetLabel(h.original);
            }
        } else {
            h.text->SetLabel(wxEmptyString);
        }
    }

    dc.SelectObject(wxNullBitmap);

    m_content_width = target;
    m_wrap_last_shown = shown;
}

void CameraPopup::rescale()
{
    m_panel->Freeze();
    wrap_hints();
    m_panel->Layout();
    main_sizer->Fit(m_panel);
    SetClientSize(m_panel->GetSize());
    m_panel->Thaw();
}

void CameraPopup::OnLeftUp(wxMouseEvent &event)
{
    auto mouse_pos = ClientToScreen(event.GetPosition());
    auto wxscroll_win_pos = m_panel->ClientToScreen(wxPoint(0, 0));

    if (mouse_pos.x > wxscroll_win_pos.x && mouse_pos.y > wxscroll_win_pos.y && mouse_pos.x < (wxscroll_win_pos.x + m_panel->GetSize().x) && mouse_pos.y < (wxscroll_win_pos.y + m_panel->GetSize().y)) {
        //recording
        auto recording_rect = m_switch_recording->ClientToScreen(wxPoint(0, 0));
        if (mouse_pos.x > recording_rect.x && mouse_pos.y > recording_rect.y && mouse_pos.x < (recording_rect.x + m_switch_recording->GetSize().x) && mouse_pos.y < (recording_rect.y + m_switch_recording->GetSize().y)) {
            wxMouseEvent recording_evt(wxEVT_LEFT_DOWN);
            on_switch_recording(recording_evt);
            return;
        }
        //live camera recording
        if (m_switch_live_cam_rec->IsShown()) {
            auto live_cam_rect = m_switch_live_cam_rec->ClientToScreen(wxPoint(0, 0));
            if (mouse_pos.x > live_cam_rect.x && mouse_pos.y > live_cam_rect.y && mouse_pos.x < (live_cam_rect.x + m_switch_live_cam_rec->GetSize().x) && mouse_pos.y < (live_cam_rect.y + m_switch_live_cam_rec->GetSize().y)) {
                wxMouseEvent live_cam_evt(wxEVT_LEFT_DOWN);
                on_switch_live_cam_rec(live_cam_evt);
                return;
            }
        }
        //visual enhancement camera recording
        if (m_switch_visual_cam_rec->IsShown()) {
            auto visual_cam_rect = m_switch_visual_cam_rec->ClientToScreen(wxPoint(0, 0));
            if (mouse_pos.x > visual_cam_rect.x && mouse_pos.y > visual_cam_rect.y && mouse_pos.x < (visual_cam_rect.x + m_switch_visual_cam_rec->GetSize().x) && mouse_pos.y < (visual_cam_rect.y + m_switch_visual_cam_rec->GetSize().y)) {
                wxMouseEvent visual_cam_evt(wxEVT_LEFT_DOWN);
                on_switch_visual_cam_rec(visual_cam_evt);
                return;
            }
        }
        //vcamera
        auto vcamera_rect = m_switch_vcamera->ClientToScreen(wxPoint(0, 0));
        if (mouse_pos.x > vcamera_rect.x && mouse_pos.y > vcamera_rect.y && mouse_pos.x < (vcamera_rect.x + m_switch_vcamera->GetSize().x) && mouse_pos.y < (vcamera_rect.y + m_switch_vcamera->GetSize().y)) {
            wxMouseEvent vcamera_evt(wxEVT_LEFT_DOWN);
            m_switch_vcamera->GetEventHandler()->ProcessEvent(vcamera_evt);
            return;
        }
        //resolution
        for (int i = 0; i < (int)RESOLUTION_OPTIONS_NUM; ++i){
            auto resolution_rbtn = resolution_rbtns[i];
            auto rbtn_rect = resolution_rbtn->ClientToScreen(wxPoint(0, 0));
            if (mouse_pos.x > rbtn_rect.x && mouse_pos.y > rbtn_rect.y && mouse_pos.x < (rbtn_rect.x + resolution_rbtn->GetSize().x) && mouse_pos.y < (rbtn_rect.y + resolution_rbtn->GetSize().y)) {
                wxMouseEvent resolution_evt(wxEVT_LEFT_DOWN);
                resolution_rbtn->GetEventHandler()->ProcessEvent(resolution_evt);
                return;
            }
            auto resolution_txt = resolution_texts[i];
            auto txt_rect = resolution_txt->ClientToScreen(wxPoint(0, 0));
            if (mouse_pos.x > txt_rect.x && mouse_pos.y > txt_rect.y && mouse_pos.x < (txt_rect.x + resolution_txt->GetSize().x) && mouse_pos.y < (txt_rect.y + resolution_txt->GetSize().y)) {
                wxMouseEvent resolution_evt(wxEVT_LEFT_DOWN);
                resolution_txt->GetEventHandler()->ProcessEvent(resolution_evt);
                return;
            }
        }
        //hyper link
        auto h_rect = vcamera_guide_link->ClientToScreen(wxPoint(0, 0));
        if (mouse_pos.x > h_rect.x && mouse_pos.y > h_rect.y && mouse_pos.x < (h_rect.x + vcamera_guide_link->GetSize().x) && mouse_pos.y < (h_rect.y + vcamera_guide_link->GetSize().y)) {
            auto url = wxString::Format(L"https://wiki.bambulab.com/%s/software/bambu-studio/virtual-camera", L"en");
            wxLaunchDefaultBrowser(url);
        }
    }
}

void CameraPopup::start_interval()
{
    m_interval_timer->Start(CAMERAPOPUP_CLICK_INTERVAL);
    m_is_in_interval = true;
}

void CameraPopup::stop_interval(wxTimerEvent& event)
{
    m_is_in_interval = false;
    m_interval_timer->Stop();
}

void CameraPopup::OnDismiss() {
    PopupWindow::OnDismiss();
    this->start_interval();
    if (m_on_dismiss)
        m_on_dismiss();
}

bool CameraPopup::ProcessLeftDown(wxMouseEvent &event)
{
    return PopupWindow::ProcessLeftDown(event);
}

bool CameraPopup::Show(bool show)
{
    return PopupWindow::Show(show);
}

void CameraPopup::OnSize(wxSizeEvent &event)
{
    event.Skip();
}

void CameraPopup::OnSetFocus(wxFocusEvent &event)
{
    event.Skip();
}

void CameraPopup::OnKillFocus(wxFocusEvent &event)
{
    event.Skip();
}

void CameraPopup::OnMouse(wxMouseEvent &event)
{
    event.Skip();
}

CameraItem::CameraItem(wxWindow *parent, std::string normal, std::string hover, int icon_dip, std::string selected, double selected_scale)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxNO_BORDER)
    , m_icon_dip(icon_dip)
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif

    m_bitmap_normal = ScalableBitmap(this, normal, m_icon_dip);
    m_bitmap_hover  = ScalableBitmap(this, hover,  m_icon_dip);
    if (!selected.empty()) {
        m_selected_icon_dip = static_cast<int>(std::lround(m_icon_dip * selected_scale));
        m_bitmap_selected   = ScalableBitmap(this, selected, m_selected_icon_dip);
    }

    apply_outer_size();

    m_image = new wxStaticBitmap(this, wxID_ANY, m_bitmap_normal.bmp());
    const wxSize icon_size{FromDIP(m_icon_dip), FromDIP(m_icon_dip)};
    m_image->SetMinSize(icon_size);
    m_image->SetMaxSize(icon_size);

    auto *sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->AddStretchSpacer();
    sizer->Add(m_image, 0, wxALIGN_CENTER_VERTICAL);
    sizer->AddStretchSpacer();
    SetSizer(sizer);
    Layout();

    Bind(wxEVT_ENTER_WINDOW, &CameraItem::on_enter_win, this);
    Bind(wxEVT_LEAVE_WINDOW, &CameraItem::on_leave_win, this);

    m_image->Bind(wxEVT_LEFT_DOWN,   &CameraItem::forward_mouse_event, this);
    m_image->Bind(wxEVT_LEFT_UP,     &CameraItem::forward_mouse_event, this);
    m_image->Bind(wxEVT_LEFT_DCLICK, &CameraItem::forward_mouse_event, this);
    m_image->Bind(wxEVT_RIGHT_DOWN,  &CameraItem::forward_mouse_event, this);
    m_image->Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent &e) {
        set_hover(true);
        e.Skip();
    });
    m_image->Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &e) {
        set_hover(false);
        e.Skip();
    });
}

CameraItem::~CameraItem() {}

void CameraItem::msw_rescale()
{
    m_bitmap_normal.msw_rescale();
    m_bitmap_hover.msw_rescale();
    if (m_bitmap_selected.bmp().IsOk())
        m_bitmap_selected.msw_rescale();
    update_displayed_bitmap();
}

void CameraItem::SetBitmaps(const std::string &normal, const std::string &hover, const std::string &selected, double selected_scale)
{
    if (m_bitmap_normal.name() != normal) m_bitmap_normal = ScalableBitmap(this, normal, m_icon_dip);
    if (m_bitmap_hover.name() != hover) m_bitmap_hover = ScalableBitmap(this, hover, m_icon_dip);

    if (selected.empty()) {
        m_selected_icon_dip = 0;
        m_bitmap_selected   = ScalableBitmap();
    } else if (m_bitmap_selected.name() != selected) {
        m_selected_icon_dip = static_cast<int>(std::lround(m_icon_dip * selected_scale));
        m_bitmap_selected   = ScalableBitmap(this, selected, m_selected_icon_dip);
    }
    apply_outer_size();
    update_displayed_bitmap();
    if (GetParent())
        GetParent()->Layout();
}

void CameraItem::apply_outer_size()
{
    const int    max_dip = std::max(m_icon_dip, m_selected_icon_dip);
    const wxSize sz(FromDIP(max_dip), FromDIP(max_dip));
    SetMinSize(sz);
    SetMaxSize(sz);
}

void CameraItem::SetSelected(bool selected)
{
    if (m_selected == selected)
        return;
    m_selected = selected;
    update_displayed_bitmap();
}

void CameraItem::on_enter_win(wxMouseEvent &evt)
{
    set_hover(true);
    evt.Skip();
}

void CameraItem::on_leave_win(wxMouseEvent &evt)
{
    set_hover(false);
    evt.Skip();
}

void CameraItem::reset_hover()
{
    set_hover(false);
}

void CameraItem::set_hover(bool hover)
{
    if (m_hover == hover)
        return;
    m_hover = hover;
    update_displayed_bitmap();
}

bool CameraItem::Enable(bool enable)
{
    bool ret = wxPanel::Enable(enable);
    m_hover = false;
    update_displayed_bitmap();
    return ret;
}

bool CameraItem::SetBackgroundColour(const wxColour &colour)
{
    bool ret = wxPanel::SetBackgroundColour(colour);
    if (m_image)
        m_image->SetBackgroundColour(colour);
    return ret;
}

wxBitmap CameraItem::make_disabled_bitmap(const wxBitmap &bmp) const
{
    if (!bmp.IsOk())
        return bmp;

    wxImage img = bmp.ConvertToImage();
    std::vector<unsigned char> alpha;
    if (img.HasAlpha()) {
        unsigned char *alpha_data = img.GetAlpha();
        alpha.assign(alpha_data, alpha_data + img.GetWidth() * img.GetHeight());
    }

    wxImage gray = img.ConvertToGreyscale();
    if (!alpha.empty()) {
        if (!gray.HasAlpha())
            gray.InitAlpha();
        std::copy(alpha.begin(), alpha.end(), gray.GetAlpha());
    }
    return wxBitmap(gray);
}

void CameraItem::update_displayed_bitmap()
{
    if (!m_image)
        return;

    bool show_selected = (m_selected || m_hover) && m_bitmap_selected.bmp().IsOk();
    int  dip = show_selected ? m_selected_icon_dip : m_icon_dip;
    const wxSize sz(FromDIP(dip), FromDIP(dip));
    if (m_image->GetMinSize() != sz) {
        m_image->SetMinSize(sz);
        m_image->SetMaxSize(sz);
        Layout();
    }

    const wxBitmap desired = show_selected ? m_bitmap_selected.bmp()
                            : IsEnabled()   ? (m_hover ? m_bitmap_hover.bmp() : m_bitmap_normal.bmp())
                                            : make_disabled_bitmap(m_bitmap_normal.bmp());
    m_image->SetBitmap(desired);
    m_image->Refresh();
}

void CameraItem::forward_mouse_event(wxMouseEvent &evt)
{
    wxMouseEvent fwd(evt);
    fwd.SetEventObject(this);
    ProcessWindowEvent(fwd);
}

}
}