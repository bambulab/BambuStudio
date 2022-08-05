#include "GLGizmoFdmSupports.hpp"

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
#include "slic3r/Utils/UndoRedo.hpp"


#include <GL/glew.h>


namespace Slic3r::GUI {

GLGizmoFdmSupports::GLGizmoFdmSupports(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoPainterBase(parent, icon_filename, sprite_id), m_current_tool(ImGui::CircleButtonIcon)
{
    m_tool_type = ToolType::BRUSH;
    m_cursor_type = TriangleSelector::CursorType::CIRCLE;
}

void GLGizmoFdmSupports::on_shutdown()
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

    m_highlight_by_angle_threshold_deg = 0.f;
    m_parent.use_slope(false);
    m_parent.toggle_model_objects_visibility(true);
}

//BBS: add on_open
void GLGizmoFdmSupports::on_opening()
{
    m_angle_threshold_deg = 40;
    m_parent.set_slope_normal_angle(90.f - m_angle_threshold_deg);
    if (! m_parent.is_using_slope()) {
        m_parent.use_slope(true);
        m_parent.set_as_dirty();
    }
    m_print_instance.print_object = NULL;
    m_print_instance.model_instance = NULL;
    m_edit_state = state_idle;

    m_volume_ready = false;
    m_volume_valid = false;
}

std::string GLGizmoFdmSupports::on_get_name() const
{
    return _u8L("Supports Painting");
}

bool GLGizmoFdmSupports::on_init()
{
    // BBS
    m_shortcut_key = WXK_CONTROL_L;

    m_desc["clipping_of_view"]      = _L("Section view") + ": ";
    m_desc["cursor_size"]           = _L("Pen size") + ": ";
    m_desc["enforce_caption"]       = _L("Left mouse button") + ": ";
    m_desc["enforce"]               = _L("Enforce supports");
    m_desc["block_caption"]         = _L("Right mouse button") + ": ";
    m_desc["block"]                 = _L("Block supports");
    m_desc["remove_caption"]        = _L("Shift + Left mouse button") + ": ";
    m_desc["remove"]                = _L("Erase painting");
    m_desc["remove_all"]            = _L("Erase all painting");
    m_desc["highlight_by_angle"]    = _L("Highlight overhang areas") + ": ";
    m_desc["gap_fill"]              = _L("Gap fill");
    m_desc["perform"]               = _L("Perform");
    m_desc["gap_area"]              = _L("Gap area");
    m_desc["brush_size"]            = _L("Set pen size");
    m_desc["brush_size_caption"]    = _L("Ctrl + Mouse wheel") + ": ";
    m_desc["tool_type"]             = _L("Tool type");
    m_desc["smart_fill_angle"]      = _L("Smart fill angle") + ": ";

    memset(&m_print_instance, sizeof(m_print_instance), 0);
    return true;
}

void GLGizmoFdmSupports::render_painter_gizmo() const
{
    const Selection& selection = m_parent.get_selection();

    glsafe(::glEnable(GL_BLEND));
    glsafe(::glEnable(GL_DEPTH_TEST));

    render_triangles(selection);
    //BBS: draw support volumes
    if (m_volume_ready && m_support_volume && (m_edit_state != state_generating))
    {
        //m_support_volume->set_render_color();
        ::glColor4f(0.f, 0.7f, 0.f, 0.7f);
        m_support_volume->render();
    }

    m_c->object_clipper()->render_cut();
    m_c->instances_hider()->render_cut();
    render_cursor();

    glsafe(::glDisable(GL_BLEND));
}

// BBS
void GLGizmoFdmSupports::render_triangles(const Selection& selection) const
{
    ClippingPlaneDataWrapper clp_data = this->get_clipping_plane_data();
    auto* shader = wxGetApp().get_shader("mm_gouraud");
    if (!shader)
        return;
    shader->start_using();
    shader->set_uniform("clipping_plane", clp_data.clp_dataf);
    shader->set_uniform("z_range", clp_data.z_range);
    ScopeGuard guard([shader]() { if (shader) shader->stop_using(); });

    const ModelObject* mo = m_c->selection_info()->model_object();
    int                mesh_id = -1;
    for (const ModelVolume* mv : mo->volumes) {
        if (!mv->is_model_part())
            continue;

        ++mesh_id;

        const Transform3d trafo_matrix = mo->instances[selection.get_instance_idx()]->get_transformation().get_matrix() * mv->get_matrix();

        bool is_left_handed = trafo_matrix.matrix().determinant() < 0.;
        if (is_left_handed)
            glsafe(::glFrontFace(GL_CW));

        glsafe(::glPushMatrix());
        glsafe(::glMultMatrixd(trafo_matrix.data()));

        float normal_z = -::cos(Geometry::deg2rad(m_highlight_by_angle_threshold_deg));
        Matrix3f normal_matrix = static_cast<Matrix3f>(trafo_matrix.matrix().block(0, 0, 3, 3).inverse().transpose().cast<float>());

        shader->set_uniform("volume_world_matrix", trafo_matrix);
        shader->set_uniform("volume_mirrored", is_left_handed);
        shader->set_uniform("slope.actived", m_parent.is_using_slope());
        shader->set_uniform("slope.volume_world_normal_matrix", static_cast<Matrix3f>(trafo_matrix.matrix().block(0, 0, 3, 3).inverse().transpose().cast<float>()));
        shader->set_uniform("slope.normal_z", normal_z);
        m_triangle_selectors[mesh_id]->render(m_imgui);

        glsafe(::glPopMatrix());
        if (is_left_handed)
            glsafe(::glFrontFace(GL_CCW));
    }
}

void GLGizmoFdmSupports::on_set_state()
{
    GLGizmoPainterBase::on_set_state();

    if (get_state() == On) {
        m_support_threshold_angle = -1;
    }
    else if (get_state() == Off) {
        ModelObject* mo = m_c->selection_info()->model_object();
        if (mo) Slic3r::save_object_mesh(*mo);
    }
}

static std::string into_u8(const wxString& str)
{
    auto buffer_utf8 = str.utf8_str();
    return std::string(buffer_utf8.data());
}

void GLGizmoFdmSupports::on_render_input_window(float x, float y, float bottom_limit)
{
    init_print_instance();
    if (! m_c->selection_info()->model_object())
        return;

    // BBS
    wchar_t old_tool = m_current_tool;

    int support_threshold_angle = get_selection_support_threshold_angle();
    // when support painting tool is on, reset highlight threshold angle
    if (m_support_threshold_angle == -1) {
        m_highlight_by_angle_threshold_deg = support_threshold_angle;
        m_parent.set_slope_normal_angle(90.f - m_highlight_by_angle_threshold_deg);
    }
    m_support_threshold_angle = support_threshold_angle;

    const float approx_height = m_imgui->scaled(23.f);
    y = std::min(y, bottom_limit - approx_height);

    GizmoImguiSetNextWIndowPos(x, y, ImGuiCond_Always, 0.0f, 0.0f);

    //BBS
    ImGuiWrapper::push_toolbar_style(m_parent.get_scale());
    GizmoImguiBegin(get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    // First calculate width of all the texts that are could possibly be shown. We will decide set the dialog width based on that:
    const float clipping_slider_left    = m_imgui->calc_text_size(m_desc.at("clipping_of_view")).x + m_imgui->scaled(1.5f);
    const float cursor_slider_left      = m_imgui->calc_text_size(m_desc.at("cursor_size")).x + m_imgui->scaled(1.5f);
    const float gap_fill_slider_left    = m_imgui->calc_text_size(m_desc.at("gap_fill")).x + m_imgui->scaled(1.5f);
    const float highlight_slider_left   = m_imgui->calc_text_size(m_desc.at("highlight_by_angle")).x + m_imgui->scaled(1.5f);
    const float remove_btn_width        = m_imgui->calc_text_size(m_desc.at("remove_all")).x + m_imgui->scaled(1.5f);
    const float filter_btn_width        = m_imgui->calc_text_size(m_desc.at("perform")).x + m_imgui->scaled(1.5f);
    const float buttons_width           = remove_btn_width + filter_btn_width + m_imgui->scaled(1.5f);
    const float empty_button_width      = m_imgui->calc_button_size("").x;

    const float tips_width           = m_imgui->calc_text_size(_L("Auto support threshold angle: ") + " 90 ").x + m_imgui->scaled(1.5f);
    const float minimal_slider_width = m_imgui->scaled(4.f);
    float slider_width_times = 1.5;

    float caption_max    = 0.f;
    float total_text_max = 0.f;
    for (const auto &t : std::array<std::string, 4>{"enforce", "block", "remove", "brush_size"}) {
        caption_max    = std::max(caption_max, m_imgui->calc_text_size(m_desc[t + "_caption"]).x);
        total_text_max = std::max(total_text_max, m_imgui->calc_text_size(m_desc[t]).x);
    }
    total_text_max += caption_max + m_imgui->scaled(1.f);
    caption_max += m_imgui->scaled(1.f);

    const float sliders_left_width = std::max(std::max(cursor_slider_left, clipping_slider_left), std::max(highlight_slider_left, gap_fill_slider_left));
    const float slider_icon_width  = m_imgui->get_slider_icon_size().x;
    float       window_width       = minimal_slider_width + sliders_left_width + slider_icon_width;
    const float max_tooltip_width = ImGui::GetFontSize() * 20.0f;

    window_width = std::max(window_width, total_text_max);
    window_width = std::max(window_width, buttons_width);
    window_width = std::max(window_width, tips_width);

    float drag_pos_times     = 0.7;

    ImGui::AlignTextToFramePadding();
    m_imgui->text(m_desc.at("tool_type"));
    std::array<wchar_t, 4> tool_icons = { ImGui::CircleButtonIcon, ImGui::SphereButtonIcon, ImGui::FillButtonIcon, ImGui::GapFillIcon };
    std::array<wxString, 4> tool_tips = { _L("Circle"), _L("Sphere"), _L("Fill"), _L("Gap Fill") };
    for (int i = 0; i < tool_icons.size(); i++) {
        std::string  str_label = std::string("##");
        std::wstring btn_name = tool_icons[i] + boost::nowide::widen(str_label);

        if (i != 0) ImGui::SameLine((empty_button_width + m_imgui->scaled(1.75f)) * i + m_imgui->scaled(1.3f));

        if (m_current_tool == tool_icons[i]) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.81f, 0.81f, 0.81f, 1.0f)); // r, g, b, a
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.81f, 0.81f, 0.81f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.81f, 0.81f, 0.81f, 1.0f));
        }
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0);
        bool btn_clicked = ImGui::Button(into_u8(btn_name).c_str());
        ImGui::PopStyleVar(1);
        if (m_current_tool == tool_icons[i])ImGui::PopStyleColor(3);

        if (btn_clicked && m_current_tool != tool_icons[i]) {
            m_current_tool = tool_icons[i];
            for (auto& triangle_selector : m_triangle_selectors) {
                triangle_selector->seed_fill_unselect_all_triangles();
                triangle_selector->request_update_render_data();
            }
        }

        if (ImGui::IsItemHovered()) {
            m_imgui->tooltip(tool_tips[i], max_tooltip_width);
        }
    }

    if (m_current_tool != old_tool)
        this->tool_changed(old_tool, m_current_tool);

    ImGui::Dummy(ImVec2(0.0f, ImGui::GetFontSize() * 0.1));

    if (m_current_tool == ImGui::CircleButtonIcon) {
        m_cursor_type = TriangleSelector::CursorType::CIRCLE;
        m_tool_type = ToolType::BRUSH;

        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc.at("cursor_size"));
        ImGui::SameLine(sliders_left_width);
        ImGui::PushItemWidth(window_width - sliders_left_width - slider_icon_width);
        m_imgui->bbl_slider_float_style("##cursor_radius", &m_cursor_radius, CursorRadiusMin, CursorRadiusMax, "%.2f", 1.0f, true);
        ImGui::SameLine(window_width - drag_pos_times * slider_icon_width);
        ImGui::PushItemWidth(1.5 * slider_icon_width);
        ImGui::BBLDragFloat("##cursor_radius_input", &m_cursor_radius, 0.05f, 0.0f, 0.0f, "%.2f");
    } else if (m_current_tool == ImGui::SphereButtonIcon) {
        m_cursor_type = TriangleSelector::CursorType::SPHERE;
        m_tool_type = ToolType::BRUSH;

        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc.at("cursor_size"));
        ImGui::SameLine(sliders_left_width);
        ImGui::PushItemWidth(window_width - sliders_left_width - slider_icon_width);
        m_imgui->bbl_slider_float_style("##cursor_radius", &m_cursor_radius, CursorRadiusMin, CursorRadiusMax, "%.2f", 1.0f, true);
        ImGui::SameLine(window_width - drag_pos_times * slider_icon_width);
        ImGui::PushItemWidth(1.5 * slider_icon_width);
        ImGui::BBLDragFloat("##cursor_radius_input", &m_cursor_radius, 0.05f, 0.0f, 0.0f, "%.2f");
    } else if (m_current_tool == ImGui::FillButtonIcon) {
        m_cursor_type = TriangleSelector::CursorType::POINTER;
        m_tool_type = ToolType::SMART_FILL;

        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc.at("smart_fill_angle"));
        std::string format_str = std::string("%.f") + I18N::translate_utf8("", "Face angle threshold, placed after the number with no whitespace in between.");
        ImGui::SameLine(sliders_left_width);
        ImGui::PushItemWidth(window_width - sliders_left_width - slider_icon_width);
        if (m_imgui->bbl_slider_float_style("##smart_fill_angle", &m_smart_fill_angle, SmartFillAngleMin, SmartFillAngleMax, format_str.data(), 1.0f, true))
            for (auto& triangle_selector : m_triangle_selectors) {
                triangle_selector->seed_fill_unselect_all_triangles();
                triangle_selector->request_update_render_data();
            }
        ImGui::SameLine(window_width - drag_pos_times * slider_icon_width);
        ImGui::PushItemWidth(1.5 * slider_icon_width);
        ImGui::BBLDragFloat("##smart_fill_angle_input", &m_smart_fill_angle, 0.05f, 0.0f, 0.0f, "%.2f");
    } else if (m_current_tool == ImGui::GapFillIcon) {
        m_tool_type = ToolType::GAP_FILL;
        m_cursor_type = TriangleSelector::CursorType::POINTER;

        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc["gap_area"] + ":");
        ImGui::SameLine(sliders_left_width);
        ImGui::PushItemWidth(window_width - sliders_left_width - slider_icon_width);
        std::string format_str = std::string("%.2f") + I18N::translate_utf8("", "Triangle patch area threshold,""triangle patch will be merged to neighbor if its area is less than threshold");
        m_imgui->bbl_slider_float_style("##gap_area", &TriangleSelectorPatch::gap_area, TriangleSelectorPatch::GapAreaMin, TriangleSelectorPatch::GapAreaMax, format_str.data(), 1.0f, true);
        ImGui::SameLine(window_width - drag_pos_times * slider_icon_width);
        ImGui::PushItemWidth(1.5 * slider_icon_width);
        ImGui::BBLDragFloat("##gap_area_input", &TriangleSelectorPatch::gap_area, 0.05f, 0.0f, 0.0f, "%.2f");
    }

    float position_before_text_y = ImGui::GetCursorPos().y;
    ImGui::AlignTextToFramePadding();
    m_imgui->text(m_desc["highlight_by_angle"]);
    ImGui::AlignTextToFramePadding();
    float position_after_text_y = ImGui::GetCursorPos().y;
    ImGui::SameLine(sliders_left_width);

    float slider_height = m_imgui->get_slider_float_height();
    // Makes slider to be aligned to bottom of the multi-line text.
    float slider_start_position_y = std::max(position_before_text_y, position_after_text_y - slider_height);
    ImGui::SetCursorPosY(slider_start_position_y);

    std::string format_str = std::string("%.f");
    ImGui::PushItemWidth(window_width - sliders_left_width - slider_icon_width);
    wxString tooltip = _L("Highlight faces according to overhang angle.");
    if (m_imgui->bbl_slider_float_style("##angle_threshold_deg", &m_highlight_by_angle_threshold_deg, 0.f, 90.f, format_str.data(), 1.0f, true, tooltip)) {
        m_parent.set_slope_normal_angle(90.f - m_highlight_by_angle_threshold_deg);
        if (!m_parent.is_using_slope()) {
            m_parent.use_slope(true);
            m_parent.set_as_dirty();
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        if (m_support_threshold_angle != 0) {
            wxString str_tooltip = (_L("Auto support threshold angle: ") + std::to_string(m_support_threshold_angle));
            m_imgui->text_colored(ImGuiWrapper::COL_WINDOW_BG, str_tooltip);
        } else {
            wxString s_tooltip = (_L("No auto support"));
            m_imgui->text_colored(ImGuiWrapper::COL_WINDOW_BG, s_tooltip);
        }
        ImGui::EndTooltip();
    }
    ImGui::SameLine(window_width - drag_pos_times * slider_icon_width);
    ImGui::PushItemWidth(1.5 * slider_icon_width);
    ImGui::BBLDragFloat("##angle_threshold_deg_input", &m_highlight_by_angle_threshold_deg, 0.05f, 0.0f, 0.0f, "%.2f");
    
    if (m_current_tool != ImGui::GapFillIcon) {
        ImGui::Separator();
        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc.at("clipping_of_view"));

        auto clp_dist = float(m_c->object_clipper()->get_position());
        ImGui::SameLine(sliders_left_width);
        ImGui::PushItemWidth(window_width - sliders_left_width - slider_icon_width);
        bool b_bbl_slider_float = m_imgui->bbl_slider_float_style("##clp_dist", &clp_dist, 0.f, 1.f, "%.2f", 1.0f, true);

        ImGui::SameLine(window_width - drag_pos_times * slider_icon_width);
        ImGui::PushItemWidth(1.5 * slider_icon_width);
        bool b_drag_input = ImGui::BBLDragFloat("##clp_dist_input", &clp_dist, 0.05f, 0.0f, 0.0f, "%.2f");

        if (b_bbl_slider_float || b_drag_input) m_c->object_clipper()->set_position(clp_dist, true);
    }

    ImGui::Separator();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 10.0f));
    float get_cur_y = ImGui::GetContentRegionMax().y + ImGui::GetFrameHeight() + y;
    show_tooltip_information(caption_max, x, get_cur_y);

    float f_scale =m_parent.get_gizmos_manager().get_layout_scale();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f * f_scale));

    ImGui::SameLine();

    // Perform button is for gap fill
    if (m_current_tool == ImGui::GapFillIcon) {
        if (m_imgui->button(m_desc.at("perform"))) {
            Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Reset selection", UndoRedo::SnapshotType::GizmoAction);

            for (int i = 0; i < m_triangle_selectors.size(); i++) {
                TriangleSelectorPatch* ts_mm = dynamic_cast<TriangleSelectorPatch*>(m_triangle_selectors[i].get());
                ts_mm->update_selector_triangles();
                ts_mm->request_update_render_data(true);
            }
            update_model_object();
            m_parent.set_as_dirty();
        }
    }

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

    GizmoImguiEnd();

    // BBS
    ImGuiWrapper::pop_toolbar_style();
}

