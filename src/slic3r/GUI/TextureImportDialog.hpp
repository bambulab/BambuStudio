#pragma once

#include "GUI_Utils.hpp"
#include "Widgets/ProgressDialog.hpp"
#include "libslic3r/TexturePainting.hpp"
#include "libslic3r/Thread.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/statbmp.h>
#include "Widgets/PopupWindow.hpp"
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/textctrl.h>
#include "Widgets/TextInput.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/Label.hpp"
#include <wx/button.h>
#include "Widgets/Button.hpp"
#include <wx/glcanvas.h>
#include <wx/event.h>
#include <wx/timer.h>

#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <vector>
#include <string>

class GreenSlider;
class GreenDoubleSlider;
class StaticBox;

namespace Slic3r { namespace GUI {

struct OfficialMixingKitAddPlan;

wxDECLARE_EVENT(EVT_TEXTURE_COMPUTE_DONE, wxCommandEvent);
wxDECLARE_EVENT(EVT_TEXTURE_COMPUTE_PROGRESS, wxCommandEvent);
wxDECLARE_EVENT(EVT_TEXTURE_COMPUTE_ERROR, wxCommandEvent);
wxDECLARE_EVENT(EVT_TEXTURE_PATCH_DONE, wxCommandEvent);

enum class TextureImportState {
    Idle,
    Computing,
    Ready,
    Error
};

enum class TextureImportWizardStep {
    SimplifyColors,
    FilamentMatching
};

enum class TextureFilamentKind {
    ExistingPhysical,
    ExistingMixed,
    NewPhysical,
    NewMixed
};

struct TextureFilamentEntry {
    TextureFilamentKind kind{TextureFilamentKind::ExistingPhysical};
    int                 dialog_index{-1};
    size_t              project_config_index{size_t(-1)};
    std::string         color_hex;
    std::string         name;
    std::string         type;
    std::string         preset_name;
    std::vector<unsigned int> mixed_components;
    std::vector<int>          mixed_ratios;
};

// Coarse family type (PLA / PETG / ABS / ...). Empty if unknown or mixed
// components disagree, so callers can skip cross-material pairing.
std::string texture_entry_family_type(const TextureFilamentEntry& entry,
                                      const std::vector<TextureFilamentEntry>& entries);

struct TextureNewMixedFilament {
    int                       dialog_index{-1};
    std::string               color_hex;
    std::vector<int>          component_dialog_indices;
    std::vector<int>          ratios;
};

struct FilamentMappingRow {
    int                        cluster_id    = -1;
    std::array<std::size_t, 3> source_color  = {0, 0, 0};
    std::string                source_hex;
    int                        target_filament_idx = 0;
    wxPanel*                   source_panel  = nullptr;
    wxPanel*                   target_panel  = nullptr;
};

struct GapPreviewState;
struct TextureMixPool;

class FilamentSelectPopup;
class MatchThresholdGear;
class MatchThresholdPopup;
// Lightweight 3D preview panel using wxGLCanvas.
// Renders: original face-colored, multi-color, or filament-mapped.
class TexturePreviewCanvas : public wxGLCanvas
{
public:
    enum class RenderMode { Original, MultiColor, FilamentMap };

    TexturePreviewCanvas(wxWindow* parent, const wxGLAttributes& attrs);
    ~TexturePreviewCanvas();

    void set_mesh_data(
        const std::vector<std::array<float, 3>>& vertices,
        const std::vector<std::array<int, 3>>&   indices);

    void set_painted_mesh_data(
        const std::vector<std::array<float, 3>>& vertices,
        const std::vector<std::array<int, 3>>&   indices);
    void set_face_colors(const std::vector<std::array<std::size_t, 3>>& face_colors);
    void set_original_face_colors(const std::vector<std::array<std::size_t, 3>>& face_colors);
    // Per-vertex RGB in 0..1. Original mode interpolates these across each triangle.
    void set_original_vertex_colors(const std::vector<std::array<float, 3>>& vertex_colors);
    void set_filament_color_map(const std::map<std::array<std::size_t, 3>, std::array<float, 3>>& color_map);

