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

    bool DevMappingUtil::IsSlotAutoLoadable(const DevNozzle& nz, DevAmsType ams_type, const DevAmsSlotId& slot)
    {
        if (ams_type != DevAmsType::ZMULTI_B && ams_type != DevAmsType::EXT_SPOOL)
            return true; // ZMULTI_A has a feeder motor; N3F / N3S / AMS feed themselves

        if (!nz.HasFilamentInNozzle())
            return false;

        const auto slot_now = nz.GetSlotNowId();
        if (!slot_now.has_value()) {
            return false;
        }

        // An external spool has a single slot, so its ams id alone identifies it.
        return (ams_type == DevAmsType::EXT_SPOOL) ? (slot_now->first == slot.first)
                                                   : (*slot_now == slot);
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

    // Match priority:
    //   type (hard) > setting_id (sub-class) > filament_id (class) > color.
    static constexpr float kColorDistanceMin    = 0.f;
    static constexpr float kColorDistanceMax    = 1000.f;   // > real DeltaE76 upper bound (~260)
    static constexpr float kClassDistanceOffset = 10000.f;  // class match (filament_id) only
    static constexpr float kTypeDistanceOffset  = 20000.f;  // same type only
    static constexpr float kMismatchDistance    = 999999.f; // hard fail (type / alpha mismatch)
    static_assert(kColorDistanceMax < kClassDistanceOffset, "color distance must not override sub-class / class match priority");
    static_assert(kTypeDistanceOffset - kClassDistanceOffset > kColorDistanceMax, "class-vs-type gap must exceed the color distance range");

    // Score one (slicing filament, machine slot) pair. Shared by the legacy AMS
    // path and the hotend-rack path so both keep the same priority ordering.
    static void _calc_match_distance(const FilamentInfo& fila,
                                     const FilamentInfo& cand,
                                     const DevNozzle*    cand_nz,
                                     bool                check_nozzle_type,
                                     DisValue&           val)
    {
        wxColour c = wxColour(fila.color);
        wxColour tray_c = DevAmsTray::decode_color(cand.color);
        float color_distance = GUI::calc_color_distance(GUI::convert_to_rgba(c), GUI::convert_to_rgba(tray_c));
        val.distance = std::clamp(color_distance, kColorDistanceMin, kColorDistanceMax);
        if (fila.type != cand.type)
        {
            val.distance = kMismatchDistance;
            val.is_type_match = false;
        }
        else
        {
            const bool is_sub_class_match = !fila.setting_id.empty() && !cand.setting_id.empty()
                && fila.setting_id == cand.setting_id;
            const bool is_class_match = !fila.filament_id.empty() && !cand.filament_id.empty()
                && fila.filament_id == cand.filament_id;
            if (!is_sub_class_match) {
                val.distance += is_class_match ? kClassDistanceOffset : kTypeDistanceOffset;
            }

            val.is_type_match = true;
        }

        if (check_nozzle_type) {
            const bool has_fila_nz = (fila.nozzle_diameter > 0.0) && (!fila.nozzle_volume_type.empty());
            if (cand_nz && has_fila_nz) {
                const bool diameter_match = std::fabs(fila.nozzle_diameter - (double) cand_nz->GetNozzleDiameter()) < 1e-3;
                const NozzleFlowType fila_flow = DevNozzle::ToNozzleFlowType(fila.nozzle_volume_type);
                const bool flow_match = (fila_flow == cand_nz->GetNozzleFlowType());
                if (!diameter_match || !flow_match) {
                    val.distance = 999999;
                    val.is_nozzle_match = false;
                }
            }
        }
    }

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

    struct RackCandidate
    {
        FilamentInfo             info;
        std::optional<DevNozzle> nozzle;
        int  addr_id   = -1;                // rack position this slot feeds
        bool is_ext    = false;
        bool preferred = true;              // false = fallback, used only when nothing
                                            // on this nozzle matches the preferred set
    };

    // Resolve by stable addr_id first: the rack nozzle id becomes 0 once an extruder
    // picks it up.
    static std::optional<DevNozzle> _query_rack_nozzle(const DevNozzleSystem* nozzle_sys, int pos)
    {
        auto nz = nozzle_sys->GetNozzleOptByAddrId(pos - 0x10);
        if (!nz.has_value())
            nz = nozzle_sys->GetNozzleOptByPosId(pos);
        return nz;
    }

    // Sub-AMS units bound to a rack nozzle. GetBindAms() also lists the built-in rack
    // AMS, which cannot unload on its own and so is filtered out here.
    static std::vector<int> _collect_bound_sub_ams(const MachineObject* obj, const DevNozzle& nz)
    {
        std::vector<int> ids;
        const auto& ams_list = obj->GetFilaSystem()->GetAmsList();
        for (int bind_ams_id : nz.GetBindAms()) {
            auto it = ams_list.find(std::to_string(bind_ams_id));
            if (it == ams_list.end() || !it->second)
                continue;
            const auto ams_type = it->second->GetAmsType();
            if (ams_type != DevAmsType::N3S && ams_type != DevAmsType::N3F && ams_type != DevAmsType::AMS)
                continue;
            ids.push_back(bind_ams_id);
        }
        return ids;
    }

    // The preset only marks which position is able to host a sub-AMS; ask the machine whether one
    // is actually bound there.
    static bool _has_sub_ams_at(const MachineObject* obj, int addr_id)
    {
        if (addr_id < 0 || !obj->GetFilaSystem() || !obj->GetNozzleSystem())
            return false;

        auto nz = _query_rack_nozzle(obj->GetNozzleSystem(), addr_id + 0x10);
        if (!nz.has_value() || !nz->IsExist())
            return false;

        return !_collect_bound_sub_ams(obj, *nz).empty();
    }

    // The rack AMS mirrors the rack layout, so a nozzle's own slot is its position index.
    static std::optional<DevAmsSlotId> _find_own_rack_ams_slot(const MachineObject* obj, int pos)
    {
        auto fila_sys = obj->GetFilaSystem();
        auto rack     = obj->GetNozzleRack();
        if (!fila_sys || !rack)
            return std::nullopt;

        for (const auto& [ams_id_str, ams] : fila_sys->GetAmsList()) {
            if (!ams || ams->GetSlotCount() != rack->GetTotalRackNozzleCount())
                continue;
            const DevAmsSlotId slot{ ams->GetAmdIdInt(), pos - 0x10 };
            if (fila_sys->GetAmsTray(std::to_string(slot.first), std::to_string(slot.second)))
                return slot;
        }
        return std::nullopt;
    }

    // ZMULTI and EXT cannot unload on their own: a nozzle fed from either is locked to
    // that slot for the whole print, and a nozzle that starts on a sub-AMS must stay
    // there because coming back would need a manual unload.
    static void _collect_rack_candidates(const MachineObject*        obj,
                                         int                         sub_ams_addr,
                                         bool                        sub_ams_multi_color,
                                         std::vector<RackCandidate>& candidates)
    {
        auto  fila_sys   = obj->GetFilaSystem();
        auto* nozzle_sys = obj->GetNozzleSystem();
        auto  rack       = obj->GetNozzleRack();
        if (!fila_sys || !nozzle_sys || !rack)
            return;

        std::set<int> seen_keys;
        auto try_add = [&](int ams_id, int slot_id, DevAmsType type, const DevAmsTray* tray,
                           const DevNozzle& nz, int addr_id, bool is_ext, bool preferred) {
            if (!tray || !tray->is_tray_info_ready())
                return;
            if (!DevMappingUtil::IsSlotAutoLoadable(nz, type, DevAmsSlotId{ ams_id, slot_id }))
                return;

            RackCandidate cand;
            _parse_tray_info(ams_id, slot_id, type, *tray, cand.info);
            if (!seen_keys.insert(cand.info.id).second)
                return; // same slot reachable from multiple nozzles: keep the first

            cand.nozzle    = nz;
            cand.addr_id   = addr_id;
            cand.is_ext    = is_ext;
            cand.preferred = preferred;
            candidates.push_back(std::move(cand));
        };

        auto add_sub_ams_slots = [&](const DevNozzle& nz, int addr_id, bool preferred) {
            const auto& ams_list = fila_sys->GetAmsList();
            for (int sub_ams_id : _collect_bound_sub_ams(obj, nz)) {
                auto it = ams_list.find(std::to_string(sub_ams_id));
                if (it == ams_list.end() || !it->second)
                    continue;
                for (const auto& [slot_id_str, tray] : it->second->GetTrays()) {
                    if (!tray)
                        continue;
                    try_add(sub_ams_id, atoi(slot_id_str.c_str()), it->second->GetAmsType(), tray, nz, addr_id,
                            /*is_ext*/ false, preferred);
                }
            }
        };

        for (int pos : rack->GetTotalRackNozzlePosSet())
        {
            auto nz = _query_rack_nozzle(nozzle_sys, pos);
            if (!nz.has_value() || !nz->IsExist())
                continue;

            const int  addr_id           = nz->GetAddrId().value_or(pos - 0x10);
            const bool is_sub_ams_nozzle = (addr_id == sub_ams_addr);
            const bool is_multi_color    = is_sub_ams_nozzle && sub_ams_multi_color;

            struct BoundSlot { int ams_id; int slot_id; DevAmsType type; bool is_ext; DevAmsTray tray; };
            std::vector<BoundSlot> bounds;
            for (const auto& slot : nz->GetBindAmsSlots()) {
                if (auto vt = fila_sys->GetVirtualTray(slot.first))
                    bounds.push_back({ slot.first, 0, DevAmsType::EXT_SPOOL, true, *vt });
                else if (auto t = obj->get_tray(slot.first, slot.second))
                    bounds.push_back({ slot.first, slot.second, t->ams_type, false, *t });
            }

            auto add_bound_slots = [&](bool preferred) {
                for (const auto& b : bounds)
                    try_add(b.ams_id, b.slot_id, b.type, &b.tray, *nz, addr_id, b.is_ext, preferred);
            };

            // Fed from a slot that cannot unload itself, so it is stuck with it.
            const bool locked = nz->HasFilamentInNozzle() &&
                std::any_of(bounds.begin(), bounds.end(), [](const BoundSlot& b) {
                    return b.type == DevAmsType::ZMULTI_A || b.type == DevAmsType::ZMULTI_B ||
                           b.type == DevAmsType::EXT_SPOOL;
                });

            if (locked) {
                // Product decision, not a physical limit: deleting this branch is safe
                // and lets the colors share that one slot, the matcher's default.
                if (is_multi_color)
                    continue;

                add_bound_slots(/*preferred*/ true);
                continue;
            }

            // Opposite preferences on purpose: one color keeps the bound slot to avoid a
            // needless change, several colors need the sub-AMS to switch on its own.
            if (is_multi_color) {
                add_sub_ams_slots(*nz, addr_id, /*preferred*/ true);
                if (auto own_slot = _find_own_rack_ams_slot(obj, pos)) {
                    if (auto t = obj->get_tray(own_slot->first, own_slot->second)) {
                        DevAmsTray tray_copy = *t;
                        try_add(own_slot->first, own_slot->second, tray_copy.ams_type, &tray_copy, *nz, addr_id,
                                /*is_ext*/ false, /*preferred*/ false);
                    }
                }
            } else if (is_sub_ams_nozzle) {
                add_bound_slots(/*preferred*/ true);
                add_sub_ams_slots(*nz, addr_id, /*preferred*/ false);
            } else {
                add_bound_slots(/*preferred*/ true);
            }
        }
    }

    static bool _prefer_mapping_tie_break(bool cur_ext, bool new_ext, bool cur_id_match, bool new_id_match)
    {
        return (!cur_ext && new_ext) || (cur_ext == new_ext && !cur_id_match && new_id_match);
    }

    static void _assign_mapping_result(FilamentInfo& dst, int tray_key, const FilamentInfo& src, float distance)
    {
        dst.tray_id     = tray_key;
        dst.color       = src.color;
        dst.type        = src.type;
        dst.distance    = distance;
        dst.filament_id = src.filament_id;
        dst.setting_id  = src.setting_id;
        dst.ctype       = src.ctype;
        dst.colors      = src.colors;
        dst.ams_id      = src.ams_id;
        dst.slot_id     = src.slot_id;
    }

    static void _assign_mapping_result_from_tray(FilamentInfo&                              dst,
                                                 const std::map<int, FilamentInfo>::iterator& tray,
                                                 float                                        distance)
    {
        _assign_mapping_result(dst, tray->first, tray->second, distance);
    }

    static void _log_mapping_pick(int fila_idx, int tray_col_idx, float distance)
    {
        char buffer[256];
        ::sprintf(buffer, "ams_mapping, picked F(%02d) AMS(%02d), distance=%6.0f", fila_idx + 1, tray_col_idx + 1, distance);
        BOOST_LOG_TRIVIAL(info) << std::string(buffer);
    }

    // Hotend-rack : each slicing filament independently picks the closest-color
    // slot. Slots may be shared across filaments.
    static void _run_hotend_rack_color_mapping(const std::vector<FilamentInfo>&          filaments,
                                               const std::vector<RackCandidate>&         candidates,
                                               const std::vector<std::vector<DisValue>>& distance_map,
                                               std::vector<FilamentInfo>&                result)
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
                    const bool          cur_ext      = distance_map[i][best_j].is_ext;
                    const bool          new_ext      = dv.is_ext;
                    const FilamentInfo& cur_tray     = candidates[best_j].info;
                    const FilamentInfo& new_tray     = candidates[j].info;
                    const bool          cur_id_match = filaments[i].filament_id == cur_tray.filament_id;
                    const bool          new_id_match = filaments[i].filament_id == new_tray.filament_id;
                    if (_prefer_mapping_tie_break(cur_ext, new_ext, cur_id_match, new_id_match)
                        || (cur_ext == new_ext && _prefer_tray_by_remain(filaments[i], new_tray, cur_tray)))
                        best_j = j;
                }
            }
            if (best_j < 0)
                continue;

            _assign_mapping_result(result[i], candidates[best_j].info.id, candidates[best_j].info, min_val);
            _log_mapping_pick(i, best_j, min_val);
        }
    }

    // Kept apart from the legacy AMS path, whose flat candidate list has no room for
    // per-nozzle ownership and source preference.
    static int _run_hotend_rack_mapping(const MachineObject*             obj,
                                        const std::vector<FilamentInfo>& filaments,
                                        std::vector<FilamentInfo>&       result,
                                        bool                             check_nozzle_type,
                                        int                              sub_ams_capable_addr)
    {
        // With nothing attached that position behaves like any other one, so drop the sub-AMS
        // rules entirely rather than trust the preset.
        const bool has_sub_ams  = _has_sub_ams_at(obj, sub_ams_capable_addr);
        const int  sub_ams_addr = has_sub_ams ? sub_ams_capable_addr : -1;

        // sub_ams_addr is ams_nozzle_idx, which the slicer also uses as a logical nozzle
        // id, so it addresses filaments and rack positions alike. Only that position can
        // hold several colors; every other one is limited to what it is bound to.
        auto is_filament_on_sub_ams = [&](const FilamentInfo& fila) {
            if (sub_ams_addr < 0)
                return false;
            return std::find(fila.group_id.begin(), fila.group_id.end(), sub_ams_addr) != fila.group_id.end();
        };

        const int logical_colors = (int) std::count_if(filaments.begin(), filaments.end(), is_filament_on_sub_ams);
        const bool sub_ams_multi_color = (sub_ams_addr >= 0 && logical_colors > 1);
        BOOST_LOG_TRIVIAL(info) << "ams_mapping: rack path, sub_ams_capable_addr=" << sub_ams_capable_addr
                                << ", has_sub_ams=" << has_sub_ams
                                << ", sub_ams_addr=" << sub_ams_addr
                                << ", logical_colors=" << logical_colors;

        /////////////////////////
        // Step 2: collect filaments in machine
        std::vector<RackCandidate> candidates;
        _collect_rack_candidates(obj, sub_ams_addr, sub_ams_multi_color, candidates);
        for (const auto& cand : candidates) {
            BOOST_LOG_TRIVIAL(info) << "ams_mapping: candidate ams_id=" << cand.info.ams_id
                                    << ", slot_id=" << cand.info.slot_id << ", addr_id=" << cand.addr_id
                                    << ", " << (cand.preferred ? "preferred" : "fallback");
        }

        // The unit feeds one hotend only: its slots and the filaments grouped on that
        // position are visible to each other and to nothing else.
        auto is_visible = [&](const FilamentInfo& fila, const RackCandidate& cand) {
            if (sub_ams_addr < 0)
                return true;
            return is_filament_on_sub_ams(fila) == (cand.addr_id == sub_ams_addr);
        };

        /////////////////////////
        // Step 3: collect the distances of filaments_in_slicing to filaments_in_machine
        char buffer[256];
        std::vector<std::vector<DisValue>> distance_map;

        ::sprintf(buffer, "F(id)");
        std::string line = std::string(buffer);
        for (const auto& cand : candidates) {
            ::sprintf(buffer, "   AMS%02d", cand.info.id + 1);
            line += std::string(buffer);
        }
        BOOST_LOG_TRIVIAL(info) << "ams_mapping_distance:" << line;

        for (int i = 0; i < (int) filaments.size(); i++)
        {
            std::vector<DisValue> rol;
            ::sprintf(buffer, "F(%02d)", filaments[i].id + 1);
            line = std::string(buffer);
            for (const auto& cand : candidates)
            {
                DisValue val;
                val.tray_id = cand.info.id;
                val.is_ext  = cand.is_ext;
                if (!is_visible(filaments[i], cand)) {
                    val.distance      = kMismatchDistance;
                    val.is_type_match = false;
                } else {
                    _calc_match_distance(filaments[i], cand.info, cand.nozzle.has_value() ? &cand.nozzle.value() : nullptr,
                                         check_nozzle_type, val);
                }

                ::sprintf(buffer, "  %6.0f", val.distance);
                line += std::string(buffer);
                rol.push_back(val);
            }
            BOOST_LOG_TRIVIAL(info) << "ams_mapping_distance:" << line;
            distance_map.push_back(rol);
        }

        /////////////////////////
        // Step 4: commit each nozzle to a single source. Once ANY of its colors matches
        // in the preferred set, the fallback set is off the table for all of them.
        std::set<int> committed_addrs;
        for (int i = 0; i < (int) distance_map.size(); i++) {
            for (int j = 0; j < (int) candidates.size(); j++) {
                if (!candidates[j].preferred)
                    continue;
                const auto& dv = distance_map[i][j];
                if (dv.is_type_match && dv.is_nozzle_match)
                    committed_addrs.insert(candidates[j].addr_id);
            }
        }
        for (int j = 0; j < (int) candidates.size(); j++) {
            if (candidates[j].preferred || committed_addrs.count(candidates[j].addr_id) == 0)
                continue;
            for (auto& rol : distance_map) {
                rol[j].distance      = kMismatchDistance;
                rol[j].is_type_match = false;
            }
        }

        /////////////////////////
        // Step 5: do mapping algorithm
        for (int i = 0; i < (int) filaments.size(); i++)
        {
            FilamentInfo info;
            info.id = filaments[i].id;
            info.tray_id = -1;
            info.type = filaments[i].type;
            info.filament_id = filaments[i].filament_id;
            info.setting_id = filaments[i].setting_id;
            result.push_back(info);
        }

        _run_hotend_rack_color_mapping(filaments, candidates, distance_map, result);

        DevMappingUtil::is_valid_mapping_result(obj, result, true);
        return 0;
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

    // Every machine but the hotend rack: AMS units and external spools form one flat
    // candidate pool, filtered by map_opt.
    static int _run_legacy_ams_mapping(const MachineObject*             obj,
                                       const std::vector<FilamentInfo>& filaments,
                                       std::vector<FilamentInfo>&       result,
                                       const std::vector<bool>&         map_opt,
                                       const std::vector<int>&          exclude_id,
                                       bool                             nozzle_has_ams_then_ignore_ext,
                                       bool                             check_nozzle_type)
    {
        /////////////////////////
        // Step 1: collect filaments in machine
        std::map<int, FilamentInfo> tray_filaments; // tray_index : tray_color
        std::map<int, std::optional<DevNozzle>> tray_nozzles;
        bool  left_nozzle_has_ams = false, right_nozzle_has_ams = false;

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
                const DevNozzle* slot_nz = nullptr;
                auto nz_it = tray_nozzles.find(tray->first);
                if (nz_it != tray_nozzles.end() && nz_it->second.has_value())
                    slot_nz = &nz_it->second.value();
                _calc_match_distance(filaments[i], tray->second, slot_nz, check_nozzle_type, val);

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

    int DevMappingUtil::ams_filament_mapping(const MachineObject* obj, const std::vector<FilamentInfo>& filaments, std::vector<FilamentInfo>& result,
                                             std::vector<bool> map_opt, std::vector<int> exclude_id, bool nozzle_has_ams_then_ignore_ext, bool check_nozzle_type,
                                             int sub_ams_group_id)
    {
        if (filaments.empty() || !obj)
            return -1;

        const auto rack_type              = obj->GetNozzleRack()->GetRackType();
        const bool is_mapping_hotend_item = (rack_type == DevNozzleRack::RackType_A ||
                                             rack_type == DevNozzleRack::RackType_B);
        if (is_mapping_hotend_item)
        {
            // map_opt does not apply to the rack's per-nozzle feed.
            return _run_hotend_rack_mapping(obj, filaments, result, check_nozzle_type, sub_ams_group_id);
        }

        return _run_legacy_ams_mapping(obj, filaments, result, map_opt, exclude_id, nozzle_has_ams_then_ignore_ext, check_nozzle_type);
    }

}