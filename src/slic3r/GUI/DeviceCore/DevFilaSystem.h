#pragma once
#include "libslic3r/CommonDefs.hpp"
#include "slic3r/Utils/json_diff.hpp"

#include "DevDefs.h"
#include "DevAmsRfidRefreshCtrl.h"
#include "DevFilaAmsSetting.h"
#include "DevFilaAutoRefillMgr.h"
#include "DevFilaSwitch.h"
#include "DevUtil.h"

#include <map>
#include <optional>
#include <memory>
#include <unordered_set>
#include <set>
#include <vector>

#include <wx/string.h>
#include <wx/colour.h>

#define TRAY_HOLD_COUNT_MAX          5

namespace Slic3r
{
class MachineObject;
class DevFilaSystem;
struct DevNozzle;
struct DevFilamentDryingPreset;

class DevAmsTray
{
public:
    enum class RemainFetchStatus : int
    {
        Done         = 0,
        Refreshing   = 1,
        CloudTimeout = 2,
        CloudNoData  = 3,
        Initializing = 4,
    };

    enum class PrintStatus : int
    {
        Idle         = 0,
        Using        = 1,
        RefillBackup = 2,
    };

    DevAmsTray() = default;
    DevAmsTray(const std::string& ams_id, const std::string& slot_id)
    {
        is_bbl = false;
        id = slot_id;
        this->ams_id = ams_id;
    }

    std::string              id;
    std::string              ams_id; // same as id if it's EXT_SPOOL
    DevAmsType               ams_type = DevAmsType::EXT_SPOOL;
    std::string              tag_uid;             // tag_uid
    std::string              setting_id;          // tray_info_idx
    std::string              filament_setting_id; // setting_id
    std::string              m_fila_type;
    std::string              sub_brands;
    std::string              color;
    std::vector<std::string> cols;
    std::string              weight;
    std::string              diameter;
    std::string              temp;
    std::string              time;
    std::string              bed_temp_type;
    std::string              bed_temp;
    std::string              nozzle_temp_max;
    std::string              nozzle_temp_min;
    std::string              xcam_info;
    std::string              uuid;
    std::string              tray_id_name;
    DevFilaColorType         ctype = DevFilaColorType::CTYPE_SINGLE;
    float                    k        = 0.0f; // k range: 0 ~ 0.5
    float                    n        = 0.0f; // k range: 0.6 ~ 2.0
    int                      cali_idx             = -1;  // - 1 means default
    RemainFetchStatus        remain_fetch_status  = RemainFetchStatus::Done; // bits[5-7] of MQTT "state" field
    std::optional<PrintStatus> print_status; // nullopt: not reported by old firmware

    wxColour        wx_color;
    bool            is_bbl;
    bool            is_exists = false; // for external spool, default to true
    int             hold_count = 0;
    int             remain = 0;         // filament remain: 0 ~ 100
    int             remain_g = -1;      // accurate remaining weight in grams; -1 means not edited / not provided by firmware

    std::optional<int>                      current_extruder_id = std::nullopt;// the ams is used on the extruder currently
    std::set<int>                           binded_extruder_set;// the ams can be used on the binded extruders
    std::optional<DevFilaSwitch::SwitchPos> binded_switcher_pos = std::nullopt;
    std::shared_ptr<DevCtrlToken>           m_rfid_refresh_token;

public:
    // operators
    bool operator==(DevAmsTray const& o) const
    {
        return id == o.id && m_fila_type == o.m_fila_type && setting_id == o.setting_id &&
               filament_setting_id == o.filament_setting_id && color == o.color &&
               current_extruder_id == o.current_extruder_id && binded_extruder_set == o.binded_extruder_set && binded_switcher_pos == o.binded_switcher_pos;
    }
    bool operator!=(DevAmsTray const& o) const { return !operator==(o); }

    // setters
    void reset();
    void UpdateColorFromStr(const std::string& color);
    void set_hold_count() { hold_count = TRAY_HOLD_COUNT_MAX; }

    // getter
    DevAmsSlotId get_ams_slot_id() const;

    // Bit index of this tray inside the MachineObject tray_xxx_bits masks. -1 when the
    // ams type has no slot in those masks.
    long long get_tray_flag_bit() const;

    // status
    bool is_tray_info_ready() const;
    bool is_reading(MachineObject* obj) const;
    bool is_reading_done(MachineObject* obj) const;

