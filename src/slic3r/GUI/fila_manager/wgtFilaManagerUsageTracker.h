#ifndef slic3r_wgtFilaManagerUsageTracker_h_
#define slic3r_wgtFilaManagerUsageTracker_h_

#include <chrono>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "nlohmann/json.hpp"

namespace Slic3r {
class MachineObject;
} // namespace Slic3r

namespace Slic3r { namespace GUI {

// Estimates filament consumption for spools whose remaining amount the printer
// cannot report (non-RFID / third-party spools; the printer sends remain = -1
// for those, so wgtFilaManagerSync never updates them).
//
// Source of truth is the account's cloud print history (get_user_tasks): every
// finished task carries amsDetailMapping[].weight — sliced grams per AMS slot —
// no matter whether the job was sent from this Studio, another computer or
// Bambu Handy. Each finished (status 2) or failed (status 3) task newer than the
// device's baseline is deducted once from the Filament Manager spool that is
// in the slot it used; failed tasks are prorated by elapsed / estimated time.
//
// Deduplication is local (<data_dir>/filament_inventory/usage_tracker.json):
// the first time a device is seen, its baseline is set to "now" so older
// history is never deducted from weights the user has already entered.
//
// All public methods run on the UI thread. Cloud fetches run on a worker
// thread and hand results back through CallAfter.
class wgtFilaManagerUsageTracker {
public:
    wgtFilaManagerUsageTracker();
    ~wgtFilaManagerUsageTracker();

    void on_device_update(MachineObject* obj);

private:
    struct DeviceState {
        std::string            baseline;  // ISO-8601 UTC; tasks ending at/before it are ignored
        std::vector<std::string> processed; // task ids already handled, oldest first

        // session-only
        std::chrono::steady_clock::time_point next_check{};
        bool                                  fetching = false;
        std::string                           prev_print_status;
    };

    struct SlotUsage {
        int    ams_id  = -1;
        int    slot_id = -1;
        double grams   = 0;
        std::string filament_id;
        std::string color;
    };

    void start_fetch(const std::string& dev_id);
    void on_tasks_fetched(const std::string& dev_id, bool ok, const std::vector<nlohmann::json>& hits);
    bool is_processed(const DeviceState& st, const std::string& task_id) const;
    void mark_processed(DeviceState& st, const std::string& task_id);
    void add_history(nlohmann::json entry);

    void load();
    void save() const;

    std::map<std::string, DeviceState> m_devices;
    nlohmann::json                     m_history = nlohmann::json::array();

    // Deduction pushes waiting for the cloud to acknowledge. No new pass starts
    // while any are pending, so the next pass never computes from a stale
    // net_weight. Cleared after a timeout in case a push fails silently.
    int                                   m_inflight_pushes = 0;
    std::chrono::steady_clock::time_point m_inflight_since{};

    // Guards CallAfter callbacks against running after destruction.
    std::shared_ptr<int> m_alive = std::make_shared<int>(0);
};

}} // namespace Slic3r::GUI

#endif // slic3r_wgtFilaManagerUsageTracker_h_
