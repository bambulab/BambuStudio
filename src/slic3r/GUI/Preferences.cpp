#include "Preferences.hpp"
#include "OptionsGroup.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "Plater.hpp"
#include "MsgDialog.hpp"
#include "I18N.hpp"
#include "libslic3r/AppConfig.hpp"
#include <wx/notebook.h>
#include "Notebook.hpp"
#include "OG_CustomCtrl.hpp"
#include "wx/graphics.h"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/RadioBox.hpp"
#include "Widgets/TextInput.hpp"
#include <wx/listimpl.cpp>
#include <map>

namespace Slic3r { namespace GUI {

WX_DEFINE_LIST(RadioSelectorList);

// @class:  PreferencesDialog
// @ret:    items
// @birth:  created by onion
wxWindow *PreferencesDialog::create_item_title(wxString title, wxWindow *parent, wxString tooltip)
{
    wxWindow *item = new wxWindow(parent, wxID_ANY, wxDefaultPosition, wxSize(parent->GetSize().GetWidth() - 126, 20));
    item->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    wxStaticText *text = new wxStaticText(item, wxID_ANY, title, wxDefaultPosition, wxDefaultSize);
    text->SetForegroundColour(wxColor(50, 58, 61));
    text->SetFont(wxGetApp().bold_font());
    wxStaticLine *line = new wxStaticLine(item, wxID_ANY, wxPoint(text->GetSize().GetWidth() + 7, text->GetSize().GetHeight() / 2), wxSize(item->GetSize().GetWidth(), 1),
                                          wxLI_HORIZONTAL);
    text->SetToolTip(tooltip);
    return item;
}

wxWindow *PreferencesDialog::create_item_combobox(wxString title, wxWindow *parent, wxString tooltip, int padding_left, std::string param, std::vector<wxString> vlist)
{
    wxWindow *item = new wxWindow(parent, wxID_ANY, wxDefaultPosition, wxSize(parent->GetSize().GetWidth() - 126, 30));
    item->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    wxStaticText *text = new wxStaticText(item, wxID_ANY, title, wxDefaultPosition, wxDefaultSize);
    text->SetPosition(wxPoint(padding_left, (item->GetSize().GetHeight() - text->GetSize().GetHeight()) / 2));

    auto                            combobox = new ::ComboBox(item, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(150, 24), 0, nullptr, wxCB_READONLY);
    std::vector<wxString>::iterator iter;
    for (iter = vlist.begin(); iter != vlist.end(); iter++) { combobox->Append(*iter); }

    combobox->SetPosition(wxPoint(item->GetSize().GetWidth() - combobox->GetSize().GetWidth() - 60, (item->GetSize().GetHeight() - combobox->GetSize().GetHeight()) / 2));
    combobox->SetValue(app_config->get(param));

    // save config
    combobox->GetDropDown().Bind(wxEVT_COMBOBOX, [this, param](wxCommandEvent &e) {
        app_config->set(param, std::string(e.GetString().mb_str()));
        app_config->save();
        e.Skip();
    });

    text->SetToolTip(tooltip);
    return item;
}

wxWindow *PreferencesDialog::create_item_language_combobox(
    wxString title, wxWindow *parent, wxString tooltip, int padding_left, std::string param, std::vector<const wxLanguageInfo *> vlist)
{
    wxWindow *item = new wxWindow(parent, wxID_ANY, wxDefaultPosition, wxSize(parent->GetSize().GetWidth() - 126, 30));
    item->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    wxStaticText *text = new wxStaticText(item, wxID_ANY, title, wxDefaultPosition, wxDefaultSize);
    text->SetPosition(wxPoint(padding_left, (item->GetSize().GetHeight() - text->GetSize().GetHeight()) / 2));

    // app_config->set("language", "zh_CN");
    // app_config->save();
    auto current_language = app_config->get(param);
    auto combobox         = new ::ComboBox(item, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(150, 24), 0, nullptr, wxCB_READONLY);

    for (size_t i = 0; i < vlist.size(); ++i) {
        combobox->Append(vlist[i]->Description);
        if (current_language == vlist[i]->CanonicalName) { combobox->SetValue(vlist[i]->Description); }
    }
    combobox->SetPosition(wxPoint(item->GetSize().GetWidth() - combobox->GetSize().GetWidth() - 60, (item->GetSize().GetHeight() - combobox->GetSize().GetHeight()) / 2));

    // save config
    combobox->Bind(wxEVT_COMBOBOX, [this, param, vlist](wxCommandEvent &e) {
        if (e.GetString().mb_str() != app_config->get(param)) {
            {
                // the dialog needs to be destroyed before the call to switch_language()
                // or sometimes the application crashes into wxDialogBase() destructor
                // so we put it into an inner scope
                wxString title = _L("Language selection");
                wxMessageDialog dialog(nullptr,
                    _L("Switching the language requires application restart.\n") + "\n\n" +
                    _L("Do you want to continue?"),
                    title,
                    wxICON_QUESTION | wxOK | wxCANCEL);
                if (dialog.ShowModal() == wxID_CANCEL)
                    return;
            }
            auto check = [this](bool yes_or_no) {
                //if (yes_or_no)
                //    return true;
                int act_btns = UnsavedChangesDialog::ActionButtons::SAVE;
                return wxGetApp().check_and_keep_current_preset_changes(_L("Switching application language"), _L("Switching application language while some presets are modified."), act_btns);
            };
            if (wxGetApp().plater()->close_with_confirm(check) == wxID_CANCEL) {
                wxString name = app_config->get(param);
                for (size_t i = 0; i < vlist.size(); ++i) {
                    if (name == vlist[i]->CanonicalName) {
                        dynamic_cast<ComboBox *>(e.GetEventObject())->SetLabel(vlist[i]->Description);
                        break;
                    }
                }
                return;
            }
            for (size_t i = 0; i < vlist.size(); ++i) {
                if (e.GetString().mb_str() == vlist[i]->Description) {
                    app_config->set(param, vlist[i]->CanonicalName.ToUTF8().data());
                    app_config->save();

                    wxGetApp().load_language(vlist[i]->CanonicalName, false);
                    Close();
                    wxGetApp().recreate_GUI(_L("Changing application language"));
                    break;
                }
            }
        }

        e.Skip();
    });

    text->SetToolTip(tooltip);
    return item;
}

wxWindow *PreferencesDialog::create_item_multiple_combobox(
    wxString title, wxWindow *parent, wxString tooltip, int padding_left, std::string param, std::vector<wxString> vlista, std::vector<wxString> vlistb)
{
    wxWindow *item = new wxWindow(parent, wxID_ANY, wxDefaultPosition, wxSize(parent->GetSize().GetWidth() - 80, 30));
    item->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    wxStaticText *text = new wxStaticText(item, wxID_ANY, title, wxDefaultPosition, wxDefaultSize);
    text->SetPosition(wxPoint(padding_left, (item->GetSize().GetHeight() - text->GetSize().GetHeight()) / 2));

    std::vector<wxString> params;
    Split(app_config->get(param), "/", params);

    std::vector<wxString>::iterator iter;
    auto                            combobox_right = new ::ComboBox(item, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(140, 24), 0, nullptr, wxCB_READONLY);
    for (iter = vlistb.begin(); iter != vlistb.end(); iter++) { combobox_right->Append(*iter); }

    auto pad_right = item->GetSize().GetWidth() - combobox_right->GetSize().GetWidth();

    combobox_right->SetPosition(wxPoint(pad_right, (item->GetSize().GetHeight() - combobox_right->GetSize().GetHeight()) / 2));
    combobox_right->SetValue(std::string(params[1].mb_str()));

    wxStaticText *plus = new wxStaticText(item, wxID_ANY, wxString(" + "), wxDefaultPosition, wxDefaultSize);
    pad_right -= plus->GetSize().GetWidth();
    plus->SetPosition(wxPoint(pad_right, (item->GetSize().GetHeight() - plus->GetSize().GetHeight()) / 2));

    auto combobox_left = new ::ComboBox(item, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(100, 24), 0, nullptr, wxCB_READONLY);
    for (iter = vlista.begin(); iter != vlista.end(); iter++) { combobox_left->Append(*iter); }

    pad_right -= combobox_left->GetSize().GetWidth();

    combobox_left->SetPosition(wxPoint(pad_right, (item->GetSize().GetHeight() - combobox_left->GetSize().GetHeight()) / 2));
    combobox_left->SetValue(std::string(params[0].mb_str()));

    // save config
    combobox_left->GetDropDown().Bind(wxEVT_COMBOBOX, [this, param, combobox_right](wxCommandEvent &e) {
        auto config = e.GetString() + wxString("/") + combobox_right->GetValue();
        app_config->set(param, std::string(config.mb_str()));
        app_config->save();
        e.Skip();
    });

    combobox_right->GetDropDown().Bind(wxEVT_COMBOBOX, [this, param, combobox_left](wxCommandEvent &e) {
        auto config = combobox_left->GetValue() + wxString("/") + e.GetString();
        app_config->set(param, std::string(config.mb_str()));
        app_config->save();
        e.Skip();
    });

    text->SetToolTip(tooltip);
    return item;
}

wxWindow *PreferencesDialog::create_item_checkbox(wxString title, wxWindow *parent, wxString tooltip, int padding_left, std::string param)
{
    wxWindow *item = new wxWindow(parent, wxID_ANY, wxDefaultPosition, wxSize(parent->GetSize().GetWidth() - 126, 30));
    item->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));

