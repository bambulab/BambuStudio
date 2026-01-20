#pragma once
#include "libslic3r/CommonDefs.hpp"
#include "slic3r/Utils/json_diff.hpp"

#include "DevDefs.h"
#include "DevFilaAmsSetting.h"
#include "DevUtil.h"

#include <map>
#include <optional>
#include <memory>
#include <unordered_set>

#include <wx/string.h>
#include <wx/colour.h>

#define HOLD_COUNT_MAX          3

namespace Slic3r
{
class MachineObject;
struct DevFilamentDryingPreset;

enum DevFilaColorType : int
{
    CTYPE_MULTI = 0,
    CTYPE_GRADIANT = 1,
    CTYPE_SINGLE = 2,
};

class DevAmsTray
{
public:
    DevAmsTray(std::string tray_id)
    {
        is_bbl = false;
        id = tray_id;
    }

    std::string              id;
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
    DevFilaColorType         ctype = DevFilaColorType::CTYPE_SINGLE;
    float                    k        = 0.0f; // k range: 0 ~ 0.5
    float                    n        = 0.0f; // k range: 0.6 ~ 2.0
    int                      cali_idx = -1;   // - 1 means default

    wxColour        wx_color;
    bool            is_bbl;
    bool            is_exists = false;
    int             hold_count = 0;
    int             remain = 0;         // filament remain: 0 ~ 100

public:
    // operators
    bool operator==(DevAmsTray const& o) const
    {
        return id == o.id && m_fila_type == o.m_fila_type && filament_setting_id == o.filament_setting_id && color == o.color;
    }
    bool operator!=(DevAmsTray const& o) const { return !operator==(o); }

    // setters
    void reset();
    void UpdateColorFromStr(const std::string& color);
    void set_hold_count() { hold_count = HOLD_COUNT_MAX; }

    // getter
    bool is_tray_info_ready() const;
    bool is_unset_third_filament() const;

    wxColour    get_color()  const { return decode_color(color); };

    std::string get_filament_id() const { return setting_id; }
    std::string get_display_filament_type() const;
    std::string get_filament_type();

    // static
    static wxColour decode_color(const std::string& color);

    std::optional<DevFilamentDryingPreset> get_ams_drying_preset() const;
};

class DevAms
{
    friend class DevFilaSystemParser;
public:
    enum AmsType : int
    {
        DUMMY = 0,
        AMS = 1,      // AMS
        AMS_LITE = 2, // AMS-Lite
        N3F = 3,      // N3F
        N3S = 4,      // N3S
    };

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
        InsufficientPowerNeedPluginPower = 8
    };

    struct DrySettings
    {
        std::string dry_filament;
        int dry_temp = -1; // -1 means invalid
        int dry_hour = -1; // -1 means invalid, hours
    };

public:
    DevAms(const std::string& ams_id, int extruder_id, AmsType type);
    DevAms(const std::string& ams_id, int nozzle_id, int type);
    ~DevAms();

public:
    std::string GetAmsId() const { return m_ams_id; }
    wxString    GetDisplayName() const; // display

    void     SetAmsType(int type) { m_ams_type = (AmsType)type; }
    void     SetAmsType(AmsType type) { m_ams_type = type; }
    AmsType  GetAmsType() const { return m_ams_type; }

    // exist or not
    bool  IsExist() const { return m_exist; }

    // slots
    int   GetSlotCount() const;
    DevAmsTray* GetTray(const std::string& tray_id) const;
    const std::map<std::string, DevAmsTray*>& GetTrays() const { return m_trays; }

    // installed on the extruder
    int   GetExtruderId() const { return m_ext_id; }

    // temperature and humidity
    float GetCurrentTemperature() const { return m_current_temperature; }

    // humidity ans drytime
    bool  SupportHumidityLevel() const { return m_ams_type == AMS; }
    int   GetHumidityLevel() const { return m_humidity_level; }

    bool  SupportHumidityPercent() const { return (m_ams_type == N3F) || (m_ams_type == N3S); }
    int   GetHumidityPercent() const { return m_humidity_percent; }

    bool  SupportDrying() const { return m_ams_type == N3F || m_ams_type == N3S; }
    int   GetLeftDryTime() const { return m_left_dry_time; } //miniutes

