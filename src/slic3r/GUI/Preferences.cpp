#include "Preferences.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "OpenGLManager.hpp"
#include "Plater.hpp"
#include "MsgDialog.hpp"
#include "I18N.hpp"
#include "UxProgramTermsDialog.hpp"
#include "libslic3r/AppConfig.hpp"
#include "ReleaseNote.hpp"
#include "Tabbook.hpp"
#include "fila_manager/wgtFilaManagerFeature.h"
#include "Gizmos/GLGizmoBase.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/LinkLabel.hpp"
#include "Widgets/RadioBox.hpp"
#include "Widgets/StaticLine.hpp"
#include "Widgets/TextInput.hpp"
#include <wx/listimpl.cpp>

#ifdef __WINDOWS__
#ifdef _MSW_DARK_MODE
#include "dark_mode.hpp"
#endif // _MSW_DARK_MODE
#endif //__WINDOWS__

#define DESIGN_GRAY900_COLOR      wxColour(38, 46, 48)
#define DESIGN_GRAY800_COLOR      wxColour(50, 58, 61)
#define DESIGN_GRAY600_COLOR      wxColour(144, 144, 144)
#define DESIGN_GRAY400_COLOR      wxColour(166, 169, 170)
#define DESIGN_CONTENT_BG_COLOR   *wxWHITE
#define DESIGN_SIDEBAR_BG_COLOR   wxColour(241, 241, 241)
#define DESIGN_HIGHLIGHT_COLOR    wxColour(0, 174, 66)  // BBL green

#define DESIGN_PAGE_PADDING          FromDIP(12)  // padding around page content
#define DESIGN_ITEM_SPACING          FromDIP(3)   // vertical spacing between items
#define DESIGN_SECTION_SPACING       DESIGN_PAGE_PADDING  // vertical spacing between titled sections on a page
#define DESIGN_ITEM_INDENT           FromDIP(23)  // left side offset for all editable properties, indent relative to section titles
#define DESIGN_INPUT_PAD             FromDIP(8)   // horizontal space between label(s) and input widget

#define DESIGN_LABEL_W               100          // common width of labels on left side of input (forms an even column)
#define DESIGN_COMBO_W               140
#define DESIGN_COMBO_LRG_W           160
#define DESIGN_INPUT_W               100          // default text box width
#define DESIGN_BUTTON_RADIUS         12
#define DESIGN_BUTTON_SIZE           wxSize(58, 22)

namespace Slic3r { namespace GUI {

WX_DEFINE_LIST(RadioSelectorList);

// Scrollable panel container for property pages.
class ScrolledPanel : public wxScrolledWindow
{
    bool m_has_top_title { false };
    wxBoxSizer *m_content_sizer;
public:
    ScrolledPanel(wxWindow* parent, bool top_level = true) :
        m_content_sizer{ new wxBoxSizer(wxVERTICAL) },
        wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNO_BORDER | wxTAB_TRAVERSAL)
    {
        if (top_level)
            SetScrollRate(FromDIP(15), FromDIP(15));
        SetBackgroundColour(DESIGN_CONTENT_BG_COLOR);
        SetSizer(new wxBoxSizer(wxVERTICAL));
        GetSizer()->Add(m_content_sizer, 0, wxEXPAND | wxALL, top_level ? DESIGN_PAGE_PADDING : 0);
    }

    wxSizerItem* Add(wxSizerItem *item)
    {
        if (item->GetBorder() == wxDefaultCoord)
            item->SetBorder(DESIGN_ITEM_SPACING);
        if (!item->GetFlag())
            item->SetFlag(wxTOP | wxEXPAND);
        return m_content_sizer->Add(item);
    }

    wxSizerItem* Add(wxWindow *window, int padding = wxDefaultCoord, int flag = 0) {
        return Add(new wxSizerItem(window, 0, flag, padding));
    }

    wxSizerItem* Add(wxSizer *sizer, int padding = wxDefaultCoord, int flag = 0) {
        return Add(new wxSizerItem(sizer, 0, flag, padding));
    }

    wxSizerItem* AddTitle(const wxString &title, const wxString &tooltip = wxEmptyString, int padding = wxDefaultCoord, int flag = 0)
    {
        auto line = new StaticLine(this, false, title);
        line->SetFont(::Label::Head_13);
        line->SetToolTip(tooltip);

        wxBoxSizer *sizer = new wxBoxSizer(wxHORIZONTAL);
        sizer->Add(line, 1, wxEXPAND | wxBOTTOM, FromDIP(3));

        if (padding == wxDefaultCoord)
            padding = m_has_top_title ? DESIGN_SECTION_SPACING : 0;
        m_has_top_title = true;

        return Add(sizer, padding, flag);
    }

    wxSizerItem* AddSubtitle(const wxString &title, const wxString &tooltip = wxEmptyString)
    {
        auto text = new wxStaticText(this, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, 0);
        text->SetForegroundColour(DESIGN_GRAY800_COLOR);
        text->SetFont(::Label::Head_11);
        text->SetToolTip(tooltip);
        return Add(text, FromDIP(5), wxTOP | wxLEFT | wxEXPAND);
    }

    ScrolledPanel* Finalize()
    {
        GetSizer()->SetSizeHints(this);
        return this;
    }

    bool ShouldScrollToChildOnFocus(wxWindow* /* child */) override { return false; }
};


// ----------------------------------------------------------------------------
// PreferencesDialog
// ----------------------------------------------------------------------------

// static
int PreferencesDialog::m_last_selected_page = 0;


// Static utility functions

static wxStaticText* new_static_text(wxWindow *parent, const wxString &text, const wxString &tooltip = wxEmptyString, int width = DESIGN_LABEL_W, int style = 0)
{
    if (!width)
        width = DESIGN_LABEL_W;
    auto ctrl = new wxStaticText(parent, wxID_ANY, text, wxDefaultPosition, wxSize(parent->FromDIP(width), -1), style | wxBORDER_NONE);
    ctrl->SetForegroundColour(DESIGN_GRAY900_COLOR);
    ctrl->SetFont(::Label::Body_13);
    ctrl->SetToolTip(tooltip);
    // ctrl->Wrap(-1);
    return ctrl;
}

static TextInput* new_text_input(wxWindow *parent, const wxString &text, const wxString &tooltip, int validator = wxFILTER_DIGITS) {
    auto ctrl = new TextInput(
        parent, text, wxEmptyString, wxEmptyString, wxDefaultPosition,
        parent->FromDIP(wxSize(DESIGN_INPUT_W, -1)), wxTE_PROCESS_ENTER
    );
    ctrl->SetToolTip(tooltip);
    if (validator > -1)
        ctrl->GetTextCtrl()->SetValidator(wxTextValidator(validator));
    return ctrl;
}

static ComboBox* new_combobox(wxWindow *parent, const wxString &tooltip = wxEmptyString, std::vector<ComboBox*> *list = nullptr, int width = DESIGN_COMBO_LRG_W)
{
    if (!width)
        width = DESIGN_COMBO_LRG_W;
    auto combobox = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(parent->FromDIP(width), -1), 0, nullptr, wxCB_READONLY);
    combobox->SetFont(::Label::Body_13);
    combobox->GetDropDown().SetFont(::Label::Body_13);
    combobox->SetToolTip(tooltip);
    if (list)
        list->push_back(combobox);
    return combobox;
}

