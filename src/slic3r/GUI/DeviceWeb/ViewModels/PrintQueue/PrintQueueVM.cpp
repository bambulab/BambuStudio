// src/slic3r/GUI/DeviceWeb/ViewModels/PrintQueue/PrintQueueVM.cpp
#include "PrintQueueVM.hpp"

#include <limits>
#include <thread>
#include <utility>

#include "slic3r/GUI/DeviceWeb/DeviceWebBridge.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/PrintQueueManager.hpp"
#include "slic3r/GUI/QueuePrintController.hpp"
#include "slic3r/Utils/NetworkAgent.hpp"
#include "libslic3r/Log.hpp"

namespace Slic3r { namespace GUI {

namespace {

// Ids cross the bridge as decimal strings so JavaScript never rounds an int64.
int64_t ReadId(const nlohmann::json& payload, const char* key)
{
    if (!payload.contains(key)) return 0;
    const auto& value = payload.at(key);
    if (value.is_number_integer()) return value.get<int64_t>();
    if (value.is_number_unsigned()) {
        const uint64_t id = value.get<uint64_t>();
        return id <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ? static_cast<int64_t>(id) : 0;
    }
    if (!value.is_string()) return 0;
    try {
        const std::string text = value.get<std::string>();
        size_t consumed = 0;
        const int64_t id = std::stoll(text, &consumed);
        return consumed == text.size() && id > 0 ? id : 0;
    } catch (const std::exception&) {
        BOOST_LOG_TRIVIAL(warning) << current_exception_message();
        return 0;
    }
}

std::string ReadString(const nlohmann::json& node, const char* key)
{
    if (!node.contains(key)) return {};
    const auto& value = node.at(key);
    if (value.is_string()) return value.get<std::string>();
    if (value.is_number_integer()) return std::to_string(value.get<int64_t>());
    if (value.is_number_unsigned()) return std::to_string(value.get<uint64_t>());
    return {};
}

double ReadNumber(const nlohmann::json& node, const char* key)
{
    if (!node.contains(key)) return 0.0;
    const auto& value = node.at(key);
    return value.is_number() ? value.get<double>() : 0.0;
}

bool ReadQueuePlateIndex(const nlohmann::json& payload, int& plate_index)
{
    const auto it = payload.find("plate_index");
    if (it == payload.end()) return false;
    if (it->is_number_integer()) {
        const int64_t value = it->get<int64_t>();
        if (value < 1 || value > kMaxQueuePlateIndex) return false;
        plate_index = static_cast<int>(value);
        return true;
    }
    if (!it->is_number_unsigned()) return false;
    const uint64_t value = it->get<uint64_t>();
    if (value == 0 || value > static_cast<uint64_t>(kMaxQueuePlateIndex)) return false;
    plate_index = static_cast<int>(value);
    return true;
}

} // namespace

PrintQueueVM::PrintQueueVM()
    : m_alive(std::make_shared<int>(0))
{
}

PrintQueueVM::~PrintQueueVM()
{
    m_alive.reset();
}

nlohmann::json PrintQueueVM::MakeResp(const std::string& submod,
                                      const std::string& action,
                                      int code,
                                      const std::string& msg,
                                      const nlohmann::json& payload)
{
    return MakeResponse(GetModule(), submod, action, code, msg, payload);
}

void PrintQueueVM::SetDeviceId(const std::string& device_id)
{
    if (m_device_id == device_id) return;
    m_device_id = device_id;
    RunAsync(nullptr);
}

nlohmann::json PrintQueueVM::OnCommand(const std::string& submod,
                                       const std::string& action,
                                       const nlohmann::json& payload)
{
    if (m_device_id.empty())
        return MakeResp(submod, action, BAMBU_NETWORK_ERR_INVALID_HANDLE, "No printer is selected.");

    if (submod == "queue" && action == "list") {
        RunAsync(nullptr);
        return MakeResp(submod, action, 0);
    }
    if (submod == "project") return HandleProject(action, payload);
    if (submod == "plate")   return HandlePlate(action, payload);

    return MakeResp(submod, action, BAMBU_NETWORK_ERR_INVALID_HANDLE, "Unsupported print queue command.");
}

nlohmann::json PrintQueueVM::HandleProject(const std::string& action, const nlohmann::json& payload)
{
    const int64_t project_id = ReadId(payload, "project_id");
    if (project_id <= 0)
        return MakeResp("project", action, BAMBU_NETWORK_ERR_INVALID_HANDLE, "Missing project id.");

    if (action == "delete") {
        RunAsync([project_id](std::string& body) {
            return PrintQueueManager::Get().delete_project(project_id, body);
        });
        return MakeResp("project", action, 0);
    }
    if (action == "sort") {
        // prev_project_id == 0 means "move to the top of the queue".
        const int64_t prev_project_id = ReadId(payload, "prev_project_id");
        RunAsync([project_id, prev_project_id](std::string& body) {
            return PrintQueueManager::Get().sort_project(project_id, prev_project_id, body);
        });
        return MakeResp("project", action, 0);
    }

    return MakeResp("project", action, BAMBU_NETWORK_ERR_INVALID_HANDLE, "Unsupported project command.");
}

nlohmann::json PrintQueueVM::HandlePlate(const std::string& action, const nlohmann::json& payload)
{
    const int64_t plate_id = ReadId(payload, "plate_id");
    if (plate_id <= 0)
        return MakeResp("plate", action, BAMBU_NETWORK_ERR_INVALID_HANDLE, "Missing plate id.");

    if (action == "delete") {
        RunAsync([plate_id](std::string& body) {
            return PrintQueueManager::Get().delete_plate(plate_id, body);
        });
        return MakeResp("plate", action, 0);
    }
    if (action == "print") {
        const std::string model_id = ReadString(payload, "model_id");
        const int64_t profile_id = ReadId(payload, "profile_id");
        int plate_index = 0;
        const std::string title = ReadString(payload, "title");
        const std::string device_id = ReadString(payload, "device_id");
        if (model_id.empty() || profile_id <= 0 || !ReadQueuePlateIndex(payload, plate_index) || device_id.empty())
            return MakeResp("plate", action, BAMBU_NETWORK_ERR_INVALID_HANDLE, "Invalid queue plate print request.");

        std::weak_ptr<int> alive = m_alive;
        QueuePrintController::Get().PrepareAndShow({
            std::to_string(plate_id), model_id, std::to_string(profile_id), plate_index, title, device_id,
            ReadString(payload, "cover"),
        }, [this, alive](int result, const std::string& message) {
            if (alive.expired() || !m_bridge) return;
            m_bridge->ReportMsg(MakeResp("plate", "print", result, message));
        });
        return MakeResp("plate", action, 0);
    }
    if (action == "sort") {
        const int64_t project_id = ReadId(payload, "project_id");
        if (project_id <= 0)
            return MakeResp("plate", action, BAMBU_NETWORK_ERR_INVALID_HANDLE, "Missing project id.");
        // prev_plate_id == 0 means "move to the top of the target project".
        const int64_t prev_plate_id = ReadId(payload, "prev_plate_id");
        RunAsync([plate_id, project_id, prev_plate_id](std::string& body) {
            return PrintQueueManager::Get().sort_plate(plate_id, project_id, prev_plate_id, body);
        });
        return MakeResp("plate", action, 0);
    }
    if (action == "extract") {
        // prev_project_id == 0 means "put it at the top of the queue".
        const int64_t prev_project_id = ReadId(payload, "prev_project_id");
        const std::string title = ReadString(payload, "title");
        const std::string cover = ReadString(payload, "cover");
        RunAsync([plate_id, title, cover, prev_project_id](std::string& body) {
            return PrintQueueManager::Get().extract_plate(plate_id, title, cover, prev_project_id, body);
        });
        return MakeResp("plate", action, 0);
    }

    return MakeResp("plate", action, BAMBU_NETWORK_ERR_INVALID_HANDLE, "Unsupported plate command.");
}

void PrintQueueVM::RunAsync(QueueTask task)
{
    const std::string device_id = m_device_id;
    if (device_id.empty()) return;

    std::weak_ptr<int> alive = m_alive;
    std::thread([this, alive, device_id, task = std::move(task)]() {
        std::string body;
        int result = BAMBU_NETWORK_SUCCESS;
        if (task) result = task(body);

        // Re-read the queue even after a failed write: the service is the only
        // source of truth and the page must not keep a stale row on screen.
        std::string list_body;
        const int list_result = PrintQueueManager::Get().refresh_projects(device_id, list_body);
        if (result == BAMBU_NETWORK_SUCCESS) result = list_result;

        wxGetApp().CallAfter([this, alive, result, list_body]() {
            if (alive.expired()) return;
            PublishQueue(result, list_body);
        });
    }).detach();
}

void PrintQueueVM::PublishQueue(int result, const std::string& response_body)
{
    if (!m_bridge) return;

    if (result != BAMBU_NETWORK_SUCCESS) {
        BOOST_LOG_TRIVIAL(warning) << "PrintQueueVM: queue request failed, code=" << result;
        m_bridge->ReportMsg(MakeResp("queue", "list", result, "Unable to reach the print queue service."));
        return;
    }

    m_bridge->ReportMsg(MakeResp("queue", "list", 0, "", BuildProjects(response_body)));
}

nlohmann::json PrintQueueVM::BuildProjects(const std::string& response_body)
{
    nlohmann::json projects = nlohmann::json::array();

    nlohmann::json root = nlohmann::json::parse(response_body, nullptr, false);
    if (root.is_discarded() || !root.is_object())
        return nlohmann::json{{"projects", projects}};

    const auto hits_it = root.find("hits");
    if (hits_it == root.end() || !hits_it->is_array())
        return nlohmann::json{{"projects", projects}};

    for (const auto& hit : *hits_it) {
        if (!hit.is_object()) continue;

        nlohmann::json plates = nlohmann::json::array();
        const auto plates_it = hit.find("plates");
        if (plates_it != hit.end() && plates_it->is_array()) {
            for (const auto& plate : *plates_it) {
                if (!plate.is_object()) continue;
                std::string name = ReadString(plate, "title");
                if (name.empty()) name = ReadString(plate, "plateName");
                const std::string device_id = ReadString(plate, "deviceId").empty()
                    ? ReadString(hit, "deviceId") : ReadString(plate, "deviceId");
                plates.push_back({
                    {"id",        ReadString(plate, "id")},
                    {"model_id",  ReadString(plate, "modelId")},
                    {"profile_id", ReadString(plate, "profileId")},
                    {"device_id", device_id},
                    {"name",      name},
                    {"profile_name", std::string()},
                    {"cost_time", ReadNumber(plate, "costTime")},
                    {"weight",    ReadNumber(plate, "weight")},
                    {"plate_index", plate.contains("plateIndex") && (plate.at("plateIndex").is_number_integer() || plate.at("plateIndex").is_number_unsigned())
                        ? plate.at("plateIndex") : nlohmann::json(-1)},
                    {"cover",     ReadString(plate, "cover")},
                    {"status",    static_cast<int>(ReadNumber(plate, "status"))},
                    {"available", !plate.contains("available") || plate.at("available").is_null()
                                      ? true
                                      : plate.at("available").get<bool>()},
                });
            }
        }

        projects.push_back({
            {"id",     ReadString(hit, "id")},
            {"name",   ReadString(hit, "title")},
            {"cover",  ReadString(hit, "cover")},
            {"plates", plates},
        });
    }

    return nlohmann::json{{"projects", projects}};
}

void PrintQueueVM::ReportState(const std::string& submod, const std::string& action)
{
    if (!m_bridge) return;
    if (submod != "queue" || action != "list") return;
    RunAsync(nullptr);
}

}} // namespace Slic3r::GUI
