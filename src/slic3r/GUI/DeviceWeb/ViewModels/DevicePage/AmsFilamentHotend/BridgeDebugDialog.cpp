/*****************************************************************//**
 * \file   BridgeDebugDialog.cpp
 * \brief  Bridge trace viewer of the AMS filament hotend page (debug builds).
 *
 * Snapshots the C++ state and both trace rings, then opens a dialog hosting
 * `/device_page/ams_bridge_debug` to display them side by side with the state
 * and trace the Web side reported.
 *********************************************************************/
#include "ViewModel.hpp"

#include "slic3r/GUI/DeviceWeb/DeviceWebHost.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/wxExtensions.hpp"

#include <chrono>
#include <cstdint>

#include <wx/sizer.h>

namespace Slic3r { namespace GUI {

namespace {

constexpr int kDialogWidthDip     = 900;
constexpr int kDialogHeightDip    = 700;
constexpr int kDialogMinWidthDip  = 720;
constexpr int kDialogMinHeightDip = 520;

class AmsBridgeDebugDialog : public DPIDialog
{
public:
    explicit AmsBridgeDebugDialog(wxWindow* parent)
        : DPIDialog(parent, wxID_ANY, _L("AMS Bridge Debug"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX | wxRESIZE_BORDER)
    {
        SetClientSize(wxSize(FromDIP(kDialogWidthDip), FromDIP(kDialogHeightDip)));
        SetMinClientSize(wxSize(FromDIP(kDialogMinWidthDip), FromDIP(kDialogMinHeightDip)));
    }

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override { SetSize(suggested_rect.GetSize()); }
};

/// Handed over from the page VM to the dialog VM, which cannot rebuild it: the
/// two live on different DeviceWebHost instances.
nlohmann::json& bridge_debug_snapshot()
{
    static nlohmann::json snapshot = nlohmann::json::object();
    return snapshot;
}

std::int64_t now_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

void open_bridge_debug_dialog()
{
    wxWindow* parent = wxGetApp().mainframe;
    AmsBridgeDebugDialog dlg(parent);
    auto* host = new DeviceWebHost(&dlg, DeviceWebHostMode::DevicePageAmsFilamentHotend, "/device_page/ams_bridge_debug");
    host->SetMinSize(wxSize(dlg.FromDIP(kDialogMinWidthDip), dlg.FromDIP(kDialogMinHeightDip)));
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(host, 1, wxEXPAND);
    dlg.SetSizer(sizer);
    dlg.Layout();
    dlg.ShowModal();
}

} // namespace

nlohmann::json DevicePageAmsFilamentHotendVM::BuildDebugSnapshot(const nlohmann::json& payload) const
{
    nlohmann::json snapshot;
    snapshot["created_ts"] = now_ms();
    snapshot["cpp_state"] = BuildState();
    snapshot["web_state"] = payload.value("web_state", nlohmann::json::object());
    snapshot["web_trace"] = payload.value("web_trace", nlohmann::json::array());
    snapshot["cpp_trace"] = m_bridge ? m_bridge->RecentTrace() : nlohmann::json::array();
    return snapshot;
}

nlohmann::json DevicePageAmsFilamentHotendVM::HandleDebug(const std::string& action, const nlohmann::json& payload)
{
#if !BBL_RELEASE_TO_PUBLIC
    if (action == "open_bridge_trace") {
        bridge_debug_snapshot() = BuildDebugSnapshot(payload);
        wxGetApp().CallAfter([]() { open_bridge_debug_dialog(); });
        return MakeResponse(GetModule(), "debug", action, 0, "", bridge_debug_snapshot());
    }
    if (action == "snapshot") {
        nlohmann::json snapshot = bridge_debug_snapshot();
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

}} // namespace Slic3r::GUI
