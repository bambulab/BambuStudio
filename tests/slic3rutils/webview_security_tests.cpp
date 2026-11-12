#include <catch2/catch.hpp>

#include "slic3r/GUI/WebViewSecurity.hpp"

#include <boost/property_tree/json_parser.hpp>

#include <sstream>

namespace {

boost::property_tree::ptree ParseJson(const std::string &payload)
{
    std::stringstream stream(payload);
    boost::property_tree::ptree root;
    boost::property_tree::read_json(stream, root);
    return root;
}

} // namespace

TEST_CASE("Remote web commands are limited by source", "[WebView][Security]")
{
    using namespace Slic3r::GUI;

    REQUIRE(WebViewSecurity::IsAllowedCommand(WebRequestSource::Internal, "homepage_logout"));

    REQUIRE(WebViewSecurity::IsAllowedCommand(WebRequestSource::MakerLab, "common_openurl"));
    REQUIRE(WebViewSecurity::IsAllowedCommand(WebRequestSource::MakerLab, "homepage_makerlab_open_3mf_binary"));
    REQUIRE_FALSE(WebViewSecurity::IsAllowedCommand(WebRequestSource::MakerLab, "homepage_logout"));

    REQUIRE(WebViewSecurity::IsAllowedCommand(WebRequestSource::MakerWorld, "homepage_leftmenu_switch"));
    REQUIRE(WebViewSecurity::IsAllowedCommand(WebRequestSource::MakerWorld, "get_web_shortcut"));
    REQUIRE_FALSE(WebViewSecurity::IsAllowedCommand(WebRequestSource::MakerWorld, "homepage_leftmenu_change_width"));
    REQUIRE_FALSE(WebViewSecurity::IsAllowedCommand(WebRequestSource::MakerWorld, ""));

    REQUIRE(WebViewSecurity::IsAllowedCommand(WebRequestSource::PrintHistory, "common_openurl"));
    REQUIRE_FALSE(WebViewSecurity::IsAllowedCommand(WebRequestSource::PrintHistory, "homepage_logout"));
    REQUIRE_FALSE(WebViewSecurity::IsAllowedCommand(WebRequestSource::PrintHistory, "open_project"));
    REQUIRE_FALSE(WebViewSecurity::IsAllowedCommand(WebRequestSource::PrintHistory, "homepage_leftmenu_clicked"));

    REQUIRE(WebViewSecurity::IsAllowedCommand(WebRequestSource::DisconnectPage, "homepage_leftmenu_clicked"));
    REQUIRE_FALSE(WebViewSecurity::IsAllowedCommand(WebRequestSource::DisconnectPage, "homepage_logout"));
    REQUIRE_FALSE(WebViewSecurity::IsAllowedCommand(WebRequestSource::DisconnectPage, "common_openurl"));
}

TEST_CASE("WebView kinds resolve to the intended trust level", "[WebView][Security]")
{
    using namespace Slic3r::GUI;

    REQUIRE(WebViewSecurity::SourceForWebViewKind(WebViewKind::Internal) ==
            std::optional<WebRequestSource>{WebRequestSource::Internal});
    REQUIRE(WebViewSecurity::SourceForWebViewKind(WebViewKind::MakerWorld) ==
            std::optional<WebRequestSource>{WebRequestSource::MakerWorld});
    REQUIRE(WebViewSecurity::SourceForWebViewKind(WebViewKind::PrintHistory) ==
            std::optional<WebRequestSource>{WebRequestSource::PrintHistory});
    REQUIRE_FALSE(WebViewSecurity::SourceForWebViewKind(WebViewKind::Unknown));

    REQUIRE_FALSE(WebViewSecurity::RequiresTrustedHttpsOrigin(WebRequestSource::Internal));
    REQUIRE_FALSE(WebViewSecurity::RequiresTrustedHttpsOrigin(WebRequestSource::DisconnectPage));
    REQUIRE(WebViewSecurity::RequiresTrustedHttpsOrigin(WebRequestSource::MakerWorld));
    REQUIRE(WebViewSecurity::RequiresTrustedHttpsOrigin(WebRequestSource::PrintHistory));
}

