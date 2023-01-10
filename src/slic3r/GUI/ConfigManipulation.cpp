// #include "libslic3r/GCodeSender.hpp"
#include "ConfigManipulation.hpp"
#include "I18N.hpp"
#include "GUI_App.hpp"
#include "format.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "MsgDialog.hpp"

#include <wx/msgdlg.h>

namespace Slic3r {
namespace GUI {

void ConfigManipulation::apply(DynamicPrintConfig* config, DynamicPrintConfig* new_config)
{
    bool modified = false;
    m_applying_keys = config->diff(*new_config);
    for (auto opt_key : m_applying_keys) {
        config->set_key_value(opt_key, new_config->option(opt_key)->clone());
        modified = true;
    }
    if (modified && load_config != nullptr)
        load_config();
    m_applying_keys.clear();
}

bool ConfigManipulation::is_applying() const { return is_msg_dlg_already_exist; }

t_config_option_keys const &ConfigManipulation::applying_keys() const
{
    return m_applying_keys;
}

void ConfigManipulation::toggle_field(const std::string &opt_key, const bool toggle, int opt_index /* = -1*/)
{
    if (local_config) {
        if (local_config->option(opt_key) == nullptr) return;
    }
    cb_toggle_field(opt_key, toggle, opt_index);
}

void ConfigManipulation::toggle_line(const std::string& opt_key, const bool toggle)
{
    if (local_config) {
        if (local_config->option(opt_key) == nullptr)
            return;
    }
    if (cb_toggle_line)
        cb_toggle_line(opt_key, toggle);
}

void ConfigManipulation::check_nozzle_temperature_range(DynamicPrintConfig *config)
{
    if (is_msg_dlg_already_exist)
        return;

    int temperature_range_low = config->has("nozzle_temperature_range_low") ?
                                config->opt_int("nozzle_temperature_range_low", (unsigned int)0) :
                                0;
    int temperature_range_high = config->has("nozzle_temperature_range_high") ?
                                 config->opt_int("nozzle_temperature_range_high", (unsigned int)0) :
                                 0;

    if (temperature_range_low != 0 &&
        temperature_range_high != 0 &&
        config->has("nozzle_temperature")) {
        if (config->opt_int("nozzle_temperature", 0) < temperature_range_low ||
            config->opt_int("nozzle_temperature", 0) > temperature_range_high)
        {
            wxString msg_text = _(L("Nozzle may be blocked when the temperature is out of recommended range.\n"
                "Please make sure whether to use the temperature to print.\n\n"));
            msg_text += wxString::Format(_L("Recommended nozzle temperature of this filament type is [%d, %d] degree centigrade"), temperature_range_low, temperature_range_high);
            MessageDialog dialog(m_msg_dlg_parent, msg_text, "", wxICON_WARNING | wxOK);
            is_msg_dlg_already_exist = true;
            dialog.ShowModal();
            is_msg_dlg_already_exist = false;
        }
    }
}

void ConfigManipulation::check_nozzle_temperature_initial_layer_range(DynamicPrintConfig* config)
{
    if (is_msg_dlg_already_exist)
        return;

    int temperature_range_low = config->has("nozzle_temperature_range_low") ?
                            config->opt_int("nozzle_temperature_range_low", (unsigned int)0) :
                            0;
    int temperature_range_high = config->has("nozzle_temperature_range_high") ?
                                 config->opt_int("nozzle_temperature_range_high", (unsigned int)0) :
                                 0;

    if (temperature_range_low != 0 &&
        temperature_range_high != 0 &&
        config->has("nozzle_temperature_initial_layer")) {
        if (config->opt_int("nozzle_temperature_initial_layer", 0) < temperature_range_low ||
            config->opt_int("nozzle_temperature_initial_layer", 0) > temperature_range_high)
        {
            wxString msg_text = _(L("Nozzle may be blocked when the temperature is out of recommended range.\n"
                "Please make sure whether to use the temperature to print.\n\n"));
            msg_text += wxString::Format(_L("Recommended nozzle temperature of this filament type is [%d, %d] degree centigrade"), temperature_range_low, temperature_range_high);
            MessageDialog dialog(m_msg_dlg_parent, msg_text, "", wxICON_WARNING | wxOK);
            is_msg_dlg_already_exist = true;
            dialog.ShowModal();
            is_msg_dlg_already_exist = false;
        }
    }
}

void ConfigManipulation::check_bed_temperature_difference(int bed_type, DynamicPrintConfig* config)
{
    if (is_msg_dlg_already_exist)
        return;

    if (config->has("bed_temperature_difference") && config->has("temperature_vitrification")) {
        int bed_temp_difference = config->opt_int("bed_temperature_difference", 0);
        int vitrification = config->opt_int("temperature_vitrification", 0);
        const ConfigOptionInts* bed_temp_1st_layer_opt = config->option<ConfigOptionInts>(get_bed_temp_1st_layer_key((BedType)bed_type));
        const ConfigOptionInts* bed_temp_opt = config->option<ConfigOptionInts>(get_bed_temp_key((BedType)bed_type));

        if (bed_temp_1st_layer_opt != nullptr && bed_temp_opt != nullptr) {
            int first_layer_bed_temp = bed_temp_1st_layer_opt->get_at(0);
            int bed_temp = bed_temp_opt->get_at(0);
            if (first_layer_bed_temp - bed_temp > bed_temp_difference) {
                const wxString msg_text = wxString::Format(_L("Bed temperature of other layer is lower than bed temperature of initial layer for more than %d degree centigrade.\nThis may cause model broken free from build plate during printing"), bed_temp_difference);
                MessageDialog dialog(m_msg_dlg_parent, msg_text, "", wxICON_WARNING | wxOK);
                is_msg_dlg_already_exist = true;
                dialog.ShowModal();
                is_msg_dlg_already_exist = false;
            }

            if (first_layer_bed_temp > vitrification || bed_temp > vitrification) {
                const wxString msg_text = wxString::Format(
                    _L("Bed temperature is higher than vitrification temperature of this filament.\nThis may cause nozzle blocked and printing failure\nPlease keep the printer open during the printing process to ensure air circulation or reduce the temperature of the hot bed"));
                MessageDialog dialog(m_msg_dlg_parent, msg_text, "", wxICON_WARNING | wxOK);
                is_msg_dlg_already_exist = true;
                dialog.ShowModal();
                is_msg_dlg_already_exist = false;
            }
        }
    }
}

void ConfigManipulation::check_filament_max_volumetric_speed(DynamicPrintConfig *config)
{
    //if (is_msg_dlg_already_exist) return;
    //float max_volumetric_speed = config->opt_float("filament_max_volumetric_speed");

    float max_volumetric_speed = config->has("filament_max_volumetric_speed") ? config->opt_float("filament_max_volumetric_speed", (float) 0.5) : 0.5;
    // BBS: limite the min max_volumetric_speed
    if (max_volumetric_speed < 0.5) {
        const wxString     msg_text = _(L("Too small max volumetric speed.\nReset to 0.5"));
        MessageDialog      dialog(nullptr, msg_text, "", wxICON_WARNING | wxOK);
        DynamicPrintConfig new_conf = *config;
        is_msg_dlg_already_exist    = true;
        dialog.ShowModal();
        new_conf.set_key_value("filament_max_volumetric_speed", new ConfigOptionFloats({0.5}));
        apply(config, &new_conf);
        is_msg_dlg_already_exist = false;
    }
 
}

void ConfigManipulation::update_print_fff_config(DynamicPrintConfig* config, const bool is_global_config)
{
    // #ys_FIXME_to_delete
    //! Temporary workaround for the correct updates of the TextCtrl (like "layer_height"):
    // KillFocus() for the wxSpinCtrl use CallAfter function. So,
    // to except the duplicate call of the update() after dialog->ShowModal(),
    // let check if this process is already started.
    if (is_msg_dlg_already_exist)
        return;

    // layer_height shouldn't be equal to zero
    if (config->opt_float("layer_height") < EPSILON)
    {
        const wxString msg_text = _(L("Too small layer height.\nReset to 0.2"));
        MessageDialog dialog(m_msg_dlg_parent, msg_text,"", wxICON_WARNING | wxOK);
        DynamicPrintConfig new_conf = *config;
        is_msg_dlg_already_exist = true;
        dialog.ShowModal();
        new_conf.set_key_value("layer_height", new ConfigOptionFloat(0.2));
        apply(config, &new_conf);
        is_msg_dlg_already_exist = false;
    }

    //BBS: limite the max layer_herght
    if (config->opt_float("layer_height") > 0.6 + EPSILON)
    {
        const wxString msg_text = _(L("Too large layer height.\nReset to 0.2"));
        MessageDialog dialog(nullptr, msg_text, "", wxICON_WARNING | wxOK);
        DynamicPrintConfig new_conf = *config;
        is_msg_dlg_already_exist = true;
        dialog.ShowModal();
        new_conf.set_key_value("layer_height", new ConfigOptionFloat(0.2));
        apply(config, &new_conf);
        is_msg_dlg_already_exist = false;
    }

    //BBS: ironing_spacing shouldn't be too small or equal to zero
    if (config->opt_float("ironing_spacing") < 0.05)
    {
        const wxString msg_text = _(L("Too small ironing spacing.\nReset to 0.1"));
        MessageDialog dialog(nullptr, msg_text, "", wxICON_WARNING | wxOK);
        DynamicPrintConfig new_conf = *config;
        is_msg_dlg_already_exist = true;
        dialog.ShowModal();
        new_conf.set_key_value("ironing_spacing", new ConfigOptionFloat(0.1));
        apply(config, &new_conf);
        is_msg_dlg_already_exist = false;
    }

    if (config->option<ConfigOptionFloat>("initial_layer_print_height")->value < EPSILON)
    {
        const wxString msg_text = _(L("Zero initial layer height is invalid.\n\nThe first layer height will be reset to 0.2."));
        MessageDialog dialog(m_msg_dlg_parent, msg_text, "", wxICON_WARNING | wxOK);
        DynamicPrintConfig new_conf = *config;
        is_msg_dlg_already_exist = true;
        dialog.ShowModal();
        new_conf.set_key_value("initial_layer_print_height", new ConfigOptionFloat(0.2));
        apply(config, &new_conf);
        is_msg_dlg_already_exist = false;
    }

    if (abs(config->option<ConfigOptionFloat>("xy_hole_compensation")->value) > 2)
    {
        const wxString msg_text = _(L("This setting is only used for model size tunning with small value in some cases.\n"
                                      "For example, when model size has small error and hard to be assembled.\n"
                                      "For large size tuning, please use model scale function.\n\n"
                                      "The value will be reset to 0."));
        MessageDialog dialog(m_msg_dlg_parent, msg_text, "", wxICON_WARNING | wxOK);
        DynamicPrintConfig new_conf = *config;
        is_msg_dlg_already_exist = true;
        dialog.ShowModal();
        new_conf.set_key_value("xy_hole_compensation", new ConfigOptionFloat(0));
        apply(config, &new_conf);
        is_msg_dlg_already_exist = false;
    }

    if (abs(config->option<ConfigOptionFloat>("xy_contour_compensation")->value) > 2)
    {
        const wxString msg_text = _(L("This setting is only used for model size tunning with small value in some cases.\n"
                                      "For example, when model size has small error and hard to be assembled.\n"
                                      "For large size tuning, please use model scale function.\n\n"
                                      "The value will be reset to 0."));
        MessageDialog dialog(m_msg_dlg_parent, msg_text, "", wxICON_WARNING | wxOK);
        DynamicPrintConfig new_conf = *config;
        is_msg_dlg_already_exist = true;
        dialog.ShowModal();
        new_conf.set_key_value("xy_contour_compensation", new ConfigOptionFloat(0));
        apply(config, &new_conf);
        is_msg_dlg_already_exist = false;
    }

    if (config->option<ConfigOptionFloat>("elefant_foot_compensation")->value > 1)
    {
        const wxString msg_text = _(L("Too large elefant foot compensation is unreasonable.\n"
                                      "If really have serious elephant foot effect, please check other settings.\n"
                                      "For example, whether bed temperature is too high.\n\n"
                                      "The value will be reset to 0."));
        MessageDialog dialog(m_msg_dlg_parent, msg_text, "", wxICON_WARNING | wxOK);
        DynamicPrintConfig new_conf = *config;
        is_msg_dlg_already_exist = true;
        dialog.ShowModal();
        new_conf.set_key_value("elefant_foot_compensation", new ConfigOptionFloat(0));
        apply(config, &new_conf);
        is_msg_dlg_already_exist = false;
    }

    double sparse_infill_density = config->option<ConfigOptionPercent>("sparse_infill_density")->value;
    auto timelapse_type = config->opt_enum<TimelapseType>("timelapse_type");

    if (config->opt_bool("spiral_mode") &&
        ! (config->opt_int("wall_loops") == 1 &&
           config->opt_int("top_shell_layers") == 0 &&
           sparse_infill_density == 0 &&
           ! config->opt_bool("enable_support") &&
           config->opt_int("enforce_support_layers") == 0 &&
           config->opt_bool("ensure_vertical_shell_thickness") &&
           ! config->opt_bool("detect_thin_wall") &&
            config->opt_enum<TimelapseType>("timelapse_type") == TimelapseType::tlTraditional))
    {
        wxString msg_text = _(L("Spiral mode only works when wall loops is 1, support is disabled, top shell layers is 0, sparse infill density is 0 and timelapse type is traditional"));
        if (is_global_config)
            msg_text += "\n\n" + _(L("Change these settings automatically? \n"
                                     "Yes - Change these settings and enable spiral mode automatically\n"
                                     "No  - Give up using spiral mode this time"));
        MessageDialog dialog(m_msg_dlg_parent, msg_text, "",
                               wxICON_WARNING | (is_global_config ? wxYES | wxNO : wxOK));
        DynamicPrintConfig new_conf = *config;
        is_msg_dlg_already_exist = true;
        auto answer = dialog.ShowModal();
        bool support = true;
        if (!is_global_config || answer == wxID_YES) {
            new_conf.set_key_value("wall_loops", new ConfigOptionInt(1));
            new_conf.set_key_value("top_shell_layers", new ConfigOptionInt(0));
            new_conf.set_key_value("sparse_infill_density", new ConfigOptionPercent(0));
            new_conf.set_key_value("enable_support", new ConfigOptionBool(false));
            new_conf.set_key_value("enforce_support_layers", new ConfigOptionInt(0));
            new_conf.set_key_value("ensure_vertical_shell_thickness", new ConfigOptionBool(true));
            new_conf.set_key_value("detect_thin_wall", new ConfigOptionBool(false));
            new_conf.set_key_value("timelapse_type", new ConfigOptionEnum<TimelapseType>(tlTraditional));
            sparse_infill_density = 0;
            timelapse_type = TimelapseType::tlTraditional;
            support = false;
        }
        else {
            new_conf.set_key_value("spiral_mode", new ConfigOptionBool(false));
        }
        apply(config, &new_conf);
        if (cb_value_change) {
            cb_value_change("sparse_infill_density", sparse_infill_density);
            cb_value_change("timelapse_type", timelapse_type);
            if (!support)
                cb_value_change("enable_support", false);
        }
        is_msg_dlg_already_exist = false;
    }

    //BBS
    if (config->opt_enum<PerimeterGeneratorType>("wall_generator") == PerimeterGeneratorType::Arachne &&
        config->opt_bool("enable_overhang_speed"))
    {
        wxString msg_text = _(L("Arachne engine only works when overhang slowing down is disabled.\n"
                               "This may cause decline in the quality of overhang surface when print fastly")) + "\n";
        if (is_global_config)
            msg_text += "\n" + _(L("Disable overhang slowing down automatically? \n"
                "Yes - Enable arachne and disable overhang slowing down\n"
                "No  - Give up using arachne this time"));
        MessageDialog dialog(m_msg_dlg_parent, msg_text, "",
            wxICON_WARNING | (is_global_config ? wxYES | wxNO : wxOK));
        DynamicPrintConfig new_conf = *config;
        is_msg_dlg_already_exist = true;
        auto answer = dialog.ShowModal();
        bool enable_overhang_slow_down = true;
        if (!is_global_config || answer == wxID_YES) {
            new_conf.set_key_value("enable_overhang_speed", new ConfigOptionBool(false));
            enable_overhang_slow_down = false;
        }
        else {
            new_conf.set_key_value("wall_generator", new ConfigOptionEnum<PerimeterGeneratorType>(PerimeterGeneratorType::Classic));
        }
        apply(config, &new_conf);
        if (cb_value_change) {
            if (!enable_overhang_slow_down)
                cb_value_change("enable_overhang_speed", false);
        }
        is_msg_dlg_already_exist = false;
    }

    // BBS
    int filament_cnt = wxGetApp().preset_bundle->filament_presets.size();
#if 0
    bool has_wipe_tower = filament_cnt > 1 && config->opt_bool("enable_prime_tower");
    if (has_wipe_tower && (config->opt_bool("adaptive_layer_height") || config->opt_bool("independent_support_layer_height"))) {
        wxString msg_text;
        if (config->opt_bool("adaptive_layer_height") && config->opt_bool("independent_support_layer_height")) {
            msg_text = _(L("Prime tower does not work when Adaptive Layer Height or Independent Support Layer Height is on.\n"
                "Which do you want to keep?\n"
                "YES - Keep Prime Tower\n"
                "NO  - Keep Adaptive Layer Height and Independent Support Layer Height"));
        }
        else if (config->opt_bool("adaptive_layer_height")) {
            msg_text = _(L("Prime tower does not work when Adaptive Layer Height is on.\n"
                "Which do you want to keep?\n"
                "YES - Keep Prime Tower\n"
                "NO  - Keep Adaptive Layer Height"));
        }
        else {
            msg_text = _(L("Prime tower does not work when Independent Support Layer Height is on.\n"
                "Which do you want to keep?\n"
                "YES - Keep Prime Tower\n"
                "NO  - Keep Independent Support Layer Height"));
        }

        MessageDialog dialog(m_msg_dlg_parent, msg_text, "", wxICON_WARNING | wxYES | wxNO);
        is_msg_dlg_already_exist = true;
        auto answer = dialog.ShowModal();

        DynamicPrintConfig new_conf = *config;
        if (answer == wxID_YES) {
            if (config->opt_bool("adaptive_layer_height"))
                 new_conf.set_key_value("adaptive_layer_height", new ConfigOptionBool(false));

            if (config->opt_bool("independent_support_layer_height"))
                new_conf.set_key_value("independent_support_layer_height", new ConfigOptionBool(false));
        }
        else
            new_conf.set_key_value("enable_prime_tower", new ConfigOptionBool(false));

        apply(config, &new_conf);
        is_msg_dlg_already_exist = false;
    }

    // BBS
    if (has_wipe_tower && config->opt_bool("enable_support") && !config->opt_bool("independent_support_layer_height")) {
        double layer_height = config->opt_float("layer_height");
        double top_gap_raw = config->opt_float("support_top_z_distance");
        //double bottom_gap_raw = config->opt_float("support_bottom_z_distance");
        double top_gap = std::round(top_gap_raw / layer_height) * layer_height;
        //double bottom_gap = std::round(bottom_gap_raw / layer_height) * layer_height;
        if (top_gap != top_gap_raw /* || bottom_gap != bottom_gap_raw*/) {
            DynamicPrintConfig new_conf = *config;
            new_conf.set_key_value("support_top_z_distance", new ConfigOptionFloat(top_gap));
            //new_conf.set_key_value("support_bottom_z_distance", new ConfigOptionFloat(bottom_gap));
            apply(config, &new_conf);

            //wxMessageBox(_L("Support top/bottom Z distance is automatically changed to multiple of layer height."));
        }
    }
#endif

    // Check "enable_support" and "overhangs" relations only on global settings level
    if (is_global_config && config->opt_bool("enable_support")) {
        // Ask only once.
        if (!m_support_material_overhangs_queried) {
            m_support_material_overhangs_queried = true;
            if (!config->opt_bool("detect_overhang_wall")/* != 1*/) {
                //BBS: detect_overhang_wall is setting in develop mode. Enable it directly.
                DynamicPrintConfig new_conf = *config;
                new_conf.set_key_value("detect_overhang_wall", new ConfigOptionBool(true));
                apply(config, &new_conf);
            }
        }
    }
    else {
        m_support_material_overhangs_queried = false;
    }

    if (config->option<ConfigOptionPercent>("sparse_infill_density")->value == 100) {
        std::string  sparse_infill_pattern            = config->option<ConfigOptionEnum<InfillPattern>>("sparse_infill_pattern")->serialize();
        const auto  &top_fill_pattern_values = config->def()->get("top_surface_pattern")->enum_values;
        bool correct_100p_fill = std::find(top_fill_pattern_values.begin(), top_fill_pattern_values.end(), sparse_infill_pattern) != top_fill_pattern_values.end();
        if (!correct_100p_fill) {
            // get sparse_infill_pattern name from enum_labels for using this one at dialog_msg
            const ConfigOptionDef *fill_pattern_def = config->def()->get("sparse_infill_pattern");
            assert(fill_pattern_def != nullptr);
            auto it_pattern = std::find(fill_pattern_def->enum_values.begin(), fill_pattern_def->enum_values.end(), sparse_infill_pattern);
            assert(it_pattern != fill_pattern_def->enum_values.end());
            if (it_pattern != fill_pattern_def->enum_values.end()) {
                wxString msg_text = GUI::format_wxstr(_L("%1% infill pattern doesn't support 100%% density."),
                    _(fill_pattern_def->enum_labels[it_pattern - fill_pattern_def->enum_values.begin()]));
                if (is_global_config)
                    msg_text += "\n" + _L("Switch to rectilinear pattern?\n"
                                          "Yes - switch to rectilinear pattern automaticlly\n"
                                          "No  - reset density to default non 100% value automaticlly") + "\n";
                MessageDialog dialog(m_msg_dlg_parent, msg_text, "",
                                                  wxICON_WARNING | (is_global_config ? wxYES | wxNO : wxOK) );
                DynamicPrintConfig new_conf = *config;
                is_msg_dlg_already_exist = true;
                auto answer = dialog.ShowModal();
                if (!is_global_config || answer == wxID_YES) {
                    new_conf.set_key_value("sparse_infill_pattern", new ConfigOptionEnum<InfillPattern>(ipRectilinear));
                    sparse_infill_density = 100;
                }
                else
                    sparse_infill_density = wxGetApp().preset_bundle->prints.get_selected_preset().config.option<ConfigOptionPercent>("sparse_infill_density")->value;
                new_conf.set_key_value("sparse_infill_density", new ConfigOptionPercent(sparse_infill_density));
                apply(config, &new_conf);
                if (cb_value_change)
                    cb_value_change("sparse_infill_density", sparse_infill_density);
                is_msg_dlg_already_exist = false;
            }
        }
    }

    // BBS
    static const char* keys[] = { "support_filament", "support_interface_filament"};
    for (int i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        std::string key = std::string(keys[i]);
        auto* opt = dynamic_cast<ConfigOptionInt*>(config->option(key, false));
        if (opt != nullptr) {
            if (opt->getInt() > filament_cnt) {
                DynamicPrintConfig new_conf = *config;
                new_conf.set_key_value(key, new ConfigOptionInt(0));
                apply(config, &new_conf);
            }
        }
    }
}

void ConfigManipulation::apply_null_fff_config(DynamicPrintConfig *config, std::vector<std::string> const &keys, std::map<ObjectBase *, ModelConfig *> const &configs)
{
    for (auto &k : keys) {
        if (/*k == "adaptive_layer_height" || */k == "independent_support_layer_height" || k == "enable_support" || k == "detect_thin_wall")
            config->set_key_value(k, new ConfigOptionBool(true));
        else if (k == "wall_loops")
            config->set_key_value(k, new ConfigOptionInt(0));
        else if (k == "top_shell_layers" || k == "enforce_support_layers")
            config->set_key_value(k, new ConfigOptionInt(1));
        else if (k == "sparse_infill_density") {
            double v = config->option<ConfigOptionPercent>(k)->value;
            for (auto &c : configs) {
                auto o = c.second->get().option<ConfigOptionPercent>(k);
                if (o && o->value > v) v = o->value;
            }
            config->set_key_value(k, new ConfigOptionPercent(v)); // sparse_infill_pattern
        }
        else if (k == "detect_overhang_wall")
            config->set_key_value(k, new ConfigOptionBool(false));
        else if (k == "sparse_infill_pattern")
            config->set_key_value(k, new ConfigOptionEnum<InfillPattern>(ipGrid));
    }
}

void ConfigManipulation::toggle_print_fff_options(DynamicPrintConfig *config, const bool is_global_config)
{
    bool have_perimeters = config->opt_int("wall_loops") > 0;
    for (auto el : { "ensure_vertical_shell_thickness", "detect_thin_wall", "detect_overhang_wall",
                    "seam_position", "wall_infill_order", "outer_wall_line_width",
                    "inner_wall_speed", "outer_wall_speed" })
        toggle_field(el, have_perimeters);

    bool have_infill = config->option<ConfigOptionPercent>("sparse_infill_density")->value > 0;
    // sparse_infill_filament uses the same logic as in Print::extruders()
    for (auto el : { "sparse_infill_pattern", "infill_combination",
                    "minimum_sparse_infill_area", "sparse_infill_filament"})
        toggle_line(el, have_infill);

    bool has_spiral_vase         = config->opt_bool("spiral_mode");
    bool has_top_solid_infill 	 = config->opt_int("top_shell_layers") > 0;
    bool has_bottom_solid_infill = config->opt_int("bottom_shell_layers") > 0;
    bool has_solid_infill 		 = has_top_solid_infill || has_bottom_solid_infill;
    // solid_infill_filament uses the same logic as in Print::extruders()
    for (auto el : { "top_surface_pattern", "bottom_surface_pattern", "solid_infill_filament"})
        toggle_field(el, has_solid_infill);

    for (auto el : { "infill_direction", "sparse_infill_line_width", "bridge_angle",
                    "sparse_infill_speed", "bridge_speed" })
        toggle_field(el, have_infill || has_solid_infill);

    toggle_field("top_shell_thickness", ! has_spiral_vase && has_top_solid_infill);
    toggle_field("bottom_shell_thickness", ! has_spiral_vase && has_bottom_solid_infill);

    // Gap fill is newly allowed in between perimeter lines even for empty infill (see GH #1476).
    toggle_field("gap_infill_speed", have_perimeters);

    for (auto el : { "top_surface_line_width", "top_surface_speed" })
        toggle_field(el, has_top_solid_infill || (has_spiral_vase && has_bottom_solid_infill));

    bool have_default_acceleration = config->opt_float("default_acceleration") > 0;
    //BBS
    for (auto el : { "initial_layer_acceleration", "outer_wall_acceleration", "top_surface_acceleration" })
        toggle_field(el, have_default_acceleration);

    bool have_skirt = config->opt_int("skirt_loops") > 0;
    toggle_field("skirt_height", have_skirt && config->opt_enum<DraftShield>("draft_shield") != dsEnabled);
    for (auto el : { "skirt_distance", "draft_shield"})
        toggle_field(el, have_skirt);

    bool have_brim = (config->opt_enum<BrimType>("brim_type") != btNoBrim);
    toggle_field("brim_object_gap", have_brim);
    bool have_brim_width = (config->opt_enum<BrimType>("brim_type") != btNoBrim) && config->opt_enum<BrimType>("brim_type") != btAutoBrim;
    toggle_field("brim_width", have_brim_width);
    // wall_filament uses the same logic as in Print::extruders()
    toggle_field("wall_filament", have_perimeters || have_brim);

    bool have_raft = config->opt_int("raft_layers") > 0;
    bool have_support_material = config->opt_bool("enable_support") || have_raft;
    // BBS
    SupportType support_type = config->opt_enum<SupportType>("support_type");
    bool have_support_interface = config->opt_int("support_interface_top_layers") > 0 || config->opt_int("support_interface_bottom_layers") > 0;
    bool have_support_soluble = have_support_material && config->opt_float("support_top_z_distance") == 0;
    auto support_style = config->opt_enum<SupportMaterialStyle>("support_style");
    for (auto el : { "support_style", "support_base_pattern",
                    "support_base_pattern_spacing", "support_expansion", "support_angle",
                    "support_interface_pattern", "support_interface_top_layers", "support_interface_bottom_layers",
                    "bridge_no_support", "max_bridge_length", "support_top_z_distance", "support_bottom_z_distance",
                     //BBS: add more support params to dependent of enable_support
                    "support_type", "support_on_build_plate_only", "support_critical_regions_only",
                    "support_object_xy_distance", "independent_support_layer_height"})
        toggle_field(el, have_support_material);
    toggle_field("support_threshold_angle", have_support_material && is_auto(support_type));
    //toggle_field("support_closing_radius", have_support_material && support_style == smsSnug);

    bool support_is_tree = config->opt_bool("enable_support") && is_tree(support_type);
    for (auto el : {"tree_support_branch_angle", "tree_support_wall_count", "tree_support_branch_distance", "tree_support_branch_diameter"})
        toggle_field(el, support_is_tree);

    // hide tree support settings when normal is selected
    for (auto el : {"tree_support_branch_angle", "tree_support_wall_count", "tree_support_branch_distance", "tree_support_branch_diameter", "max_bridge_length"})
        toggle_line(el, support_is_tree);

    // tree support use max_bridge_length instead of bridge_no_support
    toggle_line("bridge_no_support", !support_is_tree);

    for (auto el : { "support_interface_spacing", "support_interface_filament",
                     "support_interface_loop_pattern", "support_bottom_interface_spacing" })
        toggle_field(el, have_support_material && have_support_interface);

    //BBS
    bool have_skirt_height = have_skirt &&
                             (config->opt_int("skirt_height") > 1 || config->opt_enum<DraftShield>("draft_shield") != dsEnabled);
    toggle_line("support_speed", have_support_material || have_skirt_height);
    toggle_line("support_interface_speed", have_support_material && have_support_interface);

    // BBS
    //toggle_field("support_material_synchronize_layers", have_support_soluble);

    toggle_field("inner_wall_line_width", have_perimeters || have_skirt || have_brim);
    toggle_field("support_filament", have_support_material || have_skirt);

    toggle_line("raft_contact_distance", have_raft && !have_support_soluble);
    for (auto el : { "raft_first_layer_expansion", "raft_first_layer_density"})
        toggle_line(el, have_raft);

    bool has_ironing = (config->opt_enum<IroningType>("ironing_type") != IroningType::NoIroning);
    for (auto el : { "ironing_flow", "ironing_spacing", "ironing_speed" })
        toggle_line(el, has_ironing);

    // bool have_sequential_printing = (config->opt_enum<PrintSequence>("print_sequence") == PrintSequence::ByObject);
    // for (auto el : { "extruder_clearance_radius", "extruder_clearance_height_to_rod", "extruder_clearance_height_to_lid" })
    //     toggle_field(el, have_sequential_printing);

    bool have_ooze_prevention = config->opt_bool("ooze_prevention");
    toggle_field("standby_temperature_delta", have_ooze_prevention);

    bool have_prime_tower = config->opt_bool("enable_prime_tower");
    for (auto el : { "prime_tower_width", "prime_volume", "prime_tower_brim_width"})
        toggle_line(el, have_prime_tower);

    for (auto el : {"flush_into_infill", "flush_into_support", "flush_into_objects"})
        toggle_field(el, have_prime_tower);

    bool have_avoid_crossing_perimeters = config->opt_bool("reduce_crossing_wall");
    toggle_line("max_travel_detour_distance", have_avoid_crossing_perimeters);

    bool has_overhang_speed = config->opt_bool("enable_overhang_speed");
    for (auto el : { "overhang_1_4_speed", "overhang_2_4_speed", "overhang_3_4_speed", "overhang_4_4_speed"})
        toggle_line(el, has_overhang_speed);

    toggle_line("flush_into_objects", !is_global_config);

    bool has_fuzzy_skin = (config->opt_enum<FuzzySkinType>("fuzzy_skin") != FuzzySkinType::None);
    for (auto el : { "fuzzy_skin_thickness", "fuzzy_skin_point_distance"})
        toggle_line(el, has_fuzzy_skin);

    // C11 printer is not support smooth timelapse
    PresetBundle *preset_bundle  = wxGetApp().preset_bundle;
    std::string str_preset_type = preset_bundle->printers.get_edited_preset().get_printer_type(preset_bundle);
    toggle_field("timelapse_type", str_preset_type != "C11");

    bool have_arachne = config->opt_enum<PerimeterGeneratorType>("wall_generator") == PerimeterGeneratorType::Arachne;
    for (auto el : { "wall_transition_length", "wall_transition_filter_deviation", "wall_transition_angle",
        "min_feature_size", "min_bead_width", "wall_distribution_count" })
        toggle_line(el, have_arachne);
    toggle_field("detect_thin_wall", !have_arachne);
    toggle_field("enable_overhang_speed", !have_arachne);
    toggle_field("only_one_wall_top", !have_arachne);
}

void ConfigManipulation::update_print_sla_config(DynamicPrintConfig* config, const bool is_global_config/* = false*/)
{
    double head_penetration = config->opt_float("support_head_penetration");
    double head_width = config->opt_float("support_head_width");
    if (head_penetration > head_width) {
        //wxString msg_text = _(L("Head penetration should not be greater than the head width."));
        wxString msg_text = "Head penetration should not be greater than the head width.";

        //MessageDialog dialog(m_msg_dlg_parent, msg_text, _(L("Invalid Head penetration")), wxICON_WARNING | wxOK);
        MessageDialog dialog(m_msg_dlg_parent, msg_text, "Invalid Head penetration", wxICON_WARNING | wxOK);
        DynamicPrintConfig new_conf = *config;
        if (dialog.ShowModal() == wxID_OK) {
            new_conf.set_key_value("support_head_penetration", new ConfigOptionFloat(head_width));
            apply(config, &new_conf);
        }
    }

    double pinhead_d = config->opt_float("support_head_front_diameter");
    double pillar_d = config->opt_float("support_pillar_diameter");
    if (pinhead_d > pillar_d) {
        //wxString msg_text = _(L("Pinhead diameter should be smaller than the pillar diameter."));
        wxString msg_text = "Pinhead diameter should be smaller than the pillar diameter.";

        //MessageDialog dialog(m_msg_dlg_parent, msg_text, _(L("Invalid pinhead diameter")), wxICON_WARNING | wxOK);
        MessageDialog dialog(m_msg_dlg_parent, msg_text, "Invalid pinhead diameter", wxICON_WARNING | wxOK);

        DynamicPrintConfig new_conf = *config;
        if (dialog.ShowModal() == wxID_OK) {
            new_conf.set_key_value("support_head_front_diameter", new ConfigOptionFloat(pillar_d / 2.0));
            apply(config, &new_conf);
        }
    }
}

void ConfigManipulation::toggle_print_sla_options(DynamicPrintConfig* config)
{
    bool supports_en = config->opt_bool("supports_enable");

    toggle_field("support_head_front_diameter", supports_en);
    toggle_field("support_head_penetration", supports_en);
    toggle_field("support_head_width", supports_en);
    toggle_field("support_pillar_diameter", supports_en);
    toggle_field("support_small_pillar_diameter_percent", supports_en);
    toggle_field("support_max_bridges_on_pillar", supports_en);
    toggle_field("support_pillar_connection_mode", supports_en);
    toggle_field("support_buildplate_only", supports_en);
    toggle_field("support_base_diameter", supports_en);
    toggle_field("support_base_height", supports_en);
    toggle_field("support_base_safety_distance", supports_en);
    toggle_field("support_critical_angle", supports_en);
    toggle_field("support_max_bridge_length", supports_en);
    toggle_field("support_max_pillar_link_distance", supports_en);
    toggle_field("support_points_density_relative", supports_en);
    toggle_field("support_points_minimal_distance", supports_en);

    bool pad_en = config->opt_bool("pad_enable");

    toggle_field("pad_wall_thickness", pad_en);
    toggle_field("pad_wall_height", pad_en);
    toggle_field("pad_brim_size", pad_en);
    toggle_field("pad_max_merge_distance", pad_en);
 // toggle_field("pad_edge_radius", supports_en);
    toggle_field("pad_wall_slope", pad_en);
    toggle_field("pad_around_object", pad_en);
    toggle_field("pad_around_object_everywhere", pad_en);

    bool zero_elev = config->opt_bool("pad_around_object") && pad_en;

    toggle_field("support_object_elevation", supports_en && !zero_elev);
    toggle_field("pad_object_gap", zero_elev);
    toggle_field("pad_around_object_everywhere", zero_elev);
    toggle_field("pad_object_connector_stride", zero_elev);
    toggle_field("pad_object_connector_width", zero_elev);
    toggle_field("pad_object_connector_penetration", zero_elev);
}

} // GUI
} // Slic3r
