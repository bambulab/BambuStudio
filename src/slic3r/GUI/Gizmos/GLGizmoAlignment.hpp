#ifndef slic3r_GLGizmoAlignment_hpp_
#define slic3r_GLGizmoAlignment_hpp_

#include "libslic3r/Point.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "slic3r/GUI/Selection.hpp"
#include <vector>
#include <functional>

namespace Slic3r {
namespace GUI {

class GLCanvas3D;

class GLGizmoAlignment
{
public:
    enum class AlignType {
        NONE = -1,
        CENTER_X,
        CENTER_Y,
        CENTER_Z,
        Y_MAX,
        Y_MIN,
        X_MAX,
        X_MIN,
        Z_MAX,
        Z_MIN,
        DISTRIBUTE_X,
        DISTRIBUTE_Y,
        DISTRIBUTE_Z
    };
    struct ObjectInfo {
        int object_idx;
        int instance_idx;
        BoundingBoxf3 bbox;
        Vec3d center;

        ObjectInfo(int obj_idx, int inst_idx, const BoundingBoxf3& bb)
            : object_idx(obj_idx), instance_idx(inst_idx), bbox(bb), center(bb.center()) {}
    };

    explicit GLGizmoAlignment(GLCanvas3D& canvas);
    ~GLGizmoAlignment() = default;

    bool align_objects(AlignType type);
    bool distribute_objects(AlignType type);

    bool align_to_center_x();
    bool align_to_center_y();
    bool align_to_center_z();
    bool align_to_y_max();
    bool align_to_y_min();
    bool align_to_x_max();
    bool align_to_x_min();
    bool align_to_z_max();
    bool align_to_z_min();

    bool distribute_x();
    bool distribute_y();
    bool distribute_z();

    bool can_align(AlignType type) const;
    bool can_distribute(AlignType type) const;

    std::vector<ObjectInfo> get_selected_objects_info() const;

private:
    GLCanvas3D& m_canvas;

    template<typename GetCoordFunc, typename SetCoordFunc>
    bool align_objects_generic(GetCoordFunc get_coord, SetCoordFunc set_coord,
                             const std::string& operation_name);

    template<typename GetCoordFunc>
    bool distribute_objects_generic(GetCoordFunc get_coord, int axis,
                                  const std::string& operation_name);

    Selection& get_selection() const;
    bool take_snapshot(const std::string& name);
    void apply_transformation(int obj_idx, int inst_idx, const Vec3d& displacement);
    void       finish_operation(const std::string &operation_name, bool force_volume_move = false);

    bool validate_selection_for_align() const;
    bool validate_selection_for_distribute() const;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoAlignment_hpp_