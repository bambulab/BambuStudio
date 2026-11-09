#pragma once

// Owned-spool swatches for the filament colour pickers.
//
// The project filament colour button and the AMS slot colour popup offer the
// colours of the user's Filament Manager spools whose type matches the
// filament being coloured (e.g. every "PLA Matte" spool, any brand).
//
// A spool's type is its series (cloud `filamentName`). Spool types that are
// not named like a Bambu filament type are mapped by a user-editable rules
// file, <data_dir>/spool_color_rules.json (see kSpoolColorRulesFile):
//
//   { "type_aliases": { "PLA+": "PLA Basic", "PETG Rapid": ["PETG HF"] } }
//
// The file is read each time a picker opens, so edits apply without a rebuild.
// No wxWidgets here so the matching logic stays unit-testable.

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "wgtFilaManagerStore.h"

namespace Slic3r { namespace GUI {

constexpr const char* kSpoolColorRulesFile = "spool_color_rules.json";

struct SpoolColorRules
{
    // Lower-cased, trimmed spool type -> filament types it is offered under.
    std::map<std::string, std::vector<std::string>> type_aliases;

    static SpoolColorRules from_json(const nlohmann::json& j);
    // Missing or unreadable file -> no rules.
    static SpoolColorRules load(const std::string& path);
};

struct SpoolSwatch
{
    std::vector<std::string> colors;     // "#RRGGBB"; several for multi-colour spools
    int                      color_type = 2; // FilamentSpool::color_type
    std::string              label;      // "Overture Army Green", or just "Overture"
    std::string              brand;      // "Overture"; when label == brand the
                                         // caller resolves the colour word itself
};

// Filament types the spool is offered under, as written in the spool or in
// its rule (e.g. {"PETG HF"} for a "PETG Rapid" spool). Used to find the
// Bambu preset for spools that the Filament Manager left without one.
std::vector<std::string> spool_preset_types(const FilamentSpool& spool, const SpoolColorRules& rules);

// Swatches of the usable spools offered under the filament preset being
// coloured. A spool is offered when it is linked to that preset by its
// `filament_id` (the Filament Manager "Prints With" field), or else when its
// type matches `filament_type`, the preset's display name (e.g. "PLA Matte"),
// directly or through a rule. `filament_id` is the preset's filament_id; the
// link is the primary path so a spool needs no type rule once it is set.
// Sorted by label; identical swatches appear once.
std::vector<SpoolSwatch> spool_swatches_for(const std::vector<FilamentSpool>& spools,
                                            const std::string&                filament_type,
                                            const SpoolColorRules&            rules,
                                            const std::string&                filament_id = "");

}} // namespace Slic3r::GUI
