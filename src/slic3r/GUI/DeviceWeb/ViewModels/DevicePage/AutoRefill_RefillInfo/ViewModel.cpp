/*****************************************************************//**
 * \file   ViewModel.cpp
 * \brief  Command dispatch and state reporting for the RefillInfo dialog.
 *********************************************************************/
#include "ViewModel.hpp"

#include "refill_info/Actions.hpp"
#include "refill_info/Builder.hpp"
#include "refill_info/Schema.hpp"
#include "add_refill_filaments/Actions.hpp"
#include "add_refill_filaments/Builder.hpp"

#include "slic3r/GUI/DeviceWeb/DeviceWebBridge.hpp"
#include "slic3r/GUI/DeviceWeb/ViewModels/DevicePage/DevicePageDialogHelpers.h"

#include "slic3r/GUI/DeviceCore/DevCtrlToken.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevFilaAutoRefillMgr.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "libslic3r_version.h"

#include <exception>
#include <utility>
#include "libslic3r/Log.hpp"

namespace Slic3r { namespace GUI {

namespace {

/// Stamp the resolved Studio colour mode onto a first-paint response so the
/// page picks its palette immediately instead of flashing the default theme
/// while it waits for the separate `init/theme_changed` Report.
void stamp_theme(nlohmann::json& state)
{
    const bool dark = wxGetApp().app_config && wxGetApp().app_config->get("dark_color_mode") == "1";
    state["theme"] = dark ? "dark" : "light";
}

} // namespace

AutoRefillRefillInfoVM::~AutoRefillRefillInfoVM()
{
    *m_alive = false;
    ClearAddRefillQuery();
    ClearRefillEdit();
}

nlohmann::json AutoRefillRefillInfoVM::BuildDebugSnapshot() const
{
    nlohmann::json snapshot;
    snapshot["session_seq"] = m_session_seq;

    // Raw device reply, kept even when parsing failed
    snapshot["query_reply"] = m_add_refill_query_reply;

    // The parsed refill_command 5 / 6 reply the pages were built from
    if (m_add_refill_query_result) {
        nlohmann::json slots = nlohmann::json::array();
        for (const auto& slot : m_add_refill_query_result->slots) {
            nlohmann::json codes = nlohmann::json::array();
            for (const auto code : slot.block_codes) codes.push_back(static_cast<int>(code));
            slots.push_back({{"ams_id", slot.slot_id.first},
                             {"slot_id", slot.slot_id.second},
                             {"block_codes", codes}});
        }
        snapshot["query_result"] = {{"ack_result", static_cast<int>(m_add_refill_query_result->ack_result)},
                                    {"slots", slots}};
    } else {
        snapshot["query_result"] = nullptr;
    }

    // Picker request replayed into the built state; empty for the initial Query
    snapshot["query_payload"] = m_add_refill_query_payload;

    // Exactly what the two pages are told to render
    snapshot["refill_info"] = AutoRefillRefillInfoBuilder::Build(m_obj, m_session_seq);
    snapshot["add_refill_filaments"] =
        AutoRefillAddRefillFilamentsBuilder::Build(m_obj, m_session_seq, m_add_refill_query_payload,
                                                   m_add_refill_query_result ? &*m_add_refill_query_result : nullptr);
    return snapshot;
}

nlohmann::json AutoRefillRefillInfoVM::OnCommand(const std::string& submod,
                                                 const std::string& action,
                                                 const nlohmann::json& payload)
{
    try {
        if (submod == "state" && (action == "get" || action == "init")) {
            nlohmann::json state = AutoRefillRefillInfoBuilder::Build(m_obj, m_session_seq);
            stamp_theme(state);
            return MakeResponse(GetModule(), submod, action, 0, "", state);
        }
        if (submod == "action") {
            return AutoRefillRefillInfoActions::Handle(*this, m_obj, action, payload);
        }
        if (submod == "debug") {
#if !BBL_RELEASE_TO_PUBLIC
            if (action == "snapshot") {
                return MakeResponse(GetModule(), submod, action, 0, "", BuildDebugSnapshot());
            }
#endif
            return MakeResponse(GetModule(), submod, action, 1, "debug is unavailable");
        }
        if (submod == "add_refill_filaments") {
            if (action == "get" || action == "init") {
                // The picker runs in its own WebView host, so it needs the theme
                // stamped on its own first-paint response.
                nlohmann::json state = AutoRefillAddRefillFilamentsBuilder::Build(
                    m_obj, m_session_seq, payload,
                    m_add_refill_query_result ? &*m_add_refill_query_result : nullptr);
                stamp_theme(state);
                return MakeResponse(GetModule(), submod, action, 0, "", state);
            }
            return AutoRefillAddRefillFilamentsActions::Handle(*this, m_obj, action, payload);
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "exception: " << e.what();
        return MakeResponse(GetModule(), submod, action, 2, e.what(),
                            AutoRefillRefillInfoBuilder::Build(m_obj, m_session_seq));
    }

    return MakeResponse(GetModule(), submod, action, 1,
                        "unknown auto refill command",
                        AutoRefillRefillInfoBuilder::Build(m_obj, m_session_seq));
}

void AutoRefillRefillInfoVM::ReportState(const std::string& submod, const std::string& action)
{
    ReportStateImpl(submod, action, /*force=*/false);
}

void AutoRefillRefillInfoVM::ReportStateImpl(const std::string& submod, const std::string& action, bool force)
{
    if (!m_bridge) return;

    nlohmann::json state = AutoRefillRefillInfoBuilder::Build(m_obj, m_session_seq);

    std::string state_key;
    try {
        state_key = state.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "state.dump() failed: " << e.what();
        return;
    }
    if (state_key == m_last_report_state) return;

    m_last_report_state = state_key;
    const nlohmann::json body = MakeResponse(GetModule(), submod, action, 0, "", state);
    if (force) {
        m_bridge->ReportMsgForce(body);
    } else {
        m_bridge->ReportMsg(body);
    }
}

bool AutoRefillRefillInfoVM::StartAddRefillQuery()
{
    ClearAddRefillQuery();

    if (!m_obj || !m_obj->GetFilaSystem()) {
        return false;
    }

    return WatchAddRefillQuery(m_obj->GetFilaSystem()->GetAutoRefillMgr().CtrlQuery());
}

bool AutoRefillRefillInfoVM::StartAddRefillQueryNext(const DevAmsSlotId& to, const nlohmann::json& picker_payload)
{
    if (!m_obj || !m_obj->GetFilaSystem()) {
        return false;
    }

    // The previous result is about to be replaced, so drop it before the new
    // command goes out; the selection is kept and replayed when reporting.
    ClearAddRefillQuery();
    m_add_refill_query_payload = picker_payload;

    return WatchAddRefillQuery(m_obj->GetFilaSystem()->GetAutoRefillMgr().CtrlQueryNext(to));
}

bool AutoRefillRefillInfoVM::WatchAddRefillQuery(std::shared_ptr<DevCtrlToken> token)
{
    if (!token || token->GetStatus() == DevCtrlTokenStatus::SendFailed) {
        return false;
    }
    m_add_refill_query_token = std::move(token);

    auto on_settled = [this](const std::shared_ptr<DevCtrlToken>& settled, DevCtrlTokenStatus status) {
        // A newer query, or a machine switch, already replaced this token.
        if (settled != m_add_refill_query_token) {
            return;
        }
        m_add_refill_query_token.reset();

        if (status != DevCtrlTokenStatus::ReplyReceived || !settled->GetReply()) {
            ReportAddRefillQuery("query_failed", 1, "refill query got no reply");
            return;
        }

        const nlohmann::json& reply = *settled->GetReply();
        m_add_refill_query_reply    = reply;// kept before parsing for the debug snapshot
        auto parsed = DevFilaAutoRefillEdit(m_obj->GetFilaSystem().get()).ParseQueryResult(reply);
        if (!parsed) {
            const auto ack_result = DevFilaRefillParseAckResult(reply);
            ReportAddRefillQuery("query_failed", 1,
                                 ack_result ? DevFilaRefillAckResultMessage(*ack_result)
                                            : "refill query reply is invalid");
            return;
        }

        m_add_refill_query_result = std::move(parsed);
        ReportAddRefillQuery("query_result", 0, "");
    };

    m_add_refill_query_token->SetCallback(DevCtrlTokenStatus::ReplyReceived, on_settled);
    m_add_refill_query_token->SetCallback(DevCtrlTokenStatus::SendFailed, on_settled);
    m_add_refill_query_token->SetCallback(DevCtrlTokenStatus::TimedOut, on_settled);
    return true;
}

void AutoRefillRefillInfoVM::ClearAddRefillQuery()
{
    if (m_add_refill_query_token) {
        m_add_refill_query_token->Reset();
        m_add_refill_query_token.reset();
    }
    m_add_refill_query_result.reset();
    m_add_refill_query_reply   = nlohmann::json::object();
    m_add_refill_query_payload = nlohmann::json::object();
}

void AutoRefillRefillInfoVM::ReportAddRefillQuery(const std::string& action, int code, const std::string& msg)
{
    if (!m_bridge) return;

    // Without a query result there is no trustworthy slot list to send, and
    // the web side only leaves the loading state on the failure itself.
    const nlohmann::json state = m_add_refill_query_result
        ? AutoRefillAddRefillFilamentsBuilder::Build(m_obj, m_session_seq, m_add_refill_query_payload,
                                                     &*m_add_refill_query_result)
        : nlohmann::json::object();

    // The user is waiting on this result, so it must not be dropped by the
    // hidden-page gate the periodic state reports rely on.
    m_bridge->ReportMsgForce(MakeResponse(GetModule(), "add_refill_filaments", action, code, msg, state));
}

bool AutoRefillRefillInfoVM::StartConfirmAddRefill(const std::vector<DevAmsSlotId>& slots)
{
    if (!m_obj || !m_obj->GetFilaSystem() || slots.empty()) {
        return false;
    }

    ClearRefillEdit();

    // The first picked spool heads the group; the rest ride along in the same
    // command, so a single ACK settles the whole creation.
    const DevAmsSlotId head = slots.front();
    const std::vector<DevAmsSlotId> members(slots.begin() + 1, slots.end());
    return WatchRefillEdit(
        m_obj->GetFilaSystem()->GetAutoRefillMgr().CtrlCreateGroup(head, members),
        "confirm");
}

bool AutoRefillRefillInfoVM::StartAddRefillToGroup(const DevAmsSlotId& group_member,
                                                   const std::vector<DevAmsSlotId>& slots)
{
    if (!m_obj || !m_obj->GetFilaSystem() || slots.empty()) {
        return false;
    }

    ClearRefillEdit();
    m_add_to_group_member  = group_member;
    m_add_to_group_pending = slots;
    return ContinueAddRefillToGroup();
}

bool AutoRefillRefillInfoVM::ContinueAddRefillToGroup()
{
    if (!m_obj || !m_obj->GetFilaSystem() || !m_add_to_group_member.has_value() ||
        m_add_to_group_pending.empty()) {
        return false;
    }

    const DevAmsSlotId slot = m_add_to_group_pending.front();
    m_add_to_group_pending.erase(m_add_to_group_pending.begin());
    return WatchRefillEdit(
        m_obj->GetFilaSystem()->GetAutoRefillMgr().CtrlPushBack(slot, *m_add_to_group_member),
        "append");
}

bool AutoRefillRefillInfoVM::StartResetGroup(const std::string& group_tag)
{
    if (!m_obj || !m_obj->GetFilaSystem() || group_tag.empty()) {
        return false;
    }
    return WatchRefillEdit(m_obj->GetFilaSystem()->GetAutoRefillMgr().CtrlReset(group_tag), "reset");
}

bool AutoRefillRefillInfoVM::StartRemoveSlot(const DevAmsSlotId& slot)
{
    if (!m_obj || !m_obj->GetFilaSystem()) {
        return false;
    }
    return WatchRefillEdit(m_obj->GetFilaSystem()->GetAutoRefillMgr().CtrlRemove(slot), "remove");
}

void AutoRefillRefillInfoVM::FinishResetConfirmation(const std::string& group_tag, bool confirmed)
{
    namespace SK = AutoRefillRefillInfoSchema::keys;
    namespace SV = AutoRefillRefillInfoSchema::values;

    if (!m_bridge) return;

    if (!confirmed) {
        m_bridge->ReportMsgForce(
            MakeResponse(GetModule(), "action", SV::action::reset_cancelled, 0, ""));
        return;
    }

    // Announce the pending reset before publishing so the page can switch to
    // the updating state and remember which success toast to show.
    m_bridge->ReportMsgForce(MakeResponse(GetModule(), "action", SV::action::reset_started, 0, "",
                                          nlohmann::json{{SK::confirm_remove::toast,
                                                          "Reset successfully"}}));

    if (!StartResetGroup(group_tag)) {
        ReportRefillEdit(SV::action::reset, 1, "publish failed");
    }
}

void AutoRefillRefillInfoVM::FinishRemoveConfirmation(const DevAmsSlotId& slot,
                                                      bool               confirmed,
                                                      const std::string& toast_key)
{
    namespace SK = AutoRefillRefillInfoSchema::keys;
    namespace SV = AutoRefillRefillInfoSchema::values;

    if (!m_bridge) return;

    if (!confirmed) {
        m_bridge->ReportMsgForce(
            MakeResponse(GetModule(), "action", SV::action::remove_cancelled, 0, ""));
        return;
    }

    // Announce the pending removal before publishing so the page can switch to
    // the updating state and remember which success toast to show.
    m_bridge->ReportMsgForce(MakeResponse(GetModule(), "action", SV::action::remove_started, 0, "",
                                          nlohmann::json{{SK::confirm_remove::toast, toast_key}}));

    if (!StartRemoveSlot(slot)) {
        ReportRefillEdit(SV::action::remove, 1, "publish failed");
    }
}

bool AutoRefillRefillInfoVM::StartMoveSlot(const DevAmsSlotId& slot, const DevAmsSlotId& member, bool as_head)
{
    if (!m_obj || !m_obj->GetFilaSystem() || slot == member) {
        return false;
    }
    return WatchRefillEdit(
        m_obj->GetFilaSystem()->GetAutoRefillMgr().CtrlMove(slot, member, as_head), "edit");
}

bool AutoRefillRefillInfoVM::WatchRefillEdit(std::shared_ptr<DevCtrlToken> token, const std::string& action)
{
    if (m_refill_edit_token) {
        m_refill_edit_token->Reset();
        m_refill_edit_token.reset();
    }
    if (!token || token->GetStatus() == DevCtrlTokenStatus::SendFailed) {
        return false;
    }
    m_refill_edit_token = std::move(token);

    auto on_settled = [this, action](const std::shared_ptr<DevCtrlToken>& settled, DevCtrlTokenStatus status) {
        if (settled != m_refill_edit_token) {
            return;
        }
        m_refill_edit_token.reset();

        if (status != DevCtrlTokenStatus::ReplyReceived || !settled->GetReply()) {
            ReportRefillEdit(action, 1, "refill command got no reply");
            return;
        }

        const auto ack_result = DevFilaRefillParseAckResult(*settled->GetReply());
        if (!ack_result || !DevFilaRefillAckSucceeded(*ack_result)) {
            ReportRefillEdit(action, 1,
                             ack_result ? DevFilaRefillAckResultMessage(*ack_result)
                                        : "refill command reply is invalid");
            return;
        }

        if (action == "append" && !m_add_to_group_pending.empty()) {
            if (!ContinueAddRefillToGroup()) {
                ReportRefillEdit(action, 1, "failed to append remaining slots");
            }
            return;
        }

        ReportRefillEdit(action, 0, "");
    };

    m_refill_edit_token->SetCallback(DevCtrlTokenStatus::ReplyReceived, on_settled);
    m_refill_edit_token->SetCallback(DevCtrlTokenStatus::SendFailed, on_settled);
    m_refill_edit_token->SetCallback(DevCtrlTokenStatus::TimedOut, on_settled);
    return true;
}

void AutoRefillRefillInfoVM::ClearRefillEdit()
{
    if (m_refill_edit_token) {
        m_refill_edit_token->Reset();
        m_refill_edit_token.reset();
    }
    m_add_to_group_member.reset();
    m_add_to_group_pending.clear();
}

void AutoRefillRefillInfoVM::ReportRefillEdit(const std::string& action, int code, const std::string& msg)
{
    if (!m_bridge) return;

    if (action == "confirm" || action == "append") {
        m_add_to_group_member.reset();
        m_add_to_group_pending.clear();
        if (code == 0) {
            ClearAddRefillQuery();
            m_last_report_state.clear();
            const nlohmann::json state = AutoRefillRefillInfoBuilder::Build(m_obj, m_session_seq);
            m_bridge->ReportMsgForce(MakeResponse(GetModule(), "add_refill_filaments", "confirm_result", 0, "", state));
            wxGetApp().CallAfter([]() { CloseAutoRefillFilamentsDialog(wxID_OK); });
        } else {
            ReportAddRefillQuery("confirm_failed", code, msg);
        }
        return;
    }

    m_last_report_state.clear();
    const nlohmann::json state = AutoRefillRefillInfoBuilder::Build(m_obj, m_session_seq);
    m_bridge->ReportMsgForce(MakeResponse(GetModule(), "action", action, code, msg, state));
}

void AutoRefillRefillInfoVM::OnSysColorChanged()
{
    // Theme is global UI state, so it must NOT be routed through ReportState():
    // that path de-dupes against the last Build() snapshot, and toggling dark
    // mode leaves the device payload untouched, so the notification would be
    // dropped. Emit an independent `init/theme_changed` Report instead. One
    // Report reaches both WebView hosts because they share this module.
    if (!m_bridge) return;
    nlohmann::json data;
    stamp_theme(data);
    m_bridge->ReportMsg(MakeResponse(GetModule(), "init", "theme_changed", 0, "", data));
}

void AutoRefillRefillInfoVM::UpdateByMachine(MachineObject* obj)
{
    m_obj = obj;
    ReportState("state", "changed");
}

void AutoRefillRefillInfoVM::Refresh(MachineObject* obj)
{
    m_obj = obj;
    ++m_session_seq;

    ClearAddRefillQuery();
    ClearRefillEdit();

    m_last_report_state.clear();
    ReportStateImpl("state", "refresh", /*force=*/true);
}

}} // namespace Slic3r::GUI
