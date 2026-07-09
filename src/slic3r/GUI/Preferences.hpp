#ifndef slic3r_Preferences_hpp_
#define slic3r_Preferences_hpp_

#include "GUI.hpp"
#include "GUI_Utils.hpp"

#include <wx/list.h>
#include <vector>

class Button;
class CheckBox;
class ComboBox;
class Tabbook;
class TextInput;
class wxBookCtrlEvent;

namespace Slic3r { namespace GUI {

class ScrolledPanel;
class RadioBox;

class RadioSelector
{
public:
    wxString  m_param_name;
    int       m_groupid;
    RadioBox *m_radiobox;
    bool      m_selected = false;
};

WX_DECLARE_LIST(RadioSelector, RadioSelectorList);

class PreferencesDialog : public DPIDialog
{
public:
    PreferencesDialog(wxWindow *      parent,
                      wxWindowID      id    = wxID_ANY,
                      const wxString &title = wxT(""),
                      const wxPoint & pos   = wxDefaultPosition,
                      const wxSize &  size  = wxDefaultSize,
                      long            style = wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

    ~PreferencesDialog();

    bool use_12h_time_format_changed() const;
    // TODO: legacy use, remove
    bool seq_top_layer_only_changed() const { return false; }
    bool seq_seq_top_gcode_indices_changed() const { return false; }

private:
    AppConfig *app_config;
    Tabbook *m_page_book { nullptr };

    int m_current_language_selected = { 0 };
    bool m_original_use_12h_time_format;

    std::vector<Button *> m_button_list;
    std::vector<CheckBox *> m_checkbox_list;
    std::vector<ComboBox *> m_combobox_list;
    RadioSelectorList m_radio_group;

    static int m_last_selected_page;

    // debug mode
    wxString m_backup_interval_time;
    ::CheckBox * m_developer_mode_ckeckbox   = {nullptr};
    ::CheckBox * m_internal_developer_mode_ckeckbox = {nullptr};
    ::CheckBox * m_dark_mode_ckeckbox        = {nullptr};
    ::TextInput *m_backup_interval_textinput = {nullptr};

    bool m_developer_mode_def;
    bool m_internal_developer_mode_def;
    wxString m_backup_interval_def;
    wxString m_iot_environment_def;


private:
    wxBoxSizer *create_item_combobox(
        const wxString &title, wxWindow *parent, const wxString &tooltip, const std::string &param,
        const std::vector<wxString> &label_list, const std::vector<std::string> &value_list,
        std::function<void(int)> callback = nullptr, int label_width = 0, int combo_width = 0
    );
    wxBoxSizer *create_item_region_combobox(const wxString &title, wxWindow *parent, const wxString &tooltip, const std::vector<wxString> &vlist);
    wxBoxSizer *create_item_language_combobox(const wxString &title, wxWindow *parent, const wxString &tooltip, const std::string &param, const std::vector<const wxLanguageInfo *> &vlist);
    wxBoxSizer *create_item_checkbox(const wxString &title, wxWindow *parent, const wxString &tooltip, const std::string &param, std::function<void(int)> callback = nullptr);
    wxBoxSizer *create_item_darkmode_checkbox(const wxString &title, wxWindow *parent, const wxString &tooltip, const std::string &param);
    wxWindow   *create_item_downloads(wxWindow* parent, const std::string &param);
    wxBoxSizer *create_item_input(
        const wxString &title, const wxString &title2, wxWindow *parent, const wxString &tooltip, const std::string &param,
        std::function<void(const wxString&)> onchange = nullptr, int label_width = 0
    );
    TextInput *create_range_input(
        wxWindow *parent, const wxString &tooltip, const std::string &param, float range_min, float range_max, int precision, std::function<void(const wxString&)> onchange = nullptr
    );
    wxBoxSizer *create_item_range_input(
        const wxString &title, wxWindow *parent, const wxString &tooltip, const std::string &param, float range_min, float range_max, int precision,
        std::function<void(const wxString&)> onchange = nullptr, int label_width = 0
    );
    wxBoxSizer *create_item_range_two_input(
        const wxString &title, wxWindow *parent, const wxString &tooltip,
        const std::string &param, const std::string &param1,
        float range_min, float range_max, int precision,
        std::function<void(const wxString&)> onchange = nullptr,
        std::function<void(const wxString&)> onchange1 = nullptr,
        int label_width = 0
    );
    wxBoxSizer *create_item_backup_input(const wxString &title, wxWindow *parent, const wxString &tooltip);
    /// Radio boxes are only used for debug mode page and have no inherent property saving functionality;
    /// TODO: convert host options to selectors, or make radios more generally useful.
    wxWindow   *create_item_radiobox(const wxString &title, wxWindow *parent, const wxString &tooltip, int groupid, const std::string &param, bool select = false);

    void create();
    void set_window_size();
    void on_menu_item_selected(wxBookCtrlEvent &ev);
    void on_dpi_changed(const wxRect &suggested_rect) override;

    /// Passing a non-null parent that != m_page_book to the page creation methods will create a non-scrollable pane which can be embedded in another page (the given parent).
    ScrolledPanel* create_book_page(wxWindow *parent = nullptr) const;
    wxWindow* create_general_page(wxWindow *parent = nullptr);
    wxWindow* create_device_page(wxWindow *parent = nullptr);
    wxWindow* create_online_page(wxWindow *parent = nullptr);
    wxWindow* create_projects_page(wxWindow *parent = nullptr);
    wxWindow* create_files_page(wxWindow *parent = nullptr);
    wxWindow* create_3Dview_page(wxWindow *parent = nullptr);
    wxWindow* create_advanced_page(wxWindow *parent = nullptr);

    // debug mode
    wxWindow* create_debug_page(wxWindow *parent = nullptr);
    void     select_radio_by_param(const wxString &param);
    wxString get_selected_radio_param_by_group(int groupid);
};

}} // namespace Slic3r::GUI

#endif /* slic3r_Preferences_hpp_ */
