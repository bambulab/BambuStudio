#include "DevicePresetUtil.hpp"

#include "Preset.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

namespace Slic3r {

std::string DevicePresetUtil::FilamentPresetQueryResult::best_preset_name() const
{
    return best_preset ? best_preset->name : std::string();
}

std::vector<std::string> DevicePresetUtil::FilamentPresetQueryResult::compatible_preset_names() const
{
    std::vector<std::string> names;
    names.reserve(compatible_presets.size());
    for (const Preset* candidate : compatible_presets) {
        if (candidate != nullptr)
            names.push_back(candidate->name);
    }
    return names;
}

std::vector<std::string> DevicePresetUtil::FilamentPresetQueryResult::incompatible_preset_names() const
{
    std::vector<std::string> names;
    names.reserve(incompatible_presets.size());
    for (const Preset* candidate : incompatible_presets) {
        if (candidate != nullptr)
            names.push_back(candidate->name);
    }
    return names;
}

namespace {

std::string lower_copy(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

bool equals_ci(const std::string& lhs, const std::string& rhs)
{
    return lower_copy(lhs) == lower_copy(rhs);
}

std::string to_preset_printer_suffix(std::string printer_name)
{
    const std::string bambu_lab = "Bambu Lab ";
    if (printer_name.rfind(bambu_lab, 0) == 0)
        printer_name.replace(0, bambu_lab.size(), "BBL ");
    return printer_name;
}

std::string format_nozzle_diameter(float diameter)
{
    if (diameter <= 0.f)
        return std::string();

    std::ostringstream stream;
    stream << std::fixed << std::setprecision(1) << diameter;
    return stream.str();
}

bool is_compatible_with_printers(const Preset& preset, const std::set<std::string>& printer_names)
{
    if (printer_names.empty())
        return true;

    const auto* compatible = preset.config.option<ConfigOptionStrings>("compatible_printers");
    if (compatible == nullptr || compatible->values.empty())
        return true;

    for (const std::string& printer_name : printer_names)
        if (std::find(compatible->values.begin(), compatible->values.end(), printer_name) != compatible->values.end())
            return true;

    return false;
}

bool longer_preset_name_first(const Preset* lhs, const Preset* rhs)
{
    if (lhs == nullptr || rhs == nullptr)
        return rhs != nullptr;
    if (lhs->name.size() != rhs->name.size())
        return lhs->name.size() > rhs->name.size();
    return lhs->name < rhs->name;
}

FilamentBaseInfo make_info(const Preset& filament_preset)
{
    const auto& config = filament_preset.config;
    FilamentBaseInfo info;
    info.filament_id   = filament_preset.filament_id;
    info.is_system     = filament_preset.is_system;
    info.filament_name = filament_preset.alias.empty() ? filament_preset.name : filament_preset.alias;
    info.setting_id    = filament_preset.setting_id;

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
    if (config.has("temperature_vitrification"))
        info.temperature_vitrification = config.option<ConfigOptionInts>("temperature_vitrification")->values[0];
    if (config.has("filament_printable"))
        info.filament_printable = config.option<ConfigOptionInts>("filament_printable")->values[0];
    if (config.has("filament_extruder_compatibility"))
        info.set_filament_extruder_compatibility(config.option<ConfigOptionInts>("filament_extruder_compatibility")->values[0]);

    return info;
}

DevicePresetUtil::FilamentPresetQueryResult make_result(const Preset& filament_preset,
                                                        const std::vector<const Preset*>& compatible_presets = {},
                                                        const std::vector<const Preset*>& incompatible_presets = {})
{
    DevicePresetUtil::FilamentPresetQueryResult result;
    result.best_preset          = &filament_preset;
    result.best_preset_info     = make_info(filament_preset);
    result.compatible_presets   = compatible_presets;
    result.incompatible_presets = incompatible_presets;
    return result;
}

DevicePresetUtil::FilamentPresetQueryResult make_result(const FilamentBaseInfo& filament_info,
                                                        const Preset* filament_preset,
                                                        const std::vector<const Preset*>& compatible_presets,
                                                        const std::vector<const Preset*>& incompatible_presets)
{
    DevicePresetUtil::FilamentPresetQueryResult result;
    result.best_preset          = filament_preset;
    result.best_preset_info     = filament_info;
    result.compatible_presets   = compatible_presets;
    result.incompatible_presets = incompatible_presets;
    return result;
}

struct PrinterNameScope
{
    std::set<std::string> printer_name_with_nozzles;
    std::set<std::string> printer_names;
};

std::string strip_nozzle_suffix(std::string printer_name)
{
    const size_t nozzle_pos = printer_name.find(" nozzle");
    if (nozzle_pos == std::string::npos)
        return printer_name;

    const size_t diameter_pos = printer_name.rfind(' ', nozzle_pos - 1);
    if (diameter_pos == std::string::npos)
        return printer_name;

    printer_name.erase(diameter_pos);
    return printer_name;
}

std::string resolve_printer_model_name(const PresetBundle& bundle, const std::string& printer_type)
{
    for (const auto& vendor_profile : bundle.vendors) {
        for (const auto& vendor_model : vendor_profile.second.models) {
            if (vendor_model.model_id == printer_type)
                return vendor_model.name;
        }
    }
    return printer_type;
}

PrinterNameScope resolve_printer_names(const PresetBundle& bundle, const DevicePresetUtil::FilamentPresetQuery& query)
{
    PrinterNameScope scope;
    if (query.printer_type.empty()) {
        return scope;
    }

    const std::string base_printer_name = resolve_printer_model_name(bundle, query.printer_type);

    scope.printer_names.insert(base_printer_name);
    for (auto printer_it = bundle.printers.begin(); printer_it != bundle.printers.end(); ++printer_it) {
        if (!printer_it->is_system) {
            continue;
        }

        const ConfigOption* printer_model_opt = printer_it->config.option("printer_model");
        const ConfigOptionString* printer_model_str = dynamic_cast<const ConfigOptionString*>(printer_model_opt);
        if (!printer_model_str) {
            continue;
        }

        if (printer_model_str->value != base_printer_name) {
            continue;
        }

        scope.printer_names.insert(strip_nozzle_suffix(printer_it->name));
        for (float diameter : query.nozzle_diameters) {
            const std::string nozzle_diameter = format_nozzle_diameter(diameter);
            if (!nozzle_diameter.empty() && printer_it->name.find(nozzle_diameter) != std::string::npos) {
                scope.printer_name_with_nozzles.insert(printer_it->name);
            }
        }
    }

    return scope;
}

const Preset* find_by_name(const PresetBundle& bundle,
                           const std::string& name,
                           const std::set<std::string>& compatible_printers,
                           bool only_system,
                           bool require_compatible)
{
    for (auto it = bundle.filaments.begin(); it != bundle.filaments.end(); ++it) {
        const Preset& preset = *it;
        if (only_system && !preset.is_system)
            continue;
        if (!equals_ci(preset.name, name))
            continue;
        if (require_compatible && !is_compatible_with_printers(preset, compatible_printers))
            continue;
        return &preset;
    }
    return nullptr;
}

const Preset* find_preset_by_filament_info(const PresetBundle& bundle,
                                           const FilamentBaseInfo& filament_info,
                                           bool only_system)
{
    if (!filament_info.setting_id.empty()) {
        for (auto it = bundle.filaments.begin(); it != bundle.filaments.end(); ++it) {
            const Preset& preset = *it;
            if (only_system && !preset.is_system)
                continue;
            if (preset.setting_id == filament_info.setting_id)
                return &preset;
        }
    }

    for (auto it = bundle.filaments.begin(); it != bundle.filaments.end(); ++it) {
        const Preset& preset = *it;
        if (only_system && !preset.is_system)
            continue;
        if (preset.filament_id == filament_info.filament_id)
            return &preset;
    }
    return nullptr;
}

void collect_by_filament_id(const PresetBundle& bundle,
                            const DevicePresetUtil::FilamentPresetQuery& query,
                            const PrinterNameScope& printer_names,
                            std::vector<const Preset*>& precise_presets,
                            std::vector<const Preset*>& rough_presets,
                            std::vector<const Preset*>& compatible_presets,
                            std::vector<const Preset*>& incompatible_presets)
{
    precise_presets.clear();
    rough_presets.clear();
    compatible_presets.clear();
    incompatible_presets.clear();
    if (query.filament_id.empty()) {
        return;
    }

    for (auto it = bundle.filaments.begin(); it != bundle.filaments.end(); ++it) {
        const Preset& preset = *it;
        if (query.only_system && !preset.is_system)
            continue;
        if (preset.filament_id == query.filament_id) {
            if (!printer_names.printer_name_with_nozzles.empty() && is_compatible_with_printers(preset, printer_names.printer_name_with_nozzles))
                precise_presets.push_back(&preset);
            else if (!printer_names.printer_names.empty() && is_compatible_with_printers(preset, printer_names.printer_names))
                rough_presets.push_back(&preset);
            else
                incompatible_presets.push_back(&preset);
        }
    }

    std::sort(precise_presets.begin(), precise_presets.end(), longer_preset_name_first);
    std::sort(rough_presets.begin(), rough_presets.end(), longer_preset_name_first);
    std::sort(incompatible_presets.begin(), incompatible_presets.end(), longer_preset_name_first);
    compatible_presets.reserve(precise_presets.size() + rough_presets.size());
    compatible_presets.insert(compatible_presets.end(), precise_presets.begin(), precise_presets.end());
    compatible_presets.insert(compatible_presets.end(), rough_presets.begin(), rough_presets.end());
}

const Preset* find_by_base_and_suffix(const PresetBundle& bundle,
                                      const std::string& base_name,
                                      const std::string& suffix,
                                      const std::set<std::string>& compatible_printers,
                                      bool only_system,
                                      bool require_compatible)
{
    if (base_name.empty() || suffix.empty())
        return nullptr;
    return find_by_name(bundle, base_name + " @" + suffix, compatible_printers, only_system, require_compatible);
}

std::optional<DevicePresetUtil::FilamentPresetQueryResult> find_fallback_by_base_name(const PresetBundle& bundle,
                                                                                      const DevicePresetUtil::FilamentPresetQuery& query,
                                                                                      const std::string& base_name,
                                                                                      const PrinterNameScope& printer_names,
                                                                                      const std::vector<const Preset*>& compatible_presets,
                                                                                      const std::vector<const Preset*>& incompatible_presets)
{
    if (base_name.empty())
        return std::nullopt;

    for (const std::string& printer_name : printer_names.printer_name_with_nozzles) {
        if (const Preset* preset = find_by_base_and_suffix(bundle, base_name, to_preset_printer_suffix(printer_name),
                                                           printer_names.printer_name_with_nozzles, query.only_system, true))
            return make_result(*preset, compatible_presets, incompatible_presets);
    }

    for (const std::string& printer_name : printer_names.printer_names) {
        if (const Preset* preset = find_by_base_and_suffix(bundle, base_name, to_preset_printer_suffix(printer_name),
                                                           printer_names.printer_names, query.only_system, true))
            return make_result(*preset, compatible_presets, incompatible_presets);
    }

    if (const Preset* preset = find_by_name(bundle, base_name + " @base", {}, query.only_system, false))
        return make_result(*preset, compatible_presets, incompatible_presets);

    return std::nullopt;
}

std::optional<DevicePresetUtil::FilamentPresetQueryResult> get_filament_by_filament_id(const PresetBundle& bundle,
                                                                                       const DevicePresetUtil::FilamentPresetQuery& query,
                                                                                       const PrinterNameScope& printer_names,
                                                                                       const std::vector<const Preset*>& compatible_presets,
                                                                                       const std::vector<const Preset*>& incompatible_presets)
{
    auto make_query_result = [&](const FilamentBaseInfo& filament_info) {
        const Preset* preset = find_preset_by_filament_info(bundle, filament_info, query.only_system);
        return make_result(filament_info, preset, compatible_presets, incompatible_presets);
    };

    for (const std::string& printer_name : printer_names.printer_name_with_nozzles) {
        if (auto filament_info = bundle.get_filament_by_filament_id(query.filament_id, printer_name, query.only_system))
            return make_query_result(*filament_info);
    }

    for (const std::string& printer_name : printer_names.printer_names) {
        if (auto filament_info = bundle.get_filament_by_filament_id(query.filament_id, printer_name, query.only_system))
            return make_query_result(*filament_info);
    }

    if (printer_names.printer_name_with_nozzles.empty() && printer_names.printer_names.empty()) {
        if (auto filament_info = bundle.get_filament_by_filament_id(query.filament_id, std::string(), query.only_system))
            return make_query_result(*filament_info);
    }

    return std::nullopt;
}

std::optional<DevicePresetUtil::FilamentPresetQueryResult> get_generic_filament(const PresetBundle& bundle,
                                                                                const DevicePresetUtil::FilamentPresetQuery& query,
                                                                                const PrinterNameScope& printer_names,
                                                                                const std::vector<const Preset*>& compatible_presets,
                                                                                const std::vector<const Preset*>& incompatible_presets)
{
    if (query.filament_type.empty())
        return std::nullopt;

    return find_fallback_by_base_name(bundle, query, "Generic " + query.filament_type, printer_names,
                                      compatible_presets, incompatible_presets);
}

} // namespace

DevicePresetUtil::FilamentPresetQueryResult DevicePresetUtil::query_filament_preset(const PresetBundle& bundle,
                                                                                    const FilamentPresetQuery& query)
{
    const PrinterNameScope printer_names = resolve_printer_names(bundle, query);

    // Step 1: collect every preset sharing the same filament_id, grouped by current printer compatibility.
    std::vector<const Preset*> precise_presets;
    std::vector<const Preset*> rough_presets;
    std::vector<const Preset*> compatible_presets;
    std::vector<const Preset*> incompatible_presets;
    collect_by_filament_id(bundle, query, printer_names, precise_presets, rough_presets, compatible_presets, incompatible_presets);

    // Step 2: prefer exact printer + nozzle matches.
    if (!precise_presets.empty())
        return make_result(*precise_presets.front(), compatible_presets, incompatible_presets);

    // Step 3: fall back to printer-level matches.
    if (!rough_presets.empty())
        return make_result(*rough_presets.front(), compatible_presets, incompatible_presets);

    // Step 4: fall back to vendor/type preset-name lookup, then Generic <type>.
    if (query.with_fallback) {
        if (auto result = get_filament_by_filament_id(bundle, query, printer_names, compatible_presets, incompatible_presets))
            return *result;

        if (auto result = get_generic_filament(bundle, query, printer_names, compatible_presets, incompatible_presets))
            return *result;
    }

    FilamentPresetQueryResult result;
    result.compatible_presets   = compatible_presets;
    result.incompatible_presets = incompatible_presets;
    return result;
}

std::optional<FilamentBaseInfo> DevicePresetUtil::query_best_filament_preset(const PresetBundle& bundle,
                                                                             const FilamentPresetQuery& query)
{
    auto result = query_filament_preset(bundle, query);
    return !result.best_preset_info.filament_id.empty() ? std::optional<FilamentBaseInfo>(result.best_preset_info) : std::nullopt;
}

} // namespace Slic3r
