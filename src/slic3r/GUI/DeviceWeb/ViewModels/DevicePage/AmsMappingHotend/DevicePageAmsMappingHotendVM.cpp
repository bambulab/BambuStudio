/*****************************************************************//**
 * \file   DevicePageAmsMappingHotendVM.cpp
 * \brief  Implementation of the hotend mapping ViewModel.
 *
 * \author xin.zhang
 * \date   May 2026
 *********************************************************************/
#include "DevicePageAmsMappingHotendVM.hpp"
#include "DevicePageAmsMappingHotendSchema.hpp"

#include "slic3r/GUI/DeviceWeb/DeviceWebBridge.hpp"
#include "slic3r/GUI/DeviceWeb/ViewModels/DevicePage/DevicePageDialogHelpers.h"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MsgDialog.hpp"

#include "slic3r/GUI/DeviceCore/DevDefs.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/DeviceCore/DevMapping.h"
#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"
#include "slic3r/GUI/DeviceCore/DevSliceContext.hpp"

#include "libslic3r/libslic3r.h"  // for is_approx (nozzle dia comparison)

#include <algorithm>
#include "libslic3r/Log.hpp"

namespace Slic3r { namespace GUI {

namespace {

std::string normalize_hex_for_web(const wxColour& col)
{
    char buf[12] = {0};
    snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X",
             col.Red(), col.Green(), col.Blue(), col.Alpha());
    return std::string(buf);
}

std::string color_type_to_string(int ctype)
{
    namespace SV = AmsMappingHotendSchema::values;
    switch (ctype) {
    case 0:  return SV::color_type::gradient;
    case 1:  return SV::color_type::multi;
    default: return SV::color_type::single;
    }
}

std::string show_type_to_string(ShowType t)
{
    namespace SV = AmsMappingHotendSchema::values;
    switch (t) {
    case ShowType::LEFT:                   return SV::show_type::left;
    case ShowType::RIGHT:                  return SV::show_type::right;
    case ShowType::LEFT_AND_RIGHT:         return SV::show_type::left_and_right;
    case ShowType::LEFT_AND_RIGHT_DYNAMIC: return SV::show_type::left_and_right_dynamic;
    }
    return SV::show_type::right;
}

std::string slot_state_to_string(HotendSlotState s)
{
    namespace SV = AmsMappingHotendSchema::values;
    switch (s) {
    case HotendSlotState::Normal:             return SV::slot_state::normal;
    case HotendSlotState::MaterialUnmatch:    return SV::slot_state::material_unmatch;
    case HotendSlotState::MaterialEmpty:      return SV::slot_state::material_empty;
    case HotendSlotState::NozzleUnmatch:      return SV::slot_state::nozzle_unmatch;
    case HotendSlotState::NozzleEmpty:        return SV::slot_state::nozzle_empty;
    case HotendSlotState::NozzleLost:         return SV::slot_state::nozzle_lost;
    case HotendSlotState::NozzleUncalibrated: return SV::slot_state::nozzle_uncalibrated;
    case HotendSlotState::NozzleError:        return SV::slot_state::nozzle_error;
    case HotendSlotState::NozzleUnknown:      return SV::slot_state::nozzle_unknown;
    }
    return SV::slot_state::normal;
}

std::string feed_block_to_string(DevSlotFeedBlock b)
{
    namespace SV = AmsMappingHotendSchema::values;
    switch (b) {
    case DevSlotFeedBlock::None:              return SV::feed_block::none;
    case DevSlotFeedBlock::NeedManualFeed:    return SV::feed_block::need_manual_feed;
    case DevSlotFeedBlock::BlockedByRackAms:  return SV::feed_block::rack_ams;
    case DevSlotFeedBlock::BlockedByExtSpool: return SV::feed_block::ext_spool;
    }
    return SV::feed_block::none;
}

// Final pickability rule for the *main* slot card. A slot is pickable only
// when fully matched (state == Normal): nozzle present, nozzle spec matches,
// filament present and material matches. This holds in every mode, including
// all-ext (external spool): a mismatched or empty external spool is no longer
// selectable, matching the rack-AMS / sub-slot behavior. The is_all_ext flag
// is kept on the signature for the caller but no longer relaxes anything.
//
// Sub-slot cards do NOT use this helper — they enforce the same strict rule
// `state == Normal` directly (see BuildSlotJson sub_slot loop).
bool ComputePickable(HotendSlotState state, bool /*is_all_ext*/)
{
    return state == HotendSlotState::Normal;
}

// Whether a slot exposes the inline edit (pencil) entry that re-uses the
// device-page AMSMaterialsSetting dialog. Editable == a third-party (non-BBL)
// spool is physically present. This mirrors the dialog's own editability gate
// `m_is_third == !IsBBL_Filament(tag_uid)` (see DevicePageDialogHelpers.cpp),
// so the pencil shows exactly when the dialog would treat the slot as an
// editable third-party tray. We must NOT use DevAmsTray::is_bbl: it is never
// populated from device data (only the AMS-level tray_is_bbl_bits is), so it
// stays false for every tray. The check is on tag_uid, NOT on TrayType:
// editing an unidentified third-party spool fills in its color / type so
// MakeTrayData flips it from THIRD to NORMAL, yet it is still a third-party
// spool and must keep the pencil. EMPTY slots have no spool to edit.
bool ComputeCanEdit(MachineObject* obj, const TrayData& td)
{
    if (!obj || td.type == EMPTY) return false;
    // External spool (virtual slot) is always editable: it has no physical AMS
    // bay behind it, so the user can always (re-)assign its filament info even
    // when the device reports it as a BBL spool.
    if (td.ams_type == DevAmsType::EXT_SPOOL) return true;
    auto tray_opt = obj->get_tray(td.ams_id, td.slot_id);
    if (!tray_opt.has_value()) return false;
    return !DevFilaSystem::IsBBL_Filament(tray_opt->tag_uid);
}

// Display remain (%) for the spool ring. A present spool (slot is not EMPTY)
// whose reported remain is below 5% — including an unknown 0 — is floored to
// 5% so its color arc stays visible; otherwise a 0% spool renders as a blank
// ring even though its color is known. The web layer only renders this value.
// `out_of_range_fallback` keeps the legacy behaviour for malformed inputs.
int ComputeDisplayRemain(const TrayData& td, int out_of_range_fallback)
{
    // Unknown weight (remain_g == -1, e.g. third-party spool): show as full.
    if (td.type != EMPTY && td.remain_g == -1) return 100;
    int remain = (td.remain >= 0 && td.remain <= 100) ? td.remain : out_of_range_fallback;
    if (td.type != EMPTY && remain < 5) remain = 5;
    return remain;
}

} // namespace

nlohmann::json DevicePageAmsMappingHotendVM::OnCommand(const std::string& submod,
                                                       const std::string& action,
                                                       const nlohmann::json& payload)
{
    try {
        if (submod == "state" && (action == "get" || action == "init")) {
            return MakeResponse(GetModule(), submod, action, 0, "", BuildState());
        }
        if (submod == "action") {
            return HandleAction(action, payload);
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "exception: " << e.what();
        return MakeResponse(GetModule(), submod, action, 2, e.what(), BuildState());
    }

    return MakeResponse(GetModule(), submod, action, 1,
                        "unknown device page AMS mapping hotend command", BuildState());
}

void DevicePageAmsMappingHotendVM::ReportState(const std::string& submod, const std::string& action)
{
    ReportStateImpl(submod, action, /*force=*/false);
}

void DevicePageAmsMappingHotendVM::ReportStateImpl(const std::string& submod, const std::string& action, bool force)
{
    if (!m_bridge) return;
    nlohmann::json state = BuildState();
    // Device data may contain non-UTF-8 bytes; strict dump() throws
    // type_error.316. Skip this report instead of letting it crash the page.
    std::string state_key;
    try {
        state_key = state.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "state.dump() failed: " << e.what();
        return;
    }
    if (state_key == m_last_report_state) {
        return;
    }

    m_last_report_state = state_key;
    if (force) {
        m_bridge->ReportMsgForce(MakeResponse(GetModule(), submod, action, 0, "", state));
    } else {
        m_bridge->ReportMsg(MakeResponse(GetModule(), submod, action, 0, "", state));
    }
}

void DevicePageAmsMappingHotendVM::OnSysColorChanged()
{
    ReportState("state", "theme_changed");
}

void DevicePageAmsMappingHotendVM::Refresh(MachineObject*                       obj,
                                           const std::vector<FilamentInfo>&     mapping_result,
                                           int                                  current_filament_id,
                                           ShowType                             show_type,
                                           bool                                 ext_mapping_filatype_check,
                                           bool                                 match_all,
                                           const std::string&                   material_index,
                                           const ResetCallback&                 reset_cb,
                                           wxWindow*                            send_win,
                                           AmsMapingPopup*                      popup,
                                           std::optional<PrintFromType>         print_from_type)
{
    m_obj                        = obj;
    m_mapping_result             = mapping_result;
    m_current_filament_id        = current_filament_id;
    m_show_type                  = show_type;
    m_ext_mapping_filatype_check = ext_mapping_filatype_check;
    m_match_all                  = match_all;
    m_material_index             = material_index;
    m_reset_callback             = reset_cb;
    m_send_win                   = send_win;
    m_popup                      = popup;
    m_print_from_type            = print_from_type;

    // Bump the session counter so the Web side can tell apart "data refreshed
    // while popup stayed open" from "popup just (re-)opened" and reset its
    // sub-page navigation accordingly.
    ++m_session_seq;

    // Force-push the latest state to the web side. Skip diff suppression
    // here because external context (e.g. current_filament_id flipping)
    // can produce identical JSON yet still requires a refresh on selection.
    //
    // force=true bypasses the host visibility gate: Refresh() runs before the
    // popup window is shown, so a gated report would be dropped (and would
    // poison m_last_report_state). Pushing into the still-hidden but alive
    // WebView lets it pre-render the new machine's data; the popup waits for
    // the web "rendered" ack before showing, eliminating the switch flicker.
    m_last_report_state.clear();
    ReportStateImpl("state", "refresh", /*force=*/true);
}

std::pair<int, int> DevicePageAmsMappingHotendVM::RemapMainCardSlot(MachineObject*             obj,
                                                                    const std::pair<int, int>& original_slot,
                                                                    int                        nozzle_pos)
{
    if (!obj) return original_slot;
    auto fila_sys = obj->GetFilaSystem();
    if (!fila_sys) return original_slot;

    const int ams_id = original_slot.first;

    // External spool: keep as-is. The caller still normalizes slot_id to 0
    // for virtual trays, so we don't have to touch slot_id here.
    if (fila_sys->GetVirtualTray(ams_id).has_value()) return original_slot;

    const auto& ams_list = fila_sys->GetAmsList();
    auto it = ams_list.find(std::to_string(ams_id));
    if (it == ams_list.end() || !it->second) return original_slot;

    const auto at = it->second->GetAmsType();
    if (at == DevAmsType::ZMULTI_A || at == DevAmsType::ZMULTI_B) {
        return original_slot;
    }

    // Anything else (N3S / N3F / AMS sub-units …) -> remap to the
    // rack-aligned ZMULTI slot for THIS hotend pos. Even when that ZMULTI
    // slot is empty (is_exists=false) the tray is still in DevAms::m_trays,
    // so MakeTrayData marks it type=EMPTY and BuildSlotJson renders "-".
    for (const auto& kv : ams_list) {
        if (!kv.second) continue;
        const auto kvat = kv.second->GetAmsType();
        if (kvat == DevAmsType::ZMULTI_A || kvat == DevAmsType::ZMULTI_B) {
            return std::make_pair(kv.second->GetAmdIdInt(), nozzle_pos - 0x10);
        }
    }

    // No ZMULTI on this rack at all -> leave unchanged (rare).
    return original_slot;
}

nlohmann::json DevicePageAmsMappingHotendVM::BuildState() const
{
    namespace SK = AmsMappingHotendSchema::keys;

    nlohmann::json data;
    data[SK::root::selected_dev_id]     = "";
    data[SK::root::current_filament_id] = m_current_filament_id;
    data[SK::root::show_type]           = show_type_to_string(m_show_type);
    data[SK::root::material_index]      = m_material_index;
    data[SK::root::reset_visible]       = true;
    data[SK::root::session_seq]         = m_session_seq;
    // Report Studio's UI theme so the web popup chrome can follow light/dark
    // (mirrors FilamentManagerVM). Only the chrome reacts on the web side; the
    // filament color content is never themed.
    data[SK::root::theme]               = (wxGetApp().app_config->get("dark_color_mode") == "1") ? "dark" : "light";
    data[SK::root::hotend_slots]        = nlohmann::json::array();
    data[SK::root::sub_ams_groups]      = nlohmann::json::object();

    auto* obj = m_obj;
    if (!obj) {
        return data;
    }

    data[SK::root::selected_dev_id] = obj->get_dev_id();

    // Slicing nozzle requirement for the current filament. The slice context handed over
    // by the send-to-print dialog is the only source; without it nozzle_match is skipped.
    ExpectedNozzleSpec expected_nozzle;
    if (m_slice_context && obj->GetNozzleRack()->IsSupported()) {
        expected_nozzle = ResolveExpectedNozzleSpecFromSliceContext(m_current_filament_id, *m_slice_context);
    }

    // Determine whether a ZMULTI (internal AMS) is present.
    const auto& ams_list = obj->GetFilaSystem()->GetAmsList();
    bool has_zmulti = false;
    for (const auto& kv : ams_list) {
        if (!kv.second) continue;
        auto at = kv.second->GetAmsType();
        if (at == DevAmsType::ZMULTI_A || at == DevAmsType::ZMULTI_B) {
            has_zmulti = true;
            break;
        }
    }

    // Collect TrayData via rack nozzle binding + unified get_tray query.
    // This ensures both AMS and external-spool (virtual tray) data are
    // captured, matching the approach used by DevicePageAmsFilamentHotendVM.
    std::vector<TrayData> hotend_trays;
    std::vector<std::optional<std::pair<int, int>>> hotend_preload_slots;
    // Track the rack-position id alongside each tray so BuildSlotJson can
    // resolve the parent rack nozzle directly via GetNozzleOptByPosId(...)
    // without falling back to DevAmsTray::get_nozzle_pos_id (which returns
    // nullopt for AMS / N3F / N3S sub-trays bound directly to a rack — that
    // path used to drop the nozzle row + +N badge + sub-AMS list).
    std::vector<int> hotend_nozzle_positions;
    if (obj->GetNozzleRack()->IsSupported()) {
        std::vector<int> nozzle_positions;
        std::map<int, std::optional<std::pair<int, int>>> nozzle_slots;
        std::map<int, std::optional<std::pair<int, int>>> preload_slots;
        const auto& total_nozzles = obj->GetNozzleRack()->GetTotalRackNozzlePosSet();
        for (const auto& nozzle_pos : total_nozzles) {
            // Resolve by stable addr_id first: when this rack nozzle is picked up
            // by an extruder its id becomes 0, so an addr lookup stays correct.
            // Fall back to a pos-based lookup so the parser-filled none-exist rack
            // placeholders (which carry no addr_id but still hold the feed binding
            // from sTraverseBindAmsSlot) are not dropped — that drop was the root
            // cause of empty rack positions silently vanishing from the popup.
            auto rack_nozzle_opt = obj->GetNozzleSystem()->GetNozzleOptByAddrId(nozzle_pos - 0x10);
            if (!rack_nozzle_opt.has_value()) {
                rack_nozzle_opt = obj->GetNozzleSystem()->GetNozzleOptByPosId(nozzle_pos);
            }
            if (!rack_nozzle_opt.has_value()) {
                nozzle_slots[nozzle_pos] = std::nullopt;
                preload_slots[nozzle_pos] = std::nullopt;
                nozzle_positions.push_back(nozzle_pos);
                continue;
            }
            const auto& ams_slots = rack_nozzle_opt->GetBindAmsSlots();
            if (!ams_slots.empty()) {
                // Force the main card to a rack-aligned ZMULTI / external-spool
                // slot when the device-side feed binding hits a sub-AMS. Sub-AMS
                // info still surfaces via parent_nz->GetBindAms() in the popup.
                auto remapped = RemapMainCardSlot(obj, ams_slots[0], nozzle_pos);
                int ams_id  = remapped.first;
                int slot_id = remapped.second;
                if (ams_id == 0 && slot_id == 0) {
                    ams_id = 32;
                    slot_id = 4;
                }
                if (obj->GetFilaSystem()->GetVirtualTray(ams_id).has_value()) {
                    slot_id = 0;
                }
                nozzle_slots[nozzle_pos] = std::make_pair(ams_id, slot_id);
            } else {
                nozzle_slots[nozzle_pos] = std::nullopt;
            }

            const auto& preload_ams_slots = rack_nozzle_opt->GetPreloadBindAmsSlots();
            if (!preload_ams_slots.empty()) {
                preload_slots[nozzle_pos] =
                    std::make_pair(preload_ams_slots[0].first, preload_ams_slots[0].second);
            } else {
                preload_slots[nozzle_pos] = std::nullopt;
            }
            nozzle_positions.push_back(nozzle_pos);
        }

        std::sort(nozzle_positions.begin(), nozzle_positions.end(), [](int a, int b) {
            auto idx = [](int p) { return p >= 0x10 ? (p - 0x10 + 1) : (p + 1); };
            return idx(a) < idx(b);
        });

        for (int nozzle_pos : nozzle_positions) {
            auto& slot_opt = nozzle_slots[nozzle_pos];

            // Slot display rule (per product spec):
            //   * no binding (m_bind_ams_slots empty)                   -> EMPTY.
            //   * AMS slot, feed-hall absent (tray_exist_bits bit = 0)  -> EMPTY.
            //   * AMS slot, feed-hall present + readable info           -> NORMAL.
            //   * AMS slot, feed-hall present + no readable info        -> THIRD ("?").
            //   * external spool / virtual slot                        -> "always
            //     loaded": NORMAL when readable, else THIRD ("?"), never EMPTY.
            // m_bind_ams_slots can stay non-empty on an N1x AMS even when the bay
            // has no filament fed, so the feed-hall presence bit (tray_exist_bits)
            // is the authoritative loaded/empty gate for AMS slots. It is read via
            // DevAms::IsAmsSlotPreLoadFilament, which works even when no tray object
            // exists for an empty bay (the case where get_tray returns nullopt).
            std::optional<TrayData> td_opt;
            if (slot_opt.has_value()) {
                DevAms* ams_ptr = nullptr;
                std::string ams_id_str = std::to_string(slot_opt->first);
                const bool is_vt = obj->GetFilaSystem()->GetVirtualTray(slot_opt->first).has_value();
                if (!is_vt) {
                    auto it = ams_list.find(ams_id_str);
                    if (it != ams_list.end()) ams_ptr = it->second;
                }

                // External-spool / virtual slots are always considered loaded;
                // AMS slots are gated by the feed-hall presence bit.
                const bool ams_loaded =
                    is_vt ? true
                          : (ams_ptr ? ams_ptr->IsAmsSlotPreLoadFilament(slot_opt->second) : false);

                if (!is_vt && !ams_loaded) {
                    // Physically empty AMS bay: render an AMS-typed EMPTY card so
                    // the slot keeps its real identity (do NOT fall through to "?").
                    TrayData empty_td;
                    empty_td.type     = EMPTY;
                    empty_td.colour   = wxColour(0xCE, 0xCE, 0xCE);
                    empty_td.ams_id   = slot_opt->first;
                    empty_td.slot_id  = slot_opt->second;
                    empty_td.ams_type = ams_ptr ? ams_ptr->GetAmsType() : DevAmsType::AMS;
                    empty_td.id       = ams_ptr ? ams_ptr->GetTrayId(empty_td.slot_id)
                                                : empty_td.slot_id;
                    td_opt = empty_td;
                } else {
                    if (auto opt_tray = obj->get_tray(slot_opt->first, slot_opt->second)) {
                        DevAmsTray tray_copy = *opt_tray;
                        td_opt = MakeTrayData(obj, ams_ptr, &tray_copy, ams_id_str);
                    }

                    // Loaded but no usable tray data: show as third-party "?"
                    // instead of an empty card. Covers a loaded bay whose tray
                    // object is missing (get_tray miss -> MakeTrayData nullopt) or
                    // carries no readable filament info (MakeTrayData EMPTY).
                    if (!td_opt.has_value() || td_opt->type == EMPTY) {
                        TrayData third_td;
                        third_td.type    = THIRD;
                        third_td.colour  = wxColour(0xCE, 0xCE, 0xCE);
                        third_td.ams_id  = slot_opt->first;
                        if (is_vt) {
                            third_td.ams_type = DevAmsType::EXT_SPOOL;
                            third_td.slot_id  = 0;
                            third_td.id       = third_td.ams_id;
                        } else {
                            third_td.slot_id  = slot_opt->second;
                            third_td.ams_type = ams_ptr ? ams_ptr->GetAmsType() : DevAmsType::AMS;
                            third_td.id       = ams_ptr ? ams_ptr->GetTrayId(third_td.slot_id)
                                                        : third_td.slot_id;
                        }
                        td_opt = third_td;
                    }
                }
            }

            // No binding at all (m_bind_ams_slots empty): the only case that
            // renders an EMPTY placeholder. We still keep the card (never drop a
            // rack position) so the popup mirrors the device page.
            if (!td_opt.has_value()) {
                TrayData empty_td;
                empty_td.type     = EMPTY;
                empty_td.colour   = wxColour(0xCE, 0xCE, 0xCE);
                empty_td.ams_id   = 255 - nozzle_pos + 16; // external-spool id for this pos
                empty_td.slot_id  = 0;
                empty_td.ams_type = DevAmsType::EXT_SPOOL;
                empty_td.id       = empty_td.ams_id;
                td_opt = empty_td;
            }

            hotend_trays.push_back(td_opt.value());
            hotend_preload_slots.push_back(preload_slots[nozzle_pos]);
            hotend_nozzle_positions.push_back(nozzle_pos);
        }
    } else if (!has_zmulti) {
        hotend_trays = CollectVirtualTrayData(obj);
        hotend_preload_slots.assign(hotend_trays.size(), std::nullopt);
        // Without a rack, BuildSlotJson takes the legacy non-rack path; the
        // -1 sentinel keeps GetNozzleOptByPosId(...) a noop on that branch.
        hotend_nozzle_positions.assign(hotend_trays.size(), -1);
    }

    const bool is_all_ext = !has_zmulti;
    data[SK::root::is_all_ext] = is_all_ext;

    // Build per-slot JSON in the original index order.
    for (size_t i = 0; i < hotend_trays.size(); ++i) {
        const auto& preload_slot = (i < hotend_preload_slots.size())
                                       ? hotend_preload_slots[i]
                                       : std::optional<std::pair<int, int>>{};
        const int rack_nozzle_pos = (i < hotend_nozzle_positions.size())
                                       ? hotend_nozzle_positions[i]
                                       : -1;
        data[SK::root::hotend_slots].push_back(
            BuildSlotJson(obj, hotend_trays[i], (int)i + 1, rack_nozzle_pos,
                          expected_nozzle, is_all_ext, preload_slot));
    }

    return data;
}

std::vector<TrayData> DevicePageAmsMappingHotendVM::CollectZMultiTrayData(MachineObject* obj, DevAms* ams_group) const
{
    std::vector<TrayData> trays;
    if (!ams_group) return trays;
    for (auto tray_iter = ams_group->GetTrays().cbegin();
         tray_iter != ams_group->GetTrays().cend(); ++tray_iter) {
        const auto& td_opt = MakeTrayData(obj, ams_group, tray_iter->second, ams_group->GetAmsId());
        if (td_opt.has_value()) {
            trays.push_back(td_opt.value());
        }
    }
    return trays;
}

std::vector<TrayData> DevicePageAmsMappingHotendVM::CollectVirtualTrayData(MachineObject* obj) const
{
    std::vector<TrayData> trays;
    if (!obj) return trays;
    const auto& vir_list = obj->GetFilaSystem()->GetVirtualTrays();
    for (auto iter = vir_list.rbegin(); iter != vir_list.rend(); ++iter) {
        DevAmsTray tray = iter->second;
        const auto& td_opt = MakeTrayData(obj, nullptr, &tray, tray.id);
        if (td_opt.has_value()) {
            trays.push_back(td_opt.value());
        }
    }
    return trays;
}

SlotJudgement DevicePageAmsMappingHotendVM::ResolveSlotJudgement(MachineObject*                  obj,
                                                                  const TrayData&                 td,
                                                                  const ExpectedNozzleSpec&       expected_nozzle,
                                                                  const std::optional<DevNozzle>& override_nz) const
{
    SlotJudgement j;

    auto is_match = [this](const std::string& mat) {
        // Mirrors AmsMapingPopup::is_match_material via the cached material
        // index passed in via Refresh().
        return m_material_index.empty() ? true : (m_material_index == mat);
    };

    // ---- Step 1: collect atomic facts from a single rack-nozzle query ----
    // Prefer the caller-provided nozzle (used for sub-AMS slots, which share
    // their parent hotend's rack nozzle rather than carrying a direct
    // binding). Fall back to per-slot query for the main hotend card.
    auto nz = override_nz.has_value() ? override_nz : QueryRackNozzleAt(obj, td);

    j.has_nozzle    = nz.has_value() && nz->IsExist();
    j.has_filament  = (td.type != EMPTY);

    // THIRD-party trays don't expose a usable filament_type; treat them as
    // material-match by default so we don't false-positive a mismatch. match_all
    // lifts both the type and the nozzle diameter / flow restriction, so every
    // present filament on a usable nozzle can be picked (see the nozzle gate below).
    j.material_match = m_match_all || (td.type != NORMAL) || is_match(td.filament_type);

    // Nozzle dia / flow gate. Left as a match when the slicing spec is unknown,
    // the rack nozzle is unresolved, or match_all is set (the sync-all dialog lets
    // any slot be picked regardless of diameter). A cutter / pen still fails: it
    // can never hold a filament, whatever the mode.
    j.nozzle_match = true;
    if (j.has_nozzle && !nz->IsHotend()) {
        j.nozzle_match = false;
    } else if (!m_match_all && j.has_nozzle && expected_nozzle.valid) {
        if (!is_approx(nz->GetNozzleDiameter(), expected_nozzle.diameter)) {
            j.nozzle_match = false;
        } else if (expected_nozzle.flow != NozzleFlowType::NONE_FLOWTYPE &&
                   nz->GetNozzleFlowType() != expected_nozzle.flow) {
            j.nozzle_match = false;
        }
    }

    const auto& rack           = obj ? obj->GetNozzleRack() : nullptr;
    const bool  nozzle_lost    = nz.has_value() && nz->IsToolLostInHolder();
    const bool  docking_uncali = rack && rack->IsDirectConnectedRack() && j.has_nozzle &&
                                 nz->GetAddrId().has_value() &&
                                 !rack->IsHotendDockingCalibrated(*nz->GetAddrId());

    // ---- Step 2: project facts to state via priority chain ----
    // Order: hardware > config > content. Earliest match wins.
    if (nozzle_lost)             j.state = HotendSlotState::NozzleLost;
    else if (!j.has_nozzle)      j.state = HotendSlotState::NozzleEmpty;
    else if (nz->IsAbnormal())   j.state = HotendSlotState::NozzleError;
    else if (nz->IsUnknown())    j.state = HotendSlotState::NozzleUnknown;
    else if (docking_uncali)     j.state = HotendSlotState::NozzleUncalibrated;
    else if (!j.nozzle_match)    j.state = HotendSlotState::NozzleUnmatch;
    else if (!j.has_filament)    j.state = HotendSlotState::MaterialEmpty;
    else if (!j.material_match)  j.state = HotendSlotState::MaterialUnmatch;
    else                         j.state = HotendSlotState::Normal;

    // is_pickable is left at its default (false) — callers must decide
    // pickability against the global mode (main slot uses ComputePickable
    // with all-ext relaxation; sub slot uses strict Normal-only rule).
    return j;
}

std::vector<SubAmsGroup> DevicePageAmsMappingHotendVM::CollectSubAmsGroups(MachineObject*                  obj,
                                                                          const TrayData&                 self_td,
                                                                          const std::optional<DevNozzle>& parent_nz) const
{
    std::vector<SubAmsGroup> groups;
    if (!obj || !obj->GetNozzleRack()->IsSupported()) return groups;

    // The caller pre-resolved the parent rack nozzle from a known rack pos id;
    // we deliberately do NOT round-trip through `self_td`'s `get_nozzle_pos_id`
    // here because that helper returns nullopt for AMS / N3F / N3S sub-trays
    // bound directly to a rack nozzle (see BuildSlotJson comment).
    if (!parent_nz.has_value()) return groups;

    // The self tray (whatever it points at — ext spool or AMS sub-slot) is
    // rendered as the parent card on the left of the SubAmsPanel; the groups
    // below are the *other* AMS units bound to the same rack nozzle.
    (void)self_td;

    const auto& ams_list = obj->GetFilaSystem()->GetAmsList();
    for (int bind_ams_id : parent_nz->GetBindAms()) {
        std::string bind_ams_id_str = std::to_string(bind_ams_id);
        auto ams_it = ams_list.find(bind_ams_id_str);
        if (ams_it == ams_list.end()) continue;
        auto at = ams_it->second->GetAmsType();
        if (at != DevAmsType::N3S && at != DevAmsType::N3F && at != DevAmsType::AMS) continue;

        SubAmsGroup g;
        g.name = ams_it->second->GetDisplayName();
        for (auto ti = ams_it->second->GetTrays().cbegin();
             ti != ams_it->second->GetTrays().cend(); ++ti) {
            auto td_opt = MakeTrayData(obj, ams_it->second, ti->second, bind_ams_id_str);
            if (td_opt.has_value()) g.trays.push_back(td_opt.value());
        }
        if (!g.trays.empty()) groups.push_back(g);
    }

    if (groups.empty()) return {};
    return groups;
}

nlohmann::json DevicePageAmsMappingHotendVM::BuildSlotJson(MachineObject*                            obj,
                                                            const TrayData&                           td,
                                                            int                                       slot_index,
                                                            int                                       rack_nozzle_pos,
                                                            const ExpectedNozzleSpec&                 expected_nozzle,
                                                            bool                                      is_all_ext,
                                                            const std::optional<std::pair<int, int>>& preload_slot) const
{
    // Resolve the parent rack nozzle from the known rack position handed in
    // by BuildState. We must NOT round-trip through `tray->get_nozzle_pos_id`
    // here: that helper drops to the unimplemented TODO branch when the bound
    // tray belongs to a real AMS sub-unit (DevAmsType::AMS / N3F / N3S), so
    // the nozzle row, +N badge and sub-AMS list would silently disappear for
    // that hotend slot — even though the wx popup and the hotend page both
    // had enough information to render them.
    auto parent_nz = (obj && rack_nozzle_pos >= 0x10) ? obj->GetNozzleSystem()->GetNozzleOptByAddrId(rack_nozzle_pos - 0x10)
                         : std::nullopt;

    // Pre-compute sub-AMS groups so the state resolver knows whether to
    // grant the slot the "selectable via expand" override.
    std::vector<SubAmsGroup> sub_groups = CollectSubAmsGroups(obj, td, parent_nz);

    // Preload (pre-feed) AMS slot: when it differs from the current bind,
    // expose the AMS slot as a synthetic sub-AMS group so the sub-page shows
    // both the current feed source (parent) and the preloaded AMS slot.
    //
    // Selectability of the preload card is decided together with every other
    // sub-AMS slot in the sub-slot loop below, where `is_pickable` is finalized
    // against the slot's DevSlotFeedBlock.
    if (obj && preload_slot.has_value() &&
        *preload_slot != std::make_pair(td.ams_id, td.slot_id)) {
        auto preload_tray = obj->get_tray(preload_slot->first, preload_slot->second);
        if (preload_tray.has_value()) {
            DevAmsTray pt = *preload_tray;
            std::string ams_id_str = std::to_string(preload_slot->first);
            DevAms* ams_ptr = nullptr;
            const auto& ams_list = obj->GetFilaSystem()->GetAmsList();
            auto it = ams_list.find(ams_id_str);
            if (it != ams_list.end()) ams_ptr = it->second;
            auto td_opt = MakeTrayData(obj, ams_ptr, &pt, ams_id_str);
            if (td_opt.has_value()) {
                SubAmsGroup g;
                g.name = ams_ptr ? ams_ptr->GetDisplayName() : wxString("AMS");
                g.trays.push_back(td_opt.value());
                g.is_preload = true;
                sub_groups.insert(sub_groups.begin(), g);
            }
        }
    }

    // `parent_nz` was resolved up-front from the rack-position id we received
    // from BuildState (see top of this function); reuse it for the slot
    // judgement and for every sub-AMS slot below — sub-AMS trays don't carry
    // a direct nozzle pos binding, they inherit from the parent hotend.
    SlotJudgement j = ResolveSlotJudgement(obj, td, expected_nozzle, parent_nz);
    // Main card pickability: strictly Normal-only in every mode (external
    // spool included) — see ComputePickable.
    j.is_pickable = ComputePickable(j.state, is_all_ext);

    wxColour display_color =
        (td.type == NORMAL) ? td.colour : wxColour(0xCE, 0xCE, 0xCE);

    wxString display_name;
    if (td.type == THIRD)      display_name = "?";
    else if (td.type == EMPTY) display_name = "-";
    else                       display_name = td.name;

    namespace SK = AmsMappingHotendSchema::keys;

    nlohmann::json slot;
    slot[SK::slot::ams_id]          = std::to_string(td.ams_id);
    slot[SK::slot::slot_id]         = std::to_string(td.slot_id);
    slot[SK::slot::tray_id]         = td.id;
    slot[SK::slot::index]           = std::to_string(slot_index);
    slot[SK::slot::is_top_tier]     = (slot_index % 2) == 0;
    slot[SK::slot::ext]             = (td.ams_type == DevAmsType::EXT_SPOOL);
    slot[SK::slot::state]           = slot_state_to_string(j.state);
    slot[SK::slot::filament_type]   = td.filament_type;
    slot[SK::slot::filament_name]   = display_name.ToUTF8().data();
    slot[SK::slot::color]           = normalize_hex_for_web(display_color);
    slot[SK::slot::ctype]           = td.ctype;
    slot[SK::slot::color_type]      = color_type_to_string(td.ctype);
    slot[SK::slot::remain]          = ComputeDisplayRemain(td, 0);
    slot[SK::slot::has_filament]    = j.has_filament;
    slot[SK::slot::is_pickable]     = j.is_pickable;
    slot[SK::slot::can_edit]        = ComputeCanEdit(obj, td);
    slot[SK::slot::checked]         = false;

    // Derived from the feed path, not from `td.ams_type`: sTraverseBindAmsSlot
    // falls back to the external slot when nothing is fed, and that must be silent.
    const DevSlotFeedBlock hotend_feed_block =
        parent_nz.has_value() ? DevMappingUtil::QueryNozzleOccupantBlock(*parent_nz)
                              : DevSlotFeedBlock::None;
    slot[SK::slot::hotend_feed_block] = feed_block_to_string(hotend_feed_block);

    // Mark the checked slot if it appears in the current mapping result.
    for (const auto& mapping_res : m_mapping_result) {
        if (mapping_res.id == m_current_filament_id &&
            mapping_res.ams_id == std::to_string(td.ams_id) &&
            mapping_res.slot_id == std::to_string(td.slot_id)) {
            slot[SK::slot::checked] = true;
            break;
        }
    }

    nlohmann::json multi_colors = nlohmann::json::array();
    for (const auto& c : td.material_cols) {
        multi_colors.push_back(normalize_hex_for_web(c));
    }
    slot[SK::slot::multi_colors] = multi_colors;

    // Per-slot nozzle info row. Use the rack nozzle resolved from the known
    // rack position rather than reverse-deriving via `tray->get_nozzle_pos_id`,
    // which fails on AMS sub-slots bound directly to a hotend rack.
    const bool keep_lost_spec = parent_nz.has_value() && parent_nz->IsToolLostInHolder() &&
                                parent_nz->GetNozzleDiameter() > 0.f;
    if (parent_nz.has_value() && (parent_nz->IsExist() || keep_lost_spec)) {
        namespace SV = AmsMappingHotendSchema::values;
        wxString dia = wxString::Format("%.1f mm", parent_nz->GetNozzleDiameter());
        wxString flow = wxString::FromUTF8(
            DevNozzle::ToNozzleVolumeString(
                DevNozzle::ToNozzleVolumeType(parent_nz->GetNozzleFlowType())));
        const char* tool_kind = SV::tool_kind::nozzle;
        switch (parent_nz->GetToolKind()) {
        case DevNozzle::NozzleToolKind::Cutter: tool_kind = SV::tool_kind::cutter; break;
        case DevNozzle::NozzleToolKind::Pen:    tool_kind = SV::tool_kind::pen;    break;
        default:                                tool_kind = SV::tool_kind::nozzle; break;
        }
        slot[SK::slot::nozzle] = {
            {SK::nozzle::dia,       dia.ToUTF8().data()},
            {SK::nozzle::flow_type, flow.ToUTF8().data()},
            {SK::nozzle::tool_kind, tool_kind}
        };
    }

    // Sub-AMS groups payload (mirrors the popup's expand panel).
    nlohmann::json sub_ams_arr = nlohmann::json::array();
    for (const auto& g : sub_groups) {
        nlohmann::json group_json;
        group_json[SK::sub_ams_group::name]       = g.name.ToUTF8().data();
        group_json[SK::sub_ams_group::is_preload] = g.is_preload;
        group_json[SK::sub_ams_group::slots]      = nlohmann::json::array();

        int sub_index = 1;
        for (const auto& sub_td : g.trays) {
            // Sub-slot reuses the same resolver but applies a stricter
            // pickable rule (only Normal is pickable). All-ext relaxation
            // does NOT propagate to sub-slots — once the user enters the
            // sub-AMS expand they're picking a specific filament, so we
            // refuse to silently accept a mismatch.
            //
            // Pass the parent hotend's rack nozzle so the judgement uses
            // the right nozzle constraint (sub-AMS trays don't carry a
            // direct rack-nozzle binding and would otherwise be classified
            // as NozzleEmpty).
            SlotJudgement sj = ResolveSlotJudgement(obj, sub_td, expected_nozzle, parent_nz);
            sj.is_pickable = (sj.state == HotendSlotState::Normal);

            // Shared with the auto-mapping collector so the popup never offers
            // what auto mapping refuses, and vice versa.
            const DevSlotFeedBlock feed_block =
                parent_nz.has_value()
                    ? DevMappingUtil::QuerySlotFeedBlock(*parent_nz, sub_td.ams_type,
                                                         DevAmsSlotId{sub_td.ams_id, sub_td.slot_id})
                    : DevSlotFeedBlock::None;

            // Only an engaged external spool locks the card, since it cannot be
            // retracted at all. Locking also drops any stale checked mark.
            const bool feed_locked = (feed_block == DevSlotFeedBlock::BlockedByExtSpool);
            if (feed_locked) sj.is_pickable = false;

            wxColour sub_col = (sub_td.type == NORMAL) ? sub_td.colour : wxColour(0xCE, 0xCE, 0xCE);
            wxString sub_name;
            if (sub_td.type == THIRD)      sub_name = "?";
            else if (sub_td.type == EMPTY) sub_name = "-";
            else                           sub_name = sub_td.name;

            nlohmann::json sub_slot;
            sub_slot[SK::slot::ams_id]         = std::to_string(sub_td.ams_id);
            sub_slot[SK::slot::slot_id]        = std::to_string(sub_td.slot_id);
            sub_slot[SK::slot::tray_id]        = sub_td.id;
            // A preload card is another feed source for the *same* hotend
            // position as the main card, so it carries the hotend index (which
            // the send page also shows as the mapping label). Real sub-AMS
            // slots keep their ordinal inside their own AMS unit.
            sub_slot[SK::slot::index]          = std::to_string(g.is_preload ? slot_index : sub_index);
            ++sub_index;
            sub_slot[SK::slot::ext]            = (sub_td.ams_type == DevAmsType::EXT_SPOOL);
            sub_slot[SK::slot::state]          = slot_state_to_string(sj.state);
            sub_slot[SK::slot::filament_type]  = sub_td.filament_type;
            sub_slot[SK::slot::filament_name]  = sub_name.ToUTF8().data();
            sub_slot[SK::slot::color]          = normalize_hex_for_web(sub_col);
            sub_slot[SK::slot::ctype]          = sub_td.ctype;
            sub_slot[SK::slot::color_type]     = color_type_to_string(sub_td.ctype);
            sub_slot[SK::slot::remain]         = ComputeDisplayRemain(sub_td, 100);
            sub_slot[SK::slot::has_filament]   = sj.has_filament;
            sub_slot[SK::slot::is_pickable]    = sj.is_pickable;
            sub_slot[SK::slot::can_edit]       = ComputeCanEdit(obj, sub_td);
            sub_slot[SK::slot::feed_block]     = feed_block_to_string(feed_block);
            sub_slot[SK::slot::checked]        = false;
            if (!feed_locked) {
                for (const auto& mapping_res : m_mapping_result) {
                    if (mapping_res.id == m_current_filament_id &&
                        mapping_res.ams_id == std::to_string(sub_td.ams_id) &&
                        mapping_res.slot_id == std::to_string(sub_td.slot_id)) {
                        sub_slot[SK::slot::checked] = true;
                        break;
                    }
                }
            }
            nlohmann::json sub_multi = nlohmann::json::array();
            for (const auto& c : sub_td.material_cols) {
                sub_multi.push_back(normalize_hex_for_web(c));
            }
            sub_slot[SK::slot::multi_colors] = sub_multi;

            group_json[SK::sub_ams_group::slots].push_back(sub_slot);
        }

        sub_ams_arr.push_back(group_json);
    }
    slot[SK::slot::sub_ams] = sub_ams_arr;

    return slot;
}

nlohmann::json DevicePageAmsMappingHotendVM::HandleAction(const std::string& action,
                                                          const nlohmann::json& payload)
{
    if (action == "select") {
        PostSelectEvent(payload);
    } else if (action == "edit") {
        namespace SK = AmsMappingHotendSchema::keys;
        const std::string ams_id  = payload.value(SK::slot::ams_id, "");
        const std::string slot_id = payload.value(SK::slot::slot_id, "");
        if (!ams_id.empty() && !slot_id.empty()) {
            // Open the shared AMSMaterialsSetting modal on the UI thread, then
            // re-publish state so the mapping popup reflects the edited tray.
            // The dialog writes to the device asynchronously, so push an
            // immediate report once the modal returns plus a delayed one to
            // catch the device echo (mirrors the device-page edit refresh).
            auto* self = this;
            wxGetApp().CallAfter([self, ams_id, slot_id]() {
                auto edited = OpenAmsMaterialsSetting(ams_id, slot_id);
                // The modal AMSMaterialsSetting steals focus and dismisses the
                // transient mapping popup. Re-show it so the user can keep
                // picking filament after editing (its WebView stays alive
                // across the dismiss, and wxEVT_SHOW re-positions it relative
                // to the still-set parent item).
                if (self->m_popup && !self->m_popup->IsShown()) {
                    self->m_popup->Popup();
                }
                self->RefreshAfterEdit();
                // Forward the just-applied filament to the send page so it can
                // resync only the edited slot's MaterialItem without waiting
                // for the asynchronous device echo.
                if (edited.has_value()) {
                    self->PostEditEvent(*edited);
                }
            });
        }
    } else if (action == "reset") {
        if (m_reset_callback) {
            const std::string idx = m_material_index;
            auto cb = m_reset_callback;
            wxGetApp().CallAfter([cb, idx]() { cb(idx); });
        }
    } else if (action == "dismiss") {
        // The popup is owned by SelectMachineDialog; it listens to the same
        // EVT_SET_FINISH_MAPPING / focus loss machinery, so just refresh
        // the cached state. Actual Dismiss() is driven by the popup layer.
    } else if (action == "rendered") {
        // The web side acks that it has committed the DOM for a given session.
        // The popup defers its show until this arrives so the window's first
        // frame is the new machine's data (no stale-then-fresh flicker).
        namespace SK = AmsMappingHotendSchema::keys;
        const std::uint32_t seq = payload.value(SK::root::session_seq, 0u);
        if (m_popup) {
            auto* popup = m_popup;
            wxGetApp().CallAfter([popup, seq]() { popup->OnHotendWebRendered(seq); });
        }
    } else if (action == "presented") {
        // Web acked a painted frame; on macOS the popup stays masked until then.
        namespace SK = AmsMappingHotendSchema::keys;
        const std::uint32_t seq = payload.value(SK::root::session_seq, 0u);
        if (m_popup) {
            auto* popup = m_popup;
            wxGetApp().CallAfter([popup, seq]() { popup->OnHotendWebPresented(seq); });
        }
    } else if (action == "show_nozzle_error") {
        auto* popup = m_popup;
        auto* send_win = m_send_win;
        wxGetApp().CallAfter([popup, send_win]() {
            MessageDialog dlg(wxGetActiveWindow(),
                              _L("Please check the hotend cable and try again."),
                              _L("Hotend communication error"),
                              wxOK | wxCANCEL | wxICON_WARNING);
            dlg.SetButtonLabel(wxID_OK, _L("View Details"));
            if (dlg.ShowModal() != wxID_OK) {
                // Keep the user where they were: the modal steals focus and
                // dismisses the transient popup, same as the edit path.
                if (popup && !popup->IsShown()) { popup->Popup(); }
                return;
            }

            if (popup && popup->IsShown()) { popup->Dismiss(); }
            if (auto* send_dlg = dynamic_cast<wxDialog*>(send_win)) {
                if (send_dlg->IsModal()) { send_dlg->EndModal(wxID_CANCEL); }
            }
            OpenHmsPanel();
        });
    } else if (action == "open_wiki") {
        // Open the filament-matching-rules Wiki in the system browser. URL kept
        // identical to the wx popup (AmsMapingPopup::update_ams_tips).
        wxGetApp().CallAfter([]() {
            wxLaunchDefaultBrowser("https://e.bambulab.com/t?c=v4Q4e7Rm2dR0dWkw");
        });
    }

    return MakeResponse(GetModule(), "action", action, 0, "", BuildState());
}

void DevicePageAmsMappingHotendVM::RefreshAfterEdit()
{
    // Report once the modal returns (the user has confirmed by now). Clearing
    // the dedup cache forces this report through even if the cached payload
    // hasn't changed yet.
    m_last_report_state.clear();
    ReportState("state", "changed");
}

void DevicePageAmsMappingHotendVM::PostSelectEvent(const nlohmann::json& payload)
{
    if (!m_send_win) return;

    // Wire format: payload carries the same fields the C++ popup used to
    // pack into MappingItemHotend::send_event. We re-serialize here so the
    // SelectMachineDialog / Sync-AMS slot picker dispatcher sees a byte-
    // identical EVT_SET_FINISH_MAPPING command.
    namespace SK = AmsMappingHotendSchema::keys;

    int r = payload.value(SK::select_action::r, 0);
    int g = payload.value(SK::select_action::g, 0);
    int b = payload.value(SK::select_action::b, 0);
    int a = payload.value(SK::select_action::a, 255);

    const std::string tray_index    = payload.value(SK::select_action::tray_index, "");
    const int         filament_id   = payload.value(SK::select_action::filament_id, m_current_filament_id);
    const int         tray_id       = payload.value(SK::slot::tray_id, 0);
    const std::string ams_id_str    = payload.value(SK::slot::ams_id, "");
    const std::string slot_id_str   = payload.value(SK::slot::slot_id, "");

    int ams_id = 0;
    int slot_id = 0;
    try {
        if (!ams_id_str.empty())  ams_id  = std::stoi(ams_id_str);
        if (!slot_id_str.empty()) slot_id = std::stoi(slot_id_str);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "invalid ams/slot id: " << e.what();
        return;
    }

    // Stage ctype / multi-color preview on the popup so SelectMachine.cpp's
    // EVT_SET_FINISH_MAPPING handler can recover them (the legacy code path
    // pulled this from MappingItemHotend::m_tray_data which is gone).
    if (m_popup) {
        const int ctype = payload.value(SK::slot::ctype, 0);
        std::vector<wxColour> cols;
        if (payload.contains(SK::slot::multi_colors) && payload[SK::slot::multi_colors].is_array()) {
            for (const auto& c : payload[SK::slot::multi_colors]) {
                if (!c.is_string()) continue;
                const std::string hex = c.get<std::string>();
                // Accept "#RRGGBB" or "#RRGGBBAA".
                if (hex.size() < 7 || hex[0] != '#') continue;
                long rv = std::strtol(hex.substr(1, 2).c_str(), nullptr, 16);
                long gv = std::strtol(hex.substr(3, 2).c_str(), nullptr, 16);
                long bv = std::strtol(hex.substr(5, 2).c_str(), nullptr, 16);
                long av = (hex.size() >= 9) ? std::strtol(hex.substr(7, 2).c_str(), nullptr, 16) : 255;
                cols.emplace_back((unsigned char)rv, (unsigned char)gv, (unsigned char)bv, (unsigned char)av);
            }
        }
        m_popup->set_last_hotend_select(ctype, cols);
    }

    auto* evt = new wxCommandEvent(EVT_SET_FINISH_MAPPING);
    evt->SetInt(tray_id);

    wxString param = wxString::Format("%d|%d|%d|%d|%s|%d|%d|%d",
                                      r, g, b, a,
                                      wxString::FromUTF8(tray_index.c_str()),
                                      filament_id, ams_id, slot_id);
    evt->SetString(param);
    evt->SetEventObject(m_send_win);
    wxQueueEvent(m_send_win, evt);

    if (m_popup) {
        wxGetApp().CallAfter([popup = m_popup]() { popup->Dismiss(); });
    }
}

void DevicePageAmsMappingHotendVM::PostEditEvent(const EditedFilamentInfo& info)
{
    if (!m_send_win) return;

    // Wire format consumed by SelectMachineDialog::on_hotend_slot_edited:
    //   ams_id|slot_id|r|g|b|a|ctype|filament_type|col0,col1,...
    // The trailing field is a comma-separated list of "#RRGGBBAA" multi-colors
    // (may be empty). filament_type / ids never contain '|' or ','.
    wxString colors_str;
    for (size_t i = 0; i < info.colors.size(); ++i) {
        if (i) colors_str += ",";
        colors_str += wxString::Format("#%02X%02X%02X%02X",
                                       info.colors[i].Red(), info.colors[i].Green(),
                                       info.colors[i].Blue(), info.colors[i].Alpha());
    }

    wxString param = wxString::Format("%s|%s|%d|%d|%d|%d|%d|%s|%s",
                                      wxString::FromUTF8(info.ams_id.c_str()),
                                      wxString::FromUTF8(info.slot_id.c_str()),
                                      info.color.Red(), info.color.Green(),
                                      info.color.Blue(), info.color.Alpha(),
                                      info.ctype,
                                      wxString::FromUTF8(info.filament_type.c_str()),
                                      colors_str);

    auto* evt = new wxCommandEvent(EVT_HOTEND_SLOT_EDITED);
    evt->SetString(param);
    evt->SetEventObject(m_send_win);
    wxQueueEvent(m_send_win, evt);
}

}} // namespace Slic3r::GUI
