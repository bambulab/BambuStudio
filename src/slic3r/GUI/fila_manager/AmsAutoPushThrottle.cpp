#include "AmsAutoPushThrottle.h"

namespace Slic3r { namespace GUI {

AmsAutoPushThrottle::Decision
AmsAutoPushThrottle::evaluate(const std::string& tag_uid,
                              int64_t            current_net_weight,
                              DeviceState        device_state,
                              TimePoint          now)
{
    if (tag_uid.empty()) return Decision::SkipNoRfid;

    std::lock_guard<std::mutex> lk(m_mu);
    auto it = m_entries.find(tag_uid);

    // 不在表中 → 首次（先于 SkipNoDiff 判，避免 -1 与 0 克的合法 spool
    // 被误判为同值短路）
    if (it == m_entries.end()) return Decision::Push;

    // SkipNoDiff 优先于 SkipCooldown：克数没变就没必要再发
    if (it->second.last_pushed_net_weight == current_net_weight)
        return Decision::SkipNoDiff;

    if (device_state == DeviceState::Busy) {
        if (now - it->second.last_pushed_at < kMinIntervalBusy)
            return Decision::SkipCooldown;
    }

    // device_state==Idle 时无视 cooldown：用户改料 / 拔卡换槽时云端
    // 立即同步（Q8）
    return Decision::Push;
}

void AmsAutoPushThrottle::record_success(const std::string& tag_uid,
                                         int64_t            pushed_net_weight,
                                         TimePoint          now)
{
    if (tag_uid.empty()) return;

    std::lock_guard<std::mutex> lk(m_mu);
    auto& e                    = m_entries[tag_uid];
    e.last_pushed_at           = now;
    e.last_pushed_net_weight   = pushed_net_weight;
}

void AmsAutoPushThrottle::clear_for_tag(const std::string& tag_uid)
{
    if (tag_uid.empty()) return;

    std::lock_guard<std::mutex> lk(m_mu);
    m_entries.erase(tag_uid);
}

void AmsAutoPushThrottle::clear_all()
{
    std::lock_guard<std::mutex> lk(m_mu);
    m_entries.clear();
}

}} // namespace Slic3r::GUI
