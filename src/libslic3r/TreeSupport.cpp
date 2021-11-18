#include <math.h>

#include "MinimumSpanningTree.hpp"
#include "TreeSupport.hpp"
#include "Print.hpp"
#include "Layer.hpp"
#include "Fill/FillBase.hpp"
#include "CurveAnalyzer.hpp"
#include "SVG.hpp"

#define SQUARE_SUPPORT 0
#if SQUARE_SUPPORT
#define CIRCLE_RESOLUTION 4 // 100 //The number of vertices in each circle.
#else
#define CIRCLE_RESOLUTION 100 //The number of vertices in each circle.
#endif
#define MAX_BRANCH_RADIUS 10.0

#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif
#ifndef SIGN
#define SIGN(x) (x>=0?1:-1)
#endif
#ifndef SQ
#define SQ(x) ((x)*(x))
#endif
#define TAU (2.0 * M_PI)
#define NO_INDEX (std::numeric_limits<unsigned int>::max())

//#define SUPPORT_TREE_DEBUG_TO_SVG

namespace Slic3r
{
#define unscale_(val) ((val) * SCALING_FACTOR)
#define FIRST_LAYER_EXPANSION 1.2

inline unsigned int round_divide(unsigned int dividend, unsigned int divisor) //!< Return dividend divided by divisor rounded to the nearest integer
{
    return (dividend + divisor / 2) / divisor;
}
inline unsigned int round_up_divide(unsigned int dividend, unsigned int divisor) //!< Return dividend divided by divisor rounded to the nearest integer
{
    return (dividend + divisor - 1) / divisor;
}

inline double dot_with_unscale(const Point a, const Point b)
{
    return unscale_(a(0)) * unscale_(b(0)) + unscale_(a(1)) * unscale_(b(1));
}

inline double vsize2_with_unscale(const Point pt)
{
    return dot_with_unscale(pt, pt);
}

inline Point turn90_ccw(const Point pt)
{
    Point ret;

    ret(0) = -pt(1);
    ret(1) = pt(0);
    return ret;
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
    NUM_STAGES
};

class TreeSupportProfiler
{
public:
    uint32_t stage_durations[NUM_STAGES];
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
    void toc() { toc_time = boost::posix_time::microsec_clock::local_time(); }
    void stage_add(TreeSupportStage stage, bool do_toc = false)
    {
        if (stage > NUM_STAGES)
            return;
        if(do_toc)
            toc_time = boost::posix_time::microsec_clock::local_time();
        stage_durations[stage] += (toc_time - tic_time).total_milliseconds();
    }
    
    std::string report()
    {
        std::stringstream ss;
        ss << "STAGE_DETECT_OVERHANGS: " << stage_durations[STAGE_DETECT_OVERHANGS] << "; STAGE_GENERATE_CONTACT_NODES: " << stage_durations[STAGE_GENERATE_CONTACT_NODES]
            << "; STAGE_DROP_DOWN_NODES: " << stage_durations[STAGE_DROP_DOWN_NODES] << "; STAGE_DRAW_CIRCLES: " << stage_durations[STAGE_DRAW_CIRCLES]
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
static  std::string get_svg_filename(int layer_nr, std::string tag = "bbl_ts")
{
    static bool rand_init = false;

    if (!rand_init) {
        srand(time(NULL));
        rand_init = true;
    }

    int rand_num = rand() % 1000000;
    //makedir("./SVG");
    std::string prefix = "./SVG/";
    std::string suffix = ".svg";
    return prefix + tag + "_" + std::to_string(layer_nr) /*+ "_" + std::to_string(rand_num)*/ + suffix;
}

static void draw_avoidance_and_nodes_to_svg
(
    int layer_nr,
    const ExPolygons &overhangs,
    const ExPolygons &overhangs_after_offset,
    const ExPolygons &outlines_below,
    const std::vector<TreeSupport::Node*> &layer_nodes,
    const std::vector<TreeSupport::Node*> &lower_layer_nodes,
    std::string name_prefix,
    std::vector<std::string> legends = { "overhang","avoid","outlines" }, std::vector<std::string> colors = { "blue","red","yellow" }
)
{
    BoundingBox bbox = get_extents(overhangs_after_offset);
    bbox.merge(get_extents(outlines_below));
    Points layer_pts;
    for (TreeSupport::Node* node : layer_nodes) {
        layer_pts.push_back(node->position);
    }
    bbox.merge(get_extents(layer_pts));
    bbox.inflated(scale_(1));
    bbox.max.x() = std::max(bbox.max.x(), (coord_t)scale_(10));
    bbox.max.y() = std::max(bbox.max.y(), (coord_t)scale_(10));

    SVG svg(get_svg_filename(layer_nr, name_prefix), bbox);
    if (!svg.is_opened())        return;

    // draw grid
    const coordf_t step = scale_(1.0);
    Point bbox_size = bbox.size();
    if (bbox_size(0) < step || bbox_size(1) < step)
        return;

    Point start_pt(bbox.min(0), bbox.min(1));
    Point end_pt(bbox.max(1), bbox.min(1));
    for (coordf_t y = bbox.min(1); y <= bbox.max(1); y += step) {
        start_pt(1) = y;
        end_pt(1) = y;
        svg.draw(Line(start_pt, end_pt), "gray", coord_t(scale_(0.05)));
    }

    start_pt(1) = bbox.min(1);
    end_pt(1) = bbox.max(1);
    for (coordf_t x = bbox.min(0); x <= bbox.max(0); x += step) {
        start_pt(0) = x;
        end_pt(0) = x;
        svg.draw(Line(start_pt, end_pt), "gray", coord_t(scale_(0.05)));
    }

    // draw overhang areas
    svg.draw(union_ex(overhangs), colors[0]);
    svg.draw_outline(union_ex(overhangs_after_offset), colors[1]);
    svg.draw_outline(outlines_below, colors[2]);

    // draw legend
    svg.draw_text(bbox.min + Point(scale_(0), scale_(0)), ("nPoints: "+std::to_string(layer_nodes.size())+"->").c_str(), "green", 4);
    svg.draw_text(bbox.min + Point(scale_(15), scale_(0)), std::to_string(lower_layer_nodes.size()).c_str(), "black", 4);
    svg.draw_text(bbox.min + Point(scale_(0), scale_(1)), legends[0].c_str(), colors[0].c_str(), 4);
    svg.draw_text(bbox.min + Point(scale_(0), scale_(2)), legends[1].c_str(), colors[1].c_str(), 4);
    svg.draw_text(bbox.min + Point(scale_(0), scale_(3)), legends[2].c_str(), colors[2].c_str(), 4);

    // draw layer nodes    
    svg.draw(layer_pts, "green", coord_t(scale_(0.1)));

    // lower layer points
    layer_pts.clear();
    for (TreeSupport::Node *node : lower_layer_nodes) {
        layer_pts.push_back(node->position);
    }
    svg.draw(layer_pts, "black", coord_t(scale_(0.1)));

    // higher layer points
    layer_pts.clear();
    for (TreeSupport::Node* node : layer_nodes) {
        if(node->parent)
            layer_pts.push_back(node->parent->position);
    }
    svg.draw(layer_pts, "blue", coord_t(scale_(0.1)));
}

static void draw_layer_mst
(
    int layer_nr,
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

    SVG svg(get_svg_filename(layer_nr, "mstree").c_str(), bbox);
    if (!svg.is_opened())        return;
    
    svg.draw(lines, "blue", coord_t(scale_(0.05)));
    svg.draw_outline(outline, "yellow");
}
#endif

static unsigned int move_inside_expoly(const ExPolygon &polygon, Point& from, double distance = 0, double max_move_distance = std::numeric_limits<double>::max())
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
        return 0;
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
    }
    else if (bestDist2 < max_move_distance * max_move_distance)
    {
        from = ret;
    }
    return 0;
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

Point projection_onto_ex(const ExPolygons& polygons, Point from)
{
    profiler.tic();
    Point projected_pt;
    double min_dist = std::numeric_limits<double>::max();
#if 0
    for (auto poly : polygons) {
        for (int i = 0; i < poly.num_contours(); i++) {
            Point p = from.projection_onto(poly.contour_or_hole(i));
            double dist = (from - p).cast<double>().squaredNorm();
            if (dist < min_dist) {
                projected_pt = p;
                min_dist = dist;
            }
        }
    }
#else
    // simplified method: first find the nearest vertex, then project onto the 2 lines of the vertex
    Point nearest = find_closest_ex(from, polygons);
    for (auto poly : polygons) {
        for (int i = 0; i < poly.num_contours(); i++) {
            auto& points = poly.contour_or_hole(i).points;
            int nPoints = points.size();
            for (int i = 0; i < nPoints;i++) {
                if (points[i] == nearest) {
                    Point p = from.projection_onto(Line(nearest, points[(i - 1 + nPoints) % nPoints]));
                    double dist = (from - p).cast<double>().squaredNorm();
                    if (dist < min_dist) {
                        projected_pt = p;
                        min_dist = dist;
                    }
                    p = from.projection_onto(Line(nearest, points[(i + 1 + nPoints) % nPoints]));
                    dist = (from - p).cast<double>().squaredNorm();
                    if (dist < min_dist) {
                        projected_pt = p;
                        min_dist = dist;
                    }
                }
            }            
        }
    }
#endif
    profiler.stage_add(STAGE_projection_onto_ex, true);
    return projected_pt;
}

static bool move_out_expolys(const ExPolygons& polygons, Point& from, double distance, double max_move_distance)
{
    Point from0 = from;
    ExPolygons polys_dilated = union_ex(offset_ex(polygons, scale_(distance)));
    Point pt = projection_onto_ex(polys_dilated, from);// find_closest_ex(from, polys_dilated);
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
    : m_object(object), m_slicing_params(slicing_params)
{
    m_raft_layers = slicing_params.base_raft_layers + slicing_params.interface_raft_layers;
}

#define SUPPORT_SURFACES_OFFSET_PARAMETERS ClipperLib::jtSquare, 0.
static void remove_bridges_from_contacts(
    const Layer* lower_layer,
    const Layer* current_layer,
    float extrusion_width,
    ExPolygons& overhang_regions)
{
    // Extrusion width accounts for the roundings of the extrudates.
    // It is the maximum widh of the extrudate.
    float fw = extrusion_width;
    Lines overhang_perimeters = to_lines(overhang_regions);
    auto layer_regions = current_layer->regions();
    Polygons lower_layer_polygons = to_polygons(lower_layer->lslices);

    for (LayerRegion* layerm : layer_regions)
    {
        Polygons bridges;
        // Surface supporting this layer, expanded by 0.5 * nozzle_diameter, as we consider this kind of overhang to be sufficiently supported.
        Polygons lower_grown_slices = offset(lower_layer_polygons,
            //FIXME to mimic the decision in the perimeter generator, we should use half the external perimeter width.
            0.5f * fw, SUPPORT_SURFACES_OFFSET_PARAMETERS);
        Polylines overhang_perimeters = diff_pl(layerm->perimeters.as_polylines(), lower_grown_slices);
        // only consider straight overhangs
            // only consider overhangs having endpoints inside layer's slices
            // convert bridging polylines into polygons by inflating them with their thickness
            // since we're dealing with bridges, we can't assume width is larger than spacing,
            // so we take the largest value and also apply safety offset to be ensure no gaps
            // are left in between
        Flow bridge_flow = layerm->flow(frPerimeter, true);
        float w = float(std::max(bridge_flow.scaled_width(), bridge_flow.scaled_spacing()));
        for (Polyline& polyline : overhang_perimeters)
            if (polyline.is_straight()) {
                // This is a bridge 
                polyline.extend_start(fw);
                polyline.extend_end(fw);
                // Is the straight perimeter segment supported at both sides?
                Point pts[2] = { polyline.first_point(), polyline.last_point() };
                bool  supported[2] = { false, false };
                for (size_t i = 0; i < lower_layer->lslices.size() && !(supported[0] && supported[1]); ++i)
                    for (int j = 0; j < 2; ++j)
                        if (!supported[j] && lower_layer->lslices_bboxes[i].contains(pts[j]) && lower_layer->lslices[i].contains(pts[j]))
                            supported[j] = true;
                if (supported[0] && supported[1])
                    // Offset a polyline into a thick line.
                    polygons_append(bridges, offset(polyline, 0.5f * w + 10.f));
            }
        bridges = union_(bridges);

        // remove the entire bridges and only support the unsupported edges
        //FIXME the brided regions are already collected as layerm->bridged. Use it?
        for (const Surface& surface : layerm->fill_surfaces.surfaces)
            if (surface.surface_type == stBottomBridge && surface.bridge_angle != -1)
                polygons_append(bridges, surface.expolygon);
        //FIXME add the gap filled areas. Extrude the gaps with a bridge flow?
        // Remove the unsupported ends of the bridges from the bridged areas.
        //FIXME add supports at regular intervals to support long bridges!
#define SUPPORT_MATERIAL_MARGIN 1.5	
        bridges = diff(bridges,
            // Offset unsupported edges into polygons.
            offset(layerm->unsupported_bridge_edges, scale_(SUPPORT_MATERIAL_MARGIN), SUPPORT_SURFACES_OFFSET_PARAMETERS));

        overhang_regions = diff_ex(overhang_regions, to_expolygons(bridges));
    }
}

void TreeSupport::detect_object_overhangs()
{
    const PrintObjectConfig& config = m_object.config();
    const coordf_t radius_sample_resolution = m_ts_data->m_radius_sample_resolution;
    const coordf_t extrusion_width = config.extrusion_width.value;
    const coordf_t extrusion_width_scaled = scale_(extrusion_width);
    const bool dont_support_bridges = config.dont_support_bridges.value;
    const bool support_sharp_tails = config.support_sharp_tails.value;
    const int support_material_enforce_layers = config.support_material_enforce_layers.value;
    const double thresh_well_supported = SQ(scale_(4));  // min: 4x4=16mm^2
    double obj_height = m_object.size().z();

    if (config.support_type.value == stTreeAuto) {
        double threshold_rad = (config.support_material_threshold.value < EPSILON ? 30 : config.support_material_threshold.value) * M_PI / 180.;
        ExPolygons regions_well_supported; // regions on buildplate or well supported

        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, m_object.layer_count(), m_object.layer_count()),
            [&](const tbb::blocked_range<size_t>& range)
            {
                for (size_t layer_nr = range.begin(); layer_nr < range.end(); layer_nr++)
                {
                    Layer* layer = m_object.get_layer(layer_nr);
                    if (layer->lower_layer == nullptr) {
                        for (auto& slice : layer->lslices) {
                            if (slice.area() > thresh_well_supported)
                                regions_well_supported.emplace_back(slice);
                        }
                        continue;
                    }

                    Layer* lower_layer = layer->lower_layer;
                    coordf_t lower_layer_offset = layer_nr < support_material_enforce_layers ? -0.15 * extrusion_width_scaled : (float)lower_layer->height / tan(threshold_rad);
                    coordf_t support_offset_scaled = scale_(lower_layer_offset);
                    // Filter out areas whose diameter that is smaller than extrusion_width. Do not use offset2() for this purpose!
                    ExPolygons lower_polys;
                    for (const ExPolygon& expoly : lower_layer->lslices) {
                        if (!offset_ex(expoly, -extrusion_width_scaled / 2).empty()) {
                            lower_polys.emplace_back(expoly);
                        }
                    }

                    ExPolygons lower_layer_offseted;
                    if (support_sharp_tails)
                    {
                        // detect sharp tail and add more supports around
                        for (auto lower_region : lower_polys) {
                            ExPolygons lower_region_off;
                            ExPolygons lower_region_tmp;
                            lower_region_tmp.push_back(lower_region);
                            auto radius = get_extents(lower_region).radius();
                            if ( area(intersection_ex(lower_region_tmp, regions_well_supported)) < thresh_well_supported
                                && (obj_height-scale_(layer->slice_z))>get_extents(lower_region).radius()*5) {
                                lower_region_off = { lower_region };// offset_ex(lower_region, -0.1 * extrusion_width_scaled);
                            }
                            else {
                                lower_region_off = offset_ex(lower_region, support_offset_scaled);
                            }
                            if (!lower_region_off.empty())
                                lower_layer_offseted.push_back(lower_region_off.front());
                        }
                    }
                    else {
                        lower_layer_offseted = offset_ex(lower_polys, support_offset_scaled);
                    }

                    ExPolygons overhang_areas = std::move(diff_ex(layer->lslices, lower_layer_offseted));
                    overhang_areas = std::move(offset2_ex(overhang_areas, -0.1 * extrusion_width_scaled, 0.1 * extrusion_width_scaled));
                    if (dont_support_bridges && overhang_areas.size()>0) {
                        remove_bridges_from_contacts(lower_layer, layer, extrusion_width_scaled, overhang_areas);
                    }

                    TreeSupportLayer* ts_layer = m_object.get_tree_support_layer(layer_nr + m_raft_layers);
                    for (ExPolygon& poly : overhang_areas) {
                        ExPolygons poly_simp = poly.simplify(scale_(radius_sample_resolution));
                        // simplify method may delete the entire polygon which is unwanted
                        if(poly_simp.empty())
                            ts_layer->overhang_areas.emplace_back(poly);
                        else
                            append(ts_layer->overhang_areas, poly_simp);
                    }

                    if (support_sharp_tails)
                    {  // update well supported regions
                        ExPolygons regions_well_supported2;
                        // regions intersects with lower regions_well_supported or large support are also well supported
                        auto inters = intersection_ex(layer->lslices, regions_well_supported);
                        auto inters2 = intersection_ex(layer->lslices, ts_layer->overhang_areas);
                        inters.insert(inters.end(), inters2.begin(), inters2.end());
                        for (auto inter : inters) {
                            if (inter.area() >= thresh_well_supported)
                            {
                                //inter = offset_ex(inter, support_offset_scaled)[0];
                                regions_well_supported2.emplace_back(inter);
                            }
                        }                        
                        regions_well_supported = union_ex(regions_well_supported2);
                    }
                }
            }
        );
    }

