#include "GLGizmoSimplify.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/Widgets/ProgressDialog.hpp"
#include "slic3r/GUI/NotificationManager.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Selection.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/OpenGLManager.hpp"
#include "slic3r/GUI/GUI_Utils.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/QuadricEdgeCollapse.hpp"

#include <GL/glew.h>

#include <algorithm>
#include <thread>
#include <atomic>
#include <chrono>
#include <string>
#include "libslic3r/Log.hpp"

namespace Slic3r::GUI {

// Extend call after only when Simplify gizmo is still alive
static void call_after_if_active(std::function<void()> fn, GUI_App* app = &wxGetApp())
{
    // check application GUI
    if (app == nullptr) return;
    app->CallAfter([fn, app]() {
        // app must exist because it call this
        // if (app == nullptr) return;
        const Plater *plater = app->plater();
        if (plater == nullptr) return;
        const GLCanvas3D *canvas = plater->canvas3D();
        if (canvas == nullptr) return;
        const GLGizmosManager &mng = canvas->get_gizmos_manager();
        // check if simplify is still activ gizmo
        if (mng.get_current_type() != GLGizmosManager::Simplify) return;
        fn();
    });
}

static ModelVolume* get_model_volume(const Selection& selection, Model& model)
{
    const Selection::IndicesList& idxs = selection.get_volume_idxs();
    // only one selected volume
    if (idxs.size() != 1)
        return nullptr;
    const GLVolume* selected_volume = selection.get_volume(*idxs.begin());
    if (selected_volume == nullptr)
        return nullptr;

    const GLVolume::CompositeID& cid = selected_volume->composite_id;
    const ModelObjectPtrs& objs = model.objects;
    if (cid.object_id < 0 || objs.size() <= static_cast<size_t>(cid.object_id))
        return nullptr;
    const ModelObject* obj = objs[cid.object_id];
    if (cid.volume_id < 0 || obj->volumes.size() <= static_cast<size_t>(cid.volume_id))
        return nullptr;
    return obj->volumes[cid.volume_id];
}

static bool has_any_painting(const ModelVolume& mv)
{
    return mv.is_mm_painted() || mv.is_fdm_support_painted() || mv.is_seam_painted() || mv.is_fuzzy_skin_facets_painted();
}

static size_t count_simplify_parts(const ModelObject &object)
{
    size_t n = 0;
    for (const ModelVolume *mv : object.volumes)
        if (mv != nullptr && mv->is_model_part() && ! mv->mesh().its.indices.empty())
            ++n;
    return n;
}

bool GLGizmoSimplify::selection_is_simplify_parts(const Selection &selection)
{
    if (! selection.is_single_full_object())
        return false;
    const int object_idx = selection.get_object_idx();
    const Model *model = selection.get_model();
    if (model == nullptr || object_idx < 0 || size_t(object_idx) >= model->objects.size())
        return false;
    return count_simplify_parts(*model->objects[object_idx]) > 1;
}

std::vector<ModelVolume *> GLGizmoSimplify::collect_volumes(const Selection &selection, Model &model)
{
    std::vector<ModelVolume *> out;
    if (selection.is_single_volume() || selection.is_single_modifier()) {
        ModelVolume *mv = get_model_volume(selection, model);
        if (mv != nullptr && ! mv->mesh().its.indices.empty())
            out.push_back(mv);
        return out;
    }
    if (selection.is_single_full_object()) {
        const int object_idx = selection.get_object_idx();
        if (object_idx < 0 || size_t(object_idx) >= model.objects.size())
            return out;
        ModelObject *obj = model.objects[object_idx];
        for (ModelVolume *mv : obj->volumes)
            if (mv != nullptr && mv->is_model_part() && ! mv->mesh().its.indices.empty())
                out.push_back(mv);
        return out;
    }
    return out;
}

static const GLVolume *find_glvolume_for(const Selection &selection, const ModelVolume *mv)
{
    if (mv == nullptr || mv->get_object() == nullptr)
        return nullptr;
    const ModelObject *obj = mv->get_object();
    int vol_idx = -1;
    for (size_t i = 0; i < obj->volumes.size(); ++i)
        if (obj->volumes[i] == mv) {
            vol_idx = int(i);
            break;
        }
    if (vol_idx < 0)
        return nullptr;

    const Model *model = selection.get_model();
    int object_idx = -1;
    if (model != nullptr) {
        for (size_t i = 0; i < model->objects.size(); ++i)
            if (model->objects[i] == obj) {
                object_idx = int(i);
                break;
            }
    }
    if (object_idx < 0)
        return nullptr;

    for (unsigned int idx : selection.get_volume_idxs()) {
        const GLVolume *glv = selection.get_volume(idx);
        if (glv != nullptr && glv->object_idx() == object_idx && glv->volume_idx() == vol_idx)
            return glv;
    }
    return nullptr;
}

static bool volumes_equal(const std::vector<ModelVolume *> &a, const std::vector<ModelVolume *> &b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
}

static bool includes_equal(const std::vector<char> &a, const std::vector<char> &b)
{
    return a == b;
}

static bool results_ready_for_includes(const std::vector<char> &include,
                                       const std::vector<std::unique_ptr<indexed_triangle_set>> &results)
{
    if (include.size() != results.size())
        return false;
    bool any = false;
    for (size_t i = 0; i < include.size(); ++i) {
        if (! include[i])
            continue;
        any = true;
        if (! results[i])
            return false;
    }
    return any;
}

static int filament_number(const ModelVolume &mv)
{
    int eid = mv.extruder_id();
    return eid < 1 ? 1 : eid;
}

static ImVec4 filament_color_rgba(int filament_1based)
{
    const std::vector<std::string> colors =
        wxGetApp().plater()->get_extruder_colors_from_plater_config();
    if (filament_1based < 1 || colors.empty())
        return ImVec4(1.f, 1.f, 1.f, 1.f);
    const size_t idx = size_t(filament_1based - 1);
    if (idx >= colors.size())
        return ImVec4(1.f, 1.f, 1.f, 1.f);
    const std::array<float, 4> rgba = decode_color_to_float_array(colors[idx]);
    return ImVec4(rgba[0], rgba[1], rgba[2], rgba[3]);
}

static std::string truncate_name(const std::string &name, size_t max_chars)
{
    if (name.length() <= max_chars)
        return name;
    return name.substr(0, max_chars - 3) + "...";
}

GLGizmoSimplify::GLGizmoSimplify(GLCanvas3D & parent, unsigned int sprite_id)
    : GLGizmoBase(parent, sprite_id)
    , m_show_wireframe(false)
    , m_move_to_center(false)
    // translation for GUI size
    , tr_mesh_name(_u8L("Mesh name"))
    , tr_triangles(_u8L("Triangles"))
    , tr_detail_level(_u8L("Detail level"))
    , tr_decimate_ratio(_u8L("Decimate ratio"))
{}

GLGizmoSimplify::~GLGizmoSimplify()
{
    stop_worker_thread_request();
    if (m_worker.joinable())
        m_worker.join();
    m_glmodels.clear();
}

bool GLGizmoSimplify::on_esc_key_down() {
    return false;
    /*if (!m_is_worker_running)
        return false;
    stop_worker_thread_request();
    return true;*/
}

static bool needs_simplify_suggestion(const ModelObject &object)
{
    const size_t triangles_to_suggest_simplify = 1000000;
    return object.volumes.size() == 1 && object.volumes.front()->mesh().its.indices.size() >= triangles_to_suggest_simplify;
}

// The hyperlink looks the object up by ID when clicked, so it stays valid after objects
// before it are deleted or reordered.
void GLGizmoSimplify::push_simplify_suggestion(const ModelObject &object, NotificationManager &manager)
{
    std::string t = GUI::format(_L(
        "Processing model '%1%' with more than 1M triangles "
        "could be slow. It is highly recommended to simplify the model."), object.name);
    std::string hypertext = _u8L("Simplify model");

    const ObjectID oid = object.id();
    std::function<bool(wxEvtHandler *)> open_simplify =
        [oid](wxEvtHandler *) {
            auto plater = wxGetApp().plater();
            const ModelObjectPtrs &objects = plater->model().objects;
            const auto it = std::find_if(objects.begin(), objects.end(), [oid](const ModelObject *o) { return o->id() == oid; });
            if (it == objects.end()) return true;

            Selection &selection = plater->canvas3D()->get_selection();
            selection.clear();
            selection.add_object((unsigned int) (it - objects.begin()));

            auto &manager = plater->get_view3D_canvas3D()->get_gizmos_manager();
            bool  close_notification = true;
            if(!manager.open_gizmo(GLGizmosManager::Simplify))
                return close_notification;
            GLGizmoSimplify* simplify = dynamic_cast<GLGizmoSimplify*>(manager.get_current());
            if (simplify == nullptr) return close_notification;
            simplify->set_center_position();
            return close_notification;
        };
    manager.push_simplify_suggestion_notification(t, oid, hypertext, open_simplify);
}

// while opening needs GLGizmoSimplify to set window position
void GLGizmoSimplify::add_simplify_suggestion_notification(
    const std::vector<size_t> &object_ids,
    const std::vector<ModelObject*>&    objects,
    NotificationManager &      manager)
{
    for (size_t object_id : object_ids)
        if (object_id < objects.size() && needs_simplify_suggestion(*objects[object_id]))
            push_simplify_suggestion(*objects[object_id], manager);
}

std::string GLGizmoSimplify::get_icon_filename(bool is_dark_mode) const
{
    return "reduce_triangles.svg";
}

std::string GLGizmoSimplify::on_get_name() const
{
    if (!on_is_activable()) {
        return _u8L("Simplify") + ":\n" + _u8L("Please select single object.");
    }
    if (selection_is_simplify_parts(m_parent.get_selection()) || m_volumes.size() > 1)
        return _u8L("Simplify parts");
    return _u8L("Simplify");
}

void GLGizmoSimplify::on_render_input_window(float x, float y, float bottom_limit)
{
    create_gui_cfg();

    const Selection &selection = m_parent.get_selection();
    std::vector<ModelVolume *> act_volumes = collect_volumes(selection, wxGetApp().plater()->model());
    if (act_volumes.empty()) {
        stop_worker_thread_request();
        close();
        if (! selection.is_single_volume() && ! selection.is_single_full_object()) {
            MessageDialog msg((wxWindow*)wxGetApp().mainframe,
                _L("Simplification is currently only allowed when a single part is selected"),
                _L("Error"));
            msg.ShowModal();
        }
        return;
    }

    // Worker / result status is read later once m_include is known for this frame.
    bool is_cancelling = false;
    bool is_worker_running = false;
    bool is_result_ready = false;
    int progress = 0;

    // Whether to trigger calculation after rendering is done.
    bool start_process = false;


    // Check selection of new volume set
    // Do not reselect object when processing
    if (! volumes_equal(act_volumes, m_volumes)) {
        bool change_window_position = m_volumes.empty();
        // select different model

        // close suggestion notification
        auto notification_manager = wxGetApp().plater()->get_notification_manager();
        const ObjectID oid = act_volumes.front()->get_object()->id();
        notification_manager->remove_simplify_suggestion_with_id(oid);
        if (std::find(m_hidden_suggestions.begin(), m_hidden_suggestions.end(), oid) == m_hidden_suggestions.end())
            m_hidden_suggestions.push_back(oid);

        m_volumes = act_volumes;
        m_include.assign(m_volumes.size(), 1); // all parts included by default
        m_preserve_color = false; // default off for each newly selected model
        // Both methods start from their defaults for each newly selected model; the chosen
        // method itself is kept so switching models does not flip it.
        m_detail_level                  = 2;
        m_configuration.max_error       = 0.1f;
        m_configuration.decimate_ratio  = 50.;
        size_t total_triangles = 0;
        for (const ModelVolume *mv : m_volumes)
            total_triangles += mv->mesh().its.indices.size();
        m_configuration.fix_count_by_ratio(total_triangles);
        init_preview_models();

        // Start processing. If we switched from another object, process will
        // stop the background thread and it will restart itself later.
        start_process = true;

        // set window position
        const bool multi_parts = m_volumes.size() > 1;
        if (change_window_position && (m_move_to_center || multi_parts)) {
            // Multi-part dialog is large; always open centered so it stays on-screen.
            m_move_to_center = false;
            auto parent_size = m_parent.get_canvas_size();
            ImGui::SetNextWindowPos(
                ImVec2(parent_size.get_width() * 0.5f, parent_size.get_height() * 0.5f),
                ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        } else if (change_window_position) {
            ImVec2 pos = ImGui::GetMousePos();
            pos.x -= m_gui_cfg->window_offset_x;
            pos.y -= m_gui_cfg->window_offset_y;
            // minimal top left value
            ImVec2 tl(m_gui_cfg->window_padding, m_gui_cfg->window_padding + m_parent.get_main_toolbar_height());
            if (pos.x < tl.x) pos.x = tl.x;
            if (pos.y < tl.y) pos.y = tl.y;
            // maximal bottom right value
            auto parent_size = m_parent.get_canvas_size();
            ImVec2 br(
                parent_size.get_width() - (2 * m_gui_cfg->window_offset_x + m_gui_cfg->window_padding),
                parent_size.get_height() - (2 * m_gui_cfg->window_offset_y + m_gui_cfg->window_padding));
            if (pos.x > br.x) pos.x = br.x;
            if (pos.y > br.y) pos.y = br.y;
            ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        }
    }
    float space_size = m_imgui->get_style_scaling() * 8;
    float mesh_size = m_imgui->calc_text_size(_L("Mesh name")).x + m_imgui->scaled(2.0f);
    float triangle_size = m_imgui->calc_text_size(_L("Triangles")).x + m_imgui->scaled(2.0f);
    float text_left_width = std::max(triangle_size,mesh_size);

    float detail_size = m_imgui->calc_text_size(_L("Detail level")).x + m_imgui->scaled(2.0f);
    float decimate_size = m_imgui->calc_text_size(_L("Decimate ratio")).x + m_imgui->scaled(2.0f);
    float circle_size = m_imgui->scaled(2.0f);
    float bottom_left_width = std::max(detail_size, decimate_size) + circle_size;

    float slider_width = m_imgui->scaled(5.0f);

    if (m_include.size() != m_volumes.size())
        m_include.assign(m_volumes.size(), 1);

    m_imgui->push_common_window_style(m_parent.get_scale());
    int flag = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize |
               ImGuiWindowFlags_NoCollapse;
    m_imgui->begin(on_get_name(), flag);

    size_t orig_triangle_count = 0;
    for (size_t i = 0; i < m_volumes.size(); ++i) {
        if (! m_include[i])
            continue;
        orig_triangle_count += m_volumes[i]->mesh().its.indices.size();
    }

    // Per-part after counts from the worker; shown as soon as that included part finishes.
    std::vector<size_t> after_counts(m_volumes.size(), 0);
    std::vector<char>   after_ready(m_volumes.size(), 0);
    bool                all_after_ready = false;
    size_t              after_total = 0;
    {
        std::lock_guard lk(m_state_mutex);
        if (volumes_equal(m_state.volumes, m_volumes) && includes_equal(m_state.include, m_include)
            && m_state.config == m_configuration && m_state.results.size() == m_volumes.size()) {
            all_after_ready = results_ready_for_includes(m_include, m_state.results);
            for (size_t i = 0; i < m_volumes.size(); ++i) {
                if (! m_include[i])
                    continue;
                if (m_state.results[i]) {
                    after_ready[i]  = 1;
                    after_counts[i] = m_state.results[i]->indices.size();
                    after_total += after_counts[i];
                }
            }
        }
        is_result_ready = volumes_equal(m_state.volumes, m_volumes)
            && includes_equal(m_state.include, m_include)
            && m_state.config == m_configuration
            && results_ready_for_includes(m_include, m_state.results);
        is_worker_running = m_state.status == State::running;
        is_cancelling = m_state.status == State::cancelling;
        progress = m_state.progress;
    }

    const bool multi = m_volumes.size() > 1;
    if (multi) {
        const ModelObject *obj = m_volumes.front()->get_object();
        const std::string obj_name = truncate_name(obj != nullptr ? obj->name : std::string(), m_gui_cfg->max_char_in_name);
        const std::string header = GUI::format(_L("%1% (%2% parts)"), obj_name, m_volumes.size());
        m_imgui->text_colored(ImVec4(0.42f, 0.42f, 0.42f, 1.00f), header);

        const float canvas_w    = float(m_parent.get_canvas_size().get_width());
        const float canvas_h    = float(m_parent.get_canvas_size().get_height());
        const float list_w      = std::min(canvas_w * 0.45f, m_imgui->scaled(42.f));
        // Leave room for the controls under the table so the window fits on-screen when centered.
        const float below_table = m_imgui->scaled(20.f);
        const float list_h      = std::max(m_imgui->scaled(12.f),
                                          canvas_h * 0.5f - below_table);
        const float check_w     = ImGui::GetFrameHeight();
        const float count_w     = m_imgui->scaled(4.5f);
        const float filament_w  = m_imgui->scaled(5.5f);

        const ImGuiTableFlags table_flags =
            ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX;
        if (ImGui::BeginTable("##simplify_parts_table", 5, table_flags, ImVec2(list_w, list_h))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("##incl", ImGuiTableColumnFlags_WidthFixed, check_w);
            ImGui::TableSetupColumn(_u8L("Part").c_str(), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(_u8L("Filament").c_str(), ImGuiTableColumnFlags_WidthFixed, filament_w);
            ImGui::TableSetupColumn(_u8L("Before").c_str(), ImGuiTableColumnFlags_WidthFixed, count_w);
            ImGui::TableSetupColumn(_u8L("After").c_str(), ImGuiTableColumnFlags_WidthFixed, count_w);
            ImGui::TableHeadersRow();

            for (size_t i = 0; i < m_volumes.size(); ++i) {
                ImGui::TableNextRow();
                ImGui::PushID(int(i));

                ImGui::TableNextColumn();
                bool incl = m_include[i] != 0;
                if (ImGui::Checkbox("##incl", &incl)) {
                    m_include[i] = incl ? 1 : 0;
                    start_process = true;
                }

                ImGui::TableNextColumn();
                ImGui::TextUnformatted(m_volumes[i]->name.c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", m_volumes[i]->name.c_str());

                ImGui::TableNextColumn();
                const int filament = filament_number(*m_volumes[i]);
                const ImVec4 fcol = filament_color_rgba(filament);
                const ImVec2 swatch(ImGui::GetTextLineHeight(), ImGui::GetTextLineHeight());
                ImGui::ColorButton("##fcol", fcol,
                                   ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker |
                                       ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoDragDrop,
                                   swatch);
                ImGui::SameLine(0.f, space_size * 0.5f);
                ImGui::Text("%d", filament);

                ImGui::TableNextColumn();
                const size_t before = m_volumes[i]->mesh().its.indices.size();
                ImGui::Text("%s", std::to_string(before).c_str());

                ImGui::TableNextColumn();
                if (! m_include[i])
                    ImGui::Text("%s", "-");
                else if (after_ready[i])
                    ImGui::Text("%s", std::to_string(after_counts[i]).c_str());
                else
                    ImGui::Text("%s", "-");

                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        m_imgui->text(tr_triangles + ":");
        ImGui::SameLine(text_left_width + space_size);
        m_imgui->text_colored(ImVec4(0.42f, 0.42f, 0.42f, 1.00f), std::to_string(orig_triangle_count));
        m_imgui->text_colored(ImVec4(0.42f, 0.42f, 0.42f, 1.00f),
                              GUI::format(_L("Triangles after simplification: %1%"),
                                          all_after_ready ? std::to_string(after_total) : std::string("-")));
    } else {
        m_imgui->text(tr_mesh_name + ":");
        // BBS: somehow the calculated utf8 width is too narrow, have to add 35 here
        ImGui::SameLine(text_left_width + space_size);
        m_imgui->text_colored(ImVec4(0.42f, 0.42f, 0.42f, 1.00f),
                              truncate_name(m_volumes.front()->name, m_gui_cfg->max_char_in_name));

        m_imgui->text(tr_triangles + ":");
        ImGui::SameLine(text_left_width + space_size);
        m_imgui->text_colored(ImVec4(0.42f, 0.42f, 0.42f, 1.00f), std::to_string(orig_triangle_count));
    }

    ImGui::Separator();

    // Simplification method. The two methods are independent: each keeps its own setting and
    // only the selected one is shown and used for the preview.
    // render_combo places the box with SameLine(label_width), measured from the window edge, so the
    // label column includes the window padding (current cursor x). Combo width follows GLGizmoAssembly:
    // BBLBeginCombo draws the box two arrow widths narrower than the item width.
    const float method_label_width = ImGui::GetCursorPosX() + m_imgui->calc_text_size(_L("Simplification method")).x +
                                     m_imgui->calc_text_size("xx").x;
    std::vector<std::string> methods = { tr_detail_level, tr_decimate_ratio };
    size_t method_idx = m_configuration.use_count ? 1 : 0;
    const float method_text_width  = std::max(m_imgui->calc_text_size(_L("Detail level")).x,
                                              m_imgui->calc_text_size(_L("Decimate ratio")).x);
    const float method_combo_width = method_text_width * 1.2f + m_imgui->calc_text_size("xxx").x + m_imgui->scaled(3.0f);
    ImGuiWrapper::push_combo_style(m_parent.get_scale());
    if (render_combo(_u8L("Simplification method"), methods, method_idx, method_label_width, method_combo_width)) {
        m_configuration.use_count = method_idx == 1;
        start_process = true;
    }
    ImGuiWrapper::pop_combo_style();
    ImGui::Dummy(ImVec2(0.f, space_size * 0.5f));

    if (! m_configuration.use_count) {
        std::vector<std::string> reduce_captions = {
            static_cast<std::string>(_u8L("Extra high")),
            static_cast<std::string>(_u8L("High")),
            static_cast<std::string>(_u8L("Medium")),
            static_cast<std::string>(_u8L("Low")),
            static_cast<std::string>(_u8L("Extra low"))
        };
        ImGui::PushItemWidth(bottom_left_width + slider_width);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.81f, 0.81f, 0.81f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.81f, 0.81f, 0.81f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.81f, 0.81f, 0.81f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.00f, 0.68f, 0.26f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.00f, 0.68f, 0.26f, 1.00f));
        if (m_imgui->bbl_sliderin("##ReductionLevel", &m_detail_level, 0, 4, reduce_captions[std::clamp(m_detail_level, 0, 4)].c_str())) {
            m_detail_level = std::clamp(m_detail_level, 0, 4);
            switch (m_detail_level) {
            case 0: m_configuration.max_error = 1e-3f; break;
            case 1: m_configuration.max_error = 1e-2f; break;
            case 2: m_configuration.max_error = 0.1f; break;
            case 3: m_configuration.max_error = 0.5f; break;
            case 4: m_configuration.max_error = 1.f; break;
            }
            start_process = true;
        }
        ImGui::PopStyleColor(5);
        ImGui::PopItemWidth();
    } else {
        const char * format = (m_configuration.decimate_ratio > 10)? "%.0f %%":
            ((m_configuration.decimate_ratio > 1)? "%.1f %%":"%.2f %%");

        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0);
        ImGui::PushItemWidth(bottom_left_width + slider_width - space_size);
        if (m_imgui->bbl_slider_float_style("##decimate_ratio", &m_configuration.decimate_ratio, 0.f, 100.f, format)) {
            m_configuration.decimate_ratio = std::clamp(m_configuration.decimate_ratio, 0.f, 100.f);
            m_configuration.fix_count_by_ratio(orig_triangle_count);
            start_process = true;
        }
        ImGui::PopItemWidth();
        ImGui::SameLine();
        ImGui::PopStyleVar(1);

        ImGui::PushItemWidth(ImGui::CalcTextSize("100.00%").x + space_size);
        if (ImGui::BBLDragFloat("##decimate_ratio_input", &m_configuration.decimate_ratio, 0.05f, 0.0f, 0.0f, "%.2f%%")) {
            m_configuration.decimate_ratio = std::clamp(m_configuration.decimate_ratio, 0.f, 100.f);
            m_configuration.fix_count_by_ratio(orig_triangle_count);
            start_process = true;
        }
        ImGui::PopItemWidth();
    }

    if (! multi) {
        // Triangle count of the computed preview, the same for both methods. Shown only once the
        // preview matches the current settings, so it never shows a stale count.
        const std::string result_count = all_after_ready ? std::to_string(after_total) : std::string("-");
        m_imgui->text_colored(ImVec4(0.42f, 0.42f, 0.42f, 1.00f),
                              GUI::format(_L("Triangles after simplification: %1%"), result_count));
    }

    // Same checkbox look as the Mesh Boolean panel: wider gap between box and label.
    const ImVec2 original_inner_spacing = ImGui::GetStyle().ItemInnerSpacing;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(original_inner_spacing.x + 4.0f, original_inner_spacing.y));
    m_imgui->bbl_checkbox(_L("Show wireframe"), m_show_wireframe);

    // Only offer paint preservation when an included part actually has painting. When none do,
    // keep the old fast behavior (no checkbox, nothing to keep).
    bool painted = false;
    for (size_t i = 0; i < m_volumes.size(); ++i)
        if (m_include[i] && m_volumes[i] != nullptr && has_any_painting(*m_volumes[i])) {
            painted = true;
            break;
        }
    if (painted) {
        m_imgui->bbl_checkbox(_L("Keep all painting"), m_preserve_color);
        if (ImGui::IsItemHovered())
            m_imgui->tooltip(_u8L("Includes color, seam, support and fuzzy skin painting. If unchecked, all painting will be removed from the result."),
                             ImGui::GetFontSize() * 20.0f);
    } else
        m_preserve_color = false;
    ImGui::PopStyleVar(1);
    // Say it before Apply, not only in the hover tooltip: applying without the box drops all painting.
    if (painted && ! m_preserve_color)
        m_imgui->warning_text_wrapped(_L("Warning") + ": " +
                                          _L("All painting (color, seam, support and fuzzy skin) will be removed from the simplified model. "
                                             "Check \"Keep all painting\" to keep it."),
                                      ImGui::GetFontSize() * 20.0f);

    // The preview starts computing as soon as the window opens; label the bar so it is clear what runs.
    // Drawn above the bar (its row is shared with the Apply/Cancel buttons), and the line is kept
    // while idle so the auto-sized window does not jump each time a slider move restarts the preview.
    if (is_worker_running)
        ImGui::TextColored(ImVec4(0.42f, 0.42f, 0.42f, 1.00f), "%s", _u8L("Calculating simplified preview...").c_str());
    else
        ImGui::Dummy(ImVec2(0.f, ImGui::GetTextLineHeight()));

     // draw progress bar
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,12);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(0,0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(10,20));
    if (is_worker_running) { // apply or preview
        // draw progress bar
        std::string progress_text = GUI::format(_L("%1%"), std::to_string(progress)) + "%%";
        ImVec2 progress_size(bottom_left_width - space_size, 0.0f);
        ImGui::BBLProgressBar2(progress / 100., progress_size);
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(0.42f, 0.42f, 0.42f, 1.00f), progress_text.c_str());
        ImGui::SameLine(bottom_left_width + slider_width +  m_imgui->scaled(1.0f));
    } else {
        ImGui::Dummy(ImVec2(bottom_left_width - space_size, -1));
        ImGui::SameLine(bottom_left_width + slider_width +  m_imgui->scaled(1.0f));
    }
    ImGui::PopStyleVar(3);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,12);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(10,3));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(10,0));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.15f, 0.18f, 0.19f, 1.00f));

    m_imgui->disabled_begin(is_worker_running || ! is_result_ready || m_apply_pending ||
                            ! std::any_of(m_include.begin(), m_include.end(), [](char c) { return c != 0; }));
    m_imgui->push_confirm_button_style();
    if (m_imgui->bbl_button(_L("Apply"))) {
        // Defer the actual apply out of the GL/imgui render frame. apply_simplify() may pop a
        // modal progress dialog, and running a nested modal event loop inside the render
        // callback swallows the mouse-up event, leaving toolbar/other buttons unresponsive
        // afterwards. CallAfter runs it once the current event finishes.
        if (!m_apply_pending) {
            m_apply_pending = true;
            call_after_if_active([this]() { apply_simplify(); });
        }
    }
    else if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && is_worker_running) {
        ImGui::SetTooltip("%s", _u8L("Can't apply when process preview.").c_str());
    }
    m_imgui->pop_confirm_button_style();
    m_imgui->disabled_end(); // state !settings

    ImGui::SameLine();

    m_imgui->disabled_begin(is_cancelling);
    m_imgui->push_cancel_button_style();
    if (m_imgui->bbl_button(_L("Cancel"))) {
        close();
    }
    else if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && is_cancelling)
        ImGui::SetTooltip("%s", _u8L("Operation already cancelling. Please wait few seconds.").c_str());
    m_imgui->pop_cancel_button_style();
    m_imgui->disabled_end(); // state cancelling

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(1);

    m_imgui->end();
    m_imgui->pop_common_window_style();
    if (start_process)
        process();
}


