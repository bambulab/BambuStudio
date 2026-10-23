#ifndef slic3r_PresetBundle_hpp_
#define slic3r_PresetBundle_hpp_

#include "Preset.hpp"
#include "AppConfig.hpp"
#include "NozzleInventory.hpp"
#include "enum_bitmask.hpp"

#include <memory>
#include <unordered_map>
#include <optional>
#include <utility>
#include <map>
#include <set>
#include <functional>
#include <boost/filesystem/path.hpp>

#define DEFAULT_USER_FOLDER_NAME "default"
#define BUNDLE_STRUCTURE_JSON_NAME "bundle_structure.json"

#define VALIDATE_PRESETS_SUCCESS                0
#define VALIDATE_PRESETS_PRINTER_NOT_FOUND      1
#define VALIDATE_PRESETS_FILAMENTS_NOT_FOUND    2
#define VALIDATE_PRESETS_MODIFIED_GCODES        3


namespace Slic3r {

struct AMSMapInfo
{
    /*for new ams mapping*/ // from struct FilamentInfo
    std::string ams_id{""};
    std::string slot_id{""};
    std::string nozzle_diameter{""};
    // Flow of the nozzle this tray feeds, as the machine reports it. Carried alongside the diameter
    // because the two together identify a nozzle: a rack can hold the same diameter in two flows, so
    // syncing only the diameter leaves a slot on whichever flow it happened to have. Empty when the
    // machine said nothing about it -- nvtStandard is a real value and cannot stand in for "unknown".
    std::optional<NozzleVolumeType> nozzle_volume_type;
};
struct AMSComboInfo
{
    std::vector<std::string>              ams_filament_colors;
    std::vector<std::vector<std::string>> ams_multi_color_filment;
    std::vector<std::string>              ams_filament_presets;
    std::vector<std::string>              ams_names;
    void  clear() {
        ams_filament_colors.clear();
        ams_multi_color_filment.clear();
        ams_filament_presets.clear();
        ams_names.clear();
    }
    bool empty() {
        return ams_names.empty();
    }
};
struct MergeFilamentInfo {
    std::vector<std::vector<int>> merges;
    bool  is_empty() { return merges.empty();}
};


struct FilamentBaseInfo
{
    std::string filament_name;
    std::string filament_id;
    std::string filament_type;
    std::string vendor;
    int nozzle_temp_range_low{ 220 };
    int nozzle_temp_range_high{ 220 };
    int temperature_vitrification = INT_MAX;
    bool is_support{ false };
    bool is_system{ true };
    int  filament_printable = 3;
    std::string setting_id = "";

    // extruder_id is 0-based (0 = first extruder)
    int get_extruder_compatibility(int extruder_id) const {
        constexpr int bits_per_extruder  = 3;
        constexpr int extruder_mask      = (1 << bits_per_extruder) - 1; // 0x7
        constexpr int max_extruder_count = 32 / bits_per_extruder;       // 10

        if (extruder_id < 0 || extruder_id >= max_extruder_count)
            return 0;
        return (m_filament_extruder_compatibility >> (bits_per_extruder * extruder_id)) & extruder_mask;
    }

    void set_filament_extruder_compatibility(int value) { m_filament_extruder_compatibility = value; }
    int  get_filament_extruder_compatibility() const     { return m_filament_extruder_compatibility; }

private:
    int  m_filament_extruder_compatibility = 0;
};

/// Nozzle counts per extruder and volume type; the value type behind the legacy extruder_nozzle_stats key.
struct ExtruderNozzleStat
{
public:
    ExtruderNozzleStat() = default;
    explicit ExtruderNozzleStat(const std::vector<std::map<NozzleVolumeType, int>>& nozzle_counts) : extruder_nozzle_counts(nozzle_counts) {}
    void on_volume_type_switch(int extruder_id, NozzleVolumeType type);
    void on_printer_model_change_cli(const std::vector<int> &nozzle_volume_type, const std::vector<int> &max_nozzle_count);
    void set_extruder_nozzle_count(int extruder_id, NozzleVolumeType type, int count, bool clear);
    int get_extruder_nozzle_count(int extruder_id, std::optional<NozzleVolumeType> volume_type = std::nullopt) const;

