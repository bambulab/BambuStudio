#ifndef slic3r_GUI_PopupWindow_hpp_
#define slic3r_GUI_PopupWindow_hpp_

#include <wx/popupwin.h>
#include <wx/event.h>
#include <wx/weakref.h>

class PopupWindow : public wxPopupTransientWindow
{
public:
    PopupWindow() {}

    ~PopupWindow();

    PopupWindow(wxWindow *parent, int style = wxBORDER_NONE) { Create(parent, style); }

    bool Create(wxWindow *parent, int flags = wxBORDER_NONE);
#ifdef __WXMSW__
    void BindUnfocusEvent();
#endif
#ifdef __WIN32__
    WXLRESULT MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam) override;
#endif
#ifdef __WXOSX__
protected:
    /**
     * \brief Make \p win the window this popup reports as hovered, with the leave/enter pair.
     *
     * While a popup holds the mouse capture, cocoa routes every mouse event in the application to
     * it (see nonownedwnd.mm, WX_filterSendEvent), native enter/leave included -- so the controls
     * inside it never see one and the popup has to synthesize them from the motion events it does
     * get. Exposed because a cascade of popups has to hand the hover between its levels.
     */
    void SetHovered(wxWindow *win);

    /** \brief Drop the hover: the pointer is no longer over anything this popup owns. */
    void ClearHovered();
#endif

private:
#ifdef __WXOSX__
    void OnMouseEvent2(wxMouseEvent &evt);
    // Weak: a popup that is refilled in place destroys the row this points at (DestroyChildren()),
    // and the next motion event would otherwise send a leave event to freed memory.
    wxWeakRef<wxWindow> hovered { this };
#endif

#ifdef __WXGTK__
    void topWindowActivate(wxActivateEvent &event);
#endif

#ifdef __WXMSW__
    void topWindowActivate(wxActivateEvent &event);
    void topWindowIconize(wxIconizeEvent &event);
    void topWindowShow(wxShowEvent &event);
#endif
};

#endif // !slic3r_GUI_PopupWindow_hpp_
