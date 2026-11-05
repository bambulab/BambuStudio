/*****************************************************************//**
 * \file   Builder.cpp
 * \brief  Build the add-refill-filaments picker from AMS trays and Query(5).
 *
 * The page is assembled in two steps:
 *   1. Collect every slot that should be shown (the rack's associated
 *      filaments, then ordinary AMS slots).
 *   2. Overlay `DevFilaRefillQueryResult` onto those trays: selected,
 *      selectable, warning, and disable_reason.
 *********************************************************************/
#include "Builder.hpp"
#include "BlockCodeUtil.hpp"
#include "Schema.hpp"

#include "slic3r/GUI/DeviceCore/DevFilaAutoRefillMgr.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/ResolverSlotState.hpp"

#include <optional>
#include <set>
#include <utility>

namespace Slic3r { namespace GUI {

namespace {

namespace SchemaFormat = AutoRefillAddRefillFilamentsSchema::format;
namespace SV = AutoRefillAddRefillFilamentsSchema::values;
namespace BlockCode = AutoRefillAddRefillFilamentsBlockCodeUtil;

using SlotKey = std::pair<int, int>;

std::string wx_to_utf8(const wxString& value)
{
    return std::string(value.ToUTF8().data());
}

std::string normalize_hex_for_web(const std::string& raw)
{
    if (raw.empty()) return "#D9D9D9";
    std::string hex = raw;
    if (hex[0] != '#') hex = "#" + hex;
    if (hex.size() > 9) hex = hex.substr(0, 9);
    return hex;
}

const char* color_type_for_web(DevFilaColorType color_type)
{
    switch (color_type) {
    case DevFilaColorType::CTYPE_MULTI:    return SV::color_type::multi;
    case DevFilaColorType::CTYPE_GRADIANT: return SV::color_type::gradient;
    case DevFilaColorType::CTYPE_SINGLE:   return SV::color_type::single;
    }
    return SV::color_type::single;
}

const char* nozzle_flow_short_name(NozzleFlowType flow_type)
{
    switch (flow_type) {
    case NozzleFlowType::S_FLOW: return "Std";
    case NozzleFlowType::H_FLOW: return "HF";
    case NozzleFlowType::U_FLOW: return "UHF";
    case NozzleFlowType::T_FLOW: return "TPU 285";
    case NozzleFlowType::E_FLOW: return "EHF";
    default: return "";
    }
}

bool is_stable_ams(DevAmsType type)
{
    return type == DevAmsType::ZMULTI_A || type == DevAmsType::ZMULTI_B;
}

std::set<SlotKey> parse_selected_slots(const nlohmann::json& payload)
{
    std::set<SlotKey> selected;
    const auto slots_it = payload.find("slots");
    if (slots_it == payload.end() || !slots_it->is_array()) return selected;

    for (const auto& item : *slots_it) {
        if (!item.is_object()) continue;
        const auto ams_it  = item.find("ams_id");
        const auto slot_it = item.find("slot_id");
        if (ams_it == item.end() || slot_it == item.end() ||
            !ams_it->is_number_integer() || !slot_it->is_number_integer()) {
            continue;
        }
        selected.emplace(ams_it->get<int>(), slot_it->get<int>());
    }
    return selected;
}

/// \return the tray behind a slot key. External spools only resolve through
/// the virtual trays, so GetAmsTray() alone is not enough.
std::optional<DevAmsTray> find_tray(MachineObject* obj, const SlotKey& slot)
{
    return obj->get_tray(slot.first, slot.second);
}


/**
 * \brief Fill the appearance fields of a card from its device tray.
 *
 * Color, type, and filament id only. Selection and eligibility are filled
 * later by \ref apply_query_result.
 */
void fill_slot_filament(const DevAmsTray* tray, SchemaFormat::Slot& slot)
{
    if (!tray || !tray->is_exists || tray->get_filament_id().empty()) return;

    slot.has_filament  = true;
    slot.filament_id   = tray->get_filament_id();
    slot.filament_type = tray->get_display_filament_type();
    slot.filament_name = DevicePageFilamentSlotStateResolver::ResolveFilamentCardText(
        tray, slot.filament_type);
    slot.remain        = DevicePageFilamentSlotStateResolver::ResolveDisplayRemain(tray);
    slot.color         = normalize_hex_for_web(tray->color);
    slot.colors        = tray->cols;
    if (slot.colors.empty()) slot.colors.push_back(tray->color);
    for (std::string& color : slot.colors) color = normalize_hex_for_web(color);
    slot.color_type = color_type_for_web(tray->ctype);
}

/// \return display index of a rack nozzle card; same convention as the panel.
int addr_id_to_display_index(int addr_id) { return addr_id + 1; }

/**
 * \brief Fill the nozzle information and state associated with one slot.
 *
 * The state follows the filament/hotend panel's device signals and keeps
 * empty, non-nozzle, and unknown tools distinct for the picker.
 */
void fill_slot_nozzle(MachineObject* obj,
                      const DevAmsTray* tray,
                      const std::optional<int>& addr_id,
                      SchemaFormat::Slot& slot)
{
    if (!addr_id.has_value() || !obj->GetNozzleSystem()) return;

    const auto nozzle = obj->GetNozzleSystem()->GetNozzleOptByAddrId(*addr_id);
    if (!nozzle.has_value()) return;

    const bool has_tool = nozzle->IsExist();
    const bool is_nozzle = nozzle->GetToolDefs().tool_type == DevNozzleToolType::Nozzle;
    if (nozzle->IsToolLostInHolder()) {
        slot.nozzle.state = SV::nozzle_state::lost;
    } else if (!has_tool) {
        slot.nozzle.state = SV::nozzle_state::empty;
    } else if (nozzle->IsAbnormal()) {
        slot.nozzle.state = SV::nozzle_state::error;
    } else if (nozzle->IsUnknown()) {
        slot.nozzle.state = SV::nozzle_state::unknown;
    } else if (!is_nozzle) {
        slot.nozzle.state = SV::nozzle_state::non_nozzle;
    } else if (DevicePageFilamentSlotStateResolver::IsTraySlotLoadedInNozzle(obj, tray)) {
        slot.nozzle.state = SV::nozzle_state::active;
    } else {
        slot.nozzle.state = SV::nozzle_state::normal;
    }

    if (nozzle->IsToolConnected() && is_nozzle) {
        slot.nozzle.label   = wx_to_utf8(wxString::Format("%.1f", nozzle->GetNozzleDiameter()));
        slot.nozzle.subtext = nozzle_flow_short_name(nozzle->GetNozzleFlowType());
    }
}

/**
 * \brief Step 1.1 helper: the AMS slot feeding one rack nozzle position.
 *
 * Mirrors the filament/hotend panel: only the *first* bound slot drives the
 * card, and a virtual (external spool) tray always reports slot 0. Pre-feed
 * slots, attached AMS units, and tray menus are deliberately left out.
 *
 * \return empty when the position has no nozzle or nothing bound to it.
 */
std::optional<SlotKey> resolve_hotend_bound_slot(MachineObject* obj,
                                                 const std::shared_ptr<DevFilaSystem>& fila_sys,
                                                 int addr_id)
{
    const auto nozzle = obj->GetNozzleSystem()->GetNozzleOptByAddrId(addr_id);
    if (!nozzle.has_value()) {
        return std::nullopt;
    }

    const auto& bind_slots = nozzle->GetBindAmsSlots();
    if (bind_slots.empty()) {
        return std::nullopt;
    }

    const auto& related_ams_slot = nozzle->GetRelatedAmsSlot();

    const int ams_id  = bind_slots[0].first;
    int       slot_id = bind_slots[0].second;
    if (fila_sys->GetVirtualTray(ams_id).has_value()) {
        slot_id = 0;
    } else if (fila_sys->GetAmsById(std::to_string(ams_id)) && related_ams_slot.has_value() && related_ams_slot.value() != bind_slots[0]) {
        return SlotKey{related_ams_slot->first, related_ams_slot->second};
    }

    return SlotKey{ams_id, slot_id};
}

/// \brief Step 1.1 helper: one hotend slot, either bound to filament or empty.
SchemaFormat::Slot build_hotend_slot(MachineObject* obj,
                                     const std::shared_ptr<DevFilaSystem>& fila_sys,
                                     int addr_id)
{
    SchemaFormat::Slot slot;
    slot.addr_id       = addr_id;
    slot.display_index = addr_id_to_display_index(addr_id);
    // Even display indices sit on the upper row, same as the hotend panel.
    slot.is_top_tier   = (slot.display_index % 2) == 0;
    slot.slot_name     = std::to_string(slot.display_index);

    const auto bound = resolve_hotend_bound_slot(obj, fila_sys, addr_id);
    slot.has_slot = bound.has_value();
    if (!bound.has_value()) {
        fill_slot_nozzle(obj, nullptr, addr_id, slot);
        return slot;
    }

    slot.ams_id  = bound->first;
    slot.slot_id = bound->second;
    // Keep the rack position as the index - that is what the grid means - and
    // mark external spools the way the management page labels them.
    if (fila_sys->GetVirtualTray(slot.ams_id).has_value()) {
        slot.slot_name += "-Ext";
    }
    const auto tray_copy = obj->get_tray(slot.ams_id, slot.slot_id);
    const DevAmsTray* tray = tray_copy ? &*tray_copy : nullptr;
    fill_slot_filament(tray, slot);
    fill_slot_nozzle(obj, tray, addr_id, slot);
    return slot;
}

/// \brief Step 1.2 helper: one AMS slot card for a physical slot.
SchemaFormat::Slot build_ams_slot(MachineObject* obj, DevAms* ams, int slot_id)
{
    SchemaFormat::Slot slot;
    slot.ams_id  = ams->GetAmdIdInt();
    slot.slot_id = slot_id;

    const auto tray_copy = obj->get_tray(slot.ams_id, slot_id);
    const DevAmsTray* tray = tray_copy ? &*tray_copy : nullptr;
    slot.slot_name = std::to_string(slot_id + 1);
    if (tray) {
        const auto nozzle_addr_id = tray->get_nozzle_addr_id(obj);
        if (nozzle_addr_id.has_value()) {
            slot.display_index = addr_id_to_display_index(*nozzle_addr_id);
        }
    }
    fill_slot_filament(tray, slot);
    fill_slot_nozzle(obj, tray, tray ? tray->get_nozzle_addr_id(obj) : std::nullopt, slot);
    return slot;
}

/// \return the device API display name of the built-in stable AMS.
std::string resolve_hotend_unit_name(const std::shared_ptr<DevFilaSystem>& fila_sys)
{
    for (const auto& [_, ams] : fila_sys->GetAmsList()) {
        if (ams && ams->IsExist() && is_stable_ams(ams->GetAmsType())) {
            std::string name = wx_to_utf8(ams->GetDisplayName());
            for (char& c : name) {
                if (c == '-') c = ' ';
            }
            return name;
        }
    }
    return "AMS";
}

/**
 * \brief Step 1.1: append the rack's associated-filament unit.
 */
void append_hotend_unit(std::vector<SchemaFormat::Unit>& units,
                        MachineObject* obj,
                        const std::shared_ptr<DevFilaSystem>& fila_sys)
{
    const auto rack = obj->GetNozzleRack();
    if (!rack || !obj->GetNozzleSystem()) return;

    if (rack->IsDirectConnectedRack()) {
        SchemaFormat::Unit unit;
        unit.kind = SV::unit_kind::hotend;
        unit.name = resolve_hotend_unit_name(fila_sys);
        for (int addr_id : rack->GetTotalRackNozzleAddIdSet()) {
            unit.slots.push_back(build_hotend_slot(obj, fila_sys, addr_id));
        }
        if (!unit.slots.empty()) {
            unit.slot_count = static_cast<int>(unit.slots.size());
            units.push_back(std::move(unit));
        }
    }
}

/// \brief Step 1.2: append every physical slot of the non-stable AMS units.
void append_other_ams_units(std::vector<SchemaFormat::Unit>& units,
                            MachineObject* obj,
                            const std::shared_ptr<DevFilaSystem>& fila_sys)
{
    for (const auto& [_, ams] : fila_sys->GetAmsList()) {
        if (!ams || !ams->IsExist()) {
            continue;
        }

        if (ams->GetSlotCount() == obj->GetNozzleRack()->GetTotalRackNozzleCount()) {
            continue;
        }

        SchemaFormat::Unit unit;
        unit.kind       = SV::unit_kind::ams;
        unit.ams_id     = ams->GetAmdIdInt();
        unit.ams_type   = std::to_string(static_cast<int>(ams->GetAmsType()));
        unit.name       = wx_to_utf8(ams->GetDisplayName());
        unit.slot_count = ams->GetSlotCount();
        unit.slots.reserve(unit.slot_count);
        for (int slot_id = 0; slot_id < unit.slot_count; ++slot_id) {
            unit.slots.push_back(build_ams_slot(obj, ams, slot_id));
        }
        units.push_back(std::move(unit));
    }
}

/**
 * \brief Step 1: collect every slot in the printer.
 *
 * The rack's associated filaments come first, then ordinary AMS units.
 */
std::vector<SchemaFormat::Unit> collect_display_units(MachineObject* obj,
                                                       const std::shared_ptr<DevFilaSystem>& fila_sys)
{
    std::vector<SchemaFormat::Unit> units;
    append_hotend_unit(units, obj, fila_sys);// ---- Step 1.1: rack associated filaments ----
    append_other_ams_units(units, obj, fila_sys);// ---- Step 1.2: other AMS slots ----
    return units;
}

/// \return the hover text of a code: its own bubble, else its dialog title.
const char* block_reason(const BlockCode::RefillBlockCodeUx& ux)
{
    if (ux.tooltip) return ux.tooltip;
    if (ux.title) return ux.title;
    return "This filament cannot be added to an auto-refill group.";
}

/**
 * \brief What the mismatch dialogs compare a candidate against.
 *
 * Reuses the already-formatted cards instead of re-reading the device, so
 * the dialog and the card cannot disagree on how a filament is spelled.
 *
 * \return empty members when no selected slot carries filament yet.
 */
SchemaFormat::GroupReference resolve_group_reference(const std::vector<SchemaFormat::Unit>& units,
                                                    const std::set<SlotKey>&               selected_slots)
{
    SchemaFormat::GroupReference reference;
    for (const SchemaFormat::Unit& unit : units) {
        for (const SchemaFormat::Slot& slot : unit.slots) {
            if (!slot.has_filament || selected_slots.find({slot.ams_id, slot.slot_id}) == selected_slots.end()) {
                continue;
            }
            reference.filament_name = slot.filament_name;
            reference.filament_type = slot.filament_type;
            reference.nozzle        = slot.nozzle.subtext.empty()
                                          ? slot.nozzle.label
                                          : slot.nozzle.label + " " + slot.nozzle.subtext;
            return reference;
        }
    }
    return reference;
}

/**
 * \brief First selected tray that still has filament, used as the type filter.
 *
 * \return `{filament_id, display_type}`; both empty when no selected tray qualifies.
 */
std::pair<std::string, std::string> find_reference_filament(
    MachineObject* obj,
    const std::set<SlotKey>& selected_slots)
{
    for (const SlotKey& selected : selected_slots) {
        if (const std::optional<DevAmsTray> tray = find_tray(obj, selected);
            tray && tray->is_exists && !tray->get_filament_id().empty()) {
            return {tray->get_filament_id(), tray->get_display_filament_type()};
        }
    }
    return {};
}

/**
 * \brief Step 2: overlay Query eligibility onto already-collected slots.
 *
 * Local rules (empty, already grouped, type mismatch) run first and decide
 * eligibility on their own. Where the device also reported a `block` code,
 * its interaction wins the copy: the device knows *which* mismatch it is
 * and therefore which follow-up to offer, while the local rules only know
 * that something did not match.
 */
void apply_query_result(std::vector<SchemaFormat::Unit>& units,
                        const std::set<SlotKey>& selected_slots,
                        const std::string& reference_filament_id,
                        const DevFilaAutoRefillMgr& refill_mgr,
                        const DevFilaRefillQueryResult* query)
{
    for (SchemaFormat::Unit& unit : units) {
        for (SchemaFormat::Slot& slot : unit.slots) {
            // A rack position with nothing bound has no slot to query.
            if (!slot.has_slot) {
                slot.disable_reason = "No filament is associated with this hotend.";
                continue;
            }

            const SlotKey slot_key{slot.ams_id, slot.slot_id};
            const bool requested_selected = selected_slots.find(slot_key) != selected_slots.end();

            // ---- Step 2.1: local selectable / disable_reason ----
            if (!slot.has_filament) {
                slot.disable_reason = "Empty slot.";
            } else {
                const bool already_grouped = !refill_mgr.GetGroupsBySlot(slot_key).groups.empty();
                if (already_grouped && !requested_selected) {
                    slot.disable_reason = "This filament is already in an auto-refill group.";
                } else if (!reference_filament_id.empty() && slot.filament_id != reference_filament_id) {
                    slot.disable_reason = "The filament does not match the first selected filament.";
                } else {
                    slot.selectable = true;
                }
            }

            // ---- Step 2.2: Query(5) reasons, their copy and their interaction ----
            if (query) {
                const std::vector<DevFilaRefillBlockCode> codes = query->FindBlockCodes(slot_key);
                const DevFilaRefillBlockCode primary = BlockCode::SelectPrimary(codes);
                if (primary != DevFilaRefillBlockCode::Available) {
                    slot.block_code  = primary;
                    slot.interaction = BlockCode::ToJson(primary);

                    // Primary first, so the page can explain the rest in order.
                    slot.block_codes.push_back(static_cast<int>(primary));
                    for (const DevFilaRefillBlockCode code : codes) {
                        if (code != primary) slot.block_codes.push_back(static_cast<int>(code));
                    }

                    const BlockCode::RefillBlockCodeUx ux = BlockCode::Create(primary);
                    switch (ux.policy) {
                    case BlockCode::SelectPolicy::Blocked:
                        slot.selectable     = false;
                        slot.disable_reason = block_reason(ux);
                        break;
                    case BlockCode::SelectPolicy::ConfirmThenSelect:
                        // A dismissed warning keeps the slot selectable and
                        // stops asking, so the page must not raise a dialog.
                        slot.warning = slot.selectable && !BlockCode::IsSuppressed(primary);
                        break;
                    case BlockCode::SelectPolicy::Selectable:
                        break;
                    }
                }
            }

            slot.selected = requested_selected && slot.selectable;
        }
    }
}

} // namespace

nlohmann::json AutoRefillAddRefillFilamentsBuilder::Build(MachineObject*        obj,
                                                          std::uint32_t          session_seq,
                                                          const nlohmann::json& payload,
                                                          const DevFilaRefillQueryResult* query)
{
    SchemaFormat::State state;
    state.session_seq = session_seq;
    state.mode = payload.value("mode", std::string(SV::mode::create_group));
    if (state.mode != SV::mode::create_group && state.mode != SV::mode::add_to_group) {
        state.mode = SV::mode::create_group;
    }
    state.mode_title = state.mode == SV::mode::add_to_group ? "Add refill filament" : "Add material group";

    if (!obj || !obj->GetFilaSystem()) return state;

    state.selected_dev_id = obj->get_dev_id();
    auto fila_sys = obj->GetFilaSystem();

    // ---- Step 1: collect every slot that should be shown ----
    state.units = collect_display_units(obj, fila_sys);

    const auto selected_slots = parse_selected_slots(payload);
    const auto [reference_filament_id, reference_filament_type] = find_reference_filament(obj, selected_slots);
    if (!reference_filament_type.empty()) {
        state.notice               = "Please select {{type}} filament for the refill relation.";
        state.notice_filament_type = reference_filament_type;
    }

    // ---- Step 2: overlay Query selected / selectable / warning / reason ----
    const DevFilaAutoRefillMgr& refill_mgr = fila_sys->GetAutoRefillMgr();
    apply_query_result(state.units, selected_slots, reference_filament_id, refill_mgr, query);

    state.group_reference = resolve_group_reference(state.units, selected_slots);

    return state;
}

}} // namespace Slic3r::GUI
