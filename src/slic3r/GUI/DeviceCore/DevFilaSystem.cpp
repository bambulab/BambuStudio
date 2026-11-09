#include <nlohmann/json.hpp>
#include "DevExtruderSystem.h"
#include "DevFilaSystem.h"
#include "DevFilaSwitch.h"
#include "DevMcTask.hpp"

// TODO: remove this include
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/I18N.hpp"

#include "DevUtil.h"
#include "DevNozzleSystem.h"
#include "DevNozzleRack.h"
#include "DevMcTask.hpp"

#include "DevUtilBackend.h"

#include <algorithm>
#include "libslic3r/Log.hpp"

using namespace nlohmann;

namespace Slic3r
{

wxColour DevAmsTray::decode_color(const std::string& color)
{
    if (color.empty()) {
        BOOST_LOG_TRIVIAL(error) << "empty";
        return wxColour(255, 255, 255, 255);//default white
    }

    std::string clr_str = color;
    if (clr_str[0] != '#') {
        clr_str = "#" + clr_str;
    }

    const auto& clr = wxColour(clr_str);
    if (clr.IsOk()) {
        return clr;
    }

    BOOST_LOG_TRIVIAL(error) << clr_str;
    return wxColour(255, 255, 255, 255);//default white
}

void DevAmsTray::UpdateColorFromStr(const std::string& color)
{
    if (color.empty()) return;
    if (this->color != color) {
        wx_color = "#" + wxString::FromUTF8(color);
        this->color = color;
    }
}

#if 0
DevAmsTray::DevAmsTray(const DevAmsTray& other)
    : id(other.id)
    , ams_type(other.ams_type)
    , tag_uid(other.tag_uid)
    , setting_id(other.setting_id)
    , filament_setting_id(other.filament_setting_id)
    , m_fila_type(other.m_fila_type)
    , sub_brands(other.sub_brands)
    , color(other.color)
    , cols(other.cols)
    , weight(other.weight)
    , diameter(other.diameter)
    , temp(other.temp)
    , time(other.time)
    , bed_temp_type(other.bed_temp_type)
    , bed_temp(other.bed_temp)
    , nozzle_temp_max(other.nozzle_temp_max)
    , nozzle_temp_min(other.nozzle_temp_min)
    , xcam_info(other.xcam_info)
    , uuid(other.uuid)
    , ctype(other.ctype)
    , k(other.k)
    , n(other.n)
    , cali_idx(other.cali_idx)
    , wx_color(other.wx_color)
    , is_bbl(other.is_bbl)
    , is_exists(other.is_exists)
    , hold_count(other.hold_count)
    , remain(other.remain)
    , current_extruder_id(other.current_extruder_id)
    , binded_extruder_set(other.binded_extruder_set)
    , binded_switcher_pos(other.binded_switcher_pos)
{
}
#endif

void DevAmsTray::reset()
{
    tag_uid = "";
    setting_id = "";
    filament_setting_id = "";
    m_fila_type         = "";
    sub_brands          = "";
    color               = "";
    weight              = "";
    diameter            = "";
    temp                = "";
    time                = "";
    bed_temp_type       = "";
    bed_temp            = "";
    nozzle_temp_max     = "";
    nozzle_temp_min     = "";
    xcam_info           = "";
    uuid                = "";
    tray_id_name        = "";
    k                   = 0.0f;
    n                   = 0.0f;
    is_bbl              = false;
    hold_count          = 0;
    remain              = 0;
    remain_g            = -1;
    m_rfid_refresh_token.reset();
}


bool DevAmsTray::is_tray_info_ready() const
{
    if (color.empty()) return false;
    if (m_fila_type.empty()) return false;
    //if (setting_id.empty()) return false;
    return true;
}

bool DevAmsTray::is_unset_third_filament() const
{
    if (this->is_bbl) return false;
    return (color.empty() || m_fila_type.empty());
}

std::string DevAmsTray::get_display_filament_type() const
{
    if (m_fila_type == "PLA-S") return "Sup.PLA";
    if (m_fila_type == "PA-S") return "Sup.PA";
    if (m_fila_type == "ABS-S") return "Sup.ABS";
    return m_fila_type;
}

std::string DevAmsTray::get_filament_type()
{
    if (m_fila_type == "Sup.PLA") { return "PLA-S"; }
    if (m_fila_type == "Sup.PA") { return "PA-S"; }
    if (m_fila_type == "Sup.ABS") { return "ABS-S"; }
    if (m_fila_type == "Support W") { return "PLA-S"; }
    if (m_fila_type == "Support G") { return "PA-S"; }
    if (m_fila_type == "Support") { if (setting_id == "GFS00") { m_fila_type = "PLA-S"; } else if (setting_id == "GFS01") { m_fila_type = "PA-S"; } else { return "PLA-S"; } }

    return m_fila_type;
}

std::optional<Slic3r::DevFilamentDryingPreset> DevAmsTray::get_ams_drying_preset() const
{
    return DevUtilBackend::GetFilamentDryingPreset(setting_id);
}

std::optional<int> DevAmsTray::get_filament_remain_weight() const
{
    // Prefer the accurate per-gram value reported by firmware; -1 means not edited/not provided.
    if (remain_g >= 0) {
        return remain_g > 0 ? std::optional<int>(remain_g) : std::nullopt;
    }

    if (weight.empty()) {
        return std::nullopt;
    }

    std::optional<int> weight_int;
    try {
        weight_int = stoi(weight) * remain / 100;
    } catch (...) {
        weight_int = std::nullopt;
        BOOST_LOG_TRIVIAL(error) << "invalid filament weight " << weight;
    }

    if (weight_int.has_value() && weight_int.value() > 0) {
        return weight_int;
    } else {
        return std::nullopt;
    }
}

std::optional<int> DevAmsTray::get_nozzle_addr_id(MachineObject* obj) const
{
    if (this->id.empty() || !obj) {
        return std::nullopt;
    }

    try {
        if (obj->GetNozzleRack()->IsDirectConnectedRack()) {
            if (ams_type == DevAmsType::ZMULTI_A || ams_type == DevAmsType::ZMULTI_B) {
                return std::stoi(this->id);
            } else if (ams_type == DevAmsType::EXT_SPOOL) {
                return 255 - std::stoi(this->id);
            } else {
                const auto& nozzle_map = obj->GetNozzleSystem()->GetRackNozzles();
                for (const auto& [nozzle_id, nozzle] : nozzle_map) {
                    const auto& bind_ams_list = nozzle.GetBindAms();
                    for (const auto& bind_ams : bind_ams_list) {
                        if (std::to_string(bind_ams) == ams_id) {
                            return nozzle.GetAddrId();
                        }
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "[error]: exception=" << e.what();
    };

    return std::nullopt;
}

std::optional<int> DevAmsTray::get_nozzle_pos_id(MachineObject* obj) const
{
    if (this->id.empty() || !obj) {
        return std::nullopt;
    }

    try {
        if (obj->GetNozzleRack()->IsDirectConnectedRack()) {
            if (ams_type == DevAmsType::ZMULTI_A || ams_type == DevAmsType::ZMULTI_B) {
                return std::stoi(this->id) + 16;
            } else if (ams_type == DevAmsType::EXT_SPOOL) {
                return 255 - std::stoi(this->id) + 16;
            } else {
                const auto& nozzle_map = obj->GetNozzleSystem()->GetRackNozzles();
                for (const auto& [nozzle_id, nozzle] : nozzle_map) {
                    const auto& bind_ams_list = nozzle.GetBindAms();
                    for (const auto& bind_ams : bind_ams_list) {
                        if (std::to_string(bind_ams) == ams_id) {
                            return nozzle.GetNozzlePosId();
                        }
                    }
                }

                return std::nullopt;//TODO
            }
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "[error]: exception=" << e.what();
    };

    int total_extruder_count = obj->GetExtderSystem()->GetTotalExtderCount();
    if (total_extruder_count == 1) {
        return MAIN_EXTRUDER_ID;
    } else if (total_extruder_count == 2) {
        if (this->id == VIRTUAL_AMS_MAIN_ID_STR) {
            return MAIN_EXTRUDER_ID;
        } else if (this->id == VIRTUAL_AMS_DEPUTY_ID_STR) {
            return DEPUTY_EXTRUDER_ID;
        }
    }

    return std::nullopt;
}

std::optional<DevNozzle> DevAmsTray::get_nozzle(MachineObject* obj) const
{
    if (auto addr_id = get_nozzle_addr_id(obj); addr_id.has_value()) {
        return obj->GetNozzleSystem()->GetNozzleOptByAddrId(*addr_id);
    } else if (auto pos_id = get_nozzle_pos_id(obj); pos_id.has_value()) {
        return obj->GetNozzleSystem()->GetNozzleOptByPosId(*pos_id);
    }

    return std::nullopt;
}

std::string DevAmsTray::get_slot_name(MachineObject* obj) const
{
    int slot_id = 0;
    try {
        slot_id = std::stoi(this->id);
    } catch (const std::exception& e){
        BOOST_LOG_TRIVIAL(error) << "[error] e=" << e.what();
        return "";
    }

    if (ams_type != DevAmsType::EXT_SPOOL) {
        int ams_count = 0;
        const auto& ams_list = obj->GetFilaSystem()->GetAmsList();
        for (const auto& [ams_item_id, item] : ams_list) {
            if (ams_item_id == ams_id) {
                break;
            }

            ams_count++;
        }

        char val_char = 'A' + ams_count;
        if (ams_type == DevAmsType::N3S) {
            int ht_index = 0;
            try {
                ht_index = std::stoi(ams_id) - 128;
            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << "[error] e=" << e.what();
            }
            return std::string("HT-") + static_cast<char>('A' + ht_index);
        } else if (ams_type == DevAmsType::ZMULTI_A) {
            return wxString::Format("%d", slot_id + 1).ToStdString();
        } else if (ams_type == DevAmsType::ZMULTI_B) {
            return wxString::Format("%d", slot_id + 1).ToStdString();
        } else {
            return val_char + wxString::Format("%d", slot_id + 1).ToStdString();
        }
    } else {
        int total_extruder_count = obj->GetExtderSystem()->GetTotalExtderCount();
        int total_external_count = obj->GetFilaSystem()->GetVirtualTrays().size();
        if (total_external_count > total_extruder_count) {
            return wxString::Format("%d-Ext", 256 - slot_id).ToStdString();// index first
        }

        if (total_external_count == 2) {
            if (this->id == VIRTUAL_AMS_MAIN_ID_STR) {
                return "Ext-R";
            } else if (this->id == VIRTUAL_AMS_DEPUTY_ID_STR) {
                return "Ext-L";
            }
        }

        return "Ext";
    }

    return "";

}

DevAmsSlotId DevAmsTray::get_ams_slot_id() const
{
    DevAmsSlotId ams_slot_id;
    ams_slot_id.first = 0;
    ams_slot_id.second = 0;

    try {
        ams_slot_id.first = std::stoi(this->ams_id);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "[error] e=" << e.what();
    }

    if (ams_type == DevAmsType::EXT_SPOOL) {
        ams_slot_id.second = 0;
    } else {
        try {
            ams_slot_id.second = std::stoi(id);
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "[error] e=" << e.what();
        }
    }

    return ams_slot_id;
}

long long DevAmsTray::get_tray_flag_bit() const {
    auto ams_slot_id = get_ams_slot_id();
    if (ams_type == DevAmsType::AMS || ams_type == DevAmsType::AMS_LITE || ams_type == DevAmsType::N3F) {
        return ams_slot_id.first * 4 + ams_slot_id.second;
    } else if (ams_type == DevAmsType::N3S) {
        return 16 + (ams_slot_id.first - 128) + ams_slot_id.second;
    } else if (ams_type == DevAmsType::AMS_LITE_MIXED) {
        return 24 + ams_slot_id.second;
    } else if (ams_type == DevAmsType::ZMULTI_A) {
        return 28 + ams_slot_id.second;
    } else if (ams_type == DevAmsType::ZMULTI_B) {
        return 37 + ams_slot_id.second;
    }

    return -1;
}

bool DevAmsTray::is_reading(MachineObject* obj) const
{
    if (obj) {
        if (CanReadExternalRFID_TPU(obj)) {
            if (auto nozzle_addr_opt = get_nozzle_addr_id(obj); nozzle_addr_opt.has_value()) {
                auto nozzle_opt = obj->GetNozzleSystem()->GetNozzleOptByAddrId(*nozzle_addr_opt);
                if (nozzle_opt.has_value() && nozzle_opt->IsExist() && nozzle_opt->IsHotend() && nozzle_opt->GetNozzleFlowType() == NozzleFlowType::T_FLOW) {
                    auto related_ams_slot_opt = nozzle_opt->GetRelatedAmsSlot();
                    if (related_ams_slot_opt.has_value()) {
                        if (auto tray_opt = obj->get_tray(related_ams_slot_opt->first, related_ams_slot_opt->second); tray_opt.has_value() && tray_opt->is_reading(obj)) {
                            return true;
                        }
                    }
                };
            }
        }

        if (m_rfid_refresh_token) {
            const auto status = m_rfid_refresh_token->GetStatus();
            if (status == DevCtrlTokenStatus::SendSucceeded || status == DevCtrlTokenStatus::WaitingReply) {
                return true;
            }
        }

        return DevUtil::get_flag_bits(obj->tray_reading_bits, get_tray_flag_bit());
    }

    return false;
}

bool DevAmsTray::is_reading_done(MachineObject* obj) const
{
    if (obj) {
        if (CanReadExternalRFID_TPU(obj)) {
            if (auto nozzle_addr_opt = get_nozzle_addr_id(obj); nozzle_addr_opt.has_value()) {
                auto nozzle_opt = obj->GetNozzleSystem()->GetNozzleOptByAddrId(*nozzle_addr_opt);
                if (nozzle_opt.has_value() && nozzle_opt->IsExist() && nozzle_opt->IsHotend() && nozzle_opt->GetNozzleFlowType() == NozzleFlowType::T_FLOW) {
                    auto related_ams_slot_opt = nozzle_opt->GetRelatedAmsSlot();
                    if (related_ams_slot_opt.has_value()) {
                        if (auto tray_opt = obj->get_tray(related_ams_slot_opt->first, related_ams_slot_opt->second); tray_opt.has_value() && tray_opt->is_reading_done(obj)) {
                            return true;
                        }
                    }
                };
            }
        }

        return DevUtil::get_flag_bits(obj->tray_read_done_bits, get_tray_flag_bit());
    }

    return false;
}

bool DevAmsTray::WaitReadExternalRFID_TPU(MachineObject* obj) const
{
    if (obj && ams_type == DevAmsType::EXT_SPOOL && (setting_id.empty() || m_fila_type.empty())) {
        if (auto nozzle_addr_opt = get_nozzle_addr_id(obj); nozzle_addr_opt.has_value()) {
            auto nozzle_opt = obj->GetNozzleSystem()->GetNozzleOptByAddrId(*nozzle_addr_opt);
            if (nozzle_opt.has_value() && nozzle_opt->IsExist() && nozzle_opt->IsHotend() && nozzle_opt->GetNozzleFlowType() == NozzleFlowType::T_FLOW) {
                for (const auto& [_, ams] : obj->GetFilaSystem()->GetAmsList()) {
                    if (ams && ams->SupportsReadExtRfid()) {
                        return true;
                    }
                }
            };
        }
    }

    return false;
}

bool DevAmsTray::CanReadExternalRFID_TPU(MachineObject* obj) const
{
    if (obj && ams_type == DevAmsType::EXT_SPOOL) {
        if (auto nozzle_addr_opt = get_nozzle_addr_id(obj); nozzle_addr_opt.has_value()) {
            auto nozzle_opt = obj->GetNozzleSystem()->GetNozzleOptByAddrId(*nozzle_addr_opt);
            if (nozzle_opt.has_value() && nozzle_opt->IsExist() && nozzle_opt->IsHotend() && nozzle_opt->GetNozzleFlowType() == NozzleFlowType::T_FLOW) {
                for (const auto& [_, ams] : obj->GetFilaSystem()->GetAmsList()) {
                    if (ams && ams->SupportsReadExtRfid()) {
                        return true;
                    }
                }
            };
        }
    }

    return false;
}

bool DevAmsTray::CanResetRFID_FilamentInfo(MachineObject* obj) const {
    if (ams_type == DevAmsType::EXT_SPOOL) {
        return true;
    } else if (ams_type == DevAmsType::ZMULTI_B) {
        return true;
    }

    return false;
}

DevAms::DevAms(std::shared_ptr<DevFilaSystem> owner, const std::string& ams_id, const std::set<int>& binded_extruder_set, DevAmsType type)
{
    m_fila_system = owner;
    m_ams_id = ams_id;
    m_ams_type = type;
    m_binded_extruder_set = binded_extruder_set;
}

DevAms::~DevAms()
{
    for (auto it = m_trays.begin(); it != m_trays.end(); it++) {
        if (it->second) {
            delete it->second;
            it->second = nullptr;
        }
    }
    m_trays.clear();
}

std::optional<int> DevAms::GetUniqueBindedExtruderId() const
{
    if (m_binded_extruder_set.size() == 1) {
        return *m_binded_extruder_set.begin();
    }

    return std::nullopt;
}

std::optional<int> DevAms::GetCurrentExtruderId() const
{
    if (m_binded_extruder_set.size() == 1) {
        return *m_binded_extruder_set.begin();
    }

    if (auto fila_sys = m_fila_system.lock()) {
        const auto& extruder_map = fila_sys->GetOwner()->GetExtderSystem()->GetExtruders();
        for (const auto& extruder : extruder_map) {
            if (extruder.GetSlotNow().ams_id == m_ams_id) {
                return extruder.GetExtId();
            }
        }
    }

    return std::nullopt;
}

int DevAms::GetAmdIdInt() const
{
    try {
        return std::stoi(m_ams_id);
    } catch (...) {
        BOOST_LOG_TRIVIAL(warning) << current_exception_message();
    }

    return 0;
}

static unordered_map<DevAmsType, wxString> s_ams_display_formats = {
    {DevAmsType::AMS,      "AMS(%d)"},
    {DevAmsType::AMS_LITE, "AMS Lite(%d)"},
    {DevAmsType::N3F,      "AMS 2 Pro(%d)"},
    {DevAmsType::N3S,      "AMS HT(%d)"},
    {DevAmsType::ZMULTI_A, "AMS-3H"},
    {DevAmsType::ZMULTI_B, "AMS-B"},
};

wxString DevAms::GetDisplayName() const
{
    wxString ams_display_format;
    auto iter = s_ams_display_formats.find(GetAmsType());
    if (iter != s_ams_display_formats.end()) {
        ams_display_format = iter->second;
    } else {
        assert(0 && __FUNCTION__);
        ams_display_format = "AMS(%d)";
    }

    int num_id;
    try {
        num_id = std::stoi(GetAmsId());
    } catch (const std::exception& e) {
        assert(0 && __FUNCTION__);
        BOOST_LOG_TRIVIAL(error) << "Invalid AMS ID: " << GetAmsId() << ", error: " << e.what();
        num_id = 0;
    }
    int loc = num_id + 1;
    if (num_id > 127) {
        loc = num_id - 127;
    } else if (num_id >= 0x10 && num_id <= 0x1f) {
        loc = num_id - 15;
    }
    return wxString::Format(ams_display_format, loc);
}

int DevAms::GetSlotCount() const
{
    auto ams_type = GetAmsType();
    if (ams_type == DevAmsType::AMS || ams_type == DevAmsType::AMS_LITE || ams_type == DevAmsType::N3F) {
        return 4;
    } else if (ams_type == DevAmsType::N3S) {
        return 1;
    }

    if (!m_trays.empty()) {
        return m_trays.size();
    }

    return 1;
}

DevAmsTray* DevAms::GetTray(const std::string& tray_id) const
{
    auto it = m_trays.find(tray_id);
    if (it != m_trays.end()) {
        return it->second;
    }

    return nullptr;
}

std::optional<DevAms::DevAmsZone>
DevAms::GetZone(int zone_id) const
{
    auto it = m_zones.find(zone_id);
    return it == m_zones.end() ? std::optional<DevAms::DevAmsZone>() : it->second;
}

std::optional<DevAms::DevAmsZone>
DevAms::GetZoneBySlotId(int slot_id) const
{
    for (const auto& [zone_id, zone] : m_zones) {
        if (zone.zone_slots.count(slot_id) != 0) {
            return zone;
        }
    }

    return std::nullopt;
}


int DevAms::GetTrayId(int slot_id) const
{
    int ams_id = 0;
    try {
        ams_id = std::stoi(m_ams_id);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "invalid ams_id " << m_ams_id << ", error: " << e.what();
        return -1;
    }

    if (m_ams_type == DevAmsType::AMS || m_ams_type == DevAmsType::AMS_LITE || m_ams_type == DevAmsType::N3F) {
        return ams_id * 4 + slot_id;
    } else if (m_ams_type == DevAmsType::N3S) {
        return 16 + (ams_id - 128) + slot_id;
    } else if (m_ams_type == DevAmsType::AMS_LITE_MIXED) {
        return 24 + slot_id;
    } else if (m_ams_type == DevAmsType::ZMULTI_A) {
        return 28 + slot_id;
    } else if (m_ams_type == DevAmsType::ZMULTI_B) {
        return 37 + slot_id;
    } else {
        assert(0);
        BOOST_LOG_TRIVIAL(error) << m_ams_type;
    }

    return -1;
}


bool DevAms::IsExist() const
{
    auto fila_sys = m_fila_system.lock();
    if (!fila_sys) {
        return false;
    }

    auto obj_ = fila_sys->GetOwner();
    try {
        int ams_id_int = atoi(m_ams_id.c_str());
        if (m_ams_type == DevAmsType::AMS ||
            m_ams_type == DevAmsType::AMS_LITE ||
            m_ams_type == DevAmsType::N3F) {
            return DevUtil::get_flag_bits(obj_->ams_exist_bits, ams_id_int);
        } else if (m_ams_type == DevAmsType::N3S) {
            return DevUtil::get_flag_bits(obj_->ams_exist_bits, 4 + (ams_id_int - 128));
        } else if (m_ams_type == DevAmsType::AMS_LITE_MIXED) {
            return DevUtil::get_flag_bits(obj_->ams_exist_bits, 12);
        } else if (m_ams_type == DevAmsType::ZMULTI_A) {
            return DevUtil::get_flag_bits(obj_->ams_exist_bits, 13);
        } else if (m_ams_type == DevAmsType::ZMULTI_B) {
            return DevUtil::get_flag_bits(obj_->ams_exist_bits, 14);
        } else {
            return DevUtil::get_flag_bits(obj_->ams_exist_bits, 4 + (ams_id_int - 128));
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "[error]: invalid " << KV(log_key::ams_id, m_ams_id) << "error: " << e.what();
    }

    return true;
}


bool DevAms::IsAmsSlotPreLoadFilament(int slot_id) const
{
    auto fila_sys = m_fila_system.lock();
    if (!fila_sys) {
        return false;
    }

    auto obj_ = fila_sys->GetOwner();
    if (!obj_) {
        return false;
    }

    int bit = GetTrayId(slot_id);
    if (bit < 0) {
        return false;
    }

    return DevUtil::get_flag_bits(obj_->tray_exist_bits, bit) != 0;
}

bool DevAms::SupportHumidityPercent() const
{
    return (m_ams_type == DevAmsType::N3F) || (m_ams_type == DevAmsType::N3S) || (m_ams_type == DevAmsType::ZMULTI_A);
}

bool DevAms::SupportDrying() const
{
    if (m_ams_type == DevAmsType::N3F || m_ams_type == DevAmsType::N3S) {
        return true;
    }
    else if (m_ams_type == DevAmsType::ZMULTI_A || m_ams_type == DevAmsType::ZMULTI_B) {
        for (auto [_, zone] : m_zones) {
            if (zone.SupportDrying()) { return true; }
        }
    }
    return false;
}

bool DevAms::IsSupportRemoteDry(const MachineObject* obj) const
{
    if (obj && obj->is_support_remote_dry) {
        return SupportDrying();
    }

    return false;
}

bool DevAms::IsDryableSlot(int slot_id) const
{
    if (m_zones.empty()) {
        return SupportDrying();
    }

    auto zone = GetZoneBySlotId(slot_id);
    return zone && zone->SupportDrying();
}

std::vector<int> DevAms::GetDryableZoneIds() const
{
    std::vector<int> zone_ids;
    for (const auto& [zone_id, zone] : m_zones) {
        if (zone.SupportDrying()) {
            zone_ids.push_back(zone_id);
        }
    }
    return zone_ids;
}

bool DevAms::AmsIsDrying()
{
    if (!GetDryStatus().has_value()) {
        return false;
    }

    return GetDryStatus().value() == DevAms::DryStatus::Checking
        || GetDryStatus().value() == DevAms::DryStatus::Drying
        || GetDryStatus().value() == DevAms::DryStatus::Error
        || GetDryStatus().value() == DevAms::DryStatus::CannotStopHeatOutofControl;
}

DevFilaSystem::~DevFilaSystem()
{
    if (m_ams_rfid_refresh_ctrl)
        m_ams_rfid_refresh_ctrl->Detach();
    m_ams_rfid_refresh_ctrl.reset();

    for (auto it = amsList.begin(); it != amsList.end(); it++) {
        if (it->second) {
            delete it->second;
            it->second = nullptr;
        }
    }
    amsList.clear();
}

void DevFilaSystem::reset()
{
    m_ams_system_setting.ResetFilamentTabAutoReadRfidState();
    if (m_ams_rfid_refresh_ctrl)
        m_ams_rfid_refresh_ctrl->Detach();
    m_ams_rfid_refresh_ctrl = DevAmsRfidRefreshCtrl::Create(this);
}

DevAms* DevFilaSystem::GetAmsById(const std::string& ams_id) const
{
    if (ams_id.empty()) {
        return nullptr;
    }

    auto it = amsList.find(ams_id);
    if (it != amsList.end()) {
        return it->second;
    }

    return nullptr;
}

std::optional<DevAmsZone>
DevFilaSystem::GetZoneBySlot(int ams_id, int slot_id) const
{
    if (auto ams_item = GetAmsById(std::to_string(ams_id))) {
        return ams_item->GetZoneBySlotId(slot_id);
    }

    return std::nullopt;
}

std::optional<DevAmsTray> DevFilaSystem::GetTray(int ams_id, int slot_id) const
{
    if (auto item = GetAmsTray(std::to_string(ams_id), std::to_string(slot_id))) {
        return *item;
    }

    if (auto item = GetVirtualTray(ams_id)) {
        return item;
    }

    return GetVirtualTray(slot_id);
}

DevAmsTray* DevFilaSystem::GetAmsTray(const std::string& ams_id, const std::string& tray_id) const
{
    auto it = amsList.find(ams_id);
    if (it == amsList.end()) return nullptr;
    if (!it->second) return nullptr;
    return it->second->GetTray(tray_id);
}

std::optional<DevAmsTray> DevFilaSystem::GetVirtualTray(int virtual_tray_id) const
{
    auto it = m_vt_trays.find(std::to_string(virtual_tray_id));
    if (it != m_vt_trays.end()) {
        return it->second;
    }

    return std::nullopt;
}

std::optional<DevAmsTray> DevFilaSystem::GetVirtualTray(const std::string& virtual_tray_id) const
{
    auto it = m_vt_trays.find(virtual_tray_id);
    if (it != m_vt_trays.end()) {
        return it->second;
    }

    return std::nullopt;
}

DevAmsTray* DevFilaSystem::GetVirtualTrayPtr(const std::string& virtual_tray_id)
{
    auto it = m_vt_trays.find(virtual_tray_id);
    return it != m_vt_trays.end() ? &it->second : nullptr;
}

bool DevFilaSystem::UpdateVirtualTray(const std::string& virtual_tray_id, const DevAmsTray& tray)
{
    auto it = m_vt_trays.find(virtual_tray_id);
    if (it == m_vt_trays.end()) {
        BOOST_LOG_TRIVIAL(warning) << "virtual tray " << virtual_tray_id << " not found";
        return false;
    }

    it->second = tray;
    return true;
}

void DevFilaSystem::CollectAmsColors(std::vector<wxColour>& ams_colors) const
{
    ams_colors.clear();
    ams_colors.reserve(amsList.size());
    for (auto ams = amsList.begin(); ams != amsList.end(); ams++) {
        for (auto tray = ams->second->GetTrays().begin(); tray != ams->second->GetTrays().end(); tray++) {
            if (tray->second->is_tray_info_ready()) {
                auto ams_color = DevAmsTray::decode_color(tray->second->color);
                ams_colors.emplace_back(ams_color);
            }
        }
    }
}

std::map<int, DevAmsSlotId> DevFilaSystem::GetTrayIndexMap()
{
    std::map<int, DevAmsSlotId> tray_id_map;
    for (auto& [virtual_tray_id, virtual_tray_item] : GetVirtualTrays()) {
        try {
            int virtual_tray_id_int = stoi(virtual_tray_id);
            tray_id_map[virtual_tray_id_int] = DevAmsSlotId{ virtual_tray_id_int, 0 };
        } catch (...) {
            BOOST_LOG_TRIVIAL(warning) << "invalid virtual tray id: " << virtual_tray_id;
        }
    }

    for (auto& [ams_id, ams_item] : GetAmsList()) {
        for (auto& [slot_id, slot_item] : ams_item->GetTrays()) {
            if (ams_item && slot_item) {
                try {
                    int ams_id_int = stoi(ams_id);
                    int slot_id_int = stoi(slot_id);
                    int tray_index = -1;
                    if (ams_item->GetAmsType() == DevAmsType::N3S) {
                        tray_index = ams_id_int;
                    } else if (ams_item->GetAmsType() == DevAmsType::AMS_LITE && ams_item->IsAmsLiteMixed()) {
                        tray_index = 24 + slot_id_int;
                    } else if (ams_item->GetAmsType() == DevAmsType::ZMULTI_A) {
                        tray_index = 28 + slot_id_int;
                    } else if (ams_item->GetAmsType() == DevAmsType::ZMULTI_B) {
                        tray_index = 37 + slot_id_int;
                    } else {
                        tray_index = (ams_id_int * 4 + slot_id_int);
                    }
                    tray_id_map[tray_index] = { ams_id_int, slot_id_int };
                } catch (...) {
                    BOOST_LOG_TRIVIAL(warning) << "invalid ams id: " << ams_id << " or slot id: " << slot_id;
                }
            }
        }
    }

    return  tray_id_map;
}

int DevFilaSystem::GetTrayIdByAmsSlotId(int ams_id, int slot_id)
{
    auto tray_ams_slot_map = GetTrayIndexMap();
    auto tray_it = std::find_if(tray_ams_slot_map.begin(), tray_ams_slot_map.end(),
                                [ams_id, slot_id](auto& item) {
        return ams_id == item.second.first && slot_id == item.second.second;
    });

    if (tray_it != tray_ams_slot_map.end())
        return tray_it->first;
    else
        return -1;
}

std::string DevFilaSystem::GetTrayNameByTrayId(int tray_id)
{
    std::string tray_name;

    auto tray_ams_slot_map = GetTrayIndexMap();

    if (IsVirtualTrayId(std::to_string(tray_id))) {
        tray_name = "Ext";
    } else if (tray_ams_slot_map.find(tray_id) != tray_ams_slot_map.end()) {
        int  ams_id = tray_ams_slot_map[tray_id].first;
        int slot_id = tray_ams_slot_map[tray_id].second;

        if (ams_id >= 128 && ams_id < 153) {
            char prefix = 'A' + ams_id - 128;
            tray_name   = std::string(1, prefix);
        } else {
            char prefix = 'A' + ams_id;
            char suffix = '0' + 1 + slot_id;
            tray_name = std::string(1, prefix) + std::string(1, suffix);
        }
    }

    MachineObject* obj  = GetOwner();
    if (!obj) return tray_name;

    if (obj->GetNozzleRack()->IsDirectConnectedRack()) {
        auto it = tray_ams_slot_map.find(tray_id);
        if (it != tray_ams_slot_map.end()) {
            if (auto tray = obj->get_tray(it->second.first, it->second.second); tray.has_value()) {
                auto addr_id = tray->get_nozzle_addr_id(obj);
                if (addr_id.has_value()) {
                    if (tray->ams_type == DevAmsType::ZMULTI_A || tray->ams_type == DevAmsType::ZMULTI_B) {
                        tray_name = std::to_string(addr_id.value() + 1);
                    } else {
                        tray_name = std::to_string(addr_id.value() + 1) + "-" + tray_name;
                    }
                }
            }
        }
    }

    return tray_name;
}


std::string DevFilaSystem::GetTrayNameByAmsSlotId(const std::string& dev_id, int ams_id, int slot_id, DevAmsType ams_type)
{
    if (ams_type == DevAmsType::N3S) {
        const char base = 'A' + (ams_id - 128);
        wxString tray_name("HT-");
        tray_name.append(base);
        return tray_name.ToStdString();
    } else if (ams_type == DevAmsType::ZMULTI_A || ams_type == DevAmsType::ZMULTI_B) {
        return wxString::Format("%d", slot_id + 1).ToStdString();
    } else if (ams_type == DevAmsType::AMS_LITE_MIXED) {
        return wxString::Format("Q%d", slot_id + 1).ToStdString();
    } else if (ams_type == DevAmsType::EXT_SPOOL) {
        int nozzle_count = DevPrinterConfigUtil::get_default_nozzle_count(DevPrinterConfigUtil::get_printer_type_by_dev_id(dev_id));
        if (nozzle_count == 1) {
            return "Ext";
        } else if (nozzle_count == 2) {
            if (ams_id == VIRTUAL_TRAY_MAIN_ID) {
                return "Ext-R";
            } else {
                return "Ext-L";
            }
        } else if (nozzle_count > 2) {
            return wxString::Format("Ext-%d", 256 - slot_id).ToStdString();
        }

        return "Ext";
    } else {
        wxString tray_name;
        const char base = 'A' + ams_id;
        tray_name.append(base);
        tray_name.append(std::to_string(slot_id + 1));
        return tray_name.ToStdString();
    }
}

int DevFilaSystem::GetExtruderIdByAmsId(const std::string& ams_id) const
{
    auto it = amsList.find(ams_id);
    if (it != amsList.end()) {
        if (it->second->GetBindedExtruderSet().size() == 1) {
            return *it->second->GetBindedExtruderSet().begin();
        }
    } else if(IsVirtualTrayId(ams_id) && GetOwner()->GetNozzleRack()->IsDirectConnectedRack()) {
        return MAIN_EXTRUDER_ID;
    } else if (ams_id == VIRTUAL_AMS_MAIN_ID_STR) {
        return MAIN_EXTRUDER_ID;
    } else if (ams_id == VIRTUAL_AMS_DEPUTY_ID_STR) {
        return DEPUTY_EXTRUDER_ID;
    }
    BOOST_LOG_TRIVIAL(warning) << "ams_id " << ams_id << " not found";
    return -1; // not found
}

std::string DevFilaSystem::GetNozzleFlowStringByAmsId(const std::string& ams_id) const
{
    auto extuder_id = GetExtruderIdByAmsId(ams_id);
    auto nozzle = GetOwner()->GetNozzleSystem()->GetExtNozzle(extuder_id);
    return nozzle.GetNozzleFlowTypeString(nozzle.GetNozzleFlowType());
}

bool DevFilaSystem::IsAmsSettingUp() const
{
    int setting_up_stat = DevUtil::get_flag_bits(m_ams_cali_stat, 0, 8);
    if (setting_up_stat == 0x01 || setting_up_stat == 0x02 || setting_up_stat == 0x03 || setting_up_stat == 0x04) {
        return true;
    }

    return false;
}

DevAmsTaskRunStatus DevFilaSystem::GetAmsTaskRunStatus() const
{
    return static_cast<DevAmsTaskRunStatus>(DevUtil::get_flag_bits(m_ams_stat, 0, 4));
}

bool DevFilaSystem::IsBBL_Filament(std::string tag_uid)
{
    if (tag_uid.empty()) {
        return false;
    }

    for (int i = 0; i < tag_uid.length(); i++) {
        if (tag_uid[i] != '0') { return true; }
    }

    return false;
}

bool DevFilaSystem::CanShowFilamentBackup() const
{
    return m_auto_refill_mgr.CanShow();
}

bool DevFilaSystem::IsDryAreaInPrintUse(const std::string& ams_id, std::optional<int> zone_id) const
{
    if (!m_owner || !m_owner->is_in_printing()) {
        return false;
    }

    const DevAms* ams = GetAmsById(ams_id);
    if (!ams) {
        return false;
    }

    auto is_in_dry_area = [ams, zone_id](int slot_id) {
        if (!ams->IsDryableSlot(slot_id)) {
            return false;
        }
        if (!zone_id) {
            return true;
        }
        const auto zone = ams->GetZoneBySlotId(slot_id);
        return zone && zone->zone_id == *zone_id;
    };

    // new firmware reports tray print_status directly
    bool has_print_status = false;
    for (const auto& [tray_id, tray] : ams->GetTrays()) {
        if (!tray || !tray->print_status.has_value()) {
            continue;
        }
        has_print_status = true;
        try {
            if (*tray->print_status != DevAmsTray::PrintStatus::Idle && is_in_dry_area(std::stoi(tray_id))) {
                return true;
            }
        } catch (...) {
            BOOST_LOG_TRIVIAL(warning) << "invalid " << KV(log_key::ams_id, ams_id) << " " << KV(log_key::tray_id, tray_id);
        }
    }
    // old firmware without tray print_status: any printing counts as in use
    return !has_print_status;
}

bool DevFilaSystem::HasSubAms() const
{
    auto* nozzle_sys = m_owner ? m_owner->GetNozzleSystem() : nullptr;
    if (!nozzle_sys) {
        return false;
    }

    auto has_bind_ams = [](const std::map<int, DevNozzle>& nozzles) {
        for (const auto& kv : nozzles) {
            if (!kv.second.GetBindAms().empty()) {
                return true;
            }
        }
        return false;
    };

    return has_bind_ams(nozzle_sys->GetRackNozzles()) || has_bind_ams(nozzle_sys->GetExtNozzles());
}


std::optional<DevFilamentStep> DevFilaSystem::GetCurrentFilamentChangeStep() const
{
    if (auto ext_opt = m_owner->GetExtderSystem()->GetLoadingExtder()) {
        return ext_opt->GetCurrentFilamentChangeStep();
    };

    return std::nullopt;
}
std::vector<DevFilamentStep> DevFilaSystem::GetCurrentFilamentChangeSteps() const
{
    if (m_owner->GetMcTask().IsValid() && m_filament_change_steps_multi.size() > m_owner->GetMcTask().GetSubStep() && m_filament_change_steps_multi.size() > 1) {
        return m_filament_change_steps_multi[m_owner->GetMcTask().GetSubStep()];
    };

    return m_filament_change_steps;
}

std::optional<DevAmsSlotId> DevFilaSystem::DecodeFilamentChangingSlot(int encoded_slot) const
{
    if (encoded_slot < 0) {
        return std::nullopt;
    }

    try {
        const int tar_first = DevUtil::get_flag_bits(encoded_slot, 8, 8);
        const int tar_second = DevUtil::get_flag_bits(encoded_slot, 0, 8);
        if (GetAmsById(std::to_string(tar_first))) {
            auto tray = GetTray(tar_first, tar_second);
            if (!tray) {
                tray = GetTray(tar_second, tar_first);
            }

            if (tray) {
                return tray->get_ams_slot_id();
            }
        } else if (GetVirtualTray(std::to_string(tar_first))) {
            return DevAmsSlotId{ tar_first, 0 };
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "[exception]:" << e.what();
    }

    return std::nullopt;
}

std::vector<DevAmsSlotId> DevFilaSystem::GetFilamentChangingSlots() const
{
    std::vector<DevAmsSlotId> changing_slots;
    changing_slots.reserve(m_encoded_filament_changing_slots.size());
    for (const int encoded_slot : m_encoded_filament_changing_slots) {
        if (auto slot = DecodeFilamentChangingSlot(encoded_slot); slot.has_value()) {
            changing_slots.push_back(*slot);
        }
    }
    return changing_slots;
}

bool DevFilaSystem::IsFilamentChangingSlot(int ams_id, int slot_id) const
{
    return std::any_of(m_encoded_filament_changing_slots.begin(), m_encoded_filament_changing_slots.end(),
                       [this, ams_id, slot_id](int encoded_slot) {
        if (auto item = DecodeFilamentChangingSlot(encoded_slot); item.has_value()) {
            return item->first == ams_id && item->second == slot_id;
        }
        return false;
    });
};

std::optional<DevAmsSlotId> dev_ams_slot_info_to_id(const DevAmsSlotInfo& slot_info, bool normalize_virtual_slot)
{
    if (slot_info.ams_id.empty() || slot_info.slot_id.empty()) return std::nullopt;
    try {
        const int ams_id = std::stoi(slot_info.ams_id);
        const int slot_id = normalize_virtual_slot ? 0 : std::stoi(slot_info.slot_id);
        return DevAmsSlotId{ams_id, slot_id};
    } catch (...) {
        BOOST_LOG_TRIVIAL(warning) << current_exception_message();
        return std::nullopt;
    }
}

std::optional<DevAmsSlotId> DevFilaSystem::GetCurrentChangingSlot() const
{
    if (m_owner->GetMcTask().IsValid()) {
        int sub_step = m_owner->GetMcTask().GetSubStep();
        if (m_encoded_filament_changing_slots.size() > sub_step) {
            if (auto slot = DecodeFilamentChangingSlot(m_encoded_filament_changing_slots[sub_step]); slot.has_value()) {
                return slot;
            }
        }
    }

    auto extder_sys = m_owner->GetExtderSystem();
    const auto& extder = extder_sys->GetLoadingExtder();
    if (extder.has_value()) {
        const auto& slot_target = extder->GetSlotTarget();
        if (slot_target.slot_id == "255")/*unload*/ {
            const auto& slot_now = extder->GetSlotNow();
            return dev_ams_slot_info_to_id(slot_now, IsVirtualTrayId(slot_now.ams_id));
        } else {
            return dev_ams_slot_info_to_id(slot_target, IsVirtualTrayId(slot_target.ams_id));
        }
    }

    return std::nullopt;
}

bool DevFilaSystem::IsCurrentFilamentChangingSlot(int ams_id, int slot_id) const
{
    auto changing_slot_opt = GetCurrentChangingSlot();
    if (changing_slot_opt.has_value()) {
        return changing_slot_opt->first == ams_id && changing_slot_opt->second == slot_id;
    }

    return false;
}


// only support for rack
std::map<DevAmsSlotId, int> DevFilaSystem::GetSlot2HotendDisplayIdx() const {
	std::map<DevAmsSlotId, int> slot_to_display_index;
	if (!m_owner || !m_owner->GetNozzleRack() || !m_owner->GetNozzleSystem()) {
		assert(0);
		return slot_to_display_index;
	}

	if (!m_owner->GetNozzleRack()->IsDirectConnectedRack()) {
		return slot_to_display_index;
	}

	for (const auto& [ams_id, ams] : amsList) {
		if (!ams) {
            continue;
		}

        for (const auto& [slot_id, slot] : ams->GetTrays()) {
            if (slot && slot->get_nozzle_addr_id(m_owner).has_value()) {
                try {
                    auto nozzle_addr_id = slot->get_nozzle_addr_id(m_owner).value();
                    slot_to_display_index[DevAmsSlotId{ std::stoi(ams_id), std::stoi(slot_id) }] = nozzle_addr_id + 1;
                } catch (const std::exception&) {
                }
            }
        }
	}

	const auto& vt_trays = GetVirtualTrays();
	for (const auto& [vt_id, vt] : vt_trays) {
		if (vt.get_nozzle_addr_id(m_owner).has_value()) {
            try {
                auto nozzle_addr_id = vt.get_nozzle_addr_id(m_owner).value();
                slot_to_display_index[DevAmsSlotId{ std::stoi(vt_id), 0 }] = nozzle_addr_id + 1;
            } catch (const std::exception&) {
            }
		}
	}

	return slot_to_display_index;
}

void DevFilaSystem::ParseVirtualTrayInfos(const json& jj)
{
    auto update_virtual_tray = [this](const DevAmsTray& vslot) {
        auto current = m_vt_trays.find(vslot.id);
        if (current != m_vt_trays.end() && current->second.hold_count > 0) {
            current->second.hold_count--;
            current->second.is_exists = vslot.is_exists;
            current->second.current_extruder_id = vslot.current_extruder_id;
            current->second.binded_extruder_set = vslot.binded_extruder_set;
            current->second.binded_switcher_pos = vslot.binded_switcher_pos;
            return;
        }
        m_vt_trays[vslot.id] = vslot;
    };

    if (jj.contains("vir_slot") && jj["vir_slot"].is_array()) {
        for (auto it = jj["vir_slot"].begin(); it != jj["vir_slot"].end(); it++) {
            const auto& vslot = MachineObject::parse_vt_tray(m_owner, it.value().get<json>());
            update_virtual_tray(vslot);
        }
    } else if (jj.contains("vt_tray")) {
        auto vir_main_slot = MachineObject::parse_vt_tray(m_owner, jj["vt_tray"].get<json>());
        vir_main_slot.id = std::to_string(VIRTUAL_TRAY_MAIN_ID);
        update_virtual_tray(vir_main_slot);
    }
}

void DevFilaSystemParser::ParseV1_0(const json& jj, MachineObject* obj, DevFilaSystem* system, bool key_field_only)
{
    if (jj.contains("ams")) {
        if (jj["ams"].contains("ams")) {
            if (jj["ams"].contains("ams_exist_bits")) {
                obj->ams_exist_bits = stol(jj["ams"]["ams_exist_bits"].get<std::string>(), nullptr, 16);
            }

            if (jj["ams"].contains("tray_exist_bits")) {
                DevJsonValParser::ParseLongLongVal(jj["ams"]["tray_exist_bits"], obj->tray_exist_bits, 16);
            }

            if (jj["ams"].contains("cali_stat")) { system->m_ams_cali_stat = jj["ams"]["cali_stat"].get<int>(); }
            if (jj["ams"].contains("stat")) { system->m_ams_stat = jj["ams"]["stat"].get<int>(); }

            if (!key_field_only) {
                if (jj["ams"].contains("cfs")) {
                    system->m_filament_change_steps = DevJsonValParser::GetVal<std::vector<DevFilamentStep>>(jj["ams"], "cfs");
                } else {
                    system->m_filament_change_steps.clear();
                }

                if (jj["ams"].contains("cfs_m")) {
                    system->m_filament_change_steps_multi = DevJsonValParser::GetVal<std::vector<std::vector<DevFilamentStep>>>(jj["ams"], "cfs_m");
                } else {
                    system->m_filament_change_steps_multi.clear();
                }

                const char* changing_slot_key = jj["ams"].contains("csl") ? "csl" : (jj["ams"].contains("cs") ? "cs" : nullptr);
                if (changing_slot_key) {
                    system->m_encoded_filament_changing_slots.clear();
                    const auto changing_slots = DevJsonValParser::GetVal<std::vector<int>>(jj["ams"], changing_slot_key);
                    for (const int encoded_slot : changing_slots) {
                        system->m_encoded_filament_changing_slots.push_back(encoded_slot);
                    }
                } else {
                    system->m_encoded_filament_changing_slots.clear();
                }

                if (jj["ams"].contains("tray_read_done_bits")) {
                    DevJsonValParser::ParseLongLongVal(jj["ams"]["tray_read_done_bits"], obj->tray_read_done_bits, 16);
                }
                if (jj["ams"].contains("tray_reading_bits")) {
                    DevJsonValParser::ParseLongLongVal(jj["ams"]["tray_reading_bits"], obj->tray_reading_bits, 16);
                }
                if (jj["ams"].contains("tray_is_bbl_bits")) {
                    DevJsonValParser::ParseLongLongVal(jj["ams"]["tray_is_bbl_bits"], obj->tray_is_bbl_bits, 16);
                }

                if (jj["ams"].contains("version")) {
                    if (jj["ams"]["version"].is_number()) {
                        obj->ams_version = jj["ams"]["version"].get<int>();
                    }
                }

                if (time(nullptr) - obj->ams_user_setting_start > HOLD_TIME_3SEC) {
                    if (jj["ams"].contains("insert_flag")) {
                        system->m_ams_system_setting.SetDetectOnInsertEnabled(jj["ams"]["insert_flag"].get<bool>());
                    }
                    if (jj["ams"].contains("power_on_flag")) {
                        system->m_ams_system_setting.SetDetectOnPowerupEnabled(jj["ams"]["power_on_flag"].get<bool>());
                    }
                    if (jj["ams"].contains("calibrate_remain_flag")) {
                        system->m_ams_system_setting.SetDetectRemainEnabled(jj["ams"]["calibrate_remain_flag"].get<bool>());
                    }
                }

                if (jj["ams"].contains("ams")) {
                    std::unordered_set<std::string> existing_ams_set;
                    const json& j_ams = jj["ams"]["ams"];
                    for (const auto& ams_item : j_ams) {
                        const auto& ams_info = ParseAmsInfo(ams_item, obj, system);
                        if (ams_info) {
                            system->amsList[ams_info->GetAmsId()] = ams_info;
                            existing_ams_set.insert(ams_info->GetAmsId());
                        }
                    }

                    auto iter = system->amsList.begin();
                    while (iter != system->amsList.end()) {
                        if (existing_ams_set.count(iter->first) == 0) {
                            BOOST_LOG_TRIVIAL(trace) << "parse_json: remove " << KV(log_key::ams_id, iter->first);
                            iter = system->amsList.erase(iter);
                            continue;
                        }

                        iter++;
                    }
                }
            }
        }
    }
}

// return DevAms pointer if parsed successfully, otherwise return nullptr
DevAms* DevFilaSystemParser::ParseAmsInfo(const json& j_ams, MachineObject* obj, DevFilaSystem* system)
{
    if (!system) {
        return nullptr;
    }

    if (!j_ams.contains("id")) {
        return nullptr;
    };

    const auto& ams_id = DevJsonValParser::GetVal<std::string>(j_ams, "id");
    if (ams_id.empty()) {
        return nullptr;
    }


    std::set<int> binded_extruder_set;
    std::optional<DevFilaSwitch::SwitchPos> binded_switcher_pos;
    auto type_id = DevAmsType::AMS; // 0:dummy 1:ams 2:ams-lite 3:n3f 4:n3s
    auto remain_estimate_version = DevAms::RemainEstimateVersion::Legacy;
    bool enable_filament_tab_auto_read_rfid = false;
    bool support_read_ext_rfid = false;

    /*ams info*/
    if (j_ams.contains("info")) {
        const std::string& info = j_ams["info"].get<std::string>();
        type_id = (DevAmsType)DevUtil::get_flag_bits(info, 0, 4);
        int extuder_id = DevUtil::get_flag_bits(info, 8, 4);
        if (extuder_id == 0xE && obj->GetFilaSwitch()->IsInstalled()) {
            int bind_switch_in = DevUtil::get_flag_bits(info, 24, 4);
            if (bind_switch_in == 0 || bind_switch_in == 1) {
                binded_extruder_set = { MAIN_EXTRUDER_ID, DEPUTY_EXTRUDER_ID };
            }

            if (bind_switch_in == 0) {
                binded_switcher_pos = DevFilaSwitch::SwitchPos::POS_IN_B;
            } else if (bind_switch_in == 1) {
                binded_switcher_pos = DevFilaSwitch::SwitchPos::POS_IN_A;
            }
        } else if (extuder_id == 0xE && (type_id == DevAmsType::ZMULTI_A || type_id == DevAmsType::ZMULTI_B)) {
            binded_extruder_set = { MAIN_EXTRUDER_ID };
        } else if (extuder_id == 0xE && obj->GetNozzleRack()->IsDirectConnectedRack()) {
            binded_extruder_set = { MAIN_EXTRUDER_ID };
        } else if (extuder_id == 0xE)
        {
            binded_extruder_set = {};
        } else
        {
            binded_extruder_set = { extuder_id };
        }

        remain_estimate_version = static_cast<DevAms::RemainEstimateVersion>(DevUtil::get_flag_bits(info, 30, 2));
        enable_filament_tab_auto_read_rfid = DevUtil::get_flag_bits(info, 32) != 0;
        support_read_ext_rfid = DevUtil::get_flag_bits(info, 33) != 0;
    } else {
        binded_extruder_set = { MAIN_EXTRUDER_ID }; // Default extruder id
        if (!obj->is_enable_ams_np && obj->get_printer_ams_type() == "f1") {
            type_id = DevAmsType::AMS_LITE;
        }
    }

    DevAms* curr_ams = nullptr;
    auto ams_it = system->amsList.find(ams_id);
    if (ams_it == system->amsList.end()) {
        DevAms* new_ams = new DevAms(obj->GetFilaSystem(), ams_id, binded_extruder_set, type_id);
        curr_ams = new_ams;// new ams event
    } else {
        curr_ams = ams_it->second;
    }

    if (!curr_ams) {
        return nullptr;
    };

    /*set ams type flag*/
    if (curr_ams->m_binded_switcher_pos != binded_switcher_pos || curr_ams->m_binded_extruder_set != binded_extruder_set) {
        BOOST_LOG_TRIVIAL(info) << "[FilaSystem] AMS bind changed: " << KV(log_key::ams_id, ams_id) << "extruder_set "
                                << curr_ams->m_binded_extruder_set.size() << " -> " << binded_extruder_set.size() << ", switcher_pos "
                                << (curr_ams->m_binded_switcher_pos.has_value() ? std::to_string((int)curr_ams->m_binded_switcher_pos.value()) : "nullopt")
                                << " -> " << (binded_switcher_pos.has_value() ? std::to_string((int)binded_switcher_pos.value()) : "nullopt");}
    curr_ams->SetAmsType(type_id);
    curr_ams->m_binded_switcher_pos = binded_switcher_pos;
    curr_ams->m_binded_extruder_set = binded_extruder_set;
    curr_ams->m_remain_estimate_version = remain_estimate_version;
    curr_ams->m_enable_filament_tab_auto_read_rfid = enable_filament_tab_auto_read_rfid;
    curr_ams->m_support_read_ext_rfid = support_read_ext_rfid;

    // Temperature
    if (j_ams.contains("temp")) {
        std::string temp = j_ams["temp"].get<std::string>();
        try {
            curr_ams->m_current_temperature = DevUtil::string_to_float(temp);
        } catch (...) {
            BOOST_LOG_TRIVIAL(warning) << current_exception_message();
            curr_ams->m_current_temperature = INVALID_AMS_TEMPERATURE;
        }
    }

    // Humidity level and percent
    if (j_ams.contains("humidity")) {
        std::string humidity = j_ams["humidity"].get<std::string>();
        try {
            curr_ams->m_humidity_level = atoi(humidity.c_str());
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "[error]: invalid humidity, error: " << e.what();
        }
    }

    if (j_ams.contains("humidity_raw")) {
        std::string humidity_raw = j_ams["humidity_raw"].get<std::string>();
        try {
            curr_ams->m_humidity_percent = atoi(humidity_raw.c_str());
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "[error]: invalid humidity_raw, error: " << e.what();
        }
    }

    // Drying
    DevJsonValParser::ParseVal(j_ams, "dry_time", curr_ams->m_left_dry_time);
    if (obj->is_support_remote_dry) {
        if (j_ams.contains("info")) {
            const std::string& info = j_ams["info"].get<std::string>();
            curr_ams->m_dry_status = (DevAms::DryStatus)DevUtil::get_flag_bits(info, 4, 4);
            curr_ams->m_dry_fan1_status = (DevAms::DryFanStatus)DevUtil::get_flag_bits(info, 18, 2);
            curr_ams->m_dry_fan2_status = (DevAms::DryFanStatus)DevUtil::get_flag_bits(info, 20, 2);
            curr_ams->m_dry_sub_status = (DevAms::DrySubStatus)DevUtil::get_flag_bits(info, 22, 2);
        }

        if (j_ams.contains("dry_setting")) {
            const auto& j_dry_settings = j_ams["dry_setting"];
            DevAms::DrySettings dry_settings;
            DevJsonValParser::ParseVal(j_dry_settings, "dry_filament", dry_settings.dry_filament);
            DevJsonValParser::ParseVal(j_dry_settings, "dry_temperature", dry_settings.dry_temp);
            DevJsonValParser::ParseVal(j_dry_settings, "dry_duration", dry_settings.dry_hour);
            curr_ams->m_dry_settings = dry_settings;
        }

        if (j_ams.contains("dry_sf_reason")) {
            curr_ams->m_dry_cannot_reasons = DevJsonValParser::GetVal<std::vector<DevAms::CannotDryReason>>(j_ams, "dry_sf_reason");
        }
    }

    curr_ams->m_zones.clear();
    if (j_ams.contains("zone_info") && j_ams["zone_info"].is_array()) {
        for (const auto& j_zone : j_ams["zone_info"]) {
            DevAms::DevAmsZone zone;
            DevJsonValParser::ParseVal(j_zone, "zone_id", zone.zone_id);

            if (j_zone.contains("zone_slots") && j_zone["zone_slots"].is_array()) {
                for (const auto& j_slot : j_zone["zone_slots"]) {
                    try {
                        if (j_slot.is_number_integer()) {
                            zone.zone_slots.insert(j_slot.get<int>());
                        } else if (j_slot.is_string()) {
                            zone.zone_slots.insert(std::stoi(j_slot.get<std::string>()));
                        }
                    } catch (const std::exception& e) {
                        BOOST_LOG_TRIVIAL(error) << "[error]: invalid zone slot, error: " << e.what();
                    }
                }
            }

            if (j_zone.contains("temp")) {
                std::string temp = j_zone["temp"].get<std::string>();
                try {
                    zone.current_temperature = DevUtil::string_to_float(temp);
                } catch (...) {
                    zone.current_temperature = INVALID_AMS_TEMPERATURE;
                }
            }

            if (j_zone.contains("humidity")) {
                std::string humidity = j_zone["humidity"].get<std::string>();
                try {
                    zone.humidity_level = atoi(humidity.c_str());
                } catch (const std::exception& e) {
                    BOOST_LOG_TRIVIAL(error) << "[error]: invalid zone humidity, error: " << e.what();
                }
            }

            if (j_zone.contains("humidity_raw")) {
                std::string humidity_raw = j_zone["humidity_raw"].get<std::string>();
                try {
                    zone.humidity_percent = atoi(humidity_raw.c_str());
                } catch (const std::exception& e) {
                    BOOST_LOG_TRIVIAL(error) << "[error]: invalid zone humidity_raw, error: " << e.what();
                }
            }

            DevJsonValParser::ParseVal(j_zone, "dry_time", zone.left_dry_time);

            if (j_zone.contains("info")) {
                zone.info = j_zone["info"].get<std::string>();
                zone.dry_status = (DevAms::DryStatus)DevUtil::get_flag_bits(zone.info, 4, 4);
                zone.dry_fan1_status = (DevAms::DryFanStatus)DevUtil::get_flag_bits(zone.info, 18, 2);
                zone.dry_fan2_status = (DevAms::DryFanStatus)DevUtil::get_flag_bits(zone.info, 20, 2);
                zone.dry_sub_status = (DevAms::DrySubStatus)DevUtil::get_flag_bits(zone.info, 22, 2);
            }

            if (j_zone.contains("dry_setting")) {
                const auto& j_dry_settings = j_zone["dry_setting"];
                DevAms::DrySettings dry_settings;
                DevJsonValParser::ParseVal(j_dry_settings, "dry_filament", dry_settings.dry_filament);
                DevJsonValParser::ParseVal(j_dry_settings, "dry_temperature", dry_settings.dry_temp);
                DevJsonValParser::ParseVal(j_dry_settings, "dry_duration", dry_settings.dry_hour);
                zone.dry_settings = dry_settings;
            }

            if (j_zone.contains("dry_sf_reason")) {
                zone.dry_cannot_reasons = DevJsonValParser::GetVal<std::vector<DevAms::CannotDryReason>>(j_zone, "dry_sf_reason");
            }

            curr_ams->m_zones[zone.zone_id] = zone;
        }
    }

    if (j_ams.contains("tray")) {
        std::unordered_set<std::string> existing_tray_set;
        const auto& tray_array = j_ams["tray"];
        for (const auto& tray_item : tray_array) {
            auto tray_info = ParseAmsTrayInfo(tray_item, obj, curr_ams);
            if (tray_info) {
                existing_tray_set.insert(tray_info->id);
                curr_ams->m_trays.insert(std::make_pair(tray_info->id, tray_info));
            }
        }

        // remove not in trayList
        auto iter = curr_ams->m_trays.begin();
        while (iter != curr_ams->m_trays.end()) {
            if (!existing_tray_set.count(iter->first)) {
                BOOST_LOG_TRIVIAL(trace) << "parse_json: remove " << KV(log_key::ams_id, ams_id) << KV(log_key::tray_id, iter->first);
                iter = curr_ams->m_trays.erase(iter);
                continue;
            }

            iter++;
        }
    }

    return curr_ams;
}

// return DevAmsTray pointer if parsed successfully, otherwise return nullptr
DevAmsTray* DevFilaSystemParser::ParseAmsTrayInfo(const json& j_tray, MachineObject* obj, DevAms* curr_ams)
{
    if (!j_tray.contains("id")) {
        return nullptr;
    }

    std::string tray_id = DevJsonValParser::GetVal<std::string>(j_tray, "id");
    if (tray_id.empty()) {
        return nullptr;
    }

    // compare tray_list
    DevAmsTray* curr_tray = curr_ams->GetTray(tray_id);
    if (!curr_tray) {
        curr_tray = new DevAmsTray(curr_ams->GetAmsId(), tray_id); // new tray event
    }

    curr_tray->ams_id = curr_ams->GetAmsId();
    curr_tray->ams_type = curr_ams->GetAmsType();
    curr_tray->current_extruder_id = curr_ams->GetCurrentExtruderId();
    curr_tray->binded_extruder_set = curr_ams->GetBindedExtruderSet();
    curr_tray->binded_switcher_pos = curr_ams->GetSwitcherPos();
    // 把 is_exists 更新提前到 hold_count 守卫之前：物理插拔状态（feed-hall 传感器
    // 的 tray_exist_bits）需要始终即时反映到 UI，不应受 hold 机制影响。
    // hold 的用途是防止 slicer 发出 ams_filament_setting 之后紧接收到旧的
    // push_status 而把类型/颜色等 identity 字段回滚，与物理在位状态无关。
    int ams_id_int = 0;
    int tray_id_int = 0;
    try {
        tray_id_int = atoi(curr_tray->id.c_str());
        curr_tray->is_exists = DevUtil::get_flag_bits(obj->tray_exist_bits, curr_ams->GetTrayId(tray_id_int));
    } catch (...) {
        BOOST_LOG_TRIVIAL(warning) << current_exception_message();
    }

    // runtime state, must not be held back by hold_count
    curr_tray->print_status.reset();
    if (j_tray.contains("print_status") && j_tray["print_status"].is_number_integer()) {
        curr_tray->print_status = static_cast<DevAmsTray::PrintStatus>(j_tray["print_status"].get<int>());
    }

    if (curr_tray->hold_count > 0) {
        curr_tray->hold_count--;
        return curr_tray;   // 只跳过类型/颜色等 identity 字段，is_exists 已更新
    }

    DevJsonValParser::ParseVal(j_tray, "tag_uid", curr_tray->tag_uid, std::string("0"));
    if (j_tray.contains("tray_info_idx") && j_tray.contains("tray_type")) {
        DevJsonValParser::ParseVal(j_tray, "tray_info_idx", curr_tray->setting_id);

        std::string type = MachineObject::setting_id_to_type(curr_tray->setting_id, j_tray["tray_type"].get<std::string>());
        if (curr_tray->setting_id == "GFS00") {
            curr_tray->m_fila_type = "PLA-S";
        } else if (curr_tray->setting_id == "GFS01") {
            curr_tray->m_fila_type = "PA-S";
        } else {
            curr_tray->m_fila_type = type;
        }
    } else {
        curr_tray->setting_id = "";
        curr_tray->m_fila_type = "";
    }

    DevJsonValParser::ParseVal(j_tray, "tray_sub_brands", curr_tray->sub_brands);
    DevJsonValParser::ParseVal(j_tray, "tray_weight", curr_tray->weight);
    DevJsonValParser::ParseVal(j_tray, "tray_diameter", curr_tray->diameter);
    DevJsonValParser::ParseVal(j_tray, "tray_temp", curr_tray->temp);
    DevJsonValParser::ParseVal(j_tray, "tray_time", curr_tray->time);

    DevJsonValParser::ParseVal(j_tray, "bed_temp_type", curr_tray->bed_temp_type);
    DevJsonValParser::ParseVal(j_tray, "bed_temp", curr_tray->bed_temp);
    DevJsonValParser::ParseVal(j_tray, "nozzle_temp_max", curr_tray->nozzle_temp_max);
    DevJsonValParser::ParseVal(j_tray, "nozzle_temp_min", curr_tray->nozzle_temp_min);
    DevJsonValParser::ParseVal(j_tray, "xcam_info", curr_tray->xcam_info);
    DevJsonValParser::ParseVal(j_tray, "tray_uuid", curr_tray->uuid, std::string("0"));
    DevJsonValParser::ParseVal(j_tray, "tray_id_name", curr_tray->tray_id_name);
    DevJsonValParser::ParseVal(j_tray, "remain", curr_tray->remain, -1);
    DevJsonValParser::ParseVal(j_tray, "remain_g", curr_tray->remain_g, -1);
    DevJsonValParser::ParseVal(j_tray, "setting_id", curr_tray->filament_setting_id);

    {
        curr_tray->UpdateColorFromStr(DevJsonValParser::GetVal<std::string>(j_tray, "tray_color"));
        if (j_tray.contains("cols")) {
            curr_tray->cols.clear();
            if (j_tray["cols"].is_array()) {
                for (auto it = j_tray["cols"].begin(); it != j_tray["cols"].end(); it++) {
                    curr_tray->cols.push_back(it.value().get<std::string>());
                }
            }
        }

        if (curr_tray->cols.empty()) {
            curr_tray->cols.push_back(curr_tray->color);
        }

        if (j_tray.contains("ctype")) {
            curr_tray->ctype = (DevFilaColorType)DevJsonValParser::GetVal<int>(j_tray, "ctype");
        } else {
            if (curr_tray->cols.size() < 2) {
                curr_tray->ctype = DevFilaColorType::CTYPE_SINGLE;
            } else {
                curr_tray->ctype = DevFilaColorType::CTYPE_MULTI;
            }
        }
    }

    // Calibration k, n, cali_idx
    auto curr_time = std::chrono::system_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(curr_time - obj->extrusion_cali_set_hold_start);
    if (diff.count() > HOLD_TIMEOUT || diff.count() < 0
        || ams_id_int != (obj->extrusion_cali_set_tray_id / 4)
        || tray_id_int != (obj->extrusion_cali_set_tray_id % 4)) {
        DevJsonValParser::ParseVal(j_tray, "k", curr_tray->k);
        DevJsonValParser::ParseVal(j_tray, "n", curr_tray->n);
    }

    DevJsonValParser::ParseVal(j_tray, "cali_idx", curr_tray->cali_idx);

    if (j_tray.contains("state")) {
        const int state = DevJsonValParser::GetVal<int>(j_tray, "state");
        curr_tray->remain_fetch_status =
            static_cast<DevAmsTray::RemainFetchStatus>((state >> 5) & 0x7);
    }

    return curr_tray;
}

}