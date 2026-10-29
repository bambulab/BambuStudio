#include "HelioDragon.hpp"

#include <string>
#include <wx/string.h>
#include <wx/file.h>
#include <boost/optional.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <vector>
#include <boost/format.hpp>
#include "PrintHost.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/PrintBase.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/LocalesUtils.hpp"
#include <boost/log/trivial.hpp>
#include "../GUI/PartPlate.hpp"
#include "../GUI/GUI_App.hpp"
#include "../GUI/Event.hpp"
#include "../GUI/Plater.hpp"
#include "../GUI/NotificationManager.hpp"
#include "../GUI/HelioReleaseNote.hpp"
#include "wx/app.h"
#include "cstdio"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <sstream>
#include <thread>

namespace Slic3r {

std::map<std::string, std::vector<HelioQuery::PrintPriorityOption>> HelioQuery::global_print_priority_cache;

std::string HelioQuery::last_simulation_trace_id;
std::string HelioQuery::last_optimization_trace_id;  

namespace {

thread_local std::uint64_t helio_worker_generation = 0;

constexpr int HELIO_CREATE_MAX_ATTEMPTS = 4;
constexpr int HELIO_UPLOAD_MAX_ATTEMPTS = 4;
constexpr int HELIO_MAX_POLL_BACKOFF_SECONDS = 30;
constexpr int HELIO_MAX_TRANSIENT_POLL_FAILURES = 60;

struct HelioPrintPriorityCacheState
{
    std::mutex   mutex;
    std::uint64_t generation{0};
};

HelioPrintPriorityCacheState& helio_print_priority_cache_state()
{
    // Requests are joined during GUI shutdown. Leak the synchronization state
    // as an additional guard against static destruction order regressions.
    static HelioPrintPriorityCacheState* state = new HelioPrintPriorityCacheState;
    return *state;
}

class HelioRequestWorkers
{
public:
    using Task = std::function<void(const std::atomic_bool&)>;

    bool start(Task task)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopping.load(std::memory_order_acquire)) {
            return false;
        }

        reap_finished_locked();
        auto finished = std::make_shared<std::atomic_bool>(false);
        try {
            m_workers.reserve(m_workers.size() + 1);
            m_workers.emplace_back();
            Worker& worker = m_workers.back();
            worker.finished = finished;
            worker.thread = std::thread([this, task = std::move(task), finished]() mutable {
                try {
                    task(m_stopping);
                } catch (...) {
                    // SupportDataCatalogStore::run normally contains failures.
                    // Keep the owner safe if one escapes unexpectedly.
                }
                finished->store(true, std::memory_order_release);
            });
        } catch (...) {
            if (!m_workers.empty() && m_workers.back().finished == finished &&
                !m_workers.back().thread.joinable()) {
                m_workers.pop_back();
            }
            return false;
        }
        return true;
    }

    void shutdown()
    {
        std::vector<Worker> workers;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stopping.store(true, std::memory_order_release);
            workers.swap(m_workers);
        }
        for (Worker& worker : workers) {
            if (worker.thread.joinable()) {
                worker.thread.join();
            }
        }
    }

private:
    struct Worker
    {
        std::thread                       thread;
        std::shared_ptr<std::atomic_bool> finished;
    };

    void reap_finished_locked()
    {
        auto worker = m_workers.begin();
        while (worker != m_workers.end()) {
            if (!worker->finished->load(std::memory_order_acquire)) {
                ++worker;
                continue;
            }
            if (worker->thread.joinable()) {
                worker->thread.join();
            }
            worker = m_workers.erase(worker);
        }
    }

    std::mutex          m_mutex;
    std::atomic_bool    m_stopping{false};
    std::vector<Worker> m_workers;
};

HelioRequestWorkers& helio_request_workers()
{
    // GUI_App::OnExit joins these workers before Http's global curl cleanup.
    // Keep the owner process-lived so static destruction cannot race a load.
    static HelioRequestWorkers* workers = new HelioRequestWorkers;
    return *workers;
}

bool helio_starts_with(const std::string& value, const std::string& prefix)
{
    return value.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), value.begin());
}

std::string helio_generate_idempotency_key()
{
    return boost::uuids::to_string(boost::uuids::random_generator()());
}

std::string helio_generate_retry_job_name(const std::string& idempotency_key)
{
    const std::string unique_suffix = idempotency_key.empty() ? helio_generate_idempotency_key().substr(0, 8) :
                                                                idempotency_key.substr(0, 8);
    const std::string timestamped_name = HelioQuery::generateTimestampedString();
    const size_t timestamp_pos = timestamped_name.find_last_of(' ');
    if (timestamp_pos == std::string::npos) {
        return timestamped_name + " " + unique_suffix;
    }
    return timestamped_name.substr(0, timestamp_pos) + " " + unique_suffix + timestamped_name.substr(timestamp_pos);
}

void helio_attach_idempotency_key(Http& http, const std::string& idempotency_key)
{
    if (!idempotency_key.empty()) {
        http.header("Idempotency-Key", idempotency_key);
    }
}

std::string helio_http_status_message(unsigned status, const std::string& body = std::string())
{
    std::string message = (boost::format("HTTP status: %1%") % status).str();
    if (!body.empty()) {
        message += ", body: " + body;
    }
    return message;
}

std::string helio_bearer_token_from_authorization(const std::string& authorization)
{
    const std::string bearer_prefix = "Bearer ";
    if (helio_starts_with(authorization, bearer_prefix)) {
        return authorization.substr(bearer_prefix.size());
    }
    return authorization;
}

int helio_next_poll_backoff_seconds(int transient_failures)
{
    return std::min(HELIO_MAX_POLL_BACKOFF_SECONDS, 3 + transient_failures * 3);
}

bool helio_should_cancel(const std::function<bool()>& should_cancel)
{
    return should_cancel && should_cancel();
}

bool helio_sleep_for_retry_or_cancel(int seconds, const std::function<bool()>& should_cancel)
{
    for (int elapsed = 0; elapsed < seconds; ++elapsed) {
        if (helio_should_cancel(should_cancel)) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return !helio_should_cancel(should_cancel);
}

bool helio_adopt_recent_simulation_by_name(const std::string& helio_api_url,
                                           const std::string& helio_api_key,
                                           const std::string& job_name,
                                           HelioQuery::CreateSimulationResult& result)
{
    if (job_name.empty()) {
        return false;
    }

    HelioQuery::GetRecentRunsResult recent_runs =
        HelioQuery::get_recent_runs(helio_api_url, helio_bearer_token_from_authorization(helio_api_key));
    if (!recent_runs.success) {
        return false;
    }

    for (const HelioQuery::SimulationRun& run : recent_runs.simulations) {
        if (run.name == job_name && !run.id.empty()) {
            result.status = recent_runs.status == 0 ? 200 : recent_runs.status;
            result.success = true;
            result.id = run.id;
            result.name = run.name;
            result.error.clear();
            BOOST_LOG_TRIVIAL(info) << "Helio create_simulation adopted existing run after retry: " << run.id;
            return true;
        }
    }

    return false;
}

bool helio_adopt_recent_optimization_by_name(const std::string& helio_api_url,
                                             const std::string& helio_api_key,
                                             const std::string& job_name,
                                             HelioQuery::CreateOptimizationResult& result)
{
    if (job_name.empty()) {
        return false;
    }

    HelioQuery::GetRecentRunsResult recent_runs =
        HelioQuery::get_recent_runs(helio_api_url, helio_bearer_token_from_authorization(helio_api_key));
    if (!recent_runs.success) {
        return false;
    }

    for (const HelioQuery::OptimizationRun& run : recent_runs.optimizations) {
        if (run.name == job_name && !run.id.empty()) {
            result.status = recent_runs.status == 0 ? 200 : recent_runs.status;
            result.success = true;
            result.id = run.id;
            result.name = run.name;
            result.error.clear();
            BOOST_LOG_TRIVIAL(info) << "Helio create_optimization adopted existing run after retry: " << run.id;
            return true;
        }
    }

    return false;
}

} // namespace

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

std::string helio_join_messages(const std::vector<std::string>& messages)
{
    std::string joined;
    for (size_t i = 0; i < messages.size(); ++i) {
        if (messages.size() > 1) {
            joined += std::to_string(i + 1) + ". ";
        }
        joined += messages[i];
        if (i + 1 < messages.size()) {
            joined += "\n";
        }
    }
    return joined;
}

std::string format_error(const std::string& body)
{
    const nlohmann::json parsed_obj = nlohmann::json::parse(body);
    if (!helio_has_graphql_errors(parsed_obj)) {
        return {};
    }

    std::vector<std::string> messages;
    const nlohmann::json& errors = parsed_obj["errors"];
    if (errors.is_array()) {
        for (const nlohmann::json& error : errors) {
            if (error.is_object() && error.contains("message") && error["message"].is_string()) {
                messages.push_back(error["message"].get<std::string>());
            }
        }
    } else if (errors.is_object() && errors.contains("message") && errors["message"].is_string()) {
        messages.push_back(errors["message"].get<std::string>());
    }

    return helio_join_messages(messages);
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
        catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "Helio request_remaining_optimizations: " << e.what();
            func(0, 0, "", false, false, false);
        } catch (...) {
            BOOST_LOG_TRIVIAL(error) << "Helio request_remaining_optimizations: unknown error";
            func(0, 0, "", false, false, false);
        }
            })
        .on_error([func](std::string body, std::string error, unsigned status) {
            func(0, 0, "", false, false, false);
            BOOST_LOG_TRIVIAL(error) << "Failed to obtain remaining optimization attempts: " << error << ", status: " << status;
        })
        .perform();
}

