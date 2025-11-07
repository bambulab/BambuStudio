#include "GLGizmoAlignment.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "libslic3r/Model.hpp"

namespace Slic3r {
namespace GUI {

GLGizmoAlignment::GLGizmoAlignment(GLCanvas3D& canvas) : m_canvas(canvas)
{
}

bool GLGizmoAlignment::align_objects(AlignType type)
{
    if (!validate_selection_for_align()) {
        return false;
    }

    switch (type) {
        case AlignType::CENTER_X:
            return align_to_center_x();
        case AlignType::CENTER_Y:
            return align_to_center_y();
        case AlignType::CENTER_Z:
            return align_to_center_z();
        case AlignType::Y_MAX:
            return align_to_y_max();
        case AlignType::Y_MIN:
            return align_to_y_min();
        case AlignType::X_MAX:
            return align_to_x_max();
        case AlignType::X_MIN:
            return align_to_x_min();
        case AlignType::Z_MAX:
            return align_to_z_max();
        case AlignType::Z_MIN:
            return align_to_z_min();
        default:
            return false;
    }
}

bool GLGizmoAlignment::distribute_objects(AlignType type)
{
    if (!can_distribute(type)) {
        return false;
    }

    switch (type) {
    case AlignType::DISTRIBUTE_X:
        return distribute_x();
    case AlignType::DISTRIBUTE_Y:
        return distribute_y();
    case AlignType::DISTRIBUTE_Z:
        return distribute_z();
    default:
            return false;
    }
}

bool GLGizmoAlignment::align_to_y_max()
{
    return align_objects_generic(
        [](const ObjectInfo& obj) { return obj.bbox.max.y(); },
        [](Vec3d& displacement, double target, double current_max) {
            displacement.y() = target - current_max;
        },
        "Align Y Max"
    );
}

bool GLGizmoAlignment::align_to_y_min()
{
    return align_objects_generic(
        [](const ObjectInfo& obj) { return obj.bbox.min.y(); },
        [](Vec3d& displacement, double target, double current_min) {
            displacement.y() = target - current_min;
        },
        "Align Y Min"
    );
}

bool GLGizmoAlignment::align_to_x_max()
{
    return align_objects_generic(
        [](const ObjectInfo& obj) { return obj.bbox.max.x(); },
        [](Vec3d& displacement, double target, double current_max) {
            displacement.x() = target - current_max;
        },
        "Align X Max"
    );
}

bool GLGizmoAlignment::align_to_x_min()
{
    return align_objects_generic(
        [](const ObjectInfo& obj) { return obj.bbox.min.x(); },
        [](Vec3d& displacement, double target, double current_min) {
            displacement.x() = target - current_min;
        },
        "Align X Min"
    );
}

bool GLGizmoAlignment::align_to_center_x()
{
    const Selection& selection = get_selection();
    // Handle single object selection
    /*if (selection.is_single_full_object()) {
        auto objects = get_selected_objects_info();
        if (objects.empty()) return false;

        GUI::PartPlate* curr_plate = GUI::wxGetApp().plater()->get_partplate_list().get_selected_plate();
        Vec3d plate_center = curr_plate->get_center_origin();
        double bed_center_x = plate_center.x();

        if (!take_snapshot("Align to Bed Center X")) {
            return false;
        }

        get_selection().setup_cache();

        for (const auto& obj : objects) {
            double offset_x = bed_center_x - obj.center.x();
            if (std::abs(offset_x) > 1e-6) {
                Vec3d displacement(offset_x, 0, 0);
                apply_transformation(obj.object_idx, obj.instance_idx, displacement);
            }
        }

        finish_operation("Align to Bed Center X");
        return true;
    }*/

    // Handle multiple volume selection
    if (selection.is_single_full_object() || selection.is_multiple_volume() || selection.is_multiple_modifier()) {
        if (!take_snapshot("Align Parts Center X")) {
            return false;
        }

        get_selection().setup_cache();

        const BoundingBoxf3& selection_bbox = selection.get_bounding_box();
        double selection_center_x = selection_bbox.center().x();

        // Move each selected volume to align to the center of their collective bounding box
        for (unsigned int idx : selection.get_volume_idxs()) {
            const GLVolume* volume = selection.get_volume(idx);
            if (volume) {
                double current_x = volume->transformed_convex_hull_bounding_box().center().x();
                double offset_x = selection_center_x - current_x;
                if (std::abs(offset_x) > 1e-6) {
                    Vec3d displacement(offset_x, 0, 0);
                    get_selection().translate(volume->object_idx(), volume->instance_idx(), volume->volume_idx(), displacement);
                }
            }
        }

        finish_operation("Align Parts Center X",true);
        return true;
    }

    // Handle single volume selection
    if (selection.is_single_volume() || selection.is_single_modifier()) {
        auto objects = get_selected_objects_info();
        if (objects.empty()) return false;

        const auto& obj_info = objects[0];
        double object_center_x = obj_info.center.x();

        if (!take_snapshot("Align Part to Object Center X")) {
            return false;
        }

        get_selection().setup_cache();

        // Single part: align to parent object's center X
        const GLVolume* volume = selection.get_first_volume();
        if (volume) {
            double current_x = volume->transformed_convex_hull_bounding_box().center().x();
            double offset_x = object_center_x - current_x;
            if (std::abs(offset_x) > 1e-6) {
                Vec3d displacement(offset_x, 0, 0);
                get_selection().translate(volume->object_idx(), volume->instance_idx(), volume->volume_idx(), displacement);
            }
        }

        finish_operation("Align Part to Object Center X");
        return true;
    }
    auto objects = get_selected_objects_info();
    if (objects.empty()) return false;
    // Handle multiple objects or parts selection
    if (selection.is_multiple_full_object()) {
        BoundingBoxf3 selection_bbox = selection.get_bounding_box();
        double selection_center_x = selection_bbox.center().x();

        if (!take_snapshot("Align Objects Center X")) {
            return false;
        }

        get_selection().setup_cache();

        // Multiple objects: use selection bounding box for alignment
        for (const auto& obj : objects) {
            double offset_x = selection_center_x - obj.center.x();
            if (std::abs(offset_x) > 1e-6) {
                Vec3d displacement(offset_x, 0, 0);
                apply_transformation(obj.object_idx, obj.instance_idx, displacement);
            }
        }

        finish_operation("Align Objects Center X");
        return true;
    }

    if (selection.is_multiple_volume() || selection.is_multiple_modifier()) {
        BoundingBoxf3 selection_bbox = selection.get_bounding_box();
        double selection_center_x = selection_bbox.center().x();

        if (!take_snapshot("Align Parts Center X")) {
            return false;
        }

        get_selection().setup_cache();

        // Multiple parts: use selection bounding box for alignment
        for (unsigned int idx : selection.get_volume_idxs()) {
            const GLVolume* volume = selection.get_volume(idx);
            if (volume) {
                double current_x = volume->transformed_convex_hull_bounding_box().center().x();
                double offset_x = selection_center_x - current_x;
                if (std::abs(offset_x) > 1e-6) {
                    Vec3d displacement(offset_x, 0, 0);
                    get_selection().translate(volume->object_idx(), volume->instance_idx(), volume->volume_idx(), displacement);
                }
            }
        }

        finish_operation("Align Parts Center X");
        return true;
    }
    return false;
}

bool GLGizmoAlignment::align_to_center_y()
{
    const Selection& selection = get_selection();

    /*if (selection.is_single_full_object()) {
        auto objects = get_selected_objects_info();
        if (objects.empty()) return false;

        if (!take_snapshot("Align Object to Bed Center Y")) {
            return false;
        }

        get_selection().setup_cache();

        GUI::PartPlate* curr_plate = GUI::wxGetApp().plater()->get_partplate_list().get_selected_plate();
        Vec3d plate_center = curr_plate->get_center_origin();
        double bed_center_y = plate_center.y();

        for (const auto& obj : objects) {
            double current_y = obj.bbox.center().y();
            Vec3d displacement = Vec3d::Zero();
            displacement.y() = bed_center_y - current_y;

            if (std::abs(displacement.y()) > 1e-6) {
                apply_transformation(obj.object_idx, obj.instance_idx, displacement);
            }
        }

        finish_operation("Align Object to Bed Center Y");
        return true;
    }*/

    if (selection.is_single_full_object() || selection.is_single_volume() || selection.is_single_modifier()) {
        auto objects = get_selected_objects_info();
        if (objects.empty()) return false;

        const auto& obj_info = objects[0];
        double object_center_y = obj_info.center.y();

        if (!take_snapshot("Align Part to Object Center Y")) {
            return false;
        }

        get_selection().setup_cache();

        const GLVolume* volume = selection.get_first_volume();
        if (volume) {
            double current_y = volume->transformed_convex_hull_bounding_box().center().y();
            double offset_y = object_center_y - current_y;
            if (std::abs(offset_y) > 1e-6) {
                Vec3d displacement(0, offset_y, 0);
                get_selection().translate(volume->object_idx(), volume->instance_idx(), volume->volume_idx(), displacement);
            }
        }

        finish_operation("Align Part to Object Center Y");
        return true;
    }

    auto objects = get_selected_objects_info();
    if (objects.empty()) return false;

    if (selection.is_multiple_full_object()) {
        BoundingBoxf3 selection_bbox = selection.get_bounding_box();
        double selection_center_y = selection_bbox.center().y();

        if (!take_snapshot("Align Objects Center Y")) {
            return false;
        }

        get_selection().setup_cache();

        for (const auto& obj : objects) {
            double offset_y = selection_center_y - obj.center.y();
            if (std::abs(offset_y) > 1e-6) {
                Vec3d displacement(0, offset_y, 0);
                apply_transformation(obj.object_idx, obj.instance_idx, displacement);
            }
        }

        finish_operation("Align Objects Center Y");
        return true;
    }

    if (selection.is_multiple_volume() || selection.is_multiple_modifier()) {
        BoundingBoxf3 selection_bbox = selection.get_bounding_box();
        double selection_center_y = selection_bbox.center().y();

        if (!take_snapshot("Align Parts Center Y")) {
            return false;
        }

        get_selection().setup_cache();

        for (unsigned int idx : selection.get_volume_idxs()) {
            const GLVolume* volume = selection.get_volume(idx);
            if (volume) {
                double current_y = volume->transformed_convex_hull_bounding_box().center().y();
                double offset_y = selection_center_y - current_y;
                if (std::abs(offset_y) > 1e-6) {
                    Vec3d displacement(0, offset_y, 0);
                    get_selection().translate(volume->object_idx(), volume->instance_idx(), volume->volume_idx(), displacement);
                }
            }
        }

        finish_operation("Align Parts Center Y");
        return true;
    }
    return false;
}

bool GLGizmoAlignment::align_to_center_z()
{
    const Selection& selection = get_selection();

    // Single part: align to parent object's center Z
    if (selection.is_single_volume() || selection.is_single_modifier()) {
        auto objects = get_selected_objects_info();
        if (objects.empty()) return false;

        const auto& obj_info = objects[0];
        double object_center_z = obj_info.center.z();

        if (!take_snapshot("Align Part to Object Center Z")) {
            return false;
        }
        get_selection().setup_cache();

        const GLVolume* volume = selection.get_first_volume();
        if (volume) {
            double current_z = volume->transformed_convex_hull_bounding_box().center().z();
            double offset_z = object_center_z - current_z;
            if (std::abs(offset_z) > 1e-6) {
                Vec3d displacement(0, 0, offset_z);
                get_selection().translate(volume->object_idx(), volume->instance_idx(), volume->volume_idx(), displacement);
            }
        }

        finish_operation("Align Part to Object Center Z");
        return true;
    }

    // Handle multiple parts selection (only parts support Z alignment)
    if (selection.is_single_full_object() || selection.is_multiple_volume() || selection.is_multiple_modifier()) {
        BoundingBoxf3 selection_bbox = selection.get_bounding_box();
        double selection_center_z = selection_bbox.center().z();

        if (!take_snapshot("Align Parts Center Z")) {
            return false;
        }

        get_selection().setup_cache();

        for (unsigned int idx : selection.get_volume_idxs()) {
            const GLVolume* volume = selection.get_volume(idx);
            if (volume) {
                double current_z = volume->transformed_convex_hull_bounding_box().center().z();
                double offset_z = selection_center_z - current_z;
                if (std::abs(offset_z) > 1e-6) {
                    Vec3d displacement(0, 0, offset_z);
                    get_selection().translate(volume->object_idx(), volume->instance_idx(), volume->volume_idx(), displacement);
                }
            }
        }

        finish_operation("Align Parts Center Z");
        return true;
    }
    return false;
}

bool GLGizmoAlignment::align_to_z_max()
{
    return align_objects_generic(
        [](const ObjectInfo& obj) { return obj.bbox.max.z(); },
        [](Vec3d& displacement, double target, double current_max) {
            displacement.z() = target - current_max;
        },
        "Align Z Max"
    );
}

bool GLGizmoAlignment::align_to_z_min()
{
    return align_objects_generic(
        [](const ObjectInfo& obj) { return obj.bbox.min.z(); },
        [](Vec3d& displacement, double target, double current_min) {
            displacement.z() = target - current_min;
        },
        "Align Z Min"
    );
}

bool GLGizmoAlignment::distribute_y()
{
    return distribute_objects_generic(
        [](const ObjectInfo& obj) { return obj.center.y(); },
        1,
        "Distribute Y"
    );
}

bool GLGizmoAlignment::distribute_x()
{
    return distribute_objects_generic(
        [](const ObjectInfo& obj) { return obj.center.x(); },
        0,
        "Distribute X"
    );
}

bool GLGizmoAlignment::distribute_z()
{
    return distribute_objects_generic(
        [](const ObjectInfo& obj) { return obj.center.z(); },
        2,
        "Distribute Z"
    );
}

template<typename GetCoordFunc, typename SetCoordFunc>
bool GLGizmoAlignment::align_objects_generic(GetCoordFunc get_coord, SetCoordFunc set_coord,
                                           const std::string& operation_name)
{
    const Selection& selection = get_selection();

    // Handle parts selection differently
    if (selection.is_single_full_object() || selection.is_multiple_volume() || selection.is_multiple_modifier()) {
        if (!take_snapshot(operation_name)) {
            return false;
        }

        get_selection().setup_cache();

        // Get all selected volumes
        std::vector<const GLVolume*> volumes;
        for (unsigned int idx : selection.get_volume_idxs()) {
            const GLVolume* volume = selection.get_volume(idx);
            if (volume) {
                volumes.push_back(volume);
            }
        }

        if (volumes.empty()) return false;

        // Find reference coordinate
        double reference_coord = 0.0;
        bool first = true;

        for (const GLVolume* volume : volumes) {
            BoundingBoxf3 bbox = volume->transformed_convex_hull_bounding_box();
            double coord = 0.0;

            if (operation_name.find("X Max") != std::string::npos) {
                coord = bbox.max.x();
            } else if (operation_name.find("X Min") != std::string::npos) {
                coord = bbox.min.x();
            } else if (operation_name.find("Y Max") != std::string::npos) {
                coord = bbox.max.y();
            } else if (operation_name.find("Y Min") != std::string::npos) {
                coord = bbox.min.y();
            } else if (operation_name.find("Z Max") != std::string::npos) {
                coord = bbox.max.z();
            } else if (operation_name.find("Z Min") != std::string::npos) {
                coord = bbox.min.z();
            }

            if (first) {
                reference_coord = coord;
                first = false;
            } else {
                if (operation_name.find("Max") != std::string::npos) {
                    reference_coord = std::max(reference_coord, coord);
                } else if (operation_name.find("Min") != std::string::npos) {
                    reference_coord = std::min(reference_coord, coord);
                }
            }
        }

        for (const GLVolume* volume : volumes) {
            BoundingBoxf3 bbox = volume->transformed_convex_hull_bounding_box();
            double current_coord = 0.0;

            if (operation_name.find("X Max") != std::string::npos) {
                current_coord = bbox.max.x();
            } else if (operation_name.find("X Min") != std::string::npos) {
                current_coord = bbox.min.x();
            } else if (operation_name.find("Y Max") != std::string::npos) {
                current_coord = bbox.max.y();
            } else if (operation_name.find("Y Min") != std::string::npos) {
                current_coord = bbox.min.y();
            } else if (operation_name.find("Z Max") != std::string::npos) {
                current_coord = bbox.max.z();
            } else if (operation_name.find("Z Min") != std::string::npos) {
                current_coord = bbox.min.z();
            }

            Vec3d displacement = Vec3d::Zero();

            if (operation_name.find("X") != std::string::npos) {
                displacement.x() = reference_coord - current_coord;
            } else if (operation_name.find("Y") != std::string::npos) {
                displacement.y() = reference_coord - current_coord;
            } else if (operation_name.find("Z") != std::string::npos) {
                displacement.z() = reference_coord - current_coord;
            }

            if (displacement.norm() > 1e-6) {
                get_selection().translate(volume->object_idx(), volume->instance_idx(), volume->volume_idx(), displacement);
            }
        }

        finish_operation(operation_name,true);
        return true;
    }

    // Handle objects selection (original logic)
    auto objects = get_selected_objects_info();
    if (objects.empty()) return false;

    if (!take_snapshot(operation_name)) {
        return false;
    }

    get_selection().setup_cache();

    double reference_coord = get_coord(objects[0]);
    for (const auto& obj : objects) {
        double coord = get_coord(obj);
        if (operation_name.find("Max") != std::string::npos) {
            reference_coord = std::max(reference_coord, coord);
        } else if (operation_name.find("Min") != std::string::npos) {
            reference_coord = std::min(reference_coord, coord);
        }
    }

    for (const auto& obj : objects) {
        double current_coord = get_coord(obj);
        Vec3d displacement = Vec3d::Zero();
        set_coord(displacement, reference_coord, current_coord);

        if (displacement.norm() > 1e-6) {
            apply_transformation(obj.object_idx, obj.instance_idx, displacement);
        }
    }

    finish_operation(operation_name);
    return true;
}

template<typename GetCoordFunc>
bool GLGizmoAlignment::distribute_objects_generic(GetCoordFunc get_coord, int axis, const std::string& operation_name)
{
    const Selection& selection = get_selection();

    if (selection.is_single_full_object() || selection.is_multiple_volume() || selection.is_multiple_modifier()) {
        std::vector<const GLVolume*> volumes;
        for (unsigned int idx : selection.get_volume_idxs()) {
            const GLVolume* v = selection.get_volume(idx);
            if (v) volumes.push_back(v);
        }

        if (volumes.size() < 3) return false;
        if (!take_snapshot(operation_name)) {
            return false;
        }
        get_selection().setup_cache();

        struct VolEntry { const GLVolume* v; double coord; };
        std::vector<VolEntry> entries;
        entries.reserve(volumes.size());
        for (const GLVolume* v : volumes) {
            BoundingBoxf3 bbox = v->transformed_convex_hull_bounding_box();
            Vec3d c = bbox.center();
            double coord = (axis == 0 ? c.x() : axis == 1 ? c.y() : c.z());
            entries.push_back({v, coord});
        }

        std::sort(entries.begin(), entries.end(), [](const VolEntry& a, const VolEntry& b){ return a.coord < b.coord; });

        BoundingBoxf3 selection_bbox = selection.get_bounding_box();
        BoundingBoxf3 left_bbox      = entries.front().v->transformed_convex_hull_bounding_box();
        BoundingBoxf3 right_bbox     = entries.back().v->transformed_convex_hull_bounding_box();
        auto   axis_extent = [&](const BoundingBoxf3 &bb) { return (axis == 0 ? (bb.max.x() - bb.min.x()) : axis == 1 ? (bb.max.y() - bb.min.y()) : (bb.max.z() - bb.min.z())); };
        double left_width  = axis_extent(left_bbox);
        double right_width = axis_extent(right_bbox);
        double bbox_min    = (axis == 0 ? selection_bbox.min.x() : axis == 1 ? selection_bbox.min.y() : selection_bbox.min.z());
        double bbox_max    = (axis == 0 ? selection_bbox.max.x() : axis == 1 ? selection_bbox.max.y() : selection_bbox.max.z());
        double min_center  = bbox_min + left_width * 0.5;
        double max_center  = bbox_max - right_width * 0.5;
        double total_distance = max_center - min_center;
        double interval       = (entries.size() > 1) ? total_distance / (entries.size() - 1) : 0;
        for (size_t i = 1; i < entries.size() - 1; ++i) {
            double target_coord  = min_center + i * interval;
            double current_coord = entries[i].coord;
            double offset        = target_coord - current_coord;

            if (std::abs(offset) > 1e-6) {
                Vec3d displacement = Vec3d::Zero();
                displacement[axis] = offset;
                const GLVolume *v  = entries[i].v;
                get_selection().translate(v->object_idx(), v->instance_idx(), v->volume_idx(), displacement);
            }
        }

        finish_operation(operation_name,true);
        return true;
    }

    auto objects = get_selected_objects_info();
    if (objects.size() < 3) return false;

    if (!take_snapshot(operation_name)) {
        return false;
    }

    get_selection().setup_cache();

    std::sort(objects.begin(), objects.end(),
        [&get_coord](const ObjectInfo& a, const ObjectInfo& b) {
            return get_coord(a) < get_coord(b);
        });

    double min_coord = get_coord(objects.front());
    double max_coord = get_coord(objects.back());
    double total_distance = max_coord - min_coord;
    double interval = (objects.size() > 1) ? total_distance / (objects.size() - 1) : 0;
    for (size_t i = 1; i < objects.size() - 1; ++i) {
        double target_coord = min_coord + i * interval;
        double current_coord = get_coord(objects[i]);
        double offset = target_coord - current_coord;

        if (std::abs(offset) > 1e-6) {
            Vec3d displacement = Vec3d::Zero();
            displacement[axis] = offset;
            apply_transformation(objects[i].object_idx, objects[i].instance_idx, displacement);
        }
    }

    finish_operation(operation_name);
    return true;
}

bool GLGizmoAlignment::can_align(AlignType type) const
{
    const Selection& selection = get_selection();

    bool is_single_object = selection.is_single_full_object();
    bool is_multiple_objects = selection.is_multiple_full_object();
    bool is_single_part = selection.is_single_volume() || selection.is_single_modifier();
    bool is_multiple_parts = selection.is_multiple_volume() || selection.is_multiple_modifier();


    return validate_selection_for_align();
}

bool GLGizmoAlignment::can_distribute(AlignType type) const
{
    const Selection& selection = get_selection();
    if (selection.is_single_full_object()) {
        size_t count = selection.get_volume_idxs().size();
        if (count >= 3)
            return true;
    }
    if (selection.is_single_volume() ||
        selection.is_single_modifier()) {
        return false;
    }

    if (selection.is_multiple_volume() || selection.is_multiple_modifier()) {
        size_t count = selection.get_volume_idxs().size();
        if (count < 3) return false;
        return (type == AlignType::DISTRIBUTE_X ||
                type == AlignType::DISTRIBUTE_Y ||
                type == AlignType::DISTRIBUTE_Z);
    }

    if (selection.is_multiple_full_object()) {
        auto objects = get_selected_objects_info();
        if (objects.size() < 3) return false;
        return (type == AlignType::DISTRIBUTE_X || type == AlignType::DISTRIBUTE_Y || type == AlignType::DISTRIBUTE_Z);
    }

    return validate_selection_for_distribute();
}

std::vector<GLGizmoAlignment::ObjectInfo> GLGizmoAlignment::get_selected_objects_info() const
{
    std::vector<ObjectInfo> objects;
    Selection& selection = get_selection();

    if (selection.is_empty()) return objects;

    const auto& content = selection.get_content();
    for (const auto& obj_it : content) {
        for (const auto& inst_idx : obj_it.second) {
            int obj_idx = obj_it.first;

            if (obj_idx >= 0 && obj_idx < selection.get_model()->objects.size()) {
                ModelObject* object = selection.get_model()->objects[obj_idx];
                if (inst_idx >= 0 && inst_idx < object->instances.size()) {
                    BoundingBoxf3 bbox = object->instance_bounding_box(inst_idx);
                    objects.emplace_back(obj_idx, inst_idx, bbox);
                }
            }
        }
    }

    return objects;
}

Selection& GLGizmoAlignment::get_selection() const
{
    return m_canvas.get_selection();
}

bool GLGizmoAlignment::take_snapshot(const std::string& name)
{
    wxGetApp().plater()->take_snapshot(name);
    return true;
}

void GLGizmoAlignment::apply_transformation(int obj_idx, int inst_idx, const Vec3d& displacement)
{
    get_selection().translate(obj_idx, inst_idx, displacement);
}

void GLGizmoAlignment::finish_operation(const std::string &operation_name, bool force_volume_move)
{
    get_selection().notify_instance_update(-1, -1);
    m_canvas.do_move(operation_name, force_volume_move);
}

bool GLGizmoAlignment::validate_selection_for_align() const
{
    const Selection& selection = get_selection();

    if (selection.is_single_full_object() ||
        selection.is_single_volume() ||
        selection.is_single_modifier()) {
        return true;
    }

    return selection.get_volume_idxs().size() >= 2;
}

bool GLGizmoAlignment::validate_selection_for_distribute() const
{
    const Selection& selection = get_selection();

    if (selection.is_single_full_object() || selection.is_multiple_full_object()) {
        std::set<int> object_indices;
        for (int volume_idx : selection.get_volume_idxs()) {
            const GLVolume* volume = selection.get_volume(volume_idx);
            if (volume) {
                object_indices.insert(volume->object_idx());
            }
        }
        return object_indices.size() >= 3;
    }

    return selection.get_volume_idxs().size() >= 3;
}

} // namespace GUI
} // namespace Slic3r