#include "GLGizmoMeshBoolean.hpp"
#include "slic3r/GUI/UIHelpers/MeshBooleanUI.hpp"
#include "libslic3r/MeshBoolean.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "libslic3r/CSGMesh/CSGMesh.hpp"
#include "libslic3r/CSGMesh/ModelToCSGMesh.hpp"
#include "libslic3r/CSGMesh/PerformCSGMeshBooleans.hpp"

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui/imgui_internal.h>
#include <algorithm>
#include <unordered_set>
#include <memory>

namespace Slic3r {
namespace GUI {

// ========================== DEFINE STATIC WARNING MSG ==========================

const std::string MeshBooleanWarnings::COMMON                   = _u8L("Unable to perform boolean operation on selected parts.");
const std::string MeshBooleanWarnings::JOB_CANCELED             = _u8L("Operation Canceled.");
const std::string MeshBooleanWarnings::JOB_FAILED               = _u8L("Operation Failed.");
const std::string MeshBooleanWarnings::MIN_VOLUMES_UNION        = _u8L("Union operation requires at least two volumes.");
const std::string MeshBooleanWarnings::MIN_VOLUMES_INTERSECTION = _u8L("Intersection operation requires at least two volumes.");
const std::string MeshBooleanWarnings::MIN_VOLUMES_DIFFERENCE   = _u8L("Difference operation requires at least one volume in both A and B lists.");
const std::string MeshBooleanWarnings::MIN_OBJECTS_UNION        = _u8L("Union operation requires at least two objects.");
const std::string MeshBooleanWarnings::MIN_OBJECTS_INTERSECTION = _u8L("Intersection operation requires at least two objects.");
const std::string MeshBooleanWarnings::MIN_OBJECTS_DIFFERENCE   = _u8L("Difference operation requires at least one object in both A and B lists.");
const std::string MeshBooleanWarnings::GROUPING                 = _u8L("Failed to group and merge parts by object. Please check for self-intersections, non-manifold geometry, or open meshes.");
const std::string MeshBooleanWarnings::OVERLAPING               = _u8L("No overlapping region found. Boolean intersection failed.");

// ========================== UTILITY FUNCTIONS ==========================

namespace {

    class DebugLogger {
        std::ofstream log_file;
    public:
        DebugLogger() {
            log_file.open("C:/temp/boolean_debug.txt", std::ios::out | std::ios::app);

            // Get current time
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);

            log_file << "\n=== Boolean Operation Debug Started at "
                    << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S")
                    << " ===" << std::endl;
        }

        void log(const std::string& msg) {
            if (log_file.is_open()) {
                log_file << msg << std::endl;
                log_file.flush();
            }
        }

        ~DebugLogger() {
            if (log_file.is_open()) {
                log_file << "=== Debug Session Ended ===" << std::endl;
                log_file.close();
            }
        }
    };

    template <class T>
    static bool is_equal_ignore_order(std::vector<T> a, std::vector<T> b)
    {
        if (a.size() != b.size()) return false;
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        return a == b;
    }

    static bool check_if_active()
    {
        const Plater* plater = wxGetApp().plater();
        if (plater == nullptr) return false;
        const GLCanvas3D* canvas = plater->canvas3D();
        if (canvas == nullptr) return false;
        const GLGizmosManager& mng = canvas->get_gizmos_manager();
        // check if mesh boolean is still activ gizmo
        if (mng.get_current_type() != GLGizmosManager::MeshBoolean) return false;
        return true;
    }

    static const std::string& get_boolean_warning_by_mode(MeshBooleanOperation op, BooleanTargetMode mode) {
        switch (op) {
        case MeshBooleanOperation::Union:
            return mode == BooleanTargetMode::Object ? MeshBooleanWarnings::MIN_OBJECTS_UNION
                                                    : MeshBooleanWarnings::MIN_VOLUMES_UNION;
        case MeshBooleanOperation::Intersection:
            return mode == BooleanTargetMode::Object ? MeshBooleanWarnings::MIN_OBJECTS_INTERSECTION
                                                    : MeshBooleanWarnings::MIN_VOLUMES_INTERSECTION;
        case MeshBooleanOperation::Difference:
            return mode == BooleanTargetMode::Object ? MeshBooleanWarnings::MIN_OBJECTS_DIFFERENCE
                                                    : MeshBooleanWarnings::MIN_VOLUMES_DIFFERENCE;
        default:
            return MeshBooleanWarnings::COMMON;
        }
    }
    // Unified minimums guard used by Delete action; returns true if deletion should be blocked
    static bool violates_minimums_for_delete(MeshBooleanOperation mode,
                                             BooleanTargetMode target_mode,
                                             const VolumeListManager &mgr,
                                             const Selection &selection,
                                             BooleanWarningManager &warnings)
    {
        auto selected_in = [&](const std::vector<unsigned int>& vec){
            std::set<unsigned int> s;
            for (unsigned int v : mgr.get_selected_objects())
                if (std::find(vec.begin(), vec.end(), v) != vec.end()) s.insert(v);
            return s;
        };
        auto count_objects_after = [&](const std::vector<unsigned int>& vol_list, const std::set<unsigned int>& remove_set) {
            std::set<int> objs;
            for (unsigned int v : vol_list) {
                if (remove_set.count(v) > 0) continue;
                const GLVolume* glv = selection.get_volume(v);
                if (!glv) continue;
                objs.insert(glv->object_idx());
            }
            return (int)objs.size();
        };
        auto count_meshes_after = [&](const std::vector<unsigned int>& vol_list, const std::set<unsigned int>& remove_set) {
            int remain = 0;
            for (unsigned int v : vol_list) if (remove_set.count(v) == 0) ++remain;
            return remain;
        };

        auto remove_warning = [&](const std::string &w){ warnings.remove_warning(w); };

        std::string pre_warning = get_boolean_warning_by_mode(mode, target_mode);

        bool object_mode = (target_mode == BooleanTargetMode::Object);
        if (mode == MeshBooleanOperation::Union || mode == MeshBooleanOperation::Intersection) {
            int remain = object_mode
                ? count_objects_after(mgr.get_working_list(), mgr.get_selected_objects())
                : count_meshes_after(mgr.get_working_list(), mgr.get_selected_objects());
            if (remain < 2) {
                warnings.add_warning(pre_warning);
                return true;
            }
            // satisfied now -> drop previous warning
            remove_warning(pre_warning);
            return false;
        }

        // Difference
        int remain_a = object_mode
            ? count_objects_after(mgr.get_list_a(), selected_in(mgr.get_list_a()))
            : count_meshes_after(mgr.get_list_a(), selected_in(mgr.get_list_a()));
        int remain_b = object_mode
            ? count_objects_after(mgr.get_list_b(), selected_in(mgr.get_list_b()))
            : count_meshes_after(mgr.get_list_b(), selected_in(mgr.get_list_b()));
        if (remain_a < 1 || remain_b < 1) {
            warnings.add_warning(pre_warning);
            return true;
        }
        remove_warning(pre_warning);
        return false;
    }
} // anonymous namespace

// ========================== COLOR OVERRIDES ==========================

void GLGizmoMeshBoolean::ColorOverrideManager::apply_for_indices(const Selection &selection,
                                                                 const std::vector<unsigned int>& volume_indices,
                                                                 const std::array<float,4>& rgba)
{
    for (unsigned int idx : volume_indices) {
        const GLVolume *glv_c = selection.get_volume(idx);
        if (!glv_c) continue;
        GLVolume *glv = const_cast<GLVolume*>(glv_c);
        if (saved.find(idx) == saved.end()) {
            saved.emplace(idx, SavedVolumeColorState{ glv->color, glv->force_native_color, glv->force_neutral_color });
        }
        glv->force_native_color  = true;
        glv->force_neutral_color = false;
        glv->set_color(rgba);
        glv->set_render_color(rgba[0], rgba[1], rgba[2], rgba[3]);
    }
    applied = true;
}

void GLGizmoMeshBoolean::ColorOverrideManager::restore_non_selected_indices(const Selection &selection)
{
    for (auto &it : saved) {
        unsigned int vol_idx = it.first;
        const GLVolume *glv_c = selection.get_volume(vol_idx);
        if (!glv_c) continue;
        GLVolume *glv = const_cast<GLVolume*>(glv_c);
        const SavedVolumeColorState &s = it.second;
        glv->force_native_color  = s.original_force_native_color;
        glv->force_neutral_color = s.original_force_neutral_color;
        glv->set_color(s.original_color);
        glv->set_render_color(s.original_color[0], s.original_color[1], s.original_color[2], s.original_color[3]);
    }
    clear();
}

// ========================== WARNING SYSTEM IMPLEMENTATION ==========================
// All methods for managing and displaying warnings

BooleanWarningManager::BooleanWarningManager() = default;

void BooleanWarningManager::add_warning(const std::string& warning, WarningSeverity severity)
{
    auto it = std::find_if(m_warnings.begin(), m_warnings.end(), [&](const WarningItem& w){ return w.text == warning; });
    if (it == m_warnings.end()) {
        m_warnings.push_back(WarningItem{ warning, severity });
    } else if (severity == WarningSeverity::Error) {
        it->severity = WarningSeverity::Error;
    }
    if (m_on_change) m_on_change();
}

void BooleanWarningManager::add_error(const std::string& error_text)
{
    add_warning(error_text, WarningSeverity::Error);
}

void BooleanWarningManager::clear_warnings()
{
    m_warnings.clear();
    if (m_on_change) m_on_change();
}

void BooleanWarningManager::remove_warning(const std::string& warning_text)
{
    m_warnings.erase(
        std::remove_if(m_warnings.begin(), m_warnings.end(),
            [&warning_text](const WarningItem& item) {
                return item.text == warning_text;
            }),
        m_warnings.end()
    );
    if (m_on_change) m_on_change();
}

bool BooleanWarningManager::has_errors() const
{
    return std::any_of(m_warnings.begin(), m_warnings.end(), [](const WarningItem& w) {
        return w.severity == WarningSeverity::Error;
    });
}

bool BooleanWarningManager::has_errors_for_mode(MeshBooleanOperation mode) const
{
    auto mode_warnings = get_warnings_for_current_mode(mode);
    return std::any_of(mode_warnings.begin(), mode_warnings.end(), [](const WarningItem& w) {
        return w.severity == WarningSeverity::Error;
    });
}

bool BooleanWarningManager::is_general_warning(const std::string& warning_text) const
{
    return warning_text == MeshBooleanWarnings::COMMON ||
           warning_text == MeshBooleanWarnings::JOB_CANCELED ||
           warning_text == MeshBooleanWarnings::JOB_FAILED;
}

bool BooleanWarningManager::is_mode_specific_warning(const std::string& warning_text, MeshBooleanOperation mode) const
{
    switch (mode) {
        case MeshBooleanOperation::Union:
            return warning_text == MeshBooleanWarnings::MIN_VOLUMES_UNION ||
                   warning_text == MeshBooleanWarnings::MIN_OBJECTS_UNION;
        case MeshBooleanOperation::Intersection:
            return warning_text == MeshBooleanWarnings::MIN_VOLUMES_INTERSECTION ||
                   warning_text == MeshBooleanWarnings::MIN_OBJECTS_INTERSECTION ||
                   warning_text == MeshBooleanWarnings::OVERLAPING;
        case MeshBooleanOperation::Difference:
            return warning_text == MeshBooleanWarnings::MIN_VOLUMES_DIFFERENCE ||
                   warning_text == MeshBooleanWarnings::MIN_OBJECTS_DIFFERENCE;
        default:
            return false;
    }
}

void BooleanWarningManager::render_warning(const WarningItem& item, float width, ImTextureID warning_icon, ImTextureID error_icon, ImGuiWrapper* imgui, float icon_size)
{
    // Draw icon: warning or error
    ImTextureID icon_id = (item.severity == WarningSeverity::Error && error_icon) ? error_icon : warning_icon;
    if (icon_id) {
        ImVec2 icon_pos = ImGui::GetCursorScreenPos();
        icon_pos.y += (ImGui::GetFontSize() - icon_size) * 0.5f; // Center vertically
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddImage(icon_id, icon_pos,
            ImVec2(icon_pos.x + icon_size,
                  icon_pos.y + icon_size));
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + icon_size + MeshBooleanConfig::ICON_SPACING);
    }

