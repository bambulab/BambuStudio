#ifndef slic3r_Tabbook_hpp_
#define slic3r_Tabbook_hpp_

//#ifdef _WIN32

#include <wx/bookctrl.h>
#include <wx/sizer.h>
#include "wxExtensions.hpp"
#include "Widgets/StateColor.hpp"


class ScalableButton;
class TabButton;

// custom message the ButtonsListCtrl sends to its parent (Notebook) to notify a selection change:
wxDECLARE_EVENT(wxCUSTOMEVT_TABBOOK_SEL_CHANGED, wxCommandEvent);

class TabButtonsListCtrl : public wxScrolled<wxControl>
{
public:
    // BBS
    TabButtonsListCtrl(wxWindow *parent, wxBoxSizer *side_tools = NULL);
    ~TabButtonsListCtrl() {}

    void SetSelection(int sel);
    void showNewTag(int sel, bool show = false);
    void Rescale();
    /// An empty bmp_name will use the default arrow image instead
    bool InsertPage(size_t n, const wxString& text, const std::string& bmp_name, const wxString &tooltip = wxEmptyString);
    bool InsertPage(size_t n, const wxString& text, const ScalableBitmap &bmp, const wxString &tooltip = wxEmptyString);
    void RemovePage(size_t n);
    bool SetPageImage(size_t n, const std::string& bmp_name);
    bool SetPageText(size_t n, const wxString& strText);
    wxString GetPageText(size_t n) const;
    bool SetPageToolTip(size_t n, const wxString& tooltip);
    wxString GetPageToolTip(size_t n) const;
    /// Sets button padding around text label + icon.
    void SetPaddingSize(const wxSize& size);
    /// Gets current button padding for button at given index. All buttons have the same padding.
    const wxSize& GetPaddingSize(size_t n = 0) const;
    void SetButtonBGColors(const StateColor &stateColor);
    StateColor GetButtonBGColors() const { return m_btn_bg_colors; }
    void SetButtonBorderWidth(int width);
    int GetButtonBorderWidth() const { return m_btn_border_width; }
    void SetButtonBorderColors(const StateColor &stateColor);
    /// Sets minimum size for all buttons, which affects the overall width and height of this list control.
    /// Setting either dimension to `-1` means that dimension is unconstrained (as per wxWidgets convention).
    void SetButtonSize(const wxSize& size);
    wxSize GetButtonSize() const { return m_btn_size; }
    /// Sets padding at top of button list. Default is no padding.
    void SetListTopPadding(int padding);
    int Count() const { return (int)m_pageButtons.size(); }
    /// When `enable == true`, a vertical scrollbar will use existing client area instead of expanding the overall control width
    /// Disabled by default. Only relevant when the control is set to be scrollable, with `SetScrollRate()`.
    void SetUseClientAreaForScrollbar(bool enable = true);

private:
    wxSizer*                        m_buttons_sizer;
    ScalableBitmap                  m_arrow_img;
    wxSize                          m_btn_size;
    wxSize                          m_btn_padding { -1, -1 };  // use default TabButton padding unless explicitly set
    StateColor                      m_btn_bg_colors;
    StateColor                      m_btn_border_colors;  // use default TabButton colors unless explicitly set
    std::vector<TabButton*>         m_pageButtons;
    int                             m_selection {-1};
    int                             m_btn_border_width { 1 };
    bool                            m_scrollbar_in_client_area { false };  // see SetUseClientAreaForScrollbar()

    void update_client_size(bool layout = true);
};

class Tabbook: public wxBookCtrlBase
{
public:
    Tabbook(wxWindow *     parent,
                 wxWindowID winid = wxID_ANY,
                 const wxPoint & pos = wxDefaultPosition,
                 const wxSize & size = wxDefaultSize,
                // BBS
                 wxBoxSizer* side_tools = NULL,
                 long style = 0)
    {
        Init();
        Create(parent, winid, pos, size, side_tools, style);
    }

