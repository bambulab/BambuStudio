#include "Preferences.hpp"
#include "OptionsGroup.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "MsgDialog.hpp"
#include "I18N.hpp"
#include "libslic3r/AppConfig.hpp"
#include <wx/notebook.h>
#include "Notebook.hpp"
#include "ButtonsDescription.hpp"
#include "OG_CustomCtrl.hpp"

namespace Slic3r {

	static t_config_enum_names enum_names_from_keys_map(const t_config_enum_values& enum_keys_map)
	{
		t_config_enum_names names;
		int cnt = 0;
		for (const auto& kvp : enum_keys_map)
			cnt = std::max(cnt, kvp.second);
		cnt += 1;
		names.assign(cnt, "");
		for (const auto& kvp : enum_keys_map)
			names[kvp.second] = kvp.first;
		return names;
	}

namespace GUI {

PreferencesDialog::PreferencesDialog(wxWindow* parent, int selected_tab, const std::string& highlight_opt_key) :
    DPIDialog(parent, wxID_ANY, _L("Preferences"), wxDefaultPosition, 
              wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
#ifdef __WXOSX__
    isOSX = true;
#endif
	build(selected_tab);
	if (!highlight_opt_key.empty())
		init_highlighter(highlight_opt_key);
}

static std::shared_ptr<ConfigOptionsGroup>create_options_tab(const wxString& title, wxBookCtrlBase* tabs)
{
	wxPanel* tab = new wxPanel(tabs, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBK_LEFT | wxTAB_TRAVERSAL);
	tabs->AddPage(tab, title);
	tab->SetFont(wxGetApp().normal_font());

	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
	sizer->SetSizeHints(tab);
	tab->SetSizer(sizer);

	std::shared_ptr<ConfigOptionsGroup> optgroup = std::make_shared<ConfigOptionsGroup>(tab);
	optgroup->label_width = 40;
	return optgroup;
}

static void activate_options_tab(std::shared_ptr<ConfigOptionsGroup> optgroup)
{
	optgroup->activate([](){}, wxALIGN_RIGHT);
	optgroup->update_visibility(comSimple);
	wxBoxSizer* sizer = static_cast<wxBoxSizer*>(static_cast<wxPanel*>(optgroup->parent())->GetSizer());
	sizer->Add(optgroup->sizer, 0, wxEXPAND | wxALL, 10);
}

void PreferencesDialog::build(size_t selected_tab)
{
#ifdef _WIN32
	wxGetApp().UpdateDarkUI(this);
#else
	SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
#endif
	const wxFont& font = wxGetApp().normal_font();
	SetFont(font);

	auto app_config = get_app_config();

#ifdef _MSW_DARK_MODE
	wxBookCtrlBase* tabs;
//	if (wxGetApp().dark_mode())
		tabs = new Notebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNB_TOP | wxTAB_TRAVERSAL | wxNB_NOPAGETHEME | wxNB_DEFAULT);
/*	else {
		tabs = new wxNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNB_TOP | wxTAB_TRAVERSAL | wxNB_NOPAGETHEME | wxNB_DEFAULT);
		tabs->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
	}*/
#else
    wxNotebook* tabs = new wxNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNB_TOP | wxTAB_TRAVERSAL  |wxNB_NOPAGETHEME | wxNB_DEFAULT );
	tabs->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
#endif

	// Add "General" tab
	m_optgroup_general = create_options_tab(_L("General"), tabs);
	m_optgroup_general->m_on_change = [this](t_config_option_key opt_key, boost::any value) {
		// BBS: backup
		if (opt_key == "backup_interval")
			m_values[opt_key] = boost::lexical_cast<std::string>(boost::any_cast<int>(value));
		else
		    m_values[opt_key] = boost::any_cast<bool>(value) ? "1" : "0";
	};

	bool is_editor = wxGetApp().is_editor();