    auto checkbox = new ::CheckBox(item);
    checkbox->SetPosition(wxPoint(padding_left, (item->GetSize().GetHeight() - checkbox->GetSize().GetHeight()) / 2));
    auto a = app_config->get(param);
    checkbox->SetValue((app_config->get(param) == "true") ? true : false);

    wxStaticText *text = new wxStaticText(item, wxID_ANY, title, wxDefaultPosition, wxDefaultSize);
    text->SetPosition(wxPoint(padding_left + checkbox->GetSize().GetWidth() + 10, (item->GetSize().GetHeight() - text->GetSize().GetHeight()) / 2 + 1));

    // save config
    checkbox->Bind(wxEVT_TOGGLEBUTTON, [this, checkbox, param](wxCommandEvent &e) {
        app_config->set_bool(param, checkbox->GetValue());
        app_config->save();
        e.Skip();
    });

    //for debug mode
    if (param == "developer_mode") { m_developer_mode_ckeckbox = checkbox;}
    if (param == "dump_video") { m_dump_video_ckeckbox = checkbox;}

    checkbox->SetToolTip(tooltip);
    text->SetToolTip(tooltip);
    return item;
}

wxWindow *PreferencesDialog::create_item_input(wxString title, wxWindow *parent, wxString tooltip, int padding_left, std::string param)
{
    wxWindow *item = new wxWindow(parent, wxID_ANY, wxDefaultPosition, wxSize(parent->GetSize().GetWidth() - 126, 30));
    item->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    wxStaticText *text = new wxStaticText(item, wxID_ANY, title, wxDefaultPosition, wxDefaultSize);
    text->SetPosition(wxPoint(padding_left, (item->GetSize().GetHeight() - text->GetSize().GetHeight()) / 2));

    wxStaticText *s = new wxStaticText(item, wxID_ANY, wxString("S"), wxDefaultPosition, wxDefaultSize);
    s->SetPosition(wxPoint(item->GetSize().GetWidth() - s->GetSize().GetWidth() - 5, (item->GetSize().GetHeight() - text->GetSize().GetHeight()) / 2));

    auto input = new ::TextInput(item, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxSize(80, 28));
    input->GetTextCtrl()->SetValue(app_config->get(param));
    input->SetPosition(wxPoint(item->GetSize().GetWidth() - input->GetSize().GetWidth() - 20, (item->GetSize().GetHeight() - input->GetSize().GetHeight()) / 2));

    // save config
    input->GetTextCtrl()->Bind(wxEVT_COMMAND_TEXT_UPDATED, [this, param, input](wxCommandEvent &e) {
        m_backup_interval_time = input->GetTextCtrl()->GetValue();
        e.Skip();
    });

    //for debug mode
    if (param == "backup_interval") {
        m_backup_interval_textinput = input;
    }

    text->SetToolTip(tooltip);
    return item;
}

