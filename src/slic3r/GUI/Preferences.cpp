#include "Preferences.hpp"
#include "OptionsGroup.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "Plater.hpp"
#include "MsgDialog.hpp"
#include "I18N.hpp"
#include "UxProgramTermsDialog.hpp"
#include "Widgets/StateColor.hpp"
#include "libslic3r/AppConfig.hpp"
#include <cassert>
#include <string>
#include <vector>
#include <wx/event.h>
#include <wx/gdicmn.h>
#include <wx/simplebook.h>
#include "OG_CustomCtrl.hpp"
#include "fila_manager/wgtFilaManagerFeature.h"
#include "slic3r/GUI/Widgets/Label.hpp"
#include "wx/graphics.h"

#include <wx/listimpl.cpp>
#include <map>
#include <wx/sizer.h>
#include "Gizmos/GLGizmoBase.hpp"
#include "OpenGLManager.hpp"
#ifdef __WINDOWS__
#ifdef _MSW_DARK_MODE
#include "dark_mode.hpp"
#endif // _MSW_DARK_MODE
#endif //__WINDOWS__

namespace Slic3r { namespace GUI {

WX_DEFINE_LIST(RadioSelectorList);

// Raw (pre-DPI) control widths for Preferences rows. Height is -1 (auto) unless
// noted. Wrap in FromDIP(...) at each use site, e.g. wxSize(FromDIP(TITLE_WIDTH), -1).
static constexpr int TITLE_WIDTH          = 100; // row label column
static constexpr int COMBOBOX_WIDTH       = 140;
static constexpr int LARGE_COMBOBOX_WIDTH = 160;
static constexpr int INPUT_WIDTH          = 100;
static constexpr int BTN_WIDTH            = 58; // small action button (reset / browse)
static constexpr int BTN_HEIGHT           = 22;
static constexpr int TITLE_PADDING        = 48;
static constexpr int ITEM_LEFT_PADDING    = 48 + 16;
static constexpr int ITEM_RIGHT_PADDING   = 24;
static constexpr int ITEM_MIN_HEIGHT      = 24;

// Scrolled panel used for every Preferences tab. wxScrolledWindow's default
// behavior is to scroll to whatever child receives focus, which makes the
// dialog jump around when the user tabs between combobox/checkbox rows.
// Suppressing that and presetting a sensible scroll rate keeps tab pages stable.
class ScrollPanel : public wxScrolledWindow
{
public:
    explicit ScrollPanel(wxWindow *parent) : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL)
    {
        SetScrollRate(5, 5);
        SetBackgroundColour(*wxWHITE);
    }

    bool ShouldScrollToChildOnFocus(wxWindow* child) override { return false; }
};

// Confirm dialog for "Reset all warning dialogs". A "Check details" button
// expands a panel listing the warning settings that get cleared
class ResetWarningsDialog : public DPIDialog
{
public:
    explicit ResetWarningsDialog(wxWindow *parent);
    ~ResetWarningsDialog() override = default;
    void on_dpi_changed(const wxRect &suggested_rect) override {}

private:
    void toggle_details();

    Button   *m_details_btn   = nullptr;
    wxWindow *m_details_panel = nullptr;
    bool      m_expanded      = false;
};

wxBoxSizer *PreferencesDialog::create_item_title(wxString title, wxWindow *parent, wxString tooltip)
{
    wxBoxSizer *m_sizer_title = new wxBoxSizer(wxHORIZONTAL);

    auto m_title = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, 0);
    m_title->SetForegroundColour(ThemeColor::TextSecondary);
    m_title->SetFont(::Label::Head_13);

    // The Preferences dialog has no native default push button (every visible button
    // is a custom-drawn ::Button, i.e. a plain wxWindow, not a Win32 BUTTON control).
    // Without a designated default button the Win32 dialog manager's xxxRemoveDefaultButton
    // walk (run on every WM_ACTIVATE / focus save) has no fixed target and enumerates the
    // child windows probing them with SendMessage(WM_GETDLGCODE). With an endpoint-DLP / IME
    // DLL injected into the process, that probe can be redirected cross-thread to a
    // non-pumping injected window and deadlock the UI thread. Give the dialog one real,
    // hidden, zero-size native button as a stable in-thread default so the walk finds its
    // target immediately and never leaves our own windows.
    // The hang is first found on windows, but keep it on other platforms is no harm
    auto line = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));

    m_sizer_title->AddSpacer(FromDIP(TITLE_PADDING));
    m_sizer_title->Add(m_title, wxSizerFlags().CenterVertical());

    return m_sizer_title;
}

wxBoxSizer *PreferencesDialog::create_item_combobox(wxString title, wxWindow *parent, wxString tooltip, std::string param, const std::vector<wxString>& label_list, const std::vector<std::string>& value_list, std::function<void(int)> callback, int title_width, int combox_width)
{
    assert(label_list.size() == value_list.size());

    auto find_nearst_by_value = [value_list](const std::string value) -> int {
        try {
            std::vector<int> values;
            for (const auto &v : value_list) values.push_back(stoi(v));
            int target = stoi(value);

            auto it = std::min_element(values.begin(), values.end(), [target](int a, int b) { return std::abs(a - target) < std::abs(b - target); });
            return std::distance(values.begin(), it);

        } catch (...) {
            return 0;
        }
    };

    auto get_value_idx = [value_list, find_nearst_by_value](const std::string value) -> int {
        auto iter = std::find(value_list.begin(), value_list.end(), value);
        if (iter != value_list.end()) return std::distance(value_list.begin(), iter);
        return find_nearst_by_value(value);
    };

    wxBoxSizer *m_sizer_combox = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_combox->AddSpacer(FromDIP(ITEM_LEFT_PADDING));
    m_sizer_combox->SetMinSize(wxSize(-1, FromDIP(ITEM_MIN_HEIGHT)));

    auto combo_title = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, title_width == 0 ? wxSize(FromDIP(TITLE_WIDTH), -1) : wxSize(title_width, -1), 0);
    combo_title->SetForegroundColour(ThemeColor::TextPrimary);
    combo_title->SetFont(::Label::Body_13);
    combo_title->SetToolTip(tooltip);
    combo_title->Wrap(-1);
    m_sizer_combox->Add(combo_title, wxSizerFlags().CenterVertical().Proportion(1));

    auto combobox = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, combox_width == 0 ? wxSize(FromDIP(LARGE_COMBOBOX_WIDTH), -1) : wxSize(combox_width, -1),
                                   0, nullptr, wxCB_READONLY);
    m_combobox_list[m_combobox_list.size()] = combobox;
    combobox->SetFont(::Label::Body_13);
    combobox->GetDropDown().SetFont(::Label::Body_13);

    std::vector<wxString>::iterator iter;
    for (auto label : label_list)
        combobox->Append(label);

    auto old_value = app_config->get(param);
    if (!old_value.empty()) {
        combobox->SetSelection(get_value_idx(old_value));
    }
    else {
        combobox->SetSelection(0);
    }

    m_sizer_combox->Add(combobox, wxSizerFlags().CenterVertical().Border(wxRIGHT, FromDIP(ITEM_RIGHT_PADDING)));

    //// save config
    combobox->GetDropDown().Bind(wxEVT_COMBOBOX, [this, param, value_list, callback](wxCommandEvent &e) {
        app_config->set(param, value_list[e.GetSelection()]);
        app_config->save();
        if (callback) {
            callback(e.GetSelection());
        }
        e.Skip();
    });
    return m_sizer_combox;
}

wxBoxSizer *PreferencesDialog::create_item_language_combobox(
    wxString title, wxWindow *parent, wxString tooltip, int padding_left, std::string param, std::vector<const wxLanguageInfo *> vlist)
{
    wxBoxSizer *m_sizer_combox = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_combox->AddSpacer(FromDIP(ITEM_LEFT_PADDING));
    m_sizer_combox->SetMinSize(wxSize(-1, FromDIP(ITEM_MIN_HEIGHT)));

    auto combo_title = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, wxSize(FromDIP(TITLE_WIDTH), -1), 0);
    combo_title->SetForegroundColour(ThemeColor::TextPrimary);
    combo_title->SetFont(::Label::Body_13);
    combo_title->SetToolTip(tooltip);
    combo_title->Wrap(-1);
    m_sizer_combox->Add(combo_title, wxSizerFlags().CenterVertical().Proportion(1));

    auto combobox = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(LARGE_COMBOBOX_WIDTH), -1), 0, nullptr, wxCB_READONLY);
    m_combobox_list[m_combobox_list.size()] = combobox;
    combobox->SetFont(::Label::Body_13);
    combobox->GetDropDown().SetFont(::Label::Body_13);
    auto language = app_config->get(param);
    m_current_language_selected = -1;
    std::vector<wxString>::iterator iter;
    for (size_t i = 0; i < vlist.size(); ++i) {
        auto language_name = vlist[i]->Description;

        if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_CHINESE_SIMPLIFIED)) {
            language_name = wxString::FromUTF8("\xe4\xb8\xad\xe6\x96\x87\x28\xe7\xae\x80\xe4\xbd\x93\x29");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_CHINESE_TRADITIONAL)) {
            language_name = wxString::FromUTF8("\xe4\xb8\xad\xe6\x96\x87\x28\xe7\xb9\x81\xe9\xab\x94\x29");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_SPANISH)) {
            language_name = wxString::FromUTF8("\x45\x73\x70\x61\xc3\xb1\x6f\x6c");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_GERMAN)) {
            language_name = wxString::FromUTF8("Deutsch");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_SWEDISH)) {
            language_name = wxString::FromUTF8("\x53\x76\x65\x6e\x73\x6b\x61"); //Svenska
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_DUTCH)) {
            language_name = wxString::FromUTF8("Nederlands");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_FRENCH)) {
            language_name = wxString::FromUTF8("\x46\x72\x61\x6E\xC3\xA7\x61\x69\x73");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_HUNGARIAN)) {
            language_name = wxString::FromUTF8("Magyar");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_JAPANESE)) {
            language_name = wxString::FromUTF8("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_ITALIAN)) {
            language_name = wxString::FromUTF8("\x69\x74\x61\x6c\x69\x61\x6e\x6f");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_KOREAN)) {
            language_name = wxString::FromUTF8("\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_RUSSIAN)) {
            language_name = wxString::FromUTF8("\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_CZECH)) {
            if (wxGetApp().app_config->get("language") == "ja_JP") {
                language_name = wxString::FromUTF8("\x43\x7A\x65\x63\x68");
            }
            else{
                language_name = wxString::FromUTF8("\xC4\x8D\x65\xC5\xA1\x74\x69\x6E\x61");
            }
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_UKRAINIAN)) {
            if (wxGetApp().app_config->get("language") == "ja_JP") {
                language_name = wxString::FromUTF8("\x55\x6B\x72\x61\x69\x6E\x69\x61\x6E");
            } else {
                language_name = wxString::FromUTF8("\xD0\xA3\xD0\xBA\xD1\x80\xD0\xB0\xD1\x97\xD0\xBD\xD1\x81\xD1\x8C\xD0\xBA\xD0\xB0");
            }
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_PORTUGUESE_BRAZILIAN)) {
            language_name = wxString::FromUTF8("\x50\x6F\x72\x74\x75\x67\x75\xC3\xAA\x73\x20\x28\x42\x72\x61\x73\x69\x6C\x29");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_TURKISH)) {
            language_name = wxString::FromUTF8("\x54\xC3\xBC\x72\x6B\xC3\xA7\x65");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_POLISH)) {
            language_name = wxString::FromUTF8("Polski");
        }

        if (language == vlist[i]->CanonicalName) {
            m_current_language_selected = i;
        }
        combobox->Append(language_name);
    }
    if (m_current_language_selected == -1 && language.size() >= 5) {
        language = language.substr(0, 2);
        for (size_t i = 0; i < vlist.size(); ++i) {
            if (vlist[i]->CanonicalName.StartsWith(language)) {
                m_current_language_selected = i;
                break;
            }
        }
    }
    combobox->SetSelection(m_current_language_selected);

    m_sizer_combox->Add(combobox, wxSizerFlags().CenterVertical().Border(wxRIGHT, FromDIP(ITEM_RIGHT_PADDING)));

    combobox->Bind(wxEVT_LEFT_DOWN, [this, combobox](wxMouseEvent &e) {
        m_current_language_selected = combobox->GetSelection();
        e.Skip();
    });

    combobox->Bind(wxEVT_COMBOBOX, [this, param, vlist, combobox](wxCommandEvent &e) {
        if (combobox->GetSelection() == m_current_language_selected)
            return;

        if (e.GetString().mb_str() != app_config->get(param)) {
            {
                //check if the project has changed
                if (wxGetApp().plater()->is_project_dirty()) {
                    auto result = MessageDialog(static_cast<wxWindow*>(this), _L("The current project has unsaved changes, save it before continuing?"),
                        wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Save"), wxYES_NO | wxCANCEL | wxYES_DEFAULT | wxCENTRE).ShowModal();

                    if (result == wxID_YES) {
                        wxGetApp().plater()->save_project();
                    }
                }


                // the dialog needs to be destroyed before the call to switch_language()
                // or sometimes the application crashes into wxDialogBase() destructor
                // so we put it into an inner scope
                MessageDialog msg_wingow(nullptr, _L("Switching the language requires application restart.\n") + "\n" + _L("Do you want to continue?"),
                                         _L("Language selection"), wxICON_QUESTION | wxOK | wxCANCEL);
                if (msg_wingow.ShowModal() == wxID_CANCEL) {
                    combobox->SetSelection(m_current_language_selected);
                    return;
                }
            }

            auto check = [this](bool yes_or_no) {
                // if (yes_or_no)
                //    return true;
                int act_btns = UnsavedChangesDialog::ActionButtons::SAVE;
                return wxGetApp().check_and_keep_current_preset_changes(_L("Switching application language"),
                                                                        _L("Switching application language while some presets are modified."), act_btns);
            };

            m_current_language_selected = combobox->GetSelection();
            if (m_current_language_selected >= 0 && m_current_language_selected < vlist.size()) {
                app_config->set(param, vlist[m_current_language_selected]->CanonicalName.ToUTF8().data());
                app_config->save();

                wxGetApp().load_language(vlist[m_current_language_selected]->CanonicalName, false);
                Close();
                // Reparent(nullptr);
                GetParent()->RemoveChild(this);
                Label::initSysFont(app_config->get_language_code());
                wxGetApp().recreate_GUI(_L("Changing application language"));
            }
        }

        e.Skip();
    });

    return m_sizer_combox;
}

