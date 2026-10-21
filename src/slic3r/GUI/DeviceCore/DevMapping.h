#pragma once
#include "libslic3r/CommonDefs.hpp"
#include "libslic3r/ProjectTask.hpp"

#include "DevDefs.h"
#include "slic3r/Utils/json_diff.hpp"
#include <wx/string.h>

namespace Slic3r
{
//Previous definitions
class MachineObject;
struct DevNozzle;

enum MappingOption
{
    USE_LEFT_AMS = 0,
    USE_RIGHT_AMS,
    USE_LEFT_EXT,
    USE_RIGHT_EXT
};

// Why a hotend cannot be fed from a given slot without the user touching the
// machine. The UI reacts differently per value, so they must stay distinct.
enum class DevSlotFeedBlock
{
    None = 0,          // the printer can load this slot on its own
    NeedManualFeed,    // the slot has no feeder of its own and its filament is not in the feed path
    BlockedByRackAms,  // rack AMS filament occupies the hotend and cannot be retracted automatically
    BlockedByExtSpool, // an external spool is engaged and has to be pulled out by hand
};

// How strongly a candidate slot's nozzle (diameter + flow type) constrains the auto-map.
enum class NozzleMatchWeight
{
    Ignore = 0, // the slot's nozzle is not considered
    Prefer,     // soft: a mismatch only loses a near-equal colour tie to a matching slot
    Require,    // hard: a mismatch is barred (filament left unmapped when no slot fits)
};

class DevMappingUtil
{
public:
    DevMappingUtil() = delete;
    ~DevMappingUtil() = delete;

public:
    // Why this slot cannot feed the hotend on its own. Reports the hotend
    // occupant's block first, since it bars every slot behind it.
    static DevSlotFeedBlock QuerySlotFeedBlock(const DevNozzle& nz, DevAmsType ams_type, const DevAmsSlotId& slot);

    // The block whatever occupies this hotend imposes on every slot behind it.
    // Lets the UI explain it once per hotend instead of per slot.
    static DevSlotFeedBlock QueryNozzleOccupantBlock(const DevNozzle& nz);

    // Whether the ams can feed this slot without the user touching the machine.
    static bool IsSlotAutoLoadable(const DevNozzle& nz, DevAmsType ams_type, const DevAmsSlotId& slot);

    static bool is_valid_mapping_result(const MachineObject* obj, std::vector<FilamentInfo>& result, bool check_empty_slot = false);

    /**
     * \brief Auto-map project filaments onto the machine's slots by type, product identity and colour.
     *
     * \param color_over_subtype  rank same-type slots by colour alone; product identity (setting_id /
     *                            filament_id) then only breaks a near-equal colour tie. Off keeps product
     *                            identity dominant over colour.
     * \param nozzle_weight       how much a slot's nozzle (diameter + flow type) constrains the match:
     *                            Ignore skips it; Prefer only breaks a near-equal colour tie toward a
     *                            matching nozzle; Require bars a mismatched slot from auto mapping.
     * \param allow_manual_feed   let slots the printer cannot load on its own (e.g. an unloaded external
     *                            AMS) take part; on an equal match a self-loadable slot still wins.
     *                            Only affects hotend-rack machines.
     */
    static int ams_filament_mapping(const MachineObject* obj, const std::vector<FilamentInfo>& filaments, std::vector<FilamentInfo>& result, std::vector<bool> map_opt, std::vector<int> exclude_id = std::vector<int>(), bool nozzle_has_ams_then_ignore_ext = false, bool color_over_subtype = false, NozzleMatchWeight nozzle_weight = NozzleMatchWeight::Ignore, bool allow_manual_feed = false);

    // List every installed AMS / external slot on the machine as a mapping candidate, with
    // nozzle_diameter filled from the slot's bound nozzle. A diameter-aware picker uses these to
    // offer destinations and to flag diameter mismatches. Empty when the machine has no filament system.
    static std::vector<FilamentInfo> enumerate_dest_slots(const MachineObject* obj);
};

};