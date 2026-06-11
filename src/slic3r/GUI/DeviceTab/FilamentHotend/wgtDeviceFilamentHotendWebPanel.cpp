#include "wgtDeviceFilamentHotendWebPanel.h"

#include "slic3r/GUI/DeviceWeb/DeviceWebHost.hpp"

#include <wx/sizer.h>

namespace Slic3r::GUI {

namespace {
constexpr int kMinWidthDip  = 560;
constexpr int kMinHeightDip = 440;
} // namespace

wgtDeviceFilamentHotendWebPanel::wgtDeviceFilamentHotendWebPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    auto* root = new wxBoxSizer(wxVERTICAL);

    SetMinSize(wxSize(FromDIP(kMinWidthDip), FromDIP(kMinHeightDip)));
    m_device_web = new DeviceWebHost(this,
                                     DeviceWebHostMode::DevicePageAmsFilamentHotend,
                                     "/device_page/ams_filament_hotend");
    m_device_web->SetMinSize(wxSize(FromDIP(kMinWidthDip), FromDIP(kMinHeightDip)));

    root->Add(m_device_web, 1, wxEXPAND, 0);
    SetSizer(root);
    Layout();
}

void wgtDeviceFilamentHotendWebPanel::UpdateByMachine(Slic3r::MachineObject* /*obj*/)
{
    if (m_device_web) {
        m_device_web->NotifyDeviceFilamentChanged();
    }
}

} // namespace Slic3r::GUI
