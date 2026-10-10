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

// wxWindow re-declares wxEvtHandler::ProcessEventLocally as protected, and the static type is what
// the access check looks at. Local dispatch is what we want: a synthesized enter/leave must reach
// the row and stop there, not carry on up to the popup's parent.
static void SendLocally(wxWindow *win, wxEvent &evt)
{
    static_cast<wxEvtHandler *>(win)->ProcessEventLocally(evt);
}

static wxWindow * HitTest(wxWindow * parent, wxMouseEvent &evt)
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

void PopupWindow::SetHovered(wxWindow *win)
{
    // A reference that went null means the row was destroyed under us -- a refilled popup does
    // that -- so there is nothing left to send a leave event to; carry on from the popup itself.
    wxWindow *const previous = hovered ? hovered.get() : this;
    if (previous == win) return;

    wxMouseEvent leave(wxEVT_LEAVE_WINDOW);
    leave.SetEventObject(previous);
    leave.SetId(previous->GetId());
    SendLocally(previous, leave);

    hovered = win;

    wxMouseEvent enter(wxEVT_ENTER_WINDOW);
    enter.SetEventObject(win);
    enter.SetId(win->GetId());
    SendLocally(win, enter);
}

void PopupWindow::ClearHovered() { SetHovered(this); }

void PopupWindow::OnMouseEvent2(wxMouseEvent &evt)
{
    auto *child = ::HitTest(this, evt);
    if (evt.GetEventType() == wxEVT_MOTION)
        SetHovered(child ? child : this);
    if (child) {
        SendLocally(child, evt);
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

#ifdef __WIN32__
// Windows delivers WM_ACTIVATEAPP to every top-level window of the thread when
// the app loses foreground, including a transient popup that wx neither
// activated nor re-raised wxEVT_ACTIVATE_APP for. Handling the raw message on
// the popup's own HWND is the reliable dismiss path the wx events miss.
WXLRESULT PopupWindow::MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam)
{
    if (nMsg == WM_ACTIVATEAPP && wParam == FALSE && IsShown())
        DismissAndNotify();
    return wxPopupTransientWindow::MSWWindowProc(nMsg, wParam, lParam);
}
#endif
