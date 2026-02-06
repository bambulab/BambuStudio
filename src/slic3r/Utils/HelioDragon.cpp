#include "HelioDragon.hpp"

#include <string>
#include <wx/string.h>
#include <wx/file.h>
#include <boost/optional.hpp>
#include <boost/asio/ip/address.hpp>
#include <vector>
#include <boost/format.hpp>
#include "PrintHost.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/PrintBase.hpp"
#include "libslic3r/Print.hpp"
#include <boost/log/trivial.hpp>
#include "../GUI/PartPlate.hpp"
#include "../GUI/GUI_App.hpp"
#include "../GUI/Event.hpp"
#include "../GUI/Plater.hpp"
#include "../GUI/NotificationManager.hpp"
#include "../GUI/HelioReleaseNote.hpp"
#include "wx/app.h"
#include "cstdio"

namespace Slic3r {

std::vector<HelioQuery::SupportedData> HelioQuery::global_supported_printers;
std::vector<HelioQuery::SupportedData> HelioQuery::global_supported_materials;
std::map<std::string, std::vector<HelioQuery::PrintPriorityOption>> HelioQuery::global_print_priority_cache;

std::string HelioQuery::last_simulation_trace_id;
std::string HelioQuery::last_optimization_trace_id;  

std::string extract_trace_id(const std::string& headers) {
    // Validate input to prevent crashes
    if (headers.empty()) {
        BOOST_LOG_TRIVIAL(warning) << "extract_trace_id: empty headers";
        return "";
    }

    try {
        std::istringstream iss(headers);
        std::string line;
        std::string trace_id;
        while (std::getline(iss, line)) {
            std::string lower_line;
            for (unsigned char c : line) lower_line += std::tolower(c);
            if (lower_line.find("trace-id:") == 0) {
                size_t colon_pos = line.find(':');
                if (colon_pos != std::string::npos) {
                    size_t value_start = colon_pos + 1;
                    while (value_start < line.size() && std::isspace(static_cast<unsigned char>(line[value_start]))) value_start++;
                    trace_id = line.substr(value_start);
                    trace_id.erase(trace_id.find_last_not_of(" \r\n") + 1);
                    break;
                }
            }
        }

        return trace_id;
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "extract_trace_id error: " << e.what();
        return "";
    }
}

std::string format_error(std::string body)
{
    std::string message;
    nlohmann::json parsed_obj = nlohmann::json::parse(body);
    if (parsed_obj.contains("errors")) {
        nlohmann::json err_arr = parsed_obj["errors"];

        if (err_arr.is_array()) {
            size_t error_count = err_arr.size();
            for (size_t i = 0; i < error_count; ++i) {
                nlohmann::json err_obj = err_arr[i];

                if (err_obj.contains("message")) {
                    std::string current_msg = err_obj["message"].get<std::string>();
                    if (error_count > 1) {
                        message += std::to_string(i + 1) + ". " + current_msg;
                    }
                    else {
                        message = current_msg;
                    }

                    if (error_count > 1 && i != error_count - 1) {
                        message += "\n";
                    }
                }
            }
        }
        else if (err_arr.is_object()) {
            if (err_arr.contains("message")) {
                message = err_arr["message"].get<std::string>();
            }
        }
    }
    return message;
}

double HelioQuery::convert_speed(float mm_per_second) {
    double value = static_cast<double>(mm_per_second) / 1000.0;
    return std::round(value * 1e9) / 1e9;
}

double HelioQuery::convert_volume_speed(float mm3_per_second) {
    double value = static_cast<double>(mm3_per_second) / 1e9;
    return std::round(value * 1e20) / 1e20;
}

void HelioQuery::request_remaining_optimizations(const std::string & helio_api_url, const std::string & helio_api_key, 
    std::function<void(int, int, const std::string&, bool, bool, bool)> func) {
    std::string query_body = R"( {
        "query": "query GetUserRemainingOpts { user { remainingOptsThisMonth addOnOptimizations isFreeTrialActive isFreeTrialClaimed subscription { name } } freeTrialEligibility }",
        "variables": {}
    } )";

    std::string url_copy = helio_api_url;
    std::string key_copy = helio_api_key;

    auto http = Http::post(url_copy);

    http.header("Content-Type", "application/json")
        .header("Authorization", "Bearer " + helio_api_key)
        .header("HelioAdditive-Client-Name", SLIC3R_APP_NAME)
        .header("HelioAdditive-Client-Version", GUI::VersionInfo::convert_full_version(SLIC3R_VERSION))
        .set_post_body(query_body);

    http.timeout_connect(20)
        .timeout_max(100)
        .on_complete([url_copy, key_copy, func](std::string body, unsigned status) {
        try {
            nlohmann::json parsed_obj = nlohmann::json::parse(body);

            if (parsed_obj.contains("data") && parsed_obj["data"].contains("user")
                && parsed_obj["data"]["user"].contains("remainingOptsThisMonth")
                && parsed_obj["data"]["user"].contains("addOnOptimizations")) {

                int global_remaining_opt_count = 0;
                int global_remaining_addon_opt_count = 0;
                std::string subscription_name = "";
                bool free_trial_eligible = false;
                bool is_free_trial_active = false;
                bool is_free_trial_claimed = false;

                if (parsed_obj["data"]["user"]["remainingOptsThisMonth"].is_number()) {
                    global_remaining_opt_count = parsed_obj["data"]["user"]["remainingOptsThisMonth"].get<int>();
                }

                if (parsed_obj["data"]["user"]["addOnOptimizations"].is_number()) {
                    global_remaining_addon_opt_count = parsed_obj["data"]["user"]["addOnOptimizations"].get<int>();
                }

                // Parse subscription name
                if (parsed_obj["data"]["user"].contains("subscription") 
                    && parsed_obj["data"]["user"]["subscription"].contains("name")
                    && parsed_obj["data"]["user"]["subscription"]["name"].is_string()) {
                    subscription_name = parsed_obj["data"]["user"]["subscription"]["name"].get<std::string>();
                }

                // Parse free trial eligibility
                if (parsed_obj["data"].contains("freeTrialEligibility") 
                    && parsed_obj["data"]["freeTrialEligibility"].is_boolean()) {
                    free_trial_eligible = parsed_obj["data"]["freeTrialEligibility"].get<bool>();
                }

                // Parse isFreeTrialActive
                if (parsed_obj["data"]["user"].contains("isFreeTrialActive") 
                    && parsed_obj["data"]["user"]["isFreeTrialActive"].is_boolean()) {
                    is_free_trial_active = parsed_obj["data"]["user"]["isFreeTrialActive"].get<bool>();
                }

                // Parse isFreeTrialClaimed
                if (parsed_obj["data"]["user"].contains("isFreeTrialClaimed") 
                    && parsed_obj["data"]["user"]["isFreeTrialClaimed"].is_boolean()) {
                    is_free_trial_claimed = parsed_obj["data"]["user"]["isFreeTrialClaimed"].get<bool>();
                }

                func(global_remaining_opt_count, global_remaining_addon_opt_count, subscription_name, free_trial_eligible, is_free_trial_active, is_free_trial_claimed);
            }
            else {
                func(0, 0, "", false, false, false);
            }
        }
        catch (...) {
            func(0, 0, "", false, false, false);
        }
            })
        .on_error([func](std::string body, std::string error, unsigned status) {
            func(0, 0, "", false, false, false);
            BOOST_LOG_TRIVIAL(error) << "Failed to obtain remaining optimization attempts: " << error << ", status: " << status;
        })
        .perform();
}

void HelioQuery::request_support_machine(const std::string helio_api_url, const std::string helio_api_key, int page)
{
    std::string query_body = R"( {
            "query": "query GetPrinters($page: Int) { printers(page: $page, pageSize: 20) { pages pageInfo { hasNextPage } objects { ... on Printer  { id name alternativeNames { bambustudio } } } } }",
            "variables": {"page": %1%}
		} )";

    query_body = boost::str(boost::format(query_body) % page);

    std::string url_copy  = helio_api_url;
    std::string key_copy  = helio_api_key;
    int         page_copy = page;

    std::string response_headers;
    auto http = Http::post(url_copy);

    http.header("Content-Type", "application/json")
        .header("Authorization", "Bearer " + helio_api_key)
        .header("HelioAdditive-Client-Name", SLIC3R_APP_NAME)
        .header("HelioAdditive-Client-Version", GUI::VersionInfo::convert_full_version(SLIC3R_VERSION))
        .set_post_body(query_body);

    http.timeout_connect(20)
        .timeout_max(100)
        .on_complete([url_copy, key_copy, page_copy](std::string body, unsigned status) {
            nlohmann::json                         parsed_obj = nlohmann::json::parse(body);
            std::vector<HelioQuery::SupportedData> supported_printers;

            try {
                if (parsed_obj.contains("data") && parsed_obj["data"].contains("printers")) {
                    auto materials = parsed_obj["data"]["printers"];
                    if (materials.contains("objects") && materials["objects"].is_array()) {
                        for (const auto &pobj : materials["objects"]) {
                            HelioQuery::SupportedData sp;
                            if (pobj.contains("id") && !pobj["id"].is_null()) { sp.id = pobj["id"].get<std::string>(); }
                            if (pobj.contains("name") && !pobj["id"].is_null()) { sp.name = pobj["name"].get<std::string>(); }

                            if (pobj.contains("alternativeNames") && pobj["alternativeNames"].is_object()) {
                                auto alternativeNames = pobj["alternativeNames"];

                                if (alternativeNames.contains("bambustudio") && !alternativeNames["bambustudio"].is_null()) {
                                    sp.native_name = alternativeNames["bambustudio"].get<std::string>();
                                }
                            }

                            supported_printers.push_back(sp);
                        }
                    }

                    HelioQuery::global_supported_printers.insert(HelioQuery::global_supported_printers.end(), supported_printers.begin(), supported_printers.end());

                    if (materials.contains("pageInfo") && materials["pageInfo"].contains("hasNextPage") && materials["pageInfo"]["hasNextPage"].get<bool>()) {
                        HelioQuery::request_support_machine(url_copy, key_copy, page_copy + 1);
                    }
                }
            } catch (...) {}
        })
        .on_error([](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << "request_support_machine error: " << error << ", status: " << status << ", body: " << body;
        })
        .perform();
}

void HelioQuery::request_support_material(const std::string helio_api_url, const std::string helio_api_key, int page)
{
    std::string query_body = R"( {
			"query": "query GetMaterias($page: Int) { materials(page: $page, pageSize: 20) { pages pageInfo { hasNextPage } objects { ... on Material  { id name feedstock alternativeNames { bambustudio } } } } }",
            "variables": {"page": %1%}
		} )";

    query_body = boost::str(boost::format(query_body) % page);

    std::string url_copy  = helio_api_url;
    std::string key_copy  = helio_api_key;
    int         page_copy = page;

    auto http = Http::post(url_copy);

    http.header("Content-Type", "application/json")
        .header("Authorization", "Bearer " + helio_api_key)
        .header("HelioAdditive-Client-Name", SLIC3R_APP_NAME)
        .header("HelioAdditive-Client-Version", GUI::VersionInfo::convert_full_version(SLIC3R_VERSION))
        .set_post_body(query_body);

    http.timeout_connect(20)
        .timeout_max(100)
        .on_complete([url_copy, key_copy, page_copy](std::string body, unsigned status) {
            BOOST_LOG_TRIVIAL(info) << "request_support_material" << body;
            nlohmann::json                         parsed_obj = nlohmann::json::parse(body);
            std::vector<HelioQuery::SupportedData> supported_materials;

            try {
                if (parsed_obj.contains("data") && parsed_obj["data"].contains("materials")) {
                    auto materials = parsed_obj["data"]["materials"];
                    if (materials.contains("objects") && materials["objects"].is_array()) {
                        for (const auto &pobj : materials["objects"]) {
                            HelioQuery::SupportedData sp;
                            if (pobj.contains("id") && !pobj["id"].is_null()) { sp.id = pobj["id"].get<std::string>(); }
                            if (pobj.contains("name") && !pobj["id"].is_null()) { sp.name = pobj["name"].get<std::string>(); }
                            if (pobj.contains("feedstock") && !pobj["feedstock"].is_null()) { sp.feedstock = pobj["feedstock"].get<std::string>(); }
                            if (pobj.contains("alternativeNames") && pobj["alternativeNames"].is_object()) {
                                auto alternativeNames = pobj["alternativeNames"];

                                //bambu materials
                                if (alternativeNames.contains("bambustudio") && !alternativeNames["bambustudio"].is_null()) {
                                    sp.native_name = alternativeNames["bambustudio"].get<std::string>();
                                }
                                //third party materials
                                else {
                                    if (pobj.contains("name") && !pobj["id"].is_null()) { sp.native_name = pobj["name"].get<std::string>(); }
                                }
                            }
                            // Only include materials with feedstock type FILAMENT
                            if (sp.feedstock == "FILAMENT") {
                                supported_materials.push_back(sp);
                            }
                        }
                    }

                    HelioQuery::global_supported_materials.insert(HelioQuery::global_supported_materials.end(), supported_materials.begin(), supported_materials.end());

                    if (materials.contains("pageInfo") && materials["pageInfo"].contains("hasNextPage") && materials["pageInfo"]["hasNextPage"].get<bool>()) {
                        HelioQuery::request_support_material(url_copy, key_copy, page_copy + 1);
                    }
                }
            } catch (...) {}
        })
        .on_error([](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << "request_support_material error: " << error << ", status: " << status << ", body: " << body;
        })
        .perform();
}