TEST_CASE("Command authorization only uses one unique top-level command", "[WebView][Security]")
{
    using namespace Slic3r::GUI;

    const auto nested_bypass = ParseJson(
        R"({"probe":{"command":"common_openurl"},"command":"homepage_leftmenu_change_width","width":500})");
    const auto rejected = WebViewSecurity::AuthorizeWebCommand(nested_bypass, WebRequestSource::MakerWorld);
    REQUIRE_FALSE(rejected.accepted);
    REQUIRE(rejected.command == "homepage_leftmenu_change_width");
    REQUIRE(std::string(rejected.reason) == "unauthorized_command");

    const auto nested_decoy = ParseJson(
        R"({"probe":{"command":"homepage_logout"},"command":"common_openurl","url":"https://example.test"})");
    const auto accepted = WebViewSecurity::AuthorizeWebCommand(nested_decoy, WebRequestSource::MakerWorld);
    REQUIRE(accepted.accepted);
    REQUIRE(accepted.command == "common_openurl");

    const auto duplicate = ParseJson(
        R"({"command":"common_openurl","command":"homepage_leftmenu_switch","menu":"home"})");
    REQUIRE(std::string(WebViewSecurity::AuthorizeWebCommand(duplicate, WebRequestSource::MakerWorld).reason) ==
            "duplicate_command");

    const auto nested_only = ParseJson(R"({"probe":{"command":"common_openurl"}})");
    REQUIRE(std::string(WebViewSecurity::AuthorizeWebCommand(nested_only, WebRequestSource::MakerWorld).reason) ==
            "missing_command");

    const auto object_command = ParseJson(R"({"command":{"value":"common_openurl"}})");
    REQUIRE(std::string(WebViewSecurity::AuthorizeWebCommand(object_command, WebRequestSource::MakerWorld).reason) ==
            "invalid_command_type");

    const auto array_command = ParseJson(R"({"command":["common_openurl"]})");
    REQUIRE(std::string(WebViewSecurity::AuthorizeWebCommand(array_command, WebRequestSource::MakerWorld).reason) ==
            "invalid_command_type");
}

TEST_CASE("Remote web commands require the exact trusted HTTPS origin", "[WebView][Security]")
{
    using Slic3r::GUI::WebViewSecurity::IsTrustedHttpsOrigin;

    const wxString trusted = "https://trusted.example/";

    REQUIRE(IsTrustedHttpsOrigin("https://trusted.example/models/1?from=test#details", trusted));
    REQUIRE(IsTrustedHttpsOrigin("https://TRUSTED.EXAMPLE:443/path", trusted));

    REQUIRE_FALSE(IsTrustedHttpsOrigin("http://trusted.example/path", trusted));
    REQUIRE_FALSE(IsTrustedHttpsOrigin("https://trusted.example:444/path", trusted));
    REQUIRE_FALSE(IsTrustedHttpsOrigin("https://sub.trusted.example/path", trusted));
    REQUIRE_FALSE(IsTrustedHttpsOrigin("https://www.trusted.example/path", trusted));
    REQUIRE_FALSE(IsTrustedHttpsOrigin("https://trusted.example.evil.test/path", trusted));
    REQUIRE_FALSE(IsTrustedHttpsOrigin("https://trusted.example@evil.test/path", trusted));
    REQUIRE_FALSE(IsTrustedHttpsOrigin("file:///resources/web/homepage3/disconnect.html", trusted));
    REQUIRE_FALSE(IsTrustedHttpsOrigin("about:blank", trusted));
    REQUIRE_FALSE(IsTrustedHttpsOrigin("", trusted));
}

TEST_CASE("Local pages are recognized by their exact normalized path", "[WebView][Security]")
{
    using Slic3r::GUI::WebViewSecurity::IsLocalDisconnectPage;
    using Slic3r::GUI::WebViewSecurity::IsResourcesFileUrl;

    const std::string root = "F:/projects/bamboo_slicer/resources";

    REQUIRE(IsLocalDisconnectPage(
        "file:///F:/projects/bamboo_slicer/resources/web/homepage3/disconnect.html?menu=printhistory&lang=en", root));
    REQUIRE(IsLocalDisconnectPage(
        "file://F:/projects/bamboo_slicer/web/../resources/web/homepage3/disconnect.html", root));
    REQUIRE_FALSE(IsLocalDisconnectPage(
        "file:///F:/projects/bamboo_slicer/resources/web/homepage3/disconnect.html.evil", root));
    REQUIRE_FALSE(IsLocalDisconnectPage(
        "file:///F:/projects/bamboo_slicer/resources/web/homepage3/extra/disconnect.html", root));
    REQUIRE_FALSE(IsLocalDisconnectPage(
        "https://makerworld.com/web/homepage3/disconnect.html", root));

    REQUIRE(IsResourcesFileUrl(
        "file:///F:/projects/bamboo_slicer/resources/web/homepage3/home.html?lang=en", root));
    REQUIRE_FALSE(IsResourcesFileUrl(
        "file:///F:/projects/bamboo_slicer/resources_backup/web/homepage3/home.html", root));
    REQUIRE_FALSE(IsResourcesFileUrl(
        "file:///F:/projects/bamboo_slicer/resources/../secret.html", root));
    REQUIRE_FALSE(IsResourcesFileUrl("https://makerworld.com/", root));
    REQUIRE_FALSE(IsResourcesFileUrl("file://evil.test/F:/projects/bamboo_slicer/resources/web/home.html", root));
}
