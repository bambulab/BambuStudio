#include "ViewModel.hpp"

#include "ViewModelActions.hpp"
#include "ViewModelStateBuilder.hpp"

#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/DeviceWeb/DeviceWebBridge.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/wxExtensions.hpp"

#include "libslic3r/Log.hpp"

#include <chrono>
#include <utility>
#include <wx/sizer.h>
#include <wx/textctrl.h>

namespace Slic3r { namespace GUI {

namespace {

nlohmann::json& filament_load_debug_snapshot()
{
    static nlohmann::json snapshot = nlohmann::json::object();
    return snapshot;
}

class FilamentLoadDebugDialog : public DPIDialog
{
public:
    explicit FilamentLoadDebugDialog(wxWindow* parent)
        : DPIDialog(parent, wxID_ANY, _devL("Filament Load Web Debug"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX | wxRESIZE_BORDER)
    {
        SetClientSize(wxSize(FromDIP(900), FromDIP(700)));
        SetMinClientSize(wxSize(FromDIP(720), FromDIP(520)));
    }

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override { SetSize(suggested_rect.GetSize()); }
};

void open_filament_load_debug_dialog()
{
    wxWindow* parent = wxGetApp().mainframe;
    FilamentLoadDebugDialog dlg(parent);
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    auto* text = new wxTextCtrl(&dlg, wxID_ANY,
                                wxString::FromUTF8(filament_load_debug_snapshot().dump(2)),
                                wxDefaultPosition, wxDefaultSize,
                                wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
    sizer->Add(text, 1, wxEXPAND | wxALL, dlg.FromDIP(8));
    dlg.SetSizer(sizer);
    dlg.Layout();
    dlg.ShowModal();
}

} // namespace

nlohmann::json DevicePageFilamentLoadVM::OnCommand(const std::string& submod,
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
#if !BBL_RELEASE_TO_PUBLIC
        if (submod == "debug") {
            return HandleDebug(action, payload);
        }
#endif
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "filament load web exception: " << e.what();
        return MakeResponse(GetModule(), submod, action, 2, e.what(), BuildState());
    }

    return MakeResponse(GetModule(), submod, action, 1, "unknown filament load command", BuildState());
}

nlohmann::json DevicePageFilamentLoadVM::HandleAction(const std::string& action, const nlohmann::json& payload)
{
    MachineObject* machine_obj = wxGetApp().getDeviceManager()
        ? wxGetApp().getDeviceManager()->get_selected_machine()
        : nullptr;

    if (action == "toggle_expand") {
        m_expanded = !m_expanded;
        InvalidateReportCache();
        return MakeActionResponse(action, 0, "", BuildState());
    }
    if (action == "reset_expanded") {
        m_expanded = false;
        InvalidateReportCache();
        return MakeActionResponse(action, 0, "", BuildState());
    }

    if (auto handled = FilamentLoadActionHandler::TryHandle(*this, action, payload, machine_obj))
        return std::move(handled.value());

    return MakeResponse(GetModule(), "action", action, 1, "unknown filament load action", BuildState());
}

nlohmann::json DevicePageFilamentLoadVM::BuildState() const
{
    MachineObject* machine_obj = wxGetApp().getDeviceManager()
        ? wxGetApp().getDeviceManager()->get_selected_machine()
        : nullptr;
    return FilamentLoadStateBuilder::BuildState(machine_obj, m_expanded);
}

nlohmann::json DevicePageFilamentLoadVM::BuildDebugSnapshot(const nlohmann::json& payload) const
{
    nlohmann::json snapshot;
    snapshot["created_ts"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    snapshot["cpp_state"] = BuildState();
    snapshot["web_state"] = payload.value("web_state", nlohmann::json::object());
    snapshot["cpp_trace"] = m_bridge ? m_bridge->RecentTrace() : nlohmann::json::array();
    return snapshot;
}

nlohmann::json DevicePageFilamentLoadVM::HandleDebug(const std::string& action, const nlohmann::json& payload)
{
#if !BBL_RELEASE_TO_PUBLIC
    if (action == "open_bridge_trace") {
        filament_load_debug_snapshot() = BuildDebugSnapshot(payload);
        wxGetApp().CallAfter([]() { open_filament_load_debug_dialog(); });
        return MakeResponse(GetModule(), "debug", action, 0, "", filament_load_debug_snapshot());
    }
    if (action == "snapshot")
        return MakeResponse(GetModule(), "debug", action, 0, "", BuildDebugSnapshot(payload));
#else
    (void) payload;
#endif
    return MakeResponse(GetModule(), "debug", action, 1, "unknown debug action", nlohmann::json::object());
}

void DevicePageFilamentLoadVM::ReportState(const std::string& submod, const std::string& action)
{
    if (!m_bridge) return;

    nlohmann::json state = BuildState();

    std::string state_key;
    try {
        state_key = state.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "filament load state.dump() failed: " << e.what();
        return;
    }
    if (state_key == m_last_report_state) return;

    m_last_report_state = state_key;
    m_bridge->ReportMsg(MakeResponse(GetModule(), submod, action, 0, "", state));
}

void DevicePageFilamentLoadVM::OnResponseSent(const nlohmann::json& response)
{
    const auto payload_it = response.find("payload");
    if (payload_it == response.end()) return;
    try {
        m_last_report_state = payload_it->dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    } catch (...) {
        m_last_report_state.clear();
    }
}

void DevicePageFilamentLoadVM::OnSysColorChanged()
{
    if (!m_bridge) return;
    const bool dark = wxGetApp().app_config->get("dark_color_mode") == "1";
    nlohmann::json data;
    data["theme"] = dark ? "dark" : "light";
    m_bridge->ReportMsg(MakeResponse(GetModule(), "init", "theme_changed", 0, "", data));
}

}} // namespace Slic3r::GUI
