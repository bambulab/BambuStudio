#include "libslic3r/Point.hpp"
#include "libslic3r/libslic3r.h"

#include "GLToolbar.hpp"

#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/Plater.hpp"

#include <wx/event.h>
#include <wx/bitmap.h>
#include <wx/dcmemory.h>
#include <wx/settings.h>
#include <wx/glcanvas.h>

namespace Slic3r {
namespace GUI {

//BBS: GUI refactor: GLToolbar
wxDEFINE_EVENT(EVT_GLTOOLBAR_OPEN_PROJECT, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_SLICE_ALL, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_SLICE_PLATE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_PRINT_ALL, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_PRINT_PLATE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_EXPORT_GCODE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_SEND_GCODE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_UPLOAD_GCODE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_EXPORT_SLICED_FILE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_EXPORT_ALL_SLICED_FILE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_PRINT_SELECT, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_SEND_TO_PRINTER, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_SEND_TO_PRINTER_ALL, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_PRINT_MULTI_MACHINE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_SEND_MULTI_APP, SimpleEvent);


wxDEFINE_EVENT(EVT_GLTOOLBAR_ADD, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_DELETE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_DELETE_ALL, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_ADD_PLATE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_DEL_PLATE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_ORIENT, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_ARRANGE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_CUT, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_COPY, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_PASTE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_LAYERSEDITING, SimpleEvent);
//BBS: add clone event
wxDEFINE_EVENT(EVT_GLTOOLBAR_CLONE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_MORE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_FEWER, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_SPLIT_OBJECTS, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_SPLIT_VOLUMES, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_FILLCOLOR, IntEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_SELECT_SLICED_PLATE, wxCommandEvent);

wxDEFINE_EVENT(EVT_GLVIEWTOOLBAR_3D, SimpleEvent);
wxDEFINE_EVENT(EVT_GLVIEWTOOLBAR_PREVIEW, SimpleEvent);
wxDEFINE_EVENT(EVT_GLVIEWTOOLBAR_ASSEMBLE, SimpleEvent);

const GLToolbarItem::ActionCallback GLToolbarItem::Default_Action_Callback = [](){};
const GLToolbarItem::VisibilityCallback GLToolbarItem::Default_Visibility_Callback = []()->bool { return true; };
const GLToolbarItem::EnablingCallback GLToolbarItem::Default_Enabling_Callback = []()->bool { return true; };
const GLToolbarItem::RenderCallback GLToolbarItem::Default_Render_Callback = [](float, float, float, float, float){};

GLToolbarItem::Data::Option::Option()
    : toggable(false)
    , action_callback(Default_Action_Callback)
    , render_callback(nullptr)
{
}

GLToolbarItem::Data::Data()
    : name("")
    , tooltip("")
    , additional_tooltip("")
    , sprite_id(-1)
    , visible(true)
    , visibility_callback(Default_Visibility_Callback)
    , enabling_callback(Default_Enabling_Callback)
    //BBS: GUI refactor
    , extra_size_ratio(0.0)
    , button_text("")
    //BBS gei a default value
    , image_width(40)
    , image_height(40)
{
}

const GLToolbarItem::Data& GLToolbarItem::get_data() const
{
    return m_data;
}

void GLToolbarItem::set_visible(bool visible)
{
    m_data.visible = visible;
}

GLToolbarItem::EType GLToolbarItem::get_type() const
{
    return m_type;
}

bool GLToolbarItem::is_inside(const Vec2d& scaled_mouse_pos) const
{
    bool inside = (render_rect[0] <= (float)scaled_mouse_pos(0))
        && ((float)scaled_mouse_pos(0) <= render_rect[1])
        && (render_rect[2] <= (float)scaled_mouse_pos(1))
        && ((float)scaled_mouse_pos(1) <= render_rect[3]);
    return inside;
}

bool GLToolbarItem::is_collapsible() const
{
    return m_data.b_collapsible;
}

bool GLToolbarItem::is_collapse_button() const
{
    return m_data.b_collapse_button;
}

void GLToolbarItem::set_collapsed(bool value)
{
    if (!is_collapsible()) {
        return;
    }
    m_data.b_collapsed = value;
}

bool GLToolbarItem::is_collapsed() const
{
    if (!is_collapsible()) {
        return false;
    }
    return m_data.b_collapsed;
}

bool GLToolbarItem::recheck_pressed() const
{
    bool rt = false;
    if (m_data.pressed_recheck_callback) {
        const bool recheck_rt = m_data.pressed_recheck_callback();
        rt = (is_pressed() != recheck_rt);
    }
    return rt;
}

GLToolbarItem::GLToolbarItem(GLToolbarItem::EType type, const GLToolbarItem::Data& data)
    : m_type(type)
    , m_state(Normal)
    , m_data(data)
    , m_last_action_type(Undefined)
    , m_highlight_state(NotHighlighted)
{
}

void GLToolbarItem::set_state(EState state)
{
    if (m_data.name == "arrange" || m_data.name == "layersediting" || m_data.name == "assembly_view") {
        if (m_state == Hover && state == HoverPressed) {
            start = std::chrono::system_clock::now();
        }
        else if ((m_state == HoverPressed && state == Hover) ||
                 (m_state == Pressed && state == Normal) ||
                 (m_state == HoverPressed && state == Normal)) {
            if (m_data.name != "assembly_view") {
                std::chrono::system_clock::time_point end = std::chrono::system_clock::now();
                std::chrono::duration<int> duration = std::chrono::duration_cast<std::chrono::duration<int>>(end - start);
                int times = duration.count();

                NetworkAgent* agent = GUI::wxGetApp().getAgent();
                if (agent) {
                    std::string name = m_data.name + "_duration";
                    std::string value = "";
                    int existing_time = 0;

                    agent->track_get_property(name, value);
                    try {
                        if (value != "") {
                            existing_time = std::stoi(value);
                        }
                    }
                    catch (...) {}

                    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " tool name:" << name << " duration: " << times + existing_time;
                    agent->track_update_property(name, std::to_string(times + existing_time));
                }
            }
        }
    }
    m_state = state;
}

std::string GLToolbarItem::get_icon_filename(bool is_dark_mode) const
{
    if (m_data.icon_filename_callback) {
        return m_data.icon_filename_callback(is_dark_mode);
    }
    return "";
}

void GLToolbarItem::set_last_action_type(GLToolbarItem::EActionType type)
{
    m_last_action_type = type;
}

void GLToolbarItem::do_left_action()
{
    m_last_action_type = Left;
    m_data.left.action_callback();
}

void GLToolbarItem::do_right_action()
{
    m_last_action_type = Right;
    m_data.right.action_callback();
}

bool GLToolbarItem::is_visible() const
{
    bool rt = m_data.visible;
    return rt;
}

bool GLToolbarItem::toggle_disable_others() const
{
    return m_data.b_toggle_disable_others;
}

bool GLToolbarItem::toggle_affectable() const
{
    return m_data.b_toggle_affectable;
}

bool GLToolbarItem::update_visibility()
{
    bool visible = m_data.visibility_callback();
    bool ret = (m_data.visible != visible);
    if (ret)
        m_data.visible = visible;
    // Return false for separator as it would always return true.
    return is_separator() ? false : ret;
}

bool GLToolbarItem::update_enabled_state()
{
    bool enabled = m_data.enabling_callback();
    bool ret = (is_enabled() != enabled);
    if (ret)
        m_state = enabled ? GLToolbarItem::Normal : GLToolbarItem::Disabled;

    return ret;
}

//BBS: GUI refactor: GLToolbar
void GLToolbarItem::render_text() const
{
    if (is_collapsed()) {
        return;
    }
    float tex_width = (float)m_data.text_texture.get_width();
    float tex_height = (float)m_data.text_texture.get_height();
    //float inv_tex_width = (tex_width != 0.0f) ? 1.0f / tex_width : 0.0f;
    //float inv_tex_height = (tex_height != 0.0f) ? 1.0f / tex_height : 0.0f;

    float internal_left_uv = 0.0f;
    float internal_right_uv = (float)m_data.text_texture.m_original_width / tex_width;
    float internal_top_uv = 0.0f;
    float internal_bottom_uv = (float)m_data.text_texture.m_original_height / tex_height;

    GLTexture::render_sub_texture(m_data.text_texture.get_id(), render_rect[0], render_rect[1], render_rect[2], render_rect[3], {{internal_left_uv, internal_bottom_uv}, {internal_right_uv, internal_bottom_uv}, {internal_right_uv, internal_top_uv}, {internal_left_uv, internal_top_uv}});
}

//BBS: GUI refactor: GLToolbar
int GLToolbarItem::generate_texture(wxFont& font)
{
    int ret = 0;
    bool result;

    if (m_type != ActionWithText && m_type != ActionWithTextImage)
        return -1;

    result = m_data.text_texture.generate_from_text_string(m_data.button_text, font);
    if (!result)
        ret = -1;

    return ret;
}


int GLToolbarItem::generate_image_texture()
{
    int ret = 0;
    bool result = false;
    if (m_type != ActionWithTextImage)
        return -1;

    /* load default texture when image is empty */
    if (m_data.image_data.empty()) {
        std::string default_image = resources_dir() + "/images/default_thumbnail.svg";
        result = m_data.image_texture.load_from_svg_file(default_image, true, false, false, m_data.image_width);
    }  else {
        result = m_data.image_texture.load_from_raw_data(m_data.image_data, m_data.image_width, m_data.image_height);
    }
    if (!result)
        ret = -1;

    return ret;
}

void GLToolbarItem::render(unsigned int tex_id, unsigned int tex_width, unsigned int tex_height, unsigned int icon_size, float toolbar_height, bool b_flip_v) const
{
    auto uvs = [this](unsigned int tex_width, unsigned int tex_height, unsigned int icon_size, bool b_flip_v) -> GLTexture::Quad_UVs
    {
        assert((tex_width != 0) && (tex_height != 0));
        GLTexture::Quad_UVs ret;
        // tiles in the texture are spaced by 1 pixel
        float icon_size_px = (float)(tex_width - 1) / ((float)Num_States + (float)Num_Rendered_Highlight_States);
        char render_state = (m_highlight_state ==  NotHighlighted ? m_state : Num_States + m_highlight_state);
        float inv_tex_width = 1.0f / (float)tex_width;
        float inv_tex_height = 1.0f / (float)tex_height;
        // tiles in the texture are spaced by 1 pixel
        float u_offset = 1.0f * inv_tex_width;
        float v_offset = 1.0f * inv_tex_height;
        float du = icon_size_px * inv_tex_width;
        float dv = icon_size_px * inv_tex_height;
        float left = u_offset + (float)render_state * du;
        float right = left + du - u_offset;
        float top = v_offset + (float)m_data.sprite_id * dv;
        float bottom = top + dv - v_offset;

        if (b_flip_v) {
            std::swap(top, bottom);
        }
        ret.left_top = { left, top };
        ret.left_bottom = { left, bottom };
        ret.right_bottom = { right, bottom };
        ret.right_top = { right, top };
        return ret;
    };

    float* t_render_rect = render_rect;
    if (is_visible()) {
        if (!is_collapsed()) {
            GLTexture::render_sub_texture(tex_id, render_rect[0], render_rect[1], render_rect[2], render_rect[3], uvs(tex_width, tex_height, icon_size, b_flip_v));
        }
        else if (override_render_rect) {
            t_render_rect = override_render_rect;
        }
    }

    if (is_pressed())
    {
        if ((m_last_action_type == Left) && m_data.left.can_render())
            m_data.left.render_callback(t_render_rect[0], t_render_rect[1], t_render_rect[2], t_render_rect[3], toolbar_height);
        else if ((m_last_action_type == Right) && m_data.right.can_render())
            m_data.right.render_callback(t_render_rect[0], t_render_rect[1], t_render_rect[2], t_render_rect[3], toolbar_height);
    }
}

void GLToolbarItem::render_image(unsigned int tex_id, float left, float right, float bottom, float top, unsigned int tex_width, unsigned int tex_height, unsigned int icon_size) const
{
    GLTexture::Quad_UVs image_uvs = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
    //GLTexture::Quad_UVs image_uvs = { { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f } };

    if (is_visible()) {
        GLTexture::render_sub_texture(tex_id, left, right, bottom, top, image_uvs);
    }

    if (is_pressed()) {
        if ((m_last_action_type == Left) && m_data.left.can_render())
            m_data.left.render_callback(left, right, bottom, top, 0.0f);
        else if ((m_last_action_type == Right) && m_data.right.can_render())
            m_data.right.render_callback(left, right, bottom, top, 0.0f);
    }
}

const float GLToolbar::Default_Icons_Size = 40.0f;

ToolbarLayout::ToolbarLayout()
    : type(Horizontal)
    , horizontal_orientation(HO_Center)
    , vertical_orientation(VO_Center)
    , position_mode(EPositionMode::TopMiddle)
    , offset(0.0f)
    , top(0.0f)
    , left(0.0f)
    , border(0.0f)
    , separator_size(0.0f)
    , gap_size(0.0f)
    , icons_size(GLToolbar::Default_Icons_Size)
    , scale(1.0f)
    , width(0.0f)
    , height(0.0f)
    , collapsed_offset(0.0f)
    , dirty(true)
{
}

GLToolbar::GLToolbar(GLToolbar::EType type, const std::string& name)
    : m_type(type)
    , m_name(name)
    , m_pressed_toggable_id(-1)
{
}

GLToolbar::~GLToolbar()
{
}

bool GLToolbar::init(const BackgroundTexture::Metadata& background_texture)
{
    std::string path = resources_dir() + "/images/";
    bool res = false;

    if (!background_texture.filename.empty())
        res = m_background_texture.texture.load_from_file(path + background_texture.filename, false, GLTexture::SingleThreaded, false);

    if (res)
        m_background_texture.metadata = background_texture;

    return res;
}

bool GLToolbar::init_arrow(const BackgroundTexture::Metadata& arrow_texture)
{
    if (m_arrow_texture.texture.get_id() != 0)
        return true;

    std::string path = resources_dir() + "/images/";
    bool res = false;

    if (!arrow_texture.filename.empty()) {
        res = m_arrow_texture.texture.load_from_svg_file(path + arrow_texture.filename, false, false, false, 1000);
    }
    if (res)
        m_arrow_texture.metadata = arrow_texture;

    return res;
}

const ToolbarLayout& GLToolbar::get_layout() const
{
    if (m_layout.dirty) {
        calc_layout();
    }
    return m_layout;
}

void GLToolbar::set_layout_type(ToolbarLayout::EType type)
{
    if (m_layout.type == type) {
        return;
    }
    m_layout.type = type;
    m_layout.dirty = true;
}

ToolbarLayout::EHorizontalOrientation GLToolbar::get_horizontal_orientation() const
{
    return m_layout.horizontal_orientation;
}

void GLToolbar::set_horizontal_orientation(ToolbarLayout::EHorizontalOrientation orientation)
{
    m_layout.horizontal_orientation = orientation;
}

ToolbarLayout::EVerticalOrientation GLToolbar::get_vertical_orientation() const
{
    return m_layout.vertical_orientation;
}

void GLToolbar::set_vertical_orientation(ToolbarLayout::EVerticalOrientation orientation)
{
    m_layout.vertical_orientation = orientation;
}

void GLToolbar::set_position_mode(ToolbarLayout::EPositionMode t_position_mode)
{
    m_layout.position_mode = t_position_mode;
}

void GLToolbar::set_offset(float offset)
{
    m_layout.offset = offset;
}

void GLToolbar::set_position(float top, float left)
{
    m_layout.top = top;
    m_layout.left = left;
}

void GLToolbar::set_border(float border)
{
    m_layout.border = border;
    m_layout.dirty = true;
}

void GLToolbar::set_separator_size(float size)
{
    m_layout.separator_size = size;
    m_layout.dirty = true;
}

void GLToolbar::set_gap_size(float size)
{
    m_layout.gap_size = size;
    m_layout.dirty = true;
}

void GLToolbar::set_icons_size(float size)
{
    if (abs(m_layout.icons_size - size) < 1e-6f) {
        return;
    }
    m_layout.icons_size = size;
    m_layout.dirty = true;
    m_icons_texture_dirty = true;
}

void GLToolbar::set_text_size(float size)
{
    if (m_layout.text_size != size)
    {
        m_layout.text_size = size;
        m_layout.dirty = true;
    }
}

void GLToolbar::set_scale(float scale)
{
    if (m_layout.scale != scale) {
        m_layout.scale = scale;
        m_layout.dirty = true;
        m_icons_texture_dirty = true;
    }
}

float GLToolbar::get_scale() const
{
    return m_layout.scale;
}

void GLToolbar::set_icon_dirty()
{
    m_icons_texture_dirty = true;
}

bool GLToolbar::is_enabled() const
{
    return m_enabled;
}

void GLToolbar::set_enabled(bool enable)
{
    m_enabled = enable;
}

float GLToolbar::get_icons_size() const
{
    return m_layout.icons_size;
}

float GLToolbar::get_width() const
{
    if (m_layout.dirty)
        calc_layout();

    return m_layout.width;
}

float GLToolbar::get_height() const
{
    if (m_layout.dirty)
        calc_layout();

    return m_layout.height;
}

bool GLToolbar::add_item(const GLToolbarItem::Data& data, GLToolbarItem::EType type)
{
    const auto item = std::make_shared<GLToolbarItem>(type, data);
    m_items.emplace_back(item);
    m_layout.dirty = true;
    return true;
}

bool GLToolbar::add_separator()
{
    GLToolbarItem::Data data;
    const auto item = std::make_shared<GLToolbarItem>(GLToolbarItem::Separator, data);
    m_items.push_back(item);
    m_layout.dirty = true;
    return true;
}

bool GLToolbar::del_all_item()
{
    m_items.clear();
    m_layout.dirty = true;
    return true;
}

void GLToolbar::select_item(const std::string& name)
{
    if (is_item_disabled(name))
        return;

    for (const auto& item : m_items)
    {
        if (!item->is_disabled())
        {
            bool hover = item->is_hovered();
            item->set_state((item->get_name() == name) ? (hover ? GLToolbarItem::HoverPressed : GLToolbarItem::Pressed) : (hover ? GLToolbarItem::Hover : GLToolbarItem::Normal));
        }
    }
}

bool GLToolbar::is_item_pressed(const std::string& name) const
{
    for (const auto& item : m_items)
    {
        if (item->get_name() == name)
            return item->is_pressed();
    }

    return false;
}

bool GLToolbar::is_item_disabled(const std::string& name) const
{
    for (const auto& item : m_items)
    {
        if (item->get_name() == name)
            return item->is_disabled();
    }

    return false;
}

bool GLToolbar::is_item_visible(const std::string& name) const
{
    for (const auto& item : m_items)
    {
        if (item->get_name() == name)
            return item->is_visible();
    }

    return false;
}

bool GLToolbar::is_any_item_pressed() const
{
    for (const auto& item : m_items)
    {
        if (item->is_pressed())
            return true;
    }

    return false;
}

int GLToolbar::get_item_id(const std::string& name) const
{
    for (int i = 0; i < (int)m_items.size(); ++i)
    {
        if (m_items[i]->get_name() == name)
            return i;
    }

    return -1;
}

std::string GLToolbar::get_tooltip() const
{
    std::string tooltip;

    for (const auto& item : m_items)
    {
        if (item->is_collapsed()) {
            continue;
        }
        if (item->is_hovered())
        {
            const auto t_item_data = item->get_data();
            if (t_item_data.on_hover) {
                tooltip = t_item_data.on_hover();
                break;
            }
            else {
                tooltip = item->get_tooltip();
                if (!item->is_enabled())
                {
                    const std::string& additional_tooltip = item->get_additional_tooltip();
                    if (!additional_tooltip.empty())
                        tooltip += ":\n" + additional_tooltip;

                    break;
                }
            }
        }
    }

    return tooltip;
}

void GLToolbar::set_tooltip(int item_id, const std::string& text)
{
    if (0 <= item_id && item_id < (int)m_items.size())
        m_items[item_id]->set_tooltip(text);
}

void GLToolbar::get_additional_tooltip(int item_id, std::string& text)
{
    if (0 <= item_id && item_id < (int)m_items.size())
    {
        text = m_items[item_id]->get_additional_tooltip();
        return;
    }

    text.clear();
}

void GLToolbar::set_additional_tooltip(int item_id, const std::string& text)
{
    if (0 <= item_id && item_id < (int)m_items.size())
        m_items[item_id]->set_additional_tooltip(text);
}

int GLToolbar::get_visible_items_cnt() const
{
    int cnt = 0;
    for (unsigned int i = 0; i < (unsigned int)m_items.size(); ++i)
        if (m_items[i]->is_visible() && !m_items[i]->is_separator())
            cnt++;

    return cnt;
}

const std::shared_ptr<GLToolbarItem>& GLToolbar::get_item(const std::string& item_name) const
{
    if (m_enabled)
    {
        for (const auto& item : m_items)
        {
            if (item->get_name() == item_name)
            {
                return item;
            }
        }
    }
    static std::shared_ptr<GLToolbarItem> s_empty{ nullptr };
    return s_empty;
}

void GLToolbar::calc_layout() const
{
    switch (m_layout.type)
    {
    default:
    case ToolbarLayout::EType::Horizontal:
    {
        m_layout.width = get_width_horizontal();
        m_layout.height = get_height_horizontal();
        break;
    }
    case ToolbarLayout::EType::Vertical:
    {
        m_layout.width = get_width_vertical();
        m_layout.height = get_height_vertical();
        break;
    }
    }

    m_layout.dirty = false;
}

const std::shared_ptr<ToolbarRenderer>& GLToolbar::get_renderer() const
{
    if (!m_p_renderer || m_p_renderer->get_mode() != m_rendering_mode) {
        switch (m_rendering_mode) {
        case EToolbarRenderingMode::KeepSize:
            m_p_renderer = std::make_shared<ToolbarKeepSizeRenderer>();
            break;
        case EToolbarRenderingMode::Auto:
        default:
            m_p_renderer = std::make_shared<ToolbarAutoSizeRenderer>();
            break;
        }
        for (const auto& p_item : m_items) {
            p_item->set_collapsed(false);
        }
    }
    return m_p_renderer;
}

float GLToolbar::get_width_horizontal() const
{
    float size = 2.0f * m_layout.border;
    for (unsigned int i = 0; i < (unsigned int)m_items.size(); ++i)
    {
        if (!m_items[i]->is_visible())
            continue;

        if (m_items[i]->is_separator())
            size += m_layout.separator_size;
        else if (m_items[i]->get_type() == GLToolbarItem::EType::SeparatorLine) {
            size += ((float)m_layout.icons_size * 0.5f);
        }
        else
        {
            size += (float)m_layout.icons_size;
            if (m_items[i]->is_action_with_text())
                size += m_items[i]->get_extra_size_ratio() * m_layout.icons_size;
            if (m_items[i]->is_action_with_text_image())
                size += m_layout.text_size;
        }

        if (i < m_items.size() - 1) {
            size += m_layout.gap_size;
        }
    }

    return size * m_layout.scale;
}

float GLToolbar::get_width_vertical() const
{
    float max_extra_text_size = 0.0;
    for (unsigned int i = 0; i < (unsigned int)m_items.size(); ++i)
    {
        if (m_items[i]->is_action_with_text())
        {
            float temp_size = m_items[i]->get_extra_size_ratio() * m_layout.icons_size;

            max_extra_text_size = (temp_size > max_extra_text_size) ? temp_size : max_extra_text_size;
        }

        if (m_items[i]->is_action_with_text_image())
        {
            max_extra_text_size = m_layout.text_size;
        }
    }

    return (2.0f * m_layout.border + m_layout.icons_size + max_extra_text_size) * m_layout.scale;
}

float GLToolbar::get_height_horizontal() const
{
    return (2.0f * m_layout.border + m_layout.icons_size) * m_layout.scale;
}

float GLToolbar::get_height_vertical() const
{
    return get_main_size();
}

float GLToolbar::get_main_size() const
{
    float size = 2.0f * m_layout.border;
    for (unsigned int i = 0; i < (unsigned int)m_items.size(); ++i)
    {
        if (!m_items[i]->is_visible())
            continue;

        if (m_items[i]->is_separator())
            size += m_layout.separator_size;
        else
            size += (float)m_layout.icons_size;
    }

    if (m_items.size() > 1)
        size += ((float)m_items.size() - 1.0f) * m_layout.gap_size;

    return size * m_layout.scale;
}

bool GLToolbar::generate_icons_texture() const
{
    std::string path = resources_dir() + "/images/";
    std::vector<std::string> filenames;
    for (const auto& item : m_items) {
        const std::string icon_filename = item->get_icon_filename(m_b_dark_mode_enabled);
        if (!icon_filename.empty())
            filenames.push_back(path + icon_filename);
    }

    std::vector<std::pair<int, bool>> states;
    //1: white only, 2: gray only, 0 : normal
    //true/false: apply background or not
    if (m_type == Normal) {
        states.push_back({ 1, false }); // Normal
        states.push_back({ 0, false }); // Pressed
        states.push_back({ 2, false }); // Disabled
        states.push_back({ 0, false }); // Hover
        states.push_back({ 0, false }); // HoverPressed
        states.push_back({ 2, false }); // HoverDisabled
        states.push_back({ 0, false }); // HighlightedShown
        states.push_back({ 2, false }); // HighlightedHidden
    }
    else {
        states.push_back({ 1, false }); // Normal
        states.push_back({ 0, true });  // Pressed
        states.push_back({ 2, false }); // Disabled
        states.push_back({ 0, false }); // Hover
        states.push_back({ 1, true });  // HoverPressed
        states.push_back({ 1, false }); // HoverDisabled
        states.push_back({ 0, false }); // HighlightedShown
        states.push_back({ 1, false }); // HighlightedHidden
    }

    unsigned int sprite_size_px = (unsigned int)(m_layout.icons_size * m_layout.scale);
    //    // force even size
    //    if (sprite_size_px % 2 != 0)
    //        sprite_size_px += 1;

    bool res = m_icons_texture.load_from_svg_files_as_sprites_array(filenames, states, sprite_size_px, false);
    if (res)
        m_icons_texture_dirty = false;

    return res;
}

bool GLToolbar::update_items_visibility()
{
    bool ret = false;

    for (const auto& item : m_items) {
        ret |= item->update_visibility();
    }

    if (ret)
        m_layout.dirty = true;

    // updates separators visibility to avoid having two of them consecutive
    bool any_item_visible = false;
    for (const auto& item : m_items) {
        if (!item->is_separator())
            any_item_visible |= item->is_visible();
        else {
            item->set_visible(any_item_visible);
            any_item_visible = false;
        }
    }

    return ret;
}

bool GLToolbar::update_items_enabled_state()
{
    bool ret = false;

    for (int i = 0; i < (int)m_items.size(); ++i)
    {
        const auto& item = m_items[i];
        ret |= item->update_enabled_state();
        if ((m_pressed_toggable_id != -1) && (m_pressed_toggable_id != i)) {
            const auto& pressed_item = m_items[m_pressed_toggable_id];
            if (pressed_item->toggle_disable_others()) {
                if (item->is_enabled() && item->toggle_affectable()) {
                    ret = true;
                    item->set_state(GLToolbarItem::Disabled);
                }
            }
        }
    }

    if (ret)
        m_layout.dirty = true;

    return ret;
}

bool GLToolbar::update_items_pressed_state()
{
    bool ret = false;

    for (int i = 0; i < (int)m_items.size(); ++i)
    {
        const auto& item = m_items[i];
        if (!item) {
            continue;
        }

        if (!item->recheck_pressed()) {
            continue;
        }
        ret = true;
        if (item->is_pressed()) {
            item->set_state(GLToolbarItem::EState::Normal);
        }
        else {
            item->set_state(GLToolbarItem::EState::Pressed);
            m_pressed_toggable_id = i;
            item->set_last_action_type(GLToolbarItem::EActionType::Left);
            set_collapsed();
        }
    }

    return ret;
}

void GLToolbar::render(const Camera& t_camera)
{
    if (!m_enabled || m_items.empty())
        return;

    const auto& p_renderer = get_renderer();

    if (!p_renderer) {
        return;
    }
    p_renderer->render(*this, t_camera);
}

bool GLToolbar::update_items_state()
{
    bool ret = false;
    ret |= update_items_visibility();
    ret |= update_items_enabled_state();
    ret |= update_items_pressed_state();
    if (!is_any_item_pressed())
        m_pressed_toggable_id = -1;

    return ret;
}

void GLToolbar::set_dark_mode_enabled(bool is_enabled)
{
    if (m_b_dark_mode_enabled == is_enabled) {
        return;
    }

    m_b_dark_mode_enabled = is_enabled;
    set_icon_dirty();
}

const std::vector<std::shared_ptr<GLToolbarItem>>& GLToolbar::get_items() const
{
    return m_items;
}

const GLTexture& GLToolbar::get_icon_texture() const
{
    if (m_icons_texture_dirty) {
        generate_icons_texture();
    }
    return m_icons_texture;
}

const BackgroundTexture& GLToolbar::get_background_texture() const
{
    return m_background_texture;
}

const BackgroundTexture& GLToolbar::get_arrow_texture() const
{
    return m_arrow_texture;
}

bool GLToolbar::needs_collapsed() const
{
    const auto& p_renderer = get_renderer();
    if (!p_renderer) {
        return false;
    }
    return p_renderer->needs_collapsed();
}

void GLToolbar::toggle_collapsed()
{
    m_b_collapsed = !m_b_collapsed;
}

void GLToolbar::set_collapsed()
{
    m_b_collapsed = true;
}

bool GLToolbar::is_collapsed() const
{
    return m_b_collapsed;
}

void GLToolbar::set_collapsed_offset(uint32_t offset_in_pixel)
{
    m_layout.collapsed_offset = offset_in_pixel;
    m_layout.dirty = true;
}

uint32_t GLToolbar::get_collapsed_offset()
{
    return m_layout.collapsed_offset;
}

GLToolbar::EToolbarRenderingMode GLToolbar::get_rendering_mode()
{
    return m_rendering_mode;
}

void GLToolbar::set_rendering_mode(EToolbarRenderingMode mode)
{
    m_rendering_mode = mode;
}

void GLToolbar::render_arrow(const std::weak_ptr<GLToolbarItem>& highlighted_item)
{
    if (!m_enabled || m_items.empty()) {
        return;
    }
    const auto& p_renderer = get_renderer();
    if (!p_renderer) {
        return;
    }
    p_renderer->render_arrow(*this, highlighted_item);
}

bool GLToolbar::on_mouse(wxMouseEvent& evt, GLCanvas3D& parent)
{
    if (!m_enabled)
        return false;

    Vec2d mouse_pos((double)evt.GetX(), (double)evt.GetY());
    bool processed = false;

    // mouse anywhere
    if (!evt.Dragging() && !evt.Leaving() && !evt.Entering() && m_mouse_capture.parent != nullptr) {
        if (m_mouse_capture.any() && (evt.LeftUp() || evt.MiddleUp() || evt.RightUp())) {
            // prevents loosing selection into the scene if mouse down was done inside the toolbar and mouse up was down outside it,
            // as when switching between views
            m_mouse_capture.reset();
            return true;
        }
        m_mouse_capture.reset();
    }

    if (evt.Moving())
        update_hover_state(mouse_pos, parent);
    else if (evt.LeftUp()) {
        if (m_mouse_capture.left) {
            processed = true;
            m_mouse_capture.left = false;
        }
        else
            return false;
    }
    else if (evt.MiddleUp()) {
        if (m_mouse_capture.middle) {
            processed = true;
            m_mouse_capture.middle = false;
        }
        else
            return false;
    }
    else if (evt.RightUp()) {
        if (m_mouse_capture.right) {
            processed = true;
            m_mouse_capture.right = false;
        }
        else
            return false;
    }
    else if (evt.Dragging()) {
        if (m_mouse_capture.any())
            // if the button down was done on this toolbar, prevent from dragging into the scene
            processed = true;
        else
            return false;
    }

    int item_id = contains_mouse(mouse_pos, parent);
    if (item_id != -1) {
        // mouse inside toolbar
        if (evt.LeftDown() || evt.LeftDClick()) {
            m_mouse_capture.left = true;
            m_mouse_capture.parent = &parent;
            processed = true;
            bool rt = item_id != -2 && !m_items[item_id]->is_separator() && !m_items[item_id]->is_disabled() &&
                (m_pressed_toggable_id == -1
                    || m_items[item_id]->get_last_action_type() == GLToolbarItem::Left);
            if (!rt) {
                if (item_id >= 0 && item_id < m_items.size()) {
                    rt = !m_items[item_id]->toggle_affectable();
                }
            }
            if (!rt) {
                if (m_pressed_toggable_id >= 0 && m_pressed_toggable_id < m_items.size()) {
                    rt = !m_items[m_pressed_toggable_id]->toggle_disable_others();
                }
            }
            if (rt) {
                // mouse is inside an icon
                do_action(GLToolbarItem::Left, item_id, parent, true);
                parent.set_as_dirty();
                evt.StopPropagation();
                processed = true;
            }
        }
        else if (evt.MiddleDown()) {
            m_mouse_capture.middle = true;
            m_mouse_capture.parent = &parent;
        }
        else if (evt.RightDown()) {
            m_mouse_capture.right = true;
            m_mouse_capture.parent = &parent;
            processed = true;
            if (item_id != -2 && !m_items[item_id]->is_separator() && !m_items[item_id]->is_disabled() &&
                (m_pressed_toggable_id == -1 || m_items[item_id]->get_last_action_type() == GLToolbarItem::Right)) {
                // mouse is inside an icon
                do_action(GLToolbarItem::Right, item_id, parent, true);
                parent.set_as_dirty();
                evt.StopPropagation();
                processed = true;
            }
        }
    }

    return processed;
}

void GLToolbar::do_action(GLToolbarItem::EActionType type, int item_id, GLCanvas3D& parent, bool check_hover)
{
    if (item_id < 0 || item_id >= (int)m_items.size()) {
        return;
    }
    const auto& item = m_items[item_id];
    if (!item || item->is_separator() || item->is_disabled() || (check_hover && !item->is_hovered())) {
        return;
    }

    auto do_item_action = [this](GLToolbarItem::EActionType type, const std::shared_ptr<GLToolbarItem>& item, int item_id, GLCanvas3D& parent)->void {
        if (((type == GLToolbarItem::Right) && item->is_right_toggable()) ||
            ((type == GLToolbarItem::Left) && item->is_left_toggable()))
        {
            GLToolbarItem::EState state = item->get_state();
            if (state == GLToolbarItem::Hover)
                item->set_state(GLToolbarItem::HoverPressed);
            else if (state == GLToolbarItem::HoverPressed)
                item->set_state(GLToolbarItem::Hover);
            else if (state == GLToolbarItem::Pressed)
                item->set_state(GLToolbarItem::Normal);
            else if (state == GLToolbarItem::Normal)
                item->set_state(GLToolbarItem::Pressed);

            m_pressed_toggable_id = item->is_pressed() ? item_id : -1;
            item->reset_last_action_type();

            switch (type)
            {
            default:
            case GLToolbarItem::Left: { item->do_left_action(); break; }
            case GLToolbarItem::Right: { item->do_right_action(); break; }
            }

            parent.set_as_dirty();
        }
        else
        {
            if (m_type == Radio)
                select_item(item->get_name());
            else
                item->set_state(item->is_hovered() ? GLToolbarItem::HoverPressed : GLToolbarItem::Pressed);

            item->reset_last_action_type();
            switch (type)
            {
            default:
            case GLToolbarItem::Left: { item->do_left_action(); break; }
            case GLToolbarItem::Right: { item->do_right_action(); break; }
            }
            if (item->get_continuous_click_flag()) {
                item->set_state(GLToolbarItem::Hover);
            }
            else if ((m_type == Normal) && (item->get_state() != GLToolbarItem::Disabled) && !item->get_continuous_click_flag())
            {
                // the item may get disabled during the action, if not, set it back to normal state
                item->set_state(GLToolbarItem::Normal);
            }

            parent.set_as_dirty();
        }

        if (item->is_collapse_button()) {
            toggle_collapsed();
        }
        else {
            set_collapsed();
        }
    };
    if ((m_pressed_toggable_id != -1) && (m_pressed_toggable_id != item_id))
    {
        do_item_action(type, m_items[m_pressed_toggable_id], m_pressed_toggable_id, parent);
    }
    do_item_action(type, item, item_id, parent);
}

void GLToolbar::update_hover_state(const Vec2d& mouse_pos, GLCanvas3D& parent)
{
    if (!m_enabled)
        return;

    float inv_zoom = (float)wxGetApp().plater()->get_camera().get_inv_zoom();
    Size cnv_size = parent.get_canvas_size();
    Vec2d scaled_mouse_pos((mouse_pos(0) - 0.5 * (double)cnv_size.get_width()) * inv_zoom, (0.5 * (double)cnv_size.get_height() - mouse_pos(1)) * inv_zoom);

    for (const auto& item : m_items)
    {
        if (!item->is_visible())
            continue;

        if (item->is_separator()) {
            continue;
        }

        if (item->get_type() == GLToolbarItem::EType::SeparatorLine) {
            continue;
        }
        bool inside = item->is_inside(scaled_mouse_pos);
        GLToolbarItem::EState state = item->get_state();
        switch (state)
        {
        case GLToolbarItem::Normal:
        {
            if (inside)
            {
                item->set_state(GLToolbarItem::Hover);
                parent.set_as_dirty();
            }

            break;
        }
        case GLToolbarItem::Hover:
        {
            if (!inside)
            {
                item->set_state(GLToolbarItem::Normal);
                parent.set_as_dirty();
            }

            break;
        }
        case GLToolbarItem::Pressed:
        {
            if (inside)
            {
                item->set_state(GLToolbarItem::HoverPressed);
                parent.set_as_dirty();
            }

            break;
        }
        case GLToolbarItem::HoverPressed:
        {
            if (!inside)
            {
                item->set_state(GLToolbarItem::Pressed);
                parent.set_as_dirty();
            }

            break;
        }
        case GLToolbarItem::Disabled:
        {
            if (inside)
            {
                item->set_state(GLToolbarItem::HoverDisabled);
                parent.set_as_dirty();
            }

            break;
        }
        case GLToolbarItem::HoverDisabled:
        {
            if (!inside)
            {
                item->set_state(GLToolbarItem::Disabled);
                parent.set_as_dirty();
            }

            break;
        }
        default:
        {
            break;
        }
        }
    }
}

int GLToolbar::contains_mouse(const Vec2d& mouse_pos, const GLCanvas3D& parent) const
{
    if (!m_enabled)
        return -1;

    for (size_t i = 0; i < m_items.size(); ++i) {
        if (m_items[i]->is_collapsed()) {
            continue;
        }
        if (m_items[i]->is_hovered()) {
            return i;
        }
    }

    return -1;
}

//BBS: GUI refactor: GLToolbar
int GLToolbar::generate_button_text_textures(wxFont& font)
{
    int ret = 0;

    for (int i = 0; i < (int)m_items.size(); ++i)
    {
        const auto& item = m_items[i];

        if (item->is_action_with_text())
        {
            ret |= item->generate_texture(font);
        }

        if (item->is_action_with_text_image())
        {
            ret |= item->generate_texture(font);
        }
    }

    return ret;
}

int GLToolbar::generate_image_textures()
{
    int ret = 0;
    for (int i = 0; i < (int)m_items.size(); ++i)
    {
        const auto& item = m_items[i];
        if (item->is_action_with_text_image()) {
            ret |= item->generate_image_texture();
        }
    }
    return ret;
}

//BBS: GUI refactor: GLToolbar
float GLToolbar::get_scaled_icon_size()
{
    return m_layout.icons_size * m_layout.scale;
}

ToolbarRenderer::ToolbarRenderer()
{
}

ToolbarRenderer::~ToolbarRenderer()
{
}

bool ToolbarRenderer::needs_collapsed() const
{
    return false;
}

ToolbarAutoSizeRenderer::ToolbarAutoSizeRenderer()
{
}

ToolbarAutoSizeRenderer::~ToolbarAutoSizeRenderer()
{
}

void ToolbarAutoSizeRenderer::render(const GLToolbar& t_toolbar, const Camera& t_camera)
{
    const auto& t_layout = t_toolbar.get_layout();
    switch (t_layout.type)
    {
    default:
    case ToolbarLayout::EType::Horizontal: { render_horizontal(t_toolbar, t_camera); break; }
    case ToolbarLayout::EType::Vertical: { render_vertical(t_toolbar, t_camera); break; }
    }
}

GLToolbar::EToolbarRenderingMode ToolbarAutoSizeRenderer::get_mode() const
{
    return GLToolbar::EToolbarRenderingMode::Auto;
}

void ToolbarAutoSizeRenderer::render_horizontal(const GLToolbar& t_toolbar, const Camera& t_camera)
{
    const auto& t_layout = t_toolbar.get_layout();
    float inv_zoom = (float)t_camera.get_inv_zoom();
    float factor = inv_zoom * t_layout.scale;

    float scaled_icons_size = t_layout.icons_size * factor;
    float scaled_separator_size = t_layout.separator_size * factor;
    float scaled_gap_size = t_layout.gap_size * factor;
    float scaled_border = t_layout.border * factor;
    float scaled_width = t_toolbar.get_width() * inv_zoom;
    float scaled_height = t_toolbar.get_height() * inv_zoom;

    float separator_stride = scaled_separator_size + scaled_gap_size;
    float icon_stride = scaled_icons_size + scaled_gap_size;

    float left = 0.0f;
    float top = 0.0f;
    calculate_position(t_toolbar, t_camera, left, top);
    float right = left + scaled_width;
    float bottom = top - scaled_height;

    render_background(t_toolbar, left, top, right, bottom, scaled_border);

    left += scaled_border;
    top -= scaled_border;

    // renders icons
    const auto& t_items = t_toolbar.get_items();
    const auto& t_icon_texture = t_toolbar.get_icon_texture();
    for (const auto& item : t_items)
    {
        if (!item->is_visible())
            continue;

        if (item->is_separator())
            left += separator_stride;
        else
        {
            if (!item->is_action_with_text_image()) {
                unsigned int tex_id = t_icon_texture.get_id();
                int tex_width = t_icon_texture.get_width();
                int tex_height = t_icon_texture.get_height();
                if ((tex_id == 0) || (tex_width <= 0) || (tex_height <= 0))
                    return;
                item->render_rect[0] = left;
                item->render_rect[1] = left + scaled_icons_size;
                item->render_rect[2] = top - scaled_icons_size;
                item->render_rect[3] = top;
                item->render(tex_id, (unsigned int)tex_width, (unsigned int)tex_height, (unsigned int)(t_layout.icons_size * t_layout.scale), t_toolbar.get_height());
            }
            //BBS: GUI refactor: GLToolbar
            if (item->is_action_with_text())
            {
                float scaled_text_size = item->get_extra_size_ratio() * scaled_icons_size;
                item->render_rect[0] = left + scaled_icons_size;
                item->render_rect[1] = left + scaled_icons_size + scaled_text_size;
                item->render_rect[2] = top - scaled_icons_size;
                item->render_rect[3] = top;
                item->render_text();
                left += scaled_text_size;
            }
            if (item->get_type() == GLToolbarItem::EType::SeparatorLine) {
                left += (icon_stride - 0.5f * scaled_icons_size);
            }
            else {
                left += icon_stride;
            }
        }
    }
}

void ToolbarAutoSizeRenderer::render_vertical(const GLToolbar& t_toolbar, const Camera& t_camera)
{
    const auto& t_layout = t_toolbar.get_layout();
    float inv_zoom = (float)wxGetApp().plater()->get_camera().get_inv_zoom();
    float factor = inv_zoom * t_layout.scale;

    float scaled_icons_size = t_layout.icons_size * factor;
    float scaled_separator_size = t_layout.separator_size * factor;
    float scaled_gap_size = t_layout.gap_size * factor;
    float scaled_border = t_layout.border * factor;
    float scaled_width = t_toolbar.get_width() * inv_zoom;
    float scaled_height = t_toolbar.get_height() * inv_zoom;

    float separator_stride = scaled_separator_size + scaled_gap_size;
    float icon_stride = scaled_icons_size + scaled_gap_size;

    float left = 0.0f;
    float top = 0.0f;
    calculate_position(t_toolbar, t_camera, left, top);
    float right = left + scaled_width;
    float bottom = top - scaled_height;

    render_background(t_toolbar, left, top, right, bottom, scaled_border);

    left += scaled_border;
    top -= scaled_border;

    // renders icons
    const auto& t_items = t_toolbar.get_items();
    const auto& t_icon_texture = t_toolbar.get_icon_texture();
    for (const auto& item : t_items) {
        if (!item->is_visible())
            continue;

        if (item->is_separator())
            top -= separator_stride;
        else {
            unsigned int tex_id;
            int tex_width, tex_height;
            if (item->is_action_with_text_image()) {
                float scaled_text_size = t_layout.text_size * factor;
                float scaled_text_width = item->get_extra_size_ratio() * scaled_icons_size;
                float scaled_text_border = 2.5 * factor;
                float scaled_text_height = scaled_icons_size / 2.0f;
                item->render_rect[0] = left;
                item->render_rect[1] = left + scaled_text_size;
                item->render_rect[2] = top - scaled_text_border - scaled_text_height;
                item->render_rect[3] = top - scaled_text_border;
                item->render_text();

                float image_left = left + scaled_text_size;
                const auto& item_data = item->get_data();
                tex_id = item_data.image_texture.get_id();
                tex_width = item_data.image_texture.get_width();
                tex_height = item_data.image_texture.get_height();
                if ((tex_id == 0) || (tex_width <= 0) || (tex_height <= 0))
                    return;
                item->render_image(tex_id, image_left, image_left + scaled_icons_size, top - scaled_icons_size, top, (unsigned int)tex_width, (unsigned int)tex_height, (unsigned int)(t_layout.icons_size * t_layout.scale));
            }
            else {
                tex_id = t_icon_texture.get_id();
                tex_width = t_icon_texture.get_width();
                tex_height = t_icon_texture.get_height();
                if ((tex_id == 0) || (tex_width <= 0) || (tex_height <= 0))
                    return;
                item->render_rect[0] = left;
                item->render_rect[1] = left + scaled_icons_size;
                item->render_rect[2] = top - scaled_icons_size;
                item->render_rect[3] = top;
                item->render(tex_id, (unsigned int)tex_width, (unsigned int)tex_height, (unsigned int)(t_layout.icons_size * t_layout.scale), t_toolbar.get_width());
                //BBS: GUI refactor: GLToolbar
            }
            if (item->is_action_with_text())
            {
                float scaled_text_width = item->get_extra_size_ratio() * scaled_icons_size;
                float scaled_text_height = scaled_icons_size;

                item->render_rect[0] = left + scaled_icons_size;
                item->render_rect[1] = left + scaled_icons_size + scaled_text_width;
                item->render_rect[2] = top - scaled_text_height;
                item->render_rect[3] = top;
                item->render_text();
            }
            top -= icon_stride;
            if (item->get_type() == GLToolbarItem::EType::SeparatorLine) {
                top -= (icon_stride - 0.5f * scaled_icons_size);
            }
            else {
                top -= icon_stride;
            }
        }
    }
}

void ToolbarAutoSizeRenderer::render_background(const GLToolbar& t_toolbar, float left, float top, float right, float bottom, float border) const
{
    const auto& t_background_texture = t_toolbar.get_background_texture();
    unsigned int tex_id = t_background_texture.texture.get_id();
    float tex_width = (float)t_background_texture.texture.get_width();
    float tex_height = (float)t_background_texture.texture.get_height();
    if ((tex_id != 0) && (tex_width > 0) && (tex_height > 0))
    {
        float inv_tex_width = (tex_width != 0.0f) ? 1.0f / tex_width : 0.0f;
        float inv_tex_height = (tex_height != 0.0f) ? 1.0f / tex_height : 0.0f;

        float internal_left = left + border;
        float internal_right = right - border;
        float internal_top = top - border;
        float internal_bottom = bottom + border;

        float left_uv = 0.0f;
        float right_uv = 1.0f;
        float top_uv = 1.0f;
        float bottom_uv = 0.0f;

        float internal_left_uv = (float)t_background_texture.metadata.left * inv_tex_width;
        float internal_right_uv = 1.0f - (float)t_background_texture.metadata.right * inv_tex_width;
        float internal_top_uv = 1.0f - (float)t_background_texture.metadata.top * inv_tex_height;
        float internal_bottom_uv = (float)t_background_texture.metadata.bottom * inv_tex_height;

        const auto& t_layout = t_toolbar.get_layout();
        // top-left corner
        if ((t_layout.horizontal_orientation == ToolbarLayout::HO_Left) || (t_layout.vertical_orientation == ToolbarLayout::VO_Top))
            GLTexture::render_sub_texture(tex_id, left, internal_left, internal_top, top, { { internal_left_uv, internal_bottom_uv }, { internal_right_uv, internal_bottom_uv }, { internal_right_uv, internal_top_uv }, { internal_left_uv, internal_top_uv } });
        else
            GLTexture::render_sub_texture(tex_id, left, internal_left, internal_top, top, { { left_uv, internal_top_uv }, { internal_left_uv, internal_top_uv }, { internal_left_uv, top_uv }, { left_uv, top_uv } });

        // top edge
        if (t_layout.vertical_orientation == ToolbarLayout::VO_Top)
            GLTexture::render_sub_texture(tex_id, internal_left, internal_right, internal_top, top, { { internal_left_uv, internal_bottom_uv }, { internal_right_uv, internal_bottom_uv }, { internal_right_uv, internal_top_uv }, { internal_left_uv, internal_top_uv } });
        else
            GLTexture::render_sub_texture(tex_id, internal_left, internal_right, internal_top, top, { { internal_left_uv, internal_top_uv }, { internal_right_uv, internal_top_uv }, { internal_right_uv, top_uv }, { internal_left_uv, top_uv } });

        // top-right corner
        if ((t_layout.horizontal_orientation == ToolbarLayout::HO_Right) || (t_layout.vertical_orientation == ToolbarLayout::VO_Top))
            GLTexture::render_sub_texture(tex_id, internal_right, right, internal_top, top, { { internal_left_uv, internal_bottom_uv }, { internal_right_uv, internal_bottom_uv }, { internal_right_uv, internal_top_uv }, { internal_left_uv, internal_top_uv } });
        else
            GLTexture::render_sub_texture(tex_id, internal_right, right, internal_top, top, { { internal_right_uv, internal_top_uv }, { right_uv, internal_top_uv }, { right_uv, top_uv }, { internal_right_uv, top_uv } });

        // center-left edge
        if (t_layout.horizontal_orientation == ToolbarLayout::HO_Left)
            GLTexture::render_sub_texture(tex_id, left, internal_left, internal_bottom, internal_top, { { internal_left_uv, internal_bottom_uv }, { internal_right_uv, internal_bottom_uv }, { internal_right_uv, internal_top_uv }, { internal_left_uv, internal_top_uv } });
        else
            GLTexture::render_sub_texture(tex_id, left, internal_left, internal_bottom, internal_top, { { left_uv, internal_bottom_uv }, { internal_left_uv, internal_bottom_uv }, { internal_left_uv, internal_top_uv }, { left_uv, internal_top_uv } });

        // center
        GLTexture::render_sub_texture(tex_id, internal_left, internal_right, internal_bottom, internal_top, { { internal_left_uv, internal_bottom_uv }, { internal_right_uv, internal_bottom_uv }, { internal_right_uv, internal_top_uv }, { internal_left_uv, internal_top_uv } });

        // center-right edge
        if (t_layout.horizontal_orientation == ToolbarLayout::HO_Right)
            GLTexture::render_sub_texture(tex_id, internal_right, right, internal_bottom, internal_top, { { internal_left_uv, internal_bottom_uv }, { internal_right_uv, internal_bottom_uv }, { internal_right_uv, internal_top_uv }, { internal_left_uv, internal_top_uv } });
        else
            GLTexture::render_sub_texture(tex_id, internal_right, right, internal_bottom, internal_top, { { internal_right_uv, internal_bottom_uv }, { right_uv, internal_bottom_uv }, { right_uv, internal_top_uv }, { internal_right_uv, internal_top_uv } });

        // bottom-left corner
        if ((t_layout.horizontal_orientation == ToolbarLayout::HO_Left) || (t_layout.vertical_orientation == ToolbarLayout::VO_Bottom))
            GLTexture::render_sub_texture(tex_id, left, internal_left, bottom, internal_bottom, { { internal_left_uv, internal_bottom_uv }, { internal_right_uv, internal_bottom_uv }, { internal_right_uv, internal_top_uv }, { internal_left_uv, internal_top_uv } });
        else
            GLTexture::render_sub_texture(tex_id, left, internal_left, bottom, internal_bottom, { { left_uv, bottom_uv }, { internal_left_uv, bottom_uv }, { internal_left_uv, internal_bottom_uv }, { left_uv, internal_bottom_uv } });

        // bottom edge
        if (t_layout.vertical_orientation == ToolbarLayout::VO_Bottom)
            GLTexture::render_sub_texture(tex_id, internal_left, internal_right, bottom, internal_bottom, { { internal_left_uv, internal_bottom_uv }, { internal_right_uv, internal_bottom_uv }, { internal_right_uv, internal_top_uv }, { internal_left_uv, internal_top_uv } });
        else
            GLTexture::render_sub_texture(tex_id, internal_left, internal_right, bottom, internal_bottom, { { internal_left_uv, bottom_uv }, { internal_right_uv, bottom_uv }, { internal_right_uv, internal_bottom_uv }, { internal_left_uv, internal_bottom_uv } });

        // bottom-right corner
        if ((t_layout.horizontal_orientation == ToolbarLayout::HO_Right) || (t_layout.vertical_orientation == ToolbarLayout::VO_Bottom))
            GLTexture::render_sub_texture(tex_id, internal_right, right, bottom, internal_bottom, { { internal_left_uv, internal_bottom_uv }, { internal_right_uv, internal_bottom_uv }, { internal_right_uv, internal_top_uv }, { internal_left_uv, internal_top_uv } });
        else
            GLTexture::render_sub_texture(tex_id, internal_right, right, bottom, internal_bottom, { { internal_right_uv, bottom_uv }, { right_uv, bottom_uv }, { right_uv, internal_bottom_uv }, { internal_right_uv, internal_bottom_uv } });
    }
}

void ToolbarAutoSizeRenderer::calculate_position(const GLToolbar& t_toolbar, const Camera& t_camera, float& left, float& top)
{
    const auto& t_layout = t_toolbar.get_layout();
    switch (t_layout.position_mode) {
    case ToolbarLayout::EPositionMode::TopLeft:
    {
        float inv_zoom = (float)t_camera.get_inv_zoom();
        const auto& t_viewport = t_camera.get_viewport();
        top = 0.5f * (float)t_viewport[3] * inv_zoom;
        left = -0.5f * (float)t_viewport[2] * inv_zoom;

        break;
    }
    case ToolbarLayout::EPositionMode::TopMiddle:
    {
        float inv_zoom = (float)t_camera.get_inv_zoom();
        const auto& t_viewport = t_camera.get_viewport();
        const auto cnv_width = t_viewport[2];
        const auto cnv_height = t_viewport[3];
        top = 0.5f * (float)cnv_height * inv_zoom;
        left = -0.5f * (float)cnv_width * inv_zoom;
        const auto final_width = t_toolbar.get_width() + t_layout.offset;
        if (cnv_width < final_width) {
            left += (t_layout.offset * inv_zoom);
        }
        else {
            const float offset = (cnv_width - final_width) / 2.f;
            left += (offset + t_layout.offset) * inv_zoom;
        }

        break;
    }
    case ToolbarLayout::EPositionMode::Custom:
    default:
    {
        left = t_layout.left;
        top = t_layout.top;
        break;
    }
    }
}

void ToolbarAutoSizeRenderer::render_arrow(const GLToolbar& t_toolbar, const std::weak_ptr<GLToolbarItem>& highlighted_item)
{
    const auto p_item = highlighted_item.lock();
    if (!p_item) {
        return;
    }
    const auto& t_arrow_texture = t_toolbar.get_arrow_texture();
    // arrow texture not initialized
    if (t_arrow_texture.texture.get_id() == 0)
        return;

    const auto& t_layout = t_toolbar.get_layout();
    float inv_zoom = (float)wxGetApp().plater()->get_camera().get_inv_zoom();
    float factor = inv_zoom * t_layout.scale;

    float scaled_icons_size = t_layout.icons_size * factor;
    float scaled_separator_size = t_layout.separator_size * factor;
    float scaled_gap_size = t_layout.gap_size * factor;
    float border = t_layout.border * factor;

    float separator_stride = scaled_separator_size + scaled_gap_size;
    float icon_stride = scaled_icons_size + scaled_gap_size;

    float left = t_layout.left;
    float top = t_layout.top - icon_stride;

    bool found = false;
    const auto& t_items = t_toolbar.get_items();
    for (const auto& item : t_items) {
        if (!item->is_visible())
            continue;

        if (item->is_separator())
            left += separator_stride;
        else {
            if (item->get_name() == p_item->get_name()) {
                found = true;
                break;
            }
            if (item->get_type() == GLToolbarItem::EType::SeparatorLine) {
                left += (icon_stride - 0.5f * scaled_icons_size);
            }
            else {
                left += icon_stride;
            }
        }
    }
    if (!found)
        return;

    left += border;
    top -= separator_stride;
    float right = left + scaled_icons_size;

    unsigned int tex_id = t_arrow_texture.texture.get_id();
    // arrow width and height
    float arr_tex_width = (float)t_arrow_texture.texture.get_width();
    float arr_tex_height = (float)t_arrow_texture.texture.get_height();
    if ((tex_id != 0) && (arr_tex_width > 0) && (arr_tex_height > 0)) {
        float inv_tex_width = (arr_tex_width != 0.0f) ? 1.0f / arr_tex_width : 0.0f;
        float inv_tex_height = (arr_tex_height != 0.0f) ? 1.0f / arr_tex_height : 0.0f;

        float internal_left = left + border - scaled_icons_size * 1.5f; // add scaled_icons_size for huge arrow
        float internal_right = right - border + scaled_icons_size * 1.5f;
        float internal_top = top - border;
        // bottom is not moving and should be calculated from arrow texture sides ratio
        float arrow_sides_ratio = (float)t_arrow_texture.texture.get_height() / (float)t_arrow_texture.texture.get_width();
        float internal_bottom = internal_top - (internal_right - internal_left) * arrow_sides_ratio;

        float internal_left_uv = (float)t_arrow_texture.metadata.left * inv_tex_width;
        float internal_right_uv = 1.0f - (float)t_arrow_texture.metadata.right * inv_tex_width;
        float internal_top_uv = 1.0f - (float)t_arrow_texture.metadata.top * inv_tex_height;
        float internal_bottom_uv = (float)t_arrow_texture.metadata.bottom * inv_tex_height;

        GLTexture::render_sub_texture(tex_id, internal_left, internal_right, internal_bottom, internal_top, { { internal_left_uv, internal_top_uv }, { internal_right_uv, internal_top_uv }, { internal_right_uv, internal_bottom_uv }, { internal_left_uv, internal_bottom_uv } });
    }
}

ToolbarKeepSizeRenderer::ToolbarKeepSizeRenderer()
{
}

ToolbarKeepSizeRenderer::~ToolbarKeepSizeRenderer()
{
}

void ToolbarKeepSizeRenderer::render(const GLToolbar& t_toolbar, const Camera& t_camera)
{
    const auto& t_layout = t_toolbar.get_layout();

    const auto& t_viewport = t_camera.get_viewport();
    const auto canvas_width = t_viewport[2];
    const auto canvas_height = t_viewport[3];

    const auto toolbar_width = t_toolbar.get_width();
    const auto toolbar_height = t_toolbar.get_height();
    const auto toolbar_offset = t_layout.offset >= canvas_width ? 0.0f : t_layout.offset;

    float final_toolbar_width = toolbar_width;
    float final_toolbar_height = toolbar_height;

    const auto final_canvas_width = canvas_width - toolbar_offset;

    float collapse_width = 0.0f;
    recalculate_item_pos(t_toolbar, t_camera, final_toolbar_width, final_toolbar_height, collapse_width);

    if (t_toolbar.is_collapsed()) {
        final_toolbar_height = toolbar_height;
    }
    float inv_zoom = (float)t_camera.get_inv_zoom();
    float factor = inv_zoom * t_layout.scale;
    float scaled_border = t_layout.border * factor;

    float left = 0.0f;
    float top = 0.0f;
    calculate_position(t_toolbar, t_camera, final_toolbar_width, final_toolbar_height, left, top);

    if (t_layout.collapsed_offset < 1e-6f || t_toolbar.is_collapsed()) {
        float scaled_width = final_toolbar_width * inv_zoom;
        float scaled_height = final_toolbar_height * inv_zoom;
        float right = left + scaled_width;
        float bottom = top - scaled_height;

        render_background(t_toolbar, left, top, right, bottom, scaled_border);
    }
    else {
        float scaled_width = final_toolbar_width * inv_zoom;
        float scaled_height = toolbar_height * inv_zoom;
        float right = left + scaled_width;
        float bottom = top - scaled_height;

        render_background(t_toolbar, left, top, right, bottom, scaled_border);

        const auto others_height = final_toolbar_height - toolbar_height;
        if (others_height > 1e-6f) {
            scaled_width = collapse_width * inv_zoom;
            right = left + scaled_width;
            top = bottom - t_layout.collapsed_offset * factor;
            scaled_height = others_height * inv_zoom;
            bottom = top - scaled_height;
            render_background(t_toolbar, left, top, right, bottom, scaled_border);
        }
    }

    // renders icons
    const auto& t_icon_texture = t_toolbar.get_icon_texture();
    unsigned int tex_id = t_icon_texture.get_id();
    int tex_width = t_icon_texture.get_width();
    int tex_height = t_icon_texture.get_height();
    if ((tex_id == 0) || (tex_width <= 0) || (tex_height <= 0))
        return;

    const auto& t_items = t_toolbar.get_items();
    for (size_t i = 0; i < t_items.size(); ++i) {
        const auto& current_item = t_items[i];
        current_item->override_render_rect = m_p_override_render_rect;
        if (current_item->is_action() || current_item->get_type() == GLToolbarItem::EType::SeparatorLine) {
            const bool b_filp_v = !t_toolbar.is_collapsed() && current_item->is_collapse_button();
            current_item->render(tex_id, (unsigned int)tex_width, (unsigned int)tex_height, (unsigned int)(t_layout.icons_size * t_layout.scale), t_toolbar.get_height(), b_filp_v);
        }
        //BBS: GUI refactor: GLToolbar
        if (current_item->is_action_with_text())
        {
            current_item->render_text();
        }
    }
}

GLToolbar::EToolbarRenderingMode ToolbarKeepSizeRenderer::get_mode() const
{
    return GLToolbar::EToolbarRenderingMode::KeepSize;
}

void ToolbarKeepSizeRenderer::render_arrow(const GLToolbar& t_toolbar, const std::weak_ptr<GLToolbarItem>& highlighted_item)
{
}

bool ToolbarKeepSizeRenderer::needs_collapsed() const
{
    return m_b_needs_collapsed;
}

void ToolbarKeepSizeRenderer::render_horizontal(const GLToolbar& t_toolbar)
{
}

void ToolbarKeepSizeRenderer::render_vertical(const GLToolbar& t_toolbar)
{
}

void ToolbarKeepSizeRenderer::render_background(const GLToolbar& t_toolbar, float left, float top, float right, float bottom, float border) const
{
    const auto& t_background_texture = t_toolbar.get_background_texture();
    unsigned int tex_id = t_background_texture.texture.get_id();
    if (tex_id < 0) {
        return;
    }

    float tex_width = (float)t_background_texture.texture.get_width();
    if (tex_width <= 0) {
        return;
    }

    float tex_height = (float)t_background_texture.texture.get_height();
    if (tex_height <= 0) {
        return;
    }

    float inv_tex_width = (tex_width != 0.0f) ? 1.0f / tex_width : 0.0f;
    float inv_tex_height = (tex_height != 0.0f) ? 1.0f / tex_height : 0.0f;

    float internal_left = left + border;
    float internal_right = right - border;
    float internal_top = top - border;
    float internal_bottom = bottom + border;

    float left_uv = 0.0f;
    float right_uv = 1.0f;
    float top_uv = 1.0f;
    float bottom_uv = 0.0f;

    float internal_left_uv = (float)t_background_texture.metadata.left * inv_tex_width;
    float internal_right_uv = 1.0f - (float)t_background_texture.metadata.right * inv_tex_width;
    float internal_top_uv = 1.0f - (float)t_background_texture.metadata.top * inv_tex_height;
    float internal_bottom_uv = (float)t_background_texture.metadata.bottom * inv_tex_height;

    const auto& t_layout = t_toolbar.get_layout();
    GLTexture::render_sub_texture(tex_id, left, right, bottom, top, { { internal_left_uv, internal_bottom_uv }, { internal_right_uv, internal_bottom_uv }, { internal_right_uv, internal_top_uv }, { internal_left_uv, internal_top_uv } });
}

void ToolbarKeepSizeRenderer::calculate_position(const GLToolbar& t_toolbar, const Camera& t_camera, float toolbar_width, float toolbar_height, float& left, float& top)
{
    const auto& t_layout = t_toolbar.get_layout();
    switch (t_layout.position_mode) {
    case ToolbarLayout::EPositionMode::TopLeft:
    {
        float inv_zoom = (float)t_camera.get_inv_zoom();
        const auto& t_viewport = t_camera.get_viewport();
        top = 0.5f * (float)t_viewport[3] * inv_zoom;
        left = -0.5f * (float)t_viewport[2] * inv_zoom;

        break;
    }
    case ToolbarLayout::EPositionMode::TopMiddle:
    {
        float inv_zoom = (float)t_camera.get_inv_zoom();
        const auto& t_viewport = t_camera.get_viewport();
        const auto cnv_width = t_viewport[2];
        const auto cnv_height = t_viewport[3];
        top = 0.5f * (float)cnv_height * inv_zoom;
        left = -0.5f * (float)cnv_width * inv_zoom;
        const auto final_width = toolbar_width + t_layout.offset;
        if (cnv_width < final_width) {
            left += (t_layout.offset * inv_zoom);
        }
        else {
            const float offset = (cnv_width - final_width) / 2.f;
            left += (offset + t_layout.offset) * inv_zoom;
        }

        break;
    }
    case ToolbarLayout::EPositionMode::Custom:
    default:
    {
        left = t_layout.left;
        top = t_layout.top;
        break;
    }
    }
}

void ToolbarKeepSizeRenderer::recalculate_item_pos(const GLToolbar& t_toolbar, const Camera& t_camera, float& final_toolbar_width, float& final_toolbar_height, float& collapse_width)
{
    const auto& t_layout = t_toolbar.get_layout();

    const auto& t_viewport = t_camera.get_viewport();
    const auto canvas_width = t_viewport[2];
    const auto canvas_height = t_viewport[3];

    const auto toolbar_width = t_toolbar.get_width();
    const auto toolbar_height = t_toolbar.get_height();
    const auto toolbar_offset = t_layout.offset >= canvas_width ? 0.0f : t_layout.offset;

    const auto final_canvas_width = canvas_width - toolbar_offset;

    m_b_needs_collapsed = false;
    const auto& t_items = t_toolbar.get_items();
    m_indices_to_draw.clear();

    float total_collapse_item_width = 2.0f * t_layout.border;
    for (size_t i = 0; i < t_items.size(); ++i)
    {
        const auto& current_item = t_items[i];

        if (!current_item) {
            continue;
        }

        if (!current_item->is_visible())
            continue;

        current_item->set_collapsed(false);
        m_indices_to_draw.emplace_back(i);
    }
    
    float collapse_button_width = 0.0f;
    
    if (final_canvas_width - toolbar_width < 1e-6f) {
        float current_width = 2.0f * t_layout.border;
        float uncollapsible_width = 2.0f * t_layout.border;
        std::vector<size_t> t_other_visible_indices;
        std::vector<size_t> t_uncollapsible_indices;
        t_other_visible_indices.reserve(10);
        t_uncollapsible_indices.reserve(10);
        for (size_t i = 0; i < m_indices_to_draw.size(); ++i)
        {
            const auto& current_index = m_indices_to_draw[i];
            const auto& current_item = t_items[current_index];

            if (!current_item) {
                continue;
            }

            if (!current_item->is_visible())
                continue;

            if (current_item->is_collapsible()) {
                t_other_visible_indices.emplace_back(i);
            }
            else {
                t_uncollapsible_indices.emplace_back(i);
                float item_width = 0.0f;
                if (current_item->is_separator())
                    item_width += t_layout.separator_size;
                else if (current_item->get_type() == GLToolbarItem::EType::SeparatorLine) {
                    item_width += ((float)t_layout.icons_size * 0.5f);
                }
                else
                {
                    item_width += (float)t_layout.icons_size;
                    if (current_item->is_action_with_text())
                        item_width += current_item->get_extra_size_ratio() * t_layout.icons_size;
                    if (current_item->is_action_with_text_image())
                        item_width += t_layout.text_size;
                }

                if (i < m_indices_to_draw.size() - 1) {
                    item_width += t_layout.gap_size;
                }
                
                if (current_item->is_collapse_button()) {
                    collapse_button_width = item_width;
                    continue;
                }
                
                uncollapsible_width += item_width;
            }
        }

        current_width = uncollapsible_width;
        size_t final_index = 0;

        if (current_width * t_layout.scale - final_canvas_width < 1e-6f) {
            for (; final_index < t_other_visible_indices.size(); ++final_index) {
                const auto& item_index = m_indices_to_draw[t_other_visible_indices[final_index]];
                const auto& current_item = t_items[item_index];
                float item_width = 0.0f;
                if (current_item->is_separator())
                    item_width += t_layout.separator_size;
                else if (current_item->get_type() == GLToolbarItem::EType::SeparatorLine) {
                    item_width += ((float)t_layout.icons_size * 0.5f);
                }
                else
                {
                    item_width += (float)t_layout.icons_size;
                    if (current_item->is_action_with_text())
                        item_width += current_item->get_extra_size_ratio() * t_layout.icons_size;
                    if (current_item->is_action_with_text_image())
                        item_width += t_layout.text_size;
                }

                if (item_index < m_indices_to_draw.size() - 1) {
                    item_width += t_layout.gap_size;
                }

                if ((current_width + item_width) * t_layout.scale - final_canvas_width > 1e-6f) {
                    break;
                }
                current_width += item_width;
            }
        }

        if (final_index < t_other_visible_indices.size()) {
            current_width = std::max(GLToolbar::Default_Icons_Size + 2.0f * t_layout.border, current_width + collapse_button_width);

            final_toolbar_width = current_width * t_layout.scale;
            while (final_canvas_width - final_toolbar_width < 1e-6f) {
                if (final_index < 1) {
                    break;
                }
                const auto item_index = m_indices_to_draw[t_other_visible_indices[final_index]];
                float item_width = 0.0f;
                const auto& current_item = t_items[item_index];
                if (current_item->get_type() == GLToolbarItem::EType::SeparatorLine) {
                    item_width += ((float)t_layout.icons_size * 0.5f);
                    item_width += t_layout.gap_size;
                }
                else if (current_item->is_separator()) {
                    item_width += t_layout.separator_size;
                    item_width += t_layout.gap_size;
                }
                else
                {
                    item_width += (float)t_layout.icons_size;
                    if (current_item->is_action_with_text())
                        item_width += current_item->get_extra_size_ratio() * t_layout.icons_size;
                    if (current_item->is_action_with_text_image())
                        item_width += t_layout.text_size;
                    item_width += t_layout.gap_size;
                }
                
                final_toolbar_width = final_toolbar_width - (item_width * t_layout.scale);

                --final_index;
            }
            m_b_needs_collapsed = true;
        }

        if (m_b_needs_collapsed) {
            if (final_index < t_other_visible_indices.size()) {
                std::vector<size_t> temp_indices;
                temp_indices.reserve(m_indices_to_draw.size());
                for (size_t i = 0; i < final_index; ++i) {
                    const auto item_index = m_indices_to_draw[t_other_visible_indices[i]];
                    temp_indices.emplace_back(item_index);
                }
                for (size_t i = 0; i < t_uncollapsible_indices.size(); ++i) {
                    const auto item_index = m_indices_to_draw[t_uncollapsible_indices[i]];
                    temp_indices.emplace_back(item_index);
                }
                for (size_t i = final_index; i < t_other_visible_indices.size(); ++i) {
                    const auto item_index = m_indices_to_draw[t_other_visible_indices[i]];
                    const auto& p_item = t_items[item_index];
                    temp_indices.emplace_back(item_index);
                    p_item->set_collapsed(t_toolbar.is_collapsed());

                    if (p_item->is_separator()) {
                        total_collapse_item_width += t_layout.separator_size;
                        total_collapse_item_width += t_layout.gap_size;
                    }
                    else if (p_item->is_action() || p_item->get_type() == GLToolbarItem::EType::SeparatorLine) {
                        if (p_item->is_action()) {
                            total_collapse_item_width += (float)t_layout.icons_size;
                            total_collapse_item_width += t_layout.gap_size;
                        }
                        else {
                            total_collapse_item_width += ((float)t_layout.icons_size * 0.5f);
                            total_collapse_item_width += t_layout.gap_size;
                        }
                    }
                    else if (p_item->is_action_with_text())
                    {
                        total_collapse_item_width += p_item->get_extra_size_ratio() * t_layout.icons_size;
                        total_collapse_item_width += t_layout.gap_size;
                    }
                }
                m_indices_to_draw = std::move(temp_indices);
            }
        }
    }

    float inv_zoom = (float)t_camera.get_inv_zoom();
    float factor = inv_zoom * t_layout.scale;
    float scaled_icons_size = t_layout.icons_size * factor;
    float scaled_separator_size = t_layout.separator_size * factor;
    float scaled_gap_size = t_layout.gap_size * factor;
    float scaled_border = t_layout.border * factor;

    float separator_stride = scaled_separator_size + scaled_gap_size;
    float icon_stride = scaled_icons_size + scaled_gap_size;

    float left = 0.0f;
    float top = 0.0f;
    calculate_position(t_toolbar, t_camera, final_toolbar_width, final_toolbar_height, left, top);

    left += scaled_border;
    top -= scaled_border;

    float temp_left = left;
    float temp_top = top;
    float temp_width = 2.0f * t_layout.border;
    bool line_start = true;
    final_toolbar_height = 0;

    bool offset_flag = true;
    collapse_width = 0.0f;
    bool b_needs_to_double_check_collapse_width = true;
    m_p_override_render_rect = nullptr;
    std::vector<size_t> temp_indices;
    temp_indices.reserve(m_indices_to_draw.size());
    for (size_t i = 0; i < m_indices_to_draw.size(); ++i) {
        const auto& current_item = t_items[m_indices_to_draw[i]];

        if (line_start && (current_item->get_type() == GLToolbarItem::EType::SeparatorLine || current_item->is_separator())) {
            continue;
        }

        if (current_item->is_separator()) {
            temp_left += separator_stride;
            temp_width += t_layout.separator_size;
            temp_width += t_layout.gap_size;
            continue;
        }

        if (current_item->is_action() || current_item->get_type() == GLToolbarItem::EType::SeparatorLine) {
            if (current_item->is_action()) {
                temp_width += (float)t_layout.icons_size;
                temp_width += t_layout.gap_size;
            }
            else {
                temp_width += ((float)t_layout.icons_size * 0.5f);
                temp_width += t_layout.gap_size;
            }
            current_item->render_rect[0] = temp_left;
            current_item->render_rect[1] = temp_left + scaled_icons_size;
            current_item->render_rect[2] = temp_top - scaled_icons_size;
            current_item->render_rect[3] = temp_top;
        }
        //BBS: GUI refactor: GLToolbar
        if (current_item->is_action_with_text())
        {
            float scaled_text_size = current_item->get_extra_size_ratio() * scaled_icons_size;
            current_item->render_rect[0] = temp_left + scaled_icons_size;
            current_item->render_rect[1] = temp_left + scaled_icons_size + scaled_text_size;
            current_item->render_rect[2] = temp_top - scaled_icons_size;
            current_item->render_rect[3] = temp_top;
            temp_left += scaled_text_size;
            temp_width += current_item->get_extra_size_ratio() * t_layout.icons_size;
            temp_width += t_layout.gap_size;
        }
        if (current_item->get_type() == GLToolbarItem::EType::SeparatorLine) {
            temp_left += (icon_stride - 0.5f * scaled_icons_size);
        }
        else {
            temp_left += icon_stride;
        }

        if (line_start) {
            final_toolbar_height += toolbar_height;
        }

        if (current_item->is_collapse_button()) {
            collapse_width = temp_width;
            if (total_collapse_item_width < collapse_width) {
                collapse_width = total_collapse_item_width;
            }
            else {
                collapse_width = (total_collapse_item_width) / 2.0f + GLToolbar::Default_Icons_Size;
            }
            collapse_width = std::min(collapse_width * t_layout.scale, final_toolbar_width);
        }

        temp_indices.emplace_back(m_indices_to_draw[i]);

        line_start = false;

        bool new_line = false;
        if (offset_flag) {
            new_line = final_toolbar_width - temp_width * t_layout.scale < GLToolbar::Default_Icons_Size * t_layout.scale;
        }
        else {
            new_line = collapse_width - temp_width * t_layout.scale < GLToolbar::Default_Icons_Size * t_layout.scale;
        }
        if (new_line) {
            temp_left = left;
            temp_top -= toolbar_height * inv_zoom;
            if (offset_flag) {
                temp_top -= t_layout.collapsed_offset * factor;
                offset_flag = false;
            }
            else {
                if (b_needs_to_double_check_collapse_width) {
                    b_needs_to_double_check_collapse_width = false;
                    collapse_width = temp_width * t_layout.scale;
                }
            }
            temp_width = 2.0f * t_layout.border;
            line_start = true;
        }

        if (current_item->is_collapse_button()) {
            m_p_override_render_rect = current_item->render_rect;
        }
    }
    m_indices_to_draw.clear();
    m_indices_to_draw = std::move(temp_indices);

}

} // namespace GUI
} // namespace Slic3r