    auto enforcers = m_object.slice_support_enforcers();
    auto blockers  = m_object.slice_support_blockers();
    m_object.project_and_append_custom_facets(false, EnforcerBlockerType::ENFORCER, enforcers);
    m_object.project_and_append_custom_facets(false, EnforcerBlockerType::BLOCKER, blockers);
    // small overhang removal
    std::vector<ExPolygons> overhangs_dilated(m_object.layer_count());
    for (int layer_nr = 0; layer_nr < m_object.layer_count(); layer_nr++) {
        overhangs_dilated[layer_nr] = offset_ex(m_object.get_tree_support_layer(layer_nr + m_raft_layers)->overhang_areas, extrusion_width_scaled);
    }
    for (int layer_nr = 0; layer_nr < m_object.layer_count(); layer_nr++) {
        TreeSupportLayer* ts_layer = m_object.get_tree_support_layer(layer_nr + m_raft_layers);

        if (1)
        {   // small overhang removal
            if (layer_nr<1 || layer_nr>overhangs_dilated.size() - 2) continue;
            ExPolygons small_overhangs;
            for (auto& overhang : ts_layer->overhang_areas)
            {
                //auto overhang_dilated = offset_ex({ overhang }, extrusion_width_scaled);
                if (intersection_ex(ExPolygons{overhang}, overhangs_dilated[layer_nr-1]).empty() && intersection_ex(ExPolygons{overhang}, overhangs_dilated[layer_nr+1]).empty()
                    && offset_ex({ overhang }, -extrusion_width_scaled * 2).empty())
                    small_overhangs.push_back(overhang);
            }
            ts_layer->overhang_areas = diff_ex(ts_layer->overhang_areas, small_overhangs);
        }

        if (layer_nr < enforcers.size()) {
            Polygons& enforcer = enforcers[layer_nr];
            // coconut: enforcer can't do offset2_ex, otherwise faces with angle near 90 degrees can't have enforcers, which
            // is not good. For example: tails of animals needs extra support except the lowest tip.
            //enforcer = std::move(offset2_ex(enforcer, -0.1 * extrusion_width_scaled, 0.1 * extrusion_width_scaled));
            ts_layer->overhang_areas.insert(ts_layer->overhang_areas.end(), enforcer.begin(), enforcer.end());
        }

        if (layer_nr < blockers.size()) {
            Polygons& blocker = blockers[layer_nr];
            ts_layer->overhang_areas = diff_ex(ts_layer->overhang_areas, offset_ex(blocker, scale_(radius_sample_resolution)));
        }
    }

#ifdef SUPPORT_TREE_DEBUG_TO_SVG
    for (const TreeSupportLayer* layer : m_object.tree_support_layers()) {
        if (layer->overhang_areas.empty())
            continue;

        SVG svg(get_svg_filename(layer->id(), "overhang_areas"), m_object.bounding_box());
        if (svg.is_opened()) {
            svg.draw_outline(m_object.get_layer(layer->id())->lslices, "yellow");
            svg.draw(layer->overhang_areas, "red");
            for (auto& overhang : layer->overhang_areas) {
                double aarea = overhang.area()/ thresh_well_supported;
                auto pt = get_extents(overhang).center();
                char x[20]; sprintf(x, "%.2f", aarea);
                svg.draw_text(pt, x, "red");
            }
        }
    }
#endif
}

void TreeSupport::create_tree_support_layers()
{
    int layer_id = 0;
    coordf_t raft_print_z = 0.f;
    coordf_t raft_slice_z = 0.f;
    for (; layer_id < m_slicing_params.base_raft_layers; layer_id++) {
        raft_print_z += m_slicing_params.base_raft_layer_height;
        raft_slice_z = raft_print_z - m_slicing_params.base_raft_layer_height / 2;
         m_object.add_tree_support_layer(layer_id, m_slicing_params.base_raft_layer_height, raft_print_z, raft_slice_z);
    }

    for (; layer_id < m_slicing_params.base_raft_layers + m_slicing_params.interface_raft_layers; layer_id++) {
        raft_print_z += m_slicing_params.interface_raft_layer_height;
        raft_slice_z = raft_print_z - m_slicing_params.interface_raft_layer_height / 2;
         m_object.add_tree_support_layer(layer_id, m_slicing_params.base_raft_layer_height, raft_print_z, raft_slice_z);
    }

    for (Layer *layer : m_object.layers()) {
        TreeSupportLayer* ts_layer = m_object.add_tree_support_layer(layer->id(), layer->height, layer->print_z, layer->slice_z);
        if (ts_layer->id() > m_raft_layers) {
            TreeSupportLayer* lower_layer = m_object.get_tree_support_layer(ts_layer->id() - 1);
            lower_layer->upper_layer = ts_layer;
            ts_layer->lower_layer = lower_layer;
        }
    }
}

static inline std::vector<BoundingBox> fill_expolygon_generate_paths(
    ExtrusionEntitiesPtr    &dst,
    ExPolygon              &&expolygon,
    Fill                    *filler,
    const FillParams        &fill_params,
    float                    density,
    ExtrusionRole            role,
    const Flow              &flow)
{
    Surface surface(stInternal, std::move(expolygon));
    Polylines polylines;
    try {
        polylines = filler->fill_surface(&surface, fill_params);
    } catch (InfillFailedException &) {
    }

    std::vector<BoundingBox> fill_bboxes;
    for (auto& polyline : polylines)
    {
        fill_bboxes.push_back(polyline.bounding_box());
    }

    extrusion_entities_append_paths(dst, std::move(polylines), role, flow.mm3_per_mm(), flow.width(), flow.height());

    return fill_bboxes;
}

static inline std::vector<BoundingBox> fill_expolygons_generate_paths(
    ExtrusionEntitiesPtr   &dst,
    ExPolygons            &&expolygons,
    Fill                   *filler,
    const FillParams       &fill_params,
    float                   density,
    ExtrusionRole           role,
    const Flow             &flow)
{
    BoundingBox bbox_object(Point(-scale_(1.), -scale_(1.0)), Point(scale_(1.), scale_(1.)));
    filler->set_bounding_box(bbox_object);
    std::vector<BoundingBox> fill_boxes;
    for (ExPolygon& expoly : expolygons) {
        auto boxes = fill_expolygon_generate_paths(dst, std::move(expoly), filler, fill_params, density, role, flow);
        append(fill_boxes, boxes);
    }
    return fill_boxes;
}

static void make_perimeter_and_inner_brim(ExtrusionEntitiesPtr &dst, const Print &print, const ExPolygon &support_area, size_t wall_count, const Flow &flow, bool is_interface)
{
    Polygons   loops;
    ExPolygons support_area_new = offset_ex(support_area, -0.5f * float(flow.scaled_spacing()), jtSquare);

    std::map<ExPolygon *, int> depth_per_expoly;
    std::list<ExPolygon> expoly_list;

    for (ExPolygon &expoly : support_area_new) {
        expoly_list.emplace_back(std::move(expoly));
        depth_per_expoly.insert({&expoly_list.back(), 0});
    }

    while (!expoly_list.empty()) {
        polygons_append(loops, to_polygons(expoly_list.front()));

        auto first_iter = expoly_list.begin();
        auto depth_iter = depth_per_expoly.find(&expoly_list.front());
        if (depth_iter->second + 1 < wall_count) {
            ExPolygons expolys_new = offset_ex(expoly_list.front(), -float(flow.scaled_spacing()), jtSquare);

            for (ExPolygon &expoly : expolys_new) {
                auto new_iter = expoly_list.insert(expoly_list.begin(), expoly);
                depth_per_expoly.insert({ &*new_iter, depth_iter->second + 1 });
            }
        }
        depth_per_expoly.erase(depth_iter);
        expoly_list.erase(first_iter);
    }

    ExtrusionRole role = is_interface ? erSupportMaterialInterface : erSupportMaterial;
    if (print.config().auto_slow_down_for_overhang_and_curva) {
        CurveAnalyzer curve_analyzer;
        for (size_t i = 0; i < loops.size(); i++) {
            // BBS: check polygon valid
            if (!loops[i].is_valid())
                continue;
            // BBS: calculate curvatures for the loop of polygon and generate ExtrusionPaths
            // by order which has different curve degree.
            ExtrusionPaths paths;
            ExtrusionPath path(0, 0, role, flow.mm3_per_mm(), flow.width(), flow.height());
            path.polyline = loops[i].split_at_first_point();
            paths.emplace_back(std::move(path));
            // BBS: use absolute mode for tree support because we don't care about the surface quality of support
            curve_analyzer.calculate_curvatures(paths, ECurveAnalyseMode::AbsoluteMode);
            // BBS: save result
            dst.reserve(dst.size() + 1);
            dst.emplace_back(new ExtrusionLoop(std::move(paths)));
            paths.clear();
        }
    } else {
        extrusion_entities_append_loops(dst, std::move(loops), role,
            float(flow.mm3_per_mm()), float(flow.width()), float(flow.height()));
    }
}

static void make_perimeter_and_infill(ExtrusionEntitiesPtr& dst, const Print& print, const ExPolygon& support_area, size_t wall_count, const Flow& flow, bool is_interface, Fill* filler_support, double support_density)
{
    Polygons   loops;
    ExPolygons support_area_new = offset_ex(support_area, -0.5f * float(flow.scaled_spacing()), jtSquare);

    // draw infill (remember to adjust to align infill between layers)
    FillParams fill_params;
    fill_params.density = support_density;
    fill_params.dont_adjust = true;
    ExPolygons to_infill = offset_ex(support_area, -0.5f * float(flow.scaled_spacing()), jtSquare);// offset2_ex(support_area, float(SCALED_EPSILON), float(-SCALED_EPSILON - 0.5 * flow.scaled_spacing()));
    std::vector<BoundingBox> fill_boxes = fill_expolygons_generate_paths(dst, std::move(to_infill), filler_support, fill_params, support_density, erSupportMaterial, flow);

    // allow wall_count to be zero, which means only draw infill
    if (wall_count == 0) {
        for (auto fill_bbox : fill_boxes)
        {
            if (filler_support->angle == 0) {
                fill_bbox.min[0] -= scale_(10);
                fill_bbox.max[0] += scale_(10);
            }
            else {
                fill_bbox.min[1] -= scale_(10);
                fill_bbox.max[1] += scale_(10);
            }
            support_area_new = diff_ex(support_area_new, to_expolygons({ fill_bbox.polygon() }));
        }
        // filter out small areas
        for (auto it = support_area_new.begin(); it != support_area_new.end(); ) {
            if (offset_ex(*it, -flow.scaled_width()).empty())
                it = support_area_new.erase(it);
            else
                it++;
        }
    }

    {
        std::map<ExPolygon*, int> depth_per_expoly;
        std::list<ExPolygon> expoly_list;

        for (ExPolygon& expoly : support_area_new) {
            expoly_list.emplace_back(std::move(expoly));
            depth_per_expoly.insert({ &expoly_list.back(), 0 });
        }

        while (!expoly_list.empty()) {
            polygons_append(loops, to_polygons(expoly_list.front()));

            auto first_iter = expoly_list.begin();
            auto depth_iter = depth_per_expoly.find(&expoly_list.front());
            if (depth_iter->second + 1 < wall_count) {
                ExPolygons expolys_new = offset_ex(expoly_list.front(), -float(flow.scaled_spacing()), jtSquare);

                for (ExPolygon& expoly : expolys_new) {
                    auto new_iter = expoly_list.insert(expoly_list.begin(), expoly);
                    depth_per_expoly.insert({ &*new_iter, depth_iter->second + 1 });
                }
            }
            depth_per_expoly.erase(depth_iter);
            expoly_list.erase(first_iter);
        }


        ExtrusionRole role = is_interface ? erSupportMaterialInterface : erSupportMaterial;
        if (print.config().auto_slow_down_for_overhang_and_curva) {
            CurveAnalyzer curve_analyzer;
            for (size_t i = 0; i < loops.size(); i++) {
                // BBS: check polygon valid
                if (!loops[i].is_valid())
                    continue;
                // BBS: calculate curvatures for the loop of polygon and generate ExtrusionPaths
                // by order which has different curve degree.
                ExtrusionPaths paths;
                ExtrusionPath path(0, 0, role, flow.mm3_per_mm(), flow.width(), flow.height());
                path.polyline = loops[i].split_at_first_point();
                paths.emplace_back(std::move(path));
                // BBS: use absolute mode for tree support because we don't care about the surface quality of support
                curve_analyzer.calculate_curvatures(paths, ECurveAnalyseMode::AbsoluteMode);
                // BBS: save result
                dst.reserve(dst.size() + 1);
                dst.emplace_back(new ExtrusionLoop(std::move(paths)));
                paths.clear();
            }
        }
        else {
            extrusion_entities_append_loops(dst, std::move(loops), role,
                float(flow.mm3_per_mm()), float(flow.width()), float(flow.height()));
        }
    }
}

void TreeSupport::generate_toolpaths()
{
    const PrintConfig &print_config = m_object.print()->config();
    const PrintObjectConfig &object_config = m_object.config();
    coordf_t support_extrusion_width = object_config.support_material_extrusion_width.value > 0 ? object_config.support_material_extrusion_width : object_config.extrusion_width;
    coordf_t nozzle_diameter = print_config.nozzle_diameter.get_at(object_config.support_material_extruder - 1);

    const size_t wall_count = object_config.tree_support_wall_count.value;
    const bool with_infill = object_config.tree_support_with_infill.value;
    auto m_support_material_flow = support_material_flow(&m_object, float(m_slicing_params.layer_height));

    // coconut: use same intensity settings as SupportMaterial.cpp
    auto m_support_material_interface_flow = support_material_interface_flow(&m_object, float(m_slicing_params.layer_height));
    coordf_t interface_spacing = object_config.support_material_interface_spacing.value + m_support_material_interface_flow.spacing();
    coordf_t interface_density = std::min(1., m_support_material_interface_flow.spacing() / interface_spacing);
    coordf_t support_spacing = object_config.support_material_spacing.value + m_support_material_flow.spacing();
    coordf_t support_density = std::min(1., m_support_material_flow.spacing() / support_spacing);

    if (m_object.tree_support_layers().empty())
        return;

    // calculate fill areas for raft layers
    ExPolygons raft_areas;
    if (m_object.layer_count() > 0) {
        const Layer *layer = m_object.layers().front();
        for (const ExPolygon &expoly : layer->lslices) {
            raft_areas.push_back(expoly);
        }
    }

    if (m_object.tree_support_layer_count() > m_raft_layers) {
        const TreeSupportLayer *ts_layer = m_object.get_tree_support_layer(m_raft_layers);
        for (const ExPolygon expoly : ts_layer->floor_areas)
            raft_areas.push_back(expoly);
        for (const ExPolygon expoly : ts_layer->roof_areas)
            raft_areas.push_back(expoly);
        for (const ExPolygon expoly : ts_layer->base_areas)
            raft_areas.push_back(expoly);
    }

    raft_areas = std::move(offset_ex(raft_areas, scale_(3.)));

    // generate raft tool path
    if (m_raft_layers > 0)
    {
        ExtrusionRole raft_contour_er = m_slicing_params.base_raft_layers > 0 ? erSupportMaterial : erSupportMaterialInterface;
        TreeSupportLayer *ts_layer = m_object.tree_support_layers().front();
        Flow flow = m_object.print()->brim_flow();

        Polygons loops;
        for (const ExPolygon& expoly : raft_areas) {
            loops.push_back(expoly.contour);
            loops.insert(loops.end(), expoly.holes.begin(), expoly.holes.end());
        }
        extrusion_entities_append_loops(ts_layer->support_fills.entities, std::move(loops), raft_contour_er,
            float(flow.mm3_per_mm()), float(flow.width()), float(flow.height()));
        raft_areas = offset_ex(raft_areas, -flow.scaled_spacing() / 2.);
    }

    for (size_t layer_nr = 0; layer_nr < m_slicing_params.base_raft_layers; layer_nr++) {
        TreeSupportLayer *ts_layer = m_object.get_tree_support_layer(layer_nr);
        coordf_t expand_offset = (layer_nr == 0 ? 0. : -1.);

        Flow support_flow = layer_nr == 0 ? m_object.print()->brim_flow() : Flow(support_extrusion_width, ts_layer->height, nozzle_diameter);
        Fill* filler_interface = Fill::new_from_type(ipRectilinear);
        filler_interface->angle = layer_nr == 0 ? 90 : 0;
        filler_interface->spacing = support_extrusion_width;

        FillParams fill_params;
        fill_params.density = interface_density;
        fill_params.dont_adjust = true;

        fill_expolygons_generate_paths(ts_layer->support_fills.entities, std::move(offset_ex(raft_areas, scale_(expand_offset))),
            filler_interface, fill_params, interface_density, erSupportMaterial, support_flow);
    }

    for (size_t layer_nr = m_slicing_params.base_raft_layers;
         layer_nr < m_slicing_params.base_raft_layers + m_slicing_params.interface_raft_layers;
         layer_nr++)
    {
        TreeSupportLayer *ts_layer = m_object.get_tree_support_layer(layer_nr);
        coordf_t expand_offset = (layer_nr == 0 ? 0. : -1.);

        Flow support_flow(support_extrusion_width, ts_layer->height, nozzle_diameter);
        Fill* filler_interface = Fill::new_from_type(ipRectilinear);
        filler_interface->angle = 0;
        filler_interface->spacing = support_extrusion_width;

        FillParams fill_params;
        fill_params.density = interface_density;
        fill_params.dont_adjust = true;

        fill_expolygons_generate_paths(ts_layer->support_fills.entities, std::move(offset_ex(raft_areas, scale_(expand_offset))),
            filler_interface, fill_params, interface_density, erSupportMaterialInterface, support_flow);
    }

    auto obj_size = m_object.size();
    Fill* filler_support = Fill::new_from_type(ipRectilinear);
    filler_support->angle = obj_size.x() > obj_size.y() ? 0. : M_PI/2;  // roughly align fill direction to object direction
    filler_support->spacing = m_support_material_flow.spacing();//support_extrusion_width;
    // generate tree support tool paths
    tbb::parallel_for(
        tbb::blocked_range<size_t>(m_raft_layers, m_object.tree_support_layer_count()),
        [&](const tbb::blocked_range<size_t>& range)
        {
            for (size_t layer_id = range.begin(); layer_id < range.end(); layer_id++) {
                TreeSupportLayer* ts_layer = m_object.get_tree_support_layer(layer_id);
                Flow support_flow(support_extrusion_width, ts_layer->height, nozzle_diameter);
                Fill* filler_interface = Fill::new_from_type(ipRectilinear);

                filler_interface->angle = layer_id % 2 ? 0 : 90;
                filler_interface->spacing = interface_spacing;

                // bool stands for is_support_interface
                std::vector<std::pair<ExPolygons *, bool>> area_groups;
                area_groups.emplace_back(&ts_layer->roof_areas, true);
                area_groups.emplace_back(&ts_layer->floor_areas, true);
                area_groups.emplace_back(&ts_layer->base_areas, false);
                for (std::pair<ExPolygons*, bool>& area_group : area_groups) {
                    for (ExPolygon& poly : *area_group.first) {
                        if (area_group.second) {
                            ExPolygons polys;
                            if (layer_id == 0) {
                                Flow flow = m_raft_layers == 0 ? m_object.print()->brim_flow() : support_flow;
                                make_perimeter_and_inner_brim(ts_layer->support_fills.entities, *m_object.print(),
                                    poly, wall_count, flow, true);
                                polys = std::move(offset_ex(poly, -flow.scaled_spacing()));
                            } else {
                                polys.push_back(poly);
                            }

                            FillParams fill_params;
                            fill_params.density = interface_density;
                            fill_params.dont_adjust = true;
                            fill_expolygons_generate_paths(ts_layer->support_fills.entities, std::move(polys),
                                filler_interface, fill_params, interface_density, erSupportMaterialInterface, support_flow);
                        } else {
                            Flow flow = (layer_id == 0 && m_raft_layers == 0)? m_object.print()->brim_flow() : support_flow;
                            if(with_infill && layer_id > 0 && offset(poly, -scale_(support_spacing)).empty()==false)
                            {
                                // with infill we only need half the extrusion width
                                make_perimeter_and_infill(ts_layer->support_fills.entities, *m_object.print(), poly, wall_count, Flow(support_extrusion_width*0.65, ts_layer->height, nozzle_diameter), false, filler_support, support_density);
                            }
                            else
                            {
                                make_perimeter_and_inner_brim(ts_layer->support_fills.entities, *m_object.print(), poly,
                                    layer_id > 0 ? wall_count : std::numeric_limits<size_t>::max(), flow, false);
                            }
                        }
                    }
                }
            }
        }
    );
}

Polygons TreeSupport::spanning_tree_to_polygon(const std::vector<MinimumSpanningTree>& spanning_trees, Polygons layer_contours, int layer_nr)
{
    Polygons polys;
    auto& mst_line_x_layer_contour_cache = m_mst_line_x_layer_contour_caches[layer_nr];
    for (MinimumSpanningTree mst : spanning_trees) {
        std::vector<Point> points = mst.vertices();
        if (points.size() == 0)
            continue;
        std::map<Point, bool> visited;
        for (int i=0;i<points.size();i++)
            visited.emplace(points[i],false);

        std::unordered_set<Line, LineHash> to_ignore;
        for (int i = 0; i < points.size(); i++) {
            if (visited[points[i]] == true)
                continue;

            Polygon poly;
            bool has_next = true;
            Point pt1 = points[i];
            poly.points.push_back(pt1);
            visited[pt1] = true;

            while (has_next) {
                const std::vector<Point>& neighbours = mst.adjacent_nodes(pt1);
                if (neighbours.empty())
                {
                    break;
                }

                double min_ccw = std::numeric_limits<double>::max();
                Point pt_selected = neighbours[0];
                has_next = false;
                for (Point pt2 : neighbours) {
                    if (to_ignore.find(Line(pt1, pt2)) == to_ignore.end()) {
                        auto iter = mst_line_x_layer_contour_cache.find({ pt1,pt2 });
                        if (iter != mst_line_x_layer_contour_cache.end()) {
                            if (iter->second)
                                continue;
                        }
                        else {
                            Polylines pls;
                            pls.emplace_back(pt1, pt2);
                            Polylines pls_intersect = intersection_pl(pls, layer_contours);
                            mst_line_x_layer_contour_cache.insert({ {pt1, pt2}, !pls_intersect.empty() });
                            mst_line_x_layer_contour_cache.insert({ {pt2, pt1}, !pls_intersect.empty() });
                            if (!pls_intersect.empty())
                                continue;
                        }

                        if (poly.points.size() < 2 || visited[pt2]==false)
                        {
                            pt_selected = pt2;
                            has_next = true;
                            break;
                        }
                        double curr_ccw = pt2.ccw(pt1, poly.points.back());
                        if (curr_ccw < min_ccw)
                        {
                            min_ccw = curr_ccw;
                            pt_selected = pt2;
                            has_next = true;
                        }
                    }
                }
                if (has_next) {
                    poly.points.push_back(pt_selected);
                    to_ignore.insert(Line(pt1, pt_selected));
                    visited[pt_selected] = true;
                    pt1 = pt_selected;
                }
            }
            polys.emplace_back(std::move(poly));
        }
    }
    return polys;
}

Polygons TreeSupport::contact_nodes_to_polygon(const std::vector<Node*>& contact_nodes, Polygons layer_contours, int layer_nr, std::vector<double>& radiis, std::vector<bool>& is_interface)
{
    Polygons polys;
    std::vector<MinimumSpanningTree> spanning_trees;
    std::vector<double> radiis_mtree;
    std::vector<bool> is_interface_mtree;
    // generate minimum spanning trees
    {
        std::map<Node*, bool> visited;
        for (int i = 0; i < contact_nodes.size(); i++)
            visited.emplace(contact_nodes[i], false);
        std::unordered_set<Line, LineHash> to_ignore;

        // generate minimum spaning trees
        for (int i = 0; i < contact_nodes.size(); i++) {
            Node* node = contact_nodes[i];
            if (visited[node])
                continue;

            std::vector<Point> points_to_mstree;
            double radius = 0;
            Point pt1 = node->position;
            points_to_mstree.push_back(pt1);
            visited[node] = true;
            radius += node->radius;

            for (int j = i + 1; j < contact_nodes.size(); j++) {
                Node* node2 = contact_nodes[j];
                Point pt2 = node2->position;
                // connect to this neighbor if:
                // 1) both are interface or both are not
                // 3) not readly added
                // 4) won't cross perimeters: this is not right since we need to check all possible connections
                if ((node->support_roof_layers_below > 0) == (node2->support_roof_layers_below > 0)
                    && to_ignore.find(Line(pt1, pt2)) == to_ignore.end())
                {
                    points_to_mstree.emplace_back(pt2);
                    visited[node2] = true;
                    radius += node2->radius;
                }
            }

            spanning_trees.emplace_back(points_to_mstree);
            radiis_mtree.push_back(radius / points_to_mstree.size());
            is_interface_mtree.push_back(node->support_roof_layers_below > 0);
        }
    }
    auto lines = spanning_tree_to_lines(spanning_trees);
#if 1
    // convert mtree to polygon
    for (int k = 0; k < spanning_trees.size(); k++) {
        auto& mst_line_x_layer_contour_cache = m_mst_line_x_layer_contour_caches[layer_nr];
        MinimumSpanningTree mst = spanning_trees[k];
        std::vector<Point> points = mst.vertices();
        std::map<Point, bool> visited;
        for (int i = 0; i < points.size(); i++)
            visited.emplace(points[i], false);

        std::unordered_set<Line, LineHash> to_ignore;
        for (int i = 0; i < points.size(); i++) {
            if (visited[points[i]])
                continue;

            Polygon poly;
            Point pt1 = points[i];
            poly.points.push_back(pt1);
            visited[pt1] = true;

            bool has_next = true;
            while (has_next)
            {
                const std::vector<Point>& neighbours = mst.adjacent_nodes(pt1);
                double min_ccw = -std::numeric_limits<double>::max();
                Point pt_selected;
                has_next = false;
                for (Point pt2 : neighbours) {
                    if (to_ignore.find(Line(pt1, pt2)) == to_ignore.end()) {
                        auto iter = mst_line_x_layer_contour_cache.find({ pt1,pt2 });
                        if (iter != mst_line_x_layer_contour_cache.end()) {
                            if (iter->second)
                                continue;
                        }
                        else {
                            Polylines pls;
                            pls.emplace_back(pt1, pt2);
                            Polylines pls_intersect = intersection_pl(pls, layer_contours);
                            mst_line_x_layer_contour_cache.insert({ {pt1, pt2}, !pls_intersect.empty() });
                            mst_line_x_layer_contour_cache.insert({ {pt2, pt1}, !pls_intersect.empty() });
                            if (!pls_intersect.empty())
                                continue;
                        }
                        if (poly.points.size() < 2)
                        {
                            pt_selected = pt2;
                            has_next = true;
                            break;
                        }
                        double curr_ccw = pt2.ccw(pt1, poly.points.rbegin()[1]);
                        if (curr_ccw > min_ccw)
                        {
                            has_next = true;
                            min_ccw = curr_ccw;
                            pt_selected = pt2;
                        }
                    }
                }
                if (!has_next)
                    break;

                poly.points.push_back(pt_selected);
                to_ignore.insert(Line(pt1, pt_selected));
                visited[pt_selected] = true;
                pt1 = pt_selected;
            }
            polys.emplace_back(std::move(poly));
            radiis.push_back(radiis_mtree[k]);
            is_interface.push_back(is_interface_mtree[k]);
        }
    }
#else
    polys = spanning_tree_to_polygon(spanning_trees, layer_contours, layer_nr, radiis);
#endif
    return polys;
}


void TreeSupport::generate_support_areas()
{
    const PrintObjectConfig &config = m_object.config();
    bool tree_support_enable = config.support_material.value &&
        (config.support_type.value == stTreeAuto || config.support_type.value == stTree);
    if (!tree_support_enable)
        return;

    std::vector<std::vector<Node*>> contact_nodes(m_object.layers().size()); //Generate empty layers to store the points in.
    m_ts_data = m_object.alloc_tree_support_preview_cache();

    // Create Tree Support Layers
    create_tree_support_layers();

    // Generate overhang areas
    profiler.stage_start(STAGE_DETECT_OVERHANGS);
    m_object.print()->set_status(86, "Support: detect_object_overhangs");
    detect_object_overhangs();
    profiler.stage_finish(STAGE_DETECT_OVERHANGS);

    // Generate contact points of tree support
    profiler.stage_start(STAGE_GENERATE_CONTACT_NODES);
    m_object.print()->set_status(87, "Support: generate_contact_points");
    generate_contact_points(contact_nodes);
    profiler.stage_finish(STAGE_GENERATE_CONTACT_NODES);

    //Drop nodes to lower layers.
    profiler.stage_start(STAGE_DROP_DOWN_NODES);
    m_object.print()->set_status(90, "Support: drop_nodes");
    drop_nodes(contact_nodes);
    profiler.stage_finish(STAGE_DROP_DOWN_NODES);

    //Generate support areas.
    profiler.stage_start(STAGE_DRAW_CIRCLES);
    m_object.print()->set_status(92, "Support: draw_circles");
    draw_circles(contact_nodes);
    profiler.stage_finish(STAGE_DRAW_CIRCLES);

    for (auto& layer : contact_nodes)
    {
        for (Node* p_node : layer)
        {
            delete p_node;
        }
        layer.clear();
    }
    contact_nodes.clear();

    profiler.stage_start(STAGE_GENERATE_TOOLPATHS);
    m_object.print()->set_status(95, "Generate toolpath");
    generate_toolpaths();
    profiler.stage_finish(STAGE_GENERATE_TOOLPATHS);

    BOOST_LOG_TRIVIAL(debug) << "tree support time " << profiler.report();
}

inline coordf_t calc_branch_radius(coordf_t base_radius, size_t layers_to_top, size_t tip_layers, double diameter_angle_scale_factor)
{
    double radius;
    if ((layers_to_top + 1) > tip_layers)
    {
        radius = base_radius + base_radius * (layers_to_top + 1) * diameter_angle_scale_factor;
    }
    else
    {
        radius = base_radius * (layers_to_top + 1) / tip_layers;
    }
    radius = std::min(radius, MAX_BRANCH_RADIUS);
    return radius;
}

void TreeSupport::draw_circles(const std::vector<std::vector<Node*>>& contact_nodes)
{
    const PrintObjectConfig &config = m_object.config();
    const coordf_t branch_radius = config.tree_support_branch_diameter.value / 2;
    const coordf_t branch_radius_scaled = scale_(branch_radius);
    Polygon branch_circle; //Pre-generate a circle with correct diameter so that we don't have to recompute those (co)sines every time.
    for (unsigned int i = 0; i < CIRCLE_RESOLUTION; i++)
    {
#if SQUARE_SUPPORT
        double angle = (double)i / CIRCLE_RESOLUTION * TAU + TAU/8.0;
#else
        double angle = (double)i / CIRCLE_RESOLUTION * TAU;
#endif
        branch_circle.append(Point(cos(angle) * branch_radius_scaled, sin(angle) * branch_radius_scaled));
    }

    // Performance optimization. Only generate lslices for brim and skirt.
    size_t brim_skirt_layers = 0;
    if (config.brim_width.value > EPSILON)
    {
        brim_skirt_layers = 1;
    }

    const Print* print = m_object.print();
    const PrintConfig& print_config = print->config();
    for (const PrintObject* object : print->objects())
    {
        size_t skirt_layers = print->has_infinite_skirt() ? object->layer_count() : std::min(size_t(print_config.skirt_height.value), object->layer_count());
        brim_skirt_layers = std::max(brim_skirt_layers, skirt_layers);
    }

    // generate areas
    const coordf_t circle_side_length = 2 * branch_radius * sin(M_PI / CIRCLE_RESOLUTION); //Side length of a regular polygon.
    const coordf_t layer_height = config.layer_height.value;
    const size_t bottom_interface_layers = config.support_material_bottom_interface_layers.value;
    const size_t tip_layers = branch_radius / layer_height; //The number of layers to be shrinking the circle to create a tip. This produces a 45 degree angle.
    const double diameter_angle_scale_factor = sin(config.tree_support_branch_diameter_angle.value * M_PI / 180.) * layer_height / branch_radius; //Scale factor per layer to produce the desired angle.
    const coordf_t line_width = config.support_material_extrusion_width.get_abs_value(layer_height);

    // coconut: previously std::unordered_map in m_collision_cache is not multi-thread safe which may cause programs stuck, here we change to tbb::concurrent_unordered_map
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, m_object.layer_count()),
        [&](const tbb::blocked_range<size_t>& range)
        {
            for (size_t layer_nr = range.begin(); layer_nr < range.end(); layer_nr++)
            {
                TreeSupportLayer* ts_layer = m_object.get_tree_support_layer(layer_nr + m_raft_layers);
                assert(ts_layer != nullptr);

                ExPolygons& base_areas = ts_layer->base_areas;
                ExPolygons& roof_areas = ts_layer->roof_areas;
                ExPolygons& floor_areas = ts_layer->roof_areas;

                // skip if current layer has no points. This fixes potential crash in get_collision (see jira BBL001-355)
                if (contact_nodes[layer_nr].empty())
                    continue;

                //Draw the support areas and add the roofs appropriately to the support roof instead of normal areas.
                ts_layer->lslices.reserve(contact_nodes[layer_nr].size());
#if 1
                for (const Node* p_node : contact_nodes[layer_nr])
                {
                    const Node& node = *p_node;
                    ExPolygon area;
                    if (node.type == ePolygon) {
                        area = *node.overhang;
                    }
                    else {
                        Polygon circle;
                        size_t layers_to_top = node.distance_to_top;
                        double scale = static_cast<double>(layers_to_top + 1) / tip_layers;
                        scale = layers_to_top < tip_layers ? (0.5 + scale / 2) : (1 + static_cast<double>(layers_to_top - tip_layers) * diameter_angle_scale_factor);
                        scale = std::min(scale, MAX_BRANCH_RADIUS / branch_radius);
                        for (auto iter = branch_circle.points.begin(); iter != branch_circle.points.end(); iter++)
                        {
                            Point corner = (*iter) * scale;
                            circle.append(node.position + corner);
                        }

                        if (layer_nr == 0 && m_raft_layers == 0) {
                            double brim_width = layers_to_top * layer_height / (scale * branch_radius) * 0.5;
                            circle = offset(circle, scale_(brim_width))[0];
                        }
                        area = ExPolygon(circle);
                    }

                    if (node.support_roof_layers_below > 0)
                    {
                        roof_areas.emplace_back(area);
                    }
                    else if (node.support_floor_layers_above > 0)
                        floor_areas.emplace_back(area);
                    else
                    {
                        base_areas.emplace_back(area);
                    }

                    if (layer_nr < brim_skirt_layers)
                        ts_layer->lslices.emplace_back(area);
                }
#else
                // some nodes may not have radius set
                for (Node* p_node : contact_nodes[layer_nr])
                {
                    size_t layers_to_top = p_node->distance_to_top;// std::min(node.distance_to_top, (size_t)300);
                    double scale = static_cast<double>(layers_to_top + 1) / tip_layers;
                    scale = layers_to_top < tip_layers ? (0.5 + scale / 2) : (1 + static_cast<double>(layers_to_top - tip_layers) * diameter_angle_scale_factor);
                    p_node->radius = scale * branch_radius;
                }
                {
                    // now this method is extremely slow. Need to optimize the speed before we can use it.
                    Polygons layer_contours = std::move(m_ts_data->get_contours_with_holes(layer_nr));
                    std::vector<double> radiis;
                    std::vector<bool> is_interface;
                    //Polygons lines = spanning_tree_to_polygon(m_spanning_trees[layer_nr], layer_contours, layer_nr, radiis);
                    Polygons lines = contact_nodes_to_polygon(contact_nodes[layer_nr], layer_contours, layer_nr, radiis, is_interface);

                    for (int k = 0; k < lines.size(); k++) {
                        auto line = lines[k];
                        double radius = radiis[k];
                        Polygons line_expanded;
                        if (line.size() == 1)
                        {
                            Polygon circle;
                            double scale = radiis[k] / branch_radius;
                            for (auto iter = branch_circle.points.begin(); iter != branch_circle.points.end(); iter++)
                            {
                                Point corner = (*iter) * scale;
                                circle.append(line.first_point() + corner);
                            }
                            line_expanded.emplace_back(circle);
                        }
                        else {
                            line_expanded = offset(line, scale_(radius), jtRound, scale_(config.tree_support_collision_resolution));
                        }
                        if (line_expanded.empty())
                            continue;
                        if (is_interface[k])
                            roof_areas.emplace_back(line_expanded[0]);
                        else
                            base_areas.emplace_back(line_expanded[0]);
                        if (layer_nr < brim_skirt_layers)
                            ts_layer->lslices.emplace_back(line_expanded[0]);

                        //if (radius > config.support_material_spacing * 2)
                        //    ts_layer->need_infill = true;
                    }

#ifdef SUPPORT_TREE_DEBUG_TO_SVG
                    draw_avoidance_and_nodes_to_svg( layer_nr, base_areas, to_expolygons(lines), m_ts_data->m_layer_outlines_below[layer_nr], {}, {}, "circles", { "lines","base_areas","outlines" });
#endif
                }
#endif
                ts_layer->lslices = std::move(union_ex(ts_layer->lslices));

                //Must update bounding box which is used in avoid crossing perimeter
                ts_layer->lslices_bboxes.clear();
                ts_layer->lslices_bboxes.reserve(ts_layer->lslices.size());
                for (const ExPolygon &expoly : ts_layer->lslices)
                    ts_layer->lslices_bboxes.emplace_back(get_extents(expoly));
                ts_layer->backup_untyped_slices();

                m_object.print()->set_status(95, "Support: draw_circles at layer " + std::to_string(layer_nr));

                auto avoid_region = m_ts_data->get_collision(m_ts_data->m_xy_distance, layer_nr);
                auto avoid_region_interface = m_ts_data->get_collision(config.support_material_contact_distance, layer_nr);
                Polygons layer_contours = std::move(m_ts_data->get_contours_with_holes(layer_nr));
                base_areas = std::move(diff_ex(base_areas, avoid_region));
                roof_areas = std::move(diff_ex(roof_areas, avoid_region_interface));
                double contact_dist_scaled = scale_(config.support_material_contact_distance);
                roof_areas = std::move(offset2_ex(roof_areas, contact_dist_scaled, -contact_dist_scaled));
                base_areas = std::move(diff_ex(base_areas, roof_areas));

#if SQUARE_SUPPORT
                if (m_object.print()->config().enable_arc_fitting.value == false) {
                    // simplify support contours if arc fitting is disabled
                    ExPolygons base_areas_simplified;
                    for (auto& area : base_areas) {
                        area.simplify(scale_(line_width / 2), &base_areas_simplified, SimplifyMethodDP);
                    }
                    base_areas = std::move(base_areas_simplified);
                }
#endif
                //Subtract support floors.
                if (bottom_interface_layers > 0)
                {
                    if (layer_nr >= bottom_interface_layers)
                    {
                        const Layer* below_layer = m_object.get_layer(layer_nr - bottom_interface_layers);
                        ExPolygons bottom_interface = std::move(intersection_ex(base_areas, below_layer->lslices));
                        floor_areas.insert(floor_areas.end(), bottom_interface.begin(), bottom_interface.end());
                    }
                    if (floor_areas.empty() == false) {
                        floor_areas = std::move(diff_ex(floor_areas, avoid_region_interface));
                        floor_areas = std::move(offset2_ex(floor_areas, contact_dist_scaled, -contact_dist_scaled));
                        base_areas = std::move(diff_ex(base_areas, offset_ex(floor_areas, 10)));
                    }
                }
            }
        });
}

