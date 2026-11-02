#include "ExtruderHelpers.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iterator>
#include <sstream>

#include <boost/algorithm/string.hpp>

#include "DeviceCore/DevNozzleSystem.h"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "MsgDialog.hpp"
#include "Plater.hpp"
#include "libslic3r/LocalesUtils.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"
#include "slic3r/GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/ResolverSlotState.hpp"
#include "slic3r/GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/Schema.hpp"
#include "libslic3r/Log.hpp"

namespace Slic3r { namespace GUI {

std::string get_diameter_string(float diameter)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(1) << diameter;
    return stream.str();
}

bool nozzle_loses_detail(double from, double to)
{
    if (from <= 0. || to <= 0.) return false;
    const auto is = [](double diameter, double value) { return std::abs(diameter - value) < EPSILON; };
    return (is(from, 0.2) && !is(to, 0.2)) || (is(to, 0.8) && !is(from, 0.8));
}

wxString format_nozzle_spec(const NozzleConfig &cfg)
{
    if (cfg.diameter.IsEmpty()) return _L("Empty");

    const auto    &names      = ConfigOptionEnum<NozzleVolumeType>::get_enum_names();
    const int      volume_idx = static_cast<int>(cfg.volume);
    const wxString flow_label = (volume_idx >= 0 && volume_idx < static_cast<int>(names.size())) ? _L(names[volume_idx]) : wxString();

    if (flow_label.IsEmpty()) return cfg.diameter + "mm";
    return cfg.diameter + "mm " + flow_label;
}

NozzleLayout nozzle_layout_of(const ConfigBase &printer_config)
{
    const auto *variants   = printer_config.option<ConfigOptionStrings>("extruder_variant_list");
    const auto *max_counts = printer_config.option<ConfigOptionIntsNullable>("extruder_max_nozzle_count");
    const bool  dual       = variants != nullptr && variants->size() == 2;
    const bool  rack       = max_counts != nullptr && std::any_of(max_counts->values.begin(), max_counts->values.end(), [](int count) { return count > 1; });
    if (dual)
        return rack ? NozzleLayout::DualExtruderRack : NozzleLayout::DualExtruder;
    return rack ? NozzleLayout::SingleExtruderRack : NozzleLayout::SingleExtruder;
}

bool is_dual_extruder(NozzleLayout layout) { return layout == NozzleLayout::DualExtruder || layout == NozzleLayout::DualExtruderRack; }

bool has_nozzle_rack(NozzleLayout layout) { return layout == NozzleLayout::DualExtruderRack || layout == NozzleLayout::SingleExtruderRack; }

std::string slot_name(MachineObject *obj, int ams_id, int slot_id)
{
    const auto &units = obj->GetFilaSystem()->GetAmsList();
    const auto  unit  = units.find(std::to_string(ams_id));
    if (unit == units.end()) return "Ext";

    const std::string slot_no = std::to_string(slot_id + 1);
    switch (unit->second->GetAmsType()) {
    case DevAmsType::ZMULTI_A:
    case DevAmsType::ZMULTI_B: return slot_no; // the slot id is its nozzle's address
    case DevAmsType::N3S: return "HT-" + std::string(1, char('A' + ams_id - 128));
    default: return std::string(1, char('A' + ams_id)) + slot_no;
    }
}

// Collect bound AMS ids per (logical_extruder_id, slot_kind) for the given machine.
// Keys use the *logical* extruder id.
AmsIdMap collect_ams_ids(MachineObject *obj)
{
    const int  extruder_nums     = obj->GetExtderSystem()->GetTotalExtderCount();
    const bool fila_switch_ready = wxGetApp().sidebar().is_fila_switch_ready();

    AmsIdMap ams_ids{
        {{0, 1}, {}},
        {{0, 4}, {}},
        {{1, 1}, {}},
        {{1, 4}, {}},
    };
    for (const auto &[_, ams] : obj->GetFilaSystem()->GetAmsList()) {
        for (auto extruder_id : ams->GetBindedExtruderSet()) {
            const auto ams_type = ams->GetAmsType();

            // built in AMS
            if (ams_type == ZMULTI_A || ams_type == ZMULTI_B) continue;

            if (fila_switch_ready) {
                auto switcher_pos = ams->GetSwitcherPos();
                if (!switcher_pos) continue;

                auto switcher_id = obj->is_main_extruder_on_left() ? (1 - static_cast<int>(switcher_pos.value())) : static_cast<int>(switcher_pos.value());
                if (extruder_id != switcher_id) continue;
            }

            auto logical_id = DevExtder::to_logical_extruder_id(extruder_nums, extruder_id);
            auto key        = std::make_pair(logical_id, ams_type == DevAmsType::N3S ? 1 : 4);
            ams_ids[key].push_back(ams->GetAmsId());
        }
    }
    return ams_ids;
}

std::map<std::pair<int, int>, int> collect_ams_counts(MachineObject *obj)
{
    std::map<std::pair<int, int>, int> ams_cnt_map;
    for (const auto &[key, ids] : collect_ams_ids(obj))
        ams_cnt_map[key] = static_cast<int>(ids.size());
    return ams_cnt_map;
}

namespace {

/**
 * \brief Check if two slots hold the same nozzle, or are both empty
 *
 * Health is left out: it does not change how a slice uses the nozzle.
 */
bool same_nozzle(const NozzleSlot &lhs, const NozzleSlot &rhs)
{
    if (lhs.empty() || rhs.empty())
        return lhs.empty() == rhs.empty();
    return lhs.diameter == rhs.diameter && lhs.volume_type == rhs.volume_type;
}

/**
 * \brief Check if the local nozzle slots of one extruder equal what the machine reports
 *
 * \param obj          connected machine
 * \param extruder_id  0-based logical extruder
 * \param local        slots the next slice assumes for that extruder
 * \param slot_count   slot count of that extruder in the selected printer preset
 */
bool slots_match_machine(MachineObject *obj, int extruder_id, const ExtruderSlots &local, int slot_count)
{
    const ExtruderSlots machine = nozzle_slots_of(collect_nozzle_configs_from_machine(obj, extruder_id, slot_count));
    return std::equal(local.begin(), local.end(), machine.begin(), machine.end(), same_nozzle);
}

} // namespace

void show_filament_switcher_tip(bool ready)
{
    wxString msg = ready ? _L("Filament switcher detected. All AMS filaments are now available for both extruders. "
                              "The slicer will auto-assign for optimal printing. ") :
                           _L("A filament switcher is detected but not calibrated and thus currently unavailable. "
                              "Please calibrate it on the printer and synchronize before use. ");

    long style = ready ? (wxICON_INFORMATION | wxOK) : (wxICON_WARNING | wxOK);
    MessageDialog dlg(static_cast<wxWindow *>(wxGetApp().mainframe), msg, _L("Tips"), style, "", _L("Learn more"),
                      [](const wxString &url) { wxLaunchDefaultBrowser("https://e.bambulab.com/t?c=yElVKjHwyw9o3pND"); });
    dlg.CenterOnParent();
    dlg.ShowModal();
}

// Compare whole slots, not the groups of the slicing diameter: on a rack one extruder can hold other
// diameters only, and a group compare then never matches, even right after a sync.
bool is_extruder_stat_synced(int target_extruder_id)
{
    Slic3r::DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return false;

    MachineObject* obj = dev->get_selected_machine();
    if (!obj || !obj->GetNozzleSystem())
        return false;

    PresetBundle* preset_bundle = wxGetApp().preset_bundle;
    if (!preset_bundle)
        return false;

    // Nozzle slots do not encode the printer model. An A1 0.8 preset can still match a leftover 0.8
    // on an H3C rack, so require the selected preset's type to match the connected machine first.
    Preset &printer = preset_bundle->printers.get_edited_preset();
    if (printer.get_printer_type(preset_bundle) != obj->get_show_printer_type())
        return false;

    const auto *max_nozzle_counts = printer.config.option<ConfigOptionIntsNullable>("extruder_max_nozzle_count");
    if (!max_nozzle_counts)
        return false;

    const NozzleInventory &nozzles        = preset_bundle->nozzle_inventory;
    const int              extruder_count = nozzles.extruder_count();
    const int              first_extruder = target_extruder_id < 0 ? 0 : target_extruder_id;
    const int              last_extruder  = target_extruder_id < 0 ? extruder_count : target_extruder_id + 1;
    if (first_extruder < 0 || last_extruder > extruder_count ||
        last_extruder > static_cast<int>(max_nozzle_counts->values.size()))
        return false;

    for (int extruder_id = first_extruder; extruder_id < last_extruder; ++extruder_id)
        if (!slots_match_machine(obj, extruder_id, nozzles.slots(extruder_id), max_nozzle_counts->values[extruder_id]))
            return false;
    return true;
}

namespace {

// Put a nozzle in the slot the printer reported for it. The id comes off the wire (addr id, else
// nozzle id), so it can name a slot the preset's `extruder_max_nozzle_count` does not have; writing
// it unchecked would run past the end of `nozzles`. An id we cannot place is dropped, leaving the
// slot empty -- the conservative reading, since an empty slot shows as unknown rather than as a
// nozzle sitting in the wrong place.
void place_nozzle(std::vector<const DevNozzle *> &nozzles, int slot, const DevNozzle *nozzle)
{
    if (slot < 0 || slot >= (int) nozzles.size()) {
        BOOST_LOG_TRIVIAL(warning) << "nozzle slot " << slot << " outside the " << nozzles.size() << " slot(s) this extruder has, ignored";
        return;
    }
    nozzles[slot] = nozzle;
}

// The machine nozzles of one logic extruder in slot order: rack nozzles by addr id, then the nozzle on
// the extruder on top of its slot. A slot the machine reports nothing for stays nullptr.
std::vector<const DevNozzle *> placed_nozzles(const DevNozzleSystem &nozzle_system, int extruder_idx, int slot_count)
{
    // remove duplicated nozzle, total number of nozzle is no more than `slot_count`
    std::vector<const DevNozzle *> nozzles(slot_count, nullptr);

    // addr_id is more robust
    auto id_from_rack = [](const DevNozzle &nozzle) {
        auto addr_id = nozzle.GetAddrId();
        if (addr_id) return addr_id.value();
        return nozzle.GetNozzleId();
    };

    // where the slot should the nozzle on the extuder be in
    auto id_from_ext = [&nozzles](const DevNozzle &nozzle) -> int {
        auto addr_id = nozzle.GetAddrId();
        if (addr_id) return addr_id.value();

        // The first slot holding no actual nozzle. A rack slot the machine reports as vacant still
        // gets a placeholder DevNozzle with `IsExist() == false`, so testing the pointer alone finds
        // no free slot once the rack filled every entry, and the nozzle would fall through to the
        // last resort below and evict a nozzle that is really there.
        for (size_t i = 0; i < nozzles.size(); ++i) {
            if (!nozzles[i] || !nozzles[i]->IsExist()) return i;
        }
        return 0;
    };

    //
    // check nozzles on the rack
    for (const auto &[_, nozzle] : nozzle_system.GetRackNozzles()) {
        if (nozzle.GetLogicExtruderId() != extruder_idx) continue;
        place_nozzle(nozzles, id_from_rack(nozzle), &nozzle);
    }

    // check nozzles on the extruder, override the one on the rack
    for (const auto &[_, nozzle] : nozzle_system.GetExtNozzles()) {
        if (nozzle.GetLogicExtruderId() != extruder_idx) continue;
        place_nozzle(nozzles, id_from_ext(nozzle), &nozzle);
    }

    return nozzles;
}

/// What the device page shows for \p nozzle: its tool under a healthy state, a question mark under
/// the unknown state, and nothing under the other states.
MachineSlot machine_slot(MachineObject *obj, const DevNozzle &nozzle)
{
    namespace SV = AmsHotendSchema::values;
    const std::string state = DevicePageFilamentSlotStateResolver::ResolveNozzleState(obj, nozzle, {});
    if (state == SV::nozzle_state::unknown) return MachineSlot::Unknown;
    if (state != SV::nozzle_state::normal && state != SV::nozzle_state::active && state != SV::nozzle_state::feed_suspect)
        return MachineSlot::Blank;

    switch (nozzle.GetToolKind()) {
    case DevNozzle::NozzleToolKind::Hotend: return MachineSlot::Hotend;
    case DevNozzle::NozzleToolKind::Cutter: return MachineSlot::Cutter;
    case DevNozzle::NozzleToolKind::Pen: return MachineSlot::Pen;
    default: return MachineSlot::Unknown;
    }
}

} // namespace

// The nozzles the machine reports for one logical extruder, as slot-ordered configs. Rack nozzles
// are placed by addr id, then overridden by whatever sits on the extruder itself. Slots the machine
// cannot describe (absent, unknown, error, not a hotend) come back with an empty diameter, and the
// result always holds `extruder_nozzle_count` entries.
std::vector<NozzleConfig> collect_nozzle_configs_from_machine(MachineObject *obj, int extruder_idx, int extruder_nozzle_count)
{
    if (!obj || !obj->GetNozzleSystem()) { return {}; }
    if (extruder_nozzle_count <= 0) { return {}; }
    auto *nozzle_system = obj->GetNozzleSystem();

    const std::vector<const DevNozzle *> nozzles = placed_nozzles(*nozzle_system, extruder_idx, extruder_nozzle_count);

    auto has_rack = nozzle_system->GetNozzleRack()->IsSupported();

    // only check nozzle is normal and unknown when nozzle rack is supported
    auto is_normal  = [has_rack](const DevNozzle *nozzle) { return !has_rack || nozzle->IsNormal(); };
    auto is_unknown = [has_rack](const DevNozzle *nozzle) { return has_rack && nozzle->IsUnknown(); };

    std::vector<NozzleConfig> nozzle_configs;
    nozzle_configs.reserve(extruder_nozzle_count);
    for (const auto *nozzle : nozzles) {
        auto &nozzle_config = nozzle_configs.emplace_back();
        // A slot the machine reported nothing for keeps the default config: empty diameter and
        // nsUnknown status, i.e. "the machine cannot describe this slot".
        if (!nozzle) continue;

        if (extruder_nozzle_count <= 1) {
            nozzle_config.status = NozzleStatus::nsNormal;
        } else {
            if (is_unknown(nozzle)) {
                nozzle_config.status = NozzleStatus::nsUnknown;
            } else if (!is_normal(nozzle)) {
                nozzle_config.status = NozzleStatus::nsError;
            } else {
                nozzle_config.status = NozzleStatus::nsNormal;
            }
        }

        // A nozzle may be fed by more than one filament: the current feed slot(s) plus a distinct preload slot.
        nozzle_config.ams_slots = nozzle->GetBindAmsSlots();
        const auto &main_slots  = nozzle_config.ams_slots;
        for (const auto &preload : nozzle->GetPreloadBindAmsSlots()) {
            const bool same_as_main = std::any_of(main_slots.begin(), main_slots.end(), [&preload](const DevAmsSlotId &s) { return s == preload; });
            if (!same_as_main) nozzle_config.ams_slots.push_back(preload);
        }

        // External AMS units bound to this nozzle — anchors the AMS attach block in the dialog.
        nozzle_config.has_bind_ams = !nozzle->GetBindAms().empty();

        if (!nozzle->IsExist() || is_unknown(nozzle) || !is_normal(nozzle) || !nozzle->IsHotend()) continue;

        nozzle_config.diameter = get_diameter_string(nozzle->GetNozzleDiameter());
        nozzle_config.volume   = DevNozzle::ToNozzleVolumeType(nozzle->GetNozzleFlowType());
    }

    return nozzle_configs;
}

ExtruderSlots nozzle_slots_of(const std::vector<NozzleConfig> &configs)
{
    ExtruderSlots slots;
    for (const NozzleConfig &cfg : configs) {
        if (cfg.is_empty()) {
            slots.push_back(NozzleSlot());
            continue;
        }
        const NozzleSlotStatus status = cfg.status == NozzleStatus::nsError   ? NozzleSlotStatus::Error :
                                        cfg.status == NozzleStatus::nsUnknown ? NozzleSlotStatus::Unknown :
                                                                                NozzleSlotStatus::Normal;
        slots.push_back({into_u8(cfg.diameter), cfg.volume, status});
    }
    return slots;
}

std::vector<MachineSlot> machine_slots(const MachineObject *obj, int extruder)
{
    if (!obj || !obj->GetNozzleSystem()) return {};

    PresetBundle *preset_bundle = wxGetApp().preset_bundle;
    if (!preset_bundle) return {};
    const auto *max_nozzle_count = preset_bundle->printers.get_edited_preset().config.option<ConfigOptionIntsNullable>("extruder_max_nozzle_count");
    if (!max_nozzle_count || extruder < 0 || extruder >= (int) max_nozzle_count->values.size()) return {};
    const int slot_count = max_nozzle_count->values[extruder];
    if (slot_count <= 0) return {};

    // The resolver takes a mutable printer but never touches it.
    MachineObject *resolver_obj = const_cast<MachineObject *>(obj);

    std::vector<MachineSlot> slots;
    slots.reserve(slot_count);
    for (const DevNozzle *nozzle : placed_nozzles(*obj->GetNozzleSystem(), extruder, slot_count))
        slots.push_back(nozzle ? machine_slot(resolver_obj, *nozzle) : MachineSlot::Blank);
    return slots;
}

SlotFace slot_face(const NozzleConfig &project, MachineSlot machine)
{
    if (!project.is_empty()) return SlotFace::Hotend;
    switch (machine) {
    case MachineSlot::Cutter: return SlotFace::Cutter;
    case MachineSlot::Pen: return SlotFace::Pen;
    case MachineSlot::Unknown: return SlotFace::Unknown;
    default: return SlotFace::Empty;
    }
}

// What the printer reports right now, for callers deciding "is this nozzle layout synced".
MachineNozzleBaseline collect_machine_nozzle_baseline(bool is_dual)
{
    MachineNozzleBaseline baseline;

    DeviceManager *dev = wxGetApp().getDeviceManager();
    MachineObject *obj = dev ? dev->get_selected_machine() : nullptr;
    if (!obj || !obj->is_info_ready()) return baseline;

    PresetBundle *preset_bundle = wxGetApp().preset_bundle;
    if (!preset_bundle) return baseline;

    // Same guard is_extruder_stat_synced() applies: a slot on a printer of another model says
    // nothing about the selected preset, so reporting it as a baseline would let callers mark
    // slots synced against hardware the preset has no relation to.
    if (preset_bundle->printers.get_edited_preset().get_printer_type(preset_bundle) != obj->get_show_printer_type()) return baseline;

    const auto *max_nozzle_count = preset_bundle->printers.get_edited_preset().config.option<ConfigOptionIntsNullable>("extruder_max_nozzle_count");
    if (!max_nozzle_count) return baseline;

    const auto &counts = max_nozzle_count->values;
    if (is_dual) {
        if (counts.size() < 2) return baseline;
        baseline.configs[0] = collect_nozzle_configs_from_machine(obj, LOGIC_L_EXTRUDER_ID, counts[0]);
        baseline.configs[1] = collect_nozzle_configs_from_machine(obj, LOGIC_R_EXTRUDER_ID, counts[1]);
    } else {
        if (counts.empty()) return baseline;
        baseline.configs[0] = collect_nozzle_configs_from_machine(obj, LOGIC_UNIQUE_EXTRUDER_ID, counts[0]);
    }

    baseline.synced = true;
    return baseline;
}

std::string serialize_nozzle_config(const std::map<int, std::vector<DevNozzle>>& nozzle_cfg_map)
{
    std::ostringstream oss;

    std::vector<DevNozzle> deputy_nozzles;
    auto deputy_it = nozzle_cfg_map.find(1);
    if (deputy_it != nozzle_cfg_map.end()) {
        deputy_nozzles = deputy_it->second;
    }

    std::vector<DevNozzle> main_nozzles;
    auto main_it = nozzle_cfg_map.find(0);
    if (main_it != nozzle_cfg_map.end()) {
        main_nozzles = main_it->second;
    }

    for (size_t i = 0; i < deputy_nozzles.size(); ++i) {
        if (i > 0) oss << ";";
        oss << std::fixed << std::setprecision(1) << deputy_nozzles[i].GetNozzleDiameter() << ","
            << static_cast<int>(deputy_nozzles[i].GetNozzleFlowType());
    }

    oss << "|";

    for (size_t i = 0; i < main_nozzles.size(); ++i) {
        if (i > 0) oss << ";";
        oss << std::fixed << std::setprecision(1) << main_nozzles[i].GetNozzleDiameter() << ","
            << static_cast<int>(main_nozzles[i].GetNozzleFlowType());
    }

    return oss.str();
}

std::map<int, std::vector<DevNozzle>> deserialize_nozzle_config(const std::string &config_str)
{
    std::map<int, std::vector<DevNozzle>> nozzle_cfg_map;
    if (config_str.empty()) return nozzle_cfg_map;

    std::vector<std::string> extruder_parts;
    boost::split(extruder_parts, config_str, boost::is_any_of("|"));

    auto get_nozzles_from_string = [](const std::string& part_str) -> std::vector<DevNozzle> {
        std::vector<DevNozzle> nozzles;
        std::vector<std::string> parts;
        boost::split(parts, part_str, boost::is_any_of(";"));
        for (const auto &part : parts) {
            std::vector<std::string> values;
            boost::split(values, part, boost::is_any_of(","));
            if (values.size() == 2) {
                DevNozzle nozzle;
                nozzle.m_diameter = string_to_double_decimal_point(values[0]);
                nozzle.m_nozzle_flow = static_cast<NozzleFlowType>(std::stoi(values[1]));
                nozzles.push_back(nozzle);
            }
        }
        return nozzles;
    };

    if (extruder_parts.size() != 2) {
        auto nozzles = get_nozzles_from_string(config_str);
        nozzle_cfg_map[MAIN_EXTRUDER_ID] = nozzles;
        nozzle_cfg_map[DEPUTY_EXTRUDER_ID] = { DevNozzle() };
        return nozzle_cfg_map;
    }

    if (!extruder_parts[0].empty()) {
        auto nozzles = get_nozzles_from_string(extruder_parts[0]);
        nozzle_cfg_map[DEPUTY_EXTRUDER_ID] = nozzles;
    }
    if (!extruder_parts[1].empty()) {
        auto nozzles = get_nozzles_from_string(extruder_parts[1]);
        nozzle_cfg_map[MAIN_EXTRUDER_ID] = nozzles;
    }

    return nozzle_cfg_map;
}

bool is_same_nozzle_config(const std::map<int, std::vector<DevNozzle>> &config1, const std::map<int, std::vector<DevNozzle>> &config2)
{
    if (config1.size() != config2.size()) return false;

    for (const auto& [eid, nozzles1] : config1) {
        auto it = config2.find(eid);
        if (it == config2.end()) return false;

        const auto &nozzles2 = it->second;
        if (nozzles1.size() != nozzles2.size()) return false;

        auto sorted_nozzles1 = nozzles1;
        auto sorted_nozzles2 = nozzles2;

        auto compare_nozzle = [](const DevNozzle &a, const DevNozzle &b) {
            float dia_a = a.GetNozzleDiameter();
            float dia_b = b.GetNozzleDiameter();
            if (std::abs(dia_a - dia_b) > EPSILON) {
                return dia_a < dia_b;
            }
            return static_cast<int>(a.GetNozzleFlowType()) < static_cast<int>(b.GetNozzleFlowType());
        };

        std::sort(sorted_nozzles1.begin(), sorted_nozzles1.end(), compare_nozzle);
        std::sort(sorted_nozzles2.begin(), sorted_nozzles2.end(), compare_nozzle);

        for (size_t i = 0; i < sorted_nozzles1.size(); ++i) {
            if (std::abs(sorted_nozzles1[i].GetNozzleDiameter() - sorted_nozzles2[i].GetNozzleDiameter()) > EPSILON ||
                sorted_nozzles1[i].GetNozzleFlowType() != sorted_nozzles2[i].GetNozzleFlowType()) {
                return false;
            }
        }
    }

    return true;
}

std::string serialize_nozzle_option(const NozzleOption& option) {
    std::ostringstream oss;
    oss << option.diameter << "|";

    bool first = true;
    for (const auto& pair : option.extruder_nozzle_stats) {
        if (!first) oss << ";";
        first = false;
        oss << pair.first << ":";

        bool first_stat = true;
        for (const auto& stat_pair : pair.second) {
            if (!first_stat) oss << ",";
            first_stat = false;
            oss << static_cast<int>(stat_pair.first) << "#" << stat_pair.second;
        }
    }
    return oss.str();
}

std::optional<NozzleOption> deserialize_nozzle_option(const std::string& option_str) {
    if (option_str.empty()) return std::nullopt;

    std::vector<std::string> parts;
    boost::split(parts, option_str, boost::is_any_of("|"));
    if (parts.size() != 2) return std::nullopt;

    NozzleOption option;
    option.diameter = parts[0];

    std::vector<std::string> extruder_parts;
    boost::split(extruder_parts, parts[1], boost::is_any_of(";"));

    for (const auto& extruder_part : extruder_parts) {
        if (extruder_part.empty()) continue;

        std::vector<std::string> extruder_data;
        boost::split(extruder_data, extruder_part, boost::is_any_of(":"));
        if (extruder_data.size() != 2) continue;

        int extruder_id = std::stoi(extruder_data[0]);
        std::unordered_map<NozzleVolumeType, int> stats;

        std::vector<std::string> stat_parts;
        boost::split(stat_parts, extruder_data[1], boost::is_any_of(","));

        for (const auto& stat_part : stat_parts) {
            std::vector<std::string> kv;
            boost::split(kv, stat_part, boost::is_any_of("#"));
            if (kv.size() == 2) {
                NozzleVolumeType type = static_cast<NozzleVolumeType>(std::stoi(kv[0]));
                int count = std::stoi(kv[1]);
                stats[type] = count;
            }
        }

        option.extruder_nozzle_stats[extruder_id] = stats;
    }

    return option;
}

namespace {
// Priority rank of a diameter among several usable ones, 0 first: 0.4, 0.2, 0.6, 0.8, then any other.
int diameter_priority_rank(double diameter)
{
    const double preferred[] = {0.4, 0.2, 0.6, 0.8};
    for (int rank = 0; rank < (int) std::size(preferred); ++rank)
        if (std::abs(diameter - preferred[rank]) < EPSILON) return rank;
    return (int) std::size(preferred);
}
} // namespace

float get_best_diameter(MachineObject *obj, int extruder_index)
{
    int                  physical_idx = DevExtder::to_physical_extruder_id(obj->GetExtderSystem()->GetTotalExtderCount(), extruder_index);
    std::map<float, int> diameter_cnt;
    auto                 nozzle_system = obj->GetNozzleSystem();
    for (const auto &nozzles : {nozzle_system->GetRackNozzles(), nozzle_system->GetExtNozzles()}) {
        for (const auto &[_, nozzle] : nozzles) {
            if (nozzle.IsEmpty() || nozzle.IsUnknown() || nozzle.IsAbnormal() || !nozzle.IsHotend()) continue;
            if (nozzle.GetExtruderId() != physical_idx) continue;
            diameter_cnt[nozzle.GetNozzleDiameter()] += 1;
        }
    }

    // the most available diameter
    if (diameter_cnt.empty()) return 0.4;

    float best_diameter = 0.0f;
    int   best_count    = -1;
    for (const auto &[diameter, count] : diameter_cnt) {
        if (count > best_count || (count == best_count && diameter_priority_rank(diameter) < diameter_priority_rank(best_diameter))) {
            best_count    = count;
            best_diameter = diameter;
        }
    }

    return best_diameter;
}

wxString usable_nozzle_diameter(const wxString &wanted, const std::vector<NozzleConfig> &nozzles)
{
    // Among diameters outside the priority list, the one nearest 0.4 wins.
    const auto rank = [](double diameter) { return std::make_pair(diameter_priority_rank(diameter), std::abs(diameter - 0.4)); };

    std::optional<wxString> best;
    double                  best_value = 0.;
    for (const NozzleConfig &nozzle : nozzles) {
        if (nozzle.is_empty()) continue;
        if (nozzle.diameter == wanted) return wanted;
        double value = 0.;
        if (!nozzle.diameter.ToCDouble(&value)) continue;
        if (!best || rank(value) < rank(best_value)) {
            best       = nozzle.diameter;
            best_value = value;
        }
    }
    if (!best) {
        BOOST_LOG_TRIVIAL(warning) << "no nozzle is configured, slicing diameter stays at " << wanted;
        return wanted;
    }

    BOOST_LOG_TRIVIAL(warning) << "no " << wanted << "mm nozzle is left, slicing diameter falls back to " << *best;
    return *best;
}

bool machine_has_nozzle_diameter(MachineObject *obj, int extruder_index, float diameter)
{
    if (!obj || !obj->GetNozzleSystem() || diameter <= 0.f) return false;

    const int physical_idx  = DevExtder::to_physical_extruder_id(obj->GetExtderSystem()->GetTotalExtderCount(), extruder_index);
    auto     *nozzle_system = obj->GetNozzleSystem();
    for (const auto &nozzles : {nozzle_system->GetRackNozzles(), nozzle_system->GetExtNozzles()}) {
        for (const auto &[_, nozzle] : nozzles) {
            if (nozzle.IsEmpty() || nozzle.IsUnknown() || nozzle.IsAbnormal() || !nozzle.IsHotend()) continue;
            if (nozzle.GetExtruderId() != physical_idx) continue;
            if (std::abs(nozzle.GetNozzleDiameter() - diameter) < EPSILON) return true;
        }
    }
    return false;
}

}} // namespace Slic3r::GUI