wxBoxSizer *PreferencesDialog::create_item_region_combobox(wxString title, wxWindow *parent, wxString tooltip, std::vector<wxString> vlist)
{
    std::vector<wxString> local_regions = {"Asia-Pacific", "China", "Europe", "North America", "Others"};

    wxBoxSizer *m_sizer_combox = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_combox->AddSpacer(FromDIP(ITEM_LEFT_PADDING));
    m_sizer_combox->SetMinSize(wxSize(-1, FromDIP(ITEM_MIN_HEIGHT)));

    auto combo_title = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, wxSize(FromDIP(TITLE_WIDTH), -1), 0);
    combo_title->SetForegroundColour(ThemeColor::TextPrimary);
    combo_title->SetFont(::Label::Body_13);
    combo_title->SetToolTip(tooltip);
    combo_title->Wrap(-1);
    m_sizer_combox->Add(combo_title, wxSizerFlags().CenterVertical().Proportion(1));

    auto combobox = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(LARGE_COMBOBOX_WIDTH), -1), 0, nullptr, wxCB_READONLY);
    m_combobox_list[m_combobox_list.size()] = combobox;
    combobox->SetFont(::Label::Body_13);
    combobox->GetDropDown().SetFont(::Label::Body_13);
    m_sizer_combox->Add(combobox, wxSizerFlags().CenterVertical().Border(wxRIGHT, FromDIP(ITEM_RIGHT_PADDING)));

    std::vector<wxString>::iterator iter;
    for (iter = vlist.begin(); iter != vlist.end(); iter++) { combobox->Append(*iter); }

    AppConfig * config       = GUI::wxGetApp().app_config;

    int         current_region = 0;
    if (!config->get("region").empty()) {
        std::string country_code = config->get("region");
        for (auto i = 0; i < vlist.size(); i++) {
            if (local_regions[i].ToStdString() == country_code) {
                combobox->SetSelection(i);
                current_region = i;
            }
        }
    }

    combobox->GetDropDown().Bind(wxEVT_COMBOBOX, [this, combobox, current_region, local_regions](wxCommandEvent &e) {
        auto region_index = e.GetSelection();
        auto region       = local_regions[region_index];

        combobox->SetSelection(region_index);
        NetworkAgent* agent = wxGetApp().getAgent();
        AppConfig* config = GUI::wxGetApp().app_config;
        if (agent) {
            MessageDialog msg_wingow(this, _L("Changing the region will log out your account.\n") + "\n" + _L("Do you want to continue?"), _L("Region selection"),
                                     wxICON_QUESTION | wxOK | wxCANCEL);
            if (msg_wingow.ShowModal() == wxID_CANCEL) {
                combobox->SetSelection(current_region);
                return;
            } else {
                wxGetApp().request_user_logout();
                config->set("region", region.ToStdString());
                wxGetApp().update_log_sink_region();
                auto area = config->get_country_code();
                if (agent) {
                    agent->set_country_code(area);
                }
                EndModal(wxID_CANCEL);
            }
        } else {
            config->set("region", region.ToStdString());
            wxGetApp().update_log_sink_region();
        }
        wxGetApp().update_publish_status();
        //e.Skip();
    });

    return m_sizer_combox;
}

wxBoxSizer *PreferencesDialog::create_item_loglevel_combobox(wxString title, wxWindow *parent, wxString tooltip, std::vector<wxString> vlist)
{
    wxBoxSizer *m_sizer_combox = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_combox->AddSpacer(FromDIP(ITEM_LEFT_PADDING));
    m_sizer_combox->SetMinSize(wxSize(-1, FromDIP(ITEM_MIN_HEIGHT)));

    auto combo_title = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, wxSize(FromDIP(TITLE_WIDTH), -1), 0);
    combo_title->SetForegroundColour(ThemeColor::TextPrimary);
    combo_title->SetFont(::Label::Body_13);
    combo_title->SetToolTip(tooltip);
    combo_title->Wrap(-1);
    m_sizer_combox->Add(combo_title, wxSizerFlags().CenterVertical().Proportion(1));

    auto combobox                           = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(COMBOBOX_WIDTH), -1), 0, nullptr, wxCB_READONLY);
    m_combobox_list[m_combobox_list.size()] = combobox;
    combobox->SetFont(::Label::Body_13);
    combobox->GetDropDown().SetFont(::Label::Body_13);

    std::vector<wxString>::iterator iter;
    for (iter = vlist.begin(); iter != vlist.end(); iter++) { combobox->Append(*iter); }

    auto severity_level = app_config->get("severity_level");
    if (!severity_level.empty()) { combobox->SetValue(severity_level); }

    m_sizer_combox->Add(combobox, wxSizerFlags().CenterVertical().Border(wxRIGHT, FromDIP(ITEM_RIGHT_PADDING)));

    // save config
    combobox->GetDropDown().Bind(wxEVT_COMBOBOX, [this](wxCommandEvent &e) {
        auto level = Slic3r::get_string_logging_level(e.GetSelection());
        Slic3r::set_logging_level(Slic3r::level_string_to_boost(level));
        app_config->set("severity_level",level);
        app_config->save();
        e.Skip();
     });
    return m_sizer_combox;
}


wxBoxSizer *PreferencesDialog::create_item_multiple_combobox(
    wxString title, wxWindow *parent, wxString tooltip, int padding_left, std::string param, std::vector<wxString> vlista, std::vector<wxString> vlistb)
{
    std::vector<wxString> params;
    Split(app_config->get(param), "/", params);

    std::vector<wxString>::iterator iter;

   wxBoxSizer *m_sizer_tcombox= new wxBoxSizer(wxHORIZONTAL);
   m_sizer_tcombox->Add(0, 0, 0, wxEXPAND | wxLEFT, 23);
   m_sizer_tcombox->SetMinSize(wxSize(-1, FromDIP(ITEM_MIN_HEIGHT)));

   auto combo_title = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, wxSize(FromDIP(TITLE_WIDTH), -1), 0);
   combo_title->SetToolTip(tooltip);
   combo_title->Wrap(-1);
   combo_title->SetForegroundColour(ThemeColor::TextPrimary);
   combo_title->SetFont(::Label::Body_13);
   m_sizer_tcombox->Add(combo_title, 0, wxALIGN_CENTER | wxALL, 3);

   auto combobox_left                      = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(COMBOBOX_WIDTH), -1), 0, nullptr, wxCB_READONLY);
   m_combobox_list[m_combobox_list.size()] = combobox_left;
   combobox_left->SetFont(::Label::Body_13);
   combobox_left->GetDropDown().SetFont(::Label::Body_13);


   for (iter = vlista.begin(); iter != vlista.end(); iter++) { combobox_left->Append(*iter); }
   combobox_left->SetValue(std::string(params[0].mb_str()));
   m_sizer_tcombox->Add(combobox_left, 0, wxALIGN_CENTER, 0);

   auto combo_title_add = new wxStaticText(parent, wxID_ANY, wxT("+"), wxDefaultPosition, wxDefaultSize, 0);
   combo_title->SetForegroundColour(ThemeColor::TextPrimary);
   combo_title->SetFont(::Label::Body_13);
   combo_title_add->Wrap(-1);
   m_sizer_tcombox->Add(combo_title_add, 0, wxALIGN_CENTER | wxALL, 3);

   auto combobox_right                     = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(COMBOBOX_WIDTH), -1), 0, nullptr, wxCB_READONLY);
   m_combobox_list[m_combobox_list.size()] = combobox_right;
   combobox_right->SetFont(::Label::Body_13);
   combobox_right->GetDropDown().SetFont(::Label::Body_13);

   for (iter = vlistb.begin(); iter != vlistb.end(); iter++) { combobox_right->Append(*iter); }
   combobox_right->SetValue(std::string(params[1].mb_str()));
   m_sizer_tcombox->Add(combobox_right, 0, wxALIGN_CENTER, 0);

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

    return m_sizer_tcombox;
}

wxBoxSizer *PreferencesDialog::create_item_input(wxString title, wxString title2, wxWindow *parent, wxString tooltip, std::string param, std::function<void(wxString)> onchange)
{
    wxBoxSizer *sizer_input = new wxBoxSizer(wxHORIZONTAL);
    sizer_input->SetMinSize(wxSize(-1, FromDIP(ITEM_MIN_HEIGHT)));
    auto        input_title   = new wxStaticText(parent, wxID_ANY, title);
    input_title->SetForegroundColour(ThemeColor::TextPrimary);
    input_title->SetFont(::Label::Body_13);
    input_title->SetToolTip(tooltip);
    input_title->Wrap(-1);

    auto       input = new ::TextInput(parent, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(INPUT_WIDTH), -1), wxTE_PROCESS_ENTER);
    StateColor input_bg(std::pair<wxColour, int>(ThemeColor::Grey250, StateColor::Disabled), std::pair<wxColour, int>(ThemeColor::White, StateColor::Enabled));
    input->SetBackgroundColor(input_bg);
    input->GetTextCtrl()->SetValue(app_config->get(param));
    wxTextValidator validator(wxFILTER_DIGITS);
    input->GetTextCtrl()->SetValidator(validator);

    wxStaticText *second_title = nullptr;
    if (!title2.empty()) {
        second_title = new wxStaticText(parent, wxID_ANY, title2, wxDefaultPosition, wxSize(FromDIP(TITLE_WIDTH), -1), 0);
        second_title->SetForegroundColour(ThemeColor::TextPrimary);
        second_title->SetFont(::Label::Body_13);
        second_title->SetToolTip(tooltip);
        second_title->Wrap(-1);
    }

    sizer_input->AddSpacer(FromDIP(ITEM_LEFT_PADDING));
    sizer_input->Add(input_title, wxSizerFlags().CenterVertical().Proportion(1));
    sizer_input->Add(input, wxSizerFlags().CenterVertical().Border(wxRIGHT, ITEM_RIGHT_PADDING));
    if (second_title) sizer_input->Add(second_title, 0, wxALIGN_CENTER_VERTICAL | wxALL, 3);

    input->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, [this, param, input, onchange](wxCommandEvent &e) {
        auto value = input->GetTextCtrl()->GetValue();
        app_config->set(param, std::string(value.mb_str()));
        app_config->save();
        onchange(value);
        e.Skip();
    });

    input->GetTextCtrl()->Bind(wxEVT_KILL_FOCUS, [this, param, input, onchange](wxFocusEvent &e) {
        auto value = input->GetTextCtrl()->GetValue();
        app_config->set(param, std::string(value.mb_str()));
        app_config->save();
        onchange(value);
        e.Skip();
    });

    return sizer_input;
}

