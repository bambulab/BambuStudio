#ifndef slic3r_GUI_App_hpp_
#define slic3r_GUI_App_hpp_

#include <memory>
#include <string>
#include "ImGuiWrapper.hpp"
#include "ConfigWizard.hpp"
#include "OpenGLManager.hpp"
#include "libslic3r/Preset.hpp"
#include "wxExtensions.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/Utils/NetworkAgent.hpp"
#include "slic3r/GUI/WebViewDialog.hpp"
#include "slic3r/GUI/HMS.hpp"
#include "slic3r/GUI/Jobs/UpgradeNetworkJob.hpp"
#include "../Utils/PrintHost.hpp"

#include <wx/app.h>
#include <wx/colour.h>
#include <wx/font.h>
#include <wx/string.h>
#include <wx/snglinst.h>
#include <wx/msgdlg.h>

#include <mutex>
#include <stack>

//#define BBL_HAS_FIRST_PAGE          1
#define STUDIO_INACTIVE_TIMEOUT     15*60*1000
#define LOG_FILES_MAX_NUM           30
#define TIMEOUT_CONNECT             15
#define TIMEOUT_RESPONSE            15

#define BE_UNACTED_ON               0x00200001
#ifndef _MSW_DARK_MODE
    #define _MSW_DARK_MODE            1
#endif // _MSW_DARK_MODE

class wxMenuItem;
class wxMenuBar;
class wxTopLevelWindow;
class wxDataViewCtrl;
class wxBookCtrlBase;
// BBS
class Notebook;
struct wxLanguageInfo;


namespace Slic3r {

class AppConfig;
class PresetBundle;
class PresetUpdater;
class ModelObject;
// class PrintHostJobQueue;
class Model;
class DeviceManager;
class NetworkAgent;

namespace GUI{

class RemovableDriveManager;
class OtherInstanceMessageHandler;
class MainFrame;
class Sidebar;
class ObjectSettings;
class ObjectList;
class Plater;
class ParamsPanel;
class NotificationManager;
struct GUI_InitParams;
class ParamsDialog;
class HMSQuery;
class ModelMallDialog;


enum FileType
{
    FT_STEP,
    FT_STL,
    FT_OBJ,
    FT_AMF,
    FT_3MF,
    FT_GCODE,
    FT_MODEL,
    FT_PROJECT,
    FT_GALLERY,

    FT_INI,
    FT_SVG,

    FT_TEX,

    FT_SL1,

    FT_SIZE,
};

extern wxString file_wildcards(FileType file_type, const std::string &custom_extension = std::string{});

enum ConfigMenuIDs {
    //ConfigMenuWizard,
    //ConfigMenuSnapshots,
    //ConfigMenuTakeSnapshot,
    //ConfigMenuUpdate,
    //ConfigMenuDesktopIntegration,
    ConfigMenuPreferences,
    ConfigMenuPrinter,
    //ConfigMenuModeSimple,
    //ConfigMenuModeAdvanced,
    //ConfigMenuLanguage,
    //ConfigMenuFlashFirmware,
    ConfigMenuCnt,
};

enum BambuStudioMenuIDs {
  BambuStudioMenuAbout,
  BambuStudioMenuPreferences,
};

enum CameraMenuIDs {
    wxID_CAMERA_PERSPECTIVE,
    wxID_CAMERA_ORTHOGONAL,
    wxID_CAMERA_COUNT,
};


class Tab;
class ConfigWizard;

static wxString dots("...", wxConvUTF8);

// Does our wxWidgets version support markup?
#if wxUSE_MARKUP && wxCHECK_VERSION(3, 1, 1)
    #define SUPPORTS_MARKUP
#endif


#define  VERSION_LEN    4
class VersionInfo
{
public:
    std::string version_str;
    std::string version_name;
    std::string description;
    std::string url;
    bool        force_upgrade{ false };
    int      ver_items[VERSION_LEN];  // AA.BB.CC.DD
    VersionInfo() {
        for (int i = 0; i < VERSION_LEN; i++) {
            ver_items[i] = 0;
        }
        force_upgrade = false;
        version_str = "";
    }

    void parse_version_str(std::string str) {
        version_str = str;
        std::vector<std::string> items;
        boost::split(items, str, boost::is_any_of("."));
        if (items.size() == VERSION_LEN) {
            try {
                for (int i = 0; i < VERSION_LEN; i++) {
                    ver_items[i] = stoi(items[i]);
                }
            }
            catch (...) {
                ;
            }
        }
    }
    static std::string convert_full_version(std::string short_version);
    static std::string convert_short_version(std::string full_version);
    static std::string get_full_version() {
        return convert_full_version(SLIC3R_VERSION);
    }

