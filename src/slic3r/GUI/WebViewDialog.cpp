#include "WebViewDialog.hpp"

#include "I18N.hpp"
#include "slic3r/GUI/wxExtensions.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "libslic3r_version.h"
#include "../Utils/Http.hpp"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <wx/sizer.h>
#include <wx/toolbar.h>
#include <wx/textdlg.h>
#include <wx/url.h>

#include <slic3r/GUI/Widgets/WebView.hpp>

namespace pt = boost::property_tree;

namespace Slic3r {
namespace GUI {
    wxDECLARE_EVENT(EVT_RESPONSE_MESSAGE, wxCommandEvent);

    wxDEFINE_EVENT(EVT_RESPONSE_MESSAGE, wxCommandEvent);

    #define LOGIN_INFO_UPDATE_TIMER_ID 10002

    BEGIN_EVENT_TABLE(WebViewPanel, wxPanel)
    EVT_TIMER(LOGIN_INFO_UPDATE_TIMER_ID, WebViewPanel::OnFreshLoginStatus)
    END_EVENT_TABLE()


WebViewPanel::WebViewPanel(wxWindow *parent)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
 {
    m_Region = wxGetApp().app_config->get_country_code();
    m_loginstatus = -1;

    wxString UrlLeft = wxString::Format("file://%s/web/homepage3/left.html", from_u8(resources_dir()));
    wxString UrlRight = wxString::Format("file://%s/web/homepage3/home.html", from_u8(resources_dir()));

    wxString strlang = wxGetApp().current_language_code_safe();
    if (strlang != "") 
    { 
        UrlLeft = wxString::Format("file://%s/web/homepage3/left.html?lang=%s", from_u8(resources_dir()), strlang);
        UrlRight = wxString::Format("file://%s/web/homepage3/home.html?lang=%s", from_u8(resources_dir()), strlang);
    }

    topsizer = new wxBoxSizer(wxVERTICAL);
    
#if !BBL_RELEASE_TO_PUBLIC
    // Create the button
    bSizer_toolbar = new wxBoxSizer(wxHORIZONTAL);

    m_button_back = new wxButton(this, wxID_ANY, wxT("Back"), wxDefaultPosition, wxDefaultSize, 0);
    m_button_back->Enable(false);
    bSizer_toolbar->Add(m_button_back, 0, wxALL, 5);

    m_button_forward = new wxButton(this, wxID_ANY, wxT("Forward"), wxDefaultPosition, wxDefaultSize, 0);
    m_button_forward->Enable(false);
    bSizer_toolbar->Add(m_button_forward, 0, wxALL, 5);

    m_button_stop = new wxButton(this, wxID_ANY, wxT("Stop"), wxDefaultPosition, wxDefaultSize, 0);

    bSizer_toolbar->Add(m_button_stop, 0, wxALL, 5);

    m_button_reload = new wxButton(this, wxID_ANY, wxT("Reload"), wxDefaultPosition, wxDefaultSize, 0);
    bSizer_toolbar->Add(m_button_reload, 0, wxALL, 5);

    m_url = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    bSizer_toolbar->Add(m_url, 1, wxALL | wxEXPAND, 5);

    m_button_tools = new wxButton(this, wxID_ANY, wxT("Tools"), wxDefaultPosition, wxDefaultSize, 0);
    bSizer_toolbar->Add(m_button_tools, 0, wxALL, 5);

    topsizer->Add(bSizer_toolbar, 0, wxEXPAND, 0);
    bSizer_toolbar->Show(false);

    // Create panel for find toolbar.
    wxPanel* panel = new wxPanel(this);
    topsizer->Add(panel, wxSizerFlags().Expand());

    // Create sizer for panel.
    wxBoxSizer* panel_sizer = new wxBoxSizer(wxVERTICAL);
    panel->SetSizer(panel_sizer);
#endif //BBL_RELEASE_TO_PUBLIC
    // Create the info panel
    m_info = new wxInfoBar(this);
    topsizer->Add(m_info, wxSizerFlags().Expand());

    //Create Webview Panel
    m_home_web = new wxBoxSizer(wxHORIZONTAL);

    // Create the webview
    m_browser = WebView::CreateWebView(this, UrlRight);
    if (m_browser == nullptr) {
        wxLogError("Could not init m_browser");
        return;
    }

    m_browserMW       = WebView::CreateWebView(this, "about:blank");
    if (m_browserMW == nullptr) {
        wxLogError("Could not init  m_browserMW");
        return;
    } 
    m_browserMW->Hide();
    SetMakerworldModelID("");
    m_onlinefirst    = false;

    m_leftfirst   = false;
    m_browserLeft = WebView::CreateWebView(this, UrlLeft);
    if (m_browserLeft == nullptr) {
        wxLogError("Could not init m_browser");
        return;
    }
    m_browserLeft->SetSize(wxSize(FromDIP(224), -1));
    m_browserLeft->SetMinSize(wxSize(FromDIP(224), -1));
    m_browserLeft->SetMaxSize(wxSize(FromDIP(224), -1));

    m_home_web->Add(m_browserLeft, 0, wxEXPAND | wxALL, 0);
    m_home_web->Add(m_browser, 1, wxEXPAND | wxALL, 0);
    m_home_web->Add(m_browserMW, 1, wxEXPAND | wxALL, 0);

    topsizer->Add(m_home_web,1, wxEXPAND | wxALL, 0);

    SetSizer(topsizer);
    Layout();

    // Create the Tools menu
    m_tools_menu = new wxMenu();
    wxMenuItem* viewSource = m_tools_menu->Append(wxID_ANY, _L("View Source"));
    wxMenuItem* viewText = m_tools_menu->Append(wxID_ANY, _L("View Text"));
    m_tools_menu->AppendSeparator();
    m_tools_handle_navigation = m_tools_menu->AppendCheckItem(wxID_ANY, _L("Handle Navigation"));
    m_tools_handle_new_window = m_tools_menu->AppendCheckItem(wxID_ANY, _L("Handle New Windows"));
    m_tools_menu->AppendSeparator();

    //Create an editing menu
    wxMenu* editmenu = new wxMenu();
    m_edit_cut = editmenu->Append(wxID_ANY, _L("Cut"));
    m_edit_copy = editmenu->Append(wxID_ANY, _L("Copy"));
    m_edit_paste = editmenu->Append(wxID_ANY, _L("Paste"));
    editmenu->AppendSeparator();
    m_edit_undo = editmenu->Append(wxID_ANY, _L("Undo"));
    m_edit_redo = editmenu->Append(wxID_ANY, _L("Redo"));
    editmenu->AppendSeparator();
    m_edit_mode = editmenu->AppendCheckItem(wxID_ANY, _L("Edit Mode"));
    m_tools_menu->AppendSubMenu(editmenu, "Edit");

    wxMenu* script_menu = new wxMenu;
    m_script_string = script_menu->Append(wxID_ANY, "Return String");
    m_script_integer = script_menu->Append(wxID_ANY, "Return integer");
    m_script_double = script_menu->Append(wxID_ANY, "Return double");
    m_script_bool = script_menu->Append(wxID_ANY, "Return bool");
    m_script_object = script_menu->Append(wxID_ANY, "Return JSON object");
    m_script_array = script_menu->Append(wxID_ANY, "Return array");
    m_script_dom = script_menu->Append(wxID_ANY, "Modify DOM");
    m_script_undefined = script_menu->Append(wxID_ANY, "Return undefined");
    m_script_null = script_menu->Append(wxID_ANY, "Return null");
    m_script_date = script_menu->Append(wxID_ANY, "Return Date");
    m_script_message = script_menu->Append(wxID_ANY, "Send script message");
    m_script_custom = script_menu->Append(wxID_ANY, "Custom script");
    m_tools_menu->AppendSubMenu(script_menu, _L("Run Script"));
    wxMenuItem* addUserScript = m_tools_menu->Append(wxID_ANY, _L("Add user script"));
    wxMenuItem* setCustomUserAgent = m_tools_menu->Append(wxID_ANY, _L("Set custom user agent"));

    //Selection menu
    wxMenu* selection = new wxMenu();
    m_selection_clear = selection->Append(wxID_ANY, _L("Clear Selection"));
    m_selection_delete = selection->Append(wxID_ANY, _L("Delete Selection"));
    wxMenuItem* selectall = selection->Append(wxID_ANY, _L("Select All"));

    editmenu->AppendSubMenu(selection, "Selection");

    wxMenuItem* loadscheme = m_tools_menu->Append(wxID_ANY, _L("Custom Scheme Example"));
    wxMenuItem* usememoryfs = m_tools_menu->Append(wxID_ANY, _L("Memory File System Example"));

    m_context_menu = m_tools_menu->AppendCheckItem(wxID_ANY, _L("Enable Context Menu"));
    m_dev_tools = m_tools_menu->AppendCheckItem(wxID_ANY, _L("Enable Dev Tools"));

    //By default we want to handle navigation and new windows
    m_tools_handle_navigation->Check();
    m_tools_handle_new_window->Check();

    //Zoom
    m_zoomFactor = 100;

    // Connect the button events
#if !BBL_RELEASE_TO_PUBLIC
    Bind(wxEVT_BUTTON, &WebViewPanel::OnBack, this, m_button_back->GetId());
    Bind(wxEVT_BUTTON, &WebViewPanel::OnForward, this, m_button_forward->GetId());
    Bind(wxEVT_BUTTON, &WebViewPanel::OnStop, this, m_button_stop->GetId());
    Bind(wxEVT_BUTTON, &WebViewPanel::OnReload, this, m_button_reload->GetId());
    Bind(wxEVT_BUTTON, &WebViewPanel::OnToolsClicked, this, m_button_tools->GetId());
    Bind(wxEVT_TEXT_ENTER, &WebViewPanel::OnUrl, this, m_url->GetId());

#endif //BBL_RELEASE_TO_PUBLIC

    // Connect the webview events
    Bind(wxEVT_WEBVIEW_NAVIGATING, &WebViewPanel::OnNavigationRequest, this);
    Bind(wxEVT_WEBVIEW_NAVIGATED, &WebViewPanel::OnNavigationComplete, this);
    Bind(wxEVT_WEBVIEW_LOADED, &WebViewPanel::OnDocumentLoaded, this);
    Bind(wxEVT_WEBVIEW_TITLE_CHANGED, &WebViewPanel::OnTitleChanged, this);
    Bind(wxEVT_WEBVIEW_ERROR, &WebViewPanel::OnError, this);
    Bind(wxEVT_WEBVIEW_NEWWINDOW, &WebViewPanel::OnNewWindow, this);
    Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &WebViewPanel::OnScriptMessage, this);
    Bind(EVT_RESPONSE_MESSAGE, &WebViewPanel::OnScriptResponseMessage, this);