void HelioQuery::request_print_priority_options(
    const std::string& helio_api_url,
    const std::string& helio_api_key,
    const std::string& material_id,
    std::function<void(GetPrintPriorityOptionsResult)> callback
)
{
    std::string query_body = R"( {
        "query": "query GetPrintPriorityOptions($materialId: ID!) { printPriorityOptions(materialId: $materialId) { value label isAvailable description } }",
        "variables": {"materialId": "%1%"}
    } )";

    query_body = boost::str(boost::format(query_body) % material_id);

    auto http = Http::post(helio_api_url);

    // Add Accept-Language header if Chinese region
    bool is_china = GUI::wxGetApp().app_config->get("region") == "China";

    http.header("Content-Type", "application/json")
        .header("Authorization", "Bearer " + helio_api_key)
        .header("HelioAdditive-Client-Name", SLIC3R_APP_NAME)
        .header("HelioAdditive-Client-Version", GUI::VersionInfo::convert_full_version(SLIC3R_VERSION));

    if (is_china) {
        http.header("Accept-Language", "zh-CN");
    }

    http.set_post_body(query_body);

    // Use shared_ptr to prevent stack corruption in async callbacks
    auto response_headers = std::make_shared<std::string>();
    http.timeout_connect(10)
        .timeout_max(30)
        .on_header_callback([response_headers](std::string headers) {
            *response_headers += headers;
        })
        .on_complete([callback, material_id, response_headers](std::string body, unsigned status) {
            BOOST_LOG_TRIVIAL(info) << "request_print_priority_options response: " << body;

            GetPrintPriorityOptionsResult result;
            result.status = status;
            result.success = false;
            result.trace_id = extract_trace_id(*response_headers);

            try {
                nlohmann::json parsed_obj = nlohmann::json::parse(body);

                if (parsed_obj.contains("data") && parsed_obj["data"].contains("printPriorityOptions")) {
                    auto options_array = parsed_obj["data"]["printPriorityOptions"];
                    if (options_array.is_array()) {
                        for (const auto& opt : options_array) {
                            PrintPriorityOption option;
                            if (opt.contains("value") && !opt["value"].is_null()) {
                                option.value = opt["value"].get<std::string>();
                            }
                            if (opt.contains("label") && !opt["label"].is_null()) {
                                option.label = opt["label"].get<std::string>();
                            }
                            if (opt.contains("isAvailable") && !opt["isAvailable"].is_null()) {
                                option.isAvailable = opt["isAvailable"].get<bool>();
                            } else {
                                option.isAvailable = true; // Default to available
                            }
                            if (opt.contains("description") && !opt["description"].is_null()) {
                                option.description = opt["description"].get<std::string>();
                            }
                            result.options.push_back(option);
                        }
                        result.success = true;

                        // Cache the results
                        global_print_priority_cache[material_id] = result.options;
                    }
                } else if (parsed_obj.contains("errors")) {
                    // GraphQL errors
                    result.error = "API error";
                    if (parsed_obj["errors"].is_array() && !parsed_obj["errors"].empty()) {
                        result.error = parsed_obj["errors"][0].value("message", "Unknown error");
                    }
                }
            } catch (const std::exception& e) {
                result.error = std::string("Parse error: ") + e.what();
                BOOST_LOG_TRIVIAL(error) << "request_print_priority_options parse error: " << e.what()
                                        << ", trace-id: " << result.trace_id;
            }

            callback(result);
        })
        .on_error([callback, response_headers](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << "request_print_priority_options error: " << error
                                    << ", status: " << status << ", body: " << body;
            GetPrintPriorityOptionsResult result;
            result.success = false;
            result.error = error;
            result.status = status;
            result.trace_id = extract_trace_id(*response_headers);
            callback(result);
        })
        .perform();
}

std::vector<HelioQuery::PrintPriorityOption> HelioQuery::get_cached_print_priority_options(
    const std::string& material_id
)
{
    auto it = global_print_priority_cache.find(material_id);
    if (it != global_print_priority_cache.end()) {
        return it->second;
    }
    return std::vector<PrintPriorityOption>();
}

void HelioQuery::clear_print_priority_cache()
{
    global_print_priority_cache.clear();
}

std::string HelioQuery::get_helio_api_url()
{
    std::string helio_api_url;
    if (GUI::wxGetApp().app_config->get("region") == "China") {
        helio_api_url = GUI::wxGetApp().app_config->get("helio_api_china");
    } else {
        helio_api_url = GUI::wxGetApp().app_config->get("helio_api_other");
    }
    return helio_api_url;
}

std::string HelioQuery::get_helio_pat()
{
    std::string helio_pat;
    if (GUI::wxGetApp().app_config->get("region") == "China") {
        helio_pat = GUI::wxGetApp().app_config->get("helio_pat_china");
    } else {
        helio_pat = GUI::wxGetApp().app_config->get("helio_pat_other");
    }
    return helio_pat;
}

void HelioQuery::set_helio_pat(std::string pat)
{
    if (GUI::wxGetApp().app_config->get("region") == "China") {
        GUI::wxGetApp().app_config->set("helio_pat_china", pat);
    } else {
        GUI::wxGetApp().app_config->set("helio_pat_other", pat);
    }
}

void HelioQuery::request_pat_token(std::function<void(std::string)> func)
{
    std::string url_copy = "";

    if (GUI::wxGetApp().app_config->get("region") == "China") {
        url_copy = "https://api.helioam.cn/rest/auth/anonymous_token/bambustudio";
    } else {
        url_copy = "https://api.helioadditive.com/rest/auth/anonymous_token/bambustudio";
    }

    auto http = Http::get(url_copy);
    http.timeout_connect(20)
        .timeout_max(100)
        .on_complete([url_copy, func](std::string body, unsigned status) {
            //success
            if (status == 200) {
                nlohmann::json parsed_obj = nlohmann::json::parse(body);
                try {
                    if (parsed_obj.contains("pat") && parsed_obj["pat"].is_string()) {
                        func(parsed_obj["pat"].get<std::string>());
                    }
                    else {
                        func("error");
                    }

                }
                catch (...) {}
            }
            else if (status == 429) {
                func("not_enough");
            }
        })
        .on_error([func](std::string body, std::string error, unsigned status) {
            if (status == 429) {
                func("not_enough");
            }
            else {
                func("error");
            }
            //BOOST_LOG_TRIVIAL(info) << (boost::format("request pat token error: %1%, message: %2%") % error % body).str());
        })
        .perform();
}

void HelioQuery::optimization_feedback(const std::string helio_api_url, const std::string helio_api_key, std::string optimization_id, float rating, std::string comment)
{
    std::string query_body = R"({
        "query": "mutation AddOptimizationFeedback($input: OptimizationFeedbackInput!) { addOptimizationFeedback(input: $input) { id } }",
        "variables": {
            "input": {
                "optimizationId": "%1%",
                "rating": %2%,
                "comment": "%3%"
            }
        }
    })";

    query_body = boost::str(boost::format(query_body)
        % optimization_id
        % rating
        % comment);
    auto http = Http::post(helio_api_url);

    http.header("Content-Type", "application/json")
        .header("Authorization", helio_api_key)
        .header("HelioAdditive-Client-Name", SLIC3R_APP_NAME)
        .header("HelioAdditive-Client-Version", GUI::VersionInfo::convert_full_version(SLIC3R_VERSION))
        .set_post_body(query_body);

    http.timeout_connect(20)
        .timeout_max(100)
        .on_complete([=](std::string body, unsigned status) {
            BOOST_LOG_TRIVIAL(info) << "optimization_feedback response: " << body << ", status: " << status;
        })
        .on_error([=](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << "optimization_feedback error: " << error
                                     << ", status: " << status
                                     << ", body: " << body
                                     << ", optimization_id: " << optimization_id;
        })
        .perform();
}

HelioQuery::PresignedURLResult HelioQuery::create_presigned_url(const std::string helio_api_url, const std::string helio_api_key)
{
    HelioQuery::PresignedURLResult res;
    std::string                    query_body = R"( {
			"query": "query getPresignedUrl($fileName: String!) { getPresignedUrl(fileName: $fileName) { mimeType url key } }",
			"variables": {
				"fileName": "test.gcode"
			}
		} )";

    auto http = Http::post(helio_api_url);

    http.header("Content-Type", "application/json")
        .header("Authorization", helio_api_key)
        .header("HelioAdditive-Client-Name", SLIC3R_APP_NAME)
        .header("HelioAdditive-Client-Version", GUI::VersionInfo::convert_full_version(SLIC3R_VERSION))
        .set_post_body(query_body);

    if (GUI::wxGetApp().app_config->get("language") == "zh_CN") {
        http.header("Accept-Language", "zh-CN");
    }

    http.timeout_connect(20)
        .timeout_max(100)
        .on_header_callback([&res](std::string header_line) {
            std::string lower_line;
            for (unsigned char c : header_line) {
                lower_line += static_cast<char>(std::tolower(c));
            }
            auto trace_id = extract_trace_id(lower_line);
            if(!trace_id.empty()){
                res.trace_id = trace_id;
            }
        })
        .on_complete([&res](std::string body, unsigned status) {
            nlohmann::json parsed_obj = nlohmann::json::parse(body);
            res.status                = status;
            if (parsed_obj.contains("error")) {
                res.error = parsed_obj["error"];
            } else {
                res.key      = parsed_obj["data"]["getPresignedUrl"]["key"];
                res.mimeType = parsed_obj["data"]["getPresignedUrl"]["mimeType"];
                res.url      = parsed_obj["data"]["getPresignedUrl"]["url"];
            }
        })
        .on_error([&res](std::string body, std::string error, unsigned status) {
            res.error  = (boost::format("error: %1%, message: %2%") % error % body).str();
            res.status = status;
        })
        .perform_sync();

    return res;
};

