#ifndef slic3r_Fill_hpp_
#define slic3r_Fill_hpp_

#include <memory.h>
#include <float.h>
#include <stdint.h>

#include "../libslic3r.h"
#include "../PrintConfig.hpp"

#include "FillBase.hpp"

namespace Slic3r {

class ExtrusionEntityCollection;
class LayerRegion;
class PrintObject;

// Angle (in radians) for a per-layer infill rotation template, ported from OrcaSlicer. An empty
// template_string just returns fixed_infill_angle unchanged; otherwise the string is parsed as
// either a plain comma-separated angle list (repeats by modulo) or the richer metalanguage
// documented above calculate_infill_rotation_angle()'s definition in Fill.cpp.
double calculate_infill_rotation_angle(const PrintObject *object, size_t layer_id,
                                       const double &fixed_infill_angle, const std::string &template_string);

// An interface class to Perl, aggregating an instance of a Fill and a FillData.
class Filler
{
public:
    Filler() : fill(nullptr) {}
    ~Filler() { 
        delete fill; 
        fill = nullptr;
    }
    Fill        *fill;
    FillParams   params;
};

} // namespace Slic3r

#endif // slic3r_Fill_hpp_
