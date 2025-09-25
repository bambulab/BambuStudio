#include "slic3r/GUI/GCodeRenderer/LegacyRenderer.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/BuildVolume.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/LocalesUtils.hpp"
#include "libslic3r/PresetBundle.hpp"
//BBS: add convex hull logic for toolpath check
#include "libslic3r/Geometry/ConvexHull.hpp"
#include "slic3r/GUI/OpenGLManager.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/GUI_Utils.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GLToolbar.hpp"
#include "slic3r/GUI/GUI_Preview.hpp"
#include "slic3r/GUI/IMSlider.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Layer.hpp"
#include "slic3r/GUI/Widgets/ProgressDialog.hpp"
#include <imgui/imgui_internal.h>
#include <GL/glew.h>
#include <boost/log/trivial.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/nowide/cstdio.hpp>
#include <boost/nowide/fstream.hpp>
#include <wx/progdlg.h>
#include <wx/numformatter.h>
#include <array>
#include <algorithm>
#include <chrono>
namespace Slic3r {
    namespace GUI {
        namespace gcode
        {
            //BBS translation of EViewType
            //const std::string EViewType_Map[(int) LegacyRenderer::EViewType::Count] = {
            //        _u8L("Line Type"),
            //        _u8L("Layer Height"),
            //        _u8L("Line Width"),
            //        _u8L("Speed"),
            //        _u8L("Fan Speed"),
            //        _u8L("Temperature"),
            //        _u8L("Flow"),
            //        _u8L("Tool"),
            //        _u8L("Filament")
            //    };
            static unsigned char buffer_id(EMoveType type) {
                return static_cast<unsigned char>(type) - static_cast<unsigned char>(EMoveType::Retract);
            }
            static EMoveType buffer_type(unsigned char id) {
                return static_cast<EMoveType>(static_cast<unsigned char>(EMoveType::Retract) + id);
            }
            static std::array<float, 4> decode_color(const std::string& color) {
                static const float INV_255 = 1.0f / 255.0f;
                std::array<float, 4> ret = { 0.0f, 0.0f, 0.0f, 1.0f };
                const char* c = color.data() + 1;
                if (color.size() == 7 && color.front() == '#') {
                    for (size_t j = 0; j < 3; ++j) {
                        int digit1 = hex_digit_to_int(*c++);
                        int digit2 = hex_digit_to_int(*c++);
                        if (digit1 == -1 || digit2 == -1)
                            break;
                        ret[j] = float(digit1 * 16 + digit2) * INV_255;
                    }
                }
                else if (color.size() == 9 && color.front() == '#') {
                    for (size_t j = 0; j < 4; ++j) {
                        int digit1 = hex_digit_to_int(*c++);
                        int digit2 = hex_digit_to_int(*c++);
                        if (digit1 == -1 || digit2 == -1)
                            break;
                        ret[j] = float(digit1 * 16 + digit2) * INV_255;
                    }
                }
                return ret;
            }
            static std::vector<std::array<float, 4>> decode_colors(const std::vector<std::string>& colors) {
                std::vector<std::array<float, 4>> output(colors.size(), { 0.0f, 0.0f, 0.0f, 1.0f });
                for (size_t i = 0; i < colors.size(); ++i) {
                    output[i] = decode_color(colors[i]);
                }
                return output;
            }
            // Round to a bin with minimum two digits resolution.
            // Equivalent to conversion to string with sprintf(buf, "%.2g", value) and conversion back to float, but faster.
            static float round_to_bin(const float value)
            {
                //    assert(value > 0);
                constexpr float const scale[5] = { 100.f,  1000.f,  10000.f,  100000.f,  1000000.f };
                constexpr float const invscale[5] = { 0.01f,  0.001f,  0.0001f,  0.00001f,  0.000001f };
                constexpr float const threshold[5] = { 0.095f, 0.0095f, 0.00095f, 0.000095f, 0.0000095f };
                // Scaling factor, pointer to the tables above.
                int                   i = 0;
                // While the scaling factor is not yet large enough to get two integer digits after scaling and rounding:
                for (; value < threshold[i] && i < 4; ++i);
                return std::round(value * scale[i]) * invscale[i];
            }
            // Find an index of a value in a sorted vector, which is in <z-eps, z+eps>.
            // Returns -1 if there is no such member.
            static int find_close_layer_idx(const std::vector<double>& zs, double& z, double eps)
            {
                if (zs.empty()) return -1;
                auto it_h = std::lower_bound(zs.begin(), zs.end(), z);
                if (it_h == zs.end()) {
                    auto it_l = it_h;
                    --it_l;
                    if (z - *it_l < eps) return int(zs.size() - 1);
                }
                else if (it_h == zs.begin()) {
                    if (*it_h - z < eps) return 0;
                }
                else {
                    auto it_l = it_h;
                    --it_l;
                    double dist_l = z - *it_l;
                    double dist_h = *it_h - z;
                    if (std::min(dist_l, dist_h) < eps) { return (dist_l < dist_h) ? int(it_l - zs.begin()) : int(it_h - zs.begin()); }
                }
                return -1;
            }
            void LegacyRenderer::VBuffer::reset()
            {
                // release gpu memory
                if (!vbos.empty()) {
                    glsafe(::glDeleteBuffers(static_cast<GLsizei>(vbos.size()), static_cast<const GLuint*>(vbos.data())));
                    vbos.clear();
                }
                sizes.clear();
                count = 0;
            }
            void LegacyRenderer::InstanceVBuffer::Ranges::reset()
            {
                for (Range& range : ranges) {
                    // release gpu memory
                    if (range.vbo > 0)
                        glsafe(::glDeleteBuffers(1, &range.vbo));
                }
                ranges.clear();
            }
            void LegacyRenderer::InstanceVBuffer::reset()
            {
                s_ids.clear();
                s_ids.shrink_to_fit();
                buffer.clear();
                buffer.shrink_to_fit();
                render_ranges.reset();
            }
            void LegacyRenderer::IBuffer::reset()
            {
                // release gpu memory
                if (ibo > 0) {
                    glsafe(::glDeleteBuffers(1, &ibo));
                    ibo = 0;
                }
                vbo = 0;
                count = 0;
            }
            bool LegacyRenderer::Path::matches(const GCodeProcessorResult::MoveVertex& move) const
            {
                auto matches_percent = [](float value1, float value2, float max_percent) {
                    return std::abs(value2 - value1) / value1 <= max_percent;
                    };
                switch (move.type)
                {
                case EMoveType::Tool_change:
                case EMoveType::Color_change:
                case EMoveType::Pause_Print:
                case EMoveType::Custom_GCode:
                case EMoveType::Retract:
                case EMoveType::Unretract:
                case EMoveType::Seam:
                case EMoveType::Extrude: {
                    // use rounding to reduce the number of generated paths
                    return type == move.type && extruder_id == move.extruder_id && cp_color_id == move.cp_color_id && role == move.extrusion_role &&
                        move.position.z() <= sub_paths.front().first.position.z() && feedrate == move.feedrate && fan_speed == move.fan_speed &&
                        height == round_to_bin(move.height) && width == round_to_bin(move.width) &&
                        matches_percent(volumetric_rate, move.volumetric_rate(), 0.05f) && layer_time == move.layer_duration &&
                        thermal_index_mean == move.thermal_index_mean && thermal_index_min == move.thermal_index_min && thermal_index_max == move.thermal_index_max;
                }
                case EMoveType::Travel: {
                    return type == move.type && feedrate == move.feedrate && extruder_id == move.extruder_id && cp_color_id == move.cp_color_id;
                }
                default: { return false; }
                }
            }
            void LegacyRenderer::TBuffer::Model::reset()
            {
                instances.reset();
            }
            void LegacyRenderer::TBuffer::reset()
            {
                vertices.reset();
                for (IBuffer& buffer : indices) {
                    buffer.reset();
                }
                indices.clear();
                paths.clear();
                render_paths.clear();
                model.reset();
            }
            void LegacyRenderer::TBuffer::add_path(const GCodeProcessorResult::MoveVertex& move, unsigned int b_id, size_t i_id, size_t s_id)
            {
                Path::Endpoint endpoint = { b_id, i_id, s_id, move.position };
                // use rounding to reduce the number of generated paths
                paths.push_back({ move.type,
                     move.extrusion_role,
                     move.delta_extruder,
                     round_to_bin(move.height),
                     round_to_bin(move.width),
                     move.feedrate,
                     move.fan_speed,
                     move.temperature,
                     move.thermal_index_min,
                     move.thermal_index_max,
                     move.thermal_index_mean,
                     move.volumetric_rate(),
                     move.layer_duration,
                     move.extruder_id,
                     move.cp_color_id,
                     false,
                     {{endpoint, endpoint}} });
            }

            LegacyRenderer::LegacyRenderer()
                : BaseRenderer::BaseRenderer()
            {
            }
            LegacyRenderer::~LegacyRenderer()
            {
                reset();
            }
            void LegacyRenderer::init(ConfigOptionMode mode, PresetBundle* preset_bundle)
            {
                if (m_gl_data_initialized)
                    return;

                BaseRenderer::init(mode, preset_bundle);

                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": enter, m_buffers.size=%1%")
                    % m_buffers.size();
                // initializes opengl data of TBuffers
                for (size_t i = 0; i < m_buffers.size(); ++i) {
                    TBuffer& buffer = m_buffers[i];
                    EMoveType type = buffer_type(i);
                    switch (type)
                    {
                    default: { break; }
                    case EMoveType::Tool_change:
                    case EMoveType::Color_change:
                    case EMoveType::Pause_Print:
                    case EMoveType::Custom_GCode:
                    case EMoveType::Retract:
                    case EMoveType::Unretract:
                    case EMoveType::Seam: {
                        //            if (wxGetApp().is_gl_version_greater_or_equal_to(3, 3)) {
                        //                buffer.render_primitive_type = TBuffer::ERenderPrimitiveType::InstancedModel;
                        //                buffer.shader = "gouraud_light_instanced";
                        //                buffer.model.model.init_from(diamond(16));
                        //                buffer.model.color = option_color(type);
                        //                buffer.model.instances.format = InstanceVBuffer::EFormat::InstancedModel;
                        //            }
                        //            else {
                        if (type == EMoveType::Seam)
                            buffer.visible = true;
                        buffer.render_primitive_type = TBuffer::ERenderPrimitiveType::BatchedModel;
                        buffer.vertices.format = VBuffer::EFormat::PositionNormal3;
                        buffer.shader = "gouraud_light";
                        buffer.model.data = diamond(16);
                        buffer.model.color = option_color(type);
                        buffer.model.instances.format = InstanceVBuffer::EFormat::BatchedModel;
                        //            }
                        break;
                    }
                    case EMoveType::Wipe:
                    case EMoveType::Extrude: {
                        buffer.render_primitive_type = TBuffer::ERenderPrimitiveType::Triangle;
                        buffer.vertices.format = VBuffer::EFormat::PositionNormal3;
                        buffer.shader = "gouraud_light";
                        break;
                    }
                    case EMoveType::Travel: {
                        buffer.render_primitive_type = TBuffer::ERenderPrimitiveType::Line;
                        buffer.vertices.format = VBuffer::EFormat::PositionNormal3;
                        buffer.shader = "toolpaths_lines";
                        break;
                    }
                    }
                    set_move_type_visible(EMoveType::Extrude, true);
                }

                m_gl_data_initialized = true;
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": finished");
            }