wxWindow *PreferencesDialog ::create_item_radiobox(wxString title, wxWindow *parent, wxString tooltip, int padding_left, int groupid, std::string param)
{
    wxWindow *item = new wxWindow(parent, wxID_ANY, wxDefaultPosition, wxSize(parent->GetSize().GetWidth() - 126, 30));
    item->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));

    RadioBox *radiobox = new RadioBox(item);
    radiobox->SetPosition(wxPoint(padding_left, (item->GetSize().GetHeight() - radiobox->GetSize().GetHeight()) / 2));
    radiobox->Bind(wxEVT_LEFT_DOWN, &PreferencesDialog::OnSelectRadio, this);

    RadioSelector *rs = new RadioSelector;
    rs->m_groupid     = groupid;
    rs->m_param_name  = param;
    rs->m_radiobox    = radiobox;
    rs->m_selected    = false;
    m_radio_group.Append(rs);

    wxStaticText *text = new wxStaticText(item, wxID_ANY, title, wxDefaultPosition, wxDefaultSize);
    text->SetPosition(wxPoint(padding_left + radiobox->GetSize().GetWidth() + 10, (item->GetSize().GetHeight() - text->GetSize().GetHeight()) / 2));

    radiobox->SetToolTip(tooltip);
    text->SetToolTip(tooltip);
    return item;
}

