#include "DevDryer.h"

#include "DevUtil.h"
#include "DevUtilBackend.h"
#include "libslic3r/Log.hpp"
#include "slic3r/GUI/DeviceManager.hpp"

#include <algorithm>
#include <cctype>

namespace Slic3r
{

namespace
{
const std::vector<DevDryerModuleProfile> s_module_profiles = {
    {DevHtDryerModule::ID, "N11", DevAmsType::ZMULTI_B, {1, 3}, 7, DevAmsType::ZMULTI_B, 45, 75, DevDryerType::HT_MODULE},
};

struct AmsDryerSpec
{
    DevAmsType    type;
    int           min_temp;
    int           max_temp;
    DevDryerType  dryer_type;
};
const std::vector<AmsDryerSpec> s_ams_dryer_specs = {
    {DevAmsType::N3F, 45, 65, DevDryerType::N3F},
    {DevAmsType::N3S, 45, 85, DevDryerType::N3S},
    {DevAmsType::ZMULTI_A, 45, 85, DevDryerType::ZMULTI_A},
};

const AmsDryerSpec* find_ams_dryer_spec(DevAmsType type)
{
    for (const auto& spec : s_ams_dryer_specs) {
        if (spec.type == type) return &spec;
    }
    return nullptr;
}

constexpr int MODULE_DRY_ZONE_ID     = 0;
constexpr int MODULE_AMBIENT_ZONE_ID = 1;

const std::map<std::string, DevAmsTray*>& no_trays()
{
    static const std::map<std::string, DevAmsTray*> empty;
    return empty;
}

// ht reports numbers either as json numbers or as strings
std::optional<float> parse_number(const nlohmann::json& j, const char* key)
{
    if (!j.contains(key)) return std::nullopt;
    const auto& v = j[key];
    try {
        if (v.is_number()) return v.get<float>();
        if (v.is_string()) return DevUtil::string_to_float(v.get<std::string>());
    } catch (...) {
        BOOST_LOG_TRIVIAL(warning) << "invalid ht field " << key << ": " << current_exception_message();
    }
    return std::nullopt;
}
} // namespace

const DevDryerModuleProfile* DevDryerModuleProfile::Find(const std::string& module_id, const std::string& printer_type)
{
    for (const auto& profile : s_module_profiles) {
        if (profile.module_id == module_id && profile.printer_type == printer_type) {
            return &profile;
        }
    }
    return nullptr;
}

void DevHtDryerModule::ParseV1_0(const nlohmann::json& ht_json)
{
    if (!ht_json.is_object()) return;

    if (auto v = parse_number(ht_json, "temp_cur")) m_state.temperature = *v;
    if (auto v = parse_number(ht_json, "temp_tar")) m_state.target_temperature = *v;
    if (auto v = parse_number(ht_json, "humidity_level")) m_state.humidity_level = static_cast<int>(*v);
    if (auto v = parse_number(ht_json, "humidity_raw")) m_state.humidity_percent = static_cast<int>(*v);
    if (auto v = parse_number(ht_json, "total_time")) m_state.total_time = static_cast<int>(*v);
    if (auto v = parse_number(ht_json, "remain_time")) m_state.left_time = static_cast<int>(*v);
    if (auto v = parse_number(ht_json, "state")) {
        const int state = static_cast<int>(*v);
        m_power_adapter_connected = DevUtil::get_flag_bits(state, 0, 2) == 1;
        m_state.dry_status = static_cast<DevAms::DryStatus>(DevUtil::get_flag_bits(state, 2, 4));
        m_damper_open = DevUtil::get_flag_bits(state, 6, 1) == 1;
        m_state.dry_sub_status = static_cast<DevAms::DrySubStatus>(DevUtil::get_flag_bits(state, 7, 2));
    }
    if (ht_json.contains("forbid_reason")) {
        m_state.cannot_reasons = DevJsonValParser::GetVal<std::vector<DevAms::CannotDryReason>>(ht_json, "forbid_reason");
    }

    DevJsonValParser::ParseVal(ht_json, "filament", m_state.settings.dry_filament);
    m_state.settings.dry_temp = (m_state.target_temperature >= 1.0f) ? static_cast<int>(m_state.target_temperature) : -1;
    m_state.settings.dry_hour = (m_state.total_time > 0) ? (m_state.total_time + 30) / 60 : -1;
}

std::string DevDryer::GetId() const
{
    return m_module ? m_module->GetId() : m_ams->GetAmsId();
}

DevAmsType DevDryer::GetFormType() const
{
    return m_module ? m_module->GetProfile()->form_type : m_ams->GetAmsType();
}

DevDryerType DevDryer::GetDryerType() const
{
    if (m_module) return m_module->GetProfile()->dryer_type;
    const AmsDryerSpec* spec = find_ams_dryer_spec(m_ams->GetAmsType());
    return spec ? spec->dryer_type : DevDryerType::Unknown;
}

std::optional<std::pair<int, int>> DevDryer::GetTempRange() const
{
    if (m_module) {
        const DevDryerModuleProfile* profile = m_module->GetProfile();
        return std::make_pair(profile->min_temp, profile->max_temp);
    }
    const AmsDryerSpec* spec = find_ams_dryer_spec(m_ams->GetAmsType());
    if (!spec) return std::nullopt;
    return std::make_pair(spec->min_temp, spec->max_temp);
}

bool DevDryer::IsSupportRemoteDry(const MachineObject* obj) const
{
    if (m_module) return obj && obj->is_support_remote_dry;
    return m_ams->IsSupportRemoteDry(obj);
}

std::optional<DevAmsZone> DevDryer::GetZone(int zone_id) const
{
    auto it = m_zones.find(zone_id);
    return it == m_zones.end() ? std::nullopt : std::optional<DevAmsZone>(it->second);
}

std::optional<DevAmsZone> DevDryer::GetZoneBySlotId(int slot_id) const
{
    for (const auto& [zone_id, zone] : m_zones) {
        if (zone.zone_slots.count(slot_id) != 0) return zone;
    }
    return std::nullopt;
}

bool DevDryer::IsDryableZone(int zone_id) const
{
    return m_dryable_zone_ids.count(zone_id) != 0;
}

std::vector<int> DevDryer::GetDryableZoneIds() const
{
    return std::vector<int>(m_dryable_zone_ids.begin(), m_dryable_zone_ids.end());
}

bool DevDryer::IsDryableSlot(int slot_id) const
{
    if (!m_module) return m_ams->IsDryableSlot(slot_id);
    const auto zone = GetZoneBySlotId(slot_id);
    return zone && IsDryableZone(zone->zone_id);
}

const std::map<std::string, DevAmsTray*>& DevDryer::GetTrays() const
{
    return m_ams ? m_ams->GetTrays() : no_trays();
}

float DevDryer::GetCurrentTemperature() const
{
    return m_module ? m_module->GetState().temperature : m_ams->GetCurrentTemperature();
}

int DevDryer::GetHumidityPercent() const
{
    return m_module ? m_module->GetState().humidity_percent : m_ams->GetHumidityPercent();
}

int DevDryer::GetLeftDryTime() const
{
    return m_module ? m_module->GetState().left_time : m_ams->GetLeftDryTime();
}

std::optional<DevAms::DryStatus> DevDryer::GetDryStatus() const
{
    return m_module ? m_module->GetState().dry_status : m_ams->GetDryStatus();
}

std::optional<DevAms::DrySubStatus> DevDryer::GetDrySubStatus() const
{
    return m_module ? m_module->GetState().dry_sub_status : m_ams->GetDrySubStatus();
}

std::optional<std::vector<DevAms::CannotDryReason>> DevDryer::GetCannotDryReason() const
{
    if (m_module) return m_module->GetState().cannot_reasons;
    return m_ams->GetCannotDryReason();
}

std::optional<DevAms::DrySettings> DevDryer::GetDrySettings() const
{
    if (m_module) return m_module->GetState().settings;
    return m_ams->GetDrySettings();
}

bool DevDryer::IsInPrintUse(std::optional<int> zone_id) const
{
    if (!m_owner || !m_owner->is_in_printing()) {
        return false;
    }

    auto is_in_dry_area = [this, zone_id](int slot_id) {
        if (!IsDryableSlot(slot_id)) {
            return false;
        }
        if (!zone_id) {
            return true;
        }
        const auto zone = GetZoneBySlotId(slot_id);
        return zone && zone->zone_id == *zone_id;
    };

    // new firmware reports tray print_status directly
    bool has_print_status = false;
    for (const auto& [tray_id, tray] : GetTrays()) {
        if (!tray || !tray->print_status.has_value()) {
            continue;
        }
        has_print_status = true;
        try {
            if (*tray->print_status != DevAmsTray::PrintStatus::Idle && is_in_dry_area(std::stoi(tray_id))) {
                return true;
            }
        } catch (...) {
            BOOST_LOG_TRIVIAL(warning) << "invalid " << KV(log_key::ams_id, GetId()) << " " << KV(log_key::tray_id, tray_id);
        }
    }
    if (has_print_status) {
        return false;
    }

    // old firmware: print.mapping, (ams_id << 8) | slot_id per filament, 0xFFFF unused
    // no slot info (host AMS offline): only the printing state is known
    const auto& mapping = m_owner->print_job_filament_mapping;
    if (!mapping.empty() && m_ams) {
        const int ams_id_int = m_ams->GetAmdIdInt();
        for (uint16_t v : mapping) {
            if (v != 0xFFFF && ((v >> 8) & 0xFF) == ams_id_int && is_in_dry_area(v & 0xFF)) {
                return true;
            }
        }
        return false;
    }

    return true;
}

void DevDryer::FillCommandTarget(nlohmann::json& print_cmd, std::optional<int> zone_id) const
{
    print_cmd["ams_id"] = m_ams ? m_ams->GetAmdIdInt() : 0;
    if (zone_id) print_cmd["zone_id"] = m_module ? 0 : *zone_id;
    if (m_module && m_module->GetId() == DevHtDryerModule::ID) print_cmd["is_ht"] = 1;
}

int DevDryer::CtrlStartDrying(std::optional<int> zone_id, const DevDryingSettings& settings) const
{
    if (!m_owner) return -1;

    nlohmann::json jj_command;
    auto& print_cmd = jj_command["print"];
    print_cmd["command"] = "ams_filament_drying";
    print_cmd["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    FillCommandTarget(print_cmd, zone_id.value_or(0));
    print_cmd["mode"] = DevAms::DryCtrlMode::OnTime;
    print_cmd["filament"] = settings.filament;
    print_cmd["temp"] = settings.temp;
    print_cmd["duration"] = settings.duration_hour;
    print_cmd["humidity"] = 0;
    print_cmd["rotate_tray"] = settings.rotate_tray;
    print_cmd["cooling_temp"] = settings.cooling_temp;
    print_cmd["close_power_conflict"] = settings.close_power_conflict;
    return m_owner->publish_json(jj_command);
}

int DevDryer::CtrlStopDrying() const
{
    if (!m_owner) return -1;

    nlohmann::json jj_command;
    auto& print_cmd = jj_command["print"];
    print_cmd["command"] = "ams_filament_drying";
    print_cmd["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    FillCommandTarget(print_cmd, std::nullopt);
    print_cmd["mode"] = DevAms::DryCtrlMode::Off;
    print_cmd["filament"] = "";
    print_cmd["temp"] = 0;
    print_cmd["duration"] = 0;
    print_cmd["humidity"] = 0;
    print_cmd["rotate_tray"] = false;
    print_cmd["cooling_temp"] = 0;
    print_cmd["close_power_conflict"] = false;
    return m_owner->publish_json(jj_command);
}

float DevFilamentDryingInfo::GetRecommendedTemp(bool printing) const
{
    return printing ? std::min({print_temp, softening_temp, heat_distortion_temp}) : idle_temp;
}

DevFilamentDryingInfo DevDryer::MakeFilamentDryingInfo(const DevFilamentDryingPreset& preset) const
{
    const DevAmsType form = GetFormType();
    auto at = [form](const std::unordered_map<DevAmsType, float>& values) {
        const auto it = values.find(form);
        return it != values.end() ? it->second : 0.0f;
    };

    DevFilamentDryingInfo info;
    info.filament_id          = preset.filament_id;
    info.idle_temp            = at(preset.filament_dev_ams_drying_temperature_on_idle);
    info.idle_hours           = at(preset.filament_dev_ams_drying_time_on_idle);
    info.print_temp           = at(preset.filament_dev_ams_drying_temperature_on_print);
    info.print_hours          = at(preset.filament_dev_ams_drying_time_on_print);
    info.softening_temp       = at(preset.filament_dev_drying_softening_temperature);
    info.heat_distortion_temp = at(preset.filament_dev_ams_drying_heat_distortion_temperature);
    info.fully_dryable        = preset.ams_limitations.count(form) != 0;
    return info;
}

std::optional<DevFilamentDryingInfo> DevDryer::GetFilamentDryingInfo(const std::string& filament_id) const
{
    const auto preset = DevUtilBackend::GetFilamentDryingPreset(filament_id);
    if (!preset) return std::nullopt;
    return MakeFilamentDryingInfo(*preset);
}

std::optional<DevFilamentDryingInfo> DevDryer::GetTrayFilamentDryingInfo(const DevAmsTray& tray) const
{
    if (!tray.get_display_filament_type().empty()) {
        if (const auto preset = tray.get_ams_drying_preset()) return MakeFilamentDryingInfo(*preset);
    }
    return GetFilamentDryingInfo(FALLBACK_FILAMENT_ID);
}

void DevDryerMgr::ParseV1_0(const nlohmann::json& print_json)
{
    if (print_json.contains("device") && print_json["device"].is_object()) {
        const nlohmann::json& j_device = print_json["device"];
        auto it = m_modules.find(DevHtDryerModule::ID);
        if (j_device.contains("ht")) {
            if (it == m_modules.end()) {
                BOOST_LOG_TRIVIAL(info) << "ht dryer module online";
                it = m_modules.emplace(DevHtDryerModule::ID, std::make_shared<DevHtDryerModule>()).first;
            }
            if (auto ht = std::dynamic_pointer_cast<DevHtDryerModule>(it->second)) ht->ParseV1_0(j_device["ht"]);
        } else if (it != m_modules.end()) {
            BOOST_LOG_TRIVIAL(info) << "ht dryer module offline";
            m_modules.erase(it);
        }
    }

    Bind();
}

void DevDryerMgr::Bind()
{
    const MachineObject* obj = m_fila_system->GetOwner();
    for (const auto& [module_id, dryer_module] : m_modules) {
        if (!dryer_module) continue;

        const DevDryerModuleProfile* profile = obj ? DevDryerModuleProfile::Find(module_id, obj->printer_type) : nullptr;
        if (profile != dryer_module->m_profile) {
            BOOST_LOG_TRIVIAL(info) << "dryer module " << module_id << " profile " << (profile ? profile->printer_type : std::string("none"));
            dryer_module->m_profile = profile;
        }

        std::optional<DevDryerSlotBinding> binding;
        if (profile) {
            for (const auto& [ams_id, ams] : m_fila_system->GetAmsList()) {
                if (ams && ams->GetAmsType() == profile->host_ams_type) {
                    binding = DevDryerSlotBinding{ams_id, profile->slots};
                    break;
                }
            }
        }

        if (binding.has_value() != dryer_module->m_slot_binding.has_value()) {
            BOOST_LOG_TRIVIAL(info) << "dryer module " << module_id << (binding ? " bound to " + binding->ams_id : std::string(" unbound"));
        }
        dryer_module->m_slot_binding = binding;
    }
}

std::optional<DevDryer> DevDryerMgr::Find(const std::string& id) const
{
    if (auto it = m_modules.find(id); it != m_modules.end()) {
        return FromModule(it->second);
    }

    // AMS ids are numeric; the AMS list comparator logs on others (eg. an offline module)
    if (id.empty() || !std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::isdigit(c); })) return std::nullopt;

    DevAms* ams = m_fila_system->GetAmsById(id);
    if (!ams) return std::nullopt;

    if (!ams->SupportDrying()) {
        for (const auto& [module_id, dryer_module] : m_modules) {
            const auto& binding = dryer_module ? dryer_module->GetSlotBinding() : std::nullopt;
            if (binding && binding->ams_id == id) {
                if (auto dryer = FromModule(dryer_module)) return dryer;
            }
        }
    }
    return FromAms(ams);
}

std::vector<DevDryer> DevDryerMgr::All() const
{
    std::vector<DevDryer> dryers;
    for (const auto& [ams_id, ams] : m_fila_system->GetAmsList()) {
        if (ams && ams->SupportDrying()) dryers.push_back(FromAms(ams));
    }
    for (const auto& [module_id, dryer_module] : m_modules) {
        if (auto dryer = FromModule(dryer_module)) dryers.push_back(std::move(*dryer));
    }
    return dryers;
}

DevDryer DevDryerMgr::FromAms(DevAms* ams) const
{
    DevDryer dryer;
    dryer.m_owner = m_fila_system->GetOwner();
    dryer.m_ams   = ams;
    dryer.m_zones = ams->GetZones();
    for (const auto& [zone_id, zone] : dryer.m_zones) {
        if (zone.SupportDrying()) dryer.m_dryable_zone_ids.insert(zone_id);
    }
    return dryer;
}

std::optional<DevDryer> DevDryerMgr::FromModule(const std::shared_ptr<DevDryerModule>& dryer_module) const
{
    const DevDryerModuleProfile* profile = dryer_module ? dryer_module->GetProfile() : nullptr;
    if (!profile) return std::nullopt; // not fitted on this machine model

    DevDryer dryer;
    dryer.m_owner  = m_fila_system->GetOwner();
    dryer.m_module = dryer_module;

    const auto& binding = dryer_module->GetSlotBinding();
    if (binding) dryer.m_ams = m_fila_system->GetAmsById(binding->ams_id);

    const DevDryerModuleState& st = dryer_module->GetState();
    DevAmsZone dry_zone;
    dry_zone.zone_id             = MODULE_DRY_ZONE_ID;
    dry_zone.zone_slots          = binding ? binding->slots : profile->slots;
    dry_zone.current_temperature = st.temperature;
    dry_zone.humidity_level      = st.humidity_level;
    dry_zone.humidity_percent    = st.humidity_percent;
    dry_zone.left_dry_time       = st.left_time;
    dry_zone.dry_status          = st.dry_status;
    dry_zone.dry_sub_status      = st.dry_sub_status;
    dry_zone.dry_cannot_reasons  = st.cannot_reasons;
    dry_zone.dry_settings        = st.settings;
    dryer.m_zones[dry_zone.zone_id] = dry_zone;
    dryer.m_dryable_zone_ids.insert(dry_zone.zone_id);

    // Ambient zone: the host's other slots
    DevAmsZone ambient_zone;
    ambient_zone.zone_id = MODULE_AMBIENT_ZONE_ID;
    for (int slot_id = 0; slot_id < profile->host_slot_count; ++slot_id) {
        if (dry_zone.zone_slots.count(slot_id) == 0) ambient_zone.zone_slots.insert(slot_id);
    }
    if (dryer.m_ams) {
        ambient_zone.current_temperature = dryer.m_ams->GetCurrentTemperature();
        ambient_zone.humidity_percent    = dryer.m_ams->GetHumidityPercent();
    }
    if (!ambient_zone.zone_slots.empty()) dryer.m_zones[ambient_zone.zone_id] = ambient_zone;

    return dryer;
}

} // namespace Slic3r
