#ifndef MULTI_NOZZLE_UTILS_HPP
#define MULTI_NOZZLE_UTILS_HPP

#include <vector>
#include <map>
#include <optional>
#include <set>
#include "PrintConfig.hpp"

namespace Slic3r {
struct FilamentInfo;
namespace MultiNozzleUtils {
// 存储单个喷嘴的信息
struct NozzleInfo
{
    double           diameter;
    NozzleVolumeType volume_type;
    int              extruder_id; // 逻辑挤出机id
    int              group_id;    // 对应逻辑喷嘴id, 无实际意义
};

// 喷嘴组信息，执行同步操作后前端传递给后端的数据
struct NozzleGroupInfo
{
    double           diameter;
    NozzleVolumeType volume_type;
    int              extruder_id;
    int              nozzle_count;

    NozzleGroupInfo(const double nozzle_diameter_, const NozzleVolumeType volume_type_, const int extruder_id_, const int nozzle_count_)
        : diameter(nozzle_diameter_), volume_type(volume_type_), extruder_id(extruder_id_), nozzle_count(nozzle_count_)
    {}

    inline bool operator<(const NozzleGroupInfo &rhs) const
    {
        if (extruder_id != rhs.extruder_id) return extruder_id < rhs.extruder_id;
        if (diameter != rhs.diameter) return diameter < rhs.diameter;
        if (volume_type != rhs.volume_type) return volume_type < rhs.volume_type;
        return nozzle_count < rhs.nozzle_count;
    }
};

// 分组后的结果，GCodeProcessor，发起打印页面需要
class MultiNozzleGroupResult
{
private:
    std::unordered_map<int, std::unordered_map<int, NozzleInfo>> extruder_to_filament_nozzles;

public:
    MultiNozzleGroupResult() = default;
    MultiNozzleGroupResult(const std::vector<int> &filament_nozzle_map, const std::vector<NozzleInfo> &nozzle_list);
    static std::optional<MultiNozzleGroupResult> init_from_slice_filament(const std::vector<int>          &filament_map,
                                                                          const std::vector<FilamentInfo> &filament_info); // 1 based filament_map
    bool                                         are_filaments_same_extruder(int filament_id1, int filament_id2) const;    // 判断两个材料是否处于同一个挤出机
    bool                                         are_filaments_same_nozzle(int filament_id1, int filament_id2) const;      // 判断两个材料是否处于同一个喷嘴
    int                                          get_extruder_count() const;                                               // 获取挤出机数量

    std::vector<NozzleInfo> get_used_nozzles(const std::vector<unsigned int> &filament_list, int target_extruder_id = -1) const; // 获取给定材料列表下指定挤出机使用的喷嘴
    std::vector<int>        get_used_extruders(const std::vector<unsigned int> &filament_list) const;                            // 获取使用的挤出机列表
    std::vector<int>        get_extruder_list() const;

    std::vector<int> filament_map;

public:
    int                       get_extruder_id(int filament_id) const;       // 根据材料id取逻辑挤出机id
    int                       get_nozzle_count(int extruder_id = -1) const; // 获取指定挤出机下的使用的喷嘴数量，-1表示所有挤出机的喷嘴数量总和
    std::optional<NozzleInfo> get_nozzle_for_filament(int filament_id) const;
};

class NozzleStatusRecorder
{
private:
    std::unordered_map<int, int> nozzle_filament_status;

public:
    NozzleStatusRecorder() = default;
    bool is_nozzle_empty(int nozzle_id) const;
    int  get_filament_in_nozzle(int nozzle_id) const;

    void clear_nozzle_status(int nozzle_id);
    void set_nozzle_status(int nozzle_id, int filament_id);
};

} // namespace MultiNozzleUtils
} // namespace Slic3r

#endif