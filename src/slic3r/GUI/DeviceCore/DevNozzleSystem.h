#pragma once
#include "DevDefs.h"
#include "DevFirmware.h"
#include "DevFilaSystem.h"

#include "libslic3r/CommonDefs.hpp"
#include "libslic3r/MultiNozzleUtils.hpp"
#include "slic3r/Utils/json_diff.hpp"

#include <wx/string.h>
#include <map>
#include <optional>

namespace Slic3r
{
// Previous definitions
class MachineObject;
class DevNozzleRack;

enum class CutType
{
    None = 0,
    Active = 1,
    Passive = 2,
    Ultrasonic = 3,
    Pen = 4,
    Unknown = 99,
};

struct CutDef
{
    CutType type = CutType::Unknown;
};

enum class DevNozzleToolType : int
{
    None = -1,
    Nozzle = 0,
    Cut = 1,
    Laser = 2,
    Fan = 3
};

struct DevNozzleToolDefs
{
    DevNozzleToolType tool_type = DevNozzleToolType::None;
    std::optional<NozzleDef> nozzle_info; // DevNozzleToolType::Nozzle
    std::optional<CutDef> cut_info; // DevNozzleToolType::Cut
};

struct DevNozzle
{
private:
    friend class DevNozzleSystemParser;

public:
    enum class NozzleTempStatus: int
    {
        Idle = 0,
        Heating = 1,
        Keeping = 2,
        Cooling = 3
    };

    // Coarse classification of the tool mounted on this rack position. This is
    // a projection of m_tool_defs (the single source of truth, filled by the
    // parser): Nozzle -> Hotend, Cut -> Cutter/Pen (pen == CutType::Pen), and
    // anything else -> Unknown. Only Hotend can be matched to a filament.
    enum class NozzleToolKind: int
    {
        Hotend = 0,
        Cutter = 1,
        Pen = 2,
        Unknown = 3
    };

    // nozzle info.stat bits [6:8]: 0 = no filament, 1 = suspect engagement, 2 = engaged
    enum class FilamentEngagementStatus : int
    {
        None = 0,
        Suspect = 1,
        Engaged = 2,
    };


    int             m_nozzle_id = -1;
    std::optional<int> m_addr_id;
    NozzleFlowType  m_nozzle_flow = NozzleFlowType::S_FLOW;// 0-common 1-high flow
    NozzleType      m_nozzle_type = NozzleType::ntUndefine;// 0-stainless_steel 1-hardened_steel 5-tungsten_carbide
    float           m_diameter = 0.4f;// 0.2mm  0.4mm  0.6mm 0.8mm
    DevNozzleToolDefs m_tool_defs;

public:
    static wxString             GetNozzleFlowTypeStr(NozzleFlowType type);
    static std::string          GetNozzleFlowTypeString(NozzleFlowType type);// no translation
    static NozzleFlowType       ToNozzleFlowType(const std::string& type);
    static NozzleVolumeType     ToNozzleVolumeType(const std::string& type);
    static NozzleFlowType       VariantToNozzleFlowType(const std::string& variant);
    static std::string          ToNozzleFlowString(const NozzleFlowType& type);

    static wxString             GetNozzleTypeStr(NozzleType type);
    static std::string          GetNozzleTypeString(NozzleType type);

    static NozzleFlowType       ToNozzleFlowType(const NozzleVolumeType& type);
    static wxString             GetNozzleVolumeTypeStr(const NozzleVolumeType& type);
    static NozzleVolumeType     ToNozzleVolumeType(const NozzleFlowType& type);
    static std::string          ToNozzleVolumeString(const NozzleVolumeType& type);
    static std::string          ToNozzleVolumeShortString(const NozzleVolumeType& type);

    static float                ToNozzleDiameterFloat(const NozzleDiameterType& type);
    static NozzleDiameterType   ToNozzleDiameterType(float diameter);
    static NozzleDiameterType   ToNozzleDiameterType(const std::string& diameter);
    static wxString             ToNozzleDiameterStr(const NozzleDiameterType& type);

public:
    int            GetNozzlePosId() const;
    int            GetNozzleId() const { return m_nozzle_id; }
    std::optional<int> GetAddrId() const { return m_addr_id; };

    bool     IsExist() const;
    bool     IsEmpty() const { return !IsExist(); }

    // for RackA/RackB
    bool     IsToolConnected() const { return m_hc_state; }
    bool     IsToolLostInHolder() const;
    bool     GetHcState() const { return m_hc_state; };

    /**/
    void       SetRack(const std::weak_ptr<DevNozzleRack>& rack) { m_nozzle_rack = rack; };

