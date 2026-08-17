#ifndef slic3r_HelioRetryPolicy_hpp_
#define slic3r_HelioRetryPolicy_hpp_

#include <string>

#include "nlohmann/json_fwd.hpp"

namespace Slic3r {

enum class HelioRetryKind
{
    None,
    Transient,
    BackendAuth
};

inline constexpr const char HELIO_BACKEND_AUTH_401_MESSAGE[] =
    "Authentication failed: Auth service returned status 401";

bool helio_is_terminal_http_status(unsigned status) noexcept;
bool helio_is_transient_http_status(unsigned status) noexcept;
bool helio_is_transient_error_message(const std::string& message);

// GraphQL errors are usable only when at least one entry contains a string
// message. Empty, null, or structurally invalid errors members are malformed
// HTTP-200 responses and must fall through to transient handling.
bool helio_has_graphql_errors(const nlohmann::json& response);

// The backend-auth condition is deliberately narrow: it is recognized only
// when an HTTP 200 GraphQL response contains an errors[].message (or an
// errors.message object) equal to HELIO_BACKEND_AUTH_401_MESSAGE.
bool helio_has_exact_backend_auth_error(const nlohmann::json& response);

// Classifies a parsed HTTP 200 GraphQL response. A response without GraphQL
// errors has no retry classification. Unrecognized GraphQL errors are terminal
// unless one of their messages has an ordinary transient marker.
HelioRetryKind helio_classify_graphql_response(unsigned status, const nlohmann::json& response);

// Parses and classifies a GraphQL response body. Invalid JSON is terminal, while
// a parsed HTTP 200 response with a malformed errors member is transient. Actual
// HTTP authentication/authorization/not-found statuses are terminal.
HelioRetryKind helio_classify_graphql_response(unsigned status, const std::string& body);

// Classifies a completed HTTP or transport failure. Only status 0,
// 408/425/429, and 5xx responses are ordinary transients. HTTP 200 delegates
// to GraphQL classification. Other HTTP statuses are terminal.
HelioRetryKind helio_classify_retry(unsigned status,
                                    const std::string& body = std::string(),
                                    const std::string& transport_error = std::string());

const char* helio_retry_kind_name(HelioRetryKind kind) noexcept;

// Tracks the special one-retry policy for consecutive backend-auth failures.
// Call should_retry exactly once per response. Ordinary transient failures are
// retryable but break the backend-auth streak; total attempt limits remain the
// responsibility of the caller.
class HelioRetryController
{
public:
    explicit HelioRetryController(unsigned max_consecutive_backend_auth_retries = 1) noexcept;

    bool should_retry(HelioRetryKind kind) noexcept;
    void reset() noexcept;

    unsigned consecutive_backend_auth_failures() const noexcept;
    unsigned max_consecutive_backend_auth_retries() const noexcept;

private:
    unsigned m_consecutive_backend_auth_failures{0};
    unsigned m_max_consecutive_backend_auth_retries{1};
};

} // namespace Slic3r

#endif // slic3r_HelioRetryPolicy_hpp_