static wxBoxSizer* new_labeled_combobox(
    wxWindow *parent, ComboBox *&combobox, const wxString &label, const wxString &tooltip = wxEmptyString,
    std::vector<ComboBox *> *list = nullptr, int label_width = DESIGN_LABEL_W, int cb_width = DESIGN_COMBO_LRG_W
)
{
    wxBoxSizer *sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(0, 0, 0, wxEXPAND | wxLEFT, parent->DESIGN_ITEM_INDENT);

    auto combo_title = new_static_text(parent, label, tooltip, label_width);
    sizer->Add(combo_title, 0, wxALIGN_CENTER | wxALL, parent->FromDIP(3));

    combobox = new_combobox(parent, tooltip, list, cb_width);
    if (label_width < 0)
        sizer->AddSpacer(parent->DESIGN_INPUT_PAD);
    sizer->Add(combobox, 0, wxALIGN_CENTER);
    return sizer;
}

static Button* new_button(wxWindow *parent, const wxString &title, const wxString &tooltip, std::vector<Button*> *list = nullptr, const wxSize &size = DESIGN_BUTTON_SIZE)
{
    static const StateColor bg_color(
        std::make_pair(wxColour(255, 255, 255), (int)StateColor::Disabled),
        std::make_pair(wxColour(206, 206, 206), (int)StateColor::Pressed),
        std::make_pair(wxColour(238, 238, 238), (int)StateColor::Hovered),
        std::make_pair(wxColour(255, 255, 255), (int)StateColor::Enabled),
        std::make_pair(wxColour(255, 255, 255), (int)StateColor::Normal)
    );
    static const StateColor bd_color(
        std::make_pair(wxColour(144, 144, 144), (int)StateColor::Disabled),
        std::make_pair(wxColour(38, 46, 48),    (int)StateColor::Enabled)
    );
    static const StateColor text_color(
        std::make_pair(wxColour(144, 144, 144), (int)StateColor::Disabled),
        std::make_pair(wxColour(38, 46, 48),    (int)StateColor::Enabled)
    );

    auto ctrl = new Button(parent, title);
    ctrl->SetBackgroundColor(bg_color);
    ctrl->SetBorderColor(bd_color);
    ctrl->SetTextColor(text_color);
    ctrl->SetFont(Label::Body_10);
    ctrl->SetCornerRadius(parent->FromDIP(DESIGN_BUTTON_RADIUS));
    ctrl->SetToolTip(tooltip);
    if (size != wxDefaultSize)
        ctrl->SetMinSize(parent->FromDIP(size));

    if (list)
        list->push_back(ctrl);
    return ctrl;
}

// Editor widget builders

wxBoxSizer *PreferencesDialog::create_item_combobox(const wxString &title, wxWindow *parent, const wxString &tooltip, const std::string &param,
    const std::vector<wxString> &label_list, const std::vector<std::string> &value_list, std::function<void(int)> callback, int title_width, int combox_width)
{
    ComboBox *combobox = nullptr;
    wxBoxSizer *sizer_combox = new_labeled_combobox(parent, combobox, title, tooltip, &m_combobox_list, title_width, combox_width);

    auto get_value_idx = [value_list](const std::string &value) {
        size_t idx = 0;
        auto iter = std::find(value_list.begin(), value_list.end(), value);
        if (iter != value_list.end())
            idx = std::distance(value_list.begin(), iter);
        return idx;
        };

    for (const wxString &label : label_list)
        combobox->Append(label);

    auto old_value = app_config->get(param);
    if (!old_value.empty()) {
        combobox->SetSelection(get_value_idx(old_value));
    }
    else {
        combobox->SetSelection(0);
    }

    //// save config
    combobox->GetDropDown().Bind(wxEVT_COMBOBOX, [=](wxCommandEvent &e) {
        app_config->set(param, value_list[e.GetSelection()]);
        app_config->save();
        if (callback) {
            callback(e.GetSelection());
        }
        e.Skip();
    });
    return sizer_combox;
}

wxBoxSizer *PreferencesDialog::create_item_language_combobox(
    const wxString &title, wxWindow *parent, const wxString &tooltip, const std::string &param, const std::vector<const wxLanguageInfo *> &vlist)
{

    ComboBox *combobox = nullptr;
    wxBoxSizer *sizer_combox = new_labeled_combobox(parent, combobox, title, tooltip, &m_combobox_list);

    auto language = app_config->get(param);
    m_current_language_selected = -1;
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

    return sizer_combox;
}

wxBoxSizer *PreferencesDialog::create_item_region_combobox(const wxString &title, wxWindow *parent, const wxString &tooltip, const std::vector<wxString> &vlist)
{
    static const std::vector<wxString> local_regions = {"Asia-Pacific", "China", "Europe", "North America", "Others"};

    ComboBox *combobox = nullptr;
    wxBoxSizer *sizer_combox = new_labeled_combobox(parent, combobox, title, tooltip, &m_combobox_list);

    for (const wxString &v : vlist)
        combobox->Append(v);

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

    combobox->GetDropDown().Bind(wxEVT_COMBOBOX, [=](wxCommandEvent &e) {
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

    return sizer_combox;
}

wxBoxSizer *PreferencesDialog::create_item_input(
    const wxString &title, const wxString &title2, wxWindow *parent, const wxString &tooltip,
    const std::string &param, std::function<void(const wxString&)> onchange, int label_width /* = 0 */
)
{
    auto input = new_text_input(parent, app_config->get(param), tooltip);

    wxBoxSizer *sizer_input = new wxBoxSizer(wxHORIZONTAL);
    sizer_input->Add(0, 0, 0, wxEXPAND | wxLEFT, DESIGN_ITEM_INDENT);
    sizer_input->Add(new_static_text(parent, title, tooltip, label_width), 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(3));
    if (label_width < 0)
        sizer_input->AddSpacer(DESIGN_INPUT_PAD);
    sizer_input->Add(input, 0, wxALIGN_CENTER_VERTICAL);
    if (!title2.IsEmpty())
        sizer_input->Add(new_static_text(parent, title2, tooltip, -1), 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(3));

    const auto set_value_to_app = [=](wxEvent &e) {
        e.Skip();
        const auto value = input->GetTextCtrl()->GetValue();
        if (value == app_config->get(param))
            return;
        app_config->set(param, value.ToStdString());
        app_config->save();
        if (onchange)
            onchange(value);
    };
    input->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, set_value_to_app);
    input->GetTextCtrl()->Bind(wxEVT_KILL_FOCUS, set_value_to_app);

    return sizer_input;
}

TextInput *PreferencesDialog::create_range_input(
    wxWindow *parent, const wxString &tooltip, const std::string &param, float range_min, float range_max, int precision, std::function<void(const wxString&)> onchange
)
{
    const auto format_str = [=](const float val){
        std::stringstream ss;
        ss << std::fixed << std::setprecision(precision) << val;
        return ss.str();
    };

    // validate current value
    const float float_value = std::atof(app_config->get(param).c_str());
    const float check_value = std::clamp(float_value, range_min, range_max);
    if (float_value != check_value) {
        app_config->set(param, format_str(check_value));
        app_config->save();
    }

    auto input = new_text_input(parent, app_config->get(param), tooltip, wxFILTER_NUMERIC);

    const auto set_value_to_app = [=](wxEvent &e) {
        e.Skip();
        const auto value = format_str(std::clamp<float>(std::atof(input->GetTextCtrl()->GetValue().c_str()), range_min, range_max));
        if (value == app_config->get(param))
            return;
        app_config->set(param, value);
        app_config->save();
        if (onchange) {
            onchange(value);
        }
        input->GetTextCtrl()->SetValue(value);
    };
    input->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, set_value_to_app);
    input->GetTextCtrl()->Bind(wxEVT_KILL_FOCUS, set_value_to_app);

    return input;
}