    /**/

    DevNozzleToolDefs GetToolDefs() const { return m_tool_defs; }
    NozzleToolKind GetToolKind() const;

    bool           IsHotend() const { return m_tool_defs.tool_type == DevNozzleToolType::Nozzle; }
    NozzleType     GetNozzleType() const { return m_nozzle_type; }
    NozzleFlowType GetNozzleFlowType() const { return m_nozzle_flow; }
    NozzleDiameterType GetNozzleDiameterType() const;
    float          GetNozzleDiameter() const { return m_diameter; }
    float          GetNozzleWear() const { return m_wear; }
    int            GetNozzlePrintTime() const { return m_nozzle_print_time; }
    std::optional<int> GetNozzleSwitchCount() const { return m_nozzle_switch_count;}

    // display
    wxString GetDisplayId() const;
    wxString GetNozzleDiameterStr() const { return wxString::Format("%.1f mm", m_diameter); }
    wxString GetNozzleFlowTypeStr() const;
    wxString GetNozzleTypeStr() const;

    // serial number
    wxString GetSerialNumber() const;
    DevFirmwareVersionInfo GetFirmwareInfo() const;

    // location
    bool AtLeftExtruder() const;
    bool AtRightExtruder() const;

    int  GetLogicExtruderId() const;// warning: logical extruder id
    int  GetExtruderId() const;// warning: physical extruder id

    /* holder nozzle*/
    bool IsOnRack() const { return m_on_rack; }
    bool IsInfoReliable() const;

    bool IsNormal() const;
    bool IsAbnormal() const;
    bool IsUnknown() const;

    std::string GetFilamentId() const { return m_fila_id; }
    std::string GetFilamentColor() const { return m_filament_clr; }

    void SetOnRack(bool on_rack) { m_on_rack = on_rack; };
    void SetStatus(int stat) { m_stat = stat; }

    void SetCurrentTemp(int temp) { m_current_temp = temp; }
    int  GetCurrentTemp() const { return m_current_temp; }

    void SetTargetTemp(int temp) { m_target_temp = temp; }
    int  GetTargetTemp() const { return m_target_temp; }

    void SetRackNozzleTempStatus(int temp_status);
    std::optional<NozzleTempStatus> GetRackNozzleTempStatus() const { return m_temp_status; }

    bool HasFilamentAtNozzleInlet() const { return m_has_filament_in_nozzle; }

    FilamentEngagementStatus GetFilamentEngagementStatus() const;

    // direct bind ams slots. eg. ZMULTI_A
    bool IsInBindAmsSlots(int ams_id, int slot_id) const;
    std::vector<DevAmsSlotId> GetBindAmsSlots() const { return m_bind_ams_slots; };

    // external ams ids bound to this nozzle (from device push "bind_ams")
    bool IsInBindAms(int ams_id) const;
    std::vector<int> GetBindAms() const { return m_bind_ams; };

    // Preload (pre-feed) AMS slots: rack-matching internal AMS only.
    // Differs from GetBindAmsSlots() when the current feed source is the
    // external spool but an internal AMS slot is also preloaded for this
    // rack nozzle.
    std::vector<DevAmsSlotId> GetPreloadBindAmsSlots() const { return m_preload_bind_ams_slots; };

    // M2Y
    std::optional<DevAmsSlotId> GetRelatedAmsSlot() const; // eg. 32,0
    std::optional<DevAmsSlotId> GetRelatedExtSlot() const; // eg. 254

    std::optional<DevAmsSlotId> GetSlotNowId() const { return m_slot_now; }
    std::optional<DevAmsTray> GetSlotNow() const;

private:
    int  GetTotalExtruderCount() const;

private:
    bool m_is_exist = true;
    bool m_hc_state = true;

    bool m_on_rack = false;

    int m_stat = 0;
    float m_wear;

    /* nozzle temperature */
    int m_current_temp = -1;
    int m_target_temp = -1;

    bool m_has_filament_in_nozzle = false;

    std::optional<NozzleTempStatus> m_temp_status = std::nullopt;

    std::string m_sn;
    std::string m_fila_id;// main material
    std::string m_filament_clr;// main color

    std::vector<DevAmsSlotId> m_bind_ams_slots;// direct bind ams slots which means when load/unload is using the slot. eg. ZMULTI_A 
    std::vector<DevAmsSlotId> m_preload_bind_ams_slots;// preload (pre-feed) AMS slot, rack-matching internal AMS only, no Ext fallback
    std::vector<int>          m_bind_ams;
    std::optional<DevAmsSlotId> m_slot_now;

