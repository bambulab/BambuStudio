#ifndef DEVICEPAGEAMSFILAMENTHOTENDVM_HPP
#define DEVICEPAGEAMSFILAMENTHOTENDVM_HPP

#include "slic3r/GUI/DeviceWeb/IViewModel.hpp"

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

private:
    nlohmann::json BuildState() const;
    nlohmann::json BuildSlot(Slic3r::MachineObject* machine_obj,
                             int nozzle_pos,
                             const std::optional<std::pair<int, int>>& slot_id,
                             const Slic3r::DevAms* fallback_dryer_unit) const;

    std::string ResolveCardState(Slic3r::MachineObject* machine_obj, const Slic3r::DevAmsTray* tray, const std::string& ams_id, const std::string& slot_id) const;
    std::string ResolveLineState(const std::string& card_state, bool has_hotend_slot) const;
    std::string ResolveNozzleState(const std::string& card_state, const std::string& line_state) const;
    std::string ResolveStatusText(const std::string& card_state) const;
    bool IsSlotLoadedInExtruder(Slic3r::MachineObject* machine_obj, const std::string& ams_id, const std::string& slot_id) const;

    nlohmann::json HandleAction(const std::string& action, const nlohmann::json& payload);

    std::string m_last_report_state;
};

}} // namespace Slic3r::GUI

#endif // DEVICEPAGEAMSFILAMENTHOTENDVM_HPP
