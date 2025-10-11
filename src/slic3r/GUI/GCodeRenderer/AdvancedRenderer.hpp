#pragma once
#include "slic3r/GUI/GCodeRenderer/BaseRenderer.hpp"
#include "slic3r/GUI/GLModel.hpp"
#include <memory>
#include <vector>
#include <unordered_map>
namespace Slic3r {
    class GLShaderProgram;
    namespace GUI {
        class GLTexture;

        namespace render
        {
            class BasicEffect
            {
            public:
                explicit BasicEffect();
                ~BasicEffect();

                virtual const std::shared_ptr<GLShaderProgram>& get_shader() const = 0;
                virtual void update_uniform(const std::shared_ptr<GLShaderProgram>& p_shader);
            };

            class ColorEffect : public BasicEffect
            {
            public:
                explicit ColorEffect();
                ~ColorEffect();

                void set_color(float r, float g, float b, float a);

                void set_texture();

                const std::shared_ptr<GLShaderProgram>& get_shader() const override;

                void update_uniform(const std::shared_ptr<GLShaderProgram>& p_shader) override;
            public:
                float m_color[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
            };

            class EffectContainer
            {
            public:
                explicit EffectContainer();
                virtual ~EffectContainer();

                void add_effect(const std::shared_ptr<BasicEffect>& p_effect);
                void remove_effect(const std::shared_ptr<BasicEffect>& p_effect);
                void clear_effect();
                const std::vector<std::shared_ptr<BasicEffect>>& get_effects() const;

            private:
                std::vector<std::shared_ptr<BasicEffect>> m_effect_list;
            };
        }

        namespace gcode
        {
            class LayerManager;
            class AdvancedRenderer : public BaseRenderer
            {
            public:
                explicit AdvancedRenderer();
                ~AdvancedRenderer();

                void init(ConfigOptionMode mode, Slic3r::PresetBundle* preset_bundle) override;
                void update_sequential_view_current(unsigned int first, unsigned int last);
                void render_calibration_thumbnail(ThumbnailData& thumbnail_data, unsigned int w, unsigned int h, const ThumbnailsParams& thumbnail_params, PartPlateList& partplate_list, OpenGLManager& opengl_manager);
                std::vector<double> get_layers_zs() const;
                unsigned int get_options_visibility_flags() const;
                void set_options_visibility_from_flags(unsigned int flags);
                unsigned int get_toolpath_role_visibility_flags() const;
                void refresh(const GCodeProcessorResult& gcode_result, const std::vector<std::string>& str_tool_colors) override;
                void refresh_render_paths();
                bool can_export_toolpaths() const;
                void export_toolpaths_to_obj(const char* filename) const;
                void set_layers_z_range(const std::array<unsigned int, 2>& layers_z_range);
                void render(int canvas_width, int canvas_height, int right_margin);
                void reset() override;
                bool is_move_type_visible(EMoveType type) const override;
                void set_move_type_visible(EMoveType type, bool visible) override;
                bool is_extrusion_role_visible(ExtrusionRole role) const override;
                void set_extrusion_role_visible(ExtrusionRole role, bool visible) override;
                uint32_t get_extrusion_role_visibility_flags() const override;
                void set_extrusion_role_visibility_flags(uint32_t flags) override;
                void update_marker_curr_move() override;
            protected:
                bool load_toolpaths(const GCodeProcessorResult& gcode_result, const BuildVolume& build_volume, const std::vector<BoundingBoxf3>& exclude_bounding_box) override;
                void update_moves_slider(bool set_to_max = false) override;
                bool show_sequential_view() const override;
                void on_visibility_changed() override;

            private:
                void load_layer_info(const GCodeProcessorResult& gcode_result, const BuildVolume& build_volume, const std::vector<BoundingBoxf3>& exclude_bounding_box);
                const std::shared_ptr<LayerManager>& get_layer_manager() const;
                void render_toolpaths();
                void do_render_others(const std::vector<uint32_t>& layer_index_list, bool top_layer_only);
                void do_render_options(const std::vector<uint32_t>& layer_index_list, bool top_layer_only);
                void do_render_transparency(const std::vector<uint32_t>& layer_index_list);
                gcode::Range get_range_according_to_view_type(gcode::EViewType type) const;

                void bind_color_range_texture(uint8_t stage);
                void unbind_color_range_texture();

                void bind_thermal_index_range_colors_texture(uint8_t stage);
                void unbind_thermal_index_range_colors_texture();

