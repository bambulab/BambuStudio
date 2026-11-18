#pragma once
#include <nlohmann/json.hpp>
#include "slic3r/Utils/json_diff.hpp"

#include <string>
#include <vector>

namespace Slic3r {

class MachineObject;


// Printer storage: device.storage when pushed, otherwise the legacy sdcard state
class DevStorage
{
public:
    DevStorage(MachineObject *obj) : m_owner(obj){}

public:

    enum SdcardState :  int {
        NO_SDCARD = 0,
        HAS_SDCARD_NORMAL = 1,
        HAS_SDCARD_ABNORMAL = 2,
        HAS_SDCARD_READONLY = 3,
        SDCARD_STATE_NUM = 4
    };

    enum class StorageType : int {
        Internal = 0,
        External = 1,
    };

    // values match SdcardState
    enum class StorageState : int {
        NotExist   = 0,
        Normal     = 1,
        Abnormal   = 2, // need format
        ReadOnly   = 3, // legacy only
    };

    struct StorageEntry
    {
        std::string  name;
        StorageType  type{StorageType::External};
        StorageState state{StorageState::NotExist};
        long long    total_kb{-1};
        long long    used_kb{-1};

        long long free_kb() const { return (total_kb >= 0 && used_kb >= 0) ? total_kb - used_kb : -1; }
    };

public:
    // "sdcard" -> "sd", "udisk" -> "usb"
    static std::string NormalizeStorageName(const std::string &name);

    static std::string GetStorageTypeStr(StorageType type);
    static StorageType ParseStorageTypeStr(const std::string &type_str);

    // "storage" field of file system requests: external -> "", internal -> "internal"
    static std::string GetFileSystemStorageParam(StorageType type);

    StorageType  GetStorageType(const std::string &name) const;
    bool         IsExternalStorage(const std::string &name) const { return GetStorageType(name) == StorageType::External; }
    StorageState GetStorageState(const std::string &name) const;
    bool         IsStoragePresent(const std::string &name) const { return GetStorageState(name) != StorageState::NotExist; }
    bool         HasAnyPresentStorage(const std::vector<std::string> &names) const;

    StorageState GetStorageStateByType(StorageType type) const;
    StorageState GetInternalStorageState() const { return GetStorageStateByType(StorageType::Internal); }
    StorageState GetExternalStorageState() const { return GetStorageStateByType(StorageType::External); }
    bool         HasExternalStorage() const { return GetExternalStorageState() != StorageState::NotExist; }
    bool         IsExternalStorageUsable() const { return GetExternalStorageState() == StorageState::Normal; }

    // KB, -1 when unknown
    long long    GetFreeKb(StorageType type) const;

    bool         HasStorageInfo() const { return m_has_storage_info; }
    const std::vector<StorageEntry> &GetStorages() const { return m_storages; }

    // legacy: sdcard means the external storage
    SdcardState get_sdcard_state() const { return SdcardState(GetExternalStorageState()); };
    SdcardState set_sdcard_state(int state);

    bool is_timelapse_storage_low(const std::string& storage) const;

    static void ParseV1_0(const json &print_json, DevStorage *system);
    // print.device.storage
    static void ParseV2_0(const json &print_json, DevStorage *system);

private:
    static bool         parse_storage_entry(const json &item, StorageEntry &entry);
    const StorageEntry *FindStorage(const std::string &name) const;

private:
    MachineObject *m_owner;
    SdcardState    m_sdcard_state  { NO_SDCARD };

    bool                      m_has_storage_info{false};
    std::vector<StorageEntry> m_storages;

    // timelapse storage space info (from device push cam data)
    int tl_internal_free_kb{-1};
    int tl_internal_total_kb{-1};
    int tl_external_free_kb{-1};
    int tl_external_total_kb{-1};
};

} // namespace Slic3r
