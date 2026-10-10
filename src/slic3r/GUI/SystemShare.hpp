#ifndef slic3r_GUI_SystemShare_hpp_
#define slic3r_GUI_SystemShare_hpp_

#include <wx/string.h>

class wxWindow;

namespace Slic3r { namespace GUI {

// Cross-platform "share this URL" entry point. Renders a small action menu
// (modeled on macOS Mail's link context menu) with: Open in browser, Copy
// link, and -- when available -- "Share via System..." which drills into
// the OS-native share UI:
//   * macOS:   NSSharingServicePicker (Messages, Mail, AirDrop, Notes,
//              installed share extensions like Slack/Telegram, etc.)
//   * Windows: DataTransferManager (system share flyout)
//   * Linux:   no native picker; the "Share via System..." item is omitted.
//
// `anchor` is the wx widget the share UI should appear relative to (the
// button or area that triggered the share). The cross-platform action menu
// pops up at the cursor; the OS-native picker (when invoked from "Share via
// System...") anchors to `anchor`.
//
// `url` is the URL to share. It should be a public, shareable URL --
// callers must translate any internal/embedded URLs to their public-web
// form before invoking this (see WebViewPanel::TranslateMakerWorldEmbeddedUrl).
//
// `title` is a human-readable title used by the OS share menu when the
// target supports it (e.g. Messages will show the title alongside the
// link, Mail can prefill the subject). Pass an empty string when no title
// is available.
namespace SystemShare {

void ShareUrl(wxWindow *anchor, const wxString &url, const wxString &title);

// Platform shim: opens the OS-native share UI directly (no pre-menu).
// Implemented per-platform; on Linux this is a no-op. Called by ShareUrl
// when the user picks "Share via System..." from the cross-platform menu.
// Public so the menu code in SystemShare.cpp can invoke it; not intended
// for callers outside the SystemShare unit.
void OpenNativePicker(wxWindow *anchor, const wxString &url, const wxString &title);

// Returns true if OpenNativePicker is meaningful on this platform.
bool HasNativePicker();

}

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_SystemShare_hpp_
