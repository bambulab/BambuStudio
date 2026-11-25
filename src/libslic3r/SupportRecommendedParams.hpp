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

// 支撑推荐参数：按 (支撑料, 主体料, 机型) 匹配
struct SupportRecommendedParams
{
    std::vector<std::string> model_material_list;   // 主体料（类型或名称），按 any 匹配时为空
    std::string model_material_type;                // "name" / "type" / "any"（任意主体料）
    std::vector<std::string> support_material_list; // 支撑料（类型或名称），按属性匹配时为空
    std::string support_material_type;              // "name" / "type" / "soluble"（可溶料）/ "support"（支撑料）
    std::vector<std::string> printer_models;        // 适用机型；为空即默认条目，机型条目未命中时生效
    int priority{0};                                // 优先级，数字越大优先级越高
    bool use_as_support_base{false};                // 可作支撑主体：推荐时主体也改用它，选作主体时不提醒
    FilamentCombinationParams params;               // 推荐参数
};

struct SupportMaterialProperties
{
    std::string name;
    std::string type;
    bool        soluble { false };
    bool        support { false };
};

struct SupportMaterialCombination
{
    const SupportRecommendedParams *recommended_params { nullptr };
    bool                            zero_top           { false };
    bool                            zero_bottom        { false };
    bool                            use_as_support_base { false };
};

std::string support_recommended_material_name(std::string preset_name);
void        load_support_recommended_params();
// Exactly "TPU" or "TPU-AMS", the types of the PLA-for-TPU default entry. Automatic support selection ranks TPU
// below any rigid filament because it is too soft to carry a support structure.
bool        is_tpu_filament_type(const std::string &filament_type);
// Support material types that bond too poorly to the model material type to carry a support base on it
// (PLA <-> PETG, PLA on TPU). Independent of the interface distances evaluated below.
bool        unsuitable_as_support_base(const std::string &support_type, const std::string &model_type);

// Looks up the recommended parameters of one support/model material pair: the printer entry when one
// matches, otherwise the default entry. A printer entry that leaves out the top/bottom distance inherits
// the zero gap of the matching default entry.
SupportMaterialCombination evaluate_support_material_combination(
    const SupportMaterialProperties &support_material,
    const SupportMaterialProperties &model_material,
    const std::string &printer_model);

// The recommendation for a support filament over several model materials: every material has to match,
// and when they match different entries the lowest priority one is the most conservative choice (the first
// material wins a tie). The zero top/bottom gaps and the support base hold only if they do for every material.
// model_index receives the position of the material whose entry was chosen. Returns an empty combination when
// any material misses.
SupportMaterialCombination evaluate_support_material_combination(
    const SupportMaterialProperties              &support_material,
    const std::vector<SupportMaterialProperties> &model_materials,
    const std::string                            &printer_model,
    size_t                                       *model_index = nullptr);

} // namespace Slic3r

#endif // slic3r_SupportRecommendedParams_hpp_