    void set_render_mode(RenderMode mode);
    RenderMode get_render_mode() const { return m_mode; }
    void set_computing_overlay(bool show);
    void reset_view();

    enum class ResetOverlayAlign { Hidden, BottomRight };
    void set_reset_overlay_align(ResetOverlayAlign align);
    void set_reset_overlay_hovered(bool hovered);
    void set_reset_overlay_hover_callback(std::function<void(bool)> cb);

    // The canvas covers the rounded corners of its container, and child windows
    // cannot be reshaped portably, so the corners are painted back here in GL.
    // "inset" is the gap between the container edge and the canvas edge.
    enum class RoundedCornerSide { None, Left, Right, All };
    void set_rounded_corners(RoundedCornerSide side, int radius, int inset,
                             const wxColour& outside, const wxColour& border);

    struct ViewState {
        float zoom  = 1.0f;
        float rot_x = 30.0f;
        float rot_y = 30.0f;
        float pan_x = 0.0f;
        float pan_y = 0.0f;
    };
    ViewState get_view_state() const;
    void set_view_state(const ViewState& state, bool notify = true);
    void set_view_changed_callback(std::function<void(const ViewState&)> cb);

private:
    void notify_view_changed();
    void present();
    void on_paint(wxPaintEvent& evt);
    void on_size(wxSizeEvent& evt);
    void on_mouse(wxMouseEvent& evt);
    void ensure_gl_ready();
    void render();
    void render_mesh();
    void render_reset_overlay(const wxSize& logical_size, const wxSize& viewport_size);
    void render_rounded_corners(const wxSize& logical_size, const wxSize& viewport_size);
    void ensure_corner_texture(int texture_px);
    void upload_reset_icon_textures();
    unsigned int upload_reset_icon_texture(const std::string& icon_name);
    wxRect reset_overlay_rect() const;
    bool handle_reset_overlay_mouse(wxMouseEvent& evt);
    void update_bounding_box();

    wxGLContext*  m_context        = nullptr;
    bool          m_gl_initialized = false;
    RenderMode    m_mode           = RenderMode::Original;

    float   m_zoom     = 1.0f;
    float   m_rot_x    = 30.0f;
    float   m_rot_y    = 30.0f;
    float   m_pan_x    = 0.0f;
    float   m_pan_y    = 0.0f;
    wxPoint m_last_mouse_pos;
    enum class DragMode { None, Rotate, Pan };
    DragMode m_drag_mode = DragMode::None;

    std::vector<std::array<float, 3>> m_vertices;
    std::vector<std::array<int, 3>>   m_indices;
    std::vector<std::array<float, 3>> m_painted_vertices;
    std::vector<std::array<int, 3>>   m_painted_indices;
    std::vector<std::array<float, 3>> m_face_colors_rgb;
    std::vector<std::array<float, 3>> m_original_face_colors_rgb;
    std::vector<std::array<float, 3>> m_original_vertex_colors_rgb;
    std::vector<std::array<float, 3>> m_filament_colors_rgb;
    std::map<std::array<std::size_t, 3>, std::array<float, 3>> m_color_map;

    std::array<float, 3> m_center = {0, 0, 0};
    float                m_radius = 1.0f;

    ResetOverlayAlign m_reset_overlay_align = ResetOverlayAlign::Hidden;
    unsigned int m_reset_icon_tex        = 0;
    unsigned int m_reset_icon_hover_tex  = 0;
    unsigned int m_reset_icon_dark_tex   = 0;
    unsigned int m_reset_icon_dark_hover_tex = 0;
    bool         m_reset_overlay_hovered = false;
    bool         m_reset_overlay_pressed = false;
    std::function<void(bool)> m_reset_hover_cb;