            std::vector<int> LegacyRenderer::get_plater_extruder()
            {
                return m_plater_extruder;
            }
            void LegacyRenderer::refresh(const GCodeProcessorResult& gcode_result, const std::vector<std::string>& str_tool_colors)
            {
                BaseRenderer::refresh(gcode_result, str_tool_colors);
                log_memory_used("Refreshed G-code extrusion paths, ");
            }
            void LegacyRenderer::refresh_render_paths()
            {
                refresh_render_paths(false, false);
            }
            void LegacyRenderer::reset()
            {
                m_moves_count = 0;
                for (TBuffer& buffer : m_buffers) {
                    buffer.reset();
                }
                m_tools.m_tool_colors = std::vector<Color>();
                m_tools.m_tool_visibles = std::vector<bool>();
                //BBS: always load shell at preview
                //m_shells.volumes.clear();
                m_layers.reset();
                m_custom_gcode_per_print_z = std::vector<CustomGCode::Item>();
#if ENABLE_GCODE_VIEWER_STATISTICS
                m_statistics.reset_all();
#endif // ENABLE_GCODE_VIEWER_STATISTICS
                BaseRenderer::reset();
            }
            //BBS: GUI refactor: add canvas width and height
            void LegacyRenderer::render(int canvas_width, int canvas_height, int right_margin)
            {
#if ENABLE_GCODE_VIEWER_STATISTICS
                m_statistics.reset_opengl();
                m_statistics.total_instances_gpu_size = 0;
#endif // ENABLE_GCODE_VIEWER_STATISTICS
                //BBS: always render shells in preview window
                glsafe(::glEnable(GL_DEPTH_TEST));
                render_shells();
                m_legend_height = 0.0f;
                if (m_roles.empty())
                    return;
                render_toolpaths();
                //render_shells();
                render_legend(m_legend_height, canvas_width, canvas_height, right_margin);
                if (m_user_mode != wxGetApp().get_mode()) {
                    update_by_mode(wxGetApp().get_mode());
                    m_user_mode = wxGetApp().get_mode();
                }
                render_sequential_view(canvas_width, canvas_height, right_margin);
#if ENABLE_GCODE_VIEWER_STATISTICS
                render_statistics();
#endif // ENABLE_GCODE_VIEWER_STATISTICS
                //BBS render slider
                render_slider(canvas_width, canvas_height);
            }
#define ENABLE_CALIBRATION_THUMBNAIL_OUTPUT 0
#if ENABLE_CALIBRATION_THUMBNAIL_OUTPUT
            static void debug_calibration_output_thumbnail(const ThumbnailData& thumbnail_data)
            {
                // debug export of generated image
                wxImage image(thumbnail_data.width, thumbnail_data.height);
                image.InitAlpha();
                for (unsigned int r = 0; r < thumbnail_data.height; ++r)
                {
                    unsigned int rr = (thumbnail_data.height - 1 - r) * thumbnail_data.width;
                    for (unsigned int c = 0; c < thumbnail_data.width; ++c)
                    {
                        unsigned char* px = (unsigned char*)thumbnail_data.pixels.data() + 4 * (rr + c);
                        image.SetRGB((int)c, (int)r, px[0], px[1], px[2]);
                        image.SetAlpha((int)c, (int)r, px[3]);
                    }
                }
                image.SaveFile("D:/calibrate.png", wxBITMAP_TYPE_PNG);
            }
#endif
            const int MAX_DRAWS_PER_BATCH = 1024;
            void LegacyRenderer::_render_calibration_thumbnail_internal(ThumbnailData& thumbnail_data, const ThumbnailsParams& thumbnail_params, PartPlateList& partplate_list, OpenGLManager& opengl_manager)
            {
                int plate_idx = thumbnail_params.plate_id;
                PartPlate* plate = partplate_list.get_plate(plate_idx);
                BoundingBoxf3 plate_box = plate->get_bounding_box(false);
                plate_box.min.z() = 0.0;
                plate_box.max.z() = 0.0;
                Vec3d center = plate_box.center();
#if 1
                Camera camera;
                camera.apply_viewport(0, 0, thumbnail_data.width, thumbnail_data.height);
                camera.set_scene_box(plate_box);
                camera.set_type(Camera::EType::Ortho);
                camera.set_target(center);
                camera.select_view("top");
                camera.zoom_to_box(plate_box, 1.0f);
                camera.apply_projection(plate_box);
                    auto render_as_instanced_model = [
#if ENABLE_GCODE_VIEWER_STATISTICS
                        this
#endif // ENABLE_GCODE_VIEWER_STATISTICS
                    ](TBuffer& buffer, GLShaderProgram& shader) {
                        for (auto& range : buffer.model.instances.render_ranges.ranges) {
                            if (range.vbo == 0 && range.count > 0) {
                                glsafe(::glGenBuffers(1, &range.vbo));
                                glsafe(::glBindBuffer(GL_ARRAY_BUFFER, range.vbo));
                                glsafe(::glBufferData(GL_ARRAY_BUFFER, range.count * buffer.model.instances.instance_size_bytes(), (const void*)&buffer.model.instances.buffer[range.offset * buffer.model.instances.instance_size_floats()], GL_STATIC_DRAW));
                                glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
                            }
                            if (range.vbo > 0) {
                                buffer.model.model.set_color(-1, range.color);
                                buffer.model.model.render_instanced(range.vbo, range.count);
#if ENABLE_GCODE_VIEWER_STATISTICS
                                ++m_statistics.gl_instanced_models_calls_count;
                                m_statistics.total_instances_gpu_size += static_cast<int64_t>(range.count * buffer.model.instances.instance_size_bytes());
#endif // ENABLE_GCODE_VIEWER_STATISTICS
                            }
                        }
                        };
#if ENABLE_GCODE_VIEWER_STATISTICS
                        auto render_as_batched_model = [this](TBuffer& buffer, GLShaderProgram& shader, int position_id, int normal_id) {
#else
                        auto render_as_batched_model = [](TBuffer& buffer, GLShaderProgram& shader, int position_id, int normal_id) {
#endif // ENABLE_GCODE_VIEWER_STATISTICS
                            struct Range
                            {
                                unsigned int first;
                                unsigned int last;
                                bool intersects(const Range& other) const { return (other.last < first || other.first > last) ? false : true; }
                            };
                            Range buffer_range = { 0, 0 };
                            size_t indices_per_instance = buffer.model.data.indices_count();
                            for (size_t j = 0; j < buffer.indices.size(); ++j) {
                                const IBuffer& i_buffer = buffer.indices[j];
                                buffer_range.last = buffer_range.first + i_buffer.count / indices_per_instance;
                                glsafe(::glBindBuffer(GL_ARRAY_BUFFER, i_buffer.vbo));
                                if (position_id != -1) {
                                    glsafe(::glVertexAttribPointer(position_id, buffer.vertices.position_size_floats(), GL_FLOAT, GL_FALSE, buffer.vertices.vertex_size_bytes(), (const void*)buffer.vertices.position_offset_bytes()));
                                    glsafe(::glEnableVertexAttribArray(position_id));
                                }
                                bool has_normals = buffer.vertices.normal_size_floats() > 0;
                                if (has_normals) {
                                    if (normal_id != -1) {
                                        glsafe(::glVertexAttribPointer(normal_id, buffer.vertices.normal_size_floats(), GL_FLOAT, GL_FALSE, buffer.vertices.vertex_size_bytes(), (const void*)buffer.vertices.normal_offset_bytes()));
                                        glsafe(::glEnableVertexAttribArray(normal_id));
                                    }
                                }
                                glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, i_buffer.ibo));
                                for (auto& range : buffer.model.instances.render_ranges.ranges) {
                                    Range range_range = { range.offset, range.offset + range.count };
                                    if (range_range.intersects(buffer_range)) {
                                        shader.set_uniform("uniform_color", range.color);
                                        unsigned int offset = (range_range.first > buffer_range.first) ? range_range.first - buffer_range.first : 0;
                                        size_t offset_bytes = static_cast<size_t>(offset) * indices_per_instance * sizeof(IBufferType);
                                        Range render_range = { std::max(range_range.first, buffer_range.first), std::min(range_range.last, buffer_range.last) };
                                        size_t count = static_cast<size_t>(render_range.last - render_range.first) * indices_per_instance;
                                        if (count > 0) {
                                            glsafe(::glDrawElements(GL_TRIANGLES, (GLsizei)count, GL_UNSIGNED_SHORT, (const void*)offset_bytes));
#if ENABLE_GCODE_VIEWER_STATISTICS
                                            ++m_statistics.gl_batched_models_calls_count;
#endif // ENABLE_GCODE_VIEWER_STATISTICS
                                        }
                                    }
                                }
                                glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
                                if (normal_id != -1)
                                    glsafe(::glDisableVertexAttribArray(normal_id));
                                if (position_id != -1)
                                    glsafe(::glDisableVertexAttribArray(position_id));
                                glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
                                buffer_range.first = buffer_range.last;
                            }
                            };
                        unsigned char begin_id = buffer_id(EMoveType::Retract);
                        unsigned char end_id = buffer_id(EMoveType::Count);
                        BOOST_LOG_TRIVIAL(info) << boost::format("render_calibration_thumbnail: begin_id %1%, end_id %2%") % begin_id % end_id;
                        for (unsigned char i = begin_id; i < end_id; ++i) {
                            TBuffer& buffer = m_buffers[i];
                            if (!buffer.visible || !buffer.has_data())
                                continue;
                            const auto& shader = opengl_manager.get_shader("flat");
                            if (shader != nullptr) {
                                opengl_manager.bind_shader(shader);
                                const auto& view_matrix = camera.get_view_matrix();
                                const auto& proj_matrix = camera.get_projection_matrix();
                                shader->set_uniform("view_model_matrix", view_matrix);
                                shader->set_uniform("projection_matrix", proj_matrix);
                                int position_id = shader->get_attrib_location("v_position");
                                int normal_id = shader->get_attrib_location("v_normal");
                                if (buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::InstancedModel) {
                                    //shader->set_uniform("emission_factor", 0.25f);
                                    render_as_instanced_model(buffer, *shader);
                                    //shader->set_uniform("emission_factor", 0.0f);
                                }
                                else if (buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::BatchedModel) {
                                    //shader->set_uniform("emission_factor", 0.25f);
                                    render_as_batched_model(buffer, *shader, position_id, normal_id);
                                    //shader->set_uniform("emission_factor", 0.0f);
                                }
                                else {
                                    //switch (buffer.render_primitive_type) {
                                    //default: break;
                                    //}
                                    int uniform_color = shader->get_uniform_location("uniform_color");
                                    auto it_path = buffer.render_paths.begin();
                                    for (unsigned int ibuffer_id = 0; ibuffer_id < static_cast<unsigned int>(buffer.indices.size()); ++ibuffer_id) {
                                        const IBuffer& i_buffer = buffer.indices[ibuffer_id];
                                        // Skip all paths with ibuffer_id < ibuffer_id.
                                        for (; it_path != buffer.render_paths.end() && it_path->ibuffer_id < ibuffer_id; ++it_path);
                                        if (it_path == buffer.render_paths.end() || it_path->ibuffer_id > ibuffer_id)
                                            // Not found. This shall not happen.
                                            continue;
                                        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, i_buffer.vbo));
                                        if (position_id != -1) {
                                            glsafe(::glVertexAttribPointer(position_id, buffer.vertices.position_size_floats(), GL_FLOAT, GL_FALSE, buffer.vertices.vertex_size_bytes(), (const void*)buffer.vertices.position_offset_bytes()));
                                            glsafe(::glEnableVertexAttribArray(position_id));
                                        }
                                        bool has_normals = false;// buffer.vertices.normal_size_floats() > 0;
                                        if (has_normals) {
                                            if (normal_id != -1) {
                                                glsafe(::glVertexAttribPointer(normal_id, buffer.vertices.normal_size_floats(), GL_FLOAT, GL_FALSE, buffer.vertices.vertex_size_bytes(), (const void*)buffer.vertices.normal_offset_bytes()));
                                                glsafe(::glEnableVertexAttribArray(normal_id));
                                            }
                                        }
                                        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, i_buffer.ibo));
                                        // Render all elements with it_path->ibuffer_id == ibuffer_id, possible with varying colors.
                                        switch (buffer.render_primitive_type)
                                        {
                                        case TBuffer::ERenderPrimitiveType::Triangle: {
                                            render_sub_paths(it_path, buffer.render_paths.end(), *shader, uniform_color, (unsigned int)EDrawPrimitiveType::Triangles);
                                            break;
                                        }
                                        default: { break; }
                                        }
                                        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
                                        if (normal_id != -1)
                                            glsafe(::glDisableVertexAttribArray(normal_id));
                                        if (position_id != -1)
                                            glsafe(::glDisableVertexAttribArray(position_id));
                                        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
                                    }
                                }
                                opengl_manager.unbind_shader();
                            }
                            else {
                                BOOST_LOG_TRIVIAL(info) << boost::format("render_calibration_thumbnail: can not find shader");
                            }
                        }
#endif
                        BOOST_LOG_TRIVIAL(info) << boost::format("render_calibration_thumbnail: exit");
                            }
            void LegacyRenderer::_render_calibration_thumbnail_framebuffer(ThumbnailData & thumbnail_data, unsigned int w, unsigned int h, const ThumbnailsParams & thumbnail_params, PartPlateList & partplate_list, OpenGLManager & opengl_manager)
            {
                BOOST_LOG_TRIVIAL(info) << boost::format("render_calibration_thumbnail prepare: width %1%, height %2%") % w % h;
                thumbnail_data.set(w, h);
                if (!thumbnail_data.is_valid())
                    return;
                //TODO bool multisample = m_multisample_allowed;
                bool multisample = OpenGLManager::can_multisample();
                //if (!multisample)
                //    glsafe(::glEnable(GL_MULTISAMPLE));
                GLint max_samples;
                glsafe(::glGetIntegerv(GL_MAX_SAMPLES, &max_samples));
                GLsizei num_samples = max_samples / 2;
                GLuint render_fbo;
                glsafe(::glGenFramebuffers(1, &render_fbo));
                glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, render_fbo));
                BOOST_LOG_TRIVIAL(info) << boost::format("render_calibration_thumbnail prepare: max_samples %1%, multisample %2%, render_fbo %3%") % max_samples % multisample % render_fbo;
                GLuint render_tex = 0;
                GLuint render_tex_buffer = 0;
                if (multisample) {
                    // use renderbuffer instead of texture to avoid the need to use glTexImage2DMultisample which is available only since OpenGL 3.2
                    glsafe(::glGenRenderbuffers(1, &render_tex_buffer));
                    glsafe(::glBindRenderbuffer(GL_RENDERBUFFER, render_tex_buffer));
                    glsafe(::glRenderbufferStorageMultisample(GL_RENDERBUFFER, num_samples, GL_RGBA8, w, h));
                    glsafe(::glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, render_tex_buffer));
                }
                else {
                    glsafe(::glGenTextures(1, &render_tex));
                    glsafe(::glBindTexture(GL_TEXTURE_2D, render_tex));
                    glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr));
                    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
                    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
                    glsafe(::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, render_tex, 0));
                }
                GLuint render_depth;
                glsafe(::glGenRenderbuffers(1, &render_depth));
                glsafe(::glBindRenderbuffer(GL_RENDERBUFFER, render_depth));
                if (multisample)
                    glsafe(::glRenderbufferStorageMultisample(GL_RENDERBUFFER, num_samples, GL_DEPTH_COMPONENT24, w, h));
                else
                    glsafe(::glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, w, h));
                glsafe(::glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, render_depth));
                GLenum drawBufs[] = { GL_COLOR_ATTACHMENT0 };
                glsafe(::glDrawBuffers(1, drawBufs));
                if (::glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
                    _render_calibration_thumbnail_internal(thumbnail_data, thumbnail_params, partplate_list, opengl_manager);
                    if (multisample) {
                        GLuint resolve_fbo;
                        glsafe(::glGenFramebuffers(1, &resolve_fbo));
                        glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, resolve_fbo));
                        GLuint resolve_tex;
                        glsafe(::glGenTextures(1, &resolve_tex));
                        glsafe(::glBindTexture(GL_TEXTURE_2D, resolve_tex));
                        glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr));
                        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
                        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
                        glsafe(::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolve_tex, 0));
                        glsafe(::glDrawBuffers(1, drawBufs));
                        if (::glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
                            glsafe(::glBindFramebuffer(GL_READ_FRAMEBUFFER, render_fbo));
                            glsafe(::glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolve_fbo));
                            glsafe(::glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_LINEAR));
                            glsafe(::glBindFramebuffer(GL_READ_FRAMEBUFFER, resolve_fbo));
                            glsafe(::glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, (void*)thumbnail_data.pixels.data()));
                        }
                        glsafe(::glDeleteTextures(1, &resolve_tex));
                        glsafe(::glDeleteFramebuffers(1, &resolve_fbo));
                    }
                    else
                        glsafe(::glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, (void*)thumbnail_data.pixels.data()));
                }
#if ENABLE_CALIBRATION_THUMBNAIL_OUTPUT
                debug_calibration_output_thumbnail(thumbnail_data);