    // Connect the menu events
    Bind(wxEVT_MENU, &WebViewPanel::OnViewSourceRequest, this, viewSource->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnViewTextRequest, this, viewText->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnCut, this, m_edit_cut->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnCopy, this, m_edit_copy->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnPaste, this, m_edit_paste->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnUndo, this, m_edit_undo->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnRedo, this, m_edit_redo->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnMode, this, m_edit_mode->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnRunScriptString, this, m_script_string->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnRunScriptInteger, this, m_script_integer->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnRunScriptDouble, this, m_script_double->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnRunScriptBool, this, m_script_bool->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnRunScriptObject, this, m_script_object->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnRunScriptArray, this, m_script_array->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnRunScriptDOM, this, m_script_dom->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnRunScriptUndefined, this, m_script_undefined->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnRunScriptNull, this, m_script_null->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnRunScriptDate, this, m_script_date->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnRunScriptMessage, this, m_script_message->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnRunScriptCustom, this, m_script_custom->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnAddUserScript, this, addUserScript->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnSetCustomUserAgent, this, setCustomUserAgent->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnClearSelection, this, m_selection_clear->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnDeleteSelection, this, m_selection_delete->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnSelectAll, this, selectall->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnLoadScheme, this, loadscheme->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnUseMemoryFS, this, usememoryfs->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnEnableContextMenu, this, m_context_menu->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::OnEnableDevTools, this, m_dev_tools->GetId());

    //Connect the idle events
    Bind(wxEVT_IDLE, &WebViewPanel::OnIdle, this);
    Bind(wxEVT_CLOSE_WINDOW, &WebViewPanel::OnClose, this);

    m_LoginUpdateTimer = nullptr;

