#ifndef slic3r_GCode_HeightSlowdown_hpp_
#define slic3r_GCode_HeightSlowdown_hpp_

#include <algorithm>

namespace Slic3r {

// Use model-layer heights so rafts and objects of different heights do not
// change where an object's automatic ramp starts or finishes.
inline double automatic_height_slowdown_factor(double print_z, double first_layer_z, double last_layer_z, double initial_factor = 1.)
{
    if (last_layer_z <= first_layer_z)
        return 1.;

    const double progress = std::clamp((print_z - first_layer_z) / (last_layer_z - first_layer_z), 0., 1.);
    return initial_factor + (0.5 - initial_factor) * progress;
}

} // namespace Slic3r

#endif
