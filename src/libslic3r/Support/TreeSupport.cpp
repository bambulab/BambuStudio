#include <chrono>
#include <math.h>

#include "format.hpp"
#include "BuildVolume.hpp"
#include "ClipperUtils.hpp"
#include "Clipper2Utils.hpp"
#include "Fill/FillBase.hpp"
#include "I18N.hpp"
#include "Layer.hpp"
#include "MinimumSpanningTree.hpp"
#include "Print.hpp"
#include "ShortestPath.hpp"
#include "SupportCommon.hpp"
#include "SVG.hpp"
#include "Time.hpp"
#include "TreeSupportCommon.hpp"
#include "TreeSupport.hpp"
#include "TreeSupport3D.hpp"
#include <libnest2d/backends/libslic3r/geometries.hpp>
#include <libnest2d/placers/nfpplacer.hpp>

#include <tbb/blocked_range.h>
#include <tbb/concurrent_unordered_set.h>
#include <tbb/concurrent_vector.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_for_each.h>

#include "libslic3r/Log.hpp"

#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif
#ifndef SIGN
#define SIGN(x) (x>=0?1:-1)
#endif
#define TAU (2.0 * M_PI)
#define NO_INDEX (std::numeric_limits<unsigned int>::max())
#define USE_SUPPORT_3D 0
#ifndef USE_TREE_SUPPORT_CARRIED_MASS
#define USE_TREE_SUPPORT_CARRIED_MASS 1
#endif

//#define SUPPORT_TREE_DEBUG_TO_SVG

#ifdef SUPPORT_TREE_DEBUG_TO_SVG
#include "nlohmann/json.hpp"
#endif
namespace Slic3r
{
extern bool compSecondMoment(const ExPolygons &expolys, double &smExpolysX, double &smExpolysY); // Brim.cpp
#define unscale_(val) ((val) * SCALING_FACTOR)

extern void generate_tree_support_3D(PrintObject& print_object, TreeSupport* tree_support, std::function<void()> throw_on_cancel);

inline double dot_with_unscale(const Point a, const Point b)
{
    return unscale_(a(0)) * unscale_(b(0)) + unscale_(a(1)) * unscale_(b(1));
}

inline double vsize2_with_unscale(const Point pt)
{
    return dot_with_unscale(pt, pt);
}

inline Point normal(Point pt, double scale)
{
    double length = scale_(sqrt(vsize2_with_unscale(pt)));

    return pt * (scale / length);
}

enum TreeSupportStage {
    STAGE_DETECT_OVERHANGS,
    STAGE_GENERATE_CONTACT_NODES,
    STAGE_DROP_DOWN_NODES,
    STAGE_DRAW_CIRCLES,
    STAGE_GENERATE_TOOLPATHS,
    STAGE_MinimumSpanningTree,
    STAGE_GET_AVOIDANCE,
    STAGE_projection_onto_ex,
    STAGE_get_collision,
    STAGE_intersection_ln,
    STAGE_total,
    NUM_STAGES
};

class TreeSupportProfiler
{
public:
    uint32_t stage_durations[NUM_STAGES] = { 0 };
    uint32_t stage_index = 0;
    boost::posix_time::ptime tic_time;
    boost::posix_time::ptime toc_time;

    TreeSupportProfiler()
    {
        for (uint32_t& item : stage_durations) {
            item = 0;
        }
    }

    void stage_start(TreeSupportStage stage)
    {
        if (stage > NUM_STAGES)
            return;

        m_stage_start_times[stage] = boost::posix_time::microsec_clock::local_time();
    }

    void stage_finish(TreeSupportStage stage)
    {
        if (stage > NUM_STAGES)
            return;

        boost::posix_time::ptime time = boost::posix_time::microsec_clock::local_time();
        stage_durations[stage] = (time - m_stage_start_times[stage]).total_milliseconds();
    }

    void tic() { tic_time = boost::posix_time::microsec_clock::local_time(); }
    uint32_t toc() {
        toc_time = boost::posix_time::microsec_clock::local_time();
        return (toc_time - tic_time).total_milliseconds();
    }
    void stage_add(TreeSupportStage stage)
    {
        if (stage > NUM_STAGES)
            return;
        stage_durations[stage] += toc();
    }

    std::string report()
    {
        std::stringstream ss;
        ss << "total overhange cost: " << stage_durations[STAGE_total]
            << "; STAGE_DETECT_OVERHANGS: " << stage_durations[STAGE_DETECT_OVERHANGS]
            << "; STAGE_GENERATE_CONTACT_NODES: " << stage_durations[STAGE_GENERATE_CONTACT_NODES]
            << "; STAGE_DROP_DOWN_NODES: " << stage_durations[STAGE_DROP_DOWN_NODES]
            << "; STAGE_DRAW_CIRCLES: " << stage_durations[STAGE_DRAW_CIRCLES]
            << "; STAGE_GENERATE_TOOLPATHS: " << stage_durations[STAGE_GENERATE_TOOLPATHS]
            << "; STAGE_MinimumSpanningTree: " << stage_durations[STAGE_MinimumSpanningTree]
            << "; STAGE_GET_AVOIDANCE: " << stage_durations[STAGE_GET_AVOIDANCE]
            << "; STAGE_projection_onto_ex: " << stage_durations[STAGE_projection_onto_ex]
            << "; STAGE_get_collision: " << stage_durations[STAGE_get_collision]
            << "; STAGE_intersection_ln: " << stage_durations[STAGE_intersection_ln];

        return ss.str();
    }
private:
    boost::posix_time::ptime m_stage_start_times[NUM_STAGES];
};
TreeSupportProfiler profiler;

Lines spanning_tree_to_lines(const std::vector<MinimumSpanningTree>& spanning_trees)
{
    Lines polylines;
    for (const MinimumSpanningTree& mst : spanning_trees) {
        std::vector<Point> points = mst.vertices();
        std::unordered_set<Point, PointHash> to_ignore;
        for (Point pt1 : points) {
            if (to_ignore.find(pt1) != to_ignore.end())
                continue;

            const std::vector<Point>& neighbours = mst.adjacent_nodes(pt1);
            if (neighbours.empty())
                continue;

            for (Point pt2 : neighbours) {
                if (to_ignore.find(pt2) != to_ignore.end())
                    continue;

                Line line(pt1, pt2);
                polylines.push_back(line);
            }

            to_ignore.insert(pt1);
        }
    }
    return polylines;
}


#ifdef SUPPORT_TREE_DEBUG_TO_SVG
static void draw_contours_and_nodes_to_svg
(
    std::string fname,
    const ExPolygons &overhangs,
    const ExPolygons &overhangs_after_offset,
    const ExPolygons &outlines_below,
    const std::vector<SupportNode*>& layer_nodes,
    const std::vector<SupportNode*>& lower_layer_nodes,
    std::vector<std::string> legends = { "overhang","avoid","outlines" },
	std::vector<std::string> colors = { "blue","red","yellow" }
)
{
    BoundingBox bbox = get_extents(overhangs);
    bbox.merge(get_extents(overhangs_after_offset));
    bbox.merge(get_extents(outlines_below));
    Points layer_pts;
    for (SupportNode* node : layer_nodes) {
        layer_pts.push_back(node->position);
    }
    bbox.merge(get_extents(layer_pts));
    bbox.inflated(scale_(1));
    bbox.max.x() = std::max(bbox.max.x(), (coord_t)scale_(10));
    bbox.max.y() = std::max(bbox.max.y(), (coord_t)scale_(10));

    SVG svg;
    svg.open(fname, bbox);
    if (!svg.is_opened())        return;

    // draw grid
    svg.draw_grid(bbox, "gray", coord_t(scale_(0.05)));

    // draw overhang areas
    svg.draw_outline(overhangs, colors[0]);
    svg.draw_outline(overhangs_after_offset, colors[1]);
    svg.draw_outline(outlines_below, colors[2]);

    // draw legend
	svg.draw_text(bbox.min + Point(scale_(0), scale_(0)), format("nPoints: %1%->%2%",layer_nodes.size(), lower_layer_nodes.size()).c_str(), "green", 2);
    svg.draw_text(bbox.min + Point(scale_(0), scale_(2)), legends[0].c_str(), colors[0].c_str(), 2);
    svg.draw_text(bbox.min + Point(scale_(0), scale_(4)), legends[1].c_str(), colors[1].c_str(), 2);
    svg.draw_text(bbox.min + Point(scale_(0), scale_(6)), legends[2].c_str(), colors[2].c_str(), 2);

    // draw layer nodes
    svg.draw(layer_pts, "green", coord_t(scale_(0.1)));
    for (SupportNode *node : layer_nodes) { svg.draw({node->overhang}, "green", 0.5); }

    // lower layer points
    layer_pts.clear();
    for (SupportNode* node : lower_layer_nodes) {
        layer_pts.push_back(node->position);
    }
    svg.draw(layer_pts, "black", coord_t(scale_(0.1)));

    //// higher layer points
    //layer_pts.clear();
    //for (SupportNode* node : layer_nodes) {
    //    if(node->parent)
    //        layer_pts.push_back(node->parent->position);
    //}
    //svg.draw(layer_pts, "blue", coord_t(scale_(0.1)));
}

static void draw_layer_mst
(const std::string &fname,
    const std::vector<MinimumSpanningTree> &spanning_trees,
    const ExPolygons& outline
)
{
    auto lines = spanning_tree_to_lines(spanning_trees);
    BoundingBox bbox = get_extents(lines);
    for (auto& poly : outline)
    {
        BoundingBox bb = poly.contour.bounding_box();
        bbox.merge(bb);
    }

    SVG svg(fname, bbox);
    if (!svg.is_opened())        return;

    svg.draw(lines, "blue", coord_t(scale_(0.05)));
    svg.draw_outline(outline, "yellow");
    for (auto &spanning_tree : spanning_trees)
        svg.draw(spanning_tree.vertices(), "black", coord_t(scale_(0.1)));
}

#endif

// Move point from inside polygon if distance>0, outside if distance<0.
// Special case: distance=0 means find the nearest point of from on the polygon contour.
// The max move distance should not excceed max_move_distance.
// @return success(true) or not(false)
static bool move_inside_expoly(const ExPolygon &polygon, Point& from, double distance = 0, double max_move_distance = std::numeric_limits<double>::max())
{
    //TODO: This is copied from the moveInside of Polygons.
    /*
    We'd like to use this function as subroutine in moveInside(Polygons...), but
    then we'd need to recompute the distance of the point to the polygon, which
    is expensive. Or we need to return the distance. We need the distance there
    to compare with the distance to other polygons.
    */
    Point ret = from;
    double bestDist2 = std::numeric_limits<double>::max();
    bool is_already_on_correct_side_of_boundary = false; // whether [from] is already on the right side of the boundary
    const Polygon &contour = polygon.contour;

    if (contour.points.size() < 2)
    {
        return false;
    }
    Point p0 = contour.points[polygon.contour.size() - 2];
    Point p1 = contour.points.back();
    // because we compare with vsize2_with_unscale here (no division by zero), we also need to compare by vsize2_with_unscale inside the loop
    // to avoid integer rounding edge cases
    bool projected_p_beyond_prev_segment = dot_with_unscale(p1 - p0, from - p0) >= vsize2_with_unscale(p1 - p0);
    for(const Point& p2 : polygon.contour.points)
    {
        // X = A + Normal(B-A) * (((B-A) dot_with_unscale (P-A)) / VSize(B-A));
        //   = A +       (B-A) *  ((B-A) dot_with_unscale (P-A)) / VSize2(B-A);
        // X = P projected on AB
        const Point& a = p1;
        const Point& b = p2;
        const Point& p = from;
        Point ab = b - a;
        Point ap = p - a;
        double ab_length2 = vsize2_with_unscale(ab);
        if(ab_length2 <= 0) //A = B, i.e. the input polygon had two adjacent points on top of each other.
        {
            p1 = p2; //Skip only one of the points.
            continue;
        }
        double dot_prod = dot_with_unscale(ab, ap);
        if (dot_prod <= 0) // x is projected to before ab
        {
            if (projected_p_beyond_prev_segment)
            { //  case which looks like:   > .
                projected_p_beyond_prev_segment = false;
                Point& x = p1;

                double dist2 = vsize2_with_unscale(x - p);
                if (dist2 < bestDist2)
                {
                    bestDist2 = dist2;
                    if (distance == 0)
                    {
                        ret = x;
                    }
                    else
                    {
                        // TODO: check whether it needs scale_()
                        Point inward_dir = turn90_ccw(normal(ab, 10.0) + normal(p1 - p0, 10.0)); // inward direction irrespective of sign of [distance]
                        // MM2INT(10.0) to retain precision for the eventual normalization
                        ret = x + normal(inward_dir, scale_(distance));
                        is_already_on_correct_side_of_boundary = dot_with_unscale(inward_dir, p - x) * distance >= 0;
                    }
                }
            }
            else
            {
                projected_p_beyond_prev_segment = false;
                p0 = p1;
                p1 = p2;
                continue;
            }
        }
        else if (dot_prod >= ab_length2) // x is projected to beyond ab
        {
            projected_p_beyond_prev_segment = true;
            p0 = p1;
            p1 = p2;
            continue;
        }
        else
        { // x is projected to a point properly on the line segment (not onto a vertex). The case which looks like | .
            projected_p_beyond_prev_segment = false;
            Point x = a + ab * (dot_prod / ab_length2);

            double dist2 = vsize2_with_unscale(p - x);
            if (dist2 < bestDist2)
            {
                bestDist2 = dist2;
                if (distance == 0)
                {
                    ret = x;
                }
                else
                {
                    Point inward_dir = turn90_ccw(normal(ab, scale_(distance))); // inward or outward depending on the sign of [distance]
                    ret = x + inward_dir;
                    is_already_on_correct_side_of_boundary = dot_with_unscale(inward_dir, p - x) >= 0;
                }
            }
        }

        p0 = p1;
        p1 = p2;
    }

    if (is_already_on_correct_side_of_boundary) // when the best point is already inside and we're moving inside, or when the best point is already outside and we're moving outside
    {
        // BBS. Remove this condition.
        if (bestDist2 < distance * distance)
        {
            from = ret;
        }
        return true;
    }
    else if (bestDist2 < max_move_distance * max_move_distance)
    {
        from = ret;
        return true;
    }
    return false;
}

/*
 * Implementation assumes moving inside, but moving outside should just as well be possible.
 */
static bool move_inside_expolys(const ExPolygons& polygons, Point& from, double distance, double max_move_distance)
{
    Point from0 = from;
    Point ret = from;
    std::vector<Point> valid_pts;
    double bestDist2 = std::numeric_limits<double>::max();
    unsigned int bestPoly = NO_INDEX;
    bool is_already_on_correct_side_of_boundary = false; // whether [from] is already on the right side of the boundary
    Point inward_dir;
    for (unsigned int poly_idx = 0; poly_idx < polygons.size(); poly_idx++)
    {
        const ExPolygon poly = polygons[poly_idx];
        if (poly.contour.size() < 2)
            continue;
        Point p0 = poly.contour[poly.contour.size()-2];
        Point p1 = poly.contour.points.back();
        // because we compare with vsize2_with_unscale here (no division by zero), we also need to compare by vsize2_with_unscale inside the loop
        // to avoid integer rounding edge cases
        bool projected_p_beyond_prev_segment = dot_with_unscale(p1 - p0, from - p0) >= vsize2_with_unscale(p1 - p0);
        for(const Point p2 : poly.contour.points)
        {
            // X = A + Normal(B-A) * (((B-A) dot_with_unscale (P-A)) / VSize(B-A));
            //   = A +       (B-A) *  ((B-A) dot_with_unscale (P-A)) / VSize2(B-A);
            // X = P projected on AB
            Point a = p1;
            Point b = p2;
            Point p = from;
            Point ab = b - a;
            Point ap = p - a;
            double ab_length2 = vsize2_with_unscale(ab);
            if(ab_length2 <= 0) //A = B, i.e. the input polygon had two adjacent points on top of each other.
            {
                p1 = p2; //Skip only one of the points.
                continue;
            }
            double dot_prod = dot_with_unscale(ab, ap);
            if (dot_prod <= 0) // x is projected to before ab
            {
                if (projected_p_beyond_prev_segment)
                { //  case which looks like:   > .
                    projected_p_beyond_prev_segment = false;
                    Point& x = p1;

                    double dist2 = vsize2_with_unscale(x - p);
                    if (dist2 < bestDist2)
                    {
                        bestDist2 = dist2;
                        bestPoly = poly_idx;
                        if (distance == 0) { ret = x; }
                        else
                        {
                            inward_dir = turn90_ccw(normal(ab, 10.0) + normal(p1 - p0, 10.0)); // inward direction irrespective of sign of [distance]
                            // MM2INT(10.0) to retain precision for the eventual normalization
                            ret = x + normal(inward_dir, scale_(distance));
                            is_already_on_correct_side_of_boundary = dot_with_unscale(inward_dir, p - x) * distance >= 0;
                            if (is_already_on_correct_side_of_boundary && dist2 < distance * distance)
                                valid_pts.push_back(ret-from0);
                        }
                    }
                }
                else
                {
                    projected_p_beyond_prev_segment = false;
                    p0 = p1;
                    p1 = p2;
                    continue;
                }
            }
            else if (dot_prod >= ab_length2) // x is projected to beyond ab
            {
                projected_p_beyond_prev_segment = true;
                p0 = p1;
                p1 = p2;
                continue;
            }
            else
            { // x is projected to a point properly on the line segment (not onto a vertex). The case which looks like | .
                projected_p_beyond_prev_segment = false;
                Point x = a + ab * (dot_prod / ab_length2);

                double dist2 = vsize2_with_unscale(p - x);
                if (dist2 < bestDist2)
                {
                    bestDist2 = dist2;
                    bestPoly = poly_idx;
                    if (distance == 0) { ret = x; }
                    else
                    {
                        inward_dir = turn90_ccw(normal(ab, scale_(distance))); // inward or outward depending on the sign of [distance]
                        ret = x + inward_dir;
                        is_already_on_correct_side_of_boundary = dot_with_unscale(inward_dir, p - x) >= 0;
                        if (is_already_on_correct_side_of_boundary && dist2<distance*distance)
                            valid_pts.push_back(ret-from0);
                    }
                }
            }
            p0 = p1;
            p1 = p2;
        }
    }

    //if (valid_pts.size() > 1) {
    //    std::sort(valid_pts.begin(), valid_pts.end());
    //    Point v_combine = valid_pts[0] + valid_pts[1];
    //    if(vsize2_with_unscale(v_combine)<distance*distance)
    //        v_combine = normal(v_combine, scale_(distance));
    //    ret = v_combine + from0;
    //}

    if (is_already_on_correct_side_of_boundary) // when the best point is already inside and we're moving inside, or when the best point is already outside and we're moving outside
    {
        if (bestDist2 < distance * distance)
        {
            from = ret;
        }
        return true;
    }
    else if (bestDist2 < max_move_distance * max_move_distance)
    {
        from = ret;
        return true;
    }
    return false;
}

static Point find_closest_ex(Point from, const ExPolygons& polygons)
{
    Point closest_pt;
    double min_dist2 = std::numeric_limits<double>::max();

    for (const ExPolygon &poly : polygons) {
        for (int i = 0; i < poly.num_contours(); i++) {
            const Point* candidate = poly.contour_or_hole(i).closest_point(from);
            double dist2 = vsize2_with_unscale(*candidate - from);
            if (dist2 < min_dist2) {
                closest_pt = *candidate;
                min_dist2 = dist2;
            }
        }
    }

    return closest_pt;
}

static bool move_outside_expolys(const ExPolygons& polygons, Point& from, double distance, double max_move_distance)
{
    return move_inside_expolys(polygons, from, -distance, -max_move_distance);
}

static bool is_inside_ex(const ExPolygon &polygon, const Point &pt)
{
    if (!get_extents(polygon).contains(pt))
        return false;

    return polygon.contains(pt);
}

static bool is_inside_ex(const ExPolygons &polygons, const Point &pt)
{
    for (const ExPolygon &poly : polygons) {
        if (is_inside_ex(poly, pt))
            return true;
    }

    return false;
}

// use project_onto which is more accurate but more expensive
static bool move_out_expolys(const ExPolygons& polygons, Point& from, double distance, double max_move_distance)
{
    Point from0 = from;
    ExPolygons polys_dilated = union_ex(offset_ex(polygons, scale_(distance)));
    Point pt = projection_onto(polys_dilated, from);// find_closest_ex(from, polys_dilated);
    Point outward_dir = pt - from;
    Point pt_max = from + normal(outward_dir, scale_(max_move_distance));
    double dist2 = vsize2_with_unscale(outward_dir);
    if (dist2 > SQ(max_move_distance))
        pt = pt_max;
    // case 5: already outside and far enough, no need to move
    if (!is_inside_ex(polys_dilated, from))
        return true;
    else if (!is_inside_ex(polygons, from)) {
        // case 4: already outside but not far enough
        from = pt;
        return true;
    }
    else {
        bool pt_max_in_poly = is_inside_ex(polygons, pt_max);
        if (!pt_max_in_poly) {
            from = pt_max;
            return true;
        }
        else {
            return false;
        }
    }
}

static Point bounding_box_middle(const BoundingBox &bbox)
{
    return (bbox.max + bbox.min) / 2;
}

TreeSupport::TreeSupport(PrintObject& object, const SlicingParameters &slicing_params)
    : m_object(&object), m_slicing_params(slicing_params), m_support_params(object), m_object_config(&object.config())
{
    m_print_config = &m_object->print()->config();
    m_raft_layers = slicing_params.base_raft_layers + slicing_params.interface_raft_layers;
    support_type = m_object_config->support_type;

    SupportMaterialPattern support_pattern  = m_object_config->support_base_pattern;
    if (m_support_params.support_style == smsTreeHybrid && support_pattern == smpDefault)
        support_pattern = smpRectilinear;//smpLightning;

    if(support_pattern == smpLightning)
        m_support_params.base_fill_pattern = ipLightning;
    diameter_angle_scale_factor              = std::clamp<double>(m_object_config->tree_support_branch_diameter_angle * M_PI / 180., 0., 0.5 * M_PI - EPSILON);
    is_slim                                  = is_tree_slim(support_type, m_support_params.support_style);
    is_strong = is_tree(support_type) && m_support_params.support_style == smsTreeStrong;
    const coordf_t support_extrusion_width = Flow::extrusion_width_for_nozzle(
        m_object_config->line_width,
        float(primary_nozzle_diameter(m_object->print()->full_print_config())),
        support_nozzle_diameter(m_object, m_object_config->support_filament),
        multi_nozzle_diameter_mode_enabled(m_object->print()->full_print_config()));
    // A branch may not start thinner than a single extrusion, or draw_circles() is handed a circle it
    // cannot extrude, and it may not start thicker than the branch it grows into.
    const coordf_t tip_diameter_max          = std::max<coordf_t>(support_extrusion_width, m_object_config->tree_support_branch_diameter.value);
    tip_radius                               = std::clamp<coordf_t>(m_object_config->tree_support_tip_diameter.value, support_extrusion_width, tip_diameter_max) / 2;
    base_radius                              = std::max(tip_radius, m_object_config->tree_support_branch_diameter.value / 2);
    // by default tree support needs no infill, unless it's tree hybrid which contains normal nodes.
    with_infill              = m_support_params.support_base_pattern != smpNone;
    m_machine_border.contour = get_bed_shape_with_excluded_area(*m_print_config);
    Vec3d plate_offset       = m_object->print()->get_plate_origin();
    // align with the centered object in current plate (may not be the 1st plate, so need to add the plate offset)
    m_machine_border.translate(Point(scale_(plate_offset(0)), scale_(plate_offset(1))) - m_object->instances().front().shift);
    m_ts_data      = m_object->alloc_tree_support_preview_cache();
    top_z_distance                            = m_object_config->support_top_z_distance.value;
    if (top_z_distance > EPSILON)
        top_z_distance = std::max(top_z_distance, float(m_slicing_params.min_layer_height));
#if USE_TREESUPPRT3D
    m_support_params.independent_layer_height = false; // do not use independent layer height for 3D tree support
#endif
    init_top_z_by_nozzle();
#ifdef SUPPORT_TREE_DEBUG_TO_SVG
    SVG svg(debug_out_path("machine_boarder.svg"), m_object->bounding_box());
    if (svg.is_opened()) svg.draw(m_machine_border, "yellow");
#endif
    BOOST_LOG_TRIVIAL(debug) << "tree support construct finish";
}

void TreeSupport::init_top_z_by_nozzle()
{
    m_top_z_outlines.clear();
    m_top_z_max = top_z_distance;
    // The contact nodes are pre-split on the object layer grid, see generate_contact_points(), so independent
    // support layers keep the global gap. A zero gap is the soluble interface contract and is never replaced.
    if (top_z_distance < EPSILON || m_support_params.independent_layer_height ||
        ! m_object->print()->uses_multiple_nozzle_diameters())
        return;

    const PrintConfig &print_config = m_object->print()->config();
    const float        min_gap      = float(m_slicing_params.min_layer_height);
    const coord_t      resolution   = scale_(m_ts_data->m_radius_sample_resolution);
    // An overhang's underside is printed by the walls and the bridge infill, take the larger gap of the two.
    auto region_gap = [&print_config, min_gap](const PrintRegionConfig &config) {
        const double nozzle = std::max(nozzle_diameter_for_filament(print_config, config.wall_filament.value),
                                       nozzle_diameter_for_filament(print_config, config.solid_infill_filament.value));
        return std::max(support_top_z_for_nozzle(nozzle), min_gap);
    };

    m_top_z_outlines.resize(m_object->layer_count());
    tbb::parallel_for(tbb::blocked_range<size_t>(0, m_object->layer_count()), [&](const tbb::blocked_range<size_t> &range) {
        for (size_t layer_nr = range.begin(); layer_nr < range.end(); ++ layer_nr) {
            std::vector<std::pair<float, ExPolygons>> &buckets = m_top_z_outlines[layer_nr];
            auto bucket_of = [&buckets](float gap) -> ExPolygons & {
                auto it = std::find_if(buckets.begin(), buckets.end(),
                                       [gap](const std::pair<float, ExPolygons> &bucket) { return std::abs(bucket.first - gap) < EPSILON; });
                if (it == buckets.end())
                    return buckets.emplace_back(gap, ExPolygons{}).second;
                return it->second;
            };
            // Same footprint as TreeSupportData::m_layer_outlines: the region slices plus the merged band
            // material that sits on this layer without being part of its slices. Union before simplifying, so
            // neighbouring surfaces of one region cannot open cracks between them.
            for (const LayerRegion *layerm : m_object->get_layer(layer_nr)->regions()) {
                ExPolygons region_area = to_expolygons(layerm->slices.surfaces);
                for (const AdaptiveMergePatch &patch : layerm->adaptive_merge_patches)
                    append(region_area, patch.area());
                if (region_area.empty())
                    continue;
                ExPolygons &bucket = bucket_of(region_gap(layerm->region().config()));
                for (const ExPolygon &poly : union_ex(region_area))
                    append(bucket, union_ex_2(poly.simplify_p(resolution)));
            }
            for (std::pair<float, ExPolygons> &bucket : buckets)
                bucket.second = union_ex(bucket.second);
            buckets.erase(std::remove_if(buckets.begin(), buckets.end(),
                                         [](const std::pair<float, ExPolygons> &bucket) { return bucket.second.empty(); }),
                          buckets.end());
            std::sort(buckets.begin(), buckets.end(),
                      [](const std::pair<float, ExPolygons> &a, const std::pair<float, ExPolygons> &b) { return a.first < b.first; });
        }
    });

    float max_gap = 0.f;
    for (const std::vector<std::pair<float, ExPolygons>> &buckets : m_top_z_outlines)
        if (! buckets.empty())
            max_gap = std::max(max_gap, buckets.back().first);
    if (max_gap <= 0.f)
        m_top_z_outlines.clear();
    else
        m_top_z_max = max_gap;
}

float TreeSupport::top_z_at(size_t obj_layer_nr) const
{
    if (m_top_z_outlines.empty())
        return top_z_distance;
    if (obj_layer_nr >= m_top_z_outlines.size() || m_top_z_outlines[obj_layer_nr].empty())
        return m_top_z_max;
    return m_top_z_outlines[obj_layer_nr].back().first;
}

float TreeSupport::top_z_for_overhang(size_t obj_layer_nr, const ExPolygon &overhang) const
{
    if (m_top_z_outlines.empty() || obj_layer_nr >= m_top_z_outlines.size() || m_top_z_outlines[obj_layer_nr].size() < 2)
        return top_z_at(obj_layer_nr);
    // The outlines are simplified, so a neighbouring region's may poke a sliver into the overhang. Only an
    // overlap wider than that error counts; a mere touch is the fallback when nothing wider is found.
    const float sliver  = float(scale_(m_ts_data->m_radius_sample_resolution));
    float       gap     = 0.f;
    float       touched = 0.f;
    for (const std::pair<float, ExPolygons> &bucket : m_top_z_outlines[obj_layer_nr]) {
        const ExPolygons common = intersection_ex(bucket.second, overhang);
        if (common.empty())
            continue;
        touched = std::max(touched, bucket.first);
        if (! opening_ex(common, sliver).empty())
            gap = std::max(gap, bucket.first);
    }
    return gap > 0.f ? gap : touched > 0.f ? touched : top_z_at(obj_layer_nr);
}

void TreeSupport::union_outlines_within_top_z(ExPolygons &collision, size_t obj_layer_nr, bool clamp_to_first_layer_gap) const
{
    if (top_z_distance <= EPSILON)
        return;
    // The object first layer is already offset by support_object_first_layer_gap. Outlines above still
    // have to be cleared, but expanding them by the full top-z gap cancels a smaller first-layer gap on a
    // vertical wall (the upper outline matches this one). Clamp only the XY expansion; the Z window stays
    // the top-z gap so a real overhang within that height is still kept out.
    const float first_layer_gap = clamp_to_first_layer_gap ? float(m_object_config->support_object_first_layer_gap.value) : 0.f;
    auto xy_expand = [&](float gap) {
        if (clamp_to_first_layer_gap)
            gap = std::min(gap, first_layer_gap);
        return scale_(gap);
    };
    ExPolygons above;
    float      accum_height = 0;
    for (size_t layer_id = obj_layer_nr + 1; layer_id < m_ts_data->m_layer_outlines.size(); ++ layer_id) {
        accum_height += m_object->get_layer(layer_id)->height;
        if (accum_height > m_top_z_max)
            break;
        if (m_top_z_outlines.empty())
            append(above, offset_ex(m_ts_data->m_layer_outlines[layer_id], xy_expand(top_z_distance)));
        else
            // A fine nozzle's own overhang right above its thin gap must not be pulled down onto its interface
            // because a coarse nozzle elsewhere asks for a wider gap.
            for (const std::pair<float, ExPolygons> &bucket : m_top_z_outlines[layer_id])
                if (accum_height <= bucket.first)
                    append(above, offset_ex(bucket.second, xy_expand(bucket.first)));
    }
    if (! above.empty()) {
        append(collision, std::move(above));
        collision = union_ex(collision);
    }
}

void add_overhang(Layer *layer, const ExPolygon &overhang, int type)
{
    layer->loverhangs_with_type.emplace_back(overhang, type);
    layer->loverhangs.emplace_back(overhang);
}

bool is_stable(float height, const ExPolygon &overhang, float strength_z)
{
    float stability = 1. * height;
    double Ixx = 0, Iyy = 0;
    auto   props = compSecondMoment({overhang}, Ixx, Iyy);
    // stability += 0.5 * props.centroid.norm() * SCALING_FACTOR;
    double moment = std::min(Ixx * pow(SCALING_FACTOR, 4), Iyy * pow(SCALING_FACTOR, 4));
    return moment / stability > 3.;
}

#define SUPPORT_SURFACES_OFFSET_PARAMETERS ClipperLib::jtSquare, 0.
void TreeSupport::detect_overhangs(bool check_support_necessity/* = false*/)
{
    bool tree_support_enable = m_object_config->enable_support.value && is_tree(m_object_config->support_type.value);
    if (!tree_support_enable && !check_support_necessity) {
        BOOST_LOG_TRIVIAL(info) << "Tree support is disabled.";
        return;
    }

    // Clear and create Tree Support Layers
    m_object->clear_support_layers();
    m_object->clear_tree_support_preview_cache();

    const PrintObjectConfig& config = m_object->config();
    SupportType stype = support_type;
    const coordf_t radius_sample_resolution = g_config_tree_support_collision_resolution;
    const coordf_t extrusion_width = Flow::extrusion_width_for_nozzle(
        config.line_width,
        float(primary_nozzle_diameter(m_object->print()->full_print_config())),
        support_nozzle_diameter(m_object, config.support_filament),
        multi_nozzle_diameter_mode_enabled(m_object->print()->full_print_config()));
    const coordf_t extrusion_width_scaled = scale_(extrusion_width);
    const coordf_t max_bridge_length = scale_(config.max_bridge_length.value);
    const bool bridge_no_support = max_bridge_length > 0;
    const bool support_critical_regions_only = config.support_critical_regions_only.value;
    bool config_remove_small_overhangs = config.support_remove_small_overhang.value;
    bool config_detect_sharp_tails = g_config_support_sharp_tails;
    const int enforce_support_layers = config.enforce_support_layers.value;
    const double area_thresh_well_supported = SQ(scale_(6));
    const double length_thresh_well_supported = scale_(6);
    const double length_thresh_small_overhang = scale_(2);
    const double radius_thresh_small_overhang = 2.5*extrusion_width_scaled;
    static const double sharp_tail_max_support_height = 16.f;
    // a region is considered well supported if the number of layers below it exceeds this threshold
    const int thresh_layers_below = 10 / config.layer_height;
    // +1 makes the threshold inclusive
    double thresh_angle = config.support_threshold_angle.value > EPSILON ? config.support_threshold_angle.value + 1 : 30;
    thresh_angle = std::min(thresh_angle, 89.); // should be smaller than 90
    const double threshold_rad = Geometry::deg2rad(thresh_angle);
    // Dilate painted enforcers by the effective tip, not the raw setting: tip_radius is already clamped
    // to [support extrusion width, branch diameter], so a large tree_support_tip_diameter cannot blow this
    // fudge offset up to 10mm. Enforcers are unioned back in after blockers are subtracted below, so an
    // unclamped offset here would let a thick tip swallow nearby blockers.
    auto   enforcer_overhang_offset  = scaled<double>(tip_radius * 2);

    // for small overhang removal
    struct OverhangCluster {
        std::map<int, ExPolygons> layer_overhangs;
        ExPolygons merged_poly;
        BoundingBox merged_bbox;
        int min_layer = 1e7;
        int max_layer = 0;
        coordf_t offset = 0;

        OverhangType type = Normal;
        const char  *small_reason = nullptr;
        bool         is_cantilever() { return type & Cantilever; }
        bool         is_sharp_tail() { return type & SharpTail; }
        bool         is_type(OverhangType type_mask) { return type & type_mask; }
        void         set_type(OverhangType type_mask, bool is)
        {
            if (is)
                type = OverhangType(type | type_mask);
            else
                type = OverhangType(type & (~type_mask));
        }
        OverhangCluster(const ExPolygon* expoly, int layer_nr) {
            push_back(expoly, layer_nr);
        }
        void push_back(const ExPolygon* expoly, int layer_nr) {
            auto it = layer_overhangs.find(layer_nr);
            if (it == layer_overhangs.end())
                layer_overhangs.emplace(layer_nr, ExPolygons{*expoly});
            else
                layer_overhangs[layer_nr].emplace_back(*expoly);
            auto dilate1 = offset_ex(*expoly, offset);
            if (!dilate1.empty())
                merged_poly = union_ex(merged_poly, dilate1);
            min_layer = std::min(min_layer, layer_nr);
            max_layer = std::max(max_layer, layer_nr);
            merged_bbox.merge(get_extents(dilate1));
        }
        int height() {
            return max_layer - min_layer + 1;
        }
        bool intersects(const ExPolygon& region, int layer_nr, coordf_t offset) {
            if (layer_nr < 1) return false;
            auto it = layer_overhangs.find(layer_nr - 1);
            if (it == layer_overhangs.end()) return false;
            const ExPolygons overhangs = it->second;

            this->offset = offset;
            auto dilate1 = offset_ex(region, offset);
            BoundingBox bbox = get_extents(dilate1);
            if (!merged_bbox.overlap(bbox))
                return false;
            return overlaps(overhangs, dilate1);
        }
        // it's basically the combination of push_back and intersects, but saves an offset_ex
        bool push_back_if_intersects(const ExPolygon& region, int layer_nr, coordf_t offset, size_t max_layer_gap) {
            bool is_intersect = false;
            this->offset      = offset;
            ExPolygons dilate1;
            BoundingBox bbox;
            do {
                if (layer_nr < 1) break;
                auto it = layer_overhangs.lower_bound(layer_nr);
                if (it == layer_overhangs.begin()) break;
                -- it;
                if (size_t(layer_nr - it->first) > max_layer_gap) break;
                const ExPolygons overhangs = it->second;

                dilate1 = offset_ex(region, offset);
                if (dilate1.empty()) break;
                bbox = get_extents(dilate1);
                if (!merged_bbox.overlap(bbox))
                    break;
                is_intersect = overlaps(overhangs, dilate1);
            } while (0);
            if (is_intersect) {
                auto it = layer_overhangs.find(layer_nr);
                if (it == layer_overhangs.end())
                    layer_overhangs.emplace(layer_nr, ExPolygons{region});
                else
                    layer_overhangs[layer_nr].emplace_back(region);
                merged_poly = union_ex(merged_poly, dilate1);
                min_layer = std::min(min_layer, layer_nr);
                max_layer = std::max(max_layer, layer_nr);
                merged_bbox.merge(bbox);
            }
            return is_intersect;
        }
        void check_small_overhang(coordf_t extrusion_width_scaled, double length_thresh_small_overhang,
                                  double radius_thresh_small_overhang)
        {
            bool is_small_overhang = false;
            small_reason = nullptr;
            if ((type & SharpTail) || (type & Cantilever)) {
                set_type(Small, false);
                return;
            }

            bool  all_layers_are_small = true;
            float avg_width            = 0;
            float avg_area             = 0;
            int   num_layers           = 0;
            for (auto it = layer_overhangs.begin(); it != layer_overhangs.end(); it++) {
                const ExPolygons overhangs = it->second;
                ExPolygons       erodel   = offset_ex(overhangs, -extrusion_width_scaled / 2);
                coord_t          narrow_width = 0;
                for (auto &poly : erodel) {
                    Point bbox_sz = get_extents(poly).size();
                    narrow_width  = std::max(narrow_width, std::min(bbox_sz.x(), bbox_sz.y()));
                }
                if (narrow_width > extrusion_width_scaled) all_layers_are_small = false;
                avg_width += narrow_width;
                avg_area += area(overhangs);
                num_layers++;
            }
            avg_width /= num_layers;
            avg_area /= num_layers;
            is_small_overhang = all_layers_are_small && avg_width < extrusion_width_scaled / 2 && avg_area < SQ(length_thresh_small_overhang) && this->height() < 3;
            if (is_small_overhang) {
                small_reason = "per-layer";
                set_type(Small, true);
                return;
            }

            ExPolygons erodel = offset_ex(merged_poly, -radius_thresh_small_overhang);
            Point      bbox_sz = get_extents(erodel).size();
            if (erodel.empty() ||
                (bbox_sz.x() < length_thresh_small_overhang && bbox_sz.y() < length_thresh_small_overhang && this->height() < length_thresh_small_overhang / 2)) {
                is_small_overhang = true;
                small_reason = erodel.empty() ? "radius" : "bbox-height";
                set_type(Small, true);
            }
        }

        void check_polygon_node(float thresh_big_overhang, const ExPolygons &m_layer_outlines_below)
        {
            ExPolygons outlines_below_eroded = offset_ex(m_layer_outlines_below, -this->offset);
            if (area(merged_poly) > SQ(thresh_big_overhang))
                set_type(BigFlat, true);
            else if (merged_bbox.size().any_comp(thresh_big_overhang, ">") && min_layer < 20)
                if (!overlaps(merged_poly, outlines_below_eroded)) set_type(ThinPlate, true);
        }
    };
    std::vector<OverhangCluster> overhangClusters;

    // A coarse region only contributes overhangs at its band tops. Treat the swallowed fine-grid layers as
    // adjacent here, matching how consecutive coarse layers are clustered in a single-diameter print.
    size_t max_cluster_layer_gap = 1;
    for (const Layer *layer : m_object->layers())
        for (const LayerRegion *region : layer->regions())
            for (const AdaptiveMergePatch &patch : region->adaptive_merge_patches)
                if (patch.group_top)
                    max_cluster_layer_gap =
                        std::max(max_cluster_layer_gap, patch.top_idx - patch.bottom_idx + 1);

    auto find_and_insert_cluster = [max_cluster_layer_gap](
                                       auto &regionClusters, const ExPolygon &region,
                                       int layer_nr, coordf_t offset) {
        OverhangCluster *cluster = nullptr;
        for (int i = 0; i < regionClusters.size(); i++) {
            auto cluster_i = &regionClusters[i];
            if (cluster_i->push_back_if_intersects(
                    region, layer_nr, offset, max_cluster_layer_gap)) {
                cluster = cluster_i;
                break;
            }
        }
        if (!cluster) {
            cluster = &regionClusters.emplace_back(&region, layer_nr);
        }
        return cluster;
    };

    if (!is_tree(stype)) return;

    max_cantilever_dist = 0;
    m_highest_overhang_layer = 0;

    // calc the extrudable expolygons of each layer
    tbb::parallel_for(tbb::blocked_range<size_t>(0, m_object->layer_count()),
        [&](const tbb::blocked_range<size_t>& range) {
            for (size_t layer_nr = range.begin(); layer_nr < range.end(); layer_nr++) {
                if (m_object->print()->canceled())
                    break;
                Layer* layer = m_object->get_layer(layer_nr);
                // Filter out areas whose diameter that is smaller than extrusion_width, but we don't want to lose any details.
                layer->lslices_extrudable = intersection_ex(layer->lslices, offset2_ex(layer->lslices, -extrusion_width_scaled / 2, extrusion_width_scaled));
                layer->loverhangs.clear();
                layer->loverhangs_with_type.clear();
                layer->sharp_tails.clear();
                layer->sharp_tails_height.clear();
                layer->cantilevers.clear();
            }
        });

    // main part of overhang detection can be parallel
    tbb::concurrent_vector<ExPolygons> overhangs_all_layers(m_object->layer_count());

    auto enforcers = m_object->slice_support_enforcers();
    auto blockers  = m_object->slice_support_blockers();
    m_vertical_enforcer_points.clear();
    m_object->project_and_append_custom_facets(false, EnforcerBlockerType::BLOCKER, blockers);
    m_object->project_and_append_custom_facets(false, EnforcerBlockerType::ENFORCER, enforcers, &m_vertical_enforcer_points);
    auto trim_tail_empty = [](auto &vec) {
        auto rit = std::find_if(vec.rbegin(), vec.rend(), [](const auto &e) { return !e.empty(); });
        if (rit != vec.rend()) {
            vec.erase(rit.base(), vec.end());
        } else {
            vec.clear();
        }
    };
    trim_tail_empty(enforcers);
    trim_tail_empty(blockers);

    // Adaptive multi-nozzle layers are sliced on the finest Z grid, but a coarse nozzle extrudes one
    // merged slab at the top of each band. Detecting overhangs from every fine-grid slice turns that slab's
    // overhang into several narrow strips which organic-support avoidance may remove completely. Recreate
    // the same overhang the coarse nozzle would see on its own: compare the retained slab footprint with
    // the model below the band and place the result at the slab's physical bottom.
    //
    // Every layer reads only its own patches and the already finished lslices_extrudable, so the layers are
    // processed in parallel. A slab's overhang belongs to the layer it starts on, not to the one that found
    // it; those are collected per layer and handed over afterwards, in layer order, before the union.
    std::vector<ExPolygons> adaptive_region_slices(m_object->layer_count());
    std::vector<ExPolygons> adaptive_overhangs(m_object->layer_count());
    std::vector<std::vector<std::pair<size_t, ExPolygons>>> found_overhangs(m_object->layer_count());
    tbb::parallel_for(tbb::blocked_range<size_t>(0, m_object->layer_count()),
        [&](const tbb::blocked_range<size_t> &range) {
            for (size_t layer_nr = range.begin(); layer_nr < range.end(); ++ layer_nr) {
                if (m_object->print()->canceled())
                    break;
                Layer *layer = m_object->get_layer(layer_nr);
                for (const LayerRegion *layerm : layer->regions()) {
                    if (layerm->adaptive_merge_patches.empty())
                        continue;

                    for (const AdaptiveMergePatch &patch : layerm->adaptive_merge_patches) {
                        // The region is physically represented by this retained coarse-slab footprint throughout
                        // the band. LayerRegion::slices may already be empty on merged-out fine-grid layers, which
                        // would let the normal detector process those layers again and fragment one coarse overhang.
                        if (! patch.area().empty())
                            append(adaptive_region_slices[layer_nr], patch.area());
                        if (! patch.group_top || patch.bottom_idx == 0 || patch.area().empty())
                            continue;

                        const Layer *below_band = m_object->get_layer(patch.bottom_idx - 1);
                        const coord_t support_offset = scale_(patch.merged_height / tan(threshold_rad));
                        // Keep the same minimum-width filter as the normal detector. Comparing the raw patch against
                        // a filtered lower layer turns a narrowing tip into a fully floating overhang once the lower
                        // contour becomes thinner than the support extrusion width.
                        ExPolygons patch_extrudable = intersection_ex(
                            patch.area(), offset2_ex(patch.area(), -extrusion_width_scaled / 2, extrusion_width_scaled));
                        ExPolygons overhangs = diff_ex(
                            patch_extrudable,
                            offset_ex(below_band->lslices_extrudable, support_offset, SUPPORT_SURFACES_OFFSET_PARAMETERS));
                        if (! overhangs.empty())
                            found_overhangs[layer_nr].emplace_back(patch.bottom_idx, std::move(overhangs));
                    }
                }
                if (! adaptive_region_slices[layer_nr].empty())
                    adaptive_region_slices[layer_nr] = union_ex(adaptive_region_slices[layer_nr]);
            }
        });
    for (std::vector<std::pair<size_t, ExPolygons>> &found : found_overhangs)
        for (std::pair<size_t, ExPolygons> &overhang : found)
            append(adaptive_overhangs[overhang.first], std::move(overhang.second));
    tbb::parallel_for(tbb::blocked_range<size_t>(0, m_object->layer_count()),
        [&adaptive_overhangs](const tbb::blocked_range<size_t> &range) {
            for (size_t layer_nr = range.begin(); layer_nr < range.end(); ++ layer_nr)
                if (! adaptive_overhangs[layer_nr].empty())
                    adaptive_overhangs[layer_nr] = union_ex(adaptive_overhangs[layer_nr]);
        });

    // The 30 s budget checked below covers the per-layer detection only, not the enforcer projection and the
    // coarse slab overhangs prepared above.
    typedef std::chrono::high_resolution_clock clock_;
    typedef std::chrono::duration<double, std::ratio<1> > second_;
    std::chrono::time_point<clock_> t0{ clock_::now() };
    tbb::parallel_for(tbb::blocked_range<size_t>(0, m_object->layer_count()),
        [&](const tbb::blocked_range<size_t>& range) {
            for (size_t layer_nr = range.begin(); layer_nr < range.end(); layer_nr++) {
                if (m_object->print()->canceled())
                    break;
                // FIXME the param enforce_support_layers is not set yet
                if (!(is_auto(stype) || (enforce_support_layers > 0 && layer_nr >= enforce_support_layers) || (layer_nr < enforcers.size() && !enforcers[layer_nr].empty())))
                    continue;

                Layer* layer = m_object->get_layer(layer_nr);

                if (layer->lower_layer == nullptr) {
                    ExPolygons curr_polys = layer->lslices_extrudable;
                    if (layer_nr < blockers.size() && !blockers[layer_nr].empty())
                        curr_polys = diff_ex(curr_polys, offset_ex(union_(blockers[layer_nr]), scale_(radius_sample_resolution)));
                    for (auto& slice : curr_polys) {
                        auto bbox_size = get_extents(slice).size();
                        if (!((bbox_size.x() > length_thresh_well_supported || bbox_size.y() > length_thresh_well_supported))
                            && g_config_support_sharp_tails) {
                            layer->sharp_tails.push_back(slice);
                            layer->sharp_tails_height.push_back(layer->height);
                        }
#ifdef SUPPORT_TREE_DEBUG_TO_SVG
                        SVG::export_expolygons(debug_out_path("sharp_tail_orig_%.02f.svg", layer->print_z), {slice});
#endif
                    }
                    continue;
                }

                Layer* lower_layer = layer->lower_layer;
                coordf_t lower_layer_offset = layer_nr < enforce_support_layers ? -0.15 * extrusion_width : (float)lower_layer->height / tan(threshold_rad);
                //lower_layer_offset = std::min(lower_layer_offset, extrusion_width);
                coordf_t support_offset_scaled = scale_(lower_layer_offset);
                ExPolygons curr_polys = adaptive_region_slices[layer_nr].empty() ?
                    layer->lslices_extrudable : diff_ex(layer->lslices_extrudable, adaptive_region_slices[layer_nr]);
                ExPolygons& lower_polys = lower_layer->lslices_extrudable;
                ExPolygons  lower_layer_offseted  = offset_ex(lower_polys, support_offset_scaled, SUPPORT_SURFACES_OFFSET_PARAMETERS);

                // add enforcer
                ExPolygons enforced_overhangs;
                ExPolygons blocker;
                if (!enforcers.empty()) 
                    enforced_overhangs = intersection_ex(diff_ex(layer->lslices_extrudable, lower_layer->lslices_extrudable), enforcers[layer_nr]);
                if (is_auto(stype)) {
                    // normal overhang
                    overhangs_all_layers[layer_nr] = std::move(diff_ex(curr_polys, lower_layer_offseted));
                    if (! adaptive_overhangs[layer_nr].empty()) {
                        Polygons combined = to_polygons(overhangs_all_layers[layer_nr]);
                        polygons_append(combined, to_polygons(adaptive_overhangs[layer_nr]));
                        overhangs_all_layers[layer_nr] = union_ex(combined);
                    }
                    // if is auto, add blocker first
                    if (layer_nr < blockers.size() && !blockers[layer_nr].empty()) {
                        // Arthur: union_ is a must because after mirroring, the blocker polygons are in left-hand coordinates, ie clockwise,
                        // which are not valid polygons, and will be removed by offset_ex. union_ can make these polygons right.
                        blocker                        = offset_ex(union_(blockers[layer_nr]), scale_(radius_sample_resolution));
                        if (!blocker.empty()) overhangs_all_layers[layer_nr] = diff_ex(overhangs_all_layers[layer_nr], blocker);
                    }
                    if (!enforced_overhangs.empty()) {
                        enforced_overhangs             = diff_ex(offset_ex(enforced_overhangs, enforcer_overhang_offset), lower_layer->lslices_extrudable);
                        overhangs_all_layers[layer_nr] = union_ex(overhangs_all_layers[layer_nr], enforced_overhangs);
                    }
                }
                else if (layer_nr < enforcers.size() && lower_layer) {
                    if (!enforced_overhangs.empty()) {
                        // FIXME this is a hack to make enforcers work on steep overhangs. See STUDIO-7538.
                        enforced_overhangs = diff_ex(offset_ex(enforced_overhangs, enforcer_overhang_offset), lower_layer->lslices_extrudable);
                        overhangs_all_layers[layer_nr] = std::move(enforced_overhangs);
                    }
                }

                double duration{ std::chrono::duration_cast<second_>(clock_::now() - t0).count() };
                if (duration > 30 || overhangs_all_layers[layer_nr].size() > 100) {
                    BOOST_LOG_TRIVIAL(info) << "detect_overhangs takes more than 30 secs, skip cantilever and sharp tails detection: layer_nr=" << layer_nr << " duration=" << duration;
                    config_detect_sharp_tails = false;
                    config_remove_small_overhangs = false;
                    continue;
                }
                if (is_auto(stype) && config_detect_sharp_tails)
                {
                    ExPolygons curr = curr_polys;
                    if (!blocker.empty()) curr = diff_ex(curr, blocker);
                    if (!enforced_overhangs.empty()) curr = union_ex(curr, enforced_overhangs);
                    // BBS detect sharp tail
                    for (const ExPolygon &expoly : curr) {
                        bool is_sharp_tail = false;
                        // 1. nothing below
                        // this is a sharp tail region if it's floating and non-ignorable
                        // The (area && bbox) gate excludes "large looped structures" but also catches
                        // thin-but-wide rings (small area, large bbox), which would otherwise be dropped
                        // by check_small_overhang and end up unsupported. Rescue them here: if eroding by
                        // radius_thresh_small_overhang (the same radius small-overhang detection uses)
                        // makes the polygon vanish, treat it as a sharp tail regardless of bbox.
                        if (!overlaps(offset_ex(expoly, 0.1 * extrusion_width_scaled), lower_polys) &&
                            area(expoly) < SQ(m_support_params.thresh_big_overhang) &&
                            (get_extents(expoly).area() < SQ(m_support_params.thresh_big_overhang) ||
                             offset_ex(expoly, -radius_thresh_small_overhang).empty())) {
                            is_sharp_tail = !offset_ex(expoly, -0.1 * extrusion_width_scaled).empty();
                        }
                        if (is_sharp_tail && lower_layer->lower_layer) {
                            if (overlaps(offset_ex(expoly, 0.1 * extrusion_width_scaled), lower_layer->lower_layer->lslices_extrudable)) is_sharp_tail = false;
                        }
                        if (is_sharp_tail) {
                            layer->sharp_tails.push_back(expoly);
                            layer->sharp_tails_height.push_back(0);

                            has_sharp_tails = true;
#ifdef SUPPORT_TREE_DEBUG_TO_SVG
                            SVG::export_expolygons(debug_out_path("sharp_tail_orig_%.02f.svg", layer->print_z), {expoly});
#endif
                        }
                    }
                }


                // check cantilever
                auto lower_layer_support_thresh = offset_ex(lower_polys, extrusion_width_scaled, SUPPORT_SURFACES_OFFSET_PARAMETERS);
                // lower_layer_offset may be very small, so we need to do max and then add 0.1
                lower_layer_offseted            = offset_ex(lower_layer_offseted, scale_(std::max(extrusion_width - lower_layer_offset, 0.) + 0.1));
                for (ExPolygon &poly : overhangs_all_layers[layer_nr]) {
                    // check if there is some contour that is totally floating
                    bool is_cantilever = false;
                    double dist_max = 0;
                    for (size_t i = 0; i < poly.num_contours(); i++) {
                        Polygon contour      = poly.contour_or_hole(i);
                        bool    is_floating  = true;
                        double  tmp_dist_max = 0;
                        for (const auto &pt : contour.points) {
                            if (is_inside_ex(lower_layer_support_thresh, pt)) {
                                is_floating = false;
                                break;
                            } else
                                tmp_dist_max = std::max(tmp_dist_max, (projection_onto(lower_layer_support_thresh, pt) - pt).cast<double>().norm());
                        }
                        if (is_floating) {
                            is_cantilever = true;
                            dist_max = tmp_dist_max;
                            break;
                        }
                    }
                    if (!is_cantilever) {
                        dist_max = 0;
                        auto     cluster_boundary_ex = intersection_ex(poly, lower_layer_offseted);
                        Polygons cluster_boundary    = to_polygons(cluster_boundary_ex);
                        if (cluster_boundary.empty()) continue;

                        for (auto &pt : poly.contour.points) {
                            double dist_pt = std::numeric_limits<double>::max();
                            for (auto &ply : cluster_boundary) {
                                double d = ply.distance_to(pt);
                                dist_pt  = std::min(dist_pt, d);
                            }
                            dist_max = std::max(dist_max, dist_pt);
                        }
                        is_cantilever = dist_max > scale_(3);
                    }
                    // is cantilever if the farmost point is larger than 3mm away from base or some contour is totally floating
                    if (is_cantilever) {
                        max_cantilever_dist = std::max(max_cantilever_dist, dist_max);
                        layer->cantilevers.emplace_back(poly);
                        BOOST_LOG_TRIVIAL(debug) << "found a cantilever cluster. layer_nr=" << layer_nr << dist_max;
                        has_cantilever = true;
                    }
                }
            }
        }
    ); // end tbb::parallel_for

    BOOST_LOG_TRIVIAL(info) << "max_cantilever_dist=" << max_cantilever_dist;
    if (check_support_necessity)
        return;

    // check if the sharp tails should be extended higher
    if (is_auto(stype) && config_detect_sharp_tails) {
        for (size_t layer_nr = 0; layer_nr < m_object->layer_count(); layer_nr++) {
            if (m_object->print()->canceled())
                break;

            Layer* layer = m_object->get_layer(layer_nr);
            Layer* lower_layer = layer->lower_layer;
            if (!lower_layer)
                continue;

            // BBS detect sharp tail
            const ExPolygons& lower_layer_sharptails = lower_layer->sharp_tails;
            const auto& lower_layer_sharptails_height = lower_layer->sharp_tails_height;
            
            ExPolygons curr_polys = layer->lslices_extrudable;
            if (!blockers.empty() && layer_nr < blockers.size() && !blockers[layer_nr].empty())
                curr_polys = diff_ex(curr_polys, offset_ex(union_(blockers[layer_nr]), scale_(radius_sample_resolution)));
            if (!enforcers.empty() && layer_nr < enforcers.size() && !enforcers[layer_nr].empty()) 
                curr_polys = union_ex(curr_polys, intersection_ex(diff_ex(layer->lslices_extrudable, lower_layer->lslices_extrudable), enforcers[layer_nr]));
            for (ExPolygon& expoly : curr_polys) {
                bool  is_sharp_tail = false;
                float accum_height = layer->height;
                do {
                    // 2. something below
                    // check whether this is above a sharp tail region.

                    // 2.1 If no sharp tail below, this is considered as common region.
                    ExPolygons supported_by_lower = intersection_ex({ expoly }, lower_layer_sharptails);
                    if (supported_by_lower.empty()) {
                        is_sharp_tail = false;
                        break;
                    }

                    // 2.2 If sharp tail below, check whether it support this region enough.
#if 0
                    // judge by area isn't reliable, failure cases include 45 degree rotated cube
                    float       supported_area = area(supported_by_lower);
                    if (supported_area > area_thresh_well_supported) {
                        is_sharp_tail = false;
                        break;
                    }
#endif
                    BoundingBox bbox = get_extents(supported_by_lower);
                    if (bbox.size().x() > length_thresh_well_supported && bbox.size().y() > length_thresh_well_supported) {
                        is_sharp_tail = false;
                        break;
                    }

                    // 2.3 check whether sharp tail exceed the max height
                    for(size_t i=0;i<lower_layer_sharptails.size();i++) {
                        if (lower_layer_sharptails[i].overlaps(expoly)) {
                            accum_height += lower_layer_sharptails_height[i];
                            break;
                        }
                    }
                    if (accum_height > sharp_tail_max_support_height) {
                        is_sharp_tail = false;
                        break;
                    }

                    // 2.4 if the area grows fast than threshold, it get connected to other part or
                    // it has a sharp slop and will be auto supported.
                    ExPolygons new_overhang_expolys = diff_ex({ expoly }, lower_layer_sharptails);
                    if ((get_extents(new_overhang_expolys).size() - get_extents(lower_layer_sharptails).size()).both_comp(Point(scale_(5), scale_(5)), ">") || !offset_ex(new_overhang_expolys, -5.0 * extrusion_width_scaled).empty()) {
                        is_sharp_tail = false;
                        break;
                    }

                    // 2.5 mark the expoly as sharptail
                    is_sharp_tail = true;
                } while (0);

                if (is_sharp_tail) {
                    layer->sharp_tails.push_back(expoly);
                    layer->sharp_tails_height.push_back( accum_height);
                }

            }
        }
    }

    // group overhang clusters
    for (size_t layer_nr = 0; layer_nr < m_object->layer_count(); layer_nr++) {
        if (m_object->print()->canceled())
            break;
        Layer* layer = m_object->get_layer(layer_nr);
        for (auto& overhang : overhangs_all_layers[layer_nr]) {
            OverhangCluster* cluster = find_and_insert_cluster(overhangClusters, overhang, layer_nr, extrusion_width_scaled);
            if (overlaps({overhang}, layer->cantilevers)) cluster->set_type(Cantilever, true);
        }
    }


    for (auto &cluster : overhangClusters) {
        bool enforce_add = false;
        // remove small overhangs
        if (is_auto(stype) && config_remove_small_overhangs) {
            // 3. check whether the small overhang is sharp tail
            cluster.set_type(SharpTail, false);
            for (size_t layer_id = cluster.min_layer; layer_id <= cluster.max_layer; layer_id++) {
                Layer *layer = m_object->get_layer(layer_id);
                if (overlaps(layer->sharp_tails, cluster.layer_overhangs[layer_id])) {
                    cluster.set_type(SharpTail, true);
                    break;
                }
            }
            if (!enforcers.empty() && cluster.min_layer<enforcers.size()) {
                for (size_t layer_id = cluster.min_layer; layer_id <= cluster.max_layer; layer_id++) {
                    if (layer_id >= enforcers.size()) break;
                    if (enforcers[layer_id].empty()) continue;
                    if (overlaps(to_expolygons(enforcers[layer_id]), cluster.layer_overhangs[layer_id])) {
                        enforce_add = true;
                        break;
                    }
                }
            }

            cluster.check_small_overhang(
                extrusion_width_scaled, length_thresh_small_overhang, radius_thresh_small_overhang);

#ifdef SUPPORT_TREE_DEBUG_TO_SVG
            const Layer *layer1 = m_object->get_layer(cluster.min_layer);
            std::string  fname  = debug_out_path("overhangCluster_%d-%d_%.2f-%.2f_tail=%d_cantilever=%d_small=%d.svg", cluster.min_layer, cluster.max_layer, layer1->print_z,
                                                 m_object->get_layer(cluster.max_layer)->print_z, cluster.is_sharp_tail(), cluster.is_cantilever(), cluster.is_type(Small));
            SVG::export_expolygons(fname, {{layer1->lslices, {"min_layer_lslices", "red", 0.5}},
                                           {m_object->get_layer(cluster.max_layer)->lslices, {"max_layer_lslices", "yellow", 0.5}},
                                           {cluster.merged_poly, {"overhang", "blue", 0.5}},
                                           {cluster.is_cantilever() ? layer1->cantilevers : offset_ex(cluster.merged_poly, -1 * extrusion_width_scaled),
                                            {cluster.is_cantilever() ? "cantilever" : "erode1", "green", 0.5}}});
#endif
        }

        if (!cluster.is_type(Small) || enforce_add) {
            cluster.check_polygon_node(m_support_params.thresh_big_overhang, m_ts_data->m_layer_outlines_below[cluster.min_layer - 1]);
            for (auto it = cluster.layer_overhangs.begin(); it != cluster.layer_overhangs.end(); it++) {
                int       layer_nr = it->first;
                ExPolygons overhangs = it->second;
                if (cluster.is_type(Small)) {
                    if (layer_nr >= enforcers.size() || enforcers[layer_nr].empty() || !overlaps(to_expolygons(enforcers[layer_nr]), overhangs)) continue;
                    overhangs = intersection_ex(overhangs, enforcers[layer_nr]);
                }
                for (const auto &overhang : overhangs) add_overhang(m_object->get_layer(layer_nr), overhang, cluster.type);
            }
        }
    }

    int layers_with_overhangs = 0;
    int layers_with_enforcers = 0;
    for (int layer_nr = 0; layer_nr < m_object->layer_count(); layer_nr++) {
        if (m_object->print()->canceled())
            break;

        auto layer = m_object->get_layer(layer_nr);
        auto lower_layer = layer->lower_layer;

        if (support_critical_regions_only && is_auto(stype)) {
            if (lower_layer == nullptr || enforcers.empty() || layer_nr >= enforcers.size() || enforcers[layer_nr].empty()) {
                layer->loverhangs.clear();
                layer->loverhangs_with_type.clear();
                for (const auto &cantilever : layer->cantilevers) add_overhang(layer, cantilever, OverhangType::Cantilever);
            } else {
                ExPolygons                             enforced_overhangs = offset_ex(to_expolygons(enforcers[layer_nr]), enforcer_overhang_offset);
                ExPolygons                             loverhangs_new;
                std::vector<std::pair<ExPolygon, int>> loverhangs_with_type_new;
                ExPolygons                             bigflat;
                for (auto &overhang_part : layer->loverhangs_with_type) {
                    const auto &overhang = overhang_part.first;
                    auto        type     = overhang_part.second;
                    ExPolygons  overhangs;
                    if (type & OverhangType::Cantilever || type & OverhangType::SharpTail)
                        overhangs = {overhang};
                    else if (overlaps(enforced_overhangs, overhang)) {
                        overhangs = intersection_ex(enforced_overhangs, overhang);
                    }
                    if (!overhangs.empty()) {
                        if (type & OverhangType::BigFlat)
                            append(bigflat, overhangs);
                        else
                            for (const auto &expoly : overhangs) {
                                loverhangs_new.emplace_back(expoly);
                                loverhangs_with_type_new.emplace_back(std::make_pair(expoly, type));
                            }
                    }
                }
                for (const auto &expoly : union_ex(bigflat)) {
                    loverhangs_new.emplace_back(expoly);
                    loverhangs_with_type_new.emplace_back(std::make_pair(expoly, OverhangType::BigFlat));
                }
                layer->loverhangs           = std::move(loverhangs_new);
                layer->loverhangs_with_type = std::move(loverhangs_with_type_new);
            }
        }

        // add support for every 1mm height for sharp tails
        if (lower_layer){
            ExPolygons lower_layer_expanded = offset_ex(lower_layer->lslices_extrudable, SCALED_RESOLUTION);
            for (size_t i = 0; i < layer->sharp_tails_height.size();i++) {
                ExPolygons areas = diff_clipped({ layer->sharp_tails[i]}, lower_layer_expanded);
                float accum_height = layer->sharp_tails_height[i];
                if (!areas.empty() && int(accum_height * 10) % 5 == 0) {
                    has_sharp_tails = true;
                    for (auto &area : areas)
                        add_overhang(layer, area, accum_height < EPSILON ? (OverhangType::SharpTail | OverhangType::SharpTailLowesst) : OverhangType::SharpTail);
#ifdef SUPPORT_TREE_DEBUG_TO_SVG
                    SVG::export_expolygons(debug_out_path("sharp_tail_%.02f.svg", layer->print_z), areas);
#endif
                }
            }
        }

        if (max_bridge_length > 0 && layer->loverhangs.size() > 0 && lower_layer) {
            // do not break bridge as the interface will be poor, see #4318
            bool break_bridge = false;
            m_object->remove_bridges_from_contacts(lower_layer, layer, extrusion_width_scaled, &layer->loverhangs,
                max_bridge_length, break_bridge, &layer->loverhangs_with_type);
        }

		int nDetected = layer->loverhangs.size();

        //// fill overhang_types
        //for (size_t i = 0; i < layer->loverhangs.size(); i++)
        //    overhang_types.emplace(&layer->loverhangs[i], i < nDetected ? OverhangType::Detected :
        //        i < nEnforced ? OverhangType::Enforced : OverhangType::SharpTail);

        if (!layer->loverhangs_with_type.empty()) {
            layers_with_overhangs++;
            m_highest_overhang_layer = std::max(m_highest_overhang_layer, size_t(layer_nr));
        }
        //if (nEnforced > 0) layers_with_enforcers++;
        if (!layer->cantilevers.empty()) has_cantilever = true;
    }

    BOOST_LOG_TRIVIAL(info) << "Tree support overhang detection done. " << layers_with_overhangs << " layers with overhangs. nEnforced=" << layers_with_enforcers;

#ifdef SUPPORT_TREE_DEBUG_TO_SVG
    for (const Layer* layer : m_object->layers()) {
        if (layer->loverhangs.empty() && (blockers.size()<=layer->id() || blockers[layer->id()].empty()))
            continue;

        SVG::export_expolygons(debug_out_path("overhang_areas_%d_%.2f.svg",layer->id(), layer->print_z), {
            { m_object->get_layer(layer->id())->lslices_extrudable, {"lslices_extrudable","yellow",0.5} },
            { layer->loverhangs, {"overhang","red",0.5} }
            });

        if (enforcers.size() > layer->id()) {
            SVG svg(format("SVG/enforcer_%s.svg", layer->print_z), m_object->bounding_box());
            if (svg.is_opened()) {
                svg.draw_outline(m_object->get_layer(layer->id())->lslices_extrudable, "yellow");
                svg.draw(enforcers[layer->id()], "red");
            }
        }
        if (blockers.size() > layer->id()) {
            SVG svg(format("SVG/blocker_%s.svg", layer->print_z), m_object->bounding_box());
            if (svg.is_opened()) {
                svg.draw_outline(m_object->get_layer(layer->id())->lslices_extrudable, "yellow");
                svg.draw(blockers[layer->id()], "red");
            }
        }
    }
#endif
}

// create support layers for raft and tree support
// if support is disabled, only raft layers are created
void TreeSupport::create_tree_support_layers()
{
    int layer_id = 0;
    if (m_raft_layers > 0) { //create raft layers
        coordf_t raft_print_z = 0.f;
        coordf_t raft_slice_z = 0.f;
        if (m_slicing_params.base_raft_layers > 0)
        {
            // Do not add the raft contact layer, 1st layer should use first_print_layer_height
            coordf_t height = m_slicing_params.first_print_layer_height;
            raft_print_z += height;
            raft_slice_z = raft_print_z - height / 2;
            m_object->add_tree_support_layer(layer_id++, height, raft_print_z, raft_slice_z);
        }
        // Insert the base layers.
        for (size_t i = 1; i < m_slicing_params.base_raft_layers; i++) {
            coordf_t height = m_slicing_params.base_raft_layer_height;
            raft_print_z += height;
            raft_slice_z = raft_print_z - height / 2;
            m_object->add_tree_support_layer(layer_id++, height, raft_print_z, raft_slice_z);
        }
        // Insert the interface layers.
        for (size_t i = 0; i < m_slicing_params.interface_raft_layers; i++) {
            coordf_t height = m_slicing_params.interface_raft_layer_height;
            raft_print_z += height;
            raft_slice_z = raft_print_z - height / 2;
            m_object->add_tree_support_layer(layer_id++, height, raft_print_z, raft_slice_z);
        }

        // Layers between the raft contacts and bottom of the object.
        double dist_to_go = m_slicing_params.object_print_z_min - raft_print_z;
        if (dist_to_go > EPSILON) {
            auto nsteps = int(ceil(dist_to_go / m_slicing_params.max_suport_layer_height));
            double height = dist_to_go / nsteps;
            for (int i = 0; i < nsteps; ++i) {
                raft_print_z += height;
                raft_slice_z = raft_print_z - height / 2;
                m_object->add_tree_support_layer(layer_id++, height, raft_print_z, raft_slice_z);
            }
        }
        m_raft_layers = layer_id;
    }
}

static inline BoundingBox fill_expolygon_generate_paths(
    ExtrusionEntitiesPtr    &dst,
    ExPolygon               &expolygon,
    Fill                    *filler,
    const FillParams        &fill_params,
    ExtrusionRole            role,
    const Flow              &flow)
{
    Surface surface(stInternal, std::move(expolygon));
    Polylines polylines;
    try {
        polylines = filler->fill_surface(&surface, fill_params);
    } catch (InfillFailedException &) {
    }

    BoundingBox fill_bbox;
    if (!polylines.empty()) {
        fill_bbox = polylines[0].bounding_box();
        for (auto& polyline : polylines)
            fill_bbox.merge(polyline.bounding_box());
    }

    extrusion_entities_append_paths(dst, std::move(polylines), role, flow.mm3_per_mm(), flow.width(), flow.height());

    return fill_bbox;
}

static inline std::vector<BoundingBox> fill_expolygons_generate_paths(
    ExtrusionEntitiesPtr   &dst,
    ExPolygons             &expolygons,
    Fill                   *filler,
    const FillParams       &fill_params,
    ExtrusionRole           role,
    const Flow             &flow)
{
    std::vector<BoundingBox> fill_boxes;
    for (ExPolygon& expoly : expolygons) {
        auto box = fill_expolygon_generate_paths(dst, expoly, filler, fill_params, role, flow);
        fill_boxes.emplace_back(box);
    }
    return fill_boxes;
}

static void _make_loops(ExtrusionEntitiesPtr& loops_entities, ExPolygons &support_area, ExtrusionRole role, size_t wall_count, const Flow &flow)
{
    Polygons       loops;
    std::map<ExPolygon *, int> depth_per_expoly;
    std::list<ExPolygon> expoly_list;

    for (ExPolygon &expoly : support_area) {
        expoly_list.emplace_back(std::move(expoly));
        depth_per_expoly.insert({&expoly_list.back(), 0});
    }
    if (expoly_list.empty()) return;

    while (!expoly_list.empty()) {
        polygons_append(loops, to_polygons(expoly_list.front()));

        auto first_iter = expoly_list.begin();
        auto depth_iter = depth_per_expoly.find(&expoly_list.front());
        if (depth_iter->second + 1 < wall_count) {
            //ExPolygons expolys_new = offset_ex(expoly_list.front(), -float(flow.scaled_spacing()), jtSquare);
            // shrink and then dilate to prevent overlapping and overflow
            ExPolygons expolys_new = offset2_ex({expoly_list.front()}, -1.4 * float(flow.scaled_spacing()), .4 * float(flow.scaled_spacing()));

            for (ExPolygon &expoly : expolys_new) {
                auto new_iter = expoly_list.insert(expoly_list.begin(), expoly);
                depth_per_expoly.insert({&*new_iter, depth_iter->second + 1});
            }
        }
        depth_per_expoly.erase(depth_iter);
        expoly_list.erase(first_iter);
    }

    extrusion_entities_append_loops(loops_entities, std::move(loops), role, float(flow.mm3_per_mm()), float(flow.width()), float(flow.height()));

}

static void make_perimeter_and_inner_brim(ExtrusionEntitiesPtr &dst, const ExPolygon &support_area, size_t wall_count, const Flow &flow, ExtrusionRole role)
{
    Polygons   loops;
    ExPolygons support_area_new = offset_ex(support_area, -0.5f * float(flow.scaled_spacing()), jtSquare);
    _make_loops(dst, support_area_new, role, wall_count, flow);
}

static void make_perimeter_and_infill(ExtrusionEntitiesPtr& dst, const ExPolygon& support_area, size_t wall_count, const Flow& flow, ExtrusionRole role, Fill* filler_support, double support_density, bool infill_first=true)
{
    Polygons   loops;
    ExPolygons support_area_new = offset_ex(support_area, -0.5f * float(flow.scaled_spacing()), jtSquare);

    // draw infill
    FillParams fill_params;
    fill_params.density = support_density;
    fill_params.dont_adjust = true;
    ExPolygons to_infill = offset_ex(support_area, -float(wall_count) * float(flow.scaled_spacing()), jtSquare);
    std::vector<BoundingBox> fill_boxes = fill_expolygons_generate_paths(dst, to_infill, filler_support, fill_params, role, flow);

    // allow wall_count to be zero, which means only draw infill
    if (wall_count == 0) {
        for (auto fill_bbox : fill_boxes)
        {
            // extend bounding box on x-axis
            if (cos(filler_support->angle)>=sin(filler_support->angle)) {
                fill_bbox.min[0] -= scale_(10);
                fill_bbox.max[0] += scale_(10);
            }
            else {
                fill_bbox.min[1] -= scale_(10);
                fill_bbox.max[1] += scale_(10);
            }
            support_area_new = diff_ex(support_area_new, offset_ex(to_expolygons({ fill_bbox.polygon() }), 0.5*flow.scaled_width()));
        }
        // filter out small areas
        for (auto it = support_area_new.begin(); it != support_area_new.end(); ) {
            if (offset_ex(*it, -flow.scaled_width()).empty())
                it = support_area_new.erase(it);
            else
                it++;
        }
    }

    { // draw loops
        ExtrusionEntitiesPtr loops_entities;
        _make_loops(loops_entities, support_area_new, role, wall_count, flow);

        if (infill_first)
            dst.insert(dst.end(), loops_entities.begin(), loops_entities.end());
        else { // loops first
            loops_entities.insert(loops_entities.end(), dst.begin(), dst.end());
            dst = std::move(loops_entities);
        }
    }
    dst.erase(std::remove_if(dst.begin(), dst.end(), [](ExtrusionEntity *entity) { return static_cast<ExtrusionEntityCollection *>(entity)->empty(); }), dst.end());
    if (infill_first) {
        // sort regions to reduce travel
        Points ordering_points;
        for (const auto& area : dst)
            ordering_points.push_back(area->first_point());
        std::vector<Points::size_type> order = chain_points(ordering_points);
        ExtrusionEntitiesPtr new_dst;
        new_dst.reserve(ordering_points.size());
        for (size_t i : order)
            new_dst.emplace_back(dst[i]);
        dst = new_dst;
    }
}

coordf_t TreeSupport::support_flare_step_width(coordf_t print_z, coordf_t layer_height) const
{
    double band_height = 0.;
    const SupportMergePlan::Candidate candidate =
        m_support_merge_plan.coarsest_band_containing(print_z, band_height);
    if (! candidate.valid() || band_height <= layer_height + EPSILON)
        return m_support_params.support_extrusion_width;

    const Flow slab_flow = m_support_params.support_material_flow_for_nozzle(
        candidate.nozzle_diameter, float(band_height));
    return slab_flow.width() * layer_height / band_height;
}

static ExPolygon tree_support_node_disk(const SupportNode &node, const Polygon &unit_circle,
                                        coordf_t branch_radius, coordf_t branch_radius_scaled,
                                        bool square_support, coordf_t radius = 0.)
{
    Polygon circle(unit_circle);
    const double scale  = (radius > 0. ? radius : node.radius) / branch_radius;
    const double move_x = node.movement.x() / (scale * branch_radius_scaled);
    const double move_y = node.movement.y() / (scale * branch_radius_scaled);
    if (! square_support && std::abs(move_x) > 0.001 && std::abs(move_y) > 0.001) {
        const double vsize_inv = 0.5 / (0.01 + std::sqrt(move_x * move_x + move_y * move_y));
        const double matrix[4] {
            scale * (1. + move_x * move_x * vsize_inv),
            scale * move_x * move_y * vsize_inv,
            scale * move_x * move_y * vsize_inv,
            scale * (1. + move_y * move_y * vsize_inv)
        };
        for (Point &vertex : circle.points) {
            const double x = matrix[0] * vertex.x() + matrix[1] * vertex.y();
            const double y = matrix[2] * vertex.x() + matrix[3] * vertex.y();
            vertex = node.position + Point(x, y);
        }
    } else
        for (Point &vertex : circle.points)
            vertex = vertex * scale + node.position;
    return ExPolygon(std::move(circle));
}

// Interface probes of nodes resting on one large overhang are all copies of that overhang (see probe_of()
// in fold_nodes_multi_diameter()), so a slab can collect the same polygon once per node. union_ex() can
// stall on such input: one slab collected ~1500 copies of a single 130 point outline and its union did not
// finish in 20 minutes. All callers use the non-zero fill rule, under which an exact duplicate changes no
// union, intersection or bounding box, so dropping them first is lossless.
static void remove_duplicate_expolygons(ExPolygons &expolys)
{
    if (expolys.size() < 2)
        return;
    auto points_less = [](const Points &lhs, const Points &rhs) {
        if (lhs.size() != rhs.size())
            return lhs.size() < rhs.size();
        return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
            [](const Point &a, const Point &b) { return a.x() < b.x() || (a.x() == b.x() && a.y() < b.y()); });
    };
    auto expoly_less = [&points_less](const ExPolygon &lhs, const ExPolygon &rhs) {
        if (lhs.contour.points != rhs.contour.points)
            return points_less(lhs.contour.points, rhs.contour.points);
        if (lhs.holes.size() != rhs.holes.size())
            return lhs.holes.size() < rhs.holes.size();
        for (size_t i = 0; i < lhs.holes.size(); ++ i)
            if (lhs.holes[i].points != rhs.holes[i].points)
                return points_less(lhs.holes[i].points, rhs.holes[i].points);
        return false;
    };
    std::sort(expolys.begin(), expolys.end(), expoly_less);
    expolys.erase(std::unique(expolys.begin(), expolys.end()), expolys.end());
}

// BBS multi-diameter: fold the node chains onto the slab grid before any area exists.
//
// The tree is planned on the fine node grid so collision avoidance keeps its resolution, but a coarse nozzle
// prints several of those layers as one slab. Folding here rather than after draw_circles() means every later
// stage - floating support pruning, disk rendering, toolpath generation - works on the geometry
// that is actually extruded instead of on a fine grid that never reaches the plate.
//
// The carrier is chosen per node column rather than per slab. A slab is emitted at the top of the material
// it carries, and different columns reach different heights inside one band: lifting a column that starts
// low in the band to the top of the highest one would put its roof into its own top_z gap, and dropping its
// base because it is absent from the highest column's layer would leave that roof hanging.
//
// Four things happen, in this order:
//   1. every foldable node is assigned the column it belongs to inside its slab, the column's top being the
//      highest layer its chain reaches there; each column top is a candidate carrier and is validated;
//   2. distance_to_top and support_roof_layers_below are renumbered along each chain into printed layers,
//      which is what makes support_interface_top_layers mean "N printed interface layers" again.
//      draw_circles() then classifies roof / roof_1st_layer / base from them unchanged;
//   3. the nodes are folded: the sparse base keeps one representative cross-section per column, the
//      interface keeps all of them, because the slab is one extrusion covering every roof in its span;
//   4. the printed layer index used for interface pattern alternation is assigned.
void TreeSupport::fold_nodes_multi_diameter()
{
    m_fold_carriers.clear();
    m_fold_slab_of_layer.clear();
    m_base_only_fold_layers.clear();

    const SupportMergePlan &plan = m_support_merge_plan;
    if (! plan.enabled || plan.slabs.empty())
        return;

    const std::vector<LayerHeightData> &layer_heights = m_ts_data->layer_heights;
    const size_t num_layers = std::min(contact_nodes.size(), layer_heights.size());
    if (num_layers == 0)
        return;

    // Support layers grouped by the slab they fall into, ascending. A layer with no height occupies no Z, so
    // it can neither carry an extrusion nor be swallowed into one.
    std::vector<std::vector<size_t>> layers_per_slab(plan.slabs.size());
    for (size_t layer_nr = 0; layer_nr < num_layers; ++ layer_nr) {
        if (layer_heights[layer_nr].height < EPSILON)
            continue;
        const SupportMergePlan::Slab *slab = plan.slab_for_layer(layer_heights[layer_nr].obj_layer_nr);
        if (slab == nullptr)
            continue;
        layers_per_slab[size_t(slab - plan.slabs.data())].emplace_back(layer_nr);
    }

    const int  explicit_interface_filament = m_object->config().support_interface_filament.value;
    const coord_t xy_distance_scaled = scale_(m_ts_data->m_xy_distance);
    // One decision for the whole object: an interface that folds on some slabs and not on others would leave
    // the roof counters in two different units along one chain.
    const bool fold_interface = plan.interface_fold_enabled;

    // Only nodes that are going to move belong to a column: with the interface left on the fine grid, an
    // interface node is not what a slab emits, so it must neither pick a carrier nor be swallowed.
    auto foldable = [fold_interface](const SupportNode *node) {
        return node != nullptr && node->distance_to_top >= 0 &&
               (fold_interface || node->support_roof_layers_below < 0);
    };
    // parents holds parent plus every neighbour merged into the node; older nodes may only carry parent.
    auto for_each_parent = [](const SupportNode *node, auto &&fn) {
        if (! node->parents.empty()) {
            for (const SupportNode *parent : node->parents)
                if (parent != nullptr)
                    fn(parent);
        } else if (node->parent != nullptr)
            fn(node->parent);
    };

    // One column's slab. Its extrusion spans from bottom_z up to the carrier, the column's top.
    struct Carrier
    {
        size_t   slab_idx { 0 };
        size_t   top_layer { 0 };
        size_t   base_source_layer { 0 };
        size_t   bottom_obj_layer { 0 };
        size_t   obj_layer { 0 };
        coordf_t bottom_z { 0. };
        coordf_t top_z { 0. };
        coordf_t slab_height { 0. };
        size_t   original_base_source_layer { 0 };
        size_t   original_bottom_obj_layer { 0 };
        coordf_t original_bottom_z { 0. };
        coordf_t original_slab_height { 0. };
        ExPolygons footprint;
        ExPolygons base_footprint;
        ExPolygons interface_footprint;
        BoundingBox footprint_bbox;
        BoundingBox base_footprint_bbox;
        std::vector<size_t> partial_heads;
        Carrier *lower_carrier { nullptr };
        bool     folds { false };
    };
    // Per slab: the column top of every foldable node, and the candidate carriers keyed by support layer.
    std::vector<std::unordered_map<const SupportNode *, size_t>> column_top(plan.slabs.size());
    std::vector<std::map<size_t, Carrier>>                       carriers(plan.slabs.size());

    const bool square_support = avg_node_per_layer > 200;
    const int circle_resolution = square_support ? 4 : 25;
    const coordf_t branch_radius = m_object->config().tree_support_branch_diameter.value / 2.;
    const coordf_t branch_radius_scaled = scale_(branch_radius);
    Polygon branch_circle;
    branch_circle.points.reserve(circle_resolution);
    for (int i = 0; i < circle_resolution; ++ i) {
        const double angle = double(i) / circle_resolution * TAU +
            (square_support ? PI / 4. + nodes_angle : 0.);
        branch_circle.append(Point(cos(angle) * branch_radius_scaled,
                                   sin(angle) * branch_radius_scaled));
    }
    const bool on_buildplate_only = m_object_config->support_on_build_plate_only.value;
    // Match the raw geometry draw_circles() will render. Collision clipping is deliberately left out: it
    // may only shrink this probe, so refusing an ambiguous fold remains the safe outcome.
    auto probe_of = [&](const SupportNode *node, bool use_interface_shape,
                        ExPolygons &probe, coordf_t radius) {
        if (node == nullptr || node->distance_to_top < 0)
            return;
        if (node->type == ePolygon) {
            if (node->overhang.empty())
                return;
            probe = offset_ex({ node->overhang }, float(xy_distance_scaled));
            return;
        }
        if (use_interface_shape && ! on_buildplate_only) {
            if (node->overhang.empty())
                return;
            probe = node->overhang.contour.size() > 100 || node->overhang.holes.size() > 1 ?
                ExPolygons{ node->overhang } :
                offset_ex({ node->overhang }, float(xy_distance_scaled));
            return;
        }

        probe.emplace_back(tree_support_node_disk(
            *node, branch_circle, branch_radius, branch_radius_scaled, square_support, radius));
    };
    std::unordered_map<const SupportNode *, ExPolygons> base_probe_cache;
    std::unordered_map<const SupportNode *, ExPolygons> interface_probe_cache;
    auto cached_probe_of = [&](const SupportNode *node, bool use_interface_shape) -> const ExPolygons & {
        auto &cache = use_interface_shape ? interface_probe_cache : base_probe_cache;
        auto [it, inserted] = cache.try_emplace(node);
        if (inserted)
            probe_of(node, use_interface_shape, it->second, node->radius);
        return it->second;
    };

    // Pass 1: columns and carriers, per slab.
    for (size_t slab_idx = 0; slab_idx < plan.slabs.size(); ++ slab_idx) {
        const std::vector<size_t> &layers = layers_per_slab[slab_idx];
        if (layers.size() < 2)
            continue;

        // Walk the slab top down so a node's parents inside the slab already know their column top. A parent
        // outside the slab - or one that does not move - is absent from the map and ends the column there.
        // When differently aligned columns merge, start a new carrier at the merge layer. This leaves their
        // partial heads on the fine grid without carrying that refusal into the common trunk below.
        std::unordered_map<const SupportNode *, size_t> &tops = column_top[slab_idx];
        for (auto it = layers.rbegin(); it != layers.rend(); ++ it)
            for (const SupportNode *node : contact_nodes[*it]) {
                if (! foldable(node))
                    continue;
                size_t top = *it;
                size_t parent_top = 0;
                bool has_parent_top = false;
                bool different_parent_tops = false;
                for_each_parent(node, [&](const SupportNode *parent) {
                    const auto parent_it = tops.find(parent);
                    if (parent_it == tops.end())
                        return;
                    if (! has_parent_top) {
                        parent_top = parent_it->second;
                        has_parent_top = true;
                    } else if (parent_it->second != parent_top)
                        different_parent_tops = true;
                });
                if (has_parent_top && ! different_parent_tops)
                    top = std::max(top, parent_top);
                else if (different_parent_tops) {
                    Carrier &carrier = carriers[slab_idx][top];
                    for_each_parent(node, [&](const SupportNode *parent) {
                        const auto parent_it = tops.find(parent);
                        if (parent_it != tops.end() && parent_it->second != top)
                            carrier.partial_heads.emplace_back(parent_it->second);
                    });
                }
                tops[node] = top;
                carriers[slab_idx][top];
            }
        if (tops.empty())
            continue;

        const size_t   front         = layers.front();
        const coordf_t slab_bottom_z = layer_heights[front].print_z - layer_heights[front].height;
        const size_t   slab_bottom_obj_layer = layer_heights[front].obj_layer_nr;
        // Without a raft the first band starts on the bed, so its bed face is the representative cross
        // section - the same choice the model band planner makes, and the one that keeps the adhesion
        // geometry. With a raft the band rests on the raft and behaves like any other.
        const bool bed_anchor = plan.slabs[slab_idx].bottom_anchor && m_raft_layers == 0;

        // Carriers ascend, and so does the span each one has to clear: grow it incrementally.
        ExPolygons span_collision;
        size_t     span_top_obj_layer = slab_bottom_obj_layer;
        for (auto &[carrier_layer, carrier] : carriers[slab_idx]) {
            carrier.slab_idx          = slab_idx;
            carrier.top_layer         = carrier_layer;
            carrier.obj_layer         = layer_heights[carrier_layer].obj_layer_nr;
            carrier.bottom_obj_layer = slab_bottom_obj_layer;
            carrier.bottom_z         = slab_bottom_z;
            carrier.top_z            = layer_heights[carrier_layer].print_z;
            carrier.slab_height      = carrier.top_z - slab_bottom_z;
            // A column whose top is the slab's bottom layer has nothing to fold.
            if (carrier.slab_height <= layer_heights[carrier_layer].height + EPSILON)
                continue;
            // A carrier below the band top shortens the slab, so the nozzle has to be able to extrude the
            // shortened height as well. Too thin and the column simply stays on the fine grid, which is what
            // R4 already does for a chain head hanging off the top of the ladder.
            if (! adaptive_layer_height_range(plan.slabs[slab_idx].nozzle_diameter).contains(carrier.slab_height))
                continue;

            carrier.base_source_layer = carrier_layer;
            if (bed_anchor)
                for (const SupportNode *node : contact_nodes[front]) {
                    const auto top_it = tops.find(node);
                    if (top_it != tops.end() && top_it->second == carrier_layer) {
                        carrier.base_source_layer = front;
                        break;
                    }
                }
            carrier.original_base_source_layer = carrier.base_source_layer;
            carrier.original_bottom_obj_layer  = carrier.bottom_obj_layer;
            carrier.original_bottom_z          = carrier.bottom_z;
            carrier.original_slab_height       = carrier.slab_height;

            // A slab is one extrusion over its whole Z span, so it has to clear the object everywhere in that
            // span and not only at the face its cross-section was taken from. Refuse the fold outright when
            // nothing would survive: printing those layers thin is wasteful for this nozzle, but dropping them
            // removes the support altogether, and keeping the thin ones is the lesser evil.
            bool appended = false;
            for (; span_top_obj_layer < carrier.obj_layer; ++ span_top_obj_layer)
                if (span_top_obj_layer < m_ts_data->m_layer_outlines.size()) {
                    append(span_collision, offset_ex(m_ts_data->m_layer_outlines[span_top_obj_layer], float(xy_distance_scaled)));
                    appended = true;
                }
            if (appended)
                span_collision = union_ex(span_collision);
            if (! span_collision.empty()) {
                bool any_foldable = false;
                bool survives     = false;
                // Only this column's nodes on the faces it would draw matter. The roof counters are still in
                // fine units here, so the interface is not told apart: a slab whose contact disks all vanish
                // is no better than one whose base does.
                for (const size_t probe_layer : { carrier.base_source_layer, carrier_layer }) {
                    for (const SupportNode *node : contact_nodes[probe_layer]) {
                        const auto top_it = tops.find(node);
                        if (top_it == tops.end() || top_it->second != carrier_layer)
                            continue;
                        const bool use_interface_shape =
                            m_support_params.num_top_interface_layers > 0 &&
                            (node->support_roof_layers_below > 1 ||
                             (node->support_roof_layers_below >= 0 && ! node->is_sharp_tail));
                        const ExPolygons &probe = cached_probe_of(node, use_interface_shape);
                        if (probe.empty())
                            continue;
                        any_foldable = true;
                        if (! diff_ex(probe, span_collision).empty()) {
                            survives = true;
                            break;
                        }
                    }
                    if (survives || probe_layer == carrier_layer)
                        break;
                }
                if (any_foldable && ! survives)
                    continue;
            }

            carrier.folds = true;
        }
    }

    // A common carrier replaces only the material below its synchronization boundary. Its differently
    // aligned heads stay on their original layers; refusing them must not disable the common trunk.
    for (size_t slab_idx = 0; slab_idx < plan.slabs.size(); ++ slab_idx) {
        std::vector<size_t> partial_heads;
        for (const auto &entry : carriers[slab_idx])
            if (entry.second.folds)
                partial_heads.insert(partial_heads.end(),
                                     entry.second.partial_heads.begin(), entry.second.partial_heads.end());
        std::unordered_set<size_t> refused_heads;
        while (! partial_heads.empty()) {
            const size_t partial_head = partial_heads.back();
            partial_heads.pop_back();
            if (! refused_heads.insert(partial_head).second)
                continue;
            Carrier &carrier = carriers[slab_idx].at(partial_head);
            carrier.folds = false;
            partial_heads.insert(partial_heads.end(),
                                 carrier.partial_heads.begin(), carrier.partial_heads.end());
        }
    }

    auto move_target = [&](size_t slab_idx, const SupportNode *node) -> Carrier * {
        const auto top_it = column_top[slab_idx].find(node);
        if (top_it == column_top[slab_idx].end())
            return nullptr;
        Carrier *carrier = &carriers[slab_idx].at(top_it->second);
        while (node->print_z <= carrier->bottom_z + EPSILON &&
               carrier->lower_carrier != nullptr)
            carrier = carrier->lower_carrier;
        return carrier->folds && node->print_z > carrier->bottom_z + EPSILON ?
            carrier : nullptr;
    };

    // A carrier extrudes its whole layer at the slab height, so it may not share the layer with a node that
    // stays there: that node was already printed on the layers below and would be extruded a second time.
    // Refusing a carrier leaves its own column behind on lower layers, which may be carriers in turn.
    auto refuse_shared_carriers = [&]() {
        for (bool refused = true; refused;) {
            refused = false;
            for (size_t slab_idx = 0; slab_idx < plan.slabs.size(); ++ slab_idx) {
                const std::unordered_map<const SupportNode *, size_t> &tops = column_top[slab_idx];
                for (auto &[carrier_layer, carrier] : carriers[slab_idx]) {
                    if (! carrier.folds)
                        continue;
                    // Structured bindings cannot be captured before C++20 (clang rejects it), hence the init-capture.
                    const bool shared = std::any_of(contact_nodes[carrier_layer].begin(), contact_nodes[carrier_layer].end(),
                        [&, carrier_layer = carrier_layer](const SupportNode *node) {
                            if (! foldable(node))
                                return false;
                            const auto top_it = tops.find(node);
                            return top_it != tops.end() && top_it->second != carrier_layer &&
                                   move_target(slab_idx, node) == nullptr;
                        });
                    if (shared) {
                        carrier.folds = false;
                        refused       = true;
                    }
                }
            }
        }
    };
    refuse_shared_carriers();

    using NodeMoves = std::unordered_map<const SupportNode *, size_t>;
    using NodeCounters = std::unordered_map<SupportNode *, std::pair<int, int>>;
    std::unordered_map<const SupportNode *, coordf_t> folded_radius;
    auto evaluate_folded_radius = [&](const NodeMoves &moves) {
        folded_radius.clear();
        for (size_t slab_idx = 0; slab_idx < plan.slabs.size(); ++ slab_idx) {
            struct FoldedColumn {
                FoldedColumn *parent { nullptr };
                coordf_t      max_radius;
                size_t        carrier_layer;
            };
            std::unordered_map<const SupportNode *, FoldedColumn> columns;
            columns.reserve(column_top[slab_idx].size());
            for (const auto &entry : column_top[slab_idx]) {
                const SupportNode *node = entry.first;
                const auto move_it = moves.find(node);
                if (move_it == moves.end())
                    continue;
                FoldedColumn &column = columns.emplace(
                    node, FoldedColumn { nullptr, node->radius, move_it->second }).first->second;
                column.parent = &column;
            }
            auto find_column = [](FoldedColumn *column) {
                FoldedColumn *root = column;
                while (root->parent != root)
                    root = root->parent;
                while (column->parent != column) {
                    FoldedColumn *next = column->parent;
                    column->parent = root;
                    column = next;
                }
                return root;
            };
            for (auto &[node, column] : columns) {
                if (node->parent == nullptr)
                    continue;
                const auto parent_column = columns.find(node->parent);
                if (parent_column == columns.end() ||
                    parent_column->second.carrier_layer != column.carrier_layer)
                    continue;
                FoldedColumn *node_root   = find_column(&column);
                FoldedColumn *parent_root = find_column(&parent_column->second);
                if (node_root != parent_root) {
                    parent_root->max_radius = std::max(parent_root->max_radius, node_root->max_radius);
                    node_root->parent = parent_root;
                }
            }
            for (auto &[node, column] : columns)
                folded_radius.emplace(node, find_column(&column)->max_radius);
        }
    };

    // Every folded span is built on the support-layer grid. Interface material may contain a finer span:
    // the final-area pass removes what it covers. A genuine partial overlap cannot be repaired in XY, so
    // reject the less useful carrier first; the fixed-point check also rejects the survivor if the fine
    // layers left behind still overlap it unsafely.
    auto evaluate_roof_layers = [&](const NodeMoves &moves) {
        NodeCounters counters;
        if (! fold_interface)
            return counters;
        // The roof counter is carried down the chain like steps. Deriving it from the node's own fine counters
        // (roof + distance_to_top - steps) breaks below a merge: the merged node keeps the larger distance_to_top
        // of one branch and the larger roof of another, so their sum no longer matches the chain above and a deep
        // base node could come out as roof / roof_1st_layer again.
        struct ChainState { int steps; int roof; size_t printed; };
        std::unordered_map<const SupportNode *, ChainState> chain;
        std::vector<const SupportNode *> merged;
        // 汇合时被另一父节点完整包住的父节点（圆在多边形内、小圆在大圆内）不参与计数，以外层父节点为准。
        auto contained_in = [](const SupportNode *inner, const SupportNode *outer) {
            if (inner == outer || inner->type == ePolygon)
                return false;
            if (outer->type == ePolygon)
                return ! outer->overhang.empty() && outer->overhang.contains(inner->position);
            const double r_in = scale_(inner->radius), r_out = scale_(outer->radius);
            return r_out > r_in + SCALED_EPSILON && (outer->position - inner->position).cast<double>().norm() + r_in <= r_out;
        };
        for (size_t layer_nr = num_layers; layer_nr -- > 0;)
            for (SupportNode *node : contact_nodes[layer_nr]) {
                if (node == nullptr || node->distance_to_top < 0)
                    continue;
                const auto move_it = moves.find(node);
                const size_t printed = move_it == moves.end() ? layer_nr : move_it->second;
                int  steps      = -1;
                int  roof       = 0;
                bool has_parent = false;
                merged.clear();
                for_each_parent(node, [&](const SupportNode *parent) {
                    if (chain.count(parent))
                        merged.emplace_back(parent);
                });
                for (const SupportNode *parent : merged) {
                    if (merged.size() > 1 && std::any_of(merged.begin(), merged.end(),
                            [&](const SupportNode *other) { return contained_in(parent, other); }))
                        continue;
                    const ChainState &state = chain.at(parent);
                    const int step = state.printed != printed ? 1 : 0;
                    // 分支汇合处：层数取大、接触面层数取小，汇合点按更深的分支算主体（各分支汇合前至少已打一层接触面）。
                    steps = std::max(steps, state.steps + step);
                    roof  = has_parent ? std::min(roof, state.roof - step) : state.roof - step;
                    has_parent = true;
                }
                if (! has_parent) {
                    steps = node->distance_to_top;
                    roof  = node->support_roof_layers_below;
                }
                chain[node] = { steps, roof, printed };
                counters[node] = { steps, roof };
            }
        return counters;
    };
    // counters is { distance_to_top, support_roof_layers_below } in printed layers.
    auto is_interface_role = [&](const std::pair<int, int> &counters, size_t printed_layer) {
        const auto [distance_to_top, roof_layers] = counters;
        return m_support_params.num_top_interface_layers > 0 &&
               layer_heights[printed_layer].obj_layer_nr > 0 &&
               (roof_layers > 0 ||
                (roof_layers == 0 &&
                 distance_to_top >= m_support_params.num_top_interface_layers));
    };
    auto footprints_overlap = [](const ExPolygons &lhs, const BoundingBox &lhs_bbox,
                                 const ExPolygons &rhs, const BoundingBox &rhs_bbox) {
        return ! lhs.empty() && ! rhs.empty() && lhs_bbox.overlap(rhs_bbox) &&
               ! intersection_ex(lhs, rhs).empty();
    };
    auto contains_span = [](const Carrier &outer, const Carrier &inner) {
        return outer.bottom_z <= inner.bottom_z + EPSILON && outer.top_z >= inner.top_z - EPSILON;
    };
    size_t misaligned_carriers = 0;
    auto refuse_carriers = [&](const std::unordered_set<Carrier *> &refused) {
        misaligned_carriers += refused.size();
        for (Carrier *carrier : refused)
            carrier->folds = false;
        // Survivors changed, so every split is rebuilt from its original span in the next pass.
        for (auto &slab_carriers : carriers)
            for (auto &entry : slab_carriers) {
                Carrier &carrier = entry.second;
                if (carrier.lower_carrier == nullptr)
                    continue;
                carrier.base_source_layer = carrier.original_base_source_layer;
                carrier.bottom_obj_layer  = carrier.original_bottom_obj_layer;
                carrier.bottom_z          = carrier.original_bottom_z;
                carrier.slab_height       = carrier.original_slab_height;
                carrier.lower_carrier     = nullptr;
            }
        refuse_shared_carriers();
    };
    NodeMoves moves_to;
    NodeCounters final_counters;
    // Recompute the tentative printed counters and footprints after every refusal. The roof/base split is
    // defined in printed-layer units, so using the original counters here could omit a node that Pass 2
    // later turns into interface material.
    for (;;) {
        // Every pass redoes the polygon unions of all carriers, so let Stop end support generation here.
        if (throw_on_cancel)
            throw_on_cancel();
        NodeMoves tentative_moves;
        for (size_t slab_idx = 0; slab_idx < plan.slabs.size(); ++ slab_idx)
            for (const auto &[node, top] : column_top[slab_idx])
                if (const Carrier *carrier = move_target(slab_idx, node))
                    tentative_moves.emplace(node, carrier->top_layer);

        NodeCounters future_counters = evaluate_roof_layers(tentative_moves);
        auto future_of = [&](SupportNode *node) {
            const auto it = future_counters.find(node);
            return it == future_counters.end() ?
                std::make_pair(node->distance_to_top, node->support_roof_layers_below) : it->second;
        };
        auto use_interface_shape = [&](const SupportNode *node, int roof_layers) {
            return m_support_params.num_top_interface_layers > 0 &&
                   (roof_layers > 1 || (roof_layers >= 0 && ! node->is_sharp_tail));
        };

        // Every loop follows a split or refusal, so the radius groups always match the current destinations.
        evaluate_folded_radius(tentative_moves);
        for (size_t slab_idx = 0; slab_idx < plan.slabs.size(); ++ slab_idx)
            for (auto &entry : carriers[slab_idx]) {
                Carrier &carrier = entry.second;
                carrier.footprint.clear();
                carrier.base_footprint.clear();
                carrier.interface_footprint.clear();
                carrier.footprint_bbox = {};
                carrier.base_footprint_bbox = {};
            }
        for (size_t slab_idx = 0; slab_idx < plan.slabs.size(); ++ slab_idx)
            for (size_t layer_nr : layers_per_slab[slab_idx])
                for (SupportNode *node : contact_nodes[layer_nr]) {
                    const auto move_it = tentative_moves.find(node);
                    if (move_it == tentative_moves.end())
                        continue;
                    Carrier &carrier = carriers[slab_idx].at(move_it->second);
                    const std::pair<int, int> counters = future_of(node);
                    const int roof_layers = counters.second;
                    const bool interface_node = is_interface_role(counters, carrier.top_layer);
                    if (! carrier.folds || (! interface_node && layer_nr != carrier.base_source_layer))
                        continue;
                    ExPolygons final_base_probe;
                    const ExPolygons *probe = nullptr;
                    if (interface_node)
                        probe = &cached_probe_of(node, use_interface_shape(node, roof_layers));
                    else {
                        const auto radius_it = folded_radius.find(node);
                        probe_of(node, false, final_base_probe,
                                 radius_it == folded_radius.end() ? node->radius : radius_it->second);
                        probe = &final_base_probe;
                    }
                    if (probe->empty())
                        continue;
                    if (interface_node)
                        append(carrier.interface_footprint, *probe);
                    else
                        append(carrier.base_footprint, *probe);
                }
        for (size_t slab_idx = 0; slab_idx < plan.slabs.size(); ++ slab_idx)
            for (auto &entry : carriers[slab_idx]) {
                Carrier &carrier = entry.second;
                if (! carrier.folds)
                    continue;
                if (! carrier.base_footprint.empty()) {
                    remove_duplicate_expolygons(carrier.base_footprint);
                    carrier.base_footprint = union_ex(carrier.base_footprint);
                    carrier.base_footprint_bbox = get_extents(carrier.base_footprint);
                    append(carrier.footprint, carrier.base_footprint);
                }
                if (! carrier.interface_footprint.empty()) {
                    remove_duplicate_expolygons(carrier.interface_footprint);
                    carrier.interface_footprint = union_ex(carrier.interface_footprint);
                    append(carrier.footprint, carrier.interface_footprint);
                }
                if (! carrier.footprint.empty())
                    carrier.footprint_bbox = get_extents(carrier.footprint);
            }

        std::vector<Carrier *> folded_spans;
        for (auto &slab_carriers : carriers)
            for (auto &entry : slab_carriers)
                if (entry.second.folds)
                    folded_spans.push_back(&entry.second);
        std::sort(folded_spans.begin(), folded_spans.end(),
                  [](const Carrier *lhs, const Carrier *rhs) {
                      if (lhs->bottom_z != rhs->bottom_z)
                          return lhs->bottom_z < rhs->bottom_z;
                      if (lhs->slab_idx != rhs->slab_idx)
                          return lhs->slab_idx < rhs->slab_idx;
                      return lhs->top_layer < rhs->top_layer;
                  });

        // Select every split from this immutable footprint/span snapshot. Applying a split while scanning
        // would make later pairs depend on sort order and could leave an old partial-overlap refusal behind.
        struct SplitDecision {
            Carrier *highest_inner { nullptr };
            bool     required { false };
        };
        std::unordered_map<Carrier *, SplitDecision> split_candidates;
        for (size_t lhs = 0; lhs < folded_spans.size(); ++ lhs) {
            Carrier *lhs_carrier = folded_spans[lhs];
            for (size_t rhs = lhs + 1; rhs < folded_spans.size(); ++ rhs) {
                Carrier *rhs_carrier = folded_spans[rhs];
                if (rhs_carrier->bottom_z >= lhs_carrier->top_z - EPSILON)
                    break;
                const bool lhs_contains_rhs = contains_span(*lhs_carrier, *rhs_carrier);
                const bool rhs_contains_lhs = contains_span(*rhs_carrier, *lhs_carrier);
                if (lhs_contains_rhs == rhs_contains_lhs)
                    continue;

                Carrier &outer = lhs_contains_rhs ? *lhs_carrier : *rhs_carrier;
                Carrier &inner = lhs_contains_rhs ? *rhs_carrier : *lhs_carrier;
                if (outer.slab_idx != inner.slab_idx ||
                    inner.top_z <= outer.bottom_z + EPSILON)
                    continue;

                // Interface-over-base creates the need to split. Once a split is needed, use the highest
                // overlapping inner boundary so the shortened outer cannot partially overlap another inner.
                // A base-only overlap does not trigger a split by itself; final geometry trimming handles it.
                SplitDecision &decision = split_candidates[&outer];
                const Carrier *best = decision.highest_inner;
                const bool higher = best == nullptr || inner.top_z > best->top_z ||
                    (inner.top_z == best->top_z && inner.top_layer > best->top_layer);
                // Interface-over-base implies a full-footprint overlap. Once a split is required, a lower inner
                // cannot change the decision, so skip both polygon tests.
                if ((! higher && decision.required) ||
                    ! footprints_overlap(inner.footprint, inner.footprint_bbox,
                                         outer.footprint, outer.footprint_bbox))
                    continue;
                decision.required = decision.required ||
                    footprints_overlap(inner.interface_footprint, inner.footprint_bbox,
                                       outer.base_footprint, outer.base_footprint_bbox);
                if (higher)
                    decision.highest_inner = &inner;
            }
        }

        std::unordered_set<Carrier *> split_misaligned;
        bool carriers_shortened = false;
        for (const auto &[outer_ptr, decision] : split_candidates) {
            if (! decision.required)
                continue;
            Carrier &outer = *outer_ptr;
            Carrier &inner = *decision.highest_inner;

            outer.bottom_z          = inner.top_z;
            outer.base_source_layer = outer.top_layer;
            outer.lower_carrier     = &inner;
            outer.slab_height       = outer.top_z - outer.bottom_z;
            const auto first_layer_above = std::upper_bound(
                layer_heights.begin(), layer_heights.begin() + num_layers, outer.bottom_z + EPSILON,
                [](coordf_t z, const LayerHeightData &layer) { return z < layer.print_z; });
            outer.bottom_obj_layer = first_layer_above == layer_heights.begin() + num_layers ?
                outer.obj_layer : first_layer_above->obj_layer_nr;
            carriers_shortened = true;
            if (outer.slab_height <= layer_heights[outer.top_layer].height + EPSILON ||
                ! adaptive_layer_height_range(plan.slabs[outer.slab_idx].nozzle_diameter)
                      .contains(outer.slab_height))
                split_misaligned.insert(&outer);
        }

        if (carriers_shortened) {
            if (! split_misaligned.empty())
                refuse_carriers(split_misaligned);
            continue;
        }

        std::vector<ExPolygons> stationary_footprints(num_layers);
        std::vector<ExPolygons> stationary_interface_footprints(num_layers);
        std::vector<BoundingBox> stationary_bboxes(num_layers);
        std::vector<bool> stationary_ready(num_layers, false);
        auto ensure_stationary = [&](size_t layer_nr) {
            if (stationary_ready[layer_nr])
                return;
            for (SupportNode *node : contact_nodes[layer_nr])
                if (tentative_moves.count(node) == 0) {
                    const std::pair<int, int> counters = future_of(node);
                    const ExPolygons &probe =
                        cached_probe_of(node, use_interface_shape(node, counters.second));
                    append(stationary_footprints[layer_nr], probe);
                    if (is_interface_role(counters, layer_nr))
                        append(stationary_interface_footprints[layer_nr], probe);
                }
            remove_duplicate_expolygons(stationary_footprints[layer_nr]);
            remove_duplicate_expolygons(stationary_interface_footprints[layer_nr]);
            if (! stationary_footprints[layer_nr].empty())
                stationary_bboxes[layer_nr] =
                    get_extents(stationary_footprints[layer_nr]);
            stationary_ready[layer_nr] = true;
        };

        // No carrier changed after the snapshot above, so overlap refusals now describe the current spans.
        // Same-slab containment was either split at the highest valid boundary or rejected as unprintable.
        std::unordered_set<Carrier *> newly_misaligned;
        for (size_t lhs = 0; lhs < folded_spans.size(); ++ lhs) {
            Carrier *lhs_carrier = folded_spans[lhs];
            if (! lhs_carrier->folds || newly_misaligned.count(lhs_carrier) > 0)
                continue;
            for (size_t rhs = lhs + 1; rhs < folded_spans.size(); ++ rhs) {
                Carrier *rhs_carrier = folded_spans[rhs];
                if (rhs_carrier->bottom_z >= lhs_carrier->top_z - EPSILON)
                    break;
                if (! rhs_carrier->folds || newly_misaligned.count(rhs_carrier) > 0)
                    continue;
                const bool lhs_contains_rhs = contains_span(*lhs_carrier, *rhs_carrier);
                const bool rhs_contains_lhs = contains_span(*rhs_carrier, *lhs_carrier);
                if (! lhs_contains_rhs && ! rhs_contains_lhs) {
                    if (! footprints_overlap(lhs_carrier->footprint, lhs_carrier->footprint_bbox,
                                             rhs_carrier->footprint, rhs_carrier->footprint_bbox))
                        continue;
                    const coordf_t lhs_height = lhs_carrier->slab_height;
                    const coordf_t rhs_height = rhs_carrier->slab_height;
                    Carrier *rejected = lhs_height + EPSILON < rhs_height ? lhs_carrier :
                        rhs_height + EPSILON < lhs_height ? rhs_carrier :
                        lhs_carrier->top_z < rhs_carrier->top_z ? lhs_carrier : rhs_carrier;
                    newly_misaligned.insert(rejected);
                    if (rejected == lhs_carrier)
                        break;
                    continue;
                }
                if (lhs_contains_rhs && rhs_contains_lhs)
                    continue;

                Carrier &outer = lhs_contains_rhs ? *lhs_carrier : *rhs_carrier;
                Carrier &inner = lhs_contains_rhs ? *rhs_carrier : *lhs_carrier;
                if (outer.slab_idx != inner.slab_idx &&
                    inner.top_z > outer.bottom_z + EPSILON &&
                    footprints_overlap(inner.interface_footprint, inner.footprint_bbox,
                                       outer.base_footprint, outer.base_footprint_bbox)) {
                    newly_misaligned.insert(&outer);
                    if (&outer == lhs_carrier)
                        break;
                }
            }
        }

        // Only material left on its original fine layer can conflict with a folded span here. Nodes assigned
        // to another surviving carrier are covered by the carrier-to-carrier check above.
        for (Carrier *span : folded_spans) {
            Carrier &carrier = *span;
            if (! carrier.folds || carrier.footprint.empty())
                continue;
            const auto first_layer = std::lower_bound(
                layer_heights.begin(), layer_heights.begin() + num_layers, carrier.bottom_z + EPSILON,
                [](const LayerHeightData &layer, coordf_t z) { return layer.print_z < z; });
            for (size_t layer_nr = size_t(first_layer - layer_heights.begin());
                 layer_nr < num_layers; ++ layer_nr) {
                if (layer_heights[layer_nr].height < EPSILON)
                    continue;
                const coordf_t layer_top    = layer_heights[layer_nr].print_z;
                const coordf_t layer_bottom = layer_top - layer_heights[layer_nr].height;
                if (layer_top <= carrier.bottom_z + EPSILON)
                    continue;
                if (layer_bottom >= carrier.top_z - EPSILON)
                    break;
                const bool carrier_contains_layer =
                    carrier.bottom_z <= layer_bottom + EPSILON &&
                    carrier.top_z >= layer_top - EPSILON;
                const bool layer_contains_carrier =
                    layer_bottom <= carrier.bottom_z + EPSILON &&
                    layer_top >= carrier.top_z - EPSILON;
                const bool partial_z = ! carrier_contains_layer && ! layer_contains_carrier;

                ensure_stationary(layer_nr);
                const ExPolygons &stationary = stationary_footprints[layer_nr];
                if (stationary.empty())
                    continue;
                if (partial_z) {
                    if (! footprints_overlap(stationary, stationary_bboxes[layer_nr],
                                             carrier.footprint, carrier.footprint_bbox))
                        continue;
                    newly_misaligned.insert(&carrier);
                    break;
                }

                ExPolygons uncovered_interface_overlap;
                if (! stationary_interface_footprints[layer_nr].empty() &&
                    ! carrier.base_footprint.empty() &&
                    stationary_bboxes[layer_nr].overlap(carrier.base_footprint_bbox)) {
                    ExPolygons overlap = intersection_ex(stationary_interface_footprints[layer_nr],
                                                         carrier.base_footprint);
                    if (! overlap.empty())
                        uncovered_interface_overlap = diff_ex(overlap, carrier.interface_footprint);
                }
                if (! carrier_contains_layer || ! uncovered_interface_overlap.empty()) {
                    if (footprints_overlap(stationary, stationary_bboxes[layer_nr],
                                           carrier.base_footprint,
                                           carrier.base_footprint_bbox)) {
                        newly_misaligned.insert(&carrier);
                        break;
                    }
                    continue;
                }
            }
        }
        if (newly_misaligned.empty()) {
            moves_to      = std::move(tentative_moves);
            final_counters = std::move(future_counters);
            break;
        }
        refuse_carriers(newly_misaligned);
    }
    if (misaligned_carriers > 0)
        BOOST_LOG_TRIVIAL(warning) << "refused " << misaligned_carriers
                                   << " folded support carriers with unsafe Z overlap";

    // 第二轮：第一轮被拒的合并段，只把贯穿整段的支撑主体合并到段顶，接触面/转换层保持细层。
    // 角色沿用第一轮 final_counters；整段合并只会完全覆盖段内细层，重叠由裁剪处理。
    struct BaseFold { size_t slab_idx; std::vector<std::pair<SupportNode *, coordf_t>> tops; };
    std::vector<BaseFold>                   base_folds;
    std::unordered_set<const SupportNode *> base_folded; // 被并入第二轮合并段的节点（含段顶）
    {
        auto counters_of = [&](const SupportNode *node) {
            const auto it = final_counters.find(const_cast<SupportNode *>(node));
            return it == final_counters.end() ? std::make_pair(node->distance_to_top, node->support_roof_layers_below) : it->second;
        };
        auto unmoved = [&](const SupportNode *node) {
            return node != nullptr && node->distance_to_top >= 0 && moves_to.count(node) == 0;
        };
        auto is_left_base = [&](const SupportNode *node, size_t layer_nr) {
            return foldable(node) && moves_to.count(node) == 0 && ! is_interface_role(counters_of(node), layer_nr);
        };
        // 段顶允许留下的细层接触面；转换层由主体材料挤出，仅当接触面也指定为本段材料时才能留在细层。
        auto stays_roof = [&](const SupportNode *node, bool keep_transition) {
            const auto [dist, roof] = counters_of(node);
            const int  if_layers  = int(m_support_params.num_top_interface_layers);
            const bool transition = (roof == 0 || roof == 1) && dist >= if_layers;
            return if_layers > 0 && (transition ? keep_transition : roof > 0);
        };
        std::unordered_set<const SupportNode *> members, fine, reaches;
        for (size_t slab_idx = 0; slab_idx < plan.slabs.size(); ++ slab_idx) {
            const std::vector<size_t>    &layers = layers_per_slab[slab_idx];
            const SupportMergePlan::Slab &slab   = plan.slabs[slab_idx];
            if (layers.size() < 2 || (slab.bottom_anchor && m_raft_layers == 0))
                continue;
            const size_t top = layers.back(), bottom = layers.front();
            if (const auto it = carriers[slab_idx].find(top); it != carriers[slab_idx].end() && it->second.folds)
                continue;
            const coordf_t slab_height = layer_heights[top].print_z - layer_heights[bottom].print_z + layer_heights[bottom].height;
            const bool keep_transition = explicit_interface_filament == slab.filament_id;
            if (! adaptive_layer_height_range(slab.nozzle_diameter).contains(slab_height) ||
                std::any_of(contact_nodes[top].begin(), contact_nodes[top].end(), [&](const SupportNode *node) {
                    return unmoved(node) && ! is_left_base(node, top) && ! stays_roof(node, keep_transition);
                }))
                continue;
            // 自上而下：剩余主体都是成员；父节点不全是非 fine 成员（接触面下方、段内新分支）的标为 fine，保持细层托住接触面。
            members.clear();
            fine.clear();
            for (auto it = layers.rbegin(); it != layers.rend(); ++ it)
                for (const SupportNode *node : contact_nodes[*it])
                    if (is_left_base(node, *it)) {
                        bool keep = *it != top && node->parent == nullptr && node->parents.empty();
                        if (*it != top)
                            for_each_parent(node, [&](const SupportNode *p) { keep = keep || ! members.count(p) || fine.count(p); });
                        if (keep)
                            fine.insert(node);
                        members.insert(node);
                    }
            if (members.empty())
                continue;
            // 自下而上：只有经主体节点连到段底的支撑柱才能合并，否则合并层会悬空或被接触面裁空（R1）。
            reaches.clear();
            for (size_t layer_nr : layers)
                for (const SupportNode *node : contact_nodes[layer_nr])
                    if (members.count(node) && (layer_nr == bottom || (node->child != nullptr && reaches.count(node->child))))
                        reaches.insert(node);
            // 段顶的非主体节点已在上面检查过；段顶主体必须都能经主体连到段底，否则放弃整段（R3）。
            BaseFold fold { slab_idx, {} };
            bool     ok = true;
            for (SupportNode *node : contact_nodes[top]) {
                if (! members.count(node))
                    continue;
                if (! (ok = reaches.count(node) > 0))
                    break;
                coordf_t radius = node->radius;
                for (const SupportNode *n = node->child; n != nullptr && members.count(n); n = n->child)
                    radius = std::max(radius, coordf_t(n->radius));
                fold.tops.emplace_back(node, radius);
            }
            if (! ok || fold.tops.empty())
                continue;
            // 与第一轮相同：整段被模型挡住、没有任何截面能留下时不合并。
            ExPolygons span_collision;
            for (size_t l = layer_heights[bottom].obj_layer_nr; l < layer_heights[top].obj_layer_nr && l < m_ts_data->m_layer_outlines.size(); ++ l)
                append(span_collision, offset_ex(m_ts_data->m_layer_outlines[l], float(xy_distance_scaled)));
            if (! span_collision.empty()) {
                span_collision = union_ex(span_collision);
                if (std::none_of(fold.tops.begin(), fold.tops.end(), [&](const auto &top_node) {
                        ExPolygons probe;
                        probe_of(top_node.first, false, probe, top_node.second);
                        return probe.empty() || ! diff_ex(probe, span_collision).empty();
                    }))
                    continue;
            }
            for (const SupportNode *node : reaches)
                if (members.count(node) && ! fine.count(node))
                    base_folded.insert(node);
            base_folds.emplace_back(std::move(fold));
        }
    }

    // A rejected carrier keeps its original fine-grid geometry. Pin that material to the finest candidate
    // that can print the fine layer, otherwise ToolOrdering may select the coarse support nozzle even though
    // no coarse slab was emitted there.
    std::vector<bool> fine_base_layers(num_layers, false);
    std::vector<bool> fine_interface_layers(num_layers, false);
    for (size_t slab_idx = 0; slab_idx < plan.slabs.size(); ++ slab_idx)
        for (size_t layer_nr : layers_per_slab[slab_idx])
            for (SupportNode *node : contact_nodes[layer_nr]) {
                const auto top_it = column_top[slab_idx].find(node);
                if (top_it == column_top[slab_idx].end() ||
                    moves_to.count(node) > 0 || base_folded.count(node) > 0)
                    continue;
                const auto counter_it = final_counters.find(node);
                const std::pair<int, int> counters = counter_it == final_counters.end() ?
                    std::make_pair(node->distance_to_top, node->support_roof_layers_below) : counter_it->second;
                const bool interface_node = is_interface_role(counters, layer_nr);
                if (interface_node)
                    fine_interface_layers[layer_nr] = true;
                else
                    fine_base_layers[layer_nr] = true;
            }
    auto finest_candidate_for = [](const std::vector<SupportMergePlan::Candidate> &candidates,
                                   coordf_t height) {
        SupportMergePlan::Candidate best;
        for (const SupportMergePlan::Candidate &candidate : candidates)
            if (candidate.window.contains(height) &&
                (! best.valid() || candidate.nozzle_diameter < best.nozzle_diameter))
                best = candidate;
        return best;
    };
    for (size_t layer_nr = 0; layer_nr < num_layers; ++ layer_nr) {
        if (! fine_base_layers[layer_nr] && ! fine_interface_layers[layer_nr])
            continue;
        SupportLayer *ts_layer = m_object->get_support_layer(layer_nr + m_raft_layers);
        if (ts_layer == nullptr)
            continue;
        const LayerHeightData &layer = layer_heights[layer_nr];
        if (fine_base_layers[layer_nr]) {
            const SupportMergePlan::Candidate candidate =
                finest_candidate_for(plan.candidates_for_slab(layer.print_z, layer.height), layer.height);
            if (candidate.valid()) {
                ts_layer->planned_filament_id     = candidate.filament_id;
                ts_layer->planned_nozzle_diameter = candidate.nozzle_diameter;
            }
        }
        if (fine_interface_layers[layer_nr]) {
            const SupportMergePlan::Candidate candidate =
                plan.finest_interface_for_slab(layer.print_z, layer.height);
            if (candidate.valid()) {
                ts_layer->planned_interface_filament_id     = candidate.filament_id;
                ts_layer->planned_interface_nozzle_diameter = candidate.nozzle_diameter;
            }
        }
    }
    if (moves_to.empty() && base_folds.empty()) {
        BOOST_LOG_TRIVIAL(info) << "nothing folded of " << plan.slabs.size() << " slabs";
        return;
    }

    // No floor guard here. The area level merge refused an interface slab whose nozzle could not also
    // extrude a single layer, but only on a layer that actually carried floor areas. Those do not exist yet
    // at this point, and approximating the test from Z alone turns it into "a coarse nozzle may never fold
    // an interface over a fine grid" - which is precisely the configuration this whole path exists for, so
    // it silently disabled the feature. A carrier that ends up with floor areas extrudes them at the layer
    // height through floor_flow, which is the same compromise the unfolded path already makes.

    // Pass 2: apply the counters evaluated before folding while every node is still on its planned layer.
    for (const auto &[node, counters] : final_counters) {
        node->distance_to_top           = counters.first;
        node->support_roof_layers_below = counters.second;
    }

    // Pass 3: fold.
    size_t folded_carriers = 0;
    m_fold_slab_of_layer.assign(num_layers, -1);
    for (size_t slab_idx = 0; slab_idx < plan.slabs.size(); ++ slab_idx) {
        const SupportMergePlan::Slab &slab   = plan.slabs[slab_idx];
        const std::vector<size_t>    &layers = layers_per_slab[slab_idx];

        // Per carrier: the base cross-section it takes, and every interface node of its columns.
        std::map<size_t, std::pair<std::vector<SupportNode *>, std::vector<SupportNode *>>> adopted;
        for (size_t layer_nr : layers) {
            std::vector<SupportNode *> stay;
            stay.reserve(contact_nodes[layer_nr].size());
            for (SupportNode *node : contact_nodes[layer_nr]) {
                if (node == nullptr)
                    continue;
                // Virtual gap nodes, an interface left on the fine grid and the columns whose
                // carrier refused keep their own layer. Polygon nodes do fold: they render a different shape
                // but occupy the slab the same way a disk does.
                const auto move_it = moves_to.find(node);
                if (move_it == moves_to.end()) {
                    stay.emplace_back(node);
                    continue;
                }
                const size_t   carrier_layer = move_it->second;
                const Carrier &carrier       = carriers[slab_idx].at(carrier_layer);
                if (is_interface_role({ node->distance_to_top, node->support_roof_layers_below }, carrier_layer)) {
                    // Every roof in the column contributes: one extrusion covers all of them.
                    adopted[carrier_layer].second.emplace_back(node);
                } else if (layer_nr == carrier.base_source_layer) {
                    // A thick extrusion has one XY cross-section over its whole height. Unioning every layer
                    // would turn lateral branch movement into a swept wedge and distort both the tree shape
                    // and its clearance, so only the representative face is kept.
                    adopted[carrier_layer].first.emplace_back(node);
                }
                // Anything else is swallowed by its column's slab. The node stays alive in TreeSupportData,
                // it simply no longer renders a disk of its own.
            }
            contact_nodes[layer_nr] = std::move(stay);
        }
        if (adopted.empty())
            continue;

        std::vector<FoldCarrier> &slab_carriers = m_fold_carriers.emplace_back();
        for (auto &[carrier_layer, nodes] : adopted) {
            const Carrier &carrier = carriers[slab_idx].at(carrier_layer);
            for (std::vector<SupportNode *> *group : { &nodes.first, &nodes.second })
                for (SupportNode *node : *group) {
                    if (group == &nodes.first)
                        node->radius = folded_radius.at(node);
                    node->slab_height           = carrier.slab_height;
                    node->slab_bottom_obj_layer = carrier.bottom_obj_layer;
                    node->obj_layer_nr          = int(carrier.obj_layer);
                    node->print_z               = carrier.top_z;
                    node->height                = carrier.slab_height;
                    contact_nodes[carrier_layer].emplace_back(node);
                }
            slab_carriers.push_back({ carrier_layer, carrier.bottom_z });
            ++ folded_carriers;

            SupportLayer *ts_layer = m_object->get_support_layer(carrier_layer + m_raft_layers);
            if (ts_layer == nullptr)
                continue;
            if (! nodes.first.empty()) {
                ts_layer->base_slab_height = carrier.slab_height;
                // Pin the slab to its filament only once it got thicker than the object layers: a slab of base
                // height can be extruded by any nozzle, and leaving it alone lets the exporter minimize tool
                // changes.
                if (carrier.slab_height > plan.base_height_at(carrier.top_z) + EPSILON) {
                    ts_layer->planned_filament_id     = slab.filament_id;
                    ts_layer->planned_nozzle_diameter = slab.nozzle_diameter;
                }
            }
            if (! nodes.second.empty()) {
                ts_layer->interface_slab_height = carrier.slab_height;
                // An explicitly pinned interface filament keeps the tool: the plan only allowed the fold
                // because that filament can extrude this slab in the first place.
                ts_layer->planned_interface_filament_id =
                    explicit_interface_filament > 0 ? explicit_interface_filament : slab.filament_id;
                ts_layer->planned_interface_nozzle_diameter = slab.nozzle_diameter;
            }
        }
        for (size_t layer_nr : layers)
            m_fold_slab_of_layer[layer_nr] = int(m_fold_carriers.size() - 1);
    }
    // Later stages gate their folded-only behaviour on these being non-empty.
    if (folded_carriers == 0) {
        m_fold_carriers.clear();
        m_fold_slab_of_layer.clear();
    }

    // 应用第二轮：被并入的节点从细层移除，段顶节点扩成整段高度。
    if (! base_folds.empty() && m_fold_slab_of_layer.empty())
        m_fold_slab_of_layer.assign(num_layers, -1);
    for (const BaseFold &fold : base_folds) {
        const SupportMergePlan::Slab &slab   = plan.slabs[fold.slab_idx];
        const std::vector<size_t>    &layers = layers_per_slab[fold.slab_idx];
        const size_t   top      = layers.back();
        const coordf_t bottom_z = layer_heights[layers.front()].print_z - layer_heights[layers.front()].height;
        const coordf_t height   = layer_heights[top].print_z - bottom_z;
        for (size_t layer_nr : layers)
            if (layer_nr != top)
                contact_nodes[layer_nr].erase(std::remove_if(contact_nodes[layer_nr].begin(), contact_nodes[layer_nr].end(),
                    [&](const SupportNode *node) { return base_folded.count(node) > 0; }), contact_nodes[layer_nr].end());
        for (const auto &[node, radius] : fold.tops) {
            node->radius                = radius;
            node->height = node->slab_height = height;
            node->slab_bottom_obj_layer = layer_heights[layers.front()].obj_layer_nr;
        }
        if (SupportLayer *ts_layer = m_object->get_support_layer(top + m_raft_layers)) {
            ts_layer->base_slab_height        = height;
            ts_layer->planned_filament_id     = slab.filament_id;
            ts_layer->planned_nozzle_diameter = slab.nozzle_diameter;
        }
        int &slab_ref = m_fold_slab_of_layer[top];
        if (slab_ref < 0) {
            m_fold_carriers.emplace_back();
            for (size_t layer_nr : layers)
                m_fold_slab_of_layer[layer_nr] = int(m_fold_carriers.size() - 1);
        }
        m_fold_carriers[size_t(slab_ref)].push_back({ top, bottom_z });
        m_base_only_fold_layers.emplace_back(top); // slab 按 Z 升序，结果天然有序
    }
    folded_carriers += base_folds.size();

    // Pass 4: printed layer index for the interface pattern. Consecutive printed layers have to alternate,
    // and a folded stack prints several object layers apart, so count the layers that still print.
    if (fold_interface && folded_carriers > 0) {
        int printed = 0;
        for (size_t layer_nr = 0; layer_nr < num_layers; ++ layer_nr) {
            if (layer_heights[layer_nr].height < EPSILON || contact_nodes[layer_nr].empty())
                continue;
            for (SupportNode *node : contact_nodes[layer_nr])
                if (node != nullptr && node->distance_to_top >= 0)
                    node->slab_index = printed;
            ++ printed;
        }
    }

    BOOST_LOG_TRIVIAL(info) << "folded " << folded_carriers << " column carriers over "
                            << m_fold_carriers.size() << " of " << plan.slabs.size() << " slabs, interface fold "
                            << (fold_interface ? "on" : "off") << ", base only " << base_folds.size();
}

// BBS multi-diameter: a fine branch section sitting next to a coarse slab only overlaps the coarse wall over its
// own narrow bead, so the joint is weak. Widen the wall of the fine side close to every nozzle change, ramping
// back to the default width within a short distance. Only base disks that draw_circles() actually renders take
// part, a node away from any change keeps line_width == 0, and nothing runs in single-diameter mode.
void TreeSupport::smooth_line_width_multi_diameter()
{
    // Lightning roots are trimmed against the default wall, so a wider wall there would overlap them.
    if (! m_support_merge_plan.enabled || m_support_params.base_fill_pattern == ipLightning)
        return;

    const std::vector<LayerHeightData> &layer_heights = m_ts_data->layer_heights;
    const size_t num_layers = std::min(contact_nodes.size(), layer_heights.size());
    const bool   has_roof   = m_support_params.num_top_interface_layers > 0;
    // Same classification as draw_circles(): what ends up as a plain base disk.
    auto is_base_disk = [has_roof](const SupportNode *node) {
        return node != nullptr && ! node->fading && node->type != ePolygon &&
               node->distance_to_top >= 0 && (! has_roof || node->support_roof_layers_below < 0);
    };

    // width: default base width of the node's layer; target: the widened width, raised pairwise below.
    struct Disk { coordf_t width; coordf_t top; coordf_t bottom; coordf_t target; };
    std::unordered_map<SupportNode *, Disk> disks;
    coordf_t min_width = std::numeric_limits<coordf_t>::max();
    coordf_t max_width = 0.;
    coordf_t max_span  = 0.;
    for (size_t layer_nr = 0; layer_nr < num_layers; ++ layer_nr) {
        if (layer_heights[layer_nr].height < EPSILON || contact_nodes[layer_nr].empty())
            continue;
        // Default base width of this layer, resolved the way generate_toolpaths() does.
        const SupportLayer *ts_layer = m_object->get_support_layer(layer_nr + m_raft_layers);
        const coordf_t width = ts_layer != nullptr && ts_layer->planned_nozzle_diameter > EPSILON ?
            m_support_params.support_material_flow_for_nozzle(ts_layer->planned_nozzle_diameter,
                                                              float(layer_heights[layer_nr].height)).width() :
            m_support_params.support_extrusion_width;
        for (SupportNode *node : contact_nodes[layer_nr]) {
            if (! is_base_disk(node) || node->height < EPSILON)
                continue;
            disks.emplace(node, Disk{ width, node->print_z, node->print_z - node->height, width });
            min_width = std::min(min_width, width);
            max_width = std::max(max_width, width);
            max_span  = std::max(max_span, node->height);
        }
    }
    // No nozzle change anywhere: every node keeps its default width.
    if (disks.empty() || max_width - min_width < EPSILON)
        return;

    // The finer disk of a pair ramps from its capped width (the coarse width, at most twice its own) at contact
    // back to its own over max(coarse slab, two own layers) plus one own layer, so no partner farther than this
    // matters.
    const coordf_t reach = 3. * max_span;
    // Every node has a single child towards the bed, so walking down from each disk meets every pair of
    // rendered disks on one chain exactly once. Nodes swallowed by a fold are walked through: they only
    // bridge the Z gap between the disks that remain.
    for (auto &[node, upper] : disks)
        for (SupportNode *c = node->child; c != nullptr && upper.bottom - c->print_z <= reach; c = c->child) {
            const auto it = disks.find(c);
            if (it == disks.end())
                continue;
            Disk &lower = it->second;
            if (std::abs(upper.width - lower.width) < EPSILON)
                continue;
            Disk          &fine   = upper.width < lower.width ? upper : lower;
            const Disk    &coarse = upper.width < lower.width ? lower : upper;
            // Ramp towards the capped peak rather than the coarse width, so the cap does not flatten the layers
            // next to the change. One own layer is added so the last widened layer does not snap back.
            const coordf_t fine_h = fine.top - fine.bottom;
            const coordf_t peak   = std::min(coarse.width, 2. * fine.width);
            const coordf_t ramp   = std::max(coarse.top - coarse.bottom, 2. * fine_h) + fine_h;
            const coordf_t gap    = std::max(0., upper.bottom - lower.top);
            if (gap < ramp)
                fine.target = std::max(fine.target, fine.width + (peak - fine.width) * (1. - gap / ramp));
        }

    size_t widened = 0;
    for (auto &[node, disk] : disks) {
        // The target is already capped at twice the own width by the peak. It is also kept within the disk radius,
        // or the wall inset in tree_supports_generate_paths() would eat a thin branch away.
        const coordf_t width = std::min(disk.target, std::max(disk.width, coordf_t(node->radius)));
        if (width > disk.width + EPSILON) {
            node->line_width = width;
            ++ widened;
        }
    }
    BOOST_LOG_TRIVIAL(info) << "support line width smoothing widened " << widened << " of " << disks.size()
                            << " base nodes, default widths " << min_width << " to " << max_width;
}

std::vector<size_t> TreeSupport::fold_carriers_above(size_t layer_nr) const
{
    std::vector<size_t> carriers;
    if (layer_nr >= m_fold_slab_of_layer.size() || m_fold_slab_of_layer[layer_nr] < 0)
        return carriers;
    const coordf_t layer_top = m_ts_data->layer_heights[layer_nr].print_z;
    for (const FoldCarrier &carrier : m_fold_carriers[size_t(m_fold_slab_of_layer[layer_nr])])
        if (carrier.layer > layer_nr && layer_top > carrier.bottom_z + EPSILON)
            carriers.emplace_back(carrier.layer);
    return carriers;
}

void TreeSupport::generate_toolpaths()
{
    const PrintObjectConfig &object_config = m_object->config();
    coordf_t support_extrusion_width = m_support_params.support_extrusion_width;
    coordf_t nozzle_diameter = support_nozzle_diameter(m_object, object_config.support_filament);
    coordf_t layer_height = object_config.layer_height.value;
    const int wall_count = object_config.tree_support_wall_count.value;
    // Bridge angle of the object bridge resting right above the support layer at print_z. A per-nozzle gap is
    // rounded up to whole object layers by generate_contact_points(), so it does not land on a fixed Z: search
    // up to the largest gap plus one layer instead.
    auto bridge_angle_on = [](const Layer *obj_layer, const ExPolygons &polys, double &angle) {
        if (obj_layer == nullptr)
            return false;
        for (const LayerRegion *layerm : obj_layer->regions())
            for (const Surface &surface : layerm->fill_surfaces.surfaces)
                if (surface.surface_type == stBottomBridge && overlaps(polys, surface.expolygon)) {
                    angle = surface.bridge_angle;
                    return true;
                }
        return false;
    };
    auto bridge_angle_above = [&](coordf_t print_z, const ExPolygons &polys, double &angle) {
        if (m_top_z_outlines.empty())
            return bridge_angle_on(m_object->get_layer_at_bottomz(print_z + object_config.support_top_z_distance.value, layer_height / 10.), polys, angle);
        const coordf_t z_max = print_z + m_top_z_max + m_slicing_params.max_layer_height + EPSILON;
        const auto     first = std::upper_bound(m_object->layers().begin(), m_object->layers().end(), print_z - EPSILON,
                                                [](coordf_t z, const Layer *layer) { return z < layer->bottom_z(); });
        for (auto it = first; it != m_object->layers().end() && (*it)->bottom_z() <= z_max; ++ it)
            if (bridge_angle_on(*it, polys, angle))
                return true;
        return false;
    };

    // coconut: use same intensity settings as SupportMaterial.cpp
    auto m_support_material_interface_flow = support_material_interface_flow(m_object, float(m_slicing_params.layer_height));
    coordf_t interface_spacing = object_config.support_interface_spacing.value + m_support_material_interface_flow.spacing();
    coordf_t interface_density = std::min(1., m_support_material_interface_flow.spacing() / interface_spacing);

    const coordf_t branch_radius = object_config.tree_support_branch_diameter.value / 2;
    const coordf_t branch_radius_scaled = scale_(branch_radius);

    if (m_object->support_layers().empty())
        return;

    auto generate_support_ironing_entity = [this](const ExPolygons &unioned_expolygons,
                                           const BoundingBox &bbox_object,
                                           const SupportLayer *ts_layer,
                                           ExtrusionEntityCollection *temp_entity_collection,
                                           const Flow &interface_flow) {
        // support interface ironing related generation, tree support logic
        if (m_support_params.enable_support_ironing && !unioned_expolygons.empty()) {
            auto ironing_fill = std::unique_ptr<Fill>(Fill::new_from_type(m_support_params.ironing_pattern));
            ironing_fill->set_bounding_box(bbox_object);
            ironing_fill->layer_id        = ts_layer->id();
            ironing_fill->z               = ts_layer->print_z;
            ironing_fill->overlap         = 0;
            ironing_fill->angle           = m_support_params.ironing_angle;
            ironing_fill->spacing         = m_support_params.ironing_line_spacing;
            ironing_fill->link_max_length = (coord_t) scale_(3. * ironing_fill->spacing);

            ExPolygons polys_to_iron = unioned_expolygons;
            if (m_support_params.ironing_inset > EPSILON)
                polys_to_iron = union_ex(offset(polys_to_iron, -scale_(m_support_params.ironing_inset)));

            Flow       support_ironing_flow(interface_flow.width(),
                                            interface_flow.height() * m_support_params.ironing_flow_percent * 0.01,
                                            interface_flow.nozzle_diameter());
            FillParams ironing_fill_params;
            ironing_fill_params.density     = 1.0f;
            ironing_fill_params.dont_adjust = true;
            fill_expolygons_generate_paths(
                // Destination
                temp_entity_collection->entities,
                // Regions to fill
                polys_to_iron,
                // Filler and its parameters
                ironing_fill.get(), ironing_fill_params,
                // Extrusion parameters
                ExtrusionRole::erSupportIroning, support_ironing_flow);
        }
    };

    // calculate fill areas for raft layers
    ExPolygons raft_areas;
    if (m_object->layer_count() > 0) {
        const Layer *layer = m_object->layers().front();
        for (const ExPolygon &expoly : layer->lslices) {
            raft_areas.push_back(expoly);
    }
    }

    if (m_object->support_layer_count() > m_raft_layers) {
        const SupportLayer *ts_layer = m_object->get_support_layer(m_raft_layers);
        for (const ExPolygon expoly : ts_layer->floor_areas)
            raft_areas.push_back(expoly);
        for (const ExPolygon expoly : ts_layer->roof_areas)
            raft_areas.push_back(expoly);
        for (const ExPolygon expoly : ts_layer->base_areas)
            raft_areas.push_back(expoly);
    }

    raft_areas = std::move(offset_ex(raft_areas, scale_(3.)));

    size_t layer_nr = 0;
    for (; layer_nr < m_slicing_params.base_raft_layers; layer_nr++) {
        SupportLayer *ts_layer = m_object->get_support_layer(layer_nr);
        coordf_t expand_offset = (layer_nr == 0 ? m_object_config->raft_first_layer_expansion.value : 0.);
        auto raft_areas1 = offset_ex(raft_areas, scale_(expand_offset));

        Flow support_flow = Flow(support_extrusion_width, ts_layer->height, nozzle_diameter);
        Fill* filler_raft = Fill::new_from_type(ipRectilinear);
        filler_raft->angle = layer_nr == 0 ? PI/2 : 0;
        filler_raft->spacing = support_flow.spacing();

        FillParams fill_params;
        coordf_t raft_density = std::min(1., support_flow.spacing() / (object_config.support_base_pattern_spacing.value + support_flow.spacing()));
        fill_params.density = layer_nr == 0 ? object_config.raft_first_layer_density * 0.01 : raft_density;
        fill_params.dont_adjust = true;

        // wall of first layer raft
        if (layer_nr == 0) {
            Flow flow = Flow(support_extrusion_width, ts_layer->height, nozzle_diameter);
            extrusion_entities_append_loops(ts_layer->support_fills.entities, to_polygons(raft_areas1), erSupportMaterial,
                        float(flow.mm3_per_mm()), float(flow.width()), float(flow.height()));
            raft_areas1 = offset_ex(raft_areas1, -flow.scaled_spacing() / 2.);
        }
        fill_expolygons_generate_paths(ts_layer->support_fills.entities, raft_areas1,
            filler_raft, fill_params, erSupportMaterial, support_flow);
    }

    // subtract the non-raft support bases, otherwise we'll get support base on top of raft interfaces which is not stable
    ExPolygons first_non_raft_base;
    SupportLayer* first_non_raft_layer = m_object->get_support_layer(m_raft_layers);
    if (first_non_raft_layer) {
        for (auto& area_group : first_non_raft_layer->area_groups) {
            if (area_group.type == SupportLayer::BaseType)
                first_non_raft_base.emplace_back(*area_group.area);
        }
    }
    first_non_raft_base = offset_ex(first_non_raft_base, support_extrusion_width);
    ExPolygons raft_base_areas = intersection_ex(raft_areas, first_non_raft_base);
    ExPolygons raft_interface_areas = diff_ex(raft_areas, raft_base_areas);


    // raft interfaces
    for (layer_nr = m_slicing_params.base_raft_layers;
         layer_nr < m_slicing_params.base_raft_layers + m_slicing_params.interface_raft_layers;
         layer_nr++)
    {
        SupportLayer *ts_layer = m_object->get_support_layer(layer_nr);

        Flow support_flow(support_extrusion_width, ts_layer->height, nozzle_diameter);
        Fill* filler_interface = Fill::new_from_type(ipRectilinear);
        filler_interface->angle = PI / 2;  // interface should be perpendicular to base
        filler_interface->spacing = support_flow.spacing();

        FillParams fill_params;
        fill_params.density = interface_density;
        fill_params.dont_adjust = true;

        fill_expolygons_generate_paths(ts_layer->support_fills.entities, raft_interface_areas,
            filler_interface, fill_params, erSupportMaterialInterface, support_flow);

        if (m_support_params.enable_support_ironing && !raft_interface_areas.empty()
            && layer_nr == m_slicing_params.base_raft_layers + m_slicing_params.interface_raft_layers-1) {
            BoundingBox                bbox_object(Point(-scale_(1.), -scale_(1.0)), Point(scale_(1.), scale_(1.)));
            generate_support_ironing_entity(
                raft_interface_areas, bbox_object, ts_layer, &(ts_layer->support_fills), support_flow);
        }

        fill_params.density = object_config.raft_first_layer_density * 0.01;
        fill_expolygons_generate_paths(ts_layer->support_fills.entities, raft_base_areas,
            filler_interface, fill_params, erSupportMaterial, support_flow);
    }

    // layers between raft and object
    for (; layer_nr < m_raft_layers; layer_nr++) {
        SupportLayer *ts_layer = m_object->get_support_layer(layer_nr);
        Flow support_flow(support_extrusion_width, ts_layer->height, nozzle_diameter);
        Fill* filler_raft = Fill::new_from_type(ipRectilinear);
        filler_raft->angle = PI / 2;
        filler_raft->spacing = support_flow.spacing();
        for (auto& poly : first_non_raft_base)
            make_perimeter_and_infill(ts_layer->support_fills.entities, poly, std::min(1, wall_count), support_flow, erSupportMaterial, filler_raft, interface_density, false);
    }

    if (m_object->support_layer_count() <= m_raft_layers)
        return;

    // generate tree support tool paths
    tbb::parallel_for(
        tbb::blocked_range<size_t>(m_raft_layers, m_object->support_layer_count()),
        [&](const tbb::blocked_range<size_t>& range)
        {
            for (size_t layer_id = range.begin(); layer_id < range.end(); layer_id++) {
                if (m_object->print()->canceled())
                    break;

                BoundingBox           bbox_object(Point(-scale_(1.), -scale_(1.0)), Point(scale_(1.), scale_(1.)));
                std::shared_ptr<Fill> filler_interface    = std::shared_ptr<Fill>(Fill::new_from_type(m_support_params.contact_fill_pattern));
                std::shared_ptr<Fill> filler_Roof1stLayer = std::shared_ptr<Fill>(Fill::new_from_type(ipRectilinear));

                //m_object->print()->set_status(70, (boost::format(_u8L("Support: generate toolpath at layer %d")) % layer_id).str());

                SupportLayer* ts_layer = m_object->get_support_layer(layer_id);
                const coordf_t base_height = ts_layer->base_slab_height > EPSILON ? ts_layer->base_slab_height : ts_layer->height;
                // An interface folded onto the bed band reaches the bed as well, even when no base was folded
                // onto this layer, and draw_circles() already gave it the first layer expansion. Its brim then
                // has to be extruded at the interface slab's height and with the interface slab's nozzle.
                const bool     interface_only_bed = ts_layer->interface_slab_height > base_height + EPSILON;
                const coordf_t bed_height         = interface_only_bed ? ts_layer->interface_slab_height : base_height;
                const double   bed_nozzle         = interface_only_bed ? ts_layer->planned_interface_nozzle_diameter :
                                                                         ts_layer->planned_nozzle_diameter;
                const bool base_starts_on_bed =
                    m_raft_layers == 0 && ts_layer->print_z - bed_height <= EPSILON;
                const Flow support_flow = ts_layer->planned_nozzle_diameter > EPSILON ?
                    m_support_params.support_material_flow_for_nozzle(ts_layer->planned_nozzle_diameter, float(base_height)) :
                    Flow(support_extrusion_width, base_height, nozzle_diameter);
                const Flow first_layer_support_flow =
                    bed_nozzle > EPSILON ?
                    m_support_params.support_material_first_layer_flow_for_nozzle(
                        bed_nozzle, float(bed_height)) :
                    m_support_params.first_layer_flow;
                const coordf_t interface_height = ts_layer->interface_slab_height > EPSILON ?
                    ts_layer->interface_slab_height : ts_layer->height;
                Flow interface_flow = ts_layer->planned_interface_nozzle_diameter > EPSILON ?
                    m_support_params.support_material_interface_flow_for_nozzle(
                        ts_layer->planned_interface_nozzle_diameter, float(interface_height)) :
                    support_material_interface_flow(m_object, interface_height);
                // The interface is folded on the node grid now, so the roof and the transition layer below it
                // belong to the same slab and share its height. The floor is the one interface region that is
                // never folded - it is the bottom contact, generated per layer in postprocess_base_areas() -
                // so it keeps a flow of its own even when the roof above got pinned to a nozzle.
                const coordf_t layer_interface_density = std::min(
                    1., interface_flow.spacing() /
                        (object_config.support_interface_spacing.value + interface_flow.spacing()));
                const Flow floor_flow = ts_layer->planned_interface_nozzle_diameter > EPSILON ?
                    m_support_params.support_material_interface_flow_for_nozzle(
                        ts_layer->planned_interface_nozzle_diameter, float(ts_layer->height)) :
                    support_material_interface_flow(m_object, ts_layer->height);
                const coordf_t floor_density = std::min(
                    1., floor_flow.spacing() /
                        (object_config.support_interface_spacing.value + floor_flow.spacing()));
                coordf_t support_spacing         = object_config.support_base_pattern_spacing.value + support_flow.spacing();
                coordf_t support_density         = std::min(1., support_flow.spacing() / support_spacing);
                ts_layer->support_fills.no_sort = false;

                for (auto& area_group : ts_layer->area_groups) {
                    
                    filler_interface->set_bounding_box(bbox_object);
                    filler_Roof1stLayer->set_bounding_box(bbox_object);
                    filler_interface->angle    = Geometry::deg2rad(object_config.support_angle.value + 90.);
                    filler_Roof1stLayer->angle = Geometry::deg2rad(object_config.support_angle.value + 90.);

                    ExPolygon& poly = *area_group.area;
                    ExPolygons polys;
                    FillParams fill_params;
                    if (area_group.type != SupportLayer::BaseType) {
                        // interface
                        // BBS multi-diameter: the bed band is emitted on its top layer once folded, so the
                        // brim belongs to whichever layer's material actually reaches the bed.
                        if (base_starts_on_bed) {
                            // This brim belongs to the tree support, so its flow must follow support_filament.
                            // Print::brim_flow() is derived from a model wall filament and may use a different
                            // nozzle diameter in multi-diameter mode.
                            Flow flow = first_layer_support_flow;
                            make_perimeter_and_inner_brim(ts_layer->support_fills.entities, poly, wall_count, flow,
                                                          area_group.type == SupportLayer::RoofType ? erSupportMaterialInterface : erSupportMaterial);
                            polys = std::move(offset_ex(poly, -flow.scaled_spacing()));
                        } else if (area_group.type == SupportLayer::Roof1stLayer) {
                            polys = std::move(offset_ex(poly, 0.5*support_flow.scaled_width()));
                        }
                        else {
                            polys.push_back(poly);
                        }
                        fill_params.density     = layer_interface_density;
                        fill_params.dont_adjust = true;
                    }
                    if (area_group.type == SupportLayer::Roof1stLayer) {
                        // use higher flow for roof_1st_layer ?
                        Flow roof_1st_layer_flow = interface_flow/*.with_flow_ratio(1.5)*/;

                        // Note: spacing means the separation between two lines as if they are tightly extruded
                        filler_Roof1stLayer->spacing = roof_1st_layer_flow.spacing();

                        // BBS multi-diameter: the interface sits N *printed* layers above, and a folded slab
                        // swallows the ones in between - draw_circles() zeroes their height. Adding the raw
                        // layer count would land inside a slab, so walk the layers that actually print.
                        SupportLayer *interface_layer_above = nullptr;
                        if (m_fold_carriers.empty())
                            interface_layer_above = m_object->get_support_layer(layer_id + m_support_params.num_top_interface_layers);
                        else {
                            int left = int(m_support_params.num_top_interface_layers);
                            for (int probe = int(layer_id) + 1;
                                 left > 0 && probe < int(m_object->support_layer_count()); ++ probe) {
                                SupportLayer *candidate = m_object->get_support_layer(probe);
                                if (candidate == nullptr || candidate->height < EPSILON)
                                    continue;
                                if (-- left == 0)
                                    interface_layer_above = candidate;
                            }
                        }
                        double bridge_angle = 0.;
                        if (interface_layer_above && bridge_angle_above(interface_layer_above->print_z, polys, bridge_angle))
                            filler_Roof1stLayer->angle = bridge_angle + (m_support_params.num_top_interface_layers - 1) * M_PI_2;

                        // generate a perimeter first to support interface better
                        ExtrusionEntityCollection* temp_support_fills = new ExtrusionEntityCollection();
                        make_perimeter_and_infill(temp_support_fills->entities, poly, 1, roof_1st_layer_flow, erSupportTransition,
                            filler_Roof1stLayer.get(), layer_interface_density, false);
                        temp_support_fills->no_sort = true; // make sure loops are first
                        if (!temp_support_fills->entities.empty())
                            ts_layer->support_fills.entities.push_back(temp_support_fills);
                        else
                            delete temp_support_fills;
                    } else if (area_group.type == SupportLayer::FloorType) {
                        // floor_areas
                        fill_params.density = floor_density;
                        filler_interface->spacing = floor_flow.spacing();
                        fill_expolygons_generate_paths(ts_layer->support_fills.entities, polys,
                            filler_interface.get(), fill_params, erSupportMaterialInterface,
                            floor_flow);
                    } else if (area_group.type == SupportLayer::RoofType) {
                        // roof_areas
                        fill_params.density       = layer_interface_density;
                        filler_interface->spacing = interface_flow.spacing();
                        if (m_object_config->support_interface_pattern == smipGrid) {
                            filler_interface->angle = Geometry::deg2rad(object_config.support_angle.value);
                            fill_params.dont_sort = true;
                        }
                        if (m_support_params.contact_fill_pattern == ipRectilinear) {
                            bool bridge_found           = false;
                            for (size_t i = 0; i < m_support_params.num_top_interface_layers; i++) {
                                auto cur_ts_layer = m_object->get_support_layer(layer_id + i);
                                if (cur_ts_layer == nullptr) break;
                                double bridge_angle = 0.;
                                if (bridge_angle_above(cur_ts_layer->print_z, polys, bridge_angle)) {
                                    filler_interface->angle = bridge_angle + (i + 1) * M_PI_2;
                                    bridge_found            = true;
                                    break;
                                }
                            }
                            if (!bridge_found)
                                filler_interface->layer_id = area_group.interface_id;
                            else
                                filler_interface->layer_id = 0;
                        }

                        ExtrusionEntityCollection *temp_support_fills = new ExtrusionEntityCollection();
                        for (auto &single_poly : union_ex(polys))
                            make_perimeter_and_infill(temp_support_fills->entities, single_poly, 1, interface_flow, erSupportMaterialInterface, filler_interface.get(),
                                                      layer_interface_density, false);
                        temp_support_fills->no_sort = true; // make sure loops are first
                        if (!temp_support_fills->entities.empty())
                            ts_layer->support_fills.entities.push_back(temp_support_fills);
                        else
                            delete temp_support_fills;

                        if (m_support_params.enable_support_ironing) {
                            SupportLayer const        *ts_upper_layer        = m_object->get_support_layer(layer_id+1);
                            ExtrusionEntityCollection *temp_support_ironings = new ExtrusionEntityCollection();
                            ExPolygons                 unioned_expolygons    = union_ex(polys), upper_expolygons;
                            if (ts_upper_layer) {
                                for (auto &area_t : ts_upper_layer->area_groups) upper_expolygons.push_back(*(area_t.area));
                                unioned_expolygons = diff_ex(unioned_expolygons, upper_expolygons);
                                unioned_expolygons = union_ex(unioned_expolygons);
                            }
                            if (!unioned_expolygons.empty()) {
                                generate_support_ironing_entity(
                                    unioned_expolygons, bbox_object, ts_layer,
                                    temp_support_ironings, interface_flow);
                            }
                            temp_support_ironings->no_sort = true; // make sure loops are first
                            if (!temp_support_ironings->entities.empty())
                                ts_layer->support_fills.entities.push_back(temp_support_ironings);
                            else
                                delete temp_support_ironings;
                        }
                    }
                    else {
                        // base_areas
                        Flow flow = base_starts_on_bed ? first_layer_support_flow : support_flow;
                        bool need_infill = with_infill;
                        if(m_object_config->support_base_pattern==smpDefault)
                            need_infill &= area_group.need_infill;

                        std::shared_ptr<Fill> filler_support = std::shared_ptr<Fill>(Fill::new_from_type(base_starts_on_bed ? ipConcentric : m_support_params.base_fill_pattern));
                        filler_support->set_bounding_box(bbox_object);
                        filler_support->spacing = flow.spacing();
                        filler_support->angle = Geometry::deg2rad(object_config.support_angle.value);

                        Polygons loops = to_polygons(poly);
                        if (base_starts_on_bed) {
                            float density = float(m_object_config->raft_first_layer_density.value * 0.01);
                            fill_expolygons_with_sheath_generate_paths(ts_layer->support_fills.entities, loops, filler_support.get(), density, erSupportMaterial, flow,
                                                                       m_support_params, true, false);
                        }
                        else {
                            if (need_infill && m_support_params.base_fill_pattern != ipLightning) {
                                // allow infill-only mode if support is thick enough (so min_wall_count is 0);
                                // otherwise must draw 1 wall
                                // Don't need extra walls if we have infill. Extra walls may overlap with the infills.
                                size_t min_wall_count = wall_count == 0 ? 0 : 1;
                                make_perimeter_and_infill(ts_layer->support_fills.entities, poly, std::max(int(min_wall_count), wall_count), flow,
                                    erSupportMaterial, filler_support.get(), support_density);
                            }
                            else {
                                // BBS multi-diameter: an area widened at a nozzle change also gets the second wall
                                // when the wall count is automatic. Its paths are inset by half their own width, so
                                // the bead's outer edge stays on the contour and it grows inwards onto the coarse bead.
                                const bool widened = area_group.line_width > flow.width() + EPSILON;
                                SupportParameters support_params = m_support_params;
                                if ((area_group.need_extra_wall || widened) && object_config.tree_support_wall_count.value == -1)
                                    support_params.tree_branch_diameter_double_wall_area_scaled = 0.1;
                                const Flow wall_flow = widened ? flow.with_width(float(area_group.line_width)) : flow;
                                tree_supports_generate_paths(ts_layer->support_fills.entities, loops, wall_flow, support_params);
                            }
                        }

                        if (area_group.need_cooling) 
                        {
                            std::function<void(ExtrusionEntityCollection *)> setOverhangDegreeImpl = [&](ExtrusionEntityCollection *entity) {
                                for (auto entityPtr : entity->entities) {
                                    if (ExtrusionEntityCollection *collection = dynamic_cast<ExtrusionEntityCollection *>(entityPtr)) {
                                        setOverhangDegreeImpl(collection);
                                    } else if (ExtrusionPath *path = dynamic_cast<ExtrusionPath *>(entityPtr)) {
                                        path->set_overhang_degree(10);
                                    } else if (ExtrusionMultiPath *multipath = dynamic_cast<ExtrusionMultiPath *>(entityPtr)) {
                                        for (ExtrusionPath &path : multipath->paths) { path.set_overhang_degree(10); }
                                    } else if (ExtrusionLoop *loop = dynamic_cast<ExtrusionLoop *>(entityPtr)) {
                                        for (ExtrusionPath &path : loop->paths) { path.set_overhang_degree(10); }
                                    }
                                }
                            };
                            setOverhangDegreeImpl(&ts_layer->support_fills);
                        }
                    }
                }
                if (m_support_params.base_fill_pattern == ipLightning)
                {
                    // A base that reaches the bed was already filled above, concentrically and at the raft
                    // first layer density, and that path has no lightning guard of its own. Laying a
                    // lightning scaffold over it would extrude the area twice. Unfolded, this never showed:
                    // the bed band is support layer 0, which never becomes a lower_layer and so never gets
                    // a generator entry. Folded, the band is emitted on its top layer, which does.
                    if (base_starts_on_bed)
                        continue;
                    double print_z = ts_layer->print_z;
                    if (printZ_to_lightninglayer.find(print_z) == printZ_to_lightninglayer.end())
                        continue;
                    //TODO:
                    //1.the second parameter of convertToLines seems to decide how long the lightning should be trimmed from its root, so that the root wont overlap/detach the support contour.
                    // whether current value works correctly remained to be tested
                    //2.related to previous one, that lightning roots need to be trimed more when support has multiple walls
                    //3.function connect_infill() and variable 'params' helps create connection pattern along contours between two lightning roots,
                    // strengthen lightnings while it may make support harder. decide to enable it or not. if yes, proper values for params are remained to be tested
                    auto& lightning_layer = generator->getTreesForLayer(printZ_to_lightninglayer[print_z]);

                    Flow       flow  = base_starts_on_bed ? first_layer_support_flow : support_flow;
                    ExPolygons areas = offset_ex(ts_layer->base_areas, -flow.scaled_spacing());

                    for (auto& area : areas)
                    {
                        Polylines polylines = lightning_layer.convertToLines(to_polygons(area), 0);
                        for (auto itr = polylines.begin(); itr != polylines.end();)
                        {
                            if (itr->length() < scale_(1.0))
                                itr = polylines.erase(itr);
                            else
                                itr++;
                        }
                        Polylines opt_polylines;
#if 1
                        //this wont create connection patterns along contours
                        append(opt_polylines, chain_polylines(std::move(polylines)));
#else
                        //this will create connection patterns along contours
                        FillParams params;
                        params.anchor_length = float(Fill::infill_anchor * 0.01 * flow.spacing());
                        params.anchor_length_max = Fill::infill_anchor_max;
                        params.anchor_length = std::min(params.anchor_length, params.anchor_length_max);
                        Fill::connect_infill(std::move(polylines), area, opt_polylines, flow.spacing(), params);
#endif
                        extrusion_entities_append_paths(ts_layer->support_fills.entities, opt_polylines, erSupportMaterial,
                            float(flow.mm3_per_mm()), float(flow.width()), float(flow.height()));

#ifdef SUPPORT_TREE_DEBUG_TO_SVG
                        std::string name = debug_out_path("trees_polyline_%.2f.svg", ts_layer->print_z);
                        BoundingBox bbox = get_extents(ts_layer->base_areas);
                        SVG svg(name, bbox);
                        if (svg.is_opened()) {
                            svg.draw(ts_layer->base_areas, "blue");
                            svg.draw(generator->Overhangs()[printZ_to_lightninglayer[print_z]], "red");
                            for (auto &line : opt_polylines) svg.draw(line, "yellow");
                        }
#endif
                    }
                }

                // sort extrusions to reduce travel, also make sure walls go before infills
                if (ts_layer->support_fills.no_sort == false) {
                    chain_and_reorder_extrusion_entities(ts_layer->support_fills.entities);
                }
            }
        }
    );
}

void deleteDirectoryContents(const std::filesystem::path& dir)
{
    for (const auto& entry : std::filesystem::directory_iterator(dir))
        std::filesystem::remove_all(entry.path());
}


void TreeSupport::move_bounds_to_contact_nodes(std::vector<TreeSupport3D::SupportElements> &move_bounds,
                                  PrintObject                             &print_object,
                                  const TreeSupport3D::TreeSupportSettings    &config)
{
    m_ts_data = print_object.alloc_tree_support_preview_cache();
    // convert move_bounds back to Support Nodes for tree skeleton preview
    this->contact_nodes.resize(move_bounds.size());
    for (int layer_nr = move_bounds.size() - 1; layer_nr >= 0; layer_nr--) {
        TreeSupport3D::SupportElements &elements      = move_bounds[layer_nr];
        auto            &contact_nodes_layer = this->contact_nodes[layer_nr];
        for (size_t i = 0; i < elements.size(); i++) {
            auto &elem    = elements[i];
            auto &state   = elem.state;
            state.print_z = layer_z(print_object.slicing_parameters(), config, layer_nr);
            auto node = this->create_node(state.result_on_layer, state.distance_to_top, layer_nr, 0, state.to_buildplate, nullptr, state.print_z, config.layer_height);
            contact_nodes_layer.push_back(node);
            if (layer_nr < move_bounds.size() - 1 && !elem.parents.empty()) {
                node->parent = contact_nodes[layer_nr + 1][elem.parents.front()];
                for (int j : elem.parents) {
                    contact_nodes[layer_nr + 1][j]->child = node;
                    node->parents.push_back(contact_nodes[layer_nr + 1][j]);
                }
            }

            if (i == 0) { m_ts_data->layer_heights.emplace_back(state.print_z, config.layer_height, std::max(0, layer_nr - 1)); }
        }
    }
}

void TreeSupport::generate()
{
    if (!is_tree(m_object_config->support_type.value)) return;

    if (m_support_params.support_style == smsTreeOrganic) {
        generate_tree_support_3D(*m_object, this, this->throw_on_cancel);
        return;
    }
    
    profiler.stage_start(STAGE_total);

    // Generate overhang areas
    profiler.stage_start(STAGE_DETECT_OVERHANGS);
    const long long support_detect_begin_time = Slic3r::Utils::get_current_milliseconds_time_monotonic();
    m_object->print()->set_status(55, _u8L("Generating support"));
    detect_overhangs();
    m_object->support_stage_times().detect +=
        Slic3r::Utils::get_current_milliseconds_time_monotonic() - support_detect_begin_time;
    profiler.stage_finish(STAGE_DETECT_OVERHANGS);

    create_tree_support_layers();
    //m_ts_data = m_object->alloc_tree_support_preview_cache();
    m_ts_data->is_slim = is_slim;
    // // get the ring of outside plate
    // auto tmp= diff_ex(offset_ex(m_machine_border, scale_(100)), m_machine_border);
    // if (!tmp.empty()) m_ts_data->m_machine_border = tmp[0];

#if USE_SUPPORT_3D
    std::vector<TreeSupport3D::SupportElements> move_bounds(m_highest_overhang_layer + 1);
    profiler.stage_start(STAGE_GENERATE_CONTACT_NODES);
    //m_object->print()->set_status(56, _u8L("Support: precalculate avoidance"));

    Points  bedpts = m_machine_border.contour.points;
    Pointfs bedptsf;
    std::transform(bedpts.begin(), bedpts.end(), std::back_inserter(bedptsf), [](const Point &p) { return unscale(p); });
    BuildVolume build_volume{bedptsf, m_print_config->printable_height, {}};
    TreeSupport3D::TreeSupportSettings tree_support_3d_config{ TreeSupport3D::TreeSupportMeshGroupSettings{ *m_object }, m_slicing_params };
    m_model_volumes = std::make_unique<TreeSupport3D::TreeModelVolumes>( *m_object, build_volume, tree_support_3d_config.maximum_move_distance, tree_support_3d_config.maximum_move_distance_slow, 1);
    // ### Precalculate avoidances, collision etc.
    if (m_highest_overhang_layer <= tree_support_3d_config.z_distance_top_layers)
        return;
    size_t highest_node_layer = m_highest_overhang_layer - tree_support_3d_config.z_distance_top_layers + 1;
    m_model_volumes->precalculate(*m_object, highest_node_layer, throw_on_cancel);

    auto t_precalc = std::chrono::high_resolution_clock::now();
    // value is the area where support may be placed. As this is calculated in CreateLayerPathing it is saved and reused in draw_areas
    // ### Place tips of the support tree
    size_t                       num_support_layers = m_highest_overhang_layer + 1;
    std::vector<Polygons>        overhangs;
    const int                    num_raft_layers = int(tree_support_3d_config.raft_layers.size());
    const int                    num_layers      = int(m_object->layer_count()) + num_raft_layers;
    bool                         has_raft        = num_raft_layers > 0;
    overhangs.resize(num_layers);
    for (size_t i = 0; i < m_object->layer_count(); i++) {
        for (ExPolygon &expoly : m_object->get_layer(i)->loverhangs) {
            Polygons polys = to_polygons(expoly);
            if (overhang_types[&expoly] == TreeSupport::SharpTail) { polys = offset(polys, scale_(0.2)); }
            append(overhangs[i + num_raft_layers], polys);
        }
    }

    SupportGeneratorLayerStorage   layer_storage;
    SupportGeneratorLayersPtr      top_contacts;
    SupportGeneratorLayersPtr      bottom_contacts;
    SupportGeneratorLayersPtr      interface_layers;
    SupportGeneratorLayersPtr      base_interface_layers;
    SupportGeneratorLayersPtr      intermediate_layers(num_support_layers, nullptr);
    if (m_support_params.has_top_contacts || has_raft) top_contacts.assign(num_support_layers, nullptr);
    if (m_support_params.has_bottom_contacts) bottom_contacts.assign(num_support_layers, nullptr);
    if (m_support_params.has_interfaces() || has_raft) interface_layers.assign(num_support_layers, nullptr);
    if (m_support_params.has_base_interfaces() || has_raft) base_interface_layers.assign(num_support_layers, nullptr);
    TreeSupport3D::InterfacePlacer interface_placer{m_object->slicing_parameters(), m_support_params, tree_support_3d_config,
                                     // Outputs
                                     layer_storage, top_contacts, interface_layers, base_interface_layers};

    TreeSupport3D::generate_initial_areas(*m_object, *m_model_volumes.get(), tree_support_3d_config, overhangs, move_bounds, interface_placer, throw_on_cancel);
    move_bounds_to_contact_nodes(move_bounds, *m_object, tree_support_3d_config);
#else
    generate_contact_points();
    profiler.stage_finish(STAGE_GENERATE_CONTACT_NODES);

    m_ts_data->layer_heights = plan_layer_heights();
#endif

    // The tree geometry is generated on the fine support-node grid, while a coarse support nozzle prints
    // several of those nodes as one fixed-grid slab. Cache the plan before dropping nodes so per-node growth
    // can be normalized to the slab that will eventually be emitted.
    m_support_merge_plan = make_support_merge_plan(*m_object);

    //Drop nodes to lower layers.
    profiler.stage_start(STAGE_DROP_DOWN_NODES);
    m_object->print()->set_status(60, _u8L("Generating support"));
    drop_nodes();
    profiler.stage_finish(STAGE_DROP_DOWN_NODES);
    auto log_zigzag_stats = [&](const char *tag) {
        if (get_logging_level() < 4)
            return;
        size_t nodes = 0, moved = 0, reversals = 0, heavy_reversals = 0;
        double lateral = 0., rev_mass = 0.;
        for (const auto &layer : contact_nodes)
            for (const SupportNode *n : layer) {
                if (!n->valid)
                    continue;
                ++nodes;
                if (!n->parent)
                    continue;
                const SupportNode *heavy_p = n->parent;
                for (const SupportNode *p : n->parents)
                    if (p && p->carried_mass > heavy_p->carried_mass)
                        heavy_p = p;
                const Vec2d d1 = unscale(n->position - n->parent->position);
                lateral += d1.norm();
                if (d1.norm() > 0.05)
                    ++moved;
                if (!n->child || !n->child->valid)
                    continue;
                const Vec2d d2 = unscale(n->child->position - n->position);
                if (d1.norm() > 0.05 && d2.norm() > 0.05 && d1.dot(d2) < -0.5 * d1.norm() * d2.norm())
                    ++reversals;
                const Vec2d dh1 = unscale(n->position - heavy_p->position);
                if (dh1.norm() > 0.05 && d2.norm() > 0.05 && dh1.dot(d2) < -0.5 * dh1.norm() * d2.norm()) {
                    ++heavy_reversals;
                    rev_mass += n->carried_mass;
                }
            }
        BOOST_LOG_TRIVIAL(debug) << format("[%s] object=%s nodes=%zu moved=%zu reversals=%zu heavy_rev=%zu rev_mass=%.0f lateral_mm=%.1f",
                                           tag, m_object->model_object()->name.c_str(), nodes, moved, reversals, heavy_reversals, rev_mass, lateral);
    };
    log_zigzag_stats("zigzag");

    smooth_nodes();// , tree_support_3d_config);

    log_zigzag_stats("zigzag_post");

    // BBS multi-diameter: fold the node chains onto the slab grid a coarse nozzle can extrude. This has to
    // follow smoothing, which needs the fine grid samples, and precede everything that turns nodes into
    // geometry, so the floating support prune and the disks all see the folded tree.
    fold_nodes_multi_diameter();
    smooth_line_width_multi_diameter();

    //Generate support areas.
    profiler.stage_start(STAGE_DRAW_CIRCLES);
    m_object->print()->set_status(65, _u8L("Generating support"));
    const bool has_support_interface =
        m_support_params.has_interfaces() || m_support_params.has_base_interfaces() ||
        m_slicing_params.interface_raft_layers > 0;
    const long long support_area_begin_time = Slic3r::Utils::get_current_milliseconds_time_monotonic();
    draw_circles();
    if (has_support_interface) {
        // The classic tree path creates base and interface areas in one pass, so this is the
        // narrowest wall-clock stage that contains its interface-layer generation.
        m_object->support_stage_times().interface_generate +=
            Slic3r::Utils::get_current_milliseconds_time_monotonic() - support_area_begin_time;
    }
    profiler.stage_finish(STAGE_DRAW_CIRCLES);

    profiler.stage_start(STAGE_GENERATE_TOOLPATHS);
    const long long support_toolpath_begin_time = Slic3r::Utils::get_current_milliseconds_time_monotonic();
    m_object->print()->set_status(70, _u8L("Generating support"));
    generate_toolpaths();
    m_object->support_stage_times().toolpath_generate +=
        Slic3r::Utils::get_current_milliseconds_time_monotonic() - support_toolpath_begin_time;
    profiler.stage_finish(STAGE_GENERATE_TOOLPATHS);

    profiler.stage_finish(STAGE_total);
    BOOST_LOG_TRIVIAL(info) << "tree support time " << profiler.report();
}

coordf_t TreeSupport::calc_branch_radius(coordf_t base_radius, size_t layers_to_top, size_t tip_layers, double diameter_angle_scale_factor)
{
    double radius;
    if (!is_slim) {
        if ((layers_to_top + 1) > tip_layers) {
            radius = base_radius + base_radius * (layers_to_top + 1) * diameter_angle_scale_factor;
        } else {
            radius = base_radius * (layers_to_top + 1) / tip_layers;
        }
    } else {
        if ((layers_to_top + 1) > tip_layers * 2) {
            radius = base_radius + base_radius * (layers_to_top + 1) * diameter_angle_scale_factor;
        } else {
            radius = base_radius * (layers_to_top + 1) / (tip_layers * 2);
        }
    }
    radius = std::clamp(radius, tip_radius, MAX_BRANCH_RADIUS);
    return radius;
}

coordf_t TreeSupport::calc_branch_radius(coordf_t base_radius, coordf_t mm_to_top,  double diameter_angle_scale_factor, bool use_min_distance)
{
    double radius;
    coordf_t tip_height = base_radius;// this is a 45 degree tip
    if (mm_to_top > tip_height)
    {
        radius = base_radius + (mm_to_top-tip_height) * diameter_angle_scale_factor;
    }
    else
    {
        radius = mm_to_top;// this is a 45 degree tip
    }
    radius = std::clamp(radius, tip_radius, MAX_BRANCH_RADIUS);
    // if have interface layers, radius should be larger
    if (m_object_config->support_interface_top_layers.value > 0)
        radius = std::max(radius, base_radius);
#if USE_SUPPORT_3D
    if (m_model_volumes)
        radius = unscale_(m_model_volumes->ceilRadius(scale_(radius), use_min_distance));
#endif
    return radius;
}

coordf_t TreeSupport::calc_radius(coordf_t mm_to_top)
{
    return calc_branch_radius(base_radius, mm_to_top, diameter_angle_scale_factor);
}

coordf_t TreeSupport::get_radius(const SupportNode* node)
{
    if (node->radius == 0)
        node->radius = calc_radius(node->dist_mm_to_top);
    return node->radius;
}

ExPolygons TreeSupport::get_avoidance(coordf_t radius, size_t obj_layer_nr)
{
#if USE_SUPPORT_3D
    if (m_model_volumes) {
        bool on_build_plate = m_object_config->support_on_build_plate_only.value;
        const Polygons& avoid_polys = m_model_volumes->getAvoidance(radius, obj_layer_nr, TreeSupport3D::TreeModelVolumes::AvoidanceType::FastSafe, on_build_plate, true);
        ExPolygons expolys;
        for (auto& poly : avoid_polys)
            expolys.emplace_back(std::move(poly));
        return expolys;
    }
    return ExPolygons();
#else
    return m_ts_data->get_avoidance(radius, obj_layer_nr);
#endif
}

ExPolygons TreeSupport::get_collision(coordf_t radius, size_t layer_nr)
{
#if USE_SUPPORT_3D
    if (m_model_volumes) {
        bool on_build_plate = m_object_config->support_on_build_plate_only.value;
        const Polygons& collision_polys = m_model_volumes->getCollision(radius, layer_nr, true);
        ExPolygons expolys;
        for (auto& poly : collision_polys)
            expolys.emplace_back(std::move(poly));
        return expolys;
    }
#else
    return m_ts_data->get_collision(radius, layer_nr);
#endif
    return ExPolygons();
}

Polygons TreeSupport::get_collision_polys(coordf_t radius, size_t layer_nr)
{
#if USE_SUPPORT_3D
    if (m_model_volumes) {
        bool on_build_plate = m_object_config->support_on_build_plate_only.value;
        const Polygons& collision_polys = m_model_volumes->getCollision(radius, layer_nr, true);
        return collision_polys;
    }
#else
    ExPolygons expolys = m_ts_data->get_collision(radius, layer_nr);
    Polygons polys;
    for (auto& expoly : expolys)
        for (int i = 0; i < expoly.num_contours(); i++)
            polys.emplace_back(std::move(expoly.contour_or_hole(i)));
    return polys;
#endif
    return Polygons();
}

ExPolygons avoid_object_remove_extra_small_parts(const ExPolygon &expoly, const ExPolygons& avoid_region) {
    ExPolygons expolys_out;
    if(expoly.empty()) return expolys_out;
    auto clipped_avoid_region=ClipperUtils::clip_clipper_polygons_with_subject_bbox(avoid_region, get_extents(expoly));
    auto  expolys_avoid = diff_ex(expoly, clipped_avoid_region);
    int   idx_max_area  = -1;
    float max_area      = 0;
    for (int i = 0; i < expolys_avoid.size(); ++i) {
        auto a = expolys_avoid[i].area();
        if (a > max_area) {
            max_area     = a;
            idx_max_area = i;
        }
    }
    if (idx_max_area >= 0) expolys_out.emplace_back(std::move(expolys_avoid[idx_max_area]));

    return expolys_out;
}

Polygons TreeSupport::get_trim_support_regions(
    const PrintObject& object,
    SupportLayer* support_layer_ptr,
    const coordf_t       gap_extra_above,
    const coordf_t       gap_extra_below,
    const coordf_t       gap_xy)
{
    static const double sharp_tail_xy_gap = 0.2f;
    static const double no_overlap_xy_gap = 0.2f;
    double gap_xy_scaled = scale_(gap_xy);
    SupportLayer& support_layer = *support_layer_ptr;
    auto m_print_config = object.print()->config();

    size_t idx_object_layer_overlapping = size_t(-1);

    auto is_layers_overlap = [](const SupportLayer& support_layer, const Layer& object_layer, coordf_t bridging_height = 0.f) -> bool {
        if (std::abs(support_layer.print_z - object_layer.print_z) < EPSILON)
            return true;

        coordf_t object_lh = bridging_height > EPSILON ? bridging_height : object_layer.height;
        if (support_layer.print_z < object_layer.print_z && support_layer.print_z > object_layer.print_z - object_lh)
            return true;

        if (support_layer.print_z > object_layer.print_z && support_layer.bottom_z() < object_layer.print_z - EPSILON)
            return true;

        return false;
        };

    // Find the overlapping object layers including the extra above / below gap.
    coordf_t z_threshold = support_layer.bottom_z() - gap_extra_below + EPSILON;
    idx_object_layer_overlapping = Layer::idx_higher_or_equal(
        object.layers().begin(), object.layers().end(), idx_object_layer_overlapping,
        [z_threshold](const Layer* layer) { return layer->print_z >= z_threshold; });
    // Collect all the object layers intersecting with this layer.
    Polygons polygons_trimming;
    size_t i = idx_object_layer_overlapping;
    for (; i < object.layers().size(); ++i) {
        const Layer& object_layer = *object.layers()[i];
        if (object_layer.bottom_z() > support_layer.print_z + gap_extra_above - EPSILON)
            break;

        bool is_overlap = is_layers_overlap(support_layer, object_layer);
        for (const ExPolygon& expoly : object_layer.lslices) {
            // BBS
            bool is_sharptail = !intersection_ex({ expoly }, object_layer.sharp_tails).empty();
            coordf_t trimming_offset = is_sharptail ? scale_(sharp_tail_xy_gap) :
                is_overlap ? gap_xy_scaled :
                scale_(no_overlap_xy_gap);
            polygons_append(polygons_trimming, offset({ expoly }, trimming_offset, SUPPORT_SURFACES_OFFSET_PARAMETERS));
            }
        }
    if (!m_slicing_params.soluble_interface && m_object_config->thick_bridges) {
        // Collect all bottom surfaces, which will be extruded with a bridging flow.
        for (; i < object.layers().size(); ++i) {
            const Layer& object_layer = *object.layers()[i];
            bool some_region_overlaps = false;
            for (LayerRegion* region : object_layer.regions()) {
                coordf_t bridging_height = region->region().bridging_height_avg(m_print_config);
                if (object_layer.print_z - bridging_height > support_layer.print_z + gap_extra_above - EPSILON)
                    break;
                some_region_overlaps = true;

                bool is_overlap = is_layers_overlap(support_layer, object_layer, bridging_height);
                coordf_t trimming_offset = is_overlap ? gap_xy_scaled : scale_(no_overlap_xy_gap);
                polygons_append(polygons_trimming,
                    offset(region->fill_surfaces.filter_by_type(stBottomBridge), trimming_offset, SUPPORT_SURFACES_OFFSET_PARAMETERS));
                }
            if (!some_region_overlaps)
                break;
            }
        }

    return polygons_trimming;
}

void TreeSupport::draw_circles()
{
    const PrintObjectConfig &config = m_object->config();
    const Print* print = m_object->print();
    bool has_brim = print->has_brim();
    int bottom_gap_layers = round(m_slicing_params.gap_object_support / m_slicing_params.layer_height);
    const coordf_t branch_radius = config.tree_support_branch_diameter.value / 2;
    const coordf_t branch_radius_scaled = scale_(branch_radius);
    bool on_buildplate_only = m_object_config->support_on_build_plate_only.value;
    Polygon branch_circle; //Pre-generate a circle with correct diameter so that we don't have to recompute those (co)sines every time.

    const bool z_overrides  = print->config().top_z_overrides_xy_distance;

    // Use square support if there are too many nodes per layer because circle support needs much longer time to compute
    // Hower circle support can be printed faster, so we prefer circle for fewer nodes case.
    const bool SQUARE_SUPPORT = avg_node_per_layer > 200;
    const int  CIRCLE_RESOLUTION = SQUARE_SUPPORT ? 4 : 25; // The number of vertices in each circle.


    for (int i = 0; i < CIRCLE_RESOLUTION; i++)
    {
        double angle;
        if (SQUARE_SUPPORT)
            angle = (double) i / CIRCLE_RESOLUTION * TAU + PI / 4.0 + nodes_angle;
        else
            angle = (double) i / CIRCLE_RESOLUTION * TAU;
        branch_circle.append(Point(cos(angle) * branch_radius_scaled, sin(angle) * branch_radius_scaled));
    }

    // Performance optimization. Only generate lslices for brim and skirt.
    size_t brim_skirt_layers = has_brim ? 1 : 0;
    const PrintConfig& print_config = print->config();
    for (const PrintObject* object : print->objects())
    {
        size_t skirt_layers = print->has_infinite_skirt() ? object->layer_count() : std::min(size_t(print_config.skirt_height.value), object->layer_count());
        brim_skirt_layers = std::max(brim_skirt_layers, skirt_layers);
    }

    // generate areas
    const size_t   top_interface_layers = config.support_interface_top_layers.value;
    const size_t   bottom_interface_layers = 0; //config.support_interface_bottom_layers.value;
    const bool with_lightning_infill = m_support_params.base_fill_pattern == ipLightning;
    coordf_t support_extrusion_width = m_support_params.support_extrusion_width;
    const coordf_t line_width_scaled                 = scale_(support_extrusion_width);
    const float tree_brim_width = config.raft_first_layer_expansion.value;
    const bool bottom_expand_enabled = config.tree_support_wall_count > 1 || config.tree_support_wall_count < 0;

    if (m_object->support_layer_count() <= m_raft_layers)
        return;
    BOOST_LOG_TRIVIAL(info) << "for object: " << m_object->model_object()->name;

    // BBS multi-diameter: disk of a node whose wall is widened at a nozzle change.
    struct WidenedArea {
        ExPolygon   area;
        BoundingBox bbox;
        coordf_t    width;
    };
    // BBS multi-diameter: the per layer state handed from drawing the nodes to finishing the areas. The two run
    // as separate passes so the finishing one can read the roof gaps of every layer a folded slab swallowed.
    struct LayerDrawData
    {
        size_t     obj_layer_nr { 0 };
        coordf_t   max_layers_above_base { 0 };
        coordf_t   max_layers_above_roof { 0 };
        coordf_t   max_layers_above_roof1 { 0 };
        int        interface_id { 0 };
        ExPolygons collision_base;
        ExPolygons area_poly;
        ExPolygons extra_wall_area;
        ExPolygons cooldown_area;
        // Sorted by width, widest first.
        std::vector<WidenedArea> widened_areas;
    };
    std::vector<LayerDrawData> layer_draw_data(m_ts_data->layer_heights.size());

    tbb::parallel_for(tbb::blocked_range<size_t>(0, m_ts_data->layer_heights.size()),
        [&](const tbb::blocked_range<size_t>& range)
        {
            for (size_t layer_nr = range.begin(); layer_nr < range.end(); layer_nr++)
            {
                if (print->canceled())
                    break;

                const std::vector<SupportNode*>& curr_layer_nodes = contact_nodes[layer_nr];
                SupportLayer* ts_layer = m_object->get_support_layer(layer_nr + m_raft_layers);
                assert(ts_layer != nullptr);

                // skip if current layer has no points. This fixes potential crash in get_collision (see jira BBL001-355)
                if (curr_layer_nodes.empty()) {
                    ts_layer->print_z = 0.0;
                    ts_layer->height = 0.0;
                    continue;
                }

                ts_layer->print_z = m_ts_data->layer_heights[layer_nr].print_z;
                ts_layer->height = m_ts_data->layer_heights[layer_nr].height;
                size_t obj_layer_nr = m_ts_data->layer_heights[layer_nr].obj_layer_nr;
                if (ts_layer->height < EPSILON) {
                    continue;
                }

                ExPolygons& base_areas = ts_layer->base_areas;
                ExPolygons& roof_areas = ts_layer->roof_areas;
                ExPolygons& roof_1st_layer = ts_layer->roof_1st_layer;
                ExPolygons& roof_gap_areas = ts_layer->roof_gap_areas;
                coordf_t         max_layers_above_base = 0;
                coordf_t         max_layers_above_roof = 0;
                coordf_t         max_layers_above_roof1 = 0;
                int interface_id = 0;
                bool has_circle_node = false;
                ExPolygons collision_sharp_tails;
                ExPolygons collision_base;
                auto             get_collision = [&](bool sharp_tail) -> ExPolygons             &{
                    ExPolygons &collision = sharp_tail ? collision_sharp_tails : collision_base;
                    if (collision.empty()) {
                        collision = offset_ex(m_ts_data->m_layer_outlines[obj_layer_nr],
                                              sharp_tail ? scale_(top_z_at(obj_layer_nr)) :
                                                           scale_((obj_layer_nr == 0) ? config.support_object_first_layer_gap : m_ts_data->m_xy_distance));
                        // the top layers may be too close to interface with adaptive layer heights and very small overhang angle.
                        // On the object first layer, clamp that expansion so a smaller support_object_first_layer_gap
                        // is not cancelled. Sharp tails keep the top-z clearance.
                        union_outlines_within_top_z(collision, obj_layer_nr, !sharp_tail && obj_layer_nr == 0);
                    }
                    return collision;
                };

                // BBS multi-diameter: fold_nodes_multi_diameter() may have made this layer the carrier of a
                // slab. A slab is one extrusion over its whole Z span, so it has to clear the object
                // everywhere in that span and not only at the face its cross-section was taken from - the
                // canonical case being a shape that widens towards the bed. Only the folded nodes get that
                // treatment: polygon nodes and the virtual gap nodes stayed on the fine grid
                // and would only be shrunk by a clearance they never needed.
                ExPolygons collision_sharp_tails_span;
                ExPolygons collision_base_span;
                auto             get_node_collision = [&](const SupportNode &node, bool sharp_tail) -> ExPolygons & {
                    if (node.slab_height <= EPSILON || node.slab_bottom_obj_layer >= obj_layer_nr)
                        return get_collision(sharp_tail);
                    ExPolygons &collision = sharp_tail ? collision_sharp_tails_span : collision_base_span;
                    if (collision.empty()) {
                        collision = get_collision(sharp_tail);
                        for (size_t layer_id = node.slab_bottom_obj_layer; layer_id < obj_layer_nr; ++ layer_id)
                            collision = union_ex(collision,
                                                 offset_ex(m_ts_data->m_layer_outlines[layer_id],
                                                           sharp_tail ? scale_(top_z_at(layer_id)) :
                                                                        scale_((layer_id == 0) ? config.support_object_first_layer_gap :
                                                                                                 m_ts_data->m_xy_distance)));
                    }
                    return collision;
                };
                // A folded interface is one extrusion over the slab span as well. Its areas come from the overhang
                // and skip the node collision, so the span has to be cleared when the roof segments are joined.
                const SupportNode *roof_span_node = nullptr;

                BOOST_LOG_TRIVIAL(debug) << "circles at layer " << layer_nr << " contact nodes size=" << curr_layer_nodes.size();
                //Draw the support areas and add the roofs appropriately to the support roof instead of normal areas.
                ts_layer->support_islands.reserve(curr_layer_nodes.size());
                ExPolygons area_poly;  // the polygon node area which will be printed as normal support
                ExPolygons extra_wall_area; //where nodes would have extra walls
                ExPolygons cooldown_area;
                std::vector<WidenedArea> widened_areas;
                for (const SupportNode* p_node : curr_layer_nodes)
                {
                    if (print->canceled())
                        break;

                    const SupportNode& node = *p_node;
                    ExPolygons area;
                    double brim_width = tree_brim_width;
                    // Generate directly from overhang polygon if one of the following is true:
                    // 1) node is a normal part of hybrid support
                    // 2) node is virtual
                    if (node.type == ePolygon || (node.distance_to_top<0 && !node.is_sharp_tail)) {
                    //    if (node.overhang.contour.size() > 100 || node.overhang.holes.size()>1)
                    //        area.emplace_back(node.overhang);
                    //    else {
                    //        area = offset_ex({ node.overhang }, scale_(m_ts_data->m_xy_distance));
                    //}
                        area = offset_ex({node.overhang}, scale_(m_ts_data->m_xy_distance));
                        area = diff_clipped(area, get_node_collision(node, node.is_sharp_tail && node.distance_to_top <= 0));
                        if (node.type == ePolygon) {
                            append(area_poly, area);
                            if (node.need_extra_wall) append(extra_wall_area, area);
                        }
                    }
                    else {
                        // merge overhang to get a smoother interface surface
                        // Do not merge when buildplate_only is on, because some underneath nodes may have been deleted.
                        if (top_interface_layers > 0 && (node.support_roof_layers_below > 1 || (node.support_roof_layers_below >= 0 && !node.is_sharp_tail)) &&
                            !on_buildplate_only) {
                            if (node.overhang.contour.size() > 100 || node.overhang.holes.size() > 1)
                                area.emplace_back(node.overhang);
                            else {
                                area = offset_ex({node.overhang}, scale_(m_ts_data->m_xy_distance));
                            }
                        } else {
                            area = avoid_object_remove_extra_small_parts(
                                tree_support_node_disk(node, branch_circle, branch_radius,
                                                       branch_radius_scaled, SQUARE_SUPPORT),
                                get_node_collision(node, node.is_sharp_tail && node.distance_to_top <= 0));
                        }

                        if (!area.empty()) has_circle_node = true;
                        if (node.need_extra_wall || (bottom_expand_enabled && node.print_z < DO_NOT_MOVER_UNDER_MM && node.dist_mm_to_top > DO_NOT_MOVER_UNDER_MM))
                            append(extra_wall_area, area);
                        if (node.overhang_degree >= 2) append(cooldown_area, area);
                    }

                    // BBS multi-diameter: a folded slab is emitted on its top layer, so the bed band no longer lives on
                    // support layer 0. Ask the node whether its material reaches the bed instead of trusting the
                    // layer index, or the first layer expansion is skipped for every folded print.
                    if (m_raft_layers == 0 &&
                        (m_fold_carriers.empty() ? layer_nr == 0 : node.print_z - node.height <= EPSILON)) {
                        if (tree_brim_width >= 0) brim_width = tree_brim_width;
                        else {
                            for (const auto &expoly : area)
                                brim_width = std::max(brim_width, expoly.map_moment_to_expansion(m_object_config->support_speed.values[0], node.dist_mm_to_top));
                        }
                        area = safe_offset_inc(area, scale_(brim_width), get_node_collision(node, false), scale_(MIN_BRANCH_RADIUS * 0.5), 0, 1);
                    }
                    if (obj_layer_nr>0 && node.distance_to_top < 0)
                        append(roof_gap_areas, area);
                    else if (m_support_params.num_top_interface_layers > 0 && obj_layer_nr > 0 && (node.support_roof_layers_below == 0 || node.support_roof_layers_below == 1) &&
                             node.distance_to_top >= m_support_params.num_top_interface_layers) {
                        append(roof_1st_layer, area);
                        max_layers_above_roof1 = std::max(max_layers_above_roof1, node.dist_mm_to_top);
                        if (node.slab_height > EPSILON) roof_span_node = &node;
                    } else if (m_support_params.num_top_interface_layers > 0 && obj_layer_nr > 0 && node.support_roof_layers_below > 0) {
                        append(roof_areas, area);
                        if (node.slab_height > EPSILON) roof_span_node = &node;
                        max_layers_above_roof = top_z_distance == 0 ? node.height : std::max(max_layers_above_roof, node.dist_mm_to_top);
                        // BBS multi-diameter: consecutive folded slabs sit several object layers apart, so the
                        // object layer number modulo the interface layer count repeats and the interface pattern
                        // stops alternating. The folded ladder position still steps by one per printed layer.
                        interface_id          = (node.slab_index >= 0 ? node.slab_index : node.obj_layer_nr) % int(top_interface_layers);
                    }
                    else
                    {
                        if (node.line_width > EPSILON)
                            for (const ExPolygon &expoly : area)
                                widened_areas.push_back({ expoly, get_extents(expoly), node.line_width });
                        append(base_areas, area);
                        max_layers_above_base = std::max(max_layers_above_base, node.dist_mm_to_top);
                    }
                }

                //m_object->print()->set_status(65, (boost::format( _u8L("Support: generate polygons at layer %d")) % layer_nr).str());

                // join roof segments
                auto get_roof_collision = [&](bool sharp_tail) -> ExPolygons & {
                    return roof_span_node != nullptr ? get_node_collision(*roof_span_node, sharp_tail) : get_collision(sharp_tail);
                };
                roof_areas     = diff_clipped(offset2_ex(roof_areas, line_width_scaled, -line_width_scaled), get_roof_collision(z_overrides));
                roof_areas     = intersection_ex(roof_areas, m_machine_border);
                roof_areas     = union_ex(roof_areas);
                roof_1st_layer = diff_clipped(offset2_ex(roof_1st_layer, line_width_scaled, -line_width_scaled),
                                              z_overrides ? offset_ex(get_roof_collision(z_overrides), line_width_scaled / 2) : get_roof_collision(false));

                // roof_1st_layer and roof_areas may intersect, so need to subtract roof_areas from roof_1st_layer
                roof_1st_layer = diff_ex(roof_1st_layer, ClipperUtils::clip_clipper_polygons_with_subject_bbox(roof_areas,get_extents(roof_1st_layer)));
                roof_1st_layer = intersection_ex(roof_1st_layer, m_machine_border);

                LayerDrawData &data         = layer_draw_data[layer_nr];
                data.obj_layer_nr           = obj_layer_nr;
                data.max_layers_above_base  = max_layers_above_base;
                data.max_layers_above_roof  = max_layers_above_roof;
                data.max_layers_above_roof1 = max_layers_above_roof1;
                data.interface_id           = interface_id;
                data.collision_base         = std::move(collision_base);
                data.area_poly              = std::move(area_poly);
                data.extra_wall_area        = std::move(extra_wall_area);
                data.cooldown_area          = std::move(cooldown_area);
                std::sort(widened_areas.begin(), widened_areas.end(),
                          [](const WidenedArea &a, const WidenedArea &b) { return a.width > b.width; });
                data.widened_areas          = std::move(widened_areas);
            }
        });

        // BBS multi-diameter: the gap areas of the layers a slab swallowed, keyed by every carrier whose
        // extrusion spans their Z. Folding moves the roof up to the carrier but leaves the virtual gap nodes
        // on their own fine layers, so a carrier has to clear the gaps of its whole span and not only the ones
        // that happen to land on its own layer - otherwise the keep-out silently stops applying as soon as the
        // interface folds.
        std::unordered_map<int, ExPolygons> swallowed_gap_by_carrier;
        for (size_t swallowed_nr = 0; swallowed_nr < m_fold_slab_of_layer.size(); ++swallowed_nr) {
            if (m_fold_slab_of_layer[swallowed_nr] < 0)
                continue;
            const SupportLayer *swallowed = m_object->get_support_layer(swallowed_nr + m_raft_layers);
            if (swallowed == nullptr || swallowed->roof_gap_areas.empty())
                continue;
            for (size_t carrier : fold_carriers_above(swallowed_nr))
                append(swallowed_gap_by_carrier[int(carrier)], swallowed->roof_gap_areas);
        }

    tbb::parallel_for(tbb::blocked_range<size_t>(0, m_ts_data->layer_heights.size()),
        [&](const tbb::blocked_range<size_t>& range)
        {
            for (size_t layer_nr = range.begin(); layer_nr < range.end(); layer_nr++)
            {
                if (print->canceled())
                    break;

                SupportLayer* ts_layer = m_object->get_support_layer(layer_nr + m_raft_layers);
                if (contact_nodes[layer_nr].empty() || ts_layer->height < EPSILON)
                    continue;

                LayerDrawData &data = layer_draw_data[layer_nr];
                const size_t obj_layer_nr           = data.obj_layer_nr;
                const coordf_t max_layers_above_base  = data.max_layers_above_base;
                const coordf_t max_layers_above_roof  = data.max_layers_above_roof;
                const coordf_t max_layers_above_roof1 = data.max_layers_above_roof1;
                const int interface_id              = data.interface_id;
                const ExPolygons &collision_base    = data.collision_base;
                const ExPolygons &area_poly         = data.area_poly;
                const ExPolygons &extra_wall_area   = data.extra_wall_area;
                const ExPolygons &cooldown_area     = data.cooldown_area;
                ExPolygons& base_areas = ts_layer->base_areas;
                ExPolygons& roof_areas = ts_layer->roof_areas;
                ExPolygons& roof_1st_layer = ts_layer->roof_1st_layer;
                ExPolygons& floor_areas = ts_layer->floor_areas;
                ExPolygons& roof_gap_areas = ts_layer->roof_gap_areas;

                // The gap under an overhang may carry no material at all, so it carves the base below. A folded
                // carrier answers for its whole slab span, hence the gaps the swallowed layers left behind are pulled
                // in as well. The interface must not be carved: the gap areas are widened overhangs, which on a sloped
                // underside cover every interface line next to the wall; union_outlines_within_top_z() keeps its gap.
                ExPolygons gap_keep_out = roof_gap_areas;
                if (!m_fold_carriers.empty()) {
                    const auto swallowed_it = swallowed_gap_by_carrier.find(int(layer_nr));
                    if (swallowed_it != swallowed_gap_by_carrier.end())
                        append(gap_keep_out, swallowed_it->second);
                }

                ExPolygons roofs; append(roofs, roof_1st_layer); append(roofs, roof_areas);append(roofs, std::move(gap_keep_out));

                // slightly slow down the slicing speed
                base_areas = diff_ex(base_areas, collision_base.empty() ? m_ts_data->get_collision(0, obj_layer_nr) : collision_base);

                base_areas = diff_ex(base_areas, ClipperUtils::clip_clipper_polygons_with_subject_bbox(roofs, get_extents(base_areas)));
                base_areas = intersection_ex(base_areas, m_machine_border);

                if (SQUARE_SUPPORT) {
                    // simplify support contours
                    ExPolygons base_areas_simplified;
                    for (auto &area : base_areas) { area.simplify(scale_(support_extrusion_width / 2), &base_areas_simplified); }
                    base_areas = std::move(base_areas_simplified);
                }
                //Subtract support floors. We can only compute floor_areas here instead of with roof_areas,
                // or we'll get much wider floor than necessary.
                if (bottom_interface_layers + bottom_gap_layers > 0)
                {
                    if (layer_nr >= bottom_interface_layers + bottom_gap_layers)
                    {
                        // find the lowest interface layer
                        // TODO the gap may not be exact when "independent support layer height" is enabled
                        size_t layer_nr_next = layer_nr - bottom_interface_layers;
                        size_t obj_layer_nr_next = m_ts_data->layer_heights[layer_nr_next].obj_layer_nr;
                        for (size_t i = 0; i <= bottom_gap_layers && i <= obj_layer_nr_next; i++)
                        {
                            const Layer *below_layer      = m_object->get_layer(obj_layer_nr_next - i);
                            ExPolygons bottom_interface = std::move(intersection_ex(base_areas, below_layer->lslices));
                            floor_areas.insert(floor_areas.end(), bottom_interface.begin(), bottom_interface.end());
                        }
                    }
                    if (floor_areas.empty() == false) {
                        //floor_areas = std::move(diff_ex(floor_areas, avoid_region_interface));
                        //floor_areas = std::move(offset2_ex(floor_areas, contact_dist_scaled, -contact_dist_scaled));
                        base_areas = std::move(diff_ex(base_areas, offset_ex(floor_areas, 10)));
                    }
                }
                if (bottom_gap_layers > 0 && m_ts_data->layer_heights[layer_nr].obj_layer_nr > bottom_gap_layers) {
                    const Layer* below_layer = m_object->get_layer(m_ts_data->layer_heights[layer_nr].obj_layer_nr - bottom_gap_layers);
                    ExPolygons bottom_gap_area = std::move(intersection_ex(floor_areas, below_layer->lslices));
                    if (!bottom_gap_area.empty()) {
                        floor_areas = std::move(diff_ex(floor_areas, bottom_gap_area));
                    }
                }
                // prune_floating_supports() pairs each area_group back with its region by walking the vector below
                // in step with the group order, so the fills that follow have to keep appending in vector order
                // and nothing may reorder or drop entries on either side afterwards.
                auto &area_groups = ts_layer->area_groups;
                for (auto& expoly : ts_layer->base_areas) {
                    //if (area(expoly) < SQ(scale_(1))) continue;
                    area_groups.emplace_back(&expoly, SupportLayer::BaseType, max_layers_above_base);
                    area_groups.back().need_infill = overlaps({ expoly }, area_poly);
                    bool need_extra_wall = overlaps({expoly},extra_wall_area);
                    area_groups.back().need_extra_wall = need_extra_wall && !area_groups.back().need_infill;
                    area_groups.back().need_cooling = overlaps({ expoly }, cooldown_area);
                    // The whole island takes the widest wall among the widened disks that overlap it.
                    const BoundingBox expoly_bbox = get_extents(expoly);
                    for (const WidenedArea &widened : data.widened_areas)
                        if (widened.bbox.overlap(expoly_bbox) && widened.area.overlaps(expoly)) {
                            area_groups.back().line_width = widened.width;
                            break;
                        }
                }
                ExPolygons new_roofs;
                for (auto &expoly : ts_layer->roof_areas) {
                    if (area(expoly) < SQ(scale_(1))) {
                        if (max_layers_above_roof > EPSILON) { 
                            ts_layer->roof_1st_layer.push_back(expoly); 
                        }
                        continue;
                    }
                    new_roofs.push_back(expoly);
                }
                roof_areas = std::move(new_roofs);
                for (auto& expoly : ts_layer->roof_areas) {
                    area_groups.emplace_back(&expoly, SupportLayer::RoofType, max_layers_above_roof);
                    area_groups.back().interface_id = interface_id;
                }
                for (auto &expoly : ts_layer->floor_areas) {
                    //if (area(expoly) < SQ(scale_(1))) continue;
                    area_groups.emplace_back(&expoly, SupportLayer::FloorType, 10000);
                }
                for (auto &expoly : ts_layer->roof_1st_layer) {
                    //if (area(expoly) < SQ(scale_(1))) continue;
                    area_groups.emplace_back(&expoly, SupportLayer::Roof1stLayer, max_layers_above_roof1);
                }

                // A folded bed band reaches the bed from its carrier, which may sit well above the first layer, and the
                // brim keeps off the support by these islands.
                const bool keep_support_islands = layer_nr < brim_skirt_layers ||
                    (has_brim && m_raft_layers == 0 && !m_fold_carriers.empty() &&
                     ts_layer->print_z - ts_layer->effective_print_height() <= EPSILON);
                for (auto &area_group : area_groups) {
                    auto& expoly = area_group.area;
                    expoly->holes.erase(std::remove_if(expoly->holes.begin(), expoly->holes.end(),
                                                       [](auto &hole) {
                                                           auto bbox_size = get_extents(hole).size();
                                                           return bbox_size[0] < scale_(2) && bbox_size[1] < scale_(2);
                                                       }),
                                        expoly->holes.end());

                    if (keep_support_islands)
                        ts_layer->support_islands.emplace_back(*expoly);
                }

                ts_layer->support_islands = std::move(union_ex(ts_layer->support_islands));
                //Must update bounding box which is used in avoid crossing perimeter
                ts_layer->lslices_bboxes.clear();
                ts_layer->lslices_bboxes.reserve(ts_layer->support_islands.size());
                for (const ExPolygon& expoly : ts_layer->support_islands)
                    ts_layer->lslices_bboxes.emplace_back(get_extents(expoly));
                ts_layer->backup_untyped_slices();

            }
        });

        // prune_floating_supports() is not called. Its carrier test deletes bed-landing
        // supports that still reach the plate, so those supports come out floating.
        resolve_material_covered_by_folded_interfaces();

        if (with_lightning_infill)
        {
            std::vector<Polygons> contours;
            std::vector<Polygons> overhangs;
            // BBS multi-diameter: a swallowed layer is not a gap in the support - its material moved up to
            // the slab carrier. FillLightning::Generator derives its prune and straightening lengths from a
            // single object_config.layer_height and treats consecutive entries as adjacent layers, so
            // skipping the swallowed ones would hand it a stack whose steps are a whole band apart and it
            // would join trees across that band with one straight segment. Feeding the footprint of every
            // carrier spanning those layers keeps the stack one nominal layer per step: inside a column's slab
            // the footprint does not change, so no spurious overhang appears, and the real one still shows up
            // at the boundary.
            auto effective_base = [this](int layer_nr) -> ExPolygons {
                if (layer_nr < 0)
                    return {};
                SupportLayer *ts = m_object->get_support_layer(layer_nr + m_raft_layers);
                const std::vector<size_t> spanning = fold_carriers_above(size_t(layer_nr));
                if (spanning.empty())
                    return ts == nullptr || ts->area_groups.empty() ? ExPolygons() : ts->base_areas;
                // A layer inside a folded slab contributes its own regions only while it still prints a column
                // of its own - a carrier, or a column whose carrier refused. Reading the footprint of a layer
                // without one would make the stack jump from a sliver to the full slab and invent an overhang.
                const bool own_column = std::any_of(contact_nodes[layer_nr].begin(), contact_nodes[layer_nr].end(),
                    [](const SupportNode *node) { return node != nullptr && node->distance_to_top >= 0; });
                ExPolygons footprint;
                if (own_column && ts != nullptr && ! ts->area_groups.empty())
                    footprint = ts->base_areas;
                for (size_t carrier_nr : spanning)
                    if (const SupportLayer *carrier = m_object->get_support_layer(carrier_nr + m_raft_layers); carrier != nullptr)
                        append(footprint, carrier->base_areas);
                return union_ex(footprint);
            };
            // Each layer is read up to three times below, and a folded footprint is a union, so build them once.
            // Unfolded, the layer's own base_areas are read in place.
            std::vector<ExPolygons> folded_base;
            if (! m_fold_carriers.empty()) {
                folded_base.resize(contact_nodes.size());
                tbb::parallel_for(tbb::blocked_range<size_t>(0, folded_base.size()), [&](const tbb::blocked_range<size_t> &range) {
                    for (size_t layer_nr = range.begin(); layer_nr < range.end(); ++ layer_nr)
                        folded_base[layer_nr] = effective_base(int(layer_nr));
                });
            }
            const ExPolygons no_base;
            auto base_of = [this, &folded_base, &no_base](int layer_nr) -> const ExPolygons & {
                if (! folded_base.empty())
                    return folded_base[layer_nr];
                const SupportLayer *ts = m_object->get_support_layer(layer_nr + m_raft_layers);
                return ts == nullptr || ts->area_groups.empty() ? no_base : ts->base_areas;
            };
            // A roof-only layer still has to be visited: its empty base turns the whole base below it into
            // overhang, which is what grows the lightning tree up under the roof.
            auto has_nothing = [this, &base_of](int layer_nr) {
                if (! base_of(layer_nr).empty())
                    return false;
                const SupportLayer *ts = m_object->get_support_layer(layer_nr + m_raft_layers);
                return ts == nullptr || ts->area_groups.empty();
            };
            for (int layer_nr = 1; layer_nr < contact_nodes.size(); layer_nr++) {
                if (print->canceled()) break;

                // skip if current layer has no points. This fixes potential crash in get_collision (see jira BBL001-355)
                if (has_nothing(layer_nr)) continue;
                const ExPolygons &base_areas = base_of(layer_nr);

                int layer_nr_lower = layer_nr - 1;
                for (; layer_nr_lower >= 0; layer_nr_lower--) {
                    if (! has_nothing(layer_nr_lower)) break;
                }
                if (layer_nr_lower <= 0) continue;

                SupportLayer* lower_layer = m_object->get_support_layer(layer_nr_lower + m_raft_layers);
                const ExPolygons &base_areas_lower = base_of(layer_nr_lower);

                ExPolygons overhang = diff_ex(offset_ex_2(base_areas_lower, -2.5 * line_width_scaled), base_areas);
                for (auto it = overhang.begin(); it != overhang.end();) {
                    it->remove_colinear_points();
                    if (it->empty())
                    it = overhang.erase(it);
                    else ++it;
                }


                overhangs.emplace_back(to_polygons(overhang));
                contours.emplace_back(to_polygons(base_areas_lower));
                // draw_circles() zeroes print_z on a layer that got no node, which is every swallowed layer,
                // so key the entry by the Z the plan gave it. Those entries are never looked up - a
                // swallowed layer extrudes nothing - but a shared key of 0 would let them overwrite each
                // other and, worse, collide with a layer that really does print at 0.
                const coordf_t lower_print_z = size_t(layer_nr_lower) < m_ts_data->layer_heights.size() ?
                    m_ts_data->layer_heights[layer_nr_lower].print_z : lower_layer->print_z;
                printZ_to_lightninglayer[lower_print_z] = overhangs.size() - 1;

#ifdef SUPPORT_TREE_DEBUG_TO_SVG
                if (!overhang.empty() && !base_areas_lower.empty()) {
                    std::string fname = debug_out_path("lightning_%d_%.2f.svg", layer_nr, lower_print_z);
                    SVG::export_expolygons(fname, {{base_areas_lower, {"base_areas_lower", "red", 0.5}}, {overhang, {"overhang", "blue", 0.5}}});
                }
#endif
            }


            auto m_support_material_flow = support_material_flow(m_object, m_slicing_params.layer_height);
            coordf_t support_spacing = m_object_config->support_base_pattern_spacing.value + m_support_material_flow.spacing();
            coordf_t support_density = std::min(1., m_support_material_flow.spacing() / support_spacing * 2); // for lightning infill the density is defined differently, so need to double it
            generator = std::make_unique<FillLightning::Generator>(m_object, contours, overhangs, []() {}, support_density);
        }

        else if (!with_infill) {
            // move the holes to contour so they can be well supported

            // check if poly's contour intersects with expoly's contour
            auto intersects_contour = [](Polygon poly, ExPolygon expoly, Point& pt_on_poly, Point& pt_on_expoly, Point& pt_far_on_poly, float dist_thresh = 0.01) {
                Polylines pl_out = intersection_pl(to_polylines(expoly), ExPolygon(poly));
                if (pl_out.empty()) return false;
                float min_dist = std::numeric_limits<float>::max();
                float max_dist = 0;
                for (auto from : poly.points) {
                    for (int i = 0; i < expoly.num_contours(); i++) {
                        const Point* candidate = expoly.contour_or_hole(i).closest_point(from);
                        double       dist2 = vsize2_with_unscale(*candidate - from);
                        if (dist2 < min_dist) {
                            min_dist = dist2;
                            pt_on_poly = from;
                            pt_on_expoly = *candidate;
                            }
                        if (dist2 > max_dist) {
                            max_dist = dist2;
                            pt_far_on_poly = from;
                            }
                        if (dist2 < dist_thresh) { return true; }
                        }
                    }
                return false;
                };

            // polygon pointer: depth, direction, farPoint
            std::map<const Polygon*, std::tuple<int, Point, Point>> holePropagationInfos;
            for (int layer_nr = contact_nodes.size() - 1; layer_nr > 0; layer_nr--) {
                if (print->canceled()) break;
                //m_object->print()->set_status(66, (boost::format(_u8L("Support: fix holes at layer %d")) % layer_nr).str());

                const std::vector<SupportNode*>& curr_layer_nodes = contact_nodes[layer_nr];
                SupportLayer* ts_layer = m_object->get_support_layer(layer_nr + m_raft_layers);
                assert(ts_layer != nullptr);

                // skip if current layer has no points. This fixes potential crash in get_collision (see jira BBL001-355)
                if (curr_layer_nodes.empty()) continue;
                if (ts_layer->height < EPSILON) continue;
                if (ts_layer->area_groups.empty()) continue;

                int layer_nr_lower = layer_nr - 1;
                for (layer_nr_lower; layer_nr_lower >= 0; layer_nr_lower--) {
                    if (!m_object->get_support_layer(layer_nr_lower + m_raft_layers)->area_groups.empty()) break;
                }
                if (layer_nr_lower < 0) continue;
                auto& area_groups_lower = m_object->get_support_layer(layer_nr_lower + m_raft_layers)->area_groups;

                for (const auto& area_group : ts_layer->area_groups) {
                    if (area_group.type != SupportLayer::BaseType) continue;
                    const auto& area = area_group.area;
                    for (const auto& hole : area->holes) {
                        // auto hole_bbox = get_extents(hole).polygon();
                        for (auto& area_group_lower : area_groups_lower) {
                            auto& base_area_lower = *area_group_lower.area;
                            Point pt_on_poly, pt_on_expoly, pt_far_on_poly;
                            // if a hole doesn't intersect with lower layer's contours, add a hole to lower layer and move it slightly to the contour
                            if (base_area_lower.contour.contains(hole.points.front()) &&
                                !intersects_contour(hole, base_area_lower, pt_on_poly, pt_on_expoly, pt_far_on_poly,
                                                    0.01 + unscale_(line_width_scaled) * (area_group.need_extra_wall ? 2. : 0.5))) {
                                Polygon hole_lower = hole;
                                Point   direction  = normal(pt_on_expoly - pt_on_poly, line_width_scaled / 2);
                                hole_lower.translate(direction);
                                // note to expand a hole, we need to do negative offset
                                auto hole_expanded = offset(hole_lower, -line_width_scaled / 4, ClipperLib::JoinType::jtSquare);
                                if (!hole_expanded.empty()) {
                                    base_area_lower.holes.push_back(std::move(hole_expanded[0]));
                                    holePropagationInfos.insert({&base_area_lower.holes.back(), {25, direction, pt_far_on_poly}});
                                }
                                break;
                            }
                            else if (holePropagationInfos.find(&hole) != holePropagationInfos.end() && std::get<0>(holePropagationInfos[&hole]) > 0 &&
                                base_area_lower.contour.contains(std::get<2>(holePropagationInfos[&hole]))) {
                                // after the hole connects to contour, shrink it gradually until it vanishes while moving it outwards. The moving direction is defined in the previous step
                                Polygon hole_lower = hole;
                                auto&& direction = std::get<1>(holePropagationInfos[&hole]);
                                hole_lower.translate(direction);
                                // note to shrink a hole, we need to do positive offset
                                auto  hole_expanded = offset(hole_lower, line_width_scaled / 2, ClipperLib::JoinType::jtSquare);
                                Point farPoint = std::get<2>(holePropagationInfos[&hole]) + direction * 2;
                                if (!hole_expanded.empty()) {
                                    base_area_lower.holes.push_back(std::move(hole_expanded[0]));
                                    holePropagationInfos.insert({ &base_area_lower.holes.back(), {std::get<0>(holePropagationInfos[&hole]) - 1, direction, farPoint} });
                                    }
                                break;
                                }
                            }
                        {
                        // if roof1 interface is inside a hole, need to expand the interface
                        for (auto& roof1 : ts_layer->roof_1st_layer) {
                            //if (hole.contains(roof1.contour.points.front()) && hole.contains(roof1.contour.bounding_box().center()))
                            bool is_inside_hole = std::all_of(roof1.contour.points.begin(), roof1.contour.points.end(), [&hole](Point& pt) { return hole.contains(pt); });
                            if (is_inside_hole) {
                                Polygon hole_reoriented = hole;
                                if (roof1.contour.is_counter_clockwise())
                                    hole_reoriented.make_counter_clockwise();
                                else if (roof1.contour.is_clockwise())
                                    hole_reoriented.make_clockwise();
                                auto tmp = union_({ roof1.contour ,  hole_reoriented });
                                if (!tmp.empty()) roof1.contour = tmp[0];

                                // make sure 1) roof1 and object 2) roof1 and roof, won't intersect
                                // Note: We can't replace roof1 directly, as we have recorded its address.
                                //       So instead we need to replace its members one by one.
                                auto tmp1 = diff_ex(roof1, get_collision(0, m_ts_data->layer_heights[layer_nr].obj_layer_nr));
                                tmp1 = diff_ex(tmp1, ts_layer->roof_areas);
                                if (!tmp1.empty()) {
                                    roof1.contour = std::move(tmp1[0].contour);
                                    roof1.holes = std::move(tmp1[0].holes);
                                    }
                                break;
                                }
                            }
                        }
                        }
                    }
                }
            }


#ifdef SUPPORT_TREE_DEBUG_TO_SVG
    for (int layer_nr = m_object->support_layer_count() - 1; layer_nr >= 0; layer_nr--) {
        SupportLayer* ts_layer = m_object->get_support_layer(layer_nr + m_raft_layers);
        ExPolygons& base_areas = ts_layer->base_areas;
        ExPolygons& roof_areas = ts_layer->roof_areas;
        ExPolygons& roof_1st_layer = ts_layer->roof_1st_layer;
        ExPolygons roofs = roof_areas; append(roofs, roof_1st_layer);
        if (base_areas.empty() && roof_areas.empty() && roof_1st_layer.empty()) continue;
        draw_contours_and_nodes_to_svg(debug_out_path("circles_%d_%.2f.svg", layer_nr, ts_layer->print_z), base_areas, roofs, ts_layer->lslices_extrudable, {}, {}, {"base", "roof", "lslices"}, {"blue","red","black"});
    }
#endif  // SUPPORT_TREE_DEBUG_TO_SVG

    SupportLayerPtrs& ts_layers = m_object->support_layers();
    // Only remove empty non-raft support layers. Raft layers (first m_raft_layers entries)
    // must be preserved to avoid support_layer_count < m_raft_layers mismatch.
    auto iter = std::remove_if(ts_layers.begin() + m_raft_layers, ts_layers.end(), [](SupportLayer* ts_layer) { return ts_layer->height < EPSILON; });
    ts_layers.erase(iter, ts_layers.end());
    for (int layer_nr = 0; layer_nr < ts_layers.size(); layer_nr++) {
        ts_layers[layer_nr]->upper_layer = layer_nr != ts_layers.size() - 1 ? ts_layers[layer_nr + 1] : nullptr;
        ts_layers[layer_nr]->lower_layer = layer_nr > 0 ? ts_layers[layer_nr - 1] : nullptr;
    }
}

// Area of a region that the carrier below holds up. The bounding boxes come from get_extents_vector(carrier)
// and spare the clipper boolean the candidates that are nowhere near the region. Those that are near are trimmed
// to the region's own box before the boolean: the carrier covers the whole layer below and takes its outline from
// the model slices, so feeding it whole would drag thousands of vertices through every one of the layer's regions.
static double carried_area(const ExPolygons &carrier, const std::vector<BoundingBox> &carrier_bboxes, const ExPolygon &region)
{
    if (carrier.empty()) return 0.;
    const BoundingBox region_bbox = get_extents(region);
    Polygons          near_carrier;
    for (size_t i = 0; i < carrier.size(); i++)
        if (carrier_bboxes[i].overlap(region_bbox))
            append(near_carrier, ClipperUtils::clip_clipper_polygons_with_subject_bbox(carrier[i], region_bbox));
    if (near_carrier.empty()) return 0.;
    double area = 0.;
    for (const ExPolygon &part : intersection_ex(region, near_carrier)) area += part.area();
    return area;
}

// Fraction of a region that the carrier below holds up.
static double carried_area_ratio(const ExPolygons &carrier, const std::vector<BoundingBox> &carrier_bboxes, const ExPolygon &region)
{
    const double region_area = region.area();
    if (region_area <= 0.) return 0.;
    return carried_area(carrier, carrier_bboxes, region) / region_area;
}

void TreeSupport::resolve_material_covered_by_folded_interfaces()
{
    if (m_fold_carriers.empty())
        return;
    SupportLayerPtrs &layers = m_object->support_layers();
    if (layers.size() <= size_t(m_raft_layers))
        return;

    auto sync_support_islands = [](SupportLayer &layer) {
        if (layer.support_islands.empty())
            return;
        ExPolygons islands;
        islands.reserve(layer.area_groups.size());
        for (const SupportLayer::AreaGroup &area_group : layer.area_groups)
            islands.emplace_back(*area_group.area);
        layer.support_islands = union_ex(islands);
        layer.lslices_bboxes.clear();
        layer.lslices_bboxes.reserve(layer.support_islands.size());
        for (const ExPolygon &expoly : layer.support_islands)
            layer.lslices_bboxes.emplace_back(get_extents(expoly));
    };
    static_assert(SupportLayer::BaseType == 0 && SupportLayer::RoofType == 1 &&
                  SupportLayer::FloorType == 2 && SupportLayer::Roof1stLayer == 3,
                  "regions_by_type must be indexed by SupportLayer::AreaType");
    auto is_folded_interface = [](const SupportLayer &layer) {
        return layer.interface_slab_height > layer.height + EPSILON &&
               (! layer.roof_areas.empty() || ! layer.roof_1st_layer.empty());
    };
    auto interface_height = [](const SupportLayer &layer) {
        return layer.interface_slab_height > EPSILON ? layer.interface_slab_height : layer.height;
    };
    auto base_height = [](const SupportLayer &layer) {
        return layer.base_slab_height > EPSILON ? layer.base_slab_height : layer.height;
    };
    auto trim_layer = [&](SupportLayer &layer, const ExPolygons &base_cut,
                          const ExPolygons &interface_cut) {
        if (base_cut.empty() && interface_cut.empty())
            return false;

        constexpr int num_area_types = SupportLayer::Roof1stLayer + 1;
        ExPolygons *regions_by_type[num_area_types] {
            &layer.base_areas, &layer.roof_areas, &layer.floor_areas, &layer.roof_1st_layer };
        ExPolygons kept_regions[num_area_types];
        const std::vector<BoundingBox> base_cut_bboxes = get_extents_vector(base_cut);
        const std::vector<BoundingBox> interface_cut_bboxes = get_extents_vector(interface_cut);
        std::vector<SupportLayer::AreaGroup> kept_groups;
        kept_groups.reserve(layer.area_groups.size());
        bool changed = false;
        size_t source_index[num_area_types] {};
        for (const SupportLayer::AreaGroup &area_group : layer.area_groups) {
            const int type = area_group.type;
            if (type < SupportLayer::BaseType || type > SupportLayer::Roof1stLayer)
                continue;
            const size_t index = source_index[type] ++;
            const bool mapping_valid =
                index < regions_by_type[type]->size() &&
                area_group.area == regions_by_type[type]->data() + index;
            assert(mapping_valid);
            if (! mapping_valid) {
                BOOST_LOG_TRIVIAL(error) << "Cannot trim folded support overlap: stale area-group mapping";
                return false;
            }

            const ExPolygons *cut = type == SupportLayer::BaseType ? &base_cut :
                (type == SupportLayer::RoofType || type == SupportLayer::Roof1stLayer) ? &interface_cut : nullptr;
            const std::vector<BoundingBox> *cut_bboxes = type == SupportLayer::BaseType ? &base_cut_bboxes :
                (type == SupportLayer::RoofType || type == SupportLayer::Roof1stLayer) ?
                    &interface_cut_bboxes : nullptr;
            const BoundingBox area_bbox = get_extents(*area_group.area);
            ExPolygons local_cut;
            if (cut != nullptr)
                for (size_t cut_idx = 0; cut_idx < cut->size(); ++ cut_idx)
                    if ((*cut_bboxes)[cut_idx].overlap(area_bbox))
                        local_cut.emplace_back((*cut)[cut_idx]);
            ExPolygons pieces = ! local_cut.empty() ?
                diff_ex(ExPolygons{ *area_group.area }, local_cut) :
                ExPolygons{ *area_group.area };
            changed |= ! local_cut.empty();

            for (ExPolygon &piece : pieces) {
                kept_regions[type].emplace_back(std::move(piece));
                SupportLayer::AreaGroup kept = area_group;
                kept.area = nullptr;
                kept_groups.emplace_back(kept);
            }
        }

        for (int type = 0; type < num_area_types; ++ type) {
            const bool mapping_complete = source_index[type] == regions_by_type[type]->size();
            assert(mapping_complete);
            if (! mapping_complete) {
                BOOST_LOG_TRIVIAL(error) << "Cannot trim folded support overlap: incomplete area-group mapping";
                return false;
            }
        }
        if (! changed)
            return false;

        for (int type = 0; type < num_area_types; ++ type)
            *regions_by_type[type] = std::move(kept_regions[type]);
        size_t next_index[num_area_types] {};
        for (SupportLayer::AreaGroup &area_group : kept_groups)
            area_group.area = &(*regions_by_type[area_group.type])[next_index[area_group.type] ++];
        layer.area_groups = std::move(kept_groups);

        if (layer.roof_areas.empty() && layer.roof_1st_layer.empty()) {
            layer.planned_interface_filament_id      = 0;
            layer.planned_interface_nozzle_diameter  = 0.;
            layer.interface_slab_height              = 0.;
        }
        if (layer.base_areas.empty()) {
            layer.planned_filament_id      = 0;
            layer.planned_nozzle_diameter  = 0.;
            layer.base_slab_height         = 0.;
        }
        sync_support_islands(layer);
        return true;
    };

    std::vector<ExPolygons> base_cut(layers.size());
    std::vector<ExPolygons> interface_cut(layers.size());
    size_t trimmed_layers = 0;
    for (size_t source_idx = layers.size(); source_idx -- > size_t(m_raft_layers);) {
        SupportLayer *source = layers[source_idx];
        if (source == nullptr)
            continue;
        if (trim_layer(*source, base_cut[source_idx], interface_cut[source_idx]))
            ++ trimmed_layers;

        if (! source->base_areas.empty() &&
            (! source->roof_areas.empty() || ! source->roof_1st_layer.empty())) {
            ExPolygons base_priority_cut = source->roof_areas;
            append(base_priority_cut, source->roof_1st_layer);
            if (! base_priority_cut.empty() &&
                get_extents(source->base_areas).overlap(get_extents(base_priority_cut)) &&
                ! intersection_ex(source->base_areas, base_priority_cut).empty() &&
                trim_layer(*source, base_priority_cut, {}))
                ++ trimmed_layers;
        }

        // Nested folded bases share a complete Z interval, so the outer carrier already extrudes their XY
        // intersection. Trim that duplicate from the shorter layer using the final rendered geometry. True
        // partial-Z overlaps and interface-over-base conflicts were rejected before folding.
        if (source->base_slab_height > source->height + EPSILON && ! source->base_areas.empty()) {
            const coordf_t source_top    = source->print_z;
            const coordf_t source_bottom = source_top - source->base_slab_height;
            // 第二轮合并层让开段内保留细层的接触面/转换层：段内细层主体托住它们，粗层在其投影处留空（第一轮合并前已检查过）。
            if (std::binary_search(m_base_only_fold_layers.begin(), m_base_only_fold_layers.end(),
                                   source_idx - size_t(m_raft_layers))) {
                const BoundingBox base_bbox = get_extents(source->base_areas);
                ExPolygons        fine_interface;
                for (size_t victim_idx = source_idx; victim_idx -- > size_t(m_raft_layers);) {
                    const SupportLayer *victim = layers[victim_idx];
                    if (victim == nullptr || victim->height < EPSILON)
                        continue;
                    if (victim->print_z <= source_bottom + EPSILON)
                        break;
                    for (const ExPolygons *areas : { &victim->roof_areas, &victim->roof_1st_layer })
                        for (const ExPolygon &area : *areas)
                            if (base_bbox.overlap(get_extents(area)))
                                fine_interface.emplace_back(area);
                }
                if (! fine_interface.empty() && trim_layer(*source, union_ex(fine_interface), {}))
                    ++ trimmed_layers;
            }
            const ExPolygons &source_base = source->base_areas;
            const BoundingBox source_bbox = get_extents(source_base);

            for (size_t victim_idx = source_idx; victim_idx -- > size_t(m_raft_layers);) {
                if (m_object->print()->canceled())
                    return;
                const SupportLayer *victim = layers[victim_idx];
                if (victim == nullptr || victim->height < EPSILON || victim->base_areas.empty())
                    continue;
                const coordf_t victim_top    = victim->print_z;
                const coordf_t victim_bottom = victim_top - base_height(*victim);
                if (victim_top <= source_bottom + EPSILON)
                    break;
                if (! source_bbox.overlap(get_extents(victim->base_areas)))
                    continue;

                ExPolygons overlap = intersection_ex(victim->base_areas, source_base);
                if (overlap.empty())
                    continue;
                if (source_bottom <= victim_bottom + EPSILON &&
                    source_top >= victim_top - EPSILON)
                    append(base_cut[victim_idx], std::move(overlap));
                else
                    BOOST_LOG_TRIVIAL(warning)
                        << "Folded support base overlaps only part of a base Z span: source z=["
                        << source_bottom << ',' << source_top << "], victim z=["
                        << victim_bottom << ',' << victim_top << "]; keeping both regions";
            }
        }
        if (! is_folded_interface(*source))
            continue;

        const coordf_t source_top    = source->print_z;
        const coordf_t source_bottom = source_top - source->interface_slab_height;
        ExPolygons footprint = source->roof_areas;
        append(footprint, source->roof_1st_layer);
        if (footprint.empty())
            continue;
        const BoundingBox footprint_bbox = get_extents(footprint);

        auto collect_cut = [&](size_t victim_idx, coordf_t victim_height,
                               const ExPolygons &victim_areas, ExPolygons &cut,
                               const char *victim_type) {
            if (victim_height < EPSILON || victim_areas.empty())
                return;
            const coordf_t victim_top    = layers[victim_idx]->print_z;
            const coordf_t victim_bottom = victim_top - victim_height;
            if (std::min(source_top, victim_top) <= std::max(source_bottom, victim_bottom) + EPSILON)
                return;
            if (! footprint_bbox.overlap(get_extents(victim_areas)))
                return;

            ExPolygons xy_overlap = intersection_ex(victim_areas, footprint);
            if (xy_overlap.empty())
                return;
            const bool fully_covered =
                source_bottom <= victim_bottom + EPSILON && source_top >= victim_top - EPSILON;
            if (fully_covered) {
                append(cut, std::move(xy_overlap));
                return;
            }

            // Keep both regions in this unexpected fallback path. The carrier precheck handles known
            // partial overlaps; aborting here is worse than accepting localized over-extrusion.
            BOOST_LOG_TRIVIAL(warning)
                << "Folded support interface overlaps only part of a " << victim_type
                << " Z span: source z=[" << source_bottom << ',' << source_top
                << "], victim z=[" << victim_bottom << ',' << victim_top
                << "]; keeping both regions";
        };

        collect_cut(source_idx, base_height(*source), source->base_areas,
                    base_cut[source_idx], "base");

        for (size_t victim_idx = source_idx; victim_idx -- > size_t(m_raft_layers);) {
            if (m_object->print()->canceled())
                return;
            const SupportLayer *victim = layers[victim_idx];
            if (victim == nullptr || victim->height < EPSILON)
                continue;

            const coordf_t victim_top = victim->print_z;
            if (victim_top <= source_bottom + EPSILON)
                break;
            collect_cut(victim_idx, interface_height(*victim), victim->roof_areas,
                        interface_cut[victim_idx], "interface");
            collect_cut(victim_idx, interface_height(*victim), victim->roof_1st_layer,
                        interface_cut[victim_idx], "interface");
            collect_cut(victim_idx, base_height(*victim), victim->base_areas,
                        base_cut[victim_idx], "base");
        }

        if (! base_cut[source_idx].empty()) {
            if (trim_layer(*source, base_cut[source_idx], {}))
                ++ trimmed_layers;
            base_cut[source_idx].clear();
        }
    }

    BOOST_LOG_TRIVIAL(debug) << "trimmed material covered by folded support interfaces on "
                             << trimmed_layers << " layers";
}

void TreeSupport::prune_floating_supports()
{
    SupportLayerPtrs &ts_layers = m_object->support_layers();
    if (m_object->layers().empty() || ts_layers.size() <= size_t(m_raft_layers)) return;

    // Regions are stored in one vector per area type, and area_groups holds raw pointers into those vectors, so
    // both are pruned together and the pointers re-bound afterwards. The per type arrays below index by AreaType.
    constexpr int num_area_types = SupportLayer::Roof1stLayer + 1;
    static_assert(SupportLayer::BaseType == 0 && SupportLayer::RoofType == 1 && SupportLayer::FloorType == 2 && SupportLayer::Roof1stLayer == 3,
                  "regions_by_type must be indexed by SupportLayer::AreaType");

    // A support layer is carried either by the support printed right below it, or by the model it came to rest
    // on. That model top sits gap_object_support below the support bottom, since the gap is left on purpose.
    const PrintObjectConfig &config             = m_object->config();
    const coordf_t           gap_object_support = std::max(0., m_slicing_params.gap_object_support);
    // Support standing on the plate is carried by definition, and so is support resting on the raft, whose top
    // is not among the object slices searched below. This cannot be keyed off the lowest entry left in ts_layers:
    // draw_circles() drops the layers that got no node, so that entry may well be the first one up in the air.
    const coordf_t           grounded_z         = m_raft_layers > 0 ? ts_layers[m_raft_layers - 1]->print_z : 0.;

    const double branch_angle = config.tree_support_branch_angle.value * M_PI / 180.;
    // Branches may then move freely, so no region can be told apart as unsupported.
    if (branch_angle >= M_PI / 2) return;
    const double   tan_branch_angle        = tan(branch_angle);
    const int      wall_count              = std::max(1, config.tree_support_wall_count.value);
    // Footprint of the thinnest branch the tree will ever print, and hence the least support that can still be
    // standing under a region rather than touching it by accident.
    const double   min_anchor_area         = M_PI * SQ(scaled<double>(MIN_BRANCH_RADIUS));

    std::vector<coordf_t> obj_print_z;
    obj_print_z.reserve(m_object->layers().size());
    for (const Layer *object_layer : m_object->layers()) obj_print_z.emplace_back(object_layer->print_z);

    size_t     num_pruned = 0;
    ExPolygons support_below; // everything printed on the previous support layer
    for (size_t layer_nr = m_raft_layers; layer_nr < ts_layers.size(); layer_nr++) {
        if (m_object->print()->canceled()) return;

        SupportLayer *ts_layer = ts_layers[layer_nr];
        // A layer that got no node keeps a zero height and is never printed, so it neither carries what is
        // above it nor interrupts the layer that does.
        if (ts_layer->height < EPSILON) continue;
        // Only what sits near this layer's own regions can carry them, so the candidates are clipped to their
        // bounding box below. It stays undefined when the layer holds no region at all, or none with an extent,
        // and either way there is nothing here to prune and nothing to carry the layer above.
        BoundingBox layer_bbox;
        for (const SupportLayer::AreaGroup &area_group : ts_layer->area_groups) layer_bbox.merge(get_extents(*area_group.area));
        if (!layer_bbox.defined) {
            support_below.clear();
            continue;
        }

        // BBS multi-diameter: a folded layer carries a slab, so its material reaches down to the slab bottom
        // rather than to its own layer bottom, and the branch is free to drift over that whole height before
        // the layer below picks it up. Every bound here is taken from the carried height for that reason.
        const coordf_t carry_height = std::max({ts_layer->height, ts_layer->base_slab_height,
                                                ts_layer->interface_slab_height});
        const coordf_t bottom_z = ts_layer->print_z - carry_height;
        const bool     grounded = bottom_z <= grounded_z + EPSILON;

        // The support printed below and the model are both carriers, but they are not interchangeable, so the
        // support one is kept apart: an interface may rest on nothing but the tip of the branch that spawned it,
        // and that concession must not extend to a model wall the interface merely brushes past.
        ExPolygons carrier;
        ExPolygons support_carrier;
        if (!grounded) {
            // A region may stick out past whatever carries it, but only by so much. Grow the carrier by that
            // much so legitimate growth is not counted as floating. drop_nodes() caps a step two different
            // ways and this has to bound both of them, since too little slack prunes healthy branches:
            //   - get_max_move_dist() caps how far a node may shift, by the node's own layer height;
            //   - its layer wide max_move_distance is how fast a fading branch loses radius and how far a
            //     polygon node may shrink or a merged node may escape, and that one scales with the wall count.
            // Both are driven by a layer height, and this support layer's own is taken for it rather than
            // config.layer_height, since independent_support_layer_height lets it run thicker than the
            // nominal one and the bound has to hold for the thicker case.
            // The last term is the line width gained under bottom expansion: passing the carried height makes
            // support_flare_step_width() return the slab's own line width rather than one layer's share of it.
            const coordf_t flare_step_width = support_flare_step_width(ts_layer->print_z, carry_height);
            const coordf_t carry_slack = std::max({std::min(tan_branch_angle * carry_height, flare_step_width),
                                                   tan_branch_angle * carry_height * wall_count,
                                                   flare_step_width / 2.});
            // Assembling the carrier means a union and an offset over everything below, which is a clipper
            // boolean across the whole layer. Clipping first keeps that off the parts of the layer that are too
            // far away to matter: on a plate full of small parts the rest of it is pure overhead.
            layer_bbox.offset(scale_(carry_slack) + SCALED_EPSILON);

            const int top_obj_layer_nr = int(std::upper_bound(obj_print_z.begin(), obj_print_z.end(), bottom_z + EPSILON) - obj_print_z.begin()) - 1;
            // Walk down by print_z instead of by a layer count. With adaptive layer height the object layers are
            // thinner than m_slicing_params.layer_height, so a count derived from that nominal height spans less
            // than the real gap and the model actually carrying this region would be missed, pruning it away.
            // One local layer of slack on top of the gap, the support bottom need not line up with an object layer.
            const coordf_t lowest_z = top_obj_layer_nr >= 0 ? bottom_z - gap_object_support - m_object->get_layer(top_obj_layer_nr)->height : 0.;
            Polygons support_candidates = ClipperUtils::clip_clipper_polygons_with_subject_bbox(support_below, layer_bbox);
            Polygons candidates         = support_candidates;
            for (int obj_layer_nr = top_obj_layer_nr; obj_layer_nr >= 0 && obj_print_z[obj_layer_nr] >= lowest_z - EPSILON; obj_layer_nr--)
                append(candidates, ClipperUtils::clip_clipper_polygons_with_subject_bbox(m_object->get_layer(obj_layer_nr)->lslices, layer_bbox));
            carrier         = offset_ex(union_ex(candidates), scale_(carry_slack));
            support_carrier = offset_ex(union_ex(support_candidates), scale_(carry_slack));
        }
        const std::vector<BoundingBox> carrier_bboxes         = get_extents_vector(carrier);
        const std::vector<BoundingBox> support_carrier_bboxes = get_extents_vector(support_carrier);

        ExPolygons *regions_by_type[num_area_types] = {&ts_layer->base_areas, &ts_layer->roof_areas, &ts_layer->floor_areas, &ts_layer->roof_1st_layer};
        ExPolygons  kept_regions[num_area_types];
        std::vector<SupportLayer::AreaGroup> kept_groups;
        kept_groups.reserve(ts_layer->area_groups.size());
        size_t next_index[num_area_types] = {};
#ifdef SUPPORT_TREE_DEBUG_TO_SVG
        ExPolygons pruned_regions;
#endif
        for (const SupportLayer::AreaGroup &area_group : ts_layer->area_groups) {
            const int type = area_group.type;
            if (type < SupportLayer::BaseType || type > SupportLayer::Roof1stLayer) continue;
            const size_t index = next_index[type]++;
            if (index >= regions_by_type[type]->size()) continue;
            // draw_circles() fills area_groups in vector order within each type and nothing reorders either
            // side before this point. Were that to change, the pruning below would drop the wrong regions and
            // the pointers would still re-bind, leaving no trace.
            assert(area_group.area == &(*regions_by_type[type])[index]);
            ExPolygon &region = (*regions_by_type[type])[index];
            // Regions are dropped whole, never trimmed: what is kept here feeds the next layer's carrier, so
            // trimming would eat a little more off every layer and erode a healthy branch into a thread over a
            // hundred layers. A branch landing on the edge of its carrier still prints, so only drop the ones
            // mostly out in the air.
            const bool   is_interface = type == SupportLayer::RoofType || type == SupportLayer::Roof1stLayer;
            const double region_area  = region.area();
            bool         carried      = grounded;
            if (!grounded && region_area > 0.) {
                if (is_interface) {
                    // An interface spans the whole overhang that spawned it, so it cannot be held to a fraction
                    // of its own area: it routinely rests on nothing but a branch tip a fraction that wide. That
                    // concession is what MIN_CARRIED_RATIO_INTERFACE buys, and it only makes sense measured
                    // against the support below. Measured against the model too, an interface passing a wall on
                    // its way up clears the bar on a few percent of incidental overlap, and once kept it becomes
                    // the carrier for the layer above, so a single such region grows a floating stack. The model
                    // still carries an interface that comes to rest on it, but then it has to do so as squarely
                    // as a base standing on the same ledge would.
                    carried = carried_area(support_carrier, support_carrier_bboxes, region) >= MIN_CARRIED_RATIO_INTERFACE * region_area ||
                              carried_area(carrier, carrier_bboxes, region) >= MIN_CARRIED_RATIO * region_area;
                } else {
                    // A region is the union of everything the layer grew, so a branch standing on its own can end
                    // up merged with an overhang area many times its size that a node spawned this layer. Judged
                    // by fraction alone the union comes out mostly floating and the healthy branch is deleted
                    // along with it, which orphans every layer above it for the rest of the object. A carried
                    // patch as wide as the thinnest branch that can be printed is an anchor on its own, however
                    // much unsupported area happens to share the region with it. Only the support below counts
                    // as such an anchor: a region merely brushing a model wall meets it over an area that small
                    // as well, and keeping it would grow the floating stack the interface rule guards against.
                    carried = carried_area(carrier, carrier_bboxes, region) >= MIN_CARRIED_RATIO * region_area ||
                              carried_area(support_carrier, support_carrier_bboxes, region) >= min_anchor_area;
                }
            }
            if (!carried) {
                num_pruned++;
#ifdef SUPPORT_TREE_DEBUG_TO_SVG
                pruned_regions.emplace_back(region);
#endif
                continue;
            }
            kept_regions[type].emplace_back(std::move(region));
            kept_groups.emplace_back(area_group);
        }

        for (int type = 0; type < num_area_types; type++) *regions_by_type[type] = std::move(kept_regions[type]);
        size_t rebound_index[num_area_types] = {};
        for (SupportLayer::AreaGroup &area_group : kept_groups) {
            ExPolygons &regions = *regions_by_type[area_group.type];
            area_group.area     = &regions[rebound_index[area_group.type]++];
        }
        ts_layer->area_groups = std::move(kept_groups);

        // support_islands feeds retraction avoidance, keep it in sync with what is left.
        if (!ts_layer->support_islands.empty()) {
            ExPolygons islands;
            islands.reserve(ts_layer->area_groups.size());
            for (const SupportLayer::AreaGroup &area_group : ts_layer->area_groups) islands.emplace_back(*area_group.area);
            ts_layer->support_islands = union_ex(islands);
            ts_layer->lslices_bboxes.clear();
            ts_layer->lslices_bboxes.reserve(ts_layer->support_islands.size());
            for (const ExPolygon &expoly : ts_layer->support_islands) ts_layer->lslices_bboxes.emplace_back(get_extents(expoly));
        }

        support_below.clear();
        support_below.reserve(ts_layer->area_groups.size());
        for (const SupportLayer::AreaGroup &area_group : ts_layer->area_groups) support_below.emplace_back(*area_group.area);

#ifdef SUPPORT_TREE_DEBUG_TO_SVG
        if (!pruned_regions.empty())
            SVG::export_expolygons(debug_out_path("prune_floating_%d_%.2f.svg", int(layer_nr), ts_layer->print_z),
                                   {{carrier, {"carrier", "yellow", 0.5}},
                                    {support_below, {"kept", "green", 0.5}},
                                    {pruned_regions, {"pruned", "red", 0.5}}});
#endif
    }

    BOOST_LOG_TRIVIAL(info) << "Tree support floating region pruning done. " << num_pruned << " regions removed.";
}

double SupportNode::diameter_angle_scale_factor;

static void smooth_filter(const Polygon &poly, int filter_size, std::unordered_map<Point, Point, PointHash> &movements, double max_move)
{
    const Points &pts    = poly.points;
    int           n      = pts.size();
    Point         center = poly.centroid();
    Polygon       newpoly(poly);
    for (int i = 0; i < n; i++) {
        Point filter_pt(0, 0);
        for (int j = (i + n - filter_size) % n; j != (i + filter_size + 1) % n; j = (j + 1) % n) filter_pt += pts[j];
        newpoly[i] = filter_pt / (2 * filter_size + 1);
        newpoly[i] -= center;
    }
    if (newpoly.area() < SQ(scale_(10.))) newpoly.scale(SQ(scale_(10.)) / newpoly.area());

    for (int i = 0; i < n; i++) {
        auto &pt   = pts[i];
        Point move = newpoly[i] - (pt - center);
        if (vsize2_with_unscale(move) > SQ(max_move)) move = normal(move, scale_(max_move));
        movements[pt] = move;
    }
}
static void densify_polygon(Polygon &poly, double max_edge_length)
{
    Points  dense;
    Points &pts = poly.points;
    int     n   = pts.size();
    for (int i = 0; i < n; i++) {
        int i2 = (i + 1) % n;
        dense.emplace_back(pts[i]);
        Point prev = pts[i];
        Point curr;
        auto  edge   = pts[i2] - pts[i];
        int   n_frag = std::ceil(std::sqrt(vsize2_with_unscale(edge)) / max_edge_length);
        for (int j = 1; j < n_frag; j++) {
            curr = pts[i] + edge * (double(j) / double(n_frag));
            if (curr != prev) {
                prev = curr;
                dense.emplace_back(curr);
            }
        }
    }
    poly.points = std::move(dense);
}

void TreeSupport::drop_nodes()
{
    const PrintObjectConfig &config = m_object->config();
    // Use Minimum Spanning Tree to connect the points on each layer and move them while dropping them down.
    const coordf_t support_extrusion_width = m_support_params.support_extrusion_width;
    const coordf_t layer_height = config.layer_height.value;
    const double angle = config.tree_support_branch_angle.value * M_PI / 180.;
    const int wall_count = std::max(1, config.tree_support_wall_count.value);
    double tan_angle = tan(angle); // when nodes are thick, they can move further. this is the max angle
    const coordf_t max_move_distance = (angle < M_PI / 2) ? (coordf_t)(tan_angle * layer_height)*wall_count : std::numeric_limits<coordf_t>::max();
    const double max_move_distance2 = max_move_distance * max_move_distance;
    const size_t tip_layers = base_radius / layer_height; //The number of layers to be shrinking the circle to create a tip. This produces a 45 degree angle.
    const coordf_t radius_sample_resolution = m_ts_data->m_radius_sample_resolution;
    const bool support_on_buildplate_only = config.support_on_build_plate_only.value;
    const size_t top_interface_layers = config.support_interface_top_layers.value;
    SupportNode::diameter_angle_scale_factor = diameter_angle_scale_factor;
    // enabled only when the wall count is set to auto or dual-wall is explicitly enabled by the user.
    const bool bottom_expand_enabled = config.tree_support_wall_count > 1 || config.tree_support_wall_count < 0;

    auto get_max_move_dist = [this, &config, tan_angle, wall_count](const SupportNode *node, int power = 1) {
        if (node->max_move_dist == 0) {
            node->radius        = get_radius(node);
            node->max_move_dist = std::min(
                tan_angle * node->height,
                support_flare_step_width(node->print_z, node->height));
        }
        double move_dist = node->max_move_dist;
        if (power == 2) move_dist = SQ(move_dist);
        return move_dist;
    };

    std::vector<LayerHeightData> &layer_heights = m_ts_data->layer_heights;
    if (layer_heights.empty()) return;

    // precalculate avoidance of all possible radii.
    // This will cause computing more (radius, layer_nr) pairs, but it's worth to do so since we are doning this in parallel.
    if (1) {
        typedef std::chrono::high_resolution_clock clock_;
        typedef std::chrono::duration<double, std::ratio<1> > second_;
        std::chrono::time_point<clock_> t0{ clock_::now() };

        // get all the possible radiis
        std::vector<std::set<coordf_t> > all_layer_radius(contact_nodes.size());
        std::vector<std::set<coordf_t>> all_layer_node_dist(contact_nodes.size());
        for (size_t layer_nr = contact_nodes.size() - 1; layer_nr > 0; layer_nr--) {
            auto& layer_radius = all_layer_radius[layer_nr];
            auto& layer_node_dist = all_layer_node_dist[layer_nr];
            for (auto* p_node : contact_nodes[layer_nr]) {
                layer_node_dist.emplace(p_node->dist_mm_to_top);
            }
            size_t layer_nr_next = layer_nr - 1;
            if (layer_nr_next <= contact_nodes.size() - 1 && layer_nr_next > 0) {
                for (auto node_dist : layer_node_dist)
                    all_layer_node_dist[layer_nr_next].emplace(node_dist + layer_heights[layer_nr].height);
            }
            for (auto node_dist : layer_node_dist) {
                layer_radius.emplace(calc_radius(node_dist));
            }
        }
        // parallel pre-compute avoidance
        tbb::parallel_for(tbb::blocked_range<size_t>(0, contact_nodes.size() - 1), [&](const tbb::blocked_range<size_t> &range) {
            for (size_t layer_nr = range.begin(); layer_nr < range.end(); layer_nr++) {
            for (auto node_radius : all_layer_radius[layer_nr]) {
                size_t obj_layer_nr= layer_heights[layer_nr].obj_layer_nr;
                m_ts_data->get_avoidance(node_radius, obj_layer_nr);
                get_collision(0, obj_layer_nr);
                get_collision(node_radius, obj_layer_nr);
            }
        }
        });

        double duration{ std::chrono::duration_cast<second_>(clock_::now() - t0).count() };
        BOOST_LOG_TRIVIAL(debug) << "finish pre calculate_avoidance. before m_avoidance_cache.size()=" << m_ts_data->m_avoidance_cache.size()
            << ", takes " << duration << " secs.";
    }

    m_spanning_trees.resize(contact_nodes.size());
    //m_mst_line_x_layer_contour_caches.resize(contact_nodes.size());

    for (size_t layer_nr = contact_nodes.size() - 1; layer_nr > 0; layer_nr--) // Skip layer 0, since we can't drop down the vertices there.
    {
        if (m_object->print()->canceled())
            break;

        auto& layer_contact_nodes = contact_nodes[layer_nr];
        if (layer_contact_nodes.empty())
            continue;

        int layer_nr_next = layer_nr - 1;
        coordf_t print_z = layer_heights[layer_nr].print_z;
        coordf_t print_z_next = layer_heights[layer_nr_next].print_z;
        coordf_t height_next   = layer_heights[layer_nr_next].height;
        size_t   obj_layer_nr  = layer_heights[layer_nr].obj_layer_nr;
        size_t   obj_layer_nr_next = layer_heights[layer_nr_next].obj_layer_nr;

        std::deque<std::pair<size_t, SupportNode*>> unsupported_branch_leaves; // All nodes that are leaves on this layer that would result in unsupported ('mid-air') branches.

        m_object->print()->set_status(60 + int(10 * (1 - float(layer_nr) / contact_nodes.size())), _u8L("Generating support"));// (boost::format(_u8L("Support: propagate branches at layer %d")) % layer_nr).str());

        Polygons layer_contours = std::move(m_ts_data->get_contours_with_holes(obj_layer_nr));
        //std::unordered_map<Line, bool, LineHash>& mst_line_x_layer_contour_cache = m_mst_line_x_layer_contour_caches[layer_nr];
        tbb::concurrent_unordered_map<Line, bool, LineHash> mst_line_x_layer_contour_cache;
        auto is_line_cut_by_contour = [&mst_line_x_layer_contour_cache,&layer_contours](Point a, Point b)
        {
            auto iter = mst_line_x_layer_contour_cache.find({ a, b });
            if (iter != mst_line_x_layer_contour_cache.end()) {
                if (iter->second)
                    return true;
            }
            else {
                profiler.tic();
                Line ln(b, a);
                Lines pls_intersect = intersection_ln(ln, layer_contours);
                mst_line_x_layer_contour_cache.insert({ {a, b}, !pls_intersect.empty() });
                mst_line_x_layer_contour_cache.insert({ ln, !pls_intersect.empty() });
                profiler.stage_add(STAGE_intersection_ln);
                if (!pls_intersect.empty())
                    return true;
            }
            return false;
        };

        //Group together all nodes for each part.
        const ExPolygons& parts = m_ts_data->m_layer_outlines_below[obj_layer_nr];
        std::vector<std::unordered_map<Point, SupportNode*, PointHash>> nodes_per_part(1 + parts.size()); //All nodes that aren't inside a part get grouped together in the 0th part.
        for (SupportNode* p_node : layer_contact_nodes)
        {
            const SupportNode& node = *p_node;

            if (!node.to_buildplate) {
                // Can't rest on model and unable to reach the build plate. Then we must drop the node and leave parts unsupported.
                if (support_on_buildplate_only) {
                    unsupported_branch_leaves.push_front({layer_nr, p_node});
                    continue;
                }
                //If permitted to fall on the model, downward growth ceases.
                auto overlap_with_circle = shrink_ex(get_collision(0, obj_layer_nr), scale_(node.radius));
                if (!overlap_with_circle.empty() && is_inside_ex(overlap_with_circle, node.position)) {
                    continue;
                }
                Polygon circle = make_circle(scale_(node.radius), 0.00789 * scale_(node.radius));
                circle.translate(node.position);
                ExPolygons area = avoid_object_remove_extra_small_parts(ExPolygon(circle), get_collision(0, obj_layer_nr));
                if (!area.empty()) {
                    p_node->overhang = area[0];
                    if (area[0].area() < SQ(scale_(1.)) || (!node.parent->to_buildplate && !overlaps({node.overhang}, {node.parent->overhang}))) {
                        p_node->valid        = false;
                        p_node->is_processed = true;
                        continue;
                    }
                } else
                    continue;
            }
            if (node.to_buildplate || parts.empty()) //It's outside, so make it go towards the build plate.
            {
                if (node.type ==eCircle && nodes_per_part[0][node.position])
                    p_node->position += Point(1, 1);
                nodes_per_part[0][node.position] = p_node;
                continue;
            }

            /* Find which part this node is located in and group the nodes in
             * the same part together. Since nodes have a radius and the
             * avoidance areas are offset by that radius, the set of parts may
             * be different per node. Here we consider a node to be inside the
             * part that is closest. The node may be inside a bigger part that
             * is actually two parts merged together due to an offset. In that
             * case we may incorrectly keep two nodes separate, but at least
             * every node falls into some group.
             */
            coordf_t closest_part_distance2 = std::numeric_limits<coordf_t>::max();
            size_t closest_part = -1;
            for (size_t part_index = 0; part_index < parts.size(); part_index++)
            {
                //constexpr bool border_result = true;
                if (is_inside_ex(parts[part_index], node.position)) //If it's inside, the distance is 0 and this part is considered the best.
                {
                    closest_part = part_index;
                    closest_part_distance2 = 0;
                    break;
                }

                Point closest_point = *parts[part_index].contour.closest_point(node.position);
                const coordf_t distance2 = vsize2_with_unscale(node.position - closest_point);
                if (distance2 < closest_part_distance2)
                {
                    closest_part_distance2 = distance2;
                    closest_part = part_index;
                }
            }
            //Put it in the best one.
            if (node.type == eCircle && nodes_per_part[closest_part + 1][node.position])
                p_node->position += Point(1, 1);
            nodes_per_part[closest_part + 1][node.position] = p_node; //Index + 1 because the 0th index is the outside part.
        }

        //Create a MST for every part.
        profiler.tic();
        //std::vector<MinimumSpanningTree>& spanning_trees = m_spanning_trees[layer_nr];
        std::vector<MinimumSpanningTree> spanning_trees;
        for (const std::unordered_map<Point, SupportNode*, PointHash>& group : nodes_per_part)
        {
            std::vector<Point> points_to_buildplate;
            for (const std::pair<const Point, SupportNode*>& entry : group)
            {
                points_to_buildplate.emplace_back(entry.first);
            }
            std::sort(points_to_buildplate.begin(), points_to_buildplate.end(),
                      [](const Point& a, const Point& b) {
                          return a.x() != b.x() ? a.x() < b.x() : a.y() < b.y();
                      });
            spanning_trees.emplace_back(points_to_buildplate);
        }
        profiler.stage_add(STAGE_MinimumSpanningTree);

        //for (size_t i = 0; i < layer_contact_nodes.size(); i++) {
        //    SupportNode *p_node = layer_contact_nodes[i];
        //    if (!p_node->valid) continue;
        //    if (p_node->type == ePolygon) {
        //        if (p_node->distance_to_top < 0 && p_node->overhang.area() < SQ(scale_(1.))) {
        //            p_node->type = eCircle;
        //        } else if (p_node->overhang.area() < SQ(scale_(1.)))
        //            p_node->valid = false;
        //    }
        //}

#ifdef SUPPORT_TREE_DEBUG_TO_SVG
        coordf_t branch_radius_temp = 0;
        coordf_t max_y = std::numeric_limits<coordf_t>::min();
        draw_layer_mst(debug_out_path("mtree_%.2f.svg", print_z), spanning_trees, m_object->get_layer(obj_layer_nr)->lslices_extrudable);
#endif
        for (size_t group_index = 0; group_index < nodes_per_part.size(); group_index++)
        {
            auto& nodes_this_part = nodes_per_part[group_index];
            const MinimumSpanningTree& mst = spanning_trees[group_index];
            std::vector<std::pair<Point, SupportNode*>> nodes_vec;
            nodes_vec.reserve(nodes_this_part.size());
            for (const auto& kv : nodes_this_part)
                nodes_vec.emplace_back(kv.first, kv.second);
            std::sort(nodes_vec.begin(), nodes_vec.end(),
                      [](const std::pair<Point, SupportNode*>& a, const std::pair<Point, SupportNode*>& b) {
                          return a.first.x() != b.first.x() ? a.first.x() < b.first.x() : a.first.y() < b.first.y();
                      });
            //In the first pass, merge all nodes that are close together.
#if USE_TREE_SUPPORT_CARRIED_MASS
            ExPolygons lower_avoid_p1;
            if (group_index == 0) {
                const int lookahead_layers = std::max(1, int(std::round(8.0 / std::max<double>(height_next, 0.1))));
                const size_t lower_obj_layer = (obj_layer_nr_next > size_t(lookahead_layers)) ? (obj_layer_nr_next - size_t(lookahead_layers)) : 0;
                lower_avoid_p1 = get_avoidance(MIN_BRANCH_RADIUS, lower_obj_layer);
            }
#endif
            for (const std::pair<Point, SupportNode*>& entry : nodes_vec) {
                SupportNode* p_node = entry.second;
                SupportNode& node = *p_node;
                if (!p_node->valid)
                {
                    continue;
                }
                if (node.fading) continue;
                const std::vector<Point>& neighbours = mst.adjacent_nodes(node.position);
                if (node.type == ePolygon) {
                    // Remove all circle neighbours that are completely inside the polygon and merge them into this node.
                    for (const Point &neighbour : neighbours) {
                        SupportNode *neighbour_node = nodes_this_part[neighbour];
                        bool         can_merge      = false;
                        if (neighbour_node->valid == false) continue;
                        if (neighbour_node->fading) continue;
                        if (neighbour_node->type == ePolygon) {
                            if ((node.distance_to_top < 0 && neighbour_node->distance_to_top < 0) ||
                                (node.distance_to_top > m_support_params.num_top_interface_layers + 1 &&
                                 neighbour_node->distance_to_top > m_support_params.num_top_interface_layers + 1)) {
                                auto overhang_shrinked = shrink_ex({node.overhang}, scale_(support_extrusion_width));
                                if (!overhang_shrinked.empty() && overlaps(overhang_shrinked, {neighbour_node->overhang})) {
                                    auto tmp = union_ex({node.overhang}, {neighbour_node->overhang});
                                    if (!tmp.empty() && tmp.size() == 1) {
                                        Point        next_pt     = tmp[0].contour.centroid();
                                        SupportNode *next_node   = m_ts_data->create_node(next_pt, std::max(node.distance_to_top, neighbour_node->distance_to_top) + 1,
                                                                                          obj_layer_nr_next,
                                                                                          std::max(node.support_roof_layers_below, neighbour_node->support_roof_layers_below) - 1,
                                                                                          true, p_node, print_z_next, height_next);
                                        next_node->max_move_dist = 0;
                                        next_node->overhang      = std::move(tmp[0]);
                                        next_node->target_radius = std::max(p_node->target_radius, neighbour_node->target_radius);
                                        next_node->origin_area   = next_node->overhang.area();
                                        next_node->carried_mass += neighbour_node->total_carried_mass();
                                        contact_nodes[layer_nr_next].emplace_back(next_node);
                                        p_node->valid = false;
                                        neighbour_node->valid = false;
                                        goto first_pass_next;
                                    }
                                }
                            }
                        } else {
                            coord_t neighbour_radius = scale_(neighbour_node->radius);
                            Point   pt_north = neighbour + Point(0, neighbour_radius), pt_south = neighbour - Point(0, neighbour_radius),
                                  pt_west = neighbour - Point(neighbour_radius, 0), pt_east = neighbour + Point(neighbour_radius, 0);
                            can_merge = is_inside_ex(node.overhang, neighbour) && is_inside_ex(node.overhang, pt_north) && is_inside_ex(node.overhang, pt_south) &&
                                is_inside_ex(node.overhang, pt_west) && is_inside_ex(node.overhang, pt_east);
                            const auto circle_area = M_PI * SQ(neighbour_radius);
                            if (!can_merge && is_inside_ex(node.overhang, neighbour)) {
                                //ExPolygon neighbor_circle(make_circle(neighbour_radius, scale_(0.1)));
                                //neighbor_circle.translate(neighbour);
                                //node.overhang = union_ex({node.overhang}, {neighbor_circle})[0];
                                neighbour_node->fading = true;
                                if (node.overhang.contour.bounding_box().radius() < neighbour_radius) {
                                    node.target_radius = neighbour_node->radius;
                                }
                            }
                        }
                        if (p_node->valid && can_merge) {
                            node.merged_neighbours.push_front(neighbour_node);
                            node.merged_neighbours.insert(node.merged_neighbours.end(), neighbour_node->merged_neighbours.begin(), neighbour_node->merged_neighbours.end());
                            neighbour_node->valid = false;
                        }
                    }
                } else if (neighbours.size() == 1 && vsize2_with_unscale(neighbours[0] - node.position) < get_max_move_dist(p_node, 2) &&
                           mst.adjacent_nodes(neighbours[0]).size() == 1 &&
                           nodes_this_part[neighbours[0]]->type!=ePolygon) // We have just two nodes left, and they're very close, and the only neighbor is not ePolygon
                {
                    //Insert a completely new node and let both original nodes fade.
                    SupportNode* neighbour = nodes_this_part[neighbours[0]];
                    const Point mid_position = (node.position + neighbours[0]) / 2; //Average position of the two nodes.
                    Point next_position = mid_position;
                    coordf_t next_radius = calc_radius(node.dist_mm_to_top + height_next);
                    auto avoid_layer = get_avoidance(next_radius, obj_layer_nr_next);
#if USE_TREE_SUPPORT_CARRIED_MASS
                    {
                        const double m_self    = std::max<double>(node.total_carried_mass(), EPSILON);
                        const double m_nb      = std::max<double>(neighbour->total_carried_mass(), EPSILON);
                        double       nb_weight = m_nb / (m_self + m_nb);
                        if (group_index == 0 && !lower_avoid_p1.empty()) {
                            const bool me_in_low = is_inside_ex(lower_avoid_p1, node.position);
                            const bool nb_in_low = is_inside_ex(lower_avoid_p1, neighbours[0]);
                            if (!me_in_low && nb_in_low)
                                nb_weight = std::min(nb_weight, 0.2);
                            else if (me_in_low && !nb_in_low)
                                nb_weight = std::max(nb_weight, 0.8);
                        }
                        const Point  weighted_pos = node.position + Point(
                            coord_t(std::round(nb_weight * double(neighbours[0].x() - node.position.x()))),
                            coord_t(std::round(nb_weight * double(neighbours[0].y() - node.position.y()))));
                        if (!is_inside_ex(get_collision(0, obj_layer_nr_next), weighted_pos) &&
                            (group_index > 0 || !is_inside_ex(avoid_layer, weighted_pos))) {
                            next_position = weighted_pos;
                        }
                    }
#endif
                    if (group_index == 0)
                    {
                        //Avoid collisions.
                        const coordf_t max_move_between_samples = max_move_distance + radius_sample_resolution + EPSILON; //100 micron extra for rounding errors.
                        move_out_expolys(avoid_layer, next_position, radius_sample_resolution + EPSILON, max_move_between_samples);
                    }

                    SupportNode* node_parent;
                    if (p_node->parent && neighbour->parent)
                        node_parent = (node.radius >= neighbour->radius) ? p_node : neighbour;
                    else
                        node_parent = p_node->parent ? p_node : neighbour;
                    // Make sure the next pass doesn't drop down either of these (since that already happened).
                    node_parent->merged_neighbours.push_front(node_parent == p_node ? neighbour : p_node);
                    const bool to_buildplate = !is_inside_ex(get_collision(0, obj_layer_nr_next), next_position);
                    SupportNode* next_node = m_ts_data->create_node(next_position, node_parent->distance_to_top + 1, obj_layer_nr_next, node_parent->support_roof_layers_below - 1, to_buildplate, node_parent,
                        print_z_next, height_next);
                    auto dist_xy_to_node     = (next_position - node.position).cast<double>().norm();
                    auto dist_xy_to_neighbor = (next_position - neighbour->position).cast<double>().norm();
                    next_node->orig_pos      = dist_xy_to_neighbor < dist_xy_to_node ? node.orig_pos : neighbour->orig_pos;
                    get_max_move_dist(next_node);
                    contact_nodes[layer_nr_next].push_back(next_node);
                    neighbour->valid = false;
                    p_node->valid = false;
                }
                else if (neighbours.size() > 1) //Don't merge leaf nodes because we would then incur movement greater than the maximum move distance.
                {
                    //Remove all neighbours that are too close and merge them into this node.
                    for (const Point& neighbour : neighbours)
                    {
                        if (vsize2_with_unscale(neighbour - node.position) < get_max_move_dist(&node,2))
                        {
                            SupportNode* neighbour_node = nodes_this_part[neighbour];
                            if (!neighbour_node->valid) continue;
                            if (neighbour_node->type == ePolygon) continue;
                            if (node.radius < neighbour_node->radius) continue; // bug fix: STUDIO-6326, do not merge if current node has smaller radius than neighbor.

                            if (p_node->valid)
                            {
                                node.merged_neighbours.push_front(neighbour_node);
                                node.merged_neighbours.insert(node.merged_neighbours.end(), neighbour_node->merged_neighbours.begin(), neighbour_node->merged_neighbours.end());
                                neighbour_node->valid = false;
                            }
                        }
                    }
                }
first_pass_next:;
            }

            //In the second pass, move all middle nodes.
            tbb::parallel_for_each(nodes_vec.begin(), nodes_vec.end(), [&](const std::pair<Point, SupportNode*>& entry) {

                SupportNode* p_node = entry.second;
                const SupportNode& node = *p_node;
                if (!p_node->valid)
                {
                    return;
                }
                if (node.fading) {
                    coordf_t     next_radius = node.radius - max_move_distance;
                    if (next_radius < EPSILON) return;
                    SupportNode *next_node = m_ts_data->create_node(node.position, p_node->distance_to_top + 1, obj_layer_nr_next, p_node->support_roof_layers_below - 1, node.to_buildplate,
                                                                    p_node, print_z_next, height_next);
                    next_node->max_move_dist = 0;
                    next_node->radius        = next_radius;
                    next_node->fading        = true;
                    m_ts_data->m_mutex.lock();
                    contact_nodes[layer_nr_next].emplace_back(next_node);
                    m_ts_data->m_mutex.unlock();
                    return;
                }
                if (node.type == ePolygon) {
                    // polygon node do not merge or move
                    if (node.overhang.empty()) {
                        p_node->valid = false;
                        return;
                    }
                    const bool to_buildplate = true;
                    ExPolygons overhangs_next{node.overhang};
                    auto       poly_radius   = node.overhang.contour.bounding_box().radius();
                    bool       offseted    = false;
                    // polygon support expansion cases:
                    // 1. bottom expansion is enabled
                    // 2. large tree supports merge into the region
                    if ((bottom_expand_enabled && node.print_z < DO_NOT_MOVER_UNDER_MM) ||
                        (poly_radius < scale_(node.target_radius) || node.overhang.area() < 3. * SQ(scale_(node.target_radius)))) {
                        overhangs_next = offset_ex({node.overhang}, scale_(max_move_distance / 2.));
                        offseted       = true;
                    }
                    // keep only the part that won't be removed by the next layer
                    overhangs_next = diff_clipped(overhangs_next, get_collision(0, obj_layer_nr_next));
                    if (node.distance_to_top == 0) {
                        overhangs_next      = offset2_ex(overhangs_next, scale_(max_move_distance), -scale_(max_move_distance));
                        p_node->origin_area = node.overhang.area();
                        densify_polygon(p_node->overhang.contour, 2.);
                    }
                    if (m_support_params.num_top_interface_layers > 0 && obj_layer_nr_next > 0 && node.support_roof_layers_below == 1 &&
                        node.distance_to_top >= m_support_params.num_top_interface_layers)
                        overhangs_next = safe_offset_inc(overhangs_next, scale_(max_move_distance), get_collision(0, obj_layer_nr_next), scale_(MIN_BRANCH_RADIUS * 1.75), 0, 1);
                    for(auto& overhang:overhangs_next) {
                        if (overhang.empty()) continue;
                        if (overhang.area() > node.origin_area / 2. && overhang.area() > SQ(scale_(10.))) {
                            Polygon contour = overhang.contour;
                            std::unordered_map<Point, Point, PointHash> movements;
                            smooth_filter(overhang.contour, 2, movements, max_move_distance / 2.);
                            for (auto &pt : overhang.contour.points) {
                                auto tmp = pt + movements.at(pt);
                                if (!is_inside_ex(to_expolygons({contour}), tmp) && !is_inside_ex(m_ts_data->m_layer_outlines_below[obj_layer_nr], tmp))
                                    pt = tmp;
                            }
                        }
                        // if the part would fall straight to th buildplate, shrink it a little
                        if (!offseted && node.support_roof_layers_below<0 && overhang.area() > node.origin_area / 2. &&
                            overhang.area() > double(SQ(scale_(10.)))) {
                            ExPolygons shrink_overhangs = union_ex(shrink_ex(safe_union({overhang}), double(scale_(max_move_distance / 2.))));
                            if (shrink_overhangs.size() == 1 && shrink_overhangs[0].area() > double(SQ(scale_(10.))) &&
                                !overlaps({overhang}, m_ts_data->m_layer_outlines_below[obj_layer_nr_next])) {
                                if (diff_ex({overhang}, offset_ex(shrink_overhangs[0],scale_(max_move_distance))).empty())
                                    overhang = shrink_overhangs[0];
                            }
                            Point        next_pt     = overhang.contour.centroid();
                            SupportNode *next_node   = m_ts_data->create_node(next_pt, p_node->distance_to_top + 1, obj_layer_nr_next, p_node->support_roof_layers_below - 1,
                                                                              to_buildplate, p_node, print_z_next, height_next);
                            next_node->max_move_dist = 0;
                            next_node->overhang      = std::move(overhang);
                            next_node->target_radius = p_node->target_radius;
                            next_node->origin_area   = node.origin_area;
                            m_ts_data->m_mutex.lock();
                            contact_nodes[layer_nr_next].emplace_back(next_node);
                            m_ts_data->m_mutex.unlock();

                        } else {
                            Point        next_pt     = overhang.contour.centroid();
                            SupportNode *next_node   = m_ts_data->create_node(next_pt, p_node->distance_to_top + 1, obj_layer_nr_next, p_node->support_roof_layers_below - 1,
                                                                              to_buildplate, p_node, print_z_next, height_next);
                            next_node->max_move_dist = 0;
                            next_node->target_radius = p_node->target_radius;
                            next_node->overhang      = std::move(overhang);
                            next_node->origin_area   = node.origin_area;
                            m_ts_data->m_mutex.lock();
                            contact_nodes[layer_nr_next].emplace_back(next_node);
                            m_ts_data->m_mutex.unlock();
                        }
                    }
                    return;
                }

                //If the branch falls completely inside a collision area (the entire branch would be removed by the X/Y offset), delete it.
                if (group_index > 0 && is_inside_ex(get_collision(0, obj_layer_nr), node.position))
                {
                    std::scoped_lock lock(m_ts_data->m_mutex);
                    const coordf_t branch_radius_node = get_radius(p_node);
                    Point to_outside = projection_onto(get_collision(0, obj_layer_nr), node.position);
                    double dist2_to_outside = vsize2_with_unscale(node.position - to_outside);
                    if (dist2_to_outside >= branch_radius_node * branch_radius_node) //Too far inside.
                    {
                        if (support_on_buildplate_only)
                        {
                            unsupported_branch_leaves.push_front({ layer_nr, p_node });
                        }
                        else {
                            p_node->valid = false;
                        }
                        return;
                    }
                    // if the link between parent and current is cut by contours, mark current as bottom contact node
                    if (p_node->parent && intersection_ln({p_node->position, p_node->parent->position}, layer_contours).empty()==false)
                    {
                        p_node->valid = false;
                        return;
                    }
                }
                Point next_layer_vertex = node.position;
                Point move_to_neighbor_center;
#if USE_TREE_SUPPORT_CARRIED_MASS
                Point move_to_neighbor_center_base;
#endif
                std::vector<Point>       moves;
                std::vector<float>       weights;
                const std::vector<Point>& neighbours = mst.adjacent_nodes(node.position);
                // 1. do not merge neighbors under 5mm
                // 2. Only merge node with single neighbor in distance between [max_move_distance, 10mm/layer_height]
                float dist2_to_first_neighbor = neighbours.empty() ? 0 : vsize2_with_unscale(neighbours[0] - node.position);
                if (node.print_z > DO_NOT_MOVER_UNDER_MM &&
                    (neighbours.size() > 1 ||
                     (neighbours.size() == 1/* && nodes_this_part[neighbours[0]]->type != ePolygon */&& dist2_to_first_neighbor >= get_max_move_dist(p_node, 2))/* ||
                     (neighbours.size() == 1 && nodes_this_part[neighbours[0]]->type == ePolygon)*/)) // Only nodes that aren't about to collapse.
                {
                    // Move towards the average position of all neighbours, weighted by carried_mass
                    // to encourage light branches to move towards heavy trunks and suppress heavy trunks
                    // from deflecting towards light branches.
#if USE_TREE_SUPPORT_CARRIED_MASS
                    const double self_mass     = std::max<double>(node.total_carried_mass(), EPSILON);
                    double       max_nb_mass   = 0.0;
                    const Point  prev_move     = p_node->parent ? (node.position - p_node->parent->position) : Point(0, 0);
                    const Vec2d  prev_move_d   = prev_move.cast<double>();
                    const bool   has_prev_move = prev_move_d.squaredNorm() > double(scale_(0.01)) * double(scale_(0.01));
                    // Direction towards the nearest edge of the model outlines below (for group_index == 0).
                    // Pulling a branch deeper over a lower model part forces it to either reverse outward later
                    // (elbow bend) or land on the model top.
                    Vec2d        outlines_below_out_d(0.0, 0.0);
                    bool         in_outlines_below = false;
                    if (group_index == 0 && !parts.empty() && !lower_avoid_p1.empty() && is_inside_ex(parts, node.position)) {
                        const Point proj_out = projection_onto(parts, node.position);
                        outlines_below_out_d = (proj_out - node.position).cast<double>();
                        in_outlines_below    = outlines_below_out_d.squaredNorm() > double(scale_(0.2)) * double(scale_(0.2));
                    }
                    Vec2d        base_dir_d(0.0, 0.0);
#endif
                    Vec2d        sum_dir_d(0.0, 0.0);
                    for (const Point &neighbour : neighbours) {
                        // do not move to the neighbor to be deleted
                        SupportNode *neighbour_node = nodes_this_part[neighbour];
                        if (!neighbour_node->valid) continue;
                        Point direction;
                        if (neighbour_node->type == ePolygon && neighbour_node->overhang.is_valid()) {
                            Point contact_point = projection_onto({neighbour_node->overhang}, node.position);
                            direction     = contact_point - node.position;
                        } else {
                            direction = neighbour - node.position;
                        }
                        // do not move to neighbor that's too far away (即使以最大速度移动，在接触热床之前都无法汇聚)
                        float dist2_to_neighbor = vsize2_with_unscale(direction);

                        coordf_t branch_bottom_radius = calc_radius(node.dist_mm_to_top + node.print_z);
                        coordf_t neighbour_bottom_radius = calc_radius(neighbour_node->dist_mm_to_top + neighbour_node->print_z);
                        double max_converge_distance = tan_angle * (p_node->print_z - DO_NOT_MOVER_UNDER_MM) + std::max(branch_bottom_radius, neighbour_bottom_radius);
                        if (dist2_to_neighbor > max_converge_distance * max_converge_distance) continue;

                        if (is_line_cut_by_contour(node.position, neighbour)) continue;

                        double mass_weight = 1.0;
#if USE_TREE_SUPPORT_CARRIED_MASS
                        const double nb_mass = std::max<double>(neighbour_node->total_carried_mass(), EPSILON);
                        max_nb_mass = std::max(max_nb_mass, nb_mass);
                        {
                            const Vec2d dir_d         = direction.cast<double>();
                            const bool  reverses_prev = has_prev_move && dir_d.dot(prev_move_d) < 0.0;
                            if (nb_mass >= self_mass) {
                                mass_weight = std::sqrt(2.0 * nb_mass / (self_mass + nb_mass));
                            } else {
                                if (reverses_prev)
                                    mass_weight = std::sqrt(2.0 * nb_mass / (self_mass + nb_mass));
                                else
                                    mass_weight = 1.0;
                            }
                            if (group_index == 0 && !lower_avoid_p1.empty()) {
                                const bool me_in_low = is_inside_ex(lower_avoid_p1, node.position);
                                const bool nb_in_low = is_inside_ex(lower_avoid_p1, neighbour);
                                if (!me_in_low && nb_in_low) {
                                    mass_weight *= 0.25;
                                } else if (me_in_low && !nb_in_low) {
                                    mass_weight = std::max(mass_weight, 1.4);
                                } else if (me_in_low && nb_in_low && in_outlines_below) {
                                    if (dir_d.dot(outlines_below_out_d) > 0.0)
                                        mass_weight = std::max(mass_weight, 1.3);
                                    else
                                        mass_weight *= 0.4;
                                }
                            }
                        }
#endif

                        if (!is_strong) {
                            sum_dir_d += direction.cast<double>() * (mass_weight / double(dist2_to_neighbor));
#if USE_TREE_SUPPORT_CARRIED_MASS
                            base_dir_d += direction.cast<double>() * (1.0 / double(dist2_to_neighbor));
#endif
                        } else {
                            sum_dir_d += direction.cast<double>() * mass_weight;
#if USE_TREE_SUPPORT_CARRIED_MASS
                            base_dir_d += direction.cast<double>();
#endif
                        }
                    }

                    Point sum_direction(coord_t(std::round(sum_dir_d.x())), coord_t(std::round(sum_dir_d.y())));
#if USE_TREE_SUPPORT_CARRIED_MASS
                    Point base_direction(coord_t(std::round(base_dir_d.x())), coord_t(std::round(base_dir_d.y())));
#endif

                    if (!is_strong) {
                        move_to_neighbor_center = sum_direction;
#if USE_TREE_SUPPORT_CARRIED_MASS
                        move_to_neighbor_center_base = base_direction;
#endif
                    } else {
                        if (vsize2_with_unscale(sum_direction) <= get_max_move_dist(p_node, 2)) {
                            move_to_neighbor_center = sum_direction;
                        } else {
                            move_to_neighbor_center = normal(sum_direction, scale_(get_max_move_dist(p_node)));
                        }
#if USE_TREE_SUPPORT_CARRIED_MASS
                        if (vsize2_with_unscale(base_direction) <= get_max_move_dist(p_node, 2)) {
                            move_to_neighbor_center_base = base_direction;
                        } else {
                            move_to_neighbor_center_base = normal(base_direction, scale_(get_max_move_dist(p_node)));
                        }
#endif
                    }
#if USE_TREE_SUPPORT_CARRIED_MASS
                    // When all reachable neighbours carry less mass than this node and pull it opposite to its
                    // trajectory (or deeper over lower model outlines), cap the attraction step by mass ratio.
                    if (max_nb_mass > 0.0 && max_nb_mass < self_mass) {
                        const Vec2d move_d   = move_to_neighbor_center.cast<double>();
                        const bool  rev_move = (has_prev_move && move_d.dot(prev_move_d) < 0.0) ||
                                               (in_outlines_below && move_d.dot(outlines_below_out_d) < 0.0);
                        if (rev_move) {
                            const double attract_scale = (support_on_buildplate_only || is_strong) ? 1.0 : 2.0;
                            const double mass_factor   = std::clamp(2.0 * max_nb_mass / (self_mass + max_nb_mass), 0.15, 1.0);
                            const double max_attract   = (get_max_move_dist(&node) * mass_factor) / attract_scale;
                            if (vsize2_with_unscale(move_to_neighbor_center) > SQ(max_attract))
                                move_to_neighbor_center = normal(move_to_neighbor_center, scale_(max_attract));
                        }
                    }
#endif
                }

#ifdef SUPPORT_TREE_DEBUG_TO_SVG
                if (node.position(1) > max_y) {
                    max_y              = node.position(1);
                    branch_radius_temp = get_radius(p_node);
                }
#endif
                coordf_t next_radius = calc_radius(node.dist_mm_to_top + height_next);
                auto avoidance_next = get_avoidance(next_radius, obj_layer_nr_next);
                
                Point  to_outside         = projection_onto(avoidance_next, node.position);
                Point  direction_to_outer = to_outside - node.position;
                if (node.skin_direction != Point(0, 0) && node.dist_mm_to_top < 3) {
                    direction_to_outer = move_to_neighbor_center = normal(node.skin_direction, scale_(max_move_distance));
                }
                double dist2_to_outer     = vsize2_with_unscale(direction_to_outer);
                // don't move if
                // 1) line of node and to_outside is cut by contour (means supports may intersect with object)
                // 2) it's impossible to move to build plate
                if (is_line_cut_by_contour(node.position, to_outside) || dist2_to_outer > max_move_distance2 * SQ(obj_layer_nr) ||
                    !is_inside_ex(avoidance_next, node.position)) {
                    // try move to outside of lower layer instead
                    Point candidate_vertex = node.position;
                    const coordf_t max_move_between_samples = max_move_distance + radius_sample_resolution + EPSILON; // 100 micron extra for rounding errors.
                    // use get_collision instead of get_avoidance here (See STUDIO-4252)
                    bool           is_outside               = move_out_expolys(get_collision(next_radius,obj_layer_nr_next), candidate_vertex, max_move_between_samples, max_move_between_samples);
                    if (is_outside) {
                        direction_to_outer = candidate_vertex - node.position;
                        dist2_to_outer    = vsize2_with_unscale(direction_to_outer);
                    } else {
                        direction_to_outer = Point(0, 0);
                        dist2_to_outer     = 0;
                    }
                }
                // move to the averaged direction of neighbor center and contour edge if they are roughly same direction
#if USE_TREE_SUPPORT_CARRIED_MASS
                if (dist2_to_outer > EPSILON && move_to_neighbor_center_base != Point(0, 0)) {
                    // When escaping an obstacle or avoidance zone, keep the unweighted attraction magnitude
                    // so carried_mass scaling never alters the escape-vs-attraction vector balance.
                    move_to_neighbor_center = move_to_neighbor_center_base;
                }
#endif
                Point movement;
                if (support_on_buildplate_only)
                    movement = move_to_neighbor_center + direction_to_outer * 2;
                else if (!is_strong)
                    movement = move_to_neighbor_center*2 + (dist2_to_outer > EPSILON ? direction_to_outer * (1 / dist2_to_outer) : Point(0, 0));
                else {
                    if (movement.dot(move_to_neighbor_center) >= 0.2 || move_to_neighbor_center == Point(0, 0))
                        movement = direction_to_outer + move_to_neighbor_center;
                    else
                        movement = move_to_neighbor_center; // otherwise move to neighbor center first
                }

                //if (node.is_sharp_tail && node.dist_mm_to_top < 3) {
                //    movement = normal(node.skin_direction, scale_(get_max_move_dist(&node)));
                //}
                //else if (dist2_to_outer > 0)
                //    movement = normal(direction_to_outer, scale_(get_max_move_dist(&node)));
                //else
                //    movement = normal(move_to_neighbor_center, scale_(get_max_move_dist(&node)));
                if (vsize2_with_unscale(movement) > get_max_move_dist(&node, 2)) movement = normal(movement, scale_(get_max_move_dist(&node)));

                next_layer_vertex += movement;

                if (group_index == 0 && 0) {
                    // Avoid collisions.
                    const coordf_t max_move_between_samples = get_max_move_dist(&node, 1) + radius_sample_resolution + EPSILON; // 100 micron extra for rounding errors.
                    bool           is_outside               = move_out_expolys(avoidance_next, next_layer_vertex, radius_sample_resolution + EPSILON, max_move_between_samples);
                    if (!is_outside) {
                        Point candidate_vertex = node.position;
                        is_outside             = move_out_expolys(avoidance_next, candidate_vertex, radius_sample_resolution + EPSILON, max_move_between_samples);
                        if (is_outside) { next_layer_vertex = candidate_vertex; }
                    }
                }
                auto              next_collision = get_collision(0, obj_layer_nr_next);
                const bool   to_buildplate  = !is_inside_ex(m_ts_data->m_layer_outlines[obj_layer_nr_next], next_layer_vertex);
                SupportNode *     next_node     = m_ts_data->create_node(next_layer_vertex, node.distance_to_top + 1, obj_layer_nr_next, node.support_roof_layers_below - 1, to_buildplate, p_node,
                    print_z_next, height_next);
                // don't increase radius if next node will collide partially with the object (STUDIO-7883)
                to_outside             = projection_onto(next_collision, next_node->position);
                direction_to_outer     = to_outside - node.position;
                double dist_to_outer   = unscale_(direction_to_outer.cast<double>().norm());
                next_node->radius      = (bottom_expand_enabled && next_node->print_z < DO_NOT_MOVER_UNDER_MM && node.dist_mm_to_top > DO_NOT_MOVER_UNDER_MM) ?
                                             node.radius + support_flare_step_width(next_node->print_z, next_node->height) / 2. :
                                             std ::max(node.radius, std::min(next_node->radius, dist_to_outer));
                get_max_move_dist(next_node);
                m_ts_data->m_mutex.lock();
                contact_nodes[layer_nr_next].push_back(next_node);
                m_ts_data->m_mutex.unlock();
            }
            );

            // Sort contact_nodes for deterministic processing in the next layer
            std::sort(contact_nodes[layer_nr_next].begin(), contact_nodes[layer_nr_next].end(),
                      [](const SupportNode* a, const SupportNode* b) {
                          if (a->position.x() != b->position.x()) return a->position.x() < b->position.x();
                          return a->position.y() < b->position.y();
                      });
        }

        // BBS multi-diameter: a slab is one extrusion printed at the cross-section of its top face, so a
        // branch that drifts while crossing the slab ends up somewhere the slab below does not carry it.
        // Cap the drift a chain may accumulate between two slab tops by what the slab's own height allows -
        // the same branch-angle bound a single fine layer gets, applied over the distance that really
        // separates two printed faces. Without it the per layer cap is spent band_size times over.
        if (m_support_merge_plan.enabled && layer_nr_next < m_ts_data->layer_heights.size() &&
            !contact_nodes[layer_nr_next].empty()) {
            const size_t obj_layer_next = m_ts_data->layer_heights[layer_nr_next].obj_layer_nr;
            const SupportMergePlan::Slab *slab = m_support_merge_plan.slab_for_layer(obj_layer_next);
            // Nothing to cap on the slab's own top layer: that face is where the budget is handed out.
            if (slab != nullptr && obj_layer_next != slab->top_idx && slab->height > EPSILON) {
                const double budget_scaled = double(scale_(tan_angle * slab->height));
                for (SupportNode *node : contact_nodes[layer_nr_next]) {
                    if (node == nullptr || node->type == ePolygon || node->parent == nullptr)
                        continue;
                    const SupportNode *anchor = node;
                    while (anchor->parent != nullptr && size_t(anchor->obj_layer_nr) < slab->top_idx)
                        anchor = anchor->parent;
                    if (anchor == node)
                        continue;
                    const Vec2d  delta = (node->position - anchor->position).cast<double>();
                    const double len   = delta.norm();
                    if (len > budget_scaled && len > EPSILON) {
                        const Vec2d capped = delta * (budget_scaled / len);
                        node->position = anchor->position + Point(coord_t(capped.x()), coord_t(capped.y()));
                    }
                }
            }
        }

        if (layer_nr_next == 0 && support_on_buildplate_only && !contact_nodes[layer_nr_next].empty()) {
            for (SupportNode *node : contact_nodes[layer_nr_next]) {
                if (!node->to_buildplate) { 
                    unsupported_branch_leaves.push_front({layer_nr_next, node});
                }
            }
        }

#ifdef SUPPORT_TREE_DEBUG_TO_SVG
        if (contact_nodes[layer_nr].empty() == false) {
            draw_contours_and_nodes_to_svg(debug_out_path("contact_points_%.2f.svg", contact_nodes[layer_nr][0]->print_z), get_collision(0,obj_layer_nr_next),
                                           get_avoidance(branch_radius_temp, obj_layer_nr),
                                           m_ts_data->m_layer_outlines[obj_layer_nr],
            contact_nodes[layer_nr], contact_nodes[layer_nr_next], { "overhang","avoid","outline" }, { "blue","red","yellow" });

            BOOST_LOG_TRIVIAL(debug) << "layer->next " << layer_nr << "->" << layer_nr_next << ", print_z=" << print_z
                << ", num points: " << contact_nodes[layer_nr].size() << "->" << contact_nodes[layer_nr_next].size();
            for (size_t i = 0; i < std::min(size_t(5), contact_nodes[layer_nr].size()); i++) {
                auto &node = contact_nodes[layer_nr][i];
                BOOST_LOG_TRIVIAL(debug) << "\t node " << i << ", pos=" << node->position << ", move = " << node->movement;
            }
        }
#endif

        // Prune all branches that couldn't find support on either the model or the buildplate (resulting in 'mid-air' branches).
        for (;! unsupported_branch_leaves.empty(); unsupported_branch_leaves.pop_back())
        {
            const auto& entry = unsupported_branch_leaves.back();
            SupportNode* i_node = entry.second;
            for (; i_node != nullptr; i_node = i_node->parent)
            {
                size_t i_layer = i_node->obj_layer_nr;
                {
                    // update the parent-child chain
                    if (i_node->parent) {
                        i_node->parent->child = i_node->child;
                        for (SupportNode* parent : i_node->parents) {
                        if (parent->child==i_node)
                            parent->child = i_node->child;
                        }
                    }
                    if (i_node->child) {
                        i_node->child->parent = i_node->parent;
                        auto it = std::find(i_node->child->parents.begin(), i_node->child->parents.end(), i_node);
                        if (it != i_node->child->parents.end()) i_node->child->parents.erase(it);
                        if (!i_node->parents.empty())
                            append(i_node->child->parents, i_node->parents);
                    }
                    i_node->is_processed = true;  // mark to be deleted later

                    for (SupportNode* neighbour : i_node->merged_neighbours)
                    {
                        if (neighbour && !neighbour->is_processed)
                            unsupported_branch_leaves.push_front({ i_layer, neighbour });
                    }
                }
            }
        }
        for (auto &layer_contact_nodes : contact_nodes) {
            if (!layer_contact_nodes.empty())
                layer_contact_nodes.erase(std::remove_if(layer_contact_nodes.begin(), layer_contact_nodes.end(), [](SupportNode *node) { return node->is_processed; }),
                                          layer_contact_nodes.end());
        }

        if (!contact_nodes.empty()) {
            // make sure r[i - 1] - r[i] <= line_width
            const double radius_stable_threshold = (max_move_distance + max_move_distance) / (SQ(max_move_distance + MIN_BRANCH_RADIUS) - SQ(MIN_BRANCH_RADIUS));
            tbb::parallel_for_each(contact_nodes[0].begin(), contact_nodes[0].end(), [&radius_stable_threshold](SupportNode *node) {
#if USE_TREE_SUPPORT_CARRIED_MASS
                SupportNode *trunk_top = node;
                while (trunk_top->parent && trunk_top->parents.size() <= 1)
                    trunk_top = trunk_top->parent;
#endif
                while (node != nullptr) {
#if USE_TREE_SUPPORT_CARRIED_MASS
                    const Point &ref_pos = trunk_top->position;
#else
                    const Point &ref_pos = node->orig_pos;
#endif
                    const double stable_radius = sqrt((node->dist_mm_to_top + unscale_((node->position - ref_pos).cast<double>().norm())) / radius_stable_threshold);
                    if (node->radius >= stable_radius)
                        break;
                    node->radius = stable_radius;
                    if (!node->parent || node->parents.size() > 1) break;
                    node          = node->parent;
                }
            });
        }
    }

    BOOST_LOG_TRIVIAL(debug) << "after m_avoidance_cache.size()=" << m_ts_data->m_avoidance_cache.size();
}

void TreeSupport::smooth_nodes()
{
    for (int layer_nr = 0; layer_nr < contact_nodes.size(); layer_nr++) {
        std::vector<SupportNode *> &curr_layer_nodes = contact_nodes[layer_nr];
        if (curr_layer_nodes.empty()) continue;
        for (SupportNode *node : curr_layer_nodes) {
            node->is_processed = false;
        }
    }

    const coordf_t support_nozzle = Slic3r::support_nozzle_diameter(m_object, m_object_config->support_filament);
    const coordf_t support_lw = m_object_config->support_line_width.value > 0 ?
        Flow::extrusion_width_for_nozzle(
            m_object_config->support_line_width,
            float(primary_nozzle_diameter(m_object->print()->full_print_config())),
            float(support_nozzle),
            multi_nozzle_diameter_mode_enabled(m_object->print()->full_print_config())) :
        m_support_params.support_extrusion_width;
    float max_move = scale_(support_lw / 2);
    // if the branch is very tall, the tip also needs extra wall
    float thresh_tall_branch = 100;
    float thresh_dist_to_top = 30;

    for (int layer_nr = 0; layer_nr< contact_nodes.size(); layer_nr++) {
        std::vector<SupportNode *> &curr_layer_nodes = contact_nodes[layer_nr];
        if (curr_layer_nodes.empty()) continue;
        for (SupportNode *node : curr_layer_nodes) {
            if (!node->is_processed) {
                std::vector<Point> pts;
                std::vector<double> radii;
                std::vector<SupportNode *>   branch;
                SupportNode *              p_node = node;
                float                        total_height = 0;
                // add a fixed head if it's not a polygon node, see STUDIO-4403
                // Polygon node can't be added because the move distance might be huge, making the nodes in between jump and dangling
                if (node->child && node->child->type!=ePolygon) {
                    pts.push_back(p_node->child->position);
                    radii.push_back(p_node->child->radius);
                    branch.push_back(p_node->child);
                    total_height += p_node->child->height;
                }
                do {
                    pts.push_back(p_node->position);
                    radii.push_back(p_node->radius);
                    branch.push_back(p_node);
                    total_height += p_node->height;
                    p_node = p_node->parent;
                } while (p_node && !p_node->is_processed);
                if (pts.size() < 3) continue;

                std::vector<Point> pts1 = pts;
                std::vector<double> radii1 = radii;
                // TODO here we assume layer height gap is constant. If not true, need to consider height jump
                const int iterations = 100;
                for (size_t k = 0; k < iterations; k++) {
                    for (size_t i = 1; i < pts.size() - 1; i++) {
                        Point pt = ( pts[i - 1] + pts[i] + pts[i + 1] ) / 3;
                        pts1[i] = pt;
                        radii1[i] = (radii[i - 1] + radii[i] + radii[i + 1] ) / 3;
                        if (k == iterations - 1) {
                            branch[i]->position = pt;
                            branch[i]->radius = radii1[i];
                            branch[i]->movement = (pts[i + 1] - pts[i - 1]) / 2;
                            branch[i]->is_processed = true;
                            branch[i]->overhang_degree = std::min(10.0, unscale_((branch[i]->movement).cast<double>().norm()) / support_lw * 10);
#if USE_TREE_SUPPORT_CARRIED_MASS
                            if ((branch[i]->movement.x() > max_move || branch[i]->movement.y() > max_move) ||
                                (total_height > thresh_tall_branch && branch[i]->dist_mm_to_top < thresh_dist_to_top))
                                branch[i]->need_extra_wall = true;
#else
                            if (branch[i]->parents.size() > 1 || (branch[i]->movement.x() > max_move || branch[i]->movement.y() > max_move) ||
                                (total_height > thresh_tall_branch && branch[i]->dist_mm_to_top < thresh_dist_to_top))
                                branch[i]->need_extra_wall = true;
#endif
                            BOOST_LOG_TRIVIAL(trace) << "layer_nr=" << layer_nr << ", i=" << i << ", pt=" << pt << ", movement=" << branch[i]->movement << ", radius=" << branch[i]->radius;
                        }
                    }
                    if (k < iterations - 1) {
                        std::swap(pts, pts1);
                        std::swap(radii, radii1);
                    }
                }
#if !USE_TREE_SUPPORT_CARRIED_MASS
                // interpolate need_extra_wall in the end
                int idx_first_double_wall = -1;
                int idx_last_double_wall  = -1;
                for (size_t i = 0; i < pts.size(); i++) {
                    if (branch[i]->need_extra_wall) {
                        if (idx_first_double_wall < 0) idx_first_double_wall = i;
                        idx_last_double_wall = i;
                    }
                }
                if (idx_last_double_wall >= 0 && idx_first_double_wall >= 0 && idx_last_double_wall > idx_first_double_wall) {
                    for (size_t i = idx_first_double_wall + 1; i < idx_last_double_wall; i++) branch[i]->need_extra_wall = true;
                }
#endif
            }
        }
    }
}

#if USE_SUPPORT_3D
void TreeSupport::smooth_nodes(const TreeSupport3D::TreeSupportSettings& config)
{
    const coordf_t branch_radius = m_object_config->tree_support_branch_diameter.value / 2;
    const coordf_t branch_radius_scaled = scale_(branch_radius);
    // contact nodes to support elements
    TreeSupport3D::SupportElements move_bounds;
    std::map<SupportNode*, int> node2elemIdx;
    std::map<int, SupportNode*> elemIdx2node;
    int n = 0;
    for (int layer_nr = 0; layer_nr < contact_nodes.size(); layer_nr++) {
        std::vector<SupportNode*>& curr_layer_nodes = contact_nodes[layer_nr];
        for (SupportNode* node : curr_layer_nodes) {
            if (node->distance_to_top < 0) continue;
            TreeSupport3D::SupportElementState state;
            state.type = node->type;
            state.radius = node->radius;
            state.print_z = node->print_z;
            state.target_height = node->obj_layer_nr;
            state.target_position = node->position;
            state.next_position = node->position;
            state.layer_idx = node->obj_layer_nr;
            state.effective_radius_height = node->distance_to_top;
            state.to_buildplate = node->to_buildplate;
            state.distance_to_top = node->distance_to_top;
            state.result_on_layer = node->position;
            state.increased_to_model_radius = 0;
            state.to_model_gracious = true;//TODO
            state.elephant_foot_increases = 0;
            state.use_min_xy_dist = true;//TODO
            state.supports_roof = node->distance_to_top <= m_object_config->support_interface_top_layers.value;
            state.dont_move_until = DO_NOT_MOVER_UNDER_MM/m_object_config->layer_height.value;
            state.can_use_safe_radius = true;//TODO
            state.missing_roof_layers = 0;
            state.skip_ovalisation = false;
            Polygons circle;
            if (node->type == eCircle) {
                const Polygon base_circle = make_circle(scale_(std::max(1.0,node->radius)), SUPPORT_TREE_CIRCLE_RESOLUTION);
                circle.emplace_back(base_circle);
                circle.front().translate(node->position);
            }
            else if (node->type == ePolygon) {
                circle.emplace_back(node->overhang.contour);
            }
            move_bounds.emplace_back(state, std::move(circle));
            node2elemIdx.emplace(node, n);
            elemIdx2node.emplace(n, node);
            n++;
        }
    }

    // All SupportElements are put into a layer independent storage to improve parallelization.
    std::vector<std::pair<TreeSupport3D::SupportElement*, int>> elements_with_link_down;
    std::vector<size_t>                          linear_data_layers;

    do
    {
        for (size_t i = 0; i < move_bounds.size(); i++) {
            TreeSupport3D::SupportElement* elem = &move_bounds[i];
            SupportNode* node= elemIdx2node[i];
            size_t child = i;
            for(auto parent : node->parents) {
                size_t idx_parent = parent ? node2elemIdx[parent] : 0;
                if (!parent || idx_parent >= move_bounds.size()) child = -1;
                elem->parents.push_back(idx_parent);
                TreeSupport3D::SupportElement* elem_parent = &move_bounds[idx_parent];
                if (elem_parent->state.layer_idx == 0) child = -1;
                elements_with_link_down.push_back({ elem_parent, child });
            }
        }
        for (int layer_idx = 0; layer_idx < int(contact_nodes.size()); ++layer_idx) {
            linear_data_layers.emplace_back(0);
        }
        break;


        std::vector<std::pair<SupportNode*, int>> map_downwards_old;
        std::vector<std::pair<SupportNode*, int>> map_downwards_new;
        linear_data_layers.emplace_back(0);
        for (int layer_idx = 0; layer_idx < int(contact_nodes.size()); ++layer_idx) {
            std::sort(map_downwards_old.begin(), map_downwards_old.end(), [](auto& l, auto& r) { return l.first < r.first;  });
            auto& layer = contact_nodes[layer_idx];
            for (size_t elem_idx = 0; elem_idx < layer.size(); ++elem_idx) {
                SupportNode* node = layer[elem_idx];
                int child = -1;
                if (layer_idx > 0) {
                    auto it = std::lower_bound(map_downwards_old.begin(), map_downwards_old.end(), node, [](auto& l, const SupportNode* r) { return l.first < r; });
                    if (it != map_downwards_old.end() && it->first == node) {
                        child = it->second;
                        // Only one link points to a node above from below.
                        assert(!(++it != map_downwards_old.end() && it->first == node));
                    }
                    const SupportNode* pchild = child == -1 ? nullptr : contact_nodes[layer_idx - 1][child];
                }
                TreeSupport3D::SupportElement* elem = &move_bounds[node2elemIdx[node]];
                if (node->parent) {
                    map_downwards_new.emplace_back(node->parent, elem_idx);
                    elem->parents.push_back(node2elemIdx[node->parent]);
                    //for (auto parent_neighbor : node->parent->merged_neighbours) {
                    //    assert(parent_neighbor);
                    //    if (parent_neighbor) {
                    //        map_downwards_new.emplace_back(parent_neighbor, elem_idx);
                    //        elem->parents.push_back(node2elemIdx[parent_neighbor]);
                    //    }
                    //}
                }
                elements_with_link_down.push_back({ elem, int(child)});
            }
            std::swap(map_downwards_old, map_downwards_new);
            // index of parents are directly based on the beginning of move_bounds, so no need to store the size of the previous layer.
            linear_data_layers.emplace_back(0);
        }
    } while (0);

    throw_on_cancel();

    organic_smooth_branches_avoid_collisions(*m_object, *this->m_model_volumes, config, elements_with_link_down, linear_data_layers, throw_on_cancel);
}
#endif

std::vector<LayerHeightData> TreeSupport::plan_layer_heights()
{
    std::vector<LayerHeightData> layer_heights;
    std::map<coordf_t, coordf_t> z_heights; // print_z:height
    if (!m_support_params.independent_layer_height) {
        layer_heights.resize(m_object->layer_count());
        for (int layer_nr = 0; layer_nr < m_object->layer_count(); layer_nr++) {
            z_heights[m_object->get_layer(layer_nr)->print_z] = m_object->get_layer(layer_nr)->height;
            layer_heights[layer_nr] = {m_object->get_layer(layer_nr)->print_z, m_object->get_layer(layer_nr)->height, size_t(layer_nr)};
        }
    } else {
        const coordf_t               max_layer_height = m_slicing_params.max_suport_layer_height;
        const coordf_t               min_layer_height = m_slicing_params.min_layer_height;
        std::map<coordf_t, coordf_t> bounds; // print_z: height
        // Keep first layer still
        bounds[m_object->get_layer(0)->print_z] = {m_object->get_layer(0)->height};
        std::vector<float> obj_layer_zs;
        obj_layer_zs.reserve(m_object->layer_count());
        for (const Layer *l : m_object->layers()) obj_layer_zs.emplace_back((float) l->print_z);
        z_heights[m_object->get_layer(0)->print_z] = m_object->get_layer(0)->height;
        // Collect top contact layers
        for (int layer_nr = 1; layer_nr < contact_nodes.size(); layer_nr++) {
            if (!contact_nodes[layer_nr].empty()) {
                coordf_t print_z = contact_nodes[layer_nr].front()->print_z;
                coordf_t height = contact_nodes[layer_nr].front()->height;
                // insertion will fail if there is already a key of print_z, so no need to check
                bounds.insert({print_z, height});
                bounds.insert({print_z - height, 0}); // the bottom_z of the layer
            }
        }

        auto it1 = bounds.begin();
        auto it2 = std::next(it1);
        for (; it2 != bounds.end(); it2++) {
            coordf_t z2   = it2->first;
            coordf_t h2   = it2->second;
            coordf_t z1   = it1->first;
            coordf_t h1   = it1->second;
            coordf_t dist = z2 - z1;
            if (dist < min_layer_height - EPSILON) continue;

            BOOST_LOG_TRIVIAL(trace) << format("plan_layer_heights0 (%.2f,%.2f)->(%.2f,%.2f): ", z1, h1, z2, h2);

            // Insert intermediate layers.
            size_t   n_layers_extra = size_t(ceil(dist / max_layer_height));
            coordf_t step = dist / coordf_t(n_layers_extra);
            coordf_t print_z        = z1 + step;
            for (int i = 0; i < n_layers_extra; i++, print_z += step) {
                z_heights[print_z] = step;
                BOOST_LOG_TRIVIAL(debug) << "add entry print_z, height: " << print_z << " " << step;
            }
            it1=it2;
        }

        // map z_heights to layer_heights
        int i                = 0;
        size_t obj_layer_nr     = 0;
        layer_heights.resize(z_heights.size());
        for (auto it = z_heights.begin(); it != z_heights.end(); it++, i++) {
            coordf_t print_z         = it->first;
            coordf_t height          = it->second;
            while (obj_layer_nr < obj_layer_zs.size() && obj_layer_zs[obj_layer_nr] < print_z - height / 2) obj_layer_nr++;
            layer_heights[i]       = {print_z, height, obj_layer_nr};

        }
    }

    // add support layers according to layer_heights
    int support_layer_nr = m_raft_layers;
    for (size_t i = 0; i < layer_heights.size(); i++, support_layer_nr++) {
        SupportLayer *ts_layer = m_object->add_tree_support_layer(support_layer_nr, layer_heights[i].height, layer_heights[i].print_z,
                                                                  layer_heights[i].print_z - layer_heights[i].height / 2.);
        if (ts_layer->id() > m_raft_layers) {
            SupportLayer *lower_layer = m_object->get_support_layer(ts_layer->id() - 1);
            if (lower_layer) {
                lower_layer->upper_layer = ts_layer;
                ts_layer->lower_layer    = lower_layer;
            }
        }
    }


    // re-distribute contact_nodes to support layers
    decltype(contact_nodes) contact_nodes2(support_layer_nr);
    for (int layer_nr = 0; layer_nr < contact_nodes.size(); layer_nr++) {
        if (contact_nodes[layer_nr].empty()) continue;
        SupportNode *node1 = contact_nodes[layer_nr].front();
        auto         it    = std::min_element(layer_heights.begin(), layer_heights.end(), [node1](const LayerHeightData &l1, const LayerHeightData &l2) {
            return std::abs(l1.print_z - node1->print_z) < std::abs(l2.print_z - node1->print_z);
        });
        if (it == layer_heights.end()) it = std::prev(it);
        int layer_nr2 = std::distance(layer_heights.begin(), it);
        contact_nodes2[layer_nr2].insert(contact_nodes2[layer_nr2].end(), contact_nodes[layer_nr].begin(), contact_nodes[layer_nr].end());
    }
    contact_nodes = contact_nodes2;

    // adjust contact nodes' distance_to_top and support_roof_layers_below according to layer_heights
    // In case of very large top z distance, one gap layer is not enough, we need to split it into multiple layers
    for (int layer_nr = 0; layer_nr < contact_nodes.size(); layer_nr++) {
        if (contact_nodes[layer_nr].empty()) continue;
        SupportNode *node1 = contact_nodes[layer_nr].front();
        BOOST_LOG_TRIVIAL(debug) << format("node1->layer_nr,printz,height,distance_to_top: %d, %.2f,%.2f, %d", layer_nr, node1->print_z, node1->height, node1->distance_to_top)
                                 << ", object_layer_zs[" << layer_heights[layer_nr].obj_layer_nr << "]=" << m_object->get_layer(layer_heights[layer_nr].obj_layer_nr)->print_z;
        coordf_t new_height = layer_heights[layer_nr].height;
        if (std::abs(node1->height - new_height) < EPSILON) continue;
        if (top_z_distance < EPSILON && node1->height < EPSILON) continue; // top_z_distance==0, this is soluable interface
        coordf_t              accum_height = 0;
        int                   num_layers   = 0;
        for (int i=layer_nr;i>=0;i--){
            if (layer_heights[i].height > EPSILON) {
                accum_height += layer_heights[i].height;
                num_layers++;
                if (accum_height > node1->height - EPSILON) break;
            }
        }
        BOOST_LOG_TRIVIAL(debug) << format("adjust node's height print_z[%d]=%.2f: (%.3f,%d)->(%.3f,%.3f,%d)", layer_nr, node1->print_z,
            node1->height, node1->distance_to_top, new_height, accum_height, -num_layers);
        for (SupportNode *node : contact_nodes[layer_nr]) {
            node->height          = new_height;
            node->distance_to_top = -num_layers;
            node->support_roof_layers_below += num_layers - 1;
        }
    }

    // log layer_heights
    for (size_t i = 0; i < layer_heights.size(); i++) {
        //if (layer_heights[i].height > EPSILON)
            BOOST_LOG_TRIVIAL(trace) << format("plan_layer_heights[%d] print_z, height: %.2f, %.2f",i, layer_heights[i].print_z, layer_heights[i].height);
    }
    return layer_heights;
}

void TreeSupport::generate_contact_points()
{
    const PrintObjectConfig &config = m_object->config();
    const coordf_t point_spread = scale_(config.tree_support_branch_distance.value);
    const coordf_t max_bridge_length = scale_(config.max_bridge_length.value);
    coord_t    radius_scaled         = scale_(base_radius);
    bool       on_buildplate_only    = m_object_config->support_on_build_plate_only.value;
    const bool roof_enabled          = config.support_interface_top_layers.value > 0;
    const bool force_tip_to_roof = roof_enabled && m_support_params.soluble_interface;
    const coordf_t extrusion_width = Flow::extrusion_width_for_nozzle(
        config.line_width,
        float(primary_nozzle_diameter(m_object->print()->full_print_config())),
        support_nozzle_diameter(m_object, config.support_filament),
        multi_nozzle_diameter_mode_enabled(m_object->print()->full_print_config()));

    //First generate grid points to cover the entire area of the print.
    BoundingBox bounding_box = m_object->bounding_box();
    const Point bounding_box_size = bounding_box.max - bounding_box.min;
    constexpr double rotate_angle = 22.0 / 180.0 * M_PI;

    const auto center = bounding_box_middle(bounding_box);
    const auto sin_angle = std::sin(rotate_angle);
    const auto cos_angle = std::cos(rotate_angle);
    const Point rotated_dims = Point(
        bounding_box_size(0) * cos_angle + bounding_box_size(1) * sin_angle,
        bounding_box_size(0) * sin_angle + bounding_box_size(1) * cos_angle) / 2;

    std::vector<Point> grid_points;
    coordf_t sample_step = std::max(point_spread, max_bridge_length / 2);
    for (auto x = -rotated_dims(0); x < rotated_dims(0); x += sample_step) {
        for (auto y = -rotated_dims(1); y < rotated_dims(1); y += sample_step) {
            Point pt(x, y);
            pt.rotate(cos_angle, sin_angle);
            pt += center;
            if (bounding_box.contains(pt)) {
                grid_points.push_back(pt);
            }
        }
    }

    const coordf_t layer_height = config.layer_height.value;
    coordf_t       z_distance_top = this->top_z_distance;
  //  if (!m_support_params.independent_layer_height) {
  //      z_distance_top = round(z_distance_top / layer_height) * layer_height;
  //  // BBS: add extra distance if thick bridge is enabled
  //  // Note: normal support uses print_z, but tree support uses integer layers, so we need to subtract layer_height
  //  if (!m_slicing_params.soluble_interface && m_object_config->thick_bridges) {
  //      z_distance_top += m_object->layers()[0]->regions()[0]->region().bridging_height_avg(m_object->print()->config()) - layer_height;
		//}
  //  }
    const int z_distance_top_layers = round_up_divide(scale_(m_top_z_max), scale_(layer_height)) + 1; //Support must always be 1 layer below overhang.
    int gap_layers = z_distance_top == 0 ? 0 : 1;
    // BBS multi-diameter: the gap follows the overhang's nozzle, so one layer's contacts may need different
    // gaps. plan_layer_heights() splits a gap into virtual layers from the first contact of each layer only,
    // so split each contact here instead, exactly as it would: count the layers below until the gap is filled
    // and hand it the layer's own height, which plan_layer_heights() then leaves alone. The supports are on
    // the object layer grid whenever m_top_z_outlines is filled.
    const bool presplit_gap = ! m_top_z_outlines.empty();

    size_t support_roof_layers = config.support_interface_top_layers.value;
    if (support_roof_layers > 0)
        support_roof_layers += 1; // BBS: add a normal support layer below interface (if we have interface)
    coordf_t  thresh_angle = std::min(89.f, config.support_threshold_angle.value < EPSILON ? 30.f : config.support_threshold_angle.value);
    coordf_t half_overhang_distance = scale_(tan(thresh_angle * M_PI / 180.0) * layer_height / 2);
    coordf_t xy_expansion           = scale_(config.support_expansion.value);
    if (m_support_params.soluble_interface && xy_expansion < EPSILON) xy_expansion = scale_(2);

    // fix bug of generating support for very thin objects
    if (m_object->layers().size() <= z_distance_top_layers + 1)
        return;

    contact_nodes.clear();
    contact_nodes.resize(m_object->layers().size());

    tbb::spin_mutex mtx;

    // add vertical enforcer points
    std::vector<float> zs = zs_from_layers(m_object->layers());
    std::vector<std::vector<std::pair<Point,Point>>> vertical_enforcer_points_by_layers(m_object->layer_count());
    for (auto& pt_and_normal : m_vertical_enforcer_points) {
        auto pt = pt_and_normal.first;
        auto normal = pt_and_normal.second;  // normal seems useless
        auto iter = std::lower_bound(zs.begin(), zs.end(), pt.z());
        if (iter != zs.end()) {
            size_t layer_nr = iter - zs.begin();
            if (layer_nr > 0 && layer_nr < contact_nodes.size()) {
                vertical_enforcer_points_by_layers[layer_nr].push_back({ to_2d(pt).cast<coord_t>(),scaled(to_2d(normal)) });
            }
        }
    }

    int      nonempty_layers = 0;
    tbb::concurrent_vector<Slic3r::Vec3f> all_nodes;
    tbb::parallel_for(tbb::blocked_range<size_t>(1, m_object->layers().size()), [&](const tbb::blocked_range<size_t>& range) {
        for (size_t layer_nr = range.begin(); layer_nr < range.end(); layer_nr++) {
            if (m_object->print()->canceled())
                break;
            Layer* layer = m_object->get_layer(layer_nr);
            auto& curr_nodes = contact_nodes[layer_nr-1];

            std::unordered_set<Point, PointHash> already_inserted;
            auto                                 bottom_z = m_object->get_layer(layer_nr)->bottom_z();
            bool                                 added = false; // Did we add a point this way?
            bool                                 is_sharp_tail = false;
            coordf_t                             overhang_top_z = z_distance_top;
            int                                  overhang_gap_layers = gap_layers;
            coordf_t                             overhang_gap_height = z_distance_top;
            auto set_overhang_top_z = [&](coordf_t top_z) {
                overhang_top_z      = top_z;
                overhang_gap_layers = gap_layers;
                overhang_gap_height = top_z;
                if (! presplit_gap)
                    return;
                coordf_t accum_height = 0;
                overhang_gap_layers   = 0;
                for (int i = int(layer_nr) - 1; i >= 0; --i) {
                    accum_height += m_object->get_layer(i)->height;
                    ++overhang_gap_layers;
                    if (accum_height > top_z - EPSILON)
                        break;
                }
                overhang_gap_height = m_object->get_layer(layer_nr - 1)->height;
            };

            auto insert_point = [&](Point pt, const ExPolygon& overhang, double radius, bool force_add = false, bool add_interface=true) {
                Point        hash_pos = pt / ((radius_scaled + 1) / 1);
                SupportNode* contact_node = nullptr;
                if (force_add || !already_inserted.count(hash_pos)) {
                    already_inserted.emplace(hash_pos);
                    bool to_buildplate = true;
                    size_t roof_layers = add_interface ? support_roof_layers : 0;
                    // add a new node as a virtual node which acts as the invisible gap between support and object
                    // distance_to_top<0: it's virtual (-1, or minus the gap layers once pre-split)
                    // print_z=object_layer->bottom_z: it directly contacts the bottom
                    // height=z_distance_top: it's height is exactly the gap distance (the layer height once pre-split)
                    // dist_mm_to_top=0: it directly contacts the bottom
                    if (bottom_z - overhang_top_z < m_object->get_layer(0)->print_z) return contact_node; // dont add it if overlapping with the initial layer
                    contact_node = m_ts_data->create_node(pt, -overhang_gap_layers, layer_nr - 1, roof_layers + overhang_gap_layers, to_buildplate, SupportNode::NO_PARENT, bottom_z,
                                                          overhang_gap_height, 0, radius);
                    contact_node->overhang = overhang;
                    contact_node->is_sharp_tail = is_sharp_tail;
                    contact_node->carried_mass = float(M_PI * SQ(std::max(radius, tip_radius)));
                    curr_nodes.emplace_back(contact_node);
                    added = true;
                };
                return contact_node;
            };

            auto extrudable_collision = offset_ex(layer->lower_layer->lslices_extrudable, m_ts_data->m_xy_distance);
            for (const auto& overhang_with_type : layer->loverhangs_with_type)  {
                const auto &overhang_part = overhang_with_type.first;
                const auto &overhang_type = overhang_with_type.second;
                is_sharp_tail = overhang_type & OverhangType::SharpTail;
                set_overhang_top_z(top_z_for_overhang(layer_nr, overhang_part));
                bool       add_interface  = (force_tip_to_roof || area(overhang_part) > minimum_roof_area);
                const auto &relevant_forbidden = get_collision(0, layer_nr - 1);
                ExPolygons overhangs{overhang_part};
                ExPolygons overhangs_regular;
                if (!is_sharp_tail && (config.support_expansion.value > EPSILON))
                    overhangs = safe_offset_inc({overhang_part}, scale_(config.support_expansion.value), relevant_forbidden, scale_(MIN_BRANCH_RADIUS * 1.75), 0, 1);
                if (m_support_params.support_style == smsTreeHybrid &&
                    (overhang_type & (BigFlat | ThinPlate))) {
                    overhangs_regular = intersection_ex(overhangs, offset_ex(m_ts_data->m_layer_outlines_below[layer_nr - 1], scale_(config.support_object_xy_distance.value)));
                    ExPolygons overhangs_normal = offset2_ex(diff_ex({overhang_part}, overhangs_regular), scale_(extrusion_width), -scale_(extrusion_width));
                    if (add_interface && xy_expansion > EPSILON && !is_sharp_tail)
                        overhangs_normal = safe_offset_inc(overhangs_normal, xy_expansion,
                                                           offset_ex(m_ts_data->m_layer_outlines_below[layer_nr - 1], scale_(config.support_object_xy_distance.value)),
                                                           scale_(MIN_BRANCH_RADIUS * 1.75), 0, 1);
                    // if the outside area is still big, we can need normal nodes
                    coord_t gap_width = scale_(extrusion_width / 2.) + scale_(m_ts_data->m_xy_distance);
                    ExPolygons overhangs_normal_split;
                    for (auto &overhang : overhangs_normal) {
                        ExPolygons sub_overhangs = overhang.split_expoly_with_holes(gap_width, get_collision(0, layer_nr));
                        if (sub_overhangs.size() > 0)
                            for (auto &sub : sub_overhangs) overhangs_normal_split.emplace_back(sub);
                        else
                            overhangs_normal_split.emplace_back(overhang);
                    }
                    for (auto &overhang : overhangs_normal_split) {
                        if (!is_stable(layer->bottom_z(), overhang, 0) || overhang.area() < SQ(scale_(2.))) {
                            ExPolygons unstable_overhangs = intersection_ex({overhang}, overhangs);
                            overhangs_regular.insert(overhangs_regular.end(), unstable_overhangs.begin(), unstable_overhangs.end());
                            continue;
                        }
                        BoundingBox  overhang_bounds = get_extents(overhang);
                        double       radius          = unscale_(overhang_bounds.radius());
                        Point        candidate       = overhang_bounds.center();
                        SupportNode *contact_node    = insert_point(candidate, overhang, radius, true, true);
                        if (!contact_node) continue;
                        contact_node->type           = ePolygon;
                        contact_node->carried_mass   = std::max(contact_node->carried_mass, float(overhang.area() * SCALING_FACTOR * SCALING_FACTOR));
                        curr_nodes.emplace_back(contact_node);
                    }
                }
                else{
                    overhangs_regular = overhangs;
                }

                if (!is_sharp_tail && layer->lower_layer) 
                    overhangs_regular = diff_ex(overhangs_regular, extrudable_collision);
                for (auto &overhang : overhangs_regular) {
                    const size_t nodes_before = curr_nodes.size();
                    if (is_sharp_tail && !m_support_params.soluble_interface && overhang.area() < SQ(scale_(2.))) add_interface = false;
                    BoundingBox overhang_bounds = get_extents(overhang);
                    double      radius          = std::clamp(unscale_(overhang_bounds.radius()), tip_radius, base_radius);
                    // add supports at corners for both auto and manual overhangs, github #2008
                    auto &points = overhang.contour.points;
                    int   nSize = points.size();
                    for (int i = 0; i < nSize; i++) {
                        auto pt = points[i];
                        auto v1 = (pt - points[(i - 1 + nSize) % nSize]).cast<double>().normalized();
                        auto v2 = (pt - points[(i + 1) % nSize]).cast<double>().normalized();
                        if (v1.dot(v2) > -0.7) { // angle smaller than 135 degrees
                            SupportNode *contact_node = insert_point(pt, overhang, radius, false, add_interface);
                            if (contact_node) {
                                contact_node->is_corner = true;
                            }
                        }
                    }

                    // add supports along contours
                    libnest2d::placers::EdgeCache<ExPolygon> edge_cache(overhang);
                    for (size_t i = 0; i < edge_cache.holeCount() + 1; i++) {
                        double step     = point_spread / (i == 0 ? edge_cache.circumference() : edge_cache.circumference(i - 1));
                        double distance = 0;
                        while (distance < 1) {
                            auto         pt           = i == 0 ? edge_cache.coords(distance) : edge_cache.coords(i - 1, distance);
                            SupportNode *contact_node = insert_point(pt, overhang,radius, false, add_interface);
                            distance += step;
                        }
                    }

                    // don't add inner supports for sharp tails
                    if (is_sharp_tail) {
                        Point cent = overhang.contour.centroid();
                        // For ring/concave sharp tails the centroid may fall outside the overhang
                        // (e.g. inside its hole). In that case project it onto the nearest edge of
                        // the overhang so the sharp tail still gets a contact point.
                        if (!is_inside_ex({overhang}, cent)) cent = overhang.point_projection(cent);
                        insert_point(cent, overhang, radius, false, add_interface);
                    } else {
                        // add inner supports
                        overhang_bounds.inflated(-radius_scaled);
                        ExPolygons overhang_inner = offset_ex(overhang, -radius_scaled);
                        for (Point candidate : grid_points) {
                            if (overhang_bounds.contains(candidate)) {
                                // BBS: move_inside_expoly shouldn't be used if candidate is already inside, as it moves point to boundary and the inside is not well supported!
                                bool is_inside = is_inside_ex(overhang_inner, candidate);
                                if (is_inside) { SupportNode *contact_node = insert_point(candidate, overhang,radius, false, add_interface); }
                            }
                        }
                    }
                    const size_t nodes_added = curr_nodes.size() - nodes_before;
                    if (nodes_added > 0) {
                        const float overhang_area_mm2 = float(overhang.area() * SCALING_FACTOR * SCALING_FACTOR);
                        const float node_mass         = std::max(overhang_area_mm2 / float(nodes_added), float(M_PI * SQ(tip_radius)));
                        for (size_t idx = nodes_before; idx < curr_nodes.size(); ++idx)
                            curr_nodes[idx]->carried_mass = node_mass;
                    }
                }
            }
            if (! vertical_enforcer_points_by_layers[layer_nr].empty())
                set_overhang_top_z(top_z_at(layer_nr));
            for (auto& pt_and_normal : vertical_enforcer_points_by_layers[layer_nr]) {
                is_sharp_tail = true;// fake it as sharp tail point so the contact distance will be 0
                auto vertical_enforcer_point= pt_and_normal.first;
                auto node=insert_point(vertical_enforcer_point, ExPolygon(), false);
                if (node)
                    node->skin_direction = pt_and_normal.second;
            }
            if (!curr_nodes.empty()) nonempty_layers++;
            for (auto node : curr_nodes) { all_nodes.emplace_back(node->position(0), node->position(1), scale_(node->print_z)); }
#ifdef SUPPORT_TREE_DEBUG_TO_SVG
            if (!curr_nodes.empty())
            draw_contours_and_nodes_to_svg(debug_out_path("init_contact_points_%.2f.svg", bottom_z), layer->loverhangs,layer->lslices_extrudable, m_ts_data->m_layer_outlines_below[layer_nr],
                contact_nodes[layer_nr], contact_nodes[layer_nr - 1], { "overhang","lslices","outlines_below"});
#endif
        }}
    ); // end tbb::parallel_for



    int nNodes = all_nodes.size();
    avg_node_per_layer = nodes_angle = 0;
    if (nNodes > 0) {
        avg_node_per_layer = nNodes / nonempty_layers;
        // get orientation of nodes by line fitting
        // line: y=kx+b, where
        //       k=tan(nodes_angle)=(n\sum{xy}-\sum{x}\sum{y})/(n\sum{x^2}-\sum{x}^2)
        float mx = 0, my = 0, mxy = 0, mx2 = 0;
        for (auto &pt : all_nodes) {
            float x = unscale_(pt(0));
            float y = unscale_(pt(1));
            mx += x;
            my += y;
            mxy += x * y;
            mx2 += x * x;
        }
        nodes_angle = atan2(nNodes * mxy - mx * my, nNodes * mx2 - SQ(mx));

        BOOST_LOG_TRIVIAL(info) << "avg_node_per_layer=" << avg_node_per_layer << ", nodes_angle=" << nodes_angle;
    }
}

void TreeSupport::insert_dropped_node(std::vector<SupportNode*>& nodes_layer, SupportNode* p_node)
{
    std::vector<SupportNode*>::iterator conflicting_node_it = std::find(nodes_layer.begin(), nodes_layer.end(), p_node);
    if (conflicting_node_it == nodes_layer.end()) //No conflict.
    {
        nodes_layer.emplace_back(p_node);
        return;
    }

    SupportNode* conflicting_node = *conflicting_node_it;
    conflicting_node->distance_to_top = std::max(conflicting_node->distance_to_top, p_node->distance_to_top);
    conflicting_node->support_roof_layers_below = std::max(conflicting_node->support_roof_layers_below, p_node->support_roof_layers_below);
}

TreeSupportData::TreeSupportData(const PrintObject &object, coordf_t xy_distance, coordf_t radius_sample_resolution)
    : m_xy_distance(xy_distance), m_radius_sample_resolution(radius_sample_resolution)
{
    branch_scale_factor = tan(object.config().tree_support_branch_angle.value * M_PI / 180.);
    clear_nodes();
    m_max_move_distances.resize(object.layers().size(), 0);
    m_layer_outlines.resize(object.layers().size());
    m_layer_outlines_below.resize(object.layer_count());
    ExPolygons machine_border;
    ExPolygon  m_machine_border;

    //cal m_machine_border twice, this may happen before TreeSupport builds
    m_machine_border.contour = get_bed_shape_with_excluded_area(object.print()->config());
    Vec3d plate_offset       = object.print()->get_plate_origin();
    // align with the centered object in current plate (may not be the 1st plate, so need to add the plate offset)
    m_machine_border.translate(Point(scale_(plate_offset(0)), scale_(plate_offset(1))) - object.instances().front().shift);

    if (!m_machine_border.empty()) {
        Polygon hole(m_machine_border.contour);
        hole.reverse();
        ExPolygon  machine_outline(offset(m_machine_border.contour, scale_(1000))[0], hole);
        machine_border = machine_outline.split_expoly_with_holes(scale_(1.), {m_machine_border});
    }

    for (std::size_t layer_nr = 0; layer_nr < object.layers().size(); ++layer_nr) {
        const Layer *layer             = object.get_layer(layer_nr);
        m_max_move_distances[layer_nr] = layer->height * branch_scale_factor;
        ExPolygons &outline            = m_layer_outlines[layer_nr];
        outline.clear();
        outline.reserve(layer->lslices.size());
        for (const ExPolygon &poly : layer->lslices) { append(outline, union_ex_2( poly.simplify_p(scale_(m_radius_sample_resolution)))); }
        // BBS multi-diameter: a merged band extrudes its retained footprint over the whole band, down to the
        // band bottom, where lslices may not contain it at all (a bridge underside sitting high in the band).
        // Overhang detection already places such an overhang at the band bottom, so the collision has to
        // know that material occupies the swallowed layers as well. suppressed_area() claims no Z.
        bool has_patch_area = false;
        for (const LayerRegion *layerm : layer->regions())
            for (const AdaptiveMergePatch &patch : layerm->adaptive_merge_patches)
                for (const ExPolygon &poly : patch.area()) {
                    append(outline, union_ex_2(poly.simplify_p(scale_(m_radius_sample_resolution))));
                    has_patch_area = true;
                }
        if (has_patch_area)
            outline = union_ex(outline);
        append(outline, machine_border);

        if (layer_nr == 0)
            m_layer_outlines_below[layer_nr] = outline;
        else {
            m_layer_outlines_below[layer_nr] = m_layer_outlines_below[layer_nr - 1];
            m_layer_outlines_below[layer_nr].insert(m_layer_outlines_below[layer_nr].end(), outline.begin(), outline.end());
            if (layer_nr % 10 == 0) m_layer_outlines_below[layer_nr] = union_ex_2(m_layer_outlines_below[layer_nr]);
        }
    }
}

const ExPolygons& TreeSupportData::get_collision(coordf_t radius, size_t layer_nr) const
{
    profiler.tic();
    radius = ceil_radius(radius);
    RadiusLayerPair key{radius, layer_nr};
    const auto it = m_collision_cache.find(key);
    const ExPolygons& collision = it != m_collision_cache.end() ? it->second : calculate_collision(key);
    profiler.stage_add(STAGE_get_collision);
    return collision;
}

const ExPolygons& TreeSupportData::get_avoidance(coordf_t radius, size_t layer_nr, int recursions) const
{
    profiler.tic();
    radius = ceil_radius(radius);
    RadiusLayerPair key{radius, layer_nr, recursions };
    const auto it = m_avoidance_cache.find(key);
    const ExPolygons& avoidance = it != m_avoidance_cache.end() ? it->second : calculate_avoidance(key);

    profiler.stage_add(STAGE_GET_AVOIDANCE);
    return avoidance;
}

Polygons TreeSupportData::get_contours(size_t layer_nr) const
{
    Polygons contours;
    for (const ExPolygon expoly : m_layer_outlines[layer_nr]) {
        contours.push_back(expoly.contour);
    }

    return contours;
}

Polygons TreeSupportData::get_contours_with_holes(size_t layer_nr) const
{
    Polygons contours;
    for (const ExPolygon expoly : m_layer_outlines[layer_nr]) {
        for(int i=0;i<expoly.num_contours();i++)
            contours.push_back(expoly.contour_or_hole(i));
    }
    return contours;
}

SupportNode* TreeSupportData::create_node(const Point position, const int distance_to_top, const int obj_layer_nr, const int support_roof_layers_below, const bool to_buildplate, SupportNode* parent, coordf_t print_z_, coordf_t height_, coordf_t dist_mm_to_top_, coordf_t radius_)
{
    // this function may be called from multiple threads, need to lock
    m_mutex.lock();
    std::unique_ptr<SupportNode> node = std::make_unique<SupportNode>(position, distance_to_top, obj_layer_nr, support_roof_layers_below, to_buildplate, parent, print_z_, height_, dist_mm_to_top_, radius_);
    SupportNode* raw_ptr = node.get();
    contact_nodes.emplace_back(std::move(node));
    m_mutex.unlock();
    if (parent)
        raw_ptr->movement = position - parent->position;
    return raw_ptr;
}

void TreeSupportData::clear_nodes()
{
    tbb::spin_mutex::scoped_lock guard(m_mutex);
    contact_nodes.clear();
}

coordf_t TreeSupportData::ceil_radius(coordf_t radius) const
{
    size_t factor = (size_t)(radius / m_radius_sample_resolution);
    coordf_t remains = radius - m_radius_sample_resolution * factor;
    if (remains > EPSILON) {
        return radius + m_radius_sample_resolution - remains;
    }
    else {
        return radius;
    }
}

const ExPolygons& TreeSupportData::calculate_collision(const RadiusLayerPair& key) const
{
    assert(key.layer_nr < m_layer_outlines.size());

    ExPolygons collision_areas = std::move(offset_ex(m_layer_outlines[key.layer_nr], scale_(key.radius+m_xy_distance)));
    collision_areas = expolygons_simplify(collision_areas, scale_(m_radius_sample_resolution));
    // collision_areas.emplace_back(m_machine_border);
    const auto ret = m_collision_cache.insert({ key, std::move(collision_areas) });
    return ret.first->second;
}

const ExPolygons& TreeSupportData::calculate_avoidance(const RadiusLayerPair& key) const
{
    const auto &radius = key.radius;
    const auto &layer_nr = key.layer_nr;
    ExPolygons avoidance_areas;
    if (layer_nr > 0) {
        // Avoidance for a given layer depends on all layers beneath it so could have very deep recursion depths if
        // called at high layer heights. We can limit the reqursion depth to N by checking if the layer N
        // below the current one exists and if not, forcing the calculation of that layer. This may cause another recursion
        // if the layer at 2N below the current one but we won't exceed our limit unless there are N*N uncalculated layers
        // below our current one.
        constexpr auto max_recursion_depth = 100;
        // Check if we would exceed the recursion limit by trying to process this layer
        if (layer_nr >= max_recursion_depth && m_avoidance_cache.find({radius, layer_nr - max_recursion_depth}) == m_avoidance_cache.end()) {
            // Force the calculation of the layer `max_recursion_depth` below our current one, ignoring the result.
            get_avoidance(radius, layer_nr - max_recursion_depth);
        }

        avoidance_areas = std::move(offset_ex(get_avoidance(radius, layer_nr - 1), scale_(-m_max_move_distances[layer_nr-1])));
    }
    const ExPolygons &collision       = get_collision(radius, layer_nr);
    avoidance_areas.insert(avoidance_areas.end(), collision.begin(), collision.end());
    avoidance_areas = std::move(union_ex(avoidance_areas));
    auto ret = m_avoidance_cache.insert({key, std::move(avoidance_areas)});
    //assert(ret.second);
    // BOOST_LOG_TRIVIAL(debug) << format("calculate_avoidance: radius=%.2f, layer_nr=%d, recursions=%d, avoidance_areas=%d", radius, layer_nr, key.recursions,
    //                                    avoidance_areas.size());

    return ret.first->second;
}

} //namespace Slic3r