    const std::vector<std::map<NozzleVolumeType, int>> get_raw_stat() const { return extruder_nozzle_counts; }
    void set_raw_stat(const std::vector<std::map<NozzleVolumeType, int>>& data) { extruder_nozzle_counts = data; }

private:
    std::vector<std::map<NozzleVolumeType,int>> extruder_nozzle_counts;
};

struct ExtruderFullStat
{
public:
    ExtruderFullStat() = default;
    ExtruderFullStat(const std::vector<std::vector<std::pair<std::string, std::string>>> &stats) : extruder_nozzle_stats(stats) {}
    const std::vector<std::vector<std::pair<std::string, std::string>>> &get_raw_stat() const { return extruder_nozzle_stats; }
    void set_raw_stat(const std::vector<std::vector<std::pair<std::string, std::string>>> &data) { extruder_nozzle_stats = data; }
    int get_extruder_count() const { return extruder_nozzle_stats.size(); }
    int get_extruder_nozzle_count(size_t extruder_id) const
    {
        if (extruder_id >= extruder_nozzle_stats.size()) return 0;
        return extruder_nozzle_stats[extruder_id].size();
    }
    const std::vector<std::pair<std::string, std::string>> get_extruder_stats(size_t extruder_id) const
    {
        if (extruder_id >= extruder_nozzle_stats.size()) return {};
        return extruder_nozzle_stats[extruder_id];
    }
    void resize(size_t extruder_count) { extruder_nozzle_stats.resize(extruder_count); }
    void set_extruder_stats(size_t extruder_id, const std::vector<std::pair<std::string, std::string>> &stats)
    {
        if (extruder_id >= extruder_nozzle_stats.size()) extruder_nozzle_stats.resize(extruder_id + 1);
        extruder_nozzle_stats[extruder_id] = stats;
    }
    void clear_extruder(size_t extruder_id)
    {
        if (extruder_id < extruder_nozzle_stats.size()) extruder_nozzle_stats[extruder_id].clear();
    }
    void clear() { extruder_nozzle_stats.clear(); }
    bool empty() const { return extruder_nozzle_stats.empty(); }

    // Save every extruder's full stats.
    // Outer vector: extruder index.
    // Inner vector: nozzle index.
    // Pair: <diameter, volume>.
private:
    std::vector<std::vector<std::pair<std::string, std::string>>> extruder_nozzle_stats;
};

// GUI(PresetBundle)与 CLI(BambuStudio.cpp)共用的逐槽喷嘴对账:两端用同一套判据和重建逻辑,
// 从权威 ExtruderNozzleStat 生成逐槽四字段(count / volume_type / diameter / slot)。
ExtruderFullStat build_extruder_full_stat_from_nozzle_stat(const ExtruderNozzleStat &nozzle_stat,
                                                           const std::vector<int>   &extruder_max_nozzle_count,
                                                           const std::string        &diameter_str);
void sync_per_slot_nozzle_options_from_full_stat(DynamicPrintConfig &config, const ExtruderFullStat &full_stat);
bool per_slot_nozzle_options_consistent(const DynamicPrintConfig &config);

/**
 * \brief Count one extruder's physical nozzle slots by their preset default volume type
 *
 * \param printer_config  printer config holding default_nozzle_volume_type
 * \param extruder_id     0-based extruder id
 * \param slot_count      number of physical nozzle slots of the extruder
 * \return volume type -> slot count; never contains Hybrid
 */
std::map<NozzleVolumeType, int> count_default_nozzle_volume_types(const ConfigBase &printer_config, int extruder_id, int slot_count);

/// Untranslated name of a volume type, as BambuStudio.conf and the per-slot snapshot store it ("High Flow").
std::string GetNozzleVolumeTypeStr(const NozzleVolumeType &type);

// Ordered AMS units selected for one logical extruder. A non-empty string is a device AMS id;
// an empty string is a virtual unit. This runtime identity layer complements the persisted counts.
struct ExtruderAmsSelection
{
    bool                     initialized{false};
    std::string              device_id;
    std::vector<std::string> ams4;
    std::vector<std::string> ams1;
};

// Bundle of Print + Filament + Printer presets.
class PresetBundle
{
public:
    static DynamicPrintConfig construct_full_config(Preset                         &in_printer_preset,
                                                    Preset                         &in_print_preset,
                                                    const DynamicPrintConfig       &project_config,
                                                    std::vector<Preset>            &in_filament_presets,
                                                    bool                            apply_extruder,
                                                    std::optional<std::vector<int>> filament_maps_new,
                                                    std::optional<std::vector<int>> filament_volume_maps_new);
    PresetBundle();
    PresetBundle(const PresetBundle &rhs);
    PresetBundle& operator=(const PresetBundle &rhs);

