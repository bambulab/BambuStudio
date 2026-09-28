#include "DiscordPresenceSnapshot.hpp"

#include <chrono>

#include "DeviceManager.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "PartPlate.hpp"
#include "Plater.hpp"
#include "DeviceCore/DevManager.h"

#include "libslic3r/Model.hpp"

namespace Slic3r {
namespace GUI {

using Activity = PresenceSnapshot::Activity;

static int64_t unix_now()
{
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

// Elapsed-time origin for states with no more meaningful one of their own.
static int64_t session_start_time()
{
    static const int64_t started = unix_now();
    return started;
}

// A printer keeps reporting FINISH until the next job starts, so terminal
// states have to expire or the profile sticks on "Print complete" for hours.
static const int64_t TERMINAL_STATUS_LINGER_S = 5 * 60;

// GUI thread only.
static int64_t seconds_in_status(const std::string &status_key)
{
    static std::string last_key;
    static int64_t     entered_at = 0;

    const int64_t now = unix_now();
    if (status_key != last_key) {
        last_key   = status_key;
        entered_at = now;
    }
    return now - entered_at;
}

static std::string percent_suffix(int percent)
{
    if (percent < 0)
        return std::string();
    return " - " + std::to_string(percent) + "%";
}

// False when there is no printer worth reporting.
static bool collect_from_printer(PresenceSnapshot &snapshot, bool hide_names)
{
    DeviceManager *dev = wxGetApp().getDeviceManager();
    if (dev == nullptr)
        return false;

    MachineObject *obj = dev->get_selected_machine();
    if (obj == nullptr || !obj->is_connected())
        return false;

    snapshot.small_text = obj->get_printer_type_display_str().ToUTF8().data();

    const std::string job_name = obj->subtask_name;
    snapshot.details = (hide_names || job_name.empty()) ? _u8L("A print job") : job_name;

    // Tracked on every call so a RUNNING -> FINISH transition is seen at once.
    const int64_t status_age = seconds_in_status(obj->print_status + "|" + job_name);

    const int percent = obj->mc_print_percent;

    if (obj->is_in_printing_pause()) {
        snapshot.activity = Activity::Paused;
        snapshot.state    = _u8L("Paused") + percent_suffix(percent);
        return true;
    }

    // is_in_printing() also covers PREPARE and on-device SLICING, which can
    // each last minutes; "Printing - 0%" would be misleading.
    if (obj->print_status == "PREPARE") {
        snapshot.activity = Activity::Printing;
        snapshot.state    = _u8L("Preparing");
        return true;
    }

    if (obj->print_status == "SLICING") {
        snapshot.activity = Activity::Slicing;
        snapshot.state    = _u8L("Slicing on the printer");
        return true;
    }

    if (obj->is_in_printing()) {
        snapshot.activity = Activity::Printing;
        snapshot.state    = _u8L("Printing") + percent_suffix(percent);
        // An absolute end time lets Discord run the countdown itself.
        if (obj->mc_left_time > 0)
            snapshot.end_time = unix_now() + obj->mc_left_time;
        return true;
    }

    if (status_age <= TERMINAL_STATUS_LINGER_S) {
        if (obj->print_status == "FAILED") {
            snapshot.activity = Activity::Failed;
            snapshot.state    = _u8L("Print failed");
            return true;
        }
        if (obj->print_status == "FINISH") {
            snapshot.activity = Activity::Finished;
            snapshot.state    = _u8L("Print complete");
            return true;
        }
    }

    // Connected but idle: local activity is more interesting than "printer is on".
    snapshot = PresenceSnapshot();
    return false;
}

static void collect_from_plater(PresenceSnapshot &snapshot, bool hide_names)
{
    snapshot.start_time = session_start_time();

    Plater *plater = wxGetApp().plater();
    if (plater == nullptr) {
        snapshot.activity = Activity::Idle;
        snapshot.state    = _u8L("Idle");
        return;
    }

    const std::string project = plater->get_project_name().ToUTF8().data();
    const size_t      objects = plater->model().objects.size();

    if (objects == 0) {
        snapshot.activity = Activity::Idle;
        snapshot.state    = _u8L("Idle");
        return;
    }

    snapshot.details = (hide_names || project.empty()) ? _u8L("Working on a project") : project;

    // PartPlate holds the percentage the progress bar is driven from: -1 when
    // unsliced, 0..100 while slicing, 100 once the result is valid.
    PartPlate *plate = plater->get_partplate_list().get_curr_plate();
    if (plate != nullptr && !plate->is_slice_result_valid()) {
        const float percent = plate->get_slicing_percent();
        if (percent >= 0.f && percent < 100.f) {
            snapshot.activity = Activity::Slicing;
            snapshot.state    = _u8L("Slicing") + percent_suffix(static_cast<int>(percent));
            return;
        }
    }

    if (plater->is_preview_shown()) {
        snapshot.activity = Activity::Previewing;
        snapshot.state    = _u8L("Previewing");
        return;
    }

    snapshot.activity = Activity::Editing;
    snapshot.state    = wxString::Format(_L_PLURAL("Editing - %d object", "Editing - %d objects", (int) objects),
                                         (int) objects)
                         .ToUTF8()
                         .data();
}

PresenceSnapshot collect_presence_snapshot(bool hide_names)
{
    PresenceSnapshot snapshot;

    if (collect_from_printer(snapshot, hide_names))
        return snapshot;

    collect_from_plater(snapshot, hide_names);
    return snapshot;
}

} // namespace GUI
} // namespace Slic3r