    RoundedCornerSide m_corner_side   = RoundedCornerSide::None;
    int          m_corner_radius      = 0;
    int          m_corner_inset       = 0;
    wxColour     m_corner_outside;
    wxColour     m_corner_border;
    unsigned int m_corner_tex         = 0;
    int          m_corner_tex_px      = 0;

    bool         m_computing_overlay     = false;
    std::function<void(const ViewState&)> m_view_changed_cb;
};


class TextureImportDialog : public DPIDialog
{
public:
    TextureImportDialog(wxWindow*                        parent,
                        const Slic3r::TexturedMesh&      textured_mesh,
                        const std::vector<TextureFilamentEntry>& filament_entries,
                        std::function<bool()>            initial_cancel_callback = {},
                        std::function<bool(int)>         initial_progress_callback = {},
                        std::function<void(bool)>        initial_progress_visibility_callback = {},
                        const wxString&                  hint = {});
    ~TextureImportDialog();

    int ShowModal() override;
    void on_dpi_changed(const wxRect& suggested_rect) override;
    void on_sys_color_changed() override;

    Slic3r::PaintedMesh               get_painted_mesh() const;
    std::vector<Slic3r::FilamentMatch> get_matches() const;
    bool                               was_skipped() const { return m_skipped; }
    bool                               fallback_to_geometry_only() const { return m_fallback_to_geometry_only; }
    // Colors of virtual filaments that need to be created after dialog confirmation.
    // Index i corresponds to filament index (m_existing_filament_count + i).
    const std::vector<std::array<float, 4>>& get_new_filament_colors() const { return m_new_filament_colors; }
    const std::vector<std::string>& get_new_filament_preset_names() const { return m_new_filament_preset_names; }
    const std::vector<TextureNewMixedFilament>& get_new_mixed_filaments() const { return m_new_mixed_filaments; }
    const std::vector<TextureFilamentEntry>& get_filament_entries() const { return m_filament_entries; }
    size_t get_existing_filament_count() const { return m_existing_filament_count; }

private:
    void build_ui();
    void build_stepper(wxWindow* parent, wxSizer* sizer);
    void build_preview_panel(wxWindow* parent, wxSizer* sizer);
    void build_params_panel(wxWindow* parent, wxSizer* sizer);
    void build_mapping_panel(wxWindow* parent, wxSizer* sizer);
    void build_bottom_buttons(wxSizer* sizer);

    void set_state(TextureImportState new_state);
    void update_ui_for_state();
    void set_wizard_step(TextureImportWizardStep step);
    void update_wizard_ui();
    void for_each_preview(const std::function<void(TexturePreviewCanvas*)>& fn);
    void update_preview_modes();
    void update_color_captions();
    void recenter_preview_tags();
    void update_preview_rounded_corners();
    void clear_param_spin_selection();
    void update_dialog_min_size();
    // Apply the DIP client size after the native window exists (macOS needs
    // wxEVT_SHOW). allow_center only permits the one-time initial centering;
    // every later call keeps the window where the user left it.
    void apply_dialog_geometry(bool allow_center);
    // Shift the window back into the current display's work area after it grew,
    // keeping its top-left corner unless that would push it off screen.
    void keep_dialog_within_display();
    void update_stepper();
    void style_primary_button(Button* btn);
    void style_secondary_button(Button* btn);
    void style_color_count_preset_button(Button* btn);
    void style_advanced_settings_card();
    // Re-break the Advanced settings hints against the current column width.
    void layout_advanced_hints();
    void apply_theme();

    void start_computation(bool auto_color = false, bool initial = false);
    void cancel_computation();
    void capture_compute_snapshot();
    void restore_compute_snapshot();
    void schedule_recompute(bool auto_color, int delay_ms);
    void invalidate_inflight_computation();
    void show_last_result_after_cancel();
    bool params_match_last_result() const;
    void update_gap_param_lock();
    void on_recompute_timer(wxTimerEvent& evt);
    void on_computation_complete(wxCommandEvent& evt);
    void on_computation_progress(wxCommandEvent& evt);
    void on_computation_error(wxCommandEvent& evt);
    void on_patch_build_complete(wxCommandEvent& evt);