void GLGizmoFdmSupports::tool_changed(wchar_t old_tool, wchar_t new_tool)
{
    if ((old_tool == ImGui::GapFillIcon && new_tool == ImGui::GapFillIcon) ||
        (old_tool != ImGui::GapFillIcon && new_tool != ImGui::GapFillIcon))
        return;

    for (auto& selector_ptr : m_triangle_selectors) {
        TriangleSelectorPatch* tsp = dynamic_cast<TriangleSelectorPatch*>(selector_ptr.get());
        tsp->set_filter_state(new_tool == ImGui::GapFillIcon);
    }
}

void GLGizmoFdmSupports::show_tooltip_information(float caption_max, float x, float y)
{
    ImTextureID normal_id = m_parent.get_gizmos_manager().get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TOOLBAR_TOOLTIP);
    ImTextureID hover_id  = m_parent.get_gizmos_manager().get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TOOLBAR_TOOLTIP_HOVER);

    float font_size = ImGui::GetFontSize();
    ImVec2 button_size = ImVec2(font_size * 1.8, font_size * 1.3);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::ImageButton3(normal_id, hover_id, button_size);

    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip2(ImVec2(x, y));
        auto draw_text_with_caption = [this, &caption_max](const wxString &caption, const wxString &text) {
            // BBS
            m_imgui->text_colored(ImGuiWrapper::COL_ACTIVE, caption);
            ImGui::SameLine(caption_max);
            m_imgui->text_colored(ImGuiWrapper::COL_WINDOW_BG, text);
        };

        for (const auto &t : std::array<std::string, 4>{"enforce", "block", "remove", "brush_size"}) draw_text_with_caption(m_desc.at(t + "_caption"), m_desc.at(t));
        ImGui::EndTooltip();
    }
    ImGui::PopStyleVar(1);
}

