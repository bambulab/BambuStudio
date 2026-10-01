#include <catch2/catch.hpp>

#include "slic3r/Utils/HelioRetryPolicy.hpp"

#include "nlohmann/json.hpp"

namespace {

std::string graphql_errors(std::initializer_list<std::string> messages)
{
    nlohmann::json errors = nlohmann::json::array();
    for (const std::string& message : messages) {
        errors.push_back({{"message", message}});
    }
    return nlohmann::json({{"errors", std::move(errors)}}).dump();
}

} // namespace

TEST_CASE("Helio retry policy narrowly recognizes nested backend auth failures", "[Helio][RetryPolicy]")
{
    using namespace Slic3r;

    const std::string exact_response = graphql_errors({HELIO_BACKEND_AUTH_401_MESSAGE});
    REQUIRE(helio_classify_graphql_response(200, exact_response) == HelioRetryKind::BackendAuth);
    REQUIRE(helio_classify_retry(200, exact_response) == HelioRetryKind::BackendAuth);

    const nlohmann::json parsed = nlohmann::json::parse(exact_response);
    REQUIRE(helio_has_exact_backend_auth_error(parsed));
    REQUIRE(helio_classify_graphql_response(200, parsed) == HelioRetryKind::BackendAuth);

    SECTION("near matches remain terminal")
    {
        REQUIRE(helio_classify_retry(200, graphql_errors({
            "Authentication failed: Auth service returned status 401."
        })) == HelioRetryKind::None);
        REQUIRE(helio_classify_retry(200, graphql_errors({
            "authentication failed: auth service returned status 401"
        })) == HelioRetryKind::None);
        REQUIRE(helio_classify_retry(200, graphql_errors({
            "Authentication failed: Auth service returned status 401 "
        })) == HelioRetryKind::None);
    }

    SECTION("multiple ordinary GraphQL errors do not become backend auth")
    {
        const std::string response = graphql_errors({
            "Authentication failed",
            "Auth service returned status 401",
            "Material is unsupported"
        });
        REQUIRE(helio_classify_retry(200, response) == HelioRetryKind::None);
    }

    SECTION("malformed sibling errors do not hide an exact backend auth error")
    {
        const nlohmann::json response = {
            {"errors", nlohmann::json::array({
                {{"message", 401}},
                nlohmann::json::object(),
                {{"message", HELIO_BACKEND_AUTH_401_MESSAGE}}
            })}
        };
        REQUIRE(helio_classify_retry(200, response.dump()) == HelioRetryKind::BackendAuth);
    }
}

TEST_CASE("Helio retry policy classifies ordinary transient failures", "[Helio][RetryPolicy]")
{
    using namespace Slic3r;

    REQUIRE(helio_classify_retry(200, "not-json") == HelioRetryKind::None);
    REQUIRE(helio_classify_retry(200, graphql_errors({"Service temporarily unavailable"})) ==
            HelioRetryKind::Transient);
    REQUIRE(helio_classify_retry(200, graphql_errors({"Request deadline exceeded"})) ==
            HelioRetryKind::Transient);
    REQUIRE(helio_classify_retry(200, graphql_errors({"Invalid printer ID"})) == HelioRetryKind::None);

    SECTION("malformed GraphQL errors members are transient")
    {
        REQUIRE(helio_classify_retry(200, R"({"errors":[]})") == HelioRetryKind::Transient);
        REQUIRE(helio_classify_retry(200, R"({"errors":null})") == HelioRetryKind::Transient);
        REQUIRE(helio_classify_retry(200, R"({"errors":[{}]})") == HelioRetryKind::Transient);
    }

    for (const unsigned status : {0u, 408u, 425u, 429u, 500u, 502u, 503u, 599u}) {
        INFO("HTTP status " << status);
        REQUIRE(helio_classify_retry(status) == HelioRetryKind::Transient);
        REQUIRE(helio_is_transient_http_status(status));
    }
}

TEST_CASE("Helio retry policy recognizes only the reported subgraph deserialization error",
          "[Helio][RetryPolicy]")
{
    using namespace Slic3r;

    REQUIRE(helio_classify_retry(200, graphql_errors({"Failed to deserialize subgraph response"})) ==
            HelioRetryKind::Transient);
    REQUIRE(helio_classify_retry(200, graphql_errors({"FAILED TO DESERIALIZE SUBGRAPH RESPONSE"})) ==
            HelioRetryKind::Transient);

    for (const std::string& message : {
             "Failed to deserialize subgraph response.",
             "Failed to deserialize subgraph response from materials",
             "Failed to deserialize response",
             "Material is unsupported"
         }) {
        INFO(message);
        REQUIRE(helio_classify_retry(200, graphql_errors({message})) == HelioRetryKind::None);
    }
}

TEST_CASE("Actual HTTP authentication failures are terminal", "[Helio][RetryPolicy]")
{
    using namespace Slic3r;

    const std::string exact_response = graphql_errors({HELIO_BACKEND_AUTH_401_MESSAGE});
    for (const unsigned status : {401u, 403u, 404u}) {
        INFO("HTTP status " << status);
        REQUIRE(helio_is_terminal_http_status(status));
        REQUIRE(helio_classify_graphql_response(status, exact_response) == HelioRetryKind::None);
        REQUIRE(helio_classify_retry(status, exact_response, HELIO_BACKEND_AUTH_401_MESSAGE) ==
                HelioRetryKind::None);
    }
}

TEST_CASE("Helio retry controller permits only one consecutive backend auth retry", "[Helio][RetryPolicy]")
{
    using namespace Slic3r;

    HelioRetryController controller;
    REQUIRE(controller.consecutive_backend_auth_failures() == 0);

    REQUIRE(controller.should_retry(HelioRetryKind::BackendAuth));
    REQUIRE(controller.consecutive_backend_auth_failures() == 1);

    REQUIRE_FALSE(controller.should_retry(HelioRetryKind::BackendAuth));
    REQUIRE(controller.consecutive_backend_auth_failures() == 2);
}

TEST_CASE("Helio retry controller resets the backend auth streak", "[Helio][RetryPolicy]")
{
    using namespace Slic3r;

    SECTION("a recovered response starts a fresh streak")
    {
        HelioRetryController controller;
        REQUIRE(controller.should_retry(HelioRetryKind::BackendAuth));
        REQUIRE_FALSE(controller.should_retry(HelioRetryKind::None));
        REQUIRE(controller.consecutive_backend_auth_failures() == 0);
        REQUIRE(controller.should_retry(HelioRetryKind::BackendAuth));
    }

    SECTION("an ordinary transient failure breaks the backend auth streak")
    {
        HelioRetryController controller;
        REQUIRE(controller.should_retry(HelioRetryKind::BackendAuth));
        REQUIRE(controller.should_retry(HelioRetryKind::Transient));
        REQUIRE(controller.consecutive_backend_auth_failures() == 0);
        REQUIRE(controller.should_retry(HelioRetryKind::BackendAuth));
        REQUIRE_FALSE(controller.should_retry(HelioRetryKind::BackendAuth));
    }
}
