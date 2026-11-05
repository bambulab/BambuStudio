#include "WipeTower.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <cassert>
#include <iostream>
#include <vector>
#include <numeric>
#include <set>
#include <sstream>
#include <iomanip>
#include <optional>
#include <stdexcept>
#include "GCodeProcessor.hpp"
#include "../AdaptiveLayerMerge.hpp"
#include "BoundingBox.hpp"
#include "ClipperUtils.hpp"
#include "LocalesUtils.hpp"
#include "Triangulation.hpp"
#include "libslic3r/Log.hpp"


namespace Slic3r
{
static const double wipe_tower_wall_infill_overlap = 0.0;
static constexpr double WIPE_TOWER_RESOLUTION = 0.1;
static constexpr double WT_SIMPLIFY_TOLERANCE_SCALED   = 0.001 / SCALING_FACTOR;
static constexpr int    arc_fit_size = 20;
#define SCALED_WIPE_TOWER_RESOLUTION (WIPE_TOWER_RESOLUTION / SCALING_FACTOR)
enum class LimitFlow { None, LimitPrintFlow, LimitRammingFlow, LimitRammingFlowNC};//nc:nozzle change
static const std::map<float, float> nozzle_diameter_to_nozzle_change_width{{0.2f, 0.5f}, {0.4f, 1.0f}, {0.6f, 1.2f}, {0.8f, 1.4f}};

static float nozzle_change_width_for_diameter(float nozzle_diameter)
{
    auto best = nozzle_diameter_to_nozzle_change_width.begin();
    for (auto iter = std::next(nozzle_diameter_to_nozzle_change_width.begin()); iter != nozzle_diameter_to_nozzle_change_width.end(); ++iter)
        if (std::abs(iter->first - nozzle_diameter) < std::abs(best->first - nozzle_diameter))
            best = iter;
    return best->second;
}

bool wipe_tower_sparse_layers_skipped(const PrintConfig &config)
{
    return config.wipe_tower_no_sparse_layers.value && config.timelapse_type.value != TimelapseType::tlSmooth &&
           ! config.enable_wrapping_detection.value;
}

bool wipe_tower_layer_is_sparse(const std::vector<WipeTower::ToolChangeResult> &layer_tool_changes)
{
    return layer_tool_changes.size() == 1 && layer_tool_changes.front().initial_tool == layer_tool_changes.front().new_tool;
}

double prime_tower_side_scrape_margin(const PrintConfig &config)
{
    if (! config.prime_tower_side_scrape.value)
        return 0.;
    // The diagonal starts one scrape length outside the line that already sits the wall gap outside the
    // wall, so that sum is the outermost point the nozzle reaches.
    return std::max(0., config.prime_tower_side_scrape_wall_gap.value + config.prime_tower_side_scrape_length.value);
}

std::vector<float> compute_compacted_wipe_tower_z(const std::vector<std::vector<WipeTower::ToolChangeResult>> &tool_changes)
{
    std::vector<float> tower_z(tool_changes.size(), 0.f);
    float              last = 0.f;
    for (size_t i = 0; i < tool_changes.size(); ++i) {
        if (! tool_changes[i].empty() && ! wipe_tower_layer_is_sparse(tool_changes[i]))
            last += tool_changes[i].front().layer_height;
        tower_z[i] = last;
    }
    return tower_z;
}

static std::vector<double> parseDoubles(const std::string &str)
{
    std::vector<double> result;
    if (str.empty()) return result;
    // std::stod follows the C locale: under a comma-decimal locale (French, German, ...) it reads "0.5" as 0.
    auto to_double = [](const std::string &token) {
        const size_t first = token.find_first_not_of(" \t");
        size_t       pos   = 0;
        const double value = first == std::string::npos ? 0. : string_to_double_decimal_point(std::string_view(token).substr(first), &pos);
        if (pos == 0) throw std::invalid_argument(token);
        return value;
    };
    try {
        size_t start = 0, end;
        while ((end = str.find(',', start)) != std::string::npos) {
            result.push_back(to_double(str.substr(start, end - start)));
            start = end + 1;
        }
        result.push_back(to_double(str.substr(start)));
    } catch (...) {
        BOOST_LOG_TRIVIAL(warning) << "Failed to parse prime_tower_speed_factor_curve: \"" << str << "\", using built-in default";
        return {};
    }
    for (auto &v : result) {
        if (v > 5.0) v = 5.0;
        if (v < 0.01) v = 0.01;
    }
    return result;
}

inline float align_round(float value, float base)
{
    return std::round(value / base) * base;
}

inline float align_ceil(float value, float base)
{
    return std::ceil(value / base) * base;
}

inline float align_floor(float value, float base)
{
    return std::floor((value) / base) * base;
}

static bool is_valid_gcode(const std::string &gcode)
{
    int  str_size    = gcode.size();
    int  start_index = 0;
    int  end_index   = 0;
    bool is_valid    = false;
    while (end_index < str_size) {
        if (gcode[end_index] != '\n') {
            end_index++;
            continue;
        }

        if (end_index > start_index) {
            std::string line_str = gcode.substr(start_index, end_index - start_index);
            line_str.erase(0, line_str.find_first_not_of(" "));
            line_str.erase(line_str.find_last_not_of(" ") + 1);
            if (!line_str.empty() && line_str[0] != ';') {
                is_valid = true;
                break;
            }
        }

        start_index = end_index + 1;
        end_index   = start_index;
    }

    return is_valid;
}

Polygon chamfer_polygon(Polygon &polygon, double chamfer_dis = 2., double angle_tol = 30. / 180. * PI)
{
    if (polygon.points.size() < 3) return polygon;
    Polygon res;
    res.points.reserve(polygon.points.size() * 2);
    int    mod           = polygon.points.size();
    double cos_angle_tol = abs(std::cos(angle_tol));

    for (int i = 0; i < polygon.points.size(); i++) {
        Vec2d  a        = unscaled(polygon.points[(i - 1 + mod) % mod]);
        Vec2d  b        = unscaled(polygon.points[i]);
        Vec2d  c        = unscaled(polygon.points[(i + 1) % mod]);
        double ab_len   = (a - b).norm();
        double bc_len   = (b - c).norm();
        Vec2d  ab       = (b - a) / ab_len;
        Vec2d  bc       = (c - b) / bc_len;
        assert(ab_len != 0);
        assert(bc_len != 0);
        float  cosangle = ab.dot(bc);
        //std::cout << " angle " << acos(cosangle) << " cosangle " << cosangle << std::endl;
        //std::cout << " ab_len " << ab_len << " bc_len " << bc_len << std::endl;
        if (abs(cosangle) < cos_angle_tol) {
            float real_chamfer_dis = std::min({chamfer_dis, ab_len / 2.1, bc_len / 2.1}); // 2.1 to ensure the points do not coincide
            Vec2d left             = b - ab * real_chamfer_dis;
            Vec2d right            = b + bc * real_chamfer_dis;
            res.points.push_back(scaled(left));
            res.points.push_back(scaled(right));
        } else
            res.points.push_back(polygon.points[i]);
    }
    res.points.shrink_to_fit();
    return res;
}

Polygon WipeTower::rounding_polygon(Polygon &polygon, double rounding /*= 2.*/, double angle_tol/* = 30. / 180. * PI*/)
{
    if (polygon.points.size() < 3) return polygon;
    Polygon res;
    res.points.reserve(polygon.points.size() * 2);
    int    mod           = polygon.points.size();
    double cos_angle_tol = abs(std::cos(angle_tol));

    for (int i = 0; i < polygon.points.size(); i++) {
        Vec2d  a      = unscaled(polygon.points[(i - 1 + mod) % mod]);
        Vec2d  b      = unscaled(polygon.points[i]);
        Vec2d  c      = unscaled(polygon.points[(i + 1) % mod]);
        double ab_len = (a - b).norm();
        double bc_len = (b - c).norm();
        Vec2d  ab     = (b - a) / ab_len;
        Vec2d  bc     = (c - b) / bc_len;
        assert(ab_len != 0);
        assert(bc_len != 0);
        float cosangle = ab.dot(bc);
        cosangle       = std::clamp(cosangle, -1.f, 1.f);
        bool  is_ccw   = cross2(ab, bc) > 0;
        if (abs(cosangle) < cos_angle_tol) {
            float real_rounding_dis = std::min({rounding, ab_len / 2.1, bc_len / 2.1}); // 2.1 to ensure the points do not coincide
            Vec2d left              = b - ab * real_rounding_dis;
            Vec2d right             = b + bc * real_rounding_dis;
            //Point r_left            = scaled(left);
            //Point r_right            = scaled(right);
           // std::cout << " r_left  " << r_left[0] << " " << r_left[1] << std::endl;
            //std::cout << " r_right  " << r_right[0] << " " << r_right[1] << std::endl;
            {
                float half_angle = std::acos(cosangle)/2.f;
                //std::cout << " half_angle  " << cos(half_angle) << std::endl;

                Vec2d dir        = (right - left).normalized();
                dir              = Vec2d{-dir[1], dir[0]};
                dir                  = is_ccw ? dir : -dir;
                double dis       = real_rounding_dis / sin(half_angle);
                //std::cout << " dis  " << dis << std::endl;

                Vec2d  center    = b + dir * dis;
                double radius    = (left - center).norm();
                ArcSegment arc(scaled(center), scaled(radius), scaled(left), scaled(right), is_ccw ? ArcDirection::Arc_Dir_CCW : ArcDirection::Arc_Dir_CW);
                int        n            = arc_fit_size;
                //std::cout << "start  " << arc.start_point[0] << " " << arc.start_point[1] << std::endl;
                //std::cout << "end  " << arc.end_point[0] << " " << arc.end_point[1] << std::endl;
                //std::cout << "start angle   " << arc.polar_start_theta << " end angle " << arc.polar_end_theta << std::endl;
                for (int j = 0; j < n; j++) {
                    float cur_angle = arc.polar_start_theta + (float)j/n * arc.angle_radians ;
                    //std::cout << " cur_angle " << cur_angle << std::endl;
                    if (cur_angle > 2 * PI)
                        cur_angle -= 2 * PI;
                    else if (cur_angle < 0)
                        cur_angle += 2 * PI;
                    Point tmp = arc.center + Point{arc.radius * std::cos(cur_angle), arc.radius *std::sin(cur_angle)};
                    //std::cout << "j = " << j << std::endl;
                    //std::cout << "tmp  = " << tmp[0]<<" "<<tmp[1] << std::endl;
                    res.points.push_back(tmp);
                }
            }
            res.points.push_back(scaled(right));
        } else
            res.points.push_back(polygon.points[i]);
    }
    res.remove_duplicate_points();
    res.points.shrink_to_fit();
    return res;
}

Polygon rounding_rectangle(Polygon &polygon, double rounding = 2., double angle_tol = 30. / 180. * PI) {
    if (polygon.points.size() < 3) return polygon;
    Polygon res;
    res.points.reserve(polygon.points.size() * 2);
    int    mod           = polygon.points.size();
    double cos_angle_tol = abs(std::cos(angle_tol));

    for (int i = 0; i < polygon.points.size(); i++) {
        Vec2d  a      = unscaled(polygon.points[(i - 1 + mod) % mod]);
        Vec2d  b      = unscaled(polygon.points[i]);
        Vec2d  c      = unscaled(polygon.points[(i + 1) % mod]);
        double ab_len = (a - b).norm();
        double bc_len = (b - c).norm();
        Vec2d  ab     = (b - a) / ab_len;
        Vec2d  bc     = (c - b) / bc_len;
        assert(ab_len != 0);
        assert(bc_len != 0);
        float cosangle = ab.dot(bc);
        cosangle       = std::clamp(cosangle, -1.f, 1.f);
        bool is_ccw    = cross2(ab, bc) > 0;
        if (abs(cosangle) < cos_angle_tol) {
            float real_rounding_dis = std::min({rounding, ab_len / 2.1, bc_len / 2.1}); // 2.1 to ensure the points do not coincide
            Vec2d left              = b - ab * real_rounding_dis;
            Vec2d right             = b + bc * real_rounding_dis;
            //Point r_left            = scaled(left);
            //Point r_right           = scaled(right);
            // std::cout << " r_left  " << r_left[0] << " " << r_left[1] << std::endl;
            // std::cout << " r_right  " << r_right[0] << " " << r_right[1] << std::endl;
            {
                Vec2d      center = b;
                double     radius = real_rounding_dis;
                ArcSegment arc(scaled(center), scaled(radius), scaled(left), scaled(right), is_ccw ? ArcDirection::Arc_Dir_CCW : ArcDirection::Arc_Dir_CW);
                int        n            = arc_fit_size;
                // std::cout << "start  " << arc.start_point[0] << " " << arc.start_point[1] << std::endl;
                // std::cout << "end  " << arc.end_point[0] << " " << arc.end_point[1] << std::endl;
                // std::cout << "start angle   " << arc.polar_start_theta << " end angle " << arc.polar_end_theta << std::endl;
                for (int j = 0; j < n; j++) {
                    float cur_angle = arc.polar_start_theta + (float) j / n * arc.angle_radians;
                    // std::cout << " cur_angle " << cur_angle << std::endl;
                    if (cur_angle > 2 * PI)
                        cur_angle -= 2 * PI;
                    else if (cur_angle < 0)
                        cur_angle += 2 * PI;
                    Point tmp = arc.center + Point{arc.radius * std::cos(cur_angle), arc.radius * std::sin(cur_angle)};
                    // std::cout << "j = " << j << std::endl;
                    // std::cout << "tmp  = " << tmp[0]<<" "<<tmp[1] << std::endl;
                    res.points.push_back(tmp);
                }
            }
            res.points.push_back(scaled(right));
        } else
            res.points.push_back(polygon.points[i]);
    }
    res.points.shrink_to_fit();
    return res;
}

std::pair<bool, Vec2f> ray_intersetion_line(const Vec2f &a, const Vec2f &v1, const Vec2f &b, const Vec2f &c)
{
    const Vec2f v2    = c - b;
    double      denom = cross2(v1, v2);
    if (fabs(denom) < EPSILON) return {false, Vec2f(0, 0)};
    const Vec2f v12    = (a - b);
    double      nume_a = cross2(v2, v12);
    double      nume_b = cross2(v1, v12);
    double      t1     = nume_a / denom;
    double      t2     = nume_b / denom;
    if (t1 >= 0 && t2 >= 0 && t2 <= 1.) {
        // Get the intersection point.
        Vec2f res = a + t1 * v1;
        return std::pair<bool, Vec2f>(true, res);
    }
    return std::pair<bool, Vec2f>(false, Vec2f{0, 0});
}
Polygon scale_polygon(const std::vector<Vec2f> &points) {
    Polygon res;
    for (const auto &p : points) res.points.push_back(scaled(p));
    return res;
}
std::vector<Vec2f> unscale_polygon(const Polygon& polygon)
{
    std::vector<Vec2f> res;
    for (const auto &p : polygon.points) res.push_back(unscaled<float>(p));
    return res;
}

Polygon generate_rectange(const Line &line, coord_t offset)
{
    Point p1 = line.a;
    Point p2 = line.b;

    double dx = p2.x() - p1.x();
    double dy = p2.y() - p1.y();

    double length = std::sqrt(dx * dx + dy * dy);

    double ux = dx / length;
    double uy = dy / length;

    double vx = -uy;
    double vy = ux;

    double ox = vx * offset;
    double oy = vy * offset;

    Points rect;
    rect.resize(4);
    rect[0] = {p1.x() + ox, p1.y() + oy};
    rect[1] = {p1.x() - ox, p1.y() - oy};
    rect[2] = {p2.x() - ox, p2.y() - oy};
    rect[3] = {p2.x() + ox, p2.y() + oy};
    Polygon poly(rect);
    return poly;
};

struct Segment
{
    Vec2f start;
    Vec2f end;
    bool  is_arc = false;
    ArcSegment arcsegment;
    Segment(const Vec2f &s, const Vec2f &e) : start(s), end(e) {}
    bool is_valid() const { return start.y() < end.y(); }
};

std::vector<Segment> remove_points_from_segment(const Segment &segment, const std::vector<Vec2f> &skip_points, double range)
{
    std::vector<Segment> result;
    result.push_back(segment);
    float x = segment.start.x();

    for (const Vec2f &point : skip_points) {
        std::vector<Segment> newResult;
        for (const auto &seg : result) {
            if (point.y() + range <= seg.start.y() || point.y() - range >= seg.end.y()) {
                newResult.push_back(seg);
            } else {
                if (point.y() - range > seg.start.y()) { newResult.push_back(Segment(Vec2f(x, seg.start.y()), Vec2f(x, point.y() - range))); }
                if (point.y() + range < seg.end.y()) { newResult.push_back(Segment(Vec2f(x, point.y() + range), Vec2f(x, seg.end.y()))); }
            }
        }

        result = newResult;
    }

    result.erase(std::remove_if(result.begin(), result.end(), [](const Segment &seg) { return !seg.is_valid(); }), result.end());
    return result;
}

struct IntersectionInfo
{
    Vec2f pos;
    int   idx;
    int   pair_idx; // gap_pair idx
    float dis_from_idx;
    bool  is_forward;
};

struct PointWithFlag
{
    Vec2f pos;
    int   pair_idx; // gap_pair idx
    bool  is_forward;
};
IntersectionInfo move_point_along_polygon(const std::vector<Vec2f> &points, const Vec2f &startPoint, int startIdx, float offset, bool forward, int pair_idx)
{
    float            remainingDistance = offset;
    IntersectionInfo res;
    int  mod = points.size();
    if (forward) {
        int next = (startIdx + 1) % mod;
        remainingDistance -= (points[next] - startPoint).norm();
        if (remainingDistance <= 0) {
            res.idx          = startIdx;
            res.pos          = startPoint + (points[next] - startPoint).normalized() * offset;
            res.pair_idx     = pair_idx;
            res.dis_from_idx = (points[startIdx] - res.pos).norm();
            return res;
        } else {
            for (int i = (startIdx + 1) % mod; i != startIdx; i = (i + 1) % mod) {
                float segmentLength = (points[(i + 1) % mod] - points[i]).norm();
                if (remainingDistance <= segmentLength) {
                    float ratio      = remainingDistance / segmentLength;
                    res.idx          = i;
                    res.pos          = points[i] + ratio * (points[(i + 1) % mod] - points[i]);
                    res.dis_from_idx = remainingDistance;
                    res.pair_idx     = pair_idx;
                    return res;
                }
                remainingDistance -= segmentLength;
            }
            res.idx          = (startIdx - 1 + mod) % mod;
            res.pos          = points[startIdx];
            res.pair_idx     = pair_idx;
            res.dis_from_idx = (res.pos - points[res.idx]).norm();
        }
    } else {
        int next = (startIdx + 1) % mod;
        remainingDistance -= (points[startIdx] - startPoint).norm();
        if (remainingDistance <= 0) {
            res.idx          = startIdx;
            res.pos          = startPoint - (points[next] - points[startIdx]).normalized() * offset;
            res.dis_from_idx = (res.pos - points[startIdx]).norm();
            res.pair_idx     = pair_idx;
            return res;
        }
        for (int i = (startIdx - 1 + mod) % mod; i != startIdx; i = (i - 1 + mod) % mod) {
            float segmentLength = (points[(i + 1) % mod] - points[i]).norm();
            if (remainingDistance <= segmentLength) {
                float ratio      = remainingDistance / segmentLength;
                res.idx          = i;
                res.pos          = points[(i + 1) % mod] - ratio * (points[(i + 1) % mod] - points[i]);
                res.dis_from_idx = segmentLength - remainingDistance;
                res.pair_idx     = pair_idx;
                return res;
            }
            remainingDistance -= segmentLength;
        }
        res.idx          = startIdx;
        res.pos          = points[res.idx];
        res.pair_idx     = pair_idx;
        res.dis_from_idx = 0;
    }
    return res;
};

void insert_points(std::vector<PointWithFlag> &pl, int idx, Vec2f pos, int pair_idx, bool is_forward)
{
    int   next = (idx + 1) % pl.size();
    Vec2f pos1 = pl[idx].pos;
    Vec2f pos2 = pl[next].pos;
    if ((pos - pos1).squaredNorm() < EPSILON) {
        pl[idx].pair_idx   = pair_idx;
        pl[idx].is_forward = is_forward;
    } else if ((pos - pos2).squaredNorm() < EPSILON) {
        pl[next].pair_idx   = pair_idx;
        pl[next].is_forward = is_forward;
    } else {
        pl.insert(pl.begin() + idx + 1, PointWithFlag{pos, pair_idx, is_forward});
    }
}

// For skip_point
// TODO: Optimize the skip_point algorithm itself instead of adding guards here
Polygon add_extra_point(const Polygon &polygon, int scale_range)
{
    Polygon res;
    if (polygon.size() < 2) return polygon;

    // Compute bounding box of the polygon
    auto polygon_box = get_extents(polygon);

    // Anchor point: X at bbox center, Y at bbox bottom
    Vec2f anchor_point(float(polygon_box.center()[0]), float(polygon_box.min[1]));

    // Find the edge whose midpoint is closest to the anchor point
    size_t closest_edge_idx = 0;
    float  min_dist_sq      = std::numeric_limits<float>::max();

    for (size_t i = 0; i < polygon.size(); ++i) {
        const Point &a_i = polygon[i];
        const Point &b_i = polygon[(i + 1) % polygon.size()];

        Vec2f a(float(a_i.x()), float(a_i.y()));
        Vec2f b(float(b_i.x()), float(b_i.y()));
        Vec2f mid = (a + b) * 0.5f;

        float dist_sq = (anchor_point - mid).squaredNorm();
        if (dist_sq < min_dist_sq) {
            min_dist_sq      = dist_sq;
            closest_edge_idx = i;
        }
    }

    // Edge endpoints (integer space)
    const Point &a_i = polygon[closest_edge_idx];
    const Point &b_i = polygon[(closest_edge_idx + 1) % polygon.size()];

    // Convert to float for geometric computation
    Vec2f a(float(a_i.x()), float(a_i.y()));
    Vec2f b(float(b_i.x()), float(b_i.y()));

    Vec2f mid = (a + b) * 0.5f;

    // Direction vectors from midpoint towards A and B
    Vec2f dir_to_a = a - mid;
    Vec2f dir_to_b = b - mid;

    float len_a = dir_to_a.norm();
    float len_b = dir_to_b.norm();

    // Guard against degenerated edges
    if (len_a < EPSILON || len_b < EPSILON) return polygon;

    dir_to_a /= len_a;
    dir_to_b /= len_b;

    // Clamp range to avoid overshooting the edge
    float max_range = std::min(len_a, len_b) * 0.9f;
    float range     = std::min(float(scale_range), max_range);

    // Offset points (float space)
    Vec2f offset_to_a_f = mid + dir_to_a * range;
    Vec2f offset_to_b_f = mid + dir_to_b * range;

    // Safe cast back to scaled integer Point
    auto to_int_point = [](const Vec2f &p) {
        auto clamp = [](float v) -> coord_t {
            constexpr float kMin = float(std::numeric_limits<coord_t>::min());
            constexpr float kMax = float(std::numeric_limits<coord_t>::max());
            v                    = std::clamp(v, kMin, kMax);
            return static_cast<coord_t>(std::lround(v));
        };
        return Point(clamp(p.x()), clamp(p.y()));
    };

    Point mid_i         = to_int_point(mid);
    Point offset_to_a_i = to_int_point(offset_to_a_f);
    Point offset_to_b_i = to_int_point(offset_to_b_f);

    // Rebuild polygon with inserted points
    for (size_t i = 0; i < polygon.size(); ++i) {
        res.points.push_back(polygon[i]);

        // Insert points right after the selected edge start vertex
        if (i == closest_edge_idx) {
            res.points.push_back(offset_to_a_i);
            res.points.push_back(mid_i);
            res.points.push_back(offset_to_b_i);
        }
    }

    return res;
}



Polylines remove_points_from_polygon(const Polygon &polygon_ori, const std::vector<Vec2f> &skip_points, double range, float wt_width, Polygon &insert_skip_pg)
{
    Polygon polygon = add_extra_point(polygon_ori, scale_(range));
    if (polygon.size() < 2) return Polylines{to_polyline(polygon)};
    Polylines                     result;
    std::vector<PointWithFlag>    new_pl; // add intersection points for gaps, where bool indicates whether it's a gap point.
    std::vector<IntersectionInfo> inter_info;
    auto                          polygon_box  = get_extents(polygon);
    //Point                         anchor_point = /*is_left ? Point{polygon_box.max[0], polygon_box.min[1]} :*/ polygon_box.min; // rd:ld
    Point              anchor_point = Point{polygon_box.center()[0], polygon_box.min[1]}; // for next reconnect
    std::vector<Vec2f>            points;
    {
        points.reserve(polygon.points.size());
        int idx = polygon.closest_point_index(anchor_point);
        Polyline tmp_poly = polygon.split_at_index(idx);
        for (auto &p : tmp_poly) points.push_back(unscale(p).cast<float>());
        points.pop_back();
    }

    for (int i = 0; i < skip_points.size(); i++) {
        bool  is_left = abs(skip_points[i].x()) < wt_width / 2.f;
        Vec2f ray = is_left ? Vec2f(-1, 0) : Vec2f(1, 0);
        for (int j = 0; j < points.size(); j++) {
            Vec2f& p1                   = points[j];
            Vec2f& p2                   = points[(j + 1) % points.size()];
            auto [is_inter, inter_pos] = ray_intersetion_line(skip_points[i], ray, p1, p2);
            if (is_inter) {
                IntersectionInfo forward  = move_point_along_polygon(points, inter_pos, j, range, true, i);
                IntersectionInfo backward = move_point_along_polygon(points, inter_pos, j, range, false, i);
                backward.is_forward       = false;
                forward.is_forward        = true;
                inter_info.push_back(backward);
                inter_info.push_back(forward);
                break;
            }
        }
    }

    // insert point to new_pl
    for (const auto &p : points) new_pl.push_back({p, -1});
    std::sort(inter_info.begin(), inter_info.end(), [](const IntersectionInfo &lhs, const IntersectionInfo &rhs) {
        if (rhs.idx == lhs.idx) return lhs.dis_from_idx < rhs.dis_from_idx;
        return lhs.idx < rhs.idx;
    });
    for (int i = inter_info.size() - 1; i >= 0; i--) { insert_points(new_pl, inter_info[i].idx, inter_info[i].pos, inter_info[i].pair_idx, inter_info[i].is_forward); }

    {
        //set insert_pg for wipe_path
        for (auto &p : new_pl) insert_skip_pg.points.push_back(scaled(p.pos));
    }

    int beg = 0;
    bool skip = true;
    int  i    = beg;
    Polyline pl;

    do {
        if (skip || new_pl[i].pair_idx == -1) {
            pl.points.push_back(scaled(new_pl[i].pos));
            i    = (i + 1) % new_pl.size();
            skip = false;
        } else {
            if (!pl.points.empty()) {
                pl.points.push_back(scaled(new_pl[i].pos));
                result.push_back(pl);
                pl.points.clear();
            }
            int left = new_pl[i].pair_idx;
            int j    = (i + 1) % new_pl.size();
            while (j != beg && new_pl[j].pair_idx != left) {
                if (new_pl[j].pair_idx != -1 && !new_pl[j].is_forward) left = new_pl[j].pair_idx;
                j = (j + 1) % new_pl.size();
            }
            i    = j;
            skip = true;
        }
    } while (i != beg);

    if (!pl.points.empty()) {
        if (new_pl[i].pair_idx==-1) pl.points.push_back(scaled(new_pl[i].pos));
        result.push_back(pl);
    }
    return result;
}

Polylines contrust_gap_for_skip_points(const Polygon &polygon, const std::vector<Vec2f> & skip_points ,float wt_width,float gap_length,Polygon& insert_skip_polygon)
{
    if (skip_points.empty()) {
        insert_skip_polygon = polygon;
        return Polylines{to_polyline(polygon)};
    }
    //bool is_left  = false;
    //const auto &pt      = skip_points.front();
    //if (abs(pt.x()) < wt_width/2.f) {
    //    is_left = true;
    //}
    return remove_points_from_polygon(polygon, skip_points, gap_length, wt_width, insert_skip_polygon);

};

Polygon generate_rectange_polygon(const Vec2f &wt_box_min ,const Vec2f & wt_box_max) {
    Polygon res;
    res.points.push_back(scaled(wt_box_min));
    res.points.push_back(scaled(Vec2f{wt_box_max[0], wt_box_min[1]}));
    res.points.push_back(scaled(wt_box_max));
    res.points.push_back(scaled(Vec2f{wt_box_min[0], wt_box_max[1]}));
    return res;
}

class WipeTowerWriter
{
public:
	WipeTowerWriter(float layer_height, float line_width, GCodeFlavor flavor, const std::vector<WipeTower::FilamentParameters>& filament_parameters, bool enable_arc_fitting) :
		m_current_pos(std::numeric_limits<float>::max(), std::numeric_limits<float>::max()),
		m_current_z(0.f),
		m_current_feedrate(0.f),
		m_layer_height(layer_height),
        m_line_width(line_width),
		m_extrusion_flow(0.f),
		m_preview_suppressed(false),
		m_elapsed_time(0.f),
#if ENABLE_GCODE_VIEWER_DATA_CHECKING
        m_default_analyzer_line_width(line_width),
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING
        m_gcode_flavor(flavor),
        m_enable_arc_fitting(enable_arc_fitting),
        m_filpar(filament_parameters)
        {
            // adds tag for analyzer:
            std::ostringstream str;
            str << ";" << GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Height) << std::to_string(m_layer_height) << "\n"; // don't rely on GCodeAnalyzer knowing the layer height - it knows nothing at priming
            str << ";" << GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Role) << ExtrusionEntity::role_to_string(erWipeTower) << "\n";
            m_gcode += str.str();
            change_analyzer_line_width(line_width);
    }

    WipeTowerWriter& change_analyzer_line_width(float line_width) {
        // adds tag for analyzer:
        std::stringstream str;
        str << ";" << GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Width) << std::to_string(line_width) << "\n";
        m_gcode += str.str();
        return *this;
    }

    WipeTowerWriter& change_analyzer_layer_height(float layer_height) {
        // adds tag for analyzer:
        std::stringstream str;
        str << ";" << GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Height) << std::to_string(layer_height) << "\n";
        m_gcode += str.str();
        return *this;
    }

#if ENABLE_GCODE_VIEWER_DATA_CHECKING
    WipeTowerWriter& change_analyzer_mm3_per_mm(float len, float e) {
        float area = m_filpar[m_current_tool].filament_area;
        float mm3_per_mm = (len == 0.f ? 0.f : area * e / len);
        // adds tag for processor:
        std::stringstream str;
        str << ";" << GCodeProcessor::Mm3_Per_Mm_Tag << mm3_per_mm << "\n";
        m_gcode += str.str();
        return *this;
    }
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING

	WipeTowerWriter& 			 set_initial_position(const Vec2f &pos, float width = 0.f, float depth = 0.f, float internal_angle = 0.f) {
        m_wipe_tower_width = width;
        m_wipe_tower_depth = depth;
        m_internal_angle = internal_angle;
		m_start_pos = this->rotate(pos);
		m_current_pos = pos;
		return *this;
	}

    WipeTowerWriter&				 set_initial_tool(size_t tool) { m_current_tool = tool; return *this; }

	WipeTowerWriter&				 set_z(float z)
		{ m_current_z = z; return *this; }

	WipeTowerWriter& 			 set_extrusion_flow(float flow)
		{ m_extrusion_flow = flow; return *this; }

	WipeTowerWriter&				 set_line_width(float line_width)
		{ m_line_width = line_width; return *this; }

	WipeTowerWriter&				 set_y_shift(float shift) {
        m_current_pos.y() -= shift-m_y_shift;
        m_y_shift = shift;
        return (*this);
    }

	// Offset this writer adds to every Y it emits, needed to convert coordinates that were derived from
	// the tower footprint frame (WipeTower::m_outer_wall) back into the frame travel() expects.
	float                            y_shift() const { return m_y_shift; }

    WipeTowerWriter&            disable_linear_advance() {
        if (m_gcode_flavor == gcfKlipper)
            m_gcode += "SET_PRESSURE_ADVANCE ADVANCE=0\n";
        else if (m_gcode_flavor == gcfRepRapFirmware)
            m_gcode += std::string("M572 D") + std::to_string(m_current_tool) + " S0\n";
        else
            m_gcode += "M900 K0\n";

        return *this;
    }

	// Suppress / resume G-code preview in Slic3r. Slic3r will have difficulty to differentiate the various
	// filament loading and cooling moves from normal extrusion moves. Therefore the writer
	// is asked to suppres output of some lines, which look like extrusions.
#if ENABLE_GCODE_VIEWER_DATA_CHECKING
    WipeTowerWriter& suppress_preview() { change_analyzer_line_width(0.f); m_preview_suppressed = true; return *this; }
    WipeTowerWriter& resume_preview() { change_analyzer_line_width(m_default_analyzer_line_width); m_preview_suppressed = false; return *this; }
#else
    WipeTowerWriter& 			 suppress_preview() { m_preview_suppressed = true; return *this; }
	WipeTowerWriter& 			 resume_preview()   { m_preview_suppressed = false; return *this; }
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING

	WipeTowerWriter& 			 feedrate(float f)
	{
        if (f != m_current_feedrate) {
			m_gcode += "G1" + set_format_F(f) + "\n";
            m_current_feedrate = f;
        }
		return *this;
	}

	const std::string&   gcode() const { return m_gcode; }
	const std::vector<WipeTower::Extrusion>& extrusions() const { return m_extrusions; }
	float                x()     const { return m_current_pos.x(); }
	float                y()     const { return m_current_pos.y(); }
	const Vec2f& 		 pos()   const { return m_current_pos; }
	const Vec2f	 		 start_pos_rotated() const { return m_start_pos; }
	const Vec2f  		 pos_rotated() const { return this->rotate(m_current_pos); }
	// Rotate an arbitrary wipe-tower-local point into the same frame as start_pos_rotated().
	const Vec2f  		 point_rotated(const Vec2f &pt) const { return this->rotate(pt); }
	float 				 elapsed_time() const { return m_elapsed_time; }
    float                get_and_reset_used_filament_length() { float temp = m_used_filament_length; m_used_filament_length = 0.f; return temp; }

	// Extrude with an explicitely provided amount of extrusion.
    WipeTowerWriter &extrude_explicit(float x, float y, float e, float f = 0.f, bool record_length = false ,LimitFlow limit_flow = LimitFlow::LimitPrintFlow)
	{
        if ((std::abs(x - m_current_pos.x()) <= (float)EPSILON) && (std::abs(y - m_current_pos.y()) < (float)EPSILON) && e == 0.f && (f == 0.f || f == m_current_feedrate))
			// Neither extrusion nor a travel move.
			return *this;

		float dx = x - m_current_pos.x();
		float dy = y - m_current_pos.y();
        float len = std::sqrt(dx*dx+dy*dy);
        if (record_length)
            m_used_filament_length += e;

		// Now do the "internal rotation" with respect to the wipe tower center
		Vec2f rotated_current_pos(this->pos_rotated());
		Vec2f rot(this->rotate(Vec2f(x,y)));                               // this is where we want to go

        if (! m_preview_suppressed && e > 0.f && len > 0.f) {
#if ENABLE_GCODE_VIEWER_DATA_CHECKING
            change_analyzer_mm3_per_mm(len, e);
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING
            // Width of a squished extrusion, corrected for the roundings of the squished extrusions.
			// This is left zero if it is a travel move.
            float width = e * m_filpar[m_current_tool].filament_area / (len * m_layer_height);
			// Correct for the roundings of a squished extrusion.
			width += m_layer_height * float(1. - M_PI / 4.);
			if (m_extrusions.empty() || m_extrusions.back().pos != rotated_current_pos)
				m_extrusions.emplace_back(WipeTower::Extrusion(rotated_current_pos, 0, m_current_tool));
			m_extrusions.emplace_back(WipeTower::Extrusion(rot, width, m_current_tool));
		}

        if (e == 0.f) {
            m_gcode += set_travel_acceleration();
        } else {
            m_gcode += set_normal_acceleration();
        }

		m_gcode += "G1";
        if (std::abs(rot.x() - rotated_current_pos.x()) > (float)EPSILON)
			m_gcode += set_format_X(rot.x());

        if (std::abs(rot.y() - rotated_current_pos.y()) > (float)EPSILON)
			m_gcode += set_format_Y(rot.y());


		if (e != 0.f)
			m_gcode += set_format_E(e);

		if (f != 0.f && f != m_current_feedrate) {
            if (limit_flow!= LimitFlow::None) {
                float e_speed = e / (((len == 0.f) ? std::abs(e) : len) / f * 60.f);
                float tmp     = m_filpar[m_current_tool].max_e_speed;
                if (limit_flow == LimitFlow::LimitRammingFlow) tmp = m_filpar[m_current_tool].max_e_ramming_speed.first;
                else if (limit_flow == LimitFlow::LimitRammingFlowNC) tmp = m_filpar[m_current_tool].max_e_ramming_speed.second;
                f /= std::max(1.f, e_speed / tmp);
            }
			m_gcode += set_format_F(f);
        }

        m_current_pos.x() = x;
        m_current_pos.y() = y;

		// Update the elapsed time with a rough estimate.
        m_elapsed_time += ((len == 0.f) ? std::abs(e) : len) / m_current_feedrate * 60.f;
		m_gcode += "\n";
		return *this;
	}

    	// Extrude with an explicitely provided amount of extrusion.
    WipeTowerWriter &extrude_arc_explicit(ArcSegment &arc, float f = 0.f, bool record_length = false, LimitFlow limit_flow = LimitFlow::LimitPrintFlow)
    {
        float x   = (float)unscale(arc.end_point).x();
        float y   = (float)unscale(arc.end_point).y();
        float len = unscaled<float>(arc.length);
        float e   = len * m_extrusion_flow;
        if (len < (float) EPSILON && e == 0.f && (f == 0.f || f == m_current_feedrate))
            // Neither extrusion nor a travel move.
            return *this;
        if (record_length) m_used_filament_length += e;

        // Now do the "internal rotation" with respect to the wipe tower center
        Vec2f rotated_current_pos(this->pos_rotated());
        Vec2f rot(this->rotate(Vec2f(x, y))); // this is where we want to go

        if (!m_preview_suppressed && e > 0.f && len > 0.f) {
#if ENABLE_GCODE_VIEWER_DATA_CHECKING
            change_analyzer_mm3_per_mm(len, e);
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING
       // Width of a squished extrusion, corrected for the roundings of the squished extrusions.
       // This is left zero if it is a travel move.
            float width = e * m_filpar[m_current_tool].filament_area / (len * m_layer_height);
            // Correct for the roundings of a squished extrusion.
            width += m_layer_height * float(1. - M_PI / 4.);
            if (m_extrusions.empty() || m_extrusions.back().pos != rotated_current_pos) m_extrusions.emplace_back(WipeTower::Extrusion(rotated_current_pos, 0, m_current_tool));
            {
                int   n            = arc_fit_size;
                for (int j = 0; j < n; j++) {
                    float cur_angle = arc.polar_start_theta + (float) j / n * arc.angle_radians;
                    if (cur_angle > 2 * PI)
                        cur_angle -= 2 * PI;
                    else if (cur_angle < 0)
                        cur_angle += 2 * PI;
                    Point tmp = arc.center + Point{arc.radius * std::cos(cur_angle), arc.radius * std::sin(cur_angle)};
                    m_extrusions.emplace_back(WipeTower::Extrusion(this->rotate(unscaled<float>(tmp)), width, m_current_tool));
                }
                m_extrusions.emplace_back(WipeTower::Extrusion(rot, width, m_current_tool));
            }

        }

        if (e == 0.f) {
            m_gcode += set_travel_acceleration();
        } else {
            m_gcode += set_normal_acceleration();
        }

        m_gcode += arc.direction == ArcDirection::Arc_Dir_CCW ? "G3" : "G2";
        const Vec2f center_offset = this->rotate(unscaled<float>(arc.center)) - rotated_current_pos;
        m_gcode += set_format_X(rot.x());
        m_gcode += set_format_Y(rot.y());
        m_gcode += set_format_I(center_offset.x());
        m_gcode += set_format_J(center_offset.y());

        if (e != 0.f) m_gcode += set_format_E(e);

        if (f != 0.f && f != m_current_feedrate) {
            if (limit_flow != LimitFlow::None) {
                float e_speed = e / (((len == 0.f) ? std::abs(e) : len) / f * 60.f);
                float tmp     = m_filpar[m_current_tool].max_e_speed;
                if (limit_flow == LimitFlow::LimitRammingFlow) tmp = m_filpar[m_current_tool].max_e_ramming_speed.first;
                else if (limit_flow == LimitFlow::LimitRammingFlowNC)
                    tmp = m_filpar[m_current_tool].max_e_ramming_speed.second;
                f /= std::max(1.f, e_speed / tmp);
            }
            m_gcode += set_format_F(f);
        }

        m_current_pos.x() = x;
        m_current_pos.y() = y;

        // Update the elapsed time with a rough estimate.
        m_elapsed_time += ((len == 0.f) ? std::abs(e) : len) / m_current_feedrate * 60.f;
        m_gcode += "\n";
        return *this;
    }

	WipeTowerWriter &extrude_explicit(const Vec2f &dest, float e, float f = 0.f, bool record_length = false, LimitFlow limit_flow = LimitFlow::LimitPrintFlow)
    {
        return extrude_explicit(dest.x(), dest.y(), e, f, record_length, limit_flow);
    }

	// Travel to a new XY position. f=0 means use the current value.
	WipeTowerWriter& travel(float x, float y, float f = 0.f)
		{ return extrude_explicit(x, y, 0.f, f); }

	WipeTowerWriter& travel(const Vec2f &dest, float f = 0.f)
		{ return extrude_explicit(dest.x(), dest.y(), 0.f, f); }

	// Combined travel move to an XY position (in wipe tower local coordinates) while simultaneously
	// moving to an absolute Z. Used for the diagonal side scrape. The Z value is emitted verbatim so the
	// caller can hand over a placeholder instead of a number, which is what the side scrape does: the Z
	// the tower layer is really printed at is only known when the gcode is exported.
	// This is a pure travel move (no extrusion) and does not change m_current_z.
	WipeTowerWriter& travel_xyz(const Vec2f &dest, const std::string &z, float f = 0.f)
	{
		Vec2f rotated_current_pos(this->pos_rotated());
		Vec2f rot(this->rotate(dest));

		m_gcode += set_travel_acceleration();
		m_gcode += "G1";
		if (std::abs(rot.x() - rotated_current_pos.x()) > (float) EPSILON)
			m_gcode += set_format_X(rot.x());
		if (std::abs(rot.y() - rotated_current_pos.y()) > (float) EPSILON)
			m_gcode += set_format_Y(rot.y());
		m_gcode += " Z" + z;
		if (f != 0.f && f != m_current_feedrate)
			m_gcode += set_format_F(f);

		m_current_pos.x() = dest.x();
		m_current_pos.y() = dest.y();

		float dx  = rot.x() - rotated_current_pos.x();
		float dy  = rot.y() - rotated_current_pos.y();
		float len = std::sqrt(dx * dx + dy * dy);
		if (m_current_feedrate > 0.f)
			m_elapsed_time += len / m_current_feedrate * 60.f;
		m_gcode += "\n";
		return *this;
	}

	// Extrude a line from current position to x, y with the extrusion amount given by m_extrusion_flow.
    WipeTowerWriter &extrude(float x, float y, float f = 0.f, LimitFlow limit_flow = LimitFlow::LimitPrintFlow)
	{
		float dx = x - m_current_pos.x();
		float dy = y - m_current_pos.y();
        return extrude_explicit(x, y, std::sqrt(dx * dx + dy * dy) * m_extrusion_flow, f, false, limit_flow);
	}
    WipeTowerWriter &extrude_arc(ArcSegment &arc, float f = 0.f, LimitFlow limit_flow = LimitFlow::LimitPrintFlow)
    {
        return extrude_arc_explicit(arc, f, false , limit_flow);
    }

	WipeTowerWriter& extrude(const Vec2f &dest, const float f = 0.f)
		{ return extrude(dest.x(), dest.y(), f); }

    WipeTowerWriter& rectangle(const Vec2f& ld,float width,float height,const float f = 0.f)
    {
        Vec2f corners[4];
        corners[0] = ld;
        corners[1] = ld + Vec2f(width,0.f);
        corners[2] = ld + Vec2f(width,height);
        corners[3] = ld + Vec2f(0.f,height);
        int index_of_closest = 0;
        if (x()-ld.x() > ld.x()+width-x())    // closer to the right
            index_of_closest = 1;
        if (y()-ld.y() > ld.y()+height-y())   // closer to the top
            index_of_closest = (index_of_closest==0 ? 3 : 2);

        travel(corners[index_of_closest].x(), y());      // travel to the closest corner
        travel(x(),corners[index_of_closest].y());

        int i = index_of_closest;
        do {
            ++i;
            if (i==4) i=0;
            extrude(corners[i], f);
        } while (i != index_of_closest);
        return (*this);
    }

    WipeTowerWriter &rectangle_fill_box(const WipeTower* wipe_tower, const Vec2f &ld, float width, float height, const float f = 0.f)
    {
        bool need_change_flow = wipe_tower->need_thick_bridge_flow(ld.y(), m_layer_height);
        const float saved_flow = m_extrusion_flow;

        Vec2f corners[4];
        corners[0]           = ld;
        corners[1]           = ld + Vec2f(width, 0.f);
        corners[2]           = ld + Vec2f(width, height);
        corners[3]           = ld + Vec2f(0.f, height);
        int index_of_closest = 0;
        if (x() - ld.x() > ld.x() + width - x()) // closer to the right
            index_of_closest = 1;
        if (y() - ld.y() > ld.y() + height - y()) // closer to the top
            index_of_closest = (index_of_closest == 0 ? 3 : 2);

        travel(corners[index_of_closest].x(), y()); // travel to the closest corner
        travel(x(), corners[index_of_closest].y());

        int i = index_of_closest;
        bool flow_changed = false;
        do {
            ++i;
            if (i == 4) i = 0;
            if (need_change_flow) {
                if (i == 1) {
                    // using bridge flow in bridge area, and add notes for gcode-check when flow changed
                    set_extrusion_flow(wipe_tower->extrusion_flow_for(0.2f, m_line_width));
                    append(";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Height) + std::to_string(0.2) + "\n");
                    flow_changed = true;
                } else if (i == 2 && flow_changed) {
                    set_extrusion_flow(saved_flow);
                    append(";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Height) + std::to_string(m_layer_height) + "\n");
                }
            }
            extrude(corners[i], f);
        } while (i != index_of_closest);
        return (*this);
    }
    WipeTowerWriter &line(const WipeTower *wipe_tower, Vec2f p0, Vec2f p1,const float f = 0.f)
    {
        bool need_change_flow = wipe_tower->need_thick_bridge_flow(p0.y(), m_layer_height);
        const float saved_flow = m_extrusion_flow;
        if (need_change_flow) {
            set_extrusion_flow(wipe_tower->extrusion_flow_for(0.2f, m_line_width));
            append(";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Height) + std::to_string(0.2) + "\n");
        }
        if (abs(x() - p0.x()) > abs(x() - p1.x())) std::swap(p0, p1);
        travel(p0.x(), y());
        travel(x(), p0.y());
        extrude(p1, f);
        if (need_change_flow) {
            set_extrusion_flow(saved_flow);
            append(";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Height) + std::to_string(m_layer_height) + "\n");
        }
        return (*this);
    }

    WipeTowerWriter &rectangle_fill_box(const WipeTower *wipe_tower, const WipeTower::box_coordinates &fill_box, std::vector<Vec2f> &finish_rect_wipe_path, const float f = 0.f)
    {
        float width  = fill_box.rd.x() - fill_box.ld.x();
        float height = fill_box.ru.y() - fill_box.rd.y();
        // Two opposite sides of the box closer than the line this writer extrudes with would overlap, so such
        // a box is drawn as a single line instead. Judged by that line width and not by the tower-wide
        // m_perimeter_width, which follows the smallest nozzle.
        if (height > m_line_width - wipe_tower->WT_EPSILON) {
            rectangle_fill_box(wipe_tower, fill_box.ld, width, height, f);
            Vec2f target = (pos() == fill_box.ld ? fill_box.rd : (pos() == fill_box.rd ? fill_box.ru : (pos() == fill_box.ru ? fill_box.lu : fill_box.ld)));
            finish_rect_wipe_path.emplace_back(pos());
            finish_rect_wipe_path.emplace_back(target);
        } else if (height > wipe_tower->WT_EPSILON) {
            line(wipe_tower, fill_box.ld, fill_box.rd);
            Vec2f target = (pos() == fill_box.ld ? fill_box.rd : fill_box.ld);
            finish_rect_wipe_path.emplace_back(pos());
            finish_rect_wipe_path.emplace_back(target);
        }
        return (*this);
    }
    WipeTowerWriter& rectangle(const WipeTower::box_coordinates& box, const float f = 0.f)
    {
        rectangle(Vec2f(box.ld.x(), box.ld.y()),
                  box.ru.x() - box.lu.x(),
                  box.ru.y() - box.rd.y(), f);
        return (*this);
    }
    WipeTowerWriter &polygon(const Polygon &wall_polygon, const float f = 0.f)
    {
        Polyline    pl = to_polyline(wall_polygon);
        pl.simplify(WT_SIMPLIFY_TOLERANCE_SCALED);
        if (m_enable_arc_fitting) {
            pl.simplify_by_fitting_arc(SCALED_WIPE_TOWER_RESOLUTION);
        } else {
            pl.simplify(SCALED_WIPE_TOWER_RESOLUTION);
            pl.reset_to_linear_move();
        }

        auto get_closet_idx = [this](std::vector<Segment> &corners) -> int {
            Vec2f anchor{this->m_current_pos.x(), this->m_current_pos.y()};
            int   closestIndex = -1;
            float minDistance  = std::numeric_limits<float>::max();
            for (int i = 0; i < corners.size(); ++i) {
                float distance = (corners[i].start - anchor).squaredNorm();
                if (distance < minDistance) {
                    minDistance  = distance;
                    closestIndex = i;
                }
            }
            return closestIndex;
        };
        std::vector<Segment> segments;
        for (int i = 0; i < pl.fitting_result.size(); i++) {
            if (pl.fitting_result[i].path_type == EMovePathType::Linear_move) {
                for (int j = pl.fitting_result[i].start_point_index; j < pl.fitting_result[i].end_point_index; j++)
                    segments.push_back({unscaled<float>(pl.points[j]), unscaled<float>(pl.points[j + 1])});
            } else {
                int beg = pl.fitting_result[i].start_point_index;
                int end = pl.fitting_result[i].end_point_index;
                segments.push_back({unscaled<float>(pl.points[beg]), unscaled<float>(pl.points[end])});
                segments.back().is_arc     = true;
                segments.back().arcsegment = pl.fitting_result[i].arc_data;
            }
        }

        if (segments.empty())
            return (*this);

        int index_of_closest = get_closet_idx(segments);
        int i                = index_of_closest;
        travel(segments[i].start); // travel to the closest points
        segments[i].is_arc ? extrude_arc(segments[i].arcsegment, f) : extrude(segments[i].end, f);
        do {
            i = (i + 1) % segments.size();
            if (i == index_of_closest) break;
            segments[i].is_arc ? extrude_arc(segments[i].arcsegment, f) : extrude(segments[i].end, f);
        } while (1);
        return (*this);
    }

	WipeTowerWriter& load(float e, float f = 0.f)
	{
		if (e == 0.f && (f == 0.f || f == m_current_feedrate))
			return *this;
		m_gcode += "G1";
		if (e != 0.f)
			m_gcode += set_format_E(e);
		if (f != 0.f && f != m_current_feedrate)
			m_gcode += set_format_F(f);
		m_gcode += "\n";
		return *this;
	}

	WipeTowerWriter& retract(float e, float f = 0.f)
		{ return load(-e, f); }

// Loads filament while also moving towards given points in x-axis (x feedrate is limited by cutting the distance short if necessary)
    WipeTowerWriter& load_move_x_advanced(float farthest_x, float loading_dist, float loading_speed, float max_x_speed = 50.f)
    {
        float time = std::abs(loading_dist / loading_speed); // time that the move must take
        float x_distance = std::abs(farthest_x - x());       // max x-distance that we can travel
        float x_speed = x_distance / time;                   // x-speed to do it in that time

        if (x_speed > max_x_speed) {
            // Necessary x_speed is too high - we must shorten the distance to achieve max_x_speed and still respect the time.
            x_distance = max_x_speed * time;
            x_speed = max_x_speed;
        }

        float end_point = x() + (farthest_x > x() ? 1.f : -1.f) * x_distance;
        return extrude_explicit(end_point, y(), loading_dist, x_speed * 60.f, false, LimitFlow::None);
    }

	// Elevate the extruder head above the current print_z position.
	WipeTowerWriter& z_hop(float hop, float f = 0.f)
	{
		m_gcode += std::string("G1") + set_format_Z(m_current_z + hop);
		if (f != 0 && f != m_current_feedrate)
			m_gcode += set_format_F(f);
		m_gcode += "\n";
		return *this;
	}

	// Lower the extruder head back to the current print_z position.
	WipeTowerWriter& z_hop_reset(float f = 0.f)
		{ return z_hop(0, f); }

	// Move to x1, +y_increment,
	// extrude quickly amount e to x2 with feed f.
	WipeTowerWriter& ram(float x1, float x2, float dy, float e0, float e, float f)
	{
        extrude_explicit(x1, m_current_pos.y() + dy, e0, f, true, LimitFlow::None);
        extrude_explicit(x2, m_current_pos.y(), e, 0.f, true, LimitFlow::None);
		return *this;
	}

	// Let the end of the pulled out filament cool down in the cooling tube
	// by moving up and down and moving the print head left / right
	// at the current Y position to spread the leaking material.
	WipeTowerWriter& cool(float x1, float x2, float e1, float e2, float f)
	{
		extrude_explicit(x1, m_current_pos.y(), e1, f, false, LimitFlow::None);
        extrude_explicit(x2, m_current_pos.y(), e2, 0.f, false, LimitFlow::None);
		return *this;
	}

    WipeTowerWriter& set_tool(size_t tool)
	{
		m_current_tool = tool;
		return *this;
	}

	// Set extruder temperature, don't wait by default.
	WipeTowerWriter& set_extruder_temp(int temperature, bool wait = false)
	{
        m_gcode += "M" + std::to_string(wait ? 109 : 104) + " S" + std::to_string(temperature) + "\n";
        return *this;
    }

    // Wait for a period of time (seconds).
	WipeTowerWriter& wait(float time)
	{
        if (time==0.f)
            return *this;
        m_gcode += "G4 S" + Slic3r::float_to_string_decimal_point(time, 3) + "\n";
		return *this;
    }

	// Set speed factor override percentage.
	WipeTowerWriter& speed_override(int speed)
	{
        m_gcode += "M220 S" + std::to_string(speed) + "\n";
		return *this;
    }

    // Let the firmware back up the active speed override value.
    WipeTowerWriter& speed_override_backup()
    {
        // BBS: BBL machine don't support speed backup
        if (m_gcode_flavor == gcfMarlinLegacy || m_gcode_flavor == gcfMarlinFirmware)
            m_gcode += "M220 B\n";
        return *this;
    }

    // Let the firmware restore the active speed override value.
    WipeTowerWriter& speed_override_restore()
    {
        // BBS: BBL machine don't support speed restore
        if (m_gcode_flavor == gcfMarlinLegacy || m_gcode_flavor == gcfMarlinFirmware)
            m_gcode += "M220 R\n";
        return *this;
    }

	// Set digital trimpot motor
	WipeTowerWriter& set_extruder_trimpot(int current)
	{
        // BBS: don't control trimpot
#if 0
        if (m_gcode_flavor == gcfRepRapSprinter || m_gcode_flavor == gcfRepRapFirmware)
            m_gcode += "M906 E";
        else
            m_gcode += "M907 E";
        m_gcode += std::to_string(current) + "\n";
#endif
		return *this;
    }

	WipeTowerWriter& flush_planner_queue()
	{
		m_gcode += "G4 S0\n";
		return *this;
	}

	// Reset internal extruder counter.
	WipeTowerWriter& reset_extruder()
	{
		m_gcode += "G92 E0\n";
		return *this;
	}

	WipeTowerWriter& comment_with_value(const char *comment, int value)
    {
        m_gcode += std::string(";") + comment + std::to_string(value) + "\n";
		return *this;
    }


    WipeTowerWriter& set_fan(unsigned speed)
	{
		if (speed == m_last_fan_speed)
			return *this;
		if (speed == 0)
			m_gcode += "M107\n";
        else
            m_gcode += "M106 S" + std::to_string(unsigned(255.0 * speed / 100.0)) + "\n";
		m_last_fan_speed = speed;
		return *this;
	}

	WipeTowerWriter& append(const std::string& text) { m_gcode += text; return *this; }

    const std::vector<Vec2f>& wipe_path() const
    {
        return m_wipe_path;
    }

    WipeTowerWriter& add_wipe_point(const Vec2f& pt)
    {
        m_wipe_path.push_back(rotate(pt));
        return *this;
    }

    WipeTowerWriter& add_wipe_point(float x, float y)
    {
        return add_wipe_point(Vec2f(x, y));
    }

    WipeTowerWriter &add_wipe_path(const Polygon & polygon,double wipe_dist)
    {
        int closest_idx = polygon.closest_point_index(scaled(m_current_pos));
        Polyline wipe_path   = polygon.split_at_index(closest_idx);
        wipe_path.reverse();
        for (int i = 0; i < wipe_path.size(); ++i) {
            if (wipe_dist < EPSILON) break;
            add_wipe_point(unscaled<float>(wipe_path[i]));
            if (i != 0) wipe_dist -= (unscaled(wipe_path[i]) - unscaled(wipe_path[i - 1])).norm();
        }
        return *this;
    }
    void generate_path(Polylines &pls, float feedrate, float retract_length, float retract_speed, bool used_fillet)
    {
        auto get_closet_idx = [this](std::vector<Segment> &corners) -> int {
            Vec2f anchor{this->m_current_pos.x(), this->m_current_pos.y()};
            int   closestIndex = -1;
            float minDistance  = std::numeric_limits<float>::max();
            for (int i = 0; i < corners.size(); ++i) {
                float distance = (corners[i].start - anchor).squaredNorm();
                if (distance < minDistance) {
                    minDistance  = distance;
                    closestIndex = i;
                }
            }
            return closestIndex;
        };
        if (m_enable_arc_fitting) {
            for (auto &pl : pls) pl.simplify_by_fitting_arc(SCALED_WIPE_TOWER_RESOLUTION);
        } else {
            for (auto &pl : pls) {
                pl.simplify(SCALED_WIPE_TOWER_RESOLUTION);
                pl.reset_to_linear_move();
            }
        }

        std::vector<Segment> segments;
        for (const auto &pl : pls) {
            if (pl.points.size()<2) continue;
            for (int i = 0; i < pl.fitting_result.size(); i++) {
                if (pl.fitting_result[i].path_type == EMovePathType::Linear_move) {
                    for (int j = pl.fitting_result[i].start_point_index; j < pl.fitting_result[i].end_point_index; j++)
                        segments.push_back({unscaled<float>(pl.points[j]), unscaled<float>(pl.points[j + 1])});
                } else {
                    int beg = pl.fitting_result[i].start_point_index;
                    int end = pl.fitting_result[i].end_point_index;
                    segments.push_back({unscaled<float>(pl.points[beg]), unscaled<float>(pl.points[end])});
                    segments.back().is_arc = true;
                    segments.back().arcsegment = pl.fitting_result[i].arc_data;
                }
            }
        }
        if (segments.empty())
            return;

        int index_of_closest = get_closet_idx(segments);
        int i = index_of_closest;
        travel(segments[i].start); // travel to the closest points
        segments[i].is_arc? extrude_arc(segments[i].arcsegment,feedrate) : extrude(segments[i].end, feedrate);
        do {
            i         = (i + 1) % segments.size();
            if (i == index_of_closest) break;
            float dx  = segments[i].start.x() - m_current_pos.x();
            float dy  = segments[i].start.y() - m_current_pos.y();
            float len = std::sqrt(dx * dx + dy * dy);
            if (len > EPSILON) {
                retract(retract_length, retract_speed);
                travel(segments[i].start, 600.);
                retract(-retract_length, retract_speed);
            }
            segments[i].is_arc ? extrude_arc(segments[i].arcsegment, feedrate) : extrude(segments[i].end, feedrate);
        } while (1);
    }
    void spiral_flat_ironing(const Vec2f &center, float area, float step_length, float feedrate)
    {
        float edge_length = std::sqrt(area);
        Vec2f box_max     = center + Vec2f{step_length, step_length};
        Vec2f box_min     = center - Vec2f{step_length, step_length};
        int   n           = std::ceil(edge_length / step_length / 2.f);
        if (n <= 0) return;
        while (n--) {
            travel(box_max.x(), m_current_pos.y(), feedrate);
            travel(m_current_pos.x(), box_max.y(), feedrate);
            travel(box_min.x(), m_current_pos.y(), feedrate);
            travel(m_current_pos.x(), box_min.y(), feedrate);

            box_max += Vec2f{step_length, step_length};
            box_min -= Vec2f{step_length, step_length};
        }
    }

    WipeTowerWriter &format_line_M104(int target_temp, int tool, bool is_heating, bool wait_for_moves = true, const std::string &comment = std::string())
    {
        std::string buffer;
        // Only wait before cooling: G1 is non-blocking, so M104 cool-down must not start mid-travel.
        // Heating can start early and does not need M400.
        if (wait_for_moves && !is_heating)
            buffer += "M400\n";
        buffer += "M104";
        if (!m_handle_hotend_as_extruder) {
            int mapped_extruder = m_multi_nozzle_group_result->get_extruder_id(tool, m_layer_id);
            if(mapped_extruder<0||mapped_extruder>=m_physical_extruder_map.size()) return *this;
            buffer += (" T" + std::to_string(m_physical_extruder_map[mapped_extruder]));
        } else {
            buffer += (" I" + std::to_string(tool));
        }
        buffer += " S" + std::to_string(target_temp) + " N0"; // N0 means the gcode is generated by slicer
        buffer += is_heating ? " W" : " C"; // mark heating (W) or cooling (C), aligned with GCodeProcessor M104
        if (!comment.empty()) buffer += " ;" + comment;
        buffer += '\n';
        append(buffer);
        return *this;
    }

    WipeTowerWriter &format_line_M109(int target_temp,int tool,const std::string &comment = std::string())
    {
        std::string buffer = "M109";
        if (!m_multi_nozzle_group_result)
            return *this;
        if (!m_handle_hotend_as_extruder) {
            int mapped_extruder = m_multi_nozzle_group_result->get_extruder_id(tool, m_layer_id);
            if(mapped_extruder<0||mapped_extruder>=m_physical_extruder_map.size()) return *this;
            buffer += (" T" + std::to_string(m_physical_extruder_map[mapped_extruder]));
        } else {
            buffer += (" I" + std::to_string(tool));
        }
        buffer += " S" + std::to_string(target_temp) + " N0"; // N0 means the gcode is generated by slicer
        if (!comment.empty()) buffer += " ;" + comment;
        buffer += '\n';
        append(buffer);
        return *this;
    };

    void set_first_layer(bool is_first_layer) { m_is_first_layer = is_first_layer; }
    void set_normal_acceleration(const std::vector<unsigned int> &accelerations) { m_normal_accelerations = accelerations; };
    void set_first_layer_normal_acceleration(const std::vector<unsigned int> &accelerations) { m_first_layer_normal_accelerations = accelerations; };
    void set_travel_acceleration(const std::vector<unsigned int> &accelerations) { m_travel_accelerations = accelerations; };
    void set_first_layer_travel_acceleration(const std::vector<unsigned int> &accelerations) { m_first_layer_travel_accelerations = accelerations; };
    void set_max_acceleration(unsigned int acceleration) { m_max_acceleration = acceleration; };
    void set_accel_to_decel_enable(bool enable) { m_accel_to_decel_enable = enable; }
    void set_accel_to_decel_factor(float factor) { m_accel_to_decel_factor = factor; }
    void set_layer_id(int layer_id) { m_layer_id = layer_id; }
    void set_handle_hotend_as_extruder(bool handle_hotend_as_extruder) { m_handle_hotend_as_extruder = handle_hotend_as_extruder; }
    void set_multi_nozzle_group_result(const MultiNozzleUtils::LayeredNozzleGroupResult *multi_nozzle_group_result) { m_multi_nozzle_group_result = multi_nozzle_group_result; }
    void set_physical_extruder_map(const std::vector<int> &physical_extruder_map) { m_physical_extruder_map = physical_extruder_map; }

private:
    std::string set_normal_acceleration() {
        std::vector<unsigned int> accelerations = m_is_first_layer ? m_first_layer_normal_accelerations : m_normal_accelerations;
        if (accelerations.empty() || !m_multi_nozzle_group_result)
            return std::string();
        int extruder_id = m_multi_nozzle_group_result->get_extruder_id(m_current_tool, m_layer_id);
        unsigned int acc         = accelerations[extruder_id];
        return set_acceleration_impl(acc);
    }
    std::string set_travel_acceleration()
    {
        std::vector<unsigned int> accelerations = m_is_first_layer ? m_first_layer_travel_accelerations : m_travel_accelerations;
        if (accelerations.empty() || !m_multi_nozzle_group_result)
            return std::string();
        int extruder_id = m_multi_nozzle_group_result->get_extruder_id(m_current_tool, m_layer_id);
        unsigned int acc = accelerations[extruder_id];
        return set_acceleration_impl(acc);
    }
    std::string set_acceleration_impl(unsigned int acceleration) {
        // Clamp the acceleration to the allowed maximum.
        if (m_max_acceleration > 0 && acceleration > m_max_acceleration)
            acceleration = m_max_acceleration;

        if (acceleration == 0 || acceleration == m_last_acceleration)
            return std::string();

        m_last_acceleration = acceleration;

        std::ostringstream gcode;
        if (m_gcode_flavor == gcfRepetier) {
            // M201: Set max printing acceleration
            gcode << "M201 X" << acceleration << " Y" << acceleration;
            gcode << "\n";
            // M202: Set max travel acceleration
            gcode << "M202 X" << acceleration << " Y" << acceleration;
        } else if (m_gcode_flavor == gcfRepRapFirmware) {
            // M204: Set default acceleration
            gcode << "M204 P" << acceleration;
        } else if (m_gcode_flavor == gcfMarlinFirmware) {
            // This is new MarlinFirmware with separated print/retraction/travel acceleration.
            // Use M204 P, we don't want to override travel acc by M204 S (which is deprecated anyway).
            gcode << "M204 P" << acceleration;
        }
        else if (m_gcode_flavor == gcfKlipper && m_accel_to_decel_enable) {
            gcode << "SET_VELOCITY_LIMIT ACCEL_TO_DECEL=" << acceleration * m_accel_to_decel_factor / 100;
            gcode << "\nM204 S" << acceleration;
        }
        else {
            // M204: Set default acceleration
            gcode << "M204 S" << acceleration;
        }
        gcode << "\n";
        return gcode.str();
    }
    std::vector<unsigned int> m_normal_accelerations;
    std::vector<unsigned int> m_first_layer_normal_accelerations;
    std::vector<unsigned int> m_travel_accelerations;
    std::vector<unsigned int> m_first_layer_travel_accelerations;
    bool                      m_is_first_layer{false};
    unsigned int              m_max_acceleration{0};
    unsigned int              m_last_acceleration{0};
    bool                      m_accel_to_decel_enable;
    float                     m_accel_to_decel_factor;
    const MultiNozzleUtils::LayeredNozzleGroupResult *m_multi_nozzle_group_result{nullptr};
    int                       m_layer_id = -1;
    bool                      m_handle_hotend_as_extruder = false;
    std::vector<int>          m_physical_extruder_map;

private:
	Vec2f         m_start_pos { 0.f, 0.f };
	Vec2f         m_current_pos;
    std::vector<Vec2f>  m_wipe_path;
	float    	  m_current_z;
	float 	  	  m_current_feedrate;
    size_t        m_current_tool;
	float 		  m_layer_height;
    float         m_line_width;
	float 	  	  m_extrusion_flow;
	bool		  m_preview_suppressed;
	std::string   m_gcode;
	std::vector<WipeTower::Extrusion> m_extrusions;
	float         m_elapsed_time;
	float   	  m_internal_angle = 0.f;
	float		  m_y_shift = 0.f;
	float 		  m_wipe_tower_width = 0.f;
	float		  m_wipe_tower_depth = 0.f;
    unsigned      m_last_fan_speed = 0;
    int           current_temp = -1;
#if ENABLE_GCODE_VIEWER_DATA_CHECKING
    const float   m_default_analyzer_line_width;
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING
    float         m_used_filament_length = 0.f;
    GCodeFlavor   m_gcode_flavor;
    bool          m_enable_arc_fitting = true;
    const std::vector<WipeTower::FilamentParameters>& m_filpar;

	std::string   set_format_X(float x)
    {
        m_current_pos.x() = x;
        return " X" + Slic3r::float_to_string_decimal_point(x, 3);
	}

	std::string   set_format_Y(float y) {
        m_current_pos.y() = y;
        return " Y" + Slic3r::float_to_string_decimal_point(y, 3);
	}

	std::string   set_format_Z(float z) {
        return " Z" + Slic3r::float_to_string_decimal_point(z, 3);
	}

	std::string   set_format_E(float e) {
        return " E" + Slic3r::float_to_string_decimal_point(e, 4);
	}

	std::string   set_format_F(float f) {
        char buf[64];
        sprintf(buf, " F%d", int(floor(f + 0.5f)));
        m_current_feedrate = f;
        return buf;
	}
    std::string set_format_I(float i) { return " I" + Slic3r::float_to_string_decimal_point(i, 3); }
    std::string set_format_J(float j) { return " J" + Slic3r::float_to_string_decimal_point(j, 3); }

	WipeTowerWriter& operator=(const WipeTowerWriter &rhs);

	// Rotate the point around center of the wipe tower about given angle (in degrees)
	Vec2f rotate(Vec2f pt) const
	{
		pt.x() -= m_wipe_tower_width / 2.f;
		pt.y() += m_y_shift - m_wipe_tower_depth / 2.f;
	    double angle = m_internal_angle * float(M_PI/180.);
	    double c = cos(angle);
	    double s = sin(angle);
	    return Vec2f(float(pt.x() * c - pt.y() * s) + m_wipe_tower_width / 2.f, float(pt.x() * s + pt.y() * c) + m_wipe_tower_depth / 2.f);
	}

}; // class WipeTowerWriter



WipeTower::ToolChangeResult WipeTower::construct_tcr(WipeTowerWriter& writer,
                                                     bool priming,
                                                     size_t old_tool,
                                                     bool is_finish,
                                                     bool is_tool_change,
                                                     float purge_volume,
                                                     bool is_contact,
                                                     float slab_height ) const
{
    ToolChangeResult result;
    result.priming      = priming;
    result.initial_tool = int(old_tool);
    result.new_tool     = int(m_current_tool);
    result.print_z      = m_z_pos;
    result.layer_height = slab_height > 0.f ? slab_height : m_layer_height;
    result.elapsed_time = writer.elapsed_time();
    result.start_pos    = writer.start_pos_rotated();
    result.end_pos      = priming ? writer.pos() : writer.pos_rotated();
    result.gcode        = std::move(writer.gcode());
    result.extrusions   = std::move(writer.extrusions());
    result.wipe_path    = std::move(writer.wipe_path());
    result.is_finish_first = is_finish;
    result.nozzle_change_result = m_nozzle_change_result;
    result.is_tool_change       = is_tool_change;
    result.tool_change_start_pos = is_tool_change ? result.start_pos : Vec2f(0, 0);
    result.is_contact            = is_contact;
    // BBS
    result.purge_volume = purge_volume;
    return result;
}

WipeTower::ToolChangeResult WipeTower::construct_block_tcr(WipeTowerWriter &writer, bool priming, size_t filament_id, bool is_finish, float purge_volume, float slab_height) const
{
    ToolChangeResult result;
    result.priming              = priming;
    result.initial_tool         = int(filament_id);
    result.new_tool             = int(filament_id);
    result.print_z              = m_z_pos;
    result.layer_height         = slab_height > 0.f ? slab_height : m_layer_height;
    result.elapsed_time         = writer.elapsed_time();
    result.start_pos            = writer.start_pos_rotated();
    result.end_pos              = priming ? writer.pos() : writer.pos_rotated();
    result.gcode                = std::move(writer.gcode());
    result.extrusions           = std::move(writer.extrusions());
    result.wipe_path            = std::move(writer.wipe_path());
    result.is_finish_first      = is_finish;
    result.is_tool_change       = false;
    result.tool_change_start_pos = Vec2f(0, 0);
    // BBS
    result.purge_volume = purge_volume;
    return result;
}

// BBS
// Local alias so uses below don't need rewriting; the header value is shared with
// PartPlate::estimate_wipe_tower_size.
static constexpr double wrapping_wipe_tower_depth = WipeTower::wrapping_wipe_tower_depth;

// BBS
const std::map<float, float> WipeTower::min_depth_per_height = {
    {5.f,5.f}, {100.f, 20.f}, {250.f, 40.f}, {350.f, 60.f}
};

float WipeTower::get_limit_depth_by_height(float max_height)
{
    float min_wipe_tower_depth = 0.f;
    auto  iter                 = WipeTower::min_depth_per_height.begin();
    while (iter != WipeTower::min_depth_per_height.end()) {
        auto curr_height_to_depth = *iter;

        // This is the case that wipe tower height is lower than the first min_depth_to_height member.
        if (curr_height_to_depth.first >= max_height) {
            min_wipe_tower_depth = curr_height_to_depth.second;
            break;
        }

        iter++;

        // If curr_height_to_depth is the last member, use its min_depth.
        if (iter == WipeTower::min_depth_per_height.end()) {
            min_wipe_tower_depth = curr_height_to_depth.second;
            break;
        }

        // If wipe tower height is between the current and next member, set the min_depth as linear interpolation between them
        auto next_height_to_depth = *iter;
        if (next_height_to_depth.first > max_height) {
            float height_base    = curr_height_to_depth.first;
            float height_diff    = next_height_to_depth.first - curr_height_to_depth.first;
            float min_depth_base = curr_height_to_depth.second;
            float depth_diff     = next_height_to_depth.second - curr_height_to_depth.second;

            min_wipe_tower_depth = min_depth_base + (max_height - curr_height_to_depth.first) / height_diff * depth_diff;
            break;
        }
    }
    return min_wipe_tower_depth;
}

float WipeTower::get_auto_brim_by_height(float max_height)
{
    if (max_height < 100)
        return max_height / 100.f * 8.f;
    return 8.f;
}

Vec2f WipeTower::move_box_inside_box(const BoundingBox &box1, const BoundingBox &box2,int scaled_offset)
{
    Vec2f res{0, 0};
    if (box1.size()[0] >= box2.size()[0]- 2*scaled_offset || box1.size()[1] >= box2.size()[1]-2*scaled_offset) return res;

    if (box1.max[0] > box2.max[0] - scaled_offset) {
        res[0] = unscaled<float>((box2.max[0] - scaled_offset) - box1.max[0]);
    }
    else if (box1.min[0] < box2.min[0] + scaled_offset) {
        res[0] = unscaled<float>((box2.min[0] + scaled_offset) - box1.min[0]);
    }

    if (box1.max[1] > box2.max[1] - scaled_offset) {
        res[1] = unscaled<float>((box2.max[1] - scaled_offset) - box1.max[1]);
    }
    else if (box1.min[1] < box2.min[1] + scaled_offset) {
        res[1] = unscaled<float>((box2.min[1] + scaled_offset) - box1.min[1]);
    }
    return res;
}

Polygon WipeTower::rib_section(float width, float depth, float rib_length, float rib_width,bool fillet_wall)
{
    Polygon res;
    res.points.resize(16);
    float              theta     = std::atan(width / depth);
    float              costheta  = std::cos(theta);
    float              sintheta  = std::sin(theta);
    float              w         = rib_width / 2.f;
    float              diag      = std::sqrt(width * width + depth * depth);
    float              l         = (rib_length - diag) / 2;
    Vec2f              diag_dir1 = Vec2f{width, depth}.normalized();
    Vec2f              diag_dir1_perp{-diag_dir1[1], diag_dir1[0]};
    Vec2f              diag_dir2 = Vec2f{-width, depth}.normalized();
    Vec2f              diag_dir2_perp{-diag_dir2[1], diag_dir2[0]};
    std::vector<Vec2f> p{{0, 0}, {width, 0}, {width, depth}, {0, depth}};
    Polyline           p_render;
    for (auto &x : p) p_render.points.push_back(scaled(x));
    res.points[0] = scaled(Vec2f{p[0].x(), p[0].y() + w / sintheta});
    res.points[1] = scaled(Vec2f{p[0] - diag_dir1 * l + diag_dir1_perp * w});
    res.points[2] = scaled(Vec2f{p[0] - diag_dir1 * l - diag_dir1_perp * w});
    res.points[3] = scaled(Vec2f{p[0].x() + w / costheta, p[0].y()});

    res.points[4] = scaled(Vec2f{p[1].x() - w / costheta, p[1].y()});
    res.points[5] = scaled(Vec2f{p[1] - diag_dir2 * l + diag_dir2_perp * w});
    res.points[6] = scaled(Vec2f{p[1] - diag_dir2 * l - diag_dir2_perp * w});
    res.points[7] = scaled(Vec2f{p[1].x(), p[1].y() + w / sintheta});

    res.points[8]  = scaled(Vec2f{p[2].x(), p[2].y() - w / sintheta});
    res.points[9]  = scaled(Vec2f{p[2] + diag_dir1 * l - diag_dir1_perp * w});
    res.points[10] = scaled(Vec2f{p[2] + diag_dir1 * l + diag_dir1_perp * w});
    res.points[11] = scaled(Vec2f{p[2].x() - w / costheta, p[2].y()});

    res.points[12] = scaled(Vec2f{p[3].x() + w / costheta, p[3].y()});
    res.points[13] = scaled(Vec2f{p[3] + diag_dir2 * l - diag_dir2_perp * w});
    res.points[14] = scaled(Vec2f{p[3] + diag_dir2 * l + diag_dir2_perp * w});
    res.points[15] = scaled(Vec2f{p[3].x(), p[3].y() - w / sintheta});
    res.remove_duplicate_points();
    if (fillet_wall) { res = rounding_polygon(res); }
    res.points.shrink_to_fit();
    return res;
}

TriangleMesh WipeTower::its_make_rib_tower(float width, float depth, float height, float rib_length, float rib_width, bool fillet_wall)
{
    TriangleMesh res;
    Polygon      bottom = rib_section(width, depth, rib_length, rib_width, fillet_wall);
    Polygon      top    = rib_section(width, depth, std::sqrt(width * width + depth * depth), rib_width, fillet_wall);
    if (fillet_wall)
        assert(bottom.points.size() == top.points.size());
    int     offset       = bottom.points.size();
    res.its.vertices.reserve(offset * 2);
    if (bottom.area() < scaled(EPSILON) || top.area() < scaled(EPSILON) || bottom.points.size() != top.points.size()) return res;
    auto    faces_bottom = Triangulation::triangulate(bottom);
    auto    faces_top    = Triangulation::triangulate(top);
    res.its.indices.reserve(offset * 2 + faces_bottom.size() + faces_top.size());
    for (auto &t : faces_bottom) res.its.indices.push_back({t[1], t[0], t[2]});
    for (auto &t : faces_top) res.its.indices.push_back({t[0] + offset, t[1] + offset, t[2] + offset});

    for (int i = 0; i < bottom.size(); i++) res.its.vertices.push_back({unscaled<float>(bottom[i][0]), unscaled<float>(bottom[i][1]), 0});
    for (int i = 0; i < top.size(); i++) res.its.vertices.push_back({unscaled<float>(top[i][0]), unscaled<float>(top[i][1]), height});

    for (int i = 0; i < offset; i++) {
        int a = i;
        int b = (i + 1) % offset;
        int c = i + offset;
        int d = b + offset;
        res.its.indices.push_back({a, b, c});
        res.its.indices.push_back({d, c, b});
    }
    return res;
}

TriangleMesh WipeTower::its_make_rib_brim(const Polygon& brim, float layer_height) {
    TriangleMesh res;
    if (brim.area() < scaled(EPSILON))return res;
    int          offset = brim.size();
    res.its.vertices.reserve(brim.size() * 2);
    auto    faces= Triangulation::triangulate(brim);
    res.its.indices.reserve(brim.size() * 2  + 2 * faces.size());
    for (auto &t : faces) res.its.indices.push_back({t[1], t[0], t[2]});
    for (auto &t : faces) res.its.indices.push_back({t[0] + offset, t[1] + offset, t[2] + offset});

    for (int i = 0; i < brim.size(); i++) res.its.vertices.push_back({unscaled<float>(brim[i][0]), unscaled<float>(brim[i][1]), 0});
    for (int i = 0; i < brim.size(); i++) res.its.vertices.push_back({unscaled<float>(brim[i][0]), unscaled<float>(brim[i][1]), layer_height});

    for (int i = 0; i < offset; i++) {
        int a = i;
        int b = (i + 1) % offset;
        int c = i + offset;
        int d = b + offset;
        res.its.indices.push_back({a, b, c});
        res.its.indices.push_back({d, c, b});
    }
    return res;
}


WipeTower::WipeTower(const PrintConfig& config, int plate_idx, Vec3d plate_origin, size_t initial_tool, const float wipe_tower_height, const std::vector<unsigned int>& slice_used_filaments) :
    m_semm(config.single_extruder_multi_material.value),
    m_wipe_tower_pos(config.wipe_tower_x.get_at(plate_idx), config.wipe_tower_y.get_at(plate_idx)),
    m_wipe_tower_width(float(config.prime_tower_width)),
    // BBS
    m_wipe_tower_height(wipe_tower_height),
    m_wipe_tower_rotation_angle(float(config.wipe_tower_rotation_angle)),
    m_wipe_tower_brim_width(float(config.prime_tower_brim_width)),
    m_y_shift(0.f),
    m_z_pos(0.f),
    //m_bridging(float(config.wipe_tower_bridging)),
    m_bridging(10.f),
    m_sparse_layers_skipped(wipe_tower_sparse_layers_skipped(config)),
    m_gcode_flavor(config.gcode_flavor),
    m_travel_speed(config.travel_speed.get_at(get_process_config_idx(config, (unsigned int)initial_tool))),
    m_current_tool(initial_tool),
    //wipe_volumes(flush_matrix)
    m_enable_timelapse_print(config.timelapse_type.value == TimelapseType::tlSmooth),
    m_enable_wrapping_detection(config.enable_wrapping_detection),
    m_wrapping_detection_layers(config.wrapping_detection_layers.value && (config.wrapping_exclude_area.values.size() > 2)),
    m_slice_used_filaments(slice_used_filaments.size()),
    m_is_multi_extruder(config.nozzle_diameter.size() > 1),
    m_use_gap_wall(config.prime_tower_skip_points.value),
    m_use_rib_wall(config.prime_tower_rib_wall.value),
    m_extra_rib_length((float)config.prime_tower_extra_rib_length.value),
    m_rib_width((float)config.prime_tower_rib_width.value),
    m_used_fillet(config.prime_tower_fillet_wall.value),
    m_extra_spacing((float)config.prime_tower_infill_gap.value/100.f),
    m_tower_framework(config.prime_tower_enable_framework.value),
    m_max_speed((float)config.prime_tower_max_speed.value*60.f),
    m_tail_speed_factor(std::clamp((float)config.prime_tower_tail_speed_factor.value / 100.f, 0.1f, 1.f)),
    m_tail_slowdown_volume(std::max(0.f, (float)config.prime_tower_tail_slowdown_volume.value)),
    m_accel_to_decel_enable(config.accel_to_decel_enable.value),
    m_accel_to_decel_factor(config.accel_to_decel_factor.value),
    m_printable_height(config.extruder_printable_height.values),
    m_flat_ironing(config.prime_tower_flat_ironing.value),
    m_line_ironing(config.prime_tower_line_ironing.value),
    m_ironing_speed(config.prime_tower_ironing_speed.value * 60.f),
    m_enable_side_scrape(config.prime_tower_side_scrape.value),
    m_side_scrape_on_left_right(config.prime_tower_side_scrape_face.value == sssfLeftRight),
    m_side_scrape_length((float) config.prime_tower_side_scrape_length.value),
    m_side_scrape_height((float) config.prime_tower_side_scrape_height.value),
    m_side_scrape_speed((float) config.prime_tower_side_scrape_speed.value * 60.f),
    m_side_scrape_wall_gap((float) config.prime_tower_side_scrape_wall_gap.value),
    m_side_scrape_point_spacing((float) config.prime_tower_side_scrape_point_spacing.value),
    m_side_scrape_along_length((float) config.prime_tower_side_scrape_along_length.value),
    m_enable_tower_interface_features(config.enable_tower_interface_features.value),
    m_physical_extruder_map(config.physical_extruder_map.values),
    m_enable_arc_fitting(config.enable_arc_fitting.value),
    m_has_filament_switcher(config.has_filament_switcher.value),
    m_handle_hotend_as_extruder(config.handle_hotend_as_extruder.value)
{
    m_contact_speed                  = 20 * 60.f;
    m_filaments_change_length.first = config.filament_change_length.values;
    m_filaments_change_length.second = config.filament_change_length_nc.values;
    m_hotend_heating_rate            = config.hotend_heating_rate.values;
    m_hotend_cooling_rate            = config.hotend_cooling_rate.values;
    {
        float max_x = config.machine_max_speed_x.values.empty() ? 500.f : (float)config.machine_max_speed_x.values[0];
        float max_y = config.machine_max_speed_y.values.empty() ? 500.f : (float)config.machine_max_speed_y.values[0];
        m_machine_max_speed = std::min(max_x, max_y) * 60.f;
    }
    m_flat_ironing = (m_flat_ironing && m_use_gap_wall);
    m_normal_accels.clear();
    if (config.prime_tower_acceleration.value > 0) {
        unsigned int tower_accel = (unsigned int) floor(config.prime_tower_acceleration.value + 0.5);
        for (size_t i = 0; i < config.default_acceleration.values.size(); ++i)
            m_normal_accels.emplace_back(tower_accel);
    } else {
        for (auto value : config.default_acceleration.values)
            m_normal_accels.emplace_back((unsigned int) floor(value + 0.5));
    }

    m_first_layer_normal_accels.clear();
    for (auto value : config.initial_layer_acceleration.values) {
        m_first_layer_normal_accels.emplace_back((unsigned int) floor(value + 0.5));
    }

    m_travel_accels.clear();
    for (auto value : config.travel_acceleration.values) {
        m_travel_accels.emplace_back((unsigned int) floor(value + 0.5));
    }

    m_first_layer_travel_accels.clear();
    for (auto value : config.initial_layer_travel_acceleration.values) {
        m_first_layer_travel_accels.emplace_back((unsigned int) floor(value + 0.5));
    }

    m_max_accels = config.machine_max_acceleration_extruding.values.front();

    // Read absolute value of first layer speed, if given as percentage,
    // it is taken over following default. Speeds from config are not
    // easily accessible here.
    const float default_speed = 60.f;
    m_first_layer_speed       = config.initial_layer_speed.get_at(get_process_config_idx(config, (unsigned int) initial_tool));
    if (m_first_layer_speed == 0.f) // just to make sure autospeed doesn't break it.
        m_first_layer_speed = default_speed / 2.f;

    // If this is a single extruder MM printer, we will use all the SE-specific config values.
    // Otherwise, the defaults will be used to turn off the SE stuff.
    // BBS: remove useless config
#if 0
    if (m_semm) {
        m_cooling_tube_retraction = float(config.cooling_tube_retraction);
        m_cooling_tube_length     = float(config.cooling_tube_length);
        m_parking_pos_retraction  = float(config.parking_pos_retraction);
        m_extra_loading_move      = float(config.extra_loading_move);
        m_set_extruder_trimpot    = config.high_current_on_filament_swap;
    }
#endif
    // Calculate where the priming lines should be - very naive test not detecting parallelograms etc.
    const std::vector<Vec2d>& bed_points = config.printable_area.values;
    BoundingBoxf bb(bed_points);
    m_bed_width = float(bb.size().x());
    m_bed_shape = (bed_points.size() == 4 ? RectangularBed : CircularBed);

    if (m_bed_shape == CircularBed) {
        // this may still be a custom bed, check that the points are roughly on a circle
        double r2 = std::pow(m_bed_width/2., 2.);
        double lim2 = std::pow(m_bed_width/10., 2.);
        Vec2d center = bb.center();
        for (const Vec2d& pt : bed_points)
            if (std::abs(std::pow(pt.x()-center.x(), 2.) + std::pow(pt.y()-center.y(), 2.) - r2) > lim2) {
                m_bed_shape = CustomBed;
                break;
            }
    }

    m_bed_bottom_left = m_bed_shape == RectangularBed
                  ? Vec2f(bed_points.front().x(), bed_points.front().y())
                  : Vec2f::Zero();
    m_last_layer_id.resize(config.nozzle_diameter.size(), -1);
    m_origin  = {plate_origin[0], plate_origin[1]};
    m_is_multiple_nozzle = std::any_of(config.extruder_max_nozzle_count.values.begin(), config.extruder_max_nozzle_count.values.end(), [](auto &elem) { return elem > 1; });
}



void WipeTower::set_extruder(size_t idx, const PrintConfig& config, size_t config_idx)
{
    //while (m_filpar.size() < idx+1)   // makes sure the required element is in the vector
    m_filpar.push_back(FilamentParameters());

    m_filpar[idx].material = config.filament_type.get_at(idx);
    m_filpar[idx].is_soluble = config.filament_soluble.get_at(idx);
    // BBS
    m_filpar[idx].is_support = config.filament_is_support.get_at(idx);
    m_filpar[idx].nozzle_temperature = config.nozzle_temperature.get_at(config_idx);
    m_filpar[idx].nozzle_temperature_initial_layer = config.nozzle_temperature_initial_layer.get_at(config_idx);
    m_filpar[idx].category = config.filament_adhesiveness_category.get_at(idx);
    m_filpar[idx].flat_iron_area = config.filament_tower_ironing_area.get_at(idx);

    // If this is a single extruder MM printer, we will use all the SE-specific config values.
    // Otherwise, the defaults will be used to turn off the SE stuff.
    // BBS: remove useless config
#if 0
    if (m_semm) {
        m_filpar[idx].loading_speed           = float(config.filament_loading_speed.get_at(idx));
        m_filpar[idx].loading_speed_start     = float(config.filament_loading_speed_start.get_at(idx));
        m_filpar[idx].unloading_speed         = float(config.filament_unloading_speed.get_at(idx));
        m_filpar[idx].unloading_speed_start   = float(config.filament_unloading_speed_start.get_at(idx));
        m_filpar[idx].delay                   = float(config.filament_toolchange_delay.get_at(idx));
        m_filpar[idx].cooling_moves           = config.filament_cooling_moves.get_at(idx);
        m_filpar[idx].cooling_initial_speed   = float(config.filament_cooling_initial_speed.get_at(idx));
        m_filpar[idx].cooling_final_speed     = float(config.filament_cooling_final_speed.get_at(idx));
    }
#endif

    m_filpar[idx].filament_area = float((M_PI/4.f) * pow(config.filament_diameter.get_at(idx), 2));
    float nozzle_diameter = nozzle_diameter_for_filament(config, unsigned(idx + 1));
    if (nozzle_diameter <= 0.f)
        nozzle_diameter = float(config.nozzle_diameter.get_at(idx));
    m_filpar[idx].nozzle_diameter = nozzle_diameter;
    // Multi-diameter mode uses the hardcoded per-diameter defaults. The process values stay
    // editable only while that mode is off.
    const float line_width = m_per_nozzle_tower_params ?
        float(multi_nozzle_prime_tower_line_width(nozzle_diameter, config.prime_tower_line_width.value)) :
        float(config.prime_tower_line_width.value);
    const float ramming_line_width = m_per_nozzle_tower_params ?
        float(multi_nozzle_prime_tower_ramming_line_width(nozzle_diameter, config.prime_tower_ramming_line_width.value)) :
        float(config.prime_tower_ramming_line_width.value);
    const float feedrate_factor = m_per_nozzle_tower_params ?
        float(multi_nozzle_prime_tower_feedrate_factor(nozzle_diameter, config.prime_tower_feedrate_factor.value)) :
        float(config.prime_tower_feedrate_factor.value);
    m_filpar[idx].perimeter_width = line_width > 0 ? line_width : nozzle_diameter * Width_To_Nozzle_Ratio;
    m_filpar[idx].nozzle_change_perimeter_width = ramming_line_width > 0 ?
        ramming_line_width : nozzle_change_width_for_diameter(nozzle_diameter);
    m_filpar[idx].feedrate_factor = std::max(0.01f, feedrate_factor);
    // Shared tower line widths follow the finest filament nozzle in use. The fixed Z-grid reference is set
    // separately from the machine's layer-grid nozzle, so an unused fine nozzle still anchors band sizes.
    // Per-filament widths and the feedrate factor live in m_filpar.
    if (m_tower_grid_nozzle_diameter <= 0.f || nozzle_diameter < m_tower_grid_nozzle_diameter) {
        m_tower_grid_nozzle_diameter    = nozzle_diameter;
        m_perimeter_width               = m_filpar[idx].perimeter_width;
        m_nozzle_change_perimeter_width = m_filpar[idx].nozzle_change_perimeter_width;
    }
    const float idx_filament_area = m_filpar[idx].filament_area;

    float max_vol_speed = float(config.filament_max_volumetric_speed.get_at(config_idx));
    if (max_vol_speed!= 0.f)
        m_filpar[idx].max_e_speed = (max_vol_speed / idx_filament_area);

    //set extruder change and nozzle change ramming speed
    {
        float ramming_vol_speed = float(config.filament_ramming_volumetric_speed.get_at(config_idx));
        if (config.filament_ramming_volumetric_speed.is_nil(config_idx) || is_approx(config.filament_ramming_volumetric_speed.get_at(config_idx), -1.)) ramming_vol_speed = max_vol_speed;
        m_filpar[idx].max_e_ramming_speed.first = (ramming_vol_speed / idx_filament_area);

        float ramming_vol_speed_nc = float(config.filament_ramming_volumetric_speed_nc.get_at(config_idx));
        if (config.filament_ramming_volumetric_speed_nc.is_nil(config_idx) || is_approx(config.filament_ramming_volumetric_speed_nc.get_at(config_idx), -1.))
            ramming_vol_speed_nc = max_vol_speed;
        m_filpar[idx].max_e_ramming_speed.second = (ramming_vol_speed_nc / idx_filament_area);
    }

    //set precooling time/precooling target temp during extruder change and nozzle change
    {
        int extruder_count = m_multi_nozzle_group_result->get_extruder_count();
        m_filpar[idx].precool_t.first.resize(extruder_count, 0.f);
        m_filpar[idx].precool_t_first_layer.first.resize(extruder_count, 0.f);
        m_filpar[idx].precool_t.second.resize(extruder_count, 0.f);
        m_filpar[idx].precool_t_first_layer.second.resize(extruder_count, 0.f);
        m_filpar[idx].precool_target_temp.first     = 0;
        m_filpar[idx].precool_target_temp.second     = 0;
        float nozzle_temp_first_layer = config.nozzle_temperature_initial_layer.is_nil(config_idx) ? -1.f : float(config.nozzle_temperature_initial_layer.get_at(config_idx));
        float nozzle_temp_other_layer = config.nozzle_temperature.is_nil(config_idx) ? -1.f : float(config.nozzle_temperature.get_at(config_idx));
        std::vector<double> hotend_cooling_rates    = config.hotend_cooling_rate.values;
        auto  is_need_precooling      = [&](bool extruder_change) -> bool
        {
            bool res = config.enable_pre_heating.value; 
            if (extruder_change) return res &&!config.filament_pre_cooling_temperature.is_nil(config_idx) && config.filament_pre_cooling_temperature.get_at(config_idx) != 0;
            return res &&!config.filament_pre_cooling_temperature_nc.is_nil(config_idx) && config.filament_pre_cooling_temperature_nc.get_at(config_idx) != 0;
        };
        if (is_need_precooling(true)) {
            for (int i = 0; i < m_filpar[idx].precool_t.first.size(); i++) {
                if (config.hotend_cooling_rate.is_nil(i)) continue;
                m_filpar[idx].precool_t.first[i] = std::max(0.f, nozzle_temp_other_layer - float(config.filament_pre_cooling_temperature.get_at(config_idx))) / float(hotend_cooling_rates[i]);
                m_filpar[idx].precool_t_first_layer.first[i] = std::max(0.f, nozzle_temp_first_layer -float(config.filament_pre_cooling_temperature.get_at(config_idx))) /float(hotend_cooling_rates[i]);
            }
            m_filpar[idx].precool_target_temp.first = config.filament_pre_cooling_temperature.get_at(config_idx);
        }

        if (is_need_precooling(false)) {
            for (int i = 0; i < m_filpar[idx].precool_t.second.size(); i++) {
                if (config.hotend_cooling_rate.is_nil(i)) continue;
                m_filpar[idx].precool_t.second[i] = std::max(0.f, nozzle_temp_other_layer - float(config.filament_pre_cooling_temperature_nc.get_at(config_idx))) / float(hotend_cooling_rates[i]);
                m_filpar[idx].precool_t_first_layer.second[i] = std::max(0.f, nozzle_temp_first_layer -float(config.filament_pre_cooling_temperature_nc.get_at(config_idx))) /float(hotend_cooling_rates[i]);
            }
            m_filpar[idx].precool_target_temp.second = config.filament_pre_cooling_temperature_nc.get_at(config_idx);
        }

    }
    //set ramming reverse travel time during extruder change and nozzle change
    {
        m_filpar[idx].ramming_travel_time = {0, 0};
        if (!config.filament_ramming_travel_time.is_nil(config_idx)) m_filpar[idx].ramming_travel_time.first = float(config.filament_ramming_travel_time.get_at(config_idx));
        if (!config.filament_ramming_travel_time_nc.is_nil(config_idx)) m_filpar[idx].ramming_travel_time.second = float(config.filament_ramming_travel_time_nc.get_at(config_idx));
    }
    m_filpar[idx].idle_travel_cooling_enabled = config.filament_idle_travel_cooling_enabled.get_at(idx);
    std::string filament_curve_str = config.filament_prime_tower_speed_factor_curve.get_at(idx);
    m_filpar[idx].speed_factor_curve = parseDoubles(filament_curve_str);

    // BBS: remove useless config
#if 0
    if (m_semm) {
        std::istringstream stream{config.filament_ramming_parameters.get_at(idx)};
        float speed = 0.f;
        stream >> m_filpar[idx].ramming_line_width_multiplicator >> m_filpar[idx].ramming_step_multiplicator;
        m_filpar[idx].ramming_line_width_multiplicator /= 100;
        m_filpar[idx].ramming_step_multiplicator /= 100;
        while (stream >> speed)
            m_filpar[idx].ramming_speed.push_back(speed);
    }
#endif

    m_used_filament_length.resize(std::max(m_used_filament_length.size(), idx + 1)); // makes sure that the vector is big enough so we don't have to check later

    m_filpar[idx].retract_length = config.retraction_length.get_at(config_idx);
    m_filpar[idx].retract_speed  = config.retraction_speed.get_at(config_idx);
    m_filpar[idx].wipe_dist      = config.wipe_distance.get_at(config_idx);
    m_filpar[idx].filament_cooling_before_tower = config.filament_cooling_before_tower.get_at(config_idx);
    m_filpar[idx].filament_petg_pre_extrusion_offset_dist = config.filament_tower_interface_pre_extrusion_dist.get_at(idx);
    if (config.enable_tower_interface_features.value) {
        m_filpar[idx].filament_tower_interface_print_temp = config.filament_tower_interface_print_temp.get_at(idx) == -1 ? config.nozzle_temperature_range_high.get_at(idx) :
                                                                                                                           config.filament_tower_interface_print_temp.get_at(idx);
        m_filpar[idx].filament_tower_interface_pre_extrusion_dist = config.filament_tower_interface_pre_extrusion_dist.get_at(idx);
        m_filpar[idx].filament_tower_interface_pre_extrusion_length = config.filament_tower_interface_pre_extrusion_length.get_at(idx);
    } else {
        m_filpar[idx].filament_tower_interface_print_temp = config.nozzle_temperature.get_at(config_idx);
        m_filpar[idx].filament_tower_interface_pre_extrusion_dist = 0.f;
        m_filpar[idx].filament_tower_interface_pre_extrusion_length = 0.f;
    }
}

void WipeTower::set_single_diameter_parameters(float nozzle_diameter)
{
    for (const FilamentParameters &filament : m_filpar)
        if (std::abs(filament.nozzle_diameter - nozzle_diameter) < EPSILON) {
            m_tower_grid_nozzle_diameter    = filament.nozzle_diameter;
            m_perimeter_width               = filament.perimeter_width;
            m_nozzle_change_perimeter_width = filament.nozzle_change_perimeter_width;
            return;
        }
    // The nozzle comes from the filaments the plate prints with, so one of them must carry it.
    throw Slic3r::RuntimeError("Single-diameter layering nozzle " + std::to_string(nozzle_diameter) +
                               " is not used by any filament of the wipe tower.");
}



// Returns gcode to prime the nozzles at the front edge of the print bed.
std::vector<WipeTower::ToolChangeResult> WipeTower::prime(
	// print_z of the first layer.
	float 						initial_layer_print_height,
	// Extruder indices, in the order to be primed. The last extruder will later print the wipe tower brim, print brim and the object.
	const std::vector<unsigned int> &tools,
	// If true, the last priming are will be the same as the other priming areas, and the rest of the wipe will be performed inside the wipe tower.
	// If false, the last priming are will be large enough to wipe the last extruder sufficiently.
    bool 						/*last_wipe_inside_wipe_tower*/)
{
    return std::vector<ToolChangeResult>();
}

float WipeTower::wipe_line_spacing(bool solid_toolchange)
{
    const float tool_width = filament_perimeter_width(m_current_tool);
    if (solid_toolchange || is_first_layer())
        return tool_width;
    return m_layer_info->extra_spacing * get_block_gap_width(m_current_tool, false);
}

Vec2f WipeTower::get_next_pos(const WipeTower::box_coordinates &cleaning_box, float wipe_length, bool solid_toolchange)
{
    const float &xl = cleaning_box.ld.x();
    const float &xr = cleaning_box.rd.x();
    const float dy = wipe_line_spacing(solid_toolchange);
    const float box_depth = cleaning_box.lu.y() - cleaning_box.ld.y();
    float y_offset = std::max(0.f, box_depth - dy);
    if (!solid_toolchange && xr > xl) {
        const int line_count = std::max(1, int(std::ceil(wipe_length / (xr - xl))));
        // N rows contain N - 1 vertical steps. Starting one extra step up leaves a hole at the
        // band's bottom while cur_depth still advances as if that hole had been filled.
        y_offset = std::min(float(line_count - 1) * dy, y_offset);
    }
    const Vec2f pos_offset = Vec2f(0.f, m_depth_traversed);

    Vec2f res;
    int   index = m_cur_layer_id % 4;
    //Vec2f offset = m_use_gap_wall ? Vec2f(5 * m_perimeter_width, 0) : Vec2f{0, 0};
    Vec2f offset = Vec2f{0, 0};
    switch (index % 4) {
    case 0:
        res = offset +cleaning_box.ld + pos_offset;
        break;
    case 1:
        res = -offset +cleaning_box.rd + pos_offset + Vec2f(0, y_offset);
        break;
    case 2:
        res = -offset+ cleaning_box.rd + pos_offset;
        break;
    case 3:
        res = offset+cleaning_box.ld + pos_offset + Vec2f(0, y_offset);
        break;
    default: break;
    }
    bool is_contact_pre_extrusion = solid_toolchange && m_enable_tower_interface_features;
    bool is_petg_pre_extrusion   = !is_contact_pre_extrusion && is_petg_filament(m_current_tool) && m_has_filament_switcher;
    if (is_contact_pre_extrusion || is_petg_pre_extrusion) {
        Vec2f        stop_pos                                    = res;
        float        filament_tower_interface_pre_extrusion_dist = is_petg_pre_extrusion
                                                                       ? m_filpar[m_current_tool].filament_petg_pre_extrusion_offset_dist
                                                                       : m_filpar[m_current_tool].filament_tower_interface_pre_extrusion_dist;
        BoundingBoxf printer_bbx                                 = unscaled(get_extents(m_shared_print_bed));
        printer_bbx.translate((-m_wipe_tower_pos - m_rib_offset).cast<double>()); // first layer never be contact
        if (stop_pos.x() < m_wipe_tower_width / 2.f)
            stop_pos = Vec2f(stop_pos.x() - filament_tower_interface_pre_extrusion_dist, stop_pos.y());
        else
            stop_pos = Vec2f(stop_pos.x() + filament_tower_interface_pre_extrusion_dist, stop_pos.y());
        if (stop_pos.x() < printer_bbx.min[0]) stop_pos.x() = printer_bbx.min[0];
        if (stop_pos.x() > printer_bbx.max[0]) stop_pos.x() = printer_bbx.max[0];
        res = stop_pos;
    }
    return res;
}

WipeTower::ToolChangeResult WipeTower::tool_change(size_t tool, bool extrude_perimeter, bool first_toolchange_to_nonsoluble)
{
    //only for tool = unsigned (-1) ,never get here
    //m_nozzle_change_result.gcode.clear();
    //if (!m_filament_map.empty() && tool < m_filament_map.size() && m_filament_map[m_current_tool] != m_filament_map[tool]) {
    //    m_nozzle_change_result = nozzle_change(m_current_tool, tool);
    //}

    size_t old_tool = m_current_tool;

    float wipe_depth = 0.f;
	float wipe_length = 0.f;
    float purge_volume = 0.f;
    float nozzle_change_depth = 0.f;
	// Finds this toolchange info
	if (tool != (unsigned int)(-1))
	{
		for (const auto &b : m_layer_info->tool_changes)
			if ( b.new_tool == tool ) {
                wipe_length = b.wipe_length;
                wipe_depth = b.required_depth;
                purge_volume = b.purge_volume;
                nozzle_change_depth = b.nozzle_change_depth;
				break;
			}
	}
	else {
		// Otherwise we are going to Unload only. And m_layer_info would be invalid.
	}

    box_coordinates cleaning_box(
		Vec2f(m_perimeter_width, m_perimeter_width),
		m_wipe_tower_width - 2 * m_perimeter_width,
        (tool != (unsigned int)(-1) ? wipe_depth + m_depth_traversed - m_perimeter_width
                                    : m_wipe_tower_depth - m_perimeter_width));

	WipeTowerWriter writer(m_layer_height, m_perimeter_width, m_gcode_flavor, m_filpar, m_enable_arc_fitting);
	writer.set_extrusion_flow(m_extrusion_flow)
		.set_z(m_z_pos)
		.set_initial_tool(m_current_tool)
        .set_y_shift(m_y_shift + (tool!=(unsigned int)(-1) && (m_current_shape == SHAPE_REVERSED) ? m_layer_info->depth - m_layer_info->toolchanges_depth(): 0.f))
		.append(";--------------------\n"
				"; CP TOOLCHANGE START\n")
		.comment_with_value(" toolchange #", m_num_tool_changes + 1); // the number is zero-based

    set_for_wipe_tower_writer(writer);

    if (tool != (unsigned)(-1))
        writer.append(std::string("; material : " + (m_current_tool < m_filpar.size() ? m_filpar[m_current_tool].material : "(NONE)") + " -> " + m_filpar[tool].material + "\n").c_str())
              .append(";--------------------\n");

    writer.speed_override_backup();
    writer.speed_override(100);

    float feedrate = is_first_layer() ? std::min(m_first_layer_speed * 60.f, 5400.f) : calc_tower_feedrate(m_current_tool, m_extrusion_flow);

    // Increase the extruder driver current to allow fast ramming.
    //BBS
	//if (m_set_extruder_trimpot)
	//	writer.set_extruder_trimpot(750);

    // Ram the hot material out of the melt zone, retract the filament into the cooling tubes and let it cool.
    if (tool != (unsigned int)-1){ 			// This is not the last change.
        writer.append(";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Wipe_Tower_Start) + "\n");
        toolchange_Unload(writer, cleaning_box, m_filpar[m_current_tool].material,
                          is_first_layer() ? m_filpar[tool].nozzle_temperature_initial_layer : m_filpar[tool].nozzle_temperature);
        toolchange_Change(writer, tool, m_filpar[tool].material); // Change the tool, set a speed override for soluble and flex materials.
        toolchange_Load(writer, cleaning_box);
        // BBS
        //writer.travel(writer.x(), writer.y()-m_perimeter_width); // cooling and loading were done a bit down the road

        if (m_is_multi_extruder && is_tpu_filament(tool)) {
            float dy                  = 2 * m_perimeter_width;
            float nozzle_change_speed = 60.0f * m_filpar[tool].max_e_speed / m_extrusion_flow;
            nozzle_change_speed *= 0.25;

            const float &xl = cleaning_box.ld.x();
            const float &xr = cleaning_box.rd.x();

            Vec2f start_pos = m_nozzle_change_result.start_pos + Vec2f(0, m_perimeter_width);
            bool   left_to_right     = true;
            double tpu_travel_length = 5;
            double e_flow            = extrusion_flow(m_layer_height);
            double length            = tpu_travel_length / e_flow;
            int    tpu_line_count    = length / (m_wipe_tower_width - 2 * m_perimeter_width) + 1;

            writer.travel(start_pos);

            for (int i = 0; true; ++i) {
                if (left_to_right)
                    writer.travel(xr - m_perimeter_width, writer.y(), nozzle_change_speed);
                else
                    writer.travel(xl + m_perimeter_width, writer.y(), nozzle_change_speed);

                if (i == tpu_line_count - 1)
                    break;

                writer.travel(writer.x(), writer.y() + dy);
                left_to_right = !left_to_right;
            }
        }

        Vec2f initial_position = get_next_pos(cleaning_box, wipe_length,false);
        writer.set_initial_position(initial_position, m_wipe_tower_width, m_wipe_tower_depth, m_internal_rotation);

        if (extrude_perimeter) {
            box_coordinates wt_box(Vec2f(0.f, (m_current_shape == SHAPE_REVERSED) ? m_layer_info->toolchanges_depth() - m_layer_info->depth : 0.f), m_wipe_tower_width,
                                   m_layer_info->depth + m_perimeter_width);

            // align the perimeter
            Vec2f pos = initial_position;
            switch (m_cur_layer_id % 4){
            case 0:
                pos = wt_box.ld;
                break;
            case 1:
                pos = wt_box.rd;
                break;
            case 2:
                pos = wt_box.ru;
                break;
            case 3:
                pos = wt_box.lu;
                break;
            default: break;
            }
            writer.set_initial_position(pos, m_wipe_tower_width, m_wipe_tower_depth, m_internal_rotation);

            wt_box = align_perimeter(wt_box);
            writer.rectangle(wt_box, feedrate);
        }

        writer.travel(initial_position);

        toolchange_Wipe(writer, cleaning_box, wipe_length);     // Wipe the newly loaded filament until the end of the assigned wipe area.

        writer.append(";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Wipe_Tower_End) + "\n");
        ++ m_num_tool_changes;
    } else
        toolchange_Unload(writer, cleaning_box, m_filpar[m_current_tool].material, m_filpar[m_current_tool].nozzle_temperature);

    m_depth_traversed += (wipe_depth - nozzle_change_depth);

    //BBS
	//if (m_set_extruder_trimpot)
	//	writer.set_extruder_trimpot(550);    // Reset the extruder current to a normal value.
    writer.speed_override_restore();
    writer.feedrate(m_travel_speed * 60.f)
          .flush_planner_queue()
          .reset_extruder()
          .append("; CP TOOLCHANGE END\n"
                  ";------------------\n"
                  "\n\n");

    // Ask our writer about how much material was consumed:
    if (m_current_tool < m_used_filament_length.size())
        m_used_filament_length[m_current_tool] += writer.get_and_reset_used_filament_length();

    return construct_tcr(writer, false, old_tool, false, true, purge_volume,false);
}
#if 0
WipeTower::NozzleChangeResult WipeTower::nozzle_change(int old_filament_id, int new_filament_id)
{
    float wipe_depth               = 0.f;
    float wipe_length              = 0.f;
    float purge_volume             = 0.f;
    int   nozzle_change_line_count = 0;

    // Finds this toolchange info
    if (new_filament_id != (unsigned int) (-1)) {
        for (const auto &b : m_layer_info->tool_changes)
            if (b.new_tool == new_filament_id) {
                wipe_length              = b.wipe_length;
                wipe_depth               = b.required_depth;
                purge_volume             = b.purge_volume;
                if (has_tpu_filament())
                    nozzle_change_line_count = ((b.nozzle_change_depth + WT_EPSILON) / m_nozzle_change_perimeter_width) / 2;
                else
                    nozzle_change_line_count = (b.nozzle_change_depth + WT_EPSILON) / m_nozzle_change_perimeter_width;
                break;
            }
    } else {
        // Otherwise we are going to Unload only. And m_layer_info would be invalid.
    }

    auto format_nozzle_change_line = [](bool start, int old_filament_id, int new_filament_id)->std::string {
        char buff[64];
        std::string tag = start ? GCodeProcessor::reserved_tag(GCodeProcessor::ETags::NozzleChangeStart) : GCodeProcessor::reserved_tag(GCodeProcessor::ETags::NozzleChangeEnd);
        snprintf(buff, sizeof(buff), ";%s OF%d NF%d\n", tag.c_str(), old_filament_id, new_filament_id);
        return std::string(buff);
        };

    float nozzle_change_speed = 60.0f * m_filpar[m_current_tool].max_e_speed / m_extrusion_flow;
    if (is_tpu_filament(m_current_tool)) {
        nozzle_change_speed *= 0.25;
    }

    WipeTowerWriter writer(m_layer_height, m_perimeter_width, m_gcode_flavor, m_filpar, m_enable_arc_fitting);
    writer.set_extrusion_flow(m_extrusion_flow)
        .set_z(m_z_pos)
        .set_initial_tool(m_current_tool)
        .set_extrusion_flow(m_extrusion_flow)
        .set_y_shift(m_y_shift + (new_filament_id != (unsigned int) (-1) && (m_current_shape == SHAPE_REVERSED) ? m_layer_info->depth - m_layer_info->toolchanges_depth() : 0.f))
        .append(format_nozzle_change_line(true,old_filament_id,new_filament_id));

    set_for_wipe_tower_writer(writer);

    box_coordinates cleaning_box(Vec2f(m_perimeter_width, m_perimeter_width), m_wipe_tower_width - 2 * m_perimeter_width,
                                 (new_filament_id != (unsigned int) (-1) ? wipe_depth + m_depth_traversed - m_perimeter_width : m_wipe_tower_depth - m_perimeter_width));

    Vec2f initial_position = cleaning_box.ld + Vec2f(0.f, m_depth_traversed);
    writer.set_initial_position(initial_position, m_wipe_tower_width, m_wipe_tower_depth, m_internal_rotation);

    const float &xl = cleaning_box.ld.x();
    const float &xr = cleaning_box.rd.x();

    float dy = m_layer_info->extra_spacing * m_perimeter_width;
    if (has_tpu_filament())
        dy = 2 * m_perimeter_width;

    float start_y = writer.y();

    m_left_to_right = true;

    bool need_change_flow = false;
    // now the wiping itself:
    for (int i = 0; true; ++i) {
        if (m_left_to_right)
            writer.extrude(xr + wipe_tower_wall_infill_overlap * m_perimeter_width, writer.y(), nozzle_change_speed);
        else
            writer.extrude(xl - wipe_tower_wall_infill_overlap * m_perimeter_width, writer.y(), nozzle_change_speed);

        if (writer.y() - float(EPSILON) > cleaning_box.lu.y())
            break; // in case next line would not fit

        if (i == nozzle_change_line_count - 1)
            break;

        // stepping to the next line:
        writer.extrude(writer.x(), writer.y() + dy);
        m_left_to_right = !m_left_to_right;
    }

    writer.set_extrusion_flow(m_extrusion_flow); // Reset the extrusion flow.

    m_depth_traversed += nozzle_change_line_count * dy;

    NozzleChangeResult result;

    if (is_tpu_filament(m_current_tool))
    {
        bool left_to_right = !m_left_to_right;
        double tpu_travel_length        = 5;
        double e_flow                   = extrusion_flow(m_layer_height);
        double length                   = tpu_travel_length / e_flow;
        int    tpu_line_count = length / (m_wipe_tower_width - 2 * m_perimeter_width) + 1;

        writer.travel(writer.x(), writer.y() - m_perimeter_width);

        for (int i = 0; true; ++i) {
            if (left_to_right)
                writer.travel(xr - m_perimeter_width, writer.y(), nozzle_change_speed);
            else
                writer.travel(xl + m_perimeter_width, writer.y(), nozzle_change_speed);

            if (i == tpu_line_count - 1)
                break;

            writer.travel(writer.x(), writer.y() - dy);
            left_to_right = !left_to_right;
        }
    }
    else {
        result.wipe_path.push_back(writer.pos());
        if (m_left_to_right) {
             result.wipe_path.push_back(Vec2f(0, writer.y()));
        } else {
             result.wipe_path.push_back(Vec2f(m_wipe_tower_width, writer.y()));
        }
    }

    writer.append(format_nozzle_change_line(false, old_filament_id, new_filament_id));

    result.start_pos = writer.start_pos_rotated();
    result.end_pos   = writer.pos();
    result.gcode     = writer.gcode();
    return result;
}
#endif
// Ram the hot material out of the melt zone, retract the filament into the cooling tubes and let it cool.
void WipeTower::toolchange_Unload(
	WipeTowerWriter &writer,
	const box_coordinates 	&cleaning_box,
	const std::string&		 current_material,
	const int 				 new_temperature)
{
    // BBS: toolchange unload is done in change_filament_gcode
#if 0
	float xl = cleaning_box.ld.x() + 1.f * m_perimeter_width;
	float xr = cleaning_box.rd.x() - 1.f * m_perimeter_width;

	const float line_width = m_perimeter_width * m_filpar[m_current_tool].ramming_line_width_multiplicator;       // desired ramming line thickness
	const float y_step = line_width * m_filpar[m_current_tool].ramming_step_multiplicator * m_extra_spacing; // spacing between lines in mm

    writer.append("; CP TOOLCHANGE UNLOAD\n")
        .change_analyzer_line_width(line_width);

	unsigned i = 0;										// iterates through ramming_speed
	m_left_to_right = true;								// current direction of ramming
	float remaining = xr - xl ;							// keeps track of distance to the next turnaround
	float e_done = 0;									// measures E move done from each segment

	writer.travel(xl, cleaning_box.ld.y() + m_depth_traversed + y_step/2.f ); // move to starting position

    // if the ending point of the ram would end up in mid air, align it with the end of the wipe tower:
    if (m_layer_info > m_plan.begin() && m_layer_info < m_plan.end() && (m_layer_info-1!=m_plan.begin() || !m_adhesion )) {

        // this is y of the center of previous sparse infill border
        float sparse_beginning_y = 0.f;
        if (m_current_shape == SHAPE_REVERSED)
            sparse_beginning_y += ((m_layer_info-1)->depth - (m_layer_info-1)->toolchanges_depth())
                                      - ((m_layer_info)->depth-(m_layer_info)->toolchanges_depth()) ;
        else
            sparse_beginning_y += (m_layer_info-1)->toolchanges_depth() + m_perimeter_width;

        float sum_of_depths = 0.f;
        for (const auto& tch : m_layer_info->tool_changes) {  // let's find this toolchange
            if (tch.old_tool == m_current_tool) {
                sum_of_depths += tch.ramming_depth;
                float ramming_end_y = sum_of_depths;
                ramming_end_y -= (y_step/m_extra_spacing-m_perimeter_width) / 2.f;   // center of final ramming line

                if ( (m_current_shape == SHAPE_REVERSED   && ramming_end_y < sparse_beginning_y - 0.5f*m_perimeter_width  ) ||
                     (m_current_shape == SHAPE_NORMAL && ramming_end_y > sparse_beginning_y + 0.5f*m_perimeter_width  )  )
                {
                    writer.extrude(xl + tch.first_wipe_line-1.f*m_perimeter_width,writer.y());
                    remaining -= tch.first_wipe_line-1.f*m_perimeter_width;
                }
                break;
            }
            sum_of_depths += tch.required_depth;
        }
    }

    writer.disable_linear_advance();

    // now the ramming itself:
    while (i < m_filpar[m_current_tool].ramming_speed.size())
    {
        const float x = volume_to_length(m_filpar[m_current_tool].ramming_speed[i] * 0.25f, line_width, m_layer_height);
        const float e = m_filpar[m_current_tool].ramming_speed[i] * 0.25f / filament_area(); // transform volume per sec to E move;
        const float dist = std::min(x - e_done, remaining);		  // distance to travel for either the next 0.25s, or to the next turnaround
        const float actual_time = dist/x * 0.25f;
        writer.ram(writer.x(), writer.x() + (m_left_to_right ? 1.f : -1.f) * dist, 0.f, 0.f, e * (dist / x), dist / (actual_time / 60.f));
        remaining -= dist;

		if (remaining < WT_EPSILON)	{ // we reached a turning point
			writer.travel(writer.x(), writer.y() + y_step, 7200);
			m_left_to_right = !m_left_to_right;
			remaining = xr - xl;
		}
		e_done += dist; // subtract what was actually done
		if (e_done > x - WT_EPSILON) { // current segment finished
			++i;
			e_done = 0;
		}
	}
	Vec2f end_of_ramming(writer.x(),writer.y());
    writer.change_analyzer_line_width(m_perimeter_width);   // so the next lines are not affected by ramming_line_width_multiplier

    // Retraction:
    float old_x = writer.x();
    float turning_point = (!m_left_to_right ? xl : xr );
    if (m_semm && (m_cooling_tube_retraction != 0 || m_cooling_tube_length != 0)) {
        float total_retraction_distance = m_cooling_tube_retraction + m_cooling_tube_length/2.f - 15.f; // the 15mm is reserved for the first part after ramming
        writer.suppress_preview()
              .retract(15.f, m_filpar[m_current_tool].unloading_speed_start * 60.f) // feedrate 5000mm/min = 83mm/s
              .retract(0.70f * total_retraction_distance, 1.0f * m_filpar[m_current_tool].unloading_speed * 60.f)
              .retract(0.20f * total_retraction_distance, 0.5f * m_filpar[m_current_tool].unloading_speed * 60.f)
              .retract(0.10f * total_retraction_distance, 0.3f * m_filpar[m_current_tool].unloading_speed * 60.f)
              .resume_preview();
    }
    // Wipe tower should only change temperature with single extruder MM. Otherwise, all temperatures should
    // be already set and there is no need to change anything. Also, the temperature could be changed
    // for wrong extruder.
    if (m_semm) {
        if (new_temperature != 0 && (new_temperature != m_old_temperature || is_first_layer()) ) { 	// Set the extruder temperature, but don't wait.
            // If the required temperature is the same as last time, don't emit the M104 again (if user adjusted the value, it would be reset)
            // However, always change temperatures on the first layer (this is to avoid issues with priming lines turned off).
            writer.set_extruder_temp(new_temperature, false);
            m_old_temperature = new_temperature;
        }
    }

    // Cooling:
    const int& number_of_moves = m_filpar[m_current_tool].cooling_moves;
    if (number_of_moves > 0) {
        const float& initial_speed = m_filpar[m_current_tool].cooling_initial_speed;
        const float& final_speed   = m_filpar[m_current_tool].cooling_final_speed;

        float speed_inc = (final_speed - initial_speed) / (2.f * number_of_moves - 1.f);

        writer.suppress_preview()
              .travel(writer.x(), writer.y() + y_step);
        old_x = writer.x();
        turning_point = xr-old_x > old_x-xl ? xr : xl;
        for (int i=0; i<number_of_moves; ++i) {
            float speed = initial_speed + speed_inc * 2*i;
            writer.load_move_x_advanced(turning_point, m_cooling_tube_length, speed);
            speed += speed_inc;
            writer.load_move_x_advanced(old_x, -m_cooling_tube_length, speed);
        }
    }

    // let's wait is necessary:
    writer.wait(m_filpar[m_current_tool].delay);
    // we should be at the beginning of the cooling tube again - let's move to parking position:
    writer.retract(-m_cooling_tube_length/2.f+m_parking_pos_retraction-m_cooling_tube_retraction, 2000);

	// this is to align ramming and future wiping extrusions, so the future y-steps can be uniform from the start:
    // the perimeter_width will later be subtracted, it is there to not load while moving over just extruded material
	writer.travel(end_of_ramming.x(), end_of_ramming.y() + (y_step/m_extra_spacing-m_perimeter_width) / 2.f + m_perimeter_width, 2400.f);

	writer.resume_preview()
		  .flush_planner_queue();
#endif
}

// Change the tool, set a speed override for soluble and flex materials.
void WipeTower::toolchange_Change(
	WipeTowerWriter &writer,
    const size_t 	new_tool,
    const std::string&  new_material)
{
    // Ask the writer about how much of the old filament we consumed:
    if (m_current_tool < m_used_filament_length.size())
    	m_used_filament_length[m_current_tool] += writer.get_and_reset_used_filament_length();

    // This is where we want to place the custom gcodes. We will use placeholders for this.
    // These will be substituted by the actual gcodes when the gcode is generated.
    writer.append("[filament_end_gcode]\n");
    writer.append("[change_filament_gcode]\n");

    // BBS: do travel in GCode::append_tcr() for lazy_lift
#if 0
    // Travel to where we assume we are. Custom toolchange or some special T code handling (parking extruder etc)
    // gcode could have left the extruder somewhere, we cannot just start extruding. We should also inform the
    // postprocessor that we absolutely want to have this in the gcode, even if it thought it is the same as before.
    Vec2f current_pos = writer.pos_rotated();
    writer.feedrate(m_travel_speed * 60.f)
          .append(std::string("G1 X") + Slic3r::float_to_string_decimal_point(current_pos.x())
                             +  " Y"  + Slic3r::float_to_string_decimal_point(current_pos.y())
                             + never_skip_tag() + "\n");
#endif

    // The toolchange Tn command will be inserted later, only in case that the user does
    // not provide a custom toolchange gcode.
	writer.set_tool(new_tool); // This outputs nothing, the writer just needs to know the tool has changed.
    writer.append("[filament_start_gcode]\n");

	writer.flush_planner_queue();
	m_current_tool = new_tool;
	m_extrusion_flow = extrusion_flow(m_layer_height);
}

void WipeTower::toolchange_Load(
	WipeTowerWriter &writer,
	const box_coordinates  &cleaning_box)
{
    // BBS: tool load is done in change_filament_gcode
#if 0
    if (m_semm && (m_parking_pos_retraction != 0 || m_extra_loading_move != 0)) {
        float xl = cleaning_box.ld.x() + m_perimeter_width * 0.75f;
        float xr = cleaning_box.rd.x() - m_perimeter_width * 0.75f;
        float oldx = writer.x();	// the nozzle is in place to do the first wiping moves, we will remember the position

        // Load the filament while moving left / right, so the excess material will not create a blob at a single position.
        float turning_point = ( oldx-xl < xr-oldx ? xr : xl );
        float edist = m_parking_pos_retraction+m_extra_loading_move;

        writer.append("; CP TOOLCHANGE LOAD\n")
              .suppress_preview()
              .load(0.2f * edist, 60.f * m_filpar[m_current_tool].loading_speed_start)
              .load_move_x_advanced(turning_point, 0.7f * edist,        m_filpar[m_current_tool].loading_speed)  // Fast phase
              .load_move_x_advanced(oldx,          0.1f * edist, 0.1f * m_filpar[m_current_tool].loading_speed)  // Super slow*/

              .travel(oldx, writer.y()) // in case last move was shortened to limit x feedrate
              .resume_preview();

        // Reset the extruder current to the normal value.
        if (m_set_extruder_trimpot)
            writer.set_extruder_trimpot(550);
    }
#endif
}

// Wipe the newly loaded filament until the end of the assigned wipe area.
void WipeTower::toolchange_Wipe(
	WipeTowerWriter &writer,
	const box_coordinates  &cleaning_box,
	float wipe_length)
{
	// Increase flow on first layer, slow down print.
    writer.set_extrusion_flow(m_extrusion_flow * (is_first_layer() ? m_first_layer_flow_ratio : 1.f))
		  .append("; CP TOOLCHANGE WIPE\n");

    // BBS: add the note for gcode-check, when the flow changed, the width should follow the change
    if (is_first_layer()) {
        writer.append(";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Width) + std::to_string(m_first_layer_flow_ratio * m_perimeter_width) + "\n");
    }

	const float& xl = cleaning_box.ld.x();
	const float& xr = cleaning_box.rd.x();

	// Variables x_to_wipe and traversed_x are here to be able to make sure it always wipes at least
    //   the ordered volume, even if it means violating the box. This can later be removed and simply
    // wipe until the end of the assigned area.

    float x_to_wipe = wipe_length;
    float dy = m_layer_info->extra_spacing * m_perimeter_width;

    const float target_speed = is_first_layer() ? std::min(m_first_layer_speed * 60.f, 4800.f) : 4800.f;
    float wipe_speed = 0.33f * target_speed;

    float start_y = writer.y();

#if 0
    // if there is less than 2.5*m_perimeter_width to the edge, advance straightaway (there is likely a blob anyway)
    if ((m_left_to_right ? xr-writer.x() : writer.x()-xl) < 2.5f*m_perimeter_width) {
        writer.travel((m_left_to_right ? xr-m_perimeter_width : xl+m_perimeter_width),writer.y()+dy);
        m_left_to_right = !m_left_to_right;
    }
#endif

    m_left_to_right = ((m_cur_layer_id + 3) % 4 >= 2);
    bool is_from_up = (m_cur_layer_id % 2 == 1);

    // BBS: do not need to move dy
#if 0
    if (m_depth_traversed != 0)
        writer.travel(xl, writer.y() + dy);
#endif

    bool need_change_flow = false;
    // now the wiping itself:
	for (int i = 0; true; ++i)	{
		if (i!=0) {
            if      (wipe_speed < 0.34f * target_speed) wipe_speed = 0.375f * target_speed;
            else if (wipe_speed < 0.377 * target_speed) wipe_speed = 0.458f * target_speed;
            else if (wipe_speed < 0.46f * target_speed) wipe_speed = 0.875f * target_speed;
            else wipe_speed = std::min(target_speed, wipe_speed + 50.f);
		}

        // BBS: check the bridging area and use the bridge flow
        if (need_change_flow || need_thick_bridge_flow(writer.y(), m_layer_height)) {
            writer.set_extrusion_flow(extrusion_flow(0.2));
            writer.append(";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Height) + std::to_string(0.2) + "\n");
            need_change_flow = true;
        }

        if (m_left_to_right)
            writer.extrude(xr + wipe_tower_wall_infill_overlap * m_perimeter_width, writer.y(), wipe_speed);
        else
            writer.extrude(xl - wipe_tower_wall_infill_overlap * m_perimeter_width, writer.y(), wipe_speed);

        // BBS: recover the flow in non-bridging area
        if (need_change_flow) {
            writer.set_extrusion_flow(m_extrusion_flow);
            writer.append(";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Height) + std::to_string(m_layer_height) + "\n");
        }

        if (!is_from_up && (writer.y() - float(EPSILON) > cleaning_box.lu.y()))
            break;		// in case next line would not fit

        if (is_from_up && (writer.y() + float(EPSILON) < cleaning_box.ld.y()))
            break;

        x_to_wipe -= (xr - xl);
		if (x_to_wipe < WT_EPSILON) {
            // BBS: Delete some unnecessary travel
            //writer.travel(m_left_to_right ? xl + 1.5f*m_perimeter_width : xr - 1.5f*m_perimeter_width, writer.y(), 7200);
			break;
		}
		// stepping to the next line:
        if (is_from_up)
            writer.extrude(writer.x(), writer.y() - dy);
        else
            writer.extrude(writer.x(), writer.y() + dy);

		m_left_to_right = !m_left_to_right;
	}

    float end_y = writer.y();

    // We may be going back to the model - wipe the nozzle. If this is followed
    // by finish_layer, this wipe path will be overwritten.
    //writer.add_wipe_point(writer.x(), writer.y())
    //      .add_wipe_point(writer.x(), writer.y() - dy)
    //      .add_wipe_point(! m_left_to_right ? m_wipe_tower_width : 0.f, writer.y() - dy);
    // BBS: modify the wipe_path after toolchange
    writer.add_wipe_point(writer.x(), writer.y())
          .add_wipe_point(! m_left_to_right ? m_wipe_tower_width : 0.f, writer.y());

    if (m_layer_info != m_plan.end() && m_current_tool != m_layer_info->tool_changes.back().new_tool)
        m_left_to_right = !m_left_to_right;

    writer.set_extrusion_flow(m_extrusion_flow); // Reset the extrusion flow.
    // BBS: add the note for gcode-check when the flow changed
    if (is_first_layer()) {
        writer.append(";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Width) + std::to_string(m_perimeter_width) + "\n");
    }
}



// BBS
WipeTower::box_coordinates WipeTower::align_perimeter(const WipeTower::box_coordinates& perimeter_box)
{
    box_coordinates aligned_box = perimeter_box;

    float spacing = m_extra_spacing * m_perimeter_width;
    float up = perimeter_box.lu(1) - m_perimeter_width - EPSILON;
    up = align_ceil(up, spacing);
    up += m_perimeter_width;
    up = std::min(up, m_wipe_tower_depth);

    float down = perimeter_box.ld(1) - m_perimeter_width + EPSILON;
    down = align_floor(down, spacing);
    down += m_perimeter_width;
    down = std::max(down, -m_y_shift);

    aligned_box.lu(1) = aligned_box.ru(1) = up;
    aligned_box.ld(1) = aligned_box.rd(1) = down;

    return aligned_box;
}

void WipeTower::set_for_wipe_tower_writer(WipeTowerWriter &writer)
{
    writer.set_normal_acceleration(m_normal_accels);
    writer.set_travel_acceleration(m_travel_accels);
    writer.set_first_layer_normal_acceleration(m_first_layer_normal_accels);
    writer.set_first_layer_travel_acceleration(m_first_layer_travel_accels);
    writer.set_max_acceleration(m_max_accels);
    writer.set_multi_nozzle_group_result(m_multi_nozzle_group_result);
    writer.set_accel_to_decel_enable(m_accel_to_decel_enable);
    writer.set_accel_to_decel_factor(m_accel_to_decel_factor);
    writer.set_first_layer(is_first_layer());
    writer.set_layer_id(m_cur_layer_id);
    writer.set_handle_hotend_as_extruder(m_handle_hotend_as_extruder);
    writer.set_physical_extruder_map(m_physical_extruder_map);
}
#if 0
WipeTower::ToolChangeResult WipeTower::finish_layer(bool extrude_perimeter, bool extruder_fill)
{
	assert(! this->layer_finished());
    m_current_layer_finished = true;

    size_t old_tool = m_current_tool;

	WipeTowerWriter writer(m_layer_height, m_perimeter_width, m_gcode_flavor, m_filpar, m_enable_arc_fitting);
	writer.set_extrusion_flow(m_extrusion_flow)
		.set_z(m_z_pos)
		.set_initial_tool(m_current_tool)
        .set_y_shift(m_y_shift - (m_current_shape == SHAPE_REVERSED ? m_layer_info->toolchanges_depth() : 0.f));

    set_for_wipe_tower_writer(writer);

    writer.append(";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Wipe_Tower_Start) + "\n");

	// Slow down on the 1st layer.
    bool first_layer = is_first_layer();
    // BBS: speed up perimeter speed to 90mm/s for non-first layer
    float           feedrate   = first_layer ? std::min(m_first_layer_speed * 60.f, 5400.f) : std::min(60.0f * m_filpar[m_current_tool].max_e_speed / m_extrusion_flow, 5400.f);
    float fill_box_y = m_layer_info->toolchanges_depth() + m_perimeter_width;
    box_coordinates fill_box(Vec2f(m_perimeter_width, fill_box_y),
                             m_wipe_tower_width - 2 * m_perimeter_width, m_layer_info->depth - fill_box_y);

    writer.set_initial_position((m_left_to_right ? fill_box.ru : fill_box.lu), // so there is never a diagonal travel
                                 m_wipe_tower_width, m_wipe_tower_depth, m_internal_rotation);

    bool toolchanges_on_layer = m_layer_info->toolchanges_depth() > WT_EPSILON;

    // inner perimeter of the sparse section, if there is space for it:
    if (fill_box.ru.y() - fill_box.rd.y() > m_perimeter_width - WT_EPSILON)
        writer.rectangle_fill_box(this, fill_box.ld, fill_box.rd.x() - fill_box.ld.x(), fill_box.ru.y() - fill_box.rd.y(), feedrate);

    // we are in one of the corners, travel to ld along the perimeter:
    // BBS: Delete some unnecessary travel
    //if (writer.x() > fill_box.ld.x() + EPSILON) writer.travel(fill_box.ld.x(), writer.y());
    //if (writer.y() > fill_box.ld.y() + EPSILON) writer.travel(writer.x(), fill_box.ld.y());

    // Extrude infill to support the material to be printed above.
    const float dy = (fill_box.lu.y() - fill_box.ld.y() - m_perimeter_width);
    float left = fill_box.lu.x() + 2*m_perimeter_width;
    float right = fill_box.ru.x() - 2 * m_perimeter_width;
    std::vector<Vec2f> finish_rect_wipe_path;
    if (extruder_fill && dy > m_perimeter_width)
    {
        writer.travel(fill_box.ld + Vec2f(m_perimeter_width * 2, 0.f))
              .append(";--------------------\n"
                      "; CP EMPTY GRID START\n")
              .comment_with_value(" layer #", m_num_layer_changes + 1);

        // Is there a soluble filament wiped/rammed at the next layer?
        // If so, the infill should not be sparse.
        bool solid_infill = m_layer_info+1 == m_plan.end()
                          ? false
                          : std::any_of((m_layer_info+1)->tool_changes.begin(),
                                        (m_layer_info+1)->tool_changes.end(),
                                   [this](const WipeTowerInfo::ToolChange& tch) {
                                       return m_filpar[tch.new_tool].is_soluble
                                           || m_filpar[tch.old_tool].is_soluble;
                                   });
        solid_infill |= first_layer && m_adhesion;

        if (solid_infill) {
            float sparse_factor = 1.5f; // 1=solid, 2=every other line, etc.
            if (first_layer) { // the infill should touch perimeters
                left  -= m_perimeter_width;
                right += m_perimeter_width;
                sparse_factor = 1.f;
            }
            float y = fill_box.ld.y() + m_perimeter_width;
            int n = dy / (m_perimeter_width * sparse_factor);
            float spacing = (dy-m_perimeter_width)/(n-1);
            int i=0;
            for (i=0; i<n; ++i) {
                writer.extrude(writer.x(), y, feedrate)
                      .extrude(i%2 ? left : right, y);
                y = y + spacing;
            }
            writer.extrude(writer.x(), fill_box.lu.y());
        } else {
            // Extrude an inverse U at the left of the region and the sparse infill.
            writer.extrude(fill_box.lu + Vec2f(m_perimeter_width * 2, 0.f), feedrate);

            const int n = 1+int((right-left)/m_bridging);
            const float dx = (right-left)/n;
            for (int i=1;i<=n;++i) {
                float x=left+dx*i;
                writer.travel(x,writer.y());
                writer.extrude(x,i%2 ? fill_box.rd.y() : fill_box.ru.y());
            }
            // BBS: add wipe_path for this case: only with finish rectangle
            finish_rect_wipe_path.emplace_back(writer.pos());
            finish_rect_wipe_path.emplace_back(Vec2f(left + dx * n, n % 2 ? fill_box.ru.y() : fill_box.rd.y()));
        }

        writer.append("; CP EMPTY GRID END\n"
                      ";------------------\n\n\n\n\n\n\n");
    }

    // outer perimeter (always):
    // BBS
    float wipe_tower_depth = m_layer_info->depth + m_perimeter_width;
    if (keeps_outer_walls_unshrunk())
        wipe_tower_depth = m_wipe_tower_depth;
    box_coordinates wt_box(Vec2f(0.f, (m_current_shape == SHAPE_REVERSED ? m_layer_info->toolchanges_depth() : 0.f)),
        m_wipe_tower_width, wipe_tower_depth);
    wt_box = align_perimeter(wt_box);
    if (extrude_perimeter) {
        writer.rectangle(wt_box, feedrate);
    }

    // brim chamfer
    float spacing = m_perimeter_width - m_layer_height * float(1. - M_PI_4);
    // How many perimeters shall the brim have?
    int loops_num = (m_wipe_tower_brim_width + spacing / 2.f) / spacing;
    const float max_chamfer_width = 3.f;
    if (!first_layer) {
        // stop print chamfer if depth changes
        if (m_layer_info->depth != m_plan.front().depth) {
            loops_num = 0;
        }
        else {
            // limit max chamfer width to 3 mm
            int chamfer_loops_num = (int)(max_chamfer_width / spacing);
            int dist_to_1st = int(m_layer_info - m_plan.begin()) - first_printed_layer_idx();
            loops_num = std::min(loops_num, chamfer_loops_num) - dist_to_1st;
        }
    }

    if (loops_num > 0) {
        box_coordinates box = wt_box;
        for (size_t i = 0; i < loops_num; ++i) {
            box.expand(spacing);
            writer.rectangle(box, feedrate);
        }

        if (first_layer) {
            // Save actual brim width to be later passed to the Print object, which will use it
            // for skirt calculation and pass it to GLCanvas for precise preview box
            m_wipe_tower_brim_width_real = wt_box.ld.x() - box.ld.x() + spacing / 2.f;
        }
        wt_box = box;
    }

    // Now prepare future wipe. box contains rectangle that was extruded last (ccw).
    Vec2f target = (writer.pos() == wt_box.ld ? wt_box.rd :
                   (writer.pos() == wt_box.rd ? wt_box.ru :
                   (writer.pos() == wt_box.ru ? wt_box.lu :
                    wt_box.ld)));

    // BBS: add wipe_path for this case: only with finish rectangle
    if (finish_rect_wipe_path.size() == 2 && finish_rect_wipe_path[0] == writer.pos())
        target = finish_rect_wipe_path[1];

    writer.add_wipe_point(writer.pos())
          .add_wipe_point(target);

    writer.append(";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Wipe_Tower_End) + "\n");

    // Ask our writer about how much material was consumed.
    // Skip this in case the layer is sparse and config option to not print sparse layers is enabled.
    if (! m_sparse_layers_skipped || toolchanges_on_layer)
        if (m_current_tool < m_used_filament_length.size())
            m_used_filament_length[m_current_tool] += writer.get_and_reset_used_filament_length();

    return construct_tcr(writer, false, old_tool, true, false, 0.f,false);
}
#endif
WipeTower::WipeTowerInfo::ToolChange WipeTower::set_toolchange(int old_tool, int new_tool, float layer_height, float wipe_volume, float purge_volume,int layer_id)
{
    const float new_tool_width         = filament_perimeter_width(new_tool);
    const float old_tool_nc_width      = filament_nozzle_change_width(old_tool);
    // The wipe and the ramming live in different bands and may therefore sit on slabs of different heights.
    const float wipe_height            = slab_height_for(new_tool, layer_id, layer_height);
    const float ramming_height         = slab_height_for(old_tool, layer_id, layer_height);
    float depth             = 0.f;
    float width             = m_wipe_tower_width - 2 * new_tool_width;
    float                             nozzle_change_width     = m_wipe_tower_width - (old_tool_nc_width + new_tool_width);
    float length_to_extrude = volume_to_length(wipe_volume, new_tool_width, wipe_height);
    float                             toolchange_gap_width    = get_block_gap_width(new_tool,false);
    float                             nozzlechange_gap_width  = get_block_gap_width(old_tool,true);
    float filament_change_length = !is_same_extruder(old_tool, new_tool, layer_id) ? m_filaments_change_length.first[old_tool] : m_filaments_change_length.second[old_tool];
    depth += std::ceil(length_to_extrude / width) * toolchange_gap_width;
    // depth *= m_extra_spacing;

    float nozzle_change_depth  = 0;
    float nozzle_change_length = 0;
    if (is_need_ramming(old_tool, new_tool, layer_id)) {
        double e_flow                   = ramming_height * (old_tool_nc_width - ramming_height * (1.f - float(M_PI) / 4.f)) / m_filpar[old_tool].filament_area;
        double length                   = filament_change_length / e_flow;
        int    nozzle_change_line_count = std::ceil(length / nozzle_change_width);
        nozzle_change_depth             = nozzle_change_line_count * nozzlechange_gap_width;
        depth += nozzle_change_depth;
        nozzle_change_length = length;
    }
    WipeTowerInfo::ToolChange tool_change = WipeTowerInfo::ToolChange(old_tool, new_tool, depth, 0.f, 0.f, wipe_volume, length_to_extrude, purge_volume);
    tool_change.nozzle_change_depth       = nozzle_change_depth;
    tool_change.nozzle_change_length      = nozzle_change_length;
    return tool_change;
}

// Appends a toolchange into m_plan and calculates neccessary depth of the corresponding box
void WipeTower::plan_toolchange(float z_par, float layer_height_par, unsigned int old_tool,
                                unsigned int new_tool, float wipe_volume_ec,float wipe_volume_nc,float purge_volume)
{
	assert(m_plan.empty() || m_plan.back().z <= z_par + WT_EPSILON);	// refuses to add a layer below the last one

	if (m_plan.empty() || m_plan.back().z + WT_EPSILON < z_par) // if we moved to a new layer, we'll add it to m_plan first
		m_plan.push_back(WipeTowerInfo(z_par, layer_height_par));

    if (m_first_layer_idx == size_t(-1) && (! m_sparse_layers_skipped || old_tool != new_tool))
        m_first_layer_idx = m_plan.size() - 1;

    if (old_tool == new_tool)	// new layer without toolchanges - we are done
        return;

	// this is an actual toolchange - let's calculate depth to reserve on the wipe tower
    float depth = 0.f;
    const float new_tool_width    = filament_perimeter_width(int(new_tool));
    const float old_tool_nc_width = filament_nozzle_change_width(int(old_tool));
    float width = m_wipe_tower_width - 2 * new_tool_width;

    // BBS: if the wipe tower width is too small, the depth will be infinity
    if (width <= EPSILON)
        return;
    int   layer_id    = static_cast<int>(m_plan.size()) - 1;
    float wipe_volume = is_same_extruder(old_tool, new_tool, layer_id) && !is_same_nozzle(old_tool, new_tool, layer_id) ? wipe_volume_nc : wipe_volume_ec;
    // BBS: remove old filament ramming and first line
#if 0
	float length_to_extrude = volume_to_length(0.25f * std::accumulate(m_filpar[old_tool].ramming_speed.begin(), m_filpar[old_tool].ramming_speed.end(), 0.f),
										m_perimeter_width * m_filpar[old_tool].ramming_line_width_multiplicator,
										layer_height_par);
	depth = (int(length_to_extrude / width) + 1) * (m_perimeter_width * m_filpar[old_tool].ramming_line_width_multiplicator * m_filpar[old_tool].ramming_step_multiplicator);
    float ramming_depth = depth;
    length_to_extrude = width*((length_to_extrude / width)-int(length_to_extrude / width)) - width;
    float first_wipe_line = -length_to_extrude;
    length_to_extrude += volume_to_length(wipe_volume, m_perimeter_width, layer_height_par);
    length_to_extrude = std::max(length_to_extrude,0.f);

    depth += (int(length_to_extrude / width) + 1) * m_perimeter_width;
    depth *= m_extra_spacing;

    m_plan.back().tool_changes.push_back(WipeTowerInfo::ToolChange(old_tool, new_tool, depth, ramming_depth, first_wipe_line, wipe_volume));
#else
    float length_to_extrude = volume_to_length(wipe_volume, new_tool_width, layer_height_par);

    depth += std::ceil(length_to_extrude / width) * new_tool_width;
    //depth *= m_extra_spacing;
    float filament_change_length = !is_same_extruder(old_tool, new_tool, layer_id) ? m_filaments_change_length.first[old_tool] : m_filaments_change_length.second[old_tool];
    float nozzle_change_depth = 0;
    float nozzle_change_length = 0;
    if (is_need_ramming(old_tool, new_tool, layer_id)) {
        double e_flow                   = layer_height_par * (old_tool_nc_width - layer_height_par * (1.f - float(M_PI) / 4.f)) / m_filpar[old_tool].filament_area;
        double length                   = filament_change_length / e_flow;
        int    nozzle_change_line_count = std::ceil(length / (m_wipe_tower_width - 2*old_tool_nc_width));
        nozzle_change_depth = nozzle_change_line_count * old_tool_nc_width;
        depth += nozzle_change_depth;
        nozzle_change_length = length;
    }
    WipeTowerInfo::ToolChange tool_change = WipeTowerInfo::ToolChange(old_tool, new_tool, depth, 0.f, 0.f, wipe_volume, length_to_extrude, purge_volume);
    tool_change.nozzle_change_depth       = nozzle_change_depth;
    tool_change.nozzle_change_length      = nozzle_change_length;
    m_plan.back().tool_changes.push_back(tool_change);
#endif
}
#if 0
void WipeTower::plan_tower()
{
    // BBS
    // calculate extra spacing
    float max_depth = 0.f;
    for (auto& info : m_plan)
        max_depth = std::max(max_depth, info.toolchanges_depth());

    float min_wipe_tower_depth = WipeTower::get_limit_depth_by_height(m_wipe_tower_height);

    {
        if (m_enable_wrapping_detection && max_depth < EPSILON)
            max_depth = wrapping_wipe_tower_depth;

        if (m_enable_timelapse_print && max_depth < EPSILON)
            max_depth = min_wipe_tower_depth;

        if (max_depth + EPSILON < min_wipe_tower_depth && !has_tpu_filament())
            m_extra_spacing = min_wipe_tower_depth / max_depth;
        else
            m_extra_spacing = 1.f;

        for (int idx = 0; idx < m_plan.size(); idx++) {
            auto& info = m_plan[idx];
            if (idx == 0 && m_extra_spacing > 1.f + EPSILON) {
                // apply solid fill for the first layer
                info.extra_spacing = 1.f;
                for (auto& toolchange : info.tool_changes) {
                    float x_to_wipe = volume_to_length(toolchange.wipe_volume, m_perimeter_width, info.height);
                    float line_len = m_wipe_tower_width - 2 * m_perimeter_width;
                    float x_to_wipe_new = x_to_wipe * m_extra_spacing;
                    x_to_wipe_new = std::floor(x_to_wipe_new / line_len) * line_len;
                    x_to_wipe_new = std::max(x_to_wipe_new, x_to_wipe);

                    int line_count = std::ceil((x_to_wipe_new - WT_EPSILON) / line_len);

                    {  // nozzle change length
                        int nozzle_change_line_count = (toolchange.nozzle_change_depth + WT_EPSILON) / m_perimeter_width;
                        line_count += nozzle_change_line_count;
                    }

                    toolchange.required_depth = line_count * m_perimeter_width;
                    toolchange.wipe_volume = x_to_wipe_new / x_to_wipe * toolchange.wipe_volume;
                    toolchange.wipe_length = x_to_wipe_new;
                }
            }
            else {
                info.extra_spacing = m_extra_spacing;
                for (auto& toolchange : info.tool_changes) {
                    toolchange.required_depth *= m_extra_spacing;
                    toolchange.wipe_length = volume_to_length(toolchange.wipe_volume, m_perimeter_width, info.height);
                }
            }
        }
    }

	// Calculate m_wipe_tower_depth (maximum depth for all the layers) and propagate depths downwards
	m_wipe_tower_depth = 0.f;
	for (auto& layer : m_plan)
		layer.depth = 0.f;

    float max_depth_for_all = 0;
    for (int layer_index = int(m_plan.size()) - 1; layer_index >= 0; --layer_index)
	{
        float this_layer_depth = std::max(m_plan[layer_index].depth, m_plan[layer_index].toolchanges_depth());
        if (m_enable_wrapping_detection && (layer_index < m_wrapping_detection_layers) && this_layer_depth < EPSILON)
            this_layer_depth = wrapping_wipe_tower_depth;

        if (m_enable_timelapse_print && this_layer_depth < EPSILON)
            this_layer_depth = min_wipe_tower_depth;

		m_plan[layer_index].depth = this_layer_depth;

		if (this_layer_depth > m_wipe_tower_depth - m_perimeter_width)
			m_wipe_tower_depth = this_layer_depth + m_perimeter_width;

		for (int i = layer_index - 1; i >= 0 ; i--)
		{
			if (m_plan[i].depth - this_layer_depth < 2*m_perimeter_width )
				m_plan[i].depth = this_layer_depth;
		}

        if (m_enable_timelapse_print && layer_index == 0)
            max_depth_for_all = m_plan[0].depth;
    }

    if (m_enable_wrapping_detection) {
        for (int i = m_wrapping_detection_layers - 1; i >= 0; i--) {
            if (m_plan.size() <= m_wrapping_detection_layers && (m_plan[i].depth < wrapping_wipe_tower_depth)) {
                m_plan[i].depth = wrapping_wipe_tower_depth;
            }
        }
    }

    if (m_enable_timelapse_print) {
        for (int i = int(m_plan.size()) - 1; i >= 0; i--) {
            m_plan[i].depth = max_depth_for_all;
        }
    }

    // Side scrape: pin every layer to the tallest per-layer depth so the outer walls never step inward
    // (the scrape descends outside them).
    if (keeps_outer_walls_unshrunk()) {
        float max_d = 0.f;
        for (const WipeTowerInfo &layer : m_plan)
            max_d = std::max(max_d, layer.depth);
        for (WipeTowerInfo &layer : m_plan)
            layer.depth = max_d;
    }
}

void WipeTower::save_on_last_wipe()
{
    for (m_layer_info=m_plan.begin();m_layer_info<m_plan.end();++m_layer_info) {
        set_layer(m_layer_info->z, m_layer_info->height, 0, m_layer_info->z == m_plan.front().z, m_layer_info->z == m_plan.back().z);
        if (m_layer_info->tool_changes.size()==0)   // we have no way to save anything on an empty layer
            continue;

        // Which toolchange will finish_layer extrusions be subtracted from?
        // BBS: consider both soluable and support properties
        int idx = first_toolchange_to_nonsoluble_nonsupport(m_layer_info->tool_changes);

        for (int i=0; i<int(m_layer_info->tool_changes.size()); ++i) {
            auto& toolchange = m_layer_info->tool_changes[i];
            tool_change(toolchange.new_tool);

            if (i == idx) {
                float width = m_wipe_tower_width - 3*m_perimeter_width; // width we draw into
                float length_to_save = finish_layer().total_extrusion_length_in_plane();
                float length_to_wipe = volume_to_length(toolchange.wipe_volume,
                                      m_perimeter_width, m_layer_info->height)  - toolchange.first_wipe_line - length_to_save;

                length_to_wipe = std::max(length_to_wipe,0.f);
                float depth_to_wipe = m_perimeter_width * (std::floor(length_to_wipe/width) + ( length_to_wipe > 0.f ? 1.f : 0.f ) ) * m_extra_spacing;

                toolchange.required_depth = toolchange.ramming_depth + depth_to_wipe;
            }
        }
    }
}
#endif
bool WipeTower::is_tpu_filament(int filament_id) const
{
    return m_filpar[filament_id].material == "TPU";
}

bool WipeTower::is_petg_filament(int filament_id) const
{
    return m_filpar[filament_id].material == "PETG";
}

bool WipeTower::is_need_reverse_travel(int filament_id,bool extruder_change) const
{
    if (extruder_change)
        return m_filpar[filament_id].ramming_travel_time.first > EPSILON && m_filaments_change_length.first[filament_id]>EPSILON;
    return m_filpar[filament_id].ramming_travel_time.second > EPSILON && m_filaments_change_length.second[filament_id] > EPSILON;
}

// BBS: consider both soluable and support properties
// Return index of first toolchange that switches to non-soluble and non-support extruder
// ot -1 if there is no such toolchange.
int WipeTower::first_toolchange_to_nonsoluble_nonsupport(
        const std::vector<WipeTowerInfo::ToolChange>& tool_changes) const
{
    for (size_t idx=0; idx<tool_changes.size(); ++idx)
        if (! m_filpar[tool_changes[idx].new_tool].is_soluble && ! m_filpar[tool_changes[idx].new_tool].is_support)
            return idx;
    return -1;
}

WipeTower::ToolChangeResult WipeTower::merge_tcr(ToolChangeResult &first, ToolChangeResult &second)
{
    assert(first.new_tool == second.initial_tool);
    if (first.new_tool != second.initial_tool) {
        std::ostringstream message;
        message << "WipeTower: cannot merge discontinuous tool-change results"
                << ", first=" << first.initial_tool << "->" << first.new_tool
                << ", second=" << second.initial_tool << "->" << second.new_tool
                << ", layer=" << m_cur_layer_id;
        BOOST_LOG_TRIVIAL(error) << message.str();
        throw Slic3r::InvalidArgument(message.str());
    }
    WipeTower::ToolChangeResult out = first;
    if ((first.end_pos - second.start_pos).norm() > (float)EPSILON) {
        std::string travel_gcode = "G1 X" + Slic3r::float_to_string_decimal_point(second.start_pos.x(), 3) + " Y" +
                                   Slic3r::float_to_string_decimal_point(second.start_pos.y(), 3) + " F" + Slic3r::float_to_string_decimal_point(m_max_speed, 0) + "\n";
        bool need_insert_travel = true;
        if (second.is_tool_change
            && is_approx(second.start_pos.x(), second.tool_change_start_pos.x())
            && is_approx(second.start_pos.y(), second.tool_change_start_pos.y())) {
            // will insert travel in gcode.cpp
            need_insert_travel = false;
        }

        if (need_insert_travel)
            out.gcode += travel_gcode;
    }
    out.gcode += second.gcode;
    out.extrusions.insert(out.extrusions.end(), second.extrusions.begin(), second.extrusions.end());
    out.end_pos = second.end_pos;
    out.wipe_path = second.wipe_path;
    out.initial_tool = first.initial_tool;
    out.new_tool = second.new_tool;
    out.is_contact   = first.is_contact || second.is_contact;
    // Preserve the side scrape flag regardless of merge order: the [wipe_tower_deretract_gcode]
    // placeholder lives in whichever part emitted the scrape, and both gcodes are concatenated here.
    // The scrape Z data has to follow the same part, otherwise the Z placeholders resolve to the
    // default (no descent) and the scrape degenerates into a flat move.
    // At most one of the two may carry a scrape: only tool_change_new() emits one, and every caller
    // pairs a tool change with a finish_layer/finish_block result, which never scrapes. Merging two
    // scrapes would leave two [wipe_tower_deretract_gcode] placeholders in the concatenated gcode,
    // and GCode::append_tcr expands all of them, deretracting twice.
    out.side_scrape  = first.side_scrape || second.side_scrape;
    if (! first.side_scrape && second.side_scrape) {
        out.side_scrape_z_low                 = second.side_scrape_z_low;
        out.side_scrape_depth                 = second.side_scrape_depth;
        out.side_scrape_min_height_above_base = second.side_scrape_min_height_above_base;
    }
    // start_pos / tool_change_start_pos (which for the scrape case already point at the scrape start)
    // are propagated by the existing merge logic below, so both the deferred append_tcr approach and
    // the merge-inserted travel land on the scrape start.
    auto has_nozzle_change_action = [](const NozzleChangeResult &ncr) {
        return !ncr.gcode.empty() || ncr.heater_off_only || ncr.zero_flush_idle_cooling;
    };
    if (has_nozzle_change_action(first.nozzle_change_result))
        out.nozzle_change_result = first.nozzle_change_result;
    else if (has_nozzle_change_action(second.nozzle_change_result))
        out.nozzle_change_result = second.nozzle_change_result;

    if (first.is_tool_change) {
        out.is_tool_change = true;
        out.tool_change_start_pos = first.tool_change_start_pos;
    }
    else if (second.is_tool_change) {
        out.is_tool_change = true;
        out.tool_change_start_pos = second.tool_change_start_pos;
    }
    else {
        out.is_tool_change = false;
    }

    // BBS
    out.purge_volume += second.purge_volume;
    return out;
}

void WipeTower::get_all_wall_skip_points(int wall_filament) {
    m_wall_skip_points.clear();
    m_wall_skip_points.resize(m_plan.size());
    int current_tool = m_current_tool;
    for (int i = 0; i < m_plan.size(); i++) {
        if (layer_is_skipped(i) || m_plan[i].depth < m_perimeter_width)
            continue;
        const WipeTowerInfo &layer = m_plan[i];
        if (size_t(i) >= m_wall_reserved.size() || !m_wall_reserved[i]) {
            int wall_idx = get_wall_filament_for_layer(layer, i, wall_filament);
            if (wall_idx == -1 && std::any_of(m_wipe_tower_blocks.begin(), m_wipe_tower_blocks.end(),
                [i](const WipeTowerBlock &block) { return block.layers_type[i] != WipeTowerLayerType::Normal; }))
                wall_idx = current_tool;
            get_wall_skip_points(layer, i, wall_idx);
        }
        if (! layer.tool_changes.empty())
            current_tool = int(layer.tool_changes.back().new_tool);
    }
}


void WipeTower::get_wall_skip_points(const WipeTowerInfo &layer, int layer_id, int wall_filament)
{
    const int                      pre_access_layer = 4;
    std::unordered_map<int, float> cur_block_depth;
    for (int i = 0; i < int(layer.tool_changes.size()); ++i) {
        const WipeTowerInfo::ToolChange &tool_change         = layer.tool_changes[i];
        size_t                           old_filament        = tool_change.old_tool;
        size_t                           new_filament        = tool_change.new_tool;
        float                            nozzle_change_depth = tool_change.nozzle_change_depth;
        float                            wipe_depth          = tool_change.required_depth - nozzle_change_depth;
        if (!is_valid_last_layer(old_filament, layer_id, m_plan[layer_id].z)) nozzle_change_depth = 0.f;
        const int new_block_key = get_block_key(new_filament, layer_id);
        const int old_block_key = get_block_key(old_filament, layer_id);
        auto *block = get_block_by_category(new_block_key, false);
        if (!block) continue;
        float process_depth = 0.f;
        if (!cur_block_depth.count(new_block_key)) cur_block_depth[new_block_key] = block->start_depth;
        process_depth = cur_block_depth[new_block_key];
        if (is_need_ramming(new_filament, old_filament, layer_id)) {
            if (new_block_key == old_block_key)
                process_depth += nozzle_change_depth;
            else {
                if (!cur_block_depth.count(old_block_key)) {
                    auto *old_block = get_block_by_category(old_block_key, false);
                    if (!old_block) continue;
                    cur_block_depth[old_block_key] = old_block->start_depth;
                }
                cur_block_depth[old_block_key] += nozzle_change_depth;
            }
        }

        float infill_gap_width = get_block_gap_width(new_filament, false);
        Vec2f res;
        int   index = layer_id % 4;
        switch (index % 4) {
        case 0: res = Vec2f(0, process_depth); break;
        case 1: res = Vec2f(m_wipe_tower_width, process_depth + wipe_depth - m_plan[layer_id].extra_spacing * infill_gap_width); break;
        case 2: res = Vec2f(m_wipe_tower_width, process_depth); break;
        case 3: res = Vec2f(0, process_depth + wipe_depth - m_plan[layer_id].extra_spacing * infill_gap_width); break;
        default: break;
        }

        m_wall_skip_points[layer_id].emplace_back(res);

        cur_block_depth[new_block_key] = process_depth + wipe_depth;

        bool solid_toolchange = block->layers_type[layer_id] == WipeTowerLayerType::Contact;
        if (solid_toolchange && m_enable_tower_interface_features) {
            for (int j = 0; j < pre_access_layer; j++) {
                int pre_layer_id = layer_id - j;
                if (pre_layer_id < 0) break;
                m_wall_skip_points[pre_layer_id].push_back(res);
            }
        }
    }
    if (m_enable_tower_interface_features) {
        for (auto &block : m_wipe_tower_blocks) {
            // A band reserved for a merged slab above prints nothing here, so the wall needs no gap for it.
            if (layer_id < (int) block.layer_reserved.size() && block.layer_reserved[layer_id]) continue;
            float block_depth = cur_block_depth.count(block.block_key) ? cur_block_depth[block.block_key] : block.start_depth;
            if (block_depth + EPSILON >= block.start_depth + band_fill_depth(block, layer_id) - m_perimeter_width) { continue; }
            bool block_solid = block.layers_type[layer_id] == WipeTowerLayerType::Contact;
            bool add_skip_point = block_solid && std::abs(block_depth - block.start_depth) < EPSILON;
            if (add_skip_point) {
                const int fill_filament = get_block_fill_filament(layer, layer_id, block, wall_filament);
                if (fill_filament < 0)
                    continue;
                const float fill_width = filament_perimeter_width(fill_filament);
                Vec2f res;
                int   index = layer_id % 4;

                float dy_skip = band_fill_depth(block, layer_id) - fill_width;
                int   n_skip  = (int) ((dy_skip + 0.25f * fill_width) / fill_width + 1);
                float gird_depth = fill_width * (n_skip - 1); // in sync with finish_block_solid
                switch (index % 4) {
                case 0: res = Vec2f(0, block_depth); break;
                case 1: res = Vec2f(m_wipe_tower_width, block_depth + gird_depth); break;
                case 2: res = Vec2f(m_wipe_tower_width, block_depth); break;
                case 3: res = Vec2f(0, block_depth + gird_depth); break;
                default: break;
                }
                m_wall_skip_points[layer_id].emplace_back(res);
                for (int j = 0; j < pre_access_layer; j++) {
                    int pre_layer_id = layer_id - j;
                    if (pre_layer_id < 0) break;
                    m_wall_skip_points[pre_layer_id].push_back(res);
                }
            }
        }
    }
}

WipeTower::ToolChangeResult WipeTower::tool_change_new(size_t new_tool, bool solid_toolchange,bool solid_nozzlechange)
{
    m_nozzle_change_result = NozzleChangeResult();
    bool hotend_change = false;
    if (is_need_ramming(m_current_tool, new_tool, m_cur_layer_id)) {
        bool use_idle_travel = false;
        float fc_length = 0.f;
        if (m_handle_hotend_as_extruder) {
            fc_length = !is_same_extruder(m_current_tool, new_tool, m_cur_layer_id)
                ? m_filaments_change_length.first[m_current_tool]
                : m_filaments_change_length.second[m_current_tool];
            use_idle_travel = (fc_length <= EPSILON && !is_first_layer() && m_filpar[m_current_tool].idle_travel_cooling_enabled);
        }
        if (use_idle_travel) {
            if (is_valid_last_layer(m_current_tool, m_cur_layer_id, m_z_pos))
                m_nozzle_change_result = idle_travel_cooling(m_current_tool, new_tool);
        } else if (m_handle_hotend_as_extruder && fc_length <= EPSILON && !is_first_layer()) {
            if (is_valid_last_layer(m_current_tool, m_cur_layer_id, m_z_pos))
                m_nozzle_change_result = heater_off_nozzle_change(m_current_tool, new_tool);
        } else {
            hotend_change = is_same_extruder(m_current_tool, new_tool, m_cur_layer_id);
            if (is_valid_last_layer(m_current_tool, m_cur_layer_id, m_z_pos))
                m_nozzle_change_result = ramming(m_current_tool, new_tool, solid_nozzlechange, !hotend_change);
        }
    }

    size_t old_tool = m_current_tool;
    float wipe_depth          = 0.f;
    float wipe_length         = 0.f;
    float purge_volume        = 0.f;
    float nozzle_change_depth = 0.f;
    int   nozzle_change_line_count = 0;

    if (new_tool != (unsigned int) (-1)) {
        for (const auto &b : m_layer_info->tool_changes)
            if (b.new_tool == new_tool) {
                wipe_length         = b.wipe_length;
                wipe_depth          = b.required_depth;
                purge_volume        = b.purge_volume;
                nozzle_change_depth = b.nozzle_change_depth;
                break;
            }
    }
    m_current_tool        = new_tool;
    m_extrusion_flow      = extrusion_flow(m_layer_height);
    const int block_key = get_block_key(new_tool, m_cur_layer_id);
    WipeTowerBlock* block = get_block_by_category(block_key, false);
    if (!block) {
        std::ostringstream message;
        message << "WipeTower: missing block for planned tool change"
                << ", layer=" << m_cur_layer_id
                << ", planned=" << old_tool << "->" << new_tool
                << ", block_key=" << block_key;
        BOOST_LOG_TRIVIAL(error) << message.str();
        throw Slic3r::InvalidArgument(message.str());
    }
    m_current_tool = new_tool;
    m_cur_block = block;
    const float new_tool_width = filament_perimeter_width(new_tool);
    // The purge sits on the merged slab of this band, which may span several base layers.
    const float slab_height    = slab_height_for(new_tool, m_cur_layer_id, m_layer_height);
    m_extrusion_flow      = extrusion_flow_for(slab_height, new_tool_width);
    box_coordinates cleaning_box(Vec2f(new_tool_width, block->cur_depth), m_wipe_tower_width - 2 * new_tool_width, wipe_depth-nozzle_change_depth);

    WipeTowerWriter writer(slab_height, new_tool_width, m_gcode_flavor, m_filpar, m_enable_arc_fitting);
    writer.set_extrusion_flow(m_extrusion_flow)
        .set_z(m_z_pos)
        .set_initial_tool(m_current_tool)
        .set_y_shift(m_y_shift + (new_tool != (unsigned int) (-1) && (m_current_shape == SHAPE_REVERSED) ? m_layer_info->depth - m_layer_info->toolchanges_depth() : 0.f))
        .append(";--------------------\n"
                "; CP TOOLCHANGE START\n")
        .comment_with_value(" toolchange #", m_num_tool_changes + 1); // the number is zero-based

    set_for_wipe_tower_writer(writer);
    const bool first_layer = is_first_layer_for_slab(slab_height);
    writer.set_first_layer(first_layer);

    if (new_tool != (unsigned) (-1))
        writer.append( std::string("; material : " + (old_tool < m_filpar.size() ? m_filpar[old_tool].material : "(NONE)") + " -> " + m_filpar[new_tool].material + "\n").c_str())
            .append(";--------------------\n");

    writer.speed_override_backup();
    writer.speed_override(100);

    bool        did_side_scrape    = false;
    Vec2f       scrape_start_local = Vec2f(0.f, 0.f);
    SideScrapeZ scrape_z;

    // Ram the hot material out of the melt zone, retract the filament into the cooling tubes and let it cool.
    if (new_tool != (unsigned int) -1) { // This is not the last change.
        Vec2f initial_position = get_next_pos(cleaning_box, wipe_length, solid_toolchange);
        writer.set_initial_position(initial_position, m_wipe_tower_width, m_wipe_tower_depth, m_internal_rotation);

        writer.append(";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Wipe_Tower_Start) + "\n");
        toolchange_Unload(writer, cleaning_box, m_filpar[m_current_tool].material,
                          first_layer ? m_filpar[new_tool].nozzle_temperature_initial_layer : m_filpar[new_tool].nozzle_temperature);
        toolchange_Change(writer, new_tool, m_filpar[new_tool].material); // Change the tool, set a speed override for soluble and flex materials.
        // toolchange_Change() resets the flow to the tower wide default, which is derived from the finest
        // nozzle and the base layer height. The purge below has to keep the new filament's own line width
        // and the slab height of its band.
        m_extrusion_flow = extrusion_flow_for(slab_height, new_tool_width);
        toolchange_Load(writer, cleaning_box);
# if 0
        if (m_is_multi_extruder && is_need_reverse_travel(new_tool)) {
            float dy = m_layer_info->extra_spacing * m_nozzle_change_perimeter_width;
            if (m_layer_info->extra_spacing < m_tpu_fixed_spacing) {
                dy = m_tpu_fixed_spacing * m_nozzle_change_perimeter_width;
            }

            float nozzle_change_speed = 60.0f * m_filpar[new_tool].max_e_speed / m_extrusion_flow;
            nozzle_change_speed *= 0.25;

            const float &xl = cleaning_box.ld.x();
            const float &xr = cleaning_box.rd.x();

            Vec2f  start_pos         = m_nozzle_change_result.origin_start_pos + Vec2f(0, m_nozzle_change_perimeter_width);
            bool   left_to_right     = true;
            int tpu_line_count = (nozzle_change_line_count + 2 - 1) / 2; // nozzle_change_line_count / 2 round up

            writer.travel(start_pos);

            for (int i = 0; true; ++i) {
                if (left_to_right)
                    writer.travel(xr - m_perimeter_width, writer.y(), nozzle_change_speed);
                else
                    writer.travel(xl + m_perimeter_width, writer.y(), nozzle_change_speed);

                if (i == tpu_line_count - 1) break;

                writer.travel(writer.x(), writer.y() + dy);
                left_to_right = !left_to_right;
            }
            writer.travel(initial_position);
        }
#endif
        // Diagonal side scrape: after the tool change, before the wipe line, scrape the oozed
        // filament stub onto the tower side wall (travel only). Skipped on the first layer.
        if (m_enable_side_scrape && !first_layer &&
            toolchange_side_scrape(writer, cleaning_box, initial_position, &scrape_start_local, &scrape_z)) {
            // The E deretract is emitted here (after the scrape) instead of in filament_start_gcode.
            // GCode::append_tcr substitutes this placeholder based on ToolChangeResult::side_scrape.
            writer.append("[wipe_tower_deretract_gcode]\n");
            did_side_scrape = true;
        }

        toolchange_wipe_new(writer, cleaning_box, wipe_length, solid_toolchange);

        writer.append(";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Wipe_Tower_End) + "\n");
        ++m_num_tool_changes;
    } else
        toolchange_Unload(writer, cleaning_box, m_filpar[m_current_tool].material, m_filpar[m_current_tool].nozzle_temperature);

    block->cur_depth += (wipe_depth - nozzle_change_depth);
    block->last_filament_change_id = new_tool;

    // BBS
    writer.speed_override_restore();
    writer.feedrate(m_travel_speed * 60.f)
        .flush_planner_queue()
        .reset_extruder()
        .append("; CP TOOLCHANGE END\n"
                ";------------------\n"
                "\n\n");

    // Ask our writer about how much material was consumed:
    if (m_current_tool < m_used_filament_length.size())
        m_used_filament_length[m_current_tool] += writer.get_and_reset_used_filament_length();

    ToolChangeResult result = construct_tcr(writer, false, old_tool, false, true, purge_volume, solid_toolchange, slab_height);
    result.side_scrape = did_side_scrape;
    if (did_side_scrape) {
        result.side_scrape_z_low                 = scrape_z.z_low;
        result.side_scrape_depth                 = scrape_z.depth;
        result.side_scrape_min_height_above_base = scrape_z.min_height_above_base;
        // Route every travel that brings the nozzle to the tower (append_tcr approach or merge travel)
        // straight to the scrape start, so the nozzle no longer detours to the wipe line start first.
        // The scrape itself ends at the wipe line start, where the wipe line then begins as usual.
        Vec2f rotated_scrape_start   = writer.point_rotated(scrape_start_local);
        result.start_pos             = rotated_scrape_start;
        result.tool_change_start_pos = rotated_scrape_start;
    }
    return result;
}

//for extruder change and nozzle change
WipeTower::NozzleChangeResult WipeTower::ramming(int old_filament_id, int new_filament_id, bool solid_infill, bool extruder_change)
{
    auto format_line_M106 = []() { return std::string{"M106 S255\n"};};
    auto format_line_M633 = []() { return std::string{"M633\n"};};
    auto format_line_M632 = [](int filament_id, int nozzle_id) {
        std::string buffer = "M632 S" + std::to_string(filament_id);
        if (nozzle_id >= 0)
            buffer += " H" + std::to_string(nozzle_id);
        buffer += " M N\n";
        return buffer;
    };

    int   nozzle_change_line_count = 0;
    // The ramming lines are extruded by the outgoing filament, so they follow its own nozzle.
    const float nc_width           = filament_nozzle_change_width(old_filament_id);
    const float old_tool_width     = filament_perimeter_width(old_filament_id);
    float x_offset                 = old_tool_width + (nc_width - old_tool_width) / 2;
    float nozzle_change_box_width  = m_wipe_tower_width - 2 * x_offset;
    float nozzle_change_depth      = 0.f;
    if (new_filament_id != (unsigned int) (-1)) {
        for (const auto &b : m_layer_info->tool_changes)
            if (b.new_tool == new_filament_id) {
                nozzle_change_line_count = std::ceil(b.nozzle_change_length  / nozzle_change_box_width);
                nozzle_change_depth      = b.nozzle_change_depth;
                break;
            }
    }
    auto format_nozzle_change_line = [this](bool start, int old_filament_id, int new_filament_id) -> std::string {
        char        buff[64];
        std::string tag = start ? GCodeProcessor::reserved_tag(GCodeProcessor::ETags::NozzleChangeStart) : GCodeProcessor::reserved_tag(GCodeProcessor::ETags::NozzleChangeEnd);
        int         old_nozzle_id = get_nozzle_id(old_filament_id, m_cur_layer_id);
        int         new_nozzle_id = get_nozzle_id(new_filament_id, m_cur_layer_id);
        snprintf(buff, sizeof(buff), ";%s OF%d NF%d ON%d NN%d\n", tag.c_str(), old_filament_id, new_filament_id,old_nozzle_id,new_nozzle_id);
        return std::string(buff);
    };

    // The ramming lands in the outgoing filament's band, which carries its own merged slab height.
    const float ramming_slab_height = slab_height_for(old_filament_id, m_cur_layer_id, m_layer_height);
    float nz_extrusion_flow = extrusion_flow_for(ramming_slab_height, nc_width);
    WipeTowerWriter writer(ramming_slab_height, nc_width, m_gcode_flavor, m_filpar, m_enable_arc_fitting);
    writer.set_extrusion_flow(nz_extrusion_flow)
        .set_z(m_z_pos)
        .set_initial_tool(m_current_tool)
        .set_y_shift(m_y_shift + (new_filament_id != (unsigned int) (-1) && (m_current_shape == SHAPE_REVERSED) ? m_layer_info->depth - m_layer_info->toolchanges_depth() : 0.f));
    set_for_wipe_tower_writer(writer);
    const bool first_layer = is_first_layer_for_slab(ramming_slab_height);
    writer.set_first_layer(first_layer);

    WipeTowerBlock* block = get_block_by_category(get_block_key(old_filament_id, m_cur_layer_id), false);
    if (!block) {
        assert(false);
        return WipeTower::NozzleChangeResult();
    }
    m_cur_block = block;

    float dy = first_layer ? nc_width : m_layer_info->extra_spacing * get_block_gap_width(m_current_tool, true);
    box_coordinates cleaning_box(Vec2f(x_offset, block->cur_depth + (nc_width - old_tool_width) / 2),
                                 nozzle_change_box_width,
                                 nozzle_change_depth);
    Vec2f initial_position = cleaning_box.ld;
    writer.set_initial_position(initial_position, m_wipe_tower_width, m_wipe_tower_depth, m_internal_rotation);

    // --- Nozzle change preamble: notify firmware + precool ---
    writer.append(format_nozzle_change_line(true, old_filament_id, new_filament_id));
    if (!extruder_change) {
        int new_nozzle_id = m_multi_nozzle_group_result->is_support_dynamic_nozzle_map()
                                ? get_nozzle_id(new_filament_id, m_cur_layer_id) : -1;
        writer.append(format_line_M632(new_filament_id, new_nozzle_id));
        if (m_filpar[m_current_tool].precool_target_temp.second != 0) {
            writer.format_line_M104(m_filpar[m_current_tool].precool_target_temp.second, m_current_tool, false, true, "Wipe tower nozzle change pre cooling")
                .append(format_line_M106());
        }
        writer.append(format_line_M633());
    }

    NozzleChangeResult result;

    if (nozzle_change_line_count > 0) {
        float max_e_ramming_speed = extruder_change ? m_filpar[m_current_tool].max_e_ramming_speed.first : m_filpar[m_current_tool].max_e_ramming_speed.second;
        float nozzle_change_speed = 60.0f * max_e_ramming_speed / nz_extrusion_flow;
        if (solid_infill)
            nozzle_change_speed = std::min(40.f * 60.f, nozzle_change_speed);
        float bridge_speed = std::min(60.0f * max_e_ramming_speed / extrusion_flow_for(0.2f, nc_width), nozzle_change_speed);

        const float &xl = cleaning_box.ld.x();
        const float &xr = cleaning_box.rd.x();
        dy = solid_infill ? nc_width : dy;
        if (solid_infill)
            nozzle_change_line_count = std::floor(EPSILON + (cleaning_box.ru[1] - cleaning_box.rd[1] + (nc_width - old_tool_width) / 2.f) /
                                                                nc_width);
        m_left_to_right = true;
        bool need_change_flow   = false;
        float ramming_length    = nozzle_change_line_count * (xr - xl);
        int   extruder_id      = get_extruder_id(m_current_tool, m_cur_layer_id);
        float precool_t         = extruder_change ? m_filpar[m_current_tool].precool_t.first[extruder_id] : m_filpar[m_current_tool].precool_t.second[extruder_id];
        float precool_t_first_layer = extruder_change ? m_filpar[m_current_tool].precool_t_first_layer.first[extruder_id] :
                                                        m_filpar[m_current_tool].precool_t_first_layer.second[extruder_id];
        float per_cooling_max_speed = nozzle_change_speed;
        // PVA-only: keep volumetric ram speed (do not stretch to precool_t). Stretching ram to
        // wait for temperature would delay the one-shot long cut retract and keep the melt zone
        // higher in the heatbreak. Other filaments still stretch F so ram lasts ~precool_t.
        const bool command_only_active = m_handle_hotend_as_extruder && m_filpar[m_current_tool].material == "PVA";
        if (extruder_change && !command_only_active) {
            if (first_layer && precool_t_first_layer > EPSILON)
                per_cooling_max_speed = ramming_length / precool_t_first_layer * 60.f;
            else if (precool_t > EPSILON)
                per_cooling_max_speed = ramming_length / precool_t * 60.f;
        }//BBS:nozzle change does not require forcing a cooldown to a specific temperature.
        if (nozzle_change_speed > per_cooling_max_speed) nozzle_change_speed = per_cooling_max_speed;
        if (bridge_speed > per_cooling_max_speed) bridge_speed = per_cooling_max_speed;
        LimitFlow LimitRamming = extruder_change ? LimitFlow::LimitRammingFlow : LimitFlow::LimitRammingFlowNC;
        for (int i = 0; true; ++i) {
            if (need_thick_bridge_flow(writer.pos().y(), ramming_slab_height)) {
                writer.set_extrusion_flow(extrusion_flow_for(0.2f, nc_width));
                writer.append(";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Height) + std::to_string(0.2) + "\n");
                need_change_flow = true;
            }
            if (m_left_to_right)
                writer.extrude(xr + wipe_tower_wall_infill_overlap * m_perimeter_width, writer.y(), need_change_flow ? bridge_speed : nozzle_change_speed, LimitRamming);
            else
                writer.extrude(xl - wipe_tower_wall_infill_overlap * m_perimeter_width, writer.y(), need_change_flow ? bridge_speed : nozzle_change_speed, LimitRamming);
            if (i == nozzle_change_line_count - 1)
                break;
            if ((writer.y() + dy - cleaning_box.ru.y()+(nc_width+old_tool_width)/2) > (float)EPSILON) break;
            if (need_change_flow) {
                writer.set_extrusion_flow(extrusion_flow_for(ramming_slab_height, nc_width));
                writer.append(";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Height) + std::to_string(ramming_slab_height) + "\n");
                need_change_flow = false;
            }
            writer.extrude(writer.x(), writer.y() + dy, nozzle_change_speed, LimitRamming);
            m_left_to_right = !m_left_to_right;
        }
        if (need_change_flow) {
            writer.append(";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Height) + std::to_string(ramming_slab_height) + "\n");
        }

        writer.set_extrusion_flow(nz_extrusion_flow);
        block->cur_depth += nozzle_change_depth;
        block->last_nozzle_change_id = old_filament_id;
        // --- Post-ramming: re-arm nozzle change for travel phase ---
        if (!extruder_change) {
            int new_nozzle_id = m_multi_nozzle_group_result->is_support_dynamic_nozzle_map()
                                    ? get_nozzle_id(new_filament_id, m_cur_layer_id) : -1;
            writer.append(format_line_M632(new_filament_id, new_nozzle_id));
        }

        if (is_need_reverse_travel(m_current_tool, extruder_change)) {
            bool   left_to_right     = !m_left_to_right;
            int  tpu_line_count = nozzle_change_line_count;
            nozzle_change_speed *= 2; // due to nozzle change 2 perimeter
            float ramming_travel_time     = extruder_change ? m_filpar[m_current_tool].ramming_travel_time.first : m_filpar[m_current_tool].ramming_travel_time.second;
            float need_reverse_travel_dis = ramming_travel_time * nozzle_change_speed / 60.f;
            float real_travel_dis         = tpu_line_count * (xr - xl - 2 * old_tool_width);
            if (real_travel_dis < need_reverse_travel_dis)
                nozzle_change_speed *= real_travel_dis / need_reverse_travel_dis;
            writer.travel(writer.x(), writer.y() + dy/2);

            for (int i = 0; true; ++i) {
                need_reverse_travel_dis -= (xr - xl - 2 * old_tool_width);
                float offset_dis = 0.f;
                if (need_reverse_travel_dis < 0) {
                    offset_dis              = -need_reverse_travel_dis;
                }
                if (left_to_right)
                    writer.travel(xr - old_tool_width - offset_dis, writer.y(), nozzle_change_speed);
                else
                    writer.travel(xl + old_tool_width + offset_dis , writer.y(), nozzle_change_speed);
                if (need_reverse_travel_dis < EPSILON) break;
                if (i == tpu_line_count - 1)
                    break;

                writer.travel(writer.x(), writer.y() - dy);
                left_to_right = !left_to_right;
            }
        } else {
            result.wipe_path.push_back(writer.pos_rotated());
            if (m_left_to_right) {
                result.wipe_path.push_back(Vec2f(0, writer.pos_rotated().y()));
            } else {
                result.wipe_path.push_back(Vec2f(m_wipe_tower_width, writer.pos_rotated().y()));
            }
        }
        if (!extruder_change) writer.append(format_line_M633());
    }

    writer.append(format_nozzle_change_line(false, old_filament_id, new_filament_id));

    result.start_pos = writer.start_pos_rotated();
    result.origin_start_pos = initial_position;
    result.end_pos   = writer.pos_rotated();
    result.gcode     = writer.gcode();
    result.is_extruder_change = extruder_change;
    return result;
}

WipeTower::NozzleChangeResult WipeTower::heater_off_nozzle_change(int old_filament_id, int new_filament_id)
{
    auto format_nozzle_change_line = [this](bool start, int old_fid, int new_fid) -> std::string {
        char        buff[128];
        std::string tag = start ? GCodeProcessor::reserved_tag(GCodeProcessor::ETags::NozzleChangeStart)
                                : GCodeProcessor::reserved_tag(GCodeProcessor::ETags::NozzleChangeEnd);
        int         old_nozzle_id = get_nozzle_id(old_fid, m_cur_layer_id);
        int         new_nozzle_id = get_nozzle_id(new_fid, m_cur_layer_id);
        snprintf(buff, sizeof(buff), ";%s OF%d NF%d ON%d NN%d\n", tag.c_str(), old_fid, new_fid, old_nozzle_id, new_nozzle_id);
        return std::string(buff);
    };

    NozzleChangeResult result;
    result.heater_off_only     = true;
    // This path is only entered when the nozzles differ.
    result.is_extruder_change  = true;
    result.precool_filament_id = old_filament_id;
    result.gcode              = format_nozzle_change_line(true, old_filament_id, new_filament_id) +
                                format_nozzle_change_line(false, old_filament_id, new_filament_id);
    return result;
}

WipeTower::NozzleChangeResult WipeTower::idle_travel_cooling(int old_filament_id, int new_filament_id)
{
    auto format_nozzle_change_line = [this](bool start, int old_fid, int new_fid) -> std::string {
        char        buff[128];
        std::string tag = start ? GCodeProcessor::reserved_tag(GCodeProcessor::ETags::NozzleChangeStart)
                                : GCodeProcessor::reserved_tag(GCodeProcessor::ETags::NozzleChangeEnd);
        int         old_nozzle_id = get_nozzle_id(old_fid, m_cur_layer_id);
        int         new_nozzle_id = get_nozzle_id(new_fid, m_cur_layer_id);
        snprintf(buff, sizeof(buff), ";%s OF%d NF%d ON%d NN%d\n", tag.c_str(), old_fid, new_fid, old_nozzle_id, new_nozzle_id);
        return std::string(buff);
    };
    auto format_line_M633 = []() { return std::string{"M633\n"};};
    auto format_line_M632 = [](int filament_id) {
        return std::string{"M632 S"} + std::to_string(filament_id) + " D\n";
    };

    float wipe_depth  = 0.f;
    float wipe_length = 0.f;
    for (const auto &b : m_layer_info->tool_changes)
        if (b.new_tool == new_filament_id) {
            wipe_length = b.wipe_length;
            wipe_depth  = b.required_depth;
            break;
        }

    WipeTowerBlock *block = get_block_by_category(get_block_key(new_filament_id, m_cur_layer_id), false);
    if (!block) return NozzleChangeResult();

    float dy       = m_layer_info->extra_spacing * get_block_gap_width(new_filament_id, false);
    float xl       = filament_perimeter_width(new_filament_id);
    float xr       = m_wipe_tower_width - xl;
    float line_len = xr - xl;

    int line_count_by_length = (line_len > EPSILON) ? std::max(1, (int) std::ceil(wipe_length / line_len)) : 0;
    int line_count_by_depth  = (dy > EPSILON) ? (int) std::floor(wipe_depth / dy) : 0;
    int line_count           = std::min(line_count_by_length, std::max(1, line_count_by_depth));
    if (line_count <= 0) return NozzleChangeResult();

    bool  extruder_change  = !is_same_extruder(old_filament_id, new_filament_id, m_cur_layer_id);
    float precool_target   = extruder_change ? m_filpar[old_filament_id].precool_target_temp.first
                                             : m_filpar[old_filament_id].precool_target_temp.second;
    float max_travel_speed = m_travel_speed * 60.f;
    float travel_speed     = max_travel_speed;

    if (precool_target > 0) {
        int extruder_id = get_nozzle_id(old_filament_id, m_cur_layer_id);
        float cooling_rate = (extruder_id >= 0 && extruder_id < (int)m_hotend_cooling_rate.size())
            ? (float)m_hotend_cooling_rate[extruder_id] : 2.0f;
        if (cooling_rate < EPSILON) cooling_rate = 2.0f;
        float current_temp = (float) m_filpar[old_filament_id].nozzle_temperature;
        if (current_temp > precool_target) {
            float cooling_time    = (current_temp - precool_target) / cooling_rate;
            float total_travel_len = line_count * line_len;
            if (total_travel_len > EPSILON && cooling_time > EPSILON)
                travel_speed = std::min(max_travel_speed, total_travel_len / cooling_time * 60.f);
        }
    }

    WipeTowerWriter writer(m_layer_height, m_perimeter_width, m_gcode_flavor, m_filpar, m_enable_arc_fitting);
    writer.set_extrusion_flow(m_extrusion_flow)
        .set_z(m_z_pos)
        .set_initial_tool(m_current_tool)
        .set_y_shift(m_y_shift + (new_filament_id != (unsigned int) (-1) && (m_current_shape == SHAPE_REVERSED)
                                      ? m_layer_info->depth - m_layer_info->toolchanges_depth()
                                      : 0.f))
        .append(format_nozzle_change_line(true, old_filament_id, new_filament_id));
    set_for_wipe_tower_writer(writer);

    // Alternate the entry corner every layer (left-bottom / right-top / right-bottom / left-top),
    // matching the get_next_pos pattern so idle-travel direction aligns with the flushing path.
    const bool  start_at_left   = (m_cur_layer_id % 4 == 0) || (m_cur_layer_id % 4 == 3);
    const bool  start_at_bottom = (m_cur_layer_id % 4 == 0) || (m_cur_layer_id % 4 == 2);
    const float depth_span      = (line_count - 1) * dy;

    Vec2f start_pos(start_at_left ? xl : xr,
                    start_at_bottom ? block->cur_depth : block->cur_depth + depth_span);
    writer.set_initial_position(start_pos, m_wipe_tower_width, m_wipe_tower_depth, m_internal_rotation);

    if (precool_target > 0)
        writer.format_line_M104(precool_target, old_filament_id, false, true, "Wipe tower idle travel pre cooling");
    else
        writer.format_line_M104(0, old_filament_id, false, true, "Wipe tower idle travel heater off");

    // --- Wrap idle-travel moves with nozzle change notify (always for idle travel) ---
    writer.append(format_line_M632(new_filament_id));

    bool  moving_right = start_at_left;
    float row_step     = start_at_bottom ? dy : -dy;
    for (int i = 0; i < line_count; ++i) {
        writer.travel(moving_right ? xr : xl, writer.y(), travel_speed);

        if (i < line_count - 1)
            writer.travel(writer.x(), writer.y() + row_step);
        moving_right = !moving_right;
    }

    writer.append(format_line_M633());

    writer.append(format_nozzle_change_line(false, old_filament_id, new_filament_id));

    NozzleChangeResult result;
    result.start_pos              = writer.start_pos_rotated();
    result.origin_start_pos       = start_pos;
    result.end_pos                = writer.pos_rotated();
    result.gcode                  = writer.gcode();
    result.is_extruder_change     = extruder_change;
    result.zero_flush_idle_cooling = true;
    result.precool_target_temp    = precool_target;
    result.precool_filament_id    = old_filament_id;
    return result;
}

int WipeTower::brim_chamfer_loops_num(float layer_height, bool first_layer, int dist_to_first, float line_width) const
{
    if (line_width <= 0.f)
        line_width = m_perimeter_width;
    const float spacing = extrusion_spacing_for(layer_height, line_width);
    if (spacing <= (float) EPSILON)
        return 0;
    // The first layer carries the full brim; every layer above it one loop less, capped by the maximum
    // chamfer width.
    int loops_num = int((m_wipe_tower_brim_width + spacing / 2.f) / spacing);
    if (! first_layer) {
        // A layer below the tower's first layer yields a negative distance, which would turn the
        // subtraction below into hundreds of ever-expanding loops. Such a layer must not get a chamfer.
        if (dist_to_first < 0)
            return 0;
        const int chamfer_loops_num = int(WT_MAX_CHAMFER_WIDTH / spacing);
        loops_num                   = std::min(loops_num, chamfer_loops_num) - dist_to_first;
    }
    return std::max(0, loops_num);
}

WipeTower::ToolChangeResult WipeTower::finish_layer_new(bool extrude_perimeter, bool extrude_fill, bool extrude_fill_wall,
                                                        float wall_height, bool wall_reserved)
{
    assert(!this->layer_finished());
    m_current_layer_finished = true;

    if (wall_height <= 0.f)
        wall_height = m_layer_height;
    const float wall_width = filament_perimeter_width(m_current_tool);
    const float wall_flow  = extrusion_flow_for(m_layer_height, wall_width);

    WipeTowerWriter writer(m_layer_height, wall_width, m_gcode_flavor, m_filpar, m_enable_arc_fitting);
    writer.set_extrusion_flow(wall_flow)
        .set_z(m_z_pos)
        .set_initial_tool(m_current_tool)
        .set_y_shift(m_y_shift - (m_current_shape == SHAPE_REVERSED ? m_layer_info->toolchanges_depth() : 0.f));

    set_for_wipe_tower_writer(writer);

    writer.append(";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Wipe_Tower_Start) + "\n");

    // Slow down on the 1st layer.
    bool first_layer = is_first_layer();
    float feedrate = calc_tower_feedrate(m_current_tool, wall_flow);

    bool toolchanges_on_layer = m_layer_info->toolchanges_depth() > WT_EPSILON;

    std::vector<Vec2f> finish_rect_wipe_path;
    const bool         multi_block_fill = (m_wipe_tower_blocks.size() > 1) && (extrude_fill_wall || extrude_fill);

    // Build list of fill boxes: one per block when multi_block_fill, else one for whole tower.
    // Inset by wall_width, the line this visit actually extrudes with, not by the tower-wide
    // m_perimeter_width, which follows the smallest nozzle: a large nozzle inset by the small one's pitch
    // would lay its fill on top of the outer wall and on top of the neighbouring band.
    std::vector<box_coordinates> fill_boxes;
    if (multi_block_fill) {
        for (const WipeTowerBlock &block : m_wipe_tower_blocks) {
            // A reserved band is the trench a merged slab above will sink into; leave it empty.
            if (m_cur_layer_id >= 0 && size_t(m_cur_layer_id) < block.layer_reserved.size() && block.layer_reserved[m_cur_layer_id])
                continue;
            float block_fill_height = band_fill_depth(block, m_cur_layer_id) - 2 * wall_width;
            if (block_fill_height <= WT_EPSILON)
                continue;
            fill_boxes.emplace_back(
                Vec2f(wall_width, block.start_depth + wall_width),
                m_wipe_tower_width - 2 * wall_width,
                block_fill_height);
        }
    }
    // Falling back to one box spanning the whole tower is only valid when the blocks were not enumerated;
    // if they were and all of them dropped out, there is genuinely nothing to fill.
    if (fill_boxes.empty() && !multi_block_fill) {
        float fill_box_depth = m_wipe_tower_depth - 2 * wall_width;
        // Use the band's own depth: m_layer_info->depth is locked to the full tower depth when the outer
        // walls are kept unshrunk (side scrape), which would lay the grid over an area nothing supports.
        if (m_wipe_tower_blocks.size() == 1)
            fill_box_depth = band_fill_depth(m_wipe_tower_blocks.front(), m_cur_layer_id) - 2 * wall_width;
        fill_boxes.emplace_back(Vec2f(wall_width, wall_width), m_wipe_tower_width - 2 * wall_width, fill_box_depth);
    }

    if (!fill_boxes.empty())
        writer.set_initial_position((m_left_to_right ? fill_boxes.front().ru : fill_boxes.front().lu), m_wipe_tower_width, m_wipe_tower_depth, m_internal_rotation);
    else
        // The caller reads start_pos even when every block is reserved and this layer has no fill.
        writer.set_initial_position(Vec2f(wall_width, wall_width), m_wipe_tower_width, m_wipe_tower_depth, m_internal_rotation);

    bool solid_infill = (m_layer_info + 1 == m_plan.end()) ? false :
                        std::any_of((m_layer_info + 1)->tool_changes.begin(), (m_layer_info + 1)->tool_changes.end(),
                                    [this](const WipeTowerInfo::ToolChange &tch) { return m_filpar[tch.new_tool].is_soluble || m_filpar[tch.old_tool].is_soluble; });
    solid_infill |= first_layer && m_adhesion;

    // The sparse grid, the rectangle around it and the outer wall may extrude at a capped width narrower
    // than wall_width; their positions and spacing still follow wall_width.
    const float capped_width  = capped_wall_grid_width(m_current_tool);
    const bool  grid_narrowed = capped_width + WT_EPSILON < wall_width;

    for (size_t i = 0; i < fill_boxes.size(); ++i) {
        const box_coordinates &fill_box = fill_boxes[i];
        if (i > 0)
            writer.travel(m_left_to_right ? fill_box.ru : fill_box.lu);

        if (extrude_fill_wall && (fill_box.ru.y() - fill_box.rd.y() > WT_EPSILON))
        {
            // The rectangle around the grid follows the capped width too, except on the first layer.
            const bool narrow_rect = grid_narrowed && !first_layer;
            if (narrow_rect)
                writer.set_line_width(capped_width).set_extrusion_flow(extrusion_flow_for(m_layer_height, capped_width)).change_analyzer_line_width(capped_width);
            writer.rectangle_fill_box(this, fill_box, finish_rect_wipe_path, feedrate);
            if (narrow_rect)
                writer.set_line_width(wall_width).set_extrusion_flow(wall_flow).change_analyzer_line_width(wall_width);
        }
        // Extrude infill to support the material to be printed above.
        const float         dy    = (fill_box.lu.y() - fill_box.ld.y() - wall_width);
        float               left  = fill_box.lu.x() + 2 * wall_width;
        float               right = fill_box.ru.x() - 2 * wall_width;

        if (extrude_fill && dy > wall_width) {
            writer.travel(fill_box.ld + Vec2f(wall_width * 2, 0.f))
                .append(";--------------------\n"
                        "; CP EMPTY GRID START\n")
                .comment_with_value(" layer #", m_num_layer_changes + 1);

            if (solid_infill) {
                float sparse_factor = 1.5f; // 1=solid, 2=every other line, etc.
                if (first_layer) {          // the infill should touch perimeters
                    left -= wall_width;
                    right += wall_width;
                    sparse_factor = 1.f;
                }
                float y       = fill_box.ld.y() + wall_width;
                int   n       = dy / (wall_width * sparse_factor);
                // A single line has no spacing to distribute; dividing by n - 1 would be a division by zero.
                float spacing = n > 1 ? (dy - wall_width) / (n - 1) : 0.f;
                int   i       = 0;
                for (i = 0; i < n; ++i) {
                    writer.extrude(writer.x(), y, feedrate).extrude(i % 2 ? left : right, y);
                    y = y + spacing;
                }
                writer.extrude(writer.x(), fill_box.lu.y());
            } else {
                // Extrude an inverse U at the left of the region and the sparse infill.
                if (grid_narrowed)
                    writer.set_extrusion_flow(extrusion_flow_for(m_layer_height, capped_width)).change_analyzer_line_width(capped_width);
                writer.extrude(fill_box.lu + Vec2f(wall_width * 2, 0.f), feedrate);

                // A box narrower than the inset leaves nothing to bridge over and no line count to divide by.
                const int   n  = 1 + int((right - left) / m_bridging);
                const float dx = n > 0 ? (right - left) / n : 0.f;
                for (int i = 1; i <= n; ++i) {
                    float x = left + dx * i;
                    writer.travel(x, writer.y());
                    writer.extrude(x, i % 2 ? fill_box.rd.y() : fill_box.ru.y());
                }
                if (grid_narrowed)
                    writer.set_extrusion_flow(wall_flow).change_analyzer_line_width(wall_width);

                finish_rect_wipe_path.clear();
                // BBS: add wipe_path for this case: only with finish rectangle
                finish_rect_wipe_path.emplace_back(writer.pos());
                finish_rect_wipe_path.emplace_back(Vec2f(left + dx * n, n % 2 ? fill_box.ru.y() : fill_box.rd.y()));
            }

            writer.append("; CP EMPTY GRID END\n"
                          ";------------------\n\n\n\n\n\n\n");
        }
    }

    // The fill above follows the tower slab. The outer wall has its own Z merge plan across consecutive
    // tool-change layers, so switch flow only for the perimeter.
    const float perimeter_flow = extrusion_flow_for(wall_height, wall_width);
    writer.set_extrusion_flow(perimeter_flow);
    if (extrude_perimeter && std::abs(wall_height - m_layer_height) > WT_EPSILON)
        writer.change_analyzer_layer_height(wall_height);
    // A merged wall reaching down to the bed is the tower's anchor, so it carries the brim and the first
    // layer speed even when it tops out above the tower's first layer.
    const bool wall_first_layer = is_first_layer_for_slab(wall_height);
    writer.set_first_layer(wall_first_layer);
    feedrate = calc_tower_feedrate(m_current_tool, perimeter_flow, wall_first_layer);

    // outer perimeter (always):
    // BBS
    float wipe_tower_depth = m_wipe_tower_depth;
    // Single-block towers normally shrink the outer wall with the per-layer depth. The side scrape needs
    // that wall to stay at the full tower depth on every layer, so keep m_wipe_tower_depth.
    if (m_wipe_tower_blocks.size() == 1 && ! keeps_outer_walls_unshrunk()) {
        wipe_tower_depth = m_layer_info->depth + m_perimeter_width;
    }
    box_coordinates wt_box(Vec2f(0.f, 0.f), m_wipe_tower_width, wipe_tower_depth);
    wt_box = align_perimeter(wt_box);

    //if (extrude_perimeter && !m_use_rib_wall) {
    //    if (!m_use_gap_wall)
    //        writer.rectangle(wt_box, feedrate);
    //    else
    //        generate_support_wall(writer, wt_box, feedrate, first_layer);
    //}
    // The outer wall itself may extrude at the capped width; brim and chamfer loops keep wall_width. The
    // wall's first layer keeps the full width for bed adhesion.
    const bool outer_wall_narrowed = extrude_perimeter && grid_narrowed && !wall_first_layer;
    // Width of the outer wall the brim and chamfer loops surround: the one drawn below, or the one
    // only_generate_out_wall() drew on this layer.
    float drawn_wall_width = wall_width;
    if (outer_wall_narrowed)
        drawn_wall_width = capped_width;
    else if (!extrude_perimeter && std::abs(m_out_wall_only_z - m_z_pos) < WT_EPSILON)
        drawn_wall_width = m_out_wall_only_width;
    if (outer_wall_narrowed)
        writer.set_extrusion_flow(extrusion_flow_for(wall_height, capped_width)).change_analyzer_line_width(capped_width);
    Polygon outer_wall;
    outer_wall = generate_support_wall_new(writer, wt_box, feedrate, wall_first_layer, m_use_rib_wall, extrude_perimeter, m_use_gap_wall);
    if (outer_wall_narrowed)
        writer.set_extrusion_flow(perimeter_flow).change_analyzer_line_width(wall_width);
    if (extrude_perimeter) {
        Polyline shift_polyline = to_polyline(outer_wall);
        shift_polyline.translate(0, scaled(m_y_shift));
        m_outer_wall[m_z_pos].push_back(shift_polyline);
    }

    // First layer: fill the voids left inside the outer wall to maximise the area the tower has glued to
    // the bed. The diagonal scrape drags the ooze stub along the wall on every tool change, and that
    // repeated sideways load can topple a tower whose base only touches the bed on the sparse infill
    // bands. The rib/cross outer wall is concave - its diagonal arms extend past the tower body - and the
    // sparse block infill only covers the central full-width bands, so the arm interiors, the triangular
    // pockets where the arms meet the body, and the thin ring just inside the wall are all left empty.
    // Fill everything inside the wall that the block infill does not cover
    // by offsetting the wall polygon inward step by step. The concentric loops naturally follow the arms,
    // the corner triangles and the ring in one pass. Only done when side scrape is enabled. These loops
    // sit strictly inside the outer wall, so they are intentionally not recorded in m_outer_wall (which
    // tracks the outermost wall for the scrape geometry) and they leave the brim/wipe handling below,
    // which uses outer_wall, untouched.
    if (extrude_perimeter && wall_first_layer && m_enable_side_scrape) {
        const float step = extrusion_spacing_for(wall_height, wall_width);
        // Region already covered by the block/tower purge infill, rebuilt like the fill boxes above so the
        // concentric loops do not double-extrude over it.
        Polygons purged;
        auto add_band = [&](float y0, float y1) {
            if (y1 - y0 <= WT_EPSILON)
                return;
            box_coordinates band(Vec2f(m_perimeter_width, y0), m_wipe_tower_width - 2 * m_perimeter_width, y1 - y0);
            Polygon         p;
            p.points = {Point::new_scale(band.ld), Point::new_scale(band.rd), Point::new_scale(band.ru), Point::new_scale(band.lu)};
            purged.emplace_back(std::move(p));
        };
        if (m_wipe_tower_blocks.size() > 1) {
            // Upper bound of what finish_block() fills: the block band minus one perimeter width at its far
            // end. A band filled by a wider nozzle is inset further, so this never underestimates it, which
            // keeps the concentric loops below off the purge at the cost of a slightly larger empty ring.
            for (const WipeTowerBlock &block : m_wipe_tower_blocks)
                add_band(block.start_depth, block.start_depth + band_fill_depth(block, m_cur_layer_id) - m_perimeter_width);
        } else {
            for (const box_coordinates &fb : fill_boxes)
                add_band(fb.ld.y(), fb.lu.y());
        }
        // The band edges are the outermost purge extrusion axes, so grow them by one line spacing to keep the
        // concentric loops just touching the purge instead of printing on top of it.
        Polygons purged_grown = purged.empty() ? Polygons{} : offset(purged, scaled(step));
        // Interior of the wall, one line in (the wall itself is already extruded above), minus the purge.
        // Tracked as ExPolygons and re-unioned after every shrink: the rib wall is concave, so shrinking it
        // splits the region into islands whose contours would otherwise be re-offset as an unrelated set of
        // loops and could end up overlapping each other.
        ExPolygons voids = diff_ex(offset(outer_wall, -scaled(step)), purged_grown);
        // Anything narrower than a single extrusion cannot be printed as a loop and would only deposit a
        // blob on the first layer, so drop those islands instead of tracing them.
        const double min_island_area = scaled<double>(step) * scaled<double>(step);
        remove_small_and_small_holes(voids, min_island_area);
        for (int guard = 0; guard < 200 && ! voids.empty(); ++guard) {
            for (const ExPolygon &island : voids) {
                writer.polygon(island.contour, feedrate);
                for (const Polygon &hole : island.holes)
                    writer.polygon(hole, feedrate);
            }
            voids = union_ex(offset_ex(voids, -scaled(step)));
            remove_small_and_small_holes(voids, min_island_area);
        }
    }

    // brim chamfer
    // Offset by the wall filament's spacing, not the tower-wide smallest-nozzle pitch, otherwise a
    // 0.4 mm wall is packed at ~0.2 mm and the chamfer looks solid for a dozen layers.
    float spacing = extrusion_spacing_for(wall_height, wall_width);
    int   dist_to_1st = int(m_layer_info - m_plan.begin()) - first_printed_layer_idx();
    int   loops_num   = brim_chamfer_loops_num(wall_height, wall_first_layer, dist_to_1st, wall_width);
    if (wall_reserved)
        loops_num = 0;
    // stop print chamfer if depth changes. Kept out of brim_chamfer_loops_num because it depends on the
    // layer being printed rather than on the chamfer geometry; side scrape locks the depth (see
    // keeps_outer_walls_unshrunk), so side_scrape_z_floor() never has to model this branch.
    if (! wall_first_layer && m_layer_info->depth != m_plan.front().depth)
        loops_num = 0;

    if (loops_num > 0) {
        // The surrounded wall may be narrower than the loops, so the first loop moves in to stay against it.
        const float first_loop_step = extrusion_spacing_for(wall_height, 0.5f * (drawn_wall_width + wall_width));
        //box_coordinates box = wt_box;
        for (size_t i = 0; i < loops_num; ++i) {
            Polygons expanded = offset(outer_wall, scaled(i == 0 ? first_loop_step : spacing));
            if (expanded.empty())
                break;
            outer_wall = std::move(expanded.front());
            writer.polygon(outer_wall, feedrate);
            m_outer_wall[m_z_pos].push_back(to_polyline(outer_wall));
        }

            /*for (size_t i = 0; i < loops_num; ++i) {
                box.expand(spacing);
                writer.rectangle(box, feedrate);
            }*/

        if (wall_first_layer) {
            // Save actual brim width to be later passed to the Print object, which will use it
            // for skirt calculation and pass it to GLCanvas for precise preview box
            m_wipe_tower_brim_width_real = first_loop_step + (loops_num - 1) * spacing + spacing / 2.f;
            //m_wipe_tower_brim_width_real = wt_box.ld.x() - box.ld.x() + spacing / 2.f;
        }
        //wt_box = box;
    }

    if (extrude_perimeter || loops_num > 0) {
        writer.add_wipe_path(outer_wall, m_filpar[m_current_tool].wipe_dist);
    }
    else {
        // Now prepare future wipe. box contains rectangle that was extruded last (ccw).
        Vec2f target = (writer.pos() == wt_box.ld ? wt_box.rd : (writer.pos() == wt_box.rd ? wt_box.ru : (writer.pos() == wt_box.ru ? wt_box.lu : wt_box.ld)));

        // BBS: add wipe_path for this case: only with finish rectangle
        if (finish_rect_wipe_path.size() == 2 && finish_rect_wipe_path[0] == writer.pos()) target = finish_rect_wipe_path[1];

        writer.add_wipe_point(writer.pos()).add_wipe_point(target);
    }
    writer.append(";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Wipe_Tower_End) + "\n");

    // Ask our writer about how much material was consumed.
    // Skip this in case the layer is sparse and config option to not print sparse layers is enabled.
    if (!m_sparse_layers_skipped || toolchanges_on_layer)
        if (m_current_tool < m_used_filament_length.size())
            m_used_filament_length[m_current_tool] += writer.get_and_reset_used_filament_length();

    m_nozzle_change_result = NozzleChangeResult();
    return construct_tcr(writer, false, m_current_tool, true, false, 0.f, false, wall_height);
}

WipeTower::ToolChangeResult WipeTower::finish_block(const WipeTowerBlock &block, int filament_id, bool extrude_fill)
{
    // A block can be topped up by a filament of a different diameter than the one that owns it, so the
    // grid follows the filament that is actually extruding.
    const float fill_width = filament_perimeter_width(filament_id);
    const float slab_height = block.layer_heights.empty() ? m_layer_height : block.layer_heights[m_cur_layer_id];
    float filament_e_flow = slab_height * (fill_width - slab_height * (1.f - float(M_PI) / 4.f)) / m_filpar[filament_id].filament_area;

    WipeTowerWriter writer(slab_height, fill_width, m_gcode_flavor, m_filpar, m_enable_arc_fitting);
    writer.set_extrusion_flow(filament_e_flow)
        .set_z(m_z_pos)
        .set_initial_tool(filament_id)
        .set_y_shift(m_y_shift - (m_current_shape == SHAPE_REVERSED ? m_layer_info->toolchanges_depth() : 0.f));

    set_for_wipe_tower_writer(writer);
    // Slow down on the 1st layer.
    bool first_layer = is_first_layer_for_slab(slab_height);
    writer.set_first_layer(first_layer);

    writer.append(";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Wipe_Tower_Start) + "\n");

    float feedrate = calc_tower_feedrate(filament_id, filament_e_flow, first_layer);

    box_coordinates fill_box(Vec2f(0, 0), 0, 0);
    // The band ends where the next one starts, so keep the fill one extrusion of the filament that draws it
    // away from that edge instead of one tower-wide m_perimeter_width, which follows the smallest nozzle.
    fill_box = box_coordinates(Vec2f(fill_width, block.cur_depth), m_wipe_tower_width - 2 * fill_width, block.start_depth + band_fill_depth(block, m_cur_layer_id) - block.cur_depth - fill_width);

    writer.set_initial_position((m_left_to_right ? fill_box.ru : fill_box.lu), m_wipe_tower_width, m_wipe_tower_depth, m_internal_rotation);

    bool toolchanges_on_layer = m_layer_info->toolchanges_depth() > WT_EPSILON;

    std::vector<Vec2f> finish_rect_wipe_path;
    // The sparse grid and the rectangle around it may extrude at a capped width narrower than fill_width;
    // their positions and spacing still follow fill_width.
    const float capped_width  = capped_wall_grid_width(filament_id);
    const bool  grid_narrowed = capped_width + WT_EPSILON < fill_width;
    const float capped_flow   = slab_height * extrusion_spacing_for(slab_height, capped_width) / m_filpar[filament_id].filament_area;
    // inner perimeter of the sparse section, if there is space for it:
    if (fill_box.ru.y() - fill_box.rd.y() > WT_EPSILON) {
        const bool narrow_rect = grid_narrowed && !first_layer;
        if (narrow_rect)
            writer.set_line_width(capped_width).set_extrusion_flow(capped_flow).change_analyzer_line_width(capped_width);
        writer.rectangle_fill_box(this, fill_box, finish_rect_wipe_path, feedrate);
        if (narrow_rect)
            writer.set_line_width(fill_width).set_extrusion_flow(filament_e_flow).change_analyzer_line_width(fill_width);
    }

    // Extrude infill to support the material to be printed above.
    const float        dy    = (fill_box.lu.y() - fill_box.ld.y() - fill_width);
    float              left  = fill_box.lu.x() + 2 * fill_width;
    float              right = fill_box.ru.x() - 2 * fill_width;
    if (extrude_fill && dy > fill_width) {
        writer.travel(fill_box.ld + Vec2f(fill_width * 2, 0.f))
            .append(";--------------------\n"
                    "; CP EMPTY GRID START\n")
            .comment_with_value(" layer #", m_num_layer_changes + 1);

        // Is there a soluble filament wiped/rammed at the next layer?
        // If so, the infill should not be sparse.
        bool solid_infill = m_layer_info + 1 == m_plan.end() ?
                                false :
                                std::any_of((m_layer_info + 1)->tool_changes.begin(), (m_layer_info + 1)->tool_changes.end(),
                                            [this](const WipeTowerInfo::ToolChange &tch) { return m_filpar[tch.new_tool].is_soluble || m_filpar[tch.old_tool].is_soluble; });
        solid_infill |= first_layer && m_adhesion;

        if (solid_infill) {
            float sparse_factor = 1.5f; // 1=solid, 2=every other line, etc.
            if (first_layer) {          // the infill should touch perimeters
                left -= fill_width;
                right += fill_width;
                sparse_factor = 1.f;
            }
            float y       = fill_box.ld.y() + fill_width;
            int   n       = dy / (fill_width * sparse_factor);
            // A single line has no spacing to distribute; dividing by n - 1 would be a division by zero.
            float spacing = n > 1 ? (dy - fill_width) / (n - 1) : 0.f;
            int   i       = 0;
            for (i = 0; i < n; ++i) {
                writer.extrude(writer.x(), y, feedrate).extrude(i % 2 ? left : right, y);
                y = y + spacing;
            }
            writer.extrude(writer.x(), fill_box.lu.y());
        } else {
            // Extrude an inverse U at the left of the region and the sparse infill.
            if (grid_narrowed)
                writer.set_extrusion_flow(capped_flow).change_analyzer_line_width(capped_width);
            writer.extrude(fill_box.lu + Vec2f(fill_width * 2, 0.f), feedrate);

            // A box narrower than the inset leaves nothing to bridge over and no line count to divide by.
            const int   n  = 1 + int((right - left) / m_bridging);
            const float dx = n > 0 ? (right - left) / n : 0.f;
            for (int i = 1; i <= n; ++i) {
                float x = left + dx * i;
                writer.travel(x, writer.y());
                writer.extrude(x, i % 2 ? fill_box.rd.y() : fill_box.ru.y());
            }
            if (grid_narrowed)
                writer.set_extrusion_flow(filament_e_flow).change_analyzer_line_width(fill_width);
            finish_rect_wipe_path.clear();
            // BBS: add wipe_path for this case: only with finish rectangle
            finish_rect_wipe_path.emplace_back(writer.pos());
            finish_rect_wipe_path.emplace_back(Vec2f(left + dx * n, n % 2 ? fill_box.ru.y() : fill_box.rd.y()));
        }

        writer.append("; CP EMPTY GRID END\n"
                      ";------------------\n\n\n\n\n\n\n");
    }

    // outer perimeter (always):
    // BBS
    box_coordinates wt_box(Vec2f(0.f, 0.f), m_wipe_tower_width, m_layer_info->depth + m_perimeter_width);
    wt_box = align_perimeter(wt_box);

    // Now prepare future wipe. box contains rectangle that was extruded last (ccw).
    Vec2f target = (writer.pos() == wt_box.ld ? wt_box.rd : (writer.pos() == wt_box.rd ? wt_box.ru : (writer.pos() == wt_box.ru ? wt_box.lu : wt_box.ld)));

    // BBS: add wipe_path for this case: only with finish rectangle
    if (finish_rect_wipe_path.size() == 2 && finish_rect_wipe_path[0] == writer.pos()) target = finish_rect_wipe_path[1];

    writer.add_wipe_point(writer.pos()).add_wipe_point(target);

    writer.append(";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Wipe_Tower_End) + "\n");

    // Ask our writer about how much material was consumed.
    // Skip this in case the layer is sparse and config option to not print sparse layers is enabled.
    if (!m_sparse_layers_skipped || toolchanges_on_layer)
        if (filament_id < m_used_filament_length.size())
            m_used_filament_length[filament_id] += writer.get_and_reset_used_filament_length();

    return construct_block_tcr(writer, false, filament_id, true, 0.f, slab_height);
}

WipeTower::ToolChangeResult WipeTower::finish_block_solid(const WipeTowerBlock &block, int filament_id, bool extrude_fill, WipeTowerLayerType layer_type)
{
    float filament_area_val = m_filpar[filament_id].filament_area;
    const float fill_width  = filament_perimeter_width(filament_id);
    // A merged slab has a fixed physical height and must not be thickened after planning. Without merged
    // slabs, retain the legacy 0.2 mm equivalent flow when a solid block is laid over a normal sparse layer.
    const float slab_height = block.layer_heights.empty() ? m_layer_height : block.layer_heights[m_cur_layer_id];
    float       extrusion_height = slab_height;
    float       e_flow = slab_height * (fill_width - slab_height * (1.f - float(M_PI) / 4.f)) / filament_area_val;
    const float e_flow_02 = 0.2f * (fill_width - 0.2f * (1.f - float(M_PI) / 4.f)) / filament_area_val;
    if (!m_merged_slabs_active && m_cur_layer_id > 1 &&
        block.layers_type[m_cur_layer_id - 1] == WipeTowerLayerType::Normal && e_flow < e_flow_02) {
        extrusion_height = 0.2f;
        e_flow = e_flow_02;
    }

    WipeTowerWriter writer(extrusion_height, fill_width, m_gcode_flavor, m_filpar, m_enable_arc_fitting);
    writer.set_extrusion_flow(e_flow)
        .set_z(m_z_pos)
        .set_initial_tool(filament_id)
        .set_y_shift(m_y_shift - (m_current_shape == SHAPE_REVERSED ? m_layer_info->toolchanges_depth() : 0.f));

    set_for_wipe_tower_writer(writer);
    const bool first_layer = is_first_layer_for_slab(slab_height);
    writer.set_first_layer(first_layer);

    writer.append(";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Wipe_Tower_Start) + "\n");

    float feedrate = calc_tower_feedrate(filament_id, e_flow, first_layer);
    if (layer_type == WipeTowerLayerType::Contact || layer_type == WipeTowerLayerType::Contact_UP)
        feedrate = m_contact_speed;
    box_coordinates fill_box(Vec2f(0, 0), 0, 0);
    // Same reasoning as in finish_block(): the far edge of the band is kept clear by the width of the
    // filament that fills it, not by the smallest nozzle's m_perimeter_width.
    fill_box = box_coordinates(Vec2f(fill_width, block.cur_depth), m_wipe_tower_width - 2 * fill_width,
                               block.start_depth + band_fill_depth(block, m_cur_layer_id) - block.cur_depth - fill_width);
    bool toolchanges_on_layer = m_layer_info->toolchanges_depth() > WT_EPSILON;
    const float dy                   = (fill_box.lu.y() - fill_box.ld.y());
    int   n                    = (dy + 0.25 * fill_width) / fill_width+1;
    float spacing              = fill_width;
    Vec2f initial_pos(0, 0);
    bool        up_to_down = false;
    //set initial pos 
    {
        int   index      = m_cur_layer_id % 4;
        float gird_depth = spacing * (n-1);
        switch (index % 4) {
        case 0:
            initial_pos = fill_box.ld;
            m_left_to_right = true;
            up_to_down      = false;
            break;
        case 1:
            initial_pos     = Vec2f(fill_box.rd.x(), fill_box.rd.y() + gird_depth);
            m_left_to_right = false;
            up_to_down      = true;
            break;
        case 2:
            initial_pos     = fill_box.rd;
            m_left_to_right = false;
            up_to_down      = false;
            break;
        case 3:
            initial_pos     = Vec2f(fill_box.ld.x(), gird_depth + fill_box.ld.y());
            m_left_to_right = true;
            up_to_down      = true;
            break;
        default: break;
        }
    }
    // Extrude infill to support the material to be printed above.
    float              left  = fill_box.lu.x();
    float              right = fill_box.ru.x();
    std::vector<Vec2f> finish_rect_wipe_path;
    {
        writer
            .append(";--------------------\n"
                    "; CP EMPTY GRID START\n")
            .comment_with_value(" layer #", m_num_layer_changes + 1);
        bool is_full_block = std::abs(block.cur_depth - block.start_depth) < EPSILON;
        if (is_full_block && layer_type == WipeTowerLayerType::Contact && m_enable_tower_interface_features ) {
            Vec2f        stop_pos                                    = initial_pos;
            float        filament_tower_interface_pre_extrusion_dist = m_filpar[m_current_tool].filament_tower_interface_pre_extrusion_dist;
            BoundingBoxf printer_bbx                                 = unscaled(get_extents(m_shared_print_bed));
            printer_bbx.translate((- m_wipe_tower_pos - m_rib_offset).cast<double>()); // first layer never be contact
            if (stop_pos.x() < m_wipe_tower_width/2.f)
                stop_pos = Vec2f(stop_pos.x() - filament_tower_interface_pre_extrusion_dist, stop_pos.y());
            else
                stop_pos = Vec2f(stop_pos.x() + filament_tower_interface_pre_extrusion_dist, stop_pos.y());
            if (stop_pos.x() < printer_bbx.min[0]) stop_pos.x() = printer_bbx.min[0];
            if (stop_pos.x() > printer_bbx.max[0]) stop_pos.x() = printer_bbx.max[0];
            initial_pos = stop_pos;
            if (m_filpar[m_current_tool].filament_tower_interface_print_temp != m_filpar[m_current_tool].nozzle_temperature)
                writer.format_line_M109(m_filpar[m_current_tool].filament_tower_interface_print_temp,this->m_current_tool, "Wipe tower interface layer enter");
                writer.retract(-m_filpar[m_current_tool].filament_tower_interface_pre_extrusion_length - 2.f, 100.f);
        }
        writer.set_initial_position(initial_pos, m_wipe_tower_width, m_wipe_tower_depth, m_internal_rotation);

        int   i             = 0;
        for (i = 0; i < n; ++i) {
            writer.extrude(m_left_to_right ? right : left, writer.y(), feedrate);
            if (i == n - 1) {
                writer.add_wipe_point(writer.pos()).add_wipe_point(Vec2f(m_left_to_right ? left : right, writer.y()));
                break;
            }
            m_left_to_right = !m_left_to_right;
            writer.extrude(writer.x(), writer.y()+spacing*(up_to_down?-1:1), feedrate);
        }
        if (layer_type == WipeTowerLayerType::Contact && m_enable_tower_interface_features && m_filpar[m_current_tool].filament_tower_interface_print_temp != m_filpar[m_current_tool].nozzle_temperature)
            writer.format_line_M104(m_filpar[m_current_tool].nozzle_temperature, this->m_current_tool,
                                    m_filpar[m_current_tool].nozzle_temperature > m_filpar[m_current_tool].filament_tower_interface_print_temp,
                                    true, "Wipe tower interface layer leave restore");
        writer.append("; CP EMPTY GRID END\n"
                      ";------------------\n\n\n\n\n\n\n");
    }

    writer.append(";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Wipe_Tower_End) + "\n");

    // Ask our writer about how much material was consumed.
    // Skip this in case the layer is sparse and config option to not print sparse layers is enabled.
    if (!m_sparse_layers_skipped || toolchanges_on_layer)
        if (filament_id < m_used_filament_length.size())
            m_used_filament_length[filament_id] += writer.get_and_reset_used_filament_length();

    return construct_block_tcr(writer, false, filament_id, true, 0.f, slab_height);
}

// Cut the polylines with the line "along == const" and keep the extreme coordinate on the requested
// side. See the WipeTower::outer_wall_extreme_at declaration for the across/along axis convention.
static bool polylines_extreme_at(const Polylines &pls, bool across_is_x, bool positive_side, float along, float &out_across)
{
    const int i_across = across_is_x ? 0 : 1;
    const int i_along  = across_is_x ? 1 : 0;

    bool  found   = false;
    float extreme = positive_side ? -std::numeric_limits<float>::max() : std::numeric_limits<float>::max();
    for (const Polyline &pl : pls) {
        const Points &pts = pl.points;
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            Vec2f a = unscaled<float>(pts[i]);
            Vec2f b = unscaled<float>(pts[i + 1]);
            if ((a[i_along] - along) * (b[i_along] - along) > 0.f)          // segment does not straddle the cut
                continue;
            if (std::abs(b[i_along] - a[i_along]) < (float) EPSILON)        // segment parallel to the cut, ignore
                continue;
            float t = (along - a[i_along]) / (b[i_along] - a[i_along]);
            float c = a[i_across] + t * (b[i_across] - a[i_across]);
            extreme = positive_side ? std::max(extreme, c) : std::min(extreme, c);
            found   = true;
        }
    }
    if (found)
        out_across = extreme;
    return found;
}

bool WipeTower::outer_wall_extreme_at(float z, bool across_is_x, bool positive_side, float along, float &out_across) const
{
    if (m_outer_wall.empty())
        return false;
    // Pick the stored layer whose Z is closest to the requested z (within half a layer height). The map
    // is ordered by Z, so the candidates are the first entry not below z and the one before it.
    const float      tol = m_layer_height * 0.5f + (float) EPSILON;
    const Polylines *pls = nullptr;
    float            best_dz = tol;
    auto             it  = m_outer_wall.lower_bound(z - tol);
    for (; it != m_outer_wall.end() && it->first <= z + tol; ++it) {
        float dz = std::abs(it->first - z);
        if (dz <= best_dz) {
            best_dz = dz;
            pls     = &it->second;
        }
    }
    if (pls == nullptr)
        return false;

    return polylines_extreme_at(*pls, across_is_x, positive_side, along, out_across);
}

bool WipeTower::side_wall_straight_along_range(bool across_is_x, bool positive_side, float &along_begin, float &along_end) const
{
    const float along_extent = across_is_x ? m_wipe_tower_depth : m_wipe_tower_width;
    if (along_extent <= (float) EPSILON)
        return false;

    const int   n    = std::max(32, int(std::lround(double(along_extent) / 0.25)));
    const float step = along_extent / float(n);

    std::vector<float> wall(n + 1, 0.f);
    std::vector<char>  wall_valid(n + 1, 0);
    for (int i = 0; i <= n; ++i) {
        float along = step * float(i);
        float q     = 0.f;
        for (float z : {m_z_pos - m_layer_height, m_z_pos}) {
            if (! outer_wall_extreme_at(z, across_is_x, positive_side, along, q))
                continue;
            wall[i]       = wall_valid[i] ? (positive_side ? std::max(wall[i], q) : std::min(wall[i], q)) : q;
            wall_valid[i] = 1;
        }
    }

    // Grow the straight run outwards from the middle of the wall. The diagonal ribs run from the tower
    // centre to its corners, so they always meet a wall near its ends and never at its middle, and the
    // fillets only round the corners: the middle sample sits on the straight part whenever there is one,
    // which makes it both the seed and the nominal outer coordinate of the wall. Estimating that nominal
    // edge from all samples instead (a median, say) only holds while the straight part covers more than
    // half of the wall - on a small tower the fixed-width rib arms take most of it and the estimate lands
    // on a rib slope, rejecting the wall even though its middle runs perfectly straight.
    const int mid = n / 2;
    int       seed = -1;
    for (int d = 0; d <= n && seed < 0; ++d) {
        if (mid - d >= 0 && wall_valid[mid - d])
            seed = mid - d;
        else if (mid + d <= n && wall_valid[mid + d])
            seed = mid + d;
    }
    if (seed < 0)
        return false;
    const float edge = wall[seed];

    // Deviation from the seed that still counts as straight: fillets pull the wall inwards and the
    // diagonal ribs push it outwards, both of which show up as a deviation from edge.
    const float tol = std::max(0.02f, 0.05f * m_perimeter_width);

    int lo = seed, hi = seed;
    while (lo > 0 && wall_valid[lo - 1] && std::abs(wall[lo - 1] - edge) <= tol)
        --lo;
    while (hi < n && wall_valid[hi + 1] && std::abs(wall[hi + 1] - edge) <= tol)
        ++hi;

    // Pull in by one sample step: the true fillet/rib boundary lies somewhere between two samples. A wall
    // with no straight run at all collapses to the seed, which is still the safest single spot to scrape
    // on, so the caller keeps the scrape there rather than dropping it for the whole layer.
    along_begin = std::min(step * float(lo) + step, step * float(seed));
    along_end   = std::max(step * float(hi) - step, step * float(seed));
    return true;
}

float WipeTower::side_scrape_z_floor() const
{
    // Clearance kept above the highest flared layer. Tangency would not be enough: the scrape line sits
    // only m_side_scrape_wall_gap outside the wall while the chamfer under it flares out much further, so
    // the nozzle passes right over that flare, and over-extrusion on it, the three-decimal rounding of the
    // Z placeholders and the separately computed base Z of a compacted tower all eat into the gap.
    const float clearance = 0.4f;

    if (m_plan.empty())
        return m_layer_height + clearance;

    const size_t first = size_t(std::max(0, first_printed_layer_idx()));

    // Highest layer still carrying a brim or chamfer loop, counted with the same helper finish_layer_new()
    // sizes them with, so the two cannot drift apart. Walk with the wall filament's line width: the loops
    // are drawn at that pitch, not at the tower-wide smallest-nozzle m_perimeter_width. Walking the plan
    // rather than closing the form also handles a variable layer height, which changes the loop spacing
    // from layer to layer.
    size_t flared_top = first;
    const int wall_filament = get_wall_filament_for_all_layer();
    const float bed_z = float(tower_bed_z());
    for (size_t i = first; i < m_plan.size(); ++i) {
        if (layer_is_skipped(int(i)))
            continue;
        // A layer whose wall is merged upwards draws no loops; the group above carries them.
        if (i < m_wall_reserved.size() && m_wall_reserved[i])
            continue;
        int wf = get_wall_filament_for_layer(m_plan[i], int(i), wall_filament);
        if (wf < 0)
            wf = wall_filament;
        const float line_width  = filament_perimeter_width(wf);
        const float wall_height = (i < m_wall_slab_height.size() && m_wall_slab_height[i] > 0.f) ?
                                      m_wall_slab_height[i] : layer_slab_height(int(i), m_plan[i].height);
        const bool  anchor      = i == first || m_plan[i].z - wall_height <= bed_z + WT_EPSILON;
        if (brim_chamfer_loops_num(wall_height, anchor, int(i - first), line_width) <= 0)
            break;
        flared_top = i;
    }

    // Stay above the topmost flared layer; without a brim that is the tower's own first layer, which also
    // keeps the nozzle off the bed.
    return m_plan[flared_top].z + clearance;
}

bool WipeTower::toolchange_side_scrape(WipeTowerWriter &writer, const box_coordinates &cleaning_box, const Vec2f &initial_position,
                                       Vec2f *out_scrape_start, SideScrapeZ *out_z)
{
    // cleaning_box is kept in the signature for call-site consistency; the scrape position is chosen
    // from the full tower footprint so starts can be spread along both walls of the selected pair.
    (void)cleaning_box;

    // Which pair of walls to scrape on, expressed through the across/along axes the geometry helpers
    // use: the diagonal moves along "across", the smear travel after it along "along".
    const bool  across_is_x   = m_side_scrape_on_left_right;
    const float across_extent = across_is_x ? m_wipe_tower_width : m_wipe_tower_depth;

    // Advance the sequence up front so that a layer on which the scrape turns out to be impossible
    // hands the next attempt over to the opposite wall and the next slot instead of retrying the same
    // blocked spot forever.
    const unsigned int slot = m_side_scrape_seq++;

    // Clamp the diagonal run as a sanity bound.
    float length = std::min(m_side_scrape_length, across_extent / 2.f - m_perimeter_width);
    if (length <= (float) EPSILON || m_side_scrape_height <= (float) EPSILON)
        return false;

    // Spread scrape starts across both walls of the pair so ooze stubs do not pile at one XY. Sequence:
    // positive@slot0, negative@slot0, positive@slot1, ... cycling through evenly spaced slots.
    const bool  positive_side = (slot % 2u) == 0u;
    const float outward       = positive_side ? 1.f : -1.f;

    // The m_outer_wall polylines the geometry helpers read live in the tower footprint frame, which is
    // the writer frame with its Y shift already applied. Everything below is computed in that frame and
    // converted back when a move is emitted.
    const float y_shift = writer.y_shift();
    auto to_writer = [across_is_x, y_shift](float across, float along) {
        Vec2f p = across_is_x ? Vec2f(across, along) : Vec2f(along, across);
        p.y() -= y_shift;
        return p;
    };

    // Keep both the scrape start and the following along-wall move on the straight part of the wall:
    // outside that span the wall curves into the fillets or bulges out on the ribs, which would turn
    // the along-wall move into a diagonal and risk touching the tower. The straight span is also what
    // makes the collision reference below safe to take from the current and previous layer alone: a rib
    // arm is longest at the tower base and shrinks with height (see generate_rib_polygon), so on a rib
    // the layers under the one being scraped reach further out than the sampled ones. The span is grown
    // from the middle of the wall, which the ribs never reach, so it degenerates to that single point
    // rather than to the full wall when the wall offers no straight run - the scrape then still happens,
    // just without the along-wall move.
    float straight_lo = 0.f, straight_hi = 0.f;
    if (! side_wall_straight_along_range(across_is_x, positive_side, straight_lo, straight_hi))
        return false;
    const float margin   = std::min(m_perimeter_width, 0.25f * (straight_hi - straight_lo));
    const float along_lo = straight_lo + margin;
    const float along_hi = std::max(along_lo, straight_hi - margin);
    // Fit as many evenly spaced slots into the usable span as the requested spacing allows. A short
    // straight span therefore yields fewer, tighter points rather than points off the straight part,
    // down to a single point when the span has collapsed to the middle of the wall.
    const float span            = along_hi - along_lo;
    const float spacing         = std::max(0.1f, m_side_scrape_point_spacing);
    const int   points_per_side = std::clamp(int(std::lround(double(span) / double(spacing))), 1, 64);
    const int   along_slot      = int((slot / 2u) % unsigned(points_per_side));

    float along0       = along_lo + (float(along_slot) + 0.5f) * span / float(points_per_side);
    float scrape_speed = m_side_scrape_speed;

    float z_top = m_z_pos; // current layer top
    // Top of the tower's own first layer, i.e. the tower base the floor below is expressed relative to.
    float z_base = m_layer_height;
    if (m_first_layer_idx != size_t(-1) && m_first_layer_idx < m_plan.size())
        z_base = m_plan[m_first_layer_idx].z;
    else if (!m_plan.empty())
        z_base = m_plan.front().z;
    // keeps_outer_walls_unshrunk() locks the wall of every layer to the same footprint, so nothing below
    // the current layer reaches further out except the brim chamfer at the base. Staying above that also
    // keeps a large configured height from driving the nozzle into the bed.
    const float z_floor = side_scrape_z_floor();
    float       z_low   = std::max(m_z_pos - m_side_scrape_height, z_floor);
    // Close to the tower base there is no vertical room left. Skip the scrape rather than emit a flat
    // move, which would not drag the stub anywhere.
    if (z_top - z_low <= (float) EPSILON)
        return false;

    // After the diagonal, a short empty travel along the wall drags the ooze stub further instead of
    // leaving a blob at the scrape end. Prefer the direction toward the wipe start and keep it inside
    // the straight span.
    const float initial_along = across_is_x ? initial_position.y() + y_shift : initial_position.x();
    float smear     = std::min(m_side_scrape_along_length, span);
    float smear_dir = (initial_along >= along0) ? 1.f : -1.f;
    float along1    = std::clamp(along0 + smear_dir * smear, along_lo, along_hi);
    // If clamped to the same position (span boundary), try the opposite direction once.
    if (std::abs(along1 - along0) <= (float) EPSILON) {
        smear_dir = -smear_dir;
        along1    = std::clamp(along0 + smear_dir * smear, along_lo, along_hi);
    }

    // Reference wall coordinate for the whole scrape, taken from the stored wall only. The tower extents
    // would be a tempting nominal value for the left and right walls, but seeding the search with them
    // makes a fillet that pulls the wall inwards invisible, and the diagonal would then end short of the
    // real wall and scrape nothing. During a tool change the current layer wall may or may not be printed
    // yet, so take the outermost of the previous and the current layer; sampling the whole [along0, along1]
    // stretch on top of that yields one value that clears the wall everywhere the nozzle goes. Inside a
    // straight span every sample is the same, so this costs nothing there.
    bool  wall_found  = false;
    float wall_across = 0.f;
    {
        const float a  = std::min(along0, along1);
        const float b  = std::max(along0, along1);
        const int   ns = std::max(2, int(std::lround(double(b - a) / 0.25)));
        for (int i = 0; i <= ns; ++i) {
            const float at = a + (b - a) * float(i) / float(ns);
            for (float z : {m_z_pos - m_layer_height, m_z_pos}) {
                float q = 0.f;
                if (! outer_wall_extreme_at(z, across_is_x, positive_side, at, q))
                    continue;
                wall_across = wall_found ? (positive_side ? std::max(wall_across, q) : std::min(wall_across, q)) : q;
                wall_found  = true;
            }
        }
    }
    // Without stored wall data there is nothing to place the scrape against.
    if (! wall_found)
        return false;

    // The scrape line sits a controllable gap outside the wall; the diagonal starts further out and low,
    // then rises while approaching the wall, ending gap-outside the upper wall.
    float scrape_across = wall_across + outward * m_side_scrape_wall_gap;
    float start_across  = scrape_across + outward * length;

    if (out_scrape_start)
        *out_scrape_start = to_writer(start_across, along0);
    if (out_z) {
        out_z->z_low = z_low;
        // The descent the scrape asks for, and how far above the tower base it has to stay to clear the
        // brim chamfer flare. Both are independent of where the layer ends up in Z, so GCode::append_tcr
        // can rebuild the scrape for a tower compacted by wipe_tower_no_sparse_layers.
        out_z->depth                 = z_top - z_low;
        out_z->min_height_above_base = std::max(0.f, z_floor - z_base);
    }

    writer.append(";--------------------\n"
                  "; CP TOOLCHANGE SIDE SCRAPE\n");
    // 1) Position outside the wall at the layer top (also the after-toolchange landing point).
    writer.travel(to_writer(start_across, along0), scrape_speed);
    // 2) Descend below the already-printed top surface, staying outside the wall.
    writer.travel_xyz(to_writer(start_across, along0), "[wipe_tower_scrape_z_low]", scrape_speed);
    // 3) Diagonal scrape: rise back to the layer top while approaching the wall, dragging the ooze
    //    stub up along the outside of the upper wall; the closest approach stays gap-outside the wall.
    writer.travel_xyz(to_writer(scrape_across, along0), "[wipe_tower_scrape_z_top]", scrape_speed);
    // 4) Short empty travel along the wall to smear the stub further. The tower wall is not necessarily
    //    a straight segment (fillets pull it in, rib bulges push it out), so a single straight move at the
    //    constant reference coordinate would lift off the wall wherever the wall curves inward and drag the
    //    stub through the air. Instead follow the actual wall contour, kept a fixed gap outside it. The move
    //    stays at the layer top, so only the current/previous layer walls matter and lower layers, which the
    //    reference coordinate guarded against, cannot be hit here.
    if (std::abs(along1 - along0) > (float) EPSILON) {
        // Outer wall coordinate at one `along`, offset out by the gap so the path hugs the wall without
        // cutting into it. Uses the outermost of the previous and current layer walls, matching the scrape
        // reference. Falls back to the straight reference where this layer stored no wall data.
        auto hug_across = [&](float at) -> float {
            bool  found = false;
            float q     = positive_side ? -std::numeric_limits<float>::max() : std::numeric_limits<float>::max();
            for (float z : {m_z_pos - m_layer_height, m_z_pos}) {
                float w = 0.f;
                if (! outer_wall_extreme_at(z, across_is_x, positive_side, at, w))
                    continue;
                q     = positive_side ? std::max(q, w) : std::min(q, w);
                found = true;
            }
            // Where this layer stored no wall data, keep the straight reference rather than risk cutting in.
            if (! found)
                return scrape_across;
            return q + outward * m_side_scrape_wall_gap;
        };
        // Sample finely enough to trace the fillets and rib bulges; the move is travel only, so a dense
        // polyline just costs a few extra G1 lines.
        const int ns = std::max(1, int(std::lround(double(std::abs(along1 - along0)) / 0.5)));
        for (int i = 1; i <= ns; ++i) {
            const float at = along0 + (along1 - along0) * float(i) / float(ns);
            writer.travel(to_writer(hug_across(at), at), scrape_speed);
        }
    }
    // 5) Travel to the wipe start position so the wipe line begins as usual.
    writer.travel(initial_position, scrape_speed);
    writer.append(";--------------------\n");

    return true;
}

void WipeTower::toolchange_wipe_new(WipeTowerWriter &writer, const box_coordinates &cleaning_box, float wipe_length,bool solid_tool_toolchange)
{
    const float tool_width       = filament_perimeter_width(m_current_tool);
    const float wipe_slab_height = slab_height_for(m_current_tool, m_cur_layer_id, m_layer_height);
    const bool  first_layer      = is_first_layer_for_slab(wipe_slab_height);
    writer.set_first_layer(first_layer);
    writer.set_extrusion_flow(m_extrusion_flow * (first_layer ? m_first_layer_flow_ratio : 1.f))
          .append(";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::CP_TOOLCHANGE_WIPE) + " CT" + std::to_string(solid_tool_toolchange) + " FL" + std::to_string(first_layer) + "\n");
    if (!m_nozzle_change_result.gcode.empty()) {
        // The nozzle change block that was spliced in above ran on the old filament and left its own
        // width/height tags behind, so both have to be restored, not only the width.
        writer.change_analyzer_line_width(tool_width);
        writer.change_analyzer_layer_height(wipe_slab_height);
    }

    // BBS: add the note for gcode-check, when the flow changed, the width should follow the change
    if (first_layer) {
        writer.append(";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Width) + std::to_string(m_first_layer_flow_ratio * tool_width) + "\n");
    }

    //if (solid_tool_toolchange && m_filpar[m_current_tool].filament_tower_interface_print_temp != m_filpar[m_current_tool].nozzle_temperature)
    //    writer.append(format_line_M109(m_filpar[m_current_tool].filament_tower_interface_print_temp, m_filament_map[this->m_current_tool] - 1));
    //if (solid_tool_toolchange && m_filpar[m_current_tool].filament_tower_interface_pre_extrusion_length != 0)
    //    writer.retract(-m_filpar[m_current_tool].filament_tower_interface_pre_extrusion_length, 100.f);

    float        retract_length = m_filpar[m_current_tool].retract_length;
    float        retract_speed  = m_filpar[m_current_tool].retract_speed * 60;
    const float &xl = cleaning_box.ld.x();
    const float &xr = cleaning_box.rd.x();
    bool         should_flat_ironging = m_flat_ironing;
    bool         should_line_ironing  = m_line_ironing;
    if (!m_contact_ironing && solid_tool_toolchange) {
        should_flat_ironging     = false;
        should_line_ironing = false;
    }
    bool  should_cooling_before_tower = !solid_tool_toolchange;
    bool  should_cooling_before_object = false;//interface layer heating print tower, then cooling print object
    int cooling_begin_line = 2;
    float x_to_wipe = wipe_length;
    const float dy           = wipe_line_spacing(solid_tool_toolchange);
    x_to_wipe                = solid_tool_toolchange ? std::numeric_limits<float>::max(): x_to_wipe;
    float target_speed       = first_layer ? std::min(m_first_layer_speed * 60.f, m_max_speed) : calc_tower_feedrate(m_current_tool, m_extrusion_flow, false);
    if (solid_tool_toolchange)
        target_speed = m_contact_speed;
    std::vector<float> WipeSpeedMap;
    const auto &filament_curve = m_filpar[m_current_tool].speed_factor_curve;
    if (!filament_curve.empty()) {
        WipeSpeedMap.reserve(filament_curve.size());
        for (double v : filament_curve)
            WipeSpeedMap.push_back((float)(v * target_speed));
        constexpr size_t kMinSpeedMapSize = 5;
        if (WipeSpeedMap.size() < kMinSpeedMapSize)
            WipeSpeedMap.resize(kMinSpeedMapSize, WipeSpeedMap.back());
    } else {
        WipeSpeedMap = {0.33f * target_speed, 0.375f * target_speed, 0.458f * target_speed, 0.875f * target_speed,
                        std::min(target_speed, 0.875f * target_speed + 50.f)};
    }
    float                    wipe_speed = WipeSpeedMap[0];

    m_left_to_right = ((m_cur_layer_id + 3) % 4 >= 2);

    bool is_from_up = (m_cur_layer_id % 2 == 1);

    auto estimate_time_kernel = [&WipeSpeedMap,&xr,&xl](int n) {
        float time = std::numeric_limits<float>::max();
        float one_line_len = xr - xl;
        if (n <= 1)
            time = one_line_len / WipeSpeedMap[0];
        else if (n <= 2)
            time = one_line_len / WipeSpeedMap[0] + one_line_len / WipeSpeedMap[1];
        else if (n <= 3)
            time = one_line_len / WipeSpeedMap[0] + one_line_len / WipeSpeedMap[1] + one_line_len / WipeSpeedMap[2];
        else if (n <= 4)
            time = one_line_len / WipeSpeedMap[0] + one_line_len / WipeSpeedMap[1] + one_line_len / WipeSpeedMap[2] + one_line_len / WipeSpeedMap[3];
        else {
            time = one_line_len / WipeSpeedMap[0] + one_line_len / WipeSpeedMap[1] + one_line_len / WipeSpeedMap[2] + one_line_len / WipeSpeedMap[3];
            time += (n - 4) * one_line_len / WipeSpeedMap[4];
        }
        return time * 60.f;
    };
    auto estimate_wipe_time = [&estimate_time_kernel, & cleaning_box, &target_speed, &x_to_wipe, &xr, &xl, &dy, &WipeSpeedMap, &solid_tool_toolchange](int begin_line) -> float {
        int                      n            = std::ceil(x_to_wipe / (xr - xl));
        if (solid_tool_toolchange) n = (cleaning_box.lu[1] - cleaning_box.ld[1]) / dy;
        float total_time   = estimate_time_kernel(n);
        float beg_time   = n <= 0 ? 0 : estimate_time_kernel(begin_line);
        return total_time-beg_time;
    };

    bool should_heating = m_filpar[m_current_tool].filament_cooling_before_tower > EPSILON && !solid_tool_toolchange && !first_layer;
    auto add_M104_by_requirement = [&writer, &should_heating, first_layer, this]() {
        if (m_filpar[m_current_tool].filament_cooling_before_tower < EPSILON) return;
        if (!should_heating) return;
        float target_temp = first_layer ? m_filpar[m_current_tool].nozzle_temperature_initial_layer : m_filpar[m_current_tool].nozzle_temperature;
        writer.format_line_M104(target_temp, this->m_current_tool, true, true, "Wipe tower reheat before wipe");
    };
    float speed_factor = 1.f;
    if (should_heating)
    {
        //No additional heating time is required.
        //float estimate_time = estimate_wipe_time(0);
        //int   extruder_id   = m_filament_map[m_current_tool] - 1;
        //float heat_time     = m_filpar[m_current_tool].filament_cooling_before_tower / m_hotend_heating_rate[extruder_id];
        //heat_time /= 2.f;
        //speed_factor = estimate_time / (heat_time+estimate_time);
        //wipe_speed *= speed_factor;
    }
    if (should_cooling_before_object) {
        int n = (cleaning_box.lu[1] - cleaning_box.ld[1]) / dy;
        int extruder_id = get_extruder_id(m_current_tool, m_cur_layer_id);
        float cooling_time     = (m_filpar[m_current_tool].filament_tower_interface_print_temp - m_filpar[m_current_tool].nozzle_temperature) / m_hotend_cooling_rate[extruder_id];
        if (n < 2) {
            float estimate_time = estimate_wipe_time(0);
            speed_factor        = estimate_time > cooling_time? 1: estimate_time / cooling_time;
            cooling_begin_line = 0;
        } else {
            float estimate_time = estimate_wipe_time(2);
            speed_factor        = estimate_time > cooling_time ? 1 : estimate_time / cooling_time;
            cooling_begin_line = 2; //TODO: No slowdown for the first two lines.
        }
        wipe_speed *= speed_factor;
    }


    for (int i = 0; true; ++i) {
        if (i < WipeSpeedMap.size()) wipe_speed = WipeSpeedMap[i] * speed_factor;

        bool need_change_flow = need_thick_bridge_flow(writer.y(), wipe_slab_height);
        // BBS: check the bridging area and use the bridge flow
        if (need_change_flow) {
            writer.set_extrusion_flow(extrusion_flow_for(0.2f, tool_width));
            writer.append(";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Height) + std::to_string(0.2) + "\n");
        }
        float flat_iron_area = m_filpar[m_current_tool].flat_iron_area;
        float ironing_length = 3.;

        if (should_cooling_before_object && i == cooling_begin_line) {
            writer.format_line_M104(m_filpar[m_current_tool].nozzle_temperature, this->m_current_tool,
                                    m_filpar[m_current_tool].nozzle_temperature > m_filpar[m_current_tool].filament_tower_interface_print_temp,
                                    true, "Wipe tower pre object cooling");
        }

        if (i == 0 && m_use_gap_wall) { // BBS: add ironing after extruding start
            if (m_left_to_right) {
                if (should_line_ironing) {
                    float dx = xr + wipe_tower_wall_infill_overlap * m_perimeter_width - writer.pos().x();
                    if (abs(dx) < ironing_length) ironing_length = abs(dx);
                    writer.extrude(writer.x() + ironing_length, writer.y(), wipe_speed);
                    writer.retract(retract_length, retract_speed);
                    writer.travel(writer.x() - 1.5 * ironing_length, writer.y(), 600.);
                    if (should_flat_ironging) {
                        writer.travel(writer.x() + 0.5f * ironing_length, writer.y(), 240.);
                        Vec2f pos{writer.x() + 1.f * ironing_length, writer.y()};
                        writer.spiral_flat_ironing(writer.pos(), flat_iron_area, tool_width, m_ironing_speed);
                        writer.travel(pos, wipe_speed);
                    } else
                        writer.travel(writer.x() + 1.5 * ironing_length, writer.y(), 240.);
                    writer.retract(-retract_length, retract_speed);
                }
                add_M104_by_requirement();
                writer.extrude(xr + wipe_tower_wall_infill_overlap * m_perimeter_width, writer.y(), wipe_speed);
            } else {
                if (should_line_ironing) {
                    float dx = xl - wipe_tower_wall_infill_overlap * m_perimeter_width - writer.pos().x();
                    if (abs(dx) < ironing_length) ironing_length = abs(dx);
                    writer.extrude(writer.x() - ironing_length, writer.y(), wipe_speed);
                    writer.retract(retract_length, retract_speed);
                    writer.travel(writer.x() + 1.5 * ironing_length, writer.y(), 600.);
                    if (should_flat_ironging) {
                        writer.travel(writer.x() - 0.5f * ironing_length, writer.y(), 240.);
                        Vec2f pos{writer.x() - 1.0f * ironing_length, writer.y()};
                        writer.spiral_flat_ironing(writer.pos(), flat_iron_area, tool_width, m_ironing_speed);
                        writer.travel(pos, wipe_speed);
                    } else
                        writer.travel(writer.x() - 1.5 * ironing_length, writer.y(), 240.);
                    writer.retract(-retract_length, retract_speed);
                }
                add_M104_by_requirement();
                writer.extrude(xl - wipe_tower_wall_infill_overlap * m_perimeter_width, writer.y(), wipe_speed);
            }
        } else {
            if (i == 0) add_M104_by_requirement();
            if (m_left_to_right)
                writer.extrude(xr + wipe_tower_wall_infill_overlap * m_perimeter_width, writer.y(), wipe_speed);
            else
                writer.extrude(xl - wipe_tower_wall_infill_overlap * m_perimeter_width, writer.y(), wipe_speed);
        }

        // BBS: recover the flow in non-bridging area
        if (need_change_flow) {
            writer.set_extrusion_flow(m_extrusion_flow);
            writer.append(";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Height) + std::to_string(wipe_slab_height) + "\n");
        }

        if (!is_from_up && (writer.y() + dy - float(EPSILON) >cleaning_box.lu.y() - tool_width))
            break; // in case next line would not fit

        if (is_from_up && (writer.y() - dy+ float(EPSILON))<cleaning_box.ld.y()) // Because the top of the clean box cannot have wiring, but the bottom can have wiring.
            break;

        x_to_wipe -= (xr - xl);
        if (x_to_wipe < WT_EPSILON) {
            // BBS: Delete some unnecessary travel
            // writer.travel(m_left_to_right ? xl + 1.5f*m_perimeter_width : xr - 1.5f*m_perimeter_width, writer.y(), 7200);
            break;
        }
        // stepping to the next line:
        if (is_from_up)
            writer.extrude(writer.x(), writer.y() - dy);
        else
            writer.extrude(writer.x(), writer.y() + dy);

        m_left_to_right = !m_left_to_right;
    }

    writer.add_wipe_point(writer.x(), writer.y()).add_wipe_point(!m_left_to_right ? m_wipe_tower_width : 0.f, writer.y());

    if (m_layer_info != m_plan.end() && m_current_tool != m_layer_info->tool_changes.back().new_tool) m_left_to_right = !m_left_to_right;

    writer.set_extrusion_flow(m_extrusion_flow); // Reset the extrusion flow.
    // BBS: add the note for gcode-check when the flow changed
    if (first_layer) { writer.append(";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Width) + std::to_string(tool_width) + "\n"); }
}

float WipeTower::min_layer_height_for_filament(int filament_id) const
{
    const float diameter = (filament_id >= 0 && size_t(filament_id) < m_filpar.size()) ? m_filpar[filament_id].nozzle_diameter : 0.f;
    return diameter > 0.f ? float(adaptive_layer_height_range(diameter).min_height) : 0.f;
}

float WipeTower::max_layer_height_for_filament(int filament_id) const
{
    const float diameter = (filament_id >= 0 && size_t(filament_id) < m_filpar.size()) ? m_filpar[filament_id].nozzle_diameter : 0.f;
    return diameter > 0.f ? float(adaptive_layer_height_range(diameter).max_height) : std::numeric_limits<float>::max();
}

bool WipeTower::filament_can_print_height(int filament_id, float height) const
{
    return height + WT_EPSILON >= min_layer_height_for_filament(filament_id) &&
           height <= max_layer_height_for_filament(filament_id) + WT_EPSILON;
}

size_t WipeTower::preferred_band_size_for_filament(int filament_id) const
{
    const float diameter = (filament_id >= 0 && size_t(filament_id) < m_filpar.size()) ?
                               m_filpar[filament_id].nozzle_diameter : 0.f;
    const float reference = m_tower_grid_reference_nozzle > 0.f ?
                                m_tower_grid_reference_nozzle : m_tower_grid_nozzle_diameter;
    return adaptive_layer_band_size(diameter, reference, m_tower_grid_layer_height);
}

float WipeTower::preferred_height_for_filament(int filament_id) const
{
    if (m_tower_grid_layer_height <= WT_EPSILON)
        return min_layer_height_for_filament(filament_id);
    return float(preferred_band_size_for_filament(filament_id)) * m_tower_grid_layer_height;
}

bool WipeTower::plan_band_merge_ladder(const std::vector<int> &printed, int first_step, int last_step, double bottom_z,
                                       const std::vector<std::vector<int>> &visitors, const std::vector<char> &reservable,
                                       const std::vector<char> &can_merge_top, std::vector<MergeLadderGroup> &groups,
                                       float preferred_height, bool prefer_max_height, float height_ceiling) const
{
    groups.clear();
    if (first_step > last_step)
        return true;
    const int span = last_step - first_step + 1;

    double widest = 0.;
    for (int filament_id = 0; filament_id < int(m_filpar.size()); ++filament_id)
        widest = std::max(widest, double(max_layer_height_for_filament(filament_id)));

    // The Z a group tops out at; below the first step of the range the caller tells us what is already there.
    auto step_z = [this, &printed, first_step, bottom_z](int step) {
        return step < first_step ? bottom_z : double(m_plan[printed[step]].z);
    };

    const double unreachable = std::numeric_limits<double>::max();
    std::vector<double> best(span + 1, unreachable);
    std::vector<int>    prev(span + 1, -1);
    std::vector<int>    chosen(span + 1, -1);
    best[0] = 0.;
    for (int end = 1; end <= span; ++end) {
        const int top_step = first_step + end - 1;
        for (int begin = end - 1; begin >= 0; --begin) {
            const bool merged = begin < end - 1;
            if (merged && !can_merge_top[top_step])
                break;
            // This iteration is the one handing printed[first_step + begin] over to the group above it.
            if (merged && !reservable[first_step + begin])
                break;
            const double height = step_z(top_step) - step_z(first_step + begin - 1);
            // A block keeps its own nozzle's printable maximum even when a larger nozzle is filling the
            // slot under it. Merging further past that is refused; a single step is kept only when the
            // extruding nozzle can print it.
            const double span_limit = height_ceiling > WT_EPSILON ? std::min(widest, double(height_ceiling)) : widest;
            if (merged && height > span_limit + WT_EPSILON)
                break;
            if (best[begin] == unreachable)
                continue;

            int    filament_id = -1;
            double penalty     = unreachable;
            for (int candidate : visitors[top_step]) {
                if (!filament_can_print_height(candidate, float(height)))
                    continue;
                double target_height;
                double candidate_penalty;
                if (prefer_max_height) {
                    // Distance under this nozzle's maximum, plus one per slab. A slab that removes a
                    // whole layer always beats a closer match split into the grid step.
                    target_height      = double(max_layer_height_for_filament(candidate));
                    candidate_penalty  = 1. + std::max(0., target_height - height);
                } else {
                    target_height = preferred_height > WT_EPSILON ?
                                        double(preferred_height) :
                                        double(preferred_band_size_for_filament(candidate)) *
                                            double(m_tower_grid_layer_height);
                    candidate_penalty = std::abs(height - target_height);
                }
                if (candidate_penalty + WT_EPSILON < penalty) {
                    filament_id = candidate;
                    penalty     = candidate_penalty;
                }
            }
            // A height no visiting nozzle can extrude is not a slab. Leaving it for later is how a
            // 0.4 mm nozzle was handed 0.1 mm, or a folded leftover grew past 0.7D.
            if (filament_id < 0)
                continue;

            // Minimize distance from each selected nozzle's fixed-grid height. This gives a 0.4 mm nozzle
            // 0.2 mm slabs and a 0.6 mm nozzle 0.3 mm slabs on a 0.1 mm base grid. prefer_max_height instead
            // pays one unit per slab, so the thickest legal slab wins and a leftover is not left at the grid
            // step. Folding afterwards stays inside the same window.
            const double cost = best[begin] + penalty;
            if (cost <= best[end] + WT_EPSILON) {
                best[end]   = cost;
                prev[end]   = begin;
                chosen[end] = filament_id;
            }
        }
    }

    if (prev[span] < 0)
        return false;

    for (int end = span; end > 0; end = prev[end]) {
        const int begin = prev[end];
        groups.push_back({ first_step + begin, first_step + end - 1, chosen[end],
                           float(step_z(first_step + end - 1) - step_z(first_step + begin - 1)) });
    }
    std::reverse(groups.begin(), groups.end());
    // Short groups join a neighbor only while the extruding nozzle can still print the result.
    fold_thin_ladder_groups(visitors, reservable, can_merge_top, groups, height_ceiling);
    return true;
}

void WipeTower::fold_thin_ladder_groups(const std::vector<std::vector<int>> &visitors, const std::vector<char> &reservable,
                                        const std::vector<char> &can_merge_top, std::vector<MergeLadderGroup> &groups,
                                        float height_ceiling) const
{
    auto span_is_reservable = [&reservable](int begin, int end) {
        for (int step = begin; step <= end; ++step)
            if (step < 0 || step >= int(reservable.size()) || !reservable[step])
                return false;
        return true;
    };
    auto pick_filament = [&](int top_step, float height) {
        int   best       = -1;
        float best_delta = std::numeric_limits<float>::max();
        if (top_step < 0 || top_step >= int(visitors.size()))
            return best;
        for (int candidate : visitors[top_step]) {
            if (!filament_can_print_height(candidate, height))
                continue;
            const float delta = std::abs(height - preferred_height_for_filament(candidate));
            if (delta + WT_EPSILON < best_delta) {
                best       = candidate;
                best_delta = delta;
            }
        }
        return best;
    };

    bool folded = true;
    while (folded) {
        folded = false;
        for (size_t i = 0; i < groups.size(); ++i) {
            const float preferred = preferred_height_for_filament(groups[i].filament_id);
            if (preferred <= WT_EPSILON || groups[i].height + WT_EPSILON >= preferred)
                continue;
            // A slab prints at its top and extends downward, so a short group can only join the group
            // above it, or grow downward over the group below.
            if (i + 1 < groups.size() && groups[i + 1].top_step >= 0 &&
                groups[i + 1].top_step < int(can_merge_top.size()) && can_merge_top[groups[i + 1].top_step] &&
                span_is_reservable(groups[i].begin_step, groups[i].top_step) &&
                !(height_ceiling > WT_EPSILON &&
                  groups[i].height + groups[i + 1].height > height_ceiling + WT_EPSILON)) {
                const float height   = groups[i].height + groups[i + 1].height;
                const int   filament = pick_filament(groups[i + 1].top_step, height);
                if (filament >= 0) {
                    groups[i + 1].begin_step  = groups[i].begin_step;
                    groups[i + 1].height      = height;
                    groups[i + 1].filament_id = filament;
                    groups.erase(groups.begin() + int(i));
                    folded = true;
                    break;
                }
            }
            if (i > 0 && groups[i].top_step >= 0 && groups[i].top_step < int(can_merge_top.size()) &&
                can_merge_top[groups[i].top_step] &&
                span_is_reservable(groups[i - 1].begin_step, groups[i - 1].top_step) &&
                !(height_ceiling > WT_EPSILON &&
                  groups[i - 1].height + groups[i].height > height_ceiling + WT_EPSILON)) {
                const float height   = groups[i - 1].height + groups[i].height;
                const int   filament = pick_filament(groups[i].top_step, height);
                if (filament >= 0) {
                    groups[i].begin_step  = groups[i - 1].begin_step;
                    groups[i].height      = height;
                    groups[i].filament_id = filament;
                    groups.erase(groups.begin() + int(i - 1));
                    folded = true;
                    break;
                }
            }
        }
    }
}

int WipeTower::get_block_key(int filament_id, int layer_id) const
{
    const int category = (filament_id >= 0 && size_t(filament_id) < m_filament_categories.size()) ? m_filament_categories[filament_id] : 0;
    const float diameter = (filament_id >= 0 && size_t(filament_id) < m_filpar.size()) ? m_filpar[filament_id].nozzle_diameter : 0.f;
    int lane = 0;
    if (layer_id >= 0 && size_t(layer_id) < m_lane_assignment.size()) {
        const auto iter = m_lane_assignment[layer_id].find(filament_id);
        if (iter != m_lane_assignment[layer_id].end())
            lane = iter->second;
    }
    return encode_block_key(category, diameter, lane);
}

WipeTower::WipeTowerBlock * WipeTower::get_block_by_category(int block_key, bool create)
{
    auto iter = std::find_if(m_wipe_tower_blocks.begin(), m_wipe_tower_blocks.end(), [&block_key](const WipeTower::WipeTowerBlock &item) {
        return item.block_key == block_key;
    });

    if (iter != m_wipe_tower_blocks.end()) {
        return &(*iter);
    }

    if (create) {
        WipeTower::WipeTowerBlock new_block;
        new_block.block_id = m_wipe_tower_blocks.size();
        new_block.block_key = block_key;
        new_block.filament_adhesiveness_category = block_key >> 10;
        m_wipe_tower_blocks.emplace_back(new_block);
        return &m_wipe_tower_blocks.back();
    }

    return nullptr;
}

void WipeTower::add_depth_to_block(int filament_id, int block_key, float depth, bool is_nozzle_change)
{
    std::vector<WipeTower::BlockDepthInfo> &layer_depth = m_all_layers_depth[m_cur_layer_id];
    auto iter = std::find_if(layer_depth.begin(), layer_depth.end(), [&block_key](const WipeTower::BlockDepthInfo &item) {
        return item.category == block_key;
    });

    if (iter != layer_depth.end()) {
        iter->depth += depth;
        if (is_nozzle_change)
            iter->nozzle_change_depth += depth;
    }
    else {
        WipeTower::BlockDepthInfo new_block;
        new_block.category = block_key;
        new_block.depth = depth;
        if (is_nozzle_change)
            new_block.nozzle_change_depth += depth;
        layer_depth.emplace_back(std::move(new_block));
    }
}

int WipeTower::get_filament_category(int filament_id) const
{
    if (filament_id >= m_filament_categories.size())
        return 0;
    return m_filament_categories[filament_id];
}

void WipeTower::reset_block_status()
{
    for (auto &block : m_wipe_tower_blocks) {
        block.cur_depth = block.start_depth;
        block.last_filament_change_id = -1;
        block.last_nozzle_change_id   = -1;
    }
}
void WipeTower::set_nozzle_last_layer_id()
{
    for (int idx = 0; idx < m_plan.size(); idx++) {
        auto &info = m_plan[idx];
        for(int i =0 ; i<info.tool_changes.size();i++) {
            int old_tool = info.tool_changes[i].old_tool;
            int new_tool = info.tool_changes[i].new_tool;
            if (old_tool >= 0) m_last_layer_id[get_extruder_id(old_tool, idx)] = idx;
            m_last_layer_id[get_extruder_id(new_tool, idx)] = idx;
        }
    }
}

void WipeTower::set_first_layer_flow_ratio(const float flow_ratio)
{
    m_first_layer_flow_ratio = flow_ratio;
}

void WipeTower::update_all_layer_depth(float wipe_tower_depth)
{
    m_wipe_tower_depth = 0.f;
    float start_offset = m_perimeter_width;
    float start_depth = start_offset;
    for (auto& block : m_wipe_tower_blocks) {
        block.depth *= m_extra_spacing;
        block.start_depth = start_depth;
        start_depth += block.depth;
        m_wipe_tower_depth += block.depth;

        for (auto& layer_depth : block.layer_depths) {
            layer_depth *= m_extra_spacing;
        }
    }
    // Scale each layer once. Doing this inside the block loop raised the spacing to the power of the block
    // count, which splitting the bands per nozzle diameter makes far more visible.
    for (WipeTowerInfo& plan_info : m_plan) {
        plan_info.depth *= m_extra_spacing;
    }
    if (m_wipe_tower_depth > 0)
        m_wipe_tower_depth += start_offset;

    // Keep every layer at the full tower depth when the outer walls must stay put for the diagonal
    // scrape (or when wrapping / smooth timelapse already needs a tower on every layer).
    if (m_enable_wrapping_detection || m_enable_timelapse_print || keeps_outer_walls_unshrunk()) {
        if (is_approx(m_wipe_tower_depth, 0.f))
            m_wipe_tower_depth = wipe_tower_depth;
        for (WipeTowerInfo &plan_info : m_plan) {
            plan_info.depth = m_wipe_tower_depth;
        }
    }
}

void WipeTower::generate_wipe_tower_blocks(bool add_solid_flag)
{
    // 1. generate all layer depth
    m_all_layers_depth.clear();
    m_all_layers_depth.resize(m_plan.size());
    m_cur_layer_id = 0;
    for (auto& info : m_plan) {
        for (const WipeTowerInfo::ToolChange &tool_change : info.tool_changes) {
            if (!is_need_ramming(tool_change.old_tool, tool_change.new_tool, m_cur_layer_id)) {
                add_depth_to_block(tool_change.new_tool, get_block_key(tool_change.new_tool, m_cur_layer_id), tool_change.required_depth);
            }
            else {
                add_depth_to_block(tool_change.old_tool, get_block_key(tool_change.old_tool, m_cur_layer_id), tool_change.nozzle_change_depth, true);
                add_depth_to_block(tool_change.new_tool, get_block_key(tool_change.new_tool, m_cur_layer_id), tool_change.required_depth - tool_change.nozzle_change_depth);

            }
        }
        ++m_cur_layer_id;
    }

    // 2. generate all layer depth
    std::vector<std::unordered_map<int, float>> all_layer_category_to_depth(m_plan.size());
    for (size_t layer_id = 0; layer_id < m_all_layers_depth.size(); ++layer_id) {
        const auto& layer_blocks = m_all_layers_depth[layer_id];
        std::unordered_map<int, float> &category_to_depth = all_layer_category_to_depth[layer_id];
        for (auto block : layer_blocks) {
            category_to_depth[block.category] = block.depth;
        }
    }

    // 3. generate wipe tower block
    m_wipe_tower_blocks.clear();
    for (int layer_id = 0; layer_id < all_layer_category_to_depth.size(); ++layer_id) {
        const auto &layer_category_depths = all_layer_category_to_depth[layer_id];
        // Creation order decides where each band sits along Y, so walk the keys in a stable order instead
        // of the hash order. Sorting by key also keeps the lanes of one category and diameter adjacent.
        std::vector<int> ordered_keys;
        ordered_keys.reserve(layer_category_depths.size());
        for (const auto &entry : layer_category_depths)
            ordered_keys.push_back(entry.first);
        std::sort(ordered_keys.begin(), ordered_keys.end());
        for (int key : ordered_keys) {
            auto iter = layer_category_depths.find(key);
            auto* block = get_block_by_category(iter->first, true);
            if (block->layer_depths.empty()) {
                block->layer_depths.resize(all_layer_category_to_depth.size(), 0);
                block->finish_depth.resize(all_layer_category_to_depth.size(), 0);
                block->layers_type.resize(all_layer_category_to_depth.size(), WipeTowerLayerType::Normal);
                block->layer_heights.resize(all_layer_category_to_depth.size(), 0.f);
                block->layer_reserved.resize(all_layer_category_to_depth.size(), 0);
                block->layer_fill_filament.resize(all_layer_category_to_depth.size(), -1);
            }
            block->depth = std::max(block->depth, iter->second);
            block->layer_depths[layer_id] = iter->second;
        }
    }
    // 4. get real depth for every layer
    for (int layer_id = m_plan.size() - 1; layer_id >= 0; --layer_id) {
        m_plan[layer_id].depth = 0;
        for (auto& block : m_wipe_tower_blocks) {
            if (layer_id < m_plan.size() - 1)
                block.layer_depths[layer_id] = std::max(block.layer_depths[layer_id], block.layer_depths[layer_id + 1]);
            m_plan[layer_id].depth += block.layer_depths[layer_id];
        }
    }

    if (m_tower_framework) {
        for (int layer_id = 1; layer_id < m_plan.size(); ++layer_id) {
            m_plan[layer_id].depth = 0;
            for (auto &block : m_wipe_tower_blocks) {
                block.layer_depths[layer_id] = block.layer_depths[0];
                m_plan[layer_id].depth += block.layer_depths[layer_id];
            }
        }
    }

        // add solid infill flag
    if (add_solid_flag) {
        int solid_infill_layer_low = 4;
        // With merged fill, Contact and Solid both follow the adhesiveness category, not the full block key.
        // Bands that only differ in nozzle diameter or lane are the same material: keying either test by the
        // block key flips occupancy whenever the nozzle or the lane changes and irons that band solid. Solid is
        // the dense floor under a band that is wider above, and only when the category there is not the one
        // already on this layer. The same category bridges with the sparse empty grid.
        // Without merged fill a band may be filled by whatever is loaded, so Solid keeps the per-band key
        // and each layer derives its categories from the running tool or that layer's tool changes.
        const bool merged_fill = merged_fill_active();
        std::vector<std::unordered_set<int>> layers_used_tools;
        std::vector<std::unordered_set<int>> layers_used_categories;

        int first_tool = -1;
        for (const auto &layer : m_plan) {
            if (!layer.tool_changes.empty()) {
                first_tool = layer.tool_changes.front().old_tool;
                break;
            }
        }
        for (int layer_id = 0; layer_id < (int) m_plan.size(); ++layer_id) {
            const auto &info = m_plan[layer_id];
            std::unordered_set<int> used_tools;
            if (info.tool_changes.empty()) {
                used_tools.insert(get_block_key(first_tool, layer_id));
            } else {
                for (const WipeTowerInfo::ToolChange &tool_change : info.tool_changes) {
                    used_tools.insert(get_block_key(tool_change.old_tool, layer_id));
                    used_tools.insert(get_block_key(tool_change.new_tool, layer_id));
                }
                first_tool = info.tool_changes.back().new_tool;
            }
            // Which material families the tower hosts on this layer. A band that is still standing was not
            // removed just because this layer's tool-change list does not name it: the slot keeps being
            // filled with the same family. Dropping the family here makes the next purge look like a
            // material change and packs its wipe lines solid. A family leaves the set only when its column
            // has ended. Adaptive Z grouping leaves nearly half the layers without a tool change, and a
            // multi-diameter layer often changes only one of the bands, so both have to inherit.
            std::unordered_set<int> used_categories;
            if (layer_id > 0 && merged_fill)
                used_categories = layers_used_categories[layer_id - 1];
            if (merged_fill && !info.tool_changes.empty()) {
                std::unordered_set<int> kept;
                for (int category : used_categories) {
                    const bool column_continues = std::any_of(m_wipe_tower_blocks.begin(), m_wipe_tower_blocks.end(),
                        [category, layer_id, this](const WipeTowerBlock &block) {
                            return decode_block_category(block.block_key) == category &&
                                   layer_id < (int) block.layer_depths.size() &&
                                   block.layer_depths[layer_id] > 2.f * m_perimeter_width;
                        });
                    if (column_continues)
                        kept.insert(category);
                }
                used_categories.swap(kept);
            }
            for (int key : used_tools)
                used_categories.insert(decode_block_category(key));
            layers_used_tools.push_back(std::move(used_tools));
            layers_used_categories.push_back(std::move(used_categories));
        }

        for (WipeTowerBlock &block : m_wipe_tower_blocks) {
            const int block_category = decode_block_category(block.block_key);
            auto      solid_has_band = [&](int layer_id) {
                return merged_fill ? layers_used_categories[layer_id].count(block_category) > 0 :
                                     layers_used_tools[layer_id].count(block.block_key) > 0;
            };
            for (int layer_id = 0; layer_id < all_layer_category_to_depth.size(); ++layer_id) {
                std::unordered_map<int, float> &category_to_depth = all_layer_category_to_depth[layer_id];
                if (category_to_depth[block.block_key] < block.layer_depths[layer_id] - m_perimeter_width) {
                    const bool cur_has_category = solid_has_band(layer_id);
                    for (int layer_count = solid_infill_layer_low; layer_count > 0; --layer_count) {
                        if (layer_id + layer_count >= int(all_layer_category_to_depth.size()))
                            continue;
                        const bool up_has_category = solid_has_band(layer_id + layer_count);
                        if (cur_has_category != up_has_category) {
                            block.layers_type[layer_id] = WipeTowerLayerType::Solid;
                            break;
                        }
                    }
                }
                if (layer_id > 0) {
                    bool cur_has_block_category = layers_used_categories[layer_id].count(block_category);
                    bool pre_has_block_category = layers_used_categories[layer_id - 1].count(block_category);
                    if (cur_has_block_category != pre_has_block_category) { block.layers_type[layer_id] = WipeTowerLayerType::Contact; }
                    if (block.layers_type[layer_id - 1] == WipeTowerLayerType::Contact && block.layers_type[layer_id] != WipeTowerLayerType::Contact) {
                        block.layers_type[layer_id] = WipeTowerLayerType::Contact_UP;
                    }
                }
            }
        }
    }

    // Carry the slab plan onto the blocks now that they exist. A band runs at whatever the whole tower lays
    // down on that layer unless it digs its own trench, and a layer the tower drops leaves every band empty.
    for (WipeTowerBlock &block : m_wipe_tower_blocks) {
        for (int layer_id = 0; layer_id < (int) m_plan.size(); ++layer_id) {
            block.layer_heights[layer_id]  = layer_slab_height(layer_id, m_plan[layer_id].height);
            block.layer_reserved[layer_id] = (layer_is_skipped(layer_id) ||
                                              (layer_id < (int) m_reserved_blocks.size() && m_reserved_blocks[layer_id].count(block.block_key))) ? 1 : 0;
            if (layer_id < (int) m_block_slab_height.size()) {
                const auto iter = m_block_slab_height[layer_id].find(block.block_key);
                if (iter != m_block_slab_height[layer_id].end() && iter->second > 0.f)
                    block.layer_heights[layer_id] = iter->second;
            }
        }
    }
}
void WipeTower::calc_block_infill_gap()
{
    //1.calc block infill gap width
    struct BlockInfo
    {
        bool has_ramming = false;
        bool has_reverse_travel = false;
        float depth              = 0.f;
        // Line widths of the nozzle owning this group; the gaps below are derived from them.
        float perimeter_width = 0.f;
        float nozzle_change_perimeter_width = 0.f;
    };
    // Keyed by the lane-independent block key: all lanes of one (category, diameter) group share the gaps.
    std::unordered_map<int, BlockInfo> block_info;
    std::unordered_map<int, BlockInfo> high_block_info;
    for (int i= (int)m_plan.size()-1;i>=0;i--)
    {
        for (auto &toolchange : m_plan[i].tool_changes) {
            int new_tool =toolchange.new_tool;
            int old_tool =toolchange.old_tool;
            const int old_key = get_gap_key(old_tool);
            const int new_key = get_gap_key(new_tool);
            if (is_need_ramming(old_tool,new_tool, i)) {
                bool extruder_change = !is_same_extruder(new_tool, old_tool, i);
                block_info[old_key].has_ramming=true;
                block_info[old_key].perimeter_width = filament_perimeter_width(old_tool);
                block_info[old_key].nozzle_change_perimeter_width = filament_nozzle_change_width(old_tool);
                if (is_need_reverse_travel(old_tool, extruder_change)) block_info[old_key].has_reverse_travel = true;
                block_info[old_key].depth += toolchange.nozzle_change_depth;
            }
            if (!block_info.count(new_key)) block_info.insert({new_key,BlockInfo{}});
            block_info[new_key].perimeter_width = filament_perimeter_width(new_tool);
            block_info[new_key].nozzle_change_perimeter_width = filament_nozzle_change_width(new_tool);
            block_info[new_key].depth += toolchange.required_depth - toolchange.nozzle_change_depth;
        }
        for (auto &block : block_info) {
            if (high_block_info.count(block.first) && high_block_info[block.first].depth > block.second.depth)
                block.second.depth = high_block_info[block.first].depth;
        }
        high_block_info = block_info;

        for (auto &block : block_info) { block.second.depth = 0.f;}
        if (i == 0) block_info = high_block_info;
    }
    float max_depth = std::accumulate(block_info.begin(), block_info.end(), 0.f, [](float value, const std::pair<int,BlockInfo> &block) { return value + block.second.depth; });
    float height_to_depth = get_limit_depth_by_height(m_wipe_tower_height);
    float height_to_spacing = max_depth > height_to_depth ? 1.f : height_to_depth / max_depth;

    float spacing_ratio = m_extra_spacing - 1.f;
    for (auto &info : block_info) {
        // Every band is spaced by the line width of the nozzle that fills it, not by the tower-wide value.
        const float band_width    = info.second.perimeter_width > 0.f ? info.second.perimeter_width : m_perimeter_width;
        const float band_nc_width = info.second.nozzle_change_perimeter_width > 0.f ? info.second.nozzle_change_perimeter_width :
                                                                                     m_nozzle_change_perimeter_width;
        const float extra_width   = spacing_ratio * band_width;
        const float line_gap_tol  = 2.f * band_nc_width; //If the block's line_gap is greater than it, the block should be aligned.
        //case1: no ramming, it can always align
        if (!info.second.has_ramming) {
            m_block_infill_gap_width[info.first].first = m_block_infill_gap_width[info.first].second = extra_width + band_width;
        }
        // case2: has ramming, but no reverse travel
        //
        else if (!info.second.has_reverse_travel) {
            float line_gap                              = band_nc_width + extra_width;
            if (!m_use_rib_wall) line_gap *= height_to_spacing;
            if (line_gap < line_gap_tol) {
                m_block_infill_gap_width[info.first].first  = band_width + extra_width;
                m_block_infill_gap_width[info.first].second = band_nc_width + extra_width;
            } else {
                m_block_infill_gap_width[info.first].first = m_block_infill_gap_width[info.first].second = band_nc_width + extra_width;
            }
        }
        // case 3: has ramming and reverse travel
        else {
            float extra_tpu_fix_spacing = m_tpu_fixed_spacing - 1.f;
            float line_gap = band_nc_width + std::max(extra_tpu_fix_spacing * band_width, extra_width);
            if (!m_use_rib_wall) line_gap = height_to_spacing * line_gap;
            if (line_gap < line_gap_tol) {
                m_block_infill_gap_width[info.first].first  = band_width + extra_width;
                m_block_infill_gap_width[info.first].second = band_nc_width + std::max(extra_tpu_fix_spacing * band_width, extra_width);
            } else {
                m_block_infill_gap_width[info.first].first = m_block_infill_gap_width[info.first].second = band_nc_width +
                                                                                                           std::max(extra_tpu_fix_spacing * band_width, extra_width);
            }
        }
    }

    //2. recalculate toolchange depth
     for (int idx = 0; idx < m_plan.size(); idx++) {
        for (auto &toolchange : m_plan[idx].tool_changes) {
            toolchange = set_toolchange(toolchange.old_tool, toolchange.new_tool, m_plan[idx].height, toolchange.wipe_volume, toolchange.purge_volume,idx);
        }
     }
     m_extra_spacing = 1.f;
}

bool WipeTower::has_multiple_nozzle_diameters() const
{
    // Whether the print carries more than one diameter is decided once, by Print, over the filaments it
    // actually uses. Scanning m_filpar here would let the tower and the model slabs disagree.
    return m_multi_diameter_print;
}

// Hand a run of tool-change-free layers to the tool-change layer right above it. A run the loaded nozzle
// cannot lay down on its own has no purge to move, so the tower prints nothing on it and the layer above
// extrudes the whole stack at once. That layer can only grow downwards - its own purge is pinned to its Z -
// and only as far as every nozzle extruding there tolerates.
bool WipeTower::absorb_run_into_next_layer(int run_begin, int run_end)
{
    // A run only ends on a tool-change layer or at the top of the tower. Absorbing into a layer that carries
    // no tool change would hand the run to a layer the ladder still has to plan as a run of its own.
    const int above = run_end + 1;
    if (above >= int(m_plan.size()) || m_plan[above].tool_changes.empty())
        return false;

    float grown = m_plan[above].height;
    for (int layer_id = run_begin; layer_id <= run_end; ++layer_id)
        grown += m_plan[layer_id].height;

    // The incoming filament purges into its band on that layer, and the outgoing one rams there when a nozzle
    // change is needed. Both would have to lay the grown slab down, so neither may be left out of its window.
    for (const WipeTowerInfo::ToolChange &tool_change : m_plan[above].tool_changes) {
        if (! filament_can_print_height(int(tool_change.new_tool), grown))
            return false;
        if (is_need_ramming(int(tool_change.old_tool), int(tool_change.new_tool), above) &&
            ! filament_can_print_height(int(tool_change.old_tool), grown))
            return false;
    }

    for (int layer_id = run_begin; layer_id <= run_end; ++layer_id) {
        m_layer_skipped[layer_id]     = 1;
        m_layer_slab_height[layer_id] = 0.f;
    }
    m_layer_slab_height[above] = grown;
    m_merged_slabs_active      = true;
    return true;
}

// A no-tool-change layer contains one material and one full-tower patch, so it follows the same Z grouping
// as a spatially uniform model patch. Keep the raw foundation only when real printing also occurs at that Z;
// otherwise merge it upwards so a tower-only layer never forces a nozzle below its printable height.
void WipeTower::plan_tower_ladder()
{
    const int layer_count = int(m_plan.size());
    m_layer_slab_height.assign(layer_count, 0.f);
    m_layer_skipped.assign(layer_count, 0);
    m_merged_slabs_active = false;
    for (int layer_id = 0; layer_id < layer_count; ++layer_id)
        m_layer_slab_height[layer_id] = m_plan[layer_id].height;

    // With one diameter every layer is printable by whatever fills it, so leave the tower untouched.
    // Skipping sparse layers collapses the tower's z ladder after the fact (see
    // compute_compacted_wipe_tower_z); that ladder and this one cannot both be applied. Smooth timelapse
    // and wrapping detection both need the tower present on every single layer, so they rule dropping out.
    if (!merged_fill_active())
        return;

    std::vector<int> loaded_filament(layer_count, -1);
    int loaded = -1;
    for (const WipeTowerInfo &info : m_plan)
        if (!info.tool_changes.empty()) { loaded = int(info.tool_changes.front().old_tool); break; }
    for (int layer_id = 0; layer_id < layer_count; ++layer_id) {
        for (const WipeTowerInfo::ToolChange &tool_change : m_plan[layer_id].tool_changes)
            loaded = int(tool_change.new_tool);
        loaded_filament[layer_id] = loaded;
    }

    const int first_layer = m_first_layer_idx == size_t(-1) ? 0 : int(m_first_layer_idx);
    std::vector<double> heights;
    heights.reserve(m_plan.size());
    for (const WipeTowerInfo &info : m_plan)
        heights.push_back(info.height);

    int run_begin = m_foundation_required ? first_layer + 1 : first_layer;
    while (run_begin < layer_count) {
        while (run_begin < layer_count && !m_plan[run_begin].tool_changes.empty())
            ++run_begin;
        if (run_begin >= layer_count)
            break;

        int run_end = run_begin;
        while (run_end + 1 < layer_count && m_plan[run_end + 1].tool_changes.empty() &&
               loaded_filament[run_end + 1] == loaded_filament[run_begin])
            ++run_end;

        // Before any real first-layer extrusion exists, there is no tower surface worth printing on its
        // own. Prefer handing that complete prefix to the first required tool change; this makes an all-0.6
        // print start with one 0.3 mm foundation instead of standalone 0.1/0.2 mm tower layers.
        if (!m_foundation_required && run_begin == first_layer &&
            absorb_run_into_next_layer(run_begin, run_end)) {
            run_begin = run_end + 1;
            continue;
        }

        const int filament_id = loaded_filament[run_begin];
        if (filament_id >= 0) {
            // A sparse run is one material. Plan it on that nozzle's own band, inside its printable
            // window, instead of leaving every grid step at the minimum or capping it by some other
            // nozzle that is not extruding here.
            std::vector<std::pair<size_t, size_t>> groups;
            const size_t band_size = preferred_band_size_for_filament(filament_id);
            const AdaptiveLayerHeightRange range {
                min_layer_height_for_filament(filament_id),
                max_layer_height_for_filament(filament_id)
            };
            auto apply_group = [&](const std::pair<size_t, size_t> &group) {
                float merged_height = 0.f;
                for (size_t layer_id = group.first; layer_id <= group.second; ++layer_id)
                    merged_height += m_plan[layer_id].height;
                for (size_t layer_id = group.first; layer_id < group.second; ++layer_id) {
                    m_layer_skipped[layer_id] = 1;
                    m_layer_slab_height[layer_id] = 0.f;
                }
                m_layer_slab_height[group.second] = merged_height;
                if (group.first != group.second)
                    m_merged_slabs_active = true;
                BOOST_LOG_TRIVIAL(debug)
                    << "[wt_ladder] layers " << group.first << ".." << group.second
                    << " -> print at layer " << group.second << " z=" << m_plan[group.second].z
                    << " span=(" << (m_plan[group.second].z - merged_height) << ".." << m_plan[group.second].z
                    << "] h=" << merged_height << " filament=" << filament_id;
            };
            auto span_height = [&](size_t bottom, size_t top) {
                float height = 0.f;
                for (size_t layer_id = bottom; layer_id <= top; ++layer_id)
                    height += m_plan[layer_id].height;
                return height;
            };
            const float preferred = preferred_height_for_filament(filament_id);
            // Stay inside the printable window. Folding up to one nozzle diameter is what pushed a sparse
            // slab past the nozzle that has to lay it down.
            const float ceiling   = max_layer_height_for_filament(filament_id);
            // A group shorter than this nozzle's band joins a neighbor when the result still fits the window,
            // so a coarse nozzle is not left on the raw grid height.
            auto fold_short_groups = [&]() {
                bool folded = true;
                while (folded) {
                    folded = false;
                    for (size_t i = 0; i < groups.size(); ++i) {
                        if (span_height(groups[i].first, groups[i].second) + WT_EPSILON >= preferred)
                            continue;
                        if (i + 1 < groups.size() &&
                            span_height(groups[i].first, groups[i + 1].second) <= ceiling + WT_EPSILON) {
                            groups[i + 1].first = groups[i].first;
                            groups.erase(groups.begin() + int(i));
                            folded = true;
                            break;
                        }
                        if (i > 0 &&
                            span_height(groups[i - 1].first, groups[i].second) <= ceiling + WT_EPSILON) {
                            groups[i].first = groups[i - 1].first;
                            groups.erase(groups.begin() + int(i - 1));
                            folded = true;
                            break;
                        }
                    }
                }
            };
            // The tool-change layer above this run is pinned to its own Z and would otherwise stay on the
            // grid step. Reserve a suffix of the run for it before the split, so the split cannot spend
            // that suffix on a finished band and leave the purge at 0.1 mm.
            int   suffix = run_end + 1;
            float suffix_limit = ceiling;
            const int above = run_end + 1;
            if (above < layer_count && !m_plan[above].tool_changes.empty()) {
                float target = 0.f;
                suffix_limit = std::numeric_limits<float>::max();
                for (const WipeTowerInfo::ToolChange &tool_change : m_plan[above].tool_changes) {
                    const int new_tool = int(tool_change.new_tool);
                    // Stay inside the printable window. Growing to one nozzle diameter lets a 0.4 mm
                    // TPU nozzle be handed 0.4 mm, past its 0.28 mm maximum, and leaves the layers
                    // under that slab empty.
                    target       = std::max(target, preferred_height_for_filament(new_tool));
                    suffix_limit = std::min(suffix_limit, max_layer_height_for_filament(new_tool));
                    if (is_need_ramming(int(tool_change.old_tool), new_tool, above)) {
                        const int old_tool = int(tool_change.old_tool);
                        target       = std::max(target, preferred_height_for_filament(old_tool));
                        suffix_limit = std::min(suffix_limit, max_layer_height_for_filament(old_tool));
                    }
                }
                target = std::min(target, suffix_limit);
                float grown = m_plan[above].height;
                if (grown + WT_EPSILON < target) {
                    for (int layer_id = run_end; layer_id >= run_begin; --layer_id) {
                        const float next = grown + m_plan[layer_id].height;
                        if (next > suffix_limit + WT_EPSILON)
                            break;
                        grown  = next;
                        suffix = layer_id;
                        if (grown + WT_EPSILON >= target)
                            break;
                    }
                    // A suffix that misses the full band is still worth keeping when it lifts the purge
                    // off the raw grid step. Missing the target entirely leaves the grid step in place.
                    if (!(suffix <= run_end && grown + WT_EPSILON > m_plan[above].height))
                        suffix = run_end + 1;
                }
            }
            const int plan_end = suffix - 1;
            const bool split_ok = plan_end >= run_begin &&
                                  split_adaptive_layer_span(heights, size_t(run_begin), size_t(plan_end), band_size, range, groups);
            if (plan_end < run_begin) {
                if (!absorb_run_into_next_layer(run_begin, run_end))
                    BOOST_LOG_TRIVIAL(warning) << "WipeTower: cannot hand layers " << run_begin << ".." << run_end
                                               << " to the tool change above them; keeping their base layer heights";
            } else if (split_ok) {
                fold_short_groups();
                std::pair<size_t, size_t> pulled_tail;
                bool                      pulled = false;
                if (!groups.empty() && suffix <= run_end) {
                    const float tail = span_height(groups.back().first, groups.back().second);
                    const float grown_tail = span_height(size_t(suffix), size_t(run_end)) + m_plan[above].height;
                    if (tail + WT_EPSILON < preferred && grown_tail + tail <= suffix_limit + WT_EPSILON) {
                        pulled_tail = groups.back();
                        pulled      = true;
                        suffix      = int(groups.back().first);
                        groups.pop_back();
                    }
                }
                for (const auto &group : groups)
                    apply_group(group);
                if (suffix <= run_end && !absorb_run_into_next_layer(suffix, run_end)) {
                    if (pulled)
                        apply_group(pulled_tail);
                    BOOST_LOG_TRIVIAL(warning) << "WipeTower: cannot thicken the tool change at layer " << above
                                               << " with layers " << suffix << ".." << run_end
                                               << "; keeping their base layer heights";
                }
            } else if (!absorb_run_into_next_layer(run_begin, run_end)) {
                BOOST_LOG_TRIVIAL(warning) << "WipeTower: cannot apply the model Z merge plan to layers "
                                           << run_begin << ".." << run_end << " with filament " << filament_id
                                           << "; keeping their base layer heights";
            }
        }
        run_begin = run_end + 1;
    }
}

int WipeTower::first_printed_layer_idx() const
{
    const int first = m_first_layer_idx == size_t(-1) ? 0 : int(m_first_layer_idx);
    int printed = first;
    while (printed < int(m_plan.size()) && layer_is_skipped(printed))
        ++printed;
    return printed < int(m_plan.size()) ? printed : first;
}

bool WipeTower::merged_fill_active() const
{
    return !m_plan.empty() && has_multiple_nozzle_diameters() && !m_sparse_layers_skipped &&
           !m_enable_timelapse_print && !m_enable_wrapping_detection;
}

// Lanes of one group share a nozzle and a material, so the band's own group comes first; otherwise a band is
// stacked on, or under, a material it does not bond to.
int WipeTower::same_category_filler(const WipeTowerInfo &layer, int block_key) const
{
    const int group_key       = block_key & ~0xF;
    const int block_category  = decode_block_category(block_key);
    int       group_filler    = -1;
    int       category_filler = -1;
    // Every tool that touches the layer counts, including the one being switched away from.
    // A layer that starts on PETG and changes to PLA still has PETG, and the PETG block has to
    // be filled with it rather than with the incoming material.
    auto consider = [&](int filament) {
        if (filament < 0)
            return;
        if (get_gap_key(filament) == group_key)
            group_filler = filament;
        else if (category_filler < 0 && get_filament_category(filament) == block_category)
            category_filler = filament;
    };
    for (const WipeTowerInfo::ToolChange &tool_change : layer.tool_changes) {
        consider(int(tool_change.old_tool));
        consider(int(tool_change.new_tool));
    }
    return group_filler != -1 ? group_filler : category_filler;
}

int WipeTower::loaded_filament(int layer_id) const
{
    for (int l = std::min(layer_id, int(m_plan.size())) - 1; l >= 0; --l)
        if (!m_plan[l].tool_changes.empty())
            return int(m_plan[l].tool_changes.back().new_tool);
    return -1;
}

// On top of the tower-wide ladder a single band still has to dig its own trench when a large nozzle purges
// on a step the ladder kept thin, typically because the layer below it also carried a purge. The band stays
// empty on the steps the slab reaches into and the purge is printed at the merged height. Bands of the same
// category and diameter are split into parallel lanes so that purges on consecutive steps can each own their
// own reservation instead of competing for the same one.
void WipeTower::plan_merged_slabs()
{
    m_lane_assignment.assign(m_plan.size(), {});
    m_block_slab_height.assign(m_plan.size(), {});
    m_reserved_blocks.assign(m_plan.size(), {});

    // With one diameter every event already fits into its own layer, so leave the tower untouched.
    if (!merged_fill_active())
        return;

    // Trenches are dug through the tower's own z ladder, so the layers it does not print at all are not
    // steps a slab can reach down into.
    std::vector<int> printed;
    collect_printed_steps(printed);
    if (printed.empty())
        return;
    const double bed_z = tower_bed_z();

    enum : unsigned char { CellFree = 0, CellPurge, CellReserved };
    constexpr int max_lanes           = 4;  // more lanes means a deeper tower, so keep the budget tight
    constexpr int max_encodable_lanes = 16; // the lane field of the block key is 4 bits wide
    constexpr int max_merge_layers    = 8;
    constexpr int unfillable          = std::numeric_limits<int>::max() / 2;
    const int     step_count          = int(printed.size());

    auto top_z = [&](int step) { return step < 0 ? float(bed_z) : m_plan[printed[step]].z; };
    // A lane can host the event if the purge step is not reserved by a slab above and every step the slab
    // reaches into is still completely untouched.
    auto lane_fits = [](const std::vector<unsigned char> &lane, int step, int merge_layers) {
        if (lane[step] == CellReserved)
            return false;
        for (int l = step - merge_layers + 1; l < step; ++l)
            if (lane[l] != CellFree)
                return false;
        return true;
    };

    // Every filament extruding into a band on a layer: the incoming one wipes there, and the outgoing one
    // rams there when a nozzle change is needed. A nozzle change does not have to ram: with
    // filament_change_length at zero there is no ramming footprint at all, only an idle cooling travel. Such an
    // event lays nothing down in the outgoing band, so it must not pin that band's Z - the band would be stuck
    // one base layer above its own last purge, and the step above it then has nowhere to merge to.
    // block_fill_filament_base() already draws the line at the same place. The depth is the footprint the
    // event claims at the base layer height.
    struct GroupEvent
    {
        int              step = 0;
        std::vector<int> filaments;
        float            base_depth = 0.f;
    };
    std::map<int, std::vector<GroupEvent>> group_events;
    std::map<int, std::set<int>>           category_groups;
    for (int step = 0; step < step_count; ++step) {
        const int                 layer_id = printed[step];
        std::map<int, GroupEvent> on_layer;
        for (const WipeTowerInfo::ToolChange &tool_change : m_plan[layer_id].tool_changes) {
            const int new_tool = int(tool_change.new_tool);
            const int old_tool = int(tool_change.old_tool);
            GroupEvent &wipe = on_layer[get_gap_key(new_tool)];
            wipe.filaments.push_back(new_tool);
            wipe.base_depth += std::max(0.f, tool_change.required_depth - tool_change.nozzle_change_depth);
            category_groups[get_filament_category(new_tool)].insert(get_gap_key(new_tool));
            category_groups[get_filament_category(old_tool)].insert(get_gap_key(old_tool));
            if (tool_change.nozzle_change_depth > EPSILON && is_need_ramming(old_tool, new_tool, layer_id)) {
                GroupEvent &ramming = on_layer[get_gap_key(old_tool)];
                ramming.filaments.push_back(old_tool);
                ramming.base_depth += tool_change.nozzle_change_depth;
            }
        }
        for (auto &entry : on_layer) {
            entry.second.step = step;
            group_events[entry.first].push_back(std::move(entry.second));
        }
    }

    struct PlannedPurge
    {
        int  lane   = 0;
        int  merge  = 1;
        bool fitted = true;
    };
    struct GroupPlan
    {
        std::vector<PlannedPurge> purges;      // one per event
        int                       thin_layers = 0;   // slabs of the band's nozzle thinner than its band size
        float                     depth       = 0.f; // sum over lanes of the deepest footprint each one holds
        int                       lanes       = 0;
    };

    // A second lane is only for a purge whose step is already reserved on every lane in use.
    // Opening one so a coarse nozzle can sit closer to its preferred band prints the same filament
    // in a second block.
    auto plan_group = [&](int group_key, const std::vector<GroupEvent> &events, bool lanes_for_thinness) {
        const int   filament         = events.front().filaments.front();
        const float min_height       = min_layer_height_for_filament(filament);
        const float max_height       = max_layer_height_for_filament(filament);
        const float preferred_height = preferred_height_for_filament(filament);
        // With a second diameter of the same material around, a smaller nozzle can lay a thin fill down; a
        // category with this nozzle alone has to top its bands up with it.
        const bool own_nozzle_fill = category_groups[decode_block_category(group_key)].size() <= 1;
        auto       is_thin         = [&](float height) { return height + WT_EPSILON < preferred_height; };

        // A layer without tool changes fills its bands straight from the layer routine at the base height, so
        // the band fill may stop there but never merge down from there.
        enum : char { NoFill = 0, SingleFill, MergedFill };
        std::vector<char> can_fill(step_count, NoFill);
        for (int step = 0; step < step_count; ++step) {
            const WipeTowerInfo &layer = m_plan[printed[step]];
            if (same_category_filler(layer, group_key) >= 0)
                can_fill[step] = MergedFill;
            else if (layer.tool_changes.empty()) {
                const int loaded = loaded_filament(printed[step]);
                if (loaded >= 0 && get_filament_category(loaded) == decode_block_category(group_key))
                    can_fill[step] = SingleFill;
            }
        }

        // The fewest thin slabs the band fill needs to bring a lane from its surface at step `from` (-1 is the
        // bed) up to step `to`. It can only stop where the band has a filler, and each of its slabs has to be a
        // height that filler covers.
        auto fill_thin_layers = [&](int from, int to) {
            if (to <= from)
                return 0;
            bool any_own = false;
            for (int t = std::max(0, from + 1); t <= to && t < step_count; ++t)
                if (can_fill[t])
                    any_own = true;
            // Layers under the first purge of this category are printed by whoever else is on the tower.
            // Treating that stretch as a hole rejects the lane the purge already fits on and opens a
            // second band for the same filament.
            if (!any_own)
                return 0;
            if (!can_fill[to])
                return unfillable;
            // The stretch is laid down by the band fill ladder over several slabs, not as one slab of this
            // nozzle, so its total height says nothing about whether it can be filled.
            if (!own_nozzle_fill)
                return 0;
            std::vector<int> best(size_t(to - from + 1), unfillable);
            best[0] = 0;
            for (int t = from + 1; t <= to; ++t) {
                if (!can_fill[t])
                    continue;
                const int lowest = can_fill[t] == SingleFill ? t - 1 : from;
                for (int u = t - 1; u >= lowest; --u) {
                    const float height = top_z(t) - top_z(u);
                    if (height > max_height + WT_EPSILON)
                        break;
                    if (height + WT_EPSILON < min_height || best[u - from] >= unfillable)
                        continue;
                    best[t - from] = std::min(best[t - from], best[u - from] + (is_thin(height) ? 1 : 0));
                }
            }
            return best[to - from];
        };
        auto surface_step = [](const std::vector<unsigned char> &lane, int below_step) {
            for (int l = below_step; l >= 0; --l)
                if (lane[l] == CellPurge)
                    return l;
            return -1;
        };

        // Thinness ranks first: the slab's own, and that of the fill it leaves below it. Then the lane: a
        // lower lane in use beats a higher one, and any lane in use beats a new one. Then the deviation from
        // the band size, and on equal deviation the deeper slab, which swallows the odd step instead of
        // leaving it behind as a thin fill of its own.
        struct SlabChoice
        {
            int   merge = 1;
            int   lane  = -1;
            int   thin  = std::numeric_limits<int>::max();
            float delta = std::numeric_limits<float>::max();
            bool  valid() const { return thin != std::numeric_limits<int>::max(); }
        };
        auto better = [](const SlabChoice &choice, const SlabChoice &best) {
            if (choice.thin != best.thin)
                return choice.thin < best.thin;
            if (choice.lane != best.lane)
                return choice.lane < best.lane;
            return choice.delta <= best.delta + WT_EPSILON;
        };

        GroupPlan                               plan;
        std::vector<std::vector<unsigned char>> lanes(1, std::vector<unsigned char>(step_count, CellFree));
        std::vector<float>                      lane_depth(1, 0.f);
        for (const GroupEvent &event : events) {
            const int  step          = event.step;
            const int  max_available = std::min(step + 1, max_merge_layers);
            SlabChoice in_use, fresh;
            int        unreachable_merge = 1;
            float      unreachable_delta = std::numeric_limits<float>::max();
            for (int candidate = 1; candidate <= max_available; ++candidate) {
                const float height = top_z(step) - top_z(step - candidate);
                // Nothing on the tower is extruded outside this nozzle's window, including a single
                // grid step the ladder already left too tall or too short.
                if (height > max_height + WT_EPSILON)
                    break;
                if (height + WT_EPSILON < min_height)
                    continue;
                const float delta = std::abs(height - preferred_height);
                if (delta <= unreachable_delta + WT_EPSILON) {
                    unreachable_merge = candidate;
                    unreachable_delta = delta;
                }
                const int below     = step - candidate;
                const int slab_thin = is_thin(height) ? 1 : 0;
                // A fresh lane starts at the bed, so its fill covers everything below the slab.
                const int fresh_fill = fill_thin_layers(-1, below);
                if (fresh_fill < unfillable) {
                    const SlabChoice choice{candidate, -1, slab_thin + fresh_fill, delta};
                    if (better(choice, fresh))
                        fresh = choice;
                }
                for (int idx = 0; idx < int(lanes.size()); ++idx) {
                    if (!lane_fits(lanes[idx], step, candidate))
                        continue;
                    const int fill = fill_thin_layers(surface_step(lanes[idx], below), below);
                    if (fill >= unfillable)
                        continue;
                    const SlabChoice choice{candidate, idx, slab_thin + fill, delta};
                    if (better(choice, in_use))
                        in_use = choice;
                }
            }

            const bool lanes_left = int(lanes.size()) < max_lanes;
            const bool open_lane  = lanes_for_thinness && in_use.valid() && fresh.valid() && lanes_left &&
                                   fresh.thin < in_use.thin;
            PlannedPurge purge;
            int          lane = -1;
            if (in_use.valid() && !open_lane) {
                purge.merge = in_use.merge;
                lane        = in_use.lane;
            }
            if (lane < 0) {
                // A fresh lane starts at the bed, so it imposes no surface of its own and the best height is
                // free to win. Open one only when every lane already in use has this step reserved.
                // A free cell on the block this material already has is that block: a thinner slab, or a
                // gap the own nozzle cannot tile perfectly, is not a reason to print the same filament twice.
                if (!fresh.valid())
                    BOOST_LOG_TRIVIAL(warning) << "WipeTower: no slab of group " << group_key << " on layer "
                                               << printed[step] << " can be topped up with its own material";
                purge.merge = fresh.valid() ? fresh.merge : unreachable_merge;
                if (!(open_lane && fresh.valid())) {
                    for (int candidate = 0; candidate < int(lanes.size()); ++candidate)
                        if (lane_fits(lanes[candidate], step, purge.merge)) {
                            lane = candidate;
                            break;
                        }
                    // The preferred merge does not fit, but the purge cell itself may still be free.
                    // Stay on that lane at the thickest height it can actually host.
                    if (lane < 0) {
                        for (int candidate = 0; candidate < int(lanes.size()); ++candidate) {
                            if (lanes[candidate][step] == CellReserved)
                                continue;
                            int fit_merge = 0;
                            for (int m = 1; m <= max_available; ++m) {
                                if (!lane_fits(lanes[candidate], step, m))
                                    break;
                                const float height = top_z(step) - top_z(step - m);
                                if (height > max_height + WT_EPSILON)
                                    break;
                                if (height + WT_EPSILON < min_height)
                                    continue;
                                fit_merge = m;
                            }
                            // A free cell is not a licence to print below the nozzle minimum.
                            if (fit_merge == 0)
                                continue;
                            purge.merge = fit_merge;
                            lane        = candidate;
                            break;
                        }
                    }
                }
                if (lane < 0 && lanes_left && (open_lane || fresh.valid())) {
                    lanes.emplace_back(step_count, CellFree);
                    lane_depth.push_back(0.f);
                    lane = int(lanes.size()) - 1;
                }
            }
            if (lane < 0) {
                purge.fitted = false;
                // Last resort still has to be a height this nozzle can extrude, and it has to fit the
                // lane. A single grid step outside the window is not an acceptable fallback.
                const float single = top_z(step) - top_z(step - 1);
                const bool  single_ok = single + WT_EPSILON >= min_height && single <= max_height + WT_EPSILON;
                if (unreachable_delta < std::numeric_limits<float>::max()) {
                    for (int candidate = 0; candidate < int(lanes.size()); ++candidate)
                        if (lane_fits(lanes[candidate], step, unreachable_merge)) {
                            purge.merge = unreachable_merge;
                            lane        = candidate;
                            break;
                        }
                }
                if (lane < 0 && single_ok) {
                    purge.merge = 1;
                    for (int candidate = 0; candidate < int(lanes.size()); ++candidate)
                        if (lanes[candidate][step] != CellReserved) {
                            lane = candidate;
                            break;
                        }
                }
                if (lane < 0 && unreachable_delta < std::numeric_limits<float>::max())
                    purge.merge = unreachable_merge;
                else if (lane < 0 && !single_ok)
                    BOOST_LOG_TRIVIAL(error) << "WipeTower: group " << group_key << " on layer " << printed[step]
                                             << " has no slab inside " << min_height << ".." << max_height << " mm";
                if (lane < 0 && int(lanes.size()) < max_encodable_lanes) {
                    lanes.emplace_back(step_count, CellFree);
                    lane_depth.push_back(0.f);
                    lane = int(lanes.size()) - 1;
                }
                if (lane < 0)
                    lane = 0; // The lane bits of the block key are exhausted; share the first one.
            }
            purge.lane = lane;

            std::vector<unsigned char> &lane_state  = lanes[lane];
            const int                   below       = step - purge.merge;
            const float                 slab_height = top_z(step) - top_z(below);
            const int                   fill        = fill_thin_layers(surface_step(lane_state, below), below);
            plan.thin_layers += (is_thin(slab_height) ? 1 : 0) + (fill < unfillable ? fill : 0);
            for (int l = below + 1; l < step; ++l)
                lane_state[l] = CellReserved;
            lane_state[step] = CellPurge;
            const float base_height = m_plan[printed[step]].height;
            lane_depth[lane]        = std::max(lane_depth[lane], slab_height > EPSILON ?
                                                                     event.base_depth * base_height / slab_height :
                                                                     event.base_depth);
            plan.purges.push_back(purge);
        }
        plan.lanes = int(lanes.size());
        for (float depth : lane_depth)
            plan.depth += depth;
        return plan;
    };

    for (const auto &entry : group_events) {
        const int                      group_key = entry.first;
        const std::vector<GroupEvent> &events    = entry.second;
        // One block per material and diameter. A second lane is opened inside plan_group only when
        // every lane already in use has the purge step reserved. A coarse nozzle used to get another
        // block so its slabs could sit closer to the preferred band; that block prints the same
        // filament the first one can already print.
        const GroupPlan  few_lanes = plan_group(group_key, events, false);
        const GroupPlan &chosen    = few_lanes;
        BOOST_LOG_TRIVIAL(debug) << "[wt_lanes] group=" << group_key << " lanes=" << few_lanes.lanes
                                 << " thin=" << few_lanes.thin_layers << " depth=" << few_lanes.depth;

        for (size_t idx = 0; idx < events.size(); ++idx) {
            const GroupEvent   &event       = events[idx];
            const PlannedPurge &purge       = chosen.purges[idx];
            const int           step        = event.step;
            const int           layer_id    = printed[step];
            const int           block_key   = (group_key & ~0xF) | purge.lane;
            const float         slab_height = top_z(step) - top_z(step - purge.merge);
            for (int filament : event.filaments)
                m_lane_assignment[layer_id][filament] = purge.lane;
            m_block_slab_height[layer_id][block_key] = slab_height;
            for (int l = step - purge.merge + 1; l < step; ++l)
                m_reserved_blocks[printed[l]].insert(block_key);
            if (purge.merge > 1)
                m_merged_slabs_active = true;
            if (!purge.fitted)
                BOOST_LOG_TRIVIAL(warning) << "WipeTower: no lane left for a merged purge on layer " << layer_id
                                           << ", falling back to the tower layer height";

            std::ostringstream filaments;
            for (size_t i = 0; i < event.filaments.size(); ++i)
                filaments << (i ? "," : "") << event.filaments[i];
            std::ostringstream trench;
            for (int l = step - purge.merge + 1; l < step; ++l)
                trench << (l > step - purge.merge + 1 ? "," : "") << printed[l];
            BOOST_LOG_TRIVIAL(debug)
                << "[wt_purge] layer " << layer_id << " z=" << m_plan[layer_id].z
                << " filaments=" << filaments.str() << " group=" << group_key << " lane=" << purge.lane
                << (purge.fitted ? "" : "(no lane left)") << " merge=" << purge.merge
                << " min_h=" << min_layer_height_for_filament(event.filaments.front()) << " slab_h=" << slab_height
                << " span=(" << top_z(step - purge.merge) << ".." << top_z(step) << "]"
                << " block=" << block_key << " reserves=[" << trench.str() << "]";
        }
    }
}

void WipeTower::collect_printed_steps(std::vector<int> &printed) const
{
    printed.clear();
    const int first_layer = m_first_layer_idx == size_t(-1) ? 0 : int(m_first_layer_idx);
    for (int layer_id = first_layer; layer_id < int(m_plan.size()); ++layer_id)
        if (!layer_is_skipped(layer_id))
            printed.push_back(layer_id);
}

double WipeTower::tower_bed_z() const
{
    if (m_plan.empty())
        return 0.;
    const int first = m_first_layer_idx == size_t(-1) ? 0 : int(m_first_layer_idx);
    return double(m_plan[first].z - m_plan[first].height);
}

void WipeTower::collect_tower_visitors(const std::vector<int> &printed, std::vector<std::vector<int>> &visitors) const
{
    visitors.assign(printed.size(), {});

    int loaded = -1;
    for (const WipeTowerInfo &info : m_plan)
        if (!info.tool_changes.empty()) { loaded = int(info.tool_changes.front().old_tool); break; }
    std::vector<int> loaded_filament(m_plan.size(), -1);
    for (size_t layer_id = 0; layer_id < m_plan.size(); ++layer_id) {
        for (const WipeTowerInfo::ToolChange &tool_change : m_plan[layer_id].tool_changes)
            loaded = int(tool_change.new_tool);
        loaded_filament[layer_id] = loaded;
    }

    for (size_t step = 0; step < printed.size(); ++step) {
        const int layer_id = printed[step];
        std::unordered_set<int> seen;
        for (const WipeTowerInfo::ToolChange &tool_change : m_plan[layer_id].tool_changes) {
            seen.insert(int(tool_change.old_tool));
            seen.insert(int(tool_change.new_tool));
        }
        if (seen.empty() && loaded_filament[layer_id] >= 0)
            seen.insert(loaded_filament[layer_id]);
        visitors[step].assign(seen.begin(), seen.end());
        std::sort(visitors[step].begin(), visitors[step].end(), [this](int lhs, int rhs) {
            return m_filpar[lhs].nozzle_diameter < m_filpar[rhs].nozzle_diameter;
        });
    }
}

bool WipeTower::plan_merge_ladder(const std::vector<int> &printed, int first_step, int last_step, double bottom_z,
                                  const std::vector<std::vector<int>> &visitors, const std::vector<char> &reservable,
                                  const std::vector<char> &can_merge_top, std::vector<MergeLadderGroup> &groups) const
{
    groups.clear();
    if (first_step > last_step)
        return true;
    const int span = last_step - first_step + 1;

    double widest = 0.;
    for (int filament_id = 0; filament_id < int(m_filpar.size()); ++filament_id)
        widest = std::max(widest, double(max_layer_height_for_filament(filament_id)));

    // The Z a group tops out at; below the first step of the range the caller tells us what is already there.
    auto step_z = [this, &printed, first_step, bottom_z](int step) {
        return step < first_step ? bottom_z : double(m_plan[printed[step]].z);
    };

    const double unreachable = std::numeric_limits<double>::max();
    // A step no group can cover at a legal height still has to be printed somehow. Price it so the partition
    // only falls back to it where there is no alternative.
    const double unfit_cost = 1.e6;
    std::vector<double> best(span + 1, unreachable);
    std::vector<int>    prev(span + 1, -1);
    std::vector<int>    chosen(span + 1, -1);
    best[0] = 0.;
    for (int end = 1; end <= span; ++end) {
        const int top_step = first_step + end - 1;
        for (int begin = end - 1; begin >= 0; --begin) {
            const bool merged = begin < end - 1;
            if (merged && !can_merge_top[top_step])
                break;
            // This iteration is the one handing printed[first_step + begin] over to the group above it.
            if (merged && !reservable[first_step + begin])
                break;
            const double height = step_z(top_step) - step_z(first_step + begin - 1);
            if (merged && height > widest + WT_EPSILON)
                break;
            if (best[begin] == unreachable)
                continue;

            int    filament_id = -1;
            double penalty     = 0.;
            for (int candidate : visitors[top_step])
                if (filament_can_print_height(candidate, float(height))) { filament_id = candidate; break; }
            if (filament_id < 0) {
                // Merging is pointless when the merged span is unprintable too, so only a single step may
                // stay on a height none of its nozzles covers.
                if (merged || visitors[top_step].empty())
                    continue;
                filament_id = visitors[top_step].front();
                penalty     = unfit_cost;
            }

            // Every swallowed step costs one, so the partition merges only where a step cannot be printed on
            // its own and otherwise leaves the tower on its base cadence.
            const double cost = best[begin] + penalty + double(end - 1 - begin);
            if (cost + WT_EPSILON < best[end]) {
                best[end]   = cost;
                prev[end]   = begin;
                chosen[end] = filament_id;
            }
        }
    }

    if (prev[span] < 0)
        return false;

    for (int end = span; end > 0; end = prev[end]) {
        const int begin = prev[end];
        groups.push_back({ first_step + begin, first_step + end - 1, chosen[end],
                           float(step_z(first_step + end - 1) - step_z(first_step + begin - 1)) });
    }
    std::reverse(groups.begin(), groups.end());
    return true;
}

// A band's purge is pinned to the layer it belongs to and already owns the trench it dug for itself, but the
// fill that tops the band up to its full depth is not pinned to anything. On the layers in between - typically
// a tool change of some other band - that fill would otherwise run at the tower's base layer height, which is
// exactly where a large nozzle ends up laying down 0.1 mm. So the fill of every band gets its own Z ladder
// over the layers between its purges, planned the same way as the outer wall. Runs on the finished blocks
// because a merged group has to carry the material interfaces and the band depth of the layers it swallows.
void WipeTower::plan_merged_block_fill()
{
    if (!merged_fill_active())
        return;

    std::vector<int> printed;
    collect_printed_steps(printed);
    if (printed.empty())
        return;

    const int               n = int(printed.size());
    const std::vector<char> reservable(n, 1); // a band left empty is fine, the wall planner keeps the layer alive
    const int               wall_filament = get_wall_filament_for_all_layer();

    // Only a layer that lays its bands down through the block bookkeeping reads the merged height back; one
    // that carries neither a tool change nor a solid interface fills its bands straight from the layer
    // routine at the whole-tower height. Such a layer already prints at a height its loaded nozzle covers, so
    // it can be swallowed by a group above it, but it can never top one.
    std::vector<char> can_merge_top(n, 0);
    for (int step = 0; step < n; ++step) {
        const int layer_id = printed[step];
        can_merge_top[step] = !m_plan[layer_id].tool_changes.empty() ||
                              std::any_of(m_wipe_tower_blocks.begin(), m_wipe_tower_blocks.end(),
                                          [layer_id](const WipeTowerBlock &block) {
                                              return block.layers_type[layer_id] != WipeTowerLayerType::Normal;
                                          });
    }

    // Contact beats Contact_UP beats Solid: a merged group has to be laid down with the strongest fill any of
    // the layers it swallows asked for, otherwise a material interface is silently turned into sparse grid.
    auto type_rank = [](WipeTowerLayerType type) {
        switch (type) {
        case WipeTowerLayerType::Contact: return 3;
        case WipeTowerLayerType::Contact_UP: return 2;
        case WipeTowerLayerType::Solid: return 1;
        default: return 0;
        }
    };

    for (WipeTowerBlock &block : m_wipe_tower_blocks) {
        // A block key contains one nozzle diameter even if a different visiting filament occasionally tops
        // the block up. Keep this band's grid tied to its own nozzle instead of changing the target height
        // whenever another nozzle happens to visit the tower.
        const int   diameter_units          = (block.block_key >> 4) & 0x3F;
        const float block_nozzle_diameter   = float(diameter_units) * 0.05f;
        const float reference_nozzle        = m_tower_grid_reference_nozzle > 0.f ?
                                                  m_tower_grid_reference_nozzle : m_tower_grid_nozzle_diameter;
        const float preferred_block_height =
            float(adaptive_layer_band_size(block_nozzle_diameter, reference_nozzle, m_tower_grid_layer_height)) *
            m_tower_grid_layer_height;
        const int  block_category = decode_block_category(block.block_key);
        // The slot under this band is filled by whatever visits the layer, but the slab still has to be a
        // height this band's own nozzle can lay down. A 0.6 mm PLA filling under 0.4 mm TPU must not
        // leave the column empty and then extrude one 0.4 mm+ first layer.
        float block_height_ceiling = 0.f;
        for (int id = 0; id < int(m_filpar.size()); ++id) {
            if (get_filament_category(id) != block_category)
                continue;
            if (std::abs(m_filpar[id].nozzle_diameter - block_nozzle_diameter) > 0.026f)
                continue;
            block_height_ceiling = std::max(block_height_ceiling, max_layer_height_for_filament(id));
        }
        if (block_height_ceiling <= WT_EPSILON)
            block_height_ceiling = 0.7f * block_nozzle_diameter;
        bool       category_is_tpu = false;
        for (int id = 0; id < int(m_filpar.size()); ++id)
            if (get_filament_category(id) == block_category && is_tpu_filament(id)) {
                category_is_tpu = true;
                break;
            }
        // Ramming is extruded by the outgoing filament into its own band. The slot under that strip is
        // still filled, and switching material on every grid step turns each one into a solid interface.
        bool band_has_tpu_ramming = false;
        if (category_is_tpu) {
            for (int layer_id = 0; layer_id < int(m_plan.size()) && !band_has_tpu_ramming; ++layer_id) {
                for (const WipeTowerInfo::ToolChange &tool_change : m_plan[layer_id].tool_changes) {
                    const int old_tool = int(tool_change.old_tool);
                    if (tool_change.nozzle_change_depth > EPSILON &&
                        is_need_ramming(old_tool, int(tool_change.new_tool), layer_id) &&
                        is_tpu_filament(old_tool) && get_block_key(old_tool, layer_id) == block.block_key) {
                        band_has_tpu_ramming = true;
                        break;
                    }
                }
            }
        }
        // An extra lane is the band opened because the first one could not host the next purge. Its slot
        // below that purge was empty. A TPU ramming band has the same hole under the ramming strip. Both
        // are covered with the thickest slab the filament actually on each layer can extrude.
        const bool prefer_max_fill  = category_is_tpu && ((block.block_key & 0xF) > 0 || band_has_tpu_ramming);
        // Under a TPU ramming strip, stay on that category for as far as its nozzle can reach. A foreign
        // category only fills the stretch TPU itself cannot extrude.
        const bool prefer_own_fill  = band_has_tpu_ramming;

        // The ladder must only accept heights the filament that will actually top this band up can extrude,
        // so plan it against that one filament rather than against everything visiting the tower: the
        // generic list contains the other bands' nozzles too, and the largest of them would wave through a
        // slab the band's own nozzle cannot lay down.
        // A band only takes material of its own category, so it simply has no visitor on a layer none of that
        // category visits, and that layer gets swallowed by the next visit above it.
        std::vector<std::vector<int>> block_visitors(n);
        for (int step = 0; step < n; ++step) {
            const int layer_id = printed[step];
            const int filler    = block_fill_filament_base(m_plan[layer_id], layer_id, block, wall_filament);
            if (filler >= 0)
                block_visitors[step].assign(1, filler);
        }

        // Steps the purge plan already decided on: the band lays down its purge there, or it is the trench
        // such a purge sinks into. A trench always runs right up to its purge, so a free step never follows
        // one. A band that has nothing to fill on a layer is not a step of its ladder either.
        std::vector<char> fixed(n, 0);
        for (int step = 0; step < n; ++step) {
            const int layer_id = printed[step];
            if (block.layer_reserved[layer_id] || m_block_slab_height[layer_id].count(block.block_key) ||
                block.layer_depths[layer_id] <= 2.f * m_perimeter_width)
                fixed[step] = 1;
        }

        // This is the top surface of this band, not the previous layer visited by the whole tower. Other
        // nozzle bands may print between two visits of this one without advancing its supporting surface.
        double band_top_z = tower_bed_z();
        for (int run_begin = 0; run_begin < n;) {
            if (fixed[run_begin]) {
                const int layer_id = printed[run_begin];
                // A purge is real extrusion in this band and brings its surface to this Z. A reserved step
                // is only the empty trench below a later purge, so it must not advance the surface.
                if (m_block_slab_height[layer_id].count(block.block_key))
                    band_top_z = double(m_plan[layer_id].z);
                ++run_begin;
                continue;
            }
            // The band widens towards the bed, so a group may only span layers of one depth: a slab printed
            // at the top of the group cannot cover a wider part of the band below it.
            const float run_depth = block.layer_depths[printed[run_begin]];
            int         run_end   = run_begin;
            while (run_end + 1 < n && !fixed[run_end + 1] &&
                   std::abs(block.layer_depths[printed[run_end + 1]] - run_depth) <= WT_EPSILON)
                ++run_end;
            const int next_run = run_end + 1;
            // Layers this category does not visit are still filled: the slot under a TPU column is printed
            // by whatever other material is on the tower. Leaving them out of the ladder kept that support
            // fill on the raw grid height, so a 0.4 or 0.6 nozzle laid it down at 0.1 mm.
            auto filaments_on_step = [&](int step, bool other_category_only) {
                std::vector<int> visitors;
                auto             add = [&](int filament) {
                    if (filament < 0)
                        return;
                    if (other_category_only &&
                        get_filament_category(filament) == decode_block_category(block.block_key))
                        return;
                    if (std::find(visitors.begin(), visitors.end(), filament) == visitors.end())
                        visitors.push_back(filament);
                };
                const int            layer_id = printed[step];
                const WipeTowerInfo &layer    = m_plan[layer_id];
                if (layer.tool_changes.empty())
                    add(loaded_filament(layer_id));
                else {
                    add(int(layer.tool_changes.front().old_tool));
                    for (const WipeTowerInfo::ToolChange &tool_change : layer.tool_changes)
                        add(int(tool_change.new_tool));
                }
                return visitors;
            };
            auto apply_fill_groups = [&](const std::vector<MergeLadderGroup> &groups, bool foreign_support, bool keep_sparse = false) {
                double surface = band_top_z;
                for (const MergeLadderGroup &group : groups) {
                    const int top_layer = printed[group.top_step];
                    float     height    = group.height;
                    // The support nozzle may already be extruding a thicker slab of its own on this layer.
                    // Use that height when it still sits on this band's surface, so the fill under TPU
                    // matches the 0.4/0.6 band instead of the grid step.
                    if (foreign_support && group.filament_id >= 0 &&
                        height + WT_EPSILON < preferred_height_for_filament(group.filament_id)) {
                        const float carrier = slab_height_for(group.filament_id, top_layer, height);
                        const float bottom  = m_plan[top_layer].z - carrier;
                        if (carrier > height + WT_EPSILON && double(bottom) + WT_EPSILON >= surface &&
                            (block_height_ceiling <= WT_EPSILON || carrier <= block_height_ceiling + WT_EPSILON))
                            height = carrier;
                    }
                    const float bottom_z = m_plan[top_layer].z - height;
                    for (int step = 0; step < n; ++step) {
                        const int   layer_id = printed[step];
                        const float z        = m_plan[layer_id].z;
                        if (z <= bottom_z + WT_EPSILON)
                            continue;
                        if (z > m_plan[top_layer].z + WT_EPSILON)
                            break;
                        if (layer_id == top_layer)
                            continue;
                        if (layer_id < int(m_block_slab_height.size()) &&
                            m_block_slab_height[layer_id].count(block.block_key))
                            continue;
                        block.layer_reserved[layer_id] = 1;
                        // A same-material support run stays sparse. Promoting Solid from a swallowed
                        // layer would turn the whole slab solid just because the TPU category flickered.
                        if (!keep_sparse &&
                            type_rank(block.layers_type[layer_id]) > type_rank(block.layers_type[top_layer]))
                            block.layers_type[top_layer] = block.layers_type[layer_id];
                    }
                    if (keep_sparse)
                        block.layers_type[top_layer] = WipeTowerLayerType::Normal;
                    // Remember which filament this slab actually extrudes. A later category check
                    // compares these, and generation has to lay down the same one.
                    if (top_layer < (int) block.layer_fill_filament.size() && group.filament_id >= 0)
                        block.layer_fill_filament[top_layer] = group.filament_id;
                    block.layer_heights[top_layer] = height;
                    if (height > m_plan[top_layer].height + WT_EPSILON)
                        m_merged_slabs_active = true;
                    BOOST_LOG_TRIVIAL(debug)
                        << "[wt_fill]   block=" << block.block_key << " layers z=(" << bottom_z << ".."
                        << m_plan[top_layer].z << "] -> print at " << top_layer << " h=" << height
                        << " filament=" << group.filament_id
                        << (foreign_support ? " (support under other category)" : "");
                    surface = double(m_plan[top_layer].z);
                }
                if (!groups.empty())
                    band_top_z = surface;
            };
            auto plan_fill_span = [&](int begin, int end, const std::vector<std::vector<int>> &visitors,
                                      float preferred, bool foreign_support) {
                while (end >= begin && (end >= n || visitors[end].empty()))
                    --end;
                if (begin > end)
                    return;
                BOOST_LOG_TRIVIAL(debug)
                    << "[wt_fill] block=" << block.block_key << " lane=" << (block.block_key & 0xF)
                    << " run=layers " << printed[begin] << ".." << printed[end] << " depth=" << run_depth
                    << " bottom_z=" << band_top_z << (foreign_support ? " support-fill" : " own-fill")
                    << (prefer_max_fill ? " max-height" : "");
                std::vector<MergeLadderGroup> groups;
                const float capped_preferred = block_height_ceiling > WT_EPSILON ?
                                                   std::min(preferred, block_height_ceiling) : preferred;
                if (plan_band_merge_ladder(printed, begin, end, band_top_z, visitors, reservable, can_merge_top, groups,
                                      capped_preferred, prefer_max_fill, block_height_ceiling))
                    apply_fill_groups(groups, foreign_support);
                else {
                    BOOST_LOG_TRIVIAL(warning) << "WipeTower: cannot merge the fill of band " << block.block_key
                                               << " over printed layers " << printed[begin] << ".." << printed[end]
                                               << "; keeping their base layer heights";
                    band_top_z = double(m_plan[printed[end]].z);
                }
            };
            // Own-category stretches keep this band's nozzle grid. Stretches nobody of this category visits
            // are the support fill under that column, planned on the nozzle that actually prints them.
            // An extra TPU lane, and the band a TPU ramming strip lands in, are the exception: chopping
            // them wherever the category flickers fills every grid step with a different material, and each
            // change is a solid interface. Lock one material for as far as its nozzle can reach, and lay
            // that stretch down at the nozzle's maximum height. Under the ramming strip the locked material
            // is TPU itself whenever that nozzle can still print the slab.
            auto filaments_of = [&](int step, int category) {
                std::vector<int> out;
                for (int filament : filaments_on_step(step, false))
                    if (get_filament_category(filament) == category)
                        out.push_back(filament);
                return out;
            };
            auto mark_fill_interface = [&](const std::vector<MergeLadderGroup> &groups, int category, int prev_category, bool meets_band) {
                if (groups.empty())
                    return;
                const int first_layer = printed[groups.front().top_step];
                const int last_layer  = printed[groups.back().top_step];
                if (prev_category >= 0 && prev_category != category)
                    block.layers_type[first_layer] = WipeTowerLayerType::Contact;
                if (meets_band && category != block_category)
                    block.layers_type[last_layer] = WipeTowerLayerType::Contact;
            };
            if (prefer_max_fill) {
                int prev_category = -1;
                if (run_begin > 0) {
                    for (int step = run_begin - 1; step >= 0 && fixed[step]; --step) {
                        if (m_block_slab_height[printed[step]].count(block.block_key)) {
                            prev_category = block_category;
                            break;
                        }
                    }
                }
                const bool meets_band = next_run < n && fixed[next_run] &&
                                        (block.layer_reserved[printed[next_run]] ||
                                         m_block_slab_height[printed[next_run]].count(block.block_key));
                for (int cursor = run_begin; cursor <= run_end;) {
                    std::vector<int> categories;
                    for (int step = cursor; step <= run_end; ++step)
                        for (int filament : filaments_on_step(step, false)) {
                            const int category = get_filament_category(filament);
                            if (std::find(categories.begin(), categories.end(), category) == categories.end())
                                categories.push_back(category);
                        }
                    int   best_cat   = -1;
                    int   best_end   = cursor - 1;
                    float best_max_h = -1.f;
                    int   own_end    = cursor - 1;
                    for (int category : categories) {
                        float cat_max_h = 0.f;
                        for (int step = cursor; step <= run_end; ++step)
                            for (int filament : filaments_of(step, category))
                                cat_max_h = std::max(cat_max_h, max_layer_height_for_filament(filament));
                        if (block_height_ceiling > WT_EPSILON)
                            cat_max_h = std::min(cat_max_h, block_height_ceiling);
                        double surface = band_top_z;
                        int    next    = cursor;
                        int    end     = cursor - 1;
                        while (next <= run_end) {
                            int farthest = -1;
                            for (int step = next; step <= run_end; ++step) {
                                const std::vector<int> fils = filaments_of(step, category);
                                if (fils.empty())
                                    continue;
                                const double height = double(m_plan[printed[step]].z) - surface;
                                if (height > double(cat_max_h) + WT_EPSILON)
                                    break;
                                bool swallows = false;
                                for (int below = next; below < step; ++below)
                                    if (double(m_plan[printed[below]].z) > surface + WT_EPSILON)
                                        swallows = true;
                                if (swallows && !can_merge_top[step])
                                    continue;
                                const bool can_print = std::any_of(fils.begin(), fils.end(), [&](int filament) {
                                    return filament_can_print_height(filament, float(height));
                                });
                                if (!can_print)
                                    continue;
                                farthest = step;
                            }
                            if (farthest < 0)
                                break;
                            end     = farthest;
                            surface = double(m_plan[printed[farthest]].z);
                            next    = farthest + 1;
                        }
                        if (category == block_category && end >= cursor)
                            own_end = end;
                        if (end > best_end || (end == best_end && cat_max_h > best_max_h + WT_EPSILON)) {
                            best_end   = end;
                            best_cat   = category;
                            best_max_h = cat_max_h;
                        }
                    }
                    // The ramming strip is TPU. Take that category for the whole reach of its nozzle before
                    // a longer foreign stretch is allowed to replace it.
                    if (prefer_own_fill && own_end >= cursor) {
                        best_cat = block_category;
                        best_end = own_end;
                    }
                    if (best_cat < 0 || best_end < cursor) {
                        std::vector<std::vector<int>> foreign(n);
                        foreign[cursor] = filaments_on_step(cursor, false);
                        plan_fill_span(cursor, cursor, foreign, 0.f, true);
                        ++cursor;
                        prev_category = -1;
                        continue;
                    }
                    std::vector<std::vector<int>> visitors(n);
                    for (int step = cursor; step <= best_end; ++step)
                        visitors[step] = filaments_of(step, best_cat);
                    std::vector<MergeLadderGroup> groups;
                    if (plan_band_merge_ladder(printed, cursor, best_end, band_top_z, visitors, reservable, can_merge_top, groups,
                                          0.f, true, block_height_ceiling)) {
                        apply_fill_groups(groups, true, true);
                        mark_fill_interface(groups, best_cat, prev_category, best_end == run_end && meets_band);
                    } else {
                        BOOST_LOG_TRIVIAL(warning) << "WipeTower: cannot merge the fill of band " << block.block_key
                                                   << " over printed layers " << printed[cursor] << ".." << printed[best_end]
                                                   << "; keeping their base layer heights";
                        band_top_z = double(m_plan[printed[best_end]].z);
                    }
                    prev_category = best_cat;
                    cursor        = best_end + 1;
                }
            } else {
                for (int cursor = run_begin; cursor <= run_end;) {
                    const bool own     = !block_visitors[cursor].empty();
                    int        seg_end = cursor;
                    while (seg_end + 1 <= run_end && block_visitors[seg_end + 1].empty() == !own)
                        ++seg_end;
                    if (own)
                        plan_fill_span(cursor, seg_end, block_visitors, preferred_block_height, false);
                    else {
                        std::vector<std::vector<int>> foreign(n);
                        for (int step = cursor; step <= seg_end; ++step)
                            foreign[step] = filaments_on_step(step, true);
                        plan_fill_span(cursor, seg_end, foreign, 0.f, true);
                    }
                    cursor = seg_end + 1;
                }
            }
            run_begin = next_run;
        }
    }

    // Each block is one adhesiveness category, but the slot under it is often printed by a
    // filament from another block. Those two meet on a plane, and both slabs are the interface.
    // PLA in a TPU block is only a fill, so it was already marked. TPU's own layers are the
    // purge: skipping those left the TPU side of the same joint sparse. A run of one category
    // stays sparse except at the joint. Layers 1-5 PLA, 6-7 TPU, 8-10 PLA, then TPU again
    // mark 5, 6, 7, 8 and 10.
    for (WipeTowerBlock &block : m_wipe_tower_blocks) {
        struct PrintedSlab {
            int   layer_id;
            int   filament;
            float top_z;
            float bottom_z;
        };
        std::vector<PrintedSlab> slabs;
        for (int layer_id = 0; layer_id < int(m_plan.size()); ++layer_id) {
            if (layer_is_skipped(layer_id))
                continue;
            if (layer_id < int(block.layer_reserved.size()) && block.layer_reserved[layer_id])
                continue;
            if (band_fill_depth(block, layer_id) <= 2.f * m_perimeter_width)
                continue;
            int filament = -1;
            if (layer_id < int(block.layer_fill_filament.size()) && block.layer_fill_filament[layer_id] >= 0)
                filament = block.layer_fill_filament[layer_id];
            else
                filament = block_fill_filament_base(m_plan[layer_id], layer_id, block, wall_filament);
            if (filament < 0)
                continue;
            const float height = layer_id < int(block.layer_heights.size()) && block.layer_heights[layer_id] > WT_EPSILON ?
                                     block.layer_heights[layer_id] :
                                     m_plan[layer_id].height;
            if (height <= WT_EPSILON)
                continue;
            const float top_z = m_plan[layer_id].z;
            slabs.push_back({layer_id, filament, top_z, top_z - height});
        }
        for (const PrintedSlab &upper : slabs) {
            const PrintedSlab *support = nullptr;
            for (const PrintedSlab &lower : slabs) {
                if (lower.layer_id >= upper.layer_id || lower.top_z > upper.bottom_z + WT_EPSILON)
                    continue;
                if (support == nullptr || lower.top_z > support->top_z)
                    support = &lower;
            }
            if (support == nullptr || support->top_z + WT_EPSILON < upper.bottom_z)
                continue;
            if (get_filament_category(support->filament) == get_filament_category(upper.filament))
                continue;
            block.layers_type[support->layer_id] = WipeTowerLayerType::Contact;
            block.layers_type[upper.layer_id]    = WipeTowerLayerType::Contact;
        }
    }
}

// Only a purge is pinned to the layer it belongs to; the outer wall is not. So the wall gets its own Z ladder
// spanning every layer the tower prints, no matter where the tool changes fall: a group draws one wall at its
// top layer covering the whole span, and the layers below it merely omit their wall. Planning globally is what
// lets an isolated tool-change layer - too thin for any large nozzle on its own - hand its wall to the next
// printed layer instead of falling back to the base layer height.
void WipeTower::plan_merged_outer_wall()
{
    const int layer_count = int(m_plan.size());
    m_wall_filament.assign(layer_count, -1);
    m_wall_slab_height.assign(layer_count, 0.f);
    m_wall_reserved.assign(layer_count, 0);

    if (!merged_fill_active())
        return;

    // The foundation participates in the wall ladder so the first printed wall may span down to the bed.
    // The tower's adhesion anchor is the group covering the foundation, not the foundation layer itself:
    // a nozzle that first reaches the tower a few layers up may lay that wall down from the bed in one go.
    std::vector<int> printed;
    collect_printed_steps(printed);
    if (printed.empty())
        return;
    const int n = int(printed.size());

    // Like on a single-diameter tower, the wall keeps to the material of the tower's wall filament: a layer
    // any filament of that category visits hands its wall only to one of those, the wall filament itself
    // first and the others in the order they come to the tower. Another material takes the wall over only
    // on a layer none of that category visits. A layer without tool changes is visited by whatever is still
    // loaded.
    const int wall_filament = get_wall_filament_for_all_layer();
    const int wall_category = wall_filament >= 0 ? get_filament_category(wall_filament) : -1;
    int       initial       = -1;
    for (const WipeTowerInfo &info : m_plan)
        if (!info.tool_changes.empty()) {
            initial = int(info.tool_changes.front().old_tool);
            break;
        }
    std::vector<std::vector<int>> visitors(n);
    for (int step = 0; step < n; ++step) {
        const WipeTowerInfo &layer = m_plan[printed[step]];
        std::vector<int>     arrivals;
        auto                 arrive = [&arrivals](int filament) {
            if (filament >= 0 && std::find(arrivals.begin(), arrivals.end(), filament) == arrivals.end())
                arrivals.push_back(filament);
        };
        if (layer.tool_changes.empty()) {
            const int loaded = loaded_filament(printed[step]);
            arrive(loaded >= 0 ? loaded : initial);
        } else {
            arrive(int(layer.tool_changes.front().old_tool));
            for (const WipeTowerInfo::ToolChange &tool_change : layer.tool_changes)
                arrive(int(tool_change.new_tool));
        }
        std::vector<int> own_material;
        if (std::find(arrivals.begin(), arrivals.end(), wall_filament) != arrivals.end())
            own_material.push_back(wall_filament);
        for (int filament : arrivals)
            if (filament != wall_filament && get_filament_category(filament) == wall_category)
                own_material.push_back(filament);
        visitors[step] = own_material.empty() ? std::move(arrivals) : std::move(own_material);
    }

    // Every printed layer may hand its wall to the group above. Holding a wall back so that the layer keeps a
    // visit would defeat the ladder: a tool-change layer sitting one thin step above another one has no other
    // way out than merging its wall upwards. A layer left with neither a wall nor an unreserved band lays down
    // nothing at all, and generate_new() drops that visit rather than travelling to the tower for nothing.
    // The foundation is no exception; finish_layer_new() gives the brim to whichever wall reaches the bed.
    const std::vector<char> reservable(n, 1);

    // The layer routine draws the wall from the planned height on every layer, so any of them can top a group.
    const std::vector<char>       can_merge_top(n, 1);
    const double                  bottom_z = tower_bed_z();
    std::vector<MergeLadderGroup> groups;
    if (!plan_band_merge_ladder(printed, 0, n - 1, bottom_z, visitors, reservable, can_merge_top, groups)) {
        BOOST_LOG_TRIVIAL(warning) << "WipeTower: cannot lay out a wall ladder over the printed tower layers "
                                   << printed.front() << ".." << printed.back()
                                   << "; keeping the existing per-layer wall plan";
        return;
    }

    for (const MergeLadderGroup &group : groups) {
        for (int step = group.begin_step; step < group.top_step; ++step)
            m_wall_reserved[printed[step]] = 1;
        m_wall_filament[printed[group.top_step]]    = group.filament_id;
        m_wall_slab_height[printed[group.top_step]] = group.height;
        if (group.begin_step < group.top_step) {
            m_merged_slabs_active = true;
            const int top_layer = printed[group.top_step];
            BOOST_LOG_TRIVIAL(debug)
                << "[wt_wall] layers " << printed[group.begin_step] << ".." << top_layer << " -> print at "
                << top_layer << " z=" << m_plan[top_layer].z << " h=" << group.height
                << " span=(" << (m_plan[top_layer].z - group.height) << ".." << m_plan[top_layer].z
                << "] filament=" << group.filament_id;
        }
    }
}

// The tower's Z is decided by four ladders that never see each other's result: the tower-wide one, the trench
// each purge digs, the fill of every band and the outer wall. Walk the finished plan and report every place
// where a band, or the wall, claims Z that a lower layer already filled - such a slab is extruded straight
// into the previous one instead of on top of it.
void WipeTower::audit_merged_slabs() const
{
    if (m_plan.empty() || !m_merged_slabs_active)
        return;

    // Mirrors the fill condition of finish_layer_new(): a reserved band is the trench of a slab above, and a
    // band with no depth left lays nothing down. A purge is pinned to its layer and drawn either way.
    auto band_prints = [this](const WipeTowerBlock &block, int layer_id) {
        if (layer_is_skipped(layer_id))
            return false;
        if (layer_id < int(m_block_slab_height.size()) && m_block_slab_height[layer_id].count(block.block_key))
            return true;
        return layer_id < int(block.layer_reserved.size()) && !block.layer_reserved[layer_id] &&
               band_fill_depth(block, layer_id) > 2.f * m_perimeter_width;
    };

    int overlaps = 0;
    for (const WipeTowerBlock &block : m_wipe_tower_blocks) {
        float prev_top   = 0.f;
        int   prev_layer = -1;
        for (int layer_id = 0; layer_id < int(m_plan.size()); ++layer_id) {
            if (! band_prints(block, layer_id))
                continue;
            const float top    = m_plan[layer_id].z;
            const float height = (layer_id < int(block.layer_heights.size()) && block.layer_heights[layer_id] > 0.f) ?
                                     block.layer_heights[layer_id] : m_plan[layer_id].height;
            if (prev_layer >= 0 && top - height + WT_EPSILON < prev_top) {
                ++overlaps;
                BOOST_LOG_TRIVIAL(error)
                    << "[wt_audit] band " << block.block_key << " (cat=" << decode_block_category(block.block_key)
                    << " lane=" << (block.block_key & 0xF) << " y=" << block.start_depth << ") fills ("
                    << (top - height) << ".." << top << "] at layer " << layer_id << " h=" << height
                    << ", but layer " << prev_layer << " already reached " << prev_top << "; overlap "
                    << (prev_top - (top - height)) << " mm";
            }
            prev_top   = top;
            prev_layer = layer_id;
        }
    }

    float prev_wall_top   = 0.f;
    int   prev_wall_layer = -1;
    for (int layer_id = 0; layer_id < int(m_plan.size()); ++layer_id) {
        if (layer_is_skipped(layer_id) || (layer_id < int(m_wall_reserved.size()) && m_wall_reserved[layer_id]))
            continue;
        const float top    = m_plan[layer_id].z;
        const float height = (layer_id < int(m_wall_slab_height.size()) && m_wall_slab_height[layer_id] > 0.f) ?
                                 m_wall_slab_height[layer_id] : layer_slab_height(layer_id, m_plan[layer_id].height);
        if (prev_wall_layer >= 0 && top - height + WT_EPSILON < prev_wall_top) {
            ++overlaps;
            BOOST_LOG_TRIVIAL(error)
                << "[wt_audit] outer wall draws (" << (top - height) << ".." << top << "] at layer " << layer_id
                << " h=" << height << ", but layer " << prev_wall_layer << " already reached " << prev_wall_top
                << "; overlap " << (prev_wall_top - (top - height)) << " mm";
        }
        prev_wall_top   = top;
        prev_wall_layer = layer_id;
    }

    if (overlaps)
        BOOST_LOG_TRIVIAL(error) << "[wt_audit] wipe tower slab plan has " << overlaps
                                 << " overlapping slab(s) over " << m_wipe_tower_blocks.size() << " band(s)";
    else
        BOOST_LOG_TRIVIAL(info) << "[wt_audit] wipe tower slab plan is free of overlapping slabs over "
                                << m_wipe_tower_blocks.size() << " band(s)";
}

void WipeTower::plan_tower_new()
{
    if (m_wipe_tower_brim_width < 0) m_wipe_tower_brim_width = get_auto_brim_by_height(m_wipe_tower_height);
    plan_tower_ladder();
    plan_merged_slabs();
    const int foundation_layer = first_printed_layer_idx();
    calc_block_infill_gap();
    if (m_use_rib_wall) {
        // recalculate wipe_tower_with and layer's depth
        generate_wipe_tower_blocks(false);
        float max_depth    = std::accumulate(m_wipe_tower_blocks.begin(), m_wipe_tower_blocks.end(), 0.f, [](float a, const auto &t) { return a + t.depth; }) + m_perimeter_width;
        float square_width = align_ceil(std::sqrt(max_depth * m_wipe_tower_width * m_extra_spacing), m_perimeter_width);
        m_wipe_tower_width = square_width;
        for (int idx = 0; idx < m_plan.size(); idx++) {
            for (auto &toolchange : m_plan[idx].tool_changes) {
                toolchange = set_toolchange(toolchange.old_tool, toolchange.new_tool, m_plan[idx].height, toolchange.wipe_volume, toolchange.purge_volume,idx);
            }
        }
    }

    generate_wipe_tower_blocks(true);

    float max_depth = 0.f;
    for (const auto &block : m_wipe_tower_blocks) {
        max_depth += block.depth;
    }
    //std::cout << " after square " << m_wipe_tower_width << "  depth  " << max_depth << std::endl;

    float min_wipe_tower_depth = get_limit_depth_by_height(m_wipe_tower_height);

    // only for get m_extra_spacing
    {
        if (m_enable_wrapping_detection && max_depth < EPSILON) {
            max_depth = wrapping_wipe_tower_depth;
            if (m_use_rib_wall) {
                m_wipe_tower_width = max_depth;
            }
        }

        if (m_enable_timelapse_print && max_depth < EPSILON) {
            max_depth = min_wipe_tower_depth;
            if (m_use_rib_wall) { m_wipe_tower_width = max_depth; }
        }

        if (max_depth + EPSILON < min_wipe_tower_depth) {
            //if enable rib_wall, there is no need to set extra_spacing
            if (m_use_rib_wall)
                m_rib_length = std::max(m_rib_length, min_wipe_tower_depth * (float) std::sqrt(2));
            else
                m_extra_spacing = std::max(min_wipe_tower_depth / max_depth, m_extra_spacing);
        }

        for (int idx = 0; idx < m_plan.size(); idx++) {
            auto &info = m_plan[idx];
            if (idx == foundation_layer /*&& m_extra_spacing > 1.f + EPSILON*/) {
                // apply solid fill for the first layer
                info.extra_spacing = 1.f;
                for (auto &toolchange : info.tool_changes) {
                    const float new_tool_width = filament_perimeter_width(toolchange.new_tool);
                    const float old_tool_nc_width = filament_nozzle_change_width(toolchange.old_tool);
                    //float x_to_wipe     = volume_to_length(toolchange.wipe_volume, new_tool_width, info.height);
                    float line_len      = m_wipe_tower_width - 2 * new_tool_width;
                    float wipe_depth = (toolchange.required_depth - toolchange.nozzle_change_depth) * m_extra_spacing;
                    float wipe_line_count = wipe_depth / new_tool_width;
                    float nozzle_change_depth = toolchange.nozzle_change_depth * m_extra_spacing;

                    int nozzle_change_line_count = (toolchange.nozzle_change_depth * m_extra_spacing + WT_EPSILON) / old_tool_nc_width;

                    toolchange.required_depth = wipe_depth + nozzle_change_depth;
                    toolchange.wipe_length = wipe_line_count * line_len;
                    toolchange.wipe_volume          = length_to_volume(toolchange.wipe_length, new_tool_width, info.height);
                    toolchange.nozzle_change_length = nozzle_change_line_count * (m_wipe_tower_width - (old_tool_nc_width + new_tool_width));
                    toolchange.nozzle_change_depth  = nozzle_change_depth;
                }
            } else {
                info.extra_spacing = m_extra_spacing;
                for (auto &toolchange : info.tool_changes) {
                    toolchange.required_depth *= m_extra_spacing;
                    toolchange.nozzle_change_depth *= m_extra_spacing;
                    toolchange.wipe_length = volume_to_length(toolchange.wipe_volume, filament_perimeter_width(toolchange.new_tool),
                                                              slab_height_for(toolchange.new_tool, idx, info.height));
                }
            }
        }
    }

    update_all_layer_depth(max_depth);
    set_nozzle_last_layer_id();

    // The band fill and the outer wall are the only parts of the tower free to merge across Z, and both need
    // the finished blocks: the fill has to carry the material interfaces and the depth of the layers it
    // swallows, and the wall has to know which layers still print a band once the fill ladder is in place.
    plan_merged_block_fill();
    plan_merged_outer_wall();
    audit_merged_slabs();

    float diagonal = sqrt(m_wipe_tower_depth * m_wipe_tower_depth + m_wipe_tower_width * m_wipe_tower_width);
    m_rib_length    = std::max({m_rib_length, diagonal});
    m_rib_length += m_extra_rib_length;
    m_rib_length = std::max(diagonal, m_rib_length);
    m_rib_width  = std::min(m_rib_width, std::min(m_wipe_tower_depth, m_wipe_tower_width) / 2.f); // Ensure that the rib wall of the wipetower are attached to the infill.

}

bool WipeTower::can_print_base_layers(int filament_id) const
{
    if (!m_merged_slabs_active)
        return true;
    for (int layer_id = 0; layer_id < (int) m_plan.size(); ++layer_id)
        if (!layer_is_skipped(layer_id) && !filament_can_print_height(filament_id, layer_slab_height(layer_id, m_plan[layer_id].height)))
            return false;
    return true;
}

int WipeTower::get_wall_filament_for_all_layer() const
{
    std::map<int, int> category_counts;
    std::map<int, int> filament_counts;
    int current_tool = m_current_tool;
    for (const auto &layer : m_plan) {
        if (layer.tool_changes.empty()){
            filament_counts[current_tool]++;
            category_counts[get_filament_category(current_tool)]++;
            continue;
        }
        std::unordered_set<int> used_tools;
        std::unordered_set<int> used_category;
        for (size_t i = 0; i < layer.tool_changes.size(); ++i) {
            if (i == 0) {
                filament_counts[layer.tool_changes[i].old_tool]++;
                category_counts[get_filament_category(layer.tool_changes[i].old_tool)]++;
                used_tools.insert(layer.tool_changes[i].old_tool);
                used_category.insert(get_filament_category(layer.tool_changes[i].old_tool));
            }
            if (!used_category.count(get_filament_category(layer.tool_changes[i].new_tool)))
                category_counts[get_filament_category(layer.tool_changes[i].new_tool)]++;
            if (!used_tools.count(layer.tool_changes[i].new_tool))
                filament_counts[layer.tool_changes[i].new_tool]++;
            used_tools.insert(layer.tool_changes[i].new_tool);
            used_category.insert(get_filament_category(layer.tool_changes[i].new_tool));
        }
        current_tool = layer.tool_changes.empty()?current_tool:layer.tool_changes.back().new_tool;
    }

    // The wall spans every layer, including the ones a merged slab leaves empty, so it has to come from a
    // nozzle that can print the base layer height. Drop the others unless that would leave no candidate.
    if (std::any_of(filament_counts.begin(), filament_counts.end(),
                    [this](const std::pair<const int, int> &entry) { return can_print_base_layers(entry.first); })) {
        for (auto iter = filament_counts.begin(); iter != filament_counts.end();)
            iter = can_print_base_layers(iter->first) ? std::next(iter) : filament_counts.erase(iter);
        category_counts.clear();
        for (const auto &entry : filament_counts)
            category_counts[get_filament_category(entry.first)] += entry.second;
    }

    // std::vector<std::pair<int, int>> category_counts_vec;
    int selected_category = -1;
    int selected_count    = 0;

    for (auto iter = category_counts.begin(); iter != category_counts.end(); ++iter) {
        if (iter->second > selected_count) {
            selected_category = iter->first;
            selected_count    = iter->second;
        }
    }

    // std::sort(category_counts_vec.begin(), category_counts_vec.end(), [](const std::pair<int, int> &left, const std::pair<int, int>& right) {
    //     return left.second > right.second;
    // });

    int filament_id    = -1;
    int filament_count = 0;
    for (auto iter = filament_counts.begin(); iter != filament_counts.end(); ++iter) {
        if (m_filament_categories[iter->first] == selected_category && iter->second > filament_count) {
            filament_id    = iter->first;
            filament_count = iter->second;
        }
    }
    return filament_id;
}

float WipeTower::band_fill_depth(const WipeTowerBlock &block, int layer_id) const
{
    // layer_depths already is the depth this layer must fill. The downward max in
    // generate_wipe_tower_blocks raises it to whatever the layers above still print, and internal
    // ribs (m_tower_framework) copy the deepest layer onto every layer on top of that.
    // Do not substitute block.depth here. That is the band's deepest layer anywhere in the tower,
    // including below this one, so a multi-band tower (and a tower whose wall is locked by side
    // scrape, wrapping or timelapse) would fill the whole slot on every layer. The extra is support
    // for material that is not printed above this layer.
    if (layer_id >= 0 && size_t(layer_id) < block.layer_depths.size())
        return block.layer_depths[layer_id];
    return block.depth;
}

int WipeTower::get_wall_filament_for_layer(const WipeTowerInfo &layer, int layer_id, int wall_filament) const
{
    if (layer.tool_changes.empty())
        return -1;

    if (layer_id >= 0 && size_t(layer_id) < m_wall_filament.size() && m_wall_filament[layer_id] >= 0)
        return m_wall_filament[layer_id];

    const bool enforce_height_window = merged_fill_active();
    auto fits_this_layer = [this, &layer, layer_id, enforce_height_window](int filament_id) {
        return !enforce_height_window ||
               filament_can_print_height(filament_id, layer_slab_height(layer_id, layer.height));
    };

    int candidate_id = -1;
    for (size_t idx = 0; idx < layer.tool_changes.size(); ++ idx) {
        if (idx == 0) {
            if (layer.tool_changes[idx].old_tool == wall_filament &&
                is_valid_last_layer(int(layer.tool_changes[idx].old_tool), layer_id, layer.z))
                return wall_filament;
            if (m_filpar[layer.tool_changes[idx].old_tool].category == m_filpar[wall_filament].category &&
                is_valid_last_layer(int(layer.tool_changes[idx].old_tool), layer_id, layer.z) &&
                fits_this_layer(int(layer.tool_changes[idx].old_tool)))
                candidate_id = int(layer.tool_changes[idx].old_tool);
        }
        if (layer.tool_changes[idx].new_tool == wall_filament && fits_this_layer(wall_filament))
            return wall_filament;
        if (candidate_id == -1 &&
            m_filpar[layer.tool_changes[idx].new_tool].category == m_filpar[wall_filament].category &&
            fits_this_layer(int(layer.tool_changes[idx].new_tool)))
            candidate_id = int(layer.tool_changes[idx].new_tool);
    }
    if (candidate_id != -1)
        return candidate_id;

    int fallback_id = int(layer.tool_changes.front().new_tool);
    for (const WipeTowerInfo::ToolChange &tool_change : layer.tool_changes)
        if (fits_this_layer(int(tool_change.new_tool)))
            return int(tool_change.new_tool);
    for (const WipeTowerInfo::ToolChange &tool_change : layer.tool_changes)
        if (m_filpar[tool_change.new_tool].nozzle_diameter < m_filpar[fallback_id].nozzle_diameter)
            fallback_id = int(tool_change.new_tool);
    return fallback_id;
}

// Which filament tops this band up on this layer, decided without looking at the slab height: the one that
// last purged into it, the one that rammed out of it, the one already loaded when the layer started, or the
// wall filament. The Z ladder has to be planned against this filament, because this is the one that ends up
// extruding the slab; picking the height first and the filament afterwards lets a nozzle be handed a slab
// outside its own window.
int WipeTower::block_fill_filament_base(
    const WipeTowerInfo &layer, int layer_id, const WipeTowerBlock &block, int wall_filament) const
{
    int last_filament_change = -1;
    int last_nozzle_change   = -1;
    for (const WipeTowerInfo::ToolChange &tool_change : layer.tool_changes) {
        if (get_block_key(int(tool_change.new_tool), layer_id) == block.block_key)
            last_filament_change = int(tool_change.new_tool);
        if (tool_change.nozzle_change_depth > EPSILON &&
            is_need_ramming(int(tool_change.old_tool), int(tool_change.new_tool), layer_id) &&
            is_valid_last_layer(int(tool_change.old_tool), layer_id, layer.z) &&
            get_block_key(int(tool_change.old_tool), layer_id) == block.block_key)
            last_nozzle_change = int(tool_change.old_tool);
    }

    int fill_filament = last_filament_change != -1 ? last_filament_change : last_nozzle_change;
    if (fill_filament == -1 && ! layer.tool_changes.empty() &&
        get_block_key(int(layer.tool_changes.front().old_tool), layer_id) == block.block_key)
        fill_filament = int(layer.tool_changes.front().old_tool);
    if (fill_filament == -1 && merged_fill_active()) {
        fill_filament = same_category_filler(layer, block.block_key);
        // A layer without tool changes is laid down by whatever is still loaded.
        if (fill_filament == -1 && layer.tool_changes.empty()) {
            const int loaded = loaded_filament(layer_id);
            if (loaded >= 0 && get_filament_category(loaded) == decode_block_category(block.block_key))
                fill_filament = loaded;
        }
        return fill_filament;
    }
    if (fill_filament == -1)
        fill_filament = wall_filament;
    return fill_filament;
}

int WipeTower::get_block_fill_filament(
    const WipeTowerInfo &layer, int layer_id, const WipeTowerBlock &block, int wall_filament) const
{
    // The support under an extra TPU lane already chose one filament for the whole slab. Keep that
    // material: swapping in whoever happens to visit this layer turns every slab into an interface.
    if (layer_id >= 0 && layer_id < (int) block.layer_fill_filament.size() && block.layer_fill_filament[layer_id] >= 0) {
        const int   planned = block.layer_fill_filament[layer_id];
        const float height  = block.layer_heights[layer_id];
        if (filament_can_print_height(planned, height))
            return planned;
        const int category = get_filament_category(planned);
        for (const WipeTowerInfo::ToolChange &tool_change : layer.tool_changes) {
            for (int filament : { int(tool_change.new_tool), int(tool_change.old_tool) }) {
                if (get_filament_category(filament) == category && filament_can_print_height(filament, height))
                    return filament;
            }
        }
        return planned;
    }
    const int fill_filament = block_fill_filament_base(layer, layer_id, block, wall_filament);
    if (fill_filament < 0)
        return -1;

    const float height = block.layer_heights[layer_id];
    if (! filament_can_print_height(fill_filament, height)) {
        const bool same_category_only = merged_fill_active();
        auto       can_take_over      = [&](int filament) {
            return filament_can_print_height(filament, height) &&
                   (!same_category_only || get_filament_category(filament) == decode_block_category(block.block_key));
        };
        for (const WipeTowerInfo::ToolChange &tool_change : layer.tool_changes) {
            if (can_take_over(int(tool_change.new_tool)))
                return int(tool_change.new_tool);
            if (can_take_over(int(tool_change.old_tool)))
                return int(tool_change.old_tool);
        }
        // Nothing on this layer covers the planned height. Extruding it anyway is the least bad option left
        // this late - dropping the band would leave a void the tower cannot recover from - but the plan that
        // got here is wrong, so say so instead of laying it down silently.
        BOOST_LOG_TRIVIAL(error)
            << "WipeTower: band " << block.block_key << " on layer " << layer_id << " is planned " << height
            << " mm high, which no filament visiting that layer can extrude; filament " << fill_filament
            << " lays it down outside its " << min_layer_height_for_filament(fill_filament) << ".."
            << max_layer_height_for_filament(fill_filament) << " mm window";
    }
    return fill_filament;
}

// Reduce the feedrate of the trailing extrusion moves of an assembled wipe-tower visit so
// that its last `tail_volume` mm3 of extrusion is printed at `speed_factor` of the original
// speed. This softens the flow discontinuity when the toolhead leaves the wipe tower for the
// model. It operates uniformly on any trailing toolpath (outer wall / wipe line / grid) and on
// both linear (G1) and arc (G2/G3) moves. The wipe-tower writer emits relative E, so a move's
// extruded volume equals E * filament_area. `tail_volume <= 0` slows the whole visit.
static void apply_wipe_tower_tail_slowdown(std::string &gcode, float tail_volume, float speed_factor, float filament_area,
                                           const std::optional<Vec2f> &initial_position = std::nullopt,
                                           const std::optional<Vec2f> &tool_change_start_position = std::nullopt)
{
    if (gcode.empty() || filament_area <= 0.f || speed_factor >= 1.f - 1e-4f)
        return;
    speed_factor = std::clamp(speed_factor, 0.05f, 1.f);

    // Split into lines (content only). Rejoining with '\n' reproduces the original exactly.
    std::vector<std::string> lines;
    {
        size_t start = 0;
        while (true) {
            size_t nl = gcode.find('\n', start);
            if (nl == std::string::npos) { lines.emplace_back(gcode.substr(start)); break; }
            lines.emplace_back(gcode.substr(start, nl - start));
            start = nl + 1;
        }
    }

    // Return the numeric value of the token starting with `key` on a G-code move line.
    auto get_param = [](const std::string &line, char key, float &out) -> bool {
        size_t i = 0, n = line.size();
        while (i < n) {
            while (i < n && line[i] == ' ') ++i;
            if (i >= n) break;
            size_t tok = i;
            while (i < n && line[i] != ' ') ++i;
            if (line[tok] == key && i - tok >= 2) {
                out = (float) string_to_double_decimal_point(std::string_view(line).substr(tok + 1, i - tok - 1));
                return true;
            }
        }
        return false;
    };

    // Replace the F value of a move line, or append one if the line has no explicit feedrate.
    auto set_feedrate = [](std::string &line, float f) {
        int fi = std::max(1, (int) std::floor(f + 0.5f));
        size_t i = 0, n = line.size();
        while (i < n) {
            while (i < n && line[i] == ' ') ++i;
            size_t tok = i;
            while (i < n && line[i] != ' ') ++i;
            if (tok < n && line[tok] == 'F') {
                line = line.substr(0, tok) + "F" + std::to_string(fi) + line.substr(i);
                return;
            }
        }
        line += " F" + std::to_string(fi);
    };

    // A plain linear move made of G1/X/Y/E/F tokens only, which can be split in two.
    auto is_plain_linear = [](const std::string &line) {
        size_t i = 0, n = line.size();
        bool first = true;
        while (i < n) {
            while (i < n && line[i] == ' ') ++i;
            if (i >= n) break;
            size_t tok = i;
            while (i < n && line[i] != ' ') ++i;
            if (first) {
                if (line.compare(tok, i - tok, "G1") != 0)
                    return false;
                first = false;
            } else if (std::string_view("XYEF").find(line[tok]) == std::string_view::npos)
                return false;
        }
        return true;
    };

    struct Move { int line_idx; float e; float feedrate; bool splittable; Vec2f from; Vec2f to; };
    std::vector<Move> moves;
    float current_feedrate = 0.f;
    // Mirror post_process_wipe_tower_moves(): the placeholder resets local XY to the tool-change
    // start. Continuing from pre-toolchange coordinates would create a bogus split point.
    Vec2f position = initial_position.value_or(Vec2f::Zero());
    bool  has_x_known = initial_position.has_value(), has_y_known = initial_position.has_value();
    for (int idx = 0; idx < (int) lines.size(); ++idx) {
        const std::string &line = lines[idx];
        if (line == "[change_filament_gcode]") {
            if (tool_change_start_position) {
                position = *tool_change_start_position;
                has_x_known = has_y_known = true;
            } else {
                has_x_known = has_y_known = false;
            }
            continue;
        }
        size_t p = line.find_first_not_of(' ');
        if (p == std::string::npos || line[p] != 'G') continue;
        size_t q = line.find(' ', p);
        std::string cmd = line.substr(p, (q == std::string::npos ? line.size() : q) - p);
        bool is_arc = (cmd == "G2" || cmd == "G3");
        if (cmd != "G1" && !is_arc) continue;

        float fval = 0.f, eval = 0.f, xval = 0.f, yval = 0.f;
        bool has_f = get_param(line, 'F', fval);
        bool has_e = get_param(line, 'E', eval);
        bool has_x = get_param(line, 'X', xval);
        bool has_y = get_param(line, 'Y', yval);
        bool is_motion = has_x || has_y || is_arc;
        // Track the running print feedrate from motion moves and standalone "G1 F" lines,
        // but ignore pure retract/de-retract moves (E only) so retract speeds don't leak in.
        if (has_f && (is_motion || (!has_e && !has_x && !has_y)))
            current_feedrate = fval;
        const Vec2f from        = position;
        const bool  from_known  = has_x_known && has_y_known;
        if (has_x) { position.x() = xval; has_x_known = true; }
        if (has_y) { position.y() = yval; has_y_known = true; }
        if (has_e && eval > 0.f && is_motion && current_feedrate > 0.f)
            moves.push_back({idx, eval, current_feedrate, from_known && !is_arc && is_plain_linear(line), from, position});
    }
    if (moves.empty())
        return;

    float tail_len = (tail_volume <= 0.f) ? std::numeric_limits<float>::max() : tail_volume / filament_area;
    float acc = 0.f;
    for (int m = (int) moves.size() - 1; m >= 0; --m) {
        if (acc >= tail_len - 1e-6f)
            break;
        const Move &move      = moves[m];
        const float remaining = tail_len - acc;
        acc += move.e;
        if (move.splittable && move.e > remaining + 1e-5f) {
            const float  t_head = 1.f - remaining / move.e;
            const Vec2f  mid    = move.from + (move.to - move.from) * t_head;
            const std::string mid_x = float_to_string_decimal_point(mid.x(), 3);
            const std::string mid_y = float_to_string_decimal_point(mid.y(), 3);
            // post_process_wipe_tower_moves() rewrites X/Y only when the position changes, so a split
            // point that prints identical to either end would leave raw tower coordinates in the G-code.
            const auto prints_as = [&mid_x, &mid_y](const Vec2f &p) {
                return mid_x == float_to_string_decimal_point(p.x(), 3) && mid_y == float_to_string_decimal_point(p.y(), 3);
            };
            // Tail too short to print on its own: keep this move at its original feedrate.
            if (prints_as(move.to))
                break;
            // Head too short to print on its own: slow the whole move below.
            if (prints_as(move.from)) {
                set_feedrate(lines[move.line_idx], move.feedrate * speed_factor);
                break;
            }
            // Only the end of this move is in the tail: its head keeps the original feedrate.
            std::string &line   = lines[move.line_idx];
            const std::string e_head = float_to_string_decimal_point(move.e * t_head, 5);
            const float  e_tail = move.e - float(string_to_double_decimal_point(e_head));
            float f_head = 0.f;
            const bool has_f_head = get_param(line, 'F', f_head);
            std::string head = "G1 X" + mid_x + " Y" + mid_y + " E" + e_head;
            if (has_f_head)
                head += " F" + float_to_string_decimal_point(f_head, 3);
            std::string tail = "G1 X" + float_to_string_decimal_point(move.to.x(), 3) + " Y" +
                               float_to_string_decimal_point(move.to.y(), 3) + " E" +
                               float_to_string_decimal_point(e_tail, 5);
            set_feedrate(tail, move.feedrate * speed_factor);
            line = head + "\n" + tail;
            break;
        }
        set_feedrate(lines[move.line_idx], move.feedrate * speed_factor);
    }

    std::string out;
    out.reserve(gcode.size() + 16);
    for (size_t i = 0; i < lines.size(); ++i) {
        out += lines[i];
        if (i + 1 < lines.size())
            out += '\n';
    }
    gcode = std::move(out);
}

void WipeTower::generate_new(std::vector<std::vector<WipeTower::ToolChangeResult>> &result)
{
    if (m_plan.empty())
        return;
    //m_extra_spacing = 1.f;
    m_wipe_tower_height = m_plan.back().z;//real wipe_tower_height
    plan_tower_new();
    m_layer_info = m_plan.begin();

    for (const auto &layer : m_plan) {
        if (!layer.tool_changes.empty()) {
            m_current_tool = layer.tool_changes.front().old_tool;
            break;
        }
    }

    for (auto &used : m_used_filament_length) // reset used filament stats
        used = 0.f;

    int wall_filament = get_wall_filament_for_all_layer();
    if (m_use_gap_wall)
        get_all_wall_skip_points(wall_filament);

    std::vector<WipeTower::ToolChangeResult> layer_result;
    int index = 0;
    for (auto layer : m_plan) {
        reset_block_status();
        m_cur_layer_id = index++;
        set_layer(layer.z, layer_slab_height(m_cur_layer_id, layer.height), 0, false, layer.z == m_plan.back().z);
        if (layer_is_skipped(m_cur_layer_id)) {
            // The nozzle that would fill this layer cannot lay down what is left of it, so the tower waits:
            // no tool change happens here, and the height rolls into the next layer the tower does print.
            // The material below the next printed layer is still the one of the last printed layer, so the
            // fill front each block reached has to be carried over for get_floating_area().
            for (WipeTowerBlock &block : m_wipe_tower_blocks)
                block.finish_depth[m_cur_layer_id] = m_cur_layer_id > 0 ? block.finish_depth[m_cur_layer_id - 1] :
                                                                          block.start_depth + block.depth;
            result.emplace_back();
            continue;
        }
        if (m_layer_info->depth < m_perimeter_width) {
            // Same empty-layer slot as a skipped layer so later G-code stays indexed by planned layer.
            result.emplace_back();
            continue;
        }
        // Do not centre a shallow layer with y_shift when the scrape needs the outer walls locked to the
        // full tower depth; that shift is exactly the depth shrinkage we just disabled.
        if (m_wipe_tower_blocks.size() == 1 && ! keeps_outer_walls_unshrunk()) {
            if (m_layer_info->depth < m_wipe_tower_depth - m_perimeter_width) {
                // align y shift to perimeter width
                float dy  = m_extra_spacing * m_perimeter_width;
                m_y_shift = (m_wipe_tower_depth - m_layer_info->depth) / 2.f;
                m_y_shift = align_round(m_y_shift, dy);
            }
        }

        //get_wall_skip_points(layer);

        ToolChangeResult finish_layer_tcr;
        ToolChangeResult timelapse_wall;

        bool only_generate_wall = m_enable_timelapse_print || (m_enable_wrapping_detection && m_slice_used_filaments <= 1);
        int wall_idx = get_wall_filament_for_layer(layer, int(m_cur_layer_id), wall_filament);
        const bool wall_reserved = size_t(m_cur_layer_id) < m_wall_reserved.size() &&
                                   m_wall_reserved[m_cur_layer_id] != 0;
        const bool extrude_outer_wall = !only_generate_wall && !wall_reserved;
        const float wall_height = size_t(m_cur_layer_id) < m_wall_slab_height.size() &&
                                  m_wall_slab_height[m_cur_layer_id] > 0.f ?
                                  m_wall_slab_height[m_cur_layer_id] : m_layer_height;
        if (wall_idx != -1 && m_merged_slabs_active &&
            ! filament_can_print_height(wall_idx, wall_height))
            BOOST_LOG_TRIVIAL(warning) << "WipeTower: no nozzle on layer " << m_cur_layer_id
                                       << " covers the wall layer height, drawing the wall with filament " << wall_idx;

        // this layer has no tool_change
        if (wall_idx == -1) {
            bool need_insert_solid_infill = false;
            for (const WipeTowerBlock &block : m_wipe_tower_blocks) {
                if (block.layers_type[m_cur_layer_id] != WipeTowerLayerType::Normal) {
                    need_insert_solid_infill = true;
                    break;
                }
            }

            if (need_insert_solid_infill) {
                wall_idx = m_current_tool;
            } else {
                if (only_generate_wall) {
                    timelapse_wall = only_generate_out_wall(true);
                }
                finish_layer_tcr = finish_layer_new(extrude_outer_wall, layer.extruder_fill, true, wall_height, wall_reserved);
                // Only a dense fill supports the layer above; a sparse grid has to be bridged.
                const bool dense_fill = m_merged_slabs_active && layer.extruder_fill && m_adhesion && is_first_layer();
                std::for_each(m_wipe_tower_blocks.begin(), m_wipe_tower_blocks.end(), [this, dense_fill](WipeTowerBlock &block) {
                    block.finish_depth[this->m_cur_layer_id] =
                        (!dense_fill || block.layer_reserved[this->m_cur_layer_id]) ? block.start_depth :
                        block.start_depth + band_fill_depth(block, this->m_cur_layer_id);
                });
            }
        }

        // generate tool change
        bool insert_wall = false;
        int  insert_finish_layer_idx = -1;
        if (wall_idx != -1 && only_generate_wall) {
            timelapse_wall = only_generate_out_wall(true);
        }
        for (int i = 0; i < int(layer.tool_changes.size()); ++i) {
            ToolChangeResult wall_gcode;
            if (m_current_tool != layer.tool_changes[i].old_tool) {
                BOOST_LOG_TRIVIAL(warning) << "WipeTower: realigning generated tool state on layer "
                                           << m_cur_layer_id << " from " << m_current_tool
                                           << " to planned filament " << layer.tool_changes[i].old_tool;
                m_current_tool = layer.tool_changes[i].old_tool;
            }
            if (i == 0 && (layer.tool_changes[i].old_tool == wall_idx)) {
                finish_layer_tcr = finish_layer_new(extrude_outer_wall, false, false, wall_height, wall_reserved);
            }
            bool        solid_nozzlechange = false, solid_toolchange = false;
            const auto * block = get_block_by_category(get_block_key(layer.tool_changes[i].new_tool, m_cur_layer_id), false);
            if (block) solid_toolchange = block->layers_type[m_cur_layer_id] == WipeTowerLayerType::Contact;

            const auto * block2 = get_block_by_category(get_block_key(layer.tool_changes[i].old_tool, m_cur_layer_id), false);
            if(block2) solid_nozzlechange = block2->layers_type[m_cur_layer_id] == WipeTowerLayerType::Contact;
            layer_result.emplace_back(tool_change_new(layer.tool_changes[i].new_tool, solid_toolchange,solid_nozzlechange));

            if (i == 0 && (layer.tool_changes[i].old_tool == wall_idx)) {

            }
            else if (layer.tool_changes[i].new_tool == wall_idx) {
                finish_layer_tcr = finish_layer_new(extrude_outer_wall, false, false, wall_height, wall_reserved);
                insert_finish_layer_idx = i;
            }
        }

        // insert finish block
        if (wall_idx != -1) {
            if (layer.tool_changes.empty()) {
                finish_layer_tcr = finish_layer_new(extrude_outer_wall, false, false, wall_height, wall_reserved);
            }

            for (WipeTowerBlock& block : m_wipe_tower_blocks) {
                // The band is the trench of a merged slab printed on a layer above; nothing may go in here.
                if (block.layer_reserved[m_cur_layer_id]) {
                    block.finish_depth[m_cur_layer_id] = block.start_depth;
                    continue;
                }
                const float planned_depth = block.start_depth + band_fill_depth(block, m_cur_layer_id);
                block.finish_depth[m_cur_layer_id] = m_merged_slabs_active ? std::min(block.cur_depth, planned_depth) :
                                                                            block.start_depth + block.depth;
                if (block.cur_depth + EPSILON >= planned_depth - m_perimeter_width) {
                    continue;
                }
                int id = std::find_if(m_wipe_tower_blocks.begin(), m_wipe_tower_blocks.end(), [&](const WipeTowerBlock &b) { return &b == &block; }) - m_wipe_tower_blocks.begin();
                bool block_solid = block.layers_type[m_cur_layer_id] == WipeTowerLayerType::Contact || block.layers_type[m_cur_layer_id] == WipeTowerLayerType::Contact_UP ||
                                   block.layers_type[m_cur_layer_id] == WipeTowerLayerType::Solid;
                int finish_layer_filament = get_block_fill_filament(
                    layer, int(m_cur_layer_id), block, wall_idx);
                // No filament of this band's category visits the layer. The slot is still there — the
                // next band sits on top of it — so a filament that does visit has to fill it. Leaving
                // the slot empty is the hole under a TPU band on a layer that only purges TPU.
                if (finish_layer_filament < 0) {
                    for (const ToolChangeResult &tcr : layer_result)
                        if (filament_can_print_height(int(tcr.new_tool), block.layer_heights[m_cur_layer_id])) {
                            finish_layer_filament = int(tcr.new_tool);
                            break;
                        }
                    if (finish_layer_filament < 0 && wall_idx >= 0)
                        finish_layer_filament = wall_idx;
                }
                if (finish_layer_filament < 0)
                    continue;
                // The band gcode is appended to one of this layer's tower visits, so only a filament that
                // visit actually has loaded can lay it down. get_block_fill_filament() may swap in another
                // filament when the planned one cannot print the band height; such a swap is only usable
                // when this layer really visits the tower with it.
                auto can_carry_band = [this, &layer_result, &finish_layer_tcr, &block](int filament_id) {
                    if (filament_id < 0)
                        return false;
                    if (std::any_of(layer_result.begin(), layer_result.end(),
                                    [filament_id](const ToolChangeResult &tcr) { return tcr.new_tool == filament_id; }))
                        return true;
                    // The outgoing tool is still loaded at the start of its visit, so it can lay
                    // this band down before the change. Limiting that to a block that has never
                    // been used dropped the PETG fill on every later PETG->PLA layer and the
                    // fallback then extruded it with PLA.
                    const bool own_category = get_filament_category(filament_id) == decode_block_category(block.block_key);
                    if ((block.last_filament_change_id == -1 || own_category) &&
                        std::any_of(layer_result.begin(), layer_result.end(),
                                    [filament_id](const ToolChangeResult &tcr) { return tcr.initial_tool == filament_id; }))
                        return true;
                    // A layer without tool changes has nothing but the finish-layer visit to hang the band on.
                    // That visit may currently lay down nothing (its wall merged upwards and every other band
                    // is reserved), but the band gcode merged into it turns it into a real visit again.
                    return this->layer_finished() && finish_layer_tcr.new_tool == filament_id;
                };
                if (! can_carry_band(finish_layer_filament)) {
                    // A band stacked on, or under, another material does not bond to it.
                    const bool same_category_only = merged_fill_active();
                    auto       may_fill           = [&](int filament_id) {
                        return !same_category_only ||
                               get_filament_category(filament_id) == decode_block_category(block.block_key);
                    };
                    int fallback = -1;
                    for (const ToolChangeResult &tcr : layer_result)
                        if (filament_can_print_height(tcr.new_tool, block.layer_heights[m_cur_layer_id]) &&
                            may_fill(tcr.new_tool)) {
                            fallback = tcr.new_tool;
                            break;
                        }
                    if (fallback < 0 && can_carry_band(wall_idx) && may_fill(wall_idx))
                        fallback = wall_idx;
                    if (fallback < 0 && ! layer_result.empty() && may_fill(layer_result.back().new_tool))
                        fallback = layer_result.back().new_tool;
                    // Still nothing of this category. Fill with whoever is actually on the layer; an
                    // empty slot under the next band will not hold the tower up.
                    if (fallback < 0) {
                        for (const ToolChangeResult &tcr : layer_result)
                            if (filament_can_print_height(int(tcr.new_tool), block.layer_heights[m_cur_layer_id])) {
                                fallback = int(tcr.new_tool);
                                break;
                            }
                        if (fallback < 0 && can_carry_band(wall_idx))
                            fallback = wall_idx;
                        if (fallback < 0 && ! layer_result.empty())
                            fallback = int(layer_result.back().new_tool);
                        // The planner never saw this material swap, so no Contact was marked for it.
                        // Print the foreign fill dense so the band above still has a surface to bond to.
                        if (fallback >= 0)
                            block_solid = true;
                    }
                    BOOST_LOG_TRIVIAL(warning) << "WipeTower: filament " << finish_layer_filament
                                               << " does not visit the tower on layer " << m_cur_layer_id
                                               << ", band " << block.block_key << " falls back to filament " << fallback;
                    if (fallback < 0)
                        continue;
                    finish_layer_filament = fallback;
                }
                if (! filament_can_print_height(finish_layer_filament, block.layer_heights[m_cur_layer_id]))
                    BOOST_LOG_TRIVIAL(warning) << "WipeTower: filament " << finish_layer_filament << " fills band "
                                               << block.block_key << " on layer " << m_cur_layer_id << " at "
                                               << block.layer_heights[m_cur_layer_id] << " mm, outside its nozzle window";
                // Cancel the block of the last layer
                if (!is_valid_last_layer(finish_layer_filament, m_cur_layer_id, layer.z)) continue;
                ToolChangeResult finish_block_tcr;
                if (block_solid) {
                    finish_block_tcr = finish_block_solid(block, finish_layer_filament, layer.extruder_fill, block.layers_type[m_cur_layer_id]);
                }
                else {
                    finish_block_tcr = finish_block(block, finish_layer_filament, layer.extruder_fill);
                }
                // A dense fill supports the complete footprint. A sparse grid does not, so keep the purge front
                // and let the next layer detect which part of its wipe lines bridges the grid.
                if (m_merged_slabs_active) {
                    const bool dense_fill = block_solid ||
                                            (m_adhesion && is_first_layer_for_slab(block.layer_heights[m_cur_layer_id]));
                    block.finish_depth[m_cur_layer_id] = dense_fill ? planned_depth : std::min(block.cur_depth, planned_depth);
                } else {
                    block.finish_depth[m_cur_layer_id] = block_solid ? block.start_depth + block.depth : block.cur_depth;
                }

                bool has_inserted = false;
                {
                    auto fc_iter = std::find_if(layer_result.begin(), layer_result.end(),
                                                [&finish_layer_filament](const WipeTower::ToolChangeResult &item) { return item.new_tool == finish_layer_filament; });
                    if (fc_iter != layer_result.end()) {
                        *fc_iter = merge_tcr(*fc_iter, finish_block_tcr);
                        has_inserted = true;
                    }
                }

                const bool own_category_fill =
                    get_filament_category(finish_layer_filament) == decode_block_category(block.block_key);
                if ((block.last_filament_change_id == -1 || own_category_fill) && !has_inserted) {
                    auto nc_iter = std::find_if(layer_result.begin(), layer_result.end(),
                                                [&finish_layer_filament](const WipeTower::ToolChangeResult &item) { return item.initial_tool == finish_layer_filament; });
                    if (nc_iter != layer_result.end()) {
                        *nc_iter = merge_tcr(finish_block_tcr, *nc_iter);
                        has_inserted = true;
                    }
                }

                if (!has_inserted && is_valid_gcode(finish_block_tcr.gcode)) {
                    if (this->layer_finished() && finish_layer_tcr.new_tool == finish_layer_filament)
                        finish_layer_tcr = merge_tcr(finish_layer_tcr, finish_block_tcr);
                    else
                        BOOST_LOG_TRIVIAL(error) << "WipeTower: no visit on layer " << m_cur_layer_id
                                                 << " can carry band " << block.block_key << " of filament "
                                                 << finish_layer_filament << ", dropping its fill";
                }
            }
        }
        // record the contact layers of different categories
        if (layer_result.empty()) {
            // There is nothing to merge finish_layer with. A visit whose wall went to a group above and whose
            // bands are all reserved still writes a travel and a deretract, so it looks like valid gcode while
            // laying down nothing. Extrusions are only recorded for real material, so they tell the two apart:
            // drop the visit and leave this layer without a tower visit at all.
            // With only_generate_wall the timelapse wall is inserted in front below, and the G-code side takes it
            // before asking for the finish_layer visit, so that visit has to stay even when it lays down nothing.
            if (only_generate_wall || (is_valid_gcode(finish_layer_tcr.gcode) && !finish_layer_tcr.extrusions.empty()))
                layer_result.emplace_back(std::move(finish_layer_tcr));
        }
        else if (is_valid_gcode(finish_layer_tcr.gcode)) {
            if (insert_finish_layer_idx == -1)
                layer_result[0] = merge_tcr(finish_layer_tcr, layer_result[0]);
            else
                layer_result[insert_finish_layer_idx] = merge_tcr(layer_result[insert_finish_layer_idx], finish_layer_tcr);
        }

        if (only_generate_wall && !timelapse_wall.gcode.empty()) {
            layer_result.insert(layer_result.begin(), std::move(timelapse_wall));
        }

        // Soften the flow discontinuity when the toolhead leaves the wipe tower for the model:
        // slow down the last m_tail_slowdown_volume mm3 of every visit (whatever the trailing
        // toolpath is: outer wall / wipe line / grid). Only for single-extruder multi-nozzle
        // machines (handle_hotend_as_extruder) and never on an extrusion that sits on the bed.
        if (m_handle_hotend_as_extruder && m_tail_speed_factor < 1.f) {
            auto slow_visit = [&](ToolChangeResult &tcr) {
                size_t tail_tool = tcr.new_tool < m_filpar.size() ? tcr.new_tool : m_current_tool;
                float  area      = tail_tool < m_filpar.size() ? m_filpar[tail_tool].filament_area : 0.f;
                apply_wipe_tower_tail_slowdown(tcr.gcode, m_tail_slowdown_volume, m_tail_speed_factor, area,
                                               tcr.start_pos, tcr.tool_change_start_pos);
            };
            // Single-diameter: the nominal layer height is the extrusion height.
            if (!has_multiple_nozzle_diameters()) {
                if (!is_first_layer()) {
                    for (auto &tcr : layer_result)
                        slow_visit(tcr);
                }
            } else {
                // Multi-diameter: current Z minus this nozzle's own slab. A merged slab can land on
                // the bed while the nominal layer height does not; that visit keeps its feedrate.
                for (auto &tcr : layer_result) {
                    size_t tail_tool = tcr.new_tool < m_filpar.size() ? tcr.new_tool : m_current_tool;
                    if (is_first_layer_for_slab(filament_extrusion_height(int(tail_tool))))
                        continue;
                    slow_visit(tcr);
                }
            }
        }

        result.emplace_back(std::move(layer_result));
    }
    // Layers the tower skips carry no wall, so the wall map can be shorter than the plan.
    assert(m_outer_wall.size() <= m_plan.size());
}

#if 0
// Processes vector m_plan and calls respective functions to generate G-code for the wipe tower
// Resulting ToolChangeResults are appended into vector "result"
void WipeTower::generate(std::vector<std::vector<WipeTower::ToolChangeResult>> &result)
{
	if (m_plan.empty())
        return;

    m_extra_spacing = 1.f;

	plan_tower();
    // BBS
#if 0
    for (int i=0;i<5;++i) {
        save_on_last_wipe();
        plan_tower();
    }
#endif

    m_layer_info = m_plan.begin();

    // we don't know which extruder to start with - we'll set it according to the first toolchange
    for (const auto& layer : m_plan) {
        if (!layer.tool_changes.empty()) {
            m_current_tool = layer.tool_changes.front().old_tool;
            break;
        }
    }

    for (auto& used : m_used_filament_length) // reset used filament stats
        used = 0.f;

    m_old_temperature = -1; // reset last temperature written in the gcode

    std::vector<WipeTower::ToolChangeResult> layer_result;
    int index = 0;
	for (auto layer : m_plan)
	{
        m_cur_layer_id = index++;
        set_layer(layer.z, layer.height, 0, false/*layer.z == m_plan.front().z*/, layer.z == m_plan.back().z);
        // BBS
        //m_internal_rotation += 180.f;

        if (m_layer_info->depth < m_perimeter_width)
            continue;

        if (! keeps_outer_walls_unshrunk() && m_layer_info->depth < m_wipe_tower_depth - m_perimeter_width) {
            // align y shift to perimeter width
            float dy = m_extra_spacing * m_perimeter_width;
            m_y_shift = (m_wipe_tower_depth - m_layer_info->depth) / 2.f;
            m_y_shift = align_round(m_y_shift, dy);
        }

        // BBS: consider both soluable and support properties
        int idx = first_toolchange_to_nonsoluble_nonsupport (layer.tool_changes);
        ToolChangeResult finish_layer_tcr;
        ToolChangeResult timelapse_wall;

        if (idx == -1) {
            // if there is no toolchange switching to non-soluble, finish layer
            // will be called at the very beginning. That's the last possibility
            // where a nonsoluble tool can be.
            if (m_enable_timelapse_print) {
                timelapse_wall = only_generate_out_wall();
            }
            finish_layer_tcr = finish_layer(m_enable_timelapse_print ? false : true, layer.extruder_fill);
        }

        for (int i=0; i<int(layer.tool_changes.size()); ++i) {
            if (i == 0 && m_enable_timelapse_print) {
                timelapse_wall = only_generate_out_wall();
            }

            if (i == idx) {
                layer_result.emplace_back(tool_change(layer.tool_changes[i].new_tool, m_enable_timelapse_print ? false : true, false));
                // finish_layer will be called after this toolchange
                finish_layer_tcr = finish_layer(false, layer.extruder_fill);
            }
            else {
                if (idx == -1 && i == 0) {
                    layer_result.emplace_back(tool_change(layer.tool_changes[i].new_tool, false, true));
                } else {
                    layer_result.emplace_back(tool_change(layer.tool_changes[i].new_tool, false, false));
                }
            }
        }

        if (layer_result.empty()) {
            // there is nothing to merge finish_layer with
            layer_result.emplace_back(std::move(finish_layer_tcr));
        }
        else {
            if (idx == -1)
                layer_result[0] = merge_tcr(finish_layer_tcr, layer_result[0]);
            else if (is_valid_gcode(finish_layer_tcr.gcode))
                layer_result[idx] = merge_tcr(layer_result[idx], finish_layer_tcr);
        }

        if (m_enable_timelapse_print) {
            layer_result.insert(layer_result.begin(), std::move(timelapse_wall));
        }

		result.emplace_back(std::move(layer_result));
	}
}
#endif
WipeTower::ToolChangeResult WipeTower::only_generate_out_wall(bool is_new_mode)
{
    size_t old_tool = m_current_tool;

    // Drawn like the regular outer wall in finish_layer_new(): capped width, except on the first layer.
    bool        first_layer = is_first_layer();
    const float full_width  = filament_perimeter_width(int(m_current_tool));
    const float capped      = capped_wall_grid_width(int(m_current_tool));
    const float wall_width  = (!first_layer && capped + WT_EPSILON < full_width) ? capped : full_width;
    const float wall_flow  = extrusion_flow_for(m_layer_height, wall_width);
    m_out_wall_only_z     = m_z_pos;
    m_out_wall_only_width = wall_width;

    WipeTowerWriter writer(m_layer_height, wall_width, m_gcode_flavor, m_filpar, m_enable_arc_fitting);
    writer.set_extrusion_flow(wall_flow)
        .set_z(m_z_pos)
        .set_initial_tool(m_current_tool)
        .set_y_shift(m_y_shift - (m_current_shape == SHAPE_REVERSED ? m_layer_info->toolchanges_depth() : 0.f));

    set_for_wipe_tower_writer(writer);

    // Slow down on the 1st layer.
    float feedrate = calc_tower_feedrate(m_current_tool, m_extrusion_flow);
    float           fill_box_y = m_layer_info->toolchanges_depth() + m_perimeter_width;
    box_coordinates fill_box(Vec2f(m_perimeter_width, fill_box_y), m_wipe_tower_width - 2 * m_perimeter_width, m_layer_info->depth - fill_box_y);

    writer.set_initial_position((m_left_to_right ? fill_box.ru : fill_box.lu), // so there is never a diagonal travel
                                m_wipe_tower_width, m_wipe_tower_depth, m_internal_rotation);

    bool toolchanges_on_layer = m_layer_info->toolchanges_depth() > WT_EPSILON;

    // we are in one of the corners, travel to ld along the perimeter:
    // BBS: Delete some unnecessary travel
    //if (writer.x() > fill_box.ld.x() + EPSILON) writer.travel(fill_box.ld.x(), writer.y());
    //if (writer.y() > fill_box.ld.y() + EPSILON) writer.travel(writer.x(), fill_box.ld.y());
    writer.append(";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Wipe_Tower_Start) + "\n");
    // outer perimeter (always):
    // BBS

    float wipe_tower_depth = m_layer_info->depth + m_perimeter_width;
    if (is_new_mode && (m_enable_timelapse_print || m_enable_wrapping_detection || keeps_outer_walls_unshrunk()))
        wipe_tower_depth = m_wipe_tower_depth;
    box_coordinates wt_box(Vec2f(0.f, (m_current_shape == SHAPE_REVERSED ? m_layer_info->toolchanges_depth() : 0.f)), m_wipe_tower_width, wipe_tower_depth);
    wt_box = align_perimeter(wt_box);
    Polygon outer_wall;
    //if (m_use_gap_wall)
    //    generate_support_wall(writer, wt_box, feedrate, first_layer);
    //else
    //    writer.rectangle(wt_box, feedrate);
    outer_wall = generate_support_wall_new(writer, wt_box, feedrate, first_layer, m_use_rib_wall, true, m_use_gap_wall);
    m_outer_wall[m_z_pos].push_back(to_polyline(outer_wall));
    // Now prepare future wipe. box contains rectangle that was extruded last (ccw).

    // Vec2f target = (writer.pos() == wt_box.ld ? wt_box.rd : (writer.pos() == wt_box.rd ? wt_box.ru : (writer.pos() == wt_box.ru ? wt_box.lu : wt_box.ld)));
    //writer.add_wipe_point(writer.pos()).add_wipe_point(target);

    writer.add_wipe_path(outer_wall, m_filpar[m_current_tool].wipe_dist);
    writer.append(";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Wipe_Tower_End) + "\n");

    // Ask our writer about how much material was consumed.
    // Skip this in case the layer is sparse and config option to not print sparse layers is enabled.
    if (!m_sparse_layers_skipped || toolchanges_on_layer)
        if (m_current_tool < m_used_filament_length.size()) m_used_filament_length[m_current_tool] += writer.get_and_reset_used_filament_length();

    return construct_tcr(writer, false, old_tool, true, false, 0.f, false);
}

Polygon WipeTower::generate_rib_polygon(const box_coordinates &wt_box)
{
    auto    get_current_layer_rib_len = [](float cur_height, float max_height, float max_len) -> float { return std::abs(max_height - cur_height) / max_height * max_len; };
    coord_t diagonal_width            = scaled(m_rib_width)/2;
    float   a = this->m_wipe_tower_width, b = this->m_wipe_tower_depth;
    Line    line_1(Point::new_scale(Vec2f{0, 0}), Point::new_scale(Vec2f{a, b}));
    Line    line_2(Point::new_scale(Vec2f{a, 0}), Point::new_scale(Vec2f{0, b}));
    float   diagonal_extra_length = std::max(0.f, m_rib_length - (float) unscaled(line_1.length())) / 2.f;
    diagonal_extra_length         = scaled(get_current_layer_rib_len(this->m_z_pos, this->m_wipe_tower_height, diagonal_extra_length));
    Point y_shift{0, scaled(this->m_y_shift)};

    line_1.extend(double(diagonal_extra_length));
    line_2.extend(double(diagonal_extra_length));
    line_1.translate(-y_shift);
    line_2.translate(-y_shift);

    Polygon poly_1 = generate_rectange(line_1, diagonal_width);
    Polygon poly_2 = generate_rectange(line_2, diagonal_width);
    Polygon poly;
    poly.points.push_back(Point::new_scale(wt_box.ld));
    poly.points.push_back(Point::new_scale(wt_box.rd));
    poly.points.push_back(Point::new_scale(wt_box.ru));
    poly.points.push_back(Point::new_scale(wt_box.lu));

    Polygons           p_1_2    = union_({poly_1, poly_2, poly});
    // A zero-area box or collapsed rib leaves Clipper with nothing. front() on that
    // empty result is a null polygon; keep the tower rectangle instead.
    if (p_1_2.empty())
        return poly;
    return std::move(p_1_2.front());
};

Polygon WipeTower::generate_support_wall_new(WipeTowerWriter &writer, const box_coordinates &wt_box, double feedrate, bool first_layer,bool rib_wall, bool extrude_perimeter, bool skip_points)
{
    auto get_closet_idx = [this, &writer](Polylines &pls) -> std::pair<int,int> {
        Vec2f anchor{writer.x(), writer.y()};
        int   closestIndex = -1;
        int   closestPl = -1;
        float minDistance  = std::numeric_limits<float>::max();
        for (int i = 0; i < pls.size(); ++i) {
            for (int j = 0; j < pls[i].size(); ++j) {
                float distance = (unscaled<float>(pls[i][j]) - anchor).squaredNorm();
                if (distance < minDistance) {
                    minDistance  = distance;
                    closestPl    = i;
                    closestIndex = j;
                }
            }
        }
        return {closestPl, closestIndex};
    };

    float retract_length = m_filpar[m_current_tool].retract_length;
    float retract_speed  = m_filpar[m_current_tool].retract_speed * 60;
    Polygon wall_polygon   = rib_wall ? generate_rib_polygon(wt_box) : generate_rectange_polygon(wt_box.ld, wt_box.ru);
    Polylines result_wall;
    Polygon   insert_skip_polygon;
    if (m_used_fillet) {
        if (!rib_wall && m_y_shift > EPSILON)// do nothing because the fillet will cause it to be suspended.
        {
        } else {
            wall_polygon           = rib_wall ? rounding_polygon(wall_polygon) : wall_polygon; // rectangle_wall do nothing
            Polygon wt_box_polygon = generate_rectange_polygon(wt_box.ld, wt_box.ru);
            // Fillet can open a gap under the tower body, so merge it back with the rectangle.
            // union_() drops degenerate contours; front() on an empty result crashes in Polygon::operator=.
            Polygons united = union_({wall_polygon, wt_box_polygon});
            if (!united.empty())
                wall_polygon = std::move(united.front());
            else if (wall_polygon.empty())
                wall_polygon = std::move(wt_box_polygon);
        }
    }
    if (!extrude_perimeter) return wall_polygon;

    if (skip_points) {
        result_wall = contrust_gap_for_skip_points(wall_polygon, m_wall_skip_points[m_cur_layer_id], m_wipe_tower_width, 2.5 * m_perimeter_width, insert_skip_polygon);
    }
    else {
        result_wall.push_back(to_polyline(wall_polygon));
        insert_skip_polygon = wall_polygon;
    }
    writer.generate_path(result_wall, feedrate, retract_length, retract_speed,m_used_fillet);
    if (first_layer) {
        BoundingBox bbox = get_extents(result_wall);
        m_rib_offset     = Vec2f(-unscaled<float>(bbox.min.x()), -unscaled<float>(bbox.min.y()));
    }
    return insert_skip_polygon;
}

Polygon WipeTower::generate_support_wall(WipeTowerWriter &writer, const box_coordinates &wt_box, double feedrate, bool first_layer)
{
    float retract_length = m_filpar[m_current_tool].retract_length;
    float retract_speed  = m_filpar[m_current_tool].retract_speed *60 ;
    bool is_left  = false;
    bool is_right = false;
    for (auto pt : m_wall_skip_points[m_cur_layer_id]) {
        if (abs(pt.x()) < EPSILON) {
            is_left = true;
        } else if (abs(pt.x() - m_wipe_tower_width) < EPSILON) {
            is_right = true;
        }
    }

    if (is_left && is_right) {
        Vec2f *p = nullptr;
        p->x();
    }

    if (!is_left && !is_right) {
        Vec2f *p = nullptr;
        p->x();
    }

    //  3 -------------  2
    //    |           |
    //    |           |
    //  0 -------------  1

    int   index   = 0;
    Vec2f cur_pos = writer.pos();
    if (abs(cur_pos.x() - wt_box.ld.x()) > abs(cur_pos.x() - wt_box.rd.x())) {
        if (abs(cur_pos.y() - wt_box.ld.y()) > abs(cur_pos.y() - wt_box.lu.y())) {
            index = 2;
        } else {
            index = 1;
        }
    } else {
        if (abs(cur_pos.y() - wt_box.ld.y()) > abs(cur_pos.y() - wt_box.lu.y())) {
            index = 3;
        } else {
            index = 0;
        }
    }

    std::vector<Vec2f> points;
    points.emplace_back(wt_box.ld);
    points.emplace_back(wt_box.rd);
    points.emplace_back(wt_box.ru);
    points.emplace_back(wt_box.lu);

    writer.travel(points[index]);
    int extruded_nums = 0;
    while (extruded_nums < 4) {
        index = (index + 1) % 4;
        if (index == 2) {
            if (is_right) {
                std::vector<Segment> break_segments = remove_points_from_segment(Segment(wt_box.rd, wt_box.ru), m_wall_skip_points[m_cur_layer_id], 2.5 * m_perimeter_width);
                for (auto iter = break_segments.begin(); iter != break_segments.end(); ++iter) {
                    float dx  = iter->start.x() - writer.pos().x();
                    float dy  = iter->start.y() - writer.pos().y();
                    float len = std::sqrt(dx * dx + dy * dy);
                    if (len > 0) {
                        writer.retract(retract_length, retract_speed);
                        writer.travel(iter->start, 600.);
                        writer.retract(-retract_length, retract_speed);
                    } else
                        writer.travel(iter->start, 600.);

                    writer.extrude(iter->end, feedrate);
                }
                writer.travel(wt_box.ru, feedrate);
            } else {
                writer.extrude(wt_box.ru, feedrate);
            }
        } else if (index == 0) {
            if (is_left) {
                std::vector<Segment> break_segments = remove_points_from_segment(Segment(wt_box.ld, wt_box.lu), m_wall_skip_points[m_cur_layer_id], 2.5 * m_perimeter_width);
                for (auto iter = break_segments.rbegin(); iter != break_segments.rend(); ++iter) {
                    float dx  = iter->end.x() - writer.pos().x();
                    float dy  = iter->end.y() - writer.pos().y();
                    float len = std::sqrt(dx * dx + dy * dy);
                    if (len > 0) {
                        writer.retract(retract_length, retract_speed);
                        writer.travel(iter->end, 600.);
                        writer.retract(-retract_length, retract_speed);
                    } else
                        writer.travel(iter->end, 600.);

                    writer.extrude(iter->start, feedrate);
                }
                writer.travel(wt_box.ld, feedrate);
            } else {
                writer.extrude(wt_box.ld, feedrate);
            }
        } else {
            writer.extrude(points[index], feedrate);
        }
        extruded_nums++;
    }

    return Polygon();
}


bool WipeTower::get_floating_area(float &start_pos_y, float &end_pos_y) const {
    if (!m_cur_block)
        return false;

    // Without merged slabs, preserve the original per-layer support boundary. The immediately preceding
    // layer may contain only a sparse grid, and its actual finish depth is what makes the overhanging part
    // of the current wipe lines require 0.2 mm equivalent bridge flow.
    if (!m_merged_slabs_active) {
        if (m_layer_info == m_plan.begin() || (m_layer_info - 1) == m_plan.begin())
            return false;
        end_pos_y   = m_cur_block->start_depth + m_cur_block->depth - m_perimeter_width;
        start_pos_y = m_cur_block->finish_depth[m_cur_layer_id - 1];
        return true;
    }

    // Support comes from the last layer on which this band itself deposited material. Planned layers that
    // the band reserved as a trench, or that the whole tower skipped, do not move its supporting surface.
    int below = -1;
    for (int layer_id = int(m_cur_layer_id) - 1; layer_id >= 0; --layer_id) {
        if (layer_is_skipped(layer_id))
            continue;
        const bool purge = layer_id < int(m_block_slab_height.size()) &&
                           m_block_slab_height[layer_id].count(m_cur_block->block_key) != 0;
        const bool fill = layer_id < int(m_cur_block->layer_reserved.size()) &&
                          !m_cur_block->layer_reserved[layer_id] &&
                          band_fill_depth(*m_cur_block, layer_id) > 2.f * m_perimeter_width;
        if (purge || fill) {
            below = layer_id;
            break;
        }
    }
    if (below < 0)
        return false;

    const float layer_depth = band_fill_depth(*m_cur_block, m_cur_layer_id);
    end_pos_y = m_cur_block->start_depth + layer_depth - m_perimeter_width;
    start_pos_y = m_cur_block->finish_depth[below];

#if 0
    float last_layer_fill_box_y = (m_layer_info - 1)->toolchanges_depth() + m_perimeter_width;
    float last_layer_wipe_depth = (m_layer_info - 1)->depth;
    if (last_layer_wipe_depth - last_layer_fill_box_y <= 2 * m_perimeter_width)
        return false;

    start_pos_y = last_layer_fill_box_y + m_perimeter_width;
    end_pos_y   = last_layer_wipe_depth - m_perimeter_width;
#endif
    return true;
}

bool WipeTower::need_thick_bridge_flow(float pos_y, float slab_height) const {
    // 0.2 mm bridge flow over a floating band is the same large-nozzle compensation as the
    // solid-fill bump.
    if (slab_height >= 0.2f)
        return false;

    float y_min = 0., y_max = 0.;
    if (get_floating_area(y_min, y_max)) {
        return pos_y > y_min && pos_y < y_max;
    }
    return false;
}

bool WipeTower::is_valid_last_layer(int tool, int layer_id, double layer_z) const
{
    int extruder_id = get_extruder_id(tool, layer_id);
    if (extruder_id < 0 || extruder_id >= m_printable_height.size()) return true;
    if (m_last_layer_id[extruder_id] == layer_id && layer_z > m_printable_height[extruder_id]) return false;
    return true;
}
float WipeTower::get_block_gap_width(int tool,bool is_nozzlechangle)
{
    //assert(m_block_infill_gap_width.count(get_gap_key(tool)));//The code contains logic that attempts to access non-existent blocks,
                                                                     // such as in case of involving two extruders with only a single head and a single layer,
                                                                     // some code will attempt to access the block's nozzle_change_gap_width, even though the block does not exist.
    const int gap_key = get_gap_key(tool);
    if (!m_block_infill_gap_width.count(gap_key)) {
        return is_nozzlechangle ? filament_nozzle_change_width(tool) : filament_perimeter_width(tool);
    }
    return is_nozzlechangle ? m_block_infill_gap_width[gap_key].second : m_block_infill_gap_width[gap_key].first;

}

bool WipeTower::is_need_ramming(int filament_id_1, int filament_id_2, int layer_id) const
{
    return !m_multi_nozzle_group_result->are_filaments_same_nozzle(filament_id_1, filament_id_2, layer_id);
}
bool WipeTower::is_same_extruder(int filament_id_1, int filament_id_2, int layer_id) const
{
    return m_multi_nozzle_group_result->are_filaments_same_extruder(filament_id_1, filament_id_2, layer_id);
}

bool WipeTower::is_same_nozzle(int filament_id_1, int filament_id_2, int layer_id) const
{
    return m_multi_nozzle_group_result->are_filaments_same_nozzle(filament_id_1, filament_id_2, layer_id);
}

int WipeTower::get_nozzle_id(int filament_id, int layer_id) const { return m_multi_nozzle_group_result->get_nozzle_id(filament_id, layer_id); }

int WipeTower::get_extruder_id(int filament_id, int layer_id) const {
    return m_multi_nozzle_group_result->get_extruder_id(filament_id, layer_id);
}

} // namespace Slic3r
