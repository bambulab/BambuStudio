#include "DevExtruderSystem.h"

#include "DevFilaSystem.h"
#include "DevNozzleRack.h"
#include "DevNozzleSystem.h"
#include "DevUtil.h"

#include "slic3r/GUI/DeviceManager.hpp"

#include "slic3r/GUI/I18N.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/Log.hpp"

namespace Slic3r
{

bool DevNozzle::IsExist() const
{
    return m_is_exist && m_nozzle_id > -1;
}

bool DevNozzle::IsToolLostInHolder() const
{
    return m_hc_state && !m_is_exist && m_nozzle_id > -1;
}

wxString DevNozzle::GetNozzleFlowTypeStr() const
{
    return GetNozzleFlowTypeStr(m_nozzle_flow);
}

wxString DevNozzle::GetNozzleFlowTypeStr(NozzleFlowType type)
{
    switch (type) {
    case NozzleFlowType::H_FLOW: return _L("High Flow");
    case NozzleFlowType::S_FLOW: return _L("Standard");
    case NozzleFlowType::U_FLOW: return _L("TPU High Flow");
    case NozzleFlowType::T_FLOW: return _L("TPU285");
    case NozzleFlowType::E_FLOW: return _L("E3D High Flow");
    default: return _L("Unknown");
    }

    return _L("Unknown");
}

std::string DevNozzle::GetNozzleFlowTypeString(NozzleFlowType type)
{
    switch (type) {
        case NozzleFlowType::H_FLOW: return "High Flow";
        case NozzleFlowType::S_FLOW: return "Standard";
        case NozzleFlowType::U_FLOW: return "TPU High Flow";
        case NozzleFlowType::T_FLOW: return "TPU285";
        case NozzleFlowType::E_FLOW: return "E3D High Flow";
        default: return "Unknown";
    }
}

NozzleFlowType DevNozzle::ToNozzleFlowType(const std::string& type)
{
    if(type == "Standard")
        return NozzleFlowType::S_FLOW;
    else if(type == "High Flow")
        return NozzleFlowType::H_FLOW;
    else if(type == "TPU High Flow")
        return NozzleFlowType::U_FLOW;
    else if(type == "TPU285")
        return NozzleFlowType::T_FLOW;
    else if(type == "E3D High Flow")
        return NozzleFlowType::E_FLOW;
    else
        return NozzleFlowType::NONE_FLOWTYPE;
}

NozzleVolumeType DevNozzle::ToNozzleVolumeType(const std::string &type)
{
    if(type == "Standard")
        return NozzleVolumeType::nvtStandard;
    else if(type == "High Flow")
        return NozzleVolumeType::nvtHighFlow;
    else if(type == "TPU High Flow")
        return NozzleVolumeType::nvtTPUHighFlow;
    else if(type == "TPU285")
        return NozzleVolumeType::nvtTPU285;
    else if(type == "E3D High Flow")
        return NozzleVolumeType::nvtE3DHighFlow;
    else
        return NozzleVolumeType::nvtStandard;
}

std::string DevNozzle::ToNozzleFlowString(const NozzleFlowType& type)
{
    switch (type) {
    case NozzleFlowType::S_FLOW: return "Standard";
    case NozzleFlowType::H_FLOW: return "High Flow";
    case NozzleFlowType::U_FLOW: return "TPU High Flow";
    case NozzleFlowType::T_FLOW: return "TPU285";
    case NozzleFlowType::E_FLOW: return "E3D High Flow";
    default: return std::string();
    }
}

NozzleFlowType DevNozzle::VariantToNozzleFlowType(const std::string& variant)
{
    // Order matters: variant is a composite like "Bowden TPU High Flow", so match
    // longer flow names first to avoid the "High Flow" substring shadowing
    // "TPU High Flow" / "E3D High Flow".
    if (variant.find("TPU High Flow") != std::string::npos) {
        return NozzleFlowType::U_FLOW;
    } else if (variant.find("E3D High Flow") != std::string::npos) {
        return NozzleFlowType::E_FLOW;
    } else if (variant.find("TPU285") != std::string::npos) {
        return NozzleFlowType::T_FLOW;
    } else if (variant.find("High Flow") != std::string::npos) {
        return NozzleFlowType::H_FLOW;
    } else if (variant.find("Standard") != std::string::npos) {
        return NozzleFlowType::S_FLOW;
    } else {
        return NozzleFlowType::S_FLOW;
    }
}

NozzleVolumeType DevNozzle::ToNozzleVolumeType(const NozzleFlowType& type)
{
    switch (type) {
        case NozzleFlowType::S_FLOW: return NozzleVolumeType::nvtStandard;
        case NozzleFlowType::H_FLOW: return NozzleVolumeType::nvtHighFlow;
        case NozzleFlowType::U_FLOW: return NozzleVolumeType::nvtTPUHighFlow;
        case NozzleFlowType::T_FLOW: return NozzleVolumeType::nvtTPU285;
        case NozzleFlowType::E_FLOW: return NozzleVolumeType::nvtE3DHighFlow;
        default: {
            BOOST_LOG_TRIVIAL(warning) << "nozzle flow type None convert to nozzle volume type Standard";
            return NozzleVolumeType::nvtStandard;
        }
    }
}

NozzleFlowType DevNozzle::ToNozzleFlowType(const NozzleVolumeType& type)
{
    switch (type) {
        case NozzleVolumeType::nvtStandard:     return NozzleFlowType::S_FLOW;
        case NozzleVolumeType::nvtHighFlow:     return NozzleFlowType::H_FLOW;
        case NozzleVolumeType::nvtTPUHighFlow:  return NozzleFlowType::U_FLOW;
        case NozzleVolumeType::nvtTPU285:       return NozzleFlowType::T_FLOW;
        case NozzleVolumeType::nvtE3DHighFlow:  return NozzleFlowType::E_FLOW;
        default: return NozzleFlowType::NONE_FLOWTYPE;
    }
}

wxString DevNozzle::GetNozzleVolumeTypeStr(const NozzleVolumeType& type)
{
    switch (type) {
        case NozzleVolumeType::nvtStandard:     return _L("Standard");
        case NozzleVolumeType::nvtHighFlow:     return _L("High Flow");
        case NozzleVolumeType::nvtTPUHighFlow:  return _L("TPU High Flow");
        case NozzleVolumeType::nvtTPU285:       return _L("TPU285");
        case NozzleVolumeType::nvtE3DHighFlow:  return _L("E3D High Flow");
        case NozzleVolumeType::nvtHybrid:       return _L("Hybrid");
        default: return wxEmptyString;
    }
}

std::string DevNozzle::ToNozzleVolumeString(const NozzleVolumeType& type)
{
    return ToNozzleFlowString(ToNozzleFlowType(type));
}

std::string DevNozzle::ToNozzleVolumeShortString(const NozzleVolumeType& type)
{
    switch (type) {
    case NozzleVolumeType::nvtStandard:     return "Std";
    case NozzleVolumeType::nvtHighFlow:     return "HF";
    case NozzleVolumeType::nvtTPUHighFlow:  return "UHF";
    case NozzleVolumeType::nvtTPU285:       return "TPU285";
    case NozzleVolumeType::nvtE3DHighFlow:  return "EHF";
    default: return std::string();
    }
}

float DevNozzle::ToNozzleDiameterFloat(const NozzleDiameterType &type)
{
    switch (type){
        case NozzleDiameterType::NOZZLE_DIAMETER_0_2: return 0.2f;
        case NozzleDiameterType::NOZZLE_DIAMETER_0_4: return 0.4f;
        case NozzleDiameterType::NOZZLE_DIAMETER_0_6: return 0.6f;
        case NozzleDiameterType::NOZZLE_DIAMETER_0_8: return 0.8f;
        default: return 0.4f;
    }
}

NozzleDiameterType DevNozzle::ToNozzleDiameterType(float diameter)
{
    if(is_approx(diameter, 0.2f)) {
        return NozzleDiameterType::NOZZLE_DIAMETER_0_2;
    } else if(is_approx(diameter, 0.4f)) {
        return NozzleDiameterType::NOZZLE_DIAMETER_0_4;
    } else if(is_approx(diameter, 0.6f)) {
        return NozzleDiameterType::NOZZLE_DIAMETER_0_6;
    } else if(is_approx(diameter, 0.8f)) {
        return NozzleDiameterType::NOZZLE_DIAMETER_0_8;
    } else{
        return NozzleDiameterType::NONE_DIAMETER_TYPE;
    }
}

NozzleDiameterType DevNozzle::ToNozzleDiameterType(const std::string& diameter)
{
    if(diameter == "0.2") {
        return NozzleDiameterType::NOZZLE_DIAMETER_0_2;
    } else if(diameter == "0.4") {
        return NozzleDiameterType::NOZZLE_DIAMETER_0_4;
    } else if(diameter == "0.6") {
        return NozzleDiameterType::NOZZLE_DIAMETER_0_6;
    } else if(diameter == "0.8") {
        return NozzleDiameterType::NOZZLE_DIAMETER_0_8;
    } else{
        return NozzleDiameterType::NONE_DIAMETER_TYPE;
    }
}

bool DevNozzle::IsInfoReliable() const
{
    if (IsEmpty()) { return false;}
    return DevUtil::get_flag_bits(m_stat, 0, 1) == 0;
}

bool DevNozzle::IsNormal() const
{
    if (IsEmpty()) { return false; }
    return DevUtil::get_flag_bits(m_stat, 1, 2) == 0;
}

bool DevNozzle::IsAbnormal() const
{
    return DevUtil::get_flag_bits(m_stat, 1, 2) == (1 << 0);
}

bool DevNozzle::IsUnknown() const
{
    // only support unknown for printers with rack. AP 
    if (auto rack_ptr = m_nozzle_rack.lock(); rack_ptr && rack_ptr->IsSupported()) {
        return DevUtil::get_flag_bits(m_stat, 1, 2) == (1 << 1);
    }

    return false;
}


bool DevNozzle::IsInBindAmsSlots(int ams_id, int slot_id) const
{
    for(const auto& slot : m_bind_ams_slots){
        if(slot.first == ams_id && slot.second == slot_id) {
            return true;
        }
    }

    return false;
}

bool DevNozzle::IsInBindAms(int ams_id) const
{
    for (const auto bind_ams_id : m_bind_ams) {
        if (bind_ams_id == ams_id) {
            return true;
        }
    }

    return false;
}

std::optional<DevAmsSlotId> DevNozzle::GetRelatedAmsSlot() const
{
    auto rack = m_nozzle_rack.lock();
    if (rack && rack->IsDirectConnectedRack() && GetAddrId().has_value()) {
        auto fila_sys = rack->GetNozzleSystem()->GetOwner()->GetFilaSystem();
        for (const auto& [_, ams] : fila_sys->GetAmsList()) {
            if (ams && ams->GetSlotCount() == rack->GetTotalRackNozzleCount()) {
                return DevAmsSlotId{ ams->GetAmdIdInt(), GetAddrId().value() };
            }
        }
    }

    return std::nullopt;
}

std::optional<DevAmsSlotId> DevNozzle::GetRelatedExtSlot() const
{
    auto rack = m_nozzle_rack.lock();
    if (rack && rack->IsDirectConnectedRack() && GetAddrId().has_value()) {
        auto fila_sys = rack->GetNozzleSystem()->GetOwner()->GetFilaSystem();
        const int vt_tray_id = 255 - GetAddrId().value();
        if (fila_sys->GetVirtualTray(vt_tray_id).has_value()) {
            return DevAmsSlotId{ vt_tray_id, 0 };
        }
    }

    return std::nullopt;
}

DevNozzle::FilamentEngagementStatus DevNozzle::GetFilamentEngagementStatus() const
{
    if (IsEmpty()) {
        return FilamentEngagementStatus::None;
    }
    const int status = DevUtil::get_flag_bits(m_stat, 6, 3);
    if (status == static_cast<int>(FilamentEngagementStatus::Suspect)) {
        return FilamentEngagementStatus::Suspect;
    }
    if (status == static_cast<int>(FilamentEngagementStatus::Engaged)) {
        return FilamentEngagementStatus::Engaged;
    }
    return FilamentEngagementStatus::None;
}

int DevNozzle::GetNozzlePosId() const
{
    if (m_addr_id.has_value()) {
        return *m_addr_id + 0x10;
    }
    return IsOnRack() ? (m_nozzle_id + 0x10) : m_nozzle_id;
}

wxString DevNozzle::GetDisplayId() const
{
    return wxString::Format("%d", m_nozzle_id + 1);
};

DevNozzle::NozzleToolKind DevNozzle::GetToolKind() const
{
    switch (m_tool_defs.tool_type) {
        case DevNozzleToolType::Nozzle: return NozzleToolKind::Hotend;
        case DevNozzleToolType::Cut:
            return (m_tool_defs.cut_info.has_value() && m_tool_defs.cut_info->type == CutType::Pen)
                ? NozzleToolKind::Pen
                : NozzleToolKind::Cutter;
        default: return NozzleToolKind::Unknown;
    }
}

wxString DevNozzle::GetNozzleTypeStr() const
{
    return GetNozzleTypeStr(m_nozzle_type);
}

wxString DevNozzle::GetNozzleTypeStr(NozzleType type)
{
    switch (type) {
    case Slic3r::ntHardenedSteel:  return _L("Hardened Steel");
    case Slic3r::ntStainlessSteel: return _L("Stainless Steel");
    case Slic3r::ntTungstenCarbide: return _L("Tungsten Carbide");
    default: break;
    }

    return _L("Unknown");
}

NozzleDiameterType DevNozzle::GetNozzleDiameterType() const
{
    if (is_approx(m_diameter, 0.2f))
        return NozzleDiameterType::NOZZLE_DIAMETER_0_2;
    else if(is_approx(m_diameter, 0.4f))
        return NozzleDiameterType::NOZZLE_DIAMETER_0_4;
    else if(is_approx(m_diameter, 0.6f))
        return NozzleDiameterType::NOZZLE_DIAMETER_0_6;
    else if(is_approx(m_diameter, 0.8f))
        return NozzleDiameterType::NOZZLE_DIAMETER_0_8;
    else
        return NozzleDiameterType::NONE_DIAMETER_TYPE;
}

template<typename T>
std::string to_string_with_precision(T num, int decimal_places = 2)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(decimal_places) << num;
    return oss.str();
}

