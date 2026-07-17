#pragma once

#include "slic3r/GUI/GUI_Utils.hpp"

namespace Slic3r {
class MachineObject;
} // namespace Slic3r

namespace Slic3r::GUI {

class DeviceWebHost;

// Auto-refill management dialog for the RefillVersion::RefillInfo variant.
// Hosts the React `auto_refill_refill_info` page inside a DeviceWebHost. The
// legacy FilaBackup variant keeps its own native AmsReplaceMaterialDialog and
// is routed here only via the shared entry factory.
class wgtDeviceAutoRefillWebDialog : public DPIDialog
{
public:
    explicit wgtDeviceAutoRefillWebDialog(wxWindow* parent);

    void UpdateByMachine(Slic3r::MachineObject* obj);

private:
    void OnShow(wxShowEvent& event);
    void OnWebContentSizeChanged(const wxSize& content_size);
    void PushSnapshot();
    wxSize ClampContentSize(const wxSize& content_size);
    bool IsCloseEnough(const wxSize& lhs, const wxSize& rhs);

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;

private:
    DeviceWebHost* m_device_web{ nullptr };
    Slic3r::MachineObject* m_obj{ nullptr };
};

// Open the auto-refill management dialog for `obj`, branching on refill
// version: RefillInfo opens the new web dialog above, FilaBackup keeps the
// legacy native AmsReplaceMaterialDialog. Shared by every auto-refill entry
// point so new and old UIs never mix.
void ShowAutoRefillDialog(wxWindow* parent, Slic3r::MachineObject* obj);

} // namespace Slic3r::GUI
