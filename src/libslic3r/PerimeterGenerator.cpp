#include "PerimeterGenerator.hpp"
#include "ClipperUtils.hpp"
#include "ExtrusionEntityCollection.hpp"
#include "ShortestPath.hpp"

#include <cmath>
#include <cassert>

static const int sampling_number = 11;
static const double curvatures_sampling_width = 6;         // mm
static const double curvatures_angle_best = PI / 6;
static const double curvatures_angle_worst = 5 * PI / 6;

static const double curvatures_best = (curvatures_angle_best * 1000 / curvatures_sampling_width);
static const double curvatures_worst = (curvatures_angle_worst * 1000 / curvatures_sampling_width);

namespace Slic3r {

static ExtrusionPaths thick_polyline_to_extrusion_paths(const ThickPolyline &thick_polyline, ExtrusionRole role, const Flow &flow, const float tolerance)
{
    ExtrusionPaths paths;
    ExtrusionPath path(role);
    ThickLines lines = thick_polyline.thicklines();
    
    for (int i = 0; i < (int)lines.size(); ++i) {
        const ThickLine& line = lines[i];
        
        const coordf_t line_len = line.length();
        if (line_len < SCALED_EPSILON) continue;
        
        double thickness_delta = fabs(line.a_width - line.b_width);
        if (thickness_delta > tolerance) {
            const unsigned int segments = (unsigned int)ceil(thickness_delta / tolerance);
            const coordf_t seg_len = line_len / segments;
            Points pp;
            std::vector<coordf_t> width;
            {
                pp.push_back(line.a);
                width.push_back(line.a_width);
                for (size_t j = 1; j < segments; ++j) {
                    pp.push_back((line.a.cast<double>() + (line.b - line.a).cast<double>().normalized() * (j * seg_len)).cast<coord_t>());
                    
                    coordf_t w = line.a_width + (j*seg_len) * (line.b_width-line.a_width) / line_len;
                    width.push_back(w);
                    width.push_back(w);
                }
                pp.push_back(line.b);
                width.push_back(line.b_width);
                
                assert(pp.size() == segments + 1u);
                assert(width.size() == segments*2);
            }
            
            // delete this line and insert new ones
            lines.erase(lines.begin() + i);
            for (size_t j = 0; j < segments; ++j) {
                ThickLine new_line(pp[j], pp[j+1]);
                new_line.a_width = width[2*j];
                new_line.b_width = width[2*j+1];
                lines.insert(lines.begin() + i + j, new_line);
            }
            
            -- i;
            continue;
        }
        
        const double w = fmax(line.a_width, line.b_width);
        if (path.polyline.points.empty()) {
            path.polyline.append(line.a);
            path.polyline.append(line.b);
            // Convert from spacing to extrusion width based on the extrusion model
            // of a square extrusion ended with semi circles.
            Flow new_flow = flow.with_width(unscale<float>(w) + flow.height() * float(1. - 0.25 * PI));
            #ifdef SLIC3R_DEBUG
            printf("  filling %f gap\n", flow.width);
            #endif
            path.mm3_per_mm  = new_flow.mm3_per_mm();
            path.width       = new_flow.width();
            path.height      = new_flow.height();
        } else {
            thickness_delta = fabs(scale_(flow.width()) - w);
            if (thickness_delta <= tolerance) {
                // the width difference between this line and the current flow width is 
                // within the accepted tolerance
                path.polyline.append(line.b);
            } else {
                // we need to initialize a new line
                paths.emplace_back(std::move(path));
                path = ExtrusionPath(role);
                -- i;
            }
        }
    }
    if (path.polyline.is_valid())
        paths.emplace_back(std::move(path));
    return paths;
}

static void variable_width(const ThickPolylines& polylines, ExtrusionRole role, const Flow &flow, std::vector<ExtrusionEntity*> &out)
{
	// This value determines granularity of adaptive width, as G-code does not allow
	// variable extrusion within a single move; this value shall only affect the amount
	// of segments, and any pruning shall be performed before we apply this tolerance.
	const float tolerance = float(scale_(0.05));
	for (const ThickPolyline &p : polylines) {
		ExtrusionPaths paths = thick_polyline_to_extrusion_paths(p, role, flow, tolerance);
		// Append paths to collection.
		if (! paths.empty()) {
			if (paths.front().first_point() == paths.back().last_point())
				out.emplace_back(new ExtrusionLoop(std::move(paths)));
			else {
				for (ExtrusionPath &path : paths)
					out.emplace_back(new ExtrusionPath(std::move(path)));
			}
		}
	}
}

// Hierarchy of perimeters.
class PerimeterGeneratorLoop {
public:
    // Polygon of this contour.
    Polygon                             polygon;
    // Is it a contour or a hole?
    // Contours are CCW oriented, holes are CW oriented.
    bool                                is_contour;
    // Depth in the hierarchy. External perimeter has depth = 0. An external perimeter could be both a contour and a hole.
    unsigned short                      depth;
    // Should this contur be fuzzyfied on path generation?
    bool                                fuzzify;
    // Children contour, may be both CCW and CW oriented (outer contours or holes).
    std::vector<PerimeterGeneratorLoop> children;
    