// @class:  PreferencesDialog
// @ret:    Preferences Dialog
// @birth:  created by onion
PreferencesDialog::PreferencesDialog(wxWindow *parent, wxWindowID id, const wxString &title, const wxPoint &pos, const wxSize &size, long style)
    : DPIDialog(parent, id, _L("Preferences"), pos, size, style)
{
    SetBackgroundColour(DESIGN_SELECTOR_SELECTED_COLOR);
    SetFont(wxGetApp().normal_font());
    Init();
}

void PreferencesDialog::Init()
{
    app_config             = get_app_config();
    m_backup_interval_time = app_config->get("backup_interval");

    Bind(wxEVT_PAINT, &PreferencesDialog::OnPaint, this);

    // set icon for dialog
    std::string icon_path = (boost::format("%1%/icons/BambuStudio.ico") % resources_dir()).str();
    // std::string icon_path = resources_dir() + "", resources_dir();
    SetIcon(wxIcon(icon_path, wxBITMAP_TYPE_ICO));

    // init data
    m_fgSizer_main = new wxBoxSizer(wxVERTICAL);

    m_main_line = new wxStaticLine(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL);
    m_fgSizer_main->Add(m_main_line, 0, wxEXPAND);

    m_fgSizer_body = new wxFlexGridSizer(0, 2, 0, 0);
    m_fgSizer_body->SetFlexibleDirection(wxBOTH);
    m_fgSizer_body->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);

    m_panel_selects = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(125, 430), wxTAB_TRAVERSAL);
    m_panel_selects->SetBackgroundColour(DESIGN_SELECTOR_NOMORE_COLOR);

    m_bSizer_selects = new wxBoxSizer(wxVERTICAL);

    m_panel_selects->SetSizer(m_bSizer_selects);
    m_panel_selects->Layout();
    m_fgSizer_body->Add(m_panel_selects, 1, wxEXPAND, 0);

    m_panel_content = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(493, 430), wxTAB_TRAVERSAL);
    m_panel_content->SetBackgroundColour(DESIGN_SELECTOR_SELECTED_COLOR);

    // general tab
    m_panel_general = new wxPanel(m_panel_content, wxID_ANY, wxDefaultPosition, m_panel_content->GetSize());
    m_panel_general->SetBackgroundColour(DESIGN_SELECTOR_SELECTED_COLOR);
    m_panel_general->Hide();
    create_select_tabel(_L("General"), 0, 20, m_panel_general);

    // gui tab
    m_panel_gui = new wxPanel(m_panel_content, wxID_ANY, wxDefaultPosition, m_panel_content->GetSize());
    m_panel_gui->SetBackgroundColour(DESIGN_SELECTOR_SELECTED_COLOR);
    m_panel_gui->Hide();
    create_select_tabel(_L("GUI"), 1, 0, m_panel_gui);

    // sync tab
    m_panel_sync = new wxPanel(m_panel_content, wxID_ANY, wxDefaultPosition, m_panel_content->GetSize());
    m_panel_sync->SetBackgroundColour(DESIGN_SELECTOR_SELECTED_COLOR);
    m_panel_sync->Hide();
    create_select_tabel(_L("Sync"), 2, 0, m_panel_sync);

    // shortcuts tab
    m_panel_shortcuts = new wxPanel(m_panel_content, wxID_ANY, wxDefaultPosition, m_panel_content->GetSize());
    m_panel_shortcuts->SetBackgroundColour(DESIGN_SELECTOR_SELECTED_COLOR);
    m_panel_shortcuts->Hide();
    create_select_tabel(_L("Shortcuts"), 3, 0, m_panel_shortcuts);

    // debug tab
    m_panel_debug = new wxPanel(m_panel_content, wxID_ANY, wxDefaultPosition, m_panel_content->GetSize());
    m_panel_debug->SetBackgroundColour(DESIGN_SELECTOR_SELECTED_COLOR);
    m_panel_debug->Hide();
    create_select_tabel(_L("Debug"), 4, 0, m_panel_debug);

    // create all page
    create_general_page();
    create_gui_page();
    create_sync_page();
    create_shortcuts_page();
    create_debug_page();

    m_fgSizer_body->Add(m_panel_content, 1, wxLEFT, 38);
    m_fgSizer_main->Add(m_fgSizer_body, 1, wxEXPAND, 0);

    this->SetSizer(m_fgSizer_main);
    this->Layout();
    this->CenterOnParent();

    on_select(0);
}

// void PreferencesDialog::ShowModal()
//{
//    this->Show();
//}

PreferencesDialog::~PreferencesDialog() { m_hash_selector.clear(); }

void PreferencesDialog::OnPaint(wxPaintEvent &e) {}

void PreferencesDialog::on_dpi_changed(const wxRect &suggested_rect) { this->Refresh(); }

