#include "libslic3r/libslic3r.h"
#include "libslic3r/Utils.hpp"
#include "AppConfig.hpp"
#include "Exception.hpp"
#include "LocalesUtils.hpp"
#include "Thread.hpp"
#include "format.hpp"
#include "nlohmann/json.hpp"

#include <utility>
#include <vector>
#include <stdexcept>

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/nowide/cenv.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree_fwd.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/format/format_fwd.hpp>
#include <boost/log/trivial.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#ifdef WIN32
//FIXME replace the two following includes with <boost/md5.hpp> after it becomes mainstream.
#include <boost/uuid/detail/md5.hpp>
#include <boost/algorithm/hex.hpp>
#endif

#define USE_JSON_CONFIG

using namespace nlohmann;

namespace Slic3r {

static const std::string VERSION_CHECK_URL = "";
static const std::string MODELS_STR = "models";

const std::string AppConfig::SECTION_FILAMENTS = "filaments";
const std::string AppConfig::SECTION_MATERIALS = "sla_materials";

void AppConfig::reset()
{
    m_storage.clear();
    set_defaults();
};

// Override missing or keys with their defaults.
void AppConfig::set_defaults()
{
    if (m_mode == EAppMode::Editor) {
#ifdef SUPPORT_AUTO_CENTER
        // Reset the empty fields to defaults.
        if (get("autocenter").empty())
            set("autocenter", true);
#endif

#ifdef SUPPORT_BACKGROUND_PROCESSING
        // Disable background processing by default as it is not stable.
        if (get("background_processing").empty())
            set("background_processing", false);
#endif

#ifdef SUPPORT_SHOW_DROP_PROJECT
        if (get("show_drop_project_dialog").empty())
            set("show_drop_project_dialog", true);
#endif

        if (get("drop_project_action").empty())
            set("drop_project_action", true);

#ifdef _WIN32
        if (get("associate_3mf").empty())
            set("associate_3mf", false);
        if (get("associate_stl").empty())
            set("associate_stl", false);

#endif // _WIN32

        // remove old 'use_legacy_opengl' parameter from this config, if present
        if (!get("use_legacy_opengl").empty())
            erase("app", "use_legacy_opengl");

#ifdef __APPLE__
        if (get("use_retina_opengl").empty())
            set("use_retina_opengl", true);
#endif

        if (get("single_instance").empty())
            set("single_instance", 
#ifdef __APPLE__
                true
#else // __APPLE__
                false
#endif // __APPLE__
                );

#ifdef SUPPORT_REMEMBER_OUTPUT_PATH
        if (get("remember_output_path").empty())
            set("remember_output_path", true);

        if (get("remember_output_path_removable").empty())
            set("remember_output_path_removable", true);
#endif
        if (get("toolkit_size").empty())
            set("toolkit_size", "100");

#if ENABLE_ENVIRONMENT_MAP
        if (get("use_environment_map").empty())
            set("use_environment_map", false);
#endif // ENABLE_ENVIRONMENT_MAP

        if (get("use_inches").empty())
            set("use_inches", false);
    }
    else {
#ifdef _WIN32
        if (get("associate_gcode").empty())
            set("associate_gcode", false);
#endif // _WIN32
    }

    if (get("is_perspective").empty())
        set("is_perspective", true);

#ifdef SUPPORT_FREE_CAMERA
    if (get("use_free_camera").empty())
        set("use_free_camera", false);
#endif

#ifdef SUPPORT_REVERSE_MOUSE_ZOOM
    if (get("reverse_mouse_wheel_zoom").empty())
        set("reverse_mouse_wheel_zoom", false);
#endif

#ifdef SUPPORT_SHOW_HINTS
    if (get("show_hints").empty())
        set("show_hints", true);
#endif


#ifdef _WIN32

#ifdef SUPPORT_3D_CONNEXION
    if (get("use_legacy_3DConnexion").empty())
        set("use_legacy_3DConnexion", false);
#endif

#ifdef SUPPORT_DARK_MODE
    if (get("dark_color_mode").empty())
        set("dark_color_mode", false);
#endif

#ifdef SUPPORT_SYS_MENU
    if (get("sys_menu_enabled").empty())
        set("sys_menu_enabled", true);
#endif
#endif // _WIN32

    // BBS
    /*if (get("3mf_include_gcode").empty())
        set("3mf_include_gcode", true);*/

    if (get("developer_mode").empty())
        set("developer_mode", false);

    // BBS
    if (get("preset_folder").empty())
        set("preset_folder", "");

    // BBS
    if (get("slicer_uuid").empty()) {
        boost::uuids::uuid uuid = boost::uuids::random_generator()();
        set("slicer_uuid", to_string(uuid));
    }

    if (get("show_model_mesh").empty()) {
        set("show_model_mesh", false);
    }

    if (get("show_model_shadow").empty()) {
        set("show_model_shadow", true);
    }

    if (get("show_build_edges").empty()) {
        set("show_build_edgets", false);
    }

    if (get("show_daily_tips").empty()) {
        set("show_daily_tips", true);
    }

    if (get("show_printable_box").empty()) {
        set("show_printable_box", true);
    }

    // Remove legacy window positions/sizes
    erase("app", "main_frame_maximized");
    erase("app", "main_frame_pos");
    erase("app", "main_frame_size");
    erase("app", "object_settings_maximized");
    erase("app", "object_settings_pos");
    erase("app", "object_settings_size");
}

#ifdef WIN32
static std::string appconfig_md5_hash_line(const std::string_view data)
{
    //FIXME replace the two following includes with <boost/md5.hpp> after it becomes mainstream.
    // return boost::md5(data).hex_str_value();
    // boost::uuids::detail::md5 is an internal namespace thus it may change in the future.
    // Also this implementation is not the fastest, it was designed for short blocks of text.
    using boost::uuids::detail::md5;
    md5              md5_hash;
    // unsigned int[4], 128 bits
    md5::digest_type md5_digest{};
    std::string      md5_digest_str;
    md5_hash.process_bytes(data.data(), data.size());
    md5_hash.get_digest(md5_digest);
    boost::algorithm::hex(md5_digest, md5_digest + std::size(md5_digest), std::back_inserter(md5_digest_str));
    // MD5 hash is 32 HEX digits long.
    assert(md5_digest_str.size() == 32);
    // This line will be emited at the end of the file.
    return "# MD5 checksum " + md5_digest_str + "\n";
};

// Assume that the last line with the comment inside the config file contains a checksum and that the user didn't modify the config file.
static bool verify_config_file_checksum(boost::nowide::ifstream &ifs)
{
    auto read_whole_config_file = [&ifs]() -> std::string {
        std::stringstream ss;
        ss << ifs.rdbuf();
        return ss.str();
    };

    ifs.seekg(0, boost::nowide::ifstream::beg);
    std::string whole_config = read_whole_config_file();

    // The checksum should be on the last line in the config file.
    if (size_t last_comment_pos = whole_config.find_last_of('#'); last_comment_pos != std::string::npos) {
        // Split read config into two parts, one with checksum, and the second part is part with configuration from the checksum was computed.
        // Verify existence and validity of the MD5 checksum line at the end of the file.
        // When the checksum isn't found, the checksum was not saved correctly, it was removed or it is an older config file without the checksum.
        // If the checksum is incorrect, then the file was either not saved correctly or modified.
        if (std::string_view(whole_config.c_str() + last_comment_pos, whole_config.size() - last_comment_pos) == appconfig_md5_hash_line({ whole_config.data(), last_comment_pos }))
            return true;
    }
    return false;
}
#endif



#ifdef USE_JSON_CONFIG
std::string AppConfig::load()
{
    json j;

    // 1) Read the complete config file into a boost::property_tree.
    namespace pt = boost::property_tree;
    pt::ptree tree;
    boost::nowide::ifstream ifs;
    bool                    recovered = false;

    try {
        ifs.open(AppConfig::loading_path());
        ifs >> j;

#ifdef WIN32
        // Verify the checksum of the config file without taking just for debugging purpose.
        if (!verify_config_file_checksum(ifs))
            BOOST_LOG_TRIVIAL(info) << "The configuration file " << AppConfig::loading_path() <<
            " has a wrong MD5 checksum or the checksum is missing. This may indicate a file corruption or a harmless user edit.";
#endif
    }
    catch(nlohmann::detail::parse_error &err) {
        BOOST_LOG_TRIVIAL(info) << "AppConfig: parse json error = " << err.what();
    }
    
    try {
        for (auto it = j.begin(); it != j.end(); it++) {
            if (it.key() == MODELS_STR) {
                for (auto& j_model : it.value()) {
                    // This is a vendor section listing enabled model / variants
                    const auto vendor_name = j_model["vendor"].get<std::string>();
                    auto& vendor = m_vendors[vendor_name];
                    const auto model_name = j_model["model"].get<std::string>();
                    std::vector<std::string> variants;
                    if (!unescape_strings_cstyle(j_model["nozzle_diameter"], variants)) { continue; }
                    for (const auto& variant : variants) {
                        vendor[model_name].insert(variant);
                    }
                }
            } else if (it.key() == SECTION_FILAMENTS) {
                json j_filaments = it.value();
                for (auto& element : j_filaments) {
                    m_storage[it.key()][element] = "true";
                }
            } else if (it.key() == "presets") {
                for (auto iter = it.value().begin(); iter != it.value().end(); iter++) {
                    if (iter.key() == "filaments") {
                        int idx = 0;
                        for(auto& element: iter.value()) {
                            if (idx == 0)
                                m_storage[it.key()]["filament"] = element;
                            else
                                m_storage[it.key()]["filament_" + std::to_string(idx)] = element;
                            idx++;
                        }
                    } else {
                        m_storage[it.key()][iter.key()] = iter.value().get<std::string>();
                    }
                }
            } else {
                if (it.value().is_object()) {
                    for (auto iter = it.value().begin(); iter != it.value().end(); iter++) {
                        if (iter.value().is_boolean()) {
                            if (iter.value()) {
                                m_storage[it.key()][iter.key()] = "true";
                            } else {
                                m_storage[it.key()][iter.key()] = "false";
                            }
                        } else {
                            if (iter.value().is_string())
                                m_storage[it.key()][iter.key()] = iter.value().get<std::string>();
                            else {
                                BOOST_LOG_TRIVIAL(trace) << "load config warning...";
                            }
                        }
                    }
                }
            }
        }
    } catch(...) {
        ;
    }

    // Figure out if datadir has legacy presets
    auto ini_ver = Semver::parse(get("version"));
    m_legacy_datadir = false;
    if (ini_ver) {
        m_orig_version = *ini_ver;
        ini_ver->set_metadata(boost::none);
        ini_ver->set_prerelease(boost::none);
    }

    // Legacy conversion
    if (m_mode == EAppMode::Editor) {
        // Convert [extras] "physical_printer" to [presets] "physical_printer",
        // remove the [extras] section if it becomes empty.
        if (auto it_section = m_storage.find("extras"); it_section != m_storage.end()) {
            if (auto it_physical_printer = it_section->second.find("physical_printer"); it_physical_printer != it_section->second.end()) {
                m_storage["presets"]["physical_printer"] = it_physical_printer->second;
                it_section->second.erase(it_physical_printer);
            }
            if (it_section->second.empty())
                m_storage.erase(it_section);
        }
    }

    // Override missing or keys with their defaults.
    this->set_defaults();
    m_dirty = false;
    return "";
}

void AppConfig::save()
{
    {
        // Returns "undefined" if the thread naming functionality is not supported by the operating system.
        std::optional<std::string> current_thread_name = get_current_thread_name();
        if (current_thread_name && *current_thread_name != "bambustudio_main")
            throw CriticalException("Calling AppConfig::save() from a worker thread!");
    }

    // The config is first written to a file with a PID suffix and then moved
    // to avoid race conditions with multiple instances of Slic3r
    const auto path = config_path();
    std::string path_pid = (boost::format("%1%.%2%") % path % get_current_pid()).str();

    json j;

    std::stringstream config_ss;
    if (m_mode == EAppMode::Editor)
        j["header"] = Slic3r::header_slic3r_generated();
    else
        j["header"] = Slic3r::header_gcodeviewer_generated();

    // Make sure the "no" category is written first.
    for (const auto& kvp : m_storage["app"]) {
        if (kvp.second == "true") {
            j["app"][kvp.first] = true;
            continue;
        }
        if (kvp.second == "false") {
            j["app"][kvp.first] = false;
            continue;
        }
        j["app"][kvp.first] = kvp.second;
    }

    // Write the other categories.
    for (const auto& category : m_storage) {
        if (category.first.empty())
            continue;
        if (category.first == SECTION_FILAMENTS) {
            json j_filaments;
            for (const auto& kvp: category.second) {
                j_filaments.push_back(kvp.first);
            }
            j[category.first] = j_filaments;
            continue;
        } else if (category.first == "presets") {
            json j_filament_array;
            for(const auto& kvp : category.second) {
                if (boost::starts_with(kvp.first, "filament")) {
                    j_filament_array.push_back(kvp.second);
                } else {
                    j[category.first][kvp.first] = kvp.second;
                }
            }
            j["presets"]["filaments"] = j_filament_array;
            continue;
        }
        for (const auto& kvp : category.second) {
            if (kvp.second == "true") {
                j[category.first][kvp.first] = true;
                continue;
            }
            if (kvp.second == "false") {
                j[category.first][kvp.first] = false;
                continue;
            }
            j[category.first][kvp.first] = kvp.second;
        }
    }

    // Write vendor sections
    for (const auto& vendor : m_vendors) {
        size_t size_sum = 0;
        for (const auto& model : vendor.second) { size_sum += model.second.size(); }
        if (size_sum == 0) { continue; }

        for (const auto& model : vendor.second) {
            if (model.second.empty()) { continue; }
            const std::vector<std::string> variants(model.second.begin(), model.second.end());
            const auto escaped = escape_strings_cstyle(variants);
            //j[VENDOR_PREFIX + vendor.first][MODEL_PREFIX + model.first] = escaped;
            json j_model;
            j_model["vendor"] = vendor.first;
            j_model["model"] = model.first;
            j_model["nozzle_diameter"] = escaped;
            j[MODELS_STR].push_back(j_model);
        }
    }

    boost::nowide::ofstream c;
    c.open(path_pid, std::ios::out | std::ios::trunc);
    c << std::setw(4) << j << std::endl;
    c.close();
    // c << appconfig_md5_hash_line(config_str);

#ifdef WIN32
    // Make a backup of the configuration file before copying it to the final destination.
    std::string error_message;
    std::string backup_path = (boost::format("%1%.bak") % path).str();
    // Copy configuration file with PID suffix into the configuration file with "bak" suffix.
    if (copy_file(path_pid, backup_path, error_message, false) != SUCCESS)
        BOOST_LOG_TRIVIAL(error) << "Copying from " << path_pid << " to " << backup_path << " failed. Failed to create a backup configuration.";
#endif

    // Rename the config atomically.
    // On Windows, the rename is likely NOT atomic, thus it may fail if BambuStudio crashes on another thread in the meanwhile.
    // To cope with that, we already made a backup of the config on Windows.
    rename_file(path_pid, path);
    m_dirty = false;
}

#else

std::string AppConfig::load()
{
    // 1) Read the complete config file into a boost::property_tree.
    namespace pt = boost::property_tree;
    pt::ptree tree;
    boost::nowide::ifstream ifs;
    bool                    recovered = false;

    try {
        ifs.open(AppConfig::loading_path());
#ifdef WIN32
        // Verify the checksum of the config file without taking just for debugging purpose.
        if (!verify_config_file_checksum(ifs))
            BOOST_LOG_TRIVIAL(info) << "The configuration file " << AppConfig::loading_path() <<
            " has a wrong MD5 checksum or the checksum is missing. This may indicate a file corruption or a harmless user edit.";

        ifs.seekg(0, boost::nowide::ifstream::beg);
#endif
        pt::read_ini(ifs, tree);
    }
    catch (pt::ptree_error& ex) {
#ifdef WIN32
        // The configuration file is corrupted, try replacing it with the backup configuration.
        ifs.close();
        std::string backup_path = (boost::format("%1%.bak") % AppConfig::loading_path()).str();
        if (boost::filesystem::exists(backup_path)) {
            // Compute checksum of the configuration backup file and try to load configuration from it when the checksum is correct.
            boost::nowide::ifstream backup_ifs(backup_path);
            if (!verify_config_file_checksum(backup_ifs)) {
                BOOST_LOG_TRIVIAL(error) << format("Both \"%1%\" and \"%2%\" are corrupted. It isn't possible to restore configuration from the backup.", AppConfig::loading_path(), backup_path);
                backup_ifs.close();
                boost::filesystem::remove(backup_path);
            }
            else if (std::string error_message; copy_file(backup_path, AppConfig::loading_path(), error_message, false) != SUCCESS) {
                BOOST_LOG_TRIVIAL(error) << format("Configuration file \"%1%\" is corrupted. Failed to restore from backup \"%2%\": %3%", AppConfig::loading_path(), backup_path, error_message);
                backup_ifs.close();
                boost::filesystem::remove(backup_path);
            }
            else {
                BOOST_LOG_TRIVIAL(info) << format("Configuration file \"%1%\" was corrupted. It has been succesfully restored from the backup \"%2%\".", AppConfig::loading_path(), backup_path);
                // Try parse configuration file after restore from backup.
                try {
                    ifs.open(AppConfig::loading_path());
                    pt::read_ini(ifs, tree);
                    recovered = true;
                }
                catch (pt::ptree_error& ex) {
                    BOOST_LOG_TRIVIAL(info) << format("Failed to parse configuration file \"%1%\" after it has been restored from backup: %2%", AppConfig::loading_path(), ex.what());
                }
            }
        }
        else
#endif // WIN32
            BOOST_LOG_TRIVIAL(info) << format("Failed to parse configuration file \"%1%\": %2%", AppConfig::loading_path(), ex.what());
        if (!recovered) {
            // Report the initial error of parsing BambuStudio.ini.
            // Error while parsing config file. We'll customize the error message and rethrow to be displayed.
            // ! But to avoid the use of _utf8 (related to use of wxWidgets) 
            // we will rethrow this exception from the place of load() call, if returned value wouldn't be empty
            /*
            throw Slic3r::RuntimeError(
                _utf8(L("Error parsing BambuStudio config file, it is probably corrupted. "
                        "Try to manually delete the file to recover from the error. Your user profiles will not be affected.")) +
                "\n\n" + AppConfig::config_path() + "\n\n" + ex.what());
            */
            return ex.what();
        }
    }

    // 2) Parse the property_tree, extract the sections and key / value pairs.
    for (const auto& section : tree) {
        if (section.second.empty()) {
            // This may be a top level (no section) entry, or an empty section.
            std::string data = section.second.data();
            if (!data.empty())
                // If there is a non-empty data, then it must be a top-level (without a section) config entry.
                m_storage[""][section.first] = data;
        }
        else if (boost::starts_with(section.first, VENDOR_PREFIX)) {
            // This is a vendor section listing enabled model / variants
            const auto vendor_name = section.first.substr(VENDOR_PREFIX.size());
            auto& vendor = m_vendors[vendor_name];
            for (const auto& kvp : section.second) {
                if (!boost::starts_with(kvp.first, MODEL_PREFIX)) { continue; }
                const auto model_name = kvp.first.substr(MODEL_PREFIX.size());
                std::vector<std::string> variants;
                if (!unescape_strings_cstyle(kvp.second.data(), variants)) { continue; }
                for (const auto& variant : variants) {
                    vendor[model_name].insert(variant);
                }
            }
        }
        else {
            // This must be a section name. Read the entries of a section.
            std::map<std::string, std::string>& storage = m_storage[section.first];
            for (auto& kvp : section.second)
                storage[kvp.first] = kvp.second.data();
        }
    }

    // Figure out if datadir has legacy presets
    auto ini_ver = Semver::parse(get("version"));
    m_legacy_datadir = false;
    if (ini_ver) {
        m_orig_version = *ini_ver;
        // Make 1.40.0 alphas compare well
        ini_ver->set_metadata(boost::none);
        ini_ver->set_prerelease(boost::none);
        //m_legacy_datadir = ini_ver < Semver(1, 40, 0);
    }

    // Legacy conversion
    if (m_mode == EAppMode::Editor) {
        // Convert [extras] "physical_printer" to [presets] "physical_printer",
        // remove the [extras] section if it becomes empty.
        if (auto it_section = m_storage.find("extras"); it_section != m_storage.end()) {
            if (auto it_physical_printer = it_section->second.find("physical_printer"); it_physical_printer != it_section->second.end()) {
                m_storage["presets"]["physical_printer"] = it_physical_printer->second;
                it_section->second.erase(it_physical_printer);
            }
            if (it_section->second.empty())
                m_storage.erase(it_section);
        }
    }

    // Override missing or keys with their defaults.
    this->set_defaults();
    m_dirty = false;
    return "";
}

void AppConfig::save()
{
    {
        // Returns "undefined" if the thread naming functionality is not supported by the operating system.
        std::optional<std::string> current_thread_name = get_current_thread_name();
        if (current_thread_name && *current_thread_name != "bambustudio_main")
            throw CriticalException("Calling AppConfig::save() from a worker thread!");
    }

    // The config is first written to a file with a PID suffix and then moved
    // to avoid race conditions with multiple instances of Slic3r
    const auto path = config_path();
    std::string path_pid = (boost::format("%1%.%2%") % path % get_current_pid()).str();

    std::stringstream config_ss;
    if (m_mode == EAppMode::Editor)
        config_ss << "# " << Slic3r::header_slic3r_generated() << std::endl;
    else
        config_ss << "# " << Slic3r::header_gcodeviewer_generated() << std::endl;
    // Make sure the "no" category is written first.
    for (const auto& kvp : m_storage[""])
        config_ss << kvp.first << " = " << kvp.second << std::endl;
    // Write the other categories.
    for (const auto& category : m_storage) {
    	if (category.first.empty())
    		continue;
        config_ss << std::endl << "[" << category.first << "]" << std::endl;
        for (const auto& kvp : category.second)
            config_ss << kvp.first << " = " << kvp.second << std::endl;
	}
    // Write vendor sections
    for (const auto &vendor : m_vendors) {
        size_t size_sum = 0;
        for (const auto &model : vendor.second) { size_sum += model.second.size(); }
        if (size_sum == 0) { continue; }

        config_ss << std::endl << "[" << VENDOR_PREFIX << vendor.first << "]" << std::endl;

        for (const auto &model : vendor.second) {
            if (model.second.empty()) { continue; }
            const std::vector<std::string> variants(model.second.begin(), model.second.end());
            const auto escaped = escape_strings_cstyle(variants);
            config_ss << MODEL_PREFIX << model.first << " = " << escaped << std::endl;
        }
    }
    // One empty line before the MD5 sum.
    config_ss << std::endl;

    std::string config_str = config_ss.str();
    boost::nowide::ofstream c;
    c.open(path_pid, std::ios::out | std::ios::trunc);
    c << config_str;
#ifdef WIN32
    // WIN32 specific: The final "rename_file()" call is not safe in case of an application crash, there is no atomic "rename file" API
    // provided by Windows (sic!). Therefore we save a MD5 checksum to be able to verify file corruption. In addition,
    // we save the config file into a backup first before moving it to the final destination.
    c << appconfig_md5_hash_line(config_str);
#endif
    c.close();
    
#ifdef WIN32
    // Make a backup of the configuration file before copying it to the final destination.
    std::string error_message;
    std::string backup_path = (boost::format("%1%.bak") % path).str();
    // Copy configuration file with PID suffix into the configuration file with "bak" suffix.
    if (copy_file(path_pid, backup_path, error_message, false) != SUCCESS)
        BOOST_LOG_TRIVIAL(error) << "Copying from " << path_pid << " to " << backup_path << " failed. Failed to create a backup configuration.";
#endif

    // Rename the config atomically.
    // On Windows, the rename is likely NOT atomic, thus it may fail if BambuStudio crashes on another thread in the meanwhile.
    // To cope with that, we already made a backup of the config on Windows.
    rename_file(path_pid, path);
    m_dirty = false;
}
#endif

bool AppConfig::get_variant(const std::string &vendor, const std::string &model, const std::string &variant) const
{
    const auto it_v = m_vendors.find(vendor);
    if (it_v == m_vendors.end()) { return false; }
    const auto it_m = it_v->second.find(model);
    return it_m == it_v->second.end() ? false : it_m->second.find(variant) != it_m->second.end();
}

void AppConfig::set_variant(const std::string &vendor, const std::string &model, const std::string &variant, bool enable)
{
    if (enable) {
        if (get_variant(vendor, model, variant)) { return; }
        m_vendors[vendor][model].insert(variant);
    } else {
        auto it_v = m_vendors.find(vendor);
        if (it_v == m_vendors.end()) { return; }
        auto it_m = it_v->second.find(model);
        if (it_m == it_v->second.end()) { return; }
        auto it_var = it_m->second.find(variant);
        if (it_var == it_m->second.end()) { return; }
        it_m->second.erase(it_var);
    }
    // If we got here, there was an update
    m_dirty = true;
}

void AppConfig::set_vendors(const AppConfig &from)
{
    m_vendors = from.m_vendors;
    m_dirty = true;
}

std::string AppConfig::get_last_dir() const
{
    const auto it = m_storage.find("recent");
    if (it != m_storage.end()) {
        {
            const auto it2 = it->second.find("last_opened_folder");
            if (it2 != it->second.end() && ! it2->second.empty())
                return it2->second;
        }
        {
            const auto it2 = it->second.find("settings_folder");
            if (it2 != it->second.end() && ! it2->second.empty())
                return it2->second;
        }
    }
    return std::string();
}

std::vector<std::string> AppConfig::get_recent_projects() const
{
    std::vector<std::string> ret;
    const auto it = m_storage.find("recent_projects");
    if (it != m_storage.end())
    {
        for (const std::map<std::string, std::string>::value_type& item : it->second)
        {
            ret.push_back(item.second);
        }
    }
    return ret;
}

void AppConfig::set_recent_projects(const std::vector<std::string>& recent_projects)
{
    auto it = m_storage.find("recent_projects");
    if (it == m_storage.end())
        it = m_storage.insert(std::map<std::string, std::map<std::string, std::string>>::value_type("recent_projects", std::map<std::string, std::string>())).first;

    it->second.clear();
    for (unsigned int i = 0; i < (unsigned int)recent_projects.size(); ++i)
    {
        it->second[std::to_string(i + 1)] = recent_projects[i];
    }
}

void AppConfig::set_mouse_device(const std::string& name, double translation_speed, double translation_deadzone,
                                 float rotation_speed, float rotation_deadzone, double zoom_speed, bool swap_yz)
{
    std::string key = std::string("mouse_device:") + name;
    auto it = m_storage.find(key);
    if (it == m_storage.end())
        it = m_storage.insert(std::map<std::string, std::map<std::string, std::string>>::value_type(key, std::map<std::string, std::string>())).first;

    it->second.clear();
    it->second["translation_speed"] = float_to_string_decimal_point(translation_speed);
    it->second["translation_deadzone"] = float_to_string_decimal_point(translation_deadzone);
    it->second["rotation_speed"] = float_to_string_decimal_point(rotation_speed);
    it->second["rotation_deadzone"] = float_to_string_decimal_point(rotation_deadzone);
    it->second["zoom_speed"] = float_to_string_decimal_point(zoom_speed);
    it->second["swap_yz"] = swap_yz ? "1" : "0";
}

std::vector<std::string> AppConfig::get_mouse_device_names() const
{
    static constexpr const char   *prefix     = "mouse_device:";
    static const size_t  prefix_len = strlen(prefix);
    std::vector<std::string> out;
    for (const auto& key_value_pair : m_storage)
        if (boost::starts_with(key_value_pair.first, prefix) && key_value_pair.first.size() > prefix_len)
            out.emplace_back(key_value_pair.first.substr(prefix_len));
    return out;
}

void AppConfig::update_config_dir(const std::string &dir)
{
    this->set("recent", "settings_folder", dir);
}

void AppConfig::update_skein_dir(const std::string &dir)
{
    if (is_shapes_dir(dir))
        return; // do not save "shapes gallery" directory
    this->set("recent", "last_opened_folder", dir);
}
/*
std::string AppConfig::get_last_output_dir(const std::string &alt) const
{
	
    const auto it = m_storage.find("");
    if (it != m_storage.end()) {
        const auto it2 = it->second.find("last_export_path");
        const auto it3 = it->second.find("remember_output_path");
        if (it2 != it->second.end() && it3 != it->second.end() && ! it2->second.empty() && it3->second == "1")
            return it2->second;
    }
    return alt;
}

void AppConfig::update_last_output_dir(const std::string &dir)
{
    this->set("", "last_export_path", dir);
}
*/
std::string AppConfig::get_last_output_dir(const std::string& alt, const bool removable) const
{
	std::string s1 = (removable ? "last_export_path_removable" : "last_export_path");
	const auto it = m_storage.find("app");
	if (it != m_storage.end()) {
		const auto it2 = it->second.find(s1);
		if (it2 != it->second.end() && !it2->second.empty())
			return it2->second;
	}
	return is_shapes_dir(alt) ? get_last_dir() : alt;
}

void AppConfig::update_last_output_dir(const std::string& dir, const bool removable)
{
	this->set("app", (removable ? "last_export_path_removable" : "last_export_path"), dir);
}

// BBS: backup
std::string AppConfig::get_last_backup_dir() const
{
	const auto it = m_storage.find("app");
	if (it != m_storage.end()) {
		const auto it2 = it->second.find("last_backup_path");
		if (it2 != it->second.end())
			return it2->second;
	}
	return "";
}

// BBS: backup
void AppConfig::update_last_backup_dir(const std::string& dir)
{
	this->set("app", "last_backup_path", dir);
    this->save();
}


void AppConfig::reset_selections()
{
    auto it = m_storage.find("presets");
    if (it != m_storage.end()) {
        it->second.erase("print");
        it->second.erase("filament");
        it->second.erase("sla_print");
        it->second.erase("sla_material");
        it->second.erase("printer");
        it->second.erase("physical_printer");
        m_dirty = true;
    }
}

std::string AppConfig::config_path()
{
#ifdef USE_JSON_CONFIG
    std::string path = (m_mode == EAppMode::Editor) ?
        (boost::filesystem::path(Slic3r::data_dir()) / (SLIC3R_APP_KEY ".conf")).make_preferred().string() :
        (boost::filesystem::path(Slic3r::data_dir()) / (GCODEVIEWER_APP_KEY ".conf")).make_preferred().string();
#else
    std::string path = (m_mode == EAppMode::Editor) ?
        (boost::filesystem::path(Slic3r::data_dir()) / (SLIC3R_APP_KEY ".ini")).make_preferred().string() :
        (boost::filesystem::path(Slic3r::data_dir()) / (GCODEVIEWER_APP_KEY ".ini")).make_preferred().string();
#endif

    return path;
}

std::string AppConfig::version_check_url() const
{
    auto from_settings = get("version_check_url");
    return from_settings.empty() ? VERSION_CHECK_URL : from_settings;
}

bool AppConfig::exists()
{
    return boost::filesystem::exists(config_path());
}

}; // namespace Slic3r
