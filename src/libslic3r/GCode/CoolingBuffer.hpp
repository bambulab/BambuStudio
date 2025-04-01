#ifndef slic3r_CoolingProcess_hpp_
#define slic3r_CoolingProcess_hpp_
#include "../libslic3r.h"
#include "GCodeEditor.hpp"

namespace Slic3r {

class CoolingBuffer
{
public:
    CoolingBuffer(){};

    float calculate_layer_slowdown(std::vector<PerExtruderAdjustments> &per_extruder_adjustments);

private:
    // Old logic: proportional.
    bool m_cooling_logic_proportional = false;
};

}
#endif