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
    return _u8L("Paint-on supports");
}



bool GLGizmoFdmSupports::on_init()
{
    m_shortcut_key = WXK_CONTROL_L;

    m_desc["clipping_of_view"] = _L("Clipping of view") + ": ";
    m_desc["reset_direction"]  = _L("Reset direction");
    m_desc["cursor_size"]      = _L("Brush size") + ": ";
    m_desc["cursor_type"]      = _L("Brush shape") + ": ";
    m_desc["enforce_caption"]  = _L("Left mouse button") + ": ";
    m_desc["enforce"]          = _L("Enforce supports");
    m_desc["block_caption"]    = _L("Right mouse button") + ": ";
    m_desc["block"]            = _L("Block supports");
    m_desc["remove_caption"]   = _L("Shift + Left mouse button") + ": ";
    m_desc["remove"]           = _L("Remove selection");
    m_desc["remove_all"]       = _L("Remove all selection");
    m_desc["circle"]           = _L("Circle");
    m_desc["sphere"]           = _L("Sphere");
    m_desc["pointer"]          = _L("Triangles");
    m_desc["highlight_by_angle"] = _L("Highlight overhang by angle");
    m_desc["enforce_button"]   = _L("Enforce");
    m_desc["cancel"]           = _L("Cancel");

    m_desc["tool_type"]        = _L("Tool type") + ": ";
    m_desc["tool_brush"]       = _L("Brush");
    m_desc["tool_smart_fill"]  = _L("Smart fill");

    m_desc["smart_fill_angle"] = _L("Smart fill angle");

    m_desc["split_triangles"]   = _L("Split triangles");
    m_desc["on_overhangs_only"] = _L("On overhangs only");
    //BBS: use overhang_threashold in the same view
    m_desc["overhang_threshold"] = _L("Overhang Threshold Angle") + ": ";
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



void GLGizmoFdmSupports::on_render_input_window(float x, float y, float bottom_limit)
{
    init_print_instance();
    if (! m_c->selection_info()->model_object())
        return;

    const float approx_height = m_imgui->scaled(23.f);
    y = std::min(y, bottom_limit - approx_height);
    //BBS: GUI refactor: move gizmo to the right
#if BBS_TOOLBAR_ON_TOP
    m_imgui->set_next_window_pos(x, y, ImGuiCond_Always, 0.5f, 0.0f);
#else
    m_imgui->set_next_window_pos(x, y, ImGuiCond_Always, 1.0f, 0.0f);
#endif

    //BBS
    ImGuiWrapper::push_toolbar_style();

    m_imgui->begin(get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);

    // First calculate width of all the texts that are could possibly be shown. We will decide set the dialog width based on that:
    //BBS: add overhang slider
    //const float overhang_slider_left = m_imgui->calc_text_size(m_desc.at("overhang_threshold")).x + m_imgui->scaled(1.5f);
    const float clipping_slider_left           = std::max(m_imgui->calc_text_size(m_desc.at("clipping_of_view")).x,
                                                          m_imgui->calc_text_size(m_desc.at("reset_direction")).x) + m_imgui->scaled(1.5f);
    const float cursor_slider_left             = m_imgui->calc_text_size(m_desc.at("cursor_size")).x + m_imgui->scaled(1.f);
    const float smart_fill_slider_left         = m_imgui->calc_text_size(m_desc.at("smart_fill_angle")).x + m_imgui->scaled(1.f);
    const float autoset_slider_label_max_width = m_imgui->scaled(7.5f);
    const float autoset_slider_left            = m_imgui->calc_text_size(m_desc.at("highlight_by_angle"), autoset_slider_label_max_width).x + m_imgui->scaled(1.f);

    const float cursor_type_radio_circle  = m_imgui->calc_text_size(m_desc["circle"]).x + m_imgui->scaled(2.5f);
    const float cursor_type_radio_sphere  = m_imgui->calc_text_size(m_desc["sphere"]).x + m_imgui->scaled(2.5f);
    const float cursor_type_radio_pointer = m_imgui->calc_text_size(m_desc["pointer"]).x + m_imgui->scaled(2.5f);

    const float button_width = m_imgui->calc_text_size(m_desc.at("remove_all")).x + m_imgui->scaled(1.f);
    const float button_enforce_width = m_imgui->calc_text_size(m_desc.at("enforce_button")).x;
    const float button_cancel_width = m_imgui->calc_text_size(m_desc.at("cancel")).x;
    const float buttons_width = std::max(button_enforce_width, button_cancel_width) + m_imgui->scaled(0.5f);
    const float minimal_slider_width = m_imgui->scaled(4.f);

    const float tool_type_radio_left       = m_imgui->calc_text_size(m_desc["tool_type"]).x + m_imgui->scaled(1.f);
    const float tool_type_radio_brush      = m_imgui->calc_text_size(m_desc["tool_brush"]).x + m_imgui->scaled(2.5f);
    const float tool_type_radio_smart_fill = m_imgui->calc_text_size(m_desc["tool_smart_fill"]).x + m_imgui->scaled(2.5f);

    const float split_triangles_checkbox_width   = m_imgui->calc_text_size(m_desc["split_triangles"]).x + m_imgui->scaled(2.5f);
    const float on_overhangs_only_checkbox_width = m_imgui->calc_text_size(m_desc["on_overhangs_only"]).x + m_imgui->scaled(2.5f);

    float caption_max    = 0.f;
    float total_text_max = 0.f;
    for (const auto &t : std::array<std::string, 3>{"enforce", "block", "remove"}) {
        caption_max    = std::max(caption_max, m_imgui->calc_text_size(m_desc[t + "_caption"]).x);
        total_text_max = std::max(total_text_max, m_imgui->calc_text_size(m_desc[t]).x);
    }
    total_text_max += caption_max + m_imgui->scaled(1.f);
    caption_max    += m_imgui->scaled(1.f);

    //BBS: consider overhang 
    //const float sliders_left_width = std::max(std::max(autoset_slider_left, smart_fill_slider_left), std::max(std::max(cursor_slider_left, clipping_slider_left), overhang_slider_left));
    const float sliders_left_width = std::max(std::max(autoset_slider_left, smart_fill_slider_left), std::max(cursor_slider_left, clipping_slider_left));
#if ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT
    const float slider_icon_width  = m_imgui->get_slider_icon_size().x;
    float       window_width       = minimal_slider_width + sliders_left_width + slider_icon_width;
#else
    float       window_width       = minimal_slider_width + sliders_left_width;
#endif // ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT
    window_width = std::max(window_width, total_text_max);
    window_width = std::max(window_width, button_width);
    window_width = std::max(window_width, split_triangles_checkbox_width);
    window_width = std::max(window_width, on_overhangs_only_checkbox_width);
    window_width = std::max(window_width, cursor_type_radio_circle + cursor_type_radio_sphere + cursor_type_radio_pointer);
    window_width = std::max(window_width, tool_type_radio_left + tool_type_radio_brush + tool_type_radio_smart_fill);
    window_width = std::max(window_width, 2.f * buttons_width + m_imgui->scaled(1.f));

    auto draw_text_with_caption = [this, &caption_max](const wxString& caption, const wxString& text) {
        //BBS
        m_imgui->text_colored(ImGuiWrapper::COL_BLUE_LIGHT, caption);
        ImGui::SameLine(caption_max);
        m_imgui->text(text);
    };

    for (const auto &t : std::array<std::string, 3>{"enforce", "block", "remove"})
        draw_text_with_caption(m_desc.at(t + "_caption"), m_desc.at(t));

    ImGui::Separator();

    // BBS
    const float max_tooltip_width = ImGui::GetFontSize() * 20.0f;
    std::string format_str = std::string("%.f") + I18N::translate_utf8("°",
        "Degree sign to use in the respective slider in FDM supports gizmo,"
        "placed after the number with no whitespace in between.");

#if 0
    float position_before_text_y = ImGui::GetCursorPos().y;
    ImGui::AlignTextToFramePadding();
    m_imgui->text_wrapped(m_desc["highlight_by_angle"] + ":", autoset_slider_label_max_width);
    ImGui::AlignTextToFramePadding();
    float position_after_text_y  = ImGui::GetCursorPos().y;
    ImGui::SameLine(sliders_left_width);

    float slider_height = m_imgui->get_slider_float_height();
    // Makes slider to be aligned to bottom of the multi-line text.
    float slider_start_position_y = std::max(position_before_text_y, position_after_text_y - slider_height);
    ImGui::SetCursorPosY(slider_start_position_y);

#if ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT
    ImGui::PushItemWidth(window_width - sliders_left_width - slider_icon_width);
    wxString tooltip = format_wxstr(_L("Preselects faces by overhang angle. It is possible to restrict paintable facets to only preselected faces when "
            "the option \"%1%\" is enabled."), m_desc["on_overhangs_only"]);
    if (m_imgui->slider_float("##angle_threshold_deg", &m_highlight_by_angle_threshold_deg, 0.f, 90.f, format_str.data(), 1.0f, true, tooltip)) {
#else
    ImGui::PushItemWidth(window_width - sliders_left_width);
    if (m_imgui->slider_float("##angle_threshold_deg", &m_highlight_by_angle_threshold_deg, 0.f, 90.f, format_str.data())) {
#endif // ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT
        m_parent.set_slope_normal_angle(90.f - m_highlight_by_angle_threshold_deg);
        if (! m_parent.is_using_slope()) {
            m_parent.use_slope(true);
            m_parent.set_as_dirty();
        }
    }

    // Restores the cursor position to be below the multi-line text.
    ImGui::SetCursorPosY(std::max(position_before_text_y + slider_height, position_after_text_y));

#if !ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT
    if (ImGui::IsItemHovered())
        m_imgui->tooltip(format_wxstr(_L("Preselects faces by overhang angle. It is possible to restrict paintable facets to only preselected faces when "
                                           "the option \"%1%\" is enabled."), m_desc["on_overhangs_only"]), max_tooltip_width);
#endif // !ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT

    m_imgui->disabled_begin(m_highlight_by_angle_threshold_deg == 0.f);
    ImGui::NewLine();
    ImGui::SameLine(window_width - 2.f*buttons_width - m_imgui->scaled(0.5f));
    if (m_imgui->button(m_desc["enforce_button"], buttons_width, 0.f)) {
        select_facets_by_angle(m_highlight_by_angle_threshold_deg, false);
        m_highlight_by_angle_threshold_deg = 0.f;
        m_parent.use_slope(false);
    }
    ImGui::SameLine(window_width - buttons_width);
    if (m_imgui->button(m_desc["cancel"], buttons_width, 0.f)) {
        m_highlight_by_angle_threshold_deg = 0.f;
        m_parent.use_slope(false);
    }
    m_imgui->disabled_end();
#endif

    ImGui::Separator();

    ImGui::AlignTextToFramePadding();
    m_imgui->text(m_desc["tool_type"]);

    float tool_type_offset = tool_type_radio_left + (window_width - tool_type_radio_left - tool_type_radio_brush - tool_type_radio_smart_fill + m_imgui->scaled(0.5f)) / 2.f;
    ImGui::SameLine(tool_type_offset);
    ImGui::PushItemWidth(tool_type_radio_brush);
    if (m_imgui->radio_button(m_desc["tool_brush"], m_tool_type == ToolType::BRUSH))
        m_tool_type = ToolType::BRUSH;


    if (ImGui::IsItemHovered())
        m_imgui->tooltip(_L("Paints facets according to the chosen painting brush."), max_tooltip_width);

    ImGui::SameLine(tool_type_offset + tool_type_radio_brush);
    ImGui::PushItemWidth(tool_type_radio_smart_fill);
    if (m_imgui->radio_button(m_desc["tool_smart_fill"], m_tool_type == ToolType::SMART_FILL))
        m_tool_type = ToolType::SMART_FILL;

    if (ImGui::IsItemHovered())
        m_imgui->tooltip(_L("Paints neighboring facets whose relative angle is less or equal to set angle."), max_tooltip_width);

    m_imgui->checkbox(m_desc["on_overhangs_only"], m_paint_on_overhangs_only);
    if (ImGui::IsItemHovered())
        m_imgui->tooltip(format_wxstr(_L("Allows painting only on facets selected by: \"%1%\""), m_desc["highlight_by_angle"]), max_tooltip_width);

    ImGui::Separator();

    if (m_tool_type == ToolType::BRUSH) {
        m_imgui->text(m_desc.at("cursor_type"));
        ImGui::NewLine();

        float cursor_type_offset = (window_width - cursor_type_radio_sphere - cursor_type_radio_circle - cursor_type_radio_pointer + m_imgui->scaled(1.5f)) / 2.f;
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

        ImGui::SameLine(cursor_type_offset + cursor_type_radio_sphere + cursor_type_radio_circle);
        ImGui::PushItemWidth(cursor_type_radio_pointer);

        if (m_imgui->radio_button(m_desc["pointer"], m_cursor_type == TriangleSelector::CursorType::POINTER))
            m_cursor_type = TriangleSelector::CursorType::POINTER;

        if (ImGui::IsItemHovered())
            m_imgui->tooltip(_L("Paints only one facet."), max_tooltip_width);

        m_imgui->disabled_begin(m_cursor_type != TriangleSelector::CursorType::SPHERE && m_cursor_type != TriangleSelector::CursorType::CIRCLE);

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

        m_imgui->checkbox(m_desc["split_triangles"], m_triangle_splitting_enabled);

        if (ImGui::IsItemHovered())
            m_imgui->tooltip(_L("Splits bigger facets into smaller ones while the object is painted."), max_tooltip_width);

        m_imgui->disabled_end();
    } else {
        assert(m_tool_type == ToolType::SMART_FILL);
        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc["smart_fill_angle"] + ":");

        ImGui::SameLine(sliders_left_width);
#if ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT
        ImGui::PushItemWidth(window_width - sliders_left_width - slider_icon_width);
        if (m_imgui->slider_float("##smart_fill_angle", &m_smart_fill_angle, SmartFillAngleMin, SmartFillAngleMax, format_str.data(), 1.0f, true, _L("Alt + Mouse wheel")))
#else
        ImGui::PushItemWidth(window_width - sliders_left_width);
        if (m_imgui->slider_float("##smart_fill_angle", &m_smart_fill_angle, SmartFillAngleMin, SmartFillAngleMax, format_str.data()))
#endif // ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT
            for (auto &triangle_selector : m_triangle_selectors) {
                triangle_selector->seed_fill_unselect_all_triangles();
                triangle_selector->request_update_render_data();
            }

#if !ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT
        if (ImGui::IsItemHovered())
            m_imgui->tooltip(_L("Alt + Mouse wheel"), max_tooltip_width);
#endif // !ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT
    }

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
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), _L("Reset selection"), UndoRedo::SnapshotType::GizmoAction);
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

    //BBS: add support preview button logic
    ImGui::SameLine(window_width - button_width);
    if (m_edit_state == state_idle)
    {
        if (m_imgui->button(_L("Support Preview"))) {
            std::unique_lock<std::mutex> lck(m_mutex);
            m_edit_state = state_generating;
            lck.unlock();

            update_support_volumes();
            m_generate_count = 0;
        }
    }
    else if (m_edit_state == state_generating)
    {
        if (m_imgui->button(_L(" Generating ... "))) {
            m_generate_count++;
        }
    }
    else
    {
        if (!m_volume_ready)
            generate_support_volume();
        if (m_imgui->button(_L(" Close Preview  "))) {
            std::unique_lock<std::mutex> lck(m_mutex);
            m_edit_state = state_idle;
            m_volume_ready = false;
            lck.unlock();
        }
    }

    m_imgui->end();

    //BBS
    ImGuiWrapper::pop_toolbar_style();
}


