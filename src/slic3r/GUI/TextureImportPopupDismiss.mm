#include "TextureImportPopupDismiss.hpp"

#ifdef __APPLE__

#import <Cocoa/Cocoa.h>

#include <wx/utils.h>
#include <wx/window.h>

namespace Slic3r {
namespace GUI {

namespace {

struct MonitorState {
    NSTimer *                          timer { nil };
    wxWindow *                         popup { nullptr };
    wxWindow *                         owner { nullptr };
    TextureImportOutsideClickCallback  callback { nullptr };
    void *                             context { nullptr };
    uint32_t                           down_count { 0 };
    bool                               awaiting_release { false };
};

uint32_t mouse_down_count()
{
    const CGEventSourceStateID source = kCGEventSourceStateCombinedSessionState;
    return CGEventSourceCounterForEventType(source, kCGEventLeftMouseDown) +
           CGEventSourceCounterForEventType(source, kCGEventRightMouseDown) +
           CGEventSourceCounterForEventType(source, kCGEventOtherMouseDown);
}

void poll(MonitorState *state)
{
    if (!state->popup || state->popup->IsBeingDeleted() ||
        !state->owner || state->owner->IsBeingDeleted() || !state->callback)
        return;

    const uint32_t count = mouse_down_count();
    const bool buttons_pressed = [NSEvent pressedMouseButtons] != 0;

    if (!state->awaiting_release) {
        if (count == state->down_count)
            return;
        state->down_count = count;

        const wxPoint mouse = wxGetMousePosition();
        if (!state->popup->GetScreenRect().Contains(mouse) &&
            !state->owner->GetScreenRect().Contains(mouse))
            state->awaiting_release = true;
        return;
    }

    // Do not hide the NSPanel while Cocoa is still tracking the mouse-down.
    if (buttons_pressed)
        return;

    wxWindow *popup = state->popup;
    const auto callback = state->callback;
    void *context = state->context;
    state->callback = nullptr;
    popup->CallAfter([callback, context]() {
        if (callback)
            callback(context);
    });
}

} // namespace

void *install_texture_import_external_click_monitor(wxWindow *popup,
                                                    wxWindow *owner,
                                                    TextureImportOutsideClickCallback callback,
                                                    void *context)
{
    auto *state = new MonitorState;
    state->popup = popup;
    state->owner = owner;
    state->callback = callback;
    state->context = context;
    state->down_count = mouse_down_count();
    state->timer = [NSTimer timerWithTimeInterval:1.0 / 60.0
                                          repeats:YES
                                            block:^(NSTimer *) { poll(state); }];
    [[NSRunLoop mainRunLoop] addTimer:state->timer forMode:NSRunLoopCommonModes];
    return state;
}

void request_texture_import_external_click_monitor_dismiss(void *monitor)
{
    auto *state = static_cast<MonitorState *>(monitor);
    if (state && state->callback)
        state->awaiting_release = true;
}

void uninstall_texture_import_external_click_monitor(void *monitor)
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
