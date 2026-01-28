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
    std::string      diameter;
    NozzleVolumeType volume_type;
    int              extruder_id{-1}; // 逻辑挤出机id
    int              group_id{-1};   // 对应逻辑喷嘴id

    std::string serialize() const;

    bool operator<(const NozzleInfo& other) const {
        if(group_id != other.group_id) return group_id < other.group_id;
        if(extruder_id != other.extruder_id) return extruder_id < other.extruder_id;
        if(volume_type != other.volume_type) return volume_type < other.volume_type;
        return diameter < other.diameter;
    }

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

struct FilamentChangeTimeParams
{
    float selector_load_time{0.0f};
    float selector_unload_time{0.0f};
    float standard_load_time{0.0f};
    float standard_unload_time{0.0f};
};

/**
 * @brief 喷嘴分组结果的虚基类
 */
class NozzleGroupResultBase
{
protected:
    bool support_dynamic_nozzle_map{false}; // 是否支持动态映射(选料器)

public:
    NozzleGroupResultBase(bool support_dynamic_map = false) : support_dynamic_nozzle_map(support_dynamic_map) {}
    virtual ~NozzleGroupResultBase() = default;

    virtual std::optional<NozzleInfo> get_nozzle_from_id(int nozzle_id) const = 0; // 根据喷嘴id获取NozzleInfo
    virtual std::optional<NozzleInfo> get_first_nozzle_for_filament(int filament_id) const = 0; // 指定耗材首次使用的逻辑喷嘴信息

    virtual std::vector<NozzleInfo> get_nozzles_for_filament(int filament_id) const = 0; // 获取耗材可能使用的所有喷嘴（跨所有层）

    bool is_support_dynamic_nozzle_map() const { return support_dynamic_nozzle_map; }
    
    virtual int get_extruder_count() const = 0; // 获取挤出机数量

    virtual std::vector<NozzleInfo> get_used_nozzles_in_extruder(int extruder_id =-1) const = 0;
    virtual std::vector<int> get_used_extruders() const = 0; // 获取使用的挤出机列表
    virtual std::vector<unsigned int> get_used_filaments() const = 0 ; // 获取使用的耗材列表
};

/**
 * @brief 有layer信息的喷嘴分组结果
 * 用于后端切片代码，支持逐层的喷嘴映射。
 */
class LayeredNozzleGroupResult : public NozzleGroupResultBase
{
private:
    std::vector<std::vector<int>>                      _layer_filament_nozzle_maps; // 每一层的 filament -> nozzle 映射
    std::vector<std::vector<unsigned int>>             _layer_filament_sequences; // 每一层使用耗材的顺序
    std::vector<int>                                   _default_filament_nozzle_map; // 默认映射，全局材料
    std::vector<unsigned int>                          _used_filaments; // 所有使用的材料idx
    std::vector<NozzleInfo>                            _nozzle_list; // 全局的喷嘴列表

public:
    LayeredNozzleGroupResult(bool support_dynamic_map = false) : NozzleGroupResultBase(support_dynamic_map) {}

    // 无选料器：全局使用一份 filament->nozzle
    static std::optional<LayeredNozzleGroupResult> create(
        const std::vector<int>&          filament_nozzle_map,
        const std::vector<NozzleInfo>&   nozzle_list,
        const std::vector<unsigned int>& used_filaments);

    // 有选料器：从逐层映射构建（每层可能不同）
    static std::optional<LayeredNozzleGroupResult> create(
        const std::vector<std::vector<int>>&          layer_filament_nozzle_maps,
        const std::vector<NozzleInfo>&                nozzle_list,
        const std::vector<unsigned int>&              used_filaments,
        const std::vector<std::vector<unsigned int>>& layer_filament_sequences);

    // O1C + 无选料器 + 命令行切片
    static std::optional<LayeredNozzleGroupResult> create(
        const std::vector<unsigned int>&                    used_filaments,
        const std::vector<int>&                             filament_map,
        const std::vector<int>&                             filament_volume_map,
        const std::vector<int>&                             filament_nozzle_map,
        const std::vector<std::map<NozzleVolumeType, int>>& nozzle_count,
        float                                               diameter);

    bool are_filaments_same_extruder(int filament_id1, int filament_id2, int layer_id = -1) const; // 判断两个材料是否处于同一个挤出机
    bool are_filaments_same_nozzle(int filament_id1, int filament_id2, int layer_id = -1) const; // 判断两个材料是否处于同一个喷嘴
    int get_extruder_count() const override; // 获取挤出机数量

    std::vector<NozzleInfo> get_used_nozzles_in_extruder(int target_extruder_id = -1) const override;
    std::vector<NozzleInfo> get_used_nozzles_in_extruder(int target_extruder_id, int layer_id) const; // 获取指定挤出机使用的喷嘴，layer_id=-1时使用默认映射
    std::vector<int> get_used_extruders() const override;
    std::vector<int> get_used_extruders(int layer_id) const; // 获取使用的挤出机列表，layer_id=-1时返回全局挤出机

    std::vector<int> get_extruder_map(bool zero_based = true, int layer_id = -1) const; // 获取指定层的挤出机映射
    std::vector<int> get_nozzle_map(int layer_id = -1) const; // 获取耗材使用的所有逻辑喷嘴信息
    std::vector<int> get_volume_map(int layer_id = -1) const; // 获取指定层的体积类型映射

