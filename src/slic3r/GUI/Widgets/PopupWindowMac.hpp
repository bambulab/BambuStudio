#ifndef slic3r_GUI_PopupWindowMac_hpp_
#define slic3r_GUI_PopupWindowMac_hpp_

// macOS-specific helpers for PopupWindow. `view` is the popup's native content view as
// returned by wxWindow::GetHandle().

namespace Slic3r { namespace GUI {

// Call right after hiding a popup window: if the popup was dismissed while handling a mouse
// click on itself, order it out again once the click has completed, because on macOS 27 the
// window server raises the clicked window on mouse-up and would otherwise show it as a blank
// panel while AppKit reports it hidden.
void mac_reassert_popup_hidden_after_click(void *view);

// Call before showing a popup window: restores the visibility state that
// mac_reassert_popup_hidden_after_click() may still be holding.
void mac_prepare_popup_show(void *view);

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_PopupWindowMac_hpp_
