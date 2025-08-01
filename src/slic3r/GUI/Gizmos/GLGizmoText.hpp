#ifndef slic3r_GLGizmoText_hpp_
#define slic3r_GLGizmoText_hpp_

#include "GLGizmoRotate.hpp"
#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/SurfaceDrag.hpp"
#include "../GLTexture.hpp"
#include "../Camera.hpp"
#include "libslic3r/Model.hpp"
#include "slic3r/GUI/IconManager.hpp"
#include "slic3r/Utils/EmbossStyleManager.hpp"
#include "slic3r/GUI/Jobs/EmbossJob.hpp"
#include "libslic3r/TextConfiguration.hpp"
#include "slic3r/GUI/TextLines.hpp"
namespace Slic3r {

enum class ModelVolumeType : int;
class ModelVolume;

namespace GUI {
//#define DEBUG_TEXT_VALUE
//search TextToDo
enum class SLAGizmoEventType : unsigned char;
//1.0 mean v1.10 bambu version
//1.1 mean v2.0 v2.1 bambu version(202505)
//2.0 mean v2.2 bambu version(202507)
const std::string CUR_FONT_VERSION  = "2.0";
class GLGizmoText : public GLGizmoBase
{
private:
    bool  m_is_direct_create_text = false;
    std::vector<Transform3d> m_trafo_matrices;//Need to correspond m_c->raycaster()->raycasters
    int m_show_calc_meshtod = 0;//1 preview //2 draging
    std::vector<std::string> m_avail_font_names;
    std::string   m_text{""};
    std::string   m_font_name;
    wxString      m_cur_font_name;
    std::string m_font_version = CUR_FONT_VERSION;
    std::string m_style_name;
    float m_font_size = 10.f;
    const float m_font_size_min = 3.f;
    const float m_font_size_max     = 1000.f;
    bool m_warning_font      = false;
    bool m_bold = true;
    bool m_italic = false;
    float m_thickness = 2.f;
    const float  m_thickness_min     = 0.1f;
    const float  m_thickness_max     = 1000.f;
    float m_embeded_depth = 0.f;
    const float  m_embeded_depth_max = 1000.f;
    float m_rotate_angle = 0;
    float m_text_gap = 0.f;
    TextConfiguration  m_ui_text_configuration;
    TextInfo::TextType m_surface_type{TextInfo::TextType ::SURFACE};
    bool m_really_use_surface_calc = false;
    bool m_draging_cube            = false;
    mutable RaycastResult    m_rr;

    float m_combo_height = 0.0f;
    float m_combo_width = 0.0f;
    float m_scale;

    Vec2d m_mouse_position = Vec2d::Zero();
    Vec2d m_origin_mouse_position = Vec2d::Zero();

    class TextureInfo {
    public:
        GLTexture* texture { nullptr };
        int h;
        int w;
        int hl;

        std::string font_name;
    };

    std::vector<TextureInfo> m_textures;

    std::vector<std::string> m_font_names;

    bool m_init_texture = false;
    std::vector<bool> m_font_status;
    std::mutex m_mutex;
    std::thread m_thread;

    bool m_need_update_text = false;
    bool m_need_update_tran = false;
    bool m_reedit_text      = false;
    bool m_show_warning_text_create_fail = false;
    bool m_show_text_normal_error = false;
    bool m_show_text_normal_reset_tip = false;
    bool m_show_warning_regenerated = false;
    bool m_show_warning_old_tran    = false;
    bool m_show_warning_error_mesh  = false;
    bool m_show_warning_lost_rotate  = false;
    bool m_fix_old_tran_flag        = false;
    bool m_is_version1_10_xoy       = false;
    bool m_is_version1_9_xoz        = false;
    bool m_is_version1_8_yoz        = false;
    int  m_object_idx = -1;
    int  m_volume_idx = -1;
    //font deal
    struct Facenames; // forward declaration
    std::unique_ptr<Facenames> m_face_names;
    // Keep information about stored styles and loaded actual style to compare with
    Emboss::StyleManager     m_style_manager;
    std::shared_ptr<std::atomic<bool>> m_job_cancel                 = nullptr;
    // When open text loaded from .3mf it could be written with unknown font
    bool m_is_unknown_font = false;
    // Is open tree with advanced options
    bool m_is_advanced_edit_style = false;
    // True when m_text contain character unknown by selected font
    bool m_text_contain_unknown_glyph = false;
    std::string        m_style_new_name = "";
    // For text on scaled objects
    std::optional<float>     m_scale_height;
    std::optional<float>     m_scale_depth;
    void                     calculate_scale();
    std::optional<float>     m_scale_width;
    TextLinesModel           m_text_lines;
    // drawing icons
    IconManager              m_icon_manager;
    IconManager::VIcons      m_icons;

