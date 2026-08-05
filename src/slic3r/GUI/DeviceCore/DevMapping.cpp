#include <nlohmann/json.hpp>
#include "DevMapping.h"
#include "DevFilaSystem.h"
#include "DevUtil.h"
#include "DevNozzleSystem.h"
#include "DevNozzleRack.h"

// TODO: remove this include
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/GuiColor.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>

using namespace nlohmann;

namespace Slic3r
{
    static bool _is_mapping_slot_on_machine(const MachineObject* obj, const FilamentInfo& item, bool check_empty_slot)
    {
        auto fila_sys = obj->GetFilaSystem();
        if (!fila_sys) {
            return false;
        }

        if (item.ams_id.empty() || item.slot_id.empty()) {
            return false;
        }

		auto ams_tray = fila_sys->GetAmsTray(item.ams_id, item.slot_id);
		if (ams_tray) {
            return check_empty_slot ? ams_tray->is_exists : true;
		}

		auto vir_tray = fila_sys->GetVirtualTray(item.ams_id);
		if (vir_tray.has_value()) {
            return true;// vir_tray does't support is_exists
		}

        return false;
    }

    bool DevMappingUtil::is_valid_mapping_result(const MachineObject* obj, std::vector<FilamentInfo>& result, bool check_empty_slot)
    {
        if (!obj || result.empty()) {
            return false;
        }

        bool all_valid = true;
        for (FilamentInfo& item : result) {
            if (!_is_mapping_slot_on_machine(obj, item, check_empty_slot)) {
                item.tray_id = -1;
                all_valid    = false;
            }
        }

        return all_valid;
    }

    // calc distance map
    struct DisValue {
        int  tray_id;
        float distance;
        bool  is_same_color = true;
        bool  is_type_match = true;
        bool  is_nozzle_match = true;
        bool  is_ext = false; // candidate tray is an external spool (hotend-rack)
    };

    static std::optional<DevNozzle> _query_slot_nozzle(const MachineObject* obj, const DevAmsTray* tray)
    {
        if (!obj || !tray)
            return std::nullopt;

        // const_cast: get_nozzle_pos_id() needs a non-const obj but is read-only here.
        MachineObject* mobj = const_cast<MachineObject*>(obj);
        auto pos_id = tray->get_nozzle_pos_id(mobj);
        if (!pos_id.has_value() || pos_id.value() < 0x10)
            return std::nullopt;

        return mobj->GetNozzleSystem()->GetNozzleOptByAddrId(pos_id.value() - 0x10);
    }

    static void _parse_tray_info(int ams_id, int slot_id, DevAmsType type, DevAmsTray tray, FilamentInfo& result)
    {
        result.color = tray.color;
        result.type = tray.get_filament_type();
        result.filament_id = tray.setting_id;
        result.setting_id = tray.filament_setting_id.empty() ? tray.setting_id : tray.filament_setting_id;
        result.ctype = tray.ctype;
        result.colors = tray.cols;
        result.remain = tray.remain; // 0~100, -1 when the printer does not report a remain (e.g. non-genuine spools)

        /*for new ams mapping*/
        result.ams_id = std::to_string(ams_id);
        result.slot_id = std::to_string(slot_id);

        if (ams_id == VIRTUAL_TRAY_MAIN_ID || ams_id == VIRTUAL_TRAY_DEPUTY_ID
            || type == DevAmsType::EXT_SPOOL)
        {
            // External spools (including the hotend-rack virtual trays that are
            // not the legacy MAIN/DEPUTY ids) use the tray id directly.
            result.tray_id = atoi(tray.id.c_str());
            result.id = atoi(tray.id.c_str());
        }
        else
        {
            if (type == DevAmsType::N3S) {
                result.id = ams_id + slot_id;
            } else if (type == DevAmsType::AMS_LITE_MIXED) {
                result.id = AMS_LITE_MIXED_TRAY_INDEX_OFFSET + slot_id;
            } else if (type == DevAmsType::ZMULTI_A) {
                result.id = 28 + slot_id;
            } else if (type == DevAmsType::ZMULTI_B) {
                result.id = 37 + slot_id;
            } else {
                result.id = ams_id * 4 + slot_id;
            }
        }
    }