// BBS
int GLGizmoFdmSupports::get_selection_support_threshold_angle()
{
    auto sel_info = m_c->selection_info();
    if (sel_info == nullptr)
        return -1;

    const DynamicPrintConfig& obj_cfg = sel_info->model_object()->config.get();
    const DynamicPrintConfig& glb_cfg = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    bool enable_support = obj_cfg.option("enable_support") ? obj_cfg.opt_bool("enable_support") : glb_cfg.opt_bool("enable_support");
    SupportType support_type = obj_cfg.option("support_type") ? obj_cfg.opt_enum<SupportType>("support_type") : glb_cfg.opt_enum<SupportType>("support_type");
    int support_threshold_angle = obj_cfg.option("support_threshold_angle") ? obj_cfg.opt_int("support_threshold_angle") : glb_cfg.opt_int("support_threshold_angle");

    bool auto_support = enable_support &&
        (support_type == SupportType::stHybridAuto ||
         support_type == SupportType::stTreeAuto ||
         support_type == SupportType::stNormalAuto);
    return auto_support ? support_threshold_angle : 0;
}

void GLGizmoFdmSupports::select_facets_by_angle(float threshold_deg, bool block)
{
    float threshold = (float(M_PI)/180.f)*threshold_deg;
    const Selection& selection = m_parent.get_selection();
    const ModelObject* mo = m_c->selection_info()->model_object();
    const ModelInstance* mi = mo->instances[selection.get_instance_idx()];

    int mesh_id = -1;
    for (const ModelVolume* mv : mo->volumes) {
        if (! mv->is_model_part())
            continue;

        ++mesh_id;

        const Transform3d trafo_matrix = mi->get_matrix(true) * mv->get_matrix(true);
        Vec3f down  = (trafo_matrix.inverse() * (-Vec3d::UnitZ())).cast<float>().normalized();
        Vec3f limit = (trafo_matrix.inverse() * Vec3d(std::sin(threshold), 0, -std::cos(threshold))).cast<float>().normalized();

        float dot_limit = limit.dot(down);

        // Now calculate dot product of vert_direction and facets' normals.
        int idx = 0;
        const indexed_triangle_set &its = mv->mesh().its;
        for (const stl_triangle_vertex_indices &face : its.indices) {
            if (its_face_normal(its, face).dot(down) > dot_limit) {
                m_triangle_selectors[mesh_id]->set_facet(idx, block ? EnforcerBlockerType::BLOCKER : EnforcerBlockerType::ENFORCER);
                m_triangle_selectors.back()->request_update_render_data();
            }
            ++ idx;
        }
    }

    Plater::TakeSnapshot snapshot(wxGetApp().plater(), block ? "Block supports by angle"
                                                    : "Add supports by angle");
    update_model_object();
    m_parent.set_as_dirty();
}

