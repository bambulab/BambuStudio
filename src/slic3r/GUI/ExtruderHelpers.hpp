#ifndef slic3r_GUI_ExtruderHelpers_hpp_
#define slic3r_GUI_ExtruderHelpers_hpp_

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <wx/string.h>

#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"
#include "slic3r/GUI/Widgets/MultiNozzleSync.hpp"

namespace Slic3r {

class MachineObject;

namespace GUI {

enum class NozzleStatus : int;

struct NozzleConfig
{
    wxString                         diameter;
    NozzleVolumeType                 volume{nvtStandard};
    NozzleStatus                     status{2}; // unknown
    std::vector<std::pair<int, int>> ams_slots;
    bool                             has_bind_ams = false;

    NozzleConfig() = default;
    NozzleConfig(const wxString &diameter_, NozzleVolumeType volume_, NozzleStatus status_) : diameter(diameter_), volume(volume_), status(status_) {}
    NozzleConfig(const wxString &diameter_, NozzleVolumeType volume_) : diameter(diameter_), volume(volume_), status(static_cast<NozzleStatus>(0)) {}

    bool is_empty() const { return diameter.IsEmpty(); }
};

/**
 * \brief Human-readable nozzle spec for a config, e.g. "0.4mm Standard".
 *
 * Returns a localized status word ("Empty" / "Error" / "Unknown") when the nozzle is not usable.
 *
 * \param cfg  the nozzle configuration to describe
 */
wxString format_nozzle_spec(const NozzleConfig &cfg);

// Plater-side helpers that read device (MachineObject) state and turn it into slicer
// config, sync-status, and serialized strings. These do NOT drive the hardware; the
// DeviceCore/Dev* files own the actual machine interaction.

std::string get_diameter_string(float diameter);

// "0.4 mm" -> "0.4": keep leading digits/dot, stop at the first other character.
inline wxString parse_diameter_value(const wxString &label)
{
    wxString value;
    for (const auto &ch : label) {
        if (wxIsdigit(ch) || ch == '.')
            value += ch;
        else
            break;
    }
    return value;
}

// A flow combo stores its NozzleVolumeType as client data, offset by +1 so a null pointer is
// distinguishable from index 0. Shared by the row (list) and card nozzle dialogs.
inline void *encode_volume(int volume_index) { return reinterpret_cast<void *>(static_cast<intptr_t>(volume_index) + 1); }
inline int   decode_volume(void *data) { return static_cast<int>(reinterpret_cast<intptr_t>(data) - 1); }

std::map<std::pair<int, int>, int> collect_ams_counts(MachineObject *obj);

// check whether nozzle status is synced with printer; extruder = -1 means check both
bool is_extruder_stat_synced(int target_extruder_id = -1);

// The nozzles the machine reports for one logical extruder, as slot-ordered configs; slots the
// machine cannot describe come back with an empty diameter. Always `extruder_nozzle_count` entries.
std::vector<NozzleConfig> collect_nozzle_configs_from_machine(MachineObject *obj, int extruder_idx, int extruder_nozzle_count);

// What the printer reports right now, for callers deciding "is this nozzle layout synced".
// `synced` is false whenever no connected printer matches the selected preset; a caller must treat
// that as "not synced" outright rather than comparing configs against whatever it was seeded with.
struct MachineNozzleBaseline
{
    bool                      synced{false};
    std::vector<NozzleConfig> configs[2]; // 0 = left/single, 1 = right
};

MachineNozzleBaseline collect_machine_nozzle_baseline(bool is_dual);

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
