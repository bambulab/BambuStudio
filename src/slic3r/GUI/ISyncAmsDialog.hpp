#pragma once

/**
 * \file
 * \brief Common ground between the legacy SyncAmsInfoDialog and its replacement, SyncAmsDialog.
 *
 * SyncAmsInfoDialog is being replaced step by step, and the goal is for SyncAmsDialog to handle every
 * Sync-AMS entry point. Until every caller has moved over, both dialogs report their outcome through
 * SyncResult. Remove this header together with SyncAmsInfoDialog.
 */

#include <map>

#include <wx/defs.h> // wxID_CANCEL

#include "libslic3r/PresetBundle.hpp"

namespace Slic3r::GUI {

/**
 * \brief The outcome the Plater applies after a Sync-AMS dialog closes.
 *
 * Shared by the legacy SyncAmsInfoDialog and the new SyncAmsDialog, which are otherwise unrelated
 * concrete types. Carries both the mapping payload and the dialog's modal result, so the Plater drives
 * either dialog through one value with no wrapper: \ref dlg_res == \c wxID_CANCEL means the user
 * cancelled and nothing should be applied.
 */
struct SyncResult
{
    int                       dlg_res         = wxID_CANCEL; ///< ShowModal outcome; wxID_CANCEL == user cancelled
    bool                      direct_sync     = true;        ///< overwrite directly, no per-slot mapping
    bool                      is_same_printer = true;
    std::map<int, AMSMapInfo> sync_maps;                     ///< project filament id -> chosen slot
};

} // namespace Slic3r::GUI
