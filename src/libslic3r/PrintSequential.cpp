#include "Print.hpp"

#include "ClipperUtils.hpp"
#include "Geometry.hpp"
#include "I18N.hpp"
#include "Model.hpp"
#include "PrintConfig.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r {

std::vector<size_t> Print::layers_sorted_for_object(float start, float end, std::vector<LayerPtrs> &layers_of_objects, std::vector<BoundingBox> &boundingBox_for_objects, std::vector<Points> &objects_instances_shift)
{
    std::vector<size_t> idx_of_object_sorted;
    size_t              idx = 0;
    for (const auto &object : m_objects) {
        idx_of_object_sorted.push_back(idx++);
        object->get_certain_layers(start, end, layers_of_objects, boundingBox_for_objects);
    }
    std::sort(idx_of_object_sorted.begin(), idx_of_object_sorted.end(),
              [boundingBox_for_objects](auto left, auto right) { return boundingBox_for_objects[left].area() > boundingBox_for_objects[right].area(); });

    objects_instances_shift.clear();
    objects_instances_shift.reserve(m_objects.size());
    for (const auto &object : m_objects)
        objects_instances_shift.emplace_back(object->get_instances_shift_without_plate_offset());

    return idx_of_object_sorted;
}

StringObjectException Print::sequential_print_clearance_valid(const Print &print, Polygons *polygons, std::vector<std::pair<Polygon, float>> *height_polygons)
{
    StringObjectException single_object_exception;
    auto                  print_config = print.config();
    Pointfs               excluse_area_points = print_config.bed_exclude_area.values;
    Polygons              exclude_polys;
    Polygon               exclude_poly;
    const Vec3d           print_origin = print.get_plate_origin();
    for (int i = 0; i < excluse_area_points.size(); i++) {
        auto pt = excluse_area_points[i];
        exclude_poly.points.emplace_back(scale_(pt.x() + print_origin.x()), scale_(pt.y() + print_origin.y()));
        if (i % 4 == 3) {
            exclude_polys.push_back(exclude_poly);
            exclude_poly.points.clear();
        }
    }

    std::map<ObjectID, Polygon> map_model_object_to_convex_hull;
    struct print_instance_info
    {
        const PrintInstance *print_instance;
        BoundingBox          bounding_box;
        Polygon              hull_polygon;
        int                  object_index;
        double               arrange_score;
        double               height;
    };
    auto find_object_index = [](const Model &model, const ModelObject *obj) {
        for (int index = 0; index < model.objects.size(); index++) {
            if (model.objects[index] == obj)
                return index;
        }
        return -1;
    };
    std::vector<struct print_instance_info> print_instance_with_bounding_box;

    bool  all_objects_are_short = print.is_all_objects_are_short();
    float obj_distance = all_objects_are_short ? scale_(0.5 * MAX_OUTER_NOZZLE_RADIUS - 0.1) : scale_(0.5 * print.config().extruder_clearance_max_radius.value - 0.1);
    if (print.is_sequential_print()) {
        float skirt_extra = get_real_skirt_dist(print.config());
        obj_distance += scale_(0.5f * skirt_extra);
        obj_distance = std::max(obj_distance, static_cast<float>(scale_(skirt_extra)));
    }
    {
        Polygons convex_hulls_other;
        if (polygons != nullptr)
            polygons->clear();
        std::vector<size_t> intersecting_idxs;

        for (const PrintObject *print_object : print.objects()) {
            assert(! print_object->model_object()->instances.empty());
            assert(! print_object->instances().empty());
            ObjectID model_object_id = print_object->model_object()->id();
            auto     it_convex_hull  = map_model_object_to_convex_hull.find(model_object_id);
            ModelInstance *model_instance0 = print_object->model_object()->instances.front();
            if (it_convex_hull == map_model_object_to_convex_hull.end()) {
                Geometry::Transformation new_trans(model_instance0->get_transformation());
                new_trans.set_offset({ 0.0, 0.0, model_instance0->get_offset().z() });
                it_convex_hull = map_model_object_to_convex_hull.emplace_hint(it_convex_hull, model_object_id,
                                                                              print_object->model_object()->convex_hull_2d(new_trans.get_matrix()));
            }
            Polygon      convex_hull0 = it_convex_hull->second;
            const double z_diff       = Geometry::rotation_diff_z(model_instance0->get_rotation(), print_object->instances().front().model_instance->get_rotation());
            if (std::abs(z_diff) > EPSILON)
                convex_hull0.rotate(z_diff);
            for (const PrintInstance &instance : print_object->instances()) {
                Polygon convex_hull_no_offset = convex_hull0, convex_hull;
                auto    tmp = offset(convex_hull_no_offset, obj_distance, jtRound, scale_(0.1));
                if (! tmp.empty()) {
                    convex_hull = tmp.front();
                    convex_hull.translate(instance.shift - print_object->center_offset());
                }
                convex_hull_no_offset.translate(instance.shift - print_object->center_offset());
                if (! intersection(exclude_polys, convex_hull_no_offset).empty()) {
                    if (single_object_exception.string.empty()) {
                        single_object_exception.string = (boost::format(L("%1% is too close to exclusion area, there may be collisions when printing.")) % instance.model_instance->get_object()->name).str();
                        single_object_exception.object = instance.model_instance->get_object();
                    } else {
                        single_object_exception.string += "\n" + (boost::format(L("%1% is too close to exclusion area, there may be collisions when printing.")) % instance.model_instance->get_object()->name).str();
                        single_object_exception.object = nullptr;
                    }
                }

                for (size_t i = 0; i < convex_hulls_other.size(); ++i) {
                    if (! intersection(convex_hulls_other[i], convex_hull).empty()) {
                        bool has_exception = false;
                        if (single_object_exception.string.empty()) {
                            single_object_exception.string = (boost::format(L("%1% is too close to others, and collisions may be caused.")) % instance.model_instance->get_object()->name).str();
                            single_object_exception.object = instance.model_instance->get_object();
                            has_exception                  = true;
                        } else {
                            single_object_exception.string += "\n" + (boost::format(L("%1% is too close to others, and collisions may be caused.")) % instance.model_instance->get_object()->name).str();
                            single_object_exception.object = nullptr;
                            has_exception                  = true;
                        }

                        if (polygons) {
                            intersecting_idxs.emplace_back(i);
                            intersecting_idxs.emplace_back(convex_hulls_other.size());
                        }

                        if (has_exception)
                            break;
                    }
                }
                struct print_instance_info print_info { &instance, convex_hull.bounding_box(), convex_hull };
                print_info.height       = instance.print_object->height();
                print_info.object_index = find_object_index(print.model(), print_object->model_object());
                print_instance_with_bounding_box.push_back(std::move(print_info));
                convex_hulls_other.emplace_back(std::move(convex_hull));
            }
        }
        if (! intersecting_idxs.empty()) {
            std::sort(intersecting_idxs.begin(), intersecting_idxs.end());
            intersecting_idxs.erase(std::unique(intersecting_idxs.begin(), intersecting_idxs.end()), intersecting_idxs.end());
            for (size_t i : intersecting_idxs)
                polygons->emplace_back(std::move(convex_hulls_other[i]));
        }
    }

    double hc1              = scale_(print.config().extruder_clearance_height_to_lid);
    double hc2              = scale_(print.config().extruder_clearance_height_to_rod);
    double printable_height = scale_(print.config().printable_height);

#if 0
    auto bed_points = get_bed_shape(print_config);
    float bed_width = bed_points[1].x() - bed_points[0].x();
    float unsafe_dist = scale_(print_config.extruder_clearance_max_radius.value - print_config.extruder_clearance_radius.value);
    struct VecHash
    {
        size_t operator()(const Vec2i &n1) const
        {
            return std::hash<coord_t>()(int(n1(0) * 100 + 100)) + std::hash<coord_t>()(int(n1(1) * 100 + 100)) * 101;
        }
    };
    std::unordered_set<Vec2i, VecHash> left_right_pair;
    for (size_t i = 0; i < print_instance_with_bounding_box.size(); i++) {
        auto &inst         = print_instance_with_bounding_box[i];
        inst.index         = i;
        Point pt           = inst.bounding_box.center();
        inst.arrange_score = pt.x() / 2 + pt.y();
    }
    for (size_t i = 0; i < print_instance_with_bounding_box.size(); i++) {
        auto &inst = print_instance_with_bounding_box[i];
        auto &l    = print_instance_with_bounding_box[i];
        for (size_t j = 0; j < print_instance_with_bounding_box.size(); j++) {
            if (j != i) {
                auto &r        = print_instance_with_bounding_box[j];
                auto  ly1      = l.bounding_box.min.y();
                auto  ly2      = l.bounding_box.max.y();
                auto  ry1      = r.bounding_box.min.y();
                auto  ry2      = r.bounding_box.max.y();
                auto  lx1      = l.bounding_box.min.x();
                auto  rx1      = r.bounding_box.min.x();
                auto  lx2      = l.bounding_box.max.x();
                auto  rx2      = r.bounding_box.max.x();
                auto  inter_min = std::max(ly1, ry1);
                auto  inter_max = std::min(ly2, ry2);
                auto  inter_y   = inter_max - inter_min;

                if (inter_y > scale_(0.5 * print.config().extruder_clearance_radius.value)) {
                    if (std::max(rx1 - lx2, lx1 - rx2) < unsafe_dist) {
                        if (lx1 > rx1) {
                            left_right_pair.insert({ j, i });
                            BOOST_LOG_TRIVIAL(debug) << "in-a-row, print_instance " << r.print_instance->model_instance->get_object()->name << "(" << r.arrange_score << ")"
                                                     << " -> " << l.print_instance->model_instance->get_object()->name << "(" << l.arrange_score << ")";
                        } else {
                            left_right_pair.insert({ i, j });
                            BOOST_LOG_TRIVIAL(debug) << "in-a-row, print_instance " << l.print_instance->model_instance->get_object()->name << "(" << l.arrange_score << ")"
                                                     << " -> " << r.print_instance->model_instance->get_object()->name << "(" << r.arrange_score << ")";
                        }
                    }
                }
                if (l.height > hc1 && r.height < hc1) {
                    left_right_pair.insert({ j, i });
                    BOOST_LOG_TRIVIAL(debug) << "height>hc1, print_instance " << r.print_instance->model_instance->get_object()->name << "(" << r.arrange_score << ")"
                                             << " -> " << l.print_instance->model_instance->get_object()->name << "(" << l.arrange_score << ")";
                } else if (l.height > hc2 && l.height > r.height && l.arrange_score < r.arrange_score) {
                    if (l.arrange_score < r.arrange_score)
                        l.arrange_score = r.arrange_score + 10;
                    BOOST_LOG_TRIVIAL(debug) << "height>hc2, print_instance " << inst.print_instance->model_instance->get_object()->name
                                             << ", right=" << r.print_instance->model_instance->get_object()->name << ", l.score: " << l.arrange_score
                                             << ", r.score: " << r.arrange_score;
                }
            }
        }
    }
    for (int k = 0; k < 5; k++)
        for (auto p : left_right_pair) {
            auto &l = print_instance_with_bounding_box[p(0)];
            auto &r = print_instance_with_bounding_box[p(1)];
            if (r.arrange_score < l.arrange_score)
                r.arrange_score = l.arrange_score + 10;
        }

    BOOST_LOG_TRIVIAL(debug) << "bed width: " << unscale_(bed_width) << ", unsafe_dist:" << unscale_(unsafe_dist) << ", height_to_lid: " << unscale_(hc1) << ", height_to_rod:" << unscale_(hc2) << ", final dependency:";
    for (auto p : left_right_pair) {
        auto &l = print_instance_with_bounding_box[p(0)];
        auto &r = print_instance_with_bounding_box[p(1)];
        BOOST_LOG_TRIVIAL(debug) << "print_instance " << I18N::translate(l.print_instance->model_instance->get_object()->name) << "(" << l.arrange_score << ")"
                                 << " -> " << I18N::translate(r.print_instance->model_instance->get_object()->name) << "(" << r.arrange_score << ")";
    }
    std::sort(print_instance_with_bounding_box.begin(), print_instance_with_bounding_box.end(),
              [](print_instance_info &l, print_instance_info &r) { return l.arrange_score < r.arrange_score; });

    for (auto &inst : print_instance_with_bounding_box)
        BOOST_LOG_TRIVIAL(debug) << "after sorting print_instance " << inst.print_instance->model_instance->get_object()->name << ", score: " << inst.arrange_score
                                 << ", height:" << inst.height;
#else
    std::sort(print_instance_with_bounding_box.begin(), print_instance_with_bounding_box.end(),
              [](print_instance_info &l, print_instance_info &r) { return l.object_index < r.object_index; });

    for (auto &inst : print_instance_with_bounding_box)
        BOOST_LOG_TRIVIAL(debug) << "after sorting print_instance " << inst.print_instance->model_instance->get_object()->name << ", object_index: " << inst.object_index
                                 << ", height:" << inst.height;

#endif
    {
        int                                              print_instance_count = print_instance_with_bounding_box.size();
        std::map<const PrintInstance *, std::pair<Polygon, float>> too_tall_instances;
        for (int k = 0; k < print_instance_count; k++) {
            BoundingBox &bbox = print_instance_with_bounding_box[k].bounding_box;
            bbox.offset(scale_(print_config.extruder_clearance_dist_to_rod.value * 0.5) - obj_distance);
        }
        for (int k = 0; k < print_instance_count; k++) {
            auto inst = print_instance_with_bounding_box[k].print_instance;
            auto bbox = print_instance_with_bounding_box[k].bounding_box;
            auto iy1  = bbox.min.y();
            auto iy2  = bbox.max.y();
            (const_cast<ModelInstance *>(inst->model_instance))->arrange_order = k + 1;
            double height = (k == (print_instance_count - 1)) ? printable_height : hc1;

            for (int i = k + 1; i < print_instance_count; i++) {
                auto &p    = print_instance_with_bounding_box[i].print_instance;
                auto  bbox2 = print_instance_with_bounding_box[i].bounding_box;
                auto  py1   = bbox2.min.y();
                auto  py2   = bbox2.max.y();
                auto  inter_min = std::max(iy1, py1);
                auto  inter_max = std::min(iy2, py2);
                if (inter_max - inter_min > 0) {
                    height = hc2;
                    break;
                }
            }
            if (height < inst->print_object->max_z())
                too_tall_instances[inst] = std::make_pair(print_instance_with_bounding_box[k].hull_polygon, unscaled<double>(height));
        }

        if (too_tall_instances.size() > 0) {
            for (auto &iter : too_tall_instances) {
                if (single_object_exception.string.empty()) {
                    single_object_exception.string = (boost::format(L("%1% is too tall, and collisions will be caused.")) % iter.first->model_instance->get_object()->name).str();
                    single_object_exception.object = iter.first->model_instance->get_object();
                } else {
                    single_object_exception.string += "\n" + (boost::format(L("%1% is too tall, and collisions will be caused.")) % iter.first->model_instance->get_object()->name).str();
                    single_object_exception.object = nullptr;
                }
                if (height_polygons)
                    height_polygons->emplace_back(std::move(iter.second));
            }
        }
    }

    return single_object_exception;
}

} // namespace Slic3r