void PreferencesDialog::create_select_tabel(const wxString &title, int id, int topleft, wxPanel *slpanel)
{
    wxWindow *bk = new wxWindow(m_panel_selects, wxID_ANY, wxDefaultPosition, wxSize(m_panel_selects->GetSize().GetWidth(), 28), 0);
    bk->SetBackgroundColour(DESIGN_SELECTOR_NOMORE_COLOR);
    bk->Bind(wxEVT_LEFT_DOWN, &PreferencesDialog::OnSelectTabel, this);

    wxStaticText *text = new wxStaticText(bk, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL);

    text->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &event) {
        SelectorHash::iterator i = m_hash_selector.begin();
        while (i != m_hash_selector.end()) {
            Selector *sel = i->second;
            if (sel->m_seltext->GetId() == event.GetId()) { on_select(i->first); }
            i++;
        }
    });

    // adjust position
    auto offwidth  = bk->GetSize().GetWidth() - text->GetSize().GetWidth();
    auto offheight = bk->GetSize().GetHeight() - text->GetSize().GetHeight() + 4;
    text->SetPosition(wxPoint(offwidth / 2, offheight / 2));
    text->SetBackgroundColour(DESIGN_SELECTOR_NOMORE_COLOR);

    m_bSizer_selects->Add(bk, 0, wxTOP, topleft);

    Selector *sel   = new Selector;
    sel->m_index    = id;
    sel->m_selpanel = bk;
    sel->m_seltext  = text;
    sel->m_conpanel = slpanel;

    m_hash_selector[sel->m_index] = sel;
}

void PreferencesDialog ::on_select(int index)
{
    if (index > m_hash_selector.size()) { return; }
    SelectorHash::iterator cur    = m_hash_selector.find(index);
    Selector *             cursel = cur->second;

    SelectorHash::iterator i = m_hash_selector.begin();
    while (i != m_hash_selector.end()) {
        Selector *sel = i->second;
        sel->m_selpanel->SetOwnBackgroundColour(DESIGN_SELECTOR_NOMORE_COLOR);
        sel->m_selpanel->Refresh();

        sel->m_seltext->SetOwnBackgroundColour(DESIGN_SELECTOR_NOMORE_COLOR);
        // sel->seltext->SetFont(wxGetApp().normal_font());
        sel->m_conpanel->Hide();
        i++;
    }

    cursel->m_selpanel->SetOwnBackgroundColour(DESIGN_SELECTOR_SELECTED_COLOR);
    cursel->m_selpanel->Refresh();

    cursel->m_seltext->SetOwnBackgroundColour(DESIGN_SELECTOR_SELECTED_COLOR);
    // cursel->seltext->SetFont(wxGetApp().bold_font());
    cursel->m_conpanel->Show();
}

void PreferencesDialog::Split(const std::string &src, const std::string &separator, std::vector<wxString> &dest)
{
    std::string            str = src;
    std::string            substring;
    std::string::size_type start = 0, index;
    dest.clear();
    index = str.find_first_of(separator, start);
    do {
        if (index != std::string::npos) {
            substring = str.substr(start, index - start);
            dest.push_back(substring);
            start = index + separator.size();
            index = str.find(separator, start);
            if (start == std::string::npos) break;
        }
    } while (index != std::string::npos);

    substring = str.substr(start);
    dest.push_back(substring);
}