wxBoxSizer *PreferencesDialog::create_item_range_input(
    wxString title, wxWindow *parent, wxString tooltip, std::string param, float range_min, float range_max, int keep_digital, std::function<void(wxString)> onchange)
{
    wxBoxSizer *sizer_input = new wxBoxSizer(wxHORIZONTAL);
    sizer_input->SetMinSize(wxSize(-1, FromDIP(ITEM_MIN_HEIGHT)));
    auto        input_title = new wxStaticText(parent, wxID_ANY, title);
    input_title->SetForegroundColour(ThemeColor::TextPrimary);
    input_title->SetFont(::Label::Body_13);
    input_title->SetToolTip(tooltip);
    input_title->Wrap(-1);

    auto float_value = std::atof(app_config->get(param).c_str());
    if (float_value < range_min || float_value > range_max) {
        float_value = range_min;
        app_config->set(param, std::to_string(range_min));
        app_config->save();
    }
    auto       input = new ::TextInput(parent, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(INPUT_WIDTH), -1), wxTE_PROCESS_ENTER);
    StateColor input_bg(std::pair<wxColour, int>(ThemeColor::Grey250, StateColor::Disabled), std::pair<wxColour, int>(ThemeColor::White, StateColor::Enabled));
    input->SetBackgroundColor(input_bg);
    input->GetTextCtrl()->SetValue(app_config->get(param));
    wxTextValidator validator(wxFILTER_NUMERIC);
    input->GetTextCtrl()->SetValidator(validator);

    sizer_input->AddSpacer(FromDIP(ITEM_LEFT_PADDING));
    sizer_input->Add(input_title, wxSizerFlags().CenterVertical().Proportion(1));
    sizer_input->Add(input, wxSizerFlags().CenterVertical().Border(wxRIGHT, FromDIP(ITEM_RIGHT_PADDING)));
    auto format_str=[](int keep_digital,float val){
        std::stringstream ss;
        ss << std::fixed << std::setprecision(keep_digital) << val;
        return ss.str();
    };
    auto set_value_to_app = [this, param, onchange, input, range_min, range_max, format_str, keep_digital](float value, bool update_slider) {
        if (value < range_min) { value = range_min; }
        if (value > range_max) { value = range_max; }
        auto str = format_str(keep_digital, value);
        app_config->set(param, str);
        app_config->save();
        if (onchange) {
            onchange(str);
        }
        input->GetTextCtrl()->SetValue(str);
    };
    input->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, [this, set_value_to_app, input](wxCommandEvent &e) {
        auto value = std::atof(input->GetTextCtrl()->GetValue().c_str());
        set_value_to_app(value,true);
        e.Skip();
    });

    input->GetTextCtrl()->Bind(wxEVT_KILL_FOCUS, [this, set_value_to_app, input](wxFocusEvent &e) {
        auto value = std::atof(input->GetTextCtrl()->GetValue().c_str());
        set_value_to_app(value, true);
        e.Skip();
    });

    return sizer_input;
}

wxBoxSizer *PreferencesDialog::create_item_range_two_input(wxString                      title,
                                                           wxWindow *                    parent,
                                                           wxString                      tooltip,
                                                           std::string                   param,
                                                           std::string                   param1,
                                                           float                         range_min,
                                                           float                         range_max,
                                                           int                           keep_digital,
                                                           std::function<void(wxString)> onchange,
                                                           std::function<void(wxString)> onchange1)
{
    wxBoxSizer *sizer_input = new wxBoxSizer(wxHORIZONTAL);
    sizer_input->SetMinSize(wxSize(-1, FromDIP(ITEM_MIN_HEIGHT)));
    auto        input_title = new wxStaticText(parent, wxID_ANY, title);
    input_title->SetForegroundColour(ThemeColor::TextPrimary);
    input_title->SetFont(::Label::Body_13);
    input_title->SetToolTip(tooltip);
    input_title->Wrap(-1);

    auto float_value = std::atof(app_config->get(param).c_str());
    if (float_value < range_min || float_value > range_max) {
        float_value = range_min;
        app_config->set(param, std::to_string(range_min));
        app_config->save();
    }
    float_value = std::atof(app_config->get(param1).c_str());
    if (float_value < range_min || float_value > range_max) {
        float_value = range_min;
        app_config->set(param1, std::to_string(range_min));
        app_config->save();
    }
    auto       input = new ::TextInput(parent, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(INPUT_WIDTH), -1), wxTE_PROCESS_ENTER);
    StateColor input_bg(std::pair<wxColour, int>(ThemeColor::Grey250, StateColor::Disabled), std::pair<wxColour, int>(ThemeColor::White, StateColor::Enabled));
    input->SetBackgroundColor(input_bg);
    input->GetTextCtrl()->SetValue(app_config->get(param));
    wxTextValidator validator(wxFILTER_NUMERIC);
    input->GetTextCtrl()->SetValidator(validator);

    auto input1 = new ::TextInput(parent, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(INPUT_WIDTH), -1), wxTE_PROCESS_ENTER);
    input1->SetBackgroundColor(input_bg);
    input1->GetTextCtrl()->SetValue(app_config->get(param1));
    input1->GetTextCtrl()->SetValidator(validator);

    sizer_input->AddSpacer(FromDIP(ITEM_LEFT_PADDING));
    sizer_input->Add(input_title, wxSizerFlags().CenterVertical().Proportion(1));
    sizer_input->Add(input, 0, wxALIGN_CENTER_VERTICAL, 0);

    sizer_input->AddSpacer(FromDIP(8));
    sizer_input->Add(input1, wxSizerFlags().CenterVertical().Border(wxRIGHT, FromDIP(ITEM_RIGHT_PADDING)));
    auto format_str = [](int keep_digital, float val) {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(keep_digital) << val;
        return ss.str();
    };
    auto set_value_to_app = [this, param, onchange, input, range_min, range_max, format_str, keep_digital](float value, bool update_slider) {
        if (value < range_min) { value = range_min; }
        if (value > range_max) { value = range_max; }
        auto str = format_str(keep_digital, value);
        app_config->set(param, str);
        app_config->save();
        if (onchange) { onchange(str); }
        input->GetTextCtrl()->SetValue(str);
    };
    input->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, [this, set_value_to_app, input](wxCommandEvent &e) {
        auto value = std::atof(input->GetTextCtrl()->GetValue().c_str());
        set_value_to_app(value, true);
        e.Skip();
    });

    input->GetTextCtrl()->Bind(wxEVT_KILL_FOCUS, [this, set_value_to_app, input](wxFocusEvent &e) {
        auto value = std::atof(input->GetTextCtrl()->GetValue().c_str());
        set_value_to_app(value, true);
        e.Skip();
    });

    auto set_value1_to_app = [this, param1, onchange1, input1, range_min, range_max, format_str, keep_digital](float value, bool update_slider) {
        if (value < range_min) { value = range_min; }
        if (value > range_max) { value = range_max; }
        auto str = format_str(keep_digital, value);
        app_config->set(param1, str);
        app_config->save();
        if (onchange1) { onchange1(str); }
        input1->GetTextCtrl()->SetValue(str);
    };
    input1->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, [this, set_value1_to_app, input1](wxCommandEvent &e) {
        auto value = std::atof(input1->GetTextCtrl()->GetValue().c_str());
        set_value1_to_app(value, true);
        e.Skip();
    });

    input1->GetTextCtrl()->Bind(wxEVT_KILL_FOCUS, [this, set_value1_to_app, input1](wxFocusEvent &e) {
        auto value = std::atof(input1->GetTextCtrl()->GetValue().c_str());
        set_value1_to_app(value, true);
        e.Skip();
    });

    return sizer_input;
}

wxBoxSizer *PreferencesDialog::create_item_switch(wxString title, wxWindow *parent, wxString tooltip ,std::string param)
{
    wxBoxSizer *m_sizer_switch = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_switch->SetMinSize(wxSize(-1, FromDIP(ITEM_MIN_HEIGHT)));
    auto        switch_title   = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, wxSize(FromDIP(TITLE_WIDTH), -1), 0);
    switch_title->SetForegroundColour(ThemeColor::TextPrimary);
    switch_title->SetFont(::Label::Body_13);
    switch_title->SetToolTip(tooltip);
    switch_title->Wrap(-1);
    auto switchbox = new ::SwitchButton(parent, wxID_ANY);

    /*auto index = app_config->get(param);
    if (!index.empty()) { combobox->SetSelection(atoi(index.c_str())); }*/

    m_sizer_switch->Add(0, 0, 0, wxEXPAND | wxLEFT, 23);
    m_sizer_switch->Add(switch_title, 0, wxALIGN_CENTER | wxALL, 3);
    m_sizer_switch->Add( 0, 0, 1, wxEXPAND, 0 );
    m_sizer_switch->Add(switchbox, 0, wxALIGN_CENTER, 0);
    m_sizer_switch->Add( 0, 0, 0, wxEXPAND|wxLEFT, 40 );

    //// save config
    switchbox->Bind(wxEVT_TOGGLEBUTTON, [this, param](wxCommandEvent &e) {
        /* app_config->set(param, std::to_string(e.GetSelection()));
         app_config->save();*/
         e.Skip();
    });
    return m_sizer_switch;
}

wxBoxSizer* PreferencesDialog::create_item_darkmode_checkbox(wxString title, wxWindow* parent, wxString tooltip, int padding_left, std::string param)
{
    wxBoxSizer* m_sizer_checkbox = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_checkbox->SetMinSize(wxSize(-1, FromDIP(ITEM_MIN_HEIGHT)));

    auto checkbox = new ::CheckBox(parent);
    m_checkbox_list[m_checkbox_list.size()] = checkbox;
    checkbox->SetValue((app_config->get(param) == "1") ? true : false);
    m_dark_mode_ckeckbox = checkbox;

    auto checkbox_title = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, 0);
    checkbox_title->SetForegroundColour(ThemeColor::TextPrimary);
    checkbox_title->SetFont(::Label::Body_13);

    auto size = checkbox_title->GetTextExtent(title);
    checkbox_title->SetMinSize(wxSize(size.x + FromDIP(40), -1));
    checkbox_title->Wrap(-1);

    m_sizer_checkbox->AddSpacer(FromDIP(ITEM_LEFT_PADDING));
    m_sizer_checkbox->Add(checkbox_title, wxSizerFlags().CenterVertical().Proportion(1));
    m_sizer_checkbox->Add(checkbox, wxSizerFlags().CenterVertical().Border(wxRIGHT, FromDIP(ITEM_RIGHT_PADDING)));

    //// save config
    checkbox->Bind(wxEVT_TOGGLEBUTTON, [this, checkbox, param](wxCommandEvent& e) {
        app_config->set(param, checkbox->GetValue() ? "1" : "0");
        app_config->save();
        wxGetApp().Update_dark_mode_flag();

        //dark mode
#ifdef _MSW_DARK_MODE
        wxGetApp().force_colors_update();
        wxGetApp().update_ui_from_settings();
        set_dark_mode();
#endif
        SimpleEvent evt = SimpleEvent(EVT_GLCANVAS_COLOR_MODE_CHANGED);
        wxPostEvent(wxGetApp().plater(), evt);
        e.Skip();
        });

    checkbox->SetToolTip(tooltip);
    return m_sizer_checkbox;
}