namespace {

SupportDataHttpResponse helio_fetch_support_data_page(SupportDataCatalogKind kind,
                                                       const std::string& helio_api_url,
                                                       const std::string& helio_api_key,
                                                       int page,
                                                       const std::atomic_bool& stopping)
{
    const char* query = kind == SupportDataCatalogKind::Printers
        ? "query GetPrinters($page: Int) { printers(page: $page, pageSize: 20) { pages pageInfo { hasNextPage } objects { ... on Printer { id name heatedChamber alternativeNames { bambustudio } } } } }"
        : "query GetMaterials($page: Int) { materials(page: $page, pageSize: 20) { pages pageInfo { hasNextPage } objects { ... on Material { id name feedstock alternativeNames { bambustudio } } } } }";

    json request_body;
    request_body["query"] = query;
    request_body["variables"]["page"] = page;

    SupportDataHttpResponse response;
    std::string response_headers;
    auto http = Http::post(helio_api_url);
    http.header("Content-Type", "application/json")
        .header("Authorization", "Bearer " + helio_api_key)
        .header("HelioAdditive-Client-Name", SLIC3R_APP_NAME)
        .header("HelioAdditive-Client-Version", GUI::VersionInfo::convert_full_version(SLIC3R_VERSION))
        .set_post_body(request_body.dump())
        .timeout_connect(20)
        .timeout_max(100)
        .on_header_callback([&response_headers](std::string headers) {
            response_headers = std::move(headers);
        })
        .on_complete([&response](std::string body, unsigned status) {
            response.status = status;
            response.body = std::move(body);
        })
        .on_error([&response](std::string body, std::string error, unsigned status) {
            response.status = status;
            response.body = std::move(body);
            response.error = std::move(error);
        })
        .on_progress([&stopping](Http::Progress, bool& cancel) {
            cancel = stopping.load(std::memory_order_acquire);
        })
        .perform_sync();

    response.trace_id = extract_trace_id(response_headers);
    return response;
}

SupportDataCatalogPairCoordinator& helio_support_data_coordinator()
{
    // Workers are joined by GUI_App::OnExit before Http's global curl cleanup.
    static SupportDataCatalogPairCoordinator* coordinator = new SupportDataCatalogPairCoordinator;
    return *coordinator;
}

bool helio_request_support_data(const std::string& helio_api_url,
                                const std::string& helio_api_key,
                                bool force_refresh)
{
    auto& coordinator = helio_support_data_coordinator();
    std::uint64_t generation = 0;
    if (!coordinator.try_begin(helio_api_url, helio_api_key, force_refresh, &generation))
        return false;
    const bool worker_started = helio_request_workers().start(
        [&coordinator, generation, helio_api_url, helio_api_key](const std::atomic_bool& stopping) {
            coordinator.run(
                generation,
                [&helio_api_url, &helio_api_key, &stopping](SupportDataCatalogKind kind, int page) {
                    if (stopping.load(std::memory_order_acquire)) {
                        SupportDataHttpResponse response;
                        response.error = "Helio support-data request stopped during shutdown";
                        return response;
                    }
                    return helio_fetch_support_data_page(kind, helio_api_url, helio_api_key, page, stopping);
                },
                [&stopping](int seconds) {
                    for (int elapsed = 0; elapsed < seconds * 10 && !stopping.load(std::memory_order_acquire); ++elapsed)
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                },
                [](const SupportDataLoadAttempt& attempt) {
                    BOOST_LOG_TRIVIAL(warning)
                        << "Helio support-data load: catalog="
                        << (attempt.kind == SupportDataCatalogKind::Printers ? "printers" : "materials")
                        << ", page=" << attempt.page
                        << ", retry-kind=" << helio_retry_kind_name(attempt.retry_kind)
                        << ", attempt=" << attempt.attempt << "/" << attempt.max_attempts
                        << ", status=" << attempt.status
                        << ", trace-id=" << attempt.trace_id
                        << ", error=" << attempt.error;
                });
        });
    if (worker_started)
        return true;

    coordinator.run(generation, [](SupportDataCatalogKind, int) {
        SupportDataHttpResponse response;
        response.error = "Failed to start Helio support-data worker";
        return response;
    });
    return false;
}

} // namespace

bool HelioQuery::request_supported_data(const std::string& helio_api_url,
                                        const std::string& helio_api_key,
                                        bool force_refresh)
{
    return helio_request_support_data(helio_api_url, helio_api_key, force_refresh);
}

SupportDataCatalogPairView HelioQuery::supported_data_view()
{
    return helio_support_data_coordinator().view();
}

void HelioQuery::shutdown_background_requests()
{
    helio_request_workers().shutdown();
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
    const bool is_china = GUI::wxGetApp().app_config->get("region") == "China";

    std::uint64_t cache_generation = 0;
    {
        HelioPrintPriorityCacheState& cache = helio_print_priority_cache_state();
        std::lock_guard<std::mutex> lock(cache.mutex);
        cache_generation = cache.generation;
    }

    const bool worker_started = helio_request_workers().start(
        [helio_api_url, helio_api_key, material_id, callback,
         query_body = std::move(query_body), is_china, cache_generation]
        (const std::atomic_bool& stopping) {
            if (stopping.load(std::memory_order_acquire)) {
                return;
            }

            auto http = Http::post(helio_api_url);
            http.header("Content-Type", "application/json")
                .header("Authorization", "Bearer " + helio_api_key)
                .header("HelioAdditive-Client-Name", SLIC3R_APP_NAME)
                .header("HelioAdditive-Client-Version", GUI::VersionInfo::convert_full_version(SLIC3R_VERSION));
            if (is_china) {
                http.header("Accept-Language", "zh-CN");
            }
            http.set_post_body(query_body);

            auto response_headers = std::make_shared<std::string>();
            http.timeout_connect(10)
                .timeout_max(30)
                .on_header_callback([response_headers](std::string headers) {
                    *response_headers += headers;
                })
                .on_complete([callback, material_id, response_headers, cache_generation, &stopping]
                             (std::string body, unsigned status) {
                    if (stopping.load(std::memory_order_acquire)) {
                        return;
                    }
                    BOOST_LOG_TRIVIAL(info) << "request_print_priority_options response: " << body;

                    GetPrintPriorityOptionsResult result;
                    result.status = status;
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
                                    }
                                    if (opt.contains("description") && !opt["description"].is_null()) {
                                        option.description = opt["description"].get<std::string>();
                                    }
                                    result.options.push_back(std::move(option));
                                }
                                result.success = true;

                                HelioPrintPriorityCacheState& cache = helio_print_priority_cache_state();
                                std::lock_guard<std::mutex> lock(cache.mutex);
                                if (cache_generation == cache.generation) {
                                    global_print_priority_cache[material_id] = result.options;
                                } else {
                                    result.success = false;
                                    result.options.clear();
                                    result.error = "Helio support data was refreshed while print priorities were loading";
                                }
                            }
                        } else if (helio_has_graphql_errors(parsed_obj)) {
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

                    if (!stopping.load(std::memory_order_acquire)) {
                        callback(result);
                    }
                })
                .on_error([callback, response_headers, &stopping]
                          (std::string body, std::string error, unsigned status) {
                    if (stopping.load(std::memory_order_acquire)) {
                        return;
                    }
                    BOOST_LOG_TRIVIAL(error) << "request_print_priority_options error: " << error
                                             << ", status: " << status << ", body: " << body;
                    GetPrintPriorityOptionsResult result;
                    result.error = std::move(error);
                    result.status = status;
                    result.trace_id = extract_trace_id(*response_headers);
                    callback(result);
                })
                .on_progress([&stopping](Http::Progress, bool& cancel) {
                    cancel = stopping.load(std::memory_order_acquire);
                })
                .perform_sync();
        });

    if (!worker_started && callback) {
        GetPrintPriorityOptionsResult result;
        result.error = "Failed to start Helio print-priority request";
        callback(result);
    }
}

std::vector<HelioQuery::PrintPriorityOption> HelioQuery::get_cached_print_priority_options(
    const std::string& material_id
)
{
    HelioPrintPriorityCacheState& cache = helio_print_priority_cache_state();
    std::lock_guard<std::mutex> lock(cache.mutex);
    auto it = global_print_priority_cache.find(material_id);
    if (it != global_print_priority_cache.end()) {
        return it->second;
    }
    return std::vector<PrintPriorityOption>();
}

void HelioQuery::clear_print_priority_cache()
{
    HelioPrintPriorityCacheState& cache = helio_print_priority_cache_state();
    std::lock_guard<std::mutex> lock(cache.mutex);
    ++cache.generation;
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
                catch (const std::exception& e) {
                    BOOST_LOG_TRIVIAL(error) << "Helio request_pat_token: " << e.what();
                } catch (...) {
                    BOOST_LOG_TRIVIAL(error) << "Helio request_pat_token: unknown error";
                }
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
            BOOST_LOG_TRIVIAL(error) << "Helio request_pat_token error: " << error << ", status: " << status;
        })
        .perform();
}

void HelioQuery::optimization_feedback(const std::string helio_api_url, const std::string helio_api_key, std::string optimization_id, float rating, std::string comment)
{
    CNumericLocalesSetter locales_setter;
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
    res.status = 0;
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
            auto trace_id = extract_trace_id(header_line);
            if(!trace_id.empty()){
                res.trace_id = trace_id;
            }
        })
        .on_complete([&res](std::string body, unsigned status) {
            res.status = status;
            if (helio_is_terminal_http_status(status)) {
                res.error = helio_http_status_message(status, body);
                return;
            }
            if (status != 200) {
                res.error = helio_http_status_message(status, body);
                res.retry_kind = helio_classify_retry(status, body);
                return;
            }
            try {
                nlohmann::json parsed_obj = nlohmann::json::parse(body);
                if (helio_has_graphql_errors(parsed_obj)) {
                    res.error = format_error(body);
                    res.retry_kind = helio_classify_graphql_response(status, parsed_obj);
                    if (res.error.empty()) {
                        res.error = parsed_obj["errors"].dump();
                    }
                    return;
                }
                if (parsed_obj.contains("error")) {
                    res.error = parsed_obj["error"].is_string() ? parsed_obj["error"].get<std::string>() : parsed_obj["error"].dump();
                    return;
                }
                if (!parsed_obj.contains("data") || !parsed_obj["data"].contains("getPresignedUrl") ||
                    parsed_obj["data"]["getPresignedUrl"].is_null()) {
                    res.error = "Presigned URL response is incomplete";
                    res.retry_kind = HelioRetryKind::Transient;
                    return;
                }
                res.key      = parsed_obj["data"]["getPresignedUrl"]["key"];
                res.mimeType = parsed_obj["data"]["getPresignedUrl"]["mimeType"];
                res.url      = parsed_obj["data"]["getPresignedUrl"]["url"];
            } catch (const std::exception& e) {
                res.error = std::string("Failed to parse presigned URL response: ") + e.what();
                res.retry_kind = HelioRetryKind::Transient;
            } catch (...) {
                res.error = "Failed to parse presigned URL response";
                res.retry_kind = HelioRetryKind::Transient;
            }
        })
        .on_error([&res](std::string body, std::string error, unsigned status) {
            res.error  = (boost::format("error: %1%, message: %2%") % error % body).str();
            res.status = status;
            res.retry_kind = helio_classify_retry(status, body, error);
        })
        .perform_sync();

    return res;
};

