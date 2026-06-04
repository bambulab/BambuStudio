#ifndef slic3r_wgtFilaManagerStore_h_
#define slic3r_wgtFilaManagerStore_h_

#include <map>
#include <string>
#include <vector>
#include "nlohmann/json.hpp"

namespace Slic3r { namespace GUI {

struct FilamentSpool {
    std::string spool_id;
    std::string setting_id;
    std::string tag_uid;

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

    std::string note;

    bool        favorite          = false;
    double      net_weight        = 0;

    // Cloud synchronization marker. Cloud is the source of truth: this flag
    // is true iff the spool was present in the latest cloud pull.
    bool        cloud_synced      = false;

    nlohmann::json to_json() const;
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

private:
    std::string get_storage_path() const;

    std::map<std::string, FilamentSpool> m_spools;
    bool                                 m_dirty = false;
};

}} // namespace Slic3r::GUI

#endif // slic3r_wgtFilaManagerStore_h_
