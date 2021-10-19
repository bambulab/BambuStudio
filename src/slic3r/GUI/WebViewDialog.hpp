#ifndef slic3r_WebViewDialog_hpp_
#define slic3r_WebViewDialog_hpp_

#if defined(__WINDOWS__) || defined(__APPLE__)

#include "wx/artprov.h"
#include "wx/cmdline.h"
#include "wx/notifmsg.h"
#include "wx/settings.h"
#include "wx/webview.h"

#if wxUSE_WEBVIEW_IE
#include "wx/msw/webview_ie.h"
#endif
#if wxUSE_WEBVIEW_EDGE
#include "wx/msw/webview_edge.h"
#endif

#include "wx/webviewarchivehandler.h"
#include "wx/webviewfshandler.h"
#include "wx/numdlg.h"
#include "wx/infobar.h"
#include "wx/filesys.h"
#include "wx/fs_arc.h"
#include "wx/fs_mem.h"
#include "wx/stdpaths.h"
#include <wx/frame.h>
#include <wx/tbarbase.h>
#include "wx/textctrl.h"


namespace Slic3r {
namespace GUI {

//We map menu items to their history items
WX_DECLARE_HASH_MAP(int, wxSharedPtr<wxWebViewHistoryItem>,
    wxIntegerHash, wxIntegerEqual, wxMenuHistoryMap);

class WebFrame : public wxFrame
{
public:
    WebFrame(const wxString& url);
    virtual ~WebFrame();

    void UpdateState();
    void OnIdle(wxIdleEvent& evt);
    void OnUrl(wxCommandEvent& evt);
    void OnBack(wxCommandEvent& evt);
    void OnForward(wxCommandEvent& evt);
    void OnStop(wxCommandEvent& evt);
    void OnReload(wxCommandEvent& evt);
    void OnClearHistory(wxCommandEvent& evt);
    void OnEnableHistory(wxCommandEvent& evt);
    void OnNavigationRequest(wxWebViewEvent& evt);
    void OnNavigationComplete(wxWebViewEvent& evt);
    void OnDocumentLoaded(wxWebViewEvent& evt);
    void OnNewWindow(wxWebViewEvent& evt);
    void OnTitleChanged(wxWebViewEvent& evt);
    void OnFullScreenChanged(wxWebViewEvent& evt);
    void OnScriptMessage(wxWebViewEvent& evt);
    void OnSetPage(wxCommandEvent& evt);
    void OnViewSourceRequest(wxCommandEvent& evt);
    void OnViewTextRequest(wxCommandEvent& evt);
    void OnToolsClicked(wxCommandEvent& evt);
    void OnSetZoom(wxCommandEvent& evt);
    void OnError(wxWebViewEvent& evt);
    void OnPrint(wxCommandEvent& evt);
    void OnCut(wxCommandEvent& evt);
    void OnCopy(wxCommandEvent& evt);
    void OnPaste(wxCommandEvent& evt);
    void OnUndo(wxCommandEvent& evt);
    void OnRedo(wxCommandEvent& evt);
    void OnMode(wxCommandEvent& evt);
    void OnZoomLayout(wxCommandEvent& evt);
    void OnZoomCustom(wxCommandEvent& evt);
    void OnHistory(wxCommandEvent& evt);
    void OnScrollLineUp(wxCommandEvent&) { m_browser->LineUp(); }
    void OnScrollLineDown(wxCommandEvent&) { m_browser->LineDown(); }
    void OnScrollPageUp(wxCommandEvent&) { m_browser->PageUp(); }
    void OnScrollPageDown(wxCommandEvent&) { m_browser->PageDown(); }
    void RunScript(const wxString& javascript);
    void OnRunScriptString(wxCommandEvent& evt);
    void OnRunScriptInteger(wxCommandEvent& evt);
    void OnRunScriptDouble(wxCommandEvent& evt);
    void OnRunScriptBool(wxCommandEvent& evt);
    void OnRunScriptObject(wxCommandEvent& evt);
    void OnRunScriptArray(wxCommandEvent& evt);
    void OnRunScriptDOM(wxCommandEvent& evt);
    void OnRunScriptUndefined(wxCommandEvent& evt);
    void OnRunScriptNull(wxCommandEvent& evt);
    void OnRunScriptDate(wxCommandEvent& evt);
#if wxUSE_WEBVIEW_IE
    void OnRunScriptObjectWithEmulationLevel(wxCommandEvent& evt);
    void OnRunScriptDateWithEmulationLevel(wxCommandEvent& evt);
    void OnRunScriptArrayWithEmulationLevel(wxCommandEvent& evt);
#endif
    void OnRunScriptMessage(wxCommandEvent& evt);
    void OnRunScriptCustom(wxCommandEvent& evt);
    void OnAddUserScript(wxCommandEvent& evt);
    void OnSetCustomUserAgent(wxCommandEvent& evt);
    void OnClearSelection(wxCommandEvent& evt);
    void OnDeleteSelection(wxCommandEvent& evt);
    void OnSelectAll(wxCommandEvent& evt);
    void OnLoadScheme(wxCommandEvent& evt);
    void OnUseMemoryFS(wxCommandEvent& evt);
    void OnFind(wxCommandEvent& evt);
    void OnFindDone(wxCommandEvent& evt);
    void OnFindText(wxCommandEvent& evt);
    void OnFindOptions(wxCommandEvent& evt);
    void OnEnableContextMenu(wxCommandEvent& evt);
    void OnEnableDevTools(wxCommandEvent& evt);

private:
    wxTextCtrl* m_url;
    wxWebView* m_browser;