void GLGizmoSimplify::close() {
    // close gizmo == open it again
    GLGizmosManager &gizmos_mgr = m_parent.get_gizmos_manager();
    gizmos_mgr.open_gizmo(GLGizmosManager::EType::Simplify);
}

void GLGizmoSimplify::stop_worker_thread_request()
{
    std::lock_guard lk(m_state_mutex);
    if (m_state.status == State::running)
        m_state.status = State::Status::cancelling;
}


// Following is called from a UI thread when the worker terminates
// worker calls it through a CallAfter.
void GLGizmoSimplify::worker_finished()
{
    {
        std::lock_guard lk(m_state_mutex);
        if (m_state.status == State::running) {
            // Someone started the worker again, before this callback
            // was called. Do nothing.
            return;
        }
    }
    if (m_worker.joinable())
        m_worker.join();
    if (GLGizmoBase::m_state == Off)
        return;
    bool all_ready = false;
    {
        std::lock_guard lk(m_state_mutex);
        all_ready = volumes_equal(m_state.volumes, m_volumes) && includes_equal(m_state.include, m_include)
            && m_state.config == m_configuration
            && results_ready_for_includes(m_include, m_state.results);
    }
    if (all_ready)
        init_preview_models();
    if (m_state.config != m_configuration || ! volumes_equal(m_state.volumes, m_volumes)
        || ! includes_equal(m_state.include, m_include)) {
        // Settings or include set were changed, restart the worker immediately.
        process();
    }
    request_rerender(true);
}

