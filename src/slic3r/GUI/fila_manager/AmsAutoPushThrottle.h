#ifndef slic3r_AmsAutoPushThrottle_h_
#define slic3r_AmsAutoPushThrottle_h_

// STUDIO-18155 / openspec 20260506耗材管理器AMS自动同步云端
//
// AMS 同步路径完成本地写入后向云端 PUT 推送 netWeight 的节流闸。
// 设计目标：让 IoT 端高频 MQTT push 不会反向轰炸云端，同时让用户
// 在打印机闲置时改料 / 拔卡换槽能立即看到云端余量同步。
//
// 双轨策略（design § 2.2）：
//   1. tag_uid 空                                  → SkipNoRfid
//   2. net_weight 与上次推送一致                   → SkipNoDiff
//   3. 首次见到该 tag_uid                          → Push
//   4. device_state==Busy && 距上次 < kMinIntervalBusy → SkipCooldown
//   5. 其他（含 device_state==Idle 任意时段）      → Push
//
// 不持久化：进程重启 / 切账号都会清账，重新基于当前状态判定。

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Slic3r { namespace GUI {

class AmsAutoPushThrottle {
public:
    enum class Decision { Push, SkipNoRfid, SkipCooldown, SkipNoDiff };

    // 设备活动状态，决定是否走 cooldown 节流（design § 2.3 字段表）：
    //   Busy = is_in_printing | is_in_calibration | is_in_extrusion_cali
    //          | ams_status_main 非 IDLE / UNKNOWN
    //   Idle = 上述全否
    enum class DeviceState { Busy, Idle };

    // 忙时硬编码 10 min（Q2/Q8 决策），不暴露 setter。闲时无 cooldown。
    static constexpr std::chrono::minutes kMinIntervalBusy{10};

    using TimePoint = std::chrono::steady_clock::time_point;

    AmsAutoPushThrottle()  = default;
    ~AmsAutoPushThrottle() = default;

    AmsAutoPushThrottle(const AmsAutoPushThrottle&)            = delete;
    AmsAutoPushThrottle& operator=(const AmsAutoPushThrottle&) = delete;

    // 决策一次推送是否要走云端。current_net_weight 单位为克，对应云端
    // UpdateFilamentV2Req::netWeight 字段（Q6）。返回 SkipNoDiff 时不会
    // 修改 entries（保留上一次成功 push 的 last_pushed_at / weight）。
    Decision evaluate(const std::string& tag_uid,
                      int64_t            current_net_weight,
                      DeviceState        device_state,
                      TimePoint          now);

    // PUT 成功后回调；用 net_weight（克）记录本次成功推送，下一次
    // evaluate 据此做 SkipNoDiff / SkipCooldown 判定。
    void record_success(const std::string& tag_uid,
                        int64_t            pushed_net_weight,
                        TimePoint          now);

    // push_all_now 的旁路：清单条 entry，让该 tag 下一次 evaluate 必 Push。
    void clear_for_tag(const std::string& tag_uid);

    // 切账号 / 登出 → 清整张表，避免账号 A 的 throttle 影响账号 B。
    void clear_all();

private:
    struct Entry {
        TimePoint last_pushed_at;
        int64_t   last_pushed_net_weight {-1};
    };

    mutable std::mutex                       m_mu;
    std::unordered_map<std::string, Entry>   m_entries;
};

}} // namespace Slic3r::GUI

#endif // slic3r_AmsAutoPushThrottle_h_
