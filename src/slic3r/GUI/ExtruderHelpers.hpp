#ifndef slic3r_GUI_ExtruderHelpers_hpp_
#define slic3r_GUI_ExtruderHelpers_hpp_

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <wx/string.h>

#include "libslic3r/NozzleInventory.hpp"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"

namespace Slic3r {

class MachineObject;

namespace GUI {

/**
 * \brief One nozzle diameter installed on the printer and how many nozzles of each volume type carry it.
 */
struct NozzleOption
{
    std::string                                                        diameter;
    std::unordered_map<int, std::unordered_map<NozzleVolumeType, int>> extruder_nozzle_stats; ///< extruder id -> volume type -> nozzle count
};

/// \brief Health of a nozzle slot as the printer reports it
enum class NozzleStatus : int
{
    nsNormal = 0,
    nsError,
    nsUnknown
};

/**
 * \brief One nozzle slot of an extruder
 *
 * Caution: this type mixes two sources. `diameter` and `volume` are the project's slot, which the user
 * edits. `status`, `ams_slots` and `has_bind_ams` are a printer snapshot that
 * collect_nozzle_configs_from_machine() takes at sync time; they go stale once the user edits the slot.
 *
 * TODO: move the printer fields into their own machine-slot type with one collector. That type would
 * also carry what the slot shows, which machine_slots() now reports on a separate path.
 */
struct NozzleConfig
{
    wxString                         diameter;                        ///< project; empty means no nozzle
    NozzleVolumeType                 volume{nvtStandard};             ///< project
    NozzleStatus                     status{NozzleStatus::nsUnknown}; ///< printer snapshot
    std::vector<std::pair<int, int>> ams_slots;                       ///< printer snapshot: feed and preload AMS slots
    bool                             has_bind_ams = false;            ///< printer snapshot; nothing reads it yet

    NozzleConfig() = default;
    NozzleConfig(const wxString &diameter_, NozzleVolumeType volume_, NozzleStatus status_) : diameter(diameter_), volume(volume_), status(status_) {}
    NozzleConfig(const wxString &diameter_, NozzleVolumeType volume_) : diameter(diameter_), volume(volume_), status(NozzleStatus::nsNormal) {}

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

/**
 * \brief Check if the nozzle inventory matches the selected machine slot for slot
 *
 * Returns false when the printer model differs. AMS is not checked.
 *
 * \param target_extruder_id  0-based logical extruder, -1 for all extruders
 */
bool is_extruder_stat_synced(int target_extruder_id = -1);

/**
 * \brief Show the filament switcher tip with a "Learn more" wiki link (modal, centred on the main window)
 *
 * \param ready  true if the switcher is calibrated and usable; false shows the calibrate warning
 */
void show_filament_switcher_tip(bool ready);

// The nozzles the machine reports for one logical extruder, as slot-ordered configs; slots the
// machine cannot describe come back with an empty diameter. Always `extruder_nozzle_count` entries.
// Caution: its health rule (IsNormal/IsUnknown, on rack printers only) is not the device-page rule that
// machine_slots() applies, so one slot can read healthy here and faulty there.
std::vector<NozzleConfig> collect_nozzle_configs_from_machine(MachineObject *obj, int extruder_idx, int extruder_nozzle_count);

/**
 * \brief Convert nozzle configs into nozzle inventory slots
 *
 * Every write into the nozzle inventory and the synced check use it, so they read a slot the same way.
 *
 * \param configs  slot-ordered configs; an empty config gives an empty slot
 */
ExtruderSlots nozzle_slots_of(const std::vector<NozzleConfig> &configs);

/// \brief What the printer shows in one nozzle slot
enum class MachineSlot
{
    Blank,   ///< nothing to show: no printer, or the slot is absent, lost or faulty
    Hotend,  ///< a healthy hotend
    Cutter,  ///< a healthy cutter
    Pen,     ///< a healthy pen
    Unknown, ///< the printer has not identified the tool yet; a refresh resolves it
};

/**
 * \brief What each slot of one extruder shows, as the device page shows it
 *
 * \param obj       connected printer; nullptr gives an empty result
 * \param extruder  logic extruder id
 * \return one entry per slot of the edited printer preset. Empty when the printer has no nozzle
 *         system or the preset has no slot count.
 *
 * The sidebar calls this on every printer message, not only on sync, so the tiles follow a tool swap.
 */
std::vector<MachineSlot> machine_slots(const MachineObject *obj, int extruder);

/// \brief How a nozzle slot looks on the sidebar tile and the nozzle settings card
enum class SlotFace
{
    Hotend, ///< the project has a nozzle in this slot
    Empty,  ///< no usable nozzle in this slot
    Cutter, ///< the printer reports a cutter in a slot the project leaves empty
    Pen,    ///< the printer reports a pen in a slot the project leaves empty
    Unknown, ///< the printer reports an unidentified tool in a slot the project leaves empty
};

/**
 * \brief The face of one slot: the project's nozzle, or what the printer reports in an empty slot
 *
 * \param project  the slot in the project
 * \param machine  what the printer shows in that slot; pass Blank when not synced
 */
SlotFace slot_face(const NozzleConfig &project, MachineSlot machine);

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

/**
 * \brief The diameter to slice at with the given nozzle slots
 *
 * \param wanted   the diameter the user selected
 * \param nozzles  every configured nozzle slot of the printer
 * \return \p wanted while a nozzle carries it; else the carried diameter first in the order
 *         0.4, 0.2, 0.6, 0.8, others; \p wanted when no slot carries a nozzle
 */
wxString usable_nozzle_diameter(const wxString &wanted, const std::vector<NozzleConfig> &nozzles);

// Whether the machine currently carries at least one usable nozzle of `diameter` on `extruder_index`.
// A nozzle-rack machine holds several diameters at once, so the nozzle mounted on the extruder does
// not tell whether a diameter is available: the whole inventory (extruder + rack) has to be checked.
bool machine_has_nozzle_diameter(MachineObject *obj, int extruder_index, float diameter);

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_ExtruderHelpers_hpp_