void PreferencesDialog::create_general_page()
{
    wxBoxSizer *bSizer;
    bSizer = new wxBoxSizer(wxVERTICAL);

    wxWindow *title_general_settings = create_item_title(_L("General settings"), m_panel_general, _L("General settings"));

    wxArrayString                       translations = wxTranslations::Get()->GetAvailableTranslations(SLIC3R_APP_KEY);
    std::vector<const wxLanguageInfo *> language_infos;
    language_infos.emplace_back(wxLocale::GetLanguageInfo(wxLANGUAGE_ENGLISH));
    for (size_t i = 0; i < translations.GetCount(); ++i) {
        const wxLanguageInfo *langinfo = wxLocale::FindLanguageInfo(translations[i]);
        if (langinfo != nullptr) language_infos.emplace_back(langinfo);
    }
    sort_remove_duplicates(language_infos);
    std::sort(language_infos.begin(), language_infos.end(), [](const wxLanguageInfo *l, const wxLanguageInfo *r) { return l->Description < r->Description; });
    wxWindow *item_language = create_item_language_combobox(_L("Language"), m_panel_general, _L("Language"), 50, "language", language_infos);

    std::vector<wxString> currency_supported;
    Split(app_config->get("currency_supported"), "/", currency_supported);
    wxWindow *item_currency = create_item_combobox(_L("Currency"), m_panel_general, _L("Currency"), 50, "currency", currency_supported);

    wxWindow *title_associate_file = create_item_title(_L("Associate files to BambuStudio"), m_panel_general, _L("Associate files to BambuStudio"));

    // associate file
    wxWindow *item_associate_3mf  = create_item_checkbox(_L("Associate .3mf files to BambuStudio"), m_panel_general,
                                                        _L("If enabled, sets BambuStudio as default application to open .3mf files"), 50, "associate_3mf");
    wxWindow *item_associate_stl  = create_item_checkbox(_L("Associate .stl files to BambuStudio"), m_panel_general,
                                                        _L("If enabled, sets BambuStudio as default application to open .stl files"), 50, "associate_stl");
    wxWindow *item_associate_step = create_item_checkbox(_L("Associate .step files to BambuStudio"), m_panel_general,
                                                         _L("If enabled, sets BambuStudio as default application to open .step files"), 50, "associate_step");

    bSizer->Add(title_general_settings, 0, wxTOP, 26);
    bSizer->Add(item_language, 0, wxTOP, 5);
    bSizer->Add(item_currency, 0, wxTOP, 0);
    bSizer->Add(title_associate_file, 0, wxTOP, 20);
    bSizer->Add(item_associate_3mf, 0, wxTOP, 5);
    bSizer->Add(item_associate_stl, 0, wxTOP, 5);
    bSizer->Add(item_associate_step, 0, wxTOP, 5);

    m_panel_general->SetSizer(bSizer);
    bSizer->Fit(m_panel_general);
}

void PreferencesDialog::create_gui_page()
{
    wxBoxSizer *bSizer;
    bSizer = new wxBoxSizer(wxVERTICAL);

    // add item to page
    wxWindow *title_index_and_tip = create_item_title(_L("Home page and daily tips"), m_panel_gui, _L("Home page and daily tips"));
    wxWindow *item_home_page      = create_item_checkbox(_L("Show home page on startup"), m_panel_gui, _L("Show home page on startup"), 50, "show_home_page");
    wxWindow *item_daily_tip      = create_item_checkbox(_L("Show daily tip on startup"), m_panel_gui, _L("Show daily tip on startup"), 50, "show_daily_tips");

    bSizer->Add(title_index_and_tip, 0, wxTOP, 26);
    bSizer->Add(item_home_page, 0, wxTOP, 5);
    bSizer->Add(item_daily_tip, 0, wxTOP, 0);

    m_panel_gui->SetSizer(bSizer);
    bSizer->Fit(m_panel_gui);
}

void PreferencesDialog::create_sync_page()
{
    wxBoxSizer *bSizer;
    bSizer = new wxBoxSizer(wxVERTICAL);

    // add item to page
    wxWindow *title_sync_settingy   = create_item_title(_L("Sync settings"), m_panel_sync, _L("Sync settings"));
    wxWindow *item_user_sync        = create_item_checkbox(_L("User sync"), m_panel_sync, _L("User sync"), 50, "user_sync_switch");
    wxWindow *item_preset_sync      = create_item_checkbox(_L("Preset sync"), m_panel_sync, _L("Preset sync"), 50, "preset_sync_switch");
    wxWindow *item_preferences_sync = create_item_checkbox(_L("Preferences sync"), m_panel_sync, _L("Preferences sync"), 50, "preferences_sync_switch");

    bSizer->Add(title_sync_settingy, 0, wxTOP, 26);
    bSizer->Add(item_user_sync, 0, wxTOP, 5);
    bSizer->Add(item_preset_sync, 0, wxTOP, 0);
    bSizer->Add(item_preferences_sync, 0, wxTOP, 0);

    m_panel_sync->SetSizer(bSizer);
    bSizer->Fit(m_panel_sync);
}