HelioQuery::UploadFileResult HelioQuery::upload_file_to_presigned_url(const std::string file_path_string, const std::string upload_url)
{
    UploadFileResult res;

    Http http = Http::put(upload_url);
    http.header("Content-Type", "application/octet-stream")
        .header("HelioAdditive-Client-Name", SLIC3R_APP_NAME)
        .header("HelioAdditive-Client-Version", GUI::VersionInfo::convert_full_version(SLIC3R_VERSION));

    boost::filesystem::path file_path(file_path_string);

    /*check origin file*/
    try {
        std::string original_path_name = file_path.parent_path().string() + "/original_" + file_path.filename().string();
        if (fs::exists(original_path_name) && fs::is_regular_file(original_path_name)) {
            file_path = boost::filesystem::path(original_path_name);
            BOOST_LOG_TRIVIAL(error) << "use original gcode file for helio action";
        }
    }
    catch (...) {}

    http.set_put_body(file_path)
        .on_header_callback([&res](std::string header_line) {
            std::string lower_line;
            for (unsigned char c : header_line) {
                lower_line += static_cast<char>(std::tolower(c));
            }
            auto trace_id = extract_trace_id(lower_line);
            if (!trace_id.empty()) {
                res.trace_id = trace_id;
            }
        })
        .on_complete([&res](std::string body, unsigned status) {
            if (status == 200)
                res.success = true;
            else
                res.success = false;
        })
        .on_error([&res](std::string body, std::string error, unsigned status) {
            res.success = false;
            res.error   = (boost::format("status: %1%, error: %2%, %3%") % status % body % error).str();
        })
        .perform_sync();

    return res;
}

HelioQuery::PollResult HelioQuery::poll_gcode_status(const std::string& helio_api_url,
                                                     const std::string& helio_api_key,
                                                     const std::string& gcode_id) 
{
    HelioQuery::PollResult result;
    result.success = false;

    std::string poll_query = R"( {
        "query": "query GcodeV2($id: ID!) { gcodeV2(id: $id) { id name sizeKb status progress errors errorsV2 { line type } restrictions restrictionsV2 { description restriction } } }",
        "variables": { "id": "%1%" }
    } )";
    std::string poll_body = (boost::format(poll_query) % gcode_id).str();

    auto poll_http = Http::post(helio_api_url);
    poll_http.header("Content-Type", "application/json")
        .header("Authorization", helio_api_key)
        .set_post_body(poll_body)
        .timeout_connect(10)
        .timeout_max(30)
        .on_complete([&result](std::string poll_body, unsigned poll_status) {
            try {
                json poll_obj = json::parse(poll_body);
                if (!poll_obj["data"]["gcodeV2"].is_null()) {
                    auto gcode_data = poll_obj["data"]["gcodeV2"];
                    
                    // Verify status field exists and is valid before considering the response successful
                    if (gcode_data.contains("status") && gcode_data["status"].is_string()) {
                        result.status_str = gcode_data["status"];
                        result.progress = gcode_data["progress"];
                        result.sizeKb = gcode_data["sizeKb"];
                        
                        // Parse errors if present
                        if (gcode_data.contains("errors") && !gcode_data["errors"].is_null()) {
                            auto errors_data = gcode_data["errors"];
                            if (errors_data.is_array()) {
                                for (const auto& error : errors_data) {
                                    if (error.is_string()) {
                                        result.errors.push_back(error.get<std::string>());
                                    }
                                }
                            }
                        }
                        
                        // Parse errorsV2 for more detailed error info
                        if (gcode_data.contains("errorsV2") && !gcode_data["errorsV2"].is_null()) {
                            auto errors_v2 = gcode_data["errorsV2"];
                            if (errors_v2.is_array()) {
                                for (const auto& err : errors_v2) {
                                    std::string detailed_error;
                                    if (err.contains("type") && err["type"].is_string()) {
                                        detailed_error = err["type"].get<std::string>();
                                    }
                                    if (err.contains("line") && !err["line"].is_null()) {
                                        detailed_error += " (line " + std::to_string(err["line"].get<int>()) + ")";
                                    }
                                    if (!detailed_error.empty()) {
                                        result.errors.push_back(detailed_error);
                                    }
                                }
                            }
                        }

                        // Parse restrictions if present
                        if (gcode_data.contains("restrictions") && !gcode_data["restrictions"].is_null()) {
                            auto restrictions_data = gcode_data["restrictions"];
                            if (restrictions_data.is_array()) {
                                for (const auto& r : restrictions_data) {
                                    if (r.is_string()) {
                                        result.restrictions.push_back(r.get<std::string>());
                                    }
                                }
                            }
                        }

                        // Parse restrictionsV2 for detailed restriction info
                        if (gcode_data.contains("restrictionsV2") && !gcode_data["restrictionsV2"].is_null()) {
                            auto restrictions_v2 = gcode_data["restrictionsV2"];
                            if (restrictions_v2.is_array()) {
                                for (const auto& r : restrictions_v2) {
                                    std::string detail;
                                    if (r.contains("restriction") && r["restriction"].is_string()) {
                                        detail = r["restriction"].get<std::string>();
                                    }
                                    if (r.contains("description") && r["description"].is_string()) {
                                        std::string desc = r["description"].get<std::string>();
                                        if (!desc.empty()) {
                                            detail += (detail.empty() ? "" : ": ") + desc;
                                        }
                                    }
                                    if (!detail.empty()) {
                                        result.restrictions.push_back(detail);
                                    }
                                }
                            }
                        }

                        result.success = true;
                    }
                }
            }
            catch (...) {
                //tudo
            }
        })
        .on_error([&result](std::string /*poll_body*/, std::string /*poll_error*/, unsigned /*poll_status*/) {
        // Optionally handle polling error
        })
        .perform_sync();

    return result;
}

HelioQuery::CreateGCodeResult HelioQuery::create_gcode(const std::string key,
                                                       const std::string helio_api_url,
                                                       const std::string helio_api_key,
                                                       const std::string printer_id,
                                                       const std::string filament_id)
{
    HelioQuery::CreateGCodeResult res;
    std::string                   query_body_template = R"( {
			"query": "mutation CreateGcode($input: CreateGcodeInputV2!) { createGcodeV2(input: $input) { id name sizeKb status progress } }",
			"variables": {
			  "input": {
				"name": "%1%",
				"printerId": "%2%",
				"materialId": "%3%",
				"gcodeKey": "%4%",
				"isSingleShell": true
			  }
			}
		  } )";

    std::vector<std::string> key_split;
    boost::split(key_split, key, boost::is_any_of("/"));

    std::string gcode_name = key_split.back();

    std::string query_body = (boost::format(query_body_template) % gcode_name % printer_id % filament_id % key).str();

    auto http = Http::post(helio_api_url);

    http.header("Content-Type", "application/json")
        .header("Authorization", helio_api_key)
        .header("HelioAdditive-Client-Name", SLIC3R_APP_NAME)
        .header("HelioAdditive-Client-Version", GUI::VersionInfo::convert_full_version(SLIC3R_VERSION))
        .set_post_body(query_body);

    if (GUI::wxGetApp().app_config->get("language") == "zh_CN") {
        http.header("Accept-Language", "zh-CN");
    }

    http.timeout_connect(20)
        .timeout_max(100)
        .on_header_callback([&res](std::string header_line) {
            std::string lower_line;
            for (unsigned char c : header_line) {
                lower_line += static_cast<char>(std::tolower(c));
            }
            auto trace_id = extract_trace_id(lower_line);
            if (!trace_id.empty()) {
                res.trace_id = trace_id;
            }
        })
        .on_complete([&res, helio_api_url, helio_api_key](std::string body, unsigned status) {
        res.status = status;
        try {
            json parsed_obj = json::parse(body);
            if (parsed_obj.contains("errors")) {
                std::string message = format_error(body);
                res.error = message;
                res.success = false;
                return;
            }

            auto gcode = parsed_obj["data"]["createGcodeV2"];
            if (gcode.is_null()) {
                res.success = false;
                res.error = _u8L("Failed to create GCodeV2");
                return;
            }

            res.success = true;
            res.id = gcode["id"];
            res.name = gcode["name"];
            res.sizeKb = gcode["sizeKb"];
            res.status_str = gcode["status"];
            res.progress = gcode["progress"];

            // Check for errors in initial response
            if (gcode.contains("errors") && !gcode["errors"].is_null()) {
                auto errors_data = gcode["errors"];
                if (errors_data.is_array() && !errors_data.empty()) {
                    std::string error_message;
                    for (size_t i = 0; i < errors_data.size(); ++i) {
                        if (errors_data[i].is_string()) {
                            std::string current_error = errors_data[i].get<std::string>();
                            if (errors_data.size() > 1) {
                                error_message += std::to_string(i + 1) + ". " + current_error;
                                if (i != errors_data.size() - 1) {
                                    error_message += "\n";
                                }
                            } else {
                                error_message = current_error;
                            }
                        }
                    }
                    res.success = false;
                    res.error = error_message;
                    return;
                }
            }

            int poll_count = 0;
            while (res.status_str != "READY" && res.status_str != "ERROR" && res.status_str != "RESTRICTED" && poll_count < 60) {
                std::this_thread::sleep_for(std::chrono::seconds(2));

                PollResult poll_res = poll_gcode_status(helio_api_url, helio_api_key, res.id);

                if (poll_res.success) {
                    res.status_str = poll_res.status_str;
                    res.progress = poll_res.progress;
                    res.sizeKb = poll_res.sizeKb;
                    
                    // Check for errors during polling
                    if (!poll_res.errors.empty()) {
                        std::string error_message;
                        for (size_t i = 0; i < poll_res.errors.size(); ++i) {
                            if (poll_res.errors.size() > 1) {
                                error_message += std::to_string(i + 1) + ". " + poll_res.errors[i];
                                if (i != poll_res.errors.size() - 1) {
                                    error_message += "\n";
                                }
                            } else {
                                error_message = poll_res.errors[i];
                            }
                        }
                        res.success = false;
                        res.error = error_message;
                        return;
                    }
                    
                    // Handle ERROR/RESTRICTED status even if errors array is empty
                    if (res.status_str == "ERROR" || res.status_str == "RESTRICTED") {
                        res.success = false;
                        // For RESTRICTED, use restriction details if available
                        if (res.status_str == "RESTRICTED" && !poll_res.restrictions.empty()) {
                            std::string restriction_message;
                            for (size_t i = 0; i < poll_res.restrictions.size(); ++i) {
                                if (poll_res.restrictions.size() > 1) {
                                    restriction_message += std::to_string(i + 1) + ". " + poll_res.restrictions[i];
                                    if (i != poll_res.restrictions.size() - 1) restriction_message += "\n";
                                } else {
                                    restriction_message = poll_res.restrictions[i];
                                }
                            }
                            res.error = restriction_message;
                        } else {
                            res.error = (boost::format("GCode creation failed with status: %1%") % res.status_str).str();
                        }
                        return;
                    }
                }

                poll_count++;
            }
        }
        catch (...) {
            res.success = false;
            res.error = _u8L("Failed to parse response");
        }
        })
        .on_error([&res](std::string body, std::string error, unsigned status) {
            res.success = false;
            res.error  = error;
            res.status = status;
        })
        .perform_sync();

    return res;
}

std::string HelioQuery::generate_simulation_graphql_query(const std::string &gcode_id, 
                                                          float temperatureStabilizationHeight,
                                                          float airTemperatureAboveBuildPlate, 
                                                          float stabilizedAirTemperature)
{
    std::string name = generateTimestampedString();

    std::string base_query = R"( {
        "query": "mutation CreateSimulation($input: CreateSimulationInput!) { createSimulation(input: $input) { id name progress status gcode { id name } printer { id name } material { id name } reportJsonUrl thermalIndexGcodeUrl estimatedSimulationDurationSeconds insertedAt updatedAt } }",
        "variables": {
            "input": {
                "name": "%1%",
                "gcodeId": "%2%",
                "simulationSettings": {
    )";

    std::vector<std::string> settings_fields;

    if (temperatureStabilizationHeight != -1) {
        settings_fields.push_back(boost::str(boost::format(R"(                    "temperatureStabilizationHeight": %1%)") % temperatureStabilizationHeight));
    }

    if (airTemperatureAboveBuildPlate != -1) {
        settings_fields.push_back(boost::str(boost::format(R"(                    "airTemperatureAboveBuildPlate": %1%)") % airTemperatureAboveBuildPlate));
    }

    if (stabilizedAirTemperature != -1) {
        settings_fields.push_back(boost::str(boost::format(R"(                    "stabilizedAirTemperature": %1%)") % stabilizedAirTemperature));
    }

    std::string settings_block;
    if (!settings_fields.empty()) { settings_block = boost::join(settings_fields, ",\n"); }

    std::string full_query = base_query + settings_block + R"(
                }
            }
        }
    } )";

    boost::format formatter(full_query);
    formatter % name % gcode_id;

    return formatter.str();
}

