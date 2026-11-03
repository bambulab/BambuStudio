#include "wgtDeviceHotendRackWebPanel.h"

#include "slic3r/GUI/DeviceWeb/DeviceWebHost.hpp"

#include <wx/sizer.h>

namespace Slic3r::GUI {

namespace {
constexpr int kMinWidthDip  = 563;
constexpr int kMinHeightDip = 308;
constexpr const char* kRoute = "/device_page/hotend_rack";
} // namespace

wgtDeviceHotendRackWebPanel::wgtDeviceHotendRackWebPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    auto* root = new wxBoxSizer(wxVERTICAL);

    SetMinSize(wxSize(FromDIP(kMinWidthDip), FromDIP(kMinHeightDip)));
    m_device_web = new DeviceWebHost(this,
                                     DeviceWebHostMode::DevicePageHotendRack,
                                     kRoute,
                                     /*allow_lazy=*/true);
    m_device_web->SetMinSize(wxSize(FromDIP(kMinWidthDip), FromDIP(kMinHeightDip)));
    m_device_web->SetContentSizeChangedHandler([this](const wxSize& content_size) {
        SetMinSize(content_size);
        SetMaxSize(content_size);
        SetSize(content_size);
        Layout();
        if (auto* parent = GetParent()) {
            parent->Layout();
            parent->SendSizeEvent();
        }
    });

    root->Add(m_device_web, 0, wxEXPAND, 0);
    SetSizer(root);
    Layout();
}

void wgtDeviceHotendRackWebPanel::ShowAndLoad()
{
    const bool was_hidden = !IsShown();
    Show(true);
    if (m_device_web && was_hidden) {
        m_device_web->NavigateTo(kRoute);
    }
}

void wgtDeviceHotendRackWebPanel::HideAndSuspend()
{
    Show(false);
    if (m_device_web) {
        m_device_web->Suspend();
    }
}

void wgtDeviceHotendRackWebPanel::UpdateByMachine(Slic3r::MachineObject* /*obj*/)
{
    if (m_device_web && IsShownOnScreen()) {
        m_device_web->NotifyHotendRackChanged();
    }
}

} // namespace Slic3r::GUI