    // Render warning text
    const float avail = width - icon_size - MeshBooleanConfig::ICON_SPACING;
    if (item.severity == WarningSeverity::Error)
        imgui->error_text_wrapped(_L(item.text), avail);
    else
        imgui->warning_text_wrapped(_L(item.text), avail);
}

void BooleanWarningManager::render_warnings_list(const std::vector<WarningItem>& warnings, float width, ImTextureID warning_icon, ImTextureID error_icon, ImGuiWrapper* imgui, float icon_size)
{
    if (!warnings.empty()) {
        for (const auto& warning : warnings) {
            // Center the warning
            float window_width = ImGui::GetWindowWidth();
            float start_x = (window_width - width) * 0.5f;
            ImGui::SetCursorPosX(start_x);

            render_warning(warning, width, warning_icon, error_icon, imgui, icon_size);
            ImGui::Spacing();
        }
    }
}

void BooleanWarningManager::clear_mode_specific_warnings(MeshBooleanOperation mode)
{
    // Remove warnings that are not applicable to the current mode
    m_warnings.erase(
        std::remove_if(m_warnings.begin(), m_warnings.end(), [this, mode](const WarningItem& item) {
            const std::string& warning = item.text;
            // Keep general warnings (apply to all modes)
            if (is_general_warning(warning)) {
                return false; // Keep these warnings
            }

            // Remove mode-specific warnings that don't match current mode
            return !is_mode_specific_warning(warning, mode);
        }),
        m_warnings.end()
    );
}

std::vector<WarningItem> BooleanWarningManager::get_warnings_for_current_mode(MeshBooleanOperation mode) const
{
    std::vector<WarningItem> filtered_warnings;

    for (const WarningItem& warning_item : m_warnings) {
        const std::string& warning = warning_item.text;
        // Keep general warnings or mode-specific warnings that match current mode
        if (is_general_warning(warning) || is_mode_specific_warning(warning, mode)) {
            filtered_warnings.push_back(warning_item);
        }
    }

    return filtered_warnings;
}

//NOTE: this may could be delete or replace
std::vector<WarningItem> BooleanWarningManager::get_inline_hints_for_state(MeshBooleanOperation mode, const VolumeListManager& volume_manager) const
{
    // Delegate to per-mode validator; hints are represented as warnings.
    std::vector<WarningItem> hints;
    switch (mode) {
        case MeshBooleanOperation::Union:
            if (volume_manager.get_working_list().size() < 2)
                hints.push_back(WarningItem{ MeshBooleanWarnings::MIN_VOLUMES_UNION, WarningSeverity::Warning });
            break;
        case MeshBooleanOperation::Intersection:
            if (volume_manager.get_working_list().size() < 2)
                hints.push_back(WarningItem{ MeshBooleanWarnings::MIN_VOLUMES_INTERSECTION, WarningSeverity::Warning });
            break;
        case MeshBooleanOperation::Difference:
            if (volume_manager.get_list_a().empty() || volume_manager.get_list_b().empty())
                hints.push_back(WarningItem{ MeshBooleanWarnings::MIN_VOLUMES_DIFFERENCE, WarningSeverity::Warning });
            break;
        default: break;
    }
    return hints;
}


// ========================== VOLUME LIST MANAGER IMPLEMENTATION ==========================
// Manages volume lists for different boolean operations (A/B lists, working list)
VolumeListManager::VolumeListManager() {}

void VolumeListManager::sort_volumes_by_type(std::vector<unsigned int>& volume_indices, const Selection& selection) {
    // Sort volumes so that MODEL_PART volumes appear first, then other types
    std::sort(volume_indices.begin(), volume_indices.end(), [&selection](unsigned int a, unsigned int b) {
        const GLVolume* vol_a = selection.get_volume(a);
        const GLVolume* vol_b = selection.get_volume(b);

        if (!vol_a || !vol_b) return false;

        ModelVolume* mv_a = get_model_volume(*vol_a, selection.get_model()->objects);
        ModelVolume* mv_b = get_model_volume(*vol_b, selection.get_model()->objects);

        if (!mv_a || !mv_b) return false;

        bool is_a_model_part = mv_a->is_model_part();
        bool is_b_model_part = mv_b->is_model_part();

        // MODEL_PART volumes should come first
        if (is_a_model_part && !is_b_model_part) return true;
        if (!is_a_model_part && is_b_model_part) return false;

        // If both are the same type, maintain original order (stable sort)
        return a < b;
    });
}

bool VolumeListManager::selection_changed(const Selection& selection){
    std::vector<unsigned int> cur_volumes;
    const Selection::IndicesList& volume_idxs = selection.get_volume_idxs();
    for (unsigned int vol_idx : volume_idxs) {
        cur_volumes.push_back(vol_idx);
    }
    return !is_equal_ignore_order(m_working_volumes, cur_volumes);

}
void VolumeListManager::init_part_mode_lists(const Selection& selection) {
    clear_all();
    if (selection.volumes_count() < 1) return;
    const Selection::IndicesList& volume_idxs = selection.get_volume_idxs();
    for (unsigned int vol_idx : volume_idxs) {
        m_working_volumes.push_back(vol_idx);
    }

    // Sort volumes so that MODEL_PART volumes appear first
    sort_volumes_by_type(m_working_volumes, selection);

    m_a_list_volumes.push_back(m_working_volumes.front());
    m_b_list_volumes.assign(m_working_volumes.begin() + 1, m_working_volumes.end());
}

void VolumeListManager::init_object_mode_lists(const Selection& selection){
    clear_all();
    if (!selection.is_multiple_full_object()) return;
    const Selection::IndicesList& volume_idxs = selection.get_volume_idxs();

    if (volume_idxs.empty()) return;

    unsigned int first = *volume_idxs.begin();
    const GLVolume* first_vol = selection.get_volume(first);
    if (!first_vol) return;
    int first_object_idx = first_vol->object_idx();

    for(unsigned int vol_idx : volume_idxs) {
        const GLVolume* vol = selection.get_volume(vol_idx);
        if (!vol) continue;
        // List a and b
        int current_object_idx = vol->object_idx();
        if (current_object_idx == first_object_idx) m_a_list_volumes.push_back(vol_idx);
        else m_b_list_volumes.push_back(vol_idx);
        // overall
        m_working_volumes.push_back(vol_idx);
    }

    // Sort all lists so that MODEL_PART volumes appear first
    sort_volumes_by_type(m_working_volumes, selection);
    sort_volumes_by_type(m_a_list_volumes, selection);
    sort_volumes_by_type(m_b_list_volumes, selection);
}

void VolumeListManager::clear_all() {
    m_working_volumes.clear();
    m_a_list_volumes.clear();
    m_b_list_volumes.clear();
    m_selected_objects.clear();
    m_selected_a_objects.clear();
    m_selected_b_objects.clear();
    m_a_list_objects.clear();
    m_b_list_objects.clear();
    m_working_objects.clear();
}

std::set<unsigned int> VolumeListManager::remove_selected_from_all_lists()
{
    std::set<unsigned int> selected = m_selected_objects;
    selected.insert(m_selected_a_objects.begin(), m_selected_a_objects.end());
    selected.insert(m_selected_b_objects.begin(), m_selected_b_objects.end());
    if (selected.empty()) {
        // Clear selections anyway
        m_selected_objects.clear();
        m_selected_a_objects.clear();
        m_selected_b_objects.clear();
        return {};
    }

    auto remove_from = [&selected](std::vector<unsigned int>& vec){
        vec.erase(std::remove_if(vec.begin(), vec.end(),
                  [&selected](unsigned int v){ return selected.count(v) > 0; }), vec.end());
    };

    remove_from(m_working_volumes);
    remove_from(m_a_list_volumes);
    remove_from(m_b_list_volumes);

    m_selected_objects.clear();
    m_selected_a_objects.clear();
    m_selected_b_objects.clear();

    return selected;
}

void VolumeListManager::move_selected_to_left() {
    // Move selected volumes that are currently in B -> append to A, then remove from B
    if (m_selected_objects.empty() && m_selected_b_objects.empty()) return;
    std::set<unsigned int> selected = m_selected_objects;
    selected.insert(m_selected_b_objects.begin(), m_selected_b_objects.end());
    if (selected.empty()) return;
    // Append to A preserving order of B
    for (unsigned int v : m_b_list_volumes) {
        if (selected.count(v) > 0) {
            if (std::find(m_a_list_volumes.begin(), m_a_list_volumes.end(), v) == m_a_list_volumes.end())
                m_a_list_volumes.push_back(v);
        }
    }
    // Remove from B
    m_b_list_volumes.erase(std::remove_if(m_b_list_volumes.begin(), m_b_list_volumes.end(),
        [&selected](unsigned int v){ return selected.count(v) > 0; }), m_b_list_volumes.end());
    // Clear A/B specific selections
    m_selected_b_objects.clear();
}

void VolumeListManager::move_selected_to_right() {
    // Move selected volumes that are currently in A -> append to B, then remove from A
    if (m_selected_objects.empty() && m_selected_a_objects.empty()) return;
    std::set<unsigned int> selected = m_selected_objects;
    selected.insert(m_selected_a_objects.begin(), m_selected_a_objects.end());
    if (selected.empty()) return;
    // Append to B preserving order of A
    for (unsigned int v : m_a_list_volumes) {
        if (selected.count(v) > 0) {
            if (std::find(m_b_list_volumes.begin(), m_b_list_volumes.end(), v) == m_b_list_volumes.end())
                m_b_list_volumes.push_back(v);
        }
    }
    // Remove from A
    m_a_list_volumes.erase(std::remove_if(m_a_list_volumes.begin(), m_a_list_volumes.end(),
        [&selected](unsigned int v){ return selected.count(v) > 0; }), m_a_list_volumes.end());
    // Clear A/B specific selections
    m_selected_a_objects.clear();
}

