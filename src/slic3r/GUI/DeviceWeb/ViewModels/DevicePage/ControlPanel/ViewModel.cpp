#include "ViewModel.hpp"

#include "Schema.hpp"
#include "ViewModelActions.hpp"
#include "ViewModelStateBuilder.hpp"

#include "slic3r/GUI/DeviceWeb/DeviceWebBridge.hpp"
#include "slic3r/GUI/DeviceWeb/DeviceWebHost.hpp"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/wxExtensions.hpp"

#include <chrono>
#include <wx/sizer.h>
#include "libslic3r/Log.hpp"

namespace Slic3r { namespace GUI {

namespace {

class ControlPanelDebugDialog : public DPIDialog
{
public:
    explicit ControlPanelDebugDialog(wxWindow* parent)
        : DPIDialog(parent, wxID_ANY, _devL("Control Panel Web Debug"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX | wxRESIZE_BORDER)
    {
        SetClientSize(wxSize(FromDIP(900), FromDIP(700)));
        SetMinClientSize(wxSize(FromDIP(720), FromDIP(520)));
    }

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override { SetSize(suggested_rect.GetSize()); }
};

nlohmann::json& control_panel_debug_snapshot()
{
    static nlohmann::json snapshot = nlohmann::json::object();
    return snapshot;
}

void open_control_panel_debug_dialog()
{
    wxWindow* parent = wxGetApp().mainframe;
    ControlPanelDebugDialog dlg(parent);
    auto* host = new DeviceWebHost(&dlg, DeviceWebHostMode::DevicePageControlPanel, "/device_page/control_panel_debug");
    host->SetMinSize(wxSize(dlg.FromDIP(720), dlg.FromDIP(520)));
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(host, 1, wxEXPAND);
    dlg.SetSizer(sizer);
    dlg.Layout();
    dlg.ShowModal();
}

} // namespace

nlohmann::json DevicePageControlPanelVM::OnCommand(const std::string& submod,
                                                   const std::string& action,
                                                   const nlohmann::json& payload)
{
    try {
        if (submod == "state" && (action == "get" || action == "init")) {
            nlohmann::json state = BuildState();
            const bool dark = wxGetApp().app_config->get("dark_color_mode") == "1";
            state["theme"] = dark ? "dark" : "light";
            return MakeResponse(GetModule(), submod, action, 0, "", state);
        }
        if (submod == "action") {
            return HandleAction(action, payload);
        }
#if !BBL_RELEASE_TO_PUBLIC
        if (submod == "debug") {
            return HandleDebug(action, payload);
        }
#endif
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "control panel web exception: " << e.what();
        return MakeResponse(GetModule(), submod, action, 2, e.what(), BuildState());
    }

    return MakeResponse(GetModule(), submod, action, 1, "unknown control panel command", BuildState());
}

nlohmann::json DevicePageControlPanelVM::BuildDebugSnapshot(const nlohmann::json& payload) const
{
    nlohmann::json snapshot;
    snapshot["created_ts"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    snapshot["cpp_state"] = BuildState();
    snapshot["web_state"] = payload.value("web_state", nlohmann::json::object());
    snapshot["web_trace"] = payload.value("web_trace", nlohmann::json::array());
    snapshot["cpp_trace"] = m_bridge ? m_bridge->RecentTrace() : nlohmann::json::array();
    return snapshot;
}

nlohmann::json DevicePageControlPanelVM::HandleDebug(const std::string& action, const nlohmann::json& payload)
{
#if !BBL_RELEASE_TO_PUBLIC
    if (action == "open_bridge_trace") {
        control_panel_debug_snapshot() = BuildDebugSnapshot(payload);
        wxGetApp().CallAfter([]() { open_control_panel_debug_dialog(); });
        return MakeResponse(GetModule(), "debug", action, 0, "", control_panel_debug_snapshot());
    }
    if (action == "snapshot") {
        nlohmann::json snapshot = control_panel_debug_snapshot();
        snapshot["dialog_snapshot_ts"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        snapshot["dialog_cpp_state"] = BuildState();
        snapshot["dialog_cpp_trace"] = m_bridge ? m_bridge->RecentTrace() : nlohmann::json::array();
        return MakeResponse(GetModule(), "debug", action, 0, "", snapshot);
    }
#else
    (void) payload;
#endif
    return MakeResponse(GetModule(), "debug", action, 1, "unknown debug action", nlohmann::json::object());
}

nlohmann::json DevicePageControlPanelVM::HandleAction(const std::string& action, const nlohmann::json& payload)
{
    MachineObject* machine_obj = wxGetApp().getDeviceManager()
        ? wxGetApp().getDeviceManager()->get_selected_machine()
        : nullptr;

    if (auto handled = ControlPanelActionHandler::TryHandle(*this, action, payload, machine_obj))
        return std::move(handled.value());

    return MakeResponse(GetModule(), "action", action, 1, "unknown control panel action", BuildState());
}

nlohmann::json DevicePageControlPanelVM::BuildState() const
{
    MachineObject* machine_obj = wxGetApp().getDeviceManager()
        ? wxGetApp().getDeviceManager()->get_selected_machine()
        : nullptr;
    return ControlPanel::BuildState(machine_obj);
}

void DevicePageControlPanelVM::ReportState(const std::string& submod, const std::string& action)
{
    if (!m_bridge) return;

    nlohmann::json state = BuildState();

    std::string state_key;
    try {
        state_key = state.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "control panel state.dump() failed: " << e.what();
        return;
    }
    if (state_key == m_last_report_state) return;

    m_last_report_state = state_key;
    m_bridge->ReportMsg(MakeResponse(GetModule(), submod, action, 0, "", state));
}

void DevicePageControlPanelVM::OnResponseSent(const nlohmann::json& response)
{
    const auto payload_it = response.find("payload");
    if (payload_it == response.end() || !payload_it->is_object()) return;
    if (!payload_it->contains("display")) return;
    try {
        if (payload_it->contains("theme")) {
            nlohmann::json state = *payload_it;
            state.erase("theme");
            m_last_report_state = state.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        } else {
            m_last_report_state = payload_it->dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }
    } catch (...) {
        m_last_report_state.clear();
    }
}

void DevicePageControlPanelVM::OnSysColorChanged()
{
    if (!m_bridge) return;
    const bool dark = wxGetApp().app_config->get("dark_color_mode") == "1";
    nlohmann::json data;
    data["theme"] = dark ? "dark" : "light";
    m_bridge->ReportMsg(MakeResponse(GetModule(), "init", "theme_changed", 0, "", data));
}

}} // namespace Slic3r::GUI
