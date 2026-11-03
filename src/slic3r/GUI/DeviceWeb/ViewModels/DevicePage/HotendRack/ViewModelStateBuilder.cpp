#include "ViewModelStateBuilder.hpp"

#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"
#include "slic3r/GUI/DeviceCore/DevDefs.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"

#include <array>
#include <cstdio>
#include <cctype>
#include <memory>

namespace Slic3r { namespace GUI { namespace HotendRack {

namespace Schema = HotendRackSchema;
namespace Format = HotendRackSchema::format;

namespace {

std::string wx_to_utf8(const wxString& value)
{
    return value.ToUTF8().data();
}

std::string disabled_reason()
{
    return _u8L("This control is currently unavailable.");
}

bool is_hex_color(const std::string& value)
{
    for (char c : value) {
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return false;
    }
    return true;
}

std::string normalize_hex_color(const std::string& value)
{
    if (value.empty()) return {};
    if (value.front() == '#') return value;
    if ((value.size() == 6 || value.size() == 8) && is_hex_color(value))
        return "#" + value;
    return {};
}

Format::ActionControl make_control(bool enabled, const std::string& reason = {})
{
    Format::ActionControl result;
    result.enabled = enabled;
    result.disabled_reason = enabled ? std::string() : reason;
    return result;
}

bool is_ab_rack(MachineObject* obj)
{
    auto rack = obj ? obj->GetNozzleRack() : nullptr;
    return obj && obj->is_connected() && rack && rack->IsSupported() && rack->GetRackType() == DevNozzleRack::RackType_AB;
}

std::string rack_position(DevNozzleRack::RackABPos pos)
{
    switch (pos) {
    case DevNozzleRack::RACK_POS_A_TOP: return Schema::values::rack_position::a_top;
    case DevNozzleRack::RACK_POS_B_TOP: return Schema::values::rack_position::b_top;
    case DevNozzleRack::RACK_POS_CENTRE: return Schema::values::rack_position::center;
    default: return Schema::values::rack_position::unknown;
    }
}

Format::HotendSlot build_slot(const std::shared_ptr<DevNozzleRack>& rack, int id)
{
    Format::HotendSlot slot;
    slot.id = id;
    slot.display_id = std::to_string(id + 1);

    const auto nozzle = rack ? rack->GetNozzle(id) : DevNozzle();
    slot.filament_color = normalize_hex_color(nozzle.GetFilamentColor());
    slot.has_error_detail = nozzle.IsAbnormal();
    if (nozzle.IsEmpty()) {
        slot.status = Schema::values::slot_status::empty;
        slot.diameter = _u8L("Empty");
    } else if (nozzle.IsNormal()) {
        slot.status = Schema::values::slot_status::normal;
        slot.diameter = wx_to_utf8(nozzle.GetNozzleDiameterStr());
        slot.flow_type = wx_to_utf8(nozzle.GetNozzleFlowTypeStr());
    } else if (nozzle.IsAbnormal()) {
        slot.status = Schema::values::slot_status::error;
        slot.diameter = _u8L("Error");
    } else if (nozzle.IsUnknown()) {
        slot.status = Schema::values::slot_status::unknown;
        slot.diameter = _u8L("Unknown");
    }
    return slot;
}

Format::Toolhead build_toolhead(MachineObject* obj)
{
    Format::Toolhead toolhead;
    auto nozzle_system = obj ? obj->GetNozzleSystem() : nullptr;
    const auto nozzle = nozzle_system ? nozzle_system->GetExtNozzle(MAIN_EXTRUDER_ID) : DevNozzle();
    toolhead.filament_color = normalize_hex_color(nozzle.GetFilamentColor());
    if (nozzle.IsEmpty()) {
        toolhead.status = Schema::values::slot_status::empty;
    } else if (nozzle.IsNormal()) {
        toolhead.status = Schema::values::slot_status::normal;
        toolhead.diameter = wx_to_utf8(nozzle.GetNozzleDiameterStr());
        toolhead.flow_type = wx_to_utf8(nozzle.GetNozzleFlowTypeStr());
    } else if (nozzle.IsAbnormal()) {
        toolhead.status = Schema::values::slot_status::error;
        toolhead.diameter = _u8L("Error");
    } else if (nozzle.IsUnknown()) {
        toolhead.status = Schema::values::slot_status::unknown;
        toolhead.diameter = _u8L("Unknown");
    }
    return toolhead;
}

Format::HotendRackRow build_row(const std::shared_ptr<DevNozzleRack>& rack,
                                const std::string& id,
                                const std::array<int, 3>& slot_ids,
                                bool raised,
                                bool can_move,
                                const std::string& reason)
{
    Format::HotendRackRow row;
    row.id = id;
    row.raised = raised;
    row.move_control = make_control(can_move && !raised, reason);
    for (int slot_id : slot_ids)
        row.slots.push_back(build_slot(rack, slot_id));
    return row;
}

void fill_rack(MachineObject* obj, Format::State& state)
{
    auto rack = obj ? obj->GetNozzleRack() : nullptr;
    if (!rack) return;

    const bool visible = is_ab_rack(obj);
    state.display.visible = visible;
    state.display.title = _u8L("Induction Hotend Rack");
    state.display.toolhead = build_toolhead(obj);
    state.display.rack.position = rack_position(rack->GetPosition());

    const bool reading = rack->GetReadingCount() > 0;
    const bool running = rack->GetStatus() != DevNozzleRack::RACK_STATUS_IDLE &&
                         rack->GetStatus() != DevNozzleRack::RACK_STATUS_UNKNOWN;
    state.display.rack.busy = reading || running;
    state.display.rack.busy_text = reading ? _u8L("Reading") : (running ? _u8L("Running...") : std::string());
    state.display.reading.active = reading;
    state.display.reading.index = rack->GetReadingIdx();
    state.display.reading.count = rack->GetReadingCount();
    if (reading) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Reading (%d/%d) ...", state.display.reading.index, state.display.reading.count);
        state.display.reading.text = buf;
    }

    const bool force_disabled = wxGetApp().is_monitor_ctrl_force_disabled();
    const std::string reason = visible ? disabled_reason() : _u8L("Please select a printer.");
    const bool can_move = visible && !reading && !running && !force_disabled;
    const bool a_raised = rack->GetPosition() == DevNozzleRack::RACK_POS_A_TOP;
    const bool b_raised = rack->GetPosition() == DevNozzleRack::RACK_POS_B_TOP;

    const std::array<int, 3> row_a_slots = {0, 2, 4};
    const std::array<int, 3> row_b_slots = {1, 3, 5};
    auto row_a = build_row(rack, Schema::values::row_id::a, row_a_slots, a_raised, can_move, reason);
    auto row_b = build_row(rack, Schema::values::row_id::b, row_b_slots, b_raised, can_move, reason);

    if (!reading && !running && b_raised) {
        state.display.rack.rows = {row_b, row_a};
    } else {
        state.display.rack.rows = {row_a, row_b};
    }

    state.display.actions.home = make_control(can_move, reason);
    state.display.actions.read_all = make_control(visible && rack->CtrlCanReadAll() && !reading && !running && !force_disabled, reason);
    state.display.actions.hotends_info = make_control(visible, reason);
}

} // namespace

Format::State BuildState(MachineObject* machine_obj)
{
    Format::State state;
    fill_rack(machine_obj, state);
    return state;
}

}}} // namespace Slic3r::GUI::HotendRack
