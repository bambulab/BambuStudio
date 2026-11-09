#ifndef slic3r_SupportRecommendedParams_hpp_
#define slic3r_SupportRecommendedParams_hpp_

#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace Slic3r {

// Recommended parameters for support filament combination
struct FilamentCombinationParams
{
    using ParamValue = std::variant<double, bool, std::string, std::vector<double>>;
    std::map<std::string, ParamValue> params;  // 通用参数存储
    bool hasRecommendedParams() const {
        return !params.empty();
    }

    // 辅助函数：获取参数值
    template<typename T>
    std::optional<T> get(const std::string& key) const {
        auto it = params.find(key);
        if (it != params.end()) {
            if (auto* val = std::get_if<T>(&it->second)) {
                return *val;
            }
        }
        return std::nullopt;
    }
};

// 支撑推荐参数：通过 (support_material, model_material, printer_model) 查询
struct SupportRecommendedParams
{
    std::string model_material;                     // 主体料（类型或名称）
    std::string model_material_type;                // "name" 或 "type"
    std::vector<std::string> support_material_list; // 支撑料列表（类型或名称）
    std::string support_material_type;              // "name" 或 "type"
    std::vector<std::string> printer_models;        // 适用机型（printer_model 原名），为空则该条目不生效
    int priority{0};                                // 优先级，数字越大优先级越高
    FilamentCombinationParams params;               // 推荐参数
};

struct SupportMaterialProperties
{
    std::string name;
    std::string type;
    bool        soluble { false };
    bool        support { false };
};

enum class BuiltInSupportMaterialRule
{
    None,
    Soluble,
    DedicatedSupport,
    PetgForPla,
    PlaForPetg,
    PlaForTpu
};

struct SupportMaterialCombination
{
    const SupportRecommendedParams *recommended_params { nullptr };
    BuiltInSupportMaterialRule      built_in_rule      { BuiltInSupportMaterialRule::None };
    bool                            zero_top           { false };
    bool                            zero_bottom        { false };
};

std::string support_recommended_material_name(std::string preset_name);
void        load_support_recommended_params();

// Resolves the JSON recommendation and built-in material-pair rules in one place. An explicit JSON
// top/bottom distance overrides the corresponding built-in decision; a missing field inherits it.
SupportMaterialCombination evaluate_support_material_combination(
    const SupportMaterialProperties &support_material,
    const SupportMaterialProperties &model_material,
    const std::string &printer_model);

} // namespace Slic3r

#endif // slic3r_SupportRecommendedParams_hpp_