void PreferencesDialog::set_dark_mode()
{
#ifdef __WINDOWS__
#ifdef _MSW_DARK_MODE
    NppDarkMode::SetDarkExplorerTheme(this->GetHWND());
    NppDarkMode::SetDarkTitleBar(this->GetHWND());
    wxGetApp().UpdateDlgDarkUI(this);
    SetActiveWindow(wxGetApp().mainframe->GetHWND());
    SetActiveWindow(GetHWND());
#endif
#endif
}

wxBoxSizer *PreferencesDialog::create_item_checkbox(wxString title, wxWindow *parent, wxString tooltip, int padding_left, std::string param)
{
    wxBoxSizer *m_sizer_checkbox  = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_checkbox->SetMinSize(wxSize(-1, FromDIP(ITEM_MIN_HEIGHT)));

    auto checkbox = new ::CheckBox(parent);
    m_checkbox_list[m_checkbox_list.size()] = checkbox;
    if (param == "privacyuse") {
        checkbox->SetValue((app_config->get("firstguide", param) == "true") ? true : false);
    } else if (param == "auto_stop_liveview") {
        checkbox->SetValue((app_config->get("liveview", param) == "true") ? false : true);
    } else {
        checkbox->SetValue((app_config->get(param) == "true") ? true : false);
    }

    auto checkbox_title = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, 0);
    checkbox_title->SetForegroundColour(ThemeColor::TextPrimary);
    checkbox_title->SetFont(::Label::Body_13);

    auto size = checkbox_title->GetTextExtent(title);
    checkbox_title->SetMinSize(wxSize(size.x + FromDIP(5), -1));
    checkbox_title->Wrap(-1);

    m_sizer_checkbox->AddSpacer(FromDIP(ITEM_LEFT_PADDING));
    m_sizer_checkbox->Add(checkbox_title, wxSizerFlags().CenterVertical().Proportion(1));
    m_sizer_checkbox->Add(checkbox, wxSizerFlags().CenterVertical().Border(wxRIGHT, FromDIP(ITEM_RIGHT_PADDING)));

    //// save config
    checkbox->Bind(wxEVT_TOGGLEBUTTON, [this, checkbox, param](wxCommandEvent &e) {
        if (param == "privacyuse") {
            app_config->set("firstguide", param, checkbox->GetValue());
            NetworkAgent* agent = GUI::wxGetApp().getAgent();
            if (!checkbox->GetValue()) {
                if (agent) {
                    agent->track_enable(false);
                    agent->track_remove_files();
                }
            }
            wxGetApp().save_privacy_policy_history(checkbox->GetValue(), "preferences");
            app_config->save();
        }
        else if (param == "auto_stop_liveview") {
            app_config->set("liveview", param, !checkbox->GetValue());
        }
        else {
            app_config->set_bool(param, checkbox->GetValue());
            app_config->save();
        }

        if (param == "staff_pick_switch") {
            bool pbool = app_config->get("staff_pick_switch") == "true";
            wxGetApp().switch_staff_pick(pbool);
        }

        if (param == "sync_user_preset") {
            bool sync = app_config->get("sync_user_preset") == "true" ? true : false;
            if (sync) {
                wxGetApp().start_sync_user_preset();
            } else {
                wxGetApp().stop_sync_user_preset();
            }
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " sync_user_preset: " << (sync ? "true" : "false");
        }

        #ifdef __WXMSW__
        if (param == "associate_3mf") {
             bool pbool = app_config->get("associate_3mf") == "true" ? true : false;
             if (pbool) {
                 wxGetApp().associate_files(L"3mf");
             } else {
                 wxGetApp().disassociate_files(L"3mf");
             }
        }

        if (param == "associate_stl") {
            bool pbool = app_config->get("associate_stl") == "true" ? true : false;
            if (pbool) {
                wxGetApp().associate_files(L"stl");
            } else {
                wxGetApp().disassociate_files(L"stl");
            }
        }

        if (param == "associate_step") {
            bool pbool = app_config->get("associate_step") == "true" ? true : false;
            if (pbool) {
                wxGetApp().associate_files(L"step");
            } else {
                wxGetApp().disassociate_files(L"step");
            }
        }

        #endif // __WXMSW__

        if (param == "developer_mode")
        {
            m_developer_mode_def = app_config->get("developer_mode");
            if (m_developer_mode_def == "true") {
                Slic3r::GUI::wxGetApp().save_mode(comDevelop);
            } else {
                Slic3r::GUI::wxGetApp().save_mode(comAdvanced);
            }
        }

        // webview  dump_vedio
        if (param == "internal_developer_mode") {
            m_internal_developer_mode_def = app_config->get("internal_developer_mode");
            if (m_internal_developer_mode_def == "true") {
                Slic3r::GUI::wxGetApp().update_internal_development();
                Slic3r::GUI::wxGetApp().mainframe->show_log_window();
            } else {
                Slic3r::GUI::wxGetApp().update_internal_development();
            }
        }

        if (param == "show_print_history") {
            auto show_history = app_config->get_bool("show_print_history");
            if (show_history == true) {
                if (wxGetApp().mainframe && wxGetApp().mainframe->m_webview) { wxGetApp().mainframe->m_webview->ShowUserPrintTask(true,true); }
            } else {
                if (wxGetApp().mainframe && wxGetApp().mainframe->m_webview) { wxGetApp().mainframe->m_webview->ShowUserPrintTask(false); }
            }
        }

        if (param == "enable_lod") {
            if (wxGetApp().plater()->is_project_dirty()) {
                auto result = MessageDialog(static_cast<wxWindow *>(this), _L("The current project has unsaved changes, save it before continuing?"),
                                            wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Save"), wxYES_NO  | wxYES_DEFAULT | wxCENTRE)
                                  .ShowModal();
                if (result == wxID_YES) {
                    wxGetApp().plater()->save_project();
                }
            }
            MessageDialog msg_wingow(nullptr, _L("Please note that the model show will undergo certain changes at small pixels case.\nEnabled LOD requires application restart.") + "\n" + _L("Do you want to continue?"), _L("Enable LOD"),
                wxYES| wxYES_DEFAULT | wxCANCEL | wxCENTRE);
            if (msg_wingow.ShowModal() == wxID_YES) {
                Close();
                GetParent()->RemoveChild(this);
                wxGetApp().recreate_GUI(_L("Enable LOD"));
            } else {
                checkbox->SetValue(!checkbox->GetValue());
                app_config->set_bool(param, checkbox->GetValue());
                app_config->save();
            }
        }

        if (param == "enable_record_gcodeviewer_option_item"){
            SimpleEvent evt(EVT_ENABLE_GCODE_OPTION_ITEM_CHANGED);
            wxPostEvent(wxGetApp().plater(), evt);
        }

        if (param == "enable_high_low_temp_mixed_printing") {
            if (checkbox->GetValue()) {
                const wxString warning_title = _L("Bed Temperature Difference Warning");
                const wxString warning_message =
                    _L("Using filaments with significantly different temperatures may cause:\n"
                        "• Extruder clogging\n"
                        "• Nozzle damage\n"
                        "• Layer adhesion issues\n\n"
                        "Continue with enabling this feature?");
                std::function<void(const wxString&)> link_callback = [](const wxString&) {
                            const std::string lang_code = wxGetApp().app_config->get("language");
                            const wxString region = (lang_code.find("zh") != std::string::npos) ? L"zh" : L"en";
                            const wxString wiki_url = wxString::Format(
                                L"https://wiki.bambulab.com/%s/filament-acc/filament/h2d-filament-config-limit",
                                region
                            );
                            wxGetApp().open_browser_with_warning_dialog(wiki_url);
                            };

                MessageDialog msg_dialog(
                    nullptr,
                    warning_message,
                    warning_title,
                    wxICON_WARNING | wxYES_NO | wxCANCEL | wxYES_DEFAULT | wxCENTRE,
                    wxEmptyString,
                    _L("Click Wiki for help."),
                    link_callback
                );

                if (msg_dialog.ShowModal() != wxID_YES) {
                    checkbox->SetValue(false);
                    app_config->set_bool(param, false);
                    app_config->save();
                }
            }
        }
        e.Skip();
    });

    //// for debug mode
    if (param == "developer_mode") { m_developer_mode_ckeckbox = checkbox; }
    if (param == "internal_developer_mode") { m_internal_developer_mode_ckeckbox = checkbox; }


    checkbox->SetToolTip(tooltip);
    return m_sizer_checkbox;
}

wxWindow* PreferencesDialog::create_item_downloads(wxWindow* parent, int padding_left, std::string param)
{
    wxString download_path = wxString::FromUTF8(app_config->get("download_path"));
    auto item_panel = new wxWindow(parent, wxID_ANY);
    item_panel->SetBackgroundColour(*wxWHITE);

    wxBoxSizer *sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->AddSpacer(FromDIP(ITEM_LEFT_PADDING));
    sizer->SetMinSize(wxSize(-1, FromDIP(ITEM_MIN_HEIGHT)));

    auto m_staticTextTitle = new wxStaticText(item_panel, wxID_ANY, _L("Download path"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticTextTitle->SetForegroundColour(ThemeColor::TextPrimary);
    m_staticTextTitle->SetFont(::Label::Body_13);
    m_staticTextTitle->Wrap(-1);

    auto m_staticTextPath = new ::TextInput(item_panel, download_path, wxEmptyString, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
    // m_staticTextPath->SetBackgroundColor(ThemeColor::Grey250);
    // m_staticTextPath->SetBorderColor(ThemeColor::Grey350);
    m_staticTextPath->SetCornerRadius(FromDIP(4));
    m_staticTextPath->GetTextCtrl()->SetFont(::Label::Body_13);

    auto m_button_download = new Button(item_panel, _L("Browse"));
    m_button_list[m_button_list.size()] = m_button_download;
    StateColor abort_bg(std::pair<wxColour, int>(ThemeColor::White, StateColor::Disabled), std::pair<wxColour, int>(ThemeColor::BrandGreenPressed, StateColor::Pressed),
                        std::pair<wxColour, int>(ThemeColor::BrandGreenHovered, StateColor::Hovered), std::pair<wxColour, int>(ThemeColor::White, StateColor::Enabled),
                        std::pair<wxColour, int>(ThemeColor::White, StateColor::Normal));
    m_button_download->SetBackgroundColor(abort_bg);
    StateColor abort_bd(std::pair<wxColour, int>(ThemeColor::TextDisabled, StateColor::Disabled), std::pair<wxColour, int>(ThemeColor::TextPrimary, StateColor::Enabled));
    m_button_download->SetBorderColor(abort_bd);
    StateColor abort_text(std::pair<wxColour, int>(ThemeColor::TextDisabled, StateColor::Disabled), std::pair<wxColour, int>(ThemeColor::TextPrimary, StateColor::Enabled));
    m_button_download->SetTextColor(abort_text);
    m_button_download->SetFont(Label::Body_10);
    m_button_download->SetMinSize(wxSize(FromDIP(BTN_WIDTH), FromDIP(BTN_HEIGHT)));
    m_button_download->SetSize(wxSize(FromDIP(58), FromDIP(22)));
    m_button_download->SetCornerRadius(FromDIP(4));

    m_button_download->Bind(wxEVT_BUTTON, [this, m_staticTextPath, item_panel](auto& e) {
        wxString defaultPath = wxT("/");
        wxDirDialog dialog(this, _L("Choose Download Directory"), defaultPath, wxDD_NEW_DIR_BUTTON);

        if (dialog.ShowModal() == wxID_OK) {
            wxString download_path = dialog.GetPath();
            std::string download_path_str = download_path.ToUTF8().data();
            app_config->set("download_path", download_path_str);
            m_staticTextPath->GetTextCtrl()->SetValue(download_path);
            item_panel->Layout();
        }
        });

    sizer->Add(m_staticTextTitle, wxSizerFlags().CenterVertical().Border(wxRIGHT, FromDIP(8)));
    sizer->Add(m_staticTextPath, wxSizerFlags().CenterVertical().Proportion(1).Border(wxRIGHT, FromDIP(8)));
    sizer->Add(m_button_download, wxSizerFlags().CenterVertical().Border(wxRIGHT, FromDIP(ITEM_RIGHT_PADDING)));

    item_panel->SetSizer(sizer);
    item_panel->Layout();

    return item_panel;
}

wxSizer *PreferencesDialog::create_item_radiobox(wxString title, wxWindow *parent, wxString tooltip, int padding_left, int groupid, std::string param)
{
    RadioBox *radiobox                      = new RadioBox(parent);
    m_radiobox_list[m_radiobox_list.size()] = radiobox;
    radiobox->Bind(wxEVT_LEFT_DOWN, &PreferencesDialog::OnSelectRadio, this);

    RadioSelector *rs = new RadioSelector;
    rs->m_groupid     = groupid;
    rs->m_param_name  = param;
    rs->m_radiobox    = radiobox;
    rs->m_selected    = false;
    m_radio_group.Append(rs);

    wxStaticText *text = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize);

    radiobox->SetToolTip(tooltip);
    text->SetToolTip(tooltip);

    wxBoxSizer *sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->AddSpacer(FromDIP(ITEM_LEFT_PADDING));
    sizer->SetMinSize(wxSize(-1, FromDIP(ITEM_MIN_HEIGHT)));
    sizer->Add(text, wxSizerFlags().CenterVertical().Proportion(1));
    sizer->Add(radiobox, wxSizerFlags().CenterVertical().Border(wxRIGHT, ITEM_RIGHT_PADDING));
    return sizer;
}

PreferencesDialog::PreferencesDialog(wxWindow *parent, wxWindowID id, const wxString &title, const wxPoint &pos, const wxSize &size, long style)
    : DPIDialog(parent, id, _L("Preferences"), pos, size, style)
{
    SetBackgroundColour(*wxWHITE);
    SetSize(wxSize(620, 580));
    m_original_use_12h_time_format = wxGetApp().app_config->get("use_12h_time_format");
    create();
    wxGetApp().UpdateDlgDarkUI(this);
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
        try {
            NetworkAgent* agent = GUI::wxGetApp().getAgent();
            if (agent) {
                json j;
                std::string value;
                value = wxGetApp().app_config->get("auto_calculate_flush");
                j["auto_flushing"] = value;
                agent->track_event("preferences_changed", j.dump());
            }
        } catch(...) {}

        // Check if time format changed
        std::string current_use_12h_time_format = wxGetApp().app_config->get("use_12h_time_format");
        m_use_12h_time_format_changed = (m_original_use_12h_time_format != current_use_12h_time_format);

        event.Skip();
        });
}

