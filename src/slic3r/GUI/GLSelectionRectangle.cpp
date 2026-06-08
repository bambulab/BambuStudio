#include "GLSelectionRectangle.hpp"
#include "Camera.hpp"
#include "3DScene.hpp"
#include "GLCanvas3D.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "OpenGLManager.hpp"
#include <igl/project.h>

#include <GL/glew.h>

namespace Slic3r {
namespace GUI {

    void GLSelectionRectangle::start_dragging(const Vec2d& mouse_position, EState state)
    {
        if (is_dragging() || (state == Off))
            return;

        m_state = state;
        m_start_corner = mouse_position;
        m_end_corner = mouse_position;
    }

    void GLSelectionRectangle::dragging(const Vec2d& mouse_position)
    {
        if (!is_dragging())
            return;

        m_end_corner = mouse_position;
    }

    std::vector<unsigned int> GLSelectionRectangle::stop_dragging(const GLCanvas3D& canvas, const std::vector<Vec3d>& points)
    {
        std::vector<unsigned int> out;

        if (!is_dragging())
            return out;

        m_state = Off;

        const Camera& camera = wxGetApp().plater()->get_camera();
        Matrix4d modelview = camera.get_view_matrix().matrix();
        Matrix4d projection= camera.get_projection_matrix().matrix();
        Vec4i viewport(camera.get_viewport().data());

        // Convert our std::vector to Eigen dynamic matrix.
        Eigen::Matrix<double, Eigen::Dynamic, 3, Eigen::DontAlign> pts(points.size(), 3);
        for (size_t i=0; i<points.size(); ++i)
            pts.block<1, 3>(i, 0) = points[i];

        // Get the projections.
        Eigen::Matrix<double, Eigen::Dynamic, 3, Eigen::DontAlign> projections;
        igl::project(pts, modelview, projection, viewport, projections);

        // bounding box created from the rectangle corners - will take care of order of the corners
        BoundingBox rectangle(Points{ Point(m_start_corner.cast<coord_t>()), Point(m_end_corner.cast<coord_t>()) });

        // Iterate over all points and determine whether they're in the rectangle.
        for (int i = 0; i<projections.rows(); ++i)
            if (rectangle.contains(Point(projections(i, 0), canvas.get_canvas_size().get_height() - projections(i, 1))))
                out.push_back(i);

        return out;
    }

    void GLSelectionRectangle::stop_dragging()
    {
        if (is_dragging())
            m_state = Off;
    }

    void GLSelectionRectangle::render(const GLCanvas3D& canvas) const
    {
        if (!is_dragging())
            return;

        const Camera& camera = wxGetApp().plater()->get_camera();
        const auto& view_matrix = camera.get_view_matrix_for_billboard();
        const auto& proj_matrix = camera.get_projection_matrix();
        float inv_zoom = (float)camera.get_inv_zoom();

        Size cnv_size = canvas.get_canvas_size();
        const int cnv_width = cnv_size.get_width();
        const int cnv_height = cnv_size.get_height();
        if (0 == cnv_width || 0 == cnv_height) {
            return;
        }
        float cnv_half_width = 0.5f * static_cast<float>(cnv_width);
        float cnv_half_height = 0.5f * static_cast<float>(cnv_height);

        Vec2d start(m_start_corner(0) - cnv_half_width, cnv_half_height - m_start_corner(1));
        Vec2d end(m_end_corner(0) - cnv_half_width, cnv_half_height - m_end_corner(1));

        float left = (float)std::min(start(0), end(0)) * inv_zoom;
        float top = (float)std::max(start(1), end(1)) * inv_zoom;
        float right = (float)std::max(start(0), end(0)) * inv_zoom;
        float bottom = (float)std::min(start(1), end(1)) * inv_zoom;

        const auto& p_flat_shader = wxGetApp().get_shader("flat");
        if (!p_flat_shader) {
            return;
        }

        if (!m_rectangle.is_initialized()) {
            GLModel::Geometry init_data;
            init_data.format = { GLModel::PrimitiveType::LineLoop, GLModel::Geometry::EVertexLayout::P3 };
            init_data.reserve_vertices(4);
            init_data.reserve_indices(4);

            // vertices
            init_data.add_vertex(Vec3f(-0.5f, -0.5f, 0.0f));
            init_data.add_vertex(Vec3f(0.5f,  -0.5f, 0.0f));
            init_data.add_vertex(Vec3f(0.5f,   0.5f, 0.0f));
            init_data.add_vertex(Vec3f(-0.5f,  0.5f, 0.0f));

            // indices
            init_data.add_index(0);
            init_data.add_index(1);
            init_data.add_index(2);
            init_data.add_index(3);

            m_rectangle.init_from(std::move(init_data));
        }

        Transform3d model_matrix{ Transform3d::Identity() };
        model_matrix.data()[3 * 4 + 0] = (left + right) * 0.5f;
        model_matrix.data()[3 * 4 + 1] = (top + bottom) * 0.5f;
        model_matrix.data()[0 * 4 + 0] = (right - left);
        model_matrix.data()[1 * 4 + 1] = (top - bottom);

        GLboolean was_line_stipple_enabled = GL_FALSE;

        const auto& p_ogl_manager = wxGetApp().get_opengl_manager();
        p_ogl_manager->set_line_width(1.5f);
        glsafe(::glDisable(GL_DEPTH_TEST));

#ifdef __APPLE__
        const auto& gl_info = p_ogl_manager->get_gl_info();
        const auto formated_gl_version = gl_info.get_formated_gl_version();
        if (formated_gl_version < 30)
#endif
        {
            glsafe(::glGetBooleanv(GL_LINE_STIPPLE, &was_line_stipple_enabled));
            glsafe(::glLineStipple(4, 0xAAAA));
            glsafe(::glEnable(GL_LINE_STIPPLE));
        }

        wxGetApp().bind_shader(p_flat_shader);

        p_flat_shader->set_uniform("view_model_matrix", view_matrix * model_matrix);
        p_flat_shader->set_uniform("projection_matrix", proj_matrix);

        m_rectangle.set_color({ 0.0f, 1.0f, 0.38f, 1.0f });
        m_rectangle.render_geometry();

        wxGetApp().unbind_shader();

        if (!was_line_stipple_enabled) {
#ifdef __APPLE__
            if (formated_gl_version < 30)
#endif
            {
                glsafe(::glDisable(GL_LINE_STIPPLE));
            }
        }
    }

} // namespace GUI
} // namespace Slic3r
