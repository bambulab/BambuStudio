#include "AdvancedRenderer.hpp"
#include "slic3r/GUI/Widgets/ProgressDialog.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/GLTexture.hpp"
#include "slic3r/GUI/OpenGLManager.hpp"
#include "slic3r/GUI/IMSlider.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "libslic3r/Geometry/ConvexHull.hpp"
#include <GL/glew.h>
namespace
{
    Slic3r::Vec2f get_view_data_index_from_view_type(const Slic3r::GUI::gcode::EViewType type)
    {
        Slic3r::Vec2f rt{ 0.0f, 0.0f };
        switch (type)
        {
        case Slic3r::GUI::gcode::EViewType::Height:
        {
            rt.x() = 6.0f;
            break;
        }
        case Slic3r::GUI::gcode::EViewType::Width:
        {
            rt.x() = 5.0f;
            break;
        }
        case Slic3r::GUI::gcode::EViewType::Feedrate:
        {
            rt.x() = 7.0f;
            break;
        }
        case Slic3r::GUI::gcode::EViewType::FanSpeed:
        {
            rt.x() = 8.0f;
            break;
        }
        case Slic3r::GUI::gcode::EViewType::Temperature:
        {
            rt.x() = 9.0f;
            break;
        }
        case Slic3r::GUI::gcode::EViewType::LayerTime:
        {
            rt.x() = 10.0f;
            break;
        }
        case Slic3r::GUI::gcode::EViewType::VolumetricRate:
        {
            rt.x() = 11.0f;
            break;
        }
        case Slic3r::GUI::gcode::EViewType::FeatureType:
        {
            rt.x() = 1.0f;
            break;
        }
        case Slic3r::GUI::gcode::EViewType::Tool:
        {
            rt.x() = 2.0f;
            break;
        }
        case Slic3r::GUI::gcode::EViewType::Summary:
        case Slic3r::GUI::gcode::EViewType::ColorPrint:
        {
            rt.x() = 3.0f;
            break;
        }
        default:
            break;
        }

        return rt;
    }

    float get_move_data_from_view_type(const Slic3r::GUI::gcode::EViewType type, const Slic3r::GCodeProcessorResult::MoveVertex& t_move)
    {
        switch (type)
        {
        case Slic3r::GUI::gcode::EViewType::Height:
        {
            return t_move.height;
        }
        case Slic3r::GUI::gcode::EViewType::Width:
        {
            return t_move.width;
        }
        case Slic3r::GUI::gcode::EViewType::Feedrate:
        {
            return t_move.feedrate;
        }
        case Slic3r::GUI::gcode::EViewType::FanSpeed:
        {
            return t_move.fan_speed;
        }
        case Slic3r::GUI::gcode::EViewType::Temperature:
        {
            return t_move.temperature;
        }
        case Slic3r::GUI::gcode::EViewType::LayerTime:
        {
            return t_move.layer_duration;
        }
        case Slic3r::GUI::gcode::EViewType::VolumetricRate:
        {
            return t_move.volumetric_rate();
        }
        case Slic3r::GUI::gcode::EViewType::FeatureType:
        {
            return float(t_move.extrusion_role);
        }
        case Slic3r::GUI::gcode::EViewType::Tool:
        {
            return float(t_move.extruder_id);
        }
        case Slic3r::GUI::gcode::EViewType::Summary:
        case Slic3r::GUI::gcode::EViewType::ColorPrint:
        {
            return float(t_move.cp_color_id);
        }
        // helio
        case Slic3r::GUI::gcode::EViewType::ThermalIndexMin:
        {
            return t_move.thermal_index_min;
        }
        case Slic3r::GUI::gcode::EViewType::ThermalIndexMax:
        {
            return t_move.thermal_index_max;
        }
        case Slic3r::GUI::gcode::EViewType::ThermalIndexMean:
        {
            return t_move.thermal_index_mean;
        }
        // end helio
        default:
            return 0.0f;
        }
    }

    void export_image(const std::shared_ptr<Slic3r::GUI::GLTexture>& p_texture, const wxString& path)
    {
        if (!p_texture) {
            return;
        }
        std::vector<uint8_t> pixel_data;
        p_texture->read_back(pixel_data);
        if (pixel_data.size()) {
            const auto width = p_texture->get_width();
            const auto height = p_texture->get_height();
            wxImage image(width, height);
            image.InitAlpha();

            for (unsigned int ih = 0; ih < height; ++ih)
            {
                unsigned int rr = (height - 1 - ih) * width;
                for (unsigned int iw = 0; iw < width; ++iw)
                {
                    float* px = (float*)pixel_data.data() + 4 * (rr + iw);
                    const unsigned char r = px[0] * 255.0f;
                    const unsigned char g = px[1] * 255.0f;
                    const unsigned char b = px[2] * 255.0f;
                    const unsigned char a = px[3] * 255.0f;
                    image.SetRGB((int)iw, (int)ih, r, g, b);
                    image.SetAlpha((int)iw, (int)ih, a);
                }
            }
            if (width < 512) {
                image = image.Scale(512, height);
            }
            image.SaveFile(path, wxBITMAP_TYPE_PNG);
        }
    }

    void add_segment(const Slic3r::GUI::gcode::Segment& t_seg, const std::vector<Slic3r::GUI::gcode::SegmentVertex>& segment_vertex, std::vector<float>& segment_index_list, uint32_t& prev_seg_index, bool flag = false) {
        const auto& t_seg_vertex_first = segment_vertex.at(t_seg.m_first_mid);
        const auto& t_seg_vertex_second = segment_vertex.at(t_seg.m_second_mid);
        const auto first_pos_index = t_seg_vertex_first.m_indices[t_seg_vertex_first.m_indices.size() - 1];
        uint32_t prev_first_pos_index = 0;
        bool has_prev = false;
        if (flag) {
            if (prev_seg_index != -1u) {
                uint32_t prev_second_pos_index = segment_index_list[prev_seg_index * 4 + 1];
                if (prev_second_pos_index == first_pos_index) {
                    prev_first_pos_index = segment_index_list[prev_seg_index * 4 + 0];
                    has_prev = true;
                }
            }
        }
        segment_index_list.emplace_back(first_pos_index);
        segment_index_list.emplace_back(t_seg_vertex_second.m_indices[0]);
        segment_index_list.emplace_back(float(has_prev));
        segment_index_list.emplace_back(float(prev_first_pos_index));
        if (flag) {
            prev_seg_index = segment_index_list.size() / 4 - 1;
        }
        for (size_t k_index = 1; k_index < t_seg_vertex_second.m_indices.size(); ++k_index) {
            segment_index_list.emplace_back(t_seg_vertex_second.m_indices[k_index - 1]);
            segment_index_list.emplace_back(t_seg_vertex_second.m_indices[k_index]);
            segment_index_list.emplace_back(float(has_prev));
            if (flag) {
                segment_index_list.emplace_back(segment_index_list[prev_seg_index * 4 + 0]);
            }
            else {
                segment_index_list.emplace_back(0.0f);
            }
            ++prev_seg_index;
        }
        };
}
namespace Slic3r
{
    namespace GUI
    {
        namespace gcode
        {
            AdvancedRenderer::AdvancedRenderer()
                :BaseRenderer()
            {
            }

            AdvancedRenderer::~AdvancedRenderer()
            {
            }

            void AdvancedRenderer::init(ConfigOptionMode mode, Slic3r::PresetBundle* preset_bundle)
            {
                if (m_gl_data_initialized)
                    return;
                BaseRenderer::init(mode, preset_bundle);
                set_move_type_visible(EMoveType::Seam, true);
                set_move_type_visible(EMoveType::Extrude, true);
                m_gl_data_initialized = true;
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": finished");
            }

            void AdvancedRenderer::update_sequential_view_current(unsigned int first, unsigned int last)
            {
                const auto& p_layer_manager = get_layer_manager();
                if (!p_layer_manager) {
                    return;
                }
                p_layer_manager->set_current_move_start(first);
                p_layer_manager->set_current_move_end(last);
            }

            void AdvancedRenderer::render_calibration_thumbnail(ThumbnailData& thumbnail_data, unsigned int w, unsigned int h, const ThumbnailsParams& thumbnail_params, PartPlateList& partplate_list, OpenGLManager& opengl_manager)
            {
            }

            std::vector<double> AdvancedRenderer::get_layers_zs() const
            {
                std::vector<double> layer_zs;
                if (m_p_layer_manager) {
                    for (size_t i = 0; i < m_p_layer_manager->size(); ++i) {
                        layer_zs.push_back((*m_p_layer_manager)[i].get_z());
                    }
                }
                return layer_zs;
            }

            unsigned int AdvancedRenderer::get_options_visibility_flags() const
            {
                return 0;
            }

            void AdvancedRenderer::set_options_visibility_from_flags(unsigned int flags)
            {
            }

            unsigned int AdvancedRenderer::get_toolpath_role_visibility_flags() const
            {
                return 0;
            }

            void AdvancedRenderer::refresh(const GCodeProcessorResult& gcode_result, const std::vector<std::string>& str_tool_colors)
            {
                BaseRenderer::refresh(gcode_result, str_tool_colors);
                m_b_tool_colors_dirty = true;
            }

            void AdvancedRenderer::refresh_render_paths()
            {
            }

            bool AdvancedRenderer::can_export_toolpaths() const
            {
                return has_data();
            }

