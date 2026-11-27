#include "GLGizmoSimplify.hpp"
#include "GLGizmoSimplifyDialog.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/Widgets/ProgressDialog.hpp"
#include "slic3r/GUI/NotificationManager.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/OpenGLManager.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/QuadricEdgeCollapse.hpp"

#include <GL/glew.h>
#include <wx/glcanvas.h>

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

GLGizmoSimplify::GLGizmoSimplify(GLCanvas3D & parent, unsigned int sprite_id)
    : GLGizmoBase(parent, sprite_id)
    , m_volume(nullptr)
    , m_show_wireframe(false)
{}

GLGizmoSimplify::~GLGizmoSimplify()
{
    stop_worker_thread_request();
    if (m_worker.joinable())
        m_worker.join();
    close_dialog();
    m_glmodel.reset();
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
    } else {
        return _u8L("Simplify");
    }
}

// Follows the selection of the gizmo. Returns true when the preview has to be (re)started.
bool GLGizmoSimplify::sync_with_selection()
{
    const Selection &selection = m_parent.get_selection();
    const ModelVolume *act_volume = get_model_volume(selection, wxGetApp().plater()->model());
    if (act_volume == nullptr) {
        stop_worker_thread_request();
        close();
        if (! m_parent.get_selection().is_single_volume()) {
            MessageDialog msg((wxWindow*)wxGetApp().mainframe,
                _L("Simplification is currently only allowed when a single part is selected"),
                _L("Error"));
            msg.ShowModal();
        }
        return false;
    }

    // Do not reselect object when processing
    if (act_volume == m_volume)
        return false;

    // select different model

    // close suggestion notification
    auto notification_manager = wxGetApp().plater()->get_notification_manager();
    const ObjectID oid = act_volume->get_object()->id();
    notification_manager->remove_simplify_suggestion_with_id(oid);
    if (std::find(m_hidden_suggestions.begin(), m_hidden_suggestions.end(), oid) == m_hidden_suggestions.end())
        m_hidden_suggestions.push_back(oid);

    m_volume = act_volume;
    // Keep the painting by default when the model has any, so the user has to opt out to lose it.
    m_preserve_color = has_any_painting(*m_volume);
    // Both methods start from their defaults for each newly selected model; the chosen
    // method itself is kept so switching models does not flip it.
    m_detail_level                  = 2;
    m_configuration.max_error       = 0.1f;
    m_configuration.decimate_ratio  = 50.;
    m_configuration.fix_count_by_ratio(m_volume->mesh().its.indices.size());
    init_model(m_volume->mesh().its);
    return true;
}

void GLGizmoSimplify::open_dialog()
{
    // Created hidden; show_dialog() shows it once on_render() has filled it in, so it never opens empty.
    if (! m_dialog)
        m_dialog = new SimplifyDialog(*this);
}

void GLGizmoSimplify::show_dialog()
{
    if (! m_dialog || m_dialog->IsShown())
        return;
    // At the top left of the 3D view, below the main toolbar (not centred on the view, where it would
    // cover the model).
    if (wxGLCanvas *canvas = m_parent.get_wxglcanvas(); canvas != nullptr) {
        const wxPoint origin = canvas->GetScreenPosition();
        m_dialog->Move(origin.x, origin.y + m_parent.get_main_toolbar_height());
    }
    // Without activating it: the 3D view keeps the keyboard focus, and its shortcuts, until the user
    // clicks into the dialog.
    m_dialog->ShowWithoutActivating();
}

void GLGizmoSimplify::close_dialog()
{
    if (m_dialog) {
        // Events that are still pending for the dialog must not reach this gizmo any more.
        m_dialog->detach();
        m_dialog->Destroy();
        m_dialog = nullptr;
    }
}

