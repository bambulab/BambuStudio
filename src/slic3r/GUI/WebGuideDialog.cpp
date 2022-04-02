#include "WebGuideDialog.hpp"
#include "ConfigWizard.hpp"

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

#include "MainFrame.hpp"
#include <boost/dll.hpp>


using namespace nlohmann;

namespace Slic3r { namespace GUI {

json m_ProfileJson;

GuideFrame::GuideFrame(GUI_App *pGUI)
	: wxDialog((wxWindow *) (pGUI->mainframe), wxID_ANY, "BambuStudio"),
	m_appconfig_new()
{
    // INI
    m_SectionName = "firstguide";
    PrivacyUse    = true;

    m_MainPtr = pGUI;

    m_bbl_user_agent = wxString::Format("BBL-Slicer/v%s", SLIC3R_VERSION);

    // set the frame icon
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
        m_browser->SetUserAgent(wxString::Format("BBL-Slicer/v%s", SLIC3R_VERSION));

#ifndef __WXMAC__
        // We register the wxfs:// protocol for testing purposes
        m_browser->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewArchiveHandler("bbl")));
        // And the memory: file system
        m_browser->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewFSHandler("memory")));
#endif

        if (!m_browser->AddScriptMessageHandler("wx")) wxLogError("Could not add script message handler");
    } else {
        wxLogError("Could not init m_browser");
    }

    wxString TargetUrl = SetStartPage(BBL_WELCOME);
    bool bRet = m_browser->Create(this, wxID_ANY, TargetUrl, wxDefaultPosition, wxDefaultSize);
    m_browser->EnableContextMenu(false);
    SetSizer(topsizer);

#ifdef __WXMAC__
    // With WKWebView handlers need to be registered before creation
    m_browser->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewArchiveHandler("wxfs")));
    m_browser->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewFSHandler("memory")));
#endif

    topsizer->Add(m_browser, wxSizerFlags().Expand().Proportion(1));

    // Log backend information
    // wxLogMessage(wxWebView::GetBackendVersionInfo().ToString());
    // wxLogMessage("Backend: %s Version: %s",
    // m_browser->GetClassInfo()->GetClassName(),wxWebView::GetBackendVersionInfo().ToString());
    // wxLogMessage("User Agent: %s", m_browser->GetUserAgent());

    // Set a more sensible size for web browsing
    SetSize(FromDIP(wxSize(1280, 800)));

    // Connect the webview events
    Bind(wxEVT_WEBVIEW_NAVIGATING, &GuideFrame::OnNavigationRequest, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_NAVIGATED, &GuideFrame::OnNavigationComplete, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_LOADED, &GuideFrame::OnDocumentLoaded, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_ERROR, &GuideFrame::OnError, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_NEWWINDOW, &GuideFrame::OnNewWindow, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_TITLE_CHANGED, &GuideFrame::OnTitleChanged, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_FULLSCREEN_CHANGED, &GuideFrame::OnFullScreenChanged, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &GuideFrame::OnScriptMessage, this, m_browser->GetId());

    // Connect the idle events
    // Bind(wxEVT_IDLE, &GuideFrame::OnIdle, this);
    // Bind(wxEVT_CLOSE_WINDOW, &GuideFrame::OnClose, this);

    LoadProfile();

    // UI
    SetStartPage(BBL_WELCOME);
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

