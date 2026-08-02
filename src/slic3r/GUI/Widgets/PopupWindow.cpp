#include "PopupWindow.hpp"

static wxWindow *GetTopParent(wxWindow *pWindow)
{
    wxWindow *pWin = pWindow;
    while (pWin->GetParent()) {
        pWin = pWin->GetParent();
        if (auto top = dynamic_cast<wxNonOwnedWindow*>(pWin))
            return top;
    }
    return pWin;
}

bool PopupWindow::Create(wxWindow *parent, int style)
{
    if (!wxPopupTransientWindow::Create(parent, style))
        return false;
#ifdef __WXGTK__
    GetTopParent(parent)->Bind(wxEVT_ACTIVATE, &PopupWindow::topWindowActivate, this);
#endif
#ifdef __WXOSX__
    if (style & wxPU_CONTAINS_CONTROLS)
    for (auto evt : {wxEVT_LEFT_DOWN, wxEVT_LEFT_UP, wxEVT_LEFT_DCLICK, wxEVT_MOTION, wxEVT_MOUSEWHEEL})
        Bind(evt, &PopupWindow::OnMouseEvent2, this);
#endif
    return true;
}

PopupWindow::~PopupWindow()
{
#ifdef __WXOSX__
    removeFocusGuard();
#endif
#ifdef __WXGTK__
    GetTopParent(this)->Unbind(wxEVT_ACTIVATE, &PopupWindow::topWindowActivate, this);
#endif
#ifdef __WXMSW__
    GetTopParent(this)->Unbind(wxEVT_ACTIVATE, &PopupWindow::topWindowActivate, this);
    GetTopParent(this)->Unbind(wxEVT_ICONIZE, &PopupWindow::topWindowIconize, this);
    GetTopParent(this)->Unbind(wxEVT_SHOW, &PopupWindow::topWindowShow, this);
#endif
}

#ifdef __WXOSX__

// Any process holding accessibility permission can raise our windows via the
// AXRaise action. The raised window becomes key, an open popup resigns key, and
// wxPopupFocusHandler dismisses it. When the raise happens on mouse-down, the
// popup is gone before the click is delivered: the click is lost, and the popup's
// NSWindow is left on screen unpainted at NSPopUpMenuWindowLevel.
//
// A raise is not the user dismissing the popup, so swallow the kill-focus while
// the pointer is still inside the popup. wxFocusEvent::GetWindow() cannot be used
// to detect this - it is null for a key-window change, which is not a
// first-responder change.
//
// Other dismissal paths are unaffected: clicking outside goes through
// wxPopupWindowHandler (EVT_LEFT_DOWN on m_child), and leaving the application
// through its own deactivation path.
class PopupWindow::SameAppFocusGuard : public wxEvtHandler
{
public:
    explicit SameAppFocusGuard(PopupWindow *popup) : m_popup(popup)
    {
        Bind(wxEVT_KILL_FOCUS, &SameAppFocusGuard::OnKillFocus, this);
    }

private:
    void OnKillFocus(wxFocusEvent &event)
    {
        // Pointer still over the popup, so this was not the user dismissing it:
        // stop here so wxPopupFocusHandler behind us never runs.
        if (m_popup->GetScreenRect().Contains(wxGetMousePosition()))
            return;
        event.Skip();
    }

    PopupWindow *m_popup;
};

void PopupWindow::Popup(wxWindow *focus)
{
    wxPopupTransientWindow::Popup(focus);
    // Guard m_focus, not the focus argument: on macOS the base class reassigns
    // m_focus = FindFocus() before pushing wxPopupFocusHandler onto it, and the
    // two are different windows. Pushing after the base call puts us ahead of
    // wxPopupFocusHandler in the handler chain.
    if (m_focus_guard == nullptr && m_focus != nullptr) {
        m_focus_guard    = new SameAppFocusGuard(this);
        m_guarded_window = m_focus;
        m_focus->PushEventHandler(m_focus_guard);
    }
}

void PopupWindow::Dismiss()
{
    removeFocusGuard();
    wxPopupTransientWindow::Dismiss();
}

void PopupWindow::removeFocusGuard()
{
    if (m_focus_guard) {
        if (m_guarded_window)
            m_guarded_window->RemoveEventHandler(m_focus_guard);
        delete m_focus_guard;
        m_focus_guard = nullptr;
    }
    m_guarded_window = nullptr;
}

static wxEvtHandler * HitTest(wxWindow * parent, wxMouseEvent &evt)
{
    auto pt = evt.GetPosition();
    const wxWindowList &children = parent->GetChildren();
    for (auto w : children) {
        if (!w->IsShown()) continue;
        wxRect rc { w->GetPosition(), w->GetSize() };
        if (rc.Contains(pt)) {
            evt.SetPosition(pt - rc.GetTopLeft());
            if (auto child = HitTest(w, evt))
                return child;
            return w;
        }
    }
    return nullptr;
}

void PopupWindow::OnMouseEvent2(wxMouseEvent &evt)
{
    auto child = ::HitTest(this, evt);
    if (evt.GetEventType() == wxEVT_MOTION) {
        auto h = child ? child : this;
        if (hovered != h) {
            wxMouseEvent leave(wxEVT_LEAVE_WINDOW);
            leave.SetEventObject(hovered);
            leave.SetId(static_cast<wxWindow*>(hovered)->GetId());
            hovered->ProcessEventLocally(leave);
            hovered = h;
            wxMouseEvent enter(wxEVT_ENTER_WINDOW);
            enter.SetEventObject(hovered);
            enter.SetId(static_cast<wxWindow*>(hovered)->GetId());
            hovered->ProcessEventLocally(enter);
        }
    }
    if (child) {
        child->ProcessEventLocally(evt);
    } else {
        evt.Skip();
    }
}

#endif

#ifdef __WXGTK__
void PopupWindow::topWindowActivate(wxActivateEvent &event)
{
    event.Skip();
}
#endif

#ifdef __WXMSW__
void PopupWindow::BindUnfocusEvent()
{
    GetTopParent(this)->Bind(wxEVT_ACTIVATE, &PopupWindow::topWindowActivate, this);
    GetTopParent(this)->Bind(wxEVT_ICONIZE, &PopupWindow::topWindowIconize, this);
    GetTopParent(this)->Bind(wxEVT_SHOW, &PopupWindow::topWindowShow, this);
}

void PopupWindow::topWindowActivate(wxActivateEvent &event)
{
    if (!event.GetActive())
        Dismiss();
}

void PopupWindow::topWindowIconize(wxIconizeEvent &event)
{
    event.Skip();
    if (event.IsIconized())
        Dismiss();
}

void PopupWindow::topWindowShow(wxShowEvent &event)
{
    event.Skip();
    if (!event.IsShown())
        Dismiss();
}
#endif
