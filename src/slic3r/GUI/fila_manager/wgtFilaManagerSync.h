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

    // 返回 true 表示在位字段发生变化（调用方据此决定是否刷 UI）。
    bool on_device_update(MachineObject* obj);
    bool sync_all_trays(MachineObject* obj);

    // 机器断连时调用：清空该设备所有 spool 的在位字段。
    // 返回 true 表示有字段被清空（调用方据此决定是否刷 UI）。
    bool on_device_disconnect(const std::string& dev_id, const std::string& dev_name);

private:
    const FilamentSpool* match_tray(const DevAmsTray& tray);

    wgtFilaManagerStore* m_store;
};

}} // namespace Slic3r::GUI

#endif // slic3r_wgtFilaManagerSync_h_
