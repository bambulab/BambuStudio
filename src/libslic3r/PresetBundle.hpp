#ifndef slic3r_PresetBundle_hpp_
#define slic3r_PresetBundle_hpp_

#include "Preset.hpp"
#include "AppConfig.hpp"
#include "enum_bitmask.hpp"

#include <memory>
#include <unordered_map>
#include <boost/filesystem/path.hpp>

#define DEFAULT_USER_FOLDER_NAME     "default"

namespace Slic3r {

// Bundle of Print + Filament + Printer presets.
class PresetBundle
{
public:
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
    PresetsConfigSubstitutions load_presets(AppConfig &config, ForwardCompatibilitySubstitutionRule rule,
                                            const PresetPreferences& preferred_selection = PresetPreferences());

    // Load selections (current print, current filaments, current printer) from config.ini
    // This is done just once on application start up.
    //BBS: change it to public
    void     load_selections(AppConfig &config, const PresetPreferences& preferred_selection = PresetPreferences());

    // BBS Load user presets
    PresetsConfigSubstitutions load_user_presets(std::string user, ForwardCompatibilitySubstitutionRule rule);
    PresetsConfigSubstitutions load_user_presets(AppConfig &config, std::map<std::string, std::map<std::string, std::string>>& my_presets, ForwardCompatibilitySubstitutionRule rule);
    PresetsConfigSubstitutions import_presets(std::vector<std::string> &files, std::function<int(std::string const &)> override_confirm, ForwardCompatibilitySubstitutionRule rule);
    void save_user_presets(AppConfig& config, std::vector<std::string>& need_to_delete_list);
    void remove_users_preset(AppConfig &config, std::map<std::string, std::map<std::string, std::string>> * my_presets = nullptr);
    void update_user_presets_directory(const std::string preset_folder);
    void remove_user_presets_directory(const std::string preset_folder);
    void update_system_preset_setting_ids(std::map<std::string, std::map<std::string, std::string>>& system_presets);

    //BBS: add API to get previous machine
    bool validate_printers(const std::string &name, DynamicPrintConfig& config);

    //BBS: add function to generate differed preset for save
    //the pointer should be freed by the caller
    Preset* get_preset_differed_for_save(Preset& preset);
    int get_differed_values_to_update(Preset& preset, std::map<std::string, std::string>& key_values);

    //BBS: get vendor's current version
    Semver get_vendor_profile_version(std::string vendor_name);

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
    unsigned int sync_ams_list(unsigned int & unknowns);
    //BBS: check whether this is the only edited filament
    bool is_the_only_edited_filament(unsigned int filament_index);

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
    std::vector<DynamicPrintConfig> filament_ams_list;

    // The project configuration values are kept separated from the print/filament/printer preset,
    // they are being serialized / deserialized from / to the .amf, .3mf, .config, .gcode,
    // and they are being used by slicing core.
    DynamicPrintConfig          project_config;

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

    DynamicPrintConfig          full_config() const;
    // full_config() with the some "useless" config removed.
    DynamicPrintConfig          full_config_secure() const;

    // Load user configuration and store it into the user profiles.
    // This method is called by the configuration wizard.
    void                        load_config_from_wizard(const std::string &name, DynamicPrintConfig config, Semver file_version, bool is_custom_defined = false)
        { this->load_config_file_config(name, false, std::move(config), file_version, true, is_custom_defined); }

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
    };
    using LoadConfigBundleAttributes = enum_bitmask<LoadConfigBundleAttribute>;
    // Load the config bundle based on the flags.
    // Don't do any config substitutions when loading a system profile, perform and report substitutions otherwise.
    /*std::pair<PresetsConfigSubstitutions, size_t> load_configbundle(
        const std::string &path, LoadConfigBundleAttributes flags, ForwardCompatibilitySubstitutionRule compatibility_rule);*/
    //BBS: add json related logic
    std::pair<PresetsConfigSubstitutions, size_t> load_vendor_configs_from_json(
        const std::string &path, const std::string &vendor_name, LoadConfigBundleAttributes flags, ForwardCompatibilitySubstitutionRule compatibility_rule);

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
    void                        update_multi_material_filament_presets();

    // Update the is_compatible flag of all print and filament presets depending on whether they are marked
    // as compatible with the currently selected printer (and print in case of filament presets).
    // Also updates the is_visible flag of each preset.
    // If select_other_if_incompatible is true, then the print or filament preset is switched to some compatible
    // preset if the current print or filament preset is not compatible.
    void                        update_compatible(PresetSelectCompatibleType select_other_print_if_incompatible, PresetSelectCompatibleType select_other_filament_if_incompatible);
    void                        update_compatible(PresetSelectCompatibleType select_other_if_incompatible) { this->update_compatible(select_other_if_incompatible, select_other_if_incompatible); }

    // Set the is_visible flag for printer vendors, printer models and printer variants
    // based on the user configuration.
    // If the "vendor" section is missing, enable all models and variants of the particular vendor.
    void                        load_installed_printers(const AppConfig &config);

    const std::string&          get_preset_name_by_alias(const Preset::Type& preset_type, const std::string& alias) const;

    // Save current preset of a provided type under a new name. If the name is different from the old one,
    // Unselected option would be reverted to the beginning values
    //BBS: add project embedded preset logic
    void                        save_changes_for_preset(const std::string& new_name, Preset::Type type, const std::vector<std::string>& unselected_options, bool save_to_project = false);

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
    // Update renamed_from and alias maps of system profiles.
    void 						update_system_maps();

    // Set the is_visible flag for filaments and sla materials,
    // apply defaults based on enabled printers when no filaments/materials are installed.
    void                        load_installed_filaments(AppConfig &config);
    void                        load_installed_sla_materials(AppConfig &config);

    // Load print, filament & printer presets from a config. If it is an external config, then the name is extracted from the external path.
    // and the external config is just referenced, not stored into user profile directory.
    // If it is not an external config, then the config will be stored into the user profile directory.
    void                        load_config_file_config(const std::string &name_or_path, bool is_external, DynamicPrintConfig &&config, Semver file_version = Semver(), bool selected = false, bool is_custom_defined = false);
    /*ConfigSubstitutions         load_config_file_config_bundle(
        const std::string &path, const boost::property_tree::ptree &tree, ForwardCompatibilitySubstitutionRule compatibility_rule);*/

    DynamicPrintConfig          full_fff_config() const;
    DynamicPrintConfig          full_sla_config() const;
};

ENABLE_ENUM_BITMASK_OPERATORS(PresetBundle::LoadConfigBundleAttribute)

} // namespace Slic3r

#endif /* slic3r_PresetBundle_hpp_ */