//BBS: remove the select facets by angle
/*void GLGizmoFdmSupports::select_facets_by_angle(float threshold_deg, bool block)
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

    Plater::TakeSnapshot snapshot(wxGetApp().plater(), block ? _L("Block supports by angle")
                                                    : _L("Add supports by angle"));
    update_model_object();
    m_parent.set_as_dirty();
}*/

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
    for (const ModelVolume* mv : mo->volumes) {
        if (! mv->is_model_part())
            continue;

        ++volume_id;

        // This mesh does not account for the possible Z up SLA offset.
        const TriangleMesh* mesh = &mv->mesh();

        m_triangle_selectors.emplace_back(std::make_unique<TriangleSelectorGUI>(*mesh));
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
    wxString action_name;
    if (shift_down)
        action_name = _L("Remove selection");
    else {
        if (button_down == Button::Left)
            action_name = _L("Add supports");
        else
            action_name = _L("Block supports");
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
    //m_angle_threshold_deg = config.support_material_angle;
    m_is_tree_support = config.support_material.value &&
        (config.support_type.value == stTreeAuto || config.support_type.value == stTree);
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",get support_material_angle "<< m_angle_threshold_deg<<", is_tree "<<m_is_tree_support;

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
                print->set_status(100, L("Support material Generated"));
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
            print->set_status(100, L("Support material Generated"));
        }
        else
        {
            if (!m_print_instance.print_object->support_layers().size())
            {
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",no support layer found, update status to 100%\n";
                print->set_status(100, L("Support material Generated"));
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
            print->set_status(100, L("Support material Generated"));
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
