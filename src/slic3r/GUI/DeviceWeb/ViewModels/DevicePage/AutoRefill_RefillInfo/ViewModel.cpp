/*****************************************************************//**
 * \file   ViewModel.cpp
 * \brief  Implementation of the RefillInfo auto-refill ViewModel.
 *********************************************************************/
#include "ViewModel.hpp"
#include "Schema.hpp"

#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/DeviceWeb/DeviceWebBridge.hpp"
#include "slic3r/GUI/DeviceManager.hpp"

#include "slic3r/GUI/DeviceCore/DevDefs.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevFilaAutoRefillMgr.h"

#include <boost/log/trivial.hpp>

namespace Slic3r { namespace GUI {

namespace {

namespace SV = AutoRefillRefillInfoSchema::values;

const char* create_type_to_string(const std::optional<DevAmsRefillSlot::CreateType>& type)
{
    if (!type.has_value()) return SV::create_type::unknown;
    switch (*type) {
    case DevAmsRefillSlot::CreateType::Auto:   return SV::create_type::automatic;
    case DevAmsRefillSlot::CreateType::Manual: return SV::create_type::manual;
    }
    return SV::create_type::unknown;
}

} // namespace

namespace SchemaFormat = AutoRefillRefillInfoSchema::format;

nlohmann::json AutoRefillRefillInfoVM::OnCommand(const std::string& submod,
                                                 const std::string& action,
                                                 const nlohmann::json& /*payload*/)
{
    try {
        if (submod == "state" && (action == "get" || action == "init")) {
            return MakeResponse(GetModule(), submod, action, 0, "", BuildState());
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "AutoRefillRefillInfoVM::OnCommand exception: " << e.what();
        return MakeResponse(GetModule(), submod, action, 2, e.what(), BuildState());
    }

    // Phase 1 is read-only. reorder / add / remove are reserved for a later
    // phase once a device write-back protocol is available.
    return MakeResponse(GetModule(), submod, action, 1,
                        "unknown auto refill command", BuildState());
}

void AutoRefillRefillInfoVM::ReportState(const std::string& submod, const std::string& action)
{
    ReportStateImpl(submod, action, /*force=*/false);
}

void AutoRefillRefillInfoVM::ReportStateImpl(const std::string& submod, const std::string& action, bool force)
{
    if (!m_bridge) return;

    nlohmann::json state = BuildState();

    // Device data may contain non-UTF-8 bytes; strict dump() throws. Replace
    // invalid bytes and skip the report on any other failure rather than crash.
    std::string state_key;
    try {
        state_key = state.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << " state.dump() failed: " << e.what();
        return;
    }
    if (state_key == m_last_report_state) return;

    m_last_report_state = state_key;
    const nlohmann::json body = MakeResponse(GetModule(), submod, action, 0, "", state);
    if (force) {
        m_bridge->ReportMsgForce(body);
    } else {
        m_bridge->ReportMsg(body);
    }
}

void AutoRefillRefillInfoVM::OnSysColorChanged()
{
    ReportState("state", "theme_changed");
}

void AutoRefillRefillInfoVM::Refresh(MachineObject* obj)
{
    m_obj = obj;
    ++m_session_seq;

    // force=true: Refresh() runs before the dialog is shown, so a visibility-
    // gated report would be dropped. Clearing the cache guarantees the pushed
    // state is not suppressed as a duplicate.
    m_last_report_state.clear();
    ReportStateImpl("state", "refresh", /*force=*/true);
}

nlohmann::json AutoRefillRefillInfoVM::BuildState() const
{
    SchemaFormat::State state;
    state.session_seq = m_session_seq;
    if (!m_obj) {
        return state;
    };

    auto             fila_sys = m_obj->GetFilaSystem();
    const DevFilaAutoRefillMgr& refill = fila_sys->GetAutoRefillMgr();
    if (refill.IsSupportManualEdit()) {
        state.description = _u8L("The system has created auto-refill groups based on material type, color, and brand.")
            + "\n"
            + _u8L("When printing, after the current spool runs out, the printer automatically switches to the next compatible spool in the group order and loops back to the first spool after the last one.")
            + "\n"
            + _u8L("Drag spools to adjust their priority.");
    } else if (refill.GetGroups().groups.size() > 0) {
        state.description = _u8L("The system has created auto-refill groups based on material type, color, and brand.")
            + "\n"
            + _u8L("When printing, after the current spool runs out, the printer automatically switches to the next compatible spool in the group order and loops back to the first spool after the last one.");
    } else {
        state.description = _u8L("When printing, after the current spool runs out, the printer automatically switches to the next compatible spool in the group order and loops back to the first spool after the last one.");
    }

    state.selected_dev_id = m_obj->get_dev_id();
    state.editable      = refill.IsSupportManualEdit();
    state.can_add_group = refill.IsSupportManualEdit();
    DevExtderSystem* ext_sys  = m_obj->GetExtderSystem();
    for (const auto& extruder : ext_sys->GetExtruders()) {
        SchemaFormat::RefillExtruder extruder_vm;
        extruder_vm.extruder_id = extruder.GetExtId();
        for (const auto& group : refill.GetGroups(extruder_vm.extruder_id).groups) {
            extruder_vm.groups.push_back(BuildGroup(group));
        }
        state.extruders.push_back(std::move(extruder_vm));
    }
    return state;
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

SchemaFormat::RefillGroup AutoRefillRefillInfoVM::BuildGroup(const DevFilaAutoRefillGroup& group) const
{
    const auto& tag = group.fila_refill_tag;

    SchemaFormat::RefillGroup group_vm;
    group_vm.filament_id = tag.filament_id;
    group_vm.colors      = tag.filament_clr;
    group_vm.color_type  = color_type_to_string(tag.filament_clr_type);
    group_vm.nozzle_diameter = tag.nozzle_diameter;
    if (tag.nozzle_flow_type.has_value()) {
        group_vm.nozzle_flow_type = flow_type_to_string(*tag.nozzle_flow_type);
    }

    for (const auto& refill_slot : group.fila_refill_slots) {
        SchemaFormat::RefillSlot slot_vm = BuildSlot(refill_slot);
        if (group_vm.filament_type.empty() && !slot_vm.filament_type.empty()) {
            group_vm.filament_type = slot_vm.filament_type;
        }
        group_vm.slots.push_back(std::move(slot_vm));
    }
    return group_vm;
}

SchemaFormat::RefillSlot AutoRefillRefillInfoVM::BuildSlot(const DevAmsRefillSlot& refill_slot) const
{
    const DevAmsSlotId& id = refill_slot.slot_id;

    SchemaFormat::RefillSlot slot_vm;
    slot_vm.ams_id      = id.first;
    slot_vm.slot_id     = id.second;
    slot_vm.create_type = create_type_to_string(refill_slot.create_type);

    DevAmsTray* tray = m_obj ? m_obj->GetFilaSystem()->GetAmsTray(std::to_string(id.first), std::to_string(id.second)) : nullptr;
    if (!tray) {
        slot_vm.has_filament = false;
        return slot_vm;
    }

    std::vector<std::string> colors = tray->cols;
    if (colors.empty() && !tray->color.empty()) colors.push_back(tray->color);

    slot_vm.has_filament  = tray->is_exists;
    slot_vm.tray_name     = tray->get_slot_name(m_obj);
    slot_vm.filament_type = tray->get_display_filament_type();
    slot_vm.sub_brand     = tray->sub_brands;
    slot_vm.color         = tray->color;
    slot_vm.colors        = std::move(colors);
    slot_vm.color_type    = color_type_to_string(tray->ctype);
    slot_vm.weight_gram   = tray->get_filament_remain_weight();
    return slot_vm;
}

}} // namespace Slic3r::GUI
