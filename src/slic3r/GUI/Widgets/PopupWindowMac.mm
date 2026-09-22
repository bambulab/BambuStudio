#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>

#include "PopupWindowMac.hpp"

namespace Slic3r { namespace GUI {

// When the user clicks a window, the window server keeps a deferred "raise this window"
// request for that click. AppKit confirms it (SLSDoDeferredOrdering) while routing the
// mouse-down, before the clicked view receives mouseDown:, and the window server carries
// the raise out when the click completes on mouse-up. On macOS 27 the app's own window
// ordering is applied through a later transaction, so a wxPopupTransientWindow that
// dismisses itself inside mouseDown: (orderOut: right after the confirmed raise) is raised
// again by the window server on mouse-up while AppKit still reports it as hidden. Its
// content view is already hidden by wxWidgets, so what stays on screen is a blank panel in
// the window's background colour, and wxWidgets never orders it out again because it
// believes the window is hidden.
//
// The window server only does this for the window that received the click, so after such a
// dismissal the popup re-asserts its order-out once the click has finished: on the next run
// loop turn after the matching mouse-up has been delivered, or after a timeout if no mouse-up
// arrives. A repeated orderOut: on an already hidden window is forwarded to the window server.
// Each dismissal of a window is one "operation". The window's current operation number is kept
// on the NSWindow itself; a callback from an older operation must not touch the window's
// ordering, alpha or mouse handling, because a newer dismissal (or a show) owns them now.
static const void *kPopupOperationKey = &kPopupOperationKey;

static NSUInteger popup_next_operation(NSWindow *window)
{
    NSUInteger op = [objc_getAssociatedObject(window, kPopupOperationKey) unsignedIntegerValue] + 1;
    objc_setAssociatedObject(window, kPopupOperationKey, @(op), OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    return op;
}

static bool popup_operation_is_current(NSWindow *window, NSUInteger op)
{
    return [objc_getAssociatedObject(window, kPopupOperationKey) unsignedIntegerValue] == op;
}

void mac_reassert_popup_hidden_after_click(void *view)
{
    if (@available(macOS 27.0, *)) {
        NSEvent *event = [NSApp currentEvent];
        if (!event)
            return;
        NSEventType type = event.type;
        if (type != NSEventTypeLeftMouseDown && type != NSEventTypeRightMouseDown && type != NSEventTypeOtherMouseDown)
            return;
        NSWindow *window = [(NSView *) view window];
        if (!window || event.window != window)
            return;
        const NSInteger  button    = event.buttonNumber;
        const NSUInteger operation = popup_next_operation(window);

        // The transient raise shows the panel with its content view already hidden, i.e. as a
        // blank rectangle in the background colour. Making the panel fully transparent and
        // click-through in the same transaction as the order-out keeps that interval invisible;
        // mac_prepare_popup_show() and the re-assertion below undo it.
        [window setAlphaValue:0.0];
        [window setIgnoresMouseEvents:YES];

        [window retain];
        __block id   monitor = nil;
        __block BOOL done    = NO;
        __block void (^fallback)(void) = nil;
        void (^reassert)(void) = ^{
            if (done)
                return;
            done = YES;
            if (fallback) {
                Block_release(fallback);
                fallback = nil;
            }
            if (monitor) {
                [NSEvent removeMonitor:monitor];
                monitor = nil;
            }
            if (popup_operation_is_current(window, operation)) {
                if (![window isVisible])
                    [window orderOut:nil];
                [window setAlphaValue:1.0];
                [window setIgnoresMouseEvents:NO];
            }
            [window release];
        };
        // Only the release of the button that dismissed the popup completes the click; a release
        // of another button while it is still held must not end the operation early.
        NSEventMask up_mask = NSEventMaskLeftMouseUp | NSEventMaskRightMouseUp | NSEventMaskOtherMouseUp;
        monitor = [NSEvent addLocalMonitorForEventsMatchingMask:up_mask handler:^NSEvent *(NSEvent *up) {
            if (up.buttonNumber == button) {
                // Let AppKit finish routing the mouse-up first; the window server has already
                // applied the deferred raise by the time the event reaches the application.
                dispatch_async(dispatch_get_main_queue(), reassert);
            }
            return up;
        }];
        // Safety net in case the mouse-up is never delivered to this application. It must not
        // run while the button is still held: the window server raises the panel only on the
        // release, and re-asserting before that would leave the panel raised afterwards.
        fallback = Block_copy(^{
            if (done)
                return;
            if ([NSEvent pressedMouseButtons] & (1u << button)) {
                dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC / 2), dispatch_get_main_queue(), fallback);
                return;
            }
            reassert();
        });
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC / 2), dispatch_get_main_queue(), fallback);
    }
}

void mac_prepare_popup_show(void *view)
{
    if (@available(macOS 27.0, *)) {
        NSWindow *window = [(NSView *) view window];
        if (!window)
            return;
        // Showing supersedes any pending dismissal callback for this window.
        popup_next_operation(window);
        [window setAlphaValue:1.0];
        [window setIgnoresMouseEvents:NO];
    }
}

}} // namespace Slic3r::GUI
