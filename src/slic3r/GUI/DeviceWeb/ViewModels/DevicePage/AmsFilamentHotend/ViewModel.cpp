#include "ViewModel.hpp"
#include "Schema.hpp"
#include "ActionHandlerExtAms.hpp"
#include "ActionHandlerFilament.hpp"
#include "BuilderFilamentChangeStatus.hpp"
#include "ActionHandlerHotend.hpp"
#include "Modes/NormalModeBuilder.hpp"
#include "Modes/BatchAmsLoadModeBuilder.hpp"
#include "Modes/BatchExtLoadModeBuilder.hpp"
#include "Modes/BatchUnloadModeBuilder.hpp"
#include "slic3r/GUI/DeviceWeb/DeviceWebBridge.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/DeviceCore/DevDefs.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"
#include "libslic3r/PresetBundle.hpp"

#include <algorithm>
#include <cctype>
#include <memory>
#include <vector>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

namespace {

// Stable enum-like dry-state labels emitted as `ams_zones[].dry_status`. The
// Web side compares these strings against logical constants
// (`!== 'Idle' && !== 'Off'`), so they MUST stay untranslated English. The
// matching user-facing copy lives in `ams_zones[].dry_status_text`, produced
// by {@link dry_status_display_label}.
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

// User-facing label for the dryer chamber, translated per current language.
// Web renders this directly as the dryer status chip text.
std::string dry_status_display_label(Slic3r::DevAms::DryStatus status)
{
    switch (status) {
    case Slic3r::DevAms::DryStatus::Off:                         return _u8L("Idle");
    case Slic3r::DevAms::DryStatus::Checking:                    return _u8L("Checking");
    case Slic3r::DevAms::DryStatus::Drying:                      return _u8L("Drying");
    case Slic3r::DevAms::DryStatus::Cooling:                     return _u8L("Cooling");
    case Slic3r::DevAms::DryStatus::Stopping:                    return _u8L("Stopping");
    case Slic3r::DevAms::DryStatus::Error:                       return _u8L("Error");
    case Slic3r::DevAms::DryStatus::CannotStopHeatOutofControl:  return _u8L("Heat fault");
    case Slic3r::DevAms::DryStatus::PrdTesting:                  return _u8L("Self test");
    }
    return _u8L("Idle");
}

} // namespace

namespace Slic3r { namespace GUI {

namespace {

int addr_id_to_display_idx(int addr_id)
{
    return addr_id + 1;
}

std::string trim_copy(std::string value)
{
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string remove_spaces_copy(std::string value)
{
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char ch) { return std::isspace(ch); }), value.end());
    return value;
}

std::string StripFilamentVendorPrefix(std::string shown_name, const std::string& vendor, const std::string& fallback_type)
{
    std::string value = trim_copy(std::move(shown_name));
    if (const auto at_pos = value.find(" @"); at_pos != std::string::npos) {
        value = trim_copy(value.substr(0, at_pos));
    }
    if (value.empty()) return fallback_type;

    std::vector<std::string> aliases;
    if (!vendor.empty()) {
        aliases.push_back(vendor);
        aliases.push_back(remove_spaces_copy(vendor));
        if (vendor == "Bambu Lab") aliases.push_back("Bambu");
    }
    for (const auto& alias : aliases) {
        if (alias.empty()) continue;
        if (value == alias) return fallback_type;
        if (value.rfind(alias + " ", 0) == 0) {
            value = trim_copy(value.substr(alias.size() + 1));
            break;
        }
    }

    return value.empty() ? fallback_type : value;
}

std::vector<AmsHotendSchema::format::BindAms> bind_ams_from_json(const nlohmann::json& items)
{
    namespace SK = AmsHotendSchema::keys;

    std::vector<AmsHotendSchema::format::BindAms> result;
    if (!items.is_array()) return result;

    result.reserve(items.size());
    for (const auto& item : items) {
        result.push_back({
            item.value(SK::bind_ams::ams_id, std::string()),
            item.value(SK::bind_ams::ams_type, std::string()),
            item.value(SK::bind_ams::slot_count, 0)
        });
    }
    return result;
}

} // namespace

