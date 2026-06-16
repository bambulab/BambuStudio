#include "Print.hpp"

namespace Slic3r {

BoundingBoxf3 PrintInstance::get_bounding_box() const
{
    return print_object->model_object()->instance_bounding_box(*model_instance, false);
}

Polygon PrintInstance::get_convex_hull_2d()
{
    Polygon poly = print_object->model_object()->convex_hull_2d(model_instance->get_matrix());
    poly.douglas_peucker(0.1);
    return poly;
}

//BBS: instance_shift is too large because of multi-plate, apply without plate offset.
Point PrintInstance::shift_without_plate_offset() const
{
    const Print *print          = print_object->print();
    const Vec3d  plate_offset   = print->get_plate_origin();
    return shift - Point(scaled(plate_offset.x()), scaled(plate_offset.y()));
}

PrintRegion *PrintObjectRegions::FuzzySkinPaintedRegion::parent_print_object_region(const LayerRangeRegions &layer_range) const
{
    using FuzzySkinParentType = PrintObjectRegions::FuzzySkinPaintedRegion::ParentType;

    if (this->parent_type == FuzzySkinParentType::PaintedRegion)
        return layer_range.painted_regions[this->parent].region;

    assert(this->parent_type == FuzzySkinParentType::VolumeRegion);
    return layer_range.volume_regions[this->parent].region;
}

int PrintObjectRegions::FuzzySkinPaintedRegion::parent_print_object_region_id(const LayerRangeRegions &layer_range) const
{
    return this->parent_print_object_region(layer_range)->print_object_region_id();
}

} // namespace Slic3r