void TreeSupport::drop_nodes(std::vector<std::vector<Node*>>& contact_nodes)
{
    const PrintObjectConfig &config = m_object.config();
    //Use Minimum Spanning Tree to connect the points on each layer and move them while dropping them down.
    const coordf_t layer_height = config.layer_height.value;
    const double angle = config.tree_support_branch_angle.value * M_PI / 180.;
    const double tan_angle = tan(angle);
    const coordf_t max_move_distance = (angle < M_PI / 2) ? (coordf_t)(tan_angle * layer_height) : std::numeric_limits<coordf_t>::max();
    const double max_move_distance2 = max_move_distance * max_move_distance;
    const coordf_t branch_radius = config.tree_support_branch_diameter.value / 2;
    const size_t tip_layers = branch_radius / layer_height; //The number of layers to be shrinking the circle to create a tip. This produces a 45 degree angle.
    const double diameter_angle_scale_factor = sin(config.tree_support_branch_diameter_angle.value * M_PI / 180.) * layer_height / branch_radius; //Scale factor per layer to produce the desired angle.
    const coordf_t radius_sample_resolution = m_ts_data->m_radius_sample_resolution;
    const bool support_on_buildplate_only = config.support_material_buildplate_only.value;
    const size_t bottom_interface_layers = config.support_material_bottom_interface_layers.value;

    std::unordered_set<Node*> to_free_node_set;
    m_spanning_trees.resize(contact_nodes.size());
    //m_mst_line_x_layer_contour_caches.resize(contact_nodes.size());

    {// get outlines below and avoidance area using tbb
        m_object.print()->set_status(90, "Support: preparing avoidance regions ");
        // get all the possible radiis
        std::vector<std::set<coordf_t> > all_layer_radiis(m_highest_overhang_layer+1);
        std::vector<std::set<size_t> > all_layer_node_dist(m_highest_overhang_layer+1);
        for (size_t layer_nr = m_highest_overhang_layer; layer_nr > 0; layer_nr--)
        {
            auto& layer_contact_nodes = contact_nodes[layer_nr];
            auto& layer_radiis = all_layer_radiis[layer_nr];
            auto& layer_node_dist = all_layer_node_dist[layer_nr];
            if (layer_contact_nodes.empty() == false) {
                for (Node* p_node : layer_contact_nodes) {
                    layer_node_dist.emplace(p_node->distance_to_top);
                }
            }
            if (layer_nr < m_highest_overhang_layer) {
                for (auto node_dist : all_layer_node_dist[layer_nr + 1])
                    layer_node_dist.emplace(node_dist+1);
            }
            for (auto node_dist : layer_node_dist) {
                layer_radiis.emplace(calc_branch_radius(branch_radius, node_dist, tip_layers, diameter_angle_scale_factor));
            }
        }
        // parallel pre-compute avoidance
        tbb::parallel_for(tbb::blocked_range<size_t>(1, m_highest_overhang_layer),
            [&](const tbb::blocked_range<size_t>& range) {
                for (size_t layer_nr = range.begin(); layer_nr < range.end(); layer_nr++) {
                    for (auto node_dist : all_layer_node_dist[layer_nr])
                    {
                        m_ts_data->get_avoidance(0, layer_nr - 1);
                        m_ts_data->get_avoidance(calc_branch_radius(branch_radius, node_dist, tip_layers, diameter_angle_scale_factor), layer_nr - 1);
                    }
                }
            });

        BOOST_LOG_TRIVIAL(debug) << "before m_avoidance_cache.size()=" << m_ts_data->m_avoidance_cache.size();
    }

    for (size_t layer_nr = contact_nodes.size() - 1; layer_nr > 0; layer_nr--) //Skip layer 0, since we can't drop down the vertices there.
    {
        auto& layer_contact_nodes = contact_nodes[layer_nr];
        std::deque<std::pair<size_t, Node*>> unsupported_branch_leaves; // All nodes that are leaves on this layer that would result in unsupported ('mid-air') branches.
        const Layer* ts_layer = m_object.get_tree_support_layer(layer_nr);
        if (layer_contact_nodes.empty())
            continue;
        m_object.print()->set_status(90, "Support: drop_nodes at layer " + std::to_string(layer_nr));

        for (Node* p_node : layer_contact_nodes)
        {
            if (p_node->type == ePolygon) {
                Node* next_node = new Node(*p_node);
                next_node->distance_to_top++;
                next_node->support_roof_layers_below--;
                contact_nodes[layer_nr - 1].emplace_back(next_node);
            }
        }

        Polygons layer_contours = std::move(m_ts_data->get_contours_with_holes(layer_nr));
        //std::unordered_map<Line, bool, LineHash>& mst_line_x_layer_contour_cache = m_mst_line_x_layer_contour_caches[layer_nr];
        std::unordered_map<Line, bool, LineHash> mst_line_x_layer_contour_cache;
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
                profiler.stage_add(STAGE_intersection_ln, true);
                if (!pls_intersect.empty())
                    return true;
            }
            return false;
        };

        //Group together all nodes for each part.
        const ExPolygons& parts = m_ts_data->get_avoidance(0, layer_nr);
        std::vector<std::unordered_map<Point, Node*, PointHash>> nodes_per_part(1 + parts.size()); //All nodes that aren't inside a part get grouped together in the 0th part.
        for (Node* p_node : layer_contact_nodes)
        {
            const Node& node = *p_node;
            if (node.type == ePolygon) {
                // polygon node do not merge or move
                continue;
            }

            if (support_on_buildplate_only && !node.to_buildplate) //Can't rest on model and unable to reach the build plate. Then we must drop the node and leave parts unsupported.
            {
                unsupported_branch_leaves.push_front({ layer_nr, p_node });
                continue;
            }
            if (node.to_buildplate || parts.empty()) //It's outside, so make it go towards the build plate.
            {
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
            nodes_per_part[closest_part + 1][node.position] = p_node; //Index + 1 because the 0th index is the outside part.
        }

        //Create a MST for every part.
        profiler.tic();
        //std::vector<MinimumSpanningTree>& spanning_trees = m_spanning_trees[layer_nr];
        std::vector<MinimumSpanningTree> spanning_trees;
        for (const std::unordered_map<Point, Node*, PointHash>& group : nodes_per_part)
        {
            std::vector<Point> points_to_buildplate;
            for (const std::pair<const Point, Node*>& entry : group)
            {
                points_to_buildplate.emplace_back(entry.first); //Just the position of the node.
            }
            spanning_trees.emplace_back(points_to_buildplate);
        }
        profiler.stage_add(STAGE_MinimumSpanningTree,true);

#ifdef SUPPORT_TREE_DEBUG_TO_SVG
        coordf_t branch_radius_temp = 0;
        coordf_t max_y = std::numeric_limits<coordf_t>::min();
        draw_layer_mst(layer_nr, spanning_trees, m_object.get_layer(layer_nr)->lslices);
#endif
        for (size_t group_index = 0; group_index < nodes_per_part.size(); group_index++)
        {
            const MinimumSpanningTree& mst = spanning_trees[group_index];
            //In the first pass, merge all nodes that are close together.
            std::unordered_set<Node*> to_delete;
            for (const std::pair<const Point, Node*>& entry : nodes_per_part[group_index])
            {
                Node* p_node = entry.second;
                Node& node = *p_node;
                if (to_delete.find(p_node) != to_delete.end())
                {
                    continue; //Delete this node (don't create a new node for it on the next layer).
                }
                const std::vector<Point>& neighbours = mst.adjacent_nodes(node.position);
                if (neighbours.size() == 1 && vsize2_with_unscale(neighbours[0] - node.position) < max_move_distance2 && mst.adjacent_nodes(neighbours[0]).size() == 1) //We have just two nodes left, and they're very close!
                {
                    //Insert a completely new node and let both original nodes fade.
                    Point next_position = (node.position + neighbours[0]) / 2; //Average position of the two nodes.

                    const coordf_t branch_radius_node = calc_branch_radius(branch_radius, node.distance_to_top, tip_layers, diameter_angle_scale_factor);

                    auto avoid_layer = m_ts_data->get_avoidance(branch_radius_node, layer_nr - 1);
                    if (group_index == 0)
                    {
                        //Avoid collisions.
                        const coordf_t max_move_between_samples = max_move_distance + radius_sample_resolution + EPSILON; //100 micron extra for rounding errors.
                        move_out_expolys(avoid_layer, next_position, radius_sample_resolution + EPSILON, max_move_between_samples);
                    }

                    Node* neighbour = nodes_per_part[group_index][neighbours[0]];
                    size_t new_distance_to_top = std::max(node.distance_to_top, neighbour->distance_to_top) + 1;
                    size_t new_support_roof_layers_below = std::max(node.support_roof_layers_below, neighbour->support_roof_layers_below) - 1;

                    const bool to_buildplate = !is_inside_ex(m_ts_data->get_avoidance(0, layer_nr - 1), next_position);
                    Node* next_node = new Node(next_position, new_distance_to_top, node.skin_direction, new_support_roof_layers_below, to_buildplate, p_node);
                    next_node->movement = next_position - node.position;
                    contact_nodes[layer_nr - 1].push_back(next_node);

                    // Make sure the next pass doesn't drop down either of these (since that already happened).
                    node.merged_neighbours.push_front(neighbour);
                    to_delete.insert(neighbour);
                    to_delete.insert(p_node);
                }
                else if (neighbours.size() > 1) //Don't merge leaf nodes because we would then incur movement greater than the maximum move distance.
                {
                    //Remove all neighbours that are too close and merge them into this node.
                    for (const Point& neighbour : neighbours)
                    {
                        if (vsize2_with_unscale(neighbour - node.position) < max_move_distance2)
                        {
                            Node* neighbour_node = nodes_per_part[group_index][neighbour];
                            node.distance_to_top = std::max(node.distance_to_top, neighbour_node->distance_to_top);
                            node.support_roof_layers_below = std::max(node.support_roof_layers_below, neighbour_node->support_roof_layers_below);
                            node.merged_neighbours.push_front(neighbour_node);
                            node.merged_neighbours.insert_after(node.merged_neighbours.end(), neighbour_node->merged_neighbours.begin(), neighbour_node->merged_neighbours.end());
                            to_delete.insert(neighbour_node);
                        }
                    }
                }
            }

            //In the second pass, move all middle nodes.
            for (const std::pair<const Point, Node*>& entry : nodes_per_part[group_index])
            {
                Node* p_node = entry.second;
                const Node& node = *p_node;
                if (to_delete.find(p_node) != to_delete.end())
                {
                    continue;
                }
                //If the branch falls completely inside a collision area (the entire branch would be removed by the X/Y offset), delete it.
                if (group_index > 0 && is_inside_ex(m_ts_data->get_collision(m_ts_data->m_xy_distance, layer_nr), node.position))
                {
                    const coordf_t branch_radius_node = calc_branch_radius(branch_radius, node.distance_to_top, tip_layers, diameter_angle_scale_factor);
                    Point to_outside = projection_onto_ex(m_ts_data->get_collision(m_ts_data->m_xy_distance, layer_nr), node.position);
                    if (vsize2_with_unscale(node.position - to_outside) >= branch_radius_node * branch_radius_node) //Too far inside.
                    {
                        if (support_on_buildplate_only)
                        {
                            unsupported_branch_leaves.push_front({ layer_nr, p_node });
                        }
                        else {
                            p_node->support_floor_layers_above = 1;
                        }
                        continue;
                    }
                    // if the link between parent and current is cut by contours, delete this branch
                    if (p_node->parent && intersection_ln({p_node->position, p_node->parent->position}, layer_contours).empty()==false)
                    {
                        //unsupported_branch_leaves.push_front({ layer_nr, p_node });
                        Node* pn = p_node->parent;
                        for (int i = 0; i < bottom_interface_layers && pn; i++, pn = pn->parent)
                            pn->support_floor_layers_above = bottom_interface_layers - i;
                        to_delete.insert(p_node);
                        continue;
                    }
                }
                Point next_layer_vertex = node.position;
                Point move_to_neighbor_center;
                const std::vector<Point> neighbours = mst.adjacent_nodes(node.position);
                // 1. do not merge neighbors under 5mm
                // 2. Only merge node with single neighbor in distance between [max_move_distance, 10mm/layer_height]
                if (layer_nr>5/layer_height && 
                    (neighbours.size() > 1 || (neighbours.size() == 1 && vsize2_with_unscale(neighbours[0] - node.position) >= max_move_distance2 && vsize2_with_unscale(neighbours[0] - node.position) < SQ(10/layer_height)*max_move_distance2))) //Only nodes that aren't about to collapse.
                {
                    //Move towards the average position of all neighbours.
                    Point sum_direction(0, 0);
                    for (const Point& neighbour : neighbours)
                    {
                        Point direction = neighbour - node.position;
                        Node *neighbour_node = nodes_per_part[group_index][neighbour];
                        coordf_t branch_bottom_radius = calc_branch_radius(branch_radius, node.distance_to_top + layer_nr, tip_layers, diameter_angle_scale_factor);
                        coordf_t neighbour_bottom_radius = calc_branch_radius(branch_radius, neighbour_node->distance_to_top + layer_nr, tip_layers, diameter_angle_scale_factor);
                        const coordf_t min_overlap = branch_radius;
                        double max_converge_distance = tan_angle * ts_layer->print_z + branch_bottom_radius + neighbour_bottom_radius - min_overlap;
                        if (vsize2_with_unscale(direction) > max_converge_distance * max_converge_distance)
                            continue;

                        if (is_line_cut_by_contour(node.position, neighbour))
                            continue;

                        sum_direction += direction;
                    }

                    if(vsize2_with_unscale(sum_direction) <= max_move_distance2)
                    {
                        move_to_neighbor_center = sum_direction;
                    }
                    else
                    {
                        move_to_neighbor_center = normal(sum_direction, scale_(max_move_distance));
                    }
                    // add momentum to force smooth movement
                    move_to_neighbor_center = move_to_neighbor_center * 0.5 + p_node->movement * 0.5;
                }

                const coordf_t branch_radius_node = calc_branch_radius(branch_radius, node.distance_to_top, tip_layers, diameter_angle_scale_factor);
#ifdef SUPPORT_TREE_DEBUG_TO_SVG
                if (node.position(1) > max_y) {
                    max_y = node.position(1);
                    branch_radius_temp = branch_radius_node;
                }
#endif
                auto avoid_layer = m_ts_data->get_avoidance(branch_radius_node, layer_nr - 1);

#if 1
                Point to_outside = projection_onto_ex(avoid_layer, node.position);
                Point movement = to_outside - node.position;
                double movelength2 = vsize2_with_unscale(movement);
                // don't move if
                // 1) line of node and to_outside is cut by contour (means supports may intersect with object)
                // 2) it's impossible to move to build plate
                if (is_line_cut_by_contour(node.position, to_outside) || movelength2 > max_move_distance2 * SQ(layer_nr))
                    movement = Point(0, 0);
                else if (movelength2 > max_move_distance2) {
                    if (is_inside_ex(avoid_layer, node.position))
                        movement = normal(movement, scale_(max_move_distance));
                    else
                        movement = Point(0, 0);  // point is already outside contour, no need to move
                }
                // move to the averaged direction of neighbor center and contour edge if they are roughly same direction
                if (movement.dot(move_to_neighbor_center) >= 0)
                    movement = movement + move_to_neighbor_center;
                else
                    movement = move_to_neighbor_center;  // otherwise move to neighbor center first

                if (vsize2_with_unscale(movement) > max_move_distance2)
                    movement = normal(movement, scale_(max_move_distance));
#else
                Point movement = move_to_neighbor_center;
#endif
                next_layer_vertex += movement;


                if (/*group_index ==*/ 0)
                {
                    //Avoid collisions.
                    const coordf_t max_move_between_samples = max_move_distance + radius_sample_resolution + EPSILON; //100 micron extra for rounding errors.
                    bool is_outside = move_out_expolys(avoid_layer, next_layer_vertex, radius_sample_resolution + EPSILON, max_move_between_samples);
                    if (!is_outside) {
                        Point candidate_vertex = node.position;
                        is_outside = move_out_expolys(avoid_layer, candidate_vertex, radius_sample_resolution + EPSILON, max_move_between_samples);
                        if (is_outside) {
                            next_layer_vertex = candidate_vertex;
                        }
                    }
                }

                const bool to_buildplate = !is_inside_ex(m_ts_data->m_layer_outlines[layer_nr], next_layer_vertex);
                Node* next_node = new Node(next_layer_vertex, node.distance_to_top + 1, node.skin_direction, node.support_roof_layers_below - 1, to_buildplate, p_node);
                next_node->movement = movement;
                contact_nodes[layer_nr - 1].push_back(next_node);
            }
        }

#ifdef SUPPORT_TREE_DEBUG_TO_SVG
        draw_avoidance_and_nodes_to_svg(layer_nr, m_ts_data->get_avoidance(0, layer_nr), m_ts_data->get_avoidance(branch_radius_temp, layer_nr), m_ts_data->m_layer_outlines_below[layer_nr],
            contact_nodes[layer_nr], contact_nodes[layer_nr - 1], "contact_points", { "overhang","avoid","outline" }, { "blue","red","yellow" });
#endif

        // Prune all branches that couldn't find support on either the model or the buildplate (resulting in 'mid-air' branches).
        for (;! unsupported_branch_leaves.empty(); unsupported_branch_leaves.pop_back())
        {
            const auto& entry = unsupported_branch_leaves.back();
            Node* i_node = entry.second;
            for (size_t i_layer = entry.first; i_node != nullptr; ++i_layer, i_node = i_node->parent)
            {
                std::vector<Node*>::iterator to_erase = std::find(contact_nodes[i_layer].begin(), contact_nodes[i_layer].end(), i_node);
                if (to_erase != contact_nodes[i_layer].end())
                {
                    to_free_node_set.insert(*to_erase);
                    contact_nodes[i_layer].erase(to_erase);
                    to_free_node_set.insert(i_node);

                    for (Node* neighbour : i_node->merged_neighbours)
                    {
                        unsupported_branch_leaves.push_front({ i_layer, neighbour });
                    }
                }
            }
        }
    }

    BOOST_LOG_TRIVIAL(debug) << "after m_avoidance_cache.size()=" << m_ts_data->m_avoidance_cache.size();

    for (Node *node : to_free_node_set)
    {
        delete node;
    }
    to_free_node_set.clear();
}