            void AdvancedRenderer::export_toolpaths_to_obj(const char* filename) const
            {
                if (filename == nullptr)
                    return;
                if (!has_data())
                    return;
                if (!m_p_layer_manager) {
                    return;
                }
                wxBusyCursor busy;

                // save materials file
                boost::filesystem::path mat_filename(filename);
                mat_filename.replace_extension("mtl");
                const auto& parent_path = mat_filename.parent_path();

                CNumericLocalesSetter locales_setter;
                FILE* fp = boost::nowide::fopen(mat_filename.string().c_str(), "w");
                if (fp == nullptr) {
                    BOOST_LOG_TRIVIAL(error) << "GCodeViewer::export_toolpaths_to_obj: Couldn't open " << mat_filename.string().c_str() << " for writing";
                    return;
                }

                fprintf(fp, "# G-Code Toolpaths Materials\n");
                fprintf(fp, "# Generated by %s-%s based on Slic3r\n", SLIC3R_APP_NAME, SLIC3R_VERSION);

                fprintf(fp, "\nnewmtl material_1\n");
                fprintf(fp, "Ka 1 1 1\n");
                fprintf(fp, "Kd 1 1 1\n");
                fprintf(fp, "Ks 0 0 0\n");
                switch (m_view_type)
                {
                case EViewType::Height:
                case EViewType::Width:
                case EViewType::Feedrate:
                case EViewType::Temperature:
                case EViewType::LayerTime:
                case EViewType::VolumetricRate:
                {
                    fprintf(fp, "map_Kd range_color.png\n");
                    export_image(m_p_color_range_texture, parent_path.wstring() + "/range_color.png");
                    break;
                }
                case EViewType::FeatureType:
                {
                    fprintf(fp, "map_Kd Extrusion_Role_Colors.png\n");
                    export_image(m_p_role_colors_texture, parent_path.wstring() + "/Extrusion_Role_Colors.png");
                    break;
                }
                case EViewType::Tool:
                case EViewType::Summary:
                case EViewType::ColorPrint:
                {
                    fprintf(fp, "map_Kd tool_colors.png\n");
                    export_image(m_p_tool_colors_texture, parent_path.wstring() + "/tool_colors.png");
                    break;
                }
                }

                fprintf(fp, "\nnewmtl material_2\n");
                fprintf(fp, "Ka 1 1 1\n");
                fprintf(fp, "Kd 0.25 0.25 0.25\n");
                fprintf(fp, "Ks 0 0 0\n");

                fclose(fp);

                // save geometry file
                fp = boost::nowide::fopen(filename, "w");
                if (fp == nullptr) {
                    BOOST_LOG_TRIVIAL(error) << "LegacyRenderer::export_toolpaths_to_obj: Couldn't open " << filename << " for writing";
                    return;
                }
                fprintf(fp, "# G-Code Toolpaths\n");
                fprintf(fp, "# Generated by %s-%s based on Slic3r\n", SLIC3R_APP_NAME, SLIC3R_VERSION);

                m_p_layer_manager->update_visibile_segment_list(true, m_tools.m_tool_visibles);
                m_p_layer_manager->update_transient_segment_list(true);
                const int top_layer_index = m_p_layer_manager->get_current_layer_end();
                const int bottom_layer_index = m_p_layer_manager->get_current_layer_start();

                std::vector<Vec3f> out_vertices;
                std::vector<Vec3f> out_normals;
                std::vector<Vec2f> out_uvs;
                std::vector<uint32_t> colored_indices;
                std::vector<uint32_t> other_indices;
                const auto vertex_count_per_seg = 10;
                out_vertices.reserve(vertex_count_per_seg * 10000);
                out_normals.reserve(vertex_count_per_seg * 10000);
                out_uvs.reserve(vertex_count_per_seg * 10000);
                colored_indices.reserve(16 * 3 * 10000);
                other_indices.reserve(16 * 3 * 10000);

                const bool b_top_layer_only = show_sequential_view();
                auto data_range = get_range_according_to_view_type(m_view_type);
                Range::EType range_type = Range::EType::Linear;
                if (EViewType::LayerTime == m_view_type) {
                    range_type = Range::EType::Logarithmic;
                    data_range.min = log(data_range.min);
                    data_range.max = log(data_range.max);
                }
                const bool is_range_data_valid = data_range.max - data_range.min > 1e-6f;
                Vec2f uv{ 0.0f, 0.5f };

                const auto add_indices = [](std::vector<uint32_t>& indices, const uint32_t base_index)->void {
                    indices.push_back(base_index + 0);
                    indices.push_back(base_index + 2);
                    indices.push_back(base_index + 1);

                    indices.push_back(base_index + 0);
                    indices.push_back(base_index + 3);
                    indices.push_back(base_index + 2);

                    indices.push_back(base_index + 0);
                    indices.push_back(base_index + 1);
                    indices.push_back(base_index + 5);

                    indices.push_back(base_index + 0);
                    indices.push_back(base_index + 5);
                    indices.push_back(base_index + 4);

                    indices.push_back(base_index + 1);
                    indices.push_back(base_index + 2);
                    indices.push_back(base_index + 6);

                    indices.push_back(base_index + 1);
                    indices.push_back(base_index + 6);
                    indices.push_back(base_index + 5);

                    indices.push_back(base_index + 2);
                    indices.push_back(base_index + 3);
                    indices.push_back(base_index + 7);

                    indices.push_back(base_index + 2);
                    indices.push_back(base_index + 7);
                    indices.push_back(base_index + 6);

                    indices.push_back(base_index + 3);
                    indices.push_back(base_index + 0);
                    indices.push_back(base_index + 4);

                    indices.push_back(base_index + 3);
                    indices.push_back(base_index + 4);
                    indices.push_back(base_index + 7);

                    indices.push_back(base_index + 4);
                    indices.push_back(base_index + 5);
                    indices.push_back(base_index + 6);

                    indices.push_back(base_index + 4);
                    indices.push_back(base_index + 6);
                    indices.push_back(base_index + 7);

                    // for left side
                    indices.push_back(base_index + 0);
                    indices.push_back(base_index + 3);
                    indices.push_back(base_index + 8);

                    indices.push_back(base_index + 8);
                    indices.push_back(base_index + 3);
                    indices.push_back(base_index + 2);
                    // for right side
                    indices.push_back(base_index + 0);
                    indices.push_back(base_index + 9);
                    indices.push_back(base_index + 1);

                    indices.push_back(base_index + 1);
                    indices.push_back(base_index + 9);
                    indices.push_back(base_index + 2);
                    };

                uint32_t final_seg_count = 0;
                auto do_export = [&](const std::vector<float>& seg_list
                    , const std::vector<PositionData>& position_data_list
                    , const std::vector<SegmentVertex>& segment_vertex_list
                    , bool b_top_layer)->void {
                    const auto seg_count = seg_list.size() / 4;
                    if (!seg_count) {
                        return;
                    }
                    for (size_t i = 0; i < seg_count; ++i) {
                        const int t_seg_start_index = seg_list[4 * i];
                        const int t_seg_end_index = seg_list[4 * i + 1];
                        const auto& startPos_mid = position_data_list[t_seg_start_index];
                        const auto& endPos_mid = position_data_list[t_seg_end_index];

                        const int t_segment_vertex_index = int(endPos_mid.m_segment_vertex_index);
                        const auto& t_segment_vertex = segment_vertex_list[t_segment_vertex_index];
                        const auto& t_move = m_gcode_result->moves[t_segment_vertex.m_move_id];
                        if (t_move.type != EMoveType::Extrude) {
                            continue;
                        }

                        const float t_move_range_data = get_move_data_from_view_type(m_view_type, t_move);
                        if (is_range_data_valid) {
                            if (range_type == Range::EType::Linear) {
                                uv.x() = (t_move_range_data - data_range.min) / (data_range.max - data_range.min);
                            }
                            else {
                                uv.x() = (log(t_move_range_data) - data_range.min) / (data_range.max - data_range.min);
                            }
                        }

                        Vec3f line = endPos_mid.m_position - startPos_mid.m_position;
                        Vec3f line_dir = Vec3f(1.0, 0.0, 0.0);
                        float line_len = line.norm();
                        line_dir = line / std::max(line_len, 1e-6f);

                        Vec3f right_dir = Vec3f(line_dir.y(), -line_dir.x(), 0.0);
                        right_dir.normalize();
                        line_dir.normalize();
                        const Vec3f left_dir = -right_dir;
                        const Vec3f up = right_dir.cross(line_dir);
                        const Vec3f down = -up;

                        float half_width = 0.5 * t_move.width;
                        float half_height = 0.5 * t_move.height;
                        Vec3f d_up = half_height * up;
                        Vec3f d_right = half_width * right_dir;
                        Vec3f d_down = half_height * down;
                        Vec3f d_left = half_width * left_dir;

                        //start side
                        const auto position_start_center = startPos_mid.m_position - half_height * up;
                        auto position_start_up = position_start_center + d_up;
                        out_vertices.emplace_back(std::move(position_start_up));
                        out_normals.emplace_back(up);
                        out_uvs.emplace_back(uv);

                        auto position_start_right = position_start_center + d_right;
                        out_vertices.emplace_back(std::move(position_start_right));
                        out_normals.emplace_back(right_dir);
                        out_uvs.emplace_back(uv);

                        auto position_start_down = position_start_center + d_down;
                        out_vertices.emplace_back(std::move(position_start_down));
                        out_normals.emplace_back(down);
                        out_uvs.emplace_back(uv);

                        auto position_start_left = position_start_center + d_left;
                        out_vertices.emplace_back(std::move(position_start_left));
                        out_normals.emplace_back(left_dir);
                        out_uvs.emplace_back(uv);

                        // end side
                        const auto position_end_center = endPos_mid.m_position - half_height * up;
                        auto position_end_up = position_end_center + d_up;
                        out_vertices.emplace_back(std::move(position_end_up));
                        out_normals.emplace_back(up);
                        out_uvs.emplace_back(uv);

                        auto position_end_right = position_end_center + d_right;
                        out_vertices.emplace_back(std::move(position_end_right));
                        out_normals.emplace_back(right_dir);
                        out_uvs.emplace_back(uv);

                        auto position_end_down = position_end_center + d_down;
                        out_vertices.emplace_back(std::move(position_end_down));
                        out_normals.emplace_back(down);
                        out_uvs.emplace_back(uv);

                        auto position_end_left = position_end_center + d_left;
                        out_vertices.emplace_back(std::move(position_end_left));
                        out_normals.emplace_back(left_dir);
                        out_uvs.emplace_back(uv);

                        const bool has_prev = seg_list[4 * i + 2] > 0.5;
                        const int prev_index = seg_list[4 * i + 3];
                        if (has_prev) {
                            const auto& prevPos_mid = position_data_list[prev_index];
                            Vec3f prev_dir = startPos_mid.m_position - prevPos_mid.m_position;
                            prev_dir = prev_dir / std::max(prev_dir.norm(), 1e-6f);
                            prev_dir.normalize();
                            Vec3f prev_right_dir = Vec3f(prev_dir.y(), -prev_dir.x(), 0.0);
                            prev_right_dir.normalize();
                            Vec3f prev_left_dir = -prev_right_dir;
                            Vec3f prev_up = prev_right_dir.cross(prev_dir);

                            Vec3f prev_end_pos_center = startPos_mid.m_position - half_height * prev_up;

                            auto prev_end_left = prev_end_pos_center + half_width * prev_left_dir;
                            out_vertices.emplace_back(std::move(prev_end_left));
                            out_normals.emplace_back(prev_left_dir);
                            out_uvs.emplace_back(uv);

                            auto prev_end_right = prev_end_pos_center + half_width * prev_right_dir;
                            out_vertices.emplace_back(std::move(prev_end_right));
                            out_normals.emplace_back(prev_right_dir);
                            out_uvs.emplace_back(uv);
                        }
                        else {
                            auto prev_end_left = out_vertices[10 * final_seg_count + 3];
                            out_vertices.emplace_back(std::move(prev_end_left));
                            out_normals.emplace_back(left_dir);
                            out_uvs.emplace_back(uv);

                            auto prev_end_right = out_vertices[10 * final_seg_count + 1];
                            out_vertices.emplace_back(std::move(prev_end_right));
                            out_normals.emplace_back(right_dir);
                            out_uvs.emplace_back(uv);
                        }

                        auto base_index = 10 * final_seg_count;
                        if (!b_top_layer_only || b_top_layer) {
                            add_indices(colored_indices, base_index);
                        }
                        else {
                            add_indices(other_indices, base_index);
                        }
                        ++final_seg_count;
                    }
                };

                // export top layer
                do_export(m_p_layer_manager->get_transient_other_segment_list()
                    , (*m_p_layer_manager)[top_layer_index].get_position_data()
                    , (*m_p_layer_manager)[top_layer_index].get_segment_vertices()
                    , true);
                // end export top layer
                for (int i_layer = bottom_layer_index; i_layer < top_layer_index; ++i_layer) {
                    Layer& t_layer = (*m_p_layer_manager)[i_layer];
                    const auto& seg_list = t_layer.get_other_segment_list();
                    const auto& t_position_data = t_layer.get_position_data();
                    const auto& t_segment_vertex_list = t_layer.get_segment_vertices();

                    do_export(seg_list, t_position_data, t_segment_vertex_list, false);
                }

                fprintf(fp, "\n# vertices\n");
                for (const Vec3f& v : out_vertices) {
                    fprintf(fp, "v %g %g %g\n", v.x(), v.y(), v.z());
                }
                // save normals to file
                fprintf(fp, "\n# normals\n");
                for (const Vec3f& n : out_normals) {
                    fprintf(fp, "vn %g %g %g\n", n.x(), n.y(), n.z());
                }

                // save uvs to file
                fprintf(fp, "\n# uvs\n");
                for (const Vec2f& uv : out_uvs) {
                    fprintf(fp, "vt %g %g \n", uv.x(), uv.y());
                }

                fprintf(fp, "\nusemtl material_%zu\n", 1);
                fprintf(fp, "# triangles material %zu\n", 1);

                if (colored_indices.size()) {
                    for (int i = 0; i < colored_indices.size(); i += 3) {
                        size_t v1 = colored_indices[i] + 1;
                        size_t v2 = colored_indices[i + 1] + 1;
                        size_t v3 = colored_indices[i + 2] + 1;
                        fprintf(fp, "f %zu/%zu/%zu %zu/%zu/%zu %zu/%zu/%zu\n", v1, v1, v1, v2, v2, v2, v3, v3, v3);
                    }
                }

                if (other_indices.size()) {
                    fprintf(fp, "\nusemtl material_%zu\n", 2);
                    fprintf(fp, "# triangles material %zu\n", 2);

                    for (int i = 0; i < other_indices.size(); i += 3) {
                        size_t v1 = other_indices[i] + 1;
                        size_t v2 = other_indices[i + 1] + 1;
                        size_t v3 = other_indices[i + 2] + 1;
                        fprintf(fp, "f %zu/%zu/%zu %zu/%zu/%zu %zu/%zu/%zu\n", v1, v1, v1, v2, v2, v2, v3, v3, v3);
                    }
                }

                fclose(fp);
            }

            void AdvancedRenderer::set_layers_z_range(const std::array<unsigned int, 2>& layers_z_range)
            {
                if (!m_p_layer_manager) {
                    return;
                }
                m_p_layer_manager->set_current_layer_start(layers_z_range[0]);
                m_p_layer_manager->set_current_layer_end(layers_z_range[1]);
            }

