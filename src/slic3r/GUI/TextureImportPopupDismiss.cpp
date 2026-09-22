#include "TextureImportPopupDismiss.hpp"

#ifndef __APPLE__

#include <wx/window.h>

#ifdef _WIN32
#include <wx/msw/wrapwin.h>
#endif

namespace Slic3r {
namespace GUI {

namespace {

struct MonitorState {
    wxWindow *                             popup    = nullptr;
    TextureImportOutsideClickCallback      callback = nullptr;
    void *                                 context  = nullptr;
#ifdef _WIN32
    HHOOK hook = nullptr;
#endif
};

MonitorState *g_active = nullptr;

#ifdef _WIN32
bool point_in_popup(wxWindow *popup, POINT pt)
{
    if (!popup || popup->IsBeingDeleted())
        return false;
    const wxRect rc = popup->GetScreenRect();
    return rc.Contains(pt.x, pt.y);
}

LRESULT CALLBACK mouse_hook(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && g_active && g_active->popup && g_active->callback) {
        switch (wParam) {
        case WM_LBUTTONDOWN:
        case WM_NCLBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_NCRBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_NCMBUTTONDOWN: {
            const auto *info = reinterpret_cast<MOUSEHOOKSTRUCT *>(lParam);
            if (info && !point_in_popup(g_active->popup, info->pt)) {
                // Queue on the popup itself: wxEvtHandler drops its own pending
                // events when it is destroyed, so a popup torn down before this
                // runs cannot be dereferenced.
                wxWindow * popup = g_active->popup;
                const auto cb    = g_active->callback;
                void *     ctx   = g_active->context;
                popup->CallAfter([cb, ctx]() {
                    if (cb)
                        cb(ctx);
                });
            }
            break;
        }
        default:
            break;
        }
    }
    return CallNextHookEx(g_active ? g_active->hook : nullptr, nCode, wParam, lParam);
}
#endif

} // namespace

void *install_texture_import_outside_click_monitor(wxWindow *popup,
                                                   TextureImportOutsideClickCallback callback,
                                                   void *context)
{
    auto *state     = new MonitorState;
    state->popup    = popup;
    state->callback = callback;
    state->context  = context;
#ifdef _WIN32
    // Only one popup is watched at a time; a stale routing target would silently
    // swallow the previous popup's dismiss.
    wxASSERT_MSG(g_active == nullptr, "texture import outside-click monitor already installed");
    g_active = state;
    // Thread hook, not WH_MOUSE_LL: this dialog runs clustering on the UI thread and
    // Windows silently drops a low-level hook whose owner blocks past
    // LowLevelHooksTimeout. It also matches the macOS side, which only watches events
    // of this application; clicks landing in another app are handled by
    // wxEVT_ACTIVATE_APP instead.
    state->hook = SetWindowsHookEx(WH_MOUSE, mouse_hook, nullptr, GetCurrentThreadId());
#endif
    return state;
}

void uninstall_texture_import_outside_click_monitor(void *monitor)
{
    auto *state = static_cast<MonitorState *>(monitor);
    if (!state)
        return;
#ifdef _WIN32
    if (state->hook)
        UnhookWindowsHookEx(state->hook);
    if (g_active == state)
        g_active = nullptr;
#endif
    delete state;
}

} // namespace GUI
} // namespace Slic3r

#endif // !__APPLE__
