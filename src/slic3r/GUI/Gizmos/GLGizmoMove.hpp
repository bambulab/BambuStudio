#ifndef slic3r_GLGizmoMove_hpp_
#define slic3r_GLGizmoMove_hpp_

#include "GLGizmoBase.hpp"
//BBS: add size adjust related
#include "GizmoObjectManipulation.hpp"


namespace Slic3r {
namespace GUI {

class GLGizmoMove : public GLGizmoBase
{
public:
    enum Axis : unsigned char
    {
        X,
        Y,
        Z
    };

    static Vec3d m_displacement;

private:
    Axis m_axis;

    static const double Offset;

    double m_snap_step;

    Vec3d m_starting_drag_position;
    Vec3d m_starting_box_center;
    Vec3d m_starting_box_bottom_center;

    GLModel m_vbo_cone;

public:
    GLGizmoMove(GLCanvas3D& parent, Axis axis);
    GLGizmoMove(const GLGizmoMove& other);
    virtual ~GLGizmoMove() = default;

    std::string get_tooltip() const override;

    void render_grabbers_for_picking(const BoundingBoxf3& box) const;

protected:
    bool on_init() override;
    std::string on_get_name() const override { return ""; }
    void on_start_dragging() override;
    void on_update(const UpdateData& data) override;
    void on_render() override;
    void on_render_for_picking() override;
    void render_grabber_extension(Axis axis, const BoundingBoxf3& box, bool picking) const;

private:
    double calc_projection(const UpdateData& data) const;
};

//BBS: GUI refactor: add object manipulation
class GizmoObjectManipulation;
class GLGizmoMove3D : public GLGizmoBase
{
    static const double Offset;

    Vec3d m_displacement;

    double m_snap_step;

    Vec3d m_starting_drag_position;
    Vec3d m_starting_box_center;
    Vec3d m_starting_box_bottom_center;

    GLModel m_vbo_cone;

    //BBS: add size adjust related
    GizmoObjectManipulation* m_object_manipulation;

public:
    //BBS: add obj manipulation logic
    //GLGizmoMove3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    GLGizmoMove3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id, GizmoObjectManipulation* obj_manipulation);
    virtual ~GLGizmoMove3D() = default;

    double get_snap_step(double step) const { return m_snap_step; }
    void set_snap_step(double step) { m_snap_step = step; }

    const Vec3d& get_displacement() const { return m_displacement; }

    std::string get_tooltip() const override;

protected:
    virtual bool on_init() override;
    virtual std::string on_get_name() const override;
    virtual bool on_is_activable() const override;
    virtual void on_start_dragging() override;
    virtual void on_stop_dragging() override;
    virtual void on_update(const UpdateData& data) override;
    virtual void on_render() override;
    virtual void on_render_for_picking() override;
    //BBS: GUI refactor: add object manipulation
    virtual void on_render_input_window(float x, float y, float bottom_limit);

private:
    double calc_projection(const UpdateData& data) const;
    void render_grabber_extension(Axis axis, const BoundingBoxf3& box, bool picking) const;
};



} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoMove_hpp_
