#pragma once
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/CustomGCode.hpp"
#include "slic3r/GUI/3DScene.hpp"
#include <boost/iostreams/device/mapped_file.hpp>
#include <imgui/imgui.h>
#include <vector>
#include <string>
#include <array>
namespace Slic3r {
    class BuildVolume;
    class PresetBundle;
    struct ThumbnailData;
    struct ThumbnailsParams;
    class DynamicPrintConfig;
    class Print;
    namespace GUI {
        class IMSlider;
        class OpenGLManager;
        class PartPlateList;
        class IMSlider;
        namespace gcode
        {
            struct SequentialView;
            struct Extrusions;
            enum class EViewType : unsigned char
            {
                Summary = 0,
                FeatureType,
                Height,
                Width,
                Feedrate,
                FanSpeed,
                Temperature,
                VolumetricRate,
                Tool,
                ColorPrint,
                FilamentId,
                LayerTime,
                // helio
                ThermalIndexMin,
                ThermalIndexMax,
                ThermalIndexMean,
                // end helio
                Count
            };

            struct ExtruderFilament
            {
                std::string   type;
                std::string   hex_color;
                unsigned char filament_id;
                bool is_support_filament;
            };

            // helper to render shells
            struct Shells
            {
                GLVolumeCollection volumes;
                bool               visible{ false };
                // BBS: always load shell when preview
                int  print_id{ -1 };
                int  print_modify_count{ -1 };
                bool previewing{ false };
                void reset();
            };

            using Color = std::array<float, 4>;

            struct ETools
            {
                std::vector<Color> m_tool_colors;
                std::vector<bool>  m_tool_visibles;
            };

            enum class EOptionsColors : unsigned char
            {
                Retractions,
                Unretractions,
                Seams,
                ToolChanges,
                ColorChanges,
                PausePrints,
                CustomGCodes
            };

            class BaseRenderer
            {
            public:
                explicit BaseRenderer();
                virtual ~BaseRenderer();
                virtual void init(ConfigOptionMode mode, PresetBundle* preset_bundle);
                bool is_legend_enabled() const;
                void enable_legend(bool enable);
                float get_legend_height() const;
                const Shells& get_shells() const;
                const GCodeCheckResult& get_gcode_check_result() const;
                const FilamentPrintableResult& get_filament_printable_result() const;
                const ConflictResultOpt& get_conflict_result() const;
                size_t get_extruders_count() const;
                bool has_data() const;
                const float get_max_print_height() const;
                const BoundingBoxf3& get_paths_bounding_box() const;
                const BoundingBoxf3& get_max_bounding_box() const;
                const BoundingBoxf3& get_shell_bounding_box() const;
                bool is_contained_in_bed() const;
                EViewType get_view_type() const;
                std::vector<CustomGCode::Item>& get_custom_gcode_per_print_z();
                void update_shells_color_by_extruder(const DynamicPrintConfig* config);
                void load_shells(const Print& print, bool initialized, bool force_previewing = false);
                void set_shells_on_preview(bool is_previewing);
                const std::array<unsigned int, 2>& get_layers_z_range() const;
                void toggle_gcode_window_visibility();
                const std::shared_ptr<SequentialView>& get_sequential_view() const;
                void on_change_color_mode(bool is_dark);
                void set_scale(float scale = 1.0);
                virtual void update_marker_curr_move();
                void load(const GCodeProcessorResult& gcode_result, const Print& print, const BuildVolume& build_volume,
                    const std::vector<BoundingBoxf3>& exclude_bounding_box, bool initialized, ConfigOptionMode mode, bool only_gcode = false);

                void set_view_type(EViewType type, bool reset_feature_type_visible = true);
                void reset_visible(EViewType type);

                //BBS: add only gcode mode
                bool is_only_gcode_in_preview() const;
                /* BBS IMSlider */
                IMSlider* get_moves_slider();
                IMSlider* get_layers_slider();
                void reset_shell();

                //BBS: add all plates filament statistics
                void render_all_plates_stats(const std::vector<const GCodeProcessorResult*>& gcode_result_list, bool show = true) const;

                // helio
                void set_show_horizontal_slider(bool flag);
                bool is_show_horizontal_slider() const;
                bool is_helio_option() const;
                bool curr_plate_has_ok_helio_slice(int plate_idx) const;
                void reset_curr_plate_thermal_options();
                void record_gcodeviewer_option_item();
                bool get_min_max_value_of_option(int index, float& _min, float& _max) const;
                // end helio