wxBoxSizer *PreferencesDialog::create_item_range_input(
    const wxString &title, wxWindow *parent, const wxString &tooltip, const std::string &param,
    float range_min, float range_max, int precision, std::function<void(const wxString&)> onchange, int label_width /* = 0 */
)
{
    auto input = create_range_input(parent, tooltip, param, range_min, range_max, precision, onchange);

    wxBoxSizer *sizer_input = new wxBoxSizer(wxHORIZONTAL);
    sizer_input->Add(0, 0, 0, wxEXPAND | wxLEFT, DESIGN_ITEM_INDENT);
    sizer_input->Add(new_static_text(parent, title, tooltip, label_width), 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(3));
    if (label_width < 0)
        sizer_input->AddSpacer(DESIGN_INPUT_PAD);
    sizer_input->Add(input, 0, wxALIGN_CENTER_VERTICAL);
    return sizer_input;
}

wxBoxSizer *PreferencesDialog::create_item_range_two_input(
    const wxString &    title,
    wxWindow *          parent,
    const wxString &    tooltip,
    const std::string & param,
    const std::string & param1,
    float               range_min,
    float               range_max,
    int                 precision,
    std::function<void(const wxString&)> onchange,
    std::function<void(const wxString&)> onchange1,
    int label_width /* = 0 */
)
{
    auto input =  create_range_input(parent, tooltip, param,  range_min, range_max, precision, onchange);
    auto input1 = create_range_input(parent, tooltip, param1, range_min, range_max, precision, onchange1);

    wxBoxSizer *sizer_input = new wxBoxSizer(wxHORIZONTAL);
    sizer_input->Add(0, 0, 0, wxEXPAND | wxLEFT, DESIGN_ITEM_INDENT);
    sizer_input->Add(new_static_text(parent, title, tooltip, label_width), 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(3));
    if (label_width < 0)
        sizer_input->AddSpacer(DESIGN_INPUT_PAD);
    sizer_input->Add(input, 0, wxALIGN_CENTER_VERTICAL, 0);
    sizer_input->AddSpacer(DESIGN_INPUT_PAD);
    sizer_input->Add(input1, 0, wxALIGN_CENTER_VERTICAL, 0);
    return sizer_input;
}

wxBoxSizer *PreferencesDialog::create_item_backup_input(const wxString &title, wxWindow *parent, const wxString &tooltip)
{
    static const std::string param("backup_interval");

    auto input = new_text_input(parent, app_config->get(param), tooltip);

    const auto save_value = [=](wxEvent &e) {
        e.Skip();
        const wxString value = input->GetTextCtrl()->GetValue();
        if (value == app_config->get(param))
            return;
        app_config->set(param, value.ToStdString());
        app_config->save();
        long interval = 0;
        value.ToLong(&interval);
        Slic3r::set_backup_interval(interval);
        m_backup_interval_time = value;
    };
    input->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, save_value);
    input->GetTextCtrl()->Bind(wxEVT_KILL_FOCUS, save_value);

    input->Enable(app_config->get_bool("backup_switch"));
    input->Refresh();

    m_backup_interval_textinput = input;

    wxBoxSizer *sizer_input = new wxBoxSizer(wxHORIZONTAL);
    sizer_input->Add(0, 0, 0, wxEXPAND | wxLEFT, FromDIP(3));
    sizer_input->Add(new_static_text(parent, title, tooltip, -1), 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(3));
    sizer_input->AddSpacer(DESIGN_INPUT_PAD);
    sizer_input->Add(input, 0, wxALIGN_CENTER_VERTICAL, 0);
    sizer_input->Add(0, 0, 0, wxEXPAND | wxLEFT, FromDIP(3));
    sizer_input->Add(new_static_text(parent, _L("Second"), tooltip, -1), 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(3));
    return sizer_input;
}

wxBoxSizer* PreferencesDialog::create_item_darkmode_checkbox(const wxString &title, wxWindow* parent, const wxString &tooltip, const std::string &param)
{
    //// save config
    auto callback = [this, param](int value) {
        app_config->set(param, value ? "1" : "0");
        app_config->save();
        wxGetApp().Update_dark_mode_flag();
        wxGetApp().force_colors_update();
        wxGetApp().update_ui_from_settings();
        wxGetApp().UpdateDlgDarkUI(this);

        SimpleEvent evt = SimpleEvent(EVT_GLCANVAS_COLOR_MODE_CHANGED);
        wxPostEvent(wxGetApp().plater(), evt);
    };

    return create_item_checkbox(title, parent, tooltip, param, callback);
}

