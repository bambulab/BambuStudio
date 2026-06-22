#include "AssemblyStepsUtils.hpp"
#include "AssemblyStepsUtilsInternal.hpp"
#include "AssemblyPdfExportDialog.hpp"
#include "AssemblyExportProgressWindow.hpp"
#include "TinyExportMardDown.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/PNGReadWrite.hpp"
#include "libslic3r/AppConfig.hpp"

#include "../I18N.hpp"
#include "../ImGuiWrapper.hpp"
#include "../GUI_App.hpp"
#include "../GUI.hpp"
#include "../GLCanvas3D.hpp"
#include "../MainFrame.hpp"
#include "../Plater.hpp"
#include "../NotificationManager.hpp"
#include "../Gizmos/GLGizmoMeasure.hpp"//use ndc_to_ss_matrix_inverse
#include "../OpenGLManager.hpp"
#include "../imgui/imgui_stdlib.h"
#include "../MP4/PBOReader.hpp"
#include "../MP4/Mp4Recorder.hpp"

#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <boost/filesystem.hpp>
#include <boost/nowide/cstdio.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <wx/filedlg.h>
#include <wx/glcanvas.h>
#include <imgui/imgui_internal.h>

#define _steps_nodes m_model->get_assembly_steps_tree_data().nodes
#define _steps_roots m_model->get_assembly_steps_tree_data().roots

namespace Slic3r {
namespace GUI {
using namespace Slic3r;

namespace {
// UTF-8 label clipping helpers, only used by the ImGui panels below.
std::string utf8_truncate_with_ellipsis(const std::string &s, size_t max_chars)
{
    size_t count = 0;
    size_t i     = 0;
    while (i < s.size() && count < max_chars) {
        const unsigned char b = static_cast<unsigned char>(s[i]);
        size_t adv = 1;
        if      ((b & 0x80) == 0x00) adv = 1;
        else if ((b & 0xE0) == 0xC0) adv = 2;
        else if ((b & 0xF0) == 0xE0) adv = 3;
        else if ((b & 0xF8) == 0xF0) adv = 4;
        if (i + adv > s.size()) break;
        i += adv;
        ++count;
    }
    if (i >= s.size())
        return s;
    return s.substr(0, i) + "...";
}

std::string utf8_fit_with_ellipsis(const std::string &s, float max_width)
{
    if (max_width <= 0.0f)
        return std::string();
    if (ImGui::CalcTextSize(s.c_str()).x <= max_width)
        return s;

    const std::string ellipsis = "...";
    const float ellipsis_w = ImGui::CalcTextSize(ellipsis.c_str()).x;
    if (ellipsis_w >= max_width)
        return ellipsis;

    std::string best;
    size_t i = 0;
    while (i < s.size()) {
        const unsigned char b = static_cast<unsigned char>(s[i]);
        size_t adv = 1;
        if      ((b & 0x80) == 0x00) adv = 1;
        else if ((b & 0xE0) == 0xC0) adv = 2;
        else if ((b & 0xF0) == 0xE0) adv = 3;
        else if ((b & 0xF8) == 0xF0) adv = 4;
        if (i + adv > s.size())
            break;

        const std::string candidate = s.substr(0, i + adv) + ellipsis;
        if (ImGui::CalcTextSize(candidate.c_str()).x > max_width)
            break;
        best = candidate;
        i += adv;
    }

    return best.empty() ? ellipsis : best;
}
} // namespace
void AssemblyStepsUtils::refresh_guide_show_part_numbers_from_current()
{
    auto *entries = get_current_kf_entries();
    if (entries && m_keyframe_selected >= 0 && m_keyframe_selected < (int) entries->size()) {
        KeyFrameEntry &entry = (*entries)[m_keyframe_selected];
        // Track the labels-show mode of the frame we just switched to.
        m_cur_labels_show_type = entry.data.labels_show_type;
        AssemblyNote &note = entry.data.assembly_note;
        const int current_folder = find_parent_folder(m_selected_node);
        if (is_empty_structure_step(current_folder)) {
            if (note.show_part_labels || !note.part_number_labels.empty()) {
                note.show_part_labels = false;
                note.part_number_labels.clear();
                entry.need_save = true;
                save_assembly_steps_json_to_model();
                do_commond_callback("dirty");
            }
            m_guide_show_part_numbers = false;
            return;
        }
        m_guide_show_part_numbers = note.show_part_labels;
        if (m_guide_show_part_numbers && note.part_number_labels.empty()){
            toggle_part_number_labels();///*user_initiated=*/true ,check clear_all_keyframe_part_number_labels();
        }
    } else {
        m_guide_show_part_numbers = false;
    }
}

void AssemblyStepsUtils::render_main(float canvas_w, float canvas_h) {
    if (!m_camera || !m_volumes || !m_model) { return;}
    auto &sc = m_imgui_scale;
    //logic
    play_cur_keyframe_logic();
    sync_canvas_selection_state();
    // Detect a selection-cleared edge ("a step was selected last frame, and
    auto_apply_final_assembly_on_selection_cleared();
    if (!m_camera->get_view_matrix().matrix().isApprox(m_last_view_matrix_for_anchor_.matrix()) ||
        !m_camera->get_projection_matrix().matrix().isApprox(m_last_proj_matrix_for_anchor_.matrix())) {
        m_selected_screen_center_dirty_ = true;
    }
    // Re-fit the current step when the framing it was computed for is no longer
    if (!is_play_or_export_mode()) {
        const std::array<int, 4> vp           = m_camera->get_viewport();
        const bool               vp_valid     = vp[2] > 0 && vp[3] > 0;
        const bool               vp_seeded    = m_last_fit_viewport_[2] != 0 || m_last_fit_viewport_[3] != 0;
        const bool               vp_changed   = vp[2] != m_last_fit_viewport_[2] || vp[3] != m_last_fit_viewport_[3];
        // Only the first seeded change is a real resize; the un-seeded first frame
        // is just initialization and must not stomp the entry framing.
        const bool               want_refit   = m_refit_camera_pending_ || (vp_valid && vp_seeded && vp_changed);
        if (vp_valid && vp_changed)
            m_last_fit_viewport_ = vp;
        if (want_refit) {
            m_refit_camera_pending_ = false;
            if (KeyFrameEntry *entry = get_selected_keyframe_entry()) {
                // A user-framed keyframe must keep its stored camera across a viewport
                if (entry->data.camera_user_defined) {
                    apply_camera(entry->data);
                    rescale_user_camera_zoom_to_viewport(entry->data);
                } else {
                    fit_camera_to_current_step_main_plane(entry->data.camera_margin_factor);
                }
                if (m_guide_show_part_numbers)
                    m_pn_autolayout_pending = true; // re-layout labels for the re-fit view
                m_selected_screen_center_dirty_ = true;
                do_commond_callback("request_extra_frame");
            }
        }
    }
    update_step_screen_center();
    update_part_number_label_forbidden_layout_areas(canvas_w, canvas_h);
    //imgui
    render_assembly_notes_on_canvas(m_selected_screen_center_);
    if (should_show_panels()) {    // Top-left "Assembly Structure" panel (Figma 732:10276).
        render_assembly_structure_panel(canvas_w, canvas_h);
        //Right-side "Assembly Guide" panel.
        const float guide_w = std::max(260.0f * sc, std::min(300.0f * sc, canvas_w * 0.20f));
        const float guide_x = canvas_w - guide_w - 12.0f * sc;
        const float guide_y_base = 14.0f * sc;
        // get_guide_panel_y_offset() also sets m_export_btn_corner_mode via a precise
        // export-button vs gizmo-toolbar AABB intersection test.
        m_export_btn_canvas_w = canvas_w;
        const float guide_offset = get_guide_panel_y_offset(guide_x, guide_y_base, guide_w, sc);
        const float guide_y = guide_y_base + guide_offset;
        const float guide_h = canvas_h - guide_y - 20.0f * sc;
        render_assembly_guide_panel(guide_x, guide_y, guide_w, guide_h, sc, m_is_dark);
    } else {
        m_assembly_structure_right_x = 0.f;
        m_panel_rect_structure_min = m_panel_rect_structure_max = ImVec2(0, 0);
        m_panel_rect_guide_min = m_panel_rect_guide_max = ImVec2(0, 0);
    }
    // Bottom-centered play bar (Figma node 732:22413). Keep it visible for
    // normal playback, including "play all frames"; hide it only for exports.
    if (!is_export_mode()) {
        const float assemble_control_clearance = 95.0f * sc;
        const float play_bar_bottom_y          = canvas_h - assemble_control_clearance;
        render_assemble_play_bar(canvas_w, play_bar_bottom_y);
    } else {
        m_panel_rect_playbar_min = m_panel_rect_playbar_max = ImVec2(0, 0);
    }

    if (is_show_video_title_mode()) {
        // Resolve which title to draw centered on the canvas:
        std::string title;
        // is_cover_phase: only the very first phase of the MP4 export intro
        bool is_cover_phase = false;
        if (m_video_intro_active) {
            if (m_video_intro_phase == 0) {
                title = m_pdf_export_title;
                if (title.empty() && wxGetApp().plater()) {
                    std::string proj = wxGetApp().plater()->get_project_name().ToUTF8().data();
                    if (!proj.empty())
                        title = std::move(proj);
                }
                if (title.empty())
                    title = _u8L("Assembly Guide");
                is_cover_phase = true;
            } else {
                // Phase 1: pick the folder for the very first playable frame.
                if (!m_play_frame_refs.empty()) {
                    const int folder_idx = find_parent_folder(m_play_frame_refs.front().node_idx);
                    if (folder_idx >= 0 && folder_idx < (int) _steps_nodes.size())
                        title = assembly_step_display_name(_steps_nodes[folder_idx]);
                }
            }
        } else if (m_pending_global_frame_index > 0) {
            const int ref_idx = m_pending_global_frame_index - 1;
            if (ref_idx >= 0 && ref_idx < (int)m_play_frame_refs.size()) {
                const int folder_idx = find_parent_folder(m_play_frame_refs[ref_idx].node_idx);
                if (folder_idx >= 0 && folder_idx < (int) _steps_nodes.size())
                    title = assembly_step_display_name(_steps_nodes[folder_idx]);
            }
        }
        if (title.empty())
            title = _u8L("Assembly Step");

        ImDrawList *dl = ImGui::GetForegroundDrawList();
        ImFont *font = ImGui::GetFont();
        const float title_font_size = std::max(48.0f * sc, ImGui::GetFontSize() * 3.0f);
        const ImVec2 text_size = font->CalcTextSizeA(title_font_size, FLT_MAX, 0.0f, title.c_str());
        const ImVec2 pos((canvas_w - text_size.x) * 0.5f, (canvas_h - text_size.y) * 0.5f);
        // Dark mode: use white text; the near-black title is unreadable on the dark canvas.
        const ImU32 title_col = m_is_dark ? IM_COL32(255, 255, 255, 255) : IM_COL32(38, 46, 48, 255);
        dl->AddText(font, title_font_size, pos, title_col, title.c_str());

        if (is_cover_phase) {
            const std::string subtitle = std::string("---- ") + _u8L("Assembly Guide");
            const float subtitle_font_size = std::max(16.0f * sc, ImGui::GetFontSize() * 1.1f);
            const ImVec2 sub_size = font->CalcTextSizeA(subtitle_font_size, FLT_MAX, 0.0f, subtitle.c_str());
            const float title_right = pos.x + text_size.x;
            const float line_gap = 6.0f * sc;
            const float sub_x = title_right - sub_size.x;
            const float sub_y = pos.y + text_size.y + line_gap;
            dl->AddText(font, subtitle_font_size,
                ImVec2(sub_x, sub_y), title_col, subtitle.c_str());
        }
    }
    if (ImGui::IsMouseClicked(0)) {
        if (is_mouse_over_blocking_panel() &&
            m_selection_origin != SelectionOrigin::NoteColorControl &&
            m_selection_origin != SelectionOrigin::ImGuiNote) {
            set_selection_origin(SelectionOrigin::ImGui);
            return;
        }

        if (is_note_edit_controls_visible() &&
            (m_selection_origin == SelectionOrigin::None ||
             m_selection_origin == SelectionOrigin::ImGui)) {
            exit_note_edit();
            do_commond_callback("dirty");
        }
    }
}

void AssemblyStepsUtils::render_assemble_play_bar(float canvas_w, float bottom_y)
{
    if (!m_imgui || !m_model)
        return;

    // Refresh the global play queue if dirty so step count / current step are accurate.
    if (m_play_frame_refs_dirty)
        rebuild_play_frame_refs();
    if (m_play_frame_refs.empty())
        return;
    auto &sc = m_imgui_scale;
    // Step folders still provide the display label, while the progress bar is
    // driven by every playable keyframe.
    const std::vector<int> step_node_idxs = sorted_step_nodes();
    const int total_frames = static_cast<int>(m_play_frame_refs.size());
    if (total_frames <= 0)
        return;

    // Locate the current step (1-based) from the current global play frame's parent folder.
    const int cur_global = std::clamp(m_assembly_play_index, 1, total_frames);
    const PlayFrameRef &cur_ref = m_play_frame_refs[cur_global - 1];
    const int cur_node_idx = cur_ref.node_idx;
    const bool disable_play_controls = m_keyframe_playing;
    int cur_step_1based = 1;
    for (int i = 0; i < static_cast<int>(step_node_idxs.size()); ++i) {
        if (step_node_idxs[i] == cur_node_idx) { cur_step_1based = i + 1; break; }
    }

    std::string cur_step_name;
    std::string cur_frame_kind;
    if (cur_node_idx >= 0 && cur_node_idx < static_cast<int>(_steps_nodes.size())) {
        const AssemblyStepsTreeNode &cur_node = _steps_nodes[cur_node_idx];
        cur_step_name = assembly_step_display_name(cur_node);
        if (cur_ref.frame_idx >= 0 && cur_ref.frame_idx < static_cast<int>(cur_node.kf_data.entries.size())) {
            const KeyFrameEntry &cur_entry = cur_node.kf_data.entries[cur_ref.frame_idx];
            if (cur_entry.is_start())
                cur_frame_kind = _u8L("Start frame");
            else if (cur_entry.is_last())
                cur_frame_kind = _u8L("End frame");
            else
                cur_frame_kind = _u8L("Transition frame");
        }
    }
    if (cur_step_name.empty())
        cur_step_name = std::to_string(cur_step_1based);
    if (cur_frame_kind.empty())
        cur_frame_kind = _u8L("Frame");

    std::string cur_label = _u8L("step") + " " + std::to_string(cur_step_1based);
    if (cur_node_idx >= 0 && cur_node_idx < static_cast<int>(_steps_nodes.size()) && cur_ref.frame_idx >= 0 &&
        cur_ref.frame_idx < static_cast<int>(_steps_nodes[cur_node_idx].kf_data.entries.size()) && _steps_nodes[cur_node_idx].kf_data.entries[cur_ref.frame_idx].is_start()) {
        cur_label += "(start frame)";
    }

    // Speed multiplier shown in the pill. m_play_transition_duration is the time per
    // frame; speed = 1 / duration. Clicking the pill cycles 1.0x -> 0.5x -> 1.5x -> 2.0x.
    const float speed_mult = static_cast<float>(m_play_transition_duration > 0.0 ? (m_play_transition_expect_duration / m_play_transition_duration): 1.0);
    char speed_text[16];
    std::snprintf(speed_text, sizeof(speed_text), "%.1fx", speed_mult);

    // ---- Layout constants (all match Figma node 732:22413, scaled by `sc`) ----
    const float PLAY_BTN_SZ      = 24.0f * sc;
    // Visually match the nav buttons: their 16px arrows fill a 24px dark box,
    const float PLAY_ICON_SZ     = 20.0f * sc;
    // Use the same font size as render_assembly_guide_export_button so the
    // Export button "Export" label, the speed pill "1.0x", the step number
    // inside the circle, and the step name below all read at the same scale.
    const float BAR_FONT_PX      = ImGui::GetFontSize();
    // Speed pill height grows with the font so a larger system font never clips
    // descenders inside the pill (Figma spec is 24).
    const float SPEED_BADGE_H    = std::max(24.0f * sc, BAR_FONT_PX + 8.0f * sc);
    const float SPEED_PAD_X      = 8.0f * sc;
    const float SPEED_FONT_PX    = BAR_FONT_PX;
    const float GAP_SECTION1     = 8.0f * sc;   // play -> speed
    // Figma's outer flex gap is 12px on a 11px font. ImGui::GetFontSize() is
    // typically larger, so the bar and circle grow while a fixed 12px gap
    // starts to look pinched. Scale the gaps with the font so the speed pill
    // and the nav buttons stop hugging the progress track at system fonts.
    const float GAP_S1_TO_BAR    = std::max(12.0f * sc, BAR_FONT_PX + 4.0f * sc); // section1 -> progress
    const float PROGRESS_W       = 558.0f * sc;
    const float BAR_H            = 6.0f * sc;
    // Circle must comfortably hold the step number at BAR_FONT_PX. Figma spec is
    // 18 (Figma 11px font); we grow it just enough to keep a margin around the
    // larger system font, but never smaller than the spec.
    const float CIRCLE_D         = std::max(18.0f * sc, BAR_FONT_PX + 6.0f * sc);
    const float TICK_W           = 1.0f * sc;
    const float TICK_HALF        = 3.0f * sc;   // half tick height extending above/below bar
    const float LABEL_FONT_PX    = BAR_FONT_PX;
    const float GAP_BAR_TO_NAV   = std::max(16.0f * sc, BAR_FONT_PX + 8.0f * sc); // section2 -> section3
    const float NAV_BTN_SZ       = 24.0f * sc;
    const float NAV_ICON_SZ      = 16.0f * sc;
    const float NAV_BTN_ROUND    = 5.333f * sc;
    const float NAV_GAP          = 8.0f * sc;

    // Pre-measure speed badge so total width is correct.
    ImFont *font = ImGui::GetFont();
    const ImVec2 speed_text_sz = font
        ? font->CalcTextSizeA(SPEED_FONT_PX, FLT_MAX, 0.0f, speed_text)
        : ImVec2(24.0f * sc, 16.0f * sc);
    const float SPEED_BADGE_W = std::max(SPEED_BADGE_H, speed_text_sz.x + 2.0f * SPEED_PAD_X);

    const float TOTAL_W = PLAY_BTN_SZ + GAP_SECTION1 + SPEED_BADGE_W
                        + GAP_S1_TO_BAR + PROGRESS_W
                        + GAP_BAR_TO_NAV + NAV_BTN_SZ + NAV_GAP + NAV_BTN_SZ;
    // main_cy is the vertical center of the top row (play, speed, progress, nav).
    // It must be at least half the tallest element so nothing clips above the window.
    const float top_half = std::max({PLAY_BTN_SZ * 0.5f, SPEED_BADGE_H * 0.5f, CIRCLE_D * 0.5f});
    // 21.74 (was 15.74) widens the gap between the step circle and the label below
    // it; keep this in sync with the label_top offset further down.
    const float TOTAL_H = top_half + std::max(top_half + 4.0f * sc, 21.74f * sc + LABEL_FONT_PX + 4.0f * sc);

    // Window position: centered horizontally, anchored so its bottom edge sits at `bottom_y`.
    const float win_x = canvas_w * 0.5f - TOTAL_W * 0.5f;
    const float win_y = bottom_y - TOTAL_H + 15;

    ImGui::SetNextWindowPos(ImVec2(win_x, win_y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(TOTAL_W, TOTAL_H), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));

    m_imgui->begin(std::string("##assembly_play_bar"),
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus);

    m_panel_rect_playbar_min = ImVec2(win_x, win_y);
    m_panel_rect_playbar_max = ImVec2(win_x + TOTAL_W, win_y + TOTAL_H);

    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 base = ImGui::GetWindowPos();

    // Section1/section2/section3 share the same vertical center line.
    // Use top_half so no element clips above the window.
    const float main_cy = top_half;

    float cursor_x = 0.0f;

    // ====== Play / pause icon button ======
    {
        const ImVec2 b0(base.x + cursor_x, base.y + main_cy - PLAY_BTN_SZ * 0.5f);
        const ImVec2 b1(b0.x + PLAY_BTN_SZ, b0.y + PLAY_BTN_SZ);

        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton("##play_pause", ImVec2(PLAY_BTN_SZ, PLAY_BTN_SZ));
        const bool hovered = ImGui::IsItemHovered();
        const bool clicked = ImGui::IsItemClicked(0);
        if (hovered)
            dl->AddRectFilled(b0, b1, IM_COL32(0, 0, 0, 50), 6.0f * sc);

        ImTextureID tex = m_keyframe_playing ? m_tree_icon_pause : (m_is_dark ? m_tree_icon_play_dark : m_tree_icon_play);
        if (tex) {
            const ImVec2 i0(b0.x + (PLAY_BTN_SZ - PLAY_ICON_SZ) * 0.5f,
                            b0.y + (PLAY_BTN_SZ - PLAY_ICON_SZ) * 0.5f);
            dl->AddImage(tex, i0, ImVec2(i0.x + PLAY_ICON_SZ, i0.y + PLAY_ICON_SZ));
        }
        if (clicked) {
            if (m_keyframe_playing) pause_playback();
            else if (m_playback_paused) resume_playback();
            else {
                // Fresh play: start from the frame the progress bar is currently
                if (cur_global <= 1 || cur_global >= total_frames)
                    start_playback_with_intro();
                else
                    play_global_frame(true);
            }
        }
        if (hovered) {
            // unified message for both play and pause states). Window has zero
            // WindowPadding, so the tooltip needs its own padding back.
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * sc, 6.0f * sc));
            m_imgui->tooltip(_u8L("Play all frames for all steps."), 20.0f * m_imgui->scaled(1.0f));
            ImGui::PopStyleVar();
        }
        cursor_x += PLAY_BTN_SZ + GAP_SECTION1;
    }

    // ====== Speed pill ("1.0x", rounded-100 dark mask) ======
    {
        const ImVec2 b0(base.x + cursor_x, base.y + main_cy - SPEED_BADGE_H * 0.5f);
        const ImVec2 b1(b0.x + SPEED_BADGE_W, b0.y + SPEED_BADGE_H);

        ImGui::SetCursorScreenPos(b0);
        m_imgui->disabled_begin(disable_play_controls);
        ImGui::InvisibleButton("##speed_pill", ImVec2(SPEED_BADGE_W, SPEED_BADGE_H));
        const bool hovered = ImGui::IsItemHovered();
        const bool clicked = ImGui::IsItemClicked(0);

        const float r = SPEED_BADGE_H * 0.5f;
        const ImU32 bg = hovered ? IM_COL32(0, 0, 0, 180) : IM_COL32(0, 0, 0, 128);
        dl->AddRectFilled(b0, b1, bg, r);

        if (font) {
            const ImVec2 tp(b0.x + (SPEED_BADGE_W - speed_text_sz.x) * 0.5f,
                            b0.y + (SPEED_BADGE_H - speed_text_sz.y) * 0.5f);
            dl->AddText(font, SPEED_FONT_PX, tp, IM_COL32(255, 255, 255, 255), speed_text);
        }
        if (clicked) {
            // Cycle order matches the Figma "1.0x" default first: 1.0 -> 0.5 -> 1.5 -> 2.0.
            const double cur        = (m_play_transition_duration > 0.0 ? m_play_transition_expect_duration / m_play_transition_duration : 1.0);
            double next_speed = 1.0;
            if      (std::abs(cur - 1.0) < 1e-3) next_speed = 0.5;
            else if (std::abs(cur - 0.5) < 1e-3) next_speed = 1.5;
            else if (std::abs(cur - 1.5) < 1e-3) next_speed = 2.0;
            else                                  next_speed = 1.0;
            m_play_transition_duration = m_play_transition_expect_duration / next_speed;
            m_play_interval_step_to_step = m_play_interval_step_to_step_expect / next_speed;
        }
        if (hovered) {
            // Window has zero WindowPadding; the tooltip needs its own padding back.
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * sc, 6.0f * sc));
            m_imgui->tooltip(_u8L("Toggle playback speed."), 20.0f * m_imgui->scaled(1.0f));
            ImGui::PopStyleVar();
        }
        m_imgui->disabled_end();
        cursor_x += SPEED_BADGE_W + GAP_S1_TO_BAR;
    }

    // ====== Step-segmented progress bar ======
    const float progress_x0 = base.x + cursor_x;
    const float progress_x1 = progress_x0 + PROGRESS_W;
    const float bar_cy      = base.y + main_cy;
    const float bar_y0      = bar_cy - BAR_H * 0.5f;
    const float bar_y1      = bar_cy + BAR_H * 0.5f;
    const ImU32 bar_bg_col  = m_is_dark ? IM_COL32(0x7A, 0x7A, 0x7A, 255) : IM_COL32(0xCE, 0xCE, 0xCE, 255);
    const ImU32 tick_col    = m_is_dark ? IM_COL32(0xD0, 0xD0, 0xD0, 255) : IM_COL32(0x9C, 0x9C, 0x9C, 255);
    const ImU32 label_col   = m_is_dark ? IM_COL32(0xE6, 0xE6, 0xE6, 255) : IM_COL32(0x6B, 0x6B, 0x6B, 255);

    dl->AddRectFilled(ImVec2(progress_x0, bar_y0), ImVec2(progress_x1, bar_y1),
                      bar_bg_col, BAR_H * 0.5f);

    auto frame_t = [&](int frame_1based) -> float {
        if (total_frames <= 1) return (frame_1based >= 1) ? 1.0f : 0.0f;
        return static_cast<float>(frame_1based - 1) / static_cast<float>(total_frames - 1);
    };

    const float progress_frac = frame_t(cur_global);
    const float fill_x1 = progress_x0 + PROGRESS_W * progress_frac;
    if (fill_x1 > progress_x0 + 0.5f) {
        dl->AddRectFilled(ImVec2(progress_x0, bar_y0), ImVec2(fill_x1, bar_y1),
                          IM_COL32(0x2C, 0xAD, 0x00, 255), BAR_H * 0.5f);
    }

    bool  show_seek_drag_preview = false;
    float seek_drag_preview_x    = progress_x0;

    // Click-to-seek over the bar (hit area expanded to circle height so clicks near
    // the marker still register).
    {
        const float hit_y0 = bar_cy - CIRCLE_D * 0.5f;
        ImGui::SetCursorScreenPos(ImVec2(progress_x0, hit_y0));
        m_imgui->disabled_begin(disable_play_controls);
        ImGui::InvisibleButton("##progress_seek", ImVec2(PROGRESS_W, CIRCLE_D));
        const bool progress_hovered = ImGui::IsItemHovered();
        const bool left_down_on_bar = progress_hovered && ImGui::IsMouseClicked(0);
        const bool left_up_on_bar   = progress_hovered && ImGui::IsMouseReleased(0);
        const bool dragging_progress = ImGui::IsItemActive() && ImGui::IsMouseDown(0);
        if (left_down_on_bar || dragging_progress) {
            show_seek_drag_preview = true;
            seek_drag_preview_x = std::clamp(ImGui::GetIO().MousePos.x, progress_x0, progress_x1);
        }
        // Dragging only previews the target. Commit the seek when the mouse is
        // released from this progress bar, or when release happens over the bar.
        if (ImGui::IsItemDeactivated() || left_up_on_bar)
            seek_global_frame_from_mouse_x(ImGui::GetIO().MousePos.x, progress_x0, PROGRESS_W, total_frames);
        m_imgui->disabled_end();
    }

    // Tick marks at every frame boundary except the current frame (which the circle
    // marker replaces). Each tick is a short vertical line straddling the bar.
    for (int i = 1; i <= total_frames; ++i) {
        if (i == cur_global) continue;
        if (i == 1 || i == total_frames) continue; // skip start/end (sit at bar caps)
        const float tx = progress_x0 + PROGRESS_W * frame_t(i);
        dl->AddLine(ImVec2(tx, bar_cy - TICK_HALF), ImVec2(tx, bar_cy + TICK_HALF),
                    tick_col, TICK_W);
    }

    // Current step circular marker + step number inside it + label below.
    {
        const float cx = progress_x0 + PROGRESS_W * progress_frac;
        const float cy = bar_cy;
        const float rd = CIRCLE_D * 0.5f;
        const std::string current_frame_tip =
            _u8L("Frame") + " " + std::to_string(cur_global) + " (" + cur_frame_kind + ")\n" + cur_step_name;
        auto show_current_frame_tooltip = [&]() {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * sc, 6.0f * sc));
            m_imgui->tooltip(current_frame_tip, 20.0f * m_imgui->scaled(1.0f));
            ImGui::PopStyleVar();
        };

        dl->AddCircleFilled(ImVec2(cx, cy), rd, IM_COL32(255, 255, 255, 255), 32);
        ImGui::SetCursorScreenPos(ImVec2(cx - rd, cy - rd));
        ImGui::InvisibleButton("##progress_current_frame_tip", ImVec2(CIRCLE_D, CIRCLE_D));
        if (ImGui::IsItemHovered())
            show_current_frame_tooltip();

        char step_text[16];
        std::snprintf(step_text, sizeof(step_text), "%d", cur_global);
        if (font) {
            const ImVec2 ts = font->CalcTextSizeA(LABEL_FONT_PX, FLT_MAX, 0.0f, step_text);
            dl->AddText(font, LABEL_FONT_PX,
                        ImVec2(cx - ts.x * 0.5f, cy - ts.y * 0.5f),
                        IM_COL32(0x32, 0x3A, 0x3D, 255), step_text);
        }

        if (font && !cur_label.empty()) {
            // Label sits below the circle. 33.74 (was Figma's 27.74) drops it a bit
            // lower to widen the gap; keep in sync with the TOTAL_H label term above.
            const float label_top = base.y + 33.74f * sc;
            const ImVec2 ls = font->CalcTextSizeA(LABEL_FONT_PX, FLT_MAX, 0.0f, cur_label.c_str());
            const ImVec2 label_pos(cx - ls.x * 0.5f, label_top);
            dl->AddText(font, LABEL_FONT_PX,
                        label_pos,
                        label_col, cur_label.c_str());
            ImGui::SetCursorScreenPos(label_pos);
            ImGui::InvisibleButton("##progress_current_frame_label_tip", ImVec2(ls.x, ls.y));
            if (ImGui::IsItemHovered())
                show_current_frame_tooltip();
        }
    }

    if (show_seek_drag_preview) {
        const ImVec2 preview_c(seek_drag_preview_x, bar_cy);
        const float preview_r = CIRCLE_D * 0.5f;
        constexpr float two_pi = 6.2831853071795864769f;
        constexpr int segments = 32;
        for (int i = 0; i < segments; i += 2) {
            const float a0 = two_pi * float(i) / float(segments);
            const float a1 = two_pi * float(i + 1) / float(segments);
            dl->AddLine(ImVec2(preview_c.x + std::cos(a0) * preview_r, preview_c.y + std::sin(a0) * preview_r),
                        ImVec2(preview_c.x + std::cos(a1) * preview_r, preview_c.y + std::sin(a1) * preview_r),
                        IM_COL32(0x2C, 0xAD, 0x00, 230), 2.0f * sc);
        }
    }

    cursor_x += PROGRESS_W + GAP_BAR_TO_NAV;

    // ====== Prev / Next nav buttons (24x24, dark rounded mask) ======
    // Icons come from resources/images/play_left.svg and play_right.svg
    // (loaded once in init_tree_icons()).
    auto draw_nav = [&](const char *id, ImTextureID tex, bool enabled, float x_local,
                        const std::string &tip) -> bool {
        const ImVec2 p0(base.x + x_local, base.y + main_cy - NAV_BTN_SZ * 0.5f);
        const ImVec2 p1(p0.x + NAV_BTN_SZ, p0.y + NAV_BTN_SZ);
        ImGui::SetCursorScreenPos(p0);
        ImGui::InvisibleButton(id, ImVec2(NAV_BTN_SZ, NAV_BTN_SZ));
        const bool hovered_raw = ImGui::IsItemHovered();
        const bool hovered = enabled && hovered_raw;
        const bool clicked = enabled && ImGui::IsItemClicked(0);
        const ImU32 bg = enabled
            ? (hovered ? IM_COL32(0, 0, 0, 180) : IM_COL32(0, 0, 0, 128))
            : IM_COL32(0, 0, 0, 60);
        dl->AddRectFilled(p0, p1, bg, NAV_BTN_ROUND);
        if (tex) {
            const ImVec2 i0(p0.x + (NAV_BTN_SZ - NAV_ICON_SZ) * 0.5f,
                            p0.y + (NAV_BTN_SZ - NAV_ICON_SZ) * 0.5f);
            const ImU32 tint = enabled ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 128);
            dl->AddImage(tex, i0, ImVec2(i0.x + NAV_ICON_SZ, i0.y + NAV_ICON_SZ),
                         ImVec2(0, 0), ImVec2(1, 1), tint);
        }
        // Tooltip is shown even when the button is disabled so the user knows what
        // each side .but only when the cursor is over the button itself.
        if (hovered_raw && !tip.empty()) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * sc, 6.0f * sc));
            m_imgui->tooltip(tip, 20.0f * m_imgui->scaled(1.0f));
            ImGui::PopStyleVar();
        }
        return clicked;
    };

    {
        m_imgui->disabled_begin(disable_play_controls);
        const bool can_prev = cur_global > 1;
        if (draw_nav("##nav_prev", m_play_left_icon, can_prev, cursor_x,
                     _u8L("Play previous frame."))) {
            goto_global_frame(cur_global - 1);
        }
        cursor_x += NAV_BTN_SZ + NAV_GAP;

        const bool can_next = cur_global < total_frames;
        if (draw_nav("##nav_next", m_play_right_icon, can_next, cursor_x,
                     _u8L("Play next frame."))) {
            goto_global_frame(cur_global + 1);
        }
        m_imgui->disabled_end();
    }

    m_imgui->end();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
}

void AssemblyStepsUtils::draw_arrow_lines(
    const std::vector<std::pair<ImVec2, ImVec2>> &arrows,
    const std::array<float, 4> &color,
    float thickness,
    const std::array<int, 4> &viewport,
    bool draw_arrowhead)
{
    if (arrows.empty())
        return;

    // Default arrowhead size in screen pixels (tentative; tune if needed).
    static const float TRI_BASE = 12.0f;
    static const float TRI_HEIGHT = TRI_BASE * 1.618033f;
    const int n = (int)arrows.size();

    {
        m_arrow_line_model.reset();
        GLModel::Geometry line_data;
        line_data.format = {GLModel::PrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3};
        line_data.color = ColorRGBA::WHITE();
        line_data.reserve_vertices(2 * n);
        line_data.reserve_indices(2 * n);
        for (int i = 0; i < n; ++i) {
            line_data.add_vertex(Vec3f(arrows[i].first.x, arrows[i].first.y, 0.0f));
            line_data.add_vertex(Vec3f(arrows[i].second.x, arrows[i].second.y, 0.0f));
            line_data.add_line((unsigned)(i * 2), (unsigned)(i * 2 + 1));
        }
        m_arrow_line_model.init_from(std::move(line_data));
    }

    if (draw_arrowhead) {
        m_arrow_tri_model.reset();
        GLModel::Geometry tri_data;
        tri_data.format = {GLModel::PrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3};
        tri_data.color = ColorRGBA::WHITE();
        tri_data.reserve_vertices(3 * n);
        tri_data.reserve_indices(3 * n);
        for (int i = 0; i < n; ++i) {
            Vec2d dir(arrows[i].second.x - arrows[i].first.x, arrows[i].second.y - arrows[i].first.y);
            double len = dir.norm();
            if (len < 1e-3)
                dir = Vec2d(1, 0);
            else
                dir /= len;
            Vec2d perp(-dir.y(), dir.x());

            Vec2d tip(arrows[i].first.x, arrows[i].first.y);
            Vec2d p1 = tip + dir * TRI_HEIGHT - perp * (0.5 * TRI_BASE);
            Vec2d p2 = tip + dir * TRI_HEIGHT + perp * (0.5 * TRI_BASE);

            tri_data.add_vertex(Vec3f((float)tip.x(), (float)tip.y(), 0.0f));
            tri_data.add_vertex(Vec3f((float)p1.x(), (float)p1.y(), 0.0f));
            tri_data.add_vertex(Vec3f((float)p2.x(), (float)p2.y(), 0.0f));
            tri_data.add_triangle((unsigned)(i * 3), (unsigned)(i * 3 + 1), (unsigned)(i * 3 + 2));
        }
        m_arrow_tri_model.init_from(std::move(tri_data), true);
    }

    auto shader = wxGetApp().get_shader("flat");
    if (shader == nullptr)
        return;

    wxGetApp().bind_shader(shader);

    const Transform3d ss_to_ndc = TransformHelper::ndc_to_ss_matrix_inverse(viewport);
    shader->set_uniform("projection_matrix", Transform3d::Identity());
    shader->set_uniform("view_model_matrix", ss_to_ndc);

    glsafe(::glDisable(GL_DEPTH_TEST));

#if ENABLE_GL_CORE_PROFILE
    if (OpenGLManager::get_gl_info().is_core_profile()) {
        wxGetApp().unbind_shader(shader);
        shader = wxGetApp().get_shader("dashed_thick_lines");
        if (shader == nullptr)
            return;
        wxGetApp().bind_shader(shader);
        shader->set_uniform("projection_matrix", Transform3d::Identity());
        shader->set_uniform("view_model_matrix", ss_to_ndc);
        shader->set_uniform("viewport_size", Vec2d(double(viewport[2]), double(viewport[3])));
        shader->set_uniform("width", thickness);
        shader->set_uniform("gap_size", 0.0f);
    }
    else
#endif
    {
        const auto &ogl_manager = wxGetApp().get_opengl_manager();
        if (ogl_manager)
            ogl_manager->set_line_width(thickness);
    }

    m_arrow_line_model.set_color(-1, color);
    m_arrow_line_model.render_geometry();

#if ENABLE_GL_CORE_PROFILE
    if (OpenGLManager::get_gl_info().is_core_profile()) {
        wxGetApp().unbind_shader(shader);
        shader = wxGetApp().get_shader("flat");
        if (shader == nullptr)
            return;
        wxGetApp().bind_shader(shader);
        shader->set_uniform("projection_matrix", Transform3d::Identity());
        shader->set_uniform("view_model_matrix", ss_to_ndc);
    }
    else
#endif
    {
        const auto &ogl_manager = wxGetApp().get_opengl_manager();
        if (ogl_manager)
            ogl_manager->set_line_width(1.0f);
    }

    if (draw_arrowhead) {
        m_arrow_tri_model.set_color(-1, color);
        m_arrow_tri_model.render_geometry();
    }

    glsafe(::glEnable(GL_DEPTH_TEST));
    wxGetApp().unbind_shader();
}

