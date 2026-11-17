#ifndef slic3r_DevicePresetUtil_hpp_
#define slic3r_DevicePresetUtil_hpp_

#include "PresetBundle.hpp"

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace Slic3r {

class DevicePresetUtil
{
public:
    struct FilamentPresetQuery
    {
        std::string           filament_id;
        std::string           vendor;
        std::string           filament_type;
        std::string           printer_type;
        std::vector<float>    nozzle_diameters;
        bool                  only_system{false};
        bool                  with_fallback{true};
    };

    struct FilamentPresetQueryResult
    {
        const Preset*    best_preset{nullptr};
        FilamentBaseInfo best_preset_info;
        std::string best_preset_name() const;

        std::vector<const Preset*> compatible_presets;
        std::vector<const Preset*> incompatible_presets;
        std::vector<std::string> compatible_preset_names() const;
        std::vector<std::string> incompatible_preset_names() const;
    };

    static FilamentPresetQueryResult query_filament_preset(const PresetBundle& bundle, const FilamentPresetQuery& query);
    static std::optional<FilamentBaseInfo> query_best_filament_preset(const PresetBundle& bundle, const FilamentPresetQuery& query);
};

} // namespace Slic3r

#endif // slic3r_DevicePresetUtil_hpp_
