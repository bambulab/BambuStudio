#import <Cocoa/Cocoa.h>

namespace Slic3r { namespace GUI {

// Turn on the native macOS window shadow for the borderless top-level window that hosts
// `nsview` (wxWindow::GetHandle() returns the NSView on wxOSX). Called each time the window
// is shown or moved so the shadow re-derives from the current shaped/resized content.
void mac_enable_window_shadow(void *nsview)
{
    if (nsview == nullptr) return;
    NSView   *view = (__bridge NSView *) nsview;
    NSWindow *win  = [view window];
    if (win == nil) return;
    [win setHasShadow:YES];
    [win invalidateShadow]; // recompute from the current content alpha (rounded silhouette)
}

}} // namespace Slic3r::GUI
