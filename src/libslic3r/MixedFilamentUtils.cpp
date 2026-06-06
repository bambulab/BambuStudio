#include "MixedFilamentUtils.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>
#include <utility>

#include "LocalesUtils.hpp"

namespace Slic3r {

std::vector<unsigned int> parse_mixed_components(const std::string &str)
{
    std::vector<unsigned int> components;
    if (str.empty())
        return components;
    std::istringstream ss(str);
    std::string token;
    while (std::getline(ss, token, ',')) {
        try {
            int val = std::stoi(token);
            if (val >= 0)
                components.push_back(static_cast<unsigned int>(val));
        } catch (...) {}
    }
    return components;
}

std::vector<double> parse_mixed_ratios(const std::string &str, size_t n_components)
{
    CNumericLocalesSetter c_locale_setter;
    std::vector<double> ratios;
    if (!str.empty()) {
        std::istringstream ss(str);
        std::string token;
        while (std::getline(ss, token, ',')) {
            try {
                double val = std::stod(token);
                if (val > 0.0)
                    ratios.push_back(val);
            } catch (...) {}
        }
    }

    if (ratios.size() != n_components || n_components == 0) {
        ratios.assign(n_components, n_components > 0 ? 1.0 / n_components : 0.0);
        return ratios;
    }

    double sum = std::accumulate(ratios.begin(), ratios.end(), 0.0);
    if (sum > 0.0 && std::abs(sum - 1.0) > 1e-6) {
        for (double &r : ratios)
            r /= sum;
    }
    return ratios;
}

bool has_any_mixed_filament(const std::vector<unsigned char> &is_mixed)
{
    for (unsigned char v : is_mixed)
        if (v)
            return true;
    return false;
}

std::vector<size_t> check_mixed_filament_integrity(
    const std::vector<unsigned char> &is_mixed,
    const std::vector<std::string>   &comp_strs,
    size_t num_physical)
{
    std::vector<size_t> broken;
    for (size_t i = 0; i < is_mixed.size(); ++i) {
        if (!is_mixed[i])
            continue;
        if (i >= comp_strs.size() || comp_strs[i].empty()) {
            broken.push_back(i);
            continue;
        }
        auto comps = parse_mixed_components(comp_strs[i]);
        if (comps.size() < 2) {
            broken.push_back(i);
            continue;
        }
        for (unsigned int c : comps) {
            if (c < 1 || c > num_physical) {
                broken.push_back(i);
                break;
            }
        }
    }
    return broken;
}

std::vector<unsigned int> expand_mixed_filaments(
    const std::vector<unsigned int> &extruders_0based,
    const std::vector<unsigned char> &is_mixed,
    const std::vector<std::string>  &comp_strs)
{
    std::vector<unsigned int> result;
    for (unsigned int ext : extruders_0based) {
        if (ext < is_mixed.size() && is_mixed[ext] && ext < comp_strs.size()) {
            auto comps = parse_mixed_components(comp_strs[ext]);
            for (unsigned int c : comps)
                if (c >= 1)
                    result.push_back(c - 1);
        } else {
            result.push_back(ext);
        }
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

void remap_mixed_components_on_delete(
    const std::vector<unsigned char> &is_mixed,
    std::vector<std::string>         &comp_strs,
    unsigned int                      del_1based)
{
    for (size_t i = 0; i < is_mixed.size(); ++i) {
        if (!is_mixed[i])
            continue;
        if (i >= comp_strs.size() || comp_strs[i].empty())
            continue;

        auto comps = parse_mixed_components(comp_strs[i]);
        std::ostringstream ss;
        for (size_t j = 0; j < comps.size(); ++j) {
            if (j > 0)
                ss << ',';
            if (comps[j] == del_1based)
                ss << 0;
            else if (comps[j] > del_1based)
                ss << (comps[j] - 1);
            else
                ss << comps[j];
        }
        comp_strs[i] = ss.str();
    }
}

std::vector<size_t> check_mixed_filament_type_consistency(
    const std::vector<unsigned char> &is_mixed,
    const std::vector<std::string>   &comp_strs,
    const std::vector<std::string>   &filament_types)
{
    std::vector<size_t> result;
    for (size_t i = 0; i < is_mixed.size(); ++i) {
        if (!is_mixed[i])
            continue;
        if (i >= comp_strs.size() || comp_strs[i].empty())
            continue;
        auto comps = parse_mixed_components(comp_strs[i]);
        if (comps.size() < 2)
            continue;

        std::string ref_type;
        bool mismatch = false;
        for (unsigned int c : comps) {
            if (c == 0)
                continue;
            size_t idx = static_cast<size_t>(c) - 1;
            if (idx >= filament_types.size())
                continue;
            if (ref_type.empty())
                ref_type = filament_types[idx];
            else if (filament_types[idx] != ref_type) {
                mismatch = true;
                break;
            }
        }
        if (mismatch)
            result.push_back(i);
    }
    return result;
}

void expand_mixed_slots_in_unprintables(
    std::vector<std::set<int>>        &unprintables,
    const std::vector<unsigned char>  &is_mixed,
    const std::vector<std::string>    &comp_strs)
{
    for (auto &unprintable_set : unprintables) {
        std::set<int> expanded;
        for (int fid : unprintable_set) {
            if (fid >= 0 && static_cast<size_t>(fid) < is_mixed.size() && is_mixed[fid] &&
                static_cast<size_t>(fid) < comp_strs.size()) {
                auto comps = parse_mixed_components(comp_strs[fid]);
                for (unsigned int c : comps)
                    if (c >= 1)
                        expanded.insert(static_cast<int>(c - 1));
            } else {
                expanded.insert(fid);
            }
        }
        unprintable_set = std::move(expanded);
    }
}

} // namespace Slic3r