    bool Create(wxWindow * parent,
                wxWindowID winid = wxID_ANY,
                const wxPoint & pos = wxDefaultPosition,
                const wxSize & size = wxDefaultSize,
                // BBS
                wxBoxSizer* side_tools = NULL,
                long style = 0)
    {
        if (!wxBookCtrlBase::Create(parent, winid, pos, size, style))
            return false;

        m_bookctrl = new TabButtonsListCtrl(this, side_tools);

        wxSizer* mainSizer = new wxBoxSizer(IsVertical() ? wxVERTICAL : wxHORIZONTAL);

        if (style & wxBK_RIGHT || style & wxBK_BOTTOM)
            mainSizer->Add(0, 0, 1, wxEXPAND, 0);

        m_controlSizer = new wxBoxSizer(IsVertical() ? wxHORIZONTAL : wxVERTICAL);
        m_controlSizer->Add(m_bookctrl, wxSizerFlags(1).Expand());
        mainSizer->Add(m_controlSizer, wxSizerFlags(0).Expand().Border(wxALL, m_controlMargin));
        SetSizer(mainSizer);

        this->Bind(wxCUSTOMEVT_TABBOOK_SEL_CHANGED, [this](wxCommandEvent& evt)
        {
            if (int page_idx = evt.GetId(); page_idx >= 0)
                SetSelection(page_idx);
        });

        this->Bind(wxEVT_NAVIGATION_KEY, &Tabbook::OnNavigationKey, this);

        return true;
    }


    // Methods specific to this class.

    // A method allowing to add a new page without any label (which is unused
    // by this control) and show it immediately.
    bool ShowNewPage(wxWindow * page)
    {
        return AddPage(page, wxString(), ""/*true *//* select it */);
    }

    // Set effect to use for showing/hiding pages.
    void SetEffects(wxShowEffect showEffect, wxShowEffect hideEffect)
    {
        m_showEffect = showEffect;
        m_hideEffect = hideEffect;
    }

    // Or the same effect for both of them.
    void SetEffect(wxShowEffect effect)
    {
        SetEffects(effect, effect);
    }

    // And the same for time outs.
    void SetEffectsTimeouts(unsigned showTimeout, unsigned hideTimeout)
    {
        m_showTimeout = showTimeout;
        m_hideTimeout = hideTimeout;
    }

    void SetEffectTimeout(unsigned timeout)
    {
        SetEffectsTimeouts(timeout, timeout);
    }

    //// Page management

    /// Adds a new page to the control with a named icon.
    /// Note that an empty `bmp_name` will use a default arrow image instead.
    bool AddPage(wxWindow* page,
                 const wxString& text,
                 const std::string& bmp_name,
                 bool bSelect = false)
    {
        return DoInsertPage(GetPageCount(), page, text, bSelect, wxEmptyString, true, bmp_name);
    }

    /// Adds a new page to the control with a named icon and tooltip.
    /// Note that an empty `bmp_name` will use a default arrow image instead.
    bool AddPage(wxWindow* page,
                 const wxString& text,
                 const std::string& bmp_name,
                 const wxString& tooltip,
                 bool bSelect = false)
    {
        return DoInsertPage(GetPageCount(), page, text, bSelect, tooltip, true, bmp_name);
    }

    /// Adds a new page to the control w/out any image.
    /// Note that `wxImageList` images are not supported and `imageId` is ignored.
    bool AddPage(wxWindow* page,
                 const wxString& text,
                 bool bSelect = false,
                 int WXUNUSED(imageId) = NO_IMAGE) override
    {
        return DoInsertPage(GetPageCount(), page, text, bSelect);
    }

    /// Adds a new page to the control with tooltip but w/out any image.
    bool Add(wxWindow* page,
             const wxString& text,
             const wxString& tooltip,
             bool bSelect = false)
    {
        return DoInsertPage(GetPageCount(), page, text, bSelect, tooltip);
    }

    /// Specialized handler for existing legacy code relying on default icon. Do not use in new code.
    bool AddPage(wxWindow* page,
                 const wxString& text,
                 const char bmp_name[1],
                 bool bSelect = false)
    {
        return DoInsertPage(GetPageCount(), page, text, bSelect, wxEmptyString, true, "");
    }

    /// Insert a new page w/out any image.
    /// Note that `wxImageList` images are not supported and `imageId` is ignored.
    virtual bool InsertPage(size_t n,
                            wxWindow * page,
                            const wxString & text,
                            bool bSelect = false,
                            int WXUNUSED(imageId) = NO_IMAGE) override
    {
        return DoInsertPage(n, page, text, bSelect);
    }