    Bind(wxEVT_SHOW, [this](auto &e) {
        if (e.IsShown() && m_has_pending_staff_pick) {
            SendDesignStaffpick(true);
        }
    });
 }

WebViewPanel::~WebViewPanel()
{
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << " Start";
    SetEvtHandlerEnabled(false);
    
    delete m_tools_menu;

    if (m_LoginUpdateTimer != nullptr) {
        m_LoginUpdateTimer->Stop();
        delete m_LoginUpdateTimer;
        m_LoginUpdateTimer = NULL;
    }
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << " End";
}

void WebViewPanel::ResetWholePage() 
{ 
    std::string tmp_Region = wxGetApp().app_config->get_country_code();
    if (tmp_Region == m_Region) return;
    
    m_Region = tmp_Region;

    //left
    if (m_browserLeft != nullptr && m_leftfirst) m_browserLeft->Reload();

    //right
    json m_Res           = json::object();
    m_Res["command"]     = "homepage_rightarea_reset";
    m_Res["sequence_id"] = "10001";

    wxString strJS = wxString::Format("window.postMessage(%s)", m_Res.dump(-1, ' ', false, json::error_handler_t::ignore));
    RunScript(strJS);

    //online
    SetMakerworldModelID("");
    m_onlinefirst = false;
}

void WebViewPanel::load_url(wxString& url)
{
    this->Show();
    this->Raise();
    m_url->SetLabelText(url);

    if (wxGetApp().get_mode() == comDevelop)
        wxLogMessage(m_url->GetValue());
    m_browser->LoadURL(url);
    m_browser->SetFocus();
    UpdateState();
}

/**
    * Method that retrieves the current state from the web control and updates the GUI
    * the reflect this current state.
    */
void WebViewPanel::UpdateState()
{
#if !BBL_RELEASE_TO_PUBLIC
    if (m_browser == nullptr) return;

    if (m_browser->CanGoBack()) {
        m_button_back->Enable(true);
    }
    else {
        m_button_back->Enable(false);
    }
    if (m_browser->CanGoForward()) {
        m_button_forward->Enable(true);
    }
    else {
        m_button_forward->Enable(false);
    }
    if (m_browser->IsBusy())
    {
        m_button_stop->Enable(true);
    }
    else
    {
        m_button_stop->Enable(false);
    }

    //SetTitle(m_browser->GetCurrentTitle());
    m_url->SetValue(m_browser->GetCurrentURL());
#endif //BBL_RELEASE_TO_PUBLIC
}

void WebViewPanel::OnIdle(wxIdleEvent& WXUNUSED(evt))
{
#if !BBL_RELEASE_TO_PUBLIC
    if (m_browser == nullptr) return;

    if (m_browser->IsBusy())
    {
        wxSetCursor(wxCURSOR_ARROWWAIT);
        m_button_stop->Enable(true);
    }
    else
    {
        wxSetCursor(wxNullCursor);
        m_button_stop->Enable(false);
    }
#endif //BBL_RELEASE_TO_PUBLIC
}

/**
    * Callback invoked when user entered an URL and pressed enter
    */
void WebViewPanel::OnUrl(wxCommandEvent& WXUNUSED(evt))
{
    if (wxGetApp().get_mode() == comDevelop)
        wxLogMessage(m_url->GetValue());
    m_browser->LoadURL(m_url->GetValue());
    m_browser->SetFocus();
    UpdateState();
}

/**
    * Callback invoked when user pressed the "back" button
    */
void WebViewPanel::OnBack(wxCommandEvent& WXUNUSED(evt))
{
    m_browser->GoBack();
    UpdateState();
}

/**
    * Callback invoked when user pressed the "forward" button
    */
void WebViewPanel::OnForward(wxCommandEvent& WXUNUSED(evt))
{
    m_browser->GoForward();
    UpdateState();
}

/**
    * Callback invoked when user pressed the "stop" button
    */
void WebViewPanel::OnStop(wxCommandEvent& WXUNUSED(evt))
{
    m_browser->Stop();
    UpdateState();
}

/**
    * Callback invoked when user pressed the "reload" button
    */
void WebViewPanel::OnReload(wxCommandEvent& WXUNUSED(evt))
{
    m_browser->Reload();
    UpdateState();
}

void WebViewPanel::OnCut(wxCommandEvent& WXUNUSED(evt))
{
    m_browser->Cut();
}

void WebViewPanel::OnCopy(wxCommandEvent& WXUNUSED(evt))
{
    m_browser->Copy();
}

void WebViewPanel::OnPaste(wxCommandEvent& WXUNUSED(evt))
{
    m_browser->Paste();
}

void WebViewPanel::OnUndo(wxCommandEvent& WXUNUSED(evt))
{
    m_browser->Undo();
}

void WebViewPanel::OnRedo(wxCommandEvent& WXUNUSED(evt))
{
    m_browser->Redo();
}

void WebViewPanel::OnMode(wxCommandEvent& WXUNUSED(evt))
{
    m_browser->SetEditable(m_edit_mode->IsChecked());
}

void WebViewPanel::OnLoadScheme(wxCommandEvent& WXUNUSED(evt))
{
    wxPathList pathlist;
    pathlist.Add(".");
    pathlist.Add("..");
    pathlist.Add("../help");
    pathlist.Add("../../../samples/help");

    wxFileName helpfile(pathlist.FindValidPath("doc.zip"));
    helpfile.MakeAbsolute();
    wxString path = helpfile.GetFullPath();
    //Under MSW we need to flip the slashes
    path.Replace("\\", "/");
    path = "wxfs:///" + path + ";protocol=zip/doc.htm";
    m_browser->LoadURL(path);
}

void WebViewPanel::OnUseMemoryFS(wxCommandEvent& WXUNUSED(evt))
{
    m_browser->LoadURL("memory:page1.htm");
}

void WebViewPanel::OnEnableContextMenu(wxCommandEvent& evt)
{
    m_browser->EnableContextMenu(evt.IsChecked());
}

void WebViewPanel::OnEnableDevTools(wxCommandEvent& evt)
{
    m_browser->EnableAccessToDevTools(evt.IsChecked());
}

void WebViewPanel::OnClose(wxCloseEvent& evt)
{
    this->Hide();
}

void WebViewPanel::OnFreshLoginStatus(wxTimerEvent &event)
{
    //wxString mwnow = m_browserMW->GetCurrentURL();

    auto mainframe = Slic3r::GUI::wxGetApp().mainframe;
    if (mainframe && mainframe->m_webview == this)
        Slic3r::GUI::wxGetApp().get_login_info();

    if (wxGetApp().is_user_login()) { 
        if (m_loginstatus != 1) 
        { 
            m_loginstatus = 1;

            if (m_onlinefirst)
                UpdateMakerworldLoginStatus();
        }
    } else {
        if (m_loginstatus != 0) {
            m_loginstatus = 0;

            if (m_onlinefirst)
                SetMakerworldPageLoginStatus(false);
        }    
    }
}

void WebViewPanel::SendRecentList(int images)
{
    boost::property_tree::wptree req;
    boost::property_tree::wptree data;
    wxGetApp().mainframe->get_recent_projects(data, images);
    req.put(L"sequence_id", "");
    req.put(L"command", L"get_recent_projects");
    req.put_child(L"response", data);
    std::wostringstream oss;
    pt::write_json(oss, req, false);
    RunScript(wxString::Format("window.postMessage(%s)", oss.str()));
}

void WebViewPanel::SendDesignStaffpick(bool on)
{
    try {
        if (on) {
            std::string sguide = wxGetApp().app_config->get("firstguide", "finish");
            if (sguide != "true") return;

            if (!IsShownOnScreen()) {
                m_has_pending_staff_pick = true;
                return;
            }

            //For U Pick
            NetworkAgent *agent = GUI::wxGetApp().getAgent();
            if (agent && agent->is_user_login()) {
                get_user_mw_4u_config([this](std::string body) {
                    if (body.empty() || body.front() != '{') {
                        BOOST_LOG_TRIVIAL(warning) << "get_mw_user_preference failed " + body;
                        return;
                    }
                    CallAfter([this, body] { 
                        json jPrefer = json::parse(body);

                        int nRecommendStatus = jPrefer["recommendStatus"];
                        if (nRecommendStatus != 1 && nRecommendStatus != 3) 
                        {
                            // Default : Staff Pick
                            get_design_staffpick(0, 10, [this](std::string body) {
                                if (body.empty() || body.front() != '{') {
                                    BOOST_LOG_TRIVIAL(warning) << "get_design_staffpick failed " + body;
                                    return;
                                }
                                CallAfter([this, body] {
                                    if (!wxGetApp().has_model_mall()) return;

                                    auto body2 = from_u8(body);
                                    body2.insert(1, "\"command\": \"modelmall_model_advise_get\", ");
                                    RunScript(wxString::Format("window.postMessage(%s)", body2));

                                    m_online_type = "browse";
                                    //Show Online Menu
                                    SetLeftMenuShow("online", 1);
                                });
                            });                        
                        } else {
                            //For U Pick
                                get_4u_staffpick(0, 10, [this](std::string body) {
                                if (body.empty() || body.front() != '{') {
                                    BOOST_LOG_TRIVIAL(warning) << "get_mw_user_4ulist failed " + body;
                                    return;
                                }
                                CallAfter([this, body] {
                                    if (!wxGetApp().has_model_mall()) return;

                                    auto body2 = from_u8(body);
                                    body2.insert(1, "\"command\": \"modelmall_model_customized_get\", ");
                                    RunScript(wxString::Format("window.postMessage(%s)", body2));

                                    m_online_type = "recommend";
                                    //Show Online Menu
                                    SetLeftMenuShow("online", 1);
                                });
                            }); 
                        }
                    });
                });

            }            
            else
            {
                // Default : Staff Pick
                get_design_staffpick(0, 10, [this](std::string body) {
                    if (body.empty() || body.front() != '{') {
                        BOOST_LOG_TRIVIAL(warning) << "get_design_staffpick failed " + body;
                        return;
                    }
                    CallAfter([this, body] {
                        if (!wxGetApp().has_model_mall()) return;

                        auto body2 = from_u8(body);
                        body2.insert(1, "\"command\": \"modelmall_model_advise_get\", ");
                        RunScript(wxString::Format("window.postMessage(%s)", body2));

                        m_online_type = "browse";
                        //Show Online Menu
                        SetLeftMenuShow("online", 1);
                    });
                });
            }
        } else {
            std::string body2 = "{\"total\":0, \"hits\":[]}";
            body2.insert(1, "\"command\": \"modelmall_model_advise_get\", ");
            RunScript(wxString::Format("window.postMessage(%s)", body2));

            m_online_type = "";
            SetLeftMenuShow("online", 0);
        }
    } catch (nlohmann::detail::parse_error &err) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": parse got a nlohmann::detail::parse_error, reason = " << err.what();
        return;
    } catch (std::exception &e) {
        // wxMessageBox(e.what(), "", MB_OK);
        // wxLogMessage("GUIDE: LoadFamily Error: %s", e.what());
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": parse got exception: " << e.what();
        return;
    }