void VolumeListManager::swap_lists() {
    std::swap(m_a_list_volumes, m_b_list_volumes);
    std::swap(m_selected_a_objects, m_selected_b_objects);
}

bool VolumeListManager::validate_for_union() const {
    return m_working_volumes.size() >= 2;
}

bool VolumeListManager::validate_for_intersection() const {
    return m_working_volumes.size() >= 2;
}

bool VolumeListManager::validate_for_difference() const {
    return !m_a_list_volumes.empty() && !m_b_list_volumes.empty();
}

std::vector<std::string> VolumeListManager::get_volume_names(const std::vector<unsigned int>& volume_list, const Selection& selection) const {
    std::vector<std::string> names;
    names.reserve(volume_list.size());

    for (unsigned int volume_idx : volume_list) {
        const GLVolume* gl_volume = selection.get_volume(volume_idx);
        if (gl_volume) {
            ModelVolume* model_volume = get_model_volume(*gl_volume, selection.get_model()->objects);
            if (model_volume) {
                names.push_back(model_volume->name);
            } else {
                names.push_back("Unknown Volume");
            }
        } else {
            names.push_back("Invalid Volume");
        }
    }

    return names;
}

bool VolumeListManager::add_volume_to_list(unsigned int volume_idx, bool to_a_list) {
    // Check if volume is already in any list
    if (std::find(m_a_list_volumes.begin(), m_a_list_volumes.end(), volume_idx) != m_a_list_volumes.end() ||
        std::find(m_b_list_volumes.begin(), m_b_list_volumes.end(), volume_idx) != m_b_list_volumes.end() ||
        std::find(m_working_volumes.begin(), m_working_volumes.end(), volume_idx) != m_working_volumes.end()) {
        return false;
    }

    // Add to appropriate list
    if (to_a_list) {
        m_a_list_volumes.push_back(volume_idx);
    } else {
        m_b_list_volumes.push_back(volume_idx);
    }

    // Also add to working list for union/intersection operations
    m_working_volumes.push_back(volume_idx);
    return true;
}

bool VolumeListManager::add_volumes_to_list(const std::vector<unsigned int>& volume_indices, bool to_a_list)
{
    bool any_added = false;
    for (unsigned int volume_idx : volume_indices) {
        if (std::find(m_a_list_volumes.begin(), m_a_list_volumes.end(), volume_idx) != m_a_list_volumes.end() ||
            std::find(m_b_list_volumes.begin(), m_b_list_volumes.end(), volume_idx) != m_b_list_volumes.end() ||
            std::find(m_working_volumes.begin(), m_working_volumes.end(), volume_idx) != m_working_volumes.end())
            continue;

        if (to_a_list) m_a_list_volumes.push_back(volume_idx);
        else            m_b_list_volumes.push_back(volume_idx);
        m_working_volumes.push_back(volume_idx);
        any_added = true;
    }
    return any_added;
}

bool VolumeListManager::add_object_to_list(int object_idx, bool to_a_list, const Selection& selection)
{
    if (object_idx < 0) return false;
    std::vector<unsigned int> vols = selection.get_volume_idxs_from_object((unsigned int)object_idx);
    bool added = add_volumes_to_list(vols, to_a_list);
    return added;
}

void VolumeListManager::update_obj_lists(const Selection& selection)
{
    if(!selection.is_multiple_full_object()){
        m_a_list_objects.clear();
        m_b_list_objects.clear();
        m_working_objects.clear();
        return;
    }
    auto build = [&](const std::vector<unsigned int>& vol_list, std::vector<int>& out) {
        out.clear();
        std::unordered_set<int> seen;
        for (unsigned int v : vol_list) {
            const GLVolume* glv = selection.get_volume(v);
            if (!glv) continue;
            int obj = glv->object_idx();
            if (seen.insert(obj).second) out.push_back(obj);
        }
    };
    build(m_a_list_volumes, m_a_list_objects);
    build(m_b_list_volumes, m_b_list_objects);
    build(m_working_volumes, m_working_objects);
}

void VolumeListManager::remove_indices_from_all_lists(const std::set<unsigned int>& indices)
{
    if (indices.empty()) return;
    auto remove_from = [&indices](std::vector<unsigned int>& vec){
        vec.erase(std::remove_if(vec.begin(), vec.end(),
            [&indices](unsigned int v){ return indices.count(v) > 0; }), vec.end());
    };
    remove_from(m_working_volumes);
    remove_from(m_a_list_volumes);
    remove_from(m_b_list_volumes);
}

// ========================== BOOLEAN OPERATION ENGINE IMPLEMENTATION ==========================
// Helpers for non-model handling abstraction
static void filter_volumes_for_boolean(std::vector<BooleanOperationEngine::VolumeInfo>& volumes,
                                       const BooleanOperationSettings& settings)
{
    // Remove invalid or empty-mesh volumes to avoid downstream boolean crashes
    volumes.erase(std::remove_if(volumes.begin(), volumes.end(), [](const BooleanOperationEngine::VolumeInfo& info){
        return info.model_volume == nullptr || info.model_volume->mesh().empty();
    }), volumes.end());

    // If only operating on entities, drop non-model parts
    if (settings.entity_only) {
        volumes.erase(std::remove_if(volumes.begin(), volumes.end(), [](const BooleanOperationEngine::VolumeInfo& info){
            return info.model_volume == nullptr || !info.model_volume->is_model_part();
        }), volumes.end());
    }
}

static void validate_before_boolean(const std::vector<BooleanOperationEngine::VolumeInfo>& volumes,
                                    BooleanOperationResult& result)
{
    std::vector<const ModelVolume*> candidates;
    for (const auto& v : volumes) {
        if (v.model_volume) candidates.push_back(v.model_volume);
    }
    csg::BooleanFailReason fail_reason;
    std::string warning_text = check_boolean_possible(candidates, fail_reason);
    if (warning_text == "" || fail_reason == csg::BooleanFailReason::SelfIntersect) return;
    result.error_message = warning_text;
}

static void attach_ignored_non_models_to_target(ModelObject* target_object,
                                                const BooleanOperationSettings& settings)
{
    // Only attach non-model volumes in object mode
    if (settings.target_mode == BooleanTargetMode::Part ||
        !settings.entity_only ||
        !target_object ||
        settings.non_model_volumes_to_attach.empty())
        return;

    // Helper function to get instance transformation matrix
    auto get_instance_matrix = [](const ModelObject* obj) -> Transform3d {
        if (obj && !obj->instances.empty() && obj->instances[0])
            return obj->instances[0]->get_matrix(false);
        return Transform3d::Identity();
    };

    // Get target object instance transform
    Transform3d tgt_inst = get_instance_matrix(target_object);

    // Attach non-model volumes
    for (ModelVolume* nv : settings.non_model_volumes_to_attach) {
        if (!nv || nv->get_object() == target_object) continue;

        // Calculate transformation matrix
        Transform3d vol_mat = nv->get_matrix();
        Transform3d src_inst = get_instance_matrix(nv->get_object());
        Transform3d pre = vol_mat.inverse() * tgt_inst.inverse() * src_inst * vol_mat;

        // Create and transform mesh
        TriangleMesh local_mesh = nv->mesh();
        local_mesh.transform(pre);

        // Create new volume and copy properties
        ModelVolume* attached = target_object->add_volume(std::move(local_mesh), nv->type(), false);
        attached->name = nv->name;
        attached->set_new_unique_id();
        attached->config.apply(nv->config);
        attached->set_material_id(nv->material_id());
        attached->set_transformation(nv->get_transformation());
    }

    // Update object info in UI
    auto* obj_list = wxGetApp().obj_list();
    if (obj_list) {
        auto& objects = *obj_list->objects();
        if (auto it = std::find(objects.begin(), objects.end(), target_object); it != objects.end()) {
            obj_list->update_info_items(int(it - objects.begin()));
        }
    }
}

// Core boolean operation engine - handles union, intersection, difference operations
BooleanOperationEngine::BooleanOperationEngine() {}

BooleanOperationResult BooleanOperationEngine::perform_union(const VolumeListManager& volume_manager,
                                                           const Selection& selection,
                                                           const BooleanOperationSettings& settings) {
    auto volumes = prepare_volumes(volume_manager.get_working_list(), selection);
    filter_volumes_for_boolean(volumes, settings);

    return part_level_boolean(volumes, settings, MeshBooleanConfig::OP_UNION);
}

BooleanOperationResult BooleanOperationEngine::perform_intersection(const VolumeListManager& volume_manager,
                                                                  const Selection& selection,
                                                                  const BooleanOperationSettings& settings) {
    auto volumes = prepare_volumes(volume_manager.get_working_list(), selection);
    filter_volumes_for_boolean(volumes, settings);

    // Object mode: first union all parts inside each object, then intersect across objects
    if (settings.target_mode == BooleanTargetMode::Object) {
        BOOST_LOG_TRIVIAL(info) << "Performing object-level intersection";
        BooleanOperationResult result;

        // Group by object index
        int obj_count = 1, last_obj = volumes[0].object_index;
        std::unordered_map<int, std::vector<VolumeInfo>> by_object;
        for (const auto &v : volumes) {
            by_object[v.object_index].push_back(v);
            if (v.object_index != last_obj) {
                last_obj = v.object_index;
                obj_count += 1;
            }
        }

        if (obj_count < 2) {
            result.error_message = MeshBooleanWarnings::MIN_OBJECTS_INTERSECTION;
            return result;
        }

        std::vector<TriangleMesh> pre_union_result;

        // Build per-object unions
        for (auto &kv : by_object) {
            const auto &group = kv.second;
            if (group.empty()) continue;
            auto acc = execute_boolean_on_meshes(group, MeshBooleanConfig::OP_UNION);
            if (!acc.has_value()) {
                BOOST_LOG_TRIVIAL(error) << "Object-level intersection failed (grouping problem): " << kv.second.front().model_volume->name;
                result.error_message =  MeshBooleanWarnings::GROUPING;
                return result;
            }
            TriangleMesh m    = *acc;
            // Slic3r::MeshBoolean::self_union(m);
            pre_union_result.push_back(m);
        }

        TriangleMesh cur = pre_union_result[0];
        for (size_t i = 1; i < pre_union_result.size(); ++i) {
            // try { Slic3r::MeshBoolean::self_union(o); } catch (...) {}
            cur = execute_boolean_operation(cur, pre_union_result[i], MeshBooleanConfig::OP_INTERSECTION);
            if (cur.empty()) {
                result.error_message = MeshBooleanWarnings::OVERLAPING;
                return result;
            }
        }

        result.success = true;
        result.result_meshes.push_back(cur);
        result.source_transforms.push_back(volumes[0].transformation);
        result.source_volumes.push_back(volumes[0].model_volume);

        // Deletion policy in object mode: for intersection, delete participating volumes when not keeping originals
        if (!settings.keep_original_models) {
            for (const auto &v : volumes) result.volumes_to_delete.push_back(v.model_volume);
        }

        return result;
    }

    return part_level_boolean(volumes, settings, MeshBooleanConfig::OP_INTERSECTION);
}

