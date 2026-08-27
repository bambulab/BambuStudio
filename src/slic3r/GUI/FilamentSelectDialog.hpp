#ifndef slic3r_FilamentSelectDialog_hpp_
#define slic3r_FilamentSelectDialog_hpp_

#include "GUI_Utils.hpp"
#include "DeviceManager.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/StaticBox.hpp"
#include "Widgets/TextInput.hpp"
#include "fila_manager/wgtFilaManagerStore.h"

#include <map>
#include <set>
#include <unordered_map>
#include <vector>
#include <wx/arrstr.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/simplebook.h>
#include <wx/statbmp.h>

namespace Slic3r { namespace GUI {

class FilamentSelectDialog : public DPIDialog
{
public:
    struct SelectionResult {
        std::string spool_id;       // non-empty = Filament Manager spool selected
        wxString    preset_alias;   // non-empty = system preset selected (spool_id empty)

        // colour info (FM spool path only — read directly from the spool)
        int                      color_type{2};
        std::vector<std::string> colors;
        std::string              color_code;

        bool is_valid() const { return !spool_id.empty() || !preset_alias.IsEmpty(); }
    };

    explicit FilamentSelectDialog(wxWindow* parent);
    ~FilamentSelectDialog() {}

    void Popup(const wxArrayString&                          filament_items,
               const std::unordered_map<wxString, wxString>& vendors,
               const std::unordered_map<wxString, wxString>& types,
               const wxString&                               current_alias = wxString(),
               std::set<std::string>                         printer_names = {});

    const SelectionResult& get_result() const { return m_result; }

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;

private:
    void      create();
    wxWindow* build_manager_page(wxWindow* parent);
    wxWindow* build_default_page(wxWindow* parent);

    void      fill_manager_tab();
    void      fill_brand_chips(const std::vector<wxString>& brands);
    void      refresh_chip_visibility();
    void      apply_filters();
    wxWindow* make_spool_row(wxWindow* parent, const FilamentSpool& sp, bool dimmed, bool is_recent = false);

    void      fill_default_tab();
    void      select_brand(const wxString& brand);
    void      set_tab(int n);   // switches simplebook + updates tab-head styles
    void      filter_by_chip(const wxString& chip_label);
    void      on_confirm();

    wxArrayString                             m_filament_items;
    std::unordered_map<wxString, wxString>    m_vendors;
    std::unordered_map<wxString, wxString>    m_types;
    std::set<std::string>                     m_printer_names;
    std::map<wxString, std::vector<wxString>> m_brand_to_aliases;
    std::vector<wxString>                     m_ordered_brands;
    wxString                                  m_checked_alias;
    wxString                                  m_selected_brand;

    // Tab head buttons (visual state toggled by set_tab())
    Button*           m_tab_mgr{nullptr};
    Button*           m_tab_def{nullptr};
    // 3-px underline panels below each tab button (green=active, white=inactive)
    wxPanel*          m_tab_underline_mgr{nullptr};
    wxPanel*          m_tab_underline_def{nullptr};
    // Content switcher — using wxSimplebook because Tabbook is for the main window
    // sidebar and its GetPageRect() returns zero size inside a dialog.
    wxSimplebook*     m_book{nullptr};

    TextInput*        m_search{nullptr};
    wxPanel*          m_chip_scroll{nullptr};
    wxBoxSizer*       m_chip_sizer{nullptr};
    wxStaticBitmap*   m_left_arrow{nullptr};
    wxStaticBitmap*   m_right_arrow{nullptr};
    wxBitmap          m_bmp_left_on;
    wxBitmap          m_bmp_left_off;
    wxBitmap          m_bmp_right_on;
    wxBitmap          m_bmp_right_off;
    wxScrolledWindow* m_mgr_list{nullptr};
    wxBoxSizer*       m_mgr_list_sizer{nullptr};

    wxScrolledWindow* m_brand_list{nullptr};
    wxBoxSizer*       m_brand_list_sizer{nullptr};
    wxScrolledWindow* m_type_list{nullptr};
    wxBoxSizer*       m_type_list_sizer{nullptr};

    // chip + search filter state
    struct MgrRow { wxWindow* w; wxString brand; wxString search_key; };
    std::vector<MgrRow>   m_mgr_rows;
    wxString                                    m_active_chip;
    std::vector<Button*>                        m_chips;
    int                                         m_chip_offset{0};
    int                                         m_chip_scroll_width{0};

    // row → spool reverse map (populated by make_spool_row for non-dimmed rows)
    std::map<wxWindow*, FilamentSpool>          m_row_to_spool;

    // confirmed selection result (valid after EndModal(wxID_OK))
    SelectionResult   m_result;

    StateColor        m_btn_bg_green;
    wxWindow*         m_selected_row{nullptr};
};

}} // namespace Slic3r::GUI

#endif // slic3r_FilamentSelectDialog_hpp_
