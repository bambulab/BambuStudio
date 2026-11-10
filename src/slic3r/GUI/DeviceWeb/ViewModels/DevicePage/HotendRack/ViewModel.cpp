#include "ViewModel.hpp"

#include "ViewModelActions.hpp"
#include "ViewModelStateBuilder.hpp"

#include "slic3r/GUI/DeviceWeb/DeviceWebBridge.hpp"
#include "slic3r/GUI/DeviceWeb/DeviceWebHost.hpp"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"
#include "slic3r/GUI/DeviceTab/wgtDeviceNozzleRackUpdate.h"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/wxExtensions.hpp"

#include "libslic3r/Log.hpp"

#include <chrono>
#include <cstdint>
#include <utility>

#include <wx/sizer.h>

namespace Slic3r { namespace GUI {

#if !BBL_RELEASE_TO_PUBLIC
namespace {

constexpr int kDebugDialogWidthDip     = 900;
constexpr int kDebugDialogHeightDip    = 700;
constexpr int kDebugDialogMinWidthDip  = 720;
constexpr int kDebugDialogMinHeightDip = 520;

class HotendRackWebDebugDialog : public DPIDialog
{
public:
    explicit HotendRackWebDebugDialog(wxWindow* parent)
        : DPIDialog(parent, wxID_ANY, _L("Hotend Rack Web Debug"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX | wxRESIZE_BORDER)
    {
        SetClientSize(wxSize(FromDIP(kDebugDialogWidthDip), FromDIP(kDebugDialogHeightDip)));
        SetMinClientSize(wxSize(FromDIP(kDebugDialogMinWidthDip), FromDIP(kDebugDialogMinHeightDip)));
    }

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override { SetSize(suggested_rect.GetSize()); }
};

// Handed over from the page VM to the dialog VM: the two live on different DeviceWebHost instances.
nlohmann::json& hotend_rack_debug_snapshot()
{
    static nlohmann::json snapshot = nlohmann::json::object();
    return snapshot;
}

std::int64_t now_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

void open_hotend_rack_debug_dialog()
{
    HotendRackWebDebugDialog dlg(wxGetApp().mainframe);
    auto* host = new DeviceWebHost(&dlg, DeviceWebHostMode::DevicePageHotendRack, "/device_page/hotend_rack_debug");
    host->SetMinSize(wxSize(dlg.FromDIP(kDebugDialogMinWidthDip), dlg.FromDIP(kDebugDialogMinHeightDip)));
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(host, 1, wxEXPAND);
    dlg.SetSizer(sizer);
    dlg.Layout();
    dlg.ShowModal();
}

} // namespace
#endif

nlohmann::json DevicePageHotendRackVM::OnCommand(const std::string& submod,
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
        BOOST_LOG_TRIVIAL(error) << "hotend rack web exception: " << e.what();
        return MakeResponse(GetModule(), submod, action, 2, e.what(), BuildState());
    }

    return MakeResponse(GetModule(), submod, action, 1, "unknown hotend rack command", BuildState());
}

nlohmann::json DevicePageHotendRackVM::HandleAction(const std::string& action, const nlohmann::json& payload)
{
    MachineObject* machine_obj = wxGetApp().getDeviceManager()
        ? wxGetApp().getDeviceManager()->get_selected_machine()
        : nullptr;

    if (auto handled = HotendRackActionHandler::TryHandle(*this, action, payload, machine_obj))
        return std::move(handled.value());

    return MakeResponse(GetModule(), "action", action, 1, "unknown hotend rack action", BuildState());
}

nlohmann::json DevicePageHotendRackVM::BuildState() const
{
    MachineObject* machine_obj = wxGetApp().getDeviceManager()
        ? wxGetApp().getDeviceManager()->get_selected_machine()
        : nullptr;
    return HotendRack::BuildState(machine_obj, m_b_row_first);
}

nlohmann::json DevicePageHotendRackVM::BuildDebugSnapshot(const nlohmann::json& payload) const
{
    nlohmann::json snapshot;
    snapshot["created_ts"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    snapshot["cpp_state"] = BuildState();
    snapshot["web_state"] = payload.value("web_state", nlohmann::json::object());
    snapshot["cpp_trace"] = m_bridge ? m_bridge->RecentTrace() : nlohmann::json::array();
    return snapshot;
}

nlohmann::json DevicePageHotendRackVM::HandleDebug(const std::string& action, const nlohmann::json& payload)
{
#if !BBL_RELEASE_TO_PUBLIC
    if (action == "open_bridge_trace") {
        hotend_rack_debug_snapshot() = BuildDebugSnapshot(payload);
        wxGetApp().CallAfter([]() { open_hotend_rack_debug_dialog(); });
        return MakeResponse(GetModule(), "debug", action, 0, "", hotend_rack_debug_snapshot());
    }
    if (action == "snapshot") {
        nlohmann::json snapshot = hotend_rack_debug_snapshot();
        snapshot["dialog_snapshot_ts"] = now_ms();
        snapshot["dialog_cpp_state"] = BuildState();
        snapshot["dialog_cpp_trace"] = m_bridge ? m_bridge->RecentTrace() : nlohmann::json::array();
        return MakeResponse(GetModule(), "debug", action, 0, "", snapshot);
    }
#else
    (void) payload;
#endif
    return MakeResponse(GetModule(), "debug", action, 1, "unknown debug action", nlohmann::json::object());
}

void DevicePageHotendRackVM::RefreshHotendsInfoDialog()
{
    if (!m_hotends_info_dlg || !m_hotends_info_dlg->IsShown()) return;

    MachineObject* machine_obj = wxGetApp().getDeviceManager()
        ? wxGetApp().getDeviceManager()->get_selected_machine()
        : nullptr;
    if (auto rack = machine_obj ? machine_obj->GetNozzleRack() : nullptr)
        m_hotends_info_dlg->UpdateRackInfo(rack);
}

void DevicePageHotendRackVM::ReportState(const std::string& submod, const std::string& action)
{
    RefreshHotendsInfoDialog();
    if (!m_bridge) return;

    nlohmann::json state = BuildState();

    std::string state_key;
    try {
        state_key = state.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "hotend rack state.dump() failed: " << e.what();
        return;
    }
    if (state_key == m_last_report_state) return;

    m_last_report_state = state_key;
    m_bridge->ReportMsg(MakeResponse(GetModule(), submod, action, 0, "", state));
}

void DevicePageHotendRackVM::OnResponseSent(const nlohmann::json& response)
{
    const auto payload_it = response.find("payload");
    if (payload_it == response.end()) return;
    try {
        m_last_report_state = payload_it->dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    } catch (...) {
        BOOST_LOG_TRIVIAL(warning) << Slic3r::current_exception_message();
        m_last_report_state.clear();
    }
}

void DevicePageHotendRackVM::OnSysColorChanged()
{
    if (!m_bridge) return;
    const bool dark = wxGetApp().app_config->get("dark_color_mode") == "1";
    nlohmann::json data;
    data["theme"] = dark ? "dark" : "light";
    m_bridge->ReportMsg(MakeResponse(GetModule(), "init", "theme_changed", 0, "", data));
}

}} // namespace Slic3r::GUI
