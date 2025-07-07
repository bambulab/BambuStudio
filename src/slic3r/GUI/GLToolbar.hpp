#ifndef slic3r_GLToolbar_hpp_
#define slic3r_GLToolbar_hpp_

#include <functional>
#include <string>
#include <vector>
#include <chrono>

#include "GLTexture.hpp"
#include "Event.hpp"
#include "libslic3r/Point.hpp"

class wxEvtHandler;

namespace Slic3r {
namespace GUI {

class GLCanvas3D;
struct Camera;

//BBS: GUI refactor: GLToolbar
wxDECLARE_EVENT(EVT_GLTOOLBAR_OPEN_PROJECT, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_SLICE_ALL, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_SLICE_PLATE, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_PRINT_ALL, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_PRINT_PLATE, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_EXPORT_GCODE, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_SEND_GCODE, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_UPLOAD_GCODE, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_EXPORT_SLICED_FILE, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_EXPORT_ALL_SLICED_FILE, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_PRINT_SELECT, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_SEND_TO_PRINTER, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_SEND_TO_PRINTER_ALL, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_PRINT_MULTI_MACHINE, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_SEND_MULTI_APP, SimpleEvent);


wxDECLARE_EVENT(EVT_GLTOOLBAR_ADD, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_DELETE, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_DELETE_ALL, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_ADD_PLATE, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_DEL_PLATE, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_ORIENT, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_ARRANGE, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_CUT, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_COPY, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_PASTE, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_LAYERSEDITING, SimpleEvent);
//BBS: add clone event
wxDECLARE_EVENT(EVT_GLTOOLBAR_CLONE, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_MORE, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_FEWER, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_SPLIT_OBJECTS, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_SPLIT_VOLUMES, SimpleEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_FILLCOLOR, IntEvent);
wxDECLARE_EVENT(EVT_GLTOOLBAR_SELECT_SLICED_PLATE, wxCommandEvent);

wxDECLARE_EVENT(EVT_GLVIEWTOOLBAR_3D, SimpleEvent);
wxDECLARE_EVENT(EVT_GLVIEWTOOLBAR_PREVIEW, SimpleEvent);
wxDECLARE_EVENT(EVT_GLVIEWTOOLBAR_ASSEMBLE, SimpleEvent);


class GLToolbarItem
{
public:
    typedef std::function<void()> ActionCallback;
    typedef std::function<bool()> VisibilityCallback;
    typedef std::function<bool()> EnablingCallback;
    typedef std::function<void(float, float, float, float, float)> RenderCallback;
    using OnHoverCallback = std::function<std::string()>;
    using IconFilenameCallback = std::function<std::string(bool is_dark_mode)>;
    using PressedRecheckCallback = std::function<bool()>;

    enum EType : unsigned char
    {
        Action,
        Separator,
        //BBS: GUI refactor: GLToolbar
        ActionWithText,
        ActionWithTextImage,
        SeparatorLine,
        Num_Types
    };

    enum EActionType : unsigned char
    {
        Undefined,
        Left,
        Right,
        Num_Action_Types
    };

    enum EState : unsigned char
    {
        Normal,
        Pressed,
        Disabled,
        Hover,
        HoverPressed,
        HoverDisabled,
        Num_States
    };

    enum EHighlightState : unsigned char
    {
        HighlightedShown,
        HighlightedHidden,
        Num_Rendered_Highlight_States,
        NotHighlighted
    };

    struct Data
    {
        struct Option
        {
            bool toggable;
            ActionCallback action_callback;
            RenderCallback render_callback;

            Option();

            bool can_render() const { return toggable && (render_callback != nullptr); }
        };

        std::string name;
        std::string tooltip;
        std::string additional_tooltip;
        //BBS: GUI refactor: GLToolbar
        std::string button_text;
        float extra_size_ratio;
        GLTexture text_texture;
        GLTexture image_texture;
        std::vector<unsigned char> image_data;
        unsigned int image_width;
        unsigned int image_height;