void AssemblyStepsUtils::draw_arrow_svg_icon(int idx, const ImVec2 &center, const ImVec2 &box_sz, ImTextureID tex, bool selected) const
{
    float half_w = box_sz.x * 0.5f;
    float half_h = box_sz.y * 0.5f;
    float icon_pad = std::max(4.0f, std::min(box_sz.x, box_sz.y) * 0.12f);
    float icon_sz = std::max(1.0f, std::min(box_sz.x, box_sz.y) - icon_pad * 2.0f);

    const ImVec2 box_min(center.x - half_w, center.y - half_h);
    const ImVec2 box_max(center.x + half_w, center.y + half_h);

    // Draw the icon body on the background draw list (same approach as the
    const float rounding = 6.0f;
    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    dl->AddRectFilled(box_min, box_max, IM_COL32(255, 255, 255, 255), rounding);
    dl->AddRect(box_min, box_max,
                selected ? IM_COL32(25, 166, 77, 242) : IM_COL32(178, 178, 178, 255),
                rounding, 0, 1.0f);
    if (tex) {
        const ImVec2 img_min(center.x - icon_sz * 0.5f, center.y - icon_sz * 0.5f);
        const ImVec2 img_max(center.x + icon_sz * 0.5f, center.y + icon_sz * 0.5f);
        dl->AddImage(tex, img_min, img_max);
    }
}

void AssemblyStepsUtils::render_part_number_labels_on_canvas(
    const std::array<int, 4> &viewport,
    float viewport_height)
{
    if (!m_guide_show_part_numbers || !m_camera || !m_volumes || !m_model)
        return;
    auto *entries = get_current_kf_entries();
    if (!entries || m_keyframe_selected < 0 || m_keyframe_selected >= (int)entries->size())
        return;

    KeyFrameEntry &cur_entry = (*entries)[m_keyframe_selected];
    AssemblyNote &note = m_render_interpolated_part_number_labels ?
        m_interpolated_part_number_label_frame.assembly_note :
        cur_entry.data.assembly_note;
    auto &labels = note.part_number_labels;
    const bool editable = !m_render_interpolated_part_number_labels;
    if (!note.show_part_labels)
        return;
    if (labels.empty())
        return;

    update_part_number_label_font_size_from_config();

    const float sc      = m_imgui_scale;
    const float font_sz = part_number_label_font_size() * sc;
    const float pad_h   = 16.0f * sc;
    const float pad_v   =  6.0f * sc;
    const float rounding = 100.0f * sc;
    const ImU32 bg_col   = IM_COL32(0, 0, 0, 128);
    const ImU32 txt_col  = IM_COL32(255, 255, 255, 204);

    ImDrawList *fg   = ImGui::GetBackgroundDrawList();
    ImFont     *font = ImGui::GetFont();

    const bool block_label_interaction = is_mouse_over_blocking_panel();
    ImGuiWindowFlags drag_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
    if (block_label_interaction)
        drag_flags |= ImGuiWindowFlags_NoInputs;

    bool any_changed = false;

    const float handle_sz = 8.0f * sc;

    for (int i = 0; i < (int)labels.size(); ++i) {
        PartNumberLabel &lbl = labels[i];

        Vec2d center = pn_screen_centers_.count(i) ? pn_screen_centers_[i]
                     : Vec2d(viewport[2] * 0.5, viewport[3] * 0.5);
        Vec2d arrow_start = center + lbl.arrow_start_offset;
        Vec2d arrow_end   = arrow_start + lbl.arrow_end_offset;
        // center/arrow_start/arrow_end are already in ImGui screen coords
        // (Y-down), so use directly -- no vp_height flip needed here.
        ImVec2 start_screen((float)arrow_start.x(), (float)arrow_start.y());
        ImVec2 label_screen((float)arrow_end.x(),   (float)arrow_end.y());

        ImVec2 text_sz = font->CalcTextSizeA(font_sz, FLT_MAX, 0.0f, lbl.part_name.c_str());
        float pill_w = text_sz.x + pad_h * 2.0f;
        float pill_h = text_sz.y + pad_v * 2.0f;
        ImVec2 pill_min(label_screen.x - pill_w * 0.5f, label_screen.y - pill_h * 0.5f);
        ImVec2 pill_max(pill_min.x + pill_w, pill_min.y + pill_h);

        fg->AddRectFilled(pill_min, pill_max, bg_col, rounding);

        ImVec2 text_pos(label_screen.x - text_sz.x * 0.5f, label_screen.y - text_sz.y * 0.5f);
        fg->AddText(font, font_sz, text_pos, txt_col, lbl.part_name.c_str());

        if (editable) {
            // Draggable invisible window over the pill label
            char drag_id[64];
            snprintf(drag_id, sizeof(drag_id), "##pn_drag_%d", i);
            ImGui::SetNextWindowPos(pill_min, ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(pill_w, pill_h));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
            if (ImGui::Begin(drag_id, nullptr, drag_flags)) {
                ImGui::InvisibleButton("##hit", ImVec2(pill_w, pill_h));
                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                    ImVec2 delta = ImGui::GetIO().MouseDelta;
                    lbl.arrow_end_offset.x() += delta.x;
                    lbl.arrow_end_offset.y() += delta.y;
                    any_changed = true;
                }
                if (ImGui::IsItemHovered())
                    set_cursor(AssemblyNoteCursorType::Move);
            }
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar();
        }

        // Draggable start handle near the volume. Following the PlainArrowNote
        // convention, dragging the start moves arrow_start_offset by delta and
        // absorbs the same delta into arrow_end_offset (with a sign flip), so
        // the pill stays anchored where the user placed it while the line tail
        // travels with the pointer:
        //     pill_pos = center + start_offset + end_offset
        //              = center + (start_offset + delta) + (end_offset - delta)
        //              = center + start_offset + end_offset       (unchanged).
        if (editable) {
            char start_drag_id[64];
            snprintf(start_drag_id, sizeof(start_drag_id), "##pn_start_drag_%d", i);
            const float start_drag_sz = handle_sz * 2.0f;
            ImGui::SetNextWindowPos(ImVec2(start_screen.x - handle_sz, start_screen.y - handle_sz), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(start_drag_sz, start_drag_sz));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
            if (ImGui::Begin(start_drag_id, nullptr, drag_flags)) {
                ImGui::InvisibleButton("##pn_start_hit", ImVec2(start_drag_sz, start_drag_sz));
                if (ImGui::IsItemHovered() || ImGui::IsItemActive())
                    set_cursor(AssemblyNoteCursorType::Move);
                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                    ImVec2 delta = ImGui::GetIO().MouseDelta;
                    lbl.arrow_start_offset.x() += delta.x;
                    lbl.arrow_start_offset.y() += delta.y;
                    lbl.arrow_end_offset.x()   -= delta.x;
                    lbl.arrow_end_offset.y()   -= delta.y;
                    any_changed = true;
                }
            }
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
        }

        // Visible affordance: small green dot at the start so users discover
        // the handle. Drawn on the foreground list so it floats above the
        // line stroke that render_assembly_notes_on_canvas batched earlier.
        fg->AddCircleFilled(start_screen, handle_sz * 0.6f, IM_COL32(0, 200, 80, 200));
    }

    if (editable && any_changed) {
        cur_entry.need_save = true;
        save_assembly_steps_json_to_model();
        do_commond_callback("dirty");
        do_commond_callback("request_extra_frame");
    }
}

ImVec2 AssemblyStepsUtils::nearest_rect_anchor(const ImVec2 &rect_min, const ImVec2 &rect_max,
                                               const ImVec2 &from, bool include_corners)
{
    float cx = (rect_min.x + rect_max.x) * 0.5f;
    float cy = (rect_min.y + rect_max.y) * 0.5f;
    ImVec2 mid_points[4] = {
        {cx, rect_min.y}, {cx, rect_max.y},
        {rect_min.x, cy}, {rect_max.x, cy},
    };
    ImVec2 corners[4] = {
        {rect_min.x, rect_min.y}, {rect_max.x, rect_min.y},
        {rect_min.x, rect_max.y}, {rect_max.x, rect_max.y},
    };
    ImVec2 best = mid_points[0];
    float  best_d2 = FLT_MAX;
    auto try_point = [&](const ImVec2 &p) {
        float dx = p.x - from.x, dy = p.y - from.y;
        float d2 = dx * dx + dy * dy;
        if (d2 < best_d2) { best_d2 = d2; best = p; }
    };
    for (const auto &p : mid_points)
        try_point(p);
    if (include_corners) {
        for (const auto &p : corners)
            try_point(p);
    }
    return best;
}