    // Tie-break helper for ams_filament_mapping(): returns true when 'candidate' should
    // replace 'picked' on an exact distance tie because it has less filament remaining,
    // so partially-used spools are consumed first.
    // Rules:
    //  - only when the candidate tray is an exact filament_id match for the sliced filament;
    //  - only when both trays report a valid remain (>= 0, e.g. genuine/RFID spools);
    //    an unknown remain (-1) keeps the legacy lowest-slot-first behavior;
    //  - a tray reporting remain == 0 is never deliberately preferred: a non-empty
    //    candidate wins over an empty picked tray, never the other way around.
    static bool _prefer_tray_by_remain(const FilamentInfo& filament, const FilamentInfo& candidate, const FilamentInfo& picked)
    {
        if (filament.filament_id != candidate.filament_id)
            return false;
        if (candidate.remain <= 0)
            return false;
        if (picked.remain <= 0)
            return true;
        return candidate.remain < picked.remain;
    }

    // Collect candidates for a hotend-rack machine (e.g. N1X, RackType_A/B):
    // for every rack nozzle position take the slot it is *currently feeding from*
    // via DevNozzle::GetBindAmsSlots(). That slot is the single coordinate
    // (ams_id, slot_id) the device reports as the active feed for the nozzle and
    // is naturally either an external spool or an internal AMS (ZMULTI / N3S /
    // N3F). Rack positions without a physical nozzle, and external spools with no
    // filament loaded in the nozzle, are excluded from auto-match candidates.
    // At most one candidate per nozzle
    // (so at most 7/9 total). ext_tray_keys records which candidate keys are
    // external spools so the matcher can prefer them on a same-color tie.
    static void _collect_mapping_hotend_item_candidates(const MachineObject*                     obj,
                                                        bool                                     check_nozzle_type,
                                                        std::map<int, FilamentInfo>&             tray_filaments,
                                                        std::map<int, std::optional<DevNozzle>>& tray_nozzles,
                                                        std::set<int>&                           ext_tray_keys)
    {
        auto  fila_sys   = obj->GetFilaSystem();
        auto* nozzle_sys = obj->GetNozzleSystem();
        auto  rack       = obj->GetNozzleRack();
        if (!fila_sys || !nozzle_sys || !rack)
            return;

        // Resolve a candidate coordinate into its tray, build the FilamentInfo and
        // record it. Readable gate (color + type) is required because the matcher
        // needs them to compute the color distance.
        auto try_add = [&](int ams_id, int slot_id, DevAmsType type, const DevAmsTray* tray,
                           const std::optional<DevNozzle>& nz, bool is_ext) {
            if (!nz.has_value() || !nz->IsExist())
                return;
            if (!tray || !tray->is_tray_info_ready())
                return;
            FilamentInfo info;
            _parse_tray_info(ams_id, slot_id, type, *tray, info);
            const int key = (type == DevAmsType::EXT_SPOOL) ? info.tray_id : info.id;
            if (!tray_filaments.emplace(key, info).second)
                return; // same slot reachable from multiple nozzles: keep the first
            if (check_nozzle_type)
                tray_nozzles.emplace(key, nz);
            if (is_ext)
                ext_tray_keys.insert(key);
        };

        for (int pos : rack->GetTotalRackNozzlePosSet())
        {
            // Resolve by stable addr_id first (the rack nozzle id becomes 0 once
            // it is picked up by an extruder); fall back to a pos-based lookup.
            auto nz = nozzle_sys->GetNozzleOptByAddrId(pos - 0x10);
            if (!nz.has_value())
                nz = nozzle_sys->GetNozzleOptByPosId(pos);
            if (!nz.has_value())
                continue;

            for (const auto& slot : nz->GetBindAmsSlots())
            {
                const int ams_id  = slot.first;
                const int slot_id = slot.second;
                if (auto vt = fila_sys->GetVirtualTray(ams_id)) // external spool
                {
                    try_add(ams_id, 0, DevAmsType::EXT_SPOOL, &(*vt), nz, /*is_ext*/ true);
                }
                else if (auto t = obj->get_tray(ams_id, slot_id)) // internal AMS (ZMULTI / N3S / N3F)
                {
                    DevAmsTray tray_copy = *t;
                    try_add(ams_id, slot_id, tray_copy.ams_type, &tray_copy, nz, /*is_ext*/ false);
                }
            }
        }
    }

    static bool _prefer_mapping_tie_break(bool cur_ext, bool new_ext, bool cur_id_match, bool new_id_match)
    {
        return (!cur_ext && new_ext) || (cur_ext == new_ext && !cur_id_match && new_id_match);
    }

