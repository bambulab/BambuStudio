#ifndef slic3r_GUI_TextureImportPopupDismiss_hpp_
#define slic3r_GUI_TextureImportPopupDismiss_hpp_

class wxWindow;

namespace Slic3r {
namespace GUI {

using TextureImportOutsideClickCallback = void (*)(void *);

// Watch system-wide mouse-down events and invoke callback only when the click
// is outside both the popup and its modal owner. The owner exclusion keeps all
// in-dialog gestures on wxPopupTransientWindow's native dismissal path.
void *install_texture_import_external_click_monitor(wxWindow *popup,
                                                    wxWindow *owner,
                                                    TextureImportOutsideClickCallback callback,
                                                    void *context);
void  request_texture_import_external_click_monitor_dismiss(void *monitor);
void  uninstall_texture_import_external_click_monitor(void *monitor);

} // namespace GUI
} // namespace Slic3r

#endif