//BBS: remove const
void GLGizmoFdmSupports::update_model_object()
{
    bool updated = false;
    ModelObject* mo = m_c->selection_info()->model_object();
    int idx = -1;
    for (ModelVolume* mv : mo->volumes) {
        if (! mv->is_model_part())
            continue;
        ++idx;
        updated |= mv->supported_facets.set(*m_triangle_selectors[idx].get());
    }

    if (updated) {
        const ModelObjectPtrs& mos = wxGetApp().model().objects;
        wxGetApp().obj_list()->update_info_items(std::find(mos.begin(), mos.end(), mo) - mos.begin());

        m_parent.post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));
    }

    //BBS: invalid volume_support status
    invalid_support_volumes(true);
}

//BBS
void GLGizmoFdmSupports::update_from_model_object(bool first_update)
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
        m_triangle_selectors.back()->deserialize(mv->supported_facets.get_data(), false);
        m_triangle_selectors.back()->request_update_render_data();

        //BBS: add timestamp logic
        m_volume_timestamps.emplace_back(mv->supported_facets.timestamp());
    }

    //BBS: invalid volume_support status
    invalid_support_volumes(true);
}

PainterGizmoType GLGizmoFdmSupports::get_painter_type() const
{
    return PainterGizmoType::FDM_SUPPORTS;
}

