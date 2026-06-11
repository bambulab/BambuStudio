#ifndef DEVICEPAGEAMSFILAMENTHOTENDVM_HPP
#define DEVICEPAGEAMSFILAMENTHOTENDVM_HPP

#include "slic3r/GUI/DeviceWeb/IViewModel.hpp"
#include "DevicePageSchema_AmsFilamentHotend.hpp"

#include <optional>
#include <string>
#include <utility>

namespace Slic3r {
class MachineObject;
class DevAms;
class DevAmsTray;
}

namespace Slic3r { namespace GUI {

class DevicePageAmsFilamentHotendVM : public IViewModel {
public:
    std::string GetModule() const override { return "device_page_ams_filament_hotend"; }

    nlohmann::json OnCommand(const std::string& submod,
                             const std::string& action,
                             const nlohmann::json& payload) override;
    void ReportState(const std::string& submod, const std::string& action) override;
    void OnSysColorChanged() override;
    std::string ResolveFilamentCardText(const Slic3r::DevAmsTray* tray, const std::string& fallback_type) const;

private:
    nlohmann::json BuildDefaultState() const;
    nlohmann::json BuildState() const;
    nlohmann::json BuildSlot(Slic3r::MachineObject* machine_obj,
                             int addr_id,
                             const std::optional<std::pair<int, int>>& slot_id,
                             const Slic3r::DevAms* fallback_dryer_unit) const;
    void BuildHotendRackStatus(Slic3r::MachineObject* machine_obj, nlohmann::json& hotend_page) const;

    std::string ResolveCardState(Slic3r::MachineObject* machine_obj, const Slic3r::DevAmsTray* tray, const std::string& ams_id, const std::string& slot_id, int addr_id) const;
    std::string ResolveLineState(const std::string& card_state,
                                 bool has_hotend_slot,
                                 bool filament_changing,
                                 bool listed_as_changing,
                                 bool loaded_in_nozzle) const;
    std::string ResolveNozzleState(const std::string& card_state, const std::string& line_state) const;
    std::string ResolveStatusText(const std::string& card_state) const;
    nlohmann::json BuildFilamentChangeStatus(Slic3r::MachineObject* machine_obj,
                                             const nlohmann::json& running_slot,
                                             const std::string& operation,
                                             const std::string& message) const;
    // Serialize a single AMS unit (including its trays) to the JSON shape consumed
    // by the Web. Used both by the top-level `ams.units` list and by per-slot
    // `ams_operation.units` so the AMS Operation dialog can read straight from
    // the slot without re-querying.
    nlohmann::json BuildAmsUnitJson(Slic3r::MachineObject* machine_obj, const Slic3r::DevAms* ams_unit) const;
    bool IsSlotLoadedInExtruder(Slic3r::MachineObject* machine_obj, const std::string& ams_id, const std::string& slot_id) const;
    bool IsSlotLoadedInNozzle(Slic3r::MachineObject* machine_obj, int addr_id, const std::string& ams_id, const std::string& slot_id) const;
    bool IsSlotTargetForExtruder(Slic3r::MachineObject* machine_obj, const std::string& ams_id, const std::string& slot_id) const;
    bool IsSlotInFilamentChangeList(Slic3r::MachineObject* machine_obj, const std::string& ams_id, const std::string& slot_id) const;
    bool IsFilamentChangeActive(Slic3r::MachineObject* machine_obj) const;
    bool IsNozzleOnExtruder(Slic3r::MachineObject* machine_obj, int addr_id) const;

    nlohmann::json HandleAction(const std::string& action, const nlohmann::json& payload);
    std::optional<nlohmann::json> HandleExtAmsAction(const std::string& action, const nlohmann::json& payload, Slic3r::MachineObject* machine_obj);
    std::optional<nlohmann::json> HandleFilamentAction(const std::string& action, const nlohmann::json& payload, Slic3r::MachineObject* machine_obj);
    std::optional<nlohmann::json> HandleHotendAction(const std::string& action, const nlohmann::json& payload, Slic3r::MachineObject* machine_obj);
    nlohmann::json HandleDebug(const std::string& action, const nlohmann::json& payload);
    nlohmann::json BuildDebugSnapshot(const nlohmann::json& payload) const;

    std::string m_last_report_state;
    // UI mode requested by Web ("normal" / "batch_ams_load" / ...). When the
    // Web sends state.get with a `mode` field it's mirrored here so the
    // next ReportState() picks the same mode-specific builder without the
    // Web re-fetching. Defaults to normal so first paint matches today's
    // behaviour exactly.
    std::string m_active_mode = "normal";
};

}} // namespace Slic3r::GUI

#endif // DEVICEPAGEAMSFILAMENTHOTENDVM_HPP
