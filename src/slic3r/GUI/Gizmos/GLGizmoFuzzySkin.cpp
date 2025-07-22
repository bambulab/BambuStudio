#include "GLGizmoFuzzySkin.hpp"

#include "libslic3r/Model.hpp"
//BBS
#include "libslic3r/Layer.hpp"
#include "libslic3r/Thread.hpp"

//#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/Utils/UndoRedo.hpp"


#include <GL/glew.h>

#include <boost/log/trivial.hpp>

namespace Slic3r::GUI {

GLGizmoFuzzySkin::GLGizmoFuzzySkin(GLCanvas3D& parent, unsigned int sprite_id)
    : GLGizmoPainterBase(parent, sprite_id), m_current_tool(ImGui::CircleButtonIcon)
{
    m_tool_type = ToolType::BRUSH;
    m_cursor_type = TriangleSelector::CursorType::CIRCLE;
}

void GLGizmoFuzzySkin::data_changed(bool is_serializing) {
    set_painter_gizmo_data(m_parent.get_selection());
}

void GLGizmoFuzzySkin::on_shutdown()
{
    //BBS
    //wait the thread
    if (m_thread.joinable()) {
        Print *print = m_print_instance.print_object->print();
        if (print) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "cancel the print";
            print->cancel();
        }
        //join the thread
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "try to join thread for 2000 ms";
        auto ret = m_thread.try_join_for(boost::chrono::milliseconds(2000));
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "join thread returns "<<ret;
    }

    m_print_instance.print_object = NULL;
    m_print_instance.model_instance = NULL;

    m_parent.use_slope(false);
    m_parent.toggle_model_objects_visibility(true);
}

std::string GLGizmoFuzzySkin::on_get_name() const
{
    if (!on_is_activable() && m_state == EState::Off) {
        return _u8L("Fuzzy skin Painting") + ":\n" + _u8L("Please select single object.");
    } else {
        return _u8L("Fuzzy skin Painting");
    }
}

EnforcerBlockerType GLGizmoFuzzySkin::get_right_button_state_type() const {
    return EnforcerBlockerType::NONE;
}

bool GLGizmoFuzzySkin::on_init()
{
    // BBS
    m_shortcut_key = WXK_CONTROL_H;
    const wxString ctrl                = GUI::shortkey_ctrl_prefix();
    const wxString alt                 = GUI::shortkey_alt_prefix();
    m_desc["clipping_of_view_caption"] = alt + _L("Mouse wheel");
    m_desc["clipping_of_view"]      = _L("Section view");
    m_desc["reset_direction"]       = _L("Reset direction");
    m_desc["cursor_size_caption"]      = ctrl + _L("Mouse wheel");
    m_desc["cursor_size"]           = _L("Pen size");
    m_desc["add_fuzzyskin_caption"] = _L("Left mouse button");
    m_desc["add_fuzzyskin"]         = _L("Add fuzzy skin");
    m_desc["remove_caption"]        =  _L("Right mouse button");
    m_desc["remove"]                = _L("Remove fuzzy skin");
    m_desc["remove_all"]            = _L("Erase all painting");
    m_desc["perform"]               = _L("Perform");
    m_desc["tool_type"]             = _L("Tool type");
    m_desc["smart_fill_angle_caption"] = ctrl + _L("Mouse wheel");
    m_desc["smart_fill_angle"]      = _L("Smart fill angle");

    memset(&m_print_instance, 0, sizeof(m_print_instance));
    return true;
}

void GLGizmoFuzzySkin::render_painter_gizmo() const
{
    const Selection& selection = m_parent.get_selection();

    glsafe(::glEnable(GL_BLEND));
    glsafe(::glEnable(GL_DEPTH_TEST));

    render_triangles(selection);

    m_c->object_clipper()->render_cut();
    m_c->instances_hider()->render_cut();
    render_cursor();

    glsafe(::glDisable(GL_BLEND));
}