    static void _assign_mapping_result_from_tray(FilamentInfo&                              dst,
                                                 const std::map<int, FilamentInfo>::iterator& tray,
                                                 float                                        distance)
    {
        dst.tray_id     = tray->first;
        dst.color       = tray->second.color;
        dst.type        = tray->second.type;
        dst.distance    = distance;
        dst.filament_id = tray->second.filament_id;
        dst.setting_id  = tray->second.setting_id;
        dst.ctype       = tray->second.ctype;
        dst.colors      = tray->second.colors;
        dst.ams_id      = tray->second.ams_id;
        dst.slot_id     = tray->second.slot_id;
    }

    static void _log_mapping_pick(int fila_idx, int tray_col_idx, float distance)
    {
        char buffer[256];
        ::sprintf(buffer, "ams_mapping, picked F(%02d) AMS(%02d), distance=%6.0f", fila_idx + 1, tray_col_idx + 1, distance);
        BOOST_LOG_TRIVIAL(info) << std::string(buffer);
    }

    // Hotend-rack (N1X): each slicing filament independently picks the closest-color
    // slot. Slots may be shared across filaments.
    static void _run_hotend_rack_color_mapping(const std::vector<FilamentInfo>&           filaments,
                                               std::map<int, FilamentInfo>&               tray_filaments,
                                               const std::vector<std::vector<DisValue>>&   distance_map,
                                               std::vector<FilamentInfo>&                 result)
    {
        for (int i = 0; i < (int) distance_map.size(); i++) {
            float min_val = INT_MAX;
            int   best_j  = -1;
            for (int j = 0; j < (int) distance_map[i].size(); j++) {
                const auto& dv = distance_map[i][j];
                if (!dv.is_type_match || !dv.is_nozzle_match)
                    continue;
                if (min_val > dv.distance) {
                    min_val = dv.distance;
                    best_j  = j;
                } else if (best_j >= 0 && min_val == dv.distance) {
                    const bool cur_ext = distance_map[i][best_j].is_ext;
                    const bool new_ext = dv.is_ext;
                    auto       cur_tray = tray_filaments.find(distance_map[i][best_j].tray_id);
                    auto       new_tray = tray_filaments.find(dv.tray_id);
                    const bool cur_id_match = cur_tray != tray_filaments.end()
                        && filaments[i].filament_id == cur_tray->second.filament_id;
                    const bool new_id_match = new_tray != tray_filaments.end()
                        && filaments[i].filament_id == new_tray->second.filament_id;
                    if (_prefer_mapping_tie_break(cur_ext, new_ext, cur_id_match, new_id_match)
                        || (cur_ext == new_ext
                            && cur_tray != tray_filaments.end() && new_tray != tray_filaments.end()
                            && _prefer_tray_by_remain(filaments[i], new_tray->second, cur_tray->second)))
                        best_j = j;
                }
            }
            if (best_j < 0)
                continue;

            auto tray = tray_filaments.find(distance_map[i][best_j].tray_id);
            if (tray == tray_filaments.end())
                continue;

            _assign_mapping_result_from_tray(result[i], tray, min_val);
            _log_mapping_pick(i, best_j, min_val);
        }
    }