	ConfigOptionDef def;
	Option option(def, "");
	if (is_editor) {

#ifdef SUPPORT_REMEMBER_OUTPUT_PATH
		def.label = L("Remember output directory");
		def.type = coBool;
		def.tooltip = L("If this is enabled, Slic3r will prompt the last output directory "
			"instead of the one containing the input files.");
		def.set_default_value(new ConfigOptionBool{ app_config->has("remember_output_path") ? app_config->get("remember_output_path") == "1" : true });
		option = Option(def, "remember_output_path");
		m_optgroup_general->append_single_option_line(option);
#endif

#ifdef SUPPORT_AUTO_CENTER
		def.label = L("Auto-center parts");
		def.type = coBool;
		def.tooltip = L("If this is enabled, Slic3r will auto-center objects "
			"around the print bed center.");
		def.set_default_value(new ConfigOptionBool{ app_config->get("autocenter") == "true" });
		option = Option(def, "autocenter");
		m_optgroup_general->append_single_option_line(option);
#endif

#ifdef SUPPORT_BACKGROUND_PROCESSING
		def.label = L("Background processing");
		def.type = coBool;
		def.tooltip = L("If this is enabled, Slic3r will pre-process objects as soon "
			"as they\'re loaded in order to save time when exporting G-code.");
		def.set_default_value(new ConfigOptionBool{ app_config->get("background_processing") == "1" });
		option = Option(def, "background_processing");
		m_optgroup_general->append_single_option_line(option);
#endif
		m_optgroup_general->append_separator();

#ifdef _WIN32
		// Please keep in sync with ConfigWizard
		def.label = L("Associate .3mf files to BambuStudio");
		def.type = coBool;
		def.tooltip = L("If enabled, sets BambuStudio as default application to open .3mf files.");
		def.set_default_value(new ConfigOptionBool(app_config->get("associate_3mf") == "true"));
		option = Option(def, "associate_3mf");
		m_optgroup_general->append_single_option_line(option);

		def.label = L("Associate .stl files to BambuStudio");
		def.type = coBool;
		def.tooltip = L("If enabled, sets BambuStudio as default application to open .stl files.");
		def.set_default_value(new ConfigOptionBool(app_config->get("associate_stl") == "true"));
		option = Option(def, "associate_stl");
		m_optgroup_general->append_single_option_line(option);
#endif // _WIN32
	}
#ifdef _WIN32
	else {
		def.label = L("Associate .gcode files to BambuStudio G-code Viewer");
		def.type = coBool;
		def.tooltip = L("If enabled, sets BambuStudio G-code Viewer as default application to open .gcode files.");
		def.set_default_value(new ConfigOptionBool(app_config->get("associate_gcode") == "true"));
		option = Option(def, "associate_gcode");
		m_optgroup_general->append_single_option_line(option);
	}
#endif // _WIN32
	m_optgroup_general->append_separator();


#ifdef SUPPORT_3D_CONNEXION
#if defined(_WIN32) || defined(__APPLE__)
	def.label = L("Enable support for legacy 3DConnexion devices");
	def.type = coBool;
	def.tooltip = L("If enabled, the legacy 3DConnexion devices settings dialog is available by pressing CTRL+M");
	def.set_default_value(new ConfigOptionBool{ app_config->get("use_legacy_3DConnexion") == "1" });
	option = Option(def, "use_legacy_3DConnexion");
	m_optgroup_general->append_single_option_line(option);
#endif // _WIN32 || __APPLE__
#endif

	// BBS
	def.label = L("Developer Mode");
	def.type = coBool;
	def.tooltip = L("Developer Mode");
	def.set_default_value(new ConfigOptionBool{ app_config->get("user_mode") == "develop" });
	option = Option(def, "developer_mode");
	m_optgroup_general->append_single_option_line(option);

	// BBS: backup
	def.label = L("Backup interval");
	def.type = coInt;
	def.sidetext = L("s");
	def.tooltip = L("Backup interval in seconds, set 0 to disable period backup");
	def.set_default_value(new ConfigOptionInt{ boost::lexical_cast<int>(app_config->has("backup_interval") ? app_config->get("backup_interval") : "10") });
	option = Option(def, "backup_interval");
	m_optgroup_general->append_single_option_line(option);
	def.sidetext.clear();

