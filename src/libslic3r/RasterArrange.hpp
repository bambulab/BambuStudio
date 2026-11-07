#ifndef slic3r_RasterArrange_hpp_
#define slic3r_RasterArrange_hpp_

#include "Arrange.hpp"

namespace Slic3r { namespace arrangement {

// Raster ("bitmap") based nester used for layer-by-layer printing.
//
// Compared to the libnest2d based nester it
//  * uses the real (non-convex) silhouette of every object, including its holes
//    (ArrangePolygon::footprint), so parts can be nested into concave areas and,
//    when allowed, into through-holes of other parts (rings, tubes, frames),
//  * tries arbitrary rotations when rotation is enabled, and searches for a fitting
//    rotation when an object only fits the bed at some angle (e.g. along the diagonal),
//  * fills the plates one by one and, for every plate, evaluates many packing strategies
//    (several item orderings x several gravity anchors) in parallel, keeping the one
//    which puts the most area on the plate. A new plate is only opened for items that
//    really do not fit.
//
// Collision tests are exact on a grid: every shape is rasterized conservatively
// (a cell is occupied if any part of the inflated shape touches it), therefore two
// placed objects never come closer than the requested spacing (with automatic spacing,
// the default 1 mm separation may shrink by up to half a grid cell diagonal).
//
// Inputs/outputs follow arrangement::arrange(): translation, rotation and bed_idx of
// the items are filled in, the excludes are fixed obstacles living on bed `bed_idx`.
void raster_arrange(ArrangePolygons &items, const ArrangePolygons &excludes, const Polygon &bed, const ArrangeParams &params);

}} // namespace Slic3r::arrangement

#endif // slic3r_RasterArrange_hpp_
