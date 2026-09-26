#ifndef slic3r_GUI_TextureImportPopupDismiss_hpp_
#define slic3r_GUI_TextureImportPopupDismiss_hpp_

class wxWindow;

namespace Slic3r {
namespace GUI {

using TextureImportOutsideClickCallback = void (*)(void *);

// Watch mouse-down events anywhere on screen and invoke callback when the
// click is outside popup's screen rect. Returns an opaque handle; pass it to
// uninstall_texture_import_outside_click_monitor().
void *install_texture_import_outside_click_monitor(wxWindow *popup,
                                                   TextureImportOutsideClickCallback callback,
                                                   void *context);
void  uninstall_texture_import_outside_click_monitor(void *monitor);

} // namespace GUI
} // namespace Slic3r

#endif
