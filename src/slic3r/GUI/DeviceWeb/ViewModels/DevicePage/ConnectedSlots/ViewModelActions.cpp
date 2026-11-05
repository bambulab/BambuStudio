/*****************************************************************//**
 * \file   ViewModelActions.cpp
 * \brief  Implementation of the Connected Slots tray actions.
 *********************************************************************/
#include "ViewModelActions.hpp"
#include "Schema.hpp"
#include "ViewModel.hpp"

#include "slic3r/GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/ResolverDeviceState.hpp"
#include "slic3r/GUI/DeviceWeb/ViewModels/DevicePage/DevicePageDialogHelpers.h"
#include "slic3r/GUI/AMSDryControl.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"

#include <cstdlib>
#include <optional>
#include "libslic3r/Log.hpp"

namespace Slic3r { namespace GUI {

namespace SK = ConnectedSlotsSchema::keys;

namespace {

/// Mid point of the tray's advertised nozzle range, -1 when it declares none.
int nozzle_temp_of(const std::optional<DevAmsTray>& tray)
{
    if (!tray || tray->nozzle_temp_min.empty() || tray->nozzle_temp_max.empty()) return -1;
    return (atoi(tray->nozzle_temp_min.c_str()) + atoi(tray->nozzle_temp_max.c_str())) / 2;
}

int current_nozzle_temp(MachineObject* obj)
{
    DevAmsTray* curr = obj ? obj->get_curr_tray() : nullptr;
    if (!curr || curr->nozzle_temp_min.empty() || curr->nozzle_temp_max.empty()) return -1;
    return (atoi(curr->nozzle_temp_min.c_str()) + atoi(curr->nozzle_temp_max.c_str())) / 2;
}

/**
 * \brief Publish the per-slot unload request.
 *
 * `command_ams_change_filament(false, ...)` always sends slot_id 255, which the
 * N-series firmware reads as "unload whatever is loaded". A real AMS slot needs
 * the 255-slot_id encoding so the right feeder retracts.
 */
void command_unload_slot(MachineObject* obj, const std::string& ams_id, const std::string& slot_id, int old_temp, int new_temp)
{
    if (!obj) return;
    nlohmann::json j;
    try {
        j["print"]["command"]     = "ams_change_filament";
        j["print"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
        j["print"]["curr_temp"]   = old_temp;
        j["print"]["tar_temp"]    = new_temp;
        j["print"]["ams_id"]      = atoi(ams_id.c_str());
        j["print"]["target"]      = 255;
        j["print"]["slot_id"]     = obj->GetFilaSystem()->IsVirtualTrayId(ams_id) ? 255 : 255 - atoi(slot_id.c_str());
        j["print"]["extruder_id"] = 0;
    } catch (const std::exception&) {
        BOOST_LOG_TRIVIAL(warning) << current_exception_message();
    }
    obj->publish_json(j);
}

} // namespace

nlohmann::json ConnectedSlotsActions::HandleTrayAction(ConnectedSlotsVM&     vm,
                                                       MachineObject*        obj,
                                                       const std::string&    action,
                                                       const nlohmann::json& payload)
{
    const std::string ams_id  = payload.value(SK::dry_action::ams_id, std::string());
    const std::string slot_id = payload.value(SK::tray_action::slot_id, std::string());

    if (!obj || !obj->GetFilaSystem() || ams_id.empty()) {
        return vm.MakeResponse(vm.GetModule(), "action", action, 1, "invalid action target", vm.BuildState());
    }

    if (action == "dry") {
        const DevAms* ams = obj->GetFilaSystem()->GetAmsById(ams_id);
        if (!ams || !ams->SupportDrying()) {
            return vm.MakeResponse(vm.GetModule(), "action", action, 1, "AMS does not support drying", vm.BuildState());
        }
        wxGetApp().CallAfter([ams_id]() { AMSDryCtrWin::RunModal(wxGetApp().mainframe, ams_id); });
        return vm.MakeResponse(vm.GetModule(), "action", action, 0, "", vm.BuildState());
    }

    if (slot_id.empty()) {
        return vm.MakeResponse(vm.GetModule(), "action", action, 1, "invalid tray action target", vm.BuildState());
    }

    if (action == "edit") {
        wxGetApp().CallAfter([vm_ptr = &vm, ams_id, slot_id]() {
            OpenAmsMaterialsSetting(ams_id, slot_id);
            vm_ptr->ReportState("state", "changed");
        });
        return vm.MakeResponse(vm.GetModule(), "action", action, 0, "", vm.BuildState());
    }

    if (action == "read") {
        const DevAms* ams = obj->GetFilaSystem()->GetAmsById(ams_id);
        if (!ams) return vm.MakeResponse(vm.GetModule(), "action", action, 1, "unknown ams", vm.BuildState());
        if (obj->is_enable_np || obj->is_enable_ams_np) {
            obj->command_ams_refresh_rfid2(std::stoi(ams_id), std::stoi(slot_id));
        } else {
            obj->command_ams_refresh_rfid(std::to_string(ams->GetTrayId(std::stoi(slot_id))));
        }
        return vm.MakeResponse(vm.GetModule(), "action", action, 0, "", vm.BuildState());
    }

    if (action == "load" || action == "unload") {
        if (DevicePageDeviceStateResolver::BlockIfBusy(obj)) {
            return vm.MakeResponse(vm.GetModule(), "action", action, 3,
                                   _u8L("The printer is busy on other job"), vm.BuildState());
        }
        const int new_temp = nozzle_temp_of(obj->get_tray(ams_id, slot_id));
        const int old_temp = current_nozzle_temp(obj);
        if (action == "load") {
            if (BlockLoadForUnknownFilament(obj, ams_id, slot_id)) {
                return vm.MakeResponse(vm.GetModule(), "action", action, 0, "", vm.BuildState());
            }
            obj->command_ams_change_filament(true, ams_id, slot_id, old_temp, new_temp, 0);
        } else {
            command_unload_slot(obj, ams_id, slot_id, old_temp, new_temp);
        }
        return vm.MakeResponse(vm.GetModule(), "action", action, 0, "", vm.BuildState());
    }

    return vm.MakeResponse(vm.GetModule(), "action", action, 1, "unknown tray action", vm.BuildState());
}

}} // namespace Slic3r::GUI