void AssemblyStepsUtils::render_assembly_notes_on_canvas(const Vec2d &object_screen_center)
{
    if (!m_camera || is_show_video_title_mode()) {
        return;
    }
    const Camera             &camera   = *m_camera;
    const std::array<int, 4> &viewport = camera.get_viewport();
    auto                      viewport_height = (float) viewport[3];
    // Detect a step-tree / keyframe switch since the previous frame and clear

    if (m_selected_node != m_last_rendered_selected_node_for_notes_ ||
        m_keyframe_selected != m_last_rendered_keyframe_selected_) {
        exit_note_edit();
        m_last_rendered_selected_node_for_notes_ = m_selected_node;
        m_last_rendered_keyframe_selected_       = m_keyframe_selected;
    }
    if (!has_selected_node())
        return;
    auto *entries = get_current_kf_entries();
    if (!entries || m_keyframe_selected < 0 || m_keyframe_selected >= (int)entries->size())
        return;

    KeyFrameEntry &cur_entry = (*entries)[m_keyframe_selected];
    AssemblyNote &note = cur_entry.data.assembly_note;
    auto &arrow_svgs = note.arrow_svgs;
    auto &text_labels = note.text_labels;
    auto &circle_notes = note.circle_notes;
    auto &rectangle_notes = note.rectangle_notes;
    auto &plain_arrows = note.plain_arrows;
    AssemblyNote &part_number_note = m_render_interpolated_part_number_labels ?
        m_interpolated_part_number_label_frame.assembly_note :
        note;
    auto &pn_labels = part_number_note.part_number_labels;

    const bool has_pn = m_guide_show_part_numbers && part_number_note.show_part_labels && !pn_labels.empty() && m_volumes && m_model;
    if (arrow_svgs.empty() && text_labels.empty() && circle_notes.empty() && rectangle_notes.empty() && plain_arrows.empty() && !has_pn)
        return;
    auto   &sc           = m_imgui_scale;
    const float handle_sz    = 8.0f * sc;
    const float close_r      = 8.0f * sc;
    const float resize_sz    = 12.0f * sc;
    const float line_hit_tol = 6.0f * sc;
    const float line_thick   = 2.0f;
    const std::array<float, 4> line_col = {0.0f, 0.78f, 0.31f, 0.86f};
    Vec2d obj_center = object_screen_center;
    const float vp_height = viewport_height;

    const int count = (int)arrow_svgs.size();
    const int plain_arrow_count = (int)plain_arrows.size();
    if (is_note_edit_controls_visible()) {
        bool selection_valid =
            (m_note_selected_type == AssemblyNoteSelectionType::ArrowSvg && m_note_selected_idx >= 0 && m_note_selected_idx < count) ||
            (m_note_selected_type == AssemblyNoteSelectionType::TextLabel && m_note_selected_idx >= 0 && m_note_selected_idx < (int)text_labels.size()) ||
            (m_note_selected_type == AssemblyNoteSelectionType::Circle && m_note_selected_idx >= 0 && m_note_selected_idx < (int)circle_notes.size()) ||
            (m_note_selected_type == AssemblyNoteSelectionType::Rectangle && m_note_selected_idx >= 0 && m_note_selected_idx < (int)rectangle_notes.size()) ||
            (m_note_selected_type == AssemblyNoteSelectionType::PlainArrow && m_note_selected_idx >= 0 && m_note_selected_idx < plain_arrow_count);
        if (!selection_valid) {
            exit_note_edit();
        }
    }
    m_guide_note_tool_selected = is_note_edit_controls_visible()
        ? note_tool_index_from_selection(m_note_selected_type)
        : -1;

    // --- Pass 1: collect screen positions & batch-draw all arrow lines ---
    struct ArrowScreenData { ImVec2 start; ImVec2 end; };
    struct ColoredLine { std::pair<ImVec2, ImVec2> line; std::array<float, 4> color; bool draw_arrowhead{false}; };
    std::vector<ArrowScreenData> screen_data(count);
    std::vector<ColoredLine> line_pairs;
    line_pairs.reserve(count + plain_arrow_count + (has_pn ? (int)pn_labels.size() : 0));

    for (int ni = 0; ni < count; ++ni) {
        ArrowSvgNote &arrow = arrow_svgs[ni];
        // Mirror the Pass-2 clamp so the line-anchor math agrees with the icon
        const double svg_min_dim = 32.0 * sc;
        arrow.label_size.x() = std::max(arrow.label_size.x(), svg_min_dim);
        arrow.label_size.y() = std::max(arrow.label_size.y(), svg_min_dim);
        // Anchor to the bound volumes' on-screen bbox center (falls back to the.
        const Vec2d arrow_center = compute_arrow_svg_anchor_center(arrow, obj_center);
        Vec2d start_pos = arrow_center + arrow.arrow_start_offset;
        Vec2d end_pos   = start_pos  + arrow.arrow_end_offset;
        screen_data[ni].start = ImVec2((float)start_pos.x(), (float)start_pos.y());
        screen_data[ni].end   = ImVec2((float)end_pos.x(),   (float)end_pos.y());
        // Anchor the line tip to the SVG icon's bbox edge (same trick used by
        ImVec2 icon_min((float)(end_pos.x() - arrow.label_size.x() * 0.5),
                        (float)(end_pos.y() - arrow.label_size.y() * 0.5));
        ImVec2 icon_max((float)(end_pos.x() + arrow.label_size.x() * 0.5),
                        (float)(end_pos.y() + arrow.label_size.y() * 0.5));
        ImVec2 from_pt((float)start_pos.x(), (float)start_pos.y());
        ImVec2 anchor  = nearest_rect_anchor(icon_min, icon_max, from_pt,true);
        line_pairs.push_back({{
            ImVec2((float)start_pos.x(), vp_height - (float)start_pos.y()),
            ImVec2(anchor.x,             vp_height - anchor.y)},
            note_color_to_float_array(arrow.color)});
    }
    std::vector<ArrowScreenData> plain_arrow_screen_data(plain_arrow_count);
    for (int ni = 0; ni < plain_arrow_count; ++ni) {
        const PlainArrowNote &arrow = plain_arrows[ni];
        // Anchor to the bound volumes' on-screen bbox center (falls back to step center).
        const Vec2d arrow_center = compute_note_anchor_center(arrow.bound_volumes, obj_center);
        Vec2d start_pos = arrow_center + arrow.arrow_start_offset;
        Vec2d end_pos   = start_pos  + arrow.arrow_end_offset;
        plain_arrow_screen_data[ni].start = ImVec2((float)start_pos.x(), (float)start_pos.y());
        plain_arrow_screen_data[ni].end   = ImVec2((float)end_pos.x(),   (float)end_pos.y());
        line_pairs.push_back({{
            ImVec2((float)start_pos.x(), vp_height - (float)start_pos.y()),
            ImVec2((float)end_pos.x(),   vp_height - (float)end_pos.y())},
            note_color_to_float_array(arrow.color), true});
    }

    // Part-number label lines: compute per-object screen centers and collect lines.
    pn_screen_centers_.clear();
    if (has_pn) {
        Matrix4d w2s = camera.get_projection_matrix().matrix() * camera.get_view_matrix().matrix();
        // vol_idx >= 0 -> anchor at that specific GLVolume; vol_idx < 0 (used
        // by final-assembly object-level labels) -> aggregate all active
        // volumes belonging to the ModelObject and use the union bbox center.
        auto pn_vol_center = [&](int obj_idx, int vol_idx) -> Vec2d {
            BoundingBoxf3 bb;
            bool has_any = false;
            for (const GLVolume *vol : m_volumes->volumes) {
                if (!vol || !vol->is_active)
                    continue;
                if (vol->composite_id.object_id != obj_idx)
                    continue;
                if (vol_idx >= 0 && vol->composite_id.volume_id != vol_idx)
                    continue;
                bb.merge(vol->transformed_bounding_box());
                has_any = true;
                if (vol_idx >= 0)
                    break;
            }
            if (!has_any)
                return Vec2d(viewport[2] * 0.5, viewport[3] * 0.5);
            Vec4d c4(bb.center().x(), bb.center().y(), bb.center().z(), 1.0);
            Vec4d ndc = w2s * c4;
            return Vec2d(0.5 * (1.0 + ndc.x() / ndc.w()) * viewport[2],
                         0.5 * (1.0 - ndc.y() / ndc.w()) * viewport[3]);
        };

        update_part_number_label_font_size_from_config();

        const float pn_font_sz = part_number_label_font_size() * sc;
        const float pn_pad_h  = 16.0f * sc;
        const float pn_pad_v  =  6.0f * sc;
        ImFont *pn_font = ImGui::GetFont();

        // One-shot auto-arrange: bucket every label onto one of the four sides of
        // the step's on-screen bbox (top/bottom/left/right), then stack the labels
        // of each side along its rail so the pills never overlap. Leader lines
        // connect each part back to its pill. Only the editable (non-interpolated)
        // frame is rewritten and persisted.
        if (m_pn_autolayout_pending && !m_render_interpolated_part_number_labels) {
            if (auto_layout_part_number_labels(pn_labels, camera, viewport, sc)) {
                record_camera(cur_entry.data);
                cur_entry.data.is_camera_define = true;
                cur_entry.need_save = true;
                save_assembly_steps_json_to_model();
            }
            m_pn_autolayout_pending = false;
        }

        for (int i = 0; i < (int)pn_labels.size(); ++i) {
            const PartNumberLabel &lbl = pn_labels[i];
            Vec2d center = pn_vol_center(lbl.object_idx, lbl.volume_idx);
            pn_screen_centers_[i] = center;
            Vec2d arrow_start = center + lbl.arrow_start_offset;
            Vec2d pill_center = arrow_start + lbl.arrow_end_offset;

            ImVec2 tsz = pn_font->CalcTextSizeA(pn_font_sz, FLT_MAX, 0.0f, lbl.part_name.c_str());
            float pw = tsz.x + pn_pad_h * 2.0f;
            float ph = tsz.y + pn_pad_v * 2.0f;
            ImVec2 pill_min((float)pill_center.x() - pw * 0.5f, (float)pill_center.y() - ph * 0.5f);
            ImVec2 pill_max(pill_min.x + pw, pill_min.y + ph);

            ImVec2 from_pt((float)arrow_start.x(), (float)arrow_start.y());
            ImVec2 anchor = nearest_rect_anchor(pill_min, pill_max, from_pt);

            line_pairs.push_back({{
                ImVec2((float)arrow_start.x(), vp_height - (float)arrow_start.y()),
                ImVec2(anchor.x,               vp_height - anchor.y)},
                line_col});
        }
    }

    for (const ColoredLine &colored_line : line_pairs) {
        std::vector<std::pair<ImVec2, ImVec2>> one_line{colored_line.line};
        draw_arrow_lines(one_line, colored_line.color, line_thick, viewport, colored_line.draw_arrowhead);
    }

    // --- Pass 2: ImGui svg icons, drag handles, close buttons ---
    bool any_changed = false;
    int  delete_idx  = -1;
    bool note_cursor_requested = false;
    static bool s_deferred_note_save_until_mouse_release = false;
    static int s_circle_line_drag_idx = -1;
    static int s_plain_arrow_line_drag_idx = -1;
    if (!ImGui::IsMouseDown(0)) {
        s_circle_line_drag_idx = -1;
        s_plain_arrow_line_drag_idx = -1;
    }
    ImDrawList *draw_list = ImGui::GetBackgroundDrawList();

    const bool block_note_interaction = is_mouse_over_blocking_panel();
    ImGuiWindowFlags drag_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
    // The text label hosts an editable InputText so it must stay a real window,
    ImGuiWindowFlags text_label_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (block_note_interaction) {
        drag_flags |= ImGuiWindowFlags_NoInputs;
        text_label_flags |= ImGuiWindowFlags_NoInputs;
    }

    auto draw_note_close_button = [&](const ImVec2 &center, const char *id, float radius = -1.0f,
                                      ImU32 fill_col = IM_COL32(200, 60, 60, 230),
                                      ImU32 cross_col = IM_COL32(255, 255, 255, 255),
                                      float hit_pad = 0.0f) {
        bool want_delete = false;
        float r = radius > 0.0f ? radius : close_r;
        // Paint on the background draw list: it sits below every ImGui window
        draw_list->AddCircleFilled(center, r, fill_col);
        float cross = r * 0.45f;
        draw_list->AddLine(ImVec2(center.x - cross, center.y - cross),
                           ImVec2(center.x + cross, center.y + cross), cross_col, 2.0f);
        draw_list->AddLine(ImVec2(center.x + cross, center.y - cross),
                           ImVec2(center.x - cross, center.y + cross), cross_col, 2.0f);
        // hit_pad enlarges only the clickable area, not the visible circle. The
        const float hit = r + hit_pad;
        ImGui::SetCursorScreenPos(ImVec2(center.x - hit, center.y - hit));
        if (ImGui::InvisibleButton(id, ImVec2(hit * 2.0f, hit * 2.0f))) {
            m_selection_origin = SelectionOrigin::ImGuiNote;
            want_delete = true;
        }
        if (ImGui::IsItemHovered()) {
            set_cursor(AssemblyNoteCursorType::Hand);
            note_cursor_requested = true;
        }
        return want_delete;
    };

    auto draw_note_resize_handle = [&](const ImVec2 &center, float handle_size = -1.0f,
                                       ImU32 fill_col = IM_COL32(0, 200, 80, 230),
                                       ImU32 border_col = IM_COL32(80, 80, 80, 230)) {
        float sz = handle_size > 0.0f ? handle_size : resize_sz;
        ImVec2 min(center.x - sz * 0.5f, center.y - sz * 0.5f);
        ImVec2 max(min.x + sz, min.y + sz);
        // Paint on the background draw list (below the side panels). Drawn after the note bodies, so the handle still renders above them.
        draw_list->AddRectFilled(min, max, fill_col, 2.0f * sc);
        draw_list->AddRect(min, max, border_col, 2.0f * sc, 0, 1.0f);
        return min;
    };
    auto is_note_selected = [&](AssemblyNoteSelectionType type, int idx) {
        return m_note_edit_controls_visible
            && m_note_selected_type == type
            && m_note_selected_idx == idx;
    };
    auto mark_imgui_note_click = [&]() {
        if (ImGui::IsItemClicked(0) || ImGui::IsItemActivated() || ImGui::IsItemActive())
            set_selection_origin(SelectionOrigin::ImGuiNote);
    };
    auto activate_note_edit_controls = [&](AssemblyNoteSelectionType type, int idx) {
        set_selection_origin(SelectionOrigin::ImGuiNote);
        if (!is_note_selected(type, idx)) {
            set_note_edit_controls_visible(true);
            m_note_selected_type = type;
            m_note_selected_idx = idx;
            m_guide_note_tool_selected = note_tool_index_from_selection(type);
            do_commond_callback("dirty");
        }
    };

    auto clamp_note_size = [](Vec2d &size, double min_w, double min_h) {
        size.x() = std::max(size.x(), min_w);
        size.y() = std::max(size.y(), min_h);
    };
    auto wrap_text_to_width = [](const std::string &text, float max_width) {
        if (max_width <= 1.0f || text.empty())
            return text;

        auto next_utf8 = [](const std::string &s, size_t pos) {
            unsigned char c = static_cast<unsigned char>(s[pos]);
            size_t len = 1;
            if ((c & 0x80) == 0)
                len = 1;
            else if ((c & 0xE0) == 0xC0)
                len = 2;
            else if ((c & 0xF0) == 0xE0)
                len = 3;
            else if ((c & 0xF8) == 0xF0)
                len = 4;
            return std::min(pos + len, s.size());
        };

        std::string wrapped;
        std::string line;
        for (size_t pos = 0; pos < text.size();) {
            if (text[pos] == '\n') {
                wrapped += line;
                wrapped += '\n';
                line.clear();
                ++pos;
                continue;
            }

            size_t next = next_utf8(text, pos);
            std::string token = text.substr(pos, next - pos);
            std::string candidate = line + token;
            if (!line.empty() && ImGui::CalcTextSize(candidate.c_str()).x > max_width) {
                wrapped += line;
                wrapped += '\n';
                line = token;
            } else {
                line = std::move(candidate);
            }
            pos = next;
        }
        wrapped += line;
        return wrapped;
    };
    auto unwrap_soft_newlines = [](const std::string &text, float previous_width) {
        if (previous_width <= 1.0f || text.empty())
            return text;

        std::string unwrapped;
        std::string line;
        for (size_t pos = 0; pos < text.size(); ++pos) {
            if (text[pos] == '\n') {
                unwrapped += line;
                if (ImGui::CalcTextSize(line.c_str()).x < previous_width - 10.0f)
                    unwrapped += '\n';
                line.clear();
            } else {
                line += text[pos];
            }
        }
        unwrapped += line;
        return unwrapped;
    };
    auto text_line_count = [](const std::string &text) {
        int count = 1;
        for (char ch : text)
            if (ch == '\n')
                ++count;
        return count;
    };
    auto distance_to_segment = [](const ImVec2 &point, const ImVec2 &start, const ImVec2 &end) {
        float vx = end.x - start.x;
        float vy = end.y - start.y;
        float wx = point.x - start.x;
        float wy = point.y - start.y;
        float len_sq = vx * vx + vy * vy;
        if (len_sq <= 0.0001f) {
            float dx = point.x - start.x;
            float dy = point.y - start.y;
            return std::sqrt(dx * dx + dy * dy);
        }
        float t = std::max(0.0f, std::min(1.0f, (wx * vx + wy * vy) / len_sq));
        float proj_x = start.x + t * vx;
        float proj_y = start.y + t * vy;
        float dx = point.x - proj_x;
        float dy = point.y - proj_y;
        return std::sqrt(dx * dx + dy * dy);
    };
    auto is_point_on_ellipse_outline = [](const ImVec2 &point, const ImVec2 &center, const ImVec2 &radius, float tolerance) {
        if (radius.x <= 1.0f || radius.y <= 1.0f)
            return false;
        float dx = (point.x - center.x) / radius.x;
        float dy = (point.y - center.y) / radius.y;
        float norm = std::sqrt(dx * dx + dy * dy);
        float normalized_tol = tolerance / std::max(1.0f, std::min(radius.x, radius.y));
        return std::fabs(norm - 1.0f) <= normalized_tol;
    };

    for (int ni = 0; ni < count; ++ni) {
        ArrowSvgNote &arrow = arrow_svgs[ni];
        clamp_note_size(arrow.label_size, 32.0 * sc, 32.0 * sc);
        const ImVec2 &arrow_start = screen_data[ni].start;
        const ImVec2 &arrow_end   = screen_data[ni].end;
        ImVec2 icon_box_sz((float)arrow.label_size.x(), (float)arrow.label_size.y());
        bool arrow_selected = is_note_selected(AssemblyNoteSelectionType::ArrowSvg, ni);

        ImTextureID tex = get_arrow_svg_icon(arrow.svg_name);
        draw_arrow_svg_icon(ni, arrow_end, icon_box_sz, tex, arrow_selected);

        // Draggable handle for arrow start (transparent ImGui window)
        if (arrow_selected) {
            char start_win[64];
            snprintf(start_win, sizeof(start_win), "##arrow_start_%d", ni);
            float drag_sz = handle_sz * 2;
            ImGui::SetNextWindowPos(ImVec2(arrow_start.x - handle_sz, arrow_start.y - handle_sz), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(drag_sz, drag_sz));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
            if (ImGui::Begin(start_win, nullptr, drag_flags)) {
                ImGui::PushID(ni * 2);
                ImGui::InvisibleButton("##sd", ImVec2(drag_sz, drag_sz));
                if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
                    set_cursor(AssemblyNoteCursorType::Move);
                    note_cursor_requested = true;
                }
                if (ImGui::IsItemClicked(0))
                    activate_note_edit_controls(AssemblyNoteSelectionType::ArrowSvg, ni);
                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                    ImVec2 delta = ImGui::GetIO().MouseDelta;
                    // Move only the start anchor; keep the SVG icon (end) in place
                    arrow.arrow_start_offset.x() += delta.x;
                    arrow.arrow_start_offset.y() += delta.y;
                    arrow.arrow_end_offset.x()   -= delta.x;
                    arrow.arrow_end_offset.y()   -= delta.y;
                    any_changed = true;
                }
                ImGui::PopID();
            }
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
            draw_list->AddCircleFilled(arrow_start, handle_sz * 0.6f, IM_COL32(0, 200, 80, 150));
        }

        // Draggable for arrow end (icon area, transparent ImGui window)
        {
            char end_win[64];
            snprintf(end_win, sizeof(end_win), "##arrow_end_%d", ni);
            ImGui::SetNextWindowPos(ImVec2(arrow_end.x - icon_box_sz.x * 0.5f, arrow_end.y - icon_box_sz.y * 0.5f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(icon_box_sz);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
            if (ImGui::Begin(end_win, nullptr, drag_flags)) {
                ImGui::PushID(ni * 2 + 1);
                ImGui::InvisibleButton("##ed", icon_box_sz);
                if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
                    set_cursor(AssemblyNoteCursorType::Move);
                    note_cursor_requested = true;
                }
                if (ImGui::IsItemClicked(0))
                    activate_note_edit_controls(AssemblyNoteSelectionType::ArrowSvg, ni);
                if (arrow_selected && ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                    ImVec2 delta = ImGui::GetIO().MouseDelta;
                    arrow.arrow_end_offset.x() += delta.x;
                    arrow.arrow_end_offset.y() += delta.y;
                    any_changed = true;
                }
                ImGui::PopID();
            }
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
        }

        if (arrow_selected) {
            ImVec2 box_min(arrow_end.x - icon_box_sz.x * 0.5f, arrow_end.y - icon_box_sz.y * 0.5f);
            ImVec2 box_max(arrow_end.x + icon_box_sz.x * 0.5f, arrow_end.y + icon_box_sz.y * 0.5f);

            // 4 corner resize handles in the same layout as TextLabelNote
            // (corner index: 0=tl, 1=tr, 2=bl, 3=br). The icon stays centered
            // on arrow_end, so dragging any corner shifts arrow_end_offset by
            // half the mouse delta (the box center moves half of the corner
            // movement when the opposite corner is anchored) while label_size
            // changes by the full delta with per-axis sign:
            //   tl: -dx, -dy   tr: +dx, -dy   bl: -dx, +dy   br: +dx, +dy
            auto drag_svg_resize_handle = [&](const char *id, const ImVec2 &handle_center, int corner) {
                ImVec2 handle_min = draw_note_resize_handle(handle_center);
                ImGui::SetNextWindowPos(handle_min, ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(resize_sz, resize_sz), ImGuiCond_Always);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
                if (ImGui::Begin(id, nullptr, drag_flags)) {
                    ImGui::InvisibleButton("##arrow_svg_resize", ImVec2(resize_sz, resize_sz));
                    mark_imgui_note_click();
                    if (ImGui::IsItemHovered()) {
                        set_cursor(corner == 1 || corner == 2 ? AssemblyNoteCursorType::ResizeNESW : AssemblyNoteCursorType::ResizeNWSE);
                        note_cursor_requested = true;
                    }
                    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                        ImVec2 delta = ImGui::GetIO().MouseDelta;
                        arrow.arrow_end_offset.x() += delta.x * 0.5;
                        arrow.arrow_end_offset.y() += delta.y * 0.5;
                        const float sx = (corner == 1 || corner == 3) ? 1.0f : -1.0f;
                        const float sy = (corner == 2 || corner == 3) ? 1.0f : -1.0f;
                        arrow.label_size.x() += sx * delta.x;
                        arrow.label_size.y() += sy * delta.y;
                        clamp_note_size(arrow.label_size, 32.0 * sc, 32.0 * sc);
                        any_changed = true;
                    }
                }
                ImGui::End();
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar(2);
            };
            char resize_win_id[64];
            snprintf(resize_win_id, sizeof(resize_win_id), "##arrow_svg_resize_tl_%d", ni);
            drag_svg_resize_handle(resize_win_id, box_min, 0);
            snprintf(resize_win_id, sizeof(resize_win_id), "##arrow_svg_resize_tr_%d", ni);
            drag_svg_resize_handle(resize_win_id, ImVec2(box_max.x, box_min.y), 1);
            snprintf(resize_win_id, sizeof(resize_win_id), "##arrow_svg_resize_bl_%d", ni);
            drag_svg_resize_handle(resize_win_id, ImVec2(box_min.x, box_max.y), 2);
            snprintf(resize_win_id, sizeof(resize_win_id), "##arrow_svg_resize_br_%d", ni);
            drag_svg_resize_handle(resize_win_id, box_max, 3);

            // Close button sits OUTSIDE the tr resize handle (matches
            // TextLabelNote's layout: tr-handle | gap | close-button) so it
            // never overlaps with the newly-added tr handle.
            ImVec2 close_center(box_max.x + resize_sz + close_r + 2.0f * sc, box_min.y);
            char close_win_id[64];
            snprintf(close_win_id, sizeof(close_win_id), "##arrow_svg_close_win_%d", ni);
            ImGui::SetNextWindowPos(ImVec2(close_center.x - close_r, close_center.y - close_r), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(close_r * 2.0f, close_r * 2.0f), ImGuiCond_Always);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
            if (ImGui::Begin(close_win_id, nullptr, drag_flags)) {
                if (draw_note_close_button(close_center, "##arrow_svg_close"))
                    delete_idx = ni;
            }
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
        }
    }

    int delete_text_idx = -1;
    static std::vector<float> s_text_label_wrap_widths;
    if (s_text_label_wrap_widths.size() < text_labels.size())
        s_text_label_wrap_widths.resize(text_labels.size(), 0.0f);
    for (int ni = 0; ni < (int)text_labels.size(); ++ni) {
        TextLabelNote &label = text_labels[ni];
        clamp_note_size(label.size, 80.0 * sc, 48.0 * sc);
        // Anchor to the bound volumes' on-screen bbox center (falls back to step center).
        const Vec2d label_center = compute_note_anchor_center(label.bound_volumes, obj_center);
        ImVec2 pos((float)(label_center.x() + label.pos_offset.x()), (float)(label_center.y() + label.pos_offset.y()));
        ImVec2 size((float)label.size.x(), (float)label.size.y());
        bool text_selected = is_note_selected(AssemblyNoteSelectionType::TextLabel, ni);

        const auto bg_color_arr = note_color_to_float_array(label.background_color);
        const ImVec4 label_bg(bg_color_arr[0], bg_color_arr[1], bg_color_arr[2], bg_color_arr[3]);

        // Paint the colored body (and selection border) on the background draw list so it stays below the side panels. .
        const float label_rounding = 4.0f * sc;
        draw_list->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                                 ImGui::ColorConvertFloat4ToU32(label_bg), label_rounding);
        if (text_selected)
            draw_list->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                               IM_COL32(25, 166, 77, 242), label_rounding, 0, 1.0f);

        char win_id[64];
        snprintf(win_id, sizeof(win_id), "##text_label_%d", ni);
        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f * sc, 6.0f * sc));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, label_rounding);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
        if (ImGui::Begin(win_id, nullptr, text_label_flags)) {
            ImGui::PushID(ni);

            ImVec2 text_size(std::max(1.0f, size.x - 12.0f * sc), std::max(24.0f * sc, size.y - 12.0f * sc));
            float wrap_width = std::max(1.0f, text_size.x - 8.0f * sc);
            // The colored body is now painted on the background draw list, so the InputText frame is kept transparent and the body shows through.
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
            const auto text_color = note_color_to_float_array(label.color);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(text_color[0], text_color[1], text_color[2], text_color[3]));
            // While the multiline input is active, ImGui owns an internal edit
            struct TextWrapCB {
                float cur_wrap_width;
                bool  move_cursor_to_end;
                std::string (*wrap_fn)(const std::string &, float);
            };
            const bool focus_request = (m_note_text_focus_request == ni);
            // A normal focus request (entering edit mode) drops the caret at the end of
            const bool focus_keep_cursor = focus_request && m_note_text_focus_keep_cursor;
            TextWrapCB cb_data;
            cb_data.cur_wrap_width     = wrap_width;
            cb_data.move_cursor_to_end = focus_request && !focus_keep_cursor;
            cb_data.wrap_fn            = wrap_text_to_width;
            ImGuiInputTextCallback text_wrap_callback = [](ImGuiInputTextCallbackData *data) -> int {
                TextWrapCB *cb = (TextWrapCB *)data->UserData;
                std::string cur(data->Buf, data->BufTextLen);
                std::string wrapped = cb->wrap_fn(cur, cb->cur_wrap_width);
                if (wrapped != cur) {
                    int visible_before = 0;
                    for (int i = 0; i < data->CursorPos && i < (int)cur.size(); ++i)
                        if (cur[i] != '\n')
                            ++visible_before;
                    data->DeleteChars(0, data->BufTextLen);
                    data->InsertChars(0, wrapped.c_str());
                    int new_pos = 0, seen = 0;
                    while (new_pos < (int)wrapped.size() && seen < visible_before) {
                        if (wrapped[new_pos] != '\n')
                            ++seen;
                        ++new_pos;
                    }
                    data->CursorPos = data->SelectionStart = data->SelectionEnd = new_pos;
                }
                if (cb->move_cursor_to_end)
                    data->CursorPos = data->SelectionStart = data->SelectionEnd = data->BufTextLen;
                return 0;
            };

            ImGuiInputTextFlags text_flags = ImGuiInputTextFlags_Multiline | ImGuiInputTextFlags_NoHorizontalScroll;
            if (text_selected)
                text_flags |= ImGuiInputTextFlags_CallbackAlways;
            else
                text_flags |= ImGuiInputTextFlags_ReadOnly;

            // Only grab keyboard focus on the frame(s) where a focus request is pending (right after the activating click).
            if (focus_request)
                ImGui::SetKeyboardFocusHere();
            bool text_changed = ImGui::InputTextMultiline("##label_text", &label.text, text_size, text_flags,
                                                          text_selected ? text_wrap_callback : nullptr, &cb_data);
            bool text_active = ImGui::IsItemActive();
            if (focus_request && (text_active || ImGui::IsItemFocused())) {
                m_note_text_focus_request     = -1;
                m_note_text_focus_keep_cursor = false;
            }
            // ImGui deactivates an active InputText whenever a mouse click happens while
            if (text_selected && ImGui::IsItemDeactivated() && ImGui::IsMouseClicked(0)) {
                const ImVec2 m = ImGui::GetIO().MousePos;
                const bool inside = m.x >= pos.x && m.x <= pos.x + size.x &&
                                    m.y >= pos.y && m.y <= pos.y + size.y;
                if (inside) {
                    m_note_text_focus_request     = ni;
                    m_note_text_focus_keep_cursor = true;
                }
            }
            if (ImGui::IsItemClicked(0)) {
                if (!text_selected) {
                    m_note_text_focus_request     = ni;
                    m_note_text_focus_keep_cursor = false;
                }
                activate_note_edit_controls(AssemblyNoteSelectionType::TextLabel, ni);
            }
            if (text_changed)
                any_changed = true;
            // Re-flow (unwrap + wrap) only when the input is NOT being edited, e.g. after the box is resized while inactive.
            if (!text_active && std::fabs(s_text_label_wrap_widths[ni] - wrap_width) > 1.0f) {
                std::string unwrapped_text = unwrap_soft_newlines(label.text, s_text_label_wrap_widths[ni]);
                std::string wrapped_text = wrap_text_to_width(unwrapped_text, wrap_width);
                if (wrapped_text != label.text) {
                    label.text = std::move(wrapped_text);
                    any_changed = true;
                }
            }
            s_text_label_wrap_widths[ni] = wrap_width;
            ImGui::PopStyleColor(2);

            double desired_height = text_line_count(label.text) * ImGui::GetTextLineHeightWithSpacing() + 16.0 * sc;
            desired_height = std::min(desired_height, std::max(48.0 * sc, (double)viewport[3] - pos.y - 12.0 * sc));
            if (label.size.y() < desired_height) {
                label.size.y() = desired_height;
                any_changed = true;
            }

            ImGui::PopID();
        }
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);

        if (text_selected) {
            auto drag_border = [&](const char *id, const ImVec2 &border_pos, const ImVec2 &border_size) {
                ImGui::SetNextWindowPos(border_pos, ImGuiCond_Always);
                ImGui::SetNextWindowSize(border_size, ImGuiCond_Always);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
                if (ImGui::Begin(id, nullptr, drag_flags)) {
                    ImGui::InvisibleButton("##label_border_move", border_size);
                    mark_imgui_note_click();
                    if (ImGui::IsItemHovered()) {
                        set_cursor(AssemblyNoteCursorType::Move);
                        note_cursor_requested = true;
                    }
                    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                        ImVec2 delta = ImGui::GetIO().MouseDelta;
                        label.pos_offset.x() += delta.x;
                        label.pos_offset.y() += delta.y;
                        any_changed = true;
                    }
                }
                ImGui::End();
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar(2);
            };
            const float border_hot = 6.0f * sc;
            char border_id[64];
            snprintf(border_id, sizeof(border_id), "##label_border_top_%d", ni);
            drag_border(border_id, ImVec2(pos.x + resize_sz, pos.y - border_hot * 0.5f), ImVec2(std::max(1.0f, size.x - resize_sz * 2.0f), border_hot));
            snprintf(border_id, sizeof(border_id), "##label_border_bottom_%d", ni);
            drag_border(border_id, ImVec2(pos.x + resize_sz, pos.y + size.y - border_hot * 0.5f), ImVec2(std::max(1.0f, size.x - resize_sz * 2.0f), border_hot));
            snprintf(border_id, sizeof(border_id), "##label_border_left_%d", ni);
            drag_border(border_id, ImVec2(pos.x - border_hot * 0.5f, pos.y + resize_sz), ImVec2(border_hot, std::max(1.0f, size.y - resize_sz * 2.0f)));
            snprintf(border_id, sizeof(border_id), "##label_border_right_%d", ni);
            drag_border(border_id, ImVec2(pos.x + size.x - border_hot * 0.5f, pos.y + resize_sz), ImVec2(border_hot, std::max(1.0f, size.y - resize_sz * 2.0f)));
        }

        if (text_selected) {
            auto drag_resize_handle = [&](const char *id, const ImVec2 &handle_center, int corner) {
                ImVec2 handle_min = draw_note_resize_handle(handle_center);
                ImGui::SetNextWindowPos(handle_min, ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(resize_sz, resize_sz), ImGuiCond_Always);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
                if (ImGui::Begin(id, nullptr, drag_flags)) {
                    ImGui::InvisibleButton("##label_resize", ImVec2(resize_sz, resize_sz));
                    mark_imgui_note_click();
                    if (ImGui::IsItemHovered()) {
                        set_cursor(corner == 1 || corner == 2 ? AssemblyNoteCursorType::ResizeNESW : AssemblyNoteCursorType::ResizeNWSE);
                        note_cursor_requested = true;
                    }
                    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                        ImVec2 delta = ImGui::GetIO().MouseDelta;
                        if (corner == 0) {
                            label.pos_offset.x() += delta.x;
                            label.pos_offset.y() += delta.y;
                            label.size.x() -= delta.x;
                            label.size.y() -= delta.y;
                        } else if (corner == 1) {
                            label.pos_offset.y() += delta.y;
                            label.size.x() += delta.x;
                            label.size.y() -= delta.y;
                        } else if (corner == 2) {
                            label.pos_offset.x() += delta.x;
                            label.size.x() -= delta.x;
                            label.size.y() += delta.y;
                        } else {
                            label.size.x() += delta.x;
                            label.size.y() += delta.y;
                        }
                        clamp_note_size(label.size, 80.0 * sc, 48.0 * sc);
                        any_changed = true;
                    }
                }
                ImGui::End();
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar(2);
            };
            char resize_win_id[64];
            snprintf(resize_win_id, sizeof(resize_win_id), "##label_resize_tl_%d", ni);
            drag_resize_handle(resize_win_id, ImVec2(pos.x, pos.y), 0);
            snprintf(resize_win_id, sizeof(resize_win_id), "##label_resize_tr_%d", ni);
            drag_resize_handle(resize_win_id, ImVec2(pos.x + size.x, pos.y), 1);
            snprintf(resize_win_id, sizeof(resize_win_id), "##label_resize_bl_%d", ni);
            drag_resize_handle(resize_win_id, ImVec2(pos.x, pos.y + size.y), 2);
            snprintf(resize_win_id, sizeof(resize_win_id), "##label_resize_br_%d", ni);
            drag_resize_handle(resize_win_id, ImVec2(pos.x + size.x, pos.y + size.y), 3);
        }

        if (text_selected) {
            const float close_hit_pad = 4.0f * sc;
            const float close_hit_r   = close_r + close_hit_pad;
            ImVec2 close_center(pos.x + size.x + resize_sz + close_r + 2.0f * sc, pos.y);
            char close_win_id[64];
            snprintf(close_win_id, sizeof(close_win_id), "##label_close_win_%d", ni);
            ImGui::SetNextWindowPos(ImVec2(close_center.x - close_hit_r, close_center.y - close_hit_r), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(close_hit_r * 2.0f, close_hit_r * 2.0f), ImGuiCond_Always);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
            if (ImGui::Begin(close_win_id, nullptr, drag_flags)) {
                if (draw_note_close_button(close_center, "##label_close", -1.0f,
                                           IM_COL32(200, 60, 60, 230), IM_COL32(255, 255, 255, 255),
                                           close_hit_pad))
                    delete_text_idx = ni;
            }
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
        }
    }

    int delete_circle_idx = -1;
    for (int ni = 0; ni < (int)circle_notes.size(); ++ni) {
        CircleNote &circle = circle_notes[ni];
        clamp_note_size(circle.size, 24.0 * sc, 24.0 * sc);
        // Anchor to the bound volumes' on-screen bbox center (falls back to step center).
        const Vec2d circle_center = compute_note_anchor_center(circle.bound_volumes, obj_center);
        ImVec2 pos((float)(circle_center.x() + circle.pos_offset.x()), (float)(circle_center.y() + circle.pos_offset.y()));
        ImVec2 size((float)circle.size.x(), (float)circle.size.y());
        ImVec2 center(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
        ImVec2 radius(size.x * 0.5f, size.y * 0.5f);
        bool circle_selected = is_note_selected(AssemblyNoteSelectionType::Circle, ni);
        static constexpr double PI = 3.14159265358979323846;

        draw_list->PathClear();
        for (int seg = 0; seg < 48; ++seg) {
            double angle = 2.0 * PI * (double)seg / 48.0;
            draw_list->PathLineTo(ImVec2(center.x + (float)std::cos(angle) * radius.x,
                                         center.y + (float)std::sin(angle) * radius.y));
        }
        draw_list->PathStroke(note_color_to_im_u32(circle.color), true, 2.0f * sc);

        char win_id[64];
        snprintf(win_id, sizeof(win_id), "##circle_note_%d", ni);

        // Moving uses the full circle bounds, while close/resize get their own
        // small overlay windows so they are not swallowed by the move hit area.
        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
        if (ImGui::Begin(win_id, nullptr, drag_flags)) {
            ImGui::PushID(ni);
            ImGui::InvisibleButton("##circle_move", size);
            ImVec2 mouse_pos = ImGui::GetIO().MousePos;
            bool on_circle_line = is_point_on_ellipse_outline(mouse_pos, center, radius, line_hit_tol);
            if (ImGui::IsItemHovered() && on_circle_line) {
                set_cursor(AssemblyNoteCursorType::Move);
                note_cursor_requested = true;
            }
            if (ImGui::IsItemClicked(0) && on_circle_line)
                activate_note_edit_controls(AssemblyNoteSelectionType::Circle, ni);
            if (ImGui::IsItemActivated() && on_circle_line)
                s_circle_line_drag_idx = ni;
            if (s_circle_line_drag_idx == ni && ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                set_cursor(AssemblyNoteCursorType::Move);
                note_cursor_requested = true;
                ImVec2 delta = ImGui::GetIO().MouseDelta;
                circle.pos_offset.x() += delta.x;
                circle.pos_offset.y() += delta.y;
                any_changed = true;
            }
            ImGui::PopID();
        }
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);

        if (circle_selected) {
            // 4 corner resize handles in the same layout as TextLabelNote
            // (corner index: 0=tl, 1=tr, 2=bl, 3=br). Each corner anchors the
            // diagonally opposite corner so the ellipse stretches/shrinks
            // toward the dragged corner. pos_offset tracks the bbox top-left
            // (= obj_center + offset), so it shifts whenever the top or left
            // edge moves.
            auto drag_circle_resize_handle = [&](const char *id, const ImVec2 &handle_center, int corner) {
                ImVec2 handle_min = draw_note_resize_handle(handle_center);
                ImGui::SetNextWindowPos(handle_min, ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(resize_sz, resize_sz), ImGuiCond_Always);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
                if (ImGui::Begin(id, nullptr, drag_flags)) {
                    ImGui::InvisibleButton("##circle_resize", ImVec2(resize_sz, resize_sz));
                    mark_imgui_note_click();
                    if (ImGui::IsItemHovered()) {
                        set_cursor(corner == 1 || corner == 2 ? AssemblyNoteCursorType::ResizeNESW : AssemblyNoteCursorType::ResizeNWSE);
                        note_cursor_requested = true;
                    }
                    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                        ImVec2 delta = ImGui::GetIO().MouseDelta;
                        if (corner == 0) {
                            circle.pos_offset.x() += delta.x;
                            circle.pos_offset.y() += delta.y;
                            circle.size.x() -= delta.x;
                            circle.size.y() -= delta.y;
                        } else if (corner == 1) {
                            circle.pos_offset.y() += delta.y;
                            circle.size.x() += delta.x;
                            circle.size.y() -= delta.y;
                        } else if (corner == 2) {
                            circle.pos_offset.x() += delta.x;
                            circle.size.x() -= delta.x;
                            circle.size.y() += delta.y;
                        } else {
                            circle.size.x() += delta.x;
                            circle.size.y() += delta.y;
                        }
                        clamp_note_size(circle.size, 24.0 * sc, 24.0 * sc);
                        any_changed = true;
                    }
                }
                ImGui::End();
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar(2);
            };
            char resize_win_id[64];
            snprintf(resize_win_id, sizeof(resize_win_id), "##circle_resize_tl_%d", ni);
            drag_circle_resize_handle(resize_win_id, ImVec2(pos.x, pos.y), 0);
            snprintf(resize_win_id, sizeof(resize_win_id), "##circle_resize_tr_%d", ni);
            drag_circle_resize_handle(resize_win_id, ImVec2(pos.x + size.x, pos.y), 1);
            snprintf(resize_win_id, sizeof(resize_win_id), "##circle_resize_bl_%d", ni);
            drag_circle_resize_handle(resize_win_id, ImVec2(pos.x, pos.y + size.y), 2);
            snprintf(resize_win_id, sizeof(resize_win_id), "##circle_resize_br_%d", ni);
            drag_circle_resize_handle(resize_win_id, ImVec2(pos.x + size.x, pos.y + size.y), 3);

            // Close button sits OUTSIDE the tr resize handle (matches
            // TextLabelNote's layout: tr-handle | gap | close-button) so it
            // never overlaps with the newly-added tr handle.
            ImVec2 close_center(pos.x + size.x + resize_sz + close_r + 2.0f * sc, pos.y);
            char close_win_id[64];
            snprintf(close_win_id, sizeof(close_win_id), "##circle_close_win_%d", ni);
            ImGui::SetNextWindowPos(ImVec2(close_center.x - close_r, close_center.y - close_r), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(close_r * 2.0f, close_r * 2.0f), ImGuiCond_Always);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
            if (ImGui::Begin(close_win_id, nullptr, drag_flags)) {
                if (draw_note_close_button(close_center, "##circle_close"))
                    delete_circle_idx = ni;
            }
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
        }
    }
    // ----------------- Rectangle notes ------- Same interaction model as CircleNote: outline-only stroke, click on the
    static int s_rect_line_drag_idx = -1;
    if (!ImGui::IsMouseDown(0))
        s_rect_line_drag_idx = -1;
    auto is_point_on_rect_outline = [](const ImVec2 &p, const ImVec2 &min_pt, const ImVec2 &max_pt, float tolerance) {
        ImVec2 omin(min_pt.x - tolerance, min_pt.y - tolerance);
        ImVec2 omax(max_pt.x + tolerance, max_pt.y + tolerance);
        if (p.x < omin.x || p.x > omax.x || p.y < omin.y || p.y > omax.y)
            return false;
        ImVec2 imin(min_pt.x + tolerance, min_pt.y + tolerance);
        ImVec2 imax(max_pt.x - tolerance, max_pt.y - tolerance);
        bool inside_inner = (imin.x < imax.x && imin.y < imax.y) &&
                            (p.x > imin.x && p.x < imax.x && p.y > imin.y && p.y < imax.y);
        return !inside_inner;
    };

    int delete_rect_idx = -1;
    for (int ni = 0; ni < (int)rectangle_notes.size(); ++ni) {
        RectangleNote &rect = rectangle_notes[ni];
        clamp_note_size(rect.size, 24.0 * sc, 24.0 * sc);
        // Anchor to the bound volumes' on-screen bbox center (falls back to step center).
        const Vec2d rect_center = compute_note_anchor_center(rect.bound_volumes, obj_center);
        ImVec2 pos((float)(rect_center.x() + rect.pos_offset.x()), (float)(rect_center.y() + rect.pos_offset.y()));
        ImVec2 size((float)rect.size.x(), (float)rect.size.y());
        ImVec2 max_pt(pos.x + size.x, pos.y + size.y);
        bool rect_selected = is_note_selected(AssemblyNoteSelectionType::Rectangle, ni);

        draw_list->AddRect(pos, max_pt, note_color_to_im_u32(rect.color), 0.0f, 0, 2.0f * sc);

        char win_id[64];
        snprintf(win_id, sizeof(win_id), "##rect_note_%d", ni);
        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
        if (ImGui::Begin(win_id, nullptr, drag_flags)) {
            ImGui::PushID(ni);
            ImGui::InvisibleButton("##rect_move", size);
            ImVec2 mouse_pos = ImGui::GetIO().MousePos;
            bool on_rect_line = is_point_on_rect_outline(mouse_pos, pos, max_pt, line_hit_tol);
            if (ImGui::IsItemHovered() && on_rect_line) {
                set_cursor(AssemblyNoteCursorType::Move);
                note_cursor_requested = true;
            }
            if (ImGui::IsItemClicked(0) && on_rect_line)
                activate_note_edit_controls(AssemblyNoteSelectionType::Rectangle, ni);
            if (ImGui::IsItemActivated() && on_rect_line)
                s_rect_line_drag_idx = ni;
            if (s_rect_line_drag_idx == ni && ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                set_cursor(AssemblyNoteCursorType::Move);
                note_cursor_requested = true;
                ImVec2 delta = ImGui::GetIO().MouseDelta;
                rect.pos_offset.x() += delta.x;
                rect.pos_offset.y() += delta.y;
                any_changed = true;
            }
            ImGui::PopID();
        }
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);

        if (rect_selected) {
            auto drag_rect_resize_handle = [&](const char *id, const ImVec2 &handle_center, int corner) {
                ImVec2 handle_min = draw_note_resize_handle(handle_center);
                ImGui::SetNextWindowPos(handle_min, ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(resize_sz, resize_sz), ImGuiCond_Always);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
                if (ImGui::Begin(id, nullptr, drag_flags)) {
                    ImGui::InvisibleButton("##rect_resize", ImVec2(resize_sz, resize_sz));
                    mark_imgui_note_click();
                    if (ImGui::IsItemHovered()) {
                        set_cursor(corner == 1 || corner == 2 ? AssemblyNoteCursorType::ResizeNESW : AssemblyNoteCursorType::ResizeNWSE);
                        note_cursor_requested = true;
                    }
                    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                        ImVec2 delta = ImGui::GetIO().MouseDelta;
                        if (corner == 0) {
                            rect.pos_offset.x() += delta.x;
                            rect.pos_offset.y() += delta.y;
                            rect.size.x() -= delta.x;
                            rect.size.y() -= delta.y;
                        } else if (corner == 1) {
                            rect.pos_offset.y() += delta.y;
                            rect.size.x() += delta.x;
                            rect.size.y() -= delta.y;
                        } else if (corner == 2) {
                            rect.pos_offset.x() += delta.x;
                            rect.size.x() -= delta.x;
                            rect.size.y() += delta.y;
                        } else {
                            rect.size.x() += delta.x;
                            rect.size.y() += delta.y;
                        }
                        clamp_note_size(rect.size, 24.0 * sc, 24.0 * sc);
                        any_changed = true;
                    }
                }
                ImGui::End();
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar(2);
            };
            char resize_win_id[64];
            snprintf(resize_win_id, sizeof(resize_win_id), "##rect_resize_tl_%d", ni);
            drag_rect_resize_handle(resize_win_id, ImVec2(pos.x, pos.y), 0);
            snprintf(resize_win_id, sizeof(resize_win_id), "##rect_resize_tr_%d", ni);
            drag_rect_resize_handle(resize_win_id, ImVec2(pos.x + size.x, pos.y), 1);
            snprintf(resize_win_id, sizeof(resize_win_id), "##rect_resize_bl_%d", ni);
            drag_rect_resize_handle(resize_win_id, ImVec2(pos.x, pos.y + size.y), 2);
            snprintf(resize_win_id, sizeof(resize_win_id), "##rect_resize_br_%d", ni);
            drag_rect_resize_handle(resize_win_id, ImVec2(pos.x + size.x, pos.y + size.y), 3);

            ImVec2 close_center(pos.x + size.x + resize_sz + close_r + 2.0f * sc, pos.y);
            char close_win_id[64];
            snprintf(close_win_id, sizeof(close_win_id), "##rect_close_win_%d", ni);
            ImGui::SetNextWindowPos(ImVec2(close_center.x - close_r, close_center.y - close_r), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(close_r * 2.0f, close_r * 2.0f), ImGuiCond_Always);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
            if (ImGui::Begin(close_win_id, nullptr, drag_flags)) {
                if (draw_note_close_button(close_center, "##rect_close"))
                    delete_rect_idx = ni;
            }
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
        }
    }

    int delete_plain_arrow_idx = -1;
    for (int ni = 0; ni < plain_arrow_count; ++ni) {
        PlainArrowNote &arrow = plain_arrows[ni];
        const ImVec2 &arrow_start = plain_arrow_screen_data[ni].start;
        const ImVec2 &arrow_end   = plain_arrow_screen_data[ni].end;
        bool plain_arrow_selected = is_note_selected(AssemblyNoteSelectionType::PlainArrow, ni);
        const float close_gap = 8.0f * sc;
        ImVec2 close_center(std::max(arrow_start.x, arrow_end.x) + close_r + close_gap,
                            std::min(arrow_start.y, arrow_end.y));
        ImVec2 mouse_pos = ImGui::GetIO().MousePos;
        bool plain_arrow_close_hot = plain_arrow_selected
            && mouse_pos.x >= close_center.x - close_r && mouse_pos.x <= close_center.x + close_r
            && mouse_pos.y >= close_center.y - close_r && mouse_pos.y <= close_center.y + close_r;

        if (!plain_arrow_close_hot) {
            float vx = arrow_end.x - arrow_start.x;
            float vy = arrow_end.y - arrow_start.y;
            float line_len = std::sqrt(vx * vx + vy * vy);
            float endpoint_hot = handle_sz * 2.0f;
            if (line_len > endpoint_hot * 2.0f) {
                ImVec2 line_body_start(arrow_start.x + vx / line_len * endpoint_hot,
                                       arrow_start.y + vy / line_len * endpoint_hot);
                ImVec2 line_body_end(arrow_end.x - vx / line_len * endpoint_hot,
                                     arrow_end.y - vy / line_len * endpoint_hot);
                ImVec2 line_min(std::min(line_body_start.x, line_body_end.x) - line_hit_tol,
                                std::min(line_body_start.y, line_body_end.y) - line_hit_tol);
                ImVec2 line_max(std::max(line_body_start.x, line_body_end.x) + line_hit_tol,
                                std::max(line_body_start.y, line_body_end.y) + line_hit_tol);
                ImVec2 line_size(std::max(1.0f, line_max.x - line_min.x),
                                 std::max(1.0f, line_max.y - line_min.y));
                char line_win_id[64];
                snprintf(line_win_id, sizeof(line_win_id), "##plain_arrow_line_%d", ni);
                ImGui::SetNextWindowPos(line_min, ImGuiCond_Always);
                ImGui::SetNextWindowSize(line_size, ImGuiCond_Always);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
                if (ImGui::Begin(line_win_id, nullptr, drag_flags)) {
                    ImGui::InvisibleButton("##plain_arrow_line_drag", line_size);
                    bool on_arrow_line = distance_to_segment(mouse_pos, line_body_start, line_body_end) <= line_hit_tol;
                    if (ImGui::IsItemHovered() && on_arrow_line) {
                        set_cursor(AssemblyNoteCursorType::Move);
                        note_cursor_requested = true;
                    }
                    if (ImGui::IsItemClicked(0) && on_arrow_line)
                        activate_note_edit_controls(AssemblyNoteSelectionType::PlainArrow, ni);
                    if (ImGui::IsItemActivated() && on_arrow_line)
                        s_plain_arrow_line_drag_idx = ni;
                    if (s_plain_arrow_line_drag_idx == ni && ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                        set_cursor(AssemblyNoteCursorType::Move);
                        note_cursor_requested = true;
                        ImVec2 delta = ImGui::GetIO().MouseDelta;
                        arrow.arrow_start_offset.x() += delta.x;
                        arrow.arrow_start_offset.y() += delta.y;
                        any_changed = true;
                    }
                }
                ImGui::End();
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar(2);
            }
        }

        auto drag_arrow_point = [&](const char *name, const ImVec2 &point, bool drag_start) {
            char win_id[64];
            snprintf(win_id, sizeof(win_id), "%s_%d", name, ni);
            // The arrow-end handle also covers the triangle arrowhead area, so
            // users can drag from the visible arrowhead instead of only the dot.
            float drag_sz = drag_start ? handle_sz * 2.0f : std::max(handle_sz * 2.0f, 28.0f * sc);
            float half_drag_sz = drag_sz * 0.5f;
            ImGui::SetNextWindowPos(ImVec2(point.x - half_drag_sz, point.y - half_drag_sz), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(drag_sz, drag_sz));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
            if (ImGui::Begin(win_id, nullptr, drag_flags)) {
                ImGui::InvisibleButton("##plain_arrow_drag", ImVec2(drag_sz, drag_sz));
                mark_imgui_note_click();
                if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
                    set_cursor(AssemblyNoteCursorType::ResizeNWSE);
                    note_cursor_requested = true;
                }
                if (ImGui::IsItemClicked(0))
                    activate_note_edit_controls(AssemblyNoteSelectionType::PlainArrow, ni);
                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                    ImVec2 delta = ImGui::GetIO().MouseDelta;
                    if (drag_start) {
                        arrow.arrow_start_offset.x() += delta.x;
                        arrow.arrow_start_offset.y() += delta.y;
                        arrow.arrow_end_offset.x() -= delta.x;
                        arrow.arrow_end_offset.y() -= delta.y;
                    } else {
                        arrow.arrow_end_offset.x() += delta.x;
                        arrow.arrow_end_offset.y() += delta.y;
                    }
                    any_changed = true;
                }
            }
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
        };
        if (plain_arrow_selected && !plain_arrow_close_hot) {
            drag_arrow_point("##plain_arrow_start", arrow_start, true);
            drag_arrow_point("##plain_arrow_end", arrow_end, false);
        }
        if (plain_arrow_selected) {
            draw_list->AddCircleFilled(arrow_start, handle_sz * 0.6f, IM_COL32(0, 200, 80, 150));
            draw_list->AddCircleFilled(arrow_end, handle_sz * 0.6f, IM_COL32(0, 160, 220, 150));
        }

        if (plain_arrow_selected) {
            char close_id[64];
            snprintf(close_id, sizeof(close_id), "##plain_arrow_close_%d", ni);
            ImGui::SetNextWindowPos(ImVec2(close_center.x - close_r, close_center.y - close_r), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(close_r * 2.0f, close_r * 2.0f), ImGuiCond_Always);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
            if (ImGui::Begin(close_id, nullptr, drag_flags)) {
                if (draw_note_close_button(close_center, "##close"))
                    delete_plain_arrow_idx = ni;
            }
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
        }
    }

    auto on_note_erased = [&](AssemblyNoteSelectionType type, int idx) {
        if (m_note_selected_type != type)
            return;
        if (m_note_selected_idx == idx) {
            exit_note_edit();
        } else if (m_note_selected_idx > idx) {
            --m_note_selected_idx;
        }
    };

    if (delete_idx >= 0 && delete_idx < count) {
        on_note_erased(AssemblyNoteSelectionType::ArrowSvg, delete_idx);
        arrow_svgs.erase(arrow_svgs.begin() + delete_idx);
        any_changed = true;
    }
    if (delete_text_idx >= 0 && delete_text_idx < (int)text_labels.size()) {
        on_note_erased(AssemblyNoteSelectionType::TextLabel, delete_text_idx);
        text_labels.erase(text_labels.begin() + delete_text_idx);
        any_changed = true;
    }
    if (delete_circle_idx >= 0 && delete_circle_idx < (int)circle_notes.size()) {
        on_note_erased(AssemblyNoteSelectionType::Circle, delete_circle_idx);
        circle_notes.erase(circle_notes.begin() + delete_circle_idx);
        any_changed = true;
    }
    if (delete_rect_idx >= 0 && delete_rect_idx < (int)rectangle_notes.size()) {
        on_note_erased(AssemblyNoteSelectionType::Rectangle, delete_rect_idx);
        rectangle_notes.erase(rectangle_notes.begin() + delete_rect_idx);
        any_changed = true;
    }
    if (delete_plain_arrow_idx >= 0 && delete_plain_arrow_idx < (int)plain_arrows.size()) {
        on_note_erased(AssemblyNoteSelectionType::PlainArrow, delete_plain_arrow_idx);
        plain_arrows.erase(plain_arrows.begin() + delete_plain_arrow_idx);
        any_changed = true;
    }

    if (!note_cursor_requested)
        reset_cursor_if_note_cursor();

    const bool mouse_down = ImGui::IsMouseDown(0);
    if (any_changed) {
        cur_entry.need_save = true;
        if (mouse_down) {
            // Dragging notes can update every frame. Defer the expensive JSON
            s_deferred_note_save_until_mouse_release = true;
            do_commond_callback("request_extra_frame");
        } else {
            save_assembly_steps_json_to_model();
            s_deferred_note_save_until_mouse_release = false;
        }
    } else if (s_deferred_note_save_until_mouse_release && !mouse_down) {
        cur_entry.need_save = true;
        save_assembly_steps_json_to_model();
        s_deferred_note_save_until_mouse_release = false;
    }

    // Part-number labels: pill text + drag (lines already drawn above in Pass 1).
    if (has_pn)
        render_part_number_labels_on_canvas(viewport, vp_height);

}

void AssemblyStepsUtils::render_assembly_structure_option_menu(
    ImGuiWrapper &imgui,
    float sc,
    bool is_dark)
{
    ImGui::PushStyleColor(ImGuiCol_PopupBg,
        is_dark ? ImVec4(0.18f, 0.18f, 0.20f, 0.95f) : ImVec4(0.96f, 0.96f, 0.96f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Text,
        is_dark ? ImVec4(0.85f, 0.85f, 0.85f, 1.0f) : ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
        is_dark ? ImVec4(0.30f, 0.55f, 0.80f, 0.60f) : ImVec4(0.26f, 0.59f, 0.98f, 0.31f));
    ImGui::PushStyleColor(ImGuiCol_Separator,
        is_dark ? ImVec4(0.35f, 0.35f, 0.40f, 1.0f) : ImVec4(0.70f, 0.70f, 0.70f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * sc, 6.0f * sc));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * sc, 4.0f * sc));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 4.0f * sc);

    if (ImGui::BeginPopup("##assembly_structure_option_menu")) {
        // Reflect the currently selected keyframe's stored margin in the slider when
        // the menu opens, so each keyframe shows (and edits) its own value.
        if (ImGui::IsWindowAppearing()) {
            if (const KeyFrameEntry *cur = get_selected_keyframe_entry())
                m_margin_factor_camera_for_not_last_frame = (float) cur->data.camera_margin_factor;
        }

        if (ImGui::MenuItem(_u8L("Set export file parameters").c_str()))
            show_pdf_export_settings_dialog();

        ImGui::Separator();
        {
            const float       margin_slider_w = 120.0f * sc;
            const float       margin_value_w  = 56.0f * sc;
            const std::string margin_tip      = _u8L("Camera margin factor for the current frame. The larger the value, the more margin.");
            auto              set_margin_tip  = [&]() {
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", margin_tip.c_str());
            };

            imgui.text(_u8L("Modify camera margin"));
            set_margin_tip();

            bool margin_changed  = false;
            bool margin_released = false;
            ImGui::PushItemWidth(margin_slider_w);
            // bbl_slider_float_style only clears the hovered/active frame bg, so the
            // idle track would otherwise show the theme's dark FrameBg. Match hover.
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            margin_changed |= imgui.bbl_slider_float_style("##assembly_start_frame_camera_margin", &m_margin_factor_camera_for_not_last_frame, 1.2f, 2.0f, "%.2f");
            margin_released |= imgui.get_last_slider_status().deactivated_after_edit;
            ImGui::PopStyleColor();
            set_margin_tip();
            ImGui::SameLine();
            ImGui::PushItemWidth(margin_value_w);
            // Drop the idle gray frame background; keep the hover / active styling.
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            margin_changed |= ImGui::BBLDragFloat("##assembly_start_frame_camera_margin_input", &m_margin_factor_camera_for_not_last_frame, 0.01f, 1.2f, 2.0f, "%.2f");
            margin_released |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::PopStyleColor();
            set_margin_tip();

            // Store the edited margin on the current keyframe and re-frame live; only
            // persist to the model once the edit ends (slider released) to avoid
            // rewriting the assembly json on every intermediate drag value.
            if (margin_changed || margin_released)
                apply_camera_margin_to_selected_keyframe(m_margin_factor_camera_for_not_last_frame, margin_released);
        }
        ImGui::Separator();
        {
            update_part_number_label_font_size_from_config();

            const float       label_slider_w = 120.0f * sc;
            const float       label_value_w  = 56.0f * sc;
            const float       label_min      = ASSEMBLY_LABEL_DEFAULT_FONT_SIZE;
            const float       label_max      = ASSEMBLY_LABEL_DEFAULT_FONT_SIZE * ASSEMBLY_LABEL_MAX_FONT_SIZE_FACTOR;
            float             label_font_size = part_number_label_font_size();
            const std::string label_tip      = _u8L("Modify canvas label text size, and real-time update the label text size of all frames after adjustment.");
            auto              set_label_tip  = [&]() {
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", label_tip.c_str());
            };

            imgui.text(_u8L("Canvas label text size"));
            set_label_tip();

            bool label_changed = false;
            bool label_released = false;
            ImGui::PushItemWidth(label_slider_w);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            label_changed |= imgui.bbl_slider_float_style("##assembly_label_font_size", &label_font_size, label_min, label_max, "%.2f");
            label_released |= imgui.get_last_slider_status().deactivated_after_edit;
            ImGui::PopStyleColor();
            set_label_tip();
            ImGui::SameLine();
            ImGui::PushItemWidth(label_value_w);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            label_changed |= ImGui::BBLDragFloat("##assembly_label_font_size_input", &label_font_size, 0.1f, label_min, label_max, "%.2f");
            label_released |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::PopStyleColor();
            set_label_tip();

            if (label_changed) {
                save_part_number_label_font_size_to_config(label_font_size);
                do_commond_callback("dirty");
                do_commond_callback("request_extra_frame");
            }
            if (label_released) {
                save_part_number_label_font_size_to_config(label_font_size, true);
                auto_layout_labels_in_current_view();
            }
        }
#if !BBL_RELEASE_TO_PUBLIC
 /*       bool explode_repeated_as_whole = m_show_modelobject_name_when_modelobject_has_occur_before;
        if (ImGui::MenuItem(_u8L("Explode repeated model objects as a whole").c_str(), nullptr,
                            &explode_repeated_as_whole)) {
            m_show_modelobject_name_when_modelobject_has_occur_before = explode_repeated_as_whole;
        }
        bool show_origin_step_tree = AssemblyTreeData::show_origin_step_tree;
        if (ImGui::MenuItem(_u8L("Show origin STEP tree").c_str(), nullptr, &show_origin_step_tree)) {
            AssemblyTreeData::show_origin_step_tree = show_origin_step_tree;
            m_assembly_tree_ui_current_folder_node = -1;
            m_assembly_tree_ui_original_checked.clear();
        }*/
#endif
        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(4);
}

