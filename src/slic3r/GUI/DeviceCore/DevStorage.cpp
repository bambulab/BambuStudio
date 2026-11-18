#include "DevStorage.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "libslic3r/Log.hpp"

#include <algorithm>
#include <cctype>

#include <boost/log/trivial.hpp>


namespace Slic3r {

static constexpr const char *STORAGE_TYPE_INTERNAL = "internal";
static constexpr const char *STORAGE_TYPE_EXTERNAL = "external";

// legacy: only emmc is internal
static bool is_legacy_internal_name(const std::string &normalized_name)
{
    return normalized_name == "emmc";
}

static long long parse_kb(const json &item, const char *key)
{
    if (!item.contains(key))
        return -1;

    const json &value = item[key];
    try {
        if (value.is_number())
            return value.get<long long>();
        if (value.is_string())
            return std::stoll(value.get<std::string>());
    } catch (...) {}
    return -1;
}

// storage_state: 0 not exist, 1 normal, 2 need format
static bool parse_type_and_state(const json &item, DevStorage::StorageType &type, DevStorage::StorageState &state)
{
    if (!item.contains("storage_type") || !item["storage_type"].is_string())
        return false;

    const std::string type_str = item["storage_type"].get<std::string>();
    if (type_str == STORAGE_TYPE_INTERNAL)
        type = DevStorage::StorageType::Internal;
    else if (type_str == STORAGE_TYPE_EXTERNAL)
        type = DevStorage::StorageType::External;
    else
        return false;

    int state_val = (item.contains("storage_state") && item["storage_state"].is_number()) ? item["storage_state"].get<int>() : 0;
    switch (state_val) {
    case 0: state = DevStorage::StorageState::NotExist; break;
    case 1: state = DevStorage::StorageState::Normal; break;
    default: state = DevStorage::StorageState::Abnormal; break;
    }
    return true;
}

// attr bit0-1: 00 internal, 01 external; bit2-3: 00 normal, 01 need format
static bool parse_attr(const json &item, DevStorage::StorageType &type, DevStorage::StorageState &state)
{
    if (!item.contains("attr") || !item["attr"].is_number())
        return false;

    int attr = item["attr"].get<int>();
    type     = ((attr & 0x3) == 1) ? DevStorage::StorageType::External : DevStorage::StorageType::Internal;
    state    = (((attr >> 2) & 0x3) == 0) ? DevStorage::StorageState::Normal : DevStorage::StorageState::Abnormal;
    return true;
}

#pragma region names

std::string DevStorage::NormalizeStorageName(const std::string &name)
{
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });

    if (lower == "sdcard")
        return "sd";
    if (lower == "udisk")
        return "usb";
    return lower;
}

std::string DevStorage::GetStorageTypeStr(StorageType type)
{
    return type == StorageType::Internal ? STORAGE_TYPE_INTERNAL : STORAGE_TYPE_EXTERNAL;
}

DevStorage::StorageType DevStorage::ParseStorageTypeStr(const std::string &type_str)
{
    return type_str == STORAGE_TYPE_INTERNAL ? StorageType::Internal : StorageType::External;
}

std::string DevStorage::GetFileSystemStorageParam(StorageType type)
{
    return type == StorageType::Internal ? STORAGE_TYPE_INTERNAL : "";
}

#pragma endregion

#pragma region queries

const DevStorage::StorageEntry *DevStorage::FindStorage(const std::string &name) const
{
    const std::string normalized = NormalizeStorageName(name);
    auto iter = std::find_if(m_storages.begin(), m_storages.end(),
                             [&normalized](const StorageEntry &entry) { return entry.name == normalized; });
    return iter != m_storages.end() ? &(*iter) : nullptr;
}

DevStorage::StorageType DevStorage::GetStorageType(const std::string &name) const
{
    if (m_has_storage_info) {
        if (const StorageEntry *entry = FindStorage(name))
            return entry->type;
    }

    return is_legacy_internal_name(NormalizeStorageName(name)) ? StorageType::Internal : StorageType::External;
}

DevStorage::StorageState DevStorage::GetStorageState(const std::string &name) const
{
    if (m_has_storage_info) {
        if (const StorageEntry *entry = FindStorage(name))
            return entry->state;
    }

    return GetStorageStateByType(GetStorageType(name));
}

bool DevStorage::HasAnyPresentStorage(const std::vector<std::string> &names) const
{
    return std::any_of(names.begin(), names.end(), [this](const std::string &name) { return IsStoragePresent(name); });
}