            void AdvancedRenderer::render(int canvas_width, int canvas_height, int right_margin)
            {
                if (m_b_loading) {
                    return;
                }
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
                if (!m_p_layer_manager || m_p_layer_manager->empty()) {
                    return;
                }

                m_p_layer_manager->set_view_type(m_view_type);

                bool b_needs_to_update_move_slider = m_p_layer_manager->update_visibile_segment_list(false, m_tools.m_tool_visibles);

                if (m_p_layer_manager->is_layer_dirty()) {
                    b_needs_to_update_move_slider = true;
                    m_p_layer_manager->clear_layer_dirty();
                }

                if (b_needs_to_update_move_slider) {
                    update_moves_slider(true);
                }

                m_p_layer_manager->update_transient_segment_list(b_needs_to_update_move_slider);

                m_p_layer_manager->update_per_move_data(m_view_type, *m_gcode_result);

                render_toolpaths();
                //render_shells();
                render_legend(m_legend_height, canvas_width, canvas_height, right_margin);
                if (m_user_mode != wxGetApp().get_mode()) {
                    update_by_mode(wxGetApp().get_mode());
                    m_user_mode = wxGetApp().get_mode();
                }
                if (m_p_sequential_view) {
                    m_p_sequential_view->current_position = Vec3f::Zero();
                    m_p_sequential_view->current.last = 0;
                    if (show_sequential_view()) {
                        const auto t_current_mid = m_p_layer_manager->get_current_move_id();
                        if (m_gcode_result) {
                            const auto& t_move_vertex = m_gcode_result->moves[t_current_mid];
                            m_p_sequential_view->current_position = t_move_vertex.position;
                            m_p_sequential_view->current.last = t_current_mid;
                            m_p_sequential_view->marker.update_curr_move(t_move_vertex);
                        }
                        render_sequential_view(canvas_width, canvas_height, right_margin);
                    }
                }
#if ENABLE_GCODE_VIEWER_STATISTICS
                render_statistics();
#endif // ENABLE_GCODE_VIEWER_STATISTICS
                //BBS render slider
                render_slider(canvas_width, canvas_height);
            }

            void AdvancedRenderer::reset()
            {
                BaseRenderer::reset();
                m_b_tool_colors_dirty = true;

                if (m_p_layer_manager) {
                    m_p_layer_manager->reset();
                }
            }

            bool AdvancedRenderer::is_move_type_visible(EMoveType type) const
            {
                const auto& p_layer_manager = get_layer_manager();
                if (p_layer_manager) {
                    return p_layer_manager->is_move_type_visible(type);
                }
                return false;
            }

            void AdvancedRenderer::set_move_type_visible(EMoveType type, bool visible)
            {
                const auto& p_layer_manager = get_layer_manager();
                if (p_layer_manager) {
                    p_layer_manager->set_move_type_visible(type, visible);
                }
            }

            bool AdvancedRenderer::is_extrusion_role_visible(ExtrusionRole role) const
            {
                const auto& p_layer_manager = get_layer_manager();
                if (p_layer_manager) {
                    return p_layer_manager->is_extrusion_role_visible(role);
                }
                return false;
            }

            void AdvancedRenderer::set_extrusion_role_visible(ExtrusionRole role, bool visible)
            {
                const auto& p_layer_manager = get_layer_manager();
                if (p_layer_manager) {
                    p_layer_manager->set_extrusion_role_visible(role, visible);
                }
            }

            uint32_t AdvancedRenderer::get_extrusion_role_visibility_flags() const
            {
                const auto& p_layer_manager = get_layer_manager();
                if (p_layer_manager) {
                    p_layer_manager->get_extrusion_role_visibility_flags();
                }
                return 0;
            }

            void AdvancedRenderer::set_extrusion_role_visibility_flags(uint32_t flags)
            {
            }

            void AdvancedRenderer::update_marker_curr_move()
            {
            }

            bool AdvancedRenderer::load_toolpaths(const GCodeProcessorResult& gcode_result, const BuildVolume& build_volume, const std::vector<BoundingBoxf3>& exclude_bounding_box)
            {
                const auto t_move_count = gcode_result.moves.size();
                if (0 == t_move_count)
                    return false;

                //BBS: add only gcode mode
                wxBusyCursor busy;

                m_b_loading = true;
                m_extruders_count = gcode_result.filaments_count;
                load_layer_info(gcode_result, build_volume, exclude_bounding_box);
                m_b_loading = false;

                if (!m_p_layer_manager || !m_p_layer_manager->size()) {
                    return false;
                }

                m_conflict_result = gcode_result.conflict_result;
                if (m_conflict_result) {
                    const auto t_layer_count = m_p_layer_manager->size();
                    const auto conflict_height = m_conflict_result.value()._height;
                    int tLayerId = -1;
                    for (size_t i = 0; i < t_layer_count; ++i) {
                        const auto& tpLayer = (*m_p_layer_manager)[i];
                        if (tpLayer.get_z() < conflict_height) {
                            continue;
                        }
                        tLayerId = i + 1;
                        break;
                    }
                    m_conflict_result.value().layer = tLayerId;
                }

                return true;
            }

            void AdvancedRenderer::update_moves_slider(bool set_to_max)
            {
                if (!m_p_layer_manager) {
                    return;
                }
                const auto& current_end_index = m_p_layer_manager->get_current_layer_end();
                const auto& t_current_top_layer = m_p_layer_manager->get_layer(current_end_index);
                if (!t_current_top_layer.is_valid()) {
                    return;
                }
                const auto t_seg_count = t_current_top_layer.get_visible_segment_list().size();
                if (t_seg_count < 0) {
                    return;
                }
                std::vector<double> values(t_seg_count + 1);
                unsigned int        count = 0;
                for (unsigned int i = 0; i <= t_seg_count; ++i) {
                    values[count] = static_cast<double>(i + 1);
                    ++count;
                }
                m_moves_slider->SetSliderValues(values);
                m_moves_slider->SetMaxValue(t_seg_count);
                m_moves_slider->SetSelectionSpan(0, m_moves_slider->GetMaxValue());
                if (set_to_max)
                    m_moves_slider->SetHigherValue(m_moves_slider->GetMaxValue());
                m_p_layer_manager->set_current_move_start(0);
                m_p_layer_manager->set_current_move_end(t_seg_count);
            }

            bool AdvancedRenderer::show_sequential_view() const
            {
                const auto& p_layer_manager = get_layer_manager();
                if (!p_layer_manager) {
                    return false;
                }
                const auto& t_layer = p_layer_manager->get_layer(p_layer_manager->get_current_layer_end());
                const auto& t_visibile_segments = t_layer.get_visible_segment_list();
                const auto t_current_move_end = p_layer_manager->get_current_move_end();
                const bool rt = (t_current_move_end != t_visibile_segments.size());
                return rt;
            }

            void AdvancedRenderer::on_visibility_changed()
            {
                const auto& p_layer_manager = get_layer_manager();
                if (!p_layer_manager) {
                    return;
                }
                p_layer_manager->on_filament_visible_changed();
            }

            void AdvancedRenderer::load_layer_info(const GCodeProcessorResult& gcode_result, const BuildVolume& build_volume, const std::vector<BoundingBoxf3>& exclude_bounding_box)
            {
                const auto& p_layer_manager = get_layer_manager();
                if (!p_layer_manager) {
                    return;
                }
                const auto& p_sequential_view = get_sequential_view();
                // layers zs / roles / extruder ids -> extract from result
                size_t last_travel_s_id = 0;
                m_extruder_ids.clear();
                size_t seams_count = 0;
                std::vector<std::vector<size_t>> t_sid_to_seamMoveIds;
                std::vector<size_t> biased_seams_ids;
                const auto t_move_count = gcode_result.moves.size();
                p_layer_manager->reset();
                t_sid_to_seamMoveIds.resize(t_move_count);

                // BBS: add only gcode mode
                ProgressDialog *progress_dialog = m_only_gcode_in_preview ?
                                                      new ProgressDialog(_L("Loading G-codes"), "...", 100, wxGetApp().mainframe, wxPD_AUTO_HIDE | wxPD_APP_MODAL) :
                                                      nullptr;

                int last_progress = 0;
                //BBS: use convex_hull for toolpath outside check
                Points pts;
                for (size_t i = 0; i < t_move_count; ++i) {
                    const GCodeProcessorResult::MoveVertex& move = gcode_result.moves[i];
                    if (move.type == EMoveType::Seam) {
                        ++seams_count;
                        biased_seams_ids.push_back(i - biased_seams_ids.size() - 1);
                    }
                    p_sequential_view->gcode_ids.push_back(move.gcode_id);

                    size_t move_id = i - seams_count;

                    if (move.type == EMoveType::Extrude) {

                        if (move.width > 1e-6f && move.height > 1e-6f) {
                            if (move.extrusion_role != erCustom) {
                                m_paths_bounding_box.merge(move.position.cast<double>());
                                //BBS: use convex_hull for toolpath outside check
                                pts.emplace_back(Point(scale_(move.position.x()), scale_(move.position.y())));
                            }
                            if (move.is_arc_move_with_interpolation_points()) {
                                for (int i = 0; i < move.interpolation_points.size(); i++) {
                                    m_paths_bounding_box.merge(move.interpolation_points[i].cast<double>());
                                    //BBS: use convex_hull for toolpath outside check
                                    pts.emplace_back(Point(scale_(move.interpolation_points[i].x()), scale_(move.interpolation_points[i].y())));
                                }
                            }
                        }

                        // layers zs
                        const double z = static_cast<double>(move.position.z());
                        bool needs_new_layer = p_layer_manager->empty();
                        if (!needs_new_layer) {
                            const auto& last_layer = p_layer_manager->get_layer(p_layer_manager->size() - 1);
                            const auto last_z = last_layer.get_z();
                            needs_new_layer = std::abs(z - last_z) > EPSILON;
                        }
                        if (needs_new_layer) {
                            Layer t_layer;
                            t_layer.set_start(last_travel_s_id)
                                .set_end(move_id)
                                .set_z(z);
                            p_layer_manager->add_layer(t_layer);
                        }
                        else {
                            auto& last_layer = p_layer_manager->get_layer(p_layer_manager->size() - 1);
                            last_layer.set_end(move_id);
                        }
                        // extruder ids
                        m_extruder_ids.emplace_back(move.extruder_id);
                        // roles
                        if (i > 0)
                            m_roles.emplace_back(move.extrusion_role);
                    }
                    else if (move.type == EMoveType::Travel) {
                        if (move_id - last_travel_s_id > 0 && !p_layer_manager->empty()) {
                            auto& last_layer = p_layer_manager->get_layer(p_layer_manager->size() - 1);
                            last_layer.set_end(move_id);
                        }
                        last_travel_s_id = move_id;
                    }
                    else if (move.type == EMoveType::Unretract && move.extrusion_role == ExtrusionRole::erFlush) {
                        m_roles.emplace_back(move.extrusion_role);
                    }
                    else if (move.type == EMoveType::Seam) {
                        t_sid_to_seamMoveIds[move_id].emplace_back(i);
                    }

                    if (progress_dialog != nullptr) {
                        float progress_value = 100.0f * float(i + 1) / float(t_move_count) * 0.5f;
                        if (int(progress_value) != last_progress && int(progress_value) % 10 == 0) {
                            progress_dialog->Update(int(progress_value),
                                                    _L("Loading gcode data") + ": " + wxNumberFormatter::ToString(progress_value, 0, wxNumberFormatter::Style_None) + "%");
                            progress_dialog->Fit();
                            last_progress = int(progress_value);
                        }
                    }
                }

                // set approximate max bounding box (take in account also the tool marker)
                m_max_bounding_box = m_paths_bounding_box;
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
                m_ssid_to_moveid_map.reserve(t_move_count - biased_seams_ids.size());
                for (size_t i = 0; i < t_move_count - biased_seams_ids.size(); i++)
                    m_ssid_to_moveid_map.push_back(extract_move_id(i));

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
                    p_layer_manager->clear();
                    for (const auto& layer : gcode_result.spiral_vase_layers) {
                        Layer t_layer;
                        t_layer.set_start(layer.second.first)
                            .set_end(layer.second.second)
                            .set_z(layer.first);
                        p_layer_manager->add_layer(std::move(t_layer));
                    }
                }

                // set layers z range
                if (p_layer_manager->empty()) {
                    return;
                }

                last_progress = 0;
                const auto t_layer_count = p_layer_manager->size();
                for (size_t i = 0; i < t_layer_count; ++i) {
                    auto& t_layer = (*p_layer_manager)[i];
                    t_layer.init_sgments(m_ssid_to_moveid_map, t_sid_to_seamMoveIds, gcode_result);
                    if (progress_dialog != nullptr) {
                        float progress_value = 100.0f * float(i + 1) / float(t_layer_count);
                        if (int(progress_value) != last_progress && int(progress_value) % 10 == 0) {
                            progress_dialog->Update(int(progress_value),
                                                    _L("Loading segments") + ": " + wxNumberFormatter::ToString(progress_value, 0, wxNumberFormatter::Style_None) + "%");
                            progress_dialog->Fit();
                            last_progress = int(progress_value);
                        }
                    }
                }

                if (progress_dialog != nullptr) {
                    progress_dialog->Update(100, "");
                    progress_dialog->Fit();
                    delete progress_dialog;
                }

                m_ssid_to_moveid_map.clear();

                set_layers_z_range({ 0, static_cast<unsigned int>(p_layer_manager->size()) - 1 });
            }

            const std::shared_ptr<LayerManager>& AdvancedRenderer::get_layer_manager() const
            {
                if (!m_p_layer_manager) {
                    m_p_layer_manager = std::make_shared<LayerManager>();
                }
                return m_p_layer_manager;
            }

