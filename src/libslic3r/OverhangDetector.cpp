#include "OverhangDetector.hpp"
#include "ExtrusionEntity.hpp"
#include "AABBTreeLines.hpp"
#include "Arachne/utils/ExtrusionLine.hpp"

namespace Slic3r {

    Polylines ZPath_to_polylines(const ZPaths& paths)
    {
        Polylines lines;
        for (auto& path : paths) {
            lines.emplace_back();
            for (auto& p : path) lines.back().points.push_back(Point{ p.x(), p.y() });
        }
        return lines;
    };

    ZPaths clip_extrusion(const ZPath& subject, const ZPaths& clip, ClipperLib_Z::ClipType clipType)
    {
        ClipperLib_Z::Clipper clipper;
        clipper.ZFillFunction([](const ClipperLib_Z::IntPoint& e1bot, const ClipperLib_Z::IntPoint& e1top, const ClipperLib_Z::IntPoint& e2bot, const ClipperLib_Z::IntPoint& e2top,
            ClipperLib_Z::IntPoint& pt) {
                // The clipping contour may be simplified by clipping it with a bounding box of "subject" path.
                // The clipping function used may produce self intersections outside of the "subject" bounding box. Such self intersections are
                // harmless to the result of the clipping operation,
                // Both ends of each edge belong to the same source: Either they are from subject or from clipping path.
                assert(e1bot.z() >= 0 && e1top.z() >= 0);
                assert(e2bot.z() >= 0 && e2top.z() >= 0);
                assert((e1bot.z() == 0) == (e1top.z() == 0));
                assert((e2bot.z() == 0) == (e2top.z() == 0));

                // Start & end points of the clipped polyline (extrusion path with a non-zero width).
                ClipperLib_Z::IntPoint start = e1bot;
                ClipperLib_Z::IntPoint end = e1top;
                if (start.z() <= 0 && end.z() <= 0) {
                    start = e2bot;
                    end = e2top;
                }

                if (start.z() <= 0 && end.z() <= 0) {
                    // Self intersection on the source contour.
                    assert(start.z() == 0 && end.z() == 0);
                    pt.z() = 0;
                }
                else {
                    // Interpolate extrusion line width.
                    assert(start.z() > 0 && end.z() > 0);

                    double length_sqr = (end - start).cast<double>().squaredNorm();
                    double dist_sqr = (pt - start).cast<double>().squaredNorm();
                    double t = std::sqrt(dist_sqr / length_sqr);

                    pt.z() = start.z() + coord_t((end.z() - start.z()) * t);
                }
            });

        clipper.AddPath(subject, ClipperLib_Z::ptSubject, false);
        clipper.AddPaths(clip, ClipperLib_Z::ptClip, true);

        ClipperLib_Z::PolyTree clipped_polytree;
        ClipperLib_Z::Paths    clipped_paths;
        clipper.Execute(clipType, clipped_polytree, ClipperLib_Z::pftNonZero, ClipperLib_Z::pftNonZero);
        ClipperLib_Z::PolyTreeToPaths(clipped_polytree, clipped_paths);

        // Clipped path could contain vertices from the clip with a Z coordinate equal to zero.
        // For those vertices, we must assign value based on the subject.
        // This happens only in sporadic cases.
        for (ClipperLib_Z::Path& path : clipped_paths)
            for (ClipperLib_Z::IntPoint& c_pt : path)
                if (c_pt.z() == 0) {
                    // Now we must find the corresponding line on with this point is located and compute line width (Z coordinate).
                    if (subject.size() <= 2) continue;

                    const Point pt(c_pt.x(), c_pt.y());
                    Point       projected_pt_min;
                    auto        it_min = subject.begin();
                    auto        dist_sqr_min = std::numeric_limits<double>::max();
                    Point       prev(subject.front().x(), subject.front().y());
                    for (auto it = std::next(subject.begin()); it != subject.end(); ++it) {
                        Point curr(it->x(), it->y());
                        Point projected_pt = pt.projection_onto(Line(prev, curr));
                        if (double dist_sqr = (projected_pt - pt).cast<double>().squaredNorm(); dist_sqr < dist_sqr_min) {
                            dist_sqr_min = dist_sqr;
                            projected_pt_min = projected_pt;
                            it_min = std::prev(it);
                        }
                        prev = curr;
                    }

                    assert(dist_sqr_min <= SCALED_EPSILON);
                    assert(std::next(it_min) != subject.end());

                    const Point  pt_a(it_min->x(), it_min->y());
                    const Point  pt_b(std::next(it_min)->x(), std::next(it_min)->y());
                    const double line_len = (pt_b - pt_a).cast<double>().norm();
                    const double dist = (projected_pt_min - pt_a).cast<double>().norm();
                    c_pt.z() = coord_t(double(it_min->z()) + (dist / line_len) * double(std::next(it_min)->z() - it_min->z()));
                }

        assert([&clipped_paths = std::as_const(clipped_paths)]() -> bool {
            for (const ClipperLib_Z::Path& path : clipped_paths)
                for (const ClipperLib_Z::IntPoint& pt : path)
                    if (pt.z() <= 0) return false;
            return true;
            }());

        return clipped_paths;
    }

