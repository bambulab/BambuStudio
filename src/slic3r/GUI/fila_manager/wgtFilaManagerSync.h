#ifndef slic3r_wgtFilaManagerSync_h_
#define slic3r_wgtFilaManagerSync_h_

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r {
class MachineObject;
class DevAmsTray;
} // namespace Slic3r

namespace Slic3r { namespace GUI {

class wgtFilaManagerStore;
struct FilamentSpool;

class wgtFilaManagerSync {
public:
    explicit wgtFilaManagerSync(wgtFilaManagerStore* store);
    ~wgtFilaManagerSync() = default;

    bool on_device_update(MachineObject* obj);
    bool sync_all_trays(MachineObject* obj);
    bool on_device_disconnect(const std::string& dev_id, const std::string& dev_name);
    void drain_filament_hints(const std::string& dev_id);
    void dismiss_pending_badge(const std::string& dev_id,
                               const std::string& ams_id,
                               const std::string& slot_id);

private:
    const FilamentSpool* match_tray(const DevAmsTray& tray,
                                    const std::string& dev_id,
                                    const std::string& ams_id);
  
    static bool slot_pin_still_valid(const FilamentSpool& sp,
                                     const DevAmsTray&    tray);

    wgtFilaManagerStore* m_store;
    std::map<std::string, bool> m_prev_tray_exists;
    std::set<std::string> m_auto_added_rfid_uuids;
    std::map<std::string, std::string> m_pending_badges;

    void check_and_register_new_rfid_spools(MachineObject* obj);
    void calibrate_pending_badges(MachineObject* obj);
};

}} // namespace Slic3r::GUI

#endif // slic3r_wgtFilaManagerSync_h_
