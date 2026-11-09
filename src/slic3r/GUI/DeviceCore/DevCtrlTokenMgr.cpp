#include "DevCtrlTokenMgr.h"

#include "DevCtrlToken.h"

namespace Slic3r {

DevCtrlTokenMgr::DevCtrlTokenMgr() = default;

DevCtrlTokenMgr::~DevCtrlTokenMgr()
{
    Reset();
}

std::shared_ptr<DevCtrlToken> DevCtrlTokenMgr::Build(const std::string& sequence_id, int timeout_ms)
{
    auto token = DevCtrlToken::Create(sequence_id, timeout_ms);
    m_tokens.push_back(token);
    return token;
}

void DevCtrlTokenMgr::DispatchReply(const nlohmann::json& reply)
{
    for (auto it = m_tokens.begin(); it != m_tokens.end();) {
        // Only the manager still references the token: nobody is waiting, drop it.
        if (it->use_count() <= 1) {
            it = m_tokens.erase(it);
            continue;
        }
        (*it)->TryParseReply(reply);
        ++it;
    }
}

void DevCtrlTokenMgr::Reset()
{
    for (auto& token : m_tokens) {
        if (token)
            token->Reset();
    }
    m_tokens.clear();
}

} // namespace Slic3r