    /// Note that an empty `bmp_name` will use a default arrow image instead.
    bool InsertPage(size_t n,
                    wxWindow * page,
                    const wxString & text,
                    const std::string & bmp_name,
                    bool bSelect = false)
    {
        return DoInsertPage(n, page, text, bSelect, wxEmptyString, true, bmp_name);
    }

    /// Note that an empty `bmp_name` will use a default arrow image instead.
    bool InsertPage(size_t n,
                    wxWindow * page,
                    const wxString & text,
                    const std::string & bmp_name,
                    const wxString & tooltip,
                    bool bSelect = false)
    {
        return DoInsertPage(n, page, text, bSelect, tooltip, true, bmp_name);
    }

    /// Insert a new page with tooltip but w/out any image.
    bool Insert(size_t n,
                wxWindow * page,
                const wxString & text,
                const wxString & tooltip,
                bool bSelect = false)
    {
        return DoInsertPage(n, page, text, bSelect, tooltip);
    }

    virtual int SetSelection(size_t n) override
    {
        GetBtnsListCtrl()->SetSelection(n);
        int ret = DoSetSelection(n, SetSelection_SendEvent);

        // check that only the selected page is visible and others are hidden:
        wxWindow* sel_pg = GetPage(n);
        for (size_t page = 0; page < m_pages.size(); page++) {
            if (page != n && GetPage(page) != sel_pg) {
                m_pages[page]->Hide();
            }
        }

        return ret;
    }

    virtual int ChangeSelection(size_t n) override
    {
        GetBtnsListCtrl()->SetSelection(n);
        return DoSetSelection(n);
    }

    virtual bool SetPageText(size_t n, const wxString & strText) override
    {
        return GetBtnsListCtrl()->SetPageText(n, strText);
    }

    virtual wxString GetPageText(size_t n) const override
    {
        return GetBtnsListCtrl()->GetPageText(n);
    }

    virtual bool SetPageToolTip(size_t n, const wxString & tooltip)
    {
        return GetBtnsListCtrl()->SetPageToolTip(n, tooltip);
    }

    virtual wxString GetPageToolTip(size_t n) const
    {
        return GetBtnsListCtrl()->GetPageToolTip(n);
    }

    /// `wxImageList` indexed images are not supported
    virtual bool SetPageImage(size_t WXUNUSED(n), int WXUNUSED(imageId)) override
    {
        return false;
    }

    /// `wxImageList` indexed images are not supported
    virtual int GetPageImage(size_t WXUNUSED(n)) const override
    {
        return NO_IMAGE;
    }

    bool SetPageImage(size_t n, const std::string& bmp_name)
    {
        return GetBtnsListCtrl()->SetPageImage(n, bmp_name);
    }

    // Override some wxWindow methods too.
    virtual void SetFocus() override
    {
        wxWindow* const page = GetCurrentPage();
        if (page)
            page->SetFocus();
    }

    TabButtonsListCtrl *GetBtnsListCtrl() const { return static_cast<TabButtonsListCtrl *>(m_bookctrl); }

    void Rescale() { GetBtnsListCtrl()->Rescale(); }

protected:
    virtual bool DoInsertPage(
        size_t n,
        wxWindow *page,
        const wxString &text,
        bool bSelect,
        const wxString &tooltip = wxEmptyString,
        bool use_bmp = false,
        const std::string& bmp_name = ""
    ) {
        if (!wxBookCtrlBase::InsertPage(n, page, text, bSelect))
            return false;

        bool res;
        if (use_bmp)
            res = GetBtnsListCtrl()->InsertPage(n, text, bmp_name, tooltip);
        else
            res = GetBtnsListCtrl()->InsertPage(n, text, ScalableBitmap(), tooltip);

        if (!res)  // unlikely
            RemovePage(n);
        else if (!DoSetSelectionAfterInsertion(n, bSelect))
            page->Hide();

        return res;
    }

