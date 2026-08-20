#ifndef slic3r_GUI_ExtruderHelpers_hpp_
#define slic3r_GUI_ExtruderHelpers_hpp_

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"
#include "slic3r/GUI/Widgets/MultiNozzleSync.hpp"

namespace Slic3r {

class MachineObject;

namespace GUI {

// Plater-side helpers that read device (MachineObject) state and turn it into slicer
// config, sync-status, and serialized strings. These do NOT drive the hardware; the
// DeviceCore/Dev* files own the actual machine interaction.

std::string get_diameter_string(float diameter);

std::map<std::pair<int, int>, int> collect_ams_counts(MachineObject *obj);

// check whether nozzle status is synced with printer; extruder = -1 means check both
bool is_extruder_stat_synced(int target_extruder_id = -1);

std::string serialize_nozzle_config(const std::map<int, std::vector<DevNozzle>> &nozzle_cfg_map);
std::map<int, std::vector<DevNozzle>> deserialize_nozzle_config(const std::string &config_str);
bool is_same_nozzle_config(const std::map<int, std::vector<DevNozzle>> &config1, const std::map<int, std::vector<DevNozzle>> &config2);

std::string serialize_nozzle_option(const NozzleOption &option);
std::optional<NozzleOption> deserialize_nozzle_option(const std::string &option_str);

// get the best diameter for given extruder
// priority:
// diameter with the most nozzle count
// 0.4 -> 0.2 -> 0.6 -> 0.8
float get_best_diameter(MachineObject *obj, int extruder_index);

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_ExtruderHelpers_hpp_