	activate_options_tab(m_optgroup_general);

	// BBS
	create_select_domain_widget();

	// Add "GUI" tab
	//m_optgroup_gui = create_options_tab(_L("GUI"), tabs);

	if (is_editor) {
#ifdef SUPPORT_COLLAPSE_BUTTON
		def.label = L("Show sidebar collapse/expand button");
		def.type = coBool;
		def.tooltip = L("If enabled, the button for the collapse sidebar will be appeared in top right corner of the 3D Scene");
		def.set_default_value(new ConfigOptionBool{ app_config->get("show_collapse_button") == "1" });
		option = Option(def, "show_collapse_button");
		m_optgroup_gui->append_single_option_line(option);
#endif

		//m_optgroup_gui->append_separator();

#ifdef SUPPORT_SHOW_HINTS
		def.label = L("Show \"Tip of the day\" notification after start");
		def.type = coBool;
		def.tooltip = L("If enabled, useful hints are displayed at startup.");
		def.set_default_value(new ConfigOptionBool{ app_config->get("show_hints") == "1" });
		option = Option(def, "show_hints");
		m_optgroup_gui->append_single_option_line(option);
#endif
	}

	//activate_options_tab(m_optgroup_gui);

#if ENABLE_ENVIRONMENT_MAP
	if (is_editor) {
		// Add "Render" tab
		m_optgroup_render = create_options_tab(_L("Render"), tabs);
		m_optgroup_render->m_on_change = [this](t_config_option_key opt_key, boost::any value) {
			m_values[opt_key] = boost::any_cast<bool>(value) ? "1" : "0";
		};

		def.label = L("Use environment map");
		def.type = coBool;
		def.tooltip = L("If enabled, renders object using the environment map.");
		def.set_default_value(new ConfigOptionBool{ app_config->get("use_environment_map") == "1" });
		option = Option(def, "use_environment_map");
		m_optgroup_render->append_single_option_line(option);

		activate_options_tab(m_optgroup_render);
	}
#endif // ENABLE_ENVIRONMENT_MAP

#ifdef SUPPORT_DARK_MODE
#ifdef _WIN32
	// Add "Dark Mode" tab
	{
		// Add "Dark Mode" tab
		m_optgroup_dark_mode = create_options_tab(_L("Dark mode (experimental)"), tabs);
		m_optgroup_dark_mode->m_on_change = [this](t_config_option_key opt_key, boost::any value) {
			m_values[opt_key] = boost::any_cast<bool>(value) ? "1" : "0";
		};

		def.label = L("Enable dark mode");
		def.type = coBool;
		def.tooltip = L("If enabled, UI will use Dark mode colors. "
			"If disabled, old UI will be used.");
		def.set_default_value(new ConfigOptionBool{ app_config->get("dark_color_mode") == "1" });
		option = Option(def, "dark_color_mode");
		m_optgroup_dark_mode->append_single_option_line(option);

		if (wxPlatformInfo::Get().GetOSMajorVersion() >= 10) // Use system menu just for Window newer then Windows 10
															 // Use menu with ownerdrawn items by default on systems older then Windows 10
		{
			def.label = L("Use system menu for application");
			def.type = coBool;
			def.tooltip = L("If enabled, application will use the standard Windows system menu,\n"
				"but on some combination of display scales it can looks ugly. If disabled, old UI will be used.");
			def.set_default_value(new ConfigOptionBool{ app_config->get("sys_menu_enabled") == "1" });
			option = Option(def, "sys_menu_enabled");
			m_optgroup_dark_mode->append_single_option_line(option);
		}

		activate_options_tab(m_optgroup_dark_mode);
	}
#endif //_WIN32
#endif

	// update alignment of the controls for all tabs
	update_ctrls_alignment();

	if (selected_tab < tabs->GetPageCount())
		tabs->SetSelection(selected_tab);

	auto sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(tabs, 1, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, 5);

