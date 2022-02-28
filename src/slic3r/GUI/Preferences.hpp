#ifndef slic3r_Preferences_hpp_
#define slic3r_Preferences_hpp_

#include "GUI.hpp"
#include "GUI_Utils.hpp"

#include <wx/dialog.h>
#include <wx/timer.h>
#include <vector>
#include <list>
#include <map>
#include "Widgets/ComboBox.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/TextInput.hpp"

namespace Slic3r { namespace GUI {

class Selector
{
public:
    int           m_index;
    wxWindow *    m_selpanel;
    wxPanel *     m_conpanel;
    wxStaticText *m_seltext;
};
WX_DECLARE_HASH_MAP(int, Selector *, wxIntegerHash, wxIntegerEqual, SelectorHash);

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

#define DESIGN_RESOUTION_PREFERENCES wxSize(618, 520)
#define DESIGN_SELECTOR_NOMORE_COLOR wxColour(248, 248, 248)
#define DESIGN_SELECTOR_SELECTED_COLOR wxColour(255, 255, 255)


class CheckBox;
class TextInput;

class PreferencesDialog : public DPIDialog
{
private:
    AppConfig *app_config;

protected:
    wxPanel *     m_panel_selects;
    wxPanel *     m_panel_content;
    wxStaticLine *m_main_line;

    wxBoxSizer *     m_fgSizer_main;
    wxFlexGridSizer *m_fgSizer_body;
    wxBoxSizer *     m_bSizer_selects;

    wxPanel *m_panel_general;
    wxPanel *m_panel_gui;
    wxPanel *m_panel_sync;
    wxPanel *m_panel_shortcuts;
    wxPanel *m_panel_debug;

	//bool								m_settings_layout_changed {false};
	bool								m_seq_top_layer_only_changed{ false };
	bool								m_recreate_GUI{false};

public:
    bool seq_top_layer_only_changed() const { return m_seq_top_layer_only_changed; }
    bool recreate_GUI() const { return m_recreate_GUI; }
    void build(size_t selected_tab = 0);
    void on_dpi_changed(const wxRect &suggested_rect) override;
    void OnPaint(wxPaintEvent &e);

public:
    PreferencesDialog(wxWindow *      parent,
                      wxWindowID      id    = wxID_ANY,
                      const wxString &title = wxT(""),
                      const wxPoint & pos   = wxDefaultPosition,
                      const wxSize &  size  = DESIGN_RESOUTION_PREFERENCES,
                      long            style = wxSYSTEM_MENU | wxCAPTION | wxCLOSE_BOX);

     ~PreferencesDialog();
    void Init();
    wxString          m_backup_interval_time;

    //debug mode
    ::CheckBox*       m_developer_mode_ckeckbox;
    ::TextInput*      m_backup_interval_textinput;

    wxString          m_developer_mode_def;
    wxString          m_backup_interval_def;
    wxString          m_iot_environment_def;


    SelectorHash      m_hash_selector;
    RadioSelectorList m_radio_group;
    // ComboBoxSelectorList    m_comxbo_group;

    wxWindow *create_item_title(wxString title, wxWindow *parent, wxString tooltip);
    wxWindow *create_item_combobox(wxString title, wxWindow *parent, wxString tooltip, int padding_left, std::string param, std::vector<wxString> vlist);
    wxWindow *create_item_language_combobox(wxString title, wxWindow *parent, wxString tooltip, int padding_left, std::string param, std::vector<const wxLanguageInfo *> vlist);
    wxWindow *create_item_checkbox(wxString title, wxWindow *parent, wxString tooltip, int padding_left, std::string param);
    wxWindow *create_item_input(wxString title, wxWindow *parent, wxString tooltip, int padding_left, std::string param);
    wxWindow *create_item_multiple_combobox(
        wxString title, wxWindow *parent, wxString tooltip, int padding_left, std::string parama, std::vector<wxString> vlista, std::vector<wxString> vlistb);
    wxWindow *create_item_radiobox(wxString title, wxWindow *parent, wxString tooltip, int padding_left, int groupid, wxString param);

    void create_select_tabel(const wxString &title, int id, int topleft, wxPanel *panel);

    void create_general_page();
    void create_gui_page();
    void create_sync_page();
    void create_shortcuts_page();
    void create_debug_page();

    void on_select(int index);
    void on_select_radio(wxString param);
	wxString get_select_radio(int groupid);
    // BBS
	void create_select_domain_widget();

    void Split(const std::string &src, const std::string &separator, std::vector<wxString> &dest);

protected:
    void OnSelectTabel(wxMouseEvent &event);
    void OnSelectRadio(wxMouseEvent &event);
};

}} // namespace Slic3r::GUI

#endif /* slic3r_Preferences_hpp_ */