    void rebuild_mapping_rows();
    void layout_mapping_rows();
    void do_auto_match();
    // Among candidates that pass m_match_delta_threshold, filaments inside the
    // Color Tolerance band are treated as equal and chosen by slot kind:
    // ExistingPhysical, NewPhysical, ExistingMixed, NewMixed. Outside that band
    // the smallest CIEDE2000 ΔE wins. Same rank keeps the smaller ΔE, then the
    // smaller dialog index. Returns -1 when none pass the threshold.
    int select_filament_for_color(const std::array<std::size_t, 3>& color,
                                  const std::vector<int>& candidates) const;
    // Bind every cluster to a project physical filament of the voted family.
    // Mixed slots are skipped. Candidates outside m_match_delta_threshold stay
    // unmatched. This pool is physical-only, so Color Tolerance does not change
    // which slot wins.
    void match_clusters_to_physical_filaments();
    // Rebind each color with select_filament_for_color(). Mixed slots are
    // compared by their display color only while Color Mixing is on; otherwise
    // they stay out of the pool at every threshold. Candidates are project
    // filaments and ones created in this dialog. drop_unused removes created
    // filaments that no color references; live edits pass false. Keeps row order.
    void reapply_match_threshold(bool drop_unused);
    void show_match_threshold_popup();
    void hide_match_threshold_popup();
    void update_match_threshold_gear_hover();
    // Unchecks Color mixing and rebinds rows on mixed slots to physical ones.
    // No-op while Color mixing is off.
    void disable_mix_for_threshold_change();
    void on_match_threshold_changed(int value);
    void on_match_threshold_committed(int value);
    // Drop NewPhysical / NewMixed slots created in this dialog, then rebuild
    // the baseline mapping with do_auto_match(). Used by a fresh compute when
    // Color Mixing is off.
    void drop_virtual_filaments_keep_project();
    void reset_to_project_filaments_and_auto_match();
    // Reorder m_current_matches into a canonical, predictable order (ascending
    // filament_index, with unmapped entries pushed to the end). Used right
    // after the initial computation so the first view the user sees has a
    // stable, intuitive layout.
    void sort_current_matches_by_filament_index();
    std::vector<Slic3r::FilamentMatch> build_matches_from_rows() const;
    void update_filament_color_map();
    void show_filament_popup(size_t row_index);
    void dismiss_filament_popup();
    void dismiss_filament_popup_on_wheel(wxMouseEvent& evt);
    void show_mixing_kits_help();
    void on_mix_toggled(wxCommandEvent& evt);
    void apply_mix_from_existing_filaments();
    void add_pla_palette_cmykw_to_project(const OfficialMixingKitAddPlan& plan);
    // NewPhysical slots a row still uses. reload_existing_filaments_from_project()
    // keeps exactly these, so filaments added to the project must leave room for them.
    size_t pending_physical_filament_count() const;
    // True when the mixing family has fewer than two distinct physical colors,
    // no complete CMYKW kit exists, and the project still has free filament slots.
    bool mix_needs_supplement_filaments() const;
    // Let the user stage the filaments mixing needs (red / yellow / blue of the mixing
    // family by default) and add them to the project on OK. Returns false when cancelled.
    bool prompt_supplement_filaments_for_mix();
    void reload_existing_filaments_from_project(const std::set<size_t>& deleted_project_indices = {});
    void rematch_after_project_filament_change();
    bool has_complete_pla_palette_cmykw() const;
    std::string preferred_mix_family() const;
    // Physical filaments and family that apply_mix_from_existing_filaments() mixes from.
    void build_mix_pool(TextureMixPool& pool) const;
    // Mixed recipe for one color from the pool. Returns the reused or created mixed
    // slot, or the single physical component, or -1 when there is no usable recipe.
    // When the recipe needs a new mixed slot and allow_create is false, returns -1
    // with needs_slot set.
    int  resolve_mix_color(const TextureMixPool& pool, const std::array<std::size_t, 3>& color,
                           bool allow_create, bool& created, bool& needs_slot);
    void reset_auto_mix();
    int  add_virtual_filament(const std::array<float, 4>& rgba, const std::string& hex,
                              const std::string& preset_name = std::string(),
                              const std::string& display_name = std::string());
    int  add_virtual_mixed_filament(const std::string& color_hex,
                                    const std::vector<int>& component_dialog_indices,
                                    const std::vector<int>& ratios);
    bool remove_virtual_filament(int dialog_index);
    size_t max_physical_filament_count() const;
    size_t physical_filament_entry_count() const;
    size_t mixed_filament_entry_count() const;
    int    max_color_count() const;
    void   update_color_count_controls();
    void   set_color_count_exceeded(bool exceeded);
    void   update_color_count_warning();
    int    visible_simplified_color_count() const;
    void   sync_color_count_from_preview();
    bool   can_restore_applied_color_count(int count) const;
    void   restore_applied_color_result();
    void   request_color_count(int count, int delay_ms);
    bool filament_count_exceeded() const;
    // True when at least one mapping row / match has no filament assigned.
    bool has_unmatched_mapping() const;
    // Bind every unmatched color without changing matched rows or exceeding
    // the physical / mixed filament limits. Larger colors get the free slots first: a NewPhysical
    // slot of the auto-match family, or with Color Mixing on a mixed recipe before
    // that. Colors left without a slot take closest_filament_ignoring_threshold().
    void complete_unmatched_filaments();
    void update_unmatched_warning_visibility();
    void wrap_unmatched_warning_label();
    // Screen Y of the matching-page hint bar bottom; used to dock the filament popup.
    int  filament_popup_align_bottom() const;
    // Total triangle area of each simplified color in m_painted.
    std::map<std::array<std::size_t, 3>, double> cluster_color_areas() const;
    // Indices into m_current_matches with no filament, larger color area first.
    std::vector<size_t> unmatched_match_indices_by_area() const;
    // Drops created filaments no color references, then returns the free slots.
    size_t remaining_physical_filament_capacity();
    size_t remaining_mixed_filament_capacity();
    // Closest filament of family_type by CIEDE2000, ignoring m_match_delta_threshold.
    // Mixed slots are candidates only while Color Mixing is on. Falls back to any
    // family when none matches.
    int  closest_filament_ignoring_threshold(const std::array<std::size_t, 3>& color,
                                             const std::string& family_type) const;
    void compact_used_virtual_filaments();
    // Drop NewPhysical / NewMixed slots that no mapping row references, then
    // refresh target cards in place (no rebuild_mapping_rows).
    void drop_unused_new_filaments_and_refresh();
    void refresh_mapping_target_panels();
    void bind_match_inplace(Slic3r::FilamentMatch& match, int filament_index, bool repaint = true);
    int  find_closest_filament_index(const std::array<std::size_t, 3>& color) const;
    int  find_closest_filament_index(const std::array<std::size_t, 3>& color,
                                    int skip_index, bool physical_only,
                                    const std::string& family_type = {}) const;
    // Returns a vector indexed by dialog_index whose value is the 1-based
    // display number that mirrors the final sidebar ordering produced by
    // apply_textured_mesh_import_result (Plater.cpp): ExistingPhysical,
    // NewPhysical, ExistingMixed, NewMixed. Used so the dialog shows the
    // same IDs the sidebar will show after OK, instead of the raw
    // dialog_index + 1 (which interleaves physicals and mixeds).
    // MUST mirror ordering in apply_textured_mesh_import_result (Plater.cpp:9896).
    std::vector<int> compute_display_numbers() const;