    /* return > 0, need update */
    int compare(std::string ver_str) {
        if (version_str.empty()) return -1;

        int      ver_target[VERSION_LEN];
        std::vector<std::string> items;
        boost::split(items, ver_str, boost::is_any_of("."));
        if (items.size() == VERSION_LEN) {
            try {
                for (int i = 0; i < VERSION_LEN; i++) {
                    ver_target[i] = stoi(items[i]);
                    if (ver_target[i] < ver_items[i]) {
                        return 1;
                    }
                    else if (ver_target[i] == ver_items[i]) {
                        continue;
                    }
                    else {
                        return -1;
                    }
                }
            }
            catch (...) {
                return -1;
            }
        }
        return -1;
    }
};

class GUI_App : public wxApp
{
public:

    //BBS: remove GCodeViewer as seperate APP logic
    enum class EAppMode : unsigned char
    {
        Editor,
        GCodeViewer
    };

private:
    bool            m_initialized { false };
    bool            m_post_initialized { false };
    bool            m_app_conf_exists{ false };
    EAppMode        m_app_mode{ EAppMode::Editor };
    bool            m_is_recreating_gui{ false };
#ifdef __linux__
    bool            m_opengl_initialized{ false };
#endif

   
//#ifdef _WIN32
    wxColour        m_color_label_modified;
    wxColour        m_color_label_sys;
    wxColour        m_color_label_default;
    wxColour        m_color_window_default;
    wxColour        m_color_highlight_label_default;
    wxColour        m_color_hovered_btn_label;
    wxColour        m_color_default_btn_label;
    wxColour        m_color_highlight_default;
    wxColour        m_color_selected_btn_bg;
    bool            m_force_colors_update { false };
//#endif

    wxFont		    m_small_font;
    wxFont		    m_bold_font;
	wxFont			m_normal_font;
	wxFont			m_code_font;
    wxFont		    m_link_font;

    int             m_em_unit; // width of a "m"-symbol in pixels for current system font
                               // Note: for 100% Scale m_em_unit = 10 -> it's a good enough coefficient for a size setting of controls

    std::unique_ptr<wxLocale> 	  m_wxLocale;
    // System language, from locales, owned by wxWidgets.
    const wxLanguageInfo		 *m_language_info_system = nullptr;
    // Best translation language, provided by Windows or OSX, owned by wxWidgets.
    const wxLanguageInfo		 *m_language_info_best   = nullptr;

    OpenGLManager m_opengl_mgr;
    std::unique_ptr<RemovableDriveManager> m_removable_drive_manager;

    std::unique_ptr<ImGuiWrapper> m_imgui;
    std::unique_ptr<PrintHostJobQueue> m_printhost_job_queue;
	//std::unique_ptr <OtherInstanceMessageHandler> m_other_instance_message_handler;
    //std::unique_ptr <wxSingleInstanceChecker> m_single_instance_checker;
    //std::string m_instance_hash_string;
	//size_t m_instance_hash_int;

    //BBS
    bool m_is_closing {false};
    Slic3r::DeviceManager* m_device_manager { nullptr };
    NetworkAgent* m_agent { nullptr };
    std::vector<std::string> need_delete_presets;   // store setting ids of preset
    bool m_networking_compatible { false };
    bool m_networking_need_update { false };
    bool m_networking_cancel_update { false };
    std::shared_ptr<UpgradeNetworkJob> m_upgrade_network_job;

    VersionInfo version_info;
    static std::string version_display;
    HMSQuery    *hms_query { nullptr };

    boost::thread    m_sync_update_thread;
    bool             enable_sync = false;
    bool             m_is_dark_mode{ false };
    bool             m_adding_script_handler { false };
public:
    std::string     get_local_models_path();
    bool            OnInit() override;
    bool            initialized() const { return m_initialized; }

    //BBS: remove GCodeViewer as seperate APP logic
    explicit GUI_App();
    //explicit GUI_App(EAppMode mode = EAppMode::Editor);
    ~GUI_App() override;

    void show_message_box(std::string msg) { wxMessageBox(msg); }
    EAppMode get_app_mode() const { return m_app_mode; }
    Slic3r::DeviceManager* getDeviceManager() { return m_device_manager; }
    HMSQuery* get_hms_query() { return hms_query; }
    NetworkAgent* getAgent() { return m_agent; }
    bool is_editor() const { return m_app_mode == EAppMode::Editor; }
    bool is_gcode_viewer() const { return m_app_mode == EAppMode::GCodeViewer; }
    bool is_recreating_gui() const { return m_is_recreating_gui; }
    std::string logo_name() const { return is_editor() ? "BambuStudio" : "BambuStudio-gcodeviewer"; }

