#include "Print.hpp"

namespace Slic3r {

std::string Print::output_filename(const std::string &filename_base) const
{
    DynamicConfig config = this->finished() ? this->print_statistics().config() : this->print_statistics().placeholders();
    config.set_key_value("num_filaments", new ConfigOptionInt((int) m_config.nozzle_diameter.size()));
    config.set_key_value("plate_name", new ConfigOptionString(get_plate_name()));

    return this->PrintBase::output_filename(m_config.filename_format.value, ".gcode", filename_base, &config);
}

} // namespace Slic3r
