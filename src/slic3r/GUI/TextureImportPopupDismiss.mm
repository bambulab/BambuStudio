#include "TextureImportPopupDismiss.hpp"

#ifdef __APPLE__

#import <Cocoa/Cocoa.h>

#include <wx/utils.h>
#include <wx/window.h>

namespace Slic3r {
namespace GUI {

namespace {

struct MonitorState {
    NSTimer *                          timer    { nil };
    wxWindow *                         popup    { nullptr };
    TextureImportOutsideClickCallback  callback { nullptr };
    void *                             context  { nullptr };
    uint32_t                           down_count { 0 };
    bool                               pending    { false };
};

// System-wide count of mouse-down events. Unlike NSEvent monitors it also
// sees clicks that a modal session discards, menu-bar/Dock clicks, and clicks
// in other applications, and it never misses a click shorter than one poll.
uint32_t mouse_down_count()
{
    const CGEventSourceStateID src = kCGEventSourceStateCombinedSessionState;
    return CGEventSourceCounterForEventType(src, kCGEventLeftMouseDown) +
           CGEventSourceCounterForEventType(src, kCGEventRightMouseDown) +
           CGEventSourceCounterForEventType(src, kCGEventOtherMouseDown);
}

void poll(MonitorState *state)
{
    if (!state->popup || state->popup->IsBeingDeleted() || !state->callback)
        return;

    const uint32_t count = mouse_down_count();
    if (count != state->down_count) {
        state->down_count = count;
        if (!state->popup->GetScreenRect().Contains(wxGetMousePosition()))
            state->pending = true;
    }

    // Hiding an NSPanel while its mouse-down tracking session is open makes
    // the paired mouseUp order the window front again, so wait for release.
    if (!state->pending || [NSEvent pressedMouseButtons] != 0)
        return;
    state->pending = false;

    // Queue on the popup itself: wxEvtHandler drops its own pending events
    // when it is destroyed, so a popup torn down before this runs cannot be
    // dereferenced.
    wxWindow * popup = state->popup;
    const auto cb    = state->callback;
    void *     ctx   = state->context;
    popup->CallAfter([cb, ctx]() {
        if (cb)
            cb(ctx);
    });
}

} // namespace

void *install_texture_import_outside_click_monitor(wxWindow *popup,
                                                   TextureImportOutsideClickCallback callback,
                                                   void *context)
{
    auto *state     = new MonitorState;
    state->popup    = popup;
    state->callback = callback;
    state->context  = context;
    state->down_count = mouse_down_count();
    state->timer = [NSTimer timerWithTimeInterval:1.0 / 60.0
                                          repeats:YES
                                            block:^(NSTimer *) { poll(state); }];
    // Common modes: keep polling inside modal sessions and event tracking.
    [[NSRunLoop mainRunLoop] addTimer:state->timer forMode:NSRunLoopCommonModes];
    return state;
}

void uninstall_texture_import_outside_click_monitor(void *monitor)
{
    auto *state = static_cast<MonitorState *>(monitor);
    if (!state)
        return;
    if (state->timer)
        [state->timer invalidate];
    state->timer = nil;
    delete state;
}

} // namespace GUI
} // namespace Slic3r

#endif
