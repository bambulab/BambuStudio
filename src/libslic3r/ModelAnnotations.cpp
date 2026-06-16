#include "Model.hpp"

#include "TriangleSelector.hpp"

namespace Slic3r {

indexed_triangle_set FacetsAnnotation::get_facets(const ModelVolume &mv, EnforcerBlockerType type) const
{
    TriangleSelector selector(mv.mesh());
    selector.deserialize(m_data, false);
    return selector.get_facets(type);
}

void FacetsAnnotation::get_facets(const ModelVolume &mv, std::vector<indexed_triangle_set> &facets_per_type) const
{
    TriangleSelector selector(mv.mesh());
    selector.deserialize(m_data, false);
    selector.get_facets(facets_per_type);
}

void FacetsAnnotation::shift_states_above(const ModelVolume &mv, EnforcerBlockerType threshold, int delta)
{
    if (empty())
        return;
    TriangleSelector selector(mv.mesh());
    selector.deserialize(m_data, false);
    selector.shift_states_above(threshold, delta);
    this->set(selector);
}

void FacetsAnnotation::set_enforcer_block_type_limit(const ModelVolume &mv,
                                                     EnforcerBlockerType max_type,
                                                     EnforcerBlockerType to_delete_filament,
                                                     EnforcerBlockerType replace_filament)
{
    TriangleSelector selector(mv.mesh());
    selector.deserialize(m_data, false, max_type, to_delete_filament, replace_filament);
    this->set(selector);
}

indexed_triangle_set FacetsAnnotation::get_facets_strict(const ModelVolume &mv, EnforcerBlockerType type) const
{
    TriangleSelector selector(mv.mesh());
    selector.deserialize(m_data, false);
    return selector.get_facets_strict(type);
}

bool FacetsAnnotation::has_facets(const ModelVolume &mv, EnforcerBlockerType type) const
{
    return TriangleSelector::has_facets(m_data, type);
}

bool FacetsAnnotation::set(const TriangleSelector &selector)
{
    std::pair<std::vector<std::pair<int, int>>, std::vector<bool>> sel_map = selector.serialize();
    if (sel_map != m_data) {
        m_data = std::move(sel_map);
        this->touch();
        return true;
    }
    return false;
}

void FacetsAnnotation::reset()
{
    m_data.first.clear();
    m_data.second.clear();
    this->touch();
}

std::string FacetsAnnotation::get_triangle_as_string(int triangle_idx) const
{
    std::string out;

    auto triangle_it = std::lower_bound(m_data.first.begin(), m_data.first.end(), triangle_idx, [](const std::pair<int, int> &l, const int r) { return l.first < r; });
    if (triangle_it != m_data.first.end() && triangle_it->first == triangle_idx) {
        int offset = triangle_it->second;
        int end    = ++triangle_it == m_data.first.end() ? int(m_data.second.size()) : triangle_it->second;
        while (offset < end) {
            int next_code = 0;
            for (int i = 3; i >= 0; --i) {
                next_code = next_code << 1;
                next_code |= int(m_data.second[offset + i]);
            }
            offset += 4;

            assert(next_code >= 0 && next_code <= 15);
            char digit = next_code < 10 ? next_code + '0' : (next_code - 10) + 'A';
            out.insert(out.begin(), digit);
        }
    }
    return out;
}

void FacetsAnnotation::set_triangle_from_string(int triangle_id, const std::string &str)
{
    assert(!str.empty());
    assert(m_data.first.empty() || m_data.first.back().first < triangle_id);
    m_data.first.emplace_back(triangle_id, int(m_data.second.size()));

    for (auto it = str.crbegin(); it != str.crend(); ++it) {
        const char ch = *it;
        int        dec = 0;
        if (ch >= '0' && ch <= '9')
            dec = int(ch - '0');
        else if (ch >= 'A' && ch <= 'F')
            dec = 10 + int(ch - 'A');
        else
            assert(false);

        for (int i = 0; i < 4; ++i)
            m_data.second.insert(m_data.second.end(), bool(dec & (1 << i)));
    }
}

} // namespace Slic3r
