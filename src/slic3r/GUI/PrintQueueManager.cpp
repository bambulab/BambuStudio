#include "PrintQueueManager.hpp"

#include "GUI_App.hpp"
#include "slic3r/Utils/NetworkAgent.hpp"

#include <nlohmann/json.hpp>
#include "libslic3r/Log.hpp"

namespace Slic3r {
namespace GUI {

namespace {

// The service rejects a larger page size with HTTP 400; 20 is its own default.
constexpr int kQueueProjectsPageSize = 20;
constexpr int kQueueProjectsMaxPages = 200;

} // namespace

PrintQueueManager& PrintQueueManager::Get()
{
    static PrintQueueManager manager;
    return manager;
}

int PrintQueueManager::refresh_projects(const std::string& device_id, std::string& response_body)
{
    NetworkAgent* agent = wxGetApp().getAgent();
    if (!agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;

    BBL::PrintQueueProjectsQueryParams params;
    params.device_id = device_id;
    params.limit = kQueueProjectsPageSize;

    nlohmann::json merged_hits = nlohmann::json::array();
    int64_t total = 0;

    for (int page = 0; page < kQueueProjectsMaxPages; page++) {
        params.offset = static_cast<int>(merged_hits.size());

        std::string page_body;
        const int result = agent->get_print_queue_projects(params, &page_body);
        if (result != BAMBU_NETWORK_SUCCESS) {
            response_body = page_body;
            return result;
        }

        const nlohmann::json root = nlohmann::json::parse(page_body, nullptr, false);
        const auto hits_it = root.is_object() ? root.find("hits") : root.end();
        if (hits_it == root.end() || !hits_it->is_array()) {
            if (merged_hits.empty()) {
                response_body = page_body;
                return result;
            }
            break;
        }

        const size_t page_hits = hits_it->size();
        for (const auto& hit : *hits_it)
            merged_hits.push_back(hit);

        const auto total_it = root.find("total");
        if (total_it != root.end() && total_it->is_number_integer())
            total = total_it->get<int64_t>();

        if (page_hits < static_cast<size_t>(params.limit))
            break;
        if (total > 0 && static_cast<int64_t>(merged_hits.size()) >= total)
            break;
    }

    if (total < static_cast<int64_t>(merged_hits.size()))
        total = static_cast<int64_t>(merged_hits.size());

    response_body = nlohmann::json{{"total", total}, {"hits", std::move(merged_hits)}}.dump();
    return BAMBU_NETWORK_SUCCESS;
}

int PrintQueueManager::create_project(const BBL::PrintQueueProjectCreateParams& params, std::string& response_body)
{
    NetworkAgent* agent = wxGetApp().getAgent();
    if (!agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;
    return agent->create_print_queue_project(params, &response_body);
}

int PrintQueueManager::add_current_plate(const BBL::PrintParams& params, BBL::OnUpdateStatusFn update_fn,
                                         BBL::WasCancelledFn cancel_fn, std::string& response_body)
{
    NetworkAgent* agent = wxGetApp().getAgent();
    if (!agent)
        return BAMBU_NETWORK_ERR_INVALID_HANDLE;
    return agent->upload_print_queue_content(params, update_fn, cancel_fn, &response_body);
}

int PrintQueueManager::update_project_title(int64_t project_id, const std::string& title, std::string& response_body)
{
    NetworkAgent* agent = wxGetApp().getAgent();
    return agent ? agent->update_print_queue_project({ project_id, title }, &response_body) : BAMBU_NETWORK_ERR_INVALID_HANDLE;
}

int PrintQueueManager::sort_project(int64_t project_id, int64_t prev_project_id, std::string& response_body)
{
    NetworkAgent* agent = wxGetApp().getAgent();
    return agent ? agent->sort_print_queue_project({ project_id, prev_project_id }, &response_body) : BAMBU_NETWORK_ERR_INVALID_HANDLE;
}

int PrintQueueManager::delete_project(int64_t project_id, std::string& response_body)
{
    NetworkAgent* agent = wxGetApp().getAgent();
    return agent ? agent->delete_print_queue_project({ project_id }, &response_body) : BAMBU_NETWORK_ERR_INVALID_HANDLE;
}

int PrintQueueManager::delete_all_projects(const std::string& device_id, std::string& response_body)
{
    std::string list_body;
    const int list_result = refresh_projects(device_id, list_body);
    if (list_result != BAMBU_NETWORK_SUCCESS) {
        response_body = list_body;
        return list_result;
    }

    try {
        const nlohmann::json response = nlohmann::json::parse(list_body);
        const nlohmann::json* projects = nullptr;
        if (response.is_array())
            projects = &response;
        else if (response.contains("hits") && response["hits"].is_array())
            projects = &response["hits"];
        else if (response.contains("projects") && response["projects"].is_array())
            projects = &response["projects"];
        else if (response.contains("data") && response["data"].is_object() && response["data"].contains("projects") && response["data"]["projects"].is_array())
            projects = &response["data"]["projects"];

        if (!projects) {
            response_body = list_body;
            return BAMBU_NETWORK_ERR_INVALID_RESULT;
        }

        for (const nlohmann::json& project : *projects) {
            if (!project.is_object())
                continue;
            const auto id_it = project.find("id");
            const auto project_id_it = project.find("projectId");
            if (id_it == project.end() && project_id_it == project.end())
                continue;
            const nlohmann::json& id = id_it != project.end() ? *id_it : *project_id_it;
            int64_t project_id = 0;
            if (id.is_number_integer())
                project_id = id.get<int64_t>();
            else if (id.is_number_unsigned())
                project_id = static_cast<int64_t>(id.get<uint64_t>());
            else if (id.is_string())
                project_id = std::stoll(id.get<std::string>());
            else
                continue;
            const int result = delete_project(project_id, response_body);
            if (result != BAMBU_NETWORK_SUCCESS)
                return result;
        }
    } catch (...) {
        BOOST_LOG_TRIVIAL(warning) << current_exception_message();
        response_body = list_body;
        return BAMBU_NETWORK_ERR_INVALID_RESULT;
    }
    return BAMBU_NETWORK_SUCCESS;
}

int PrintQueueManager::update_plate_title(int64_t plate_id, const std::string& title, std::string& response_body)
{
    NetworkAgent* agent = wxGetApp().getAgent();
    return agent ? agent->update_print_queue_plate({ plate_id, title }, &response_body) : BAMBU_NETWORK_ERR_INVALID_HANDLE;
}

int PrintQueueManager::sort_plate(int64_t plate_id, int64_t project_id,
                                  int64_t prev_plate_id, std::string& response_body)
{
    NetworkAgent* agent = wxGetApp().getAgent();
    return agent ? agent->sort_print_queue_plate({ plate_id, project_id, prev_plate_id }, &response_body) : BAMBU_NETWORK_ERR_INVALID_HANDLE;
}

int PrintQueueManager::delete_plate(int64_t plate_id, std::string& response_body)
{
    NetworkAgent* agent = wxGetApp().getAgent();
    return agent ? agent->delete_print_queue_plate({ plate_id }, &response_body) : BAMBU_NETWORK_ERR_INVALID_HANDLE;
}

int PrintQueueManager::extract_plate(int64_t plate_id, const std::string& title, const std::string& cover,
                                     int64_t prev_project_id, std::string& response_body)
{
    NetworkAgent* agent = wxGetApp().getAgent();
    return agent ? agent->extract_print_queue_plate({ plate_id, title, cover, prev_project_id }, &response_body) : BAMBU_NETWORK_ERR_INVALID_HANDLE;
}

int PrintQueueManager::delete_unavailable_plates(const std::string& device_id, std::string& response_body)
{
    NetworkAgent* agent = wxGetApp().getAgent();
    return agent ? agent->delete_unavailable_print_queue_plates({ device_id }, &response_body) : BAMBU_NETWORK_ERR_INVALID_HANDLE;
}

int PrintQueueManager::get_model_profile(int64_t profile_id, const std::string& model_id, std::string& response_body)
{
    NetworkAgent* agent = wxGetApp().getAgent();
    return agent ? agent->get_print_queue_model_profile({ profile_id, model_id }, &response_body) : BAMBU_NETWORK_ERR_INVALID_HANDLE;
}

} // namespace GUI
} // namespace Slic3r
