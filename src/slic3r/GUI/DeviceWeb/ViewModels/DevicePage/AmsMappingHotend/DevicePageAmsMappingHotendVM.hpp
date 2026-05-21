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

#include <functional>
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

    // Build the list of TrayData for one ZMULTI AMS unit (main rack).
    std::vector<TrayData> CollectZMultiTrayData(MachineObject* obj, DevAms* ams_group) const;

    // Build the list of TrayData for virtual trays when no ZMULTI is present.
    std::vector<TrayData> CollectVirtualTrayData(MachineObject* obj) const;

    // For one hotend slot, produce its JSON payload (state, color, sub_ams, ...).
    nlohmann::json BuildSlotJson(MachineObject*                            obj,
                                 const TrayData&                           td,
                                 int                                       slot_index,
                                 const ExpectedNozzleSpec&                 expected_nozzle,
                                 bool                                      is_all_ext,
                                 const std::optional<std::pair<int, int>>& preload_slot) const;

    // Sub-AMS groups for one hotend slot, mirrors add_ams_mapping_hotend.
    std::vector<SubAmsGroup> CollectSubAmsGroups(MachineObject*  obj,
                                                 const TrayData& self_td) const;

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
};

}} // namespace Slic3r::GUI

#endif // DEVICEPAGEAMSMAPPINGHOTENDVM_HPP
