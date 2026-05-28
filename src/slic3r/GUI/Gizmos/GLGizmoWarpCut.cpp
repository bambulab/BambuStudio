#include "GLGizmoWarpCut.hpp"

#include "ag/customcut/ColorCutIntegrationBridge.hpp"
#include "ag/warpcut/WarpCutCoordinator.hpp"
#include "ag/warpcut/WarpCutTransferBridge.hpp"
#include "libslic3r/CutUtils.hpp"
#include "libslic3r/Model.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Widgets/ProgressDialog.hpp"

#include <imgui/imgui.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
#include <thread>

namespace {

void append_curve_log(const std::string &message)
{
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    std::ofstream out("curve.log", std::ios::app);
    if (!out)
        return;
    out << message << std::endl;
}

std::string format_vec3(const Slic3r::Vec3d &value)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(4)
           << "(" << value.x() << ", " << value.y() << ", " << value.z() << ")";
    return stream.str();
}

Slic3r::Vec2d world_to_screen(const Slic3r::Vec3d& world, const Slic3r::GUI::Camera& camera)
{
    const Eigen::Vector4d clip = camera.get_projection_matrix().matrix() * camera.get_view_matrix().matrix() * Eigen::Vector4d(world.x(), world.y(), world.z(), 1.0);
    if (std::abs(clip.w()) < 1e-8)
        return Slic3r::Vec2d(std::numeric_limits<double>::max(), std::numeric_limits<double>::max());

    const Slic3r::Vec3d ndc = clip.head<3>() / clip.w();
    const std::array<int, 4>& viewport = camera.get_viewport();
    const double half_w = 0.5 * double(viewport[2]);
    const double half_h = 0.5 * double(viewport[3]);
    return {
        half_w * ndc.x() + double(viewport[0]) + half_w,
        half_h * ndc.y() + double(viewport[1]) + half_h
    };
}

const Slic3r::ModelVolume *find_single_warpcut_source_volume(const Slic3r::ModelObject &object)
{
    const Slic3r::ModelVolume *source_volume = nullptr;
    for (const Slic3r::ModelVolume *volume : object.volumes) {
        if (!volume->is_model_part() || volume->is_cut_connector())
            continue;

        if (source_volume != nullptr)
            return nullptr;

        source_volume = volume;
    }

    return source_volume;
}

void show_warpcut_warning(const Slic3r::ColorCut::ColorCutResult &result)
{
    if (result.warnings.empty())
        return;

    Slic3r::GUI::MessageDialog dlg(nullptr, wxString::FromUTF8(result.warnings.front().message.c_str()), _L("WarpCut"), wxOK | wxICON_WARNING);
    dlg.ShowModal();
}

void synchronize_model_after_cut(Slic3r::Model &model, const Slic3r::CutObjectBase &cut_id)
{
    for (Slic3r::ModelObject *obj : model.objects)
        if (obj->is_cut() && obj->cut_id.has_same_id(cut_id) && !obj->cut_id.is_equal(cut_id))
            obj->cut_id.copy(cut_id);
}

}