bool AssemblyStepsUtils::render_structure_card_select_controls(
    int card_idx,
    const ImVec2& pos,
    const AssemblySelectControlsStyle& style,
    const std::string& value_label,
    const std::string& full_value_label)
{
    ImFont*     font      = ImGui::GetFont();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    const std::string select_label = _u8L("Select");
    const float label_w = font->CalcTextSizeA(style.font_size, FLT_MAX, 0.f, select_label.c_str()).x;
    const float width = label_w + 2.0f * style.pad_x;
    const ImVec2 max(pos.x + width, pos.y + style.height);

    draw_list->AddRectFilled(pos, max, style.bg_col, style.radius);
    draw_list->AddText(font, style.font_size,
                       ImVec2(pos.x + style.pad_x, pos.y + (style.height - style.font_size) * 0.5f),
                       style.button_text_col, select_label.c_str());

    const float value_x = max.x + style.gap;
    const float value_w = value_label.empty()
        ? 0.f
        : font->CalcTextSizeA(style.font_size, FLT_MAX, 0.f, value_label.c_str()).x;
    if (!value_label.empty())
        draw_list->AddText(font, style.font_size,
                           ImVec2(value_x, pos.y + (style.height - style.font_size) * 0.5f),
                           style.label_text_col, value_label.c_str());

    ImGui::SetCursorScreenPos(pos);
    const std::string id = "##asp_sel_" + std::to_string(card_idx);
    ImGui::InvisibleButton(id.c_str(), ImVec2(width, style.height));
    const bool clicked = ImGui::IsItemClicked();
    const bool button_hovered = ImGui::IsItemHovered();
    if (button_hovered)
        render_panel_tooltip(_u8L("Select partial parts or objects from the added items to perform operations such as move, rotate, and coloring gizmo. Pose-modifying operations like move gizmo can be used to create custom keyframe animations."));
    if (clicked)
        m_structure_select_popup_pending_card = card_idx;

    bool label_hovered = false;
    if (value_w > 0.f) {
        ImGui::SetCursorScreenPos(ImVec2(value_x, pos.y));
        ImGui::PushID((id + "_val").c_str());
        ImGui::InvisibleButton("##val", ImVec2(value_w, style.height));
        label_hovered = ImGui::IsItemHovered();
        if (label_hovered && full_value_label != value_label)
            render_panel_tooltip(full_value_label);
        ImGui::PopID();
    }

    return clicked || button_hovered || label_hovered;
}

void AssemblyStepsUtils::render_panel_tooltip(const std::string &text, bool use_dark_style) const
{
    auto& sc = m_imgui_scale;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * sc, 6.0f * sc));
    if (use_dark_style) {
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(38.0f / 255.0f, 46.0f / 255.0f, 48.0f / 255.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    }
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(280.0f * sc);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
    if (use_dark_style)
        ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
}

void AssemblyStepsUtils::render_structure_step_option_menu(
    int card_idx,
    const AssemblyStructureCard& card,
    const ImVec2& anchor,
    float sc,
    bool is_dark)
{
    if (card.node_idx < 0)
        return;

    const std::string popup_id = "##asp_step_option_menu_" + std::to_string(card_idx);
    if (ImGui::IsItemClicked(0)) {
        select_steps_tree_node_for_canvas(card.node_idx);
        ImGui::OpenPopup(popup_id.c_str());
    }

    const std::array<std::string, 4> labels = {
        _u8L("Edit step name"),
        _u8L("Delete step"),
        _u8L("Insert step before"),
        _u8L("Insert step after"),
    };

    const float row_height = 28.0f * sc;
    const float row_spacing = 2.0f * sc;
    const float win_padding = 12.0f * sc;
    const float row_pad_x = 8.0f * sc;
    const float text_right_margin = 8.0f * sc;

    float max_text_width = 0.0f;
    for (const std::string &label : labels)
        max_text_width = std::max(max_text_width, ImGui::CalcTextSize(label.c_str()).x);

    const float menu_width = std::max(168.0f * sc,
        2.0f * win_padding + row_pad_x + max_text_width + text_right_margin);
    const float menu_height = win_padding * 2.0f + row_height * 4.0f + row_spacing * 3.0f;
    ImGui::SetNextWindowPos(ImVec2(anchor.x + 35.0f * sc, anchor.y), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(menu_width, menu_height), ImGuiCond_Always);

    ImGui::PushStyleColor(ImGuiCol_PopupBg, is_dark ? ImVec4(45 / 255.0f, 45 / 255.0f, 49 / 255.0f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 77.0f / 255.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, is_dark ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(38.0f / 255.0f, 46.0f / 255.0f, 48.0f / 255.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 4.0f * sc);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(win_padding, win_padding));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

    if (ImGui::BeginPopup(popup_id.c_str(), ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove)) {
        ImDrawList *draw_list = ImGui::GetWindowDrawList();
        for (int i = 0; i < (int)labels.size(); ++i) {
            ImGui::PushID(i);
            ImVec2 row_pos = ImGui::GetCursorScreenPos();
            const float row_content_w = ImGui::GetContentRegionAvail().x;
            if (ImGui::InvisibleButton("##item", ImVec2(row_content_w, row_height))) {
                if (i == 0) {
                    begin_structure_step_rename(card.node_idx);
                } else if (i == 1) {
                    delete_structure_step(card.node_idx);
                } else if (i == 2) {
                    insert_structure_step_relative(card.node_idx, true);
                } else {
                    insert_structure_step_relative(card.node_idx, false);
                }
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::IsItemHovered()) {
                const ImU32 bg = is_dark ? IM_COL32(55, 55, 59, 255) : IM_COL32(240, 240, 240, 255);
                draw_list->AddRectFilled(row_pos, ImVec2(row_pos.x + row_content_w, row_pos.y + row_height), bg, 4.0f * sc);
            }
            const ImVec2 text_size = ImGui::CalcTextSize(labels[i].c_str());
            draw_list->AddText(ImVec2(row_pos.x + row_pad_x, row_pos.y + (row_height - text_size.y) * 0.5f),
                               ImGui::GetColorU32(ImGuiCol_Text), labels[i].c_str());
            ImGui::PopID();
            if (i != (int)labels.size() - 1)
                ImGui::Dummy(ImVec2(0.0f, row_spacing));
        }
        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(3);
}

void AssemblyStepsUtils::render_assembly_structure_panel(float canvas_w, float canvas_h)
{
    m_assembly_structure_right_x = 0.f;
    if (!m_imgui || !m_model)
        return;
    ImGuiWrapper &imgui = *m_imgui;
    const float   sc    = m_imgui_scale > 0.f ? m_imgui_scale : 1.f;

    AssemblyStructurePanelData data = build_assembly_structure_panel_data();
    if (data.cards.empty())
        return;

    // Font sizes derived from ImGui's active font (same baseline as tree view).
    const float fs       = ImGui::GetFontSize();
    const float fs_title = fs;
    const float fs_chip  = fs * (12.0f / 13.0f);
    const float fs_small = fs * (11.0f / 13.0f);
    const float fs_tiny  = fs * (10.0f / 13.0f);
    const float line_h   = fs * 1.25f;

    // Layout constants -------------------------------------------------------
    const float panel_x        = 12.0f * sc;
    const float panel_y        = 17.0f * sc;
    const float panel_w        = 384.0f * sc;       // 1.5x of original 256*sc
    const float panel_radius   = 4.0f * sc;
    // Header height tracks the title + subtitle text instead of a fixed *sc
    // value, which left a large empty gap under the subtitle on macOS (the imgui
    // font is smaller relative to the Retina scale). Mirrors the draw positions:
    // 6*sc top pad + title line + 4*sc gap + subtitle line + 8*sc bottom pad.
    const float header_h       = 6.0f * sc + fs_title + 4.0f * sc + fs_small + 8.0f * sc;
    const float side_pad       = 8.0f * sc;
    const float card_gap       = 12.0f * sc;
    const float card_pad       = 8.0f * sc;
    const float card_radius    = 4.0f * sc;
    const float tag_h          = 20.0f * sc;
    const float tag_h_pad      = 6.0f * sc;
    const float tag_radius     = 6.0f * sc;
    const float chip_h         = 24.0f * sc;
    const float chip_h_pad     = 10.0f * sc;
    const float chip_gap       = 8.0f * sc;
    const float chip_radius    = 6.0f * sc;
    // Per-card height tracks each card's real content (tag row + title line +
    // an optional chip strip / dashed placeholder row) instead of one shared
    // hard-coded value. A single fixed height left a large empty band at the
    // bottom of cards on macOS (where the imgui font is smaller relative to the
    // Retina DPI scale) and over-reserved a third row for cards that have
    // neither chips nor a placeholder. Deriving it from the very metrics used
    // while drawing keeps every card snug on every platform.
    const float placeholder_row_h = chip_h + 12.0f * sc;
    auto card_h_of = [&](const auto &c) -> float {
        float h = card_pad + tag_h + 8.0f * sc + line_h; // tag row + title row
        if (!c.chips.empty())
            h += 8.0f * sc + chip_h;                     // single chip strip
        else if (!c.placeholder_text.empty())
            h += 8.0f * sc + placeholder_row_h;          // dashed placeholder box
        h += card_pad;
        return h;
    };
    const float action_h       = 34.0f * sc;
    const float bottom_pad     = 16.0f * sc;
    const float icon_sz_hdr    = 16.0f * sc;
    constexpr size_t kChipMaxChars = 20;

    // Colors -----------------------------------------------------------------
    const ImU32 col_white      = m_is_dark ? IM_COL32(55, 55, 59, 255) : IM_COL32(255, 255, 255, 255);
    const ImU32 col_header_top = m_is_dark ? IM_COL32(48, 48, 52, 255) : IM_COL32(0xF8, 0xF8, 0xF8, 255);
    const ImU32 col_header_bot = m_is_dark ? IM_COL32(42, 42, 46, 255) : IM_COL32(0xF1, 0xF1, 0xF1, 255);
    const ImU32 col_text_dark  = m_is_dark ? IM_COL32(0xE0, 0xE0, 0xE0, 255) : IM_COL32(0x26, 0x2E, 0x30, 255);
    const ImU32 col_text_mid   = m_is_dark ? IM_COL32(0xA0, 0xA0, 0xA0, 255) : IM_COL32(0x6B, 0x6B, 0x6B, 255);
    const ImU32 col_text_light = m_is_dark ? IM_COL32(0x80, 0x80, 0x80, 255) : IM_COL32(0xAC, 0xAC, 0xAC, 255);
    const ImU32 col_card_bg    = m_is_dark ? IM_COL32(45, 45, 49, 255) : IM_COL32(0xF8, 0xF8, 0xF8, 255);
    const ImU32 col_card_border= m_is_dark ? IM_COL32(70, 70, 74, 255) : IM_COL32(0xEE, 0xEE, 0xEE, 255);
    const ImU32 col_brand      = IM_COL32(0x00, 0xAE, 0x42, 255);
    const ImU32 col_brand_soft = IM_COL32(0x2C, 0xAD, 0x00, (int) (0.14f * 255.f));
    const ImU32 col_brand_addbg= m_is_dark ? IM_COL32(0x2A, 0x3F, 0x26, 255) : IM_COL32(0xD8, 0xEA, 0xD2, 255);
    const ImU32 col_chip_bg    = m_is_dark ? IM_COL32(65, 65, 69, 255) : IM_COL32(0xEE, 0xEE, 0xEE, 255);

    auto text_w_fn = [&](float size, const std::string &s) {
        return ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.f, s.c_str()).x;
    };

    // Footer hint shown when no step card is selected: editing object pose
    std::string footer_hint_str;
    if (!has_selected_node())
        footer_hint_str = _u8L("Tip") + ":" + _u8L("No step card is currently selected. Pose-changing operations such as move gizmo will affect the actual final-assembly view.");
    else
        footer_hint_str = _u8L("Tip") + ":" + _u8L("Double-click an empty area to exit all editing states.");
    const float footer_hint_wrap = panel_w - 2.0f * side_pad;
    const ImVec2 footer_hint_size = footer_hint_str.empty()
        ? ImVec2(0.0f, 0.0f)
        : ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, footer_hint_wrap,
                                          footer_hint_str.c_str(), nullptr, nullptr);
    const float footer_hint_extra_h = footer_hint_str.empty()
        ? 0.0f
        : (footer_hint_size.y + 8.0f * sc);

    // When collapsed only the header is visible.
    float cards_total = 0.0f;
    for (const auto &c : data.cards)
        cards_total += card_h_of(c) + card_gap;
    const float scroll_content_h = card_gap + cards_total;
    const float max_scroll_region_h = canvas_h * 0.5f;
    const float scroll_region_h_target = std::min(scroll_content_h, max_scroll_region_h);
    const float full_h      = header_h + scroll_region_h_target + action_h + bottom_pad + card_gap + footer_hint_extra_h;
    const float panel_h     = m_structure_panel_collapsed
                                ? header_h
                                : std::min(canvas_h - panel_y - 12.0f * sc, full_h);

    // Window setup -----------------------------------------------------------
    imgui.set_next_window_pos(panel_x, panel_y, ImGuiCond_Always);
    imgui.set_next_window_size(panel_w, panel_h, ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));

    imgui.begin(std::string("##assembly_structure_panel"),
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
                | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
                | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
                | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImDrawList *dl  = ImGui::GetWindowDrawList();
    const ImVec2 win_min = ImGui::GetWindowPos();
    const ImVec2 win_max = ImVec2(win_min.x + panel_w, win_min.y + panel_h);
    ImFont *font = ImGui::GetFont();

    // Outer card (background + soft shadow + rounded corners).
    if (!m_is_dark) {
        dl->AddRectFilled(ImVec2(win_min.x + 2.f, win_min.y + 2.f),
                          ImVec2(win_max.x + 2.f, win_max.y + 2.f),
                          IM_COL32(0, 0, 0, 26), panel_radius);
    }
    dl->AddRectFilled(win_min, win_max, col_white, panel_radius);

    // ---- Header ----------------------------------------------------------
    const ImVec2 hd_min = win_min;
    const ImVec2 hd_max = ImVec2(win_max.x, win_min.y + header_h);
    dl->AddRectFilledMultiColor(hd_min, hd_max, col_header_top, col_header_top,
                                 col_header_bot, col_header_bot);

    // Left icon: collapse / expand toggle (vertically centered on title line).
    const float title_line_cy = win_min.y + 6.f * sc + fs_title * 0.5f;
    {
        const ImVec2 toggle_min(win_min.x + 8.f * sc,
                                title_line_cy - icon_sz_hdr * 0.5f);
        const ImVec2 toggle_max(toggle_min.x + icon_sz_hdr, toggle_min.y + icon_sz_hdr);
        ImTextureID toggle_icon = m_structure_panel_collapsed ? m_panel_expand_icon
                                                              : m_panel_collapse_icon;
        if (toggle_icon)
            dl->AddImage(toggle_icon, toggle_min, toggle_max);
        ImGui::SetCursorScreenPos(toggle_min);
        ImGui::PushID("##asp_toggle");
        ImGui::InvisibleButton("##t", ImVec2(icon_sz_hdr, icon_sz_hdr));
        if (ImGui::IsItemClicked(0))
            m_structure_panel_collapsed = !m_structure_panel_collapsed;
        if (ImGui::IsItemHovered()) {
            dl->AddRectFilled(toggle_min, toggle_max, IM_COL32(38, 46, 48, 18), 3.0f * sc);
            render_panel_tooltip(m_structure_panel_collapsed ? _u8L("Expand") : _u8L("Collapse"));
        }
        ImGui::PopID();
    }

    // Far-right icon: option (tree_option.svg).
    {
        const ImVec2 opt_min(win_max.x - 8.f * sc - icon_sz_hdr,
                             title_line_cy - icon_sz_hdr * 0.5f);
        const ImVec2 opt_max(opt_min.x + icon_sz_hdr, opt_min.y + icon_sz_hdr);
        ImTextureID option_icon = (m_is_dark && m_structure_option_icon_dark) ?
            m_structure_option_icon_dark : m_structure_option_icon;
        if (option_icon)
            dl->AddImage(option_icon, opt_min, opt_max);
        ImGui::SetCursorScreenPos(opt_min);
        ImGui::PushID("##asp_option");
        ImGui::InvisibleButton("##o", ImVec2(icon_sz_hdr, icon_sz_hdr));
        if (ImGui::IsItemClicked(0))
            ImGui::OpenPopup("##assembly_structure_option_menu");
        if (ImGui::IsItemHovered()) {
            dl->AddRectFilled(opt_min, opt_max, IM_COL32(38, 46, 48, 18), 3.0f * sc);
            render_panel_tooltip(_u8L("Options"));
        }
        render_assembly_structure_option_menu(imgui, sc, m_is_dark);
        ImGui::PopID();
    }

    // Title + help icon (view_help.svg right after title text) + subtitle.
    {
        const float title_left = win_min.x + 8.f * sc + icon_sz_hdr + 6.f * sc;
        const float title_y    = win_min.y + 6.f * sc;
        const float title_right = win_max.x - 8.f * sc - icon_sz_hdr - 6.f * sc;
        const float title_max_w = title_right - title_left;

        dl->AddText(font, fs_title,
                    ImVec2(title_left, title_y),
                    col_text_dark, data.title.c_str());

        // Help icon (view_help.svg) immediately after title text.
        {
            const float title_tw = text_w_fn(fs_title, data.title);
            const float help_x = title_left + title_tw + 4.f * sc;
            const float help_y = title_line_cy - icon_sz_hdr * 0.5f;
            const ImVec2 help_min(help_x, help_y);
            const ImVec2 help_max(help_x + icon_sz_hdr, help_y + icon_sz_hdr);
            if (m_structure_help_icon)
                dl->AddImage(m_structure_help_icon, help_min, help_max);
            ImGui::SetCursorScreenPos(help_min);
            ImGui::PushID("##asp_help");
            ImGui::InvisibleButton("##h", ImVec2(icon_sz_hdr, icon_sz_hdr));
            if (ImGui::IsItemClicked(0)) {
                wxLaunchDefaultBrowser("https://e.bambulab.com/t?c=T0HuraoU2gH6ufRk");
            }
            if (ImGui::IsItemHovered()) {
                dl->AddRectFilled(help_min, help_max, IM_COL32(38, 46, 48, 18), 3.0f * sc);
                render_panel_tooltip(_u8L("Go to Wiki"));
            }
            ImGui::PopID();
        }

        // Subtitle: starts at the same x as the collapse icon (left-aligned to panel edge).
        const float sub_left = win_min.x + 8.f * sc;
        const float sub_y    = title_y + fs_title + 4.f * sc;
        const float sub_max_w = panel_w - 16.f * sc;
        const float sub_full_w = text_w_fn(fs_small, data.subtitle);
        if (sub_full_w <= sub_max_w) {
            dl->AddText(font, fs_small,
                        ImVec2(sub_left, sub_y),
                        col_text_mid, data.subtitle.c_str());
        } else {
            std::string clipped = data.subtitle;
            while (!clipped.empty()) {
                std::string trial = clipped + "...";
                if (text_w_fn(fs_small, trial) <= sub_max_w)
                    break;
                size_t i = clipped.size();
                while (i > 0 && (static_cast<unsigned char>(clipped[i - 1]) & 0xC0) == 0x80)
                    --i;
                if (i > 0) --i;
                clipped.resize(i);
            }
            const std::string display_sub = clipped + "...";
            dl->AddText(font, fs_small,
                        ImVec2(sub_left, sub_y),
                        col_text_mid, display_sub.c_str());
            const ImVec2 sub_min_pos(sub_left, sub_y);
            ImGui::SetCursorScreenPos(sub_min_pos);
            ImGui::PushID("##asp_subtitle");
            ImGui::InvisibleButton("##sub", ImVec2(sub_max_w, fs_small + 4.f * sc));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", data.subtitle.c_str());
            ImGui::PopID();
        }
    }

    // When collapsed, only the header is visible.
    if (m_structure_panel_collapsed) {
        imgui.end();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(1);
        m_assembly_structure_right_x = panel_x + panel_w;
        return;
    }

    // ---- Scrollable card region -------------------------------------------
    // The child window only owns the cards stack: header sits above it, the
    const float scroll_region_y = win_min.y + header_h;
    const float scroll_region_h = scroll_region_h_target;
    ImGui::SetCursorScreenPos(ImVec2(win_min.x, scroll_region_y));
    ImGuiWindowFlags scroll_flags = ImGuiWindowFlags_NoBackground;
    if (data.always_show_scrollbar || scroll_content_h > scroll_region_h)
        scroll_flags |= ImGuiWindowFlags_AlwaysVerticalScrollbar;
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 6.0f * sc);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(144 / 255.f, 144 / 255.f, 144 / 255.f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(144 / 255.f, 144 / 255.f, 144 / 255.f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, ImVec4(120 / 255.f, 120 / 255.f, 120 / 255.f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(38 / 255.f, 46 / 255.f, 48 / 255.f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(245 / 255.f, 247 / 255.f, 248 / 255.f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(236 / 255.f, 240 / 255.f, 242 / 255.f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(228 / 255.f, 235 / 255.f, 238 / 255.f, 1.0f));
    ImGui::BeginChild("##asp_cards_scroll", ImVec2(panel_w, scroll_region_h), false, scroll_flags);

    const ImVec2 child_pos = ImGui::GetWindowPos();
    float scroll_y = ImGui::GetScrollY();
    if (m_structure_scroll_to_node >= 0) {
        for (size_t ci = 0; ci < data.cards.size(); ++ci) {
            if (data.cards[ci].node_idx != m_structure_scroll_to_node)
                continue;

            float target_y = card_gap;
            for (size_t k = 0; k < ci; ++k)
                target_y += card_h_of(data.cards[k]) + card_gap;
            const float target_scroll_y = std::max(0.0f, target_y - card_gap);
            ImGui::SetScrollY(target_scroll_y);
            scroll_y = target_scroll_y;

            if (target_y >= scroll_y && target_y + card_h_of(data.cards[ci]) <= scroll_y + scroll_region_h)
                m_structure_scroll_to_node = -1;
            break;
        }
    }
    float cur_y = card_gap;
    ImDrawList *cdl = ImGui::GetWindowDrawList();
    for (size_t ci = 0; ci < data.cards.size(); ++ci) {
        const auto &c = data.cards[ci];
        const float card_x = child_pos.x + side_pad;
        const float card_w = panel_w - 2.f * side_pad;
        const float card_h = card_h_of(c);
        const float card_screen_y = child_pos.y + cur_y - scroll_y;

        const ImVec2 card_min(card_x, card_screen_y);
        const ImVec2 card_max(card_x + card_w, card_screen_y + card_h);
        bool suppress_card_click = false;

        cdl->AddRectFilled(card_min, card_max, c.selected ? col_white : col_card_bg, card_radius);
        cdl->AddRect(card_min, card_max, c.selected ? col_brand : col_card_border, card_radius, 0, 1.0f);

        // Tag pill.
        const ImU32 tag_bg  = (c.tag_style == AssemblyStructureCard::TagStyle::Step) ? col_brand_soft : col_chip_bg;
        const ImU32 tag_txt = (c.tag_style == AssemblyStructureCard::TagStyle::Step) ? col_brand     : col_text_mid;
        const float tag_text_w = text_w_fn(fs_small, c.tag_text);
        const float tag_w      = tag_text_w + 2.f * tag_h_pad;
        const ImVec2 tag_min(card_x + card_pad, card_screen_y + card_pad);
        const ImVec2 tag_max(tag_min.x + tag_w, tag_min.y + tag_h);
        cdl->AddRectFilled(tag_min, tag_max, tag_bg, tag_radius);
        cdl->AddText(font, fs_small,
                    ImVec2(tag_min.x + tag_h_pad, tag_min.y + (tag_h - fs_small) * 0.5f),
                    tag_txt, c.tag_text.c_str());

        // Step option icon (top-right of step cards).
        if (c.show_add_button) {
            const float opt_sz = 20.0f * sc;
            const ImVec2 opt_min(card_max.x - card_pad - opt_sz, tag_min.y);
            const ImVec2 opt_max(opt_min.x + opt_sz, opt_min.y + opt_sz);
            if (m_structure_step_option_icon)
                cdl->AddImage(m_structure_step_option_icon, opt_min, opt_max);
            ImGui::SetCursorScreenPos(opt_min);
            const std::string opt_id = std::string("##asp_step_option_") + std::to_string(ci);
            ImGui::InvisibleButton(opt_id.c_str(), ImVec2(opt_sz, opt_sz));
            if (ImGui::IsItemHovered()) {
                suppress_card_click = true;
                render_panel_tooltip(_u8L("Current step options: edit the current step name, delete the step, or insert a step before/after."));
            }
            render_structure_step_option_menu(static_cast<int>(ci), c, opt_min, sc, m_is_dark);
        }

        // Title row.
        const float title_y      = card_screen_y + card_pad + tag_h + 8.0f * sc;
        const std::string cnt_s  = " (" + std::to_string(c.count) + ")";
        const bool show_select_controls = c.selected && c.node_idx >= 0 &&
            (c.is_final_assembly || c.count > 0);
        const float title_action_sz = 20.0f * sc;
        const float title_action_y = title_y + (fs_title - title_action_sz) * 0.5f;
        const float title_right_anchor = c.show_add_button
            ? (card_max.x - card_pad - title_action_sz)
            : (card_max.x - card_pad);
        const std::string default_select_label = _CTX_utf8(L_CONTEXT("Default", "AssemblyStructure"), "AssemblyStructure");
        // "Select all" collapses the chip text to "Default" (for both the
        const bool select_show_default = c.select_show_default || c.select_label.empty();
        const auto select_chip_label = [&]() -> std::string {
            return select_show_default ? default_select_label : c.select_label;
        };
        // Tooltip text. When the chip shows "Default" (every object/part of this step
        const auto select_tooltip_label = [&]() -> std::string {
            if (select_show_default)
                return _u8L("All parts of the current step are selected");
            return c.select_label;
        };
        const auto select_display_label = [&](const std::string &lbl, float right_anchor) -> std::string {
            const float sel_text_w = text_w_fn(fs_chip, _u8L("Select"));
            const float sel_btn_w = sel_text_w + 2.f * chip_h_pad;
            const float available_value_w = std::max(0.f, right_anchor - chip_gap - sel_btn_w - chip_gap - (card_x + card_pad + 72.0f * sc));
            const float default_value_w = text_w_fn(fs_chip, default_select_label + "   ");
            const float max_value_w = std::min(available_value_w, default_value_w);
            return max_value_w > 0.f
                ? utf8_fit_with_ellipsis(lbl, max_value_w)
                : utf8_truncate_with_ellipsis(lbl, 10);
        };
        const auto calc_select_start_x = [&](float right_anchor) -> float {
            if (!show_select_controls)
                return right_anchor;
            const std::string lbl = select_chip_label();
            const std::string display_lbl = select_display_label(lbl, right_anchor);
            const float sel_text_w = text_w_fn(fs_chip, _u8L("Select"));
            const float sel_btn_w = sel_text_w + 2.f * chip_h_pad;
            const float value_w = text_w_fn(fs_chip, display_lbl);
            const float controls_w = sel_btn_w + chip_gap + value_w;
            return right_anchor - chip_gap - controls_w;
        };
        const float title_control_start_x = calc_select_start_x(title_right_anchor);
        const float title_reserved_w = text_w_fn(fs_title, "M");///MMMMM
        const float title_left_x = card_x + card_pad;
        const float title_max_w = std::max(0.f, title_control_start_x - title_left_x - title_reserved_w);
        const bool title_editing = c.node_idx >= 0 &&
                                   c.node_idx == m_structure_step_rename_node;
        if (title_editing) {
            const float edit_right = (show_select_controls ? title_control_start_x : title_right_anchor) - title_reserved_w;
            const float edit_w = std::max(60.0f * sc, edit_right - title_left_x);
            ImGui::SetCursorScreenPos(ImVec2(card_x + card_pad, title_y - 2.0f * sc));
            ImGui::SetNextItemWidth(edit_w);
            if (m_structure_step_rename_open_pending) {
                ImGui::SetKeyboardFocusHere();
                m_structure_step_rename_open_pending = false;
                m_structure_step_rename_had_focus = false;
            }
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.86f, 0.86f, 0.86f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.82f, 0.82f, 0.82f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.78f, 0.78f, 0.78f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f * sc, 2.0f * sc));
            bool confirmed = ImGui::InputText("##asp_inline_step_name",
                m_structure_step_rename_buf, sizeof(m_structure_step_rename_buf),
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            const ImVec2 edit_min = ImGui::GetItemRectMin();
            const ImVec2 edit_max = ImGui::GetItemRectMax();
            const bool focused = ImGui::IsItemFocused() || ImGui::IsItemActive();
            if (focused)
                m_structure_step_rename_had_focus = true;
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);
            suppress_card_click = true;

            const bool lost_focus = m_structure_step_rename_had_focus && !focused;
            const bool clicked_outside = ImGui::IsMouseClicked(0) &&
                !ImGui::IsMouseHoveringRect(edit_min, edit_max, true);
            if (confirmed || lost_focus || clicked_outside) {
                if (m_structure_step_rename_node >= 0 && m_structure_step_rename_node < (int) _steps_nodes.size() &&
                    m_structure_step_rename_buf[0] != '\0') {
                    _steps_nodes[m_structure_step_rename_node].name = m_structure_step_rename_buf;
                    save_assembly_steps_json_to_model();
                    do_commond_callback("dirty");
                }
                m_structure_step_rename_node = -1;
                m_structure_step_rename_open_pending = false;
                m_structure_step_rename_had_focus = false;
            }
        } else {
            const float cnt_text_w = text_w_fn(fs_tiny, cnt_s);
            const std::string display_title = title_max_w > cnt_text_w
                ? utf8_fit_with_ellipsis(c.title, title_max_w - cnt_text_w)
                : utf8_fit_with_ellipsis(c.title, title_max_w);
            const float title_text_w = text_w_fn(fs_title, display_title);
            cdl->AddText(font, fs_title, ImVec2(card_x + card_pad, title_y),
                        col_text_dark, display_title.c_str());
            cdl->AddText(font, fs_tiny,
                        ImVec2(card_x + card_pad + title_text_w, title_y + (fs_title - fs_tiny)),
                        col_text_dark, cnt_s.c_str());

            if (c.node_idx >= 0) {
                const ImVec2 title_hit_min(card_x + card_pad, title_y - 2.0f * sc);
                const ImVec2 title_hit_size(std::min(card_w - 2.0f * card_pad, title_text_w + cnt_text_w + 8.0f * sc),
                                            fs_title + 6.0f * sc);
                ImGui::SetCursorScreenPos(title_hit_min);
                ImGui::InvisibleButton((std::string("##asp_title_rename_") + std::to_string(ci)).c_str(), title_hit_size);
                const bool title_hovered = ImGui::IsItemHovered();
                if (title_hovered) {
                    suppress_card_click = true;
                    if (display_title != c.title)
                        render_panel_tooltip(c.title);
                }
                const bool title_clicked = ImGui::IsItemClicked(0);
                const bool title_double_clicked = title_hovered && ImGui::IsMouseDoubleClicked(0);
                if (!c.selected && title_clicked) {
                    select_steps_tree_node_for_canvas(c.node_idx);
                    suppress_card_click = true;
                } else if (c.selected && (title_clicked || title_double_clicked)) {
                    begin_structure_step_rename(c.node_idx, c.title);
                    suppress_card_click = true;
                }
            }
        }

        // Add-object button on the second row. Clicking it opens the assembly
        // tree panel outside this card, anchored to the button's right side.
        {
            const float add_sz = title_action_sz;
            const float add_y = title_action_y;
            const float right_anchor = title_right_anchor;
            const ImVec2 add_min(card_max.x - card_pad - add_sz, add_y);
            const ImVec2 add_max(add_min.x + add_sz, add_min.y + add_sz);

            // "Select" button + value label, placed on the title row.
            if (show_select_controls) {
                const std::string lbl = select_chip_label();
                const std::string tooltip_lbl = select_tooltip_label();
                const float sel_text_w = text_w_fn(fs_chip, _u8L("Select"));
                const float sel_btn_w = sel_text_w + 2.f * chip_h_pad;
                const std::string display_lbl = select_display_label(lbl, right_anchor);
                const float value_w = text_w_fn(fs_chip, display_lbl);
                const float controls_w = sel_btn_w + chip_gap + value_w;
                const ImVec2 sel_min(right_anchor - chip_gap - controls_w,
                                     add_y + (add_sz - chip_h) * 0.5f);
                AssemblySelectControlsStyle sel_style;
                sel_style.height          = chip_h;
                sel_style.pad_x           = chip_h_pad;
                sel_style.gap             = chip_gap;
                sel_style.radius          = chip_radius;
                sel_style.font_size       = fs_chip;
                sel_style.bg_col          = col_brand;
                sel_style.button_text_col = col_white;
                sel_style.label_text_col  = col_text_mid;
                if (render_structure_card_select_controls(static_cast<int>(ci), sel_min, sel_style,
                                                          display_lbl, tooltip_lbl)) {
                    suppress_card_click = true;
                }
            }

            if (c.show_add_button) {
                // Render the "+ add object to step" affordance with one of
                const bool add_disabled = !c.selected;
                ImTextureID add_tex = c.selected ? m_structure_step_add_icon_edit : m_structure_step_add_icon_unedit;
                if (add_tex) {
                    const ImU32 add_tint = add_disabled ? IM_COL32(255, 255, 255, 128) : IM_COL32_WHITE;
                    cdl->AddImage(add_tex, add_min, add_max, ImVec2(0, 0), ImVec2(1, 1), add_tint);
                } else {
                    const ImU32 add_bg = add_disabled ? IM_COL32(0xD8, 0xEA, 0xD2, 128) : col_brand_addbg;
                    const ImU32 add_fg = add_disabled ? IM_COL32(0x00, 0xAE, 0x42, 128) : col_brand;
                    cdl->AddRectFilled(add_min, add_max, add_bg, card_radius);
                    const float cx = (add_min.x + add_max.x) * 0.5f;
                    const float cy = (add_min.y + add_max.y) * 0.5f;
                    const float arm = 5.f * sc;
                    cdl->AddLine(ImVec2(cx - arm, cy), ImVec2(cx + arm, cy), add_fg, 1.5f);
                    cdl->AddLine(ImVec2(cx, cy - arm), ImVec2(cx, cy + arm), add_fg, 1.5f);
                }
                ImGui::SetCursorScreenPos(add_min);
                const std::string add_id = std::string("##asp_add_") + std::to_string(ci);
                imgui.disabled_begin(add_disabled);
                ImGui::InvisibleButton(add_id.c_str(), ImVec2(add_sz, add_sz));
                if (ImGui::IsItemHovered()) {
                    suppress_card_click = true;
                    render_panel_tooltip(_u8L("Add some objects to current step"));
                }
                if (ImGui::IsItemClicked() && c.node_idx >= 0) {//popup add object tree
                    open_structure_add_tree(static_cast<int>(ci), c.node_idx, ImVec2(add_max.x + 8.0f * sc, add_min.y));
                }
                imgui.disabled_end();
            }
        }
        // Single-row chip strip (clipped to one line, 20 char ellipsis + hover).
        const float chip_top_y = title_y + line_h + 8.0f * sc;
        if (!c.chips.empty()) {
            float chip_x = card_x + card_pad;
            const float chip_y = chip_top_y;
            const float row_max_x = card_max.x - card_pad;

            if (!c.prefix_text.empty()) {
                const float prefix_w = text_w_fn(fs_chip, c.prefix_text);
                cdl->AddText(font, fs_chip,
                            ImVec2(chip_x, chip_y + (chip_h - fs_chip) * 0.5f),
                            col_text_light, c.prefix_text.c_str());
                chip_x += prefix_w + chip_gap;
            }

            auto draw_chip = [&](const std::string& label, const std::string& tooltip,
                                 size_t id_idx, float w) {
                cdl->AddRectFilled(ImVec2(chip_x, chip_y),
                                  ImVec2(chip_x + w, chip_y + chip_h),
                                  col_chip_bg, chip_radius);
                cdl->AddText(font, fs_chip,
                            ImVec2(chip_x + chip_h_pad,
                                   chip_y + (chip_h - fs_chip) * 0.5f),
                            col_text_mid, label.c_str());

                ImGui::SetCursorScreenPos(ImVec2(chip_x, chip_y));
                const std::string chip_id = std::string("##asp_chip_") + std::to_string(ci) + "_" + std::to_string(id_idx);
                ImGui::InvisibleButton(chip_id.c_str(), ImVec2(w, chip_h));
                if (!tooltip.empty() && ImGui::IsItemHovered()) {
                    suppress_card_click = true;
                    render_panel_tooltip(tooltip);
                }

                chip_x += w + chip_gap;
            };

            struct ChipDrawItem {
                std::string label;
                std::string tooltip;
                size_t id_idx{0};
                float width{0.0f};
            };
            std::vector<ChipDrawItem> chip_items;
            const float row_start_x = chip_x;
            const std::string ellipsis = "...";
            const float ellipsis_w = text_w_fn(fs_chip, ellipsis) + 2.f * chip_h_pad;
            auto join_hidden_labels = [&](size_t begin_idx) {
                std::string hidden_labels;
                for (size_t hi = begin_idx; hi < c.chips.size(); ++hi) {
                    if (!hidden_labels.empty())
                        hidden_labels += "\n";
                    hidden_labels += c.chips[hi].label;
                }
                return hidden_labels;
            };
            auto make_fitted_label = [&](const std::string& label, float available_w) {
                const float text_max_w = std::max(0.0f, available_w - 2.f * chip_h_pad);
                return utf8_fit_with_ellipsis(label, text_max_w);
            };
            auto used_width = [&]() {
                float width = 0.0f;
                for (const ChipDrawItem& item : chip_items)
                    width += item.width;
                if (!chip_items.empty())
                    width += chip_gap * static_cast<float>(chip_items.size() - 1);
                return width;
            };

            for (size_t i = 0; i < c.chips.size(); ++i) {
                const std::string &full_label    = c.chips[i].label;
                const std::string  display_label = utf8_truncate_with_ellipsis(full_label, kChipMaxChars);
                const bool         truncated     = (display_label.size() != full_label.size());
                const float        w             = text_w_fn(fs_chip, display_label) + 2.f * chip_h_pad;
                const float next_right = row_start_x + used_width() + (chip_items.empty() ? 0.0f : chip_gap) + w;
                if (next_right <= row_max_x) {
                    chip_items.push_back({display_label, truncated ? full_label : std::string(), i, w});
                    continue;
                }

                size_t hidden_begin_idx = i;
                if (!chip_items.empty()) {
                    while (!chip_items.empty()) {
                        ChipDrawItem& last_item = chip_items.back();
                        const float used_without_last = used_width() - last_item.width -
                            (chip_items.size() > 1 ? chip_gap : 0.0f);
                        const float gap_before_last = chip_items.size() > 1 ? chip_gap : 0.0f;
                        const float available_for_last = row_max_x - row_start_x - used_without_last -
                            gap_before_last - chip_gap - ellipsis_w;
                        if (available_for_last >= ellipsis_w) {
                            const std::string fitted = make_fitted_label(c.chips[last_item.id_idx].label,
                                                                         available_for_last);
                            const float fitted_w = text_w_fn(fs_chip, fitted) + 2.f * chip_h_pad;
                            if (fitted != c.chips[last_item.id_idx].label && fitted_w <= available_for_last) {
                                last_item.label = fitted;
                                last_item.tooltip = c.chips[last_item.id_idx].label;
                                last_item.width = fitted_w;
                            }
                            break;
                        }
                        hidden_begin_idx = std::min(hidden_begin_idx, last_item.id_idx);
                        chip_items.pop_back();
                    }
                }

                const std::string hidden_labels = join_hidden_labels(hidden_begin_idx);
                if (!hidden_labels.empty() &&
                    row_start_x + used_width() + (chip_items.empty() ? 0.0f : chip_gap) + ellipsis_w <= row_max_x)
                    chip_items.push_back({ellipsis, hidden_labels, c.chips.size(), ellipsis_w});
                break;
            }

            for (const ChipDrawItem& item : chip_items)
                draw_chip(item.label, item.tooltip, item.id_idx, item.width);
        } else if (!c.placeholder_text.empty()) {
            const float ph_h_local = chip_h + 12.0f * sc;
            const ImVec2 ph_min(card_x + card_pad, chip_top_y);
            const ImVec2 ph_max(card_max.x - card_pad, chip_top_y + ph_h_local);
            cdl->AddRectFilled(ph_min, ph_max, col_card_bg, card_radius);
            const float dash_len = 4.f * sc;
            const float gap_len  = 3.f * sc;
            auto draw_dashed = [&](ImVec2 a, ImVec2 b) {
                ImVec2 d(b.x - a.x, b.y - a.y);
                float  L = std::sqrt(d.x * d.x + d.y * d.y);
                if (L < 1e-3f) return;
                ImVec2 n(d.x / L, d.y / L);
                for (float t = 0.f; t < L; t += dash_len + gap_len) {
                    float t1 = std::min(L, t + dash_len);
                    cdl->AddLine(ImVec2(a.x + n.x * t, a.y + n.y * t),
                                ImVec2(a.x + n.x * t1, a.y + n.y * t1),
                                col_card_border, 1.f);
                }
            };
            draw_dashed(ph_min, ImVec2(ph_max.x, ph_min.y));
            draw_dashed(ImVec2(ph_max.x, ph_min.y), ph_max);
            draw_dashed(ph_max, ImVec2(ph_min.x, ph_max.y));
            draw_dashed(ImVec2(ph_min.x, ph_max.y), ph_min);
            const float ph_text_w = text_w_fn(fs_chip, c.placeholder_text);
            cdl->AddText(font, fs_chip,
                        ImVec2((ph_min.x + ph_max.x - ph_text_w) * 0.5f,
                               (ph_min.y + ph_max.y - fs_chip) * 0.5f),
                        col_text_light, c.placeholder_text.c_str());

            if (c.selected && c.node_idx >= 0) {
                ImGui::SetCursorScreenPos(ph_min);
                const std::string ph_id = std::string("##asp_placeholder_add_") + std::to_string(ci);
                ImGui::InvisibleButton(ph_id.c_str(), ImVec2(ph_max.x - ph_min.x, ph_max.y - ph_min.y));
                /*if (ImGui::IsItemHovered()) {
                    suppress_card_click = true;
                    render_panel_tooltip(_u8L("Add object to current step"));
                }*/
                if (ImGui::IsItemClicked()) {
                    open_structure_add_tree(static_cast<int>(ci), c.node_idx, ImVec2(ph_max.x + 8.0f * sc, ph_min.y));
                    suppress_card_click = true;
                }
            }
        }

        const bool block_card_click = m_structure_select_popup_active_card >= 0 ||
                                      m_structure_select_popup_pending_card >= 0 ||
                                      m_structure_add_tree_card >= 0;
        if (!suppress_card_click && !block_card_click &&
            ImGui::IsMouseHoveringRect(card_min, card_max, true) && ImGui::IsMouseClicked(0)) {
            int click_node = c.node_idx;
            if (click_node < 0 && c.tag_style == AssemblyStructureCard::TagStyle::Default) {
                click_node = ensure_final_assembly_folder();
            }
            if (click_node >= 0) {
                select_steps_tree_node_for_canvas(click_node);
            }
        }

        cur_y += card_h + card_gap;
    }
    ImGui::SetCursorPosY(scroll_content_h);
    ImGui::Dummy(ImVec2(1.0f, 1.0f));
    ImGui::EndChild();
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(1);

    if (m_structure_add_tree_card >= 0 &&
        m_structure_add_tree_card < static_cast<int>(data.cards.size())) {
        const float tree_w = 300.0f * sc;
        const float tree_h = std::min(420.0f * sc, std::max(180.0f * sc, canvas_h - m_structure_add_tree_pos.y - 12.0f * sc));
        const float tree_x = panel_x + panel_w + 10.0f * sc;
        render_assembly_tree_ui(tree_x, m_structure_add_tree_pos.y, tree_w, tree_h, sc);
    }

    // Popup tree selector for the selected card (rendered outside the child window).
    if (m_model) {
        const int popup_card = m_structure_select_popup_pending_card >= 0
            ? m_structure_select_popup_pending_card
            : m_structure_select_popup_active_card;
        if (popup_card >= 0 && popup_card < static_cast<int>(data.cards.size())) {
            const int popup_step_node = data.cards[popup_card].node_idx;
            if (m_structure_select_popup_tree_card != popup_card ||
                m_structure_select_popup_tree_step_node != popup_step_node) {
                m_structure_select_popup_tree = build_structure_card_select_tree_data(popup_step_node);
                m_structure_select_popup_tree_card = popup_card;
                m_structure_select_popup_tree_step_node = popup_step_node;
                m_structure_select_popup_checked_card = -1;
            }
            render_structure_card_select_popup(popup_card, &m_structure_select_popup_tree);
        }
    }

    // ---- Action buttons --------------------------------------------------
    {
        const float btn_w   = 86.0f * sc;
        const float btn_gap = 12.0f * sc;
        const float btn_pad_x = 14.0f * sc;
        const float copy_w = std::max(btn_w, ImGui::CalcTextSize(_u8L("Copy Step").c_str()).x + 2.0f * btn_pad_x);
        const float add_w  = std::max(btn_w, ImGui::CalcTextSize(_u8L("Add Step").c_str()).x + 2.0f * btn_pad_x);
        const float total_w = copy_w + btn_gap + add_w;
        const float bx0     = win_min.x + (panel_w - total_w) * 0.5f;
        // Anchor the button row to the bottom of the scrollable card region
        const float by      = scroll_region_y + scroll_region_h + card_gap;
        const ImVec2 copy_btn_size(copy_w, action_h);
        const ImVec2 add_btn_size(add_w, action_h);
        const int selected_folder = find_parent_folder(m_selected_node);
        const bool selected_final_assembly = selected_folder >= 0 && selected_folder < (int) _steps_nodes.size() && _steps_nodes[selected_folder].is_final_assembly;
        // can_add_non_final_assembly_step() also caches the limit state into
        // m_non_final_assembly_step_limit_reached, which the tooltips read below.
        const bool reached_step_limit = !can_add_non_final_assembly_step();
        const bool copy_disabled = selected_folder < 0 || selected_final_assembly || reached_step_limit;
        const bool add_disabled  = reached_step_limit;
        // Extra warning appended when the step cap is hit. The cap counts non-final
        // steps (MAX) plus the single final-assembly step, hence MAX + 1 total.
        const std::string step_over_limit_tip = (boost::format(_u8L("No more than %1% steps are allowed.")) % (MAX_NON_FINAL_ASSEMBLY_STEPS + 1)).str();

        imgui.disabled_begin(copy_disabled);
        if (render_footer_button("##asp_btn_copy", _u8L("Copy Step"), ImVec2(bx0, by), copy_btn_size, false, sc))
            copy_assembly_step();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            std::string copy_tip = _u8L("Only copy steps. This is independent of the selected objects on the canvas.");
            if (m_non_final_assembly_step_limit_reached)
                copy_tip += "\n" + step_over_limit_tip;
            render_panel_tooltip(copy_tip);
        }
        imgui.disabled_end();

        imgui.disabled_begin(add_disabled);
        if (render_footer_button("##asp_btn_add", _u8L("Add Step"), ImVec2(bx0 + copy_w + btn_gap, by), add_btn_size, true, sc)) {
            add_assembly_step();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            std::string add_tip = _u8L("Only add empty steps. This has nothing to do with the selected objects on the canvas.");
            if (m_non_final_assembly_step_limit_reached)
                add_tip += "\n" + step_over_limit_tip;
            render_panel_tooltip(add_tip);
        }
        imgui.disabled_end();

        // Footer hint (no-step-selected variant). Drawn directly via the
        // window draw list so we can wrap-render at the panel font size
        // without disturbing the centered button row above. Position is
        // immediately under the Copy/Add button row, with the wrap height
        // already accounted for in full_h above.
        if (!footer_hint_str.empty()) {
            const float hint_y = by + action_h + 8.0f * sc;
            dl->AddText(ImGui::GetFont(), fs,
                ImVec2(win_min.x + side_pad, hint_y),
                col_text_mid, footer_hint_str.c_str(), nullptr, footer_hint_wrap);
        }
    }

    imgui.end();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(1);

    m_assembly_structure_right_x = panel_x + panel_w;
    m_panel_rect_structure_min = ImVec2(panel_x, panel_y);
    m_panel_rect_structure_max = ImVec2(panel_x + panel_w, panel_y + panel_h);

    if (!m_save_project_tip_text.empty()) {
        if (std::chrono::steady_clock::now() >= m_save_project_tip_until) {
            m_save_project_tip_text.clear();
        } else {
            const ImVec2 padding(10.0f * sc, 7.0f * sc);
            const ImVec2 text_size = ImGui::CalcTextSize(m_save_project_tip_text.c_str());
            const ImVec2 tip_pos(panel_x + panel_w + 10.0f * sc, panel_y + 8.0f * sc);
            const ImVec2 tip_size(text_size.x + padding.x * 2.0f, text_size.y + padding.y * 2.0f);
            ImGui::SetNextWindowPos(tip_pos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(tip_size, ImGuiCond_Always);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f * sc);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f * sc);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 0.98f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.82f, 0.82f, 0.82f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(38 / 255.0f, 46 / 255.0f, 48 / 255.0f, 1.0f));
            ImGui::Begin("##assembly_save_project_tip", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs);
            ImGui::TextUnformatted(m_save_project_tip_text.c_str());
            ImGui::End();
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(3);
        }
    }
}