HelioQuery::UploadFileResult HelioQuery::upload_file_to_presigned_url(const std::string file_path_string, const std::string upload_url)
{
    UploadFileResult res;
    res.success = false;

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
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Helio upload_file check original: " << e.what();
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "Helio upload_file check original: unknown error";
    }

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
        .on_header_callback([&result](std::string header_line) {
            const std::string trace_id = extract_trace_id(header_line);
            if (!trace_id.empty()) {
                result.trace_id = trace_id;
            }
        })
        .on_complete([&result](std::string poll_body, unsigned poll_status) {
            result.status = poll_status;
            if (helio_is_terminal_http_status(poll_status)) {
                result.error = helio_http_status_message(poll_status, poll_body);
                return;
            }
            if (poll_status != 200) {
                result.error = helio_http_status_message(poll_status, poll_body);
                result.retry_kind = helio_classify_retry(poll_status, poll_body);
                return;
            }

            try {
                json poll_obj = json::parse(poll_body);
                if (helio_has_graphql_errors(poll_obj)) {
                    result.error = format_error(poll_body);
                    result.retry_kind = helio_classify_graphql_response(poll_status, poll_obj);
                    return;
                }
                if (poll_obj.contains("data") && poll_obj["data"].contains("gcodeV2") &&
                    !poll_obj["data"]["gcodeV2"].is_null()) {
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

                if (!result.success && result.error.empty()) {
                    result.error = _u8L("Helio GCode status response is incomplete");
                    result.retry_kind = HelioRetryKind::Transient;
                }
            }
            catch (const std::exception& e) {
                result.error = std::string("Helio GCode status response parse failed: ") + e.what();
                result.retry_kind = HelioRetryKind::Transient;
                BOOST_LOG_TRIVIAL(error) << "Helio poll_gcode_status: " << e.what();
            } catch (...) {
                result.error = _u8L("Helio GCode status response parse failed");
                result.retry_kind = HelioRetryKind::Transient;
                BOOST_LOG_TRIVIAL(error) << "Helio poll_gcode_status: unknown error";
            }
        })
        .on_error([&result](std::string poll_body, std::string poll_error, unsigned poll_status) {
            result.status = poll_status;
            result.error = poll_error.empty() ? poll_body : poll_error;
            result.retry_kind = helio_classify_retry(poll_status, poll_body, poll_error);
            BOOST_LOG_TRIVIAL(error) << "Helio poll_gcode_status error: " << poll_error << ", status: " << poll_status;
        })
        .perform_sync();

    return result;
}

namespace {

HelioQuery::CreateGCodeResult helio_create_gcode_and_poll(const std::string& helio_api_url,
                                                           const std::string& helio_api_key,
                                                           const std::string& query_body,
                                                           const std::string& response_field,
                                                           const std::string& idempotency_key,
                                                           const std::function<bool()>& should_cancel)
{
    HelioQuery::CreateGCodeResult res;
    HelioRetryController create_retry_controller;

    for (int attempt = 1; attempt <= HELIO_CREATE_MAX_ATTEMPTS; ++attempt) {
        if (helio_should_cancel(should_cancel)) {
            res.error = _u8L("Helio action canceled");
            return res;
        }

        HelioQuery::CreateGCodeResult attempt_res;
        HelioRetryKind retry_kind = HelioRetryKind::None;

        auto http = Http::post(helio_api_url);
        http.header("Content-Type", "application/json")
            .header("Authorization", helio_api_key)
            .header("HelioAdditive-Client-Name", SLIC3R_APP_NAME)
            .header("HelioAdditive-Client-Version", GUI::VersionInfo::convert_full_version(SLIC3R_VERSION))
            .set_post_body(query_body);
        helio_attach_idempotency_key(http, idempotency_key);

        if (GUI::wxGetApp().app_config->get("language") == "zh_CN") {
            http.header("Accept-Language", "zh-CN");
        }

        http.timeout_connect(20)
            .timeout_max(100)
            .on_header_callback([&attempt_res](std::string header_line) {
                const std::string trace_id = extract_trace_id(header_line);
                if (!trace_id.empty()) {
                    attempt_res.trace_id = trace_id;
                }
            })
            .on_complete([&attempt_res, &retry_kind, &response_field](std::string body, unsigned status) {
                attempt_res.status = status;
                if (helio_is_terminal_http_status(status)) {
                    attempt_res.error = helio_http_status_message(status, body);
                    return;
                }
                if (status != 200) {
                    attempt_res.error = helio_http_status_message(status, body);
                    retry_kind = helio_classify_retry(status, body);
                    return;
                }

                try {
                    const json parsed_obj = json::parse(body);
                    if (helio_has_graphql_errors(parsed_obj)) {
                        attempt_res.error = format_error(body);
                        retry_kind = helio_classify_graphql_response(status, parsed_obj);
                        return;
                    }
                    if (!parsed_obj.contains("data") || !parsed_obj["data"].contains(response_field) ||
                        parsed_obj["data"][response_field].is_null()) {
                        attempt_res.error = _u8L("Helio GCode creation response is incomplete");
                        retry_kind = HelioRetryKind::Transient;
                        return;
                    }

                    const auto& gcode = parsed_obj["data"][response_field];
                    attempt_res.id = gcode.value("id", "");
                    attempt_res.name = gcode.value("name", "");
                    attempt_res.sizeKb = gcode.value("sizeKb", 0.0f);
                    attempt_res.status_str = gcode.value("status", "");
                    attempt_res.progress = gcode.value("progress", 0.0f);
                    if (attempt_res.id.empty() || attempt_res.status_str.empty()) {
                        attempt_res.error = _u8L("Helio GCode creation response is incomplete");
                        retry_kind = HelioRetryKind::Transient;
                        return;
                    }
                    attempt_res.success = true;
                } catch (const std::exception& e) {
                    attempt_res.error = std::string("Failed to parse response: ") + e.what();
                    retry_kind = HelioRetryKind::Transient;
                } catch (...) {
                    attempt_res.error = _u8L("Failed to parse response");
                    retry_kind = HelioRetryKind::Transient;
                }
            })
            .on_error([&attempt_res, &retry_kind](std::string body, std::string error, unsigned status) {
                attempt_res.status = status;
                attempt_res.error = error.empty() ? body : error;
                retry_kind = helio_classify_retry(status, body, error);
            })
            .perform_sync();

        res = attempt_res;
        if (res.success) {
            break;
        }

        const bool should_retry = create_retry_controller.should_retry(retry_kind) &&
                                  attempt < HELIO_CREATE_MAX_ATTEMPTS;
        if (!should_retry) {
            break;
        }

        BOOST_LOG_TRIVIAL(warning) << "Helio GCode create retry: kind=" << helio_retry_kind_name(retry_kind)
                                   << ", attempt=" << attempt << ", status=" << res.status
                                   << ", trace-id=" << res.trace_id;
        if (!helio_sleep_for_retry_or_cancel(attempt * 2, should_cancel)) {
            res.error = _u8L("Helio action canceled");
            return res;
        }
    }

    if (!res.success) {
        return res;
    }
    if (helio_should_cancel(should_cancel)) {
        res.success = false;
        res.error = _u8L("Helio action canceled");
        return res;
    }
    if (res.status_str == "ERROR" || res.status_str == "RESTRICTED") {
        res.success = false;
        res.error = (boost::format("GCode creation failed with status: %1%") % res.status_str).str();
        return res;
    }

    HelioRetryController poll_retry_controller;
    int poll_count = 0;
    while (res.status_str != "READY" && res.status_str != "ERROR" &&
           res.status_str != "RESTRICTED" && poll_count < 60 &&
           !helio_should_cancel(should_cancel)) {
        if (!helio_sleep_for_retry_or_cancel(2, should_cancel)) {
            res.success = false;
            res.error = _u8L("Helio action canceled");
            return res;
        }
        HelioQuery::PollResult poll_res = HelioQuery::poll_gcode_status(helio_api_url, helio_api_key, res.id);
        ++poll_count;

        if (!poll_res.success) {
            const bool policy_allows_retry = poll_retry_controller.should_retry(poll_res.retry_kind);
            const bool has_retry_budget = poll_count < 60 ||
                                          poll_res.retry_kind == HelioRetryKind::BackendAuth;
            if (policy_allows_retry && has_retry_budget) {
                const int failed_poll_attempt = poll_count;
                // The narrow backend-auth retry is additional to the normal
                // polling budget so an occurrence on poll 60 still gets its
                // promised single retry.
                if (poll_res.retry_kind == HelioRetryKind::BackendAuth) {
                    --poll_count;
                }
                BOOST_LOG_TRIVIAL(warning) << "Helio GCode poll retry: kind="
                                           << helio_retry_kind_name(poll_res.retry_kind)
                                           << ", attempt=" << failed_poll_attempt << ", status=" << poll_res.status
                                           << ", trace-id=" << poll_res.trace_id;
                continue;
            }
            res.success = false;
            res.error = poll_res.error.empty() ? _u8L("Helio GCode status check failed") : poll_res.error;
            return res;
        }

        poll_retry_controller.reset();
        res.status_str = poll_res.status_str;
        res.progress = poll_res.progress;
        res.sizeKb = poll_res.sizeKb;

        if (!poll_res.errors.empty()) {
            res.success = false;
            res.error = helio_join_messages(poll_res.errors);
            return res;
        }
        if (res.status_str == "ERROR" || res.status_str == "RESTRICTED") {
            res.success = false;
            if (res.status_str == "RESTRICTED" && !poll_res.restrictions.empty()) {
                res.error = helio_join_messages(poll_res.restrictions);
            } else {
                res.error = (boost::format("GCode creation failed with status: %1%") % res.status_str).str();
            }
            return res;
        }
    }

    if (helio_should_cancel(should_cancel)) {
        res.success = false;
        res.error = _u8L("Helio action canceled");
        return res;
    }
    if (res.status_str != "READY") {
        res.success = false;
        res.error = _u8L("Helio GCode creation timed out while checking status");
    }
    return res;
}

} // namespace

// V2: single material — createGcodeV2 mutation
HelioQuery::CreateGCodeResult HelioQuery::create_gcode(const std::string key,
                                                       const std::string helio_api_url,
                                                       const std::string helio_api_key,
                                                       const std::string printer_id,
                                                       const std::string filament_id,
                                                       const std::string idempotency_key,
                                                       std::function<bool()> should_cancel)
{
    const std::string effective_idempotency_key = idempotency_key.empty() ? helio_generate_idempotency_key() : idempotency_key;
    std::string query_body_template = R"( {
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
    const std::string gcode_name = key_split.back();
    const std::string query_body = (boost::format(query_body_template) % gcode_name % printer_id % filament_id % key).str();
    return helio_create_gcode_and_poll(helio_api_url, helio_api_key, query_body,
                                       "createGcodeV2", effective_idempotency_key,
                                       should_cancel);
}