    // Legacy AMS: greedy bipartite matching; a tray is reserved per round unless an
    // exact color match allows reuse.
    static void _run_legacy_greedy_ams_mapping(const std::vector<FilamentInfo>&           filaments,
                                               std::map<int, FilamentInfo>&               tray_filaments,
                                               const std::vector<std::vector<DisValue>>&   distance_map,
                                               std::vector<FilamentInfo>&                 result)
    {
        std::set<int> picked_src;
        std::set<int> picked_tar;
        for (int k = 0; k < (int) distance_map.size(); k++) {
            float min_val        = INT_MAX;
            int   picked_src_idx = -1;
            int   picked_tar_idx = -1;
            for (int i = 0; i < (int) distance_map.size(); i++) {
                if (picked_src.find(i) != picked_src.end())
                    continue;

                // try to mapping to different tray
                for (int j = 0; j < (int) distance_map[i].size(); j++) {
                    if (picked_tar.find(j) != picked_tar.end()) {
                        if (distance_map[i][j].is_same_color
                            && distance_map[i][j].is_type_match
                            && distance_map[i][j].is_nozzle_match
                            && distance_map[i][j].distance < (float) 0.0001) {
                            min_val        = distance_map[i][j].distance;
                            picked_src_idx = i;
                            picked_tar_idx = j;
                            tray_filaments[picked_tar_idx].distance = min_val;
                        }
                        continue;
                    }

                    if (distance_map[i][j].is_same_color
                        && distance_map[i][j].is_type_match
                        && distance_map[i][j].is_nozzle_match) {
                        if (min_val > distance_map[i][j].distance) {
                            min_val        = distance_map[i][j].distance;
                            picked_src_idx = i;
                            picked_tar_idx = j;
                            tray_filaments[picked_tar_idx].distance = min_val;
                        } else if (min_val == distance_map[i][j].distance) {
                            const bool cur_ext      = distance_map[picked_src_idx][picked_tar_idx].is_ext;
                            const bool new_ext      = distance_map[i][j].is_ext;
                            const bool cur_id_match = filaments[picked_src_idx].filament_id == tray_filaments[picked_tar_idx].filament_id;
                            const bool new_id_match = filaments[i].filament_id == tray_filaments[j].filament_id;
                            if (_prefer_mapping_tie_break(cur_ext, new_ext, cur_id_match, new_id_match)
                                || (cur_ext == new_ext
                                    && _prefer_tray_by_remain(filaments[i], tray_filaments[j], tray_filaments[picked_tar_idx]))) {
                                picked_src_idx = i;
                                picked_tar_idx = j;
                            }
                        }
                    }
                }

                // take a retry to mapping to used tray
                if (picked_src_idx < 0 || picked_tar_idx < 0) {
                    for (int j = 0; j < (int) distance_map[i].size(); j++) {
                        if (distance_map[i][j].is_same_color && distance_map[i][j].is_type_match && distance_map[i][j].is_nozzle_match) {
                            if (min_val > distance_map[i][j].distance) {
                                min_val        = distance_map[i][j].distance;
                                picked_src_idx = i;
                                picked_tar_idx = j;
                                tray_filaments[picked_tar_idx].distance = min_val;
                            } else if (min_val == distance_map[i][j].distance) {
                                const bool cur_ext      = distance_map[picked_src_idx][picked_tar_idx].is_ext;
                                const bool new_ext      = distance_map[i][j].is_ext;
                                const bool cur_id_match = filaments[picked_src_idx].filament_id == tray_filaments[picked_tar_idx].filament_id;
                                const bool new_id_match = filaments[i].filament_id == tray_filaments[j].filament_id;
                                if (_prefer_mapping_tie_break(cur_ext, new_ext, cur_id_match, new_id_match)
                                    || (cur_ext == new_ext
                                        && _prefer_tray_by_remain(filaments[i], tray_filaments[j], tray_filaments[picked_tar_idx]))) {
                                    picked_src_idx = i;
                                    picked_tar_idx = j;
                                }
                            }
                        }
                    }
                }
            }

            if (picked_src_idx >= 0 && picked_tar_idx >= 0) {
                auto tray = tray_filaments.find(distance_map[k][picked_tar_idx].tray_id);

                if (tray != tray_filaments.end())
                    _assign_mapping_result_from_tray(result[picked_src_idx], tray, tray->second.distance);

                _log_mapping_pick(picked_src_idx, picked_tar_idx, distance_map[picked_src_idx][picked_tar_idx].distance);
                picked_src.insert(picked_src_idx);
                picked_tar.insert(picked_tar_idx);
            }
        }
    }