    void on_color_preset_clicked(wxCommandEvent& evt);
    void on_color_slider_changed(wxCommandEvent& evt);
    void on_color_spin_text_changed(wxCommandEvent& evt);
    void on_color_spin_commit(bool from_enter = false);
    void on_smooth_slider_changed(wxCommandEvent& evt);
    void on_smooth_spin_commit();
    void on_gap_slider_changed(wxCommandEvent& evt);
    void on_gap_spin_commit();
    void toggle_advanced_design();
    void retarget_filament_slot(int from_index, int to_index);
    // Fold duplicate components of a NewMixed slot (ratios are summed, first
    // occurrence order kept) and mirror the result into m_new_mixed_filaments.
    // Returns the remaining physical dialog index when only one component is
    // left, otherwise -1.
    int  fold_new_mixed_components(int mixed_idx);
    void on_skip_clicked(wxCommandEvent& evt);
    void on_next_clicked(wxCommandEvent& evt);
    void on_prev_clicked(wxCommandEvent& evt);
    void on_reset_clicked(wxCommandEvent& evt);
    void on_ok_clicked(wxCommandEvent& evt);

    void set_color_count_value(int value, bool update_spin);
    void set_smooth_value(int value, bool update_spin);
    void set_gap_value(double value, bool update_spin);

