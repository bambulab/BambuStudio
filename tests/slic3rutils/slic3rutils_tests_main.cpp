#include <catch_main.hpp>

#include "slic3r/Utils/Http.hpp"
#include "slic3r/GUI/DeepLink.hpp"

using Slic3r::GUI::RemoteModelAction;
using Slic3r::GUI::is_studio_deep_link_candidate;
using Slic3r::GUI::parse_studio_deep_link;

TEST_CASE("Bambu Studio canonical deep links select their requested action", "[DeepLink]")
{
    const auto open = parse_studio_deep_link(
        "bambustudio://open?file=https%3A%2F%2Fexample.com%2Fproject.3mf");
    REQUIRE(open.has_value());
    CHECK(open->action == RemoteModelAction::OpenProject);
    CHECK(open->encoded_download_info == "https%3A%2F%2Fexample.com%2Fproject.3mf");
    CHECK_FALSE(open->legacy_macos_scheme);

    const auto import = parse_studio_deep_link(
        "bambustudio://import?file=https%3A%2F%2Fexample.com%2Fmodel.3mf&name=model.3mf");
    REQUIRE(import.has_value());
    CHECK(import->action == RemoteModelAction::ImportModel);
    CHECK(import->encoded_download_info == "https%3A%2F%2Fexample.com%2Fmodel.3mf&name=model.3mf");
    CHECK_FALSE(import->legacy_macos_scheme);
}

TEST_CASE("Legacy macOS deep links remain project-open requests", "[DeepLink]")
{
    const auto link = parse_studio_deep_link(
        "bambustudioopen://https%3A%2F%2Fexample.com%2Fproject.3mf");
    REQUIRE(link.has_value());
    CHECK(link->action == RemoteModelAction::OpenProject);
    CHECK(link->encoded_download_info == "https%3A%2F%2Fexample.com%2Fproject.3mf");
    CHECK(link->legacy_macos_scheme);
}

TEST_CASE("Unsupported or incomplete deep links are not forwarded", "[DeepLink]")
{
    CHECK(is_studio_deep_link_candidate("bambustudio://import"));
    CHECK(is_studio_deep_link_candidate("bambustudio://open?file="));
    CHECK_FALSE(parse_studio_deep_link("bambustudio://import"));
    CHECK_FALSE(parse_studio_deep_link("bambustudio://import?file="));
    CHECK_FALSE(parse_studio_deep_link("bambustudio://unknown?file=https%3A%2F%2Fexample.com%2Fmodel.3mf"));
    CHECK_FALSE(parse_studio_deep_link("bambustudio://import?profile=fast"));
    CHECK_FALSE(parse_studio_deep_link("bambustudio://import??file=https%3A%2F%2Fexample.com%2Fmodel.3mf"));
    CHECK_FALSE(parse_studio_deep_link("/tmp/model.3mf"));
    CHECK_FALSE(parse_studio_deep_link("https://example.com/model.3mf"));
    CHECK_FALSE(is_studio_deep_link_candidate("bambustudio://unknown?file=x"));
}

TEST_CASE("Check SSL certificates paths", "[Http][NotWorking]") {
    
    Slic3r::Http g = Slic3r::Http::get("https://github.com/");
    
    unsigned status = 0;
    g.on_error([&status](std::string, std::string, unsigned http_status) {
        status = http_status;
    });
    
    g.on_complete([&status](std::string /* body */, unsigned http_status){
        status = http_status;
    });
    
    g.perform_sync();
    
    REQUIRE(status == 200);
}

TEST_CASE("Http digest authentication", "[Http][NotWorking]") {
    Slic3r::Http g = Slic3r::Http::get("https://jigsaw.w3.org/HTTP/Digest/");

    g.auth_digest("guest", "guest");

    unsigned status = 0;
    g.on_error([&status](std::string, std::string, unsigned http_status) {
        status = http_status;
    });

    g.on_complete([&status](std::string /* body */, unsigned http_status){
        status = http_status;
    });

    g.perform_sync();

    REQUIRE(status == 200);
}

TEST_CASE("Http basic authentication", "[Http][NotWorking]") {
    Slic3r::Http g = Slic3r::Http::get("https://jigsaw.w3.org/HTTP/Basic/");

    g.auth_basic("guest", "guest");

    unsigned status = 0;
    g.on_error([&status](std::string, std::string, unsigned http_status) {
        status = http_status;
    });

    g.on_complete([&status](std::string /* body */, unsigned http_status){
        status = http_status;
    });

    g.perform_sync();

    REQUIRE(status == 200);
}