    m_has_pending_staff_pick = false;
}

void WebViewPanel::SendMakerlabList(  )
{
    try {
        std::string sguide = wxGetApp().app_config->get("firstguide", "finish");
        if (sguide != "true") return;

        get_makerlab_list([this](std::string body) {
            if (body.empty() || body.front() != '{') {
                BOOST_LOG_TRIVIAL(warning) << "get_makerlab_list failed " + body;
                return;
            }
            CallAfter([this, body] {
                auto body2 = from_u8(body);

                json jLab = json::parse(body2);
                if (jLab.contains("list")) 
                { 
                    int nSize = jLab["list"].size();
                    if (nSize > 0) 
                    {
                        body2.insert(1, "\"command\": \"homepage_makerlab_get\", ");
                        RunScript(wxString::Format("window.postMessage(%s)", body2));

                        SetLeftMenuShow("makerlab", 1);                    
                    }
                }
            });
        });
    } catch (nlohmann::detail::parse_error &err) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": parse got a nlohmann::detail::parse_error, reason = " << err.what();
        return;
    } catch (std::exception &e) {
        // wxMessageBox(e.what(), "", MB_OK);
        // wxLogMessage("GUIDE: LoadFamily Error: %s", e.what());
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": parse got exception: " << e.what();
        return;
    }
}

void WebViewPanel::OpenModelDetail(std::string id, NetworkAgent *agent) 
{ 
    SwitchLeftMenu("online"); 

    SetMakerworldModelID(id);
}


void WebViewPanel::SendLoginInfo()
{
    if (wxGetApp().getAgent()) {
        std::string login_info = wxGetApp().getAgent()->build_login_info();
        wxString strJS = wxString::Format("window.postMessage(%s)", login_info);
        RunScript(strJS);
    }
}

void WebViewPanel::ShowNetpluginTip()
{
    // Install Network Plugin
    //std::string NP_Installed = wxGetApp().app_config->get("installed_networking");
    bool        bValid       = wxGetApp().is_compatibility_version();

    int nShow = 0;
    if (!bValid) nShow = 1;

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": bValid=%1%, nShow=%2%")%bValid %nShow;

    json m_Res           = json::object();
    m_Res["command"]     = "network_plugin_installtip";
    m_Res["sequence_id"] = "10001";
    m_Res["show"]        = nShow;

    wxString strJS = wxString::Format("window.postMessage(%s)", m_Res.dump(-1, ' ', false, json::error_handler_t::ignore));

    RunScriptLeft(strJS);
}

void WebViewPanel::get_design_staffpick(int offset, int limit, std::function<void(std::string)> callback)
{
    auto host = wxGetApp().get_http_url(wxGetApp().app_config->get_country_code(), "v1/design-service/design/staffpick");
    std::string url = (boost::format("%1%/?offset=%2%&limit=%3%") % host % offset % limit).str();

    Http http = Http::get(url);
    http.header("accept", "application/json")
        .header("Content-Type", "application/json")
        .on_complete([this, callback](std::string body, unsigned status) { callback(body); })
        .on_error([this, callback](std::string body, std::string error, unsigned status) {
            callback(body + error);
        })
        .perform();
}