BooleanOperationResult BooleanOperationEngine::perform_difference(const VolumeListManager& volume_manager,
                                                                const Selection& selection,
                                                                const BooleanOperationSettings& settings) {
    auto volumes_a = prepare_volumes(volume_manager.get_list_a(), selection);
    auto volumes_b = prepare_volumes(volume_manager.get_list_b(), selection);
    filter_volumes_for_boolean(volumes_a, settings);
    filter_volumes_for_boolean(volumes_b, settings);

    // Object mode: each object separately subtracts all B objects (no union within objects)
    if (settings.target_mode == BooleanTargetMode::Object) {
        BooleanOperationResult result;

        if (volumes_a.empty() || volumes_b.empty()) {
            result.error_message = MeshBooleanWarnings::MIN_OBJECTS_DIFFERENCE;
            return result;
        }

        // Group volumes by object (but don't union them - keep each object separate)
        auto group_by_object = [&](const std::vector<VolumeInfo> &vols) -> std::unordered_map<int, std::vector<VolumeInfo>> {
            std::unordered_map<int, std::vector<VolumeInfo>> by_object;
            for (const auto &v : vols) by_object[v.object_index].push_back(v);
            return by_object;
        };

        auto a_by_object = group_by_object(volumes_a);
        auto b_by_object = group_by_object(volumes_b);

        // Build combined B mesh for subtraction (union all B objects for subtraction)
        TriangleMesh b_union;
        bool b_union_init = false;
        for (const auto &kv : b_by_object) {
            for (const auto &vol_info : kv.second) {
                TriangleMesh bm = get_transformed_mesh(vol_info);
                if (!b_union_init) { b_union = bm; b_union_init = true; }
                else {
                    try {
                        b_union = execute_boolean_operation(b_union, bm, MeshBooleanConfig::OP_UNION);
                        if (b_union.empty()) { result.error_message = MeshBooleanWarnings::MIN_VOLUMES_DIFFERENCE; return result; }
                    } catch (const std::exception &e) {
                        result.error_message = std::string("Boolean union (B group) failed: ") + e.what();
                        return result;
                    }
                }
            }
        }
        if (!b_union_init) {
            result.error_message = MeshBooleanWarnings::MIN_VOLUMES_DIFFERENCE;
            return result;
        }
        try { Slic3r::MeshBoolean::self_union(b_union); } catch (...) {}

        // For each A object, subtract all B volumes
        for (const auto &kv : a_by_object) {
            const std::vector<VolumeInfo> &a_object_volumes = kv.second;

            // Each volume in this A object gets subtracted by all B
            for (const auto &a_vol : a_object_volumes) {
                TriangleMesh accumulated_result = get_transformed_mesh(a_vol);

                // Subtract unified B mesh once
                try {
                    accumulated_result = execute_boolean_operation(accumulated_result, b_union, MeshBooleanConfig::OP_DIFFERENCE);
                } catch (const std::exception& e) {
                    result.error_message = std::string("Boolean difference (object mode) failed: ") + e.what();
                    return result;
                }

                // Only add non-empty results
                if (!accumulated_result.empty()) {
                    result.result_meshes.push_back(accumulated_result);
                    result.source_volumes.push_back(a_vol.model_volume);
                    result.source_transforms.push_back(a_vol.transformation);
                }
            }
        }

        // Deletion policy for object mode
        if (!settings.keep_original_models) {
            for (const auto &v : volumes_b) result.volumes_to_delete.push_back(v.model_volume);
        }

        result.success = true;
        return result;
    }

    return part_level_sub(volumes_a, volumes_b, settings);
}

std::string BooleanOperationEngine::validate_operation(MeshBooleanOperation type, const VolumeListManager& volume_manager) const {
    switch (type) {
        case MeshBooleanOperation::Union:
            if (!volume_manager.validate_for_union())
                return MeshBooleanWarnings::MIN_VOLUMES_UNION;
            break;
        case MeshBooleanOperation::Intersection:
            if (!volume_manager.validate_for_intersection())
                return MeshBooleanWarnings::MIN_VOLUMES_INTERSECTION;
            break;
        case MeshBooleanOperation::Difference:
            if (!volume_manager.validate_for_difference())
                return MeshBooleanWarnings::MIN_VOLUMES_DIFFERENCE;
            break;
        default:
            return "Unknown operation type";
    }
    return "";
}


std::vector<BooleanOperationEngine::VolumeInfo> BooleanOperationEngine::prepare_volumes(
    const std::vector<unsigned int>& volume_indices,
    const Selection& selection) const {

    static DebugLogger logger;
    std::vector<VolumeInfo> result;

    for (unsigned int volume_idx : volume_indices) {
        const GLVolume* gl_volume = selection.get_volume(volume_idx);
        if (!gl_volume) continue;

        ModelVolume* model_volume = get_model_volume(*gl_volume, selection.get_model()->objects);
        if (!model_volume) continue;

        VolumeInfo info;
        info.model_volume = model_volume;
        info.volume_index = volume_idx;
        info.object_index = gl_volume->object_idx();
        info.transformation = gl_volume->get_instance_transformation().get_matrix() *
                             gl_volume->get_volume_transformation().get_matrix();

        result.push_back(info);
    }

    // Sort volumes: MODEL_PART first, then others (consistent with UI display order)
    std::sort(result.begin(), result.end(), [](const VolumeInfo& a, const VolumeInfo& b) {
        if (!a.model_volume || !b.model_volume) return false;
        bool a_is_model_part = a.model_volume->is_model_part();
        bool b_is_model_part = b.model_volume->is_model_part();
        // MODEL_PART volumes come first
        if (a_is_model_part && !b_is_model_part) return true;
        if (!a_is_model_part && b_is_model_part) return false;
        // Same type: maintain original order (stable)
        return a.volume_index < b.volume_index;
    });

    return result;
}

TriangleMesh BooleanOperationEngine::get_transformed_mesh(const VolumeInfo& volume_info) const {
    TriangleMesh mesh;
    if (volume_info.model_volume != nullptr)
        mesh = volume_info.model_volume->mesh();
    if (mesh.empty())
        return mesh;
    mesh.transform(volume_info.transformation);
    return mesh;
}

TriangleMesh BooleanOperationEngine::execute_boolean_operation(const TriangleMesh& mesh_a,
                                                             const TriangleMesh& mesh_b,
                                                             const std::string& operation_name) const {
    std::vector<TriangleMesh> result_meshes;
    Slic3r::MeshBoolean::mcut::make_boolean(mesh_a, mesh_b, result_meshes, operation_name);

    if (!result_meshes.empty()) {
        return result_meshes[0];
    }

    return TriangleMesh();
}

std::optional<TriangleMesh> BooleanOperationEngine::execute_boolean_on_meshes(const std::vector<VolumeInfo> &volumes, const std::string &operation) const
{
    if (volumes.empty()) return std::nullopt;

    TriangleMesh accumulated_result = get_transformed_mesh(volumes[0]);
    for (size_t i = 1; i < volumes.size(); ++i) {
        TriangleMesh next_mesh = get_transformed_mesh(volumes[i]);
        try {
            accumulated_result = execute_boolean_operation(accumulated_result, next_mesh, operation);

            // Check if result is empty
            if (accumulated_result.empty()) {
                BOOST_LOG_TRIVIAL(warning) << "Boolean operation returned empty mesh at step " << i;

                // For INTERSECTION: try CGAL as fallback
                if (operation == MeshBooleanConfig::OP_INTERSECTION) {
                    BOOST_LOG_TRIVIAL(info) << "Attempting CGAL fallback for intersection...";

                    // Rebuild the mesh before the failed operation
                    TriangleMesh acc_before = get_transformed_mesh(volumes[0]);
                    for (size_t j = 1; j < i; ++j) {
                        TriangleMesh temp_mesh = get_transformed_mesh(volumes[j]);
                        acc_before = execute_boolean_operation(acc_before, temp_mesh, operation);
                        // Minimal cleanup after each step
                        its_merge_vertices(acc_before.its);
                        its_remove_degenerate_faces(acc_before.its);
                    }

                    try {
                        Slic3r::MeshBoolean::cgal::intersect(acc_before, next_mesh);
                        if (!acc_before.empty()) {
                            BOOST_LOG_TRIVIAL(info) << "CGAL fallback succeeded!";
                            accumulated_result = acc_before;
                        } else {
                            BOOST_LOG_TRIVIAL(warning) << "CGAL also returned empty mesh";
                            return std::nullopt;
                        }
                    } catch (const std::exception& e) {
                        BOOST_LOG_TRIVIAL(warning) << "CGAL fallback failed: " << e.what();
                        return std::nullopt;
                    }
                } else {
                    // For other operations, just return nullopt
                    return std::nullopt;
                }
            }

            // Apply mesh cleanup based on operation type
            if (operation == MeshBooleanConfig::OP_INTERSECTION) {
                // Aggressive cleanup for intersection
                its_remove_degenerate_faces(accumulated_result.its);
                its_merge_vertices(accumulated_result.its, true);
                its_remove_degenerate_faces(accumulated_result.its, true);
                its_compactify_vertices(accumulated_result.its, true);
#if 0
                // Rebuild mesh from scratch
                indexed_triangle_set cleaned_its;
                cleaned_its.vertices = accumulated_result.its.vertices;
                cleaned_its.indices = accumulated_result.its.indices;
                accumulated_result = TriangleMesh(std::move(cleaned_its));

                its_merge_vertices(accumulated_result.its, true);
                its_compactify_vertices(accumulated_result.its, true);
            } else {
                // Minimal cleanup for difference/union to preserve precision
                its_remove_degenerate_faces(accumulated_result.its);
                its_compactify_vertices(accumulated_result.its);
#endif
            }

        } catch (const std::exception &e) {
            BOOST_LOG_TRIVIAL(warning) << "Executing boolean on meshes failed: " << e.what();
            return std::nullopt;
        }
    }
    return accumulated_result;
}

BooleanOperationResult BooleanOperationEngine::part_level_boolean(const std::vector<VolumeInfo>& volumes,
                                                      const BooleanOperationSettings& settings,
                                                      const std::string& operation,
                                                      bool allow_single_volume) const {
    BooleanOperationResult result;
    // Difference
    if (operation == MeshBooleanConfig::OP_DIFFERENCE) { return part_level_sub(volumes, volumes, settings); }
    // Union or Intersection
    if (volumes.size() < 2 && !allow_single_volume) {
        result.error_message = MeshBooleanWarnings::COMMON;
        return result;
    }
    validate_before_boolean(volumes, result);
    if (result.error_message != "") return result;

    auto acc = execute_boolean_on_meshes(volumes, operation);
    if (!acc.has_value()) {
        result.success = false;
        result.error_message = operation == MeshBooleanConfig::OP_UNION ? MeshBooleanWarnings::JOB_FAILED : MeshBooleanWarnings::OVERLAPING;
        return result;
    }
    TriangleMesh accumulated_mesh = *acc;

    result.success = true;
    result.result_meshes.push_back(*acc);
    result.source_transforms.push_back(volumes[0].transformation);
    result.source_volumes.push_back(volumes[0].model_volume); // Use the first volume as the source for transform/context
    update_delete_list(result, volumes, settings);
    return result;
}