wxString GLGizmoFdmSupports::handle_snapshot_action_name(bool shift_down, GLGizmoPainterBase::Button button_down) const
{
    // BBS remove _L()
    wxString action_name;
    if (shift_down)
        action_name = ("Unselect all");
    else {
        if (button_down == Button::Left)
            action_name = ("Enforce supports");
        else
            action_name = ("Block supports");
    }
    return action_name;
}

//BBS
void GLGizmoFdmSupports::init_print_instance()
{
    const PrintObject* print_object = NULL;
    PrintInstance print_instance = { 0 };
    const Print *print = m_parent.fff_print();

    if (!m_c->selection_info() || (m_print_instance.print_object))
    {
        //no selection or already got a print instance before
        return;
    }
    const ModelObject* model_object = m_c->selection_info()->model_object();
    int instance_index = m_c->selection_info()->get_active_instance();
    const ModelInstance* model_instance = model_object->instances[instance_index];

    //check the print
    if (!print)
    {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",print invalid\n";
        return;
    }

    for (const PrintObject* object : print->objects())
    {
        if (object->model_object()->id() == model_object->id())
        {
            BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ",found a PrintObject, id is" << model_object->id().id;
            print_object = object;
            break;
        }
    }

    //check the pring object
    if (!print_object)
    {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",can not find a PrintObject\n";
        return;
    }

    //find the print instance
    for (const PrintInstance &instance : print_object->instances())
    {
        if (instance.model_instance->id() == model_instance->id())
        {
            BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ",found a PrintInstance, id is" << model_instance->id().id;
            m_print_instance = instance;
            break;
        }
    }

    //check the pring object
    if (!m_print_instance.print_object)
    {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",can not find a PrintInstance\n";
        return;
    }

    const PrintObjectConfig& config = m_print_instance.print_object->config();
    m_angle_threshold_deg = config.support_angle;
    m_is_tree_support = config.enable_support.value &&
        (config.support_type.value == stTreeAuto || config.support_type.value == stTree || config.support_type.value==stHybridAuto);
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",get support_angle "<< m_angle_threshold_deg<<", is_tree "<<m_is_tree_support;

    return;
}