// TODO: Move special handling of individual properties in the change handler into their own callbacks.
wxBoxSizer *PreferencesDialog::create_item_checkbox(
    const wxString &title, wxWindow *parent, const wxString &tooltip,
    const std::string &param, std::function<void(int)> callback /* = nullptr */
)
{
    wxBoxSizer *m_sizer_checkbox  = new wxBoxSizer(wxHORIZONTAL);

    m_sizer_checkbox->Add(0, 0, 0, wxEXPAND | wxLEFT, DESIGN_ITEM_INDENT);

    auto checkbox = new ::CheckBox(parent);
    checkbox->SetToolTip(tooltip);
    m_checkbox_list.push_back(checkbox);

    if (!param.empty()) {
        bool current_value = app_config->get_bool(param);
        // special exception for reversed item label vs. actual config value
        if (param == "no_warn_when_modified_gcodes")
            current_value = !current_value;
        checkbox->SetValue(current_value);
    }

    m_sizer_checkbox->Add(checkbox, 0, wxALIGN_CENTER, 0);
    m_sizer_checkbox->Add(0, 0, 0, wxEXPAND | wxLEFT, 8);

    auto checkbox_title = new_static_text(parent, title, tooltip, -1);
    const auto size = checkbox_title->GetTextExtent(title);
    checkbox_title->SetMinSize(wxSize(size.x + FromDIP(5), -1));
    m_sizer_checkbox->Add(checkbox_title, 0, wxALIGN_CENTER | wxALL, 3);

     //// save config
    checkbox->Bind(wxEVT_TOGGLEBUTTON, [=](wxCommandEvent &e)
    {
        bool value = checkbox->GetValue();

        if (!param.empty()) {
            // special exception for reversed item label vs. actual config value
            if (param == "no_warn_when_modified_gcodes")
                value = !value;
            app_config->set_bool(param, value);
            app_config->save();
        }

        if (callback) {
            callback(e.GetInt());
        }

        else if (param == "firstguide") {
            if (NetworkAgent* agent = GUI::wxGetApp().getAgent(); agent && !value) {
                agent->track_enable(false);
                agent->track_remove_files();
            }
            wxGetApp().save_privacy_policy_history(value, "preferences");
        }

        else if (param == "staff_pick_switch") {
            wxGetApp().switch_staff_pick(value);
        }

         // backup
        else if (param == "backup_switch") {
            std::string backup_interval = "10";
            app_config->get("backup_interval", backup_interval);
            Slic3r::set_backup_interval(value ? boost::lexical_cast<long>(backup_interval) : 0);
            if (m_backup_interval_textinput != nullptr) { m_backup_interval_textinput->Enable(value); }
        }

        else if (param == "sync_user_preset") {
            if (value) {
                wxGetApp().start_sync_user_preset();
            } else {
                wxGetApp().stop_sync_user_preset();
            }
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " sync_user_preset: " << (value ? "true" : "false");
        }

#ifdef __WXMSW__
        else if (param == "associate_3mf") {
             if (value) {
                 wxGetApp().associate_files(L"3mf");
             } else {
                 wxGetApp().disassociate_files(L"3mf");
             }
        }

        else if (param == "associate_stl") {
            if (value) {
                wxGetApp().associate_files(L"stl");
            } else {
                wxGetApp().disassociate_files(L"stl");
            }
        }

        else if (param == "associate_step") {
            if (value) {
                wxGetApp().associate_files(L"step");
            } else {
                wxGetApp().disassociate_files(L"step");
            }
        }
#endif // __WXMSW__

        else if (param == "developer_mode")
        {
            Slic3r::GUI::wxGetApp().save_mode(value ? comDevelop : comAdvanced);
            m_developer_mode_def = value;
        }

        // webview  dump_vedio
        else if (param == "internal_developer_mode") {
            Slic3r::GUI::wxGetApp().update_internal_development();
            if (value)
                Slic3r::GUI::wxGetApp().mainframe->show_log_window();
            m_internal_developer_mode_def = value;
        }

        else if (param == "show_print_history") {
            if (wxGetApp().mainframe && wxGetApp().mainframe->m_webview)
                wxGetApp().mainframe->m_webview->ShowUserPrintTask(value, value);
        }

        else if (param == "enable_lod") {
            MessageDialog msg_wingow(this,
                _L("Please note that the model show will undergo certain changes at small pixels case.\nEnabled LOD requires application restart.") + "\n" + _L("Do you want to continue?"),
                _L("Enable LOD"), wxICON_WARNING | wxYES_NO | wxYES_DEFAULT | wxCENTRE);
            if (msg_wingow.ShowModal() == wxID_YES) {
                if (wxGetApp().plater()->is_project_dirty()) {
                    MessageDialog save_dlg(this, _L("The current project has unsaved changes, save it before continuing?"),
                        wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Save"), wxICON_WARNING | wxYES_NO  | wxYES_DEFAULT | wxCENTRE);
                    if (save_dlg.ShowModal() == wxID_YES)
                        wxGetApp().plater()->save_project();
                }
                Close();
                GetParent()->RemoveChild(this);
                wxGetApp().recreate_GUI(_L("Enable LOD"));
            }
            else {
                checkbox->SetValue(!value);
                app_config->set_bool(param, !value);
                app_config->save();
            }
        }

        else if (param == "enable_record_gcodeviewer_option_item"){
            SimpleEvent evt(EVT_ENABLE_GCODE_OPTION_ITEM_CHANGED);
            wxPostEvent(wxGetApp().plater(), evt);
        }

        else if (param == "enable_high_low_temp_mixed_printing") {
            if (value) {
                const wxString warning_title = _L("Bed Temperature Difference Warning");
                const wxString warning_message =
                    _L("Using filaments with significantly different temperatures may cause:\n"
                        "• Extruder clogging\n"
                        "• Nozzle damage\n"
                        "• Layer adhesion issues\n\n"
                        "Continue with enabling this feature?") + "\n";
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
                    this,
                    warning_message,
                    warning_title,
                    wxICON_WARNING | wxYES_NO | wxYES_DEFAULT | wxCENTRE,
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
    else if (param == "internal_developer_mode") { m_internal_developer_mode_ckeckbox = checkbox; }

    return m_sizer_checkbox;
}

wxWindow* PreferencesDialog::create_item_downloads(wxWindow* parent, const std::string &param)
{
    auto item_panel = new wxWindow(parent, wxID_ANY);
    item_panel->SetBackgroundColour(DESIGN_CONTENT_BG_COLOR);

    auto label = new_static_text(item_panel, _L("Downloads Folder:"), wxEmptyString, -1);
    auto m_staticTextPath = new_static_text(item_panel, wxString::FromUTF8(app_config->get(param)), wxEmptyString, -1, wxST_ELLIPSIZE_END);
    auto m_button_download = new_button(item_panel, _L("Browse"), _L("Select default folder for downloads."), &m_button_list);

    m_button_download->Bind(wxEVT_BUTTON, [=](auto& e) {
        const wxString defaultPath = wxString::FromUTF8(app_config->get(param));
        wxDirDialog dialog(const_cast<PreferencesDialog*>(this), _L("Choose Download Directory"), defaultPath, wxDD_NEW_DIR_BUTTON);

        if (dialog.ShowModal() == wxID_OK) {
            const wxString download_path = dialog.GetPath();
            app_config->set(param, download_path.ToUTF8().data());
            m_staticTextPath->SetLabelText(download_path);
            item_panel->Layout();
        }
    });

    wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(0, 0, 0, wxEXPAND | wxLEFT, DESIGN_ITEM_INDENT);
    sizer->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));
    sizer->AddSpacer(DESIGN_INPUT_PAD);
    sizer->Add(m_staticTextPath, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(m_button_download, 0, wxALL, FromDIP(5));

    item_panel->SetSizer(sizer);
    item_panel->Layout();

    return item_panel;
}

wxWindow *PreferencesDialog::create_item_radiobox(
    const wxString &title,
    wxWindow *parent,
    const wxString &tooltip,
    int groupid,
    const std::string &param,
    bool select /* = false */
)
{
    wxWindow *item = new wxWindow(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(28)));
    item->SetBackgroundColour(DESIGN_CONTENT_BG_COLOR);

    RadioBox *radiobox = new RadioBox(item);
    radiobox->SetValue(select);
    radiobox->SetToolTip(tooltip);
    radiobox->SetPosition(wxPoint(DESIGN_ITEM_INDENT, (item->GetSize().GetHeight() - radiobox->GetSize().GetHeight()) / 2));

    RadioSelector *rs = new RadioSelector;
    rs->m_groupid     = groupid;
    rs->m_param_name  = param;
    rs->m_radiobox    = radiobox;
    rs->m_selected    = select;
    m_radio_group.Append(rs);

    radiobox->Bind(wxEVT_LEFT_DOWN, [=](wxMouseEvent &event) {
        // Ensure only one button is selected in exclusive groups.
        int groupid = -1;
        const int event_id = event.GetId();

        for (auto rs : std::as_const(m_radio_group)) {
            if (RadioBox *rb = rs->m_radiobox; rb && rb->GetId() == event_id) {
                groupid = rs->m_groupid;
                break;
            }
        }
        if (groupid < 0)
            return;

        for (auto rs : std::as_const(m_radio_group))
            if (RadioBox *rb = rs->m_radiobox; rb && rs->m_groupid == groupid)
                rb->SetValue(rb->GetId() == event_id);
    });

    wxStaticText *text = new_static_text(item, title, tooltip, -1);
    text->SetPosition(wxPoint(DESIGN_ITEM_INDENT + radiobox->GetSize().GetWidth() + 10, (item->GetSize().GetHeight() - text->GetSize().GetHeight()) / 2));

    return item;
}


//
// Begin implementation
//

PreferencesDialog::PreferencesDialog(wxWindow *parent, wxWindowID id, const wxString &title, const wxPoint &pos, const wxSize &size, long style)
    : DPIDialog(parent, id, _L("Preferences"), pos, size, style)
{
    create();
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
        event.Skip();
    });
}

PreferencesDialog::~PreferencesDialog()
{
    m_radio_group.DeleteContents(true);
}

void PreferencesDialog::create()
{
    app_config             = get_app_config();
    m_backup_interval_time = app_config->get("backup_interval");
    m_original_use_12h_time_format = app_config->get_bool("use_12h_time_format");

    // set icon for dialog
    const std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));
    SetSizeHints(wxDefaultSize, wxDefaultSize);  // ensure dialog is resizable
    SetBackgroundColour(DESIGN_CONTENT_BG_COLOR);

    // The page display and navigation handler.
    m_page_book = new Tabbook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, nullptr, wxBK_LEFT);

    // Customize the navigation button list sidebar control.
    auto *ctrl = m_page_book->GetBtnsListCtrl();
    ctrl->SetBackgroundColour(DESIGN_SIDEBAR_BG_COLOR);
    // make scrollable vertically
    ctrl->SetScrollRate(0, FromDIP(10));
    // option to have vertical scrollbar use allotted sidebar space instead of expanding the sidebar area horizontally
    // ctrl->SetUseClientAreaForScrollbar(true);
    // Add top padding above buttons.
    ctrl->SetListTopPadding(DESIGN_PAGE_PADDING);
    // Reduce button padding around text label.
    ctrl->SetPaddingSize({ FromDIP(20), FromDIP(16) });
    // Use natural button size by default (we'll adjust width after adding pages).
    ctrl->SetButtonSize(wxDefaultSize);
    // No button borders
    ctrl->SetButtonBorderWidth(0);
    // Make selected or hovered button background match the content page background color, otherwise same as sidebar
    ctrl->SetButtonBGColors(StateColor(
        std::make_pair(DESIGN_CONTENT_BG_COLOR, (int)StateColor::Checked),
        std::make_pair(DESIGN_CONTENT_BG_COLOR, (int)StateColor::Hovered),
        std::make_pair(DESIGN_SIDEBAR_BG_COLOR, (int)StateColor::Normal)
    ));

    // Add all our pages.
    m_page_book->Add(create_general_page(),  _L("General"),        _L("Settings affecting basic application behaviors."));
    m_page_book->Add(create_online_page(),   _L("Online"),         _L("Configure features requiring an Internet connection."));
    m_page_book->Add(create_projects_page(), _L("Projects"),       _L("Settings related to working with projects."));
    m_page_book->Add(create_files_page(),    _L("File Handling"),  _L("Settings related to opening and importing files of various types, including 3MF projects."));
    m_page_book->Add(create_3Dview_page(),   _L("3D View"),        _L("The \"Prepare\" and \"Preview\" 3D workspace and graphics settings."));
    m_page_book->Add(create_advanced_page(), _L("Advanced"),       _L("Other settings for more advanced control over program behavior."));

    // Set nav button width to increase padding on the right side, with constraint in case of some erroneous translation.
    ctrl->SetButtonSize({ std::min(int(ctrl->GetBestSize().x * 1.33), FromDIP(240)), -1 });

    // Final layout
    auto main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->Add(m_page_book, 1, wxEXPAND /* | wxTOP, FromDIP(10) */);
    SetSizer(main_sizer);

    // Resize and position the dialog.
    set_window_size();
    CenterOnParent();
    // update for dark mode
    wxGetApp().UpdateDlgDarkUI(this);

    // Ensure static selection ID is valid after all pages have been added.
    if (m_last_selected_page < 0 || m_last_selected_page >= (int)m_page_book->GetPageCount())
        m_last_selected_page = 0;

    // track page changes
    m_page_book->Bind(wxEVT_BOOKCTRL_PAGE_CHANGED, &PreferencesDialog::on_menu_item_selected, this);

    // set current page
    m_page_book->SetSelection((size_t)m_last_selected_page);
}