wxString GuideFrame::SetStartPage(GuidePage startpage)
{ 
    wxString TargetUrl = (boost::filesystem::path(resources_dir()) / "web\\guide\\1\\index.html").make_preferred().string();

    if (startpage == BBL_WELCOME){ 
        SetTitle(_L("Guide"));
        TargetUrl = (boost::filesystem::path(resources_dir()) / "web\\guide\\1\\index.html").make_preferred().string();
    } else if (startpage == BBL_MODELS) {
        SetTitle(_L("Guide"));
        TargetUrl = (boost::filesystem::path(resources_dir()) / "web\\guide\\21\\index.html").make_preferred().string();
    } else if (startpage == BBL_FILAMENTS) {
        SetTitle(_L("Guide"));

        int nSize = m_ProfileJson["model"].size();

        if (nSize>0)
            TargetUrl = (boost::filesystem::path(resources_dir()) / "web\\guide\\22\\index.html").make_preferred().string();
        else     
            TargetUrl = (boost::filesystem::path(resources_dir()) / "web\\guide\\21\\index.html").make_preferred().string();
    } else if (startpage == BBL_FILAMENT_ONLY) {
        SetTitle(_L("Filaments Selection"));
        TargetUrl = (boost::filesystem::path(resources_dir()) / "web\\guide\\23\\index.html").make_preferred().string();
    }
    else {
        SetTitle(_L("Guide"));
        TargetUrl = (boost::filesystem::path(resources_dir()) / "web\\guide\\1\\index.html").make_preferred().string();
    }

    std::string strlang = wxGetApp().app_config->get("language");
    if (strlang != "") 
        TargetUrl = wxString::Format("%s?lang=%s", w2s(TargetUrl), strlang);

    load_url(TargetUrl);

    return TargetUrl;
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
    //wxLogMessage("%s", "Navigation complete; url='" + evt.GetURL() + "'");

    wxString NewUrl = evt.GetURL();

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

    if (evt.GetNavigationAction() == wxWEBVIEW_NAV_ACTION_USER) { flag = " (user)"; }

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
                this->EndModal(wxID_OK);
                
                m_MainPtr->mainframe->Close(); // Refuse Clause, App quit immediately
            }
        } else if (strCmd == "user_private_choice") {
            wxString strAction = j["data"]["action"];

            if (strAction == "agree") {
                PrivacyUse = true;
            } else {
                PrivacyUse = false;
            }
        } 
        else if (strCmd == "request_userguide_profile") {
            json m_Res = json::object();
            m_Res["command"] = "response_userguide_profile";
            m_Res["sequence_id"] = "10001";
            m_Res["response"]        = m_ProfileJson;

            wxString strJS = wxString::Format("HandleStudio(%s)", m_Res.dump());

            wxGetApp().CallAfter([this,strJS] { RunScript(strJS); });
        } 
        else if (strCmd == "save_userguide_models") 
        {
            json MSelected = j["data"];

            int nModel = m_ProfileJson["model"].size();
            for (int m = 0; m < nModel; m++) {
                json TmpModel = m_ProfileJson["model"][m];
                m_ProfileJson["model"][m]["nozzle_selected"] = "";

                for (auto it = MSelected.begin(); it != MSelected.end(); ++it) {
                    json OneSelect = it.value();

                    wxString s1 = TmpModel["model"];
                    wxString s2 = OneSelect["model"];
                    if (s1.compare(s2) == 0) {
                        m_ProfileJson["model"][m]["nozzle_selected"] = OneSelect["nozzle_diameter"];
                        break;
                    }
                }
            }
        } 
        else if (strCmd == "save_userguide_filaments") {
            //reset
            for (auto it = m_ProfileJson["filament"].begin(); it != m_ProfileJson["filament"].end(); ++it) 
            {
                m_ProfileJson["filament"][it.key()]["selected"] = 0;
            }

            json fSelected = j["data"]["filament"];
            int nF = fSelected.size();
            for (int m = 0; m < nF; m++) 
            { 
                std::string fName = fSelected[m];

                m_ProfileJson["filament"][fName]["selected"] = 1;
            }
        }
        else if (strCmd == "user_guide_finish") {
            SaveProfile();

            //Close();
            this->EndModal(wxID_OK);
        } 
        else if (strCmd == "user_guide_cancel") {
            this->EndModal(wxID_CANCEL);
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
    wxString strAll = m_ProfileJson.dump();
}

void GuideFrame::RunScript(const wxString &javascript)
{
    // Remember the script we run in any case, so the next time the user opens
    // the "Run Script" dialog box, it is shown there for convenient updating.
    //m_javascript = javascript;

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
        //wxMessageBox(e.what(), "", MB_OK);
    }
}

#if wxUSE_WEBVIEW_IE
void GuideFrame::OnRunScriptObjectWithEmulationLevel(wxCommandEvent &WXUNUSED(evt))
{
    wxWebViewIE::MSWSetModernEmulationLevel();
    RunScript("function f(){var person = new Object();person.name = 'Foo'; \
    person.lastName = 'Bar';return person;}f();");
    wxWebViewIE::MSWSetModernEmulationLevel(false);
}

void GuideFrame::OnRunScriptDateWithEmulationLevel(wxCommandEvent &WXUNUSED(evt))
{
    wxWebViewIE::MSWSetModernEmulationLevel();
    RunScript("function f(){var d = new Date('10/08/2017 21:30:40'); \
    var tzoffset = d.getTimezoneOffset() * 60000; return \
    new Date(d.getTime() - tzoffset);}f();");
    wxWebViewIE::MSWSetModernEmulationLevel(false);
}

