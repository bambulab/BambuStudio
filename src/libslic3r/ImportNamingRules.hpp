#pragma once
#include <string>
#include <vector>
#include <regex>
#include "Model.hpp"

namespace Slic3r {

// Result of parsing tags from a body/volume name.
struct ImportNameTags {
    int              filament   = 0;               // 0 = not specified
    ModelVolumeType  volume_type = ModelVolumeType::INVALID; // INVALID = not specified
};

// Default tag vocabulary (shipped defaults, user-configurable).
// Tags are matched case-insensitively inside square brackets.
struct ImportNamingRule {
    std::string tag;           // e.g. "neg", "f1"
    int         filament = 0;  // >0 means this rule sets the filament
    ModelVolumeType volume_type = ModelVolumeType::INVALID;
};

// Returns the default ruleset.
std::vector<ImportNamingRule> default_import_naming_rules();

// Parse all [tag] tokens from a name string and return combined result.
// Rules are checked in order; last filament/type tag wins.
ImportNameTags parse_import_name_tags(const std::string& name,
                                      const std::vector<ImportNamingRule>& rules);

// Apply parsed tags to a volume in-place.
// Only sets filament/type if the tag was actually found (non-zero / non-INVALID).
void apply_import_name_tags(ModelVolume& volume, const ImportNameTags& tags);

// Convenience: parse and apply in one call.
void apply_naming_rules_to_volume(ModelVolume& volume,
                                  const std::vector<ImportNamingRule>& rules);

// Apply naming rules to all volumes of all objects in a list.
void apply_naming_rules_to_objects(const ModelObjectPtrs& objects,
                                   const std::vector<ImportNamingRule>& rules);

} // namespace Slic3r
