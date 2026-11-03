#include "SupportRecommendedParams.hpp"

#include "libslic3r.h"
#include "Utils.hpp"
#include "nlohmann/json.hpp"

#include <array>
#include <cmath>
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

namespace {

// Keyed by "support_material|model_material|printer_model". Multiple typed entries may share the
// textual key; lookup selects the highest-priority entry with matching name/type semantics.
using SupportRecommendedParamsMap = std::map<std::string, std::vector<SupportRecommendedParams>>;

std::string support_recommended_params_key(const std::string &support_material, const std::string &model_material, const std::string &printer_model)
{
    return support_material + "|" + model_material + "|" + printer_model;
}

boost::filesystem::path support_recommended_params_path()
{
    return (boost::filesystem::path(resources_dir()) / "profiles" / "BBL" / "filament" / "support_recommended_params.json").make_preferred();
}

SupportRecommendedParamsMap load_support_recommended_params_map(const boost::filesystem::path &json_path)
{
    SupportRecommendedParamsMap support_recommended_params_map;

    if (!boost::filesystem::exists(json_path)) {
        BOOST_LOG_TRIVIAL(warning) << "Support recommended params JSON file not found: " << json_path.string();
        return support_recommended_params_map;
    }

    try {
        boost::nowide::ifstream ifs(json_path.string());
        if (!ifs.is_open()) {
            BOOST_LOG_TRIVIAL(error) << "Failed to open support recommended params JSON file";
            return support_recommended_params_map;
        }

        nlohmann::json j;
        ifs >> j;

        if (j.contains("combinations") && j["combinations"].is_array()) {
            for (const auto &combo : j["combinations"]) {
                SupportRecommendedParams rec_params;

                // Load model_material (single string)
                if (combo.contains("model_material") && combo["model_material"].is_string()) {
                    rec_params.model_material = combo["model_material"].get<std::string>();
                }

                // Load model_material_type (default to "type")
                if (combo.contains("model_material_type") && combo["model_material_type"].is_string()) {
                    rec_params.model_material_type = combo["model_material_type"].get<std::string>();
                } else {
                    rec_params.model_material_type = "type";
                }

                // Load support_material_list (array)
                if (combo.contains("support_material_list") && combo["support_material_list"].is_array()) {
                    for (const auto &mat : combo["support_material_list"]) {
                        if (mat.is_string()) {
                            rec_params.support_material_list.push_back(mat.get<std::string>());
                        }
                    }
                }

                // Load support_material_type (default to "type")
                if (combo.contains("support_material_type") && combo["support_material_type"].is_string()) {
                    rec_params.support_material_type = combo["support_material_type"].get<std::string>();
                } else {
                    rec_params.support_material_type = "type";
                }

                // Load printer_models (array)
                if (combo.contains("printer_models") && combo["printer_models"].is_array()) {
                    for (const auto &pm : combo["printer_models"]) {
                        if (pm.is_string()) {
                            rec_params.printer_models.push_back(pm.get<std::string>());
                        }
                    }
                }

                // Load priority (default to 0)
                if (combo.contains("priority") && combo["priority"].is_number_integer()) {
                    rec_params.priority = combo["priority"].get<int>();
                }

                // An entry without printer_models would apply to every machine, including those it was
                // never validated on, so require the scope to be explicit.
                if (rec_params.model_material.empty() || rec_params.support_material_list.empty() || rec_params.printer_models.empty()) {
                    continue;
                }

                // Load recommended params
                if (combo.contains("recommended_params") && combo["recommended_params"].is_object()) {
                    const auto &params = combo["recommended_params"];

                    for (auto it = params.begin(); it != params.end(); ++it) {
                        const std::string &key = it.key();
                        const auto &value = it.value();
                        if ((key == "support_top_z_distance" || key == "support_bottom_z_distance") &&
                            ! value.is_number()) {
                            BOOST_LOG_TRIVIAL(warning) << "Ignoring non-numeric support distance parameter: " << key;
                            continue;
                        }

                        if (value.is_number_float() || value.is_number_integer()) {
                            rec_params.params.params[key] = value.get<double>();
                        } else if (value.is_boolean()) {
                            rec_params.params.params[key] = value.get<bool>();
                        } else if (value.is_string()) {
                            rec_params.params.params[key] = value.get<std::string>();
                        } else if (value.is_array()) {
                            std::vector<double> vec;
                            for (const auto& elem : value) {
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
                            if (!vec.empty()) {
                                rec_params.params.params[key] = vec;
                            }
                        }
                    }
                }
                if (! rec_params.params.hasRecommendedParams())
                    continue;

                // Create key for each (support_material, printer_model): "support_material|model_material|printer_model"
                for (const auto &support_mat : rec_params.support_material_list) {
                    for (const auto &printer_model : rec_params.printer_models) {
                        support_recommended_params_map[support_recommended_params_key(
                            support_mat, rec_params.model_material, printer_model)].push_back(rec_params);
                    }
                }
            }
        }

        size_t entry_count = 0;
        for (const auto &item : support_recommended_params_map)
            entry_count += item.second.size();
        BOOST_LOG_TRIVIAL(info) << "Loaded " << entry_count << " support recommended params from JSON";
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Error loading support recommended params JSON: " << e.what();
    }
    return support_recommended_params_map;
}

const SupportRecommendedParamsMap &support_recommended_params()
{
    static const SupportRecommendedParamsMap table = load_support_recommended_params_map(support_recommended_params_path());
    return table;
}

} // namespace

void load_support_recommended_params()
{
    (void) support_recommended_params();
}

static const SupportRecommendedParams *find_support_recommended_params(
    const std::string &support_material_name,
    const std::string &support_material_type,
    const std::string &model_material_name,
    const std::string &model_material_type,
    const std::string &printer_model)
{
    const SupportRecommendedParamsMap &table = support_recommended_params();
    struct MaterialQuery {
        const std::string *support_material;
        const char        *support_key_type;
        const std::string *model_material;
        const char        *model_key_type;
    };
    const std::array<MaterialQuery, 4> queries {{
        {&support_material_name, "name", &model_material_name, "name"},
        {&support_material_type, "type", &model_material_name, "name"},
        {&support_material_name, "name", &model_material_type, "type"},
        {&support_material_type, "type", &model_material_type, "type"}
    }};
    const SupportRecommendedParams *best = nullptr;
    size_t best_query_index = queries.size();
    for (size_t query_index = 0; query_index < queries.size(); ++query_index) {
        const MaterialQuery &query = queries[query_index];
        if (query.support_material->empty() || query.model_material->empty())
            continue;
        auto it = table.find(support_recommended_params_key(
            *query.support_material, *query.model_material, printer_model));
        if (it == table.end())
            continue;
        for (const SupportRecommendedParams &entry : it->second) {
            if (entry.support_material_type != query.support_key_type ||
                entry.model_material_type != query.model_key_type)
                continue;
            // Query order resolves equal priorities from specific to general; within one key, the later
            // JSON entry wins as it did when the table stored a single value.
            if (best == nullptr || entry.priority > best->priority ||
                (entry.priority == best->priority && query_index == best_query_index)) {
                best = &entry;
                best_query_index = query_index;
            }
        }
    }
    return best;
}

SupportMaterialCombination evaluate_support_material_combination(
    const SupportMaterialProperties &support_material,
    const SupportMaterialProperties &model_material,
    const std::string &printer_model)
{
    SupportMaterialCombination result;
    if (! printer_model.empty()) {
        result.recommended_params = find_support_recommended_params(
            support_material.name, support_material.type,
            model_material.name, model_material.type, printer_model);
    }

    if (support_material.type == "PLA" &&
        (model_material.type == "TPU" || model_material.type == "TPU-AMS"))
        result.built_in_rule = BuiltInSupportMaterialRule::PlaForTpu;
    else if (support_material.soluble)
        result.built_in_rule = BuiltInSupportMaterialRule::Soluble;
    else if (support_material.support)
        result.built_in_rule = BuiltInSupportMaterialRule::DedicatedSupport;
    else if (support_material.type == "PETG" && model_material.type == "PLA")
        result.built_in_rule = BuiltInSupportMaterialRule::PetgForPla;
    else if (support_material.type == "PLA" && model_material.type == "PETG")
        result.built_in_rule = BuiltInSupportMaterialRule::PlaForPetg;

    const bool built_in_zero_top = result.built_in_rule != BuiltInSupportMaterialRule::None;
    const bool built_in_zero_bottom = support_material.type == "PVA" && model_material.type == "PLA";
    const std::optional<double> json_top = result.recommended_params == nullptr ?
        std::nullopt : result.recommended_params->params.get<double>("support_top_z_distance");
    const std::optional<double> json_bottom = result.recommended_params == nullptr ?
        std::nullopt : result.recommended_params->params.get<double>("support_bottom_z_distance");

    result.zero_top = json_top ? std::abs(*json_top) < EPSILON : built_in_zero_top;
    result.zero_bottom = json_bottom ? std::abs(*json_bottom) < EPSILON : built_in_zero_bottom;
    return result;
}

} // namespace Slic3r