void PreferencesDialog::on_menu_item_selected(wxBookCtrlEvent &ev)
{
    m_last_selected_page = m_page_book->GetSelection();
    // Move focus from sidebar nav control to the current page.
    if (wxWindow *pg = m_page_book->GetCurrentPage())
        pg->SetFocus();
    ev.Skip();
}

void PreferencesDialog::set_window_size()
{
    Layout();
    Fit();
    // Make sure dialog window size doesn't overflow the display it is, or will be, shown on.
    int disp_idx = wxDisplay::GetFromWindow(this);
    if (disp_idx == wxNOT_FOUND && GetParent())
        disp_idx = wxDisplay::GetFromWindow(GetParent());
    // Use default (primary) display if we couldn't find the specific one (unlikely).
    const wxDisplay disp = disp_idx == wxNOT_FOUND ? wxDisplay() : wxDisplay(disp_idx);
    const wxSize disp_sz = disp.GetClientArea().GetSize();
    const wxSize this_sz = GetBestSize();
    // Do not stretch the dialog if it will already fit at its natural size.
    SetSize(std::min(this_sz.x + FromDIP(40), disp_sz.x - FromDIP(20)), std::min(this_sz.y, disp_sz.y - FromDIP(40)));
}

void PreferencesDialog::on_dpi_changed(const wxRect &/* suggested_rect */)
{
    for (auto item : std::as_const(m_button_list)) {
        item->Rescale();
        item->SetMinSize(FromDIP(DESIGN_BUTTON_SIZE));
        item->SetCornerRadius(FromDIP(DESIGN_BUTTON_RADIUS));
    }
    for (auto item : std::as_const(m_checkbox_list)) {
        item->Rescale();
    }
    for (auto rs : std::as_const(m_radio_group)) {
        if (RadioBox *rb = rs->m_radiobox)
            rb->Rescale();
    }
    for (auto item : std::as_const(m_combobox_list)) {
        item->Rescale();
    }
    m_page_book->Rescale();
    this->Refresh();
    set_window_size();
}

bool PreferencesDialog::use_12h_time_format_changed() const {
    return app_config->get_bool("use_12h_time_format") != m_original_use_12h_time_format;
}

ScrolledPanel* PreferencesDialog::create_book_page(wxWindow *parent /* = nullptr */) const
{
    if (!parent)
        parent = m_page_book;
    return new ScrolledPanel(parent, parent == m_page_book);
}

wxWindow* PreferencesDialog::create_general_page(wxWindow *parent)
{
    auto page = create_book_page(parent);

    page->AddTitle(_L("General Settings"), _L("Settings affecting basic application behaviors."));

    // Language selector
    const auto translations = wxTranslations::Get()->GetAvailableTranslations(SLIC3R_APP_KEY);
    std::vector<const wxLanguageInfo *> language_infos { wxLocale::GetLanguageInfo(wxLANGUAGE_ENGLISH) };
    for (size_t i = 0, e = translations.GetCount(); i < e; ++i) {
        if (const wxLanguageInfo *available_lan = wxLocale::FindLanguageInfo(translations[i])) {
            for (size_t si = 0, se = s_supported_languages.size(); si < se; ++si) {
                if (auto* supported_lan = wxLocale::GetLanguageInfo(s_supported_languages[si])){
                    if (available_lan->CanonicalName == supported_lan->CanonicalName) {
                        language_infos.emplace_back(supported_lan);
                        break;
                    }
                }
            }
        }
    }
    sort_remove_duplicates(language_infos);
    std::sort(language_infos.begin(), language_infos.end(), [](const wxLanguageInfo *l, const wxLanguageInfo *r) { return l->Description < r->Description; });
    page->Add( create_item_language_combobox(_L("Language"), page, _L("Language"), "language", language_infos) );

    static const std::vector<wxString> Units = {_L("Metric") + " (mm, g)", _L("Imperial") + " (in, oz)"};
    page->Add( create_item_combobox(_L("Units"), page, _L("Units"), "use_inches", Units,{"0","1"}) );

    page->Add( create_item_checkbox(_L("Use 12-hour time format"), page, _L("Display time in 12-hour format with AM/PM instead of 24-hour format"), "use_12h_time_format") );

    page->Add(
        create_item_checkbox(_L("Keep only one Bambu Studio instance"), page,
#if __APPLE__
            _L("On OSX there is always only one instance of app running by default. However it is allowed to run multiple instances "
               "of same app from the command line. In such case this settings will allow only one instance."),
#else
            _L("If this is enabled, when starting Bambu Studio and another instance of the same Bambu Studio is already running, that instance will be reactivated instead."),
#endif
            "single_instance"
        ));

#ifdef _WIN32
    page->Add( create_item_darkmode_checkbox(_L("Enable dark mode"), page, _L("Enable dark mode"), "dark_color_mode") );
#endif // _WIN32

    //page->Add( create_item_checkbox(_L("Show \"Tip of the day\" notification after start"), page, _L("If enabled, useful hints are displayed at startup."),  "show_hints") );

    page->Add(
        create_item_checkbox(
            _L("Enable Filament Manager (Takes effect after restarting Studio)"), page,
#if __APPLE__
            _L("The Filament Manager is turned off by default on macOS because compatibility issues on some systems may cause the application to become unresponsive."),
#else
            _L("Enable or disable the Filament Manager feature."),
#endif
            FilaManagerEnabledConfigKey
        ));

    page->Add( create_item_downloads(page, "download_path") );

    // Embed Device properties here
    page->Add( create_device_page(page), DESIGN_SECTION_SPACING );

    return page->Finalize();
}

