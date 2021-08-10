#include <math.h>

#include "MinimumSpanningTree.hpp"
#include "TreeSupport.hpp"
#include "Print.hpp"
#include "Layer.hpp"
#include "Fill/FillBase.hpp"

#include "SVG.hpp"

#define SQRT_2 1.4142135623730950488 //Square root of 2.
#define CIRCLE_RESOLUTION 10 //The number of vertices in each circle.

#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif

#define TAU (2.0 * M_PI)
#define NO_INDEX (std::numeric_limits<unsigned int>::max())

//#define SUPPORT_TREE_DEBUG_TO_SVG

namespace Slic3r
{
#define unscale_(val) ((val) * SCALING_FACTOR)

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

Point normal(Point pt, double scale)
{
    double length = scale_(sqrt(vsize2_with_unscale(pt)));

    return pt * (scale / length);
}

#ifdef SUPPORT_TREE_DEBUG_TO_SVG
std::string get_svg_filename(int layer_nr, std::string tag = "")
{
    static bool rand_init = false;

    if (!rand_init) {
        srand(time(NULL));
        rand_init = true;
    }

    int rand_num = rand() % 1000000;
    std::string prefix = "./bbl_ts_";
    std::string suffix = ".svg";
    char buf1[256];
    char buf2[256];

    memset(buf1, sizeof(buf1), 0);
    memset(buf2, sizeof(buf2), 0);
    itoa(layer_nr, buf1, 10);
    itoa(rand_num, buf2, 10);
    return prefix + tag + "_" + buf1 + "_" + buf2 + suffix;
}

void draw_layer_to_svg
(
    int layer_nr,
    const ExPolygons &avoidance_polys,
    const std::vector<TreeSupport::Node*> &layer_nodes,
    const std::vector<TreeSupport::Node*> &lower_layer_nodes,
    BoundingBox bbox
)
{
    SVG svg(get_svg_filename(layer_nr, "avoidance").c_str(), bbox);
    svg.draw(union_ex(avoidance_polys), "blue");

    Points layer_pts;
    for (TreeSupport::Node *node : layer_nodes) {
        layer_pts.push_back(node->position);
    }
    svg.draw(layer_pts, "green", coord_t(scale_(0.1)));

    layer_pts.clear();
    for (TreeSupport::Node *node : lower_layer_nodes) {
        layer_pts.push_back(node->position);
    }
    svg.draw(layer_pts, "black", coord_t(scale_(0.1)));

}

void draw_layer_mst
(
    int layer_nr,
    const std::vector<MinimumSpanningTree> &spanning_trees,
    BoundingBox bbox
)
{
    SVG svg(get_svg_filename(layer_nr, "mstree").c_str(), bbox);
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

                Polyline line(pt1, pt2);
                svg.draw(line, "blue", coord_t(scale_(0.05)));
            }

            to_ignore.insert(pt1);
        }
    }
}
#endif

unsigned int move_inside_ex(const ExPolygon &polygon, Point& from, double distance = 0, double maxDist2 = std::numeric_limits<double>::max())
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
    else if (bestDist2 < maxDist2)
    {
        from = ret;
    }
    return 0;
}

/*
 * Implementation assumes moving inside, but moving outside should just as well be possible.
 */
unsigned int move_inside_ex(const ExPolygons& polygons, Point& from, double distance, double maxDist2)
{
    Point ret = from;
    double bestDist2 = std::numeric_limits<double>::max();
    unsigned int bestPoly = NO_INDEX;
    bool is_already_on_correct_side_of_boundary = false; // whether [from] is already on the right side of the boundary
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
        for(const Point& p2 : poly.contour.points)
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
                        bestPoly = poly_idx;
                        if (distance == 0) { ret = x; }
                        else
                        {
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
                    bestPoly = poly_idx;
                    if (distance == 0) { ret = x; }
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
    }
    if (is_already_on_correct_side_of_boundary) // when the best point is already inside and we're moving inside, or when the best point is already outside and we're moving outside
    {
        if (bestDist2 < distance * distance)
        {
            from = ret;
        }
        return bestPoly;
    }
    else if (bestDist2 < maxDist2)
    {
        from = ret;
        return bestPoly;
    }
    return NO_INDEX;
}

