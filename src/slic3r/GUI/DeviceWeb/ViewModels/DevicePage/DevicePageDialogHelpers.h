#ifndef DEVICEPAGEDIALOGHELPERS_H
#define DEVICEPAGEDIALOGHELPERS_H

#include <optional>
#include <string>
#include <vector>

#include <wx/colour.h>

namespace Slic3r {

class MachineObject;

namespace GUI {

// Snapshot of the filament the user just applied in AMSMaterialsSetting. Lets
// the send-to-print page sync the edited slot without waiting for the device
// echo (the dialog writes to the device asynchronously).
struct EditedFilamentInfo {
    std::string           ams_id;
    std::string           slot_id;
    std::string           filament_type;
    std::string           filament_id;
    std::string           setting_id;
    wxColour              color;
    std::vector<wxColour> colors;
    int                   ctype = 0;
};

// Open the native AMS material-settings dialog (AMSMaterialsSetting) for the
// tray identified by (ams_id, slot_id) on the currently selected machine.
//
// Shared by the device-page filament/hotend view-model and the send-to-print
// AMS-mapping view-model so both edit paths use the exact same dialog setup.
// Must run on the UI thread (the caller is responsible for wxGetApp().CallAfter).
//
// Returns the applied filament info only when the user confirmed the dialog;
// std::nullopt on cancel or when the dialog could not be shown.
// `view_only` locks the dialog (2D laser/cut mode).
std::optional<EditedFilamentInfo> OpenAmsMaterialsSetting(const std::string& ams_id, const std::string& slot_id, bool view_only = false);

// AmsReplaceMaterialDialog ("Auto Refill"). Same popup AMSControl posts via
// EVT_AMS_FILAMENT_BACKUP. Must run on the UI thread.
void OpenAmsAutoRefillDialog();

// AMSDryCtrWin ("Drying") for the N3F / N3S unit `ams_id`. Runs modal
void OpenAmsDryControlDialog(MachineObject* machine_obj, const std::string& ams_id);

}} // namespace Slic3r::GUI

#endif // DEVICEPAGEDIALOGHELPERS_H