//  PreferenceTabbar — plain-text top tabs matching the Preferences Figma:
//  a horizontal row of labels (active = bold dark, inactive = regular grey) with
//  a green underline under the selected tab, over a 1px divider line. Emits the
//  standard wxEVT_CHOICE (int = selected index) when the user clicks a tab.
class PreferenceTabbar : public wxControl
{
public:
    PreferenceTabbar(wxWindow *parent);
    void AddTab(const wxString &label);
    void SetSelection(int sel);
    int  GetSelection() const { return m_selection; }
    void Rescale();

private:
    void                        render();
    std::vector<wxStaticText *> m_labels;
    std::vector<wxWindow *>     m_underlines; // green indicator under each tab
    wxBoxSizer                 *m_row       = nullptr;
    int                         m_selection = -1;
};

PreferenceTabbar::PreferenceTabbar(wxWindow *parent) : wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
{
    SetBackgroundColour(*wxWHITE);
    auto *outer = new wxBoxSizer(wxVERTICAL);
    m_row       = new wxBoxSizer(wxHORIZONTAL);
    outer->Add(m_row, 0, wxLEFT, FromDIP(8));
    auto *line = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    line->SetBackgroundColour(ThemeColor::Grey300);
    outer->Add(line, 0, wxEXPAND);
    SetSizer(outer);
}

void PreferenceTabbar::AddTab(const wxString &label)
{
    const int index = (int) m_labels.size();

    // Each tab is a column: the label on top and a 2px underline below it that
    // turns ThemeColor::BrandGreen when the tab is selected.
    auto *col  = new wxBoxSizer(wxVERTICAL);
    auto *text = new wxStaticText(this, wxID_ANY, label);
    text->SetFont(::Label::Body_14);

    auto *underline = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(2)));
    underline->SetBackgroundColour(this->GetBackgroundColour());

    auto on_click = [this, index](wxMouseEvent &) {
        SetSelection(index);
        wxCommandEvent evt(wxEVT_CHOICE, GetId());
        evt.SetEventObject(this);
        evt.SetInt(index);
        wxPostEvent(this, evt);
    };
    text->Bind(wxEVT_LEFT_DOWN, on_click);
    underline->Bind(wxEVT_LEFT_DOWN, on_click);
    text->Bind(wxEVT_ENTER_WINDOW, [text](wxMouseEvent &e) {
        text->SetCursor(wxCURSOR_HAND);
        e.Skip();
    });

    col->AddStretchSpacer();
    col->Add(text);
    col->AddStretchSpacer();
    col->Add(underline, 0, wxEXPAND);

    m_labels.push_back(text);
    m_underlines.push_back(underline);
    m_row->AddSpacer(FromDIP(32));
    m_row->Add(col, wxSizerFlags().Border(wxRIGHT, FromDIP(48)));
    if (m_selection < 0) SetSelection(0);
}

void PreferenceTabbar::SetSelection(int sel)
{
    if (sel < 0 || sel >= (int) m_labels.size()) return;

    m_selection = sel;
    render();
}

void PreferenceTabbar::render()
{
    for (int i = 0; i < (int) m_labels.size(); ++i) {
        const bool active = (i == m_selection);
        m_labels[i]->SetFont(active ? Label::Head_14 : Label::Body_14);
        m_underlines[i]->SetBackgroundColour(active ? ThemeColor::BrandGreen : GetBackgroundColour());
        m_underlines[i]->Refresh();
    }
    Layout();
    Refresh();
}

void PreferenceTabbar::Rescale() { render(); }

void PreferencesDialog::create()
{
    app_config             = get_app_config();

    // backup switch has two option in the old versions:
    // 1. switch to turn on/off
    // 2. interval
    // in the new verison we use 0 for `not backup`
    if (app_config->get("backup_switch") != "true") { app_config->set("backup_interval", "0"); }
    m_backup_interval_time = app_config->get("backup_interval");

    // set icon for dialog
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));
    SetSizeHints(wxDefaultSize, wxDefaultSize);

    auto main_sizer = new wxBoxSizer(wxVERTICAL);

    m_tabbar = new PreferenceTabbar(this);
    m_book   = new wxSimplebook(this, wxID_ANY);

    auto add_tab = [this](const wxString &label, wxWindow *page) {
        m_tabbar->AddTab(label);
        m_book->AddPage(page, label);
    };
    add_tab(_CTX(L_CONTEXT("General", "Preference"), "Preference"), create_general_tab());
    add_tab(_CTX(L_CONTEXT("User", "Preference"), "Preference"), create_user_tab());
    add_tab(_CTX(L_CONTEXT("3D", "Preference"), "Preference"), create_3d_tab());
    add_tab(_CTX(L_CONTEXT("Other", "Preference"), "Preference"), create_other_tab());

#if !BBL_RELEASE_TO_PUBLIC
    add_tab(_L("Developer Tools"), create_developer_tab());
#endif

    m_tabbar->SetSelection(0);
    m_book->SetSelection(0);
    m_tabbar->Bind(wxEVT_CHOICE, [this](wxCommandEvent &e) { m_book->SetSelection(e.GetInt()); });

    main_sizer->Add(m_tabbar, 0, wxEXPAND | wxTOP, FromDIP(4));
    main_sizer->Add(m_book, 1, wxEXPAND);
    main_sizer->Add(create_bottom_buttons(), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));

    SetSizer(main_sizer);
    Layout();
    Fit();

    // Fixed dialog size matching the Figma panel (~640x640). The multi-tab layout
    // makes each page short, so we no longer stretch the dialog to a fraction of
    // the screen (the old single-scroll-page behavior). Tabs are scrollable, so a
    // tab taller than this simply scrolls. Cap the height to the screen so it
    // still fits on small displays.
    int screen_height = std::numeric_limits<int>::max();
    int count = wxDisplay::GetCount();
    for (int i = 0; i < count; ++i) {
        wxDisplay display(i);
        wxRect rect = display.GetGeometry();
        screen_height  = std::min(screen_height, rect.GetHeight());
    }
    if (screen_height == std::numeric_limits<int>::max()) screen_height = wxGetDisplaySize().GetY();

    const int max_height = int(screen_height * 0.7); // never exceed most of the screen
    this->SetSize(FromDIP(640), std::min(FromDIP(640), max_height));

    CenterOnParent();
    wxPoint start_pos = this->GetPosition();
    if (start_pos.y < 0) { this->SetPosition(wxPoint(start_pos.x, 0)); }
}

PreferencesDialog::~PreferencesDialog()
{
    m_radio_group.DeleteContents(true);
    m_hash_selector.clear();
}

