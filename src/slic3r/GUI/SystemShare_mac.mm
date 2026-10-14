#include "SystemShare.hpp"

#ifdef __APPLE__

#import <Cocoa/Cocoa.h>
#import <AppKit/NSSharingService.h>

#include <wx/window.h>

namespace Slic3r { namespace GUI { namespace SystemShare {

bool HasNativePicker() { return true; }

// macOS share menu via NSSharingServicePicker. The picker shows everything
// the user has configured: Messages, Mail, AirDrop, Notes, Reminders, Safari
// Reading List, plus any installed share extensions (Things, Drafts, Slack,
// Telegram, etc.) -- the same menu users get from Safari's share button or
// the Finder share menu.
//
// We pass two items:
//   1. NSURL  -- the canonical share payload. Most targets pick this up.
//   2. NSString (the title) -- some targets (Mail subject, Messages preview)
//      use this when present.
//
// The picker anchors to the wx widget that triggered the share so the
// popover arrow points at the right control.
void OpenNativePicker(wxWindow *anchor, const wxString &url, const wxString &title)
{
    if (url.IsEmpty()) return;

    NSString *ns_url = [NSString stringWithUTF8String:url.utf8_str().data()];
    NSURL    *url_obj = [NSURL URLWithString:ns_url];
    if (!url_obj) return;

    NSMutableArray *items = [NSMutableArray arrayWithObject:url_obj];
    if (!title.IsEmpty()) {
        NSString *ns_title = [NSString stringWithUTF8String:title.utf8_str().data()];
        if (ns_title) [items addObject:ns_title];
    }

    // BambuStudio's Objective-C++ files compile under MRR (no -fobjc-arc),
    // so [[NSSharingServicePicker alloc] init...] returns a +1 retained
    // object that the caller owns. The system retains the picker for the
    // popover lifetime, but we still need to release our own reference or
    // we leak one picker per share invocation. -autorelease defers the
    // release to the next autorelease pool drain, which is after the
    // popover's internal refcount goes to zero.
    NSSharingServicePicker *picker = [[[NSSharingServicePicker alloc] initWithItems:items] autorelease];

    NSView    *anchor_view = nil;
    NSRect     anchor_rect = NSZeroRect;
    NSRectEdge edge        = NSMinYEdge;
    if (anchor && anchor->GetHandle()) {
        anchor_view = (NSView *) anchor->GetHandle();
        anchor_rect = [anchor_view bounds];
    }

    if (anchor_view) {
        [picker showRelativeToRect:anchor_rect ofView:anchor_view preferredEdge:edge];
    } else {
        // Fall back to anchoring on the key window's content view so the
        // picker still appears even if the wx widget wasn't backed by a
        // realized NSView at trigger time.
        NSWindow *key = [NSApp keyWindow];
        NSView   *content = key ? [key contentView] : nil;
        if (content) {
            [picker showRelativeToRect:[content bounds] ofView:content preferredEdge:edge];
        }
    }
}

}}} // namespace Slic3r::GUI::SystemShare

#endif // __APPLE__
