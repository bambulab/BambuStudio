/*****************************************************************//**
 * \file   DevicePageAmsMappingHotendVM.hpp
 * \brief  ViewModel for the "send to print" hotend mapping popup.
 *
 * Mirrors the legacy wxWidgets AmsMapingPopup hotend-rack branch (hotend item
 * grid + sub-AMS expand) over to the React/WebView2 architecture used
 * by DevicePageAmsFilamentHotendVM.
 *
 * Event contract is preserved verbatim: on action/select the VM posts
 * EVT_SET_FINISH_MAPPING to the original send_win (SelectMachineDialog
 * / SyncAmsInfoDialog), so the higher-level mapping pipeline is
 * untouched.
 *
 * \author xin.zhang
 * \date   May 2026
 *********************************************************************/
#ifndef DEVICEPAGEAMSMAPPINGHOTENDVM_HPP
#define DEVICEPAGEAMSMAPPINGHOTENDVM_HPP

#include "slic3r/GUI/DeviceWeb/IViewModel.hpp"
#include "DevicePageAmsMappingHotendSchema.hpp"

#include "slic3r/GUI/AmsMappingPopup.hpp"      // for FilamentInfo, ShowType, ResetCallback
#include "slic3r/GUI/DevMappingHelpers.h"      // for ExpectedNozzleSpec, HotendSlotState

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r {
class MachineObject;
class DevAms;
class DevAmsTray;
namespace GUI {

class DevicePageAmsMappingHotendVM : public IViewModel {
public:
    using ResetCallback = std::function<void(const std::string&)>;

    std::string GetModule() const override { return "device_page_ams_mapping_hotend"; }

    nlohmann::json OnCommand(const std::string& submod,
                             const std::string& action,
                             const nlohmann::json& payload) override;
    void ReportState(const std::string& submod, const std::string& action) override;
    void OnSysColorChanged() override;

    // Called by AmsMapingPopup whenever the popup-level inputs change
    // (open / mapping result update / show type flip / etc.). Caches
    // the snapshot then pushes a fresh state report to the web side.
    void Refresh(MachineObject*                       obj,
                 const std::vector<FilamentInfo>&     mapping_result,
                 int                                  current_filament_id,
                 ShowType                             show_type,
                 bool                                 ext_mapping_filatype_check,
                 const std::string&                   material_index,
                 const ResetCallback&                 reset_cb,
                 wxWindow*                            send_win,
                 AmsMapingPopup*                      popup,
                 std::optional<PrintFromType>         print_from_type);

private:
    nlohmann::json BuildState() const;

    // Remap the bound AMS slot for the main hotend card.
    //
    // The main card in the AMS-mapping popup must only ever display a slot
    // that lives on the rack itself (ZMULTI_A/B) or an external spool. When
    // the device pushes a sub-AMS as the current feed source (slot_now hits
    // N3S/N3F/AMS), GetBindAmsSlots()[0] returns the sub-AMS coords directly
    // and the card would render the sub-AMS filament — that contradicts the
    // product spec which expects the rack-aligned ZMULTI slot (possibly
    // empty) on the main card and the sub-AMS info inside the expand panel.
    //
    // Returns the original slot unchanged when:
    //   - it already points at ZMULTI / external spool,
    //   - or the device has no ZMULTI on this rack at all.
    static std::pair<int, int> RemapMainCardSlot(MachineObject*             obj,
                                                 const std::pair<int, int>& original_slot,
                                                 int                        nozzle_pos);

    // Build the list of TrayData for one ZMULTI AMS unit (main rack).
    std::vector<TrayData> CollectZMultiTrayData(MachineObject* obj, DevAms* ams_group) const;

    // Build the list of TrayData for virtual trays when no ZMULTI is present.
    std::vector<TrayData> CollectVirtualTrayData(MachineObject* obj) const;

    // For one hotend slot, produce its JSON payload (state, color, sub_ams, ...).
    //
    // `rack_nozzle_pos` is the rack-position id we were already iterating in
    // BuildState() (the very same key we used to look up the bound AMS slot).
    // We pass it down explicitly because reverse-deriving it later from the
    // bound tray via DevAmsTray::get_nozzle_pos_id() drops to the unimplemented
    // TODO branch when the tray happens to belong to a real AMS sub-unit
    // (DevAmsType::AMS / N3F / N3S) hooked directly onto a hotend rack — in
    // which case we used to silently lose the nozzle row, the +N badge and
    // the sub-AMS expand list for that hotend slot.
    nlohmann::json BuildSlotJson(MachineObject*                            obj,
                                 const TrayData&                           td,
                                 int                                       slot_index,
                                 int                                       rack_nozzle_pos,
                                 const ExpectedNozzleSpec&                 expected_nozzle,
                                 bool                                      is_all_ext,
                                 const std::optional<std::pair<int, int>>& preload_slot) const;

    // Sub-AMS groups for one hotend slot, mirrors add_ams_mapping_hotend.
    //
    // `parent_nz` is the rack nozzle resolved by the caller from the known
    // rack position. Passing it in lets us avoid the reverse derivation via
    // `self_td`'s `get_nozzle_pos_id` — that path is unreliable when the
    // self tray is a real AMS sub-slot bound directly to the rack (see
    // BuildSlotJson comment).
    std::vector<SubAmsGroup> CollectSubAmsGroups(MachineObject*                  obj,
                                                 const TrayData&                 self_td,
                                                 const std::optional<DevNozzle>& parent_nz) const;

    // Resolve the per-slot judgement (state + atomic facts) by walking the
    // same rules as the legacy popup. The returned struct does NOT yet have
    // `is_pickable` filled in — that is the caller's responsibility because
    // pickability depends on the global all-ext mode (main slot) vs. the
    // strict-Normal-only rule (sub-slot).
    //
    // `override_nz` lets the caller pre-resolve a rack nozzle to use instead
    // of querying via the slot's own `get_nozzle_pos_id`. Sub-AMS slots
    // share their parent hotend's rack nozzle (they don't carry a direct
    // binding), so BuildSlotJson resolves the parent nozzle once and passes
    // it down. When `override_nz` is omitted, we fall back to a per-slot
    // QueryRackNozzleAt(obj, td) which is correct for main slots.
    SlotJudgement ResolveSlotJudgement(MachineObject*                  obj,
                                       const TrayData&                 td,
                                       const ExpectedNozzleSpec&       expected_nozzle,
                                       const std::optional<DevNozzle>& override_nz = std::nullopt) const;

    // action dispatch
    nlohmann::json HandleAction(const std::string& action, const nlohmann::json& payload);

    void PostSelectEvent(const nlohmann::json& payload);

    // Re-publish state after the AMSMaterialsSetting modal closes (the user has
    // confirmed). Pushes a single immediate report.
    void RefreshAfterEdit();

    // Cached snapshot of the popup context.
    MachineObject*               m_obj{nullptr};
    std::vector<FilamentInfo>    m_mapping_result;
    int                          m_current_filament_id{0};
    ShowType                     m_show_type{ShowType::RIGHT};
    bool                         m_ext_mapping_filatype_check{true};
    std::string                  m_material_index;
    ResetCallback                m_reset_callback{nullptr};
    wxWindow*                    m_send_win{nullptr};
    AmsMapingPopup*              m_popup{nullptr};
    std::optional<PrintFromType> m_print_from_type;

    // Last published state JSON, used to suppress duplicate reports.
    std::string m_last_report_state;

    // Bumped on every `Refresh()` so the Web side can detect popup re-opens
    // (the React component stays mounted across PopupWindow show/hide) and
    // reset its transient sub-page navigation state.
    std::uint32_t m_session_seq{0};
};

}} // namespace Slic3r::GUI

#endif // DEVICEPAGEAMSMAPPINGHOTENDVM_HPP
