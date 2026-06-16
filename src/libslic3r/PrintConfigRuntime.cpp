#include "PrintConfig.hpp"

#include <algorithm>

namespace Slic3r {

t_config_option_keys DynamicPrintConfig::normalize_fdm_2(int num_objects, int used_filaments, DynamicConfig *ori_values)
{
    t_config_option_keys changed_keys;
    ConfigOptionBool *   ept_opt = this->option<ConfigOptionBool>("enable_prime_tower");
    if (used_filaments > 0 && ept_opt != nullptr) {
        ConfigOptionBool *                islh_opt = this->option<ConfigOptionBool>("independent_support_layer_height", true);
        ConfigOptionEnum<PrintSequence> * ps_opt = this->option<ConfigOptionEnum<PrintSequence>>("print_sequence");
        ConfigOptionEnum<TimelapseType> * timelapse_opt = this->option<ConfigOptionEnum<TimelapseType>>("timelapse_type");
        const bool                        is_smooth_timelapse = timelapse_opt != nullptr && timelapse_opt->value == TimelapseType::tlSmooth;
        ConfigOptionBool *                enable_wrapping_opt = this->option<ConfigOptionBool>("enable_wrapping_detection");
        const bool                        enable_wrapping = enable_wrapping_opt != nullptr && enable_wrapping_opt->value;

        bool has_mixed_filament = false;
        if (auto *mixed_opt = this->option<ConfigOptionBools>("filament_is_mixed")) {
            has_mixed_filament = std::any_of(
                mixed_opt->values.begin(),
                mixed_opt->values.end(),
                [](unsigned char value) { return value != 0; });
        }

        if (!is_smooth_timelapse && !enable_wrapping
            && ((used_filaments == 1 && !has_mixed_filament)
                || (ps_opt->value == PrintSequence::ByObject && num_objects > 1))) {
            if (ept_opt->value) {
                if (ori_values)
                    ori_values->set_key_value("enable_prime_tower", ept_opt->clone());
                ept_opt->value = false;
                changed_keys.push_back("enable_prime_tower");
            }
        } else if (ori_values && ori_values->has("enable_prime_tower")) {
            ept_opt->value = ori_values->opt_bool("enable_prime_tower");
            changed_keys.push_back("enable_prime_tower");
        }

        if (ept_opt->value && islh_opt && islh_opt->value) {
            islh_opt->value = false;
            changed_keys.push_back("independent_support_layer_height");
        }
    }

    return changed_keys;
}

} // namespace Slic3r
