#ifndef slic3r_GUI_ExtruderHelpers_hpp_
#define slic3r_GUI_ExtruderHelpers_hpp_

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <wx/string.h>

#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"

namespace Slic3r {

class MachineObject;

namespace GUI {

enum class NozzleStatus : int;

/**
 * \brief One nozzle diameter installed on the printer and how many nozzles of each volume type carry it.
 */
struct NozzleOption
{
    std::string                                                        diameter;
    std::unordered_map<int, std::unordered_map<NozzleVolumeType, int>> extruder_nozzle_stats; ///< extruder id -> volume type -> nozzle count
};

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
 * Driven by the configured diameter/volume, not the live machine status, so a disconnected nozzle
 * still reads as its preset spec. Returns "Empty" when no diameter is set.
 *
 * \param cfg  the nozzle configuration to describe
 */
wxString format_nozzle_spec(const NozzleConfig &cfg);

/**
 * \brief How a printer's nozzles are arranged, which decides the sidebar nozzle controls and the nozzle settings dialog
 */
enum class NozzleLayout
{
    SingleExtruder,     ///< one extruder with one nozzle, picked on a single combo box
    DualExtruder,       ///< two extruders with one nozzle each, picked on a combo box per extruder
    DualExtruderRack,   ///< two extruders, one of them drawing from a nozzle rack; edited in the list dialog
    SingleExtruderRack, ///< one extruder drawing from a nozzle rack; edited in the card dialog
};

/**
 * \brief Nozzle layout of a printer
 *
 * \param printer_config  a printer preset's config
 */
NozzleLayout nozzle_layout_of(const ConfigBase &printer_config);

/**
 * \brief True if the layout has two extruders
 */
bool is_dual_extruder(NozzleLayout layout);

/**
 * \brief True if the layout holds several nozzles on an extruder, so nozzles are edited in a settings dialog
 */
bool has_nozzle_rack(NozzleLayout layout);

// Plater-side helpers that read device (MachineObject) state and turn it into slicer
// config, sync-status, and serialized strings. These do NOT drive the hardware; the
// DeviceCore/Dev* files own the actual machine interaction.

std::string get_diameter_string(float diameter);

// Whether printing a filament on the `to` nozzle instead of the `from` one is coarse enough to warn the
// user about lost detail: leaving a 0.2 nozzle, or landing on a 0.8 one. A diameter <= 0 is unknown and
// never warns.
bool nozzle_loses_detail(double from, double to);

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

/**
 * \brief Whether a nozzle of this (diameter, flow) is a product that exists at all.
 *
 * A printer preset's extruder_variant_list is its list of *parameter* variants, not of nozzles that
 * can be fitted: a 0.2 preset carries "Direct Drive High Flow" so the profile set stays complete,
 * though no 0.2 high-flow nozzle is made. Menus built from that list have to drop the impossible
 * combinations rather than offer them greyed -- "greyed" has to keep meaning "you could have this
 * but do not", the only reading a user can act on.
 *
 * Mirrors the per-extruder filter in Plater.cpp's update_extruder_variant: E3D high flow is only
 * made in 0.4 and 0.6, and there is no 0.2 high-flow nozzle.
 */
inline bool nozzle_variant_exists(const std::string &diameter, NozzleVolumeType volume_type)
{
    if (volume_type == NozzleVolumeType::nvtE3DHighFlow && diameter != "0.4" && diameter != "0.6")
        return false;
    if (diameter == "0.2" && volume_type == NozzleVolumeType::nvtHighFlow)
        return false;
    return true;
}

// A flow combo stores its NozzleVolumeType as client data, offset by +1 so a null pointer is
// distinguishable from index 0. Shared by the row (list) and card nozzle dialogs.
inline void *encode_volume(int volume_index) { return reinterpret_cast<void *>(static_cast<intptr_t>(volume_index) + 1); }
inline int   decode_volume(void *data) { return static_cast<int>(reinterpret_cast<intptr_t>(data) - 1); }

/**
 * \brief A slot's name for filament mapping: "A1" (four-slot unit), "HT-A" (single-slot unit), "Ext" (no
 * AMS unit), or the nozzle number "1" for an internal slot (one slot per nozzle).
 *
 * \param obj      the machine holding the slot
 * \param ams_id   the slot's AMS id
 * \param slot_id  0-based slot within the unit
 */
std::string slot_name(MachineObject *obj, int ams_id, int slot_id);

using AmsIdMap = std::map<std::pair<int, int>, std::vector<std::string>>;
AmsIdMap collect_ams_ids(MachineObject *obj);
std::map<std::pair<int, int>, int> collect_ams_counts(MachineObject *obj);

// True when the selected printer preset's type matches the connected machine and the nozzle
// groups agree. extruder = -1 means check both extruders.
bool is_extruder_stat_synced(int target_extruder_id = -1);

// The nozzles the machine reports for one logical extruder, as slot-ordered configs; slots the
// machine cannot describe come back with an empty diameter. Always `extruder_nozzle_count` entries.
std::vector<NozzleConfig> collect_nozzle_configs_from_machine(MachineObject *obj, int extruder_idx, int extruder_nozzle_count);

// What the printer reports right now, for callers deciding "is this nozzle layout synced".
// `synced` only means a connected printer of the selected preset's model answered; it says nothing
// about whether the configs agree. A caller must treat it as a precondition -- false means "not
// synced" outright, true still leaves the per-slot comparison to the caller.
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

// Whether the machine currently carries at least one usable nozzle of `diameter` on `extruder_index`.
// A nozzle-rack machine holds several diameters at once, so the nozzle mounted on the extruder does
// not tell whether a diameter is available: the whole inventory (extruder + rack) has to be checked.
bool machine_has_nozzle_diameter(MachineObject *obj, int extruder_index, float diameter);

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_ExtruderHelpers_hpp_