void WebViewPanel::get_makerlab_list(std::function<void(std::string)> callback)
{
    std::string url = wxGetApp().get_http_url(wxGetApp().app_config->get_country_code(), "v1/operation-service/makerlabhomepage");

    Http http = Http::get(url);
    http.header("accept", "application/json")
        .header("Content-Type", "application/json")
        .on_complete([this, callback](std::string body, unsigned status) { callback(body); })
        .on_error([this, callback](std::string body, std::string error, unsigned status) { callback(body + error); })
        .perform();
}

unsigned char ToHex(unsigned char x) { return x > 9 ? x + 55 : x + 48; }

unsigned char FromHex(unsigned char x)
{
    unsigned char y;
    if (x >= 'A' && x <= 'Z')
        y = x - 'A' + 10;
    else if (x >= 'a' && x <= 'z')
        y = x - 'a' + 10;
    else if (x >= '0' && x <= '9')
        y = x - '0';
    else
        assert(0);
    return y;
}

std::string UrlEncode(const std::string &str)
{
    std::string strTemp = "";
    size_t      length  = str.length();
    for (size_t i = 0; i < length; i++) {
        if (isalnum((unsigned char) str[i]) || (str[i] == '-') || (str[i] == '_') || (str[i] == '.') || (str[i] == '~'))
            strTemp += str[i];
        else if (str[i] == ' ')
            strTemp += "+";
        else {
            strTemp += '%';
            strTemp += ToHex((unsigned char) str[i] >> 4);
            strTemp += ToHex((unsigned char) str[i] % 16);
        }
    }
    return strTemp;
}

std::string UrlDecode(const std::string &str)
{
    std::string strTemp = "";
    size_t      length  = str.length();
    for (size_t i = 0; i < length; i++) {
        if (str[i] == '+')
            strTemp += ' ';
        else if (str[i] == '%') {
            assert(i + 2 < length);
            unsigned char high = FromHex((unsigned char) str[++i]);
            unsigned char low  = FromHex((unsigned char) str[++i]);
            strTemp += high * 16 + low;
        } else
            strTemp += str[i];
    }
    return strTemp;
}

bool WebViewPanel::GetJumpUrl(bool login, wxString ticket, wxString targeturl, wxString &finalurl)
{
    std::string h             = wxGetApp().get_model_http_url(wxGetApp().app_config->get_country_code());

    if (login) {
        if (ticket == "") return false;

        finalurl = wxString::Format("%sapi/sign-in/ticket?to=%s&ticket=%s", h, UrlEncode( std::string(targeturl.mb_str())), ticket);
    } else {
        finalurl = wxString::Format("%sapi/sign-out?to=%s", h, UrlEncode(std::string(targeturl.mb_str())));
    }

    return true;
}

void WebViewPanel::UpdateMakerworldLoginStatus()
{
    NetworkAgent *agent = GUI::wxGetApp().getAgent();
    if (agent == nullptr) return;

    std::string newticket;
    int ret = agent->request_bind_ticket(&newticket);
    if (ret==0) SetMakerworldPageLoginStatus(true, newticket);
}


void WebViewPanel::SetMakerworldPageLoginStatus(bool login ,wxString ticket) 
{ 
    if (m_browserMW == nullptr) return;
    
    wxString mw_currenturl;
    if (m_online_LastUrl != "") {
        mw_currenturl = m_online_LastUrl;
    } else {
        mw_currenturl = m_browserMW->GetCurrentURL();
        mw_currenturl.Replace("modelid=", "");
    }

    //mw_currenturl.Replace("modelid=", "");
    wxString mw_jumpurl = "";

    bool b = GetJumpUrl(login, ticket, mw_currenturl, mw_jumpurl);
    if (b) { 
        m_browserMW->LoadURL(mw_jumpurl);
        m_online_LastUrl = "";
    }
}


void WebViewPanel::get_user_mw_4u_config(std::function<void(std::string)> callback) { 
    NetworkAgent *agent = GUI::wxGetApp().getAgent();
    if (agent)
        int ret = agent->get_mw_user_preference(callback); 
}

void WebViewPanel::get_4u_staffpick(int seed, int limit, std::function<void(std::string)> callback)
{
    NetworkAgent *agent = GUI::wxGetApp().getAgent();
    if (agent) 
        int ret = agent->get_mw_user_4ulist(seed,limit,callback);
}

int WebViewPanel::get_model_mall_detail_url(std::string *url, std::string id)
{
    // https://makerhub-qa.bambu-lab.com/en/models/2077
    std::string h = wxGetApp().get_model_http_url(wxGetApp().app_config->get_country_code());
    auto l = wxGetApp().current_language_code_safe();
    if (auto n = l.find('_'); n != std::string::npos)
        l = l.substr(0, n);
    *url = (boost::format("%1%%2%/models/%3%") % h % l % id).str();
    return 0;
}

void WebViewPanel::update_mode()
{
    GetSizer()->Show(size_t(0), wxGetApp().app_config->get("internal_developer_mode") == "true");
    GetSizer()->Layout();
}

/**
    * Callback invoked when there is a request to load a new page (for instance
    * when the user clicks a link)
    */
void WebViewPanel::OnNavigationRequest(wxWebViewEvent& evt)
{
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ": " << evt.GetTarget().ToUTF8().data();
    const wxString &url = evt.GetURL();
    if (url.StartsWith("File://") || url.StartsWith("file://")) {
        if (!url.Contains("/web/homepage3/")) {
            auto file = wxURL::Unescape(wxURL(url).GetPath());
#ifdef _WIN32
            if (file.StartsWith('/'))
                file = file.Mid(1);
#endif
            wxGetApp().plater()->load_files(wxArrayString{1, &file});
            evt.Veto();
            return;
        }
    } 
    else {
        wxString surl = url;
        if (surl.find("?") != std::string::npos) {
            surl = surl.substr(0, surl.find("?")).Lower();
        } 

        if (surl.EndsWith(".zip")  || 
            surl.EndsWith(".pdf")  || 
            surl.EndsWith(".stl")  || 
            surl.EndsWith(".3mf")  || 
            surl.EndsWith(".xlsx") || 
            surl.EndsWith(".xls")  ||
            surl.EndsWith(".txt")
            ) 
        {
            wxLaunchDefaultBrowser(url);

            evt.Veto();
            return;
        }
    }

    if (m_info->IsShown())
    {
        m_info->Dismiss();
    }

    if (wxGetApp().get_mode() == comDevelop)
        wxLogMessage("%s", "Navigation request to '" + evt.GetURL() + "' (target='" +
            evt.GetTarget() + "')");

    //If we don't want to handle navigation then veto the event and navigation
    //will not take place, we also need to stop the loading animation
    if (!m_tools_handle_navigation->IsChecked())
    {
        evt.Veto();
        m_button_stop->Enable(false);
    }
    else
    {
        UpdateState();
    }
}

