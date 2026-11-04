#pragma once

#include "slic3r/GUI/GUI_Utils.hpp"

namespace Slic3r {
class MachineObject;
} // namespace Slic3r

namespace Slic3r::GUI {

class DeviceWebHost;

class wgtDeviceFilamentHotendWebDialog : public DPIDialog
{
public:
    explicit wgtDeviceFilamentHotendWebDialog(wxWindow* parent);

    void UpdateByMachine(Slic3r::MachineObject* obj);

private:
    void OnShow(wxShowEvent& event);
    void OnWebContentSizeChanged(const wxSize& content_size);
    wxSize ClampContentSize(const wxSize& content_size);
    bool IsCloseEnough(const wxSize& lhs, const wxSize& rhs);

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;

private:
    DeviceWebHost* m_device_web{ nullptr };
    Slic3r::MachineObject* m_obj{ nullptr };
};

} // namespace Slic3r::GUI