void GuideFrame::OnRunScriptArrayWithEmulationLevel(wxCommandEvent &WXUNUSED(evt))
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
    std::string strVal = wxGetApp().app_config->get(std::string(m_SectionName.mb_str()), "finish");
    if (strVal == "1") 
        return false;

    if (bbl_bundle_rsrc == true) 
        return true;

    return true;
}

/*int GuideFrame::CopyDir(const boost::filesystem::path &from_dir, const boost::filesystem::path &to_dir)
{
    if (!boost::filesystem::is_directory(from_dir)) return -1;
    // i assume to_dir.parent surely exists
    if (!boost::filesystem::is_directory(to_dir)) boost::filesystem::create_directory(to_dir);
    for (auto &dir_entry : boost::filesystem::directory_iterator(from_dir)) {
        if (!boost::filesystem::is_directory(dir_entry.path())) {
            std::string    em;
            CopyFileResult cfr = copy_file(dir_entry.path().string(), (to_dir / dir_entry.path().filename()).string(), em, false);
            if (cfr != SUCCESS) { BOOST_LOG_TRIVIAL(error) << "Error when copying files from " << from_dir << " to " << to_dir << ": " << em; }
        } else {
            CopyDir(dir_entry.path(), to_dir / dir_entry.path().filename());
        }
    }

    return 0;
}*/

int GuideFrame::SaveProfile() 
{
    //privacy
    if (PrivacyUse == true) {
        wxGetApp().app_config->set(std::string(m_SectionName.mb_str()), "privacyuse", "1");
    } else
        wxGetApp().app_config->set(std::string(m_SectionName.mb_str()), "privacyuse", "0");

    //finish
    wxGetApp().app_config->set(std::string(m_SectionName.mb_str()), "finish", "1");

    m_MainPtr->app_config->save();

    //Load BBS Conf
    /*wxString strConfPath = wxGetApp().app_config->config_path();
    json     jCfg;
    std::ifstream(w2s(strConfPath)) >> jCfg;

    //model
    jCfg["models"] = json::array();
    int nM         = m_ProfileJson["model"].size();
    int nModelChoose    = 0;
    for (int m = 0; m < nM; m++) 
    { 
        json amodel = m_ProfileJson["model"][m];

        amodel["nozzle_diameter"] = amodel["nozzle_selected"];
        amodel.erase("nozzle_selected");
        amodel.erase("preview");
        amodel.erase("sub_path");
        amodel.erase("cover");
        amodel.erase("materials");

        std::string ss = amodel["nozzle_diameter"];
        if (ss.compare("") != 0) { 
            nModelChoose++;
            jCfg["models"].push_back(amodel); 
        }
    }
    if (nModelChoose == 0) 
        jCfg.erase("models");

    if (nModelChoose > 0) {
        // filament
        jCfg["filaments"] = json::array();
        for (auto it = m_ProfileJson["filament"].begin(); it != m_ProfileJson["filament"].end(); ++it) {
            if (it.value()["selected"] == 1) { jCfg["filaments"].push_back(it.key()); }
        }

        // Preset
        jCfg["presets"]["filaments"] = json::array();
        jCfg["presets"]["filaments"].push_back(jCfg["filaments"][0]);

        std::string PresetMachine  = m_ProfileJson["machine"][0]["name"];
        jCfg["presets"]["machine"] = PresetMachine;

        int nTotal = m_ProfileJson["process"].size();
        int nSet   = nTotal / 2;
        if (nSet > 0) nSet--;

        std::string sMode          = m_ProfileJson["process"][nSet]["name"];
        jCfg["presets"]["process"] = sMode;

    } else {
        jCfg["presets"]["filaments"] = json::array();
        jCfg["presets"]["filaments"].push_back("- default -");
    
        jCfg["presets"]["machine"] = "- default FFF -";

        jCfg["presets"]["process"] = "- default -";
    }

    std::string sOut = jCfg.dump(4, ' ', false);

    std::ofstream output_file(w2s(strConfPath));
    output_file << sOut;
    output_file.close();

    //Copy Profiles
    if (bbl_bundle_rsrc) 
    { 
        CopyDir(rsrc_vendor_dir,vendor_dir);
    }*/

    //set filaments to app_config
    const std::string &section_name = AppConfig::SECTION_FILAMENTS;
    std::map<std::string, std::string> section_new;
    m_appconfig_new.clear_section(section_name);
    for (auto it = m_ProfileJson["filament"].begin(); it != m_ProfileJson["filament"].end(); ++it) {
        if (it.value()["selected"] == 1){
            section_new[it.key()] = "true";
        }
    }
    m_appconfig_new.set_section(section_name, section_new);

    //set vendors to app_config
    Slic3r::AppConfig::VendorMap empty_vendor_map;
    m_appconfig_new.set_vendors(empty_vendor_map);
    for (auto it = m_ProfileJson["model"].begin(); it != m_ProfileJson["model"].end(); ++it) 
    {
        if (it.value().is_object()) {
            json temp_model = it.value();
            std::string model_name = temp_model["model"];
            std::string vendor_name = temp_model["vendor"];
            std::string selected = temp_model["nozzle_selected"];
            boost::trim(selected);
            std::string nozzle;
            while (selected.size() > 0) {
                auto pos = selected.find(';');
                if (pos != std::string::npos) {
                    nozzle   = selected.substr(0, pos);
                    m_appconfig_new.set_variant(vendor_name, model_name, nozzle, "true");
                    selected = selected.substr(pos + 1);
                    boost::trim(selected);
                }
                else {
                    m_appconfig_new.set_variant(vendor_name, model_name, selected, "true");
                    break;
                }
            }
        }
    }

    //m_appconfig_new

    return 0;
}

