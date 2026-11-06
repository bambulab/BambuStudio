#ifndef slic3r_PresetIncludeDiffCache_hpp_
#define slic3r_PresetIncludeDiffCache_hpp_

#include "PrintConfig.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r {

// Category-local: include-map values stay immutable until clear(). Live default
// presets may move during deque insertion, so key differences by owned snapshots.
class PresetIncludeDiffCache
{
public:
    const DynamicPrintConfig &default_snapshot(std::size_t index, const DynamicPrintConfig &config)
    {
        return m_defaults.try_emplace(index, config).first->second;
    }

    template<class Compute>
    const std::vector<std::string> &get(const DynamicPrintConfig &included,
                                      const DynamicPrintConfig &defaults, Compute &&compute)
    {
        const Key key{&included, &defaults};
        const auto found = m_diffs.find(key);
        if (found != m_diffs.end())
            return found->second;
        return m_diffs.emplace(key, std::forward<Compute>(compute)()).first->second;
    }

    void clear()
    {
        m_diffs.clear();
        m_defaults.clear();
    }

private:
    using Key = std::pair<const DynamicPrintConfig *, const DynamicPrintConfig *>;
    struct Less {
        bool operator()(const Key &left, const Key &right) const
        {
            const std::less<const DynamicPrintConfig *> less;
            return less(left.first, right.first) ||
                   (left.first == right.first && less(left.second, right.second));
        }
    };
    std::map<std::size_t, DynamicPrintConfig> m_defaults;
    std::map<Key, std::vector<std::string>, Less> m_diffs;
};

} // namespace Slic3r

#endif
