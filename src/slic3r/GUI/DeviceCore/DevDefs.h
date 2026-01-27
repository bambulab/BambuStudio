/**
* @file  DevDefs.h
* @brief Common definitions, macros, and constants for printer modules.
*        Enhance building and including.
*
* This file provides shared macros, constants, and enumerations for all printer-related
* modules, including printer types, status, binding states, connection types, and error codes.
* It is intended to be included wherever printer-specific definitions are required.
*/

#pragma once
#include <string>
#include <nlohmann/json.hpp>

// Reserved for future usage
#define DEV_RESERVED_FOR_FUTURE(...) /* stripped */

// Previous definitions
namespace Slic3r
{
    class MachineObject;
}

enum PrinterArch
{
    ARCH_CORE_XY,// move hotbed
    ARCH_I3,//move z
};

enum PrinterSeries
{
    SERIES_X1 = 0,
    SERIES_P1P,
    SERIES_UNKNOWN,
};

namespace Slic3r
{

/*AMS*/
enum AmsStatusMain
{
    AMS_STATUS_MAIN_IDLE = 0x00,
    AMS_STATUS_MAIN_FILAMENT_CHANGE = 0x01,
    AMS_STATUS_MAIN_RFID_IDENTIFYING = 0x02,
    AMS_STATUS_MAIN_ASSIST = 0x03,
    AMS_STATUS_MAIN_CALIBRATION = 0x04,
    AMS_STATUS_MAIN_COLD_PULL = 0x07,
    AMS_STATUS_MAIN_SELF_CHECK = 0x10,
    AMS_STATUS_MAIN_DEBUG = 0x20,
    AMS_STATUS_MAIN_UNKNOWN = 0xFF,
};

// Slots and Tray
#define VIRTUAL_TRAY_MAIN_ID    255
#define VIRTUAL_TRAY_DEPUTY_ID  254

#define VIRTUAL_AMS_MAIN_ID_STR   "255"
#define VIRTUAL_AMS_DEPUTY_ID_STR "254"

#define INVALID_AMS_TEMPERATURE std::numeric_limits<float>::min()

/* Extruder*/
#define MAIN_EXTRUDER_ID          0
#define DEPUTY_EXTRUDER_ID        1
#define UNIQUE_EXTRUDER_ID        MAIN_EXTRUDER_ID
#define INVALID_EXTRUDER_ID       -1

// see PartPlate::get_physical_extruder_by_logical_extruder
#define LOGIC_UNIQUE_EXTRUDER_ID  0
#define LOGIC_L_EXTRUDER_ID       0
#define LOGIC_R_EXTRUDER_ID       1

/* Nozzle*/
enum NozzleFlowType : int
{
    NONE_FLOWTYPE,
    S_FLOW,
    H_FLOW,
    U_FLOW, // TPU 1.75 High Flow
};
/* 0.2mm  0.4mm  0.6mm 0.8mm */
enum NozzleDiameterType : int
{
    NONE_DIAMETER_TYPE,
    NOZZLE_DIAMETER_0_2,
    NOZZLE_DIAMETER_0_4,
    NOZZLE_DIAMETER_0_6,
    NOZZLE_DIAMETER_0_8
};

/*Print speed*/
enum DevPrintingSpeedLevel
{
    SPEED_LEVEL_INVALID = 0,
    SPEED_LEVEL_SILENCE = 1,
    SPEED_LEVEL_NORMAL = 2,
    SPEED_LEVEL_RAPID = 3,
    SPEED_LEVEL_RAMPAGE = 4,
    SPEED_LEVEL_COUNT
};

/*Upgrade*/
enum class DevFirmwareUpgradeState : int
{
    DC = -1,
    UpgradingUnavaliable = 0,
    UpgradingAvaliable = 1,
    UpgradingInProgress = 2,
    UpgradingFinished = 3
};

struct DevNozzleMappingResult
{
    friend class MachineObject;
public:
    void Clear();

    bool HasResult() const { return !m_result.empty();}
    std::string GetResultStr() const { return m_result; }

    // mqtt error info
    std::string GetMqttReason() const { return m_mqtt_reason; }

    // command error info
    int GetErrno() const { return m_errno; }
    std::string GetDetailMsg() const { return m_detail_msg; }

    // nozzle mapping
    std::unordered_map<int, int> GetNozzleMapping() const { return m_nozzle_mapping; }
    nlohmann::json GetNozzleMappingJson() const { return m_nozzle_mapping_json; }
    void SetManualNozzleMapping(Slic3r::MachineObject* obj, int fila_id, int nozzle_pos_id);
    int  GetMappedNozzlePosIdByFilaId(Slic3r::MachineObject* obj, int fila_id) const;// return -1 if not mapped

    // flush weight
    float  GetFlushWeightBase() const { return m_flush_weight_base;}
    float  GetFlushWeightCurrent() const { return m_flush_weight_current; }

public:
    void ParseAutoNozzleMapping(Slic3r::MachineObject* obj, const nlohmann::json& print_jj);

private:
    float  GetFlushWeight(Slic3r::MachineObject* obj) const;

private:
    std::string m_sequence_id;

    std::string m_result;
    std::string m_mqtt_reason;
    std::string m_type; // auto or manual

    int         m_errno;
    std::string m_detail_msg;
    nlohmann::json m_detail_json;

    nlohmann::json m_nozzle_mapping_json;
    std::unordered_map<int, int> m_nozzle_mapping; // key: fila_id, value: nozzle_id (from 0x10), the tar_id is no extruder

    float m_flush_weight_base = -1;// the base weight for flush
    float m_flush_weight_current = -1;// the weight current
};

class devPrinterUtil
{
public:
    devPrinterUtil() = delete;
    ~devPrinterUtil() = delete;

public:
    static bool IsVirtualSlot(int ams_id) { return (ams_id == VIRTUAL_TRAY_MAIN_ID || ams_id == VIRTUAL_TRAY_DEPUTY_ID);}
    static bool IsVirtualSlot(const std::string& ams_id) { return (ams_id == VIRTUAL_AMS_MAIN_ID_STR || ams_id == VIRTUAL_AMS_DEPUTY_ID_STR); }
};

};// namespace Slic3r

struct NozzleDef
{
    float                  nozzle_diameter;
    Slic3r::NozzleFlowType nozzle_flow_type;

    bool operator==(const NozzleDef& other) const
    {
        return nozzle_diameter == other.nozzle_diameter && nozzle_flow_type == other.nozzle_flow_type;
    }
};

template<> struct std::hash<NozzleDef>
{
    std::size_t operator()(const NozzleDef& v) const noexcept
    {
        size_t h1 = std::hash<int>{}(v.nozzle_diameter * 1000);
        size_t h2 = std::hash<int>{}(v.nozzle_flow_type);
        return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    };
};

// key(extruder_id) -> { key1(nozzle type info), val1( number of the nozzle type)}
using ExtruderNozzleInfos = std::unordered_map<int, std::unordered_map<NozzleDef, int>>;