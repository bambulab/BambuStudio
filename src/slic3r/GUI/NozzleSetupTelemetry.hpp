#pragma once

/**
 * \file NozzleSetupTelemetry.hpp
 * \brief Nozzle setup usage telemetry for printers with the multi-diameter mode: which diameter mode
 *        and nozzle diameters the user slices with, and what they edited in the nozzle settings.
 */

#include <utility>

#include "nlohmann/json_fwd.hpp"

namespace Slic3r {
class PresetBundle;
struct NozzleSlot;
}

namespace Slic3r::GUI {

struct NozzleConfig;

/** \brief Edit operation counts, one field per counter in the reported payload. */
struct NozzleSetupEdits
{
    int nozzle_diameter_ops{0};
    int nozzle_volume_ops{0};
    int ams4_add_ops{0};
    int ams4_remove_ops{0};
    int ams1_add_ops{0};
    int ams1_remove_ops{0};
};

/**
 * \brief Counts the nozzle and AMS edits of one edit session.
 *
 * Nothing is reported until commit(), so a cancelled session counts nothing. A view that applies each
 * edit at once commits after each edit.
 * AMS counts are totals over all extruders. The counts at construction are the first baseline.
 */
class NozzleSetupEditTally
{
public:
    NozzleSetupEditTally();

    /**
     * \brief Count one nozzle pick.
     *
     * A pick that changes both diameter and flow type counts once for each. A flow type change
     * counts only when the slot holds a nozzle both before and after the pick.
     */
    void record_nozzle_pick(const NozzleConfig &from, const NozzleConfig &to);
    void record_nozzle_pick(const NozzleSlot &from, const NozzleSlot &to);

    /** \brief Count the AMS additions and removals since the previous observation or baseline. */
    void observe_ams_counts();

    /**
     * \brief Take the current AMS counts as the baseline, without counting the change.
     *
     * Call it after a change that is not a user edit, such as a printer sync.
     */
    void rebaseline_ams_counts();

    /** \brief Queue the counted edits for the next report and start again from zero. */
    void commit();

private:
    NozzleSetupEdits    m_edits;
    std::pair<int, int> m_ams_counts; ///< {4-slot, 1-slot} totals at the last observation
};

/**
 * \brief Build the "nozzle_setup" payload for the slice event.
 *
 * The payload carries the diameter mode, the diameters of the nozzles that print, and the edits
 * confirmed and not yet reported. Building it changes nothing: the edits stay queued until
 * on_nozzle_setup_telemetry_sent() gets this payload.
 * Call it on the UI thread only.
 *
 * \return a null json when the selected printer is not a Bambu printer with the multi-diameter mode;
 *         the caller then leaves the field out
 */
nlohmann::json nozzle_setup_telemetry(PresetBundle &preset_bundle);

/**
 * \brief Remove the edits in a sent payload from the queue.
 *
 * Call it only after the event is really sent. Edits confirmed after the payload was built stay queued.
 * Call it on the UI thread only.
 *
 * \param sent_payload    the payload from nozzle_setup_telemetry(); a null json changes nothing
 */
void on_nozzle_setup_telemetry_sent(const nlohmann::json &sent_payload);

} // namespace Slic3r::GUI