	auto buttons = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
	this->Bind(wxEVT_BUTTON, &PreferencesDialog::accept, this, wxID_OK);

	for (int id : {wxID_OK, wxID_CANCEL})
		wxGetApp().UpdateDarkUI(static_cast<wxButton*>(FindWindowById(id, this)));

	sizer->Add(buttons, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM | wxTOP, 10);

	SetSizer(sizer);
	sizer->SetSizeHints(this);
	this->CenterOnParent();
}

std::vector<ConfigOptionsGroup*> PreferencesDialog::optgroups()
{
	std::vector<ConfigOptionsGroup*> out;
	out.reserve(4);
	for (ConfigOptionsGroup* opt : { m_optgroup_general.get()
		})
		if (opt)
			out.emplace_back(opt);
	return out;
}

void PreferencesDialog::update_ctrls_alignment()
{
	int max_ctrl_width{ 0 };
	for (ConfigOptionsGroup* og : this->optgroups())
		if (int max = og->custom_ctrl->get_max_win_width();
			max_ctrl_width < max)
			max_ctrl_width = max;
	if (max_ctrl_width)
		for (ConfigOptionsGroup* og : this->optgroups())
			og->custom_ctrl->set_max_win_width(max_ctrl_width);
}

void PreferencesDialog::accept(wxEvent&)
{
    auto app_config = get_app_config();

	//BBS domain changed
	m_domain_changed = false;
	for (const std::string& key : { "api_dev_domain",
									"api_rel_domain" })
	{
		auto it = m_values.find(key);
		if (it != m_values.end() && app_config->get(key) != it->second) {
			m_domain_changed = true;
			break;
		}
	}

	if (m_domain_changed) {
		AccountManager* manager = wxGetApp().getAccountManager();
		manager->user_logout();
		if (m_values["api_dev_domain"].compare("true") == 0) {
			manager->set_host(DEFAULT_HOST);
		}
		else if (m_values["api_rel_domain"].compare("true") == 0) {
			manager->set_host("https://api.bambulab.com");
		}
	}

	//BBS
	m_develop_mode_changed = false;
	if (auto it = m_values.find("developer_mode"); it != m_values.end()) {
		m_develop_mode_changed = !app_config->get("developer_mode").empty() || app_config->get("developer_mode") != it->second;
		if (m_develop_mode_changed) {
			if (it->second.compare("true") == 0 || it->second.compare("1") == 0) {
				Slic3r::GUI::wxGetApp().save_mode(comDevelop);
			}
			else {
				Slic3r::GUI::wxGetApp().save_mode(comAdvanced);
			}
		}
	}

	//BBS backup
	bool backup_interval_changed = false;
	if (auto it = m_values.find("backup_interval"); it != m_values.end()) {
		backup_interval_changed = app_config->get("backup_interval") != it->second;
		if (backup_interval_changed) {
			Slic3r::set_backup_interval(boost::lexical_cast<long>(it->second));
		}
	}

#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
	m_seq_top_gcode_indices_changed = false;
	if (auto it = m_values.find("seq_top_gcode_indices"); it != m_values.end())
		m_seq_top_gcode_indices_changed = app_config->get("seq_top_gcode_indices") != it->second;
#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER

#if 0 //#ifdef _WIN32 // #ysDarkMSW - Allow it when we deside to support the sustem colors for application
	if (m_values.find("always_dark_color_mode") != m_values.end())
		wxGetApp().force_sys_colors_update();
#endif

	for (std::map<std::string, std::string>::iterator it = m_values.begin(); it != m_values.end(); ++it)
		app_config->set(it->first, it->second);

	app_config->save();

	EndModal(wxID_OK);

	//BBS GUI refactor: remove unuse layout logic
#ifdef _WIN32
#ifdef SUPPORT_DARK_MODE
	if (m_values.find("dark_color_mode") != m_values.end())
		wxGetApp().force_colors_update();
#endif
#ifdef _MSW_DARK_MODE
	if (m_values.find("sys_menu_enabled") != m_values.end())
		wxGetApp().force_menu_update();
#endif //_MSW_DARK_MODE
#endif // _WIN32
}

