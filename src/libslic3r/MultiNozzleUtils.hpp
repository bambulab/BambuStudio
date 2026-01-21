#ifndef MULTI_NOZZLE_UTILS_HPP
#define MULTI_NOZZLE_UTILS_HPP

#include <vector>
#include <map>
#include <optional>
#include <set>
#include "PrintConfig.hpp"

namespace Slic3r {
class PrintObject;
struct FilamentInfo;
class PrintObject;
namespace MultiNozzleUtils {

using ObjectLayerRange = std::pair<int,int>;
// 存储单个喷嘴的信息
struct NozzleInfo
{
    std::string      diameter;
    NozzleVolumeType volume_type;
    int              extruder_id{ -1 }; // 逻辑挤出机id
    int              group_id{ -1 };    // 对应逻辑喷嘴id

    std::string serialize(int id = -1) const;
    static std::optional<NozzleInfo> deserialize(const std::string& str);
};

// 喷嘴组信息，执行同步操作后前端传递给后端的数据
struct NozzleGroupInfo
{
    std::string      diameter;
    NozzleVolumeType volume_type;
    int              extruder_id;
    int              nozzle_count;

    NozzleGroupInfo() = default;

    NozzleGroupInfo(const std::string& nozzle_diameter_, const NozzleVolumeType volume_type_, const int extruder_id_, const int nozzle_count_)
        : diameter(nozzle_diameter_), volume_type(volume_type_), extruder_id(extruder_id_), nozzle_count(nozzle_count_)
    {}

    inline bool operator<(const NozzleGroupInfo &rhs) const
    {
        if (extruder_id != rhs.extruder_id) return extruder_id < rhs.extruder_id;
        if (diameter != rhs.diameter) return diameter < rhs.diameter;
        if (volume_type != rhs.volume_type) return volume_type < rhs.volume_type;
        return nozzle_count < rhs.nozzle_count;
    }

    bool is_same_type(const NozzleGroupInfo &rhs) const
    {
        return diameter == rhs.diameter && volume_type == rhs.volume_type && extruder_id == rhs.extruder_id;
    }

    inline bool operator==(const NozzleGroupInfo &rhs) const
    {
        return diameter == rhs.diameter && volume_type == rhs.volume_type && extruder_id == rhs.extruder_id && nozzle_count == rhs.nozzle_count;
    }

    std::string serialize() const;
    static std::optional<NozzleGroupInfo> deserialize(const std::string& str);
};

// 分组后的结果，GCodeProcessor，发起打印页面需要
class MultiNozzleGroupResult
{
private:
    std::vector<std::vector<int>> layer_filament_nozzle_maps;  // 每一层的filament_nozzle_map
    std::vector<int> default_filament_nozzle_map;  // 默认的filament_nozzle_map
    std::vector<NozzleInfo> nozzle_list;      // 可用的逻辑喷嘴信息
    std::vector<unsigned int> used_filaments; // 所有使用的材料idx
    bool support_dynamic_nozzle_map {false};  // 是否支持动态喷嘴映射
    std::unordered_map<PrintObject*, ObjectLayerRange> object_layer_range; // 每个对象到全局的layer range
    bool is_by_object {false}; //是否为逐件
public:
    MultiNozzleGroupResult() = default;
    MultiNozzleGroupResult(const std::vector<int> &filament_nozzle_map, const std::vector<NozzleInfo> &nozzle_list, const std::vector<unsigned int>& used_filament);
    MultiNozzleGroupResult(const std::vector<std::vector<int>> &layer_filament_nozzle_maps, const std::vector<NozzleInfo> &nozzle_list, const std::vector<unsigned int>& used_filament);
    // 根据extruder_map构建group_result，场景：studio无选料器模式构建
    static std::optional<MultiNozzleGroupResult> init_from_slice_filament(const std::vector<int>          &filament_map,
                                                                          const std::vector<FilamentInfo> &filament_info); // 1 based filament_map

    // 根据nozzle_map,volume_map,extruder_map构建group_result，场景：H2C+无选料器模式+cli切片
    static std::optional<MultiNozzleGroupResult> init_from_cli_config(const std::vector<unsigned int>& used_filaments,
                                                                      const std::vector<int>& filament_map,
                                                                      const std::vector<int>& filament_volume_map,
                                                                      const std::vector<int>& filament_nozzle_map,
                                                                      const std::vector<std::map<NozzleVolumeType,int>>& nozzle_count,
                                                                      float diameter);