    bool is_unset_third_filament() const;

    wxColour    get_color()  const { return decode_color(color); };

    std::string get_filament_id() const { return setting_id; }
    std::string get_slot_name(MachineObject* obj) const;
    std::string get_display_filament_type() const;
    std::string get_filament_type();

    std::optional<int> get_filament_remain_weight() const;

    // extruder nozzle
    std::optional<int> get_nozzle_pos_id(MachineObject* obj) const;
    std::optional<int> get_nozzle_addr_id(MachineObject* obj) const;
    std::optional<DevNozzle> get_nozzle(MachineObject* obj) const;

    // ability
    bool WaitReadExternalRFID_TPU(MachineObject* obj) const;
    bool CanReadExternalRFID_TPU(MachineObject* obj) const;

    bool CanResetRFID_FilamentInfo(MachineObject* obj) const;

    // static
    static wxColour decode_color(const std::string& color);

    static double get_fila_remain_tolerance() { return 0.05; } //+- 5%

    std::optional<DevFilamentDryingPreset> get_ams_drying_preset() const;
};

class DevAms
{
    friend class DevFilaSystemParser;
public:

    enum class DryCtrlMode : int
    {
        Off = 0,
        OnTime = 1,
        OnHumidity = 2,
    };

    enum class DryStatus : char
    {
        Off = 0,
        Checking = 1,
        Drying = 2,
        Cooling = 3,
        Stopping = 4,
        Error = 5,
        CannotStopHeatOutofControl = 6,
        PrdTesting = 7,
    };

    enum class DrySubStatus
    {
        Off = 0,
        Heating = 1,
        Dehumidify = 2,
    };

    enum class DryFanStatus : char
    {
        Off = 0,
        On = 1,
    };

    enum class RemainEstimateVersion : int
    {
        Legacy   = 0,   // Legacy: coarse estimate.
        Accurate = 1,   // Accurate: O1D U4 precise estimate.
    };

    enum class CannotDryReason : int
    {
        TaskOccupied = 0,
        InsufficientPower = 1,
        AmsBusy = 2,
        ConsumableAtAmsOutlet = 3,
        InitiatingAmsDrying = 4,
        NotSupportedIn2dMode = 5,
        DryingInProgress = 6,
        Upgrading = 7,
        InsufficientPowerNeedPluginPower = 8,
        ClosingAmsDrying = 9,
        FilamentAtAmsOutletManualUnload = 10,
    };

    struct DrySettings
    {
        std::string dry_filament;
        int dry_temp = -1; // -1 means invalid
        int dry_hour = -1; // -1 means invalid, hours
    };

    struct DevAmsZone
    {
        int zone_id = -1;
        std::set<int> zone_slots;

        float current_temperature = INVALID_AMS_TEMPERATURE;
        int humidity_level = -1; // 0-5, larger means drier
        int humidity_percent = -1; // 0-100, -1 means invalid

        int left_dry_time = 0; // minutes
        std::optional<DryStatus> dry_status;
        std::optional<DrySubStatus> dry_sub_status;
        std::optional<DryFanStatus> dry_fan1_status;
        std::optional<DryFanStatus> dry_fan2_status;
        std::optional<std::vector<CannotDryReason>> dry_cannot_reasons;
        std::optional<DrySettings> dry_settings;
        std::string info;

        int   GetHumidityPercent() const { return humidity_percent; }
        bool  SupportDrying() const { return zone_slots.size() == 2; };
        std::optional<DryStatus> GetDryStatus() const { return dry_status; };
    };

public:
    DevAms(std::shared_ptr<DevFilaSystem> owner, const std::string& ams_id, const std::set<int>& binded_extruder_set, DevAmsType type);
    ~DevAms();

public:
    int GetAmdIdInt() const;
    std::string GetAmsId() const { return m_ams_id; }
    wxString    GetDisplayName() const; // display

    void       SetAmsType(int type) { m_ams_type = (DevAmsType) type; }
    void       SetAmsType(DevAmsType type) { m_ams_type = type; }
    DevAmsType GetAmsType() const { return m_ams_type == DevAmsType::AMS_LITE_MIXED ? DevAmsType::AMS_LITE : m_ams_type; }

    // exist or not
    bool  IsExist() const;

