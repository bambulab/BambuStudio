/*****************************************************************//**
 * \file   ViewModel.cpp
 * \brief  Command dispatch and state reporting of the Select Filament dialog.
 *
 * Business rules live in ViewModelBuilder.cpp (state) and
 * ViewModelActions.cpp (confirm / close); this file only routes.
 *********************************************************************/
#include "ViewModel.hpp"
#include "ViewModelActions.hpp"

#include "slic3r/GUI/DeviceWeb/DeviceWebBridge.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "libslic3r/LogKeys.hpp"

#include <chrono>
#include <string>
#include "libslic3r/Log.hpp"

namespace Slic3r { namespace GUI {

namespace {

bool host_dark_mode()
{
    return wxGetApp().app_config && wxGetApp().app_config->get("dark_color_mode") == "1";
}

} // namespace

std::string FilamentSelectVM::GetModule() const
{
    return FilamentSelectSchema::module_name;
}

void FilamentSelectVM::Open(const FilamentSelectInput& in, FilamentSelectResult* result_out, CloseHandler close_handler)
{
    m_snapshot      = FilamentSelectBuilder::BuildSnapshot(in);
    m_result        = result_out;
    m_close_handler = std::move(close_handler);
    m_opened        = true;

    BOOST_LOG_TRIVIAL(info) << "filament select web dialog opened "
                            << KV(log_key::spool_count, m_snapshot.rows.rows.size())
                            << KV(log_key::preset_count, m_snapshot.filament_items.size());
}

nlohmann::json FilamentSelectVM::BuildStateJson() const
{
    if (!m_opened) {
        // Not opened yet: an empty but well-formed state keeps the page stable.
        FilamentSelectSchema::format::State empty;
        empty.theme = host_dark_mode() ? FilamentSelectSchema::values::theme::dark
                                       : FilamentSelectSchema::values::theme::light;
        return empty;
    }
    return FilamentSelectBuilder::BuildState(m_snapshot, host_dark_mode());
}

void FilamentSelectVM::RequestClose(int code) const
{
    if (m_close_handler) m_close_handler(code);
}

nlohmann::json FilamentSelectVM::OnCommand(const std::string& submod,
                                           const std::string& action,
                                           const nlohmann::json& payload)
{
    try {
        if (submod == "state" && (action == "get" || action == "init"))
            return MakeResponse(GetModule(), submod, action, 0, "", BuildStateJson());

        if (submod == "action")
            return FilamentSelectActions::Handle(*this, action, payload);

#if !BBL_RELEASE_TO_PUBLIC
        if (submod == "debug")
            return HandleDebug(action, payload);
#endif
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "filament select command failed: " << e.what();
        return MakeResponse(GetModule(), submod, action, 2, e.what());
    }

    BOOST_LOG_TRIVIAL(warning) << "unknown filament select command " << submod << "/" << action;
    return MakeResponse(GetModule(), submod, action, 1, "unknown filament select command");
}

void FilamentSelectVM::ReportState(const std::string& submod, const std::string& action)
{
    if (!m_bridge) return;
    m_bridge->ReportMsg(MakeResponse(GetModule(), submod, action, 0, "", BuildStateJson()));
}

void FilamentSelectVM::OnSysColorChanged()
{
    // The snapshot itself never changes; this only carries the new theme.
    ReportState("state", "theme_changed");
}

#if !BBL_RELEASE_TO_PUBLIC
nlohmann::json FilamentSelectVM::HandleDebug(const std::string& action, const nlohmann::json& payload)
{
    if (action == "snapshot") {
        nlohmann::json snapshot;
        snapshot["created_ts"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        snapshot["cpp_state"] = BuildStateJson();
        snapshot["web_state"] = payload.value("web_state", nlohmann::json::object());
        snapshot["cpp_trace"] = m_bridge ? m_bridge->RecentTrace() : nlohmann::json::array();
        return MakeResponse(GetModule(), "debug", action, 0, "", snapshot);
    }
    return MakeResponse(GetModule(), "debug", action, 1, "unknown debug action");
}
#endif

}} // namespace Slic3r::GUI