#endif
                glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, 0));
                glsafe(::glDeleteRenderbuffers(1, &render_depth));
                if (render_tex_buffer != 0)
                    glsafe(::glDeleteRenderbuffers(1, &render_tex_buffer));
                if (render_tex != 0)
                    glsafe(::glDeleteTextures(1, &render_tex));
                glsafe(::glDeleteFramebuffers(1, &render_fbo));
                //if (!multisample)
                //    glsafe(::glDisable(GL_MULTISAMPLE));
                BOOST_LOG_TRIVIAL(info) << boost::format("render_calibration_thumbnail prepare: exit");
            }
            //BBS
            void LegacyRenderer::render_calibration_thumbnail(ThumbnailData & thumbnail_data, unsigned int w, unsigned int h, const ThumbnailsParams & thumbnail_params, PartPlateList & partplate_list, OpenGLManager & opengl_manager)
            {
                // reset values and refresh render
                int       last_view_type_sel = m_view_type_sel;
                EViewType last_view_type = m_view_type;
                unsigned int last_role_visibility_flags = m_p_extrusions->role_visibility_flags;
                // set color scheme to FilamentId
                for (int i = 0; i < view_type_items.size(); i++) {
                    if (view_type_items[i] == EViewType::FilamentId) {
                        m_view_type_sel = i;
                        break;
                    }
                }
                set_view_type(EViewType::FilamentId, false);
                // set m_layers_z_range to 0, 1;
                // To be safe, we include both layers here although layer 1 seems enough
                // layer 0: custom extrusions such as flow calibration etc.
                // layer 1: the real first layer of object
                std::array<unsigned int, 2> tmp_layers_z_range = m_layers_z_range;
                m_layers_z_range = { 0, 1 };
                // BBS exclude feature types
                m_p_extrusions->role_visibility_flags = m_p_extrusions->role_visibility_flags & ~(1 << erSkirt);
                m_p_extrusions->role_visibility_flags = m_p_extrusions->role_visibility_flags & ~(1 << erCustom);
                // BBS include feature types
                m_p_extrusions->role_visibility_flags = m_p_extrusions->role_visibility_flags | (1 << erWipeTower);
                m_p_extrusions->role_visibility_flags = m_p_extrusions->role_visibility_flags | (1 << erPerimeter);
                m_p_extrusions->role_visibility_flags = m_p_extrusions->role_visibility_flags | (1 << erExternalPerimeter);
                m_p_extrusions->role_visibility_flags = m_p_extrusions->role_visibility_flags | (1 << erOverhangPerimeter);
                m_p_extrusions->role_visibility_flags = m_p_extrusions->role_visibility_flags | (1 << erSolidInfill);
                m_p_extrusions->role_visibility_flags = m_p_extrusions->role_visibility_flags | (1 << erFloatingVerticalShell);
                m_p_extrusions->role_visibility_flags = m_p_extrusions->role_visibility_flags | (1 << erTopSolidInfill);
                m_p_extrusions->role_visibility_flags = m_p_extrusions->role_visibility_flags | (1 << erInternalInfill);
                m_p_extrusions->role_visibility_flags = m_p_extrusions->role_visibility_flags | (1 << erBottomSurface);
                refresh_render_paths(false, false);
                _render_calibration_thumbnail_framebuffer(thumbnail_data, w, h, thumbnail_params, partplate_list, opengl_manager);
                // restore values and refresh render
                // reset m_layers_z_range and view type
                m_view_type_sel = last_view_type_sel;
                set_view_type(last_view_type, false);
                m_layers_z_range = tmp_layers_z_range;
                m_p_extrusions->role_visibility_flags = last_role_visibility_flags;
                refresh_render_paths(false, false);
            }
            bool LegacyRenderer::can_export_toolpaths() const
            {
                return has_data() && m_buffers[buffer_id(EMoveType::Extrude)].render_primitive_type == TBuffer::ERenderPrimitiveType::Triangle;
            }
            void LegacyRenderer::update_sequential_view_current(unsigned int first, unsigned int last)
            {
                const auto& p_sequential_view = get_sequential_view();
                if (!p_sequential_view) {
                    return;
                }
                auto is_visible = [this](unsigned int id) {
                    for (const TBuffer& buffer : m_buffers) {
                        if (buffer.visible) {
                            for (const Path& path : buffer.paths) {
                                if (path.sub_paths.front().first.s_id <= id && id <= path.sub_paths.back().last.s_id) return true;
                            }
                        }
                    }
                    return false;
                    };
                const int first_diff = static_cast<int>(first) - static_cast<int>(p_sequential_view->last_current.first);
                const int last_diff = static_cast<int>(last) - static_cast<int>(p_sequential_view->last_current.last);
                unsigned int new_first = first;
                unsigned int new_last = last;
                if (p_sequential_view->skip_invisible_moves) {
                    while (!is_visible(new_first)) {
                        if (first_diff > 0)
                            ++new_first;
                        else
                            --new_first;
                    }
                    while (!is_visible(new_last)) {
                        if (last_diff > 0)
                            ++new_last;
                        else
                            --new_last;
                    }
                }
                p_sequential_view->current.first = new_first;
                p_sequential_view->current.last = new_last;
                p_sequential_view->last_current = p_sequential_view->current;
                refresh_render_paths(true, true);
                if (new_first != first || new_last != last) {
                    update_moves_slider();
                }
            }
            void LegacyRenderer::enable_moves_slider(bool enable) const
            {
                bool render_as_disabled = !enable;
                if (m_moves_slider != nullptr && m_moves_slider->is_rendering_as_disabled() != render_as_disabled) {
                    m_moves_slider->set_render_as_disabled(render_as_disabled);
                    m_moves_slider->set_as_dirty();
                }
            }
            void LegacyRenderer::update_layers_slider_mode()
            {
                //    true  -> single-extruder printer profile OR
                //             multi-extruder printer profile , but whole model is printed by only one extruder
                //    false -> multi-extruder printer profile , and model is printed by several extruders
                bool one_extruder_printed_model = true;
                // extruder used for whole model for multi-extruder printer profile
                int only_extruder = -1;
                // BBS
                if (wxGetApp().filaments_cnt() > 1) {
                    const ModelObjectPtrs& objects = wxGetApp().plater()->model().objects;
                    // check if whole model uses just only one extruder
                    if (!objects.empty()) {
                        const int extruder = objects[0]->config.has("extruder") ? objects[0]->config.option("extruder")->getInt() : 0;
                        auto is_one_extruder_printed_model = [objects, extruder]() {
                            for (ModelObject* object : objects) {
                                if (object->config.has("extruder") && object->config.option("extruder")->getInt() != extruder) return false;
                                for (ModelVolume* volume : object->volumes)
                                    if ((volume->config.has("extruder") && volume->config.option("extruder")->getInt() != extruder) || !volume->mmu_segmentation_facets.empty()) return false;
                                for (const auto& range : object->layer_config_ranges)
                                    if (range.second.has("extruder") && range.second.option("extruder")->getInt() != extruder) return false;
                            }
                            return true;
                            };
                        if (is_one_extruder_printed_model())
                            only_extruder = extruder;
                        else
                            one_extruder_printed_model = false;
                    }
                }
                // TODO m_layers_slider->SetModeAndOnlyExtruder(one_extruder_printed_model, only_extruder);
            }

            bool LegacyRenderer::is_move_type_visible(EMoveType type) const
            {
                size_t id = static_cast<size_t>(buffer_id(type));
                return (id < m_buffers.size()) ? m_buffers[id].visible : false;
            }
            void LegacyRenderer::set_move_type_visible(EMoveType type, bool visible)
            {
                size_t id = static_cast<size_t>(buffer_id(type));
                if (id < m_buffers.size())
                    m_buffers[id].visible = visible;
            }
            unsigned int LegacyRenderer::get_options_visibility_flags() const
            {
                auto set_flag = [](unsigned int flags, unsigned int flag, bool active) {
                    return active ? (flags | (1 << flag)) : flags;
                    };
                unsigned int flags = 0;
                flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::Travel), is_move_type_visible(EMoveType::Travel));
                flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::Wipe), is_move_type_visible(EMoveType::Wipe));
                flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::Retractions), is_move_type_visible(EMoveType::Retract));
                flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::Unretractions), is_move_type_visible(EMoveType::Unretract));
                flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::Seams), is_move_type_visible(EMoveType::Seam));
                flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::ToolChanges), is_move_type_visible(EMoveType::Tool_change));
                flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::ColorChanges), is_move_type_visible(EMoveType::Color_change));
                flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::PausePrints), is_move_type_visible(EMoveType::Pause_Print));
                flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::CustomGCodes), is_move_type_visible(EMoveType::Custom_GCode));
                flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::Shells), m_shells.visible);
                const auto& p_sequential_view = get_sequential_view();
                if (p_sequential_view) {
                    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::ToolMarker), p_sequential_view->marker.is_visible());
                }
                flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::Legend), is_legend_enabled());
                return flags;
            }
            void LegacyRenderer::set_options_visibility_from_flags(unsigned int flags)
            {
                auto is_flag_set = [flags](unsigned int flag) {
                    return (flags & (1 << flag)) != 0;
                    };
                set_move_type_visible(EMoveType::Travel, is_flag_set(static_cast<unsigned int>(Preview::OptionType::Travel)));
                set_move_type_visible(EMoveType::Wipe, is_flag_set(static_cast<unsigned int>(Preview::OptionType::Wipe)));
                set_move_type_visible(EMoveType::Retract, is_flag_set(static_cast<unsigned int>(Preview::OptionType::Retractions)));
                set_move_type_visible(EMoveType::Unretract, is_flag_set(static_cast<unsigned int>(Preview::OptionType::Unretractions)));
                set_move_type_visible(EMoveType::Seam, is_flag_set(static_cast<unsigned int>(Preview::OptionType::Seams)));
                set_move_type_visible(EMoveType::Tool_change, is_flag_set(static_cast<unsigned int>(Preview::OptionType::ToolChanges)));
                set_move_type_visible(EMoveType::Color_change, is_flag_set(static_cast<unsigned int>(Preview::OptionType::ColorChanges)));
                set_move_type_visible(EMoveType::Pause_Print, is_flag_set(static_cast<unsigned int>(Preview::OptionType::PausePrints)));
                set_move_type_visible(EMoveType::Custom_GCode, is_flag_set(static_cast<unsigned int>(Preview::OptionType::CustomGCodes)));
                m_shells.visible = is_flag_set(static_cast<unsigned int>(Preview::OptionType::Shells));
                const auto& p_sequential_view = get_sequential_view();
                if (p_sequential_view) {
                    p_sequential_view->marker.set_visible(is_flag_set(static_cast<unsigned int>(Preview::OptionType::ToolMarker)));
                }
                enable_legend(is_flag_set(static_cast<unsigned int>(Preview::OptionType::Legend)));
            }
            void LegacyRenderer::set_layers_z_range(const std::array<unsigned int, 2>&layers_z_range)
            {
                bool keep_sequential_current_first = layers_z_range[0] >= m_layers_z_range[0];
                bool keep_sequential_current_last = layers_z_range[1] <= m_layers_z_range[1];
                m_layers_z_range = layers_z_range;
                refresh_render_paths(keep_sequential_current_first, keep_sequential_current_last);
                update_moves_slider(true);
            }
            void LegacyRenderer::export_toolpaths_to_obj(const char* filename) const
            {
                if (filename == nullptr)
                    return;
                if (!has_data())
                    return;
                wxBusyCursor busy;
                // the data needed is contained into the Extrude TBuffer
                const TBuffer& t_buffer = m_buffers[buffer_id(EMoveType::Extrude)];
                if (!t_buffer.has_data())
                    return;
                if (t_buffer.render_primitive_type != TBuffer::ERenderPrimitiveType::Triangle)
                    return;
                // collect color information to generate materials
                std::vector<Color> colors;
                for (const RenderPath& path : t_buffer.render_paths) {
                    colors.push_back(path.color);
                }
                sort_remove_duplicates(colors);
                // save materials file
                boost::filesystem::path mat_filename(filename);
                mat_filename.replace_extension("mtl");
                CNumericLocalesSetter locales_setter;
                FILE* fp = boost::nowide::fopen(mat_filename.string().c_str(), "w");
                if (fp == nullptr) {
                    BOOST_LOG_TRIVIAL(error) << "LegacyRenderer::export_toolpaths_to_obj: Couldn't open " << mat_filename.string().c_str() << " for writing";
                    return;
                }
                fprintf(fp, "# G-Code Toolpaths Materials\n");
                fprintf(fp, "# Generated by %s-%s based on Slic3r\n", SLIC3R_APP_NAME, SLIC3R_VERSION);
                unsigned int colors_count = 1;
                for (const Color& color : colors) {
                    fprintf(fp, "\nnewmtl material_%d\n", colors_count++);
                    fprintf(fp, "Ka 1 1 1\n");
                    fprintf(fp, "Kd %g %g %g\n", color[0], color[1], color[2]);
                    fprintf(fp, "Ks 0 0 0\n");
                }
                fclose(fp);
                // save geometry file
                fp = boost::nowide::fopen(filename, "w");
                if (fp == nullptr) {
                    BOOST_LOG_TRIVIAL(error) << "LegacyRenderer::export_toolpaths_to_obj: Couldn't open " << filename << " for writing";
                    return;
                }
                fprintf(fp, "# G-Code Toolpaths\n");
                fprintf(fp, "# Generated by %s-%s based on Slic3r\n", SLIC3R_APP_NAME, SLIC3R_VERSION);
                fprintf(fp, "\nmtllib ./%s\n", mat_filename.filename().string().c_str());
                const size_t floats_per_vertex = t_buffer.vertices.vertex_size_floats();
                std::vector<Vec3f> out_vertices;
                std::vector<Vec3f> out_normals;
                struct VerticesOffset
                {
                    unsigned int vbo;
                    size_t offset;
                };
                std::vector<VerticesOffset> vertices_offsets;
                vertices_offsets.push_back({ t_buffer.vertices.vbos.front(), 0 });
                // get vertices/normals data from vertex buffers on gpu
                for (size_t i = 0; i < t_buffer.vertices.vbos.size(); ++i) {
                    const size_t floats_count = t_buffer.vertices.sizes[i] / sizeof(float);
                    VertexBuffer vertices(floats_count);
                    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, t_buffer.vertices.vbos[i]));
                    glsafe(::glGetBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(t_buffer.vertices.sizes[i]), static_cast<void*>(vertices.data())));
                    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
                    const size_t vertices_count = floats_count / floats_per_vertex;
                    for (size_t j = 0; j < vertices_count; ++j) {
                        const size_t base = j * floats_per_vertex;
                        out_vertices.push_back({ vertices[base + 0], vertices[base + 1], vertices[base + 2] });
                        out_normals.push_back({ vertices[base + 3], vertices[base + 4], vertices[base + 5] });
                    }
                    if (i < t_buffer.vertices.vbos.size() - 1)
                        vertices_offsets.push_back({ t_buffer.vertices.vbos[i + 1], vertices_offsets.back().offset + vertices_count });
                }
                // save vertices to file
                fprintf(fp, "\n# vertices\n");
                for (const Vec3f& v : out_vertices) {
                    fprintf(fp, "v %g %g %g\n", v.x(), v.y(), v.z());
                }
                // save normals to file
                fprintf(fp, "\n# normals\n");
                for (const Vec3f& n : out_normals) {
                    fprintf(fp, "vn %g %g %g\n", n.x(), n.y(), n.z());
                }
                size_t i = 0;
                for (const Color& color : colors) {
                    // save material triangles to file
                    fprintf(fp, "\nusemtl material_%zu\n", i + 1);
                    fprintf(fp, "# triangles material %zu\n", i + 1);
                    for (const RenderPath& render_path : t_buffer.render_paths) {
                        if (render_path.color != color)
                            continue;
                        const IBuffer& ibuffer = t_buffer.indices[render_path.ibuffer_id];
                        size_t vertices_offset = 0;
                        for (size_t j = 0; j < vertices_offsets.size(); ++j) {
                            const VerticesOffset& offset = vertices_offsets[j];
                            if (offset.vbo == ibuffer.vbo) {
                                vertices_offset = offset.offset;
                                break;
                            }
                        }
                        // get indices data from index buffer on gpu
                        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibuffer.ibo));
                        for (size_t j = 0; j < render_path.sizes.size(); ++j) {
                            IndexBuffer indices(render_path.sizes[j]);
                            glsafe(::glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLintptr>(render_path.offsets[j]),
                                static_cast<GLsizeiptr>(render_path.sizes[j] * sizeof(IBufferType)), static_cast<void*>(indices.data())));
                            const size_t triangles_count = render_path.sizes[j] / 3;
                            for (size_t k = 0; k < triangles_count; ++k) {
                                const size_t base = k * 3;
                                const size_t v1 = 1 + static_cast<size_t>(indices[base + 0]) + vertices_offset;
                                const size_t v2 = 1 + static_cast<size_t>(indices[base + 1]) + vertices_offset;
                                const size_t v3 = 1 + static_cast<size_t>(indices[base + 2]) + vertices_offset;
                                if (v1 != v2)
                                    // do not export dummy triangles
                                    fprintf(fp, "f %zu//%zu %zu//%zu %zu//%zu\n", v1, v1, v2, v2, v3, v3);
                            }
                        }
                        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
                    }
                    ++i;
                }
                fclose(fp);
            }

            bool LegacyRenderer::is_extrusion_role_visible(ExtrusionRole role) const
            {
                if (!m_p_extrusions) {
                    return false;
                }
                return role < erCount && (m_p_extrusions->role_visibility_flags & (1 << role)) != 0;
            }

            void LegacyRenderer::set_extrusion_role_visible(ExtrusionRole role, bool is_visible)
            {
                if (!m_p_extrusions) {
                    return;
                }
                if (is_visible) {
                    m_p_extrusions->role_visibility_flags |= (1 << role);
                }
                else {
                    m_p_extrusions->role_visibility_flags = m_p_extrusions->role_visibility_flags &= ~(1 << role);
                }
            }

            uint32_t LegacyRenderer::get_extrusion_role_visibility_flags() const
            {
                if (!m_p_extrusions) {
                    return 0;
                }
                return m_p_extrusions->role_visibility_flags;
            }

            void LegacyRenderer::set_extrusion_role_visibility_flags(uint32_t flags)
            {
                if (!m_p_extrusions) {
                    return;
                }
                m_p_extrusions->role_visibility_flags = flags;
            }

            bool LegacyRenderer::load_toolpaths(const GCodeProcessorResult & gcode_result, const BuildVolume & build_volume, const std::vector<BoundingBoxf3>&exclude_bounding_box)
            {
                // max index buffer size, in bytes
                static const size_t IBUFFER_THRESHOLD_BYTES = 64 * 1024 * 1024;
                //BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(",build_volume center{%1%, %2%}, moves count %3%\n")%build_volume.bed_center().x() % build_volume.bed_center().y() %gcode_result.moves.size();
                auto log_memory_usage = [this](const std::string& label, const std::vector<MultiVertexBuffer>& vertices, const std::vector<MultiIndexBuffer>& indices) {
                    int64_t vertices_size = 0;
                    for (const MultiVertexBuffer& buffers : vertices) {
                        for (const VertexBuffer& buffer : buffers) {
                            vertices_size += SLIC3R_STDVEC_MEMSIZE(buffer, float);
                        }
                        //BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format("vertices count %1%\n")%buffers.size();
                    }
                    int64_t indices_size = 0;
                    for (const MultiIndexBuffer& buffers : indices) {
                        for (const IndexBuffer& buffer : buffers) {
                            indices_size += SLIC3R_STDVEC_MEMSIZE(buffer, IBufferType);
                        }
                        //BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format("indices count %1%\n")%buffers.size();
                    }
                    log_memory_used(label, vertices_size + indices_size);
                    };
                // format data into the buffers to be rendered as points
                auto add_vertices_as_point = [](const GCodeProcessorResult::MoveVertex& curr, VertexBuffer& vertices) {
                    vertices.push_back(curr.position.x());
                    vertices.push_back(curr.position.y());
                    vertices.push_back(curr.position.z());
                    };
                auto add_indices_as_point = [](const GCodeProcessorResult::MoveVertex& curr, TBuffer& buffer,
                    unsigned int ibuffer_id, IndexBuffer& indices, size_t move_id) {
                        buffer.add_path(curr, ibuffer_id, indices.size(), move_id);
                        indices.push_back(static_cast<IBufferType>(indices.size()));
                    };
                // format data into the buffers to be rendered as lines
                auto add_vertices_as_line = [](const GCodeProcessorResult::MoveVertex& prev, const GCodeProcessorResult::MoveVertex& curr, VertexBuffer& vertices) {
                    auto add_vertex = [&vertices](const Vec3f& position, const Vec3f& normal) {
                        // add position
                        vertices.push_back(position.x());
                        vertices.push_back(position.y());
                        vertices.push_back(position.z());
                        // add normal
                        vertices.push_back(normal.x());
                        vertices.push_back(normal.y());
                        vertices.push_back(normal.z());
                        };
                    // x component of the normal to the current segment (the normal is parallel to the XY plane)
                    //BBS: Has modified a lot for this function to support arc move
                    size_t loop_num = curr.is_arc_move_with_interpolation_points() ? curr.interpolation_points.size() : 0;
                    for (size_t i = 0; i < loop_num + 1; i++) {
                        const Vec3f& previous = (i == 0 ? prev.position : curr.interpolation_points[i - 1]);
                        const Vec3f& current = (i == loop_num ? curr.position : curr.interpolation_points[i]);
                        const Vec3f dir = (current - previous).normalized();
                        Vec3f normal(dir.y(), -dir.x(), 0.0);
                        normal.normalize();
                        // add previous vertex
                        add_vertex(previous, normal);
                        // add current vertex
                        add_vertex(current, normal);
                    }
                    };
                //BBS: modify a lot to support arc travel
                auto add_indices_as_line = [](const GCodeProcessorResult::MoveVertex& prev, const GCodeProcessorResult::MoveVertex& curr, TBuffer& buffer,
                    size_t& vbuffer_size, unsigned int ibuffer_id, IndexBuffer& indices, size_t move_id) {
                        if (buffer.paths.empty() || prev.type != curr.type || !buffer.paths.back().matches(curr)) {
                            buffer.add_path(curr, ibuffer_id, indices.size(), move_id - 1);
                            buffer.paths.back().sub_paths.front().first.position = prev.position;
                        }
                        Path& last_path = buffer.paths.back();
                        size_t loop_num = curr.is_arc_move_with_interpolation_points() ? curr.interpolation_points.size() : 0;
                        for (size_t i = 0; i < loop_num + 1; i++) {
                            //BBS: add previous index
                            indices.push_back(static_cast<IBufferType>(indices.size()));
                            //BBS: add current index
                            indices.push_back(static_cast<IBufferType>(indices.size()));
                            vbuffer_size += buffer.max_vertices_per_segment();
                        }
                        last_path.sub_paths.back().last = { ibuffer_id, indices.size() - 1, move_id, curr.position };
                    };
                // format data into the buffers to be rendered as solid.
                auto add_vertices_as_solid = [](const GCodeProcessorResult::MoveVertex& prev, const GCodeProcessorResult::MoveVertex& curr, TBuffer& buffer, unsigned int vbuffer_id, VertexBuffer& vertices, size_t move_id) {
                    auto store_vertex = [](VertexBuffer& vertices, const Vec3f& position, const Vec3f& normal) {
                        // append position
                        vertices.push_back(position.x());
                        vertices.push_back(position.y());
                        vertices.push_back(position.z());
                        // append normal
                        vertices.push_back(normal.x());
                        vertices.push_back(normal.y());
                        vertices.push_back(normal.z());
                        };
                    if (buffer.paths.empty() || prev.type != curr.type || !buffer.paths.back().matches(curr)) {
                        buffer.add_path(curr, vbuffer_id, vertices.size(), move_id - 1);
                        buffer.paths.back().sub_paths.back().first.position = prev.position;
                    }
                    Path& last_path = buffer.paths.back();
                    //BBS: Has modified a lot for this function to support arc move
                    size_t loop_num = curr.is_arc_move_with_interpolation_points() ? curr.interpolation_points.size() : 0;
                    for (size_t i = 0; i < loop_num + 1; i++) {
                        const Vec3f& prev_position = (i == 0 ? prev.position : curr.interpolation_points[i - 1]);
                        const Vec3f& curr_position = (i == loop_num ? curr.position : curr.interpolation_points[i]);
                        const Vec3f dir = (curr_position - prev_position).normalized();
                        const Vec3f right = Vec3f(dir.y(), -dir.x(), 0.0f).normalized();
                        const Vec3f left = -right;
                        const Vec3f up = right.cross(dir);
                        const Vec3f down = -up;
                        const float half_width = 0.5f * last_path.width;
                        const float half_height = 0.5f * last_path.height;
                        const Vec3f prev_pos = prev_position - half_height * up;
                        const Vec3f curr_pos = curr_position - half_height * up;
                        const Vec3f d_up = half_height * up;
                        const Vec3f d_down = -half_height * up;
                        const Vec3f d_right = half_width * right;
                        const Vec3f d_left = -half_width * right;
                        if ((last_path.vertices_count() == 1 || vertices.empty()) && i == 0) {
                            store_vertex(vertices, prev_pos + d_up, up);
                            store_vertex(vertices, prev_pos + d_right, right);
                            store_vertex(vertices, prev_pos + d_down, down);
                            store_vertex(vertices, prev_pos + d_left, left);
                        }
                        else {
                            store_vertex(vertices, prev_pos + d_right, right);
                            store_vertex(vertices, prev_pos + d_left, left);
                        }
                        store_vertex(vertices, curr_pos + d_up, up);
                        store_vertex(vertices, curr_pos + d_right, right);
                        store_vertex(vertices, curr_pos + d_down, down);
                        store_vertex(vertices, curr_pos + d_left, left);
                    }
                    last_path.sub_paths.back().last = { vbuffer_id, vertices.size(), move_id, curr.position };
                    };
                auto add_indices_as_solid = [&](const GCodeProcessorResult::MoveVertex& prev, const GCodeProcessorResult::MoveVertex& curr, const GCodeProcessorResult::MoveVertex* next,
                    TBuffer& buffer, size_t& vbuffer_size, unsigned int ibuffer_id, IndexBuffer& indices, size_t move_id) {
                        static Vec3f prev_dir;
                        static Vec3f prev_up;
                        static float sq_prev_length;
                        auto store_triangle = [](IndexBuffer& indices, IBufferType i1, IBufferType i2, IBufferType i3) {
                            indices.push_back(i1);
                            indices.push_back(i2);
                            indices.push_back(i3);
                            };
                        auto append_dummy_cap = [store_triangle](IndexBuffer& indices, IBufferType id) {
                            store_triangle(indices, id, id, id);
                            store_triangle(indices, id, id, id);
                            };
                        auto convert_vertices_offset = [](size_t vbuffer_size, const std::array<int, 8>& v_offsets) {
                            std::array<IBufferType, 8> ret = {
                                static_cast<IBufferType>(static_cast<int>(vbuffer_size) + v_offsets[0]),
                                static_cast<IBufferType>(static_cast<int>(vbuffer_size) + v_offsets[1]),
                                static_cast<IBufferType>(static_cast<int>(vbuffer_size) + v_offsets[2]),
                                static_cast<IBufferType>(static_cast<int>(vbuffer_size) + v_offsets[3]),
                                static_cast<IBufferType>(static_cast<int>(vbuffer_size) + v_offsets[4]),
                                static_cast<IBufferType>(static_cast<int>(vbuffer_size) + v_offsets[5]),
                                static_cast<IBufferType>(static_cast<int>(vbuffer_size) + v_offsets[6]),
                                static_cast<IBufferType>(static_cast<int>(vbuffer_size) + v_offsets[7])
                            };
                            return ret;
                            };
                        auto append_starting_cap_triangles = [&](IndexBuffer& indices, const std::array<IBufferType, 8>& v_offsets) {
                            store_triangle(indices, v_offsets[0], v_offsets[2], v_offsets[1]);
                            store_triangle(indices, v_offsets[0], v_offsets[3], v_offsets[2]);
                            };
                        auto append_stem_triangles = [&](IndexBuffer& indices, const std::array<IBufferType, 8>& v_offsets) {
                            store_triangle(indices, v_offsets[0], v_offsets[1], v_offsets[4]);
                            store_triangle(indices, v_offsets[1], v_offsets[5], v_offsets[4]);
                            store_triangle(indices, v_offsets[1], v_offsets[2], v_offsets[5]);
                            store_triangle(indices, v_offsets[2], v_offsets[6], v_offsets[5]);
                            store_triangle(indices, v_offsets[2], v_offsets[3], v_offsets[6]);
                            store_triangle(indices, v_offsets[3], v_offsets[7], v_offsets[6]);
                            store_triangle(indices, v_offsets[3], v_offsets[0], v_offsets[7]);
                            store_triangle(indices, v_offsets[0], v_offsets[4], v_offsets[7]);
                            };
                        auto append_ending_cap_triangles = [&](IndexBuffer& indices, const std::array<IBufferType, 8>& v_offsets) {
                            store_triangle(indices, v_offsets[4], v_offsets[6], v_offsets[7]);
                            store_triangle(indices, v_offsets[4], v_offsets[5], v_offsets[6]);
                            };
                        if (buffer.paths.empty() || prev.type != curr.type || !buffer.paths.back().matches(curr)) {
                            buffer.add_path(curr, ibuffer_id, indices.size(), move_id - 1);
                            buffer.paths.back().sub_paths.back().first.position = prev.position;
                        }
                        Path& last_path = buffer.paths.back();
                        bool is_first_segment = (last_path.vertices_count() == 1);
                        //BBS: has modified a lot for this function to support arc move
                        std::array<IBufferType, 8> first_seg_v_offsets = convert_vertices_offset(vbuffer_size, { 0, 1, 2, 3, 4, 5, 6, 7 });
                        std::array<IBufferType, 8> non_first_seg_v_offsets = convert_vertices_offset(vbuffer_size, { -4, 0, -2, 1, 2, 3, 4, 5 });
                        size_t loop_num = curr.is_arc_move_with_interpolation_points() ? curr.interpolation_points.size() : 0;
                        for (size_t i = 0; i < loop_num + 1; i++) {
                            const Vec3f& prev_position = (i == 0 ? prev.position : curr.interpolation_points[i - 1]);
                            const Vec3f& curr_position = (i == loop_num ? curr.position : curr.interpolation_points[i]);
                            const Vec3f dir = (curr_position - prev_position).normalized();
                            const Vec3f right = Vec3f(dir.y(), -dir.x(), 0.0f).normalized();
                            const Vec3f up = right.cross(dir);
                            const float sq_length = (curr_position - prev_position).squaredNorm();
                            if ((is_first_segment || vbuffer_size == 0) && i == 0) {
                                append_starting_cap_triangles(indices, first_seg_v_offsets);
                                // stem triangles
                                append_stem_triangles(indices, first_seg_v_offsets);

                                // ending cap triangles
                                append_ending_cap_triangles(indices, first_seg_v_offsets);

                                // dummy triangles outer corner cap
                                append_dummy_cap(indices, vbuffer_size);
                                append_dummy_cap(indices, vbuffer_size);

                                vbuffer_size += 8;
                            }
                            else {
                                // stem triangles
                                non_first_seg_v_offsets = convert_vertices_offset(vbuffer_size, { -4, 0, -2, 1, 2, 3, 4, 5 });

                                append_starting_cap_triangles(indices, non_first_seg_v_offsets);
                                append_stem_triangles(indices, non_first_seg_v_offsets);
                                append_ending_cap_triangles(indices, non_first_seg_v_offsets);

                                store_triangle(indices, vbuffer_size - 4, vbuffer_size + 1, vbuffer_size - 1);
                                store_triangle(indices, vbuffer_size + 1, vbuffer_size - 2, vbuffer_size - 1);

                                store_triangle(indices, vbuffer_size - 4, vbuffer_size - 3, vbuffer_size + 0);
                                store_triangle(indices, vbuffer_size - 3, vbuffer_size - 2, vbuffer_size + 0);

                                vbuffer_size += 6;
                            }
                        }
                        last_path.sub_paths.back().last = { ibuffer_id, indices.size() - 1, move_id, curr.position };
                    };
                // format data into the buffers to be rendered as instanced model
                auto add_model_instance = [](const GCodeProcessorResult::MoveVertex& curr, InstanceBuffer& instances, InstanceIdBuffer& instances_ids, size_t move_id) {
                    // append position
                    instances.push_back(curr.position.x());
                    instances.push_back(curr.position.y());
                    instances.push_back(curr.position.z());
                    // append width
                    instances.push_back(curr.width);
                    // append height
                    instances.push_back(curr.height);
                    // append id
                    instances_ids.push_back(move_id);
                    };
                // format data into the buffers to be rendered as batched model
                auto add_vertices_as_model_batch = [](const GCodeProcessorResult::MoveVertex& curr, const GLModel::InitializationData& data, VertexBuffer& vertices, InstanceBuffer& instances, InstanceIdBuffer& instances_ids, size_t move_id) {
                    const double width = static_cast<double>(1.5f * curr.width);
                    const double height = static_cast<double>(1.5f * curr.height);
                    const Transform3d trafo = Geometry::assemble_transform((curr.position - 0.5f * curr.height * Vec3f::UnitZ()).cast<double>(), Vec3d::Zero(), { width, width, height });
                    const Eigen::Matrix<double, 3, 3, Eigen::DontAlign> normal_matrix = trafo.matrix().template block<3, 3>(0, 0).inverse().transpose();
                    for (const auto& entity : data.entities) {
                        // append vertices
                        for (size_t i = 0; i < entity.positions.size(); ++i) {
                            // append position
                            const Vec3d position = trafo * entity.positions[i].cast<double>();
                            vertices.push_back(static_cast<float>(position.x()));
                            vertices.push_back(static_cast<float>(position.y()));
                            vertices.push_back(static_cast<float>(position.z()));
                            // append normal
                            const Vec3d normal = normal_matrix * entity.normals[i].cast<double>();
                            vertices.push_back(static_cast<float>(normal.x()));
                            vertices.push_back(static_cast<float>(normal.y()));
                            vertices.push_back(static_cast<float>(normal.z()));
                        }
                    }
                    // append instance position
                    instances.push_back(curr.position.x());
                    instances.push_back(curr.position.y());
                    instances.push_back(curr.position.z());
                    // append instance id
                    instances_ids.push_back(move_id);
                    };
                auto add_indices_as_model_batch = [](const GLModel::InitializationData& data, IndexBuffer& indices, IBufferType base_index) {
                    for (const auto& entity : data.entities) {
                        for (size_t i = 0; i < entity.indices.size(); ++i) {
                            indices.push_back(static_cast<IBufferType>(entity.indices[i] + base_index));
                        }
                    }
                    };
#if ENABLE_GCODE_VIEWER_STATISTICS
                auto start_time = std::chrono::high_resolution_clock::now();
                m_statistics.results_size = SLIC3R_STDVEC_MEMSIZE(gcode_result.moves, GCodeProcessorResult::MoveVertex);
                m_statistics.results_time = gcode_result.time;
#endif // ENABLE_GCODE_VIEWER_STATISTICS
                m_moves_count = gcode_result.moves.size();
                if (m_moves_count == 0)
                    return false;
                m_extruders_count = gcode_result.filaments_count;
                unsigned int progress_count = 0;
                static const unsigned int progress_threshold = 1000;
                //BBS: add only gcode mode
                ProgressDialog* progress_dialog = m_only_gcode_in_preview ?
                    new ProgressDialog(_L("Loading G-codes"), "...",
                        100, wxGetApp().mainframe, wxPD_AUTO_HIDE | wxPD_APP_MODAL) : nullptr;
                wxBusyCursor busy;
                //BBS: use convex_hull for toolpath outside check
                Points pts;
                // extract approximate paths bounding box from result
                //BBS: add only gcode mode
                for (const GCodeProcessorResult::MoveVertex& move : gcode_result.moves) {
                    //if (wxGetApp().is_gcode_viewer()) {
                    //if (m_only_gcode_in_preview) {
                        // for the gcode viewer we need to take in account all moves to correctly size the printbed
                    //    m_paths_bounding_box.merge(move.position.cast<double>());
                    //}
                    //else {
                    if (move.type == EMoveType::Extrude && move.extrusion_role != erCustom && move.width != 0.0f && move.height != 0.0f) {
                        m_paths_bounding_box.merge(move.position.cast<double>());
                        //BBS: use convex_hull for toolpath outside check
                        pts.emplace_back(Point(scale_(move.position.x()), scale_(move.position.y())));
                    }
                    //}
                }
                // BBS: also merge the point on arc to bounding box
                for (const GCodeProcessorResult::MoveVertex& move : gcode_result.moves) {
                    // continue if not arc path
                    if (!move.is_arc_move_with_interpolation_points())
                        continue;
                    //if (wxGetApp().is_gcode_viewer())
                    //if (m_only_gcode_in_preview)
                    //    for (int i = 0; i < move.interpolation_points.size(); i++)
                    //        m_paths_bounding_box.merge(move.interpolation_points[i].cast<double>());
                    //else {
                    if (move.type == EMoveType::Extrude && move.width != 0.0f && move.height != 0.0f)
                        for (int i = 0; i < move.interpolation_points.size(); i++) {
                            m_paths_bounding_box.merge(move.interpolation_points[i].cast<double>());
                            //BBS: use convex_hull for toolpath outside check
                            pts.emplace_back(Point(scale_(move.interpolation_points[i].x()), scale_(move.interpolation_points[i].y())));
                        }
                    //}
                }
                // set approximate max bounding box (take in account also the tool marker)
                m_max_bounding_box = m_paths_bounding_box;
                const auto& p_sequential_view = get_sequential_view();
                if (p_sequential_view) {
                    m_max_bounding_box.merge(m_paths_bounding_box.max + p_sequential_view->marker.get_bounding_box().size().z() * Vec3d::UnitZ());
                }
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(",m_paths_bounding_box {%1%, %2%}-{%3%, %4%}\n")
                    % m_paths_bounding_box.min.x() % m_paths_bounding_box.min.y() % m_paths_bounding_box.max.x() % m_paths_bounding_box.max.y();
                //if (wxGetApp().is_editor())
                {
                    //BBS: use convex_hull for toolpath outside check
                    m_contained_in_bed = build_volume.all_paths_inside(gcode_result, m_paths_bounding_box);
                    if (m_contained_in_bed) {
                        //PartPlateList& partplate_list = wxGetApp().plater()->get_partplate_list();
                        //PartPlate* plate = partplate_list.get_curr_plate();
                        //const std::vector<BoundingBoxf3>& exclude_bounding_box = plate->get_exclude_areas();
                        if (exclude_bounding_box.size() > 0)
                        {
                            int index;
                            Slic3r::Polygon convex_hull_2d = Slic3r::Geometry::convex_hull(std::move(pts));
                            for (index = 0; index < exclude_bounding_box.size(); index++)
                            {
                                Slic3r::Polygon p = exclude_bounding_box[index].polygon(true);  // instance convex hull is scaled, so we need to scale here
                                if (intersection({ p }, { convex_hull_2d }).empty() == false)
                                {
                                    m_contained_in_bed = false;
                                    break;
                                }
                            }
                        }
                    }
                    (const_cast<GCodeProcessorResult&>(gcode_result)).toolpath_outside = !m_contained_in_bed;
                }
                if (p_sequential_view) {
                    p_sequential_view->gcode_ids.clear();
                    for (size_t i = 0; i < gcode_result.moves.size(); ++i) {
                        const GCodeProcessorResult::MoveVertex& move = gcode_result.moves[i];
                        if (move.type != EMoveType::Seam)
                            p_sequential_view->gcode_ids.push_back(move.gcode_id);
                    }
                }
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(",m_contained_in_bed %1%\n") % m_contained_in_bed;
                std::vector<MultiVertexBuffer> vertices(m_buffers.size());
                std::vector<MultiIndexBuffer> indices(m_buffers.size());
                std::vector<InstanceBuffer> instances(m_buffers.size());
                std::vector<InstanceIdBuffer> instances_ids(m_buffers.size());
                std::vector<InstancesOffsets> instances_offsets(m_buffers.size());
                std::vector<float> options_zs;
                size_t seams_count = 0;
                std::vector<size_t> biased_seams_ids;
                // toolpaths data -> extract vertices from result
                for (size_t i = 0; i < m_moves_count; ++i) {
                    const GCodeProcessorResult::MoveVertex& curr = gcode_result.moves[i];
                    if (curr.type == EMoveType::Seam) {
                        ++seams_count;
                        biased_seams_ids.push_back(i - biased_seams_ids.size() - 1);
                    }
                    size_t move_id = i - seams_count;
                    // skip first vertex
                    if (i == 0)
                        continue;
                    const GCodeProcessorResult::MoveVertex& prev = gcode_result.moves[i - 1];
                    // update progress dialog
                    ++progress_count;
                    if (progress_dialog != nullptr && progress_count % progress_threshold == 0) {
                        progress_dialog->Update(int(100.0f * float(i) / (2.0f * float(m_moves_count))),
                            _L("Generating geometry vertex data") + ": " + wxNumberFormatter::ToString(100.0 * double(i) / double(m_moves_count), 0, wxNumberFormatter::Style_None) + "%");
                        progress_dialog->Fit();
                        progress_count = 0;
                    }
                    const unsigned char id = buffer_id(curr.type);
                    if (id >= m_buffers.size()) {//Add an array protection
                        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << " m_buffers array bound";
                        continue;
                    }
                    TBuffer& t_buffer = m_buffers[id];
                    MultiVertexBuffer& v_multibuffer = vertices[id];
                    InstanceBuffer& inst_buffer = instances[id];
                    InstanceIdBuffer& inst_id_buffer = instances_ids[id];
                    InstancesOffsets& inst_offsets = instances_offsets[id];
                    /*if (i%1000 == 1) {
                        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(":i=%1%, buffer_id %2% render_type %3%, gcode_id %4%\n")
                            %i %(int)id %(int)t_buffer.render_primitive_type %curr.gcode_id;
                    }*/
                    // ensure there is at least one vertex buffer
                    if (v_multibuffer.empty())
                        v_multibuffer.push_back(VertexBuffer());
                    // if adding the vertices for the current segment exceeds the threshold size of the current vertex buffer
                    // add another vertex buffer
                    // BBS: get the point number and then judge whether the remaining buffer is enough
                    size_t points_num = curr.is_arc_move_with_interpolation_points() ? curr.interpolation_points.size() + 1 : 1;
                    size_t vertices_size_to_add = (t_buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::BatchedModel) ? t_buffer.model.data.vertices_size_bytes() : points_num * t_buffer.max_vertices_per_segment_size_bytes();
                    if (v_multibuffer.back().size() * sizeof(float) > t_buffer.vertices.max_size_bytes() - vertices_size_to_add) {
                        v_multibuffer.push_back(VertexBuffer());
                        if (t_buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::Triangle) {
                            Path& last_path = t_buffer.paths.back();
                            if (prev.type == curr.type && last_path.matches(curr))
                                last_path.add_sub_path(prev, static_cast<unsigned int>(v_multibuffer.size()) - 1, 0, move_id - 1);
                        }
                    }
                    VertexBuffer& v_buffer = v_multibuffer.back();
                    switch (t_buffer.render_primitive_type)
                    {
                    case TBuffer::ERenderPrimitiveType::Point: { add_vertices_as_point(curr, v_buffer); break; }
                    case TBuffer::ERenderPrimitiveType::Line: { add_vertices_as_line(prev, curr, v_buffer); break; }
                    case TBuffer::ERenderPrimitiveType::Triangle: { add_vertices_as_solid(prev, curr, t_buffer, static_cast<unsigned int>(v_multibuffer.size()) - 1, v_buffer, move_id); break; }
                    case TBuffer::ERenderPrimitiveType::InstancedModel:
                    {
                        add_model_instance(curr, inst_buffer, inst_id_buffer, move_id);
                        inst_offsets.push_back(prev.position - curr.position);
#if ENABLE_GCODE_VIEWER_STATISTICS
                        ++m_statistics.instances_count;
#endif // ENABLE_GCODE_VIEWER_STATISTICS
                        break;
                    }
                    case TBuffer::ERenderPrimitiveType::BatchedModel:
                    {
                        add_vertices_as_model_batch(curr, t_buffer.model.data, v_buffer, inst_buffer, inst_id_buffer, move_id);
                        inst_offsets.push_back(prev.position - curr.position);
#if ENABLE_GCODE_VIEWER_STATISTICS
                        ++m_statistics.batched_count;
#endif // ENABLE_GCODE_VIEWER_STATISTICS
                        break;
                    }
                    }
                    // collect options zs for later use
                    if (curr.type == EMoveType::Pause_Print || curr.type == EMoveType::Custom_GCode) {
                        const float* const last_z = options_zs.empty() ? nullptr : &options_zs.back();
                        if (last_z == nullptr || curr.position[2] < *last_z - EPSILON || *last_z + EPSILON < curr.position[2])
                            options_zs.emplace_back(curr.position[2]);
                    }
                }
                /*for (size_t b = 0; b < vertices.size(); ++b) {
                    MultiVertexBuffer& v_multibuffer = vertices[b];
                    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(":b=%1%, vertex buffer count %2%\n")
                        %b %v_multibuffer.size();
                }*/
                auto extract_move_id = [&biased_seams_ids](size_t id) {
                    size_t new_id = size_t(-1);
                    auto it = std::lower_bound(biased_seams_ids.begin(), biased_seams_ids.end(), id);
                    if (it == biased_seams_ids.end())
                        new_id = id + biased_seams_ids.size();
                    else {
                        if (it == biased_seams_ids.begin() && *it < id)
                            new_id = id;
                        else if (it != biased_seams_ids.begin())
                            new_id = id + std::distance(biased_seams_ids.begin(), it);
                    }
                    return (new_id == size_t(-1)) ? id : new_id;
                    };
                //BBS: generate map from ssid to move id in advance to reduce computation
                m_ssid_to_moveid_map.clear();
                m_ssid_to_moveid_map.reserve(m_moves_count - biased_seams_ids.size());
                for (size_t i = 0; i < m_moves_count - biased_seams_ids.size(); i++)
                    m_ssid_to_moveid_map.push_back(extract_move_id(i));
#if ENABLE_GCODE_VIEWER_STATISTICS
                auto load_vertices_time = std::chrono::high_resolution_clock::now();
                m_statistics.load_vertices = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
#endif // ENABLE_GCODE_VIEWER_STATISTICS
                // dismiss, no more needed
                std::vector<size_t>().swap(biased_seams_ids);
                for (MultiVertexBuffer& v_multibuffer : vertices) {
                    for (VertexBuffer& v_buffer : v_multibuffer) {
                        v_buffer.shrink_to_fit();
                    }
                }
                // move the wipe toolpaths half height up to render them on proper position
                MultiVertexBuffer& wipe_vertices = vertices[buffer_id(EMoveType::Wipe)];
                for (VertexBuffer& v_buffer : wipe_vertices) {
                    for (size_t i = 2; i < v_buffer.size(); i += 3) {
                        v_buffer[i] += 0.5f * GCodeProcessor::Wipe_Height;
                    }
                }
                // send vertices data to gpu, where needed
                for (size_t i = 0; i < m_buffers.size(); ++i) {
                    TBuffer& t_buffer = m_buffers[i];
                    if (t_buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::InstancedModel) {
                        const InstanceBuffer& inst_buffer = instances[i];
                        if (!inst_buffer.empty()) {
                            t_buffer.model.instances.buffer = inst_buffer;
                            t_buffer.model.instances.s_ids = instances_ids[i];
                            t_buffer.model.instances.offsets = instances_offsets[i];
                        }
                    }
                    else {
                        if (t_buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::BatchedModel) {
                            const InstanceBuffer& inst_buffer = instances[i];
                            if (!inst_buffer.empty()) {
                                t_buffer.model.instances.buffer = inst_buffer;
                                t_buffer.model.instances.s_ids = instances_ids[i];
                                t_buffer.model.instances.offsets = instances_offsets[i];
                            }
                        }
                        const MultiVertexBuffer& v_multibuffer = vertices[i];
                        for (const VertexBuffer& v_buffer : v_multibuffer) {
                            const size_t size_elements = v_buffer.size();
                            const size_t size_bytes = size_elements * sizeof(float);
                            const size_t vertices_count = size_elements / t_buffer.vertices.vertex_size_floats();
                            t_buffer.vertices.count += vertices_count;
#if ENABLE_GCODE_VIEWER_STATISTICS
                            m_statistics.total_vertices_gpu_size += static_cast<int64_t>(size_bytes);
                            m_statistics.max_vbuffer_gpu_size = std::max(m_statistics.max_vbuffer_gpu_size, static_cast<int64_t>(size_bytes));
                            ++m_statistics.vbuffers_count;
#endif // ENABLE_GCODE_VIEWER_STATISTICS
                            GLuint id = 0;
                            glsafe(::glGenBuffers(1, &id));
                            glsafe(::glBindBuffer(GL_ARRAY_BUFFER, id));
                            glsafe(::glBufferData(GL_ARRAY_BUFFER, size_bytes, v_buffer.data(), GL_STATIC_DRAW));
                            glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
                            t_buffer.vertices.vbos.push_back(static_cast<unsigned int>(id));
                            t_buffer.vertices.sizes.push_back(size_bytes);
                        }
                    }
                }
#if ENABLE_GCODE_VIEWER_STATISTICS
                auto smooth_vertices_time = std::chrono::high_resolution_clock::now();
                m_statistics.smooth_vertices = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - load_vertices_time).count();