    // To be called after the GUI is fully built up.
    // Process command line parameters cached in this->init_params,
    // load configs, STLs etc.
    void            post_init();
    void            shutdown();
    // If formatted for github, plaintext with OpenGL extensions enclosed into <details>.
    // Otherwise HTML formatted for the system info dialog.
    static std::string get_gl_info(bool for_github);
    wxGLContext*    init_glcontext(wxGLCanvas& canvas);
    bool            init_opengl();

    void            init_download_path();
    static unsigned get_colour_approx_luma(const wxColour& colour);
    static bool     dark_mode();
    const wxColour  get_label_default_clr_system();
    const wxColour  get_label_default_clr_modified();
    void            init_label_colours();
    void            update_label_colours_from_appconfig();
    void            update_label_colours();
    // update color mode for window
    void            UpdateDarkUI(wxWindow *window, bool highlited = false, bool just_font = false);
    void            UpdateDarkUIWin(wxWindow* win);
    void            Update_dark_mode_flag();
    // update color mode for whole dialog including all children
    void            UpdateDlgDarkUI(wxDialog* dlg);
    void            UpdateFrameDarkUI(wxFrame* dlg);
    // update color mode for DataViewControl
    void            UpdateDVCDarkUI(wxDataViewCtrl* dvc, bool highlited = false);
    // update color mode for panel including all static texts controls
    void            UpdateAllStaticTextDarkUI(wxWindow* parent);
    void            init_fonts();
	void            update_fonts(const MainFrame *main_frame = nullptr);
    void            set_label_clr_modified(const wxColour& clr);
    void            set_label_clr_sys(const wxColour& clr);

    const wxColour& get_label_clr_modified(){ return m_color_label_modified; }
    const wxColour& get_label_clr_sys()     { return m_color_label_sys; }
    const wxColour& get_label_clr_default() { return m_color_label_default; }
    const wxColour& get_window_default_clr(){ return m_color_window_default; }

    // BBS
//#ifdef _WIN32
    const wxColour& get_label_highlight_clr()   { return m_color_highlight_label_default; }
    const wxColour& get_highlight_default_clr() { return m_color_highlight_default; }
    const wxColour& get_color_hovered_btn_label() { return m_color_hovered_btn_label; }
    const wxColour& get_color_selected_btn_bg() { return m_color_selected_btn_bg; }
    void            force_colors_update();
#ifdef _MSW_DARK_MODE
    void            force_menu_update();
#endif //_MSW_DARK_MODE
//#endif

    const wxFont&   small_font()            { return m_small_font; }
    const wxFont&   bold_font()             { return m_bold_font; }
    const wxFont&   normal_font()           { return m_normal_font; }
    const wxFont&   code_font()             { return m_code_font; }
    const wxFont&   link_font()             { return m_link_font; }
    int             em_unit() const         { return m_em_unit; }
    bool            tabs_as_menu() const;
    wxSize          get_min_size() const;
    float           toolbar_icon_scale(const bool is_limited = false) const;
    void            set_auto_toolbar_icon_scale(float scale) const;
    void            check_printer_presets();

    void            recreate_GUI(const wxString& message);
    void            system_info();
    void            keyboard_shortcuts();
    void            load_project(wxWindow *parent, wxString& input_file) const;
    void            import_model(wxWindow *parent, wxArrayString& input_files) const;
    void            load_gcode(wxWindow* parent, wxString& input_file) const;

    wxString transition_tridid(int trid_id);
    void            ShowUserGuide();
    void            ShowDownNetPluginDlg();
    void            ShowUserLogin();
    void            ShowOnlyFilament();
    //BBS
    void            request_login(bool show_user_info = false);
    bool            check_login();
    void            get_login_info();
    bool            is_user_login();

    void            request_user_login(int online_login);
    void            request_user_logout();
    int             request_user_unbind(std::string dev_id);
    std::string     handle_web_request(std::string cmd);
    void            handle_script_message(std::string msg);
    void            request_model_download(std::string url, std::string filename);
    void            download_project(std::string project_id);
    void            request_project_download(std::string project_id);
    void            request_open_project(std::string project_id);
    void            request_remove_project(std::string project_id);

