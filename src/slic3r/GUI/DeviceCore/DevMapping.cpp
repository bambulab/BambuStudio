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
    bool DevMappingUtil::is_valid_mapping_result(const MachineObject* obj, std::vector<FilamentInfo>& result, bool check_empty_slot)
    {
        if (result.empty()) return false;

        for (int i = 0; i < result.size(); i++)
        {
            // invalid mapping result
            if (result[i].tray_id < 0)
            {
                if (result[i].ams_id.empty() && result[i].slot_id.empty())
                {
                    return false;
                }
            }
            else
            {

                auto ams_item = obj->GetFilaSystem()->GetAmsById(result[i].ams_id);
                if (ams_item == nullptr)
                {
                    if ((result[i].ams_id != std::to_string(VIRTUAL_TRAY_MAIN_ID)) &&
                        (result[i].ams_id != std::to_string(VIRTUAL_TRAY_DEPUTY_ID)))
                    {
                        result[i].tray_id = -1;
                        return false;
                    }
                }
                else
                {
                    if (check_empty_slot)
                    {
                        auto tray_item = ams_item->GetTrays().find(result[i].slot_id);
                        if (tray_item == ams_item->GetTrays().end())
                        {
                            result[i].tray_id = -1;
                            return false;
                        }
                        else
                        {
                            if (!tray_item->second->is_exists)
                            {
                                result[i].tray_id = -1;
                                return false;
                            }
                        }
                    }
                }
            }
        }
        return true;
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
    // N3F). A nozzle with no filament reports an empty bind list, so empty
    // external spools never become candidates. At most one candidate per nozzle
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



        // traverse the mapping
        std::set<int> picked_src;
        std::set<int> picked_tar;
        for (int k = 0; k < distance_map.size(); k++)
        {
            float min_val = INT_MAX;
            int picked_src_idx = -1;
            int picked_tar_idx = -1;
            for (int i = 0; i < distance_map.size(); i++)
            {
                if (picked_src.find(i) != picked_src.end())
                    continue;

                // try to mapping to different tray
                for (int j = 0; j < distance_map[i].size(); j++)
                {
                    if (picked_tar.find(j) != picked_tar.end())
                    {
                        if (distance_map[i][j].is_same_color
                            && distance_map[i][j].is_type_match
                            && distance_map[i][j].is_nozzle_match
                            && distance_map[i][j].distance < (float)0.0001)
                        {
                            min_val = distance_map[i][j].distance;
                            picked_src_idx = i;
                            picked_tar_idx = j;
                            tray_filaments[picked_tar_idx].distance = min_val;
                        }
                        continue;
                    }

                    if (distance_map[i][j].is_same_color
                        && distance_map[i][j].is_type_match
                        && distance_map[i][j].is_nozzle_match)
                    {
                        if (min_val > distance_map[i][j].distance)
                        {
                            min_val = distance_map[i][j].distance;
                            picked_src_idx = i;
                            picked_tar_idx = j;
                            tray_filaments[picked_tar_idx].distance = min_val;
                        }
                        else if (min_val == distance_map[i][j].distance)
                        {
                            // Same-color (equal-distance) tie-break: prefer an
                            // external spool first (hotend-rack requirement), then
                            // fall back to preferring an exact filament_id match.
                            const bool cur_ext      = distance_map[picked_src_idx][picked_tar_idx].is_ext;
                            const bool new_ext      = distance_map[i][j].is_ext;
                            const bool cur_id_match = filaments[picked_src_idx].filament_id == tray_filaments[picked_tar_idx].filament_id;
                            const bool new_id_match = filaments[i].filament_id == tray_filaments[j].filament_id;
                            if ((!cur_ext && new_ext) ||
                                (cur_ext == new_ext && !cur_id_match && new_id_match))
                            {
                                picked_src_idx = i;
                                picked_tar_idx = j;
                            }
                        }
                        // Same color/type and equal distance: prefer the matching tray with the
                        // least remaining filament, see _prefer_tray_by_remain() for the rules.
                        else if (min_val == distance_map[i][j].distance
                                 && _prefer_tray_by_remain(filaments[i], tray_filaments[j], tray_filaments[picked_tar_idx]))
                        {
                            picked_src_idx = i;
                            picked_tar_idx = j;
                        }
                    }
                }

                // take a retry to mapping to used tray
                if (picked_src_idx < 0 || picked_tar_idx < 0)
                {
                    for (int j = 0; j < distance_map[i].size(); j++)
                    {
                        if (distance_map[i][j].is_same_color && distance_map[i][j].is_type_match && distance_map[i][j].is_nozzle_match)
                        {
                            if (min_val > distance_map[i][j].distance)
                            {
                                min_val = distance_map[i][j].distance;
                                picked_src_idx = i;
                                picked_tar_idx = j;
                                tray_filaments[picked_tar_idx].distance = min_val;
                            }
                            else if (min_val == distance_map[i][j].distance)
                            {
                                // Same-color tie-break (retry-to-used-tray branch):
                                // prefer external spool, then exact filament_id.
                                const bool cur_ext      = distance_map[picked_src_idx][picked_tar_idx].is_ext;
                                const bool new_ext      = distance_map[i][j].is_ext;
                                const bool cur_id_match = filaments[picked_src_idx].filament_id == tray_filaments[picked_tar_idx].filament_id;
                                const bool new_id_match = filaments[i].filament_id == tray_filaments[j].filament_id;
                                if ((!cur_ext && new_ext) ||
                                    (cur_ext == new_ext && !cur_id_match && new_id_match))
                                {
                                    picked_src_idx = i;
                                    picked_tar_idx = j;
                                }
                            }
                            // Prefer the matching tray with the least remaining filament (see above).
                            else if (min_val == distance_map[i][j].distance
                                     && _prefer_tray_by_remain(filaments[i], tray_filaments[j], tray_filaments[picked_tar_idx]))
                            {
                                picked_src_idx = i;
                                picked_tar_idx = j;
                            }
                        }
                    }
                }
            }

            if (picked_src_idx >= 0 && picked_tar_idx >= 0)
            {
                auto tray = tray_filaments.find(distance_map[k][picked_tar_idx].tray_id);

                if (tray != tray_filaments.end())
                {
                    result[picked_src_idx].tray_id = tray->first;

                    result[picked_src_idx].color = tray->second.color;
                    result[picked_src_idx].type = tray->second.type;
                    result[picked_src_idx].distance = tray->second.distance;
                    result[picked_src_idx].filament_id = tray->second.filament_id;
                    result[picked_src_idx].setting_id = tray->second.setting_id;
                    result[picked_src_idx].ctype = tray->second.ctype;
                    result[picked_src_idx].colors = tray->second.colors;

                    /*for new ams mapping*/
                    result[picked_src_idx].ams_id = tray->second.ams_id;
                    result[picked_src_idx].slot_id = tray->second.slot_id;
                }

                ::sprintf(buffer, "ams_mapping, picked F(%02d) AMS(%02d), distance=%6.0f", picked_src_idx + 1, picked_tar_idx + 1,
                    distance_map[picked_src_idx][picked_tar_idx].distance);
                BOOST_LOG_TRIVIAL(info) << std::string(buffer);
                picked_src.insert(picked_src_idx);
                picked_tar.insert(picked_tar_idx);
            }
        }



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