#endif // ENABLE_GCODE_VIEWER_STATISTICS
                log_memory_usage("Loaded G-code generated vertex buffers ", vertices, indices);
                // dismiss vertices data, no more needed
                std::vector<MultiVertexBuffer>().swap(vertices);
                std::vector<InstanceBuffer>().swap(instances);
                std::vector<InstanceIdBuffer>().swap(instances_ids);
                // toolpaths data -> extract indices from result
                // paths may have been filled while extracting vertices,
                // so reset them, they will be filled again while extracting indices
                for (TBuffer& buffer : m_buffers) {
                    buffer.paths.clear();
                }
                // variable used to keep track of the current vertex buffers index and size
                using CurrVertexBuffer = std::pair<unsigned int, size_t>;
                std::vector<CurrVertexBuffer> curr_vertex_buffers(m_buffers.size(), { 0, 0 });
                // variable used to keep track of the vertex buffers ids
                using VboIndexList = std::vector<unsigned int>;
                std::vector<VboIndexList> vbo_indices(m_buffers.size());
                seams_count = 0;
                for (size_t i = 0; i < m_moves_count; ++i) {
                    const GCodeProcessorResult::MoveVertex& curr = gcode_result.moves[i];
                    if (curr.type == EMoveType::Seam)
                        ++seams_count;
                    size_t move_id = i - seams_count;
                    // skip first vertex
                    if (i == 0)
                        continue;
                    const GCodeProcessorResult::MoveVertex& prev = gcode_result.moves[i - 1];
                    const GCodeProcessorResult::MoveVertex* next = nullptr;
                    if (i < m_moves_count - 1)
                        next = &gcode_result.moves[i + 1];
                    ++progress_count;
                    if (progress_dialog != nullptr && progress_count % progress_threshold == 0) {
                        progress_dialog->Update(int(100.0f * float(m_moves_count + i) / (2.0f * float(m_moves_count))),
                            _L("Generating geometry index data") + ": " + wxNumberFormatter::ToString(100.0 * double(i) / double(m_moves_count), 0, wxNumberFormatter::Style_None) + "%");
                        progress_dialog->Fit();
                        progress_count = 0;
                    }
                    const unsigned char id = buffer_id(curr.type);
                    TBuffer& t_buffer = m_buffers[id];
                    MultiIndexBuffer& i_multibuffer = indices[id];
                    CurrVertexBuffer& curr_vertex_buffer = curr_vertex_buffers[id];
                    VboIndexList& vbo_index_list = vbo_indices[id];
                    // ensure there is at least one index buffer
                    if (i_multibuffer.empty()) {
                        i_multibuffer.push_back(IndexBuffer());
                        if (!t_buffer.vertices.vbos.empty())
                            vbo_index_list.push_back(t_buffer.vertices.vbos[curr_vertex_buffer.first]);
                    }
                    // if adding the indices for the current segment exceeds the threshold size of the current index buffer
                    // create another index buffer
                    // BBS: get the point number and then judge whether the remaining buffer is enough
                    size_t points_num = curr.is_arc_move_with_interpolation_points() ? curr.interpolation_points.size() + 1 : 1;
                    size_t indiced_size_to_add = (t_buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::BatchedModel) ? t_buffer.model.data.indices_size_bytes() : points_num * t_buffer.indices_per_segment_size_bytes();
                    if (i_multibuffer.back().size() * sizeof(IBufferType) >= IBUFFER_THRESHOLD_BYTES - indiced_size_to_add) {
                        i_multibuffer.push_back(IndexBuffer());
                        vbo_index_list.push_back(t_buffer.vertices.vbos[curr_vertex_buffer.first]);
                        if (t_buffer.render_primitive_type != TBuffer::ERenderPrimitiveType::Point &&
                            t_buffer.render_primitive_type != TBuffer::ERenderPrimitiveType::BatchedModel) {
                            Path& last_path = t_buffer.paths.back();
                            last_path.add_sub_path(prev, static_cast<unsigned int>(i_multibuffer.size()) - 1, 0, move_id - 1);
                        }
                    }
                    // if adding the vertices for the current segment exceeds the threshold size of the current vertex buffer
                    // create another index buffer
                    // BBS: support multi points in one MoveVertice, should multiply point number
                    size_t vertices_size_to_add = (t_buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::BatchedModel) ? t_buffer.model.data.vertices_size_bytes() : points_num * t_buffer.max_vertices_per_segment_size_bytes();
                    if (curr_vertex_buffer.second * t_buffer.vertices.vertex_size_bytes() > t_buffer.vertices.max_size_bytes() - vertices_size_to_add) {
                        i_multibuffer.push_back(IndexBuffer());
                        ++curr_vertex_buffer.first;
                        curr_vertex_buffer.second = 0;
                        vbo_index_list.push_back(t_buffer.vertices.vbos[curr_vertex_buffer.first]);
                        if (t_buffer.render_primitive_type != TBuffer::ERenderPrimitiveType::Point &&
                            t_buffer.render_primitive_type != TBuffer::ERenderPrimitiveType::BatchedModel) {
                            Path& last_path = t_buffer.paths.back();
                            last_path.add_sub_path(prev, static_cast<unsigned int>(i_multibuffer.size()) - 1, 0, move_id - 1);
                        }
                    }
                    IndexBuffer& i_buffer = i_multibuffer.back();
                    switch (t_buffer.render_primitive_type)
                    {
                    case TBuffer::ERenderPrimitiveType::Point: {
                        add_indices_as_point(curr, t_buffer, static_cast<unsigned int>(i_multibuffer.size()) - 1, i_buffer, move_id);
                        curr_vertex_buffer.second += t_buffer.max_vertices_per_segment();
                        break;
                    }
                    case TBuffer::ERenderPrimitiveType::Line: {
                        add_indices_as_line(prev, curr, t_buffer, curr_vertex_buffer.second, static_cast<unsigned int>(i_multibuffer.size()) - 1, i_buffer, move_id);
                        break;
                    }
                    case TBuffer::ERenderPrimitiveType::Triangle: {
                        add_indices_as_solid(prev, curr, next, t_buffer, curr_vertex_buffer.second, static_cast<unsigned int>(i_multibuffer.size()) - 1, i_buffer, move_id);
                        break;
                    }
                    case TBuffer::ERenderPrimitiveType::BatchedModel: {
                        add_indices_as_model_batch(t_buffer.model.data, i_buffer, curr_vertex_buffer.second);
                        curr_vertex_buffer.second += t_buffer.model.data.vertices_count();
                        break;
                    }
                    default: { break; }
                    }
                }
                for (MultiIndexBuffer& i_multibuffer : indices) {
                    for (IndexBuffer& i_buffer : i_multibuffer) {
                        i_buffer.shrink_to_fit();
                    }
                }
                // toolpaths data -> send indices data to gpu
                for (size_t i = 0; i < m_buffers.size(); ++i) {
                    TBuffer& t_buffer = m_buffers[i];
                    if (t_buffer.render_primitive_type != TBuffer::ERenderPrimitiveType::InstancedModel) {
                        const MultiIndexBuffer& i_multibuffer = indices[i];
                        for (const IndexBuffer& i_buffer : i_multibuffer) {
                            const size_t size_elements = i_buffer.size();
                            const size_t size_bytes = size_elements * sizeof(IBufferType);
                            // stores index buffer informations into TBuffer
                            t_buffer.indices.push_back(IBuffer());
                            IBuffer& ibuf = t_buffer.indices.back();
                            ibuf.count = size_elements;
                            ibuf.vbo = vbo_indices[i][t_buffer.indices.size() - 1];
#if ENABLE_GCODE_VIEWER_STATISTICS
                            m_statistics.total_indices_gpu_size += static_cast<int64_t>(size_bytes);
                            m_statistics.max_ibuffer_gpu_size = std::max(m_statistics.max_ibuffer_gpu_size, static_cast<int64_t>(size_bytes));
                            ++m_statistics.ibuffers_count;
#endif // ENABLE_GCODE_VIEWER_STATISTICS
                            glsafe(::glGenBuffers(1, &ibuf.ibo));
                            glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibuf.ibo));
                            glsafe(::glBufferData(GL_ELEMENT_ARRAY_BUFFER, size_bytes, i_buffer.data(), GL_STATIC_DRAW));
                            glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
                        }
                    }
                }
                if (progress_dialog != nullptr) {
                    progress_dialog->Update(100, "");
                    progress_dialog->Fit();
                }