BooleanOperationResult BooleanOperationEngine::part_level_sub(const std::vector<VolumeInfo>& volumes_a,
                                                                      const std::vector<VolumeInfo>& volumes_b,
                                                                      const BooleanOperationSettings& settings) const {
    BooleanOperationResult result;

    if (volumes_a.empty() || volumes_b.empty()) {
        result.error_message = MeshBooleanWarnings::COMMON;
        return result;
    }
    validate_before_boolean(volumes_a, result);
    if (result.error_message != "") return result;
    validate_before_boolean(volumes_b, result);
    if (result.error_message != "") return result;
    // Process each A volume: A_i = A_i - ALL_B_volumes
    for (size_t a_idx = 0; a_idx < volumes_a.size(); ++a_idx) {
        TriangleMesh accumulated_result = get_transformed_mesh(volumes_a[a_idx]);

        // Subtract ALL B volumes sequentially
        for (size_t b_idx = 0; b_idx < volumes_b.size(); ++b_idx) {
            TriangleMesh b_mesh = get_transformed_mesh(volumes_b[b_idx]);

            try {
                // Only A-B operation
                accumulated_result = execute_boolean_operation(accumulated_result, b_mesh, MeshBooleanConfig::OP_DIFFERENCE);

                if (accumulated_result.empty()) {
                    result.error_message = "Boolean difference operation failed for volume " + volumes_a[a_idx].model_volume->name;
                    return result;
                }
            } catch (const std::exception& e) {
                result.error_message = std::string("Boolean difference operation failed: ") + e.what();
                return result;
            }

        }

        result.result_meshes.push_back(accumulated_result);
        result.source_volumes.push_back(volumes_a[a_idx].model_volume);
        result.source_transforms.push_back(volumes_a[a_idx].transformation);
    }

    // Delete B volumes if not keeping original models
    if (!settings.keep_original_models) {
        for (const auto& volume_b : volumes_b) {
            result.volumes_to_delete.push_back(volume_b.model_volume);
        }
    }

    result.success = true;
    return result;
}

// Collect volumes that need to be deleted (excluding the source/first volume)
// Note: volumes[0] is handled separately in apply_result_to_model via should_replace_source
void BooleanOperationEngine::update_delete_list(BooleanOperationResult& result,
                                                        const std::vector<VolumeInfo>& volumes,
                                                        const BooleanOperationSettings& settings ) const {
    // If keeping original models, don't delete anything here (volumes[0] may still be replaced in Difference mode)
    if (settings.keep_original_models) return;

    // Delete all volumes except volumes[0] (which is the source, handled separately)
    if (settings.entity_only) {
        // In entity_only mode, only delete MODEL_PART volumes
        for (size_t i = 1; i < volumes.size(); ++i) {
            if (volumes[i].model_volume->is_model_part()) {
                result.volumes_to_delete.push_back(volumes[i].model_volume);
            }
        }
    } else {
        // In normal mode, delete all volumes
        for (size_t i = 1; i < volumes.size(); ++i) {
            result.volumes_to_delete.push_back(volumes[i].model_volume);
        }
    }
}

//NOTE: keep watching
void BooleanOperationEngine::apply_result_to_model(const BooleanOperationResult& result,
                                                  ModelObject* target_object,
                                                  int object_index,
                                                  const BooleanOperationSettings& settings,
                                                  MeshBooleanOperation mode,
                                                  const std::vector<ModelObject*>& participating_objects,
                                                  const std::vector<ModelObject*>& a_group_objects,
                                                  const std::vector<ModelObject*>& b_group_objects) {
    if (!result.success || result.result_meshes.empty() || settings.target_mode == BooleanTargetMode::Unknown)
        return;
    (void)object_index;

    auto add_object_to_sidebar = [](ModelObject* obj) {
        if (!obj) return;
        auto &objects = *wxGetApp().obj_list()->objects();
        auto it = std::find(objects.begin(), objects.end(), obj);
        if (it == objects.end()) return;
        const int obj_idx = int(it - objects.begin());
        wxGetApp().obj_list()->add_object_to_list(obj_idx);
        wxGetApp().obj_list()->select_item(wxGetApp().obj_list()->GetModel()->GetItemById(obj_idx));
        wxGetApp().obj_list()->update_info_items(obj_idx);
    };

    auto select_or_create_target = [&](ModelObject* cur_target) -> ModelObject* {
        // Part mode: do operation on current object
        if (settings.target_mode == BooleanTargetMode::Part) {
            if (cur_target) return cur_target;
            for (ModelVolume* sv : result.source_volumes)
                if (sv && sv->get_object()) return sv->get_object();
            return nullptr;
        }

        // Object mode: always create a fresh object for results
        ModelObject* first_source_object = nullptr;
        for (ModelVolume* sv : result.source_volumes) {
            if (sv && sv->get_object()) { first_source_object = sv->get_object(); break; }
        }
        Model* model = first_source_object ? first_source_object->get_model() : (wxGetApp().obj_list()->objects() && !wxGetApp().obj_list()->objects()->empty() ? (*wxGetApp().obj_list()->objects())[0]->get_model() : nullptr);
        if (!model) return cur_target;

        ModelObject* new_obj = model->add_object();
        if (first_source_object) {
            new_obj->name = first_source_object->name + "_Boolean";
            new_obj->config.assign_config(first_source_object->config);
        }
        if (first_source_object && !first_source_object->instances.empty() && first_source_object->instances[0])
            new_obj->add_instance(*first_source_object->instances[0]);
        else if (new_obj->instances.empty())
            new_obj->add_instance();
        if (!new_obj->instances.empty() && new_obj->instances[0] && !new_obj->instances[0]->is_assemble_initialized())
            new_obj->instances[0]->set_assemble_transformation(new_obj->instances[0]->get_transformation());

        return new_obj;
    };

    target_object = select_or_create_target(target_object);
    if (!target_object)
        return;

    // ===== STEP 1: Decide which source volumes should be replaced =====
    // "Replace" means: delete the old source volume and create new result volume in its place
    // This is different from "delete other volumes" which happens in update_delete_list()
    auto should_replace_source = [&](const ModelVolume* src) -> bool {
        if (!src || src->get_object() != target_object) return false;

        // Replacement policy depends on mode:
        // - Difference mode: ALWAYS replace A volumes (even if keeping originals, we need new A-B result)
        // - Union/Intersection: Replace only if not keeping originals
        const bool is_difference_mode = (mode == MeshBooleanOperation::Difference);
        if (!is_difference_mode && settings.keep_original_models) return false;

        // In entity_only mode, only MODEL_PART volumes participate in boolean ops
        if (settings.entity_only && !src->is_model_part()) return false;

        return true;
    };

    // ===== STEP 2: Create new result volumes =====
    // Collect sources that need to be replaced (deleted after new volumes are created)
    std::vector<ModelVolume*> sources_to_replace;

    for (size_t i = 0; i < result.result_meshes.size(); ++i) {
        if (i >= result.source_volumes.size()) break;
        ModelVolume* source_volume = result.source_volumes[i];
        if (settings.entity_only && (!source_volume || !source_volume->is_model_part()))
            continue;

        // Create the new result volume
        ModelVolume* new_volume = create_result_volume(target_object, result.result_meshes[i], source_volume);
        if (!new_volume) continue;
        new_volume->set_type(ModelVolumeType::MODEL_PART);

        // Mark source for replacement if policy allows
        if (should_replace_source(source_volume)) {
            sources_to_replace.push_back(source_volume);
        }
    }

    // ===== STEP 3: Delete source volumes (now that new ones exist) =====
    // This is the "replace" operation: delete old volumes[0] from each result
    for (ModelVolume* src : sources_to_replace) {
        auto &volumes = target_object->volumes;
        if (auto it = std::find(volumes.begin(), volumes.end(), src); it != volumes.end())
            target_object->delete_volume(std::distance(volumes.begin(), it));
    }

    // Attach ignored non-model volumes before any deletion to avoid dangling pointers
    attach_ignored_non_models_to_target(target_object, settings);

    auto perform_post_deletions = [&]() {
        if (settings.target_mode == BooleanTargetMode::Part) {
            if (!settings.keep_original_models)
                delete_volumes_from_model(result.volumes_to_delete);
            return;
        }

        // Object mode: collect objects to delete
        auto &objects = *wxGetApp().obj_list()->objects();
        std::set<int> obj_indices_to_delete;

        // Collect object indices to delete
        auto collect_indices = [&](const std::vector<ModelObject*>& obj_list) {
            for (const ModelObject* obj : obj_list) {
                if (!obj || obj == target_object) continue;
                if (auto it = std::find(objects.begin(), objects.end(), obj); it != objects.end()) {
                    obj_indices_to_delete.insert(static_cast<int>(it - objects.begin()));
                }
            }
        };

        if (mode == MeshBooleanOperation::Difference) {
            // Difference: Always delete A group objects
            collect_indices(a_group_objects);
            // Delete B group objects only if keep_original_models is false
            if (!settings.keep_original_models) {
                collect_indices(b_group_objects);
            }
        } else {
            // Union/Intersection: Delete all participating objects only if keep_original_models is false
            if (!settings.keep_original_models) {
                collect_indices(participating_objects);
            }
        }

        if (!obj_indices_to_delete.empty()) {
            std::vector<ItemForDelete> obj_items;
            obj_items.reserve(obj_indices_to_delete.size());
            for (int idx : obj_indices_to_delete) {
                obj_items.emplace_back(ItemType::itObject, idx, -1);
            }
            wxGetApp().obj_list()->delete_from_model_and_list(obj_items);
        }
    };
    perform_post_deletions();

    auto refresh_sidebar_for = [](ModelObject* obj) {
        if (!obj) return;
        auto &objects = *wxGetApp().obj_list()->objects();
        auto it = std::find(objects.begin(), objects.end(), obj);
        if (it == objects.end()) return;
        const int obj_idx = int(it - objects.begin());
        wxGetApp().obj_list()->update_info_items(obj_idx);
        wxGetApp().obj_list()->reorder_volumes_and_get_selection(obj_idx);
        wxGetApp().obj_list()->changed_object(obj_idx);
    };

    // For object mode, add to sidebar after all volumes are created and configured
    // This ensures proper material/extruder configuration before wipe tower updates
    if (settings.target_mode == BooleanTargetMode::Object) {
        add_object_to_sidebar(target_object);
    }

    refresh_sidebar_for(target_object);

    if (settings.target_mode == BooleanTargetMode::Part)
       target_object->ensure_on_bed();
    wxGetApp().plater()->update();
}
//NOTE: keep watching
ModelVolume* BooleanOperationEngine::create_result_volume(ModelObject* target_object,
                                                        const TriangleMesh& result_mesh,
                                                        ModelVolume* source_volume) {
    // Convert world mesh to target's local space using target instance and source volume transforms:
    // local = (T_inst_target * T_vol_source)^{-1} * world
    TriangleMesh local_mesh = result_mesh;

    Transform3d target_instance_matrix = Transform3d::Identity();
    if (target_object && !target_object->instances.empty() && target_object->instances[0])
        target_instance_matrix = target_object->instances[0]->get_transformation().get_matrix();

    Transform3d source_volume_matrix = Transform3d::Identity();
    if (source_volume)
        source_volume_matrix = source_volume->get_transformation().get_matrix();

    // World -> (target instance * source volume) local
    Transform3d world_to_target_local = (target_instance_matrix * source_volume_matrix).inverse();
    local_mesh.transform(world_to_target_local);

    // Create new volume with the local coordinate mesh (no re-centering), then copy metadata
    ModelVolumeType result_type = source_volume->type();
    ModelVolume* new_volume = target_object->add_volume(std::move(local_mesh), result_type, false);

    // Copy properties from source volume
    new_volume->name = source_volume->name + "_Boolean";
    new_volume->set_new_unique_id();
    bool same_object = (source_volume && source_volume->get_object() == target_object);

    // Copy config: always for same object, or when source is non-MODEL_PART (needs extruder settings)
    // This is critical for avoiding "No extrusions" errors when boolean result replaces non-MODEL_PART
    if (same_object || !source_volume->is_model_part()) {
        new_volume->config.apply(source_volume->config);
    }

    new_volume->set_type(result_type);
    new_volume->set_material_id(source_volume->material_id());

    // Copy the transformation from source volume
    new_volume->set_transformation(source_volume->get_transformation());
    return new_volume;
}

