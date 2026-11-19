#include "SupportRecommendedParams.hpp"

#include "libslic3r.h"
#include "Utils.hpp"
#include "nlohmann/json.hpp"

#include <algorithm>
#include <cmath>
#include <tuple>
#include <boost/algorithm/string/trim.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>

namespace Slic3r {

std::string support_recommended_material_name(std::string preset_name)
{
    if (size_t pos = preset_name.find('@'); pos != std::string::npos)
        preset_name.erase(pos);
    boost::trim_right(preset_name);
    return preset_name;
}

bool is_tpu_filament_type(const std::string &filament_type)
{
    return filament_type == "TPU" || filament_type == "TPU-AMS";
}

bool unsuitable_as_support_base(const std::string &support_type, const std::string &model_type)
{
    return (support_type == "PLA" && (model_type == "PETG" || is_tpu_filament_type(model_type))) ||
           (support_type == "PETG" && model_type == "PLA");
}

namespace {

using SupportRecommendedParamsTable = std::vector<SupportRecommendedParams>;

boost::filesystem::path support_recommended_params_path()
{
    return (boost::filesystem::path(resources_dir()) / "profiles" / "BBL" / "filament" / "support_recommended_params.json").make_preferred();
}

std::string json_string(const nlohmann::json &combo, const char *key, const char *default_value)
{
    return combo.contains(key) && combo[key].is_string() ? combo[key].get<std::string>() : std::string(default_value);
}

bool json_bool(const nlohmann::json &combo, const char *key)
{
    return combo.contains(key) && combo[key].is_boolean() && combo[key].get<bool>();
}

std::vector<std::string> json_string_list(const nlohmann::json &combo, const char *key)
{
    std::vector<std::string> out;
    if (! combo.contains(key))
        return out;
    const auto &value = combo[key];
    if (value.is_string())
        out.push_back(value.get<std::string>());
    else if (value.is_array())
        for (const auto &item : value)
            if (item.is_string())
                out.push_back(item.get<std::string>());
    return out;
}

// Material match modes: "name"/"type" compare against the list; "any", "soluble" and "support" need no list.
bool material_match_needs_list(const std::string &how)
{
    return how == "name" || how == "type";
}

void parse_recommended_params(const nlohmann::json &params, FilamentCombinationParams &out)
{
    for (auto it = params.begin(); it != params.end(); ++it) {
        const std::string &key = it.key();
        const auto &value = it.value();
        if ((key == "support_top_z_distance" || key == "support_bottom_z_distance") && ! value.is_number()) {
            BOOST_LOG_TRIVIAL(warning) << "Ignoring non-numeric support distance parameter: " << key;
            continue;
        }

        if (value.is_number()) {
            out.params[key] = value.get<double>();
        } else if (value.is_boolean()) {
            out.params[key] = value.get<bool>();
        } else if (value.is_string()) {
            out.params[key] = value.get<std::string>();
        } else if (value.is_array()) {
            std::vector<double> vec;
            for (const auto &elem : value) {
                if (elem.is_number()) {
                    vec.push_back(elem.get<double>());
                } else if (elem.is_string()) {
                    try {
                        vec.push_back(std::stod(elem.get<std::string>()));
                    } catch (...) {
                        BOOST_LOG_TRIVIAL(warning) << "Failed to parse array element as double for " << key;
                    }
                }
            }
            if (! vec.empty())
                out.params[key] = vec;
        }
    }
}

SupportRecommendedParamsTable load_support_recommended_params_table(const boost::filesystem::path &json_path)
{
    SupportRecommendedParamsTable table;

    if (!boost::filesystem::exists(json_path)) {
        BOOST_LOG_TRIVIAL(warning) << "Support recommended params JSON file not found: " << json_path.string();
        return table;
    }

    try {
        boost::nowide::ifstream ifs(json_path.string());
        if (!ifs.is_open()) {
            BOOST_LOG_TRIVIAL(error) << "Failed to open support recommended params JSON file";
            return table;
        }

        nlohmann::json j;
        ifs >> j;

        // Named parameter groups that entries pull in through "inherits", in order, before their own params.
        const nlohmann::json param_sets = j.contains("param_sets") && j["param_sets"].is_object() ?
            j["param_sets"] : nlohmann::json::object();

        if (j.contains("combinations") && j["combinations"].is_array()) {
            for (const auto &combo : j["combinations"]) {
                SupportRecommendedParams rec_params;

                // model_material is a string or a list of strings
                rec_params.model_material_list   = json_string_list(combo, "model_material");
                rec_params.model_material_type   = json_string(combo, "model_material_type", "type");
                rec_params.support_material_list = json_string_list(combo, "support_material_list");
                rec_params.support_material_type = json_string(combo, "support_material_type", "type");
                rec_params.printer_models        = json_string_list(combo, "printer_models");

                // Load priority (default to 0)
                if (combo.contains("priority") && combo["priority"].is_number_integer()) {
                    rec_params.priority = combo["priority"].get<int>();
                }
                rec_params.use_as_support_base = json_bool(combo, "use_as_support_base");

                if ((material_match_needs_list(rec_params.model_material_type) && rec_params.model_material_list.empty()) ||
                    (material_match_needs_list(rec_params.support_material_type) && rec_params.support_material_list.empty())) {
                    BOOST_LOG_TRIVIAL(warning) << "Ignoring support recommended params entry without materials";
                    continue;
                }

                for (const std::string &set_name : json_string_list(combo, "inherits")) {
                    if (param_sets.contains(set_name) && param_sets[set_name].is_object())
                        parse_recommended_params(param_sets[set_name], rec_params.params);
                    else
                        BOOST_LOG_TRIVIAL(warning) << "Unknown support recommended param set: " << set_name;
                }
                if (combo.contains("recommended_params") && combo["recommended_params"].is_object())
                    parse_recommended_params(combo["recommended_params"], rec_params.params);
                if (! rec_params.params.hasRecommendedParams())
                    continue;

                table.push_back(std::move(rec_params));
            }
        }

        BOOST_LOG_TRIVIAL(info) << "Loaded " << table.size() << " support recommended params from JSON";
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Error loading support recommended params JSON: " << e.what();
    }
    return table;
}

const SupportRecommendedParamsTable &support_recommended_params()
{
    static const SupportRecommendedParamsTable table = load_support_recommended_params_table(support_recommended_params_path());
    return table;
}

bool contains(const std::vector<std::string> &list, const std::string &value)
{
    return ! value.empty() && std::find(list.begin(), list.end(), value) != list.end();
}

// 0 = no match; otherwise a larger value is a more specific match (name > type > property/any).
int material_specificity(const std::string &how, const std::vector<std::string> &list, const SupportMaterialProperties &material)
{
    if (how == "name")
        return contains(list, material.name) ? 3 : 0;
    if (how == "type")
        return contains(list, material.type) ? 2 : 0;
    if (how == "any")
        return 1;
    if (how == "soluble")
        return material.soluble ? 1 : 0;
    if (how == "support")
        return material.support ? 1 : 0;
    return 0;
}

// Highest priority wins; ties go to the more specific match (model side first), then to the later entry.
const SupportRecommendedParams *find_support_recommended_params(
    const SupportMaterialProperties &support_material,
    const SupportMaterialProperties &model_material,
    const std::string               &printer_model,
    bool                             default_entries)
{
    const SupportRecommendedParams *best = nullptr;
    std::tuple<int, int, int> best_rank;
    for (const SupportRecommendedParams &entry : support_recommended_params()) {
        if (default_entries ? ! entry.printer_models.empty() : ! contains(entry.printer_models, printer_model))
            continue;
        const int support_rank = material_specificity(entry.support_material_type, entry.support_material_list, support_material);
        const int model_rank   = support_rank > 0 ?
            material_specificity(entry.model_material_type, entry.model_material_list, model_material) : 0;
        if (model_rank == 0)
            continue;
        const std::tuple<int, int, int> rank {entry.priority, model_rank, support_rank};
        if (best == nullptr || rank >= best_rank) {
            best      = &entry;
            best_rank = rank;
        }
    }
    return best;
}

std::optional<double> recommended_distance(const SupportRecommendedParams *entry, const char *key)
{
    return entry == nullptr ? std::nullopt : entry->params.get<double>(key);
}

} // namespace

void load_support_recommended_params()
{
    (void) support_recommended_params();
}

SupportMaterialCombination evaluate_support_material_combination(
    const SupportMaterialProperties &support_material,
    const SupportMaterialProperties &model_material,
    const std::string &printer_model)
{
    SupportMaterialCombination result;
    const SupportRecommendedParams *printer_entry =
        ! printer_model.empty() ?
        find_support_recommended_params(support_material, model_material, printer_model, false) : nullptr;
    // Also looked up next to a printer entry, which inherits its missing top/bottom gaps from it.
    const SupportRecommendedParams *default_entry =
        find_support_recommended_params(support_material, model_material, printer_model, true);
    result.recommended_params = printer_entry != nullptr ? printer_entry : default_entry;
    if (result.recommended_params == nullptr)
        return result;

    // A printer entry without a distance keeps the gap decision of the default entry.
    auto zero_gap = [&](const char *key) {
        std::optional<double> distance = recommended_distance(printer_entry, key);
        if (! distance)
            distance = recommended_distance(default_entry, key);
        return distance && std::abs(*distance) < EPSILON;
    };
    result.zero_top    = zero_gap("support_top_z_distance");
    result.zero_bottom = zero_gap("support_bottom_z_distance");
    // The selected entry decides alone, so a printer entry can deny the support base its default allows.
    result.use_as_support_base = result.recommended_params->use_as_support_base;
    return result;
}

SupportMaterialCombination evaluate_support_material_combination(
    const SupportMaterialProperties              &support_material,
    const std::vector<SupportMaterialProperties> &model_materials,
    const std::string                            &printer_model,
    size_t                                       *model_index)
{
    SupportMaterialCombination chosen;
    // A gap or the support base holds only when every material's recommendation agrees on it.
    bool zero_top = true, zero_bottom = true, use_as_support_base = true;
    for (size_t i = 0; i < model_materials.size(); ++i) {
        const SupportMaterialCombination combination =
            evaluate_support_material_combination(support_material, model_materials[i], printer_model);
        if (combination.recommended_params == nullptr)
            return {};
        zero_top            &= combination.zero_top;
        zero_bottom         &= combination.zero_bottom;
        use_as_support_base &= combination.use_as_support_base;
        if (chosen.recommended_params == nullptr || combination.recommended_params->priority < chosen.recommended_params->priority) {
            chosen = combination;
            if (model_index != nullptr)
                *model_index = i;
        }
    }
    if (chosen.recommended_params == nullptr)
        return {};
    chosen.zero_top            = zero_top;
    chosen.zero_bottom         = zero_bottom;
    chosen.use_as_support_base = use_as_support_base;
    return chosen;
}

} // namespace Slic3r