Point find_closest_ex(Point from, const ExPolygons& polygons)
{
    Point closest_pt;
    double min_dist2 = std::numeric_limits<double>::max();

    for (const ExPolygon &poly : polygons) {
        const Point* candidate = poly.contour.closest_point(from);
        double dist2 = vsize2_with_unscale(*candidate - from);
        if (dist2 < min_dist2) {
            closest_pt = *candidate;
            min_dist2 = dist2;
        }
    }

    return closest_pt;
}


unsigned int move_outside_ex(const ExPolygons& polygons, Point& from, double distance, double maxDist2)
{
    return move_inside_ex(polygons, from, -distance, maxDist2);
}

bool is_inside_ex(const ExPolygon &polygon, const Point &pt)
{
    if (!get_extents(polygon).contains(pt))
        return false;

    return polygon.contains(pt);
}

bool is_inside_ex(const ExPolygons &polygons, const Point &pt)
{
    for (const ExPolygon &poly : polygons) {
        if (is_inside_ex(poly, pt))
            return true;
    }

    return false;
}

Point bounding_box_middle(const BoundingBox &bbox)
{
    return (bbox.max + bbox.min) / 2;
}

TreeSupport::TreeSupport(PrintObject& object)
    : m_object(object)
{
    const PrintObjectConfig &config = m_object.config();

    const coordf_t layer_height = config.layer_height.value;
    const coordf_t line_width = config.support_material_extrusion_width.get_abs_value(layer_height);
    // FIXME: currently use support_material_extrusion_width to replace support_material_external_perimeter_width
    const coordf_t xy_distance = config.support_material_xy_spacing.get_abs_value(line_width);
    const double angle = config.tree_support_branch_angle.value * M_PI / 180.;
    const coordf_t max_move_distance
        = (angle < TAU / 4) ? (coordf_t)(tan(angle) * layer_height) : std::numeric_limits<coordf_t>::max();
    const coordf_t radius_sample_resolution = config.tree_support_collision_resolution.value;

    m_ts_data = new TreeSupportData(m_object, xy_distance, max_move_distance, radius_sample_resolution);
}

void TreeSupport::detect_object_overhangs()
{
    const PrintObjectConfig& object_config = m_object.config();
    const coordf_t radius_sample_resolution = object_config.tree_support_collision_resolution.value;
    double threshold_rad = 0.;
    if (object_config.support_material_threshold.value < EPSILON) {
        threshold_rad = 45. * M_PI / 180.;
    }
    else {
        threshold_rad = object_config.support_material_threshold.value * M_PI / 180.;
    }

    for (Layer *layer : m_object.layers()) {
        if (layer->lower_layer == nullptr)
            continue;

        Layer *lower_layer = layer->lower_layer;
        coordf_t lower_layer_offset = (float)lower_layer->height / tan(threshold_rad);
        ExPolygons overhang_areas = std::move(diff_ex(layer->lslices, offset_ex(lower_layer->lslices, scale_(lower_layer_offset))));
        for (ExPolygon& poly : overhang_areas) {
            poly.simplify(scale_(radius_sample_resolution), &layer->overhang_areas);
        }
    }
}

void TreeSupport::create_tree_support_layers()
{
    for (TreeSupportLayer *ts_layer : m_object.tree_support_layers()) {
        //delete ts_layer;
    }
    m_object.tree_support_layers().clear();

    for (Layer *layer : m_object.layers()) {
        m_object.add_tree_support_layer(layer->id(), layer->height, layer->print_z, layer->slice_z);
    }
}

static inline void fill_expolygon_generate_paths(
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

    extrusion_entities_append_paths(dst, std::move(polylines), role, flow.mm3_per_mm(), flow.width, flow.height);
}

static inline void fill_expolygons_generate_paths(
    ExtrusionEntitiesPtr   &dst,
    ExPolygons            &&expolygons,
    Fill                   *filler,
    const FillParams       &fill_params,
    float                   density,
    ExtrusionRole           role,
    const Flow             &flow)
{
    for (ExPolygon& expoly : expolygons)
        fill_expolygon_generate_paths(dst, std::move(expoly), filler, fill_params, density, role, flow);
}

