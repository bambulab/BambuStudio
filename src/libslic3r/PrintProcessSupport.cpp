#include "Print.hpp"

#include "ClipperUtils.hpp"
#include "Extruder.hpp"
#include "Geometry/ConvexHull.hpp"
#include "Layer.hpp"

namespace Slic3r {

float Print::min_skirt_length = 0;

void Print::_make_skirt()
{
    coordf_t skirt_height_z = 0.;
    for (const PrintObject *object : m_objects) {
        size_t skirt_layers = this->has_infinite_skirt() ? object->layer_count() : std::min(size_t(m_config.skirt_height.value), object->layer_count());
        skirt_height_z      = std::max(skirt_height_z, object->m_layers[skirt_layers - 1]->print_z);
    }

    Points                           points;
    std::map<PrintObject *, Polygon> object_convex_hulls;
    for (PrintObject *object : m_objects) {
        Points object_points;
        for (const Layer *layer : object->m_layers) {
            if (layer->print_z > skirt_height_z)
                break;
            for (const ExPolygon &expoly : layer->lslices)
                append(object_points, expoly.contour.points);
        }
        for (const SupportLayer *layer : object->support_layers()) {
            if (layer->print_z > skirt_height_z)
                break;
            layer->support_fills.collect_points(object_points);
        }

        object_convex_hulls.insert({ object, Slic3r::Geometry::convex_hull(object_points) });

        for (const PrintInstance &instance : object->instances()) {
            Points copy_points = object_points;
            for (Point &pt : copy_points)
                pt += instance.shift;
            append(points, copy_points);
        }
    }

    append(points, this->first_layer_wipe_tower_corners());
    if (config().draft_shield == dsDisabled)
        append(points, m_first_layer_convex_hull.points);
    if (points.size() < 3)
        return;

    this->throw_if_canceled();
    Polygon convex_hull = Slic3r::Geometry::convex_hull(points);

    double initial_layer_print_height = this->skirt_first_layer_height();
    Flow   flow                       = this->skirt_flow();
    float  spacing                    = flow.spacing();
    double mm3_per_mm                 = flow.mm3_per_mm();

    std::vector<size_t> extruders;
    std::vector<double> extruders_e_per_mm;
    {
        auto set_extruders = this->extruders();
        extruders.reserve(set_extruders.size());
        extruders_e_per_mm.reserve(set_extruders.size());
        for (auto &extruder_id : set_extruders) {
            extruders.push_back(extruder_id);
            extruders_e_per_mm.push_back(Extruder((unsigned int) extruder_id, &m_config, m_config.single_extruder_multi_material).e_per_mm(mm3_per_mm));
        }
    }

    size_t n_skirts = m_config.skirt_loops.value;
    if (this->has_infinite_skirt() && n_skirts == 0)
        n_skirts = 1;

    auto distance = float(scale_(m_config.skirt_distance.value - spacing / 2.f));
    std::vector<coordf_t> extruded_length(extruders.size(), 0.);
    for (size_t i = n_skirts, extruder_idx = 0; i > 0; -- i) {
        this->throw_if_canceled();
        distance += float(scale_(spacing));
        Polygon loop;
        {
            Polygons loops = offset(convex_hull, distance, ClipperLib::jtRound, float(scale_(0.1)));
            Geometry::simplify_polygons(loops, scale_(0.05), &loops);
            if (loops.empty())
                break;
            loop = loops.front();
        }

        ExtrusionLoop eloop(elrSkirt);
        eloop.paths.emplace_back(ExtrusionPath(ExtrusionPath(erSkirt, (float) mm3_per_mm, flow.width(), (float) initial_layer_print_height)));
        eloop.paths.back().polyline = loop.split_at_first_point();
        m_skirt.append(eloop);
        if (Print::min_skirt_length > 0) {
            extruded_length[extruder_idx] += unscale<double>(loop.length()) * extruders_e_per_mm[extruder_idx];
            if (extruded_length[extruder_idx] < Print::min_skirt_length) {
                if (i == 1)
                    ++ i;
            } else if (extruder_idx + 1 < extruders.size()) {
                ++ extruder_idx;
            }
        }
    }
    m_skirt.reverse();

    for (Polygon &poly : offset(convex_hull, distance + 0.5f * float(scale_(spacing)), ClipperLib::jtRound, float(scale_(0.1))))
        append(m_skirt_convex_hull, std::move(poly.points));

    const bool   by_object                  = is_sequential_print() && m_config.skirt_per_object.value;
    const size_t n_object_skirts            = by_object ? std::max(size_t(1), size_t(m_config.skirt_loops.value)) : 1;
    const float  object_skirt_initial_offset = by_object ? float(scale_(m_config.skirt_distance.value - spacing / 2.f)) : float(scale_(1.0));
    for (auto obj_cvx_hull : object_convex_hulls) {
        PrintObject *object       = obj_cvx_hull.first;
        float        obj_distance = object_skirt_initial_offset;
        for (size_t i = 0; i < n_object_skirts; i++) {
            if (by_object || i > 0)
                obj_distance += float(scale_(spacing));
            Polygon loop;
            {
                Polygons loops = offset(obj_cvx_hull.second, obj_distance, ClipperLib::jtRound, float(scale_(0.1)));
                Geometry::simplify_polygons(loops, scale_(0.05), &loops);
                if (loops.empty())
                    break;
                loop = loops.front();
            }

            ExtrusionLoop eloop(elrSkirt);
            eloop.paths.emplace_back(ExtrusionPath(ExtrusionPath(erSkirt, (float) mm3_per_mm, flow.width(), (float) initial_layer_print_height)));
            eloop.paths.back().polyline = loop.split_at_first_point();
            object->m_skirt.append(std::move(eloop));
        }
    }
}

Polygons Print::first_layer_islands() const
{
    Polygons islands;
    for (PrintObject *object : m_objects) {
        Polygons object_islands;
        for (ExPolygon &expoly : object->m_layers.front()->lslices)
            object_islands.push_back(expoly.contour);
        if (! object->support_layers().empty()) {
            ExPolygons &expolys_first_layer = object->m_support_layers.front()->support_islands;
            for (ExPolygon &expoly : expolys_first_layer)
                object_islands.push_back(expoly.contour);
        }
        islands.reserve(islands.size() + object_islands.size() * object->instances().size());
        for (const PrintInstance &instance : object->instances())
            for (Polygon &poly : object_islands) {
                islands.push_back(poly);
                islands.back().translate(instance.shift);
            }
    }
    return islands;
}

std::vector<Point> Print::first_layer_wipe_tower_corners(bool check_wipe_tower_existance) const
{
    std::vector<Point> corners;
    if (check_wipe_tower_existance && (! has_wipe_tower() || m_wipe_tower_data.tool_changes.empty()))
        return corners;
    {
        double width = m_wipe_tower_data.bbx.max.x() - m_wipe_tower_data.bbx.min.x();
        double depth = m_wipe_tower_data.bbx.max.y() - m_wipe_tower_data.bbx.min.y();
        Vec2d  pt0   = m_wipe_tower_data.bbx.min + m_wipe_tower_data.rib_offset.cast<double>();
        for (Vec2d pt : { pt0, Vec2d(pt0.x() + width, pt0.y()), Vec2d(pt0.x() + width, pt0.y() + depth), Vec2d(pt0.x(), pt0.y() + depth) }) {
            pt = Eigen::Rotation2Dd(Geometry::deg2rad(m_config.wipe_tower_rotation_angle.value)) * pt;
            pt += Vec2d(m_config.wipe_tower_x.get_at(m_plate_index) + m_origin(0), m_config.wipe_tower_y.get_at(m_plate_index) + m_origin(1));
            corners.emplace_back(Point(scale_(pt.x()), scale_(pt.y())));
        }
    }
    return corners;
}

void Print::finalize_first_layer_convex_hull()
{
    append(m_first_layer_convex_hull.points, m_skirt_convex_hull);
    if (m_first_layer_convex_hull.empty()) {
        for (Polygon &poly : this->first_layer_islands())
            append(m_first_layer_convex_hull.points, std::move(poly.points));
    }
    append(m_first_layer_convex_hull.points, this->first_layer_wipe_tower_corners());
    m_first_layer_convex_hull = Geometry::convex_hull(m_first_layer_convex_hull.points);
}

} // namespace Slic3r