nlohmann::json DevicePageAmsFilamentHotendVM::OnCommand(const std::string& submod,
                                                        const std::string& action,
                                                        const nlohmann::json& payload)
{
    namespace SV = AmsHotendSchema::values;
    try {
        if (submod == "state" && (action == "get" || action == "init")) {
            // Mode dispatch: Web includes `{ mode: "..." }` in the payload to
            // request a specific page variant (normal / batch_ams_load / ...).
            // An unknown or missing mode falls back to "normal" so we never
            // refuse a state request. The chosen mode sticks across
            // subsequent ReportState() pushes until Web sends a new get.
            const std::string requested_mode = payload.value("mode", std::string(SV::mode::normal));
            if (requested_mode == SV::mode::normal
             || requested_mode == SV::mode::batch_ams_load
             || requested_mode == SV::mode::batch_ext_load
             || requested_mode == SV::mode::batch_unload) {
                m_active_mode = requested_mode;
            } else {
                m_active_mode = SV::mode::normal;
            }
            m_last_report_state.clear();
            nlohmann::json state = BuildState();
            // Surface the current Studio colour mode so the Web layer can apply
            // its `data-theme` attribute on first paint without waiting for a
            // separate `theme_changed` Report. Mirrors FilamentManagerVM.
            const bool dark = wxGetApp().app_config->get("dark_color_mode") == "1";
            state["theme"] = dark ? "dark" : "light";
            return MakeResponse(GetModule(), submod, action, 0, "", state);
        }
        if (submod == "action") {
            return HandleAction(action, payload);
        }
        if (submod == "debug") {
            return HandleDebug(action, payload);
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "DevicePageAmsFilamentHotendVM::OnCommand exception: " << e.what();
        return MakeResponse(GetModule(), submod, action, 2, e.what(), BuildState());
    }

    return MakeResponse(GetModule(), submod, action, 1, "unknown device page AMS filament hotend command", BuildState());
}

void DevicePageAmsFilamentHotendVM::ReportState(const std::string& submod, const std::string& action)
{
    if (!m_bridge) return;
    nlohmann::json state = BuildState();
    // Device data may contain non-UTF-8 bytes; strict dump() throws
    // type_error.316. Skip this report instead of letting it crash the page.
    std::string state_key;
    try {
        state_key = state.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << " state.dump() failed: " << e.what();
        return;
    }
    if (state_key == m_last_report_state) {
        return;
    }
    
    m_last_report_state = state_key;
    m_bridge->ReportMsg(MakeResponse(GetModule(), submod, action, 0, "", state));
}

void DevicePageAmsFilamentHotendVM::OnSysColorChanged()
{
    // Theme is global UI state â€?it must NOT be routed through
    // ReportState("state", ...) because that path is de-duped against the
    // last BuildState() snapshot; when the device payload is unchanged
    // (e.g. the user just toggled dark mode without touching the AMS) the
    // notification would otherwise be silently dropped. Emit an independent
    // `init/theme_changed` Report carrying the resolved theme so the Web
    // layer can update `<html data-theme>` regardless of device state.
    if (!m_bridge) return;
    const bool dark = wxGetApp().app_config->get("dark_color_mode") == "1";
    nlohmann::json data;
    data["theme"] = dark ? "dark" : "light";
    m_bridge->ReportMsg(MakeResponse(GetModule(), "init", "theme_changed", 0, "", data));
}

std::string DevicePageAmsFilamentHotendVM::ResolveFilamentCardText(const DevAmsTray* tray, const std::string& fallback_type) const
{
    if (!tray) return fallback_type;
    if (!tray->sub_brands.empty()) return tray->sub_brands;

    PresetBundle* preset_bundle = wxGetApp().preset_bundle;
    if (preset_bundle) {
        if (auto fila_item = preset_bundle->get_filament_by_filament_id(tray->get_filament_id(), "", true); fila_item.has_value()) {
            return StripFilamentVendorPrefix(fila_item->filament_name, fila_item->vendor, fallback_type);
        }

        if (auto fila_item = preset_bundle->get_filament_by_filament_id(tray->get_filament_id(), "", false); fila_item.has_value()) {
            return StripFilamentVendorPrefix(fila_item->filament_name, fila_item->vendor, fallback_type);
        }
    }

    return fallback_type;
}

nlohmann::json DevicePageAmsFilamentHotendVM::HandleAction(const std::string& action, const nlohmann::json& payload)
{
    auto* dev_mgr = wxGetApp().getDeviceManager();
    MachineObject* machine_obj = dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
    if (!machine_obj) {
        return MakeResponse(GetModule(), "action", action, 1, _u8L("No selected machine"), BuildState());
    }

    if (auto result = DevicePageExtAmsActionHandler::TryHandle(*this, action, payload, machine_obj)) {
        return *result;
    }
    if (auto result = DevicePageHotendActionHandler::TryHandle(*this, action, payload, machine_obj)) {
        return *result;
    }
    if (auto result = DevicePageFilamentActionHandler::TryHandle(*this, action, payload, machine_obj)) {
        return *result;
    }

    return MakeResponse(GetModule(), "action", action, 0, "", BuildState());
}

nlohmann::json DevicePageAmsFilamentHotendVM::BuildDefaultState() const
{
    return AmsHotendSchema::format::State{};
}

nlohmann::json DevicePageAmsFilamentHotendVM::BuildState() const
{
    namespace SK = AmsHotendSchema::keys;
    namespace SV = AmsHotendSchema::values;

    nlohmann::json data = BuildDefaultState();

    auto* dev_mgr = wxGetApp().getDeviceManager();
    MachineObject* machine_obj = dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
    if (!machine_obj) {
        data[SK::root::filament_page][SK::filament_page::actions][SK::filament_actions::tips] = _u8L("No selected machine");
        return data;
    }

    auto& fp = data[SK::root::filament_page];
    auto& hp = data[SK::root::hotend_page];
    AmsHotendSchema::format::AmsExtOperationPage ext_operation_page;

    BuildHotendRackStatus(machine_obj, hp);
    const bool filament_changing = IsFilamentChangeActive(machine_obj);
    if (filament_changing) {
        fp[SK::filament_page::actions][SK::filament_actions::can_load]          = false;
        fp[SK::filament_page::actions][SK::filament_actions::can_load_external] = false;
        fp[SK::filament_page::actions][SK::filament_actions::can_show_load_auto] = false;
        fp[SK::filament_page::actions][SK::filament_actions::can_unload]        = false;
    }

    if (auto fila_system = machine_obj->GetFilaSystem()) {
        const bool has_connected_ams = !fila_system->GetAmsList().empty() && machine_obj->ams_exist_bits != 0;
        if (!has_connected_ams) {
            fp[SK::filament_page::actions][SK::filament_actions::can_show_load_auto] = false;
        }

        fp[SK::filament_page::actions][SK::filament_actions::show_ams_settings] = has_connected_ams;
        fp[SK::filament_page::actions][SK::filament_actions::show_auto_refill]  = has_connected_ams;
        // Per-slot ams_operation.units is the canonical source for AMS-unit
        // detail (the AMS Operation dialog reads from there). The Filament tab
        // reads dryer chamber data from `ams_dry_zones` instead â€?slots no
        // longer carry their own dryer fields.
        //
        // Only zones whose physical layout actually supports drying are
        // shipped (matches `DevAms::Zone::SupportDrying()`, today that means
        // `zone_slots.size() == 2`). Non-drying zones (e.g. the "everything
        // else" bucket some AMS units report) MUST be filtered out or the
        // DryerChamber decoration would wrap unrelated columns.
        for (const auto& [_, ams_unit] : fila_system->GetAmsList()) {
            if (!ams_unit) continue;
            for (const auto& [zone_id, zone] : ams_unit->GetZones()) {
                if (!zone.SupportDrying()) continue;
                const std::string dry_status_enum = zone.dry_status.has_value()
                    ? dry_status_short_name(*zone.dry_status)
                    : std::string();
                const std::string dry_status_label = zone.dry_status.has_value()
                    ? dry_status_display_label(*zone.dry_status)
                    : _u8L("Idle");
                fp[SK::filament_page::ams_dry_zones].push_back({
                    {SK::dryer_zone::ams_id,          ams_unit->GetAmsId()},
                    {SK::dryer_zone::zone_slots,      zone.zone_slots},
                    {SK::dryer_zone::temp,            zone.current_temperature},
                    {SK::dryer_zone::humidity_raw,    zone.humidity_percent},
                    {SK::dryer_zone::dry_time,        zone.left_dry_time},
                    {SK::dryer_zone::dry_status,      dry_status_enum},
                    {SK::dryer_zone::dry_status_text, dry_status_label}
                });
            }
        }

    }

    std::vector<int> addr_ids;
    std::map<int, std::optional<std::pair<int, int>>> addr_slots;
    const auto& rack_type = machine_obj->GetNozzleRack()->GetRackType();
    if (rack_type == DevNozzleRack::RackType_A || rack_type == DevNozzleRack::RackType_B) {
        auto addr_id_set = machine_obj->GetNozzleRack()->GetTotalRackNozzleAddIdSet();
        for (auto addr_id : addr_id_set) {
            const auto& rack_nozzle_opt = machine_obj->GetNozzleSystem()->GetNozzleOptByAddrId(addr_id);
            if (!rack_nozzle_opt.has_value()) {
                addr_slots[addr_id] = std::nullopt;
                addr_ids.push_back(addr_id);
                continue;
            }

            const auto& ams_slots = rack_nozzle_opt->GetBindAmsSlots();
            if (!ams_slots.empty()) {
                int ams_id = ams_slots[0].first;
                int slot_id = ams_slots[0].second;
                if (auto fila_system = machine_obj->GetFilaSystem(); fila_system && fila_system->GetVirtualTray(ams_id).has_value()) {
                    slot_id = 0;
                }
                addr_slots[addr_id] = std::make_pair(ams_id, slot_id);
            } else {
                addr_slots[addr_id] = std::nullopt;
            }
            addr_ids.push_back(addr_id);
        }
    }

    std::sort(addr_ids.begin(), addr_ids.end(), [](int lhs, int rhs) {
        return addr_id_to_display_idx(lhs) < addr_id_to_display_idx(rhs);
    });

    // Fallback for empty dryer slots without AMS binding.
    const Slic3r::DevAms* fallback_dryer_unit = nullptr;
    if (auto fila_system = machine_obj->GetFilaSystem()) {
        for (const auto& [_, ams_unit] : fila_system->GetAmsList()) {
            if (ams_unit && ams_unit->SupportDrying()) {
                fallback_dryer_unit = ams_unit;
                break;
            }
        }
    }

    // Field sets, indexed by both names, used to project the full slot json
    // into the per-page subsets.
    static const std::vector<const char*> kFilamentSlotFields = {
        SK::filament_slot::addr_id,
        SK::filament_slot::display_index,
        SK::filament_slot::ams_id,
        SK::filament_slot::slot_id,
        SK::filament_slot::bind_ams,
        SK::filament_slot::bind_ams_slot_count,
        SK::filament_slot::enabled,
        SK::filament_slot::is_external_spool,
        SK::filament_slot::is_top_tier,
        SK::filament_slot::menu_actions,
        SK::filament_slot::ams_operation,
        SK::filament_slot::filament_card,
        SK::filament_slot::filament_line,
        SK::filament_slot::filament_nozzle,
    };

    std::string fallback_unloading_tip;
    nlohmann::json running_change_slot = nullptr;
    std::string running_change_operation = "change";
    std::string running_change_message;
    for (int addr_id : addr_ids) {
        nlohmann::json full_slot = BuildSlot(machine_obj, addr_id, addr_slots[addr_id], fallback_dryer_unit);
        if (filament_changing) {
            const auto& line_section = full_slot.value(SK::filament_slot::filament_line, nlohmann::json::object());
            const std::string line_state = line_section.value(SK::filament_line::state, std::string());
            auto& tips = fp[SK::filament_page::actions][SK::filament_actions::tips];
            // "Slot N filament loading/unloading in progress" â€?translated on
            // the C++ side so the Web only prints the localized string.
            const std::string change_op = _u8L("loading/unloading");
            if (line_state == SV::line_state::loading) {
                tips = (boost::format(_u8L("Slot %1% filament %2% in progress"))
                    % full_slot.value(SK::filament_slot::display_index, 0)
                    % change_op).str();
                if (running_change_slot.is_null()) {
                    running_change_message = tips.get<std::string>();
                    running_change_slot = {
                        {SK::running_slot::ams_id,        full_slot.value(SK::filament_slot::ams_id, std::string())},
                        {SK::running_slot::slot_id,       full_slot.value(SK::filament_slot::slot_id, std::string())},
                        {SK::running_slot::display_index, full_slot.value(SK::filament_slot::display_index, 0)}
                    };
                }
            } else if (line_state == SV::line_state::unloading && fallback_unloading_tip.empty()) {
                fallback_unloading_tip = (boost::format(_u8L("Slot %1% filament %2% in progress"))
                    % full_slot.value(SK::filament_slot::display_index, 0)
                    % change_op).str();
                if (running_change_slot.is_null()) {
                    running_change_message = fallback_unloading_tip;
                    running_change_slot = {
                        {SK::running_slot::ams_id,        full_slot.value(SK::filament_slot::ams_id, std::string())},
                        {SK::running_slot::slot_id,       full_slot.value(SK::filament_slot::slot_id, std::string())},
                        {SK::running_slot::display_index, full_slot.value(SK::filament_slot::display_index, 0)}
                    };
                }
            }
        }

        // Project to per-page slot shapes.
        nlohmann::json filament_slot_json = nlohmann::json::object();
        for (const char* key : kFilamentSlotFields) {
            if (full_slot.contains(key)) {
                if (std::string(key) == SK::filament_slot::ams_operation) {
                    const auto& op = full_slot.at(key);
                    filament_slot_json[key] = {
                        {SK::ams_operation::available, op.value(SK::ams_operation::available, false)}
                    };
                } else {
                    filament_slot_json[key] = full_slot.at(key);
                }
            }
        }
        fp[SK::filament_page::hotend_slots].push_back(std::move(filament_slot_json));

        nlohmann::json hotend_slot_json = {
            {SK::hotend_slot::addr_id,    full_slot.value(SK::filament_slot::addr_id,     0)},
            {SK::hotend_slot::display_index, full_slot.value(SK::filament_slot::display_index,  0)},
            {SK::hotend_slot::ams_id,        full_slot.value(SK::filament_slot::ams_id,         std::string())},
            {SK::hotend_slot::slot_id,       full_slot.value(SK::filament_slot::slot_id,        std::string())},
            {SK::hotend_slot::enabled,       full_slot.value(SK::filament_slot::enabled,        false)},
            {SK::hotend_slot::taken,         full_slot.value(SK::filament_slot::is_on_extruder, false)},
            {SK::hotend_slot::menu_actions,  full_slot.value(SK::filament_slot::hotend_menu_actions, nlohmann::json::object())},
            {SK::hotend_slot::hotend,        full_slot.value(SK::filament_slot::hotend,         nlohmann::json::object())},
            {SK::hotend_slot::hotend_detail_dialog, full_slot.value(SK::filament_slot::hotend_detail_dialog, nlohmann::json::object())}
        };
        hp[SK::hotend_page::hotend_slots].push_back(std::move(hotend_slot_json));

        const auto& ams_operation = full_slot.value(SK::filament_slot::ams_operation, nlohmann::json::object());
        if (ams_operation.value(SK::ams_operation::available, false)) {
            ext_operation_page.slots.push_back({
                full_slot.value(SK::filament_slot::addr_id, 0),
                full_slot.value(SK::filament_slot::display_index, 0),
                full_slot.value(SK::filament_slot::ams_id, std::string()),
                full_slot.value(SK::filament_slot::slot_id, std::string()),
                bind_ams_from_json(full_slot.value(SK::filament_slot::bind_ams, nlohmann::json::array())),
                full_slot.value(SK::filament_slot::bind_ams_slot_count, 0),
                ams_operation.value(SK::ams_ext_operation_slot::units, nlohmann::json::array())
            });
        }
    }
    data[SK::root::ams_ext_operation_page] = ext_operation_page;
    auto& fp_tips = fp[SK::filament_page::actions][SK::filament_actions::tips];
    if (filament_changing && fp_tips.get<std::string>().empty() && !fallback_unloading_tip.empty()) {
        fp_tips = fallback_unloading_tip;
    }
    if (filament_changing) {
        if (running_change_message.empty()) {
            running_change_message = fp_tips.get<std::string>();
        }
        fp[SK::filament_page::filament_change_status] = DevicePageFilamentChangeStatusBuilder::Build(machine_obj, running_change_slot, running_change_operation, running_change_message);
    }

    // Mode dispatch: for non-normal modes, hand the prebuilt normal-shape
    // filament_page off to the matching builder. The builder returns the
    // batch-shaped page (per-slot select_action + {enabled, reason} actions),
    // which replaces the normal filament_page in the final payload. Normal
    // mode just stamps `mode = "normal"` on the existing object.
    //
    // hotend_page is unaffected: batch UI lives entirely on the Filament tab.
    {
        std::unique_ptr<IFilamentPageModeBuilder> builder;
        if (m_active_mode == SV::mode::batch_ams_load) {
            builder = std::make_unique<BatchAmsLoadModeBuilder>();
        } else if (m_active_mode == SV::mode::batch_ext_load) {
            builder = std::make_unique<BatchExtLoadModeBuilder>();
        } else if (m_active_mode == SV::mode::batch_unload) {
            builder = std::make_unique<BatchUnloadModeBuilder>();
        } else {
            builder = std::make_unique<NormalModeBuilder>();
        }
        data[SK::root::filament_page] = builder->BuildFilamentPage(*this, machine_obj, fp);
    }

    return data;
}

}} // namespace Slic3r::GUI