std::string HelioQuery::generate_optimization_graphql_query(const std::string& gcode_id,
    const std::string& printPriority,
    bool optimizeOuterwall,
    bool useOldMethod,
    float temperatureStabilizationHeight,
    float airTemperatureAboveBuildPlate,
    float stabilizedAirTemperature,
    double minVelocity,
    double maxVelocity,
    double minExtruderFlowRate,
    double maxExtruderFlowRate,
    int layersToOptimizeStart,
    int layersToOptimizeEnd)
{
    std::string name = generateTimestampedString();

    // basic query structure
    std::string base_query = R"( {
        "query": "mutation CreateOptimization($input: CreateOptimizationInput!) { createOptimization(input: $input) { id name progress status gcode { id name } printer { id name } material { id name } insertedAt updatedAt } }",
        "variables": {
            "input": {
                "name": "%1%",
                "gcodeId": "%2%",
                "simulationSettings": {
    )";

    // Step 1.SimulationSettingsInput
    std::vector<std::string> simulation_fields;
    if (temperatureStabilizationHeight != -1) {
        simulation_fields.push_back(boost::str(boost::format(R"("temperatureStabilizationHeight": %1%)") % temperatureStabilizationHeight));
    }
    
    if (airTemperatureAboveBuildPlate != -1) {
        simulation_fields.push_back(boost::str(boost::format(R"("airTemperatureAboveBuildPlate": %1%)") % airTemperatureAboveBuildPlate));
    }

    if (stabilizedAirTemperature != -1) {
        simulation_fields.push_back(boost::str(boost::format(R"("stabilizedAirTemperature": %1%)") % stabilizedAirTemperature));
    }

    std::string simulation_block = boost::join(simulation_fields, ",\n");

    // Step 2. OptimizationSettingsInput
    std::vector<std::string> optimization_fields;

    if (useOldMethod) {
        // OLD METHOD: Use optimizeOuterwall boolean (fallback when API print priority options unavailable)
        optimization_fields.push_back(boost::str(boost::format(R"("optimizeOuterwall": %1%)")
            % (optimizeOuterwall ? "true" : "false")));
    } else if (!printPriority.empty()) {
        // NEW METHOD: Use printPriority string
        optimization_fields.push_back(boost::str(boost::format(R"("printPriority": "%1%")") % printPriority));
    }

    if (minVelocity != -1) {
        optimization_fields.push_back(boost::str(boost::format(R"("minVelocity": %1%)") % minVelocity));
    }

    if (maxVelocity != -1) {
        optimization_fields.push_back(boost::str(boost::format(R"("maxVelocity": %1%)") % maxVelocity));
    }
    
    if (minExtruderFlowRate != -1) {
        optimization_fields.push_back(boost::str(boost::format(R"("minExtruderFlowRate": %1%)") % minExtruderFlowRate));
    }
    
    if (maxExtruderFlowRate != -1) {
        optimization_fields.push_back(boost::str(boost::format(R"("maxExtruderFlowRate": %1%)") % maxExtruderFlowRate));
    }
    
    optimization_fields.push_back(R"("residualStrategySettings": {"strategy": "LINEAR"})");

    // Set optimizer to HYBRID
    optimization_fields.push_back(R"("optimizer": "HYBRID")");

    // Default to layer 2 if not specified
    int actualStartLayer = (layersToOptimizeStart == 1) ? 2 : layersToOptimizeStart;

    optimization_fields.push_back(boost::str(boost::format(
        R"("layersToOptimize": [{"fromLayer": %1%, "toLayer": %2%}])"
    ) % actualStartLayer% layersToOptimizeEnd));

    // optimization_block
    std::string optimization_block = boost::join(optimization_fields, ",\n");

    // full query
    std::string full_query = base_query + simulation_block + R"(
                },
                "optimizationSettings": {
    )" + optimization_block + R"(
                }
            }
        }
    } )";

    boost::format formatter(full_query);
    formatter% name% gcode_id;

    return formatter.str();
}

std::string HelioQuery::create_optimization_default_get(const std::string helio_api_url, const std::string helio_api_key, const std::string gcode_id)
{
    std::string res;
    std::string query_body = generate_default_optimization_query(gcode_id);

    auto http = Http::post(helio_api_url);

    http.header("Content-Type", "application/json")
        .header("Authorization", helio_api_key)
        .header("HelioAdditive-Client-Name", SLIC3R_APP_NAME)
        .header("HelioAdditive-Client-Version", GUI::VersionInfo::convert_full_version(SLIC3R_VERSION))
        .set_post_body(query_body);

    std::string response_headers;
    http.timeout_connect(20)
        .timeout_max(100)
        .on_header_callback([&response_headers](std::string headers) {
            response_headers += headers;
        })
        .on_complete([&res](std::string body, unsigned status) {
            nlohmann::json parsed_obj = nlohmann::json::parse(body);
        })
        .on_error([&res, &response_headers](std::string body, std::string error, unsigned status) {
            auto err = error;
            auto c =response_headers;
        })
        .perform_sync();

    return res;
}

HelioQuery::CreateSimulationResult HelioQuery::create_simulation(const std::string helio_api_url,
                                                                 const std::string helio_api_key,
                                                                 const std::string gcode_id,
                                                                 SimulationInput sinput)
{
    /*field processing*/
    const float chamber_temp = sinput.chamber_temp;
    DynamicPrintConfig print_config = GUI::wxGetApp().preset_bundle->full_config();
    const float layer_threshold = 20; //Default values from Helio
    std::string bed_temp_key = Slic3r::get_bed_temp_1st_layer_key((Slic3r::BedType)(print_config.option("curr_bed_type")->getInt()));

    const float bed_temp = print_config.option<ConfigOptionInts>(bed_temp_key)->get_at(0);
    float initial_room_airtemp = -1;
    if (chamber_temp > 0.0f) {
        initial_room_airtemp = (chamber_temp + bed_temp) / 2;
    }

    const float initial_room_temp_kelvin        = initial_room_airtemp == -1 ? -1 : initial_room_airtemp + 273.15;
    const float object_proximity_airtemp_kelvin = chamber_temp == -1 ? -1 : chamber_temp + 273.15;
    const float layer_threshold_meters          = layer_threshold / 1000;

    std::string query_body = generate_simulation_graphql_query(gcode_id,
                                                    layer_threshold_meters,
                                                    initial_room_temp_kelvin,
                                                    object_proximity_airtemp_kelvin
    );

    HelioQuery::CreateSimulationResult res;
    auto http = Http::post(helio_api_url);

    http.header("Content-Type", "application/json")
        .header("Authorization", helio_api_key)
        .header("HelioAdditive-Client-Name", SLIC3R_APP_NAME)
        .header("HelioAdditive-Client-Version", GUI::VersionInfo::convert_full_version(SLIC3R_VERSION))
        .set_post_body(query_body);

    if (GUI::wxGetApp().app_config->get("language") == "zh_CN") {
        http.header("Accept-Language", "zh-CN");
    }

    http.timeout_connect(20)
        .timeout_max(100)
        .on_complete([&res](std::string body, unsigned status) {
            nlohmann::json parsed_obj = nlohmann::json::parse(body);
            res.status                = status;
            if (parsed_obj.contains("errors")) {
                std::string message = format_error(body);
                res.error   = message;
                res.success = false;
            } else {
                res.success = true;
                res.id      = parsed_obj["data"]["createSimulation"]["id"];
                res.name    = parsed_obj["data"]["createSimulation"]["name"];
            }
        })
        .on_error([&res](std::string body, std::string error, unsigned status) {
            res.success = false;
            res.error  = error;
            res.status = status;
        })
        .on_header_callback([&res](std::string header_line) {
            std::string lower_line;
            for (unsigned char c : header_line) {
                lower_line += static_cast<char>(std::tolower(c));
            }
            auto trace_id = extract_trace_id(lower_line);
            if (!trace_id.empty()) {
                res.trace_id = trace_id;
            }
        })
        .perform_sync();

    return res;
}

void HelioQuery::stop_simulation(const std::string helio_api_url, const std::string helio_api_key, const std::string simulation_id)
{
    std::string query_body_template = R"( {
        "query": "mutation StopSimulation($id: ID!) { stopSimulation(id: $id) }",
        "variables": {
            "id": "%1%"
        }
    } )";

    std::string query_body = (boost::format(query_body_template) % simulation_id).str();
    auto http = Http::post(helio_api_url);

    http.header("Content-Type", "application/json")
        .header("Authorization", helio_api_key)
        .header("HelioAdditive-Client-Name", SLIC3R_APP_NAME)
        .header("HelioAdditive-Client-Version", GUI::VersionInfo::convert_full_version(SLIC3R_VERSION))
        .set_post_body(query_body);

    http.timeout_connect(20)
        .timeout_max(100)
        .on_header_callback([](std::string header_line) {
            std::string lower_line;
            for (unsigned char c : header_line) {
                lower_line += static_cast<char>(std::tolower(c));
            }
            auto trace_id = extract_trace_id(lower_line);
            if (!trace_id.empty()) {
                //auto trace_id = trace_id;
            }
        })
        .on_complete([=](std::string body, unsigned status) {
            BOOST_LOG_TRIVIAL(info) << (boost::format("stop_simulation: success")).str();
        })
        .on_error([=](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(info) << (boost::format("stop_simulation: failed")).str();
         })
        .perform_sync();
}

