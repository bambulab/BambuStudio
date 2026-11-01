#pragma once

#include <optional>
#include <utility>

namespace Slic3r::GUI {

// Coalesce hidden-page requests without losing an initial all-thumbnail refresh.
class RecentProjectsRefresh
{
public:
    void request(int images)
    {
        if (!m_images || images > *m_images)
            m_images = images;
    }

    std::optional<int> take(bool ready)
    {
        if (!ready)
            return std::nullopt;
        return std::exchange(m_images, std::nullopt);
    }

private:
    std::optional<int> m_images;
};

} // namespace Slic3r::GUI