    // tray id
    int  GetTrayId(int slot_id) const; //same as tray_exist_bits

    // Feed-hall presence for a given slot, read straight from tray_exist_bits.
    // Reliable even when no tray object exists for an empty AMS bay.
    bool IsAmsSlotPreLoadFilament(int slot_id) const;

    // mixed ams lite for N9
    bool IsAmsLiteMixed() const { return m_ams_type == DevAmsType::AMS_LITE_MIXED; }

    // slots
    int   GetSlotCount() const;
    DevAmsTray* GetTray(const std::string& tray_id) const;
    const std::map<std::string, DevAmsTray*>& GetTrays() const { return m_trays; }

    // the ams can be used on the binded extruders
    int GetBindedExtruderCount() const { return m_binded_extruder_set.size(); }
    std::optional<int> GetUniqueBindedExtruderId() const;
    std::set<int> GetBindedExtruderSet() const { return m_binded_extruder_set; };

    // the ams is used on the extruder currently
    std::optional<int> GetCurrentExtruderId() const;

    // filament switcher
    std::optional<DevFilaSwitch::SwitchPos> GetSwitcherPos() const { return m_binded_switcher_pos; };

    // temperature and humidity
    float GetCurrentTemperature() const { return m_current_temperature; }

    // humidity ans drytime
    bool  SupportHumidityLevel() const { return m_ams_type == DevAmsType::AMS; }
    int   GetHumidityLevel() const { return m_humidity_level; }

    bool  SupportHumidityPercent() const;
    int   GetHumidityPercent() const { return m_humidity_percent; }

    bool  SupportDrying() const;
    int   GetLeftDryTime() const { return m_left_dry_time; } //miniutes

    // remote drying control
    bool IsSupportRemoteDry(const MachineObject* obj) const;
    bool IsDryableSlot(int slot_id) const;

    std::vector<int> GetDryableZoneIds() const;
    std::optional<DryStatus> GetDryStatus() const { return m_dry_status; };
    std::optional<DrySubStatus> GetDrySubStatus() const { return m_dry_sub_status; }
    std::optional<DryFanStatus> GetFan1Status() const { return m_dry_fan1_status; }
    std::optional<DryFanStatus> GetFan2Status() const { return m_dry_fan2_status; }
    std::optional<std::vector<CannotDryReason>> GetCannotDryReason() const { return m_dry_cannot_reasons; }
    std::optional<DrySettings> GetDrySettings() const { return m_dry_settings; };

    std::map<int, DevAmsZone> GetZones() const { return m_zones; }
    std::optional<DevAmsZone> GetZone(int zone_id) const;
    std::optional<DevAmsZone> GetZoneBySlotId(int slot_id) const;

    bool AmsIsDrying();

    RemainEstimateVersion GetRemainEstimateVersion() const { return m_remain_estimate_version; }
    bool SupportsFilamentTabAutoReadRfid() const { return m_enable_filament_tab_auto_read_rfid; }
    bool SupportsReadExtRfid() const { return m_support_read_ext_rfid; }

private:
    std::weak_ptr<DevFilaSystem> m_fila_system;

    DevAmsType    m_ams_type = DevAmsType::AMS;
    std::string   m_ams_id;

    // bind extruder id set
    // normally, one ams binds to one extruder
    // for filament switcher, one ams may bind to multiple extruders
    std::set<int> m_binded_extruder_set;

    std::optional<DevFilaSwitch::SwitchPos> m_binded_switcher_pos;

    // slots and trays
    std::map<std::string, DevAmsTray*> m_trays;//id -> DevAmsTray*

    // temperature and humidity
    float  m_current_temperature = INVALID_AMS_TEMPERATURE; // the temperature
    int    m_humidity_level = 5; // AmsType::AMS
    int    m_humidity_percent = -1; // N3F N3S, the percentage, -1 means invalid. eg. 100 means 100%

    // dry control
    int    m_left_dry_time = 0; // min

    // see is_support_remote_dry
    public: std::optional<DryStatus> m_dry_status;
    public: std::optional<DrySubStatus> m_dry_sub_status;
    std::optional<DryFanStatus> m_dry_fan1_status;
    std::optional<DryFanStatus> m_dry_fan2_status;
    std::optional<std::vector<CannotDryReason>> m_dry_cannot_reasons;
    std::optional<DrySettings> m_dry_settings;