#if ENABLE_GCODE_VIEWER_STATISTICS
                for (const TBuffer& buffer : m_buffers) {
                    m_statistics.paths_size += SLIC3R_STDVEC_MEMSIZE(buffer.paths, Path);
                }
                auto update_segments_count = [&](EMoveType type, int64_t& count) {
                    unsigned int id = buffer_id(type);
                    const MultiIndexBuffer& buffers = indices[id];
                    int64_t indices_count = 0;
                    for (const IndexBuffer& buffer : buffers) {
                        indices_count += buffer.size();
                    }
                    const TBuffer& t_buffer = m_buffers[id];
                    if (t_buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::Triangle)
                        indices_count -= static_cast<int64_t>(12 * t_buffer.paths.size()); // remove the starting + ending caps = 4 triangles
                    count += indices_count / t_buffer.indices_per_segment();
                    };
                update_segments_count(EMoveType::Travel, m_statistics.travel_segments_count);
                update_segments_count(EMoveType::Wipe, m_statistics.wipe_segments_count);
                update_segments_count(EMoveType::Extrude, m_statistics.extrude_segments_count);
                m_statistics.load_indices = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - smooth_vertices_time).count();
#endif // ENABLE_GCODE_VIEWER_STATISTICS
                log_memory_usage("Loaded G-code generated indices buffers ", vertices, indices);
                // dismiss indices data, no more needed
                std::vector<MultiIndexBuffer>().swap(indices);
                // layers zs / roles / extruder ids -> extract from result
                size_t last_travel_s_id = 0;
                seams_count = 0;
                m_extruder_ids.clear();
                for (size_t i = 0; i < m_moves_count; ++i) {
                    const GCodeProcessorResult::MoveVertex& move = gcode_result.moves[i];
                    if (move.type == EMoveType::Seam)
                        ++seams_count;
                    size_t move_id = i - seams_count;
                    if (move.type == EMoveType::Extrude) {
                        // layers zs
                        const double* const last_z = m_layers.empty() ? nullptr : &m_layers.get_zs().back();
                        const double z = static_cast<double>(move.position.z());
                        if (last_z == nullptr || z < *last_z - EPSILON || *last_z + EPSILON < z)
                            m_layers.append(z, { last_travel_s_id, move_id });
                        else
                            m_layers.get_endpoints().back().last = move_id;
                        // extruder ids
                        m_extruder_ids.emplace_back(move.extruder_id);
                        // roles
                        if (i > 0)
                            m_roles.emplace_back(move.extrusion_role);
                    }
                    else if (move.type == EMoveType::Travel) {
                        if (move_id - last_travel_s_id > 0 && !m_layers.empty())
                            m_layers.get_endpoints().back().last = move_id;
                        last_travel_s_id = move_id;
                    }
                    else if (move.type == EMoveType::Unretract && move.extrusion_role == ExtrusionRole::erFlush) {
                        m_roles.emplace_back(move.extrusion_role);
                    }
                }
                // roles -> remove duplicates
                sort_remove_duplicates(m_roles);
                m_roles.shrink_to_fit();
                // extruder ids -> remove duplicates
                sort_remove_duplicates(m_extruder_ids);
                m_extruder_ids.shrink_to_fit();
                std::vector<int> plater_extruder;
                for (auto mid : m_extruder_ids) {
                    int eid = mid;
                    plater_extruder.push_back(++eid);
                }
                m_plater_extruder = plater_extruder;
                // replace layers for spiral vase mode
                if (!gcode_result.spiral_vase_layers.empty()) {
                    m_layers.reset();
                    for (const auto& layer : gcode_result.spiral_vase_layers) {
                        m_layers.append(layer.first, { layer.second.first, layer.second.second });
                    }
                }
                // set layers z range
                if (!m_layers.empty())
                    m_layers_z_range = { 0, static_cast<unsigned int>(m_layers.size() - 1) };
                // change color of paths whose layer contains option points
                if (!options_zs.empty()) {
                    TBuffer& extrude_buffer = m_buffers[buffer_id(EMoveType::Extrude)];
                    for (Path& path : extrude_buffer.paths) {
                        const float z = path.sub_paths.front().first.position.z();
                        if (std::find_if(options_zs.begin(), options_zs.end(), [z](float f) { return f - EPSILON <= z && z <= f + EPSILON; }) != options_zs.end())
                            path.m_b_has_effect_layer = true;
                    }
                }