bool GuideFrame::apply_config(AppConfig *app_config, PresetBundle *preset_bundle, const PresetUpdater *updater, bool& apply_keeped_changes)
{
    const auto enabled_vendors = m_appconfig_new.vendors();
    const auto old_enabled_vendors = app_config->vendors();

    const auto enabled_filaments = m_appconfig_new.has_section(AppConfig::SECTION_FILAMENTS) ? m_appconfig_new.get_section(AppConfig::SECTION_FILAMENTS) : std::map<std::string, std::string>();
    const auto old_enabled_filaments = app_config->has_section(AppConfig::SECTION_FILAMENTS) ? app_config->get_section(AppConfig::SECTION_FILAMENTS) : std::map<std::string, std::string>();

    bool check_unsaved_preset_changes = false;
    std::vector<std::string> install_bundles;
    std::vector<std::string> remove_bundles;
    const auto vendor_dir = (boost::filesystem::path(Slic3r::data_dir()) / PRESET_SYSTEM_DIR).make_preferred();
    for (const auto &it : enabled_vendors) {
        if (it.second.size() > 0) {
            auto vendor_file = vendor_dir/(it.first + ".json");
            if (!fs::exists(vendor_file)) {
                install_bundles.emplace_back(it.first);
            }
        }
    }

    //add the removed vendor bundles
    for (const auto &it : old_enabled_vendors) {
        if (it.second.size() > 0) {
            if (enabled_vendors.find(it.first) != enabled_vendors.end())
                continue;
            auto vendor_file = vendor_dir/(it.first + ".json");
            if (fs::exists(vendor_file)) {
                remove_bundles.emplace_back(it.first);
            }
        }
    }

    check_unsaved_preset_changes = (enabled_vendors != old_enabled_vendors) || (enabled_filaments != old_enabled_filaments);
    wxString header = _L("The configuration package is changed in previous Config Guide");
    wxString caption = _L("Configuration package changed");
    int act_btns = UnsavedChangesDialog::ActionButtons::KEEP|UnsavedChangesDialog::ActionButtons::SAVE;

    if (check_unsaved_preset_changes &&
        !wxGetApp().check_and_keep_current_preset_changes(caption, header, act_btns, &apply_keeped_changes))
        return false;

    if (install_bundles.size() > 0) {
        // Install bundles from resources.
        // Don't create snapshot - we've already done that above if applicable.
        if (! updater->install_bundles_rsrc(std::move(install_bundles), false))
            return false;
    } else {
        BOOST_LOG_TRIVIAL(info) << "No bundles need to be installed from resource directory";
    }

    if (remove_bundles.size() > 0) {
        //remove unused bundles
        for (const auto &it : remove_bundles) {
            auto vendor_file = vendor_dir/(it + ".json");
            auto sub_dir = vendor_dir/(it);
            if (fs::exists(vendor_file))
                fs::remove(vendor_file);
            if (fs::exists(sub_dir))
                fs::remove_all(sub_dir);
        }
    } else {
        BOOST_LOG_TRIVIAL(info) << "No bundles need to be removed";
    }

    //update the app_config
    app_config->set_section(AppConfig::SECTION_FILAMENTS, enabled_filaments);
    app_config->set_vendors(m_appconfig_new);

    if (check_unsaved_preset_changes)
        preset_bundle->load_presets(*app_config, ForwardCompatibilitySubstitutionRule::EnableSilentDisableSystem,
                                    {PresetBundle::BBL_DEFAULT_PRINTER_MODEL, PresetBundle::BBL_DEFAULT_PRINTER_VARIANT, PresetBundle::BBL_DEFAULT_FILAMENT, std::string()});

    // Update the selections from the compatibilty.
    preset_bundle->export_selections(*app_config);

    return true;
}