    // 构建支持动态喷嘴分配情况下的group_result，场景：H2C+选料器自动模式切片得到的切片文件
    static std::optional<MultiNozzleGroupResult> init_from_layer_filament_maps(const std::vector<std::vector<int>> &layer_filament_nozzle_maps,
                                                                                const std::vector<NozzleInfo> &nozzle_list,
                                                                                const std::vector<unsigned int>& used_filament);

    const std::vector<std::vector<int>>& get_layer_filament_nozzle_maps() const { return layer_filament_nozzle_maps; }

    bool                                         are_filaments_same_extruder(int filament_id1, int filament_id2, int layer_id = -1, const PrintObject* obj = nullptr) const;    // 判断两个材料是否处于同一个挤出机
    bool                                         are_filaments_same_nozzle(int filament_id1, int filament_id2, int layer_id = -1, const PrintObject* obj = nullptr) const;      // 判断两个材料是否处于同一个喷嘴
    int                                          get_extruder_count() const;                                               // 获取挤出机数量

    std::vector<NozzleInfo> get_used_nozzles_in_extruder(int target_extruder_id = -1, int layer_id = -1, const PrintObject* obj = nullptr) const; // 获取指定挤出机使用的喷嘴，layer_id=-1时使用默认映射
    std::vector<int>        get_used_extruders(int layer_id = -1, const PrintObject* obj = nullptr) const;                            // 获取使用的挤出机列表，layer_id=-1时返回全局挤出机

    std::vector<int>        get_extruder_map(bool zero_based = true, int layer_id = -1, const PrintObject* obj = nullptr) const; // 获取指定层的挤出机映射
    std::vector<int>        get_nozzle_map(int layer_id = -1, const PrintObject* obj = nullptr) const; // 获取指定层的喷嘴映射
    std::vector<int>        get_volume_map(int layer_id = -1, const PrintObject* obj = nullptr) const; // 获取指定层的体积类型映射
    std::vector<unsigned int> get_used_filaments() const { return used_filaments;}
    std::vector<unsigned int> get_used_filaments(int layer_id, const PrintObject* obj = nullptr) const; // 获取指定层使用的耗材列表

    bool   is_support_dynamic_nozzle_map() const { return support_dynamic_nozzle_map; }

    /**
     * @brief 预估给定序列的冲刷重量
     *
     * @param flush_matrix 换挤出机的矩阵，挤出机-起始耗材-结束耗材
     * @param filament_change_seq 换料序列
     * @return int 冲刷重量
     */
    int estimate_seq_flush_weight(const std::vector<std::vector<std::vector<float>>>& flush_matrix, const std::vector<int>& filament_change_seq) const;


    std::vector<NozzleInfo>   get_nozzles_for_filament(int filament_id, const PrintObject* obj = nullptr) const; // 获取耗材使用的所有逻辑喷嘴信息
    std::optional<NozzleInfo> get_nozzle_for_filament(int filament_id, int layer_id = -1, const PrintObject* obj = nullptr) const; // 获取指定层、特定耗材使用的逻辑喷嘴
    std::optional<NozzleInfo> get_nozzle_by_id(int nozzle_id) const;

    int get_extruder_id(int filament_id, int layer_id = -1, const PrintObject* obj = nullptr) const;
    int get_nozzle_id(int filament_id, int layer_id = -1, const PrintObject* obj = nullptr) const;

    std::unordered_map<const PrintConfig*, std::vector<int>> config_idx_map;

private:
    const std::vector<int>& get_filament_nozzle_map(int layer_id) const;
    const std::vector<int>& get_filament_nozzle_map(int layer_id, const PrintObject* obj) const;
    int convert_to_global_layer_id(int layer_id, const PrintObject* obj) const;
};

class NozzleStatusRecorder
{
private:
    std::unordered_map<int, int> nozzle_filament_status; // Track filament in each nozzle
    std::unordered_map<int, int> extruder_nozzle_status; // Track nozzle for each extruder

public:
    NozzleStatusRecorder() = default;
    bool is_nozzle_empty(int nozzle_id) const;
    int  get_filament_in_nozzle(int nozzle_id) const;
    int  get_nozzle_in_extruder(int extruder_id) const;

    void clear_nozzle_status(int nozzle_id);
    
    // Update the status of a nozzle with new filament and extruder information
    void set_nozzle_status(int nozzle_id, int filament_id, int extruder_id = -1);
};


std::vector<NozzleInfo> build_nozzle_list(std::vector<NozzleGroupInfo> info);
std::vector<NozzleInfo> build_nozzle_list(double diameter,const std::vector<int>& filament_nozzle_map, const std::vector<int>& filament_volume_map, const std::vector<int>& filament_map);


} // namespace MultiNozzleUtils
} // namespace Slic3r

#endif