    std::vector<unsigned int> get_used_filaments() const override { return _used_filaments; }
    std::vector<unsigned int> get_used_filaments(int layer_id) const; // 获取指定层使用的耗材列表

    std::optional<NozzleInfo> get_nozzle_for_filament(int filament_id, int layer_id = -1) const; // 获取指定层、特定耗材使用的逻辑喷嘴
    std::vector<NozzleInfo> get_nozzles_for_filament(int filament_id) const override; // 获取耗材可能使用的所有喷嘴（跨所有层）

    std::optional<NozzleInfo> get_nozzle_from_id(int nozzle_id) const override; // 根据喷嘴id获取NozzleInfo
    std::optional<NozzleInfo> get_first_nozzle_for_filament(int filament_id) const override;
    int get_extruder_id(int filament_id, int layer_id = -1) const;
    int get_nozzle_id(int filament_id, int layer_id = -1) const;

    size_t get_layer_count() const { return _layer_filament_nozzle_maps.size(); }
    const std::vector<int>& get_layer_filament_nozzle_map(int layer_id) const;
    const std::vector<std::vector<int>> &get_layer_filament_nozzle_maps() const { return _layer_filament_nozzle_maps; }
    const std::vector<std::vector<unsigned int>>& get_layer_filament_sequences() const { return _layer_filament_sequences; }

    /**
     * @brief 预估给定序列的冲刷重量
     *
     * @param flush_matrix 换挤出机的矩阵，挤出机-起始耗材-结束耗材
     * @param filament_change_seq 换料序列
     * @return int 冲刷重量
     */
    int estimate_seq_flush_weight(const std::vector<std::vector<std::vector<float>>>& flush_matrix, const std::vector<int>& filament_change_seq) const;
};

/**
 * @brief 无layer信息的喷嘴分组结果
 * 用于设备端，只有静态的喷嘴映射。
 */
class StaticNozzleGroupResult : public NozzleGroupResultBase
{
private:
    std::map<int, std::set<int>> _filament_to_nozzles; // 每个材料可能映射到的所有喷嘴
    std::map<int, NozzleInfo>    _nozzle_list_map; // 使用的喷嘴
    std::vector<int>             _filament_change_seq; // 首次使用计算需要的耗材序列
    std::vector<int>             _nozzle_change_seq;   // 与耗材序列配对的逻辑喷嘴序列

public:
    StaticNozzleGroupResult(bool support_dynamic_map) : NozzleGroupResultBase(support_dynamic_map) {}
    // 从3mf加载，附带换料/换喷嘴序列
    static std::optional<StaticNozzleGroupResult> create(
        const std::vector<FilamentInfo>& filaments_info,
        const std::vector<NozzleInfo>&   nozzles_info,
        const std::vector<int>&          filament_change_seq,
        const std::vector<int>&          nozzle_change_seq,
        bool support_dynamic_map);

    int get_extruder_count() const override;
    std::vector<NozzleInfo> get_used_nozzles_in_extruder(int extruder_id = -1) const override;
    std::vector<int> get_used_extruders() const override;
    std::vector<unsigned int> get_used_filaments() const override;

    std::optional<NozzleInfo> get_nozzle_from_id(int nozzle_id) const override; // 根据喷嘴id获取NozzleInfo

    std::vector<NozzleInfo> get_nozzles_for_filament(int filament_id) const override; // 获取耗材可能使用的所有喷嘴（跨所有层）
    std::optional<NozzleInfo> get_first_nozzle_for_filament(int filament_id) const override;
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

float calc_filament_change_gap_for_assignment(
    const std::vector<int>&           logical_filaments,
    const std::vector<NozzleInfo>&    nozzle_list,
    const std::vector<int>&           filament_change_seq,
    const std::vector<int>&           nozzle_change_seq,
    const std::vector<int>&           group_of_filament,
    const FilamentChangeTimeParams&   time_params);

std::vector<int> find_optimal_physical_assignment(
    const std::vector<int>&           logical_filaments,
    const std::vector<NozzleInfo>&    nozzle_list,
    const std::vector<int>&           filament_change_seq,
    const std::vector<int>&           nozzle_change_seq,
    int                               group_count,
    const FilamentChangeTimeParams&   time_params);

// ==================== 工具函数 ====================
std::vector<NozzleInfo> build_nozzle_list(std::vector<NozzleGroupInfo> info);
std::vector<NozzleInfo> build_nozzle_list(double diameter, const std::vector<int>& filament_nozzle_map,
                                          const std::vector<int>& filament_volume_map, const std::vector<int>& filament_map);
// 从gcode.3mf中加载nozzle info需要处理前后兼容性
std::vector<NozzleInfo> load_nozzle_infos_with_compatibility(
    const std::vector<NozzleInfo>& nozzle_infos,
    const std::vector<FilamentInfo>& filament_infos,
    const std::vector<int>& filament_map,
    const std::vector<NozzleVolumeType>& extruder_volume_types,
    const std::vector<double>& nozzle_diameter
);
} // namespace MultiNozzleUtils
} // namespace Slic3r

#endif // MULTI_NOZZLE_UTILS_HPP