                void bind_role_colors_texture(uint8_t stage);
                void unbind_role_colors_texture();

                void bind_option_colors_texture(uint8_t stage);
                void unbind_option_colors_texture();

                void bind_tool_colors_texture(uint8_t stage);
                void unbind_tool_colors_texture();

                void initialize_triangular_prism();
                void initialize_diamond();

            private:
                mutable std::shared_ptr<LayerManager> m_p_layer_manager{ nullptr };
                GLModel m_triangular_prism;
                GLModel m_diamond;

                std::shared_ptr<GLTexture> m_p_color_range_texture{ nullptr };
                std::shared_ptr<GLTexture> m_p_thermal_index_range_colors_texture{ nullptr };
                std::shared_ptr<GLTexture> m_p_role_colors_texture{ nullptr };
                std::shared_ptr<GLTexture> m_p_option_colors_texture{ nullptr };
                std::shared_ptr<GLTexture> m_p_tool_colors_texture{ nullptr };
                bool m_b_tool_colors_dirty{ true };

                bool m_b_loading{false};
            };

            struct SegmentVertex
            {
                uint32_t m_move_id{ -1u };
                std::vector<uint32_t> m_indices;
            };

            struct Segment
            {
                uint32_t m_first_mid{ -1u };
                uint32_t m_second_mid{ -1u };
                EMoveType m_type{ EMoveType::Count };
                ExtrusionRole m_role{ ExtrusionRole::erCount };
                uint16_t m_extruder_id{ UINT16_MAX };
            };

            struct PositionData
            {
                Vec3f m_position;
                uint32_t m_segment_vertex_index{ 0 };
            };

            class Layer: public render::EffectContainer
            {
            public:
                explicit Layer();
                ~Layer();

                Layer& set_start(uint32_t sid);
                uint32_t get_start() const;

                Layer& set_end(uint32_t sid);
                uint32_t get_end() const;

                Layer& set_z(float z);
                float get_z() const;

                void init_sgments(const std::vector<size_t>& sid_to_mid, const std::vector<std::vector<size_t>>& sid_to_seamMoveId, const GCodeProcessorResult& gcode_result);

                bool is_valid() const;
                void set_vaild(bool is_valid);

                const std::vector<Segment>& get_segments() const;
                const std::vector<SegmentVertex>& get_segment_vertices() const;

                void update_visible_segment_list(const LayerManager& t_layer_manager, const std::vector<bool>& filament_visible_flags, uint32_t start_seg_index = UINT32_MAX, uint32_t end_seg_index = UINT32_MAX);
                const std::vector<uint32_t>& get_visible_segment_list() const;

                const std::vector<float>& get_options_segment_list() const;
                const std::vector<float>& get_other_segment_list() const;

                void update_position_data_texture();
                void bind_position_data_texture(uint8_t stage) const;

                void update_width_height_data_texture(const GCodeProcessorResult& gcode_result);
                void bind_width_height_data_texture(uint8_t stage) const;

                void update_per_move_data(EViewType t_view_type, const GCodeProcessorResult& gcode_result);
                void update_per_move_data_texture();
                void bind_per_move_data_texture(uint8_t stage) const;

                void update_other_segment_texture();
                uint32_t get_other_segment_count() const;
                void bind_other_segment_texture(uint8_t stage) const;

                void update_options_segment_texture();
                uint32_t get_options_segment_count() const;
                void bind_options_segment_texture(uint8_t stage) const;

                uint32_t get_current_move_id(uint32_t seg_index) const;

                const std::vector<PositionData>& get_position_data() const;

            private:
                uint32_t add_segment_vertex(uint32_t move_id, const GCodeProcessorResult::MoveVertex& t_move);

            private:
                bool m_b_valid{ true };
                uint32_t m_start_sid{ -1u };
                uint32_t m_end_sid{ -1u };
                float m_zs{ 0.0f };
                std::vector<Segment> m_segments;
                std::vector<uint32_t> m_visible_segment_list;
                mutable std::vector<float> m_options_segment_list;
                mutable std::vector<float> m_other_segment_list;

                std::vector<SegmentVertex> m_segment_vertices;

                std::vector<PositionData> m_position_data;
                mutable bool m_b_position_dirty{ true };
                mutable std::shared_ptr<GLTexture> m_p_position_texture{ nullptr };