    ZPath add_sampling_points(const ZPath& path, double min_sampling_interval)
    {
        ZPath sampled_path;
        if (path.empty())
            return sampled_path;
        sampled_path.reserve(1.5 * path.size());
        for (size_t idx = 0; idx < path.size(); ++idx) {
            ZPoint curr_zp = path[idx];
            Point  curr_p = { curr_zp.x(), curr_zp.y() };
            sampled_path.emplace_back(curr_zp);
            if (idx + 1 < path.size()) {
                ZPoint next_zp = path[idx + 1];
                Point  next_p = { next_zp.x(), next_zp.y() };

                double dist = (next_p - curr_p).cast<double>().norm();
                if (dist > min_sampling_interval) {
                    size_t num_samples = static_cast<size_t>(std::floor(dist / min_sampling_interval));
                    for (size_t j = 1; j <= num_samples; ++j) {
                        double t = j * min_sampling_interval / dist;
                        ZPoint new_point;
                        new_point.x() = curr_p.x() + t * (next_p.x() - curr_p.x());
                        new_point.y() = curr_p.y() + t * (next_p.y() - curr_p.y());
                        new_point.z() = curr_zp.z() + t * (next_zp.z() - curr_zp.z());
                        sampled_path.push_back(new_point);
                    }
                }
            }
        }
        sampled_path.shrink_to_fit();
        return sampled_path;
    }
    ZPaths add_sampling_points(const ZPaths &paths, double min_sampling_interval) {
        ZPaths res;
        res.resize(paths.size());
        for (size_t i = 0; i < res.size(); i++) res[i] = add_sampling_points(paths[i], min_sampling_interval);
        return res;
    }

    OverhangDistancer::OverhangDistancer(const Polygons& layer_polygons)
    {
        for (const Polygon& island : layer_polygons) {
            for (const auto& line : island.lines()) { lines.emplace_back(line.a.cast<double>(), line.b.cast<double>()); }
        }
        tree = AABBTreeLines::build_aabb_tree_over_indexed_lines(lines);
    }

    float OverhangDistancer::distance_from_perimeter(const Vec2f& point) const
    {
        Vec2d  p = point.cast<double>();
        size_t hit_idx_out{};
        Vec2d  hit_point_out = Vec2d::Zero();
        auto   distance = AABBTreeLines::squared_distance_to_indexed_lines(lines, tree, p, hit_idx_out, hit_point_out);
        if (distance < 0) { return std::numeric_limits<float>::max(); }

        distance = sqrt(distance);
        return distance;
    }