void AssemblyStepsUtils::render_structure_card_select_popup(int card_idx,
                                                            const AssemblyTreeData *popup_tree_ptr)
{
    if (!m_imgui || !popup_tree_ptr || popup_tree_ptr->empty())
        return;
    const float sc = m_imgui_scale;

    const std::string popup_id = "##asp_select_popup_" + std::to_string(card_idx);

    if (m_structure_select_popup_pending_card == card_idx) {
        ImGui::OpenPopup(popup_id.c_str());
        m_structure_select_popup_active_card  = card_idx;
        m_structure_select_popup_pending_card = -1;

        // Start each popup session with a clean search box.
        m_assembly_tree_search_active        = false;
        m_assembly_tree_search_focus_pending = false;
        m_assembly_tree_search_text.clear();

        // Force-seed every node's open state on each popup open. The cache
        if (m_model && m_structure_select_popup_tree_step_node >= 0) {
            const auto &step_nodes = m_model->get_assembly_steps_tree_data().nodes;
            const bool  is_final   = m_structure_select_popup_tree_step_node < static_cast<int>(step_nodes.size()) &&
                step_nodes[m_structure_select_popup_tree_step_node].is_final_assembly;
            if (is_final) {
                for (const auto &node : popup_tree_ptr->nodes)
                    s_assembly_tree_open_nodes[node.uid] = (node.parent_id == -1);
            } else {
                for (const auto &node : popup_tree_ptr->nodes)
                    s_assembly_tree_open_nodes[node.uid] = true;
            }
        }

        // Every time the user opens the Select popup AND a step tree node is
        if (m_selected_node >= 0)
            apply_canvas_selection_to_popup_checked(card_idx, *popup_tree_ptr);
    }

    if (m_structure_select_popup_active_card != card_idx)
        return;

    if (m_structure_select_popup_checked_card != card_idx) {
        m_structure_select_popup_checked.clear();
        for (const auto& node : popup_tree_ptr->nodes) {
            if (node.selectable)
                m_structure_select_popup_checked[node.uid] = true;
        }
        m_structure_select_popup_checked_card = card_idx;
        update_structure_select_label(card_idx, *popup_tree_ptr);
        sync_structure_select_popup_to_canvas(*popup_tree_ptr);
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f * sc);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * sc, 10.0f * sc));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 6.0f * sc);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, m_is_dark ? ImVec4(45 / 255.0f, 45 / 255.0f, 49 / 255.0f, 0.98f) : ImVec4(1.0f, 1.0f, 1.0f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Text, m_is_dark ? ImVec4(0xE0 / 255.0f, 0xE0 / 255.0f, 0xE0 / 255.0f, 1.0f) : ImVec4(38 / 255.0f, 46 / 255.0f, 48 / 255.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(144 / 255.f, 144 / 255.f, 144 / 255.f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(144 / 255.f, 144 / 255.f, 144 / 255.f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, ImVec4(120 / 255.f, 120 / 255.f, 120 / 255.f, 1.0f));

    const bool is_final_select_popup = m_model &&
        m_structure_select_popup_tree_step_node >= 0 &&
        m_structure_select_popup_tree_step_node < static_cast<int>(m_model->get_assembly_steps_tree_data().nodes.size()) &&
        m_model->get_assembly_steps_tree_data().nodes[m_structure_select_popup_tree_step_node].is_final_assembly;

    const float header_h_calc   = 28.0f * sc;
    const float row_h_calc      = 36.0f * sc;
    const float window_pad_v    = 10.0f * sc * 2.0f;
    int visible_row_est;
    if (is_final_select_popup) {
        visible_row_est = static_cast<int>(popup_tree_ptr->roots.size());
        for (int root_idx : popup_tree_ptr->roots)
            visible_row_est += static_cast<int>(popup_tree_ptr->nodes[root_idx].children.size());
    } else {
        visible_row_est = static_cast<int>(popup_tree_ptr->nodes.size());
    }
    const int   row_count_est   = std::max(visible_row_est, 1);
    const float popup_h_dynamic = window_pad_v + header_h_calc + row_count_est * row_h_calc + 8.0f * sc;
    const float popup_h_max     = 360.0f * sc;
    const float popup_w         = 260.0f * sc;
    const float popup_h         = std::min(popup_h_dynamic, popup_h_max);
    ImGui::SetNextWindowSize(ImVec2(popup_w, popup_h), ImGuiCond_Always);

    if (ImGui::BeginPopup(popup_id.c_str())) {
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_ChildWindows)) {
            ImGuiIO &io = ImGui::GetIO();
            io.WantCaptureMouse = true;
        }

        // Title row: "Select" / search box on the left, search toggle then the
        {
            const ImVec2 header_min      = ImGui::GetCursorScreenPos();
            const float  header_w        = ImGui::GetContentRegionAvail().x;
            const float  header_h        = header_h_calc;
            const float  icon_sz         = 18.0f * sc;        // close (cross) icon
            const float  search_icon_sz  = 16.0f * sc;
            const float  search_h        = std::min(header_h, 24.0f * sc);
            ImDrawList  *dl              = ImGui::GetWindowDrawList();

            // Make sure the search glyph is available before the tree selector
            load_assembly_tree_icons(sc);

            // Close (cross) icon stays anchored at the far right in both states.
            const ImVec2 close_min(header_min.x + header_w - icon_sz,
                                   header_min.y + (header_h - icon_sz) * 0.5f);

            if (m_assembly_tree_search_active) {
                const ImVec2 search_min(header_min.x, header_min.y + (header_h - search_h) * 0.5f);
                const ImVec2 search_max(close_min.x - 8.0f * sc, search_min.y + search_h);
                dl->AddRectFilled(search_min, search_max, m_is_dark ? IM_COL32(58, 58, 62, 255) : IM_COL32(248, 248, 248, 255), 12.0f * sc);
                dl->AddRect(search_min, search_max, m_is_dark ? IM_COL32(78, 78, 82, 255) : IM_COL32(238, 238, 238, 255), 12.0f * sc);
                const ImVec2 sicon_min(search_min.x + 8.0f * sc, search_min.y + (search_h - search_icon_sz) * 0.5f);
                ImTextureID search_tex = m_is_dark && s_assembly_tree_icons.search_dark ? s_assembly_tree_icons.search_dark : s_assembly_tree_icons.search;
                if (search_tex)
                    dl->AddImage(search_tex, sicon_min,
                        ImVec2(sicon_min.x + search_icon_sz, sicon_min.y + search_icon_sz));
                // Clicking the leading icon area exits search and clears the filter.
                ImGui::SetCursorScreenPos(search_min);
                ImGui::InvisibleButton("##asp_select_search_close", ImVec2(30.0f * sc, search_h));
                if (ImGui::IsItemClicked(0)) {
                    m_assembly_tree_search_active        = false;
                    m_assembly_tree_search_focus_pending = false;
                    m_assembly_tree_search_text.clear();
                }

                // Vertically center the input frame inside the rounded search
                // box. Using a fixed offset made the text sit too low because the
                // input frame height (font + 2*padding) is not constant across DPI.
                const float input_pad_y   = 3.0f * sc;
                const float input_frame_h = ImGui::GetFontSize() + 2.0f * input_pad_y;
                ImGui::SetCursorScreenPos(ImVec2(search_min.x + 30.0f * sc,
                                                 search_min.y + std::max(0.0f, (search_h - input_frame_h) * 0.5f)));
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, input_pad_y));
                ImGui::SetNextItemWidth(std::max(0.0f, search_max.x - search_min.x - 36.0f * sc));
                if (m_assembly_tree_search_focus_pending) {
                    ImGui::SetKeyboardFocusHere();
                    m_assembly_tree_search_focus_pending = false;
                }
                ImGui::InputTextWithHint("##asp_select_search", _u8L("Search").c_str(), &m_assembly_tree_search_text);
                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor(4);
            } else {
                const std::string title      = _u8L("Select");
                const ImVec2      title_size = ImGui::CalcTextSize(title.c_str());
                dl->AddText(ImVec2(header_min.x, header_min.y + (header_h - title_size.y) * 0.5f),
                    m_is_dark ? IM_COL32(0xE0, 0xE0, 0xE0, 255) : IM_COL32(38, 46, 48, 255), title.c_str());

                // Search icon sits just to the left of the close icon.
                const ImVec2 sicon_min(close_min.x - 8.0f * sc - search_icon_sz,
                                       header_min.y + (header_h - search_icon_sz) * 0.5f);
                ImTextureID search_tex = m_is_dark && s_assembly_tree_icons.search_dark ? s_assembly_tree_icons.search_dark : s_assembly_tree_icons.search;
                if (search_tex)
                    dl->AddImage(search_tex, sicon_min,
                        ImVec2(sicon_min.x + search_icon_sz, sicon_min.y + search_icon_sz));
                ImGui::SetCursorScreenPos(sicon_min);
                ImGui::InvisibleButton("##asp_select_search_open", ImVec2(search_icon_sz, search_icon_sz));
                if (ImGui::IsItemHovered()) {
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * sc, 6.0f * sc));
                    m_imgui->tooltip(_u8L("Search"), 20.0f * m_imgui->scaled(1.0f));
                    ImGui::PopStyleVar();
                }
                if (ImGui::IsItemClicked(0)) {
                    m_assembly_tree_search_active        = true;
                    m_assembly_tree_search_focus_pending = true;
                }
            }

            // Close (cross) icon - always present, far right.
            ImTextureID cross_tex = m_is_dark && m_tree_icon_cross_dark ? m_tree_icon_cross_dark : m_tree_icon_cross;
            if (cross_tex) {
                dl->AddImage(cross_tex, close_min,
                    ImVec2(close_min.x + icon_sz, close_min.y + icon_sz));
                ImGui::SetCursorScreenPos(close_min);
                ImGui::InvisibleButton("##asp_select_popup_close", ImVec2(icon_sz, icon_sz));
                if (ImGui::IsItemClicked(0))
                    ImGui::CloseCurrentPopup();
            }
            ImGui::SetCursorScreenPos(ImVec2(header_min.x, header_min.y + header_h));
        }

        AssemblyTreeRenderOptions options;
        options.allow_object_check = true;
        options.allow_volume_check = true;
        options.show_footer = false;
        options.readonly = false;
        options.child_id = "##structure_select_tree_nodes";
        AssemblyTreeRenderResult result = render_assembly_tree_selector(*popup_tree_ptr, m_structure_select_popup_checked, options, sc);
        if (result.changed) {
            update_structure_select_label(card_idx, *popup_tree_ptr);
            sync_structure_select_popup_to_canvas(*popup_tree_ptr);
            do_commond_callback("update_gizmos_on_off_state");
        }

        ImGui::EndPopup();
    }
    else {
        m_structure_select_popup_active_card = -1;
        // Drop the shared search filter so it does not leak into the Assembly
        m_assembly_tree_search_active        = false;
        m_assembly_tree_search_focus_pending = false;
        m_assembly_tree_search_text.clear();
    }

    ImGui::PopStyleColor(11);
    ImGui::PopStyleVar(3);
}

void AssemblyStepsUtils::init_tree_icons()
{
    if (m_tree_icons_loaded)
        return;

    const unsigned icon_sz = 64;
    IMTexture::load_from_svg_file(m_images_dir + "tree_play.svg",      icon_sz, icon_sz, m_tree_icon_play);
    IMTexture::load_from_svg_file(m_images_dir + "tree_play_dark.svg", icon_sz, icon_sz, m_tree_icon_play_dark);
    IMTexture::load_from_svg_file(m_images_dir + "tree_pause.svg", icon_sz, icon_sz, m_tree_icon_pause);
    IMTexture::load_from_svg_file(m_images_dir + "tree_apply_camera.svg", icon_sz, icon_sz, m_tree_icon_apply_camera);
    IMTexture::load_from_svg_file(m_images_dir + "tree_apply_camera_dark.svg", icon_sz, icon_sz, m_tree_icon_apply_camera_dark);
    IMTexture::load_from_svg_file(m_images_dir + "tree_explosion.svg", icon_sz, icon_sz, m_tree_icon_auto_explode);
    IMTexture::load_from_svg_file(m_images_dir + "tree_explosion_dark.svg", icon_sz, icon_sz, m_tree_icon_auto_explode_dark);
    IMTexture::load_from_svg_file(m_images_dir + "tree_object.svg", icon_sz, icon_sz, m_tree_icon_object);
    IMTexture::load_from_svg_file(m_images_dir + "tree_part.svg", icon_sz, icon_sz, m_tree_icon_part);

    IMTexture::load_from_svg_file(m_images_dir + "tree_screw.svg",      icon_sz, icon_sz, m_tree_icon_screw);
    IMTexture::load_from_svg_file(m_images_dir + "tree_screw_dark.svg", icon_sz, icon_sz, m_tree_icon_screw_dark);
    IMTexture::load_from_svg_file(m_images_dir + "tree_glue.svg",       icon_sz, icon_sz, m_tree_icon_glue);
    IMTexture::load_from_svg_file(m_images_dir + "tree_glue_dark.svg",  icon_sz, icon_sz, m_tree_icon_glue_dark);
    IMTexture::load_from_svg_file(m_images_dir + "tree_clip.svg",       icon_sz, icon_sz, m_tree_icon_clip);
    IMTexture::load_from_svg_file(m_images_dir + "tree_clip_dark.svg",  icon_sz, icon_sz, m_tree_icon_clip_dark);
    IMTexture::load_from_svg_file(m_images_dir + "tree_rect.svg",        icon_sz, icon_sz, m_note_icon_rect);
    IMTexture::load_from_svg_file(m_images_dir + "tree_rect_dark.svg",   icon_sz, icon_sz, m_note_icon_rect_dark);
    IMTexture::load_from_svg_file(m_images_dir + "tree_circle.svg",      icon_sz, icon_sz, m_note_icon_circle);
    IMTexture::load_from_svg_file(m_images_dir + "tree_circle_dark.svg", icon_sz, icon_sz, m_note_icon_circle_dark);
    IMTexture::load_from_svg_file(m_images_dir + "tree_line.svg",   icon_sz, icon_sz, m_note_icon_line);
    IMTexture::load_from_svg_file(m_images_dir + "tree_vector.svg",      icon_sz, icon_sz, m_note_icon_vector);
    IMTexture::load_from_svg_file(m_images_dir + "tree_vector_dark.svg", icon_sz, icon_sz, m_note_icon_vector_dark);
    IMTexture::load_from_svg_file(m_images_dir + "tree_note.svg",        icon_sz, icon_sz, m_note_icon_tag);
    IMTexture::load_from_svg_file(m_images_dir + "tree_note_dark.svg",   icon_sz, icon_sz, m_note_icon_tag_dark);
    IMTexture::load_from_svg_file(m_images_dir + "tree_pencil.svg", icon_sz, icon_sz, m_note_icon_pencil);
    IMTexture::load_from_svg_file(m_images_dir + "tree_frame.svg",       icon_sz, icon_sz, m_tree_icon_frame);
    IMTexture::load_from_svg_file(m_images_dir + "cross.svg",       icon_sz, icon_sz, m_tree_icon_cross);
    IMTexture::load_from_svg_file(m_images_dir + "cross_dark.svg",  icon_sz, icon_sz, m_tree_icon_cross_dark);
    IMTexture::load_from_svg_file(m_images_dir + "panel_collapse.svg", icon_sz, icon_sz, m_panel_collapse_icon);
    IMTexture::load_from_svg_file(m_images_dir + "panel_expand.svg", icon_sz, icon_sz, m_panel_expand_icon);
    IMTexture::load_from_svg_file(m_images_dir + "view_help.svg", icon_sz, icon_sz, m_structure_help_icon);
    IMTexture::load_from_svg_file(m_images_dir + "tree_option.svg", icon_sz, icon_sz, m_structure_option_icon);
    IMTexture::load_from_svg_file(m_images_dir + "tree_option_dark.svg", icon_sz, icon_sz, m_structure_option_icon_dark);
    IMTexture::load_from_svg_file(m_images_dir + "tree_step_option.svg", icon_sz, icon_sz, m_structure_step_option_icon);
    IMTexture::load_from_svg_file(m_images_dir + "tree_unedit.svg",   icon_sz, icon_sz, m_structure_step_add_icon_unedit);
    IMTexture::load_from_svg_file(m_images_dir + "tree_cur_edit.svg", icon_sz, icon_sz, m_structure_step_add_icon_edit);
    IMTexture::load_from_svg_file(m_images_dir + "tree_from_assembly_end_frame.svg", icon_sz, icon_sz, m_tree_icon_from_assembly_end_frame);
    IMTexture::load_from_svg_file(m_images_dir + "tree_from_assembly_end_frame_dark.svg", icon_sz, icon_sz, m_tree_icon_from_assembly_end_frame_dark);
    IMTexture::load_from_svg_file(m_images_dir + "tree_export.svg", icon_sz, icon_sz, m_btn_icon_export);
    IMTexture::load_from_svg_file(m_images_dir + "play_left.svg",   icon_sz, icon_sz, m_play_left_icon);
    IMTexture::load_from_svg_file(m_images_dir + "play_right.svg",  icon_sz, icon_sz, m_play_right_icon);
    load_assembly_tree_icons(m_imgui_scale > 0.0f ? m_imgui_scale : 1.0f);
    m_tree_icons_loaded = true;
}

ImTextureID AssemblyStepsUtils::get_arrow_svg_icon(const std::string &svg_name)
{
    // These icons are drawn by draw_arrow_svg_icon() onto a box whose background is
    if (svg_name == "screw") return m_tree_icon_screw;
    if (svg_name == "glue") return m_tree_icon_glue;
    if (svg_name == "clip") return m_tree_icon_clip;
    auto it = m_arrow_svg_icons.find(svg_name);
    if (it != m_arrow_svg_icons.end())
        return it->second;

    ImTextureID tex = nullptr;
    IMTexture::load_from_svg_file(m_images_dir + svg_name + ".svg", 64, 64, tex);
    m_arrow_svg_icons[svg_name] = tex;
    return tex;
}

AssemblyTreeRenderResult AssemblyStepsUtils::render_assembly_tree_selector(
    const AssemblyTreeData& tree,
    std::unordered_map<std::string, bool>& checked,
    const AssemblyTreeRenderOptions& options,
    float sc)
{
    AssemblyTreeRenderResult result;
    if (tree.nodes.empty())
        return result;

    load_assembly_tree_icons(sc);

    const ImU32 text_col      = m_is_dark ? IM_COL32(0xE0, 0xE0, 0xE0, 255) : IM_COL32(38, 46, 48, 255);
    const ImU32 sub_text_col  = m_is_dark ? IM_COL32(0x90, 0x90, 0x90, 255) : IM_COL32(144, 144, 144, 255);
    const ImU32 green_col     = IM_COL32(0, 174, 66, 255);
    const ImU32 line_col      = m_is_dark ? IM_COL32(80, 80, 84, 255)  : IM_COL32(209, 213, 216, 255);
    const ImU32 border_col    = m_is_dark ? IM_COL32(90, 90, 94, 255)  : IM_COL32(190, 190, 190, 255);
    const ImU32 separator_col = m_is_dark ? IM_COL32(60, 60, 64, 255)  : IM_COL32(229, 229, 229, 255);
    // Unchecked / partial checkbox background follows the surface color so the
    // box does not glow white on the dark panel.
    const ImU32 checkbox_bg_col = m_is_dark ? IM_COL32(45, 45, 49, 255) : IM_COL32(255, 255, 255, 255);

    auto to_lower_ascii = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return value;
    };
    const std::string search_text_lc = to_lower_ascii(m_assembly_tree_search_text);

    std::function<bool(int)> node_matches_search;
    node_matches_search = [&tree, &search_text_lc, &to_lower_ascii, &node_matches_search](int node_id) {
        if (search_text_lc.empty())
            return true;
        if (node_id < 0 || node_id >= static_cast<int>(tree.nodes.size()))
            return false;
        const auto &node = tree.nodes[node_id];
        if (to_lower_ascii(node.label).find(search_text_lc) != std::string::npos)
            return true;
        for (int child_id : node.children) {
            if (node_matches_search(child_id))
                return true;
        }
        return false;
    };

    auto node_checkable = [&tree, &options](int node_id) {
        if (node_id < 0 || node_id >= static_cast<int>(tree.nodes.size()))
            return false;
        const auto& node = tree.nodes[node_id];
        if (!node.selectable)
            return false;
        if (node.volume_idx >= 0)
            return options.allow_volume_check;
        if (node.object_idx >= 0)
            return options.allow_object_check;
        return options.allow_object_check;
    };

    std::function<void(int, bool)> set_subtree_checked;
    set_subtree_checked = [&tree, &checked, &node_checkable, &set_subtree_checked](int node_id, bool checked_value) {
        if (node_id < 0 || node_id >= static_cast<int>(tree.nodes.size()))
            return;
        const auto& node = tree.nodes[node_id];
        if (node_checkable(node_id))
            checked[node.uid] = checked_value;
        for (int child_id : node.children)
            set_subtree_checked(child_id, checked_value);
    };

    std::function<bool(int)> has_selectable_descendant;
    has_selectable_descendant = [&tree, &node_checkable, &has_selectable_descendant](int node_id) {
        if (node_id < 0 || node_id >= static_cast<int>(tree.nodes.size()))
            return false;
        const auto &node = tree.nodes[node_id];
        for (int child_id : node.children) {
            if (node_checkable(child_id) || has_selectable_descendant(child_id))
                return true;
        }
        return false;
    };

    enum class AssemblyTreeCheckState { None, Partial, All };
    std::function<AssemblyTreeCheckState(int)> get_subtree_state;
    get_subtree_state = [&tree, &checked, &node_checkable, &has_selectable_descendant, &get_subtree_state](int node_id) {
        if (node_id < 0 || node_id >= static_cast<int>(tree.nodes.size()))
            return AssemblyTreeCheckState::None;
        const auto& node = tree.nodes[node_id];
        const bool checkable = node_checkable(node_id);
        bool checked_value = !checkable;
        if (checkable) {
            auto it = checked.find(node.uid);
            checked_value = it != checked.end() && it->second;
        }
        if (node.children.empty())
            return checked_value ? AssemblyTreeCheckState::All : AssemblyTreeCheckState::None;

        const bool has_descendant = has_selectable_descendant(node_id);
        bool has_state = checkable && !has_descendant;
        bool has_checked = has_state && checked_value;
        bool has_unchecked = has_state && !checked_value;
        for (int child_id : node.children) {
            if (!node_checkable(child_id) && !has_selectable_descendant(child_id))
                continue;
            has_state = true;
            switch (get_subtree_state(child_id)) {
            case AssemblyTreeCheckState::All:
                has_checked = true;
                break;
            case AssemblyTreeCheckState::None:
                has_unchecked = true;
                break;
            case AssemblyTreeCheckState::Partial:
                has_checked = true;
                has_unchecked = true;
                break;
            }
        }
        if (!has_state)
            return AssemblyTreeCheckState::All;
        if (has_checked && has_unchecked)
            return AssemblyTreeCheckState::Partial;
        return has_checked ? AssemblyTreeCheckState::All : AssemblyTreeCheckState::None;
    };

    std::function<void(int)> count_leaves;
    count_leaves = [&tree, &checked, &node_checkable, &has_selectable_descendant, &node_matches_search, &count_leaves, &result](int node_id) {
        if (node_id < 0 || node_id >= static_cast<int>(tree.nodes.size()))
            return;
        if (!node_matches_search(node_id))
            return;
        const auto& node = tree.nodes[node_id];
        if (node_checkable(node_id) && !has_selectable_descendant(node_id)) {
            ++result.leaf_count;
            auto it = checked.find(node.uid);
            if (it != checked.end() && it->second)
                ++result.checked_leaf_count;
            return;
        }
        for (int child_id : node.children)
            count_leaves(child_id);
    };
    for (int root_id : tree.roots)
        count_leaves(root_id);

    const float footer_h = options.show_footer ? 58.0f * sc : 0.0f;
    const float row_h = 36.0f * sc;
    const float indent_step = 26.0f * sc;
    const float checkbox_size = 20.0f * sc;
    const float arrow_size = 14.0f * sc;

    auto draw_checkbox = [sc, checkbox_size, green_col, border_col, checkbox_bg_col](ImDrawList* target_draw_list, const ImRect& rect, AssemblyTreeCheckState state) {
        const bool checked_state = state == AssemblyTreeCheckState::All;
        const bool partial = state == AssemblyTreeCheckState::Partial;
        if (checked_state) {
            target_draw_list->AddRectFilled(rect.Min, rect.Max, green_col, 3.0f * sc);
            if (s_assembly_tree_icons.select) {
                const float icon_w = 10.0f * sc;
                const float icon_h = 8.2f * sc;
                const ImVec2 icon_min(rect.Min.x + (checkbox_size - icon_w) * 0.5f, rect.Min.y + (checkbox_size - icon_h) * 0.5f);
                target_draw_list->AddImage(s_assembly_tree_icons.select, icon_min, ImVec2(icon_min.x + icon_w, icon_min.y + icon_h));
            }
        } else if (partial) {
            target_draw_list->AddRectFilled(rect.Min, rect.Max, checkbox_bg_col, 3.0f * sc);
            target_draw_list->AddRect(rect.Min, rect.Max, border_col, 3.0f * sc, 0, 2.0f * sc);
            target_draw_list->AddRectFilled(ImVec2(rect.Min.x + 4.0f * sc, rect.Min.y + 4.0f * sc),
                                            ImVec2(rect.Max.x - 4.0f * sc, rect.Max.y - 4.0f * sc), green_col, 1.0f * sc);
        } else {
            target_draw_list->AddRectFilled(rect.Min, rect.Max, checkbox_bg_col, 3.0f * sc);
            target_draw_list->AddRect(rect.Min, rect.Max, border_col, 3.0f * sc, 0, 2.0f * sc);
        }
    };

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    ImGui::BeginChild(options.child_id, ImVec2(0, options.show_footer ? -footer_h : 0), false, ImGuiWindowFlags_NoBackground);
    std::function<void(int, int, bool)> render_node;
    render_node = [this, &tree, &checked, &node_checkable, &set_subtree_checked, &get_subtree_state, &render_node,
                   &node_matches_search, search_text_lc, row_h, indent_step, checkbox_size, arrow_size, line_col, text_col, draw_checkbox, sc, options, &result]
                  (int node_id, int depth, bool is_last) {
        if (node_id < 0 || node_id >= static_cast<int>(tree.nodes.size()))
            return;
        if (!node_matches_search(node_id))
            return;
        const auto& node = tree.nodes[node_id];
        ImGui::PushID(node.uid.c_str());

        auto open_it = s_assembly_tree_open_nodes.find(node.uid);
        if (open_it == s_assembly_tree_open_nodes.end())
            open_it = s_assembly_tree_open_nodes.emplace(node.uid, true).first;
        bool& open = open_it->second;

        const bool checkable = node_checkable(node_id);
        const AssemblyTreeCheckState state = get_subtree_state(node_id);
        ImDrawList* child_draw_list = ImGui::GetWindowDrawList();
        const ImVec2 row_min = ImGui::GetCursorScreenPos();
        const float avail_w = ImGui::GetContentRegionAvail().x;
        ImGui::InvisibleButton("##assembly_tree_row", ImVec2(avail_w, row_h));
        const ImVec2 row_max = ImGui::GetItemRectMax();
        const bool hovered = ImGui::IsItemHovered();

        if (hovered)
            child_draw_list->AddRectFilled(row_min, row_max,
                m_is_dark ? IM_COL32(58, 58, 62, 255) : IM_COL32(245, 247, 248, 255), 4.0f * sc);

        const float center_y = row_min.y + row_h * 0.5f;
        const float content_x = row_min.x + depth * indent_step;
        if (depth > 0) {
            const float line_x = content_x - indent_step * 0.50f;
            child_draw_list->AddLine(ImVec2(line_x, row_min.y), ImVec2(line_x, is_last ? center_y : row_max.y), line_col, 2.0f * sc);
            child_draw_list->AddLine(ImVec2(line_x, center_y), ImVec2(content_x - 5.0f * sc, center_y), line_col, 2.0f * sc);
        }

        const ImRect checkbox_rect(ImVec2(content_x, center_y - checkbox_size * 0.5f),
                                   ImVec2(content_x + checkbox_size, center_y + checkbox_size * 0.5f));
        if (checkable)
            draw_checkbox(child_draw_list, checkbox_rect, state);

        const bool has_children = !node.children.empty();
        const float arrow_x = checkable ? checkbox_rect.Max.x + 10.0f * sc : content_x;
        const ImVec2 arrow_min(arrow_x, center_y - arrow_size * 0.5f);
        const ImRect arrow_rect(arrow_min, ImVec2(arrow_min.x + arrow_size, arrow_min.y + arrow_size));
        if (has_children && s_assembly_tree_icons.loaded) {
            ImTextureID arrow_tex = open
                ? (m_is_dark && s_assembly_tree_icons.expand_dark ? s_assembly_tree_icons.expand_dark : s_assembly_tree_icons.expand)
                : (m_is_dark && s_assembly_tree_icons.collapse_dark ? s_assembly_tree_icons.collapse_dark : s_assembly_tree_icons.collapse);
            child_draw_list->AddImage(arrow_tex, arrow_rect.Min, arrow_rect.Max);
        }

        const float text_x = has_children
            ? arrow_rect.Max.x + 10.0f * sc
            : (checkable ? checkbox_rect.Max.x + 10.0f * sc : content_x);
        const float text_max_x = row_max.x - 8.0f * sc;
        const float text_avail_w = std::max(0.0f, text_max_x - text_x);
        const std::string display_label = utf8_fit_with_ellipsis(node.label, text_avail_w);
        const char* display_begin = display_label.c_str();
        const char* display_end = display_begin + display_label.size();
        const ImVec2 text_size = ImGui::CalcTextSize(display_begin, display_end);
        const ImVec2 text_pos(text_x, center_y - text_size.y * 0.5f);
        child_draw_list->PushClipRect(text_pos, ImVec2(text_max_x, text_pos.y + text_size.y), true);
        child_draw_list->AddText(text_pos, text_col, display_begin, display_end);
        child_draw_list->PopClipRect();

        if (ImGui::IsItemClicked()) {
            const ImVec2 mouse_pos = ImGui::GetIO().MousePos;
            if (!options.readonly && checkable && checkbox_rect.Contains(mouse_pos)) {
                set_subtree_checked(node_id, state != AssemblyTreeCheckState::All);
                result.changed = true;
            } else if (has_children && arrow_rect.Contains(mouse_pos)) {
                open = !open;
            }
        }

        if (hovered && display_label != node.label)
            render_panel_tooltip(node.label);

        if ((open || !search_text_lc.empty()) && has_children) {
            for (size_t child_idx = 0; child_idx < node.children.size(); ++child_idx)
                render_node(node.children[child_idx], depth + 1, child_idx + 1 == node.children.size());
        }

        ImGui::PopID();
    };
    for (size_t root_idx = 0; root_idx < tree.roots.size(); ++root_idx)
        render_node(tree.roots[root_idx], 0, root_idx + 1 == tree.roots.size());
    ImGui::EndChild();

    if (!options.show_footer)
        return result;

    ImVec2 separator_start = ImGui::GetCursorScreenPos();
    draw_list->AddLine(separator_start, ImVec2(separator_start.x + ImGui::GetContentRegionAvail().x, separator_start.y), separator_col, 1.0f * sc);
    ImGui::Dummy(ImVec2(0.0f, 16.0f * sc));

    const ImVec2 footer_pos = ImGui::GetCursorScreenPos();
    const std::string checked_text = std::to_string(result.checked_leaf_count);
    const std::string total_text = " / " + std::to_string(result.leaf_count);
    draw_list->AddText(footer_pos, text_col, checked_text.c_str());
    const ImVec2 checked_text_size = ImGui::CalcTextSize(checked_text.c_str());
    draw_list->AddText(ImVec2(footer_pos.x + checked_text_size.x, footer_pos.y), sub_text_col, total_text.c_str());

    const ImVec2 cancel_size(86.0f * sc, 34.0f * sc);
    const ImVec2 ok_size(86.0f * sc, 34.0f * sc);
    const float button_gap = 12.0f * sc;
    const float buttons_x = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x - ImGui::GetStyle().WindowPadding.x - cancel_size.x - button_gap - ok_size.x;
    const float buttons_y = footer_pos.y - 7.0f * sc;

    if (render_footer_button("##assembly_tree_cancel", _u8L("Cancel"), ImVec2(buttons_x, buttons_y), cancel_size, false, sc)) {
        checked.clear();
        result.cancel = true;
        result.changed = true;
    }
    if (render_footer_button("##assembly_tree_ok", _u8L("OK"), ImVec2(buttons_x + cancel_size.x + button_gap, buttons_y), ok_size, true, sc))
        result.confirm = true;

    ImGui::SetCursorScreenPos(ImVec2(footer_pos.x, footer_pos.y + ok_size.y));
    return result;
}

