#ifndef slic3r_ColorCutAppearanceSnapshot_hpp_
#define slic3r_ColorCutAppearanceSnapshot_hpp_

#include "ColorCutAppearanceTypes.hpp"

namespace Slic3r {

class ModelObject;
class ModelVolume;

namespace ColorCut {

class ColorCutAttributeRepository;

class ColorCutAppearanceSnapshotBuilder
{
public:
    ObjectAppearanceSnapshot build(const ModelObject &object, const ColorCutAttributeRepository *repository = nullptr) const;
    VolumeAppearanceSnapshot build_volume(const ModelVolume &volume, const ColorCutAttributeRepository *repository = nullptr) const;
};

} // namespace ColorCut
} // namespace Slic3r

#endif