void PreferencesDialog::create_shortcuts_page()
{
    wxBoxSizer *bSizer;
    bSizer = new wxBoxSizer(wxVERTICAL);

    // add item to page
    wxWindow *            title_view_control = create_item_title(_L("View control settings"), m_panel_shortcuts, _L("View control settings"));
    std::vector<wxString> keyboard_supported;
    Split(app_config->get("keyboard_supported"), "/", keyboard_supported);

    std::vector<wxString> mouse_supported;
    Split(app_config->get("mouse_supported"), "/", mouse_supported);

    wxWindow *item_rotate_view     = create_item_multiple_combobox(_L("Rotate of view"), m_panel_shortcuts, _L("Rotate of view"), 10, "rotate_view", keyboard_supported,
                                                               mouse_supported);
    wxWindow *item_move_view   = create_item_multiple_combobox(_L("Move of view"), m_panel_shortcuts, _L("Move of view"), 10, "move_view", keyboard_supported, mouse_supported);
    wxWindow *item_zoom_view = create_item_multiple_combobox(_L("Zoom of view"), m_panel_shortcuts, _L("Zoom of view"), 10, "rotate_view", keyboard_supported, mouse_supported);
    wxWindow *item_precise_control = create_item_multiple_combobox(_L("Precise of control"), m_panel_shortcuts, _L("Precise of control"), 10, "precise_control",
                                                                   keyboard_supported, mouse_supported);

    wxWindow *title_other = create_item_title(_L("Other"), m_panel_shortcuts, _L("Other"));
    wxWindow *item_other  = create_item_checkbox(_L("Mouse wheel reverses when zooming"), m_panel_shortcuts, _L("Mouse wheel reverses when zooming"), 50, "mouse_wheel");

    bSizer->Add(title_view_control, 0, wxTOP, 26);
    bSizer->Add(item_rotate_view, 0, wxTOP, 5);
    bSizer->Add(item_move_view, 0, wxTOP, 0);
    bSizer->Add(item_zoom_view, 0, wxTOP, 0);
    bSizer->Add(item_precise_control, 0, wxTOP, 0);
    bSizer->Add(title_other, 0, wxTOP, 20);
    bSizer->Add(item_other, 0, wxTOP, 5);

    m_panel_shortcuts->SetSizer(bSizer);
    bSizer->Fit(m_panel_shortcuts);
}

void PreferencesDialog::create_debug_page()
{

    m_developer_mode_def = app_config->get("developer_mode");
    m_dump_video_def = app_config->get("dump_video");
    m_backup_interval_def = app_config->get("backup_interval");
    m_iot_environment_def= app_config->get("iot_environment");

    wxBoxSizer *bSizer;
    bSizer = new wxBoxSizer(wxVERTICAL);

    wxWindow *title_develop_mode   = create_item_title(_L("Develop mode"), m_panel_debug, _L("Develop mode"));
    wxWindow *item_develop_mode    = create_item_checkbox(_L("Develop mode"), m_panel_debug, _L("Develop mode"), 50, "developer_mode");
    wxWindow *item_dump_video      = create_item_checkbox(_L("Dump video"), m_panel_debug, _L("Dump video"), 50, "dump_video");
    wxWindow *item_backup_interval = create_item_input(_L("Backup interval"), m_panel_debug, _L("Backup interval"), 50, "backup_interval");

    auto radio1 = create_item_radiobox(_L("DEV host: api-dev.bambu-lab.com/v1"), m_panel_debug, wxEmptyString, 50, 1, "dev_host");
    auto radio2 = create_item_radiobox(_L("QA  host: api-qa.bambu-lab.com/v1"), m_panel_debug, wxEmptyString, 50, 1, "qa_host");
    auto radio3 = create_item_radiobox(_L("PRE host: api-pre.bambu-lab.com/v1"), m_panel_debug, wxEmptyString, 50, 1, "pre_host");



    if (m_iot_environment_def == "0") {
        on_select_radio("dev_host");
    } else if (m_iot_environment_def == "1") {
        on_select_radio("qa_host");
    } else if (m_iot_environment_def == "2") {
        on_select_radio("pre_host");
    }

    wxButton *debug_button = new wxButton(m_panel_debug, wxID_ANY, _L("debug save button"), wxDefaultPosition, wxDefaultSize, 0);
    debug_button->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) {
        // success message box
        wxMessageDialog dialog(this, _L("save debug settings"), _L("DEBUG settings have saved successfully!"), wxNO_DEFAULT | wxYES_NO | wxICON_INFORMATION);
        switch (dialog.ShowModal()) {
        case wxID_NO: {
            if (m_developer_mode_def != app_config->get("developer_mode")) {
                app_config->set_bool("developer_mode", m_developer_mode_def == "true"?true: false);
                m_developer_mode_ckeckbox->SetValue(m_developer_mode_def == "true" ? true : false);
            }
            if (m_dump_video_def != app_config->get("dump_video")) {
                app_config->set_bool("dump_video", m_dump_video_def == "true"?true: false);
                m_dump_video_ckeckbox->SetValue(m_dump_video_def == "true" ? true : false);
            }

            if (m_backup_interval_def != m_backup_interval_time) {
                m_backup_interval_textinput->GetTextCtrl()->SetValue(m_backup_interval_def);
            }

            if (m_iot_environment_def == "0") {
                    on_select_radio("dev_host");
            } else if (m_iot_environment_def == "1") {
                    on_select_radio("qa_host");
            } else if (m_iot_environment_def == "2") {
                    on_select_radio("pre_host");
            }

            break;
        }

        case wxID_YES: {
            // bbs  domain changed
            auto            param   = get_select_radio(1);

            std::map<wxString, wxString> iot_environment_map;
            iot_environment_map["dev_host"]  = "0";
            iot_environment_map["qa_host"]   = "1";
            iot_environment_map["pre_host"]  = "2";

            if (iot_environment_map[param] != m_iot_environment_def) {
                AccountManager* manager = wxGetApp().getAccountManager();
                if (param == "dev_host") {
                    app_config->set("iot_environment", "0");
                    manager->set_host(DEV_HOST_URL);
                }
                else if (param == "qa_host") {
                    app_config->set("iot_environment", "1");
                    manager->set_host(QAT_HOST_URL);
                }
                else if (param == "pre_host") {
                    app_config->set("iot_environment", "2");
                    manager->set_host(PRE_HOST_URL);
                }
                manager->user_logout();
                wxMessageBox(_L("Swith cloud environment, Please login again!"));
            }

            // bbs  backup
            app_config->set("backup_interval", std::string(m_backup_interval_time.mb_str()));
            app_config->save();
            Slic3r::set_backup_interval(boost::lexical_cast<long>(app_config->get("backup_interval")));

            // bbs  developer mode
            auto developer_mode = app_config->get("developer_mode");
            if (developer_mode == "true") {
                Slic3r::GUI::wxGetApp().save_mode(comDevelop);
                Slic3r::GUI::wxGetApp().mainframe->show_log_window();
            } else {
                Slic3r::GUI::wxGetApp().save_mode(comAdvanced);
            }

            this->Destroy();
            break;
        }
        }
    });

    bSizer->Add(title_develop_mode, 0, wxTOP, 20);
    bSizer->Add(item_develop_mode, 0, wxTOP, 20);
    bSizer->Add(item_dump_video, 0, wxTOP, 20);
    bSizer->Add(item_backup_interval, 0, wxTOP, 20);
    bSizer->Add(radio1, 0, wxTOP, 20);
    bSizer->Add(radio2, 0, wxTOP, 5);
    bSizer->Add(radio3, 0, wxTOP, 5);
    bSizer->Add(debug_button, 0, wxALL, 50);

    m_panel_debug->SetSizer(bSizer);
    bSizer->Fit(m_panel_debug);
}

