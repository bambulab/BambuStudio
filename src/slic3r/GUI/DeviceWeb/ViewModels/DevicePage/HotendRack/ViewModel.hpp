#pragma once

#include "slic3r/GUI/DeviceWeb/IViewModel.hpp"

#include <string>

namespace Slic3r { namespace GUI {

class DevicePageHotendRackVM : public IViewModel
{
public:
    std::string GetModule() const override { return "device_page_hotend_rack"; }

    nlohmann::json OnCommand(const std::string& submod,
                             const std::string& action,
                             const nlohmann::json& payload) override;

    void ReportState(const std::string& submod, const std::string& action) override;
    void OnResponseSent(const nlohmann::json& response) override;
    void OnSysColorChanged() override;

    nlohmann::json BuildState() const;
    nlohmann::json HandleAction(const std::string& action, const nlohmann::json& payload);
    nlohmann::json HandleDebug(const std::string& action, const nlohmann::json& payload);
    nlohmann::json BuildDebugSnapshot(const nlohmann::json& payload) const;
    void InvalidateReportCache() { m_last_report_state.clear(); }
    nlohmann::json MakeActionResponse(const std::string& action,
                                      int code,
                                      const std::string& message = {},
                                      const nlohmann::json& payload = nlohmann::json::object()) const
    {
        return MakeResponse(GetModule(), "action", action, code, message, payload);
    }

private:
    std::string m_last_report_state;
};

}} // namespace Slic3r::GUI