void TreeSupport::generate_contact_points(std::vector<std::vector<TreeSupport::Node*>>& contact_nodes)
{
    const PrintObjectConfig &config = m_object.config();
    const coordf_t point_spread = scale_(config.tree_support_branch_distance.value);

    //First generate grid points to cover the entire area of the print.
    BoundingBox bounding_box = m_object.bounding_box();
    const Point bounding_box_size = bounding_box.max - bounding_box.min;
    constexpr double rotate_angle = 22.0 / 180.0 * M_PI;
    constexpr double thresh_big_overhang = SQ(scale_(10));

    const auto center = bounding_box_middle(bounding_box);
    const auto sin_angle = std::sin(rotate_angle);
    const auto cos_angle = std::cos(rotate_angle);
    const auto rotated_dims = Point(
        bounding_box_size(0) * cos_angle + bounding_box_size(1) * sin_angle,
        bounding_box_size(0) * sin_angle + bounding_box_size(1) * cos_angle) / 2;

    std::vector<Point> grid_points;
    for (auto x = -rotated_dims(0); x < rotated_dims(0); x += point_spread) {
        for (auto y = -rotated_dims(1); y < rotated_dims(1); y += point_spread) {
            Point pt(x, y);
            pt.rotate(cos_angle, sin_angle);
            pt += center;
            if (bounding_box.contains(pt)) {
                grid_points.push_back(pt);
            }
        }
    }

    const coordf_t layer_height = config.layer_height.value;
    const coordf_t z_distance_top = config.support_material_contact_distance.value;
    const size_t z_distance_top_layers = round_up_divide(scale_(z_distance_top), scale_(layer_height)) + 1; //Support must always be 1 layer below overhang.
    const size_t support_roof_layers = config.support_material_interface_layers.value;
    coordf_t half_overhang_distance = 0.;
    if (config.support_material_threshold.value < EPSILON) {
        half_overhang_distance = tan(30. * M_PI / 180.0) * layer_height / 2;
    }
    else {
        half_overhang_distance = tan((double)config.support_material_threshold.value * M_PI / 180.0) * layer_height / 2;
    }

    m_highest_overhang_layer = 0;
    // fix bug of generating support for very thin objects
    if (m_object.layers().size() <= z_distance_top_layers + 1)
        return;

    for (size_t layer_nr = 1; layer_nr < m_object.layers().size() - z_distance_top_layers; layer_nr++)
    {
        const ExPolygons &overhang = m_object.get_tree_support_layer(layer_nr + m_raft_layers + z_distance_top_layers)->overhang_areas;
        if (overhang.empty())
            continue;
        
        m_highest_overhang_layer = std::max(m_highest_overhang_layer, layer_nr);

        for (const ExPolygon &overhang_part : overhang)
        {
            BoundingBox overhang_bounds = get_extents(overhang_part);
            if (overhang_part.area() > thresh_big_overhang) {
                Point candidate = overhang_bounds.center();
                if (!overhang_part.contains(candidate))
                    move_inside_expoly(overhang_part, candidate);
                Node* contact_node = new Node(candidate, 0, (layer_nr + z_distance_top_layers) % 2, support_roof_layers, true, Node::NO_PARENT);
                contact_node->type = ePolygon;
                contact_node->overhang = &overhang_part;
                contact_nodes[layer_nr].emplace_back(contact_node);
                continue;
            }

            overhang_bounds.inflated(scale_(half_overhang_distance));
            bool added = false; //Did we add a point this way?
            for (Point candidate : grid_points)
            {
                if (overhang_bounds.contains(candidate))
                {
                    constexpr coordf_t distance_inside = 0; //Move point towards the border of the polygon if it is closer than half the overhang distance: Catch points that fall between overhang areas on constant surfaces.
                    move_inside_expoly(overhang_part, candidate, distance_inside, half_overhang_distance);
                    constexpr bool border_is_inside = true;
                    if (is_inside_ex(overhang_part, candidate))
                    {
                        if (!is_inside_ex(m_ts_data->get_collision(m_ts_data->m_xy_distance, layer_nr), candidate)) {
                            constexpr size_t distance_to_top = 0;
                            constexpr bool to_buildplate = true;
                            Node* contact_node = new Node(candidate, distance_to_top, (layer_nr + z_distance_top_layers) % 2, support_roof_layers, to_buildplate, Node::NO_PARENT);
                            contact_nodes[layer_nr].emplace_back(contact_node);
                            added = true;
                        }
                    }
                }
            }

            if (!added) //If we didn't add any points due to bad luck, we want to add one anyway such that loose parts are also supported.
            {
                auto bbox = overhang_part.contour.bounding_box();
                Points candidates = { bbox.min, bounding_box_middle(bbox), bbox.max };
                for (Point candidate : candidates) {
                    if (!overhang_part.contains(candidate))
                        move_inside_expoly(overhang_part, candidate);
                    constexpr size_t distance_to_top = 0;
                    constexpr bool to_buildplate = true;
                    Node* contact_node = new Node(candidate, distance_to_top, layer_nr % 2, support_roof_layers, to_buildplate, Node::NO_PARENT);
                    contact_nodes[layer_nr].emplace_back(contact_node);
                }
            }
        }

#ifdef SUPPORT_TREE_DEBUG_TO_SVG
        draw_avoidance_and_nodes_to_svg(layer_nr, overhang, m_ts_data->m_layer_outlines_below[layer_nr], {},
            contact_nodes[layer_nr], {}, "init_contact_points", { "overhang","outlines","" });
#endif
    }
}