            void AdvancedRenderer::render_toolpaths()
            {
                const auto& p_layer_manager = get_layer_manager();
                if (!p_layer_manager) {
                    return;
                }

                if (p_layer_manager->empty()) {
                    return;
                }

                p_layer_manager->conform_layer_range_valid();

                const int top_layer_index = p_layer_manager->get_current_layer_end();
                const int bottom_layer_index = p_layer_manager->get_current_layer_start();

                std::vector<uint32_t> t_opaque_list;
                std::vector<uint32_t> t_transparency_list;
                t_opaque_list.reserve(top_layer_index - bottom_layer_index + 1);
                t_transparency_list.reserve(10);
                for (int i = top_layer_index; i >= bottom_layer_index; --i) {
                    t_opaque_list.emplace_back(i);
                    const auto& t_layer = (*p_layer_manager)[i];
                    if (t_layer.get_effects().size()) {
                        t_transparency_list.emplace_back(i);
                    }
                }
                if (t_opaque_list.size() > 2) {
                    std::swap(t_opaque_list[1], t_opaque_list[t_opaque_list.size() - 1]);
                }

                const bool b_top_layer_only = show_sequential_view();

                // render EMoveType::Tool_change && EMoveType::Color_change && EMoveType::Pause_Print && EMoveType::Custom_GCode && EMoveType::Retract && EMoveType::Unretract && EMoveType::Seam:
                initialize_diamond();
                do_render_options(t_opaque_list, b_top_layer_only);

                // render EMoveType::Wipe && EMoveType::Extrude && EMoveType::Travel
                initialize_triangular_prism();
                do_render_others(t_opaque_list, b_top_layer_only);

                do_render_transparency(t_transparency_list);
            }

            void AdvancedRenderer::do_render_others(const std::vector<uint32_t>& layer_index_list, bool top_layer_only)
            {
                if (layer_index_list.empty()) {
                    return;
                }

                const auto& p_layer_manager = get_layer_manager();
                if (!p_layer_manager) {
                    return;
                }

                // draw opaque
                const auto& p_shader = wxGetApp().get_shader("gcode");
                if (!p_shader) {
                    return;
                }
                std::vector<uint32_t> vertex_attribute_id_list;
                wxGetApp().bind_shader(p_shader);

                const Camera& camera = wxGetApp().plater()->get_camera();
                const Transform3d& view_matrix = camera.get_view_matrix();
                p_shader->set_uniform("view_model_matrix", view_matrix);
                p_shader->set_uniform("projection_matrix", camera.get_projection_matrix());
                p_shader->set_uniform("normal_matrix", (Matrix3d)view_matrix.matrix().block(0, 0, 3, 3).inverse().transpose());

                auto data_range = get_range_according_to_view_type(m_view_type);
                Range::EType range_type = Range::EType::Linear;
                if (EViewType::LayerTime == m_view_type) {
                    range_type = Range::EType::Logarithmic;
                    data_range.min = log(data_range.min);
                    data_range.max = log(data_range.max);
                }
                p_shader->set_uniform("u_isRangeView_isRangeVaild_topLayerOnly_viewType", Vec4f(1.0f, float(data_range.max - data_range.min > 1e-6f), float(top_layer_only), float(m_view_type)));

                p_shader->set_uniform("u_pathDataRange", Vec2f(
                    data_range.min,
                    data_range.max));

                bool b_unlit = false;
                p_shader->set_uniform("u_rangeType_isUnlit_topLayerIndex", Vec3f(float(range_type), float(b_unlit), float(p_layer_manager->get_current_layer_end())));

                uint8_t texture_stage = 0;
                int tool_colos_count = -1;
                switch (m_view_type) {
                case EViewType::Summary:
                case EViewType::ColorPrint:
                {
                    tool_colos_count = m_tools.m_tool_colors.size();
                    const uint8_t color_range_stage = texture_stage;
                    bind_tool_colors_texture(color_range_stage);
                    p_shader->set_uniform("s_color_range_texture", color_range_stage);
                    break;
                }
                case EViewType::Height:
                case EViewType::Width:
                case EViewType::Feedrate:
                case EViewType::FanSpeed:
                case EViewType::Temperature:
                case EViewType::LayerTime:
                case EViewType::VolumetricRate:
                {
                    const uint8_t color_range_stage = texture_stage;
                    bind_color_range_texture(color_range_stage);
                    p_shader->set_uniform("s_color_range_texture", color_range_stage);
                    break;
                }
                case EViewType::FeatureType:
                {
                    const uint8_t color_range_stage = texture_stage;
                    bind_role_colors_texture(color_range_stage);
                    p_shader->set_uniform("s_color_range_texture", color_range_stage);
                    break;
                }
                // helio
                case EViewType::ThermalIndexMin:
                case EViewType::ThermalIndexMax:
                case EViewType::ThermalIndexMean:
                {
                    const uint8_t color_range_stage = texture_stage;
                    bind_thermal_index_range_colors_texture(color_range_stage);
                    p_shader->set_uniform("s_color_range_texture", color_range_stage);
                    break;
                }
                // end helio
                }

                for (size_t i = 0; i < layer_index_list.size(); ++i) {

                    auto& t_layer = (*p_layer_manager)[layer_index_list[i]];

                    t_layer.update_position_data_texture();

                    t_layer.update_width_height_data_texture(*m_gcode_result);

                    t_layer.update_per_move_data_texture();

                    if (0 == i) {
                        m_p_layer_manager->update_transient_other_texture();
                    }
                    else {
                        t_layer.update_other_segment_texture();
                    }

                    p_shader->set_uniform("u_isTopLayer_hasCustomOptins", Vec2f(float(i == 0), 0.0f));

                    uint8_t position_texture_stage = texture_stage + 1;
                    t_layer.bind_position_data_texture(position_texture_stage);
                    p_shader->set_uniform("s_position_texture", position_texture_stage);

                    uint8_t width_height_texture_stage = texture_stage + 2;
                    t_layer.bind_width_height_data_texture(width_height_texture_stage);
                    p_shader->set_uniform("s_width_height_data_texture", width_height_texture_stage);

                    uint8_t per_move_data_texture_stage = texture_stage + 3;
                    t_layer.bind_per_move_data_texture(per_move_data_texture_stage);
                    p_shader->set_uniform("s_per_move_data_texture", per_move_data_texture_stage);

                    uint32_t seg_count = 0;
                    uint8_t segment_texture_stage = texture_stage + 4;
                    if (0 == i) {
                        p_layer_manager->bind_transient_other_texture(segment_texture_stage);
                        seg_count = p_layer_manager->get_transient_other_segment_count();
                    }
                    else {
                        t_layer.bind_other_segment_texture(segment_texture_stage);
                        seg_count = t_layer.get_other_segment_count();
                    }
                    p_shader->set_uniform("s_segment_texture", segment_texture_stage);
                    m_triangular_prism.render_geometry_instance(0, seg_count);
                }
                // end draw opaque

                wxGetApp().unbind_shader();
            }

            void AdvancedRenderer::do_render_options(const std::vector<uint32_t>& layer_index_list, bool top_layer_only)
            {
                if (layer_index_list.empty()) {
                    return;
                }

                const auto& p_layer_manager = get_layer_manager();
                if (!p_layer_manager) {
                    return;
                }

                const auto& p_shader = wxGetApp().get_shader("gcode_options");
                if (!p_shader) {
                    return;
                }
                std::vector<uint32_t> vertex_attribute_id_list;
                wxGetApp().bind_shader(p_shader);

                const Camera& camera = wxGetApp().plater()->get_camera();
                const Transform3d& view_matrix = camera.get_view_matrix();
                p_shader->set_uniform("view_model_matrix", view_matrix);
                p_shader->set_uniform("projection_matrix", camera.get_projection_matrix());
                p_shader->set_uniform("normal_matrix", (Matrix3d)view_matrix.matrix().block(0, 0, 3, 3).inverse().transpose());
                bool b_unlit = false;
                p_shader->set_uniform("u_isUnlit_optionTextureSize_topLayerOnly_emissionFactor", Vec4f(float(b_unlit), float(Options_Colors.size()), float(top_layer_only), 0.25f));

                uint8_t texture_stage = 0;
                const uint8_t option_color_texture_stage = texture_stage;
                bind_option_colors_texture(option_color_texture_stage);
                p_shader->set_uniform("s_option_color_texture", option_color_texture_stage);

                for (size_t i = 0; i < layer_index_list.size(); ++i)
                {
                    p_shader->set_uniform("u_is_top_layer", float(i == 0));

                    auto& t_layer = (*p_layer_manager)[layer_index_list[i]];

                    t_layer.update_position_data_texture();

                    t_layer.update_width_height_data_texture(*m_gcode_result);

                    t_layer.update_per_move_data_texture();

                    if (0 == i) {
                        m_p_layer_manager->update_transient_options_texture();
                    }
                    else {
                        t_layer.update_options_segment_texture();
                    }

                    uint8_t position_texture_stage = texture_stage + 1;
                    t_layer.bind_position_data_texture(position_texture_stage);
                    p_shader->set_uniform("s_position_texture", position_texture_stage);

                    uint8_t width_height_data_texture_stage = texture_stage + 2;
                    t_layer.bind_width_height_data_texture(width_height_data_texture_stage);
                    p_shader->set_uniform("s_width_height_data_texture", width_height_data_texture_stage);

                    uint8_t per_move_data_texture_stage = texture_stage + 3;
                    t_layer.bind_per_move_data_texture(per_move_data_texture_stage);
                    p_shader->set_uniform("s_per_move_data_texture", per_move_data_texture_stage);

                    uint32_t seg_count = 0;
                    uint8_t segment_texture_stage = texture_stage + 4;
                    if (0 == i) {
                        p_layer_manager->bind_transient_options_texture(segment_texture_stage);
                        seg_count = p_layer_manager->get_transient_options_segment_count();
                    }
                    else {
                        t_layer.bind_options_segment_texture(segment_texture_stage);
                        seg_count = t_layer.get_options_segment_count();
                    }
                    p_shader->set_uniform("s_segment_texture", segment_texture_stage);
                    m_diamond.render_geometry_instance(0, seg_count);
                }
                wxGetApp().unbind_shader();
            }

            void AdvancedRenderer::do_render_transparency(const std::vector<uint32_t>& layer_index_list)
            {
                if (layer_index_list.empty()) {
                    return;
                }

                if (m_view_type != EViewType::Summary && m_view_type != EViewType::ColorPrint) {
                    return;
                }

                const auto& p_layer_manager = get_layer_manager();
                if (!p_layer_manager) {
                    return;
                }

                const Camera& camera = wxGetApp().plater()->get_camera();
                const Transform3d& view_matrix = camera.get_view_matrix();
                const auto normal_matrix = (Matrix3d)view_matrix.matrix().block(0, 0, 3, 3).inverse().transpose();
                const auto proj_matrix = camera.get_projection_matrix();

                const auto& t_top_layer_index = p_layer_manager->get_current_layer_end();

                glsafe(::glEnable(GL_BLEND));
                // Linear Dodge
                // https://gamedev.stackexchange.com/questions/17043/blend-modes-in-cocos2d-with-glblendfunc
                glsafe(::glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ONE));
                glsafe(::glDepthFunc(GL_LEQUAL));
                glsafe(::glDepthMask(GL_FALSE));

                for (size_t i = 0; i < layer_index_list.size(); ++i)
                {
                    uint8_t texture_stage = 0;
                    bool b_is_top_layer = t_top_layer_index == layer_index_list[i];
                    const auto& t_layer = p_layer_manager->get_layer(layer_index_list[i]);
                    const auto& t_effects = t_layer.get_effects();
                    for (const auto& t_effect : t_effects) {
                        const auto& p_shader = t_effect->get_shader();
                        if (!p_shader) {
                            continue;
                        }

                        wxGetApp().bind_shader(p_shader);

                        t_effect->update_uniform(p_shader);

                        p_shader->set_uniform("view_model_matrix", view_matrix);
                        p_shader->set_uniform("projection_matrix", proj_matrix);
                        p_shader->set_uniform("normal_matrix", normal_matrix);

                        uint8_t position_texture_stage = texture_stage++;
                        t_layer.bind_position_data_texture(position_texture_stage);
                        p_shader->set_uniform("s_position_texture", position_texture_stage);

                        uint8_t width_height_data_texture_stage = texture_stage++;
                        t_layer.bind_width_height_data_texture(width_height_data_texture_stage);
                        p_shader->set_uniform("s_width_height_data_texture", width_height_data_texture_stage);

                        uint32_t seg_count = 0;
                        uint8_t segment_texture_stage = texture_stage++;
                        if (b_is_top_layer) {
                            p_layer_manager->bind_transient_other_texture(segment_texture_stage);
                            seg_count = p_layer_manager->get_transient_other_segment_count();
                        }
                        else {
                            t_layer.bind_other_segment_texture(segment_texture_stage);
                            seg_count = t_layer.get_other_segment_count();
                        }
                        p_shader->set_uniform("s_segment_texture", segment_texture_stage);
                        m_triangular_prism.render_geometry_instance(0, seg_count);
                    }
                }