    // Remove all the presets but the "-- default --".
    // Optionally remove all the files referenced by the presets from the user profile directory.
    void            reset(bool delete_files);

    void            setup_directories();
    void            copy_files(const std::string& from);

    struct PresetPreferences {
        std::string printer_model_id;// name of a preferred printer model
        std::string printer_variant; // name of a preferred printer variant
        std::string filament;        // name of a preferred filament preset
        std::string sla_material;    // name of a preferred sla_material preset
    };

    // Load ini files of all types (print, filament, printer) from Slic3r::data_dir() / presets.
    // Load selections (current print, current filaments, current printer) from config.ini
    // select preferred presets, if any exist
    std::pair<PresetsConfigSubstitutions, std::string> load_presets(AppConfig &config, ForwardCompatibilitySubstitutionRule rule,
                                            const PresetPreferences& preferred_selection = PresetPreferences());

    // Load selections (current print, current filaments, current printer) from config.ini
    // This is done just once on application start up.
    //BBS: change it to public
    void     load_selections(AppConfig &config, const PresetPreferences& preferred_selection = PresetPreferences());

    // BBS Load user presets
    PresetsConfigSubstitutions load_user_presets(std::string user, ForwardCompatibilitySubstitutionRule rule);
    PresetsConfigSubstitutions load_user_presets(AppConfig &config, std::map<std::string, std::map<std::string, std::string>>& my_presets, ForwardCompatibilitySubstitutionRule rule);
    PresetsConfigSubstitutions import_presets(std::vector<std::string> &files, std::function<int(std::string const &)> override_confirm, ForwardCompatibilitySubstitutionRule rule);
    bool                       import_json_presets(PresetsConfigSubstitutions &            substitutions,
                                                   std::string &                           file,
                                                   std::function<int(std::string const &)> override_confirm,
                                                   ForwardCompatibilitySubstitutionRule    rule,
                                                   int &                                   overwrite,
                                                   std::vector<std::string> &              result);
    void save_user_presets(AppConfig& config, std::vector<std::string>& need_to_delete_list);
    void remove_users_preset(AppConfig &config, std::map<std::string, std::map<std::string, std::string>> * my_presets = nullptr);
    void update_user_presets_directory(const std::string preset_folder);
    void remove_user_presets_directory(const std::string preset_folder);
    void update_system_preset_setting_ids(std::map<std::string, std::map<std::string, std::string>>& system_presets);

    std::vector<std::vector<std::vector<float>>> get_full_flush_matrix(bool with_multiplier = true) const;

    //BBS: add API to get previous machine
    int validate_presets(const std::string &file_name, DynamicPrintConfig& config, std::set<std::string>& different_gcodes);