    ExtrusionPaths detect_overhang_degree(const Flow& flow,
        const ExtrusionRole role,
        const Polygons& lower_polys,
        const ClipperLib_Z::Paths& clip_paths,
        const ClipperLib_Z::Path& extrusion_path,
        const double nozzle_diameter)
    {
        ExtrusionPaths ret_paths;
        std::list<ExtrusionPath> ret_path_list;
        ZPaths paths_in_range = clip_extrusion(extrusion_path, clip_paths, ClipperLib_Z::ctIntersection); //doing intersection first would be faster.

        paths_in_range = add_sampling_points(paths_in_range, scale_(2));// enough?

        //Polylines polylines = ZPath_to_polylines(subject_paths);
        //Polylines path_in_range_debug = ZPath_to_polylines(paths_in_range);

        for (auto& path : paths_in_range) {
            for (auto& p : path) { assert(p.z() != 0); }
        }

        auto get_base_degree = [](double d){
            return std::floor(d/min_degree_gap_arachne) * min_degree_gap_arachne;
        };

        auto in_same_degree_range = [&](double a, double b) -> bool {
            double base_a = get_base_degree(a);
            double base_b = get_base_degree(b);
            return std::abs(base_a - base_b) < EPSILON;
        };

        struct SplitPoint
        {
            Point   p;
            coord_t w;
            double  degree;
        };

        auto get_split_points = [&](const Point& pa, const Point& pb, const coord_t wa, const coord_t wb, const double da, const double db) -> std::vector<SplitPoint> {
            std::vector<SplitPoint> ret;
            double start_d = get_base_degree(std::min(da, db)) + min_degree_gap_arachne;
            double end_d = get_base_degree(std::max(da, db));

            if (start_d > end_d) return ret;

            double delta_d = db - da;
            if (std::abs(delta_d) < 1e-6) return ret;

            if (da < db) {
                for (double k = start_d; k <= end_d; k+=min_degree_gap_arachne) {
                    const double  t = (k - da) / delta_d;
                    const Point   pt = pa + (pb - pa) * t;
                    const coord_t w = wa + coord_t((wb - wa) * t);
                    ret.emplace_back(SplitPoint{ pt, w, (double)k });
                }
            }
            else {
                for (double k = end_d; k >= start_d; k-=min_degree_gap_arachne) {
                    const double  t = (k - da) / delta_d;
                    const Point   pt = pa + (pb - pa) * t;
                    const coord_t w = wa + coord_t((wb - wa) * t);
                    ret.emplace_back(SplitPoint{ pt, w, (double)k });
                }
            }
            return ret;
            };

        std::unique_ptr<SignedOverhangDistancer> prev_layer_distancer = std::make_unique<SignedOverhangDistancer>(lower_polys);

        coord_t offset_width = scale_(nozzle_diameter) / 2;

        for (auto& path : paths_in_range) {
            std::vector<double> overhang_degree_arr;
            if (path.empty()) continue;
            for (size_t idx = 0; idx < path.size(); ++idx) {
                Point  p{ path[idx].x(), path[idx].y() };
                double  overhang_dist = prev_layer_distancer->distance_from_perimeter(p.cast<double>());
                float  width = path[idx].z();
                double real_dist = offset_width + overhang_dist;

                double degree = 0;

                if (std::abs(real_dist) > (width / 2)) {
                    degree = real_dist < 0 ? 0 : 100;
                }
                else {
                    degree = (width / 2 + real_dist) / width * 100;
                }

                double mapped_degree;
                // map overhang speed to a range
                {
                    auto   it = std::upper_bound(non_uniform_degree_map.begin(), non_uniform_degree_map.end(), degree);
                    if (it == non_uniform_degree_map.end()) {
                        mapped_degree = non_uniform_degree_map.size() - 1;
                    }
                    else {
                        int    high_idx = it - non_uniform_degree_map.begin();
                        int    low_idx = high_idx - 1;
                        double t = (degree - non_uniform_degree_map[low_idx]) / (non_uniform_degree_map[high_idx] - non_uniform_degree_map[low_idx]);
                        mapped_degree = low_idx * (1 - t) + t * high_idx;
                    }
                }
                overhang_degree_arr.emplace_back(mapped_degree);
            }
            // split into extrusion path

            Point   prev_p{ path.front().x(), path.front().y() };
            coord_t prev_w = path.front().z();
            double  prev_d = overhang_degree_arr.front();
            ZPath   prev_line = { path.front() };

            for (size_t idx = 1; idx < path.size(); ++idx) {
                Point   curr_p{ path[idx].x(), path[idx].y() };
                coord_t curr_w = path[idx].z();
                double  curr_d = overhang_degree_arr[idx];

                if (in_same_degree_range(prev_d, curr_d)) {
                    prev_w = curr_w;
                    prev_d = curr_d;
                    prev_p = curr_p;
                    prev_line.push_back(path[idx]);
                    continue;
                }
                else {
                    auto split_points = get_split_points(prev_p, curr_p, prev_w, curr_w, prev_d, curr_d);
                    for (auto& split_point : split_points) {
                        prev_line.push_back({ split_point.p.x(), split_point.p.y(), split_point.w });
                        double target_degree = 0;
                        if( prev_d < curr_d)
                            target_degree = split_point.degree - min_degree_gap_arachne;
                        else
                            target_degree = split_point.degree;
                        extrusion_paths_append(ret_path_list , { prev_line }, role, flow, target_degree);
                        prev_line.clear();
                        prev_line.push_back({ split_point.p.x(), split_point.p.y(), split_point.w });
                    }
                    prev_w = curr_w;
                    prev_d = curr_d;
                    prev_p = curr_p;
                    prev_line.push_back(path[idx]);
                }
            }
            if (prev_line.size() > 1) { extrusion_paths_append(ret_path_list, { prev_line }, role, flow, get_base_degree(prev_d)); }
        }

        ret_paths.reserve(ret_path_list.size());
        ret_paths.insert(ret_paths.end(), std::make_move_iterator(ret_path_list.begin()), std::make_move_iterator(ret_path_list.end()));

        return ret_paths;
    }