bool GuideFrame::run()
{
    //BOOST_LOG_TRIVIAL(info) << boost::format("Running ConfigWizard, reason: %1%, start_page: %2%") % reason % start_page;

    GUI_App &app = wxGetApp();

    //p->set_run_reason(reason);
    //p->set_start_page(start_page);

    if (this->ShowModal() == wxID_OK) {
        bool apply_keeped_changes = false;
        if (! this->apply_config(app.app_config, app.preset_bundle, app.preset_updater, apply_keeped_changes))
            return false;

        if (apply_keeped_changes)
            app.apply_keeped_preset_modifications();

        app.app_config->set_legacy_datadir(false);
        app.update_mode();
        // BBS
        //app.obj_manipul()->update_ui_from_settings();
        BOOST_LOG_TRIVIAL(info) << "GuideFrame applied";
        return true;
    } else {
        BOOST_LOG_TRIVIAL(info) << "GuideFrame cancelled";
        return false;
    }
}

int GuideFrame::LoadProfile()
{
    try {
        //wxString ExePath            = boost::dll::program_location().parent_path().string();
        //wxString TargetFolder       = ExePath + "\\resources\\profiles\\";
        //wxString TargetFolderSearch = ExePath + "\\resources\\profiles\\*.json";

        //intptr_t    handle;
        //_finddata_t findData;

        //handle = _findfirst(TargetFolderSearch.mb_str(), &findData); // 查找目录中的第一个文件
        //if (handle == -1) { return -1; }

        //do {
        //    if (findData.attrib & _A_SUBDIR && strcmp(findData.name, ".") == 0 && strcmp(findData.name, "..") == 0) // 是否是子目录并且不为"."或".."
        //    {
        //        // cout << findData.name << "\t<dir>\n";
        //    } else {
        //        wxString strVendor = wxString(findData.name).BeforeLast('.');
        //        LoadProfileFamily(strVendor, TargetFolder + findData.name);
        //    }

        //} while (_findnext(handle, &findData) == 0); // 查找目录中的下一个文件

        // BBS: change directories by design
        vendor_dir      = (boost::filesystem::path(Slic3r::data_dir()) / PRESET_SYSTEM_DIR).make_preferred();
        rsrc_vendor_dir = (boost::filesystem::path(resources_dir()) / "profiles").make_preferred();

        // BBS: add BBL as default
        // BBS: add json logic for vendor bundle
        auto bbl_bundle_path = (vendor_dir / PresetBundle::BBL_BUNDLE).replace_extension(".json");
        bbl_bundle_rsrc = false;
        if (!boost::filesystem::exists(bbl_bundle_path)) {
            bbl_bundle_path = (rsrc_vendor_dir / PresetBundle::BBL_BUNDLE).replace_extension(".json");
            bbl_bundle_rsrc = true;
        }

        m_ProfileJson             = json::parse("{}");
        m_ProfileJson["model"]    = json::array();
        m_ProfileJson["machine"]  = json::array();
        m_ProfileJson["filament"] = json::object();
        m_ProfileJson["process"]  = json::array();
        LoadProfileFamily(PresetBundle::BBL_BUNDLE, bbl_bundle_path.string());

        const auto enabled_filaments = wxGetApp().app_config->has_section(AppConfig::SECTION_FILAMENTS) ? wxGetApp().app_config->get_section(AppConfig::SECTION_FILAMENTS) : std::map<std::string, std::string>();
        m_appconfig_new.set_vendors(*wxGetApp().app_config);
        m_appconfig_new.set_section(AppConfig::SECTION_FILAMENTS, enabled_filaments);

        for (auto it = m_ProfileJson["model"].begin(); it != m_ProfileJson["model"].end(); ++it) 
        {
            if (it.value().is_object()) {
                json& temp_model = it.value();
                std::string model_name = temp_model["model"];
                std::string vendor_name = temp_model["vendor"];
                std::string nozzle_diameter = temp_model["nozzle_diameter"];
                std::string selected;
                boost::trim(nozzle_diameter);
                std::string nozzle;
                bool enabled = false, first=true;
                while (nozzle_diameter.size() > 0) {
                    auto pos = nozzle_diameter.find(';');
                    if (pos != std::string::npos) {
                        nozzle   = nozzle_diameter.substr(0, pos);
                        enabled = m_appconfig_new.get_variant(vendor_name, model_name, nozzle);
                        if (enabled) {
                            if (!first)
                                selected += ";";
                            selected += nozzle;
                            first = false;
                        }
                        nozzle_diameter = nozzle_diameter.substr(pos + 1);
                        boost::trim(nozzle_diameter);
                    }
                    else {
                        enabled = m_appconfig_new.get_variant(vendor_name, model_name, nozzle_diameter);
                        if (enabled) {
                            if (!first)
                                selected += ";";
                            selected += nozzle_diameter;
                        }
                        break;
                    }
                }
                temp_model["nozzle_selected"] = selected;
                //m_ProfileJson["model"][a]["nozzle_selected"]
            }
        }

        if (m_ProfileJson["model"].size() == 1) { 
            std::string strNozzle = m_ProfileJson["model"][0]["nozzle_diameter"];
            m_ProfileJson["model"][0]["nozzle_selected"]=strNozzle;
        }


        for (auto it = m_ProfileJson["filament"].begin(); it != m_ProfileJson["filament"].end(); ++it) {
            //json temp_filament = it.value();
            std::string filament_name = it.key();
            if (enabled_filaments.find(filament_name) != enabled_filaments.end())
                m_ProfileJson["filament"][filament_name]["selected"] = 1;
        }

        /*wxString zz = m_ProfileJson.dump();
        // BBS: Compare with BambuConf
        wxString strConfPath = wxGetApp().app_config->config_path();

        json jCfg;
        std::ifstream(w2s(strConfPath)) >> jCfg;

        json ModelList = jCfg["models"];

        json ModelLocal = m_ProfileJson["model"];
        int  nLocal     = ModelLocal.size();
        int  nModel     = ModelList.size();

        for (int a=0;a<nLocal;a++) { 
            wxString LocalName = ModelLocal[a]["model"];

            for (int b = 0; b < nModel; b++) 
            { 
                wxString NameCfg = ModelList[b]["model"];

                if (NameCfg.compare(LocalName)==0) 
                {
                    m_ProfileJson["model"][a]["nozzle_selected"] = ModelList[b]["nozzle_diameter"];
                }
            }
        }

        json FilamentList = jCfg["filaments"];
        int  nFilament    = FilamentList.size();

        for (int m = 0; m < nFilament; m++) 
        { 
            wxString sKey = FilamentList[m];

            if (m_ProfileJson["filament"].contains(sKey)) 
            { 
                m_ProfileJson["filament"][w2s(sKey)]["selected"] = 1;
            }
        }*/
        }
    catch (std::exception &e) {
        // wxMessageBox(e.what(), "", MB_OK);
    }

    wxString strAll = m_ProfileJson.dump();

    return 0;
}

