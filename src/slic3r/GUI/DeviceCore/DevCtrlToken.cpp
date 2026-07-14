#include "DevCtrlToken.h"

#include <utility>

#include <wx/app.h>

namespace Slic3r {

DevCtrlToken::DevCtrlToken(std::string sequence_id, int timeout_ms)
    : m_sequence_id(std::move(sequence_id)), m_timeout_ms(timeout_ms)
{
    m_timer.Bind(wxEVT_TIMER, [this](wxTimerEvent&) { SetStatus(DevCtrlTokenStatus::TimedOut); });
}

std::shared_ptr<DevCtrlToken> DevCtrlToken::Create(std::string sequence_id, int timeout_ms)
{
    return std::shared_ptr<DevCtrlToken>(new DevCtrlToken(std::move(sequence_id), timeout_ms));
}

DevCtrlToken::~DevCtrlToken()
{
    Reset();
}

void DevCtrlToken::SetCallback(DevCtrlTokenStatus status, Callback callback)
{
    m_callbacks[status] = std::move(callback);
    if (m_active && m_status == status)
        Notify(status);
}

void DevCtrlToken::SetSendResult(int result)
{
    if (!m_active)
        return;

    if (result != 0) {
        SetStatus(DevCtrlTokenStatus::SendFailed);
        return;
    }

    SetStatus(DevCtrlTokenStatus::SendSucceeded);
    SetStatus(DevCtrlTokenStatus::WaitingReply);
    if (!m_timer.StartOnce(m_timeout_ms))
        SetStatus(DevCtrlTokenStatus::TimedOut);
}

bool DevCtrlToken::MatchesReply(const nlohmann::json& reply) const
{
    if (!reply.contains("sequence_id"))
        return false;

    std::string sequence_id;
    if (reply["sequence_id"].is_string())
        sequence_id = reply["sequence_id"].get<std::string>();
    else if (reply["sequence_id"].is_number_integer())
        sequence_id = std::to_string(reply["sequence_id"].get<long long>());
    else
        return false;

    return sequence_id == m_sequence_id;
}

bool DevCtrlToken::TryParseReply(const nlohmann::json& reply)
{
    if (!m_active || !MatchesReply(reply))
        return false;

    m_timer.Stop();
    m_reply = reply;
    SetStatus(DevCtrlTokenStatus::ReplyReceived);
    return true;
}

void DevCtrlToken::Reset()
{
    m_active = false;
    m_timer.Stop();
    m_callbacks.clear();
}

void DevCtrlToken::SetStatus(DevCtrlTokenStatus status)
{
    if (!m_active)
        return;
    if (m_status == DevCtrlTokenStatus::ReplyReceived || m_status == DevCtrlTokenStatus::SendFailed ||
        m_status == DevCtrlTokenStatus::TimedOut)
        return;

    m_status = status;
    Notify(status);
}

void DevCtrlToken::Notify(DevCtrlTokenStatus status)
{
    const auto it = m_callbacks.find(status);
    if (it == m_callbacks.end() || !it->second)
        return;

    const std::weak_ptr<DevCtrlToken> weak_self = weak_from_this();
    const auto callback = it->second;
    wxTheApp->CallAfter([weak_self, callback, status]() {
        if (const auto self = weak_self.lock(); self && self->m_active)
            callback(self, status);
    });
}

} // namespace Slic3r
