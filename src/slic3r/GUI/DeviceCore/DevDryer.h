#pragma once
#include "DevFilaSystem.h"

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

// A dryer is built into an AMS (N3F, N3S, ZMULTI_A) or a standalone module (eg. ht)

namespace Slic3r
{

// Callers map it to their own display text
enum class DevDryerType : int
{
    Unknown = 0,
    N3F,       // built into an AMS 2 Pro
    N3S,       // built into an AMS HT
    ZMULTI_A,  // built into the rack-A AMS
    HT_MODULE,
};

struct DevDryerModuleState
{
    float temperature = INVALID_AMS_TEMPERATURE;
    float target_temperature = INVALID_AMS_TEMPERATURE;
    int   humidity_level = -1;   // 1-5, larger means drier
    int   humidity_percent = -1;
    int   total_time = 0;        // minutes
    int   left_time = 0;         // minutes
    std::optional<DevAms::DryStatus> dry_status;
    std::optional<DevAms::DrySubStatus> dry_sub_status;
    std::vector<DevAms::CannotDryReason> cannot_reasons;
    DevAms::DrySettings settings;
};

// How a module is fitted on one machine model; a new machine only needs an entry in DevDryer.cpp
struct DevDryerModuleProfile
{
    std::string   module_id;
    std::string   printer_type;
    DevAmsType    host_ams_type;
    std::set<int> slots;           // host slots the module dries
    int           host_slot_count; // host slots in total, for the layout while the host is offline
    DevAmsType    form_type;       // display form: images and drying presets follow this AMS type
    int           min_temp;
    int           max_temp;
    DevDryerType  dryer_type;

    static const DevDryerModuleProfile* Find(const std::string& module_id, const std::string& printer_type);
};

// Only present while the host AMS is connected
struct DevDryerSlotBinding
{
    std::string   ams_id;
    std::set<int> slots;
};

// Never part of an AMS; without a slot binding it still dries, just without filament info
class DevDryerModule
{
public:
    virtual ~DevDryerModule() = default;

    virtual std::string GetId() const = 0;

    const DevDryerModuleState& GetState() const { return m_state; }
    const DevDryerModuleProfile* GetProfile() const { return m_profile; }
    const std::optional<DevDryerSlotBinding>& GetSlotBinding() const { return m_slot_binding; }

protected:
    DevDryerModuleState m_state;

private:
    friend class DevDryerMgr;
    const DevDryerModuleProfile* m_profile = nullptr;
    std::optional<DevDryerSlotBinding> m_slot_binding;
};

// device.ht
class DevHtDryerModule : public DevDryerModule
{
public:
    static constexpr const char* ID = "ht";

    std::string GetId() const override { return ID; }

    bool IsPowerAdapterConnected() const { return m_power_adapter_connected; }
    bool IsDamperOpen() const { return m_damper_open; }

    void ParseV1_0(const nlohmann::json& ht_json);

private:
    bool m_power_adapter_connected = false;
    bool m_damper_open = false;
};

struct DevDryingSettings
{
    std::string filament;
    int         temp                 = 0;
    int         duration_hour        = 0;
    bool        rotate_tray          = false;
    int         cooling_temp         = 0;
    bool        close_power_conflict = false;
};

// Drying presets are kept per dryer form, so the same filament differs between dryers
struct DevFilamentDryingInfo
{
    std::string filament_id;
    float idle_temp            = 0;
    float idle_hours           = 0;
    float print_temp           = 0;
    float print_hours          = 0;
    float softening_temp       = 0;
    float heat_distortion_temp = 0;
    bool  fully_dryable        = false; // the preset lists this dryer's form