DevStorage::StorageState DevStorage::GetStorageStateByType(StorageType type) const
{
    // legacy: emmc is always present, external follows the sdcard state
    if (!m_has_storage_info)
        return type == StorageType::Internal ? StorageState::Normal : StorageState(m_sdcard_state);

    StorageState result = StorageState::NotExist;
    for (const auto &entry : m_storages) {
        if (entry.type != type || entry.state == StorageState::NotExist)
            continue;
        if (entry.state == StorageState::Normal)
            return StorageState::Normal;
        if (result == StorageState::NotExist)
            result = entry.state;
    }
    return result;
}

long long DevStorage::GetFreeKb(StorageType type) const
{
    if (m_has_storage_info) {
        long long free_kb = -1;
        for (const auto &entry : m_storages) {
            if (entry.type == type && entry.state == StorageState::Normal)
                free_kb = std::max(free_kb, entry.free_kb());
        }
        if (free_kb >= 0)
            return free_kb;
    }

    return type == StorageType::Internal ? tl_internal_free_kb : tl_external_free_kb;
}

bool DevStorage::is_timelapse_storage_low(const std::string& storage) const
{
    const int THRESHOLD_KB = 20480; // 10MB
    long long free_kb = GetFreeKb(ParseStorageTypeStr(storage));
    return free_kb >= 0 && free_kb < THRESHOLD_KB;
}

DevStorage::SdcardState Slic3r::DevStorage::set_sdcard_state(int state)

{
    if (state < DevStorage::NO_SDCARD || state >= DevStorage::SDCARD_STATE_NUM) {
        m_sdcard_state = DevStorage::NO_SDCARD;
    } else {
        m_sdcard_state = DevStorage::SdcardState(state);
    }
    return m_sdcard_state;
}

#pragma endregion

#pragma region parsers

bool DevStorage::parse_storage_entry(const json &item, StorageEntry &entry)
{
    if (!item.is_object() || !item.contains("name") || !item["name"].is_string())
        return false;

    if (!parse_type_and_state(item, entry.type, entry.state) && !parse_attr(item, entry.type, entry.state))
        return false;

    entry.name     = NormalizeStorageName(item["name"].get<std::string>());
    entry.total_kb = parse_kb(item, "total");
    entry.used_kb  = parse_kb(item, "used");
    return true;
}

 void DevStorage::ParseV1_0(const json &print_json, DevStorage *system)
{
     if (system)
     {
        try {
            if (print_json.contains("sdcard")) {
                if (print_json["sdcard"].get<bool>())
                    system->m_sdcard_state = DevStorage::SdcardState::HAS_SDCARD_NORMAL;
                else
                    system->m_sdcard_state = DevStorage::SdcardState::NO_SDCARD;
            } else {
                system->m_sdcard_state = DevStorage::SdcardState::NO_SDCARD;
            }

            // parse timelapse storage space from cam push data
            if (print_json.contains("device") && print_json["device"].is_object()) {
                const auto& device_json = print_json["device"];
                if (device_json.contains("cam") && device_json["cam"].is_object()) {
                    const auto& cam_data = device_json["cam"];
                    if (cam_data.contains("tl_internal_free_kb"))
                        system->tl_internal_free_kb  = cam_data["tl_internal_free_kb"].get<int>();
                    if (cam_data.contains("tl_internal_total_kb"))
                        system->tl_internal_total_kb = cam_data["tl_internal_total_kb"].get<int>();
                    if (cam_data.contains("tl_external_free_kb"))
                        system->tl_external_free_kb  = cam_data["tl_external_free_kb"].get<int>();
                    if (cam_data.contains("tl_external_total_kb"))
                        system->tl_external_total_kb = cam_data["tl_external_total_kb"].get<int>();
                }
            }
        } catch (...) {
            BOOST_LOG_TRIVIAL(warning) << current_exception_message();
        }
     }
}

// an unlisted storage is not present
void DevStorage::ParseV2_0(const json &print_json, DevStorage *system)
{
    if (!system || !print_json.contains("device") || !print_json["device"].is_object())
        return;

    const json &device_json = print_json["device"];
    if (!device_json.contains("storage") || !device_json["storage"].is_array()) {
        system->m_has_storage_info = false;
        system->m_storages.clear();
        return;
    }

    std::vector<StorageEntry> storages;
    try {
        for (const auto &item : device_json["storage"]) {
            StorageEntry entry;
            if (parse_storage_entry(item, entry))
                storages.push_back(entry);
        }
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "DevStorage::ParseV2_0: failed to parse device.storage";
        return;
    }

    system->m_has_storage_info = true;
    system->m_storages.swap(storages);
}

#pragma endregion

} // namespace Slic3r