    //BBS: add function to generate differed preset for save
    //the pointer should be freed by the caller
    Preset* get_preset_differed_for_save(Preset& preset);
    int get_differed_values_to_update(Preset& preset, std::map<std::string, std::string>& key_values);


    //BBS: get vendor's current version
    Semver get_vendor_profile_version(std::string vendor_name);

    std::optional<FilamentBaseInfo> get_filament_by_filament_id(const std::string& filament_id, const std::string& printer_name = std::string(), bool only_system = false) const;

    //BBS: project embedded preset logic
    PresetsConfigSubstitutions load_project_embedded_presets(std::vector<Preset*> project_presets, ForwardCompatibilitySubstitutionRule substitution_rule);
    std::vector<Preset*> get_current_project_embedded_presets();
    void reset_project_embedded_presets();

    //BBS: find printer model
    std::string get_texture_for_printer_model(std::string model_name);
    std::string get_stl_model_for_printer_model(std::string model_name);
    std::string get_hotend_model_for_printer_model(std::string model_name);

    // Export selections (current print, current filaments, current printer) into config.ini
    void            export_selections(AppConfig &config);

    // BBS
    void            set_num_filaments(unsigned int n, std::string new_col = "");
    void         update_num_filaments(unsigned int to_del_flament_id);

    bool is_mixed_filament(size_t idx) const;
    std::vector<size_t> physical_filament_config_indices() const;
    size_t physical_filament_count() const;
    size_t mixed_filament_count() const;

    void         get_ams_cobox_infos(AMSComboInfo &combox_info,bool skip_ext = false);
    unsigned int sync_ams_list(std::vector<std::pair<DynamicPrintConfig *, std::string>> &unknowns,
                               bool                                                       use_map,
                               std::map<int, AMSMapInfo> &                                maps,
                               bool                                                       enable_append,
                               MergeFilamentInfo &                                        merge_info,
                               bool                                                       skip_ext = false);
    //BBS: check whether this is the only edited filament
    bool is_the_only_edited_filament(unsigned int filament_index);

    std::vector<int> get_used_tpu_filaments(const std::vector<int> &used_filaments);
    void set_calibrate_printer(std::string name);

    std::vector<std::vector<DynamicPrintConfig>> get_extruder_filament_info() const;

    std::vector<NozzleVolumeType> get_printer_nozzle_volume_list();

    std::set<std::string> get_printer_names_by_printer_type_and_nozzle(const std::string &printer_type, std::string nozzle_diameter_str, bool system_only = true);
    bool                  check_filament_temp_equation_by_printer_type_and_nozzle_for_mas_tray(const std::string &printer_type,
                                                                                               std::string &      nozzle_diameter_str,
                                                                                               std::string &      setting_id,
                                                                                               std::string &      tag_uid,
                                                                                               std::string &      nozzle_temp_min,
                                                                                               std::string &      nozzle_temp_max,
                                                                                               std::string &      preset_setting_id);
    Preset *                    get_similar_printer_preset(std::string printer_model, std::string printer_variant);
    // BBS multi-diameter: printer preset names of the current printer model whose nozzle-diameter variant
    // matches any diameter selected via "set nozzle diameter" (stored in nozzle_inventory). Returns an
    // empty list when no multi-diameter selection exists, keeping single-diameter behavior unchanged.
    std::vector<std::string>    get_extra_compatible_printers_by_full_stat();

