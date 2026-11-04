#pragma once

#include <wx/panel.h>

namespace Slic3r {
class MachineObject;
} // namespace Slic3r

namespace Slic3r::GUI {

class DeviceWebHost;

class wgtDeviceFilamentLoadWebPanel : public wxPanel
{
public:
    explicit wgtDeviceFilamentLoadWebPanel(wxWindow* parent);
    ~wgtDeviceFilamentLoadWebPanel() override = default;

    void ShowAndLoad();
    void HideAndSuspend();
    void UpdateByMachine(Slic3r::MachineObject* obj);

private:
    DeviceWebHost* m_device_web{ nullptr };
};

} // namespace Slic3r::GUI