wxWindow* PreferencesDialog::create_device_page(wxWindow *parent)
{
    auto page = create_book_page(parent);

    page->AddTitle(_L("Device"), _L("Options for Device management."));

    page->Add(
        create_item_checkbox(
            _L("Keep liveview when printing"), page,
            _L("By default, Liveview will pause after 15 minutes of inactivity on the computer. Check this box to disable this feature during printing."),
            "liveview"
        ));
    page->Add(
        create_item_checkbox(
            _L("Open full screen camera view on active monitor only"), page,
            _L("When enabled, the camera full screen view opens only on the monitor that contains Bambu Studio."),
            "camera_fullscreen_active_monitor_only"
        ));
    page->Add(
        create_item_checkbox(
            _L("Multi-device Management (Takes effect after restarting Studio)"), page,
            _L("With this option enabled, you can send a task to multiple devices at the same time and manage multiple devices."),
            "enable_multi_machine"
        ));

    return page->Finalize();
}

wxWindow* PreferencesDialog::create_online_page(wxWindow *parent)
{
    auto page = create_book_page(parent);

    page->AddTitle(_L("Online Features"));

    static const std::vector<wxString> Regions = {_L("Asia-Pacific"), _L("Chinese Mainland"), _L("Europe"), _L("North America"), _L("Others")};
    page->Add( create_item_region_combobox(_L("Login Region"), page, _L("Login Region"), Regions) );
    ComboBox *combo_region = m_combobox_list[m_combobox_list.size()-1];

    page->Add(
        create_item_checkbox(
            _L("Auto sync user presets(Printer/Filament/Process)"), page,
            _L("If enabled, auto sync user presets with cloud after Bambu Studio startup or presets modified."),
            "sync_user_preset"
        ));
    page->Add(
        create_item_checkbox(
            _L("Auto check for system presets updates"), page,
            _L("If enabled, auto check whether there are system presets updates after Bambu Studio startup."),
            "sync_system_preset"
        ));
    page->Add(
        create_item_checkbox(
            _L("Support beta version update"), page,
            _L("With this option enabled, you can receive beta version updates."),
            "enable_beta_version_update"
        ));

#ifdef _WIN32
    page->Add( create_item_checkbox(_L("Auto-fill previously logged-in accounts"), page, _L(""), "webview_auto_fill") );
#endif // _WIN32

    auto item_join_program = create_item_checkbox(_L("Join the User Experience Improvement Program."), page, "",  "firstguide");
    auto* hyperlink = new Label(page, wxString::FromUTF8(_CTX_utf8(L_CONTEXT("Learn more", "Preferences"), "Preferences")));
    hyperlink->SetFont(Label::Head_13);
    hyperlink->SetForegroundColour(wxColour("#0078D4"));
    hyperlink->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
    hyperlink->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });
    hyperlink->Bind(wxEVT_LEFT_DOWN, [this](auto& e) {
        UxProgramTermsDialog dlg(this);
        dlg.ShowModal();
    });
    item_join_program->Add(hyperlink, 0, wxALIGN_CENTER, 0);
    page->Add(item_join_program);


    auto item_hp_title = page->AddTitle(_L("Home Page"));

    auto item_modelmall = page->Add(
        create_item_checkbox(
            _L("Show online staff-picked models"), page,
            _L("Show a selection of staff-picked models from MakerWorld on the home page."),
            "staff_pick_switch"
        ));
    auto item_show_history = page->Add(
        create_item_checkbox(
            _L("Show MakerWorld history"), page,
            _L("Show recently printed files from your MakerWorld history on the home page."),
            "show_print_history"
        ));

    // Hide online model features in unsupported regions
    // NOTE: Do not hide `item_hp_title` if the "Home Page" section gets other options later.
    auto update_modelmall = [=](wxEvent &) {
        bool has_model_mall = wxGetApp().has_model_mall();
        item_modelmall->Show(has_model_mall);
        item_show_history->Show(has_model_mall);
        item_hp_title->Show(has_model_mall);
        Layout();
        Fit();
    };
    wxCommandEvent eee;
    update_modelmall(eee);
    combo_region->Bind(wxEVT_COMBOBOX, update_modelmall);

    return page->Finalize();
}

wxWindow* PreferencesDialog::create_projects_page(wxWindow *parent)
{
    auto page = create_book_page(parent);

    page->AddTitle(_L("Projects"), _L("Settings affecting program behavior when working with projects."));

    page->Add(
        create_item_input(
            _L("Maximum recent projects"), "", page,
            _L("Maximum count of recent projects"),
            "max_recent_count",
            [](const wxString &value) {
                long max = 0;
                if (value.ToLong(&max))
                    wxGetApp().mainframe->set_max_recent_count(max);
            },
            -1
        ));

    static const std::vector<wxString> FlushOptionLabels = {_L("All"),_L("Color change"),_L("Disabled")};
    static const std::vector<std::string> FlushOptionValues = { "all","color change","disabled" };
    page->Add(
        create_item_combobox(
            _L("Auto Flush"), page,
            _L("Auto calculate flush volumes"),
            "auto_calculate_flush", FlushOptionLabels, FlushOptionValues
        ));

    page->Add(
        create_item_checkbox(
            _L("Remember plate type"), page,
            _L("Studio will remember build plate selected last time for certain printer model."),
            "user_bed_type"
        ));
    page->Add(
        create_item_checkbox(
            _L("Automatically transfer modified value when switching process and filament presets"), page,
            _L("After closing, a popup will appear to ask each time"),
            "auto_transfer_when_switch_preset"
    ));
    page->Add(
        create_item_checkbox(
            _L("Remove the restriction on mixed printing of high and low temperature filaments"), page,
            _L("With this option enabled, you can print materials with a large temperature difference together."),
            "enable_high_low_temp_mixed_printing"
        ));

    page->Add(
        create_item_checkbox(
            _L("Show recommendations dialog when a dedicated support filament is detected"), page,
            _L("With this option enabled, the program will prompt to change support generation options when a dedicated support filament is first detected."),
            "show_support_recommend_dialog"
        ));

    auto item_backup = create_item_checkbox(
        _L("Auto-Backup"), page,
        _L("Backup your project periodically for restoring from the occasional crash."),
        "backup_switch"
    );
    item_backup->Add(create_item_backup_input(_L("every"), page, _L("The peroid of backup in seconds.")));
    page->Add(item_backup);

    static const std::vector<wxString> unsaved_labels = {_L("Prompt to Save"), _L("Always Save"), _L("Never Save")};
    static const std::vector<std::string> unsaved_values = { "", "yes", "no" };
    page->Add(
        create_item_combobox(
            _L("When closing an unsaved project:"), page,
            _L("Select what to do when a project with unsaved changes is being closed."),
            "save_project_choise", unsaved_labels, unsaved_values,
            nullptr, -1
        ));

    static const std::vector<wxString> post_proc_labels = {_L("Always Ask"), _L("Always Execute"), _L("Never Execute")};
    static const std::vector<std::string> post_proc_values = { "", "execute", "do_not_execute" };
    page->Add(
        create_item_combobox(
            _L("When slicing a project containing post-processing scripts:"), page,
            _L("\"Always Ask\" will show the security warning dialog before slicing when post-processing scripts are configured."),
            "post_process_script_choice", post_proc_labels, post_proc_values,
            [](int sel) {
                if (sel == 0) {
                    wxGetApp().app_config->erase("app", "post_process_script_choice");
                    if (wxGetApp().plater())
                        wxGetApp().plater()->reset_post_process_script_choice();
                }
            },
            -1
        ));

#if 0
    //temporarily disable it
    page->AddTitle(_L("Filament Grouping"));
    //page->Add(create_item_checkbox(_L("Ignore ext filament when auto grouping"), page, _L("Ignore ext filament when auto grouping"), "ignore_ext_filament_when_group"));
    page->Add( create_item_checkbox(_L("Pop up to select filament grouping mode"), page, _L("Pop up to select filament grouping mode"), "pop_up_filament_map_dialog") );
#endif

    return page->Finalize();
}

