#include "HelioDragon.hpp"

#include <string>
#include <wx/string.h>
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
#include "wx/app.h"
#include "cstdio"


namespace Slic3r {

std::vector<HelioQuery::SupportedData> HelioQuery::global_supported_printers;
std::vector<HelioQuery::SupportedData> HelioQuery::global_supported_materials;

std::string HelioQuery::last_simulation_trace_id;    
std::string HelioQuery::last_optimization_trace_id;  

std::string extract_trace_id(const std::string& headers) {
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

void HelioQuery::request_remaining_optimizations(const std::string & helio_api_url, const std::string & helio_api_key, std::function<void(int, int)> func) {
    std::string query_body = R"( {
        "query": "query GetUserRemainingOpts { user { remainingOptsThisMonth addOnOptimizations} }",
        "variables": {}
    } )";

    std::string url_copy = helio_api_url;
    std::string key_copy = helio_api_key;

    auto http = Http::post(url_copy);

    http.header("Content-Type", "application/json")
        .header("Authorization", "Bearer " + helio_api_key)
        .header("X-Version-Type", "Official")
        .set_post_body(query_body);

    http.timeout_connect(20)
        .timeout_max(100)
        .on_complete([url_copy, key_copy, func](std::string body, unsigned status) {
        try {
            nlohmann::json parsed_obj = nlohmann::json::parse(body);

            if (parsed_obj.contains("data") && parsed_obj["data"].contains("user")
                && parsed_obj["data"]["user"].contains("remainingOptsThisMonth")
                && parsed_obj["data"]["user"]["remainingOptsThisMonth"].is_number()) {

                int global_remaining_opt_count = parsed_obj["data"]["user"]["remainingOptsThisMonth"].get<int>();
                int global_remaining_addon_opt_count = 0;

                if (parsed_obj["data"]["user"]["addOnOptimizations"].is_number()){
                    global_remaining_addon_opt_count = parsed_obj["data"]["user"]["addOnOptimizations"].get<int>();
                }

                
                func(global_remaining_opt_count, global_remaining_addon_opt_count);
            }
            else {
                func(0, 0);
            }
        }
        catch (...) {
            func(0, 0);
        }
            })
        .on_error([func](std::string body, std::string error, unsigned status) {
            func(0, 0);
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
        .header("X-Version-Type", "Official")
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
            // BOOST_LOG_TRIVIAL(info) << (boost::format("error: %1%, message: %2%") % error % body).str()
        })
        .perform();
}