    // While printing, also capped by the softening and heat distortion points
    float GetRecommendedTemp(bool printing) const;
    float GetRecommendedHours(bool printing) const { return printing ? print_hours : idle_hours; }
};

// Short-lived view rebuilt on each use; a module's host AMS only adds the trays and readings
class DevDryer
{
public:
    std::string GetId() const;            // AMS id, or module id
    bool        IsModule() const { return m_module != nullptr; }
    // False for a module whose host AMS is offline
    bool        HasSlotInfo() const { return m_ams != nullptr; }
    DevAmsType  GetFormType() const;      // images and drying presets follow this AMS type
    DevDryerType GetDryerType() const;
    std::optional<std::pair<int, int>> GetTempRange() const;
    bool        IsSupportRemoteDry(const MachineObject* obj) const;

    // A module has a dry zone plus an ambient zone for the host's other slots
    const std::map<int, DevAmsZone>& GetZones() const { return m_zones; }
    std::optional<DevAmsZone> GetZone(int zone_id) const;
    std::optional<DevAmsZone> GetZoneBySlotId(int slot_id) const;
    bool             IsDryableZone(int zone_id) const;
    std::vector<int> GetDryableZoneIds() const;
    bool             IsDryableSlot(int slot_id) const;
    const std::map<std::string, DevAmsTray*>& GetTrays() const;

    float GetCurrentTemperature() const;
    int   GetHumidityPercent() const;
    int   GetLeftDryTime() const;
    std::optional<DevAms::DryStatus> GetDryStatus() const;
    std::optional<DevAms::DrySubStatus> GetDrySubStatus() const;
    std::optional<std::vector<DevAms::CannotDryReason>> GetCannotDryReason() const;
    std::optional<DevAms::DrySettings> GetDrySettings() const;

    // tray print_status first, then print.mapping, then the printing state alone
    bool IsInPrintUse(std::optional<int> zone_id) const;

    int CtrlStartDrying(std::optional<int> zone_id, const DevDryingSettings& settings) const;
    int CtrlStopDrying() const;

    static constexpr const char* FALLBACK_FILAMENT_ID = "GFA00"; // PLA
    std::optional<DevFilamentDryingInfo> GetFilamentDryingInfo(const std::string& filament_id) const;
    // An unknown filament is treated as PLA
    std::optional<DevFilamentDryingInfo> GetTrayFilamentDryingInfo(const DevAmsTray& tray) const;

    DevAms* GetAms() const { return m_ams; } // the AMS itself, or a module's host while connected
    const std::shared_ptr<DevDryerModule>& GetModule() const { return m_module; }

private:
    friend class DevDryerMgr;
    DevDryer() = default;

    // With is_ht the printer ignores ams_id/zone_id
    void FillCommandTarget(nlohmann::json& print_cmd, std::optional<int> zone_id) const;
    DevFilamentDryingInfo MakeFilamentDryingInfo(const DevFilamentDryingPreset& preset) const;

    MachineObject*                  m_owner = nullptr;
    DevAms*                         m_ams = nullptr;
    std::shared_ptr<DevDryerModule> m_module;
    std::map<int, DevAmsZone>       m_zones; // an AMS's own zones, or a module's dry + ambient zones
    std::set<int>                   m_dryable_zone_ids;
};

class DevDryerMgr
{
public:
    explicit DevDryerMgr(DevFilaSystem* fila_system) : m_fila_system(fila_system) {}

    // A device object without a module means it went offline
    void ParseV1_0(const nlohmann::json& print_json);

    // An AMS id or a module id; an AMS that cannot dry resolves to the module bound to it
    std::optional<DevDryer> Find(const std::string& id) const;
    std::vector<DevDryer> All() const;

    const std::map<std::string, std::shared_ptr<DevDryerModule>>& GetModules() const { return m_modules; }

private:
    void Bind();
    DevDryer FromAms(DevAms* ams) const;
    std::optional<DevDryer> FromModule(const std::shared_ptr<DevDryerModule>& dryer_module) const;

    DevFilaSystem* m_fila_system;
    std::map<std::string, std::shared_ptr<DevDryerModule>> m_modules;
};

} // namespace Slic3r