void PreferencesDialog::on_dpi_changed(const wxRect &suggested_rect) {
    for (auto item : m_button_list) {
        item.second->Rescale();
        item.second->SetMinSize(wxSize(FromDIP(BTN_WIDTH), FromDIP(BTN_HEIGHT)));
        item.second->SetCornerRadius(FromDIP(12));
    }
    for (auto item : m_checkbox_list) {
        item.second->Rescale();
    }
    for (auto item : m_radiobox_list) {
        item.second->Rescale();
    }
    for (auto item : m_combobox_list) {
        item.second->Rescale();
    }
    if (m_tabbar) m_tabbar->Rescale();
    this->Refresh();
    Layout();
    Fit();
    int displayIndex = wxDisplay::GetFromWindow(this);
    if (displayIndex != wxNOT_FOUND) {
        wxDisplay display(displayIndex);
        wxRect    screenRect = display.GetGeometry();
        if (m_screen_height != screenRect.GetHeight()) {
            m_screen_height = screenRect.GetHeight();
            // Keep the fixed Figma-matched size (capped to the screen) on a
            // DPI/monitor switch instead of stretching to a fraction of the screen.
            this->SetSize(FromDIP(640), std::min(FromDIP(640), int(m_screen_height * 0.7)));
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " The display screen has switched";
        }
    }
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

wxWindow *PreferencesDialog::create_general_tab()
{
    auto        scrolled = new ScrollPanel(m_book);
    wxBoxSizer *sizer    = new wxBoxSizer(wxVERTICAL);

    auto title_basic = create_item_title(_L("General Settings"), scrolled, _L("General Settings"));

    // Language list (same source as before).
    auto available_translations = wxTranslations::Get()->GetAvailableTranslations(SLIC3R_APP_KEY);
    std::vector<const wxLanguageInfo *> language_infos;
    language_infos.emplace_back(wxLocale::GetLanguageInfo(wxLANGUAGE_ENGLISH));
    for (size_t i = 0; i < available_translations.GetCount(); ++i) {
        const wxLanguageInfo *available_lan = wxLocale::FindLanguageInfo(available_translations[i]);
        if (available_lan == nullptr) continue;
        for (auto si = 0; si < s_supported_languages.size(); si++) {
            auto* supported_lan = wxLocale::GetLanguageInfo(s_supported_languages[si]);
            if (available_lan->CanonicalName == supported_lan->CanonicalName) {
                language_infos.emplace_back(supported_lan);
                break;
            }
        }
    }
    sort_remove_duplicates(language_infos);
    std::sort(language_infos.begin(), language_infos.end(), [](const wxLanguageInfo *l, const wxLanguageInfo *r) { return l->Description < r->Description; });
    auto item_language = create_item_language_combobox(_L("Language"), scrolled, _L("Language"), 50, "language", language_infos);

    std::vector<wxString> Regions     = {_L("Asia-Pacific"), _L("Chinese Mainland"), _L("Europe"), _L("North America"), _L("Others")};
    auto                  item_region = create_item_region_combobox(_L("Login Region"), scrolled, _L("Login Region"), Regions);

    std::vector<wxString> Units         = {_L("Metric") + " (mm, g)", _L("Imperial") + " (in, oz)"};
    auto                  item_currency = create_item_combobox(_L("Units"), scrolled, _L("Units"), "use_inches", Units, {"0", "1"});

#ifdef _WIN32
    auto item_darkmode = create_item_darkmode_checkbox(_L("Enable dark mode"), scrolled, _L("Enable dark mode"), 50, "dark_color_mode");
#endif

    std::vector<wxString>    FlushOptionLabels = {_L("All"), _L("Color change"), _L("Disabled")};
    std::vector<std::string> FlushOptionValues = {"all", "color change", "disabled"};
    auto item_auto_flush = create_item_combobox(_L("Auto Flush"), scrolled, _L("Auto calculate flush volumes"), "auto_calculate_flush", FlushOptionLabels, FlushOptionValues);

    auto item_single_instance = create_item_checkbox(_L("Keep only one Bambu Studio instance"), scrolled,
#if __APPLE__
                                                     _L("On OSX there is always only one instance of app running by default. However it is allowed to run multiple instances "
                                                        "of same app from the command line. In such case this settings will allow only one instance."),
#else
                                                     _L("If this is enabled, when starting Bambu Studio and another instance of the same Bambu Studio is already running, that "
                                                        "instance will be reactivated instead."),
#endif
                                                     50, "single_instance");

    auto item_fila_manager = create_item_checkbox(
        _L("Filament Manager") + " (" + _L("Take effect after restarting Studio") + ")", scrolled,
#if __APPLE__
        _L("The Filament Manager is turned off by default on macOS because compatibility issues on some systems may cause the application to become unresponsive."),
#else
        wxEmptyString,
#endif
        50, FilaManagerEnabledConfigKey);

    auto item_multi_machine = create_item_checkbox(_L("Multi-device Management(Take effect after restarting Studio)."), scrolled,
                                                   _L("With this option enabled, you can send a task to multiple devices at the same time and manage multiple devices."), 50,
                                                   "enable_multi_machine");

    auto item_beta_version_update = create_item_checkbox(_L("Support beta version update."), scrolled, _L("With this option enabled, you can receive beta version updates."), 50,
                                                         "enable_beta_version_update");

    // User Experience Improvement Program + "what data" hyperlink.
    auto  item_priv_policy = create_item_checkbox(_L("Join the User Experience Improvement Program."), scrolled, "", 50, "privacyuse");
    auto *hyperlink        = new Label(scrolled, wxString::FromUTF8(_CTX_utf8(L_CONTEXT("Learn more", "Preferences"), "Preferences")));
    hyperlink->SetFont(Label::Head_13);
    hyperlink->SetForegroundColour(ThemeColor::Link);
    hyperlink->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
    hyperlink->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });
    hyperlink->Bind(wxEVT_LEFT_DOWN, [this](auto &e) {
        UxProgramTermsDialog dlg(this);
        dlg.ShowModal();
    });
    item_priv_policy->GetItem(1)->SetProportion(0);
    item_priv_policy->Insert(item_priv_policy->GetItemCount() - 1, hyperlink, wxSizerFlags().CenterVertical().Proportion(1));

    // Download path row lives inside the General Settings section (Figma:
    // "下载地址" as a plain row, no separate "Downloads" section title).
    auto item_downloads = create_item_downloads(scrolled, 50, "download_path");

    sizer->Add(title_basic, wxSizerFlags().Expand().Border(wxTOP, FromDIP(24)));
    sizer->AddSpacer(FromDIP(8));
    auto flags = wxSizerFlags().Expand().Border(wxTOP, FromDIP(4));

    sizer->Add(item_language, flags);
    sizer->Add(item_region, flags);
    sizer->Add(item_currency, flags);
    sizer->Add(item_auto_flush, flags);
#ifdef _WIN32
    sizer->Add(item_darkmode, flags);
#endif
    sizer->Add(item_single_instance, flags);
    sizer->Add(item_fila_manager, flags);
    sizer->Add(item_multi_machine, flags);
    sizer->Add(item_beta_version_update, flags);
    sizer->Add(item_priv_policy, flags);
    sizer->Add(item_downloads, flags);
    scrolled->SetSizer(sizer);
    scrolled->FitInside();
    return scrolled;
}

wxWindow *PreferencesDialog::create_user_tab()
{
    auto        scrolled = new ScrollPanel(m_book);
    scrolled->SetBackgroundColour(*wxWHITE);
    wxBoxSizer *sizer    = new wxBoxSizer(wxVERTICAL);

    auto title_user = create_item_title(_L("User Settings"), scrolled, _L("User Settings"));

    auto item_bed_type_follow_preset = create_item_checkbox(_L("Auto plate type"), scrolled, _L("Studio will remember build plate selected last time for certain printer model."),
                                                            50, "user_bed_type");

    // 打印进度计时方式 — 2-option select (12h / 24h) over use_12h_time_format.
    std::vector<wxString>    time_labels = {_L("12-hour"), _L("24-hour")};
    std::vector<std::string> time_values = {"1", "0"};
    auto item_time_format                = create_item_combobox(_L("time format for print progress"), scrolled, _L("Display time in 12-hour format with AM/PM instead of 24-hour format"),
                                                                "use_12h_time_format", time_labels, time_values);

    auto item_auto_stop_liveview =
        create_item_checkbox(_L("Keep liveview when printing."), scrolled,
                             _L("By default, Liveview will pause after 15 minutes of inactivity on the computer. Check this box to disable this feature during printing."), 50,
                             "auto_stop_liveview");

    auto item_auto_transfer = create_item_checkbox(_L("Automatically transfer modified value when switching process and filament presets"), scrolled,
                                                   _L("After closing, a popup will appear to ask each time"), 50, "auto_transfer_when_switch_preset");

    auto item_mix_print_high_low_temp = create_item_checkbox(_L("Remove the restriction on mixed printing of high and low temperature filaments."), scrolled,
                                                             _L("With this option enabled, you can print materials with a large temperature difference together."), 50,
                                                             "enable_high_low_temp_mixed_printing");

    auto item_user_sync = create_item_checkbox(_L("Auto sync user presets(Printer/Filament/Process)"), scrolled,
                                               _L("If enabled, auto sync user presets with cloud after Bambu Studio startup or presets modified."), 50, "sync_user_preset");

    auto item_system_sync = create_item_checkbox(_L("Auto check for system presets updates"), scrolled,
                                                 _L("If enabled, auto check whether there are system presets updates after Bambu Studio startup."), 50, "sync_system_preset");

#ifdef _WIN32
    auto item_webview_auto_fill = create_item_checkbox(_L("Auto-fill previously logged-in accounts."), scrolled, _L(""), 50, "webview_auto_fill");
#endif

    sizer->Add(title_user, wxSizerFlags().Expand().Border(wxTOP, FromDIP(24)));
    sizer->AddSpacer(FromDIP(8));
    auto flags = wxSizerFlags().Expand().Border(wxTOP, FromDIP(4));

    sizer->Add(item_time_format, flags);
    sizer->Add(item_bed_type_follow_preset, flags);
    sizer->Add(item_auto_stop_liveview, flags);
    sizer->Add(item_auto_transfer, flags);
    sizer->Add(item_mix_print_high_low_temp, flags);
    sizer->Add(item_user_sync, flags);
    sizer->Add(item_system_sync, flags);
#ifdef _WIN32
    sizer->Add(item_webview_auto_fill, flags);
#endif

    scrolled->SetSizer(sizer);
    scrolled->FitInside();
    return scrolled;
}

wxWindow *PreferencesDialog::create_3d_tab()
{
    auto        scrolled = new ScrollPanel(m_book);
    wxBoxSizer *sizer    = new wxBoxSizer(wxVERTICAL);

    auto title_3d = create_item_title(_L("3D Settings"), scrolled, _L("3D Settings"));

    auto item_zoom_to_mouse = create_item_checkbox(_L("Zoom to mouse position"), scrolled,
                                                   _L("Zoom in towards the mouse pointer's position in the 3D view, rather than the 2D window center."), 50, "zoom_to_mouse");

    std::vector<wxString> assemble_view_preview_options = {_L("Auto"), _L("Open"), _L("Close")};
    auto                  enable_assemble_view_preview  = create_item_combobox(
        _L("Display overview"), scrolled, _L("Display overview"), "enable_assemble_view_preview", assemble_view_preview_options, {"Auto", "Open", "Close"},
        [](int idx) {
            wxGetApp().app_config->set("enable_assemble_view_preview", idx == 0 ? "Auto" : idx == 1 ? "Open" : "Close");
            if (wxGetApp().app_config->get("enable_assemble_view_preview") == "Auto")
                wxGetApp().app_config->set_bool("enable_bvh", true);
            else if (wxGetApp().app_config->get("enable_assemble_view_preview") == "Open")
                wxGetApp().app_config->set_bool("enable_bvh", false);
        },
        FromDIP(150), FromDIP(120));

    float range_min = 1.0f, range_max = 2.5f;
    auto  item_grabber_size = create_item_range_input(_L("Grabber scale"), scrolled,
                                                      _L("Set grabber size for move,rotate,scale tool.") + _L("Value range") + ":[" + std::to_string(range_min) + "," +
                                                          std::to_string(range_max) + "]",
                                                      "grabber_size_factor", range_min, range_max, 1, [](wxString value) {
                                                         double d_value = 0;
                                                         if (value.ToDouble(&d_value)) GLGizmoBase::Grabber::GrabberSizeFactor = d_value;
                                                     });

    range_min                = 0.0f;
    range_max                = 150.0f;
    auto item_tooltip_offset = create_item_range_two_input(_L("Tooltip offset"), scrolled,
                                                           _L("Set tooltip offset for different mouse size.") + _L("Value range") + ":[" + std::to_string(range_min) + "," +
                                                               std::to_string(range_max) + "]",
                                                           "3d_middle_tooltip_offset_x", "3d_middle_tooltip_offset_y", range_min, range_max, 1, nullptr, nullptr);

    std::vector<wxString> toolbar_style = {_L("Collapsible"), _L("Uncollapsible")};
    auto item_toolbar_style = create_item_combobox(_L("Toolbar Style"), scrolled, _L("Toolbar Style"), "toolbar_style", toolbar_style, {"0", "1"}, [](int idx) -> void {
        const auto &p_ogl_manager = wxGetApp().get_opengl_manager();
        p_ogl_manager->set_toolbar_rendering_style(idx);
    });

    auto item_show_shells = create_item_checkbox(_L("Always show shells in preview"), scrolled,
                                                 _L("Always show shells or not in preview view tab. If you change this value, you should reslice."), 50,
                                                 "show_shells_in_preview");

    auto item_step_mesh_setting = create_item_checkbox(_L("Show the step mesh parameter setting dialog."), scrolled,
                                                       _L("If enabled,a parameter settings dialog will appear during STEP file import."), 50, "enable_step_mesh_setting");

    auto item_import_svg = create_item_checkbox(_L("Import a single SVG and split it"), scrolled, _L("Import a single SVG and then split it to several parts."), 50,
                                                "import_single_svg_and_split");

    auto item_gamma_obj = create_item_checkbox(_L("Enable gamma correction for the imported obj file"), scrolled,
                                               _L("Perform gamma correction on color after importing the obj model."), 50, "gamma_correct_in_import_obj");

    auto item_enable_record_gcodeviewer =
        create_item_checkbox(_L("Remember last used color scheme"), scrolled,
                             _L("When enabled, the last used color scheme (e.g., Line Type, Speed) will be automatically applied on next startup."), 50,
                             "enable_record_gcodeviewer_option_item");

    auto item_enable_lod = create_item_checkbox(_L("Improve rendering performance by lod"), scrolled,
                                                _L("Improved rendering performance under the scene of multiple plates and many models."), 50, "enable_lod");

    auto item_advanced_gcode = create_item_checkbox(_L("Enable advanced gcode viewer"), scrolled, _L("Enable advanced gcode viewer."), 50, "enable_advanced_gcode_viewer_");

    sizer->Add(title_3d, wxSizerFlags().Expand().Border(wxTOP, FromDIP(24)));
    sizer->AddSpacer(FromDIP(8));
    auto flags = wxSizerFlags().Expand().Border(wxTOP, FromDIP(4));

    sizer->Add(enable_assemble_view_preview, flags);
    sizer->Add(item_grabber_size, flags);
    sizer->Add(item_tooltip_offset, flags);
    sizer->Add(item_toolbar_style, flags);
    sizer->Add(item_zoom_to_mouse, flags);
    sizer->Add(item_show_shells, flags);
#if !BBL_RELEASE_TO_PUBLIC
    auto item_show_bvh_bounds = create_item_checkbox(_L("Show assembly BVH primary bounds"), scrolled, _L("Display the BVH primary bounding box wireframe in assembly view."), 50,
                                                     "show_assembly_bvh_bounds");
    sizer->Add(item_show_bvh_bounds, flags);
#endif
    sizer->Add(item_step_mesh_setting, flags);
    sizer->Add(item_import_svg, flags);
    sizer->Add(item_gamma_obj, flags);
    sizer->Add(item_enable_record_gcodeviewer, flags);
    sizer->Add(item_enable_lod, flags);
    sizer->Add(item_advanced_gcode, flags);

    // [refactor-review] Not in Figma v2 3D tab; camera-fullscreen kept here (a 3D/
    // viewport-adjacent toggle). Reviewer: confirm placement.
    auto item_camera_fullscreen = create_item_checkbox(_L("Open full screen camera view on active monitor only."), scrolled,
                                                       _L("When enabled, the camera full screen view opens only on the monitor that contains Bambu Studio."), 50,
                                                       "camera_fullscreen_active_monitor_only");
    sizer->Add(item_camera_fullscreen, flags); // [refactor-review]

    sizer->AddSpacer(FromDIP(20));
    scrolled->SetSizer(sizer);
    scrolled->FitInside();
    return scrolled;
}

