#include "HelioRetryPolicy.hpp"

#include <algorithm>
#include <cctype>
#include <vector>

#include "nlohmann/json.hpp"

namespace Slic3r {

namespace {

constexpr const char HELIO_FAILED_TO_DESERIALIZE_SUBGRAPH_RESPONSE[] =
    "failed to deserialize subgraph response";

std::string helio_lower_copy(const std::string& value)
{
    std::string lowered;
    lowered.reserve(value.size());
    for (unsigned char c : value) {
        lowered += static_cast<char>(std::tolower(c));
    }
    return lowered;
}

bool helio_error_object_has_exact_backend_auth_error(const nlohmann::json& error)
{
    return error.is_object() && error.contains("message") && error["message"].is_string() &&
           error["message"].get<std::string>() == HELIO_BACKEND_AUTH_401_MESSAGE;
}

bool helio_error_object_has_message(const nlohmann::json& error)
{
    return error.is_object() && error.contains("message") && error["message"].is_string();
}

bool helio_graphql_errors_have_transient_message(const nlohmann::json& errors)
{
    const auto has_transient_message = [](const nlohmann::json& error) {
        return error.is_object() && error.contains("message") && error["message"].is_string() &&
               helio_is_transient_error_message(error["message"].get<std::string>());
    };

    if (errors.is_array()) {
        return std::any_of(errors.begin(), errors.end(), has_transient_message);
    }
    return has_transient_message(errors);
}

} // namespace

bool helio_is_terminal_http_status(unsigned status) noexcept
{
    return status == 401 || status == 403 || status == 404;
}

bool helio_is_transient_http_status(unsigned status) noexcept
{
    return status == 0 || status == 408 || status == 425 || status == 429 || status >= 500;
}

bool helio_is_transient_error_message(const std::string& message)
{
    const std::string lower = helio_lower_copy(message);
    if (lower == HELIO_FAILED_TO_DESERIALIZE_SUBGRAPH_RESPONSE) {
        return true;
    }

    static const std::vector<std::string> transient_markers = {
        "temporar",
        "timeout",
        "timed out",
        "try again",
        "rate limit",
        "too many requests",
        "network",
        "connection",
        "disconnect",
        "reset by peer",
        "econn",
        "unavailable",
        "gateway",
        "service unavailable",
        "internal server",
        "deadline",
        "overloaded"
    };

    return std::any_of(transient_markers.begin(), transient_markers.end(), [&lower](const std::string& marker) {
        return lower.find(marker) != std::string::npos;
    });
}

bool helio_has_graphql_errors(const nlohmann::json& response)
{
    if (!response.is_object() || !response.contains("errors")) {
        return false;
    }

    const nlohmann::json& errors = response["errors"];
    if (errors.is_array()) {
        return std::any_of(errors.begin(), errors.end(), helio_error_object_has_message);
    }
    return helio_error_object_has_message(errors);
}

bool helio_has_exact_backend_auth_error(const nlohmann::json& response)
{
    if (!helio_has_graphql_errors(response)) {
        return false;
    }

    const nlohmann::json& errors = response["errors"];
    if (errors.is_array()) {
        return std::any_of(errors.begin(), errors.end(), helio_error_object_has_exact_backend_auth_error);
    }
    return helio_error_object_has_exact_backend_auth_error(errors);
}

HelioRetryKind helio_classify_graphql_response(unsigned status, const nlohmann::json& response)
{
    if (status != 200 || !response.is_object() || !response.contains("errors")) {
        return HelioRetryKind::None;
    }

    if (!helio_has_graphql_errors(response)) {
        return HelioRetryKind::Transient;
    }

    if (helio_has_exact_backend_auth_error(response)) {
        return HelioRetryKind::BackendAuth;
    }

    return helio_graphql_errors_have_transient_message(response["errors"]) ? HelioRetryKind::Transient :
                                                                             HelioRetryKind::None;
}

HelioRetryKind helio_classify_graphql_response(unsigned status, const std::string& body)
{
    if (helio_is_terminal_http_status(status) || status != 200) {
        return HelioRetryKind::None;
    }

    try {
        return helio_classify_graphql_response(status, nlohmann::json::parse(body));
    } catch (...) {
        return HelioRetryKind::None;
    }
}

HelioRetryKind helio_classify_retry(unsigned status,
                                    const std::string& body,
                                    const std::string& transport_error)
{
    if (helio_is_terminal_http_status(status)) {
        return HelioRetryKind::None;
    }
    if (status == 200) {
        return helio_classify_graphql_response(status, body);
    }
    if (helio_is_transient_http_status(status)) {
        return HelioRetryKind::Transient;
    }

    // A completed non-transient HTTP status remains terminal even if its body
    // happens to contain words such as "temporary" or the nested auth text.
    (void) body;
    (void) transport_error;
    return HelioRetryKind::None;
}

const char* helio_retry_kind_name(HelioRetryKind kind) noexcept
{
    switch (kind) {
    case HelioRetryKind::None:        return "none";
    case HelioRetryKind::Transient:   return "transient";
    case HelioRetryKind::BackendAuth: return "backend-auth";
    }
    return "unknown";
}

HelioRetryController::HelioRetryController(unsigned max_consecutive_backend_auth_retries) noexcept
    : m_max_consecutive_backend_auth_retries(max_consecutive_backend_auth_retries)
{}

bool HelioRetryController::should_retry(HelioRetryKind kind) noexcept
{
    if (kind == HelioRetryKind::BackendAuth) {
        ++m_consecutive_backend_auth_failures;
        return m_consecutive_backend_auth_failures <= m_max_consecutive_backend_auth_retries;
    }

    m_consecutive_backend_auth_failures = 0;
    return kind == HelioRetryKind::Transient;
}

void HelioRetryController::reset() noexcept
{
    m_consecutive_backend_auth_failures = 0;
}

unsigned HelioRetryController::consecutive_backend_auth_failures() const noexcept
{
    return m_consecutive_backend_auth_failures;
}

unsigned HelioRetryController::max_consecutive_backend_auth_retries() const noexcept
{
    return m_max_consecutive_backend_auth_retries;
}

} // namespace Slic3r