    Vec3d                    m_fix_text_position_in_world = Vec3d::Zero();
    Vec3f                    m_fix_text_normal_in_world   = Vec3f::Zero();
    Vec3d                    m_text_position_in_world = Vec3d::Zero();
    Vec3f                    m_text_normal_in_world  = Vec3f::Zero();
    Geometry::Transformation m_text_tran_in_object;
    Geometry::Transformation m_text_tran_in_world;
    Geometry::Transformation m_load_text_tran_in_object;
    Geometry::Transformation m_model_object_in_world_tran;
    std::optional<Geometry::Transformation> m_fix_text_tran;

    Vec3d m_cut_plane_dir_in_world = Vec3d::UnitZ();

    std::vector<Vec3d> m_position_points;
    std::vector<Vec3d> m_normal_points;
    std::vector<Vec3d> m_cut_points_in_world;
    // This map holds all translated description texts, so they can be easily referenced during layout calculations
    // etc. When language changes, GUI is recreated and this class constructed again, so the change takes effect.
    std::map<std::string, wxString> m_desc;
    ModelVolume *                   m_last_text_mv{nullptr};
    // move gizmo
    Grabber m_move_grabber;
    const int m_move_cube_id = 1;
    // Rotation gizmo
    GLGizmoRotate        m_rotate_gizmo;
    std::optional<float> m_distance;
    std::optional<float> m_rotate_start_angle;
    // TRN - Title in Undo/Redo stack after move with SVG along emboss axe - From surface
    const std::string move_snapshot_name = "Text move";
    const std::string rotation_snapshot_name = "Text rotate";

    float m_rotate_angle_min_max = 0.f;
    float m_text_gap_min_max     = 0.f;

public:
    GLGizmoText(GLCanvas3D& parent, unsigned int sprite_id);
    ~GLGizmoText();

    void update_font_texture();

    bool gizmo_event(SLAGizmoEventType action, const Vec2d &mouse_position, bool shift_down, bool alt_down, bool control_down) override;

    bool is_mesh_point_clipped(const Vec3d &point, const Transform3d &trafo) const;
    BoundingBoxf3 bounding_box() const;
    static EmbossStyles create_default_styles();
    bool                select_facename(const wxString &facename,bool update_text );
    bool                on_shortcut_key();
    bool                is_only_text_case() const;
    void                close();

    std::string get_icon_filename(bool b_dark_mode) const override;
    virtual std::string get_gizmo_entering_text() const{return "Enter Text gizmo";}
    virtual std::string get_gizmo_leaving_text() const{return "Leave Text gizmo";}
    bool    wants_enter_leave_snapshots() const override { return true; }

protected:
    virtual bool on_init() override;
    virtual std::string on_get_name() const override;
    virtual std::string on_get_name_str() override { return "Text shape"; }
    virtual bool on_is_activable() const override;
    virtual void on_render() override;
    virtual void on_render_for_picking() override;
    virtual void on_start_dragging() override;
    virtual void on_stop_dragging() override;
    virtual void on_update(const UpdateData &data) override;
    void push_combo_style(const float scale);
    void pop_combo_style();
    void push_button_style(bool pressed);
    void pop_button_style();
    virtual void on_set_state() override;
    virtual void data_changed(bool is_serializing) override;
    void     on_set_hover_id() override;
    void     on_enable_grabber(unsigned int id) override;
    void     on_disable_grabber(unsigned int id) override;
    bool     on_mouse(const wxMouseEvent &mouse_event) override;
    bool     on_mouse_for_rotation(const wxMouseEvent &mouse_event);
    virtual CommonGizmosDataID on_get_requirements() const override;
    virtual void on_render_input_window(float x, float y, float bottom_limit);