    std::map<int, DevAmsZone> m_zones; // zone_id -> zones
    
    RemainEstimateVersion m_remain_estimate_version = RemainEstimateVersion::Legacy;
    bool m_enable_filament_tab_auto_read_rfid = false;
    bool m_support_read_ext_rfid = false;
};

using DevAmsZone = DevAms::DevAmsZone;

class DevFilaSystem
{
    friend class DevFilaSystemParser;
public:
    DevFilaSystem(MachineObject* owner) { m_owner = owner;};
    ~DevFilaSystem();

public:
    MachineObject* GetOwner() const { return m_owner; }
    void reset();

    bool        HasAms() const { return !amsList.empty(); }
    bool        IsAmsSettingUp() const;

    /* ams task*/
    DevAmsTaskRunStatus GetAmsTaskRunStatus() const;

    /* ams */
    DevAms*                         GetAmsById(const std::string& ams_id) const;
    std::map<std::string, DevAms*, NumericStrCompare>& GetAmsList() { return amsList; }
    int                             GetAmsCount() const { return amsList.size(); }

    // Whether an externally attached sub-AMS is bound to any nozzle.
    bool                            HasSubAms() const;

    /* zone */
    std::optional<DevAmsZone> GetZoneBySlot(int ams_id, int slot_id) const;

    /* all tray */
    std::optional<DevAmsTray> GetTray(int ams_id, int slot_id) const;

    /* ams tray*/
    DevAmsTray* GetAmsTray(const std::string& ams_id, const std::string& tray_id) const;
    void        CollectAmsColors(std::vector<wxColour>& ams_colors) const;

    /* external trays*/
    std::optional<DevAmsTray> GetVirtualTray(int virtual_tray_id) const;
    std::optional<DevAmsTray> GetVirtualTray(const std::string& virtual_tray_id) const;
    DevAmsTray* GetVirtualTrayPtr(const std::string& virtual_tray_id);
    bool UpdateVirtualTray(const std::string& virtual_tray_id, const DevAmsTray& tray);
    const std::map<std::string, DevAmsTray>& GetVirtualTrays() const { return m_vt_trays; }
    bool IsVirtualTrayId(const std::string& tray_id) const { return m_vt_trays.find(tray_id) != m_vt_trays.end(); }

    std::map<int, DevAmsSlotId> GetTrayIndexMap();
    int GetTrayIdByAmsSlotId(int ams_id, int slot_id);
    std::string GetTrayNameByTrayId(int tray_id);
    static std::string GetTrayNameByAmsSlotId(const std::string& dev_id, int ams_id, int slot_id, DevAmsType ams_type);

    // extruder
    int  GetExtruderIdByAmsId(const std::string& ams_id) const;

    // nozzle
    std::string GetNozzleFlowStringByAmsId(const std::string& ams_id) const;
    std::map<DevAmsSlotId, int> GetSlot2HotendDisplayIdx() const;

    // filament backup
    bool CanShowFilamentBackup() const;

    // tray print_status granularity; old firmware without it falls back to is_in_printing()
    bool IsDryAreaInPrintUse(const std::string& ams_id, std::optional<int> zone_id) const;

    // filament change steps
    std::optional<DevFilamentStep> GetCurrentFilamentChangeStep() const;
    std::vector<DevFilamentStep> GetCurrentFilamentChangeSteps() const;// some firmwares support this, IsDirectConnectedRack

    bool IsFilamentChangingSlot(int ams_id, int slot_id) const;
    bool IsCurrentFilamentChangingSlot(int ams_id, int slot_id) const;
    std::optional<DevAmsSlotId> GetCurrentChangingSlot() const;
    std::vector<DevAmsSlotId> GetFilamentChangingSlots() const;// some firmwares support this, IsDirectConnectedRack
    std::vector<int> GetEncodedChangingSlots() const { return m_encoded_filament_changing_slots;}

    /* AMS settings*/
    DevAmsSystemSetting& GetAmsSystemSetting() { return m_ams_system_setting; }
    std::optional<bool>  IsDetectOnInsertEnabled() const { return m_ams_system_setting.IsDetectOnInsertEnabled(); };
    bool                 IsDetectOnPowerupEnabled() const { return m_ams_system_setting.IsDetectOnPowerupEnabled(); }
    bool                 IsDetectRemainEnabled() const { return m_ams_system_setting.IsDetectRemainEnabled(); }
    bool                 IsAutoRefillEnabled() const { return m_auto_refill_mgr.IsEnabled(); }