// V3: multi-material — createGcodeV3 mutation
HelioQuery::CreateGCodeResult HelioQuery::create_gcode_v3(const std::string key,
                                                          const std::string helio_api_url,
                                                          const std::string helio_api_key,
                                                          const std::string printer_id,
                                                          const std::vector<MaterialInput>& materials,
                                                          bool isMultiColor,
                                                          bool isMultiMaterial,
                                                          const std::string idempotency_key,
                                                          std::function<bool()> should_cancel)
{
    const std::string effective_idempotency_key = idempotency_key.empty() ? helio_generate_idempotency_key() : idempotency_key;

    std::vector<std::string> key_split;
    boost::split(key_split, key, boost::is_any_of("/"));

    std::string gcode_name = key_split.back();

    // Build materials JSON array
    json materials_array = json::array();
    for (const auto& mat : materials) {
        materials_array.push_back({
            {"materialId", mat.materialId},
            {"slotIndex", mat.slotIndex},
            {"nozzleIndex", mat.nozzleIndex}
        });
    }

    // Build complete V3 request
    json request_body;
    request_body["query"] = "mutation CreateGcode($input: CreateGcodeInputV3!) { "
                            "createGcodeV3(input: $input) { id name sizeKb status progress } }";
    request_body["variables"]["input"] = {
        {"name", gcode_name},
        {"printerId", printer_id},
        {"materials", materials_array},
        {"gcodeKey", key},
        {"isSingleShell", true},
        {"isMultiColor", isMultiColor},
        {"isMultiMaterial", isMultiMaterial}
    };
    std::string query_body = request_body.dump();
    return helio_create_gcode_and_poll(helio_api_url, helio_api_key, query_body,
                                       "createGcodeV3", effective_idempotency_key,
                                       should_cancel);
}

std::string HelioQuery::generate_simulation_graphql_query(const std::string &gcode_id,
                                                          float temperatureStabilizationHeight,
                                                          float airTemperatureAboveBuildPlate,
                                                          float stabilizedAirTemperature,
                                                          const std::string& job_name)
{
    CNumericLocalesSetter locales_setter;
    std::string name = job_name.empty() ? generateTimestampedString() : job_name;

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
    int layersToOptimizeEnd,
    const std::string& job_name)
{
    CNumericLocalesSetter locales_setter;
    std::string name = job_name.empty() ? generateTimestampedString() : job_name;

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
            BOOST_LOG_TRIVIAL(error) << "Helio create_optimization_default_get error: " << error << ", status: " << status;
        })
        .perform_sync();

    return res;
}

