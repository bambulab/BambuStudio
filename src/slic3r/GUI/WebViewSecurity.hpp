#ifndef slic3r_WebViewSecurity_hpp_
#define slic3r_WebViewSecurity_hpp_

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <boost/property_tree/ptree.hpp>

#include <wx/string.h>
#include <wx/uri.h>

namespace Slic3r {
namespace GUI {

enum class WebRequestSource {
    Internal,
    MakerLab,
    MakerWorld,
    PrintHistory,
    DisconnectPage,
};

enum class WebViewKind {
    Internal,
    MakerLab,
    MakerWorld,
    PrintHistory,
    Unknown,
};

namespace WebViewSecurity {

inline std::optional<WebRequestSource> SourceForWebViewKind(WebViewKind kind)
{
    switch (kind) {
    case WebViewKind::Internal:     return WebRequestSource::Internal;
    case WebViewKind::MakerLab:     return WebRequestSource::MakerLab;
    case WebViewKind::MakerWorld:   return WebRequestSource::MakerWorld;
    case WebViewKind::PrintHistory: return WebRequestSource::PrintHistory;
    case WebViewKind::Unknown:      return std::nullopt;
    }
    return std::nullopt;
}

inline const char *SourceName(WebRequestSource source)
{
    switch (source) {
    case WebRequestSource::Internal:       return "internal";
    case WebRequestSource::MakerLab:       return "makerlab";
    case WebRequestSource::MakerWorld:     return "makerworld";
    case WebRequestSource::PrintHistory:   return "print_history";
    case WebRequestSource::DisconnectPage: return "disconnect_page";
    }
    return "unknown";
}

inline bool RequiresTrustedHttpsOrigin(WebRequestSource source)
{
    return source == WebRequestSource::MakerLab ||
           source == WebRequestSource::MakerWorld ||
           source == WebRequestSource::PrintHistory;
}

inline bool IsAllowedCommand(WebRequestSource source, std::string_view command)
{
    static constexpr std::array<std::string_view, 5> makerlab_allowed = {
        "homepage_makerlab_open_3mf_binary",
        "homepage_makerlab_stl_download",
        "common_openurl",
        "makerworld_model_open",
        "homepage_login_or_register",
    };
    // The remote print-history page is not in this repository. It currently uses the
    // same bridge commands as MakerWorld; rejection logs identify any command that needs
    // to be added after the web page is confirmed.
    static constexpr std::array<std::string_view, 5> makerworld_allowed = {
        "makerworld_model_open",
        "common_openurl",
        "homepage_leftmenu_switch",
        "get_web_shortcut",
        "homepage_login_or_register",
    };
    static constexpr std::array<std::string_view, 1> disconnect_page_allowed = {
        "homepage_leftmenu_clicked",
    };

    switch (source) {
    case WebRequestSource::Internal:
        return true;
    case WebRequestSource::MakerLab:
        return std::find(makerlab_allowed.begin(), makerlab_allowed.end(), command) != makerlab_allowed.end();
    case WebRequestSource::MakerWorld:
    case WebRequestSource::PrintHistory:
        return std::find(makerworld_allowed.begin(), makerworld_allowed.end(), command) != makerworld_allowed.end();
    case WebRequestSource::DisconnectPage:
        return std::find(disconnect_page_allowed.begin(), disconnect_page_allowed.end(), command) !=
               disconnect_page_allowed.end();
    }
    return false;
}

struct WebCommandAuthorization
{
    bool        accepted = false;
    std::string command;
    const char *reason = "rejected";
};

inline std::string CommandForLog(std::string_view command)
{
    constexpr size_t k_max_chars = 64;
    std::string      safe;
    safe.reserve(std::min(command.size(), k_max_chars));
    for (const char value : command) {
        if (safe.size() == k_max_chars)
            break;
        const unsigned char byte = static_cast<unsigned char>(value);
        safe.push_back(byte < 0x20 || byte == 0x7f ? '?' : value);
    }
    return safe;
}

inline WebCommandAuthorization AuthorizeWebCommand(const boost::property_tree::ptree &root,
                                                    WebRequestSource source)
{
    const boost::property_tree::ptree *command_node = nullptr;
    for (const auto &item : root) {
        if (item.first != "command")
            continue;
        if (command_node != nullptr)
            return {false, {}, "duplicate_command"};
        command_node = &item.second;
    }

    if (command_node == nullptr)
        return {false, {}, "missing_command"};
    if (!command_node->empty())
        return {false, {}, "invalid_command_type"};

    const auto command = command_node->get_value_optional<std::string>();
    if (!command)
        return {false, {}, "invalid_command_type"};
    if (!IsAllowedCommand(source, *command))
        return {false, *command, "unauthorized_command"};
    return {true, *command, nullptr};
}

inline wxString EffectivePort(const wxURI &uri)
{
    if (uri.HasPort())
        return uri.GetPort();
    if (uri.GetScheme().CmpNoCase("https") == 0)
        return "443";
    if (uri.GetScheme().CmpNoCase("http") == 0)
        return "80";
    return {};
}

inline bool IsTrustedHttpsOrigin(const wxString &page_url, const wxString &trusted_base_url)
{
    wxURI page;
    wxURI trusted;
    if (!page.Create(page_url) || !trusted.Create(trusted_base_url))
        return false;
    if (!page.HasScheme() || !page.HasServer() || !trusted.HasScheme() || !trusted.HasServer())
        return false;
    if (page.HasUserInfo() || trusted.HasUserInfo())
        return false;
    if (page.GetScheme().CmpNoCase("https") != 0 || trusted.GetScheme().CmpNoCase("https") != 0)
        return false;
    return page.GetServer().CmpNoCase(trusted.GetServer()) == 0 &&
           EffectivePort(page) == EffectivePort(trusted);
}

inline std::optional<std::string> NormalizeLocalPath(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    const bool               absolute = !path.empty() && path.front() == '/';
    std::vector<std::string> parts;
    std::string              token;
    auto consume = [&]() {
        if (token.empty() || token == ".")
            return true;
        if (token == "..") {
            if (parts.empty())
                return false;
            parts.pop_back();
            return true;
        }
        parts.push_back(token);
        return true;
    };
    for (const char value : path) {
        if (value == '/') {
            if (!consume())
                return std::nullopt;
            token.clear();
        } else {
            token.push_back(value);
        }
    }
    if (!consume())
        return std::nullopt;

    std::string normalized = absolute ? "/" : "";
    for (size_t index = 0; index < parts.size(); ++index) {
        if (index != 0)
            normalized.push_back('/');
        normalized += parts[index];
    }
    if (normalized.size() >= 3 && normalized[0] == '/' &&
        std::isalpha(static_cast<unsigned char>(normalized[1])) && normalized[2] == ':')
        normalized.erase(0, 1);
#ifdef _WIN32
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
#endif
    return normalized;
}

inline std::optional<std::string> LocalPathFromFileUrl(const wxString &page_url)
{
    wxURI uri;
    if (!uri.Create(page_url) || uri.HasUserInfo() || uri.GetScheme().CmpNoCase("file") != 0)
        return std::nullopt;

    const wxString server = uri.GetServer();
    if (!server.empty() &&
        !(server.length() == 2 && std::isalpha(static_cast<unsigned char>(server[0])) && server[1] == ':'))
        return std::nullopt;

    std::string path = std::string(wxURI::Unescape(uri.GetPath()).ToUTF8());
    if (!server.empty())
        path = std::string(server.ToUTF8()) + path;
    return NormalizeLocalPath(path);
}

inline bool IsResourcesFileUrl(const wxString &page_url, const std::string &resources_directory)
{
    const auto page_path = LocalPathFromFileUrl(page_url);
    const auto root_path = NormalizeLocalPath(resources_directory);
    if (!page_path || !root_path || root_path->empty())
        return false;
    if (*page_path == *root_path)
        return true;
    const std::string prefix = *root_path + "/";
    return page_path->compare(0, prefix.size(), prefix) == 0;
}

inline bool IsLocalDisconnectPage(const wxString &page_url, const std::string &resources_directory)
{
    const auto page_path = LocalPathFromFileUrl(page_url);
    const auto root_path = NormalizeLocalPath(resources_directory);
    if (!page_path || !root_path)
        return false;
    return *page_path == *root_path + "/web/homepage3/disconnect.html";
}

} // namespace WebViewSecurity
} // namespace GUI
} // namespace Slic3r

#endif