/**
    * Callback invoked when a navigation request was accepted
    */
void WebViewPanel::OnNavigationComplete(wxWebViewEvent& evt)
{
    if (m_browserMW!=nullptr && evt.GetId() == m_browserMW->GetId()) 
    {    
        std::string TmpNowUrl = m_browserMW->GetCurrentURL().ToStdString();
        std::string mwHost    = wxGetApp().get_model_http_url(wxGetApp().app_config->get_country_code());
        if (TmpNowUrl.find(mwHost) != std::string::npos) m_onlinefirst = true;

        if (m_contentname == "online") { // conf save
            SetWebviewShow("right", false); 
            SetWebviewShow("online", true);
        }
    }

    //m_browser->Show();
    Layout();
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ": " << evt.GetTarget().ToUTF8().data();
    if (wxGetApp().get_mode() == comDevelop)
        wxLogMessage("%s", "Navigation complete; url='" + evt.GetURL() + "'");
    UpdateState();
    ShowNetpluginTip();
}

/**
    * Callback invoked when a page is finished loading
    */
void WebViewPanel::OnDocumentLoaded(wxWebViewEvent& evt)
{
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ": " << evt.GetTarget().ToUTF8().data();
    wxString wurl = evt.GetURL();
    // Only notify if the document is the main frame, not a subframe
    if (m_browser!=nullptr && evt.GetId() == m_browser->GetId()) {
        if (wxGetApp().get_mode() == comDevelop) wxLogMessage("%s", "Document loaded; url='" + evt.GetURL() + "'");
    } 
    else if (m_browserLeft!=nullptr && evt.GetId() == m_browserLeft->GetId()) 
    {       
        m_leftfirst = true;
    }

    UpdateState();
}

void WebViewPanel::OnTitleChanged(wxWebViewEvent &evt)
{
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ": " << evt.GetString().ToUTF8().data();
    // wxGetApp().CallAfter([this] { SendRecentList(); });
}

/**
    * On new window, we veto to stop extra windows appearing
    */
void WebViewPanel::OnNewWindow(wxWebViewEvent& evt)
{
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ": " << evt.GetURL().ToUTF8().data();
    wxString flag = " (other)";

    if (evt.GetNavigationAction() == wxWEBVIEW_NAV_ACTION_USER)
    {
        flag = " (user)";
    }

    if (wxGetApp().get_mode() == comDevelop)
        wxLogMessage("%s", "New window; url='" + evt.GetURL() + "'" + flag);

    //If we handle new window events then just load them in this window as we
    //are a single window browser
    if (m_tools_handle_new_window->IsChecked())
        m_browser->LoadURL(evt.GetURL());

    UpdateState();
}

void WebViewPanel::OnScriptMessage(wxWebViewEvent& evt)
{
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ": " << evt.GetString().ToUTF8().data();
    // update login status
    if (m_LoginUpdateTimer == nullptr) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " Create Timer";
        m_LoginUpdateTimer = new wxTimer(this, LOGIN_INFO_UPDATE_TIMER_ID);
        m_LoginUpdateTimer->Start(2000);
    }

    if (wxGetApp().get_mode() == comDevelop)
        wxLogMessage("Script message received; value = %s, handler = %s", evt.GetString(), evt.GetMessageHandler());
    std::string response = wxGetApp().handle_web_request(evt.GetString().ToUTF8().data());
    if (response.empty()) return;

    /* remove \n in response string */
    response.erase(std::remove(response.begin(), response.end(), '\n'), response.end());
    if (!response.empty()) {
        m_response_js = wxString::Format("window.postMessage('%s')", response);
        wxCommandEvent* event = new wxCommandEvent(EVT_RESPONSE_MESSAGE, this->GetId());
        wxQueueEvent(this, event);
    }
    else {
        m_response_js.clear();
    }
}

void WebViewPanel::OnScriptResponseMessage(wxCommandEvent& WXUNUSED(evt))
{
    if (!m_response_js.empty()) {
        RunScript(m_response_js);
    }
}

/**
    * Invoked when user selects the "View Source" menu item
    */
void WebViewPanel::OnViewSourceRequest(wxCommandEvent& WXUNUSED(evt))
{
    SourceViewDialog dlg(this, m_browser->GetPageSource());
    dlg.ShowModal();
}

/**
    * Invoked when user selects the "View Text" menu item
    */