HelioQuery::CreateSimulationResult HelioQuery::create_simulation(const std::string helio_api_url,
                                                                 const std::string helio_api_key,
                                                                 const std::string gcode_id,
                                                                 SimulationInput sinput,
                                                                 const std::string job_name,
                                                                 const std::string idempotency_key,
                                                                 std::function<bool()> should_cancel)
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
    const std::string effective_idempotency_key = idempotency_key.empty() ? helio_generate_idempotency_key() : idempotency_key;
    const std::string effective_job_name = job_name.empty() ? helio_generate_retry_job_name(effective_idempotency_key) : job_name;

    std::string query_body = generate_simulation_graphql_query(gcode_id,
                                                    layer_threshold_meters,
                                                    initial_room_temp_kelvin,
                                                    object_proximity_airtemp_kelvin,
                                                    effective_job_name
    );

    HelioQuery::CreateSimulationResult res;
    res.reset();

    if (helio_should_cancel(should_cancel)) {
        res.error = _u8L("Helio action canceled");
        return res;
    }

    HelioRetryController retry_controller;
    for (int attempt = 1; attempt <= HELIO_CREATE_MAX_ATTEMPTS; ++attempt) {
        if (helio_should_cancel(should_cancel)) {
            res.error = _u8L("Helio action canceled");
            break;
        }

        HelioQuery::CreateSimulationResult attempt_res;
        attempt_res.reset();
        HelioRetryKind attempt_retry_kind = HelioRetryKind::None;

        auto http = Http::post(helio_api_url);

        http.header("Content-Type", "application/json")
            .header("Authorization", helio_api_key)
            .header("HelioAdditive-Client-Name", SLIC3R_APP_NAME)
            .header("HelioAdditive-Client-Version", GUI::VersionInfo::convert_full_version(SLIC3R_VERSION))
            .set_post_body(query_body);
        helio_attach_idempotency_key(http, effective_idempotency_key);

        if (GUI::wxGetApp().app_config->get("language") == "zh_CN") {
            http.header("Accept-Language", "zh-CN");
        }

        http.timeout_connect(20)
            .timeout_max(100)
            .on_complete([&attempt_res, &attempt_retry_kind](std::string body, unsigned status) {
                attempt_res.status = status;
                if (helio_is_terminal_http_status(status)) {
                    attempt_res.error = helio_http_status_message(status, body);
                    attempt_res.success = false;
                    return;
                }
                if (status != 200) {
                    attempt_res.error = helio_http_status_message(status, body);
                    attempt_res.success = false;
                    attempt_retry_kind = helio_classify_retry(status, body);
                    return;
                }

                try {
                    nlohmann::json parsed_obj = nlohmann::json::parse(body);
                    if (helio_has_graphql_errors(parsed_obj)) {
                        std::string message = format_error(body);
                        attempt_res.error   = message;
                        attempt_res.success = false;
                        attempt_retry_kind = helio_classify_graphql_response(status, parsed_obj);
                    } else if (parsed_obj.contains("data") && parsed_obj["data"].contains("createSimulation") &&
                               parsed_obj["data"]["createSimulation"].is_object()) {
                        const nlohmann::json& simulation = parsed_obj["data"]["createSimulation"];
                        attempt_res.id = simulation.value("id", "");
                        attempt_res.name = simulation.value("name", "");
                        if (!attempt_res.id.empty()) {
                            attempt_res.success = true;
                        } else {
                            attempt_res.success = false;
                            attempt_res.error = _u8L("Failed to create Simulation");
                            attempt_retry_kind = HelioRetryKind::Transient;
                        }
                    } else {
                        attempt_res.success = false;
                        attempt_res.error = _u8L("Failed to create Simulation");
                        attempt_retry_kind = HelioRetryKind::Transient;
                    }
                } catch (const std::exception& e) {
                    attempt_res.success = false;
                    attempt_res.error = std::string("Failed to parse response: ") + e.what();
                    attempt_retry_kind = HelioRetryKind::Transient;
                } catch (...) {
                    attempt_res.success = false;
                    attempt_res.error = _u8L("Failed to parse response");
                    attempt_retry_kind = HelioRetryKind::Transient;
                }
            })
            .on_error([&attempt_res, &attempt_retry_kind](std::string body, std::string error, unsigned status) {
                attempt_res.success = false;
                attempt_res.error  = error.empty() ? body : error;
                attempt_res.status = status;
                attempt_retry_kind = helio_classify_retry(status, body, error);
            })
            .on_header_callback([&attempt_res](std::string header_line) {
                auto trace_id = extract_trace_id(header_line);
                if (!trace_id.empty()) {
                    attempt_res.trace_id = trace_id;
                }
            })
            .perform_sync();

        res = attempt_res;
        if (res.success) {
            if (helio_should_cancel(should_cancel)) {
                if (!res.id.empty()) {
                    HelioQuery::stop_simulation(helio_api_url, helio_api_key, res.id);
                }
                res.reset();
                res.error = _u8L("Helio action canceled");
            }
            break;
        }

        const bool policy_allows_retry = retry_controller.should_retry(attempt_retry_kind);
        if (policy_allows_retry && attempt_retry_kind != HelioRetryKind::BackendAuth &&
            helio_adopt_recent_simulation_by_name(helio_api_url, helio_api_key, effective_job_name, res)) {
            if (helio_should_cancel(should_cancel)) {
                if (!res.id.empty()) {
                    HelioQuery::stop_simulation(helio_api_url, helio_api_key, res.id);
                }
                res.reset();
                res.error = _u8L("Helio action canceled");
            }
            break;
        }

        if (helio_should_cancel(should_cancel)) {
            res.error = _u8L("Helio action canceled");
            break;
        }

        if (!policy_allows_retry || attempt >= HELIO_CREATE_MAX_ATTEMPTS) {
            break;
        }

        BOOST_LOG_TRIVIAL(warning) << "Helio simulation create retry: kind="
                                   << helio_retry_kind_name(attempt_retry_kind)
                                   << ", attempt=" << attempt << ", status=" << res.status
                                   << ", trace-id=" << res.trace_id;

        if (!helio_sleep_for_retry_or_cancel(std::min(8, attempt * 2), should_cancel)) {
            res.error = _u8L("Helio action canceled");
            break;
        }
    }

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
            auto trace_id = extract_trace_id(header_line);
            if (!trace_id.empty()) {
                res.trace_id = trace_id;
            }
        })
        .on_complete([&res](std::string body, unsigned status) {
            res.status = status;
            res.retry_kind = HelioRetryKind::None;

            if (helio_is_terminal_http_status(status)) {
                res.error = helio_http_status_message(status, body);
                return;
            }
            if (status != 200) {
                res.error = helio_http_status_message(status, body);
                res.retry_kind = helio_classify_retry(status, body);
                return;
            }

            try {
                nlohmann::json parsed_obj = nlohmann::json::parse(body);
                if (helio_has_graphql_errors(parsed_obj)) {
                    std::string message = format_error(body);
                    res.error = message;
                    res.retry_kind = helio_classify_graphql_response(status, parsed_obj);
                    return;
                }

                if (!parsed_obj.contains("data") || !parsed_obj["data"].contains("simulation") ||
                    parsed_obj["data"]["simulation"].is_null()) {
                    res.error = _u8L("Helio simulation response is incomplete");
                    res.retry_kind = HelioRetryKind::Transient;
                    return;
                }

                auto simulation = parsed_obj["data"]["simulation"];
                if (!simulation.contains("status") || !simulation["status"].is_string()) {
                    res.error = _u8L("Helio simulation response is incomplete");
                    res.retry_kind = HelioRetryKind::Transient;
                    return;
                }

                const std::string status_str = simulation["status"].get<std::string>();
                if (status_str == "FAILED") {
                    res.error = _u8L("Helio simulation task failed");
                }

                res.id = simulation.value("id", "");
                res.name = simulation.value("name", "");
                res.progress = simulation.value("progress", 0.0f);
                res.is_finished = status_str == "FINISHED";
                if (res.is_finished) {
                    res.url = simulation.value("thermalIndexGcodeUrl", "");
                    if (res.url.empty()) {
                        res.is_finished = false;
                        res.error = _u8L("Helio simulation response is incomplete");
                        res.retry_kind = HelioRetryKind::Transient;
                        return;
                    }
                    
                    // Parse printInfo if present
                    if (simulation.contains("printInfo") && !simulation["printInfo"].is_null()) {
                        auto printInfo = simulation["printInfo"];
                        PrintInfo info;
                        info.printOutcome = printInfo.value("printOutcome", "");
                        if (printInfo.contains("printOutcomeDescription") && printInfo["printOutcomeDescription"].is_string()) {
                            info.printOutcomeDescription = printInfo["printOutcomeDescription"];
                        }
                        info.temperatureDirection = printInfo.value("temperatureDirection", "");
                        if (printInfo.contains("temperatureDirectionDescription") && printInfo["temperatureDirectionDescription"].is_string()) {
                            info.temperatureDirectionDescription = printInfo["temperatureDirectionDescription"];
                        }
                        
                        if (printInfo.contains("caveats") && printInfo["caveats"].is_array()) {
                            for (const auto& caveat : printInfo["caveats"]) {
                                Caveat c;
                                c.caveatType = caveat.value("caveatType", "");
                                c.description = caveat.value("description", "");
                                info.caveats.push_back(c);
                            }
                        }
                        res.simulationResult.printInfo = info;
                    }
                    
                    // Parse speedFactor if present
                    if (simulation.contains("speedFactor") && !simulation["speedFactor"].is_null()) {
                        res.simulationResult.speedFactor = simulation["speedFactor"];
                    }
                    
                    // Parse suggestedFixes if present
                    if (simulation.contains("suggestedFixes") && simulation["suggestedFixes"].is_array()) {
                        for (const auto& fixJson : simulation["suggestedFixes"]) {
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
            } catch (const std::exception& e) {
                res.error = std::string("Helio simulation response parse failed: ") + e.what();
                res.retry_kind = HelioRetryKind::Transient;
            } catch (...) {
                res.error = _u8L("Helio simulation response parse failed");
                res.retry_kind = HelioRetryKind::Transient;
            }
        })
        .on_error([&res](std::string body, std::string error, unsigned status) {
            res.error  = error.empty() ? body : error;
            res.status = status;
            res.retry_kind = helio_classify_retry(status, body, error);
        })
        .perform_sync();

    return res;
}

Slic3r::HelioQuery::CreateOptimizationResult HelioQuery::create_optimization(const std::string helio_api_url, 
                                                                             const std::string helio_api_key, 
                                                                             const std::string gcode_id,
                                                                             OptimizationInput oinput,
                                                                             const std::string job_name,
                                                                             const std::string idempotency_key,
                                                                             std::function<bool()> should_cancel)
{

    std::string query_body;
    const std::string effective_idempotency_key = idempotency_key.empty() ? helio_generate_idempotency_key() : idempotency_key;
    const std::string effective_job_name = job_name.empty() ? helio_generate_retry_job_name(effective_idempotency_key) : job_name;

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
            oinput.layers_to_optimize[1],
            effective_job_name);
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
            oinput.layers_to_optimize[1],
            effective_job_name);
    }
    

    HelioQuery::CreateOptimizationResult res;
    res.reset();

    if (helio_should_cancel(should_cancel)) {
        res.error = _u8L("Helio action canceled");
        return res;
    }

    HelioRetryController retry_controller;
    for (int attempt = 1; attempt <= HELIO_CREATE_MAX_ATTEMPTS; ++attempt) {
        if (helio_should_cancel(should_cancel)) {
            res.error = _u8L("Helio action canceled");
            break;
        }

        HelioQuery::CreateOptimizationResult attempt_res;
        attempt_res.reset();
        HelioRetryKind attempt_retry_kind = HelioRetryKind::None;

        auto http = Http::post(helio_api_url);
        http.header("Content-Type", "application/json")
            .header("Authorization", helio_api_key)
            .header("HelioAdditive-Client-Name", SLIC3R_APP_NAME)
            .header("HelioAdditive-Client-Version", GUI::VersionInfo::convert_full_version(SLIC3R_VERSION))
            .set_post_body(query_body);
        helio_attach_idempotency_key(http, effective_idempotency_key);

        if (GUI::wxGetApp().app_config->get("language") == "zh_CN") {
            http.header("Accept-Language", "zh-CN");
        }

        http.timeout_connect(20)
            .timeout_max(100)
            .on_header_callback([&attempt_res](std::string header_line) {
                auto trace_id = extract_trace_id(header_line);
                if (!trace_id.empty()) {
                    attempt_res.trace_id = trace_id;
                }
            })
            .on_complete([&attempt_res, &attempt_retry_kind](std::string body, unsigned status) {
                attempt_res.status = status;
                if (helio_is_terminal_http_status(status)) {
                    attempt_res.error = helio_http_status_message(status, body);
                    attempt_res.success = false;
                    return;
                }
                if (status != 200) {
                    attempt_res.error = helio_http_status_message(status, body);
                    attempt_res.success = false;
                    attempt_retry_kind = helio_classify_retry(status, body);
                    return;
                }

                try {
                    nlohmann::json parsed_obj = nlohmann::json::parse(body);
                    if (helio_has_graphql_errors(parsed_obj)) {
                        std::string message = format_error(body);
                        attempt_res.error   = message;
                        attempt_res.success = false;
                        attempt_retry_kind = helio_classify_graphql_response(status, parsed_obj);
                    } else if (parsed_obj.contains("data") && parsed_obj["data"].contains("createOptimization") &&
                               parsed_obj["data"]["createOptimization"].is_object()) {
                        const nlohmann::json& optimization = parsed_obj["data"]["createOptimization"];
                        attempt_res.id = optimization.value("id", "");
                        attempt_res.name = optimization.value("name", "");
                        if (!attempt_res.id.empty()) {
                            attempt_res.success = true;
                        } else {
                            attempt_res.success = false;
                            attempt_res.error = _u8L("Failed to create Optimization");
                            attempt_retry_kind = HelioRetryKind::Transient;
                        }
                    } else {
                        attempt_res.success = false;
                        attempt_res.error = _u8L("Failed to create Optimization");
                        attempt_retry_kind = HelioRetryKind::Transient;
                    }
                } catch (const std::exception& e) {
                    attempt_res.success = false;
                    attempt_res.error = std::string("Failed to parse response: ") + e.what();
                    attempt_retry_kind = HelioRetryKind::Transient;
                } catch (...) {
                    attempt_res.success = false;
                    attempt_res.error = _u8L("Failed to parse response");
                    attempt_retry_kind = HelioRetryKind::Transient;
                }
            })
            .on_error([&attempt_res, &attempt_retry_kind](std::string body, std::string error, unsigned status) {
                attempt_res.success = false;
                attempt_res.error   = error.empty() ? body : error;
                attempt_res.status  = status;
                attempt_retry_kind = helio_classify_retry(status, body, error);
            })
            .perform_sync();

        res = attempt_res;
        if (res.success) {
            if (helio_should_cancel(should_cancel)) {
                if (!res.id.empty()) {
                    HelioQuery::stop_optimization(helio_api_url, helio_api_key, res.id);
                }
                res.reset();
                res.error = _u8L("Helio action canceled");
            }
            break;
        }

        const bool policy_allows_retry = retry_controller.should_retry(attempt_retry_kind);
        if (policy_allows_retry && attempt_retry_kind != HelioRetryKind::BackendAuth &&
            helio_adopt_recent_optimization_by_name(helio_api_url, helio_api_key, effective_job_name, res)) {
            if (helio_should_cancel(should_cancel)) {
                if (!res.id.empty()) {
                    HelioQuery::stop_optimization(helio_api_url, helio_api_key, res.id);
                }
                res.reset();
                res.error = _u8L("Helio action canceled");
            }
            break;
        }

        if (helio_should_cancel(should_cancel)) {
            res.error = _u8L("Helio action canceled");
            break;
        }

        if (!policy_allows_retry || attempt >= HELIO_CREATE_MAX_ATTEMPTS) {
            break;
        }

        BOOST_LOG_TRIVIAL(warning) << "Helio optimization create retry: kind="
                                   << helio_retry_kind_name(attempt_retry_kind)
                                   << ", attempt=" << attempt << ", status=" << res.status
                                   << ", trace-id=" << res.trace_id;

        if (!helio_sleep_for_retry_or_cancel(std::min(8, attempt * 2), should_cancel)) {
            res.error = _u8L("Helio action canceled");
            break;
        }
    }

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
            auto trace_id = extract_trace_id(header_line);
            if (!trace_id.empty()) {
                res.trace_id = trace_id;
            }
        })
        .on_complete([&res](std::string body, unsigned status) {
            res.status = status;
            res.retry_kind = HelioRetryKind::None;

            if (helio_is_terminal_http_status(status)) {
                res.error = helio_http_status_message(status, body);
                return;
            }
            if (status != 200) {
                res.error = helio_http_status_message(status, body);
                res.retry_kind = helio_classify_retry(status, body);
                return;
            }

            try {
                nlohmann::json parsed_obj = nlohmann::json::parse(body);
                if (helio_has_graphql_errors(parsed_obj)) {
                    std::string message = format_error(body);
                    res.error = message;
                    res.retry_kind = helio_classify_graphql_response(status, parsed_obj);
                    return;
                }

                if (!parsed_obj.contains("data") || !parsed_obj["data"].contains("optimization") ||
                    parsed_obj["data"]["optimization"].is_null()) {
                    res.error = _u8L("Helio optimization response is incomplete");
                    res.retry_kind = HelioRetryKind::Transient;
                    return;
                }

                auto optimization = parsed_obj["data"]["optimization"];
                if (!optimization.contains("status") || !optimization["status"].is_string()) {
                    res.error = _u8L("Helio optimization response is incomplete");
                    res.retry_kind = HelioRetryKind::Transient;
                    return;
                }

                const std::string status_str = optimization["status"].get<std::string>();
                if (status_str == "FAILED") {
                    res.error = _u8L("Helio optimization task failed");
                }

                res.id = optimization.value("id", "");
                res.name = optimization.value("name", "");
                res.progress = optimization.value("progress", 0.0f);
                res.is_finished = status_str == "FINISHED";
                if (res.is_finished) {
                    res.qualityStdImprovement = optimization.value("qualityStdImprovement", "");
                    res.qualityMeanImprovement = optimization.value("qualityMeanImprovement", "");
                    res.url = optimization.value("optimizedGcodeWithThermalIndexesUrl", "");
                    if (res.url.empty()) {
                        res.is_finished = false;
                        res.error = _u8L("Helio optimization response is incomplete");
                        res.retry_kind = HelioRetryKind::Transient;
                        return;
                    }
                }
            } catch (const std::exception& e) {
                res.error = std::string("Helio optimization response parse failed: ") + e.what();
                res.retry_kind = HelioRetryKind::Transient;
            } catch (...) {
                res.error = _u8L("Helio optimization response parse failed");
                res.retry_kind = HelioRetryKind::Transient;
            }
        })
        .on_error([&res](std::string body, std::string error, unsigned status) {
            res.error  = error.empty() ? body : error;
            res.status = status;
            res.retry_kind = helio_classify_retry(status, body, error);
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

bool HelioBackgroundProcess::begin_action()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_thread.joinable()) {
        if (!m_worker_completed)
            return false;
        m_thread.join();
    }

    ++m_action_generation;
    m_state = STATE_STARTED;
    return true;
}

