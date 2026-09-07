#include "ResolverDeviceState.hpp"

#include "slic3r/GUI/DeviceCore/DevDefs.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/MsgDialog.hpp"

#include <wx/thread.h>

namespace Slic3r { namespace GUI {

bool DevicePageDeviceStateResolver::IsBusy(MachineObject* machine_obj)
{
    if (!machine_obj) return false;

    if (machine_obj->is_in_printing()) return true;
    if (machine_obj->is_in_calibration()) return true;
    if (machine_obj->is_in_extrusion_cali()) return true;
    if (machine_obj->is_in_upgrading()) return true;
    return false;
}

bool DevicePageDeviceStateResolver::BlockIfBusy(MachineObject* machine_obj)
{
    if (!IsBusy(machine_obj)) return false;

    // Always deferred: every caller is a bridge action handler running on the
    // main thread from the WebView2 message callback, and a modal event loop
    // started there re-enters that callback and crashes. Callers only use the
    // return value to abort, so they do not need the prompt to be synchronous.
    wxGetApp().CallAfter([]() {
        MessageDialog dlg(wxGetApp().mainframe,
                          _L("The printer is busy on other job"),
                          _L("Error"),
                          wxICON_WARNING | wxOK);
        dlg.ShowModal();
    });
    return true;
}

}} // namespace Slic3r::GUI