void WebViewPanel::OnViewTextRequest(wxCommandEvent& WXUNUSED(evt))
{
    wxDialog textViewDialog(this, wxID_ANY, "Page Text",
        wxDefaultPosition, wxSize(700, 500),
        wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

    wxTextCtrl* text = new wxTextCtrl(this, wxID_ANY, m_browser->GetPageText(),
        wxDefaultPosition, wxDefaultSize,
        wxTE_MULTILINE |
        wxTE_RICH |
        wxTE_READONLY);

    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(text, 1, wxEXPAND);
    SetSizer(sizer);
    textViewDialog.ShowModal();
}

/**
    * Invoked when user selects the "Menu" item
    */
void WebViewPanel::OnToolsClicked(wxCommandEvent& WXUNUSED(evt))
{
    if (m_browser->GetCurrentURL() == "")
        return;

    m_edit_cut->Enable(m_browser->CanCut());
    m_edit_copy->Enable(m_browser->CanCopy());
    m_edit_paste->Enable(m_browser->CanPaste());

    m_edit_undo->Enable(m_browser->CanUndo());
    m_edit_redo->Enable(m_browser->CanRedo());

    m_selection_clear->Enable(m_browser->HasSelection());
    m_selection_delete->Enable(m_browser->HasSelection());

    m_context_menu->Check(m_browser->IsContextMenuEnabled());
    m_dev_tools->Check(m_browser->IsAccessToDevToolsEnabled());

    wxPoint position = ScreenToClient(wxGetMousePosition());
    PopupMenu(m_tools_menu, position.x, position.y);
}

void WebViewPanel::RunScript(const wxString& javascript)
{
    // Remember the script we run in any case, so the next time the user opens
    // the "Run Script" dialog box, it is shown there for convenient updating.
    m_javascript = javascript;

    if (!m_browser) return;

    WebView::RunScript(m_browser, javascript);
}

void WebViewPanel::RunScriptLeft(const wxString &javascript)
{
    // Remember the script we run in any case, so the next time the user opens
    // the "Run Script" dialog box, it is shown there for convenient updating.
    m_javascript = javascript;

    if (!m_browserLeft) return;

    WebView::RunScript(m_browserLeft, javascript);
}


void WebViewPanel::OnRunScriptString(wxCommandEvent& WXUNUSED(evt))
{
    RunScript("setCount(345);");
}

void WebViewPanel::OnRunScriptInteger(wxCommandEvent& WXUNUSED(evt))
{
    RunScript("function f(a){return a;}f(123);");
}

void WebViewPanel::OnRunScriptDouble(wxCommandEvent& WXUNUSED(evt))
{
    RunScript("function f(a){return a;}f(2.34);");
}

void WebViewPanel::OnRunScriptBool(wxCommandEvent& WXUNUSED(evt))
{
    RunScript("function f(a){return a;}f(false);");
}

void WebViewPanel::OnRunScriptObject(wxCommandEvent& WXUNUSED(evt))
{
    RunScript("function f(){var person = new Object();person.name = 'Foo'; \
    person.lastName = 'Bar';return person;}f();");
}

void WebViewPanel::OnRunScriptArray(wxCommandEvent& WXUNUSED(evt))
{
    RunScript("function f(){ return [\"foo\", \"bar\"]; }f();");
}

void WebViewPanel::OnRunScriptDOM(wxCommandEvent& WXUNUSED(evt))
{
    RunScript("document.write(\"Hello World!\");");
}

void WebViewPanel::OnRunScriptUndefined(wxCommandEvent& WXUNUSED(evt))
{
    RunScript("function f(){var person = new Object();}f();");
}

void WebViewPanel::OnRunScriptNull(wxCommandEvent& WXUNUSED(evt))
{
    RunScript("function f(){return null;}f();");
}

void WebViewPanel::OnRunScriptDate(wxCommandEvent& WXUNUSED(evt))
{
    RunScript("function f(){var d = new Date('10/08/2017 21:30:40'); \
    var tzoffset = d.getTimezoneOffset() * 60000; \
    return new Date(d.getTime() - tzoffset);}f();");
}

void WebViewPanel::OnRunScriptMessage(wxCommandEvent& WXUNUSED(evt))
{
    RunScript("window.wx.postMessage('This is a web message');");
}

void WebViewPanel::OnRunScriptCustom(wxCommandEvent& WXUNUSED(evt))
{
    wxTextEntryDialog dialog
    (
        this,
        "Please enter JavaScript code to execute",
        wxGetTextFromUserPromptStr,
        m_javascript,
        wxOK | wxCANCEL | wxCENTRE | wxTE_MULTILINE
    );
    if (dialog.ShowModal() != wxID_OK)
        return;

    RunScript(dialog.GetValue());
}

void WebViewPanel::OnAddUserScript(wxCommandEvent& WXUNUSED(evt))
{
    wxString userScript = "window.wx_test_var = 'wxWidgets webview sample';";
    wxTextEntryDialog dialog
    (
        this,
        "Enter the JavaScript code to run as the initialization script that runs before any script in the HTML document.",
        wxGetTextFromUserPromptStr,
        userScript,
        wxOK | wxCANCEL | wxCENTRE | wxTE_MULTILINE
    );
    if (dialog.ShowModal() != wxID_OK)
        return;

    if (!m_browser->AddUserScript(dialog.GetValue()))
        wxLogError("Could not add user script");
}

void WebViewPanel::OnSetCustomUserAgent(wxCommandEvent& WXUNUSED(evt))
{
    wxString customUserAgent = "Mozilla/5.0 (iPhone; CPU iPhone OS 13_1_3 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/13.0.1 Mobile/15E148 Safari/604.1";
    wxTextEntryDialog dialog
    (
        this,
        "Enter the custom user agent string you would like to use.",
        wxGetTextFromUserPromptStr,
        customUserAgent,
        wxOK | wxCANCEL | wxCENTRE
    );
    if (dialog.ShowModal() != wxID_OK)
        return;

    if (!m_browser->SetUserAgent(customUserAgent))
        wxLogError("Could not set custom user agent");
}

void WebViewPanel::OnClearSelection(wxCommandEvent& WXUNUSED(evt))
{
    m_browser->ClearSelection();
}

void WebViewPanel::OnDeleteSelection(wxCommandEvent& WXUNUSED(evt))
{
    m_browser->DeleteSelection();
}

void WebViewPanel::OnSelectAll(wxCommandEvent& WXUNUSED(evt))
{
    m_browser->SelectAll();
}

/**
    * Callback invoked when a loading error occurs
    */
void WebViewPanel::OnError(wxWebViewEvent& evt)
{
    BOOST_LOG_TRIVIAL(info) << "HomePage OnError, Url = " << evt.GetURL() << " , Message: "<<evt.GetString();

#define WX_ERROR_CASE(type) \
    case type: \
    category = #type; \
    break;

    wxString category;
    switch (evt.GetInt())
    {
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_CONNECTION);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_CERTIFICATE);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_AUTH);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_SECURITY);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_NOT_FOUND);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_REQUEST);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_USER_CANCELLED);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_OTHER);
    }

    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ": [" << category << "] " << evt.GetString().ToUTF8().data();

    if (wxGetApp().get_mode() == comDevelop) 
    {
        wxLogMessage("%s", "Error; url='" + evt.GetURL() + "', error='" + category + " (" + evt.GetString() + ")'");

        // Show the info bar with an error        
    }
    //m_info->ShowMessage(_L("An error occurred loading ") + evt.GetURL() + "\n" + "'" + category + "'", wxICON_ERROR);

    if (evt.GetInt() == wxWEBVIEW_NAV_ERR_CONNECTION && evt.GetId() == m_browserMW->GetId()) 
    {        
        m_online_LastUrl = m_browserMW->GetCurrentURL();

        if (m_contentname == "online") 
        { 
            wxString errurl = evt.GetURL();

            wxString UrlRight = wxString::Format("file://%s/web/homepage3/disconnect.html", from_u8(resources_dir()));

            wxString strlang = wxGetApp().current_language_code_safe();
            if (strlang != "") {
                UrlRight = wxString::Format("file://%s/web/homepage3/disconnect.html?lang=%s", from_u8(resources_dir()), strlang);
            }

            m_browserMW->LoadURL(UrlRight);
       
            SetWebviewShow("online", true);
            SetWebviewShow("right", false);
        }
    }

    UpdateState();
}

void WebViewPanel::SetMakerworldModelID(std::string ModelID) 
{
    auto host = wxGetApp().get_model_http_url(wxGetApp().app_config->get_country_code());

    wxString language_code = wxGetApp().current_language_code().BeforeFirst('_');
    language_code          = language_code.ToStdString();

    if (ModelID != "")
        m_online_LastUrl = (boost::format("%1%%2%/studio/webview?modelid=%3%&from=bambustudio") % host % language_code.mb_str() % ModelID).str();
    else
        m_online_LastUrl = (boost::format("%1%%2%/studio/webview?from=bambustudio") % host % language_code.mb_str()).str();
}