    std::weak_ptr<DevNozzleRack> m_nozzle_rack; // weak pointer to the nozzle rack
    int m_nozzle_print_time = 0; // seconds
    std::optional<int> m_nozzle_switch_count;
};

class DevNozzleSystem
{
    friend class DevNozzleSystemParser;
private:
    enum Status : int
    {
        NOZZLE_SYSTEM_IDLE = 0,
        NOZZLE_SYSTEM_REFRESHING = 1,
    };

public:
    DevNozzleSystem(MachineObject* owner);
    virtual ~DevNozzleSystem() {};

public:
    MachineObject* GetOwner() const { return m_owner; }

    // nozzles
    DevNozzle                       GetNozzleByPosId(int pos_id) const { return pos_id < 0x10 ? GetExtNozzle(pos_id) : GetRackNozzle(pos_id - 0x10); };
    std::optional<DevNozzle>        GetNozzleOptByPosId(int pos_id) const;
    std::optional<DevNozzle>        GetNozzleOptByAddrId(int addr_id) const;

    // nozzles on extruder
    bool                            ContainsExtNozzle(int id) const { return m_ext_nozzles.find(id) != m_ext_nozzles.end(); }
    DevNozzle                       GetExtNozzle(int id) const;
    const std::map<int, DevNozzle>& GetExtNozzles() const { return m_ext_nozzles; }
    int                             GetExtNozzleCount() const { return m_ext_nozzles.size(); }

    // nozzles on rack
    void  SetSupportNozzleRack(bool supported);
    std::shared_ptr<DevNozzleRack>  GetNozzleRack() const { return m_nozzle_rack; }
    DevNozzle                       GetRackNozzle(int idx) const;
    const std::map<int, DevNozzle>& GetRackNozzles() const;

    // nozzles on extruder and rack
    bool IsRackMaximumInstalled() const;
    std::vector<MultiNozzleUtils::NozzleGroupInfo> GetNozzleGroups() const;
    const std::vector<DevNozzle> CollectNozzles(int ext_loc, NozzleFlowType flow_type, float diameter = -1.0f) const;
    ExtruderNozzleInfos  GetExtruderNozzleInfo() const;

    bool  IsIdle() const { return m_state_0_4 == NOZZLE_SYSTEM_IDLE; }
    bool  IsRefreshing() const { return m_state_0_4 == NOZZLE_SYSTEM_REFRESHING; }

    bool  HasUnreliableNozzles() const;
    bool  HasUnknownNozzles() const;
    int   GetKnownNozzleCountOn(int ext_id) const;

    /* reading*/
    int GetReadingIdx() const { return m_reading_idx; };
    int GetReadingCount() const { return m_reading_count; };
    int GetReadingNozzleIdFlag() const { return m_reading_nozzle_id_flag; };

    /* firmware*/
    void AddFirmwareInfoWTM(const DevFirmwareVersionInfo& info);
    void AddFirmwareInfoHE(const DevFirmwareVersionInfo& info);
    void ClearFirmwareInfoWTM();
    DevFirmwareVersionInfo GetExtruderNozzleFirmware() const { return m_ext_nozzle_firmware_info; }

    /* replace nozzle*/
    std::optional<int> GetReplaceNozzleSrc() const { return m_replace_nozzle_src; }
    std::optional<int> GetReplaceNozzleTar() const { return m_replace_nozzle_tar; }

private:
    void Reset();
    void ClearNozzles();

private:
    MachineObject* m_owner = nullptr;

    int m_state_0_4 = 0;

    std::optional<int> m_replace_nozzle_src; // replace nozzle source position
    std::optional<int> m_replace_nozzle_tar; // replace nozzle target position

    /* refreshing */
    int m_reading_idx = 0;
    int m_reading_count = 0;
    int m_reading_nozzle_id_flag; // 0: not read 0xff: read all

    // nozzles on extruder
    std::map<int, DevNozzle> m_ext_nozzles;
    DevFirmwareVersionInfo m_ext_nozzle_firmware_info;

    // nozzles on rack
    std::shared_ptr<DevNozzleRack> m_nozzle_rack;
};

class DevNozzleSystemParser
{
public:
    static void  ParseV1_0(const nlohmann::json& nozzletype_json, const nlohmann::json& diameter_json, DevNozzleSystem* system, std::optional<int> flag_e3d);
    static void  ParseV2_0(const json& device_json, DevNozzleSystem* system);

private:
    static void sTraverseBindAmsSlot(DevNozzleSystem* system, DevNozzle& nozzle_obj);
    static void sTraversePreloadBindAmsSlot(DevNozzleSystem* system, DevNozzle& nozzle_obj);
};
};
