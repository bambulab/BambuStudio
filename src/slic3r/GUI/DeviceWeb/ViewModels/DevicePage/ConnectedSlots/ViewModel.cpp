/*****************************************************************//**
 * \file   ViewModel.cpp
 * \brief  Command dispatch and state reporting for the Connected Slots dialog.
 *
 * BuildState() lives in ViewModelState.cpp, the `action` handlers in
 * ViewModelActions.cpp.
 *********************************************************************/
#include "ViewModel.hpp"
#include "Schema.hpp"
#include "ViewModelActions.hpp"

#include "slic3r/GUI/DeviceWeb/DeviceWebBridge.hpp"
#include "slic3r/GUI/GUI_App.hpp"

#include <string>
#include "libslic3r/Log.hpp"

namespace Slic3r { namespace GUI {

namespace SK = ConnectedSlotsSchema::keys;

namespace {

/// Every response and report carries the host theme so the page can paint before
/// the separate theme report arrives.
void stamp_theme(nlohmann::json& state)
{
    const bool dark = wxGetApp().app_config && wxGetApp().app_config->get("dark_color_mode") == "1";
    state["theme"] = dark ? "dark" : "light";
}

} // namespace

nlohmann::json ConnectedSlotsVM::OnCommand(const std::string& submod,
                                           const std::string& action,
                                           const nlohmann::json& payload)
{
    try {
        if (submod == "state" && (action == "get" || action == "init")) {
            if (payload.contains("addr_id") && payload["addr_id"].is_number_integer()) {
                m_addr_id = payload["addr_id"].get<int>();
            }
            nlohmann::json state = BuildState();
            stamp_theme(state);
            return MakeResponse(GetModule(), submod, action, 0, "", state);
        }

        if (submod == "source" && action == "select") {
            if (payload.contains(SK::source_select::ext_index) && payload[SK::source_select::ext_index].is_number_integer()) {
                m_selected_ext_index = payload[SK::source_select::ext_index].get<int>();
            }
            nlohmann::json state = BuildState();
            stamp_theme(state);
            return MakeResponse(GetModule(), submod, action, 0, "", state);
        }

        if (submod == "action") {
            return ConnectedSlotsActions::HandleTrayAction(*this, m_obj, action, payload);
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "exception: " << e.what();
        return MakeResponse(GetModule(), submod, action, 2, e.what(), BuildState());
    }

    return MakeResponse(GetModule(), submod, action, 1,
                        "unknown connected slots command", BuildState());
}

void ConnectedSlotsVM::ReportState(const std::string& submod, const std::string& action)
{
    ReportStateImpl(submod, action, /*force=*/false);
}

void ConnectedSlotsVM::ReportStateImpl(const std::string& submod, const std::string& action, bool force)
{
    if (!m_bridge) return;

    nlohmann::json state = BuildState();
    stamp_theme(state);

    std::string state_key;
    try {
        state_key = state.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "state.dump() failed: " << e.what();
        return;
    }
    if (state_key == m_last_report_state) return;

    const nlohmann::json body = MakeResponse(GetModule(), submod, action, 0, "", state);
    // Only remember what actually left: a report the visibility gate suppressed
    // has to be retried on the next tick, not recorded as already delivered.
    const bool sent = force ? m_bridge->ReportMsgForce(body) : m_bridge->ReportMsg(body);
    if (sent) {
        m_last_report_state = state_key;
    }
}

void ConnectedSlotsVM::OnSysColorChanged()
{
    ReportState("state", "theme_changed");
}

void ConnectedSlotsVM::Refresh(MachineObject* obj, int addr_id)
{
    m_obj = obj;
    m_addr_id = addr_id;
    ++m_session_seq;
    // A freshly opened dialog follows the live feed source again.
    m_selected_ext_index = -1;

    m_last_report_state.clear();
    ReportStateImpl("state", "refresh", /*force=*/true);
}

void ConnectedSlotsVM::UpdateByMachine(MachineObject* obj)
{
    m_obj = obj;
    ReportState("state", "changed");
}

}} // namespace Slic3r::GUI