void PreferencesDialog::on_select_radio(std::string param)
{
    RadioSelectorList::Node *node    = m_radio_group.GetFirst();
    auto                     groupid = 0;

    while (node) {
        RadioSelector *rs = node->GetData();
        if (rs->m_param_name == param) groupid = rs->m_groupid;
        node = node->GetNext();
    }

    node = m_radio_group.GetFirst();
    while (node) {
        RadioSelector *rs = node->GetData();
        if (rs->m_groupid == groupid && rs->m_param_name == param) rs->m_radiobox->SetValue(true);
        if (rs->m_groupid == groupid && rs->m_param_name != param) rs->m_radiobox->SetValue(false);
        node = node->GetNext();
    }
}

wxString PreferencesDialog::get_select_radio(int groupid)
{
    RadioSelectorList::Node *node = m_radio_group.GetFirst();
    while (node) {
        RadioSelector *rs = node->GetData();
        if (rs->m_groupid == groupid && rs->m_radiobox->GetValue()) { return rs->m_param_name; }
        node = node->GetNext();
    }

    return wxEmptyString;
}

void PreferencesDialog::OnSelectRadio(wxMouseEvent &event)
{
    RadioSelectorList::Node *node    = m_radio_group.GetFirst();
    auto                     groupid = 0;

    while (node) {
        RadioSelector *rs = node->GetData();
        if (rs->m_radiobox->GetId() == event.GetId()) groupid = rs->m_groupid;
        node = node->GetNext();
    }

    node = m_radio_group.GetFirst();
    while (node) {
        RadioSelector *rs = node->GetData();
        if (rs->m_groupid == groupid && rs->m_radiobox->GetId() == event.GetId()) rs->m_radiobox->SetValue(true);
        if (rs->m_groupid == groupid && rs->m_radiobox->GetId() != event.GetId()) rs->m_radiobox->SetValue(false);
        node = node->GetNext();
    }
}

void PreferencesDialog::OnSelectTabel(wxMouseEvent &event)
{
    SelectorHash::iterator i = m_hash_selector.begin();
    while (i != m_hash_selector.end()) {
        Selector *sel = i->second;
        if (sel->m_selpanel->GetId() == event.GetId()) { on_select(i->first); }
        i++;
    }
}

}} // namespace Slic3r::GUI