HelioQuery::CheckSimulationProgressResult HelioQuery::check_simulation_progress(const std::string helio_api_url,
                                                                                const std::string helio_api_key,
                                                                                const std::string simulation_id)
{
    HelioQuery::CheckSimulationProgressResult res;
    std::string                               query_body_template = R"( {
							"query": "query Simulation($id: ID!) { simulation(id: $id) { id name progress status thermalIndexGcodeUrl printInfo { printOutcome printOutcomeDescription temperatureDirection temperatureDirectionDescription caveats { caveatType description } } speedFactor suggestedFixes { category extraDetails fix orderIndex } } }",
							"variables": {
								"id": "%1%"
							}
						} )";

    std::string query_body = (boost::format(query_body_template) % simulation_id).str();

    auto http = Http::post(helio_api_url);

    http.header("Content-Type", "application/json")
        .header("Authorization", helio_api_key)
        .header("HelioAdditive-Client-Name", SLIC3R_APP_NAME)
        .header("HelioAdditive-Client-Version", GUI::VersionInfo::convert_full_version(SLIC3R_VERSION))
        .set_post_body(query_body);

    if (GUI::wxGetApp().app_config->get("language") == "zh_CN") {
        http.header("Accept-Language", "zh-CN");
    }

    http.timeout_connect(20)
        .timeout_max(100)
        .on_header_callback([&res](std::string header_line) {
            std::string lower_line;
            for (unsigned char c : header_line) {
                lower_line += static_cast<char>(std::tolower(c));
            }
            auto trace_id = extract_trace_id(lower_line);
            if (!trace_id.empty()) {
                res.trace_id = trace_id;
            }
        })
        .on_complete([&res](std::string body, unsigned status) {
            nlohmann::json parsed_obj = nlohmann::json::parse(body);
            res.status                = status;
            if (parsed_obj.contains("errors")) {
                std::string message = format_error(body);
                res.error = message;
            } else {
                if (parsed_obj["data"]["simulation"]["status"] == "FAILED") {
                    res.error = _u8L("Helio simulation task failed");
                }

                res.id = parsed_obj["data"]["simulation"]["id"];
                res.name = parsed_obj["data"]["simulation"]["name"];
                res.progress = parsed_obj["data"]["simulation"]["progress"];
                res.is_finished = parsed_obj["data"]["simulation"]["status"] == "FINISHED";
                if (res.is_finished) {
                    res.url = parsed_obj["data"]["simulation"]["thermalIndexGcodeUrl"];
                    
                    // Parse printInfo if present
                    if (parsed_obj["data"]["simulation"].contains("printInfo") && 
                        !parsed_obj["data"]["simulation"]["printInfo"].is_null()) {
                        auto printInfo = parsed_obj["data"]["simulation"]["printInfo"];
                        PrintInfo info;
                        info.printOutcome = printInfo["printOutcome"];
                        if (printInfo.contains("printOutcomeDescription") && printInfo["printOutcomeDescription"].is_string()) {
                            info.printOutcomeDescription = printInfo["printOutcomeDescription"];
                        }
                        info.temperatureDirection = printInfo["temperatureDirection"];
                        if (printInfo.contains("temperatureDirectionDescription") && printInfo["temperatureDirectionDescription"].is_string()) {
                            info.temperatureDirectionDescription = printInfo["temperatureDirectionDescription"];
                        }
                        
                        if (printInfo.contains("caveats") && printInfo["caveats"].is_array()) {
                            for (const auto& caveat : printInfo["caveats"]) {
                                Caveat c;
                                c.caveatType = caveat["caveatType"];
                                c.description = caveat["description"];
                                info.caveats.push_back(c);
                            }
                        }
                        res.simulationResult.printInfo = info;
                    }
                    
                    // Parse speedFactor if present
                    if (parsed_obj["data"]["simulation"].contains("speedFactor") && 
                        !parsed_obj["data"]["simulation"]["speedFactor"].is_null()) {
                        res.simulationResult.speedFactor = parsed_obj["data"]["simulation"]["speedFactor"];
                    }
                    
                    // Parse suggestedFixes if present
                    if (parsed_obj["data"]["simulation"].contains("suggestedFixes") && 
                        parsed_obj["data"]["simulation"]["suggestedFixes"].is_array()) {
                        for (const auto& fixJson : parsed_obj["data"]["simulation"]["suggestedFixes"]) {
                            SuggestedFix fix;
                            if (fixJson.contains("category") && fixJson["category"].is_string()) {
                                fix.category = fixJson["category"];
                            }
                            if (fixJson.contains("fix") && fixJson["fix"].is_string()) {
                                fix.fix = fixJson["fix"];
                            }
                            if (fixJson.contains("orderIndex") && !fixJson["orderIndex"].is_null()) {
                                fix.orderIndex = fixJson["orderIndex"].get<int>();
                            }
                            if (fixJson.contains("extraDetails") && fixJson["extraDetails"].is_array()) {
                                for (const auto& detail : fixJson["extraDetails"]) {
                                    if (detail.is_string()) {
                                        fix.extraDetails.push_back(detail.get<std::string>());
                                    }
                                }
                            }
                            res.simulationResult.suggestedFixes.push_back(fix);
                        }
                    }
                }
            }
        })
        .on_error([&res](std::string body, std::string error, unsigned status) {
            res.error  = error;
            res.status = status;
        })
        .perform_sync();

    return res;
}

Slic3r::HelioQuery::CreateOptimizationResult HelioQuery::create_optimization(const std::string helio_api_url, 
                                                                             const std::string helio_api_key, 
                                                                             const std::string gcode_id,
                                                                             OptimizationInput oinput)
{

    std::string query_body;

    /*print priority*/
    const std::string print_priority = oinput.print_priority;

    /*SimulationInput*/
    const float chamber_temp = oinput.chamber_temp;
    DynamicPrintConfig print_config = GUI::wxGetApp().preset_bundle->full_config();
    const float layer_threshold = 20; //Default values from Helio
    std::string bed_temp_key = Slic3r::get_bed_temp_1st_layer_key((Slic3r::BedType)(print_config.option("curr_bed_type")->getInt()));

    const float bed_temp = print_config.option<ConfigOptionInts>(bed_temp_key)->get_at(0);
    float initial_room_airtemp = -1;
    if (chamber_temp > 0.0f) {
        initial_room_airtemp = (chamber_temp + bed_temp) / 2;
    }

    const float initial_room_temp_kelvin = initial_room_airtemp == -1 ? -1 : initial_room_airtemp + 273.15;
    const float object_proximity_airtemp_kelvin = chamber_temp == -1 ? -1 : chamber_temp + 273.15;
    const float layer_threshold_meters = layer_threshold / 1000;

    /*field processing*/
    if (!oinput.isDefault()) {
        
        const double min_velocity = convert_speed(oinput.min_velocity);
        const double max_velocity = convert_speed(oinput.max_velocity);
        const double min_volumetric_speed = convert_volume_speed(oinput.min_volumetric_speed);
        const double max_volumetric_speed = convert_volume_speed(oinput.max_volumetric_speed);

        query_body = generate_optimization_graphql_query(gcode_id,
            print_priority,
            oinput.optimize_outerwall,
            oinput.use_old_method,
            layer_threshold_meters,
            initial_room_temp_kelvin,
            object_proximity_airtemp_kelvin,
            min_velocity,
            max_velocity,
            min_volumetric_speed,
            max_volumetric_speed,
            oinput.layers_to_optimize[0],
            oinput.layers_to_optimize[1]);
    }
    else {
        query_body = generate_optimization_graphql_query(gcode_id,
            print_priority,
            oinput.optimize_outerwall,
            oinput.use_old_method,
            layer_threshold_meters,
            initial_room_temp_kelvin,
            object_proximity_airtemp_kelvin,
            oinput.min_velocity,
            oinput.max_velocity,
            oinput.min_volumetric_speed,
            oinput.max_volumetric_speed,
            oinput.layers_to_optimize[0],
            oinput.layers_to_optimize[1]);
    }
    

    HelioQuery::CreateOptimizationResult res;
    auto http = Http::post(helio_api_url);
    http.header("Content-Type", "application/json")
        .header("Authorization", helio_api_key)
        .header("HelioAdditive-Client-Name", SLIC3R_APP_NAME)
        .header("HelioAdditive-Client-Version", GUI::VersionInfo::convert_full_version(SLIC3R_VERSION))
        .set_post_body(query_body);

    if (GUI::wxGetApp().app_config->get("language") == "zh_CN") {
        http.header("Accept-Language", "zh-CN");
    }

    http.timeout_connect(20)
        .timeout_max(100)
        .on_header_callback([&res](std::string header_line) {
            std::string lower_line;
            for (unsigned char c : header_line) {
                lower_line += static_cast<char>(std::tolower(c));
            }
            auto trace_id = extract_trace_id(lower_line);
            if (!trace_id.empty()) {
                res.trace_id = trace_id;
            }
        })
        .on_complete([&res](std::string body, unsigned status) {
            nlohmann::json parsed_obj = nlohmann::json::parse(body);
            res.status                = status;
            if (parsed_obj.contains("errors")) {
                std::string message = format_error(body);
                res.error   = message;
                res.success = false;
            } else {
                res.success = true;
                res.id      = parsed_obj["data"]["createOptimization"]["id"];
                res.name    = parsed_obj["data"]["createOptimization"]["name"];
            }
        })
        .on_error([&res](std::string body, std::string error, unsigned status) {
            res.success = false;
            res.error   = error;
            res.status  = status;
        })
        .perform_sync();

    return res;
}

void HelioQuery::stop_optimization(const std::string helio_api_url, const std::string helio_api_key, const std::string optimization_id)
{
    std::string query_body_template = R"( {
        "query": "mutation StopOptimization($id: ID!) { stopOptimization(id: $id) }",
        "variables": {
            "id": "%1%"
        }
    } )";

    std::string query_body = (boost::format(query_body_template) % optimization_id).str();
    auto http = Http::post(helio_api_url);

    http.header("Content-Type", "application/json")
        .header("Authorization", helio_api_key)
        .header("HelioAdditive-Client-Name", SLIC3R_APP_NAME)
        .header("HelioAdditive-Client-Version", GUI::VersionInfo::convert_full_version(SLIC3R_VERSION))
        .set_post_body(query_body);

    http.timeout_connect(20)
        .timeout_max(100)
        .on_header_callback([](std::string header_line) {
            std::string lower_line;
            for (unsigned char c : header_line) {
                lower_line += static_cast<char>(std::tolower(c));
            }
            auto trace_id = extract_trace_id(lower_line);
            if (!trace_id.empty()) {
                //res.trace_id = trace_id;
            }
        })
        .on_complete([=](std::string body, unsigned status) {
            BOOST_LOG_TRIVIAL(info) << (boost::format("stop_optimization: success")).str();
        })
        .on_error([=](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(info) << (boost::format("stop_optimization: failed")).str();
        })
        .perform_sync();
}

Slic3r::HelioQuery::CheckOptimizationResult HelioQuery::check_optimization_progress(const std::string helio_api_url,
                                                                                    const std::string helio_api_key,
                                                                                    const std::string optimization_id)
{
    HelioQuery::CheckOptimizationResult res;
    std::string                               query_body_template = R"( {
							"query": "query Optimization($id: ID!) { optimization(id: $id) { id name progress status optimizedGcodeWithThermalIndexesUrl qualityStdImprovement qualityMeanImprovement } }",
							"variables": {
								"id": "%1%"
							}
						} )";

    std::string query_body = (boost::format(query_body_template) % optimization_id).str();

    auto http = Http::post(helio_api_url);

    http.header("Content-Type", "application/json")
        .header("Authorization", helio_api_key)
        .header("HelioAdditive-Client-Name", SLIC3R_APP_NAME)
        .header("HelioAdditive-Client-Version", GUI::VersionInfo::convert_full_version(SLIC3R_VERSION))
        .set_post_body(query_body);

    if (GUI::wxGetApp().app_config->get("language") == "zh_CN") {
        http.header("Accept-Language", "zh-CN");
    }

    http.timeout_connect(20)
        .timeout_max(100)
        .on_header_callback([&res](std::string header_line) {
            std::string lower_line;
            for (unsigned char c : header_line) {
                lower_line += static_cast<char>(std::tolower(c));
            }
            auto trace_id = extract_trace_id(lower_line);
            if (!trace_id.empty()) {
                res.trace_id = trace_id;
            }
        })
        .on_complete([&res](std::string body, unsigned status) {
            nlohmann::json parsed_obj = nlohmann::json::parse(body);
            res.status                = status;

            if (parsed_obj.contains("errors")) {
                std::string message = format_error(body);
                res.error = message;
            } else {
                if (parsed_obj["data"]["optimization"]["status"] == "FAILED") {
                    res.error = _u8L("Helio optimization task failed");
                }

                res.id = parsed_obj["data"]["optimization"]["id"];
                res.name = parsed_obj["data"]["optimization"]["name"];
                res.progress = parsed_obj["data"]["optimization"]["progress"];
                res.is_finished = parsed_obj["data"]["optimization"]["status"] == "FINISHED";
                if (res.is_finished) {
                    res.qualityStdImprovement = parsed_obj["data"]["optimization"]["qualityStdImprovement"];
                    res.qualityMeanImprovement = parsed_obj["data"]["optimization"]["qualityMeanImprovement"];
                    res.url = parsed_obj["data"]["optimization"]["optimizedGcodeWithThermalIndexesUrl"];
                }
            }
        })
        .on_error([&res](std::string body, std::string error, unsigned status) {
            res.error  = error;
            res.status = status;
        })
        .perform_sync();

    return res;
}

std::string HelioQuery::generate_default_optimization_query(const std::string& gcode_id)
{
    std::string base_query = R"( {
        "query": "query DefaultOptimizationSettings($gcodeId: ID!) { defaultOptimizationSettings(gcodeId: $gcodeId) { minVelocity maxVelocity minVelocityIncrement minExtruderFlowRate maxExtruderFlowRate tolerance maxIterations reductionStrategySettings { strategy autolinearDoCriticality autolinearDoFitness autolinearDoInterpolation autolinearCriticalityMaxNodesDensity autolinearCriticalityThreshold autolinearFitnessMaxNodesDensity autolinearFitnessThreshold autolinearInterpolationLevels linearNodesLimit } residualStrategySettings { strategy exponentialPenaltyHigh exponentialPenaltyLow } layersToOptimize { fromLayer toLayer } optimizer } }",
        "variables": {
            "gcodeId": "%1%"
        }
    } )";

    boost::format formatter(base_query);
    std::string query_body = (formatter % gcode_id).str();

    return query_body;
}