void GLGizmoSimplify::process()
{
    if (m_volumes.empty())
        return;
    if (m_include.size() != m_volumes.size())
        m_include.assign(m_volumes.size(), 1);
    for (const ModelVolume *mv : m_volumes)
        if (mv == nullptr || mv->mesh().its.indices.empty())
            return;
    if (! std::any_of(m_include.begin(), m_include.end(), [](char c) { return c != 0; })) {
        // Nothing to simplify: clear stale results so Apply stays disabled.
        {
            std::lock_guard lk(m_state_mutex);
            if (m_state.status == State::running)
                m_state.status = State::Status::cancelling;
            else {
                m_state.config  = m_configuration;
                m_state.volumes = m_volumes;
                m_state.include = m_include;
                m_state.results.clear();
                m_state.results.resize(m_volumes.size());
                m_state.status = State::idle;
                m_state.progress = 0;
            }
        }
        return;
    }

    bool configs_match = false;
    bool result_valid  = false;
    bool is_worker_running = false;
    {
        std::lock_guard lk(m_state_mutex);
        configs_match = volumes_equal(m_state.volumes, m_volumes) && includes_equal(m_state.include, m_include)
            && m_state.config == m_configuration;
        result_valid = configs_match && results_ready_for_includes(m_include, m_state.results);
        is_worker_running = m_state.status == State::running;
    }

    if ((result_valid || is_worker_running) && configs_match) {
        // Either finished or waiting for result already. Nothing to do.
        return;
    }

    if (is_worker_running && ! configs_match) {
        // Worker is running with outdated config. Stop it. It will
        // restart itself when cancellation is done.
        stop_worker_thread_request();
        return;
    }

    if (m_worker.joinable()) {
        // This can happen when process() is called after previous worker terminated,
        // but before the worker_finished callback was called. In this case, just join the thread,
        // the callback will check this and do nothing.
        m_worker.join();
    }

    // Copy configuration, volume list, and include mask that will be used.
    m_state.config = m_configuration;
    m_state.volumes = m_volumes;
    m_state.include = m_include;
    m_state.results.clear();
    m_state.results.resize(m_volumes.size());
    m_state.status = State::running;

    // Snapshot meshes for the worker so it does not read ModelVolume while the UI may mutate.
    struct PartInput {
        indexed_triangle_set its;
        size_t               orig_faces = 0;
        bool                 included = false;
    };
    auto inputs = std::make_shared<std::vector<PartInput>>();
    inputs->reserve(m_volumes.size());
    size_t total_faces = 0;
    for (size_t i = 0; i < m_volumes.size(); ++i) {
        PartInput p;
        p.included = m_include[i] != 0;
        if (p.included) {
            p.its = m_volumes[i]->mesh().its;
            p.orig_faces = p.its.indices.size();
            total_faces += p.orig_faces;
        }
        inputs->push_back(std::move(p));
    }
    const Configuration config = m_configuration;
    const size_t        total_faces_copy = total_faces;

    m_worker = std::thread([this, inputs, config, total_faces_copy]() {

        // Checks that the UI thread did not request cancellation, throws if so.
        std::function<void(void)> throw_on_cancel = [this]() {
            std::lock_guard lk(m_state_mutex);
            if (m_state.status == State::cancelling)
                throw SimplifyCanceledException();
        };

        size_t faces_done = 0;
        try {
            for (size_t i = 0; i < inputs->size(); ++i) {
                throw_on_cancel();
                if (! (*inputs)[i].included)
                    continue;

                uint32_t triangle_count = 0;
                float    max_error = std::numeric_limits<float>::max();
                if (config.use_count) {
                    // Same decimate ratio applied to each part's own triangle count.
                    Configuration per = config;
                    per.fix_count_by_ratio((*inputs)[i].orig_faces);
                    triangle_count = per.wanted_count;
                } else {
                    max_error = config.max_error;
                }

                {
                    std::lock_guard lk(m_state_mutex);
                    m_state.progress = total_faces_copy == 0 ? 0
                        : int(std::min<size_t>(99, (faces_done * 100) / total_faces_copy));
                    m_state.status = State::Status::running;
                }

                const size_t part_faces = (*inputs)[i].orig_faces;
                std::function<void(int)> statusfn = [this, faces_done, part_faces, total_faces_copy](int percent) {
                    std::lock_guard lk(m_state_mutex);
                    const size_t partial = faces_done + (part_faces * size_t(std::clamp(percent, 0, 100))) / 100;
                    m_state.progress = total_faces_copy == 0 ? 0
                        : int(std::min<size_t>(99, (partial * 100) / total_faces_copy));
                    call_after_if_active([this]() { request_rerender(); });
                };

                its_quadric_edge_collapse((*inputs)[i].its, triangle_count, &max_error, throw_on_cancel, statusfn);

                {
                    std::lock_guard lk(m_state_mutex);
                    if (m_state.status == State::Status::cancelling)
                        throw SimplifyCanceledException();
                    m_state.results[i] = std::make_unique<indexed_triangle_set>(std::move((*inputs)[i].its));
                }
                faces_done += part_faces;
                call_after_if_active([this]() { request_rerender(); });
            }

            std::lock_guard lk(m_state_mutex);
            if (m_state.status == State::Status::running) {
                m_state.status = State::Status::idle;
                m_state.progress = 100;
            }
        } catch (SimplifyCanceledException &) {
            BOOST_LOG_TRIVIAL(warning) << current_exception_message();
            std::lock_guard lk(m_state_mutex);
            m_state.status = State::idle;
            m_state.results.clear();
            m_state.results.resize(inputs->size());
        }

        // Update UI. Use CallAfter so the function is run on UI thread.
        call_after_if_active([this]() { worker_finished(); });
    });
}

