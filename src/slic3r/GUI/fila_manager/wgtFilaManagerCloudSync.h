#ifndef slic3r_wgtFilaManagerCloudSync_h_
#define slic3r_wgtFilaManagerCloudSync_h_

#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "nlohmann/json.hpp"

#include "AmsAutoPushThrottle.h"

namespace Slic3r { namespace GUI {

class wgtFilaManagerStore;
class wgtFilaManagerCloudClient;
struct FilamentSpool;

class wgtFilaManagerCloudSync {
public:
    // STUDIO-18155：sync 通过此结构告知 cloud_sync 哪些 spool 发生了变化。
    // 仅承载 throttle / PUT body 需要的最小字段集，identity 字段不传。
    struct AmsChangedSpool {
        std::string spool_id;
        std::string tag_uid;
        int64_t     net_weight {0}; // 已由 sync 算好的克数（Q6）
    };

    // auto_push 摘要观察者；FilamentManagerVM 在它的生命周期内挂上去，转发到
    // 前端 `submod=sync, action=auto_push_summary` ReportMsg。
    using AutoPushSummaryFn = std::function<void(const nlohmann::json&)>;

    wgtFilaManagerCloudSync(wgtFilaManagerStore* store, wgtFilaManagerCloudClient* client);
    ~wgtFilaManagerCloudSync() = default;

    void pull_from_cloud();
    void push_spool_to_cloud(const std::string& spool_id);
    // Push a selective update. `local_patch` contains only the fields the user
    // actually changed this time (local schema names). Fields outside the cloud
    // UpdateFilamentV2 whitelist are dropped silently.
    void push_update_to_cloud(const std::string& spool_id, const nlohmann::json& local_patch);
    void push_delete_to_cloud(const std::vector<std::string>& spool_ids);
    void fetch_filament_config(std::function<void(const nlohmann::json&)> on_done);

    // STUDIO-18155 / openspec 20260506耗材管理器AMS自动同步云端：
    // sync 完成本地写入后通过此入口推到云端。内部按 throttle 决策决定
    // 是否真发 PUT；摘要通过 AutoPushSummaryFn 观察者吐回前端。
    //
    // device_state 由 sync 一次性算好整批共用，避免 sync 内多 tray 处理
    // 跨越状态切换造成"半忙半闲"的语义不一致。
    void notify_ams_synced(const std::vector<AmsChangedSpool>& changed,
                           AmsAutoPushThrottle::DeviceState   device_state);

    // 用户手动覆盖入口：忽略 throttle 把所有"有 RFID + 有整卷净重"的 spool
    // 全部入 push 队列。即便都 enqueue 也会 record_success 保持 cooldown
    // 状态一致（避免下一次 sync 立刻又触发 push）。
    void push_all_now();

    AmsAutoPushThrottle&       throttle()       { return m_throttle; }
    const AmsAutoPushThrottle& throttle() const { return m_throttle; }

    void set_on_auto_push_summary(AutoPushSummaryFn fn) { m_on_auto_push_summary = std::move(fn); }

    bool is_syncing() const { return m_syncing; }
    bool last_pull_succeeded() const { return m_last_pull_succeeded; }
    int last_pull_error_code() const { return m_last_pull_error_code; }
    const std::string& last_pull_error_message() const { return m_last_pull_error_message; }
    static nlohmann::json spool_to_cloud_json(const FilamentSpool& spool);
    // Translate a local-field patch into a cloud UpdateFilamentV2 body.
    // Only whitelisted fields are emitted (filamentVendor/filamentType/
    // filamentName/filamentId/color/colorType/colors/netWeight/
    // totalNetWeight/note). Returns an empty object if nothing maps.
    static nlohmann::json spool_to_cloud_update_patch(const nlohmann::json& local_patch);
    // Build the actual UpdateFilamentV2 body for an existing spool. This keeps
    // update-only required fields out of the create path.
    static nlohmann::json spool_to_cloud_update_json(const FilamentSpool& spool, const nlohmann::json& local_patch);
    static FilamentSpool cloud_json_to_spool(const nlohmann::json& j);

private:
    wgtFilaManagerStore*       m_store;
    wgtFilaManagerCloudClient* m_client;
    bool                       m_syncing = false;
    bool                       m_last_pull_succeeded = false;
    int                        m_last_pull_error_code = 0;
    std::string                m_last_pull_error_message;

    AmsAutoPushThrottle        m_throttle;
    AutoPushSummaryFn          m_on_auto_push_summary;
};

}} // namespace Slic3r::GUI

#endif // slic3r_wgtFilaManagerCloudSync_h_