    // Multi-diameter grouping restriction: for each loaded filament, the ';'-joined list of nozzle
    // diameters its preset natively supports (compatible_printers x printer_variant). This uses the
    // filament preset's own compatible_printers only and never the broadened selectable set.
    std::vector<std::string>    compute_filament_supported_nozzle_diameters(size_t num_filaments) const;
    // Returns the configured nozzle diameters supported by the logical filament in this slot.
    std::vector<std::string>    get_filament_nozzle_diameter_options(size_t filament_index) const;
    bool                        is_filament_nozzle_diameter_valid(size_t filament_index, const std::string &diameter) const;
    // (diameter, flow) of every slot that actually carries a nozzle. Built from the same per-slot
    // snapshot the diameter option list above comes from, so the filament panel's diameter list and
    // its per-diameter flow list cannot disagree. The printer preset's extruder_variant_list only
    // says what the model could take and must not be used to offer flow types on its own.
    std::set<std::pair<std::string, NozzleVolumeType>> get_installed_nozzle_variants() const;
    // Updates the selected diameter and switches to a matching system preset when the current preset cannot serve it.
    // `diameter_picked_by_user` keeps a diameter the machine does not currently carry: the request then
    // only has to be one the filament has a preset for. Picking a tray by hand is such a case -- the
    // tray reports the nozzle it was loaded on, which may since have been retyped in the nozzle
    // settings -- and refusing it leaves the slot silently on its old nozzle, as if the pick had not
    // happened. The slot is flagged afterwards instead, which is what tells the user to add that nozzle.
    bool                        set_filament_nozzle_diameter(size_t filament_index, const std::string &diameter,
                                                           std::string *selected_preset_name = nullptr,
                                                           bool diameter_picked_by_user = false);
    // Nozzle diameter the machine reports for the tray this slot's filament came from, or an empty
    // string when no tray matches. Matched on filament_id + colour, the same pair the sync badge
    // uses to decide whether a slot still corresponds to a tray.
    std::string                 tray_diameter_for_filament_slot(size_t filament_index) const;
    // First single-diameter -> multi-diameter toggle: give every physical slot a diameter and the
    // first printable flow for it. Prefers what the machine says the slot's tray is mounted on,
    // falling back to `fallback_diameter` (the printer_variant in effect before the switch) when no
    // tray matches or its diameter is not one this filament has a preset for. Slots that cannot use
    // the result keep it anyway so validation can flag them instead of rewriting them to the
    // smallest remaining option.
    void                        seed_filament_nozzles_for_multi_diameter(const std::string &fallback_diameter);
    // Writes printer_nozzle_diameter_mode on the edited printer preset right before it is saved, so a
    // user/project printer remembers which multi-diameter mode it was saved in. Printers that cannot do
    // multi-diameter get an empty value.
    void                        stamp_printer_nozzle_diameter_mode();
    // Whether `preset` is a user/project printer preset that belongs to multi-diameter mode.
    bool                        is_multi_nozzle_user_printer_preset(const Preset &preset) const;
    // Resizes selections and fills missing or invalid values with a deterministic supported default.
    // Device mappings, when supplied, take precedence and bind a filament to its physical slot diameter.
    // `device_maps_are_user_chosen` says whether those mappings came from the matching dialog, where
    // the user picked each pairing by hand, rather than from the positional merge an overwrite sync
    // does on its own. A pairing the user chose is applied even when it does not fit, and the slot is
    // flagged; one the program chose is left alone instead, since nobody asked for it.
    void                        validate_filament_nozzle_diameter_selections(
                                    const std::map<int, AMSMapInfo> *device_maps = nullptr,
                                    bool device_maps_are_user_chosen = false);
    // When true, update_compatible() and validate_filament_nozzle_diameter_selections() skip their
    // silent fallback substitution of incompatible filament presets, recording the affected slots into
    // incompatible_nozzle_diameter_slots instead of replacing them. Used by the single-diameter-mode
    // nozzle-diameter switch entry points so the user's original filament selection is preserved.
    bool                        block_filament_fallback_substitution = false;
    std::vector<size_t>         incompatible_nozzle_diameter_slots;

    // A tray whose material the current printer has no preset for. Sync falls back the same way the
    // AMS dropdown does -- to a "Generic <type>" of the same material class -- and records it here so
    // the user is told what was swapped for what, rather than finding a material they did not pick.
    struct AmsSyncSubstitution
    {
        size_t      slot = 0;
        std::string tray;    // the tray it came from, labelled as the machine reports it
        std::string wanted;  // the material that tray holds, by its display name
        std::string applied; // what went into the slot; empty when nothing could be applied at all
    };
    std::vector<AmsSyncSubstitution> ams_sync_substitutions;