void TreeSupport::insert_dropped_node(std::vector<Node*>& nodes_layer, Node* p_node)
{
    std::vector<Node*>::iterator conflicting_node_it = std::find(nodes_layer.begin(), nodes_layer.end(), p_node);
    if (conflicting_node_it == nodes_layer.end()) //No conflict.
    {
        nodes_layer.emplace_back(p_node);
        return;
    }

    Node* conflicting_node = *conflicting_node_it;
    conflicting_node->distance_to_top = std::max(conflicting_node->distance_to_top, p_node->distance_to_top);
    conflicting_node->support_roof_layers_below = std::max(conflicting_node->support_roof_layers_below, p_node->support_roof_layers_below);
}

TreeSupportData::TreeSupportData(const PrintObject &object, coordf_t xy_distance, coordf_t max_move, coordf_t radius_sample_resolution)
    : m_xy_distance(xy_distance), m_max_move(max_move), m_radius_sample_resolution(radius_sample_resolution)
{
    for (std::size_t layer_nr  = 0; layer_nr < object.layers().size(); ++layer_nr)
    {
        const Layer* layer = object.get_layer(layer_nr);
        m_layer_outlines.push_back(ExPolygons());
        ExPolygons& outline = m_layer_outlines.back();
        for (const ExPolygon& poly : layer->lslices) {
            poly.simplify(scale_(m_radius_sample_resolution), &outline);
        }

        if (layer_nr == 0)
            m_layer_outlines_below.push_back(outline);
        else
            m_layer_outlines_below.push_back(union_ex(m_layer_outlines_below.end()[-1], outline));
    }
}