bool AssemblyStepsUtils::render_connection_type_btn(
    ImDrawList *dl, float x, float y, float w, float h,
    ImTextureID icon, const char *label,
    float icon_sz, float label_fs, float sc,
    bool selected, ImU32 label_col, ImU32 brand_col,
    const char *tooltip)
{
    const ImVec2 btn_min(x, y);
    const ImVec2 btn_max(x + w, y + h);
    const float rounding = 4.0f * sc;

    const ImU32 bg = m_is_dark ? IM_COL32(45, 45, 49, 255) : IM_COL32(255, 255, 255, 255);
    dl->AddRectFilled(btn_min, btn_max, bg, rounding);
    if (selected)
        dl->AddRect(btn_min, btn_max, brand_col, rounding, 0, 2.0f * sc);

    if (icon) {
        const float ico_x = x + (w - icon_sz) * 0.5f;
        const float ico_y = y + 8.0f * sc;
        dl->AddImage(icon, ImVec2(ico_x, ico_y), ImVec2(ico_x + icon_sz, ico_y + icon_sz));
    }

    const ImVec2 lbl_sz = ImGui::GetFont()->CalcTextSizeA(label_fs, FLT_MAX, 0.0f, label);
    const float lbl_x = x + (w - lbl_sz.x) * 0.5f;
    const float lbl_y = y + h - lbl_sz.y - 6.0f * sc;
    dl->AddText(ImGui::GetFont(), label_fs, ImVec2(lbl_x, lbl_y), label_col, label);

    ImGui::SetCursorScreenPos(btn_min);
    ImGui::PushID(label);
    ImGui::InvisibleButton("##ct", ImVec2(w, h));
    bool clicked = ImGui::IsItemClicked(0);
    bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    if (hovered && tooltip && tooltip[0] != '\0' && m_imgui) {
        // Restore non-zero WindowPadding for the tooltip window: callers that
        // already pushed WindowPadding=(0,0) on the outer window (e.g. the
        // assembly guide panel) would otherwise glue the text to the edges.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
            ImVec2(8.0f * sc, 6.0f * sc));
        m_imgui->tooltip(tooltip, 20.0f * m_imgui->scaled(1.0f));
        ImGui::PopStyleVar();
    }

    return clicked;
}

bool AssemblyStepsUtils::render_cyber_brick_section(
    ImDrawList *dl, ImVec2 card_min, float card_w, float card_h,
    float font_sz, float small_fs, float sc)
{
    const ImU32 grey300 = IM_COL32(238, 238, 238, 255);
    const ImU32 grey400 = IM_COL32(206, 206, 206, 255);
    const ImU32 grey500 = IM_COL32(172, 172, 172, 255);
    const ImU32 grey700 = IM_COL32(107, 107, 107, 255);
    const float rounding = 4.0f * sc;

    // "+" button (top-right)
    const float btn_sz = 16.0f * sc;
    const ImVec2 btn_min(card_min.x + card_w - 8.0f * sc - btn_sz, card_min.y + 6.0f * sc);
    const ImVec2 btn_max(btn_min.x + btn_sz, btn_min.y + btn_sz);
    dl->AddRectFilled(btn_min, btn_max, grey300, rounding);
    dl->AddRect(btn_min, btn_max, grey400, rounding, 0, 1.0f * sc);
    const float pp = 4.0f * sc;
    const float mx = (btn_min.x + btn_max.x) * 0.5f;
    const float my = (btn_min.y + btn_max.y) * 0.5f;
    dl->AddLine(ImVec2(mx, btn_min.y + pp), ImVec2(mx, btn_max.y - pp), grey700, 1.5f * sc);
    dl->AddLine(ImVec2(btn_min.x + pp, my), ImVec2(btn_max.x - pp, my), grey700, 1.5f * sc);

    // Dashed placeholder area
    const float area_x = card_min.x + 8.0f * sc;
    const float area_y = card_min.y + font_sz + 14.0f * sc;
    const float area_w = card_w - 16.0f * sc;
    const float area_h = 36.0f * sc;
    const ImVec2 area_min(area_x, area_y);
    const ImVec2 area_max(area_x + area_w, area_y + area_h);

    const float dash_len = 5.0f * sc;
    const float gap_len  = 3.0f * sc;
    auto draw_dashed_line = [&](ImVec2 p0, ImVec2 p1) {
        float dx = p1.x - p0.x, dy = p1.y - p0.y;
        float len = sqrtf(dx * dx + dy * dy);
        if (len < 0.01f) return;
        float nx = dx / len, ny = dy / len;
        float d = 0;
        while (d < len) {
            float seg = std::min(dash_len, len - d);
            dl->AddLine(
                ImVec2(p0.x + nx * d, p0.y + ny * d),
                ImVec2(p0.x + nx * (d + seg), p0.y + ny * (d + seg)),
                grey400, 1.0f * sc);
            d += dash_len + gap_len;
        }
    };
    dl->AddRectFilled(area_min, area_max, grey300, rounding);
    draw_dashed_line(area_min, ImVec2(area_max.x, area_min.y));
    draw_dashed_line(ImVec2(area_max.x, area_min.y), area_max);
    draw_dashed_line(area_max, ImVec2(area_min.x, area_max.y));
    draw_dashed_line(ImVec2(area_min.x, area_max.y), area_min);

    // Hint text
    const std::string hint_str = _u8L("Click to add Cyber Brick");
    const ImVec2 hint_size = ImGui::GetFont()->CalcTextSizeA(small_fs, FLT_MAX, 0.0f, hint_str.c_str());
    dl->AddText(ImGui::GetFont(), small_fs,
        ImVec2(area_min.x + (area_w - hint_size.x) * 0.5f,
               area_min.y + (area_h - hint_size.y) * 0.5f),
        grey500, hint_str.c_str());

    // Click detection on the dashed area
    ImGui::SetCursorScreenPos(area_min);
    ImGui::PushID("##cyber_brick_area");
    ImGui::InvisibleButton("##cb", ImVec2(area_w, area_h));
    bool clicked = ImGui::IsItemClicked(0);
    ImGui::PopID();

    // Also detect click on "+" button
    ImGui::SetCursorScreenPos(btn_min);
    ImGui::PushID("##cyber_brick_add");
    ImGui::InvisibleButton("##cb_add", ImVec2(btn_sz, btn_sz));
    if (ImGui::IsItemClicked(0))
        clicked = true;
    ImGui::PopID();

    return clicked;
}

int AssemblyStepsUtils::render_timeline_keyframe(
    ImDrawList *dl, float x, float y, float w, float h,
    bool has_keyframe, bool selected,
    const char *label, float label_fs, float sc,
    bool show_delete_badge)
{
    const ImU32 brand   = IM_COL32(0, 174, 66, 255);
    const ImU32 grey200 = m_is_dark ? IM_COL32(50, 50, 54, 255)  : IM_COL32(248, 248, 248, 255);
    const ImU32 grey300 = m_is_dark ? IM_COL32(60, 60, 64, 255)  : IM_COL32(238, 238, 238, 255);
    const ImU32 grey400 = m_is_dark ? IM_COL32(70, 70, 74, 255)  : IM_COL32(206, 206, 206, 255);
    const ImU32 grey600 = m_is_dark ? IM_COL32(0x90, 0x90, 0x90, 255) : IM_COL32(144, 144, 144, 255);
    const ImU32 grey700 = m_is_dark ? IM_COL32(0xA0, 0xA0, 0xA0, 255) : IM_COL32(107, 107, 107, 255);
    const ImU32 white_c = m_is_dark ? IM_COL32(55, 55, 59, 255)  : IM_COL32(255, 255, 255, 255);
    const float font_sz = ImGui::GetFontSize();

    int result = 0;

    if (!has_keyframe) {
        // Dashed placeholder with "+" icon
        const ImVec2 slot_min(x, y);
        const ImVec2 slot_max(x + w, y + h);
        dl->AddRectFilled(slot_min, slot_max, grey300);

        const float dash = 4.0f * sc, gap = 3.0f * sc;
        auto dashed = [&](ImVec2 p0, ImVec2 p1) {
            float dx = p1.x - p0.x, dy = p1.y - p0.y;
            float len = sqrtf(dx * dx + dy * dy);
            if (len < 0.01f) return;
            float nx = dx / len, ny = dy / len;
            for (float d = 0; d < len;) {
                float seg = std::min(dash, len - d);
                dl->AddLine(ImVec2(p0.x + nx * d, p0.y + ny * d),
                            ImVec2(p0.x + nx * (d + seg), p0.y + ny * (d + seg)),
                            grey400, 1.0f * sc);
                d += dash + gap;
            }
        };
        dashed(slot_min, ImVec2(slot_max.x, slot_min.y));
        dashed(ImVec2(slot_max.x, slot_min.y), slot_max);
        dashed(slot_max, ImVec2(slot_min.x, slot_max.y));
        dashed(ImVec2(slot_min.x, slot_max.y), slot_min);

        // "+" sign centered
        const float plus_len = 8.0f * sc;
        const float cx = x + w * 0.5f, cy = y + h * 0.5f;
        dl->AddLine(ImVec2(cx, cy - plus_len * 0.5f), ImVec2(cx, cy + plus_len * 0.5f), grey700, 1.5f * sc);
        dl->AddLine(ImVec2(cx - plus_len * 0.5f, cy), ImVec2(cx + plus_len * 0.5f, cy), grey700, 1.5f * sc);

        ImGui::SetCursorScreenPos(slot_min);
        ImGui::PushID(label);
        ImGui::InvisibleButton("##kf_add", ImVec2(w, h));
        if (ImGui::IsItemClicked(0)) result = 1;
        ImGui::PopID();
    } else {
        // Keyframe thumbnail
        const ImVec2 slot_min(x, y);
        const ImVec2 slot_max(x + w, y + h);

        if (selected) {
            dl->AddRectFilled(slot_min, slot_max, IM_COL32(44, 173, 0, 38));
            dl->AddRect(slot_min, slot_max, brand, 0, 0, 1.5f * sc);
        } else {
            dl->AddRectFilled(slot_min, slot_max, grey200);
            dl->AddRect(slot_min, slot_max, grey400, 0, 0, 1.0f * sc);
        }

        // Frame icon centered
        if (m_tree_icon_frame) {
            const float ico = 20.0f * sc;
            dl->AddImage(m_tree_icon_frame,
                ImVec2(x + (w - ico) * 0.5f, y + (h - ico) * 0.5f),
                ImVec2(x + (w + ico) * 0.5f, y + (h + ico) * 0.5f));
        }

        // Label below
        const ImVec2 lsz = ImGui::GetFont()->CalcTextSizeA(label_fs, FLT_MAX, 0.0f, label);
        const ImU32 lbl_col = selected ? brand : grey600;
        dl->AddText(ImGui::GetFont(), label_fs,
            ImVec2(x + (w - lsz.x) * 0.5f, y + h + 4.0f * sc), lbl_col, label);

        // Delete badge geometry (computed first for hit-test priority)
        const float badge_r = 7.0f * sc;
        const ImVec2 badge_c(slot_max.x - 3.0f * sc, slot_min.y + 3.0f * sc);
        const ImVec2 badge_min(badge_c.x - badge_r, badge_c.y - badge_r);
        const ImVec2 badge_max(badge_c.x + badge_r, badge_c.y + badge_r);

        bool slot_hovered = ImGui::IsMouseHoveringRect(slot_min, slot_max);
        bool badge_hovered = show_delete_badge && selected
            && ImGui::IsMouseHoveringRect(badge_min, badge_max);

        // Delete badge (top-right, rendered and clickable first to get priority).
        // Skipped entirely when `show_delete_badge` is false (e.g. the End frame
        // is mandatory and must not be deletable).
        if (show_delete_badge && slot_hovered && selected) {
            dl->AddCircleFilled(badge_c, badge_r, brand);
            if (m_tree_icon_cross) {
                const float cr = 5.0f * sc;
                dl->AddImage(m_tree_icon_cross,
                    ImVec2(badge_c.x - cr, badge_c.y - cr),
                    ImVec2(badge_c.x + cr, badge_c.y + cr),
                    ImVec2(0, 0), ImVec2(1, 1), white_c);
            }

            ImGui::SetCursorScreenPos(badge_min);
            ImGui::PushID(label);
            ImGui::PushID("##kf_del");
            ImGui::InvisibleButton("##del", ImVec2(badge_r * 2, badge_r * 2));
            if (ImGui::IsItemClicked(0)) result = -1;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", _u8L("Delete this keyframe").c_str());
            ImGui::PopID();
            ImGui::PopID();
        }

        // Slot click (only if not clicking the badge)
        if (result == 0) {
            ImGui::SetCursorScreenPos(slot_min);
            ImGui::PushID(label);
            ImGui::InvisibleButton("##kf_slot", ImVec2(w, h));
            if (ImGui::IsItemClicked(0) && !badge_hovered) result = 1;
            ImGui::PopID();
        }
    }

    return result;
}

bool AssemblyStepsUtils::render_note_tool_btn(
    ImDrawList *dl, float x, float y, float sz,
    ImTextureID icon, bool selected, const char *id, float sc,
    const char *tooltip)
{
    const ImU32 white_c = m_is_dark ? IM_COL32(45, 45, 49, 255) : IM_COL32(255, 255, 255, 255);
    const ImU32 grey400 = m_is_dark ? IM_COL32(70, 70, 74, 255) : IM_COL32(206, 206, 206, 255);
    const ImU32 brand   = IM_COL32(0, 174, 66, 255);
    const float rounding = 4.0f * sc;

    const ImVec2 bmin(x, y);
    const ImVec2 bmax(x + sz, y + sz);
    dl->AddRectFilled(bmin, bmax, white_c, rounding);
    dl->AddRect(bmin, bmax, selected ? brand : grey400, rounding, 0,
        selected ? 1.5f * sc : 1.0f * sc);

    if (icon) {
        const float ico = sz * 0.88f;
        const ImVec2 imin(x + (sz - ico) * 0.5f, y + (sz - ico) * 0.5f);
        dl->AddImage(icon, imin, ImVec2(imin.x + ico, imin.y + ico));
    }

    ImGui::SetCursorScreenPos(bmin);
    ImGui::PushID(id);
    ImGui::InvisibleButton("##nt", ImVec2(sz, sz));
    bool clicked = ImGui::IsItemClicked(0);
    bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    if (hovered && tooltip && tooltip[0] != '\0' && m_imgui) {
        // Restore non-zero WindowPadding so the tooltip popup window doesn't
        // inherit the outer panel's WindowPadding=(0,0) and glue text to edges.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
            ImVec2(8.0f * sc, 6.0f * sc));
        m_imgui->tooltip(tooltip, 20.0f * m_imgui->scaled(1.0f));
        ImGui::PopStyleVar();
    }

    return clicked;
}

bool AssemblyStepsUtils::render_note_color_control(ImDrawList *dl, float x, float y, float sc)
{
    const float swatch_sz = 14.4f * sc;
    const float gap       = 4.8f * sc;
    const float pad_x     = 8.0f * sc;
    const float pad_y     = 4.0f * sc;
    const float rounding  = 4.0f * sc;
    const float swatch_rounding = 2.0f * sc;
    const float w = 2.0f * pad_x + IM_ARRAYSIZE(kNoteColors) * swatch_sz + (IM_ARRAYSIZE(kNoteColors) - 1) * gap;
    const float h = 2.0f * pad_y + swatch_sz;

    const ImVec2 min(x, y);
    const ImVec2 max(x + w, y + h);
    dl->AddRectFilled(min, max, m_is_dark ? IM_COL32(45, 45, 49, 255) : IM_COL32(255, 255, 255, 255), rounding);
    dl->AddRect(min, max, m_is_dark ? IM_COL32(70, 70, 74, 255) : IM_COL32(238, 238, 238, 255), rounding);

    bool changed = false;
    float sx = x + pad_x;
    const float sy = y + pad_y;
    for (int i = 0; i < (int)IM_ARRAYSIZE(kNoteColors); ++i) {
        const NoteColorItem &item = kNoteColors[i];
        const ImVec2 smin(sx, sy);
        const ImVec2 smax(sx + swatch_sz, sy + swatch_sz);
        dl->AddRectFilled(smin, smax, note_color_to_im_u32(item.color), swatch_rounding);
        if (item.has_border)
            dl->AddRect(smin, smax, m_is_dark ? IM_COL32(100, 100, 104, 255) : IM_COL32(172, 172, 172, 255), swatch_rounding);
        if (m_guide_note_color_selected == i)
            dl->AddRect(ImVec2(smin.x - 2.4f * sc, smin.y - 2.4f * sc),
                ImVec2(smax.x + 2.4f * sc, smax.y + 2.4f * sc),
                IM_COL32(0, 174, 66, 255), swatch_rounding + 2.4f * sc, 0, 1.8f * sc);

        ImGui::SetCursorScreenPos(smin);
        ImGui::PushID(item.id);
        ImGui::InvisibleButton("##note_color", ImVec2(swatch_sz, swatch_sz));
        if (ImGui::IsItemClicked(0)) {
            set_selection_origin(SelectionOrigin::NoteColorControl);
        }
        if (ImGui::IsItemClicked(0) && m_guide_note_color_selected != i) {
            m_guide_note_color_selected = i;
            auto *entries = get_current_kf_entries();
            if (entries && m_keyframe_selected >= 0 && m_keyframe_selected < (int)entries->size()) {
                KeyFrameEntry &cur_entry = (*entries)[m_keyframe_selected];
                AssemblyNote &note = cur_entry.data.assembly_note;
                const std::array<int, 4> color = item.color;
                if (m_note_selected_type == AssemblyNoteSelectionType::ArrowSvg &&
                    m_note_selected_idx >= 0 && m_note_selected_idx < (int)note.arrow_svgs.size())
                    note.arrow_svgs[m_note_selected_idx].color = color;
                else if (m_note_selected_type == AssemblyNoteSelectionType::TextLabel &&
                         m_note_selected_idx >= 0 && m_note_selected_idx < (int)note.text_labels.size())
                    note.text_labels[m_note_selected_idx].color = color;
                else if (m_note_selected_type == AssemblyNoteSelectionType::Circle &&
                         m_note_selected_idx >= 0 && m_note_selected_idx < (int)note.circle_notes.size())
                    note.circle_notes[m_note_selected_idx].color = color;
                else if (m_note_selected_type == AssemblyNoteSelectionType::Rectangle &&
                         m_note_selected_idx >= 0 && m_note_selected_idx < (int)note.rectangle_notes.size())
                    note.rectangle_notes[m_note_selected_idx].color = color;
                else if (m_note_selected_type == AssemblyNoteSelectionType::PlainArrow &&
                         m_note_selected_idx >= 0 && m_note_selected_idx < (int)note.plain_arrows.size())
                    note.plain_arrows[m_note_selected_idx].color = color;
                cur_entry.need_save = true;
                save_assembly_steps_json_to_model();
                do_commond_callback("dirty");
            }
            changed = true;
        }
        if (ImGui::IsItemHovered() && m_imgui) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                ImVec2(8.0f * sc, 6.0f * sc));
            m_imgui->tooltip(_u8L(item.tip), 20.0f * m_imgui->scaled(1.0f));
            ImGui::PopStyleVar();
        }
        ImGui::PopID();

        sx += swatch_sz + gap;
    }

    return changed;
}

bool AssemblyStepsUtils::render_note_bg_color_control(ImDrawList *dl, float x, float y, float sc)
{
    // Visual layout matches render_note_color_control so the two rows align.
    const float swatch_sz = 14.4f * sc;
    const float gap       = 4.8f * sc;
    const float pad_x     = 8.0f * sc;
    const float pad_y     = 4.0f * sc;
    const float rounding  = 4.0f * sc;
    const float swatch_rounding = 2.0f * sc;
    const float w = 2.0f * pad_x + IM_ARRAYSIZE(kNoteColors) * swatch_sz + (IM_ARRAYSIZE(kNoteColors) - 1) * gap;
    const float h = 2.0f * pad_y + swatch_sz;

    const ImVec2 min(x, y);
    const ImVec2 max(x + w, y + h);
    dl->AddRectFilled(min, max, m_is_dark ? IM_COL32(45, 45, 49, 255) : IM_COL32(255, 255, 255, 255), rounding);
    dl->AddRect(min, max, m_is_dark ? IM_COL32(70, 70, 74, 255) : IM_COL32(238, 238, 238, 255), rounding);

    bool changed = false;
    float sx = x + pad_x;
    const float sy = y + pad_y;
    for (int i = 0; i < (int)IM_ARRAYSIZE(kNoteColors); ++i) {
        const NoteColorItem &item = kNoteColors[i];
        const ImVec2 smin(sx, sy);
        const ImVec2 smax(sx + swatch_sz, sy + swatch_sz);
        dl->AddRectFilled(smin, smax, note_color_to_im_u32(item.color), swatch_rounding);
        if (item.has_border)
            dl->AddRect(smin, smax, m_is_dark ? IM_COL32(100, 100, 104, 255) : IM_COL32(172, 172, 172, 255), swatch_rounding);
        if (m_guide_note_bg_color_selected == i)
            dl->AddRect(ImVec2(smin.x - 2.4f * sc, smin.y - 2.4f * sc),
                ImVec2(smax.x + 2.4f * sc, smax.y + 2.4f * sc),
                IM_COL32(0, 174, 66, 255), swatch_rounding + 2.4f * sc, 0, 1.8f * sc);

        ImGui::SetCursorScreenPos(smin);
        ImGui::PushID(item.id);
        ImGui::InvisibleButton("##note_bg_color", ImVec2(swatch_sz, swatch_sz));
        if (ImGui::IsItemClicked(0)) {
            set_selection_origin(SelectionOrigin::NoteColorControl);
        }
        if (ImGui::IsItemClicked(0) && m_guide_note_bg_color_selected != i) {
            m_guide_note_bg_color_selected = i;
            auto *entries = get_current_kf_entries();
            if (entries && m_keyframe_selected >= 0 && m_keyframe_selected < (int)entries->size()) {
                KeyFrameEntry &cur_entry = (*entries)[m_keyframe_selected];
                AssemblyNote &note = cur_entry.data.assembly_note;
                if (m_note_selected_type == AssemblyNoteSelectionType::TextLabel &&
                    m_note_selected_idx >= 0 && m_note_selected_idx < (int)note.text_labels.size()) {
                    // Preserve the historic ~0.85 alpha so the background still
                    // blends with the underlying canvas.
                    std::array<int, 4> bg = item.color;
                    bg[3] = 217;
                    note.text_labels[m_note_selected_idx].background_color = bg;
                    cur_entry.need_save = true;
                    save_assembly_steps_json_to_model();
                    do_commond_callback("dirty");
                }
            }
            changed = true;
        }
        if (ImGui::IsItemHovered() && m_imgui) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                ImVec2(8.0f * sc, 6.0f * sc));
            m_imgui->tooltip(_u8L(item.tip), 20.0f * m_imgui->scaled(1.0f));
            ImGui::PopStyleVar();
        }
        ImGui::PopID();

        sx += swatch_sz + gap;
    }

    return changed;
}

bool AssemblyStepsUtils::render_footer_button(const char* id, const std::string& label,
                                              const ImVec2& pos, const ImVec2& size,
                                              bool primary, float sc)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 text_size = ImGui::CalcTextSize(label.c_str());
    const float min_pad_x = 14.0f * sc;
    const ImVec2 draw_size(std::max(size.x, text_size.x + 2.0f * min_pad_x), size.y);
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id, draw_size);
    const bool clicked = ImGui::IsItemClicked();
    const bool hovered = ImGui::IsItemHovered();
    const bool disabled = ImGui::GetItemFlags() & ImGuiItemFlags_Disabled;

    const ImU32 sec_bg     = m_is_dark ? IM_COL32(55, 55, 59, 255)   : IM_COL32(255, 255, 255, 255);
    const ImU32 sec_border = m_is_dark ? IM_COL32(90, 90, 94, 255)   : IM_COL32(202, 202, 202, 255);
    const ImU32 sec_text   = m_is_dark ? IM_COL32(0xE0, 0xE0, 0xE0, 255) : IM_COL32(38, 46, 48, 255);
    const ImU32 dis_bg     = m_is_dark ? IM_COL32(60, 60, 64, 255)   : IM_COL32(238, 238, 238, 255);
    const ImU32 dis_border = m_is_dark ? IM_COL32(70, 70, 74, 255)   : IM_COL32(206, 206, 206, 255);

    const ImU32 bg = disabled ? dis_bg :
        (primary ? (hovered ? IM_COL32(0, 190, 74, 255) : IM_COL32(0, 174, 66, 255)) : sec_bg);
    const ImU32 border = disabled ? dis_border :
        (primary ? bg : (hovered ? IM_COL32(0, 174, 66, 255) : sec_border));
    const ImU32 text = disabled ? IM_COL32(172, 172, 172, 255) :
        (primary ? IM_COL32(255, 255, 255, 255) : sec_text);
    draw_list->AddRectFilled(pos, ImVec2(pos.x + draw_size.x, pos.y + draw_size.y), bg, draw_size.y * 0.5f);
    draw_list->AddRect(pos, ImVec2(pos.x + draw_size.x, pos.y + draw_size.y), border, draw_size.y * 0.5f, 0, 2.0f * sc);

    const ImVec2 text_pos(pos.x + (draw_size.x - text_size.x) * 0.5f, pos.y + (draw_size.y - text_size.y) * 0.5f);
    draw_list->AddText(text_pos, text, label.c_str());
    return !disabled && clicked;
}

void AssemblyStepsUtils::render_export_menu_popup(const char* popup_id, float sc)
{
    static const int kExportItemCount = 3;
    const std::string labels[] = { _u8L("Export PDF"), _u8L("Export Markdown"), _u8L("Export MP4") };
    const ExportType types[] = { ExportType::PDF, ExportType::MarkDown, ExportType::MP4 };
    const std::string markdown_tooltip = _u8L("After exporting the Markdown document, you can edit it in third-party software such as Zettlr and then export it to PDF.");
    const float row_height = 28.0f * sc;
    const float row_spacing = 2.0f * sc;
    const float win_padding = 12.0f * sc;
    const float row_pad_x = 8.0f * sc;
    const float text_right_margin = 8.0f * sc;

    float max_text_width = 0.0f;
    for (int i = 0; i < kExportItemCount; ++i)
        max_text_width = std::max(max_text_width, ImGui::CalcTextSize(labels[i].c_str()).x);

    const float menu_width = std::max(128.0f * sc,
        2.0f * win_padding + row_pad_x + max_text_width + text_right_margin);
    const float menu_height = win_padding * 2.0f + row_height * kExportItemCount + row_spacing * (kExportItemCount - 1);
    ImGui::SetNextWindowSize(ImVec2(menu_width, menu_height), ImGuiCond_Always);

    ImGui::PushStyleColor(ImGuiCol_PopupBg, m_is_dark ? ImVec4(45 / 255.0f, 45 / 255.0f, 49 / 255.0f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 77.0f / 255.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, m_is_dark ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(38.0f / 255.0f, 46.0f / 255.0f, 48.0f / 255.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f * sc);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(win_padding, win_padding));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

    if (ImGui::BeginPopup(popup_id, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove)) {
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_ChildWindows)) {
            ImGuiIO &io = ImGui::GetIO();
            io.WantCaptureMouse = true;
        }

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        for (int i = 0; i < kExportItemCount; ++i) {
            ImGui::PushID(i);
            ImVec2 row_pos = ImGui::GetCursorScreenPos();
            const float row_content_w = ImGui::GetContentRegionAvail().x;
            if (ImGui::InvisibleButton("##assembly_export_item", ImVec2(row_content_w, row_height))) {
                on_export(types[i]);
                ImGui::CloseCurrentPopup();
            }

            const bool hovered = ImGui::IsItemHovered();
            if (hovered) {
                const ImU32 bg = m_is_dark ? IM_COL32(55, 55, 59, 255) : IM_COL32(240, 240, 240, 255);
                draw_list->AddRectFilled(row_pos, ImVec2(row_pos.x + row_content_w, row_pos.y + row_height), bg, 4.0f * sc);
                if (types[i] == ExportType::MarkDown)
                    render_panel_tooltip(markdown_tooltip, false);
            }

            const ImVec2 text_size = ImGui::CalcTextSize(labels[i].c_str());
            draw_list->AddText(ImVec2(row_pos.x + row_pad_x, row_pos.y + (row_height - text_size.y) * 0.5f),
                               ImGui::GetColorU32(ImGuiCol_Text), labels[i].c_str());
            ImGui::PopID();
            if (i + 1 < kExportItemCount)
                ImGui::Dummy(ImVec2(0.0f, row_spacing));
        }
        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(3);
}