void BooleanOperationEngine::delete_volumes_from_model(const std::vector<ModelVolume*>& volumes_to_delete) {
    if (volumes_to_delete.empty()) return;

    std::vector<ItemForDelete> items;
    auto& objects = *wxGetApp().obj_list()->objects();

    for (ModelVolume* volume : volumes_to_delete) {
        if (!volume) continue; // Skip null pointers

        ModelObject* obj = volume->get_object();
        if (!obj) continue; // Skip if no parent object

        // Find object index
        auto obj_it = std::find(objects.begin(), objects.end(), obj);
        if (obj_it == objects.end()) continue; // Object not in list (may have been deleted)
        int obj_idx = obj_it - objects.begin();

        // Validate that the object still exists and has volumes
        if (obj_idx < 0 || obj_idx >= (int)objects.size()) continue;
        if (objects[obj_idx] != obj) continue; // Double check object pointer matches

        // Find volume index
        auto vol_it = std::find(obj->volumes.begin(), obj->volumes.end(), volume);
        if (vol_it == obj->volumes.end()) continue; // Volume not found (may have been deleted)
        int vol_idx = vol_it - obj->volumes.begin();

        // Validate volume index
        if (vol_idx < 0 || vol_idx >= (int)obj->volumes.size()) continue;
        if (obj->volumes[vol_idx] != volume) continue; // Double check volume pointer matches

        items.emplace_back(ItemType::itVolume, obj_idx, vol_idx);
    }

    if (!items.empty()) {
        wxGetApp().obj_list()->delete_from_model_and_list(items);
    }
}

// ========================== GIZMO LIFECYCLE METHODS ==========================

GLGizmoMeshBoolean::GLGizmoMeshBoolean(GLCanvas3D& parent, unsigned int sprite_id)
    : GLGizmoBase(parent, sprite_id)
{
    m_warning_manager.clear_warnings();
    m_volume_manager.clear_all();

    // Ensure UI refreshes when warnings change (no need to move mouse)
    m_warning_manager.set_on_change([this]() { refresh_canvas(); });

    // Initialize UI layer
    m_ui = std::make_unique<MeshBooleanUI>();
    m_ui->set_volume_manager(&m_volume_manager);
    m_ui->set_warning_manager(&m_warning_manager);

    // Setup UI callbacks
    m_ui->on_execute_mesh_boolean = [this]() {
        execute_mesh_boolean();
    };
    m_ui->on_reset_operation = [this]() {
        const Selection& selection = m_parent.get_selection();
        size_t cur_snapshot_time = wxGetApp().plater()->get_active_snapshot_time();
        if(cur_snapshot_time < m_last_snapshot_time) return;
        wxGetApp().plater()->undo_redo_to(m_last_snapshot_time);
        init_volume_manager();
        m_volume_manager.update_obj_lists(selection);
        restore_list_color_overrides();
        apply_color_overrides_for_mode(m_ui->get_selected_operation());
        m_warning_manager.clear_warnings();
    };
    m_ui->on_apply_color_overrides = [this](MeshBooleanOperation mode) {
        apply_color_overrides_for_mode(mode);
    };
    m_ui->on_delete_selected = [this]() -> bool {
        // Check if deletion would violate minimums
        if (violates_minimums_for_delete(m_operation_mode, m_target_mode, m_volume_manager, m_parent.get_selection(), m_warning_manager)) {
            refresh_canvas();
            return false; // Delete blocked
        }

        // Remove from lists and deselect those actually removed
        std::set<unsigned int> removed = m_volume_manager.remove_selected_from_all_lists();
        if (!removed.empty()) {
            Selection &selection = m_parent.get_selection();
            for (unsigned int idx : removed) {
                const GLVolume* glv = selection.get_volume(idx);
                if (glv) selection.remove_volume(glv->object_idx(), glv->volume_idx());
            }
        }

        // Refresh object caches after list changes
        m_volume_manager.update_obj_lists(m_parent.get_selection());

        // Handle color overrides
        if (m_color_overrides.applied) {
            restore_list_color_overrides();
        }
        m_warning_manager.clear_warnings();
        refresh_canvas();
        return true; // Delete successful
    };

}

GLGizmoMeshBoolean::~GLGizmoMeshBoolean()
{
    restore_list_color_overrides();
    m_volume_manager.clear_all();
}


// ========================== EVENT HANDLING ==========================

bool GLGizmoMeshBoolean::gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down)
{
    // Only handle events when the gizmo is active

        const Selection& selection = m_parent.get_selection();
        const GLVolume* clicked_volume = get_first_hovered_gl_volume(m_parent);

        if (clicked_volume && clicked_volume->object_idx() >= 0) {
            // Use global GLVolume index from canvas hover, not per-object volume_idx
            int hovered_idx = m_parent.get_first_hover_volume_idx();
            if (hovered_idx < 0) return false;
            unsigned int volume_idx = static_cast<unsigned int>(hovered_idx);

            // Filter: only user-created model volumes (no aux types)
            ModelVolume* mv = get_model_volume(*clicked_volume, selection.get_model()->objects);
            if (mv == nullptr) return true; // consume click but do not add
            if (clicked_volume->is_wipe_tower || clicked_volume->is_extrusion_path)
                return true; // consume click but do not add

            // Toggle off only gizmo's selection (bounding box), do not change global Selection
            // NOTE: check out
            {
                if (m_target_mode == BooleanTargetMode::Part) {
                    if (m_volume_manager.get_selected_objects().count(volume_idx) > 0) {
                        m_volume_manager.remove_from_selection(volume_idx);
                        refresh_canvas();
                        return true;
                    }
                } else { // Object mode: deselect the whole object's volumes
                    int obj_idx_clicked = clicked_volume->object_idx();
                    std::vector<unsigned int> vols = selection.get_volume_idxs_from_object((unsigned int)obj_idx_clicked);
                    bool any_selected = false;
                    for (unsigned int v : vols) {
                        if (m_volume_manager.get_selected_objects().count(v) > 0) { any_selected = true; break; }
                    }
                    if (any_selected) {
                        for (unsigned int v : vols) m_volume_manager.remove_from_selection(v);
                        refresh_canvas();
                        return true;
                    }
                }
            }

            bool added = false;
            bool already_in_list = false;

            // Add volume / Object to list
            if (m_target_mode == BooleanTargetMode::Part) {
                // Check if volume is already in the list
                const auto& working_list = m_volume_manager.get_working_list();
                const auto& list_a = m_volume_manager.get_list_a();
                const auto& list_b = m_volume_manager.get_list_b();
                already_in_list = (std::find(working_list.begin(), working_list.end(), volume_idx) != working_list.end() ||
                                   std::find(list_a.begin(), list_a.end(), volume_idx) != list_a.end() ||
                                   std::find(list_b.begin(), list_b.end(), volume_idx) != list_b.end());

                added = m_volume_manager.add_volume_to_list(volume_idx, shift_down);
            } else {
                // Object mode: ensure selection/list are object-scoped, not single mesh
                int obj_idx_clicked = clicked_volume->object_idx();
                added = m_volume_manager.add_object_to_list(obj_idx_clicked, shift_down, selection);
                // Clean stray volume indices of this object from selected set, then add full object
                std::vector<unsigned int> vols = selection.get_volume_idxs_from_object((unsigned int)obj_idx_clicked);
                for (unsigned int v : vols) m_volume_manager.remove_from_selection(v);
                for (unsigned int v : vols) m_volume_manager.add_to_selection(v);
            }

            // For Part mode: if added or already in list, update selection
            // For Object mode: only if added (existing behavior)
            if (added || (m_target_mode == BooleanTargetMode::Part && already_in_list)) {
                // Select the volume in the appropriate list
                if (m_target_mode == BooleanTargetMode::Part) {
                    m_volume_manager.add_to_selection(volume_idx);
                } else {
                    // Object mode: add all volumes of the object to the selected set
                    int obj_idx = clicked_volume->object_idx();
                    std::vector<unsigned int> vol_indices = selection.get_volume_idxs_from_object((unsigned int)obj_idx);
                    for (unsigned int v : vol_indices) m_volume_manager.add_to_selection(v);
                }

                // Also add to global selection for immediate highlight
                Selection &selection_ref = m_parent.get_selection();
                if (m_target_mode == BooleanTargetMode::Part) {
                    selection_ref.add_volume(clicked_volume->object_idx(), clicked_volume->volume_idx(), clicked_volume->instance_idx(), /*as_single_selection*/false);
                } else {
                    // Object mode: select the whole object (avoid highlighting single mesh)
                    selection_ref.add_object((unsigned int)clicked_volume->object_idx(), /*as_single_selection*/false);
                }

                // Re-apply color overrides so newly added indices get list colors immediately
                apply_color_overrides_for_mode(m_operation_mode);
                refresh_canvas();
                return true;
            }
            return true;
        }
    return false;
}


std::string GLGizmoMeshBoolean::get_icon_filename(bool b_dark_mode) const
{
    return b_dark_mode ? "toolbar_meshboolean_dark.svg" : "toolbar_meshboolean.svg";
}

bool GLGizmoMeshBoolean::is_on_same_plate(const Selection& selection) const
{
    const auto &content = selection.get_content();
    if (content.empty()) return false;

    auto &plate_list = wxGetApp().plater()->get_partplate_list();
    int target_plate = -1;
    PartPlate* target = nullptr;

    for (const auto &kv : content) {
        int obj_id = kv.first;
        if (obj_id >= 1000) continue; // skip wipe tower
        for (int inst_id : kv.second) {
            if (inst_id < 0) inst_id = 0;
            if (target_plate < 0) {
                target_plate = plate_list.find_instance(obj_id, inst_id);
                if (target_plate < 0) return false;
                target = plate_list.get_plate(target_plate);
                if (!target) return false;
            } else {
                if (!target->contain_instance_totally(obj_id, inst_id))
                    return false;
            }
        }
    }
    return true;
}

