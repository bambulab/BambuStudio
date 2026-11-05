#pragma once

#include "EncodedFilament.hpp"
#include "FilamentCatalog.hpp"
#include <wx/dialog.h>
#include <string>
#include <vector>

class wxSearchCtrl;
class wxListCtrl;
class wxStaticText;
class wxButton;
class wxCheckBox;

namespace Slic3r { namespace GUI {

struct FilamentSpool;

std::string find_catalog_profile(PresetBundle& bundle, size_t slot, const wxString& filament_id);
std::string find_inventory_profile(PresetBundle& bundle, size_t slot, const FilamentSpool& spool);

// Copies catalog values: no pointers into a reloadable application catalog survive
// this modal dialog. A result names an exact compatible profile, not an alias.
struct FilamentCatalogChoice {
    FilamentColor color;
    std::string preset_name;
};

class FilamentCatalogDialog : public wxDialog {
public:
    FilamentCatalogDialog(wxWindow* parent, PresetBundle& bundle, size_t slot);
    FilamentCatalogChoice selection() const;

private:
    struct Entry {
        FilamentColorCode* info = nullptr;
        std::string preset_name;
        FilamentColor color;
        wxString name, material, product_code, search_text, note;
        int image = -1;
        bool inventory = false;
        InventoryRemaining remaining;
        InventoryNotes notes;
    };
    void filter();
    void update_selection();
    void apply_selection();
    std::vector<Entry> m_entries;
    std::vector<size_t> m_visible;
    wxSearchCtrl* m_search;
    wxCheckBox* m_mine;
    wxListCtrl* m_results;
    wxStaticText* m_status;
    wxStaticText* m_choice;
    wxButton* m_apply;
    int m_sort_column = 0;
    bool m_sort_ascending = true;
};

}} // namespace Slic3r::GUI