void AssemblyStepsUtils::render_labels_show_type_menu_popup(const char* popup_id, float sc)
{
    static const int kTypeCount = 3;
    const std::string labels[] = {
        _u8L("Auto recommend"),
        _u8L("Model objects only"),
        _u8L("Model parts only")
    };
    const LabelsShowType types[] = {
        LabelsShowType::AutoRecommend,
        LabelsShowType::OnlyModelObject,
        LabelsShowType::OnlyModelVolume
    };
    // Per-type hover tooltip explaining what each labels-show mode does. Index-aligned with labels[]/types[].
    const std::string type_tooltips[] = {
        _u8L("Automatically choose object-or-part-level labels: objects already shown in earlier steps are collapsed into a single object label, while the rest are labeled per individual part."),
        _u8L("Show one label per model object."),
        _u8L("Show one label per model part.")
    };
    // Trailing action row (not a type): relayout the current labels in place.
    const std::string action_label = _u8L("Auto-arrange labels in current view");

    const float row_height = 28.0f * sc;
    const float row_spacing = 2.0f * sc;
    const float win_padding = 12.0f * sc;
    const float row_pad_x = 8.0f * sc;
    const float check_w = 18.0f * sc;       // leading column for the current-selection mark
    const float text_right_margin = 8.0f * sc;
    const float sep_line_h = 1.0f * sc;
    const float sep_block_h = row_spacing + sep_line_h + row_spacing; // gap+line+gap

    float max_text_width = 0.0f;
    for (int i = 0; i < kTypeCount; ++i)
        max_text_width = std::max(max_text_width, ImGui::CalcTextSize(labels[i].c_str()).x);
    max_text_width = std::max(max_text_width, ImGui::CalcTextSize(action_label.c_str()).x);

    const float menu_width = std::max(128.0f * sc,
        2.0f * win_padding + row_pad_x + check_w + max_text_width + text_right_margin);
    const float menu_height = win_padding * 2.0f
        + row_height * (kTypeCount + 1)
        + row_spacing * (kTypeCount - 1)
        + sep_block_h;
    ImGui::SetNextWindowSize(ImVec2(menu_width, menu_height), ImGuiCond_Always);

    ImGui::PushStyleColor(ImGuiCol_PopupBg, m_is_dark ? ImVec4(45 / 255.0f, 45 / 255.0f, 49 / 255.0f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 77.0f / 255.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, m_is_dark ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(38.0f / 255.0f, 46.0f / 255.0f, 48.0f / 255.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f * sc);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(win_padding, win_padding));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    if (ImGui::BeginPopup(popup_id, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove)) {
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const ImU32 brand = IM_COL32(0, 174, 66, 255);
        for (int i = 0; i < kTypeCount; ++i) {
            ImGui::PushID(i);
            ImVec2 row_pos = ImGui::GetCursorScreenPos();
            const float row_content_w = ImGui::GetContentRegionAvail().x;
            if (ImGui::InvisibleButton("##assembly_labels_show_type_item", ImVec2(row_content_w, row_height))) {
                set_labels_show_type(types[i]);
                ImGui::CloseCurrentPopup();
            }

            const bool hovered = ImGui::IsItemHovered();
            if (hovered) {
                const ImU32 bg = m_is_dark ? IM_COL32(55, 55, 59, 255) : IM_COL32(240, 240, 240, 255);
                draw_list->AddRectFilled(row_pos, ImVec2(row_pos.x + row_content_w, row_pos.y + row_height), bg, 4.0f * sc);
                render_panel_tooltip(type_tooltips[i], false);
            }

            // Leading check mark for the currently active type.
            if (types[i] == m_cur_labels_show_type) {
                const float mk = 10.0f * sc;
                const float mx = row_pos.x + row_pad_x;
                const float my = row_pos.y + (row_height - mk) * 0.5f;
                draw_list->AddLine(ImVec2(mx + mk * 0.10f, my + mk * 0.55f),
                                   ImVec2(mx + mk * 0.40f, my + mk * 0.85f), brand, 1.6f * sc);
                draw_list->AddLine(ImVec2(mx + mk * 0.40f, my + mk * 0.85f),
                                   ImVec2(mx + mk * 0.90f, my + mk * 0.15f), brand, 1.6f * sc);
            }

            const ImVec2 text_size = ImGui::CalcTextSize(labels[i].c_str());
            draw_list->AddText(ImVec2(row_pos.x + row_pad_x + check_w, row_pos.y + (row_height - text_size.y) * 0.5f),
                               ImGui::GetColorU32(ImGuiCol_Text), labels[i].c_str());
            ImGui::PopID();
            if (i + 1 < kTypeCount)
                ImGui::Dummy(ImVec2(0.0f, row_spacing));
        }

        // Separator between the type list and the action row.
        ImGui::Dummy(ImVec2(0.0f, row_spacing));
        {
            const ImVec2 sp = ImGui::GetCursorScreenPos();
            const float  full_w = ImGui::GetContentRegionAvail().x;
            draw_list->AddLine(ImVec2(sp.x, sp.y), ImVec2(sp.x + full_w, sp.y),
                               m_is_dark ? IM_COL32(70, 70, 74, 255) : IM_COL32(228, 228, 228, 255),
                               sep_line_h);
        }
        ImGui::Dummy(ImVec2(0.0f, row_spacing + sep_line_h));

        // Action row: re-layout labels in the current view (no camera reframe).
        {
            ImGui::PushID("##assembly_labels_auto_layout_cur_view");
            ImVec2      row_pos       = ImGui::GetCursorScreenPos();
            const float row_content_w = ImGui::GetContentRegionAvail().x;
            if (ImGui::InvisibleButton("##assembly_labels_auto_layout_item", ImVec2(row_content_w, row_height))) {
                auto_layout_labels_in_current_view();
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered()) {
                const ImU32 bg = m_is_dark ? IM_COL32(55, 55, 59, 255) : IM_COL32(240, 240, 240, 255);
                draw_list->AddRectFilled(row_pos, ImVec2(row_pos.x + row_content_w, row_pos.y + row_height), bg, 4.0f * sc);
            }
            const ImVec2 text_size = ImGui::CalcTextSize(action_label.c_str());
            draw_list->AddText(ImVec2(row_pos.x + row_pad_x + check_w, row_pos.y + (row_height - text_size.y) * 0.5f),
                               ImGui::GetColorU32(ImGuiCol_Text), action_label.c_str());
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(3);
}

bool AssemblyStepsUtils::render_checkbox(
    ImDrawList *dl, float x, float y, float sz,
    bool *checked, const char *id, float sc)
{
    const ImU32 white_c = IM_COL32(255, 255, 255, 255);
    const ImU32 grey400 = IM_COL32(206, 206, 206, 255);
    const ImU32 brand   = IM_COL32(0, 174, 66, 255);
    const float rounding = 2.0f * sc;

    const ImVec2 cb_min(x, y);
    const ImVec2 cb_max(x + sz, y + sz);

    bool is_checked = checked && *checked;
    if (is_checked) {
        dl->AddRectFilled(cb_min, cb_max, brand, rounding);
        // Draw checkmark (two line segments)
        const float a_x = x + sz * 0.22f, a_y = y + sz * 0.52f;
        const float b_x = x + sz * 0.43f, b_y = y + sz * 0.72f;
        const float c_x = x + sz * 0.78f, c_y = y + sz * 0.32f;
        dl->AddLine(ImVec2(a_x, a_y), ImVec2(b_x, b_y), white_c, 1.5f * sc);
        dl->AddLine(ImVec2(b_x, b_y), ImVec2(c_x, c_y), white_c, 1.5f * sc);
    } else {
        dl->AddRectFilled(cb_min, cb_max, white_c, rounding);
        dl->AddRect(cb_min, cb_max, grey400, rounding, 0, 1.0f * sc);
    }

    ImGui::SetCursorScreenPos(cb_min);
    ImGui::PushID(id);
    ImGui::InvisibleButton("##cb", ImVec2(sz, sz));
    bool clicked = ImGui::IsItemClicked(0);
    ImGui::PopID();
    if (clicked && checked) {
        *checked = !*checked;
        return true;
    }
    return false;
}

bool AssemblyStepsUtils::rects_overlap(const ImVec2 &lhs_min, const ImVec2 &lhs_max,
                                       const ImVec2 &rhs_min, const ImVec2 &rhs_max)
{
    if (lhs_min.x >= lhs_max.x || lhs_min.y >= lhs_max.y ||
        rhs_min.x >= rhs_max.x || rhs_min.y >= rhs_max.y)
        return false;

    return lhs_min.x < rhs_max.x && lhs_max.x > rhs_min.x &&
           lhs_min.y < rhs_max.y && lhs_max.y > rhs_min.y;
}

bool AssemblyStepsUtils::is_part_number_label_layout_overlapped(const ImVec2 &rect_min, const ImVec2 &rect_max) const
{
    return rects_overlap(rect_min, rect_max,
                         m_part_number_label_forbidden_left_area.min,
                         m_part_number_label_forbidden_left_area.max) ||
           rects_overlap(rect_min, rect_max,
                         m_part_number_label_forbidden_bottom_area.min,
                         m_part_number_label_forbidden_bottom_area.max);
}

void AssemblyStepsUtils::render_assembly_guide_export_button(float panel_x, float panel_y, float sc)
{
    if (!m_imgui)
        return;
    if (m_gizmo_active)
        return;
    //if (!is_selected_final_assembly_node())
        //return;

    const float pad_x         = 8.0f * sc;
    const float pad_y         = 4.0f * sc;
    const float icon_sz       = 24.0f * sc;
    const float icon_inset    = icon_sz * 0.125f;            // Figma inset-[12.5%]
    const float icon_body_sz  = icon_sz - icon_inset * 2.0f; // 18*sc effective
    const float label_fs      = ImGui::GetFontSize();
    const float label_line    = std::max(20.0f * sc, label_fs + 4.0f * sc);
    const float rounding      = 4.0f * sc;
    const float gap           = 12.0f * sc; // horizontal gap to the guide panel

    // Measure the label up-front so the card is wide enough for the bigger font.
    // (Localised "Export" can be longer than the icon e.g. zh "Export" is fine but
    // some locales need more horizontal room.)
    const std::string label_str = _u8L("Export");
    const ImVec2 text_sz = ImGui::GetFont()->CalcTextSizeA(label_fs, FLT_MAX, 0.0f, label_str.c_str());

    // Single source of truth for the button footprint so collision detection
    // (export_button_intersects_toolbar / get_guide_panel_y_offset) matches exactly.
    const ImVec2 btn_sz = export_button_size(sc);
    const float  btn_w  = btn_sz.x;
    const float  btn_h  = btn_sz.y;

    float btn_x, btn_y;
    if (m_export_btn_corner_mode) {
        // Overlapping the gizmo toolbar at the default spot: relocate to the canvas
        // top-right corner, right-aligned with the guide panel's right edge and
        // TOP-aligned with the gizmo toolbar's top. The button card is taller than the
        // toolbar, so bottom-aligning would push it above the toolbar; top-aligning
        // keeps it within the top band and the panel is shifted down to leave a gap.
        btn_x = m_export_btn_canvas_w - 12.0f * sc - btn_w;
        btn_y = m_gizmo_toolbar_rect_min.y + 8.0f * sc;
    } else {
        btn_x = panel_x - btn_w - gap;
        btn_y = panel_y;
    }

    ImGui::SetNextWindowPos(ImVec2(btn_x, btn_y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(btn_w, btn_h), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    // Window background is transparent the rgba(0,0,0,0.3) card is painted
    // explicitly below so we can clip it with the 4px rounded rect.
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));

    m_imgui->begin(std::string("##assembly_guide_export_btn"),
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImDrawList *dl      = ImGui::GetWindowDrawList();
    const ImVec2 win_min = ImGui::GetWindowPos();
    const ImVec2 win_max(win_min.x + btn_w, win_min.y + btn_h);

    // Hit area first so we can paint a hover overlay over the base card.
    ImGui::SetCursorScreenPos(win_min);
    ImGui::InvisibleButton("##t", ImVec2(btn_w, btn_h));
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked(0);

    // Base card: rgba(0,0,0,0.3) - alpha 0.3 = 77/255.
    dl->AddRectFilled(win_min, win_max, IM_COL32(0, 0, 0, 77), rounding);
    if (hovered) {
        // Subtle hover lift: slightly darker card.
        dl->AddRectFilled(win_min, win_max, IM_COL32(0, 0, 0, 38), rounding);
    }

    // Layout reserves a full icon_sz (24*sc) box so the label band positioning
    // stays Figma-faithful, but the SVG is blitted into the 12.5%-inset inner
    // 18*sc square -- effectively (pad_y + icon_inset) = 7*sc gap between the
    // card's top edge and the icon body, which matches the Figma reference.
    const float icon_box_x = win_min.x + (btn_w - icon_sz) * 0.5f;
    const float icon_box_y = win_min.y + pad_y;
    const float icon_x = icon_box_x + icon_inset;
    const float icon_y = icon_box_y + icon_inset;
    if (m_btn_icon_export) {
        dl->AddImage(m_btn_icon_export,
                     ImVec2(icon_x, icon_y),
                     ImVec2(icon_x + icon_body_sz, icon_y + icon_body_sz));
    }

    // Label "Export": white, centered horizontally within the label band.
    // label_str / text_sz were measured up-front so btn_w already fits the text.
    const float text_x = win_min.x + (btn_w - text_sz.x) * 0.5f;
    const float text_y = icon_box_y + icon_sz + (label_line - text_sz.y) * 0.5f;
    dl->AddText(ImGui::GetFont(), label_fs, ImVec2(text_x, text_y),
                IM_COL32(255, 255, 255, 255), label_str.c_str());
    const char *export_popup_id = "##assembly_export_menu";
    if (clicked) {
        update_final_assembly_step_number_to_max();
        ImGui::OpenPopup(export_popup_id);
    }
    render_export_menu_popup(export_popup_id, sc);

    if (hovered) {
        // Tooltip restores non-zero WindowPadding so the popup has margin.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * sc, 6.0f * sc));
        m_imgui->tooltip(_u8L("Export assembly guide PDF, Markdown or MP4"),
                         20.0f * m_imgui->scaled(1.0f));
        ImGui::PopStyleVar();
    }

    m_imgui->end();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
}

void AssemblyStepsUtils::render_assembly_guide_panel(float panel_x, float panel_y, float panel_w, float panel_h, float sc, bool is_dark)
{
    // The blocking rect is set to the panel's ACTUAL height once it is known
    m_panel_rect_guide_min = m_panel_rect_guide_max = ImVec2(0, 0);
    // Single-shot diagnostics: log the FIRST frame where each early-return
    auto log_skip_once = [&](const char *reason) {
        static int s_last_logged_node = -2;
        static std::string s_last_reason;
        if (s_last_logged_node == m_selected_node && s_last_reason == reason)
            return;
        s_last_logged_node = m_selected_node;
        s_last_reason      = reason;
        BOOST_LOG_TRIVIAL(warning)
            << "render_assembly_guide_panel: skip (" << reason
            << ") m_selected_node=" << m_selected_node
            << ", parent_folder=" << find_parent_folder(m_selected_node)
            << ", only_step=" << (m_only_step_node_create_key_frame ? 1 : 0)
            << ", panel=(" << panel_x << "," << panel_y << "," << panel_w << "x" << panel_h << ")"
            << ", sc=" << sc;
    };

    if (!has_selected_node()) {
        log_skip_once("no selected node");
        return;
    }
    if (!m_imgui || m_model == nullptr) {
        log_skip_once("no imgui or model");
        return;
    }

    // The original gate was "if user only allows step-node keyframing AND the
    if (m_only_step_node_create_key_frame && find_parent_folder(m_selected_node) < 0) { // has_selected_step_node
        log_skip_once("orphan node (no parent folder)");
        return;
    }

    const int current_folder = find_parent_folder(m_selected_node);
    if (is_empty_structure_step(current_folder)) {
        return;
    }

    // Floating "Export" button sits to the LEFT of the panel and shares the
    render_assembly_guide_export_button(panel_x, panel_y, sc);

    ImGuiWrapper &imgui = *m_imgui;

    const ImU32 grey900    = is_dark ? IM_COL32(0xE0, 0xE0, 0xE0, 255) : IM_COL32(38, 46, 48, 255);
    const ImU32 grey700    = is_dark ? IM_COL32(0xA0, 0xA0, 0xA0, 255) : IM_COL32(107, 107, 107, 255);
    const ImU32 grey600    = is_dark ? IM_COL32(0x90, 0x90, 0x90, 255) : IM_COL32(144, 144, 144, 255);
    const ImU32 grey500    = is_dark ? IM_COL32(0x80, 0x80, 0x80, 255) : IM_COL32(172, 172, 172, 255);
    const ImU32 grey400    = is_dark ? IM_COL32(70, 70, 74, 255)       : IM_COL32(206, 206, 206, 255);
    const ImU32 grey300    = is_dark ? IM_COL32(60, 60, 64, 255)       : IM_COL32(238, 238, 238, 255);
    const ImU32 grey200    = is_dark ? IM_COL32(50, 50, 54, 255)       : IM_COL32(248, 248, 248, 255);
    const ImU32 white_col  = is_dark ? IM_COL32(55, 55, 59, 255)       : IM_COL32(255, 255, 255, 255);
    const ImU32 brand_col  = IM_COL32(0, 174, 66, 255);

    const float font_sz      = ImGui::GetFontSize();
    const float small_fs     = std::max(font_sz * 0.77f, 10.0f * sc);
    const float section_gap  = 12.0f * sc;
    const float card_pad     = 9.0f * sc;
    const float card_rounding = 4.0f * sc;
    const float card_w       = panel_w - 2.0f * card_pad;

    // Pre-compute total content height for adaptive window sizing
    const float header_h = 36.0f * sc;

    const float ct_icon_sz  = 24.0f * sc;
    // Use native font size for crisp text rendering (ImGui rasterizes at this size).
    const float ct_label_fs = font_sz;
    const std::string ct_screw_str = _u8L("Screw");
    const ImVec2 ct_label_max = ImGui::GetFont()->CalcTextSizeA(ct_label_fs, FLT_MAX, 0.0f,
        ct_screw_str.c_str());
    const float ct_item_w   = std::max(ct_icon_sz + 28.0f * sc, ct_label_max.x + 16.0f * sc);
    const float ct_item_h   = 8.0f * sc + ct_icon_sz + 6.0f * sc + ct_label_max.y + 6.0f * sc;
    const float ct_gap      = 10.0f * sc;
    const float ct_pad      = 10.0f * sc;
    const float ct_cont_h   = ct_item_h + 2.0f * ct_pad;
    const float title_h_ct  = font_sz + 12.0f * sc;

    const float color_row_h_an  = 22.4f * sc;
    const float color_row_gap_an = 6.0f * sc;

    const float btn_sz_an   = 30.0f * sc;
    const float title_h_an  = font_sz + 12.0f * sc;
    // ArrowSvg (glue/clip/screw) notes are created from the "Connection Type"
    const bool show_connection_color_control = is_note_edit_controls_visible() &&
        m_note_selected_type == AssemblyNoteSelectionType::ArrowSvg;
    // Color control for the Add-Notes tools (rect / circle / arrow / text).
    const bool show_note_color_control = is_note_edit_controls_visible() &&
        note_tool_index_from_selection(m_note_selected_type) >= 0;
    // The background-color row only applies to TextLabel notes.
    const bool  show_note_bg_color_control = show_note_color_control &&
        m_note_selected_type == AssemblyNoteSelectionType::TextLabel;
    const float card_h_ct   = title_h_ct + ct_cont_h + 8.0f * sc +
        (show_connection_color_control ? color_row_gap_an + color_row_h_an : 0.0f);
    const float card_h_an   = title_h_an + btn_sz_an +
        (show_note_color_control ? color_row_gap_an + color_row_h_an : 0.0f) +
        (show_note_bg_color_control ? color_row_gap_an + color_row_h_an : 0.0f) +
        12.0f * sc;

    auto       *sp_entries     = get_current_kf_entries();
    // Title / description follow the currently selected label-show type so the
    // section name matches what the menu (auto / objects only / parts only) shows.
    std::string sp_title_str;
    std::string desc_sp_str;
    switch (m_cur_labels_show_type) {
    case LabelsShowType::OnlyModelObject:
        sp_title_str = _u8L("Show Object labels");
        desc_sp_str  = _u8L("Show object names on the model");
        break;
    case LabelsShowType::OnlyModelVolume:
        sp_title_str = _u8L("Show Part labels");
        desc_sp_str  = _u8L("Show part names on models");
        break;
    case LabelsShowType::AutoRecommend:
    default:
        sp_title_str = _u8L("Show Object/Part labels");
        desc_sp_str  = _u8L("Show object and part names  on models");
        break;
    }
    const float desc_sp_wrap = card_w - 16.0f * sc;
    const ImVec2 desc_sp_size = ImGui::GetFont()->CalcTextSizeA(small_fs, FLT_MAX, desc_sp_wrap,
        desc_sp_str.c_str(), nullptr, nullptr);
    const float card_h_sp   = font_sz + 14.0f * sc + desc_sp_size.y + 8.0f * sc;

    const float thumb_h_tl  = 48.0f * sc;

    // Endframe tip: surfaced under the timeline thumbs whenever the user has
    std::string endframe_tip_str;
    {
        const int   tip_folder      = find_parent_folder(m_selected_node);
        auto       *tip_entries     = sp_entries;
        const auto &tip_step_nodes  = m_model->get_assembly_steps_tree_data().nodes;
        const bool  endframe_selected =
            tip_folder >= 0 && tip_folder < (int)tip_step_nodes.size() &&
            tip_entries != nullptr &&
            m_keyframe_selected >= 0 && m_keyframe_selected < (int)tip_entries->size() &&
            (*tip_entries)[m_keyframe_selected].is_last();
        if (endframe_selected) {
            endframe_tip_str = tip_step_nodes[tip_folder].is_final_assembly ?
                                   (_u8L("Note") + ":" + _u8L("Pose changes (e.g. move gizmo) made on the final-assembly step's keyframe will affect the actual assembly display.")) :
                                   (_u8L("Note") + ":" + _u8L("Pose changes (e.g. move gizmo) made on this step's keyframe do not affect the actual assembly display.") +
                                    _u8L("If necessary, a relatively good position can be restored by pressing the \"apply actual assembly pose\" button at present.") +
                                                       _u8L("undoing these edits is not yet supported and will be added in a later version."));
        }
    }
    const float endframe_tip_wrap = card_w - 16.0f * sc;
    ImVec2      endframe_tip_size = endframe_tip_str.empty()
                                    ? ImVec2(0.0f, 0.0f)
                                    : ImGui::GetFont()->CalcTextSizeA(small_fs, FLT_MAX, endframe_tip_wrap,
                                                                       endframe_tip_str.c_str(), nullptr, nullptr);

    // Truncate to at most 3 wrapped lines; the full string is shown on hover
    const float max_3line_h    = small_fs * 3.0f + 0.5f;
    bool        tip_truncated  = false;
    std::string tip_display_str = endframe_tip_str;
    if (!endframe_tip_str.empty() && endframe_tip_size.y > max_3line_h) {
        const std::string ellipsis = "...";
        std::string       best;
        size_t            i = 0;
        while (i < endframe_tip_str.size()) {
            const unsigned char b   = static_cast<unsigned char>(endframe_tip_str[i]);
            size_t              adv = 1;
            if      ((b & 0x80) == 0x00) adv = 1;
            else if ((b & 0xE0) == 0xC0) adv = 2;
            else if ((b & 0xF0) == 0xE0) adv = 3;
            else if ((b & 0xF8) == 0xF0) adv = 4;
            if (i + adv > endframe_tip_str.size())
                break;

            std::string  candidate = endframe_tip_str.substr(0, i + adv) + ellipsis;
            const ImVec2 cand_sz   = ImGui::GetFont()->CalcTextSizeA(
                small_fs, FLT_MAX, endframe_tip_wrap, candidate.c_str(), nullptr, nullptr);
            if (cand_sz.y > max_3line_h)
                break;
            best = std::move(candidate);
            i += adv;
        }
        if (!best.empty()) {
            tip_display_str  = std::move(best);
            endframe_tip_size = ImGui::GetFont()->CalcTextSizeA(
                small_fs, FLT_MAX, endframe_tip_wrap, tip_display_str.c_str(), nullptr, nullptr);
            tip_truncated = true;
        }
    }

    const float endframe_tip_extra_h = endframe_tip_str.empty()
                                           ? 0.0f
                                           : (endframe_tip_size.y + 8.0f * sc);

    const float card_h_tl   = font_sz + 12.0f * sc + thumb_h_tl + small_fs + 16.0f * sc + endframe_tip_extra_h;

    const float total_content = 8.0f * sc
        + card_h_ct + section_gap
        + card_h_an + section_gap
        + card_h_sp + section_gap
        + card_h_tl + 8.0f * sc;
    // When collapsed, only the header bar is visible. Add a small safety buffer
    // (expanded mode) so the bottom card never gets clipped due to sub-pixel
    // rounding or any residual ImGui spacing.
    const float desired_h = m_guide_panel_collapsed
        ? header_h
        : header_h + total_content + 4.0f * sc;

    // Block canvas interaction only over the panel's real footprint, not the
    m_panel_rect_guide_min = ImVec2(panel_x, panel_y);
    m_panel_rect_guide_max = ImVec2(panel_x + panel_w, panel_y + desired_h);

    ImGui::SetNextWindowPos(ImVec2(panel_x, panel_y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panel_w, desired_h), ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f * sc);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    // Zero ItemSpacing so Dummy heights match our desired_h budget exactly.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, is_dark ? ImVec4(55/255.f, 55/255.f, 59/255.f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

    imgui.begin(std::string("##assembly_guide_panel"),
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings);

    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    const ImVec2 win_pos = ImGui::GetWindowPos();

    {
        const ImVec2 header_min = win_pos;
        const ImVec2 header_max(win_pos.x + panel_w, win_pos.y + header_h);
        const ImU32 grad_top = is_dark ? IM_COL32(48, 48, 52, 255) : IM_COL32(248, 248, 248, 255);
        const ImU32 grad_bot = is_dark ? IM_COL32(42, 42, 46, 255) : IM_COL32(241, 241, 241, 255);
        draw_list->AddRectFilled(header_min, header_max, grad_top, 4.0f * sc,
            m_guide_panel_collapsed ? ImDrawFlags_RoundCornersAll : ImDrawFlags_RoundCornersTop);
        draw_list->AddRectFilledMultiColor(header_min, header_max, grad_top, grad_top, grad_bot, grad_bot);

        // Collapse / expand toggle button on the LEFT of the title (Figma node 691:17048).
        const float toggle_sz   = 16.0f * sc;
        const float toggle_pad  = 10.0f * sc;
        const ImVec2 toggle_min(win_pos.x + toggle_pad,
                                win_pos.y + (header_h - toggle_sz) * 0.5f);
        const ImVec2 toggle_max(toggle_min.x + toggle_sz, toggle_min.y + toggle_sz);

        ImTextureID toggle_icon = m_guide_panel_collapsed ? m_panel_expand_icon
                                                          : m_panel_collapse_icon;
        if (toggle_icon) {
            draw_list->AddImage(toggle_icon, toggle_min, toggle_max);
        } else {
            // Fallback: simple chevron drawn inline so the toggle still works
            // before the SVGs finish loading.
            const float cx = (toggle_min.x + toggle_max.x) * 0.5f;
            const float cy = (toggle_min.y + toggle_max.y) * 0.5f;
            const float r  = toggle_sz * 0.25f;
            const ImU32 col = grey700;
            if (m_guide_panel_collapsed) {
                draw_list->AddTriangleFilled(ImVec2(cx - r, cy - r),
                    ImVec2(cx - r, cy + r), ImVec2(cx + r, cy), col);
            } else {
                draw_list->AddTriangleFilled(ImVec2(cx + r, cy - r),
                    ImVec2(cx + r, cy + r), ImVec2(cx - r, cy), col);
            }
        }

        // Invisible hit area for the toggle.
        ImGui::SetCursorScreenPos(toggle_min);
        ImGui::PushID("##guide_panel_toggle");
        ImGui::InvisibleButton("##t", ImVec2(toggle_sz, toggle_sz));
        if (ImGui::IsItemClicked(0)) {
            m_guide_panel_collapsed = !m_guide_panel_collapsed;
            do_commond_callback(m_guide_panel_collapsed
                ? "guide_panel:collapse" : "guide_panel:expand");
        }
        if (ImGui::IsItemHovered()) {
            // Subtle hover indicator: light overlay rectangle.
            draw_list->AddRectFilled(toggle_min, toggle_max,
                IM_COL32(38, 46, 48, 18), 3.0f * sc);
            render_panel_tooltip(m_guide_panel_collapsed ? _u8L("Expand") : _u8L("Collapse"));
        }
        ImGui::PopID();

        // Title shifts right to make room for the toggle icon.
        const float title_x = toggle_max.x + 6.0f * sc;
        const ImVec2 title_pos(title_x, win_pos.y + (header_h - font_sz) * 0.5f);
        draw_list->AddText(ImGui::GetFont(), font_sz, title_pos, grey900,
            _u8L("Assembly Guide").c_str());

        ImGui::SetCursorScreenPos(ImVec2(win_pos.x, win_pos.y + header_h));
    }

    // When collapsed, hide every section; the header bar is the only visible chrome.
    if (m_guide_panel_collapsed) {
        imgui.end();
        ImGui::PopStyleColor(1);
        ImGui::PopStyleVar(4);
        return;
    }

    ImGui::Dummy(ImVec2(0, 8.0f * sc));

    auto section_begin = [&](const char *title, float height) {
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        const ImVec2 card_min(cursor.x + card_pad, cursor.y);
        const ImVec2 card_max(card_min.x + card_w, card_min.y + height);

        draw_list->AddRectFilled(card_min, card_max, grey200, card_rounding);
        draw_list->AddRect(card_min, card_max, grey300, card_rounding);

        const ImVec2 title_pos(card_min.x + 8.0f * sc, card_min.y + 6.0f * sc);
        draw_list->AddText(ImGui::GetFont(), font_sz, title_pos, grey900, title);

        return card_min;
    };

    // === Section 1: Connection Type ===
    {
        ImVec2 card_min = section_begin(_u8L("Connection Type").c_str(), card_h_ct);

        const float cont_y = card_min.y + title_h_ct;
        const float total_w = 3.0f * ct_item_w + 2.0f * ct_gap;
        const ImVec2 cont_min(card_min.x + 8.0f * sc, cont_y);
        const ImVec2 cont_max(cont_min.x + total_w + 2.0f * ct_pad, cont_min.y + ct_cont_h);
        draw_list->AddRectFilled(cont_min, cont_max, grey300, card_rounding);

        const std::string ct_labels[] = {
            _u8L("Clip"),
            _u8L("Glue"),
            _u8L("Screw")
        };
        const std::string ct_tooltips[] = {
            _u8L("Add Clip"),
            _u8L("Add Glue"),
            _u8L("Add Screw")
        };
        ImTextureID ct_icons[] = {
            m_is_dark ? m_tree_icon_clip_dark : m_tree_icon_clip,
            m_is_dark ? m_tree_icon_glue_dark : m_tree_icon_glue,
            m_is_dark ? m_tree_icon_screw_dark : m_tree_icon_screw
        };

        for (int i = 0; i < 3; i++) {
            const float bx = cont_min.x + ct_pad + i * (ct_item_w + ct_gap);
            const float by = cont_min.y + ct_pad;
            bool sel = (m_guide_connection_selected == i);
            if (render_connection_type_btn(draw_list, bx, by, ct_item_w, ct_item_h,
                    ct_icons[i], ct_labels[i].c_str(), ct_icon_sz, ct_label_fs, sc,
                    sel, grey700, brand_col, ct_tooltips[i].c_str())) {
                m_guide_connection_selected = sel ? -1 : i;
                // Each connection type has its own action: Glue / Screw add an
                // arrow-svg note (same as the keyframe note-edit toolbar buttons),
                // Clip emits a generic command for higher layers to handle.
                switch (i) {
                case 0: add_arrow_svg_note("clip"); break;
                case 1: add_arrow_svg_note("glue");  break;
                case 2: add_arrow_svg_note("screw"); break;
                default: break;
                }
            }
        }

        // When a glue/clip/screw note is selected, show its color control right
        if (show_connection_color_control) {
            auto *entries = get_current_kf_entries();
            if (entries && m_keyframe_selected >= 0 && m_keyframe_selected < (int)entries->size()) {
                const AssemblyNote &note = (*entries)[m_keyframe_selected].data.assembly_note;
                if (m_note_selected_idx >= 0 && m_note_selected_idx < (int)note.arrow_svgs.size())
                    m_guide_note_color_selected = note_palette_index_from_color(note.arrow_svgs[m_note_selected_idx].color);
            }
            const float color_row_y = cont_max.y + color_row_gap_an;
            render_note_color_control(draw_list, cont_min.x, color_row_y, sc);
        }

        ImGui::SetCursorScreenPos(ImVec2(card_min.x, card_min.y + card_h_ct));
        ImGui::Dummy(ImVec2(card_w, 0));
        ImGui::Dummy(ImVec2(0, section_gap));
    }
    // === Section 2: Add Notes ===
    {
        ImVec2 card_min = section_begin(_u8L("Add Notes").c_str(), card_h_an);

        const float tools_y = card_min.y + title_h_an;
        float btn_x = card_min.x + 8.0f * sc;
        const float btn_gap = 6.0f * sc;

        // Lazy-init the note-tool table on first use. Cannot be a static const
        // because the action lambdas must capture `this` and the icon textures
        // are loaded after construction. Three tools currently map to existing
        // note-creation helpers; commented-out rows are placeholders for
        // future shape-tool support (rect / line / pencil).
        if (m_note_tools.empty()) {
            // NOTE: button order MUST mirror `note_tool_index_from_selection`.
            m_note_tools = {
                {"##nt_rect",   m_note_icon_rect,   m_note_icon_rect_dark,   _u8L("Add Rectangle"),
                    [this]() { add_rectangle_note(); }},
                {"##nt_circle", m_note_icon_circle, m_note_icon_circle_dark, _u8L("Add Circle"),
                    [this]() { add_circle_note(); }},
                //{"##nt_line",   m_note_icon_line,   _u8L("Line"),      nullptr},
                {"##nt_vector", m_note_icon_vector, m_note_icon_vector_dark, _u8L("Add Arrow"),
                    [this]() { add_plain_arrow_note(); }},
                {"##nt_tag",    m_note_icon_tag,    m_note_icon_tag_dark,    _u8L("Add Text"),
                    [this]() { add_text_label_note(); }},
                //{"##nt_pencil", m_note_icon_pencil, _u8L("Pencil"),    nullptr},
            };
        }

        m_guide_note_tool_selected = is_note_edit_controls_visible()
            ? note_tool_index_from_selection(m_note_selected_type)
            : -1;
        if (show_note_color_control) {
            auto *entries = get_current_kf_entries();
            if (entries && m_keyframe_selected >= 0 && m_keyframe_selected < (int)entries->size()) {
                const AssemblyNote &note = (*entries)[m_keyframe_selected].data.assembly_note;
                if (m_note_selected_type == AssemblyNoteSelectionType::TextLabel &&
                    m_note_selected_idx >= 0 && m_note_selected_idx < (int)note.text_labels.size()) {
                    m_guide_note_color_selected    = note_palette_index_from_color(note.text_labels[m_note_selected_idx].color);
                    m_guide_note_bg_color_selected = note_palette_index_from_color(note.text_labels[m_note_selected_idx].background_color);
                } else if (m_note_selected_type == AssemblyNoteSelectionType::Circle &&
                         m_note_selected_idx >= 0 && m_note_selected_idx < (int)note.circle_notes.size())
                    m_guide_note_color_selected = note_palette_index_from_color(note.circle_notes[m_note_selected_idx].color);
                else if (m_note_selected_type == AssemblyNoteSelectionType::Rectangle &&
                         m_note_selected_idx >= 0 && m_note_selected_idx < (int)note.rectangle_notes.size())
                    m_guide_note_color_selected = note_palette_index_from_color(note.rectangle_notes[m_note_selected_idx].color);
                else if (m_note_selected_type == AssemblyNoteSelectionType::PlainArrow &&
                         m_note_selected_idx >= 0 && m_note_selected_idx < (int)note.plain_arrows.size())
                    m_guide_note_color_selected = note_palette_index_from_color(note.plain_arrows[m_note_selected_idx].color);
            }
        }
        for (int i = 0; i < (int) m_note_tools.size(); ++i) {
            const NoteTool &tool = m_note_tools[i];
            bool sel = (m_guide_note_tool_selected == i);
            ImTextureID icon = (m_is_dark && tool.icon_dark) ? tool.icon_dark : tool.icon;
            bool clicked = render_note_tool_btn(draw_list, btn_x, tools_y, btn_sz_an,
                icon, sel, tool.id, sc, tool.tip.c_str());
            if (clicked) {
                set_selection_origin(SelectionOrigin::ImGuiNote);
                if (sel) {
                    exit_note_edit();
                } else if (tool.action) {
                    // Picking a note tool cancels any active Connection Type highlight.
                    m_guide_connection_selected = -1;
                    tool.action();
                    m_guide_note_tool_selected = is_note_edit_controls_visible()
                        ? note_tool_index_from_selection(m_note_selected_type)
                        : -1;
                }
            }
            btn_x += btn_sz_an + btn_gap;
        }
        if (show_note_color_control) {
            const float color_row_y = tools_y + btn_sz_an + color_row_gap_an;
            const float palette_left_x = card_min.x + 8.0f * sc;

            if (show_note_bg_color_control) {
                // Text label tool gets two color rows; prefix each row with a
                // small "Text" / "Background" caption so the user can tell them
                // apart. Reserve the larger of the two label widths so the two
                // palettes start at the same x.
                const std::string fg_caption = _u8L("Text");
                const std::string bg_caption = _u8L("Background");
                ImFont *font_ptr = ImGui::GetFont();
                const ImVec2 fg_sz = font_ptr->CalcTextSizeA(small_fs, FLT_MAX, 0.0f, fg_caption.c_str());
                const ImVec2 bg_sz = font_ptr->CalcTextSizeA(small_fs, FLT_MAX, 0.0f, bg_caption.c_str());
                const float caption_gap = 4.0f * sc;
                const float caption_w   = std::max(fg_sz.x, bg_sz.x);
                const float palette_x   = palette_left_x + caption_w + caption_gap;

                const float bg_row_y    = color_row_y + color_row_h_an + color_row_gap_an;
                const float fg_caption_y = color_row_y + (color_row_h_an - fg_sz.y) * 0.5f;
                const float bg_caption_y = bg_row_y    + (color_row_h_an - bg_sz.y) * 0.5f;
                const ImU32 caption_col = grey700;
                draw_list->AddText(font_ptr, small_fs,
                    ImVec2(palette_left_x, fg_caption_y), caption_col, fg_caption.c_str());
                draw_list->AddText(font_ptr, small_fs,
                    ImVec2(palette_left_x, bg_caption_y), caption_col, bg_caption.c_str());

                render_note_color_control(draw_list, palette_x, color_row_y, sc);
                render_note_bg_color_control(draw_list, palette_x, bg_row_y, sc);
            } else {
                render_note_color_control(draw_list, palette_left_x, color_row_y, sc);
            }
        }

        ImGui::SetCursorScreenPos(ImVec2(card_min.x, card_min.y + card_h_an));
        ImGui::Dummy(ImVec2(card_w, 0));
        ImGui::Dummy(ImVec2(0, section_gap));
    }

    // === Section 3: Show Part Numbers ===
    {
        ImVec2 card_min = section_begin(sp_title_str.c_str(), card_h_sp);

        // Whole-card click opens the label-type menu. Submitted before the
        // checkbox (+ SetItemAllowOverlap) so the checkbox still wins its own
        // top-right corner; clicks over the checkbox are filtered out below.
        const char *sp_menu_popup_id = "##assembly_labels_show_type_menu";
        ImGui::SetCursorScreenPos(card_min);
        ImGui::PushID("##sp_card_menu");
        const bool sp_card_clicked = ImGui::InvisibleButton("##sp_card", ImVec2(card_w, card_h_sp));
        ImGui::SetItemAllowOverlap();
        ImGui::PopID();

        const float cb_sz = 16.0f * sc;
        const float cb_x  = card_min.x + card_w - 8.0f * sc - cb_sz;
        const float cb_y  = card_min.y + 6.0f * sc;
        if (render_checkbox(draw_list, cb_x, cb_y, cb_sz,
                &m_guide_show_part_numbers, "##show_pn", sc)) {
            toggle_part_number_labels();
        }
        if (ImGui::IsMouseHoveringRect(ImVec2(cb_x, cb_y), ImVec2(cb_x + cb_sz, cb_y + cb_sz))) {
            std::string pn_tip = _u8L("Automatically select the optimal camera perspective and auto-arrange auto recommend labels each time the checkbox is re-ticked.");
            bool cur_is_end_frame = false;
            if (auto *pn_entries = get_current_kf_entries();
                pn_entries && m_keyframe_selected >= 0 && m_keyframe_selected < (int) pn_entries->size())
                cur_is_end_frame = (*pn_entries)[m_keyframe_selected].is_last();
            //if (!cur_is_end_frame)
               // pn_tip += " " + std::string(_u8L("The final frame will also automatically optimize the camera angle and label layout."));
            render_panel_tooltip(pn_tip);
        }

        // Open the label-type menu on a card click that did not land on the checkbox.
        const bool over_cb = ImGui::IsMouseHoveringRect(ImVec2(cb_x, cb_y), ImVec2(cb_x + cb_sz, cb_y + cb_sz));
        if (sp_card_clicked && !over_cb) {
            ImGui::OpenPopup(sp_menu_popup_id);
        }
        render_labels_show_type_menu_popup(sp_menu_popup_id, sc);

        draw_list->AddText(ImGui::GetFont(), small_fs,
            ImVec2(card_min.x + 8.0f * sc, card_min.y + font_sz + 14.0f * sc),
            grey500, desc_sp_str.c_str(), nullptr, desc_sp_wrap);

        ImGui::SetCursorScreenPos(ImVec2(card_min.x, card_min.y + card_h_sp));
        ImGui::Dummy(ImVec2(card_w, 0));
        ImGui::Dummy(ImVec2(0, section_gap));
    }

    // === Section 4: Timeline ===
    {
        const float thumb_w   = 65.0f * sc;
        const float thumb_h   = thumb_h_tl;
        const float thumb_gap = 14.0f * sc;
        const float title_h   = font_sz + 12.0f * sc;

        const std::string tl_title = _u8L("Timeline");
        ImVec2 card_min = section_begin(tl_title.c_str(), card_h_tl);
        const float thumb_y = card_min.y + title_h;

        // Inline "Play" button to the right of the title text (10px spacing).
        float next_btn_x = 0.f;
        ImTextureID play_icon = m_keyframe_playing ? m_tree_icon_pause : (m_is_dark ? m_tree_icon_play_dark : m_tree_icon_play);
        if (play_icon) {
            auto *tl_entries = get_current_kf_entries();
            const bool can_play_current_node = tl_entries && tl_entries->size() >= 2;
            const ImVec2 title_sz = ImGui::GetFont()->CalcTextSizeA(font_sz,
                FLT_MAX, 0.0f, tl_title.c_str());
            const float btn_sz = font_sz;                        // square; matches title height
            const float btn_x  = card_min.x + 8.0f * sc + title_sz.x + 10.0f * sc;
            const float btn_y  = card_min.y + 6.0f * sc + (title_sz.y - btn_sz) * 0.5f;

            const ImU32 icon_tint = can_play_current_node ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 128);
            draw_list->AddImage(play_icon,
                ImVec2(btn_x, btn_y), ImVec2(btn_x + btn_sz, btn_y + btn_sz),
                ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), icon_tint);

            ImGui::SetCursorScreenPos(ImVec2(btn_x, btn_y));
            ImGui::PushID("##tl_play_inline");
            imgui.disabled_begin(!can_play_current_node);
            ImGui::InvisibleButton("##p", ImVec2(btn_sz, btn_sz));
            if (ImGui::IsItemClicked(0)) {
                if (m_keyframe_playing)
                    pause_playback();
                else if (m_playback_paused)
                    resume_playback();
                else
                    play_all_keyframes_for_current_node();
            }
            if (ImGui::IsItemHovered())
                render_panel_tooltip(can_play_current_node ? (m_keyframe_playing ? _u8L("Pause") : _u8L("Play all frames for current step")) :
                    _u8L("At least two keyframes are required to play."));
            imgui.disabled_end();
            ImGui::PopID();
            next_btn_x = btn_x + btn_sz + 6.0f * sc;
        }

        const float right_btn_sz  = font_sz;
        const float right_btn_gap = 6.0f * sc;

        const int auto_explode_folder = find_parent_folder(m_selected_node);
        auto *auto_explode_entries = get_current_kf_entries();
        const auto &auto_explode_nodes = m_model->get_assembly_steps_tree_data().nodes;
        const bool auto_explode_is_final =
            auto_explode_folder >= 0 &&
            auto_explode_folder < static_cast<int>(auto_explode_nodes.size()) &&
            auto_explode_nodes[auto_explode_folder].is_final_assembly;
        const bool auto_explode_is_end =
            auto_explode_entries != nullptr &&
            m_keyframe_selected >= 0 &&
            m_keyframe_selected < static_cast<int>(auto_explode_entries->size()) &&
            (*auto_explode_entries)[m_keyframe_selected].is_last();
        ImTextureID auto_explode_icon = (m_is_dark && m_tree_icon_auto_explode_dark) ?
            m_tree_icon_auto_explode_dark : m_tree_icon_auto_explode;
        const bool show_auto_explode =
            auto_explode_icon != nullptr &&
            auto_explode_folder >= 0 &&
            auto_explode_entries != nullptr &&
            m_keyframe_selected >= 0 &&
            m_keyframe_selected < static_cast<int>(auto_explode_entries->size()) &&
            (!auto_explode_is_final || !auto_explode_is_end);

        const int   from_fae_folder = find_parent_folder(m_selected_node);
        auto       *from_fae_entries = get_current_kf_entries();
        const auto &fae_nodes        = m_model->get_assembly_steps_tree_data().nodes;
        ImTextureID from_fae_icon = (m_is_dark && m_tree_icon_from_assembly_end_frame_dark) ?
            m_tree_icon_from_assembly_end_frame_dark : m_tree_icon_from_assembly_end_frame;
        const bool show_from_fae =
            from_fae_icon != nullptr &&
            from_fae_folder >= 0 && from_fae_folder < (int)fae_nodes.size() &&
            !fae_nodes[from_fae_folder].is_final_assembly &&
            from_fae_entries != nullptr &&
            m_keyframe_selected >= 0 &&
            m_keyframe_selected < (int)from_fae_entries->size();

        ImTextureID apply_camera_icon = (m_is_dark && m_tree_icon_apply_camera_dark) ?
            m_tree_icon_apply_camera_dark : m_tree_icon_apply_camera;
        const bool show_apply_camera = apply_camera_icon != nullptr;

        const int right_btn_count = (show_auto_explode ? 1 : 0) + (show_from_fae ? 1 : 0) + (show_apply_camera ? 1 : 0);
        float right_btn_x = right_btn_count > 0
            ? card_min.x + card_w - 8.0f * sc - right_btn_count * right_btn_sz - (right_btn_count - 1) * right_btn_gap
            : 0.0f;
        auto take_right_btn_x = [&]() {
            const float x = right_btn_x;
            right_btn_x += right_btn_sz + right_btn_gap;
            return x;
        };

        // "Auto explode" button: pushes the current frame's objects/parts
        // outward by their dominant direction from the current overall bbox.
        {
            if (show_auto_explode) {
                const ImVec2 title_sz = ImGui::GetFont()->CalcTextSizeA(font_sz,
                    FLT_MAX, 0.0f, tl_title.c_str());
                const float btn_sz = right_btn_sz;
                const float btn_x  = take_right_btn_x();
                const float btn_y  = card_min.y + 6.0f * sc + (title_sz.y - btn_sz) * 0.5f;

                draw_list->AddImage(auto_explode_icon,
                    ImVec2(btn_x, btn_y), ImVec2(btn_x + btn_sz, btn_y + btn_sz));

                ImGui::SetCursorScreenPos(ImVec2(btn_x, btn_y));
                ImGui::PushID("##tl_auto_explode");
                ImGui::InvisibleButton("##ae", ImVec2(btn_sz, btn_sz));
                if (ImGui::IsItemClicked(0))
                    auto_explode_current_keyframe();
                if (ImGui::IsItemHovered())
                    render_panel_tooltip(auto_explode_is_final ?
                        _u8L("Automatically explode all objects.") :
                        _u8L("Automatically explode all parts."));//Excluding some previously appeared objects
                ImGui::PopID();
            }
        }

        // "Apply from final-assembly end frame": pulls the live assembled
        {
            if (show_from_fae) {
                const ImVec2 title_sz = ImGui::GetFont()->CalcTextSizeA(font_sz,
                    FLT_MAX, 0.0f, tl_title.c_str());
                const float btn_sz = right_btn_sz;
                const float btn_x  = take_right_btn_x();
                const float btn_y  = card_min.y + 6.0f * sc + (title_sz.y - btn_sz) * 0.5f;

                // Greyed out when the current keyframe's pose already
                const bool  fae_disabled = current_keyframe_matches_final_assembly_end_frame_transforms();
                const ImU32 fae_tint     = fae_disabled
                    ? IM_COL32(255, 255, 255, 128)
                    : IM_COL32_WHITE;
                draw_list->AddImage(from_fae_icon,
                    ImVec2(btn_x, btn_y), ImVec2(btn_x + btn_sz, btn_y + btn_sz),
                    ImVec2(0, 0), ImVec2(1, 1), fae_tint);

                ImGui::SetCursorScreenPos(ImVec2(btn_x, btn_y));
                ImGui::PushID("##tl_apply_from_assembly_end");
                ImGui::InvisibleButton("##fae", ImVec2(btn_sz, btn_sz));
                if (!fae_disabled && ImGui::IsItemClicked(0))
                    apply_final_assembly_end_frame_transforms_to_current_keyframe();
                if (ImGui::IsItemHovered()) {
                    // Localized counterpart in zh_CN should read along the
                    render_panel_tooltip(_u8L("Apply final-assembly pose onto the current step"));
                }
                ImGui::PopID();
            }
        }

        // "Apply camera" button: applies current camera to all frames in the step.
        if (show_apply_camera) {
            const ImVec2 title_sz = ImGui::GetFont()->CalcTextSizeA(font_sz,
                FLT_MAX, 0.0f, tl_title.c_str());
            const float btn_sz = right_btn_sz;
            const float btn_x  = take_right_btn_x();
            const float btn_y  = card_min.y + 6.0f * sc + (title_sz.y - btn_sz) * 0.5f;

            draw_list->AddImage(apply_camera_icon,
                ImVec2(btn_x, btn_y), ImVec2(btn_x + btn_sz, btn_y + btn_sz));

            ImGui::SetCursorScreenPos(ImVec2(btn_x, btn_y));
            ImGui::PushID("##tl_apply_camera");
            ImGui::InvisibleButton("##ac", ImVec2(btn_sz, btn_sz));
            if (ImGui::IsItemClicked(0)) {
                int folder = find_parent_folder(m_selected_node);
                auto &tree_nodes = m_model->get_assembly_steps_tree_data().nodes;
                if (folder >= 0 && folder < (int) tree_nodes.size()) {
                    auto &entries = tree_nodes[folder].kf_data.entries;
                    for (auto &entry : entries) {
                        record_camera(entry.data);
                        entry.need_save = true;
                        save_assembly_steps_json_to_model();
                    }
                }
            }
            if (ImGui::IsItemHovered())
                render_panel_tooltip(_u8L("Apply current camera angle to all frames of the current step"));
            ImGui::PopID();
        }

        //{//no use
        //    const int current_folder = find_parent_folder(m_selected_node);
        //    auto *current_entries = get_current_kf_entries();
        //    const auto &step_nodes = m_model->get_assembly_steps_tree_data().nodes;
        //    const bool show_apply_start_transforms =
        //        current_folder >= 0 && current_folder < (int)step_nodes.size() &&
        //        step_nodes[current_folder].is_final_assembly &&
        //        current_entries != nullptr &&
        //        m_keyframe_selected >= 0 &&
        //        m_keyframe_selected < (int)current_entries->size() &&
        //        !(*current_entries)[m_keyframe_selected].is_last();

        //    auto render_start_transform_button = [&](ImTextureID icon, const char *id, const std::string &tip,
        //                                             bool include_volume_transforms) {
        //        if (!show_apply_start_transforms || !icon || next_btn_x <= 0.f)
        //            return;

        //        const ImVec2 title_sz = ImGui::GetFont()->CalcTextSizeA(font_sz,
        //            FLT_MAX, 0.0f, tl_title.c_str());
        //        const float btn_sz = font_sz;
        //        const float btn_y  = card_min.y + 6.0f * sc + (title_sz.y - btn_sz) * 0.5f;

        //        draw_list->AddImage(icon,
        //            ImVec2(next_btn_x, btn_y), ImVec2(next_btn_x + btn_sz, btn_y + btn_sz));

        //        ImGui::SetCursorScreenPos(ImVec2(next_btn_x, btn_y));
        //        ImGui::PushID(id);
        //        ImGui::InvisibleButton("##btn", ImVec2(btn_sz, btn_sz));
        //        if (ImGui::IsItemClicked(0))
        //            apply_regular_steps_start_frame_transforms_to_current(include_volume_transforms);
        //        if (ImGui::IsItemHovered())
        //            render_panel_tooltip(tip, sc);
        //        ImGui::PopID();
        //        next_btn_x += btn_sz + 6.0f * sc;
        //    };

        //    render_start_transform_button(
        //        m_tree_icon_object,
        //        "##tl_apply_regular_start_objects",
        //        _u8L("Apply model object matrices from start frames of all non-final steps"),
        //        false);
        //    render_start_transform_button(
        //        m_tree_icon_part,
        //        "##tl_apply_regular_start_parts",
        //        _u8L("Apply model object and model volume matrices from start frames of all non-final steps"),
        //        true);
        //}

        // Render a real timeline driven by the current node's keyframe entries
        const float slot_x0    = card_min.x + 12.0f * sc;
        const float card_right = card_min.x + card_w - 4.0f * sc;

        auto *kf_entries = get_current_kf_entries();
        const int kf_count = kf_entries ? (int) kf_entries->size() : 0;

        // Cap is configurable via `m_keyframe_max_count` (3 by default). The
        // "+ add" slot disappears once we hit the cap.
        const int  kMaxTimelineKeyframes = std::max(2, m_keyframe_max_count);
        const bool show_add_slot         = (kf_count < kMaxTimelineKeyframes);

        // The "+ add" slot must always sit immediately before the end frame
        // (id == 0) when one exists, regardless of how many transition frames
        // already precede it. If there is no end frame yet (rare/transient
        // state) we fall back to appending it at the end of the row, which
        // also covers the empty-list case.
        int end_frame_idx = -1;
        if (kf_entries) {
            for (int i = 0; i < kf_count; ++i) {
                if ((*kf_entries)[i].is_last()) {
                    end_frame_idx = i;
                    break;
                }
            }
        }
        const int add_slot_pos =
            (end_frame_idx >= 0) ? end_frame_idx : kf_count;

        bool  entries_mutated = false;
        bool  add_slot_done   = false;
        float slot_x          = slot_x0;

        // Helper: render the "+" placeholder at the current slot_x and advance.
        auto render_add_slot = [&]() {
            if (slot_x + thumb_w > card_right) return;
            ImGui::PushID("##tl_add_kf");
            int r = render_timeline_keyframe(draw_list, slot_x, thumb_y,
                thumb_w, thumb_h,
                /*has_keyframe=*/ false, false,
                "", small_fs, sc);
            ImGui::PopID();
            // Hover tooltip mirrors the keyframe-slot tooltips below; uses a
            const ImVec2 slot_min(slot_x, thumb_y);
            const ImVec2 slot_max(slot_x + thumb_w, thumb_y + thumb_h);
            if (ImGui::IsMouseHoveringRect(slot_min, slot_max))
                render_panel_tooltip(_u8L("Add keyframe"));
            if (r == 1)
                insert_keyframe_after_selected();
            slot_x += thumb_w + thumb_gap;
            add_slot_done = true;
        };

        if (kf_entries) {
            for (int i = 0; i < (int) kf_entries->size(); ++i) {
                // Drop the "+ add" slot in front of the entry that lives at
                // `add_slot_pos` (== end frame index when one exists).
                if (show_add_slot && !add_slot_done && i == add_slot_pos)
                    render_add_slot();

                if (slot_x + thumb_w > card_right) break; // soft overflow guard
                auto &entry = (*kf_entries)[i];
                const bool is_last = entry.is_last();
                const bool is_sel  = (m_keyframe_selected == i);

                // Use index-scoped PushID so duplicate frame names (e.g. several
                // "transition frame" entries) don't collide on the inner buttons.
                ImGui::PushID(i + 50000);
                int r = render_timeline_keyframe(draw_list, slot_x, thumb_y,
                    thumb_w, thumb_h,
                    /*has_keyframe=*/ true, is_sel,
                    entry.data.name.c_str(), small_fs, sc,
                    /*show_delete_badge=*/ !is_last);
                ImGui::PopID();

                // Hover tooltip whose copy mirrors the next click action.
                // Geometry-based hit-test (instead of IsItemHovered) so it stays
                // accurate even when the delete badge is the last registered item.
                const ImVec2 slot_min(slot_x, thumb_y);
                const ImVec2 slot_max(slot_x + thumb_w, thumb_y + thumb_h);
                // IsMouseHoveringRect is a pure geometry test that ignores z-order,
                if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup) &&
                    ImGui::IsMouseHoveringRect(slot_min, slot_max)) {
                    render_panel_tooltip(is_sel
                        ? _u8L("Click to re-record this frame.\nEspecially after rotating the camera view, users want to save the current perspective")
                        : _u8L("Click to select this frame"));
                }

                if (r == 1) {
                    // Two-step click semantics:
                    m_selected_screen_center_dirty_ = true;//on_selected_keyframe_change
                    if (is_sel)
                        record_keyframe_at(i);
                    else
                        on_keyframe_list_item_clicked(i, entry);
                } else if (r == -1) {
                    m_keyframe_selected = i;
                    delete_selected_keyframe();

                    entries_mutated = true;
                    break;
                }
                slot_x += thumb_w + thumb_gap;
            }
        }

        // Trailing "+ add" slot: empty list, or no end frame in the entries
        // yet (so add_slot_pos == kf_count and the in-loop branch never fires).
        if (show_add_slot && !add_slot_done && !entries_mutated)
            render_add_slot();

        // Endframe tip (grey, wrapped) under the thumb row. Only rendered when
        if (!tip_display_str.empty()) {
            const float tip_y = thumb_y + thumb_h_tl + small_fs + 8.0f * sc;
            const float tip_x = card_min.x + 8.0f * sc;
            draw_list->AddText(ImGui::GetFont(), small_fs,
                ImVec2(tip_x, tip_y),
                grey500, tip_display_str.c_str(), nullptr, endframe_tip_wrap);
            if (tip_truncated) {
                // Hovering the truncated text reveals the original (un-clipped)
                // wording in the panel-style tooltip used by the rest of this
                // section.
                const ImVec2 tip_min(tip_x, tip_y);
                const ImVec2 tip_max(tip_x + endframe_tip_wrap, tip_y + endframe_tip_size.y);
                if (ImGui::IsMouseHoveringRect(tip_min, tip_max))
                    render_panel_tooltip(endframe_tip_str);
            }
        }

        ImGui::SetCursorScreenPos(ImVec2(card_min.x, card_min.y + card_h_tl));
        ImGui::Dummy(ImVec2(card_w, 0));
        ImGui::Dummy(ImVec2(0, section_gap));
    }

    ImGui::Dummy(ImVec2(0, 8.0f * sc));

    imgui.end();
    ImGui::PopStyleColor(1);
    ImGui::PopStyleVar(4);
}