void HelioBackgroundProcess::helio_thread_start(std::mutex&                                slicing_mutex,
                                                std::condition_variable&                   slicing_condition,
                                                BackgroundSlicingProcess::State&           slicing_state,
                                                std::unique_ptr<GUI::NotificationManager>& notification_manager)
{
    m_thread = create_thread([this, &slicing_mutex, &slicing_condition, &slicing_state, &notification_manager] {
        this->helio_threaded_process_start(slicing_mutex, slicing_condition, slicing_state, notification_manager);
    });
}

void HelioBackgroundProcess::stop_current_helio_action()
{
    if (!current_simulation_result.id.empty()) {
        HelioQuery::stop_simulation(helio_api_url, helio_api_key, current_simulation_result.id);
    }

    if (!current_optimization_result.id.empty()) {
        HelioQuery::stop_optimization(helio_api_url, helio_api_key, current_optimization_result.id);
    }
}

void HelioBackgroundProcess::feedback_current_helio_action(float rating, std::string commend)
{
    if (!current_optimization_result.id.empty()) {
        HelioQuery::optimization_feedback(helio_api_url, helio_api_key, current_optimization_result.id, rating, commend);
    }
}

void HelioBackgroundProcess::clear_helio_file_cache()
{
    //delete origin file
    if (m_gcode_result) {
        try {
            fs::path original_path = m_gcode_result->filename;
            std::string original_path_name = original_path.parent_path().string() + "/original_" + original_path.filename().string();
            int deleted = boost::nowide::remove(original_path_name.c_str());

            if (deleted != 0) {
                BOOST_LOG_TRIVIAL(error) << "Helio Failed to delete file";
            }
        }
        catch (...) {
            BOOST_LOG_TRIVIAL(error) << "Helio Failed to delete file";
        }
    }
}

void HelioBackgroundProcess::helio_threaded_process_start(std::mutex&                                slicing_mutex,
                                                          std::condition_variable&                   slicing_condition,
                                                          BackgroundSlicingProcess::State&           slicing_state,
                                                          std::unique_ptr<GUI::NotificationManager>& notification_manager)
{
    set_state(STATE_RUNNING);

    std::unique_lock<std::mutex> slicing_lck(slicing_mutex);
    slicing_condition.wait(slicing_lck, [this, &slicing_state]() {
        return slicing_state == BackgroundSlicingProcess::STATE_FINISHED || slicing_state == BackgroundSlicingProcess::STATE_CANCELED ||
               slicing_state == BackgroundSlicingProcess::STATE_IDLE;
    });
    slicing_lck.unlock();

    if ((slicing_state == BackgroundSlicingProcess::STATE_FINISHED || slicing_state == BackgroundSlicingProcess::STATE_IDLE) &&
        !was_canceled()) {
        wxPostEvent(GUI::wxGetApp().plater(), GUI::SimpleEvent(GUI::EVT_HELIO_PROCESSING_STARTED));

        Slic3r::PrintBase::SlicingStatus status = Slic3r::PrintBase::SlicingStatus(0.0, "Helio: Process Started");
        status.is_helio = true;
        Slic3r::SlicingStatusEvent*      evt    = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
        wxQueueEvent(GUI::wxGetApp().plater(), evt);

        BOOST_LOG_TRIVIAL(debug) << boost::format("url: %1%, key: %2%") % helio_api_url % helio_api_key;

        /*check api url*/
        if (helio_api_url.empty()) {
            set_state(STATE_CANCELED);
            Slic3r::HelioCompletionEvent *evt = new Slic3r::HelioCompletionEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, "", "", false,
                _u8L("Helio API endpoint is empty, please check the configuration."));
            wxQueueEvent(GUI::wxGetApp().plater(), evt);
            return;
        }

        /*check Personal assecc token */
        if (helio_origin_key.empty()) {
            set_state(STATE_CANCELED);
            Slic3r::HelioCompletionEvent *evt = new Slic3r::HelioCompletionEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, "", "", false,
                _u8L("Personal assecc token is empty, please fill in the correct token."));
            wxQueueEvent(GUI::wxGetApp().plater(), evt);
            return;
        }

        HelioQuery::PresignedURLResult create_presigned_url_res = HelioQuery::create_presigned_url(helio_api_url, helio_api_key);

        if (create_presigned_url_res.error.empty() && create_presigned_url_res.status == 200 && !was_canceled()) {
            status = Slic3r::PrintBase::SlicingStatus(5, _u8L("Helio: Presigned URL Created"));
            status.is_helio = true;
            evt    = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
            wxQueueEvent(GUI::wxGetApp().plater(), evt);

            HelioQuery::UploadFileResult upload_file_res = HelioQuery::upload_file_to_presigned_url(m_gcode_result->filename,
                                                                                                    create_presigned_url_res.url);

            if (upload_file_res.success && !was_canceled()) {
                status = Slic3r::PrintBase::SlicingStatus(10, _u8L("Helio: file succesfully uploaded"));
                evt    = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
                wxQueueEvent(GUI::wxGetApp().plater(), evt);

                HelioQuery::CreateGCodeResult create_gcode_res = HelioQuery::create_gcode(create_presigned_url_res.key, helio_api_url,
                                                                                          helio_api_key, printer_id, filament_id);

                if (action == 0) {
                    create_simulation_step(create_gcode_res, notification_manager);
                }
                else if (action == 1) {
                    create_optimization_step(create_gcode_res, notification_manager);
                }

            } else {
                set_state(STATE_CANCELED);

                Slic3r::HelioCompletionEvent* evt = new Slic3r::HelioCompletionEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, "", "", false,
                                                                                     _u8L("Helio: file upload failed"));
                wxQueueEvent(GUI::wxGetApp().plater(), evt);
            }
        } else {
            std::string presigned_url_message = (boost::format("error: %1%") % create_presigned_url_res.error).str();

            if (create_presigned_url_res.status == 401) {
                presigned_url_message += "\n ";
                presigned_url_message += _u8L("Please make sure you have the corrent API key set in preferences.");
            }

            set_state(STATE_CANCELED);

            Slic3r::HelioCompletionEvent* evt = new Slic3r::HelioCompletionEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, "", "", false,
                                                                                 presigned_url_message);
            wxQueueEvent(GUI::wxGetApp().plater(), evt);
        }
    } else {
        set_state(STATE_CANCELED);
    }
}

void HelioBackgroundProcess::create_simulation_step(HelioQuery::CreateGCodeResult create_gcode_res, std::unique_ptr<GUI::NotificationManager>& notification_manager)
{
    if (create_gcode_res.success && !was_canceled()) {
        Slic3r::PrintBase::SlicingStatus status = Slic3r::PrintBase::SlicingStatus(15, _u8L("Helio: GCode created successfully"));
        status.is_helio = true;
        Slic3r::SlicingStatusEvent*      evt    = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
        wxQueueEvent(GUI::wxGetApp().plater(), evt);

        const std::string gcode_id = create_gcode_res.id;
        HelioQuery::CreateSimulationResult create_simulation_res = HelioQuery::create_simulation(helio_api_url, helio_api_key, gcode_id, simulation_input_data);
        current_simulation_result = create_simulation_res;
        current_optimization_result.reset();
        // Clear previous stored result since we're starting a new simulation
        clear_last_simulation_result();

        if (!create_simulation_res.trace_id.empty()) {
            HelioQuery::last_simulation_trace_id = create_simulation_res.trace_id;  
        }

        if (create_simulation_res.success && !was_canceled()) {
            status = Slic3r::PrintBase::SlicingStatus(20, _u8L("Helio: simulation successfully created"));
            evt    = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
            wxQueueEvent(GUI::wxGetApp().plater(), evt);

            int times_tried            = 0;
            int max_unsuccessful_tries = 5;
            int times_queried          = 0;

            while (!was_canceled()) {
                HelioQuery::CheckSimulationProgressResult check_simulation_progress_res =
                    HelioQuery::check_simulation_progress(helio_api_url, helio_api_key, create_simulation_res.id);

                if (check_simulation_progress_res.status == 200) {
                    times_tried = 0;
                    if (check_simulation_progress_res.error.empty()) {
                        std::string trailing_dots = "";

                        for (int i = 0; i < (times_queried % 3); i++) {
                            trailing_dots += "....";
                        }

                        status = Slic3r::PrintBase::SlicingStatus(35 + (80 - 35) * (check_simulation_progress_res.progress / 100.0f),
                                                                  _u8L("Helio: simulation working") + trailing_dots);
                        evt    = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
                        wxQueueEvent(GUI::wxGetApp().plater(), evt);
                        if (check_simulation_progress_res.is_finished) {
                            // Start loading preview in background immediately (don't wait for dialog)
                            int original_time_seconds = static_cast<int>(m_gcode_result->print_statistics.modes[0].time);
                            std::string url = check_simulation_progress_res.url;
                            std::string filename = m_gcode_result->filename;
                            HelioQuery::SimulationResult sim_result = check_simulation_progress_res.simulationResult;

                            // Store simulation result to current plate for later access (e.g., "View Summary" button)
                            GUI::wxGetApp().plater()->CallAfter([sim_result, original_time_seconds]() {
                                auto* plate = GUI::wxGetApp().plater()->get_partplate_list().get_curr_plate();
                                if (plate) {
                                    HelioPlateResult helio_result;
                                    helio_result.action = 0;  // Simulation
                                    helio_result.simulation_result = sim_result;
                                    helio_result.original_print_time_seconds = original_time_seconds;
                                    helio_result.is_valid = true;
                                    plate->set_helio_result(helio_result);
                                }
                            });

                            // Start preview loading in background thread immediately
                            std::string simulated_gcode_path = create_path_for_simulated_gcode(filename);
                            HelioQuery::RatingData rating_data;
                            save_downloaded_gcode_and_load_preview(url,
                                                                   simulated_gcode_path, filename,
                                                                   notification_manager, rating_data);

                            // Show simulation results dialog on main thread (non-blocking for preview)
                            // Capture roles_times for calculating optimizable sections
                            auto roles_times = m_gcode_result->print_statistics.modes[0].roles_times;
                            GUI::wxGetApp().plater()->CallAfter([sim_result, original_time_seconds, roles_times]() {
                                // This runs on the main thread
                                GUI::HelioSimulationResultsDialog results_dlg(nullptr, sim_result, original_time_seconds, roles_times);
                                results_dlg.ShowModal();
                            });
                            break;
                        }
                    } else {
                        set_state(STATE_CANCELED);

                        BOOST_LOG_TRIVIAL(error) << "Helio simulation progress check returned error: " << check_simulation_progress_res.error;
                        
                        std::string error_msg = _u8L("Helio: simulation failed") + "\n" + check_simulation_progress_res.error;
                        Slic3r::HelioCompletionEvent* evt = new Slic3r::HelioCompletionEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, "", "",
                                                                                             false, error_msg);
                        wxQueueEvent(GUI::wxGetApp().plater(), evt);
                        break;
                    }
                } else {
                    times_tried++;
                    status = Slic3r::PrintBase::SlicingStatus(35, _u8L("Helio: Simulation check failed"));
                    evt    = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
                    wxQueueEvent(GUI::wxGetApp().plater(), evt);

                    if (times_tried >= max_unsuccessful_tries)
                        break;
                }

                times_queried++;
                boost::this_thread ::sleep_for(boost::chrono::seconds(3));
            }

        } else {
            set_state(STATE_CANCELED);

            std::string error;
            try{
                error = _u8L("Helio: Failed to create Simulation") + "\n" + create_simulation_res.error;
            }
            catch (...){}
            
            Slic3r::HelioCompletionEvent* evt = new Slic3r::HelioCompletionEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, "", "", false, error);
            wxQueueEvent(GUI::wxGetApp().plater(), evt);
        }

    } else {
        set_state(STATE_CANCELED);

        std::string error;
        try {
            error = _u8L("Helio: Failed to create GCode") + "\n" + create_gcode_res.error;
        }
        catch (...) {}

        Slic3r::HelioCompletionEvent* evt = new Slic3r::HelioCompletionEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, "", "", false, error);
        wxQueueEvent(GUI::wxGetApp().plater(), evt);
    }
}