        unsigned int sprite_id;
        // mouse left click
        Option left;
        // mouse right click
        Option right;
        bool visible;
        bool continuous_click{false};
        VisibilityCallback visibility_callback;
        EnablingCallback enabling_callback;
        OnHoverCallback on_hover = nullptr;
        IconFilenameCallback icon_filename_callback = nullptr;
        PressedRecheckCallback pressed_recheck_callback = nullptr;
        bool b_toggle_disable_others{ true };
        bool b_toggle_affectable{ true };
        bool b_collapsible{ true };
        bool b_collapse_button{ false };
        bool b_collapsed{ false };
        Data();
        //BBS: GUI refactor: GLToolbar
        Data(const GLToolbarItem::Data& data)
        {
            name = data.name;
            tooltip = data.tooltip;
            additional_tooltip = data.additional_tooltip;
            button_text = data.button_text;
            extra_size_ratio = data.extra_size_ratio;
            sprite_id = data.sprite_id;
            left = data.left;
            right = data.right;
            visible = data.visible;
            continuous_click    = data.continuous_click;
            visibility_callback = data.visibility_callback;
            enabling_callback = data.enabling_callback;
            image_data = data.image_data;
            image_width = data.image_width;
            image_height = data.image_height;
            on_hover = data.on_hover;
            icon_filename_callback = data.icon_filename_callback;
            pressed_recheck_callback = data.pressed_recheck_callback;
            b_toggle_disable_others = data.b_toggle_disable_others;
            b_toggle_affectable = data.b_toggle_affectable;
            b_collapsible = data.b_collapsible;
            b_collapse_button = data.b_collapse_button;
            b_collapsed = data.b_collapsed;
        }
    };

    static const ActionCallback Default_Action_Callback;
    static const VisibilityCallback Default_Visibility_Callback;
    static const EnablingCallback Default_Enabling_Callback;
    static const RenderCallback Default_Render_Callback;

private:
    EType m_type;
    EState m_state;
    Data m_data;
    EActionType m_last_action_type;
    EHighlightState m_highlight_state;
    std::chrono::system_clock::time_point start;

public:

    mutable float render_rect[4]{ 0.0f }; // left, right, bottom, top
    mutable float* override_render_rect{ nullptr };

    std::chrono::system_clock::time_point get_start_time_point() const { return start; }

    GLToolbarItem(EType type, const Data& data);

    EState get_state() const { return m_state; }
    void set_state(EState state);

    EHighlightState get_highlight() const { return m_highlight_state; }
    void set_highlight(EHighlightState state) { m_highlight_state = state; }

    const std::string& get_name() const { return m_data.name; }
    std::string get_icon_filename(bool is_dark_mode) const;
    const std::string& get_tooltip() const { return m_data.tooltip; }
    const std::string& get_additional_tooltip() const { return m_data.additional_tooltip; }
    void set_additional_tooltip(const std::string& text) { m_data.additional_tooltip = text; }
    void set_tooltip(const std::string& text)            { m_data.tooltip = text; }
    void set_last_action_type(GLToolbarItem::EActionType type);
    void do_left_action();
    void do_right_action();

    bool is_enabled() const { return (m_state != Disabled) && (m_state != HoverDisabled); }
    bool is_disabled() const { return (m_state == Disabled) || (m_state == HoverDisabled); }
    bool is_hovered() const { return (m_state == Hover) || (m_state == HoverPressed) || (m_state == HoverDisabled); }
    bool is_pressed() const { return (m_state == Pressed) || (m_state == HoverPressed); }
    bool is_visible() const;
    bool is_separator() const { return m_type == Separator; }
    bool toggle_disable_others() const;
    bool toggle_affectable() const;

    bool is_left_toggable() const { return m_data.left.toggable; }
    bool is_right_toggable() const { return m_data.right.toggable; }

    bool has_left_render_callback() const { return m_data.left.render_callback != nullptr; }
    bool has_right_render_callback() const { return m_data.right.render_callback != nullptr; }

    EActionType get_last_action_type() const { return m_last_action_type; }
    void reset_last_action_type() { m_last_action_type = Undefined; }

    // returns true if the state changes
    bool update_visibility();
    // returns true if the state changes
    bool update_enabled_state();

    //BBS: GUI refactor: GLToolbar
    bool get_continuous_click_flag() const { return m_data.continuous_click; }
    bool is_action() const { return m_type == Action; }
    bool is_action_with_text() const { return m_type == ActionWithText; }
    bool is_action_with_text_image() const { return m_type == ActionWithTextImage; }
    const std::string& get_button_text() const { return m_data.button_text; }
    void set_button_text(const std::string& text) { m_data.button_text = text; }
    float get_extra_size_ratio() const { return m_data.extra_size_ratio; }
    void set_extra_size_ratio(const float ratio) { m_data.extra_size_ratio = ratio; }
    void render_text() const;
    int generate_texture(wxFont& font);
    int generate_image_texture();

    void render(unsigned int tex_id, unsigned int tex_width, unsigned int tex_height, unsigned int icon_size, float toolbar_height, bool b_flip_v = false) const;
    void render_image(unsigned int tex_id, float left, float right, float bottom, float top, unsigned int tex_width, unsigned int tex_height, unsigned int icon_size) const;

    const GLToolbarItem::Data& get_data() const;

    void set_visible(bool visible);

    GLToolbarItem::EType get_type() const;

    bool is_inside(const Vec2d& mouse_pos) const;

    bool is_collapsible() const;

    bool is_collapse_button() const;

    void set_collapsed(bool value);

    bool is_collapsed() const;

