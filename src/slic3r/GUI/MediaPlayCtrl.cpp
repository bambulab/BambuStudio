#include "MediaPlayCtrl.h"
#include "Widgets/Button.hpp"
#include "Widgets/Label.hpp"
#include "GUI_App.hpp"

namespace Slic3r {
namespace GUI {

MediaPlayCtrl::MediaPlayCtrl(wxWindow * parent, wxMediaCtrl2* media_ctrl)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
    , m_media_ctrl(media_ctrl)
{
    m_media_ctrl->Bind(wxEVT_MEDIA_STATECHANGED, &MediaPlayCtrl::onStateChanged, this);

    m_button_play = new Button(this, "", "media_play", wxBORDER_NONE);
    m_label_status = new Label(Label::Body_14, this);

    m_button_play->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](auto & e) { TogglePlay(); });

    wxBoxSizer * sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(m_button_play);
    sizer->AddStretchSpacer(1);
    sizer->Add(m_label_status);
    SetSizer(sizer);

    SetMinSize({400, 60});
}

void MediaPlayCtrl::SetMachineObject(MachineObject* obj)
{
    if (obj == m_machine)
        return;
    m_machine = obj;
    if (m_last_state != MEDIASTATE_IDLE)
        Stop();
}

void MediaPlayCtrl::Play()
{
    if (m_machine == nullptr) {
        SetStatus(L"Initialize failed (No Device)!");
        return;
    }
    m_last_state = MEDIASTATE_INITIALIZING;
    m_button_play->SetIcon("media_stop");
    SetStatus(L"Initializing...");
    wxGetApp()
        .getAccountManager()
        ->get_camera_url(m_machine->dev_id, [this](std::string url) {
        BOOST_LOG_TRIVIAL(info) << "camera_url: " << url;
        CallAfter([this, url] {
            if (m_last_state != MEDIASTATE_INITIALIZING)
                return;
            if (url.empty()) {
                Stop();
                SetStatus(L"Initialize failed!");
            } else {
                m_last_state = MEDIASTATE_LOADING;
                m_media_ctrl->Load(wxURI(url));
                SetStatus(L"Connecting...");
            }
         });
    });
}

void MediaPlayCtrl::Stop()
{
    if (m_last_state != MEDIASTATE_IDLE) {
        m_media_ctrl->Stop();
        m_media_ctrl->InvalidateBestSize();
        m_button_play->SetIcon("media_play");
    }
    m_last_state = MEDIASTATE_IDLE;
    SetStatus(L"Stopped.");
}

void MediaPlayCtrl::TogglePlay()
{
    if (m_last_state != MEDIASTATE_IDLE)
        Stop();
    else
        Play();
}

void MediaPlayCtrl::SetStatus(wxString const& msg)
{
    m_label_status->SetLabel(msg);
    m_label_status->SetForegroundColour(!msg.EndsWith("!") ? 0x42AE00 : 0x3B65E9);
    Layout();
}

void MediaPlayCtrl::onStateChanged(wxMediaEvent& event)
{
    auto last_state = m_last_state;
    auto state = m_media_ctrl->GetState();
    m_last_state = state;
    if ((last_state == wxMEDIASTATE_PAUSED || last_state == wxMEDIASTATE_PLAYING)  &&
        state == wxMEDIASTATE_STOPPED) {
        Stop();
        return;
    }
    if (last_state == MEDIASTATE_LOADING && state == wxMEDIASTATE_STOPPED) {
        wxSize size = m_media_ctrl->GetBestSize();
        if (size.GetWidth() > 1000) {
            m_media_ctrl->Play();
            SetStatus(L"Playing...");
            m_last_state = m_media_ctrl->GetState();
        }
        else {
            Stop();
            SetStatus(L"connect failed");
        }
    }
}

}}