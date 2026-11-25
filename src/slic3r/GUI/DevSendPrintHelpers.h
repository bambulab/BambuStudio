/*****************************************************************//**
 * \file   DevSendPrintHelpers.h
 * \brief  Shared helpers for send-print UI text.
 *
 * \author xin.zhang
 * \date   November 2026
 *********************************************************************/
#ifndef slic3r_GUI_DevSendPrintHelpers_h_
#define slic3r_GUI_DevSendPrintHelpers_h_

#include <memory>
#include <string>

#include <wx/string.h>

namespace Slic3r {
struct DevSliceContext;
class MachineObject;

namespace GUI {

// Build a nozzle title like "Left nozzle" / "Right nozzle", and append the
// nozzle spec as "(0.4 HF)" while multi_nozzle_diameter_mode is on: the sliced
// one when `slice_context` is given, else the nozzle mounted on the extruder of
// `obj` (the sync dialog path, where there is no slice snapshot yet).
wxString GetNozzleTitleLabel(int total_extruder_count, const std::string& printer_type, const std::shared_ptr<DevSliceContext>& slice_context, MachineObject* obj /*could be nullptr*/, int extruder_id);

// Format the sliced nozzle spec (e.g. "0.4 Std", "0.6 HF") for a single
// filament from slice_context.
wxString GetSliceFilamentNozzleSpec(const std::shared_ptr<DevSliceContext>& slice_context, int filament_id);

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_DevSendPrintHelpers_h_