void HelioBackgroundProcess::create_optimization_step(HelioQuery::CreateGCodeResult create_gcode_res,std::unique_ptr<GUI::NotificationManager>& notification_manager)
{
    if (create_gcode_res.success && !was_canceled()) {
        Slic3r::PrintBase::SlicingStatus status = Slic3r::PrintBase::SlicingStatus(15,_u8L( "Helio: GCode created successfully"));
        status.is_helio = true;
        Slic3r::SlicingStatusEvent* evt = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
        wxQueueEvent(GUI::wxGetApp().plater(), evt);

        const std::string gcode_id = create_gcode_res.id;
        HelioQuery::CreateOptimizationResult create_optimization_res = HelioQuery::create_optimization(helio_api_url, helio_api_key, gcode_id, optimization_input_data);
        current_optimization_result = create_optimization_res;
        current_simulation_result.reset();
        // Clear previous stored result since we're starting a new optimization
        clear_last_simulation_result();

        if (!create_optimization_res.trace_id.empty()) {
            HelioQuery::last_optimization_trace_id = create_optimization_res.trace_id;
        }

        if (create_optimization_res.success && !was_canceled()) {
            status = Slic3r::PrintBase::SlicingStatus(20, _u8L("Helio: optimization successfully created"));
            evt = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
            wxQueueEvent(GUI::wxGetApp().plater(), evt);

            int times_tried = 0;
            int max_unsuccessful_tries = 5;
            int times_queried = 0;

            while (!was_canceled()) {
                HelioQuery::CheckOptimizationResult check_optimzaion_progress_res =
                    HelioQuery::check_optimization_progress(helio_api_url, helio_api_key, create_optimization_res.id);

                if (check_optimzaion_progress_res.status == 200) {
                    times_tried = 0;
                    if (check_optimzaion_progress_res.error.empty()) {
                        std::string trailing_dots = "";

                        for (int i = 0; i < (times_queried % 3); i++) {
                            trailing_dots += "....";
                        }

                        status = Slic3r::PrintBase::SlicingStatus(35 + (80 - 35) * (check_optimzaion_progress_res.progress / 100.0f),
                            _u8L("Helio: optimization working") + trailing_dots);
                        evt = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
                        wxQueueEvent(GUI::wxGetApp().plater(), evt);
                        if (check_optimzaion_progress_res.is_finished) {
                            // notification_manager->push_notification((boost::format("Helio: Optimzaion finished.")).str());
                            std::string optimized_gcode_path = HelioBackgroundProcess::create_path_for_optimization_gcode(
                                m_gcode_result->filename);


                            HelioQuery::RatingData rating_data;
                            rating_data.action = 1;
                            rating_data.qualityMeanImprovement = check_optimzaion_progress_res.qualityMeanImprovement;
                            rating_data.qualityStdImprovement =check_optimzaion_progress_res.qualityStdImprovement;

                            HelioBackgroundProcess::save_downloaded_gcode_and_load_preview(check_optimzaion_progress_res.url,
                                optimized_gcode_path, m_gcode_result->filename,
                                notification_manager, rating_data);
                            break;
                        }
                    }
                    else {
                        set_state(STATE_CANCELED);
                        
                        BOOST_LOG_TRIVIAL(error) << "Helio optimization progress check returned error: " << check_optimzaion_progress_res.error;
                        
                        std::string error_msg = _u8L("Helio: optimization failed") + "\n" + check_optimzaion_progress_res.error;
                        Slic3r::HelioCompletionEvent* evt = new Slic3r::HelioCompletionEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, "", "",
                            false, error_msg);
                        wxQueueEvent(GUI::wxGetApp().plater(), evt);
                        break;
                    }
                }
                else {
                    times_tried++;

                    status = Slic3r::PrintBase::SlicingStatus(35, _u8L("Helio: Optimzaion check failed"));
                    evt = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
                    wxQueueEvent(GUI::wxGetApp().plater(), evt);

                    if (times_tried >= max_unsuccessful_tries)
                        break;
                }

                times_queried++;
                boost::this_thread::sleep_for(boost::chrono::seconds(3));
            }

        }
        else {
            set_state(STATE_CANCELED);

            std::string error;
            try {
                error = _u8L("Helio: Failed to create Optimization") + "\n" + create_optimization_res.error;
            }
            catch (...) {}

            Slic3r::HelioCompletionEvent* evt = new Slic3r::HelioCompletionEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, "", "", false,
                (boost::format("Helio: Failed to create Optimization\n%1%") % create_optimization_res.error).str());
            wxQueueEvent(GUI::wxGetApp().plater(), evt);
        }

    }
    else {
        set_state(STATE_CANCELED);

        std::string error;
        try {
            error = _u8L("Helio: Failed to create GCode") + "\n" + create_gcode_res.error;
        }
        catch (...) {}

        Slic3r::HelioCompletionEvent* evt = new Slic3r::HelioCompletionEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, "", "", false, error);
        wxQueueEvent(GUI::wxGetApp().plater(), evt);
    }
}
void HelioBackgroundProcess::save_downloaded_gcode_and_load_preview(std::string                                file_download_url,
                                                                    std::string                                helio_gcode_path,
                                                                    std::string                                tmp_path,
                                                                    std::unique_ptr<GUI::NotificationManager>& notification_manager,
                                                                    HelioQuery::RatingData                     rating_data)
{
    auto        http            = Http::get(file_download_url);
    unsigned    response_status = 0;
    std::string downloaded_gcode;
    std::string response_error;

    int number_of_attempts                  = 0;
    int max_attempts                        = 7;
    int number_of_seconds_till_next_attempt = 0;

    while (response_status != 200 && !was_canceled()) {
        if (number_of_seconds_till_next_attempt <= 0) {
            http.on_complete([&downloaded_gcode, &response_error, &response_status](std::string body, unsigned status) {
                    response_status = status;
                    if (status == 200) {
                        downloaded_gcode = body;
                    } else {
                        response_error = (boost::format("status: %1%, error: %2%") % status % body).str();
                    }
                })
                .on_error([&response_error, &response_status](std::string body, std::string error, unsigned status) {
                    response_status = status;
                    response_error  = (boost::format("status: %1%, error: %2%") % status % body).str();
                })
                .perform_sync();

            if (response_status != 200) {
                number_of_attempts++;
                Slic3r::PrintBase::SlicingStatus status = Slic3r::PrintBase::SlicingStatus(
                    80, (boost::format("Helio: Could not download file. Attempts left %1%") % (max_attempts - number_of_attempts)).str());
                status.is_helio = true;
                Slic3r::SlicingStatusEvent* evt = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
                wxQueueEvent(GUI::wxGetApp().plater(), evt);
                number_of_seconds_till_next_attempt = number_of_attempts * 5;
            }

            if (response_status == 200) {
                response_error = "";
                break;
            }

            else if (number_of_attempts >= max_attempts) {
                response_error = "Max attempts reached but file was not found";
                break;
            }

        } else {
            Slic3r::PrintBase::SlicingStatus status = Slic3r::PrintBase::SlicingStatus(80,
                                                                                       (boost::format("Helio: Next attemp in %1% seconds") %
                                                                                        number_of_seconds_till_next_attempt)
                                                                                           .str());
            status.is_helio = true;
            Slic3r::SlicingStatusEvent*      evt    = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
            wxQueueEvent(GUI::wxGetApp().plater(), evt);
        }
            boost::this_thread::sleep_for(boost::chrono::seconds(1));
            number_of_seconds_till_next_attempt--;
    }

    if (response_error.empty() && !was_canceled()) {
        wxFile file(wxString::FromUTF8(helio_gcode_path), wxFile::write);
        if (file.IsOpened()) {
            file.Write(downloaded_gcode.data(), downloaded_gcode.size());
            file.Close();
        }

        Slic3r::PrintBase::SlicingStatus status = Slic3r::PrintBase::SlicingStatus(100, _u8L("Helio: GCode downloaded successfully"));
        status.is_helio = true;
        Slic3r::SlicingStatusEvent*      evt    = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
        wxQueueEvent(GUI::wxGetApp().plater(), evt);
        HelioBackgroundProcess::load_helio_file_to_viwer(helio_gcode_path, tmp_path, rating_data);
    } else {
        set_state(STATE_CANCELED);

        std::string error;
        try {
            error = _u8L("Helio: GCode download failed") + "\n" + response_error;
        }
        catch (...) {}

        Slic3r::HelioCompletionEvent* evt =
            new Slic3r::HelioCompletionEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, "", "", false, error);
        wxQueueEvent(GUI::wxGetApp().plater(), evt);
    }
}

void HelioBackgroundProcess::load_helio_file_to_viwer(std::string file_path, std::string tmp_path, HelioQuery::RatingData rating_data)
{
    const Vec3d origin = GUI::wxGetApp().plater()->get_partplate_list().get_current_plate_origin();
    m_gcode_processor.set_xy_offset(origin(0), origin(1));
    m_gcode_processor.process_file(file_path);
    auto res       = &m_gcode_processor.result();
    m_gcode_result = res;

    set_state(STATE_FINISHED);

    Slic3r::HelioCompletionEvent* evt = new Slic3r::HelioCompletionEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, file_path, tmp_path, true, "", rating_data.action, rating_data.qualityMeanImprovement, rating_data.qualityStdImprovement);
    wxQueueEvent(GUI::wxGetApp().plater(), evt);
}

void HelioBackgroundProcess::set_helio_api_key(std::string api_key) { helio_api_key = api_key; }
void HelioBackgroundProcess::set_gcode_result(Slic3r::GCodeProcessorResult* gcode_result) { m_gcode_result = gcode_result; }