const ExPolygons& TreeSupportData::get_collision(coordf_t radius, size_t layer_nr) const
{
    profiler.tic();
    radius = ceil_radius(radius);
    RadiusLayerPair key{radius, layer_nr};
    const auto it = m_collision_cache.find(key);
    const ExPolygons& collision = it != m_collision_cache.end() ? it->second : calculate_collision(key);
    profiler.stage_add(STAGE_get_collision, true);
    return collision;
}

const ExPolygons& TreeSupportData::get_avoidance(coordf_t radius, size_t layer_nr) const
{
    profiler.tic();
    radius = ceil_radius(radius);
    RadiusLayerPair key{radius, layer_nr};
    const auto it = m_avoidance_cache.find(key);
    const ExPolygons& avoidance = it != m_avoidance_cache.end() ? it->second : calculate_avoidance(key);
    
    profiler.stage_add(STAGE_GET_AVOIDANCE, true);
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

coordf_t TreeSupportData::ceil_radius(coordf_t radius) const
{
#if 0
    size_t factor = (size_t)(radius / m_radius_sample_resolution);
    coordf_t remains = radius - m_radius_sample_resolution * factor;
    if (remains > EPSILON) {
        return radius + m_radius_sample_resolution - remains;
    }
    else {
        return radius;
    }
#else
    coordf_t resolution = m_radius_sample_resolution;
    return ceil(radius / resolution) * resolution;
#endif
}

const ExPolygons& TreeSupportData::calculate_collision(const RadiusLayerPair& key) const
{
    const auto& radius = key.first;
    const auto& layer_nr = key.second;

    assert(layer_nr < m_layer_outlines.size());

    ExPolygons collision_areas = std::move(offset_ex(m_layer_outlines[layer_nr], scale_(radius)));
    const auto ret = m_collision_cache.insert({ key, std::move(collision_areas) });
    return ret.first->second;
}

const ExPolygons& TreeSupportData::calculate_avoidance(const RadiusLayerPair& key) const
{
    const auto& radius = key.first;
    const auto& layer_idx = key.second;
#if 0
    if (layer_idx == 0)
    {
        m_avoidance_cache[key] = get_collision(radius, 0);
        return m_avoidance_cache[key];
    }

    // Avoidance for a given layer depends on all layers beneath it so could have very deep recursion depths if
    // called at high layer heights. We can limit the reqursion depth to N by checking if the layer N
    // below the current one exists and if not, forcing the calculation of that layer. This may cause another recursion
    // if the layer at 2N below the current one but we won't exceed our limit unless there are N*N uncalculated layers
    // below our current one.
    constexpr auto max_recursion_depth = 100;
    // Check if we would exceed the recursion limit by trying to process this layer
    if (layer_nr >= max_recursion_depth
        && m_avoidance_cache.find({radius, layer_nr - max_recursion_depth}) == m_avoidance_cache.end())
    {
        // Force the calculation of the layer `max_recursion_depth` below our current one, ignoring the result.
        get_avoidance(radius, layer_nr - max_recursion_depth);
    }

    ExPolygons avoidance_areas = std::move(offset_ex(get_avoidance(radius, layer_nr - 1), scale_(-m_max_move)));
    const ExPolygons& collision = get_collision(radius, layer_nr);
    avoidance_areas.insert(avoidance_areas.end(), collision.begin(), collision.end());
    avoidance_areas = std::move(union_ex(avoidance_areas));
    const auto ret = m_avoidance_cache.insert({key, std::move(avoidance_areas)});
    assert(ret.second);
#else
    ExPolygons avoidance_areas = std::move(offset_ex(m_layer_outlines_below[layer_idx], scale_(m_xy_distance+radius)));
    const auto ret = m_avoidance_cache.insert({ key, std::move(avoidance_areas) });
    assert(ret.second);
#endif
    return ret.first->second;
}

} //namespace Slic3r