wxWindow *PreferencesDialog::create_other_tab()
{
    auto        scrolled = new ScrollPanel(m_book);
    wxBoxSizer *sizer    = new wxBoxSizer(wxVERTICAL);

    // ---- Project ----
    auto title_project         = create_item_title(_L("Project"), scrolled, "");
    auto item_max_recent_count = create_item_input(_L("Maximum recent projects"), "", scrolled, _L("Maximum count of recent projects"), "max_recent_count", [](wxString value) {
        long max = 0;
        if (value.ToLong(&max)) wxGetApp().mainframe->set_max_recent_count(max);
    });
    auto item_gcodes_warning = create_item_checkbox(_L("No warnings when loading 3MF with modified G-codes"), scrolled, _L("No warnings when loading 3MF with modified G-codes"),
                                                    50, "no_warn_when_modified_gcodes");
    std::vector<wxString>    backup_labels = {_L("10 seconds"), _L("20 seconds"), _L("30 seconds"), _L("1 minute"), _L("2 minutes"),
                                              _L("5 minutes"),  _L("10 minutes"), _L("30 minutes"), _L("never")};
    std::vector<std::string> backup_values = {"10", "20", "30", "60", "120", "300", "600", "1800", "0"};
    auto item_auto_backup = create_item_combobox(_L("Auto-Backup"), scrolled, _L("The peroid of backup in seconds."), "backup_interval", backup_labels, backup_values,
                                                 [this](int) {
                                                     m_backup_interval_time = app_config->get("backup_interval");
                                                     long backup_interval   = 0;
                                                     m_backup_interval_time.ToLong(&backup_interval);
                                                     Slic3r::set_backup_interval(backup_interval);
                                                 });

    sizer->Add(title_project, wxSizerFlags().Expand().Border(wxTOP, FromDIP(16)));
    sizer->AddSpacer(FromDIP(8));
    auto flags = wxSizerFlags().Expand().Border(wxTOP, FromDIP(4));

    sizer->Add(item_max_recent_count, flags);
    sizer->Add(item_auto_backup, flags);
    sizer->Add(item_gcodes_warning, flags);

    // ---- Online Models (visible only when has_model_mall()) ----
    auto title_modelmall   = create_item_title(_L("Online Models"), scrolled, _L("Online Models"));
    auto item_modelmall    = create_item_checkbox(_L("Show online staff-picked models on the home page"), scrolled, _L("Show online staff-picked models on the home page"), 50,
                                                  "staff_pick_switch");
    auto item_show_history = create_item_checkbox(_L("Show history on the home page"), scrolled, _L("Show history on the home page"), 50, "show_print_history");

    auto title_modelmall_item   = sizer->Add(title_modelmall, wxSizerFlags().Expand().Border(wxTOP, FromDIP(16)));
    auto item_modelmall_item    = sizer->Add(item_modelmall, flags);
    auto item_show_history_item = sizer->Add(item_show_history, flags);

    auto update_modelmall = [scrolled, title_modelmall_item, item_modelmall_item, item_show_history_item](wxEvent &) {
        bool has_model_mall = wxGetApp().has_model_mall();
        title_modelmall_item->Show(has_model_mall);
        item_modelmall_item->Show(has_model_mall);
        item_show_history_item->Show(has_model_mall);
        scrolled->Layout();
        scrolled->FitInside();
    };
    wxCommandEvent dummy(wxEVT_COMBOBOX);
    update_modelmall(dummy);

    // ---- Developer Mode (Figma keeps these two here, in the Other tab) ----
    auto title_dev           = create_item_title(_L("Developer Mode"), scrolled, _L("Developer Mode"));
    auto item_dev_mode       = create_item_checkbox(_L("Develop mode"), scrolled, _L("Develop mode"), 50, "developer_mode");
    auto item_skip_blacklist = create_item_checkbox(_L("Skip AMS blacklist check"), scrolled, _L("Skip AMS blacklist check"), 50, "skip_ams_blacklist_check");
    sizer->Add(title_dev, wxSizerFlags().Expand().Border(wxTOP, FromDIP(16)));
    sizer->Add(item_dev_mode, flags);
    sizer->Add(item_skip_blacklist, flags);

#ifdef _WIN32
    // ---- Associate Files To Bambu Studio (Windows only) ----
    auto title_associate_file = create_item_title(_L("Associate Files To Bambu Studio"), scrolled, _L("Associate Files To Bambu Studio"));
    auto item_associate_3mf   = create_item_checkbox(_L("Associate .3mf files to Bambu Studio"), scrolled,
                                                     _L("If enabled, sets Bambu Studio as default application to open .3mf files"), 50, "associate_3mf");
    auto item_associate_stl   = create_item_checkbox(_L("Associate .stl files to Bambu Studio"), scrolled,
                                                     _L("If enabled, sets Bambu Studio as default application to open .stl files"), 50, "associate_stl");
    auto item_associate_step  = create_item_checkbox(_L("Associate .step/.stp files to Bambu Studio"), scrolled,
                                                     _L("If enabled, sets Bambu Studio as default application to open .step files"), 50, "associate_step");
    sizer->Add(title_associate_file, wxSizerFlags().Expand().Border(wxTOP, FromDIP(16)));
    sizer->Add(item_associate_3mf, flags);
    sizer->Add(item_associate_stl, flags);
    sizer->Add(item_associate_step, flags);
#endif

    sizer->AddSpacer(FromDIP(20));
    scrolled->SetSizer(sizer);
    scrolled->FitInside();
    return scrolled;
}

wxWindow *PreferencesDialog::create_developer_tab()
{
    auto        scrolled = new ScrollPanel(m_book);
    wxBoxSizer *sizer    = new wxBoxSizer(wxVERTICAL);

    m_internal_developer_mode_def = app_config->get("internal_developer_mode");
    m_iot_environment_def         = app_config->get("iot_environment");

    // ---- Log ----
    auto title_log  = create_item_title(_L("Log"), scrolled, _L("Log"));
    auto log_levels = std::vector<wxString>{_L("fatal"), _L("error"), _L("warning"), _L("info"), _L("debug"), _L("trace")};
    auto item_log   = create_item_loglevel_combobox(_L("Log Level"), scrolled, _L("Log Level"), log_levels);
    sizer->Add(title_log, wxSizerFlags().Expand().Border(wxTOP, FromDIP(24)));
    sizer->AddSpacer(FromDIP(8));
    auto flags = wxSizerFlags().Expand().Border(wxTOP, FromDIP(4));

    sizer->AddSpacer(FromDIP(4));
    sizer->Add(item_log, flags);

    auto title_dev         = create_item_title(_L("Developer Tools"), scrolled, _L("Developer Tools"));
    auto item_internal_dev = create_item_checkbox(_L("Internal developer mode"), scrolled, _L("Internal developer mode"), 50, "internal_developer_mode");
    auto item_ssl_mqtt     = create_item_checkbox(_L("Enable SSL(MQTT)"), scrolled, _L("Enable SSL(MQTT)"), 50, "enable_ssl_for_mqtt");
    auto item_ssl_ftp      = create_item_checkbox(_L("Enable SSL(FTP)"), scrolled, _L("Enable SSL(FTP)"), 50, "enable_ssl_for_ftp");

    auto title_host = create_item_title(_L("Host Setting"), scrolled, _L("Host Setting"));
    auto radio1     = create_item_radiobox(_L("DEV host: api-dev.bambu-lab.com/v1"), scrolled, wxEmptyString, 50, 1, "dev_host");
    auto radio2     = create_item_radiobox(_L("QA  host: api-qa.bambu-lab.com/v1"), scrolled, wxEmptyString, 50, 1, "qa_host");
    auto radio3     = create_item_radiobox(_L("PRE host: api-pre.bambu-lab.com/v1"), scrolled, wxEmptyString, 50, 1, "pre_host");
    auto radio4     = create_item_radiobox(_L("Product host"), scrolled, wxEmptyString, 50, 1, "product_host");

    if (m_iot_environment_def == ENV_DEV_HOST) {
        on_select_radio("dev_host");
    } else if (m_iot_environment_def == ENV_QAT_HOST) {
        on_select_radio("qa_host");
    } else if (m_iot_environment_def == ENV_PRE_HOST) {
        on_select_radio("pre_host");
    } else if (m_iot_environment_def == ENV_PRODUCT_HOST) {
        on_select_radio("product_host");
    }

    StateColor btn_bg_white(std::pair<wxColour, int>(ThemeColor::Grey400, StateColor::Disabled), std::pair<wxColour, int>(ThemeColor::Grey400, StateColor::Pressed),
                            std::pair<wxColour, int>(ThemeColor::Grey300, StateColor::Hovered), std::pair<wxColour, int>(ThemeColor::White, StateColor::Normal));
    StateColor btn_bd_white(std::pair<wxColour, int>(ThemeColor::White, StateColor::Disabled), std::pair<wxColour, int>(ThemeColor::TextPrimary, StateColor::Enabled));

    Button *debug_button                = new Button(scrolled, _L("debug save button"));
    m_button_list[m_button_list.size()] = debug_button;
    debug_button->SetBackgroundColor(btn_bg_white);
    debug_button->SetBorderColor(btn_bd_white);
    debug_button->SetFont(Label::Body_13);
    debug_button->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) {
        // success message box
        MessageDialog dialog(this, _L("save debug settings"), _L("DEBUG settings have saved successfully!"), wxNO_DEFAULT | wxYES_NO | wxICON_INFORMATION);
        dialog.SetSize(400,-1);
        switch (dialog.ShowModal()) {
        case wxID_NO: {
            //if (m_developer_mode_def != app_config->get("developer_mode")) {
            //    app_config->set_bool("developer_mode", m_developer_mode_def == "true" ? true : false);
            //    m_developer_mode_ckeckbox->SetValue(m_developer_mode_def == "true" ? true : false);
            //}
            //if (m_internal_developer_mode_def != app_config->get("internal_developer_mode")) {
            //    app_config->set_bool("internal_developer_mode", m_internal_developer_mode_def == "true" ? true : false);
            //    m_internal_developer_mode_ckeckbox->SetValue(m_internal_developer_mode_def == "true" ? true : false);
            //}

            if (m_iot_environment_def == ENV_DEV_HOST) {
                on_select_radio("dev_host");
            } else if (m_iot_environment_def == ENV_QAT_HOST) {
                on_select_radio("qa_host");
            } else if (m_iot_environment_def == ENV_PRE_HOST) {
                on_select_radio("pre_host");
            } else if (m_iot_environment_def == ENV_PRODUCT_HOST) {
                on_select_radio("product_host");
            }

            break;
        }

        case wxID_YES: {
            // bbs  domain changed
            auto param = get_select_radio(1);

            std::map<wxString, wxString> iot_environment_map;
            iot_environment_map["dev_host"] = ENV_DEV_HOST;
            iot_environment_map["qa_host"]  = ENV_QAT_HOST;
            iot_environment_map["pre_host"] = ENV_PRE_HOST;
            iot_environment_map["product_host"] = ENV_PRODUCT_HOST;

            //if (iot_environment_map[param] != m_iot_environment_def) {
            if (true) {
                NetworkAgent* agent = wxGetApp().getAgent();
                if (param == "dev_host") {
                    app_config->set("iot_environment", ENV_DEV_HOST);
                }
                else if (param == "qa_host") {
                    app_config->set("iot_environment", ENV_QAT_HOST);
                }
                else if (param == "pre_host") {
                    app_config->set("iot_environment", ENV_PRE_HOST);
                }
                else if (param == "product_host") {
                    app_config->set("iot_environment", ENV_PRODUCT_HOST);
                }


                wxGetApp().update_publish_status();

                AppConfig* config = GUI::wxGetApp().app_config;
                std::string country_code = config->get_country_code();
                if (agent) {
                    wxGetApp().request_user_logout();
                    agent->set_country_code(country_code);
                }
                ConfirmBeforeSendDialog confirm_dlg(this, wxID_ANY, _L("Warning"), ConfirmBeforeSendDialog::ButtonStyle::ONLY_CONFIRM);
                confirm_dlg.update_text(_L("Switch cloud environment, Please login again!"));
                confirm_dlg.on_show();
            }

            // bbs  backup
            //app_config->set("backup_interval", std::string(m_backup_interval_time.mb_str()));
            app_config->save();
            Slic3r::set_backup_interval(boost::lexical_cast<long>(app_config->get("backup_interval")));

            this->Close();
            break;
        }
        }
    });

    sizer->Add(title_dev, wxSizerFlags().Expand().Border(wxTOP, FromDIP(24)));
    sizer->AddSpacer(FromDIP(8));

    sizer->Add(item_internal_dev, flags);
    sizer->Add(item_ssl_mqtt, flags);
    sizer->Add(item_ssl_ftp, flags);

    sizer->Add(title_host, wxSizerFlags().Expand().Border(wxTOP, FromDIP(24)));
    sizer->Add(radio1, flags);
    sizer->Add(radio2, flags);
    sizer->Add(radio3, flags);
    sizer->Add(radio4, flags);
    sizer->Add(debug_button, wxSizerFlags().Center());

    scrolled->SetSizer(sizer);
    scrolled->FitInside();
    return scrolled;
}

