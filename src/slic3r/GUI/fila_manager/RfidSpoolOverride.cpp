#include "RfidSpoolOverride.hpp"
#include "libslic3r/Utils.hpp"
#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <algorithm>
#include <cmath>
#include <cctype>

namespace Slic3r { namespace GUI {
namespace {
std::string key(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return text;
}
bool hex(const std::string& text) {
    return std::all_of(text.begin(), text.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    });
}
}

bool RfidSpoolOverrides::valid_uuid(const std::string& uuid) {
    // Callers supply tray_uuid (inventory tag_uid), never the device tag_uid.
    return (uuid.size() == 16 || uuid.size() == 32) && hex(uuid) && uuid.find_first_not_of('0') != std::string::npos;
}
bool RfidSpoolOverrides::valid_color(const std::string& color) {
    return (color.size() == 7 || color.size() == 9) && color[0] == '#' && hex(color.substr(1));
}
RfidSpoolOverrides& RfidSpoolOverrides::instance() {
    static RfidSpoolOverrides store((boost::filesystem::path(data_dir()) / "filament_inventory" / "rfid_overrides.json").string());
    return store;
}
RfidSpoolOverrides::RfidSpoolOverrides(std::string path) : m_path(std::move(path)) {
    try {
        if (!boost::filesystem::exists(m_path)) return;
        boost::nowide::ifstream file(m_path);
        auto doc = nlohmann::json::parse(file);
        if (doc.at("version") != 1 || !doc.at("spools").is_object()) throw std::runtime_error("Unsupported override file");
        std::map<std::string, RfidSpoolOverride> loaded;
        for (auto it = doc.at("spools").begin(); it != doc.at("spools").end(); ++it) {
            RfidSpoolOverride value{it.value().at("material_type").get<std::string>(),
                it.value().at("color").get<std::string>(), it.value().at("profiles").get<std::map<std::string, std::string>>()};
            if (!valid_uuid(it.key()) || !valid_color(value.color) || value.material_type.empty() || value.profiles.empty() ||
                std::any_of(value.profiles.begin(), value.profiles.end(), [](const auto& pair) { return pair.first.empty() || pair.second.empty(); }))
                throw std::runtime_error("Invalid override record");
            if (!loaded.emplace(key(it.key()), std::move(value)).second) throw std::runtime_error("Duplicate RFID identity");
        }
        m_values = std::move(loaded);
    } catch (const std::exception& e) { m_error = e.what(); }
}
const RfidSpoolOverride* RfidSpoolOverrides::find(const std::string& uuid) const {
    const auto it = m_values.find(key(uuid));
    return it == m_values.end() ? nullptr : &it->second;
}
bool RfidSpoolOverrides::save(const std::map<std::string, RfidSpoolOverride>& values, std::string& error) {
    error.clear();
    if (!m_error.empty()) { error = m_error; return false; }
    try {
        nlohmann::json doc = {{"version", 1}, {"spools", nlohmann::json::object()}};
        for (const auto& [id, value] : values)
            doc["spools"][id] = {{"material_type", value.material_type}, {"color", value.color}, {"profiles", value.profiles}};
        boost::filesystem::create_directories(boost::filesystem::path(m_path).parent_path());
        const auto temporary = m_path + ".tmp";
        boost::nowide::ofstream file(temporary, std::ios::trunc);
        file << doc.dump(2);
        file.flush();
        if (!file) throw std::runtime_error("Could not write RFID overrides");
        file.close();
        if (!file) throw std::runtime_error("Could not close RFID overrides");
        const auto ec = Slic3r::rename_file(temporary, m_path);
        if (ec) throw std::runtime_error(ec.message());
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}
bool RfidSpoolOverrides::set(const std::string& uuid, const RfidSpoolOverride& value, std::string& error) {
    if (!valid_uuid(uuid) || !valid_color(value.color) || value.material_type.empty() || value.profiles.empty() ||
        std::any_of(value.profiles.begin(), value.profiles.end(), [](const auto& pair) { return pair.first.empty() || pair.second.empty(); })) {
        error = "Invalid RFID assignment"; return false;
    }
    auto values = m_values;
    values[key(uuid)] = value;
    if (!save(values, error)) return false;
    m_values = std::move(values);
    return true;
}
bool RfidSpoolOverrides::remove(const std::string& uuid, std::string& error) {
    auto values = m_values;
    values.erase(key(uuid));
    if (!save(values, error)) return false;
    m_values = std::move(values);
    return true;
}
const Preset* resolve_rfid_override(const RfidSpoolOverride& value, PresetBundle& bundle,
                                   const std::string& reported_type, double diameter, std::string& error) {
    error.clear();
    if (reported_type.empty() || value.material_type != reported_type) {
        error = "This prototype requires the assigned filament and RFID tag to have the same base material."; return nullptr;
    }
    const auto& printer = bundle.printers.get_edited_preset();
    auto it = value.profiles.find(printer.name);
    const auto* preset = it == value.profiles.end() ? nullptr : bundle.filaments.find_preset(it->second, false);
    if (!preset || preset->is_default || preset->filament_id.empty() || !preset->is_compatible || preset->config.opt_string("filament_type", 0u) != reported_type) {
        error = "The RFID assignment needs a compatible profile for the selected printer and nozzle."; return nullptr;
    }
    // Do not guess between mixed nozzle/flow variants in the first prototype.
    if (multi_nozzle_diameter_mode_enabled(bundle.project_config)) {
        error = "RFID overrides are not yet supported in mixed nozzle mode."; return nullptr;
    }
    if (diameter > 0) {
        try {
            if (std::abs(std::stod(printer.config.opt_string("printer_variant")) - diameter) > 0.001) {
                error = "The RFID assignment does not match this tray's nozzle diameter."; return nullptr;
            }
        } catch (...) { error = "Cannot determine the tray's nozzle compatibility."; return nullptr; }
    }
    return preset;
}
}} // namespace