void AssemblyStepsUtils::apply_assembly_tree_checked_to_step(
    int active_step_node,
    const AssemblyTreeData& tree,
    const std::unordered_map<std::string, bool>& checked)
{
    if (!m_model || active_step_node < 0)
        return;
    auto& steps_tree = m_model->get_assembly_steps_tree_data();
    if (active_step_node >= static_cast<int>(steps_tree.nodes.size()))
        return;

    // `checked` usually aliases steps_tree.nodes[active_step_node].assembly_tree_checked.
    // Adding object nodes below may reallocate steps_tree.nodes, so keep a stable copy.
    const std::unordered_map<std::string, bool> checked_snapshot = checked;

    // Object indices the user wants in THIS step (checked in the tree UI).
    std::set<int> checked_objects;
    for (const auto& node : tree.nodes) {
        if (!node.selectable || node.object_idx < 0)
            continue;
        auto checked_it = checked_snapshot.find(node.uid);
        if (checked_it != checked_snapshot.end() && checked_it->second)
            checked_objects.insert(node.object_idx);
    }

    // Whether this step had no parts before this edit. An empty step never carried
    bool folder_was_empty = true;
    for (int child_idx : steps_tree.nodes[active_step_node].children) {
        if (child_idx < 0 || child_idx >= static_cast<int>(steps_tree.nodes.size()))
            continue;
        const auto &child = steps_tree.nodes[child_idx];
        if (child.type == AssemblyStepsTreeNode::Type::Object && child.object_idx >= 0) {
            folder_was_empty = false;
            break;
        }
    }

    bool step_changed = false;
    std::set<int> existing_objects;

    // 1) Drop object children that got unchecked. Membership is per-step, so we
    //    only ever touch this folder's own children, never other steps' nodes.
    {
        auto& folder_children = steps_tree.nodes[active_step_node].children;
        std::vector<int> kept_children;
        kept_children.reserve(folder_children.size());
        for (int child_idx : folder_children) {
            if (child_idx < 0 || child_idx >= static_cast<int>(steps_tree.nodes.size()))
                continue;
            const auto& child = steps_tree.nodes[child_idx];
            if (child.type == AssemblyStepsTreeNode::Type::Object && child.object_idx >= 0) {
                if (checked_objects.find(child.object_idx) == checked_objects.end()) {
                    step_changed = true;
                    continue;
                }
                existing_objects.insert(child.object_idx);
            }
            kept_children.push_back(child_idx);
        }
        if (kept_children.size() != folder_children.size())
            folder_children = std::move(kept_children);
    }

    // 2) Add a fresh, step-owned object node for every newly checked object.
    std::vector<int> new_object_nodes;
    for (int object_idx : checked_objects) {
        if (existing_objects.find(object_idx) != existing_objects.end())
            continue;
        int object_node_idx = create_object_node(object_idx, get_object_name(object_idx), get_object_id_id(object_idx));
        if (object_node_idx < 0)
            continue;
        ensure_default_keyframe(object_node_idx);
        new_object_nodes.push_back(object_node_idx);
        step_changed = true;
    }
    if (!new_object_nodes.empty()) {
        // create_object_node may have reallocated steps_tree.nodes, so re-access
        // the children container instead of holding a stale reference.
        auto& children = steps_tree.nodes[active_step_node].children;
        children.insert(children.end(), new_object_nodes.begin(), new_object_nodes.end());
    }

    if (step_changed) {
        m_structure_select_labels.erase(active_step_node);
        m_structure_select_show_default.erase(active_step_node);
        m_structure_select_popup_tree_card = -1;
        m_structure_select_popup_tree_step_node = -1;
        m_structure_select_popup_tree.clear();
        m_structure_select_popup_checked_card = -1;
        m_structure_select_popup_checked.clear();
        sync_keyframe_tree();
        // The step's children just changed (e.g. an empty step that got objects
        // added). Only capture the matrix pose for objects added in this confirm;
        // objects that were already in the step keep their stored pose.
        fill_folder_keyframes_from_children(active_step_node,/*use_glvolume_tran*/ true);
        // A step that was empty before this confirm had no real camera (its default
        if (folder_was_empty && !checked_objects.empty()) {
            for (auto &entry : steps_tree.nodes[active_step_node].kf_data.entries) {
                record_camera(entry.data);
                entry.data.is_camera_define = true;
                entry.need_save            = true;
            }
        }
        save_assembly_steps_json_to_model();
        do_commond_callback("dirty");
        do_commond_callback("request_extra_frame");
    }

    if (m_selection) {
        set_selection_origin(SelectionOrigin::TreeNode);
        clear_selection();
        for (int object_idx : checked_objects) {
            m_selection->add_object(static_cast<unsigned int>(object_idx), false);
        }
    }
    m_assembly_tree_ui_original_checked = checked_snapshot;
    invalidate_play_frame_refs();
    save_assembly_steps_json_to_model();
    apply_keyframe_display_mode();
}

void AssemblyStepsUtils::render_assembly_tree_ui(float panel_x, float panel_y, float panel_w, float panel_h, float sc)
{
    auto& steps_tree = m_model->get_assembly_steps_tree_data();
    const AssemblyTreeData *tree = nullptr;
    tree                         = &m_model->get_assembly_tree_data();

    if (!tree || tree->nodes.empty())
        return;

    auto step_node_from_card = [&steps_tree](int card_idx) {
        if (card_idx <= 0)
            return -1;
        int step_card_idx = 0;
        for (int root_idx : steps_tree.roots) {
            if (root_idx < 0 || root_idx >= static_cast<int>(steps_tree.nodes.size()))
                continue;
            if (steps_tree.nodes[root_idx].type != AssemblyStepsTreeNode::Type::Folder)
                continue;
            if (step_card_idx == card_idx - 1)
                return root_idx;
            ++step_card_idx;
        }
        return -1;
    };

    clear_active_assembly_tree_checked();
    int active_step_node = m_structure_add_tree_step_node;
    if (active_step_node < 0)
        active_step_node = step_node_from_card(m_structure_add_tree_card);
    if (active_step_node < 0) {//boost debug_break
        active_step_node = find_parent_folder(m_selected_node);//temp no use
    }

    if (active_step_node >= 0) {
        auto& checked = steps_tree.nodes[active_step_node].assembly_tree_checked;
        if (!checked)
            checked.emplace();
        m_active_assembly_tree_checked = &*checked;
    }

    if (m_active_assembly_tree_checked != nullptr &&
        m_assembly_tree_ui_current_folder_node != active_step_node) {
        reseed_assembly_tree_checked_from_step(active_step_node, *tree);
    }

    ImGuiWrapper& imgui = *m_imgui;

    ImGui::SetNextWindowPos(ImVec2(panel_x, panel_y), ImGuiCond_Always, ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(panel_w, panel_h), ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f * sc);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f * sc, 14.0f * sc));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 6.0f * sc);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, m_is_dark ? ImVec4(45 / 255.0f, 45 / 255.0f, 49 / 255.0f, 0.98f) : ImVec4(1.0f, 1.0f, 1.0f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Text, m_is_dark ? ImVec4(0xE0 / 255.0f, 0xE0 / 255.0f, 0xE0 / 255.0f, 1.0f) : ImVec4(38 / 255.0f, 46 / 255.0f, 48 / 255.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(144 / 255.0f, 144 / 255.0f, 144 / 255.0f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(144 / 255.0f, 144 / 255.0f, 144 / 255.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, ImVec4(120 / 255.0f, 120 / 255.0f, 120 / 255.0f, 1.0f));
    imgui.begin(_L("Assembly tree"), ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);

    load_assembly_tree_icons(sc);

    const ImU32 separator_col  = m_is_dark ? IM_COL32(60, 60, 64, 255) : IM_COL32(229, 229, 229, 255);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 tree_window_min = ImGui::GetWindowPos();
    const ImVec2 tree_window_max(tree_window_min.x + ImGui::GetWindowSize().x,
                                 tree_window_min.y + ImGui::GetWindowSize().y);
    bool close_from_outside_click = false;
    {
        const ImVec2 header_min = ImGui::GetCursorScreenPos();
        const float header_w = ImGui::GetContentRegionAvail().x;
        const float header_h = 36.0f * sc;
        const float search_h = 28.0f * sc;
        const float icon_sz = 24.0f * sc;

        if (m_assembly_tree_search_active) {
            const ImVec2 search_min(header_min.x, header_min.y + (header_h - search_h) * 0.5f);
            const ImVec2 search_max(search_min.x + header_w, search_min.y + search_h);
            draw_list->AddRectFilled(search_min, search_max, m_is_dark ? IM_COL32(58, 58, 62, 255) : IM_COL32(248, 248, 248, 255), 14.0f * sc);
            draw_list->AddRect(search_min, search_max, m_is_dark ? IM_COL32(78, 78, 82, 255) : IM_COL32(238, 238, 238, 255), 14.0f * sc);
            const ImVec2 icon_min(search_min.x + 10.0f * sc, search_min.y + (search_h - 16.0f * sc) * 0.5f);
            ImTextureID list_search_tex = m_is_dark && s_assembly_tree_icons.search_dark ? s_assembly_tree_icons.search_dark : s_assembly_tree_icons.search;
            if (list_search_tex)
                draw_list->AddImage(list_search_tex, icon_min,
                    ImVec2(icon_min.x + 16.0f * sc, icon_min.y + 16.0f * sc));
            ImGui::SetCursorScreenPos(ImVec2(search_min.x, search_min.y));
            ImGui::InvisibleButton("##assembly_tree_search_close", ImVec2(34.0f * sc, search_h));
            if (ImGui::IsItemClicked(0)) {
                m_assembly_tree_search_active = false;
                m_assembly_tree_search_focus_pending = false;
                m_assembly_tree_search_text.clear();
            }

            ImGui::SetCursorScreenPos(ImVec2(search_min.x + 34.0f * sc, search_min.y + 2.0f * sc));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 4.0f * sc));
            ImGui::SetNextItemWidth(std::max(0.0f, search_max.x - search_min.x - 44.0f * sc));
            if (m_assembly_tree_search_focus_pending) {
                ImGui::SetKeyboardFocusHere();
                m_assembly_tree_search_focus_pending = false;
            }
            ImGui::InputTextWithHint("##assembly_tree_search", _u8L("Search").c_str(), &m_assembly_tree_search_text);
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(4);
        } else {
            const std::string title = _u8L("List");
            const ImVec2 title_size = ImGui::CalcTextSize(title.c_str());
            draw_list->AddText(ImVec2(header_min.x, header_min.y + (header_h - title_size.y) * 0.5f),
                m_is_dark ? IM_COL32(0xE0, 0xE0, 0xE0, 255) : IM_COL32(38, 46, 48, 255), title.c_str());
            const ImVec2 icon_min(header_min.x + header_w - icon_sz, header_min.y + (header_h - icon_sz) * 0.5f);
            ImTextureID list_search_tex = m_is_dark && s_assembly_tree_icons.search_dark ? s_assembly_tree_icons.search_dark : s_assembly_tree_icons.search;
            if (list_search_tex)
                draw_list->AddImage(list_search_tex, icon_min,
                    ImVec2(icon_min.x + icon_sz, icon_min.y + icon_sz));
            ImGui::SetCursorScreenPos(icon_min);
            ImGui::InvisibleButton("##assembly_tree_search_open", ImVec2(icon_sz, icon_sz));
            if (ImGui::IsItemHovered()) {
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * sc, 6.0f * sc));
                m_imgui->tooltip(_u8L("Search"), 20.0f * m_imgui->scaled(1.0f));
                ImGui::PopStyleVar();
            }
            if (ImGui::IsItemClicked(0)) {
                m_assembly_tree_search_active = true;
                m_assembly_tree_search_focus_pending = true;
            }
        }
        ImGui::SetCursorScreenPos(ImVec2(header_min.x, header_min.y + header_h));
    }
    ImVec2 separator_start = ImGui::GetCursorScreenPos();
    draw_list->AddLine(separator_start, ImVec2(separator_start.x + ImGui::GetContentRegionAvail().x, separator_start.y), separator_col, 1.0f * sc);
    ImGui::Dummy(ImVec2(0.0f, 14.0f * sc));

    std::unordered_map<std::string, bool> dummy_checked;
    std::unordered_map<std::string, bool>& checked = m_active_assembly_tree_checked != nullptr
        ? *m_active_assembly_tree_checked
        : dummy_checked;
    bool quick_select_changed = false;
    if (m_show_assembly_tree_step_quick_select) {
        ImGui::TextColored(ImVec4(172 / 255.0f, 172 / 255.0f, 172 / 255.0f, 1.0f),
            "%s", _u8L("Select all parts in a step").c_str());
        const float chip_h = 20.0f * sc;
        const float chip_gap = 6.0f * sc;
        const float chip_pad_x = 6.0f * sc;
        float chip_x = ImGui::GetCursorScreenPos().x;
        float chip_y = ImGui::GetCursorScreenPos().y + 8.0f * sc;
        const float chip_right = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x - ImGui::GetStyle().WindowPadding.x;

        auto collect_step_objects = [&steps_tree](int node_idx) {
            std::set<int> object_idxs;
            std::function<void(int)> collect = [&](int idx) {
                if (idx < 0 || idx >= static_cast<int>(steps_tree.nodes.size()))
                    return;
                const auto &node = steps_tree.nodes[idx];
                if (node.type == AssemblyStepsTreeNode::Type::Object && node.object_idx >= 0)
                    object_idxs.insert(node.object_idx);
                for (int child_idx : node.children)
                    collect(child_idx);
            };
            collect(node_idx);
            return object_idxs;
        };

        int chip_idx = 0;
        for (int root_idx : steps_tree.roots) {
            if (root_idx < 0 || root_idx >= static_cast<int>(steps_tree.nodes.size()))
                continue;
            const auto &root = steps_tree.nodes[root_idx];
            if (root.type != AssemblyStepsTreeNode::Type::Folder || root.is_final_assembly)
                continue;
            const std::string label = assembly_step_display_name(root);
            const ImVec2 text_size = ImGui::CalcTextSize(label.c_str());
            const float chip_w = text_size.x + 2.0f * chip_pad_x;
            if (chip_x + chip_w > chip_right) {
                chip_x = ImGui::GetWindowPos().x + ImGui::GetStyle().WindowPadding.x;
                chip_y += chip_h + chip_gap;
            }

            const ImVec2 chip_min(chip_x, chip_y);
            const ImVec2 chip_max(chip_x + chip_w, chip_y + chip_h);
            draw_list->AddRectFilled(chip_min, chip_max, m_is_dark ? IM_COL32(65, 65, 69, 255) : IM_COL32(248, 248, 248, 255), 6.0f * sc);
            draw_list->AddText(ImVec2(chip_min.x + chip_pad_x, chip_min.y + (chip_h - text_size.y) * 0.5f),
                m_is_dark ? IM_COL32(0xC0, 0xC0, 0xC0, 255) : IM_COL32(107, 107, 107, 255), label.c_str());

            ImGui::SetCursorScreenPos(chip_min);
            ImGui::PushID(chip_idx++);
            ImGui::InvisibleButton("##step_quick_select", ImVec2(chip_w, chip_h));
            if (ImGui::IsItemClicked(0)) {
                const std::set<int> object_idxs = collect_step_objects(root_idx);
                checked.clear();
                for (const auto &node : tree->nodes) {
                    if (!node.selectable || node.object_idx < 0)
                        continue;
                    if (object_idxs.find(node.object_idx) != object_idxs.end())
                        checked[node.uid] = true;
                }
                quick_select_changed = true;
            }
            ImGui::PopID();
            chip_x += chip_w + chip_gap;
        }
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + ImGui::GetStyle().WindowPadding.x,
            chip_y + chip_h + 12.0f * sc));
    }
    AssemblyTreeRenderOptions render_options;
    render_options.allow_object_check = true;
    render_options.allow_volume_check = false;
    render_options.show_footer = true;
    render_options.readonly = false;
    render_options.child_id = "##assembly_tree_nodes";
    AssemblyTreeRenderResult render_result = render_assembly_tree_selector(*tree, checked, render_options, sc);
    if (quick_select_changed)
        render_result.changed = true;
    if (!m_structure_add_tree_opened_this_frame &&
        ImGui::IsMouseClicked(0) &&
        !ImGui::IsMouseHoveringRect(tree_window_min, tree_window_max, true)) {
        close_from_outside_click = true;
        render_result.cancel = true;
    }
    if (render_result.cancel && m_active_assembly_tree_checked != nullptr) {
        checked = m_assembly_tree_ui_original_checked;
    }
    if (render_result.changed && !render_result.cancel) {
        // Mirror the select popup: highlight the just-checked objects on the canvas.
        sync_checked_tree_to_canvas(*tree, checked);
        // Preview opacity from the checked set (step membership is not committed
        // until confirm, so apply_keyframe_display_mode() would leave newly-checked
        // objects transparent in Highlight/OnlyCurrentStep modes).
        apply_tree_checked_display_mode(*tree, checked);
        do_commond_callback("dirty");
    }
    if (render_result.confirm && active_step_node >= 0 && m_active_assembly_tree_checked != nullptr &&
        checked != m_assembly_tree_ui_original_checked) {
        apply_assembly_tree_checked_to_step(active_step_node, *tree, checked);
    } else if (render_result.confirm) {
        do_commond_callback("dirty");
    }
    if (render_result.cancel || render_result.confirm) {
        exit_render_assembly_tree_ui();
        if (close_from_outside_click)
            do_commond_callback("request_extra_frame");
    }

    imgui.end();
    ImGui::PopStyleColor(7);
    ImGui::PopStyleVar(4);
    m_structure_add_tree_opened_this_frame = false;
}

} // namespace GUI
} // namespace Slic3r
