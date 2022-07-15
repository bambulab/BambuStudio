#include "GLGizmoSeam.hpp"

#include "libslic3r/Model.hpp"

//#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/Utils/UndoRedo.hpp"

#include <GL/glew.h>


namespace Slic3r::GUI {



void GLGizmoSeam::on_shutdown()
{
    m_parent.toggle_model_objects_visibility(true);
}



bool GLGizmoSeam::on_init()
{
    m_shortcut_key = WXK_CONTROL_P;

    m_desc["clipping_of_view"] = _L("Clipping of view") + ": ";
    m_desc["reset_direction"]  = _L("Reset direction");
    m_desc["cursor_size"]      = _L("Brush size") + ": ";
    m_desc["cursor_type"]      = _L("Brush shape") + ": ";
    m_desc["enforce_caption"]  = _L("Left mouse button") + ": ";
    m_desc["enforce"]          = _L("Enforce seam");
    m_desc["block_caption"]    = _L("Right mouse button") + ": ";
    m_desc["block"]            = _L("Block seam");
    m_desc["remove_caption"]   = _L("Shift + Left mouse button") + ": ";
    m_desc["remove"]           = _L("Remove selection");
    m_desc["remove_all"]       = _L("Remove all selection");
    m_desc["circle"]           = _L("Circle");
    m_desc["sphere"]           = _L("Sphere");

    return true;
}



std::string GLGizmoSeam::on_get_name() const
{
    return _u8L("Seam painting");
}



void GLGizmoSeam::render_painter_gizmo() const
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

void GLGizmoSeam::render_triangles(const Selection& selection) const
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

void GLGizmoSeam::on_render_input_window(float x, float y, float bottom_limit)
{
    if (! m_c->selection_info()->model_object())
        return;

    const float approx_height = m_imgui->scaled(12.5f);
    y = std::min(y, bottom_limit - approx_height);
    //BBS: GUI refactor: move gizmo to the right
#if BBS_TOOLBAR_ON_TOP
    m_imgui->set_next_window_pos(x, y, ImGuiCond_Always, 0.0f, 0.0f);
#else
    m_imgui->set_next_window_pos(x, y, ImGuiCond_Always, 1.0f, 0.0f);
#endif
    //m_imgui->set_next_window_pos(x, y, ImGuiCond_Always);

    //BBS
    ImGuiWrapper::push_toolbar_style();

    m_imgui->begin(get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);

    // First calculate width of all the texts that are could possibly be shown. We will decide set the dialog width based on that:
    const float clipping_slider_left = std::max(m_imgui->calc_text_size(m_desc.at("clipping_of_view")).x,
                                                m_imgui->calc_text_size(m_desc.at("reset_direction")).x)
                                           + m_imgui->scaled(1.5f);
    const float cursor_size_slider_left = m_imgui->calc_text_size(m_desc.at("cursor_size")).x + m_imgui->scaled(1.f);

    const float cursor_type_radio_left   = m_imgui->calc_text_size(m_desc["cursor_type"]).x + m_imgui->scaled(1.f);
    const float cursor_type_radio_sphere = m_imgui->calc_text_size(m_desc["sphere"]).x + m_imgui->scaled(2.5f);
    const float cursor_type_radio_circle = m_imgui->calc_text_size(m_desc["circle"]).x + m_imgui->scaled(2.5f);

    const float button_width = m_imgui->calc_text_size(m_desc.at("remove_all")).x + m_imgui->scaled(1.f);
    const float minimal_slider_width = m_imgui->scaled(4.f);

    float caption_max    = 0.f;
    float total_text_max = 0.f;
    for (const auto &t : std::array<std::string, 3>{"enforce", "block", "remove"}) {
        caption_max    = std::max(caption_max, m_imgui->calc_text_size(m_desc[t + "_caption"]).x);
        total_text_max = std::max(total_text_max, m_imgui->calc_text_size(m_desc[t]).x);
    }
    total_text_max += caption_max + m_imgui->scaled(1.f);
    caption_max    += m_imgui->scaled(1.f);

    const float sliders_left_width = std::max(cursor_size_slider_left, clipping_slider_left);
#if ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT
    const float slider_icon_width  = m_imgui->get_slider_icon_size().x;
    float       window_width       = minimal_slider_width + sliders_left_width + slider_icon_width;
#else
    float       window_width       = minimal_slider_width + sliders_left_width;
#endif // ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT
    window_width = std::max(window_width, total_text_max);
    window_width = std::max(window_width, button_width);
    window_width = std::max(window_width, cursor_type_radio_left + cursor_type_radio_sphere + cursor_type_radio_circle);

    auto draw_text_with_caption = [this, &caption_max](const wxString& caption, const wxString& text) {
        //BBS set text colored to BLUE_LIGHT
        m_imgui->text_colored(ImGuiWrapper::COL_BLUE_LIGHT, caption);
        ImGui::SameLine(caption_max);
        m_imgui->text(text);
    };

    for (const auto &t : std::array<std::string, 3>{"enforce", "block", "remove"})
        draw_text_with_caption(m_desc.at(t + "_caption"), m_desc.at(t));

    ImGui::Separator();

    const float max_tooltip_width = ImGui::GetFontSize() * 20.0f;

    ImGui::AlignTextToFramePadding();
    m_imgui->text(m_desc.at("cursor_size"));
    ImGui::SameLine(sliders_left_width);
#if ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT
    ImGui::PushItemWidth(window_width - sliders_left_width - slider_icon_width);
    m_imgui->slider_float("##cursor_radius", &m_cursor_radius, CursorRadiusMin, CursorRadiusMax, "%.2f", 1.0f, true, _L("Alt + Mouse wheel"));
#else
    ImGui::PushItemWidth(window_width - sliders_left_width);
    m_imgui->slider_float("##cursor_radius", &m_cursor_radius, CursorRadiusMin, CursorRadiusMax, "%.2f");
    if (ImGui::IsItemHovered())
        m_imgui->tooltip(_L("Alt + Mouse wheel"), max_tooltip_width);
#endif // ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT

    ImGui::AlignTextToFramePadding();
    m_imgui->text(m_desc.at("cursor_type"));

    float cursor_type_offset = cursor_type_radio_left + (window_width - cursor_type_radio_left - cursor_type_radio_sphere - cursor_type_radio_circle + m_imgui->scaled(0.5f)) / 2.f;
    ImGui::SameLine(cursor_type_offset);
    ImGui::PushItemWidth(cursor_type_radio_sphere);
    if (m_imgui->radio_button(m_desc["sphere"], m_cursor_type == TriangleSelector::CursorType::SPHERE))
        m_cursor_type = TriangleSelector::CursorType::SPHERE;

    if (ImGui::IsItemHovered())
        m_imgui->tooltip(_L("Paints all facets inside, regardless of their orientation."), max_tooltip_width);

    ImGui::SameLine(cursor_type_offset + cursor_type_radio_sphere);
    ImGui::PushItemWidth(cursor_type_radio_circle);
    if (m_imgui->radio_button(m_desc["circle"], m_cursor_type == TriangleSelector::CursorType::CIRCLE))
        m_cursor_type = TriangleSelector::CursorType::CIRCLE;

    if (ImGui::IsItemHovered())
        m_imgui->tooltip(_L("Ignores facets facing away from the camera."), max_tooltip_width);

    ImGui::Separator();
    if (m_c->object_clipper()->get_position() == 0.f) {
        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc.at("clipping_of_view"));
    }
    else {
        if (m_imgui->button(m_desc.at("reset_direction"))) {
            wxGetApp().CallAfter([this](){
                    m_c->object_clipper()->set_position(-1., false);
                });
        }
    }