    // remote drying control
    bool IsSupportRemoteDry(const MachineObject* obj) const;
    std::optional<DryStatus> GetDryStatus() const { return m_dry_status; };
    std::optional<DrySubStatus> GetDrySubStatus() const { return m_dry_sub_status; }
    std::optional<DryFanStatus> GetFan1Status() const { return m_dry_fan1_status; }
    std::optional<DryFanStatus> GetFan2Status() const { return m_dry_fan2_status; }
    std::optional<std::vector<CannotDryReason>> GetCannotDryReason() const { return m_dry_cannot_reasons; }
    std::optional<DrySettings> GetDrySettings() const { return m_dry_settings; };

    bool AmsIsDrying();

private:
    AmsType       m_ams_type = AmsType::AMS;
    std::string   m_ams_id;
    int           m_ext_id;//extruder id
    bool          m_exist = false;

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
};

class DevFilaSystem
{
    friend class DevFilaSystemParser;
public:
    DevFilaSystem(MachineObject* owner) { m_owner = owner;};
    ~DevFilaSystem();

public:
    MachineObject* GetOwner() const { return m_owner; }

    bool        HasAms() const { return !amsList.empty(); }
    bool        IsAmsSettingUp() const;

    /* ams */
    DevAms*                         GetAmsById(const std::string& ams_id) const;
    std::map<std::string, DevAms*, NumericStrCompare>& GetAmsList() { return amsList; }
    int                             GetAmsCount() const { return amsList.size(); }

    /* tray*/
    DevAmsTray* GetAmsTray(const std::string& ams_id, const std::string& tray_id) const;
    void        CollectAmsColors(std::vector<wxColour>& ams_colors) const;

    // extruder
    int  GetExtruderIdByAmsId(const std::string& ams_id) const;

    // nozzle
    std::string GetNozzleFlowStringByAmsId(const std::string& ams_id) const;

    // filament backup
    bool CanShowFilamentBackup() const;

    /* AMS settings*/
    DevAmsSystemSetting& GetAmsSystemSetting() { return m_ams_system_setting; }
    std::optional<bool>  IsDetectOnInsertEnabled() const { return m_ams_system_setting.IsDetectOnInsertEnabled(); };
    bool                 IsDetectOnPowerupEnabled() const { return m_ams_system_setting.IsDetectOnPowerupEnabled(); }
    bool                 IsDetectRemainEnabled() const { return m_ams_system_setting.IsDetectRemainEnabled(); }
    bool                 IsAutoRefillEnabled() const { return m_ams_system_setting.IsAutoRefillEnabled(); }

    std::weak_ptr<DevAmsSystemFirmwareSwitch> GetAmsFirmwareSwitch() const { return m_ams_firmware_switch;}

public:
    // ctrls
    int  CtrlAmsReset() const;

    // crtl
    int  CtrlAmsStartDryingHour(int ams_id, std::string filament_type, int tag_temp, int tag_duration_hour, bool rotate_tray, int cooling_temp, bool close_power_conflict = false) const;
    int  CtrlAmsStopDrying(int ams_id) const;


public:
    static bool IsBBL_Filament(std::string tag_uid);

private:
    MachineObject* m_owner;

    /* ams properties */
    int  m_ams_cali_stat = 0;

    std::map<std::string, DevAms*, NumericStrCompare> amsList;// key: ams[id], start with 0

    DevAmsSystemSetting m_ams_system_setting{ this };
    std::shared_ptr<DevAmsSystemFirmwareSwitch> m_ams_firmware_switch = DevAmsSystemFirmwareSwitch::Create(this);
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

    std::unordered_set<DevAms::AmsType> ams_limitations; // only use ams types in the set
    std::unordered_map<DevAms::AmsType, float> filament_dev_ams_drying_time_on_idle; // hour
    std::unordered_map<DevAms::AmsType, float> filament_dev_ams_drying_temperature_on_idle;
    std::unordered_map<DevAms::AmsType, float> filament_dev_ams_drying_time_on_print; // hour
    std::unordered_map<DevAms::AmsType, float> filament_dev_ams_drying_temperature_on_print;
    float filament_dev_drying_cooling_temperature;
    float filament_dev_drying_softening_temperature;
    float filament_dev_ams_drying_heat_distortion_temperature;
};

}// namespace Slic3r