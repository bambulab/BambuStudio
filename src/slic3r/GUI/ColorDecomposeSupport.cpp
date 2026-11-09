#include "ColorDecomposeSupport.hpp"
#include "FilamentBitmapUtils.hpp"
#include "GUI_App.hpp"
#include "MsgDialog.hpp"
#include "Plater.hpp"
#include "I18N.hpp"
#include "format.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/LocalesUtils.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"

#include "nlohmann/json.hpp"

#include <fstream>
#include <algorithm>
#include <cctype>
#include <limits>
#include <utility>

using json = nlohmann::json;

namespace Slic3r { namespace GUI {

std::string decompose_normalize_color_hex(std::string color)
{
    if (color.size() >= 7)
        color = color.substr(0, 7);
    std::transform(color.begin(), color.end(), color.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return color;
}

const char* decompose_base_color_en(DecomposeBaseColor color)
{
    switch (color) {
    case DecomposeBaseColor::Cyan:    return "Cyan";
    case DecomposeBaseColor::Magenta: return "Magenta";
    case DecomposeBaseColor::Yellow:  return "Yellow";
    case DecomposeBaseColor::White:   return "White";
    case DecomposeBaseColor::Red:     return "Red";
    case DecomposeBaseColor::Green:   return "Green";
    case DecomposeBaseColor::Blue:    return "Blue";
    case DecomposeBaseColor::Black:   return "Black";
    default:                          return "";
    }
}

DecomposeColorBlockReason decompose_color_block_reason(int filament_idx)
{
    std::vector<wxColour> colors;
    bool is_gradient = false;
    get_filament_colors_by_id(filament_idx, colors, is_gradient);
    if (colors.size() >= 2 && is_gradient)
        return DecomposeColorBlockReason::Gradient;
    for (const wxColour& c : colors) {
        if (c.IsOk() && c.Alpha() != wxALPHA_OPAQUE)
            return DecomposeColorBlockReason::Transparent;
    }
    if (colors.size() >= 2)
        return DecomposeColorBlockReason::MultiColor;
    return DecomposeColorBlockReason::None;
}

std::string decompose_basic_filament_id(const std::string& basic_type)
{
    if (basic_type == kDecomposePetgBasicType)
        return kDecomposePetgFilamentId;
    if (basic_type == kDecomposePlaPaletteType)
        return kDecomposePlaPaletteFilamentId;
    return kDecomposePlaFilamentId;
}

void set_created_standard_component_metadata(size_t config_idx, const DecomposeOfficialComponent& component)
{
    auto& project_config = wxGetApp().preset_bundle->project_config;
    if (!component.filament_id.empty()) {
        if (auto* filament_id_opt = project_config.option<ConfigOptionStrings>("filament_id")) {
            while (filament_id_opt->values.size() <= config_idx)
                filament_id_opt->values.push_back("");
            filament_id_opt->values[config_idx] = component.filament_id;
        }
    }

    // PLA Palette is a PLA series, so it shares the plain "PLA" filament_type.
    const std::string type = component.filament_id == kDecomposePetgFilamentId ? kDecomposePetgShortType :
                             (component.filament_id == kDecomposePlaFilamentId ||
                              component.filament_id == kDecomposePlaPaletteFilamentId) ? kDecomposePlaShortType : "";
    if (!type.empty()) {
        if (auto* type_opt = project_config.option<ConfigOptionStrings>("filament_type")) {
            while (type_opt->values.size() <= config_idx)
                type_opt->values.push_back("");
            type_opt->values[config_idx] = type;
        }
    }
}

DecomposeOfficialComponent lookup_decompose_official_component(
    const std::string& basic_type,
    DecomposeBaseColor base_color,
    const wxColour& fallback)
{
    DecomposeOfficialComponent result;
    result.base_color  = base_color;
    result.color_hex   = decompose_normalize_color_hex(fallback.GetAsString(wxC2S_HTML_SYNTAX).ToStdString());
    result.filament_id = decompose_basic_filament_id(basic_type);

    const char* color_name = decompose_base_color_en(base_color);
    if (color_name[0] == '\0')
        return result;

    // Some materials name a standard base color differently in the color-code
    // table. PETG Basic's RYBW blue base is "Reflex Blue" (deep blue, B00,
    // #001489), not "Blue". Match by an ordered list of exact English names so
    // "Navy Blue" (B01, #0086D6) is never picked up by mistake.
    std::vector<std::string> candidate_names;
    candidate_names.emplace_back(color_name);
    if (base_color == DecomposeBaseColor::Blue && basic_type == kDecomposePetgBasicType)
        candidate_names.emplace_back("Reflex Blue");

    static json s_color_codes;
    static bool s_color_codes_ok = false;
    if (!s_color_codes_ok) {
        std::ifstream ifs(resources_dir() + "/profiles/BBL/filament/filaments_color_codes.json");
        if (!ifs)
            return result;
        json parsed = json::parse(ifs, nullptr, false);
        if (parsed.is_discarded() || !parsed.contains("data") || !parsed["data"].is_array())
            return result;
        s_color_codes = std::move(parsed);
        s_color_codes_ok = true;
    }

    for (const std::string& candidate : candidate_names) {
        for (const auto& item : s_color_codes["data"]) {
            if (!item.is_object() || item.value("fila_type", "") != basic_type)
                continue;
            if (!item.contains("fila_color_name"))
                continue;
            const auto& names = item["fila_color_name"];
            if (!names.is_object() || names.value("en", "") != candidate)
                continue;
            if (item.contains("fila_color") && item["fila_color"].is_array() && !item["fila_color"].empty())
                result.color_hex = decompose_normalize_color_hex(item["fila_color"][0].get<std::string>());
            result.filament_id = item.value("fila_id", result.filament_id);
            return result;
        }
    }
    return result;
}

std::string find_decompose_standard_preset_name(size_t source_config_idx, const std::string& basic_type)
{
    const PresetBundle& preset_bundle = *wxGetApp().preset_bundle;
    if (source_config_idx < preset_bundle.filament_presets.size()) {
        const std::string& source_name = preset_bundle.filament_presets[source_config_idx];
        if (source_name.find(std::string(kDecomposeBambuPresetPrefix) + basic_type) != std::string::npos)
            return source_name;
    }

    const std::string prefix = std::string(kDecomposeBambuPresetPrefix) + basic_type + " @BBL ";
    for (const std::string& preset_name : preset_bundle.filament_presets) {
        if (preset_name.find(prefix) == 0)
            return preset_name;
    }

    // The series may be absent from the project entirely -- picking "Bambu PLA
    // Palette" under the standard-mode recommendations does not require a
    // palette slot to exist. Fall back to the preset library, restricted to what
    // is compatible with the active printer.
    for (const Preset& preset : preset_bundle.filaments.get_presets()) {
        if (preset.is_system && preset.is_visible && preset.is_compatible && preset.name.find(prefix) == 0)
            return preset.name;
    }

    return {};
}

static bool is_official_series_preset(const Preset& preset, const std::string& basic_type)
{
    if (!preset.is_system || basic_type.empty())
        return false;
    const std::string series = std::string(kDecomposeBambuPresetPrefix) + basic_type;
    if (preset.name != series && preset.name.find(series + " @") != 0)
        return false;
    // Skip the non-instantiated base profile (e.g. "Bambu PLA Basic @base").
    return preset.name.find("@base") == std::string::npos;
}

bool ensure_official_series_installed(const std::string& basic_type)
{
    if (basic_type.empty() || !wxGetApp().preset_bundle)
        return false;

    PresetBundle& preset_bundle = *wxGetApp().preset_bundle;
    std::vector<Preset*> series;
    bool has_compatible = false;
    for (Preset& preset : preset_bundle.filaments) {
        if (!is_official_series_preset(preset, basic_type))
            continue;
        series.push_back(&preset);
        if (preset.is_compatible)
            has_compatible = true;
    }
    if (!has_compatible)
        return false;

    AppConfig* app_config = wxGetApp().app_config;
    for (Preset* preset : series) {
        if (preset->is_visible)
            continue;
        preset->is_visible = true;
        if (app_config)
            app_config->set(AppConfig::SECTION_FILAMENTS, preset->name, "true");
    }
    return true;
}


std::string official_basic_type_from_preset_name(const std::string& preset_name)
{
    if (preset_name.find(std::string(kDecomposeBambuPresetPrefix) + kDecomposePlaPaletteType) != std::string::npos)
        return kDecomposePlaPaletteType;
    if (preset_name.find(std::string(kDecomposeBambuPresetPrefix) + kDecomposePlaBasicType) != std::string::npos)
        return kDecomposePlaBasicType;
    if (preset_name.find(std::string(kDecomposeBambuPresetPrefix) + kDecomposePetgBasicType) != std::string::npos)
        return kDecomposePetgBasicType;
    return {};
}

std::string filament_type_for_color_decompose(Preset* preset)
{
    if (!preset)
        return kDecomposePlaShortType;

    std::string display_type;
    std::string ft = preset->config.get_filament_type(display_type);
    const std::string basic = official_basic_type_from_preset_name(preset->name);
    if (!basic.empty())
        ft = basic;
    if (ft.empty())
        ft = kDecomposePlaShortType;
    return ft;
}

static bool is_official_decompose_filament_id(const std::string& id)
{
    return id == kDecomposePlaFilamentId
        || id == kDecomposePlaPaletteFilamentId
        || id == kDecomposePetgFilamentId;
}

int find_existing_decompose_component(
    const DecomposeOfficialComponent& component,
    const std::vector<std::string>& physical_colors,
    const std::vector<size_t>& physical_config_indices)
{
    PresetBundle& preset_bundle = *wxGetApp().preset_bundle;
    const size_t num_physical = physical_colors.size();
    const std::string expected_basic_type = component.filament_id == kDecomposePetgFilamentId ? kDecomposePetgBasicType :
                                            component.filament_id == kDecomposePlaPaletteFilamentId ? kDecomposePlaPaletteType :
                                            component.filament_id == kDecomposePlaFilamentId ? kDecomposePlaBasicType : "";
    // PLA Palette shares filament_type "PLA" with PLA Basic, so it has no short
    // type here: matching on "PLA" would let the two series reuse each other's
    // slots. Identify Palette by filament_id, official series name, and preset
    // name. expected_short_type is empty for Palette on purpose; the type check
    // below must not treat empty == empty as a match, or any same-color slot
    // (e.g. PETG after the user switched 1-5) would be reused.
    const std::string expected_short_type = expected_basic_type == kDecomposePetgBasicType ? kDecomposePetgShortType :
                                            expected_basic_type == kDecomposePlaBasicType ? kDecomposePlaShortType : "";
    const std::string expected_preset_part = expected_basic_type.empty() ? "" : std::string(kDecomposeBambuPresetPrefix) + expected_basic_type;
    for (size_t i = 0; i < num_physical && i < physical_config_indices.size(); ++i) {
        const size_t config_idx = physical_config_indices[i];
        const std::string slot_color = decompose_normalize_color_hex(physical_colors[i]);
        const std::string preset_name = config_idx < preset_bundle.filament_presets.size() ? preset_bundle.filament_presets[config_idx] : "";
        // filament_id / filament_type are preset fields, not project_config keys.
        // Read the live preset so a later type switch (PLA Palette -> PETG) is visible.
        Preset* preset = preset_name.empty() ? nullptr : preset_bundle.filaments.find_preset(preset_name);
        const std::string slot_filament_id = (preset && !preset->filament_id.empty()) ? preset->filament_id : "";
        const std::string slot_type = filament_type_for_color_decompose(preset);
        if (slot_color != component.color_hex) {
            continue;
        }

        // Two official series must never reuse each other's slots, even when the
        // colors happen to be identical.
        if (!component.filament_id.empty() && !slot_filament_id.empty()
            && slot_filament_id != component.filament_id
            && is_official_decompose_filament_id(slot_filament_id)) {
            continue;
        }

        if (!component.filament_id.empty() && slot_filament_id == component.filament_id) {
            return static_cast<int>(config_idx + 1);
        }

        const bool type_matches =
            (!expected_basic_type.empty() && slot_type == expected_basic_type) ||
            (!expected_short_type.empty() && slot_type == expected_short_type);
        if (type_matches) {
            return static_cast<int>(config_idx + 1);
        }

        if (!expected_preset_part.empty() && preset_name.find(expected_preset_part) != std::string::npos) {
            return static_cast<int>(config_idx + 1);
        }

        const bool has_material_hint = !slot_filament_id.empty() || !slot_type.empty() || !preset_name.empty();
        if (!expected_basic_type.empty() && has_material_hint)
            continue;

        return static_cast<int>(config_idx + 1);
    }
    return -1;
}

std::vector<DecomposeOfficialComponent> pla_palette_cmykw_official_components()
{
    static const DecomposeBaseColor kBases[] = {
        DecomposeBaseColor::White,
        DecomposeBaseColor::Cyan,
        DecomposeBaseColor::Magenta,
        DecomposeBaseColor::Yellow,
        DecomposeBaseColor::Black
    };
    static const wxColour kFallbacks[] = {
        wxColour(255, 255, 255),
        wxColour(0, 144, 218),
        wxColour(226, 69, 133),
        wxColour(252, 227, 0),
        wxColour(0, 0, 0)
    };

    std::vector<DecomposeOfficialComponent> out;
    out.reserve(5);
    for (size_t i = 0; i < 5; ++i)
        out.push_back(lookup_decompose_official_component(kDecomposePlaPaletteType, kBases[i], kFallbacks[i]));
    return out;
}

bool OfficialMixingKitGroup::complete() const
{
    return !config_indices.empty()
        && std::find(config_indices.begin(), config_indices.end(), -1) == config_indices.end();
}

size_t OfficialMixingKitGroup::present_count() const
{
    return size_t(std::count_if(config_indices.begin(), config_indices.end(), [](int idx) { return idx >= 0; }));
}

namespace {

struct ProjectPhysicalFilaments {
    std::vector<std::string> colors;
    std::vector<size_t>      indices;
};

ProjectPhysicalFilaments collect_project_physical_filaments()
{
    ProjectPhysicalFilaments out;
    PresetBundle& pb = *wxGetApp().preset_bundle;
    auto* colours_opt = pb.project_config.option<ConfigOptionStrings>("filament_colour");
    auto* is_mixed_opt = pb.project_config.option<ConfigOptionBools>("filament_is_mixed");
    for (size_t i = 0; i < pb.filament_presets.size(); ++i) {
        if (is_mixed_opt && i < is_mixed_opt->values.size() && is_mixed_opt->values[i])
            continue;
        out.colors.push_back(colours_opt && i < colours_opt->values.size() ? colours_opt->values[i] : "#808080");
        out.indices.push_back(i);
    }
    return out;
}

bool official_kit_diameter_mode_enabled(const PresetBundle& pb)
{
    return is_single_ext_multi_nozzle(pb.printers.get_edited_preset().config) && multi_nozzle_diameter_mode_enabled(pb.project_config);
}

std::string official_kit_effective_diameter(const PresetBundle& pb, size_t config_idx)
{
    if (!official_kit_diameter_mode_enabled(pb))
        return {};
    const auto* selected = pb.project_config.option<ConfigOptionStrings>("filament_diameter_map");
    std::string diameter = (selected && config_idx < selected->values.size()) ? selected->values[config_idx] : std::string();
    if (diameter.empty()) {
        const std::vector<std::string> options = pb.get_filament_nozzle_diameter_options(config_idx);
        if (!options.empty())
            diameter = options.front();
    }
    if (diameter.empty())
        return {};
    return format_diameter_to_str(string_to_double_decimal_point(diameter));
}

std::pair<int, double> official_kit_diameter_rank(const std::string& diameter)
{
    if (diameter.empty())
        return {5, 0.0};
    const double value = string_to_double_decimal_point(diameter);
    const std::string normalized = format_diameter_to_str(value);
    if (normalized == "0.4")
        return {0, value};
    if (normalized == "0.2")
        return {1, value};
    if (normalized == "0.6")
        return {2, value};
    if (normalized == "0.8")
        return {3, value};
    return {4, value > 0.0 ? value : std::numeric_limits<double>::max()};
}

bool official_kit_group_better(const OfficialMixingKitGroup& lhs, const OfficialMixingKitGroup& rhs)
{
    const size_t lhs_count = lhs.present_count();
    const size_t rhs_count = rhs.present_count();
    if (lhs_count != rhs_count)
        return lhs_count > rhs_count;
    return official_kit_diameter_rank(lhs.diameter) < official_kit_diameter_rank(rhs.diameter);
}

const OfficialMixingKitGroup* find_best_partial_official_mixing_kit(const std::vector<OfficialMixingKitGroup>& groups)
{
    const OfficialMixingKitGroup* best = nullptr;
    for (const OfficialMixingKitGroup& group : groups) {
        if (!best || official_kit_group_better(group, *best))
            best = &group;
    }
    return best;
}

std::string choose_new_official_kit_diameter(const PresetBundle& pb, const std::vector<OfficialMixingKitGroup>& groups)
{
    if (!official_kit_diameter_mode_enabled(pb))
        return {};
    if (const OfficialMixingKitGroup* best = find_best_partial_official_mixing_kit(groups))
        return best->diameter;

    std::string best_diameter;
    for (const auto& variant : pb.get_installed_nozzle_variants()) {
        if (best_diameter.empty() || official_kit_diameter_rank(variant.first) < official_kit_diameter_rank(best_diameter))
            best_diameter = variant.first;
    }
    return best_diameter.empty() ? std::string("0.4") : best_diameter;
}

std::vector<DecomposeOfficialComponent> missing_official_mixing_kit_components(
    const std::vector<OfficialMixingKitGroup>& groups,
    std::set<size_t>* existing_indices = nullptr,
    std::string* target_diameter = nullptr)
{
    std::vector<DecomposeOfficialComponent> components = pla_palette_cmykw_official_components();
    PresetBundle& pb = *wxGetApp().preset_bundle;
    if (const OfficialMixingKitGroup* complete = find_complete_official_mixing_kit(groups)) {
        if (existing_indices) {
            for (int idx : complete->config_indices)
                existing_indices->insert(size_t(idx));
        }
        if (target_diameter)
            *target_diameter = complete->diameter;
        return {};
    }

    const OfficialMixingKitGroup* base = find_best_partial_official_mixing_kit(groups);
    if (target_diameter)
        *target_diameter = choose_new_official_kit_diameter(pb, groups);

    std::vector<DecomposeOfficialComponent> missing;
    for (size_t i = 0; i < components.size(); ++i) {
        const int existing = (base && i < base->config_indices.size()) ? base->config_indices[i] : -1;
        if (existing >= 0) {
            if (existing_indices)
                existing_indices->insert(size_t(existing));
            continue;
        }
        missing.push_back(components[i]);
    }
    return missing;
}

} // namespace

std::vector<OfficialMixingKitGroup> collect_official_mixing_kit_groups(const std::set<size_t>& excluded_indices)
{
    std::vector<OfficialMixingKitGroup> groups;
    if (!wxGetApp().preset_bundle)
        return groups;

    PresetBundle& pb = *wxGetApp().preset_bundle;
    const ProjectPhysicalFilaments physical = collect_project_physical_filaments();
    const std::vector<DecomposeOfficialComponent> components = pla_palette_cmykw_official_components();
    for (size_t i = 0; i < physical.indices.size(); ++i) {
        const size_t cfg_idx = physical.indices[i];
        if (excluded_indices.count(cfg_idx) > 0)
            continue;
        if (decompose_color_block_reason(int(cfg_idx)) != DecomposeColorBlockReason::None)
            continue;

        const std::string& preset_name = cfg_idx < pb.filament_presets.size() ? pb.filament_presets[cfg_idx] : std::string();
        Preset* preset = preset_name.empty() ? nullptr : pb.filaments.find_preset(preset_name);
        const bool is_palette = (preset && preset->filament_id == kDecomposePlaPaletteFilamentId)
            || official_basic_type_from_preset_name(preset_name) == kDecomposePlaPaletteType;
        if (!is_palette)
            continue;

        const std::string color_hex = decompose_normalize_color_hex(physical.colors[i]);
        const auto component = std::find_if(components.begin(), components.end(),
            [&](const DecomposeOfficialComponent& official) { return official.color_hex == color_hex; });
        if (component == components.end())
            continue;

        const std::string diameter = official_kit_effective_diameter(pb, cfg_idx);
        auto group = std::find_if(groups.begin(), groups.end(),
            [&](const OfficialMixingKitGroup& g) { return g.diameter == diameter; });
        if (group == groups.end()) {
            groups.push_back({diameter, std::vector<int>(components.size(), -1)});
            group = std::prev(groups.end());
        }
        int& existing = group->config_indices[size_t(component - components.begin())];
        if (existing < 0)
            existing = int(cfg_idx);
    }
    return groups;
}

const OfficialMixingKitGroup* find_complete_official_mixing_kit(const std::vector<OfficialMixingKitGroup>& groups)
{
    const OfficialMixingKitGroup* best = nullptr;
    for (const OfficialMixingKitGroup& group : groups) {
        if (!group.complete())
            continue;
        if (!best || official_kit_diameter_rank(group.diameter) < official_kit_diameter_rank(best->diameter))
            best = &group;
    }
    return best;
}

OfficialMixingKitAddPlan plan_official_mixing_kit_addition(size_t reserve_slots,
                                                           const std::set<size_t>& protected_indices)
{
    OfficialMixingKitAddPlan plan;
    if (!wxGetApp().plater() || !wxGetApp().preset_bundle) {
        plan.can_add = false;
        return plan;
    }

    PresetBundle& pb = *wxGetApp().preset_bundle;
    const ProjectPhysicalFilaments physical = collect_project_physical_filaments();
    std::set<size_t> keep = protected_indices;
    plan.missing_count = missing_official_mixing_kit_components(collect_official_mixing_kit_groups(), &keep).size();
    if (plan.missing_count == 0) {
        plan.kit_already_present = true;
        return plan;
    }

    const size_t physical_after = physical.indices.size() + plan.missing_count;
    const size_t total_after = pb.filament_presets.size() + plan.missing_count + reserve_slots;
    const size_t physical_over = physical_after > MAX_PHYSICAL_FILAMENT_COUNT ? physical_after - MAX_PHYSICAL_FILAMENT_COUNT : 0;
    const size_t total_over = total_after > MAX_FILAMENT_COUNT ? total_after - MAX_FILAMENT_COUNT : 0;
    plan.required_delete_count = std::max(physical_over, total_over);
    if (plan.required_delete_count == 0)
        return plan;

    Sidebar& sidebar = wxGetApp().plater()->sidebar();
    // Delete from the end so earlier indices in this plan remain valid while deleting.
    for (size_t i = physical.indices.size(); i-- > 0 && plan.delete_indices.size() < plan.required_delete_count;) {
        const size_t cfg_idx = physical.indices[i];
        if (keep.count(cfg_idx) > 0 || sidebar.is_filament_referenced(cfg_idx))
            continue;
        plan.delete_indices.push_back(cfg_idx);
        plan.delete_colors.push_back(physical.colors[i]);
    }
    plan.can_add = plan.delete_indices.size() == plan.required_delete_count;
    if (!plan.can_add) {
        plan.delete_indices.clear();
        plan.delete_colors.clear();
    }
    return plan;
}

bool add_official_mixing_kit_to_project(wxWindow* parent, const OfficialMixingKitAddPlan& plan, size_t reserve_slots)
{
    if (!wxGetApp().plater() || !wxGetApp().preset_bundle || !plan.can_add)
        return false;

    ensure_official_series_installed(kDecomposePlaPaletteType);
    const std::string preset_name = find_decompose_standard_preset_name(0, kDecomposePlaPaletteType);
    if (preset_name.empty()) {
        MessageDialog dlg(parent,
            _L("The current printer has no compatible PLA Palette filament."),
            _L("Error"), wxOK | wxICON_WARNING);
        dlg.ShowModal();
        return false;
    }

    Sidebar& sidebar = wxGetApp().plater()->sidebar();
    {
        wxBusyCursor wait;
        for (size_t cfg_idx : plan.delete_indices)
            sidebar.delete_filament(cfg_idx, -1);
    }

    PresetBundle& pb = *wxGetApp().preset_bundle;
    const ProjectPhysicalFilaments physical = collect_project_physical_filaments();
    const std::vector<OfficialMixingKitGroup> groups = collect_official_mixing_kit_groups();
    std::string target_diameter;
    const std::vector<DecomposeOfficialComponent> missing =
        missing_official_mixing_kit_components(groups, nullptr, &target_diameter);
    if (missing.empty())
        return true;
    if (physical.indices.size() + missing.size() > MAX_PHYSICAL_FILAMENT_COUNT ||
        pb.filament_presets.size() + missing.size() + reserve_slots > MAX_FILAMENT_COUNT)
        return false;

    std::vector<std::pair<wxColour, std::string>> to_add;
    to_add.reserve(missing.size());
    for (const DecomposeOfficialComponent& official : missing) {
        wxColour col(official.color_hex);
        if (!col.IsOk())
            col = *wxWHITE;
        to_add.emplace_back(col, preset_name);
    }

    int inherit_nozzle_from = -1;
    if (const OfficialMixingKitGroup* base = find_best_partial_official_mixing_kit(groups)) {
        if (base->diameter == target_diameter) {
            const auto existing = std::find_if(base->config_indices.begin(), base->config_indices.end(),
                [](int idx) { return idx >= 0; });
            if (existing != base->config_indices.end())
                inherit_nozzle_from = *existing;
        }
    }

    wxBusyCursor wait;
    const size_t insert_pos = sidebar.add_custom_filaments(to_add, inherit_nozzle_from);
    const size_t added = (insert_pos == size_t(-1)) ? 0 :
        std::min(to_add.size(), sidebar.combos_filament().size() - insert_pos);
    for (size_t i = 0; i < added; ++i)
        set_created_standard_component_metadata(insert_pos + i, missing[i]);

    // A new kit has no existing slot to inherit nozzle settings from.
    if (inherit_nozzle_from < 0 && !target_diameter.empty() && official_kit_diameter_mode_enabled(pb)) {
        for (size_t i = 0; i < added; ++i)
            pb.set_filament_nozzle_diameter(insert_pos + i, target_diameter);
        pb.validate_filament_nozzle_diameter_selections();
        sidebar.refresh_filament_diameter_buttons();
    }
    return added == missing.size();
}

}} // namespace Slic3r::GUI