void GLGizmoFdmSupports::invalid_support_volumes(bool invalid_step)
{
    std::unique_lock<std::mutex> lck(m_mutex);
    m_volume_valid = false;

    if ((invalid_step) && (m_edit_state == state_generating) && m_print_instance.print_object)
    {
        Print *print = m_print_instance.print_object->print();
        if (print) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "cancel the print";
            print->cancel();
        }
    }
    m_edit_state = state_idle;
    lck.unlock();

    return;
}

bool GLGizmoFdmSupports::need_regenerate_support_volumes()
{
    if (!m_support_volume)
        return true;

    const ModelObject* mo = m_c->selection_info()->model_object();

    if (m_object_id != m_print_instance.print_object->id().id)
    {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",object_id changed from " << m_object_id << " to " << m_print_instance.print_object->id().id << ", need to regenerate";
        return true;
    }

    int volume_id = -1;
    for (const ModelVolume* mv : mo->volumes) {
        if (! mv->is_model_part())
            continue;

        ++volume_id;
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",volume_id "<<volume_id<<", record_timestamp "<< m_volume_timestamps[volume_id]
                <<", current_timestamp "<<mv->supported_facets.timestamp();
        if (m_volume_timestamps[volume_id] != mv->supported_facets.timestamp())
        {
            return true;
        }
    }

    return false;
}