// BBS
bool GLGizmoFuzzySkin::on_key_down_select_tool_type(int keyCode) {
    switch (keyCode)
    {
    case 'F':
        m_current_tool = ImGui::FillButtonIcon;
        break;
    case 'S':
        m_current_tool = ImGui::SphereButtonIcon;
        break;
    case 'C':
        m_current_tool = ImGui::CircleButtonIcon;
        break;
    case 'T':
        m_current_tool = ImGui::TriangleButtonIcon; break;
    default:
        return false;
        break;
    }
    return true;
}

std::string GLGizmoFuzzySkin::get_icon_filename(bool is_dark_mode) const
{
    return is_dark_mode ? "toolbar_fuzzyskin_dark.svg" : "toolbar_fuzzyskin.svg";
}

// BBS
void GLGizmoFuzzySkin::render_triangles(const Selection& selection) const
{
    ClippingPlaneDataWrapper clp_data = this->get_clipping_plane_data();
    const auto &             shader   = wxGetApp().get_shader("mm_gouraud");
    if (!shader) return;
    wxGetApp().bind_shader(shader);
    shader->set_uniform("clipping_plane", clp_data.clp_dataf);
    shader->set_uniform("z_range", clp_data.z_range);
    shader->set_uniform("slope.actived", m_parent.is_using_slope());
    ScopeGuard guard([shader]() {
        if (shader) wxGetApp().unbind_shader();
    });

    // BBS: to improve the random white pixel issue
    glsafe(::glDisable(GL_CULL_FACE));

    const ModelObject *mo      = m_c->selection_info()->model_object();
    int                mesh_id = -1;
    for (const ModelVolume *mv : mo->volumes) {
        if (!mv->is_model_part()) continue;

        ++mesh_id;

        Transform3d trafo_matrix;
        if (m_parent.get_canvas_type() == GLCanvas3D::CanvasAssembleView) {
            trafo_matrix = mo->instances[selection.get_instance_idx()]->get_assemble_transformation().get_matrix() * mv->get_matrix();
            trafo_matrix.translate(mv->get_transformation().get_offset() * (GLVolume::explosion_ratio - 1.0) +
                                   mo->instances[selection.get_instance_idx()]->get_offset_to_assembly() * (GLVolume::explosion_ratio - 1.0));
        } else {
            trafo_matrix = mo->instances[selection.get_instance_idx()]->get_transformation().get_matrix() * mv->get_matrix();
        }

        bool is_left_handed = trafo_matrix.matrix().determinant() < 0.;
        if (is_left_handed) glsafe(::glFrontFace(GL_CW));

        const Camera &    camera = wxGetApp().plater()->get_camera();
        const Transform3d matrix = camera.get_view_matrix() * trafo_matrix;
        shader->set_uniform("view_model_matrix", matrix);
        shader->set_uniform("projection_matrix", camera.get_projection_matrix());
        shader->set_uniform("normal_matrix", (Matrix3d) matrix.matrix().block(0, 0, 3, 3).inverse().transpose());

        shader->set_uniform("volume_world_matrix", trafo_matrix);
        shader->set_uniform("volume_mirrored", is_left_handed);
        m_triangle_selectors[mesh_id]->render(m_imgui, trafo_matrix);

        if (is_left_handed) glsafe(::glFrontFace(GL_CCW));
    }
}

void GLGizmoFuzzySkin::on_set_state()
{
    GLGizmoPainterBase::on_set_state();

    if (get_state() == On) {

    }
    else if (get_state() == Off) {
        ModelObject* mo = m_c->selection_info()->model_object();
        if (mo) Slic3r::save_object_mesh(*mo);
    }
}