void TreeSupport::generate_fills()
{
    const PrintConfig &print_config = m_object.print()->config();
    const PrintObjectConfig &object_config = m_object.config();
    coordf_t support_extrusion_width = object_config.support_material_extrusion_width.value > 0 ? object_config.support_material_extrusion_width : object_config.extrusion_width;
    coordf_t nozzle_diameter = print_config.nozzle_diameter.get_at(object_config.support_material_extruder - 1);
    coordf_t support_spacing = object_config.support_material_spacing.value;
    coordf_t interface_spacing = object_config.support_material_interface_spacing.value;
    double support_density = support_extrusion_width / (support_spacing + support_extrusion_width);
    double interface_density = support_extrusion_width / (interface_spacing + support_extrusion_width);

    for (TreeSupportLayer *layer : m_object.tree_support_layers()) {
        Flow support_flow(support_extrusion_width, layer->height, nozzle_diameter);
        Fill* filler_interface = Fill::new_from_type(ipRectilinear);
        Fill* filler_support = Fill::new_from_type(ipRectilinear);
        //filler_interface->set_bounding_box(bbox_object);
        //filler_support->set_bounding_box(bbox_object);

        filler_interface->angle = layer->id() % 2 ? 0 : 90;
        filler_interface->spacing = support_extrusion_width;
        filler_support->angle = 0.;
        filler_support->spacing = support_extrusion_width;

        // bool stands for is_support_interface
        std::vector<std::pair<ExPolygons *, bool>> area_groups;
        area_groups.emplace_back(&layer->roof_areas, true);
        area_groups.emplace_back(&layer->floor_areas, true);
        area_groups.emplace_back(&layer->base_areas, false);
        for (std::pair<ExPolygons*, bool>& area_group : area_groups) {
            for (ExPolygon& poly : *area_group.first) {
                Polylines polylines;
                polylines.reserve(poly.holes.size() + 1);
                for (size_t i = 0; i <= poly.holes.size(); ++i) {
                    Polyline pl(i == 0 ? poly.contour.points : poly.holes[i - 1].points);
                    pl.points.emplace_back(pl.points.front());
                    //pl.clip_end(clip_length);
                    polylines.emplace_back(std::move(pl));
                }

                if (area_group.second) {
                    FillParams fill_params;
                    fill_params.density = interface_density;
                    fill_params.dont_adjust = true;
                    fill_expolygons_generate_paths(layer->support_fills.entities, offset_ex(poly, float(-0.4 * interface_spacing)),
                        filler_interface, fill_params, interface_density, erSupportMaterialInterface, support_flow);
                } else {
                    extrusion_entities_append_paths(layer->support_fills.entities, polylines, erSupportMaterial,
                        support_flow.mm3_per_mm(), support_flow.width, support_flow.height);
                }
            }
        }
    }
}

void TreeSupport::generate_support_areas()
{
    const PrintObjectConfig &config = m_object.config();
    bool tree_support_enable = config.support_material.value && config.auto_support_type.value == astTree;
    if (!tree_support_enable)
        return;

    std::vector<std::vector<Node*>> contact_nodes(m_object.layers().size()); //Generate empty layers to store the points in.

    // Generate overhang areas
    detect_object_overhangs();

    // Create Tree Support Layers
    create_tree_support_layers();

    // Generate contact points of tree support
    generate_contact_points(contact_nodes);

    //Drop nodes to lower layers.
    drop_nodes(contact_nodes);

    //Generate support areas.
    draw_circles(contact_nodes);

    for (auto& layer : contact_nodes)
    {
        for (Node* p_node : layer)
        {
            delete p_node;
        }
        layer.clear();
    }
    contact_nodes.clear();

    generate_fills();
}

