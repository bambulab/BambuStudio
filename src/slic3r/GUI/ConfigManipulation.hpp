#ifndef slic3r_ConfigManipulation_hpp_
#define slic3r_ConfigManipulation_hpp_

/*	 Class for validation config options
 *	 and update (enable/disable) IU components
 *
 *	 Used for config validation for global config (Print Settings Tab)
 *	 and local config (overrides options on sidebar)
 * */

#include "libslic3r/PrintConfig.hpp"
#include "Field.hpp"
#include <wx/string.h>
#include <set>

namespace Slic3r {

class ModelConfig;
class ObjectBase;

namespace GUI {

class ConfigManipulation
{
    bool                is_msg_dlg_already_exist{ false };
    bool                m_is_initialized_support_material_overhangs_queried{ false };
    bool                m_support_material_overhangs_queried{ false };
    bool                is_BBL_Printer{false};

    // function to loading of changed configuration
    std::function<void()>                                       load_config = nullptr;
    std::function<void (const std::string&, bool toggle, int opt_index)>   cb_toggle_field = nullptr;
    std::function<void(const std::string &, bool toggle, int opt_index)> cb_toggle_line  = nullptr;
    // callback to propagation of changed value, if needed
    std::function<void(const std::string&, const boost::any&)>  cb_value_change = nullptr;
    //BBS: change local config to const DynamicPrintConfig
    const DynamicPrintConfig* local_config = nullptr;
    //ModelConfig* local_config = nullptr;
    wxWindow*    m_msg_dlg_parent {nullptr};

    t_config_option_keys m_applying_keys;

public:
    ConfigManipulation(std::function<void()> load_config,
        std::function<void(const std::string&, bool toggle, int opt_index)> cb_toggle_field,
        std::function<void(const std::string&, bool toggle, int opt_index)> cb_toggle_line,
        std::function<void(const std::string&, const boost::any&)>  cb_value_change,
        //BBS: change local config to DynamicPrintConfig
        const DynamicPrintConfig* local_config = nullptr,
        wxWindow* msg_dlg_parent  = nullptr) :
        load_config(load_config),
        cb_toggle_field(cb_toggle_field),
        cb_toggle_line(cb_toggle_line),
        cb_value_change(cb_value_change),
        m_msg_dlg_parent(msg_dlg_parent),
        local_config(local_config) {}
    ConfigManipulation() {}

    ~ConfigManipulation() {
        load_config = nullptr;
        cb_toggle_field = nullptr;
        cb_toggle_line = nullptr;
        cb_value_change = nullptr;
    }

    bool    is_applying() const;

    void    apply(DynamicPrintConfig* config, DynamicPrintConfig* new_config);
    t_config_option_keys const &applying_keys() const;
    void    toggle_field(const std::string& field_key, const bool toggle, int opt_index = -1);
    void    toggle_line(const std::string& field_key, const bool toggle, int opt_index = -1);

    // FFF print
    void    update_print_fff_config(DynamicPrintConfig* config, const bool is_global_config = false, const bool is_plate_config = false);
    void    toggle_print_fff_options(DynamicPrintConfig* config, int variant_index, const bool is_global_config = false);
    void    apply_null_fff_config(DynamicPrintConfig *config, std::vector<std::string> const &keys, std::map<ObjectBase*, ModelConfig*> const & configs);

    //BBS: FFF filament nozzle temperature range
    void    check_nozzle_recommended_temperature_range(DynamicPrintConfig *config);
    void    check_nozzle_temperature_range(DynamicPrintConfig* config);
    void    check_nozzle_temperature_initial_layer_range(DynamicPrintConfig* config);
    void    check_filament_max_volumetric_speed(DynamicPrintConfig *config);
    void    check_filament_scarf_setting(DynamicPrintConfig *config);
    void    check_chamber_temperature(DynamicPrintConfig* config);
    void    set_is_BBL_Printer(bool is_bbl_printer) { is_BBL_Printer = is_bbl_printer; };
    // SLA print
    void    update_print_sla_config(DynamicPrintConfig* config, const bool is_global_config = false);
    void    toggle_print_sla_options(DynamicPrintConfig* config);

    bool    is_initialized_support_material_overhangs_queried() { return m_is_initialized_support_material_overhangs_queried; }
    void    initialize_support_material_overhangs_queried(bool queried)
    {
        m_is_initialized_support_material_overhangs_queried = true;
        m_support_material_overhangs_queried = queried;
    }
    int    show_spiral_mode_settings_dialog(bool is_object_config = false);

private:
    bool get_temperature_range(DynamicPrintConfig *config, int &range_low, int &range_high);
};

// 支撑耗材组合匹配结果
struct SupportFilamentRecommendation
{
    bool has_combination{false};        // 是否找到匹配的耗材组合
    int  matched_filament_index{-1};    // 匹配的支撑耗材索引 (0-based)

    std::string support_material;       // 支撑材料（用于查询推荐参数，可能是类型或名称）
    std::string model_material;         // 主体材料（用于查询推荐参数，可能是类型或名称）

    std::string support_material_name;  // 支撑材料名称（用于显示，如 "Bambu PLA Basic"）
    std::string model_material_name;    // 主体材料名称（用于显示，如 "Bambu TPU 95A"）

    std::set<int> used_extruders;       // 对象使用的所有 extruder ID（1-based）
};

// 为单个对象查找匹配的耗材组合（支持同类材料的多色模型）
SupportFilamentRecommendation has_filament_combination_for_object(const ModelObject* obj);

// 根据支撑材料和主体材料，构建推荐配置到 DynamicPrintConfig
// 返回是否有推荐参数
bool build_support_recommended_config(const std::string& support_material, const std::string& model_material, int support_filament_index, DynamicPrintConfig& out_config, bool& out_use_same_for_base);

} // GUI
} // Slic3r

#endif /* slic3r_ConfigManipulation_hpp_ */
