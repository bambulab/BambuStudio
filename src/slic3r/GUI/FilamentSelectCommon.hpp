#ifndef slic3r_FilamentSelectCommon_hpp_
#define slic3r_FilamentSelectCommon_hpp_

/*****************************************************************//**
 * \file   FilamentSelectCommon.hpp
 * \brief  Data rules of the Select Filament dialog, shared by the native
 *         dialog (FilamentSelectDialog) and the Web dialog (FilamentSelect VM).
 *
 * Grouping, ordering, the recently-used list and the row texts live here once,
 * so the two front ends cannot drift apart.
 *********************************************************************/

#include "FilamentSelectTypes.hpp"
#include "fila_manager/wgtFilaManagerStore.h"

#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <wx/string.h>

namespace Slic3r { namespace GUI { namespace FilamentSelectCommon {

/// Recently used Filament Manager spools (LRU, newest first).
constexpr std::size_t SPOOL_RECENT_MAX  = 6;
constexpr std::size_t SPOOL_RECENT_SHOW = 5;

std::vector<wxString> load_recent_spool_ids();
void                  remember_recent_spool(const wxString& spool_id);

/// Brands pinned to the front of the brand chips, in this order.
const std::vector<wxString>& brand_priority();

/// Orders brand keys: priority brands, the rest alphabetically, then `other`
/// (if present) pinned last.
std::vector<wxString> order_brands(std::vector<wxString> keys, const wxString& other);

bool     is_bbl_brand(const std::string& brand);
int      spool_weight_grams(const FilamentSpool& sp);
/// Slot label of an in-printer spool ("A1", ...); empty when not mounted.
/// Desensitized by construction: never contains dev_id / SN / device name.
wxString slot_label(const FilamentSpool& sp);
/// Name shown on a spool row.
wxString spool_row_name(const FilamentSpool& sp);
/// Lower-cased "brand series material colour" string used by the search box.
wxString spool_search_key(const FilamentSpool& sp);

/// Where a spool row sits in the Filament Manager list.
enum class SpoolRowGroup {
    Recent,      ///< recently used, selectable
    Available,   ///< usable with this printer, selectable
    InPrinter,   ///< already loaded in an AMS slot, dimmed
    Unsupported, ///< no preset for this printer, dimmed
};

struct SpoolRowEntry
{
    FilamentSpool spool;
    /// Brand bucket of a supported spool; empty = "Other" (spool has no brand).
    /// Meaningless for the Unsupported group.
    wxString      brand;
    SpoolRowGroup group{SpoolRowGroup::Available};

    bool selectable() const { return group == SpoolRowGroup::Recent || group == SpoolRowGroup::Available; }
};

struct SpoolRowList
{
    /// Ordered brand buckets of the supported spools; an empty entry is the
    /// "Other" bucket and is always last.
    std::vector<wxString>      brands;
    /// Rows in display order: Recent, Available, InPrinter, Unsupported.
    std::vector<SpoolRowEntry> rows;
};

/// Reads the Filament Manager store and lays the spools out in display order.
SpoolRowList build_spool_rows(const std::set<std::string>& printer_names);

struct PresetGroups
{
    /// Ordered vendors; an empty entry is the "Other" vendor and is always last.
    std::vector<wxString>                     brands;
    std::map<wxString, std::vector<wxString>> aliases; // vendor -> sorted aliases
};

/// Groups the system preset aliases by vendor using the shared filament order.
PresetGroups build_preset_groups(const FilamentSelectInput& in);

}}} // namespace Slic3r::GUI::FilamentSelectCommon

#endif // slic3r_FilamentSelectCommon_hpp_