    DevFilaAutoRefillMgr&       GetAutoRefillMgr() { return m_auto_refill_mgr; }
    const DevFilaAutoRefillMgr& GetAutoRefillMgr() const { return m_auto_refill_mgr; }

    std::weak_ptr<DevAmsSystemFirmwareSwitch> GetAmsFirmwareSwitch() const { return m_ams_firmware_switch;}
    std::shared_ptr<DevAmsRfidRefreshCtrl> GetAmsRfidRefreshCtrl() const { return m_ams_rfid_refresh_ctrl; }

public:
    // ctrls
    int  CtrlAmsReset() const;
    int  CtrlSetFilamentWeight(int ams_id, int slot_id, int total, int remain);
    void CtrlSetFilamentWeightAfter(const std::shared_ptr<DevCtrlToken>& token, int ams_id, int slot_id, int total, int remain);

    // crtl
    int  CtrlAmsStartDryingHour(int ams_id, int zone_id, std::string filament_type, int tag_temp, int tag_duration_hour, bool rotate_tray, int cooling_temp, bool close_power_conflict = false) const;
    int  CtrlAmsStopDrying(int ams_id) const;

    // Parse
    void ParseVirtualTrayInfos(const nlohmann::json& j_print);


public:
    static bool IsBBL_Filament(std::string tag_uid);

private:
    std::optional<DevAmsSlotId> DecodeFilamentChangingSlot(int encoded_slot) const;

private:
    MachineObject* m_owner;

    /* ams properties */
    int  m_ams_cali_stat = 0;
    int  m_ams_stat = 0;

    /* filament change step*/
    std::vector<DevFilamentStep> m_filament_change_steps; // some firmwares support this
    std::vector<std::vector<DevFilamentStep>> m_filament_change_steps_multi; // some firmwares support this
    std::vector<int> m_encoded_filament_changing_slots; // some firmwares support this, store raw firmware values

    std::map<std::string, DevAms*, NumericStrCompare> amsList;// key: ams[id], start with 0
    std::map<std::string, DevAmsTray> m_vt_trays; // virutal tray info

    DevAmsSystemSetting m_ams_system_setting{ this };
    DevFilaAutoRefillMgr m_auto_refill_mgr{ this };
    std::shared_ptr<DevAmsSystemFirmwareSwitch> m_ams_firmware_switch = DevAmsSystemFirmwareSwitch::Create(this);
    std::shared_ptr<DevAmsRfidRefreshCtrl> m_ams_rfid_refresh_ctrl = DevAmsRfidRefreshCtrl::Create(this);

    // Tokens whose deferred weight command has not run yet. DevCtrlTokenMgr drops tokens
    // nobody else references, so the pending ones have to be kept alive here.
    std::vector<std::shared_ptr<DevCtrlToken>> m_pending_weight_tokens;
};// class DevFilaSystem


class DevFilaSystemParser
{
public:
    static void ParseV1_0(const json& print_json, MachineObject* obj, DevFilaSystem* system, bool key_field_only);

private:
    static DevAms*     ParseAmsInfo(const json& j_ams, MachineObject* obj, DevFilaSystem* system);
    static DevAmsTray* ParseAmsTrayInfo(const json& j_tray, MachineObject* obj, DevAms* curr_ams);
};

struct DevFilamentDryingPreset
{
    std::string filament_id;

    std::unordered_set<DevAmsType> ams_limitations; // only use ams types in the set
    std::unordered_map<DevAmsType, float> filament_dev_ams_drying_time_on_idle; // hour
    std::unordered_map<DevAmsType, float> filament_dev_ams_drying_temperature_on_idle;
    std::unordered_map<DevAmsType, float> filament_dev_ams_drying_time_on_print; // hour
    std::unordered_map<DevAmsType, float> filament_dev_ams_drying_temperature_on_print;
    float filament_dev_drying_cooling_temperature;
    std::unordered_map<DevAmsType, float> filament_dev_drying_softening_temperature;
    std::unordered_map<DevAmsType, float> filament_dev_ams_drying_heat_distortion_temperature;
};

}// namespace Slic3r