void GLGizmoFuzzySkin::on_render_input_window(float x, float y, float bottom_limit)
{
    if (!m_c) {
        return;
    }
    const auto& p_selection_info = m_c->selection_info();
    if (!p_selection_info) {
        return;
    }
    const auto& p_model_object = p_selection_info->model_object();
    if (!p_model_object) {
        return;
    }
    m_imgui_start_pos[0] = x;
    m_imgui_start_pos[1] = y;
    // BBS
    wchar_t old_tool = m_current_tool;


    const float approx_height = m_imgui->scaled(23.f);
    y = std::min(y, bottom_limit - approx_height);

    GizmoImguiSetNextWIndowPos(x, y, ImGuiCond_Always, 0.0f, 0.0f);

    //BBS
    ImGuiWrapper::push_toolbar_style(m_parent.get_scale());
    GizmoImguiBegin(get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    // First calculate width of all the texts that are could possibly be shown. We will decide set the dialog width based on that:
    const float space_size = m_imgui->get_style_scaling() * 8;
    const float clipping_slider_left    = m_imgui->calc_text_size(m_desc.at("clipping_of_view")).x + m_imgui->scaled(1.5f);
    const float cursor_slider_left      = m_imgui->calc_text_size(m_desc.at("cursor_size")).x + m_imgui->scaled(1.5f);
    const float reset_button_slider_left = m_imgui->calc_text_size(m_desc.at("reset_direction")).x + m_imgui->scaled(1.5f) + ImGui::GetStyle().FramePadding.x * 2;
    const float remove_btn_width        = m_imgui->calc_text_size(m_desc.at("remove_all")).x + m_imgui->scaled(1.5f);
    const float smart_fill_angle_txt_width = m_imgui->calc_text_size(m_desc.at("smart_fill_angle")).x + m_imgui->scaled(1.5f);
    const float buttons_width           = remove_btn_width  + m_imgui->scaled(1.5f);
    const float empty_button_width      = m_imgui->calc_button_size("").x;

    const float minimal_slider_width = m_imgui->scaled(4.f);

    float caption_max    = 0.f;
    float total_text_max = 0.f;
    for (const auto &t : std::array<std::string, 5>{"add_fuzzyskin", "remove", "cursor_size", "clipping_of_view", "smart_fill_angle"}) {
        caption_max    = std::max(caption_max, m_imgui->calc_text_size(m_desc[t + "_caption"]).x);
        total_text_max = std::max(total_text_max, m_imgui->calc_text_size(m_desc[t]).x);
    }
    total_text_max += caption_max + m_imgui->scaled(1.f);
    caption_max += m_imgui->scaled(1.f);

    const float sliders_left_width = std::max(smart_fill_angle_txt_width, std::max(reset_button_slider_left, std::max(cursor_slider_left, clipping_slider_left)));
    const float slider_icon_width  = m_imgui->get_slider_icon_size().x;
    const float max_tooltip_width = ImGui::GetFontSize() * 20.0f;

    const float sliders_width = m_imgui->scaled(7.0f);
    const float drag_left_width = ImGui::GetStyle().WindowPadding.x + sliders_left_width + sliders_width - space_size;

    float drag_pos_times     = 0.7;

    ImGui::AlignTextToFramePadding();
    m_imgui->text(m_desc.at("tool_type"));
    std::array<wchar_t, 4> tool_ids = {ImGui::CircleButtonIcon, ImGui::SphereButtonIcon, ImGui::TriangleButtonIcon, ImGui::FillButtonIcon};
    std::array<wchar_t, 4> icons;
    if (m_is_dark_mode)
        icons = {ImGui::CircleButtonDarkIcon, ImGui::SphereButtonDarkIcon, ImGui::TriangleButtonDarkIcon, ImGui::FillButtonDarkIcon};
    else
        icons = tool_ids;

    std::array<wxString, 4> tool_tips = {_L("Circle"), _L("Sphere"), _L("Triangle"), _L("Fill")};
    for (int i = 0; i < tool_ids.size(); i++) {
        std::string  str_label = std::string("##");
        std::wstring btn_name = icons[i] + boost::nowide::widen(str_label);

        if (i != 0) ImGui::SameLine((empty_button_width + m_imgui->scaled(1.75f)) * i + m_imgui->scaled(1.3f));

        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0);
        if (m_current_tool == tool_ids[i]) {
            ImGui::PushStyleColor(ImGuiCol_Button, m_is_dark_mode ? ImVec4(43 / 255.0f, 64 / 255.0f, 54 / 255.0f, 1.00f) : ImVec4(0.86f, 0.99f, 0.91f, 1.00f)); // r, g, b, a
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, m_is_dark_mode ? ImVec4(43 / 255.0f, 64 / 255.0f, 54 / 255.0f, 1.00f) : ImVec4(0.86f, 0.99f, 0.91f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, m_is_dark_mode ? ImVec4(43 / 255.0f, 64 / 255.0f, 54 / 255.0f, 1.00f) : ImVec4(0.86f, 0.99f, 0.91f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.00f, 0.68f, 0.26f, 1.00f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 1.0);
        }
        bool btn_clicked = ImGui::Button(into_u8(btn_name).c_str());
        if (m_current_tool == tool_ids[i])
        {
            ImGui::PopStyleColor(4);
            ImGui::PopStyleVar(2);
        }
        ImGui::PopStyleVar(1);

        if (btn_clicked && m_current_tool != tool_ids[i]) {
            m_current_tool = tool_ids[i];
            for (auto& triangle_selector : m_triangle_selectors) {
                triangle_selector->seed_fill_unselect_all_triangles();
                triangle_selector->request_update_render_data();
            }
        }

        if (ImGui::IsItemHovered()) {
            m_imgui->tooltip(tool_tips[i], max_tooltip_width);
        }
    }

    ImGui::Separator();

    if (m_current_tool != old_tool)
        this->tool_changed(old_tool, m_current_tool);

    ImGui::Dummy(ImVec2(0.0f, ImGui::GetFontSize() * 0.1));

    if (m_current_tool == ImGui::CircleButtonIcon) {
        m_cursor_type = TriangleSelector::CursorType::CIRCLE;
        m_tool_type = ToolType::BRUSH;

        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc.at("cursor_size"));
        ImGui::SameLine(sliders_left_width);
        ImGui::PushItemWidth(sliders_width);
        m_imgui->bbl_slider_float_style("##cursor_radius", &m_cursor_radius, CursorRadiusMin, CursorRadiusMax, "%.2f", 1.0f, true);
        ImGui::SameLine(drag_left_width);
        ImGui::PushItemWidth(1.5 * slider_icon_width);
        ImGui::BBLDragFloat("##cursor_radius_input", &m_cursor_radius, 0.05f, 0.0f, 0.0f, "%.2f");
    } else if (m_current_tool == ImGui::SphereButtonIcon) {
        m_cursor_type = TriangleSelector::CursorType::SPHERE;
        m_tool_type = ToolType::BRUSH;

        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc.at("cursor_size"));
        ImGui::SameLine(sliders_left_width);
        ImGui::PushItemWidth(sliders_width);
        m_imgui->bbl_slider_float_style("##cursor_radius", &m_cursor_radius, CursorRadiusMin, CursorRadiusMax, "%.2f", 1.0f, true);
        ImGui::SameLine(drag_left_width);
        ImGui::PushItemWidth(1.5 * slider_icon_width);
        ImGui::BBLDragFloat("##cursor_radius_input", &m_cursor_radius, 0.05f, 0.0f, 0.0f, "%.2f");
    } else if (m_current_tool == ImGui::FillButtonIcon) {
        m_cursor_type = TriangleSelector::CursorType::POINTER;
        m_tool_type = ToolType::SMART_FILL;

        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc.at("smart_fill_angle"));
        std::string format_str = std::string("%.f") + I18N::translate_utf8("", "Face angle threshold, placed after the number with no whitespace in between.");
        ImGui::SameLine(sliders_left_width);
        ImGui::PushItemWidth(sliders_width);
        if (m_imgui->bbl_slider_float_style("##smart_fill_angle", &m_smart_fill_angle, SmartFillAngleMin, SmartFillAngleMax, format_str.data(), 1.0f, true))
            for (auto& triangle_selector : m_triangle_selectors) {
                triangle_selector->seed_fill_unselect_all_triangles();
                triangle_selector->request_update_render_data();
            }
        ImGui::SameLine(drag_left_width);
        ImGui::PushItemWidth(1.5 * slider_icon_width);
        ImGui::BBLDragFloat("##smart_fill_angle_input", &m_smart_fill_angle, 0.05f, 0.0f, 0.0f, "%.2f");
    } else if (m_current_tool == ImGui::TriangleButtonIcon) {
        m_cursor_type = TriangleSelector::CursorType::POINTER;
        m_tool_type   = ToolType::BRUSH;
    }

    //ImGui::AlignTextToFramePadding();

    if (m_current_tool != ImGui::GapFillIcon) {
        ImGui::Separator();
        if (m_c->object_clipper()->get_position() == 0.f) {
            ImGui::AlignTextToFramePadding();
            m_imgui->text(m_desc.at("clipping_of_view"));
        }
        else {
            if (m_imgui->button(m_desc.at("reset_direction"))) {
                wxGetApp().CallAfter([this]() {
                    m_c->object_clipper()->set_position(-1., false);
                    });
            }
        }

        auto clp_dist = float(m_c->object_clipper()->get_position());
        ImGui::SameLine(sliders_left_width);
        ImGui::PushItemWidth(sliders_width);
        bool b_bbl_slider_float = m_imgui->bbl_slider_float_style("##clp_dist", &clp_dist, 0.f, 1.f, "%.2f", 1.0f, true);

        ImGui::SameLine(drag_left_width);
        ImGui::PushItemWidth(1.5 * slider_icon_width);
        bool b_drag_input = ImGui::BBLDragFloat("##clp_dist_input", &clp_dist, 0.05f, 0.0f, 0.0f, "%.2f");

        if (b_bbl_slider_float || b_drag_input) m_c->object_clipper()->set_position(clp_dist, true);
    }

    ImGui::Separator();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 10.0f));
    float get_cur_y = ImGui::GetContentRegionMax().y + ImGui::GetFrameHeight() + y;
    show_tooltip_information(caption_max, x, get_cur_y);

    float f_scale = m_parent.get_main_toolbar_scale();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f * f_scale));

    ImGui::SameLine();

    if (m_imgui->button(m_desc.at("remove_all"))) {
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Reset selection", UndoRedo::SnapshotType::GizmoAction);
        ModelObject *        mo  = m_c->selection_info()->model_object();
        int                  idx = -1;
        for (ModelVolume *mv : mo->volumes)
            if (mv->is_model_part()) {
                ++idx;
                m_triangle_selectors[idx]->reset();
                m_triangle_selectors[idx]->request_update_render_data(true);
            }

        update_model_object();
        m_parent.set_as_dirty();
    }
    ImGui::PopStyleVar(2);
    m_imgui_end_pos[0] = m_imgui_start_pos[0] + ImGui::GetWindowWidth();
    m_imgui_end_pos[1] = m_imgui_start_pos[1] + ImGui::GetWindowHeight();
    GizmoImguiEnd();

    // BBS
    ImGuiWrapper::pop_toolbar_style();
}

