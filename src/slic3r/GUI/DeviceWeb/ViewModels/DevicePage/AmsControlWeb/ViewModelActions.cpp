#include "ViewModelActions.hpp"
#include "Schema.hpp"
#include "ViewModel.hpp"

#include "slic3r/GUI/DeviceWeb/ViewModels/DevicePage/DevicePageDialogHelpers.h"
#include "slic3r/GUI/AMSSetting.hpp"
#include "slic3r/GUI/AmsMappingPopup.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/DeviceTab/uiAmsHumidityPopup.h"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/DeviceCore/DevDefs.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevFilaSwitch.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevInfo.h"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/Widgets/AMSItem.hpp"

#include <boost/log/trivial.hpp>
#include <cstdlib>
#include <utility>
#include <vector>

namespace Slic3r { namespace GUI {

namespace SchemaKeys = AmsControlWebSchema::keys;

namespace {

void open_ams_settings()
{
    auto*          dev_mgr     = wxGetApp().getDeviceManager();
    MachineObject* machine_obj = dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
    if (!machine_obj) return;

    AMSSetting dlg(wxGetApp().mainframe, wxID_ANY);
    dlg.UpdateByObj(machine_obj);
    dlg.ShowModal();
}

int humidity_display_idx_of(const DevAms* ams)
{
    if (!ams) return -1;
    if (ams->GetAmsType() == DevAmsType::AMS) {
        const int level = ams->GetHumidityLevel();
        return (level > 0 && level < 6) ? level : -1;
    }
    const int percent = ams->GetHumidityPercent();
    if (percent < 0)       return -1;
    if (percent < 20)      return 5;
    if (percent < 40)      return 4;
    if (percent < 60)      return 3;
    if (percent < 80)      return 2;
    return 1;
}

// Same widget AMSControl pops for DevAmsType::AMS. PopupWindow must outlive
// Popup(), so keep one instance for the process.
void show_ams_level_humidity_tip(int humidity_value)
{
    wxWindow* parent = wxGetApp().mainframe;
    if (!parent) return;

    static AmsHumidityTipPopup* popup = nullptr;
    if (!popup)
        popup = new AmsHumidityTipPopup(parent);

    if (humidity_value > 0 && humidity_value <= 5)
        popup->set_humidity_level(humidity_value);

    popup->Layout();
    popup->Fit();
    wxGetApp().UpdateDarkUIWin(popup);

    const wxSize  sz     = popup->GetSize();
    const wxRect  screen = parent->GetScreenRect();
    const wxPoint pos(screen.x + (screen.width - sz.GetWidth()) / 2,
                      screen.y + (screen.height - sz.GetHeight()) / 2);
    popup->Position(pos, wxSize(0, 0));
    popup->Popup();
}

void open_humidity(const std::string& ams_id)
{
    auto*          dev_mgr     = wxGetApp().getDeviceManager();
    MachineObject* machine_obj = dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
    if (!machine_obj || ams_id.empty()) return;

    auto fila_system = machine_obj->GetFilaSystem();
    DevAms* ams = fila_system ? fila_system->GetAmsById(ams_id) : nullptr;
    if (!ams) return;

    const DevAmsType type = ams->GetAmsType();
    // Mirrors AMSControl::EVT_AMS_SHOW_HUMIDITY_TIPS: classic AMS uses the
    // A/B/C/D/E droplet tip; N3 with remote dry uses the dryer window;
    // everything else uses the percent / remaining-time dialog.
    if (type == DevAmsType::AMS) {
        show_ams_level_humidity_tip(humidity_display_idx_of(ams));
        return;
    }

    if (machine_obj->is_support_remote_dry &&
        (type == DevAmsType::N3F || type == DevAmsType::N3S)) {
        OpenAmsDryControlDialog(machine_obj, ams_id);
        return;
    }

    uiAmsHumidityInfo info;
    info.ams_id               = ams_id;
    info.ams_type             = type;
    info.humidity_display_idx = humidity_display_idx_of(ams);
    info.humidity_percent     = ams->GetHumidityPercent();
    info.left_dry_time        = ams->GetLeftDryTime();
    info.current_temperature  = ams->GetCurrentTemperature();

    uiAmsPercentHumidityDryPopup dlg(wxGetApp().mainframe);
    dlg.Update(&info);
    dlg.ShowModal();
}

// Temperature the classic load / unload commands carry, -1 when unknown.
int nozzle_temp_mid(const DevAmsTray* tray)
{
    if (!tray || tray->nozzle_temp_min.empty() || tray->nozzle_temp_max.empty()) return -1;
    return (atoi(tray->nozzle_temp_min.c_str()) + atoi(tray->nozzle_temp_max.c_str())) / 2;
}

// With the filament switch installed the target extruder is ambiguous; the user
// picks it in a modal dialog. Returns false when the load must not go ahead.
bool resolve_load_extruder(MachineObject*      machine_obj,
                          const std::string&  ams_id,
                          const std::string&  slot_id,
                          std::optional<int>& extruder_id)
{
    auto fila_switch = machine_obj->GetFilaSwitch();
    if (!fila_switch || !fila_switch->IsInstalled()) return true;

    if (!fila_switch->IsReady()) {
        MessageDialog msg_dlg(nullptr, _L("The Filament Track Switch has not been setup. Please setup on printer."),
                              wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return false;
    }

    std::vector<std::pair<std::string, std::string>> extruder_slots(2, {"", ""});
    if (auto extder_system = machine_obj->GetExtderSystem()) {
        for (const int id : {MAIN_EXTRUDER_ID, DEPUTY_EXTRUDER_ID}) {
            const auto ext = extder_system->GetExtderById(id);
            if (ext.has_value() && ext->HasFilamentInExt())
                extruder_slots[id] = {ext->GetSlotNow().ams_id, ext->GetSlotNow().slot_id};
        }
    }

    FeedDirectionDialog dialog(nullptr, 2, machine_obj->printer_type);
    dialog.SetExtruderMapping(machine_obj, ams_id, slot_id, extruder_slots);
    if (dialog.ShowModal() != wxID_OK) return false;

    extruder_id = dialog.GetExtruderID();
    return true;
}

// Must run on the UI thread: resolve_load_extruder is modal.
void command_load(MachineObject* machine_obj, const std::string& ams_id, const std::string& slot_id)
{
    std::optional<int> extruder_id;
    if (!resolve_load_extruder(machine_obj, ams_id, slot_id, extruder_id)) return;

    const auto target_tray = machine_obj->get_tray(ams_id, slot_id);
    const int  new_temp    = nozzle_temp_mid(target_tray ? &target_tray.value() : nullptr);
    const int  old_temp    = nozzle_temp_mid(machine_obj->get_curr_tray());

    if (devPrinterUtil::IsVirtualSlot(ams_id)) {
        // Legacy protocol addresses the ext spool by the fixed 254 id.
        const bool        np_protocol = machine_obj->is_enable_np || machine_obj->is_enable_ams_np;
        const std::string vt_ams_id   = np_protocol ? ams_id : std::string("254");
        machine_obj->command_ams_change_filament(true, vt_ams_id, "0", old_temp, new_temp, extruder_id);
        return;
    }

    machine_obj->command_ams_change_filament(true, ams_id, slot_id, old_temp, new_temp, extruder_id);
}

// Slot 255 marks the unload target. On the new protocol the slot must already
// be sitting in an extruder.
void command_unload(MachineObject* machine_obj, const std::string& ams_id, const std::string& slot_id)
{
    if (!machine_obj->is_enable_np) {
        machine_obj->command_ams_change_filament(false, ams_id, "255");
        return;
    }

    auto extder_system = machine_obj->GetExtderSystem();
    if (!extder_system) return;

    for (const auto& ext : extder_system->GetExtruders()) {
        if (ext.GetSlotNow().ams_id == ams_id && ext.GetSlotNow().slot_id == slot_id) {
            machine_obj->command_ams_change_filament(false, ams_id, "255");
            return;
        }
    }
}

void command_read_slot(MachineObject* machine_obj, const std::string& ams_id, const std::string& slot_id)
{
    auto fila_system = machine_obj->GetFilaSystem();
    if (!fila_system || devPrinterUtil::IsVirtualSlot(ams_id)) return;

    DevAms* ams = fila_system->GetAmsById(ams_id);
    if (!ams || ams->GetTrays().count(slot_id) == 0) {
        BOOST_LOG_TRIVIAL(trace) << "ams control web: read slot " << ams_id << ":" << slot_id << " not found";
        return;
    }

    // Mirror StatusPanel::on_ams_refresh_rfid: the reader cannot work while
    // filament sits at the toolhead, so block with the same dialog instead of
    // silently dropping the command.
    bool has_filament_at_extruder = false;
    if (machine_obj->is_enable_np || machine_obj->is_enable_ams_np) {
        auto extder_system = machine_obj->GetExtderSystem();
        auto current_extruder_id = ams->GetCurrentExtruderId();
        if (extder_system && current_extruder_id.has_value())
            has_filament_at_extruder = extder_system->HasFilamentInExt(current_extruder_id.value());
    } else {
        has_filament_at_extruder = machine_obj->is_filament_at_extruder();
    }
    if (has_filament_at_extruder) {
        // Modal dialogs must run on the UI thread; the bridge may call us inline.
        wxGetApp().CallAfter([]() {
            MessageDialog msg_dlg(nullptr, _L("Cannot read filament info: the filament is loaded to the toolhead, please unload the filament and try again."), wxEmptyString,
                                  wxICON_WARNING | wxYES);
            msg_dlg.ShowModal();
        });
        return;
    }

    try {
        if (machine_obj->is_enable_np || machine_obj->is_enable_ams_np)
            machine_obj->command_ams_refresh_rfid2(std::stoi(ams_id), std::stoi(slot_id));
        else
            machine_obj->command_ams_refresh_rfid(std::to_string(ams->GetTrayId(std::stoi(slot_id))));
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "ams control web: read slot failed: " << e.what();
    }
}

} // namespace

std::optional<nlohmann::json> AmsControlWebActionHandler::TryHandle(DevicePageAmsControlWebVM& vm,
                                                                   const std::string&        action,
                                                                   const nlohmann::json&     payload,
                                                                   MachineObject*            machine_obj)
{
    const std::string ams_id  = payload.value("ams_id", std::string());
    const std::string slot_id = payload.value("slot_id", std::string());

    const auto respond = [&vm, &action](int code, const std::string& message) {
        return vm.MakeResponse(vm.GetModule(), "action", action, code, message, vm.BuildState());
    };

    if (action == "select_slot") {
        vm.SetSelectedSlot(ams_id, slot_id);
        vm.PromoteActiveAms(ams_id);
        return respond(0, "");
    }

    if (action == "switch_ams") {
        // Switching the open unit drops the pick so it cannot name a hidden slot.
        vm.PromoteActiveAms(ams_id);
        vm.ClearSelectedSlot();
        return respond(0, "");
    }

    if (action == "dismiss_filament_mgr_hint") {
        DevicePageAmsControlWebVM::DismissFilamentMgrHint(ams_id, slot_id);
        vm.InvalidateReportCache();
        return respond(0, "");
    }

    const bool is_device_action = action == "settings" || action == "load" ||
                                  action == "unload" || action == "edit_slot" || action == "read_slot" ||
                                  action == "view_slot" || action == "open_filament_mgr_hint" ||
                                  action == "open_humidity" || action == "auto_refill";
    if (!is_device_action) return std::nullopt;

    if (!machine_obj) return respond(3, _u8L("Please select a printer"));

    if (action == "settings") {
        wxGetApp().CallAfter([]() { open_ams_settings(); });
        return respond(0, "");
    }

    if (action == "auto_refill") {
        wxGetApp().CallAfter([]() { OpenAmsAutoRefillDialog(); });
        return respond(0, "");
    }

    if (action == "edit_slot") {
        if (ams_id.empty() || slot_id.empty()) return respond(1, "edit_slot needs ams_id and slot_id");
        wxGetApp().CallAfter([vm_ptr = &vm, ams_id, slot_id]() {
            OpenAmsMaterialsSetting(ams_id, slot_id);
            vm_ptr->ReportState("state", "changed");
        });
        return respond(0, "");
    }

    if (action == "read_slot") {
        if (ams_id.empty() || slot_id.empty()) return respond(1, "read_slot needs ams_id and slot_id");
        command_read_slot(machine_obj, ams_id, slot_id);
        return respond(0, "");
    }

    if (action == "view_slot") {
        if (ams_id.empty() || slot_id.empty()) return respond(1, "view_slot needs ams_id and slot_id");
        const bool view_only = machine_obj->GetInfo() && !machine_obj->GetInfo()->IsFdmMode();
        wxGetApp().CallAfter([vm_ptr = &vm, ams_id, slot_id, view_only]() {
            OpenAmsMaterialsSetting(ams_id, slot_id, view_only);
            vm_ptr->ReportState("state", "changed");
        });
        return respond(0, "");
    }

    if (action == "open_filament_mgr_hint") {
        if (ams_id.empty() || slot_id.empty())
            return respond(1, "open_filament_mgr_hint needs ams_id and slot_id");
        wxGetApp().CallAfter([ams_id, slot_id]() {
            wxGetApp().open_new_official_filament_hint(ams_id, slot_id);
        });
        return respond(0, "");
    }

    if (action == "open_humidity") {
        if (ams_id.empty()) return respond(1, "open_humidity needs ams_id");
        wxGetApp().CallAfter([ams_id]() { open_humidity(ams_id); });
        return respond(0, "");
    }

    const bool           is_load = action == "load";
    const nlohmann::json state   = vm.BuildState();
    const nlohmann::json gate    = state.value(SchemaKeys::actions, nlohmann::json::object());
    const char*          can_key = is_load ? AmsControlWebSchema::actions::can_load
                                           : AmsControlWebSchema::actions::can_unload;
    const char* tips_key = is_load ? AmsControlWebSchema::actions::load_tips
                                   : AmsControlWebSchema::actions::unload_tips;
    if (!gate.value(can_key, false))
        return vm.MakeResponse(vm.GetModule(), "action", action, 3, gate.value(tips_key, std::string()), state);

    const std::string target_ams_id  = ams_id.empty() ? vm.SelectedAmsId() : ams_id;
    const std::string target_slot_id = slot_id.empty() ? vm.SelectedSlotId() : slot_id;
    if (target_ams_id.empty()) return respond(1, "no slot selected");

    if (is_load) {
        // Re-resolve the machine after the modal: the user may have switched printers.
        wxGetApp().CallAfter([vm_ptr = &vm, target_ams_id, target_slot_id]() {
            auto* dev_mgr = wxGetApp().getDeviceManager();
            if (MachineObject* obj = dev_mgr ? dev_mgr->get_selected_machine() : nullptr)
                command_load(obj, target_ams_id, target_slot_id);
            vm_ptr->ReportState("state", "changed");
        });
    } else {
        command_unload(machine_obj, target_ams_id, target_slot_id);
    }

    return respond(0, "");
}

}} // namespace Slic3r::GUI