#if ENABLE_GCODE_VIEWER_STATISTICS
                m_statistics.load_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
#endif // ENABLE_GCODE_VIEWER_STATISTICS
                if (progress_dialog != nullptr)
                    progress_dialog->Destroy();

                m_conflict_result = gcode_result.conflict_result;
                if (m_conflict_result) { m_conflict_result.value().layer = m_layers.get_l_at(m_conflict_result.value()._height); }
                //BBS: add logs
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": finished, m_buffers size %1%!") % m_buffers.size();
                return !m_layers.empty();
            }
            void LegacyRenderer::refresh_render_paths(bool keep_sequential_current_first, bool keep_sequential_current_last) const
            {
#if ENABLE_GCODE_VIEWER_STATISTICS
                auto start_time = std::chrono::high_resolution_clock::now();
#endif // ENABLE_GCODE_VIEWER_STATISTICS
                BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": enter, m_buffers size %1%!") % m_buffers.size();
                auto extrusion_color = [this](const Path& path) {
                    ColorRGBA color;
                    switch (m_view_type)
                    {
                    case EViewType::FeatureType: { color = Extrusion_Role_Colors[static_cast<unsigned int>(path.role)]; break; }
                    case EViewType::Height: { color = m_p_extrusions->ranges.height.get_color_at(path.height); break; }
                    case EViewType::Width: { color = m_p_extrusions->ranges.width.get_color_at(path.width); break; }
                    case EViewType::Feedrate: { color = m_p_extrusions->ranges.feedrate.get_color_at(path.feedrate); break; }
                    case EViewType::FanSpeed: { color = m_p_extrusions->ranges.fan_speed.get_color_at(path.fan_speed); break; }
                    case EViewType::Temperature: { color = m_p_extrusions->ranges.temperature.get_color_at(path.temperature); break; }
                    case EViewType::LayerTime: { color = m_p_extrusions->ranges.layer_duration.get_color_at(path.layer_time, Range::EType::Logarithmic); break; }
                    case EViewType::VolumetricRate: { color = m_p_extrusions->ranges.volumetric_rate.get_color_at(path.volumetric_rate); break; }
                    case EViewType::Tool: { color = m_tools.m_tool_colors[path.extruder_id]; break; }
                    case EViewType::Summary:
                    case EViewType::ColorPrint: {
                        color = m_tools.m_tool_colors[path.cp_color_id];
                        color = adjust_color_for_rendering(color.get_data());
                        break;
                    }
                    case EViewType::FilamentId: {
                        float id = float(path.extruder_id) / 256;
                        float role = float(path.role) / 256;
                        color = { id, role, id, 1.0f };
                        break;
                    }
                    // helio
                    case EViewType::ThermalIndexMin: { color = m_p_extrusions->ranges.thermal_index_min.get_color_at(path.thermal_index_min); break; }
                    case EViewType::ThermalIndexMax: { color = m_p_extrusions->ranges.thermal_index_max.get_color_at(path.thermal_index_max); break; }
                    case EViewType::ThermalIndexMean: { color = m_p_extrusions->ranges.thermal_index_mean.get_color_at(path.thermal_index_mean); break; }
                    // end helio
                    default: { color = { 1.0f, 1.0f, 1.0f, 1.0f }; break; }
                    }
                    return color;
                    };
                auto travel_color = [](const Path& path) {
                    return (path.delta_extruder < 0.0f) ? Travel_Colors[2] /* Retract */ :
                        ((path.delta_extruder > 0.0f) ? Travel_Colors[1] /* Extrude */ :
                            Travel_Colors[0] /* Move */);
                    };
                auto is_in_layers_range = [this](const Path& path, size_t min_id, size_t max_id) {
                    auto in_layers_range = [this, min_id, max_id](size_t id) {
                        return m_layers.get_endpoints_at(min_id).first <= id && id <= m_layers.get_endpoints_at(max_id).last;
                        };
                    return in_layers_range(path.sub_paths.front().first.s_id) && in_layers_range(path.sub_paths.back().last.s_id);
                    };
                //BBS
                auto is_extruder_in_layer_range = [this](const Path& path, size_t extruder_id) {
                    return path.extruder_id == extruder_id;
                    };
                auto is_travel_in_layers_range = [this](size_t path_id, size_t min_id, size_t max_id) {
                    const TBuffer& buffer = m_buffers[buffer_id(EMoveType::Travel)];
                    if (path_id >= buffer.paths.size())
                        return false;
                    Path path = buffer.paths[path_id];
                    size_t first = path_id;
                    size_t last = path_id;
                    // check adjacent paths
                    while (first > 0 && path.sub_paths.front().first.position.isApprox(buffer.paths[first - 1].sub_paths.back().last.position)) {
                        --first;
                        path.sub_paths.front().first = buffer.paths[first].sub_paths.front().first;
                    }
                    while (last < buffer.paths.size() - 1 && path.sub_paths.back().last.position.isApprox(buffer.paths[last + 1].sub_paths.front().first.position)) {
                        ++last;
                        path.sub_paths.back().last = buffer.paths[last].sub_paths.back().last;
                    }
                    const size_t min_s_id = m_layers.get_endpoints_at(min_id).first;
                    const size_t max_s_id = m_layers.get_endpoints_at(max_id).last;
                    return (min_s_id <= path.sub_paths.front().first.s_id && path.sub_paths.front().first.s_id <= max_s_id) &&
                        (min_s_id <= path.sub_paths.back().last.s_id && path.sub_paths.back().last.s_id <= max_s_id);
                    };
#if ENABLE_GCODE_VIEWER_STATISTICS
                Statistics* statistics = const_cast<Statistics*>(&m_statistics);
                statistics->render_paths_size = 0;
                statistics->models_instances_size = 0;
#endif // ENABLE_GCODE_VIEWER_STATISTICS
                const bool top_layer_only = true;
                //BBS
                const auto& p_sequential_view = get_sequential_view();
                if (!p_sequential_view) {
                    return;
                }
                SequentialView::Endpoints global_endpoints = { p_sequential_view->gcode_ids.size() , 0 };
                SequentialView::Endpoints top_layer_endpoints = global_endpoints;
                if (top_layer_only || !keep_sequential_current_first) p_sequential_view->current.first = 0;
                //BBS
                if (!keep_sequential_current_last) p_sequential_view->current.last = p_sequential_view->gcode_ids.size();
                // first pass: collect visible paths and update sequential view data
                std::vector<std::tuple<unsigned char, unsigned int, unsigned int, unsigned int>> paths;
                for (size_t b = 0; b < m_buffers.size(); ++b) {
                    TBuffer& buffer = const_cast<TBuffer&>(m_buffers[b]);
                    // reset render paths
                    buffer.render_paths.clear();
                    if (!buffer.visible)
                        continue;
                    if (buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::InstancedModel ||
                        buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::BatchedModel) {
                        for (size_t id : buffer.model.instances.s_ids) {
                            if (id < m_layers.get_endpoints_at(m_layers_z_range[0]).first || m_layers.get_endpoints_at(m_layers_z_range[1]).last < id)
                                continue;
                            global_endpoints.first = std::min(global_endpoints.first, id);
                            global_endpoints.last = std::max(global_endpoints.last, id);
                            if (top_layer_only) {
                                if (id < m_layers.get_endpoints_at(m_layers_z_range[1]).first || m_layers.get_endpoints_at(m_layers_z_range[1]).last < id)
                                    continue;
                                top_layer_endpoints.first = std::min(top_layer_endpoints.first, id);
                                top_layer_endpoints.last = std::max(top_layer_endpoints.last, id);
                            }
                        }
                    }
                    else {
                        for (size_t i = 0; i < buffer.paths.size(); ++i) {
                            const Path& path = buffer.paths[i];
                            if (path.type == EMoveType::Travel) {
                                if (!is_travel_in_layers_range(i, m_layers_z_range[0], m_layers_z_range[1]))
                                    continue;
                            }
                            else if (!is_in_layers_range(path, m_layers_z_range[0], m_layers_z_range[1]))
                                continue;
                            if (top_layer_only) {
                                if (path.type == EMoveType::Travel) {
                                    if (is_travel_in_layers_range(i, m_layers_z_range[1], m_layers_z_range[1])) {
                                        top_layer_endpoints.first = std::min(top_layer_endpoints.first, path.sub_paths.front().first.s_id);
                                        top_layer_endpoints.last = std::max(top_layer_endpoints.last, path.sub_paths.back().last.s_id);
                                    }
                                }
                                else if (is_in_layers_range(path, m_layers_z_range[1], m_layers_z_range[1])) {
                                    top_layer_endpoints.first = std::min(top_layer_endpoints.first, path.sub_paths.front().first.s_id);
                                    top_layer_endpoints.last = std::max(top_layer_endpoints.last, path.sub_paths.back().last.s_id);
                                }
                            }
                            if (path.type == EMoveType::Extrude && !is_visible(path))
                                continue;
                            if (m_view_type == EViewType::ColorPrint && !m_tools.m_tool_visibles[path.extruder_id])
                                continue;
                            // store valid path
                            for (size_t j = 0; j < path.sub_paths.size(); ++j) {
                                paths.push_back({ static_cast<unsigned char>(b), path.sub_paths[j].first.b_id, static_cast<unsigned int>(i), static_cast<unsigned int>(j) });
                            }
                            global_endpoints.first = std::min(global_endpoints.first, path.sub_paths.front().first.s_id);
                            global_endpoints.last = std::max(global_endpoints.last, path.sub_paths.back().last.s_id);
                        }
                    }
                }
                // update current sequential position
                p_sequential_view->current.first = !top_layer_only && keep_sequential_current_first ? std::clamp(p_sequential_view->current.first, global_endpoints.first, global_endpoints.last) : global_endpoints.first;
                if (global_endpoints.last == 0) {
                    p_sequential_view->current.last = global_endpoints.last;
                }
                else {
                    p_sequential_view->current.last = keep_sequential_current_last ? std::clamp(p_sequential_view->current.last, global_endpoints.first, global_endpoints.last) : global_endpoints.last;
                }
                // get the world position from the vertex buffer
                bool found = false;
                for (const TBuffer& buffer : m_buffers) {
                    if (buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::InstancedModel ||
                        buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::BatchedModel) {
                        for (size_t i = 0; i < buffer.model.instances.s_ids.size(); ++i) {
                            if (buffer.model.instances.s_ids[i] == p_sequential_view->current.last) {
                                size_t offset = i * buffer.model.instances.instance_size_floats();
                                p_sequential_view->current_position.x() = buffer.model.instances.buffer[offset + 0];
                                p_sequential_view->current_position.y() = buffer.model.instances.buffer[offset + 1];
                                p_sequential_view->current_position.z() = buffer.model.instances.buffer[offset + 2];
                                p_sequential_view->current_offset = buffer.model.instances.offsets[i];
                                found = true;
                                break;
                            }
                        }
                    }
                    else {
                        // searches the path containing the current position
                        for (const Path& path : buffer.paths) {
                            if (path.contains(p_sequential_view->current.last)) {
                                const int sub_path_id = path.get_id_of_sub_path_containing(p_sequential_view->current.last);
                                if (sub_path_id != -1) {
                                    const Path::Sub_Path& sub_path = path.sub_paths[sub_path_id];
                                    unsigned int offset = static_cast<unsigned int>(p_sequential_view->current.last - sub_path.first.s_id);
                                    if (offset > 0) {
                                        if (buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::Line) {
                                            for (size_t i = sub_path.first.s_id + 1; i < p_sequential_view->current.last + 1; i++) {
                                                size_t move_id = m_ssid_to_moveid_map[i];
                                                const GCodeProcessorResult::MoveVertex& curr = m_gcode_result->moves[move_id];
                                                if (curr.is_arc_move()) {
                                                    offset += curr.interpolation_points.size();
                                                }
                                            }
                                            offset = 2 * offset - 1;
                                        }
                                        else if (buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::Triangle) {
                                            unsigned int indices_count = buffer.indices_per_segment();
                                            // BBS: modify to support moves which has internal point
                                            for (size_t i = sub_path.first.s_id + 1; i < p_sequential_view->current.last + 1; i++) {
                                                size_t move_id = m_ssid_to_moveid_map[i];
                                                const GCodeProcessorResult::MoveVertex& curr = m_gcode_result->moves[move_id];
                                                if (curr.is_arc_move()) {
                                                    offset += curr.interpolation_points.size();
                                                }
                                            }
                                            offset = indices_count * (offset - 1) + (indices_count - 2);
                                            if (sub_path_id == 0)
                                                offset += 6; // add 2 triangles for starting cap
                                        }
                                    }
                                    offset += static_cast<unsigned int>(sub_path.first.i_id);
                                    // gets the vertex index from the index buffer on gpu
                                    if (sub_path.first.b_id >= 0 && sub_path.first.b_id < buffer.indices.size()) {
                                        const IBuffer& i_buffer = buffer.indices[sub_path.first.b_id];
                                        unsigned int index = 0;
                                        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, i_buffer.ibo));
                                        glsafe(::glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLintptr>(offset * sizeof(IBufferType)), static_cast<GLsizeiptr>(sizeof(IBufferType)), static_cast<void*>(&index)));
                                        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
                                        // gets the position from the vertices buffer on gpu
                                        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, i_buffer.vbo));
                                        glsafe(::glGetBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(index* buffer.vertices.vertex_size_bytes()), static_cast<GLsizeiptr>(3 * sizeof(float)), static_cast<void*>(p_sequential_view->current_position.data())));
                                        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
                                    }
                                    p_sequential_view->current_offset = Vec3f::Zero();
                                    found = true;
                                    break;
                                }
                            }
                        }
                    }
                    if (found)
                        break;
                }
                // second pass: filter paths by sequential data and collect them by color
                RenderPath* render_path = nullptr;
                for (const auto& [tbuffer_id, ibuffer_id, path_id, sub_path_id] : paths) {
                    TBuffer& buffer = const_cast<TBuffer&>(m_buffers[tbuffer_id]);
                    const Path& path = buffer.paths[path_id];
                    const Path::Sub_Path& sub_path = path.sub_paths[sub_path_id];
                    if (p_sequential_view->current.last < sub_path.first.s_id || sub_path.last.s_id < p_sequential_view->current.first)
                        continue;
                    ColorRGBA color;
                    switch (path.type)
                    {
                    case EMoveType::Tool_change:
                    case EMoveType::Color_change:
                    case EMoveType::Pause_Print:
                    case EMoveType::Custom_GCode:
                    case EMoveType::Retract:
                    case EMoveType::Unretract:
                    case EMoveType::Seam: { color = option_color(path.type); break; }
                    case EMoveType::Extrude: {
                        if (!top_layer_only ||
                            p_sequential_view->current.last == global_endpoints.last ||
                            is_in_layers_range(path, m_layers_z_range[1], m_layers_z_range[1]))
                            color = extrusion_color(path);
                        else
                            color = Neutral_Color;
                        break;
                    }
                    case EMoveType::Travel: {
                        if (!top_layer_only || p_sequential_view->current.last == global_endpoints.last || is_travel_in_layers_range(path_id, m_layers_z_range[1], m_layers_z_range[1]))
                            color = (m_view_type == EViewType::Feedrate || m_view_type == EViewType::Tool) ? extrusion_color(path) : travel_color(path);
                        else
                            color = Neutral_Color;
                        break;
                    }
                    case EMoveType::Wipe: { color = Wipe_Color; break; }
                    default: { color = { 0.0f, 0.0f, 0.0f, 1.0f }; break; }
                    }
                    RenderPath key{ tbuffer_id, color.get_data(), static_cast<unsigned int>(ibuffer_id), path_id, path.m_b_has_effect_layer };
                    if (render_path == nullptr || !RenderPathPropertyEqual()(*render_path, key)) {
                        buffer.render_paths.emplace_back(key);
                        render_path = const_cast<RenderPath*>(&buffer.render_paths.back());
                    }
                    unsigned int delta_1st = 0;
                    if (sub_path.first.s_id < p_sequential_view->current.first && p_sequential_view->current.first <= sub_path.last.s_id)
                        delta_1st = static_cast<unsigned int>(p_sequential_view->current.first - sub_path.first.s_id);
                    unsigned int size_in_indices = 0;
                    switch (buffer.render_primitive_type)
                    {
                    case TBuffer::ERenderPrimitiveType::Point: {
                        size_in_indices = buffer.indices_per_segment();
                        break;
                    }
                    case TBuffer::ERenderPrimitiveType::Line:
                    case TBuffer::ERenderPrimitiveType::Triangle: {
                        // BBS: modify to support moves which has internal point
                        size_t max_s_id = std::min(p_sequential_view->current.last, sub_path.last.s_id);
                        size_t min_s_id = std::max(p_sequential_view->current.first, sub_path.first.s_id);
                        unsigned int segments_count = max_s_id - min_s_id;
                        for (size_t i = min_s_id + 1; i < max_s_id + 1; i++) {
                            size_t move_id = m_ssid_to_moveid_map[i];
                            const GCodeProcessorResult::MoveVertex& curr = m_gcode_result->moves[move_id];
                            if (curr.is_arc_move()) {
                                segments_count += curr.interpolation_points.size();
                            }
                        }
                        size_in_indices = buffer.indices_per_segment() * segments_count;
                        break;
                    }
                    default: { break; }
                    }
                    if (size_in_indices == 0)
                        continue;
                    render_path->sizes.push_back(size_in_indices);
                    if (buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::Triangle) {
                        delta_1st *= buffer.indices_per_segment();
                    }
                    render_path->offsets.push_back(static_cast<size_t>((sub_path.first.i_id + delta_1st) * sizeof(IBufferType)));
#if 0
                    // check sizes and offsets against index buffer size on gpu
                    GLint buffer_size;
                    glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer->indices[render_path->ibuffer_id].ibo));
                    glsafe(::glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &buffer_size));
                    glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
                    if (render_path->offsets.back() + render_path->sizes.back() * sizeof(IBufferType) > buffer_size)
                        BOOST_LOG_TRIVIAL(error) << "LegacyRenderer::refresh_render_paths: Invalid render path data";