    bool recheck_pressed() const;
};

struct ToolbarLayout
{
    enum EType : unsigned char
    {
        Horizontal,
        Vertical,
        Num_Types
    };

    enum EHorizontalOrientation : unsigned char
    {
        HO_Left,
        HO_Center,
        HO_Right,
        Num_Horizontal_Orientations
    };

    enum EVerticalOrientation : unsigned char
    {
        VO_Top,
        VO_Center,
        VO_Bottom,
        Num_Vertical_Orientations
    };

    enum EPositionMode : uint8_t
    {
        TopLeft,
        TopMiddle,
        Custom
    };

    EType type;
    EHorizontalOrientation horizontal_orientation;
    EVerticalOrientation vertical_orientation;
    EPositionMode position_mode;
    float offset;
    float top;
    float left;
    float border;
    float separator_size;
    float gap_size;
    float icons_size;
    float text_size;
    float image_width;
    float image_height;
    float scale;
    float collapsed_offset;

    float width;
    float height;
    bool dirty;

    ToolbarLayout();
};

class ToolbarRenderer;

class GLToolbar
{
public:
    static const float Default_Icons_Size;
public:
    enum EType : unsigned char
    {
        Normal,
        Radio,
        Num_Types
    };

    enum EToolbarRenderingMode : uint8_t
    {
        KeepSize,
        Auto
    };

    GLToolbar(GLToolbar::EType type, const std::string& name);
    virtual ~GLToolbar();

    bool init(const BackgroundTexture::Metadata& background_texture);

    bool init_arrow(const BackgroundTexture::Metadata& arrow_texture);

    const ToolbarLayout& get_layout() const;

    void set_layout_type(ToolbarLayout::EType type);

    ToolbarLayout::EHorizontalOrientation get_horizontal_orientation() const;
    void set_horizontal_orientation(ToolbarLayout::EHorizontalOrientation orientation);

    ToolbarLayout::EVerticalOrientation get_vertical_orientation() const;
    void set_vertical_orientation(ToolbarLayout::EVerticalOrientation orientation);

    void set_position_mode(ToolbarLayout::EPositionMode t_position_mode);
    void set_offset(float offset);
    void set_position(float top, float left);
    void set_border(float border);
    void set_separator_size(float size);
    void set_gap_size(float size);
    void set_icons_size(float size);
    void set_text_size(float size);
    void set_scale(float scale);
    float get_scale() const;

    void set_icon_dirty();

    bool is_enabled() const;
    void set_enabled(bool enable);

    float get_icons_size() const;
    float get_width() const;
    float get_height() const;

    //BBS: GUI refactor: GLToolbar
    bool add_item(const GLToolbarItem::Data& data, GLToolbarItem::EType type = GLToolbarItem::Action);
    bool add_separator();
    bool del_all_item();

    void select_item(const std::string& name);

    bool is_item_pressed(const std::string& name) const;
    bool is_item_disabled(const std::string& name) const;
    bool is_item_visible(const std::string& name) const;

    bool is_any_item_pressed() const;

    unsigned int get_items_count() const { return (unsigned int)m_items.size(); }
    int get_item_id(const std::string& name) const;

    std::string get_tooltip() const;
    void set_tooltip(int item_id, const std::string& text);

    void get_additional_tooltip(int item_id, std::string& text);
    void set_additional_tooltip(int item_id, const std::string& text);
    int  get_visible_items_cnt() const;

    // get item pointer for highlighter timer
    const std::shared_ptr<GLToolbarItem>& get_item(const std::string& item_name) const;

    void render(const Camera& t_camera);

    // returns true if any item changed its state
    bool update_items_state();

    void set_dark_mode_enabled(bool is_enabled);

    const std::vector<std::shared_ptr<GLToolbarItem>>& get_items() const;

    const GLTexture& get_icon_texture() const;
    const BackgroundTexture& get_background_texture() const;
    const BackgroundTexture& get_arrow_texture() const;

    bool needs_collapsed() const;

    void toggle_collapsed();
    void set_collapsed();

    bool is_collapsed() const;

    void set_collapsed_offset(uint32_t offset_in_pixel);
    uint32_t get_collapsed_offset();

    GLToolbar::EToolbarRenderingMode get_rendering_mode();
    void set_rendering_mode(GLToolbar::EToolbarRenderingMode mode);

    void force_left_action(int item_id, GLCanvas3D& parent) { do_action(GLToolbarItem::Left, item_id, parent, false); }
    void force_right_action(int item_id, GLCanvas3D& parent) { do_action(GLToolbarItem::Right, item_id, parent, false); }

    void render_arrow(const std::weak_ptr<GLToolbarItem>& highlighted_item);

    bool on_mouse(wxMouseEvent& evt, GLCanvas3D& parent);