namespace Slic3r {
namespace GUI {

GLGizmoWarpCut::GLGizmoWarpCut(GLCanvas3D& parent, unsigned int sprite_id)
    : GLGizmoBase(parent, sprite_id)
{
}

std::string GLGizmoWarpCut::get_icon_filename(bool b_dark_mode) const
{
    return b_dark_mode ? "toolbar_warpcut_dark.svg" : "toolbar_warpcut.svg";
}

bool GLGizmoWarpCut::on_init()
{
    m_shortcut_key = 0;
    m_grid.offsets.assign(m_grid.size(), 0.0f);

    auto sphere_geometry = smooth_sphere(16, 1.0f);
    m_control_point_sphere.init_from(std::move(sphere_geometry), true);
    return true;
}

std::string GLGizmoWarpCut::on_get_name() const
{
    if (!on_is_activable() && m_state == EState::Off)
        return _u8L("WarpCut") + ":\n" + _u8L("Please select single object.");

    return _u8L("WarpCut");
}

bool GLGizmoWarpCut::on_is_activable() const
{
    return m_parent.get_selection().is_single_full_object();
}

void GLGizmoWarpCut::data_changed(bool)
{
    m_has_preview_state = false;
    m_preview_dirty = true;
}

void GLGizmoWarpCut::on_set_state()
{
    if (get_state() == On) {
        reset_from_selection();
    } else if (get_state() == Off) {
        m_selected_point_index = -1;
        m_preview_mode = false;
        m_parent.clear_all_volume_color_overrides();
    }
}

bool GLGizmoWarpCut::selection_box_changed() const
{
    const BoundingBoxf3 box = m_parent.get_selection().get_bounding_box();
    if (!box.defined)
        return false;

    return (box.center() - m_last_box_center).norm() > 1e-3 || (box.size() - m_last_box_size).norm() > 1e-3;
}

bool GLGizmoWarpCut::has_selected_point() const
{
    return m_selected_point_index >= 0 && size_t(m_selected_point_index) < m_grid.size();
}

float GLGizmoWarpCut::max_abs_offset() const
{
    const double max_extent = std::max(m_frame.half_extents.x(), m_frame.half_extents.y());
    return float(std::max(2.0, max_extent * 0.35));
}

bool GLGizmoWarpCut::can_perform_cut() const
{
    if (!on_is_activable() || !m_has_preview_state)
        return false;

    const Selection &selection = m_parent.get_selection();
    const Model *model = selection.get_model();
    const int object_idx = selection.get_object_idx();
    const int instance_idx = selected_instance_index();
    if (model == nullptr || object_idx < 0 || instance_idx < 0 || size_t(object_idx) >= model->objects.size())
        return false;

    const ModelObject *object = model->objects[size_t(object_idx)];
    return object != nullptr && find_single_warpcut_source_volume(*object) != nullptr;
}

int GLGizmoWarpCut::selected_instance_index() const
{
    const Selection &selection = m_parent.get_selection();
    const int instance_idx = selection.get_instance_idx();
    if (instance_idx >= 0)
        return instance_idx;

    const Model *model = selection.get_model();
    const int object_idx = selection.get_object_idx();
    if (model == nullptr || object_idx < 0 || size_t(object_idx) >= model->objects.size())
        return -1;

    const ModelObject *object = model->objects[size_t(object_idx)];
    return object != nullptr && object->instances.size() == 1 ? 0 : -1;
}

WarpCut::SurfaceDefinition GLGizmoWarpCut::build_warp_surface_definition() const
{
    WarpCut::SurfaceDefinition surface;
    surface.rows = m_grid.rows;
    surface.cols = m_grid.cols;
    surface.center = m_frame.center;
    surface.axis_u = m_frame.axis_u;
    surface.axis_v = m_frame.axis_v;
    surface.normal = m_frame.normal;
    surface.half_extents = m_frame.half_extents;
    surface.offsets = m_grid.offsets;
    return surface;
}

float GLGizmoWarpCut::sampled_offset(double u, double v) const
{
    if (m_grid.offsets.empty())
        return 0.0f;

    const double clamped_u = std::clamp(u, 0.0, 1.0);
    const double clamped_v = std::clamp(v, 0.0, 1.0);
    const double grid_x = clamped_u * double(m_grid.cols - 1);
    const double grid_y = clamped_v * double(m_grid.rows - 1);
    const size_t x0 = size_t(std::floor(grid_x));
    const size_t y0 = size_t(std::floor(grid_y));
    const size_t x1 = std::min(x0 + 1, m_grid.cols - 1);
    const size_t y1 = std::min(y0 + 1, m_grid.rows - 1);
    const double tx = grid_x - double(x0);
    const double ty = grid_y - double(y0);

    const auto offset_at = [this](size_t row, size_t col) {
        return m_grid.offsets[row * m_grid.cols + col];
    };

    const double top = double(offset_at(y0, x0)) * (1.0 - tx) + double(offset_at(y0, x1)) * tx;
    const double bottom = double(offset_at(y1, x0)) * (1.0 - tx) + double(offset_at(y1, x1)) * tx;
    return float(top * (1.0 - ty) + bottom * ty);
}

Vec3d GLGizmoWarpCut::surface_position(double u, double v) const
{
    const double u_local = (u - 0.5) * 2.0 * m_frame.half_extents.x();
    const double v_local = (v - 0.5) * 2.0 * m_frame.half_extents.y();
    return m_frame.center
        + m_frame.axis_u * u_local
        + m_frame.axis_v * v_local
        + m_frame.normal * double(sampled_offset(u, v));
}

Vec3d GLGizmoWarpCut::surface_normal(double u, double v) const
{
    const double step = 1.0 / double(std::max<size_t>(m_preview_surface_resolution - 1, 4));
    const Vec3d du = surface_position(std::min(1.0, u + step), v) - surface_position(std::max(0.0, u - step), v);
    const Vec3d dv = surface_position(u, std::min(1.0, v + step)) - surface_position(u, std::max(0.0, v - step));
    Vec3d normal = du.cross(dv);
    if (normal.squaredNorm() < 1e-8)
        return m_frame.normal;
    if (normal.dot(m_frame.normal) < 0.0)
        normal = -normal;
    return normal.normalized();
}

void GLGizmoWarpCut::mark_preview_for_refresh()
{
    m_preview_dirty = true;
    set_dirty();
    m_parent.request_extra_frame();
}

void GLGizmoWarpCut::perform_cut()
{
    ensure_preview_state();
    if (!can_perform_cut())
        return;

    const Selection &selection = m_parent.get_selection();
    Model *model = selection.get_model();
    const int object_idx = selection.get_object_idx();
    const int instance_idx = selected_instance_index();
    if (model == nullptr || object_idx < 0 || instance_idx < 0 || size_t(object_idx) >= model->objects.size())
        return;

    ModelObject *cut_object = model->objects[size_t(object_idx)];
    if (cut_object == nullptr)
        return;

    {
        std::ostringstream stream;
        stream << "[WarpCut] perform_cut begin"
               << " object='" << cut_object->name << "'"
               << " object_idx=" << object_idx
               << " instance_idx=" << instance_idx
               << " frame_center=" << format_vec3(m_frame.center)
               << " frame_normal=" << format_vec3(m_frame.normal)
               << " half_extents=" << format_vec3(Vec3d(m_frame.half_extents.x(), m_frame.half_extents.y(), 0.0));
        append_curve_log(stream.str());
    }

    // --- Progress tracking state shared between worker thread and UI ---
    struct CutProgress {
        std::mutex              mutex;
        std::condition_variable cv;
        float                   fraction{0.0f};
        std::string             phase{"Preparing..."};
        bool                    updated{false};
        std::atomic<bool>       canceled{false};
        std::atomic<bool>       finished{false};
    };
    auto progress = std::make_shared<CutProgress>();

    // Build request with progress/cancel callbacks
    Slic3r::WarpCut::WarpCutCoordinator coordinator;
    Slic3r::WarpCut::Request request;
    request.object = cut_object;
    request.object_index = object_idx;
    request.instance_index = instance_idx;
    request.attributes = ModelObjectCutAttribute::KeepUpper | ModelObjectCutAttribute::KeepLower;
    request.enable_warnings = true;
    request.uniform_cap_color = m_single_cut_surface_color;
    request.surface = build_warp_surface_definition();

    request.progress_cb = [progress](float frac, const char *phase_name) {
        std::lock_guard<std::mutex> lk(progress->mutex);
        progress->fraction = frac;
        if (phase_name) progress->phase = phase_name;
        progress->updated = true;
        progress->cv.notify_all();
    };
    request.cancel_cb = [progress]() -> bool {
        return progress->canceled.load();
    };

    // Result storage
    std::optional<Slic3r::ColorCut::ColorCutResult> result;

    // Capture source volume info before launching worker
    const ModelVolume *source_volume = find_single_warpcut_source_volume(*cut_object);
    Slic3r::ColorCut::ObjectAppearanceSnapshot snapshot;
    if (source_volume != nullptr) {
        snapshot = Slic3r::ColorCut::IntegrationBridge::build_object_snapshot(*cut_object);
    }

    // --- Launch worker thread ---
    auto start_time = std::chrono::steady_clock::now();
    std::thread worker([&]() {
        try {
            // Phase 1: Boolean cut (0% - 65%)
            result = coordinator.execute(request);

            if (progress->canceled.load()) {
                progress->finished = true;
                return;
            }

            // Phase 2: Attribute transfer (65% - 85%)
            if (result.has_value() && result->handled && !result->new_objects.empty() && source_volume != nullptr) {
                {
                    std::lock_guard<std::mutex> lk(progress->mutex);
                    progress->fraction = 0.70f;
                    progress->phase = "Transferring colors";
                    progress->updated = true;
                    progress->cv.notify_all();
                }

                Slic3r::WarpCut::TransferBridge::reapply_from_single_source(snapshot, *cut_object, *source_volume, instance_idx, result->new_objects, m_single_cut_surface_color, request.surface);
                append_curve_log("[WarpCut] perform_cut finished reapply_from_single_source");

                if (progress->canceled.load()) {
                    progress->finished = true;
                    return;
                }

                // Phase 3: Post-normalization (85% - 95%)
                {
                    std::lock_guard<std::mutex> lk(progress->mutex);
                    progress->fraction = 0.85f;
                    progress->phase = "Finalizing geometry";
                    progress->updated = true;
                    progress->cv.notify_all();
                }

                for (ModelObject *new_obj : result->new_objects) {
                    const BoundingBoxf3 bb = new_obj->full_raw_mesh_bounding_box();
                    const Vec3d shift = -bb.center();
                    for (ModelVolume *vol : new_obj->volumes) {
                        TriangleMesh mesh = vol->mesh();
                        mesh.translate(shift.cast<float>());
                        vol->set_mesh(std::move(mesh));
                        vol->calculate_convex_hull();
                    }
                    new_obj->origin_translation += shift;
                    for (ModelInstance *inst : new_obj->instances)
                        inst->set_offset(inst->get_offset() - shift);
                    new_obj->invalidate_bounding_box();
                    new_obj->ensure_on_bed();
                    for (ModelInstance *inst : new_obj->instances)
                        inst->set_assemble_transformation(inst->get_transformation());

                    {
                        std::ostringstream stream;
                        stream << "[WarpCut] post_normalize object='" << new_obj->name << "'"
                               << " shift=" << format_vec3(shift);
                        for (const auto *vol : new_obj->volumes) {
                            if (!vol->is_model_part()) continue;
                            const auto vbb = vol->mesh().bounding_box();
                            stream << " vol_mesh_z=[" << std::fixed << std::setprecision(4)
                                   << vbb.min.z() << "," << vbb.max.z() << "]";
                        }
                        stream << " origin_t=" << format_vec3(new_obj->origin_translation);
                        if (!new_obj->instances.empty())
                            stream << " inst_z=" << std::fixed << std::setprecision(4) << new_obj->instances[0]->get_offset().z();
                        stream << " get_min_z=" << std::fixed << std::setprecision(4) << new_obj->get_min_z();
                        append_curve_log(stream.str());
                    }
                }
                append_curve_log("[WarpCut] perform_cut finished post-color normalization");
            }

            {
                std::lock_guard<std::mutex> lk(progress->mutex);
                progress->fraction = 1.0f;
                progress->phase = "Complete";
                progress->updated = true;
                progress->cv.notify_all();
            }
        } catch (const std::exception &e) {
            append_curve_log(std::string("[WarpCut] perform_cut worker exception: ") + e.what());
        }
        progress->finished = true;
        progress->cv.notify_all();
    });

    // --- UI progress dialog loop ---
    wxWindow *parent_window = wxGetApp().plater();
    Slic3r::GUI::ProgressDialog progress_dialog(
        _L("WarpCut"),
        _L("Preparing cut operation..."),
        100,
        parent_window,
        wxPD_APP_MODAL | wxPD_CAN_ABORT);

    while (!progress->finished.load()) {
        {
            std::unique_lock<std::mutex> lk(progress->mutex);
            progress->cv.wait_for(lk, std::chrono::milliseconds(80),
                [&]{ return progress->updated || progress->finished.load(); });
        }

        float frac;
        std::string phase;
        {
            std::lock_guard<std::mutex> lk(progress->mutex);
            frac = progress->fraction;
            phase = progress->phase;
            progress->updated = false;
        }

        const int pct = std::clamp(int(frac * 100.0f), 0, 99);
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        double elapsed_sec = std::chrono::duration<double>(elapsed).count();
        wxString msg;
        if (frac > 0.02f && elapsed_sec > 2.0) {
            double est_total = elapsed_sec / double(frac);
            double remaining = std::max(0.0, est_total - elapsed_sec);
            int min = int(remaining) / 60;
            int sec = int(remaining) % 60;
            if (min > 0)
                msg = wxString::Format("%s  (%dm %02ds remaining)", phase, min, sec);
            else
                msg = wxString::Format("%s  (%ds remaining)", phase, sec);
        } else {
            msg = wxString::FromUTF8(phase);
        }

        if (!progress_dialog.Update(pct, msg)) {
            // User clicked Cancel
            progress->canceled = true;
            append_curve_log("[WarpCut] perform_cut canceled by user");
        }

        wxGetApp().Yield(); // keep UI responsive
    }

    worker.join();

    // If canceled, close dialog and return
    if (progress->canceled.load()) {
        progress_dialog.Update(99, _L("Canceled"));
        wxGetApp().Yield();
        progress_dialog.EndModal(wxID_OK);
        if (result.has_value())
            result->new_objects.clear();
        return;
    }

    if (!result.has_value()) {
        progress_dialog.Update(99, _L("Done"));
        wxGetApp().Yield();
        progress_dialog.EndModal(wxID_OK);
        return;
    }

    show_warpcut_warning(*result);
    if (!result->handled || result->new_objects.empty()) {
        progress_dialog.Update(99, _L("Done"));
        wxGetApp().Yield();
        progress_dialog.EndModal(wxID_OK);
        return;
    }

    // Keep dialog open through ALL post-processing
    progress_dialog.Update(92, _L("Applying results..."));
    wxGetApp().Yield();

    auto *plater = wxGetApp().plater();
    const CutObjectBase cut_id = cut_object->cut_id;
    plater->apply_cut_object_to_model(object_idx, result->new_objects);
    synchronize_model_after_cut(plater->model(), cut_id);

    // Force the deferred background processing that would normally fire
    // on a 500ms timer — do it NOW while the dialog is still visible.
    progress_dialog.Update(96, _L("Updating scene..."));
    wxGetApp().Yield();
    plater->update(false, true);

    progress_dialog.Update(99, _L("Done"));
    wxGetApp().Yield();
    progress_dialog.EndModal(wxID_OK);
}

void GLGizmoWarpCut::reset_from_selection()
{
    const BoundingBoxf3 box = m_parent.get_selection().get_bounding_box();
    if (!box.defined)
        return;

    m_frame.center = box.center();
    m_frame.axis_u = Vec3d::UnitX();
    m_frame.axis_v = Vec3d::UnitY();
    m_frame.normal = Vec3d::UnitZ();
    m_frame.half_extents = {
        std::max(10.0, box.size().x() * 1.10),
        std::max(10.0, box.size().y() * 1.10)
    };

    m_grid.offsets.assign(m_grid.size(), 0.0f);
    m_last_box_center = box.center();
    m_last_box_size = box.size();
    m_selected_point_index = m_grid.size() > 0 ? int((m_grid.rows / 2) * m_grid.cols + (m_grid.cols / 2)) : -1;
    m_edit_step = std::clamp(float(box.radius() * 0.03), 0.2f, 5.0f);
    m_initial_center = box.center();
    m_vertical_offset = 0.0f;
    m_tilt_angles = Vec3d::Zero();
    m_preview_mode = false;
    m_has_preview_state = true;
    m_preview_dirty = true;
}

void GLGizmoWarpCut::ensure_preview_state()
{
    if (!on_is_activable())
        return;

    if (!m_has_preview_state || selection_box_changed())
        reset_from_selection();

    if (m_preview_dirty)
        rebuild_preview_models();
}

void GLGizmoWarpCut::apply_selected_point_delta(float delta)
{
    if (!has_selected_point())
        return;

    const size_t point_index = size_t(m_selected_point_index);
    const float limit = max_abs_offset();
    m_grid.offsets[point_index] = std::clamp(m_grid.offsets[point_index] + delta, -limit, limit);
    smooth_offsets_around(point_index);
    mark_preview_for_refresh();
}

void GLGizmoWarpCut::smooth_offsets_around(size_t center_index)
{
    if (m_grid.offsets.empty() || m_smoothing_radius <= 0)
        return;

    const size_t center_row = center_index / m_grid.cols;
    const size_t center_col = center_index % m_grid.cols;
    const int radius = std::max(1, m_smoothing_radius);
    const float strength = std::clamp(m_smoothing_strength, 0.0f, 1.0f);
    const float limit = max_abs_offset();
    const std::vector<float> old_offsets = m_grid.offsets;

    for (int row = std::max(0, int(center_row) - radius); row <= std::min(int(m_grid.rows) - 1, int(center_row) + radius); ++row) {
        for (int col = std::max(0, int(center_col) - radius); col <= std::min(int(m_grid.cols) - 1, int(center_col) + radius); ++col) {
            const size_t current_index = size_t(row) * m_grid.cols + size_t(col);
            if (current_index == center_index)
                continue;

            const double distance = std::hypot(double(row) - double(center_row), double(col) - double(center_col));
            if (distance > double(radius) + 0.001)
                continue;

            float neighborhood_sum = 0.0f;
            float neighborhood_weight = 0.0f;
            for (int neighbor_row = std::max(0, row - 1); neighbor_row <= std::min(int(m_grid.rows) - 1, row + 1); ++neighbor_row) {
                for (int neighbor_col = std::max(0, col - 1); neighbor_col <= std::min(int(m_grid.cols) - 1, col + 1); ++neighbor_col) {
                    const size_t neighbor_index = size_t(neighbor_row) * m_grid.cols + size_t(neighbor_col);
                    const bool is_diagonal = neighbor_row != row && neighbor_col != col;
                    const float weight = is_diagonal ? 0.7f : 1.0f;
                    neighborhood_sum += old_offsets[neighbor_index] * weight;
                    neighborhood_weight += weight;
                }
            }

            if (neighborhood_weight <= 0.0f)
                continue;

            const float blended_average = neighborhood_sum / neighborhood_weight;
            const float influence = float(1.0 - distance / double(radius + 1));
            const float blend = std::clamp(strength * influence, 0.0f, 1.0f);
            m_grid.offsets[current_index] = std::clamp(old_offsets[current_index] + (blended_average - old_offsets[current_index]) * blend, -limit, limit);
        }
    }
}

Vec3d GLGizmoWarpCut::control_point_position(size_t index) const
{
    const size_t row = index / m_grid.cols;
    const size_t col = index % m_grid.cols;
    const double u = m_grid.cols > 1 ? double(col) / double(m_grid.cols - 1) : 0.5;
    const double v = m_grid.rows > 1 ? double(row) / double(m_grid.rows - 1) : 0.5;
    const double u_local = (u - 0.5) * 2.0 * m_frame.half_extents.x();
    const double v_local = (v - 0.5) * 2.0 * m_frame.half_extents.y();

    return m_frame.center
        + m_frame.axis_u * u_local
        + m_frame.axis_v * v_local
        + m_frame.normal * double(m_grid.offsets[index]);
}

void GLGizmoWarpCut::apply_tilt_to_frame()
{
    // Start from default orientation
    Vec3d axis_u = Vec3d::UnitX();
    Vec3d axis_v = Vec3d::UnitY();
    Vec3d normal = Vec3d::UnitZ();

    // Apply tilt rotations: X (pitch around axis_u), Y (pitch around axis_v), Z (roll around normal)
    const double rx = m_tilt_angles.x() * M_PI / 180.0;
    const double ry = m_tilt_angles.y() * M_PI / 180.0;
    const double rz = m_tilt_angles.z() * M_PI / 180.0;

    const Eigen::AngleAxisd rot_x(rx, Vec3d::UnitX());
    const Eigen::AngleAxisd rot_y(ry, Vec3d::UnitY());
    const Eigen::AngleAxisd rot_z(rz, Vec3d::UnitZ());
    const Eigen::Quaterniond combined = rot_z * rot_y * rot_x;

    m_frame.axis_u = combined * axis_u;
    m_frame.axis_v = combined * axis_v;
    m_frame.normal = combined * normal;

    // Apply vertical offset
    m_frame.center = m_initial_center + Vec3d(0.0, 0.0, double(m_vertical_offset));
}

void GLGizmoWarpCut::rebuild_preview_models()
{
    const size_t surface_resolution = std::max<size_t>(m_preview_surface_resolution, 5);

    GLModel::Geometry surface_geo;
    surface_geo.format.type = GLModel::PrimitiveType::Triangles;
    surface_geo.format.vertex_layout = GLModel::Geometry::EVertexLayout::P3N3;
    surface_geo.reserve_vertices(surface_resolution * surface_resolution);
    surface_geo.reserve_indices((surface_resolution - 1) * (surface_resolution - 1) * 6);

    GLModel::Geometry line_geo;
    line_geo.format.type = GLModel::PrimitiveType::Lines;
    line_geo.format.vertex_layout = GLModel::Geometry::EVertexLayout::P3;
    line_geo.reserve_vertices(m_grid.size());
    line_geo.reserve_indices((m_grid.rows * (m_grid.cols - 1) + m_grid.cols * (m_grid.rows - 1)) * 2);

    for (size_t row = 0; row < surface_resolution; ++row) {
        for (size_t col = 0; col < surface_resolution; ++col) {
            const double u = surface_resolution > 1 ? double(col) / double(surface_resolution - 1) : 0.5;
            const double v = surface_resolution > 1 ? double(row) / double(surface_resolution - 1) : 0.5;
            const Vec3f position = surface_position(u, v).cast<float>();
            const Vec3f normal = surface_normal(u, v).cast<float>();
            surface_geo.add_vertex(position, normal);
        }
    }

    for (size_t row = 0; row + 1 < surface_resolution; ++row) {
        for (size_t col = 0; col + 1 < surface_resolution; ++col) {
            const unsigned int top_left = unsigned(row * surface_resolution + col);
            const unsigned int top_right = top_left + 1;
            const unsigned int bottom_left = unsigned((row + 1) * surface_resolution + col);
            const unsigned int bottom_right = bottom_left + 1;

            surface_geo.add_triangle(top_left, bottom_left, top_right);
            surface_geo.add_triangle(top_right, bottom_left, bottom_right);
        }
    }

    for (size_t index = 0; index < m_grid.size(); ++index) {
        const Vec3f position = control_point_position(index).cast<float>();
        line_geo.add_vertex(position);
    }

    for (size_t row = 0; row < m_grid.rows; ++row) {
        for (size_t col = 0; col + 1 < m_grid.cols; ++col) {
            const unsigned int left = unsigned(row * m_grid.cols + col);
            line_geo.add_line(left, left + 1);
        }
    }
    for (size_t row = 0; row + 1 < m_grid.rows; ++row) {
        for (size_t col = 0; col < m_grid.cols; ++col) {
            const unsigned int top = unsigned(row * m_grid.cols + col);
            line_geo.add_line(top, top + unsigned(m_grid.cols));
        }
    }

    m_preview_surface.reset();
    m_preview_surface.init_from(std::move(surface_geo));
    m_preview_lines.reset();
    m_preview_lines.init_from(std::move(line_geo));
    m_preview_dirty = false;
}

int GLGizmoWarpCut::pick_control_point(const wxMouseEvent& mouse_event) const
{
    const Camera& camera = wxGetApp().plater()->get_camera();
    const std::array<int, 4>& viewport = camera.get_viewport();
    const Vec2d mouse_ss(double(mouse_event.GetX()), double(viewport[1] + viewport[3] - mouse_event.GetY()));

    int best_index = -1;
    double best_distance_sq = 20.0 * 20.0;
    for (size_t index = 0; index < m_grid.size(); ++index) {
        const Vec2d point_ss = world_to_screen(control_point_position(index), camera);
        const double distance_sq = (point_ss - mouse_ss).squaredNorm();
        if (distance_sq < best_distance_sq) {
            best_distance_sq = distance_sq;
            best_index = int(index);
        }
    }

    return best_index;
}

bool GLGizmoWarpCut::on_mouse(const wxMouseEvent& mouse_event)
{
    if (!on_is_activable() || !mouse_event.LeftDown() || mouse_event.CmdDown())
        return false;

    ensure_preview_state();
    const int picked_index = pick_control_point(mouse_event);
    if (picked_index < 0)
        return false;

    m_selected_point_index = picked_index;
    mark_preview_for_refresh();
    return true;
}

void GLGizmoWarpCut::on_render()
{
    ensure_preview_state();
    if (!m_has_preview_state || !m_preview_surface.is_initialized() || !m_preview_lines.is_initialized())
        return;

    // Apply or clear volume color overrides for preview mode
    if (m_preview_mode) {
        const auto& vol_ptrs = m_parent.get_volumes().volumes;
        m_parent.set_use_volume_color_override(true);
        const std::array<float, 4> gray{ 0.62f, 0.62f, 0.62f, 1.0f };
        for (unsigned int i = 0; i < (unsigned int)vol_ptrs.size(); ++i)
            m_parent.set_volume_color_override(i, gray);
    } else {
        m_parent.clear_all_volume_color_overrides();
    }

    const Camera& camera = wxGetApp().plater()->get_camera();
    const auto& view_matrix = camera.get_view_matrix();
    const auto& projection_matrix = camera.get_projection_matrix();
    const Transform3d identity = Transform3d::Identity();

    const auto surface_color = m_preview_mode
        ? std::array<float, 4>{ 0.95f, 0.30f, 0.50f, 0.40f }
        : std::array<float, 4>{ 0.88f, 0.46f, 0.14f, 0.32f };
    render_glmodel(m_preview_surface, surface_color, view_matrix * identity, projection_matrix);
    render_glmodel(m_preview_lines, { 0.10f, 0.55f, 0.50f, 1.0f }, view_matrix * identity, projection_matrix);

    const BoundingBoxf3 box = m_parent.get_selection().get_bounding_box();
    const double sphere_radius = std::max(1.2, box.radius() * 0.024);
    for (size_t index = 0; index < m_grid.size(); ++index) {
        const bool is_selected = int(index) == m_selected_point_index;
        const auto color = is_selected ? std::array<float, 4>{ 0.10f, 0.80f, 0.72f, 1.0f } : std::array<float, 4>{ 0.16f, 0.18f, 0.20f, 0.95f };
        const Transform3d model_matrix = Geometry::translation_transform(control_point_position(index)) * Geometry::scale_transform(Vec3d::Constant(sphere_radius));
        render_glmodel(m_control_point_sphere, color, view_matrix * model_matrix, projection_matrix, false, is_selected ? 0.35f : 0.0f);
    }
}

void GLGizmoWarpCut::on_render_input_window(float x, float y, float bottom_limit)
{
    static float last_y = 0.0f;
    static float last_h = 0.0f;

    const float win_h = ImGui::GetWindowHeight();
    y = std::min(y, bottom_limit - win_h);
    GizmoImguiSetNextWIndowPos(x, y, ImGuiCond_Always, 0.0f, 0.0f);
    if (last_h != win_h || last_y != y) {
        m_imgui->set_requires_extra_frame();
        if (last_h != win_h)
            last_h = win_h;
        if (last_y != y)
            last_y = y;
    }

    ImGuiWrapper::push_toolbar_style(m_parent.get_scale());
    GizmoImguiBegin(on_get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    ensure_preview_state();

    if (on_is_activable() && m_has_preview_state) {

        m_imgui->text(_L("Point move step"));
        float edit_step = m_edit_step;
        if (m_imgui->slider_float("##warpcut_step", &edit_step, 0.1f, 10.0f, "%.2f mm", 1.0f, false, _L("Point move step")))
            m_edit_step = std::clamp(edit_step, 0.1f, 10.0f);

        m_imgui->text(_L("Smoothing strength"));
        float smoothing_strength = m_smoothing_strength;
        if (m_imgui->slider_float("##warpcut_smooth_strength", &smoothing_strength, 0.0f, 1.0f, "%.2f", 1.0f, false, _L("Local smoothing strength")))
            m_smoothing_strength = std::clamp(smoothing_strength, 0.0f, 1.0f);

        m_imgui->text(_L("Smoothing radius") + _L(": number of neighboring rings blended after each edit."));
        int smoothing_radius = m_smoothing_radius;
        if (m_imgui->bbl_sliderin("##warpcut_smooth_radius", &smoothing_radius, 1, 5, "%d"))
            m_smoothing_radius = std::clamp(smoothing_radius, 1, 5);

        ImGui::Separator();
        m_imgui->text(_L("Vertical offset"));
        {
            const BoundingBoxf3 sbox = m_parent.get_selection().get_bounding_box();
            const float v_limit = float(sbox.size().z() * 0.5);
            float v_off = m_vertical_offset;
            if (m_imgui->slider_float("##warpcut_voffset", &v_off, -v_limit, v_limit, "%.2f mm", 1.0f, false, _L("Move cut surface up / down"))) {
                m_vertical_offset = std::clamp(v_off, -v_limit, v_limit);
                apply_tilt_to_frame();
                mark_preview_for_refresh();
            }
        }

        m_imgui->text(_L("Tilt X (pitch)"));
        {
            float tilt_x = float(m_tilt_angles.x());
            if (m_imgui->slider_float("##warpcut_tilt_x", &tilt_x, -45.0f, 45.0f, u8"%.1f\u00B0", 1.0f, false, _L("Tilt cut surface around X axis"))) {
                m_tilt_angles.x() = double(std::clamp(tilt_x, -45.0f, 45.0f));
                apply_tilt_to_frame();
                mark_preview_for_refresh();
            }
        }
        m_imgui->text(_L("Tilt Y (pitch)"));
        {
            float tilt_y = float(m_tilt_angles.y());
            if (m_imgui->slider_float("##warpcut_tilt_y", &tilt_y, -45.0f, 45.0f, u8"%.1f\u00B0", 1.0f, false, _L("Tilt cut surface around Y axis"))) {
                m_tilt_angles.y() = double(std::clamp(tilt_y, -45.0f, 45.0f));
                apply_tilt_to_frame();
                mark_preview_for_refresh();
            }
        }
        m_imgui->text(_L("Tilt Z (roll)"));
        {
            float tilt_z = float(m_tilt_angles.z());
            if (m_imgui->slider_float("##warpcut_tilt_z", &tilt_z, -45.0f, 45.0f, u8"%.1f\u00B0", 1.0f, false, _L("Roll cut surface around Z axis"))) {
                m_tilt_angles.z() = double(std::clamp(tilt_z, -45.0f, 45.0f));
                apply_tilt_to_frame();
                mark_preview_for_refresh();
            }
        }
        ImGui::Separator();

        if (m_selected_point_index >= 0) {
            const int row = m_selected_point_index / int(m_grid.cols);
            const int col = m_selected_point_index % int(m_grid.cols);
            m_imgui->text(wxString::Format(_L("Selected point") + ": (%d, %d)", row, col));
            m_imgui->text(wxString::Format(_L("Current offset") + ": %.2f", m_grid.offsets[size_t(m_selected_point_index)]));

            const bool can_move_down = m_grid.offsets[size_t(m_selected_point_index)] > -max_abs_offset() + 1e-4f;
            const bool can_move_up = m_grid.offsets[size_t(m_selected_point_index)] < max_abs_offset() - 1e-4f;
            if (m_imgui->button(_L("Move down"), ImVec2(0.f, 0.f), can_move_down))
                apply_selected_point_delta(-m_edit_step);
            ImGui::SameLine();
            if (m_imgui->button(_L("Move up"), ImVec2(0.f, 0.f), can_move_up))
                apply_selected_point_delta(m_edit_step);
            if (m_imgui->button(_L("Reset selected"), ImVec2(0.f, 0.f), true)) {
                m_grid.offsets[size_t(m_selected_point_index)] = 0.0f;
                smooth_offsets_around(size_t(m_selected_point_index));
                mark_preview_for_refresh();
            }

            if (!can_move_down || !can_move_up)
                m_imgui->text(_L("Selected point is at the current deformation limit."));
        } else {
            m_imgui->text(_L("Selected point") + ": None");
            m_imgui->text(_L("Select a control point before editing."));
        }

        ImGui::Separator();
        if (m_imgui->button(m_preview_mode ? _L("Exit preview") : _L("Preview"))) {
            m_preview_mode = !m_preview_mode;
            if (!m_preview_mode)
                m_parent.clear_all_volume_color_overrides();
            set_dirty();
            m_parent.request_extra_frame();
        }
        ImGui::Separator();
        m_imgui->bbl_checkbox(_L("Single cut surface color"), m_single_cut_surface_color);
        ImGui::Separator();
        m_imgui->disabled_begin(!can_perform_cut());
        {
            const float btn_h = ImGui::GetFrameHeight() * 2.0f;
            const float avail_w = ImGui::GetContentRegionAvail().x;
            if (m_imgui->button(_L("Perform cut"), avail_w, btn_h))
                perform_cut();
        }
        m_imgui->disabled_end();
        if (selected_instance_index() < 0)
            m_imgui->text(_L("Perform cut currently requires a single selected instance."));
    } else {
        m_imgui->text(_L("Please select single object."));
    }

    GizmoImguiEnd();
    ImGuiWrapper::pop_toolbar_style();
}

} // namespace GUI
} // namespace Slic3r
