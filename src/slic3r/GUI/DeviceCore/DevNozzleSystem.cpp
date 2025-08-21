#include "DevExtruderSystem.h"

#include "DevNozzleRack.h"
#include "DevNozzleSystem.h"
#include "DevUtil.h"

#include "slic3r/GUI/DeviceManager.hpp"

#include "slic3r/GUI/I18N.hpp"

namespace Slic3r
{

wxString DevNozzle::GetNozzleFlowTypeStr() const
{
    switch (m_nozzle_flow)
    {
        case NozzleFlowType::H_FLOW: return _L("High Flow");
        case NozzleFlowType::S_FLOW: return _L("Standard Flow");
        default: break;
    }

    return _L("Unknown");
}

bool DevNozzle::IsInfoReliable() const
{
    if (IsEmpty()) { return false;}
    return DevUtil::get_flag_bits(m_stat, 0, 1) == 0;
}

bool DevNozzle::IsNormal() const
{
    if (IsEmpty()) { return false;}
    return DevUtil::get_flag_bits(m_stat, 1, 2) == 0;
}

bool DevNozzle::IsAbnormal() const
{
    return DevUtil::get_flag_bits(m_stat, 1, 2) == (1 << 0);
}

bool DevNozzle::IsUnknown() const
{
    return DevUtil::get_flag_bits(m_stat, 1, 2) == (1 << 1);
}

wxString DevNozzle::GetDisplayId() const
{
    return wxString::Format("%d", m_nozzle_id + 1);
}

wxString DevNozzle::GetNozzleTypeStr() const
{
    switch (m_nozzle_type)
    {
    case Slic3r::ntHardenedSteel:  return _L("Hardened Steel");
    case Slic3r::ntStainlessSteel: return _L("Stainless Steel");
    case Slic3r::ntTungstenCarbide: return _L("Tungsten Carbide");
    default: break;
    }

    return _L("Unknown");
}

DevFirmwareVersionInfo DevNozzle::GetFirmwareInfo() const
{
    const auto& nozzle_rack = m_nozzle_rack.lock();
    if (nozzle_rack)
    {
        if (IsOnRack())
        {
            return nozzle_rack->GetNozzleFirmwareInfo(m_nozzle_id);
        }
        else
        {
            if (m_nozzle_id == 0)
            {
                return nozzle_rack->GetNozzleSystem()->GetExtruderNozzleFirmware();
            }
        }
    }

    return DevFirmwareVersionInfo();
}

DevNozzleSystem::DevNozzleSystem(MachineObject* owner)
    : m_owner(owner), m_nozzle_rack(std::make_shared<DevNozzleRack> (this))
{
}

DevNozzle DevNozzleSystem::GetNozzle(int id) const
{
    if (m_ext_nozzles.find(id) != m_ext_nozzles.end())
    {
        return m_ext_nozzles.at(id);
    }

    return DevNozzle();
}

std::string DevNozzle::GetNozzleFlowTypeString(NozzleFlowType type)
{
    switch (type) {
        case NozzleFlowType::H_FLOW: return "High Flow";
        case NozzleFlowType::S_FLOW: return "Standard";
        default: return "Unknown";
    }
}

std::string DevNozzle::GetNozzleTypeString(NozzleType type)
{
    switch (type) {
    case Slic3r::ntHardenedSteel:   return "Hardened Steel";
    case Slic3r::ntStainlessSteel:  return "Stainless Steel";
    case Slic3r::ntTungstenCarbide: return "Tungsten Carbide";
    case Slic3r::ntBrass:           return "Brass";
    default:                        return "Unknown";
    }
}

void DevNozzleSystem::Reset()
{
    m_ext_nozzles.clear();
    m_extder_exist = 0;
    m_state_0_4 = 0;
    m_reading_idx = 0;
    m_reading_count = 0;

    m_nozzle_rack->Reset();
}

void DevNozzleSystem::ClearNozzles()
{
    m_ext_nozzles.clear();
    m_nozzle_rack->ClearRackNozzles();
}

void DevNozzleSystem::AddFirmwareInfoWTM(const DevFirmwareVersionInfo& info)
{
    static const std::string s_wtm_prefix = "wtm/";
    auto pos = info.name.find(s_wtm_prefix);
    if (pos == string::npos)
    {
        m_ext_nozzle_firmware_info = info;
    }
    else
    {
        try
        {
            auto str = info.name.substr(s_wtm_prefix.size()); // remove "wtm/" prefix
            int rack_nozzle_id = std::stoi(str) - 0x10; // rack nozzle IDs start from 0x10
            m_nozzle_rack->AddNozzleFirmwareInfo(rack_nozzle_id, info);
        }
        catch (const std::exception& e)
        {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": Invalid nozzle ID in firmware name:  "
                                     << info.name << ", error: " << e.what();
        }
    }
}

void DevNozzleSystem::ClearFirmwareInfoWTM()
{
    m_ext_nozzle_firmware_info = DevFirmwareVersionInfo();
    m_nozzle_rack->ClearNozzleFirmwareInfo();
}

void DevNozzleSystem::SetSupportNozzleRack(bool supported)
{
    m_nozzle_rack->SetSupported(supported);
}

static unordered_map<string, NozzleFlowType> _str2_nozzle_flow_type = {
    {"S", NozzleFlowType::S_FLOW},
    {"H", NozzleFlowType::H_FLOW},
    {"A", NozzleFlowType::S_FLOW},
    {"X", NozzleFlowType::S_FLOW},
    {"E", NozzleFlowType::H_FLOW}
};

static unordered_map<string, NozzleType> _str2_nozzle_type = {
    {"00", NozzleType::ntStainlessSteel},
    {"01", NozzleType::ntHardenedSteel},
    {"05", NozzleType::ntTungstenCarbide}
};

static void s_parse_nozzle_type(const std::string& nozzle_type_str, DevNozzle& nozzle)
{
    if (NozzleTypeStrToEumn.count(nozzle_type_str) != 0)
    {
        nozzle.m_nozzle_type = NozzleTypeStrToEumn[nozzle_type_str];
    }
    else if (nozzle_type_str.length() >= 4)
    {
        const std::string& flow_type_str = nozzle_type_str.substr(1, 1);
        if (_str2_nozzle_flow_type.count(flow_type_str) != 0)
        {
            nozzle.m_nozzle_flow = _str2_nozzle_flow_type[flow_type_str];
        }
        const std::string& type_str = nozzle_type_str.substr(2, 2);
        if (_str2_nozzle_type.count(type_str) != 0)
        {
            nozzle.m_nozzle_type = _str2_nozzle_type[type_str];
        }
    }
    else if (nozzle_type_str == "N/A")
    {
        nozzle.m_nozzle_type = NozzleType::ntUndefine;
        nozzle.m_nozzle_flow = NozzleFlowType::NONE_FLOWTYPE;
    }
}


void DevNozzleSystemParser::ParseV1_0(const nlohmann::json& nozzletype_json,
                                      const nlohmann::json& diameter_json,
                                      DevNozzleSystem* system,
                                      std::optional<int> flag_e3d)
{
    //Since both the old and new protocols push data.
   // assert(system->m_nozzles.size() < 2);
    DevNozzle nozzle;
    nozzle.SetRack(system->GetNozzleRack());
    nozzle.m_nozzle_id = 0;
    nozzle.m_nozzle_flow = NozzleFlowType::S_FLOW; // default flow type

    {
        float nozzle_diameter = 0.0f;
        if (diameter_json.is_number_float())
        {
            nozzle_diameter = diameter_json.get<float>();
        }
        else if (diameter_json.is_string())
        {
            nozzle_diameter = DevUtil::string_to_float(diameter_json.get<std::string>());
        }

        if (nozzle_diameter == 0.0f)
        {
            nozzle.m_diameter = 0.0f;
        }
        else
        {
            nozzle.m_diameter = round(nozzle_diameter * 10) / 10;
        }
    }

    {
        if (nozzletype_json.is_string())
        {
            s_parse_nozzle_type(nozzletype_json.get<std::string>(), nozzle);
        }
    }

    {
        if (flag_e3d.has_value()) {
            // 0: BBL S_FLOW; 1:E3D H_FLOW (only P)
            if (flag_e3d.value() == 1) {
                nozzle.m_nozzle_flow = NozzleFlowType::H_FLOW;
            } else {
                nozzle.m_nozzle_flow = NozzleFlowType::S_FLOW;
            }
        }
    }

    system->m_ext_nozzles[nozzle.m_nozzle_id] = nozzle;
}


void DevNozzleSystemParser::ParseV2_0(const json& device_json, DevNozzleSystem* system)
{
    if (device_json.contains("nozzle"))
    {
        const json& nozzle_json = device_json["nozzle"];
        if (nozzle_json.contains("exist"))
        {
            system->m_extder_exist = DevUtil::get_flag_bits(nozzle_json["exist"].get<int>(), 0, 16);
        }

        if (nozzle_json.contains("state"))
        {
            int val = nozzle_json["state"].get<int>();
            system->m_state_0_4 = DevUtil::get_flag_bits(nozzle_json["state"].get<int>(), 0, 4);
            system->m_reading_count = DevUtil::get_flag_bits(nozzle_json["state"].get<int>(), 4, 4);
            system->m_reading_idx = DevUtil::get_flag_bits(nozzle_json["state"].get<int>(), 8, 4);
        }

        system->ClearNozzles();
        for (auto it = nozzle_json["info"].begin(); it != nozzle_json["info"].end(); it++)
        {
            DevNozzle nozzle_obj;
            nozzle_obj.SetRack(system->GetNozzleRack());

            const auto& njon = it.value();
            nozzle_obj.m_nozzle_id = DevUtil::get_hex_bits(njon["id"].get<int>(), 0);
            nozzle_obj.m_diameter = njon["diameter"].get<float>();
            s_parse_nozzle_type(njon["type"].get<std::string>(), nozzle_obj);
            if (njon.contains("stat"))/*maybe not contains*/
            {
                nozzle_obj.SetStatus(njon["stat"].get<int>());
            }

            if (njon.contains("color_m"))/*maybe not contains*/
            {
                nozzle_obj.m_filament_clr = njon["color_m"].get<std::string>();
            }

            if (DevUtil::get_hex_bits(njon["id"].get<int>(), 1) == 1)
            {
                system->m_nozzle_rack->AddRackNozzle(nozzle_obj);
            }
            else
            {
                system->m_ext_nozzles[nozzle_obj.m_nozzle_id] = nozzle_obj;
            }
        }
    }

    if (device_json.contains("holder"))
    {
        system->m_nozzle_rack->ParseRackInfo(device_json["holder"]);
    }
}

};