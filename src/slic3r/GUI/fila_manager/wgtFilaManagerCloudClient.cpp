#include "wgtFilaManagerCloudClient.h"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/Utils/NetworkAgent.hpp"

#include <boost/log/trivial.hpp>
#include <thread>

namespace Slic3r { namespace GUI {

namespace {

using SuccessFn = wgtFilaManagerCloudClient::SuccessFn;
using ErrorFn   = wgtFilaManagerCloudClient::ErrorFn;
using RequestFn = std::function<int(NetworkAgent*, std::string&)>;

void emit_http_debug(const std::string& level,
                     const std::string& title,
                     const std::string& summary,
                     const nlohmann::json& detail = nlohmann::json::object())
{
    wxGetApp().emit_fila_debug_log("http", level, title, summary, detail);
}

nlohmann::json parse_json_response(const std::string& resp_body)
{
    try {
        return nlohmann::json::parse(resp_body);
    } catch (const std::exception&) {
        return nlohmann::json{{"raw", resp_body}};
    }
}

int parse_int(const std::map<std::string, std::string>& query, const std::string& key, int default_value)
{
    auto it = query.find(key);
    if (it == query.end() || it->second.empty())
        return default_value;

    try {
        return std::stoi(it->second);
    } catch (const std::exception&) {
        return default_value;
    }
}

NetworkAgent* get_agent()
{
    return wxGetApp().getAgent();
}

void dispatch_agent_request(const std::string& action,
                            const nlohmann::json& request_detail,
                            RequestFn request,
                            SuccessFn on_ok,
                            ErrorFn on_err)
{
    emit_http_debug("info",
                    "NetworkAgent " + action,
                    "Dispatching filament cloud request through NetworkAgent",
                    request_detail);

    std::thread([action, request_detail, request = std::move(request), on_ok = std::move(on_ok), on_err = std::move(on_err)]() mutable {
        NetworkAgent* agent = get_agent();
        if (!agent) {
            wxGetApp().CallAfter([on_err, action]() {
                emit_http_debug("error",
                                "NetworkAgent " + action + " failed",
                                "NetworkAgent is unavailable",
                                {{"action", action}, {"ret_code", BAMBU_NETWORK_ERR_INVALID_HANDLE}});
                if (on_err)
                    on_err(BAMBU_NETWORK_ERR_INVALID_HANDLE, "network agent unavailable");
            });
            return;
        }

        std::string response_body;
        int ret = request(agent, response_body);

        if (ret == BAMBU_NETWORK_SUCCESS) {
            auto parsed = parse_json_response(response_body);
            wxGetApp().CallAfter([on_ok, action, parsed = std::move(parsed), response_body]() mutable {
                emit_http_debug("info",
                                "NetworkAgent " + action + " success",
                                "Filament cloud request completed via NetworkAgent",
                                {{"action", action}, {"response", parsed}, {"raw_response", response_body}});
                if (on_ok)
                    on_ok(parsed);
            });
            return;
        }

        wxGetApp().CallAfter([on_err, action, ret, response_body]() {
            emit_http_debug("error",
                            "NetworkAgent " + action + " failed",
                            "Filament cloud request failed in NetworkAgent",
                            {{"action", action}, {"ret_code", ret}, {"body", response_body}});
            if (on_err)
                on_err(ret, response_body.empty() ? "network request failed" : response_body);
        });
    }).detach();
}

} // namespace

bool wgtFilaManagerCloudClient::check_login(ErrorFn& on_err) const
{
    NetworkAgent* agent = get_agent();
    if (!agent || !agent->is_user_login()) {
        if (on_err)
            on_err(0, "user not logged in");
        return false;
    }
    return true;
}

void wgtFilaManagerCloudClient::create_spool(
    const nlohmann::json& body, SuccessFn on_ok, ErrorFn on_err)
{
    if (!check_login(on_err))
        return;

    dispatch_agent_request("create_spool",
                           {{"method", "POST"}, {"body", body}},
                           [payload = body.dump()](NetworkAgent* agent, std::string& response_body) {
                               return agent->create_filament_spool(payload, &response_body);
                           },
                           std::move(on_ok),
                           std::move(on_err));
}

void wgtFilaManagerCloudClient::update_spool(
    const std::string& id, const nlohmann::json& body, SuccessFn on_ok, ErrorFn on_err)
{
    if (!check_login(on_err))
        return;

    // Swagger UpdateFilamentV2Req：`id` 为 int64 且 required，路由是
    // `PUT /my/filament/v2`（id 在 body，不在 path）。这里把字符串形式的本地
    // spool_id 转成 int64 写回 body；其余字段由 `spool_to_cloud_update_patch`
    // 过滤过 swagger 白名单，调用方直接传入即可。
    nlohmann::json request_body = body.is_object() ? body : nlohmann::json::object();
    try {
        request_body["id"] = std::stoll(id);
    } catch (const std::exception&) {
        // spool_id 不可解析为 int64 时保持字符串形式交给 server，让服务端回
        // 具体的 400 校验错误（而不是本地吞掉），便于排查。
        request_body["id"] = id;
    }

    dispatch_agent_request("update_spool",
                           {{"method", "PUT"}, {"spool_id", id}, {"body", request_body}},
                           [id, payload = request_body.dump()](NetworkAgent* agent, std::string& response_body) {
                               return agent->update_filament_spool(id, payload, &response_body);
                           },
                           std::move(on_ok),
                           std::move(on_err));
}

void wgtFilaManagerCloudClient::batch_delete(
    const nlohmann::json& body, SuccessFn on_ok, ErrorFn on_err)
{
    if (!check_login(on_err))
        return;

    // Tolerate both int / string arrays for "ids" (cloud schema uses int64),
    // and both "RFIDs" (cloud key) / "rfids" (legacy local key).
    auto collect_as_strings = [](const nlohmann::json& arr, std::vector<std::string>& out) {
        if (!arr.is_array()) return;
        out.reserve(out.size() + arr.size());
        for (const auto& item : arr) {
            if (item.is_string())          out.push_back(item.get<std::string>());
            else if (item.is_number_integer())  out.push_back(std::to_string(item.get<int64_t>()));
            else if (item.is_number_unsigned()) out.push_back(std::to_string(item.get<uint64_t>()));
        }
    };

    FilamentDeleteParams params;
    if (body.contains("ids"))
        collect_as_strings(body["ids"], params.ids);
    if (body.contains("RFIDs"))
        collect_as_strings(body["RFIDs"], params.rfids);
    else if (body.contains("rfids"))
        collect_as_strings(body["rfids"], params.rfids);

    if (params.ids.empty() && params.rfids.empty()) {
        if (on_err)
            on_err(0, "batch_delete requires 'ids' or 'rfids'");
        return;
    }

    dispatch_agent_request("batch_delete",
                           {{"method", "DELETE"}, {"body", body}},
                           [params](NetworkAgent* agent, std::string& response_body) mutable {
                               return agent->delete_filament_spools(params, &response_body);
                           },
                           std::move(on_ok),
                           std::move(on_err));
}

void wgtFilaManagerCloudClient::list_spools(
    const std::map<std::string, std::string>& query, SuccessFn on_ok, ErrorFn on_err)
{
    if (!check_login(on_err))
        return;

    FilamentQueryParams params;
    if (auto it = query.find("category"); it != query.end())
        params.category = it->second;
    if (auto it = query.find("status"); it != query.end())
        params.status = it->second;
    if (auto it = query.find("ids"); it != query.end())
        params.spool_id = it->second;
    else if (auto it = query.find("spoolId"); it != query.end())
        params.spool_id = it->second;
    else if (auto it2 = query.find("spool_id"); it2 != query.end())
        params.spool_id = it2->second;
    if (auto it = query.find("RFIDs"); it != query.end())
        params.rfid = it->second;
    else if (auto it = query.find("rfid"); it != query.end())
        params.rfid = it->second;
    params.offset = parse_int(query, "offset", 0);
    params.limit  = parse_int(query, "limit", 20);

    dispatch_agent_request("list_spools",
                           {{"method", "GET"}, {"query", query}},
                           [params](NetworkAgent* agent, std::string& response_body) mutable {
                               return agent->get_filament_spools(params, &response_body);
                           },
                           std::move(on_ok),
                           std::move(on_err));
}

void wgtFilaManagerCloudClient::get_filament_config(SuccessFn on_ok, ErrorFn on_err)
{
    if (!check_login(on_err))
        return;

    dispatch_agent_request("get_filament_config",
                           {{"method", "GET"}},
                           [](NetworkAgent* agent, std::string& response_body) {
                               return agent->get_filament_config(&response_body);
                           },
                           std::move(on_ok),
                           std::move(on_err));
}

}} // namespace Slic3r::GUI