int GuideFrame::LoadProfileFamily(wxString strVendor, wxString strFilePath)
{
    wxString strFolder   = strFilePath.BeforeLast('\\');

    try {
        json jLocal;
        std::ifstream(w2s(strFilePath)) >> jLocal;

        // BBS:models
        json pmodels = jLocal["machine_model_list"];
        int  nsize   = pmodels.size();

        for (int n = 0; n < nsize; n++) {
            json OneModel = pmodels.at(n);

            OneModel["model"] = OneModel["name"];
            OneModel.erase("name");

            wxString s1 = OneModel["model"];
            wxString s2 = OneModel["sub_path"];

            wxString ModelFilePath = wxString::Format("%s\\%s\\%s", strFolder, strVendor, s2);
            json     pm;
            std::ifstream(w2s(ModelFilePath)) >> pm;

            OneModel["vendor"]          = strVendor.mbc_str();
            OneModel["nozzle_diameter"] = pm["nozzle_diameter"];
            OneModel["materials"]       = pm["default_materials"];
            
            wxString strCoverPath = wxString::Format("%s\\%s\\%s_cover.png", strFolder, strVendor, std::string(s1.mb_str()));
            OneModel["cover"]   = strCoverPath.mb_str();

            OneModel["nozzle_selected"] = "";

            m_ProfileJson["model"].push_back(OneModel);
        }

        // BBS:Machine
        json pmachine = jLocal["machine_list"];
        nsize         = pmachine.size();

        for (int n = 0; n < nsize; n++) {
            json OneMachine = pmachine.at(n);

            wxString s1 = OneMachine["name"];
            wxString s2 = OneMachine["sub_path"];

            wxString ModelFilePath = wxString::Format("%s\\%s\\%s", strFolder, strVendor, s2);
            json     pm;
            std::ifstream(w2s(ModelFilePath)) >> pm;

            wxString strInstant = pm["instantiation"];
            if (strInstant.compare("true") == 0) {
                OneMachine["model"] = pm["printer_model"];

                m_ProfileJson["machine"].push_back(OneMachine);
            }
        }

        // BBS:Filament
        json pFilament = jLocal["filament_list"];
        nsize          = pFilament.size();

        int nFalse = 0;
        int nModel = 0;
        int nFinish = 0;

        for (int n = 0; n < nsize; n++) {
            json OneFF = pFilament.at(n);

            wxString s1 = OneFF["name"];
            wxString s2 = OneFF["sub_path"];

            if (!m_ProfileJson["filament"].contains(s1))
            {
                wxString ModelFilePath = wxString::Format("%s\\%s\\%s", strFolder, strVendor, s2);
                json     pm;
                std::ifstream(w2s(ModelFilePath)) >> pm;

                wxString strInstant = pm["instantiation"];
                if (strInstant.compare("true") == 0) {
                    bool bExist = pm.contains("filament_vendor");

                    wxString sVendor = pm.contains("filament_vendor") ? pm["filament_vendor"][0] : "Generic";
                    OneFF["vendor"]  = std::string(sVendor.mb_str());

                    wxString strInherits = pm["inherits"];
                    wxString strTypeFile = wxString::Format("%s\\%s\\filament\\%s.json", strFolder, strVendor, strInherits.mb_str());
                    json     tm;
                    std::ifstream(w2s(strTypeFile)) >> tm;

                    wxString sN = tm["name"];
                    wxString sT = tm["filament_type"].at(0);

                    OneFF["type"] = tm["filament_type"][0];

                    OneFF["models"] = "";
                    OneFF["selected"] = 0;
                } 
                else
                    continue;
            
            } else {
                OneFF = m_ProfileJson["filament"][w2s(s1)];
            }

            wxString vModel = "";
            int nm    = m_ProfileJson["model"].size();
            int bFind = 0;
            for (int m = 0; m < nm; m++) {
                wxString strFF = m_ProfileJson["model"][m]["materials"];
                strFF          = wxString::Format(";%s;", w2s(strFF));
                wxString strTT = wxString::Format(";%s;", w2s(s1));
                if (strFF.Find(strTT) >= 0) {
                    wxString sModel = m_ProfileJson["model"][m]["model"];

                    vModel = wxString::Format("%s[%s]", w2s(vModel), w2s(sModel)).mb_str();
                    bFind           = 1;
                }
            }
            

            OneFF["models"]                    = w2s(vModel);

            m_ProfileJson["filament"][w2s(s1)] = OneFF;
        }

        //process
        json pProcess = jLocal["process_list"];
        nsize    = pProcess.size();

        for (int n = 0; n < nsize; n++) {
            json OneProcess = pProcess.at(n);

            wxString s2            = OneProcess["sub_path"];
            wxString ModelFilePath = wxString::Format("%s\\%s\\%s", strFolder, strVendor, s2);
            json     pm;
            std::ifstream(w2s(ModelFilePath)) >> pm;

            std::string bInstall = pm["instantiation"];
            if (bInstall == "true")
            { 
                m_ProfileJson["process"].push_back(OneProcess);
            }
        }

    } catch (std::exception &e) {
        // wxMessageBox(e.what(), "", MB_OK);
    }

    return 0;
}

std::string GuideFrame::w2s(wxString sSrc) 
{ 
    return std::string(sSrc.mb_str()); 
}


}} // namespace Slic3r::GUI
