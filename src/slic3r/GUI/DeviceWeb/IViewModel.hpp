// src/slic3r/GUI/DeviceWeb/IViewModel.hpp
#ifndef IVIEWMODEL_HPP
#define IVIEWMODEL_HPP

#include <string>
#include <nlohmann/json.hpp>

namespace Slic3r { namespace GUI {

class DeviceWebBridge;

class IViewModel {
public:
    virtual ~IViewModel() = default;

    virtual std::string GetModule() const = 0;

    // Handle a command from the web frontend.
    // Returns the response body JSON (containing code, message, payload).
    virtual nlohmann::json OnCommand(
        const std::string& submod,
        const std::string& action,
        const nlohmann::json& payload
    ) = 0;

    // Report current state for the given submod to the frontend.
    virtual void ReportState(const std::string& submod, const std::string& action) = 0;

    void SetBridge(DeviceWebBridge* bridge) { m_bridge = bridge; }

    // Called when the system colour mode (dark/light) changes.
    // Default implementation pushes a theme_changed report to the frontend via bridge.
    virtual void OnSysColorChanged() {}

protected:
    DeviceWebBridge* m_bridge{nullptr};

    /**
     * Build a standard response/report body.
     *
     * Wire format (JSON):
     *   {
     *     "module":     "calibration",
     *     "submod":     "pa_history",
     *     "action":     "list",
     *     "error_code": 0,
     *     "message":    "",
     *     "payload":    { ... }
     *   }
     *
     * Fields:
     *   module      Top-level feature module (matches IViewModel::GetModule()).
     *               Used by DeviceWebManager to route requests to the right ViewModel.
     *               Examples: "calibration", "filament"
     *
     *   submod      Sub-module within the module — identifies which functional
     *               group the command targets. Each ViewModel dispatches on this.
     *               Examples: "pa_history", "spool", "preset", "ams"
     *
     *   action      The operation to perform on the sub-module.
     *               Examples: "list", "add", "update", "delete"
     *
     *   error_code  Result status. 0 = success, non-zero = error.
     *
     *   message     Human-readable error description (empty on success).
     *
     *   payload     The actual data. Structure depends on module/submod/action.
     */
    static nlohmann::json MakeResponse(
        const std::string& module,
        const std::string& submod,
        const std::string& action,
        int code,
        const std::string& message = "",
        const nlohmann::json& payload = nlohmann::json::object())
    {
        return {
            {"module",     module},
            {"submod",     submod},
            {"action",     action},
            {"error_code", code},
            {"message",    message},
            {"payload",    payload}
        };
    }
};

}} // namespace Slic3r::GUI

#endif // IVIEWMODEL_HPP
