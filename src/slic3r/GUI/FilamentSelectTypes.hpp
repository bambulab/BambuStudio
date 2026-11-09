#ifndef slic3r_FilamentSelectTypes_hpp_
#define slic3r_FilamentSelectTypes_hpp_

#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <wx/arrstr.h>
#include <wx/string.h>

namespace Slic3r { namespace GUI {

/// Everything the Select Filament dialog needs from its caller. Shared by the
/// native dialog and the Web dialog so both are opened with the same data.
struct FilamentSelectInput
{
    wxArrayString                          filament_items; // system preset aliases
    std::unordered_map<wxString, wxString> vendors;        // alias -> vendor
    std::unordered_map<wxString, wxString> types;          // alias -> filament type
    wxString                               current_alias;  // preselected system preset, may be empty
    std::set<std::string>                  printer_names;  // printers a spool needs a preset for
};

/// Outcome of the Select Filament dialog. Exactly one of spool_id / preset_alias
/// is set when the user confirmed; both are empty when the dialog was cancelled.
struct FilamentSelectResult
{
    std::string spool_id;       // non-empty = Filament Manager spool selected
    wxString    preset_alias;   // non-empty = system preset selected (spool_id empty)

    // colour info (FM spool path only — read directly from the spool)
    int                      color_type{2};
    std::vector<std::string> colors;
    std::string              color_code;

    bool is_valid() const { return !spool_id.empty() || !preset_alias.IsEmpty(); }
};

}} // namespace Slic3r::GUI

#endif // slic3r_FilamentSelectTypes_hpp_
