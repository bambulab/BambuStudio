#pragma once

#include "slic3r/GUI/GUI_Utils.hpp"
#include <wx/timer.h>

namespace Slic3r {
class MachineObject;
} // namespace Slic3r

namespace Slic3r::GUI {

class DeviceWebHost;

// Beta-only sibling of wgtDeviceFilamentHotendWebDialog that hosts the native
// HTML/CSS/JS implementation of the same page instead of the React bundle.
// Both dialogs drive DeviceWebHostMode::DevicePageAmsFilamentHotend, so they
// share one ViewModel chain and can be compared side by side against the very
// same device state.
class wgtDeviceFilamentHotendHtmlDialog : public DPIDialog
{
public:
    explicit wgtDeviceFilamentHotendHtmlDialog(wxWindow* parent);

    // True when the app config opt-in is enabled. The entry button and every
    // call site must be gated on this so release paths stay untouched.
    static bool IsEnabled();

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
    wxTimer        m_poll_timer;
    bool           m_first_content_size_received{ false };
};

} // namespace Slic3r::GUI
