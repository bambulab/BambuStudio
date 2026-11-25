#ifndef slic3r_ConfigManipulation_hpp_
#define slic3r_ConfigManipulation_hpp_

/*	 Class for validation config options
 *	 and update (enable/disable) IU components
 *
 *	 Used for config validation for global config (Print Settings Tab)
 *	 and local config (overrides options on sidebar)
 * */

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/SupportRecommendedParams.hpp"
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
    bool                is_BBL_Printer{false};
    bool                m_alt_suboptimal_acknowledged{ false };

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
    /**
     * \brief Tell the user a typed nozzle temperature is outside the allowed range and gets reset
     *
     * \param entered     typed temperature
     * \param min_allowed lower bound of the allowed range
     * \param max_allowed upper bound of the allowed range
     * \note Call before the value is clamped; the change caused by the clamp shows no further warning
     */
    void    report_nozzle_temperature_out_of_range(long entered, int min_allowed, int max_allowed);
    void    check_tower_interface_print_temp(DynamicPrintConfig* config, int variant_idx);
    void    check_filament_max_volumetric_speed(DynamicPrintConfig *config);
    void    check_filament_scarf_setting(DynamicPrintConfig *config);
    //BBS: keep the painted top/bottom color inside the solid shell, out of the sparse infill
    void    check_color_penetration_layers(DynamicPrintConfig* config, const std::string& edited_key);
    void    set_is_BBL_Printer(bool is_bbl_printer) { is_BBL_Printer = is_bbl_printer; };
    // SLA print
    void    update_print_sla_config(DynamicPrintConfig* config, const bool is_global_config = false);
    void    toggle_print_sla_options(DynamicPrintConfig* config);

    int    show_spiral_mode_settings_dialog(bool is_object_config = false);

};

// 按支撑料与主体料查推荐表（每种主体料先查机型条目再查默认条目），命中时把推荐参数写入 out_config
// support_filament_index: 支撑料索引 (0-based)
// 多种主体料时每种都须命中；命中不同条目时取优先级最低的一条（同优先级取先出现的），
// out_model_index 为其下标；可作支撑主体须所有主体料都允许
bool query_support_recommended_params_for_combination(
    int support_filament_index,
    const std::vector<SupportMaterialProperties> &model_materials,
    DynamicPrintConfig& out_config,
    SupportMaterialCombination &out_combination,
    size_t &out_model_index);

} // GUI
} // Slic3r

#endif /* slic3r_ConfigManipulation_hpp_ */
