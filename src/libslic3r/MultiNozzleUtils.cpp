#include "MultiNozzleUtils.hpp"
#include "ProjectTask.hpp"
#include "Utils.hpp"

namespace Slic3r { namespace MultiNozzleUtils {

MultiNozzleGroupResult::MultiNozzleGroupResult(const std::vector<int> &filament_nozzle_map, const std::vector<NozzleInfo> &nozzle_list)
{
    filament_map = filament_nozzle_map;

    for (size_t filament_idx = 0; filament_idx < filament_nozzle_map.size(); ++filament_idx) {
        int               nozzle_id                             = filament_nozzle_map[filament_idx];
        const NozzleInfo &nozzle                                = nozzle_list[nozzle_id];
        int               extruder_id                           = nozzle.extruder_id;
        extruder_to_filament_nozzles[extruder_id][filament_idx] = nozzle;
        filament_map[filament_idx] = extruder_id;
    }
}

std::optional<MultiNozzleGroupResult> MultiNozzleGroupResult::init_from_slice_filament(const std::vector<int>& filament_map, const std::vector<FilamentInfo>& filament_info)
{
    if (filament_map.empty())
        return std::nullopt;

    std::map<int, NozzleInfo> nozzle_list_map;
    std::vector<int> filament_nozzle_map = filament_map;
    std::map<int, std::vector<int>> group_ids_in_extruder;

    auto volume_type_str_to_enum = ConfigOptionEnum<NozzleVolumeType>::get_enum_values();

    // used filaments
    for (size_t idx = 0; idx < filament_info.size(); ++idx) {
        int         nozzle_idx   = filament_info[idx].group_id;
        int         filament_idx = filament_info[idx].id;
        int         extruder_idx = filament_map[filament_idx] - 1; // 0 based idx
        double      diameter     = filament_info[idx].nozzle_diameter;
        std::string volume_type  = filament_info[idx].nozzle_volume_type;
        if (nozzle_idx == -1) return std::nullopt; // Nozzle group id is not set, return empty optional

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

std::vector<NozzleInfo>  MultiNozzleGroupResult::get_nozzle_vec(int target_extruder_id) const
{
    std::set<int> nozzles;
    std::vector<NozzleInfo> nozzleinfo_vec;
    for (auto& elem : extruder_to_filament_nozzles) {
        auto& filament_to_nozzle = elem.second;
        int   extruder_id = elem.first;

        if (target_extruder_id == -1 || extruder_id == target_extruder_id) {
            for (auto& filament_nozzle : filament_to_nozzle) {
                if (nozzles.count(filament_nozzle.second.group_id) == 0) {
                    nozzles.insert(filament_nozzle.second.group_id);
                    nozzleinfo_vec.push_back(filament_nozzle.second);
                }
            }
        }
    }
    return nozzleinfo_vec;
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
        std::string      diameter = tokens[1];
        NozzleVolumeType volume_type = NozzleVolumeType(ConfigOptionEnum<NozzleVolumeType>::get_enum_values().at(tokens[2]));
        int              nozzle_count = std::stoi(tokens[3]);

        return NozzleGroupInfo(diameter, volume_type, extruder_id, nozzle_count);
    } catch (const std::exception &) {
        return std::nullopt;
    }
}

}} // namespace Slic3r::MultiNozzleUtils