#pragma once

#include <wx/panel.h>

namespace Slic3r {
class MachineObject;
} // namespace Slic3r

namespace Slic3r::GUI {

class DeviceWebHost;

class wgtDeviceHotendRackWebPanel : public wxPanel
{
public:
    explicit wgtDeviceHotendRackWebPanel(wxWindow* parent);
    ~wgtDeviceHotendRackWebPanel() override = default;

    void ShowAndLoad();
    void HideAndSuspend();
    void UpdateByMachine(Slic3r::MachineObject* obj);

private:
    DeviceWebHost* m_device_web{ nullptr };
};

} // namespace Slic3r::GUI
