#include "MultiNozzleUtils.hpp"
#include "ProjectTask.hpp"

namespace Slic3r { namespace MultiNozzleUtils {

MultiNozzleGroupResult::MultiNozzleGroupResult(const std::vector<int> &filament_nozzle_map, const std::vector<NozzleInfo> &nozzle_list)
{
    for (size_t filament_idx = 0; filament_idx < filament_nozzle_map.size(); ++filament_idx) {
        int               nozzle_id                             = filament_nozzle_map[filament_idx];
        const NozzleInfo &nozzle                                = nozzle_list[nozzle_id];
        int               extruder_id                           = nozzle.extruder_id;
        extruder_to_filament_nozzles[extruder_id][filament_idx] = nozzle;
    }
}

std::optional<MultiNozzleGroupResult> MultiNozzleGroupResult::init_from_slice_filament(const std::vector<int>& filament_map, const std::vector<FilamentInfo>& filament_info)
{
    std::map<int, NozzleInfo> nozzle_list_map;
    std::vector<NozzleInfo>   nozzle_list_vec;

    std::vector<int> filament_nozzle_map = filament_map;

    for (size_t idx = 0; idx < filament_info.size(); ++idx) {
        int         nozzle_idx   = filament_info[idx].group_id;
        int         filament_idx = filament_info[idx].id;
        int         extruder_idx = filament_map[filament_idx] - 1; // 0 based idx
        double      diameter     = filament_info[idx].nozzle_diameter;
        std::string volume_type  = filament_info[idx].nozzle_volume_type;
        if (nozzle_idx == -1) return std::nullopt; // Nozzle group id is not set, return empty optional

        NozzleInfo nozzle;
        nozzle.diameter    = diameter;
        nozzle.group_id    = nozzle_idx;
        nozzle.extruder_id = extruder_idx;
        nozzle.volume_type = NozzleVolumeType::nvtStandard;

        if (!nozzle_list_map.count(nozzle_idx)) nozzle_list_map[nozzle_idx] = nozzle;

        filament_nozzle_map[filament_idx] = nozzle_idx;
    }

    std::vector<int> new_filament_nozzle_map = filament_nozzle_map;

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

    return MultiNozzleGroupResult(new_filament_nozzle_map, nozzle_list_vec);
}

int MultiNozzleGroupResult::get_extruder_id(int filament_id) const
{
    for (auto &elem : extruder_to_filament_nozzles) {
        auto &filament_to_nozzle = elem.second;
        int   extruder_id        = elem.first;
        if (filament_to_nozzle.find(filament_id) == filament_to_nozzle.end()) continue;
        return extruder_id;
    }
    return -1;
}

std::optional<NozzleInfo> MultiNozzleGroupResult::get_nozzle_for_filament(int filament_id) const
{
    for (auto &elem : extruder_to_filament_nozzles) {
        auto &filament_to_nozzle = elem.second;
        int   extruder_id        = elem.first;
        auto  iter               = filament_to_nozzle.find(filament_id);
        if (iter == filament_to_nozzle.end()) continue;
        return iter->second;
    }
    return std::nullopt;
}

bool MultiNozzleGroupResult::are_filaments_same_extruder(int filament_id1, int filament_id2) const
{
    int extruder_id1 = get_extruder_id(filament_id1);
    int extruder_id2 = get_extruder_id(filament_id2);

    if (extruder_id1 == -1 || extruder_id2 == -1) return false;

    return extruder_id1 == extruder_id2;
}

bool MultiNozzleGroupResult::are_filaments_same_nozzle(int filament_id1, int filament_id2) const
{
    std::optional<NozzleInfo> nozzle_info1 = get_nozzle_for_filament(filament_id1);
    std::optional<NozzleInfo> nozzle_info2 = get_nozzle_for_filament(filament_id2);
    if (!nozzle_info1 || !nozzle_info2) return false;

    return nozzle_info1->group_id == nozzle_info2->group_id;
}

int MultiNozzleGroupResult::get_extruder_count() const { return static_cast<int>(extruder_to_filament_nozzles.size()); }

int MultiNozzleGroupResult::get_nozzle_count(int target_extruder_id) const
{
    std::set<int> nozzles;
    for (auto &elem : extruder_to_filament_nozzles) {
        auto &filament_to_nozzle = elem.second;
        int   extruder_id        = elem.first;

        if (target_extruder_id == -1 || extruder_id == target_extruder_id) {
            for (auto &filament_nozzle : filament_to_nozzle) { nozzles.insert(filament_nozzle.second.group_id); }
        }
    }
    return static_cast<int>(nozzles.size());
}

std::vector<NozzleInfo> MultiNozzleGroupResult::get_used_nozzles(const std::vector<unsigned int> &filament_list, int target_extruder_id) const
{
    std::set<int> used_nozzles;
    for (auto filament : filament_list) {
        int filament_idx = static_cast<int>(filament);
        int extruder_id  = get_extruder_id(filament_idx);
        if (extruder_id != -1 && extruder_id != target_extruder_id) continue;

        auto nozzle_info = get_nozzle_for_filament(filament_idx);
        if (nozzle_info) used_nozzles.insert(nozzle_info->group_id);
    }

    std::vector<NozzleInfo> result;
    for (auto nozzle_id : used_nozzles) {
        for (auto &elem : extruder_to_filament_nozzles) {
            auto &filament_to_nozzle = elem.second;
            for (auto &nozzle : filament_to_nozzle) {
                if (nozzle.second.group_id == nozzle_id) {
                    result.push_back(nozzle.second);
                    break;
                }
            }
        }
    }
    return result;
}

std::vector<int> MultiNozzleGroupResult::get_used_extruders(const std::vector<unsigned int> &filament_list) const
{
    std::set<int> used_extruders;
    for (auto filament : filament_list) {
        int filament_idx = static_cast<int>(filament);
        int extruder_id  = get_extruder_id(filament_idx);
        if (extruder_id == -1) continue;
        used_extruders.insert(extruder_id);
    }
    return std::vector<int>(used_extruders.begin(), used_extruders.end());
}

std::vector<int> MultiNozzleGroupResult::get_extruder_list() const
{
    std::set<int> extruder_list;
    for (auto &elem : extruder_to_filament_nozzles) { extruder_list.insert(elem.first); }
    return std::vector<int>(extruder_list.begin(), extruder_list.end());
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


void NozzleStatusRecorder::set_nozzle_status(int nozzle_id, int filament_id)
{
    nozzle_filament_status[nozzle_id] = filament_id;
}

void NozzleStatusRecorder::clear_nozzle_status(int nozzle_id)
{
    auto iter = nozzle_filament_status.find(nozzle_id);
    if (iter == nozzle_filament_status.end()) return;
    nozzle_filament_status.erase(iter);
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
        double           diameter     = std::stod(tokens[1]);
        NozzleVolumeType volume_type = NozzleVolumeType(ConfigOptionEnum<NozzleVolumeType>::get_enum_values().at(tokens[2]));
        int              nozzle_count = std::stoi(tokens[3]);

        return NozzleGroupInfo(diameter, volume_type, extruder_id, nozzle_count);
    } catch (const std::exception &) {
        return std::nullopt;
    }
}

}} // namespace Slic3r::MultiNozzleUtils