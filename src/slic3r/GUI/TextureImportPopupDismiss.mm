#include "TextureImportPopupDismiss.hpp"

#ifdef __APPLE__

#import <Cocoa/Cocoa.h>

#include <wx/utils.h>
#include <wx/window.h>

namespace Slic3r {
namespace GUI {

namespace {

struct MonitorState {
    id                                 monitor  { nil };
    wxWindow *                         popup    { nullptr };
    TextureImportOutsideClickCallback  callback { nullptr };
    void *                             context  { nullptr };
};

} // namespace

void *install_texture_import_outside_click_monitor(wxWindow *popup,
                                                   TextureImportOutsideClickCallback callback,
                                                   void *context)
{
    auto *state     = new MonitorState;
    state->popup    = popup;
    state->callback = callback;
    state->context  = context;
    state->monitor  = [NSEvent addLocalMonitorForEventsMatchingMask:
        (NSEventMaskLeftMouseDown | NSEventMaskRightMouseDown | NSEventMaskOtherMouseDown)
        handler:^NSEvent *(NSEvent *event) {
            if (!state->popup || state->popup->IsBeingDeleted() || !state->callback)
                return event;
            const wxPoint pt = wxGetMousePosition();
            if (!state->popup->GetScreenRect().Contains(pt)) {
                // Queue on the popup itself: wxEvtHandler drops its own pending
                // events when it is destroyed, so a popup torn down before this
                // runs cannot be dereferenced.
                wxWindow * popup = state->popup;
                const auto cb    = state->callback;
                void *     ctx   = state->context;
                popup->CallAfter([cb, ctx]() {
                    if (cb)
                        cb(ctx);
                });
            }
            return event;
        }];
    return state;
}

void uninstall_texture_import_outside_click_monitor(void *monitor)
{
    auto *state = static_cast<MonitorState *>(monitor);
    if (!state)
        return;
    if (state->monitor)
        [NSEvent removeMonitor:state->monitor];
    state->monitor = nil;
    delete state;
}

} // namespace GUI
} // namespace Slic3r

#endif
