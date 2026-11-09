#include "ViewModel.hpp"
#include "ActionHandlerFilament.hpp"
#include "BuilderFilamentChangeStatus.hpp"
#include "FilamentManualPullDialog.hpp"
#include "ResolverDeviceState.hpp"
#include "ResolverSlotState.hpp"
#include "slic3r/GUI/DeviceWeb/ViewModels/DevicePage/DevicePageDialogHelpers.h"

#include "slic3r/GUI/AMSDryControl.hpp"
#include "slic3r/GUI/AMSMaterialsSetting.hpp"
#include "slic3r/GUI/AMSSetting.hpp"
#include "slic3r/GUI/EncodedFilament.hpp"
#include "slic3r/GUI/AmsMappingPopup.hpp"
#include "slic3r/GUI/DeviceTab/wgtDeviceAutoRefillWebDialog.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/DeviceCore/DevCtrlToken.h"
#include "slic3r/GUI/DeviceCore/DevDefs.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <vector>
#include <boost/format.hpp>
#include <wx/timer.h>
#include "libslic3r/Log.hpp"

namespace Slic3r { namespace GUI {

namespace {

std::string normalize_hex_for_web(const std::string& raw)
{
    if (raw.empty()) return "#D9D9D9";
    std::string hex = raw;
    if (hex[0] != '#') hex = "#" + hex;
    if (hex.size() > 9) hex = hex.substr(0, 9);
    return hex;
}

std::string color_type_for_web(DevFilaColorType color_type)
{
    switch (color_type) {
    case DevFilaColorType::CTYPE_MULTI:    return "multi";
    case DevFilaColorType::CTYPE_GRADIANT: return "gradient";
    case DevFilaColorType::CTYPE_SINGLE:   return "single";
    }
    return "single";
}

const char* dry_status_short_name(Slic3r::DevAms::DryStatus status)
{
    switch (status) {
    case Slic3r::DevAms::DryStatus::Off:                         return "Idle";
    case Slic3r::DevAms::DryStatus::Checking:                    return "Checking";
    case Slic3r::DevAms::DryStatus::Drying:                      return "Drying";
    case Slic3r::DevAms::DryStatus::Cooling:                     return "Cooling";
    case Slic3r::DevAms::DryStatus::Stopping:                    return "Stopping";
    case Slic3r::DevAms::DryStatus::Error:                       return "Error";
    case Slic3r::DevAms::DryStatus::CannotStopHeatOutofControl:  return "Heat fault";
    case Slic3r::DevAms::DryStatus::PrdTesting:                  return "Self test";
    }
    return "Idle";
}

// How long to wait for the RFID result before judging the slot as "nothing read".
constexpr int RFID_READ_RESULT_DELAY_MS_1 = 2500;
constexpr int RFID_READ_RESULT_DELAY_MS_2 = 1000;

// The read result lands in a later push_status, not in the command reply, so the outcome
// is judged from the freshest slot snapshot. The token is only captured to keep it alive
// until then, otherwise DevCtrlTokenMgr may drop it while the timer is still pending.
void check_external_tpu_rfid_read_result_later(const std::string&            dev_id,
                                               const std::string&            ams_id,
                                               const std::string&            slot_id,
                                               std::shared_ptr<DevCtrlToken> token)
{
    if (!token) return;

    token->SetCallback(DevCtrlTokenStatus::ReplyReceived, [dev_id, ams_id, slot_id](const std::shared_ptr<DevCtrlToken>& settled, DevCtrlTokenStatus status) {
        if (status != DevCtrlTokenStatus::ReplyReceived) {
            return;
        }

        auto* timer = new wxTimer();
        timer->Bind(wxEVT_TIMER, [timer, dev_id, ams_id, slot_id, settled](wxTimerEvent&) {
            timer->Stop();
            auto* dev_mgr = wxGetApp().getDeviceManager();
            MachineObject* obj = dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
            if (obj && obj->get_dev_id() == dev_id) {
                auto tray = obj->GetFilaSystem()->GetVirtualTray(ams_id);
                if (tray.has_value()) {
                    if (auto nozzle_opt = tray->get_nozzle(obj); nozzle_opt.has_value()) {
                        if (auto related_ams_slot_opt = nozzle_opt->GetRelatedAmsSlot(); related_ams_slot_opt.has_value()) {
                            if (auto related_tray_opt = obj->get_tray(related_ams_slot_opt->first, related_ams_slot_opt->second); related_tray_opt.has_value()) {
                                if (!related_tray_opt->is_reading_done(obj)) {
                                    timer->StartOnce(RFID_READ_RESULT_DELAY_MS_2);
                                    return;
                                }
                            }
                        }
                    }
                    if (tray->setting_id.empty() && tray->m_fila_type.empty()) {
                        MessageDialog dlg(wxGetApp().mainframe, _L("No filament information available"), _L("Tip"), wxOK);
                        dlg.ShowModal();
                    }
                }
            }

            wxGetApp().CallAfter([timer]() {
                delete timer;
            });
        });

        timer->StartOnce(RFID_READ_RESULT_DELAY_MS_1);
    });
}

void open_ams_materials_setting(const std::string& ams_id, const std::string& slot_id)
{
    auto* dev_mgr = wxGetApp().getDeviceManager();
    MachineObject* machine_obj = dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
    if (!machine_obj || ams_id.empty() || slot_id.empty()) return;
    if (!machine_obj->get_tray(ams_id, slot_id).has_value()) return;

    auto tray = machine_obj->get_tray(ams_id, slot_id).value();

    int ams_id_int = 0;
    int slot_id_int = 0;
    try {
        ams_id_int = std::stoi(ams_id);
        slot_id_int = std::stoi(slot_id);
    } catch (...) {
        BOOST_LOG_TRIVIAL(warning) << current_exception_message(); return; }

    wxWindow* parent = wxGetApp().mainframe;

    wxColor color = DevAmsTray::decode_color(tray.color);
    std::vector<wxColour> cols;
    for (const auto& col : tray.cols) cols.push_back(DevAmsTray::decode_color(col));

    wxString k_val = wxString::Format("%.3f", tray.k);
    wxString n_val = wxString::Format("%.3f", tray.n);

    std::string sn_number;
    if (tray.tag_uid.size() == 16 && tray.tag_uid.substr(12, 2) == "01") {
        sn_number = tray.tag_uid;
    }

    wxString total_weight;
    wxString remain_weight;
    if (!tray.setting_id.empty()) {
        if (tray.weight != "-1" && tray.weight != "0") {
            total_weight = wxString::FromUTF8(tray.weight.c_str());
        }

        if (auto weight = tray.get_filament_remain_weight(); weight.has_value() && weight.value() >= 0) {
            remain_weight = wxString::Format("%d", *weight);
        }
    }

    AMSMaterialsSetting dlg(parent, wxID_ANY);
    dlg.obj = machine_obj;
    dlg.ams_id = ams_id_int;
    dlg.slot_id = slot_id_int;
    dlg.m_is_third = !DevFilaSystem::IsBBL_Filament(tray.tag_uid);

    std::string filament;
    std::string temp_max;
    std::string temp_min;
    if (!dlg.m_is_third) {
        // Official RFID filament: identity fields come from the tray for the read-only card.
        filament = tray.sub_brands;
        temp_max = tray.nozzle_temp_max;
        temp_min = tray.nozzle_temp_min;
    }

    dlg.set_ctype(tray.ctype);
    dlg.ams_filament_id = tray.setting_id;
    if (dlg.ams_filament_id.empty()) dlg.set_empty_color(color);
    else {
        dlg.set_color(color);
        dlg.set_colors(cols);
    }

    dlg.Popup(filament, sn_number, temp_min, temp_max, k_val, n_val, total_weight, remain_weight);
}

void open_ams_settings()
{
    auto* dev_mgr = wxGetApp().getDeviceManager();
    MachineObject* machine_obj = dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
    if (!machine_obj) return;

    wxWindow* parent = wxGetApp().mainframe;
    AMSSetting dlg(parent, wxID_ANY);
    dlg.UpdateByObj(machine_obj);
    dlg.ShowModal();
}

void open_auto_refill()
{
    auto* dev_mgr = wxGetApp().getDeviceManager();
    MachineObject* machine_obj = dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
    if (!machine_obj) return;

    wxWindow* parent = wxGetApp().mainframe;
    ShowAutoRefillDialog(parent, machine_obj);
}

} // namespace

nlohmann::json DevicePageAmsFilamentHotendVM::BuildAmsUnitJson(MachineObject* machine_obj, const DevAms* ams_unit) const
{
    namespace SK = AmsHotendSchema::keys;
    namespace SV = AmsHotendSchema::values;

    nlohmann::json trays = nlohmann::json::array();
    if (!ams_unit) {
        return {
            {SK::ams_unit::ams_id,       ""},
            {SK::ams_unit::ams_type,     ""},
            {SK::ams_unit::slot_count,   0},
            {SK::ams_unit::humidity_raw, -1},
            {SK::ams_unit::dry_status,   ""},
            {SK::ams_unit::trays,        trays}
        };
    }

    for (const auto& [tray_id, tray] : ams_unit->GetTrays()) {
        if (!tray) continue;

        std::vector<std::string> colors = tray->cols;
        if (colors.empty() && !tray->color.empty()) colors.push_back(tray->color);
        for (auto& c : colors) c = normalize_hex_for_web(c);

        std::string tray_state = SV::tray_state::ready;
        if (!tray->is_exists) tray_state = SV::tray_state::empty;
        else if (tray->is_reading(machine_obj)) tray_state = SV::tray_state::reading;
        else if (tray->setting_id.empty() || tray->m_fila_type.empty()) tray_state = SV::tray_state::unknown;

        const std::string tray_ams_id = ams_unit->GetAmsId();
        const bool is_reading_now = tray->is_reading(machine_obj);
        const bool tray_is_loaded = DevicePageFilamentSlotStateResolver::IsTraySlotLoadedInNozzle(machine_obj, tray);
        const bool tray_has_filament = tray->is_exists && tray_state != SV::tray_state::empty;
        const char* tray_read_label = (tray_state == SV::tray_state::unknown) ? SV::read_label::read : SV::read_label::reread; // Unknown filament type shows "Read"; known type shows "Reread".
        const nlohmann::json tray_actions = {
            {SK::tray_actions::can_read,   tray->is_exists && !is_reading_now},
            {SK::tray_actions::read_label, tray_read_label},
            {SK::tray_actions::can_load,   tray_has_filament && !tray_is_loaded && !is_reading_now},
            {SK::tray_actions::can_unload, tray_is_loaded && !is_reading_now}
        };

        trays.push_back({
            {SK::ams_tray::ams_id,        tray_ams_id},
            {SK::ams_tray::slot_id,       tray_id},
            {SK::ams_tray::exists,        tray->is_exists},
            {SK::ams_tray::tray_state,    tray_state},
            {SK::ams_tray::filament_type, tray->m_fila_type},
            {SK::ams_tray::filament_name, ResolveFilamentCardText(tray, tray->m_fila_type)},
            {SK::ams_tray::color,         normalize_hex_for_web(tray->color)},
            {SK::ams_tray::color_type,    color_type_for_web(tray->ctype)},
            {SK::ams_tray::colors,        colors},
            {SK::ams_tray::remain,        DevicePageFilamentSlotStateResolver::ResolveDisplayRemain(tray)},
            {SK::ams_tray::actions,       tray_actions}
        });
    }

    return {
        {SK::ams_unit::ams_id,       ams_unit->GetAmsId()},
        {SK::ams_unit::ams_type,     std::to_string((int)ams_unit->GetAmsType())},
        {SK::ams_unit::slot_count,   ams_unit->GetSlotCount()},
        {SK::ams_unit::humidity_raw, ams_unit->SupportHumidityPercent() ? ams_unit->GetHumidityPercent() : -1},
        {SK::ams_unit::dry_status,   ams_unit->GetDryStatus().has_value() ? dry_status_short_name(*ams_unit->GetDryStatus()) : ""},
        {SK::ams_unit::trays,        trays}
    };
}

bool DevicePageAmsFilamentHotendVM::IsCurrentFilamentChangingSlot(MachineObject* machine_obj, const std::string& ams_id, const std::string& slot_id) const
{
    if (!machine_obj || ams_id.empty() || slot_id.empty()) return false;
    auto fila_system = machine_obj->GetFilaSystem();
    if (!fila_system) return false;
    try {
        return fila_system->IsCurrentFilamentChangingSlot(std::stoi(ams_id), std::stoi(slot_id));
    } catch (...) {
        BOOST_LOG_TRIVIAL(warning) << current_exception_message();
        return false;
    }
}

bool DevicePageAmsFilamentHotendVM::IsFilamentChangeActive(MachineObject* machine_obj) const
{
    return DevicePageFilamentChangeStatusBuilder::IsActive(machine_obj);
}

static void s_command_unload_filament(MachineObject* obj_, const std::string& ams_id, const std::string& slot_id, int old_temp, int new_temp, std::optional<int> extruder_id)
{
    if (obj_) {
        json j;
        try {
            int ams_id_int = atoi(ams_id.c_str());
            int slot_id_int = atoi(slot_id.c_str());
            j["print"]["command"] = "ams_change_filament";
            j["print"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
            j["print"]["curr_temp"] = old_temp;
            j["print"]["tar_temp"] = new_temp;
            j["print"]["ams_id"] = ams_id_int;
            j["print"]["target"] = 255;

            if (obj_->GetFilaSystem()->IsVirtualTrayId(ams_id)) {
                j["print"]["slot_id"] = 255; // the new protocol to mark unload
            } else {
                j["print"]["slot_id"] = 255 - slot_id_int;
            }

            if (extruder_id.has_value()) {
                j["print"]["extruder_id"] = *extruder_id;
            }

        } catch (const std::exception&) {
            BOOST_LOG_TRIVIAL(warning) << current_exception_message();
        }

        obj_->publish_json(j);
    }
}

std::optional<nlohmann::json> DevicePageFilamentActionHandler::TryHandle(DevicePageAmsFilamentHotendVM& vm,
                                                                         const std::string& action,
                                                                         const nlohmann::json& payload,
                                                                         MachineObject* machine_obj)
{
    const std::string ams_id = payload.value("ams_id", "");
    const std::string slot_id = payload.value("slot_id", "");
    auto fila_system = machine_obj->GetFilaSystem();

    if (action == "settings") {
        wxGetApp().CallAfter([]() { open_ams_settings(); });
    } else if (action == "auto_refill") {
        wxGetApp().CallAfter([]() { open_auto_refill(); });
    } else if (action == "read_all_slots_rfid") {
        // Rack-B "read all": refresh every slot's RFID on each connected
        // ZMULTI_B AMS. mode=1 (active, all slots); no slot_bits.
        if (machine_obj->is_in_printing() || machine_obj->is_in_upgrading()) {
            return vm.MakeResponse(vm.GetModule(), "action", action, 3,
                _u8L("Cannot read all while printing or upgrading"), vm.BuildState());
        }
        if (fila_system) {
            for (const auto& [_, ams_unit] : fila_system->GetAmsList()) {
                if (ams_unit && ams_unit->GetAmsType() == DevAmsType::ZMULTI_B) {
                    machine_obj->command_ams_read_all(ams_unit->GetAmdIdInt(), 1);
                }
            }
        }
    } else if (action == "read" && fila_system && !ams_id.empty() && !slot_id.empty()) {
        try {
            if (machine_obj->is_enable_np || machine_obj->is_enable_ams_np) {
                if (auto vt_tray = machine_obj->GetFilaSystem()->GetVirtualTray(ams_id); vt_tray.has_value() && vt_tray->CanReadExternalRFID_TPU(machine_obj)) {
                    if (auto nozzle_opt = vt_tray->get_nozzle(machine_obj); nozzle_opt.has_value() && !nozzle_opt->GetPreloadBindAmsSlots().empty()) {
                        wxGetApp().CallAfter([]() {
                            MessageDialog dlg(wxGetApp().mainframe, _L("Unable to read filament information.\nFilament is currently loaded in the feeding inlet. Please pull it out first and try again."), _L("Tip"), wxOK);
                            dlg.ShowModal();
                        });
                        return vm.MakeResponse(vm.GetModule(), "action", action, 0, "", vm.BuildState());
                    }
                }

                auto token = machine_obj->command_ams_refresh_rfid2(std::stoi(ams_id), std::stoi(slot_id));
                if (auto vt_tray = machine_obj->GetFilaSystem()->GetVirtualTray(ams_id); vt_tray.has_value() && vt_tray->CanReadExternalRFID_TPU(machine_obj)) {
                    if (token && token->GetStatus() != DevCtrlTokenStatus::SendFailed) {
                        check_external_tpu_rfid_read_result_later(machine_obj->get_dev_id(), ams_id, slot_id, std::move(token));
                    }
                };
            }
        } catch (const std::exception& e) {
            return vm.MakeResponse(vm.GetModule(), "action", action, 2, e.what(), vm.BuildState());
        }
    } else if (action == "edit" && fila_system && !ams_id.empty() && !slot_id.empty()) {
        wxGetApp().CallAfter([vm_ptr = &vm, ams_id, slot_id]() {
            open_ams_materials_setting(ams_id, slot_id);
            vm_ptr->ReportState("state", "changed");
        });
    } else if (action == "open_filament_mgr_hint" && !ams_id.empty() && !slot_id.empty()) {
        wxGetApp().CallAfter([ams_id, slot_id]() {
            wxGetApp().open_new_official_filament_hint(ams_id, slot_id);
        });
    } else if (action == "load" && fila_system && !ams_id.empty() && !slot_id.empty()) {
        if (DevicePageDeviceStateResolver::BlockIfBusy(machine_obj)) {
            return vm.MakeResponse(vm.GetModule(), "action", action, 3,
                _u8L("The printer is busy on other job"), vm.BuildState());
        }
        if (BlockLoadForUnknownFilament(machine_obj, ams_id, slot_id)) {
            return vm.MakeResponse(vm.GetModule(), "action", action, 0, "", vm.BuildState());
        }
        try {
            int old_temp = -1;
            int new_temp = -1;
            auto target_tray = machine_obj->get_tray(ams_id, slot_id);
            if (target_tray && !target_tray->nozzle_temp_max.empty() && !target_tray->nozzle_temp_min.empty()) {
                new_temp = (atoi(target_tray->nozzle_temp_min.c_str()) + atoi(target_tray->nozzle_temp_max.c_str())) / 2;
            }
            if (DevAmsTray* curr_tray = machine_obj->get_curr_tray()) {
                if (!curr_tray->nozzle_temp_max.empty() && !curr_tray->nozzle_temp_min.empty()) {
                    old_temp = (atoi(curr_tray->nozzle_temp_min.c_str()) + atoi(curr_tray->nozzle_temp_max.c_str())) / 2;
                }
            }
            machine_obj->command_ams_change_filament(true, ams_id, slot_id, old_temp, new_temp, 0);
        } catch (const std::exception& e) {
            return vm.MakeResponse(vm.GetModule(), "action", action, 2, e.what(), vm.BuildState());
        }
    } else if (action == "unload" && fila_system && !ams_id.empty() && !slot_id.empty()) {
        if (DevicePageDeviceStateResolver::BlockIfBusy(machine_obj)) {
            return vm.MakeResponse(vm.GetModule(), "action", action, 3,
                _u8L("The printer is busy on other job"), vm.BuildState());
        }
        try {
            int old_temp = -1;
            int new_temp = -1;
            auto target_tray = machine_obj->get_tray(ams_id, slot_id);
            if (target_tray && !target_tray->nozzle_temp_max.empty() && !target_tray->nozzle_temp_min.empty()) {
                new_temp = (atoi(target_tray->nozzle_temp_min.c_str()) + atoi(target_tray->nozzle_temp_max.c_str())) / 2;
            }
            if (DevAmsTray* curr_tray = machine_obj->get_curr_tray()) {
                if (!curr_tray->nozzle_temp_max.empty() && !curr_tray->nozzle_temp_min.empty()) {
                    old_temp = (atoi(curr_tray->nozzle_temp_min.c_str()) + atoi(curr_tray->nozzle_temp_max.c_str())) / 2;
                }
            }

            s_command_unload_filament(machine_obj, ams_id, slot_id, old_temp, new_temp, 0);
        } catch (const std::exception& e) {
            return vm.MakeResponse(vm.GetModule(), "action", action, 2, e.what(), vm.BuildState());
        }
    } else if (action == "dry") {
        std::string dry_ams_id = ams_id;
        if (dry_ams_id.empty() && fila_system) {
            for (const auto& [_, ams_unit] : fila_system->GetAmsList()) {
                if (ams_unit && ams_unit->SupportDrying()) {
                    dry_ams_id = ams_unit->GetAmsId();
                    break;
                }
            }
        }
        if (dry_ams_id.empty()) {
            return vm.MakeResponse(vm.GetModule(), "action", action, 2, "No dryable AMS unit found", vm.BuildState());
        }
        wxGetApp().CallAfter([dry_ams_id]() {
            AMSDryCtrWin::RunModal(wxGetApp().mainframe, dry_ams_id);
        });
    } else if (action == "stop_filament_change") {
        // Backend guard mirroring the can_stop schema gate: never abort while a
        // print job or calibration is running, even if a stale Web button or a
        // direct bridge call slips through.
        if (machine_obj->is_in_printing() || machine_obj->is_in_calibration()) {
            return vm.MakeResponse(vm.GetModule(), "action", action, 3,
                _u8L("Cannot stop filament change while printing or calibrating"), vm.BuildState());
        }
        const int result = machine_obj->command_ams_control("abort");
        if (result != 0) {
            return vm.MakeResponse(vm.GetModule(), "action", action, 2, "AMS filament change abort failed", vm.BuildState());
        }
    } else if (action == "retry_filament_change") {
        const int result = machine_obj->command_ams_control("resume");
        if (result != 0) {
            return vm.MakeResponse(vm.GetModule(), "action", action, 2, "AMS filament change resume failed", vm.BuildState());
        }
    } else if (action == "manual_pull_detail") {
        wxGetApp().CallAfter([]() { open_filament_manual_pull_dialog(); });
    } else if (action == "manual_pull_close") {
        wxGetApp().CallAfter([]() { close_filament_manual_pull_dialog(); });
    } else if (action == "manual_pull_exit") {
        wxGetApp().CallAfter([machine_obj, token = machine_obj ? machine_obj->get_token() : std::weak_ptr<int>()]() {
            if (token.expired()) return;
            confirm_exit_filament_manual_pull(machine_obj);
        });
    } else if (action == "batch_run") {
        namespace SK = AmsHotendSchema::keys;
        namespace SV = AmsHotendSchema::values;
        const std::string batch_mode = payload.value("mode", std::string());
        const auto& slots_array = payload.value("slots", nlohmann::json::array());
        // `committed` is set by the edit dialog's Auto Load: the user already
        // resolved every unknown slot there, so we must NOT bounce back into the
        // edit dialog while the device tray identity is still lagging; run directly.
        const bool committed = payload.value("committed", false);
        if (slots_array.empty()) {
            return vm.MakeResponse(vm.GetModule(), "action", action, 2, _u8L("No slots selected for batch run"), vm.BuildState());
        }
        const bool is_load = (batch_mode == SV::mode::batch_ams_load || batch_mode == SV::mode::batch_ext_load);
        std::vector<std::pair<int, int>> slot_pairs;
        slot_pairs.reserve(slots_array.size());
        nlohmann::json run_slots = nlohmann::json::array();
        nlohmann::json unknown_slots = nlohmann::json::array();
        for (const auto& s : slots_array) {
            try {
                const std::string ams_str = s.value("ams_id", std::string());
                const std::string slot_str = s.value("slot_id", std::string());
                if (ams_str.empty() || slot_str.empty()) continue;
                const int ams_id = std::stoi(ams_str);
                const int slot_id = std::stoi(slot_str);
                run_slots.push_back({
                    {SK::filament_slot::addr_id,       s.value("addr_id", 0)},
                    {SK::filament_slot::ams_id,        ams_str},
                    {SK::filament_slot::slot_id,       slot_str},
                    {SK::filament_slot::display_index, s.value("display_index", 0)}
                });
                if (is_load) {
                    auto tray = machine_obj->get_tray(ams_str, slot_str);
                    if (!tray.has_value() || tray->setting_id.empty() || tray->m_fila_type.empty()) {
                        unknown_slots.push_back({
                            {SK::filament_slot::addr_id,       s.value("addr_id", 0)},
                            {SK::filament_slot::ams_id,        ams_str},
                            {SK::filament_slot::slot_id,       slot_str},
                            {SK::filament_slot::display_index, s.value("display_index", 0)}
                        });
                    }
                }
                slot_pairs.emplace_back(ams_id, slot_id);
            } catch (const std::exception&) {}
        }
        if (slot_pairs.empty()) {
            return vm.MakeResponse(vm.GetModule(), "action", action, 2, _u8L("All selected slots are invalid"), vm.BuildState());
        }
        if (DevicePageDeviceStateResolver::BlockIfBusy(machine_obj)) {
            return vm.MakeResponse(vm.GetModule(), "action", action, 3,
                _u8L("The printer is busy on other job"), vm.BuildState());
        }
        if (!committed && !unknown_slots.empty()) {
            nlohmann::json edit_payload = {
                {"intent",    "run"},
                {"mode",      batch_mode},
                {"slots",     unknown_slots},
                {"run_slots", run_slots}
            };
            wxGetApp().CallAfter([vm_ptr = &vm, edit_payload]() { vm_ptr->OpenBatchFilamentEditDialog(edit_payload); });
            return vm.MakeResponse(vm.GetModule(), "action", action, 0, "", vm.BuildState());
        }
        const int rc = machine_obj->command_multi_slot_ams_change_filament(is_load, slot_pairs);
        if (rc != 0) {
            return vm.MakeResponse(vm.GetModule(), "action", action, 2, _u8L("MQTT publish failed for multi_slot_ams_change_filament"), vm.BuildState());
        }
        // Command dispatched. This runs on the (separate) edit-dialog VM
        // instance, so resetting mode here only affects the dialog. The main
        // page is switched back to Idle by OpenBatchFilamentEditDialog once the
        // modal closes with the "run committed" result code.
        vm.m_active_mode = SV::mode::normal;
        return vm.MakeResponse(vm.GetModule(), "action", action, 0, "", vm.BuildState());
    } else {
        return std::nullopt;
    }

    return vm.MakeResponse(vm.GetModule(), "action", action, 0, "", vm.BuildState());
}

}} // namespace Slic3r::GUI