    void            handle_http_error(unsigned int status, std::string body);
    void            on_http_error(wxCommandEvent &evt);
    void            on_user_login(wxCommandEvent &evt);
    void            enable_user_preset_folder(bool enable);

    // BBS
    bool            is_studio_active();
    void            reset_to_active();
    bool            m_studio_active = true;
    std::chrono::system_clock::time_point  last_active_point;

    void            check_update(bool show_tips, int by_user);
    void            check_new_version(bool show_tips = false, int by_user = 0);
    void            request_new_version(int by_user);
    void            enter_force_upgrade();
    void            set_skip_version(bool skip = true);
    void            no_new_version();
    static std::string format_display_version();
    void            show_dialog(wxString msg);
    void            reload_settings();
    void            remove_user_presets();
    void            sync_preset(Preset* preset);
    void            start_sync_user_preset(bool with_progress_dlg = false);
    void            stop_sync_user_preset();

    static bool     catch_error(std::function<void()> cb, const std::string& err);

    void            persist_window_geometry(wxTopLevelWindow *window, bool default_maximized = false);
    void            update_ui_from_settings();

    bool            switch_language();
    bool            load_language(wxString language, bool initial);

    Tab*            get_tab(Preset::Type type);
    Tab*            get_model_tab(bool part = false);
    ConfigOptionMode get_mode();
    void            save_mode(const /*ConfigOptionMode*/int mode) ;
    void            update_mode();

    // BBS
    //void            add_config_menu(wxMenuBar *menu);
    //void            add_config_menu(wxMenu* menu);
    bool            has_unsaved_preset_changes() const;
    bool            has_current_preset_changes() const;
    void            update_saved_preset_from_current_preset();
    std::vector<std::pair<unsigned int, std::string>> get_selected_presets() const;
    bool            check_and_save_current_preset_changes(const wxString& caption, const wxString& header, bool remember_choice = true, bool use_dont_save_insted_of_discard = false);
    void            apply_keeped_preset_modifications();
    bool            check_and_keep_current_preset_changes(const wxString& caption, const wxString& header, int action_buttons, bool* postponed_apply_of_keeped_changes = nullptr);
    bool            can_load_project();
    bool            check_print_host_queue();
    bool            checked_tab(Tab* tab);
    //BBS: add preset combox re-active logic
    void            load_current_presets(bool active_preset_combox = false, bool check_printer_presets = true);
    std::vector<std::string>& get_delete_cache_presets();
    void            delete_preset_from_cloud(std::string setting_id);

    wxString        current_language_code() const { return m_wxLocale->GetCanonicalName(); }
	// Translate the language code to a code, for which Prusa Research maintains translations. Defaults to "en_US".
    wxString 		current_language_code_safe() const;
    bool            is_localized() const { return m_wxLocale->GetLocale() != "English"; }

    void            open_preferences(size_t open_on_tab = 0, const std::string& highlight_option = std::string());

    virtual bool OnExceptionInMainLoop() override;
    // Calls wxLaunchDefaultBrowser if user confirms in dialog.
    bool            open_browser_with_warning_dialog(const wxString& url, int flags = 0);
#ifdef __APPLE__
    void            OSXStoreOpenFiles(const wxArrayString &files);
    // wxWidgets override to get an event on open files.
    void            MacOpenFiles(const wxArrayString &fileNames) override;
#endif /* __APPLE */

    Sidebar&             sidebar();
    ObjectSettings*      obj_settings();
    ObjectList*          obj_list();
    Plater*              plater();
    const Plater*        plater() const;
    ParamsPanel*         params_panel();
    ParamsDialog*        params_dialog();
    Model&      		 model();
    NotificationManager * notification_manager();

    ModelMallDialog*    m_mall_home_dialog{ nullptr };
    ModelMallDialog*    m_mall_publish_dialog{ nullptr };

    void            load_url(wxString url);
    void            open_mall_page_dialog();
    void            open_publish_page_dialog();
    void remove_mall_system_dialog();
    void            run_script(wxString js);
    bool            is_adding_script_handler() { return m_adding_script_handler; }
    void            set_adding_script_handler(bool status) { m_adding_script_handler = status; }

    // Parameters extracted from the command line to be passed to GUI after initialization.
    GUI_InitParams* init_params { nullptr };

    AppConfig*      app_config{ nullptr };
    PresetBundle*   preset_bundle{ nullptr };
    PresetUpdater*  preset_updater{ nullptr };
    MainFrame*      mainframe{ nullptr };
    Plater*         plater_{ nullptr };

	PresetUpdater*  get_preset_updater() { return preset_updater; }