void GLGizmoSimplify::refresh_dialog()
{
    if (m_dialog == nullptr || m_volume == nullptr)
        return;

    SimplifyDialogView view;

    // Shorten by characters: cutting the UTF-8 bytes can split a character, and the label then stays empty.
    wxString name = wxString::FromUTF8(m_volume->name);
    if (name.length() > max_char_in_name)
        name = name.Left(max_char_in_name - 3) + "...";
    view.mesh_name = name;
    view.triangle_count = m_volume->mesh().its.indices.size();

    bool preview_current = false;
    bool result_ready    = false;
    {
        std::lock_guard lk(m_state_mutex);
        preview_current  = m_state.result && m_state.mv == m_volume && m_state.config == m_configuration;
        result_ready     = bool(m_state.result);
        view.worker_running = m_state.status == State::running;
        view.cancelling     = m_state.status == State::cancelling;
        view.progress       = m_state.progress;
    }
    view.result_count = preview_current ? wxString(std::to_string(m_triangle_count)) : wxString("-");

    view.use_count      = m_configuration.use_count;
    view.detail_level   = m_detail_level;
    view.decimate_ratio = m_configuration.decimate_ratio;
    view.show_wireframe = m_show_wireframe;
    view.painted        = has_any_painting(*m_volume);
    view.preserve_color = m_preserve_color;
    view.apply_enabled  = result_ready && ! view.worker_running && ! m_apply_pending;

    m_dialog->update(view);
}

void GLGizmoSimplify::refresh_dialog_progress()
{
    if (! m_dialog)
        return;
    int progress = 0;
    {
        std::lock_guard lk(m_state_mutex);
        progress = m_state.progress;
    }
    m_dialog->update_progress(progress);
}

// The dialog's events can still arrive while the gizmo closes (e.g. the value box losing the focus
// when the dialog goes away), so every handler checks that the gizmo is open. They also ignore values
// that did not change, so such an event never starts a new preview.

void GLGizmoSimplify::on_dialog_method_changed(bool use_count)
{
    if (GLGizmoBase::m_state != On || m_configuration.use_count == use_count)
        return;
    m_configuration.use_count = use_count;
    process();
    refresh_dialog();
}

void GLGizmoSimplify::on_dialog_detail_changed(int detail_level)
{
    detail_level = std::clamp(detail_level, 0, 4);
    if (GLGizmoBase::m_state != On || m_detail_level == detail_level)
        return;
    m_detail_level = detail_level;
    switch (m_detail_level) {
    case 0: m_configuration.max_error = 1e-3f; break;
    case 1: m_configuration.max_error = 1e-2f; break;
    case 2: m_configuration.max_error = 0.1f; break;
    case 3: m_configuration.max_error = 0.5f; break;
    case 4: m_configuration.max_error = 1.f; break;
    }
    process();
    refresh_dialog();
}

void GLGizmoSimplify::on_dialog_ratio_changed(float decimate_ratio)
{
    decimate_ratio = std::clamp(decimate_ratio, 0.f, 100.f);
    if (GLGizmoBase::m_state != On || m_volume == nullptr || m_configuration.decimate_ratio == decimate_ratio)
        return;
    m_configuration.decimate_ratio = decimate_ratio;
    m_configuration.fix_count_by_ratio(m_volume->mesh().its.indices.size());
    process();
    refresh_dialog();
}

void GLGizmoSimplify::on_dialog_wireframe_changed(bool show_wireframe)
{
    if (GLGizmoBase::m_state != On)
        return;
    m_show_wireframe = show_wireframe;
    request_rerender(true);
}

void GLGizmoSimplify::on_dialog_preserve_changed(bool preserve_color)
{
    if (GLGizmoBase::m_state != On)
        return;
    m_preserve_color = preserve_color;
    refresh_dialog();
}

void GLGizmoSimplify::on_dialog_apply()
{
    if (GLGizmoBase::m_state != On || m_apply_pending)
        return;
    m_apply_pending = true;
    refresh_dialog();
    // Defer the actual apply out of the wx event that raised it: apply_simplify() may pop a
    // modal progress dialog, which must not run nested inside the button handler.
    call_after_if_active([this]() { apply_simplify(); });
}

// close() toggles the gizmo, so it would open it again if it is already closed.
void GLGizmoSimplify::on_dialog_cancel()
{
    if (GLGizmoBase::m_state == On)
        close();
}

