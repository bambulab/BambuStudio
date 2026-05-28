#include "DevNozzleRack.h"

#include "DevFilaSystem.h"
#include "DevUtil.h"

#include "slic3r/GUI/DeviceManager.hpp"

wxDEFINE_EVENT(DEV_RACK_EVENT_READING_FINISHED, wxCommandEvent);
namespace Slic3r
{

DevNozzleRack::DevNozzleRack(DevNozzleSystem* nozzle_system)
    : wxEvtHandler(), m_nozzle_system(nozzle_system)
{

}


void DevNozzleRack::Reset()
{
    m_position = RACK_POS_UNKNOWN;
    m_status = RACK_STATUS_UNKNOWN;
    m_job = 0;
    m_steps.clear();
    m_rack_type = RackType::None;
    m_rack_nozzles.clear();
}

void DevNozzleRack::SendReadingFinished()
{
   wxCommandEvent evt(DEV_RACK_EVENT_READING_FINISHED);
   evt.SetEventObject(this);
   wxPostEvent(this, evt);
}

DevNozzle DevNozzleRack::GetNozzle(int idx) const
{
    auto iter = m_rack_nozzles.find(idx);
    if (iter == m_rack_nozzles.end()) {
        DevNozzle nozzle;
        nozzle.SetOnRack(true);
        return nozzle;
    }

    return iter->second;
}

DevFirmwareVersionInfo DevNozzleRack::GetNozzleFirmwareInfo(int nozzle_id) const
{
    auto iter = m_rack_nozzles_firmware.find(nozzle_id);
    return iter != m_rack_nozzles_firmware.end() ? iter->second : DevFirmwareVersionInfo();
}

std::optional<DevFirmwareVersionInfo> DevNozzleRack::GetNozzleFirwwareInfoBySn(const std::string& sn) const
{
    for (const auto& iter : m_rack_nozzles_firmware) {
        const auto& info = iter.second;
        if (info.sn == sn) {
            return info;
        }
    }

    return std::nullopt;
}


bool DevNozzleRack::HasUnreliableNozzles() const
{
    for (const auto& nozzle : m_rack_nozzles)
    {
        if (!nozzle.second.IsInfoReliable())
        {
            return true;
        }
    }
    return false;
}

bool DevNozzleRack::HasUnknownNozzles() const
{
    for (const auto& nozzle : m_rack_nozzles)
    {
        if (nozzle.second.IsUnknown())
        {
            return true;
        }
    }
    return false;
}

int DevNozzleRack::GetKnownNozzleCount() const
{
    int count = 0;
    for (const auto& nozzle : m_rack_nozzles)
    {
        if (!nozzle.second.IsEmpty() && !nozzle.second.IsUnknown())
        {
            count++;
        }
    }

    return count;
}

int DevNozzleRack::GetTotalRackNozzleCount() const
{
    if (m_rack_type == RackType_AB) {
        return 6;
    } else if (m_rack_type == RackType_A || m_rack_type == RackType_B) {
        return m_nozzle_system->GetOwner()->GetFilaSystem()->GetVirtualTrays().size();
    }

    return 0;
}


std::set<int> DevNozzleRack::GetTotalRackNozzleAddIdSet() const
{
    std::set<int> rack_nozzle_addid_set;

    int start_idx = 0;
    for (int idx = 0; idx < GetTotalRackNozzleCount(); idx++) {
        rack_nozzle_addid_set.insert(start_idx);
        start_idx++;
    }

    return rack_nozzle_addid_set;
}

std::set<int> DevNozzleRack::GetTotalRackNozzlePosSet() const
{
    std::set<int> rack_nozzle_pos_set;

    int start_idx = 16;
    for (int idx = 0; idx < GetTotalRackNozzleCount(); idx++) {
        rack_nozzle_pos_set.insert(start_idx);
        start_idx++;
    }

    return rack_nozzle_pos_set;
}

void DevNozzleRack::ParseRackInfo(const nlohmann::json& rack_info)
{
    ParseRackInfoV1_0(rack_info);
}

void DevNozzleRack::ParseRackInfoV1_0(const nlohmann::json& rack_info)
{
    DevJsonValParser::ParseVal(rack_info, "stat", m_status, RACK_STATUS_UNKNOWN);
    if (m_status < RACK_STATUS_UNKNOWN || m_status >= RACK_STATUS_END)
    {
        m_status = RACK_STATUS_UNKNOWN; // Reset to default if out of range
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": Invalid rack status: " << m_status << ", reset";
    }

    DevJsonValParser::ParseVal(rack_info, "pos", m_position, 0);
    DevJsonValParser::ParseVal(rack_info, "info", m_cali_status);
    DevJsonValParser::ParseVal(rack_info, "job", m_job, 0);
    m_steps.clear();
    if (rack_info.contains("step") && rack_info["step"].is_array()) {
        for (const auto& item : rack_info["step"]) {
            if (item.is_number_integer()) {
                m_steps.push_back(item.get<int>());
            }
        }
    }

    m_rack_type = RackType_AB; // default to RackType_AB
    if (rack_info.contains("info2")) {
        const auto& info2 = rack_info["info2"].get<int>();
        m_cali_status = (RackCaliStatus)DevUtil::get_flag_bits(info2, 0, 1);
        m_rack_type = (RackType)DevUtil::get_flag_bits(info2, 1, 4);
    }
}

};