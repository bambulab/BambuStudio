#include "DevNozzleRack.h"
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
    return iter == m_rack_nozzles.end() ? DevNozzle() : iter->second;
}

DevFirmwareVersionInfo DevNozzleRack::GetNozzleFirmwareInfo(int nozzle_id) const
{
    auto iter = m_rack_nozzles_firmware.find(nozzle_id);
    return iter != m_rack_nozzles_firmware.end() ? iter->second : DevFirmwareVersionInfo();
}

std::vector<MultiNozzleUtils::NozzleGroupInfo> DevNozzleRack::GetNozzleGroups() const
{
    std::vector<MultiNozzleUtils::NozzleGroupInfo> nozzle_groups;
    auto nozzle_in_extruder = this->GetNozzleSystem()->GetNozzles();
    for (auto& elem : nozzle_in_extruder) {
        auto& nozzle = elem.second;
        MultiNozzleUtils::NozzleGroupInfo info;
        info.extruder_id = nozzle.AtLeftExtruder() ? 0 : nozzle.AtRightExtruder() ? 1 : 0;
        info.diameter = nozzle.GetNozzleDiameterStr().ToStdString();
        info.volume_type = nozzle.m_nozzle_flow == NozzleFlowType::H_FLOW ? NozzleVolumeType::nvtHighFlow : NozzleVolumeType::nvtStandard;
        info.nozzle_count = 1;
        nozzle_groups.emplace_back(std::move(info));
    }

    auto nozzle_in_rack = this->GetRackNozzles(); // nozzles in rack
    for (auto& elem : nozzle_in_rack) {
        auto& nozzle = elem.second;
        if (nozzle.IsUnknown() || !nozzle.IsInfoReliable())
            continue;
        int extruder_id = nozzle.AtLeftExtruder() ? 0 : nozzle.AtRightExtruder() ? 1 : 0;
        std::string diameter = nozzle.GetNozzleDiameterStr().ToStdString();
        NozzleVolumeType volume_type = nozzle.m_nozzle_flow == NozzleFlowType::H_FLOW ? NozzleVolumeType::nvtHighFlow : NozzleVolumeType::nvtStandard;

        bool found = false;
        for (auto& group : nozzle_groups) {
            if (group.extruder_id == extruder_id &&
                group.volume_type == volume_type &&
                group.diameter == diameter) {
                found = true;
                group.nozzle_count += 1;
                break;
            }
        }
        if (!found)
            nozzle_groups.emplace_back(diameter, volume_type, extruder_id, 1);
    }
    return nozzle_groups;
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

    DevJsonValParser::ParseVal(rack_info, "pos", m_position, RACK_POS_UNKNOWN);
    if (m_position < RACK_POS_UNKNOWN || m_position >= RACK_POS_END)
    {
        m_position = RACK_POS_UNKNOWN; // Reset to default if out of range
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": Invalid rack position: " << m_position << ", reset";
    }

    DevJsonValParser::ParseVal(rack_info, "info", m_cali_status);
}

};