                virtual void reset();
                // recalculate ranges in dependence of what is visible and sets tool/print colors
                virtual void refresh(const GCodeProcessorResult& gcode_result, const std::vector<std::string>& str_tool_colors);
                virtual void update_sequential_view_current(unsigned int first, unsigned int last) = 0;
                virtual void render_calibration_thumbnail(ThumbnailData& thumbnail_data, unsigned int w, unsigned int h, const ThumbnailsParams& thumbnail_params, PartPlateList& partplate_list, OpenGLManager& opengl_manager) = 0;
                virtual std::vector<double> get_layers_zs() const = 0;
                virtual unsigned int get_options_visibility_flags() const = 0;
                virtual void set_options_visibility_from_flags(unsigned int flags) = 0;
                virtual void refresh_render_paths() = 0;
                virtual bool can_export_toolpaths() const = 0;
                virtual void export_toolpaths_to_obj(const char* filename) const = 0;
                virtual void set_layers_z_range(const std::array<unsigned int, 2>& layers_z_range) = 0;
                virtual void render(int canvas_width, int canvas_height, int right_margin) = 0;
                virtual bool is_move_type_visible(EMoveType type) const = 0;
                virtual void set_move_type_visible(EMoveType type, bool visible) = 0;
                virtual bool is_extrusion_role_visible(ExtrusionRole role) const = 0;
                virtual void set_extrusion_role_visible(ExtrusionRole role, bool is_visible) = 0;
                virtual uint32_t get_extrusion_role_visibility_flags() const = 0;
                virtual void set_extrusion_role_visibility_flags(uint32_t flags) = 0;


                static const std::vector<Color> Extrusion_Role_Colors;
                static const std::vector<Color> Options_Colors;
                static const std::vector<Color> Travel_Colors;
                static const std::vector<ColorRGBA> Range_Colors;
                // helio
                static const std::vector<ColorRGBA> Default_Range_Colors;
                static const std::vector<ColorRGBA> Thermal_Index_Range_Colors;
                // end helio
                static const Color              Wipe_Color;
                static const Color              Neutral_Color;

            protected:
                void init_tool_maker(PresetBundle* preset_bundle);
                void render_sequential_view(uint32_t canvas_width, uint32_t canvas_height, int right_margin);
                virtual bool load_toolpaths(const GCodeProcessorResult& gcode_result, const BuildVolume& build_volume, const std::vector<BoundingBoxf3>& exclude_bounding_box) = 0;
                void render_shells();
                void render_slider(int canvas_width, int canvas_height);
                virtual void render_legend(float& legend_height, int canvas_width, int canvas_height, int right_margin);
                void update_by_mode(ConfigOptionMode mode);
                void update_thermal_options(bool add);
                void push_combo_style();
                void pop_combo_style();
                virtual void update_moves_slider(bool set_to_max = false);
                virtual bool show_sequential_view() const;
                virtual void on_visibility_changed();
                virtual void do_set_view_type(EViewType type);
                // helio
                void update_option_item_when_load_gcode();
                void update_default_view_type();
                void reset_curr_plate_thermal_options(int plate_idx);
                void init_thermal_icons();
                // end helio
            private:
                void delete_wipe_tower();
                void render_legend_color_arr_recommen(float window_padding);

