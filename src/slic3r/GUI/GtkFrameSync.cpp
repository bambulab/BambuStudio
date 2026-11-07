#include "GtkFrameSync.hpp"

#ifdef __WXGTK3__
#include <gtk/gtk.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif
#endif

namespace Slic3r { namespace GUI {

#ifdef __WXGTK3__
// After painting a frame, GTK's X11 backend freezes the window's frame clock
// until the compositor answers with _NET_WM_FRAME_DRAWN. Under XWayland the
// compositor (seen with mutter) sometimes never answers, e.g. after a modal
// dialog and a minimize/restore. The window then stops repainting for good,
// while still taking input: blank page area, tab bar one step behind. Paint on
// GTK's own clock instead by turning frame sync off for every toplevel.
static gboolean disable_frame_sync_on_map(GSignalInvocationHint *, guint n_params, const GValue *params, gpointer)
{
#ifdef GDK_WINDOWING_X11
    if (n_params > 0 && G_VALUE_HOLDS_OBJECT(&params[0])) {
        GObject *obj = G_OBJECT(g_value_get_object(&params[0]));
        if (obj && GTK_IS_WINDOW(obj)) {
            GdkWindow *gdk_window = gtk_widget_get_window(GTK_WIDGET(obj));
            if (gdk_window && GDK_IS_X11_WINDOW(gdk_window))
                gdk_x11_window_set_frame_sync_enabled(gdk_window, FALSE);
        }
    }
#endif
    return TRUE; // keep the hook installed
}
#endif

void disable_x11_frame_sync()
{
#ifdef __WXGTK3__
    // "map" runs after "realize", so the GdkWindow exists by the time the hook fires.
    g_signal_add_emission_hook(g_signal_lookup("map", GTK_TYPE_WIDGET), 0, disable_frame_sync_on_map, nullptr, nullptr);
#endif
}

}} // namespace Slic3r::GUI
