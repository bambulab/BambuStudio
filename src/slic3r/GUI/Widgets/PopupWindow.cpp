#include "PopupWindow.hpp"
#include <wx/app.h>

static wxWindow *GetTopParent(wxWindow *pWindow)
{
    wxWindow *pWin = pWindow;
    if (!pWin)
        return nullptr;
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
    // GTK popups are separate native windows and can outlive their visible
    // anchor when the app loses focus or a modeless helper is hidden/resized.
    m_owner = GetTopParent(parent);
    if (m_owner) {
        m_owner->Bind(wxEVT_ICONIZE, &PopupWindow::topWindowIconize, this);
        m_owner->Bind(wxEVT_SHOW, &PopupWindow::topWindowShow, this);
        m_owner->Bind(wxEVT_MOVE, &PopupWindow::topWindowMove, this);
        m_owner->Bind(wxEVT_SIZE, &PopupWindow::topWindowSize, this);
    }
    wxTheApp->Bind(wxEVT_ACTIVATE_APP, &PopupWindow::appActivate, this);
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
    if (wxTheApp)
        wxTheApp->Unbind(wxEVT_ACTIVATE_APP, &PopupWindow::appActivate, this);
    if (m_owner) {
        m_owner->Unbind(wxEVT_ICONIZE, &PopupWindow::topWindowIconize, this);
        m_owner->Unbind(wxEVT_SHOW, &PopupWindow::topWindowShow, this);
        m_owner->Unbind(wxEVT_MOVE, &PopupWindow::topWindowMove, this);
        m_owner->Unbind(wxEVT_SIZE, &PopupWindow::topWindowSize, this);
    }
#endif
#ifdef __WXMSW__
    GetTopParent(this)->Unbind(wxEVT_ACTIVATE, &PopupWindow::topWindowActivate, this);
    GetTopParent(this)->Unbind(wxEVT_ICONIZE, &PopupWindow::topWindowIconize, this);
    GetTopParent(this)->Unbind(wxEVT_SHOW, &PopupWindow::topWindowShow, this);
#endif
}

void PopupWindow::DismissOnOwnerChange()
{
    if (IsShown())
        DismissAndNotify();
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
void PopupWindow::appActivate(wxActivateEvent &event)
{
    event.Skip();
    if (!event.GetActive())
        DismissOnOwnerChange();
}

void PopupWindow::topWindowIconize(wxIconizeEvent &event)
{
    event.Skip();
    if (event.IsIconized())
        DismissOnOwnerChange();
}

void PopupWindow::topWindowShow(wxShowEvent &event)
{
    event.Skip();
    if (!event.IsShown())
        DismissOnOwnerChange();
}

void PopupWindow::topWindowMove(wxMoveEvent &event)
{
    event.Skip();
    DismissOnOwnerChange();
}

void PopupWindow::topWindowSize(wxSizeEvent &event)
{
    event.Skip();
    DismissOnOwnerChange();
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
    event.Skip();
    if (!event.GetActive())
        DismissOnOwnerChange();
}

void PopupWindow::topWindowIconize(wxIconizeEvent &event)
{
    event.Skip();
    if (event.IsIconized())
        DismissOnOwnerChange();
}

void PopupWindow::topWindowShow(wxShowEvent &event)
{
    event.Skip();
    if (!event.IsShown())
        DismissOnOwnerChange();
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
        DismissOnOwnerChange();
    return wxPopupTransientWindow::MSWWindowProc(nMsg, wParam, lParam);
}
#endif