    Notebook*       tab_panel() const ;
    int             extruders_cnt() const;
    int             extruders_edited_cnt() const;

    // BBS
    int             filaments_cnt() const;

    std::vector<Tab *>      tabs_list;
    std::vector<Tab *>      model_tabs_list;

	RemovableDriveManager* removable_drive_manager() { return m_removable_drive_manager.get(); }
	//OtherInstanceMessageHandler* other_instance_message_handler() { return m_other_instance_message_handler.get(); }
    //wxSingleInstanceChecker* single_instance_checker() {return m_single_instance_checker.get();}

	//void        init_single_instance_checker(const std::string &name, const std::string &path);
	//void        set_instance_hash (const size_t hash) { m_instance_hash_int = hash; m_instance_hash_string = std::to_string(hash); }
    //std::string get_instance_hash_string ()           { return m_instance_hash_string; }
	//size_t      get_instance_hash_int ()              { return m_instance_hash_int; }

    ImGuiWrapper* imgui() { return m_imgui.get(); }

    PrintHostJobQueue& printhost_job_queue() { return *m_printhost_job_queue.get(); }

    void            open_web_page_localized(const std::string &http_address);
    bool            may_switch_to_SLA_preset(const wxString& caption);
    bool            run_wizard(ConfigWizard::RunReason reason, ConfigWizard::StartPage start_page = ConfigWizard::SP_WELCOME);
    void            show_desktop_integration_dialog();

#if ENABLE_THUMBNAIL_GENERATOR_DEBUG
    // temporary and debug only -> extract thumbnails from selected gcode and save them as png files
    void            gcode_thumbnails_debug();
#endif // ENABLE_THUMBNAIL_GENERATOR_DEBUG

    OpenGLManager& get_opengl_manager() { return m_opengl_mgr; }
    GLShaderProgram* get_shader(const std::string& shader_name) { return m_opengl_mgr.get_shader(shader_name); }
    GLShaderProgram* get_current_shader() { return m_opengl_mgr.get_current_shader(); }

    bool is_gl_version_greater_or_equal_to(unsigned int major, unsigned int minor) const { return m_opengl_mgr.get_gl_info().is_version_greater_or_equal_to(major, minor); }
    bool is_glsl_version_greater_or_equal_to(unsigned int major, unsigned int minor) const { return m_opengl_mgr.get_gl_info().is_glsl_version_greater_or_equal_to(major, minor); }
    int  GetSingleChoiceIndex(const wxString& message, const wxString& caption, const wxArrayString& choices, int initialSelection);

#ifdef __WXMSW__
    // extend is stl/3mf/gcode/step etc
    void            associate_files(std::wstring extend);
    void            disassociate_files(std::wstring extend);
#endif // __WXMSW__
    std::string     get_plugin_url(std::string name, std::string country_code);
    int             download_plugin(std::string name, std::string package_name, InstallProgressFn pro_fn = nullptr, WasCancelledFn cancel_fn = nullptr);
    int             install_plugin(std::string name, std::string package_name, InstallProgressFn pro_fn = nullptr, WasCancelledFn cancel_fn = nullptr);
    std::string     get_http_url(std::string country_code);
    bool            is_compatibility_version();
    bool            check_networking_version();
    void            cancel_networking_install();
    void            restart_networking();

private:
    int             updating_bambu_networking();
    bool            on_init_inner();
    void            copy_network_if_available();
    bool            on_init_network(bool try_backup = false);
    void            init_networking_callbacks();
    void            init_app_config();
    void            remove_old_networking_plugins();
    //BBS set extra header for http request
    std::map<std::string, std::string> get_extra_header();
    void            init_http_extra_header();
    bool            check_older_app_config(Semver current_version, bool backup);
    void            copy_older_config();
    void            window_pos_save(wxTopLevelWindow* window, const std::string &name);
    bool            window_pos_restore(wxTopLevelWindow* window, const std::string &name, bool default_maximized = false);
    void            window_pos_sanitize(wxTopLevelWindow* window);
    void            window_pos_center(wxTopLevelWindow *window);
    bool            select_language();

    bool            config_wizard_startup();
	void            check_updates(const bool verbose);

    bool                    m_init_app_config_from_older { false };
    bool                    m_datadir_redefined { false };
    std::string             m_older_data_dir_path;
    boost::optional<Semver> m_last_config_version;
};

DECLARE_APP(GUI_App)
wxDECLARE_EVENT(EVT_CONNECT_LAN_MODE_PRINT, wxCommandEvent);
} // GUI
} // Slic3r

#endif // slic3r_GUI_App_hpp_
