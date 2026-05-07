#ifndef slic3r_wgtFilaManagerSync_h_
#define slic3r_wgtFilaManagerSync_h_

#include <string>

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

    void on_device_update(MachineObject* obj);
    void sync_all_trays(MachineObject* obj);

private:
    const FilamentSpool* match_tray(const DevAmsTray& tray);

    wgtFilaManagerStore* m_store;
};

}} // namespace Slic3r::GUI

#endif // slic3r_wgtFilaManagerSync_h_