    GapPreviewState&       gap_preview();
    const GapPreviewState& gap_preview() const;
    // Install worker-built patches and refresh the gap preview from the current slider.
    void install_gap_preview(std::unique_ptr<GapPreviewState> preview);
    // Rebuild same-color connected patches from the last simplified mesh.
    void update_color_patches();
    void schedule_color_patches_rebuild();
    // Preview only: replay or undo region merges so every region smaller than the
    // gap percent of the mesh is absorbed by its neighbor.
    void apply_gap_area();
    // Write gap preview colors into m_painted for filament matching. Does not
    // overwrite the last simplified face colors or reset the gap slider.
    void commit_gap_area_to_painted();
    void clear_color_patches();
    void refresh_gap_preview();
    // Number of merge events whose absorbed region area is below area_threshold.
    int  gap_event_count_for_threshold(double area_threshold) const;

    bool has_valid_result() const;
    // Confirm stays disabled while the filament count exceeds the limit; only
    // removing filaments re-enables it.
    bool can_confirm() const;
    void update_confirm_button_state();

    Slic3r::TexturedMesh               m_textured_mesh;
    std::vector<std::string>           m_filament_color_strs;   // existing + virtual
    std::vector<std::string>           m_filament_names;        // existing + virtual
    std::vector<std::array<float, 4>>  m_filament_colors_rgba;  // existing + virtual
    std::vector<TextureFilamentEntry>  m_filament_entries;      // aligned with m_filament_colors_rgba
    size_t                             m_existing_filament_count = 0;
    std::vector<std::array<float, 4>>  m_new_filament_colors;   // only virtual (to be created)
    std::vector<std::string>           m_new_filament_preset_names; // only virtual, aligned with m_new_filament_colors
    std::vector<TextureNewMixedFilament> m_new_mixed_filaments;
    // Old dialog index -> new dialog index from the last reload_existing_filaments_from_project(), -1 if dropped.
    std::vector<int>                   m_reload_index_remap;
    std::string                        m_default_virtual_filament_preset_name;
    // Empty when the active printer has no PLA Palette preset; CMYKW auto-mix is
    // skipped in that case rather than silently falling back to PLA Basic.
    std::string                        m_palette_virtual_filament_preset_name;

    TextureImportState                 m_state = TextureImportState::Idle;
    TextureImportWizardStep            m_wizard_step = TextureImportWizardStep::SimplifyColors;
    // CenterOnParent runs once, on the first placement. Step switches, the
    // Advanced settings card and DPI changes must not move the window again.
    bool                               m_geometry_centered = false;
    bool                               m_skipped = false;
    bool                               m_fallback_to_geometry_only = false;
    bool                               m_mix_enabled = false;
    // CIEDE2000 ΔE accepted by automatic filament matching. Slider range 0..50.
    // The right end (50) is unlimited: every color binds to the closest candidate.
    int                                m_match_delta_threshold = 20;
    // Live threshold edits only rebind. Unused new filaments are dropped after
    // the slider is released, the number box is committed, or the popup closes.
    bool                               m_match_threshold_drop_pending = false;
    // Guards the batch binders against re-entry while they rebuild m_current_matches.
    bool                               m_mix_applying = false;

