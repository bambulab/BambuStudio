#pragma once
#include "libslic3r/CommonDefs.hpp"
#include "libslic3r/ProjectTask.hpp"

#include "DevDefs.h"
#include "slic3r/Utils/json_diff.hpp"
#include <wx/string.h>

namespace Slic3r
{
//Previous definitions
class MachineObject;
struct DevNozzle;

enum MappingOption
{
    USE_LEFT_AMS = 0,
    USE_RIGHT_AMS,
    USE_LEFT_EXT,
    USE_RIGHT_EXT
};

class DevMappingUtil
{
public:
    DevMappingUtil() = delete;
    ~DevMappingUtil() = delete;

public:
    // Whether the ams can load this slot without the user touching the machine.
    static bool IsSlotAutoLoadable(const DevNozzle& nz, DevAmsType ams_type, const DevAmsSlotId& slot);

    static bool is_valid_mapping_result(const MachineObject* obj, std::vector<FilamentInfo>& result, bool check_empty_slot = false);

    // sub_ams_group_id: ams_nozzle_idx, the only rack slot able to host a sub-AMS. Read by the
    // hotend-rack path only, which asks the machine whether one is really there; -1 disables
    // the sub-AMS rules outright.
    static int ams_filament_mapping(const MachineObject* obj, const std::vector<FilamentInfo>& filaments, std::vector<FilamentInfo>& result, std::vector<bool> map_opt, std::vector<int> exclude_id = std::vector<int>(), bool nozzle_has_ams_then_ignore_ext = false, bool check_nozzle_type = false, int sub_ams_group_id = -1);
};

};