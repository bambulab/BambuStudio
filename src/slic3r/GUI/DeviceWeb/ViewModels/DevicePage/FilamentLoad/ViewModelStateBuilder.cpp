#include "ViewModelStateBuilder.hpp"

#include "slic3r/GUI/DeviceCore/DevConfigUtil.h"
#include "slic3r/GUI/DeviceCore/DevDefs.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevInfo.h"
#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/I18N.hpp"

#include <cstdlib>
#include <map>
#include <utility>

namespace Slic3r { namespace GUI { namespace FilamentLoadStateBuilder {

namespace Schema = FilamentLoadSchema;
namespace Format = FilamentLoadSchema::format;

namespace {

struct StepSnapshot
{
    bool             visible = false;
    std::string      kind = Schema::values::step_kind::load;
    DevFilamentStep  current = DevFilamentStep::STEP_IDLE;
};

std::map<DevFilamentStep, std::string> step_strings()
{
    return {
        {DevFilamentStep::STEP_IDLE, _u8L("Idling...")},
        {DevFilamentStep::STEP_PAUSE, _u8L("Pause")},
        {DevFilamentStep::STEP_HEAT_NOZZLE, _u8L("Heat the nozzle")},
        {DevFilamentStep::STEP_CUT_FILAMENT, _u8L("Cut filament")},
        {DevFilamentStep::STEP_PULL_CURR_FILAMENT, _u8L("Pull back current filament")},
        {DevFilamentStep::STEP_PUSH_NEW_FILAMENT, _u8L("Push new filament into extruder")},
        {DevFilamentStep::STEP_GRAB_NEW_FILAMENT, _u8L("Grab new filament")},
        {DevFilamentStep::STEP_PURGE_OLD_FILAMENT, _u8L("Purge old filament")},
        {DevFilamentStep::STEP_SWITCH_EXTRUDER, _u8L("Switch") + " " + _u8L("extruder")},
        {DevFilamentStep::STEP_SWITCH_HOTEND, _u8L("Switch") + " " + _u8L("hotend")},
        {DevFilamentStep::STEP_AMS_FILA_COOLING, _u8L("Wait for AMS cooling")},
        {DevFilamentStep::STEP_PUSH_SWITCHER_FILA, _u8L("Switch current filament at Filament Track Switch")},
        {DevFilamentStep::STEP_PULL_SWITCHER_FILA, _u8L("Pull back current filament at Filament Track Switch")},
        {DevFilamentStep::STEP_SWITCHER_SWITCH, _u8L("Switch track at Filament Track Switch")},
        {DevFilamentStep::STEP_CHECK_POSITION, _u8L("Confirm extruded")},
    };
}

std::vector<DevFilamentStep> default_steps(DevAmsType ams_model, DevAmsType ext_model, bool has_fila_to_switch, const std::string& kind)
{
    std::vector<DevFilamentStep> steps;
    const bool is_vt_load = kind == Schema::values::step_kind::vt_load;
    const bool is_unload  = kind == Schema::values::step_kind::unload;

    if (ams_model == DevAmsType::AMS || ext_model == DevAmsType::N3F || ext_model == DevAmsType::N3S) {
        if (is_vt_load) {
            return {
                DevFilamentStep::STEP_HEAT_NOZZLE,
                DevFilamentStep::STEP_PUSH_NEW_FILAMENT,
                DevFilamentStep::STEP_GRAB_NEW_FILAMENT,
                DevFilamentStep::STEP_PURGE_OLD_FILAMENT,
            };
        }
        if (is_unload) {
            return {
                DevFilamentStep::STEP_HEAT_NOZZLE,
                DevFilamentStep::STEP_CUT_FILAMENT,
                DevFilamentStep::STEP_PULL_CURR_FILAMENT,
            };
        }
        if (has_fila_to_switch) {
            return {
                DevFilamentStep::STEP_HEAT_NOZZLE,
                DevFilamentStep::STEP_CUT_FILAMENT,
                DevFilamentStep::STEP_PULL_CURR_FILAMENT,
                DevFilamentStep::STEP_PUSH_NEW_FILAMENT,
                DevFilamentStep::STEP_PURGE_OLD_FILAMENT,
            };
        }
        return {
            DevFilamentStep::STEP_HEAT_NOZZLE,
            DevFilamentStep::STEP_PUSH_NEW_FILAMENT,
            DevFilamentStep::STEP_PURGE_OLD_FILAMENT,
        };
    }

    if (ams_model == DevAmsType::AMS_LITE || ext_model == DevAmsType::AMS_LITE) {
        if (is_unload) {
            return {
                DevFilamentStep::STEP_HEAT_NOZZLE,
                DevFilamentStep::STEP_CHECK_POSITION,
                DevFilamentStep::STEP_CUT_FILAMENT,
                DevFilamentStep::STEP_PULL_CURR_FILAMENT,
            };
        }
        return {
            DevFilamentStep::STEP_HEAT_NOZZLE,
            DevFilamentStep::STEP_CHECK_POSITION,
            DevFilamentStep::STEP_CUT_FILAMENT,
            DevFilamentStep::STEP_PULL_CURR_FILAMENT,
            DevFilamentStep::STEP_PUSH_NEW_FILAMENT,
            DevFilamentStep::STEP_PURGE_OLD_FILAMENT,
        };
    }

    return steps;
}

DevAmsType current_ams_model(MachineObject* obj)
{
    if (!obj)
        return DevAmsType::AMS;
    if ((obj->is_enable_np || obj->is_enable_ams_np) && obj->GetFilaSystem() && !obj->GetFilaSystem()->GetAmsList().empty())
        return obj->GetFilaSystem()->GetAmsList().begin()->second->GetAmsType();
    if (obj->get_printer_ams_type() == "f1")
        return DevAmsType::AMS_LITE;
    return DevAmsType::AMS;
}

StepSnapshot current_step(MachineObject* obj)
{
    StepSnapshot snapshot;
    if (!obj || !obj->is_connected())
        return snapshot;
    if (obj->GetNozzleRack()->IsSupported() && obj->GetNozzleRack()->IsDirectConnectedRack())
        return snapshot;

    bool loading = false;
    if (obj->is_enable_np && obj->ams_status_main != AMS_STATUS_MAIN_COLD_PULL)
        loading = obj->GetExtderSystem() && obj->GetExtderSystem()->IsBusyLoading();
    else if (obj->ams_status_main == AMS_STATUS_MAIN_FILAMENT_CHANGE)
        loading = true;
    if (!loading)
        return snapshot;

    auto loading_ext = obj->GetExtderSystem() ? obj->GetExtderSystem()->GetLoadingExtder() : nullptr;
    auto tar = loading_ext ? loading_ext->GetSlotTarget() : DevAmsSlotInfo();
    const bool busy_for_vt_loading =
        (tar.ams_id == std::to_string(VIRTUAL_TRAY_MAIN_ID) || tar.ams_id == std::to_string(VIRTUAL_TRAY_DEPUTY_ID)) &&
        !obj->is_target_slot_unload();

    snapshot.kind = busy_for_vt_loading ? Schema::values::step_kind::vt_load :
                    obj->is_target_slot_unload() ? Schema::values::step_kind::unload :
                    Schema::values::step_kind::load;

    if (obj->GetFilaSystem() && obj->GetFilaSystem()->GetCurrentFilamentChangeStep().has_value()) {
        snapshot.current = obj->GetFilaSystem()->GetCurrentFilamentChangeStep().value();
        snapshot.visible = snapshot.current != DevFilamentStep::STEP_IDLE && snapshot.current != DevFilamentStep::STEP_PAUSE;
        return snapshot;
    }

    const auto sub = obj->ams_status_sub;
    if (busy_for_vt_loading) {
        if (sub == 0x02) snapshot.current = DevFilamentStep::STEP_HEAT_NOZZLE;
        else if (sub == 0x05) snapshot.current = DevFilamentStep::STEP_PUSH_NEW_FILAMENT;
        else if (sub == 0x06) snapshot.current = DevFilamentStep::STEP_CHECK_POSITION;
        else if (sub == 0x07) snapshot.current = DevFilamentStep::STEP_PURGE_OLD_FILAMENT;
    } else if (sub == 0x02) {
        snapshot.current = DevFilamentStep::STEP_HEAT_NOZZLE;
    } else if (sub == 0x03) {
        snapshot.current = DevFilamentStep::STEP_CUT_FILAMENT;
    } else if (sub == 0x04) {
        snapshot.current = DevFilamentStep::STEP_PULL_CURR_FILAMENT;
    } else if (sub == 0x05) {
        snapshot.current = obj->is_target_slot_unload() ? DevFilamentStep::STEP_PUSH_NEW_FILAMENT :
                           (!obj->is_filament_at_extruder() ? DevFilamentStep::STEP_CUT_FILAMENT : DevFilamentStep::STEP_PUSH_NEW_FILAMENT);
    } else if (sub == 0x06) {
        snapshot.current = DevFilamentStep::STEP_PUSH_NEW_FILAMENT;
    } else if (sub == 0x07) {
        snapshot.current = obj->is_target_slot_unload() ? DevFilamentStep::STEP_PURGE_OLD_FILAMENT :
                           (!obj->is_filament_at_extruder() ? DevFilamentStep::STEP_PULL_CURR_FILAMENT : DevFilamentStep::STEP_PURGE_OLD_FILAMENT);
    } else if (sub == 0x08 || sub == 0x0B) {
        snapshot.current = DevFilamentStep::STEP_CHECK_POSITION;
    }

    snapshot.visible = snapshot.current != DevFilamentStep::STEP_IDLE;
    return snapshot;
}

void fill_steps(MachineObject* obj, Format::StepArea& area, const StepSnapshot& snapshot)
{
    area.kind = snapshot.kind;

    std::vector<DevFilamentStep> step_ids;
    if (obj && obj->GetFilaSystem() && !obj->GetFilaSystem()->GetCurrentFilamentChangeSteps().empty()) {
        step_ids = obj->GetFilaSystem()->GetCurrentFilamentChangeSteps();
    } else {
        bool has_fila_to_switch = false;
        if (obj && obj->GetExtderSystem()) {
            auto cur_ext = obj->GetExtderSystem()->GetCurrentExtder();
            has_fila_to_switch = cur_ext && cur_ext->HasFilamentInExt();
        }
        const DevAmsType ams_model = current_ams_model(obj);
        step_ids = default_steps(ams_model, ams_model, has_fila_to_switch, snapshot.kind);
    }

    const auto labels = step_strings();
    int current_index = -1;
    for (size_t i = 0; i < step_ids.size(); ++i) {
        if (step_ids[i] == snapshot.current) {
            current_index = static_cast<int>(i);
            break;
        }
    }
    if (current_index < 0 && snapshot.kind == Schema::values::step_kind::vt_load &&
        snapshot.current == DevFilamentStep::STEP_CHECK_POSITION && step_ids.size() > 2) {
        current_index = 2;
    }

    for (size_t i = 0; i < step_ids.size(); ++i) {
        const auto it = labels.find(step_ids[i]);
        if (it == labels.end())
            continue;
        Format::StepItem item;
        item.text = it->second;
        if (current_index >= 0) {
            if (static_cast<int>(i) < current_index)
                item.status = Schema::values::step_status::done;
            else if (static_cast<int>(i) == current_index)
                item.status = Schema::values::step_status::current;
        }
        area.items.push_back(std::move(item));
    }
}

} // namespace

FilamentLoadSchema::format::State BuildState(MachineObject* machine_obj, bool expanded)
{
    Format::State state;
    auto& display = state.display;

    display.title = _u8L("Filament loading...");
    display.expanded = expanded;

    const StepSnapshot snapshot = current_step(machine_obj);
    display.visible = snapshot.visible;
    if (!display.visible)
        return state;

    fill_steps(machine_obj, display.steps, snapshot);

    if (machine_obj) {
        const auto& ext_system = machine_obj->GetExtderSystem();
        int cur_ext_id = (ext_system && ext_system->GetTotalExtderCount() > 1) ? ext_system->GetCurrentExtderId() : 0;
        bool has_nozzle_rack = machine_obj->GetNozzleSystem() && machine_obj->GetNozzleSystem()->GetNozzleRack()->IsSupported();
        display.image_name = DevPrinterConfigUtil::get_filament_load_img(machine_obj->printer_type, cur_ext_id, has_nozzle_rack);
    }

    display.actions.retry.visible = true;
    display.actions.retry.enabled = true;
    display.actions.retry.text = _u8L("Retry");

    const bool can_abort = machine_obj &&
        (machine_obj->is_support_fila_change_abort ||
         DevPrinterConfigUtil::support_ams_fila_change_abort(machine_obj->printer_type));
    display.actions.stop.visible = can_abort;
    display.actions.stop.enabled = can_abort;
    display.actions.stop.text = _u8L("Stop");

    return state;
}

}}} // namespace Slic3r::GUI::FilamentLoadStateBuilder
