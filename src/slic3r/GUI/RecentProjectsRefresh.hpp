#pragma once

#include <climits>
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
        if (!ready) {
            // Saving other projects can move a requested thumbnail to another
            // index before the page is shown. Refresh the images together then.
            if (m_images && *m_images >= 0)
                m_images = INT_MAX;
            return std::nullopt;
        }
        return std::exchange(m_images, std::nullopt);
    }

private:
    std::optional<int> m_images;
};

} // namespace Slic3r::GUI