wxString DevNozzle::ToNozzleDiameterStr(const NozzleDiameterType& type)
{
    switch (type) {
    case NozzleDiameterType::NOZZLE_DIAMETER_0_2: return to_string_with_precision(0.2f, 1) + " mm";
    case NozzleDiameterType::NOZZLE_DIAMETER_0_4: return to_string_with_precision(0.4f, 1) + " mm";
    case NozzleDiameterType::NOZZLE_DIAMETER_0_6: return to_string_with_precision(0.6f, 1) + " mm";
    case NozzleDiameterType::NOZZLE_DIAMETER_0_8: return to_string_with_precision(0.8f, 1) + " mm";
    default: return _L("Unknown");
    }
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

int DevNozzle::GetLogicExtruderId() const
{
    int total_ext_count = GetTotalExtruderCount();
    if (total_ext_count == 1) {
        return LOGIC_UNIQUE_EXTRUDER_ID;
    } else if(total_ext_count == 2) {
        if (AtLeftExtruder()) {
            return LOGIC_L_EXTRUDER_ID;
        } else if (AtRightExtruder()) {
            return LOGIC_R_EXTRUDER_ID;
        }
    }

    assert(0);
    return LOGIC_UNIQUE_EXTRUDER_ID;
}

int DevNozzle::GetExtruderId() const
{
    int total_ext_count = GetTotalExtruderCount();
    if (total_ext_count == 1) {
        return MAIN_EXTRUDER_ID;
    } else if (total_ext_count == 2) {
        if (AtRightExtruder()) {
            return MAIN_EXTRUDER_ID;
        } else if (AtLeftExtruder()) {
            return DEPUTY_EXTRUDER_ID;
        }
    }

    return MAIN_EXTRUDER_ID;
}

bool DevNozzle::AtLeftExtruder() const
{
    assert(GetTotalExtruderCount() == 2);
    if (IsOnRack()) { return false; }
    return m_nozzle_id == DEPUTY_EXTRUDER_ID;
}

bool DevNozzle::AtRightExtruder() const
{
    assert(GetTotalExtruderCount() == 2);
    if (IsOnRack()) { return true; }
    return m_nozzle_id == MAIN_EXTRUDER_ID;
}

int DevNozzle::GetTotalExtruderCount() const
{
    auto rack = m_nozzle_rack.lock();
    if (rack){
        MachineObject* obj = rack->GetNozzleSystem()->GetOwner();
        return obj->GetExtderSystem()->GetTotalExtderCount();
    }
    return 1;
}

wxString DevNozzle::GetSerialNumber() const
{
    if (!m_sn.empty())
    {
        return m_sn;
    }

    return GetFirmwareInfo().sn;
}

void DevNozzle::SetRackNozzleTempStatus(int temp_status)
{
    switch (temp_status) {
    case 0: m_temp_status = DevNozzle::NozzleTempStatus::Idle; break;
    case 1: m_temp_status = DevNozzle::NozzleTempStatus::Heating; break;
    case 2: m_temp_status = DevNozzle::NozzleTempStatus::Keeping; break;
    case 3: m_temp_status = DevNozzle::NozzleTempStatus::Cooling; break;
    default: m_temp_status = DevNozzle::NozzleTempStatus::Idle; break;
    }
}

std::optional<DevAmsTray> DevNozzle::GetSlotNow() const
{
    if (!m_slot_now.has_value())
        return std::nullopt;

    auto rack = m_nozzle_rack.lock();
    if (!rack)
        return std::nullopt;

    MachineObject* obj = rack->GetNozzleSystem()->GetOwner();
    if (!obj)
        return std::nullopt;

    return obj->get_tray(m_slot_now->first, m_slot_now->second);
}

DevNozzleSystem::DevNozzleSystem(MachineObject* owner)
    : m_owner(owner), m_nozzle_rack(std::make_shared<DevNozzleRack> (this))
{
}

std::optional<DevNozzle> DevNozzleSystem::GetNozzleOptByPosId(int pos_id) const
{
    if (pos_id < 0x10) {
        if (m_ext_nozzles.find(pos_id) != m_ext_nozzles.end()) {
            return m_ext_nozzles.at(pos_id);
        }
    } else {
        const auto& rack_nozzles = m_nozzle_rack->GetRackNozzles();
        if (auto iter = rack_nozzles.find(pos_id - 0x10); iter != rack_nozzles.end()) {
            return iter->second;
        }
    }

    return std::nullopt;
}

std::optional<DevNozzle> DevNozzleSystem::GetNozzleOptByAddrId(int addr_id) const
{
    const auto& ext_nozzles = GetExtNozzles();
    for (const auto& [id, nozzle] : ext_nozzles) {
        if (nozzle.GetAddrId().value_or(-1) == addr_id) {
            return nozzle;
        }
    }

    const auto& rack_nozzles = GetRackNozzles();
    for (const auto& [id, nozzle] : rack_nozzles) {
        if (nozzle.GetAddrId().value_or(-1) == addr_id) {
            return nozzle;
        }
    }

    return std::nullopt;
}

DevNozzle DevNozzleSystem::GetExtNozzle(int id) const
{
    if (m_ext_nozzles.find(id) != m_ext_nozzles.end())
    {
        return m_ext_nozzles.at(id);
    }

    return DevNozzle();
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

Slic3r::DevNozzle DevNozzleSystem::GetRackNozzle(int idx) const
{
    return m_nozzle_rack->GetNozzle(idx);
}

const std::map<int, Slic3r::DevNozzle>& DevNozzleSystem::GetRackNozzles() const
{
    return m_nozzle_rack->GetRackNozzles();
}

const std::vector<DevNozzle> DevNozzleSystem::CollectNozzles(int ext_loc, NozzleFlowType flow_type, float diameter) const
{
    auto s_match = [&](const DevNozzle& nozzle) -> bool {
        if (nozzle.IsEmpty() || nozzle.IsAbnormal())   {
            return false;
        }

        if (nozzle.GetToolDefs().tool_type != DevNozzleToolType::Nozzle) {
            return false;
        }

        if (diameter >= 0.0f && nozzle.m_diameter != diameter) {
            return false;
        }

        if (m_owner->is_nozzle_flow_type_supported() && flow_type != nozzle.GetNozzleFlowType()) {
            return false;
        }

        return true;
    };

    std::vector<DevNozzle> result;

    auto ext_nozzle = GetExtNozzle(ext_loc);
    if (s_match(ext_nozzle)){
        result.push_back(ext_nozzle);
    }

    if (ext_loc == MAIN_EXTRUDER_ID) {
        auto rack_nozzles = m_nozzle_rack->GetRackNozzles();
        for (auto rack_nozzle : rack_nozzles){
            if (s_match(rack_nozzle.second)) {
                result.push_back(rack_nozzle.second);
            }
        }
    }

    return result;
}

std::vector<MultiNozzleUtils::NozzleGroupInfo> DevNozzleSystem::GetNozzleGroups() const
{
    std::vector<MultiNozzleUtils::NozzleGroupInfo> nozzle_groups;

    auto nozzle_in_extruder = this->GetExtNozzles();
    for (auto& elem : nozzle_in_extruder) {
        auto& nozzle = elem.second;
        if (!nozzle.IsExist() || nozzle.IsUnknown() || nozzle.IsAbnormal()) continue;
        if (nozzle.GetToolDefs().tool_type != DevNozzleToolType::Nozzle) continue;
        MultiNozzleUtils::NozzleGroupInfo info;
        info.extruder_id = nozzle.GetLogicExtruderId();
        info.diameter = format_diameter_to_str(nozzle.m_diameter);
        info.volume_type = DevNozzle::ToNozzleVolumeType(nozzle.m_nozzle_flow);
        info.nozzle_count = 1;
        nozzle_groups.emplace_back(std::move(info));
    }

    auto nozzle_rack = this->GetNozzleRack();
    if (!nozzle_rack)
        return nozzle_groups;

    auto nozzle_in_rack = nozzle_rack->GetRackNozzles(); // nozzles in rack
    for (auto& elem : nozzle_in_rack) {
        auto& nozzle = elem.second;
        if (!nozzle.IsExist() || nozzle.IsUnknown() || nozzle.IsAbnormal()) continue;
        if (nozzle.GetToolDefs().tool_type != DevNozzleToolType::Nozzle) continue;
        int extruder_id = nozzle.GetLogicExtruderId();
        std::string diameter = format_diameter_to_str(nozzle.m_diameter);
        NozzleVolumeType volume_type = DevNozzle::ToNozzleVolumeType(nozzle.m_nozzle_flow);

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

void DevNozzleSystem::Reset()
{
    m_ext_nozzles.clear();
    m_state_0_4 = 0;
    m_reading_idx = 0;
    m_reading_count = 0;
    m_reading_nozzle_id_flag = 0;

    m_replace_nozzle_src = std::nullopt;
    m_replace_nozzle_tar = std::nullopt;

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
            BOOST_LOG_TRIVIAL(error) << "Invalid nozzle ID in firmware name:  "
                                     << info.name << ", error: " << e.what();
        }
    }
}

void DevNozzleSystem::AddFirmwareInfoHE(const DevFirmwareVersionInfo& info)
{
    static const std::string s_he_prefix = "he";
    if (info.name.find(s_he_prefix) != string::npos) {
        try {
            auto str = info.name.substr(s_he_prefix.size());
            int rack_nozzle_id = std::stoi(str);
            m_nozzle_rack->AddNozzleFirmwareInfo(rack_nozzle_id, info);
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "Invalid nozzle ID in firmware name:  "
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

bool DevNozzleSystem::HasUnreliableNozzles() const
{
    for (auto nozzle : m_ext_nozzles) {
        if (!nozzle.second.IsInfoReliable()) {
            return true;
        }
    }

    if (m_nozzle_rack->HasUnreliableNozzles()) {
        return true;
    }

    return false;
}

bool DevNozzleSystem::HasUnknownNozzles() const
{
    for (auto nozzle : m_ext_nozzles) {
        if (nozzle.second.IsUnknown()) {
            return true;
        }
    }

    if (m_nozzle_rack->HasUnknownNozzles()) {
        return true;
    }

    return false;
}

int DevNozzleSystem::GetKnownNozzleCountOn(int ext_id) const
{
    int count = 0;
    if (ext_id == MAIN_EXTRUDER_ID)         {
        count = m_nozzle_rack->GetKnownNozzleCount();
    }

    const auto& ext_nozzle = GetExtNozzle(ext_id);
    if (!ext_nozzle.IsUnknown() && !ext_nozzle.IsEmpty() && ext_nozzle.GetToolDefs().tool_type == DevNozzleToolType::Nozzle) {
        count++;
    }

    return count;
}

ExtruderNozzleInfos DevNozzleSystem::GetExtruderNozzleInfo() const
{
    ExtruderNozzleInfos result;

    // left
    {
        std::unordered_map<NozzleDef, int> installed_nozzle_map_l;
        const auto& left_nozzle = GetExtNozzle(DEPUTY_EXTRUDER_ID);
        if (!left_nozzle.IsEmpty() && !left_nozzle.IsAbnormal() && left_nozzle.GetToolDefs().tool_type == DevNozzleToolType::Nozzle) {
            NozzleDef data;
            data.nozzle_diameter = left_nozzle.GetNozzleDiameter();
            data.nozzle_flow_type = left_nozzle.GetNozzleFlowType();
            installed_nozzle_map_l[data]++;
        }

        result[DEPUTY_EXTRUDER_ID] = installed_nozzle_map_l;
    }

    // right
    {
        std::unordered_map<NozzleDef, int> installed_nozzle_map_r;
        const auto& r_nozzle = GetExtNozzle(MAIN_EXTRUDER_ID);
        if (!r_nozzle.IsEmpty() && !r_nozzle.IsAbnormal() && r_nozzle.GetToolDefs().tool_type == DevNozzleToolType::Nozzle) {
            NozzleDef data;
            data.nozzle_diameter = r_nozzle.GetNozzleDiameter();
            data.nozzle_flow_type = r_nozzle.GetNozzleFlowType();
            installed_nozzle_map_r[data]++;
        }

        auto rack_nozzles = m_nozzle_rack->GetRackNozzles();
        for (auto rack_nozzle : rack_nozzles)             {
            if (!rack_nozzle.second.IsEmpty() && !rack_nozzle.second.IsAbnormal() && rack_nozzle.second.GetToolDefs().tool_type == DevNozzleToolType::Nozzle) {
                NozzleDef data;
                data.nozzle_diameter = rack_nozzle.second.GetNozzleDiameter();
                data.nozzle_flow_type = rack_nozzle.second.GetNozzleFlowType();
                installed_nozzle_map_r[data]++;
            }
        }

        result[MAIN_EXTRUDER_ID] = installed_nozzle_map_r;
    }

    return result;
}

// for DevNozzleRack::RackType_AB
bool DevNozzleSystem::IsRackMaximumInstalled() const
{
    auto ext_nozzle = GetExtNozzle(MAIN_EXTRUDER_ID);
    if (ext_nozzle.IsEmpty()) {
        return false;
    }

    const auto& rack_type = m_nozzle_rack->GetRackType();
    if (rack_type != DevNozzleRack::RackType_AB) {
        return false;
    }

    const auto& rack_nozzles = m_nozzle_rack->GetRackNozzles();
    if (rack_nozzles.size() < 6) {
        return false;
    }

    for (auto item : rack_nozzles) {
        if (item.second.IsEmpty()) {
            return false;
        }
    }

    return true;
};

static unordered_map<string, NozzleFlowType> _str2_nozzle_flow_type = {
    {"S", NozzleFlowType::S_FLOW},
    {"H", NozzleFlowType::H_FLOW},
    {"A", NozzleFlowType::S_FLOW},
    {"X", NozzleFlowType::S_FLOW},
    {"E", NozzleFlowType::H_FLOW},
    {"U", NozzleFlowType::U_FLOW},
    {"T", NozzleFlowType::T_FLOW},
    {"B", NozzleFlowType::E_FLOW},
};

static unordered_map<string, NozzleType> _str2_nozzle_type = {
    {"00", NozzleType::ntStainlessSteel},
    {"01", NozzleType::ntHardenedSteel},
    {"05", NozzleType::ntTungstenCarbide}
};

static void s_update_nozzle_tool_info(DevNozzle& nozzle)
{
    nozzle.m_tool_defs.nozzle_info.reset();
    if (nozzle.m_tool_defs.tool_type != DevNozzleToolType::Nozzle) {
        return;
    }

    NozzleDef nozzle_def;
    nozzle_def.nozzle_diameter = nozzle.m_diameter;
    nozzle_def.nozzle_flow_type = nozzle.m_nozzle_flow;
    nozzle.m_tool_defs.nozzle_info = nozzle_def;
}

static CutType s_parse_cut_type(const std::string& nozzle_type_str)
{
    if (nozzle_type_str.size() < 2) {
        return CutType::Unknown;
    }

    switch (nozzle_type_str[1]) {
    case '0': return CutType::None;
    case 'A': return CutType::Active;
    case 'P': return CutType::Passive;
    case 'U': return CutType::Ultrasonic;
    case 'E': return CutType::Pen;
    default:  return CutType::Unknown;
    }
}

static void s_parse_nozzle_type(const std::string& nozzle_type_str, DevNozzle& nozzle)
{
    nozzle.m_tool_defs = DevNozzleToolDefs();

    if (NozzleTypeStrToEumn.count(nozzle_type_str) != 0)
    {
        nozzle.m_tool_defs.tool_type = DevNozzleToolType::Nozzle;
        nozzle.m_nozzle_type = NozzleTypeStrToEumn[nozzle_type_str];
    }
    else if (nozzle_type_str == "N/A")
    {
        nozzle.m_tool_defs.tool_type = DevNozzleToolType::Nozzle;// N/A means unknown nozzle by AP
        nozzle.m_nozzle_type = NozzleType::ntUndefine;
        nozzle.m_nozzle_flow = NozzleFlowType::NONE_FLOWTYPE;
    }
    else if (nozzle_type_str.length() >= 4)
    {
        switch (nozzle_type_str[0]) {
        case 'H':
            nozzle.m_tool_defs.tool_type = DevNozzleToolType::Nozzle;
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
                auto diameter_pos = nozzle_type_str.find('-');
                if (diameter_pos != std::string::npos && diameter_pos + 1 < nozzle_type_str.size())
                {
                    float diameter = DevUtil::string_to_float(nozzle_type_str.substr(diameter_pos + 1));
                    if (diameter > 0.0f) {
                        nozzle.m_diameter = round(diameter * 10) / 10;
                    }
                }
            }
            break;
        case 'C':
            nozzle.m_tool_defs.tool_type = DevNozzleToolType::Cut;
            nozzle.m_tool_defs.cut_info = CutDef{ s_parse_cut_type(nozzle_type_str) };
            nozzle.m_nozzle_type = NozzleType::ntUndefine;
            nozzle.m_nozzle_flow = NozzleFlowType::NONE_FLOWTYPE;
            break;
        case 'L':
            nozzle.m_tool_defs.tool_type = DevNozzleToolType::Laser;
            nozzle.m_nozzle_type = NozzleType::ntUndefine;
            nozzle.m_nozzle_flow = NozzleFlowType::NONE_FLOWTYPE;
            break;
        case 'F':
            nozzle.m_tool_defs.tool_type = DevNozzleToolType::Fan;
            nozzle.m_nozzle_type = NozzleType::ntUndefine;
            nozzle.m_nozzle_flow = NozzleFlowType::NONE_FLOWTYPE;
            break;
        default:
            nozzle.m_tool_defs.tool_type = DevNozzleToolType::None;
            nozzle.m_nozzle_type = NozzleType::ntUndefine;
            nozzle.m_nozzle_flow = NozzleFlowType::NONE_FLOWTYPE;
            break;
        }
    }

    s_update_nozzle_tool_info(nozzle);
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
    nozzle.m_is_exist = true;
    nozzle.m_nozzle_id = 0;
    nozzle.m_nozzle_flow = NozzleFlowType::S_FLOW; // default flow type
    nozzle.m_tool_defs.tool_type = DevNozzleToolType::Nozzle;

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
        s_update_nozzle_tool_info(nozzle);
    }

    system->m_ext_nozzles[nozzle.m_nozzle_id] = nozzle;
}



void DevNozzleSystemParser::sTraverseBindAmsSlot(DevNozzleSystem* system, DevNozzle& nozzle_obj)
{
    nozzle_obj.m_bind_ams_slots.clear();

    const auto& fila_sys = system->GetOwner()->GetFilaSystem();
    const auto& ams_list = fila_sys->GetAmsList();
    const auto& vt_list = fila_sys->GetVirtualTrays();
    const auto& rack = system->GetOwner()->GetNozzleRack();
    if (rack->GetRackType() == DevNozzleRack::None) {
        // DO NOTHING
    } else if (rack->IsDirectConnectedRack()) {
        nozzle_obj.m_bind_ams_slots.clear();

        // using the slot now value
        if (nozzle_obj.m_slot_now.has_value() &&  nozzle_obj.IsHotend()) {
            if (system->GetOwner()->get_tray(std::to_string(nozzle_obj.m_slot_now->first), std::to_string(nozzle_obj.m_slot_now->second))) {
                if (!nozzle_obj.IsInBindAms(nozzle_obj.m_slot_now->first)) {
                    nozzle_obj.m_bind_ams_slots = { nozzle_obj.m_slot_now.value() };
                    return;
                } else if (nozzle_obj.IsInBindAms(nozzle_obj.m_slot_now->first) && nozzle_obj.HasFilamentAtNozzleInlet()) {
                    nozzle_obj.m_bind_ams_slots = { nozzle_obj.m_slot_now.value() };
                    return;
                }
            }
        };

        if (nozzle_obj.GetAddrId().has_value()) {
            // using the rack ams slot
            if (nozzle_obj.GetNozzleFlowType() != NozzleFlowType::T_FLOW) {
                for (const auto& ams_item : ams_list) {
                    if (ams_item.second->GetSlotCount() == rack->GetTotalRackNozzleCount()) {
                        DevAmsSlotId ams_slot_id;
                        ams_slot_id.first = ams_item.second->GetAmdIdInt();
                        ams_slot_id.second = nozzle_obj.GetAddrId().value();
                        if (auto ams_tray = fila_sys->GetAmsTray(std::to_string(ams_slot_id.first), std::to_string(ams_slot_id.second))) {
                            nozzle_obj.m_bind_ams_slots = { ams_slot_id };
                            return;
                        }
                    }
                }
            }

            // using the external slot
            int default_exteral_spool_id = 255 - nozzle_obj.GetAddrId().value();
            if (fila_sys->GetVirtualTray(default_exteral_spool_id).has_value()) {
                nozzle_obj.m_bind_ams_slots = { DevAmsSlotId { default_exteral_spool_id, 0 } };
                return;
            }
        }
    }
}

void DevNozzleSystemParser::sTraversePreloadBindAmsSlot(DevNozzleSystem* system, DevNozzle& nozzle_obj)
{
    nozzle_obj.m_preload_bind_ams_slots.clear();

    const auto& fila_sys = system->GetOwner()->GetFilaSystem();
    const auto& ams_list = fila_sys->GetAmsList();
    const auto& rack     = system->GetOwner()->GetNozzleRack();
    if (!rack->IsDirectConnectedRack()) {
        return;
    }

    // Preload only considers rack-matching internal AMS (e.g. ZMULTI).
    // Does NOT use slot_now and does NOT fallback to external spool.
    for (const auto& ams_item : ams_list) {
        if (ams_item.second->GetSlotCount() == rack->GetTotalRackNozzleCount()) {
            DevAmsSlotId ams_slot_id;
            ams_slot_id.first  = ams_item.second->GetAmdIdInt();
            ams_slot_id.second = nozzle_obj.GetNozzlePosId() - 16;
            if (auto ams_tray = fila_sys->GetAmsTray(std::to_string(ams_slot_id.first),
                                                     std::to_string(ams_slot_id.second))) {
                if (!ams_tray->is_exists) continue;
                nozzle_obj.m_preload_bind_ams_slots = { ams_slot_id };
                return;
            }
        }
    }
}

void DevNozzleSystemParser::ParseV2_0(const json& device_json, DevNozzleSystem* system)
{
    if (device_json.contains("holder")) {
        system->m_nozzle_rack->ParseRackInfo(device_json["holder"]);
    }

    if (device_json.contains("nozzle"))
    {
        const json& nozzle_json = device_json["nozzle"];

        std::optional<int> nozzle_exist_bits_opt;
        if (nozzle_json.contains("exist")) {
            nozzle_exist_bits_opt = nozzle_json["exist"].get<int>();
        }

        std::optional<int> hc_bits_opt;
        if (nozzle_json.contains("hc")) {
            hc_bits_opt = nozzle_json["hc"].get<int>();
        }

        if (nozzle_json.contains("state"))
        {
            int val = nozzle_json["state"].get<int>();
            system->m_state_0_4 = DevUtil::get_flag_bits(nozzle_json["state"].get<int>(), 0, 4);
            int new_reading_idx = DevUtil::get_flag_bits(nozzle_json["state"].get<int>(), 8, 4);
            int new_reading_count = DevUtil::get_flag_bits(nozzle_json["state"].get<int>(), 4, 4);
            if (system->m_reading_count != new_reading_count || system->m_reading_idx != new_reading_idx)
            {
                system->m_reading_idx = new_reading_idx;
                system->m_reading_count = new_reading_count;
                if (system->m_reading_count == 0)
                {
                    system->m_nozzle_rack->SendReadingFinished();
                }
            }

            system->m_reading_nozzle_id_flag = DevUtil::get_flag_bits(nozzle_json["state"].get<int>(), 13, 8);
        }

        if (nozzle_json.contains("src_id"))
        {
            system->m_replace_nozzle_src = std::make_optional(nozzle_json["src_id"].get<int>());
        }

        if (nozzle_json.contains("tar_id"))
        {
            system->m_replace_nozzle_tar = std::make_optional(nozzle_json["tar_id"].get<int>());
        }

        system->ClearNozzles();
        for (auto it = nozzle_json["info"].begin(); it != nozzle_json["info"].end(); it++)
        {
            DevNozzle nozzle_obj;
            nozzle_obj.SetRack(system->GetNozzleRack());

            const auto& njon = it.value();
            nozzle_obj.m_nozzle_id = DevUtil::get_hex_bits(njon["id"].get<int>(), 0);
            if (DevUtil::get_hex_bits(njon["id"].get<int>(), 1) == 1) {
                nozzle_obj.SetOnRack(true);
            }

            if (njon.contains("addr_id")) {
                nozzle_obj.m_addr_id = njon["addr_id"].get<int>();
                if (hc_bits_opt.has_value()) {
                    nozzle_obj.m_hc_state = DevUtil::get_flag_bits(hc_bits_opt.value(),  nozzle_obj.m_addr_id.value());
                }
            }

            if (nozzle_exist_bits_opt.has_value()) {
                nozzle_obj.m_is_exist = DevUtil::get_flag_bits(nozzle_exist_bits_opt.value(), njon["id"].get<int>());
            }

            if (njon.contains("diameter")) {
                if (njon["diameter"].is_number()) {
                    nozzle_obj.m_diameter = njon["diameter"].get<float>();
                } else if (njon["diameter"].is_string()) {
                    nozzle_obj.m_diameter = DevUtil::string_to_float(njon["diameter"].get<std::string>());
                }
            } else {
                nozzle_obj.m_diameter = 0.0f;
            }
            s_parse_nozzle_type(njon["type"].get<std::string>(), nozzle_obj);

            DevJsonValParser::ParseVal(njon, "fila_id", nozzle_obj.m_fila_id);
            DevJsonValParser::ParseVal(njon, "wear", nozzle_obj.m_wear);

            if (njon.contains("stat"))/*maybe not contains*/
            {
                nozzle_obj.SetStatus(njon["stat"].get<int>());
                if (system->GetNozzleRack()->IsDirectConnectedRack()) {
                    nozzle_obj.SetRackNozzleTempStatus(DevUtil::get_flag_bits(njon["stat"].get<int>(), 3, 2));
                }

                // traverse the has filament
                if (system->GetNozzleRack()->IsDirectConnectedRack()) {
                    if (DevUtil::get_flag_bits(njon["stat"].get<int>(), 5, 1) == 1 || DevUtil::get_flag_bits(njon["stat"].get<int>(), 5, 1)) {
                        nozzle_obj.m_has_filament_in_nozzle = true;
                    } else if (DevUtil::get_flag_bits(njon["stat"].get<int>(), 6, 3) != 0) {
                        nozzle_obj.m_has_filament_in_nozzle = true;
                    } else {
                        nozzle_obj.m_has_filament_in_nozzle = false;
                    }
                } else if (system->GetNozzleRack()->GetRackType() == DevNozzleRack::RackType_AB) {
                    nozzle_obj.m_has_filament_in_nozzle = !nozzle_obj.m_fila_id.empty() || !nozzle_obj.m_filament_clr.empty();
                } else if (auto ext = system->GetOwner()->GetExtderSystem()->GetExtderById(nozzle_obj.m_nozzle_id); ext) {
                    nozzle_obj.m_has_filament_in_nozzle = ext->HasFilamentInExt();
                } else {
                    nozzle_obj.m_has_filament_in_nozzle = false;
                }
            }

            if (njon.contains("temp"))/*maybe not contains*/
            {
                int current_temp = DevUtil::get_flag_bits(njon["temp"].get<int>(), 0, 16);
                nozzle_obj.SetCurrentTemp(current_temp);

                int target_temp = DevUtil::get_flag_bits(njon["temp"].get<int>(), 16, 16);
                nozzle_obj.SetTargetTemp(target_temp);
            }

            if (njon.contains("p_t"))/*maybe not contains*/
            {
                nozzle_obj.m_nozzle_print_time = njon["p_t"].get<int>();
            }
            if (njon.contains("c_t")) {
                nozzle_obj.m_nozzle_switch_count = njon["c_t"].get<int>();
            }
            if (njon.contains("color_m"))/*maybe not contains*/
            {
                nozzle_obj.m_filament_clr = njon["color_m"].get<std::string>();
            }
            if (njon.contains("sn"))/*maybe not contains*/
            {
                nozzle_obj.m_sn = njon["sn"].get<std::string>();
                auto firm_info = system->GetNozzleRack()->GetNozzleFirmwareInfo(nozzle_obj.m_addr_id.value_or(-1));
                if (!firm_info.sn.empty()) {
                    nozzle_obj.m_sn = firm_info.sn;
                }
            }
            if (njon.contains("snow")) {
                if (DevUtil::get_flag_bits(njon["snow"].get<int>(), 0, 8) != 0xff) {
                    DevAmsSlotId slot_now;
                    slot_now.second = DevUtil::get_flag_bits(njon["snow"].get<int>(), 0, 8);
                    slot_now.first = DevUtil::get_flag_bits(njon["snow"].get<int>(), 8, 8);
                    nozzle_obj.m_slot_now = slot_now;
                } else {
                    nozzle_obj.m_slot_now.reset();
                }
            }

            if (njon.contains("bind_ams")) {
                nozzle_obj.m_bind_ams.clear();
                auto bind_ams_list = njon["bind_ams"].get<std::vector<int>>();
                for (auto ams_id : bind_ams_list) {
                    if (system->GetOwner()->GetFilaSystem()->GetAmsById(std::to_string(ams_id))) {
                        nozzle_obj.m_bind_ams.push_back(ams_id);
                    }
                }
            }

            // traverse bind ams slot after parse
            DevNozzleSystemParser::sTraverseBindAmsSlot(system, nozzle_obj);
            DevNozzleSystemParser::sTraversePreloadBindAmsSlot(system, nozzle_obj);

            if (nozzle_obj.IsOnRack()) {
                system->m_nozzle_rack->AddRackNozzle(nozzle_obj);
            } else {
                system->m_ext_nozzles[nozzle_obj.m_nozzle_id] = nozzle_obj;
            }
        }
    }

    // fill none-exist nozzles
    const auto& rack_nozzle_pos_set = system->m_nozzle_rack->GetTotalRackNozzlePosSet();
    for (const auto& rack_nozzle_pos : rack_nozzle_pos_set) {
        int traverse_addr_id = rack_nozzle_pos - 0x10;
        if (system->GetNozzleOptByAddrId(traverse_addr_id).has_value()) {
            continue;
        }

        const auto& nozzle_opt = system->GetNozzleOptByPosId(rack_nozzle_pos);
        if (!nozzle_opt.has_value()) {
            DevNozzle nozzle_obj;
            nozzle_obj.SetRack(system->GetNozzleRack());
            nozzle_obj.SetOnRack(true);
            nozzle_obj.m_is_exist = false;
            nozzle_obj.m_hc_state = false;
            nozzle_obj.m_nozzle_id = rack_nozzle_pos - 0x10;
            nozzle_obj.m_addr_id = rack_nozzle_pos - 0x10;
            DevNozzleSystemParser::sTraverseBindAmsSlot(system, nozzle_obj);
            DevNozzleSystemParser::sTraversePreloadBindAmsSlot(system, nozzle_obj);
            system->m_nozzle_rack->AddRackNozzle(nozzle_obj);
        }
    }
}

};