    SignedOverhangDistancer::SignedOverhangDistancer(const Polygons &layer_polygons)
    {
        std::vector<Linef> lines;
        for (const Polygon &island : layer_polygons) {
            for (const auto &line : island.lines()) { lines.emplace_back(line.a.cast<double>(), line.b.cast<double>()); }
        }
        distancer = AABBTreeLines::LinesDistancer<Linef>(lines);
    }

    double SignedOverhangDistancer::distance_from_perimeter(const Vec2d &point) const
    {
        return distancer.template distance_from_lines<true>(point);
    }

    std::tuple<float, size_t, Vec2d> SignedOverhangDistancer::distance_from_perimeter_extra(const Vec2d &point) const
    {
        return distancer.template distance_from_lines_extra<true>(point);
    }

    double get_base_degree(double d, double degree_trace)
    {
        double degee_base = int(d / degree_trace) * degree_trace;
        return degee_base >= max_overhang_degree ? max_overhang_degree : degee_base;
    };

    double get_mapped_degree(double overhang_dist, double lower_bound, double upper_bound)
    {
        // BBS : calculate overhang degree -- overhang length / width
        double this_degree = (overhang_dist - lower_bound) / (upper_bound - lower_bound) * 100;
        // BBS: covert to terraced overhang
        double terraced_overhang = 0;
        if (this_degree >= 100)
            terraced_overhang = max_overhang_degree;
        else if (this_degree > EPSILON * 100) {
            int upper_bound_idx = std::upper_bound(non_uniform_degree_map.begin(), non_uniform_degree_map.end(), this_degree) - non_uniform_degree_map.begin();
            int lower_bound_idx = upper_bound_idx - 1;

            double t = (this_degree - non_uniform_degree_map[lower_bound_idx]) / (non_uniform_degree_map[upper_bound_idx] - non_uniform_degree_map[lower_bound_idx]);
            terraced_overhang = (1.0 - t) * lower_bound_idx + t * upper_bound_idx;
        }

        return terraced_overhang;
    }

    void merged_with_degree(std::vector<SplitPoly> &in) {
        std::vector<SplitPoly> out;

        // standardization
        Polyline merged_lines;
        double   degree_base = -1;
        for (size_t idx = 0; idx < in.size(); idx ++) {
            double degree = get_base_degree(in[idx].degree, min_degree_gap_classic);

            if (!merged_lines.empty() && degree_base != degree) {
                out.emplace_back(SplitPoly(merged_lines, degree_base));
                merged_lines.clear();
            }
            degree_base = degree;
            merged_lines.append(in[idx].polyline);
        }

        if (!merged_lines.empty()) {
             out.emplace_back(SplitPoly(merged_lines, degree_base));
        }

        in = std::move(out);
    }