// Get recent simulations and optimizations for History dialog
HelioQuery::GetRecentRunsResult HelioQuery::get_recent_runs(const std::string& helio_api_url, const std::string& helio_api_key)
{
    GetRecentRunsResult result;
    result.success = false;

    // GraphQL query for both optimizations and simulations
    std::string query_body = R"({
        "query": "query GetRecentRuns {\n  optimizations {\n    objects {\n      ... on Optimization {\n        id\n        name\n        status\n        optimizedGcodeWithThermalIndexesUrl\n        qualityMeanImprovement\n        qualityStdImprovement\n        gcode {\n          gcodeUrl\n          gcodeKey\n          material {\n            id\n            name\n          }\n          printer {\n            id\n            name\n          }\n          numberOfLayers\n          slicer\n        }\n      }\n    }\n  }\n  simulations {\n    objects {\n      ... on Simulation {\n        id\n        name\n        status\n        thermalIndexGcodeUrl\n        gcode {\n          gcodeUrl\n          gcodeKey\n          material {\n            id\n            name\n          }\n          printer {\n            id\n            name\n          }\n          numberOfLayers\n          slicer\n        }\n        printInfo {\n          printOutcome\n        }\n      }\n    }\n  }\n}",
        "variables": {}
    })";

    auto http = Http::post(helio_api_url);

    http.header("Content-Type", "application/json")
        .header("Authorization", "Bearer " + helio_api_key)
        .header("HelioAdditive-Client-Name", SLIC3R_APP_NAME)
        .header("HelioAdditive-Client-Version", GUI::VersionInfo::convert_full_version(SLIC3R_VERSION))
        .set_post_body(query_body);

    // Create temporary vectors to avoid issues with reference capture and reallocation
    std::vector<OptimizationRun> temp_optimizations;
    std::vector<SimulationRun> temp_simulations;
    temp_optimizations.reserve(50);  // Reserve space to avoid reallocation
    temp_simulations.reserve(50);

    bool success = false;
    std::string error_msg;
    unsigned status_code = 0;

    http.timeout_connect(20)
        .timeout_max(100)
        .on_complete([&temp_optimizations, &temp_simulations, &success, &error_msg, &status_code](std::string body, unsigned status) {
            status_code = status;

            BOOST_LOG_TRIVIAL(info) << "get_recent_runs response status: " << status;
            BOOST_LOG_TRIVIAL(info) << "get_recent_runs response body length: " << body.length();

            if (status != 200) {
                success = false;
                error_msg = "HTTP status: " + std::to_string(status);
                BOOST_LOG_TRIVIAL(error) << "get_recent_runs failed with status: " << status;
                return;
            }

            try {
                nlohmann::json parsed = nlohmann::json::parse(body);

                // Parse optimizations
                if (parsed.contains("data") && parsed["data"].contains("optimizations")) {
                    auto opts = parsed["data"]["optimizations"];
                    BOOST_LOG_TRIVIAL(info) << "Found optimizations in response";
                    if (opts.contains("objects") && opts["objects"].is_array()) {
                        BOOST_LOG_TRIVIAL(info) << "Optimizations array size: " << opts["objects"].size();
                        for (const auto& obj : opts["objects"]) {
                            try {
                                OptimizationRun run;

                                if (obj.contains("id") && !obj["id"].is_null()) {
                                    run.id = obj["id"].get<std::string>();
                                }
                                if (obj.contains("name") && !obj["name"].is_null()) {
                                    run.name = obj["name"].get<std::string>();
                                    // Parse timestamp from name
                                    run.timestamp = HelioQuery::parse_timestamp_from_name(run.name);
                                }
                                if (obj.contains("status") && !obj["status"].is_null()) {
                                    run.status = obj["status"].get<std::string>();
                                }

                                // Parse thermal index GCode URL
                                if (obj.contains("optimizedGcodeWithThermalIndexesUrl") && !obj["optimizedGcodeWithThermalIndexesUrl"].is_null()) {
                                    run.optimized_gcode_with_thermal_indexes_url = obj["optimizedGcodeWithThermalIndexesUrl"].get<std::string>();
                                }

                                // Parse quality improvements (these are strings like "HIGH", "LOW", "MEDIUM")
                                if (obj.contains("qualityMeanImprovement") && !obj["qualityMeanImprovement"].is_null()) {
                                    run.quality_mean_improvement = obj["qualityMeanImprovement"].get<std::string>();
                                }
                                if (obj.contains("qualityStdImprovement") && !obj["qualityStdImprovement"].is_null()) {
                                    run.quality_std_improvement = obj["qualityStdImprovement"].get<std::string>();
                                }

                                // Parse gcode object
                                if (obj.contains("gcode") && !obj["gcode"].is_null()) {
                                    auto gcode = obj["gcode"];

                                    if (gcode.contains("gcodeUrl") && !gcode["gcodeUrl"].is_null()) {
                                        run.gcode_url = gcode["gcodeUrl"].get<std::string>();
                                    }
                                    if (gcode.contains("gcodeKey") && !gcode["gcodeKey"].is_null()) {
                                        run.gcode_key = gcode["gcodeKey"].get<std::string>();
                                    }
                                    if (gcode.contains("numberOfLayers") && !gcode["numberOfLayers"].is_null()) {
                                        run.number_of_layers = gcode["numberOfLayers"].get<int>();
                                    }
                                    if (gcode.contains("slicer") && !gcode["slicer"].is_null()) {
                                        run.slicer = gcode["slicer"].get<std::string>();
                                    }

                                    // Parse material
                                    if (gcode.contains("material") && !gcode["material"].is_null()) {
                                        auto material = gcode["material"];
                                        if (material.contains("id") && !material["id"].is_null()) {
                                            run.material_id = material["id"].get<std::string>();
                                        }
                                        if (material.contains("name") && !material["name"].is_null()) {
                                            run.material_name = material["name"].get<std::string>();
                                        }
                                    }

                                    // Parse printer
                                    if (gcode.contains("printer") && !gcode["printer"].is_null()) {
                                        auto printer = gcode["printer"];
                                        if (printer.contains("id") && !printer["id"].is_null()) {
                                            run.printer_id = printer["id"].get<std::string>();
                                        }
                                        if (printer.contains("name") && !printer["name"].is_null()) {
                                            run.printer_name = printer["name"].get<std::string>();
                                        }
                                    }
                                }

                                // Include all runs for now (debugging)
                                BOOST_LOG_TRIVIAL(info) << "Parsed optimization: " << run.name << ", status: " << run.status;
                                temp_optimizations.push_back(std::move(run));
                            } catch (const std::exception& e) {
                                BOOST_LOG_TRIVIAL(error) << "Failed to parse optimization: " << e.what();
                            }
                        }
                    }
                }

                BOOST_LOG_TRIVIAL(info) << "Total optimizations parsed: " << temp_optimizations.size();

                // Parse simulations
                if (parsed.contains("data") && parsed["data"].contains("simulations")) {
                    auto sims = parsed["data"]["simulations"];
                    BOOST_LOG_TRIVIAL(info) << "Found simulations in response";
                    if (sims.contains("objects") && sims["objects"].is_array()) {
                        BOOST_LOG_TRIVIAL(info) << "Simulations array size: " << sims["objects"].size();
                        for (const auto& obj : sims["objects"]) {
                            try {
                                SimulationRun run;

                                if (obj.contains("id") && !obj["id"].is_null()) {
                                    run.id = obj["id"].get<std::string>();
                                }
                                if (obj.contains("name") && !obj["name"].is_null()) {
                                    run.name = obj["name"].get<std::string>();
                                    // Parse timestamp from name
                                    run.timestamp = HelioQuery::parse_timestamp_from_name(run.name);
                                }
                                if (obj.contains("status") && !obj["status"].is_null()) {
                                    run.status = obj["status"].get<std::string>();
                                }

                                // Parse thermal index GCode URL
                                if (obj.contains("thermalIndexGcodeUrl") && !obj["thermalIndexGcodeUrl"].is_null()) {
                                    run.thermal_index_gcode_url = obj["thermalIndexGcodeUrl"].get<std::string>();
                                }

                                // Parse printInfo
                                if (obj.contains("printInfo") && !obj["printInfo"].is_null()) {
                                    auto printInfo = obj["printInfo"];
                                    if (printInfo.contains("printOutcome") && !printInfo["printOutcome"].is_null()) {
                                        run.print_outcome = printInfo["printOutcome"].get<std::string>();
                                    }
                                }

                                // Parse gcode object
                                if (obj.contains("gcode") && !obj["gcode"].is_null()) {
                                    auto gcode = obj["gcode"];

                                    if (gcode.contains("gcodeUrl") && !gcode["gcodeUrl"].is_null()) {
                                        run.gcode_url = gcode["gcodeUrl"].get<std::string>();
                                    }
                                    if (gcode.contains("gcodeKey") && !gcode["gcodeKey"].is_null()) {
                                        run.gcode_key = gcode["gcodeKey"].get<std::string>();
                                    }
                                    if (gcode.contains("numberOfLayers") && !gcode["numberOfLayers"].is_null()) {
                                        run.number_of_layers = gcode["numberOfLayers"].get<int>();
                                    }
                                    if (gcode.contains("slicer") && !gcode["slicer"].is_null()) {
                                        run.slicer = gcode["slicer"].get<std::string>();
                                    }

                                    // Parse material
                                    if (gcode.contains("material") && !gcode["material"].is_null()) {
                                        auto material = gcode["material"];
                                        if (material.contains("id") && !material["id"].is_null()) {
                                            run.material_id = material["id"].get<std::string>();
                                        }
                                        if (material.contains("name") && !material["name"].is_null()) {
                                            run.material_name = material["name"].get<std::string>();
                                        }
                                    }

                                    // Parse printer
                                    if (gcode.contains("printer") && !gcode["printer"].is_null()) {
                                        auto printer = gcode["printer"];
                                        if (printer.contains("id") && !printer["id"].is_null()) {
                                            run.printer_id = printer["id"].get<std::string>();
                                        }
                                        if (printer.contains("name") && !printer["name"].is_null()) {
                                            run.printer_name = printer["name"].get<std::string>();
                                        }
                                    }
                                }

                                // Include all runs for now (debugging)
                                BOOST_LOG_TRIVIAL(info) << "Parsed simulation: " << run.name << ", status: " << run.status;
                                temp_simulations.push_back(std::move(run));
                            } catch (const std::exception& e) {
                                BOOST_LOG_TRIVIAL(error) << "Failed to parse simulation: " << e.what();
                            }
                        }
                    }
                }

                BOOST_LOG_TRIVIAL(info) << "Total simulations parsed: " << temp_simulations.size();

                success = true;

            } catch (const std::exception& e) {
                success = false;
                error_msg = std::string("JSON parse error: ") + e.what();
                BOOST_LOG_TRIVIAL(error) << "Failed to parse get_recent_runs response: " << e.what();
            }
        })
        .on_error([&success, &error_msg, &status_code](std::string body, std::string error, unsigned status) {
            success = false;
            error_msg = error;
            status_code = status;
            BOOST_LOG_TRIVIAL(error) << "get_recent_runs error: " << error << ", status: " << status;
        })
        .perform_sync();

    // Now safely move the data to result
    result.success = success;
    result.error = error_msg;
    result.status = status_code;

    if (success) {
        // Sort by timestamp descending (newest first)
        try {
            std::sort(temp_optimizations.begin(), temp_optimizations.end(),
                [](const OptimizationRun& a, const OptimizationRun& b) {
                    return a.timestamp > b.timestamp;
                });

            std::sort(temp_simulations.begin(), temp_simulations.end(),
                [](const SimulationRun& a, const SimulationRun& b) {
                    return a.timestamp > b.timestamp;
                });
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "Failed to sort recent runs: " << e.what();
        }

        // Move the sorted data to result
        result.optimizations = std::move(temp_optimizations);
        result.simulations = std::move(temp_simulations);

        BOOST_LOG_TRIVIAL(info) << "get_recent_runs completed: " << result.optimizations.size()
                                << " optimizations, " << result.simulations.size() << " simulations";
    }

    return result;
}

// Helper function to parse timestamp from run name
std::chrono::system_clock::time_point HelioQuery::parse_timestamp_from_name(const std::string& name)
{
    try {
        // Parse timestamp from name format: "BambuSlicer 2026-01-23T07:52:27"
        // Extract the ISO timestamp using simple string parsing
        size_t pos = name.find_last_of(' ');
        if (pos != std::string::npos && pos + 1 < name.length()) {
            std::string timestamp_str = name.substr(pos + 1);

            // Parse ISO 8601 format: YYYY-MM-DDTHH:MM:SS
            if (timestamp_str.length() >= 19) {
                std::tm tm = {};
                tm.tm_year = std::stoi(timestamp_str.substr(0, 4)) - 1900;
                tm.tm_mon = std::stoi(timestamp_str.substr(5, 2)) - 1;
                tm.tm_mday = std::stoi(timestamp_str.substr(8, 2));
                tm.tm_hour = std::stoi(timestamp_str.substr(11, 2));
                tm.tm_min = std::stoi(timestamp_str.substr(14, 2));
                tm.tm_sec = std::stoi(timestamp_str.substr(17, 2));
                tm.tm_isdst = 0; // UTC has no DST

                // Parse as UTC time (timestamps from API are in UTC)
                std::time_t time;
#ifdef _WIN32
                time = _mkgmtime(&tm);
#else
                time = timegm(&tm);
#endif
                if (time != -1) {
                    return std::chrono::system_clock::from_time_t(time);
                }
            }
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Failed to parse timestamp from name: " << name << ", error: " << e.what();
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "Failed to parse timestamp from name: " << name;
    }

    // If parsing fails, return current time
    return std::chrono::system_clock::now();
}

} // namespace Slic3r
