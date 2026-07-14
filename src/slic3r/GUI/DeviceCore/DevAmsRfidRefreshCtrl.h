#pragma once

#include <memory>
#include <optional>

#include <nlohmann/json.hpp>
#include "DevCtrlToken.h"

namespace Slic3r {

class DevFilaSystem;

enum class DevAmsRfidProbeStatus
{
    NeedRefresh,      // reply parsed: refresh required
    NoRefreshNeeded,  // reply parsed: refresh not required
    SendFailed,       // command failed to publish
    TimedOut,         // no reply within the token timeout
    Unavailable,      // reply received but the "try" field was missing/unparseable
};

class DevAmsRfidRefreshCtrl : public std::enable_shared_from_this<DevAmsRfidRefreshCtrl>
{
public:
    static std::shared_ptr<DevAmsRfidRefreshCtrl> Create(DevFilaSystem *owner);
    ~DevAmsRfidRefreshCtrl();
    void Detach();

public:
    int TriggerRefresh();

    // Publish a try-probe to the first eligible AMS and return the token.
    std::shared_ptr<DevCtrlToken> ProbeRefreshNeed();

    // Translate an "ams_get_slots_rfid" try-reply into a probe status.
    static DevAmsRfidProbeStatus ParseProbeReply(const nlohmann::json &reply);

private:
    explicit DevAmsRfidRefreshCtrl(DevFilaSystem *owner);

    std::optional<int> CollectTargetAmsId() const;
    bool               CanRefresh() const;
    int                PublishAutoRefresh(int ams_id);
    std::shared_ptr<DevCtrlToken> PublishProbe(int ams_id);

private:
    DevFilaSystem *m_owner{nullptr};
};

} // namespace Slic3r
