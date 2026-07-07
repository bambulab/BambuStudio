#ifndef slic3r_wgtFilaManagerStore_h_
#define slic3r_wgtFilaManagerStore_h_

#include <map>
#include <string>
#include <vector>
#include "nlohmann/json.hpp"

namespace Slic3r { namespace GUI {

// AMS 同步时用于描述"某 spool 当前在哪台设备的哪个 AMS 槽位"的快照。
// 由 sync 层遍历 tray 时组装好，一次性交给 store 做 diff apply。
struct MountUpdate {
    int         ams_id   = -1;
    int         ams_type = -1;
    std::string slot_id;
};

struct FilamentSpool {
    std::string spool_id;
    std::string setting_id;
    std::string tag_uid;
    std::string tray_id_name;

    std::string brand;
    std::string material_type;
    std::string series;
    std::string color_name;
    std::string color_code;
    // STUDIO-17977: gradient / multicolor support, mirrors DevAmsTray::cols/ctype.
    //   color_type: 0 = gradient, 1 = multicolor, 2 = single colour
    //   invariant : when `colors` is non-empty, color_code == colors.front()
    //               (write paths are responsible for keeping this in sync).
    std::vector<std::string> colors;
    int                      color_type = 2;
    float                    diameter   = 1.75f;

    // STUDIO-17959: weights stored as double so values up to MAX_NET_WEIGHT_GRAMS
    // (999_999_999) round-trip without float32 precision loss (float32 precise
    // integer range tops out at 2^24 = 16,777,216, so 999_999_999 would snap to
    // 1.0e9 and surface as "user types 999999999 -> list shows 1000000000").
    double      initial_weight  = 0;
    double      spool_weight    = 0;
    int         remain_percent  = 100;
    std::string status          = "active"; // "active" | "low" | "empty" | "archived"

    std::string entry_method; // "manual" | "ams_sync"
    std::string created_at;
    std::string updated_at;

    std::string bound_dev_id;
    std::string bound_ams_id;

    // ---- 在位挂载状态。字段对应云端 inPrinter / devId / amsSn /
    // amsId / amsType / slotId / deviceName。
    // 来源：① MQTT push_status → apply_mount_diff 写入；
    //       ② 云端 pull → cloud_json_to_spool 解析（无实时机器数据时兜底）。
    // 持久化到 spools.json，进程重启后历史在位信息不丢。
    // 与 bound_dev_id / bound_ams_id 的区别：bound_* 是本地手动绑定（持久化、
    // 进入 sync 比较），这一组是实时挂载快照（随 AMS sync 和云端 pull 更新）。
    // 哨兵值：string 字段空串 = 未挂载；ams_id / ams_type 用 -1 表示未挂载。
    bool        in_printer  = false;
    std::string dev_id;
    std::string ams_sn;
    int         ams_id      = -1;
    int         ams_type    = -1;
    std::string slot_id;
    std::string device_name;

    std::string note;

    bool        favorite          = false;
    double      net_weight        = 0;

    // Cloud synchronization marker. Cloud is the source of truth: this flag
    // is true iff the spool was present in the latest cloud pull.
    bool        cloud_synced      = false;

    nlohmann::json to_json() const;
    // to_json_with_runtime: 持久化字段 + 运行时在位快照，供 spools_to_json() 推送前端。
    // 运行时字段**不**经 from_json / load / save 路径，仅存活于本次进程内存。
    nlohmann::json to_json_with_runtime() const;
    static FilamentSpool from_json(const nlohmann::json& j);

    static bool is_valid_tag_uid(const std::string& tag_uid);

    // STUDIO-18155：整卷净重（克）。
    // 新规约（STUDIO-17991 后）：spool_weight==0 且 initial_weight==整卷净重；
    // legacy 数据仍以 initial_weight=毛重 + spool_weight=料盘重 形式存在，
    // 减法兜底回收"整卷净重"。返回 <= 0 表示该 spool **缺整卷净重**，
    // AMS 自动同步路径会按 design Q7 决策整条冻结。
    double effective_total_net_weight() const
    {
        return (spool_weight > 0.0 && initial_weight > spool_weight)
            ? (initial_weight - spool_weight)
            : initial_weight;
    }
};

class wgtFilaManagerStore {
public:
    wgtFilaManagerStore() = default;
    ~wgtFilaManagerStore() = default;