wxWindow* PreferencesDialog::create_files_page(wxWindow *parent)
{
    auto page = create_book_page(parent);

    page->AddTitle(_L("3MF Project Files"), _L("Settings affecting program behavior when opening 3MF project files."));

    page->Add(
        // Note that the language of the label here is the reverse of the actual config value, to stay consistent with rest of the labels. The exception is handled in create_item_checkbox().
        create_item_checkbox(
            _L("Warn when loading 3MF with modified G-codes"), page,
            _L("Show a warning dialog when loading 3MF projects with modified machine or filament G-code."),
            "no_warn_when_modified_gcodes"
        ));

    page->Add(
        create_item_checkbox(
            _L("Warn when opening incompatible 3MF project files"), page,
            _L("Show a warning dialog when importing non-Bambu 3MF files."),
            "skip_non_bambu_3mf_warning"
        ));

    static const std::vector<wxString> sync_labels = {_L("Prompt to Sync"), _L("Always Sync"), _L("Never Sync")};
    static const std::vector<std::string> sync_values = { "", "true", "false" };
    page->Add(
        create_item_combobox(
            _L("When opening a project with unsynced properties:"), page,
            _L("Select what to do when a project being loaded doesn't match the current printer or nozzle choices."),
            "sync_after_load_file_show_flag", sync_labels, sync_values,
            [](int sel) {
                if (sel == 0)
                    wxGetApp().app_config->erase("app", "sync_after_load_file_show_flag");
            },
            -1
        ));

    page->AddTitle(_L("Imports"), _L("Settings affecting program behavior when importing models or other data."));

    page->Add(
        create_item_checkbox(
            _L("Show the STEP mesh parameter setting dialog"), page,
            _L("If enabled,a parameter settings dialog will appear during STEP file import."),
            "enable_step_mesh_setting"
        ));
    page->Add(
        create_item_checkbox(
            _L("Import a single SVG and split it"), page,
            _L("Import a single SVG and then split it to several parts."),
            "import_single_svg_and_split"
        ));
    page->Add(
        create_item_checkbox(
            _L("Enable gamma correction for imported OBJ files"), page,
            _L("Perform gamma correction on color after importing the OBJ model."),
            "gamma_correct_in_import_obj"
        ));

#ifdef _WIN32
    page->AddTitle(_L("Associate Files To Bambu Studio"), _L("Select file type(s) to associate with Bambu Studio on this computer for the current user."));

    page->Add(
        create_item_checkbox(_L(
            "Associate .3mf files to Bambu Studio"), page,
            _L("If enabled, sets Bambu Studio as default application to open .3mf files"), "associate_3mf"
        ));
    page->Add(
        create_item_checkbox(
            _L("Associate .stl files to Bambu Studio"), page,
            _L("If enabled, sets Bambu Studio as default application to open .stl files"), "associate_stl"
        ));
    page->Add(
        create_item_checkbox(
            _L("Associate .step/.stp files to Bambu Studio"), page,
            _L("If enabled, sets Bambu Studio as default application to open .step files"), "associate_step"
        ));
#endif // _WIN32

    return page->Finalize();
}

wxWindow* PreferencesDialog::create_3Dview_page(wxWindow *parent)
{
    auto page = create_book_page(parent);

    page->AddTitle(_L("3D Workspace"));

    page->Add(
        create_item_checkbox(
            _L("Always show shells in preview"), page,
            _L("Always show shells or not in preview view tab. If you change this value, you should reslice."),
            "show_shells_in_preview"
        ));
    page->Add(
        create_item_checkbox(
            _L("Remember last used color scheme"), page,
            _L("When enabled, the last used color scheme (e.g., Line Type, Speed) will be automatically applied on next startup."),
            "enable_record_gcodeviewer_option_item"
        ));
    page->Add(
        create_item_checkbox(
        _L("Enable advanced gcode viewer"), page,
        _L("Enable advanced gcode viewer."),
        "enable_advanced_gcode_viewer_"
    ));

    static const std::vector<wxString> toolbar_style = { _L("Collapsible"), _L("Uncollapsible") };
    page->Add(
        create_item_combobox(
            _L("Toolbar Style"), page,
            _L("Collapsible style will always fit the toolbar into the available horizontal space, and if necessary will add a button to show any hidden tools."),
            "toolbar_style", toolbar_style, { "0","1" },
            [](int idx) {
                if (const auto p_ogl_manager = wxGetApp().get_opengl_manager())
                    p_ogl_manager->set_toolbar_rendering_style(idx);
            }
        ));

    float range_min = 1.0,
        range_max = 2.5;
    page->Add(
        create_item_range_input(
            _L("Grabber scale"), page,
            _L("Set grabber size for move, rotate, and scale tools.") + " " + _L("Value range") + ": [" + std::to_string(range_min) + " - " + std::to_string(range_max) + "]",
            "grabber_size_factor",
            range_min, range_max, 1,
            [](const wxString &value) {
                double d_value = 0;
                if (value.ToDouble(&d_value)) {
                    GLGizmoBase::Grabber::GrabberSizeFactor = d_value;
                }
            }
        ));

    range_min = 0.0f;
    range_max = 150.0f;
    page->Add(
        create_item_range_two_input(
            _L("Tooltip offset"), page,
            _L("Set tooltip offset for different mouse size.") + " " + _L("Value range") + ": [" + std::to_string(range_min) + " - " + std::to_string(range_max) + "]",
            "3d_middle_tooltip_offset_x", "3d_middle_tooltip_offset_y", range_min, range_max, 1
        ));


    page->AddTitle(_L("Camera"), _L("These settings adjust how to interact with 3D view camera controls."));

    page->Add(
        create_item_checkbox(
            _L("Zoom to mouse position"), page,
            _L("Zoom in towards the mouse pointer's position in the 3D view, rather than the 2D window center."),
            "zoom_to_mouse"
        ));

    page->AddTitle(_L("Assembly View"));

    static const std::vector<wxString> assemble_overview_labels =    { _L("Auto"), _L("Open"), _L("Close") };
    static const std::vector<std::string> assemble_overview_values = {    "Auto",     "Open",     "Close" };
    page->Add(
        create_item_combobox(
            _L("Display overview"), page,
            _L("Display overview."),  // FIXME
            "enable_assemble_view_preview", assemble_overview_labels, assemble_overview_values,
            [](int idx) {
                if (idx < 2)  // auto or open
                    wxGetApp().app_config->set_bool("enable_bvh", idx == 0);
            },
            -1, FromDIP(DESIGN_COMBO_W)
        ));

#if !BBL_RELEASE_TO_PUBLIC
    page->Add( create_item_checkbox(_L("Show assembly BVH primary bounds"), page, _L("Display the BVH primary bounding box wireframe in assembly view."), "show_assembly_bvh_bounds") );
#endif

    page->AddTitle(_L("Graphics"));

    page->Add(
        create_item_checkbox(
            _L("Improve rendering performance by LOD"), page,
            _L("Improved rendering performance under the scene of multiple plates and many models."),
            "enable_lod"
        ));

    return page->Finalize();
}