void PreferencesDialog::on_dpi_changed(const wxRect &suggested_rect)
{
	for (ConfigOptionsGroup* og : this->optgroups())
		og->msw_rescale();

    msw_buttons_rescale(this, em_unit(), { wxID_OK, wxID_CANCEL });

    layout();
}

void PreferencesDialog::layout()
{
    const int em = em_unit();

    SetMinSize(wxSize(47 * em, 28 * em));
    Fit();

    Refresh();
}

void PreferencesDialog::init_highlighter(const t_config_option_key& opt_key)
{
	m_highlighter.set_timer_owner(this, 0);
	this->Bind(wxEVT_TIMER, [this](wxTimerEvent&)
		{
			m_highlighter.blink();
		});

	std::pair<OG_CustomCtrl*, bool*> ctrl = { nullptr, nullptr };
	for (ConfigOptionsGroup* opt_group : this->optgroups()) {
		ctrl = opt_group->get_custom_ctrl_with_blinking_ptr(opt_key, -1);
		if (ctrl.first && ctrl.second) {
			m_highlighter.init(ctrl);
			break;
		}
	}
}

void PreferencesDialog::PreferencesHighlighter::set_timer_owner(wxEvtHandler* owner, int timerid/* = wxID_ANY*/)
{
	m_timer.SetOwner(owner, timerid);
}

void PreferencesDialog::PreferencesHighlighter::init(std::pair<OG_CustomCtrl*, bool*> params)
{
	if (m_timer.IsRunning())
		invalidate();
	if (!params.first || !params.second)
		return;

	m_timer.Start(300, false);

	m_custom_ctrl = params.first;
	m_show_blink_ptr = params.second;

	*m_show_blink_ptr = true;
	m_custom_ctrl->Refresh();
}

void PreferencesDialog::PreferencesHighlighter::invalidate()
{
	m_timer.Stop();

	if (m_custom_ctrl && m_show_blink_ptr) {
		*m_show_blink_ptr = false;
		m_custom_ctrl->Refresh();
		m_show_blink_ptr = nullptr;
		m_custom_ctrl = nullptr;
	}

	m_blink_counter = 0;
}

void PreferencesDialog::PreferencesHighlighter::blink()
{
	if (m_custom_ctrl && m_show_blink_ptr) {
		*m_show_blink_ptr = !*m_show_blink_ptr;
		m_custom_ctrl->Refresh();
	}
	else
		return;

	if ((++m_blink_counter) == 11)
		invalidate();
}

//BBS
void PreferencesDialog::create_select_domain_widget()
{
	wxString choices[] = { _L("host: api-qa.bambu-lab.com/v2(develop)"),
						   _L("host: api.bambulab.com(release)") };

	auto app_config = get_app_config();
	int selection = app_config->get("api_dev_domain") == "1" ? 0 :
		app_config->get("api_rel_domain") == "1" ? 1 : 0;

	wxWindow* parent = m_optgroup_general->parent();

	m_select_domain_box = new wxRadioBox(parent, wxID_ANY, _L("Select Domain"), wxDefaultPosition, wxDefaultSize,
		WXSIZEOF(choices), choices, 2, wxRA_SPECIFY_ROWS);
	m_select_domain_box->SetFont(wxGetApp().normal_font());
	m_select_domain_box->SetSelection(selection);

	m_select_domain_box->Bind(wxEVT_RADIOBOX, [this](wxCommandEvent& e) {
		int selection = e.GetSelection();
		m_values["api_dev_domain"] = boost::any_cast<bool>(selection == 0) ? "1" : "0";
		m_values["api_rel_domain"] = boost::any_cast<bool>(selection == 1) ? "1" : "0";
		});

	auto sizer = new wxBoxSizer(wxHORIZONTAL);
	sizer->Add(m_select_domain_box, 1, wxALIGN_CENTER_VERTICAL);
	m_optgroup_general->sizer->Add(sizer, 0, wxEXPAND);
}

} // GUI
} // Slic3r