    void show_tooltip_information(float x, float y);

private:
    bool set_height();
    //ui
    void draw_text_input(int width);
    void draw_font_list();
    void draw_height(bool use_inch = false);
    void draw_depth(bool use_inch);
    void init_font_name_texture();
    void reinit_text_lines(unsigned count_lines = 0);
    bool   check(ModelVolumeType volume_type);
    bool   init_create(ModelVolumeType volume_type);

    template<typename T, typename Draw>
    bool revertible(const std::string &name, T &value, const T *default_value, const std::string &undo_tooltip, float undo_offset, Draw draw) const;
    template<typename T>
    bool rev_input(
        const std::string &name, T &value, const T *default_value, const std::string &undo_tooltip, T step, T step_fast, const char *format, ImGuiInputTextFlags flags = 0) const;
    template<typename T>
    bool rev_input_mm(const std::string &         name,
                      T &                         value,
                      const T *                   default_value,
                      const std::string &         undo_tooltip,
                      T                           step,
                      T                           step_fast,
                      const char *                format,
                      bool                        use_inch,
                      const std::optional<float> &scale) const;
    void load_old_font();
    void draw_style_list(float caption_size);
    void draw_style_save_button(bool is_modified);
    void draw_style_save_as_popup();
    void draw_style_add_button(bool is_modified);
    void draw_delete_style_button();
    void update_boldness();
    void update_italic();
    void set_default_boldness(std::optional<float> &boldness);
    void draw_model_type(int caption_width);
    void draw_surround_type(int caption_width);
    void draw_rotation(int caption_size, int slider_width, int drag_left_width, int slider_icon_width);

private: // ui
    struct GuiCfg;
    std::unique_ptr<GuiCfg> m_gui_cfg;

private:
    std::unique_ptr<Emboss::DataBase> create_emboss_data_base(const std::string &                 text,
                                                              Emboss::StyleManager &              style_manager,
                                                              //TextLinesModel &                    text_lines,//todo
                                                              const Selection &                   selection,
                                                              ModelVolumeType                     type,
                                                              std::shared_ptr<std::atomic<bool>> &cancel);
    bool process(bool make_snapshot = true, std::optional<Transform3d> volume_transformation = std::nullopt, bool update_text =true);

    ModelVolume *get_text_is_dragging();
    bool get_is_dragging();
    bool get_selection_is_text();
    bool update_text_tran_in_model_object(bool rewrite_text_tran = false);
    void update_trafo_matrices();
    void update_cut_plane_dir();
    void switch_text_type(TextInfo::TextType type);
    float get_angle_from_current_style();
    void volume_transformation_changed();
    void update_style_angle(ModelVolume *text_volume, float init_angle_degree, float roate_angle);

private:
    void generate_text_tran_in_world(const Vec3d &text_normal_in_world, const Vec3d &text_position_in_world, float rotate_degree, Geometry::Transformation &tran);
    void use_fix_normal_position();
    void load_init_text(bool first_open_text = false);
    void update_text_pos_normal();
    bool filter_model_volume(ModelVolume* mv);
    //void update_font_status();
    void reset_text_info();

    float get_text_height(const std::string &text);
    void close_warning_flag_after_close_or_drag();
    void update_text_normal_in_world();
    //bool update_text_positions();

    bool update_raycast_cache(const Vec2d &mouse_position, const Camera &camera, const std::vector<Transform3d> &trafo_matrices, bool exclude_last = true);
    bool generate_text_volume();

    TextInfo get_text_info();
    void     load_from_text_info(const TextInfo &text_info);
    bool     is_old_text_info(const TextInfo &text_info);
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoText_hpp_