    Slic3r::PaintedMesh               m_painted;
    Slic3r::PaintedMesh               m_original_preview;
    bool                              m_original_preview_ready = false;
    // Vertex-colored imports keep the source mesh on the left. The repair cache
    // only has flat face colors and would wipe the gradient.
    bool                              m_lock_original_vertex_preview = false;
    std::vector<Slic3r::FilamentMatch> m_current_matches;

    boost::thread                      m_worker;
    std::atomic<bool>                  m_cancel_flag{false};
    // In-dialog recompute was dropped because parameters changed. Its completion
    // must not be applied. Cleared when a new run starts or the last result is shown.
    bool                               m_compute_superseded = false;
    std::atomic<int>                   m_compute_generation{0};
    std::atomic<int>                   m_patch_generation{0};
    std::mutex                         m_result_mutex;
    Slic3r::PaintedMesh               m_pending_result;
    std::unique_ptr<GapPreviewState>  m_pending_gap_preview;
    bool                              m_patch_building = false;
    wxTimer*                           m_recompute_timer = nullptr;
    bool                               m_pending_auto_color = false;
    bool                               m_updating_params = false;
    bool                               m_auto_preset_selected = true;
    bool                               m_advance_to_matching_when_ready = false;
    std::function<bool()>              m_initial_cancel_callback;
    std::function<bool(int)>           m_initial_progress_callback;
    std::function<void(bool)>          m_initial_progress_visibility_callback;
    // Shown above the color settings in the first step; empty for no hint.
    wxString                           m_hint;
    bool                               m_current_computation_initial = false;
    bool                               m_initial_computation_pending = false;
    bool                               m_initial_computation_cancelled = false;
    bool                               m_initial_computation_failed = false;
    bool                               m_initial_tooltips_set = false;
    bool                               m_current_computation_auto_color = false;
    Slic3r::MeshRepairCachePtr         m_mesh_repair_cache;

    Button*      m_btn_color_4    = nullptr;
    Button*      m_btn_color_8    = nullptr;
    Button*      m_btn_color_16   = nullptr;
    Button*      m_btn_color_auto = nullptr;
    GreenSlider* m_color_slider   = nullptr;
    TextInput*   m_color_spin     = nullptr;
    GreenSlider* m_smooth_slider  = nullptr;
    TextInput*   m_smooth_spin    = nullptr;
    GreenDoubleSlider* m_gap_slider = nullptr;
    TextInput*         m_gap_spin   = nullptr;
    Label*                m_lbl_smooth_hint = nullptr;
    Label*                m_lbl_gap_hint    = nullptr;
    Label*                m_lbl_hint        = nullptr;
    wxStaticBitmap*       m_hint_icon       = nullptr;
    wxPanel*              m_advanced_header = nullptr;
    StaticBox*            m_advanced_body   = nullptr;
    bool                  m_advanced_expanded = false;
    bool                  m_laying_out_hints = false;
    wxPanel*              m_title_line     = nullptr;
    wxPanel*              m_params_panel   = nullptr;
    wxPanel*              m_mapping_panel  = nullptr;
    wxPanel*              m_preview_container = nullptr;
    wxPanel*              m_stepper_panel  = nullptr;

