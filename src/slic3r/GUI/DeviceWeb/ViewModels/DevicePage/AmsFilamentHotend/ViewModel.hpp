#ifndef DEVICEPAGEAMSFILAMENTHOTENDVM_HPP
#define DEVICEPAGEAMSFILAMENTHOTENDVM_HPP

#include "slic3r/GUI/DeviceWeb/IViewModel.hpp"
#include "Schema.hpp"

#include <optional>
#include <string>
#include <utility>

namespace Slic3r {
class MachineObject;
class DevAms;
class DevAmsTray;
}

namespace Slic3r { namespace GUI {

class DevicePageExtAmsActionHandler;
class DevicePageFilamentActionHandler;
class DevicePageHotendActionHandler;

class DevicePageAmsFilamentHotendVM : public IViewModel {
    friend class DevicePageExtAmsActionHandler;
    friend class DevicePageFilamentActionHandler;
    friend class DevicePageHotendActionHandler;

public:
    std::string GetModule() const override { return "device_page_ams_filament_hotend"; }

    nlohmann::json OnCommand(const std::string& submod,
                             const std::string& action,
                             const nlohmann::json& payload) override;
    void ReportState(const std::string& submod, const std::string& action) override;
    void OnResponseSent(const nlohmann::json& response) override;
    void OnSysColorChanged() override;
    std::string ResolveFilamentCardText(const Slic3r::DevAmsTray* tray, const std::string& fallback_type) const;
    std::string ResolveFilamentCardText(const std::string& filament_id, const std::string& fallback_type) const;

private:
    nlohmann::json BuildDefaultState() const;
    nlohmann::json BuildState() const;
    nlohmann::json BuildSlot(Slic3r::MachineObject* machine_obj,
                             int addr_id,
                             const std::optional<std::pair<int, int>>& slot_id,
                             const Slic3r::DevAms* fallback_dryer_unit) const;
    void BuildHotendRackStatus(Slic3r::MachineObject* machine_obj, nlohmann::json& hotend_page) const;

    // Serialize a single AMS unit (including its trays) to the JSON shape consumed
    // by the Web. Used both by the top-level `ams.units` list and by per-slot
    // `ams_operation.units` so the AMS Operation dialog can read straight from
    // the slot without re-querying.
    nlohmann::json BuildAmsUnitJson(Slic3r::MachineObject* machine_obj, const Slic3r::DevAms* ams_unit) const;
    bool IsCurrentFilamentChangingSlot(Slic3r::MachineObject* machine_obj, const std::string& ams_id, const std::string& slot_id) const;
    bool IsFilamentChangeActive(Slic3r::MachineObject* machine_obj) const;
    bool IsNozzleOnExtruder(Slic3r::MachineObject* machine_obj, int addr_id) const;

    nlohmann::json HandleAction(const std::string& action, const nlohmann::json& payload);
    nlohmann::json HandleDebug(const std::string& action, const nlohmann::json& payload);
    nlohmann::json BuildDebugSnapshot(const nlohmann::json& payload) const;
    void OpenBatchFilamentEditDialog(const nlohmann::json& payload);

    std::string m_last_report_state;
    // UI mode requested by Web ("normal" / "batch_ams_load" / ...). When the
    // Web sends state.get with a `mode` field it's mirrored here so the
    // next ReportState() picks the same mode-specific builder without the
    // Web re-fetching. Defaults to normal so first paint matches today's
    // behaviour exactly.
    std::string m_active_mode = "normal";
    // Set after a batch run dispatches its MQTT command so the NEXT state.get
    // forces the page back to "normal", even if the (separate) main-page WebView
    // re-requests its stale batch mode via focus/poll refresh. Consumed once.
    bool m_pending_exit_batch = false;
};

}} // namespace Slic3r::GUI

#endif // DEVICEPAGEAMSFILAMENTHOTENDVM_HPP
