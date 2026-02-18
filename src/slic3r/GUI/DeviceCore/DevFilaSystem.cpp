#include <nlohmann/json.hpp>
#include "DevExtruderSystem.h"
#include "DevFilaSystem.h"

// TODO: remove this include
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/I18N.hpp"

#include "DevUtil.h"
#include "DevNozzleSystem.h"

#include "DevUtilBackend.h"

using namespace nlohmann;

namespace Slic3r {

wxColour DevAmsTray::decode_color(const std::string &color)
{
    if (color.empty()) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": empty";
        return wxColour(255, 255, 255, 255);//default white
    }

    std::string clr_str = color;
    if (color[0] != '#') {
        clr_str = "#" + color;
    }

    const auto& clr = wxColour(clr_str);
    if (clr.IsOk()) {
        return clr;
    }

    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": " << clr_str;
    return wxColour(255, 255, 255, 255);//default white
}

void DevAmsTray::UpdateColorFromStr(const std::string& color)
{
    if (color.empty()) return;
    if (this->color != color)
    {
        wx_color = "#" + wxString::FromUTF8(color);
        this->color = color;
    }
}

void DevAmsTray::reset()
{
    tag_uid             = "";
    setting_id          = "";
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
    k                   = 0.0f;
    n                   = 0.0f;
    is_bbl              = false;
    hold_count          = 0;
    remain              = 0;
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

DevAms::DevAms(const std::string& ams_id, int extruder_id, AmsType type)
{
    m_ams_id = ams_id;
    m_ext_id = extruder_id;
    m_ams_type = type;
}

DevAms::DevAms(const std::string& ams_id, int nozzle_id, int type)
{
    m_ams_id = ams_id;
    m_ext_id = nozzle_id;
    m_ams_type = (AmsType)type;
    assert(DUMMY < type && m_ams_type <= N3S);
}

DevAms::~DevAms()
{
    for (auto it = m_trays.begin(); it != m_trays.end(); it++)
    {
        if (it->second)
        {
            delete it->second;
            it->second = nullptr;
        }
    }
    m_trays.clear();
}

static unordered_map<int, wxString> s_ams_display_formats = {
    {DevAms::AMS,      "AMS-%d"},
    {DevAms::AMS_LITE, "AMS Lite-%d"},
    {DevAms::N3F,      "AMS 2 PRO-%d"},
    {DevAms::N3S,      "AMS HT-%d"}
};

wxString DevAms::GetDisplayName() const
{
    wxString ams_display_format;
    auto iter = s_ams_display_formats.find(m_ams_type);
    if (iter != s_ams_display_formats.end())
    {
        ams_display_format = iter->second;
    }
    else
    {
        assert(0 && __FUNCTION__);
        ams_display_format = "AMS-%d";
    }

    int num_id;
    try
    {
        num_id = std::stoi(GetAmsId());
    }
    catch (const std::exception& e)
    {
        assert(0 && __FUNCTION__);
        BOOST_LOG_TRIVIAL(error) << "Invalid AMS ID: " << GetAmsId() << ", error: " << e.what();
        num_id = 0;
    }

    int loc = (num_id > 127) ? (num_id - 127) : (num_id + 1);
    return wxString::Format(ams_display_format, loc);
}

int DevAms::GetSlotCount() const
{
    if (m_ams_type == AMS || m_ams_type == AMS_LITE || m_ams_type == N3F)
    {
        return 4;
    }
    else if (m_ams_type == N3S)
    {
        return 1;
    }

    return 1;
}

DevAmsTray* DevAms::GetTray(const std::string& tray_id) const
{
    auto it = m_trays.find(tray_id);
    if (it != m_trays.end())
    {
        return it->second;
    }

    return nullptr;
}

bool DevAms::IsSupportRemoteDry(const MachineObject* obj) const
{
    if (obj && obj->is_support_remote_dry) {
        return SupportDrying();
    }

    return false;
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
    for (auto it = amsList.begin(); it != amsList.end(); it++)
    {
        if (it->second)
        {
            delete it->second;
            it->second = nullptr;
        }
    }
    amsList.clear();
}

DevAms* DevFilaSystem::GetAmsById(const std::string& ams_id) const
{
    auto it = amsList.find(ams_id);
    if (it != amsList.end())
    {
        return it->second;
    }

    return nullptr;
}

DevAmsTray* DevFilaSystem::GetAmsTray(const std::string& ams_id, const std::string& tray_id) const
{
    auto it = amsList.find(ams_id);
    if (it == amsList.end()) return nullptr;
    if (!it->second) return nullptr;
    return it->second->GetTray(tray_id);;
}

void DevFilaSystem::CollectAmsColors(std::vector<wxColour>& ams_colors) const
{
    ams_colors.clear();
    ams_colors.reserve(amsList.size());
    for (auto ams = amsList.begin(); ams != amsList.end(); ams++)
    {
        for (auto tray = ams->second->GetTrays().begin(); tray != ams->second->GetTrays().end(); tray++)
        {
            if (tray->second->is_tray_info_ready())
            {
                auto ams_color = DevAmsTray::decode_color(tray->second->color);
                ams_colors.emplace_back(ams_color);
            }
        }
    }
}

int DevFilaSystem::GetExtruderIdByAmsId(const std::string& ams_id) const
{
    auto it = amsList.find(ams_id);
    if (it != amsList.end()) {
        return it->second->GetExtruderId();
    } else if (ams_id == VIRTUAL_AMS_MAIN_ID_STR) {
        return MAIN_EXTRUDER_ID;
    } else if (ams_id == VIRTUAL_AMS_DEPUTY_ID_STR) {
        return DEPUTY_EXTRUDER_ID;
    }

    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": ams_id " << ams_id << " not found";
    return 0; // not found
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
    if (setting_up_stat == 0x01 || setting_up_stat == 0x02 || setting_up_stat == 0x03 || setting_up_stat == 0x04)
    {
        return true;
    }

    return false;
}

bool DevFilaSystem::IsBBL_Filament(std::string tag_uid)
{
    if (tag_uid.empty())
    {
        return false;
    }

    for (int i = 0; i < tag_uid.length(); i++)
    {
        if (tag_uid[i] != '0') { return true; }
    }

    return false;
}

bool DevFilaSystem::CanShowFilamentBackup() const
{
    return m_owner->is_support_filament_backup && IsAutoRefillEnabled() && HasAms() && m_owner->GetExtderSystem()->HasFilamentBackup();
}

void DevFilaSystemParser::ParseV1_0(const json& jj, MachineObject* obj, DevFilaSystem* system, bool key_field_only)
{
    if (jj.contains("ams"))
    {
        if (jj["ams"].contains("ams"))
        {
            if (jj["ams"].contains("ams_exist_bits"))
            {
                obj->ams_exist_bits = stol(jj["ams"]["ams_exist_bits"].get<std::string>(), nullptr, 16);
            }

            if (jj["ams"].contains("tray_exist_bits"))
            {
                obj->tray_exist_bits = stol(jj["ams"]["tray_exist_bits"].get<std::string>(), nullptr, 16);
            }

            if (jj["ams"].contains("cali_stat")) { system->m_ams_cali_stat = jj["ams"]["cali_stat"].get<int>(); }

            if (!key_field_only)
            {
                if (jj["ams"].contains("tray_read_done_bits"))
                {
                    obj->tray_read_done_bits = stol(jj["ams"]["tray_read_done_bits"].get<std::string>(), nullptr, 16);
                }
                if (jj["ams"].contains("tray_reading_bits"))
                {
                    obj->tray_reading_bits = stol(jj["ams"]["tray_reading_bits"].get<std::string>(), nullptr, 16);
                }
                if (jj["ams"].contains("tray_is_bbl_bits"))
                {
                    obj->tray_is_bbl_bits = stol(jj["ams"]["tray_is_bbl_bits"].get<std::string>(), nullptr, 16);
                }
                if (jj["ams"].contains("version"))
                {
                    if (jj["ams"]["version"].is_number())
                    {
                        obj->ams_version = jj["ams"]["version"].get<int>();
                    }
                }

#if 0
                if (jj["ams"].contains("ams_rfid_status")) { }
#endif

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
                            BOOST_LOG_TRIVIAL(trace) << "parse_json: remove ams_id=" << iter->first;
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

    int extuder_id = MAIN_EXTRUDER_ID; // Default nozzle id
    int type_id = DevAms::AMS; // 0:dummy 1:ams 2:ams-lite 3:n3f 4:n3s

    /*ams info*/
    if (j_ams.contains("info")) {
        const std::string& info = j_ams["info"].get<std::string>();
        type_id = DevUtil::get_flag_bits(info, 0, 4);
        extuder_id = DevUtil::get_flag_bits(info, 8, 4);
    } else {
        if (!obj->is_enable_ams_np && obj->get_printer_ams_type() == "f1") {
            type_id = DevAms::AMS_LITE;
        }
    }

    /*AMS without initialization*/
    if (extuder_id == 0xE) {
        return nullptr;
    }

    DevAms* curr_ams = nullptr;
    auto ams_it = system->amsList.find(ams_id);
    if (ams_it == system->amsList.end()) {
        DevAms* new_ams = new DevAms(ams_id, extuder_id, type_id);
        curr_ams = new_ams;// new ams event
    } else {
        if (extuder_id != ams_it->second->GetExtruderId()) {
            ams_it->second->m_ext_id = extuder_id;
        }

        curr_ams = ams_it->second;
    }

    if (!curr_ams) {
        return nullptr;
    };

    /*set ams type flag*/
    curr_ams->SetAmsType(type_id);

    /*set ams exist flag*/
    try {
        int ams_id_int = atoi(ams_id.c_str());
        if (type_id < 4) {
            curr_ams->m_exist = (obj->ams_exist_bits & (1 << ams_id_int)) != 0 ? true : false;
        } else {
            curr_ams->m_exist = DevUtil::get_flag_bits(obj->ams_exist_bits, 4 + (ams_id_int - 128));
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "[error]: invalid ams_id:" << ams_id << ", error: " << e.what();
    }

    // Temperature
    if (j_ams.contains("temp")) {
        std::string temp = j_ams["temp"].get<std::string>();
        try {
            curr_ams->m_current_temperature = DevUtil::string_to_float(temp);
        } catch (...) {
            curr_ams->m_current_temperature = INVALID_AMS_TEMPERATURE;
        }
    }

    // Humidity level and percent
    if (j_ams.contains("humidity")) {
        std::string humidity = j_ams["humidity"].get<std::string>();
        try {
            curr_ams->m_humidity_level = atoi(humidity.c_str());
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "[error]: invalid humidity, error: " << e.what();
        }
    }

    if (j_ams.contains("humidity_raw")) {
        std::string humidity_raw = j_ams["humidity_raw"].get<std::string>();
        try {
            curr_ams->m_humidity_percent = atoi(humidity_raw.c_str());
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "[error]: invalid humidity_raw, error: " << e.what();
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
            curr_ams->m_dry_sub_status = (DevAms::DrySubStatus)DevUtil::get_flag_bits(info, 22, 4);
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
                BOOST_LOG_TRIVIAL(trace) << "parse_json: remove ams_id=" << ams_id << ", tray_id=" << iter->first;
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
    DevAmsTray* curr_tray = nullptr;
    auto tray_iter = curr_ams->GetTrays().find(tray_id);
    if (tray_iter == curr_ams->GetTrays().end()) {
        DevAmsTray* new_tray = new DevAmsTray(tray_id);
        curr_tray = new_tray; // new tray event
    } else {
        curr_tray = tray_iter->second;
    }

    if (!curr_tray) {
        return nullptr;
    }

    if (curr_tray->hold_count > 0) {
        curr_tray->hold_count--;
        return curr_tray;
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
    DevJsonValParser::ParseVal(j_tray, "remain", curr_tray->remain, -1);
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

    int ams_id_int = 0;
    int tray_id_int = 0;
    try {
        std::string ams_id = curr_ams->GetAmsId();
        if (!ams_id.empty() && !curr_tray->id.empty()) {
            ams_id_int = atoi(ams_id.c_str());
            tray_id_int = atoi(curr_tray->id.c_str());

            if (curr_ams->GetAmsType() != DevAms::N3S) {
                curr_tray->is_exists = (obj->tray_exist_bits & (1 << (ams_id_int * 4 + tray_id_int))) != 0 ? true : false;
            } else {
                curr_tray->is_exists = DevUtil::get_flag_bits(obj->tray_exist_bits, 16 + (ams_id_int - 128));
            }

        }
    } catch (...) {
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
    return curr_tray;
}

}