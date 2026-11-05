#pragma once
#include "libslic3r/CommonDefs.hpp"
#include "libslic3r/MultiNozzleUtils.hpp"
#include "slic3r/Utils/json_diff.hpp"

#include "DevNozzleSystem.h"
#include "DevFirmware.h"

#include <map>
#include <vector>
#include <optional>

// wx
#include <wx/string.h>
#include <wx/event.h>

wxDECLARE_EVENT(DEV_RACK_EVENT_READING_FINISHED, wxCommandEvent);

namespace Slic3r
{
// Previous definitions
class DevNozzleSystem;

class DevNozzleRack : public wxEvtHandler
{
public:
    enum RackStatus : int
    {
        RACK_STATUS_UNKNOWN = -1,
        RACK_STATUS_IDLE = 0,
        RACK_STATUS_HOTEND_CENTRE = 1,
        RACK_STATUS_TOOLHEAD_CENTRE = 2,
        RACK_STATUS_CALIBRATE_HOTEND_RACK = 3,
        RACK_STATUS_WAIT_HOTEND_HEATING = 4,
        RACK_STATUS_CUT_MATERIAL = 5,
        RACK_STATUS_UNLOCK_HOTEND = 6,
        RACK_STATUS_LIFT_HOTEND_RACK = 7,
        RACK_STATUS_PLACE_HOTEND = 8,
        RACK_STATUS_PICK_HOTEND = 9,
        RACK_STATUS_LOCK_HOTEND = 10,
        RACK_STATUS_MOVE = 11,
        RACK_STATUS_HOTEND_INFO_REFRESH = 12,
        RACK_STATUS_END,
    };

    enum RackType
    {
        None = -1,
        RackType_AB = 0,
        RackType_A = 1, // DevAmsType::ZMULTI_A
        RackType_B = 2, // DevAmsType::ZMULTI_B
        RackType_C = 3,
    };

    enum RackABPos : int
    {
        RACK_POS_UNKNOWN = 0,
        RACK_POS_A_TOP = 1,
        RACK_POS_B_TOP = 2,
        RACK_POS_CENTRE = 3,
        RACK_POS_END,
    };

    // which moves as a single unit instead of the two-side RackABPos layout.
    enum class DirectRackPos : int
    {
        RACK_POS_UNKNOWN = 0,
        RACK_POS_DOWN = 1,
        RACK_POS_UP = 2,
        RACK_POS_CENTRE = 3,
    };

    enum RackCaliStatus
    {
        Rack_CALI_UNKNOWN = -1,
        Rack_CALI_NOT = 0,
        Rack_CALI_OK = 1,
    };

public:
    DevNozzleRack(DevNozzleSystem* nozzle_system);
    ~DevNozzleRack() = default;

public:
    // Is supported by the printer
    bool IsSupported() const { return m_is_supported; };
    void SetSupported(bool supported) { m_is_supported = supported; }

    // getters
    DevNozzleSystem* GetNozzleSystem() const { return m_nozzle_system; }

    RackABPos     GetPosition() const { return (RackABPos)m_position; }
    DirectRackPos GetDirectRackPos() const { return (DirectRackPos)m_position; }

    RackStatus  GetStatus() const { return m_status; }
    RackCaliStatus GetCaliStatus() const { return m_cali_status; }
    bool IsHotendDockingCalibrated(int addr_id) const;
    bool HasHotendSlotDockingCalibrated() const;
    int GetJob() const { return m_job; }
    bool IsPicking() const;
    bool IsPlacing() const;
    const std::vector<int>& GetSteps() const { return m_steps; }

    DevNozzle GetNozzle(int idx) const;
    const std::map<int, DevNozzle>& GetRackNozzles() const { return m_rack_nozzles; }

    bool        IsDirectConnectedRack() const { return m_rack_type > RackType_AB;}

    RackType    GetRackType() const { return m_rack_type; }
    int         GetTotalRackNozzleCount() const;

    std::set<int> GetTotalRackNozzleAddIdSet() const;
    std::set<int> GetTotalRackNozzlePosSet() const;

    // status
    bool HasUnreliableNozzles() const;
    bool HasUnknownNozzles() const;
    int GetKnownNozzleCount() const;

    // refreshing
    int GetReadingIdx() const { return m_nozzle_system->GetReadingIdx(); }
    int GetReadingCount() const { return m_nozzle_system->GetReadingCount(); }
    void SendReadingFinished();

    // firmware
    void AddNozzleFirmwareInfo(int nozzle_id, const DevFirmwareVersionInfo& info) { m_rack_nozzles_firmware[nozzle_id] = info; }
    void ClearNozzleFirmwareInfo() { m_rack_nozzles_firmware.clear(); }
    DevFirmwareVersionInfo GetNozzleFirmwareInfo(int nozzle_id) const;
    std::optional<DevFirmwareVersionInfo> GetNozzleFirwwareInfoBySn(const std::string& sn) const;

    // setters
    void  Reset();
    void  AddRackNozzle(DevNozzle& nozzle) { nozzle.SetOnRack(true); m_rack_nozzles[nozzle.m_nozzle_id] = nozzle; };
    void  ClearRackNozzles() { m_rack_nozzles.clear(); }

public:
    void ParseRackInfo(const nlohmann::json& rack_info);

    void CtrlRackPosMove(DirectRackPos new_pos) const;
    void CtrlRackPosMove(RackABPos new_pos) const;
    void CtrlRackPosGoHome() const;

    void CtrlRackConfirmNozzle(int rack_nozzle_id) const;
    void CtrlRackConfirmAll() const;

    void CrtlRackReadNozzle(int rack_nozzle_id) const;
    void CtrlRackReadAll(bool gui_check = false) const;
    void CtrlNozzleExchange(int from, int to) const;
    bool CtrlCanReadAll() const;

    void CtrlStopHotendRackJob() const;
    void CtrlRetryHotendRackJob() const;

    void SetNozzleTargetTemp(int nozzle_pos_id, int temp);

#if 0
    // the upgrade is not supported
    // the GUI interface is removed in STUDIO-14506
    int CtrlRackUpgradeExtruderNozzle() const;
    int CtrlRackUpgradeRackNozzle(int rack_nozzle_id) const;;
    int CtrlRackUpgradeAll() const;;
    bool CtrlCanUpdateAll() const;
#endif

private:
    void ParseRackInfoV1_0(const nlohmann::json& rack_info);

    int CtrlRackUpgrade(const std::string& module_str) const;

    bool CheckRackMoveWarningDlg() const;

private:
    DevNozzleSystem* m_nozzle_system = nullptr;

    bool m_is_supported = false; // Indicates if the nozzle rack is supported by the printer
    int m_position = RACK_POS_UNKNOWN;
    RackStatus m_status = RACK_STATUS_UNKNOWN;
    RackCaliStatus m_cali_status = Rack_CALI_UNKNOWN;
    int m_rack_cali_detail_bits = 0;
    int m_job = 0;
    std::vector<int> m_steps;

    std::map<int, DevNozzle> m_rack_nozzles; // Map of nozzle ID to DevNozzle objects
    std::map<int, DevFirmwareVersionInfo> m_rack_nozzles_firmware;

    RackType m_rack_type = RackType::None;
};
};