/*****************************************************************//**
 * \file   ViewModelBuilder.cpp
 * \brief  Snapshot + display schema of the Select Filament dialog.
 *********************************************************************/
#include "ViewModelBuilder.hpp"

#include <algorithm>
#include <cctype>

namespace Slic3r { namespace GUI {

namespace FS  = FilamentSelectSchema;
namespace FSC = FilamentSelectCommon;

namespace {

constexpr const char* kRowKeyPrefix = "r";

std::string utf8(const wxString& s) { return std::string(s.ToUTF8().data()); }

/// Chip / brand key of a supported-spool brand or a preset vendor.
/// An empty name is the "Other" bucket.
std::string brand_key(const wxString& brand)
{
    return brand.IsEmpty() ? std::string(FS::values::chip_key::other)
                           : std::string(FS::values::chip_key::brand_prefix) + utf8(brand);
}

FS::format::Chip brand_chip(const wxString& brand)
{
    FS::format::Chip c;
    c.key   = brand_key(brand);
    c.kind  = brand.IsEmpty() ? FS::values::chip_kind::other : FS::values::chip_kind::brand;
    c.label = brand.IsEmpty() ? std::string() : utf8(brand);
    return c;
}

FS::format::Tail row_tail(const FSC::SpoolRowEntry& entry)
{
    FS::format::Tail t;
    const wxString slot = FSC::slot_label(entry.spool);
    if (!slot.IsEmpty()) {
        t.kind      = FS::values::tail_kind::slot;
        t.slot_name = utf8(slot);
    } else if (entry.group == FSC::SpoolRowGroup::Recent) {
        t.kind = FS::values::tail_kind::recent;
    }
    return t;
}

} // namespace

std::string FilamentSelectBuilder::RowKey(std::size_t index)
{
    return kRowKeyPrefix + std::to_string(index);
}

bool FilamentSelectBuilder::ParseRowKey(const std::string& key, std::size_t& index)
{
    const std::string prefix(kRowKeyPrefix);
    if (key.size() <= prefix.size() || key.compare(0, prefix.size(), prefix) != 0) return false;
    const std::string digits = key.substr(prefix.size());
    if (digits.size() > 9 ||
        !std::all_of(digits.begin(), digits.end(), [](unsigned char c) { return std::isdigit(c); }))
        return false;
    index = static_cast<std::size_t>(std::stoul(digits));
    return true;
}

FilamentSelectSnapshot FilamentSelectBuilder::BuildSnapshot(const FilamentSelectInput& in)
{
    FilamentSelectSnapshot snap;
    snap.rows           = FSC::build_spool_rows(in.printer_names);
    snap.presets        = FSC::build_preset_groups(in);
    snap.filament_items = in.filament_items;
    // The native dialog may preselect a recently-used spool_id here, which is no
    // preset alias at all; only keep a value the presets tab can actually show.
    if (!in.current_alias.IsEmpty() && in.filament_items.Index(in.current_alias) != wxNOT_FOUND)
        snap.checked_alias = in.current_alias;
    return snap;
}

FS::format::State FilamentSelectBuilder::BuildState(const FilamentSelectSnapshot& snap, bool dark)
{
    FS::format::State state;
    state.theme = dark ? FS::values::theme::dark : FS::values::theme::light;

    // Manager tab chips: All + brands + Unsupported.
    auto& chips = state.manager.chips;
    chips.push_back({FS::values::chip_key::all, FS::values::chip_kind::all, {}});
    for (const wxString& b : snap.rows.brands) chips.push_back(brand_chip(b));
    chips.push_back({FS::values::chip_key::unsupported, FS::values::chip_kind::unsupported, {}});

    // Manager tab rows.
    state.manager.rows.reserve(snap.rows.rows.size());
    for (std::size_t i = 0; i < snap.rows.rows.size(); ++i) {
        const FSC::SpoolRowEntry& entry = snap.rows.rows[i];
        const FilamentSpool&      sp    = entry.spool;

        FS::format::SpoolRow row;
        row.row_key           = RowKey(i);
        row.chip_key          = entry.group == FSC::SpoolRowGroup::Unsupported
                                    ? std::string(FS::values::chip_key::unsupported)
                                    : brand_key(entry.brand);
        row.search_key        = utf8(FSC::spool_search_key(sp));
        row.name              = utf8(FSC::spool_row_name(sp));
        row.weight_g          = FSC::spool_weight_grams(sp);
        row.swatch.color_type = sp.color_type;
        row.swatch.colors     = sp.colors;
        row.swatch.color_code = sp.color_code;
        row.show_bbl_logo     = FSC::is_bbl_brand(sp.brand);
        row.tail              = row_tail(entry);
        row.selectable        = entry.selectable();
        state.manager.rows.push_back(std::move(row));
    }

    // Presets tab.
    std::string checked_brand;
    for (const wxString& b : snap.presets.brands) {
        state.presets.brands.push_back(brand_chip(b));
        std::vector<std::string>& aliases = state.presets.aliases[brand_key(b)];
        for (const wxString& a : snap.presets.aliases.at(b)) {
            aliases.push_back(utf8(a));
            if (checked_brand.empty() && !snap.checked_alias.IsEmpty() && a == snap.checked_alias)
                checked_brand = brand_key(b);
        }
    }
    state.presets.checked_alias = utf8(snap.checked_alias);
    // Show the vendor holding the current preset, else the first vendor.
    if (!checked_brand.empty())
        state.presets.initial_brand = checked_brand;
    else if (!snap.presets.brands.empty())
        state.presets.initial_brand = brand_key(snap.presets.brands.front());

    return state;
}

}} // namespace Slic3r::GUI
