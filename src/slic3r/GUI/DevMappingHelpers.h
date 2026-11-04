/*****************************************************************//**
 * \file   DevMappingHelpers.h
 * \brief  Shared helpers for AMS / hotend mapping (popup and Web VM).
 *
 * Hosts the slicing-vs-device nozzle compatibility checks and the
 * MachineObject -> TrayData adapter so both the legacy wxWidgets popup
 * and the new DevicePageAmsMappingHotendVM consume the same logic.
 *
 * \author xin.zhang
 * \date   May 2026
 *********************************************************************/
#ifndef slic3r_GUI_DevMappingHelpers_h_
#define slic3r_GUI_DevMappingHelpers_h_

#include <optional>
#include <string>
#include <vector>

#include <wx/string.h>

#include "AmsMappingPopup.hpp"

#include "DeviceCore/DevNozzleSystem.h"

namespace Slic3r {
class MachineObject;
class DevAms;
class DevAmsTray;
struct DevSliceContext;
namespace MultiNozzleUtils { class NozzleGroupResultBase; }
namespace GUI {

class Plater;

// Slicing nozzle requirement for a specific filament.
// Used by hotend mapping to flag slots whose physical nozzle (diameter
// or flow type) differs from what the current filament was sliced with.
struct ExpectedNozzleSpec
{
    float          diameter    = -1.0f;
    NozzleFlowType flow        = NozzleFlowType::NONE_FLOWTYPE;
    NozzleType     nozzle_type = NozzleType::ntUndefine;
    bool           valid       = false;
};

enum class HotendSlotState {
    Normal,             // selectable / regular
    MaterialUnmatch,    // filament type mismatch, dimmed
    MaterialEmpty,      // nozzle present but no filament loaded
    NozzleUnmatch,      // nozzle dia / flow mismatch, dimmed
    NozzleEmpty,        // no nozzle on this rack position
    NozzleLost,         // holder reports connected but the tool is detached
    NozzleUncalibrated, // rack slot has not completed docking calibration
    NozzleError,        // firmware reports the hotend as abnormal
    NozzleUnknown       // firmware cannot describe the hotend yet
};

// Combined judgement for one hotend slot.
//
// `state` is the visual driver. The 4 atomic booleans expose raw facts so
// the UI can compose richer tooltips / future warnings without extending
// the state enum. `is_pickable` is the final selectability verdict that
// already accounts for global mode (e.g. all-ext relaxation) — it is set
// by `BuildSlotJson` after applying the per-card pickable rule.
struct SlotJudgement
{
    HotendSlotState state          = HotendSlotState::Normal;
    bool            material_match = true;   // filament type vs slicing spec
    bool            nozzle_match   = true;   // nozzle dia / flow vs slicing spec
    bool            has_filament   = false;  // td.type != EMPTY
    bool            has_nozzle     = false;  // rack nozzle present at this pos
    bool            is_pickable    = false;  // user can click this card
};

// Grouping unit for the multi-AMS expand panel under a hotend slot.
// One group represents one MappingContainer card (e.g. ZMULTI self tray
// or a bound N3S / N3F sub-AMS).
struct SubAmsGroup
{
    wxString              name;
    std::vector<TrayData> trays;
    bool                  is_preload = false;
};

// Resolve per-filament expected nozzle from the slice snapshot
ExpectedNozzleSpec ResolveExpectedNozzleSpecFromSliceContext(
    int filament_id,
    const DevSliceContext& slice_context);

#if 0
// Superseded by ResolveExpectedNozzleSpecFromSliceContext: the Plater only holds the last
// local slice, which is absent or belongs to another project when printing from SD card
// or from the print queue. Kept for reference until the new source is validated on device.

// Resolve per-filament expected nozzle from slice group result only.
// Returns spec.valid=false when group_result is null or filament has no nozzles.
ExpectedNozzleSpec ResolveExpectedNozzleSpecFromGroupResult(
    int filament_id,
    const MultiNozzleUtils::NozzleGroupResultBase* group_result);

// Unified entry: resolve from slice group result when available.
ExpectedNozzleSpec ResolveExpectedNozzleSpec(int filament_id, Plater* plater = nullptr);
#endif

// Resolve the rack nozzle bound to this tray's slot. Returns std::nullopt
// when the nozzle cannot be located (no rack, slot not bound to a rack
// nozzle position, or the nozzle index falls outside the rack range).
//
// Callers should then query the resulting DevNozzle for IsExist() /
// IsAbnormal() / IsUnknown() / GetNozzleDiameter() / GetNozzleFlowType()
// as needed. We deliberately return the raw object instead of a pre-baked
// "compatible?" verdict so different consumers (slot state vs. info row)
// can apply their own rules from a single query.
std::optional<DevNozzle> QueryRackNozzleAt(MachineObject*  obj,
                                           const TrayData& td);

// Adapt a single DevAmsTray into the legacy TrayData used by the popup
// renderers. Returns std::nullopt on parse failure of ams_id / tray_id.
std::optional<TrayData> MakeTrayData(MachineObject*    obj,
                                     DevAms*           ams /*nullable*/,
                                     DevAmsTray*       tray,
                                     const std::string& ams_id_str);

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_DevMappingHelpers_h_
