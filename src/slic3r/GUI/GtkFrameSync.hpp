#ifndef slic3r_GtkFrameSync_hpp_
#define slic3r_GtkFrameSync_hpp_

namespace Slic3r { namespace GUI {

// Turn off GTK's X11 frame synchronization (_NET_WM_FRAME_DRAWN) for every
// toplevel window mapped from now on. Call once at startup, before any window
// is shown. Implemented in its own translation unit so that <gdk/gdkx.h> and
// its Xlib macros (None, Bool, Status...) stay out of the rest of the GUI.
void disable_x11_frame_sync();

}} // namespace Slic3r::GUI

#endif // slic3r_GtkFrameSync_hpp_
