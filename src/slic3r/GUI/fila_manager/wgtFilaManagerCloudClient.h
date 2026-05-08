#ifndef slic3r_wgtFilaManagerCloudClient_h_
#define slic3r_wgtFilaManagerCloudClient_h_

#include <functional>
#include <map>
#include <string>
#include "nlohmann/json.hpp"

namespace Slic3r { namespace GUI {

class wgtFilaManagerCloudClient {
public:
    using SuccessFn = std::function<void(const nlohmann::json& data)>;
    using ErrorFn   = std::function<void(int http_code, const std::string& error)>;

    wgtFilaManagerCloudClient() = default;
    ~wgtFilaManagerCloudClient() = default;

    // POST /my/filament/v2
    void create_spool(const nlohmann::json& body, SuccessFn on_ok, ErrorFn on_err);

    // PUT /my/filament/v2/:id
    void update_spool(const std::string& id, const nlohmann::json& body, SuccessFn on_ok, ErrorFn on_err);

    // DELETE /my/filament/v2/batch
    void batch_delete(const nlohmann::json& body, SuccessFn on_ok, ErrorFn on_err);

    // GET /my/filament/v2
    void list_spools(const std::map<std::string, std::string>& query, SuccessFn on_ok, ErrorFn on_err);

    // GET /filament/config  (no auth required)
    void get_filament_config(SuccessFn on_ok, ErrorFn on_err);

private:
    bool        check_login(ErrorFn& on_err) const;
};

}} // namespace Slic3r::GUI

#endif // slic3r_wgtFilaManagerCloudClient_h_
