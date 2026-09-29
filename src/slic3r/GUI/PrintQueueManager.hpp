#ifndef slic3r_PrintQueueManager_hpp_
#define slic3r_PrintQueueManager_hpp_

#include <string>
#include <vector>

#include "slic3r/Utils/bambu_networking.hpp"

namespace Slic3r {
namespace GUI {

class PrintQueueManager
{
public:
    static PrintQueueManager& Get();

    int refresh_projects(const std::string& device_id, std::string& response_body);
    int create_project(const BBL::PrintQueueProjectCreateParams& params, std::string& response_body);
    int add_current_plate(const BBL::PrintParams& params, BBL::OnUpdateStatusFn update_fn,
                          BBL::WasCancelledFn cancel_fn, std::string& response_body);

    int update_project_title(int64_t project_id, const std::string& title, std::string& response_body);
    int sort_project(int64_t project_id, int64_t prev_project_id, std::string& response_body);
    int delete_project(int64_t project_id, std::string& response_body);
    int delete_all_projects(const std::string& device_id, std::string& response_body);

    int update_plate_title(int64_t plate_id, const std::string& title, std::string& response_body);
    int sort_plate(int64_t plate_id, int64_t project_id,
                   int64_t prev_plate_id, std::string& response_body);
    int delete_plate(int64_t plate_id, std::string& response_body);
    int extract_plate(int64_t plate_id, const std::string& title, const std::string& cover,
                      int64_t prev_project_id, std::string& response_body);
    int delete_unavailable_plates(const std::string& device_id, std::string& response_body);
    int get_model_profile(int64_t profile_id, const std::string& model_id, std::string& response_body);

private:
    PrintQueueManager() = default;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_PrintQueueManager_hpp_
