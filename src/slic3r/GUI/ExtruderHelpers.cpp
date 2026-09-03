#include "ExtruderHelpers.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

#include <boost/algorithm/string.hpp>
#include <boost/log/trivial.hpp>

#include "DeviceCore/DevNozzleSystem.h"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "ExtruderPanel.hpp" // NozzleStatus enumerators
#include "libslic3r/LocalesUtils.hpp"
#include "libslic3r/MultiNozzleUtils.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"

namespace Slic3r { namespace GUI {

std::string get_diameter_string(float diameter)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(1) << diameter;
    return stream.str();
}

// Count bound AMS units per (logical_extruder_id, slot_kind) for the given machine.
// Keys use the *logical* extruder id
std::map<std::pair<int, int>, int> collect_ams_counts(MachineObject *obj)
{
    const int  extruder_nums     = obj->GetExtderSystem()->GetTotalExtderCount();
    const bool fila_switch_ready = wxGetApp().sidebar().is_fila_switch_ready();

    std::map<std::pair<int, int>, int> ams_cnt_map{
        {{0, 1}, 0},
        {{0, 4}, 0},
        {{1, 1}, 0},
        {{1, 4}, 0},
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
            ams_cnt_map[key]++;
        }
    }
    return ams_cnt_map;
}

// check whether nozzle staus is synced with printer, extruder = -1 means check both extruder
bool is_extruder_stat_synced(int target_extruder_id)
{
    using namespace MultiNozzleUtils;
    Slic3r::DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return false;

    MachineObject* obj = dev->get_selected_machine();
    if (!obj)
        return false;

    PresetBundle* preset_bundle = wxGetApp().preset_bundle;
    bool is_synced = true;

    auto nozzle_volume_values = preset_bundle->project_config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type")->values;
    auto nozzle_diameter_values = preset_bundle->printers.get_edited_preset().config.option<ConfigOptionFloatsNullable>("nozzle_diameter")->values;
    auto extruder_nozzle_stats = preset_bundle->extruder_nozzle_stat.get_raw_stat();


    std::vector<std::vector<NozzleGroupInfo>> preset_nozzle_infos(nozzle_diameter_values.size());

    for (size_t extruder_id = 0; extruder_id < nozzle_diameter_values.size(); ++extruder_id) {
        NozzleVolumeType preset_volume_type = NozzleVolumeType(nozzle_volume_values[extruder_id]);
        std::string      preset_diameter = format_diameter_to_str(nozzle_diameter_values[extruder_id]);

        if (preset_volume_type == nvtHybrid) {
            if (extruder_id < extruder_nozzle_stats.size()) {
                for (auto& elem : extruder_nozzle_stats[extruder_id]) {
                    NozzleVolumeType type = elem.first;
                    int              count = elem.second;
                    NozzleGroupInfo  preset_info(preset_diameter, type, extruder_id, count);
                    preset_nozzle_infos[extruder_id].emplace_back(preset_info);
                }
            }
        }
        else {
            NozzleGroupInfo preset_info(preset_diameter, preset_volume_type, extruder_id, preset_bundle->extruder_nozzle_stat.get_extruder_nozzle_count(extruder_id, preset_volume_type));
            preset_nozzle_infos[extruder_id].emplace_back(preset_info);
        }
    }

    auto printer_groups = obj->GetNozzleSystem()->GetNozzleGroups();
    std::vector<std::vector<NozzleGroupInfo>> target_nozzle_groups;
    if (target_extruder_id == -1)
        target_nozzle_groups = preset_nozzle_infos;
    else
        target_nozzle_groups = { preset_nozzle_infos[target_extruder_id] };

    for (auto preset_groups : target_nozzle_groups) {
        for (auto& preset_group : preset_groups) {
            if (preset_group.nozzle_count == 0) {
                if (std::find_if(printer_groups.begin(), printer_groups.end(), [&preset_group](const NozzleGroupInfo& elem) { return preset_group.is_same_type(elem); }) !=
                    printer_groups.end()) {
                    is_synced = false;
                    break;
                }
            }
            else if (std::find(printer_groups.begin(), printer_groups.end(), preset_group) == printer_groups.end()) {
                is_synced = false;
                break;
            }
        }
    }
    return is_synced;
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
        BOOST_LOG_TRIVIAL(warning) << "collect_nozzle_configs_from_machine: nozzle slot " << slot << " outside the " << nozzles.size() << " slot(s) this extruder has, ignored";
        return;
    }
    nozzles[slot] = nozzle;
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

    // remove duplicated nozzle, total number of nozzle is no more than `extruder_nozzle_count`
    std::vector<const DevNozzle *> nozzles;
    nozzles.resize(extruder_nozzle_count);

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

        // the first empty slot
        for (size_t i = 0; i < nozzles.size(); ++i) {
            if (!nozzles[i]) return i;
        }
        return 0;
    };

    //
    // check nozzles on the rack
    for (const auto &[_, nozzle] : nozzle_system->GetRackNozzles()) {
        if (nozzle.GetLogicExtruderId() != extruder_idx) continue;
        place_nozzle(nozzles, id_from_rack(nozzle), &nozzle);
    }

    // check nozzles on the extruder, override the one on the rack
    for (const auto &[_, nozzle] : nozzle_system->GetExtNozzles()) {
        if (nozzle.GetLogicExtruderId() != extruder_idx) continue;
        place_nozzle(nozzles, id_from_ext(nozzle), &nozzle);
    }

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

// What the printer reports right now, for callers deciding "is this nozzle layout synced".
MachineNozzleBaseline collect_machine_nozzle_baseline(bool is_dual)
{
    MachineNozzleBaseline baseline;

    DeviceManager *dev = wxGetApp().getDeviceManager();
    MachineObject *obj = dev ? dev->get_selected_machine() : nullptr;
    if (!obj || !obj->is_info_ready()) return baseline;

    PresetBundle *preset_bundle = wxGetApp().preset_bundle;
    if (!preset_bundle) return baseline;

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

float get_best_diameter(MachineObject *obj, int extruder_index)
{
    int                  physical_idx = DevExtder::to_physical_extruder_id(obj->GetExtderSystem()->GetTotalExtderCount(), extruder_index);
    std::map<float, int> diameter_cnt;
    auto                 nozzle_system = obj->GetNozzleSystem();
    for (const auto &nozzles : {nozzle_system->GetRackNozzles(), nozzle_system->GetExtNozzles()}) {
        for (const auto &[_, nozzle] : nozzles) {
            if (nozzle.IsEmpty() || nozzle.IsAbnormal() || !nozzle.IsHotend()) continue;
            if (nozzle.GetExtruderId() != physical_idx) continue;
            diameter_cnt[nozzle.GetNozzleDiameter()] += 1;
        }
    }

    // the most available diameter
    if (diameter_cnt.empty()) return 0.4;

    // when counts tie, prefer diameter in this order: 0.4 > 0.2 > 0.6 > 0.8 > others
    auto priority = [](float d) -> int {
        if (std::abs(d - 0.4f) < EPSILON) return 0;
        if (std::abs(d - 0.2f) < EPSILON) return 1;
        if (std::abs(d - 0.6f) < EPSILON) return 2;
        if (std::abs(d - 0.8f) < EPSILON) return 3;
        return 4;
    };

    float best_diameter = 0.0f;
    int   best_count    = -1;
    for (const auto &[diameter, count] : diameter_cnt) {
        if (count > best_count || (count == best_count && priority(diameter) < priority(best_diameter))) {
            best_count    = count;
            best_diameter = diameter;
        }
    }

    return best_diameter;
}

}} // namespace Slic3r::GUI