    PerimeterGeneratorLoop(const Polygon &polygon, unsigned short depth, bool is_contour, bool fuzzify) : 
        polygon(polygon), is_contour(is_contour), depth(depth), fuzzify(fuzzify) {}
    // External perimeter. It may be CCW or CW oriented (outer contour or hole contour).
    bool is_external() const { return this->depth == 0; }
    // An island, which may have holes, but it does not have another internal island.
    bool is_internal_contour() const;
};

// Thanks Cura developers for this function.
static void fuzzy_polygon(Polygon &poly, double fuzzy_skin_thickness, double fuzzy_skin_point_dist)
{
    const double min_dist_between_points = fuzzy_skin_point_dist * 3. / 4.; // hardcoded: the point distance may vary between 3/4 and 5/4 the supplied value
    const double range_random_point_dist = fuzzy_skin_point_dist / 2.;
    double dist_left_over = double(rand()) * (min_dist_between_points / 2) / double(RAND_MAX); // the distance to be traversed on the line before making the first new point
    Point* p0 = &poly.points.back();
    Points out;
    out.reserve(poly.points.size());
    for (Point &p1 : poly.points)
    { // 'a' is the (next) new point between p0 and p1
        Vec2d  p0p1      = (p1 - *p0).cast<double>();
        double p0p1_size = p0p1.norm();
        // so that p0p1_size - dist_last_point evaulates to dist_left_over - p0p1_size
        double dist_last_point = dist_left_over + p0p1_size * 2.;
        for (double p0pa_dist = dist_left_over; p0pa_dist < p0p1_size;
            p0pa_dist += min_dist_between_points + double(rand()) * range_random_point_dist / double(RAND_MAX))
        {
            double r = double(rand()) * (fuzzy_skin_thickness * 2.) / double(RAND_MAX) - fuzzy_skin_thickness;
            out.emplace_back(*p0 + (p0p1 * (p0pa_dist / p0p1_size) + perp(p0p1).cast<double>().normalized() * r).cast<coord_t>());
            dist_last_point = p0pa_dist;
        }
        dist_left_over = p0p1_size - dist_last_point;
        p0 = &p1;
    }
    while (out.size() < 3) {
        size_t point_idx = poly.size() - 2;
        out.emplace_back(poly[point_idx]);
        if (point_idx == 0)
            break;
        -- point_idx;
    }
    if (out.size() >= 3)
        poly.points = std::move(out);
}

using PerimeterGeneratorLoops = std::vector<PerimeterGeneratorLoop>;

static void lowpass_filter_by_paths_overhang_degree(ExtrusionPaths& paths) {
    const double filter_range = scale_(6.5);
    const double threshold_length = scale_(1.2);

    int path_num = paths.size();
    ExtrusionPaths out;

    //1.lowpass filter
    for (int i = 0; i < path_num; i++) {
        double current_length = paths[i].length();
        int current_overhang_degree = paths[i].get_overhang_degree();
        if (current_length < threshold_length &&
            (paths[i].role() == erPerimeter || paths[i].role() == erExternalPerimeter)) {
            double left_total_length = (filter_range - current_length) / 2;
            double right_total_length = left_total_length;
            int temp_overhang_degree;
            double temp_length;

            int j = i - 1;
            int index;
            std::vector<std::pair<double, int>> neighbor_path;
            while (left_total_length > 0) {
                index = (j < 0) ? path_num - 1 : j;
                if (paths[index].role() == erOverhangPerimeter)
                    break;
                temp_overhang_degree = paths[index].get_overhang_degree();
                temp_length = paths[index].length();
                if (temp_length > left_total_length) {
                    neighbor_path.emplace_back(std::pair<double, int>(left_total_length, temp_overhang_degree));
                }
                else {
                    neighbor_path.emplace_back(std::pair<double, int>(temp_length, temp_overhang_degree));
                }
                left_total_length -= temp_length;
                j = index;
                j--;
            }

            j = i + 1;
            while (right_total_length > 0) {
                index = j % path_num;
                if (paths[index].role() == erOverhangPerimeter)
                    break;
                temp_overhang_degree = paths[index].get_overhang_degree();
                temp_length = paths[index].length();
                if (temp_length > right_total_length) {
                    neighbor_path.emplace_back(std::pair<double, int>(right_total_length, temp_overhang_degree));
                }
                else {
                    neighbor_path.emplace_back(std::pair<double, int>(temp_length, temp_overhang_degree));
                }
                right_total_length -= temp_length;
                j++;
            }

            double sum = 0;
            double length_sum = 0;
            for (auto it = neighbor_path.begin(); it != neighbor_path.end(); it++) {
                sum += (it->first * it->second);
                length_sum += it->first;
            }

            double average_overhang = (double)(current_length * current_overhang_degree + sum) / (length_sum + current_length);
            ExtrusionPath new_overhang_path = paths[i];
            new_overhang_path.set_overhang_degree((int)average_overhang);
            out.push_back(new_overhang_path);
        }
        else {
            out.push_back(paths[i]);
        }
    }

    //2.merge path if have same overhang degree
    paths.clear();
    int last_overhang = -1;
    for (auto it = out.begin(); it != out.end(); it++) {
        if (last_overhang == it->get_overhang_degree()) {
            assert(paths.size() != 0);
            (paths.end() - 1)->polyline.append(it->polyline);
        }
        else {
            paths.push_back(*it);
            last_overhang = it->get_overhang_degree();
        }
    }
}

static void calculate_curvatures(ExtrusionPaths& paths)
{
    Polygon polygon;
    std::vector<float> paths_length(paths.size(), 0.0);
    for (size_t i = 0; i < paths.size(); i++) {
        if (i == 0) {
            paths_length[i] = paths[i].polyline.length();
        }
        else {
            paths_length[i] = paths_length[i - 1] + paths[i].polyline.length();
        }
        polygon.points.insert(polygon.points.end(), paths[i].polyline.points.begin(), paths[i].polyline.points.end() - 1);
    }
    // 1 generate point series which is on the line of polygon, point distance along the polygon is smaller than 1mm
    polygon.densify(scale_(1));
    std::vector<float> polygon_length = polygon.parameter_by_length();

    // 2 calculate angle of every segment
    size_t point_num = polygon.points.size();
    std::vector<float> angles(point_num, 0.f);
    for (size_t i = 0; i < point_num; i++) {
        size_t curr = i;
        size_t prev = (curr == 0) ? point_num - 1 : curr - 1;
        size_t next = (curr == point_num - 1) ? 0 : curr + 1;
        const Point  v1 = polygon.points[curr] - polygon.points[prev];
        const Point  v2 = polygon.points[next] - polygon.points[curr];
        int64_t dot = int64_t(v1(0)) * int64_t(v2(0)) + int64_t(v1(1)) * int64_t(v2(1));
        int64_t cross = int64_t(v1(0)) * int64_t(v2(1)) - int64_t(v1(1)) * int64_t(v2(0));
        angles[curr] = float(atan2(double(abs(cross)), double(dot)));
    }

    // 3 generate sum of angle and length of the adjacent segment for eveny point, range is approximately curvatures_sampling_width.
    //   And then calculate the curvature
    std::vector<float> sum_angles(point_num, 0.f);
    std::vector<double> average_curvatures(point_num, 0.f);
    if (paths_length.back() < scale_(curvatures_sampling_width)) {
        // loop is too short, so the curvatures is max
        double temp = sqrt(1000 * 2 * PI / (double)curvatures_sampling_width);
        for (size_t i = 0; i < point_num; i++) {
            average_curvatures[i] = temp;
        }
    }
    else {
        for (size_t i = 0; i < point_num; i++) {
            // right segment
            size_t j = i;
            float right_length = 0;
            while (right_length < scale_(curvatures_sampling_width / 2)) {
                int next_j = (j + 1 >= point_num) ? 0 : j + 1;
                sum_angles[i] += angles[j];
                right_length += (polygon.points[next_j] - polygon.points[j]).cast<float>().norm();
                j = next_j;
            }
            // left segment
            size_t k = i;
            float left_length = 0;
            while (left_length < scale_(curvatures_sampling_width / 2)) {
                size_t next_k = (k < 1) ? point_num - 1 : k - 1;
                sum_angles[i] += angles[k];
                left_length += (polygon.points[k] - polygon.points[next_k]).cast<float>().norm();
                k = next_k;
            }
            sum_angles[i] = sum_angles[i] - angles[i];
            average_curvatures[i] = (1000 * (double)sum_angles[i] / (double)curvatures_sampling_width);
        }
    }

    // 4 calculate the degree of curve
    //   For angle >= curvatures_angle_worst, we think it's enough to be worst. Should make the speed to be slowest.
    //   For angle <= curvatures_angle_best, we thins it's enough to be best. Should make the speed to be fastest.
    //   Use 11 steps [0 1 2...9 10] to describe the degree of curve. 0 is the flatest. 1 is the sharpest
    std::vector<int> curvatures_norm(point_num, 0.f);
    for (size_t i = 0; i < point_num; i++) {
        curvatures_norm[i] = (int)(100 * (average_curvatures[i] - curvatures_best) / (curvatures_worst - curvatures_best));
        curvatures_norm[i] = (curvatures_norm[i] < 5) ? 0 : ((curvatures_norm[i] > 95) ? 10 : (curvatures_norm[i] + 5) / 10);
    }
    std::vector<std::pair<std::pair<Point, int>, int>> curvature_list;   // point, index, curve_degree
    int last_curvature_norm = -1;
    for (int i = 0; i < point_num; i++) {
        if (curvatures_norm[i] != last_curvature_norm) {
            last_curvature_norm = curvatures_norm[i];
            curvature_list.push_back(std::pair<std::pair<Point, int>, int>(std::pair<Point, int>(polygon.points[i], i), last_curvature_norm));
        }
    }
    curvature_list.push_back(std::pair<std::pair<Point, int>, int>(std::pair<Point, int>(polygon.points[0], point_num), curvatures_norm[0])); // the last point should be the first point

    //5 split and modify the path according to the degree of curve
    if (curvature_list.size() == 2) {   // all paths has same curva_degree
        for (size_t i = 0; i < paths.size(); i++) {
            paths[i].set_curve_degree(curvature_list[0].second);
        }
    }
    else {
        ExtrusionPaths out;
        out.reserve(paths.size() + curvature_list.size() - 1);
        size_t j = 1;
        int current_curva_norm = curvature_list[0].second;
        for (size_t i = 0; i < paths.size() && j < curvature_list.size(); i++) {
            if (paths[i].last_point() == curvature_list[j].first.first) {
                paths[i].set_curve_degree(current_curva_norm);
                out.push_back(paths[i]);
                current_curva_norm = curvature_list[j].second;
                j++;
                continue;
            }
            else if (paths[i].first_point() == curvature_list[j].first.first) {
                if (paths[i].polyline.is_closed()) {
                    paths[i].set_curve_degree(current_curva_norm);
                    out.push_back(paths[i]);
                    current_curva_norm = curvature_list[j].second;
                    j++;
                    continue;
                }
                else {
                    // should never happen
                    assert(0);
                }
            }

            if (paths_length[i] <= polygon_length[curvature_list[j].first.second] ||
                paths[i].last_point() == curvature_list[j].first.first) {
                // save paths[i] directly
                paths[i].set_curve_degree(current_curva_norm);
                out.push_back(paths[i]);
                if (paths[i].last_point() == curvature_list[j].first.first) {
                    current_curva_norm = curvature_list[j].second;
                    j++;
                }
            }
            else {
                //split paths[i]
                ExtrusionPath current_path = paths[i];
                while (j < curvature_list.size()) {
                    Polyline left, right;
                    current_path.polyline.split_at(curvature_list[j].first.first, &left, &right);
                    ExtrusionPath left_path(left, current_path);
                    left_path.set_curve_degree(current_curva_norm);
                    out.push_back(left_path);
                    ExtrusionPath right_path(right, current_path);
                    current_path = right_path;

                    current_curva_norm = curvature_list[j].second;
                    j++;
                    if (j < curvature_list.size() &&
                        (paths_length[i] <= polygon_length[curvature_list[j].first.second] ||
                            paths[i].last_point() == curvature_list[j].first.first)) {
                        current_path.set_curve_degree(current_curva_norm);
                        out.push_back(current_path);
                        if (current_path.last_point() == curvature_list[j].first.first) {
                            current_curva_norm = curvature_list[j].second;
                            j++;
                        }
                        break;
                    }
                }
            }
        }

        paths.clear();
        paths.reserve(out.size());
        for (int i = 0; i < out.size(); i++) {
            paths.push_back(out[i]);
        }
    }
}

static ExtrusionEntityCollection traverse_loops(const PerimeterGenerator &perimeter_generator, const PerimeterGeneratorLoops &loops, ThickPolylines &thin_walls)
{
    // loops is an arrayref of ::Loop objects
    // turn each one into an ExtrusionLoop object
    ExtrusionEntityCollection   coll;
    Polygon                     fuzzified;
    for (const PerimeterGeneratorLoop &loop : loops) {
        bool is_external = loop.is_external();
        
        ExtrusionRole role;
        ExtrusionLoopRole loop_role;
        role = is_external ? erExternalPerimeter : erPerimeter;
        if (loop.is_internal_contour()) {
            // Note that we set loop role to ContourInternalPerimeter
            // also when loop is both internal and external (i.e.
            // there's only one contour loop).
            loop_role = elrContourInternalPerimeter;
        } else {
            loop_role = elrDefault;
        }
        
        // detect overhanging/bridging perimeters
        ExtrusionPaths paths;
        const Polygon &polygon = loop.fuzzify ? fuzzified : loop.polygon;
        if (loop.fuzzify) {
            fuzzified = loop.polygon;
            fuzzy_polygon(fuzzified, scaled<float>(perimeter_generator.config->fuzzy_skin_thickness.value), scaled<float>(perimeter_generator.config->fuzzy_skin_point_dist.value));
        }
        if (perimeter_generator.config->overhangs && perimeter_generator.layer_id > perimeter_generator.object_config->raft_layers
            && ! ((perimeter_generator.object_config->support_material || perimeter_generator.object_config->support_material_enforce_layers > 0) && 
                  perimeter_generator.object_config->support_material_contact_distance.value == 0)) {
            // get non 100% overhang paths by intersecting this loop with the grown lower slices
            Polylines remain_polines;
            for (auto it = perimeter_generator.m_lower_polygons_series.begin();
                it != perimeter_generator.m_lower_polygons_series.end(); it++)
            {
                if (it == perimeter_generator.m_lower_polygons_series.begin()) {
                    extrusion_paths_append(
                        paths,
                        intersection_pl({ polygon }, it->second),
                        (float)(0),
                        int(0),
                        role,
                        is_external ? perimeter_generator.ext_mm3_per_mm() : perimeter_generator.mm3_per_mm(),
                        is_external ? perimeter_generator.ext_perimeter_flow.width() : perimeter_generator.perimeter_flow.width(),
                        (float)perimeter_generator.layer_height);

                    remain_polines = diff_pl({ polygon }, it->second);
                }
                else {
                    extrusion_paths_append(
                        paths,
                        intersection_pl({ remain_polines }, it->second),
                        (int)(it->first * 10),
                        int(0),
                        role,
                        is_external ? perimeter_generator.ext_mm3_per_mm() : perimeter_generator.mm3_per_mm(),
                        is_external ? perimeter_generator.ext_perimeter_flow.width() : perimeter_generator.perimeter_flow.width(),
                        (float)perimeter_generator.layer_height);

                    remain_polines = diff_pl({ remain_polines }, it->second);
                }

                if (remain_polines.size() == 0)
                    break;
            }

            // get 100% overhang paths by checking what parts of this loop fall
            // outside the grown lower slices (thus where the distance between
            // the loop centerline and original lower slices is >= half nozzle diameter
            if (remain_polines.size() != 0) {
                extrusion_paths_append(
                    paths,
                    remain_polines,
                    (int)10,
                    int(0),
                    erOverhangPerimeter,
                    perimeter_generator.mm3_per_mm_overhang(),
                    perimeter_generator.overhang_flow.width(),
                    perimeter_generator.overhang_flow.height());
            }
            
            // Reapply the nearest point search for starting point.
            // We allow polyline reversal because Clipper may have randomly reversed polylines during clipping.
            chain_and_reorder_extrusion_paths(paths, &paths.front().first_point());
            // smothing the overhang degree
            // merge small path between paths which have same overhang degree
            lowpass_filter_by_paths_overhang_degree(paths);
        } else {
            ExtrusionPath path(role);
            //BBS.
            path.polyline = polygon.split_at_first_point();
            path.overhang_degree = 0;
            path.curve_degree = 0;
            path.mm3_per_mm = is_external ? perimeter_generator.ext_mm3_per_mm() : perimeter_generator.mm3_per_mm();
            path.width = is_external ? perimeter_generator.ext_perimeter_flow.width() : perimeter_generator.perimeter_flow.width();

            path.height     = (float)perimeter_generator.layer_height;
            paths.push_back(path);
        }
        
        // check all paths of the loop and generate curvature
        // this step will modify the segment of paths as well
        calculate_curvatures(paths);

        coll.append(ExtrusionLoop(std::move(paths), loop_role));
    }
    
    // Append thin walls to the nearest-neighbor search (only for first iteration)
    if (! thin_walls.empty()) {
        variable_width(thin_walls, erExternalPerimeter, perimeter_generator.ext_perimeter_flow, coll.entities);
        thin_walls.clear();
    }
    
    // Traverse children and build the final collection.
	Point zero_point(0, 0);
	std::vector<std::pair<size_t, bool>> chain = chain_extrusion_entities(coll.entities, &zero_point);
    ExtrusionEntityCollection out;
    for (const std::pair<size_t, bool> &idx : chain) {
		assert(coll.entities[idx.first] != nullptr);
        if (idx.first >= loops.size()) {
            // This is a thin wall.
			out.entities.reserve(out.entities.size() + 1);
            out.entities.emplace_back(coll.entities[idx.first]);
			coll.entities[idx.first] = nullptr;
            if (idx.second)
				out.entities.back()->reverse();
        } else {
            const PerimeterGeneratorLoop &loop = loops[idx.first];
            assert(thin_walls.empty());
            ExtrusionEntityCollection children = traverse_loops(perimeter_generator, loop.children, thin_walls);
            out.entities.reserve(out.entities.size() + children.entities.size() + 1);
            ExtrusionLoop *eloop = static_cast<ExtrusionLoop*>(coll.entities[idx.first]);
            coll.entities[idx.first] = nullptr;
            if (loop.is_contour) {
                eloop->make_counter_clockwise();
                out.append(std::move(children.entities));
                out.entities.emplace_back(eloop);
            } else {
                eloop->make_clockwise();
                out.entities.emplace_back(eloop);
                out.append(std::move(children.entities));
            }
        }
    }
    return out;
}

void PerimeterGenerator::process()
{
    // other perimeters
    m_mm3_per_mm               		= this->perimeter_flow.mm3_per_mm();
    coord_t perimeter_width         = this->perimeter_flow.scaled_width();
    coord_t perimeter_spacing       = this->perimeter_flow.scaled_spacing();
    
    // external perimeters
    m_ext_mm3_per_mm           		= this->ext_perimeter_flow.mm3_per_mm();
    coord_t ext_perimeter_width     = this->ext_perimeter_flow.scaled_width();
    coord_t ext_perimeter_spacing   = this->ext_perimeter_flow.scaled_spacing();
    coord_t ext_perimeter_spacing2  = scaled<coord_t>(0.5f * (this->ext_perimeter_flow.spacing() + this->perimeter_flow.spacing()));
    
    // overhang perimeters
    m_mm3_per_mm_overhang      		= this->overhang_flow.mm3_per_mm();
    
    // solid infill
    coord_t solid_infill_spacing    = this->solid_infill_flow.scaled_spacing();
    
    // Calculate the minimum required spacing between two adjacent traces.
    // This should be equal to the nominal flow spacing but we experiment
    // with some tolerance in order to avoid triggering medial axis when
    // some squishing might work. Loops are still spaced by the entire
    // flow spacing; this only applies to collapsing parts.
    // For ext_min_spacing we use the ext_perimeter_spacing calculated for two adjacent
    // external loops (which is the correct way) instead of using ext_perimeter_spacing2
    // which is the spacing between external and internal, which is not correct
    // and would make the collapsing (thus the details resolution) dependent on 
    // internal flow which is unrelated.
    coord_t min_spacing         = coord_t(perimeter_spacing      * (1 - INSET_OVERLAP_TOLERANCE));
    coord_t ext_min_spacing     = coord_t(ext_perimeter_spacing  * (1 - INSET_OVERLAP_TOLERANCE));
    bool    has_gap_fill 		= this->config->gap_fill_enabled.value && this->config->gap_fill_speed.value > 0;

    // prepare grown lower layer slices for overhang detection
    if (this->lower_slices != NULL && this->config->overhangs) {
        // We consider overhang any part where the entire nozzle diameter is not supported by the
        // lower layer, so we take lower slices and offset them by half the nozzle diameter used 
        // in the current layer
        double nozzle_diameter = this->print_config->nozzle_diameter.get_at(this->config->perimeter_extruder-1);
        m_lower_slices_polygons = offset(*this->lower_slices, float(scale_(+nozzle_diameter/2)));
    }

    generate_lower_polygons_series();

    // we need to process each island separately because we might have different
    // extra perimeters for each one
    for (const Surface &surface : this->slices->surfaces) {
        // detect how many perimeters must be generated for this island
        int        loop_number = this->config->perimeters + surface.extra_perimeters - 1;  // 0-indexed loops
        ExPolygons last        = union_ex(surface.expolygon.simplify_p(m_scaled_resolution));
        ExPolygons gaps;
        if (loop_number >= 0) {
            // In case no perimeters are to be generated, loop_number will equal to -1.
            std::vector<PerimeterGeneratorLoops> contours(loop_number+1);    // depth => loops
            std::vector<PerimeterGeneratorLoops> holes(loop_number+1);       // depth => loops
            ThickPolylines thin_walls;
            // we loop one time more than needed in order to find gaps after the last perimeter was applied
            for (int i = 0;; ++ i) {  // outer loop is 0
                // Calculate next onion shell of perimeters.
                ExPolygons offsets;
                if (i == 0) {
                    // the minimum thickness of a single loop is:
                    // ext_width/2 + ext_spacing/2 + spacing/2 + width/2
                    offsets = this->config->thin_walls ? 
                        offset2_ex(
                            last,
                            - float(ext_perimeter_width / 2. + ext_min_spacing / 2. - 1),
                            + float(ext_min_spacing / 2. - 1)) :
                        offset_ex(last, - float(ext_perimeter_width / 2.));
                    // look for thin walls
                    if (this->config->thin_walls) {
                        // the following offset2 ensures almost nothing in @thin_walls is narrower than $min_width
                        // (actually, something larger than that still may exist due to mitering or other causes)
                        coord_t min_width = coord_t(scale_(this->ext_perimeter_flow.nozzle_diameter() / 3));
                        ExPolygons expp = opening_ex(
                            // medial axis requires non-overlapping geometry
                            diff_ex(last, offset(offsets, float(ext_perimeter_width / 2.) + ClipperSafetyOffset)),
                            float(min_width / 2.));
                        // the maximum thickness of our thin wall area is equal to the minimum thickness of a single loop
                        for (ExPolygon &ex : expp)
                            ex.medial_axis(ext_perimeter_width + ext_perimeter_spacing2, min_width, &thin_walls);
                    }
                    if (m_spiral_vase && offsets.size() > 1) {
                    	// Remove all but the largest area polygon.
                    	keep_largest_contour_only(offsets);
                    }
                } else {
                    //FIXME Is this offset correct if the line width of the inner perimeters differs
                    // from the line width of the infill?
                    coord_t distance = (i == 1) ? ext_perimeter_spacing2 : perimeter_spacing;
                    offsets = this->config->thin_walls ?
                        // This path will ensure, that the perimeters do not overfill, as in 
                        // prusa3d/Slic3r GH #32, but with the cost of rounding the perimeters
                        // excessively, creating gaps, which then need to be filled in by the not very 
                        // reliable gap fill algorithm.
                        // Also the offset2(perimeter, -x, x) may sometimes lead to a perimeter, which is larger than
                        // the original.
                        offset2_ex(last,
                                - float(distance + min_spacing / 2. - 1.),
                                float(min_spacing / 2. - 1.)) :
                        // If "detect thin walls" is not enabled, this paths will be entered, which 
                        // leads to overflows, as in prusa3d/Slic3r GH #32
                        offset_ex(last, - float(distance));
                    // look for gaps
                    if (has_gap_fill)
                        // not using safety offset here would "detect" very narrow gaps
                        // (but still long enough to escape the area threshold) that gap fill
                        // won't be able to fill but we'd still remove from infill area
                        append(gaps, diff_ex(
                            offset(last,    - float(0.5 * distance)),
                            offset(offsets,   float(0.5 * distance + 10))));  // safety offset
                }
                if (offsets.empty()) {
                    // Store the number of loops actually generated.
                    loop_number = i - 1;
                    // No region left to be filled in.
                    last.clear();
                    break;
                } else if (i > loop_number) {
                    // If i > loop_number, we were looking just for gaps.
                    break;
                }
                {
                    const bool fuzzify_contours = this->config->fuzzy_skin != FuzzySkinType::None && i == 0 && this->layer_id > 0;
                    const bool fuzzify_holes    = fuzzify_contours && this->config->fuzzy_skin == FuzzySkinType::All;
                    for (const ExPolygon &expolygon : offsets) {
    	                // Outer contour may overlap with an inner contour,
    	                // inner contour may overlap with another inner contour,
    	                // outer contour may overlap with itself.
    	                //FIXME evaluate the overlaps, annotate each point with an overlap depth,
                        // compensate for the depth of intersection.
                        contours[i].emplace_back(expolygon.contour, i, true, fuzzify_contours);

                        if (! expolygon.holes.empty()) {
                            holes[i].reserve(holes[i].size() + expolygon.holes.size());
                            for (const Polygon &hole : expolygon.holes)
                                holes[i].emplace_back(hole, i, false, fuzzify_holes);
                        }
                    }
                }
                last = std::move(offsets);
                if (i == loop_number && (! has_gap_fill || this->config->fill_density.value == 0)) {
                	// The last run of this loop is executed to collect gaps for gap fill.
                	// As the gap fill is either disabled or not 
                	break;
                }
            }

            // nest loops: holes first
            for (int d = 0; d <= loop_number; ++ d) {
                PerimeterGeneratorLoops &holes_d = holes[d];
                // loop through all holes having depth == d
                for (int i = 0; i < (int)holes_d.size(); ++ i) {
                    const PerimeterGeneratorLoop &loop = holes_d[i];
                    // find the hole loop that contains this one, if any
                    for (int t = d + 1; t <= loop_number; ++ t) {
                        for (int j = 0; j < (int)holes[t].size(); ++ j) {
                            PerimeterGeneratorLoop &candidate_parent = holes[t][j];
                            if (candidate_parent.polygon.contains(loop.polygon.first_point())) {
                                candidate_parent.children.push_back(loop);
                                holes_d.erase(holes_d.begin() + i);
                                -- i;
                                goto NEXT_LOOP;
                            }
                        }
                    }
                    // if no hole contains this hole, find the contour loop that contains it
                    for (int t = loop_number; t >= 0; -- t) {
                        for (int j = 0; j < (int)contours[t].size(); ++ j) {
                            PerimeterGeneratorLoop &candidate_parent = contours[t][j];
                            if (candidate_parent.polygon.contains(loop.polygon.first_point())) {
                                candidate_parent.children.push_back(loop);
                                holes_d.erase(holes_d.begin() + i);
                                -- i;
                                goto NEXT_LOOP;
                            }
                        }
                    }
                    NEXT_LOOP: ;
                }
            }
            // nest contour loops
            for (int d = loop_number; d >= 1; -- d) {
                PerimeterGeneratorLoops &contours_d = contours[d];
                // loop through all contours having depth == d
                for (int i = 0; i < (int)contours_d.size(); ++ i) {
                    const PerimeterGeneratorLoop &loop = contours_d[i];
                    // find the contour loop that contains it
                    for (int t = d - 1; t >= 0; -- t) {
                        for (size_t j = 0; j < contours[t].size(); ++ j) {
                            PerimeterGeneratorLoop &candidate_parent = contours[t][j];
                            if (candidate_parent.polygon.contains(loop.polygon.first_point())) {
                                candidate_parent.children.push_back(loop);
                                contours_d.erase(contours_d.begin() + i);
                                -- i;
                                goto NEXT_CONTOUR;
                            }
                        }
                    }
                    NEXT_CONTOUR: ;
                }
            }
            // at this point, all loops should be in contours[0]
            ExtrusionEntityCollection entities = traverse_loops(*this, contours.front(), thin_walls);
            // if brim will be printed, reverse the order of perimeters so that
            // we continue inwards after having finished the brim
            // TODO: add test for perimeter order
            if (this->config->external_perimeters_first || 
                (this->layer_id == 0 && this->object_config->brim_width.value > 0))
                entities.reverse();
            // append perimeters for this slice as a collection
            if (! entities.empty())
                this->loops->append(entities);
        } // for each loop of an island

        // fill gaps
        if (! gaps.empty()) {
            // collapse 
            double min = 0.2 * perimeter_width * (1 - INSET_OVERLAP_TOLERANCE);
            double max = 2. * perimeter_spacing;
            ExPolygons gaps_ex = diff_ex(
                //FIXME offset2 would be enough and cheaper.
                opening_ex(gaps, float(min / 2.)),
                offset2_ex(gaps, - float(max / 2.), float(max / 2. + ClipperSafetyOffset)));
            ThickPolylines polylines;
            for (const ExPolygon &ex : gaps_ex)
                ex.medial_axis(max, min, &polylines);
            if (! polylines.empty()) {
				ExtrusionEntityCollection gap_fill;
				variable_width(polylines, erGapFill, this->solid_infill_flow, gap_fill.entities);
                /*  Make sure we don't infill narrow parts that are already gap-filled
                    (we only consider this surface's gaps to reduce the diff() complexity).
                    Growing actual extrusions ensures that gaps not filled by medial axis
                    are not subtracted from fill surfaces (they might be too short gaps
                    that medial axis skips but infill might join with other infill regions
                    and use zigzag).  */
                //FIXME Vojtech: This grows by a rounded extrusion width, not by line spacing,
                // therefore it may cover the area, but no the volume.
                last = diff_ex(last, gap_fill.polygons_covered_by_width(10.f));
				this->gap_fill->append(std::move(gap_fill.entities));
			}
        }

        // create one more offset to be used as boundary for fill
        // we offset by half the perimeter spacing (to get to the actual infill boundary)
        // and then we offset back and forth by half the infill spacing to only consider the
        // non-collapsing regions
        coord_t inset = 
            (loop_number < 0) ? 0 :
            (loop_number == 0) ?
                // one loop
                ext_perimeter_spacing / 2 :
                // two or more loops?
                perimeter_spacing / 2;
        // only apply infill overlap if we actually have one perimeter
        if (inset > 0)
            inset -= coord_t(scale_(this->config->get_abs_value("infill_overlap", unscale<double>(inset + solid_infill_spacing / 2))));
        // simplify infill contours according to resolution
        Polygons pp;
        for (ExPolygon &ex : last)
            ex.simplify_p(m_scaled_resolution, &pp);
        // collapse too narrow infill areas
        coord_t min_perimeter_infill_spacing = coord_t(solid_infill_spacing * (1. - INSET_OVERLAP_TOLERANCE));
        // append infill areas to fill_surfaces
        this->fill_surfaces->append(
            offset2_ex(
                union_ex(pp),
                float(- inset - min_perimeter_infill_spacing / 2.),
                float(min_perimeter_infill_spacing / 2.)),
            stInternal);
    } // for each island
}

bool PerimeterGeneratorLoop::is_internal_contour() const
{
    // An internal contour is a contour containing no other contours
    if (! this->is_contour)
        return false;
    for (const PerimeterGeneratorLoop &loop : this->children)
        if (loop.is_contour)
            return false;
    return true;
}

void PerimeterGenerator::generate_lower_polygons_series()
{
    float width = perimeter_flow.width();
    float nozzle_diameter = print_config->nozzle_diameter.get_at(config->perimeter_extruder - 1);
    float start_offset = -0.5 * width;
    float end_offset = 0.5 * nozzle_diameter;

    // generate offsets
    std::vector<float> offset_series;
    offset_series.reserve(sampling_number - 1);
    for (int i = 0; i < sampling_number - 1; i++) {
        // 5% 15% 25% ... 95%
        offset_series.push_back(start_offset + (i + 0.5) * (end_offset - start_offset) / 10);
    }

    if (this->lower_slices == NULL) {
        return;
    }

    // offset expolygon to generate series of polygons
    float delta = 1.0 / (offset_series.size() - 1);

    for (int i = 1; i < offset_series.size(); i++) {
        m_lower_polygons_series.insert(std::pair<float, Polygons>((i - 0.5) * delta, offset(*this->lower_slices, float(scale_(offset_series[i])))));
    }
}

}