    void load();
    void save();

    std::string add_spool(const FilamentSpool& spool);
    void update_spool(const FilamentSpool& spool);
    // STUDIO-18155：AMS 自动同步专用入口。仅当"sync 关心字段"
    //   (net_weight / remain_percent / status / bound_dev_id / bound_ams_id)
    // 实际发生变化时才写入；返回 true 表示有变化、应入云端 push 列表。
    //
    //
    // 为防御 sync 路径污染 identity 字段（设计 Q5 + STUDIO-18117 教训），
    // 该方法**强制**用 store 既有 spool 的 identity 字段（spool_id / tag_uid /
    // color_code / colors / color_type / setting_id / entry_method / created_at / cloud_synced）
    // 覆盖输入 sp 中的对应字段，再做比较与写入。即便 sync 误塞 identity，
    // store 也不会被改写。
    //
    // spool_id 在 store 中不存在（极端竞态）→ log warn + 返回 false，
    // 不退化为 add_spool。
    bool update_spool_if_changed(const FilamentSpool& sp);
    // Selectively merge user-editable fields from `patch` into the existing
    // spool without touching system-managed metadata (spool_id / tag_uid /
    // entry_method / created_at / bound_* / cloud_synced). Returns true if an
    // existing spool was updated.
    bool apply_patch(const std::string& spool_id, const nlohmann::json& patch);
    void remove_spool(const std::string& spool_id);
    const FilamentSpool* get_spool(const std::string& spool_id) const;

    // Flip the cloud_synced flag without rewriting other fields.
    // Returns true if the flag actually changed (used by callers to decide
    // whether to re-save and re-publish the list).
    bool mark_synced(const std::string& spool_id, bool synced);

    const FilamentSpool* find_by_tag_uid(const std::string& tag_uid) const;
    const FilamentSpool* find_by_setting_and_color(
        const std::string& setting_id, const std::string& color) const;

    // STUDIO-18155：返回所有 spool 的 id 拷贝（不暴露内部 map），调用方可
    // 配合 get_spool 遍历做 push_all_now / 全量同步等批量操作。
    std::vector<std::string> all_spool_ids() const;

    bool is_dirty() const { return m_dirty; }
    void set_dirty()      { m_dirty = true; }
    void clear_dirty()    { m_dirty = false; }

    nlohmann::json spools_to_json() const;

    // Immediately mount a spool to a specific tray slot.  Unlike the MQTT-driven
    // apply_mount_diff path, this is called when the user explicitly picks a
    // Filament Manager spool in AMSMaterialsSetting and clicks OK — we want the
    // in-printer snapshot to be up-to-date before the next MQTT push_status
    // arrives.  Sets in_printer + all snapshot fields, marks the store dirty,
    // and returns true when the spool existed.
    bool force_mount_spool(const std::string& spool_id,
                           const std::string& dev_id,
                           const std::string& dev_name,
                           int                ams_id,
                           int                ams_type,
                           const std::string& slot_id);

    // 运行时在位快照 diff apply（不 set_dirty，不写 spools.json）。
    // 语义："在 dev_id 这台机器的视角下，本轮 sync 观察到 present_now 里
    //       的 spool 在位；其余 spool 若之前挂在本机上，视为拔出，清字段"。
    // 所有权规则：
    //   - now_present  → 写入/刷新在位字段（同时抢占别机所有权）
    //   - was_our_hold → 清字段（本机拔出事件）
    //   - 其他情况    → 不动（别机所有 / 从未在位）
    // 这样两次 MQTT 之间"没变动"的 spool，字段值稳定，前端不跳变。
    bool apply_mount_diff(const std::string& dev_id,
                          const std::string& dev_name,
                          const std::map<std::string, MountUpdate>& present_now,
                          std::vector<std::string>* out_changed_ids = nullptr);

private:
    std::string get_storage_path() const;

    std::map<std::string, FilamentSpool> m_spools;
    bool                                 m_dirty = false;
};

}} // namespace Slic3r::GUI

#endif // slic3r_wgtFilaManagerStore_h_
