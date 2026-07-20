#ifndef slic3r_wgtFilaManagerSync_h_
#define slic3r_wgtFilaManagerSync_h_

#include <map>
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
    // 匹配一条 AMS tray 到 store 中的 spool。优先复用用户已手动绑定过的
    // 槽位锚（in_printer + dev_id/ams_id/slot_id 三者匹配的 spool），命中
    // 后仍需 slot_pin_still_valid 校验；未命中或校验失败再回退到 uuid
    // 精确 / setting+color 唯一模糊匹配。
    const FilamentSpool* match_tray(const DevAmsTray& tray,
                                    const std::string& dev_id,
                                    const std::string& ams_id);

    // 判断挂在 sp 上的槽位锚是否还有效——用户改颜色 / 品牌 / 类型 / 系列
    // 后视为不同物理卷，锚立即失效；槽位拔出的失效由 apply_mount_diff 独立
    // 处理，不在这里判定。
    static bool slot_pin_still_valid(const FilamentSpool& sp,
                                     const DevAmsTray&    tray);

    wgtFilaManagerStore* m_store;

    // 追踪每个 AMS 槽位上一轮的 is_exists 状态，用于检测"拔出后重新插入"跳变。
    // key 格式：dev_id + ":" + ams_id + ":" + tray.id
    // 用途：可编辑槽（无官方 RFID）重新插入时，固件会回放旧的 tray_info_idx/
    // tray_type/tray_color，需主动发 ams_filament_setting 清空使槽位回到 "?"。
    std::map<std::string, bool> m_prev_tray_exists;
};

}} // namespace Slic3r::GUI

#endif // slic3r_wgtFilaManagerSync_h_