void HelioBackgroundProcess::stop()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_action_generation;
    m_state = STATE_CANCELED;
}

bool HelioBackgroundProcess::was_canceled()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state == STATE_CANCELED ||
           (helio_worker_generation != 0 && helio_worker_generation != m_action_generation);
}

bool HelioBackgroundProcess::is_action_current(std::uint64_t generation)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return generation != 0 && generation == m_action_generation;
}

void HelioBackgroundProcess::set_state(State state)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (helio_worker_generation == 0 || helio_worker_generation == m_action_generation) {
        m_state = state;
    }
}

bool HelioBackgroundProcess::helio_thread_start(std::mutex&                                slicing_mutex,
                                                 std::condition_variable&                   slicing_condition,
                                                 BackgroundSlicingProcess::State&           slicing_state,
                                                 std::unique_ptr<GUI::NotificationManager>& notification_manager)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_thread.joinable())
        return false;

    const std::uint64_t generation = m_action_generation;
    m_state = STATE_STARTED;
    m_worker_completed = false;
    m_thread = create_thread([this, generation, &slicing_mutex, &slicing_condition, &slicing_state, &notification_manager] {
        helio_worker_generation = generation;
        struct CompletionGuard {
            HelioBackgroundProcess& process;
            ~CompletionGuard()
            {
                helio_worker_generation = 0;
                std::lock_guard<std::mutex> lock(process.m_mutex);
                process.m_worker_completed = true;
            }
        } completion_guard{*this};

        this->helio_threaded_process_start(slicing_mutex, slicing_condition, slicing_state, notification_manager);
    });
    return true;
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
        wxPostEvent(GUI::wxGetApp().plater(), HelioActionEvent(GUI::EVT_HELIO_PROCESSING_STARTED, 0, helio_worker_generation));

        Slic3r::PrintBase::SlicingStatus status = Slic3r::PrintBase::SlicingStatus(0.0, "Helio: Process Started");
        status.is_helio = true;
        Slic3r::SlicingStatusEvent*      evt    = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
        evt->generation = helio_worker_generation;
        wxQueueEvent(GUI::wxGetApp().plater(), evt);

        BOOST_LOG_TRIVIAL(debug) << "Helio process started";

        /*check api url*/
        if (helio_api_url.empty()) {
            set_state(STATE_CANCELED);
            Slic3r::HelioCompletionEvent *evt = new Slic3r::HelioCompletionEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, "", "", false,
                _u8L("Helio API endpoint is empty, please check the configuration."));
            evt->generation = helio_worker_generation;
            wxQueueEvent(GUI::wxGetApp().plater(), evt);
            return;
        }

        /*check Personal assecc token */
        if (helio_origin_key.empty()) {
            set_state(STATE_CANCELED);
            Slic3r::HelioCompletionEvent *evt = new Slic3r::HelioCompletionEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, "", "", false,
                _u8L("Personal assecc token is empty, please fill in the correct token."));
            evt->generation = helio_worker_generation;
            wxQueueEvent(GUI::wxGetApp().plater(), evt);
            return;
        }

        HelioQuery::PresignedURLResult create_presigned_url_res{};
        HelioQuery::UploadFileResult upload_file_res{};
        bool upload_success = false;
        HelioRetryController presigned_retry_controller;

        for (int attempt = 1; attempt <= HELIO_UPLOAD_MAX_ATTEMPTS && !was_canceled(); ++attempt) {
            status = Slic3r::PrintBase::SlicingStatus(
                3, (boost::format("Helio: preparing upload (%1%/%2%)") % attempt % HELIO_UPLOAD_MAX_ATTEMPTS).str());
            status.is_helio = true;
            evt = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
            evt->generation = helio_worker_generation;
            wxQueueEvent(GUI::wxGetApp().plater(), evt);

            create_presigned_url_res = HelioQuery::create_presigned_url(helio_api_url, helio_api_key);
            if (was_canceled()) {
                break;
            }
            if (create_presigned_url_res.error.empty() && create_presigned_url_res.status == 200) {
                presigned_retry_controller.reset();
                status = Slic3r::PrintBase::SlicingStatus(5, _u8L("Helio: Presigned URL Created"));
                status.is_helio = true;
                evt = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
                evt->generation = helio_worker_generation;
                wxQueueEvent(GUI::wxGetApp().plater(), evt);

                if (was_canceled()) {
                    break;
                }
                upload_file_res = HelioQuery::upload_file_to_presigned_url(m_gcode_result->filename,
                                                                           create_presigned_url_res.url);
                if (upload_file_res.success) {
                    upload_success = true;
                    break;
                }

                BOOST_LOG_TRIVIAL(warning) << "Helio upload attempt " << attempt << " failed: " << upload_file_res.error;
            } else {
                BOOST_LOG_TRIVIAL(warning) << "Helio presigned URL attempt " << attempt
                                           << " failed: retry-kind="
                                           << helio_retry_kind_name(create_presigned_url_res.retry_kind)
                                           << ", status=" << create_presigned_url_res.status
                                           << ", trace-id=" << create_presigned_url_res.trace_id
                                           << ", error=" << create_presigned_url_res.error;
                if (!presigned_retry_controller.should_retry(create_presigned_url_res.retry_kind)) {
                    break;
                }
            }

            if (attempt < HELIO_UPLOAD_MAX_ATTEMPTS &&
                !helio_sleep_for_retry_or_cancel(std::min(8, attempt * 2), [this]() { return was_canceled(); })) {
                break;
            }
        }

        if (upload_success && !was_canceled()) {
            status = Slic3r::PrintBase::SlicingStatus(10, _u8L("Helio: file successfully uploaded"));
            status.is_helio = true;
            evt    = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
            evt->generation = helio_worker_generation;
            wxQueueEvent(GUI::wxGetApp().plater(), evt);

            const std::string create_gcode_idempotency_key = helio_generate_idempotency_key();
            HelioQuery::CreateGCodeResult create_gcode_res;
            if (use_v3) {
                create_gcode_res = HelioQuery::create_gcode_v3(create_presigned_url_res.key, helio_api_url,
                                                               helio_api_key, printer_id, materials,
                                                               is_multi_color, is_multi_material,
                                                               create_gcode_idempotency_key,
                                                               [this]() { return was_canceled(); });
            } else {
                create_gcode_res = HelioQuery::create_gcode(create_presigned_url_res.key, helio_api_url,
                                                            helio_api_key, printer_id, filament_id,
                                                            create_gcode_idempotency_key,
                                                            [this]() { return was_canceled(); });
            }

            if (action == 0) {
                create_simulation_step(create_gcode_res, notification_manager);
            }
            else if (action == 1) {
                create_optimization_step(create_gcode_res, notification_manager);
            }
        } else if (was_canceled()) {
            set_state(STATE_CANCELED);
        } else {
            std::string presigned_url_message;
            if (!create_presigned_url_res.error.empty() || create_presigned_url_res.status != 200) {
                presigned_url_message = (boost::format("error: %1%") % create_presigned_url_res.error).str();
            } else {
                presigned_url_message = _u8L("Helio: file upload failed");
                if (!upload_file_res.error.empty()) {
                    presigned_url_message += "\n" + upload_file_res.error;
                }
            }

            if (create_presigned_url_res.status == 401) {
                presigned_url_message += "\n ";
                presigned_url_message += _u8L("Please make sure you have the correct API key set in preferences.");
            }

            set_state(STATE_CANCELED);

            Slic3r::HelioCompletionEvent* evt = new Slic3r::HelioCompletionEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, "", "", false,
                                                                                 presigned_url_message);
            evt->generation = helio_worker_generation;
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
        evt->generation = helio_worker_generation;
        wxQueueEvent(GUI::wxGetApp().plater(), evt);

        const std::string gcode_id = create_gcode_res.id;
        HelioQuery::CreateSimulationResult create_simulation_res =
            HelioQuery::create_simulation(helio_api_url, helio_api_key, gcode_id, simulation_input_data,
                                          std::string(), std::string(), [this]() { return was_canceled(); });
        current_simulation_result = create_simulation_res;
        current_optimization_result.reset();
        // Clear previous stored result since we're starting a new simulation
        clear_last_simulation_result();

        if (!create_simulation_res.trace_id.empty()) {
            HelioQuery::last_simulation_trace_id = create_simulation_res.trace_id;  
        }

        if (was_canceled()) {
            set_state(STATE_CANCELED);
            return;
        }

        if (create_simulation_res.success && !was_canceled()) {
            status = Slic3r::PrintBase::SlicingStatus(20, _u8L("Helio: simulation successfully created"));
            evt    = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
            evt->generation = helio_worker_generation;
            wxQueueEvent(GUI::wxGetApp().plater(), evt);

            int transient_failures = 0;
            int times_queried      = 0;
            HelioRetryController backend_auth_retry_controller;

            while (!was_canceled()) {
                int sleep_seconds = 3;
                HelioQuery::CheckSimulationProgressResult check_simulation_progress_res =
                    HelioQuery::check_simulation_progress(helio_api_url, helio_api_key, create_simulation_res.id);

                const bool retryable = check_simulation_progress_res.retry_kind != HelioRetryKind::None &&
                    backend_auth_retry_controller.should_retry(check_simulation_progress_res.retry_kind);
                if (retryable) {
                    if (check_simulation_progress_res.retry_kind == HelioRetryKind::Transient) {
                        transient_failures++;
                        sleep_seconds = helio_next_poll_backoff_seconds(transient_failures);
                    } else {
                        transient_failures = 0;
                    }
                    if (check_simulation_progress_res.retry_kind == HelioRetryKind::Transient &&
                        transient_failures >= HELIO_MAX_TRANSIENT_POLL_FAILURES) {
                        set_state(STATE_CANCELED);

                        const std::string last_error = check_simulation_progress_res.error.empty() ?
                            _u8L("transient progress checks did not recover") : check_simulation_progress_res.error;
                        const std::string error_msg = _u8L("Helio: simulation failed") + "\n" +
                            (boost::format("Exceeded retry limit after %1% consecutive transient progress checks. Last error: %2%") %
                             transient_failures % last_error).str();
                        Slic3r::HelioCompletionEvent* completion_evt =
                            new Slic3r::HelioCompletionEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, "", "", false, error_msg);
                        completion_evt->generation = helio_worker_generation;
                        wxQueueEvent(GUI::wxGetApp().plater(), completion_evt);
                        break;
                    }
                    status = Slic3r::PrintBase::SlicingStatus(
                        35, (boost::format("Helio: Simulation check retrying in %1% seconds") % sleep_seconds).str());
                    status.is_helio = true;
                    evt = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
                    evt->generation = helio_worker_generation;
                    wxQueueEvent(GUI::wxGetApp().plater(), evt);
                    BOOST_LOG_TRIVIAL(warning) << "Helio simulation progress check retry: kind="
                                               << helio_retry_kind_name(check_simulation_progress_res.retry_kind)
                                               << ", attempt=" << (times_queried + 1)
                                               << ", status=" << check_simulation_progress_res.status
                                               << ", trace-id=" << check_simulation_progress_res.trace_id
                                               << ", error=" << check_simulation_progress_res.error;
                } else if (check_simulation_progress_res.status == 200 && check_simulation_progress_res.error.empty()) {
                    transient_failures = 0;
                    backend_auth_retry_controller.reset();
                    std::string trailing_dots = "";

                    for (int i = 0; i < (times_queried % 3); i++) {
                        trailing_dots += "....";
                    }

                    status = Slic3r::PrintBase::SlicingStatus(35 + (80 - 35) * (check_simulation_progress_res.progress / 100.0f),
                                                              _u8L("Helio: simulation working") + trailing_dots);
                    status.is_helio = true;
                    evt = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
                    evt->generation = helio_worker_generation;
                    wxQueueEvent(GUI::wxGetApp().plater(), evt);
                    if (check_simulation_progress_res.is_finished) {
                        // Start loading preview in background immediately (don't wait for dialog)
                        int original_time_seconds = static_cast<int>(m_gcode_result->print_statistics.modes[0].time);
                        std::string url = check_simulation_progress_res.url;
                        std::string filename = m_gcode_result->filename;
                        HelioQuery::SimulationResult sim_result = check_simulation_progress_res.simulationResult;

                        // Store simulation result to current plate for later access (e.g., "View Summary" button)
                        GUI::wxGetApp().plater()->CallAfter([process = this, generation = helio_worker_generation, sim_result, original_time_seconds]() {
                            if (!process->is_action_current(generation)) {
                                return;
                            }
                            auto* plate = GUI::wxGetApp().plater()->get_partplate_list().get_curr_plate();
                            if (plate) {
                                HelioPlateResult helio_result;
                                helio_result.action = 0;
                                helio_result.simulation_result = sim_result;
                                helio_result.original_print_time_seconds = original_time_seconds;
                                helio_result.is_valid = true;
                                plate->set_helio_result(helio_result);
                            }
                        });

                        // Start preview loading in background thread immediately
                        std::string simulated_gcode_path = create_path_for_simulated_gcode(filename);
                        HelioQuery::RatingData rating_data;
                        save_downloaded_gcode_and_load_preview(url, simulated_gcode_path, filename, notification_manager, rating_data);

                        // Show simulation results dialog on main thread (non-blocking for preview)
                        // Capture roles_times for calculating optimizable sections
                        auto roles_times = m_gcode_result->print_statistics.modes[0].roles_times;
                        GUI::wxGetApp().plater()->CallAfter([process = this, generation = helio_worker_generation, sim_result, original_time_seconds, roles_times]() {
                            if (!process->is_action_current(generation)) {
                                return;
                            }
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
                    evt->generation = helio_worker_generation;
                    wxQueueEvent(GUI::wxGetApp().plater(), evt);
                    break;
                }

                times_queried++;
                if (!helio_sleep_for_retry_or_cancel(sleep_seconds, [this]() { return was_canceled(); })) {
                    break;
                }
            }

        } else {
            set_state(STATE_CANCELED);

            std::string error;
            try{
                error = _u8L("Helio: Failed to create Simulation") + "\n" + create_simulation_res.error;
            }
            catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << "Helio create_simulation_step format error: " << e.what();
            } catch (...) {
                BOOST_LOG_TRIVIAL(error) << "Helio create_simulation_step format error: unknown error";
            }
            
            Slic3r::HelioCompletionEvent* evt = new Slic3r::HelioCompletionEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, "", "", false, error);
            evt->generation = helio_worker_generation;
            wxQueueEvent(GUI::wxGetApp().plater(), evt);
        }

    } else {
        set_state(STATE_CANCELED);

        std::string error;
        try {
            error = _u8L("Helio: Failed to create GCode") + "\n" + create_gcode_res.error;
        }
        catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "Helio create_simulation_step GCode format error: " << e.what();
        } catch (...) {
            BOOST_LOG_TRIVIAL(error) << "Helio create_simulation_step GCode format error: unknown error";
        }

        Slic3r::HelioCompletionEvent* evt = new Slic3r::HelioCompletionEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, "", "", false, error);
        evt->generation = helio_worker_generation;
        wxQueueEvent(GUI::wxGetApp().plater(), evt);
    }
}