void GLGizmoMeshBoolean::apply_object_visibility(const Selection& selection) const
{
    const auto &volume_idxs = selection.get_volume_idxs();
    if (!volume_idxs.empty()) {
        const GLVolume *first = selection.get_volume(*volume_idxs.begin());
        if (first) {
            int obj_id = first->object_idx();
            int inst_id = first->instance_idx();
            if (inst_id < 0) inst_id = 0;
            auto &plate_list = wxGetApp().plater()->get_partplate_list();
            int plate_idx = plate_list.find_instance(obj_id, inst_id);
            if (plate_idx >= 0) {
                if (plate_idx == m_last_plate_idx_for_visibility) return; // no change
                // hide all, then show the same plate objects
                m_parent.toggle_model_objects_visibility(false);
                PartPlate *plate = plate_list.get_plate(plate_idx);
                if (plate) {
                    auto objs = plate->get_objects_on_this_plate();
                    for (ModelObject *mo : objs) {
                        m_parent.toggle_model_objects_visibility(true, mo, -1);
                    }
                }
                m_last_plate_idx_for_visibility = plate_idx;
            }
        }
    }
}

void GLGizmoMeshBoolean::apply_part_visibility(const Selection& selection) const
{
    const int obj_idx = selection.get_object_idx();
    if (obj_idx < 0) return;
    const int inst_idx = selection.get_instance_idx();
    m_parent.toggle_model_objects_visibility(false);
    const Model *model = m_parent.get_model();
    if (!model || obj_idx >= (int)model->objects.size()) return;
    m_parent.toggle_model_objects_visibility(true, model->objects[obj_idx], inst_idx);
}

bool GLGizmoMeshBoolean::on_init()
{
    m_shortcut_key = WXK_CONTROL_B;

    // Load UI icons
    if (m_ui) {
        m_ui->load_icons();
    }

    return true;
}

bool GLGizmoMeshBoolean::on_is_activable() const
{
    const Selection& selection = m_parent.get_selection();
    if (selection.is_empty())
        return false;

    // Object mode: select 2 or more objects (full object or instance)
    if (selection.is_multiple_full_object() || selection.is_multiple_full_instance())
        return is_on_same_plate(selection);

    // Part mode: single object, and it contains at least 2 meshes
    bool is_activa_when_off = m_state == EState::Off && (selection.is_single_full_object() || selection.is_single_full_instance());
    bool is_active_when_on = m_state == EState::On && (selection.is_from_single_object() || selection.is_from_single_instance());
    if (is_activa_when_off || is_active_when_on) {
        int obj_idx = selection.get_object_idx();
        if (obj_idx >= 0 && obj_idx < (int)selection.get_model()->objects.size()) {
            const ModelObject* obj = selection.get_model()->objects[obj_idx];
            if (obj && obj->volumes.size() >= 2)
                return true;
        }
    }
    return false;
}

std::string GLGizmoMeshBoolean::on_get_name() const
{
    if (!on_is_activable() && m_state == EState::Off) {
        return _u8L("Mesh Boolean") + ":\n" + _u8L("- Object mode: Please select at least two objects.") + "\n" + _u8L("- Part mode: Please select one object with at least two meshes.");
    }
    return _u8L("Mesh Boolean");
}

BooleanTargetMode GLGizmoMeshBoolean::update_cur_mode(const Selection& selection) const
{
    // Object mode: multiple objects
    if (selection.is_multiple_full_object())
        return BooleanTargetMode::Object;

    // Part mode: single object (contains at least 2 meshes)
    if (selection.is_single_full_object() || selection.get_object_idx() != -1) {
       if(selection.volumes_count() >= 2) return BooleanTargetMode::Part;
    }
    return BooleanTargetMode::Unknown;
}

void GLGizmoMeshBoolean::on_render()
{
    // Only render when the gizmo is active
    const Selection& selection = m_parent.get_selection();
    if (m_state != EState::On || selection.is_empty() || selection.volumes_count() < 2)
        return;
    if (m_target_mode == BooleanTargetMode::Unknown)
        return;

    // In Part / Object mode, draw bounding box for the selected target
    render_selected_bounding_boxes();
}

void GLGizmoMeshBoolean::on_set_state()
{

    m_warning_manager.clear_warnings();
    if (m_state == EState::On) {
        const Selection& selection = m_parent.get_selection();
        m_target_mode = update_cur_mode(selection);
        if (m_target_mode == BooleanTargetMode::Unknown) {
            // Target mode is invalid, exit immediately and clean up
            m_state = EState::Off;
            m_volume_manager.clear_all();
            restore_list_color_overrides();
            m_parent.toggle_model_objects_visibility(true);
            m_last_plate_idx_for_visibility = -1;
            return;
        }
        if (m_target_mode == BooleanTargetMode::Object) apply_object_visibility(selection);
        if (m_ui) m_ui->set_target_mode(m_target_mode);

        init_volume_manager();
        m_volume_manager.update_obj_lists(selection);

        // Apply color override immediately based on operation mode
        apply_color_overrides_for_mode(m_operation_mode);
        m_last_snapshot_time = wxGetApp().plater()->get_active_snapshot_time();
        wxGetApp().plater()->take_snapshot("Mesh Boolean");
    }
    else if (m_state == EState::Off) {
        //NOTE: check out
        restore_list_color_overrides();
        m_volume_manager.clear_all();
        m_target_mode = BooleanTargetMode::Unknown;
        m_parent.toggle_model_objects_visibility(true);
        m_last_plate_idx_for_visibility = -1;
    }
}

CommonGizmosDataID GLGizmoMeshBoolean::on_get_requirements() const
{
    // Object mode: do not hide other objects;
    // Part mode: hide other objects (enable InstancesHider)
    if (m_target_mode == BooleanTargetMode::Part) {
        return CommonGizmosDataID(
            int(CommonGizmosDataID::SelectionInfo)
          | int(CommonGizmosDataID::InstancesHider));
    }
    return CommonGizmosDataID(int(CommonGizmosDataID::SelectionInfo));
}

// ========================== UI RENDERING ==========================

