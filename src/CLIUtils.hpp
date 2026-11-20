#ifndef slic3r_CLIUtils_hpp_
#define slic3r_CLIUtils_hpp_

// Helpers only used by the command line slicer (CLI::run in BambuStudio.cpp).
// They do not use the statics of BambuStudio.cpp, everything is passed in as parameters.

#include <functional>
#include <set>
#include <string>
#include <vector>

namespace Slic3r {

class DynamicPrintConfig;

//======================================================================================================================
// --switch_process
//
// By default, when a 3mf is sliced for another printer, the CLI keeps the process of the 3mf.
// With --switch_process, the process is switched to a system process of the new printer, and the user's modifications are kept.
//   1. match_process_by_layer_height() finds the system process of the new printer with the same layer height.
//   2. compute_process_switch_keep_keys() gets the keys modified by the user in the 3mf.
//   3. merge_process_to_new_preset() starts from the new system process and keeps those modifications.
// If any step fails, the process is not switched.
// Nothing here is specific to a printer model, and any extruder count is supported in both directions.
//
// ---- Variants and print_extruder_id ----
//
// A process option with variants has one value per (print_extruder_variant[i], print_extruder_id[i]).
// On a multi-extruder printer the variant names repeat per extruder, e.g. H2D: [Std, HF, Std, HF] with ids [1, 1, 2, 2].
// The ids in a 3mf may not fit the printer of the process, in two cases.
//   (a) The 3mf comes from a printer with another extruder count.
//   (b) The 3mf was saved with a wrong system preset, e.g. H3C/P3C had [2, 2, 2] before BBL profile 02.09.00.76.
// Matching by name AND id then gives -1 for every variant, and the user's modifications would be dropped silently.
// So the process switch matches by name AND id only when both sides have more than one extruder id, otherwise by name only.
//   multi  -> single: the src variant of the master extruder (master_extruder_id of the 3mf, e.g. 2 for H2C/H2D) is used,
//                     like the GUI when switching the printer (Tab::remap_variant_cache).
//   single -> multi:  the src variant is used for every extruder of dst.
//   single -> single: there is one variant per name, so a wrong id does not matter.
// After the switch, print_extruder_id and print_extruder_variant are always the new system process's.
// The object / part / layer range configs of the 3mf are mapped by the same rules, see variant_index of merge_process_to_new_preset().
// The variants are matched by compute_variant_index_by_name() in PrintConfig.hpp.
//======================================================================================================================

// The result of --switch_process, written to result.json as "process_switch".
struct ProcessSwitchInfo
{
    // --switch_process is set
    bool                     enabled{false};
    // the process is switched
    bool                     switched{false};
    // the system process of the 3mf (the parent if the 3mf uses a user process)
    std::string              from;
    // the matched system process of the new printer, empty if not switched
    std::string              to;
    // why the process is switched: ProcessMatchResult::reason of the match, e.g. quality_match
    // why the process is not switched:
    //   not_bbl_3mf / process_specified / no_machine_switch - the conditions of --switch_process are not met, checked in this order
    //   no_system_process - the system process of the 3mf is not found
    //   no_process_full / no_compatible_process / no_layer_height_match - ProcessMatchResult::reason, no process is matched
    //   load_failed - the matched process can not be loaded
    //   merge_failed - merge_process_to_new_preset() failed
    std::string              reason;
    // the kept keys still different from the new system process, sorted; they become different_settings_to_system
    std::vector<std::string> kept_keys;
};

// The result of match_process_by_layer_height().
struct ProcessMatchResult
{
    // the matched system process, also its file name in process_full; empty if not matched
    std::string              name;
    // why it is chosen:
    //   unique          - the only candidate
    //   quality_match   - the same quality as the old process, or a fallback quality (e.g. Optimal -> Standard)
    //   default_profile - the default process of the new printer
    //   standard        - the "Standard" quality
    //   alphabetical    - the first candidate by name
    // why nothing is matched:
    //   no_process_full       - process_full does not exist
    //   no_compatible_process - no system process is compatible with the new printer
    //   no_layer_height_match - no compatible system process has the same layer height
    std::string              reason;
    // the compatible system processes with the same layer height, sorted by name
    std::vector<std::string> candidates;
};

// The quality part of a process name, e.g. "0.20mm High Quality @BBL P2S" -> "High Quality", "0.20mm Standard" -> "Standard".
// Returns an empty string if the name has no "mm " part.
std::string extract_process_quality_name(const std::string &process_name);

// Find the system process of the new printer with the same layer height, by reading the files in process_full_dir.
// A process matches if new_printer_system_name is in its compatible_printers and its layer_height equals target_layer_height.
// When several match, they are chosen in the order of ProcessMatchResult::reason.
// The quality of old_process_system_name is preferred first, then new_default_process_name.
ProcessMatchResult match_process_by_layer_height(const std::string &process_full_dir,
                                                 const std::string &new_printer_system_name,
                                                 double             target_layer_height,
                                                 const std::string &old_process_system_name,
                                                 const std::string &new_default_process_name);

// Variant mapping of the uptodate path (update_full_config in BambuStudio.cpp), for both the printer and the process.
// It always matches by name AND id, and is not used by --switch_process.
// new_index: for each variant of new_config, the index of the same variant in full_config, or -1.
// A full_config without variants (old 3mf) is regarded as "Direct Drive Standard" of extruder 1.
// count_changed: whether the variant count is changed; update_full_config only uses new_index when it is true.
// count_changed is set to false if the variants of either config are invalid.
// With a wrong print_extruder_id (case (b) above), every index is -1.
// This does no harm unless the variant count also changed: the modified keys are kept whole, and the id is reset to the system value.
void compute_variant_index(const DynamicPrintConfig &full_config,
                           const DynamicPrintConfig &new_config,
                           const std::string        &id_name,
                           const std::string        &variant_name,
                           std::vector<int>         &new_index,
                           bool                     &count_changed);

// The keys whose 3mf values are kept when switching the process.
// different_settings: different_settings_to_system[0] of the 3mf, empty if the 3mf uses a system process.
// Returns its keys, except the preset bookkeeping keys, print_extruder_id / print_extruder_variant, and the gcodes if skip_modified_gcodes.
// print_extruder_id / print_extruder_variant must be the new printer's, which also drops the wrong ids of old 3mfs.
// The wipe tower size keys (prime_tower_width / prime_tower_brim_width) are always kept, because the objects of the 3mf are placed around the wipe tower of that size.
std::set<std::string> compute_process_switch_keep_keys(const std::string           &different_settings,
                                                       bool                         skip_modified_gcodes,
                                                       const std::set<std::string> &gcodes_keys);

// Merge the new system process into full_config (the config of the 3mf, with its printer's master_extruder_id), in place.
// For each key of new_system_config, except the preset bookkeeping keys:
//   not in keep_keys:              the new value.
//   in keep_keys, without variants: the 3mf value.
//   in keep_keys, with variants:    the new value, then the 3mf value for each variant the user modified.
// A variant is modified if its 3mf value differs from old_system_config; if old_system_config is null, every mapped variant is.
// The keep_keys not in new_system_config keep their 3mf values.
// The variants are mapped twice, new -> 3mf and 3mf -> old system, both by the rules of "Variants and print_extruder_id" above.
// The 3mf and old_system_config may also have different ids, e.g. an old 3mf with wrong ids and the fixed preset.
// kept_keys: the keep_keys still different from new_system_config after merging, sorted.
// variant_index: the new -> 3mf mapping, for each variant of new_system_config the index of the 3mf variant, or -1.
// Pass it to update_values_from_multi_to_multi_2() for the object / part / layer range configs, so they follow the same rules as the process, e.g. multi -> single uses the master extruder.
// Returns 0 on success, or -1 on failure with full_config unchanged.
int merge_process_to_new_preset(DynamicPrintConfig          &full_config,
                                const DynamicPrintConfig    *old_system_config,
                                const DynamicPrintConfig    &new_system_config,
                                const std::set<std::string> &keep_keys,
                                std::vector<std::string>    &kept_keys,
                                std::vector<int>            &variant_index);

// User filament presets can hold "nil" in nullable options: a preset with a parent stores nil for
// variants equal to the parent, and root presets saved by older versions hold nil for every nullable
// option. Fill those slots from the parent, or from the option default when the parent has no value.
// Extruder override keys keep nil, which means "use the printer value".
// parent_config: the system parent preset, or null if the preset has no parent or the parent can not be loaded.
void fill_nil_filament_values(DynamicPrintConfig &config, const DynamicPrintConfig *parent_config);

// Bring a user filament preset up to date with its system parent before its values are used, in place.
// inherits: the system parent of the preset, read from profiles/BBL/filament_full; empty for a root preset.
// load_filament_preset: loads a preset file into the config, returns false if it is not a valid filament preset.
//   1. The variant options are extended to the parent's length with the parent values (set_with_default).
//   2. The nil values are filled by fill_nil_filament_values(), with the parent if it can be loaded.
void resolve_user_filament_config(DynamicPrintConfig &config, const std::string &inherits,
                                  const std::function<bool(const std::string &file, DynamicPrintConfig &config)> &load_filament_preset);

//======================================================================================================================
// filament_colour
//
// filament_colour must be "#RRGGBB" or "#RRGGBBAA". The CLI writes it as is to slice_info.config / plate_x.json,
// and computes the flush volumes from it.
// Bambu Studio always saves hex values, but some 3mfs have been edited by other tools and hold colour names, e.g. "Black".
// The CLI only rejects invalid values when publishing (slicing all plates, plate_to_slice == 0),
// so the 3mfs already online can still be sliced.
//   3mf:          an empty value is invalid.
//   command line: an empty value keeps the colour of the 3mf.
//======================================================================================================================

// Whether colour is "#RRGGBB" or "#RRGGBBAA".
bool is_hex_colour(const std::string &colour);

// Returns the index of the first value that is not hex (or is empty, unless allow_empty), or -1 if all are valid.
int find_invalid_filament_colour(const std::vector<std::string> &colours, bool allow_empty);

} // namespace Slic3r

#endif /* slic3r_CLIUtils_hpp_ */