void GLGizmoFuzzySkin::tool_changed(wchar_t old_tool, wchar_t new_tool)
{
    if ((old_tool == ImGui::GapFillIcon && new_tool == ImGui::GapFillIcon) ||
        (old_tool != ImGui::GapFillIcon && new_tool != ImGui::GapFillIcon))
        return;

    for (auto& selector_ptr : m_triangle_selectors) {
        TriangleSelectorPatch* tsp = dynamic_cast<TriangleSelectorPatch*>(selector_ptr.get());
        tsp->set_filter_state(new_tool == ImGui::GapFillIcon);
    }
}

void GLGizmoFuzzySkin::show_tooltip_information(float caption_max, float x, float y)
{
    ImTextureID normal_id = m_parent.get_gizmos_manager().get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TOOLBAR_TOOLTIP);
    ImTextureID hover_id  = m_parent.get_gizmos_manager().get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TOOLBAR_TOOLTIP_HOVER);

    caption_max += m_imgui->calc_text_size(": ").x + 15.f;

    float font_size = ImGui::GetFontSize();
    ImVec2 button_size = ImVec2(font_size * 1.8, font_size * 1.3);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 0, ImGui::GetStyle().FramePadding.y });
    ImGui::ImageButton3(normal_id, hover_id, button_size);

    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip2(ImVec2(x, y));
        auto draw_text_with_caption = [this, &caption_max](const wxString &caption, const wxString &text) {
            // BBS
            m_imgui->text_colored(ImGuiWrapper::COL_ACTIVE, caption);
            ImGui::SameLine(caption_max);
            m_imgui->text_colored(ImGuiWrapper::COL_WINDOW_BG, text);
        };

        std::vector<std::string> tip_items;
        switch (m_tool_type) {
            case ToolType::BRUSH:
                tip_items = {"add_fuzzyskin",  "remove", "cursor_size", "clipping_of_view"};
                break;
            case ToolType::BUCKET_FILL:
                break;
            case ToolType::SMART_FILL:
                tip_items = {"add_fuzzyskin", "remove", "smart_fill_angle", "clipping_of_view"};
                break;
            default:
                break;
        }
        for (const auto &t : tip_items)
            draw_text_with_caption(m_desc.at(t + "_caption") + ": ", m_desc.at(t));

        ImGui::EndTooltip();
    }
    ImGui::PopStyleVar(2);
}

