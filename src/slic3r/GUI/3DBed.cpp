#include "libslic3r/libslic3r.h"

#include "3DBed.hpp"

#include "libslic3r/Polygon.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Geometry/Circle.hpp"
#include "libslic3r/Tesselate.hpp"
#include "libslic3r/PresetBundle.hpp"

#include "GUI_App.hpp"
#include "GUI_Colors.hpp"
#include "GLCanvas3D.hpp"

#include <GL/glew.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/log/trivial.hpp>
#include <boost/timer.hpp>

static const float GROUND_Z = -0.04f;
static const std::array<float, 4> DEFAULT_MODEL_COLOR = { 0.3255f, 0.337f, 0.337f, 1.0f };
static const std::array<float, 4> DEFAULT_MODEL_COLOR_DARK = { 0.255f, 0.255f, 0.283f, 1.0f };
static const std::array<float, 4> PICKING_MODEL_COLOR = { 0.0f, 0.0f, 0.0f, 1.0f };

namespace Slic3r {
namespace GUI {

bool GeometryBuffer::set_from_triangles(const std::vector<Vec2f> &triangles, float z)
{
    if (triangles.empty()) {
        m_vertices.clear();
        return false;
    }

    m_vertices.clear();
    assert(triangles.size() % 3 == 0);
    m_vertices = std::vector<Vertex>(triangles.size(), Vertex());

    Vec2f min = triangles.front();
    Vec2f max = min;

    for (size_t v_count = 0; v_count < triangles.size(); ++ v_count) {
        const Vec2f &p = triangles[v_count];
        Vertex      &v = m_vertices[v_count];
        v.position   = Vec3f(p.x(), p.y(), z);
        v.tex_coords = p;
        min = min.cwiseMin(p).eval();
        max = max.cwiseMax(p).eval();
    }

    Vec2f size = max - min;
    if (size.x() != 0.f && size.y() != 0.f) {
        Vec2f inv_size = size.cwiseInverse();
        inv_size.y() *= -1;
        for (Vertex& v : m_vertices) {
            v.tex_coords -= min;
            v.tex_coords.x() *= inv_size.x();
            v.tex_coords.y() *= inv_size.y();
        }
    }

    return true;
}

bool GeometryBuffer::set_from_lines(const Lines& lines, float z)
{
    m_vertices.clear();

    unsigned int v_size = 2 * (unsigned int)lines.size();
    if (v_size == 0)
        return false;

    m_vertices = std::vector<Vertex>(v_size, Vertex());

    unsigned int v_count = 0;
    for (const Line& l : lines) {
        Vertex& v1 = m_vertices[v_count];
        v1.position[0] = unscale<float>(l.a(0));
        v1.position[1] = unscale<float>(l.a(1));
        v1.position[2] = z;
        ++v_count;

        Vertex& v2 = m_vertices[v_count];
        v2.position[0] = unscale<float>(l.b(0));
        v2.position[1] = unscale<float>(l.b(1));
        v2.position[2] = z;
        ++v_count;
    }

    return true;
}

//BBS: set from 3d lines
bool GeometryBuffer::set_from_3d_Lines(const Lines3& lines)
{
    m_vertices.clear();

    unsigned int v_size = 2 * (unsigned int)lines.size();
    if (v_size == 0)
        return false;

    m_vertices = std::vector<Vertex>(v_size, Vertex());

    unsigned int v_count = 0;
    for (const Line3& l : lines) {
        Vertex& v1 = m_vertices[v_count];
        v1.position[0] = unscale<float>(l.a(0));
        v1.position[1] = unscale<float>(l.a(1));
        v1.position[2] = unscale<float>(l.a(2));
        ++v_count;

        Vertex& v2 = m_vertices[v_count];
        v2.position[0] = unscale<float>(l.b(0));
        v2.position[1] = unscale<float>(l.b(1));
        v2.position[2] = unscale<float>(l.b(2));
        ++v_count;
    }

    return true;
}

const float* GeometryBuffer::get_vertices_data() const
{
    return (m_vertices.size() > 0) ? (const float*)m_vertices.data() : nullptr;
}

const float Bed3D::Axes::DefaultStemRadius = 0.5f;
const float Bed3D::Axes::DefaultStemLength = 25.0f;
const float Bed3D::Axes::DefaultTipRadius = 2.5f * Bed3D::Axes::DefaultStemRadius;
const float Bed3D::Axes::DefaultTipLength = 5.0f;

std::array<float, 4> Bed3D::AXIS_X_COLOR = decode_color_to_float_array("#FF0000");
std::array<float, 4> Bed3D::AXIS_Y_COLOR = decode_color_to_float_array("#00FF00");
std::array<float, 4> Bed3D::AXIS_Z_COLOR = decode_color_to_float_array("#0000FF");

void Bed3D::update_render_colors()
{
    Bed3D::AXIS_X_COLOR = GLColor(RenderColor::colors[RenderCol_Axis_X]);
    Bed3D::AXIS_Y_COLOR = GLColor(RenderColor::colors[RenderCol_Axis_Y]);
    Bed3D::AXIS_Z_COLOR = GLColor(RenderColor::colors[RenderCol_Axis_Z]);
}

void Bed3D::load_render_colors()
{
    RenderColor::colors[RenderCol_Axis_X] = IMColor(Bed3D::AXIS_X_COLOR);
    RenderColor::colors[RenderCol_Axis_Y] = IMColor(Bed3D::AXIS_Y_COLOR);
    RenderColor::colors[RenderCol_Axis_Z] = IMColor(Bed3D::AXIS_Z_COLOR);
}

void Bed3D::Axes::render() const
{
    auto render_axis = [this](const Transform3d& transform, const std::shared_ptr<GLShaderProgram>& shader) {
        const Camera& camera = wxGetApp().plater()->get_camera();
        Transform3d view_matrix = camera.get_view_matrix();
        const Transform3d view_model_matrix = view_matrix * transform;
        shader->set_uniform("view_model_matrix", view_model_matrix);
        shader->set_uniform("projection_matrix", camera.get_projection_matrix());
        shader->set_uniform("normal_matrix", (Matrix3d)view_model_matrix.matrix().block(0, 0, 3, 3).inverse().transpose());
        m_arrow.render_geometry();
    };

    if (!m_arrow.is_initialized())
        const_cast<GLModel*>(&m_arrow)->init_from(stilized_arrow(16, DefaultTipRadius, DefaultTipLength, DefaultStemRadius, m_stem_length));

    const auto& shader = wxGetApp().get_shader("gouraud_light");
    if (shader == nullptr)
        return;

    glsafe(::glEnable(GL_DEPTH_TEST));

    wxGetApp().bind_shader(shader);
    shader->set_uniform("emission_factor", 0.0f);

    // x axis
    const_cast<GLModel*>(&m_arrow)->set_color(-1, AXIS_X_COLOR);
    render_axis(Geometry::assemble_transform(m_origin, { 0.0, 0.5 * M_PI, 0.0 }), shader);

    // y axis
    const_cast<GLModel*>(&m_arrow)->set_color(-1, AXIS_Y_COLOR);
    render_axis(Geometry::assemble_transform(m_origin, { -0.5 * M_PI, 0.0, 0.0 }), shader);

    // z axis
    const_cast<GLModel*>(&m_arrow)->set_color(-1, AXIS_Z_COLOR);
    render_axis(Geometry::assemble_transform(m_origin), shader);

    wxGetApp().unbind_shader();

    glsafe(::glDisable(GL_DEPTH_TEST));
}

//BBS: add part plate logic
bool Bed3D::set_shape(const Pointfs& printable_area, const double printable_height, std::vector<Pointfs> extruder_areas, std::vector<double> extruder_heights, const std::string& custom_model, bool force_as_custom,
    const Vec2d position, bool with_reset)
{
    /*auto check_texture = [](const std::string& texture) {
        boost::system::error_code ec; // so the exists call does not throw (e.g. after a permission problem)
        return !texture.empty() && (boost::algorithm::iends_with(texture, ".png") || boost::algorithm::iends_with(texture, ".svg")) && boost::filesystem::exists(texture, ec);
    };*/

    auto check_model = [](const std::string& model) {
        boost::system::error_code ec;
        return !model.empty() && boost::algorithm::iends_with(model, ".stl") && boost::filesystem::exists(model, ec);
    };

    Type type;
    std::string model;
    std::string texture;
    if (force_as_custom)
        type = Type::Custom;
    else {
        auto [new_type, system_model, system_texture] = detect_type(printable_area);
        type = new_type;
        model = system_model;
        texture = system_texture;
    }

    /*std::string texture_filename = custom_texture.empty() ? texture : custom_texture;
    if (! texture_filename.empty() && ! check_texture(texture_filename)) {
        BOOST_LOG_TRIVIAL(error) << "Unable to load bed texture: " << texture_filename;
        texture_filename.clear();
    }*/

    std::string model_filename = custom_model.empty() ? model : custom_model;
    if (! model_filename.empty() && ! check_model(model_filename)) {
        BOOST_LOG_TRIVIAL(error) << "Unable to load bed model: " << model_filename;
        model_filename.clear();
    }

    //BBS: add position related logic
    if (m_bed_shape == printable_area && m_build_volume.printable_height() == printable_height && m_type == type && m_model_filename == model_filename && position == m_position && m_extruder_shapes == extruder_areas  && m_extruder_heights == extruder_heights)
        // No change, no need to update the UI.
        return false;

    //BBS: add part plate logic, apply position to bed shape
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(":current position {%1%,%2%}, new position {%3%, %4%}") % m_position.x() % m_position.y() % position.x() % position.y();
    m_position = position;
    m_bed_shape = printable_area;
    m_extruder_shapes = extruder_areas;
    m_extruder_heights = extruder_heights;
    if ((position(0) != 0) || (position(1) != 0)) {
        Pointfs new_bed_shape;
        for (const Vec2d& p : m_bed_shape) {
            Vec2d point(p(0) + m_position.x(), p(1) + m_position.y());
            new_bed_shape.push_back(point);
        }
        std::vector<Pointfs> new_extruder_shapes;
        for (const std::vector<Vec2d>& shape : m_extruder_shapes) {
            std::vector<Vec2d> new_extruder_shape;
            for (const Vec2d& p : shape) {
                Vec2d point(p(0) + m_position.x(), p(1) + m_position.y());
                new_extruder_shape.push_back(point);
            }
            new_extruder_shapes.push_back(new_extruder_shape);
        }
        m_build_volume = BuildVolume { new_bed_shape, printable_height, new_extruder_shapes, m_extruder_heights };
    }
    else
        m_build_volume = BuildVolume { printable_area, printable_height, m_extruder_shapes, m_extruder_heights };
    m_type = type;
    //m_texture_filename = texture_filename;
    m_model_filename = model_filename;
    std::replace(m_model_filename.begin(), m_model_filename.end(), '\\', '/');
    //BBS: add part plate logic
    m_extended_bounding_box = this->calc_extended_bounding_box(false);

    //BBS: add part plate logic

    //BBS add default bed
#if 1
    ExPolygon poly{ Polygon::new_scale(printable_area) };
#else
    ExPolygon poly;
    for (const Vec2d& p : printable_area) {
        poly.contour.append(Point(scale_(p(0) + m_position.x()), scale_(p(1) + m_position.y())));
    }
#endif

    calc_triangles(poly);

    //no need gridline for 3dbed
    //const BoundingBox& bed_bbox = poly.contour.bounding_box();
    //calc_gridlines(poly, bed_bbox);

    //m_polygon = offset(poly.contour, (float)bed_bbox.radius() * 1.7f, jtRound, scale_(0.5))[0];

    if (with_reset) {
        this->release_VBOs();
        //m_texture.reset();
        m_model.reset();
    }
    //BBS: add part plate logic, always update model offset
    //else {
        update_model_offset();
    //}

    // Set the origin and size for rendering the coordinate system axes.
    m_axes.set_origin({ 0.0, 0.0, static_cast<double>(GROUND_Z) });
    m_axes.set_stem_length(0.1f * static_cast<float>(m_build_volume.bounding_volume().max_size()));

    // Let the calee to update the UI.
    return true;
}

//BBS: add api to set position for partplate related bed
void Bed3D::set_position(Vec2d& position)
{
    set_shape(m_bed_shape, m_build_volume.printable_height(), m_extruder_shapes, m_extruder_heights, m_model_filename, false, position, false);
}

void Bed3D::set_axes_mode(bool origin)
{
    if (origin) {
        m_axes.set_origin({ 0.0, 0.0, static_cast<double>(GROUND_Z) });
    }
    else {
        m_axes.set_origin({ m_position.x(), m_position.y(), static_cast<double>(GROUND_Z) });
    }
}

/*bool Bed3D::contains(const Point& point) const
{
    return m_polygon.contains(point);
}

Point Bed3D::point_projection(const Point& point) const
{
    return m_polygon.point_projection(point);
}*/

void Bed3D::on_change_color_mode(bool is_dark)
{
    m_is_dark = is_dark;
}

BoundingBoxf3 Bed3D::get_cur_bed_model_box()
{
    BoundingBoxf3 model_bb = m_model.get_bounding_box();
    model_bb.translate(m_model_offset);
    return model_bb;
}

void Bed3D::render(GLCanvas3D &canvas, bool bottom, float scale_factor, bool show_axes) {
    render_internal(canvas, bottom, scale_factor, show_axes);
}

/*void Bed3D::render_for_picking(GLCanvas3D& canvas, bool bottom, float scale_factor)
{
    render_internal(canvas, bottom, scale_factor, false, false, true);
}*/

void Bed3D::render_internal(GLCanvas3D& canvas, bool bottom, float scale_factor,
    bool show_axes)
{
    float* factor = const_cast<float*>(&m_scale_factor);
    *factor = scale_factor;

    if (show_axes)
        render_axes();

    glsafe(::glEnable(GL_DEPTH_TEST));

    m_model.set_color(-1, m_is_dark ? DEFAULT_MODEL_COLOR_DARK : DEFAULT_MODEL_COLOR);

    switch (m_type)
    {
    case Type::System: { render_system(canvas, bottom); break; }
    default:
    case Type::Custom: { render_custom(canvas, bottom); break; }
    }

    glsafe(::glDisable(GL_DEPTH_TEST));
}

//BBS: add partplate related logic
// Calculate an extended bounding box from axes and current model for visualization purposes.
BoundingBoxf3 Bed3D::calc_extended_bounding_box(bool consider_model_offset) const
{
    BoundingBoxf3 out { m_build_volume.bounding_volume() };

    const Vec3d size = out.size();
    // ensures that the bounding box is set as defined or the following calls to merge() will not work as intented
    if (size.x() > 0.0 && size.y() > 0.0 && !out.defined)
        out.defined = true;
    // Reset the build volume Z, we don't want to zoom to the top of the build volume if it is empty.
    out.min.z() = 0.0;
    out.max.z() = 0.0;
    // extend to contain axes
    //BBS: add part plate related logic.
    Vec3d offset{ m_position.x(), m_position.y(), 0.f };
    //out.merge(m_axes.get_origin() + offset + m_axes.get_total_length() * Vec3d::Ones());
    out.merge(Vec3d(0.f, 0.f, GROUND_Z) + offset + m_axes.get_total_length() * Vec3d::Ones());
    out.merge(out.min + Vec3d(-Axes::DefaultTipRadius, -Axes::DefaultTipRadius, out.max.z()));
    //BBS: add part plate related logic.
    if (consider_model_offset) {
        // extend to contain model, if any
        BoundingBoxf3 model_bb = m_model.get_bounding_box();
        if (model_bb.defined) {
            model_bb.translate(m_model_offset);
            out.merge(model_bb);
        }
    }
    return out;
}

void Bed3D::calc_triangles(const ExPolygon& poly)
{
    m_triangles.reset();
    if (!m_triangles.init_model_from_poly(triangulate_expolygon_2f(poly, NORMALS_UP), GROUND_Z))
        BOOST_LOG_TRIVIAL(error) << "Unable to create bed triangles";
}

void Bed3D::calc_gridlines(const ExPolygon& poly, const BoundingBox& bed_bbox)
{
    /*Polylines axes_lines;
    for (coord_t x = bed_bbox.min.x(); x <= bed_bbox.max.x(); x += scale_(10.0)) {
        Polyline line;
        line.append(Point(x, bed_bbox.min.y()));
        line.append(Point(x, bed_bbox.max.y()));
        axes_lines.push_back(line);
    }
    for (coord_t y = bed_bbox.min.y(); y <= bed_bbox.max.y(); y += scale_(10.0)) {
        Polyline line;
        line.append(Point(bed_bbox.min.x(), y));
        line.append(Point(bed_bbox.max.x(), y));
        axes_lines.push_back(line);
    }

    // clip with a slightly grown expolygon because our lines lay on the contours and may get erroneously clipped
    Lines gridlines = to_lines(intersection_pl(axes_lines, offset(poly, (float)SCALED_EPSILON)));

    // append bed contours
    Lines contour_lines = to_lines(poly);
    std::copy(contour_lines.begin(), contour_lines.end(), std::back_inserter(gridlines));

    if (!m_gridlines.set_from_lines(gridlines, GROUND_Z))
        BOOST_LOG_TRIVIAL(error) << "Unable to create bed grid lines\n";*/
}

// Try to match the print bed shape with the shape of an active profile. If such a match exists,
// return the print bed model.
std::tuple<Bed3D::Type, std::string, std::string> Bed3D::detect_type(const Pointfs& shape)
{
    auto bundle = wxGetApp().preset_bundle;
    if (bundle != nullptr) {
        const Preset* curr = &bundle->printers.get_selected_preset();
        while (curr != nullptr) {
            if (curr->config.has("printable_area")) {
                std::string texture_filename, model_filename;
                if (shape == dynamic_cast<const ConfigOptionPoints*>(curr->config.option("printable_area"))->values) {
                    if (curr->is_system)
                        model_filename = PresetUtils::system_printer_bed_model(*curr);
                    else {
                        auto *printer_model = curr->config.opt<ConfigOptionString>("printer_model");
                        if (printer_model != nullptr && ! printer_model->value.empty()) {
                            model_filename = bundle->get_stl_model_for_printer_model(printer_model->value);
                        }
                    }
                    //std::string model_filename = PresetUtils::system_printer_bed_model(*curr);
                    //std::string texture_filename = PresetUtils::system_printer_bed_texture(*curr);
                    if (!model_filename.empty())
                        return { Type::System, model_filename, texture_filename };
                }
            }

            curr = bundle->printers.get_preset_parent(*curr);
        }
    }

    return { Type::Custom, {}, {} };
}

void Bed3D::render_axes() const
{
    if (m_build_volume.valid())
        m_axes.render();
}

void Bed3D::render_system(GLCanvas3D& canvas, bool bottom) const
{
    if (!bottom)
        render_model();

    /*if (show_texture)
        render_texture(bottom, canvas);*/
}

/*void Bed3D::render_texture(bool bottom, GLCanvas3D& canvas) const
{
    GLTexture* texture = const_cast<GLTexture*>(&m_texture);
    GLTexture* temp_texture = const_cast<GLTexture*>(&m_temp_texture);

    if (m_texture_filename.empty()) {
        texture->reset();
        render_default(bottom, false);
        return;
    }

    if (texture->get_id() == 0 || texture->get_source() != m_texture_filename) {
        texture->reset();

        if (boost::algorithm::iends_with(m_texture_filename, ".svg")) {
            // use higher resolution images if graphic card and opengl version allow
            GLint max_tex_size = OpenGLManager::get_gl_info().get_max_tex_size();
            if (temp_texture->get_id() == 0 || temp_texture->get_source() != m_texture_filename) {
                // generate a temporary lower resolution texture to show while no main texture levels have been compressed
                if (!temp_texture->load_from_svg_file(m_texture_filename, false, false, false, max_tex_size / 8)) {
                    render_default(bottom, false);
                    return;
                }
                canvas.request_extra_frame();
            }

            // starts generating the main texture, compression will run asynchronously
            if (!texture->load_from_svg_file(m_texture_filename, true, true, true, max_tex_size)) {
                render_default(bottom, false);
                return;
            }
        }
        else if (boost::algorithm::iends_with(m_texture_filename, ".png")) {
            // generate a temporary lower resolution texture to show while no main texture levels have been compressed
            if (temp_texture->get_id() == 0 || temp_texture->get_source() != m_texture_filename) {
                if (!temp_texture->load_from_file(m_texture_filename, false, GLTexture::None, false)) {
                    render_default(bottom, false);
                    return;
                }
                canvas.request_extra_frame();
            }

            // starts generating the main texture, compression will run asynchronously
            if (!texture->load_from_file(m_texture_filename, true, GLTexture::MultiThreaded, true)) {
                render_default(bottom, false);
                return;
            }
        }
        else {
            render_default(bottom, false);
            return;
        }
    }
    else if (texture->unsent_compressed_data_available()) {
        // sends to gpu the already available compressed levels of the main texture
        texture->send_compressed_data_to_gpu();

        // the temporary texture is not needed anymore, reset it
        if (temp_texture->get_id() != 0)
            temp_texture->reset();

        canvas.request_extra_frame();
    }

    if (m_triangles.get_vertices_count() > 0) {
        const auto& shader = wxGetApp().get_shader("printbed");
        if (shader != nullptr) {
            wxGetApp().bind_shader(shader);
            shader->set_uniform("transparent_background", bottom);
            shader->set_uniform("svg_source", boost::algorithm::iends_with(m_texture.get_source(), ".svg"));

            unsigned int* vbo_id = const_cast<unsigned int*>(&m_vbo_id);

            if (*vbo_id == 0) {
                glsafe(::glGenBuffers(1, vbo_id));
                glsafe(::glBindBuffer(GL_ARRAY_BUFFER, *vbo_id));
                glsafe(::glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)m_triangles.get_vertices_data_size(), (const GLvoid*)m_triangles.get_vertices_data(), GL_STATIC_DRAW));
                glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
            }

            glsafe(::glEnable(GL_DEPTH_TEST));
            if (bottom)
                glsafe(::glDepthMask(GL_FALSE));

            glsafe(::glEnable(GL_BLEND));
            glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

            if (bottom)
                glsafe(::glFrontFace(GL_CW));

            unsigned int stride = m_triangles.get_vertex_data_size();

            GLint position_id = shader->get_attrib_location("v_position");
            GLint tex_coords_id = shader->get_attrib_location("v_tex_coord");

            // show the temporary texture while no compressed data is available
            GLuint tex_id = (GLuint)temp_texture->get_id();
            if (tex_id == 0)
                tex_id = (GLuint)texture->get_id();

            glsafe(::glBindTexture(GL_TEXTURE_2D, tex_id));
            glsafe(::glBindBuffer(GL_ARRAY_BUFFER, *vbo_id));

            if (position_id != -1) {
                glsafe(::glEnableVertexAttribArray(position_id));
                glsafe(::glVertexAttribPointer(position_id, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(intptr_t)m_triangles.get_position_offset()));
            }
            if (tex_coords_id != -1) {
                glsafe(::glEnableVertexAttribArray(tex_coords_id));
                glsafe(::glVertexAttribPointer(tex_coords_id, 2, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(intptr_t)m_triangles.get_tex_coords_offset()));
            }

            glsafe(::glDrawArrays(GL_TRIANGLES, 0, (GLsizei)m_triangles.get_vertices_count()));

            if (tex_coords_id != -1)
                glsafe(::glDisableVertexAttribArray(tex_coords_id));

            if (position_id != -1)
                glsafe(::glDisableVertexAttribArray(position_id));

            glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
            glsafe(::glBindTexture(GL_TEXTURE_2D, 0));

            if (bottom)
                glsafe(::glFrontFace(GL_CCW));

            glsafe(::glDisable(GL_BLEND));
            if (bottom)
                glsafe(::glDepthMask(GL_TRUE));

            wxGetApp().unbind_shader();
        }
    }
}*/

//BBS: add part plate related logic
void Bed3D::update_model_offset() const
{
    // move the model so that its origin (0.0, 0.0, 0.0) goes into the bed shape center and a bit down to avoid z-fighting with the texture quad
    Vec3d shift = m_build_volume.bounding_volume().center();
    shift(2) = -0.03;
    Vec3d* model_offset_ptr = const_cast<Vec3d*>(&m_model_offset);
    *model_offset_ptr = shift;
    //BBS: TODO: hack for current stl for BBL printer
    if (std::string::npos != m_model_filename.find("bbl-3dp-"))
    {
        (*model_offset_ptr)(0) -= m_bed_shape[2].x() / 2.0f;
        (*model_offset_ptr)(1) -= m_bed_shape[2].y() / 2.0f;
        (*model_offset_ptr)(2) = -0.41 + GROUND_Z;
    }

    // update extended bounding box
    const_cast<BoundingBoxf3&>(m_extended_bounding_box) = calc_extended_bounding_box();
}

void Bed3D::update_bed_triangles()
{
    if (m_triangles.is_initialized()) {
        return;
    }
    GeometryBuffer new_triangles;
    Vec3d shift = m_extended_bounding_box.center();
    shift(2) = -0.03;
    Vec3d* model_offset_ptr = const_cast<Vec3d*>(&m_model_offset);
    *model_offset_ptr = shift;
    //BBS: TODO: hack for default bed
    BoundingBoxf3 build_volume;

    if (!m_build_volume.valid()) return;

    (*model_offset_ptr)(0) = m_build_volume.bounding_volume2d().min.x();
    (*model_offset_ptr)(1) = m_build_volume.bounding_volume2d().min.y();
    (*model_offset_ptr)(2) = -0.41 + GROUND_Z;

    std::vector<Vec2d> origin_bed_shape;
    for (size_t i = 0; i < m_bed_shape.size(); i++) {
        origin_bed_shape.push_back(m_bed_shape[i] - m_bed_shape[0]);
    }
    std::vector<Vec2d> new_bed_shape; // offset to correct origin
    for (auto point : origin_bed_shape) {
        Vec2d new_point(point.x() + model_offset_ptr->x(), point.y() + model_offset_ptr->y());
        new_bed_shape.push_back(new_point);
    }
    ExPolygon poly{ Polygon::new_scale(new_bed_shape) };
    calc_triangles(poly);
    // update extended bounding box
    const_cast<BoundingBoxf3&>(m_extended_bounding_box) = calc_extended_bounding_box();

}

void Bed3D::render_model() const
{
    if (m_model_filename.empty())
        return;

    GLModel* model = const_cast<GLModel*>(&m_model);

    if (model->get_filename() != m_model_filename && model->init_from_file(m_model_filename)) {
        model->set_color(-1, m_is_dark ? DEFAULT_MODEL_COLOR_DARK : DEFAULT_MODEL_COLOR);

        update_model_offset();
    }

    if (!model->get_filename().empty()) {
        const Camera &     camera      = wxGetApp().plater()->get_camera();
        const Transform3d &view_matrix = camera.get_view_matrix();
        const Transform3d &projection_matrix = camera.get_projection_matrix();
        const auto& shader = wxGetApp().get_shader("hotbed");
        if (shader != nullptr) {
            wxGetApp().bind_shader(shader);
            shader->set_uniform("emission_factor", 0.0f);
            const Transform3d model_matrix = Geometry::assemble_transform(m_model_offset);
            shader->set_uniform("volume_world_matrix",  model_matrix);
            shader->set_uniform("view_model_matrix", view_matrix * model_matrix);
            shader->set_uniform("projection_matrix", projection_matrix);
            const Matrix3d view_normal_matrix = view_matrix.matrix().block(0, 0, 3, 3) * model_matrix.matrix().block(0, 0, 3, 3).inverse().transpose();
            shader->set_uniform("view_normal_matrix", view_normal_matrix);
            if (m_build_volume.get_extruder_area_count() > 0) {
                const BuildVolume::BuildSharedVolume& shared_volume = m_build_volume.get_shared_volume();
                std::array<float, 4>       xy_data       = shared_volume.data;
                shader->set_uniform("print_volume.type", shared_volume.type);
                shader->set_uniform("print_volume.xy_data", xy_data);
                std::array<float, 2> zs = shared_volume.zs;
                zs[0]                   = -1;
                shader->set_uniform("print_volume.z_data", zs);
            }
            else {
                //use -1 ad a invalid type
                shader->set_uniform("print_volume.type", -1);
            }
            model->render_geometry();
            wxGetApp().unbind_shader();
        }
    }
}

void Bed3D::render_custom(GLCanvas3D& canvas, bool bottom)
{
    if (m_model_filename.empty()) {
        render_default(bottom);
        return;
    }

    if (!bottom)
        render_model();

    /*if (show_texture)
        render_texture(bottom, canvas);*/
}

void Bed3D::render_default(bool bottom)
{
    bool picking = false;
    const_cast<GLTexture*>(&m_texture)->reset();
    update_bed_triangles();
    unsigned int triangles_vcount = m_triangles.get_vertices_count();
    if (triangles_vcount > 0) {
        const auto &shader = wxGetApp().get_shader("flat");
        if (shader == nullptr) return;
        wxGetApp().bind_shader(shader);
        const Camera &     camera            = wxGetApp().plater()->get_camera();
        const Transform3d &view_matrix       = camera.get_view_matrix();
        const Transform3d &projection_matrix = camera.get_projection_matrix();
        shader->set_uniform("view_model_matrix", view_matrix);
        shader->set_uniform("projection_matrix", projection_matrix);

        bool has_model = !m_model.get_filename().empty();

        glsafe(::glEnable(GL_DEPTH_TEST));
        glsafe(::glEnable(GL_BLEND));
        glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

        if (!has_model && !bottom) {
            glsafe(::glDepthMask(GL_FALSE));
            m_triangles.set_color(DEFAULT_MODEL_COLOR);
            m_triangles.render_geometry();
            glsafe(::glDepthMask(GL_TRUE));
        }

        glsafe(::glDisable(GL_BLEND));
        wxGetApp().unbind_shader();
    }
}

void Bed3D::release_VBOs()
{
    if (m_vbo_id > 0) {
        glsafe(::glDeleteBuffers(1, &m_vbo_id));
        m_vbo_id = 0;
    }
}

} // GUI
} // Slic3r
