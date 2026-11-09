#pragma once

#include <wx/panel.h>

namespace Slic3r {
class MachineObject;
} // namespace Slic3r

namespace Slic3r::GUI {

class DeviceWebHost;

// Thin wxPanel host for the filament/hotend WebView page.
// DeviceWebHost already handles content size changes.
class wgtDeviceFilamentHotendWebPanel : public wxPanel
{
public:
    explicit wgtDeviceFilamentHotendWebPanel(wxWindow* parent);
    ~wgtDeviceFilamentHotendWebPanel() override = default;

    void ShowAndLoad();
    void HideAndSuspend();
    void UpdateByMachine(Slic3r::MachineObject* obj);

private:
    DeviceWebHost* m_device_web{ nullptr };
};

} // namespace Slic3r::GUI
