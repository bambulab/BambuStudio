#ifndef slic3r_wgtFilaManagerStore_h_
#define slic3r_wgtFilaManagerStore_h_

#include <map>
#include <string>
#include <vector>
#include "nlohmann/json.hpp"

namespace Slic3r { namespace GUI {

struct MountUpdate {
    int         ams_id   = -1;
    int         ams_type = -1;
    std::string slot_id;
    std::string ams_sn;
};

struct EjectedSlotSnapshot {
    std::string spool_id;
    std::string ams_sn;
    int         ams_id   = -1;
    int         ams_type = -1;
    std::string slot_id;
};

struct SoftMatchFilamentItem {
    int         id            = 0;
    std::string create_type;        // "ams" | "manual"
    std::string filament_vendor;
    std::string filament_type;
    std::string filament_name;
    std::string filament_id;
    std::string rfid;
    std::string color;              // "#RRGGBBAA" 或 "#RRGGBB"
    int         color_type        = 2;
    std::vector<std::string> colors;
    double      net_weight        = 0;
    double      total_net_weight  = 0;
    std::string note;
    std::string tray_id_name;
    std::string category;
    bool        in_printer        = false;
    std::string dev_id;
    std::string ams_sn;
    std::string slot_id;
    int         ams_id            = -1;
    int         ams_type          = -1;
    std::string device_name;
    bool        depleted          = false;

    static SoftMatchFilamentItem from_json(const nlohmann::json& j);
};

struct SoftMatchPendingCandidate {
    int                                pending_id = 0;
    std::vector<SoftMatchFilamentItem> candidates;
};

struct SoftMatchPendingResponse {
    std::vector<SoftMatchFilamentItem>     hits;
    std::vector<SoftMatchPendingCandidate> candidates;
    bool empty() const { return hits.empty(); }

    static SoftMatchPendingResponse from_json(const nlohmann::json& j);
};

struct FilamentSpool {
    std::string spool_id;
    std::string filament_id;
    std::string tag_uid;
    std::string tray_id_name;

    std::string brand;
    std::string material_type;
    std::string series;
    std::string color_name;
    std::string color_code;
    std::vector<std::string> colors;
    int                      color_type = 2;
    float                    diameter   = 1.75f;

    double      initial_weight  = 0;
    double      spool_weight    = 0;
    int         remain_percent  = 100;
    std::string status          = "active"; // "active" | "low" | "empty" | "archived"

    std::string entry_method; // "manual" | "ams_sync"
    std::string created_at;
    std::string updated_at;

    std::string bound_dev_id;
    std::string bound_ams_id;

    bool        in_printer  = false;
    std::string dev_id;
    std::string ams_sn;
    int         ams_id      = -1;
    int         ams_type    = -1;
    std::string slot_id;
    std::string device_name;

    int         mount_hold_count = 0;

    std::string note;

    bool        favorite          = false;
    double      net_weight        = 0;


    bool        cloud_synced      = false;

    nlohmann::json to_json() const;

    nlohmann::json to_json_with_runtime() const;
    static FilamentSpool from_json(const nlohmann::json& j);

    static bool is_valid_tag_uid(const std::string& tag_uid);

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
    bool update_spool_if_changed(const FilamentSpool& sp);
    bool apply_patch(const std::string& spool_id, const nlohmann::json& patch);
    void remove_spool(const std::string& spool_id);
    const FilamentSpool* get_spool(const std::string& spool_id) const;

    bool mark_synced(const std::string& spool_id, bool synced);

    const FilamentSpool* find_by_tag_uid(const std::string& tag_uid) const;
    const FilamentSpool* find_by_setting_and_color(
        const std::string& setting_id, const std::string& color) const;

    const FilamentSpool* find_by_slot(const std::string& dev_id,
                                      const std::string& ams_id,
                                      const std::string& slot_id) const;

    std::vector<std::string> all_spool_ids() const;

    bool is_dirty() const { return m_dirty; }
    void set_dirty()      { m_dirty = true; }
    void clear_dirty()    { m_dirty = false; }

    nlohmann::json spools_to_json() const;

    bool force_mount_spool(const std::string& spool_id,
                           const std::string& dev_id,
                           const std::string& dev_name,
                           int                ams_id,
                           int                ams_type,
                           const std::string& slot_id,
                           const std::string& ams_sn);

    bool apply_mount_diff(const std::string& dev_id,
                          const std::string& dev_name,
                          const std::map<std::string, MountUpdate>& present_now,
                          std::vector<std::string>* out_changed_ids = nullptr,
                          std::vector<EjectedSlotSnapshot>* out_ejected = nullptr);

private:
    std::string get_storage_path() const;

    std::map<std::string, FilamentSpool> m_spools;
    bool                                 m_dirty = false;
};

}} // namespace Slic3r::GUI

#endif // slic3r_wgtFilaManagerStore_h_