void WebViewPanel::SwitchWebContent(std::string modelname, int refresh)
{
    m_contentname = modelname;

    CheckMenuNewTag();

    wxString strlang = wxGetApp().current_language_code_safe();

    if (modelname.compare("makerlab") == 0) {
        auto        host   = wxGetApp().get_model_http_url(wxGetApp().app_config->get_country_code());
        std::string LabUrl = (boost::format("%1%makerlab?from=bambustudio") % host).str();

        wxString      FinalUrl = LabUrl;
        NetworkAgent *agent    = GUI::wxGetApp().getAgent();
        if (agent && agent->is_user_login()) {
            std::string newticket;
            int         ret = agent->request_bind_ticket(&newticket);
            if (ret == 0) GetJumpUrl(true, newticket, FinalUrl, FinalUrl);
        }

        wxLaunchDefaultBrowser(FinalUrl);

        // conf save
        wxGetApp().app_config->set_str("homepage", "makerlab_clicked", "1");
        wxGetApp().app_config->save();
        wxGetApp().CallAfter([this] { ShowMenuNewTag("makerlab", "0"); });

        return;
    } else if (modelname.compare("online") == 0) {

        if (!m_onlinefirst) {
            if (m_loginstatus == 1) {
                UpdateMakerworldLoginStatus();
            } else {
                SetMakerworldPageLoginStatus(false);
            }
        } else {
            if (m_online_LastUrl != "") {
                m_browserMW->LoadURL(m_online_LastUrl);

                m_online_LastUrl = "";
            } else {
                //m_browserMW->Reload();
            }
        }

        SetWebviewShow("online", true);
        SetWebviewShow("right", false);

        GetSizer()->Layout();

        // conf save
        wxGetApp().app_config->set_str("homepage", "online_clicked", "1");
        wxGetApp().app_config->save();
        wxGetApp().CallAfter([this] { ShowMenuNewTag("online", "0"); });
    } else if (modelname.compare("home") == 0 || modelname.compare("recent") == 0 || modelname.compare("manual") == 0) {
        if (!m_browser) return;

        json m_Res           = json::object();
        m_Res["command"]     = "homepage_leftmenu_clicked";
        m_Res["sequence_id"] = "10001";
        m_Res["menu"]        = modelname;

        // wxString strJS = wxString::Format("HandleStudio(%s)", m_Res.dump(-1, ' ', false, json::error_handler_t::ignore));
        wxString strJS = wxString::Format("HandleStudio(%s)", m_Res.dump(-1, ' ', true));

        WebView::RunScript(m_browser, strJS);

        CallAfter([this]{
            SetWebviewShow("online", false);
            SetWebviewShow("right", true);
        });
    }
}

void WebViewPanel::SwitchLeftMenu(std::string strMenu)
{
    if (!m_browserLeft) return;

    json m_Res           = json::object();
    m_Res["command"]     = "homepage_leftmenu_clicked";
    m_Res["sequence_id"] = "10001";
    m_Res["menu"]        = strMenu;

    // wxString strJS = wxString::Format("HandleStudio(%s)", m_Res.dump(-1, ' ', false, json::error_handler_t::ignore));
    wxString strJS = wxString::Format("HandleStudio(%s)", m_Res.dump(-1, ' ', true));

    WebView::RunScript(m_browserLeft, strJS);
}

void WebViewPanel::OpenOneMakerlab(std::string url) {
    auto        host = wxGetApp().get_model_http_url(wxGetApp().app_config->get_country_code());
    std::string LabUrl  = (boost::format("%1%%2%") % host % url).str();

    wxString      FinalUrl = LabUrl;
    NetworkAgent *agent    = GUI::wxGetApp().getAgent();
    if (agent && agent->is_user_login()) {
        std::string newticket;
        int         ret = agent->request_bind_ticket(&newticket);
        if (ret == 0) GetJumpUrl(true, newticket, FinalUrl, FinalUrl);
    }

    wxLaunchDefaultBrowser(FinalUrl);
}


void WebViewPanel::CheckMenuNewTag() {
    std::string sClick = wxGetApp().app_config->get("homepage", "online_clicked");
    if (sClick.compare("1")==0) 
        ShowMenuNewTag("online", "0");
    else
        ShowMenuNewTag("online", "1");


    sClick = wxGetApp().app_config->get("homepage", "makerlab_clicked");
    if (sClick.compare("1") == 0)
        ShowMenuNewTag("makerlab", "0");
    else
        ShowMenuNewTag("makerlab", "1");
}

void WebViewPanel::ShowMenuNewTag(std::string menuname, std::string show)
{ 
    if (!m_browserLeft) return;

    if (menuname != "online" && menuname != "makerlab") return;

    json m_Res           = json::object();
    m_Res["command"]     = "homepage_leftmenu_newtag";
    m_Res["sequence_id"] = "10001";
    m_Res["menu"]           = menuname;


    if (show.compare("1") == 0)
        m_Res["show"] = 1;
    else 
        m_Res["show"] = 0;

    wxString strJS = wxString::Format("HandleStudio(%s)", m_Res.dump(-1, ' ', true));
    WebView::RunScript(m_browserLeft, strJS);
}

void WebViewPanel::SetLeftMenuShow(std::string menuname, int show) 
{
    if (!m_browserLeft) return;

    json m_Res           = json::object();
    m_Res["command"]     = "homepage_leftmenu_show";
    m_Res["sequence_id"] = "10001";
    m_Res["menu"]        = menuname;
    m_Res["show"] = show;

    wxString strJS = wxString::Format("HandleStudio(%s)", m_Res.dump(-1, ' ', true));
    WebView::RunScript(m_browserLeft, strJS);
}

void WebViewPanel::SetWebviewShow(wxString name, bool show) 
{ 
    wxWebView *TmpWeb = nullptr;
    if (name == "left")
        TmpWeb = m_browserLeft;
    else if (name == "right")
        TmpWeb = m_browser;
    else if (name == "online")
        TmpWeb = m_browserMW;
    
    if (TmpWeb != nullptr) 
    { 
        if (show)
            TmpWeb->Show();
        else
            TmpWeb->Hide();
    }
}

SourceViewDialog::SourceViewDialog(wxWindow* parent, wxString source) :
                  wxDialog(parent, wxID_ANY, "Source Code",
                           wxDefaultPosition, wxSize(700,500),
                           wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    wxTextCtrl* text = new wxTextCtrl(this, wxID_ANY, source,
                                      wxDefaultPosition, wxDefaultSize,
                                      wxTE_MULTILINE |
                                      wxTE_RICH |
                                      wxTE_READONLY);

    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(text, 1, wxEXPAND);
    SetSizer(sizer);
}


} // GUI
} // Slic3r