void TreeSupport::draw_circles(const std::vector<std::vector<Node*>>& contact_nodes)
{
    const PrintObjectConfig &config = m_object.config();
    const coordf_t branch_radius = config.tree_support_branch_diameter.value / 2;
    const size_t wall_count = config.tree_support_wall_count.value;
    Polygon branch_circle; //Pre-generate a circle with correct diameter so that we don't have to recompute those (co)sines every time.
    for (unsigned int i = 0; i < CIRCLE_RESOLUTION; i++)
    {
        double angle = (double)i / CIRCLE_RESOLUTION * TAU;
        branch_circle.append(Point(cos(angle) * scale_(branch_radius), sin(angle) * scale_(branch_radius)));
    }

    const coordf_t circle_side_length = 2 * branch_radius * sin(M_PI / CIRCLE_RESOLUTION); //Side length of a regular polygon.
    const coordf_t layer_height = config.layer_height.value;
    const size_t bottom_interface_layers = config.support_material_interface_layers.value;
    const size_t tip_layers = branch_radius / layer_height; //The number of layers to be shrinking the circle to create a tip. This produces a 45 degree angle.
    const double diameter_angle_scale_factor = sin(config.tree_support_branch_diameter_angle.value * M_PI / 180.) * layer_height / branch_radius; //Scale factor per layer to produce the desired angle.
    const coordf_t line_width = config.support_material_extrusion_width.get_abs_value(layer_height);
    const coordf_t resolution = config.tree_support_collision_resolution.value;
    for (int layer_nr = 0; layer_nr < static_cast<int>(contact_nodes.size()); layer_nr++)
    {
        TreeSupportLayer *ts_layer = m_object.get_tree_support_layer(layer_nr);
        assert(ts_layer != nullptr);

        ExPolygons &base_areas = ts_layer->base_areas;
        ExPolygons &roof_areas = ts_layer->roof_areas;

        //Draw the support areas and add the roofs appropriately to the support roof instead of normal areas.
        for (const Node* p_node : contact_nodes[layer_nr])
        {
            const Node& node = *p_node;
            Polygon circle;
            const double scale = static_cast<double>(node.distance_to_top + 1) / tip_layers;
            for (auto iter = branch_circle.points.begin(); iter != branch_circle.points.end(); iter++)
            {
                Point corner = *iter;
                if (node.distance_to_top < tip_layers) //We're in the tip.
                {
                    corner = corner * (0.5 + scale / 2);
                }
                else
                {
                    corner = corner * (1 + static_cast<double>(node.distance_to_top - tip_layers) * diameter_angle_scale_factor);
                }
                circle.append(node.position + corner);
            }
            if (node.support_roof_layers_below > 0)
            {
                roof_areas.push_back(ExPolygon(circle));
            }
            else
            {
                base_areas.push_back(ExPolygon(circle));
            }

            ts_layer->lslices.push_back(ExPolygon(circle));
        }
        base_areas = union_ex(base_areas);
        roof_areas = union_ex(roof_areas);

        const size_t z_collision_layer = static_cast<size_t>(std::max(0, static_cast<int>(layer_nr) - static_cast<int>(bottom_interface_layers) + 1)); //Layer to test against to create a Z-distance.
        base_areas = diff_ex(base_areas, m_ts_data->get_collision(0, z_collision_layer)); //Subtract the model itself (sample 0 is with 0 diameter but proper X/Y offset).
        roof_areas = diff_ex(roof_areas, m_ts_data->get_collision(0, z_collision_layer));
        roof_areas = offset2_ex(roof_areas, scale_(branch_radius), -scale_(branch_radius));
        base_areas = diff_ex(base_areas, roof_areas);

        // TODO: simplify base_areas

#if 0
        //We smooth this support as much as possible without altering single circles. So we remove any line less than the side length of those circles.
        const double diameter_angle_scale_factor_this_layer = static_cast<double>(storage.support.supportLayers.size() - layer_nr - tip_layers) * diameter_angle_scale_factor; //Maximum scale factor.
        base_areas.simplify(circle_side_length * (1 + diameter_angle_scale_factor_this_layer), resolution); //Don't deviate more than the collision resolution so that the lines still stack properly.
#endif
        //Subtract support floors.
        ExPolygons &floor_areas = ts_layer->floor_areas;
        if (bottom_interface_layers > 0)
        {
            for(size_t layers_below = 0; layers_below <= bottom_interface_layers; layers_below++)
            {
                if (layer_nr < layers_below + bottom_interface_layers)
                    break;

                const Layer *below_layer = m_object.get_layer(layer_nr - layers_below - bottom_interface_layers);
                ExPolygons bottom_interface = std::move(intersection_ex(base_areas, below_layer->lslices));
                floor_areas.insert(floor_areas.end(), bottom_interface.begin(), bottom_interface.end());
            }

            floor_areas = std::move(union_ex(floor_areas));
            floor_areas = offset2_ex(floor_areas, scale_(branch_radius), -scale_(branch_radius));
            base_areas = diff_ex(base_areas, offset_ex(floor_areas, 10));
        }

        ts_layer->lslices = std::move(union_ex(ts_layer->lslices));
    }
}