    virtual void OnNavigationKey(wxNavigationKeyEvent& event)
    {
        if (event.IsWindowChange()) {
            // change pages
            //AdvanceSelection(event.GetDirection());
            this->GetGrandParent()->HandleWindowEvent(event);
        }
        else {
            // we get this event in 3 cases
            //
            // a) one of our pages might have generated it because the user TABbed
            // out from it in which case we should propagate the event upwards and
            // our parent will take care of setting the focus to prev/next sibling
            //
            // or
            //
            // b) the parent panel wants to give the focus to us so that we
            // forward it to our selected page. We can't deal with this in
            // OnSetFocus() because we don't know which direction the focus came
            // from in this case and so can't choose between setting the focus to
            // first or last panel child
            //
            // or
            //
            // c) we ourselves (see MSWTranslateMessage) generated the event
            //
            wxWindow* const parent = GetParent();

            // the wxObject* casts are required to avoid MinGW GCC 2.95.3 ICE
            const bool isFromParent = event.GetEventObject() == (wxObject*)parent;
            const bool isFromSelf = event.GetEventObject() == (wxObject*)this;
            const bool isForward = event.GetDirection();

            if (isFromSelf && !isForward)
            {
                // focus is currently on notebook tab and should leave
                // it backwards (Shift-TAB)
                event.SetCurrentFocus(this);
                parent->HandleWindowEvent(event);
            }
            else if (isFromParent || isFromSelf)
            {
                // no, it doesn't come from child, case (b) or (c): forward to a
                // page but only if entering notebook page (i.e. direction is
                // backwards (Shift-TAB) comething from out-of-notebook, or
                // direction is forward (TAB) from ourselves),
                if (m_selection != wxNOT_FOUND &&
                    (!event.GetDirection() || isFromSelf))
                {
                    // so that the page knows that the event comes from it's parent
                    // and is being propagated downwards
                    event.SetEventObject(this);

                    wxWindow* page = m_pages[m_selection];
                    if (!page->HandleWindowEvent(event))
                    {
                        page->SetFocus();
                    }
                    //else: page manages focus inside it itself
                }
                else // otherwise set the focus to the notebook itself
                {
                    SetFocus();
                }
            }
            else
            {
                // it comes from our child, case (a), pass to the parent, but only
                // if the direction is forwards. Otherwise set the focus to the
                // notebook itself. The notebook is always the 'first' control of a
                // page.
                if (!isForward)
                {
                    SetFocus();
                }
                else if (parent)
                {
                    event.SetCurrentFocus(this);
                    parent->HandleWindowEvent(event);
                }
            }
        }
    }

    virtual void UpdateSelectedPage(size_t WXUNUSED(newsel)) override
    {
        // Nothing to do here, but must be overridden to avoid the assert in
        // the base class version.
    }

    virtual wxBookCtrlEvent * CreatePageChangingEvent() const override
    {
        return new wxBookCtrlEvent(wxEVT_BOOKCTRL_PAGE_CHANGING,
                                   GetId());
    }

    virtual void MakeChangedEvent(wxBookCtrlEvent & event) override
    {
        event.SetEventType(wxEVT_BOOKCTRL_PAGE_CHANGED);
    }

    virtual wxWindow * DoRemovePage(size_t page) override
    {
        wxWindow* const win = wxBookCtrlBase::DoRemovePage(page);
        if (win)
        {
            GetBtnsListCtrl()->RemovePage(page);
            DoSetSelectionAfterRemoval(page);
        }

        return win;
    }

    virtual void DoShowPage(wxWindow * page, bool show) override
    {
        if (show)
            page->ShowWithEffect(m_showEffect, m_showTimeout);
        else
            page->HideWithEffect(m_hideEffect, m_hideTimeout);
    }

private:
    void Init()
    {
        // We don't need any border as we don't have anything to separate the
        // page contents from.
        SetInternalBorder(0);

        // No effects by default.
        m_showEffect =
        m_hideEffect = wxSHOW_EFFECT_NONE;

        m_showTimeout =
        m_hideTimeout = 0;
    }

    wxShowEffect m_showEffect,
                 m_hideEffect;

    unsigned m_showTimeout,
             m_hideTimeout;

};
//#endif // _WIN32
#endif // slic3r_Tabbook_hpp_
