#ifndef slic3r_wgtFilaManagerCloudSync_h_
#define slic3r_wgtFilaManagerCloudSync_h_

#include <functional>
#include <string>
#include <vector>
#include "nlohmann/json.hpp"

namespace Slic3r { namespace GUI {

class wgtFilaManagerStore;
class wgtFilaManagerCloudClient;
struct FilamentSpool;

class wgtFilaManagerCloudSync {
public:
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
};

}} // namespace Slic3r::GUI

#endif // slic3r_wgtFilaManagerCloudSync_h_