    int DevMappingUtil::ams_filament_mapping(const MachineObject* obj, const std::vector<FilamentInfo>& filaments, std::vector<FilamentInfo>& result,
                                             std::vector<bool> map_opt, std::vector<int> exclude_id, bool nozzle_has_ams_then_ignore_ext, bool check_nozzle_type)
    {
        // Match priority:
        //   type (hard) > setting_id (sub-class) > filament_id (class) > color.
        constexpr float kColorDistanceMin    = 0.f;
        constexpr float kColorDistanceMax    = 1000.f;   // > real DeltaE76 upper bound (~260)
        constexpr float kClassDistanceOffset = 10000.f;  // class match (filament_id) only
        constexpr float kTypeDistanceOffset  = 20000.f;  // same type only
        constexpr float kMismatchDistance    = 999999.f; // hard fail (type / alpha mismatch)
        static_assert(kColorDistanceMax < kClassDistanceOffset, "color distance must not override sub-class / class match priority");
        static_assert(kTypeDistanceOffset - kClassDistanceOffset > kColorDistanceMax, "class-vs-type gap must exceed the color distance range");

        if (filaments.empty() || !obj)
            return -1;

        /////////////////////////
        // Step 1: collect filaments in machine
        std::map<int, FilamentInfo> tray_filaments; // tray_index : tray_color
        std::map<int, std::optional<DevNozzle>> tray_nozzles;
        std::set<int> ext_tray_keys; // candidate keys that are external spools (rack path)
        bool  left_nozzle_has_ams = false, right_nozzle_has_ams = false;

        const auto rack_type              = obj->GetNozzleRack()->GetRackType();
        const bool is_mapping_hotend_item = (rack_type == DevNozzleRack::RackType_A ||
                                             rack_type == DevNozzleRack::RackType_B);
        if (is_mapping_hotend_item)
        {
            // Hotend-rack machines: take each rack nozzle's currently-fed slot
            // (GetBindAmsSlots) as the candidate. map_opt is not applicable to the
            // rack's per-nozzle feed.
            _collect_mapping_hotend_item_candidates(obj, check_nozzle_type, tray_filaments, tray_nozzles, ext_tray_keys);
        }
        else
        {
        const auto& ams_list = obj->GetFilaSystem()->GetAmsList();
        for (auto ams = ams_list.begin(); ams != ams_list.end(); ams++)
        {
            std::string ams_id = ams->second->GetAmsId();
            auto        ams_type = ams->second->GetAmsType();
            if (ams_type == DevAmsType::AMS_LITE && ams->second->IsAmsLiteMixed())
            {
                ams_type = DevAmsType::AMS_LITE_MIXED;
            }
            for (auto tray = ams->second->GetTrays().begin(); tray != ams->second->GetTrays().end(); tray++)
            {
                int ams_id = atoi(ams->first.c_str());
                int tray_id = atoi(tray->first.c_str());
                int tray_index = ams->second->GetTrayId(tray_id);

                // skip exclude id
                for (int i = 0; i < exclude_id.size(); i++)
                {
                    if (tray_index == exclude_id[i])
                        continue;
                }
                // push
                FilamentInfo info;
                if (tray->second->is_tray_info_ready())
                {
                    _parse_tray_info(ams_id, tray_id, ams_type, *(tray->second), info);
                }

                //first: left,nozzle=1,map=1   second: right,nozzle=0,map=2
                bool right_ams_valid = (ams->second->GetBindedExtruderSet().count(MAIN_EXTRUDER_ID) != 0) && map_opt[MappingOption::USE_RIGHT_AMS];
                bool left_ams_valid = (ams->second->GetBindedExtruderSet().count(DEPUTY_EXTRUDER_ID) != 0) && map_opt[MappingOption::USE_LEFT_AMS];
                if (right_ams_valid || left_ams_valid)
                {
                    tray_filaments.emplace(std::make_pair(tray_index, info));
                    if (check_nozzle_type)
                        tray_nozzles.emplace(tray_index, _query_slot_nozzle(obj, tray->second));
                    if (right_ams_valid)
                    {
                        right_nozzle_has_ams = true;
                    }
                    if (left_ams_valid)
                    {
                        left_nozzle_has_ams = true;
                    }
                }
            }
        }

        if (map_opt[MappingOption::USE_RIGHT_EXT] || map_opt[MappingOption::USE_LEFT_EXT])
        {
            for (auto tray : obj->vt_slot)
            {
                bool right_ext_valid = (tray.id == std::to_string(VIRTUAL_TRAY_MAIN_ID) && map_opt[MappingOption::USE_RIGHT_EXT]);
                bool left_ext_valid = (tray.id == std::to_string(VIRTUAL_TRAY_DEPUTY_ID) && map_opt[MappingOption::USE_LEFT_EXT]);
                if (right_ext_valid || left_ext_valid)
                {
                    if (nozzle_has_ams_then_ignore_ext)
                    {
                        if (right_ext_valid && right_nozzle_has_ams)
                        {
                            continue;
                        }
                        if (left_ext_valid && left_nozzle_has_ams)
                        {
                            continue;
                        }
                    }
                    FilamentInfo info;
                    _parse_tray_info(atoi(tray.id.c_str()), 0, DevAmsType::EXT_SPOOL, tray, info);
                    tray_filaments.emplace(std::make_pair(info.tray_id, info));
                    if (check_nozzle_type)
                        tray_nozzles.emplace(info.tray_id, _query_slot_nozzle(obj, &tray));
                }
            }
        }
        }

        /////////////////////////
        // Step 2: collect the distances of filaments_in_slicing to filaments_in_machine
        char buffer[256];
        std::vector<std::vector<DisValue>> distance_map;

        // print title
        ::sprintf(buffer, "F(id)");
        std::string line = std::string(buffer);
        for (auto tray = tray_filaments.begin(); tray != tray_filaments.end(); tray++)
        {
            ::sprintf(buffer, "   AMS%02d", tray->second.id + 1);
            line += std::string(buffer);
        }
        BOOST_LOG_TRIVIAL(info) << "ams_mapping_distance:" << line;// Print the collected filaments

        for (int i = 0; i < filaments.size(); i++)
        {
            std::vector<DisValue> rol;
            ::sprintf(buffer, "F(%02d)", filaments[i].id + 1);
            line = std::string(buffer);
            for (auto tray = tray_filaments.begin(); tray != tray_filaments.end(); tray++)
            {
                DisValue val;
                val.tray_id = tray->second.id;
                val.is_ext  = ext_tray_keys.count(tray->first) > 0;
                wxColour c = wxColour(filaments[i].color);
                wxColour tray_c = DevAmsTray::decode_color(tray->second.color);
                float color_distance = GUI::calc_color_distance(GUI::convert_to_rgba(c), GUI::convert_to_rgba(tray_c));
                val.distance = std::clamp(color_distance, kColorDistanceMin, kColorDistanceMax);
                if (filaments[i].type != tray->second.type)
                {
                    val.distance = kMismatchDistance;
                    val.is_type_match = false;
                }
                else
                {
                    const bool is_sub_class_match = !filaments[i].setting_id.empty() && !tray->second.setting_id.empty()
                        && filaments[i].setting_id == tray->second.setting_id;
                    const bool is_class_match = !filaments[i].filament_id.empty() && !tray->second.filament_id.empty()
                        && filaments[i].filament_id == tray->second.filament_id;
                    if (!is_sub_class_match) {
                        val.distance += is_class_match ? kClassDistanceOffset : kTypeDistanceOffset;
                    }

                    val.is_type_match = true;
                }

                if (check_nozzle_type) {
                    auto nz_it = tray_nozzles.find(tray->first);
                    const bool has_slot_nz = (nz_it != tray_nozzles.end() && nz_it->second.has_value());
                    const bool has_fila_nz = (filaments[i].nozzle_diameter > 0.0) && (!filaments[i].nozzle_volume_type.empty());
                    if (has_slot_nz && has_fila_nz) {
                        const DevNozzle& nz = nz_it->second.value();
                        const bool diameter_match = std::fabs(filaments[i].nozzle_diameter - (double)nz.GetNozzleDiameter()) < 1e-3;
                        const NozzleFlowType fila_flow = DevNozzle::ToNozzleFlowType(filaments[i].nozzle_volume_type);
                        const bool flow_match = (fila_flow == nz.GetNozzleFlowType());
                        if (!diameter_match || !flow_match) {
                            val.distance = 999999;
                            val.is_nozzle_match = false;
                        }
                    }
                }

                ::sprintf(buffer, "  %6.0f", val.distance);
                line += std::string(buffer);
                rol.push_back(val);
            }
            BOOST_LOG_TRIVIAL(info) << "ams_mapping_distance:" << line;
            distance_map.push_back(rol);
        }

        /////////////////////////
        // Step 3: do mapping algorithm

        // setup the mapping result
        for (int i = 0; i < filaments.size(); i++)
        {
            FilamentInfo info;
            info.id = filaments[i].id;
            info.tray_id = -1;
            info.type = filaments[i].type;
            info.filament_id = filaments[i].filament_id;
            info.setting_id = filaments[i].setting_id;
            result.push_back(info);
        }

        if (is_mapping_hotend_item)
            _run_hotend_rack_color_mapping(filaments, tray_filaments, distance_map, result);
        else
            _run_legacy_greedy_ams_mapping(filaments, tray_filaments, distance_map, result);



        //check ams mapping result
        if (DevMappingUtil::is_valid_mapping_result(obj, result, true))
        {
            return 0;
        }

        /* for (auto it = result.begin(); it != result.end(); it++) {//This code has never been effective before 2025.03.18
             if (it->distance >= 6000) {
                 it->tray_id = -1;
             }
         }*/
        return 0;
    }

}