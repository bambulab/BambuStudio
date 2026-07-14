#pragma once

#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Slic3r {

class DevCtrlToken;

// Owns the lifecycle of a MachineObject's outstanding device-command tokens.
// Not thread-safe: all methods are expected to run on the UI thread.
class DevCtrlTokenMgr
{
public:
    DevCtrlTokenMgr();
    ~DevCtrlTokenMgr();

    // Create and register a token keyed by the command's sequence_id.
    std::shared_ptr<DevCtrlToken> Build(const std::string& sequence_id, int timeout_ms = 5000);

    // Route a command reply to the matching token and drop tokens nobody waits on.
    void DispatchReply(const nlohmann::json& reply);

    // Invalidate and clear all pending tokens (e.g. on device reset).
    void Reset();

    bool Empty() const { return m_tokens.empty(); }

private:
    std::vector<std::shared_ptr<DevCtrlToken>> m_tokens;
};

} // namespace Slic3r