void GLGizmoSimplify::on_dialog_closed()
{
    if (GLGizmoBase::m_state == On)
        close();
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
    if (m_state.result)
        init_model(*m_state.result);
    if (m_state.config != m_configuration || m_state.mv != m_volume) {
        // Settings were changed, restart the worker immediately.
        process();
    }
    refresh_dialog();
    request_rerender(true);
}

void GLGizmoSimplify::process()
{
    if (m_volume == nullptr || m_volume->mesh().its.indices.empty())
        return;

    bool configs_match = false;
    bool result_valid  = false;
    bool is_worker_running = false;
    {
        std::lock_guard lk(m_state_mutex);
        configs_match = (m_volume == m_state.mv && m_state.config == m_configuration);
        result_valid = bool(m_state.result);
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

    // Copy configuration that will be used.
    m_state.config = m_configuration;
    m_state.mv = m_volume;
    m_state.status = State::running;

    // Create a copy of current mesh to pass to the worker thread.
    // Using unique_ptr instead of pass-by-value to avoid an extra
    // copy (which would happen when passing to std::thread).
    auto its = std::make_unique<indexed_triangle_set>(m_volume->mesh().its);

    m_worker = std::thread([this](std::unique_ptr<indexed_triangle_set> its) {

        // Checks that the UI thread did not request cancellation, throws if so.
        std::function<void(void)> throw_on_cancel = [this]() {
            std::lock_guard lk(m_state_mutex);
            if (m_state.status == State::cancelling)
                throw SimplifyCanceledException();
        };

        // Called by worker thread, updates progress bar.
        // Using CallAfter so the rerequest function is run in UI thread.
        std::function<void(int)> statusfn = [this](int percent) {
            std::lock_guard lk(m_state_mutex);
            m_state.progress = percent;
            call_after_if_active([this]() { refresh_dialog_progress(); request_rerender(); });
        };

        // Initialize.
        uint32_t triangle_count = 0;
        float    max_error = std::numeric_limits<float>::max();
        {
            std::lock_guard lk(m_state_mutex);
            if (m_state.config.use_count)
                triangle_count = m_state.config.wanted_count;
            if (! m_state.config.use_count)
                max_error = m_state.config.max_error;
            m_state.progress = 0;
            m_state.result.reset();
            m_state.status = State::Status::running;
        }

        // Start the actual calculation.
        try {
            its_quadric_edge_collapse(*its, triangle_count, &max_error, throw_on_cancel, statusfn);
        } catch (SimplifyCanceledException &) {
            BOOST_LOG_TRIVIAL(warning) << current_exception_message();
            std::lock_guard lk(m_state_mutex);
            m_state.status = State::idle;
        }

        std::lock_guard lk(m_state_mutex);
        if (m_state.status == State::Status::running) {
            // We were not cancelled, the result is valid.
            m_state.status = State::Status::idle;
            m_state.result = std::move(its);
        }

        // Update UI. Use CallAfter so the function is run on UI thread.
        call_after_if_active([this]() { worker_finished(); });
    }, std::move(its));
}

void GLGizmoSimplify::apply_simplify() {
    // Deferred entry point (see the Apply button): clear the guard up-front so a canceled
    // apply re-enables the button for a retry, while a successful apply closes the gizmo.
    m_apply_pending = false;
    refresh_dialog();

    const Selection& selection = m_parent.get_selection();
    int object_idx = selection.get_object_idx();

    auto plater = wxGetApp().plater();

    ModelVolume* mv = get_model_volume(selection, wxGetApp().model());
    if (mv == nullptr || !m_state.result)
        return;
    assert(mv == m_volume);

    // Preserve painting only when the user opted in AND the model actually has painting
    // (the checkbox is hidden otherwise). Everything else keeps the old fast path.
    const bool keep_paint = m_preserve_color && has_any_painting(*mv);

    if (keep_paint) {
        // Re-projecting the painted color/MMU/supports/seam/fuzzy-skin onto the decimated
        // mesh can be slow on dense/heavily-painted meshes. reproject_paint_keep() is
        // read-only - it computes into 'prepared' without touching the model - so cancelling
        // leaves everything untouched and no undo snapshot is taken. We copy the preview mesh
        // (not move) so a cancel can retry Apply immediately without recomputing.
        //
        // Run the reprojection on a worker thread and keep the UI thread free to pump the
        // (cancelable) progress dialog on a fixed cadence. Previously the reprojection ran
        // synchronously on the UI thread and the dialog was only pumped from its progress
        // callback (fired on each 1% step), so on dense meshes a single 1% batch could take
        // hundreds of ms and the Cancel click was not even seen until it finished. Pumping
        // every ~30 ms makes Cancel respond immediately regardless of reprojection cost.
        TriangleMesh new_mesh(*m_state.result);

        ProgressDialog progress_dlg(_L("Simplify"),
                                    _L("Transferring painting to the simplified mesh..."), 100,
                                    (wxWindow *) wxGetApp().mainframe,
                                    wxPD_APP_MODAL | wxPD_AUTO_HIDE | wxPD_CAN_ABORT);

        std::atomic<bool> canceled{false};
        std::atomic<bool> worker_done{false};
        std::atomic<int>  percent{1};
        std::mutex        stage_mutex;
        std::string       stage_text;
        PaintKeepPrepared prepared;
        bool              ok = false;

        std::thread worker([&]() {
            auto progress_cb = [&](int p, const char *stage) {
                percent.store(p < 1 ? 1 : (p > 99 ? 99 : p), std::memory_order_relaxed);
                if (stage != nullptr && stage[0] != '\0') {
                    std::lock_guard<std::mutex> lk(stage_mutex);
                    stage_text = stage;
                }
            };
            auto cancel_cb = [&]() { return canceled.load(std::memory_order_relaxed); };
            try {
                ok = mv->reproject_paint_keep(new_mesh, prepared, progress_cb, cancel_cb);
            } catch (...) {
                BOOST_LOG_TRIVIAL(warning) << current_exception_message();
                ok = false;
            }
            worker_done.store(true, std::memory_order_release);
        });

        // UI pump loop: refresh + process the Cancel button on a fixed cadence.
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

        if (!ok) {
            // Canceled by the user (or failed): model is unchanged and the preview is still
            // valid, so just keep the gizmo open and let the user tweak / retry.
            return;
        }
        // Slow part done - commit under a snapshot now.
        plater->take_snapshot(GUI::format("Simplify %1%", m_volume->name));
        prepared.mesh = std::move(new_mesh);
        mv->commit_mesh_keep_paint(std::move(prepared));
    } else {
        // No painting to keep (or user opted out): plain, fast mesh replacement.
        // set_mesh() swaps the geometry but leaves the annotation layers untouched, so the
        // old per-triangle ids would otherwise stay on the decimated mesh (the removed
        // clear_before_change_mesh() used to drop them). Reset only THIS volume's four
        // paint layers - not every volume of the object - so this part's stale paint is
        // cleared while the other parts keep theirs.
        plater->take_snapshot(GUI::format("Simplify %1%", m_volume->name));
        mv->supported_facets.reset();
        mv->seam_facets.reset();
        mv->mmu_segmentation_facets.reset();
        mv->fuzzy_skin_facets.reset();
        mv->set_mesh(std::move(*m_state.result));
    }
    m_state.result.reset();

    mv->calculate_convex_hull();
    mv->invalidate_convex_hull_2d();
    mv->set_new_unique_id();
    mv->get_object()->invalidate_bounding_box();
    mv->get_object()->ensure_on_bed();

    // fix hollowing, sla support points, modifiers, ...
    plater->changed_mesh(object_idx);
    // Fix warning icon in object list
    wxGetApp().obj_list()->update_item_error_icon(object_idx, -1);
    close();
}

bool GLGizmoSimplify::on_is_activable() const
{
    return m_parent.get_selection().is_single_full_object() ||
        m_parent.get_selection().is_single_volume();
}

void GLGizmoSimplify::on_set_state()
{
    // Closing gizmo. e.g. selecting another one
    if (GLGizmoBase::m_state == GLGizmoBase::Off) {
        m_parent.toggle_model_objects_visibility(true);

        stop_worker_thread_request();
        close_dialog();
        m_volume = nullptr; // invalidate selected model
        m_glmodel.reset();

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
        open_dialog();
        // when open by hyperlink it needs to show up
        request_rerender();
    }
}

void GLGizmoSimplify::request_rerender(bool force) {
    int64_t now = m_parent.timestamp_now();
    if (force || now > m_last_rerender_timestamp + 250) { // 250 ms
        set_dirty();
        m_parent.schedule_extra_frame(0);
        m_last_rerender_timestamp = now;
    }
}

void GLGizmoSimplify::init_model(const indexed_triangle_set& its)
{
    if (its.indices.empty())
        return;

    m_glmodel.reset();
    m_glmodel.init_from(its);
    m_parent.toggle_model_objects_visibility(true); // selected volume may have changed
    if (m_c) {
        const auto& p_selection_info = m_c->selection_info();
        if (p_selection_info) {
            const auto& p_model_object = p_selection_info->model_object();
            const auto& active_instance = p_selection_info->get_active_instance();
            m_parent.toggle_model_objects_visibility(false, p_model_object,
                active_instance, m_volume);
        }
    }

    if (const Selection&sel = m_parent.get_selection(); sel.get_volume_idxs().size() == 1)
        m_glmodel.set_color(-1, sel.get_volume(*sel.get_volume_idxs().begin())->color);
    m_triangle_count = its.indices.size();
}

void GLGizmoSimplify::on_render()
{
    if (sync_with_selection()) {
        process();
        refresh_dialog();
        show_dialog();
    }

    if (! m_glmodel.is_initialized())
        return;

    const auto& selection   = m_parent.get_selection();
    const auto& volume_idxs = selection.get_volume_idxs();
    if (volume_idxs.empty() || volume_idxs.size() != 1) return;
    const GLVolume *selected_volume = selection.get_volume(*volume_idxs.begin());

    // Check that the GLVolume still belongs to the ModelObject we work on.
    if (m_volume != get_model_volume(selection, wxGetApp().model()))
        return;

    const Transform3d trafo_matrix = selected_volume->world_matrix();

    const auto& gouraud_shader = wxGetApp().get_shader("gouraud_light");
    const GLboolean was_depth_test_enabled = glIsEnabled(GL_DEPTH_TEST);
    glsafe(::glEnable(GL_DEPTH_TEST));
    wxGetApp().bind_shader(gouraud_shader);
    const Camera& camera = wxGetApp().plater()->get_camera();
    const Transform3d view_model_matrix = camera.get_view_matrix() * trafo_matrix;
    gouraud_shader->set_uniform("view_model_matrix", view_model_matrix);
    gouraud_shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    gouraud_shader->set_uniform("normal_matrix", (Matrix3d)view_model_matrix.matrix().block(0, 0, 3, 3).inverse().transpose());
    m_glmodel.render_geometry();
    wxGetApp().unbind_shader();

    if (m_show_wireframe) {
        const auto& contour_shader = wxGetApp().get_shader("mm_contour");
        wxGetApp().bind_shader(contour_shader);
        contour_shader->set_uniform("view_model_matrix", view_model_matrix);
        contour_shader->set_uniform("projection_matrix", camera.get_projection_matrix());
        const auto& p_ogl_manager = wxGetApp().get_opengl_manager();
        p_ogl_manager->set_line_width(1.0f);
        glsafe(::glPolygonMode(GL_FRONT_AND_BACK, GL_LINE));
        //ScopeGuard offset_fill_guard([]() { glsafe(::glDisable(GL_POLYGON_OFFSET_FILL)); });
        //glsafe(::glEnable(GL_POLYGON_OFFSET_FILL));
        //glsafe(::glPolygonOffset(5.0, 5.0));
        m_glmodel.render_geometry();
        glsafe(::glPolygonMode(GL_FRONT_AND_BACK, GL_FILL));
        wxGetApp().unbind_shader();
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
