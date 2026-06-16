#include "Print.hpp"

#include "Exception.hpp"

namespace Slic3r {

void Print::check_gcode_path_conflicts_after_process()
{
    if (m_no_check)
        return;

    throw Slic3r::RuntimeError("Print path conflict check is not linked into this smoke target");
}

} // namespace Slic3r
