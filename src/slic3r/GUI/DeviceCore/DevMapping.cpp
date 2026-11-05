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
#include "libslic3r/Log.hpp"

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

    // Filament already in a hotend must leave before any other slot can feed it,
    // and only a sub AMS retracts its own. So the occupant decides for the rest.
    DevSlotFeedBlock DevMappingUtil::QueryNozzleOccupantBlock(const DevNozzle& nz)
    {
        if (!nz.HasFilamentAtNozzleInlet())
            return DevSlotFeedBlock::None;

        // Resolves AMS slots and external spools alike, so ams_type classifies both.
        const auto occupant = nz.GetSlotNow();
        if (!occupant.has_value())
            return DevSlotFeedBlock::None; // occupant unknown: stay permissive

        if (devAmsTypeUtil::CanAutoRetract(occupant->ams_type))
            return DevSlotFeedBlock::None;

        return (occupant->ams_type == DevAmsType::EXT_SPOOL) ? DevSlotFeedBlock::BlockedByExtSpool
                                                             : DevSlotFeedBlock::BlockedByRackAms;
    }

    DevSlotFeedBlock DevMappingUtil::QuerySlotFeedBlock(const DevNozzle& nz, DevAmsType ams_type, const DevAmsSlotId& slot)
    {
        // The slot already in the feed path needs nothing to happen first.
        if (nz.HasFilamentAtNozzleInlet()) {
            const auto slot_now = nz.GetSlotNowId();
            // An external spool has a single slot, so its ams id alone identifies it.
            const bool is_occupant = slot_now.has_value() && ((ams_type == DevAmsType::EXT_SPOOL) ? (slot_now->first == slot.first)
                                                                                                  : (*slot_now == slot));
            if (is_occupant)
                return DevSlotFeedBlock::None;
        }

        // Must outrank the feeder check below, otherwise a feeder-less rack slot
        // (ZMULTI_B) reports the milder NeedManualFeed behind an engaged spool.
        const DevSlotFeedBlock occupant_block = QueryNozzleOccupantBlock(nz);
        if (occupant_block != DevSlotFeedBlock::None)
            return occupant_block;

        if (!devAmsTypeUtil::CanAutoFeed(ams_type))
            return DevSlotFeedBlock::NeedManualFeed;

        return DevSlotFeedBlock::None;
    }

    bool DevMappingUtil::IsSlotAutoLoadable(const DevNozzle& nz, DevAmsType ams_type, const DevAmsSlotId& slot)
    {
        return QuerySlotFeedBlock(nz, ams_type, slot) == DevSlotFeedBlock::None;
    }

    // calc distance map
    struct DisValue {
        DevAmsSlotId slot_key{-1, -1}; // identifies the candidate slot, and keys tray_filaments
        float distance;
        bool  is_same_color = true;
        bool  is_type_match = true;
        bool  is_nozzle_match = true;
        bool  is_ext = false; // candidate tray is an external spool (hotend-rack)
        bool  can_auto_feed = true; // false: the user must feed or unload the hotend by hand first
        bool  is_sub_ams = false; // candidate slot belongs to an attached sub AMS (hotend-rack)
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
                // Display-side numbering (DevFilaSystem::GetTrayIndexMap(), GUI_App::transition_tridid),
                // which differs from the tray_xxx_bits index returned by DevAms::GetTrayId().
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

    static void _collect_mapping_hotend_item_candidates(const MachineObject*                              obj,
                                                        bool                                              collect_nozzles,
                                                        std::map<DevAmsSlotId, FilamentInfo>&             tray_filaments,
                                                        std::map<DevAmsSlotId, std::optional<DevNozzle>>& tray_nozzles,
                                                        std::set<DevAmsSlotId>&                           ext_tray_keys,
                                                        std::set<DevAmsSlotId>&                           no_auto_feed_keys,
                                                        std::set<DevAmsSlotId>&                           sub_ams_keys)
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
            const DevAmsSlotId key = tray->get_ams_slot_id();
            if (!tray_filaments.emplace(key, info).second)
                return; // same slot reachable from multiple nozzles: keep the first
            if (collect_nozzles)
                tray_nozzles.emplace(key, nz);
            if (is_ext)
                ext_tray_keys.insert(key);
            if (!DevMappingUtil::IsSlotAutoLoadable(*nz, type, DevAmsSlotId{ams_id, slot_id}))
                no_auto_feed_keys.insert(key);
            if (type == DevAmsType::N3S || type == DevAmsType::N3F || type == DevAmsType::AMS)
                sub_ams_keys.insert(key);
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

            // Every slot reachable from this hotend goes into the pool, matching
            // what the popup lists; try_add flags the unreachable ones instead.
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

            // A sub AMS drives its own feeder, so every slot in it is reachable
            // from this nozzle. GetBindAmsSlots() only reports the slot that is
            // feeding right now, so walk the whole unit to pick up the rest.
            for (int bind_ams_id : nz->GetBindAms())
            {
                DevAms* ams = fila_sys->GetAmsById(std::to_string(bind_ams_id));
                if (!ams || !ams->IsExist())
                    continue;

                const DevAmsType ams_type = ams->GetAmsType();
                if (ams_type != DevAmsType::N3S && ams_type != DevAmsType::N3F && ams_type != DevAmsType::AMS)
                    continue;

                for (const auto& tray_item : ams->GetTrays())
                    try_add(bind_ams_id, atoi(tray_item.first.c_str()), ams_type, tray_item.second, nz, /*is_ext*/ false);
            }
        }
    }

    // Prefer a non-empty matching tray with less remaining filament. Unknown
    // remain keeps the existing slot order, and equal remain stays stable.
    static bool _prefer_tray_by_remain(const FilamentInfo& filament, const FilamentInfo& candidate, const FilamentInfo& picked)
    {
        if (filament.filament_id != candidate.filament_id || filament.filament_id != picked.filament_id)
            return false;
        if (candidate.remain <= 0 || picked.remain < 0)
            return false;
        return picked.remain == 0 || candidate.remain < picked.remain;
    }

    static bool _prefer_mapping_tie_break(bool cur_id_match, bool new_id_match)
    {
        return !cur_id_match && new_id_match;
    }

    // On a same-color tie, a slot on the built-in rack AMS or on the external spool
    // wins over an attached sub AMS, so the sub AMS is kept for the slots that only
    // it can supply.
    static bool _prefer_non_sub_ams(bool cur_sub_ams, bool new_sub_ams)
    {
        return cur_sub_ams && !new_sub_ams;
    }

    static void _assign_mapping_result_from_tray(FilamentInfo&                                         dst,
                                                 const std::map<DevAmsSlotId, FilamentInfo>::iterator& tray,
                                                 float                                                 distance)
    {
        dst.tray_id     = tray->second.id;
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

    // Hotend-rack: each slicing filament independently picks the closest-color
    // slot. Slots may be shared across filaments. A slot needing a manual step
    // joins only when allow_manual_feed, and then loses an equal match.
    static void _run_hotend_rack_color_mapping(const std::vector<FilamentInfo>&          filaments,
                                               std::map<DevAmsSlotId, FilamentInfo>&     tray_filaments,
                                               const std::vector<std::vector<DisValue>>& distance_map,
                                               bool                                      allow_manual_feed,
                                               std::vector<FilamentInfo>&                result)
    {
        for (int i = 0; i < (int) distance_map.size(); i++) {
            float min_val = INT_MAX;
            int   best_j  = -1;
            for (int j = 0; j < (int) distance_map[i].size(); j++) {
                const auto& dv = distance_map[i][j];
                if (!dv.is_type_match || !dv.is_nozzle_match || (!dv.can_auto_feed && !allow_manual_feed))
                    continue;
                if (min_val > dv.distance) {
                    min_val = dv.distance;
                    best_j  = j;
                } else if (best_j >= 0 && min_val == dv.distance) {
                    const bool cur_auto_feed = distance_map[i][best_j].can_auto_feed;
                    if (cur_auto_feed != dv.can_auto_feed) {
                        if (dv.can_auto_feed)
                            best_j = j;
                        continue;
                    }
                    const bool cur_sub_ams = distance_map[i][best_j].is_sub_ams;
                    const bool new_sub_ams = dv.is_sub_ams;
                    auto       cur_tray = tray_filaments.find(distance_map[i][best_j].slot_key);
                    auto       new_tray = tray_filaments.find(dv.slot_key);
                    const bool cur_id_match = cur_tray != tray_filaments.end()
                        && filaments[i].filament_id == cur_tray->second.filament_id;
                    const bool new_id_match = new_tray != tray_filaments.end()
                        && filaments[i].filament_id == new_tray->second.filament_id;
                    if (_prefer_mapping_tie_break(cur_id_match, new_id_match)
                        || _prefer_non_sub_ams(cur_sub_ams, new_sub_ams)
                        || (cur_sub_ams == new_sub_ams
                            && cur_tray != tray_filaments.end() && new_tray != tray_filaments.end()
                            && _prefer_tray_by_remain(filaments[i], new_tray->second, cur_tray->second)))
                        best_j = j;
                }
            }
            if (best_j < 0)
                continue;

            auto tray = tray_filaments.find(distance_map[i][best_j].slot_key);
            if (tray == tray_filaments.end())
                continue;

            _assign_mapping_result_from_tray(result[i], tray, min_val);
            _log_mapping_pick(i, best_j, min_val);
        }
    }

    // Legacy AMS: greedy bipartite matching; a tray is reserved per round unless an
    // exact color match allows reuse.
    static void _run_legacy_greedy_ams_mapping(const std::vector<FilamentInfo>&          filaments,
                                               std::map<DevAmsSlotId, FilamentInfo>&     tray_filaments,
                                               const std::vector<std::vector<DisValue>>& distance_map,
                                               std::vector<FilamentInfo>&                result)
    {
        // A distance_map column is a candidate slot, not an index into tray_filaments:
        // the slots are keyed by <ams_id, slot_id> and the column order only mirrors
        // the map iteration order.
        auto slot_at = [&tray_filaments, &distance_map](int i, int j) {
            return tray_filaments.find(distance_map[i][j].slot_key);
        };

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
                            if (auto slot = slot_at(i, j); slot != tray_filaments.end())
                                slot->second.distance = min_val;
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
                            if (auto slot = slot_at(i, j); slot != tray_filaments.end())
                                slot->second.distance = min_val;
                        } else if (min_val == distance_map[i][j].distance) {
                            auto cur_tray = slot_at(picked_src_idx, picked_tar_idx);
                            auto new_tray = slot_at(i, j);
                            if (cur_tray == tray_filaments.end() || new_tray == tray_filaments.end())
                                continue;
                            const bool cur_id_match = filaments[picked_src_idx].filament_id == cur_tray->second.filament_id;
                            const bool new_id_match = filaments[i].filament_id == new_tray->second.filament_id;
                            if (_prefer_mapping_tie_break(cur_id_match, new_id_match)
                                || _prefer_tray_by_remain(filaments[i], new_tray->second, cur_tray->second)) {
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
                                if (auto slot = slot_at(i, j); slot != tray_filaments.end())
                                    slot->second.distance = min_val;
                            } else if (min_val == distance_map[i][j].distance) {
                                auto cur_tray = slot_at(picked_src_idx, picked_tar_idx);
                                auto new_tray = slot_at(i, j);
                                if (cur_tray == tray_filaments.end() || new_tray == tray_filaments.end())
                                    continue;
                                const bool cur_id_match = filaments[picked_src_idx].filament_id == cur_tray->second.filament_id;
                                const bool new_id_match = filaments[i].filament_id == new_tray->second.filament_id;
                                if (_prefer_mapping_tie_break(cur_id_match, new_id_match)
                                    || _prefer_tray_by_remain(filaments[i], new_tray->second, cur_tray->second)) {
                                    picked_src_idx = i;
                                    picked_tar_idx = j;
                                }
                            }
                        }
                    }
                }
            }

            if (picked_src_idx >= 0 && picked_tar_idx >= 0) {
                auto tray = slot_at(picked_src_idx, picked_tar_idx);

                if (tray != tray_filaments.end())
                    _assign_mapping_result_from_tray(result[picked_src_idx], tray, tray->second.distance);

                _log_mapping_pick(picked_src_idx, picked_tar_idx, distance_map[picked_src_idx][picked_tar_idx].distance);
                picked_src.insert(picked_src_idx);
                picked_tar.insert(picked_tar_idx);
            }
        }
    }

    int DevMappingUtil::ams_filament_mapping(const MachineObject* obj, const std::vector<FilamentInfo>& filaments, std::vector<FilamentInfo>& result,
                                             std::vector<bool> map_opt, std::vector<int> exclude_id, bool nozzle_has_ams_then_ignore_ext,
                                             bool color_over_subtype, NozzleMatchWeight nozzle_weight, bool allow_manual_feed)
    {
        // Match priority:
        //   type (hard) > setting_id (sub-class) > filament_id (class) > color.
        // color_over_subtype swaps the last three: type (hard) > color > setting_id > filament_id, using a
        // sub-DeltaE tie term so product identity only splits an otherwise-equal colour.
        constexpr float kColorDistanceMin    = 0.f;
        constexpr float kColorDistanceMax    = 1000.f;   // > real DeltaE76 upper bound (~260)
        constexpr float kClassDistanceOffset = 10000.f;  // class match (filament_id) only
        constexpr float kTypeDistanceOffset  = 20000.f;  // same type only
        constexpr float kMismatchDistance    = 999999.f; // hard fail (type / alpha mismatch)
        constexpr float kColorTieBreak       = 0.25f;    // color_over_subtype: below one DeltaE step, so it never overrides a visible colour gap
        constexpr float kNozzleMismatchWeight = 1.0f;    // NozzleMatchWeight::Prefer soft penalty: ~1 DeltaE, so it outranks the product-identity tie term yet loses to any real colour gap
        constexpr float kMultiColorPenalty   = 2000.f;   // single colour wins over a gradient slot, which is only a dominant-colour fallback
        static_assert(kColorDistanceMax < kClassDistanceOffset, "color distance must not override sub-class / class match priority");
        static_assert(kMultiColorPenalty > kColorDistanceMax, "single-colour preference must outrank any color distance");
        static_assert(kColorDistanceMax + kMultiColorPenalty < kClassDistanceOffset, "single-colour preference must not override sub-class / class match priority");
        static_assert(kTypeDistanceOffset - kClassDistanceOffset > kColorDistanceMax + kMultiColorPenalty, "class-vs-type gap must exceed the penalised color distance range");
        static_assert(kNozzleMismatchWeight > 2.f * kColorTieBreak, "nozzle weight must outrank the product-identity tie term");

        if (filaments.empty() || !obj)
            return -1;

        // Collect each candidate slot's nozzle whenever the nozzle has any say (Prefer nudges,
        // Require gates); Ignore skips the lookup entirely.
        const bool collect_slot_nozzles = (nozzle_weight != NozzleMatchWeight::Ignore);

        /////////////////////////
        // Step 1: collect filaments in machine
        std::map<DevAmsSlotId, FilamentInfo> tray_filaments; // <ams_id, slot_id> : tray_color
        std::map<DevAmsSlotId, std::optional<DevNozzle>> tray_nozzles;
        std::set<DevAmsSlotId> ext_tray_keys; // candidate keys that are external spools (rack path)
        std::set<DevAmsSlotId> no_auto_feed_keys; // candidate keys the printer cannot feed on its own (rack path)
        std::set<DevAmsSlotId> sub_ams_keys; // candidate keys on an attached sub AMS (rack path)
        bool  left_nozzle_has_ams = false, right_nozzle_has_ams = false;

        const bool is_mapping_hotend_item = obj->GetNozzleRack()->IsDirectConnectedRack();
        if (is_mapping_hotend_item)
        {
            // Hotend-rack machines: every slot reachable from a rack nozzle, with
            // the unreachable ones flagged rather than missing. map_opt is n/a here.
            _collect_mapping_hotend_item_candidates(obj, collect_slot_nozzles, tray_filaments, tray_nozzles, ext_tray_keys, no_auto_feed_keys, sub_ams_keys);
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
                    const DevAmsSlotId slot_key = tray->second->get_ams_slot_id();
                    tray_filaments.emplace(slot_key, info);
                    if (collect_slot_nozzles)
                        tray_nozzles.emplace(slot_key, _query_slot_nozzle(obj, tray->second));
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
                    const DevAmsSlotId slot_key = tray.get_ams_slot_id();
                    tray_filaments.emplace(slot_key, info);
                    if (collect_slot_nozzles)
                        tray_nozzles.emplace(slot_key, _query_slot_nozzle(obj, &tray));
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
                val.slot_key = tray->first;
                val.is_ext  = ext_tray_keys.count(tray->first) > 0;
                val.is_sub_ams = sub_ams_keys.count(tray->first) > 0;
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
                        // color_over_subtype keeps the same sub-class > class > type order but shrinks the gaps below
                        // one DeltaE step, so colour stays the primary sort and identity only breaks a tie.
                        const float class_step = color_over_subtype ? kColorTieBreak : kClassDistanceOffset;
                        const float type_step  = color_over_subtype ? kColorTieBreak * 2.f : kTypeDistanceOffset;
                        val.distance += is_class_match ? class_step : type_step;
                    }

                    // Project filaments are always single colour, so any gradient slot is a fallback.
                    if (tray->second.colors.size() > 1)
                        val.distance += kMultiColorPenalty;

                    val.is_type_match = true;
                }

                if (collect_slot_nozzles) {
                    auto nz_it = tray_nozzles.find(tray->first);
                    const bool has_slot_nz = (nz_it != tray_nozzles.end() && nz_it->second.has_value());
                    const bool has_fila_nz = filaments[i].nozzle_diameter > 0.0;
                    if (has_slot_nz && has_fila_nz) {
                        const DevNozzle& nz = nz_it->second.value();
                        const bool diameter_match = std::fabs(filaments[i].nozzle_diameter - (double)nz.GetNozzleDiameter()) < 1e-3;
                        // Empty means no flow request; Hybrid means the slice spread the filament over
                        // several flows. Either accepts any flow. An unknown flow still mismatches.
                        const std::string &fila_volume  = filaments[i].nozzle_volume_type;
                        const bool         any_flow     = fila_volume.empty() || fila_volume == get_nozzle_volume_type_string(nvtHybrid);
                        const bool         flow_match   = any_flow || DevNozzle::ToNozzleFlowType(fila_volume) == nz.GetNozzleFlowType();
                        if (!diameter_match || !flow_match) {
                            if (nozzle_weight == NozzleMatchWeight::Require) {
                                // Precision-first: a nozzle mismatch is barred from auto mapping.
                                val.distance = kMismatchDistance;
                                val.is_nozzle_match = false;
                            } else {
                                // Color-first: keep the tray eligible but rank it behind a matching
                                // nozzle, so an otherwise-equal pair resolves to the matching slot.
                                val.distance += kNozzleMismatchWeight;
                            }
                        }
                    }
                }

                // Keep the real distance so the log still explains the choice, but
                // flag it: reaching it needs a manual step.
                if (no_auto_feed_keys.count(tray->first))
                    val.can_auto_feed = false;

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
            _run_hotend_rack_color_mapping(filaments, tray_filaments, distance_map, allow_manual_feed, result);
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

    std::vector<FilamentInfo> DevMappingUtil::enumerate_dest_slots(const MachineObject* obj)
    {
        std::vector<FilamentInfo> slots;
        if (!obj || !obj->GetFilaSystem() || !obj->GetNozzleRack())
            return slots;

        auto set_diameter = [](FilamentInfo& info, const std::optional<DevNozzle>& nz) {
            if (nz.has_value()) {
                info.nozzle_diameter    = (double) nz->GetNozzleDiameter();
                info.nozzle_volume_type = DevNozzle::GetNozzleFlowTypeString(nz->GetNozzleFlowType());
            }
        };

        if (obj->GetNozzleRack()->IsDirectConnectedRack()) {
            std::map<DevAmsSlotId, FilamentInfo>             tray_filaments;
            std::map<DevAmsSlotId, std::optional<DevNozzle>> tray_nozzles;
            std::set<DevAmsSlotId>                           ext_keys, not_loaded_keys, sub_ams_keys;
            _collect_mapping_hotend_item_candidates(obj, true, tray_filaments, tray_nozzles, ext_keys, not_loaded_keys, sub_ams_keys);
            for (auto& kv : tray_filaments) {
                FilamentInfo info = kv.second;
                auto         it   = tray_nozzles.find(kv.first);
                if (it != tray_nozzles.end())
                    set_diameter(info, it->second);
                slots.push_back(info);
            }
            return slots;
        }

        // Legacy single/dual-extruder AMS plus external spools.
        // A single-nozzle machine (P1 / A1 / X1 class) has no nozzle rack, so the per-slot lookup below
        // (rack nozzles only) never resolves one. Its lone extruder nozzle feeds every slot, so fall back
        // to it for the diameter / volume type when the lookup comes back empty.
        std::optional<DevNozzle> single_ext_nozzle;
        if (auto* ns = obj->GetNozzleSystem(); ns && ns->GetExtNozzleCount() == 1)
            single_ext_nozzle = ns->GetExtNozzles().begin()->second;
        auto resolve_nozzle = [&](const DevAmsTray* tray) -> std::optional<DevNozzle> {
            auto nz = _query_slot_nozzle(obj, tray);
            return nz.has_value() ? nz : single_ext_nozzle;
        };
        const auto& ams_list = obj->GetFilaSystem()->GetAmsList();
        for (auto ams = ams_list.begin(); ams != ams_list.end(); ++ams) {
            auto ams_type = ams->second->GetAmsType();
            if (ams_type == DevAmsType::AMS_LITE && ams->second->IsAmsLiteMixed())
                ams_type = DevAmsType::AMS_LITE_MIXED;
            for (auto tray = ams->second->GetTrays().begin(); tray != ams->second->GetTrays().end(); ++tray) {
                if (!tray->second->is_tray_info_ready())
                    continue;
                FilamentInfo info;
                _parse_tray_info(atoi(ams->first.c_str()), atoi(tray->first.c_str()), ams_type, *(tray->second), info);
                set_diameter(info, resolve_nozzle(tray->second));
                slots.push_back(info);
            }
        }
        for (auto tray : obj->vt_slot) {
            FilamentInfo info;
            _parse_tray_info(atoi(tray.id.c_str()), 0, DevAmsType::EXT_SPOOL, tray, info);
            set_diameter(info, resolve_nozzle(&tray));
            slots.push_back(info);
        }
        return slots;
    }

}