//BBS: remove const
void GLGizmoFuzzySkin::update_model_object()
{
    bool updated = false;
    ModelObject* mo = m_c->selection_info()->model_object();
    int idx = -1;
    for (ModelVolume* mv : mo->volumes) {
        if (! mv->is_model_part())
            continue;
        ++idx;
        updated |= mv->fuzzy_skin_facets.set(*m_triangle_selectors[idx].get());
    }

    if (updated) {
        const ModelObjectPtrs& mos = wxGetApp().model().objects;
        wxGetApp().obj_list()->update_info_items(std::find(mos.begin(), mos.end(), mo) - mos.begin());

        m_parent.post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));
    }
}

//BBS
void GLGizmoFuzzySkin::update_from_model_object(bool first_update)
{
    wxBusyCursor wait;

    const ModelObject* mo = m_c->selection_info()->model_object();
    m_triangle_selectors.clear();
    //BBS: add timestamp logic
    m_volume_timestamps.clear();

    int volume_id = -1;
    std::vector<std::array<float, 4>> ebt_colors;
    ebt_colors.push_back(GLVolume::NEUTRAL_COLOR);
    ebt_colors.push_back(TriangleSelectorGUI::enforcers_color);
    ebt_colors.push_back(TriangleSelectorGUI::blockers_color);
    for (const ModelVolume* mv : mo->volumes) {
        if (! mv->is_model_part())
            continue;

        ++volume_id;

        // This mesh does not account for the possible Z up SLA offset.
        const TriangleMesh* mesh = &mv->mesh();
        m_triangle_selectors.emplace_back(std::make_unique<TriangleSelectorPatch>(*mesh, ebt_colors));
        // Reset of TriangleSelector is done inside TriangleSelectorGUI's constructor, so we don't need it to perform it again in deserialize().
        m_triangle_selectors.back()->deserialize(mv->fuzzy_skin_facets.get_data(), false);
        m_triangle_selectors.back()->request_update_render_data();
        m_triangle_selectors.back()->set_wireframe_needed(true);
        //BBS: add timestamp logic
        m_volume_timestamps.emplace_back(mv->fuzzy_skin_facets.timestamp());
    }
}

PainterGizmoType GLGizmoFuzzySkin::get_painter_type() const
{
    return PainterGizmoType::FUZZY_SKIN;
}

wxString GLGizmoFuzzySkin::handle_snapshot_action_name(bool shift_down, GLGizmoPainterBase::Button button_down) const
{
    // BBS remove _L()
    wxString action_name;
    if (shift_down)
        action_name = ("Unselect all");
    else {
        if (button_down == Button::Left)
            action_name = ("Add fuzzy skin");
        else
            action_name = ("Remove fuzzy skin");
    }
    return action_name;
}

} // namespace Slic3r::GUI
