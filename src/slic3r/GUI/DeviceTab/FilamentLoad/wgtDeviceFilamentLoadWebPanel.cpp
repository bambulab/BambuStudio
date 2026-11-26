#include "wgtDeviceFilamentLoadWebPanel.h"

#include "slic3r/GUI/DeviceWeb/DeviceWebHost.hpp"

#include <nlohmann/json.hpp>
#include <wx/sizer.h>

namespace Slic3r::GUI {

namespace {
constexpr int kMinWidthDip  = 586;
constexpr int kMinHeightDip = 40;
constexpr const char* kRoute = "/device_page/filament_load";
} // namespace

wgtDeviceFilamentLoadWebPanel::wgtDeviceFilamentLoadWebPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    auto* root = new wxBoxSizer(wxVERTICAL);

    SetMinSize(wxSize(FromDIP(kMinWidthDip), FromDIP(kMinHeightDip)));
    m_device_web = new DeviceWebHost(this,
                                     DeviceWebHostMode::DevicePageFilamentLoad,
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

void wgtDeviceFilamentLoadWebPanel::ShowAndLoad()
{
    const bool was_hidden = !IsShown();
    Show(true);
    if (m_device_web && was_hidden) {
        m_device_web->DispatchCommand({
            {"module", "device_page_filament_load"},
            {"submod", "action"},
            {"action", "reset_expanded"},
            {"payload", nlohmann::json::object()},
        });
        m_device_web->NavigateTo(kRoute);
    }
}

void wgtDeviceFilamentLoadWebPanel::HideAndSuspend()
{
    if (!IsShown())
        return;

    Show(false);
    if (m_device_web) {
        m_device_web->Suspend();
    }
}

void wgtDeviceFilamentLoadWebPanel::UpdateByMachine(Slic3r::MachineObject* /*obj*/)
{
    if (m_device_web && IsShownOnScreen()) {
        m_device_web->NotifyFilamentLoadChanged();
    }
}

} // namespace Slic3r::GUI
