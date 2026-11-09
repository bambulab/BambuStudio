#include "BuilderHotendRackProgressStatus.hpp"
#include "Schema.hpp"
#include "SchemaHotendRackProgress.hpp"

#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/I18N.hpp"

#include <algorithm>
#include <boost/format.hpp>
#include <vector>

namespace Slic3r { namespace GUI {

namespace {

std::string rack_operation_for_job(int job, int reading_count)
{
    namespace SV = AmsHotendSchema::values;
    switch (job) {
    case 1: return SV::rack_operation::take;
    case 2: return SV::rack_operation::place;
    case 3: return SV::rack_operation::exchange;
    case 4: return SV::rack_operation::read_all;
    case 5: return SV::rack_operation::move;
    case 6: return SV::rack_operation::read_single;
    default:
        if (reading_count > 1) return SV::rack_operation::read_all;
        if (reading_count == 1) return SV::rack_operation::read_single;
        return SV::rack_operation::idle;
    }
}

std::string rack_step_label(DevNozzleRack::RackStatus step)
{
    switch (step) {
    case DevNozzleRack::RACK_STATUS_UNKNOWN:                return _u8L("Hotend rack moving");
    case DevNozzleRack::RACK_STATUS_IDLE:                   return _u8L("Idle");
    case DevNozzleRack::RACK_STATUS_HOTEND_CENTRE:         return _u8L("Homing PrintTool Rack");
    case DevNozzleRack::RACK_STATUS_TOOLHEAD_CENTRE:       return _u8L("Homing Toolhead");
    case DevNozzleRack::RACK_STATUS_CALIBRATE_HOTEND_RACK: return _u8L("Calibrating PrintTool Rack Position");
    case DevNozzleRack::RACK_STATUS_WAIT_HOTEND_HEATING:   return _u8L("Heating Hotend");
    case DevNozzleRack::RACK_STATUS_CUT_MATERIAL:          return _u8L("Cutting Filament");
    case DevNozzleRack::RACK_STATUS_UNLOCK_HOTEND:         return _u8L("Unlocking Tool");
    case DevNozzleRack::RACK_STATUS_LIFT_HOTEND_RACK:      return _u8L("Raising PrintTool Rack");
    case DevNozzleRack::RACK_STATUS_PLACE_HOTEND:          return _u8L("Nesting Tool");
    case DevNozzleRack::RACK_STATUS_PICK_HOTEND:           return _u8L("Fetching Tool");
    case DevNozzleRack::RACK_STATUS_LOCK_HOTEND:           return _u8L("Locking Tool");
    case DevNozzleRack::RACK_STATUS_MOVE:                  return _u8L("Moving PrintTool Rack");
    case DevNozzleRack::RACK_STATUS_HOTEND_INFO_REFRESH:   return _u8L("Refreshing Hotend");
    case DevNozzleRack::RACK_STATUS_END:
    default:                                               return _u8L("Running");
    }
}

nlohmann::json build_rack_steps(const std::vector<int>& raw_steps, DevNozzleRack::RackStatus current_status)
{
    namespace SK = AmsHotendSchema::keys;
    namespace SV = AmsHotendSchema::values;

    std::vector<int> steps = raw_steps;
    if (steps.empty() && current_status > DevNozzleRack::RACK_STATUS_IDLE && current_status < DevNozzleRack::RACK_STATUS_END) {
        steps.push_back(static_cast<int>(current_status));
    }

    const auto active_it = std::find(steps.begin(), steps.end(), static_cast<int>(current_status));
    const int active_index = active_it == steps.end() ? -1 : static_cast<int>(std::distance(steps.begin(), active_it));

    nlohmann::json result = nlohmann::json::array();
    for (int i = 0; i < static_cast<int>(steps.size()); ++i) {
        const int step = steps[i];
        std::string state = SV::rack_step_state::pending;
        if (active_index >= 0) {
            if (i < active_index) state = SV::rack_step_state::done;
            else if (i == active_index) state = SV::rack_step_state::active;
        }
        result.push_back({
            {SK::rack_step::id, step},
            {SK::rack_step::label, rack_step_label(static_cast<DevNozzleRack::RackStatus>(step))},
            {SK::rack_step::state, state}
        });
    }
    return result;
}

} // namespace

int DevicePageHotendRackProgressStatusBuilder::TargetDisplayIndex(MachineObject* machine_obj, int job, int reading_idx)
{
    if (!machine_obj || !machine_obj->GetNozzleSystem()) return reading_idx;
    const int rack_count = machine_obj->GetNozzleSystem()->GetNozzleRack()
        ? machine_obj->GetNozzleSystem()->GetNozzleRack()->GetTotalRackNozzleCount()
        : 0;
    auto id_to_display = [rack_count](int id) {
        if (id == 0xff || id < 0x10) return 0;
        const int display = id - 0x10 + 1;
        return rack_count <= 0 || display <= rack_count ? display : 0;
    };

    const auto src = machine_obj->GetNozzleSystem()->GetReplaceNozzleSrc();
    const auto tar = machine_obj->GetNozzleSystem()->GetReplaceNozzleTar();
    if (job == 1 || job == 3) {
        if (src) { const int d = id_to_display(*src); if (d > 0) return d; }
        if (tar) { const int d = id_to_display(*tar); if (d > 0) return d; }
    } else if (job == 2) {
        if (tar) { const int d = id_to_display(*tar); if (d > 0) return d; }
        if (src) { const int d = id_to_display(*src); if (d > 0) return d; }
    }
    return reading_idx;
}

nlohmann::json DevicePageHotendRackProgressStatusBuilder::Build(MachineObject* machine_obj)
{
    namespace SK = AmsHotendSchema::keys;
    namespace SV = AmsHotendSchema::values;

    auto* nozzle_system = machine_obj ? machine_obj->GetNozzleSystem() : nullptr;
    auto nozzle_rack = nozzle_system ? nozzle_system->GetNozzleRack() : nullptr;

    int reading_idx = 0;
    int reading_count = 0;
    int rack_job = 0;
    auto rack_status = DevNozzleRack::RACK_STATUS_UNKNOWN;
    if (nozzle_rack) {
        reading_idx = nozzle_rack->GetReadingIdx();
        reading_count = nozzle_rack->GetReadingCount();
        rack_job = nozzle_rack->GetJob();
        rack_status = nozzle_rack->GetStatus();
    }

    const std::string rack_operation = rack_operation_for_job(rack_job, reading_count);
    if (nozzle_rack && rack_operation == SV::rack_operation::read_all) {
        // sometimes the reading_count comes with delay
        if (reading_count == 0 || reading_idx == 0) {
            reading_idx = 1;
            reading_count = nozzle_rack->GetTotalRackNozzleCount();
        }
    }

    const bool rack_reading = reading_count > 0;
    const bool rack_busy = rack_job != 0 || rack_reading;
    const bool rack_failed = rack_status == DevNozzleRack::RACK_STATUS_UNKNOWN && rack_busy;
    const bool can_stop = rack_busy;
    const bool can_retry = machine_obj && machine_obj->GetExtderSystem()->IsSwitchingFailed();

    std::string mode = SV::rack_mode::idle;
    if (rack_busy) {
        if (rack_operation == SV::rack_operation::read_all || reading_count > 1) {
            mode = SV::rack_mode::all;
        } else {
            mode = SV::rack_mode::single;
        }
    }

    std::string state = SV::rack_state::idle;
    if (rack_failed) {
        state = SV::rack_state::failed;
    } else if (rack_busy) {
        state = SV::rack_state::running;
    }

    std::string message;
    if (rack_reading) {
        if (reading_count > 1) {
            message = (boost::format(_u8L("Reading (%1% / %2%)")) % reading_idx % reading_count).str();
        } else {
            int reading_nozzle_id = nozzle_system->GetReadingNozzleIdFlag() - 0x10 + 1;
            message = (boost::format(_u8L("Reading Hotend %1%")) % reading_nozzle_id).str();
        }
    }

    std::string tips;
    if (rack_busy) {
        tips = _u8L("Please wait...");
    }

    const int target_display_index = TargetDisplayIndex(machine_obj, rack_job, reading_idx);
    int display_index = 0;
    if (rack_busy) {
        display_index = target_display_index;
    }

    int current = 0;
    if (rack_reading) {
        current = reading_idx;
    }

    int total = 0;
    if (rack_reading) {
        total = reading_count;
    } else if (nozzle_rack) {
        total = static_cast<int>(nozzle_rack->GetSteps().size());
    }

    int current_step = 0;
    if (rack_busy) {
        current_step = static_cast<int>(rack_status);
    }

    nlohmann::json steps = nlohmann::json::array();
    if (nozzle_rack) {
        steps = build_rack_steps(nozzle_rack->GetSteps(), rack_status);
    }

    return {
        {SK::rack_status::active,               rack_busy},
        {SK::rack_status::mode,                 mode},
        {SK::rack_status::display_index,        display_index},
        {SK::rack_status::current,              current},
        {SK::rack_status::total,                total},
        {SK::rack_status::message,              message},
        {SK::rack_status::tips,                 tips},
        {SK::rack_status::job,                  rack_job},
        {SK::rack_status::operation,            rack_operation},
        {SK::rack_status::state,                state},
        {SK::rack_status::target_display_index, display_index},
        {SK::rack_status::current_step,         current_step},
        {SK::rack_status::steps,                steps},
        {SK::rack_status::can_stop,             can_stop},
        {SK::rack_status::can_retry,            can_retry}
    };
}

}} // namespace Slic3r::GUI
