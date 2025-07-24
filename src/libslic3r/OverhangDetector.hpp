#ifndef OVERHANG_DETECTOR_HPP
#define OVERHANG_DETECTOR_HPP

#include "ExtrusionEntityCollection.hpp"
#include <clipper/clipper_z.hpp>
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
static const double cut_length = scale_(0.6);

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

    struct SplitPoly
    {
        SplitPoly(Polyline polyline) : polyline(polyline) {}
        SplitPoly(Polyline polyline, double degree) : polyline(polyline), degree(degree) {}
        Polyline polyline;
        double   degree = 0;
    };

    struct SplitLines
    {
        // default cut 2 part
        SplitLines(Polyline polyline, bool upsampling)
        {
            double length = polyline.length();
            if (length < 2 * cut_length) {
                middle.push_back(polyline);
                return;
            }

            // cut signle line
            int sampling_number = upsampling ? insert_point_count: 2;
            int    cut_count = std::min(int(length / cut_length), sampling_number);
            double final_cut_length       = std::min(polyline.length() / cut_count, cut_length);
            Point dir        = polyline.back() - polyline.front();
            //front
            cut_count = cut_count / 2;
            auto cut_polyline = [&](float base_length, Point first_point, Point last_point, std::vector<SplitPoly> &out) {
                Point start = first_point;
                Point end;
                for (size_t cnt = 0; cnt < cut_count - 1; cnt++) {
                    Polyline line;

                    line.append(start);

                    double t = ((cnt + 1) * cut_length + base_length) / length;
                    end      = first_point + dir * t;
                    line.append(end);

                    out.emplace_back(SplitPoly(line));
                    start = end;
                }
                out.emplace_back(SplitPoly(Polyline(start, last_point)));
            };

            double trim_length = final_cut_length * cut_count;
            // middle
            double middle_length = length - trim_length;
            Point  start_pt      = polyline.front() + dir * (trim_length / length);
            Point  end_pt        = polyline.front() + dir * ((length - trim_length)/length);
            middle.emplace_back(SplitPoly(Polyline(start_pt, end_pt)));

            cut_polyline(0, polyline.front(), start_pt, start);
            cut_polyline(middle_length, end_pt, polyline.back(), end);
        };

        std::vector<SplitPoly> start;
        std::vector<SplitPoly> end;
        std::vector<SplitPoly> middle;
    };

    using DegreePolylines = std::vector<SplitLines>;

    ZPaths clip_extrusion(const ZPath& subject, const ZPaths& clip, ClipperLib_Z::ClipType clipType);


    ZPath add_sampling_points(const ZPath& path, double min_sampling_interval);

    ExtrusionPaths detect_overhang_degree(const Flow& flow, const ExtrusionRole role, const Polygons& lower_polys, const ClipperLib_Z::Paths& clip_paths, const ClipperLib_Z::Path& extrusion_paths, const double nozzle_diameter);
    DegreePolylines prepare_split_polylines(Polyline polyline);
    void check_degree(DegreePolylines &                                    input,
                                 const std::unique_ptr<OverhangDistancer> &prev_layer_distancer,
                                 const double &                            lower_bound,
                                 const double &                            upper_bound,
                                 std::vector<SplitPoly> &                  out);
    double get_mapped_degree(double overhang_dist, double lower_bound, double upper_bound);
    void smoothing_degrees(std::vector<SplitPoly> &lines);
    void merged_with_degree(std::vector<SplitPoly> &in);
    void detect_overhang_degree(Polygons             lower_polygons,
                                          const ExtrusionRole &role,
                                          double               extrusion_mm3_per_mm,
                                          double               extrusion_width,
                                          double               layer_height,
                                          Polylines            middle_overhang_polyines,
                                          const double &       lower_bound,
                                          const double &       upper_bound,
                                          ExtrusionPaths &     paths);
    }

#endif