            protected:
                bool m_legend_enabled{ true };
                float m_legend_height{ 0.0f };
                size_t m_extruders_count{ 0 };
                std::vector<ExtrusionRole> m_roles;
                float m_max_print_height{ 0.0f };
                // bounding box of toolpaths
                BoundingBoxf3 m_paths_bounding_box;
                // bounding box of toolpaths + marker tools
                BoundingBoxf3 m_max_bounding_box;
                //BBS: add shell bounding box
                BoundingBoxf3 m_shell_bounding_box;
                bool m_contained_in_bed{ true };
                IMSlider* m_moves_slider{ nullptr };
                IMSlider* m_layers_slider{ nullptr };
                EViewType m_view_type{ EViewType::FeatureType };
                std::vector<CustomGCode::Item> m_custom_gcode_per_print_z;
                std::array<unsigned int, 2> m_layers_z_range{ 0, 0 };
                bool m_is_dark{ false };
                bool m_gl_data_initialized{ false };
                // helio
                int m_last_non_helio_option_item{ -1 };
                int m_last_helio_process_status{ 0 };
                std::map<int, bool> m_helio_slice_map_oks;
                int m_last_back_process_status{ 0 };
                ImTextureID m_helio_icon_dark_texture{ nullptr };
                ImTextureID m_helio_icon_texture{ nullptr };
                bool m_show_horizontal_slider{ false };
                // end helio
                unsigned int m_last_result_id{ 0 };
                mutable std::shared_ptr<SequentialView> m_p_sequential_view{ nullptr };
                float m_scale{ 1.0 };
                std::vector<unsigned char> m_extruder_ids;
                //BBS: extruder dispensing filament
                std::vector<ExtruderFilament> m_left_extruder_filament;
                std::vector<ExtruderFilament> m_right_extruder_filament;
                size_t m_nozzle_nums{ 0 };
                bool m_fold{ false };
                std::vector<size_t> m_ssid_to_moveid_map;
                std::vector<int> m_plater_extruder;
                //BBS: save m_gcode_result as well
                const GCodeProcessorResult* m_gcode_result{ nullptr };
                GCodeProcessorResult::SettingsIds m_settings_ids;
                std::vector<float> m_filament_diameters;
                std::vector<float> m_filament_densities;
                PrintEstimatedStatistics m_print_statistics;
                PrintEstimatedStatistics::ETimeMode m_time_estimate_mode{ PrintEstimatedStatistics::ETimeMode::Normal };
                //BBS: add only gcode mode
                bool m_only_gcode_in_preview{ false };
                //BBS
                Shells            m_shells;
                const DynamicPrintConfig *m_config;//equal glcanvas3d m_config
                GCodeCheckResult  m_gcode_check_result;
                FilamentPrintableResult filament_printable_reuslt;
                ConflictResultOpt m_conflict_result;

                /*BBS GUI refactor, store displayed items in color scheme combobox */
                std::vector<EViewType> view_type_items;
                int m_view_type_sel = 0;
                struct ImageName
                {
                    std::string option_name;
                    ImTextureID texture_id{ nullptr };
                    ImTextureID texture_id_dark{ nullptr };
                };
                std::vector<ImageName> view_type_image_names;

                std::vector<EMoveType> options_items;
                ConfigOptionMode m_user_mode;

                std::shared_ptr<Extrusions> m_p_extrusions{ nullptr };
                //BBS save m_tools_color and m_tools_visible
                ETools m_tools;
            };

            using OnAttachingHelio = std::function<void(size_t& length_of_line)>;

            class GCodeWindow
            {
            public:
                explicit GCodeWindow();
                ~GCodeWindow();
                void load_gcode(const std::string& filename, const std::vector<size_t>& lines_ends);
                void reset();
                void toggle_visibility();
                //BBS: GUI refactor: add canvas size
                //void render(float top, float bottom, uint64_t curr_line_id) const;
                void render(float top, float bottom, float right, uint64_t curr_line_id, const OnAttachingHelio& cb, bool b_show_horizon_slider, bool b_helio_option) const;
                void on_change_color_mode(bool is_dark) { m_is_dark = is_dark; }
                void stop_mapping_file();
                void render_thermal_index_windows(
                    std::vector<GCodeProcessor::ThermalIndex> thermal_indexes,
                    float                                     top,
                    float                                     right,
                    float                                     wnd_height,
                    float                                     f_lines_count,
                    uint64_t                                  start_id,
                    uint64_t                                  end_id) const;

            private:
                struct Line
                {
                    std::string command;
                    std::string parameters;
                    std::string comment;
                };
                bool m_is_dark = false;
                bool m_visible{ true };
                uint64_t m_selected_line_id{ 0 };
                size_t m_last_lines_size{ 0 };
                std::string m_filename;
                boost::iostreams::mapped_file_source m_file;
                // map for accessing data in file by line number
                std::vector<size_t> m_lines_ends;
                // current visible lines
                std::vector<Line> m_lines;
            };

