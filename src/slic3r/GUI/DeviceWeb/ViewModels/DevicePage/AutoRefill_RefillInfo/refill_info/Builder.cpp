#include "Builder.hpp"
#include "Schema.hpp"

#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/DeviceCore/DevDefs.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevFilaAutoRefillMgr.h"
#include "slic3r/GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/ResolverSlotState.hpp"

namespace Slic3r { namespace GUI {

namespace {

namespace SV = AutoRefillRefillInfoSchema::values;
namespace SchemaFormat = AutoRefillRefillInfoSchema::format;

const char* create_type_to_string(const std::optional<DevAmsRefillSlot::CreateType>& type)
{
    if (!type.has_value()) return SV::create_type::unknown;
    switch (*type) {
    case DevAmsRefillSlot::CreateType::Auto:   return SV::create_type::automatic;
    case DevAmsRefillSlot::CreateType::Manual: return SV::create_type::manual;
    }
    return SV::create_type::unknown;
}

const char* color_type_to_string(DevFilaColorType ctype)
{
    switch (ctype) {
        case DevFilaColorType::CTYPE_GRADIANT: return SV::color_type::gradient;
        case DevFilaColorType::CTYPE_MULTI:    return SV::color_type::multi;
        case DevFilaColorType::CTYPE_SINGLE:   return SV::color_type::single;
    }
    return SV::color_type::single;
}

const char* flow_type_to_string(NozzleFlowType flow)
{
    switch (flow) {
        case NozzleFlowType::S_FLOW: return SV::nozzle_flow_type::standard;
        case NozzleFlowType::H_FLOW: return SV::nozzle_flow_type::high_flow;
        case NozzleFlowType::U_FLOW: return SV::nozzle_flow_type::ultra;
        case NozzleFlowType::T_FLOW: return SV::nozzle_flow_type::tough;
        case NozzleFlowType::E_FLOW: return SV::nozzle_flow_type::elastic;
        default:                     return "";
    }
}

SchemaFormat::RefillSlot BuildSlot(MachineObject* obj, const DevAmsRefillSlot& refill_slot)
{
    const DevAmsSlotId& id = refill_slot.slot_id;

    SchemaFormat::RefillSlot slot_vm;
    slot_vm.ams_id      = id.first;
    slot_vm.slot_id     = id.second;
    slot_vm.create_type = create_type_to_string(refill_slot.create_type);

    // A refill slot may also be an external spool, which GetAmsTray() cannot
    // resolve; get_tray() falls back to the virtual trays for those.
    const std::optional<DevAmsTray> tray = obj ? obj->get_tray(id.first, id.second) : std::nullopt;
    if (!tray) {
        slot_vm.has_filament = false;
        return slot_vm;
    }

    std::vector<std::string> colors = tray->cols;
    if (colors.empty() && !tray->color.empty()) colors.push_back(tray->color);

    slot_vm.has_filament  = tray->is_exists;
    slot_vm.tray_name     = tray->get_slot_name(obj);
    slot_vm.filament_type = tray->get_display_filament_type();
    // Resolve the same display name the filament panel and the add-filament
    // picker use, so a slot never falls back to the bare type ("PLA") just
    // because the device reported no sub-brand.
    slot_vm.filament_name = DevicePageFilamentSlotStateResolver::ResolveFilamentCardText(
        &*tray, slot_vm.filament_type);
    slot_vm.sub_brand     = tray->sub_brands;
    slot_vm.color         = tray->color;
    slot_vm.colors        = std::move(colors);
    slot_vm.color_type    = color_type_to_string(tray->ctype);
    slot_vm.weight_gram   = tray->get_filament_remain_weight();
    return slot_vm;
}

SchemaFormat::RefillGroup BuildGroup(MachineObject* obj, const DevFilaAutoRefillGroup& group)
{
    const auto& tag = group.fila_refill_tag;

    SchemaFormat::RefillGroup group_vm;
    group_vm.refill_tag_id = group.refill_tag_id;
    group_vm.filament_id = tag.filament_id;
    group_vm.colors      = tag.filament_clr;
    group_vm.color_type  = color_type_to_string(tag.filament_clr_type);
    group_vm.nozzle_diameter = tag.nozzle_diameter;
    if (tag.nozzle_flow_type.has_value()) {
        group_vm.nozzle_flow_type = flow_type_to_string(*tag.nozzle_flow_type);
    }

    for (const auto& refill_slot : group.fila_refill_slots) {
        SchemaFormat::RefillSlot slot_vm = BuildSlot(obj, refill_slot);
        if (group_vm.filament_type.empty() && !slot_vm.filament_type.empty()) {
            group_vm.filament_type = slot_vm.filament_type;
        }
        group_vm.slots.push_back(std::move(slot_vm));
    }
    return group_vm;
}

} // namespace

nlohmann::json AutoRefillRefillInfoBuilder::Build(MachineObject* obj, std::uint32_t session_seq)
{
    SchemaFormat::State state;
    state.session_seq = session_seq;
    if (!obj) {
        return state;
    }

    auto             fila_sys = obj->GetFilaSystem();
    const DevFilaAutoRefillMgr& refill = fila_sys->GetAutoRefillMgr();
    state.auto_refill_enabled = fila_sys->IsAutoRefillEnabled();
    if (refill.IsSupportManualEdit()) {
        // One i18n key per line: the web page translates each line as a whole,
        // so the two sentences below have to stay in a single message.
        state.description = _u8L("When the current spool runs out, the printer automatically continues with a spool of the same brand, type, and color. Refill relations can also be edited manually, so spools of the same type in a different color can be used for refilling.")
            + "\n"
            + _u8L("Drag spools to adjust their priority.");
    } else if (refill.GetGroups().groups.size() > 0) {
        state.description = _u8L("The system has created auto-refill groups based on material type, color, and brand.")
            + "\n"
            + _u8L("When printing, after the current spool runs out, the printer automatically switches to the next compatible spool in the group order and loops back to the first spool after the last one.");
    } else {
        state.description = _u8L("When printing, after the current spool runs out, the printer automatically switches to the next compatible spool in the group order and loops back to the first spool after the last one.");
    }

    state.selected_dev_id = obj->get_dev_id();
    state.editable      = refill.IsSupportManualEdit();
    state.can_add_group = refill.IsSupportManualEdit();
    DevExtderSystem* ext_sys  = obj->GetExtderSystem();
    for (const auto& extruder : ext_sys->GetExtruders()) {
        SchemaFormat::RefillExtruder extruder_vm;
        extruder_vm.extruder_id = extruder.GetExtId();
        for (const auto& group : refill.GetGroups(extruder_vm.extruder_id).groups) {
            extruder_vm.groups.push_back(BuildGroup(obj, group));
        }
        state.extruders.push_back(std::move(extruder_vm));
    }
    return state;
}

}} // namespace Slic3r::GUI
