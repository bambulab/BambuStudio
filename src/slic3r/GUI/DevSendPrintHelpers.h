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

namespace GUI {

// Build a nozzle title like "Left nozzle" / "Right nozzle", and append the
// sliced nozzle spec as "(0.4 HF)" while multi_nozzle_diameter_mode is on.
wxString GetNozzleTitleLabel(int total_extruder_count, const std::string& printer_type, const std::shared_ptr<DevSliceContext>& slice_context, int extruder_id);

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_DevSendPrintHelpers_h_