void GLGizmoMeshBoolean::on_render_input_window(float x, float y, float bottom_limit)
{
    // Delegate to UI layer, but handle gizmo state changes here
    const Selection &sel = m_parent.get_selection();
    m_target_mode = update_cur_mode(sel);
    if (sel.get_content().empty() || m_target_mode == BooleanTargetMode::Unknown) {
        restore_list_color_overrides();
        m_state = EState::Off;
        return;
    }
    // Rebuild lists and color/visibility when selection changes (undo/redo)
    if (m_volume_manager.selection_changed(sel)) {
        init_volume_manager();
        m_volume_manager.update_obj_lists(sel);
        restore_list_color_overrides();
        apply_color_overrides_for_mode(m_operation_mode);
        // Set visibility
        if (m_target_mode == BooleanTargetMode::Object) apply_object_visibility(sel);
        else apply_part_visibility(sel);
    }

    bool has_any_items = !m_volume_manager.get_working_list().empty()
                        || !m_volume_manager.get_list_a().empty()
                        || !m_volume_manager.get_list_b().empty();
    if (!has_any_items) {
        restore_list_color_overrides();
        m_state = EState::Off;
        return;
    }

    // Manage ImGui window lifecycle (Gizmo's responsibility)
    y = std::min(y, bottom_limit - ImGui::GetWindowHeight());

    static float last_y = 0.0f;
    static float last_w = 0.0f;

    const float currt_scale = m_parent.get_scale();
    ImGuiWrapper::push_toolbar_style(currt_scale);
    GizmoImguiSetNextWIndowPos(x, y, ImGuiCond_Always);
    GizmoImguiBegin("MeshBoolean", ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    // Delegate content rendering to UI layer
    if (m_ui) {
        m_ui->set_operation_mode(m_operation_mode);
        m_ui->set_target_mode(m_target_mode);
        m_ui->set_keep_original_models(m_keep_original_models);
        m_ui->set_entity_only(m_entity_only);

        m_ui->render_content(m_parent, GLGizmoBase::m_imgui, GLGizmoBase::m_is_dark_mode);

        if (m_keep_original_models != m_ui->get_keep_original_models() ||
            m_entity_only != m_ui->get_entity_only() ||
            m_operation_mode != m_ui->get_selected_operation()){
            m_warning_manager.clear_warnings();
        }

        // Get updated values from UI
        m_operation_mode = m_ui->get_selected_operation();
        m_target_mode = m_ui->get_target_mode();
        m_keep_original_models = m_ui->get_keep_original_models();
        m_entity_only = m_ui->get_entity_only();
    }

    // Close ImGui window (Gizmo's responsibility)
    float win_w = ImGui::GetWindowWidth();
    if (last_w != win_w || last_y != y) {
        // ask canvas for another frame to render the window in the correct position
        m_imgui->set_requires_extra_frame();
        refresh_canvas();
        if (last_w != win_w)
            last_w = win_w;
        if (last_y != y)
            last_y = y;
    }

    GizmoImguiEnd();
    ImGuiWrapper::pop_toolbar_style();
}

void GLGizmoMeshBoolean::render_selected_bounding_boxes()
{
    const Selection &selection = m_parent.get_selection();
    const auto hover_rgba = abgr_u32_to_rgba(MeshBooleanConfig::COLOR_HOVER_BORDER);
    float border_color_rgb[3] = { hover_rgba[0], hover_rgba[1], hover_rgba[2] };

    if (m_target_mode == BooleanTargetMode::Object) {
        // Object mode: draw bounding box for the selected object
        std::set<int> objects_drawn;
        for (unsigned int v_idx : m_volume_manager.get_selected_objects()) {
            const GLVolume *glv = selection.get_volume(v_idx);
            if (!glv) continue;
            int obj_idx = glv->object_idx();
            if (objects_drawn.count(obj_idx)) continue;
            objects_drawn.insert(obj_idx);

            std::vector<unsigned int> vols = selection.get_volume_idxs_from_object((unsigned int)obj_idx);
            BoundingBoxf3 merged; merged.reset();
            for (unsigned int vi : vols) {
                const GLVolume *gv = selection.get_volume(vi);
                if (!gv) continue;
                BoundingBoxf3 bb = gv->transformed_convex_hull_bounding_box();
                if (!bb.defined) continue;
                if (!merged.defined) { merged = bb; merged.defined = true; }
                else merged.merge(bb);
            }
            if (merged.defined) const_cast<Selection&>(selection).render_bounding_box(merged, border_color_rgb, MeshBooleanConfig::CORNER_BOX_LINE_SCALE);
        }
    } else {
        // Part mode: draw bounding box for each selected part (allow non-entity)
        for (unsigned int v_idx : m_volume_manager.get_selected_objects()) {
            const GLVolume *glv = selection.get_volume(v_idx);
            if (!glv) continue;
            BoundingBoxf3 bb = glv->transformed_convex_hull_bounding_box();
            if (!bb.defined) continue;
            const_cast<Selection&>(selection).render_bounding_box(bb, border_color_rgb, MeshBooleanConfig::CORNER_BOX_LINE_SCALE);
        }
    }
}

void GLGizmoMeshBoolean::init_volume_manager(){
    const Selection& selection = m_parent.get_selection();
    if (m_target_mode == BooleanTargetMode::Part) m_volume_manager.init_part_mode_lists(selection);
    else m_volume_manager.init_object_mode_lists(selection);
}

void GLGizmoMeshBoolean::apply_color_overrides_for_mode(MeshBooleanOperation mode)
{
    if (mode == MeshBooleanOperation::Difference) apply_a_b_list_color_overrides(mode);
    else apply_working_list_color_overrides(mode);
}

void GLGizmoMeshBoolean::on_change_color_mode(bool is_dark)
{
    GLGizmoBase::on_change_color_mode(is_dark); // Udate m_is_dark_mode
    bool current_is_dark = m_is_dark_mode;

    // Force redraw to update col = trueors immediately
    set_dirty();
    refresh_canvas();

    //TODO: Rescale when dpi changed
    // Recreate UI icons when scale or theme may change to keep crisp
    if (m_ui) {
        // allow reload at new scale
        // m_ui->reload_icons_if_scale_changed(); // future hook
        // For now, just mark not loaded so next on_init()/open will reload
        // (safe because load_icons is idempotent)
    }
}

void GLGizmoMeshBoolean::on_load(cereal::BinaryInputArchive &ar)
{
    ar(m_enable, m_operation_mode, m_target_mode, m_keep_original_models, m_entity_only);
}

void GLGizmoMeshBoolean::on_save(cereal::BinaryOutputArchive &ar) const
{
    ar(m_enable, m_operation_mode, m_target_mode, m_keep_original_models, m_entity_only);
}

// ========================== BOOLEAN OPERATION EXECUTION ==========================

void GLGizmoMeshBoolean::execute_mesh_boolean()
{
    Plater::TakeSnapshot Snap(wxGetApp().plater(), "Bool execution");
    if (m_state != EState::On)
        return;

    const Selection& selection = m_parent.get_selection();

    // Validate operation
    std::string validation_error = m_boolean_engine.validate_operation(m_operation_mode, m_volume_manager);
    if (!validation_error.empty()) {
        m_warning_manager.add_warning(validation_error);
        return;
    }

    // Prepare settings
    BooleanOperationSettings settings;
    settings.keep_original_models = m_keep_original_models;
    settings.entity_only = m_entity_only;
    settings.target_mode = m_target_mode;

    // Map current single non-model handling choice to settings
    {
        // Collect non-model volumes in current participating lists (depends on mode)
        const Selection& sel = m_parent.get_selection();
        auto collect_non_models = [&](const std::vector<unsigned int>& vec, bool is_b = false) {
            for (unsigned int idx : vec) {
                const GLVolume* glv = sel.get_volume(idx);
                if (!glv) continue;
                ModelVolume* mv = get_model_volume(*glv, sel.get_model()->objects);
                if (!mv) continue;
                if (!mv->is_model_part()) {
                    if (is_b && m_target_mode == BooleanTargetMode::Object) continue;
                    else settings.non_model_volumes_to_attach.push_back(mv);
                }
            }
        };
        if (m_operation_mode == MeshBooleanOperation::Difference) {
            collect_non_models(m_volume_manager.get_list_a());
            collect_non_models(m_volume_manager.get_list_b(), true);
        } else {
            collect_non_models(m_volume_manager.get_working_list());
        }
    }


    // Validate A/B/Working lists. Only rebuild if indices are stale (e.g., after undo/redo).
    auto lists_valid = [&](const std::vector<unsigned int>& vec) -> bool {
        for (unsigned int idx : vec) if (selection.get_volume(idx) == nullptr) return false;
        return true;
    };
    bool valid = lists_valid(m_volume_manager.get_working_list()) &&
                 lists_valid(m_volume_manager.get_list_a()) &&
                 lists_valid(m_volume_manager.get_list_b());
    if (!valid) {
        m_volume_manager.clear_all();
        init_volume_manager();
    }

    auto perform_current_operation = [&](MeshBooleanOperation mode) {
        ModelObject* current_model_object = m_c->selection_info() ? m_c->selection_info()->model_object() : nullptr;
        int current_selected_index = m_parent.get_selection().get_object_idx();

        BooleanOperationResult result;
        switch (mode) {
            case MeshBooleanOperation::Union:
                result = m_boolean_engine.perform_union(m_volume_manager, selection, settings);
                break;
            case MeshBooleanOperation::Intersection:
                result = m_boolean_engine.perform_intersection(m_volume_manager, selection, settings);
                break;
            case MeshBooleanOperation::Difference:
                result = m_boolean_engine.perform_difference(m_volume_manager, selection, settings);
                break;
            default:
                result.success = false;
                result.error_message = "Unknown operation type";
                break;
        }

        if (result.success) {
            // For Part mode: clear color overrides before applying result (which may delete volumes)
            // to avoid index mismatch after volume deletion
            if (settings.target_mode == BooleanTargetMode::Part && m_color_overrides.applied) {
                m_color_overrides.clear();
            }

            // Collect unique ModelObject* from volume indices
            auto collect_objects = [&](const std::vector<unsigned int>& idxs) -> std::vector<ModelObject*> {
                std::unordered_set<ModelObject*> unique_objects;
                for (unsigned int v_idx : idxs) {
                    const GLVolume* glv = selection.get_volume(v_idx);
                    if (!glv) continue;
                    ModelVolume* mv = get_model_volume(*glv, selection.get_model()->objects);
                    if (!mv) continue;
                    ModelObject* mo = mv->get_object();
                    if (mo) unique_objects.insert(mo);
                }
                return std::vector<ModelObject*>(unique_objects.begin(), unique_objects.end());
            };

            // Set target mode once for all operations
            settings.target_mode = m_target_mode;

            // Collect objects and apply result based on operation type
            if (mode == MeshBooleanOperation::Difference) {
                auto a_objects = collect_objects(m_volume_manager.get_list_a());
                auto b_objects = collect_objects(m_volume_manager.get_list_b());

                std::vector<ModelObject*> participants;
                participants.reserve(a_objects.size() + b_objects.size());
                participants.insert(participants.end(), a_objects.begin(), a_objects.end());
                participants.insert(participants.end(), b_objects.begin(), b_objects.end());

                m_boolean_engine.apply_result_to_model(result, current_model_object, current_selected_index,
                    settings, mode, participants, a_objects, b_objects);
            } else {
                auto participants = collect_objects(m_volume_manager.get_working_list());
                m_boolean_engine.apply_result_to_model(result, current_model_object, current_selected_index,
                    settings, mode, participants, {}, {});
            }
            m_warning_manager.clear_warnings();
            m_volume_manager.clear_all();
            if (check_if_active()){
                init_volume_manager();
                apply_color_overrides_for_mode(mode);
            }
        } else {
            m_warning_manager.add_warning(result.error_message);
        }
    };

    // Execute boolean operation synchronously
    perform_current_operation(m_operation_mode);
}

// Helper to convert ARGB (0xAARRGGBB) to RGBA floats [0,1]
std::array<float, 4> GLGizmoMeshBoolean::abgr_u32_to_rgba(unsigned int abgr) const
{
    // ImGui style colors are usually in ABGR packed order (0xAABBGGRR)
    float a = float((abgr >> 24) & 0xFF) / 255.f;
    float b = float((abgr >> 16) & 0xFF) / 255.f;
    float g = float((abgr >> 8)  & 0xFF) / 255.f;
    float r = float((abgr >> 0)  & 0xFF) / 255.f;
    return { r, g, b, a };
}

void GLGizmoMeshBoolean::apply_color_overrides_for_list(const std::vector<unsigned int>& list,
                                                        const Selection& selection,
                                                        const std::array<float, 4>& color_for_model_part,
                                                        bool entity_only)
{
    if (entity_only) {
        for (unsigned int idx : list) {
            const GLVolume* glv = selection.get_volume(idx);
            if (!glv) continue;
            ModelVolume* mv = get_model_volume(*glv, selection.get_model()->objects);
            if (!mv) continue;
            if (mv->is_model_part())
                m_color_overrides.apply_for_indices(selection, {idx}, color_for_model_part);
            else
                m_color_overrides.apply_for_indices(selection, {idx}, abgr_u32_to_rgba(MeshBooleanConfig::COLOR_NON_MODEL));
        }
    } else {
        m_color_overrides.apply_for_indices(selection, list, color_for_model_part);
    }
}

// ========================== COLOR MANAGEMENT ==========================

// Helper methods for color override management
bool GLGizmoMeshBoolean::get_cur_entity_only() const
{
    return m_ui ? m_ui->get_entity_only() : m_entity_only;
}

void GLGizmoMeshBoolean::apply_color_overrides_to_lists(
    const std::vector<std::pair<const std::vector<unsigned int>&, std::array<float, 4>>>& lists_and_colors,
    bool entity_only)
{
    const auto &selection = m_parent.get_selection();
    for (const auto& [list, color] : lists_and_colors) {
        apply_color_overrides_for_list(list, selection, color, entity_only);
    }

    refresh_canvas();
}

void GLGizmoMeshBoolean::refresh_canvas()
{
    m_parent.set_as_dirty();
    m_parent.request_extra_frame();
}

void GLGizmoMeshBoolean::apply_a_b_list_color_overrides(MeshBooleanOperation mode)
{
    if (m_state != EState::On || mode != MeshBooleanOperation::Difference)
        return;

    const auto color_a = abgr_u32_to_rgba(MeshBooleanConfig::COLOR_LIST_A);
    const auto color_b = abgr_u32_to_rgba(MeshBooleanConfig::COLOR_LIST_B);

    apply_color_overrides_to_lists({
        {m_volume_manager.get_list_a(), color_a},
        {m_volume_manager.get_list_b(), color_b}
    }, get_cur_entity_only());
}

void GLGizmoMeshBoolean::apply_working_list_color_overrides(MeshBooleanOperation mode)
{
    if (m_state != EState::On ||
        (mode != MeshBooleanOperation::Union && mode != MeshBooleanOperation::Intersection))
        return;

    const auto working_color = abgr_u32_to_rgba(MeshBooleanConfig::COLOR_LIST_A);

    apply_color_overrides_to_lists({
        {m_volume_manager.get_working_list(), working_color}
    }, get_cur_entity_only());
}

void GLGizmoMeshBoolean::restore_list_color_overrides()
{
    if (!m_color_overrides.applied) return;
    m_color_overrides.restore_non_selected_indices(m_parent.get_selection());
    m_parent.update_volumes_colors_by_extruder(); // wipe tower update here
    refresh_canvas();
}

} // namespace GUI
} // namespace Slic3r