void GLGizmoSimplify::apply_simplify() {
    // Deferred entry point (see the Apply button): clear the guard up-front so a canceled
    // apply re-enables the button for a retry, while a successful apply closes the gizmo.
    m_apply_pending = false;

    const Selection& selection = m_parent.get_selection();
    int object_idx = selection.get_object_idx();

    auto plater = wxGetApp().plater();

    if (m_volumes.empty() || object_idx < 0)
        return;
    if (m_include.size() != m_volumes.size())
        return;
    if (! std::any_of(m_include.begin(), m_include.end(), [](char c) { return c != 0; }))
        return;

    std::vector<std::unique_ptr<indexed_triangle_set>> results;
    {
        std::lock_guard lk(m_state_mutex);
        if (! volumes_equal(m_state.volumes, m_volumes) || ! includes_equal(m_state.include, m_include)
            || m_state.config != m_configuration
            || ! results_ready_for_includes(m_include, m_state.results))
            return;
        results.resize(m_state.results.size());
        for (size_t i = 0; i < m_state.results.size(); ++i)
            if (m_include[i] && m_state.results[i])
                results[i] = std::make_unique<indexed_triangle_set>(*m_state.results[i]);
    }

    // Painting only matters for included parts that actually carry paint.
    bool any_included_painted = false;
    for (size_t i = 0; i < m_volumes.size(); ++i)
        if (m_include[i] && m_volumes[i] != nullptr && has_any_painting(*m_volumes[i]))
            any_included_painted = true;
    const bool keep_paint = m_preserve_color && any_included_painted;

    std::vector<PaintKeepPrepared> prepared(m_volumes.size());
    std::vector<char>              keep_flags(m_volumes.size(), 0);

    if (keep_paint) {
        ProgressDialog progress_dlg(_L("Simplify"),
                                    _L("Transferring painting to the simplified mesh..."), 100,
                                    (wxWindow *) wxGetApp().mainframe,
                                    wxPD_APP_MODAL | wxPD_AUTO_HIDE | wxPD_CAN_ABORT);

        std::atomic<bool> canceled{false};
        std::atomic<bool> worker_done{false};
        std::atomic<int>  percent{1};
        std::mutex        stage_mutex;
        std::string       stage_text;
        bool              ok = true;

        std::thread worker([&]() {
            const size_t n = m_volumes.size();
            for (size_t i = 0; i < n; ++i) {
                if (canceled.load(std::memory_order_relaxed)) {
                    ok = false;
                    break;
                }
                if (! m_include[i])
                    continue;
                ModelVolume *mv = m_volumes[i];
                if (! has_any_painting(*mv) || ! results[i])
                    continue;
                keep_flags[i] = 1;
                TriangleMesh new_mesh(*results[i]);
                auto progress_cb = [&](int p, const char *stage) {
                    // Map each painted part into a slice of the overall bar.
                    const int base = int((i * 100) / n);
                    const int span = std::max(1, int(100 / n));
                    const int mapped = base + (std::clamp(p, 0, 100) * span) / 100;
                    percent.store(std::clamp(mapped, 1, 99), std::memory_order_relaxed);
                    if (stage != nullptr && stage[0] != '\0') {
                        std::lock_guard<std::mutex> lk(stage_mutex);
                        stage_text = stage;
                    }
                };
                auto cancel_cb = [&]() { return canceled.load(std::memory_order_relaxed); };
                try {
                    if (! mv->reproject_paint_keep(new_mesh, prepared[i], progress_cb, cancel_cb)) {
                        ok = false;
                        break;
                    }
                    prepared[i].mesh = std::move(new_mesh);
                } catch (...) {
                    BOOST_LOG_TRIVIAL(warning) << current_exception_message();
                    ok = false;
                    break;
                }
            }
            worker_done.store(true, std::memory_order_release);
        });

        while (!worker_done.load(std::memory_order_acquire)) {
            wxString msg;
            {
                std::lock_guard<std::mutex> lk(stage_mutex);
                msg = stage_text.empty() ? wxString(_L("Transferring painting to the simplified mesh..."))
                                         : wxString::FromUTF8(stage_text);
            }
            if (!progress_dlg.Update(percent.load(std::memory_order_relaxed), msg))
                canceled.store(true, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
        worker.join();

        if (!ok)
            return;
    }

    const ModelObject *obj = m_volumes.front()->get_object();
    const std::string  snap_name = obj != nullptr
        ? GUI::format("Simplify %1%", obj->name)
        : GUI::format("Simplify %1%", m_volumes.front()->name);
    plater->take_snapshot(snap_name);

    for (size_t i = 0; i < m_volumes.size(); ++i) {
        if (! m_include[i] || ! results[i])
            continue; // unchecked parts keep their original mesh and paint
        ModelVolume *mv = m_volumes[i];
        if (keep_flags[i]) {
            mv->commit_mesh_keep_paint(std::move(prepared[i]));
        } else {
            // set_mesh() swaps the geometry but leaves the annotation layers untouched, so the
            // old per-triangle ids would otherwise stay on the decimated mesh. Reset only THIS
            // volume's four paint layers.
            mv->supported_facets.reset();
            mv->seam_facets.reset();
            mv->mmu_segmentation_facets.reset();
            mv->fuzzy_skin_facets.reset();
            mv->set_mesh(std::move(*results[i]));
        }
        mv->calculate_convex_hull();
        mv->invalidate_convex_hull_2d();
        mv->set_new_unique_id();
    }

    {
        std::lock_guard lk(m_state_mutex);
        m_state.results.clear();
    }

    ModelObject *mo = m_volumes.front()->get_object();
    mo->invalidate_bounding_box();
    mo->ensure_on_bed();

    // fix hollowing, sla support points, modifiers, ...
    plater->changed_mesh(object_idx);
    // Fix warning icon in object list
    wxGetApp().obj_list()->update_item_error_icon(object_idx, -1);
    close();
}

bool GLGizmoSimplify::on_is_activable() const
{
    return m_parent.get_selection().is_single_full_object() ||
        m_parent.get_selection().is_single_volume() ||
        m_parent.get_selection().is_single_modifier();
}

void GLGizmoSimplify::on_set_state()
{
    // Closing gizmo. e.g. selecting another one
    if (GLGizmoBase::m_state == GLGizmoBase::Off) {
        m_parent.toggle_model_objects_visibility(true);

        stop_worker_thread_request();
        m_volumes.clear();
        m_include.clear();
        m_glmodels.clear();
        m_triangle_counts.clear();

        // Closed without simplifying (Cancel, Esc, another gizmo): bring the suggestion back for
        // objects that still need it. Simplified or deleted objects no longer qualify.
        if (! m_hidden_suggestions.empty()) {
            Plater *plater = wxGetApp().plater();
            if (plater != nullptr && plater->get_notification_manager() != nullptr) {
                for (const ObjectID &oid : m_hidden_suggestions)
                    for (const ModelObject *object : plater->model().objects)
                        if (object->id() == oid && needs_simplify_suggestion(*object))
                            push_simplify_suggestion(*object, *plater->get_notification_manager());
            }
            m_hidden_suggestions.clear();
        }
    } else if (GLGizmoBase::m_state == GLGizmoBase::On) {
        m_apply_pending = false; // fresh session: never start with a stale pending-apply guard
        // when open by hyperlink it needs to show up
        request_rerender();
    }
}

void GLGizmoSimplify::create_gui_cfg() {
    if (m_gui_cfg.has_value()) return;
    int space_size = m_imgui->calc_text_size(":MM").x;
    GuiCfg cfg;
    cfg.top_left_width = std::max(m_imgui->calc_text_size(tr_mesh_name).x,
                                  m_imgui->calc_text_size(tr_triangles).x)
        + space_size;

    const float radio_size = ImGui::GetFrameHeight();
    cfg.bottom_left_width =
        std::max(m_imgui->calc_text_size(tr_detail_level).x,
                 m_imgui->calc_text_size(tr_decimate_ratio).x) +
        space_size + radio_size;

    cfg.input_width   = cfg.bottom_left_width * 1.5;
    cfg.window_offset_x = (cfg.bottom_left_width + cfg.input_width)/2;
    cfg.window_offset_y = ImGui::GetTextLineHeightWithSpacing() * 5;

    m_gui_cfg = cfg;
}

void GLGizmoSimplify::request_rerender(bool force) {
    int64_t now = m_parent.timestamp_now();
    if (force || now > m_last_rerender_timestamp + 250) { // 250 ms
        set_dirty();
        m_parent.schedule_extra_frame(0);
        m_last_rerender_timestamp = now;
    }
}

void GLGizmoSimplify::set_center_position() {
    m_move_to_center = true;
}


void GLGizmoSimplify::init_preview_models()
{
    m_glmodels.clear();
    m_triangle_counts.assign(m_volumes.size(), 0);
    if (m_volumes.empty())
        return;

    m_parent.toggle_model_objects_visibility(true); // selected volumes may have changed

    std::vector<const indexed_triangle_set *> meshes(m_volumes.size(), nullptr);
    {
        std::lock_guard lk(m_state_mutex);
        if (volumes_equal(m_state.volumes, m_volumes) && includes_equal(m_state.include, m_include)
            && m_state.config == m_configuration && m_state.results.size() == m_volumes.size()) {
            for (size_t i = 0; i < m_volumes.size(); ++i)
                if (m_include[i] && m_state.results[i])
                    meshes[i] = m_state.results[i].get();
        }
    }
    // Fall back to the original meshes for excluded parts and until a preview exists.
    for (size_t i = 0; i < m_volumes.size(); ++i)
        if (meshes[i] == nullptr)
            meshes[i] = &m_volumes[i]->mesh().its;

    m_glmodels.resize(m_volumes.size());
    const Selection &selection = m_parent.get_selection();
    if (m_c) {
        const auto &p_selection_info = m_c->selection_info();
        if (p_selection_info) {
            const auto &p_model_object = p_selection_info->model_object();
            const auto &active_instance = p_selection_info->get_active_instance();
            for (ModelVolume *mv : m_volumes)
                m_parent.toggle_model_objects_visibility(false, p_model_object, active_instance, mv);
        }
    }

    for (size_t i = 0; i < m_volumes.size(); ++i) {
        if (meshes[i] == nullptr || meshes[i]->indices.empty())
            continue;
        m_glmodels[i].reset();
        m_glmodels[i].init_from(*meshes[i]);
        m_triangle_counts[i] = meshes[i]->indices.size();
        if (const GLVolume *glv = find_glvolume_for(selection, m_volumes[i]))
            m_glmodels[i].set_color(-1, glv->color);
    }
}

void GLGizmoSimplify::on_render()
{
    if (m_glmodels.empty() || m_volumes.empty())
        return;

    const auto& selection = m_parent.get_selection();
    // Drop a stale session if the selection no longer matches the volumes we are simplifying.
    const std::vector<ModelVolume *> current = collect_volumes(selection, wxGetApp().model());
    if (! volumes_equal(current, m_volumes))
        return;

    const auto& gouraud_shader = wxGetApp().get_shader("gouraud_light");
    const GLboolean was_depth_test_enabled = glIsEnabled(GL_DEPTH_TEST);
    glsafe(::glEnable(GL_DEPTH_TEST));
    const Camera& camera = wxGetApp().plater()->get_camera();

    for (size_t i = 0; i < m_glmodels.size(); ++i) {
        if (! m_glmodels[i].is_initialized())
            continue;
        const GLVolume *glv = find_glvolume_for(selection, m_volumes[i]);
        if (glv == nullptr)
            continue;
        const Transform3d trafo_matrix = glv->world_matrix();
        const Transform3d view_model_matrix = camera.get_view_matrix() * trafo_matrix;

        wxGetApp().bind_shader(gouraud_shader);
        gouraud_shader->set_uniform("view_model_matrix", view_model_matrix);
        gouraud_shader->set_uniform("projection_matrix", camera.get_projection_matrix());
        gouraud_shader->set_uniform("normal_matrix", (Matrix3d)view_model_matrix.matrix().block(0, 0, 3, 3).inverse().transpose());
        m_glmodels[i].render_geometry();
        wxGetApp().unbind_shader();

        if (m_show_wireframe) {
            const auto& contour_shader = wxGetApp().get_shader("mm_contour");
            wxGetApp().bind_shader(contour_shader);
            contour_shader->set_uniform("view_model_matrix", view_model_matrix);
            contour_shader->set_uniform("projection_matrix", camera.get_projection_matrix());
            const auto& p_ogl_manager = wxGetApp().get_opengl_manager();
            p_ogl_manager->set_line_width(1.0f);
            glsafe(::glPolygonMode(GL_FRONT_AND_BACK, GL_LINE));
            m_glmodels[i].render_geometry();
            glsafe(::glPolygonMode(GL_FRONT_AND_BACK, GL_FILL));
            wxGetApp().unbind_shader();
        }
    }

    if (!was_depth_test_enabled) {
        glsafe(::glDisable(GL_DEPTH_TEST));
    }
}


CommonGizmosDataID GLGizmoSimplify::on_get_requirements() const
{
    return CommonGizmosDataID(
        int(CommonGizmosDataID::SelectionInfo));
}


void GLGizmoSimplify::Configuration::fix_count_by_ratio(size_t triangle_count)
{
    if (decimate_ratio <= 0.f)
        wanted_count = static_cast<uint32_t>(triangle_count);
    else if (decimate_ratio >= 100.f)
        wanted_count = 0;
    else
        wanted_count = static_cast<uint32_t>(std::round(
            triangle_count * (100.f - decimate_ratio) / 100.f));
}

} // namespace Slic3r::GUI