void TreeSupport::drop_nodes(std::vector<std::vector<Node*>>& contact_nodes)
{
    const PrintObjectConfig &config = m_object.config();
    //Use Minimum Spanning Tree to connect the points on each layer and move them while dropping them down.
    const coordf_t layer_height = config.layer_height.value;
    const double angle = config.tree_support_branch_angle.value * M_PI / 180.;
    const coordf_t max_move_distance = (angle < M_PI / 2) ? (coordf_t)(tan(angle) * layer_height) : std::numeric_limits<coordf_t>::max();
    const double max_move_distance2 = max_move_distance * max_move_distance;
    const coordf_t branch_radius = config.tree_support_branch_diameter.value / 2;
    const size_t tip_layers = branch_radius / layer_height; //The number of layers to be shrinking the circle to create a tip. This produces a 45 degree angle.
    const double diameter_angle_scale_factor = sin(config.tree_support_branch_diameter_angle.value * M_PI / 180.) * layer_height / branch_radius; //Scale factor per layer to produce the desired angle.
    const coordf_t radius_sample_resolution = config.tree_support_collision_resolution.value;
    const bool support_rests_on_model = !config.support_material_buildplate_only.value;

    std::unordered_set<Node*> to_free_node_set;

    for (size_t layer_nr = contact_nodes.size() - 1; layer_nr > 0; layer_nr--) //Skip layer 0, since we can't drop down the vertices there.
    {
        auto& layer_contact_nodes = contact_nodes[layer_nr];
        std::deque<std::pair<size_t, Node*>> unsupported_branch_leaves; // All nodes that are leaves on this layer that would result in unsupported ('mid-air') branches.

        if (layer_contact_nodes.empty())
            continue;

        //Group together all nodes for each part.
        const ExPolygons& parts = m_ts_data->get_avoidance(0, layer_nr);
        std::vector<std::unordered_map<Point, Node*, PointHash>> nodes_per_part;
        nodes_per_part.emplace_back(); //All nodes that aren't inside a part get grouped together in the 0th part.
        for (size_t part_index = 0; part_index < parts.size(); part_index++)
        {
            nodes_per_part.emplace_back();
        }
        for (Node* p_node : layer_contact_nodes)
        {
            const Node& node = *p_node;

            if (!support_rests_on_model && !node.to_buildplate) //Can't rest on model and unable to reach the build plate. Then we must drop the node and leave parts unsupported.
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

                    const coordf_t branch_radius_node = [&]() -> coordf_t
                    {
                        if ((node.distance_to_top + 1) > tip_layers)
                        {
                             return branch_radius + branch_radius * (node.distance_to_top + 1) * diameter_angle_scale_factor;
                        }
                        else
                        {
                             return branch_radius * (node.distance_to_top + 1) / tip_layers;
                        }
                    }();
                    if (group_index == 0)
                    {
                        //Avoid collisions.
                        const coordf_t max_move_between_samples = max_move_distance + radius_sample_resolution + EPSILON; //100 micron extra for rounding errors.
                        move_outside_ex(m_ts_data->get_avoidance(branch_radius_node, layer_nr - 1), next_position, radius_sample_resolution + EPSILON, max_move_between_samples * max_move_between_samples); //Some extra offset to prevent rounding errors with the sample resolution.
                    }

                    Node* neighbour = nodes_per_part[group_index][neighbours[0]];
                    size_t new_distance_to_top = std::max(node.distance_to_top, neighbour->distance_to_top) + 1;
                    size_t new_support_roof_layers_below = std::max(node.support_roof_layers_below, neighbour->support_roof_layers_below) - 1;

                    const bool to_buildplate = !is_inside_ex(m_ts_data->get_avoidance(branch_radius_node, layer_nr - 1), next_position);
                    Node* next_node = new Node(next_position, new_distance_to_top, node.skin_direction, new_support_roof_layers_below, to_buildplate, p_node);
                    insert_dropped_node(contact_nodes[layer_nr - 1], next_node); //Insert the node, resolving conflicts of the two colliding nodes.

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
                if (group_index > 0 && is_inside_ex(m_ts_data->get_collision(0, layer_nr), node.position))
                {
                    const coordf_t branch_radius_node = [&]() -> coordf_t
                    {
                        if (node.distance_to_top > tip_layers)
                        {
                            return branch_radius + branch_radius * node.distance_to_top * diameter_angle_scale_factor;
                        }
                        else
                        {
                            return branch_radius * node.distance_to_top / tip_layers;
                        }
                    }();

                    Point to_outside = find_closest_ex(node.position, m_ts_data->get_collision(0, layer_nr));
                    if (vsize2_with_unscale(node.position - to_outside) >=  scale_(branch_radius_node) * scale_(branch_radius_node)) //Too far inside.
                    {
                        if (! support_rests_on_model)
                        {
                            unsupported_branch_leaves.push_front({ layer_nr, p_node });
                        }
                        continue;
                    }
                }
                Point next_layer_vertex = node.position;
                const std::vector<Point> neighbours = mst.adjacent_nodes(node.position);
                if (neighbours.size() > 1 || (neighbours.size() == 1 && vsize2_with_unscale(neighbours[0] - node.position) >= max_move_distance2)) //Only nodes that aren't about to collapse.
                {
                    //Move towards the average position of all neighbours.
                    Point sum_direction(0, 0);
                    for (const Point& neighbour : neighbours)
                    {
                        sum_direction += neighbour - node.position;
                    }
                    if(vsize2_with_unscale(sum_direction) <= max_move_distance2)
                    {
                        next_layer_vertex += sum_direction;
                    }
                    else
                    {
                        Point movement = normal(sum_direction, scale_(max_move_distance));
                        next_layer_vertex += movement;
                    }
                }

                const coordf_t branch_radius_node = [&]() -> coordf_t
                {
                    if ((node.distance_to_top + 1) > tip_layers)
                    {
                        return branch_radius + branch_radius * (node.distance_to_top + 1) * diameter_angle_scale_factor;
                    }
                    else
                    {
                        return branch_radius * (node.distance_to_top + 1) / tip_layers;
                    }
                }();

                if (group_index == 0)
                {
                    //Avoid collisions.
                    const coordf_t max_move_between_samples = max_move_distance + radius_sample_resolution + EPSILON; //100 micron extra for rounding errors.
                    double scaled_max_move_between_samples2 = max_move_between_samples * max_move_between_samples;
                    move_outside_ex(m_ts_data->get_avoidance(branch_radius_node, layer_nr - 1), next_layer_vertex, radius_sample_resolution + EPSILON, scaled_max_move_between_samples2); //Some extra offset to prevent rounding errors with the sample resolution.
                }

                const bool to_buildplate = !is_inside_ex(m_ts_data->get_avoidance(branch_radius_node, layer_nr - 1), next_layer_vertex);
                Node* next_node = new Node(next_layer_vertex, node.distance_to_top + 1, node.skin_direction, node.support_roof_layers_below - 1, to_buildplate, p_node);
                insert_dropped_node(contact_nodes[layer_nr - 1], next_node);
            }
        }

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
            pt.rotate(rotate_angle);
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
        half_overhang_distance = tan(45. * M_PI / 180.0) * layer_height / 2;
    }
    else {
        half_overhang_distance = tan((double)config.support_material_threshold.value * M_PI / 180.0) * layer_height / 2;
    }

    for (size_t layer_nr = 1; layer_nr < m_object.layers().size() - z_distance_top_layers; layer_nr++)
    {
        const ExPolygons &overhang = m_object.get_layer(layer_nr + z_distance_top_layers)->overhang_areas;
        if (overhang.empty())
            continue;

        for (const ExPolygon &overhang_part : overhang)
        {
            BoundingBox overhang_bounds = get_extents(overhang_part);
            overhang_bounds.inflated(scale_(half_overhang_distance));
            bool added = false; //Did we add a point this way?
            for (Point candidate : grid_points)
            {
                if (overhang_bounds.contains(candidate))
                {
                    constexpr coordf_t distance_inside = 0; //Move point towards the border of the polygon if it is closer than half the overhang distance: Catch points that fall between overhang areas on constant surfaces.
                    move_inside_ex(overhang_part, candidate, distance_inside, half_overhang_distance * half_overhang_distance);
                    constexpr bool border_is_inside = true;
                    if (is_inside_ex(overhang_part, candidate))
                    {
                        if (!is_inside_ex(m_ts_data->get_collision(0, layer_nr), candidate)) {
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
                Point candidate = bounding_box_middle(bounding_box);
                //move_inside_ex(overhang_part, candidate);
                constexpr size_t distance_to_top = 0;
                constexpr bool to_buildplate = true;
                Node* contact_node = new Node(candidate, distance_to_top, layer_nr % 2, support_roof_layers, to_buildplate, Node::NO_PARENT);
                contact_nodes[layer_nr].emplace_back(contact_node);
            }
        }
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
#if 0
    const PrintConfig& print_config = object.print()->config();
    const std::vector<Vec2d>& bed_points = print_config.bed_shape.values;

    for (const Vec2d& bed_point : bed_points) {
        m_machine_border.contour.append(Point(bed_point));
    }
#endif

    for (std::size_t layer_idx  = 0; layer_idx < object.layers().size(); ++layer_idx)
    {
        const Layer* layer = object.get_layer(layer_idx);
        m_layer_outlines.push_back(layer->lslices);
    }
}

const ExPolygons& TreeSupportData::get_collision(coordf_t radius, size_t layer_idx) const
{
    radius = ceil_radius(radius);
    RadiusLayerPair key{radius, layer_idx};
    const auto it = m_collision_cache.find(key);
    if (it != m_collision_cache.end())
    {
        return it->second;
    }
    else
    {
        return calculate_collision(key);
    }
}

const ExPolygons& TreeSupportData::get_avoidance(coordf_t radius, size_t layer_idx) const
{
    radius = ceil_radius(radius);
    RadiusLayerPair key{radius, layer_idx};
    const auto it = m_avoidance_cache.find(key);
    if (it != m_avoidance_cache.end())
    {
        return it->second;
    }
    else
    {
        return calculate_avoidance(key);
    }
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
    const auto& radius = key.first;
    const auto& layer_idx = key.second;

    ExPolygons collision_areas;
    ExPolygons collision_areas_simple;
    //collision_areas.push_back(m_machine_border);
    if (layer_idx < static_cast<int>(m_layer_outlines.size()))
    {
        collision_areas.insert(collision_areas.end(), m_layer_outlines[layer_idx].begin(), m_layer_outlines[layer_idx].end());
        for (ExPolygon& poly : collision_areas)
            poly.simplify(scale_(m_radius_sample_resolution), &collision_areas_simple);
    }
    collision_areas_simple = offset_ex(collision_areas_simple, scale_(m_xy_distance + radius));
    const auto ret = m_collision_cache.insert({ key, collision_areas_simple });
    return ret.first->second;
}

const ExPolygons& TreeSupportData::calculate_avoidance(const RadiusLayerPair& key) const
{
    const auto& radius = key.first;
    const auto& layer_idx = key.second;

    if (layer_idx == 0)
    {
        m_avoidance_cache[key] = get_collision(radius, 0);
        return m_avoidance_cache[key];
    }

    // Avoidance for a given layer depends on all layers beneath it so could have very deep recursion depths if
    // called at high layer heights. We can limit the reqursion depth to N by checking if the if the layer N
    // below the current one exists and if not, forcing the calculation of that layer. This may cause another recursion
    // if the layer at 2N below the current one but we won't exceed our limit unless there are N*N uncalculated layers
    // below our current one.
    constexpr auto max_recursion_depth = 100;
    // Check if we would exceed the recursion limit by trying to process this layer
    if (layer_idx >= max_recursion_depth
        && m_avoidance_cache.find({radius, layer_idx - max_recursion_depth}) == m_avoidance_cache.end())
    {
        // Force the calculation of the layer `max_recursion_depth` below our current one, ignoring the result.
        get_avoidance(radius, layer_idx - max_recursion_depth);
    }

    ExPolygons avoidance_areas = offset_ex(get_avoidance(radius, layer_idx - 1), scale_(-m_max_move));
    ExPolygons collision = std::move(get_collision(radius, layer_idx));
    avoidance_areas.insert(avoidance_areas.end(), collision.begin(), collision.end());
    avoidance_areas = std::move(union_ex(avoidance_areas));
    const auto ret = m_avoidance_cache.insert({key, std::move(avoidance_areas)});
    assert(ret.second);
    return ret.first->second;
}

} //namespace cura
