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
//#define DEBUG_TEXT
//#define DEBUG_TEXT_VALUE
//search TextToDo
enum class SLAGizmoEventType : unsigned char;
//1.0 for 1.10 lastest version
//1.1 for 2.0 lastest version//load font update
const std::string CUR_FONT_VERSION  = "2.0";
class GLGizmoText : public GLGizmoBase
{
private:
    std::vector<std::string> m_avail_font_names;
    std::string   m_text{""};
    std::string   m_font_name;
    wxString      m_cur_font_name;
    std::string m_font_version = CUR_FONT_VERSION;
    float m_font_size = 10.f;
    const float m_font_size_min = 3.f;
    const float m_font_size_max     = 1000.f;
    int m_curr_font_idx = 0;
    bool m_warning_font      = false;
    bool m_warning_cur_font_not_support_part_text = false;
    bool m_bold = true;
    bool m_italic = false;
    float m_thickness = 2.f;
    const float  m_thickness_min     = 0.01f;
    const float  m_thickness_max     = 1000.f;
    float m_embeded_depth = 0.f;
    const float  m_embeded_depth_max = 1000.f;
    float m_rotate_angle = 0;
    float m_text_gap = 0.f;
    enum TextType {
        HORIZONAL,
        SURFACE,
        SURFACE_HORIZONAL
    };
    TextType m_text_type{TextType ::SURFACE};
    bool m_really_use_surface_calc = false;
    bool m_use_current_pose = true;
    mutable RaycastResult    m_rr;

    float m_combo_height = 0.0f;
    float m_combo_width = 0.0f;
    float m_scale;

    Vec2d m_mouse_position = Vec2d::Zero();
    Vec2d m_origin_mouse_position = Vec2d::Zero();
    bool  m_shift_down     = false;

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

    bool m_is_modify = false;
    bool m_need_update_text = false;
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
    Emboss::DataUpdate m_data_update;
    // For text on scaled objects
    std::optional<float>     m_scale_height;
    std::optional<float>     m_scale_depth;
    std::optional<float>     m_scale_width;
    TextLinesModel           m_text_lines;
    // drawing icons
    IconManager              m_icon_manager;
    IconManager::VIcons      m_icons;

    int m_preview_text_volume_id = -1;
    Vec3d                    m_fix_text_position_in_world = Vec3d::Zero();
    Vec3f                    m_fix_text_normal_in_world   = Vec3f::Zero();
    bool                     m_need_fix;
    Vec3d                    m_text_position_in_world = Vec3d::Zero();
    Vec3f                    m_text_normal_in_world  = Vec3f::Zero();
    Geometry::Transformation m_text_tran_in_object;
    Geometry::Transformation m_text_tran_in_world;
    Geometry::Transformation m_load_text_tran_in_object;
    Geometry::Transformation m_model_object_in_world_tran;
    Transform3d              m_text_cs_to_world_tran;
    Transform3d              m_object_cs_to_world_tran;
    Vec3d m_cut_plane_dir_in_world = Vec3d::UnitZ();

    std::vector<Vec3d> m_position_points;
    std::vector<Vec3d> m_normal_points;
    std::vector<Vec3d> m_cut_points_in_world;
    // This map holds all translated description texts, so they can be easily referenced during layout calculations
    // etc. When language changes, GUI is recreated and this class constructed again, so the change takes effect.
    std::map<std::string, wxString> m_desc;
    Transform3d                     m_text_volume_tran;
    ModelVolume *                   m_last_text_mv{nullptr};
    EmbossShape                     m_text_shape;
    std::vector<TriangleMesh>       m_chars_mesh_result;//temp code
    // move gizmo
    Grabber m_move_grabber;
    const int m_move_cube_id = 1;
    // Rotation gizmo
    GLGizmoRotate        m_rotate_gizmo;
    std::optional<float> m_angle;
    std::optional<float> m_distance;
    std::optional<float> m_rotate_start_angle;
    // TRN - Title in Undo/Redo stack after move with SVG along emboss axe - From surface
    const std::string move_snapshot_name = "Text move";
    const std::string rotation_snapshot_name = "Text rotate";

public:
    GLGizmoText(GLCanvas3D& parent, unsigned int sprite_id);
    ~GLGizmoText();

    void update_font_texture();

    bool gizmo_event(SLAGizmoEventType action, const Vec2d &mouse_position, bool shift_down, bool alt_down, bool control_down) override;

    bool is_mesh_point_clipped(const Vec3d &point, const Transform3d &trafo) const;
    BoundingBoxf3 bounding_box() const;
    static EmbossStyles create_default_styles();
    bool                select_facename(const wxString &facename,bool update_text );

    std::string get_icon_filename(bool b_dark_mode) const override;

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
    void draw_style_add_button();
    void draw_delete_style_button();
    void update_boldness();
    void update_italic();
    void set_default_boldness(std::optional<float> &boldness);

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

private:
    void check_text_type(bool is_surface_text,bool is_keep_horizontal);
    void generate_text_tran_in_world(const Vec3d &text_normal_in_world, const Vec3d &text_position_in_world, float rotate_degree, Geometry::Transformation &tran);
    void use_fix_normal_position();
    void load_init_text();
    void update_text_pos_normal();
    //void update_font_status();
    void reset_text_info();

    float get_text_height(const std::string &text);
    void close_warning_flag_after_close_or_drag();
    void update_text_normal_in_world();
    bool update_text_positions(const std::vector<std::string>& texts);
    TriangleMesh get_text_mesh(int i,const char* text_str, const Vec3d &position, const Vec3d &normal, const Vec3d &text_up_dir);

    bool update_raycast_cache(const Vec2d &mouse_position, const Camera &camera, const std::vector<Transform3d> &trafo_matrices);
    void generate_text_volume(bool is_temp = true);
    void delete_temp_preview_text_volume();

    TextInfo get_text_info();
    void     load_from_text_info(const TextInfo &text_info);
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoText_hpp_
