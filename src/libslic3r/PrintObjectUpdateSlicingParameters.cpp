#include "Print.hpp"

namespace Slic3r {

void PrintObject::update_slicing_parameters()
{
    if (!m_slicing_params.valid)
        m_slicing_params = SlicingParameters::create_from_config(
            this->print()->config(), m_config, this->model_object()->bounding_box().max.z(), this->object_extruders());
}

} // namespace Slic3r