    //BBS: GUI refactor: GLToolbar
    int generate_button_text_textures(wxFont& font);
    int generate_image_textures();
    float get_scaled_icon_size();

protected:
    void calc_layout() const;
    const std::shared_ptr<ToolbarRenderer>& get_renderer() const;

private:
    float get_width_horizontal() const;
    float get_width_vertical() const;
    float get_height_horizontal() const;
    float get_height_vertical() const;
    float get_main_size() const;
    bool generate_icons_texture() const;

    // returns true if any item changed its state
    bool update_items_visibility();
    // returns true if any item changed its state
    bool update_items_enabled_state();

    bool update_items_pressed_state();

    void do_action(GLToolbarItem::EActionType type, int item_id, GLCanvas3D& parent, bool check_hover);
    void update_hover_state(const Vec2d& mouse_pos, GLCanvas3D& parent);
    // returns the id of the item under the given mouse position or -1 if none
    int contains_mouse(const Vec2d& mouse_pos, const GLCanvas3D& parent) const;

private:
    mutable ToolbarLayout m_layout;
    mutable bool m_icons_texture_dirty{ true };
    mutable GLTexture m_icons_texture;
    bool m_enabled{ false };
    GLToolbar::EType m_type{ GLToolbar::EType::Normal };
    std::string m_name{};
    std::vector<std::shared_ptr<GLToolbarItem>> m_items;

    BackgroundTexture m_background_texture;
    BackgroundTexture m_arrow_texture;

    int m_pressed_toggable_id{ -1 };
    bool m_b_dark_mode_enabled{ false };

    mutable std::shared_ptr<ToolbarRenderer> m_p_renderer{ nullptr };

    bool m_b_collapsed{ true };

    EToolbarRenderingMode m_rendering_mode{ EToolbarRenderingMode::Auto };

    mutable GLTexture m_images_texture;
    mutable bool m_images_texture_dirty;
    struct MouseCapture
    {
        bool left;
        bool middle;
        bool right;
        GLCanvas3D* parent;

        MouseCapture() { reset(); }

        bool any() const { return left || middle || right; }
        void reset() { left = middle = right = false; parent = nullptr; }
    };

    MouseCapture m_mouse_capture;
};

class ToolbarRenderer
{
public:
    explicit ToolbarRenderer();
    ~ToolbarRenderer();

    virtual void render(const GLToolbar& t_toolbar, const Camera& t_camera) = 0;

    virtual GLToolbar::EToolbarRenderingMode get_mode() const = 0;

    virtual void render_arrow(const GLToolbar& t_toolbar, const std::weak_ptr<GLToolbarItem>& highlighted_item) = 0;

    virtual bool needs_collapsed() const;
};

class ToolbarAutoSizeRenderer : public ToolbarRenderer
{
public:
    explicit ToolbarAutoSizeRenderer();
    ~ToolbarAutoSizeRenderer();

    void render(const GLToolbar& t_toolbar, const Camera& t_camera) override;

    GLToolbar::EToolbarRenderingMode get_mode() const override;

    void render_arrow(const GLToolbar& t_toolbar, const std::weak_ptr<GLToolbarItem>& highlighted_item) override;

private:
    void render_horizontal(const GLToolbar& t_toolbar, const Camera& t_camera);
    void render_vertical(const GLToolbar& t_toolbar, const Camera& t_camera);
    void render_background(const GLToolbar& t_toolbar, float left, float top, float right, float bottom, float border) const;
    void calculate_position(const GLToolbar& t_toolbar, const Camera& t_camera, float& left, float& top);
};

class ToolbarKeepSizeRenderer : public ToolbarRenderer
{
public:
    explicit ToolbarKeepSizeRenderer();
    ~ToolbarKeepSizeRenderer();

    void render(const GLToolbar& t_toolbar, const Camera& t_camera) override;

    GLToolbar::EToolbarRenderingMode get_mode() const override;

    void render_arrow(const GLToolbar& t_toolbar, const std::weak_ptr<GLToolbarItem>& highlighted_item) override;

    bool needs_collapsed() const override;

private:
    void render_horizontal(const GLToolbar& t_toolbar);
    void render_vertical(const GLToolbar& t_toolbar);
    void render_background(const GLToolbar& t_toolbar, float left, float top, float right, float bottom, float border) const;
    void calculate_position(const GLToolbar& t_toolbar, const Camera& t_camera, float toolbar_width, float toolbar_height, float& left, float& top);
    void recalculate_item_pos(const GLToolbar& t_toolbar, const Camera& t_camera, float& final_toolbar_width, float& final_toolbar_height, float& collapse_width);

private:
    bool m_b_needs_collapsed{ false };
    std::vector<size_t> m_indices_to_draw;
    float* m_p_override_render_rect{ nullptr };
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLToolbar_hpp_