void GLGizmoFdmSupports::update_support_volumes()
{
    //PrintInstance m_print_instance = get_current_print_instance();

    if ((!m_print_instance.print_object))
    {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",invalid param, m_volume_ready="<< m_volume_ready;
        return;
    }

    if (m_volume_valid || !need_regenerate_support_volumes())
    {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",no need to regenerate support volume, return directly";

        std::unique_lock<std::mutex> lck(m_mutex);
        m_volume_ready = true;
        m_volume_valid = true;
        m_edit_state = state_ready;
        lck.unlock();
        return;
    }

    //generate_support_preview in async mode
    std::unique_lock<std::mutex> lck(m_mutex);
    m_volume_ready = false;
    //destroy previous support volume
    if (m_support_volume)
    {
        delete m_support_volume;
        m_support_volume = NULL;
    }
    lck.unlock();

    if (m_thread.joinable()) {
        //join the thread in ui thread
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "try to join thread for 100 ms";
        auto ret = m_thread.try_join_for(boost::chrono::milliseconds(100));
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "join thread returns "<<ret;
    }
    m_cancel = false;
    m_thread = create_thread([this]{this->run_thread();});    
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",created thread to generate support volumes";
    return;
}

void GLGizmoFdmSupports::run_thread()
{
    try {
        Print *print = m_print_instance.print_object->print();

        print->restart();
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",before generate_support_preview";
        m_print_instance.print_object->generate_support_preview();
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",after generate_support_preview";

        if (m_cancel)
        {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", cancelled";
            goto _finished;
        }

        std::unique_lock<std::mutex> lck(m_mutex);
        m_support_volume = new GLVolume(0.5f, 0.5f, 0.5f, 0.5f);
        //m_support_volume->is_support_part = true;
        m_support_volume->force_native_color = true;
        m_support_volume->set_render_color();
        lck.unlock();

        auto record_timestamp = [this]()
        {
            const ModelObject* mo = m_c->selection_info()->model_object();

            int volume_id = -1;
            for (const ModelVolume* mv : mo->volumes) {
                if (!mv->is_model_part())
                    continue;

                ++volume_id;
                m_volume_timestamps[volume_id] = mv->supported_facets.timestamp();
            }
        };

        if (m_cancel)
        {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", cancelled";
            goto _finished;
        }

        if (m_is_tree_support)
        {
            if (!m_print_instance.print_object->tree_support_layers().size())
            {
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",no tree support layer found, update status to 100%\n";
                print->set_status(100, L("Support Generated"));
                goto _finished;
            }
            for (const TreeSupportLayer *support_layer : m_print_instance.print_object->tree_support_layers())
            {
                for (const ExtrusionEntity *extrusion_entity : support_layer->support_fills.entities)
                {
                    _3DScene::extrusionentity_to_verts(extrusion_entity, float(support_layer->print_z), m_print_instance.shift, *m_support_volume);
                }
            }
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", finished extrusionentity_to_verts, update status to 100%";
            print->set_status(100, L("Support Generated"));
        }
        else
        {
            if (!m_print_instance.print_object->support_layers().size())
            {
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",no support layer found, update status to 100%\n";
                print->set_status(100, L("Support Generated"));
                goto _finished;
            }
            for (const SupportLayer *support_layer : m_print_instance.print_object->support_layers())
            {
                for (const ExtrusionEntity *extrusion_entity : support_layer->support_fills.entities)
                {
                    _3DScene::extrusionentity_to_verts(extrusion_entity, float(support_layer->print_z), m_print_instance.shift, *m_support_volume);
                }
            }
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", finished extrusionentity_to_verts, update status to 100%";
            print->set_status(100, L("Support Generated"));
        }
        record_timestamp();
    }
    catch (...) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ",exception catched, mostly cancelling from gui!";
        //wxTheApp->OnUnhandledException();
    }

_finished:
    std::unique_lock<std::mutex> lck(m_mutex);
    if (m_edit_state == state_generating)
        m_edit_state = state_ready;

    lck.unlock();
    m_parent.set_as_dirty();
    m_parent.post_event(SimpleEvent(wxEVT_PAINT));
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", finished all";
    return;
}

void GLGizmoFdmSupports::generate_support_volume()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",before finalize_geometry";
    m_support_volume->indexed_vertex_array.finalize_geometry(m_parent.is_initialized());

    std::unique_lock<std::mutex> lck(m_mutex);
    m_volume_ready = true;
    m_volume_valid = true;
    m_object_id = m_print_instance.print_object->id().id;
    lck.unlock();

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",finished finalize_geometry";
}

} // namespace Slic3r::GUI