    wxToolBar* m_toolbar;
    wxToolBarToolBase* m_toolbar_back;
    wxToolBarToolBase* m_toolbar_forward;
    wxToolBarToolBase* m_toolbar_stop;
    wxToolBarToolBase* m_toolbar_reload;
    wxToolBarToolBase* m_toolbar_tools;

    wxToolBarToolBase* m_find_toolbar_done;
    wxToolBarToolBase* m_find_toolbar_next;
    wxToolBarToolBase* m_find_toolbar_previous;
    wxToolBarToolBase* m_find_toolbar_options;
    wxMenuItem* m_find_toolbar_wrap;
    wxMenuItem* m_find_toolbar_highlight;
    wxMenuItem* m_find_toolbar_matchcase;
    wxMenuItem* m_find_toolbar_wholeword;

    wxMenu* m_tools_menu;
    wxMenu* m_tools_history_menu;
    wxMenuItem* m_tools_layout;
    wxMenuItem* m_tools_tiny;
    wxMenuItem* m_tools_small;
    wxMenuItem* m_tools_medium;
    wxMenuItem* m_tools_large;
    wxMenuItem* m_tools_largest;
    wxMenuItem* m_tools_custom;
    wxMenuItem* m_tools_handle_navigation;
    wxMenuItem* m_tools_handle_new_window;
    wxMenuItem* m_tools_enable_history;
    wxMenuItem* m_edit_cut;
    wxMenuItem* m_edit_copy;
    wxMenuItem* m_edit_paste;
    wxMenuItem* m_edit_undo;
    wxMenuItem* m_edit_redo;
    wxMenuItem* m_edit_mode;
    wxMenuItem* m_scroll_line_up;
    wxMenuItem* m_scroll_line_down;
    wxMenuItem* m_scroll_page_up;
    wxMenuItem* m_scroll_page_down;
    wxMenuItem* m_script_string;
    wxMenuItem* m_script_integer;
    wxMenuItem* m_script_double;
    wxMenuItem* m_script_bool;
    wxMenuItem* m_script_object;
    wxMenuItem* m_script_array;
    wxMenuItem* m_script_dom;
    wxMenuItem* m_script_undefined;
    wxMenuItem* m_script_null;
    wxMenuItem* m_script_date;
#if wxUSE_WEBVIEW_IE
    wxMenuItem* m_script_object_el;
    wxMenuItem* m_script_date_el;
    wxMenuItem* m_script_array_el;
#endif
    wxMenuItem* m_script_message;
    wxMenuItem* m_script_custom;
    wxMenuItem* m_selection_clear;
    wxMenuItem* m_selection_delete;
    wxMenuItem* m_find;
    wxMenuItem* m_context_menu;
    wxMenuItem* m_dev_tools;

    wxInfoBar *m_info;
    wxStaticText* m_info_text;
    wxTextCtrl* m_find_ctrl;
    wxToolBar* m_find_toolbar;

    wxMenuHistoryMap m_histMenuItems;
    wxString m_findText;
    int m_findFlags, m_findCount;
    long m_zoomFactor;

    // Last executed JavaScript snippet, for convenience.
    wxString m_javascript;

    wxString m_bbl_user_agent;
};

class SourceViewDialog : public wxDialog
{
public:
    SourceViewDialog(wxWindow* parent, wxString source);
};

} // GUI
} // Slic3r
#else
namespace Slic3r {
namespace GUI {

    class WebFrame : public wxFrame
    {
    public:
        WebFrame(const wxString& url) { BOOST_LOG_TRIVIAL(trace) << "Create dummy WebFrame"; }
        bool Show() { return true; }
    };
}
}
#endif

#endif /* slic3r_Tab_hpp_ */