    /**
     * \brief Set the ams count for extruder \p index
     *
     * \param index logical index of extruer
     * \param ams4  count of the 4-slot ams
     * \param ams1  count of the 1-slot ams
     */
    void set_ams_count(int index, int ams4, int ams1);

    /**
     * \brief get the ams count for extruder \p index, <4-slot, 1-slot>
     *
     * \param index logical index of extruer
     */
    std::pair<int, int> get_ams_count(int index);
    void initialize_ams_selection(int index, const std::string &device_id,
                                  const std::vector<std::string> &ams4_ids, const std::vector<std::string> &ams1_ids);
    void resize_ams_selection(int index, const std::string &device_id,
                              const std::vector<std::string> &ams4_ids, const std::vector<std::string> &ams1_ids,
                              int ams4, int ams1);
    void replace_ams_selection(int index, const std::string &device_id,
                               const std::vector<std::string> &ams4_ids, const std::vector<std::string> &ams1_ids);
    ExtruderAmsSelection *get_ams_selection(int index);

    PresetCollection            prints;
    PresetCollection            sla_prints;
    PresetCollection            filaments;
    PresetCollection            sla_materials;
	PresetCollection& 			materials(PrinterTechnology pt)       { return pt == ptFFF ? this->filaments : this->sla_materials; }
	const PresetCollection& 	materials(PrinterTechnology pt) const { return pt == ptFFF ? this->filaments : this->sla_materials; }
    PrinterPresetCollection     printers;
    PhysicalPrinterCollection   physical_printers;
    // Filament preset names for a multi-extruder or multi-material print.
    // extruders.size() should be the same as printers.get_edited_preset().config.nozzle_diameter.size()
    std::vector<std::string>    filament_presets;
    // BBS: ams
    std::map<int, DynamicPrintConfig> filament_ams_list;
    std::vector<std::vector<std::string>> ams_multi_color_filment;

    std::vector<std::map<int, int>> extruder_ams_counts;
    std::vector<ExtruderAmsSelection> extruder_ams_selections;
    // Calibrate
    Preset const * calibrate_printer = nullptr;
    std::set<Preset const *> calibrate_filaments;

    // The project configuration values are kept separated from the print/filament/printer preset,
    // they are being serialized / deserialized from / to the .amf, .3mf, .config, .gcode,
    // and they are being used by slicing core.
    DynamicPrintConfig          project_config;
    /// Nozzles of the selected printer; writes its keys into project_config, so it is declared after it.
    NozzleInventory             nozzle_inventory{project_config};

    // There will be an entry for each system profile loaded,
    // and the system profiles will point to the VendorProfile instances owned by PresetBundle::vendors.
    VendorMap                   vendors;

    struct ObsoletePresets {
        std::vector<std::string> prints;
        std::vector<std::string> sla_prints;
        std::vector<std::string> filaments;
        std::vector<std::string> sla_materials;
        std::vector<std::string> printers;
    };
    ObsoletePresets             obsolete_presets;

    bool                        has_defauls_only() const
        { return prints.has_defaults_only() && filaments.has_defaults_only() && printers.has_defaults_only(); }

    DynamicPrintConfig          full_config(bool apply_extruder = true, std::optional<std::vector<int>>filament_maps = std::nullopt, std::optional<std::vector<int>> filament_volume_maps = std::nullopt) const;
    // full_config() with the some "useless" config removed.
    DynamicPrintConfig          full_config_secure(std::optional<std::vector<int>>filament_maps = std::nullopt) const;

    //BBS: add some functions for multiple extruders
    int get_printer_extruder_count() const;
    bool support_different_extruders();
    std::vector<int> get_default_nozzle_volume_types_for_filaments(std::vector<int>& f_maps);