    void smoothing_degrees(SplitLines &lines)
    {
        // too short
        if (lines.start.empty() || lines.middle.empty())
            return;

        double d1 = lines.start.back().degree;
        double d2 = lines.end.front().degree;

        if (lines.middle.front().polyline.length() < 2 * cut_length || std::abs(d2 - d1) < min_degree_gap_classic) {
            lines.middle.front().degree = (d2 + d1)/2;
            return;
        }

        std::vector<SplitPoly> out;
        //BBS: smoothing polyline by degree
        // compare cut length and degree
        double length = lines.middle.front().polyline.length();
        int length_cut = length / cut_length;
        int degree_cut = std::abs(d2 - d1) / min_degree_gap_classic / 0.6;
        int count = std::min(length_cut, degree_cut);
        double cut_gap    = length / count;
        double degree_gap = (d2 - d1) / count;
        //cut
        Point dir   = lines.middle.front().polyline.back() - lines.middle.front().polyline.front();
        Point start = lines.middle.front().polyline.front();
        Point end;
        for (size_t idx = 0; idx < count - 1; idx ++) {
            double t = (idx + 1) * cut_gap / length;
            end      = lines.middle.front().polyline.front() + dir * t;
            double degree = d1 + (idx + 1) * degree_gap;
            out.push_back(SplitPoly(Polyline(start, end), degree));
            start = end;
        }
        out.push_back(SplitPoly(Polyline(start, lines.middle.front().polyline.back()), d1 + count * degree_gap));
        lines.middle = out;
    }

    void check_degree( DegreePolylines &input, const std::unique_ptr<OverhangDistancer> &prev_layer_distancer, const double &lower_bound, const double &upper_bound, std::vector<SplitPoly> &out)
    {
        auto chek_overhang = [&](std::vector<SplitPoly> &lines) {
            for (size_t i = 0; i < lines.size(); ++i) {
                Point  mid           = (lines[i].polyline.front() + lines[i].polyline.back()) / 2;
                double overhang_dist = prev_layer_distancer->distance_from_perimeter(mid.cast<float>());
                lines[i].degree      = get_mapped_degree(overhang_dist, lower_bound, upper_bound);
            }
        };

        for (size_t idx = 0; idx < input.size(); ++idx) {
            // check each part's degree
            if (input[idx].start.empty())
                chek_overhang(input[idx].middle);
            else {
                chek_overhang(input[idx].start);
                chek_overhang(input[idx].end);
            }

            // smoothing
            smoothing_degrees(input[idx]);
            out.insert(out.end(), input[idx].start.begin(), input[idx].start.end());
            out.insert(out.end(), input[idx].middle.begin(), input[idx].middle.end());
            out.insert(out.end(), input[idx].end.begin(), input[idx].end.end());
        }
    }

    DegreePolylines prepare_split_polylines(Polyline polyline)
    {
        DegreePolylines out;
        if(polyline.size() == 2)
            out.emplace_back(SplitLines(polyline, true));
        else {
            for (size_t idx = 0; idx < polyline.size() - 1; idx++) {
                out.emplace_back(SplitLines(Polyline(polyline[idx], polyline[idx + 1]), false));
            }
        }

        return out;
    }


    void detect_overhang_degree(Polygons lower_polygons,
                                const ExtrusionRole & role,
                                double extrusion_mm3_per_mm,
                                double extrusion_width,
                                double layer_height,
                                Polylines       middle_overhang_polyines,
                                const double    &lower_bound,
                                const double    &upper_bound,
                                ExtrusionPaths   &paths)
    {
        // BBS: collect lower_polygons points
        //Polylines;
        Points lower_polygon_points;
        std::vector<size_t> polygons_bound;

        std::unique_ptr<OverhangDistancer> prev_layer_distancer;
        prev_layer_distancer = std::make_unique<OverhangDistancer>(lower_polygons);
        //BBS: get overhang degree and split path
        for (size_t polyline_idx = 0; polyline_idx < middle_overhang_polyines.size(); ++polyline_idx) {
            //filter too short polyline
            std::vector<SplitPoly> out;

            Polyline middle_poly = middle_overhang_polyines[polyline_idx];
            DegreePolylines splited_lines = prepare_split_polylines(middle_poly);
            check_degree(splited_lines, prev_layer_distancer, lower_bound, upper_bound, out);

            merged_with_degree(out);
            // merge path by degree
            for (SplitPoly &polylines_collection : out)
                extrusion_paths_append(paths,
                                       std::move(polylines_collection.polyline),
                                       polylines_collection.degree,
                                       int(0),
                                       role,
                                       extrusion_mm3_per_mm,
                                       extrusion_width,
                                       layer_height);
            out.clear();
        }
    }

}