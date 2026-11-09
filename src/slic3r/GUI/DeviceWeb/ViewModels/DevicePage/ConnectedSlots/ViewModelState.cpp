/*****************************************************************//**
 * \file   ViewModelState.cpp
 * \brief  State building for the Connected Slots ViewModel.
 *
 * Owns ConnectedSlotsVM::BuildState() and every device-model to JSON helper it
 * needs. Command dispatch lives in ViewModel.cpp, command handling in
 * ViewModelActions.cpp.
 *********************************************************************/
#include "ViewModel.hpp"
#include "Schema.hpp"

#include "slic3r/GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/BuilderFilamentChangeStatus.hpp"
#include "slic3r/GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/ResolverSlotState.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/DeviceCore/DevDefs.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"
#include "libslic3r/PresetBundle.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "libslic3r/Log.hpp"

namespace Slic3r { namespace GUI {

namespace SchemaFormat = ConnectedSlotsSchema::format;
namespace SV = ConnectedSlotsSchema::values;
namespace SK = ConnectedSlotsSchema::keys;

namespace {

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

std::string color_type_for_web(DevFilaColorType color_type)
{
    switch (color_type) {
    case DevFilaColorType::CTYPE_MULTI:    return SV::color_type::multi;
    case DevFilaColorType::CTYPE_GRADIANT: return SV::color_type::gradient;
    case DevFilaColorType::CTYPE_SINGLE:   return SV::color_type::single;
    }
    return SV::color_type::single;
}

/// Stable enum-like labels; the Web compares them against literals, so they
/// must stay untranslated (mirrors AmsFilamentHotend).
const char* dry_status_short_name(DevAms::DryStatus status)
{
    switch (status) {
    case DevAms::DryStatus::Off:                        return "Idle";
    case DevAms::DryStatus::Checking:                   return "Checking";
    case DevAms::DryStatus::Drying:                     return "Drying";
    case DevAms::DryStatus::Cooling:                    return "Cooling";
    case DevAms::DryStatus::Stopping:                   return "Stopping";
    case DevAms::DryStatus::Error:                      return "Error";
    case DevAms::DryStatus::CannotStopHeatOutofControl: return "Heat fault";
    case DevAms::DryStatus::PrdTesting:                 return "Self test";
    }
    return "Idle";
}

/// User-facing copy of dry_status_short_name (mirrors AmsFilamentHotend).
std::string dry_status_display_label(DevAms::DryStatus status)
{
    switch (status) {
    case DevAms::DryStatus::Off:                        return _u8L("Idle");
    case DevAms::DryStatus::Checking:                   return _u8L("Checking");
    case DevAms::DryStatus::Drying:                     return _u8L("Drying");
    case DevAms::DryStatus::Cooling:                    return _u8L("Cooling");
    case DevAms::DryStatus::Stopping:                   return _u8L("Stopping");
    case DevAms::DryStatus::Error:                      return _u8L("Error");
    case DevAms::DryStatus::CannotStopHeatOutofControl: return _u8L("Heat fault");
    case DevAms::DryStatus::PrdTesting:                 return _u8L("Self test");
    }
    return _u8L("Idle");
}

bool is_nozzle_on_extruder(const MachineObject* obj, int addr_id)
{
    if (!obj || !obj->GetExtderSystem()) return false;
    for (const auto& ext : obj->GetExtderSystem()->GetExtruders()) {
        if (ext.GetPickedNozzleAddrId().value_or(-1) == addr_id) return true;
    }
    return false;
}

bool is_current_changing_slot(const MachineObject* obj, const std::optional<DevAmsSlotId>& slot)
{
    if (!obj || !obj->GetFilaSystem() || !slot.has_value()) return false;
    return obj->GetFilaSystem()->IsCurrentFilamentChangingSlot(slot->first, slot->second);
}

std::optional<DevAmsSlotId> to_slot_id(const std::string& ams_id, const std::string& slot_id)
{
    try {
        return DevAmsSlotId{std::stoi(ams_id), std::stoi(slot_id)};
    } catch (const std::exception&) {
        BOOST_LOG_TRIVIAL(warning) << current_exception_message();
        return std::nullopt;
    }
}

/// 正常喷嘴: a nozzle tool (not a cutter / laser / fan) is seated in this hotend
/// and reports no fault. Every tray menu action requires one.
bool is_normal_nozzle(const std::optional<DevNozzle>& nozzle)
{
    return nozzle.has_value() && nozzle->IsNormal()
        && nozzle->GetToolDefs().tool_type == DevNozzleToolType::Nozzle;
}

/// Per-hotend facts the tray menu rules need on top of the tray itself. Built
/// once per BuildState() and threaded down to every tray.
struct TrayMenuContext
{
    bool nozzle_normal        = false; ///< 正常喷嘴
    bool ext_loaded_in_nozzle = false; ///< 当前喷嘴里进的是外挂料
    bool filament_changing    = false; ///< fila_sys 正在进退料流程
};

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

std::string strip_filament_vendor_prefix(std::string shown_name, const std::string& vendor, const std::string& fallback_type)
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

std::string resolve_filament_name(const DevAmsTray* tray, const std::string& fallback_type)
{
    if (!tray) return fallback_type;
    if (!tray->sub_brands.empty()) return tray->sub_brands;

    PresetBundle* preset_bundle = wxGetApp().preset_bundle;
    if (preset_bundle) {
        for (bool only_installed : {true, false}) {
            if (auto fila_item = preset_bundle->get_filament_by_filament_id(tray->get_filament_id(), "", only_installed); fila_item.has_value()) {
                return strip_filament_vendor_prefix(fila_item->filament_name, fila_item->vendor, fallback_type);
            }
        }
    }
    return fallback_type;
}

/// Priority mirrors DevicePageFilamentSlotStateResolver::ResolveCardState, so a
/// tray reads the same on this page as on the filament page: material identity
/// (empty / reading / unknown) wins over the transport state.
std::string tray_state_for_web(MachineObject*         obj,
                               const std::string&     ams_id,
                               const std::string&     slot_id,
                               const DevAmsTray*      tray,
                               const TrayMenuContext& ctx)
{
    if (!tray || !tray->is_exists) return SV::tray_state::empty;
    if (tray->is_reading(obj)) return SV::tray_state::reading;
    if (tray->setting_id.empty() || tray->m_fila_type.empty()) return SV::tray_state::unknown;

    // 进退料中: only the one slot the firmware is actually moving animates. A
    // slot already sitting in the nozzle is on its way out, otherwise it is on
    // its way in.
    const auto slot = to_slot_id(ams_id, slot_id);
    if (ctx.filament_changing && is_current_changing_slot(obj, slot)) {
        return DevicePageFilamentSlotStateResolver::IsTraySlotLoadedInNozzle(obj, tray)
                   ? SV::tray_state::unloading
                   : SV::tray_state::loading;
    }
    return SV::tray_state::ready;
}

/**
 * \brief Visibility of the tray context menu (编辑 / 进料 / 退料 / 读取).
 *
 * All four actions need 正常喷嘴 plus a tray that really holds filament. On top
 * of that: read is RFID-only, so it excludes the external spool and a hotend
 * already fed by one; load and unload are two sides of "is this tray the one in
 * the nozzle" and both need the filament path to be idle.
 */
nlohmann::json build_tray_actions_json(MachineObject*         obj,
                                       const std::string&     ams_id,
                                       const DevAmsTray*      tray,
                                       const std::string&     tray_state,
                                       const TrayMenuContext& ctx)
{
    const bool tray_has_filament = tray && tray->is_exists && tray_state != SV::tray_state::empty;
    const bool tray_is_reading = (tray_state == SV::tray_state::reading);
    const bool tray_loaded_in_nozzle   = DevicePageFilamentSlotStateResolver::IsTraySlotLoadedInNozzle(obj, tray);
    const bool is_ams_slot = obj && obj->GetFilaSystem() && !obj->GetFilaSystem()->IsVirtualTrayId(ams_id);

    return {
        {SK::tray_actions::can_edit,   ctx.nozzle_normal && tray_has_filament && !tray_is_reading},
        {SK::tray_actions::can_load,   ctx.nozzle_normal && tray_has_filament && !tray_is_reading && !ctx.filament_changing && !tray_loaded_in_nozzle},
        {SK::tray_actions::can_unload, ctx.nozzle_normal && tray_has_filament && !tray_is_reading && !ctx.filament_changing && tray_loaded_in_nozzle},
        {SK::tray_actions::can_read,   ctx.nozzle_normal && tray_has_filament && !tray_is_reading && !ctx.filament_changing && is_ams_slot},
        {SK::tray_actions::read_label, tray_state == SV::tray_state::unknown ? SV::read_label::read : SV::read_label::reread}
    };
}

nlohmann::json build_tray_json(MachineObject*         obj,
                               const std::string&     ams_id,
                               const std::string&     slot_id,
                               const DevAmsTray*      tray,
                               const TrayMenuContext& ctx)
{
    std::vector<std::string> colors;
    if (tray) {
        colors = tray->cols;
        if (colors.empty() && !tray->color.empty()) colors.push_back(tray->color);
        for (auto& c : colors) c = normalize_hex_for_web(c);
    }

    const std::string tray_state = tray_state_for_web(obj, ams_id, slot_id, tray, ctx);

    return {
        {SK::ams_tray::ams_id,        ams_id},
        {SK::ams_tray::slot_id,       slot_id},
        {SK::ams_tray::exists,        tray && tray->is_exists},
        {SK::ams_tray::tray_state,    tray_state},
        {SK::ams_tray::filament_type, tray ? tray->m_fila_type : std::string()},
        {SK::ams_tray::filament_name, tray ? resolve_filament_name(tray, tray->m_fila_type) : std::string()},
        {SK::ams_tray::color,         normalize_hex_for_web(tray ? tray->color : std::string())},
        {SK::ams_tray::color_type,    color_type_for_web(tray ? tray->ctype : DevFilaColorType::CTYPE_SINGLE)},
        {SK::ams_tray::colors,        colors},
        {SK::ams_tray::remain,        DevicePageFilamentSlotStateResolver::ResolveDisplayRemain(tray)},
        {SK::ams_tray::actions,       build_tray_actions_json(obj, ams_id, tray, tray_state, ctx)}
    };
}

/// GetTrays() is keyed by the stringified slot id, so its map order puts "10"
/// before "2". Sort numerically to keep the rendered tray order stable.
std::vector<std::pair<int, const DevAmsTray*>> sorted_trays(const DevAms* ams)
{
    std::vector<std::pair<int, const DevAmsTray*>> result;
    if (!ams) return result;
    for (const auto& [slot_id_str, tray] : ams->GetTrays()) {
        int slot_id = 0;
        try {
            slot_id = std::stoi(slot_id_str);
        } catch (const std::exception&) {
            continue;
        }
        result.emplace_back(slot_id, tray);
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    return result;
}

/// \param only_slot limits the unit to a single tray, used by the rack column
/// where the shared rack AMS exposes one slot per hotend position.
nlohmann::json build_unit_json(MachineObject*         obj,
                               const DevAms*          ams,
                               std::optional<int>     only_slot,
                               const TrayMenuContext& ctx)
{
    if (!ams) return nullptr;

    nlohmann::json trays = nlohmann::json::array();
    for (const auto& [slot_id, tray] : sorted_trays(ams)) {
        if (only_slot.has_value() && slot_id != *only_slot) continue;
        trays.push_back(build_tray_json(obj, ams->GetAmsId(), std::to_string(slot_id), tray, ctx));
    }

    const float temp = ams->GetCurrentTemperature();
    const auto dry_status = ams->GetDryStatus();
    std::string dry_status_text;
    if (ams->SupportDrying()) {
        dry_status_text = dry_status.has_value() ? dry_status_display_label(*dry_status) : _u8L("Idle");
    }

    int humidity_level = ams->SupportHumidityLevel() ? ams->GetHumidityLevel() : -1;
    std::string humidity_display_type = SV::humidity_display_type::none;
    if (ams->SupportHumidityPercent())
        humidity_display_type = SV::humidity_display_type::percent;
    else if (ams->SupportHumidityLevel())
        humidity_display_type = SV::humidity_display_type::level;

    return {
        {SK::ams_unit::ams_id,       ams->GetAmsId()},
        {SK::ams_unit::ams_type,     std::to_string((int) ams->GetAmsType())},
        {SK::ams_unit::display_name, wx_to_utf8(ams->GetDisplayName())},
        {SK::ams_unit::slot_count,   only_slot.has_value() ? (int) trays.size() : ams->GetSlotCount()},
        {SK::ams_unit::can_dry,      ams->SupportDrying()},
        {SK::ams_unit::humidity_raw, ams->SupportHumidityPercent() ? ams->GetHumidityPercent() : -1},
        {SK::ams_unit::humidity_level,        humidity_level},
        {SK::ams_unit::humidity_display_type, humidity_display_type},
        {SK::ams_unit::temp,         temp != INVALID_AMS_TEMPERATURE ? nlohmann::json(temp) : nlohmann::json(nullptr)},
        {SK::ams_unit::dry_status,   dry_status.has_value() ? dry_status_short_name(*dry_status) : ""},
        {SK::ams_unit::dry_status_text, dry_status_text},
        {SK::ams_unit::trays,        trays}
    };
}

/// The external spool has no DevAms behind it; wrap its virtual tray in the same
/// ams_unit shape so the Web renders it with the regular tray card.
nlohmann::json build_ext_unit_json(MachineObject* obj, const DevAmsTray& ext_tray, const TrayMenuContext& ctx)
{
    nlohmann::json trays = nlohmann::json::array();
    trays.push_back(build_tray_json(obj, ext_tray.id, "0", &ext_tray, ctx));

    return {
        {SK::ams_unit::ams_id,       ext_tray.id},
        {SK::ams_unit::ams_type,     std::to_string((int) DevAmsType::EXT_SPOOL)},
        {SK::ams_unit::display_name, "Ext"},
        {SK::ams_unit::slot_count,   1},
        {SK::ams_unit::can_dry,      false},
        {SK::ams_unit::humidity_raw, -1},
        {SK::ams_unit::humidity_level,        -1},
        {SK::ams_unit::humidity_display_type, SV::humidity_display_type::none},
        {SK::ams_unit::temp,         nullptr},
        {SK::ams_unit::dry_status,   ""},
        {SK::ams_unit::dry_status_text, ""},
        {SK::ams_unit::trays,        trays}
    };
}

/// GetBindAms() also lists the built-in rack AMS, which owns the left column and
/// cannot unload on its own (mirrors DevMapping::_collect_bound_sub_ams).
std::vector<const DevAms*> collect_bound_sub_ams(const MachineObject* obj, const DevNozzle& nozzle)
{
    std::vector<const DevAms*> result;
    auto fila_sys = obj->GetFilaSystem();
    if (!fila_sys) return result;

    for (int bind_ams_id : nozzle.GetBindAms()) {
        const DevAms* ams = fila_sys->GetAmsById(std::to_string(bind_ams_id));
        if (!ams) continue;
        const auto type = ams->GetAmsType();
        if (type != DevAmsType::N3S && type != DevAmsType::N3F && type != DevAmsType::AMS) continue;
        result.push_back(ams);
    }
    return result;
}

std::string join_tray_colors(const DevAms* ams)
{
    std::string joined;
    for (const auto& [slot_id, tray] : sorted_trays(ams)) {
        if (!joined.empty()) joined += ",";
        joined += normalize_hex_for_web(tray && tray->is_exists ? tray->color : std::string());
    }
    return joined;
}

SchemaFormat::FilamentLine build_column_line(const MachineObject*               obj,
                                             const std::optional<DevAmsSlotId>& feed,
                                             int                                ams_id)
{
    SchemaFormat::FilamentLine line;
    if (feed.has_value() && feed->first == ams_id) {
        auto tray = obj->get_tray(feed->first, feed->second);
        if (tray.has_value() && tray->is_exists) {
            line.state          = SV::line_state::loaded;
            line.has_filament   = true;
            line.filament_color = normalize_hex_for_web(tray->color);
        }
    }

    return line;
}

/// The rack AMS mirrors the rack layout, so a nozzle's own slot is its position
/// index (mirrors DevMapping::_find_own_rack_ams_slot).
std::optional<DevAmsSlotId> find_own_rack_slot(const MachineObject* obj, int addr_id)
{
    auto fila_sys = obj->GetFilaSystem();
    auto rack     = obj->GetNozzleRack();
    if (!fila_sys || !rack) return std::nullopt;

    for (const auto& [ams_id_str, ams] : fila_sys->GetAmsList()) {
        if (!ams || ams->GetSlotCount() != rack->GetTotalRackNozzleCount()) continue;
        const DevAmsSlotId slot{ams->GetAmdIdInt(), addr_id};
        if (fila_sys->GetAmsTray(std::to_string(slot.first), std::to_string(slot.second))) return slot;
    }
    return std::nullopt;
}

} // namespace

nlohmann::json ConnectedSlotsVM::BuildState() const
{
    SchemaFormat::State state;
    state.session_seq = m_session_seq;
    if (!m_obj || !m_obj->GetFilaSystem() || !m_obj->GetNozzleSystem()) return state;

    state.selected_dev_id = m_obj->get_dev_id();

    const int addr_id = m_addr_id >= 0 ? m_addr_id : 0;
    auto fila_sys = m_obj->GetFilaSystem();
    const auto nozzle = m_obj->GetNozzleSystem()->GetNozzleOptByAddrId(addr_id);
    const auto feed   = nozzle.has_value() && nozzle->HasFilamentAtNozzleInlet() ? nozzle->GetSlotNowId() : std::nullopt;

    const bool filament_changing = DevicePageFilamentChangeStatusBuilder::IsActive(m_obj);
    const TrayMenuContext menu_ctx{
        is_normal_nozzle(nozzle),
        nozzle.has_value() && nozzle->HasFilamentAtNozzleInlet()
            && feed.has_value() && fila_sys->IsVirtualTrayId(std::to_string(feed->first)),
        filament_changing
    };

    SchemaFormat::Slot slot;
    slot.addr_id       = addr_id;
    slot.display_index = addr_id + 1;

    // 1.1 左列: the rack AMS slot this hotend owns, plus the rack position indicator.
    SchemaFormat::Column& left = slot.top_slots_area.left_slot;
    left.schematic.kind = SV::source_kind::ams;
    if (auto rack = m_obj->GetNozzleRack()) {
        left.schematic.slot_count     = rack->GetTotalRackNozzleCount();
        left.schematic.selected_index = addr_id;
        left.schematic.selected       = true;
    }
    if (auto own_slot = find_own_rack_slot(m_obj, addr_id); own_slot.has_value()) {
        const DevAms* rack_ams = fila_sys->GetAmsById(std::to_string(own_slot->first));
        left.unit = build_unit_json(m_obj, rack_ams, own_slot->second, menu_ctx);
        left.line = build_column_line(m_obj, feed, own_slot->first);
    }

    // 1.2 右列: Ext plus every bound sub-AMS; one of them is expanded below the bar.
    SchemaFormat::Column& right = slot.top_slots_area.right_slots;
    right.schematic.kind = SV::source_kind::ext;

    std::vector<const DevAms*> sources;      // parallel to items, null entry = Ext
    std::optional<DevAmsTray>  ext_tray;
    const int ext_tray_id = VIRTUAL_TRAY_MAIN_ID - addr_id;
    bool ext_source_active = true;
    if (nozzle.has_value() && nozzle->GetBindAms().size() > 0) {
        // only show loaded external spool when bind_ams is intalled
        ext_source_active = feed.has_value() && feed->first == ext_tray_id && fila_sys->IsVirtualTrayId(std::to_string(feed->first));
    }

    if (auto vt = m_obj->GetFilaSystem()->GetVirtualTray(ext_tray_id); ext_source_active && vt.has_value()) {
        ext_tray = vt;
        SchemaFormat::SchematicItem item;
        item.ext_index  = (int) right.schematic.items.size() + 1;
        item.label      = "Ext";
        item.color      = normalize_hex_for_web(vt->color);
        item.slot_count = 1;
        item.empty      = false;
        right.schematic.items.push_back(item);
        sources.push_back(nullptr);
    }
    if (nozzle.has_value()) {
        for (const DevAms* ams : collect_bound_sub_ams(m_obj, *nozzle)) {
            SchemaFormat::SchematicItem item;
            item.ext_index  = (int) right.schematic.items.size() + 1;
            item.ams_id     = ams->GetAmsId();
            item.label      = wx_to_utf8(ams->GetDisplayName());
            item.color      = join_tray_colors(ams);
            item.slot_count = ams->GetSlotCount();
            item.empty      = !ams->IsExist();
            right.schematic.items.push_back(item);
            sources.push_back(ams);
        }
    }

    // Honour an explicit user pick, otherwise follow whatever currently feeds.
    int selected_pos = -1;
    if (m_selected_ext_index >= 1 && m_selected_ext_index <= (int) right.schematic.items.size()) {
        selected_pos = m_selected_ext_index - 1;
    } else if (feed.has_value()) {
        for (size_t i = 0; i < sources.size(); ++i) {
            const bool matches = sources[i] ? sources[i]->GetAmdIdInt() == feed->first
                                            : devPrinterUtil::IsVirtualSlot(feed->first);
            if (matches) { selected_pos = (int) i; break; }
        }
    }
    if (selected_pos < 0 && !right.schematic.items.empty()) selected_pos = 0;

    if (selected_pos >= 0) {
        right.schematic.items[selected_pos].selected = true;
        right.schematic.selected = true;
        if (const DevAms* ams = sources[selected_pos]) {
            right.unit = build_unit_json(m_obj, ams, std::nullopt, menu_ctx);
            right.line = build_column_line(m_obj, feed, ams->GetAmdIdInt());
        } else if (ext_tray.has_value()) {
            right.unit = build_ext_unit_json(m_obj, *ext_tray, menu_ctx);
            try {
                right.line = build_column_line(m_obj, feed, std::stoi(ext_tray->id));
            } catch (const std::exception&) {
                BOOST_LOG_TRIVIAL(warning) << current_exception_message();
                // Malformed virtual tray id: leave the line in its no_filament default.
            }
        }
    }

    // 2. 下方料线 + 喷嘴.
    const DevAmsTray* feed_tray = nullptr;
    DevAmsTray        feed_tray_copy;
    std::string       feed_ams_id;
    std::string       feed_slot_id;
    if (feed.has_value()) {
        feed_ams_id  = std::to_string(feed->first);
        feed_slot_id = std::to_string(feed->second);
        if (auto tray = m_obj->get_tray(feed->first, feed->second); tray.has_value()) {
            feed_tray_copy = *tray;
            feed_tray      = &feed_tray_copy;
        }
    }

    const bool nozzle_has_filament = nozzle.has_value() && nozzle->HasFilamentAtNozzleInlet();
    const bool tray_reading        = feed_tray && feed_tray->is_reading(m_obj);
    const bool loaded_in_nozzle    = DevicePageFilamentSlotStateResolver::IsTraySlotLoadedInNozzle(m_obj, feed_tray);
    const bool current_filament_changing = feed.has_value() && fila_sys->IsCurrentFilamentChangingSlot(feed->first, feed->second);

    const std::string card_state = DevicePageFilamentSlotStateResolver::ResolveCardState(
        m_obj, feed_tray, nozzle, filament_changing, current_filament_changing,
        tray_reading);
    const std::string line_state = DevicePageFilamentSlotStateResolver::ResolveLineState(
        card_state, /*has_hotend_slot=*/true, filament_changing,
        current_filament_changing, loaded_in_nozzle);

    slot.bottom_hotend.line.state = line_state;
    if (feed_tray && feed_tray->is_exists) {
        slot.bottom_hotend.line.has_filament   = true;
        slot.bottom_hotend.line.filament_color = normalize_hex_for_web(feed_tray->color);
    }

    // Nozzle state
    slot.bottom_hotend.nozzle.state = DevicePageFilamentSlotStateResolver::ResolveNozzleState(m_obj, nozzle, line_state);
    slot.bottom_hotend.nozzle.has_filament = nozzle_has_filament;
    if (nozzle_has_filament) {
        slot.bottom_hotend.nozzle.filament_color = normalize_hex_for_web(nozzle->GetFilamentColor());
    }
    slot.bottom_hotend.nozzle.on_extruder = is_nozzle_on_extruder(m_obj, addr_id);

    state.slots.push_back(std::move(slot));
    return state;
}

}} // namespace Slic3r::GUI