                glsafe(::glDisable(GL_BLEND));
                glsafe(::glDepthFunc(GL_LESS));
                glsafe(::glDepthMask(GL_TRUE));
                wxGetApp().unbind_shader();
            }

            gcode::Range AdvancedRenderer::get_range_according_to_view_type(gcode::EViewType type) const
            {
                Slic3r::GUI::gcode::Range t_range;
                t_range.min = 0;
                t_range.max = 0;
                t_range.count = 0;
                if (!m_p_extrusions) {
                    return t_range;
                }
                switch (type)
                {
                case Slic3r::GUI::gcode::EViewType::Height:
                    return m_p_extrusions->ranges.height;
                case Slic3r::GUI::gcode::EViewType::Width:
                    return m_p_extrusions->ranges.width;
                case Slic3r::GUI::gcode::EViewType::Feedrate:
                    return m_p_extrusions->ranges.feedrate;
                case Slic3r::GUI::gcode::EViewType::FanSpeed:
                    return m_p_extrusions->ranges.fan_speed;
                case Slic3r::GUI::gcode::EViewType::Temperature:
                    return m_p_extrusions->ranges.temperature;
                case Slic3r::GUI::gcode::EViewType::LayerTime:
                    return m_p_extrusions->ranges.layer_duration;
                case Slic3r::GUI::gcode::EViewType::VolumetricRate:
                    return m_p_extrusions->ranges.volumetric_rate;
                case Slic3r::GUI::gcode::EViewType::FeatureType:
                {
                    t_range.min = 0;
                    t_range.max = Extrusion_Role_Colors.size() - 1;
                    t_range.count = t_range.max - t_range.min + 1;
                    return t_range;
                }
                case Slic3r::GUI::gcode::EViewType::Tool:
                case Slic3r::GUI::gcode::EViewType::Summary:
                case Slic3r::GUI::gcode::EViewType::ColorPrint:
                {
                    t_range.min = 0;
                    t_range.max = m_tools.m_tool_colors.size() - 1;
                    t_range.count = t_range.max - t_range.min + 1;
                    return t_range;
                }
                case Slic3r::GUI::gcode::EViewType::FilamentId:
                {
                    t_range.min = 0.0f;
                    t_range.max = 256.0f;
                    return t_range;
                }
                // helio
                case Slic3r::GUI::gcode::EViewType::ThermalIndexMin:
                {
                    return m_p_extrusions->ranges.thermal_index_min;
                }
                case Slic3r::GUI::gcode::EViewType::ThermalIndexMax:
                {
                    return m_p_extrusions->ranges.thermal_index_max;
                }
                case Slic3r::GUI::gcode::EViewType::ThermalIndexMean:
                {
                    return m_p_extrusions->ranges.thermal_index_mean;
                }
                // end helio
                default:
                {
                    t_range.min = 0;
                    t_range.max = 0;
                    t_range.count = 0;
                    return t_range;
                }
                }
            }

            void AdvancedRenderer::bind_color_range_texture(uint8_t stage)
            {
                const auto color_count = Range_Colors.size();
                const uint32_t width = color_count;
                const uint32_t height = 1;
                if (!m_p_color_range_texture) {
                    m_p_color_range_texture = std::make_shared<GLTexture>();
                    m_p_color_range_texture->set_width(width)
                        .set_height(height)
                        .set_internal_format(Slic3r::GUI::ETextureFormat::RGBA8)
                        .set_sampler(Slic3r::GUI::ESamplerType::Sampler2D)
                        .set_pixel_data_format(Slic3r::GUI::EPixelFormat::RGBA)
                        .set_pixel_data_type(Slic3r::GUI::EPixelDataType::Float)
                        .set_mag_filter(ESamplerFilterMode::Linear)
                        .set_min_filter(ESamplerFilterMode::Linear)
                        .build();

                    std::vector<float> temp_data;
                    temp_data.resize(width * height * 4);
                    std::memset(temp_data.data(), 0, temp_data.size() * sizeof(float));
                    for (size_t i = 0; i < color_count; ++i) {
                        const auto& tRGBA = Range_Colors[i].get_data();
                        for (int j = 0; j < tRGBA.size(); ++j) {
                            temp_data[i * 4 + j] = tRGBA[j];
                        }
                    }
                    std::vector<uint8_t> pixel_data;
                    pixel_data.resize(temp_data.size() * sizeof(float));
                    memcpy(pixel_data.data(), temp_data.data(), temp_data.size() * sizeof(float));
                    std::shared_ptr<Slic3r::GUI::PixelBufferDescriptor> pbd = std::make_shared<Slic3r::GUI::PixelBufferDescriptor>(std::move(pixel_data), Slic3r::GUI::EPixelFormat::RGBA, Slic3r::GUI::EPixelDataType::Float);
                    m_p_color_range_texture->set_image(0, 0, 0, 0, width, height, 1, pbd);
                }

                if (m_p_color_range_texture) {
                    m_p_color_range_texture->bind(stage);
                }
            }

            void AdvancedRenderer::unbind_color_range_texture()
            {
                if (m_p_color_range_texture) {
                    m_p_color_range_texture->unbind();
                }
            }

            void AdvancedRenderer::bind_thermal_index_range_colors_texture(uint8_t stage)
            {
                const auto color_count = Thermal_Index_Range_Colors.size();
                const uint32_t width = color_count;
                const uint32_t height = 1;
                if (!m_p_thermal_index_range_colors_texture) {
                    m_p_thermal_index_range_colors_texture = std::make_shared<GLTexture>();
                    m_p_thermal_index_range_colors_texture->set_width(width)
                        .set_height(height)
                        .set_internal_format(Slic3r::GUI::ETextureFormat::RGBA8)
                        .set_sampler(Slic3r::GUI::ESamplerType::Sampler2D)
                        .set_pixel_data_format(Slic3r::GUI::EPixelFormat::RGBA)
                        .set_pixel_data_type(Slic3r::GUI::EPixelDataType::Float)
                        .set_mag_filter(ESamplerFilterMode::Linear)
                        .set_min_filter(ESamplerFilterMode::Linear)
                        .build();

                    std::vector<float> temp_data;
                    temp_data.resize(width * height * 4);
                    std::memset(temp_data.data(), 0, temp_data.size() * sizeof(float));
                    for (size_t i = 0; i < color_count; ++i) {
                        const auto& tRGBA = Thermal_Index_Range_Colors[i].get_data();
                        for (int j = 0; j < tRGBA.size(); ++j) {
                            temp_data[i * 4 + j] = tRGBA[j];
                        }
                    }
                    std::vector<uint8_t> pixel_data;
                    pixel_data.resize(temp_data.size() * sizeof(float));
                    memcpy(pixel_data.data(), temp_data.data(), temp_data.size() * sizeof(float));
                    std::shared_ptr<Slic3r::GUI::PixelBufferDescriptor> pbd = std::make_shared<Slic3r::GUI::PixelBufferDescriptor>(std::move(pixel_data), Slic3r::GUI::EPixelFormat::RGBA, Slic3r::GUI::EPixelDataType::Float);
                    m_p_thermal_index_range_colors_texture->set_image(0, 0, 0, 0, width, height, 1, pbd);
                }

                if (m_p_thermal_index_range_colors_texture) {
                    m_p_thermal_index_range_colors_texture->bind(stage);
                }
            }

            void AdvancedRenderer::unbind_thermal_index_range_colors_texture()
            {
                if (m_p_thermal_index_range_colors_texture) {
                    m_p_thermal_index_range_colors_texture->unbind();
                }
            }

            void AdvancedRenderer::bind_role_colors_texture(uint8_t stage)
            {
                const auto color_count = Extrusion_Role_Colors.size();
                const uint32_t width = color_count;
                const uint32_t height = 1;
                if (!m_p_role_colors_texture) {
                    m_p_role_colors_texture = std::make_shared<GLTexture>();
                    m_p_role_colors_texture->set_width(width)
                        .set_height(height)
                        .set_internal_format(Slic3r::GUI::ETextureFormat::RGBA8)
                        .set_sampler(Slic3r::GUI::ESamplerType::Sampler2D)
                        .set_pixel_data_format(Slic3r::GUI::EPixelFormat::RGBA)
                        .set_pixel_data_type(Slic3r::GUI::EPixelDataType::Float)
                        .set_mag_filter(ESamplerFilterMode::Nearset)
                        .set_min_filter(ESamplerFilterMode::Nearset)
                        .build();

                    std::vector<float> temp_data;
                    temp_data.resize(width * height * 4);
                    std::memset(temp_data.data(), 0, temp_data.size() * sizeof(float));
                    for (size_t i = 0; i < color_count; ++i) {
                        for (int j = 0; j < Extrusion_Role_Colors[i].size(); ++j) {
                            temp_data[i * 4 + j] = Extrusion_Role_Colors[i][j];
                        }
                    }
                    std::vector<uint8_t> pixel_data;
                    pixel_data.resize(temp_data.size() * sizeof(float));
                    memcpy(pixel_data.data(), temp_data.data(), temp_data.size() * sizeof(float));
                    std::shared_ptr<Slic3r::GUI::PixelBufferDescriptor> pbd = std::make_shared<Slic3r::GUI::PixelBufferDescriptor>(std::move(pixel_data), Slic3r::GUI::EPixelFormat::RGBA, Slic3r::GUI::EPixelDataType::Float);
                    m_p_role_colors_texture->set_image(0, 0, 0, 0, width, height, 1, pbd);
                }

                if (m_p_role_colors_texture) {
                    m_p_role_colors_texture->bind(stage);
                }
            }

            void AdvancedRenderer::unbind_role_colors_texture()
            {
                if (m_p_role_colors_texture) {
                    m_p_role_colors_texture->unbind();
                }
            }

            void AdvancedRenderer::bind_option_colors_texture(uint8_t stage)
            {
                const auto color_count = Options_Colors.size();
                const uint32_t width = color_count;
                const uint32_t height = 1;
                if (!m_p_option_colors_texture) {
                    m_p_option_colors_texture = std::make_shared<GLTexture>();
                    m_p_option_colors_texture->set_width(width)
                        .set_height(height)
                        .set_internal_format(Slic3r::GUI::ETextureFormat::RGBA8)
                        .set_sampler(Slic3r::GUI::ESamplerType::Sampler2D)
                        .set_pixel_data_format(Slic3r::GUI::EPixelFormat::RGBA)
                        .set_pixel_data_type(Slic3r::GUI::EPixelDataType::Float)
                        .set_mag_filter(ESamplerFilterMode::Nearset)
                        .set_min_filter(ESamplerFilterMode::Nearset)
                        .build();

                    std::vector<float> temp_data;
                    temp_data.resize(width * height * 4);
                    std::memset(temp_data.data(), 0, temp_data.size() * sizeof(float));
                    for (size_t i = 0; i < color_count; ++i) {
                        for (int j = 0; j < Options_Colors[i].size(); ++j) {
                            temp_data[i * 4 + j] = Options_Colors[i][j];
                        }
                    }
                    std::vector<uint8_t> pixel_data;
                    pixel_data.resize(temp_data.size() * sizeof(float));
                    memcpy(pixel_data.data(), temp_data.data(), temp_data.size() * sizeof(float));
                    std::shared_ptr<Slic3r::GUI::PixelBufferDescriptor> pbd = std::make_shared<Slic3r::GUI::PixelBufferDescriptor>(std::move(pixel_data), Slic3r::GUI::EPixelFormat::RGBA, Slic3r::GUI::EPixelDataType::Float);
                    m_p_option_colors_texture->set_image(0, 0, 0, 0, width, height, 1, pbd);
                }

                if (m_p_option_colors_texture) {
                    m_p_option_colors_texture->bind(stage);
                }
            }

            void AdvancedRenderer::unbind_option_colors_texture()
            {
                if (m_p_option_colors_texture) {
                    m_p_option_colors_texture->unbind();
                }
            }

            void AdvancedRenderer::bind_tool_colors_texture(uint8_t stage)
            {
                const auto color_count = m_tools.m_tool_colors.size();
                if (!color_count) {
                    return;
                }
                const uint32_t width = color_count;
                const uint32_t height = 1;
                if (m_b_tool_colors_dirty) {
                    if (!m_p_tool_colors_texture || width != m_p_tool_colors_texture->get_width()) {
                        m_p_tool_colors_texture = std::make_shared<GLTexture>();
                        m_p_tool_colors_texture->set_width(width)
                            .set_height(height)
                            .set_internal_format(Slic3r::GUI::ETextureFormat::RGBA8)
                            .set_sampler(Slic3r::GUI::ESamplerType::Sampler2D)
                            .set_pixel_data_format(Slic3r::GUI::EPixelFormat::RGBA)
                            .set_pixel_data_type(Slic3r::GUI::EPixelDataType::Float)
                            .set_mag_filter(ESamplerFilterMode::Nearset)
                            .set_min_filter(ESamplerFilterMode::Nearset)
                            .build();
                    }

                    std::vector<float> temp_data;
                    temp_data.resize(width * height * 4);
                    std::memset(temp_data.data(), 0, temp_data.size() * sizeof(float));
                    for (size_t i = 0; i < color_count; ++i) {
                        const auto t_color = adjust_color_for_rendering(m_tools.m_tool_colors[i]);
                        for (int j = 0; j < t_color.size(); ++j) {
                            temp_data[i * 4 + j] = t_color[j];
                        }
                    }
                    std::vector<uint8_t> pixel_data;
                    pixel_data.resize(temp_data.size() * sizeof(float));
                    memcpy(pixel_data.data(), temp_data.data(), temp_data.size() * sizeof(float));
                    std::shared_ptr<Slic3r::GUI::PixelBufferDescriptor> pbd = std::make_shared<Slic3r::GUI::PixelBufferDescriptor>(std::move(pixel_data), Slic3r::GUI::EPixelFormat::RGBA, Slic3r::GUI::EPixelDataType::Float);
                    m_p_tool_colors_texture->set_image(0, 0, 0, 0, width, height, 1, pbd);
                }

                if (m_p_tool_colors_texture) {
                    m_p_tool_colors_texture->bind(stage);
                }
            }

            void AdvancedRenderer::unbind_tool_colors_texture()
            {
                if (m_p_tool_colors_texture) {
                    m_p_tool_colors_texture->unbind();
                }
            }

            void AdvancedRenderer::initialize_triangular_prism()
            {
                if (m_triangular_prism.is_initialized()) {
                    return;
                }
                GLModel::Geometry t_geo;
                t_geo.format.type = GLModel::PrimitiveType::Triangles;
                t_geo.format.vertex_layout = GLModel::Geometry::EVertexLayout::P3;
                const float width = 1.0f;
                const float height = 1.0f;
                const float length = 1.0f;
                const float half_width = 0.5f * width;
                const float half_height = 0.5f * height;
                const float half_length = 0.5f * length;
                t_geo.add_vertex(Vec3f(-half_width, -half_height, -half_length));
                t_geo.add_vertex(Vec3f(half_width, -half_height, -half_length));
                t_geo.add_vertex(Vec3f(half_width, half_height, -half_length));
                t_geo.add_vertex(Vec3f(-half_width, half_height, -half_length));

                t_geo.add_vertex(Vec3f(-half_width, -half_height, half_length));
                t_geo.add_vertex(Vec3f(half_width, -half_height, half_length));
                t_geo.add_vertex(Vec3f(half_width, half_height, half_length));
                t_geo.add_vertex(Vec3f(-half_width, half_height, half_length));

                t_geo.add_triangle(0, 2, 1);
                t_geo.add_triangle(0, 3, 2);

                t_geo.add_triangle(0, 1, 5);
                t_geo.add_triangle(0, 5, 4);

                t_geo.add_triangle(1, 2, 6);
                t_geo.add_triangle(1, 6, 5);

                t_geo.add_triangle(2, 3, 7);
                t_geo.add_triangle(2, 7, 6);

                t_geo.add_triangle(3, 0, 4);
                t_geo.add_triangle(3, 4, 7);

                t_geo.add_triangle(4, 5, 6);
                t_geo.add_triangle(4, 6, 7);

                // for left side
                t_geo.add_triangle(0, 3, 8);
                t_geo.add_triangle(8, 3, 2);
                // for right side
                t_geo.add_triangle(0, 9, 1);
                t_geo.add_triangle(1, 9, 2);
                m_triangular_prism.init_from(std::move(t_geo), true);
            }

            void AdvancedRenderer::initialize_diamond()
            {
                if (m_diamond.is_initialized()) {
                    return;
                }
                m_diamond.init_from(diamond(16));
            }

            Layer::Layer()
            {
            }

            Layer::~Layer()
            {

            }

            Layer& Layer::set_start(uint32_t sid)
            {
                m_start_sid = sid;
                return *this;
            }

            uint32_t Layer::get_start() const
            {
                return m_start_sid;
            }

            Layer& Layer::set_end(uint32_t sid)
            {
                m_end_sid = sid;
                return *this;
            }

            uint32_t Layer::get_end() const
            {
                return m_end_sid;
            }

            Layer& Layer::set_z(float z)
            {
                m_zs = z;
                return *this;
            }

            float Layer::get_z() const
            {
                return m_zs;
            }

            void Layer::init_sgments(const std::vector<size_t>& sid_to_mid, const std::vector<std::vector<size_t>>& sid_to_seamMoveIds, const GCodeProcessorResult& gcode_result)
            {
                if (!is_valid()) {
                    return;
                }

                std::vector<uint32_t> t_sid_to_index;
                std::vector<std::vector<uint32_t>> m_seam_to_index;
                m_seam_to_index.resize(m_end_sid - m_start_sid + 1);
                t_sid_to_index.resize(m_end_sid - m_start_sid + 1);
                uint32_t t_count = 0;
                bool b_has_custom_options = false;

                for (uint32_t sid = m_start_sid; sid <= m_end_sid; ++sid)
                {
                    size_t move_id = sid_to_mid[sid];
                    const auto& curr_move = gcode_result.moves[move_id];
                    if (curr_move.type == EMoveType::Pause_Print || curr_move.type == EMoveType::Custom_GCode) {
                        b_has_custom_options = true;
                    }
                    const uint32_t index = add_segment_vertex(move_id, curr_move);
                    t_sid_to_index[t_count] = index;

                    if (sid_to_seamMoveIds[sid].size()) {
                        std::vector<uint32_t> indices;
                        for (size_t j = 0; j < sid_to_seamMoveIds[sid].size(); ++j) {
                            const auto seam_mid = sid_to_seamMoveIds[sid][j];
                            const auto& curr_seam = gcode_result.moves[seam_mid];
                            const uint32_t seam_index = add_segment_vertex(seam_mid, curr_seam);
                            indices.emplace_back(seam_index);
                        }
                        m_seam_to_index[t_count] = std::move(indices);
                    }
                    ++t_count;
                }

                if (b_has_custom_options) {
                    const auto p_color_effect = std::make_shared<render::ColorEffect>();
                    p_color_effect->set_color(0.8f, 0.8f, 0.8f, 1.0f);
                    add_effect(p_color_effect);
                }

                m_segments.clear();
                m_segments.reserve(m_end_sid - m_start_sid + 1);
                t_count = 1;
                for (uint32_t sid = m_start_sid + 1; sid <= m_end_sid; ++sid) {
                    size_t prev_move_index = t_sid_to_index[t_count - 1];
                    SegmentVertex t_perv_vertex = m_segment_vertices[prev_move_index];
                    const auto& prev_move = gcode_result.moves[t_perv_vertex.m_move_id];

                    size_t curr_move_index = t_sid_to_index[t_count];
                    SegmentVertex t_curr_vertex = m_segment_vertices[curr_move_index];
                    const auto& curr_move = gcode_result.moves[t_curr_vertex.m_move_id];

                    Segment t_seg;
                    t_seg.m_first_mid = prev_move_index;
                    t_seg.m_second_mid = curr_move_index;
                    t_seg.m_type = curr_move.type;
                    t_seg.m_role = curr_move.extrusion_role;
                    t_seg.m_extruder_id = curr_move.extruder_id;
                    m_segments.emplace_back(std::move(t_seg));

                    if (sid_to_seamMoveIds[sid].size()) {
                        const std::vector<uint32_t>& indices = m_seam_to_index[t_count];
                        for (size_t i = 0; i < sid_to_seamMoveIds[sid].size(); ++i) {

                            const auto seam_mid = sid_to_seamMoveIds[sid][i];
                            const auto& curr_seam = gcode_result.moves[seam_mid];
                            Segment t_seg;
                            t_seg.m_first_mid = indices[i];
                            t_seg.m_second_mid = t_seg.m_first_mid;
                            t_seg.m_type = curr_seam.type;
                            t_seg.m_role = curr_seam.extrusion_role;
                            t_seg.m_extruder_id = curr_seam.extruder_id;
                            m_segments.emplace_back(std::move(t_seg));
                        }
                    }
                    ++t_count;
                }
            }

            bool Layer::is_valid() const
            {
                bool rt = false;
                if (m_start_sid == -1u) {
                    return false;
                }
                if (m_end_sid == -1u) {
                    return false;
                }
                if (m_start_sid > m_end_sid) {
                    return false;
                }
                if (m_zs < 1e-6f) {
                    return false;
                }
                return m_b_valid;
            }

            void Layer::set_vaild(bool is_valid)
            {
                m_b_valid = is_valid;
            }

            const std::vector<Segment>& Layer::get_segments() const
            {
                return m_segments;
            }

            const std::vector<SegmentVertex>& Layer::get_segment_vertices() const
            {
                return m_segment_vertices;
            }

            void Layer::update_visible_segment_list(const LayerManager& t_layer_manager, const std::vector<bool>& filament_visible_flags, uint32_t start_seg_index, uint32_t end_seg_index)
            {
                if (!is_valid()) {
                    return;
                }
                m_visible_segment_list.clear();

                m_b_options_segment_dirty = true;
                m_options_segment_list.clear();
                m_options_segment_count = 0;

                m_b_other_segment_dirty = true;
                m_other_segment_list.clear();
                m_other_segment_count = 0;

                uint32_t seg_count = m_segments.size();
                if (!seg_count) {
                    return;
                }
                m_visible_segment_list.reserve(seg_count);
                m_options_segment_list.reserve(seg_count);
                m_other_segment_list.reserve(seg_count);

                uint32_t prev_seg_index = -1u;

                uint32_t t_start_seg_index = start_seg_index == UINT32_MAX ? 0 : start_seg_index;
                t_start_seg_index = std::min(t_start_seg_index, seg_count - 1);

                uint32_t t_end_seg_index = end_seg_index == UINT32_MAX ? seg_count : end_seg_index;
                t_end_seg_index = std::min(t_end_seg_index, seg_count - 1);
                if (t_start_seg_index > t_end_seg_index) {
                    std::swap(t_start_seg_index, t_end_seg_index);
                }

                const auto t_view_type = t_layer_manager.get_view_type();
                for (int i_seg = t_start_seg_index; i_seg <= t_end_seg_index; ++i_seg) {
                    const auto& t_seg = m_segments[i_seg];
                    if (!t_layer_manager.is_move_type_visible(t_seg.m_type)) {
                        continue;
                    }
                    if (t_seg.m_type == EMoveType::Extrude) {
                        if (!t_layer_manager.is_extrusion_role_visible(t_seg.m_role)) {
                            continue;
                        }
                    }

                    if (t_view_type == EViewType::ColorPrint && !filament_visible_flags[t_seg.m_extruder_id]) {
                        continue;
                    }
                    m_visible_segment_list.emplace_back(i_seg);

                    switch (t_seg.m_type) {
                    case EMoveType::Tool_change:
                    case EMoveType::Color_change:
                    case EMoveType::Pause_Print:
                    case EMoveType::Custom_GCode:
                    case EMoveType::Retract:
                    case EMoveType::Unretract:
                    case EMoveType::Seam:
                    {
                        add_segment(t_seg, m_segment_vertices, m_options_segment_list, prev_seg_index);
                        break;
                    }
                    case EMoveType::Extrude:
                    case EMoveType::Travel:
                    case EMoveType::Wipe:
                    {
                        add_segment(t_seg, m_segment_vertices, m_other_segment_list, prev_seg_index, true);
                        break;
                    }
                    }
                }
            }

            const std::vector<uint32_t>& Layer::get_visible_segment_list() const
            {
                return m_visible_segment_list;
            }

            const std::vector<float>& Layer::get_options_segment_list() const
            {
                return m_options_segment_list;
            }

            const std::vector<float>& Layer::get_other_segment_list() const
            {
                return m_other_segment_list;
            }

            void Layer::update_position_data_texture()
            {
                const uint32_t width = 1;
                const uint32_t height = 1;
                if (!m_p_position_texture) {
                    m_p_position_texture = std::make_shared<GLTexture>();
                    m_p_position_texture->set_width(width)
                        .set_height(height)
                        .set_internal_format(Slic3r::GUI::ETextureFormat::RGBA32F)
                        .set_sampler(Slic3r::GUI::ESamplerType::SamplerBuffer)
                        .set_pixel_data_format(Slic3r::GUI::EPixelFormat::RGBA)
                        .set_pixel_data_type(Slic3r::GUI::EPixelDataType::Float)
                        .set_mag_filter(ESamplerFilterMode::Nearset)
                        .set_min_filter(ESamplerFilterMode::Nearset)
                        .build();
                }

                if (m_b_position_dirty) {
                    std::vector<float> t_buffer_data;
                    t_buffer_data.resize(m_position_data.size() * 4);
                    memset(t_buffer_data.data(), 0, t_buffer_data.size() * sizeof(float));
                    for (size_t i = 0; i < m_position_data.size(); ++i) {
                        const auto& t_pos_data = m_position_data[i];
                        t_buffer_data[4 * i] = t_pos_data.m_position[0];
                        t_buffer_data[4 * i + 1] = t_pos_data.m_position[1];
                        t_buffer_data[4 * i + 2] = t_pos_data.m_position[2];
                        t_buffer_data[4 * i + 3] = float(t_pos_data.m_segment_vertex_index);
                    }
                    const bool rt = m_p_position_texture->set_buffer(t_buffer_data);
                    if (rt) {
                        m_b_position_dirty = false;
                    }
                }
            }

            void Layer::bind_position_data_texture(uint8_t stage) const
            {
                if (m_p_position_texture) {
                    m_p_position_texture->bind(stage);
                }
            }

            void Layer::update_width_height_data_texture(const GCodeProcessorResult& gcode_result)
            {
                const uint32_t width = 1;
                const uint32_t height = 1;
                if (!m_p_width_height_data_texture) {
                    m_p_width_height_data_texture = std::make_shared<GLTexture>();
                    m_p_width_height_data_texture->set_width(width)
                        .set_height(height)
                        .set_internal_format(Slic3r::GUI::ETextureFormat::RG32F)
                        .set_sampler(Slic3r::GUI::ESamplerType::SamplerBuffer)
                        .set_pixel_data_format(Slic3r::GUI::EPixelFormat::RG)
                        .set_pixel_data_type(Slic3r::GUI::EPixelDataType::Float)
                        .set_mag_filter(ESamplerFilterMode::Nearset)
                        .set_min_filter(ESamplerFilterMode::Nearset)
                        .build();
                }

                if (m_b_width_height_data_dirty) {
                    std::vector<float> t_segment_width_height;
                    for (auto iter = m_segment_vertices.begin(); iter != m_segment_vertices.end(); ++iter)
                    {
                        const auto& t_move = gcode_result.moves[iter->m_move_id];
                        float width = t_move.width;
                        float height = t_move.height;
                        if (t_move.type == EMoveType::Travel) {
                            width = 0.1f;
                            height = 0.1f;
                        }
                        t_segment_width_height.emplace_back(float(width));
                        t_segment_width_height.emplace_back(float(height));
                    }
                    const auto& rt = m_p_width_height_data_texture->set_buffer(t_segment_width_height);
                    if (rt) {
                        m_b_width_height_data_dirty = false;
                    }
                }
            }

            void Layer::bind_width_height_data_texture(uint8_t stage) const
            {
                if (m_p_width_height_data_texture) {
                    m_p_width_height_data_texture->bind(stage);
                }
            }

            void Layer::update_per_move_data(EViewType t_view_type, const GCodeProcessorResult& gcode_result)
            {
                m_per_move_data_list.clear();
                m_per_move_data_list.reserve(4 * m_segment_vertices.size());
                for (auto iter = m_segment_vertices.begin(); iter != m_segment_vertices.end(); ++iter)
                {
                    const auto& t_move = gcode_result.moves[iter->m_move_id];
                    m_per_move_data_list.emplace_back(float(t_move.type));
                    const float t_move_range_data = get_move_data_from_view_type(t_view_type, t_move);
                    m_per_move_data_list.emplace_back(t_move_range_data);
                    m_per_move_data_list.emplace_back(t_move.delta_extruder);
                    m_per_move_data_list.emplace_back(0.0f);
                }
                m_b_per_move_data_dirty = true;
            }

            void Layer::update_per_move_data_texture()
            {
                const uint32_t width = 1;
                const uint32_t height = 1;
                if (!m_p_per_move_data_texture) {
                    m_p_per_move_data_texture = std::make_shared<GLTexture>();
                    m_p_per_move_data_texture->set_width(width)
                        .set_height(height)
                        .set_internal_format(Slic3r::GUI::ETextureFormat::RGBA32F)
                        .set_sampler(Slic3r::GUI::ESamplerType::SamplerBuffer)
                        .set_pixel_data_format(Slic3r::GUI::EPixelFormat::RGBA)
                        .set_pixel_data_type(Slic3r::GUI::EPixelDataType::Float)
                        .set_mag_filter(ESamplerFilterMode::Nearset)
                        .set_min_filter(ESamplerFilterMode::Nearset)
                        .build();
                }

                if (m_b_per_move_data_dirty) {
                    const auto& rt = m_p_per_move_data_texture->set_buffer(m_per_move_data_list);
                    if (rt) {
                        m_per_move_data_list.clear();
                        m_b_per_move_data_dirty = false;
                    }
                }
            }

            void Layer::bind_per_move_data_texture(uint8_t stage) const
            {
                if (m_p_per_move_data_texture) {
                    m_p_per_move_data_texture->bind(stage);
                }
            }

            void Layer::update_other_segment_texture()
            {
                const uint32_t width = 1;
                const uint32_t height = 1;
                if (!m_p_other_segment_texture) {
                    m_p_other_segment_texture = std::make_shared<GLTexture>();
                    m_p_other_segment_texture->set_width(width)
                        .set_height(height)
                        .set_internal_format(Slic3r::GUI::ETextureFormat::RGBA32F)
                        .set_sampler(Slic3r::GUI::ESamplerType::SamplerBuffer)
                        .set_pixel_data_format(Slic3r::GUI::EPixelFormat::RGBA)
                        .set_pixel_data_type(Slic3r::GUI::EPixelDataType::Float)
                        .set_mag_filter(ESamplerFilterMode::Nearset)
                        .set_min_filter(ESamplerFilterMode::Nearset)
                        .build();
                }

                if (m_p_other_segment_texture) {
                    if (m_b_other_segment_dirty) {
                        const auto& rt = m_p_other_segment_texture->set_buffer(m_other_segment_list);
                        if (rt) {
                            m_other_segment_count = m_other_segment_list.size() / 4;
                            m_b_other_segment_dirty = false;
                            m_other_segment_list.clear();
                        }
                        else {
                            m_other_segment_count = 0;
                        }
                    }
                }
            }

            uint32_t Layer::get_other_segment_count() const
            {
                return m_other_segment_count;
            }

            void Layer::bind_other_segment_texture(uint8_t stage) const
            {
                if (m_p_other_segment_texture) {
                    m_p_other_segment_texture->bind(stage);
                }
            }

            void Layer::update_options_segment_texture()
            {
                const uint32_t width = 1;
                const uint32_t height = 1;
                if (!m_p_options_segment_texture) {
                    m_p_options_segment_texture = std::make_shared<GLTexture>();
                    m_p_options_segment_texture->set_width(width)
                        .set_height(height)
                        .set_internal_format(Slic3r::GUI::ETextureFormat::RGBA32F)
                        .set_sampler(Slic3r::GUI::ESamplerType::SamplerBuffer)
                        .set_pixel_data_format(Slic3r::GUI::EPixelFormat::RGBA)
                        .set_pixel_data_type(Slic3r::GUI::EPixelDataType::Float)
                        .set_mag_filter(ESamplerFilterMode::Nearset)
                        .set_min_filter(ESamplerFilterMode::Nearset)
                        .build();
                }

                if (m_p_options_segment_texture) {

                    if (m_b_options_segment_dirty) {
                        const auto& rt = m_p_options_segment_texture->set_buffer(m_options_segment_list);
                        if (rt) {
                            m_options_segment_count = m_options_segment_list.size() / 4;
                            m_b_options_segment_dirty = false;
                            m_options_segment_list.clear();
                        }
                        else {
                            m_options_segment_count = 0;
                        }
                    }
                }
            }

            uint32_t Layer::get_options_segment_count() const
            {
                return m_options_segment_count;
            }

            void Layer::bind_options_segment_texture(uint8_t stage) const
            {
                if (m_p_options_segment_texture) {
                    m_p_options_segment_texture->bind(stage);
                }
            }

            uint32_t Layer::get_current_move_id(uint32_t seg_index) const
            {
                const auto& t_segments = get_segments();
                const auto& t_visibile_segments = get_visible_segment_list();
                if (seg_index >= 1) {
                    const auto& t_seg = t_segments[t_visibile_segments[seg_index - 1]];
                    return m_segment_vertices[t_seg.m_second_mid].m_move_id;
                }
                else
                {
                    const auto& t_seg = t_segments[t_visibile_segments[0]];
                    return m_segment_vertices[t_seg.m_first_mid].m_move_id;
                }
            }

            const std::vector<PositionData>& Layer::get_position_data() const
            {
                return m_position_data;
            }

            uint32_t Layer::add_segment_vertex(uint32_t move_id, const GCodeProcessorResult::MoveVertex& t_move)
            {
                uint32_t t_index = m_segment_vertices.size();
                SegmentVertex t_seg_vertex;
                t_seg_vertex.m_move_id = move_id;
                t_seg_vertex.m_indices.clear();
                t_seg_vertex.m_indices.reserve(10);
                float hight_offset = 0.0f;
                if (t_move.type == EMoveType::Wipe) {
                    hight_offset = 0.5f * GCodeProcessor::Wipe_Height;
                }
                uint32_t pos_index = m_position_data.size();
                if (t_move.is_arc_move_with_interpolation_points()) {
                    const size_t loop_num = t_move.interpolation_points.size();
                    for (size_t i = 0; i < loop_num; ++i) {
                        PositionData t_pos_data;
                        t_pos_data.m_position = t_move.interpolation_points[i];
                        t_pos_data.m_position.z() += hight_offset;
                        t_pos_data.m_segment_vertex_index = t_index;
                        m_position_data.emplace_back(std::move(t_pos_data));
                        t_seg_vertex.m_indices.push_back(pos_index);
                        ++pos_index;
                    }
                }

                PositionData t_pos_data;
                t_pos_data.m_position = t_move.position;
                t_pos_data.m_position.z() += hight_offset;
                t_pos_data.m_segment_vertex_index = t_index;
                m_position_data.emplace_back(std::move(t_pos_data));
                t_seg_vertex.m_indices.push_back(pos_index);
                m_segment_vertices.emplace_back(std::move(t_seg_vertex));

                return t_index;
            }

            LayerManager::LayerManager()
            {
                for (unsigned int i = 0; i < erCount; ++i) {
                    m_extrusion_role_visible_flag |= 1 << i;
                }
            }

            LayerManager::~LayerManager()
            {
            }

            void LayerManager::add_layer(const Layer& t_layer)
            {
                m_layer_list.emplace_back(t_layer);
            }

            void LayerManager::add_layer(Layer&& t_layer)
            {
                m_layer_list.emplace_back(std::move(t_layer));
            }

            void LayerManager::clear()
            {
                m_layer_list.clear();
            }

            bool LayerManager::empty() const
            {
                return m_layer_list.empty();
            }

            const Layer& LayerManager::get_layer(size_t index) const
            {
                if (index >= m_layer_list.size()) {
                    static Layer s_empty;
                    return s_empty;
                }
                return m_layer_list[index];
            }

            Layer& LayerManager::get_layer(size_t index)
            {
                if (index >= m_layer_list.size()) {
                    static Layer s_empty;
                    return s_empty;
                }
                return m_layer_list[index];
            }

            size_t LayerManager::size() const
            {
                return m_layer_list.size();
            }

            Layer& LayerManager::operator[](size_t index)
            {
                return m_layer_list[index];
            }

            const Layer& LayerManager::operator[](size_t index) const
            {
                return m_layer_list[index];
            }

            void LayerManager::reset()
            {
                m_b_move_dirty = true;
                m_b_layer_dirty = true;
                m_b_segment_visibility_dirty = true;

                m_layer_list.clear();

                m_current_layer_range.first = 0;
                m_current_layer_range.second = 0;

                m_current_move_range.first = 0;
                m_current_move_range.second = 0;

                m_other_segment_list.clear();
                m_other_segment_count = 0;
                m_b_other_segment_dirty = true;

                m_options_segment_list.clear();
                m_options_segment_count = 0;
                m_b_options_segment_dirty = true;

                m_b_view_type_dirty = true;
            }

            void LayerManager::set_current_layer_start(uint32_t layer_index)
            {
                if (layer_index >= size()) {
                    return;
                }
                if (layer_index != m_current_layer_range.first) {
                    m_current_layer_range.first = layer_index;
                    mark_layer_dirty();
                }
            }

            uint32_t LayerManager::get_current_layer_start() const
            {
                return m_current_layer_range.first;
            }

            void LayerManager::set_current_layer_end(uint32_t layer_index)
            {
                if (layer_index >= size()) {
                    return;
                }
                if (layer_index != m_current_layer_range.second) {
                    m_current_layer_range.second = layer_index;
                    mark_layer_dirty();
                }
            }

            uint32_t LayerManager::get_current_layer_end() const
            {
                return m_current_layer_range.second;
            }

            void LayerManager::conform_layer_range_valid()
            {
                if (m_current_layer_range.first > m_current_layer_range.second) {
                    std::swap(m_current_layer_range.first, m_current_layer_range.second);
                }
            }

            void LayerManager::set_current_move_start(uint32_t start)
            {
                const auto& current_layer_end = get_current_layer_end();
                const auto& current_top_layer = get_layer(current_layer_end);
                if (!current_top_layer.is_valid()) {
                    return;
                }
                const auto seg_count = current_top_layer.get_segments().size();
                if (seg_count <= 0) {
                    return;
                }
                const int t_start_count = 0;
                const int t_end_count = seg_count;

                if (start > t_end_count) {
                    return;
                }
                if (start > m_current_move_range.second) {
                    return;
                }

                if (m_current_move_range.first != start) {
                    m_current_move_range.first = start;
                    mark_move_dirty();
                }
            }

            uint32_t LayerManager::get_current_move_start() const
            {
                return m_current_move_range.first;
            }

            void LayerManager::set_current_move_end(uint32_t end)
            {
                const auto& current_layer_end = get_current_layer_end();
                const auto& current_top_layer = get_layer(current_layer_end);
                if (!current_top_layer.is_valid()) {
                    return;
                }
                const auto seg_count = current_top_layer.get_segments().size();
                if (seg_count <= 0) {
                    return;
                }
                const int t_start_count = 1;
                const int t_end_count = seg_count;
                if (end > t_end_count) {
                    return;
                }
                if (end < m_current_move_range.first) {
                    return;
                }

                if (m_current_move_range.second != end) {
                    m_current_move_range.second = end;
                    mark_move_dirty();
                }
            }

            uint32_t LayerManager::get_current_move_end() const
            {
                return m_current_move_range.second;
            }

            uint32_t LayerManager::get_current_move_id() const
            {
                const auto& t_current_layer = get_layer(m_current_layer_range.second);
                return t_current_layer.get_current_move_id(m_current_move_range.second);
            }

            bool LayerManager::is_move_type_visible(EMoveType type) const
            {
                return m_move_type_visible_flag & (1 << static_cast<int>(type));
            }

            void LayerManager::set_move_type_visible(EMoveType type, bool visible)
            {
                if (is_move_type_visible(type) == visible) {
                    return;
                }
                if (visible) {
                    m_move_type_visible_flag |= (1 << static_cast<int>(type));
                }
                else {
                    m_move_type_visible_flag &= ~(1 << static_cast<int>(type));
                }
                mark_visibility_dirty();
            }

            bool LayerManager::is_extrusion_role_visible(ExtrusionRole role) const
            {
                return role < erCount && m_extrusion_role_visible_flag & (1 << static_cast<int>(role));
            }

            void LayerManager::set_extrusion_role_visible(ExtrusionRole role, bool visible)
            {
                if (is_extrusion_role_visible(role) == visible) {
                    return;
                }
                if (visible) {
                    m_extrusion_role_visible_flag |= (1 << static_cast<int>(role));
                }
                else {
                    m_extrusion_role_visible_flag &= ~(1 << static_cast<int>(role));
                }
                mark_visibility_dirty();
            }

            uint32_t LayerManager::get_extrusion_role_visibility_flags() const
            {
                return m_extrusion_role_visible_flag;
            }

            void LayerManager::set_extrusion_role_visibility_flags(uint32_t flag)
            {
                if (m_extrusion_role_visible_flag != flag) {
                    m_extrusion_role_visible_flag = flag;
                    mark_visibility_dirty();
                }
            }

            bool LayerManager::update_visibile_segment_list(bool b_force_update, const std::vector<bool>& filament_visible_flags)
            {
                if (is_visibility_dirty() || b_force_update) {
                    for (size_t i_layer = 0; i_layer < m_layer_list.size(); ++i_layer) {
                        m_layer_list[i_layer].update_visible_segment_list(*this, filament_visible_flags);
                    }
                    clear_visibility_dirty();
                    return true;
                }
                return false;
            }

            void LayerManager::update_transient_segment_list(bool b_force_update)
            {
                if (!is_move_dirty() && !b_force_update) {
                    return;
                }

                m_other_segment_count = 0;
                m_other_segment_list.clear();
                m_b_other_segment_dirty = true;

                m_options_segment_count = 0;
                m_options_segment_list.clear();
                m_b_options_segment_dirty = true;

                conform_layer_range_valid();

                const auto& t_top_layer = m_layer_list[m_current_layer_range.second];
                const auto& t_segments = t_top_layer.get_segments();
                if (!t_segments.size()) {
                    clear_move_dirty();
                    return;
                }
                const auto& t_segments_indices = t_top_layer.get_visible_segment_list();

                if (!t_segments_indices.size()) {
                    clear_move_dirty();
                    return;
                }

                const auto& t_segment_vertices = t_top_layer.get_segment_vertices();
                size_t t_start_seg_count = m_current_move_range.first;
                size_t t_end_seg_count = m_current_move_range.second;
                uint32_t prev_seg_index = -1u;

                m_options_segment_list.reserve(1000);
                m_other_segment_list.reserve(1000);
                for (size_t j_seg = t_start_seg_count; j_seg <= t_end_seg_count; ++j_seg) {
                    if (j_seg < 1) {
                        continue;
                    }
                    const auto& t_seg = t_segments[t_segments_indices[j_seg - 1]];
                
                    switch (t_seg.m_type) {
                    case EMoveType::Tool_change:
                    case EMoveType::Color_change:
                    case EMoveType::Pause_Print:
                    case EMoveType::Custom_GCode:
                    case EMoveType::Retract:
                    case EMoveType::Unretract:
                    case EMoveType::Seam:
                    {
                        add_segment(t_seg, t_segment_vertices, m_options_segment_list, prev_seg_index);
                        break;
                    }
                    case EMoveType::Extrude:
                    case EMoveType::Travel:
                    case EMoveType::Wipe:
                    {
                        add_segment(t_seg, t_segment_vertices, m_other_segment_list, prev_seg_index, true);
                        break;
                    }
                    }
                }

                clear_move_dirty();
            }

            void LayerManager::update_per_move_data(EViewType t_view_type, const GCodeProcessorResult& t_gcode_result)
            {
                if (!is_view_type_dirty())
                {
                    return;
                }

                for (size_t i_layer = 0; i_layer < m_layer_list.size(); ++i_layer) {
                    m_layer_list[i_layer].update_per_move_data(t_view_type, t_gcode_result);
                }

                clear_view_type_dirty();
            }

            void LayerManager::update_transient_options_texture()
            {
                const uint32_t width = 1;
                const uint32_t height = 1;
                if (!m_p_options_segment_texture) {
                    m_p_options_segment_texture = std::make_shared<GLTexture>();
                    m_p_options_segment_texture->set_width(width)
                        .set_height(height)
                        .set_internal_format(Slic3r::GUI::ETextureFormat::RGBA32F)
                        .set_sampler(Slic3r::GUI::ESamplerType::SamplerBuffer)
                        .set_pixel_data_format(Slic3r::GUI::EPixelFormat::RGBA)
                        .set_pixel_data_type(Slic3r::GUI::EPixelDataType::Float)
                        .set_mag_filter(ESamplerFilterMode::Nearset)
                        .set_min_filter(ESamplerFilterMode::Nearset)
                        .build();
                }

                if (m_p_options_segment_texture) {

                    if (m_b_options_segment_dirty) {
                        const auto& rt = m_p_options_segment_texture->set_buffer(m_options_segment_list);
                        if (rt) {
                            m_options_segment_count = m_options_segment_list.size() / 4;
                            m_b_options_segment_dirty = false;
                            m_options_segment_list.clear();
                        }
                        else {
                            m_options_segment_count = 0;
                        }
                    }
                }
            }

            uint32_t LayerManager::get_transient_options_segment_count() const
            {
                return m_options_segment_count;
            }

            void LayerManager::bind_transient_options_texture(uint8_t stage) const
            {
                if (m_p_options_segment_texture) {
                    m_p_options_segment_texture->bind(stage);
                }
            }

            void LayerManager::update_transient_other_texture()
            {
                const uint32_t width = 1;
                const uint32_t height = 1;
                if (!m_p_other_segment_texture) {
                    m_p_other_segment_texture = std::make_shared<GLTexture>();
                    m_p_other_segment_texture->set_width(width)
                        .set_height(height)
                        .set_internal_format(Slic3r::GUI::ETextureFormat::RGBA32F)
                        .set_sampler(Slic3r::GUI::ESamplerType::SamplerBuffer)
                        .set_pixel_data_format(Slic3r::GUI::EPixelFormat::RGBA)
                        .set_pixel_data_type(Slic3r::GUI::EPixelDataType::Float)
                        .set_mag_filter(ESamplerFilterMode::Nearset)
                        .set_min_filter(ESamplerFilterMode::Nearset)
                        .build();
                }

                if (m_p_other_segment_texture) {
                    if (m_b_other_segment_dirty) {
                        const auto& rt = m_p_other_segment_texture->set_buffer(m_other_segment_list);
                        if (rt) {
                            m_other_segment_count = m_other_segment_list.size() / 4;
                            m_b_other_segment_dirty = false;
                            m_other_segment_list.clear();
                        }
                        else {
                            m_other_segment_count = 0;
                        }
                    }
                }
            }

            uint32_t LayerManager::get_transient_other_segment_count() const
            {
                return m_other_segment_count;
            }

            void LayerManager::bind_transient_other_texture(uint8_t stage) const
            {
                if (m_p_other_segment_texture) {
                    m_p_other_segment_texture->bind(stage);
                }
            }

            const std::vector<float>& LayerManager::get_transient_other_segment_list() const
            {
                return m_other_segment_list;
            }

            void LayerManager::mark_move_dirty()
            {
                m_b_move_dirty = true;
            }

            void LayerManager::clear_move_dirty()
            {
                m_b_move_dirty = false;
            }

            void LayerManager::mark_layer_dirty()
            {
                m_b_layer_dirty = true;
            }

            void LayerManager::mark_visibility_dirty()
            {
                m_b_segment_visibility_dirty = true;
            }

            bool LayerManager::is_layer_dirty()
            {
                return m_b_layer_dirty;
            }

            void LayerManager::clear_layer_dirty()
            {
                m_b_layer_dirty = false;
            }

            bool LayerManager::is_visibility_dirty()
            {
                return m_b_segment_visibility_dirty;
            }

            void LayerManager::clear_visibility_dirty()
            {
                m_b_segment_visibility_dirty = false;
            }

            void LayerManager::on_filament_visible_changed()
            {
                mark_visibility_dirty();
            }

            void LayerManager::set_view_type(EViewType type)
            {
                if (m_view_type == type)
                {
                    return;
                }
                m_view_type = type;
                mark_view_type_diry();
            }

            EViewType LayerManager::get_view_type() const
            {
                return m_view_type;
            }

            bool LayerManager::is_move_dirty() const
            {
                return m_b_move_dirty;
            }

            void LayerManager::mark_view_type_diry()
            {
                m_b_view_type_dirty = true;
            }

            void LayerManager::clear_view_type_dirty()
            {
                m_b_view_type_dirty = false;
            }

            bool LayerManager::is_view_type_dirty() const
            {
                return m_b_view_type_dirty;
            }
        }

        namespace render
        {
            BasicEffect::BasicEffect()
            {
            }

            BasicEffect::~BasicEffect()
            {
            }

            void BasicEffect::update_uniform(const std::shared_ptr<GLShaderProgram>& p_shader)
            {
            }

            ColorEffect::ColorEffect()
            {
            }

            ColorEffect::~ColorEffect()
            {
            }

            void ColorEffect::set_color(float r, float g, float b, float a)
            {
                m_color[0] = r;
                m_color[1] = g;
                m_color[2] = b;
                m_color[3] = a;
            }

            void ColorEffect::set_texture()
            {
            }

            const std::shared_ptr<GLShaderProgram>& ColorEffect::get_shader() const
            {
                const auto& p_shader = wxGetApp().get_shader("gcode_custom_effect");
                return p_shader;
            }

            void ColorEffect::update_uniform(const std::shared_ptr<GLShaderProgram>& p_shader)
            {
                if (!p_shader) {
                    return;
                }
                p_shader->set_uniform("u_base_color", Vec4f(m_color[0], m_color[1], m_color[2], m_color[3]));
            }

            EffectContainer::EffectContainer()
            {
            }

            EffectContainer::~EffectContainer()
            {
            }

            void EffectContainer::add_effect(const std::shared_ptr<BasicEffect>& p_effect)
            {
                m_effect_list.emplace_back(p_effect);
            }

            void EffectContainer::remove_effect(const std::shared_ptr<BasicEffect>& p_effect)
            {
                m_effect_list.erase(std::find(m_effect_list.begin(), m_effect_list.end(), p_effect));
            }

            void EffectContainer::clear_effect()
            {
                m_effect_list.clear();
            }

            const std::vector<std::shared_ptr<BasicEffect>>& EffectContainer::get_effects() const
            {
                return m_effect_list;
            }
        }
    }
}
