/*****************************************************************//**
 * \file   ActionHandlerExtAms.cpp
 * \brief  External-spool `action` commands of the AMS filament hotend page.
 *
 * Pure dispatch: every branch hands off to the dialog that owns the work
 * (ExtFilamentEditDialog.cpp, ConnectedSlots/PageConnectedSlots.cpp) and then
 * answers with the refreshed page state.
 *********************************************************************/
#include "ActionHandlerExtAms.hpp"

#include "ExtFilamentEditDialog.hpp"
#include "ViewModel.hpp"

#include "slic3r/GUI/DeviceWeb/ViewModels/DevicePage/ConnectedSlots/PageConnectedSlots.hpp"
#include "slic3r/GUI/GUI_App.hpp"

namespace Slic3r { namespace GUI {

std::optional<nlohmann::json> DevicePageExtAmsActionHandler::TryHandle(DevicePageAmsFilamentHotendVM& vm,
                                                                       const std::string& action,
                                                                       const nlohmann::json& payload,
                                                                       MachineObject* /*machine_obj*/)
{
    if (action == "ams_operation") {
        const nlohmann::json action_payload = payload;
        wxGetApp().CallAfter([action_payload]() { open_connected_slots_dialog(action_payload); });
    } else if (action == "batch_filament_edit" || action == "batch_ext_edit") {
        const nlohmann::json action_payload = payload;
        wxGetApp().CallAfter([vm_ptr = &vm, action_payload]() { vm_ptr->OpenBatchFilamentEditDialog(action_payload); });
    } else if (action == "close_dialog") {
        wxGetApp().CallAfter([]() { close_ext_filament_edit_dialog(wxID_OK); });
    } else if (action == "close_dialog_after_run") {
        // Close with wxID_APPLY so the opener (main-page VM) knows the user
        // committed the load and can drop the page back to Idle.
        wxGetApp().CallAfter([]() { close_ext_filament_edit_dialog(wxID_APPLY); });
    } else {
        return std::nullopt;
    }

    return vm.MakeResponse(vm.GetModule(), "action", action, 0, "", vm.BuildState());
}

}} // namespace Slic3r::GUI
