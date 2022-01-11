#include "WebGuideDialog.hpp"

#include <string.h>
#include "I18N.hpp"
#include "libslic3r/AppConfig.hpp"
#include "slic3r/GUI/wxExtensions.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/AccountManager.hpp"
#include "libslic3r_version.h"

#include <wx/sizer.h>
#include <wx/toolbar.h>
#include <wx/textdlg.h>

#include <wx/wx.h>
#include <wx/fileconf.h>
#include <wx/file.h>
#include <wx/wfstream.h>

#include <boost/cast.hpp>
#include <boost/lexical_cast.hpp>

#include <nlohmann/json.hpp>
#include "MainFrame.hpp"
#include <boost/dll.hpp>

using namespace nlohmann;

namespace Slic3r { namespace GUI {

GuideFrame::GuideFrame(GUI_App *pGUI)
    : wxDialog((wxWindow *) (pGUI->mainframe), wxID_ANY, "BambuSlicer")
{
    // INI
    m_SectionName = "firstguide";
    PrivacyUse    = true;

    wxString ExePath = boost::dll::program_location().parent_path().string();
    wxString TargetUrl = "file:///" + ExePath + "\\resources\\guide\\1\\index.html";

    m_MainPtr = pGUI;

    m_bbl_user_agent = wxString::Format("BBL-Slicer/v%s", SLIC3R_RC_VERSION);

    // set the frame icon
    SetTitle("BambuSlicer");

    wxBoxSizer *topsizer = new wxBoxSizer(wxVERTICAL);

#if wxUSE_WEBVIEW_EDGE
    // Check if a fixed version of edge is present in
    // $executable_path/edge_fixed and use it
    wxFileName edgeFixedDir(wxStandardPaths::Get().GetExecutablePath());
    edgeFixedDir.SetFullName("");
    edgeFixedDir.AppendDir("edge_fixed");
    if (edgeFixedDir.DirExists()) {
        wxWebViewEdge::MSWSetBrowserExecutableDir(edgeFixedDir.GetFullPath());
        wxLogMessage("Using fixed edge version");
    }

#endif
    // Create the webview
    m_browser = wxWebView::New();
    if (m_browser) {
        m_browser->SetUserAgent(
            wxString::Format("BBL-Slicer/v%s", SLIC3R_RC_VERSION));

#ifndef __WXMAC__
        // We register the wxfs:// protocol for testing purposes
        m_browser->RegisterHandler(wxSharedPtr<wxWebViewHandler>(
            new wxWebViewArchiveHandler("bbl")));
        // And the memory: file system
        m_browser->RegisterHandler(
            wxSharedPtr<wxWebViewHandler>(new wxWebViewFSHandler("memory")));
#endif

        if (!m_browser->AddScriptMessageHandler("wx"))
            wxLogError("Could not add script message handler");
    } else {
        wxLogError("Could not init m_browser");
    }

    bool bRet = m_browser->Create(this, wxID_ANY, TargetUrl,
                                  wxDefaultPosition, wxDefaultSize);
    SetSizer(topsizer);

#ifdef __WXMAC__
    // With WKWebView handlers need to be registered before creation
    m_browser->RegisterHandler(
        wxSharedPtr<wxWebViewHandler>(new wxWebViewArchiveHandler("wxfs")));
    m_browser->RegisterHandler(
        wxSharedPtr<wxWebViewHandler>(new wxWebViewFSHandler("memory")));
#endif

    topsizer->Add(m_browser, wxSizerFlags().Expand().Proportion(1));

    // Log backend information
    // wxLogMessage(wxWebView::GetBackendVersionInfo().ToString());
    // wxLogMessage("Backend: %s Version: %s",
    // m_browser->GetClassInfo()->GetClassName(),wxWebView::GetBackendVersionInfo().ToString());
    // wxLogMessage("User Agent: %s", m_browser->GetUserAgent());

    // Set a more sensible size for web browsing
    SetSize(FromDIP(wxSize(560, 640)));

    // Connect the webview events
    Bind(wxEVT_WEBVIEW_NAVIGATING, &GuideFrame::OnNavigationRequest, this,
         m_browser->GetId());
    Bind(wxEVT_WEBVIEW_NAVIGATED, &GuideFrame::OnNavigationComplete, this,
         m_browser->GetId());
    Bind(wxEVT_WEBVIEW_LOADED, &GuideFrame::OnDocumentLoaded, this,
         m_browser->GetId());
    Bind(wxEVT_WEBVIEW_ERROR, &GuideFrame::OnError, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_NEWWINDOW, &GuideFrame::OnNewWindow, this,
         m_browser->GetId());
    Bind(wxEVT_WEBVIEW_TITLE_CHANGED, &GuideFrame::OnTitleChanged, this,
         m_browser->GetId());
    Bind(wxEVT_WEBVIEW_FULLSCREEN_CHANGED, &GuideFrame::OnFullScreenChanged,
         this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &GuideFrame::OnScriptMessage,
         this, m_browser->GetId());

    // Connect the idle events
    // Bind(wxEVT_IDLE, &GuideFrame::OnIdle, this);
    // Bind(wxEVT_CLOSE_WINDOW, &GuideFrame::OnClose, this);

    // UI
    SetTitle("Guide");
    CenterOnParent();
}

GuideFrame::~GuideFrame() {}

void GuideFrame::load_url(wxString &url)
{
    this->Show();
    m_browser->LoadURL(url);
    m_browser->SetFocus();
    UpdateState();
}

/**
 * Method that retrieves the current state from the web control and updates
 * the GUI the reflect this current state.
 */
void GuideFrame::UpdateState()
{
    // SetTitle(m_browser->GetCurrentTitle());
}

void GuideFrame::OnIdle(wxIdleEvent &WXUNUSED(evt))
{
    if (m_browser->IsBusy()) {
        wxSetCursor(wxCURSOR_ARROWWAIT);
    } else {
        wxSetCursor(wxNullCursor);
    }
}

// void GuideFrame::OnClose(wxCloseEvent& evt)
//{
//    this->Hide();
//}

/**
 * Callback invoked when there is a request to load a new page (for instance
 * when the user clicks a link)
 */
void GuideFrame::OnNavigationRequest(wxWebViewEvent &evt)
{
    // wxLogMessage("%s", "Navigation request to '" + evt.GetURL() + "'
    // (target='" + evt.GetTarget() + "')");

    UpdateState();
}

/**
 * Callback invoked when a navigation request was accepted
 */
void GuideFrame::OnNavigationComplete(wxWebViewEvent &evt)
{
    // wxLogMessage("%s", "Navigation complete; url='" + evt.GetURL() + "'");

    UpdateState();
}

/**
 * Callback invoked when a page is finished loading
 */
void GuideFrame::OnDocumentLoaded(wxWebViewEvent &evt)
{
    // Only notify if the document is the main frame, not a subframe
    wxString tmpUrl = evt.GetURL();
    wxString NowUrl = m_browser->GetCurrentURL();

    if (evt.GetURL() == m_browser->GetCurrentURL()) {
        // wxLogMessage("%s", "Document loaded; url='" + evt.GetURL() + "'");
    }
    UpdateState();

    // wxCommandEvent *event = new
    // wxCommandEvent(EVT_WEB_RESPONSE_MESSAGE,this->GetId()); wxQueueEvent(this,
    // event);
}

/**
 * On new window, we veto to stop extra windows appearing
 */
void GuideFrame::OnNewWindow(wxWebViewEvent &evt)
{
    wxString flag = " (other)";

    if (evt.GetNavigationAction() == wxWEBVIEW_NAV_ACTION_USER) {
        flag = " (user)";
    }

    // wxLogMessage("%s", "New window; url='" + evt.GetURL() + "'" + flag);

    // If we handle new window events then just load them in this window as we
    // are a single window browser
    // if (m_tools_handle_new_window->IsChecked())
    //    m_browser->LoadURL(evt.GetURL());

    UpdateState();
}

void GuideFrame::OnTitleChanged(wxWebViewEvent &evt)
{
    // SetTitle(evt.GetString());
    // wxLogMessage("%s", "Title changed; title='" + evt.GetString() + "'");
}

void GuideFrame::OnFullScreenChanged(wxWebViewEvent &evt)
{
    // wxLogMessage("Full screen changed; status = %d", evt.GetInt());
    ShowFullScreen(evt.GetInt() != 0);
}

void GuideFrame::OnScriptMessage(wxWebViewEvent &evt)
{
    try {
        wxString strInput = evt.GetString();
        json     j        = json::parse(strInput);

        wxString strCmd = j["command"];

        if (strCmd == "user_clause") {
            wxString strAction = j["data"]["action"];

            if (strAction == "refuse") {
                // CloseTheApp
                m_MainPtr->mainframe
                    ->Close(); // Refuse Clause, App quit immediately
            }
        } else if (strCmd == "user_private_choice") {
            wxString strAction = j["data"]["action"];

            if (strAction == "agree") {
                PrivacyUse = true;
            } else {
                PrivacyUse = false;
            }
        } else if (strCmd == "user_guide_finish") {
            if (PrivacyUse == true) {
                wxGetApp().app_config->set(std::string(m_SectionName.mb_str()),
                                           "privacyuse", "1");
            } else
                wxGetApp().app_config->set(std::string(m_SectionName.mb_str()),
                                           "privacyuse", "0");

            wxGetApp().app_config->set(std::string(m_SectionName.mb_str()),
                                       "finish", "1");

            m_MainPtr->app_config->save();

            Close();
        } else if (strCmd == "user_login") {
            wxString strAction = j["data"]["action"];
            if (strAction == "login") {
                // m_MainPtr->getAccountManager()->handle_web_request(
                // std::string(strInput.mb_str()));

                // Login Success, Dialog Close, Begin use App
                Close();
            }
        }
    } catch (std::exception &e) {
        // wxMessageBox(e.what(), "json Exception", MB_OK);
    }

    // wxLogMessage("Script message received; value = %s, handler = %s",
    // evt.GetString(), evt.GetMessageHandler()); Slic3r::AccountManager*
    // account_manager = Slic3r::GUI::wxGetApp().getAccountManager(); std::string
    // response =
    // account_manager->handle_web_request(evt.GetString().ToStdString()); if
    // (response.empty()) return;

    ///* remove \n in response string */
    // response.erase(std::remove(response.begin(), response.end(), '\n'),
    // response.end()); if (!response.empty()) {
    //    m_response_js = wxString::Format("window.postMessage('%s')", response);
    //    wxCommandEvent* event = new wxCommandEvent(EVT_RESPONSE_MESSAGE,
    //    this->GetId()); wxQueueEvent(this, event);
    //}
    // else {
    //    m_response_js.clear();
    //}
}

void GuideFrame::RunScript(const wxString &javascript)
{
    // Remember the script we run in any case, so the next time the user opens
    // the "Run Script" dialog box, it is shown there for convenient updating.
    m_javascript = javascript;

    // wxLogMessage("Running JavaScript:\n%s\n", javascript);

    if (!m_browser) return;

    wxString result;
    try {
        if (m_browser->RunScript(javascript, &result)) {
            // wxLogMessage("RunScript() returned \"%s\"", result);
        } else {
            // wxLogWarning("RunScript() failed");
        }
    } catch (std::exception &e) {
        // wxMessageBox(e.what(), "", MB_OK);
    }
}

#if wxUSE_WEBVIEW_IE
void GuideFrame::OnRunScriptObjectWithEmulationLevel(
    wxCommandEvent &WXUNUSED(evt))
{
    wxWebViewIE::MSWSetModernEmulationLevel();
    RunScript("function f(){var person = new Object();person.name = 'Foo'; \
    person.lastName = 'Bar';return person;}f();");
    wxWebViewIE::MSWSetModernEmulationLevel(false);
}

void GuideFrame::OnRunScriptDateWithEmulationLevel(
    wxCommandEvent &WXUNUSED(evt))
{
    wxWebViewIE::MSWSetModernEmulationLevel();
    RunScript("function f(){var d = new Date('10/08/2017 21:30:40'); \
    var tzoffset = d.getTimezoneOffset() * 60000; return \
    new Date(d.getTime() - tzoffset);}f();");
    wxWebViewIE::MSWSetModernEmulationLevel(false);
}

void GuideFrame::OnRunScriptArrayWithEmulationLevel(
    wxCommandEvent &WXUNUSED(evt))
{
    wxWebViewIE::MSWSetModernEmulationLevel();
    RunScript("function f(){ return [\"foo\", \"bar\"]; }f();");
    wxWebViewIE::MSWSetModernEmulationLevel(false);
}
#endif

/**
 * Callback invoked when a loading error occurs
 */
void GuideFrame::OnError(wxWebViewEvent &evt)
{
#define WX_ERROR_CASE(type) \
    case type: category = #type; break;

    wxString category;
    switch (evt.GetInt()) {
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_CONNECTION);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_CERTIFICATE);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_AUTH);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_SECURITY);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_NOT_FOUND);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_REQUEST);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_USER_CANCELLED);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_OTHER);
    }

    // wxLogMessage("%s", "Error; url='" + evt.GetURL() + "', error='" +
    // category + " (" + evt.GetString() + ")'");

    // Show the info bar with an error
    // m_info->ShowMessage(_L("An error occurred loading ") + evt.GetURL() +
    // "\n" + "'" + category + "'", wxICON_ERROR);

    UpdateState();
}

void GuideFrame::OnScriptResponseMessage(wxCommandEvent &WXUNUSED(evt))
{
    // if (!m_response_js.empty())
    //{
    //    RunScript(m_response_js);
    //}

    // RunScript("This is a message to Web!");
    // RunScript("postMessage(\"AABBCCDD\");");
}

bool GuideFrame::IsFirstUse()
{
    wxString    strUse;
    std::string strVal =
        wxGetApp().app_config->get(std::string(m_SectionName.mb_str()),
                                   "finish");
    if (strVal == "1") return false;

    return true;
}

}} // namespace Slic3r::GUI