            struct SequentialView
            {
                class Marker
                {
                    GLModel m_model;
                    Vec3f m_world_position;
                    Transform3f m_world_transform;
                    // for seams, the position of the marker is on the last endpoint of the toolpath containing it
                    // the offset is used to show the correct value of tool position in the "ToolPosition" window
                    // see implementation of render() method
                    Vec3f m_world_offset;
                    float m_z_offset{ 0.5f };
                    GCodeProcessorResult::MoveVertex m_curr_move;
                    bool m_visible{ true };
                    bool m_is_dark = false;
                public:
                    float m_scale = 1.0f;
                    void init(std::string filename);
                    const BoundingBoxf3& get_bounding_box() const { return m_model.get_bounding_box(); }
                    void set_world_position(const Vec3f& position);
                    void set_world_offset(const Vec3f& offset) { m_world_offset = offset; }
                    bool is_visible() const { return m_visible; }
                    void set_visible(bool visible) { m_visible = visible; }
                    //BBS: GUI refactor: add canvas size
                    void render(int canvas_width, int canvas_height, const EViewType& view_type) const;
                    void on_change_color_mode(bool is_dark) { m_is_dark = is_dark; }
                    void update_curr_move(const GCodeProcessorResult::MoveVertex move);
                };
                struct Endpoints
                {
                    size_t first{ 0 };
                    size_t last{ 0 };
                };
                bool skip_invisible_moves{ false };
                Endpoints endpoints;
                Endpoints current;
                Endpoints last_current;
                Endpoints global;
                Vec3f current_position{ Vec3f::Zero() };
                Vec3f current_offset{ Vec3f::Zero() };
                Marker marker;
                GCodeWindow gcode_window;
                std::vector<unsigned int> gcode_ids;
                float m_scale = 1.0;
                //BBS: GUI refactor: add canvas size
                void render(float legend_height, int canvas_width, int canvas_height, int right_margin, const EViewType& view_type, const OnAttachingHelio& cb, bool b_show_horizon_slider, bool b_helio_option) const;
            };

            struct Range
            {
                enum class EType : unsigned char {
                    Linear,
                    Logarithmic
                };
                float min;
                float max;
                unsigned int count;
                bool log_scale;
                // helio
                bool is_fixed_range;
                std::vector<ColorRGBA> range_colors;
                // end helio
                Range(std::vector<ColorRGBA> range_colors_a = BaseRenderer::Range_Colors) : is_fixed_range(false), range_colors(range_colors_a) { reset(); }

                /*Sometimes we want min and max of the range to be fixed to have consistent color later*/
                Range(float min_a, float max_a, std::vector<ColorRGBA> range_colors_a = BaseRenderer::Range_Colors)
                    : min(min_a), max(max_a), is_fixed_range(true), range_colors(range_colors_a)
                {
                    reset();
                }
                void update_from(const float value) {
                    if (value != max && value != min)
                        ++count;
                    if (!is_fixed_range) {
                        min = std::min(min, value);
                        max = std::max(max, value);
                    }
                }
                void reset(bool log = false) {
                    if (!is_fixed_range) {
                        min = FLT_MAX;
                        max = -FLT_MAX;
                        count = 0;
                        log_scale = log;
                    }
                    else {
                        count = 0;
                        log_scale = log;
                    }
                }
                float step_size(EType type = EType::Linear) const;
                ColorRGBA get_color_at(float value, EType type = EType::Linear) const;

                float get_value_at_step(int step) const;
                float get_color_size() const;
            };

            struct Ranges
            {
                // Color mapping by layer height.
                Range height;
                // Color mapping by extrusion width.
                Range width;
                // Color mapping by feedrate.
                Range feedrate;
                // Color mapping by fan speed.
                Range fan_speed;
                // Color mapping by volumetric extrusion rate.
                Range volumetric_rate;
                // Color mapping by extrusion temperature.
                Range temperature;
                // Color mapping by layer time.
                Range layer_duration;
                // helio
                // Color mapping by thermal index min
                Range thermal_index_min{ -100.0f, 100.0f, BaseRenderer::Thermal_Index_Range_Colors };
                // Color mapping by thermal index max
                Range thermal_index_max{ -100.0f, 100.0f, BaseRenderer::Thermal_Index_Range_Colors };
                // Color mapping by thermal index mean
                Range thermal_index_mean{ -100.0f, 100.0f, BaseRenderer::Thermal_Index_Range_Colors };
                // end helio

                void reset() {
                    height.reset();
                    width.reset();
                    feedrate.reset();
                    fan_speed.reset();
                    volumetric_rate.reset();
                    temperature.reset();
                    layer_duration.reset(true);
                    // helio
                    thermal_index_min.reset();
                    thermal_index_max.reset();
                    thermal_index_mean.reset();
                    // end helio
                }
            };

            // helper to render extrusion paths
            struct Extrusions
            {
                uint32_t role_visibility_flags{ 0 };
                Ranges ranges;
                void reset_role_visibility_flags() {
                    role_visibility_flags = 0;
                    for (unsigned int i = 0; i < erCount; ++i) {
                        role_visibility_flags |= 1 << i;
                    }
                }
                void reset_ranges() { ranges.reset(); }
            };
        } // namespace gcode
    } // namespace GUI
} // namespace Slic3r