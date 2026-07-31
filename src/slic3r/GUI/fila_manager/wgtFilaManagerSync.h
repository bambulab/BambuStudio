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

    // 返回 true 表示在位字段发生变化（调用方据此决定是否刷 UI）。
    bool on_device_update(MachineObject* obj);
    bool sync_all_trays(MachineObject* obj);

    // 机器断连时调用：清空该设备所有 spool 的在位字段。
    // 返回 true 表示有字段被清空（调用方据此决定是否刷 UI）。
    bool on_device_disconnect(const std::string& dev_id, const std::string& dev_name);

    // 把待显示的 RFID 新耗材角标通知逐条发给 StatusPanel，并清空队列。
    // 须在 AMSControl::UpdateAms 完成后（m_ams_item_list 已更新）调用，
    // 否则 find(ams_id) 会因 item list 尚未重建而静默丢弃。
    void drain_filament_hints();

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

    // RFID 耗材自动注册防重发：记录已在 in-flight 中的 UUID，避免同一 MQTT 帧
    // 连续触发多次 sync_ams 请求。断连时在 on_device_disconnect 中清空。
    std::set<std::string> m_auto_added_rfid_uuids;

    // 待补发的角标队列：{ams_id, slot_id}。
    // check_and_register_new_rfid_spools 入队，drain_filament_hints 在
    // AMSControl::UpdateAms 完成后消费，确保 m_ams_item_list 已重建。
    std::vector<std::pair<std::string, std::string>> m_pending_hints;

    // 检测 AMS 中尚未存在于本地 store 的官方 RFID 耗材，向云端发 ams/sync 注册。
    void check_and_register_new_rfid_spools(MachineObject* obj);
};

}} // namespace Slic3r::GUI

#endif // slic3r_wgtFilaManagerSync_h_
