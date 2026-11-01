#include "WebViewTraceLogger.hpp"

#include <boost/log/trivial.hpp>

#include <regex>
#include <string>

namespace Slic3r { namespace GUI { namespace WebViewTraceLogger {

namespace {

// Keeps a value from breaking the key=value framing of the log line.
wxString escape_value(const wxString &value)
{
    wxString v = value;
    v.Replace("\r", " ");
    v.Replace("\n", " ");
    if (v.empty())
        return wxString("\"\"");
    if (v.find(' ') == wxString::npos && v.find('"') == wxString::npos)
        return v;
    v.Replace("\"", "'");
    return "\"" + v + "\"";
}

const char *const kSensitiveKeys = "dev_id|devId|dev_ip|devIp|ip|mac|tray_uuid|trayUuid|tag_uid|tagUid"
                                   "|uuid|sn|serial|serial_no|token|access_code|accessCode|accesscode"
                                   "|password|passwd|pwd|authorization|auth|secret|api_key|apiKey";

/**
 * @brief Redacts sensitive substrings of @p text in place.
 *
 * @return false when a regex failed, in which case @p text is left in an
 *         undefined state and the caller must drop it rather than log it.
 */
bool redact(std::string &text)
{
    try {
        static const std::regex re_keyed(std::string("(") + kSensitiveKeys + R"()("?\s*[:=]\s*"?)([^"',;&\s}\]]+))",
                                         std::regex::ECMAScript | std::regex::icase | std::regex::optimize);
        static const std::regex re_mac(R"(\b([0-9A-Fa-f]{2})(:[0-9A-Fa-f]{2}){4}:([0-9A-Fa-f]{2})\b)",
                                       std::regex::ECMAScript | std::regex::optimize);
        // Safety net for identifiers that are not behind a recognised key.
        static const std::regex re_long_hex(R"(\b[0-9A-Fa-f]{16,}\b)",
                                            std::regex::ECMAScript | std::regex::optimize);
        static const std::regex re_ipv4(
            R"(\b(?:(?:25[0-5]|2[0-4][0-9]|1?[0-9]{1,2})\.){3}(?:25[0-5]|2[0-4][0-9]|1?[0-9]{1,2})\b)",
            std::regex::ECMAScript | std::regex::optimize);
        static const std::regex re_windows_user_path(
            R"(([A-Za-z]:[\\/](?:Users|Documents and Settings)[\\/])[^\\/\s]+)",
            std::regex::ECMAScript | std::regex::icase | std::regex::optimize);
        static const std::regex re_unix_user_path(
            R"((/(?:Users|home)/)[^/\s]+)",
            std::regex::ECMAScript | std::regex::optimize);
        static const std::regex re_url_user_info(
            R"(([A-Za-z][A-Za-z0-9+.-]*://)[^/@\s]+@)",
            std::regex::ECMAScript | std::regex::optimize);

        text = std::regex_replace(text, re_url_user_info, "$1***@");
        text = std::regex_replace(text, re_keyed, "$1$2***");
        text = std::regex_replace(text, re_mac, "$1:**:**:**:**:$3");
        text = std::regex_replace(text, re_long_hex, "***");
        text = std::regex_replace(text, re_ipv4, "***.***.***.***");
        text = std::regex_replace(text, re_windows_user_path, "$1***");
        text = std::regex_replace(text, re_unix_user_path, "$1***");
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

} // namespace

static const char *StageName(Stage stage)
{
    switch (stage) {
    case Stage::L0_BACKEND: return "L0_BACKEND";
    case Stage::L1_RESOURCE: return "L1_RESOURCE";
    case Stage::L2_NAVIGATION: return "L2_NAVIGATION";
    case Stage::L3_PROCESS: return "L3_PROCESS";
    case Stage::L4_READY: return "L4_READY";
    case Stage::L5_RUNTIME: return "L5_RUNTIME";
    default: return "UNKNOWN";
    }
}

Fields &Fields::Add(const char *key, const wxString &value)
{
    return Append(wxString::Format("%s=%s", key, escape_value(value)));
}

Fields &Fields::Add(const char *key, const char *value)
{
    return Add(key, wxString::FromUTF8(value ? value : ""));
}

Fields &Fields::Add(const char *key, int value)
{
    return Append(wxString::Format("%s=%d", key, value));
}

Fields &Fields::Add(const char *key, long long value)
{
    return Append(wxString::Format("%s=%lld", key, value));
}

Fields &Fields::Append(const wxString &field)
{
    if (!m_buf.empty())
        m_buf += " ";
    m_buf += field;
    return *this;
}

void Emit(Stage stage, const wxString &view, const char *event, const Fields &fields, Severity severity)
{
    const wxString view_name = view.empty() ? wxString("unnamed") : view;
    if (severity == Severity::Auto)
        severity = stage == Stage::L0_BACKEND || stage == Stage::L1_RESOURCE ||
                   stage == Stage::L3_PROCESS ? Severity::Error : Severity::Info;

    wxString line = wxString::Format("[WebView] stage=%s view=%s event=%s", StageName(stage),
                                     escape_value(view_name), event ? event : "unknown");
    if (!fields.empty())
        line += " " + fields.str();

    const std::string utf8 = std::string(line.ToUTF8().data());
    switch (severity) {
    case Severity::Error: BOOST_LOG_TRIVIAL(error) << utf8; break;
    case Severity::Warning: BOOST_LOG_TRIVIAL(warning) << utf8; break;
    default: BOOST_LOG_TRIVIAL(info) << utf8; break;
    }
}

wxString SanitizeUrl(const wxString &url)
{
    if (url.empty())
        return wxString();

    wxString out = url;
    const size_t cut = out.find_first_of("?#");
    if (cut != wxString::npos)
        out = out.Left(cut);
    if (out.StartsWith("file://")) {
        const wxString filename = out.AfterLast('/');
        out = "file:///***/" + filename;
    }

    // data: and blob: URLs can be megabytes long and carry no useful location.
    constexpr size_t kMaxUrlLen = 512;
    if (out.length() > kMaxUrlLen)
        out = out.Left(kMaxUrlLen) + "...";
    return SanitizeText(out, kMaxUrlLen);
}

wxString SanitizeText(const wxString &text, size_t max_len)
{
    if (text.empty())
        return wxString();

    std::string utf8(text.ToUTF8().data());
    // Clamp before the regexes so a pathological payload cannot make logging
    // itself a performance problem.
    if (utf8.size() > max_len) {
        const size_t dropped = utf8.size() - max_len;
        utf8.resize(max_len);
        utf8 += "...<+" + std::to_string(dropped) + " bytes>";
    }

    if (!redact(utf8))
        return wxString("<redaction failed, payload dropped>");

    return wxString::FromUTF8(utf8.c_str());
}

}}} // namespace Slic3r::GUI::WebViewTraceLogger
