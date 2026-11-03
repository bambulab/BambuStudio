#include <cassert>

#include "PresetBundle.hpp"
#include "Semver.hpp"
#include "FilamentMixer.hpp"
#include "nlohmann/json.hpp"
#include "libslic3r.h"
#include "I18N.hpp"
#include "Utils.hpp"
#include "LocalesUtils.hpp"
#include "Model.hpp"
#include "SupportRecommendedParams.hpp"
#include "format.hpp"

#include <algorithm>
#include <atomic>
#include <set>
#include <fstream>
#include <unordered_set>
#include <boost/filesystem.hpp>
#include <boost/algorithm/clamp.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/range/adaptor/transformed.hpp>
#include <boost/nowide/cenv.hpp>
#include <boost/nowide/cstdio.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/locale.hpp>
#include <miniz/miniz.h>
#include <mutex>
#include <tbb/parallel_for.h>
#include "libslic3r/Log.hpp"

#define PARALLEL_LOAD_PRESET 1

#ifndef NDEBUG
#define ASSERT_ONCE(expr) do { static std::atomic_bool assertion_fired{ false }; if (!(expr) && !assertion_fired.exchange(true)) { assert(expr); } } while (false)
#else
#define ASSERT_ONCE(expr) ((void)0)
#endif

// Mark string for localization and translate.
#define L(s) Slic3r::I18N::translate(s)

// Store the print/filament/printer presets into a "presets" subdirectory of the Slic3rPE config dir.
// This breaks compatibility with the upstream Slic3r if the --datadir is used to switch between the two versions.
//#define SLIC3R_PROFILE_USE_PRESETS_SUBDIR

namespace Slic3r {

// Settles a slot's filament_volume_map entry onto a flow the slot's current filament preset can
// actually use at its current (or, failing that, first available) diameter. Called wherever a slot's
// filament preset is substituted with no user intent behind the substitution -- see the callers for
// why each one qualifies.
static void settle_filament_slot_flow(PresetBundle &bundle, size_t idx);

static std::vector<std::string> s_project_options {
    "flush_volumes_vector",
    "flush_volumes_matrix",
    // BBS
    "filament_colour",
    "filament_colour_type",
    "filament_multi_colour",
    "wipe_tower_x",
    "wipe_tower_y",
    "wipe_tower_rotation_angle",
    "curr_bed_type",
    "flush_multiplier",
    "flush_multiplier_fast",
    "nozzle_volume_type",
    "filament_map_mode",
    "filament_map",
    "filament_volume_map",
    "filament_nozzle_map",
    "filament_diameter_map",
    "multi_nozzle_diameter_mode",
    "extruder_nozzle_stats",
    "extruder_nozzle_stats_new",
    "prime_volume_mode",
    "extruder_nozzle_count",
    "extruder_nozzle_volume_type",
    "extruder_nozzle_diameter",
    "extruder_nozzle_slot",
    "enable_filament_dynamic_map",
    "filament_is_mixed",
    "filament_mixed_components",
    "filament_mixed_sublayer_ratios",
    "filament_mixed_gradient",
    "filament_mixed_gradient_range",
    "filament_mixed_gradient_curve",
    "filament_mixed_gradient_per_part",
    "has_filament_switcher"
};

//BBS: add BBL as default
const char *PresetBundle::BBL_BUNDLE = "BBL";
const char *PresetBundle::BBL_DEFAULT_PRINTER_MODEL = "Bambu Lab X1 Carbon";
const char *PresetBundle::BBL_DEFAULT_PRINTER_VARIANT = "0.4";
const char *PresetBundle::BBL_DEFAULT_FILAMENT = "Generic PLA";

DynamicPrintConfig PresetBundle::construct_full_config(
    Preset& in_printer_preset,
    Preset& in_print_preset,
    const DynamicPrintConfig& project_config,
    std::vector<Preset>& in_filament_presets,
    bool apply_extruder,
    std::optional<std::vector<int>> filament_maps_new,
    std::optional<std::vector<int>> filament_volume_maps_new)
{
    DynamicPrintConfig &printer_config = in_printer_preset.config;
    DynamicPrintConfig &print_config   = in_print_preset.config;

    DynamicPrintConfig out;
    out.apply(FullPrintConfig::defaults());
    out.apply(printer_config);
    out.apply(print_config);
    out.apply(project_config);
    out.apply(in_filament_presets[0].config);

    size_t num_filaments = in_filament_presets.size();

    std::vector<int> filament_maps = out.option<ConfigOptionInts>("filament_map")->values;
    std::vector<int> filament_volume_maps(num_filaments, (int)nvtStandard);

    ConfigOptionInts* filament_volume_map_opt = out.option<ConfigOptionInts>("filament_volume_map");
    if (filament_maps_new.has_value())
        filament_maps = *filament_maps_new;
    if (filament_volume_maps_new.has_value())
        filament_volume_maps = *filament_volume_maps_new;
    else if (filament_volume_map_opt && filament_volume_map_opt->values.size() == num_filaments)
        filament_volume_maps = filament_volume_map_opt->values;

    // in some middle state, they may be different
    if (filament_maps.size() != num_filaments) {
        filament_maps.resize(num_filaments, 1);
    }
    if (filament_volume_maps.size() != num_filaments) {
        filament_volume_maps.resize(num_filaments, nvtStandard);
    }

    auto *extruder_diameter = dynamic_cast<const ConfigOptionFloatsNullable *>(out.option("nozzle_diameter"));
    // Collect the "compatible_printers_condition" and "inherits" values over all presets (print, filaments, printers) into a single vector.
    std::vector<std::string> compatible_printers_condition;
    std::vector<std::string> compatible_prints_condition;
    std::vector<std::string> inherits;
    std::vector<std::string> filament_ids;
    std::vector<std::string> print_compatible_printers;
    // BBS: add logic for settings check between different system presets
    std::vector<std::string> different_settings;
    std::string              different_print_settings, different_printer_settings;
    compatible_printers_condition.emplace_back(in_print_preset.compatible_printers_condition());

    const ConfigOptionStrings *compatible_printers = print_config.option<ConfigOptionStrings>("compatible_printers", false);
    if (compatible_printers) print_compatible_printers = compatible_printers->values;
    // BBS: add logic for settings check between different system presets
    std::string print_inherits = in_print_preset.inherits();
    inherits.emplace_back(print_inherits);

    //BBS: update printer config related with variants
    std::vector<std::vector<NozzleVolumeType>> nozzle_volume_types;
    int extruder_count = 1, extruder_volume_type_count = 1;
    bool different_extruder = false;
    if (apply_extruder) {
        different_extruder = out.support_different_extruders(extruder_count);
        extruder_volume_type_count = out.get_extruder_nozzle_volume_count(extruder_count, nozzle_volume_types);

        if ((extruder_count > 1) || different_extruder) {
            out.update_values_to_printer_extruders(out, extruder_count, extruder_volume_type_count, nozzle_volume_types, printer_options_with_variant_2, "printer_extruder_id", "printer_extruder_variant", 2);
            out.update_values_to_printer_extruders(out, extruder_count, extruder_volume_type_count, nozzle_volume_types, printer_options_with_variant_1, "printer_extruder_id", "printer_extruder_variant");
            // update print config related with variants
            out.update_values_to_printer_extruders(out, extruder_count, extruder_volume_type_count, nozzle_volume_types, print_options_with_variant, "print_extruder_id", "print_extruder_variant");
        }
    }

    if (num_filaments <= 1) {
        // BBS: update filament config related with variants
        DynamicPrintConfig filament_config = in_filament_presets[0].config;
        if (apply_extruder && ((extruder_count > 1) || different_extruder))
            filament_config.update_values_to_printer_extruders(out, extruder_count, extruder_volume_type_count, nozzle_volume_types, filament_options_with_variant, "", "filament_extruder_variant", 1, filament_maps[0], (NozzleVolumeType)filament_volume_maps[0]);
        out.apply(filament_config);
        compatible_printers_condition.emplace_back(in_filament_presets[0].compatible_printers_condition());
        compatible_prints_condition.emplace_back(in_filament_presets[0].compatible_prints_condition());
        std::string filament_inherits = in_filament_presets[0].inherits();
        inherits.emplace_back(filament_inherits);
        filament_ids.emplace_back(in_filament_presets[0].filament_id);

        std::vector<int> &filament_self_indice = out.option<ConfigOptionInts>("filament_self_index", true)->values;
        int               index_size           = out.option<ConfigOptionStrings>("filament_extruder_variant")->size();
        filament_self_indice.resize(index_size, 1);
    } else {
        std::vector<const DynamicPrintConfig *> filament_configs;
        std::vector<const Preset *>             filament_presets;
        for (const Preset & preset : in_filament_presets) {
            filament_presets.emplace_back(&preset);
            filament_configs.emplace_back(&(preset.config));
        }

        std::vector<DynamicPrintConfig> filament_temp_configs;
        filament_temp_configs.resize(num_filaments);
        for (size_t i = 0; i < num_filaments; ++i) {
            filament_temp_configs[i] = *(filament_configs[i]);
            if (apply_extruder && ((extruder_count > 1) || different_extruder))
                filament_temp_configs[i].update_values_to_printer_extruders(out, extruder_count, extruder_volume_type_count, nozzle_volume_types, filament_options_with_variant, "", "filament_extruder_variant", 1, filament_maps[i], (NozzleVolumeType)filament_volume_maps[i]);
        }

        // loop through options and apply them to the resulting config.
        std::vector<int> filament_variant_count(num_filaments, 1);
        for (const t_config_option_key &key : in_filament_presets[0].config.keys()) {
            if (key == "compatible_prints" || key == "compatible_printers") continue;
            // Get a destination option.
            ConfigOption *opt_dst = out.option(key, false);
            if (opt_dst->is_scalar()) {
                // Get an option, do not create if it does not exist.
                const ConfigOption *opt_src = filament_temp_configs.front().option(key);
                if (opt_src != nullptr) opt_dst->set(opt_src);
            } else {
                // BBS
                ConfigOptionVectorBase *opt_vec_dst = static_cast<ConfigOptionVectorBase *>(opt_dst);
                {
                    if (apply_extruder) {
                        std::vector<const ConfigOption *> filament_opts(num_filaments, nullptr);
                        // Setting a vector value from all filament_configs.
                        for (size_t i = 0; i < filament_opts.size(); ++i) filament_opts[i] = filament_temp_configs[i].option(key);
                        opt_vec_dst->set(filament_opts);
                    } else {
                        for (size_t i = 0; i < num_filaments; ++i) {
                            const ConfigOptionVectorBase *filament_option = static_cast<const ConfigOptionVectorBase *>(filament_temp_configs[i].option(key));
                            if (i == 0)
                                opt_vec_dst->set(filament_option);
                            else
                                opt_vec_dst->append(filament_option);

                            if (key == "filament_extruder_variant") filament_variant_count[i] = filament_option->size();
                        }
                    }
                }
            }
        }

        if (apply_extruder) {
            std::vector<int> &filament_self_indice = out.option<ConfigOptionInts>("filament_self_index", true)->values;
            int               index_size           = out.option<ConfigOptionStrings>("filament_extruder_variant")->size();
            filament_self_indice.resize(index_size, 1);
            for (size_t i = 0; i < num_filaments && i < filament_self_indice.size(); i++) {
                filament_self_indice[i] = int(i + 1);
            }
        } else {
            // append filament_self_index
            std::vector<int> &filament_self_indice = out.option<ConfigOptionInts>("filament_self_index", true)->values;
            int               index_size           = out.option<ConfigOptionStrings>("filament_extruder_variant")->size();
            filament_self_indice.resize(index_size, 1);
            int k = 0;
            for (size_t i = 0; i < num_filaments; i++) {
                for (size_t j = 0; j < filament_variant_count[i]; j++) { filament_self_indice[k++] = i + 1; }
            }
        }
    }

    // These value types clash between the print and filament profiles. They should be renamed.
    out.erase("compatible_prints");
    out.erase("compatible_prints_condition");
    out.erase("compatible_printers");
    out.erase("compatible_printers_condition");
    out.erase("inherits");
    // BBS: add logic for settings check between different system presets
    out.erase("different_settings_to_system");

    static const char *keys[] = {"support_filament", "support_interface_filament"};
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        std::string key = std::string(keys[i]);
        auto       *opt = dynamic_cast<ConfigOptionInt *>(out.option(key, false));
        assert(opt != nullptr);
        opt->value = boost::algorithm::clamp<int>(opt->value, 0, int(num_filaments));
    }

    std::vector<std::string> filamnet_preset_names;
    for (auto preset : in_filament_presets) {
        filamnet_preset_names.emplace_back(preset.name);
    }
    out.option<ConfigOptionString>("print_settings_id", true)->value      = in_print_preset.name;
    out.option<ConfigOptionStrings>("filament_settings_id", true)->values = filamnet_preset_names;
    out.option<ConfigOptionString>("printer_settings_id", true)->value    = in_printer_preset.name;
    out.option<ConfigOptionStrings>("filament_ids", true)->values         = filament_ids;
    out.option<ConfigOptionInts>("filament_map", true)->values            = filament_maps;
    out.option<ConfigOptionInts>("filament_volume_map", true)->values     = filament_volume_maps;

    auto add_if_some_non_empty = [&out](std::vector<std::string> &&values, const std::string &key) {
        bool nonempty = false;
        for (const std::string &v : values)
            if (!v.empty()) {
                nonempty = true;
                break;
            }
        if (nonempty) out.set_key_value(key, new ConfigOptionStrings(std::move(values)));
    };
    add_if_some_non_empty(std::move(compatible_printers_condition), "compatible_machine_expression_group");
    add_if_some_non_empty(std::move(compatible_prints_condition), "compatible_process_expression_group");
    add_if_some_non_empty(std::move(inherits), "inherits_group");
    // BBS: add logic for settings check between different system presets
    //add_if_some_non_empty(std::move(different_settings), "different_settings_to_system");
    add_if_some_non_empty(std::move(print_compatible_printers), "print_compatible_printers");

    out.option<ConfigOptionEnumGeneric>("printer_technology", true)->value = ptFFF;
    return out;
}

int ExtruderNozzleStat::get_extruder_nozzle_count(int extruder_id, std::optional<NozzleVolumeType> volume_type) const
{
    if(extruder_id<0 || extruder_id >= extruder_nozzle_counts.size())
        return 0;
    if (!volume_type.has_value() || volume_type == NozzleVolumeType::nvtHybrid)
        return std::accumulate(extruder_nozzle_counts[extruder_id].begin(), extruder_nozzle_counts[extruder_id].end(), 0,
            [](int sum, const std::pair<NozzleVolumeType, int>& p) { return sum + p.second; });

    auto iter = extruder_nozzle_counts[extruder_id].find(*volume_type);
    if(iter == extruder_nozzle_counts[extruder_id].end())
        return 0;
    return iter->second;
}

void ExtruderNozzleStat::on_printer_model_change_cli(const std::vector<int>& nozzle_volume_type, const std::vector<int>& max_nozzle_count)
{
    extruder_nozzle_counts.resize(max_nozzle_count.size());
    for (size_t eid = 0; eid < extruder_nozzle_counts.size(); ++eid) {
        NozzleVolumeType type = nvtStandard;
        if (eid >= nozzle_volume_type.size())
            BOOST_LOG_TRIVIAL(error) << boost::format("eid out of bounds, use standard flow");
        else
            type = NozzleVolumeType(nozzle_volume_type[eid]);
        set_extruder_nozzle_count(eid, type, max_nozzle_count[eid], true);
    }
}


void ExtruderNozzleStat::on_volume_type_switch(int extruder_id, NozzleVolumeType type)
{
    if (type != nvtHybrid) {
        int current_count = get_extruder_nozzle_count(extruder_id, type);
        if (extruder_id >= extruder_nozzle_counts.size()) {
            extruder_nozzle_counts.resize(extruder_id + 1);
        }
        extruder_nozzle_counts[extruder_id].clear();
        extruder_nozzle_counts[extruder_id][type] = current_count;
    }
}

void ExtruderNozzleStat::set_extruder_nozzle_count(int extruder_id, NozzleVolumeType type, int count, bool clear)
{
    if (extruder_id >= extruder_nozzle_counts.size())
        extruder_nozzle_counts.resize(extruder_id + 1);
    if(clear)
        extruder_nozzle_counts[extruder_id].clear();
    extruder_nozzle_counts[extruder_id][type] = count;
}


PresetBundle::PresetBundle()
    : prints(Preset::TYPE_PRINT, Preset::print_options(), static_cast<const PrintRegionConfig &>(FullPrintConfig::defaults()))
    , filaments(Preset::TYPE_FILAMENT, Preset::filament_options(), static_cast<const PrintRegionConfig &>(FullPrintConfig::defaults()), "Default Filament")
    , sla_materials(Preset::TYPE_SLA_MATERIAL, Preset::sla_material_options(), static_cast<const SLAMaterialConfig &>(SLAFullPrintConfig::defaults()))
    , sla_prints(Preset::TYPE_SLA_PRINT, Preset::sla_print_options(), static_cast<const SLAPrintObjectConfig &>(SLAFullPrintConfig::defaults()))
    , printers(Preset::TYPE_PRINTER, Preset::printer_options(), static_cast<const PrintRegionConfig &>(FullPrintConfig::defaults()), "Default Printer")
    , physical_printers(PhysicalPrinter::printer_options())
{
    // The following keys are handled by the UI, they do not have a counterpart in any StaticPrintConfig derived classes,
    // therefore they need to be handled differently. As they have no counterpart in StaticPrintConfig, they are not being
    // initialized based on PrintConfigDef(), but to empty values (zeros, empty vectors, empty strings).
    //
    // "compatible_printers", "compatible_printers_condition", "inherits",
    // "print_settings_id", "filament_settings_id", "printer_settings_id", "printer_settings_id"
    // "printer_model", "printer_variant", "default_print_profile", "default_filament_profile"

    // Create the ID config keys, as they are not part of the Static print config classes.
    this->prints.default_preset().config.optptr("print_settings_id", true);
    this->prints.default_preset().compatible_printers_condition();
    this->prints.default_preset().inherits();

    this->filaments.default_preset().config.option<ConfigOptionStrings>("filament_settings_id", true)->values = {""};
    this->filaments.default_preset().compatible_printers_condition();
    this->filaments.default_preset().inherits();
    // Set all the nullable values to nils.
    {
        auto& default_config = this->filaments.default_preset().config;
        for(const std::string& opt_key : default_config.keys()){
            ConfigOption* opt = default_config.optptr(opt_key, false);
            bool is_override_key = is_filament_extruder_override_key(opt_key);
            if(!is_override_key || !opt->nullable())
                continue;
            opt->deserialize("nil",ForwardCompatibilitySubstitutionRule::Disable);
        }
    }

    this->sla_materials.default_preset().config.optptr("sla_material_settings_id", true);
    this->sla_materials.default_preset().compatible_printers_condition();
    this->sla_materials.default_preset().inherits();

    this->sla_prints.default_preset().config.optptr("sla_print_settings_id", true);
    this->sla_prints.default_preset().config.opt_string("filename_format", true) = "[input_filename_base].sl1";
    this->sla_prints.default_preset().compatible_printers_condition();
    this->sla_prints.default_preset().inherits();

    //this->printers.add_default_preset(Preset::sla_printer_options(), static_cast<const SLAMaterialConfig &>(SLAFullPrintConfig::defaults()), "- default SLA -");
    //this->printers.preset(1).printer_technology_ref() = ptSLA;
    for (size_t i = 0; i < 1; ++i) {
        // The following ugly switch is to avoid printers.preset(0) to return the edited instance, as the 0th default is the current one.
        Preset &preset = this->printers.default_preset(i);
        for (const char *key : {"printer_settings_id", "printer_model", "printer_variant","thumbnail_size"}) preset.config.optptr(key, true);
        //if (i == 0) {
            preset.config.optptr("default_print_profile", true);
            preset.config.option<ConfigOptionStrings>("default_filament_profile", true);
        //} else {
        //    preset.config.optptr("default_sla_print_profile", true);
        //    preset.config.optptr("default_sla_material_profile", true);
        //}
        // default_sla_material_profile
        preset.inherits();
    }

    // Re-activate the default presets, so their "edited" preset copies will be updated with the additional configuration values above.
    this->prints.select_preset(0);
    this->sla_prints.select_preset(0);
    this->filaments.select_preset(0);
    this->sla_materials.select_preset(0);
    this->printers.select_preset(0);

    this->project_config.apply_only(FullPrintConfig::defaults(), s_project_options);
    // This is a project-only option, intentionally kept out of the static PrintConfig layout.
    this->project_config.set_key_value("multi_nozzle_diameter_mode", new ConfigOptionBool(false));
}

PresetBundle::PresetBundle(const PresetBundle &rhs)
{
    *this = rhs;
}

PresetBundle& PresetBundle::operator=(const PresetBundle &rhs)
{
    prints              = rhs.prints;
    sla_prints          = rhs.sla_prints;
    filaments           = rhs.filaments;
    sla_materials       = rhs.sla_materials;
    printers            = rhs.printers;
    physical_printers   = rhs.physical_printers;

    filament_presets    = rhs.filament_presets;
    project_config      = rhs.project_config;
    vendors             = rhs.vendors;
    obsolete_presets    = rhs.obsolete_presets;
    extruder_ams_counts     = rhs.extruder_ams_counts;
    extruder_ams_selections = rhs.extruder_ams_selections;
    nozzle_inventory        = rhs.nozzle_inventory;

    // Adjust Preset::vendor pointers to point to the copied vendors map.
    prints       .update_vendor_ptrs_after_copy(this->vendors);
    sla_prints   .update_vendor_ptrs_after_copy(this->vendors);
    filaments    .update_vendor_ptrs_after_copy(this->vendors);
    sla_materials.update_vendor_ptrs_after_copy(this->vendors);
    printers     .update_vendor_ptrs_after_copy(this->vendors);

    return *this;
}

void PresetBundle::reset(bool delete_files)
{
    // Clear the existing presets, delete their respective files.
    this->vendors.clear();
    this->prints       .reset(delete_files);
    this->sla_prints   .reset(delete_files);
    this->filaments    .reset(delete_files);
    this->sla_materials.reset(delete_files);
    this->printers     .reset(delete_files);
    // BBS: filament_presets is load from project config, not handled here
    //this->filament_presets.clear();
    if (this->filament_presets.empty())
        this->filament_presets.emplace_back(this->filaments.get_selected_preset_name());
    this->obsolete_presets.prints.clear();
    this->obsolete_presets.sla_prints.clear();
    this->obsolete_presets.filaments.clear();
    this->obsolete_presets.sla_materials.clear();
    this->obsolete_presets.printers.clear();
    this->extruder_ams_selections.clear();
}

void PresetBundle::setup_directories()
{
    boost::filesystem::path data_dir = boost::filesystem::path(Slic3r::data_dir());
    //BBS: change directoties by design
    std::initializer_list<boost::filesystem::path> paths = {
        data_dir,
        data_dir / "ota",
		data_dir / PRESET_SYSTEM_DIR,
        data_dir / PRESET_USER_DIR,
        // Store the print/filament/printer presets at the same location as the upstream Slic3r.
        //data_dir / PRESET_SYSTEM_DIR / PRESET_PRINT_NAME,
        //data_dir / PRESET_SYSTEM_DIR / PRESET_FILAMENT_NAME,
        //data_dir / PRESET_SYSTEM_DIR / PRESET_PRINTER_NAME
    };
    for (const boost::filesystem::path &path : paths) {
		boost::filesystem::path subdir = path;
        subdir.make_preferred();
        if (! boost::filesystem::is_directory(subdir) &&
            ! boost::filesystem::create_directory(subdir)) {
            if (boost::filesystem::is_directory(subdir)) {
                BOOST_LOG_TRIVIAL(warning) << boost::format("creating directory %1% failed, maybe created by other instance, go on!")%subdir.string();
            }
            else
                throw Slic3r::RuntimeError(std::string("Unable to create directory ") + subdir.string());
        }
    }
}

// recursively copy all files and dirs in from_dir to to_dir
static void copy_dir(const boost::filesystem::path& from_dir, const boost::filesystem::path& to_dir)
{
    if(!boost::filesystem::is_directory(from_dir))
        return;
    // i assume to_dir.parent surely exists
    if (!boost::filesystem::is_directory(to_dir))
        boost::filesystem::create_directory(to_dir);
    for (auto& dir_entry : boost::filesystem::directory_iterator(from_dir)) {
        if (!boost::filesystem::is_directory(dir_entry.path())) {
            std::string em;
            CopyFileResult cfr = copy_file(dir_entry.path().string(), (to_dir / dir_entry.path().filename()).string(), em, false);
            if (cfr != SUCCESS) {
                BOOST_LOG_TRIVIAL(error) << "Error when copying files from " << from_dir << " to " << to_dir << ": " << em;
            }
        } else {
            copy_dir(dir_entry.path(), to_dir / dir_entry.path().filename());
        }
    }
}

void PresetBundle::copy_files(const std::string& from)
{
    boost::filesystem::path data_dir = boost::filesystem::path(Slic3r::data_dir());
    // list of searched paths based on current directory system in setup_directories()
    // do not copy cache and snapshots
    boost::filesystem::path from_data_dir = boost::filesystem::path(from);
    //BBS: change directoties by design
    std::initializer_list<boost::filesystem::path> from_dirs= {
        //from_data_dir / "vendor",
        // Store the print/filament/printer presets at the same location as the upstream Slic3r.
        from_data_dir / PRESET_PRINT_NAME,
        from_data_dir / PRESET_FILAMENT_NAME,
        from_data_dir / PRESET_PRINTER_NAME
    };
    // copy recursively all files
    //BBS: change directoties by design
    for (const boost::filesystem::path& from_dir : from_dirs) {
        copy_dir(from_dir, data_dir /"old"/from_dir.filename());
    }
}

std::pair<PresetsConfigSubstitutions, std::string> PresetBundle::load_presets(AppConfig &config, ForwardCompatibilitySubstitutionRule substitution_rule,
                                                      const PresetPreferences& preferred_selection/* = PresetPreferences()*/)
{
    // First load the vendor specific system presets.
    PresetsConfigSubstitutions substitutions;
    std::string errors_cummulative;

    //BBS: add config related logs
    BOOST_LOG_TRIVIAL(info) << boost::format("enter, substitution_rule %1%, preferred printer_model_id %2%")%substitution_rule%preferred_selection.printer_model_id;
    //BBS: change system config to json
    std::tie(substitutions, errors_cummulative) = this->load_system_presets_from_json(substitution_rule);

    // BBS load preset from user's folder, load system default if
    // BBS: change directories by design
    std::string dir_user_presets = config.get("preset_folder");
    if (dir_user_presets.empty()) {
        load_user_presets(DEFAULT_USER_FOLDER_NAME, substitution_rule);
    } else {
        load_user_presets(dir_user_presets, substitution_rule);
    }

    this->update_multi_material_filament_presets();
    this->update_compatible(PresetSelectCompatibleType::Never);

    this->load_selections(config, preferred_selection);

    set_calibrate_printer("");

    // Load support recommended params from JSON
    load_support_recommended_params();

    //BBS: add config related logs
    BOOST_LOG_TRIVIAL(info) << boost::format("finished, returned substitutions %1%")%substitutions.size();

    return std::make_pair(std::move(substitutions), errors_cummulative);
}

//BBS: add function to generate differed preset for save
//the pointer should be freed by the caller
Preset* PresetBundle::get_preset_differed_for_save(Preset& preset)
{
    PresetCollection* preset_collection;

    switch(preset.type) {
        case Preset::TYPE_PRINT:
            preset_collection = &(this->prints);
            break;
        case Preset::TYPE_PRINTER:
            preset_collection = &(this->printers);
            break;
        case Preset::TYPE_FILAMENT:
            preset_collection = &(this->filaments);
            break;
        default:
            BOOST_LOG_TRIVIAL(info) << boost::format("invalid type %1%, return directly")%preset.type;
            return nullptr;
    }

    return preset_collection->get_preset_differed_for_save(preset);
}

int PresetBundle::get_differed_values_to_update(Preset& preset, std::map<std::string, std::string>& key_values)
{
    PresetCollection* preset_collection;

    switch(preset.type) {
        case Preset::TYPE_PRINT:
            preset_collection = &(this->prints);
            break;
        case Preset::TYPE_PRINTER:
            preset_collection = &(this->printers);
            break;
        case Preset::TYPE_FILAMENT:
            preset_collection = &(this->filaments);
            break;
        default:
            BOOST_LOG_TRIVIAL(info) << boost::format("invalid type %1%, return directly")%preset.type;
            return -1;
    }

    return preset_collection->get_differed_values_to_update(preset, key_values);
}

//BBS: get vendor's current version
Semver PresetBundle::get_vendor_profile_version(std::string vendor_name)
{
    Semver result_ver;

    auto vendor_profile = vendors.find(vendor_name);
    if (vendor_profile != vendors.end()) {
        result_ver = vendor_profile->second.config_version;
    }

    return result_ver;
}

std::optional<FilamentBaseInfo> PresetBundle::get_filament_by_filament_id(const std::string& filament_id, const std::string& printer_name, bool only_system) const
{
    if (filament_id.empty())
        return std::nullopt;

    // basic filament info should be same in the parent preset and child preset
    // so just match the filament id is enough

    for (auto iter = filaments.begin(); iter != filaments.end(); ++iter) {
        const Preset& filament_preset = *iter;
        const auto& config = filament_preset.config;
        if (filament_preset.filament_id == filament_id) {
            if (only_system && !filament_preset.is_system)
                continue;
            FilamentBaseInfo info;
            info.filament_id = filament_id;
            info.is_system = filament_preset.is_system;
            info.filament_name = filament_preset.alias;
            if (config.has("filament_is_support"))
                info.is_support = config.option<ConfigOptionBools>("filament_is_support")->values[0];
            if (config.has("filament_type"))
                info.filament_type = config.option<ConfigOptionStrings>("filament_type")->values[0];
            if (config.has("filament_vendor"))
                info.vendor = config.option<ConfigOptionStrings>("filament_vendor")->values[0];
            if (config.has("nozzle_temperature_range_high"))
                info.nozzle_temp_range_high = config.option<ConfigOptionInts>("nozzle_temperature_range_high")->values[0];
            if (config.has("nozzle_temperature_range_low"))
                info.nozzle_temp_range_low = config.option<ConfigOptionInts>("nozzle_temperature_range_low")->values[0];
            if(config.has("temperature_vitrification"))
                info.temperature_vitrification = config.option<ConfigOptionInts>("temperature_vitrification")->values[0];

            info.setting_id = filament_preset.setting_id;
            if (!printer_name.empty()) {
                std::vector<std::string> compatible_printers = config.option<ConfigOptionStrings>("compatible_printers")->values;
                auto iter = std::find(compatible_printers.begin(), compatible_printers.end(), printer_name);
                if (iter != compatible_printers.end() && config.has("filament_printable")) {
                    info.filament_printable = config.option<ConfigOptionInts>("filament_printable")->values[0];
                    if (config.has("filament_extruder_compatibility"))
                        info.set_filament_extruder_compatibility(config.option<ConfigOptionInts>("filament_extruder_compatibility")->values[0]);
                    return info;
                }
            }
            else {
                return info;
            }
        }
    }
    return std::nullopt;
}

//BBS: load project embedded presets
PresetsConfigSubstitutions PresetBundle::load_project_embedded_presets(std::vector<Preset*> project_presets, ForwardCompatibilitySubstitutionRule substitution_rule)
{
    // First load the vendor specific system presets.
    PresetsConfigSubstitutions substitutions;
    std::string errors_cummulative;

    BOOST_LOG_TRIVIAL(debug) << boost::format("enter, substitution_rule %1%, preset toltal count %2%")%substitution_rule% project_presets.size();
    try {
        this->prints.load_project_embedded_presets(project_presets, PRESET_PRINT_NAME, substitutions, substitution_rule);
    } catch (const std::runtime_error &err) {
        errors_cummulative += err.what();
    }
    try {
        this->filaments.load_project_embedded_presets(project_presets, PRESET_FILAMENT_NAME, substitutions, substitution_rule);
    } catch (const std::runtime_error &err) {
        errors_cummulative += err.what();
    }
    try {
        this->printers.load_project_embedded_presets(project_presets, PRESET_PRINTER_NAME, substitutions, substitution_rule);
    } catch (const std::runtime_error &err) {
        errors_cummulative += err.what();
    }

    //this->update_multi_material_filament_presets();
    //this->update_compatible(PresetSelectCompatibleType::Never);
    if (! errors_cummulative.empty())
        throw Slic3r::RuntimeError(errors_cummulative);

    //this->load_selections(config, "");

    BOOST_LOG_TRIVIAL(debug) << boost::format("finished, returned substitutions %1%")%substitutions.size();
    return substitutions;
}

//BBS: get current project embedded presets
std::vector<Preset*> PresetBundle::get_current_project_embedded_presets()
{
    std::vector<Preset*> project_presets;

    project_presets = this->prints.get_project_embedded_presets();

    auto filament_presets = this->filaments.get_project_embedded_presets();
    if (!filament_presets.empty())
        std::copy(filament_presets.begin(), filament_presets.end(), std::back_inserter(project_presets));
    auto printer_presets = this->printers.get_project_embedded_presets();
    if (!printer_presets.empty())
        std::copy(printer_presets.begin(), printer_presets.end(), std::back_inserter(project_presets));

    BOOST_LOG_TRIVIAL(debug) << boost::format("finished, returned project_presets count %1%")%project_presets.size();
    return project_presets;
}

//BBS: reset project embedded presets
void PresetBundle::reset_project_embedded_presets()
{
    std::string prefer_printer;
    Preset& current_printer = this->printers.get_selected_preset();
    ConfigOption* inherits = current_printer.config.option("inherits");
    if (inherits) {
        prefer_printer = dynamic_cast<ConfigOptionString *>(inherits)->value;
    }
    //first printer, then filament, then print
    bool printer_reselect = this->printers.reset_project_embedded_presets();
    bool filament_reselect = this->filaments.reset_project_embedded_presets();
    bool print_reselect = this->prints.reset_project_embedded_presets();

    if (printer_reselect) {
        if (!prefer_printer.empty())
           this->printers.select_preset_by_name(prefer_printer, true);
        else
           this->printers.select_preset(this->printers.first_visible_idx());

        //this->update_multi_material_filament_presets();
        this->update_compatible(PresetSelectCompatibleType::Never);
    }
    else if (filament_reselect || print_reselect) {
        //Preset& current_printer = this->printers.get_selected_preset();
        /*if (filament_reselect) {
            const std::vector<std::string> &prefered_filament_profiles = current_printer.config.option<ConfigOptionStrings>("default_filament_profile")->values;
            const std::string prefered_filament_profile = prefered_filament_profiles.empty() ? std::string() : prefered_filament_profiles.front();
            if (!prefered_filament_profile.empty())
               this->filaments.select_preset_by_name(prefered_filament_profile, true);
            else
               this->filaments.select_preset(this->filaments.first_visible_idx());
        }

        if (print_reselect) {
        }*/
        this->update_compatible(PresetSelectCompatibleType::Never);
    }

    //this->update_multi_material_filament_presets();

    //update filament_presets
    for (size_t i = 0; i < filament_presets.size(); ++ i)
    {
        Preset* selected_filament = this->filaments.find_preset(filament_presets[i], false);
        if (!selected_filament) {
            //it should be the project embedded presets
            Preset& current_printer = this->printers.get_selected_preset();
            const std::vector<std::string> &prefered_filament_profiles = current_printer.config.option<ConfigOptionStrings>("default_filament_profile")->values;
            const std::string prefered_filament_profile = prefered_filament_profiles.empty() ? std::string() : prefered_filament_profiles.front();
            if (!prefered_filament_profile.empty())
                filament_presets[i] = prefered_filament_profile;
            else
            filament_presets[i] = this->filaments.first_visible().name;
            // The embedded preset this slot had is gone along with its project; nothing here is a
            // user choice to preserve, including whatever flow it happened to be sitting on, which
            // may not even exist for the filament that just replaced it (see settle_filament_slot_flow()).
            settle_filament_slot_flow(*this, i);
        }
    }
}

//BBS: get bed texture for printer model
std::string PresetBundle::get_texture_for_printer_model(std::string model_name)
{
    std::string texture_name, vendor_name, out;

    for (auto vendor_profile: this->vendors)
    {
        for (auto vendor_model: vendor_profile.second.models)
        {
            if (vendor_model.name == model_name)
            {
                texture_name = vendor_model.bed_texture;
                vendor_name = vendor_profile.first;
                break;
            }
        }
    }

    if (!texture_name.empty())
    {
        out = Slic3r::data_dir() + "/vendor/" + vendor_name + "/" + texture_name;
        if (!boost::filesystem::exists(boost::filesystem::path(out)))
            out = Slic3r::resources_dir() + "/profiles/" + vendor_name + "/" + texture_name;
    }

    return out;
}

//BBS: get stl model for printer model
std::string PresetBundle::get_stl_model_for_printer_model(std::string model_name)
{
    std::string stl_name, vendor_name, out;

    for (auto vendor_profile: this->vendors)
    {
        for (auto vendor_model: vendor_profile.second.models)
        {
            if (vendor_model.name == model_name)
            {
                stl_name = vendor_model.bed_model;
                vendor_name = vendor_profile.first;
                break;
            }
        }
    }

    if (!stl_name.empty())
    {
        out = Slic3r::data_dir() + "/vendor/" + vendor_name + "/" + stl_name;
        if (!boost::filesystem::exists(boost::filesystem::path(out)))
            out = Slic3r::resources_dir() + "/profiles/" + vendor_name + "/" + stl_name;
    }

    return out;
}

std::string PresetBundle::get_hotend_model_for_printer_model(std::string model_name)
{
    std::string hotend_stl, vendor_name, out;

    for (auto vendor_profile: this->vendors)
    {
        for (auto vendor_model: vendor_profile.second.models)
        {
            if (vendor_model.name == model_name)
            {
                hotend_stl = vendor_model.hotend_model;
                vendor_name = vendor_profile.first;
                break;
            }
        }
    }

    if (!hotend_stl.empty())
    {
        out = Slic3r::data_dir() + "/vendor/" + vendor_name + "/" + hotend_stl;
        if (!boost::filesystem::exists(boost::filesystem::path(out)))
            out = Slic3r::resources_dir() + "/profiles/" + vendor_name + "/" + hotend_stl;
    }

    return out;
}

PresetsConfigSubstitutions PresetBundle::load_user_presets(std::string user, ForwardCompatibilitySubstitutionRule substitution_rule)
{
    BOOST_LOG_TRIVIAL(info) << "entry and user is: " << user;
    PresetsConfigSubstitutions substitutions;
    std::string errors_cummulative;

    fs::path user_folder(data_dir() + "/" + PRESET_USER_DIR);
    if (!fs::exists(user_folder)) fs::create_directory(user_folder);

    std::string dir_user_presets = data_dir() + "/" + PRESET_USER_DIR + "/" + user;
    fs::path    folder(user_folder / user);
    if (!fs::exists(folder)) fs::create_directory(folder);

    // BBS do not load sla_print
    // BBS: change directoties by design
    try {
        std::string print_selected_preset_name = prints.get_selected_preset().name;
        this->prints.load_presets(dir_user_presets, PRESET_PRINT_NAME, substitutions, substitution_rule);
        prints.select_preset_by_name(print_selected_preset_name, false);
    } catch (const std::runtime_error &err) {
        errors_cummulative += err.what();
    }
    try {
        std::string filament_selected_preset_name = filaments.get_selected_preset().name;
        this->filaments.load_presets(dir_user_presets, PRESET_FILAMENT_NAME, substitutions, substitution_rule);
        filaments.select_preset_by_name(filament_selected_preset_name, false);
    } catch (const std::runtime_error &err) {
        errors_cummulative += err.what();
    }
    try {
        std::string printer_selected_preset_name = printers.get_selected_preset().name;
        this->printers.load_presets(dir_user_presets, PRESET_PRINTER_NAME, substitutions, substitution_rule);
        printers.select_preset_by_name(printer_selected_preset_name, false);
    } catch (const std::runtime_error &err) {
        errors_cummulative += err.what();
    }
    if (!errors_cummulative.empty()) throw Slic3r::RuntimeError(errors_cummulative);
    this->update_multi_material_filament_presets();
    this->update_compatible(PresetSelectCompatibleType::Never);

    set_calibrate_printer("");

    return PresetsConfigSubstitutions();
}

PresetsConfigSubstitutions PresetBundle::load_user_presets(AppConfig &                                                config,
                                                           std::map<std::string, std::map<std::string, std::string>> &my_presets,
                                                           ForwardCompatibilitySubstitutionRule                       substitution_rule)
{
    // First load the vendor specific system presets.
    PresetsConfigSubstitutions substitutions;
    std::string errors_cummulative;
    bool process_added = false, filament_added = false, machine_added = false;

    BOOST_LOG_TRIVIAL(info) << boost::format("enter, substitution_rule %1%, preset toltal count %2%")%substitution_rule%my_presets.size();
    BOOST_LOG_TRIVIAL(info) << boost::format("print's selected_idx %1%, selected_name %2%") %prints.get_selected_idx() %prints.get_selected_preset_name();
    BOOST_LOG_TRIVIAL(info) << boost::format("filament's selected_idx %1%, selected_name %2%") %filaments.get_selected_idx() %filaments.get_selected_preset_name();
    BOOST_LOG_TRIVIAL(info) << boost::format("printers's selected_idx %1%, selected_name %2%") %printers.get_selected_idx() %printers.get_selected_preset_name();

    // Sync removing
    remove_users_preset(config, &my_presets);

    std::map<std::string, std::map<std::string, std::string>>::iterator it;
    for (int pass = 0; pass < 2; ++pass)
    for (it = my_presets.begin(); it != my_presets.end(); it++) {
        std::string name = it->first;
        std::map<std::string, std::string>& value_map = it->second;
        // Load user root presets at first pass
        std::map<std::string, std::string>::iterator inherits_iter = value_map.find(BBL_JSON_KEY_INHERITS);
        if ((pass == 1) == (inherits_iter == value_map.end() || inherits_iter->second.empty()))
            continue;
        BOOST_LOG_TRIVIAL(info) << "start load from cloud: " << name;
        //get the type first
        std::map<std::string, std::string>::iterator type_iter = value_map.find(BBL_JSON_KEY_TYPE);
        if (type_iter == value_map.end()) {
            BOOST_LOG_TRIVIAL(warning) << boost::format("can not find type for setting %1%")%name;
            continue;
        }
        try {
            PresetCollection *preset_collection = nullptr;
            if (type_iter->second == PRESET_IOT_PRINT_TYPE) {
                preset_collection = &(this->prints);
                process_added |= preset_collection->load_user_preset(name, value_map, substitutions, substitution_rule);
            }
            else if (type_iter->second == PRESET_IOT_FILAMENT_TYPE) {
                preset_collection = &(this->filaments);
                filament_added |= preset_collection->load_user_preset(name, value_map, substitutions, substitution_rule);
            }
            else if (type_iter->second == PRESET_IOT_PRINTER_TYPE) {
                preset_collection = &(this->printers);
                machine_added |= preset_collection->load_user_preset(name, value_map, substitutions, substitution_rule);
            }
            else {
                BOOST_LOG_TRIVIAL(warning) << boost::format("invalid type %1% for setting %2%") %type_iter->second %name;
                continue;
            }
        }
        catch (const std::runtime_error& err) {
            errors_cummulative += err.what();
        }
    }
    /*if (process_added) {
        this->prints.update_after_user_presets_loaded();
    }
    if (filament_added) {
        this->filaments.update_after_user_presets_loaded();
    }
    if (machine_added) {
        this->printers.update_after_user_presets_loaded();
    }*/

    this->update_multi_material_filament_presets();
    this->update_compatible(PresetSelectCompatibleType::Never);
    //this->load_selections(config, PresetPreferences());

    set_calibrate_printer("");

    if (! errors_cummulative.empty())
        throw Slic3r::RuntimeError(errors_cummulative);

    BOOST_LOG_TRIVIAL(debug) << boost::format("finished, process_added %1%, filament_added %2%, machine_added %3%")%process_added %filament_added %machine_added;
    return substitutions;
}

PresetsConfigSubstitutions PresetBundle::import_presets(std::vector<std::string> &              files,
                                                        std::function<int(std::string const &)> override_confirm,
                                                        ForwardCompatibilitySubstitutionRule    rule)
{
    BOOST_LOG_TRIVIAL(info) << "entry";
    PresetsConfigSubstitutions substitutions;
    int overwrite = 0;
    std::vector<std::string>   result;
    for (auto &file : files) {
        if (Slic3r::is_json_file(file)) {
            import_json_presets(substitutions, file, override_confirm, rule, overwrite, result);
        }
        // Determine if it is a preset bundle
        if (boost::iends_with(file, ".bbscfg") || boost::iends_with(file, ".bbsflmt") || boost::iends_with(file, ".zip")) {
            boost::system::error_code ec;
            // create user folder
            fs::path user_folder(data_dir() + "/" + PRESET_USER_DIR);
            if (!fs::exists(user_folder)) fs::create_directory(user_folder, ec);
            if (ec) BOOST_LOG_TRIVIAL(error) << "create directory failed: " << ec.message();
            // create default folder
            fs::path default_folder(user_folder / DEFAULT_USER_FOLDER_NAME);
            if (!fs::exists(default_folder)) fs::create_directory(default_folder, ec);
            if (ec) BOOST_LOG_TRIVIAL(error) << "create directory failed: " << ec.message();
            //create temp folder
            //std::string user_default_temp_dir = data_dir() + "/" + PRESET_USER_DIR + "/" + DEFAULT_USER_FOLDER_NAME + "/" + "temp";
            fs::path temp_folder(default_folder / "temp");
            std::string user_default_temp_dir = temp_folder.make_preferred().string();
            if (fs::exists(temp_folder)) fs::remove_all(temp_folder);
            fs::create_directory(temp_folder, ec);
            if (ec) BOOST_LOG_TRIVIAL(error) << "create directory failed: " << ec.message();

            file = boost::filesystem::path(file).make_preferred().string();
            mz_zip_archive zip_archive;
            mz_zip_zero_struct(&zip_archive);
            mz_bool status;

            /*if (!open_zip_reader(&zip_archive, file)) {
                BOOST_LOG_TRIVIAL(info) << "Failed to initialize reader ZIP archive";
                return substitutions;
            } else {
                BOOST_LOG_TRIVIAL(info) << "Success to initialize reader ZIP archive";
            }*/

            FILE *zipFile = boost::nowide::fopen(file.c_str(), "rb");
            status        = mz_zip_reader_init_cfile(&zip_archive, zipFile, 0, MZ_ZIP_FLAG_CASE_SENSITIVE | MZ_ZIP_FLAG_IGNORE_PATH);
            if (MZ_FALSE == status) {
                BOOST_LOG_TRIVIAL(info) << "Failed to initialize reader ZIP archive";
                return substitutions;
            } else {
                BOOST_LOG_TRIVIAL(info) << "Success to initialize reader ZIP archive";
            }

            // Extract Files
            int num_files = mz_zip_reader_get_num_files(&zip_archive);
            for (int i = 0; i < num_files; i++) {
                mz_zip_archive_file_stat file_stat;
                status = mz_zip_reader_file_stat(&zip_archive, i, &file_stat);
                if (status) {
                    std::string file_name = file_stat.m_filename;
                    BOOST_LOG_TRIVIAL(info) << "Form zip file: " << file << ". Read file name: " << file_stat.m_filename;
                    size_t index = file_name.find_last_of("/\\");
                    if (std::string::npos != index) {
                        file_name = file_name.substr(index + 1);
                    }
                    if (BUNDLE_STRUCTURE_JSON_NAME == file_name) continue;
                    // create target file path
                    std::string target_file_path = boost::filesystem::path(temp_folder / file_name).make_preferred().string();

                    status = mz_zip_reader_extract_to_file(&zip_archive, i, encode_path(target_file_path.c_str()).c_str(), MZ_ZIP_FLAG_CASE_SENSITIVE);
                    // target file is opened
                    if (MZ_FALSE == status) {
                        BOOST_LOG_TRIVIAL(info) << "Failed to open target file: " << target_file_path;
                    } else {
                        bool is_success = import_json_presets(substitutions, target_file_path, override_confirm, rule, overwrite, result);
                        BOOST_LOG_TRIVIAL(info) << "import target file: " << target_file_path << " import result" << is_success;
                    }
                }
            }
            fclose(zipFile);
            if (fs::exists(temp_folder)) fs::remove_all(temp_folder, ec);
            if (ec) BOOST_LOG_TRIVIAL(error) << "remove directory failed: " << ec.message();
        }
    }
    files = result;
    return substitutions;
}

bool PresetBundle::import_json_presets(PresetsConfigSubstitutions &            substitutions,
                                       std::string &                           file,
                                       std::function<int(std::string const &)> override_confirm,
                                       ForwardCompatibilitySubstitutionRule    rule,
                                       int &                                   overwrite,
                                       std::vector<std::string> &              result)
{
    try {
        DynamicPrintConfig config;
        // BBS: change to json format
        // ConfigSubstitutions config_substitutions = config.load_from_ini(preset.file, substitution_rule);
        std::map<std::string, std::string> key_values;
        std::string                        reason;
        ConfigSubstitutions                config_substitutions = config.load_from_json(file, rule, key_values, reason);
        std::string                        name                 = key_values[BBL_JSON_KEY_NAME];
        std::string                        version_str          = key_values[BBL_JSON_KEY_VERSION];
        boost::optional<Semver>            version              = Semver::parse(version_str);
        if (!version) return false;
        Semver app_version = *(Semver::parse(SLIC3R_VERSION));
        /*if (version->maj() > app_version.maj()) {
            BOOST_LOG_TRIVIAL(warning) << "Preset incompatibla, not loading: " << name;
            return false;
        }*/

        PresetCollection *collection = nullptr;
        if (config.has("printer_settings_id"))
            collection = &printers;
        else if (config.has("print_settings_id"))
            collection = &prints;
        else if (config.has("filament_settings_id"))
            collection = &filaments;
        if (collection == nullptr) {
            BOOST_LOG_TRIVIAL(warning) << "Preset type is unknown, not loading: " << name;
            return false;
        }
        if (overwrite == 0) overwrite = 1;
        if (auto p = collection->find_preset(name, false)) {
            if (p->is_default || p->is_system) {
                BOOST_LOG_TRIVIAL(warning) << "Preset already present and is system preset, not loading: " << name;
                return false;
            }
            if (overwrite != 2 && overwrite != 3) overwrite = override_confirm(name); //3: yes to all  2: no to all
        }
        if (overwrite == 0 || overwrite == 2) {
            BOOST_LOG_TRIVIAL(warning) << "Preset already present, not loading: " << name;
            return false;
        }

        DynamicPrintConfig new_config;
        Preset *           inherit_preset  = nullptr;
        ConfigOption *     inherits_config = config.option(BBL_JSON_KEY_INHERITS);
        std::string        inherits_value;
        if (inherits_config) {
            ConfigOptionString *option_str = dynamic_cast<ConfigOptionString *>(inherits_config);
            inherits_value                 = option_str->value;
            inherit_preset                 = collection->find_preset(inherits_value, false, true);
        }
        if (inherit_preset) {
            new_config = inherit_preset->config;
            // Merge child diff onto parent the same way PresetCollection::load_presets() does,
            // so that "nil" sentinel slots in nullable variant arrays (e.g.
            // nozzle_temperature == ["270","nil"]) fall back to the parent's value instead of
            // leaking INT_MAX into the active config via DynamicPrintConfig::apply().
            std::string            extruder_id_name, extruder_variant_name;
            std::set<std::string> *key_set1 = nullptr, *key_set2 = nullptr;
            Preset::get_extruder_names_and_keysets(collection->type(), extruder_id_name, extruder_variant_name, &key_set1, &key_set2);

            extend_default_config_length(config, inherit_preset->config, false, {});
            new_config.update_diff_values_to_child_config(config, extruder_id_name, extruder_variant_name, *key_set1, *key_set2);
        } else {
            // We support custom root preset now
            auto inherits_config2 = dynamic_cast<ConfigOptionString *>(inherits_config);
            if (inherits_config2 && !inherits_config2->value.empty()) {
                // we should skip this preset here
                BOOST_LOG_TRIVIAL(warning) << boost::format("can not find inherit preset for user preset %1%, just skip") % name;
                return false;
            }
            // Find a default preset for the config. The PrintPresetCollection provides different default preset based on the "printer_technology" field.
            const Preset &default_preset = collection->default_preset_for(config);
            new_config                   = default_preset.config;
            new_config.apply(std::move(config));
            extend_default_config_length(new_config, {}, true, default_preset.config);
        }

        Preset &preset     = collection->load_preset(collection->path_from_name(name, inherit_preset == nullptr), name, std::move(new_config), false);
        if (key_values.find(BBL_JSON_KEY_FILAMENT_ID) != key_values.end())
            preset.filament_id = key_values[BBL_JSON_KEY_FILAMENT_ID];
        preset.is_external = true;
        preset.version     = *version;
        inherit_preset     = collection->find_preset(inherits_value, false, true); // pointer maybe wrong after insert, redo find
        if (inherit_preset) preset.base_id = inherit_preset->setting_id;
        BOOST_LOG_TRIVIAL(info) << preset.name << " have " << KV(log_key::filament_id, preset.filament_id) << "and "
                                << KV(log_key::base_id, preset.base_id);
        Preset::normalize(preset.config);
        // Report configuration fields, which are misplaced into a wrong group.
        const Preset &default_preset = collection->default_preset_for(new_config);
        std::string   incorrect_keys = Preset::remove_invalid_keys(preset.config, default_preset.config);
        if (!incorrect_keys.empty())
            BOOST_LOG_TRIVIAL(error) << "Error in a preset file: The preset \"" << preset.file << "\" contains the following incorrect keys: " << incorrect_keys
                                     << ", which were removed";
        if (!config_substitutions.empty())
            substitutions.push_back({name, collection->type(), PresetConfigSubstitutions::Source::UserFile, file, std::move(config_substitutions)});
        collection->set_custom_preset_alias(preset);
        preset.save(inherit_preset ? &inherit_preset->config : nullptr);
        result.push_back(file);
    } catch (const std::ifstream::failure &err) {
        BOOST_LOG_TRIVIAL(error) << boost::format("The config cannot be loaded: %1%. Reason: %2%") % file % err.what();
    } catch (const std::runtime_error &err) {
        BOOST_LOG_TRIVIAL(error) << boost::format("Failed importing config file: %1%. Reason: %2%") % file % err.what();
    }
    return true;
}

//BBS save user preset to user_id preset folder
void PresetBundle::save_user_presets(AppConfig& config, std::vector<std::string>& need_to_delete_list)
{
    std::string user_sub_folder = DEFAULT_USER_FOLDER_NAME;
    if (!config.get("preset_folder").empty())
        user_sub_folder = config.get("preset_folder");
    //BBS: change directory by design
    const std::string dir_user_presets = data_dir() + "/" + PRESET_USER_DIR + "/"+ user_sub_folder;

    BOOST_LOG_TRIVIAL(debug) << boost::format("enter, save to %1%") % PathSanitizer::sanitize(dir_user_presets);

    fs::path user_folder(data_dir() + "/" + PRESET_USER_DIR);
    if (!fs::exists(user_folder))
        fs::create_directory(user_folder);

    fs::path folder(dir_user_presets);
    if (!fs::exists(folder))
        fs::create_directory(folder);

    this->prints.save_user_presets(dir_user_presets, PRESET_PRINT_NAME, need_to_delete_list);
    this->filaments.save_user_presets(dir_user_presets, PRESET_FILAMENT_NAME, need_to_delete_list);
    this->printers.save_user_presets(dir_user_presets, PRESET_PRINTER_NAME, need_to_delete_list);
    BOOST_LOG_TRIVIAL(debug) << boost::format("finished");
}

//BBS: save user preset to user_id preset folder
void PresetBundle::update_user_presets_directory(const std::string preset_folder)
{
    //BBS: change directory by design
    const std::string dir_user_presets = data_dir() + "/" + PRESET_USER_DIR + "/"+ preset_folder;

    BOOST_LOG_TRIVIAL(debug) << boost::format("enter, update directory to %1%") % PathSanitizer::sanitize(dir_user_presets);

    fs::path user_folder(data_dir() + "/" + PRESET_USER_DIR);
    if (!fs::exists(user_folder))
        fs::create_directory(user_folder);

    fs::path folder(dir_user_presets);
    if (!fs::exists(folder))
        fs::create_directory(folder);

    this->prints.update_user_presets_directory(dir_user_presets, PRESET_PRINT_NAME);
    this->filaments.update_user_presets_directory(dir_user_presets, PRESET_FILAMENT_NAME);
    this->printers.update_user_presets_directory(dir_user_presets, PRESET_PRINTER_NAME);
    BOOST_LOG_TRIVIAL(debug) << boost::format("finished");
}

void PresetBundle::remove_user_presets_directory(const std::string preset_folder)
{
    const std::string dir_user_presets = data_dir() + "/" + PRESET_USER_DIR + "/" + preset_folder;

    if (preset_folder.empty()) {
        BOOST_LOG_TRIVIAL(debug) << boost::format("preset_folder is empty, no need to remove directory : %1%") % dir_user_presets;
        return;
    }
    BOOST_LOG_TRIVIAL(debug) << boost::format("enter, delete directory : %1%") % dir_user_presets;
    fs::path folder(dir_user_presets);
    if (fs::exists(folder)) {
        fs::remove_all(folder);
    }
}

void PresetBundle::update_system_preset_setting_ids(std::map<std::string, std::map<std::string, std::string>>& system_presets)
{
    for (auto iterator: system_presets)
    {
        std::string name = iterator.first;
        std::map<std::string, std::string>& value_map = iterator.second;
        //get the type first
        std::map<std::string, std::string>::iterator type_iter = value_map.find(BBL_JSON_KEY_TYPE);
        if (type_iter == value_map.end()) {
            BOOST_LOG_TRIVIAL(warning) << boost::format("can not find type for setting %1%")%name;
            continue;
        }
        PresetCollection *preset_collection = nullptr;
        if (type_iter->second == PRESET_IOT_PRINT_TYPE) {
            preset_collection = &(this->prints);
        }
        else if (type_iter->second == PRESET_IOT_FILAMENT_TYPE) {
            preset_collection = &(this->filaments);
        }
        else if (type_iter->second == PRESET_IOT_PRINTER_TYPE) {
            preset_collection = &(this->printers);
        }
        else {
            BOOST_LOG_TRIVIAL(warning) << boost::format("invalid type %1% for setting %2%") %type_iter->second %name;
            continue;
        }
        std::string setting_id;
        if (value_map.count(BBL_JSON_KEY_SETTING_ID) > 0)
            setting_id = value_map[BBL_JSON_KEY_SETTING_ID];
        else {
            BOOST_LOG_TRIVIAL(warning) << boost::format("can not find setting_id for setting %1%")%name;
            continue;
        }
        Preset* preset = preset_collection->find_preset(name, false, true);
        if (preset) {
            if (!preset->setting_id.empty() && (preset->setting_id.compare(setting_id) != 0)) {
                BOOST_LOG_TRIVIAL(error) << boost::format("name %1%, local setting_id %2% is different with remote id %3%")
                    %preset->name %preset->setting_id %setting_id;
            }
            else if (preset->setting_id.empty())
                preset->setting_id = setting_id;
        }
        else {
            BOOST_LOG_TRIVIAL(warning) << boost::format("can not find setting %1% in system presets, type %2%") %name %type_iter->second;
            continue;
        }
    }
    return;
}


std::vector<std::vector<std::vector<float>>> PresetBundle::get_full_flush_matrix(bool with_multiplier) const
{
    auto full_config = this->full_config();
    int extruder_nums = full_config.option<ConfigOptionFloatsNullable>("nozzle_diameter")->values.size();
    std::vector<double> flush_volume_value = full_config.option<ConfigOptionFloatsNullable>("flush_volumes_matrix")->values;
    int filament_nums = full_config.option<ConfigOptionStrings>("filament_type")->values.size();

    std::vector<std::vector<std::vector<float>>> matrix;
    for(size_t extruder_id = 0; extruder_id < extruder_nums; ++ extruder_id){
        std::vector<float>              flush_matrix(cast<float>(get_flush_volumes_matrix(flush_volume_value, extruder_id, extruder_nums)));
        std::vector<std::vector<float>> wipe_volumes;
        for (unsigned int i = 0; i < filament_nums; ++i)
            wipe_volumes.push_back(std::vector<float>(flush_matrix.begin() + i * filament_nums, flush_matrix.begin() + (i + 1) * filament_nums));

        matrix.emplace_back(wipe_volumes);
    }

    if(with_multiplier){
        bool use_fast         = project_config.option<ConfigOptionEnum<PrimeVolumeMode>>("prime_volume_mode")->value == PrimeVolumeMode::pvmFast;
        auto flush_multiplies = use_fast ? project_config.option<ConfigOptionFloats>("flush_multiplier_fast")->values :
                                           project_config.option<ConfigOptionFloats>("flush_multiplier")->values;
        flush_multiplies.resize(extruder_nums, 1);
        for (size_t extruder_id = 0; extruder_id < extruder_nums; ++extruder_id) {
            for (auto& vec : matrix[extruder_id]) {
                for (auto& v : vec)
                    v *= flush_multiplies[extruder_id];
            }
        }
    }

    return matrix;
}

//BBS: validate printers from previous project
static std::set<std::string> gcodes_key_set =  {"filament_end_gcode", "filament_start_gcode", "change_filament_gcode", "layer_change_gcode", "machine_end_gcode", "machine_pause_gcode", "machine_start_gcode",
            "template_custom_gcode", "printing_by_object_gcode", "before_layer_change_gcode", "time_lapse_gcode", "wrapping_detection_gcode"};
int PresetBundle::validate_presets(const std::string &file_name, DynamicPrintConfig& config, std::set<std::string>& different_gcodes)
{
    bool    validated = false;
    std::vector<std::string> inherits_values                        = config.option<ConfigOptionStrings>("inherits_group", true)->values;
    std::vector<std::string> filament_preset_name                   = config.option<ConfigOptionStrings>("filament_settings_id", true)->values;
    std::string printer_preset                                      = config.option<ConfigOptionString>("printer_settings_id", true)->value;
    bool has_different_settings_to_system                           = config.option("different_settings_to_system")?true:false;
    std::vector<std::string> different_values;
    int     ret = VALIDATE_PRESETS_SUCCESS;

    if (has_different_settings_to_system)
        different_values = config.option<ConfigOptionStrings>("different_settings_to_system", true)->values;

    //PrinterTechnology printer_technology = Preset::printer_technology(config);
    size_t filament_count = config.option<ConfigOptionFloats>("filament_diameter")->values.size();
    inherits_values.resize(filament_count + 2, std::string());
    different_values.resize(filament_count + 2, std::string());
    filament_preset_name.resize(filament_count, std::string());

    std::string printer_inherits = inherits_values[filament_count + 1];

    validated = this->printers.validate_preset(printer_preset, printer_inherits);
    if (!validated) {
        BOOST_LOG_TRIVIAL(warning) << boost::format("file_name %1%, found the printer preset not inherit from system") % file_name;
        different_gcodes.emplace(printer_preset);
        ret = VALIDATE_PRESETS_PRINTER_NOT_FOUND;
    }
    for(unsigned int index = 0; index < filament_count; index ++)
    {
        std::string filament_preset = filament_preset_name[index];
        std::string filament_inherits = inherits_values[index+1];

        validated = this->filaments.validate_preset(filament_preset, filament_inherits);
        if (!validated) {
            BOOST_LOG_TRIVIAL(warning) << boost::format("file_name %1%, found the filament %2% preset not inherit from system") % file_name %(index+1);
            different_gcodes.emplace(filament_preset);
            ret = VALIDATE_PRESETS_FILAMENTS_NOT_FOUND;
        }
    }

    //self defined presets, return directly
    if (ret)
    {
        BOOST_LOG_TRIVIAL(warning) << boost::format("file_name %1%, found self defined presets, count %2%") %file_name %different_gcodes.size();
        return ret;
    }

    for(unsigned int index = 1; index < filament_count+2; index ++)
    {
        std::string different_settingss = different_values[index];

        std::vector<std::string> different_keys;

        Slic3r::unescape_strings_cstyle(different_settingss, different_keys);

        for (unsigned int j = 0; j < different_keys.size(); j++) {
            if (gcodes_key_set.find(different_keys[j]) != gcodes_key_set.end()) {
                different_gcodes.emplace(different_keys[j]);
                BOOST_LOG_TRIVIAL(warning) << boost::format("preset index %1%, different key %2%") %index %different_keys[j];
            }
        }
    }

    if (!different_gcodes.empty())
    {
        BOOST_LOG_TRIVIAL(warning) << boost::format("file_name %1%, found different gcodes count %2%") %file_name %different_gcodes.size();
        return VALIDATE_PRESETS_MODIFIED_GCODES;
    }

    BOOST_LOG_TRIVIAL(info) << boost::format("file_name %1%, validate presets success!") % file_name;

    return VALIDATE_PRESETS_SUCCESS;
}

void PresetBundle::remove_users_preset(AppConfig &config, std::map<std::string, std::map<std::string, std::string>> *my_presets)
{
    auto check_removed = [my_presets](Preset &preset) -> bool {
        if (my_presets == nullptr) return true;
        if (my_presets->find(preset.name) != my_presets->end()) return false;
        if (!preset.sync_info.empty()) return false; // syncing, not remove
        if (preset.setting_id.empty()) return false; // no id, not remove
        // Saved preset is removed by another session
        if (preset.is_dirty) {
            preset.setting_id.clear();
            return false;
        }
        preset.remove_files();
        BOOST_LOG_TRIVIAL(warning) << "preset removed, name: " << preset.name << ", type: " << Preset::get_type_string(preset.type) << " "
                                   << KV(log_key::uid, preset.user_id);
        return true;
    };
    std::string preset_folder_user_id = config.get("preset_folder");
    std::string printer_selected_preset_name = printers.get_selected_preset().name;
    bool need_reset_printer_preset = false;
    for (auto it = printers.begin(); it != printers.end();) {
        if (it->is_user() && it->user_id.compare(preset_folder_user_id) == 0 && check_removed(*it)) {
            if (it->name == printer_selected_preset_name)
                need_reset_printer_preset = true;
            it = printers.erase(it);
        } else {
            it++;
        }
    }

    if (need_reset_printer_preset) {
        std::string default_printer_model = BBL_DEFAULT_PRINTER_MODEL;
        std::string default_printer_name;
        for (auto it = printers.begin(); it != printers.end(); it++) {
            if (it->config.has("printer_model")) {
                if (it->config.opt_string("printer_model") == default_printer_model) {
                    default_printer_name = it->name;
                    break;
                }
            }
        }
        printers.select_preset_by_name(default_printer_name, true);
    } else {
        printers.select_preset_by_name(printer_selected_preset_name, false);
    }

    std::string selected_print_name = prints.get_selected_preset().name;
    bool need_reset_print_preset = false;
    // remove preset if user_id is not current user
    for (auto it = prints.begin(); it != prints.end();) {
        if (it->is_user() && it->user_id.compare(preset_folder_user_id) == 0 && check_removed(*it)) {
            if (it->name == selected_print_name)
                need_reset_print_preset = true;
            it = prints.erase(it);
        } else {
            it++;
        }
    }
    if (need_reset_print_preset && printers.get_selected_preset().config.has("default_print_profile")) {
        std::string default_print_profile_name = printers.get_selected_preset().config.opt_string("default_print_profile");
        prints.select_preset_by_name(default_print_profile_name, true);
    } else {
        prints.select_preset_by_name(selected_print_name, false);
    }

    std::string selected_filament_name = filaments.get_selected_preset().name;
    bool need_reset_filament_preset = false;
    for (auto it = filaments.begin(); it != filaments.end();) {
        if (it->is_user() && it->user_id.compare(preset_folder_user_id) == 0 && check_removed(*it)) {
            if (it->name == selected_filament_name)
                need_reset_filament_preset = true;
            it = filaments.erase(it);
        } else {
            it++;
        }
    }
    if (need_reset_filament_preset && printers.get_selected_preset().config.has("default_filament_profile")) {
        const std::vector<std::string>& prefered_filament_profiles = printers.get_selected_preset().config.option<ConfigOptionStrings>("default_filament_profile")->values;
        if (prefered_filament_profiles.size() > 0)
            filaments.select_preset_by_name(prefered_filament_profiles[0], true);
    } else {
        filaments.select_preset_by_name(selected_filament_name, false);
    }

    update_compatible(PresetSelectCompatibleType::Always);

    /* set selected preset */
    for (size_t i = 0; i < filament_presets.size(); ++i)
    {
        auto preset = this->filaments.find_preset(filament_presets[i]);
        if (preset == nullptr)
            filament_presets[i] = filaments.get_selected_preset_name();
    }
}

// Load system presets into this PresetBundle.
// For each vendor, there will be a single PresetBundle loaded.
/*std::pair<PresetsConfigSubstitutions, std::string> PresetBundle::load_system_presets(ForwardCompatibilitySubstitutionRule compatibility_rule)
{
    //BBS: add config related logs
    BOOST_LOG_TRIVIAL(debug) << boost::format("enter, compatibility_rule %1%")%compatibility_rule;
    if (compatibility_rule == ForwardCompatibilitySubstitutionRule::EnableSystemSilent)
        // Loading system presets, don't log substitutions.
        compatibility_rule = ForwardCompatibilitySubstitutionRule::EnableSilent;
    else if (compatibility_rule == ForwardCompatibilitySubstitutionRule::EnableSilentDisableSystem)
        // Loading system presets, throw on unknown option value.
        compatibility_rule = ForwardCompatibilitySubstitutionRule::Disable;

    // Here the vendor specific read only Config Bundles are stored.
    //BBS: change directory by design
    boost::filesystem::path     dir = (boost::filesystem::path(data_dir()) / PRESET_SYSTEM_DIR).make_preferred();
    PresetsConfigSubstitutions  substitutions;
    std::string                 errors_cummulative;
    bool                        first = true;
    for (auto &dir_entry : boost::filesystem::directory_iterator(dir))
        if (Slic3r::is_ini_file(dir_entry)) {
            std::string name = dir_entry.path().filename().string();
            // Remove the .ini suffix.
            name.erase(name.size() - 4);
            try {
                // Load the config bundle, flatten it.
                if (first) {
                    // Reset this PresetBundle and load the first vendor config.
                    append(substitutions, this->load_configbundle(dir_entry.path().string(), PresetBundle::LoadSystem, compatibility_rule).first);
                    first = false;
                } else {
                    // Load the other vendor configs, merge them with this PresetBundle.
                    // Report duplicate profiles.
                    PresetBundle other;
                    append(substitutions, other.load_configbundle(dir_entry.path().string(), PresetBundle::LoadSystem, compatibility_rule).first);
                    std::vector<std::string> duplicates = this->merge_presets(std::move(other));
                    if (! duplicates.empty()) {
                        errors_cummulative += "Vendor configuration file " + name + " contains the following presets with names used by other vendors: ";
                        for (size_t i = 0; i < duplicates.size(); ++ i) {
                            if (i > 0)
                                errors_cummulative += ", ";
                            errors_cummulative += duplicates[i];
                        }
                    }
                }
            } catch (const std::runtime_error &err) {
                errors_cummulative += err.what();
                errors_cummulative += "\n";
            }
        }
    if (first) {
		// No config bundle loaded, reset.
		this->reset(false);
	}

	this->update_system_maps();
    //BBS: add config related logs
    BOOST_LOG_TRIVIAL(debug) << boost::format("finished, errors_cummulative %1%")%errors_cummulative;
    return std::make_pair(std::move(substitutions), errors_cummulative);
}*/

//BBS: add json related logic, load system presets from json
std::pair<PresetsConfigSubstitutions, std::string> PresetBundle::load_system_presets_from_json(ForwardCompatibilitySubstitutionRule compatibility_rule)
{
    //BBS: add config related logs
    BOOST_LOG_TRIVIAL(debug) << boost::format("enter, compatibility_rule %1%")%compatibility_rule;
    if (compatibility_rule == ForwardCompatibilitySubstitutionRule::EnableSystemSilent)
        // Loading system presets, don't log substitutions.
        compatibility_rule = ForwardCompatibilitySubstitutionRule::EnableSilent;
    else if (compatibility_rule == ForwardCompatibilitySubstitutionRule::EnableSilentDisableSystem)
        // Loading system presets, throw on unknown option value.
        compatibility_rule = ForwardCompatibilitySubstitutionRule::Disable;

    // Here the vendor specific read only Config Bundles are stored.
    //BBS: change directory by design
    boost::filesystem::path     dir = (boost::filesystem::path(data_dir()) / PRESET_SYSTEM_DIR).make_preferred();
    PresetsConfigSubstitutions  substitutions;
    std::string                 errors_cummulative;
    bool                        first = true;
    for (auto &dir_entry : boost::filesystem::directory_iterator(dir))
    {
        std::string vendor_file = dir_entry.path().string();
        if (Slic3r::is_json_file(vendor_file)) {
            std::string vendor_name = dir_entry.path().filename().string();
            // Remove the .json suffix.
            vendor_name.erase(vendor_name.size() - 5);
            try {
                // Load the config bundle, flatten it.
                if (first) {
                    // Reset this PresetBundle and load the first vendor config.
                    append(substitutions, this->load_vendor_configs_from_json(dir.string(), vendor_name, PresetBundle::LoadSystem, compatibility_rule).first);
                    first = false;
                } else {
                    // Load the other vendor configs, merge them with this PresetBundle.
                    // Report duplicate profiles.
                    PresetBundle other;
                    append(substitutions, other.load_vendor_configs_from_json(dir.string(), vendor_name, PresetBundle::LoadSystem, compatibility_rule).first);
                    std::vector<std::string> duplicates = this->merge_presets(std::move(other));
                    if (! duplicates.empty()) {
                        errors_cummulative += "Found duplicated settings in vendor " + vendor_name + "'s json file lists: ";
                        for (size_t i = 0; i < duplicates.size(); ++ i) {
                            if (i > 0)
                                errors_cummulative += ", ";
                            errors_cummulative += duplicates[i];
                        }
                    }
                }
            } catch (const std::runtime_error &err) {
                errors_cummulative += err.what();
                errors_cummulative += "\n";
            }
        }
    }

    if (first) {
		// No config bundle loaded, reset.
		this->reset(false);
	}

	this->update_system_maps();
    //BBS: add config related logs
    BOOST_LOG_TRIVIAL(debug) << boost::format("finished, errors_cummulative %1%")%errors_cummulative;
    return std::make_pair(std::move(substitutions), errors_cummulative);
}

std::pair<PresetsConfigSubstitutions, std::string> PresetBundle::load_system_models_from_json(ForwardCompatibilitySubstitutionRule compatibility_rule)
{
    BOOST_LOG_TRIVIAL(debug) << boost::format("enter, compatibility_rule %1%") % compatibility_rule;
    if (compatibility_rule == ForwardCompatibilitySubstitutionRule::EnableSystemSilent)
        // Loading system presets, don't log substitutions.
        compatibility_rule = ForwardCompatibilitySubstitutionRule::EnableSilent;
    else if (compatibility_rule == ForwardCompatibilitySubstitutionRule::EnableSilentDisableSystem)
        // Loading system presets, throw on unknown option value.
        compatibility_rule = ForwardCompatibilitySubstitutionRule::Disable;

    // Here the vendor specific read only Config Bundles are stored.
    boost::filesystem::path    dir = (boost::filesystem::path(resources_dir()) / "profiles").make_preferred();
    PresetsConfigSubstitutions substitutions;
    std::string                errors_cummulative;
    for (auto &dir_entry : boost::filesystem::directory_iterator(dir)) {
        std::string vendor_file = dir_entry.path().string();
        if (Slic3r::is_json_file(vendor_file)) {
            std::string vendor_name = dir_entry.path().filename().string();
            // Remove the .json suffix.
            vendor_name.erase(vendor_name.size() - 5);
            try {
                // Load the config bundle, flatten it.
                append(substitutions, load_vendor_configs_from_json(dir.string(), vendor_name, PresetBundle::LoadVendorOnly, compatibility_rule).first);
            } catch (const std::runtime_error &err) {
                errors_cummulative += err.what();
                errors_cummulative += "\n";
            }
        }
    }

    BOOST_LOG_TRIVIAL(debug) << boost::format("finished, errors_cummulative %1%") % errors_cummulative;
    return std::make_pair(std::move(substitutions), errors_cummulative);
}

std::pair<PresetsConfigSubstitutions, std::string> PresetBundle::load_system_filaments_json(ForwardCompatibilitySubstitutionRule compatibility_rule)
{
    BOOST_LOG_TRIVIAL(debug) << boost::format("enter, compatibility_rule %1%") % compatibility_rule;
    if (compatibility_rule == ForwardCompatibilitySubstitutionRule::EnableSystemSilent)
        // Loading system presets, don't log substitutions.
        compatibility_rule = ForwardCompatibilitySubstitutionRule::EnableSilent;
    else if (compatibility_rule == ForwardCompatibilitySubstitutionRule::EnableSilentDisableSystem)
        // Loading system presets, throw on unknown option value.
        compatibility_rule = ForwardCompatibilitySubstitutionRule::Disable;

    // Here the vendor specific read only Config Bundles are stored.
    boost::filesystem::path    dir = (boost::filesystem::path(resources_dir()) / "profiles").make_preferred();
    PresetsConfigSubstitutions substitutions;
    std::string                errors_cummulative;
    bool                       first = true;
    for (auto &dir_entry : boost::filesystem::directory_iterator(dir)) {
        std::string vendor_file = dir_entry.path().string();
        if (Slic3r::is_json_file(vendor_file)) {
            std::string vendor_name = dir_entry.path().filename().string();
            // Remove the .json suffix.
            vendor_name.erase(vendor_name.size() - 5);
            try {
                if (first) {
                    // Reset this PresetBundle and load the first vendor config.
                    append(substitutions, this->load_vendor_configs_from_json(dir.string(), vendor_name, PresetBundle::LoadSystem | PresetBundle::LoadFilamentOnly, compatibility_rule).first);
                    first = false;
                } else {
                    // Load the other vendor configs, merge them with this PresetBundle.
                    // Report duplicate profiles.
                    PresetBundle other;
                    append(substitutions, other.load_vendor_configs_from_json(dir.string(), vendor_name, PresetBundle::LoadSystem | PresetBundle::LoadFilamentOnly, compatibility_rule).first);
                    std::vector<std::string> duplicates = this->merge_presets(std::move(other));
                    if (!duplicates.empty()) {
                        errors_cummulative += "Found duplicated settings in vendor " + vendor_name + "'s json file lists: ";
                        for (size_t i = 0; i < duplicates.size(); ++i) {
                            if (i > 0) errors_cummulative += ", ";
                            errors_cummulative += duplicates[i];
                        }
                    }
                }
            } catch (const std::runtime_error &err) {
                errors_cummulative += err.what();
                errors_cummulative += "\n";
            }
        }
    }

    BOOST_LOG_TRIVIAL(debug) << boost::format("finished, errors_cummulative %1%") % errors_cummulative;
    return std::make_pair(std::move(substitutions), errors_cummulative);
}

VendorProfile PresetBundle::get_custom_vendor_models() const
{
    VendorProfile vendor;
    vendor.name = PRESET_CUSTOM_VENDOR;
    vendor.id   = PRESET_CUSTOM_VENDOR;
    for (auto &preset : printers.get_presets()) {
        if (preset.is_system) continue;
        if (printers.get_preset_base(preset) != &preset) continue;
        if (preset.is_default) continue;
        auto model = preset.config.opt_string("printer_model");
        auto variant = preset.config.opt_string("printer_variant");
        auto iter_model = std::find_if(vendor.models.begin(), vendor.models.end(), [model](VendorProfile::PrinterModel &m) {
            return m.name == model;
        });
        if (iter_model == vendor.models.end()) {
            iter_model = vendor.models.emplace(vendor.models.end(), VendorProfile::PrinterModel{});
            iter_model->id       = model;
            iter_model->name     = model;
            iter_model->variants = {VendorProfile::PrinterVariant(variant)};
        } else {
            iter_model->variants.push_back(VendorProfile::PrinterVariant(variant));
        }
    }
    return vendor;
}

std::string GetNozzleVolumeTypeStr(const NozzleVolumeType &type)
{
    switch (type) {
    case NozzleVolumeType::nvtStandard: return "Standard";
    case NozzleVolumeType::nvtHighFlow: return "High Flow";
    case NozzleVolumeType::nvtTPUHighFlow: return "TPU High Flow";
    case NozzleVolumeType::nvtTPU285: return "TPU285";
    case NozzleVolumeType::nvtE3DHighFlow: return "E3D High Flow";
    case NozzleVolumeType::nvtHybrid: return "Hybrid";
    default: return std::string();
    }
}

std::map<NozzleVolumeType, int> count_default_nozzle_volume_types(const ConfigBase &printer_config, int extruder_id, int slot_count)
{
    std::map<NozzleVolumeType, int> counts;
    for (int slot = 0; slot < slot_count; ++slot)
        ++counts[get_default_nozzle_volume_type(printer_config, extruder_id, slot)];
    return counts;
}

ExtruderFullStat build_extruder_full_stat_from_nozzle_stat(const ExtruderNozzleStat &nozzle_stat,
                                                           const std::vector<int>   &extruder_max_nozzle_count,
                                                           const std::string        &diameter_str)
{
    ExtruderFullStat full_stat;
    const auto raw_stats = nozzle_stat.get_raw_stat();
    full_stat.resize(raw_stats.size());
    for (size_t extruder_id = 0; extruder_id < raw_stats.size(); ++extruder_id) {
        std::vector<std::pair<std::string, std::string>> nozzle_stats;
        const int max_count = (extruder_id < extruder_max_nozzle_count.size())
            ? std::max(0, extruder_max_nozzle_count[extruder_id]) : 0;
        for (const auto &[volume_type, count] : raw_stats[extruder_id]) {
            std::string volume_str = GetNozzleVolumeTypeStr(volume_type);
            for (int i = 0; i < count && static_cast<int>(nozzle_stats.size()) < max_count; ++i)
                nozzle_stats.emplace_back(diameter_str, volume_str);
        }
        // 保留空槽占位,不臆造口径错误的喷嘴。
        nozzle_stats.resize(max_count, {std::string(), std::string()});
        full_stat.set_extruder_stats(extruder_id, nozzle_stats);
    }
    return full_stat;
}

bool per_slot_nozzle_options_consistent(const DynamicPrintConfig &config)
{
    const ConfigOptionInts*         count_opt = config.option<ConfigOptionInts>("extruder_nozzle_count");
    const ConfigOptionEnumsGeneric* vtype_opt = config.option<ConfigOptionEnumsGeneric>("extruder_nozzle_volume_type");
    const ConfigOptionFloats*       diam_opt  = config.option<ConfigOptionFloats>("extruder_nozzle_diameter");
    const ConfigOptionInts*         slot_opt  = config.option<ConfigOptionInts>("extruder_nozzle_slot");
    const std::vector<int> counts = count_opt ? count_opt->values : std::vector<int>{};
    const std::vector<int> vtypes = vtype_opt ? vtype_opt->values : std::vector<int>{};
    const std::vector<double> diams = diam_opt ? diam_opt->values : std::vector<double>{};
    const std::vector<int> slots = slot_opt ? slot_opt->values : std::vector<int>{};

    int total = 0;
    for (int c : counts) {
        if (c < 0)
            return false;
        total += c;
    }
    return total > 0
        && static_cast<int>(vtypes.size()) == total
        && static_cast<int>(diams.size()) == total
        && static_cast<int>(slots.size()) == total;
}

void sync_per_slot_nozzle_options_from_full_stat(DynamicPrintConfig &config, const ExtruderFullStat &full_stat)
{
    const auto raw = full_stat.get_raw_stat();
    std::vector<int>    nozzle_counts;
    std::vector<int>    nozzle_volume_types;
    std::vector<double> nozzle_diameters;
    std::vector<int>    nozzle_slots;
    nozzle_counts.reserve(raw.size());
    int global_slot = 0;
    for (const auto &ext : raw) {
        int installed = 0;
        for (const auto &slot : ext) {
            if (!slot.second.empty()) {
                nozzle_volume_types.push_back(static_cast<int>(nozzle_volume_type_from_string(slot.second)));
                nozzle_diameters.push_back(slot.first.empty() ? 0.0 : string_to_double_decimal_point(slot.first.c_str()));
                nozzle_slots.push_back(global_slot);
                ++installed;
            }
            ++global_slot;
        }
        nozzle_counts.push_back(installed);
    }
    config.option<ConfigOptionInts>("extruder_nozzle_count", true)->values              = std::move(nozzle_counts);
    config.option<ConfigOptionEnumsGeneric>("extruder_nozzle_volume_type", true)->values = std::move(nozzle_volume_types);
    config.option<ConfigOptionFloats>("extruder_nozzle_diameter", true)->values         = std::move(nozzle_diameters);
    config.option<ConfigOptionInts>("extruder_nozzle_slot", true)->values               = std::move(nozzle_slots);
}

/**
 * \brief The smallest installed nozzle diameter of each extruder, so slicing uses the selected diameters
 *
 * The smallest diameter is conservative for line-width and layer-height limits when one extruder mixes
 * diameters. An extruder with no installed slot keeps its fallback value. The result has the length of
 * \p fallback, so the extruder count stays as the preset gives it.
 *
 * \param nozzles   the nozzle inventory
 * \param fallback  nozzle_diameter of the printer preset
 */
static std::vector<double> smallest_installed_diameters(const NozzleInventory &nozzles, const std::vector<double> &fallback)
{
    std::vector<double> result = fallback;
    for (size_t e = 0; e < result.size(); ++e) {
        double min_diam = 0.0;
        bool   found    = false;
        for (const NozzleSlot &slot : nozzles.slots(int(e))) {
            if (slot.empty())
                continue;
            const double d = string_to_double_decimal_point(slot.diameter.c_str());
            if (d <= 0.0)
                continue;
            if (!found || d < min_diam) {
                min_diam = d;
                found    = true;
            }
        }
        if (found)
            result[e] = min_diam;
    }
    return result;
}

// Merge one vendor's presets with the other vendor's presets, report duplicates.
std::vector<std::string> PresetBundle::merge_presets(PresetBundle &&other)
{
    this->vendors.insert(other.vendors.begin(), other.vendors.end());
    std::vector<std::string> duplicate_prints        = this->prints       .merge_presets(std::move(other.prints),        this->vendors);
    std::vector<std::string> duplicate_sla_prints    = this->sla_prints   .merge_presets(std::move(other.sla_prints),    this->vendors);
    std::vector<std::string> duplicate_filaments     = this->filaments    .merge_presets(std::move(other.filaments),     this->vendors);
    std::vector<std::string> duplicate_sla_materials = this->sla_materials.merge_presets(std::move(other.sla_materials), this->vendors);
    std::vector<std::string> duplicate_printers      = this->printers     .merge_presets(std::move(other.printers),      this->vendors);
	append(this->obsolete_presets.prints,        std::move(other.obsolete_presets.prints));
	append(this->obsolete_presets.sla_prints,    std::move(other.obsolete_presets.sla_prints));
	append(this->obsolete_presets.filaments,     std::move(other.obsolete_presets.filaments));
    append(this->obsolete_presets.sla_materials, std::move(other.obsolete_presets.sla_materials));
	append(this->obsolete_presets.printers,      std::move(other.obsolete_presets.printers));
	append(duplicate_prints, std::move(duplicate_sla_prints));
	append(duplicate_prints, std::move(duplicate_filaments));
    append(duplicate_prints, std::move(duplicate_sla_materials));
    append(duplicate_prints, std::move(duplicate_printers));
    return duplicate_prints;
}

void PresetBundle::update_system_maps()
{
    this->prints 	   .update_map_system_profile_renamed();
    this->sla_prints   .update_map_system_profile_renamed();
    this->filaments    .update_map_system_profile_renamed();
    this->sla_materials.update_map_system_profile_renamed();
    this->printers     .update_map_system_profile_renamed();

    this->prints       .update_map_alias_to_profile_name();
    this->sla_prints   .update_map_alias_to_profile_name();
    this->filaments    .update_map_alias_to_profile_name();
    this->sla_materials.update_map_alias_to_profile_name();
}

static inline std::string remove_ini_suffix(const std::string &name)
{
    std::string out = name;
    if (boost::iends_with(out, ".ini"))
        out.erase(out.end() - 4, out.end());
    return out;
}

// Set the "enabled" flag for printer vendors, printer models and printer variants
// based on the user configuration.
// If the "vendor" section is missing, enable all models and variants of the particular vendor.
void PresetBundle::load_installed_printers(const AppConfig &config)
{
	this->update_system_maps();
    for (auto &preset : printers)
        preset.set_visible_from_appconfig(config);
}

const std::string& PresetBundle::get_preset_name_by_alias( const Preset::Type& preset_type, const std::string& alias) const
{
    // there are not aliases for Printers profiles
    if (preset_type == Preset::TYPE_PRINTER || preset_type == Preset::TYPE_INVALID)
        return alias;

    const PresetCollection& presets = preset_type == Preset::TYPE_PRINT     ? prints :
                                      preset_type == Preset::TYPE_SLA_PRINT ? sla_prints :
                                      preset_type == Preset::TYPE_FILAMENT  ? filaments :
                                      sla_materials;

    return presets.get_preset_name_by_alias(alias);
}

//BBS: get filament required hrc by filament type
const int PresetBundle::get_required_hrc_by_filament_id(const std::string& filament_id) const
{
    for (auto iter = filaments.m_presets.begin(); iter != filaments.m_presets.end(); iter++) {
        if (!iter->filament_id.empty() && iter->filament_id == filament_id && iter->config.has("required_nozzle_HRC")) { return iter->config.opt_int("required_nozzle_HRC", 0); }
    }

    return 0;
}

//BBS: add project embedded preset logic
void PresetBundle::save_changes_for_preset(const std::string& new_name, Preset::Type type,
                                           const std::vector<std::string>& unselected_options, bool save_to_project)
{
    PresetCollection& presets = type == Preset::TYPE_PRINT          ? prints :
                                type == Preset::TYPE_SLA_PRINT      ? sla_prints :
                                type == Preset::TYPE_FILAMENT       ? filaments :
                                type == Preset::TYPE_SLA_MATERIAL   ? sla_materials : printers;

    // if we want to save just some from selected options
    if (!unselected_options.empty()) {
        // revert unselected options to the old values
        presets.get_edited_preset().config.apply_only(presets.get_selected_preset().config, unselected_options);
    }

    if (type == Preset::TYPE_PRINTER)
        stamp_printer_nozzle_diameter_mode();

    // Save the preset into Slic3r::data_dir / presets / section_name / preset_name.ini
    //BBS: add project embedded preset logic
    //presets.save_current_preset(new_name);
    presets.save_current_preset(new_name, false, save_to_project);
    // Mark the print & filament enabled if they are compatible with the currently selected preset.
    // If saving the preset changes compatibility with other presets, keep the now incompatible dependent presets selected, however with a "red flag" icon showing that they are no more compatible.
    update_compatible(PresetSelectCompatibleType::Never);

    if (type == Preset::TYPE_FILAMENT) {
        // synchronize the first filament presets.
        set_filament_preset(0, filaments.get_selected_preset_name());
    }
}

void PresetBundle::load_installed_filaments(AppConfig &config)
{
    BOOST_LOG_TRIVIAL(info) << boost::format("enter, printer size %1%")%printers.size();
    //if (! config.has_section(AppConfig::SECTION_FILAMENTS)
    //    || config.get_section(AppConfig::SECTION_FILAMENTS).empty()) {
        // Compatibility with the PrusaSlicer 2.1.1 and older, where the filament profiles were not installable yet.
        // Find all filament profiles, which are compatible with installed printers, and act as if these filament profiles
        // were installed.
        std::unordered_set<const Preset*> compatible_filaments;
        for (const Preset &printer : printers)
            if (printer.is_visible && printer.printer_technology() == ptFFF && printer.vendor && (!printer.vendor->models.empty())) {
                bool add_default_materials = true;
                if (config.has_section(AppConfig::SECTION_FILAMENTS))
                {
                    const std::map<std::string, std::string>& installed_filament = config.get_section(AppConfig::SECTION_FILAMENTS);
                    for (auto filament_iter : installed_filament)
                    {
                        Preset* filament = filaments.find_preset(filament_iter.first, false, true);
                        if (filament && is_compatible_with_printer(PresetWithVendorProfile(*filament, filament->vendor), PresetWithVendorProfile(printer, printer.vendor)))
                        {
                            //already has compatible filament
                            add_default_materials = false;
                            BOOST_LOG_TRIVIAL(info) << boost::format("printer %1% vendor %2% already has default filament %3%")%printer.name %printer.vendor->name %filament_iter.first;
                            break;
                        }
                    }
                }

                if (!add_default_materials)
                    continue;

                const VendorProfile::PrinterModel *printer_model = PresetUtils::system_printer_model(printer);
                if (!printer_model) {
                    BOOST_LOG_TRIVIAL(warning) << boost::format("can not find printer_model for printer %1%")%printer.name;
                    continue;
                }
                BOOST_LOG_TRIVIAL(info) << boost::format("printer %1% vendor %2% don't have filament visible, will add %3% default filaments")%printer.name %printer.vendor %printer_model->default_materials.size();
                for (auto default_filament: printer_model->default_materials)
                {
                    Preset* filament = filaments.find_preset(default_filament, false, true);
                    if (filament && filament->is_system)
                        compatible_filaments.insert(filament);
                }
                //const PresetWithVendorProfile printer_with_vendor_profile = printers.get_preset_with_vendor_profile(printer);
                //for (const Preset &filament : filaments)
                //  if (filament.is_system && is_compatible_with_printer(filaments.get_preset_with_vendor_profile(filament), printer_with_vendor_profile))
                //      compatible_filaments.insert(&filament);
            }
        // and mark these filaments as installed, therefore this code will not be executed at the next start of the application.
        for (const auto &filament: compatible_filaments) {
            BOOST_LOG_TRIVIAL(info) << boost::format("set filament %1% to visible by default")%filament->name;
            config.set(AppConfig::SECTION_FILAMENTS, filament->name, "true");
        }
    //}

    for (auto &preset : filaments)
        preset.set_visible_from_appconfig(config);

    quick_fix_for_filaments_due_to_upgrade(config);
    BOOST_LOG_TRIVIAL(info) << boost::format("exit.");
}

void PresetBundle::quick_fix_for_filaments_due_to_upgrade(AppConfig &config)
{
    // for a visible/selected filament recorded in BambuStudio.conf fialments section
    // make all filaments of the same series(vendor+type) visible no matter the corresponding machine is selected or not
    // this is also the behaviour of the configure guide at fist startup of a clean install
    // filaments are always selected as a group by type, which is filament.alias in code
    static bool upgraded = false;
    if (upgraded) return;
    upgraded = true;

    if (!config.has_section(AppConfig::SECTION_FILAMENTS)) return;

    const auto &old_version = config.orig_version();
    const auto &cur_version = Semver(config.get("version"));
    if (old_version == cur_version) return;

    const std::map<std::string, std::string> &installed_filament = config.get_section(AppConfig::SECTION_FILAMENTS);

    std::unordered_set<std::string> visible_aliases;
    for (const auto &[name, _] : installed_filament) {
        Preset *filament = filaments.find_preset(name, false, true);
        if (!filament || !filament->is_visible || filament->alias.empty()) continue;
        visible_aliases.insert(filament->alias);
    }

    std::vector<PresetWithVendorProfile> printer_profiles;
    for (const Preset &printer : printers) {
        if (printer.printer_technology() != ptFFF) continue;
        printer_profiles.emplace_back(printers.get_preset_with_vendor_profile(printer));
    }

    // For each invisible filament, check if it should become visible
    for (auto &filament : filaments) {
        if (filament.is_visible || filament.alias.empty() || !filament.is_system) continue;

        for (const auto &printer_profile : printer_profiles) {
            if (!is_compatible_with_printer(filaments.get_preset_with_vendor_profile(filament), printer_profile)) continue;
            if (std::find(visible_aliases.cbegin(), visible_aliases.cend(), filament.alias) == visible_aliases.cend()) continue;
            filament.is_visible = true;
            config.set(AppConfig::SECTION_FILAMENTS, filament.name, "true");
        }
    }
}

void PresetBundle::load_installed_sla_materials(AppConfig &config)
{
    if (! config.has_section(AppConfig::SECTION_MATERIALS)) {
        std::unordered_set<const Preset*> comp_sla_materials;
		// Compatibility with the PrusaSlicer 2.1.1 and older, where the SLA material profiles were not installable yet.
		// Find all SLA material profiles, which are compatible with installed printers, and act as if these SLA material profiles
		// were installed.
        for (const Preset &printer : printers)
            if (printer.is_visible && printer.printer_technology() == ptSLA) {
				const PresetWithVendorProfile printer_with_vendor_profile = printers.get_preset_with_vendor_profile(printer);
				for (const Preset &material : sla_materials)
					if (material.is_system && is_compatible_with_printer(sla_materials.get_preset_with_vendor_profile(material), printer_with_vendor_profile))
						comp_sla_materials.insert(&material);
			}
		// and mark these SLA materials as installed, therefore this code will not be executed at the next start of the application.
		for (const auto &material: comp_sla_materials)
            config.set(AppConfig::SECTION_MATERIALS, material->name, "true");
    }

    for (auto &preset : sla_materials)
        preset.set_visible_from_appconfig(config);
}

// Load selections (current print, current filaments, current printer) from config.ini
// This is done on application start up or after updates are applied.
void PresetBundle::load_selections(AppConfig &config, const PresetPreferences& preferred_selection/* = PresetPreferences()*/)
{
    BOOST_LOG_TRIVIAL(info) << boost::format("enter, preferred printer_model_id %1%")%preferred_selection.printer_model_id;
	// Update visibility of presets based on application vendor / model / variant configuration.
	this->load_installed_printers(config);

    // Update visibility of filament and sla material presets
    this->load_installed_filaments(config);
    this->load_installed_sla_materials(config);

    // Parse the initial print / filament / printer profile names.
    std::string initial_print_profile_name        = remove_ini_suffix(config.get("presets", PRESET_PRINT_NAME));
    std::string initial_sla_print_profile_name    = remove_ini_suffix(config.get("presets", PRESET_SLA_PRINT_NAME));
    std::string initial_filament_profile_name     = remove_ini_suffix(config.get("presets", PRESET_FILAMENT_NAME));
    std::string initial_sla_material_profile_name = remove_ini_suffix(config.get("presets", PRESET_SLA_MATERIALS_NAME));
	std::string initial_printer_profile_name      = remove_ini_suffix(config.get("presets", PRESET_PRINTER_NAME));

    // Activate print / filament / printer profiles from either the config,
    // or from the preferred_model_id suggestion passed in by ConfigWizard.
    // If the printer profile enumerated by the config are not visible, select an alternate preset.
    // Do not select alternate profiles for the print / filament profiles as those presets
    // will be selected by the following call of this->update_compatible(PresetSelectCompatibleType::Always).

    const Preset *initial_printer = printers.find_preset(initial_printer_profile_name);
    // If executed due to a Config Wizard update, preferred_printer contains the first newly installed printer, otherwise nullptr.
    const Preset *preferred_printer = printers.find_system_preset_by_model_and_variant(preferred_selection.printer_model_id, preferred_selection.printer_variant);
    printers.select_preset_by_name(preferred_printer ? preferred_printer->name : initial_printer_profile_name, true);
    CNumericLocalesSetter locales_setter;

    //BBS: set default print/filament profiles to BBL's default setting
    if (preferred_printer)
    {
        const std::string& prefered_print_profile = preferred_printer->config.opt_string("default_print_profile");
        if ((!initial_print_profile_name.compare("Default Setting")) && (prefered_print_profile.size() > 0))
            initial_print_profile_name = prefered_print_profile;

        const std::vector<std::string>& prefered_filament_profiles = preferred_printer->config.option<ConfigOptionStrings>("default_filament_profile")->values;
        if ((!initial_filament_profile_name.compare("Default Filament")) && (prefered_filament_profiles.size() > 0))
            initial_filament_profile_name = prefered_filament_profiles[0];
    }

    // Selects the profile, leaves it to -1 if the initial profile name is empty or if it was not found.
    prints.select_preset_by_name_strict(initial_print_profile_name);
    filaments.select_preset_by_name_strict(initial_filament_profile_name);
	sla_prints.select_preset_by_name_strict(initial_sla_print_profile_name);
    sla_materials.select_preset_by_name_strict(initial_sla_material_profile_name);

    // Load the names of the other filament profiles selected for a multi-material printer.
    // Load it even if the current printer technology is SLA.
    // The possibly excessive filament names will be later removed with this->update_multi_material_filament_presets()
    // once the FFF technology gets selected.
    this->filament_presets = { filaments.get_selected_preset_name() };
    for (unsigned int i = 1; i < 1000; ++ i) {
        char name[64];
        sprintf(name, "filament_%02u", i);
        if (! config.has("presets", name))
            break;
        this->filament_presets.emplace_back(remove_ini_suffix(config.get("presets", name)));
    }

    // Load data from AppConfig to ProjectConfig when Studio is initialized.
    std::vector<std::string> filament_colors;
    if (config.has("presets", "filament_colors")) {
        boost::algorithm::split(filament_colors, config.get("presets", "filament_colors"), boost::algorithm::is_any_of(","));
    }
    filament_colors.resize(filament_presets.size(), "#00AE42");
    project_config.option<ConfigOptionStrings>("filament_colour")->values = filament_colors;

    std::vector<std::string> multi_filament_colors;
    if (config.has("presets", "filament_multi_colors")) {
        boost::algorithm::split(multi_filament_colors, config.get("presets", "filament_multi_colors"), boost::algorithm::is_any_of(","));
    }
    if (multi_filament_colors.size() == 0) project_config.option<ConfigOptionStrings>("filament_multi_colour")->values = filament_colors;
    else project_config.option<ConfigOptionStrings>("filament_multi_colour")->values = multi_filament_colors;

    std::vector<std::string> filament_color_types;
    if (config.has("presets", "filament_color_types")) {
        boost::algorithm::split(filament_color_types, config.get("presets", "filament_color_types"), boost::algorithm::is_any_of(","));
    }
    filament_color_types.resize(filament_presets.size(), "1");
    project_config.option<ConfigOptionStrings>("filament_colour_type")->values = filament_color_types;

    std::vector<int> filament_maps(filament_colors.size(), 1);
    project_config.option<ConfigOptionInts>("filament_map")->values = filament_maps;

    std::vector<int> filament_nozzle_maps(filament_colors.size(),0);
    project_config.option<ConfigOptionInts>("filament_nozzle_map")->values = filament_nozzle_maps;

    std::vector<int> filament_volume_maps(filament_colors.size(), static_cast<int>(NozzleVolumeType::nvtStandard));
    project_config.option<ConfigOptionInts>("filament_volume_map")->values = filament_volume_maps;

    std::vector<std::string> extruder_ams_count_str;
    if (config.has("presets", "extruder_ams_count")) {
        boost::algorithm::split(extruder_ams_count_str, config.get("presets", "extruder_ams_count"), boost::algorithm::is_any_of(","));
    }
    this->extruder_ams_counts = get_extruder_ams_count(extruder_ams_count_str);
    this->extruder_ams_selections.clear();

    std::vector<std::string> matrix;
    if (config.has("presets", "flush_volumes_matrix")) {
        boost::algorithm::split(matrix, config.get("presets", "flush_volumes_matrix"), boost::algorithm::is_any_of("|"));
        auto flush_volumes_matrix = matrix | boost::adaptors::transformed(boost::lexical_cast<double, std::string>);
        project_config.option<ConfigOptionFloats>("flush_volumes_matrix")->values = std::vector<double>(flush_volumes_matrix.begin(), flush_volumes_matrix.end());
    }
    if (config.has("presets", "flush_volumes_vector")) {
        boost::algorithm::split(matrix, config.get("presets", "flush_volumes_vector"), boost::algorithm::is_any_of("|"));
        auto flush_volumes_vector = matrix | boost::adaptors::transformed(boost::lexical_cast<double, std::string>);
        project_config.option<ConfigOptionFloats>("flush_volumes_vector")->values = std::vector<double>(flush_volumes_vector.begin(), flush_volumes_vector.end());
    }
    if (config.has("app", "flush_multiplier")) {
        boost::algorithm::split(matrix, config.get("app", "flush_multiplier"), boost::algorithm::is_any_of("|"));
        auto flush_multipliers = matrix | boost::adaptors::transformed(boost::lexical_cast<double, std::string>);
        project_config.option<ConfigOptionFloats>("flush_multiplier")->values = std::vector<double>(flush_multipliers.begin(), flush_multipliers.end());
    }
    if (config.has("app", "flush_multiplier_fast")) {
        boost::algorithm::split(matrix, config.get("app", "flush_multiplier_fast"), boost::algorithm::is_any_of("|"));
        auto flush_multipliers_fast                                                = matrix | boost::adaptors::transformed(boost::lexical_cast<double, std::string>);
        project_config.option<ConfigOptionFloats>("flush_multiplier_fast")->values = std::vector<double>(flush_multipliers_fast.begin(), flush_multipliers_fast.end());
    }

    // Mixed filament metadata
    size_t n_filaments = filament_presets.size();
    if (config.has("presets", "filament_is_mixed")) {
        std::vector<std::string> parts;
        boost::algorithm::split(parts, config.get("presets", "filament_is_mixed"), boost::algorithm::is_any_of(","));
        auto& vals = project_config.option<ConfigOptionBools>("filament_is_mixed")->values;
        vals.clear();
        for (auto& p : parts) vals.push_back(p == "1");
        vals.resize(n_filaments, false);
    }
    if (config.has("presets", "filament_mixed_components")) {
        std::vector<std::string> parts;
        boost::algorithm::split(parts, config.get("presets", "filament_mixed_components"), boost::algorithm::is_any_of("|"));
        parts.resize(n_filaments);
        project_config.option<ConfigOptionStrings>("filament_mixed_components")->values = parts;
    }
    if (config.has("presets", "filament_mixed_sublayer_ratios")) {
        std::vector<std::string> parts;
        boost::algorithm::split(parts, config.get("presets", "filament_mixed_sublayer_ratios"), boost::algorithm::is_any_of("|"));
        parts.resize(n_filaments);
        project_config.option<ConfigOptionStrings>("filament_mixed_sublayer_ratios")->values = parts;
    }
    if (config.has("presets", "filament_mixed_gradient")) {
        std::vector<std::string> parts;
        boost::algorithm::split(parts, config.get("presets", "filament_mixed_gradient"), boost::algorithm::is_any_of(","));
        auto& vals = project_config.option<ConfigOptionBools>("filament_mixed_gradient")->values;
        vals.clear();
        for (auto& p : parts) vals.push_back(p == "1");
        vals.resize(n_filaments, false);
    }
    if (config.has("presets", "filament_mixed_gradient_range")) {
        std::vector<std::string> parts;
        boost::algorithm::split(parts, config.get("presets", "filament_mixed_gradient_range"), boost::algorithm::is_any_of("|"));
        parts.resize(n_filaments);
        project_config.option<ConfigOptionStrings>("filament_mixed_gradient_range")->values = parts;
    }
    {
        // Old projects may not contain filament_mixed_gradient_curve; resize unconditionally.
        auto& vals = project_config.option<ConfigOptionStrings>("filament_mixed_gradient_curve")->values;
        if (config.has("presets", "filament_mixed_gradient_curve")) {
            const std::string raw = config.get("presets", "filament_mixed_gradient_curve");
            // New format: C-style escaped ";" (escape_strings_cstyle), safe for curve values
            // that contain "|" (the intra-slot control-point delimiter). Legacy format used
            // "|" as the inter-slot delimiter, which collided with the intra-slot "|" and
            // split multi-point curves across slots. Disambiguate by size: the new format
            // always round-trips to exactly n_filaments elements; a legacy "|" split of a
            // corrupted curve yields != n_filaments. Invariant: a non-empty curve implies a
            // mixed gradient slot, which requires n_filaments >= 3, so the new format's
            // escaped form always contains ";" and unescapes to size == n_filaments.
            std::vector<std::string> parts;
            if (Slic3r::unescape_strings_cstyle(raw, parts) && parts.size() == n_filaments) {
                vals = std::move(parts);
            } else {
                std::vector<std::string> legacy_parts;
                boost::algorithm::split(legacy_parts, raw, boost::algorithm::is_any_of("|"));
                vals = std::move(legacy_parts);
            }
        }
        vals.resize(n_filaments, std::string{});
        // Heal legacy corruption: clear any non-empty slot that ended up with < 2 points
        // (e.g. a curve split across slots by the old "|" delimiter). Falls back to linear.
        Slic3r::sanitize_mixed_gradient_curve_array(vals);
    }
    {
        // 兜底：旧工程没有 filament_mixed_gradient_per_part 时也保证数组长度与 n_filaments 一致
        auto& vals = project_config.option<ConfigOptionBools>("filament_mixed_gradient_per_part")->values;
        if (config.has("presets", "filament_mixed_gradient_per_part")) {
            std::vector<std::string> parts;
            boost::algorithm::split(parts, config.get("presets", "filament_mixed_gradient_per_part"), boost::algorithm::is_any_of(","));
            vals.clear();
            for (auto& p : parts) vals.push_back(p == "1");
        }
        vals.resize(n_filaments, false);
    }

    // Update visibility of presets based on their compatibility with the active printer.
    // Always try to select a compatible print and filament preset to the current printer preset,
    // as the application may have been closed with an active "external" preset, which does not
    // exist.
    this->update_compatible(PresetSelectCompatibleType::Always);
    this->update_multi_material_filament_presets();

    if (initial_printer != nullptr && (preferred_printer == nullptr || initial_printer == preferred_printer)) {
        // Don't run the following code, as we want to activate default filament / SLA material profiles when installing and selecting a new printer.
        // Only run this code if just a filament / SLA material was installed by Config Wizard for an active Printer.
        auto printer_technology = printers.get_selected_preset().printer_technology();
        if (printer_technology == ptFFF && ! preferred_selection.filament.empty()) {
            std::string preferred_preset_name = get_preset_name_by_alias(Preset::Type::TYPE_FILAMENT, preferred_selection.filament);
            if (auto it = filaments.find_preset_internal(preferred_preset_name);
                it != filaments.end() && (it->name == preferred_preset_name ) && it->is_visible && it->is_compatible) {
                filaments.select_preset_by_name_strict(preferred_preset_name);
                this->filament_presets.front() = filaments.get_selected_preset_name();
            }
        } else if (printer_technology == ptSLA && ! preferred_selection.sla_material.empty()) {
            std::string preferred_preset_name = get_preset_name_by_alias(Preset::Type::TYPE_SLA_MATERIAL, preferred_selection.sla_material);
            if (auto it = sla_materials.find_preset_internal(preferred_preset_name);
                it != sla_materials.end() && it->is_visible && it->is_compatible)
                sla_materials.select_preset_by_name_strict(preferred_preset_name);
        }
    }

    std::string first_visible_filament_name;
    for (auto & fp : filament_presets) {
        // Only replace a slot's preset when it genuinely can no longer be used: the preset was
        // deleted/renamed (end()) or is incompatible with the current printer. A preset that is
        // merely hidden (is_visible == false, e.g. the user just unchecked it in the "system
        // materials" filter) still exists and may still be perfectly usable — hiding only keeps
        // it out of pick lists for *new* selections, it must not evict a slot already using it.
        if (auto it = filaments.find_preset_internal(fp); it == filaments.end() || !it->is_compatible) {
            if (first_visible_filament_name.empty())
                first_visible_filament_name = filaments.first_compatible().name;
            fp = first_visible_filament_name;
        }
    }

    // The history of each printer lives in the app config this bundle loads its selections from.
    nozzle_inventory.set_history(&config);
    nozzle_inventory.select_printer(printers);

    // Parse the initial physical printer name.
    std::string initial_physical_printer_name = remove_ini_suffix(config.get("presets", "physical_printer"));

    // Activate physical printer from the config
    if (!initial_physical_printer_name.empty())
        physical_printers.select_printer(initial_physical_printer_name);

    BOOST_LOG_TRIVIAL(info) << boost::format("finished, preferred printer_model_id %1%")%preferred_selection.printer_model_id;
}

// Export selections (current print, current filaments, current printer) into config.ini
// BBS: change directories by design
void PresetBundle::export_selections(AppConfig &config)
{
	assert(this->printers.get_edited_preset().printer_technology() != ptFFF || filament_presets.size() >= 1);
	//assert(this->printers.get_edited_preset().printer_technology() != ptFFF || filament_presets.size() > 1 || filaments.get_selected_preset_name() == filament_presets.front());
    config.clear_section("presets");
    // clear_section() above wipes the whole presets section, so any key not re-emitted here is lost on save.
    // Re-emit the per-extruder AMS counts from the source-of-truth member (mirrors AMSCountPopupWindow::SetAMSCount).
    config.set("presets", "extruder_ams_count", boost::algorithm::join(save_extruder_ams_count_to_string(this->extruder_ams_counts), ","));
    config.set("presets", PRESET_PRINT_NAME,        prints.get_selected_preset_name());
    config.set("presets", PRESET_FILAMENT_NAME,     filament_presets.front());
    for (unsigned i = 1; i < filament_presets.size(); ++i) {
        char name[64];
        assert(!filament_presets[i].empty());
        sprintf(name, "filament_%02u", i);
        config.set("presets", name, filament_presets[i]);
    }
    // Load project config data into app config
    CNumericLocalesSetter locales_setter;
    std::string           filament_colors = boost::algorithm::join(project_config.option<ConfigOptionStrings>("filament_colour")->values, ",");
    config.set("presets", "filament_colors", filament_colors);

    // Load filament multi color data into app config
    std::string           filament_multi_colors = boost::algorithm::join(project_config.option<ConfigOptionStrings>("filament_multi_colour")->values, ",");
    config.set("presets", "filament_multi_colors", filament_multi_colors);

    // Load filament color type data into app config
    std::string           filament_color_types = boost::algorithm::join(project_config.option<ConfigOptionStrings>("filament_colour_type")->values, ",");
    config.set("presets", "filament_color_types", filament_color_types);

    std::string flush_volumes_matrix = boost::algorithm::join(project_config.option<ConfigOptionFloats>("flush_volumes_matrix")->values |
                                                             boost::adaptors::transformed(static_cast<std::string (*)(double)>(std::to_string)),
                                                         "|");
    config.set("presets", "flush_volumes_matrix", flush_volumes_matrix);
    std::string flush_volumes_vector = boost::algorithm::join(project_config.option<ConfigOptionFloats>("flush_volumes_vector")->values |
                                                             boost::adaptors::transformed(static_cast<std::string (*)(double)>(std::to_string)),
                                                         "|");
    config.set("presets", "flush_volumes_vector", flush_volumes_vector);

    config.set("presets", PRESET_PRINTER_NAME, printers.get_selected_preset_name());

    std::string flush_multiplier_str = boost::algorithm::join(project_config.option<ConfigOptionFloats>("flush_multiplier")->values |
                                                                  boost::adaptors::transformed(static_cast<std::string (*)(double)>(std::to_string)),
                                                              "|");
    config.set("flush_multiplier", flush_multiplier_str);
    std::string flush_multiplier_fast_str = boost::algorithm::join(project_config.option<ConfigOptionFloats>("flush_multiplier_fast")->values |
                                                                       boost::adaptors::transformed(static_cast<std::string (*)(double)>(std::to_string)),
                                                                   "|");
    config.set("flush_multiplier_fast", flush_multiplier_fast_str);


    // Mixed filament metadata
    if (auto* opt = project_config.option<ConfigOptionBools>("filament_is_mixed")) {
        std::string s;
        for (size_t i = 0; i < opt->values.size(); ++i) {
            if (i > 0) s += ",";
            s += (opt->values[i] ? "1" : "0");
        }
        config.set("presets", "filament_is_mixed", s);
    }
    if (auto* opt = project_config.option<ConfigOptionStrings>("filament_mixed_components"))
        config.set("presets", "filament_mixed_components", boost::algorithm::join(opt->values, "|"));
    if (auto* opt = project_config.option<ConfigOptionStrings>("filament_mixed_sublayer_ratios"))
        config.set("presets", "filament_mixed_sublayer_ratios", boost::algorithm::join(opt->values, "|"));
    if (auto* opt = project_config.option<ConfigOptionBools>("filament_mixed_gradient")) {
        std::string s;
        for (size_t i = 0; i < opt->values.size(); ++i) {
            if (i > 0) s += ",";
            s += (opt->values[i] ? "1" : "0");
        }
        config.set("presets", "filament_mixed_gradient", s);
    }
    if (auto* opt = project_config.option<ConfigOptionStrings>("filament_mixed_gradient_range"))
        config.set("presets", "filament_mixed_gradient_range", boost::algorithm::join(opt->values, "|"));
    if (auto* opt = project_config.option<ConfigOptionStrings>("filament_mixed_gradient_curve"))
        // Use C-style escaped ";" encoding (same as ConfigOptionStrings::serialize) instead
        // of "|" join: each slot's curve value may itself contain "|" as the control-point
        // delimiter (serialize_gradient_curve), which would collide with "|" as the inter-
        // slot delimiter and split the curve across slots on the next load_selections.
        config.set("presets", "filament_mixed_gradient_curve", Slic3r::escape_strings_cstyle(opt->values));
    if (auto* opt = project_config.option<ConfigOptionBools>("filament_mixed_gradient_per_part")) {
        std::string s;
        for (size_t i = 0; i < opt->values.size(); ++i) {
            if (i > 0) s += ",";
            s += (opt->values[i] ? "1" : "0");
        }
        config.set("presets", "filament_mixed_gradient_per_part", s);
    }

    // BBS
    //config.set("presets", "sla_print",    sla_prints.get_selected_preset_name());
    //config.set("presets", "sla_material", sla_materials.get_selected_preset_name());
    //config.set("presets", "physical_printer", physical_printers.get_selected_full_printer_name());
    //BBS: add config related log
    BOOST_LOG_TRIVIAL(info) << boost::format("printer %1%, print %2%, filaments[0] %3% ")%printers.get_selected_preset_name() % prints.get_selected_preset_name() %filament_presets[0];
}

// BBS
void PresetBundle::set_num_filaments(unsigned int n, std::string new_color)
{
    if (n > (unsigned int)(EnforcerBlockerType::ExtruderMax))
        n = (unsigned int)(EnforcerBlockerType::ExtruderMax);
    unsigned old_filament_count = this->filament_presets.size();
    if (n > old_filament_count && old_filament_count != 0)
        filament_presets.resize(n, filament_presets.back());
    else {
        filament_presets.resize(n);
    }
    ConfigOptionStrings* filament_color = project_config.option<ConfigOptionStrings>("filament_colour");
    ConfigOptionStrings *filament_multi_color = project_config.option<ConfigOptionStrings>("filament_multi_colour");
    ConfigOptionStrings* filament_color_type = project_config.option<ConfigOptionStrings>("filament_colour_type");
    ConfigOptionInts* filament_map = project_config.option<ConfigOptionInts>("filament_map");
    ConfigOptionInts* filament_nozzle_map = project_config.option<ConfigOptionInts>("filament_nozzle_map");
    ConfigOptionInts* filament_volume_map = project_config.option<ConfigOptionInts>("filament_volume_map");
    ConfigOptionStrings* selected_nozzle_diameters =
        project_config.option<ConfigOptionStrings>("filament_diameter_map");
    // A new slot copies its filament preset from the previous one (filament_presets.resize above), so
    // inherit that slot's nozzle diameter / flow too instead of snapping to the first installed
    // Standard nozzle. Read before the resizes below, which is where the padding value is consumed.
    // validate_filament_nozzle_diameter_selections() at the end repairs the inherited pair when the
    // new slot's filament cannot print that flow (e.g. a 2.85mm TPU needs a TPU285 nozzle).
    // Restricted to the machines whose filament panel exposes the per-slot nozzle selector: elsewhere
    // filament_volume_map is a grouping input/result rather than a user choice worth propagating.
    const bool inherit_nozzle_selection = old_filament_count > 0 &&
        multi_nozzle_diameter_mode_enabled(project_config) &&
        is_single_ext_multi_nozzle(printers.get_edited_preset().config);
    const int inherited_volume = (inherit_nozzle_selection && old_filament_count <= filament_volume_map->values.size())
        ? filament_volume_map->values[old_filament_count - 1]
        : static_cast<int>(NozzleVolumeType::nvtStandard);
    const std::string inherited_diameter = (inherit_nozzle_selection && old_filament_count <= selected_nozzle_diameters->values.size())
        ? selected_nozzle_diameters->values[old_filament_count - 1]
        : std::string();

    filament_color->resize(n);
    filament_multi_color->resize(n);
    filament_color_type->resize(n);
    filament_map->values.resize(n, 1);
    ams_multi_color_filment.resize(n);
    filament_nozzle_map->values.resize(n, 0);
    filament_volume_map->values.resize(n, inherited_volume);
    selected_nozzle_diameters->values.resize(n, inherited_diameter);

    if (auto* opt = project_config.option<ConfigOptionBools>("filament_is_mixed"))
        opt->values.resize(n, false);
    if (auto* opt = project_config.option<ConfigOptionStrings>("filament_mixed_components"))
        opt->values.resize(n, std::string{});
    if (auto* opt = project_config.option<ConfigOptionStrings>("filament_mixed_sublayer_ratios"))
        opt->values.resize(n, std::string{});
    if (auto* opt = project_config.option<ConfigOptionBools>("filament_mixed_gradient"))
        opt->values.resize(n, false);
    if (auto* opt = project_config.option<ConfigOptionStrings>("filament_mixed_gradient_range"))
        opt->values.resize(n, std::string{});
    if (auto* opt = project_config.option<ConfigOptionStrings>("filament_mixed_gradient_curve"))
        opt->values.resize(n, std::string{});
    if (auto* opt = project_config.option<ConfigOptionBools>("filament_mixed_gradient_per_part"))
        opt->values.resize(n, false);

    //BBS set new filament color to new_color
    if (old_filament_count < n) {
        if (!new_color.empty()) {
            for (unsigned i = old_filament_count; i < n; i++) {
                filament_color->values[i] = new_color;
                filament_multi_color->values[i] = new_color;
                filament_color_type->values[i]  = "1";  // default color type
            }
        }
    }

    validate_filament_nozzle_diameter_selections();
    update_multi_material_filament_presets();
}

void PresetBundle::update_num_filaments(unsigned int to_del_flament_id)
{
    unsigned old_filament_count = this->filament_presets.size();
    assert(to_del_flament_id < old_filament_count);
    filament_presets.erase(filament_presets.begin() + to_del_flament_id);

    // update edited_preset
    {
        Preset& edited_preset = filaments.get_edited_preset();
        bool    edited_preset_deleted = true;
        for (std::string filament_preset_name : filament_presets) {
            if (filament_preset_name == edited_preset.name) {
                edited_preset_deleted = false;
            }
        }
        if (edited_preset_deleted) {
            filaments.select_preset_by_name(filament_presets.front(), false);
        }
    }

    ConfigOptionStrings *filament_color = project_config.option<ConfigOptionStrings>("filament_colour");
    ConfigOptionStrings *filament_multi_color = project_config.option<ConfigOptionStrings>("filament_multi_colour");
    ConfigOptionStrings *filament_color_type  = project_config.option<ConfigOptionStrings>("filament_colour_type");
    ConfigOptionInts* filament_map = project_config.option<ConfigOptionInts>("filament_map");
    ConfigOptionStrings* selected_nozzle_diameters =
        project_config.option<ConfigOptionStrings>("filament_diameter_map");
    if (filament_color->values.size() > to_del_flament_id) {
        filament_color->values.erase(filament_color->values.begin() + to_del_flament_id);
        if (filament_map->values.size() > to_del_flament_id) {
            filament_map->values.erase(filament_map->values.begin() + to_del_flament_id);
        }
    }
    else {
        filament_color->values.resize(to_del_flament_id);
        filament_map->values.resize(to_del_flament_id, 1);
    }

    // lambda function to erase or resize the container
    auto erase_or_resize = [to_del_flament_id](auto& container) {
        if (container.size() > to_del_flament_id) {
            container.erase(container.begin() + to_del_flament_id);
        } else {
            container.resize(to_del_flament_id);
        }
    };

    erase_or_resize(filament_multi_color->values);
    erase_or_resize(filament_color_type->values);
    erase_or_resize(selected_nozzle_diameters->values);
    erase_or_resize(ams_multi_color_filment);

    {
        auto* is_mixed_opt = project_config.option<ConfigOptionBools>("filament_is_mixed");
        auto* comp_opt     = project_config.option<ConfigOptionStrings>("filament_mixed_components");
        if (is_mixed_opt && comp_opt) {
            bool del_is_physical = (to_del_flament_id >= is_mixed_opt->values.size()
                                    || !is_mixed_opt->values[to_del_flament_id]);
            if (del_is_physical)
                remap_mixed_components_on_delete(is_mixed_opt->values, comp_opt->values,
                                                 to_del_flament_id + 1);
        }
        if (is_mixed_opt)
            erase_or_resize(is_mixed_opt->values);
        if (comp_opt)
            erase_or_resize(comp_opt->values);
    }
    if (auto* opt = project_config.option<ConfigOptionStrings>("filament_mixed_sublayer_ratios"))
        erase_or_resize(opt->values);
    if (auto* opt = project_config.option<ConfigOptionBools>("filament_mixed_gradient"))
        erase_or_resize(opt->values);
    if (auto* opt = project_config.option<ConfigOptionStrings>("filament_mixed_gradient_range"))
        erase_or_resize(opt->values);
    if (auto* opt = project_config.option<ConfigOptionStrings>("filament_mixed_gradient_curve"))
        erase_or_resize(opt->values);
    if (auto* opt = project_config.option<ConfigOptionBools>("filament_mixed_gradient_per_part"))
        erase_or_resize(opt->values);

    update_multi_material_filament_presets(to_del_flament_id);
}

bool PresetBundle::is_mixed_filament(size_t idx) const
{
    auto* opt = project_config.option<ConfigOptionBools>("filament_is_mixed");
    return opt && idx < opt->values.size() && opt->values[idx];
}

std::vector<size_t> PresetBundle::physical_filament_config_indices() const
{
    std::vector<size_t> indices;
    for (size_t i = 0; i < filament_presets.size(); ++i)
        if (!is_mixed_filament(i))
            indices.push_back(i);
    return indices;
}

size_t PresetBundle::physical_filament_count() const
{
    return filament_presets.size() - mixed_filament_count();
}

size_t PresetBundle::mixed_filament_count() const
{
    size_t count = 0;
    for (size_t i = 0; i < filament_presets.size(); ++i)
        if (is_mixed_filament(i))
            ++count;
    return count;
}

void PresetBundle::get_ams_cobox_infos(AMSComboInfo &combox_info, bool skip_ext)
{
    combox_info.clear();
    std::set<std::pair<std::string, std::string>> added_filaments;
    for (auto &entry : filament_ams_list) {
        auto &ams                  = entry.second;
        auto  filament_id          = ams.opt_string("filament_id", 0u);
        auto  filament_color       = ams.opt_string("filament_colour", 0u);
        auto  ams_name             = ams.opt_string("tray_name", 0u);
        if (ams_name == "Ext" && skip_ext) {
            continue;
        }
        if (skip_ext) {
            auto filament_pair = std::make_pair(ams_name, filament_id);
            if (added_filaments.find(filament_pair) != added_filaments.end()) {
                continue;
            }
            added_filaments.insert(filament_pair);
        }
        auto  filament_changed     = !ams.has("filament_changed") || ams.opt_bool("filament_changed");
        auto  filament_multi_color = ams.opt<ConfigOptionStrings>("filament_multi_colour")->values;
        if (filament_id.empty()) {
            continue;
        }
        if (!filament_changed && this->filament_presets.size() > combox_info.ams_filament_presets.size()) {
            combox_info.ams_filament_presets.push_back(this->filament_presets[combox_info.ams_filament_presets.size()]);
            combox_info.ams_filament_colors.push_back(filament_color);
            combox_info.ams_multi_color_filment.push_back(filament_multi_color);
            combox_info.ams_names.push_back(ams_name);
            continue;
        }
        auto iter = std::find_if(filaments.begin(), filaments.end(),
                                 [this, &filament_id](auto &f) { return f.is_compatible && filaments.get_preset_base(f) == &f && f.filament_id == filament_id; });
        if (iter == filaments.end()) {
            BOOST_LOG_TRIVIAL(warning) << boost::format("filament_id %1% not found or system or compatible") % filament_id;
            auto filament_type = ams.opt_string("filament_type", 0u);
            if (!filament_type.empty()) {
                filament_type = "Generic " + filament_type;
                iter          = std::find_if(filaments.begin(), filaments.end(),
                                    [&filament_type](auto &f) { return f.is_compatible && f.is_system && boost::algorithm::starts_with(f.name, filament_type); });
            }
            if (iter == filaments.end()) {
                // Prefer old selection
                if (combox_info.ams_filament_presets.size() < this->filament_presets.size()) {
                    combox_info.ams_filament_presets.push_back(this->filament_presets[combox_info.ams_filament_presets.size()]);
                    combox_info.ams_filament_colors.push_back(filament_color);
                    combox_info.ams_multi_color_filment.push_back(filament_multi_color);
                    combox_info.ams_names.push_back(ams_name);
                    continue;
                }
                iter = std::find_if(filaments.begin(), filaments.end(), [&filament_type](auto &f) { return f.is_compatible && f.is_system; });
                if (iter == filaments.end())
                    continue;
            }
            filament_id = iter->filament_id;
        }
        combox_info.ams_filament_presets.push_back(iter->name);
        combox_info.ams_filament_colors.push_back(filament_color);
        combox_info.ams_multi_color_filment.push_back(filament_multi_color);
        combox_info.ams_names.push_back(ams_name);
    }
}

namespace {

// One AMS tray as seen by sync_ams_list, plus the side effects that have to wait until we know
// whether the tray actually reaches the project filament list.
struct AmsInfo
{
    bool                     valid{false};
    bool                     is_map{false};
    std::string              filament_color      = "";
    std::string              filament_color_type = "";
    std::string              filament_preset     = "";
    std::vector<std::string> mutli_filament_color;
    // The tray config this entry came from, i.e. the same pointer stored in `unknowns`.
    const DynamicPrintConfig *source{nullptr};
    // Preset to enable once the tray is known to be synced. Null when the tray reuses a preset
    // that is already in the project filament list.
    Preset *install_preset{nullptr};
    AMSMapInfo mapping;
};

// Apply the side effects the first pass over the AMS trays deferred. A tray only earns its preset
// install and its "unknown filament" warning once it has actually reached the project filament
// list: trays excluded by "add unused filaments to filaments list" (or by the duplicate and slot
// count limits) would otherwise install a preset the user declined, and be named in a warning
// about a filament that was never synced.
// Precondition: the caller must not move or free the presets referenced by
// `ams_infos[].install_preset`/`.source` between the deferring pass and this call. Both are raw
// pointers into live containers, so a reallocation in between would leave them dangling.
void apply_synced_tray_side_effects(const std::vector<AmsInfo>                                &ams_infos,
                                    const std::set<const DynamicPrintConfig *>                &unsynced,
                                    std::vector<std::pair<DynamicPrintConfig *, std::string>> &unknowns)
{
    for (const auto &info : ams_infos) {
        if (info.install_preset && unsynced.count(info.source) == 0) info.install_preset->is_visible = true;
    }

    if (unsynced.empty()) return;

    unknowns.erase(std::remove_if(unknowns.begin(), unknowns.end(),
                                  [&unsynced](const std::pair<DynamicPrintConfig *, std::string> &unknown) { return unsynced.count(unknown.first) > 0; }),
                   unknowns.end());
}

} // namespace

unsigned int PresetBundle::sync_ams_list(std::vector<std::pair<DynamicPrintConfig *, std::string>> &unknowns,
                                         bool                                                       use_map,
                                         std::map<int, AMSMapInfo> &                                maps,
                                         bool                                                       enable_append,
                                         MergeFilamentInfo &                                        merge_info,
                                         bool                                                       skip_ext)
{
    BOOST_LOG_TRIVIAL(info) << "use_map:" << use_map << " enable_append:" << enable_append;
    ams_sync_substitutions.clear();
    std::vector<std::string> ams_filament_presets;
    std::vector<std::string> ams_filament_colors;
    std::vector<std::string> ams_filament_color_types;
    std::vector<AMSMapInfo>  ams_array_maps;
    std::vector<AMSMapInfo>  synced_ams_maps;
    ams_multi_color_filment.clear();
    BOOST_LOG_TRIVIAL(info) << boost::format("filament_ams_list size: %1%") % filament_ams_list.size();
    std::vector<AmsInfo> ams_infos;
    std::set<std::pair<std::string, std::string>> added_filaments;
    for (auto &entry : filament_ams_list) {
        auto & ams = entry.second;
        auto filament_id = ams.opt_string("filament_id", 0u);
        auto tray_name = ams.opt_string("tray_name", 0u);
        if (tray_name == "Ext" && skip_ext) {
            continue;
        }
        if (skip_ext) {
            auto filament_pair = std::make_pair(tray_name, filament_id);
            if (added_filaments.find(filament_pair) != added_filaments.end()) {
                continue;
            }
            added_filaments.insert(filament_pair);
        }
        auto filament_color = ams.opt_string("filament_colour", 0u);
        auto filament_color_type = ams.opt_string("filament_colour_type", 0u);
        auto filament_changed = !ams.has("filament_changed") || ams.opt_bool("filament_changed");
        auto filament_multi_color = ams.opt<ConfigOptionStrings>("filament_multi_colour")->values;
        auto ams_id     = ams.opt_string("ams_id", 0u);
        auto slot_id    = ams.opt_string("slot_id", 0u);
        ams_infos.push_back({filament_id.empty() ? false : true,false, filament_color});
        ams_infos.back().source = &ams;
        AMSMapInfo temp = {ams_id, slot_id};
        if (const auto *tray_diameters = ams.option<ConfigOptionFloats>("tray_nozzle_diameter");
            tray_diameters != nullptr && !tray_diameters->values.empty() && tray_diameters->values.front() > 0.)
            temp.nozzle_diameter = format_diameter_to_str(tray_diameters->values.front());
        // Diameter alone does not name a nozzle on a rack carrying the same diameter in two flows,
        // and the slot keeps whichever flow it already had -- so a tray on a high-flow nozzle synced
        // onto a slot sitting at standard flow silently stayed standard. Take the flow here too.
        if (const auto *tray_volume_types = ams.option<ConfigOptionInts>("tray_nozzle_volume_type");
            tray_volume_types != nullptr && !tray_volume_types->values.empty())
            temp.nozzle_volume_type = static_cast<NozzleVolumeType>(tray_volume_types->values.front());
        ams_infos.back().mapping = temp;
        ams_array_maps.push_back(temp);
        if (filament_id.empty()) {
            if (use_map) {
                for (int j = maps.size() - 1; j >= 0; j--) {
                    if (maps[j].slot_id == slot_id && maps[j].ams_id == ams_id) {
                        maps.erase(j);
                    }
                }
                ams_filament_presets.push_back("Generic PLA");//for unknow matieral
                auto default_unknown_color = "#CECECE";
                ams_filament_colors.push_back(default_unknown_color);
                ams_filament_color_types.push_back("1");
                if (filament_multi_color.size() == 0) {
                    filament_multi_color.push_back(default_unknown_color);
                }
                ams_multi_color_filment.push_back(filament_multi_color);
            }
            continue;
        }
        if (!filament_changed && this->filament_presets.size() > ams_filament_presets.size()) {
            ams_filament_presets.push_back(this->filament_presets[ams_filament_presets.size()]);
            ams_filament_colors.push_back(filament_color);
            ams_filament_color_types.push_back(filament_color_type);
            ams_multi_color_filment.push_back(filament_multi_color);
            synced_ams_maps.push_back(temp);
            continue;
        }
        bool has_type = false;
        auto filament_type = ams.opt_string("filament_type", 0u);
        auto iter = std::find_if(filaments.begin(), filaments.end(), [this, &filament_id, &has_type, filament_type](auto &f) {
            has_type |= f.config.opt_string("filament_type", 0u) == filament_type;
            return f.is_compatible && filaments.get_preset_base(f) == &f && f.filament_id == filament_id; });
        if (iter == filaments.end()) {
            // Why it failed, not just that it did: the three conditions above fail for very different
            // reasons (no such material locally / the material exists but not for the printer in use /
            // only non-base variants) and the message is the same for all of them.
            int         presets_with_id = 0, incompatible = 0, non_base = 0;
            std::string incompatible_sample;
            for (auto it = filaments.begin(); it != filaments.end(); ++it) {
                if (it->filament_id != filament_id)
                    continue;
                ++presets_with_id;
                if (!it->is_compatible) {
                    ++incompatible;
                    if (incompatible_sample.empty())
                        incompatible_sample = it->name;
                } else if (filaments.get_preset_base(*it) != &*it) {
                    ++non_base;
                }
            }
            BOOST_LOG_TRIVIAL(info) << "tray " << tray_name << " type=" << filament_type << " " << KV(log_key::filament_id, filament_id)
                                    << "-> no usable preset. presets_with_this_id=" << presets_with_id << " of_which_incompatible=" << incompatible
                                    << " non_base=" << non_base << " first_incompatible=\"" << incompatible_sample << "\" active_printer=\""
                                    << printers.get_edited_preset().name << "\" printer_variant="
                                    << printers.get_edited_preset().config.opt_string("printer_variant");
            BOOST_LOG_TRIVIAL(info) << boost::format("filament_id %1% not found or system or compatible") % filament_id;
            // Every stand-in picked below is approximate by definition -- it is chosen by material type,
            // not by the tray's own id. Which slot a tray finally lands on is only known after the
            // append/overwrite merge, so the slot is flagged at the end of this function rather than here.
            if (!filament_type.empty()) {
                filament_type = "Generic " + filament_type;
                iter = std::find_if(filaments.begin(), filaments.end(), [&filament_type](auto &f) {
                    return f.is_compatible && f.is_system
                        && boost::algorithm::starts_with(f.name, filament_type);
                });
            }
            if (iter == filaments.end()) {
                // Prefer old selection
                if (ams_filament_presets.size() < this->filament_presets.size()) {
                    ams_filament_presets.push_back(this->filament_presets[ams_filament_presets.size()]);
                    ams_filament_colors.push_back(filament_color);
                    ams_filament_color_types.push_back(filament_color_type);
                    ams_multi_color_filment.push_back(filament_multi_color);
                    synced_ams_maps.push_back(temp);
                    unknowns.emplace_back(&ams, has_type ? L("The filament may not be compatible with the current machine settings. Generic filament presets will be used.") :
                                                           L("The filament model is unknown. Still using the previous filament preset."));
                    continue;
                }
                iter = std::find_if(filaments.begin(), filaments.end(), [](auto &f) {
                    return f.is_compatible && f.is_system;
                });
                if (iter == filaments.end())
                    continue;
            }
            unknowns.emplace_back(&ams, boost::algorithm::starts_with(iter->name, filament_type) ?
                                            (has_type ? L("The filament may not be compatible with the current machine settings. Generic filament presets will be used.") :
                                                        L("The filament model is unknown. Generic filament presets will be used.")) :
                                            (has_type ? L("The filament may not be compatible with the current machine settings. A random filament preset will be used.") :
                                                        L("The filament model is unknown. A random filament preset will be used.")));
            filament_id = iter->filament_id;
        }
        // A filament that the machine actually uses should be enabled (installed)
        // in Studio even if the user never ticked it in the filament preferences,
        // otherwise it stays hidden in the filament settings dropdown after sync.
        // Deferred to apply_synced_tray_side_effects: a tray that the mapping pass below
        // drops was never synced, so installing its preset here would enable a filament
        // the user asked not to add.
        ams_infos.back().install_preset = &(*iter);
        ams_filament_presets.push_back(iter->name);
        ams_filament_colors.push_back(filament_color);
        ams_filament_color_types.push_back(filament_color_type);
        ams_multi_color_filment.push_back(filament_multi_color);
        synced_ams_maps.push_back(temp);
    }
    if (ams_filament_presets.empty())
        return 0;
    BOOST_LOG_TRIVIAL(info) << "get filament_colour and from config";
    ConfigOptionStrings *filament_color = project_config.option<ConfigOptionStrings>("filament_colour");
    ConfigOptionStrings *filament_color_type = project_config.option<ConfigOptionStrings>("filament_colour_type");
    ConfigOptionInts *   filament_map = project_config.option<ConfigOptionInts>("filament_map");

    // Snapshot and temporarily strip mixed filament slots so AMS sync
    // operates on physical filaments only, preserving "physical-first" ordering.
    struct MixedSlotSnapshot {
        std::string preset;
        std::string color;
        std::string color_type;
        std::string mixed_components;
        std::string mixed_sublayer_ratios;
        bool        mixed_gradient = false;
        std::string mixed_gradient_range;
        std::string mixed_gradient_curve;
        bool        mixed_gradient_per_part = false;
    };
    std::vector<MixedSlotSnapshot> mixed_snapshots;
    auto* is_mixed_opt         = project_config.option<ConfigOptionBools>("filament_is_mixed");
    auto* mixed_comp_opt       = project_config.option<ConfigOptionStrings>("filament_mixed_components");
    auto* mixed_ratios_opt     = project_config.option<ConfigOptionStrings>("filament_mixed_sublayer_ratios");
    auto* mixed_gradient_opt   = project_config.option<ConfigOptionBools>("filament_mixed_gradient");
    auto* mixed_grad_range_opt = project_config.option<ConfigOptionStrings>("filament_mixed_gradient_range");
    auto* mixed_grad_curve_opt = project_config.option<ConfigOptionStrings>("filament_mixed_gradient_curve");
    auto* mixed_per_part_opt   = project_config.option<ConfigOptionBools>("filament_mixed_gradient_per_part");
    if (is_mixed_opt) {
        for (size_t i = 0; i < is_mixed_opt->values.size() && i < this->filament_presets.size(); ++i) {
            if (!is_mixed_opt->values[i])
                continue;
            MixedSlotSnapshot snap;
            snap.preset     = this->filament_presets[i];
            snap.color      = (i < filament_color->values.size())      ? filament_color->values[i]      : "";
            snap.color_type = (i < filament_color_type->values.size()) ? filament_color_type->values[i] : "";
            if (mixed_comp_opt     && i < mixed_comp_opt->values.size())     snap.mixed_components      = mixed_comp_opt->values[i];
            if (mixed_ratios_opt   && i < mixed_ratios_opt->values.size())   snap.mixed_sublayer_ratios = mixed_ratios_opt->values[i];
            if (mixed_gradient_opt && i < mixed_gradient_opt->values.size()) snap.mixed_gradient        = mixed_gradient_opt->values[i];
            if (mixed_grad_range_opt && i < mixed_grad_range_opt->values.size()) snap.mixed_gradient_range = mixed_grad_range_opt->values[i];
            if (mixed_grad_curve_opt && i < mixed_grad_curve_opt->values.size()) snap.mixed_gradient_curve = mixed_grad_curve_opt->values[i];
            if (mixed_per_part_opt && i < mixed_per_part_opt->values.size()) snap.mixed_gradient_per_part = mixed_per_part_opt->values[i];
            mixed_snapshots.push_back(snap);
        }
        if (!mixed_snapshots.empty()) {
            BOOST_LOG_TRIVIAL(info) << "stripping " << mixed_snapshots.size() << " mixed filament slot(s) before AMS sync";
            size_t phys_count = this->filament_presets.size() - mixed_snapshots.size();
            this->filament_presets.resize(phys_count);
            filament_color->values.resize(phys_count);
            filament_color_type->values.resize(phys_count);
            filament_map->values.resize(phys_count, 1);
            is_mixed_opt->values.resize(phys_count);
            if (mixed_comp_opt)       mixed_comp_opt->values.resize(phys_count);
            if (mixed_ratios_opt)     mixed_ratios_opt->values.resize(phys_count);
            if (mixed_gradient_opt)   mixed_gradient_opt->values.resize(phys_count);
            if (mixed_grad_range_opt) mixed_grad_range_opt->values.resize(phys_count);
            if (mixed_grad_curve_opt) mixed_grad_curve_opt->values.resize(phys_count);
            if (mixed_per_part_opt)   mixed_per_part_opt->values.resize(phys_count);
        }
    }

    // The project's materials as they stand going into this sync, indexed by physical slot (the mixed
    // slots have just been stripped, so these indices line up with the ones `maps` ends up using).
    // Consulted at the end to tell a slot the user already configured apart from one this sync appends.
    const std::vector<std::string> presets_before_sync = this->filament_presets;

    // AMS trays that end up excluded from the project filament list, so their deferred side
    // effects must not be applied. Only the mapping branch can exclude a tray; the overwrite
    // branch takes every synced tray, so the set stays empty there.
    std::set<const DynamicPrintConfig *> unsynced_trays;

    if (use_map) {
        auto check_has_merge_info = [](std::map<int, AMSMapInfo> &maps, MergeFilamentInfo &merge_info, int exist_colors_size) {
            std::set<int> done;
            for (auto it_i = maps.begin(); it_i != maps.end(); ++it_i) {
                std::vector<int> same_ams;
                same_ams.emplace_back(it_i->first);
                for (auto it_j = std::next(it_i); it_j != maps.end(); ++it_j) {
                    if (done.find(it_j->first) != done.end()) {
                        continue;
                    }
                    if (it_i->second.slot_id == "" || it_i->second.ams_id == ""){
                        continue;
                    }
                    if (it_i->second.slot_id == it_j->second.slot_id && it_i->second.ams_id == it_j->second.ams_id) {
                        same_ams.emplace_back(it_j->first);
                        done.insert(it_j->first);
                    }
                }
                if (same_ams.size() > 1) {
                    merge_info.merges.emplace_back(same_ams);
                }
            }
        };
        check_has_merge_info(maps, merge_info,filament_color->values.size());
        auto get_map_index = [&ams_infos](const std::vector<AMSMapInfo> &infos, const AMSMapInfo &temp) {
            for (int i = 0; i < infos.size(); i++) {
                const bool diameter_matches = temp.nozzle_diameter.empty() || infos[i].nozzle_diameter.empty() ||
                    std::abs(string_to_double_decimal_point(infos[i].nozzle_diameter.c_str()) - string_to_double_decimal_point(temp.nozzle_diameter.c_str())) < 1e-6;
                if (infos[i].slot_id == temp.slot_id && infos[i].ams_id == temp.ams_id && diameter_matches) {
                    ams_infos[i].is_map = true;
                    return i;
                }
            }
            return -1;
        };
        std::vector<AmsInfo> need_append_colors;
        auto exist_colors = filament_color->values;
        auto exist_color_types = filament_color_type->values;
        auto exist_filament_presets = this->filament_presets;
        std::vector<std::vector<std::string>> exist_multi_color_filment;
        exist_multi_color_filment.resize(exist_colors.size());
        for (int i = 0; i < exist_colors.size(); i++) {
            exist_multi_color_filment[i] = {exist_colors[i]};
        }
        for (size_t i = 0; i < exist_colors.size(); i++) {
            if (maps.find(i) != maps.end()) {//mapping exist
                auto valid_index = get_map_index(ams_array_maps, maps[i]);
                if (valid_index >= 0 && valid_index < ams_filament_presets.size()) {
                    maps[i].nozzle_diameter    = ams_array_maps[valid_index].nozzle_diameter;
                    maps[i].nozzle_volume_type = ams_array_maps[valid_index].nozzle_volume_type;
                    exist_colors[i]           = ams_filament_colors[valid_index];
                    exist_color_types[i]      = ams_filament_color_types[valid_index];
                    exist_filament_presets[i] = ams_filament_presets[valid_index];
                    exist_multi_color_filment[i] = ams_multi_color_filment[valid_index];
                } else {
                    BOOST_LOG_TRIVIAL(error) << "check error: array bound (mapping exist)";
                }
            }
        }
        for (size_t i = 0; i < ams_infos.size(); i++) {// check append
            if (ams_infos[i].valid) {
                if (i >= ams_filament_presets.size()) {
                    BOOST_LOG_TRIVIAL(error) << "check error: array bound (check append)";
                    continue;
                }
                ams_infos[i].filament_color_type  = ams_filament_color_types[i];
                ams_infos[i].filament_preset = ams_filament_presets[i];
                ams_infos[i].mutli_filament_color = ams_multi_color_filment[i];
                if (!ams_infos[i].is_map) {
                    need_append_colors.emplace_back(ams_infos[i]);
                    // Unmapped: only reaches the list if it is appended below.
                    unsynced_trays.insert(ams_infos[i].source);
                    ams_filament_colors[i]     = "";
                    ams_filament_color_types[i] = "";
                    ams_filament_presets[i]    = "";
                    ams_multi_color_filment[i] = std::vector<std::string>();
                }
            }
            else {
                ams_filament_colors[i]     = "";
                ams_filament_color_types[i] = "";
                ams_filament_presets[i]    = "";
                ams_multi_color_filment[i] = std::vector<std::string>();
            }
        }
        //delete redundant color
        ams_filament_colors.erase(std::remove_if(ams_filament_colors.begin(), ams_filament_colors.end(), [](std::string &value) { return value.empty(); }),
                                  ams_filament_colors.end());
        ams_filament_color_types.erase(std::remove_if(ams_filament_color_types.begin(), ams_filament_color_types.end(), [](std::string &value) { return value.empty(); }),
                                       ams_filament_color_types.end());
        ams_filament_presets.erase(std::remove_if(ams_filament_presets.begin(), ams_filament_presets.end(), [](std::string &value) { return value.empty(); }),
                                   ams_filament_presets.end());
        ams_multi_color_filment.erase(std::remove_if(ams_multi_color_filment.begin(), ams_multi_color_filment.end(),
                                                     [](std::vector<std::string> &value) { return value.empty(); }),
                                      ams_multi_color_filment.end());
        if (need_append_colors.size() > 0 && enable_append) {
            BOOST_LOG_TRIVIAL(info) << "need_append_colors.size() > 0 && enable_append";
            auto get_idx_in_array = [](std::vector<std::string> &presets, std::vector<std::string> &colors, const std::string &preset, const std::string &color) -> int {
                for (size_t i = 0; i < presets.size(); i++) {
                    if (presets[i] == preset && colors[i] == color) {
                        return i;
                    }
                }
                return -1;
            };
            for (size_t i = 0; i < need_append_colors.size(); i++){
                // Mixed slots are stripped here and re-appended after the sync, so exist_filament_presets holds
                // physical filaments only; the total still has to leave room for the mixed ones.
                if (exist_filament_presets.size() >= MAX_PHYSICAL_FILAMENT_COUNT ||
                    exist_filament_presets.size() + mixed_snapshots.size() >= MAX_FILAMENT_COUNT) {
                    break;
                }
                auto idx = get_idx_in_array(exist_filament_presets, exist_colors, need_append_colors[i].filament_preset, need_append_colors[i].filament_color);
                // Represented in the final list either way: appended just below, or already
                // present as an identical preset and colour.
                unsynced_trays.erase(need_append_colors[i].source);
                if (idx >= 0) {
                    if (maps.find(idx) == maps.end())
                        maps[idx] = need_append_colors[i].mapping;
                    continue;
                }
                maps[int(exist_filament_presets.size())] = need_append_colors[i].mapping;
                exist_filament_presets.push_back(need_append_colors[i].filament_preset);
                exist_colors.push_back(need_append_colors[i].filament_color);
                exist_color_types.push_back(need_append_colors[i].filament_color_type);
                exist_multi_color_filment.push_back(need_append_colors[i].mutli_filament_color);
            }
        }
        filament_color->values = exist_colors;
        filament_color_type->values = exist_color_types;
        ams_multi_color_filment = exist_multi_color_filment;
        this->filament_presets = exist_filament_presets;
        filament_map->values.resize(exist_filament_presets.size(), 1);
    } else { // overwrite;
        filament_color->values = ams_filament_colors;
        filament_color_type->values = ams_filament_color_types;
        this->filament_presets = ams_filament_presets;
        filament_map->values.resize(ams_filament_colors.size(), 1);
        maps.clear();
        for (size_t i = 0; i < synced_ams_maps.size(); ++i)
            maps[int(i)] = synced_ams_maps[i];

        auto& print_config = this->prints.get_edited_preset().config;
        auto  support_filament_opt = print_config.option<ConfigOptionInt>("support_filament");
        auto support_interface_filament_opt = print_config.option<ConfigOptionInt>("support_interface_filament");
        if (support_filament_opt->value > ams_filament_color_types.size())
            support_filament_opt->value = 0;

        if (support_interface_filament_opt->value > ams_filament_color_types.size())
            support_interface_filament_opt->value = 0;
    }


    apply_synced_tray_side_effects(ams_infos, unsynced_trays, unknowns);

    // Flag every already-configured slot whose tray had no matching preset, and give its material back.
    // The pass above could only seed such a slot with a "Generic <type>" stand-in, an arbitrary
    // compatible preset, or -- when not even those existed -- the slot's own preset left untouched.
    // All three mean the same thing to the user: the machine is feeding that slot from a tray the
    // filament list does not represent. Losing a material they deliberately set is worse than admitting
    // the tray is unrecognised, and saying nothing at all is worse still. Done only here because the
    // slot a tray lands on is decided by the append/overwrite merge (trays get reordered, dropped, or
    // appended past the end), and `maps` is that finished slot -> tray binding. `unknowns` has already
    // had the trays that never reached the filament list removed.
    for (const auto &unknown : unknowns) {
        const std::string ams_id  = unknown.first->opt_string("ams_id", 0u);
        const std::string slot_id = unknown.first->opt_string("slot_id", 0u);
        for (const auto &mapping : maps) {
            if (mapping.first < 0 || mapping.second.ams_id != ams_id || mapping.second.slot_id != slot_id)
                continue;
            const size_t slot = static_cast<size_t>(mapping.first);
            // A slot this sync appended did not exist before it, so there was no earlier choice of the
            // user's for the stand-in to be standing in for -- leave it alone and stay quiet about it.
            if (slot >= presets_before_sync.size() || slot >= this->filament_presets.size())
                continue;
            // The stand-in stays. It is the same fallback the AMS dropdown offers for this tray, so
            // sync and that list now agree on what this tray maps to; the user is told about the swap
            // below instead of finding the slot silently reverted to whatever was there before.
            if (std::any_of(ams_sync_substitutions.begin(), ams_sync_substitutions.end(),
                            [slot](const AmsSyncSubstitution &s) { return s.slot == slot; }))
                continue;
            AmsSyncSubstitution record;
            record.slot = slot;
            record.tray = unknown.first->opt_string("tray_name", 0u);
            // What the tray actually holds. Its presets are all incompatible with this printer -- that
            // is why we are here -- so the name is read off any of them rather than from a usable one.
            const std::string tray_filament_id = unknown.first->opt_string("filament_id", 0u);
            for (auto it = filaments.begin(); it != filaments.end(); ++it)
                if (!tray_filament_id.empty() && it->filament_id == tray_filament_id) {
                    record.wanted = it->alias.empty() ? it->name : it->alias;
                    break;
                }
            if (record.wanted.empty())
                record.wanted = unknown.first->opt_string("filament_type", 0u);
            // Whatever the slot ends up on, including the case where no stand-in was found and it
            // simply kept what it had: from the user's side the tray's material was not applied and
            // this is what got used instead, which is the same sentence either way.
            record.applied = this->filament_presets[slot];
            // The display name, to match `wanted` -- the stored name carries a printer/nozzle suffix
            // that says nothing the user needs here.
            if (const Preset *applied = filaments.find_preset(record.applied, false))
                record.applied = applied->alias.empty() ? applied->name : applied->alias;
            BOOST_LOG_TRIVIAL(info) << "ams sync substituted slot " << record.slot
                                    << " tray=" << record.tray << " wanted=" << record.wanted
                                    << " applied=" << (record.applied.empty() ? "<kept>" : record.applied);
            ams_sync_substitutions.push_back(std::move(record));
        }
    }
    // Filled in tray order, but it is read out as a list of slot numbers for the user.
    std::sort(ams_sync_substitutions.begin(), ams_sync_substitutions.end(),
              [](const AmsSyncSubstitution &a, const AmsSyncSubstitution &b) { return a.slot < b.slot; });

    // Re-append mixed filament slots that were stripped before AMS sync
    if (!mixed_snapshots.empty()) {
        BOOST_LOG_TRIVIAL(info) << "re-appending " << mixed_snapshots.size() << " mixed filament slot(s) after AMS sync";
        size_t new_phys_count = this->filament_presets.size();
        if (is_mixed_opt)         is_mixed_opt->values.resize(new_phys_count, (unsigned char)false);
        if (mixed_comp_opt)       mixed_comp_opt->values.resize(new_phys_count);
        if (mixed_ratios_opt)     mixed_ratios_opt->values.resize(new_phys_count);
        if (mixed_gradient_opt)   mixed_gradient_opt->values.resize(new_phys_count, (unsigned char)false);
        if (mixed_grad_range_opt) mixed_grad_range_opt->values.resize(new_phys_count);
        if (mixed_grad_curve_opt) mixed_grad_curve_opt->values.resize(new_phys_count);
        if (mixed_per_part_opt)   mixed_per_part_opt->values.resize(new_phys_count, (unsigned char)false);

        for (auto& snap : mixed_snapshots) {
            this->filament_presets.push_back(snap.preset);
            filament_color->values.push_back(snap.color);
            filament_color_type->values.push_back(snap.color_type);
            ams_multi_color_filment.push_back({snap.color});
            filament_map->values.push_back(1);
            if (is_mixed_opt)         is_mixed_opt->values.push_back((unsigned char)true);
            if (mixed_comp_opt)       mixed_comp_opt->values.push_back(snap.mixed_components);
            if (mixed_ratios_opt)     mixed_ratios_opt->values.push_back(snap.mixed_sublayer_ratios);
            if (mixed_gradient_opt)   mixed_gradient_opt->values.push_back((unsigned char)snap.mixed_gradient);
            if (mixed_grad_range_opt) mixed_grad_range_opt->values.push_back(snap.mixed_gradient_range);
            if (mixed_grad_curve_opt) mixed_grad_curve_opt->values.push_back(snap.mixed_gradient_curve);
            if (mixed_per_part_opt)   mixed_per_part_opt->values.push_back((unsigned char)snap.mixed_gradient_per_part);
        }
    }

    // Update ams_multi_color_filment
    update_filament_multi_color();
    update_multi_material_filament_presets();
    BOOST_LOG_TRIVIAL(info) << "finish sync ams list";
    return this->filament_presets.size();
}

void PresetBundle::update_filament_multi_color()
{
    std::vector<std::string> exsit_multi_colors;
    for (auto &fil_item : ams_multi_color_filment){
        if (fil_item.empty()) break;
        if (fil_item.size() == 1)
            exsit_multi_colors.push_back(fil_item[0]);
        else {
            std::string colors = "";
            for (auto &color : fil_item){
                   colors += color + " ";
            }
            colors.erase(colors.size() - 1); // remove last space
            exsit_multi_colors.push_back(colors);
        }
    }
    ConfigOptionStrings *filament_multi_colour = project_config.option<ConfigOptionStrings>("filament_multi_colour");
    filament_multi_colour->resize(exsit_multi_colors.size());
    filament_multi_colour->values = exsit_multi_colors;
}

std::vector<int> PresetBundle::get_used_tpu_filaments(const std::vector<int> &used_filaments)
{
    std::vector<int> tpu_filaments;
    for (size_t i = 0; i < this->filament_presets.size(); ++i) {
        auto iter = std::find(used_filaments.begin(), used_filaments.end(), i + 1);
        if (iter == used_filaments.end()) continue;

        std::string filament_name = this->filament_presets[i];
        for (int f_index = 0; f_index < this->filaments.size(); f_index++) {
            PresetCollection *filament_presets = &this->filaments;
            Preset           *preset           = &filament_presets->preset(f_index);
            int               size             = this->filaments.size();
            if (preset && filament_name.compare(preset->name) == 0) {
                std::string display_filament_type;
                std::string filament_type = preset->config.get_filament_type(display_filament_type);
                if (display_filament_type == "TPU") {
                    tpu_filaments.push_back(i);
                }
            }
        }
    }
    return tpu_filaments;
}

void PresetBundle::set_calibrate_printer(std::string name)
{
    if (name.empty()) {
        calibrate_filaments.clear();
        return;
    }
    if (!name.empty())
        calibrate_printer = printers.find_preset(name);
    const Preset &                printer_preset = calibrate_printer ? *calibrate_printer : printers.get_edited_preset();
    const PresetWithVendorProfile active_printer = printers.get_preset_with_vendor_profile(printer_preset);
    DynamicPrintConfig            config;
    config.set_key_value("printer_preset", new ConfigOptionString(active_printer.preset.name));
    const ConfigOption *opt = active_printer.preset.config.option("nozzle_diameter");
    if (opt) config.set_key_value("num_extruders", new ConfigOptionInt((int) static_cast<const ConfigOptionFloatsNullable *>(opt)->values.size()));
    calibrate_filaments.clear();
    for (size_t i = filaments.num_default_presets(); i < filaments.size(); ++i) {
        const Preset &                preset                          = filaments.m_presets[i];
        const PresetWithVendorProfile this_preset_with_vendor_profile = filaments.get_preset_with_vendor_profile(preset);
        bool                          is_compatible                   = is_compatible_with_printer(this_preset_with_vendor_profile, active_printer, &config);
        if (is_compatible) calibrate_filaments.insert(&preset);
    }
}

std::vector<std::vector<DynamicPrintConfig>> PresetBundle::get_extruder_filament_info() const
{
    std::vector<std::vector<DynamicPrintConfig>> filament_infos;
    int extruder_nums = get_printer_extruder_count();
    if (extruder_nums > 1) {
        filament_infos.resize(extruder_nums, std::vector<DynamicPrintConfig>());
        for (auto ams_item : filament_ams_list) {
            if (ams_item.first & 0x10000) { // right
                filament_infos[1].push_back(ams_item.second);
            } else { // left
                filament_infos[0].push_back(ams_item.second);
            }
        }
    }
    return filament_infos;
}

std::set<std::string> PresetBundle::get_printer_names_by_printer_type_and_nozzle(const std::string &printer_type, std::string nozzle_diameter_str, bool system_only)
{
    BOOST_LOG_TRIVIAL(info) << "printer_type: " << printer_type << "nozzle_diameter_str" << nozzle_diameter_str;
    std::set<std::string> printer_names;

    /* unknown or empty printer type */
    if (printer_type.empty())
        return printer_names;

    if ("0.0" == nozzle_diameter_str || nozzle_diameter_str.empty()) {
        nozzle_diameter_str = "0.4";
    }
    std::ostringstream    stream;

    for (auto printer_it = this->printers.begin(); printer_it != this->printers.end(); printer_it++) {
        if (system_only && !printer_it->is_system) continue;

        ConfigOption *      printer_model_opt = printer_it->config.option("printer_model");
        ConfigOptionString *printer_model_str = dynamic_cast<ConfigOptionString *>(printer_model_opt);
        if (!printer_model_str) continue;

        // use printer_model as printer type
        if (printer_model_str->value != printer_type) continue;

        if (printer_it->name.find(nozzle_diameter_str) != std::string::npos) printer_names.insert(printer_it->name);
    }

    ASSERT_ONCE(printer_names.size() == 1);

    for (auto& printer_name : printer_names) {
        BOOST_LOG_TRIVIAL(info) << "printer name: " << printer_name;
    }

    return printer_names;
}

bool PresetBundle::check_filament_temp_equation_by_printer_type_and_nozzle_for_mas_tray(
    const std::string &printer_type, std::string& nozzle_diameter_str, std::string &setting_id, std::string &tag_uid, std::string &nozzle_temp_min, std::string &nozzle_temp_max, std::string& preset_setting_id)
{
    bool is_equation = true;

    std::map<std::string, std::vector<Preset const *>> filament_list = filaments.get_filament_presets();
    std::set<std::string> printer_names       = get_printer_names_by_printer_type_and_nozzle(printer_type, nozzle_diameter_str);

    if (filament_list.find(setting_id) == filament_list.end()) return is_equation;
    for (const Preset *preset : filament_list.find(setting_id)->second) {
        if (tag_uid == "0" || (tag_uid.size() == 16 && tag_uid.substr(12, 2) == "01")) continue;
        if (preset && !preset->is_user()) continue;
        ConfigOption *       printer_opt  = const_cast<Preset *>(preset)->config.option("compatible_printers");
        ConfigOptionStrings *printer_strs = dynamic_cast<ConfigOptionStrings *>(printer_opt);
        bool                 compared = false;
        for (const std::string &printer_str : printer_strs->values) {
            if (printer_names.find(printer_str) != printer_names.end()) {
                BOOST_LOG_TRIVIAL(info) << "nozzle temp matching: preset name: " << preset->name << " printer name: " << printer_str;
                // Compare only once
                if (!compared) {
                    compared                        = true;
                    bool          min_temp_equation = false, max_temp_equation = false;
                    int           min_nozzle_temp = std::stoi(nozzle_temp_min);
                    int           max_nozzle_temp = std::stoi(nozzle_temp_max);
                    ConfigOption *opt_min         = const_cast<Preset *>(preset)->config.option("nozzle_temperature_range_low");
                    if (opt_min) {
                        ConfigOptionInts *opt_min_ints = dynamic_cast<ConfigOptionInts *>(opt_min);
                        min_nozzle_temp                = opt_min_ints->get_at(0);
                        if (std::to_string(min_nozzle_temp) == nozzle_temp_min)
                            min_temp_equation = true;
                        else {
                            BOOST_LOG_TRIVIAL(info) << "tray min temp: " << nozzle_temp_min << " preset min temp: " << min_nozzle_temp;
                            nozzle_temp_min = std::to_string(min_nozzle_temp);
                        }
                    }
                    ConfigOption *opt_max = const_cast<Preset *>(preset)->config.option("nozzle_temperature_range_high");
                    if (opt_max) {
                        ConfigOptionInts *opt_max_ints = dynamic_cast<ConfigOptionInts *>(opt_max);
                        max_nozzle_temp                = opt_max_ints->get_at(0);
                        if (std::to_string(max_nozzle_temp) == nozzle_temp_max)
                            max_temp_equation = true;
                        else {
                            BOOST_LOG_TRIVIAL(info) << "tray max temp: " << nozzle_temp_max << " preset min temp: " << max_nozzle_temp;
                            nozzle_temp_max = std::to_string(max_nozzle_temp);
                        }
                    }
                    if (min_temp_equation && max_temp_equation) {
                        BOOST_LOG_TRIVIAL(info) << "Determine if the temperature has changed: no changed";
                    } else {
                        BOOST_LOG_TRIVIAL(info) << "Determine if the temperature has changed: has changed";
                        preset_setting_id = preset->setting_id;
                        is_equation = false;
                    }
                } else {
                    assert(false);
                }
            }
        }
    }
    return is_equation;
}

Preset *PresetBundle::get_similar_printer_preset(std::string printer_model, std::string printer_variant)
{
    if (printer_model.empty())
        printer_model = printers.get_selected_preset().config.opt_string("printer_model");
    auto printer_variant_old = printers.get_selected_preset().config.opt_string("printer_variant");
    std::map<std::string, Preset*> printer_presets;
    for (auto &preset : printers.m_presets) {
        if (printer_variant.empty() && !preset.is_system)
            continue;
        if (preset.config.opt_string("printer_model") == printer_model)
            printer_presets.insert({preset.name, &preset});
    }
    if (printer_presets.empty())
        return nullptr;
    auto prefer_printer = printers.get_selected_preset().name;
    if (!printer_variant.empty())
        boost::replace_all(prefer_printer, printer_variant_old, printer_variant);
    else if (auto n = prefer_printer.find(printer_variant_old); n != std::string::npos)
        prefer_printer = printer_model + " " + printer_variant_old + prefer_printer.substr(n + printer_variant_old.length());
    if (auto iter = printer_presets.find(prefer_printer); iter != printer_presets.end()) {
        if (printer_variant.empty() || iter->second->config.opt_string("printer_variant") == printer_variant) {
            return iter->second;
        }
    }
    if (printer_variant.empty())
        printer_variant = printer_variant_old;
    for (auto& preset : printer_presets) {
        if (preset.second->config.opt_string("printer_variant") == printer_variant)
            return preset.second;
    }
    return printer_presets.begin()->second;
}

void PresetBundle::set_ams_count(int index, int ams4, int ams1)
{
    extruder_ams_counts.resize(get_printer_extruder_count());

    bool valid = (0 <= index && index < extruder_ams_counts.size());
    assert(valid && "out of bound");
    if (!valid) return;

    auto &counts = extruder_ams_counts[index];
    counts[4]    = ams4;
    counts[1]    = ams1;
    if (index < static_cast<int>(extruder_ams_selections.size()) && extruder_ams_selections[index].initialized) {
        extruder_ams_selections[index].ams4.resize(std::max(0, ams4));
        extruder_ams_selections[index].ams1.resize(std::max(0, ams1));
    }
}

std::pair<int, int> PresetBundle::get_ams_count(int index)
{
    bool valid = (0 <= index && index < extruder_ams_counts.size());
    assert(valid && "out of bound");
    if (!valid) return {0, 0};

    auto &counts = extruder_ams_counts[index];
    return {counts[4], counts[1]};
}

ExtruderAmsSelection *PresetBundle::get_ams_selection(int index)
{
    extruder_ams_selections.resize(get_printer_extruder_count());
    const bool valid = 0 <= index && index < static_cast<int>(extruder_ams_selections.size());
    assert(valid && "out of bound");
    return valid ? &extruder_ams_selections[index] : nullptr;
}

void PresetBundle::initialize_ams_selection(int index, const std::string &device_id,
                                            const std::vector<std::string> &ams4_ids, const std::vector<std::string> &ams1_ids)
{
    ExtruderAmsSelection *selection = get_ams_selection(index);
    if (!selection || (selection->initialized && (device_id.empty() || selection->device_id == device_id))) return;

    const auto [n4, n1] = get_ams_count(index);
    auto initialize = [](std::vector<std::string> &units, int count, const std::vector<std::string> &device_ids) {
        units.clear();
        units.reserve(std::max(0, count));
        for (int i = 0; i < count; ++i)
            units.push_back(i < static_cast<int>(device_ids.size()) ? device_ids[i] : std::string());
    };
    initialize(selection->ams4, n4, ams4_ids);
    initialize(selection->ams1, n1, ams1_ids);
    selection->initialized = true;
    selection->device_id   = device_id;
}

void PresetBundle::resize_ams_selection(int index, const std::string &device_id,
                                        const std::vector<std::string> &ams4_ids, const std::vector<std::string> &ams1_ids,
                                        int ams4, int ams1)
{
    initialize_ams_selection(index, device_id, ams4_ids, ams1_ids);
    ExtruderAmsSelection *selection = get_ams_selection(index);
    if (!selection) return;

    auto resize = [](std::vector<std::string> &selected, int count, const std::vector<std::string> &device_ids) {
        const size_t wanted = std::max(0, count);
        if (selected.size() > wanted) selected.resize(wanted);
        while (selected.size() < wanted) {
            const auto missing = std::find_if(device_ids.begin(), device_ids.end(), [&selected](const std::string &id) {
                return std::find(selected.begin(), selected.end(), id) == selected.end();
            });
            if (missing == device_ids.end()) {
                selected.emplace_back();
                continue;
            }

            const auto missing_rank = std::distance(device_ids.begin(), missing);
            const auto insert_at = std::find_if(selected.begin(), selected.end(), [&](const std::string &id) {
                if (id.empty()) return true;
                const auto rank = std::find(device_ids.begin(), device_ids.end(), id);
                return rank != device_ids.end() && std::distance(device_ids.begin(), rank) > missing_rank;
            });
            selected.insert(insert_at, *missing);
        }
    };
    resize(selection->ams4, ams4, ams4_ids);
    resize(selection->ams1, ams1, ams1_ids);
    set_ams_count(index, ams4, ams1);
}

void PresetBundle::replace_ams_selection(int index, const std::string &device_id,
                                         const std::vector<std::string> &ams4_ids, const std::vector<std::string> &ams1_ids)
{
    ExtruderAmsSelection *selection = get_ams_selection(index);
    if (!selection) return;

    selection->initialized = true;
    selection->device_id   = device_id;
    selection->ams4        = ams4_ids;
    selection->ams1        = ams1_ids;
    set_ams_count(index, static_cast<int>(ams4_ids.size()), static_cast<int>(ams1_ids.size()));
}

std::vector<std::string> PresetBundle::get_extra_compatible_printers_by_full_stat()
{
    std::vector<std::string> result;
    // Broadening compatibility to sibling nozzle-diameter variants only makes sense while the project is
    // allowed to print with several diameters at once.
    if (! multi_nozzle_diameter_mode_enabled(this->project_config))
        return result;
    // A project load selects its nozzles only after its printer is in place, so in between the nozzles
    // may still describe the previous printer; skip broadening rather than leak its diameters.
    const int extruder_count = this->nozzle_inventory.extruder_count();
    if (extruder_count == 0 || extruder_count != this->get_printer_extruder_count())
        return result;

    // Collect the distinct diameters the user selected via "set nozzle diameter".
    std::set<double> selected_diameters;
    for (int e = 0; e < extruder_count; ++e) {
        for (const NozzleSlot &slot : this->nozzle_inventory.slots(e)) {
            if (slot.empty())
                continue;
            const double d = string_to_double_decimal_point(slot.diameter.c_str());
            if (d > 0.0)
                selected_diameters.insert(d);
        }
    }
    // Multi-diameter mode deliberately pins the active printer/process preset to 0.2. Even when the
    // physical machine currently has only one configured diameter (for example only 0.4), that sibling
    // printer preset still has to broaden filament compatibility; otherwise every 0.4 filament appears
    // under "Unavailable" merely because the active slicing baseline is 0.2.
    if (selected_diameters.empty())
        return result;

    const std::string printer_model = this->printers.get_edited_preset().config.opt_string("printer_model");
    if (printer_model.empty())
        return result;

    std::set<std::string> names;
    for (auto printer_it = this->printers.begin(); printer_it != this->printers.end(); ++printer_it) {
        if (printer_it->config.opt_string("printer_model") != printer_model)
            continue;
        const double variant_diameter = string_to_double_decimal_point(printer_it->config.opt_string("printer_variant").c_str());
        if (variant_diameter <= 0.0)
            continue;
        for (double d : selected_diameters) {
            if (std::abs(variant_diameter - d) < 1e-3) {
                names.insert(printer_it->name);
                break;
            }
        }
    }
    result.assign(names.begin(), names.end());
    return result;
}

std::vector<std::string> PresetBundle::compute_filament_supported_nozzle_diameters(size_t num_filaments) const
{
    std::vector<std::string> result(num_filaments);

    // Map every printer preset name to its (normalized) variant diameter. Filament presets reference
    // printers by name in compatible_printers, so this lets us translate a compatible printer into the
    // diameter it represents. Only variants of the active model belong to this project's choice set.
    std::map<std::string, std::string> printer_name_to_diameter;
    const std::string printer_model = this->printers.get_edited_preset().config.opt_string("printer_model");
    for (auto printer_it = this->printers.begin(); printer_it != this->printers.end(); ++printer_it) {
        if (printer_it->config.opt_string("printer_model") != printer_model)
            continue;
        const double variant_diameter = string_to_double_decimal_point(printer_it->config.opt_string("printer_variant").c_str());
        if (variant_diameter > 0.0)
            printer_name_to_diameter[printer_it->name] = format_diameter_to_str(variant_diameter);
    }
    if (printer_name_to_diameter.empty())
        return result;

    for (size_t i = 0; i < num_filaments; ++i) {
        if (i >= this->filament_presets.size())
            continue;
        const Preset *preset = const_cast<PresetBundle *>(this)->filaments.find_preset(this->filament_presets[i], false);
        if (preset == nullptr)
            continue;

        const auto *compatible = preset->config.option<ConfigOptionStrings>("compatible_printers");
        if (compatible == nullptr)
            continue;

        // Use the filament preset's own compatible_printers only. The multi-diameter broadening that
        // makes sibling-variant filaments selectable must NOT leak here: the restriction has to reflect
        // the diameters the filament natively supports, not the loosened selectable set.
        std::set<std::string> diameters;
        for (const std::string &printer_name : compatible->values) {
            auto found = printer_name_to_diameter.find(printer_name);
            if (found != printer_name_to_diameter.end())
                diameters.insert(found->second);
        }

        std::string joined;
        for (const std::string &d : diameters) {
            if (!joined.empty())
                joined += ';';
            joined += d;
        }
        result[i] = std::move(joined);
    }
    return result;
}

static std::set<std::string> filament_preset_diameters_for_model(
    const Preset &preset, const std::map<std::string, std::string> &printer_name_to_diameter)
{
    std::set<std::string> result;
    const auto *compatible = preset.config.option<ConfigOptionStrings>("compatible_printers");
    if (compatible == nullptr)
        return result;
    for (const std::string &printer_name : compatible->values) {
        auto it = printer_name_to_diameter.find(printer_name);
        if (it != printer_name_to_diameter.end())
            result.insert(it->second);
    }
    return result;
}

static std::map<std::string, std::string> build_printer_name_to_diameter(const PresetBundle &bundle)
{
    const std::string printer_model = bundle.printers.get_edited_preset().config.opt_string("printer_model");
    std::map<std::string, std::string> result;
    for (auto it = bundle.printers.begin(); it != bundle.printers.end(); ++it) {
        if (it->config.opt_string("printer_model") != printer_model)
            continue;
        const double diameter = string_to_double_decimal_point(it->config.opt_string("printer_variant").c_str());
        if (diameter > 0.0)
            result[it->name] = format_diameter_to_str(diameter);
    }
    return result;
}

static bool uses_per_slot_nozzle_diameters(const ConfigBase &config)
{
    if (const auto *diameters = config.option<ConfigOptionFloatsNullable>("nozzle_diameter");
        diameters != nullptr && diameters->values.size() > 1)
        return true;

    if (const auto *max_counts =
            config.option<ConfigOptionIntsNullable>("extruder_max_nozzle_count");
        max_counts != nullptr &&
        std::any_of(max_counts->values.begin(), max_counts->values.end(),
                    [](int count) { return count > 1; }))
        return true;

    const auto *hotend_as_extruder =
        config.option<ConfigOptionBool>("handle_hotend_as_extruder");
    return hotend_as_extruder != nullptr && hotend_as_extruder->value;
}

static std::set<std::string> collect_configured_nozzle_diameters(const PresetBundle &bundle)
{
    std::set<std::string> result;
    const Preset &printer = bundle.printers.get_edited_preset();

    // With the project-level gate off the machine offers exactly one diameter - the one its printer preset
    // is on - no matter how many the rack physically carries. Every caller keys its single-diameter branch
    // on this set holding fewer than two entries, so the whole preset/selector side follows from here.
    if (!multi_nozzle_diameter_mode_enabled(bundle.project_config) ||
        !uses_per_slot_nozzle_diameters(printer.config)) {
        const double primary_diameter = string_to_double_decimal_point(printer.config.opt_string("printer_variant").c_str());
        if (primary_diameter > 0.0)
            result.insert(format_diameter_to_str(primary_diameter));
        return result;
    }

    // Keep every physical slot visible to the selector. Whether a machine can print multiple diameters
    // has nothing to do with how many slots one extruder has: a dual-extruder machine holding one
    // diameter per side (H2D) is just as multi-diameter as a single extruder holding several (H3C).
    // The representative per-extruder diameter is intentionally not used here either, because one
    // logical extruder may carry several physical diameters.
    for (int extruder_id = 0; extruder_id < bundle.nozzle_inventory.extruder_count(); ++extruder_id)
        for (const NozzleSlot &slot : bundle.nozzle_inventory.slots(extruder_id)) {
            const double diameter = string_to_double_decimal_point(slot.diameter.c_str());
            if (diameter > 0.0)
                result.insert(format_diameter_to_str(diameter));
        }

    // Without a per-slot snapshot the printer preset is all we know about the hardware.
    if (result.empty()) {
        const double primary_diameter = string_to_double_decimal_point(printer.config.opt_string("printer_variant").c_str());
        if (primary_diameter > 0.0)
            result.insert(format_diameter_to_str(primary_diameter));
    }
    return result;
}

// Companion of collect_configured_nozzle_diameters() on the flow axis: which (diameter, flow)
// nozzles the rack actually carries. Kept on the same source so the two axes of the per-slot
// selector always agree.
static std::set<std::pair<std::string, NozzleVolumeType>> collect_installed_nozzle_variants(const PresetBundle &bundle)
{
    std::set<std::pair<std::string, NozzleVolumeType>> result;
    const Preset &printer = bundle.printers.get_edited_preset();

    // The machine's own diameter paired with the per-extruder project flow setting. Serves both the
    // gate below and the fallback for an empty per-slot snapshot.
    auto fill_from_printer_preset = [&bundle, &printer, &result]() {
        const double primary_diameter = string_to_double_decimal_point(printer.config.opt_string("printer_variant").c_str());
        if (primary_diameter <= 0.0)
            return;
        const std::string diameter = format_diameter_to_str(primary_diameter);
        const auto *volume_types = bundle.project_config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type");
        if (volume_types == nullptr || volume_types->values.empty())
            result.emplace(diameter, nvtStandard);
        else
            for (int value : volume_types->values)
                result.emplace(diameter, static_cast<NozzleVolumeType>(value));
    };

    // Same gate as collect_configured_nozzle_diameters(), but only the diameter axis collapses with
    // the project-level gate off: every slot then prints at the printer preset's single diameter.
    // The flow axis does not collapse with it -- the slots can carry different flows at that one
    // diameter, and the grouping is free to put a filament on whichever slot has the flow it needs.
    // So the diameter comes from the printer preset while the flows are still read off the rack.
    // Pairing that diameter with the project's single flow setting instead, as this used to do, hides
    // every other flow the machine has mounted: a filament that can only print in one of them looks
    // unprintable, and a wrong setting makes every filament look that way at once.
    if (!multi_nozzle_diameter_mode_enabled(bundle.project_config) ||
        !uses_per_slot_nozzle_diameters(printer.config)) {
        const double primary_diameter =
            string_to_double_decimal_point(printer.config.opt_string("printer_variant").c_str());
        if (primary_diameter > 0.0) {
            const std::string diameter = format_diameter_to_str(primary_diameter);
            for (int extruder_id = 0; extruder_id < bundle.nozzle_inventory.extruder_count(); ++extruder_id)
                for (const NozzleSlot &slot : bundle.nozzle_inventory.slots(extruder_id)) {
                    if (slot.empty())
                        continue;
                    const double slot_diameter = string_to_double_decimal_point(slot.diameter.c_str());
                    if (slot_diameter <= 0.0 || format_diameter_to_str(slot_diameter) != diameter)
                        continue;
                    result.emplace(diameter, slot.volume_type);
                }
        }
        // Nothing mounted to read -- the printer preset and the project's flow setting are all there is.
        if (result.empty())
            fill_from_printer_preset();
        return result;
    }

    for (int extruder_id = 0; extruder_id < bundle.nozzle_inventory.extruder_count(); ++extruder_id)
        for (const NozzleSlot &slot : bundle.nozzle_inventory.slots(extruder_id)) {
            // An empty slot carries no nozzle; skipping those is what keeps the selector from
            // offering a variant the machine does not physically have.
            if (slot.empty())
                continue;
            const double diameter = string_to_double_decimal_point(slot.diameter.c_str());
            if (diameter <= 0.0)
                continue;
            result.emplace(format_diameter_to_str(diameter), slot.volume_type);
        }

    // A rack with every slot empty gives no variant. collect_configured_nozzle_diameters() falls back
    // to the printer preset in that case, so this does the same: the two describe the same rack.
    if (result.empty())
        fill_from_printer_preset();
    return result;
}

static std::vector<NozzleVolumeType> get_filament_nozzle_flow_options(
    const PresetBundle                                       &bundle,
    size_t                                                    filament_index,
    const std::string                                        &diameter,
    const std::set<std::pair<std::string, NozzleVolumeType>> &installed)
{
    std::vector<NozzleVolumeType> result;
    if (filament_index >= bundle.filament_presets.size() || diameter.empty())
        return result;

    // Same printability rule PartPlate uses to validate a slice: a 2.85mm TPU only declares the
    // TPU285 variant, so it must not inherit a Standard/High Flow selection from another slot.
    std::vector<std::string> filament_variants;
    bool                     variants_complete = false;
    if (const Preset *preset = bundle.filaments.find_preset(bundle.filament_presets[filament_index], false)) {
        if (const auto *opt = preset->config.option<ConfigOptionStrings>("filament_extruder_variant"))
            filament_variants = opt->values;
        variants_complete = bundle.filaments.is_bbl_brand_filament(*preset);
    }

    for (const auto &variant : installed) {
        if (variant.first != diameter || variant.second == nvtHybrid)
            continue;
        if (!is_nozzle_printable_for_filament(variant.second, filament_variants, variants_complete))
            continue;
        result.emplace_back(variant.second);
    }
    return result;
}

static std::vector<std::string> get_filament_nozzle_diameter_options(
    const PresetBundle                       &bundle,
    size_t                                    filament_index,
    const std::map<std::string, std::string> &printer_name_to_diameter,
    const std::set<std::string>              &configured)
{
    if (filament_index >= bundle.filament_presets.size() || bundle.is_mixed_filament(filament_index))
        return {};

    const Preset *current = bundle.filaments.find_preset(bundle.filament_presets[filament_index], false);
    if (current == nullptr)
        return {};

    std::set<std::string> supported = filament_preset_diameters_for_model(*current, printer_name_to_diameter);
    if (!current->filament_id.empty()) {
        for (auto it = bundle.filaments.begin(); it != bundle.filaments.end(); ++it) {
            // Broaden by every sibling preset sharing the same filament_id, not just system ones:
            // user-created filaments (e.g. "Polymaker ABS 777") are saved as one preset per printer-
            // nozzle variant with matching filament_id, all is_system == false. Requiring is_system
            // dropped every one of those, leaving only the current variant's own compatible_printers,
            // so the FilamentNozzlePopup only offered the currently active diameter for custom
            // filaments even when the user had explicitly configured all four nozzle variants.
            if (it->filament_id == current->filament_id) {
                const auto diameters = filament_preset_diameters_for_model(*it, printer_name_to_diameter);
                supported.insert(diameters.begin(), diameters.end());
            }
        }
    }

    std::vector<std::string> result;
    if (supported.empty()) {
        result.assign(configured.begin(), configured.end());
    } else {
        std::set_intersection(supported.begin(), supported.end(), configured.begin(), configured.end(),
                              std::back_inserter(result));
    }
    std::sort(result.begin(), result.end(), [](const std::string &lhs, const std::string &rhs) {
        return string_to_double_decimal_point(lhs.c_str()) < string_to_double_decimal_point(rhs.c_str());
    });
    return result;
}

static void settle_filament_slot_flow(PresetBundle &bundle, size_t idx)
{
    ConfigOptionInts *volume_map = bundle.project_config.option<ConfigOptionInts>("filament_volume_map");
    if (volume_map == nullptr)
        return;
    if (idx >= volume_map->values.size())
        volume_map->values.resize(idx + 1, static_cast<int>(nvtStandard));

    const auto printer_name_to_diameter = build_printer_name_to_diameter(bundle);
    const auto configured                = collect_configured_nozzle_diameters(bundle);
    const auto options = get_filament_nozzle_diameter_options(bundle, idx, printer_name_to_diameter, configured);
    if (options.empty())
        return;

    const auto *selected_diameters = bundle.project_config.option<ConfigOptionStrings>("filament_diameter_map");
    std::string target_diameter = (selected_diameters != nullptr && idx < selected_diameters->values.size()) ?
        selected_diameters->values[idx] : std::string();
    if (target_diameter.empty() ||
        std::find(options.begin(), options.end(), target_diameter) == options.end())
        target_diameter = options.front();

    const auto installed = collect_installed_nozzle_variants(bundle);
    const auto flows      = get_filament_nozzle_flow_options(bundle, idx, target_diameter, installed);
    if (flows.empty())
        return;
    const NozzleVolumeType current_flow = static_cast<NozzleVolumeType>(volume_map->values[idx]);
    if (std::find(flows.begin(), flows.end(), current_flow) == flows.end())
        volume_map->values[idx] = static_cast<int>(flows.front());
}

// Repairs filament_volume_map so every slot's flow is one the rack carries at that slot's nozzle
// diameter and that the slot's filament can print. Only invoked where the filament panel's per-slot
// selector owns the flow; everywhere else the map is still a grouping result.
static void repair_filament_nozzle_flows(
    const PresetBundle                                       &bundle,
    ConfigOptionInts                                         *volume_map,
    const std::set<std::pair<std::string, NozzleVolumeType>> &installed,
    const std::map<std::string, std::string>                 &printer_name_to_diameter,
    const std::set<std::string>                              &configured)
{
    if (volume_map == nullptr)
        return;
    volume_map->values.resize(bundle.filament_presets.size(), static_cast<int>(nvtStandard));

    const auto *selected = bundle.project_config.option<ConfigOptionStrings>("filament_diameter_map");
    for (size_t i = 0; i < bundle.filament_presets.size(); ++i) {
        if (bundle.is_mixed_filament(i))
            continue;
        std::string diameter = (selected != nullptr && i < selected->values.size()) ? selected->values[i] : std::string();

        // Ahead of the flagged-slot skip below, which preserves selections the user made. This one
        // was not: it is left over from the slot's previous diameter, and keeping it would pin the
        // slot to a flagged state with no way out -- the nozzle settings will not offer it either.
        if (!diameter.empty() &&
            !nozzle_variant_exists(diameter, static_cast<NozzleVolumeType>(volume_map->values[i]))) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": filament " << i << " sat on nozzle flow "
                                       << volume_map->values[i] << " at diameter " << diameter
                                       << ", which is not made; moved to Standard";
            volume_map->values[i] = static_cast<int>(nvtStandard);
        }

        if (std::find(bundle.incompatible_nozzle_diameter_slots.begin(),
                      bundle.incompatible_nozzle_diameter_slots.end(), i) !=
            bundle.incompatible_nozzle_diameter_slots.end())
            continue;
        if (diameter.empty()) {
            // A machine carrying a single diameter leaves filament_diameter_map empty on purpose, yet
            // the flow is still selectable there (e.g. a 0.4 Standard next to a 0.4 High Flow). Key off
            // the one diameter this slot can use, the same fallback the sidebar's nozzle label renders.
            const auto options = get_filament_nozzle_diameter_options(bundle, i, printer_name_to_diameter, configured);
            if (options.empty())
                continue;
            diameter = options.front();
        }
        const auto flows = get_filament_nozzle_flow_options(bundle, i, diameter, installed);
        if (flows.empty())
            continue; // nothing installed can print this slot: leave it for the pre-slice checks to report
        if (std::find(flows.begin(), flows.end(), static_cast<NozzleVolumeType>(volume_map->values[i])) == flows.end())
            volume_map->values[i] = static_cast<int>(flows.front());
    }
}

std::vector<std::string> PresetBundle::get_filament_nozzle_diameter_options(size_t filament_index) const
{
    const auto printer_name_to_diameter = build_printer_name_to_diameter(*this);
    const auto configured = collect_configured_nozzle_diameters(*this);
    return ::Slic3r::get_filament_nozzle_diameter_options(*this, filament_index, printer_name_to_diameter, configured);
}

std::set<std::pair<std::string, NozzleVolumeType>> PresetBundle::get_installed_nozzle_variants() const
{
    return ::Slic3r::collect_installed_nozzle_variants(*this);
}

bool PresetBundle::is_filament_nozzle_diameter_valid(size_t filament_index, const std::string &diameter) const
{
    if (diameter.empty())
        return true;
    const std::string normalized = format_diameter_to_str(string_to_double_decimal_point(diameter.c_str()));
    const auto options = get_filament_nozzle_diameter_options(filament_index);
    return std::find(options.begin(), options.end(), normalized) != options.end();
}

static bool set_filament_nozzle_diameter(
    PresetBundle                              &bundle,
    size_t                                     filament_index,
    const std::string                         &diameter,
    std::string                               *selected_preset_name,
    const std::map<std::string, std::string>  &printer_name_to_diameter,
    const std::vector<std::string>            &options)
{
    if (filament_index >= bundle.filament_presets.size())
        return false;

    auto *selected = bundle.project_config.option<ConfigOptionStrings>("filament_diameter_map");
    selected->values.resize(bundle.filament_presets.size());
    if (diameter.empty()) {
        selected->values[filament_index].clear();
        if (selected_preset_name != nullptr)
            *selected_preset_name = bundle.filament_presets[filament_index];
        return true;
    }

    const std::string normalized = format_diameter_to_str(string_to_double_decimal_point(diameter.c_str()));
    if (std::find(options.begin(), options.end(), normalized) == options.end())
        return false;

    const Preset *current = bundle.filaments.find_preset(bundle.filament_presets[filament_index], false);
    if (current == nullptr)
        return false;

    const auto current_diameters = filament_preset_diameters_for_model(*current, printer_name_to_diameter);
    std::string target_preset = current->name;
    if (!current_diameters.empty() && current_diameters.count(normalized) == 0) {
        target_preset.clear();
        if (!current->filament_id.empty()) {
            // Mirrors the sibling lookup in get_filament_nozzle_diameter_options(): user-created
            // filaments store one preset per nozzle variant sharing filament_id, all is_system ==
            // false, so requiring is_system here found no sibling and failed the switch on a diameter
            // that popup had already offered. Preferring a sibling of the same kind as the current
            // preset keeps a preset saved off a system one on the user's own variants rather than
            // jumping back to the system preset and dropping its edits.
            std::string fallback;
            for (auto it = bundle.filaments.begin(); it != bundle.filaments.end(); ++it) {
                if (it->filament_id != current->filament_id)
                    continue;
                const auto diameters = filament_preset_diameters_for_model(*it, printer_name_to_diameter);
                if (diameters.count(normalized) == 0)
                    continue;
                if (it->is_system == current->is_system) {
                    target_preset = it->name;
                    break;
                }
                if (fallback.empty())
                    fallback = it->name;
            }
            if (target_preset.empty())
                target_preset = fallback;
        }
        if (target_preset.empty())
            return false;
        bundle.filament_presets[filament_index] = target_preset;
        if (filament_index == 0)
            bundle.filaments.select_preset_by_name(target_preset, false, true);
    }

    selected->values[filament_index] = normalized;
    if (selected_preset_name != nullptr)
        *selected_preset_name = target_preset;
    return true;
}

bool PresetBundle::set_filament_nozzle_diameter(size_t filament_index, const std::string &diameter,
                                                std::string *selected_preset_name,
                                                bool diameter_picked_by_user)
{
    const auto printer_name_to_diameter = build_printer_name_to_diameter(*this);
    auto       configured = collect_configured_nozzle_diameters(*this);
    // Admitting the requested diameter to the configured set only widens what the options below may
    // intersect with; the filament still has to have a preset for it, so an outright unsupported
    // diameter is refused exactly as before.
    if (diameter_picked_by_user && !diameter.empty())
        configured.insert(format_diameter_to_str(string_to_double_decimal_point(diameter.c_str())));
    const auto options = ::Slic3r::get_filament_nozzle_diameter_options(
        *this, filament_index, printer_name_to_diameter, configured);
    const bool updated = ::Slic3r::set_filament_nozzle_diameter(
        *this, filament_index, diameter, selected_preset_name, printer_name_to_diameter, options);
    if (updated)
        sync_mixed_filament_nozzle_diameters(project_config);
    return updated;
}

std::string PresetBundle::tray_diameter_for_filament_slot(size_t filament_index) const
{
    if (filament_index >= filament_presets.size())
        return {};
    const Preset *preset = filaments.find_preset(filament_presets[filament_index], false);
    if (preset == nullptr || preset->filament_id.empty())
        return {};
    const auto *colors = project_config.option<ConfigOptionStrings>("filament_colour");
    const std::string color = (colors != nullptr && filament_index < colors->values.size())
        ? colors->values[filament_index] : std::string();

    std::string first_match;
    for (const auto &entry : filament_ams_list) {
        const DynamicPrintConfig &tray = entry.second;
        if (tray.opt_string("filament_id", 0u) != preset->filament_id)
            continue;
        if (!color.empty() && tray.opt_string("filament_colour", 0u) != color)
            continue;
        const auto *diameters = tray.option<ConfigOptionFloats>("tray_nozzle_diameter");
        if (diameters == nullptr || diameters->values.empty() || diameters->values.front() <= 0.)
            continue;
        const std::string diameter = format_diameter_to_str(diameters->values.front());
        if (first_match.empty())
            first_match = diameter;
    }
    return first_match;
}

void PresetBundle::stamp_printer_nozzle_diameter_mode()
{
    std::string tag;
    const Preset *printer_base = printers.get_preset_base(printers.get_edited_preset());
    if (printer_base != nullptr && printer_base->is_system && is_single_ext_multi_nozzle(printer_base->config))
        tag = multi_nozzle_diameter_mode_enabled(project_config) ? printer_nozzle_diameter_mode_multi() :
                                                                   printer_nozzle_diameter_mode_single();
    printers.get_edited_preset().config.option<ConfigOptionString>("printer_nozzle_diameter_mode", true)->value = tag;
}

bool PresetBundle::is_multi_nozzle_user_printer_preset(const Preset &preset) const
{
    if (preset.is_system || preset.is_default)
        return false;
    const Preset *base = printers.get_preset_base(preset);
    if (base == nullptr || !base->is_system || !is_single_ext_multi_nozzle(base->config) ||
        base->config.opt_string("printer_variant") != canonical_multi_nozzle_printer_variant())
        return false;
    const std::string tag = printer_nozzle_diameter_mode_tag(preset.config);
    if (tag == printer_nozzle_diameter_mode_multi())
        return true;
    if (tag == printer_nozzle_diameter_mode_single())
        return false;
    return preset.name.find("Multiple nozzle") != std::string::npos;
}

void PresetBundle::seed_filament_nozzles_for_multi_diameter(const std::string &fallback_diameter)
{
    const std::string normalized = format_diameter_to_str(string_to_double_decimal_point(fallback_diameter.c_str()));
    if (normalized.empty())
        return;

    auto *selected = project_config.option<ConfigOptionStrings>("filament_diameter_map");
    auto *volume_map = project_config.option<ConfigOptionInts>("filament_volume_map");
    if (selected == nullptr || volume_map == nullptr)
        return;

    selected->values.resize(filament_presets.size());
    volume_map->values.resize(filament_presets.size(), static_cast<int>(nvtStandard));

    const auto printer_name_to_diameter = build_printer_name_to_diameter(*this);
    const auto configured = collect_configured_nozzle_diameters(*this);
    const auto installed = collect_installed_nozzle_variants(*this);

    for (size_t i = 0; i < filament_presets.size(); ++i) {
        if (is_mixed_filament(i))
            continue;

        const auto options = ::Slic3r::get_filament_nozzle_diameter_options(
            *this, i, printer_name_to_diameter, configured);

        // Single-diameter mode never recorded which nozzle each tray feeds -- validate_filament_
        // nozzle_diameter_selections() returns from its configured.size() < 2 branch long before the
        // device-map code that would have read it. The machine did report it though, and
        // filament_ams_list still holds it, so take it here rather than flattening every slot onto
        // the one diameter that happened to be selected. Falling back to that diameter only when
        // the machine has nothing to say about this slot keeps the old behaviour for filaments the
        // user picked by hand.
        std::string target = tray_diameter_for_filament_slot(i);
        if (target.empty() || std::find(options.begin(), options.end(), target) == options.end())
            target = normalized;

        if (!::Slic3r::set_filament_nozzle_diameter(
                *this, i, target, nullptr, printer_name_to_diameter, options)) {
            // Keep the requested diameter so validation flags the slot instead of rewriting
            // it to the smallest remaining option.
            selected->values[i] = target;
            continue;
        }

        const auto flows = ::Slic3r::get_filament_nozzle_flow_options(*this, i, target, installed);
        if (flows.empty())
            continue;
        volume_map->values[i] = static_cast<int>(flows.front());
    }
}

static const Preset *find_default_filament_for_nozzle_diameter(
    PresetBundle                              &bundle,
    const std::string                         &diameter,
    const std::map<std::string, std::string>  &printer_name_to_diameter)
{
    const std::string printer_model = bundle.printers.get_edited_preset().config.opt_string("printer_model");
    for (auto printer = bundle.printers.begin(); printer != bundle.printers.end(); ++printer) {
        if (printer->config.opt_string("printer_model") != printer_model)
            continue;
        const double variant = string_to_double_decimal_point(printer->config.opt_string("printer_variant").c_str());
        if (variant <= 0. || format_diameter_to_str(variant) != diameter)
            continue;
        const auto *defaults = printer->config.option<ConfigOptionStrings>("default_filament_profile");
        if (defaults == nullptr)
            continue;
        for (const std::string &name : defaults->values) {
            const Preset *candidate = bundle.filaments.find_preset(name, false);
            if (candidate != nullptr && candidate->config.opt_string("filament_type", 0u) == "PLA" &&
                filament_preset_diameters_for_model(*candidate, printer_name_to_diameter).count(diameter) != 0)
                return candidate;
        }
    }

    // Keep the established incompatible-filament fallback even if the target printer preset does not
    // declare a usable default profile.
    const Preset *first_pla = nullptr;
    for (auto candidate = bundle.filaments.begin(); candidate != bundle.filaments.end(); ++candidate) {
        if (!candidate->is_system || candidate->config.opt_string("filament_type", 0u) != "PLA" ||
            filament_preset_diameters_for_model(*candidate, printer_name_to_diameter).count(diameter) == 0)
            continue;
        if (candidate->name == PresetBundle::BBL_DEFAULT_FILAMENT ||
            boost::algorithm::starts_with(candidate->name, std::string(PresetBundle::BBL_DEFAULT_FILAMENT) + " @"))
            return &*candidate;
        if (first_pla == nullptr)
            first_pla = &*candidate;
    }
    return first_pla;
}

void PresetBundle::validate_filament_nozzle_diameter_selections(const std::map<int, AMSMapInfo> *device_maps,
                                                                bool device_maps_are_user_chosen)
{
    auto *selected = project_config.option<ConfigOptionStrings>("filament_diameter_map");
    selected->values.resize(filament_presets.size());
    const bool supports_multi_diameter =
        is_single_ext_multi_nozzle(printers.get_edited_preset().config);
    if (!supports_multi_diameter) {
        if (auto *mode = project_config.option<ConfigOptionBool>("multi_nozzle_diameter_mode"))
            mode->value = false;
        std::fill(selected->values.begin(), selected->values.end(), std::string());
    }
    // multi_nozzle_diameter_mode is a project-level toggle that outlives a printer switch: a
    // project last edited against a per-slot-diameter machine keeps it set to true even after the
    // active printer preset is switched to a plain single-diameter machine, whose
    // filament_diameter_map values are equally stale leftovers from that earlier machine. Without
    // this gate every slot below is compared against the wrong "still in multi-diameter mode"
    // semantics on a machine that never supported per-slot diameters at all, and any mismatching
    // leftover value gets flagged instead of silently normalized -- matching
    // collect_configured_nozzle_diameters()'s own gate keeps the two in agreement.
    //
    // Gated on is_single_ext_multi_nozzle(), not the broader uses_per_slot_nozzle_diameters(): the
    // flag/notification behavior below is the Nozzle Settings dialog's companion feature (you swap
    // a physical nozzle for a different diameter on the same extruder, so a filament slot that was
    // relying on the removed diameter needs to be called out). Every other consumer of that same
    // swap-nozzle UX (Plater's Nozzle Settings dialog, FilamentMapDialog, FilamentGroupPopup,
    // NotificationManager, PresetComboBoxes, PartPlate) already restricts itself to
    // is_single_ext_multi_nozzle(), which excludes a multi-extruder machine whose nozzles live one
    // per extruder rather than several on the same extruder. This function was the odd one out
    // still using the broader check, so a multi-extruder machine's filament_diameter_map leftovers
    // (stale values from an earlier machine, or never normalized because it never routes through
    // the dialog that would clear them) were being flagged/red instead of silently normalized like
    // a plain machine.
    const bool multi_diameter_mode =
        multi_nozzle_diameter_mode_enabled(project_config) && supports_multi_diameter;
    const std::vector<std::string> retained_selections =
        multi_diameter_mode ? std::vector<std::string>{} : selected->values;
    const auto printer_name_to_diameter = build_printer_name_to_diameter(*this);
    const auto configured = collect_configured_nozzle_diameters(*this);
    const auto installed = collect_installed_nozzle_variants(*this);

    // Cleared unconditionally, not just inside the configured.size() < 2 branch below: turning
    // multi-diameter mode ON drives the printer preset onto canonical 0.2 *before* the project's
    // own multi_nozzle_diameter_mode gate is flipped (see apply_multi_nozzle_diameter_mode()), so
    // this function can run once mid-switch with configured == {"0.2"} and flag a slot like a
    // 0.4/0.6/0.8-only filament as incompatible. That entry must not survive into the settled
    // multi-diameter pass moments later (configured.size() >= 2), which never touched this vector
    // at all -- leaving a permanent false-positive red icon on an otherwise-compatible filament.
    // block_filament_fallback_substitution only covers the synchronous cascade of the entry point that
    // raised it. A slot marked there stays incompatible afterwards, and the very next validation pass --
    // any sidebar refresh, or an asynchronous one such as a device status push arriving seconds later --
    // would run with the flag down and silently substitute the material the user was just told to fix.
    // Carry the marking over so a slot that was deliberately kept stays kept until its situation really
    // changes: once the slot can follow the target diameter again (nozzle swapped back, or a different
    // filament picked) the branch above returns before this set is consulted and the entry drains.
    const std::vector<size_t> previously_incompatible = incompatible_nozzle_diameter_slots;
    incompatible_nozzle_diameter_slots.clear();
    auto was_marked_incompatible = [&previously_incompatible](size_t slot) {
        return std::find(previously_incompatible.begin(), previously_incompatible.end(), slot) !=
               previously_incompatible.end();
    };

    // Per-filament flow is only user-owned where the filament panel's nozzle selector is shown
    // (single-extruder multi-nozzle + multi-diameter mode). On every other machine
    // filament_volume_map is still a grouping result, so repairing it here would clobber the
    // slicer's own assignment. Null pointer means "leave the flow alone".
    ConfigOptionInts *volume_map = nullptr;
    if (multi_diameter_mode && is_single_ext_multi_nozzle(printers.get_edited_preset().config)) {
        volume_map = project_config.option<ConfigOptionInts>("filament_volume_map");
        if (volume_map != nullptr)
            volume_map->values.resize(filament_presets.size(), static_cast<int>(nvtStandard));
    }

    // Normalize any sibling preset selected by the previous multi-diameter state. Turning the mode off
    // only makes the selections inactive; retain them so turning it back on restores the project state.
    if (configured.size() < 2) {
        if (configured.size() == 1) {
            const std::string target = *configured.begin();
            for (size_t i = 0; i < filament_presets.size(); ++i) {
                if (is_mixed_filament(i))
                    continue;

                const auto options = ::Slic3r::get_filament_nozzle_diameter_options(
                    *this, i, printer_name_to_diameter, configured);

                if (multi_diameter_mode) {
                    // Same question single-diameter mode asks below -- can this filament use the
                    // diameter the rack carries -- just answered differently: there the preset is
                    // replaced with a default for that diameter, here the per-slot selection belongs
                    // to the user, so the slot is flagged instead. An empty `options` means no preset
                    // of this filament covers any installed diameter; the branches below only compare
                    // the stored diameter against the installed one and would let that through.
                    if (options.empty()) {
                        incompatible_nozzle_diameter_slots.push_back(i);
                        BOOST_LOG_TRIVIAL(warning) << "filament " << i
                                                   << " has no preset for any installed nozzle diameter"
                                                   << ", flagging the slot";
                        continue;
                    }

                    // Only ever assign a diameter here when the slot never had one (a brand-new
                    // slot). An already-selected slot is left exactly as it was, even when the
                    // remaining nozzle would also work for this filament -- the Nozzle Settings
                    // dialog must not silently recompute every slot down to the sole remaining
                    // diameter, it only removes nozzles for the diameters the user took out.
                    const std::string &current_value = selected->values[i];
                    if (current_value.empty()) {
                        if (std::find(options.begin(), options.end(), target) != options.end())
                            ::Slic3r::set_filament_nozzle_diameter(
                                *this, i, target, nullptr, printer_name_to_diameter, options);
                        continue;
                    }
                    if (format_diameter_to_str(string_to_double_decimal_point(current_value.c_str())) == target) {
                        // The diameter matching is not enough -- the slot names a flow too, and the
                        // rack may not carry that diameter in that flow -- a filament whose only flow
                        // is one no nozzle on the rack provides, or a slot left on a flow after the
                        // last nozzle of that flow was retyped. `options` above is a diameter-only
                        // check and passes both.
                        // Asking about the slot's own flow rather than "is any flow usable here" is
                        // what keeps the mark: repair_filament_nozzle_flows() silently moves a slot
                        // onto whatever flow the rack does have, and skips the slots flagged here, so
                        // the selection the user is being told about is the one that survives.
                        if (volume_map != nullptr) {
                            const auto flows = ::Slic3r::get_filament_nozzle_flow_options(*this, i, target, installed);
                            const NozzleVolumeType current_flow = i < volume_map->values.size()
                                ? static_cast<NozzleVolumeType>(volume_map->values[i]) : nvtStandard;
                            if (flows.empty() || std::find(flows.begin(), flows.end(), current_flow) == flows.end()) {
                                incompatible_nozzle_diameter_slots.push_back(i);
                                BOOST_LOG_TRIVIAL(warning) << "filament " << i
                                                           << " cannot print on the " << target << " nozzle in flow "
                                                           << int(current_flow) << ", flagging the slot";
                            }
                        }
                        continue; // the nozzle for this slot's diameter is still installed
                    }

                    // The nozzle for this slot's previously-selected diameter was removed. Flag
                    // the slot instead of silently moving it onto the sole remaining diameter.
                    incompatible_nozzle_diameter_slots.push_back(i);
                    BOOST_LOG_TRIVIAL(warning) << boost::format(
                        "slot %1% nozzle diameter %2% "
                        "no longer installed, fallback substitution blocked") % i % current_value;
                    continue;
                }

                // One flow is not interchangeable with the others: a filament declaring nvtTPU285 can
                // print on nothing else, and no other filament can use such a nozzle. That makes it a
                // yes/no question -- does the rack carry one at this diameter -- and answering it needs
                // the rack itself, not the project's settings.
                std::vector<std::string> filament_variants;
                if (const Preset *preset = filaments.find_preset(filament_presets[i], false))
                    if (const auto *opt = preset->config.option<ConfigOptionStrings>("filament_extruder_variant"))
                        filament_variants = opt->values;
                const bool needs_dedicated_flow = !filament_variants.empty() &&
                    std::all_of(filament_variants.begin(), filament_variants.end(),
                                [](const std::string &variant) { return convert_to_nvt_type(variant) == nvtTPU285; });
                if (needs_dedicated_flow && installed.count({target, nvtTPU285}) == 0) {
                    incompatible_nozzle_diameter_slots.push_back(i);
                    BOOST_LOG_TRIVIAL(warning) << "filament " << i
                                               << " needs a dedicated nozzle flow the rack does not carry at "
                                               << target << ", flagging the slot";
                    continue;
                }

                // The mirror question: every nozzle the rack carries at this diameter is one this
                // filament cannot print on (e.g. a lone TPU285 nozzle feeding an ordinary PLA).
                const bool any_flow_at_target = std::any_of(installed.begin(), installed.end(),
                    [&target](const auto &variant) { return variant.first == target; });
                if (any_flow_at_target &&
                    ::Slic3r::get_filament_nozzle_flow_options(*this, i, target, installed).empty()) {
                    incompatible_nozzle_diameter_slots.push_back(i);
                    BOOST_LOG_TRIVIAL(warning) << "filament " << i
                                               << " cannot print on any nozzle flow the rack carries at "
                                               << target << ", flagging the slot";
                    continue;
                }

                // Legacy single-diameter-machine behavior: the whole rack switched to a different
                // target diameter, so follow it when this filament preset supports it, otherwise
                // replace the preset with a default for the new diameter.
                if (std::find(options.begin(), options.end(), target) != options.end() &&
                    ::Slic3r::set_filament_nozzle_diameter(
                        *this, i, target, nullptr, printer_name_to_diameter, options))
                    continue;

                if (block_filament_fallback_substitution || was_marked_incompatible(i)) {
                    // Preserve the user's original filament selection instead of silently
                    // substituting a different material; the UI marks this slot instead.
                    incompatible_nozzle_diameter_slots.push_back(i);
                    BOOST_LOG_TRIVIAL(warning) << boost::format(
                        "slot %1% incompatible with "
                        "nozzle diameter %2%, fallback substitution blocked") % i % target;
                    continue;
                }

                if (const Preset *fallback = find_default_filament_for_nozzle_diameter(
                        *this, target, printer_name_to_diameter)) {
                    filament_presets[i] = fallback->name;
                    if (i == 0)
                        filaments.select_preset_by_name(fallback->name, false, true);
                    const auto fallback_options = ::Slic3r::get_filament_nozzle_diameter_options(
                        *this, i, printer_name_to_diameter, configured);
                    ::Slic3r::set_filament_nozzle_diameter(
                        *this, i, target, nullptr, printer_name_to_diameter, fallback_options);
                }
            }
        }
        if (!multi_diameter_mode) {
            // Turning multi-diameter mode off only makes the per-slot selections inactive; restore
            // the retained values so turning it back on brings the project state back.
            selected->values = retained_selections;
        }
        // Keep filament_diameter_map populated even when only one diameter is installed. Blanking
        // compatible slots here used to force every row through the empty-map fallback, so adding a
        // second diameter (e.g. 0.2 beside existing 0.4) lost the prior 0.4 selections. Availability
        // still updates via `options` / incompatible flags above; the stored diameter is left alone.
        // A single configured diameter still leaves the flow selectable on a multi-nozzle rack, so the
        // flow repair has to run on this path too instead of only on the multi-diameter one below.
        repair_filament_nozzle_flows(*this, volume_map, installed, printer_name_to_diameter, configured);
        return;
    }

    auto tray_diameter = [this](const AMSMapInfo &mapping) {
        const double mapped_diameter = string_to_double_decimal_point(mapping.nozzle_diameter.c_str());
        if (mapped_diameter > 0.)
            return format_diameter_to_str(mapped_diameter);
        for (const auto &entry : filament_ams_list) {
            const DynamicPrintConfig &tray = entry.second;
            if (tray.opt_string("ams_id", 0u) != mapping.ams_id ||
                tray.opt_string("slot_id", 0u) != mapping.slot_id)
                continue;
            const auto *diameters = tray.option<ConfigOptionFloats>("tray_nozzle_diameter");
            if (diameters != nullptr && !diameters->values.empty() && diameters->values.front() > 0.)
                return format_diameter_to_str(diameters->values.front());
        }
        return std::string();
    };

    // Companion of tray_diameter() on the flow axis, with the same fallback to the live tray list for
    // mappings assembled before the flow was carried along.
    auto tray_volume_type = [this](const AMSMapInfo &mapping) -> std::optional<NozzleVolumeType> {
        if (mapping.nozzle_volume_type.has_value())
            return mapping.nozzle_volume_type;
        for (const auto &entry : filament_ams_list) {
            const DynamicPrintConfig &tray = entry.second;
            if (tray.opt_string("ams_id", 0u) != mapping.ams_id ||
                tray.opt_string("slot_id", 0u) != mapping.slot_id)
                continue;
            const auto *volume_types = tray.option<ConfigOptionInts>("tray_nozzle_volume_type");
            if (volume_types != nullptr && !volume_types->values.empty())
                return static_cast<NozzleVolumeType>(volume_types->values.front());
        }
        return std::nullopt;
    };

    for (size_t i = 0; i < filament_presets.size(); ++i) {
        if (is_mixed_filament(i)) {
            selected->values[i].clear();
            continue;
        }
        const auto options = ::Slic3r::get_filament_nozzle_diameter_options(
            *this, i, printer_name_to_diameter, configured);
        if (options.empty()) {
            // This filament preset has no compatible diameter among any currently installed nozzle
            // at all (stronger than a single diameter being removed below) -- flag it the same way
            // instead of silently blanking the slot's selection.
            if (!selected->values[i].empty())
                incompatible_nozzle_diameter_slots.push_back(i);
            continue;
        }

        // A failed nozzle-mapping keeps the requested diameter on a material that cannot serve it.
        // That leftover is still "installed" on a rack, so the usual checks below would treat the
        // slot as fine (or AMS sync would move it onto options.front()). Hold the red mark until
        // the stored diameter is actually in this filament's option list.
        //
        // An empty stored value is not such a leftover though -- set_filament_preset() blanks it on
        // any filament swap that is not just a diameter-variant change, including swapping back to
        // the very filament this slot was red for in the first place. Carrying the red mark over here
        // would skip the empty-diameter fallback below and leave the slot stuck red until the user
        // re-picks a nozzle by hand, even though nothing about the new selection is actually wrong.
        if (was_marked_incompatible(i) && !selected->values[i].empty()) {
            const std::string stored = format_diameter_to_str(string_to_double_decimal_point(selected->values[i].c_str()));
            if (std::find(options.begin(), options.end(), stored) == options.end()) {
                incompatible_nozzle_diameter_slots.push_back(i);
                continue;
            }
        }

        std::string device_diameter;
        std::optional<NozzleVolumeType> device_volume;
        if (device_maps != nullptr) {
            const auto mapped = device_maps->find(int(i));
            if (mapped != device_maps->end() && !mapped->second.ams_id.empty() && !mapped->second.slot_id.empty()) {
                device_diameter = tray_diameter(mapped->second);
                device_volume   = tray_volume_type(mapped->second);
            }
        }

        if (!device_diameter.empty()) {
            const std::string normalized_device = format_diameter_to_str(string_to_double_decimal_point(device_diameter.c_str()));
            if (std::find(options.begin(), options.end(), normalized_device) != options.end() &&
                ::Slic3r::set_filament_nozzle_diameter(
                    *this, i, normalized_device, nullptr, printer_name_to_diameter, options)) {
                // The diameter stuck, so the flow the machine reports for the same nozzle applies too.
                // volume_map is null outside multi-diameter mode, where this map is a grouping result
                // the slicer owns rather than a per-slot selection (see where it is resolved above),
                // so the flow is only ever written where the user owns it.
                if (volume_map != nullptr && device_volume.has_value()) {
                    const auto flows = ::Slic3r::get_filament_nozzle_flow_options(
                        *this, i, normalized_device, installed);
                    if (std::find(flows.begin(), flows.end(), *device_volume) != flows.end()) {
                        volume_map->values[i] = static_cast<int>(*device_volume);
                    } else if (device_maps_are_user_chosen) {
                        // The user paired this tray with this slot in the matching dialog, so the
                        // pairing is carried out even though the filament has no preset for that
                        // nozzle, and the slot is flagged. Quietly declining a pairing that was asked
                        // for would leave the dialog looking like it had no effect.
                        volume_map->values[i] = static_cast<int>(*device_volume);
                        incompatible_nozzle_diameter_slots.push_back(i);
                        BOOST_LOG_TRIVIAL(warning) << "filament " << i
                                                   << " has no preset for user-mapped nozzle flow "
                                                   << int(*device_volume) << " at diameter "
                                                   << normalized_device << ", applying it and flagging the slot";
                    } else {
                        // Nobody asked for this pairing -- it came out of the positional merge -- so
                        // the slot keeps the flow it had and stays printable. The mismatch belongs to
                        // the tray, and the AMS list already marks such trays unusable.
                        BOOST_LOG_TRIVIAL(warning) << "filament " << i
                                                   << " has no preset for mapped nozzle flow "
                                                   << int(*device_volume) << " at diameter "
                                                   << normalized_device << ", keeping the slot's flow";
                    }
                }
                continue;
            }

            // Reaching here means the machine says this tray feeds a nozzle the slot's filament has
            // no preset for. Swapping the material out for that nozzle's default -- what this used to
            // do -- is the silent substitution the rest of this function stopped doing: the material
            // came from the tray the user asked to sync, and quietly turning it into a generic PLA
            // hides that the two do not fit. Keep it, and record the diameter the machine actually
            // reports so the row's nozzle chip and the notification both name the right nozzle; the
            // slot is flagged below and the user decides.
            selected->values[i] = normalized_device;
            incompatible_nozzle_diameter_slots.push_back(i);
            BOOST_LOG_TRIVIAL(warning) << "filament " << i
                                       << " has no preset for mapped nozzle diameter " << normalized_device
                                       << ", keeping the filament and flagging the slot";
            continue;
        }

        // Only reached when the device named no diameter for this slot, so there is nothing authoritative
        // to apply and the slot's own selection stands. Forcing options.front() here instead dragged every
        // slot the mapping left out down to the smallest installed diameter -- the match dialog maps only
        // the filaments the plate uses, so that silently rewrote all the others. Never rewrite a diameter
        // on the user's behalf: if what the slot carries turns out to be unusable, the pass below marks it
        // red and the user resolves it.
        std::string target = selected->values[i];
        const std::string normalized = format_diameter_to_str(string_to_double_decimal_point(target.c_str()));
        if (std::find(options.begin(), options.end(), normalized) == options.end()) {
            // A non-empty target that fell out of `options` means a nozzle of that diameter was
            // physically removed (Nozzle Settings dialog). Flag it instead of silently moving the
            // slot onto another diameter -- availability is refreshed; the selection is not rewritten.
            if (!target.empty() && multi_diameter_mode) {
                incompatible_nozzle_diameter_slots.push_back(i);
                continue;
            }
            target = options.front();
        }
        // `options` only says a filament preset exists for that diameter. The rack still has to carry a
        // nozzle of that diameter whose flow this slot's filament can print, otherwise an inherited or
        // preset-switched "0.4 Standard" survives on a slot whose 2.85mm TPU only fits a TPU285 nozzle.
        if (volume_map != nullptr) {
            const auto flows = ::Slic3r::get_filament_nozzle_flow_options(*this, i, target, installed);
            const NozzleVolumeType current_flow = i < volume_map->values.size()
                ? static_cast<NozzleVolumeType>(volume_map->values[i]) : nvtStandard;
            if (flows.empty()) {
                // Nothing printable at this diameter at all, so moving the slot to a diameter that does
                // work is still an improvement rather than a rewrite of a usable choice.
                bool moved_to_printable_diameter = false;
                for (const std::string &candidate : options)
                    if (!::Slic3r::get_filament_nozzle_flow_options(*this, i, candidate, installed).empty()) {
                        target = candidate;
                        moved_to_printable_diameter = true;
                        break;
                    }
                // No installed nozzle can print this filament at any of its diameters.
                if (!moved_to_printable_diameter) {
                    incompatible_nozzle_diameter_slots.push_back(i);
                    BOOST_LOG_TRIVIAL(warning) << "filament " << i
                                               << " has no printable nozzle flow at any installed diameter"
                                               << ", flagging the slot";
                    continue;
                }
            } else if (std::find(flows.begin(), flows.end(), current_flow) == flows.end()) {
                // The diameter is fine, only the flow the slot names is not on the rack. Flagged
                // rather than quietly moved to the flow that is -- see the single-diameter branch.
                incompatible_nozzle_diameter_slots.push_back(i);
                BOOST_LOG_TRIVIAL(warning) << "filament " << i
                                           << " cannot print on the " << target << " nozzle in flow "
                                           << int(current_flow) << ", flagging the slot";
                continue;
            }
        }
        if (!::Slic3r::set_filament_nozzle_diameter(
                *this, i, target, nullptr, printer_name_to_diameter, options))
            selected->values[i].clear();
    }

    // Runs after the loop, not inside it: set_filament_nozzle_diameter() can switch a slot to a
    // sibling preset serving another diameter, which changes filament_extruder_variant, so the flow
    // has to be resolved against the diameter that actually stuck.
    sync_mixed_filament_nozzle_diameters(project_config);
    repair_filament_nozzle_flows(*this, volume_map, installed, printer_name_to_diameter, configured);
}

//BBS: check whether this is the only edited filament
bool PresetBundle::is_the_only_edited_filament(unsigned int filament_index)
{
    unsigned n = this->filament_presets.size();
    if (filament_index >= n)
        return false;

    std::string name = this->filament_presets[filament_index];
    Preset& edited_preset = this->filaments.get_edited_preset();
    if (edited_preset.name != name)
        return false;

    unsigned index = 0;
    while (index < n)
    {
        if (index == filament_index) {
            index ++;
            continue;
        }
        std::string filament_preset = this->filament_presets[index];
        if (edited_preset.name == filament_preset)
            return false;
        else
            index ++;
    }
    return true;
}

int PresetBundle::get_printer_extruder_count() const
{
    const Preset& printer_preset = this->printers.get_edited_preset();

    int count = printer_preset.config.option<ConfigOptionFloatsNullable>("nozzle_diameter")->values.size();

    return count;
}

bool PresetBundle::support_different_extruders()
{
    Preset& printer_preset = this->printers.get_edited_preset();
    int extruder_count;
    bool supported = printer_preset.config.support_different_extruders(extruder_count);

    return supported;
}

std::vector<NozzleVolumeType> PresetBundle::get_printer_nozzle_volume_list()
{
    const Preset& printer_preset = this->printers.get_edited_preset();
    auto variants = printer_preset.config.option<ConfigOptionStrings>("printer_extruder_variant");

    std::set<NozzleVolumeType> unique_types;
    if (variants) {
        for (const std::string& variant : variants->values) {
            NozzleVolumeType nvt = convert_to_nvt_type(variant);
            unique_types.insert(nvt);
        }
    }

    return std::vector<NozzleVolumeType>(unique_types.begin(), unique_types.end());
}

std::vector<int> PresetBundle::get_default_nozzle_volume_types_for_filaments(std::vector<int>& f_maps)
{
    std::vector<int> result;
    int filament_count = f_maps.size();
    result.resize(filament_count, static_cast<int>(NozzleVolumeType::nvtStandard));

    auto opt_nozzle_volume_type = dynamic_cast<const ConfigOptionEnumsGeneric*>(this->project_config.option("nozzle_volume_type"));
    for (int index = 0; index < filament_count; index++)
    {
        if (opt_nozzle_volume_type && opt_nozzle_volume_type->values.size() > (f_maps[index] - 1))
            result[index] = opt_nozzle_volume_type->values[f_maps[index] - 1];
    }

    return result;
}

DynamicPrintConfig PresetBundle::full_config(bool apply_extruder, std::optional<std::vector<int>>filament_maps, std::optional<std::vector<int>> filament_volume_maps) const
{
    return (this->printers.get_edited_preset().printer_technology() == ptFFF) ?
        this->full_fff_config(apply_extruder, filament_maps, filament_volume_maps) :
        this->full_sla_config();
}

DynamicPrintConfig PresetBundle::full_config_secure(std::optional<std::vector<int>>filament_maps) const
{
    DynamicPrintConfig config = this->full_fff_config(false, filament_maps);
    //BBS example: config.erase("print_host");
    config.erase("print_host_webui");
    config.erase("printhost_apikey");
    config.erase("printhost_cafile");
    config.erase("printhost_user");
    config.erase("printhost_password");
    config.erase("printhost_port");
    return config;
}

const std::set<std::string> ignore_settings_list ={
    "inherits",
    "print_settings_id", "filament_settings_id", "printer_settings_id"
};

DynamicPrintConfig PresetBundle::full_fff_config(bool apply_extruder, std::optional<std::vector<int>> filament_maps_new, std::optional<std::vector<int>> filament_volume_maps_new) const
{
    DynamicPrintConfig out;
    out.apply(FullPrintConfig::defaults());
    out.apply(this->prints.get_edited_preset().config);
    // Add the default filament preset to have the "filament_preset_id" defined.
	out.apply(this->filaments.default_preset().config);
	out.apply(this->printers.get_edited_preset().config);
    out.apply(this->project_config);

    // BBS
    size_t  num_filaments = this->filament_presets.size();

    std::vector<int> filament_maps = out.option<ConfigOptionInts>("filament_map")->values;
    std::vector<int> filament_volume_maps(num_filaments, (int)nvtStandard);

    ConfigOptionInts* filament_volume_map_opt = out.option<ConfigOptionInts>("filament_volume_map");
    if (filament_maps_new.has_value())
        filament_maps = *filament_maps_new;
    if (filament_volume_maps_new.has_value()) {
        filament_volume_maps = *filament_volume_maps_new;
        out.option<ConfigOptionInts>("filament_volume_map", true)->values = filament_volume_maps;
    }
    else if (filament_volume_map_opt && filament_volume_map_opt->values.size() == num_filaments)
        filament_volume_maps = filament_volume_map_opt->values;
    //in some middle state, they may be different
    if (filament_maps.size() != num_filaments) {
        filament_maps.resize(num_filaments, 1);
    }
    if (filament_volume_maps.size() != num_filaments) {
        filament_volume_maps.resize(num_filaments, nvtStandard);
    }

    auto* extruder_diameter = dynamic_cast<const ConfigOptionFloatsNullable*>(out.option("nozzle_diameter"));
    // Collect the "compatible_printers_condition" and "inherits" values over all presets (print, filaments, printers) into a single vector.
    std::vector<std::string> compatible_printers_condition;
    std::vector<std::string> compatible_prints_condition;
    std::vector<std::string> inherits;
    std::vector<std::string> filament_ids;
    std::vector<std::string> print_compatible_printers;
    //BBS: add logic for settings check between different system presets
    std::vector<std::string> different_settings;
    std::string different_print_settings, different_printer_settings;
    compatible_printers_condition.emplace_back(this->prints.get_edited_preset().compatible_printers_condition());

    const ConfigOptionStrings* compatible_printers =  (const_cast<PresetBundle*>(this))->prints.get_edited_preset().config.option<ConfigOptionStrings>("compatible_printers", false);
    if (compatible_printers)
        print_compatible_printers = compatible_printers->values;
    //BBS: add logic for settings check between different system presets
    std::string print_inherits = this->prints.get_edited_preset().inherits();
    inherits                     .emplace_back(print_inherits);
    const Preset* print_parent_preset =  this->prints.get_selected_preset_parent();
    if (print_parent_preset) {
        std::vector<std::string> dirty_options = this->prints.dirty_options_without_option_list(&(this->prints.get_edited_preset()), print_parent_preset, ignore_settings_list, false);
        if (!dirty_options.empty()) {
            different_print_settings = Slic3r::escape_strings_cstyle(dirty_options);
        }
    }
    different_settings.emplace_back(different_print_settings);

    //BBS: update printer config related with variants
    std::vector<std::vector<NozzleVolumeType>> nozzle_volume_types;
    int extruder_count = 1, extruder_volume_type_count = 1;
    bool different_extruder = false;
    if (apply_extruder) {
        different_extruder = out.support_different_extruders(extruder_count);
        extruder_volume_type_count = out.get_extruder_nozzle_volume_count(extruder_count, nozzle_volume_types);

        if ((extruder_count > 1) || different_extruder) {
            out.update_values_to_printer_extruders(out, extruder_count, extruder_volume_type_count, nozzle_volume_types, printer_options_with_variant_2, "printer_extruder_id", "printer_extruder_variant", 2);
            out.update_values_to_printer_extruders(out, extruder_count, extruder_volume_type_count, nozzle_volume_types, printer_options_with_variant_1, "printer_extruder_id", "printer_extruder_variant");
            //update print config related with variants
            out.update_values_to_printer_extruders(out, extruder_count, extruder_volume_type_count, nozzle_volume_types, print_options_with_variant, "print_extruder_id", "print_extruder_variant");
        }
    }

    if (num_filaments <= 1) {
        //BBS: update filament config related with variants
        DynamicPrintConfig filament_config = this->filaments.get_edited_preset().config;
        if (apply_extruder && ((extruder_count > 1) || different_extruder))
            filament_config.update_values_to_printer_extruders(out, extruder_count, extruder_volume_type_count, nozzle_volume_types, filament_options_with_variant, "", "filament_extruder_variant", 1, filament_maps[0],(NozzleVolumeType)filament_volume_maps[0]);
        out.apply(filament_config);
        compatible_printers_condition.emplace_back(this->filaments.get_edited_preset().compatible_printers_condition());
        compatible_prints_condition  .emplace_back(this->filaments.get_edited_preset().compatible_prints_condition());
        //BBS: add logic for settings check between different system presets
        //std::string filament_inherits = this->filaments.get_edited_preset().inherits();
        std::string current_preset_name = this->filament_presets[0];
        const Preset* preset = this->filaments.find_preset(current_preset_name, true);
        std::string filament_inherits = preset->inherits();
        inherits                     .emplace_back(filament_inherits);
        filament_ids.emplace_back(this->filaments.get_edited_preset().filament_id);

        std::string different_filament_settings;
        const Preset* filament_parent_preset =  this->filaments.get_selected_preset_parent();
        if (filament_parent_preset) {
            std::vector<std::string> dirty_options = this->filaments.dirty_options_without_option_list(&(this->filaments.get_edited_preset()), filament_parent_preset, ignore_settings_list, false);
            if (!dirty_options.empty()) {
                different_filament_settings = Slic3r::escape_strings_cstyle(dirty_options);
            }
        }

        different_settings.emplace_back(different_filament_settings);

        std::vector<int>& filament_self_indice = out.option<ConfigOptionInts>("filament_self_index", true)->values;
        int index_size = out.option<ConfigOptionStrings>("filament_extruder_variant")->size();
        filament_self_indice.resize(index_size, 1);
    } else {
        // Retrieve filament presets and build a single config object for them.
        // First collect the filament configurations based on the user selection of this->filament_presets.
        // Here this->filaments.find_preset() and this->filaments.first_visible() return the edited copy of the preset if active.
        std::vector<const DynamicPrintConfig*> filament_configs;
        std::vector<const Preset*> filament_presets;
        for (const std::string& filament_preset_name : this->filament_presets) {
            const Preset* preset = this->filaments.find_preset(filament_preset_name, true);
            filament_presets.emplace_back(preset);
            filament_configs.emplace_back(&(preset->config));
        }
        while (filament_configs.size() < num_filaments) {
            const Preset* preset = &this->filaments.first_visible();
            filament_presets.emplace_back(preset);
            filament_configs.emplace_back(&(preset->config));
        }
        for (int index = 0; index < num_filaments; index++) {
            const DynamicPrintConfig *cfg = filament_configs[index];
            const Preset *preset = filament_presets[index];
            // The compatible_prints/printers_condition() returns a reference to configuration key, which may not yet exist.
            DynamicPrintConfig &cfg_rw = *const_cast<DynamicPrintConfig*>(cfg);
            compatible_printers_condition.emplace_back(Preset::compatible_printers_condition(cfg_rw));
            compatible_prints_condition  .emplace_back(Preset::compatible_prints_condition(cfg_rw));

            //BBS: add logic for settings check between different system presets
            std::string filament_inherits = Preset::inherits(cfg_rw);
            inherits                     .emplace_back(filament_inherits);
            filament_ids.emplace_back(preset->filament_id);
            std::string different_filament_settings;

            const Preset* filament_parent_preset = nullptr;
            if (preset->is_system || preset->is_default) {
                bool is_selected = this->filaments.get_selected_preset_name() == preset->name;
                if (is_selected) {
                    //use the real preset
                    filament_parent_preset = const_cast<PresetBundle*>(this)->filaments.find_preset(preset->name, false, true);
                }
                else {
                    filament_parent_preset = preset;
                }
            }
            else if (!filament_inherits.empty())
                filament_parent_preset =  const_cast<PresetBundle*>(this)->filaments.find_preset(filament_inherits, false, true);

            if (filament_parent_preset) {
                std::vector<std::string> dirty_options = cfg_rw.diff(filament_parent_preset->config);
                if (!dirty_options.empty()) {
                    auto iter = dirty_options.begin();
                    while (iter != dirty_options.end()) {
                        if (ignore_settings_list.find(*iter) != ignore_settings_list.end()) {
                            iter = dirty_options.erase(iter);
                        }
                        else {
                            ++iter;
                        }
                    }
                    different_filament_settings = Slic3r::escape_strings_cstyle(dirty_options);
                }
            }

            different_settings.emplace_back(different_filament_settings);
        }

        std::vector<DynamicPrintConfig> filament_temp_configs;
        filament_temp_configs.resize(num_filaments);
        for (size_t i = 0; i < num_filaments; ++i) {
            filament_temp_configs[i] = *(filament_configs[i]);
            if (apply_extruder && ((extruder_count > 1) || different_extruder))
                filament_temp_configs[i].update_values_to_printer_extruders(out, extruder_count, extruder_volume_type_count, nozzle_volume_types, filament_options_with_variant, "", "filament_extruder_variant", 1, filament_maps[i],(NozzleVolumeType)filament_volume_maps[i]);
        }

        // loop through options and apply them to the resulting config.
        std::vector<int> filament_variant_count(num_filaments, 1);
        for (const t_config_option_key &key : this->filaments.default_preset().config.keys()) {
			if (key == "compatible_prints" || key == "compatible_printers")
				continue;
            // Get a destination option.
            ConfigOption *opt_dst = out.option(key, false);
            if (opt_dst->is_scalar()) {
                // Get an option, do not create if it does not exist.
                const ConfigOption *opt_src = filament_temp_configs.front().option(key);
                if (opt_src != nullptr)
                    opt_dst->set(opt_src);
            } else {
                // BBS
                ConfigOptionVectorBase* opt_vec_dst = static_cast<ConfigOptionVectorBase*>(opt_dst);
                {
                    if (apply_extruder) {
                        std::vector<const ConfigOption*> filament_opts(num_filaments, nullptr);
                        // Setting a vector value from all filament_configs.
                        for (size_t i = 0; i < filament_opts.size(); ++i)
                            filament_opts[i] = filament_temp_configs[i].option(key);
                        opt_vec_dst->set(filament_opts);
                    }
                    else {
                        for (size_t i = 0; i < num_filaments; ++i) {
                            const ConfigOptionVectorBase* filament_option = static_cast<const ConfigOptionVectorBase*>(filament_temp_configs[i].option(key));
                            if (i == 0)
                                opt_vec_dst->set(filament_option);
                            else
                                opt_vec_dst->append(filament_option);

                            if (key == "filament_extruder_variant")
                                filament_variant_count[i] = filament_option->size();
                        }
                    }
                }
            }
        }

        if (apply_extruder) {
            std::vector<int>& filament_self_indice = out.option<ConfigOptionInts>("filament_self_index", true)->values;
            int index_size = out.option<ConfigOptionStrings>("filament_extruder_variant")->size();
            filament_self_indice.resize(index_size, 1);
            for (size_t i = 0; i < num_filaments && i < filament_self_indice.size(); i++) {
                filament_self_indice[i] = int(i + 1);
            }
        } else {
            //append filament_self_index
            std::vector<int>& filament_self_indice = out.option<ConfigOptionInts>("filament_self_index", true)->values;
            int index_size = out.option<ConfigOptionStrings>("filament_extruder_variant")->size();
            filament_self_indice.resize(index_size, 1);
            int k = 0;
            for (size_t i = 0; i < num_filaments; i++) {
                for (size_t j = 0; j < filament_variant_count[i]; j++) {
                    filament_self_indice[k++] = i + 1;
                }
            }
        }
    }

    // Filament overrides edited as one value per filament (the variant vector was still
    // length 1) must be copied onto every variant slot before extruder lookup.
    align_filament_variant_overrides(out);

    //BBS: add logic for settings check between different system presets
    std::string printer_inherits = this->printers.get_edited_preset().inherits();
    // Don't store the "compatible_printers_condition" for the printer profile, there is none.
    inherits                     .emplace_back(printer_inherits);
    const Preset* printer_parent_preset =  this->printers.get_selected_preset_parent();
    if (printer_parent_preset) {
        std::vector<std::string> dirty_options = this->printers.dirty_options_without_option_list(&(this->printers.get_edited_preset()), printer_parent_preset, ignore_settings_list, false);
        if (!dirty_options.empty()) {
            different_printer_settings = Slic3r::escape_strings_cstyle(dirty_options);
        }
    }
    different_settings.emplace_back(different_printer_settings);

    // These value types clash between the print and filament profiles. They should be renamed.
    out.erase("compatible_prints");
    out.erase("compatible_prints_condition");
    out.erase("compatible_printers");
    out.erase("compatible_printers_condition");
    out.erase("inherits");
    //BBS: add logic for settings check between different system presets
    out.erase("different_settings_to_system");
    out.erase("filament_map_2");

    static const char *keys[] = { "support_filament", "support_interface_filament" };
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++ i) {
        std::string key = std::string(keys[i]);
        auto *opt = dynamic_cast<ConfigOptionInt*>(out.option(key, false));
        assert(opt != nullptr);
        opt->value = boost::algorithm::clamp<int>(opt->value, 0, int(num_filaments));
    }

    out.option<ConfigOptionString >("print_settings_id",    true)->value  = this->prints.get_selected_preset_name();
    out.option<ConfigOptionStrings>("filament_settings_id", true)->values = this->filament_presets;
    out.option<ConfigOptionString >("printer_settings_id",  true)->value  = this->printers.get_selected_preset_name();
    out.option<ConfigOptionStrings>("filament_ids", true)->values = filament_ids;
    out.option<ConfigOptionInts>("filament_map", true)->values = filament_maps;
    // Serialize the collected "compatible_printers_condition" and "inherits" fields.
    // There will be 1 + num_exturders fields for "inherits" and 2 + num_extruders for "compatible_printers_condition" stored.
    // The vector will not be stored if all fields are empty strings.
    auto add_if_some_non_empty = [&out](std::vector<std::string> &&values, const std::string &key) {
        bool nonempty = false;
        for (const std::string &v : values)
            if (! v.empty()) {
                nonempty = true;
                break;
            }
        if (nonempty)
            out.set_key_value(key, new ConfigOptionStrings(std::move(values)));
    };
    add_if_some_non_empty(std::move(compatible_printers_condition), "compatible_machine_expression_group");
    add_if_some_non_empty(std::move(compatible_prints_condition),   "compatible_process_expression_group");
    add_if_some_non_empty(std::move(inherits),                      "inherits_group");
    //BBS: add logic for settings check between different system presets
    add_if_some_non_empty(std::move(different_settings),            "different_settings_to_system");
    add_if_some_non_empty(std::move(print_compatible_printers),     "print_compatible_printers");
    out.option<ConfigOptionStrings>("extruder_ams_count", true)->values   = save_extruder_ams_count_to_string(this->extruder_ams_counts);
    // Nozzle counts and per-slot nozzle params for the grouping backend.
    this->nozzle_inventory.export_to(out);

    // Multi-diameter: let slicing read the user-selected diameters (persisted with the project through
    // the nozzle inventory) rather than the printer preset's fixed nozzle_diameter. Preset switching is
    // kept on the UI side so diameter-dependent tuning (layer height, line width, flow) still follows
    // the preset; here we only override the raw per-extruder diameter values, preserving the array
    // length and falling back to the preset value for any extruder without an installed nozzle.
    // The topology gate includes both multi-extruder machines and single-extruder multi-slot machines.
    // A single-extruder single-slot machine must keep the active preset diameter instead of a stale slot snapshot.
    // Both overrides below are what makes a config multi-diameter, so the project-level gate comes first:
    // with it off the printer preset's own nozzle_diameter and no per-filament selection is all slicing sees.
    const bool multi_diameter_mode = multi_nozzle_diameter_mode_enabled(this->project_config);
    if (auto *nozzle_diameter_opt = out.option<ConfigOptionFloatsNullable>("nozzle_diameter");
        multi_diameter_mode && uses_per_slot_nozzle_diameters(out) &&
        nozzle_diameter_opt != nullptr && this->nozzle_inventory.extruder_count() > 0) {
        nozzle_diameter_opt->values = smallest_installed_diameters(this->nozzle_inventory, nozzle_diameter_opt->values);
    }

    std::vector<std::string> selected_nozzle_diameters;
    if (multi_diameter_mode)
        selected_nozzle_diameters = this->project_config.option<ConfigOptionStrings>("filament_diameter_map")->values;
    selected_nozzle_diameters.resize(filament_ids.size());
    out.option<ConfigOptionStrings>("filament_diameter_map", true)->values =
        std::move(selected_nozzle_diameters);

	out.option<ConfigOptionEnumGeneric>("printer_technology", true)->value = ptFFF;
    return out;
}

DynamicPrintConfig PresetBundle::full_sla_config() const
{
    DynamicPrintConfig out;
    out.apply(SLAFullPrintConfig::defaults());
    out.apply(this->sla_prints.get_edited_preset().config);
    out.apply(this->sla_materials.get_edited_preset().config);
    out.apply(this->printers.get_edited_preset().config);
    // There are no project configuration values as of now, the project_config is reserved for FFF printers.
//    out.apply(this->project_config);

    // Collect the "compatible_printers_condition" and "inherits" values over all presets (sla_prints, sla_materials, printers) into a single vector.
    std::vector<std::string> compatible_printers_condition;
	std::vector<std::string> compatible_prints_condition;
    std::vector<std::string> inherits;
    compatible_printers_condition.emplace_back(this->sla_prints.get_edited_preset().compatible_printers_condition());
	inherits					 .emplace_back(this->sla_prints.get_edited_preset().inherits());
    compatible_printers_condition.emplace_back(this->sla_materials.get_edited_preset().compatible_printers_condition());
	compatible_prints_condition  .emplace_back(this->sla_materials.get_edited_preset().compatible_prints_condition());
    inherits                     .emplace_back(this->sla_materials.get_edited_preset().inherits());
    inherits                     .emplace_back(this->printers.get_edited_preset().inherits());

    // These two value types clash between the print and filament profiles. They should be renamed.
    out.erase("compatible_printers");
    out.erase("compatible_printers_condition");
    out.erase("inherits");

    out.option<ConfigOptionString >("sla_print_settings_id",    true)->value  = this->sla_prints.get_selected_preset_name();
    out.option<ConfigOptionString >("sla_material_settings_id", true)->value  = this->sla_materials.get_selected_preset_name();
    out.option<ConfigOptionString >("printer_settings_id",      true)->value  = this->printers.get_selected_preset_name();

    // Serialize the collected "compatible_printers_condition" and "inherits" fields.
    // There will be 1 + num_exturders fields for "inherits" and 2 + num_extruders for "compatible_printers_condition" stored.
    // The vector will not be stored if all fields are empty strings.
    auto add_if_some_non_empty = [&out](std::vector<std::string> &&values, const std::string &key) {
        bool nonempty = false;
        for (const std::string &v : values)
            if (! v.empty()) {
                nonempty = true;
                break;
            }
        if (nonempty)
            out.set_key_value(key, new ConfigOptionStrings(std::move(values)));
    };
    add_if_some_non_empty(std::move(compatible_printers_condition), "compatible_machine_expression_group");
    add_if_some_non_empty(std::move(compatible_prints_condition),   "compatible_process_expression_group");
    add_if_some_non_empty(std::move(inherits),                      "inherits_group");

	out.option<ConfigOptionEnumGeneric>("printer_technology", true)->value = ptSLA;
	return out;
}

// Load an external config file containing the print, filament and printer presets.
// Instead of a config file, a G-code may be loaded containing the full set of parameters.
// In the future the configuration will likely be read from an AMF file as well.
// If the file is loaded successfully, its print / filament / printer profiles will be activated.
ConfigSubstitutions PresetBundle::load_config_file(const std::string &path, ForwardCompatibilitySubstitutionRule compatibility_rule)
{
	if (is_gcode_file(path)) {
		DynamicPrintConfig config;
        //BBS: add config related logs
        BOOST_LOG_TRIVIAL(debug) << boost::format("enter, gcodefile %1%, compatibility_rule %2%")%path %compatibility_rule;
		config.apply(FullPrintConfig::defaults());
        ConfigSubstitutions config_substitutions = config.load_from_gcode_file(path, compatibility_rule);
        Preset::normalize(config);
		load_config_file_config(path, true, std::move(config));
		return config_substitutions;
	}

    //BBS: add config related logs
    BOOST_LOG_TRIVIAL(debug) << boost::format("can not load config file %1% not from gcode")%path ;
    throw Slic3r::RuntimeError(std::string("Unknown configuration file: ") + path);
    // 1) Try to load the config file into a boost property tree.
    /*boost::property_tree::ptree tree;
    try {
        boost::nowide::ifstream ifs(path);
        boost::property_tree::read_ini(ifs, tree);
    } catch (const std::ifstream::failure &err) {
        throw Slic3r::RuntimeError(std::string("The Config Bundle cannot be loaded: ") + path + "\n\tReason: " + err.what());
    } catch (const boost::property_tree::file_parser_error &err) {
        throw Slic3r::RuntimeError(format("Failed loading the Config Bundle \"%1%\": %2% at line %3%",
        	err.filename(), err.message(), err.line()));
    } catch (const std::runtime_error &err) {
        throw Slic3r::RuntimeError(std::string("Failed loading the preset file: ") + path + "\n\tReason: " + err.what());
    }

    // 2) Continue based on the type of the configuration file.
    ConfigFileType config_file_type = guess_config_file_type(tree);
    ConfigSubstitutions config_substitutions;
    try {
        switch (config_file_type) {
        case CONFIG_FILE_TYPE_UNKNOWN:
            throw Slic3r::RuntimeError(std::string("Unknown configuration file type: ") + path);
        case CONFIG_FILE_TYPE_APP_CONFIG:
            throw Slic3r::RuntimeError(std::string("Invalid configuration file: ") + path + ". This is an application config file.");
    	case CONFIG_FILE_TYPE_CONFIG:
    	{
    		// Initialize a config from full defaults.
    		DynamicPrintConfig config;
    		config.apply(FullPrintConfig::defaults());
            config_substitutions = config.load(tree, compatibility_rule);
    		Preset::normalize(config);
    		load_config_file_config(path, true, std::move(config));
            //BBS: add config related logs
            BOOST_LOG_TRIVIAL(debug) << boost::format("normal config, finished, got %1% substitutions")%config_substitutions.size();
            return config_substitutions;
        }
        case CONFIG_FILE_TYPE_CONFIG_BUNDLE:
            //BBS: add config related logs
            BOOST_LOG_TRIVIAL(debug) << boost::format("found config bundle");
            return load_config_file_config_bundle(path, tree, compatibility_rule);
        }
    } catch (const ConfigurationError &e) {
        throw Slic3r::RuntimeError(format("Invalid configuration file %1%: %2%", path, e.what()));
    }

    //BBS: add config related logs
    BOOST_LOG_TRIVIAL(error) << boost::format("finished, should not be here");
    // This shall never happen. Suppres compiler warnings.
    assert(false);*/
    return ConfigSubstitutions{};
}


// some filament presets split from one to sperate ones
// following map recording these filament presets
// for example: previously ''Bambu PLA Basic @BBL H2D 0.6 nozzle' was saved in ''Bambu PLA Basic @BBL H2D' with 0.4
bool find_filament_preset_map_name(std::string machine_name, std::string filament_old_name, std::string &filament_new_name)
{
    static std::map<std::string, std::map<std::string, std::string>> filament_preset_convert;

    if (filament_preset_convert.empty()) {
        namespace fs  = boost::filesystem;
        fs::path path = fs::path(Slic3r::resources_dir()) / "profiles" / "BBL" / "filament" / "filament_name_map.json";

        std::ifstream file(path.string());
        if (!file.is_open()) {
            BOOST_LOG_TRIVIAL(debug) << boost::format("Failed to open filament preset map: %1%") % path.string();
            return false;
        }

        json j;
        try {
            file >> j;
        } catch (const std::exception &e) {
            BOOST_LOG_TRIVIAL(debug) << boost::format("JSON parse error: %1%") % e.what();
            return false;
        }

        for (auto &[printer_name, filament_json] : j.items()) {
            std::map<std::string, std::string> inner_map;
            for (auto &[old_name, new_name] : filament_json.items()) inner_map[old_name] = new_name;
            filament_preset_convert[printer_name] = std::move(inner_map);
        }
    }

    auto machine_iter = filament_preset_convert.find(machine_name);
    if (machine_iter != filament_preset_convert.end()) {
        std::map<std::string, std::string> &filament_maps = machine_iter->second;
        auto                                filament_iter = filament_maps.find(filament_old_name);
        if (filament_iter != filament_maps.end()) {
            filament_new_name = filament_iter->second;
            return true;
        }
    }
    return false;
}

// convert the old filament preset to new one after split
void convert_filament_preset_name(std::string &machine_name, std::string &filament_name)
{
    std::string filament_new_name;
    if (find_filament_preset_map_name(machine_name, filament_name, filament_new_name)) filament_name = filament_new_name;
}
// Load a config file from a boost property_tree. This is a private method called from load_config_file.
// is_external == false on if called from ConfigWizard
void PresetBundle::load_config_file_config(const std::string &name_or_path, bool is_external, DynamicPrintConfig &&config, Semver file_version, bool selected)
{
    PrinterTechnology printer_technology = Preset::printer_technology(config);

    auto clear_compatible_printers = [](DynamicPrintConfig& config){
        ConfigOption *opt_compatible = config.optptr("compatible_printers");
        if (opt_compatible != nullptr) {
            assert(opt_compatible->type() == coStrings);
            if (opt_compatible->type() == coStrings)
                static_cast<ConfigOptionStrings*>(opt_compatible)->values.clear();
        }
    };
    clear_compatible_printers(config);

#if 0
    size_t num_extruders = (printer_technology == ptFFF) ?
        std::min(config.option<ConfigOptionFloats>("nozzle_diameter"  )->values.size(),
                 config.option<ConfigOptionFloats>("filament_diameter")->values.size()) :
		// 1 SLA material
        1;
#else
    // BBS: use filament_colour insteadof filament_settings_id, filament_settings_id sometimes is not generated
    ConfigOptionStrings* filament_colour_option = config.option<ConfigOptionStrings>("filament_colour");
    size_t num_filaments = filament_colour_option?filament_colour_option->size():0;
    if (num_filaments == 0)
        throw Slic3r::RuntimeError(std::string("Invalid configuration file: ") + name_or_path);
#endif

    //BBS: add config related logs
    BOOST_LOG_TRIVIAL(debug) << boost::format("name_or_path %1%, is_external %2%, num_filaments %3%") % name_or_path % is_external % num_filaments;
    // Make a copy of the "compatible_machine_expression_group" and "inherits_group" vectors, which
    // accumulate values over all presets (print, filaments, printers).
    // These values will be distributed into their particular presets when loading.
    std::vector<std::string> compatible_printers_condition_values   = std::move(config.option<ConfigOptionStrings>("compatible_machine_expression_group", true)->values);
    std::vector<std::string> compatible_prints_condition_values     = std::move(config.option<ConfigOptionStrings>("compatible_process_expression_group",   true)->values);
    std::vector<std::string> inherits_values                        = std::move(config.option<ConfigOptionStrings>("inherits_group", true)->values);
    std::vector<std::string> filament_ids                           = std::move(config.option<ConfigOptionStrings>("filament_ids", true)->values);
    std::vector<std::string> print_compatible_printers              = std::move(config.option<ConfigOptionStrings>("print_compatible_printers", true)->values);
    //BBS: add different settings check logic
    bool has_different_settings_to_system                           = config.option("different_settings_to_system")?true:false;
    std::vector<std::string> different_values                       = std::move(config.option<ConfigOptionStrings>("different_settings_to_system", true)->values);
    std::string &compatible_printers_condition  = Preset::compatible_printers_condition(config);
    std::string &compatible_prints_condition    = Preset::compatible_prints_condition(config);
    std::string &inherits                       = Preset::inherits(config);
    compatible_printers_condition_values.resize(num_filaments + 2, std::string());
    compatible_prints_condition_values.resize(num_filaments, std::string());
    inherits_values.resize(num_filaments + 2, std::string());
    different_values.resize(num_filaments + 2, std::string());
    filament_ids.resize(num_filaments, std::string());
    // The "default_filament_profile" will be later extracted into the printer profile.
	switch (printer_technology) {
	case ptFFF:
		config.option<ConfigOptionString>("default_print_profile", true);
        config.option<ConfigOptionStrings>("default_filament_profile", true);
		break;
	case ptSLA:
		config.option<ConfigOptionString>("default_sla_print_profile", true);
		config.option<ConfigOptionString>("default_sla_material_profile", true);
		break;
    default: break;
	}

    bool process_multi_extruder = false;
    std::vector<int> filament_variant_index;
    size_t extruder_variant_count;
    if (!config.option<ConfigOptionInts>("filament_self_index")) {
        std::vector<int>& filament_self_indice = config.option<ConfigOptionInts>("filament_self_index", true)->values;
        filament_self_indice.resize(num_filaments);
        for (int index = 0; index < num_filaments; index++)
            filament_self_indice[index] = index + 1;
    }
    std::vector<int> filament_self_indice = std::move(config.option<ConfigOptionInts>("filament_self_index")->values);
    if (config.option("extruder_variant_list")) {
        //3mf support multiple extruder logic
        size_t extruder_count = config.option<ConfigOptionFloatsNullable>("nozzle_diameter")->values.size();
        extruder_variant_count = config.option<ConfigOptionStrings>("filament_extruder_variant", true)->size();
        if ((extruder_variant_count != filament_self_indice.size())
            || (extruder_variant_count < num_filaments)) {
            assert(false);
            BOOST_LOG_TRIVIAL(error) << boost::format("invalid config file %1%, can not find suitable filament_extruder_variant or filament_self_index") % name_or_path;
            throw Slic3r::RuntimeError(std::string("Invalid configuration file: ") + name_or_path);
        }
        if (num_filaments != extruder_variant_count) {
            process_multi_extruder = true;
            filament_variant_index.resize(num_filaments, 0);

            size_t cur_filament_id = 1;
            for (size_t index = 0; index < filament_self_indice.size(); index++) {
                if (filament_self_indice[index] == cur_filament_id) {
                    filament_variant_index[cur_filament_id - 1] = index;
                    cur_filament_id++;
                    if (cur_filament_id > num_filaments)
                        break;
                }
            }
        }
    }
    //no need to parse extruder_ams_count
    std::vector<std::string> extruder_ams_count = std::move(config.option<ConfigOptionStrings>("extruder_ams_count", true)->values);
    config.erase("extruder_ams_count");
    config.erase("enable_filament_dynamic_map");
    if (this->extruder_ams_counts.empty())
        this->extruder_ams_counts = get_extruder_ams_count(extruder_ams_count);


    // 1) Create a name from the file name.
    // Keep the suffix (.ini, .gcode, .amf, .3mf etc) to differentiate it from the normal profiles.
    std::string name = is_external ? boost::filesystem::path(name_or_path).filename().string() : name_or_path;

    // 2) If the loading succeeded, split and load the config into print / filament / printer settings.
    // First load the print and printer presets.

	auto load_preset =
		[&config, &inherits, &inherits_values,
         &compatible_printers_condition, &compatible_printers_condition_values,
         &compatible_prints_condition, &compatible_prints_condition_values,
         is_external, &name, &name_or_path, file_version, selected]
		(PresetCollection &presets, size_t idx, const std::string &key, const std::set<std::string> &different_keys, std::string filament_id) {
		// Split the "compatible_printers_condition" and "inherits" values one by one from a single vector to the print & printer profiles.
		inherits = inherits_values[idx];
		compatible_printers_condition = compatible_printers_condition_values[idx];
        if (idx > 0 && idx - 1 < compatible_prints_condition_values.size())
            compatible_prints_condition = compatible_prints_condition_values[idx - 1];
        //BBS: add config related logs
        BOOST_LOG_TRIVIAL(debug) << boost::format("name %1%, is_external %2%, inherits %3%")%name %is_external %inherits;
		if (is_external)
			presets.load_external_preset(name_or_path, name, config.opt_string(key, true), config, different_keys, PresetCollection::LoadAndSelect::Always, file_version, filament_id);
		else
            presets.load_preset(presets.path_from_name(name, inherits.empty()), name, config, selected, file_version).save(nullptr);
	};

    switch (Preset::printer_technology(config)) {
    case ptFFF:
    {
        //BBS: add different settings logic
        BOOST_LOG_TRIVIAL(debug) << boost::format("load print preset from print_settings_id");
        std::vector<std::string> print_different_keys_vector;
        std::string print_different_settings = different_values[0];
        Slic3r::unescape_strings_cstyle(print_different_settings, print_different_keys_vector);
        std::set<std::string> print_different_keys_set(print_different_keys_vector.begin(), print_different_keys_vector.end());
        //if (!has_different_settings_to_system) {
        //    print_different_keys_set.clear();
        //}
        //else
            print_different_keys_set.insert(ignore_settings_list.begin(), ignore_settings_list.end());
        if (!print_compatible_printers.empty()) {
            ConfigOptionStrings* compatible_printers = config.option<ConfigOptionStrings>("compatible_printers", true);
            compatible_printers->values = print_compatible_printers;
        }

        load_preset(this->prints, 0, "print_settings_id", print_different_keys_set, std::string());

        //clear compatible printers
        clear_compatible_printers(config);

        std::vector<std::string> printer_different_keys_vector;
        std::string printer_different_settings = different_values[num_filaments + 1];
        Slic3r::unescape_strings_cstyle(printer_different_settings, printer_different_keys_vector);
        std::set<std::string> printer_different_keys_set(printer_different_keys_vector.begin(), printer_different_keys_vector.end());
        //if (!has_different_settings_to_system) {
        //    printer_different_keys_set.clear();
        //}
        //else
            printer_different_keys_set.insert(ignore_settings_list.begin(), ignore_settings_list.end());
        //BBS: add config related logs
        BOOST_LOG_TRIVIAL(debug) << boost::format("load printer preset from printer_settings_id");
        load_preset(this->printers, num_filaments + 1, "printer_settings_id", printer_different_keys_set, std::string());

        // 3) Now load the filaments. If there are multiple filament presets, split them and load them.
        auto old_filament_profile_names = config.option<ConfigOptionStrings>("filament_settings_id", true);
        old_filament_profile_names->values.resize(num_filaments, std::string());

        auto old_machine_profile_name = config.option<ConfigOptionString>("printer_settings_id", true);
        auto machine_inherit          = config.option<ConfigOptionString>("inherits", true);

        if (num_filaments <= 1) {
            // Split the "compatible_printers_condition" and "inherits" from the cummulative vectors to separate filament presets.
            inherits                      = inherits_values[1];
            compatible_printers_condition = compatible_printers_condition_values[1];
			compatible_prints_condition   = compatible_prints_condition_values.front();
			Preset                *loaded = nullptr;

            //BBS: add different settings logic
            std::vector<std::string> filament_different_keys_vector;
            std::string filament_different_settings = different_values[1];
            Slic3r::unescape_strings_cstyle(filament_different_settings, filament_different_keys_vector);
            std::set<std::string> filament_different_keys_set(filament_different_keys_vector.begin(), filament_different_keys_vector.end());
            //if (!has_different_settings_to_system) {
            //    filament_different_keys_set.clear();
            //}
            //else
                filament_different_keys_set.insert(ignore_settings_list.begin(), ignore_settings_list.end());

            std::string filament_id = filament_ids[0];
            //BBS: add config related logs
            BOOST_LOG_TRIVIAL(debug) << boost::format("load single filament preset from filament_settings_id");
            if (is_external) {
                auto machine_name = machine_inherit->empty() ? old_machine_profile_name->value : machine_inherit->value;
                if (inherits.empty())
                    convert_filament_preset_name(machine_name, old_filament_profile_names->values.front());
                else
                    convert_filament_preset_name(machine_name, inherits);
                loaded = this->filaments.load_external_preset(name_or_path, name, old_filament_profile_names->values.front(), config, filament_different_keys_set, PresetCollection::LoadAndSelect::Always, file_version, filament_id).first;
            }
            else {
                // called from Config Wizard.
				loaded= &this->filaments.load_preset(this->filaments.path_from_name(name, inherits.empty()), name, config, true, file_version);
				loaded->save(nullptr);
			}
            this->filament_presets.clear();
			this->filament_presets.emplace_back(loaded->name);
        } else {
            assert(is_external);
            // Split the filament presets, load each of them separately.
            // Expand per-filament overrides onto variant slots before they are split,
            // otherwise a length-1 edit is read as the previous filament's High Flow slot.
            align_filament_variant_overrides(config);

            std::vector<DynamicPrintConfig> configs(num_filaments, this->filaments.default_preset().config);
            // loop through options and scatter them into configs.
            for (const t_config_option_key &key : this->filaments.default_preset().config.keys()) {
                ConfigOption *other_opt = config.option(key);
                if (other_opt == nullptr)
                    continue;
                if (other_opt->is_scalar()) {
                    for (size_t i = 0; i < configs.size(); ++ i)
                        configs[i].option(key, false)->set(other_opt);
                }
                else if (key != "compatible_printers" && key != "compatible_prints") {
                    for (size_t i = 0; i < configs.size(); ++i) {
                        if (process_multi_extruder && (filament_options_with_variant.find(key) != filament_options_with_variant.end())) {
                            ConfigOptionVectorBase* other_opt_vec = static_cast<ConfigOptionVectorBase*>(other_opt);
                            if (other_opt_vec->size() != extruder_variant_count) {
                                other_opt_vec->resize(extruder_variant_count);
                            }
                            size_t next_index = (i < (configs.size() - 1)) ? filament_variant_index[i + 1] : extruder_variant_count;
                            static_cast<ConfigOptionVectorBase*>(configs[i].option(key, false))->set(other_opt, filament_variant_index[i], next_index - filament_variant_index[i]);
                        }
                        else
                            static_cast<ConfigOptionVectorBase*>(configs[i].option(key, false))->set_at(other_opt, 0, i);
                    }
                }
            }
            // Load the configs into this->filaments and make them active.
            this->filament_presets = std::vector<std::string>(configs.size());
            // To avoid incorrect selection of the first filament preset (means a value of Preset->m_idx_selected)
            // in a case when next added preset take a place of previosly selected preset,
            // we should add presets from last to first
            bool any_modified = false;
            //BBS: add config related logs
            BOOST_LOG_TRIVIAL(debug) << boost::format("load multiple filament preset from filament_settings_id");
            for (int i = (int)configs.size()-1; i >= 0; i--) {
                DynamicPrintConfig &cfg = configs[i];
                // Split the "compatible_printers_condition" and "inherits" from the cummulative vectors to separate filament presets.
                cfg.opt_string("compatible_printers_condition", true) = compatible_printers_condition_values[i + 1];
                cfg.opt_string("compatible_prints_condition",   true) = compatible_prints_condition_values[i];
                cfg.opt_string("inherits", true)                      = inherits_values[i + 1];

                //BBS: add different settings logic
                std::vector<std::string> filament_different_keys_vector;
                std::string filament_different_settings = different_values[i+1];
                Slic3r::unescape_strings_cstyle(filament_different_settings, filament_different_keys_vector);
                std::set<std::string> filament_different_keys_set(filament_different_keys_vector.begin(), filament_different_keys_vector.end());
                //if (!has_different_settings_to_system) {
                //    filament_different_keys_set.clear();
                //}
                //else
                    filament_different_keys_set.insert(ignore_settings_list.begin(), ignore_settings_list.end());

                std::string filament_id = filament_ids[i];

                // Load all filament presets, but only select the first one in the preset dialog.
                std::string& filament_inherit = cfg.opt_string("inherits", true);
                auto machine_name = machine_inherit->empty() ? old_machine_profile_name->value : machine_inherit->value;
                if (filament_inherit.empty() && (i < int(old_filament_profile_names->values.size())))
                    convert_filament_preset_name(machine_name, old_filament_profile_names->values[i]);
                else
                    convert_filament_preset_name(machine_name, filament_inherit);
                auto [loaded, modified] = this->filaments.load_external_preset(name_or_path, name,
                    (i < int(old_filament_profile_names->values.size())) ? old_filament_profile_names->values[i] : "",
                    std::move(cfg),
                    filament_different_keys_set,
                    i == 0 ?
                        PresetCollection::LoadAndSelect::Always :
                    any_modified ?
                        PresetCollection::LoadAndSelect::Never :
                        PresetCollection::LoadAndSelect::OnlyIfModified,
                    file_version,
                    filament_id);
                any_modified |= modified;
                this->filament_presets[i] = loaded->name;
            }
        }

        // Omitted 3mf keys keep the previous value. Reset them from print_config_def.
        // Wizard loads pass is_external false and keep their partial merge.
        if (is_external) {
            for (const std::string &key : s_project_options) {
                if (config.has(key))
                    continue;
                const ConfigOptionDef *optdef = print_config_def.get(key);
                if (optdef == nullptr)
                    continue;
                this->project_config.erase(key);
                this->project_config.set_key_value(key, optdef->create_default_option());
            }
        }

        // 4) Load the project config values (the per extruder wipe matrix etc).
        this->project_config.apply_only(config, s_project_options);
        // The copy above replaced the nozzle keys; write them again for the printer of the file.
        // A project load then replaces them with the nozzles of the file in open_project().
        nozzle_inventory.select_printer(printers);
        this->project_config.option<ConfigOptionStrings>("filament_diameter_map")
            ->values.resize(this->filament_presets.size());

        break;
    }
    case ptSLA:
    {
        /*std::set<std::string> different_keys_set;
        load_preset(this->sla_prints, 0, "sla_print_settings_id", different_keys_set);
        load_preset(this->sla_materials, 1, "sla_material_settings_id", different_keys_set);
        load_preset(this->printers, 2, "printer_settings_id", different_keys_set);*/
        break;
    }
    default:
        break;
    }

	this->update_compatible(PresetSelectCompatibleType::Never);
    this->update_multi_material_filament_presets();

    //BBS
    //const std::string &physical_printer = config.option<ConfigOptionString>("physical_printer_settings_id", true)->value;
    const std::string physical_printer;
    if (this->printers.get_edited_preset().is_external || physical_printer.empty()) {
        this->physical_printers.unselect_printer();
    } else {
        // Activate the physical printer profile if possible.
        PhysicalPrinter *pp = this->physical_printers.find_printer(physical_printer, true);
        if (pp != nullptr && std::find(pp->preset_names.begin(), pp->preset_names.end(), this->printers.get_edited_preset().name) != pp->preset_names.end())
            this->physical_printers.select_printer(pp->name, this->printers.get_edited_preset().name);
        else
            this->physical_printers.unselect_printer();
    }
    //BBS: add config related logs
    BOOST_LOG_TRIVIAL(debug) << boost::format("finished");
}

// Load the active configuration of a config bundle from a boost property_tree. This is a private method called from load_config_file.
/*ConfigSubstitutions PresetBundle::load_config_file_config_bundle(
    const std::string &path, const boost::property_tree::ptree &tree, ForwardCompatibilitySubstitutionRule compatibility_rule)
{
    // 1) Load the config bundle into a temp data.
    PresetBundle tmp_bundle;
    // Load the config bundle, but don't save the loaded presets to user profile directory, as only the presets marked as active in the loaded preset bundle
    // will be loaded into the master PresetBundle and activated.
    auto [presets_substitutions, presets_imported] = tmp_bundle.load_configbundle(path, {}, compatibility_rule);
    UNUSED(presets_imported);

    std::string bundle_name = std::string(" - ") + boost::filesystem::path(path).filename().string();

    //BBS: add config related logs
    BOOST_LOG_TRIVIAL(debug) << boost::format("enter, bundle_name %1%")%bundle_name;
    // 2) Extract active configs from the config bundle, copy them and activate them in this bundle.
    ConfigSubstitutions config_substitutions;
    auto load_one = [&path, &bundle_name, &presets_substitutions = presets_substitutions, &config_substitutions](
            PresetCollection &collection_dst, PresetCollection &collection_src, const std::string &preset_name_src, bool activate) -> std::string {
        // If there are substitutions reported for this preset, move them to config_substitutions.
        if (auto it = std::find_if(presets_substitutions.begin(), presets_substitutions.end(), [&preset_name_src](const PresetConfigSubstitutions& subs){ return subs.preset_name == preset_name_src; });
            it != presets_substitutions.end() && ! it->substitutions.empty())
            append(config_substitutions, std::move(it->substitutions));
        Preset *preset_src = collection_src.find_preset(preset_name_src, false);
        Preset *preset_dst = collection_dst.find_preset(preset_name_src, false);
        assert(preset_src != nullptr);
        std::string preset_name_dst;
        if (preset_dst != nullptr && preset_dst->is_default) {
            // No need to copy a default preset, it always exists in collection_dst.
            if (activate)
                collection_dst.select_preset(0);
            return preset_name_src;
        } else if (preset_dst != nullptr && preset_src->config == preset_dst->config) {
            // Don't save as the config exists in the current bundle and its content is the same.
            return preset_name_src;
        } else {
            // Generate a new unique name.
            preset_name_dst = preset_name_src + bundle_name;
            Preset *preset_dup = nullptr;
            for (size_t i = 1; (preset_dup = collection_dst.find_preset(preset_name_dst, false)) != nullptr; ++ i) {
                if (preset_src->config == preset_dup->config)
                    // The preset has been already copied into collection_dst.
                    return preset_name_dst;
                // Try to generate another name.
                char buf[64];
                sprintf(buf, " (%d)", (int)i);
                preset_name_dst = preset_name_src + buf + bundle_name;
            }
        }
        assert(! preset_name_dst.empty());
        // Save preset_src->config into collection_dst under preset_name_dst.
        // The "compatible_printers" field should not have been exported into a config.ini or a G-code anyway,
        // but some of the alpha versions of Slic3r did.
        ConfigOption *opt_compatible = preset_src->config.optptr("compatible_printers");
        if (opt_compatible != nullptr) {
            assert(opt_compatible->type() == coStrings);
            if (opt_compatible->type() == coStrings)
                static_cast<ConfigOptionStrings*>(opt_compatible)->values.clear();
        }
        (collection_dst.type() == Preset::TYPE_FILAMENT ?
            collection_dst.load_preset(path, preset_name_dst, preset_src->config, activate) :
            // Only move the source config for non filament profiles, as single filament profile may be referenced multiple times.
            collection_dst.load_preset(path, preset_name_dst, std::move(preset_src->config), activate))
            .is_external = true;
        return preset_name_dst;
    };
    load_one(this->prints,        tmp_bundle.prints,        tmp_bundle.prints       .get_selected_preset_name(), true);
    load_one(this->sla_prints,    tmp_bundle.sla_prints,    tmp_bundle.sla_prints   .get_selected_preset_name(), true);
    load_one(this->filaments,     tmp_bundle.filaments,     tmp_bundle.filaments    .get_selected_preset_name(), true);
    load_one(this->sla_materials, tmp_bundle.sla_materials, tmp_bundle.sla_materials.get_selected_preset_name(), true);
    load_one(this->printers,      tmp_bundle.printers,      tmp_bundle.printers     .get_selected_preset_name(), true);
    this->update_multi_material_filament_presets();
    for (size_t i = 1; i < std::min(tmp_bundle.filament_presets.size(), this->filament_presets.size()); ++ i)
        this->filament_presets[i] = load_one(this->filaments, tmp_bundle.filaments, tmp_bundle.filament_presets[i], false);

    this->update_compatible(PresetSelectCompatibleType::Never);

    sort_remove_duplicates(config_substitutions);

    //BBS: add config related logs
    BOOST_LOG_TRIVIAL(debug) << boost::format("finished, got %1% substitutions")%config_substitutions.size();
    return config_substitutions;
}*/

// Process the Config Bundle loaded as a Boost property tree.
// For each print, filament and printer preset (group defined by group_name), apply the inherited presets.
// The presets starting with '*' are considered non-terminal and they are
// removed through the flattening process by this function.
// This function will never fail, but it will produce error messages through boost::log.
// system_profiles will not be flattened, and they will be kept inside the "inherits" field
/*static void flatten_configbundle_hierarchy(boost::property_tree::ptree &tree, const std::string &group_name, const std::vector<std::string> &system_profiles)
{
    namespace pt = boost::property_tree;

    // 1) For the group given by group_name, initialize the presets.
    struct Prst {
        Prst(const std::string &name, pt::ptree *node) : name(name), node(node) {}
        // Name of this preset. If the name starts with '*', it is an intermediate preset,
        // which will not make it into the result.
        const std::string           name;
        // Link to the source boost property tree node, owned by tree.
        pt::ptree                  *node;
        // Link to the presets, from which this preset inherits.
        std::vector<Prst*>          inherits;
        // Link to the presets, for which this preset is a direct parent.
        std::vector<Prst*>          parent_of;
        // When running the Kahn's Topological sorting algorithm, this counter is decreased from inherits.size() to zero.
        // A cycle is indicated, if the number does not drop to zero after the Kahn's algorithm finishes.
        size_t                      num_incoming_edges_left = 0;
        // Sorting by the name, to be used when inserted into std::set.
        bool operator==(const Prst &rhs) const { return this->name == rhs.name; }
        bool operator< (const Prst &rhs) const { return this->name < rhs.name; }
    };
    // Find the presets, store them into a std::map, addressed by their names.
    std::set<Prst> presets;
    std::string group_name_preset = group_name + ":";
    for (auto &section : tree)
        if (boost::starts_with(section.first, group_name_preset) && section.first.size() > group_name_preset.size())
            presets.emplace(section.first.substr(group_name_preset.size()), &section.second);
    // Fill in the "inherits" and "parent_of" members, report invalid inheritance fields.
    for (const Prst &prst : presets) {
        // Parse the list of comma separated values, possibly enclosed in quotes.
        std::vector<std::string> inherits_names;
        std::vector<std::string> inherits_system;
        if (Slic3r::unescape_strings_cstyle(prst.node->get<std::string>("inherits", ""), inherits_names)) {
            // Resolve the inheritance by name.
            std::vector<Prst*> &inherits_nodes = const_cast<Prst&>(prst).inherits;
            for (const std::string &node_name : inherits_names) {
                auto it_system = std::lower_bound(system_profiles.begin(), system_profiles.end(), node_name);
                if (it_system != system_profiles.end() && *it_system == node_name) {
                    // Loading a user config budnle, this preset is derived from a system profile.
                    inherits_system.emplace_back(node_name);
                } else {
                    auto it = presets.find(Prst(node_name, nullptr));
                    if (it == presets.end())
                        BOOST_LOG_TRIVIAL(error) << "flatten_configbundle_hierarchy: The preset " << prst.name << " inherits an unknown preset \"" << node_name << "\"";
                    else {
                        inherits_nodes.emplace_back(const_cast<Prst*>(&(*it)));
                        inherits_nodes.back()->parent_of.emplace_back(const_cast<Prst*>(&prst));
                    }
                }
            }
        } else {
            BOOST_LOG_TRIVIAL(error) << "flatten_configbundle_hierarchy: The preset " << prst.name << " has an invalid \"inherits\" field";
        }
        // Remove the "inherits" key, it has no meaning outside of the config bundle.
        const_cast<pt::ptree*>(prst.node)->erase("inherits");
        if (! inherits_system.empty()) {
            // Loaded a user config bundle, where a profile inherits a system profile.
			// User profile should be derived from a single system profile only.
			assert(inherits_system.size() == 1);
			if (inherits_system.size() > 1)
				BOOST_LOG_TRIVIAL(error) << "flatten_configbundle_hierarchy: The preset " << prst.name << " inherits from more than single system preset";
			prst.node->put("inherits", Slic3r::escape_string_cstyle(inherits_system.front()));
        }
    }

    // 2) Create a linear ordering for the directed acyclic graph of preset inheritance.
    // https://en.wikipedia.org/wiki/Topological_sorting
    // Kahn's algorithm.
    std::vector<Prst*> sorted;
    {
        // Initialize S with the set of all nodes with no incoming edge.
        std::deque<Prst*> S;
        for (const Prst &prst : presets)
            if (prst.inherits.empty())
                S.emplace_back(const_cast<Prst*>(&prst));
            else
                const_cast<Prst*>(&prst)->num_incoming_edges_left = prst.inherits.size();
        while (! S.empty()) {
            Prst *n = S.front();
            S.pop_front();
            sorted.emplace_back(n);
            for (Prst *m : n->parent_of) {
                assert(m->num_incoming_edges_left > 0);
                if (-- m->num_incoming_edges_left == 0) {
                    // We have visited all parents of m.
                    S.emplace_back(m);
                }
            }
        }
        if (sorted.size() < presets.size()) {
            for (const Prst &prst : presets)
                if (prst.num_incoming_edges_left)
                    BOOST_LOG_TRIVIAL(error) << "flatten_configbundle_hierarchy: The preset " << prst.name << " has cyclic dependencies";
        }
    }

    // Apply the dependencies in their topological ordering.
    for (Prst *prst : sorted) {
        // Merge the preset nodes in their order of application.
        // Iterate in a reverse order, so the last change will be placed first in merged.
        for (auto it_inherits = prst->inherits.rbegin(); it_inherits != prst->inherits.rend(); ++ it_inherits)
            for (auto it = (*it_inherits)->node->begin(); it != (*it_inherits)->node->end(); ++ it)
				if (it->first == "renamed_from") {
            		// Don't inherit "renamed_from" flag, it does not make sense. The "renamed_from" flag only makes sense for a concrete preset.
            		if (boost::starts_with((*it_inherits)->name, "*"))
			            BOOST_LOG_TRIVIAL(error) << boost::format("Nonpublic intermediate preset %1% contains a \"renamed_from\" field, which is ignored") % (*it_inherits)->name;
				} else if (prst->node->find(it->first) == prst->node->not_found())
                    prst->node->add_child(it->first, it->second);
    }

    // Remove the "internal" presets from the ptree. These presets are marked with '*'.
    group_name_preset += '*';
    for (auto it_section = tree.begin(); it_section != tree.end(); ) {
        if (boost::starts_with(it_section->first, group_name_preset) && it_section->first.size() > group_name_preset.size())
            // Remove the "internal" preset from the ptree.
            it_section = tree.erase(it_section);
        else
            // Keep the preset.
            ++ it_section;
    }
}*/

// preset_bundle is set when loading user config bundles, which must not overwrite the system profiles.
/*static void flatten_configbundle_hierarchy(boost::property_tree::ptree &tree, const PresetBundle *preset_bundle)
{
    flatten_configbundle_hierarchy(tree, "print",           preset_bundle ? preset_bundle->prints.system_preset_names()        : std::vector<std::string>());
    flatten_configbundle_hierarchy(tree, "filament",        preset_bundle ? preset_bundle->filaments.system_preset_names()     : std::vector<std::string>());
    flatten_configbundle_hierarchy(tree, "sla_print",       preset_bundle ? preset_bundle->sla_prints.system_preset_names()    : std::vector<std::string>());
    flatten_configbundle_hierarchy(tree, "sla_material",    preset_bundle ? preset_bundle->sla_materials.system_preset_names() : std::vector<std::string>());
    flatten_configbundle_hierarchy(tree, "printer",         preset_bundle ? preset_bundle->printers.system_preset_names()      : std::vector<std::string>());
}*/

// Load a config bundle file, into presets and store the loaded presets into separate files
// of the local configuration directory.
/*std::pair<PresetsConfigSubstitutions, size_t> PresetBundle::load_configbundle(
    const std::string &path, LoadConfigBundleAttributes flags, ForwardCompatibilitySubstitutionRule compatibility_rule)
{
    // Enable substitutions for user config bundle, throw an exception when loading a system profile.
    ConfigSubstitutionContext  substitution_context { compatibility_rule };
    PresetsConfigSubstitutions substitutions;

    //BBS: add config related logs
    BOOST_LOG_TRIVIAL(debug) << boost::format("enter, path %1%, compatibility_rule %2%")%path.c_str()%compatibility_rule;
    if (flags.has(LoadConfigBundleAttribute::ResetUserProfile) || flags.has(LoadConfigBundleAttribute::LoadSystem))
        // Reset this bundle, delete user profile files if SaveImported.
        this->reset(flags.has(LoadConfigBundleAttribute::SaveImported));

    // 1) Read the complete config file into a boost::property_tree.
    namespace pt = boost::property_tree;
    pt::ptree tree;
    boost::nowide::ifstream ifs(path);
    try {
        pt::read_ini(ifs, tree);
    } catch (const boost::property_tree::ini_parser::ini_parser_error &err) {
        throw Slic3r::RuntimeError(format("Failed loading config bundle \"%1%\"\nError: \"%2%\" at line %3%", path, err.message(), err.line()).c_str());
    }

    const VendorProfile *vendor_profile = nullptr;
    if (flags.has(LoadConfigBundleAttribute::LoadSystem) || flags.has(LoadConfigBundleAttribute::LoadVendorOnly)) {
        auto vp = VendorProfile::from_ini(tree, path);
        if (vp.models.size() == 0) {
            BOOST_LOG_TRIVIAL(error) << boost::format("Vendor bundle: `%1%`: No printer model defined.") % path;
            return std::make_pair(PresetsConfigSubstitutions{}, 0);
        } else if (vp.num_variants() == 0) {
            BOOST_LOG_TRIVIAL(error) << boost::format("Vendor bundle: `%1%`: No printer variant defined") % path;
            return std::make_pair(PresetsConfigSubstitutions{}, 0);
        }
        vendor_profile = &this->vendors.insert({vp.id, vp}).first->second;
    }

    //BBS: add config related logs
    if (vendor_profile)
        BOOST_LOG_TRIVIAL(debug) << boost::format("loaded vendor profile, name %1%, id %2%, version %3%")%vendor_profile->name%vendor_profile->id%vendor_profile->config_version.to_string();

    if (flags.has(LoadConfigBundleAttribute::LoadVendorOnly))
        return std::make_pair(PresetsConfigSubstitutions{}, 0);

    // 1.5) Flatten the config bundle by applying the inheritance rules. Internal profiles (with names starting with '*') are removed.
    // If loading a user config bundle, do not flatten with the system profiles, but keep the "inherits" flag intact.
    flatten_configbundle_hierarchy(tree, flags.has(LoadConfigBundleAttribute::LoadSystem) ? nullptr : this);

    // 2) Parse the property_tree, extract the active preset names and the profiles, save them into local config files.
    // Parse the obsolete preset names, to be deleted when upgrading from the old configuration structure.
    std::vector<std::string> loaded_prints;
    std::vector<std::string> loaded_filaments;
    std::vector<std::string> loaded_sla_prints;
    std::vector<std::string> loaded_sla_materials;
    std::vector<std::string> loaded_printers;
    std::vector<std::string> loaded_physical_printers;
    std::string              active_print;
    std::vector<std::string> active_filaments;
    std::string              active_sla_print;
    std::string              active_sla_material;
    std::string              active_printer;
    std::string              active_physical_printer;
    size_t                   presets_loaded = 0;
    size_t                   ph_printers_loaded = 0;

    for (const auto &section : tree) {
        PresetCollection         *presets = nullptr;
        std::string               preset_name;
        PhysicalPrinterCollection *ph_printers = nullptr;
        std::string               ph_printer_name;
        if (boost::starts_with(section.first, "print:")) {
            presets = &this->prints;
            preset_name = section.first.substr(6);
        } else if (boost::starts_with(section.first, "filament:")) {
            presets = &this->filaments;
            preset_name = section.first.substr(9);
        } else if (boost::starts_with(section.first, "sla_print:")) {
            presets = &this->sla_prints;
            preset_name = section.first.substr(10);
        } else if (boost::starts_with(section.first, "sla_material:")) {
            presets = &this->sla_materials;
            preset_name = section.first.substr(13);
        } else if (boost::starts_with(section.first, "printer:")) {
            presets = &this->printers;
            preset_name = section.first.substr(8);
        } else if (boost::starts_with(section.first, "physical_printer:")) {
            ph_printers = &this->physical_printers;
            ph_printer_name = section.first.substr(17);
        } else if (section.first == "presets") {
            // Load the names of the active presets.
            for (auto &kvp : section.second) {
                if (kvp.first == "print") {
                    active_print = kvp.second.data();
                } else if (boost::starts_with(kvp.first, "filament")) {
                    int idx = 0;
                    if (kvp.first == "filament" || sscanf(kvp.first.c_str(), "filament_%d", &idx) == 1) {
                        if (int(active_filaments.size()) <= idx)
                            active_filaments.resize(idx + 1, std::string());
                        active_filaments[idx] = kvp.second.data();
                    }
                } else if (kvp.first == "sla_print") {
                    active_sla_print = kvp.second.data();
                } else if (kvp.first == "sla_material") {
                    active_sla_material = kvp.second.data();
                } else if (kvp.first == "printer") {
                    active_printer = kvp.second.data();
                } else if (kvp.first == "physical_printer") {
                    active_physical_printer = kvp.second.data();
                }
            }
        } else if (section.first == "obsolete_presets") {
            // Parse the names of obsolete presets. These presets will be deleted from user's
            // profile directory on installation of this vendor preset.
            for (auto &kvp : section.second) {
                std::vector<std::string> *dst = nullptr;
                if (kvp.first == "print")
                    dst = &this->obsolete_presets.prints;
                else if (kvp.first == "filament")
                    dst = &this->obsolete_presets.filaments;
                else if (kvp.first == "sla_print")
                    dst = &this->obsolete_presets.sla_prints;
                else if (kvp.first == "sla_material")
                    dst = &this->obsolete_presets.sla_materials;
                else if (kvp.first == "printer")
                    dst = &this->obsolete_presets.printers;
                if (dst)
                    unescape_strings_cstyle(kvp.second.data(), *dst);
            }
        } else if (section.first == "settings") {
#ifdef SUPPORT_AUTO_CENTER
            // Load the settings.
            for (auto &kvp : section.second) {
                if (kvp.first == "autocenter") {
                }
            }
#endif
        } else
            // Ignore an unknown section.
            continue;
        if (presets != nullptr) {
            // Load the print, filament or printer preset.
            const DynamicPrintConfig *default_config = nullptr;
            DynamicPrintConfig        config;
            std::string 			  alias_name;
            std::vector<std::string>  renamed_from;
            try {
                auto parse_config_section = [&section, &alias_name, &renamed_from, &substitution_context, &path](DynamicPrintConfig &config) {
                    substitution_context.substitutions.clear();
                    for (auto &kvp : section.second) {
                    	if (kvp.first == "alias")
                    		alias_name = kvp.second.data();
                    	else if (kvp.first == "renamed_from") {
                    		if (! unescape_strings_cstyle(kvp.second.data(), renamed_from)) {
    			                BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The preset \"" <<
    			                    section.first << "\" contains invalid \"renamed_from\" key, which is being ignored.";
                       		}
                    	}
                        // Throws on parsing error. For system presets, no substituion is being done, but an exception is thrown.
                        config.set_deserialize(kvp.first, kvp.second.data(), substitution_context);
                    }
                };
                if (presets == &this->printers) {
                    // Select the default config based on the printer_technology field extracted from kvp.
                    DynamicPrintConfig config_src;
                    parse_config_section(config_src);
                    default_config = &presets->default_preset_for(config_src).config;
                    config = *default_config;
                    config.apply(config_src);
                } else {
                    default_config = &presets->default_preset().config;
                    config = *default_config;
                    parse_config_section(config);
                }
            } catch (const ConfigurationError &e) {
                throw ConfigurationError(format("Invalid configuration bundle \"%1%\", section [%2%]: ", path, section.first) + e.what());
            }
            Preset::normalize(config);
            // Report configuration fields, which are misplaced into a wrong group.
            std::string incorrect_keys = Preset::remove_invalid_keys(config, *default_config);
            if (! incorrect_keys.empty())
                BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The printer preset \"" <<
                    section.first << "\" contains the following incorrect keys: " << incorrect_keys << ", which were removed";
            if (flags.has(LoadConfigBundleAttribute::LoadSystem) && presets == &printers) {
                // Filter out printer presets, which are not mentioned in the vendor profile.
                // These presets are considered not installed.
                auto printer_model   = config.opt_string("printer_model");
                if (printer_model.empty()) {
                    BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The printer preset \"" <<
                        section.first << "\" defines no printer model, it will be ignored.";
                    continue;
                }
                auto printer_variant = config.opt_string("printer_variant");
                if (printer_variant.empty()) {
                    BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The printer preset \"" <<
                        section.first << "\" defines no printer variant, it will be ignored.";
                    continue;
                }
                auto it_model = std::find_if(vendor_profile->models.cbegin(), vendor_profile->models.cend(),
                    [&](const VendorProfile::PrinterModel &m) { return m.id == printer_model; }
                );
                if (it_model == vendor_profile->models.end()) {
                    BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The printer preset \"" <<
                        section.first << "\" defines invalid printer model \"" << printer_model << "\", it will be ignored.";
                    continue;
                }
                auto it_variant = it_model->variant(printer_variant);
                if (it_variant == nullptr) {
                    BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The printer preset \"" <<
                        section.first << "\" defines invalid printer variant \"" << printer_variant << "\", it will be ignored.";
                    continue;
                }
                const Preset *preset_existing = presets->find_preset(section.first, false);
                if (preset_existing != nullptr) {
                    BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The printer preset \"" <<
                        section.first << "\" has already been loaded from another Confing Bundle.";
                    continue;
                }
            } else if (! flags.has(LoadConfigBundleAttribute::LoadSystem)) {
                // This is a user config bundle.
                const Preset *existing = presets->find_preset(preset_name, false);
                if (existing != nullptr) {
                    if (existing->is_system) {
    					assert(existing->vendor != nullptr);
                        BOOST_LOG_TRIVIAL(error) << "Error in a user provided Config Bundle \"" << path << "\": The " << presets->name() << " preset \"" <<
    						existing->name << "\" is a system preset of vendor " << existing->vendor->name << " and it will be ignored.";
                        continue;
                    } else {
                        assert(existing->vendor == nullptr);
                        BOOST_LOG_TRIVIAL(trace) << "A " << presets->name() << " preset \"" << existing->name << "\" was overwritten with a preset from user Config Bundle \"" << path << "\"";
                    }
                } else {
					BOOST_LOG_TRIVIAL(trace) << "A new " << presets->name() << " preset \"" << preset_name << "\" was imported from user Config Bundle \"" << path << "\"";
                }
            }
            // Decide a full path to this .ini file.
            auto file_name = boost::algorithm::iends_with(preset_name, ".json") ? preset_name : preset_name + ".json";
            //BBS: change directoties by design
            auto file_path = (boost::filesystem::path(data_dir())  /PRESET_SYSTEM_DIR/ presets->section_name() / file_name).make_preferred();
            // Load the preset into the list of presets, save it to disk.
            Preset &loaded = presets->load_preset(file_path.string(), preset_name, std::move(config), false);
            if (flags.has(LoadConfigBundleAttribute::SaveImported))
                loaded.save(nullptr);
            if (flags.has(LoadConfigBundleAttribute::LoadSystem)) {
                loaded.is_system = true;
                loaded.vendor = vendor_profile;
            }

            // Derive the profile logical name aka alias from the preset name if the alias was not stated explicitely.
            if (alias_name.empty()) {
                size_t end_pos = preset_name.find_first_of("@");
	            if (end_pos != std::string::npos) {
	                alias_name = preset_name.substr(0, end_pos);
	                if (renamed_from.empty())
	                	// Add the preset name with the '@' character removed into the "renamed_from" list.
	                	renamed_from.emplace_back(alias_name + preset_name.substr(end_pos + 1));
                    boost::trim_right(alias_name);
	            }
	        }
	        if (alias_name.empty())
	        	loaded.alias = preset_name;
	        else
	         	loaded.alias = std::move(alias_name);
	        loaded.renamed_from = std::move(renamed_from);
            if (! substitution_context.empty())
                substitutions.push_back({
                    preset_name, presets->type(), PresetConfigSubstitutions::Source::ConfigBundle,
                    std::string(), std::move(substitution_context.substitutions) });
            ++ presets_loaded;
            //BBS: add config related logs
            BOOST_LOG_TRIVIAL(debug) << boost::format("got preset %1%, from %2%")%loaded.name %path;
        }

        if (ph_printers != nullptr) {
            // Load the physical printer
            const DynamicPrintConfig& default_config = ph_printers->default_config();
            DynamicPrintConfig        config = default_config;

            substitution_context.substitutions.clear();
            try {
                for (auto& kvp : section.second)
                    config.set_deserialize(kvp.first, kvp.second.data(), substitution_context);
            } catch (const ConfigurationError &e) {
                throw ConfigurationError(format("Invalid configuration bundle \"%1%\", section [%2%]: ", path, section.first) + e.what());
            }

            // Report configuration fields, which are misplaced into a wrong group.
            std::string incorrect_keys = Preset::remove_invalid_keys(config, default_config);
            if (!incorrect_keys.empty())
                BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The physical printer \"" <<
                section.first << "\" contains the following incorrect keys: " << incorrect_keys << ", which were removed";

            const PhysicalPrinter* ph_printer_existing = ph_printers->find_printer(ph_printer_name, false);
            if (ph_printer_existing != nullptr) {
                BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The physical printer \"" <<
                    section.first << "\" has already been loaded from another Confing Bundle.";
                continue;
            }

            // Decide a full path to this .ini file.
            //BBS: change directoties by design
            auto file_name = boost::algorithm::iends_with(ph_printer_name, ".json") ? ph_printer_name : ph_printer_name + ".json";
            auto file_path = (boost::filesystem::path(data_dir())/PRESET_SYSTEM_DIR/"physical_printer" / file_name).make_preferred();
            // Load the preset into the list of presets, save it to disk.
            ph_printers->load_printer(file_path.string(), ph_printer_name, std::move(config), false, flags.has(LoadConfigBundleAttribute::SaveImported));
            if (! substitution_context.empty())
                substitutions.push_back({
                    ph_printer_name, Preset::TYPE_PHYSICAL_PRINTER, PresetConfigSubstitutions::Source::ConfigBundle,
                    std::string(), std::move(substitution_context.substitutions) });
            ++ ph_printers_loaded;
        }
    }

    // 3) Activate the presets and physical printer if any exists.
    if (! flags.has(LoadConfigBundleAttribute::LoadSystem)) {
        if (! active_print.empty())
            prints.select_preset_by_name(active_print, true);
        if (! active_sla_print.empty())
            sla_prints.select_preset_by_name(active_sla_print, true);
        if (! active_sla_material.empty())
            sla_materials.select_preset_by_name(active_sla_material, true);
        if (! active_printer.empty())
            printers.select_preset_by_name(active_printer, true);
        if (! active_physical_printer.empty())
            physical_printers.select_printer(active_physical_printer, active_printer);
        // Activate the first filament preset.
        if (! active_filaments.empty() && ! active_filaments.front().empty())
            filaments.select_preset_by_name(active_filaments.front(), true);
        this->update_multi_material_filament_presets();
        for (size_t i = 0; i < std::min(this->filament_presets.size(), active_filaments.size()); ++ i)
            this->filament_presets[i] = filaments.find_preset(active_filaments[i], true)->name;
        this->update_compatible(PresetSelectCompatibleType::Never);
    }

    //BBS: add config related logs
    BOOST_LOG_TRIVIAL(debug) << boost::format("finished, presets_loaded %1%, ph_printers_loaded %2%")%presets_loaded %ph_printers_loaded;
    return std::make_pair(std::move(substitutions), presets_loaded + ph_printers_loaded);
}*/

//BBS: Load a config bundle file from json
std::pair<PresetsConfigSubstitutions, size_t> PresetBundle::load_vendor_configs_from_json(
    const std::string &path, const std::string &vendor_name, LoadConfigBundleAttributes flags, ForwardCompatibilitySubstitutionRule compatibility_rule)
{
    // Enable substitutions for user config bundle, throw an exception when loading a system profile.
    ConfigSubstitutionContext  substitution_context { compatibility_rule };
    PresetsConfigSubstitutions substitutions;

    //BBS: add config related logs
    BOOST_LOG_TRIVIAL(info) << boost::format("enter, path %1%, compatibility_rule %2%") % PathSanitizer::sanitize(path) % compatibility_rule;
    if (flags.has(LoadConfigBundleAttribute::ResetUserProfile) || flags.has(LoadConfigBundleAttribute::LoadSystem))
        // Reset this bundle, delete user profile files if SaveImported.
        this->reset(flags.has(LoadConfigBundleAttribute::SaveImported));

    // 1) load the vroot json and construct the vendor profile
    VendorProfile vendor_profile(vendor_name);
    std::string root_file = path + "/" + vendor_name + ".json";
    std::vector<std::pair<std::string, std::string>> machine_model_subfiles;
    std::vector<std::pair<std::string, std::string>> process_subfiles;
    std::vector<std::pair<std::string, std::string>> filament_subfiles;
    std::vector<std::pair<std::string, std::string>> machine_subfiles;
    auto get_name_and_subpath = [](json::iterator& it, std::vector<std::pair<std::string, std::string>>& subfile_map) {
        if (it.value().is_array()) {
            for (auto iter1 = it.value().begin(); iter1 != it.value().end(); iter1++) {
                if (iter1.value().is_object()) {
                    std::string name, subpath;
                    for (auto iter2 = iter1.value().begin(); iter2 != iter1.value().end(); iter2++) {
                        if (iter2.value().is_string()) {
                            if (boost::iequals(iter2.key(), BBL_JSON_KEY_NAME)) {
                                name = iter2.value();
                            } else if (boost::iequals(iter2.key(), BBL_JSON_KEY_SUB_PATH)) {
                                subpath = iter2.value();
                            }
                        }
                        else {
                            BOOST_LOG_TRIVIAL(error) << "invalid value type for " << iter2.key();
                        }
                    }
                    if (!name.empty() && !subpath.empty())
                        subfile_map.push_back(std::make_pair(name, subpath));
                }
                else
                    BOOST_LOG_TRIVIAL(error) << "invalid type for " << iter1.key();
            }
        }
        else
            BOOST_LOG_TRIVIAL(error) << "invalid type for " << it.key();
    };
    try {
        boost::nowide::ifstream ifs(root_file);
        json j;
        ifs >> j;
        //parse the json elements
        for (auto it = j.begin(); it != j.end(); it++) {
            if (boost::iequals(it.key(), BBL_JSON_KEY_VERSION)) {
                //get version
                std::string version_str = it.value();
                auto config_version = Semver::parse(version_str);
                if (! config_version) {
                    throw ConfigurationError((boost::format("vendor %1%'s config version: %2% invalid\nSuggest closing studio, deleting all files in %3%, and restarting studio")
                        % vendor_name % version_str % path).str());
                } else {
                    vendor_profile.config_version = std::move(*config_version);
                }
            }
            else if (boost::iequals(it.key(), BBL_JSON_KEY_URL)) {
                //get url
                vendor_profile.config_update_url = it.value();
            }
            else if (boost::iequals(it.key(), BBL_JSON_KEY_DESCRIPTION)) {
                //get description
                //BOOST_LOG_TRIVIAL(trace) << __FUNCTION__<< ": parse "<<root_file<<", got description:  " << it.value();
            }
            else if (boost::iequals(it.key(), BBL_JSON_KEY_NAME)) {
                //get name
                vendor_profile.name = it.value();
            }
            else if (boost::iequals(it.key(), BBL_JSON_KEY_MACHINE_MODEL_LIST)) {
                //get machine model list
                get_name_and_subpath(it, machine_model_subfiles);
            }
            else if (boost::iequals(it.key(), BBL_JSON_KEY_PROCESS_LIST)) {
                //get process list
                get_name_and_subpath(it, process_subfiles);
            }
            else if (boost::iequals(it.key(), BBL_JSON_KEY_FILAMENT_LIST)) {
                //get filament list
                get_name_and_subpath(it, filament_subfiles);
            }
            else if (boost::iequals(it.key(), BBL_JSON_KEY_MACHINE_LIST)) {
                //get machine list
                get_name_and_subpath(it, machine_subfiles);
            }
        }
    }
    catch(nlohmann::detail::parse_error &err) {
        BOOST_LOG_TRIVIAL(error) << "parse "<<root_file<<" got a nlohmann::detail::parse_error, reason = " << err.what();
        throw ConfigurationError((boost::format("Failed loading configuration file %1%: %2%\nSuggest closing studio, deleting all files in %3%, and restarting studio")
                %root_file %err.what() % path).str());
        //goto __error_process;
    }

    if (flags.has(LoadConfigBundleAttribute::LoadFilamentOnly)) {
        machine_model_subfiles.clear();
        machine_subfiles.clear();
        process_subfiles.clear();
    }

    //2) paste the machine model
#if PARALLEL_LOAD_PRESET

    std::mutex mutex;
    tbb::parallel_for(tbb::blocked_range<int>(0, machine_model_subfiles.size()),
                      [this, &mutex, &vendor_profile, &machine_model_subfiles, &path, &vendor_name](const tbb::blocked_range<int> &range) {
                          for (int i = range.begin(); i < range.end(); ++i) {
                              auto& machine_model = machine_model_subfiles[i];
                              std::string subfile = path + "/" + vendor_name + "/" + machine_model.second;
                              if (!boost::filesystem::exists(subfile)) {
                                  BOOST_LOG_TRIVIAL(error) << boost::format("%1% file not exist.") % subfile;
                                  continue;
                              }
                              VendorProfile::PrinterModel model = load_vendor_configs_from_json(subfile);
                              model.id = machine_model.first;
                              std::lock_guard<std::mutex> lock(mutex);
                              if (!model.id.empty() && !model.variants.empty())
                                  vendor_profile.models.push_back(std::move(model));
                          }
                      });

#else

    for (auto& machine_model : machine_model_subfiles)
    {
        std::string subfile = path + "/" + vendor_name + "/" + machine_model.second;
        if (!boost::filesystem::exists(subfile)) {
            BOOST_LOG_TRIVIAL(error) << boost::format("%1% file not exist.") % subfile;
            continue;
        }
        VendorProfile::PrinterModel model = load_vendor_configs_from_json(subfile);
        model.id                          = machine_model.first;

        if (! model.id.empty() && ! model.variants.empty())
            vendor_profile.models.push_back(std::move(model));
    }

#endif

    //insert the vendor profile
    this->vendors.emplace(vendor_name, vendor_profile);
    const VendorProfile* current_vendor_profile = &this->vendors[vendor_name];

    BOOST_LOG_TRIVIAL(debug) << boost::format("loaded vendor profile, name %1%, id %2%, version %3%")%vendor_profile.name%vendor_profile.id%vendor_profile.config_version.to_string();

    if (flags.has(LoadConfigBundleAttribute::LoadVendorOnly))
        return std::make_pair(PresetsConfigSubstitutions{}, 0);

    // 3) paste the process/filament/print configs
    PresetCollection         *presets = nullptr;
    size_t                   presets_loaded = 0;
#if PARALLEL_LOAD_PRESET
    struct ParallelPresetLoadData
    {
        std::pair<std::string, std::string>        subfile;
        DynamicPrintConfig                         config_src;
        std::string                                reason;
        std::map<std::string, std::string>         key_values;
        std::shared_ptr<ConfigSubstitutionContext> context;
    };
    std::vector<std::shared_ptr<ParallelPresetLoadData>> parallelLoadData;

    auto parse_config = [this, path, vendor_name, presets_loaded,
                         current_vendor_profile](std::shared_ptr<ParallelPresetLoadData> presetLoadData, ConfigSubstitutionContext &substitution_context,
                                                 PresetsConfigSubstitutions &substitutions, LoadConfigBundleAttributes &flags, std::pair<std::string, std::string> &subfile_iter,
                                                 std::map<std::string, DynamicPrintConfig> &config_maps, std::map<std::string, std::string> &filament_id_maps,
                                                 PresetCollection *presets_collection, size_t &count, std::map<std::string, std::string> &description_maps) -> std::string {
        std::string subfile = path + "/" + vendor_name + "/" + subfile_iter.second;
        // Load the print, filament or printer preset.
        std::string               preset_name;
        DynamicPrintConfig        config;
        std::string               alias_name, inherits, description, instantiation, setting_id, filament_id;
        std::vector<std::string>  renamed_from;
        std::vector<std::string>  includes;
        const DynamicPrintConfig *default_config = nullptr;
        std::string               reason;
        try {
            std::map<std::string, std::string> &key_values = presetLoadData->key_values;
            substitution_context.substitutions.clear();

            // parse the json elements
            DynamicPrintConfig &config_src     = presetLoadData->config_src;
            reason                             = presetLoadData->reason;
            substitution_context.substitutions = std::move(presetLoadData->context->substitutions);
            for (auto &item : presetLoadData->context->unrecogized_keys) {
                auto iter = std::find(substitution_context.unrecogized_keys.begin(), substitution_context.unrecogized_keys.end(), item);
                if (iter == substitution_context.unrecogized_keys.end()) {
                    // emplace back
                    substitution_context.unrecogized_keys.emplace_back(item);
                }
            }
            if (!reason.empty()) {
                BOOST_LOG_TRIVIAL(error) << "load config file " << subfile << " Failed!";
                return reason;
            }
            preset_name = key_values[BBL_JSON_KEY_NAME];
            instantiation   = key_values[BBL_JSON_KEY_INSTANTIATION];
            auto setting_it = key_values.find(BBL_JSON_KEY_SETTING_ID);
            if (setting_it != key_values.end())
                setting_id = setting_it->second;
            auto filament_it = key_values.find(BBL_JSON_KEY_FILAMENT_ID);
            if (filament_it != key_values.end())
                filament_id = filament_it->second;
            //check whether it inherits other preset or not
            auto it1 = key_values.find(BBL_JSON_KEY_INHERITS);
            if (it1 != key_values.end()) {
                inherits = it1->second;
                auto it2 = config_maps.find(inherits);
                if (it2 != config_maps.end()) {
                    default_config = &(it2->second);
                    if (filament_id.empty() && (presets_collection->type() == Preset::TYPE_FILAMENT)) {
                        auto filament_id_map_iter = filament_id_maps.find(inherits);
                        if (filament_id_map_iter != filament_id_maps.end()) {
                            filament_id = filament_id_map_iter->second;
                        }
                    }
                }
                else {
                    BOOST_LOG_TRIVIAL(error) << "can not find inherits "<<inherits<<" for " << preset_name;
                    //throw ConfigurationError(format("can not find inherits %1% for %2%", inherits, preset_name));
                    reason = "Can not find inherits: " + inherits;
                    return reason;
                }
                if (auto ds_iter = description_maps.find(inherits); ds_iter != description_maps.end())
                    description = ds_iter->second;
            }
            else {
                if (presets_collection->type() == Preset::TYPE_PRINTER)
                    default_config = &presets_collection->default_preset_for(config_src).config;
                else
                    default_config = &presets_collection->default_preset().config;
            }
            config = *default_config;

            auto includes_it = key_values.find(BBL_JSON_KEY_INCLUDES);
            if (includes_it != key_values.end() && !includes_it->second.empty()) {
                std::string include = includes_it->second;
                if (include.front() == '"' && include.back() == '"') {
                    includes.push_back(include.substr(1, include.length() - 2));
                }
                else if (include.front() == '[' && include.back() == ']') {
                    try {
                        nlohmann::json includes_json = nlohmann::json::parse(include);
                        if (includes_json.is_array()) {
                            for (const auto &include_item : includes_json) {
                                if (include_item.is_string()) { includes.push_back(include_item.get<std::string>()); }
                            }
                        }
                    } catch (const std::exception &e) {
                        BOOST_LOG_TRIVIAL(error) << "Failed to parse includes array: " << include;
                    }
                }
                else
                    BOOST_LOG_TRIVIAL(error) << "invalid includes format: " << include;
            }
            for (const auto &name : includes) {
                auto it = config_maps.find(name);
                if (it != config_maps.end()) {
                    const DynamicPrintConfig &include_config = it->second;

                    const DynamicPrintConfig *include_default_config = nullptr;
                    if (presets_collection->type() == Preset::TYPE_PRINTER)
                        include_default_config = &presets_collection->default_preset_for(include_config).config;
                    else
                        include_default_config = &presets_collection->default_preset().config;
                    std::vector<std::string> keys = include_config.diff(*include_default_config);

                    config.apply_only(include_config, keys);

                    BOOST_LOG_TRIVIAL(trace) << "Applied include " << name << " to " << preset_name;
                }
                else
                    BOOST_LOG_TRIVIAL(error) << "can not find include " << name << " for " << preset_name;
            }

            if ( auto ds_iter=key_values.find(BBL_JSON_KEY_DESCRIPTION); ds_iter != key_values.end())
                description = ds_iter->second;

            if (!description.empty())
                description_maps.emplace(preset_name, description);

            config.apply(config_src);
            if (instantiation == "false" && "Template" != vendor_name) {
                if (preset_name.empty() || preset_name.find("gcode") != std::string::npos) {
                    //pure included gcode file
                    config_maps.emplace(subfile_iter.first, std::move(config_src));
                    return reason;
                }
                else
                    config_maps.emplace(preset_name, std::move(config));
                if ((presets_collection->type() == Preset::TYPE_FILAMENT) && (!filament_id.empty()))
                    filament_id_maps.emplace(preset_name, filament_id);
                return reason;
            }
            else if (instantiation.empty() && (preset_name.empty() || preset_name.find("gcode") != std::string::npos)) {
                //pure included config
                config_maps.emplace(subfile_iter.first, std::move(config_src));
                return reason;
            }
            if (config.has("alias"))
                alias_name = (dynamic_cast<const ConfigOptionString *>(config.option("alias")))->value;
            if (config.has("renamed_from")) {
                const ConfigOptionVectorBase *vec = static_cast<const ConfigOptionVectorBase*>(config.option("renamed_from"));
                renamed_from = vec->vserialize();
            }
            Preset::normalize(config);
        }
        catch(nlohmann::detail::parse_error &err) {
            BOOST_LOG_TRIVIAL(error) << "parse "<< subfile <<" got a nlohmann::detail::parse_error, reason = " << err.what();
            reason = std::string("json parse error") + err.what();
            return reason;
        }

        // Report configuration fields, which are misplaced into a wrong group.
        std::string incorrect_keys = Preset::remove_invalid_keys(config, *default_config);
        if (! incorrect_keys.empty())
            BOOST_LOG_TRIVIAL(error)<< "The config " <<
                subfile << " contains incorrect keys: " << incorrect_keys << ", which were removed";

        if (presets_collection->type() == Preset::TYPE_PRINTER) {
            // Filter out printer presets, which are not mentioned in the vendor profile.
            // These presets are considered not installed.
            auto printer_model   = config.opt_string("printer_model");
            if (printer_model.empty()) {
                BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The printer preset \"" <<
                    preset_name << "\" defines no printer model, it will be ignored.";
                reason = std::string("can not find printer_model");
                return reason;
            }
            auto printer_variant = config.opt_string("printer_variant");
            if (printer_variant.empty()) {
                BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The printer preset \"" <<
                    preset_name << "\" defines no printer variant, it will be ignored.";
                reason = std::string("can not find printer_variant");
                return reason;
            }
            auto it_model = std::find_if(current_vendor_profile->models.cbegin(), current_vendor_profile->models.cend(),
                [&](const VendorProfile::PrinterModel &m) { return m.id == printer_model; }
            );
            if (it_model == current_vendor_profile->models.end()) {
                BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The printer preset \"" <<
                    preset_name << "\" defines invalid printer model \"" << printer_model << "\", it will be ignored.";
                reason = std::string("can not find printer model in vendor profile");
                return reason;
            }
            auto it_variant = it_model->variant(printer_variant);
            if (it_variant == nullptr) {
                BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The printer preset \"" <<
                    preset_name << "\" defines invalid printer variant \"" << printer_variant << "\", it will be ignored.";
                reason = std::string("can not find printer_variant in vendor profile");
                return reason;
            }
        }
        const Preset *preset_existing = presets_collection->find_preset(preset_name, false);
        if (preset_existing != nullptr) {
            BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The printer preset \"" <<
                preset_name << "\" has already been loaded from another Confing Bundle.";
            reason = std::string("duplicated defines");
            return reason;
        }

        auto file_path = (boost::filesystem::path(data_dir())  /PRESET_SYSTEM_DIR/ vendor_name / subfile_iter.second).make_preferred();
        // Load the preset into the list of presets, save it to disk.
        Preset &loaded = presets_collection->load_preset(file_path.string(), preset_name, std::move(config), false);
        if (flags.has(LoadConfigBundleAttribute::LoadSystem)) {
            loaded.is_system = true;
            loaded.vendor = current_vendor_profile;
            loaded.version = current_vendor_profile->config_version;
            loaded.description = description;
            loaded.setting_id = setting_id;
            loaded.filament_id = filament_id;
            BOOST_LOG_TRIVIAL(trace) << loaded.name << " load " << KV(log_key::filament_id, filament_id);
            if (presets_collection->type() == Preset::TYPE_FILAMENT) {
                if (filament_id.empty() && "Template" != vendor_name) {
                    BOOST_LOG_TRIVIAL(error) << "can not find filament_id for " << preset_name;
                    //throw ConfigurationError(format("can not find inherits %1% for %2%", inherits, preset_name));
                    reason = "Can not find filament_id for " + preset_name;
                    return reason;
                }
                else {
                    filament_id_maps.emplace(preset_name, filament_id);
                }
            }
        }

        // Derive the profile logical name aka alias from the preset name if the alias was not stated explicitely.
        if (alias_name.empty()) {
            size_t end_pos = preset_name.find_first_of("@");
            if (end_pos != std::string::npos) {
                alias_name = preset_name.substr(0, end_pos);
                if (renamed_from.empty())
                    // Add the preset name with the '@' character removed into the "renamed_from" list.
                    renamed_from.emplace_back(alias_name + preset_name.substr(end_pos + 1));
                boost::trim_right(alias_name);
            }
        }
        if (alias_name.empty())
            loaded.alias = preset_name;
        else {
            loaded.alias = std::move(alias_name);
            filaments.set_printer_hold_alias(loaded.alias, loaded);
        }
        loaded.renamed_from = std::move(renamed_from);
        if (! substitution_context.empty())
            substitutions.push_back({
                preset_name, presets_collection->type(), PresetConfigSubstitutions::Source::ConfigBundle,
                std::string(), std::move(substitution_context.substitutions) });
        config_maps.emplace(preset_name, loaded.config);
        ++count;
        //BBS: add config related logs
        BOOST_LOG_TRIVIAL(trace) << boost::format("got preset %1%")%loaded.name;
        return reason;
    };
#else
    auto parse_subfile = [this, path, vendor_name, presets_loaded, current_vendor_profile](\
        ConfigSubstitutionContext& substitution_context,
        PresetsConfigSubstitutions& substitutions,
        LoadConfigBundleAttributes& flags,
        std::pair<std::string, std::string>& subfile_iter,
        std::map<std::string, DynamicPrintConfig>& config_maps,
        std::map<std::string, std::string>& filament_id_maps,
        PresetCollection* presets_collection,
        size_t& count,
        std::map<std::string, std::string>&description_maps) -> std::string {

        std::string subfile = path + "/" + vendor_name + "/" + subfile_iter.second;
        // Load the print, filament or printer preset.
        std::string               preset_name;
        DynamicPrintConfig        config;
        std::string 			  alias_name, inherits, description, instantiation, setting_id, filament_id;
        std::vector<std::string>  renamed_from;
        std::vector<std::string>  includes;
        const DynamicPrintConfig* default_config = nullptr;
        std::string               reason;
        try {
            std::map<std::string, std::string> key_values;
            substitution_context.substitutions.clear();

            //parse the json elements
            DynamicPrintConfig config_src;
            config_src.load_from_json(subfile, substitution_context, false, key_values, reason);
            if (!reason.empty()) {
                BOOST_LOG_TRIVIAL(error) << "load config file "<<subfile<<" Failed!";
                return reason;
            }
            preset_name = key_values[BBL_JSON_KEY_NAME];
            instantiation   = key_values[BBL_JSON_KEY_INSTANTIATION];
            auto setting_it = key_values.find(BBL_JSON_KEY_SETTING_ID);
            if (setting_it != key_values.end())
                setting_id = setting_it->second;
            auto filament_it = key_values.find(BBL_JSON_KEY_FILAMENT_ID);
            if (filament_it != key_values.end())
                filament_id = filament_it->second;
            //check whether it inherits other preset or not
            auto it1 = key_values.find(BBL_JSON_KEY_INHERITS);
            if (it1 != key_values.end()) {
                inherits = it1->second;
                auto it2 = config_maps.find(inherits);
                if (it2 != config_maps.end()) {
                    default_config = &(it2->second);
                    if (filament_id.empty() && (presets_collection->type() == Preset::TYPE_FILAMENT)) {
                        auto filament_id_map_iter = filament_id_maps.find(inherits);
                        if (filament_id_map_iter != filament_id_maps.end()) {
                            filament_id = filament_id_map_iter->second;
                        }
                    }
                }
                else {
                    BOOST_LOG_TRIVIAL(error) << "can not find inherits "<<inherits<<" for " << preset_name;
                    //throw ConfigurationError(format("can not find inherits %1% for %2%", inherits, preset_name));
                    reason = "Can not find inherits: " + inherits;
                    return reason;
                }
                if (auto ds_iter = description_maps.find(inherits); ds_iter != description_maps.end())
                    description = ds_iter->second;
            }
            else {
                if (presets_collection->type() == Preset::TYPE_PRINTER)
                    default_config = &presets_collection->default_preset_for(config_src).config;
                else
                    default_config = &presets_collection->default_preset().config;
            }
            config = *default_config;

            auto includes_it = key_values.find(BBL_JSON_KEY_INCLUDES);
            if (includes_it != key_values.end() && !includes_it->second.empty()) {
                std::string include = includes_it->second;
                if (include.front() == '"' && include.back() == '"') {
                    includes.push_back(include.substr(1, include.length() - 2));
                }
                else if (include.front() == '[' && include.back() == ']') {
                    try {
                        nlohmann::json includes_json = nlohmann::json::parse(include);
                        if (includes_json.is_array()) {
                            for (const auto &include_item : includes_json) {
                                if (include_item.is_string()) { includes.push_back(include_item.get<std::string>()); }
                            }
                        }
                    } catch (const std::exception &e) {
                        BOOST_LOG_TRIVIAL(error) << "Failed to parse includes array: " << include;
                    }
                }
                else
                    BOOST_LOG_TRIVIAL(error) << "invalid includes format: " << include;
            }
            for (const auto &name : includes) {
                auto it = config_maps.find(name);
                if (it != config_maps.end()) {
                    const DynamicPrintConfig &include_config = it->second;

                    const DynamicPrintConfig *include_default_config = nullptr;
                    if (presets_collection->type() == Preset::TYPE_PRINTER)
                        include_default_config = &presets_collection->default_preset_for(include_config).config;
                    else
                        include_default_config = &presets_collection->default_preset().config;
                    std::vector<std::string> keys = include_config.diff(*include_default_config);

                    config.apply_only(include_config, keys);

                    BOOST_LOG_TRIVIAL(trace) << "Applied include " << name << " to " << preset_name;
                }
                else
                    BOOST_LOG_TRIVIAL(error) << "can not find include " << name << " for " << preset_name;
            }

            if ( auto ds_iter=key_values.find(BBL_JSON_KEY_DESCRIPTION); ds_iter != key_values.end())
                description = ds_iter->second;

            if (!description.empty())
                description_maps.emplace(preset_name, description);

            config.apply(config_src);
            if (instantiation == "false" && "Template" != vendor_name) {
                if (preset_name.empty() || preset_name.find("gcode") != std::string::npos) {
                    //pure included gcode file
                    config_maps.emplace(subfile_iter.first, std::move(config_src));
                    return reason;
                }
                else
                    config_maps.emplace(preset_name, std::move(config));
                if ((presets_collection->type() == Preset::TYPE_FILAMENT) && (!filament_id.empty()))
                    filament_id_maps.emplace(preset_name, filament_id);
                return reason;
            }
            else if (instantiation.empty() && (preset_name.empty() || preset_name.find("gcode") != std::string::npos)) {
                //pure included config
                config_maps.emplace(subfile_iter.first, std::move(config_src));
                return reason;
            }
            if (config.has("alias"))
                alias_name = (dynamic_cast<const ConfigOptionString *>(config.option("alias")))->value;
            if (config.has("renamed_from")) {
                const ConfigOptionVectorBase *vec = static_cast<const ConfigOptionVectorBase*>(config.option("renamed_from"));
                renamed_from = vec->vserialize();
            }
            Preset::normalize(config);
        }
        catch(nlohmann::detail::parse_error &err) {
            BOOST_LOG_TRIVIAL(error) << "parse "<< subfile <<" got a nlohmann::detail::parse_error, reason = " << err.what();
            reason = std::string("json parse error") + err.what();
            return reason;
        }

        // Report configuration fields, which are misplaced into a wrong group.
        std::string incorrect_keys = Preset::remove_invalid_keys(config, *default_config);
        if (! incorrect_keys.empty())
            BOOST_LOG_TRIVIAL(error)<< "The config " <<
                subfile << " contains incorrect keys: " << incorrect_keys << ", which were removed";

        if (presets_collection->type() == Preset::TYPE_PRINTER) {
            // Filter out printer presets, which are not mentioned in the vendor profile.
            // These presets are considered not installed.
            auto printer_model   = config.opt_string("printer_model");
            if (printer_model.empty()) {
                BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The printer preset \"" <<
                    preset_name << "\" defines no printer model, it will be ignored.";
                reason = std::string("can not find printer_model");
                return reason;
            }
            auto printer_variant = config.opt_string("printer_variant");
            if (printer_variant.empty()) {
                BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The printer preset \"" <<
                    preset_name << "\" defines no printer variant, it will be ignored.";
                reason = std::string("can not find printer_variant");
                return reason;
            }
            auto it_model = std::find_if(current_vendor_profile->models.cbegin(), current_vendor_profile->models.cend(),
                [&](const VendorProfile::PrinterModel &m) { return m.id == printer_model; }
            );
            if (it_model == current_vendor_profile->models.end()) {
                BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The printer preset \"" <<
                    preset_name << "\" defines invalid printer model \"" << printer_model << "\", it will be ignored.";
                reason = std::string("can not find printer model in vendor profile");
                return reason;
            }
            auto it_variant = it_model->variant(printer_variant);
            if (it_variant == nullptr) {
                BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The printer preset \"" <<
                    preset_name << "\" defines invalid printer variant \"" << printer_variant << "\", it will be ignored.";
                reason = std::string("can not find printer_variant in vendor profile");
                return reason;
            }
        }
        const Preset *preset_existing = presets_collection->find_preset(preset_name, false);
        if (preset_existing != nullptr) {
            BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The printer preset \"" <<
                preset_name << "\" has already been loaded from another Confing Bundle.";
            reason = std::string("duplicated defines");
            return reason;
        }

        auto file_path = (boost::filesystem::path(data_dir())  /PRESET_SYSTEM_DIR/ vendor_name / subfile_iter.second).make_preferred();
        // Load the preset into the list of presets, save it to disk.
        Preset &loaded = presets_collection->load_preset(file_path.string(), preset_name, std::move(config), false);
        if (flags.has(LoadConfigBundleAttribute::LoadSystem)) {
            loaded.is_system = true;
            loaded.vendor = current_vendor_profile;
            loaded.version = current_vendor_profile->config_version;
            loaded.description = description;
            loaded.setting_id = setting_id;
            loaded.filament_id = filament_id;
            BOOST_LOG_TRIVIAL(trace) << loaded.name << " load " << KV(log_key::filament_id, filament_id);
            if (presets_collection->type() == Preset::TYPE_FILAMENT) {
                if (filament_id.empty() && "Template" != vendor_name) {
                    BOOST_LOG_TRIVIAL(error) << "can not find filament_id for " << preset_name;
                    //throw ConfigurationError(format("can not find inherits %1% for %2%", inherits, preset_name));
                    reason = "Can not find filament_id for " + preset_name;
                    return reason;
                }
                else {
                    filament_id_maps.emplace(preset_name, filament_id);
                }
            }
        }

        // Derive the profile logical name aka alias from the preset name if the alias was not stated explicitely.
        if (alias_name.empty()) {
            size_t end_pos = preset_name.find_first_of("@");
            if (end_pos != std::string::npos) {
                alias_name = preset_name.substr(0, end_pos);
                if (renamed_from.empty())
                    // Add the preset name with the '@' character removed into the "renamed_from" list.
                    renamed_from.emplace_back(alias_name + preset_name.substr(end_pos + 1));
                boost::trim_right(alias_name);
            }
        }
        if (alias_name.empty())
            loaded.alias = preset_name;
        else {
            loaded.alias = std::move(alias_name);
            filaments.set_printer_hold_alias(loaded.alias, loaded);
        }
        loaded.renamed_from = std::move(renamed_from);
        if (! substitution_context.empty())
            substitutions.push_back({
                preset_name, presets_collection->type(), PresetConfigSubstitutions::Source::ConfigBundle,
                std::string(), std::move(substitution_context.substitutions) });
        config_maps.emplace(preset_name, loaded.config);
        ++count;
        //BBS: add config related logs
        BOOST_LOG_TRIVIAL(trace) << boost::format("got preset %1%")%loaded.name;
        return reason;
    };
#endif
    std::map<std::string, DynamicPrintConfig> configs;
    std::map<std::string, std::string> filament_id_maps;
    std::map<std::string, std::string> description_maps;

    //3.1) paste the process
    presets = &this->prints;
    configs.clear();
    filament_id_maps.clear();

#if PARALLEL_LOAD_PRESET
    for (auto &subfile : process_subfiles) {
        std::shared_ptr<ParallelPresetLoadData> configData(new ParallelPresetLoadData());
        configData->context = std::make_shared<ConfigSubstitutionContext>(substitution_context.rule);
        configData->subfile = std::move(subfile);
        parallelLoadData.emplace_back(configData);
    }
    tbb::parallel_for(tbb::blocked_range<int>(0, parallelLoadData.size()), [this, &parallelLoadData, &vendor_name, &path](const tbb::blocked_range<int> &range) {
        for (int i = range.begin(); i < range.end(); ++i) {
            auto        configData = parallelLoadData[i];
            std::string subfile    = path + "/" + vendor_name + "/" + configData->subfile.second;
            configData->config_src.load_from_json(subfile, *configData->context.get(), false, configData->key_values, configData->reason);
        }
    });
    for (auto i = 0; i < parallelLoadData.size(); i++) {
        auto        loadData = parallelLoadData[i];
        std::string reason   = parse_config(loadData, substitution_context, substitutions, flags, loadData->subfile, configs, filament_id_maps, presets, presets_loaded,
                                            description_maps);
        if (!reason.empty()) {
            // parse error
            std::string subfile_path = path + "/" + vendor_name + "/" + loadData->subfile.second;
            BOOST_LOG_TRIVIAL(error) << boost::format("got error when parse process setting from %1%") % subfile_path;
            throw ConfigurationError(
                (boost::format("Failed loading configuration file %1%\nSuggest closing studio, deleting all files in %2%, and restarting studio") % subfile_path % path).str());
        }
    }
#else
    for (auto& subfile : process_subfiles)
    {
        std::string reason = parse_subfile(substitution_context, substitutions, flags, subfile, configs, filament_id_maps, presets, presets_loaded, description_maps);
        if (!reason.empty()) {
            //parse error
            std::string subfile_path = path + "/" + vendor_name + "/" + subfile.second;
            BOOST_LOG_TRIVIAL(error) << boost::format("got error when parse process setting from %1%") % subfile_path;
            throw ConfigurationError((boost::format("Failed loading configuration file %1%\nSuggest closing studio, deleting all files in %2%, and restarting studio") % subfile_path % path).str());
        }
    }
#endif

    //3.2) paste the filaments
    presets = &this->filaments;
    configs.clear();
    filament_id_maps.clear();
#if PARALLEL_LOAD_PRESET
    parallelLoadData.clear();
    for (auto &subfile : filament_subfiles) {
        std::shared_ptr<ParallelPresetLoadData> configData(new ParallelPresetLoadData());
        configData->context = std::make_shared<ConfigSubstitutionContext>(substitution_context.rule);
        configData->subfile = std::move(subfile);
        parallelLoadData.emplace_back(configData);
    }
    tbb::parallel_for(tbb::blocked_range<int>(0, parallelLoadData.size()), [this, &parallelLoadData, &vendor_name, &path](const tbb::blocked_range<int> &range) {
        for (int i = range.begin(); i < range.end(); ++i) {
            auto        configData = parallelLoadData[i];
            std::string subfile    = path + "/" + vendor_name + "/" + configData->subfile.second;
            configData->config_src.load_from_json(subfile, *configData->context.get(), false, configData->key_values, configData->reason);
        }
    });
    for (auto i = 0; i < parallelLoadData.size(); i++) {
        auto        loadData = parallelLoadData[i];
        std::string reason   = parse_config(loadData, substitution_context, substitutions, flags, loadData->subfile, configs, filament_id_maps, presets, presets_loaded,
                                            description_maps);
        if (!reason.empty()) {
            // parse error
            std::string subfile_path = path + "/" + vendor_name + "/" + loadData->subfile.second;
            BOOST_LOG_TRIVIAL(error) << boost::format("got error when parse filament setting from %1%") % subfile_path;
            throw ConfigurationError(
                (boost::format("Failed loading configuration file %1%\nSuggest closing studio, deleting all files in %2%, and restarting studio") % subfile_path % path).str());
        }
    }
#else
    for (auto& subfile : filament_subfiles)
    {
        std::string reason = parse_subfile(substitution_context, substitutions, flags, subfile, configs, filament_id_maps, presets, presets_loaded, description_maps);
        if (!reason.empty()) {
            //parse error
            std::string subfile_path = path + "/" + vendor_name + "/" + subfile.second;
            BOOST_LOG_TRIVIAL(error) << boost::format("got error when parse filament setting from %1%") % subfile_path;
            throw ConfigurationError((boost::format("Failed loading configuration file %1%\nSuggest closing studio, deleting all files in %2%, and restarting studio") % subfile_path % path).str());
        }
    }
#endif
    //3.3) paste the printers
    presets = &this->printers;
    configs.clear();
    filament_id_maps.clear();

#if PARALLEL_LOAD_PRESET

    parallelLoadData.clear();
    for (auto &subfile : machine_subfiles) {
        std::shared_ptr<ParallelPresetLoadData> configData(new ParallelPresetLoadData());
        configData->context = std::make_shared<ConfigSubstitutionContext>(substitution_context.rule);
        configData->subfile = std::move(subfile);
        parallelLoadData.emplace_back(configData);
    }
    tbb::parallel_for(tbb::blocked_range<int>(0, parallelLoadData.size()), [this, &parallelLoadData, &vendor_name, &path](const tbb::blocked_range<int> &range) {
        for (int i = range.begin(); i < range.end(); ++i) {
            auto        configData = parallelLoadData[i];
            std::string subfile    = path + "/" + vendor_name + "/" + configData->subfile.second;
            configData->config_src.load_from_json(subfile, *configData->context.get(), false, configData->key_values, configData->reason);
        }
    });
    for (auto i = 0; i < parallelLoadData.size(); i++) {
        auto        loadData = parallelLoadData[i];
        std::string reason   = parse_config(loadData, substitution_context, substitutions, flags, loadData->subfile, configs, filament_id_maps, presets, presets_loaded,
                                            description_maps);
        if (!reason.empty()) {
            // parse error
            std::string subfile_path = path + "/" + vendor_name + "/" + loadData->subfile.second;
            BOOST_LOG_TRIVIAL(error) << boost::format("got error when parse printer setting from %1%") % subfile_path;
            throw ConfigurationError(
                (boost::format("Failed loading configuration file %1%\nSuggest closing studio, deleting all files in %2%, and restarting studio") % subfile_path % path).str());
        }
    }

#else
    for (auto& subfile : machine_subfiles)
    {
        std::string reason = parse_subfile(substitution_context, substitutions, flags, subfile, configs, filament_id_maps, presets, presets_loaded, description_maps);
        if (!reason.empty()) {
            //parse error
            std::string subfile_path = path + "/" + vendor_name + "/" + subfile.second;
            BOOST_LOG_TRIVIAL(error) << boost::format("got error when parse printer setting from %1%") % subfile_path;
            throw ConfigurationError((boost::format("Failed loading configuration file %1%\nSuggest closing studio, deleting all files in %2%, and restarting studio") % subfile_path % path).str());
        }
    }
#endif
    //BBS: add config related logs
    BOOST_LOG_TRIVIAL(debug) << boost::format("finished, presets_loaded %1%")%presets_loaded;
    return std::make_pair(std::move(substitutions), presets_loaded);
}

VendorProfile::PrinterModel PresetBundle::load_vendor_configs_from_json(const std::string &path)
{
    VendorProfile::PrinterModel model;
    if (!boost::filesystem::exists(path)) {
        BOOST_LOG_TRIVIAL(error) << boost::format("%1% file not exist.") % path;
        throw ConfigurationError((boost::format("Failed loading configuration file, please check %1% firstly") % path).str());
        return model;
    }
    try {
        boost::nowide::ifstream ifs(path);
        json                    j;
        ifs >> j;
        // parse the json elements
        for (auto it = j.begin(); it != j.end(); it++) {
            if (boost::iequals(it.key(), BBL_JSON_KEY_VERSION)) {
                // get version
            } else if (boost::iequals(it.key(), BBL_JSON_KEY_URL)) {
                // get url
            } else if (boost::iequals(it.key(), BBL_JSON_KEY_NAME)) {
                // get name
                model.name = it.value();
            } else if (boost::iequals(it.key(), BBL_JSON_KEY_MODEL_ID)) {
                // get model_id
                model.model_id = it.value();
            } else if (boost::iequals(it.key(), BBL_JSON_KEY_NOZZLE_DIAMETER)) {
                // get nozzle diameter
                std::string              nozzle_diameters = it.value();
                std::vector<std::string> variants;
                if (Slic3r::unescape_strings_cstyle(nozzle_diameters, variants)) {
                    for (const std::string &variant_name : variants) {
                        if (model.variant(variant_name) == nullptr) model.variants.emplace_back(VendorProfile::PrinterVariant(variant_name));
                    }
                }
            } else if (boost::iequals(it.key(), BBL_JSON_KEY_PRINTER_TECH)) {
                // get printer tech
                if (boost::algorithm::starts_with(it.value(), "SL"))
                    model.technology = ptSLA;
                else
                    model.technology = ptFFF;
            } else if (boost::iequals(it.key(), BBL_JSON_KEY_FAMILY)) {
                // get family
                model.family = it.value();
            } else if (boost::iequals(it.key(), BBL_JSON_KEY_BED_MODEL)) {
                // get bed model
                model.bed_model = it.value();
            } else if (boost::iequals(it.key(), BBL_JSON_KEY_DEFAULT_BED_TYPE)) {
                // get bed type
                model.default_bed_type = it.value();
            } else if (boost::iequals(it.key(), BBL_JSON_KEY_RIGHT_ICON_OFFSET_BED)) {
                model.right_icon_offset_bed = it.value();
            } else if (boost::iequals(it.key(), BBL_JSON_KEY_BOTTOM_TEXTURE_END_NAME)) {
                model.bottom_texture_end_name = it.value();
            } else if (boost::iequals(it.key(), BBL_JSON_KEY_USE_DOUBLE_EXTRUDER_DEFAULT_TEXTURE)) {
                model.use_double_extruder_default_texture = it.value();
            } else if (boost::iequals(it.key(), BBL_JSON_KEY_BOTTOM_TEXTURE_RECT)) {
                model.bottom_texture_rect = it.value();
            } else if (boost::iequals(it.key(), BBL_JSON_KEY_BOTTOM_TEXTURE_RECT_LONGER)) {
                model.bottom_texture_rect_longer = it.value();
            } else if (boost::iequals(it.key(), BBL_JSON_KEY_BOTTOM_TEXTURE_RECT_LONGER_IGNORE_LIST)) {
                std::string longer_ignore_field = it.value();
                if (Slic3r::unescape_strings_cstyle(longer_ignore_field, model.bottom_texture_rect_longer_ignore_list)) {
                    Slic3r::sort_remove_duplicates(model.bottom_texture_rect_longer_ignore_list);
                    if (!model.bottom_texture_rect_longer_ignore_list.empty() && model.bottom_texture_rect_longer_ignore_list.front().empty())
                        model.bottom_texture_rect_longer_ignore_list.erase(model.bottom_texture_rect_longer_ignore_list.begin());
                } else {
                    BOOST_LOG_TRIVIAL(error) << boost::format("invalid bottom_texture_rect_longer_ignore_list %1% for Vendor") % longer_ignore_field;
                }
            } else if (boost::iequals(it.key(), BBL_JSON_KEY_MIDDLE_TEXTURE_RECT)) {
                model.middle_texture_rect = it.value();
            } else if (boost::iequals(it.key(), BBL_JSON_KEY_USE_RECT_GRID)) {
                model.use_rect_grid = it.value();
            } else if (boost::iequals(it.key(), BBL_JSON_KEY_IMAGE_BED_TYPE)) {
                model.image_bed_type = it.value();
            } else if (boost::iequals(it.key(), BBL_JSON_KEY_BED_TEXTURE)) {
                // get bed texture
                model.bed_texture = it.value();
            } else if (boost::iequals(it.key(), BBL_JSON_KEY_HOTEND_MODEL)) {
                model.hotend_model = it.value();
            } else if (boost::iequals(it.key(), BBL_JSON_KEY_DEFAULT_MATERIALS)) {
                // get machine list
                std::string default_materials_field = it.value();
                if (Slic3r::unescape_strings_cstyle(default_materials_field, model.default_materials)) {
                    Slic3r::sort_remove_duplicates(model.default_materials);
                    if (!model.default_materials.empty() && model.default_materials.front().empty())
                        // An empty material was inserted into the list of default materials. Remove it.
                        model.default_materials.erase(model.default_materials.begin());
                }
            } else if (boost::iequals(it.key(), BBL_JSON_KEY_NOT_SUPPORT_BED_TYPE)) {
                // get machine list
                std::string not_support_bed_type_field = it.value();
                if (Slic3r::unescape_strings_cstyle(not_support_bed_type_field, model.not_support_bed_types)) {
                    Slic3r::sort_remove_duplicates(model.not_support_bed_types);
                    if (!model.not_support_bed_types.empty() && model.not_support_bed_types.front().empty())
                        // An empty material was inserted into the list of default materials. Remove it.
                        model.not_support_bed_types.erase(model.not_support_bed_types.begin());
                } else {
                    BOOST_LOG_TRIVIAL(error) << boost::format("invalid not_support_bed_types %1% for Vendor") % not_support_bed_type_field;
                }
            } else if (boost::iequals(it.key(), BBL_JSON_KEY_SUPPORT_SIDE_PANEL_FAN)) {
                model.support_side_panel_fan = it.value();
            } else if (boost::iequals(it.key(), BBL_JSON_KEY_RESOURCE_BIND_NAME)) {
                model.resource_bind_name = it.value();
            }
        }
    } catch (nlohmann::detail::parse_error &err) {
        BOOST_LOG_TRIVIAL(error) << "parse "
                                 << " got a nlohmann::detail::parse_error, reason = " << err.what();
        throw ConfigurationError((boost::format("Failed loading configuration file  %1%\nSuggest closing studio, deleting all files in %2%, and restarting studio") % err.what() % path).str());
    }
    return model;
}

void PresetBundle::on_extruders_count_changed(int extruders_count, bool reset_volume_type)
{
    printers.get_edited_preset().set_num_extruders(extruders_count);
    update_multi_material_filament_presets();
    // A project load selects the file's nozzles itself, after its printer is in place.
    if (reset_volume_type)
        nozzle_inventory.select_printer(printers);
    extruder_ams_counts.resize(extruders_count);
    extruder_ams_selections.clear();
}

void PresetBundle::update_multi_material_filament_presets(size_t to_delete_filament_id)
{
    if (printers.get_edited_preset().printer_technology() != ptFFF)
        return;

    // BBS
#if 0
    // Verify and select the filament presets.
    auto   *nozzle_diameter = static_cast<const ConfigOptionFloats*>(printers.get_edited_preset().config.option("nozzle_diameter"));
    size_t  num_extruders   = nozzle_diameter->values.size();
    // Verify validity of the current filament presets.
    for (size_t i = 0; i < std::min(this->filament_presets.size(), num_extruders); ++ i)
        this->filament_presets[i] = this->filaments.find_preset(this->filament_presets[i], true)->name;
    // Append the rest of filament presets.
    this->filament_presets.resize(num_extruders, this->filament_presets.empty() ? this->filaments.first_visible().name : this->filament_presets.back());
#else
    size_t num_filaments = this->filament_presets.size();
#endif
    if (to_delete_filament_id == -1)
        to_delete_filament_id = num_filaments;

    // Now verify if flush_volumes_matrix has proper size (it is used to deduce number of extruders in wipe tower generator):
    std::vector<double> old_matrix = this->project_config.option<ConfigOptionFloats>("flush_volumes_matrix")->values;
    size_t old_nozzle_nums = this->project_config.option<ConfigOptionFloats>("flush_multiplier")->values.size();
    size_t old_number_of_filaments = size_t(sqrt(old_matrix.size() / old_nozzle_nums) + EPSILON);
    size_t nozzle_nums = get_printer_extruder_count();
    if (old_nozzle_nums != nozzle_nums) {
        std::vector<double>& f_multiplier = this->project_config.option<ConfigOptionFloats>("flush_multiplier")->values;
        f_multiplier.resize(nozzle_nums, 1.f);
        std::vector<double> &f_multiplier_fast = this->project_config.option<ConfigOptionFloats>("flush_multiplier_fast")->values;
        f_multiplier_fast.resize(nozzle_nums, 1.2f);
    }

    if ( (num_filaments * num_filaments) != size_t(old_matrix.size() / old_nozzle_nums) ) {
        // First verify if purging volumes presets for each extruder matches number of extruders
        std::vector<double>& filaments = this->project_config.option<ConfigOptionFloats>("flush_volumes_vector")->values;
        while (filaments.size() < 2* num_filaments) {
            filaments.push_back(filaments.size()>1 ? filaments[0] : 140.);  // copy the values from the first extruder
            filaments.push_back(filaments.size()>1 ? filaments[1] : 140.);
        }
        while (filaments.size() > 2* num_filaments) {
            filaments.pop_back();
            filaments.pop_back();
        }

        size_t old_matrix_size = old_number_of_filaments * old_number_of_filaments;
        size_t new_matrix_size = num_filaments * num_filaments;
        std::vector<double> new_matrix(new_matrix_size * nozzle_nums, 0);
        for (unsigned int i = 0; i < num_filaments; ++i)
            for (unsigned int j = 0; j < num_filaments; ++j) {
                if (i < old_number_of_filaments && j < old_number_of_filaments) {
                    unsigned int old_i = i >= to_delete_filament_id ? i + 1 : i;
                    unsigned int old_j = j >= to_delete_filament_id ? j + 1 : j;
                    for (size_t nozzle_id = 0; nozzle_id < nozzle_nums; ++nozzle_id) {
                        new_matrix[i * num_filaments + j + new_matrix_size * nozzle_id] = old_matrix[old_i * old_number_of_filaments + old_j + old_matrix_size * nozzle_id];
                    }
                } else {
                    for (size_t nozzle_id = 0; nozzle_id < nozzle_nums; ++nozzle_id) {
                        new_matrix[i * num_filaments + j + new_matrix_size * nozzle_id] = (i == j ? 0. : filaments[2 * i] + filaments[2 * j + 1]);
                    }
                }
            }
        this->project_config.option<ConfigOptionFloats>("flush_volumes_matrix")->values = new_matrix;
    }
}

namespace {

// Ranks presets by preference when picking a replacement for the current selection.
// The prefered name is held by reference: it must outlive the matcher.
class PreferedProfileMatch
{
public:
    PreferedProfileMatch(const std::string &prefered_alias, const std::string &prefered_name) :
        m_prefered_alias(prefered_alias), m_prefered_name(prefered_name) {}

    int operator()(const Preset &preset) const
    {
        return
            preset.is_default || preset.is_external ?
                // Don't match any properties of the "-- default --" profile or the external profiles when switching printer profile.
                0 :
                ! m_prefered_alias.empty() && m_prefered_alias == preset.alias ?
                    // Matching an alias, always take this preset with priority.
                    std::numeric_limits<int>::max() :
                    // Otherwise take the prefered profile, or the first compatible.
                    preset.name == m_prefered_name;
    }

private:
    const std::string  m_prefered_alias;
    const std::string &m_prefered_name;
};

// Matching by the layer height in addition.
class PreferedPrintProfileMatch : public PreferedProfileMatch
{
public:
    PreferedPrintProfileMatch(const Preset *preset, const std::string &prefered_name) :
        PreferedProfileMatch(preset ? preset->alias : std::string(), prefered_name), m_prefered_layer_height(preset ? preset->config.opt_float("layer_height") : 0) {}

    int operator()(const Preset &preset) const
    {
        // Don't match any properties of the "-- default --" profile or the external profiles when switching printer profile.
        if (preset.is_default || preset.is_external)
            return 0;
        int match_quality = PreferedProfileMatch::operator()(preset);
        if (match_quality < std::numeric_limits<int>::max()) {
            match_quality += 1;
            if (preset.is_visible)
                match_quality += 1;
            if (m_prefered_layer_height > 0. && std::abs(preset.config.opt_float("layer_height") - m_prefered_layer_height) < 0.0005)
                match_quality *= 10;
        }
        return match_quality;
    }

private:
    const double m_prefered_layer_height;
};

// Matching by the filament type in addition.
class PreferedFilamentProfileMatch : public PreferedProfileMatch
{
public:
    PreferedFilamentProfileMatch(const Preset *preset, const std::string &prefered_name) :
        PreferedProfileMatch(preset ? preset->alias : std::string(), prefered_name),
        m_prefered_filament_type(preset ? preset->config.opt_string("filament_type", 0) : std::string()) {}

    int operator()(const Preset &preset) const
    {
        // Don't match any properties of the "-- default --" profile or the external profiles when switching printer profile.
        if (preset.is_default || preset.is_external)
            return 0;
        int match_quality = PreferedProfileMatch::operator()(preset);
        if (match_quality < std::numeric_limits<int>::max()) {
            match_quality += 1;
            if(preset.is_visible)
                match_quality += 1;
            if (! m_prefered_filament_type.empty() && m_prefered_filament_type == preset.config.opt_string("filament_type", 0))
                match_quality *= 10;
        }
        return match_quality;
    }

private:
    const std::string m_prefered_filament_type;
};

// Replaces each built-in default filament slot with the printer's preferred filament for that slot.
// Returns true if any slot changed.
//
// A slot here never had a user choice behind it -- it is still the system default, waiting for this
// one-time substitution to give it a real material. So unlike every other filament swap (which leaves
// the slot's stored diameter/flow alone and lets validation flag a genuine mismatch -- see
// PresetBundle::set_filament_preset()), there is no prior user intent to preserve: a flow left over
// from whatever this printer preset's defaults happened to carry (e.g. a project-embedded TPU285
// filament this slot never got to pick) would otherwise outlive the swap and immediately flag the
// slot red. Settle it onto a flow the new filament can actually use instead.
bool replace_default_filament_slots(PresetBundle &bundle, const std::vector<std::string> &prefered_names)
{
    const PresetCollection   &filaments = bundle.filaments;
    std::vector<std::string> &slots     = bundle.filament_presets;

    bool changed = false;
    for (size_t idx = 0; idx < slots.size(); ++idx) {
        const Preset *preset = filaments.find_preset(slots[idx], false);
        if (preset == nullptr || !preset->is_default) continue;

        const std::string &prefered_name = idx < prefered_names.size() ? prefered_names[idx] : prefered_names.front();
        const Preset      &replacement   = filaments.preset(filaments.first_compatible_idx(PreferedFilamentProfileMatch(nullptr, prefered_name)));
        if (replacement.is_default) continue;

        BOOST_LOG_TRIVIAL(info) << "[default-preset] filament slot " << idx << ": replace \"" << slots[idx] << "\" with \"" << replacement.name << "\"";
        slots[idx] = replacement.name;
        changed    = true;
        settle_filament_slot_flow(bundle, idx);
    }
    return changed;
}

// Traces filament slots that hold the built-in default preset after a compatibility update, with the reason.
// Slots bypass PresetCollection::select_preset(), so the collection does not log them.
// Grep the log for "[default-preset]" to find these records.
void log_default_filament_slots(const PresetCollection &filaments, const std::vector<std::string> &previous_slots, const std::vector<std::string> &slots, const std::string &printer_name)
{
    for (size_t idx = 0; idx < slots.size(); ++idx) {
        const Preset *preset = filaments.find_preset(slots[idx], false);
        if (preset == nullptr || !preset->is_default) continue;

        const std::string previous = idx < previous_slots.size() ? previous_slots[idx] : std::string();
        const std::string reason   = previous == slots[idx] ?
            "kept, the default preset is always compatible so it is never replaced" :
            "picked to replace \"" + previous + "\", no compatible filament preset exists";
        BOOST_LOG_TRIVIAL(warning) << "[default-preset] filament slot " << idx << " holds the built-in default preset \"" << slots[idx]
                                   << "\" for printer \"" << printer_name << "\": " << reason;
    }
}

} // namespace

bool PresetBundle::replace_default_presets()
{
    const Preset &printer = printers.get_edited_preset();
    bool          changed = false;

    if (prints.get_edited_preset().is_default && printer.config.has("default_print_profile")) {
        prints.select_preset(prints.first_compatible_idx(PreferedPrintProfileMatch(nullptr, printer.config.opt_string("default_print_profile"))));
        // Refresh the filament compatibility against the new process before picking filaments.
        update_compatible(PresetSelectCompatibleType::Never);
        changed = !prints.get_edited_preset().is_default;
        BOOST_LOG_TRIVIAL(info) << "[default-preset] replace the default process with \"" << prints.get_edited_preset().name << "\" for printer \"" << printer.name << "\"";
    }

    const ConfigOptionStrings *prefered_filaments = printer.config.option<ConfigOptionStrings>("default_filament_profile");
    if (prefered_filaments == nullptr || prefered_filaments->values.empty()) return changed;

    if (replace_default_filament_slots(*this, prefered_filaments->values)) {
        if (filaments.get_edited_preset().is_default)
            filaments.select_preset_by_name(filament_presets.front(), true);
        changed = true;
    }
    return changed;
}

void PresetBundle::update_compatible(PresetSelectCompatibleType select_other_print_if_incompatible, PresetSelectCompatibleType select_other_filament_if_incompatible)
{
    const Preset					&printer_preset					    = this->printers.get_edited_preset();
	const PresetWithVendorProfile    printer_preset_with_vendor_profile = this->printers.get_preset_with_vendor_profile(printer_preset);

    // Matching by the layer height in addition.
    class PreferedFilamentsProfileMatch
    {
    public:
        PreferedFilamentsProfileMatch(const Preset *preset, const std::vector<std::string> &prefered_names) :
            m_prefered_alias(preset ? preset->alias : std::string()),
            m_prefered_filament_type(preset ? preset->config.opt_string("filament_type", 0) : std::string("PLA")), // BBS: default choose PLA
            m_prefered_names(prefered_names)
            {}

        int operator()(const Preset &preset) const
        {
            // Don't match any properties of the "-- default --" profile or the external profiles when switching printer profile.
            if (preset.is_default || preset.is_external)
                return 0;
            if (! m_prefered_alias.empty() && m_prefered_alias == preset.alias)
                // Matching an alias, always take this preset with priority.
                return std::numeric_limits<int>::max();
            int match_quality = (std::find(m_prefered_names.begin(), m_prefered_names.end(), preset.name) != m_prefered_names.end()) + 1;
            if (! m_prefered_filament_type.empty() && m_prefered_filament_type == preset.config.opt_string("filament_type", 0))
                match_quality *= 10;
            return match_quality;
        }

    private:
        const std::string               m_prefered_alias;
        const std::string               m_prefered_filament_type;
        const std::vector<std::string> &m_prefered_names;
    };

    BOOST_LOG_TRIVIAL(info) << boost::format("update_compatibility for all presets enter, select_other_print_if_incompatible %1%, select_other_filament_if_incompatible %2%")%(int)select_other_print_if_incompatible %(int)select_other_filament_if_incompatible;
	switch (printer_preset.printer_technology()) {
    case ptFFF:
    {
		assert(printer_preset.config.has("default_print_profile"));
		assert(printer_preset.config.has("default_filament_profile"));
        const std::vector<std::string> &prefered_filament_profiles = printer_preset.config.option<ConfigOptionStrings>("default_filament_profile")->values;
        this->prints.update_compatible(printer_preset_with_vendor_profile, nullptr, select_other_print_if_incompatible,
            PreferedPrintProfileMatch(this->prints.get_selected_idx() == size_t(-1) ? nullptr : &this->prints.get_edited_preset(), printer_preset.config.opt_string("default_print_profile")));
        const PresetWithVendorProfile   print_preset_with_vendor_profile = this->prints.get_edited_preset_with_vendor_profile();
        // Remember whether the filament profiles were compatible before updating the filament compatibility.
        std::vector<char> 				filament_preset_was_compatible(this->filament_presets.size(), false);
        const std::vector<std::string>  previous_filament_presets = this->filament_presets;
        for (size_t idx = 0; idx < this->filament_presets.size(); ++ idx) {
            Preset *preset = this->filaments.find_preset(this->filament_presets[idx], false);
            filament_preset_was_compatible[idx] = preset != nullptr && preset->is_compatible;
        }
        // First select a first compatible profile for the preset editor.
        BOOST_LOG_TRIVIAL(info) << boost::format("prefered filaments: size %1%, previous selected %2%") %prefered_filament_profiles.size() % this->filaments.get_selected_idx();
        if (this->filaments.get_selected_idx() != size_t(-1))
        {
            BOOST_LOG_TRIVIAL(info) << boost::format("previous selected filament： %1%") % this->filaments.get_edited_preset().name;
        }
        for (size_t idx = 0; idx < prefered_filament_profiles.size(); ++idx) {
            BOOST_LOG_TRIVIAL(info) << boost::format("prefered filament： %1%") % prefered_filament_profiles[idx];
        }
        // BBS multi-diameter: also treat filament presets bound to sibling nozzle-diameter variants
        // (selected via "set nozzle diameter") as compatible, so the user can keep using them while
        // running mixed-diameter printing. Reset afterwards to avoid leaking state into later passes.
        this->filaments.set_extra_compatible_printers(this->get_extra_compatible_printers_by_full_stat());
        // When fallback substitution is blocked, also stop this call from silently reselecting the
        // filaments collection's own edited/selected preset to some other compatible default -- that
        // reselection is invisible to filament_presets[] here, but downstream single-slot consumers
        // (Sidebar::update_presets's filament_cnt == 1 branch) read get_selected_preset_name() and
        // write it straight into filament_presets[0], bypassing the guard entirely.
        const PresetSelectCompatibleType filaments_select_other_if_incompatible =
            block_filament_fallback_substitution ? PresetSelectCompatibleType::Never : select_other_filament_if_incompatible;
        this->filaments.update_compatible(printer_preset_with_vendor_profile, &print_preset_with_vendor_profile, filaments_select_other_if_incompatible,
            PreferedFilamentsProfileMatch(this->filaments.get_selected_idx() == size_t(-1) ? nullptr : &this->filaments.get_edited_preset(), prefered_filament_profiles));
        this->filaments.clear_extra_compatible_printers();
        if (select_other_filament_if_incompatible != PresetSelectCompatibleType::Never && !block_filament_fallback_substitution) {
            // Verify validity of the current filament presets.
            const std::string prefered_filament_profile = prefered_filament_profiles.empty() ? std::string() : prefered_filament_profiles.front();
            if (this->filament_presets.size() == 1) {
                // The compatible profile should have been already selected for the preset editor. Just use it.
            	if (select_other_filament_if_incompatible == PresetSelectCompatibleType::Always || filament_preset_was_compatible.front())
                	this->filament_presets.front() = this->filaments.get_edited_preset().name;
            } else {
                for (size_t idx = 0; idx < this->filament_presets.size(); ++ idx) {
                    std::string &filament_name = this->filament_presets[idx];
                    Preset      *preset = this->filaments.find_preset(filament_name, false);
                    if (preset == nullptr || (! preset->is_compatible && (select_other_filament_if_incompatible == PresetSelectCompatibleType::Always || filament_preset_was_compatible[idx])))
                        // Pick a compatible profile. If there are prefered_filament_profiles, use them.
                        filament_name = this->filaments.first_compatible(
                            PreferedFilamentProfileMatch(preset,
                                (idx < prefered_filament_profiles.size()) ? prefered_filament_profiles[idx] : prefered_filament_profile)).name;
                }
            }
        }
        log_default_filament_slots(this->filaments, previous_filament_presets, this->filament_presets, printer_preset.name);
		break;
    }
    case ptSLA:
    {
		assert(printer_preset.config.has("default_sla_print_profile"));
		assert(printer_preset.config.has("default_sla_material_profile"));
		this->sla_prints.update_compatible(printer_preset_with_vendor_profile, nullptr, select_other_print_if_incompatible,
            PreferedPrintProfileMatch(this->sla_prints.get_selected_idx() == size_t(-1) ? nullptr : &this->sla_prints.get_edited_preset(), printer_preset.config.opt_string("default_sla_print_profile")));
        const PresetWithVendorProfile sla_print_preset_with_vendor_profile = this->sla_prints.get_edited_preset_with_vendor_profile();
		this->sla_materials.update_compatible(printer_preset_with_vendor_profile, &sla_print_preset_with_vendor_profile, select_other_filament_if_incompatible,
            PreferedProfileMatch(this->sla_materials.get_selected_idx() == size_t(-1) ? std::string() : this->sla_materials.get_edited_preset().alias, printer_preset.config.opt_string("default_sla_material_profile")));
		break;
	}
    default: break;
    }

    BOOST_LOG_TRIVIAL(info) << boost::format("update_compatibility for all presets exit");
}

//BBS: add a API to dump current configbundle as default configbundle
/*void PresetBundle::export_current_configbundle(const std::string &path)
{
    boost::nowide::ofstream c;
    c.open(path, std::ios::out | std::ios::trunc);

    c << "# generate the configbundle for BBL" << std::endl;

    const Preset& print_selected_preset = this->prints.get_selected_preset();
    const Preset* system_print = NULL;
    const Preset& filament_selected_preset = this->filaments.get_selected_preset();
    const Preset* system_filament = NULL;
    const Preset& printer_selected_preset = this->printers.get_selected_preset();
    const Preset* system_printer = NULL;

    // Export the print
    std::string print_bbl = "@BBL-3DP";
    size_t pos = print_selected_preset.name.find(print_bbl);
    if (std::string::npos != pos)
    {
        std::string system_print_name = print_selected_preset.name.substr(0, pos + print_bbl.size());
        system_print = this->prints.find_preset(system_print_name);
        if (system_print)
        {
            c << std::endl << "[" << this->prints.section_name() << ":" << system_print->name << "]" << std::endl;
            for (const std::string &opt_key : print_selected_preset.config.keys())
            {
                if (!opt_key.compare("inherits"))
                    continue;
                const ConfigOption *option1 = system_print->config.option(opt_key);
                const ConfigOption *option2 = print_selected_preset.config.option(opt_key);
                if (option1 && option2 && (*option1 != *option2))
                    c << opt_key << " = " << print_selected_preset.config.opt_serialize(opt_key) << std::endl;
            }
            c << std::endl;
        }
    }

    // Export the filament
    std::string filament_bbl = "@BBL-3DP";
    pos = filament_selected_preset.name.find(filament_bbl);
    if (std::string::npos != pos)
    {
        std::string system_filamant_name = filament_selected_preset.name.substr(0, pos + filament_bbl.size());
        system_filament = this->filaments.find_preset(system_filamant_name);
        if (system_filament)
        {
            c << std::endl << "[" << this->filaments.section_name() << ":" << system_filament->name << "]" << std::endl;
            for (const std::string &opt_key : filament_selected_preset.config.keys())
            {
                if (!opt_key.compare("inherits"))
                    continue;
                const ConfigOption *option1 = system_filament->config.option(opt_key);
                const ConfigOption *option2 = filament_selected_preset.config.option(opt_key);
                if (option1 && option2 && (*option1 != *option2))
                    c << opt_key << " = " << filament_selected_preset.config.opt_serialize(opt_key) << std::endl;
            }
            c << std::endl;
        }
    }

	// Export the printer
    std::string printer_bbl = "BBL-3DP-001";
    pos = printer_selected_preset.name.find(printer_bbl);
    if (std::string::npos != pos)
    {
        std::string system_printer_name = printer_selected_preset.name.substr(0, pos + printer_bbl.size());
        system_printer = this->printers.find_preset(system_printer_name);
        if (system_printer)
        {
            c << std::endl << "[" << this->printers.section_name() << ":" << system_printer->name << "]" << std::endl;
            for (const std::string &opt_key : printer_selected_preset.config.keys())
            {
                c << opt_key << " = " << printer_selected_preset.config.opt_serialize(opt_key) << std::endl;
            }
            c << std::endl;
        }
    }

    c.close();
}*/


//void PresetBundle::export_configbundle(const std::string &path, bool export_system_settings, bool export_physical_printers/* = false*/)
//{
//    boost::nowide::ofstream c;
//    c.open(path, std::ios::out | std::ios::trunc);
//
//    // Put a comment at the first line including the time stamp and Slic3r version.
//    c << "# " << Slic3r::header_slic3r_generated() << std::endl;
//
//    // Export the print, filament and printer profiles.
//
//	for (const PresetCollection *presets : {
//		(const PresetCollection*)&this->prints, (const PresetCollection*)&this->filaments,
//		(const PresetCollection*)&this->sla_prints, (const PresetCollection*)&this->sla_materials,
//		(const PresetCollection*)&this->printers }) {
//        for (const Preset &preset : (*presets)()) {
//            //BBS: add project embedded preset logic and refine is_external
//            if (preset.is_default || preset.is_project_embedded || (preset.is_system && ! export_system_settings))
//            //if (preset.is_default || preset.is_external || (preset.is_system && ! export_system_settings))
//                // Only export the common presets, not external files or the default preset.
//                continue;
//            c << std::endl << "[" << presets->section_name() << ":" << preset.name << "]" << std::endl;
//            for (const std::string &opt_key : preset.config.keys())
//                c << opt_key << " = " << preset.config.opt_serialize(opt_key) << std::endl;
//        }
//    }
//
//    if (export_physical_printers) {
//        for (const PhysicalPrinter& ph_printer : this->physical_printers) {
//            c << std::endl << "[physical_printer:" << ph_printer.name << "]" << std::endl;
//            for (const std::string& opt_key : ph_printer.config.keys())
//                c << opt_key << " = " << ph_printer.config.opt_serialize(opt_key) << std::endl;
//        }
//    }
//
//    // Export the names of the active presets.
//    c << std::endl << "[presets]" << std::endl;
//    c << "print = " << this->prints.get_selected_preset_name() << std::endl;
//    c << "sla_print = " << this->sla_prints.get_selected_preset_name() << std::endl;
//    c << "sla_material = " << this->sla_materials.get_selected_preset_name() << std::endl;
//    c << "printer = " << this->printers.get_selected_preset_name() << std::endl;
//    for (size_t i = 0; i < this->filament_presets.size(); ++ i) {
//        char suffix[64];
//        if (i > 0)
//            sprintf(suffix, "_%d", (int)i);
//        else
//            suffix[0] = 0;
//        c << "filament" << suffix << " = " << this->filament_presets[i] << std::endl;
//    }
//
//    if (export_physical_printers && this->physical_printers.get_selected_idx() >= 0)
//        c << "physical_printer = " << this->physical_printers.get_selected_printer_name() << std::endl;
//#if 0
//    // Export the following setting values from the provided setting repository.
//    static const char *settings_keys[] = { "autocenter" };
//    c << "[settings]" << std::endl;
//    for (size_t i = 0; i < sizeof(settings_keys) / sizeof(settings_keys[0]); ++ i)
//        c << settings_keys[i] << " = " << settings.serialize(settings_keys[i]) << std::endl;
//#endif
//
//    c.close();
//}

//BBS: add export system preset functions
/*void PresetBundle::export_system_configs(const std::string &path)
{
    // Export the print, filament and printer profiles.
    for (const PresetCollection *presets : {
        (const PresetCollection*)&this->prints, (const PresetCollection*)&this->filaments,
        (const PresetCollection*)&this->sla_prints, (const PresetCollection*)&this->sla_materials,
        (const PresetCollection*)&this->printers }) {
        for (const Preset &preset : (*presets)()) {
            if (preset.is_system)
            {
                // Only export the system presets
                boost::nowide::ofstream c;
                std::string file_path = path + std::string("\\") + preset.name;

                c.open(file_path, std::ios::out | std::ios::trunc);

                // Put a comment at the first line including the time stamp and Slic3r version.
                c << "# " << Slic3r::header_slic3r_generated() << std::endl;
                //c << std::endl << "[" << presets->section_name() << ":" << preset.name << "]" << std::endl;
                for (const std::string &opt_key : preset.config.keys())
                    c << opt_key << " = " << preset.config.opt_serialize(opt_key) << std::endl;

                c.close();
            }
        }
    }
}*/

std::vector<std::string> PresetBundle::export_current_configs(const std::string &                     path,
                                                              std::function<int(std::string const &)> override_confirm,
                                                              bool                                    include_modify,
                                                              bool                                    export_system_settings)
{
    const Preset &print_preset    = include_modify ? prints.get_edited_preset() : prints.get_selected_preset();
    const Preset &printer_preset  = include_modify ? printers.get_edited_preset() : printers.get_selected_preset();
    std::set<Preset const *> presets { &print_preset, &printer_preset };
    for (auto &f : filament_presets) {
        auto filament_preset = filaments.find_preset(f, include_modify);
        if (filament_preset) presets.insert(filament_preset);
    }

    int overwrite = 0;
    std::vector<std::string> result;
    for (auto preset : presets) {
        if ((preset->is_system  && !export_system_settings) || preset->is_default)
            continue;
        std::string file = path + "/" + preset->name + ".json";
        if (overwrite == 0) overwrite = 1;
        if (boost::filesystem::exists(file) && overwrite < 2) {
            overwrite = override_confirm(preset->name);
        }
        if (overwrite == 0 || overwrite == 2)
            continue;
        preset->config.save_to_json(file, preset->name, "", preset->version.to_string());
        result.push_back(file);
    }
    return result;
}

// Set the filament preset name. As the name could come from the UI selection box,
// an optional "(modified)" suffix will be removed from the filament name.
void PresetBundle::set_filament_preset(size_t idx, const std::string &name)
{
    if (idx >= filament_presets.size()) {
        BOOST_LOG_TRIVIAL(warning) << boost::format("Warning: set_filament_preset out of range %1% - %2%") % idx % filament_presets.size();
        return;
    }
    filament_presets[idx] = Preset::remove_suffix_modified(name);

    auto *selected = project_config.option<ConfigOptionStrings>("filament_diameter_map");
    if (selected == nullptr)
        return;
    selected->values.resize(filament_presets.size());

    if (filaments.find_preset(filament_presets[idx], false) == nullptr)
        selected->values[idx].clear();
    // Picking a different material for this slot is not a diameter choice, so the slot's diameter
    // stays exactly as it was -- clearing it here used to make validate_filament_nozzle_diameter_selections()
    // fall back to the smallest installed diameter (get_filament_nozzle_diameter_options() sorts
    // ascending), silently moving the slot off a larger nozzle it was perfectly happy on (e.g. a
    // TPU285 filament on 0.4 swapped for a PLA that also prints at 0.4, landing on 0.2 instead because
    // that happened to be the smallest nozzle installed). Leaving the stored value alone instead lets
    // validate_filament_nozzle_diameter_selections() flag the slot red if the new material genuinely
    // cannot use it, same as it already does for any other stored-but-unsupported diameter.
}

void PresetBundle::set_default_suppressed(bool default_suppressed)
{
    prints.set_default_suppressed(default_suppressed);
    filaments.set_default_suppressed(default_suppressed);
    sla_prints.set_default_suppressed(default_suppressed);
    sla_materials.set_default_suppressed(default_suppressed);
    printers.set_default_suppressed(default_suppressed);
}

} // namespace Slic3r
