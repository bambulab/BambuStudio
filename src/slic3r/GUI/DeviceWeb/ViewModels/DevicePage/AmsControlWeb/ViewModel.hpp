#ifndef DEVICEPAGEAMSCONTROLWEBVM_HPP
#define DEVICEPAGEAMSCONTROLWEBVM_HPP

#include "slic3r/GUI/DeviceWeb/IViewModel.hpp"

#include <string>
#include <vector>

namespace Slic3r {
class MachineObject;
}

namespace Slic3r { namespace GUI {

namespace AmsControlWebSchema { namespace format { struct State; } }

class AmsControlWebActionHandler;

class DevicePageAmsControlWebVM : public IViewModel {
    friend class AmsControlWebActionHandler;

public:
    std::string GetModule() const override { return "device_page_ams_control_web"; }

    nlohmann::json OnCommand(const std::string& submod,
                             const std::string& action,
                             const nlohmann::json& payload) override;
    void ReportState(const std::string& submod, const std::string& action) override;
    void OnSysColorChanged() override;

    static void NotifyNewRfidFilament(const std::string& ams_id, const std::string& slot_id);
    static void DismissFilamentMgrHint(const std::string& ams_id, const std::string& slot_id);

private:
    // Drives the two builders and owns the envelope between them: selection and
    // footer actions.
    nlohmann::json BuildState() const;
    void ResolveSelection(AmsControlWebSchema::format::State& state) const;
    void FillActions(Slic3r::MachineObject* machine_obj,
                     AmsControlWebSchema::format::State& state) const;

    nlohmann::json HandleAction(const std::string& action, const nlohmann::json& payload);

    // State the action handler moves. Both drop the report cache so the next push
    // goes out even when the device data itself did not change.
    void SetSelectedSlot(const std::string& ams_id, const std::string& slot_id);
    void ClearSelectedSlot() { SetSelectedSlot({}, {}); }
    void PromoteActiveAms(const std::string& ams_id);
    void InvalidateReportCache() { m_last_report_state.clear(); }

    const std::string& SelectedAmsId() const { return m_selected_ams_id; }
    const std::string& SelectedSlotId() const { return m_selected_slot_id; }

    nlohmann::json HandleDebug(const std::string& action, const nlohmann::json& payload);
    nlohmann::json BuildDebugSnapshot(const nlohmann::json& payload) const;

    std::string m_last_report_state;
    // Empty is a real state, not an unset one: nothing is picked, and the footer
    // says so instead of acting on a slot the user cannot see.
    std::string m_selected_ams_id;
    std::string m_selected_slot_id;
    // Units the user opened, most recent first. Each column picks the first entry
    // it owns, so one column switches without disturbing the other.
    std::vector<std::string> m_active_ams_ids;
};

}} // namespace Slic3r::GUI

#endif // DEVICEPAGEAMSCONTROLWEBVM_HPP
