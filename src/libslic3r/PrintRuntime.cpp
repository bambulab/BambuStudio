#include "Print.hpp"

#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

namespace Slic3r {

void Print::apply_config_for_render(const DynamicConfig &config)
{
    m_config.apply(config);
}

// Wipe tower support.
//BBS: add gcode file preload logic
void Print::set_gcode_file_ready()
{
    this->set_started(psGCodeExport);
    this->set_done(psGCodeExport);
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": done");
}

//BBS: add gcode file preload logic
void Print::set_gcode_file_invalidated()
{
    this->invalidate_step(psGCodeExport);
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": done");
}

} // namespace Slic3r
