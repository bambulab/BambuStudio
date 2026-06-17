#include "Print.hpp"

#include "GCode/ConflictChecker.hpp"

#include <chrono>
#include <optional>

#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

namespace Slic3r {

void Print::check_gcode_path_conflicts_after_process()
{
    if (m_no_check)
        return;

    using Clock = std::chrono::high_resolution_clock;
    auto startTime = Clock::now();
    std::optional<const FakeWipeTower *> wipe_tower_opt = {};
    if (this->has_wipe_tower()) {
        m_fake_wipe_tower.set_pos({ m_config.wipe_tower_x.get_at(m_plate_index), m_config.wipe_tower_y.get_at(m_plate_index) });
        wipe_tower_opt = std::make_optional<const FakeWipeTower *>(&m_fake_wipe_tower);
    }
    auto conflictRes = ConflictChecker::find_inter_of_lines_in_diff_objs(m_objects, wipe_tower_opt);
    auto endTime     = Clock::now();
    volatile double seconds = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count() / (double) 1000;
    BOOST_LOG_TRIVIAL(info) << "gcode path conflicts check takes " << seconds << " secs.";

    m_conflict_result = conflictRes;
    if (conflictRes.has_value())
        BOOST_LOG_TRIVIAL(error) << boost::format("gcode path conflicts found between %1% and %2%") % conflictRes.value()._objName1 % conflictRes.value()._objName2;
}

} // namespace Slic3r
