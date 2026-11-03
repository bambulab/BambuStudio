#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>
#include <wx/timer.h>

namespace Slic3r {

enum class DevCtrlTokenStatus
{
    Created,
    SendSucceeded,
    WaitingReply,
    ReplyReceived,
    SendFailed,
    TimedOut,
};

class DevCtrlToken : public std::enable_shared_from_this<DevCtrlToken>
{
public:
    using Callback = std::function<void(const std::shared_ptr<DevCtrlToken>&, DevCtrlTokenStatus)>;

    static std::shared_ptr<DevCtrlToken> Create(std::string sequence_id, int timeout_ms = 5000);
    ~DevCtrlToken();

public:
    const std::string& GetSequenceId() const { return m_sequence_id; }
    DevCtrlTokenStatus GetStatus() const { return m_status; }
    const std::optional<nlohmann::json>& GetReply() const { return m_reply; }

    void Reset();
    void SetCallback(DevCtrlTokenStatus status, Callback callback);

    void SetSendResult(int result);
    bool MatchesReply(const nlohmann::json& reply) const;
    bool TryParseReply(const nlohmann::json& reply);

protected:
    DevCtrlToken(std::string sequence_id, int timeout_ms);

    void SetStatus(DevCtrlTokenStatus status);
    void Notify(DevCtrlTokenStatus status);

private:
    std::string m_sequence_id;
    int m_timeout_ms{3000};
    DevCtrlTokenStatus m_status{DevCtrlTokenStatus::Created};
    std::optional<nlohmann::json> m_reply;
    std::map<DevCtrlTokenStatus, Callback> m_callbacks;
    wxTimer m_timer;
    bool m_active{true};
};

} // namespace Slic3r