    CheckBox*             m_mix_cb = nullptr;
    wxStaticText*         m_lbl_mix_help = nullptr;
    wxScrolledWindow*     m_mapping_scroll = nullptr;
    wxBoxSizer*           m_mapping_sizer  = nullptr;
    std::vector<FilamentMappingRow> m_mapping_rows;
    FilamentSelectPopup*  m_filament_popup = nullptr;
    int                   m_filament_popup_row = -1;
    MatchThresholdGear*   m_match_threshold_gear = nullptr;
    MatchThresholdPopup*  m_match_threshold_popup = nullptr;
    int                   m_last_filament_popup_dismiss_row = -1;
    std::chrono::steady_clock::time_point m_filament_popup_dismiss_time;

    TexturePreviewCanvas* m_preview_canvas       = nullptr;
    TexturePreviewCanvas* m_preview_canvas_right = nullptr;
    wxPanel*              m_lbl_preview_left_panel  = nullptr;
    wxPanel*              m_lbl_preview_right_panel = nullptr;
    wxPanel*              m_updating_overlay        = nullptr;
    wxPanel*              m_color_count_warning     = nullptr;
    wxPanel*              m_caption_row             = nullptr;
    wxStaticText*         m_lbl_caption_left  = nullptr;
    wxStaticText*         m_lbl_caption_right = nullptr;
    wxStaticText*         m_lbl_mapping       = nullptr;

    ProgressDialog* m_progress_dlg = nullptr;

    Button*       m_btn_skip   = nullptr;
    Button*       m_btn_next   = nullptr;
    Button*       m_btn_prev   = nullptr;
    Button*       m_btn_reset  = nullptr;
    Button*       m_btn_ok     = nullptr;
    wxPanel*      m_unmatched_warning  = nullptr;
    wxStaticBitmap* m_unmatched_warning_icon = nullptr;
    Label*        m_unmatched_warning_label = nullptr;
    Label*        m_unmatched_add_link = nullptr;
    ScalableBitmap m_bmp_unmatched;
    ScalableBitmap m_bmp_brand;
    wxBoxSizer*   m_footer_btn_sizer   = nullptr;

    int    m_param_color_count = 4;
    int    m_param_smooth      = 5;
    double m_param_gap_area    = 0.0; // percent of total mesh area, 0..5

    std::unique_ptr<GapPreviewState> m_gap_preview;

    int   m_applied_color_count = -1;
    int   m_applied_smooth      = -1;
    bool  m_applied_auto_preset = true;
    int   m_original_color_count = 0;
    bool  m_color_count_exceeded = false;

    struct ComputeSnapshot {
        bool valid = false;
        Slic3r::PaintedMesh painted;
        std::vector<Slic3r::FilamentMatch> matches;
        std::vector<std::string> filament_color_strs;
        std::vector<std::string> filament_names;
        std::vector<std::array<float, 4>> filament_colors_rgba;
        std::vector<TextureFilamentEntry> filament_entries;
        std::vector<std::array<float, 4>> new_filament_colors;
        std::vector<std::string> new_filament_preset_names;
        std::vector<TextureNewMixedFilament> new_mixed_filaments;
        int  param_color_count = 4;
        int  param_smooth = 5;
        int  applied_color_count = -1;
        int  applied_smooth = -1;
        bool auto_preset_selected = true;
        TextureImportState state = TextureImportState::Idle;
        double param_gap_area = 0.0;
        std::unique_ptr<GapPreviewState> gap_preview;
    };
    ComputeSnapshot m_compute_snapshot;

    static const int ID_COLOR_4     = wxID_HIGHEST + 200;
    static const int ID_COLOR_8     = wxID_HIGHEST + 201;
    static const int ID_COLOR_16    = wxID_HIGHEST + 202;
    static const int ID_COLOR_AUTO  = wxID_HIGHEST + 203;
    static const int ID_BTN_SKIP    = wxID_HIGHEST + 205;
    static const int ID_BTN_NEXT    = wxID_HIGHEST + 206;
    static const int ID_BTN_PREV    = wxID_HIGHEST + 207;
    static const int ID_BTN_RESET   = wxID_HIGHEST + 208;

    wxDECLARE_EVENT_TABLE();
};

}} // namespace Slic3r::GUI