void HelioBackgroundProcess::create_optimization_step(HelioQuery::CreateGCodeResult create_gcode_res,std::unique_ptr<GUI::NotificationManager>& notification_manager)
{
    if (create_gcode_res.success && !was_canceled()) {
        Slic3r::PrintBase::SlicingStatus status = Slic3r::PrintBase::SlicingStatus(15,_u8L( "Helio: GCode created successfully"));
        status.is_helio = true;
        Slic3r::SlicingStatusEvent* evt = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
        evt->generation = helio_worker_generation;
        wxQueueEvent(GUI::wxGetApp().plater(), evt);

        const std::string gcode_id = create_gcode_res.id;
        HelioQuery::CreateOptimizationResult create_optimization_res =
            HelioQuery::create_optimization(helio_api_url, helio_api_key, gcode_id, optimization_input_data,
                                            std::string(), std::string(), [this]() { return was_canceled(); });
        current_optimization_result = create_optimization_res;
        current_simulation_result.reset();
        // Clear previous stored result since we're starting a new optimization
        clear_last_simulation_result();

        if (!create_optimization_res.trace_id.empty()) {
            HelioQuery::last_optimization_trace_id = create_optimization_res.trace_id;
        }

        if (was_canceled()) {
            set_state(STATE_CANCELED);
            return;
        }

        if (create_optimization_res.success && !was_canceled()) {
            status = Slic3r::PrintBase::SlicingStatus(20, _u8L("Helio: optimization successfully created"));
            evt = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
            evt->generation = helio_worker_generation;
            wxQueueEvent(GUI::wxGetApp().plater(), evt);

            int transient_failures = 0;
            int times_queried = 0;
            HelioRetryController backend_auth_retry_controller;

            while (!was_canceled()) {
                int sleep_seconds = 3;
                HelioQuery::CheckOptimizationResult check_optimzaion_progress_res =
                    HelioQuery::check_optimization_progress(helio_api_url, helio_api_key, create_optimization_res.id);

                const bool retryable = check_optimzaion_progress_res.retry_kind != HelioRetryKind::None &&
                    backend_auth_retry_controller.should_retry(check_optimzaion_progress_res.retry_kind);
                if (retryable) {
                    if (check_optimzaion_progress_res.retry_kind == HelioRetryKind::Transient) {
                        transient_failures++;
                        sleep_seconds = helio_next_poll_backoff_seconds(transient_failures);
                    } else {
                        transient_failures = 0;
                    }
                    if (check_optimzaion_progress_res.retry_kind == HelioRetryKind::Transient &&
                        transient_failures >= HELIO_MAX_TRANSIENT_POLL_FAILURES) {
                        set_state(STATE_CANCELED);

                        const std::string last_error = check_optimzaion_progress_res.error.empty() ?
                            _u8L("transient progress checks did not recover") : check_optimzaion_progress_res.error;
                        const std::string error_msg = _u8L("Helio: optimization failed") + "\n" +
                            (boost::format("Exceeded retry limit after %1% consecutive transient progress checks. Last error: %2%") %
                             transient_failures % last_error).str();
                        Slic3r::HelioCompletionEvent* completion_evt =
                            new Slic3r::HelioCompletionEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, "", "", false, error_msg);
                        completion_evt->generation = helio_worker_generation;
                        wxQueueEvent(GUI::wxGetApp().plater(), completion_evt);
                        break;
                    }
                    status = Slic3r::PrintBase::SlicingStatus(
                        35, (boost::format("Helio: Optimization check retrying in %1% seconds") % sleep_seconds).str());
                    status.is_helio = true;
                    evt = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
                    evt->generation = helio_worker_generation;
                    wxQueueEvent(GUI::wxGetApp().plater(), evt);
                    BOOST_LOG_TRIVIAL(warning) << "Helio optimization progress check retry: kind="
                                               << helio_retry_kind_name(check_optimzaion_progress_res.retry_kind)
                                               << ", attempt=" << (times_queried + 1)
                                               << ", status=" << check_optimzaion_progress_res.status
                                               << ", trace-id=" << check_optimzaion_progress_res.trace_id
                                               << ", error=" << check_optimzaion_progress_res.error;
                } else if (check_optimzaion_progress_res.status == 200 && check_optimzaion_progress_res.error.empty()) {
                    transient_failures = 0;
                    backend_auth_retry_controller.reset();
                    std::string trailing_dots = "";
                    for (int i = 0; i < (times_queried % 3); i++) {
                        trailing_dots += "....";
                    }
                    status = Slic3r::PrintBase::SlicingStatus(35 + (80 - 35) * (check_optimzaion_progress_res.progress / 100.0f),
                        _u8L("Helio: optimization working") + trailing_dots);
                    status.is_helio = true;
                    evt = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
                    evt->generation = helio_worker_generation;
                    wxQueueEvent(GUI::wxGetApp().plater(), evt);
                    if (check_optimzaion_progress_res.is_finished) {
                        std::string optimized_gcode_path = HelioBackgroundProcess::create_path_for_optimization_gcode(m_gcode_result->filename);
                        HelioQuery::RatingData rating_data;
                        rating_data.action = 1;
                        rating_data.qualityMeanImprovement = check_optimzaion_progress_res.qualityMeanImprovement;
                        rating_data.qualityStdImprovement = check_optimzaion_progress_res.qualityStdImprovement;
                        HelioBackgroundProcess::save_downloaded_gcode_and_load_preview(check_optimzaion_progress_res.url,
                            optimized_gcode_path, m_gcode_result->filename, notification_manager, rating_data);
                        break;
                    }
                } else {
                    set_state(STATE_CANCELED);
                    BOOST_LOG_TRIVIAL(error) << "Helio optimization progress check returned error: " << check_optimzaion_progress_res.error;
                    std::string error_msg = _u8L("Helio: optimization failed") + "\n" + check_optimzaion_progress_res.error;
                    Slic3r::HelioCompletionEvent* evt = new Slic3r::HelioCompletionEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, "", "", false, error_msg);
                    evt->generation = helio_worker_generation;
                    wxQueueEvent(GUI::wxGetApp().plater(), evt);
                    break;
                }
                times_queried++;
                if (!helio_sleep_for_retry_or_cancel(sleep_seconds, [this]() { return was_canceled(); })) {
                    break;
                }
            }
        } else {
            set_state(STATE_CANCELED);
            Slic3r::HelioCompletionEvent* evt = new Slic3r::HelioCompletionEvent(
                GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, "", "", false,
                (boost::format("Helio: Failed to create Optimization\n%1%") % create_optimization_res.error).str());
            evt->generation = helio_worker_generation;
            wxQueueEvent(GUI::wxGetApp().plater(), evt);
        }
    } else {
        set_state(STATE_CANCELED);
        std::string error;
        try {
            error = _u8L("Helio: Failed to create GCode") + "\n" + create_gcode_res.error;
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "Helio create_optimization_step gcode error: " << e.what();
            error = "Helio: Failed to create GCode";
        } catch (...) {
            BOOST_LOG_TRIVIAL(error) << "Helio create_optimization_step gcode: unknown error";
            error = "Helio: Failed to create GCode";
        }
        Slic3r::HelioCompletionEvent* evt = new Slic3r::HelioCompletionEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, "", "", false, error);
        evt->generation = helio_worker_generation;
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
    const std::uint64_t action_generation = helio_worker_generation;

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
                .timeout_connect(20)
                .timeout_max(100)
                .on_progress([this, action_generation](Http::Progress, bool& cancel) {
                    cancel = was_canceled() || !is_action_current(action_generation);
                })
                .perform_sync();

            if (was_canceled() || !is_action_current(action_generation)) {
                return;
            }

            if (response_status != 200) {
                number_of_attempts++;
                Slic3r::PrintBase::SlicingStatus status = Slic3r::PrintBase::SlicingStatus(
                    80, (boost::format("Helio: Could not download file. Attempts left %1%") % (max_attempts - number_of_attempts)).str());
                status.is_helio = true;
                Slic3r::SlicingStatusEvent* evt = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
                evt->generation = helio_worker_generation;
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
            evt->generation = helio_worker_generation;
            wxQueueEvent(GUI::wxGetApp().plater(), evt);
        }
            if (!helio_sleep_for_retry_or_cancel(1, [this]() { return was_canceled(); })) {
                break;
            }
            number_of_seconds_till_next_attempt--;
    }

    if (was_canceled() || !is_action_current(action_generation)) {
        return;
    }

    if (response_error.empty()) {
        wxFile file(wxString::FromUTF8(helio_gcode_path), wxFile::write);
        if (file.IsOpened()) {
            file.Write(downloaded_gcode.data(), downloaded_gcode.size());
            file.Close();
        }

        Slic3r::PrintBase::SlicingStatus status = Slic3r::PrintBase::SlicingStatus(100, _u8L("Helio: GCode downloaded successfully"));
        status.is_helio = true;
        Slic3r::SlicingStatusEvent*      evt    = new Slic3r::SlicingStatusEvent(GUI::EVT_SLICING_UPDATE, 0, status);
        evt->generation = helio_worker_generation;
        wxQueueEvent(GUI::wxGetApp().plater(), evt);
        HelioBackgroundProcess::load_helio_file_to_viwer(helio_gcode_path, tmp_path, rating_data);
    } else {
        set_state(STATE_CANCELED);

        std::string error;
        try {
            error = _u8L("Helio: GCode download failed") + "\n" + response_error;
        }
        catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "Helio gcode download error: " << e.what();
            error = "Helio: GCode download failed";
        } catch (...) {
            BOOST_LOG_TRIVIAL(error) << "Helio gcode download: unknown error";
            error = "Helio: GCode download failed";
        }

        Slic3r::HelioCompletionEvent* evt =
            new Slic3r::HelioCompletionEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, "", "", false, error);
        evt->generation = helio_worker_generation;
        wxQueueEvent(GUI::wxGetApp().plater(), evt);
    }
}