#endif
                }
                // Removes empty render paths and sort.
                for (size_t b = 0; b < m_buffers.size(); ++b) {
                    TBuffer* buffer = const_cast<TBuffer*>(&m_buffers[b]);
                    buffer->render_paths.erase(std::remove_if(buffer->render_paths.begin(), buffer->render_paths.end(),
                        [](const auto& path) { return path.sizes.empty() || path.offsets.empty(); }),
                        buffer->render_paths.end());
                    buffer->multi_effect_layer_render_paths.clear();
                    for (size_t i = 0; i < buffer->render_paths.size(); ++i) {
                        if (buffer->render_paths[i].m_b_has_effect_layer) {
                            buffer->multi_effect_layer_render_paths.emplace_back(i);
                        }
                    }
                }
                // second pass: for buffers using instanced and batched models, update the instances render ranges
                for (size_t b = 0; b < m_buffers.size(); ++b) {
                    TBuffer& buffer = const_cast<TBuffer&>(m_buffers[b]);
                    if (buffer.render_primitive_type != TBuffer::ERenderPrimitiveType::InstancedModel &&
                        buffer.render_primitive_type != TBuffer::ERenderPrimitiveType::BatchedModel)
                        continue;
                    buffer.model.instances.render_ranges.reset();
                    if (!buffer.visible || buffer.model.instances.s_ids.empty())
                        continue;
                    buffer.model.instances.render_ranges.ranges.push_back({ 0, 0, 0, buffer.model.color });
                    bool has_second_range = top_layer_only && p_sequential_view->current.last != p_sequential_view->global.last;
                    if (has_second_range)
                        buffer.model.instances.render_ranges.ranges.push_back({ 0, 0, 0, Neutral_Color });
                    if (p_sequential_view->current.first <= buffer.model.instances.s_ids.back() && buffer.model.instances.s_ids.front() <= p_sequential_view->current.last) {
                        for (size_t id : buffer.model.instances.s_ids) {
                            if (has_second_range) {
                                if (id < p_sequential_view->endpoints.first) {
                                    ++buffer.model.instances.render_ranges.ranges.front().offset;
                                    if (id <= p_sequential_view->current.first)
                                        ++buffer.model.instances.render_ranges.ranges.back().offset;
                                    else
                                        ++buffer.model.instances.render_ranges.ranges.back().count;
                                }
                                else if (id <= p_sequential_view->current.last)
                                    ++buffer.model.instances.render_ranges.ranges.front().count;
                                else
                                    break;
                            }
                            else {
                                if (id <= p_sequential_view->current.first)
                                    ++buffer.model.instances.render_ranges.ranges.front().offset;
                                else if (id <= p_sequential_view->current.last)
                                    ++buffer.model.instances.render_ranges.ranges.front().count;
                                else
                                    break;
                            }
                        }
                    }
                }
                // set sequential data to their final value
                p_sequential_view->endpoints = top_layer_only ? top_layer_endpoints : global_endpoints;
                p_sequential_view->current.first = !top_layer_only && keep_sequential_current_first ? std::clamp(p_sequential_view->current.first, p_sequential_view->endpoints.first, p_sequential_view->endpoints.last) : p_sequential_view->endpoints.first;
                p_sequential_view->global = global_endpoints;
                //BBS
                enable_moves_slider(!paths.empty());
#if ENABLE_GCODE_VIEWER_STATISTICS
                for (const TBuffer& buffer : m_buffers) {
                    statistics->render_paths_size += SLIC3R_STDUNORDEREDSET_MEMSIZE(buffer.render_paths, RenderPath);
                    for (const RenderPath& path : buffer.render_paths) {
                        statistics->render_paths_size += SLIC3R_STDVEC_MEMSIZE(path.sizes, unsigned int);
                        statistics->render_paths_size += SLIC3R_STDVEC_MEMSIZE(path.offsets, size_t);
                    }
                    statistics->models_instances_size += SLIC3R_STDVEC_MEMSIZE(buffer.model.instances.buffer, float);
                    statistics->models_instances_size += SLIC3R_STDVEC_MEMSIZE(buffer.model.instances.s_ids, size_t);
                    statistics->models_instances_size += SLIC3R_STDVEC_MEMSIZE(buffer.model.instances.render_ranges.ranges, InstanceVBuffer::Ranges::Range);
                }
                statistics->refresh_paths_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
#endif // ENABLE_GCODE_VIEWER_STATISTICS
            }

            template<typename Iterator>
            void LegacyRenderer::render_sub_paths(Iterator it_path, Iterator it_end, GLShaderProgram& shader, int uniform_color, unsigned int draw_type)
            {
                //std::vector<RenderPath>::iterator it_path, std::vector<RenderPath>::iterator it_end
                if ((EDrawPrimitiveType)draw_type == EDrawPrimitiveType::Points) {
                    glsafe(::glEnable(GL_VERTEX_PROGRAM_POINT_SIZE));
                    glsafe(::glEnable(GL_POINT_SPRITE));
                }
                for (auto it = it_path; it != it_end && it_path->ibuffer_id == it->ibuffer_id; ++it) {
                    const RenderPath& path = *it;
                    draw_render_path(path, draw_type, uniform_color, path.color);
#if ENABLE_GCODE_VIEWER_STATISTICS
                    ++m_statistics.gl_multi_triangles_calls_count;
#endif // ENABLE_GCODE_VIEWER_STATISTICS
                }
                if ((EDrawPrimitiveType)draw_type == EDrawPrimitiveType::Points) {
                    glsafe(::glDisable(GL_POINT_SPRITE));
                    glsafe(::glDisable(GL_VERTEX_PROGRAM_POINT_SIZE));
                }
            }

            void LegacyRenderer::render_toolpaths()
            {
#if ENABLE_FIXED_SCREEN_SIZE_POINT_MARKERS
                float point_size = 20.0f;
#else
                float point_size = 0.8f;
#endif // ENABLE_FIXED_SCREEN_SIZE_POINT_MARKERS
                std::array<float, 4> light_intensity = { 0.25f, 0.70f, 0.75f, 0.75f };
                const Camera& camera = wxGetApp().plater()->get_camera();
                double zoom = camera.get_zoom();
                const std::array<int, 4>& viewport = camera.get_viewport();
                float near_plane_height = camera.get_type() == Camera::EType::Perspective ? static_cast<float>(viewport[3]) / (2.0f * static_cast<float>(2.0 * std::tan(0.5 * Geometry::deg2rad(camera.get_fov())))) :
                    static_cast<float>(viewport[3]) * 0.0005;
                auto shader_init_as_points = [zoom, point_size, near_plane_height](GLShaderProgram& shader) {
#if ENABLE_FIXED_SCREEN_SIZE_POINT_MARKERS
                    shader.set_uniform("use_fixed_screen_size", 1);
#else
                    shader.set_uniform("use_fixed_screen_size", 0);
#endif // ENABLE_FIXED_SCREEN_SIZE_POINT_MARKERS
                    shader.set_uniform("zoom", zoom);
                    shader.set_uniform("percent_outline_radius", 0.0f);
                    shader.set_uniform("percent_center_radius", 0.33f);
                    shader.set_uniform("point_size", point_size);
                    shader.set_uniform("near_plane_height", near_plane_height);
                    };
                    auto shader_init_as_lines = [light_intensity](GLShaderProgram& shader) {
                        shader.set_uniform("light_intensity", light_intensity);
                        };
                            auto render_as_instanced_model = [
#if ENABLE_GCODE_VIEWER_STATISTICS
                                this
#endif // ENABLE_GCODE_VIEWER_STATISTICS
                            ](TBuffer& buffer, GLShaderProgram& shader) {
                                for (auto& range : buffer.model.instances.render_ranges.ranges) {
                                    if (range.vbo == 0 && range.count > 0) {
                                        glsafe(::glGenBuffers(1, &range.vbo));
                                        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, range.vbo));
                                        glsafe(::glBufferData(GL_ARRAY_BUFFER, range.count * buffer.model.instances.instance_size_bytes(), (const void*)&buffer.model.instances.buffer[range.offset * buffer.model.instances.instance_size_floats()], GL_STATIC_DRAW));
                                        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
                                    }
                                    if (range.vbo > 0) {
                                        buffer.model.model.set_color(-1, range.color);
                                        buffer.model.model.render_instanced(range.vbo, range.count);
#if ENABLE_GCODE_VIEWER_STATISTICS
                                        ++m_statistics.gl_instanced_models_calls_count;
                                        m_statistics.total_instances_gpu_size += static_cast<int64_t>(range.count * buffer.model.instances.instance_size_bytes());
#endif // ENABLE_GCODE_VIEWER_STATISTICS
                                    }
                                }
                                };