    // Load user configuration and store it into the user profiles.
    // This method is called by the configuration wizard.
    void                        load_config_from_wizard(const std::string &name, DynamicPrintConfig config, Semver file_version)
        { this->load_config_file_config(name, false, std::move(config), file_version, true); }

    // Load configuration that comes from a model file containing configuration, such as 3MF et al.
    // This method is called by the Plater.
    void                        load_config_model(const std::string &name, DynamicPrintConfig config, Semver file_version = Semver())
        { this->load_config_file_config(name, true, std::move(config), file_version); }

    // Load an external config file containing the print, filament and printer presets.
    // Instead of a config file, a G-code may be loaded containing the full set of parameters.
    // In the future the configuration will likely be read from an AMF file as well.
    // If the file is loaded successfully, its print / filament / printer profiles will be activated.
    ConfigSubstitutions         load_config_file(const std::string &path, ForwardCompatibilitySubstitutionRule compatibility_rule);

    // Load a config bundle file, into presets and store the loaded presets into separate files
    // of the local configuration directory.
    // Load settings into the provided settings instance.
    // Activate the presets stored in the config bundle.
    // Returns the number of presets loaded successfully.
    enum LoadConfigBundleAttribute {
        // Save the profiles, which have been loaded.
        SaveImported,
        // Delete all old config profiles before loading.
        ResetUserProfile,
        // Load a system config bundle.
        LoadSystem,
        LoadVendorOnly,
        LoadFilamentOnly,
    };
    using LoadConfigBundleAttributes = enum_bitmask<LoadConfigBundleAttribute>;
    // Load the config bundle based on the flags.
    // Don't do any config substitutions when loading a system profile, perform and report substitutions otherwise.
    /*std::pair<PresetsConfigSubstitutions, size_t> load_configbundle(
        const std::string &path, LoadConfigBundleAttributes flags, ForwardCompatibilitySubstitutionRule compatibility_rule);*/
    //BBS: add json related logic
    std::pair<PresetsConfigSubstitutions, size_t> load_vendor_configs_from_json(
        const std::string &path, const std::string &vendor_name, LoadConfigBundleAttributes flags, ForwardCompatibilitySubstitutionRule compatibility_rule);
    VendorProfile::PrinterModel                   load_vendor_configs_from_json(const std::string &path);
    // Export a config bundle file containing all the presets and the names of the active presets.
    //void                        export_configbundle(const std::string &path, bool export_system_settings = false, bool export_physical_printers = false);
    //BBS: add a function to export current configbundle as default
    //void export_current_configbundle(const std::string &path);
    //BBS: add a function to export system presets for cloud-slicer
    //void export_system_configs(const std::string &path);
    std::vector<std::string> export_current_configs(const std::string &path, std::function<int(std::string const &)> override_confirm,
        bool include_modify, bool export_system_settings = false);

    // Enable / disable the "- default -" preset.
    void                        set_default_suppressed(bool default_suppressed);

    // Set the filament preset name. As the name could come from the UI selection box,
    // an optional "(modified)" suffix will be removed from the filament name.
    void                        set_filament_preset(size_t idx, const std::string &name);

    // Read out the number of extruders from an active printer preset,
    // update size and content of filament_presets.
    void                        update_multi_material_filament_presets(size_t to_delete_filament_id = size_t(-1));

    void                        on_extruders_count_changed(int extruder_count, bool reset_volume_type = true);

    // Update the is_compatible flag of all print and filament presets depending on whether they are marked
    // as compatible with the currently selected printer (and print in case of filament presets).
    // Also updates the is_visible flag of each preset.
    // If select_other_if_incompatible is true, then the print or filament preset is switched to some compatible
    // preset if the current print or filament preset is not compatible.
    void                        update_compatible(PresetSelectCompatibleType select_other_print_if_incompatible, PresetSelectCompatibleType select_other_filament_if_incompatible);
    void                        update_compatible(PresetSelectCompatibleType select_other_if_incompatible) { this->update_compatible(select_other_if_incompatible, select_other_if_incompatible); }