void HelioBackgroundProcess::load_helio_file_to_viwer(std::string file_path, std::string tmp_path, HelioQuery::RatingData rating_data)
{
    if (!is_action_current(helio_worker_generation)) {
        return;
    }
    const Vec3d origin = GUI::wxGetApp().plater()->get_partplate_list().get_current_plate_origin();
    auto old_result = GUI::wxGetApp().plater()->get_partplate_list().get_curr_plate()->get_gcode_result();
    // auto old_group_result = GUI::wxGetApp().plater()->get_partplate_list().get_curr_plate()->get_gcode_result()->nozzle_group_result;
    m_gcode_processor.set_xy_offset(origin(0), origin(1));
    if (old_result && old_result->nozzle_group_result) {
        m_gcode_processor.initialize_from_context(old_result->nozzle_group_result);
    }
    m_gcode_processor.process_file(file_path);

    auto res       = &m_gcode_processor.result();
    m_gcode_result = res;
    m_gcode_result->nozzle_group_result = old_result->nozzle_group_result;
    m_gcode_result->used_filaments = old_result->used_filaments;
    m_gcode_result->filament_change_sequence = old_result->filament_change_sequence;
    m_gcode_result->nozzle_change_sequence = old_result->nozzle_change_sequence;

    set_state(STATE_FINISHED);

    Slic3r::HelioCompletionEvent* evt = new Slic3r::HelioCompletionEvent(GUI::EVT_HELIO_PROCESSING_COMPLETED, 0, file_path, tmp_path, true, "", rating_data.action, rating_data.qualityMeanImprovement, rating_data.qualityStdImprovement);
    evt->generation = helio_worker_generation;
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
        "query": "query GetRecentRuns {\n  optimizations(page: 1, pageSize: 10) {\n    objects {\n      ... on Optimization {\n        id\n        name\n        status\n        optimizedGcodeWithThermalIndexesUrl\n        qualityMeanImprovement\n        qualityStdImprovement\n        gcode {\n          gcodeUrl\n          gcodeKey\n          material {\n            id\n            name\n          }\n          printer {\n            id\n            name\n          }\n          numberOfLayers\n          slicer\n        }\n      }\n    }\n  }\n  simulations(page: 1, pageSize: 10) {\n    objects {\n      ... on Simulation {\n        id\n        name\n        status\n        thermalIndexGcodeUrl\n        gcode {\n          gcodeUrl\n          gcodeKey\n          material {\n            id\n            name\n          }\n          printer {\n            id\n            name\n          }\n          numberOfLayers\n          slicer\n        }\n        printInfo {\n          printOutcome\n        }\n      }\n    }\n  }\n}",
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
    temp_optimizations.reserve(10);
    temp_simulations.reserve(10);

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
        // Sort valid timestamps newest-first; malformed or missing timestamps
        // are deliberately placed after all valid runs.
        const auto newest_first = [](const auto& a, const auto& b) {
            const bool a_has_timestamp = a.timestamp != std::chrono::system_clock::time_point{};
            const bool b_has_timestamp = b.timestamp != std::chrono::system_clock::time_point{};
            if (a_has_timestamp != b_has_timestamp)
                return a_has_timestamp;
            if (a.timestamp != b.timestamp)
                return a.timestamp > b.timestamp;
            if (a.name != b.name)
                return a.name < b.name;
            return a.id < b.id;
        };
        std::sort(temp_optimizations.begin(), temp_optimizations.end(), newest_first);
        std::sort(temp_simulations.begin(), temp_simulations.end(), newest_first);

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
    constexpr size_t timestamp_length = 19;
    const auto invalid_timestamp = std::chrono::system_clock::time_point{};

    for (size_t pos = name.size(); pos >= timestamp_length; --pos) {
        const size_t start = pos - timestamp_length;
        const std::string timestamp = name.substr(start, timestamp_length);
        if (timestamp[4] != '-' || timestamp[7] != '-' || timestamp[10] != 'T' ||
            timestamp[13] != ':' || timestamp[16] != ':')
            continue;

        const auto parse_number = [&timestamp](size_t offset) {
            if (!std::isdigit(static_cast<unsigned char>(timestamp[offset])) ||
                !std::isdigit(static_cast<unsigned char>(timestamp[offset + 1])))
                return -1;
            return (timestamp[offset] - '0') * 10 + timestamp[offset + 1] - '0';
        };
        if (!std::isdigit(static_cast<unsigned char>(timestamp[0])) ||
            !std::isdigit(static_cast<unsigned char>(timestamp[1])) ||
            !std::isdigit(static_cast<unsigned char>(timestamp[2])) ||
            !std::isdigit(static_cast<unsigned char>(timestamp[3])))
            continue;

        const int year = (timestamp[0] - '0') * 1000 + (timestamp[1] - '0') * 100 +
                         (timestamp[2] - '0') * 10 + timestamp[3] - '0';
        const int month = parse_number(5);
        const int day = parse_number(8);
        const int hour = parse_number(11);
        const int minute = parse_number(14);
        const int second = parse_number(17);
        const bool leap_year = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
        constexpr int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        const int max_day = month >= 1 && month <= 12
            ? days_in_month[month - 1] + (month == 2 && leap_year ? 1 : 0)
            : 0;
        if (year < 1 || month < 1 || day < 1 || day > max_day ||
            hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59)
            continue;

        std::tm utc = {};
        utc.tm_year = year - 1900;
        utc.tm_mon = month - 1;
        utc.tm_mday = day;
        utc.tm_hour = hour;
        utc.tm_min = minute;
        utc.tm_sec = second;
#ifdef _WIN32
        const std::time_t parsed = _mkgmtime(&utc);
#else
        const std::time_t parsed = timegm(&utc);
#endif
        if (parsed != -1)
            return std::chrono::system_clock::from_time_t(parsed);
    }

    return invalid_timestamp;
}

} // namespace Slic3r