wxWindow* PreferencesDialog::create_advanced_page(wxWindow *parent)
{
    auto page = create_book_page(parent);

    page->AddTitle(_L("Other Settings"));

    page->Add(
        create_item_checkbox(
            _L("Develop mode"), page,
            _L("Enable extra and experimental features. Note that this also forces \"Advanced\" mode for all property editors."),
            "developer_mode"
        ));
    page->Add(
        create_item_checkbox(
            _L("Skip AMS blacklist check"), page,
            _L("Skip AMS blacklist check"),
            "skip_ams_blacklist_check"
        ));

#if !BBL_RELEASE_TO_PUBLIC
    page->Add(create_debug_page(page), DESIGN_SECTION_SPACING);
#endif

    return page->Finalize();
}

wxWindow* PreferencesDialog::create_debug_page(wxWindow *parent)
{
    auto page = create_book_page(parent);

    m_internal_developer_mode_def = app_config->get_bool("internal_developer_mode");
    m_backup_interval_def = app_config->get("backup_interval");
    m_iot_environment_def = app_config->get("iot_environment");

    page->AddTitle(_L("Application Developer"));

    page->Add( create_item_checkbox(_L("Enable SSL(MQTT)"), page, _L("Enable SSL(MQTT)"), "enable_ssl_for_mqtt") );
    page->Add( create_item_checkbox(_L("Enable SSL(FTP)"), page, _L("Enable SSL(MQTT)"), "enable_ssl_for_ftp") );
    page->Add( create_item_checkbox(_L("Internal developer mode"), page, _L("Internal developer mode"), "internal_developer_mode") );

    static const std::vector<wxString> ll_names { _L("fatal"), _L("error"), _L("warning"), _L("info"), _L("debug"), _L("trace") };
    static const std::vector<std::string> ll_values{ "fatal",     "error",     "warning",     "info",     "debug",     "trace"  };
    page->Add(
        create_item_combobox(
            _L("Log Level"), page, _L("Log Level"), "severity_level", ll_names, ll_values,
            [](int selection) {
                auto level = Slic3r::get_string_logging_level(selection);
                Slic3r::set_logging_level(Slic3r::level_string_to_boost(level));
            }
        ));

    page->AddTitle(_L("Host Setting"));

    static const int radio_grp_id = 1;
    static const std::map<const wxString, const wxString> iot_env_map {
        { "dev_host",     ENV_DEV_HOST },
        { "qa_host",      ENV_QAT_HOST },
        { "pre_host",     ENV_PRE_HOST },
        { "product_host", ENV_PRODUCT_HOST }
    };

    // TODO: perhaps these can be converted to a combo box selector and all radio button code removed. This is the only place that uses them.
    page->Add( create_item_radiobox(_L("DEV host: api-dev.bambu-lab.com/v1"), page, "", radio_grp_id, "dev_host",     m_iot_environment_def == ENV_DEV_HOST) );
    page->Add( create_item_radiobox(_L("QA  host: api-qa.bambu-lab.com/v1"),  page, "", radio_grp_id, "qa_host",      m_iot_environment_def == ENV_QAT_HOST) );
    page->Add( create_item_radiobox(_L("PRE host: api-pre.bambu-lab.com/v1"), page, "", radio_grp_id, "pre_host",     m_iot_environment_def == ENV_PRE_HOST) );
    page->Add( create_item_radiobox(_L("Product host"),                       page, "", radio_grp_id, "product_host", m_iot_environment_def == ENV_PRODUCT_HOST) );

    Button* debug_button = new_button(page, _L("debug save button"), wxEmptyString, &m_button_list, wxDefaultSize);
    debug_button->SetFont(Label::Body_13);
    debug_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &/* e */) {
        // success message box
        MessageDialog dialog(this, _L("save debug settings"), _L("DEBUG settings have saved successfully!"), wxNO_DEFAULT | wxYES_NO | wxICON_INFORMATION);
        dialog.SetSize(400,-1);
        switch (dialog.ShowModal()) {
        case wxID_NO: {
            //if (m_developer_mode_def != app_config->get_bool("developer_mode")) {
            //    app_config->set_bool("developer_mode", m_developer_mode_def);
            //    m_developer_mode_ckeckbox->SetValue(m_developer_mode_def);
            //}
            //if (m_internal_developer_mode_def != app_config->get_bool("internal_developer_mode")) {
            //    app_config->set_bool("internal_developer_mode", m_internal_developer_mode_def);
            //    m_internal_developer_mode_ckeckbox->SetValue(m_internal_developer_mode_def);
            //}

            if (m_backup_interval_def != m_backup_interval_time) { m_backup_interval_textinput->GetTextCtrl()->SetValue(m_backup_interval_def); }

            if (auto res = std::find_if(iot_env_map.cbegin(), iot_env_map.cend(), [=](const auto &v) { return v.second == m_iot_environment_def; }); res != iot_env_map.end())
                select_radio_by_param(res->first);

            break;
        }

        case wxID_YES: {
            // bbs  domain changed
            const wxString param = get_selected_radio_param_by_group(radio_grp_id);
            const wxString new_env = iot_env_map.at(param);

            if (new_env != m_iot_environment_def) {
            // if (true) {
                app_config->set("iot_environment", new_env.ToStdString());
                app_config->save();

                m_iot_environment_def = new_env;
                wxGetApp().update_publish_status();

                if (NetworkAgent* agent = wxGetApp().getAgent()) {
                    std::string country_code = app_config->get_country_code();
                    wxGetApp().request_user_logout();
                    agent->set_country_code(country_code);
                }
                ConfirmBeforeSendDialog confirm_dlg(this, wxID_ANY, _L("Warning"), ConfirmBeforeSendDialog::ButtonStyle::ONLY_CONFIRM);
                confirm_dlg.update_text(_L("Switch cloud environment, Please login again!"));
                confirm_dlg.on_show();
            }

            // bbs  backup
            if (m_backup_interval_def != m_backup_interval_time) {
                // app_config->set("backup_interval", std::string(m_backup_interval_time.mb_str()));
                // app_config->save();
                Slic3r::set_backup_interval(boost::lexical_cast<long>(app_config->get("backup_interval")));
            }

            this->Close();
            break;
        }
        }
    });
    page->Add(debug_button, FromDIP(15), wxALIGN_CENTER_HORIZONTAL | wxTOP);

    return page->Finalize();
}

void PreferencesDialog::select_radio_by_param(const wxString &param)
{
    int groupid = -1;

    for (auto rs : std::as_const(m_radio_group)) {
        if (rs->m_param_name == param) {
            groupid = rs->m_groupid;
            break;
        }
    }
    if (groupid < 0)
        return;

    for (auto rs : std::as_const(m_radio_group)) {
        if (RadioBox *rb = rs->m_radiobox; rb && rs->m_groupid == groupid)
            rb->SetValue(rs->m_param_name == param);
    }
}

wxString PreferencesDialog::get_selected_radio_param_by_group(int groupid)
{
    for (auto rs : std::as_const(m_radio_group)) {
        if (RadioBox *rb = rs->m_radiobox; rs->m_groupid == groupid && rb && rb->GetValue())
            return rs->m_param_name;
    }

    return wxEmptyString;
}


}} // namespace Slic3r::GUI