#if ENABLE_GCODE_VIEWER_STATISTICS
                                auto render_as_batched_model = [this](TBuffer& buffer, GLShaderProgram& shader) {
#else
                                auto render_as_batched_model = [](TBuffer& buffer, GLShaderProgram& shader) {
#endif // ENABLE_GCODE_VIEWER_STATISTICS
                                    struct Range
                                    {
                                        unsigned int first;
                                        unsigned int last;
                                        bool intersects(const Range& other) const { return (other.last < first || other.first > last) ? false : true; }
                                    };
                                    Range buffer_range = { 0, 0 };
                                    size_t indices_per_instance = buffer.model.data.indices_count();
                                    const Camera& camera = wxGetApp().plater()->get_camera();
                                    const Transform3d& view_matrix = camera.get_view_matrix();
                                    shader.set_uniform("view_model_matrix", view_matrix);
                                    shader.set_uniform("projection_matrix", camera.get_projection_matrix());
                                    shader.set_uniform("normal_matrix", (Matrix3d)view_matrix.matrix().block(0, 0, 3, 3).inverse().transpose());
                                    for (size_t j = 0; j < buffer.indices.size(); ++j) {
                                        const IBuffer& i_buffer = buffer.indices[j];
                                        buffer_range.last = buffer_range.first + i_buffer.count / indices_per_instance;
                                        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, i_buffer.vbo));
                                        const int position_id = shader.get_attrib_location("v_position");
                                        if (position_id != -1) {
                                            glsafe(::glVertexAttribPointer(position_id, buffer.vertices.position_size_floats(), GL_FLOAT, GL_FALSE, buffer.vertices.vertex_size_bytes(), (const void*)buffer.vertices.position_offset_bytes()));
                                            glsafe(::glEnableVertexAttribArray(position_id));
                                        }
                                        bool has_normals = buffer.vertices.normal_size_floats() > 0;
                                        int normal_id = -1;
                                        if (has_normals) {
                                            normal_id = shader.get_attrib_location("v_normal");
                                            if (normal_id != -1) {
                                                glsafe(::glVertexAttribPointer(normal_id, buffer.vertices.normal_size_floats(), GL_FLOAT, GL_FALSE, buffer.vertices.vertex_size_bytes(), (const void*)buffer.vertices.normal_offset_bytes()));
                                                glsafe(::glEnableVertexAttribArray(normal_id));
                                            }
                                        }
                                        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, i_buffer.ibo));
                                        for (auto& range : buffer.model.instances.render_ranges.ranges) {
                                            Range range_range = { range.offset, range.offset + range.count };
                                            if (range_range.intersects(buffer_range)) {
                                                shader.set_uniform("uniform_color", range.color);
                                                unsigned int offset = (range_range.first > buffer_range.first) ? range_range.first - buffer_range.first : 0;
                                                size_t offset_bytes = static_cast<size_t>(offset) * indices_per_instance * sizeof(IBufferType);
                                                Range render_range = { std::max(range_range.first, buffer_range.first), std::min(range_range.last, buffer_range.last) };
                                                size_t count = static_cast<size_t>(render_range.last - render_range.first) * indices_per_instance;
                                                if (count > 0) {
                                                    glsafe(::glDrawElements(GL_TRIANGLES, (GLsizei)count, GL_UNSIGNED_SHORT, (const void*)offset_bytes));
#if ENABLE_GCODE_VIEWER_STATISTICS
                                                    ++m_statistics.gl_batched_models_calls_count;
#endif // ENABLE_GCODE_VIEWER_STATISTICS
                                                }
                                            }
                                        }
                                        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
                                        if (normal_id != -1)
                                            glsafe(::glDisableVertexAttribArray(normal_id));
                                        if (position_id != -1)
                                            glsafe(::glDisableVertexAttribArray(position_id));
                                        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
                                        buffer_range.first = buffer_range.last;
                                    }
                                    };
                                auto line_width = [](double zoom) {
                                    return (zoom < 5.0) ? 1.0 : (1.0 + 5.0 * (zoom - 5.0) / (100.0 - 5.0));
                                    };
                                unsigned char begin_id = buffer_id(EMoveType::Retract);
                                unsigned char end_id = buffer_id(EMoveType::Count);
                                //BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(":begin_id %1%, end_id %2% ")%(int)begin_id %(int)end_id;
                                const auto& ogl_manager = wxGetApp().get_opengl_manager();
                                for (unsigned char i = begin_id; i < end_id; ++i) {
                                    TBuffer& buffer = m_buffers[i];
                                    if (!buffer.visible || !buffer.has_data())
                                        continue;
                                    const auto& shader = wxGetApp().get_shader(buffer.shader.c_str());
                                    if (shader != nullptr) {
                                        wxGetApp().bind_shader(shader);
                                        const Transform3d& view_matrix = camera.get_view_matrix();
                                        shader->set_uniform("view_model_matrix", view_matrix);
                                        shader->set_uniform("projection_matrix", camera.get_projection_matrix());
                                        shader->set_uniform("normal_matrix", (Matrix3d)view_matrix.matrix().block(0, 0, 3, 3).inverse().transpose());
                                        if (buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::InstancedModel) {
                                            shader->set_uniform("emission_factor", 0.25f);
                                            render_as_instanced_model(buffer, *shader);
                                            shader->set_uniform("emission_factor", 0.0f);
                                        }
                                        else if (buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::BatchedModel) {
                                            shader->set_uniform("emission_factor", 0.25f);
                                            render_as_batched_model(buffer, *shader);
                                            shader->set_uniform("emission_factor", 0.0f);
                                        }
                                        else {
                                            switch (buffer.render_primitive_type) {
                                            case TBuffer::ERenderPrimitiveType::Point: shader_init_as_points(*shader); break;
                                            case TBuffer::ERenderPrimitiveType::Line:  shader_init_as_lines(*shader); break;
                                            default: break;
                                            }
                                            int uniform_color = shader->get_uniform_location("uniform_color");
                                            auto it_path = buffer.render_paths.rbegin();
                                            //BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(":buffer indices size %1%, render_path size %2% ")%buffer.indices.size() %buffer.render_paths.size();
                                            unsigned int indices_count = static_cast<unsigned int>(buffer.indices.size());
                                            for (unsigned int index = 0; index < indices_count; ++index) {
                                                unsigned int ibuffer_id = indices_count - index - 1;
                                                const IBuffer& i_buffer = buffer.indices[ibuffer_id];
                                                // Skip all paths with ibuffer_id < ibuffer_id.
                                                for (; it_path != buffer.render_paths.rend() && it_path->ibuffer_id > ibuffer_id; ++it_path);
                                                if (it_path == buffer.render_paths.rend() || it_path->ibuffer_id < ibuffer_id)
                                                    // Not found. This shall not happen.
                                                    continue;
                                                glsafe(::glBindBuffer(GL_ARRAY_BUFFER, i_buffer.vbo));
                                                const int position_id = shader->get_attrib_location("v_position");
                                                if (position_id != -1) {
                                                    glsafe(::glVertexAttribPointer(position_id, buffer.vertices.position_size_floats(), GL_FLOAT, GL_FALSE, buffer.vertices.vertex_size_bytes(), (const void*)buffer.vertices.position_offset_bytes()));
                                                    glsafe(::glEnableVertexAttribArray(position_id));
                                                }
                                                bool has_normals = buffer.vertices.normal_size_floats() > 0;
                                                int normal_id = -1;
                                                if (has_normals) {
                                                    normal_id = shader->get_attrib_location("v_normal");
                                                    if (normal_id != -1) {
                                                        glsafe(::glVertexAttribPointer(normal_id, buffer.vertices.normal_size_floats(), GL_FLOAT, GL_FALSE, buffer.vertices.vertex_size_bytes(), (const void*)buffer.vertices.normal_offset_bytes()));
                                                        glsafe(::glEnableVertexAttribArray(normal_id));
                                                    }
                                                }
                                                glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, i_buffer.ibo));
                                                // Render all elements with it_path->ibuffer_id == ibuffer_id, possible with varying colors.
                                                switch (buffer.render_primitive_type)
                                                {
                                                case TBuffer::ERenderPrimitiveType::Point: {
                                                    render_sub_paths(it_path, buffer.render_paths.rend(), *shader, uniform_color, (unsigned int)EDrawPrimitiveType::Points);
                                                    break;
                                                }
                                                case TBuffer::ERenderPrimitiveType::Line: {
                                                    if (ogl_manager) {
                                                        ogl_manager->set_line_width(static_cast<float>(line_width(zoom)));
                                                    }
                                                    render_sub_paths(it_path, buffer.render_paths.rend(), *shader, uniform_color, (unsigned int)EDrawPrimitiveType::Lines);
                                                    break;
                                                }
                                                case TBuffer::ERenderPrimitiveType::Triangle: {
                                                    render_sub_paths(it_path, buffer.render_paths.rend(), *shader, uniform_color, (unsigned int)EDrawPrimitiveType::Triangles);
                                                    if (m_view_type == EViewType::ColorPrint || m_view_type == EViewType::Summary) {
                                                        for (uint32_t i_effect_render_path = 0; i_effect_render_path < buffer.multi_effect_layer_render_paths.size(); ++i_effect_render_path) {
                                                            const auto t_render_path_index = buffer.multi_effect_layer_render_paths[i_effect_render_path];
                                                            const auto& t_render_path = buffer.render_paths[t_render_path_index];
                                                            if (t_render_path.ibuffer_id != ibuffer_id) {
                                                                continue;
                                                            }

                                                            glsafe(::glEnable(GL_BLEND));
                                                            // Linear Dodge
                                                            // https://gamedev.stackexchange.com/questions/17043/blend-modes-in-cocos2d-with-glblendfunc
                                                            glsafe(::glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ONE));
                                                            glsafe(::glDepthFunc(GL_LEQUAL));
                                                            glsafe(::glDepthMask(GL_FALSE));

                                                            draw_render_path(t_render_path, (unsigned int)EDrawPrimitiveType::Triangles, uniform_color, { 0.8f, 0.8f, 0.8f, 1.0f });

                                                            glsafe(::glDisable(GL_BLEND));
                                                            glsafe(::glDepthFunc(GL_LESS));
                                                            glsafe(::glDepthMask(GL_TRUE));
                                                        }
                                                    }
                                                    break;
                                                }
                                                default: { break; }
                                                }
                                                glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
                                                if (normal_id != -1)
                                                    glsafe(::glDisableVertexAttribArray(normal_id));
                                                if (position_id != -1)
                                                    glsafe(::glDisableVertexAttribArray(position_id));
                                                glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
                                            }
                                        }
                                        wxGetApp().unbind_shader();
                                    }
                                }
            }

#if ENABLE_GCODE_VIEWER_STATISTICS
            void LegacyRenderer::render_statistics()
            {
                static const float offset = 275.0f;
                ImGuiWrapper& imgui = *wxGetApp().imgui();
                auto add_time = [this, &imgui](const std::string& label, int64_t time) {
                    imgui.text_colored(ImGuiWrapper::COL_ORANGE_LIGHT, label);
                    ImGui::SameLine(offset);
                    imgui.text(std::to_string(time) + " ms (" + get_time_dhms(static_cast<float>(time) * 0.001f) + ")");
                    };
                auto add_memory = [this, &imgui](const std::string& label, int64_t memory) {
                    auto format_string = [memory](const std::string& units, float value) {
                        return std::to_string(memory) + " bytes (" +
                            Slic3r::float_to_string_decimal_point(float(memory) * value, 3)
                            + " " + units + ")";
                        };
                    static const float kb = 1024.0f;
                    static const float inv_kb = 1.0f / kb;
                    static const float mb = 1024.0f * kb;
                    static const float inv_mb = 1.0f / mb;
                    static const float gb = 1024.0f * mb;
                    static const float inv_gb = 1.0f / gb;
                    imgui.text_colored(ImGuiWrapper::COL_ORANGE_LIGHT, label);
                    ImGui::SameLine(offset);
                    if (static_cast<float>(memory) < mb)
                        imgui.text(format_string("KB", inv_kb));
                    else if (static_cast<float>(memory) < gb)
                        imgui.text(format_string("MB", inv_mb));
                    else
                        imgui.text(format_string("GB", inv_gb));
                    };
                auto add_counter = [this, &imgui](const std::string& label, int64_t counter) {
                    imgui.text_colored(ImGuiWrapper::COL_ORANGE_LIGHT, label);
                    ImGui::SameLine(offset);
                    imgui.text(std::to_string(counter));
                    };
                imgui.set_next_window_pos(0.5f * wxGetApp().plater()->get_current_canvas3D()->get_canvas_size().get_width(), 0.0f, ImGuiCond_Once, 0.5f, 0.0f);
                ImGui::SetNextWindowSizeConstraints({ 300.0f, 100.0f }, { 600.0f, 900.0f });
                imgui.begin(std::string("GCodeViewer Statistics"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize);
                ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
                if (ImGui::CollapsingHeader("Time")) {
                    add_time(std::string("GCodeProcessor:"), m_statistics.results_time);
                    ImGui::Separator();
                    add_time(std::string("Load:"), m_statistics.load_time);
                    add_time(std::string("  Load vertices:"), m_statistics.load_vertices);
                    add_time(std::string("  Smooth vertices:"), m_statistics.smooth_vertices);
                    add_time(std::string("  Load indices:"), m_statistics.load_indices);
                    add_time(std::string("Refresh:"), m_statistics.refresh_time);
                    add_time(std::string("Refresh paths:"), m_statistics.refresh_paths_time);
                }
                if (ImGui::CollapsingHeader("OpenGL calls")) {
                    add_counter(std::string("Multi GL_POINTS:"), m_statistics.gl_multi_points_calls_count);
                    add_counter(std::string("Multi GL_LINES:"), m_statistics.gl_multi_lines_calls_count);
                    add_counter(std::string("Multi GL_TRIANGLES:"), m_statistics.gl_multi_triangles_calls_count);
                    add_counter(std::string("GL_TRIANGLES:"), m_statistics.gl_triangles_calls_count);
                    ImGui::Separator();
                    add_counter(std::string("Instanced models:"), m_statistics.gl_instanced_models_calls_count);
                    add_counter(std::string("Batched models:"), m_statistics.gl_batched_models_calls_count);
                }
                if (ImGui::CollapsingHeader("CPU memory")) {
                    add_memory(std::string("GCodeProcessor results:"), m_statistics.results_size);
                    ImGui::Separator();
                    add_memory(std::string("Paths:"), m_statistics.paths_size);
                    add_memory(std::string("Render paths:"), m_statistics.render_paths_size);
                    add_memory(std::string("Models instances:"), m_statistics.models_instances_size);
                }
                if (ImGui::CollapsingHeader("GPU memory")) {
                    add_memory(std::string("Vertices:"), m_statistics.total_vertices_gpu_size);
                    add_memory(std::string("Indices:"), m_statistics.total_indices_gpu_size);
                    add_memory(std::string("Instances:"), m_statistics.total_instances_gpu_size);
                    ImGui::Separator();
                    add_memory(std::string("Max VBuffer:"), m_statistics.max_vbuffer_gpu_size);
                    add_memory(std::string("Max IBuffer:"), m_statistics.max_ibuffer_gpu_size);
                }
                if (ImGui::CollapsingHeader("Other")) {
                    add_counter(std::string("Travel segments count:"), m_statistics.travel_segments_count);
                    add_counter(std::string("Wipe segments count:"), m_statistics.wipe_segments_count);
                    add_counter(std::string("Extrude segments count:"), m_statistics.extrude_segments_count);
                    add_counter(std::string("Instances count:"), m_statistics.instances_count);
                    add_counter(std::string("Batched count:"), m_statistics.batched_count);
                    ImGui::Separator();
                    add_counter(std::string("VBuffers count:"), m_statistics.vbuffers_count);
                    add_counter(std::string("IBuffers count:"), m_statistics.ibuffers_count);
                }
                imgui.end();
            }
#endif
            void LegacyRenderer::draw_render_path(const RenderPath& t_render_path, unsigned int t_draw_type, int t_uniform_color, const Color& t_color)
            {
                assert(!t_render_path.sizes.empty());
                assert(!t_render_path.offsets.empty());
                glsafe(::glUniform4fv(t_uniform_color, 1, static_cast<const GLfloat*>(t_color.data())));
                const bool b_cancel_glmultidraw = wxGetApp().get_opengl_manager()->get_cancle_glmultidraw();
                if (b_cancel_glmultidraw) {
                    for (size_t i = 0; i < t_render_path.sizes.size(); ++i) {
                        GLsizei count = t_render_path.sizes[i];
                        glsafe(::glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_SHORT, (const void*)t_render_path.offsets[i]));
                    }
                }
                else {
                    int total_draws = t_render_path.sizes.size();
                    int number = t_render_path.sizes.size() / MAX_DRAWS_PER_BATCH + 1;
                    for (size_t batch = 0; batch < number; batch++) {
                        int start = batch * MAX_DRAWS_PER_BATCH;
                        int count = std::min(MAX_DRAWS_PER_BATCH, total_draws - start);
                        if (count == 0) { continue; }
                        glsafe(::glMultiDrawElements(OpenGLManager::get_draw_primitive_type(EDrawPrimitiveType(t_draw_type)), (const GLsizei*)t_render_path.sizes.data() + start, GL_UNSIGNED_SHORT,
                            (const void* const*)(t_render_path.offsets.data() + start),
                            (GLsizei)count));
                    }
                }
            }

            bool LegacyRenderer::is_visible(const Path& path) const
            {
                return is_extrusion_role_visible(path.role);
            }
            // ENABLE_GCODE_VIEWER_STATISTICS
            void LegacyRenderer::log_memory_used(const std::string & label, int64_t additional) const
            {
                if (Slic3r::get_logging_level() >= 5) {
                    int64_t paths_size = 0;
                    int64_t render_paths_size = 0;
                    for (const TBuffer& buffer : m_buffers) {
                        paths_size += SLIC3R_STDVEC_MEMSIZE(buffer.paths, Path);
                        render_paths_size += SLIC3R_STDUNORDEREDSET_MEMSIZE(buffer.render_paths, RenderPath);
                        for (const RenderPath& path : buffer.render_paths) {
                            render_paths_size += SLIC3R_STDVEC_MEMSIZE(path.sizes, unsigned int);
                            render_paths_size += SLIC3R_STDVEC_MEMSIZE(path.offsets, size_t);
                        }
                    }
                    int64_t layers_size = SLIC3R_STDVEC_MEMSIZE(m_layers.get_zs(), double);
                    layers_size += SLIC3R_STDVEC_MEMSIZE(m_layers.get_endpoints(), Layers::Endpoints);
                    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format("paths_size %1%, render_paths_size %2%,layers_size %3%, additional %4%\n")
                        % paths_size % render_paths_size % layers_size % additional;
                    BOOST_LOG_TRIVIAL(trace) << label
                        << "(" << format_memsize_MB(additional + paths_size + render_paths_size + layers_size) << ");"
                        << log_memory_info();
                }
            }
            Color LegacyRenderer::option_color(EMoveType move_type) const
            {
                switch (move_type)
                {
                case EMoveType::Tool_change: { return Options_Colors[static_cast<unsigned int>(EOptionsColors::ToolChanges)]; }
                case EMoveType::Color_change: { return Options_Colors[static_cast<unsigned int>(EOptionsColors::ColorChanges)]; }
                case EMoveType::Pause_Print: { return Options_Colors[static_cast<unsigned int>(EOptionsColors::PausePrints)]; }
                case EMoveType::Custom_GCode: { return Options_Colors[static_cast<unsigned int>(EOptionsColors::CustomGCodes)]; }
                case EMoveType::Retract: { return Options_Colors[static_cast<unsigned int>(EOptionsColors::Retractions)]; }
                case EMoveType::Unretract: { return Options_Colors[static_cast<unsigned int>(EOptionsColors::Unretractions)]; }
                case EMoveType::Seam: { return Options_Colors[static_cast<unsigned int>(EOptionsColors::Seams)]; }
                default: { return { 0.0f, 0.0f, 0.0f, 1.0f }; }
                }
            }
            bool LegacyRenderer::show_sequential_view() const
            {
                const auto& p_sequential_view = get_sequential_view();
                if (!p_sequential_view) {
                    return false;
                }
                return p_sequential_view->current.last != p_sequential_view->endpoints.last;
            }
            void LegacyRenderer::on_visibility_changed()
            {
                // update buffers' render paths
                refresh_render_paths();
                update_moves_slider();
                BaseRenderer::on_visibility_changed();
            }
            } // namespace gcode
            } // namespace GUI
        } // namespace Slic3r