    auto clp_dist = float(m_c->object_clipper()->get_position());
    ImGui::SameLine(sliders_left_width);
#if ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT
    ImGui::PushItemWidth(window_width - sliders_left_width - slider_icon_width);
    if (m_imgui->slider_float("##clp_dist", &clp_dist, 0.f, 1.f, "%.2f", 1.0f, true, _L("Ctrl + Mouse wheel")))
        m_c->object_clipper()->set_position(clp_dist, true);
#else
    ImGui::PushItemWidth(window_width - sliders_left_width);
    if (m_imgui->slider_float("##clp_dist", &clp_dist, 0.f, 1.f, "%.2f"))
        m_c->object_clipper()->set_position(clp_dist, true);

    if (ImGui::IsItemHovered())
        m_imgui->tooltip(_L("Ctrl + Mouse wheel"), max_tooltip_width);
#endif // ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT

    ImGui::Separator();
    if (m_imgui->button(m_desc.at("remove_all"))) {
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Reset selection", UndoRedo::SnapshotType::GizmoAction);
        ModelObject         *mo  = m_c->selection_info()->model_object();
        int                  idx = -1;
        for (ModelVolume *mv : mo->volumes)
            if (mv->is_model_part()) {
                ++idx;
                m_triangle_selectors[idx]->reset();
                m_triangle_selectors[idx]->request_update_render_data();
            }

        update_model_object();
        m_parent.set_as_dirty();
    }

    m_imgui->end();

    //BBS
    ImGuiWrapper::pop_toolbar_style();
}


//BBS: remove const
void GLGizmoSeam::update_model_object()
{
    bool updated = false;
    ModelObject* mo = m_c->selection_info()->model_object();
    int idx = -1;
    for (ModelVolume* mv : mo->volumes) {
        if (! mv->is_model_part())
            continue;
        ++idx;
        updated |= mv->seam_facets.set(*m_triangle_selectors[idx].get());
    }

    if (updated) {
        const ModelObjectPtrs& mos = wxGetApp().model().objects;
        wxGetApp().obj_list()->update_info_items(std::find(mos.begin(), mos.end(), mo) - mos.begin());

        // BBS: backup
        Slic3r::save_object_mesh(*mo);
        m_parent.post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));
    }
}


//BBS: add logic to distinguish the first_time_update and later_update
void GLGizmoSeam::update_from_model_object(bool first_update)
{
    wxBusyCursor wait;

    const ModelObject* mo = m_c->selection_info()->model_object();
    m_triangle_selectors.clear();

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
        m_triangle_selectors.back()->deserialize(mv->seam_facets.get_data(), false);
        m_triangle_selectors.back()->request_update_render_data();
    }
}


PainterGizmoType GLGizmoSeam::get_painter_type() const
{
    return PainterGizmoType::SEAM;
}

wxString GLGizmoSeam::handle_snapshot_action_name(bool shift_down, GLGizmoPainterBase::Button button_down) const
{
    wxString action_name;
    if (shift_down)
        action_name = _L("Remove selection");
    else {
        if (button_down == Button::Left)
            action_name = _L("Enforce seam");
        else
            action_name = _L("Block seam");
    }
    return action_name;
}

} // namespace Slic3r::GUI
