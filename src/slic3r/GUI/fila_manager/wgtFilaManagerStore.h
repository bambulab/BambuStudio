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
    float       diameter        = 1.75f;

    float       initial_weight  = 0;
    float       spool_weight    = 0;
    int         remain_percent  = 100;
    std::string status          = "active"; // "active" | "low" | "empty" | "archived"

    std::string entry_method; // "manual" | "ams_sync"
    std::string created_at;
    std::string updated_at;

    std::string bound_dev_id;
    std::string bound_ams_id;

    std::string note;

    bool        favorite          = false;
    float       net_weight        = 0;

    // Cloud synchronization marker. Cloud is the source of truth: this flag
    // is true iff the spool was present in the latest cloud pull.
    bool        cloud_synced      = false;

    nlohmann::json to_json() const;
    static FilamentSpool from_json(const nlohmann::json& j);
};

class wgtFilaManagerStore {
public:
    wgtFilaManagerStore() = default;
    ~wgtFilaManagerStore() = default;

    void load();
    void save();

    std::string add_spool(const FilamentSpool& spool);
    void update_spool(const FilamentSpool& spool);
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