// ============================================================================
//  Bottom actions: Reset all warning dialogs / Reset preferences.
// ============================================================================
wxBoxSizer *PreferencesDialog::create_bottom_buttons()
{
    auto *row = new wxBoxSizer(wxHORIZONTAL);

    auto *btn_reset_warnings            = new Button(this, _L("Reset all warning dialogs"));
    auto *btn_reset_prefs               = new Button(this, _L("Reset preferences"));
    m_button_list[m_button_list.size()] = btn_reset_warnings;
    m_button_list[m_button_list.size()] = btn_reset_prefs;

    StateColor btn_bg(std::pair<wxColour, int>(ThemeColor::Grey400, StateColor::Disabled), std::pair<wxColour, int>(ThemeColor::BrandGreenHovered, StateColor::Hovered),
                      std::pair<wxColour, int>(ThemeColor::Grey300, StateColor::Normal));
    for (Button *b : {btn_reset_warnings, btn_reset_prefs}) {
        b->SetBackgroundColor(btn_bg);
        b->SetFont(Label::Body_13);
        b->SetCornerRadius(FromDIP(6));
    }

    btn_reset_warnings->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { on_reset_all_warnings(); });
    btn_reset_prefs->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { on_reset_preferences(); });

    row->AddStretchSpacer();
    row->Add(btn_reset_warnings, 0, wxRIGHT, FromDIP(8));
    row->Add(btn_reset_prefs, 0, 0, 0);
    row->AddStretchSpacer();
    return row;
}

ResetWarningsDialog::ResetWarningsDialog(wxWindow *parent) : DPIDialog(parent, wxID_ANY, _L("Reset"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    SetBackgroundColour(*wxWHITE);
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    const int content_width = FromDIP(417);

    auto *main_sizer = new wxBoxSizer(wxVERTICAL);

    // Body text.
    auto *msg = new wxStaticText(this, wxID_ANY,
                                 _L("All warning dialogs that you have disabled by checking \"Don't show again\" "
                                    "are now re-enabled and will show next time they apply."));
    msg->SetForegroundColour(ThemeColor::TextPrimary);
    msg->SetFont(::Label::Body_14);
    msg->Wrap(content_width);
    msg->SetMinSize(wxSize(content_width, -1));
    main_sizer->Add(msg, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(20));

    StateColor btn_bg_gray(std::pair<wxColour, int>(ThemeColor::Grey400, StateColor::Pressed), std::pair<wxColour, int>(ThemeColor::Grey200, StateColor::Hovered),
                           std::pair<wxColour, int>(ThemeColor::White, StateColor::Normal));
    m_details_btn = new Button(this, _L("Check details"));
    m_details_btn->SetBackgroundColor(btn_bg_gray);
    m_details_btn->SetBorderColor(ThemeColor::Grey450);
    m_details_btn->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_details_btn->SetFont(::Label::Body_12);
    m_details_btn->SetCornerRadius(FromDIP(6));
    m_details_btn->SetCanFocus(false);
    m_details_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { toggle_details(); });
    main_sizer->Add(m_details_btn, 0, wxLEFT | wxTOP, FromDIP(16));

    // Collapsible grey details panel (hidden initially).
    m_details_panel = new wxPanel(this, wxID_ANY);
    m_details_panel->SetBackgroundColour(ThemeColor::Grey300);
    auto *det_sizer = new wxBoxSizer(wxVERTICAL);
    auto *det_text  = new wxStaticText(m_details_panel, wxID_ANY,
                                       _L("- Sync printer presets after loading a file\n"
                                          "- \"Load 3MF\" dialog settings\n"
                                          "- Executing post-processing scripts\n"
                                          "- Support structure recommendation prompt\n"
                                          "- Unsaved projects.\n"
                                          "- Mixed color sublayer with variable layer height warning"));
    det_text->SetForegroundColour(ThemeColor::TextSecondary);
    det_text->SetFont(::Label::Body_13);
    det_sizer->Add(det_text, 0, wxALL, FromDIP(12));
    m_details_panel->SetSizer(det_sizer);
    m_details_panel->Hide();
    main_sizer->Add(m_details_panel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(20));

    // Footer buttons.
    auto *btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    btn_sizer->AddStretchSpacer(1);
    auto      *cancel_btn = new Button(this, _L("Cancel"));
    cancel_btn->SetBackgroundColor(btn_bg_gray);
    cancel_btn->SetBorderColor(ThemeColor::Grey450);
    cancel_btn->SetTextColor(ThemeColor::TextPrimary);
    cancel_btn->SetFont(::Label::Body_12);
    cancel_btn->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    cancel_btn->SetCornerRadius(FromDIP(6));
    cancel_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { EndModal(wxID_CANCEL); });
    btn_sizer->Add(cancel_btn, 0, wxRIGHT, FromDIP(8));

    StateColor btn_bg_green(std::pair<wxColour, int>(ThemeColor::BrandGreenPressed, StateColor::Pressed),
                            std::pair<wxColour, int>(ThemeColor::BrandGreenHovered, StateColor::Hovered), std::pair<wxColour, int>(ThemeColor::BrandGreen, StateColor::Normal));
    auto      *confirm_btn = new Button(this, _L("Confirm"));
    confirm_btn->SetBackgroundColor(btn_bg_green);
    confirm_btn->SetBorderColor(ThemeColor::BrandGreen);
    confirm_btn->SetTextColor(*wxWHITE);
    confirm_btn->SetFont(::Label::Body_12);
    confirm_btn->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    confirm_btn->SetCornerRadius(FromDIP(6));
    confirm_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { EndModal(wxID_OK); });
    btn_sizer->Add(confirm_btn, 0, 0, 0);

    main_sizer->Add(btn_sizer, 0, wxEXPAND | wxALL, FromDIP(20));

    SetSizer(main_sizer);
    Layout();
    Fit();
    CentreOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

void ResetWarningsDialog::toggle_details()
{
    m_expanded = !m_expanded;
    m_details_panel->Show(m_expanded);
    m_details_btn->SetLabel(m_expanded ? _L("Collapse") : _L("Check details"));
    Layout();
    Fit();
}

void PreferencesDialog::on_reset_all_warnings()
{
    // Expandable confirm dialog (Figma 4293-4971 collapsed / 4293-4972 expanded):
    // "Check details" reveals the list of settings that get cleared.
    ResetWarningsDialog dlg(this);
    if (dlg.ShowModal() != wxID_OK) return;

    // Keys that the various "Don't show again"-style dialogs persist their
    // dismissal choice under. Erasing them re-enables the corresponding dialog.
    app_config->erase("app", "sync_after_load_file_show_flag");
    app_config->erase("app", "skip_non_bambu_3mf_warning");
    app_config->erase("app", "post_process_script_choice");
    app_config->erase("app", "no_warn_mixed_sublayer_variable_layer");
    app_config->set("show_support_recommend_dialog", "true");
    app_config->set("save_project_choise", "");
    if (wxGetApp().plater()) wxGetApp().plater()->reset_post_process_script_choice();
    app_config->save();
}

void PreferencesDialog::on_reset_preferences()
{
    MessageDialog dlg(this, _L("Are you sure you want to reset all preferences? Changes will take effect after restart."), _L("Reset"), wxICON_QUESTION | wxOK | wxCANCEL);
    if (dlg.ShowModal() != wxID_OK) return;

    // Reset to factory defaults. Touch only UI preference keys — keep vendor /
    // printer / preset state intact, which AppConfig::reset() would also clear.
    static const char *kPrefKeys[] = {
        "language",
        // "region", keep this intensinaly to avoid re-login
        "use_inches",
        "dark_color_mode",
        "auto_calculate_flush",
        "single_instance",
        FilaManagerEnabledConfigKey,
        "enable_multi_machine",
        "enable_beta_version_update",
        "privacyuse",
        "download_path",
        "webview_auto_fill",
        "associate_3mf",
        "associate_stl",
        "associate_step",
        "user_bed_type",
        "use_12h_time_format",
        "auto_stop_liveview",
        "auto_transfer_when_switch_preset",
        "enable_high_low_temp_mixed_printing",
        "sync_user_preset",
        "sync_system_preset",
        "disable_fins_extrude_safe_temp",
        "zoom_to_mouse",
        "enable_assemble_view_preview",
        "grabber_size_factor",
        "3d_middle_tooltip_offset_x",
        "3d_middle_tooltip_offset_y",
        "toolbar_style",
        "show_shells_in_preview",
        "enable_step_mesh_setting",
        "import_single_svg_and_split",
        "gamma_correct_in_import_obj",
        "enable_lod",
        "enable_advanced_gcode_viewer_",
        "camera_fullscreen_active_monitor_only",
        "max_recent_count",
        "no_warn_when_modified_gcodes",
        "backup_switch",
        "backup_interval",
        "staff_pick_switch",
        "show_print_history",
        "developer_mode",
        "skip_ams_blacklist_check",
        "severity_level",
    };
    for (const char *k : kPrefKeys) app_config->erase("app", k);
    app_config->set_defaults();
    app_config->save();
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


}} // namespace Slic3r::GUI
