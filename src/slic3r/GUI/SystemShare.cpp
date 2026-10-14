#include "SystemShare.hpp"

#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/menu.h>
#include <wx/utils.h>
#include <wx/window.h>

#include "I18N.hpp"
#include "wxExtensions.hpp"

namespace Slic3r { namespace GUI { namespace SystemShare {

// Cross-platform action menu for sharing a URL. Three items:
//   * Open Link  -- launches the user's default browser
//   * Copy Link  -- writes the URL to the clipboard
//   * Share...   -- drills into the OS-native share UI when available
//                   (NSSharingServicePicker on macOS, DataTransferManager
//                   on Windows). Omitted on platforms without a native
//                   share surface (e.g. Linux).
//
// Icons are macOS Mail-style: globe for Open Link, doc-on-doc for Copy
// Link, square-with-up-arrow for Share. Loaded via the codebase's
// append_menu_item helper which handles bitmap scaling and dark-mode
// recoloring.
void ShareUrl(wxWindow *anchor, const wxString &url, const wxString &title)
{
    if (url.IsEmpty()) return;

    wxMenu *menu = new wxMenu();
    const int id_open   = wxNewId();
    const int id_copy   = wxNewId();
    const int id_native = wxNewId();

    append_menu_item(menu, id_open, _L("Open Link"), wxEmptyString,
        [url](wxCommandEvent &) { wxLaunchDefaultBrowser(url); },
        "share_open_link", nullptr, []() { return true; }, anchor);

    append_menu_item(menu, id_copy, _L("Copy Link"), wxEmptyString,
        [url](wxCommandEvent &) {
            if (wxTheClipboard->Open()) {
                wxTheClipboard->SetData(new wxTextDataObject(url));
                wxTheClipboard->Close();
            }
        },
        "share_copy_link", nullptr, []() { return true; }, anchor);

    if (HasNativePicker()) {
        menu->AppendSeparator();
        append_menu_item(menu, id_native, _L("Share..."), wxEmptyString,
            [anchor, url, title](wxCommandEvent &) {
                OpenNativePicker(anchor, url, title);
            },
            "share_native", nullptr, []() { return true; }, anchor);
    }

    if (anchor)
        anchor->PopupMenu(menu);

    delete menu;
}

}}} // namespace Slic3r::GUI::SystemShare