void HelioQuery::request_support_material(const std::string helio_api_url, const std::string helio_api_key, int page)
{
    std::string query_body = R"( {
			"query": "query GetMaterias($page: Int) { materials(page: $page, pageSize: 20) { pages pageInfo { hasNextPage } objects { ... on Material  { id name alternativeNames { bambustudio } } } } }",
            "variables": {"page": %1%}
		} )";

    query_body = boost::str(boost::format(query_body) % page);

    std::string url_copy  = helio_api_url;
    std::string key_copy  = helio_api_key;
    int         page_copy = page;

    auto http = Http::post(url_copy);

    http.header("Content-Type", "application/json")
        .header("Authorization", "Bearer " + helio_api_key)
        .header("X-Version-Type", "Official")
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
                            supported_materials.push_back(sp);
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
            // BOOST_LOG_TRIVIAL(info) << (boost::format("error: %1%, message: %2%") % error % body).str()
        })
        .perform();
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
        .header("X-Version-Type", "Official")
        .set_post_body(query_body);

    if (GUI::wxGetApp().app_config->get("region") == "China") {
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
        .header("X-Version-Type", "Official");

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
        "query": "query GcodeV2($id: ID!) { gcodeV2(id: $id) { id name sizeKb status progress } }",
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
                    result.status_str = gcode_data["status"];
                    result.progress = gcode_data["progress"];
                    result.sizeKb = gcode_data["sizeKb"];
                    result.success = true;
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
        .header("X-Version-Type", "Official")
        .set_post_body(query_body);

    if (GUI::wxGetApp().app_config->get("region") == "China") {
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

            int poll_count = 0;
            while (res.status_str != "READY" && poll_count < 60) {
                std::this_thread::sleep_for(std::chrono::seconds(2));

                PollResult poll_res = poll_gcode_status(helio_api_url, helio_api_key, res.id);

                if (poll_res.success) {
                    res.status_str = poll_res.status_str;
                    res.progress = poll_res.progress;
                    res.sizeKb = poll_res.sizeKb;
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
    bool outerwall,
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

    optimization_fields.push_back(boost::str(boost::format(R"("optimizeOuterwall": %1%)") % (outerwall ? "true" : "false")));

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
        .header("X-Version-Type", "Official")
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
        .header("X-Version-Type", "Official")
        .set_post_body(query_body);

    if (GUI::wxGetApp().app_config->get("region") == "China") {
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
        .header("X-Version-Type", "Official")
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
							"query": "query Simulation($id: ID!) { simulation(id: $id) { id name progress status thermalIndexGcodeUrl } }",
							"variables": {
								"id": "%1%"
							}
						} )";

    std::string query_body = (boost::format(query_body_template) % simulation_id).str();

    auto http = Http::post(helio_api_url);

    http.header("Content-Type", "application/json")
        .header("Authorization", helio_api_key)
        .header("X-Version-Type", "Official")
        .set_post_body(query_body);

    if (GUI::wxGetApp().app_config->get("region") == "China") {
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
                                                                             SimulationInput sinput,
                                                                             OptimizationInput oinput)
{

    std::string query_body;

    /*outer wall*/
    const bool outer_wall = oinput.outer_wall;

    /*SimulationInput*/
    const float chamber_temp = sinput.chamber_temp;
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
            outer_wall,
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
            outer_wall,
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
        .header("X-Version-Type", "Official")
        .set_post_body(query_body);

    if (GUI::wxGetApp().app_config->get("region") == "China") {
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
        .header("X-Version-Type", "Official")
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
							"query": "query Optimization($id: ID!) { optimization(id: $id) { id name progress status optimizedGcodeWithThermalIndexesUrl } }",
							"variables": {
								"id": "%1%"
							}
						} )";

    std::string query_body = (boost::format(query_body_template) % optimization_id).str();

    auto http = Http::post(helio_api_url);

    http.header("Content-Type", "application/json")
        .header("Authorization", helio_api_key)
        .header("X-Version-Type", "Official")
        .set_post_body(query_body);

    if (GUI::wxGetApp().app_config->get("region") == "China") {
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
                            // notification_manager->push_notification((boost::format("Helio: Simulation finished.")).str());
                            std::string simulated_gcode_path = HelioBackgroundProcess::create_path_for_simulated_gcode(
                                m_gcode_result->filename);

                            HelioBackgroundProcess::save_downloaded_gcode_and_load_preview(check_simulation_progress_res.url,
                                                                                           simulated_gcode_path, m_gcode_result->filename,
                                                                                           notification_manager);
                            break;
                        }
                    } else {
                        set_state(STATE_CANCELED);

                        Slic3r::HelioCompletionEvent* evt = new Slic3r::HelioCompletionEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, "", "",
                                                                                             false, _u8L("Helio: simulation failed"));
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
                error = _u8L("Helio: Failed to create Simulation\n") + create_simulation_res.error;
            }
            catch (...){}
            
            Slic3r::HelioCompletionEvent* evt = new Slic3r::HelioCompletionEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, "", "", false, error);
            wxQueueEvent(GUI::wxGetApp().plater(), evt);
        }

    } else {
        set_state(STATE_CANCELED);

        std::string error;
        try {
            error = _u8L("Helio: Failed to create GCode\n") + create_gcode_res.error;
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
        HelioQuery::CreateOptimizationResult create_optimization_res = HelioQuery::create_optimization(helio_api_url, helio_api_key, gcode_id, simulation_input_data, optimization_input_data);
        current_optimization_result = create_optimization_res;
        current_simulation_result.reset();

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

                            HelioBackgroundProcess::save_downloaded_gcode_and_load_preview(check_optimzaion_progress_res.url,
                                optimized_gcode_path, m_gcode_result->filename,
                                notification_manager);
                            break;
                        }
                    }
                    else {
                        set_state(STATE_CANCELED);

                        Slic3r::HelioCompletionEvent* evt = new Slic3r::HelioCompletionEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, "", "",
                            false, "Helio: optimization failed");
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
                error = _u8L("Helio: Failed to create Optimization\n") + create_optimization_res.error;
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
            error = _u8L("Helio: Failed to create GCode\n") + create_gcode_res.error;
        }
        catch (...) {}

        Slic3r::HelioCompletionEvent* evt = new Slic3r::HelioCompletionEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, "", "", false, error);
        wxQueueEvent(GUI::wxGetApp().plater(), evt);
    }
}
void HelioBackgroundProcess::save_downloaded_gcode_and_load_preview(std::string                                file_download_url,
                                                                    std::string                                helio_gcode_path,
                                                                    std::string                                tmp_path,
                                                                    std::unique_ptr<GUI::NotificationManager>& notification_manager)
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
        FILE* file = fopen(helio_gcode_path.c_str(), "wb");
        fwrite(downloaded_gcode.c_str(), 1, downloaded_gcode.size(), file);
        fclose(file);

        Slic3r::PrintBase::SlicingStatus status = Slic3r::PrintBase::SlicingStatus(100, _u8L("Helio: GCode downloaded successfully"));
        status.is_helio = true;
        Slic3r::SlicingStatusEvent*      evt    = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
        wxQueueEvent(GUI::wxGetApp().plater(), evt);
        HelioBackgroundProcess::load_helio_file_to_viwer(helio_gcode_path, tmp_path);
    } else {
        set_state(STATE_CANCELED);

        std::string error;
        try {
            error = _u8L("Helio: GCode download failed\n") + response_error;
        }
        catch (...) {}

        Slic3r::HelioCompletionEvent* evt =
            new Slic3r::HelioCompletionEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, "", "", false, error);
        wxQueueEvent(GUI::wxGetApp().plater(), evt);
    }
}

void HelioBackgroundProcess::load_helio_file_to_viwer(std::string file_path, std::string tmp_path)
{
    const Vec3d origin = GUI::wxGetApp().plater()->get_partplate_list().get_current_plate_origin();
    m_gcode_processor.set_xy_offset(origin(0), origin(1));
    m_gcode_processor.process_file(file_path);
    auto res       = &m_gcode_processor.result();
    m_gcode_result = res;

    set_state(STATE_FINISHED);
    Slic3r::HelioCompletionEvent* evt = new Slic3r::HelioCompletionEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, file_path, tmp_path, true);
    wxQueueEvent(GUI::wxGetApp().plater(), evt);
}

void HelioBackgroundProcess::set_helio_api_key(std::string api_key) { helio_api_key = api_key; }
void HelioBackgroundProcess::set_gcode_result(Slic3r::GCodeProcessorResult* gcode_result) { m_gcode_result = gcode_result; }

} // namespace Slic3r
