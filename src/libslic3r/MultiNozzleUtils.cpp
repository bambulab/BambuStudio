#include "MultiNozzleUtils.hpp"
#include "ProjectTask.hpp"
#include "Utils.hpp"
#include "Print.hpp"

namespace Slic3r { namespace MultiNozzleUtils {

std::vector<NozzleInfo> build_nozzle_list(std::vector<NozzleGroupInfo> nozzle_groups)
{
    std::vector<NozzleInfo> ret;
    std::sort(nozzle_groups.begin(), nozzle_groups.end());
    int nozzle_id = 0;
    for (auto& group : nozzle_groups) {
        for (int i = 0; i < group.nozzle_count; ++i) {
            NozzleInfo tmp;
            tmp.diameter = group.diameter;
            tmp.extruder_id = group.extruder_id;
            tmp.volume_type = group.volume_type;
            tmp.group_id = nozzle_id++;
            ret.emplace_back(std::move(tmp));
        }
    }
    return ret;
}

std::vector<NozzleInfo> build_nozzle_list(double diameter, const std::vector<int>& filament_nozzle_map, const std::vector<int>& filament_volume_map, const std::vector<int>& filament_map)
{
    std::string diameter_str = format_diameter_to_str(diameter);
    std::map<int, std::vector<int>> nozzle_to_filaments;
    for(size_t idx = 0; idx < filament_nozzle_map.size(); ++idx){
        int nozzle_id = filament_nozzle_map[idx];
        nozzle_to_filaments[nozzle_id].emplace_back(static_cast<int>(idx));
    }
    std::vector<NozzleInfo> ret;
    for(auto& elem : nozzle_to_filaments){
        int nozzle_id = elem.first;
        auto& filaments = elem.second;
        NozzleInfo info;
        info.diameter = diameter_str;
        info.group_id = nozzle_id;
        info.extruder_id = filament_map[filaments.front()];
        info.volume_type = NozzleVolumeType(filament_volume_map[filaments.front()]);
        ret.emplace_back(std::move(info));
    }
    return ret;
}


MultiNozzleGroupResult::MultiNozzleGroupResult(const std::vector<int> &filament_nozzle_map_, const std::vector<NozzleInfo> &nozzle_list_, const std::vector<unsigned int>& used_filaments_)
{
    default_filament_nozzle_map = filament_nozzle_map_;
    nozzle_list = nozzle_list_;
    used_filaments = used_filaments_;
    support_dynamic_nozzle_map = false;
}

MultiNozzleGroupResult::MultiNozzleGroupResult(const std::vector<std::vector<int>> &layer_filament_nozzle_maps_, const std::vector<NozzleInfo> &nozzle_list_, const std::vector<unsigned int>& used_filaments_)
{
    layer_filament_nozzle_maps = layer_filament_nozzle_maps_;
    nozzle_list = nozzle_list_;
    used_filaments = used_filaments_;
    support_dynamic_nozzle_map = true;
    
    if (!layer_filament_nozzle_maps.empty()) {
        default_filament_nozzle_map = layer_filament_nozzle_maps[0];
    }
}

std::optional<MultiNozzleGroupResult> MultiNozzleGroupResult::init_from_slice_filament(const std::vector<int>& filament_map, const std::vector<FilamentInfo>& filament_info)
{
    if (filament_map.empty())
        return std::nullopt;

    std::map<int, NozzleInfo> nozzle_list_map;
    std::vector<int> filament_nozzle_map = filament_map;
    std::map<int, std::vector<int>> group_ids_in_extruder;
    std::vector<unsigned int> used_filaments;

    auto volume_type_str_to_enum = ConfigOptionEnum<NozzleVolumeType>::get_enum_values();

    // used filaments
    for (size_t idx = 0; idx < filament_info.size(); ++idx) {
        int         nozzle_idx   = filament_info[idx].group_id.empty() ? -1 : filament_info[idx].group_id.front();
        int         filament_idx = filament_info[idx].id;
        int         extruder_idx = filament_map[filament_idx] - 1; // 0 based idx
        double      diameter     = filament_info[idx].nozzle_diameter;
        std::string volume_type  = filament_info[idx].nozzle_volume_type;
        if (nozzle_idx == -1) return std::nullopt; // Nozzle group id is not set, return empty optional

        used_filaments.emplace_back(filament_idx);
        group_ids_in_extruder[extruder_idx].emplace_back(nozzle_idx);

        NozzleInfo nozzle;
        nozzle.diameter    = format_diameter_to_str(diameter);
        nozzle.group_id    = nozzle_idx;
        nozzle.extruder_id = extruder_idx;
        nozzle.volume_type = NozzleVolumeType(volume_type_str_to_enum[volume_type]);
        if (!nozzle_list_map.count(nozzle_idx)) nozzle_list_map[nozzle_idx] = nozzle;

        filament_nozzle_map[filament_idx] = nozzle_idx;
    }

    // handle unused filaments to build some unused nozzles
    for (size_t idx = 0; idx < filament_map.size(); ++idx) {
        auto iter = std::find_if(filament_info.begin(), filament_info.end(), [idx](const FilamentInfo& info) {
            return info.id == static_cast<int>(idx);
            });
        if (iter != filament_info.end())
            continue;

        int extruder_idx = filament_map[idx] - 1;
        if (group_ids_in_extruder.count(extruder_idx)) {
            // reuse one nozzle in current extruder
            filament_nozzle_map[idx] = group_ids_in_extruder[extruder_idx].front();
        }
        else {
            // create a new nozzle
            int max_nozzle_idx = 0;
            for (auto& nozzle_groups : group_ids_in_extruder) {
                for (auto group_id : nozzle_groups.second)
                    max_nozzle_idx = std::max(max_nozzle_idx, group_id);
            }

            NozzleInfo nozzle;
            nozzle.diameter = nozzle_list_map[max_nozzle_idx].diameter;
            nozzle.volume_type = nozzle_list_map[max_nozzle_idx].volume_type;
            nozzle.group_id = max_nozzle_idx + 1;
            nozzle.extruder_id = extruder_idx;

            nozzle_list_map[max_nozzle_idx + 1] = nozzle;

            filament_nozzle_map[idx] = max_nozzle_idx + 1;
        }

    }


    std::vector<int> new_filament_nozzle_map = filament_nozzle_map;
    std::vector<NozzleInfo>   nozzle_list_vec;

    // reset group id for nozzles
    for (auto &elem : nozzle_list_map) {
        int  nozzle_id       = elem.first;
        auto nozzle_info     = elem.second;
        nozzle_info.group_id = nozzle_list_vec.size();
        nozzle_list_vec.emplace_back(nozzle_info);
        for (size_t idx = 0; idx < filament_nozzle_map.size(); ++idx) {
            if (filament_nozzle_map[idx] == nozzle_id) new_filament_nozzle_map[idx] = nozzle_list_vec.size() - 1;
        }
    }

    if(new_filament_nozzle_map.empty() || nozzle_list_vec.empty())
        return std::nullopt;

    return MultiNozzleGroupResult(new_filament_nozzle_map, nozzle_list_vec, used_filaments);
}

std::optional<MultiNozzleGroupResult> MultiNozzleGroupResult::init_from_cli_config(const std::vector<unsigned int>& used_filaments,const std::vector<int>& filament_map, const std::vector<int>& filament_volume_map, const std::vector<int>& filament_nozzle_map, const std::vector<std::map<NozzleVolumeType, int>>& nozzle_count, float diameter)
{
    std::vector<MultiNozzleUtils::NozzleGroupInfo> nozzle_groups;
    for(size_t extruder_id = 0; extruder_id < nozzle_count.size(); ++extruder_id){
        for (auto elem : nozzle_count[extruder_id]) {
            MultiNozzleUtils::NozzleGroupInfo group_info;
            group_info.diameter = format_diameter_to_str(diameter);
            group_info.volume_type = elem.first;
            group_info.nozzle_count = elem.second;
            group_info.extruder_id = extruder_id;
            nozzle_groups.emplace_back(group_info);
        }
    }

    auto nozzle_list = build_nozzle_list(nozzle_groups);
    std::vector<bool> used_nozzle(nozzle_list.size(),false);

    std::map<int,int> input_nozzle_id_to_output;

    std::vector<int> output_nozzle_map(filament_nozzle_map.size(),0);

    for(auto filament_idx : used_filaments){
        NozzleVolumeType req_type = NozzleVolumeType(filament_volume_map[filament_idx]);
        int req_extruder = filament_map[filament_idx];
        int input_nozzle_idx = filament_nozzle_map[filament_idx];

        if(input_nozzle_id_to_output.find(input_nozzle_idx) != input_nozzle_id_to_output.end()){
            output_nozzle_map[filament_idx] = input_nozzle_id_to_output[input_nozzle_idx];
            continue;
        }

        int output_nozzle_idx = -1;
        for(size_t nozzle_idx = 0; nozzle_idx < nozzle_list.size(); ++nozzle_idx){
            if(used_nozzle[nozzle_idx])
                continue;
            auto& nozzle_info = nozzle_list[nozzle_idx];
            if(!(nozzle_info.extruder_id == req_extruder&& nozzle_info.volume_type == req_type))
                continue;

            output_nozzle_idx = nozzle_idx;
            input_nozzle_id_to_output[input_nozzle_idx] = output_nozzle_idx;
            used_nozzle[nozzle_idx] = true;
            break;
        }

        if(output_nozzle_idx == -1){
            return std::nullopt;
        }
        output_nozzle_map[filament_idx] = output_nozzle_idx;
    }

    return MultiNozzleGroupResult(output_nozzle_map,nozzle_list,used_filaments);
}

std::optional<MultiNozzleGroupResult> MultiNozzleGroupResult::init_from_layer_filament_maps(const std::vector<std::vector<int>> &layer_filament_nozzle_maps, const std::vector<NozzleInfo> &nozzle_list, const std::vector<unsigned int>& used_filament)
{
    if (layer_filament_nozzle_maps.empty() || nozzle_list.empty())
        return std::nullopt;

    return MultiNozzleGroupResult(layer_filament_nozzle_maps, nozzle_list, used_filament);
}

const std::vector<int>& MultiNozzleGroupResult::get_filament_nozzle_map(int layer_id) const
{
    if (support_dynamic_nozzle_map && layer_id >= 0 && layer_id < static_cast<int>(layer_filament_nozzle_maps.size())) {
        return layer_filament_nozzle_maps[layer_id];
    }
    return default_filament_nozzle_map;
}

std::optional<NozzleInfo> MultiNozzleGroupResult::get_nozzle_for_filament(int filament_id, int layer_id) const
{
    const std::vector<int>& filament_nozzle_map = get_filament_nozzle_map(layer_id);
    if (filament_id < 0 || filament_id >= static_cast<int>(filament_nozzle_map.size())) {
        return std::nullopt;
    }
    int nozzle_id = filament_nozzle_map[filament_id];
    if (nozzle_id < 0 || nozzle_id >= static_cast<int>(nozzle_list.size())) {
        return std::nullopt;
    }
    return nozzle_list[nozzle_id];
}

std::optional<NozzleInfo> MultiNozzleGroupResult::get_nozzle_by_id(int nozzle_id) const
{
    if (nozzle_id < 0 || nozzle_id >= static_cast<int>(nozzle_list.size())) {
        return std::nullopt;
    }
    return nozzle_list[nozzle_id];
}

bool MultiNozzleGroupResult::are_filaments_same_extruder(int filament_id1, int filament_id2, int layer_id) const
{
    std::optional<NozzleInfo> nozzle_info1 = get_nozzle_for_filament(filament_id1, layer_id);
    std::optional<NozzleInfo> nozzle_info2 = get_nozzle_for_filament(filament_id2, layer_id);

    if (!nozzle_info1 || !nozzle_info2) return false;

    return nozzle_info1->extruder_id == nozzle_info2->extruder_id;
}

bool MultiNozzleGroupResult::are_filaments_same_nozzle(int filament_id1, int filament_id2, int layer_id) const
{
    std::optional<NozzleInfo> nozzle_info1 = get_nozzle_for_filament(filament_id1, layer_id);
    std::optional<NozzleInfo> nozzle_info2 = get_nozzle_for_filament(filament_id2, layer_id);
    if (!nozzle_info1 || !nozzle_info2) return false;

    return nozzle_info1->group_id == nozzle_info2->group_id;
}

int MultiNozzleGroupResult::get_extruder_count() const
{
    std::set<int> extruder_ids;
    for (const auto& nozzle : nozzle_list) {
        extruder_ids.insert(nozzle.extruder_id);
    }
    return static_cast<int>(extruder_ids.size());
}

std::vector<NozzleInfo> MultiNozzleGroupResult::get_used_nozzles_in_extruder(int target_extruder_id, int layer_id) const
{
    std::set<int> nozzle_ids;
    std::vector<NozzleInfo> result;

    std::vector<unsigned int> target_filaments = get_used_filaments(layer_id);

    for (unsigned int filament_id : target_filaments) {
        if (layer_id != -1) {
            auto nozzle_opt = get_nozzle_for_filament(static_cast<int>(filament_id), layer_id);
            if (nozzle_opt) {
                if (target_extruder_id == -1 || nozzle_opt->extruder_id == target_extruder_id) {
                    nozzle_ids.insert(nozzle_opt->group_id);
                }
            }
        } else {
            auto nozzles = get_nozzles_for_filament(static_cast<int>(filament_id));
            for (const auto& nozzle : nozzles) {
                if (target_extruder_id == -1 || nozzle.extruder_id == target_extruder_id) {
                    nozzle_ids.insert(nozzle.group_id);
                }
            }
        }
    }
    for (int nozzle_id : nozzle_ids) {
        result.push_back(nozzle_list[nozzle_id]);
    }
    return result;
}

std::vector<int> MultiNozzleGroupResult::get_used_extruders(int layer_id) const
{
    std::set<int> used_extruders;
    // 获取指定层（或全局）使用的耗材列表
    std::vector<unsigned int> target_filaments = get_used_filaments(layer_id);
    
    for (auto filament_id : target_filaments) {
        if (layer_id != -1) {
            // 单层模式：获取该层特定耗材对应的喷嘴
            auto nozzle_opt = get_nozzle_for_filament(static_cast<int>(filament_id), layer_id);
            if (nozzle_opt) {
                used_extruders.insert(nozzle_opt->extruder_id);
            }
        } else {
            // 全局模式：获取该耗材在所有层使用的所有喷嘴
            auto nozzles = get_nozzles_for_filament(static_cast<int>(filament_id));
            for (const auto& nozzle : nozzles) {
                used_extruders.insert(nozzle.extruder_id);
            }
        }
    }
    return std::vector<int>(used_extruders.begin(), used_extruders.end());
}

std::vector<unsigned int> MultiNozzleGroupResult::get_used_filaments(int layer_id) const
{
    if (!support_dynamic_nozzle_map || layer_id < 0) {
        return used_filaments;
    }

    if (layer_id >= static_cast<int>(layer_filament_nozzle_maps.size())) {
        return used_filaments;
    }
    
    const std::vector<int>& filament_nozzle_map = layer_filament_nozzle_maps[layer_id];
    std::set<unsigned int> layer_used_filaments;
    
    for (size_t idx = 0; idx < filament_nozzle_map.size(); ++idx) {
        if (std::find(used_filaments.begin(), used_filaments.end(), static_cast<unsigned int>(idx)) != used_filaments.end()) {
            layer_used_filaments.insert(static_cast<unsigned int>(idx));
        }
    }
    
    return std::vector<unsigned int>(layer_used_filaments.begin(), layer_used_filaments.end());
}

bool NozzleStatusRecorder::is_nozzle_empty(int nozzle_id) const
{
    auto iter = nozzle_filament_status.find(nozzle_id);
    if (iter == nozzle_filament_status.end()) return true;
    return false;
}

int NozzleStatusRecorder::get_filament_in_nozzle(int nozzle_id) const
{
    auto iter = nozzle_filament_status.find(nozzle_id);
    if (iter == nozzle_filament_status.end()) return -1;
    return iter->second;
}

int NozzleStatusRecorder::get_nozzle_in_extruder(int extruder_id) const
{
    auto iter = extruder_nozzle_status.find(extruder_id);
    if (iter == extruder_nozzle_status.end()) return -1;
    return iter->second;
}



void NozzleStatusRecorder::set_nozzle_status(int nozzle_id, int filament_id, int extruder_id)
{
    nozzle_filament_status[nozzle_id] = filament_id;
    if (extruder_id != -1) {
        extruder_nozzle_status[extruder_id] = nozzle_id;
    }
}

void NozzleStatusRecorder::clear_nozzle_status(int nozzle_id)
{
    auto iter = nozzle_filament_status.find(nozzle_id);
    if (iter == nozzle_filament_status.end()) return;
    nozzle_filament_status.erase(iter);
}

std::string NozzleInfo::serialize() const
{
    std::ostringstream oss;
    oss << "id=\"" << group_id << "\" "
        << "extruder_id=\"" << extruder_id << "\" "
        << "nozzle_diameter=\"" << diameter << "\" "
        << "volume_type=\"" << get_nozzle_volume_type_string(volume_type) << "\"";
    return oss.str();
}

std::optional<NozzleInfo> NozzleInfo::deserialize(const std::string& str)
{
    NozzleInfo result;
    std::string remaining = str;
    
    while (!remaining.empty()) {
        size_t key_end = remaining.find('=');
        if (key_end == std::string::npos) break;
        
        std::string key = remaining.substr(0, key_end);
        remaining = remaining.substr(key_end + 1);
        
        if (remaining.empty() || remaining[0] != '\"') break;
        remaining = remaining.substr(1);
        
        size_t value_end = remaining.find('\"');
        if (value_end == std::string::npos) break;
        
        std::string value = remaining.substr(0, value_end);
        remaining = remaining.substr(value_end + 1);
        
        while (!remaining.empty() && (remaining[0] == ' ' || remaining[0] == '\t')) {
            remaining = remaining.substr(1);
        }
        
        if (key == "id") {
            try {
                result.group_id = std::stoi(value);
            } catch (...) {
                return std::nullopt;
            }
        } else if (key == "extruder_id") {
            try {
                result.extruder_id = std::stoi(value);
            } catch (...) {
                return std::nullopt;
            }
        } else if (key == "nozzle_diameter") {
            result.diameter = value;
        } else if (key == "volume_type") {
            try {
                result.volume_type = NozzleVolumeType(ConfigOptionEnum<NozzleVolumeType>::get_enum_values().at(value));
            } catch (...) {
                return std::nullopt;
            }
        }
    }
    
    if (result.diameter.empty() || result.extruder_id == -1) {
        return std::nullopt;
    }
    
    return result;
}

std::string NozzleGroupInfo::serialize() const
{
    std::ostringstream oss;
    oss << extruder_id << "-"
        << std::setprecision(2) << diameter << "-"
        << get_nozzle_volume_type_string(volume_type)<<"-"
        << nozzle_count;
    return oss.str();
}

std::optional<NozzleGroupInfo> NozzleGroupInfo::deserialize(const std::string &str)
{
    std::istringstream       iss(str);
    std::string              token;
    std::vector<std::string> tokens;

    while (std::getline(iss, token, '-')) { tokens.push_back(token); }

    if (tokens.size() != 4) { return std::nullopt; }

    try {
        int              extruder_id  = std::stoi(tokens[0]);
        std::string      diameter = tokens[1];
        NozzleVolumeType volume_type = NozzleVolumeType(ConfigOptionEnum<NozzleVolumeType>::get_enum_values().at(tokens[2]));
        int              nozzle_count = std::stoi(tokens[3]);

        return NozzleGroupInfo(diameter, volume_type, extruder_id, nozzle_count);
    } catch (const std::exception &) {
        return std::nullopt;
    }
}

std::vector<int> MultiNozzleGroupResult::get_extruder_map(bool zero_based, int layer_id) const
{
    const std::vector<int>& filament_nozzle_map = get_filament_nozzle_map(layer_id);
    std::vector<int> extruder_map(filament_nozzle_map.size());
    for (size_t idx = 0; idx < filament_nozzle_map.size(); ++idx) {
        int nozzle_id = filament_nozzle_map[idx];
        if (nozzle_id >= 0 && nozzle_id < static_cast<int>(nozzle_list.size())) {
            extruder_map[idx] = nozzle_list[nozzle_id].extruder_id;
        } else {
            extruder_map[idx] = -1;
        }
    }

    if(zero_based)
        return extruder_map;

    auto new_filament_map = extruder_map;
    std::transform(new_filament_map.begin(), new_filament_map.end(), new_filament_map.begin(), [](int val) { return val + 1;  });
    return new_filament_map;
}

std::vector<int> MultiNozzleGroupResult::get_nozzle_map(int layer_id) const
{
    const std::vector<int>& filament_nozzle_map = get_filament_nozzle_map(layer_id);
    std::vector<int> nozzle_map(filament_nozzle_map.size());
    for (size_t idx = 0; idx < filament_nozzle_map.size(); ++idx) {
        int nozzle_id = filament_nozzle_map[idx];
        if (nozzle_id >= 0 && nozzle_id < static_cast<int>(nozzle_list.size())) {
            nozzle_map[idx] = nozzle_list[nozzle_id].group_id;
        } else {
            nozzle_map[idx] = -1;
        }
    }
    return nozzle_map;
}

std::vector<int> MultiNozzleGroupResult::get_volume_map(int layer_id) const
{
    const std::vector<int>& filament_nozzle_map = get_filament_nozzle_map(layer_id);
    std::vector<int> volume_map(filament_nozzle_map.size());
    for (size_t idx = 0; idx < filament_nozzle_map.size(); ++idx) {
        int nozzle_id = filament_nozzle_map[idx];
        if (nozzle_id >= 0 && nozzle_id < static_cast<int>(nozzle_list.size())) {
            volume_map[idx] = nozzle_list[nozzle_id].volume_type;
        } else {
            volume_map[idx] = -1;
        }
    }
    return volume_map;
}



int MultiNozzleGroupResult::estimate_seq_flush_weight(const std::vector<std::vector<std::vector<float>>>& flush_matrix, const std::vector<int>& filament_change_seq) const
{
    auto get_weight_from_volume = [](float volume){
        return static_cast<int>(volume * 1.26 * 0.01);
    };

    float total_flush_volume = 0;
    MultiNozzleUtils::NozzleStatusRecorder recorder;
    for(auto filament: filament_change_seq){
        auto nozzle = get_nozzle_for_filament(filament);
        if(!nozzle)
            continue;

        int extruder_id = nozzle->extruder_id;
        int nozzle_id = nozzle->group_id;
        int last_filament = recorder.get_filament_in_nozzle(nozzle_id);

        if(last_filament!= -1 && last_filament != filament){
            float flush_volume = flush_matrix[extruder_id][last_filament][filament];
            total_flush_volume += flush_volume;
        }
        recorder.set_nozzle_status(nozzle_id, filament);
    }

    return get_weight_from_volume(total_flush_volume);
}

std::vector<NozzleInfo> MultiNozzleGroupResult::get_nozzles_for_filament(int filament_id) const
{
    std::set<int> nozzle_ids;

    if (!support_dynamic_nozzle_map) {
        if (filament_id >= 0 && filament_id < static_cast<int>(default_filament_nozzle_map.size())) {
            nozzle_ids.insert(default_filament_nozzle_map[filament_id]);
        }
    } else {
        int start_layer = 0;
        int end_layer = static_cast<int>(layer_filament_nozzle_maps.size());

        for (int i = start_layer; i < end_layer; ++i) {
             const auto& map = layer_filament_nozzle_maps[i];
             if (filament_id >= 0 && filament_id < static_cast<int>(map.size())) {
                 nozzle_ids.insert(map[filament_id]);
             }
        }
    }

    std::vector<NozzleInfo> result;
    for (int id : nozzle_ids) {
        if (id >= 0 && id < static_cast<int>(nozzle_list.size())) {
            result.push_back(nozzle_list[id]);
        }
    }
    return result;
}

int MultiNozzleGroupResult::get_extruder_id(int filament_id, int layer_id) const
{
    auto nozzle_info = get_nozzle_for_filament(filament_id, layer_id);
    return nozzle_info ? nozzle_info->extruder_id : -1;
}

int MultiNozzleGroupResult::get_nozzle_id(int filament_id, int layer_id) const
{
    auto nozzle_info = get_nozzle_for_filament(filament_id, layer_id);
    return nozzle_info ? nozzle_info->group_id : -1;
}


}} // namespace Slic3r::MultiNozzleUtils
