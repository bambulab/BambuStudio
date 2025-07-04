#pragma once

#include "GUI_Utils.hpp"

#include <vector>
#include <map>
#include <unordered_set>

class TabCtrl;
class SwitchButton;
class TextInput;
class CheckBox;
class Label;
class Button;

namespace Slic3r {

class Preset;

namespace GUI {

class UserPresetsDialog : public DPIDialog
{
public:
    UserPresetsDialog(wxWindow * parent);

private:
    void init_preset_list();

    void create_preset_list(wxWindow *parent);

    wxSizer *create_preset_line(wxWindow *parent, std::string const & preset);

    wxSizer *create_filament_group(wxWindow *parent, std::pair<std::string const, std::vector<std::string>> const &filament);

    void layout_preset_list(bool delete_old = false);

    void on_collection_changed(int collection);

    void on_search(wxString const & keyword);

    void on_preset_checked(std::string const &preset, bool checked, bool from_user);

    void on_filament_checked(std::string const &preset, bool checked, bool from_user);

    void on_all_checked(bool checked, bool from_user);

    void update_preset_counts();

    void update_checked();

    void delete_checked();

    bool delete_presets(int collection, std::vector<std::string> &presets);

    bool delete_confirm(int collection, int preset_num);
    
    bool delete_confirm(int collection, int filament_preset_num, int print_preset_num);

    void on_dpi_changed(const wxRect &suggested_rect) override;

    bool is_filament_list() const;

private:
    TabCtrl * m_tab_ctrl;
    SwitchButton * m_switch_button;
    TextInput * m_search;
    wxPanel * m_empty_panel;
    wxScrolledWindow * m_scrolled;
    std::map<std::string, wxSizer *> m_preset_sizers;
    std::map<std::string, wxSizer *> m_filament_sizers;
    std::unordered_set<wxSizer*> m_hiden_sizers;
    CheckBox * m_check_all;
    Label * m_label_check_count;
    Button * m_button_delete;

private:
    std::vector<std::vector<std::string>> m_presets;
    std::map<std::string, std::string> m_filament_names;
    std::map<std::string, std::vector<std::string>> m_filament_presets;

    int m_collection = 0;
    std::vector<std::string> m_checked_presets;
    std::map<std::string, size_t> m_checked_filaments;
};

}}