    /**
     * \brief Replace the built-in default process and filament presets with the printer's preferred ones
     *
     * Only compatible presets are selected. A default preset stays if no compatible preset exists.
     * Edits on a default preset are dropped, not transferred. The caller must refresh the UI and export selections.
     *
     * \return true if any selection changed
     */
    bool                        replace_default_presets();

    // Set the is_visible flag for printer vendors, printer models and printer variants
    // based on the user configuration.
    // If the "vendor" section is missing, enable all models and variants of the particular vendor.
    void                        load_installed_printers(const AppConfig &config);

    const std::string&          get_preset_name_by_alias(const Preset::Type& preset_type, const std::string& alias) const;

    const int                   get_required_hrc_by_filament_id(const std::string& filament_id) const;
    // Save current preset of a provided type under a new name. If the name is different from the old one,
    // Unselected option would be reverted to the beginning values
    //BBS: add project embedded preset logic
    void                        save_changes_for_preset(const std::string& new_name, Preset::Type type, const std::vector<std::string>& unselected_options, bool save_to_project = false);

    std::pair<PresetsConfigSubstitutions, std::string> load_system_models_from_json(ForwardCompatibilitySubstitutionRule compatibility_rule);
    std::pair<PresetsConfigSubstitutions, std::string> load_system_filaments_json(ForwardCompatibilitySubstitutionRule compatibility_rule);
    VendorProfile                                      get_custom_vendor_models() const;

    //BBS: add BBL as default
    static const char *BBL_BUNDLE;
	static const char *BBL_DEFAULT_PRINTER_MODEL;
	static const char *BBL_DEFAULT_PRINTER_VARIANT;
	static const char *BBL_DEFAULT_FILAMENT;
private:
    //std::pair<PresetsConfigSubstitutions, std::string> load_system_presets(ForwardCompatibilitySubstitutionRule compatibility_rule);
    //BBS: add json related logic
    std::pair<PresetsConfigSubstitutions, std::string> load_system_presets_from_json(ForwardCompatibilitySubstitutionRule compatibility_rule);
    // Merge one vendor's presets with the other vendor's presets, report duplicates.
    std::vector<std::string>    merge_presets(PresetBundle &&other);
    // Update the multicolor information for filaments.
    void update_filament_multi_color();
    // Update renamed_from and alias maps of system profiles.
    void 						update_system_maps();

    // Set the is_visible flag for filaments and sla materials,
    // apply defaults based on enabled printers when no filaments/materials are installed.
    void                        load_installed_filaments(AppConfig &config);
    void                        load_installed_sla_materials(AppConfig &config);
    void                        quick_fix_for_filaments_due_to_upgrade(AppConfig &config);

    // Load print, filament & printer presets from a config. If it is an external config, then the name is extracted from the external path.
    // and the external config is just referenced, not stored into user profile directory.
    // If it is not an external config, then the config will be stored into the user profile directory.
    void                        load_config_file_config(const std::string &name_or_path, bool is_external, DynamicPrintConfig &&config, Semver file_version = Semver(), bool selected = false);
    /*ConfigSubstitutions         load_config_file_config_bundle(
        const std::string &path, const boost::property_tree::ptree &tree, ForwardCompatibilitySubstitutionRule compatibility_rule);*/

    DynamicPrintConfig          full_fff_config(bool apply_extruder, std::optional<std::vector<int>> filament_maps=std::nullopt, std::optional<std::vector<int>> filament_volume_maps=std::nullopt) const;
    DynamicPrintConfig          full_sla_config() const;
};

ENABLE_ENUM_BITMASK_OPERATORS(PresetBundle::LoadConfigBundleAttribute)

//add these functions here to allow other access
extern void convert_filament_preset_name(std::string& machine_name, std::string& filament_name);

} // namespace Slic3r

#endif /* slic3r_PresetBundle_hpp_ */
