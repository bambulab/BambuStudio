#include "wgtDeviceControlPanelWebPanel.h"

#include "slic3r/GUI/DeviceWeb/DeviceWebHost.hpp"
#include "slic3r/GUI/DeviceWeb/ViewModels/DevicePage/ControlPanel/ViewModelActions.hpp"

#include <wx/sizer.h>

namespace Slic3r::GUI {

namespace {
constexpr int kMinWidthDip  = 586;
constexpr int kMinHeightDip = 329;
constexpr const char* kRoute = "/device_page/control_panel";
} // namespace

wgtDeviceControlPanelWebPanel::wgtDeviceControlPanelWebPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    auto* root = new wxBoxSizer(wxVERTICAL);

    SetMinSize(wxSize(FromDIP(kMinWidthDip), FromDIP(kMinHeightDip)));
    SetMaxSize(wxSize(FromDIP(kMinWidthDip), FromDIP(kMinHeightDip)));
    m_device_web = new DeviceWebHost(this,
                                     DeviceWebHostMode::DevicePageControlPanel,
                                     kRoute,
                                     /*allow_lazy=*/true);
    m_device_web->SetLockContentSize(true);
    m_device_web->SetMinSize(wxSize(FromDIP(kMinWidthDip), FromDIP(kMinHeightDip)));
    m_device_web->SetMaxSize(wxSize(FromDIP(kMinWidthDip), FromDIP(kMinHeightDip)));
    m_device_web->SetContentSizeChangedHandler([this](const wxSize& content_size) {
        SetMinSize(content_size);
        SetMaxSize(content_size);
        SetSize(content_size);
        Layout();
        if (auto* parent = GetParent()) {
            parent->Layout();
            parent->SendSizeEvent();
            if (auto* gp = parent->GetParent()) {
                gp->Layout();
                gp->SendSizeEvent();
            }
        }
    });

    root->Add(m_device_web, 0, wxEXPAND, 0);
    SetSizer(root);
    Layout();
}

void wgtDeviceControlPanelWebPanel::ShowAndLoad()
{
    const bool was_hidden = !IsShown();
    Show(true);
    if (m_device_web && was_hidden) {
        m_device_web->NavigateTo(kRoute);
    }
}

void wgtDeviceControlPanelWebPanel::HideAndSuspend()
{
    Show(false);
    if (m_device_web) {
        m_device_web->Suspend();
    }
}

void wgtDeviceControlPanelWebPanel::UpdateByMachine(Slic3r::MachineObject* obj)
{
    ControlPanelActionHandler::UpdateActiveFanPopup(obj);
    if (m_device_web && IsShown()) {
        m_device_web->NotifyControlPanelChanged();
    }
}

void wgtDeviceControlPanelWebPanel::on_sys_color_changed()
{
    if (m_device_web) {
        m_device_web->on_sys_color_changed();
    }
}

} // namespace Slic3r::GUI
