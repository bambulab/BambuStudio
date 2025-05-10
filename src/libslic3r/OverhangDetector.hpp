#ifndef OVERHANG_DETECTOR_HPP
#define OVERHANG_DETECTOR_HPP

#include "ExtrusionEntityCollection.hpp"
#include "ClipperUtils.hpp"
#include "Flow.hpp"
#include "AABBTreeLines.hpp"
using ZPoint = ClipperLib_Z::IntPoint;
using ZPath = ClipperLib_Z::Path;
using ZPaths = ClipperLib_Z::Paths;

static const int overhang_sampling_number = 6;
static const double min_degree_gap_classic = 0.1;
static const double min_degree_gap_arachne = 0.25;
static const int max_overhang_degree = overhang_sampling_number - 1;
static const std::vector<double> non_uniform_degree_map = { 0, 10, 25, 50, 75, 100};
static const int insert_point_count = 3;

namespace Slic3r {

    class OverhangDistancer
    {
        std::vector<Linef>                lines;
        AABBTreeIndirect::Tree<2, double> tree;
    public:
        OverhangDistancer(const Polygons& layer_polygons);
        float distance_from_perimeter(const Vec2f& point) const;
    };

    class SignedOverhangDistancer
    {
        AABBTreeLines::LinesDistancer<Linef> distancer;

    public:
        SignedOverhangDistancer(const Polygons &layer_polygons);
        double                            distance_from_perimeter(const Vec2d &point) const;
        std::tuple<float, size_t, Vec2d> distance_from_perimeter_extra(const Vec2d &point) const;
    };

    ZPaths clip_extrusion(const ZPath& subject, const ZPaths& clip, ClipperLib_Z::ClipType clipType);

    ZPath add_sampling_points(const ZPath& path, double min_sampling_interval);

    ExtrusionPaths detect_overhang_degree(const Flow& flow, const ExtrusionRole role, const Polygons& lower_polys, const ClipperLib_Z::Paths& clip_paths, const ClipperLib_Z::Path& extrusion_paths, const double nozzle_diameter);

}

#endif