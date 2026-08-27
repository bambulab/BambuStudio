#ifndef slic3r_GUI_WebViewTraceLogger_hpp_
#define slic3r_GUI_WebViewTraceLogger_hpp_

#include <cstddef>

#include <wx/string.h>

namespace Slic3r { namespace GUI { namespace WebViewTraceLogger {

/// Fault layer written as `stage=` on every WebView health log.
enum class Stage {
    L0_BACKEND,    // backend/runtime unavailable, fell back to FakeWebView
    L1_RESOURCE,   // local resource missing, user data path not writable
    L2_NAVIGATION, // navigation lifecycle and wxEVT_WEBVIEW_ERROR
    L3_PROCESS,    // browser/render process failure
    L4_READY,      // front-end ready handshake result
    L5_RUNTIME,    // JS errors, sub-resource failures, console dump
};

/// Auto uses error for L0/L1/L3 and info for the remaining stages.
enum class Severity { Auto, Info, Warning, Error };

/// Builds the escaped `key=value` suffix of a log line.
class Fields
{
public:
    Fields &Add(const char *key, const wxString &value);
    Fields &Add(const char *key, const char *value);
    Fields &Add(const char *key, int value);
    Fields &Add(const char *key, long long value);

    const wxString &str() const { return m_buf; }
    bool            empty() const { return m_buf.empty(); }

private:
    Fields &Append(const wxString &field);
    wxString m_buf;
};

/// Emits one `[WebView] stage=... view=... event=...` line.
void Emit(Stage           stage,
          const wxString &view,
          const char     *event,
          const Fields   &fields   = Fields(),
          Severity        severity = Severity::Auto);

/// Removes sensitive URL parts and returns a bounded location.
wxString SanitizeUrl(const wxString &url);

/// Redacts and bounds page-originated text.
wxString SanitizeText(const wxString &text, size_t max_len = 2048);

}}} // namespace Slic3r::GUI::WebViewTraceLogger

#endif // !slic3r_GUI_WebViewTraceLogger_hpp_
