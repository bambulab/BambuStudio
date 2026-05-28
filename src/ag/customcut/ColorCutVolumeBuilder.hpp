#ifndef slic3r_ColorCutVolumeBuilder_hpp_
#define slic3r_ColorCutVolumeBuilder_hpp_

#include "ColorCutGeometryBackend.hpp"
#include "ColorCutTypes.hpp"

namespace Slic3r {

class ModelObject;

namespace ColorCut {

class ColorCutVolumeBuilder
{
public:
    ColorCutResult build(const GeometryCutOutput &geometry_output) const;

    ColorCutResult build(
        const GeometryCutOutput &geometry_output,
        const ModelObject &source_object,
        const ColorCutRequest &request) const;
};

} // namespace ColorCut
} // namespace Slic3r

#endif