                mutable bool m_b_width_height_data_dirty{ true };
                mutable std::shared_ptr<GLTexture> m_p_width_height_data_texture{ nullptr };

                mutable bool m_b_per_move_data_dirty{ true };
                mutable std::vector<float> m_per_move_data_list;
                mutable std::shared_ptr<GLTexture> m_p_per_move_data_texture{ nullptr };

                mutable bool m_b_other_segment_dirty{ false };
                mutable std::shared_ptr<GLTexture> m_p_other_segment_texture{ nullptr };
                mutable uint32_t m_other_segment_count{ 0 };

                mutable bool m_b_options_segment_dirty{ false };
                mutable std::shared_ptr<GLTexture> m_p_options_segment_texture{ nullptr };
                mutable uint32_t m_options_segment_count{ 0 };
            };

            class LayerManager
            {
            public:
                explicit LayerManager();
                ~LayerManager();

                void add_layer(const Layer& t_layer);
                void add_layer(Layer&& t_layer);

                void clear();

                bool empty() const;

                const Layer& get_layer(size_t index) const;
                Layer& get_layer(size_t index);

                size_t size() const;

                Layer& operator[](size_t index);

                const Layer& operator[](size_t index) const;

                void reset();

                void set_current_layer_start(uint32_t);
                uint32_t get_current_layer_start() const;

                void set_current_layer_end(uint32_t);
                uint32_t get_current_layer_end() const;

                void conform_layer_range_valid();

                void set_current_move_start(uint32_t);
                uint32_t get_current_move_start() const;

                void set_current_move_end(uint32_t);
                uint32_t get_current_move_end() const;
                uint32_t get_current_move_id() const;

                bool is_move_type_visible(EMoveType type) const;
                void set_move_type_visible(EMoveType type, bool visible);

                bool is_extrusion_role_visible(ExtrusionRole role) const;
                void set_extrusion_role_visible(ExtrusionRole role, bool visible);

                uint32_t get_extrusion_role_visibility_flags() const;
                void set_extrusion_role_visibility_flags(uint32_t flag);

                bool update_visibile_segment_list(bool b_force_update, const std::vector<bool>& filament_visible_flags);

                void update_transient_segment_list(bool b_force_update);

                void update_per_move_data(EViewType t_view_type, const GCodeProcessorResult& t_gcode_result);

                void update_transient_options_texture();
                uint32_t get_transient_options_segment_count() const;
                void bind_transient_options_texture(uint8_t stage) const;

                void update_transient_other_texture();
                uint32_t get_transient_other_segment_count() const;
                void bind_transient_other_texture(uint8_t stage) const;
                const std::vector<float>& get_transient_other_segment_list() const;

                void mark_move_dirty();
                bool is_move_dirty() const;
                void clear_move_dirty();

                bool is_layer_dirty();
                void clear_layer_dirty();

                bool is_visibility_dirty();
                void clear_visibility_dirty();

                void on_filament_visible_changed();

                void set_view_type(EViewType type);

                EViewType get_view_type() const;
            private:
                void mark_layer_dirty();
                void mark_visibility_dirty();

                void mark_view_type_diry();
                void clear_view_type_dirty();
                bool is_view_type_dirty() const;

            private:
                std::vector<Layer> m_layer_list;

                std::pair<uint32_t, uint32_t> m_current_layer_range;
                std::pair<uint32_t, uint32_t> m_current_move_range;

                bool m_b_layer_dirty{ true };
                bool m_b_move_dirty{ true };
                uint16_t m_move_type_visible_flag{ 0 };
                uint32_t m_extrusion_role_visible_flag{ 0 };
                bool m_b_segment_visibility_dirty{ true };

                // for the top layer draw
                mutable std::vector<float> m_other_segment_list;
                mutable bool m_b_other_segment_dirty{ true };
                mutable std::shared_ptr<GLTexture> m_p_other_segment_texture{ nullptr };
                mutable uint32_t m_other_segment_count{ 0 };

                mutable std::vector<float> m_options_segment_list;
                mutable bool m_b_options_segment_dirty{ true };
                mutable std::shared_ptr<GLTexture> m_p_options_segment_texture{ nullptr };
                mutable uint32_t m_options_segment_count{ 0 };
                // end for the top layer draw

                EViewType m_view_type{ EViewType::Count };
                bool m_b_view_type_dirty{ true };
            };
        } // namespace gcode
    } // namespace GUI
} // namespace Slic3r