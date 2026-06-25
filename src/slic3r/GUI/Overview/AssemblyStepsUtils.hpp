#ifndef slic3r_AssemblyStepsUtils_hpp_
#define slic3r_AssemblyStepsUtils_hpp_

#include "libslic3r/Model.hpp"
#include "../Camera.hpp"
#include "../Gizmos/GLGizmosManager.hpp"
#include "../GLModel.hpp"
#include "imgui/imgui.h"

class wxWindow;

namespace Slic3r {
namespace GUI {

class ImGuiWrapper;
class PBOReader;
class Mp4Recorder;
class AssemblyExportProgressWindow;
struct KeyframeObjectDisplayState
{
    bool active{true};
    float alpha{1.f};
    bool force_native_color{false};
};
using CanvasCallBack = std::function<void(std::string)>;

struct AssemblySelectionMatchInfo
{
    int         folder_node_idx{-1};
    int         object_node_idx{-1};
    int         object_idx{-1};
    int         volume_idx{-1};
    std::string step_label;
    std::string object_name;
};
// --- Assembly Structure panel data (Figma node 732:10276) -----------------------
struct AssemblyStructureChip
{
    std::string label;
};

struct AssemblyStructureCard
{
    // Tag pill shown at the top-left of the card:
    enum class TagStyle { Default, Step };
    TagStyle    tag_style{TagStyle::Default};
    std::string tag_text;
    // Title row: "<title> (<count>)" in bold + light parentheses.
    std::string title;
    int         count{0};
    // Per-step cards show a "+" add-button on the right of the title row.
    bool        show_add_button{false};
    // Selected step card has a green border instead of the default gray.
    bool        selected{false};
    // Chip strip below the title:
    std::string                        prefix_text;
    std::vector<AssemblyStructureChip> chips;
    std::string                        placeholder_text;
    // Label shown next to the "Select" button when the card is active.
    std::string select_label;
    // Final-assembly card only: show "Default" when all modelObjects are selected.
    bool        select_show_default{false};
    // Tree-node this card represents. -1 marks the synthetic "default"
    int node_idx{-1};
    bool is_final_assembly{false};
};

enum class SelectionOrigin {
    None             = 0,
    TreeNode         = 1,
    GLVolume         = 2,
    ImGui            = 3,
    ImGuiNote        = 4,
    NoteColorControl = 5
};

struct AssemblyStructurePanelData
{
    std::string                        title;     //Assembly Structure
    std::string                        subtitle;  //
    std::vector<AssemblyStructureCard> cards;
    bool                               always_show_scrollbar{false};//slider// When true the scrollbar is always visible; when false it only appears on overflow.
};

struct AssemblyTreeRenderOptions
{
    bool        allow_object_check{true};
    bool        allow_volume_check{true};
    bool        show_footer{true};
    bool        readonly{false};
    // When true, clicking a row (away from its checkbox/expander) marks it as the
    // selected row (green highlight) and selects the matching object/volume on the
    // canvas; hovering a row fires hover_tree_item_logic().
    bool        enable_row_select{false};
    const char *child_id{"##assembly_tree_nodes"};
};

struct AssemblyTreeRenderResult
{
    bool changed{false};
    bool confirm{false};
    bool cancel{false};
    int  checked_leaf_count{0};
    int  leaf_count{0};
};

enum class ExportType {
    PDF,
    MarkDown,
    MP4
};

class AssemblyStepsUtils
{
    ImTextureID         m_tree_icon_play{nullptr};
    ImTextureID         m_tree_icon_play_dark{nullptr};
    ImTextureID         m_tree_icon_pause{nullptr};
    ImTextureID         m_tree_icon_apply_camera{nullptr};
    ImTextureID         m_tree_icon_apply_camera_dark{nullptr};
    ImTextureID         m_tree_icon_auto_explode{nullptr};
    ImTextureID         m_tree_icon_auto_explode_dark{nullptr};
    ImTextureID         m_tree_icon_object{nullptr};
    ImTextureID         m_tree_icon_part{nullptr};
    ImTextureID         m_tree_icon_screw{nullptr};
    ImTextureID         m_tree_icon_screw_dark{nullptr};
    ImTextureID         m_tree_icon_glue{nullptr};
    ImTextureID         m_tree_icon_glue_dark{nullptr};
    ImTextureID         m_tree_icon_clip{nullptr};
    ImTextureID         m_tree_icon_clip_dark{nullptr};
    // Add Notes tool icons (Section 2 of guide panel).
    ImTextureID         m_note_icon_rect{nullptr};
    ImTextureID         m_note_icon_rect_dark{nullptr};
    ImTextureID         m_note_icon_circle{nullptr};
    ImTextureID         m_note_icon_circle_dark{nullptr};
    ImTextureID         m_note_icon_line{nullptr};
    ImTextureID         m_note_icon_vector{nullptr};
    ImTextureID         m_note_icon_vector_dark{nullptr};
    ImTextureID         m_note_icon_tag{nullptr};
    ImTextureID         m_note_icon_tag_dark{nullptr};
    ImTextureID         m_note_icon_pencil{nullptr};
    ImTextureID         m_tree_icon_frame{nullptr};
    // Unselected-state variant of the keyframe-thumbnail frame icon.
    ImTextureID         m_tree_icon_frame_unselect{nullptr};
    ImTextureID         m_tree_icon_cross{nullptr};
    ImTextureID         m_tree_icon_cross_dark{nullptr};
    // Gear/settings icon for the "Label" section header (tree_set.svg) and its
    // hover variant (tree_set_hover.svg).
    ImTextureID         m_tree_icon_set{nullptr};
    ImTextureID         m_tree_icon_set_hover{nullptr};
    // Header collapse / expand toggle (panel_collapse.svg / panel_expand.svg).
    ImTextureID         m_panel_collapse_icon{nullptr};
    ImTextureID         m_panel_expand_icon{nullptr};
    // Help icon for the "Assembly Structure" panel header (view_help.svg).
    ImTextureID         m_structure_help_icon{nullptr};
    // Option/settings icon at the far right of the panel header (tree_option.svg).
    ImTextureID         m_structure_option_icon{nullptr};
    ImTextureID         m_structure_option_icon_dark{nullptr};
    // Per-step option icon shown at the top-right of step cards (tree_step_option.svg).
    ImTextureID         m_structure_step_option_icon{nullptr};
    // Per-step action icons at the top-right of step cards: copy / delete / add-objects.
    ImTextureID         m_structure_step_copy_icon{nullptr};
    ImTextureID         m_structure_step_copy_icon_dark{nullptr};
    ImTextureID         m_structure_step_delete_icon{nullptr};
    ImTextureID         m_structure_step_delete_icon_dark{nullptr};
    ImTextureID         m_structure_step_object_tree_icon{nullptr};
    ImTextureID         m_structure_step_object_tree_icon_dark{nullptr};
    // "Add object to step" affordance shown on each step card. Two states:
    ImTextureID         m_structure_step_add_icon_unedit{nullptr};
    ImTextureID         m_structure_step_add_icon_edit{nullptr};
    // Inline timeline button: pull transforms from the final-assembly end
    ImTextureID         m_tree_icon_from_assembly_end_frame{nullptr};
    ImTextureID         m_tree_icon_from_assembly_end_frame_dark{nullptr};
    // Whether the "Assembly Structure" panel is collapsed to header-only.
    bool                m_structure_panel_collapsed{false};
    // Card index whose "Select" popup should be opened this frame (-1 = none).
    int                 m_structure_select_popup_pending_card{-1};
    // Card index whose "Select" popup is currently active (-1 = none).
    int                 m_structure_select_popup_active_card{-1};
    int                 m_structure_select_popup_checked_card{-1};
    int                 m_structure_select_popup_tree_card{-1};
    int                 m_structure_select_popup_tree_step_node{-1};
    AssemblyTreeData    m_structure_select_popup_tree;
    std::unordered_map<std::string, bool> m_structure_select_popup_checked;
    std::map<int, std::string> m_structure_select_labels;
    std::set<int>              m_structure_select_show_default;
    // Card index whose add-object tree panel is currently shown (-1 = none).
    int                 m_structure_add_tree_card{-1};
    int                 m_structure_add_tree_step_node{-1};
    ImVec2              m_structure_add_tree_pos{0.0f, 0.0f};
    bool                m_structure_add_tree_opened_this_frame{false};
    // Step folder node whose add-object tree should auto-open once its card is
    // laid out (-1 = none). Set right after a step is created so the tree shows
    // without the user clicking the card's add affordance first.
    int                 m_structure_add_tree_pending_node{-1};
    int                 m_structure_step_rename_node{-1};
    bool                m_structure_step_rename_open_pending{false};
    bool                m_structure_step_rename_had_focus{false};
    char                m_structure_step_rename_buf[256]{};
    int                 m_structure_scroll_to_node{-1};
    // Drag-to-reorder state for non-final-assembly step cards. The drag handle is
    int                 m_structure_drag_node{-1};
    bool                m_structure_drag_active{false};
    float               m_structure_drag_start_y{0.0f};
    int                 m_structure_drag_insert_before{-1};
    bool                m_show_modelobject_name_when_modelobject_has_occur_before{true};
    // Floating "Export" button icon shown on the left of the guide panel
    ImTextureID         m_btn_icon_export{nullptr};
    // Prev/next icons used by the bottom play bar (Figma node 732:22413).
    ImTextureID         m_play_left_icon{nullptr};
    ImTextureID         m_play_right_icon{nullptr};
    // Exit icon shown to the right of the Copy/Add step button row (tree_exit.svg).
    ImTextureID         m_structure_exit_icon{nullptr};
    ImTextureID         m_structure_exit_icon_dark{nullptr};

    GLVolumeCollection *m_volumes{nullptr};
    Model              *m_model{nullptr};
    CanvasCallBack      m_commond_callback;
    Camera             *m_camera{nullptr};
    Selection          *m_selection{nullptr};
    float               m_imgui_scale{1.f};
    ImGuiWrapper       *m_imgui{nullptr};
    bool                m_is_dark{false};
    std::string         m_images_dir{};
    bool                m_play_global{false}; // play_global or play  local
    // Maximum keyframes the timeline UI is willing to display per node.
    int                 m_keyframe_max_count{3};
    //only_step_node_create_key_frame
    bool                         m_only_step_node_create_key_frame{true};
    int                          m_last_folder_idx{-1};
    // Cached screen-space anchor for the current step's volumes. All on-canvas
    Vec2d        m_selected_screen_center_{Vec2d::Zero()};
    bool         m_selected_screen_center_dirty_{true};
    Transform3d  m_last_view_matrix_for_anchor_{Transform3d::Identity()};
    Transform3d  m_last_proj_matrix_for_anchor_{Transform3d::Identity()};
    // Last camera viewport (x, y, w, h) the current static frame was framed for.
    std::array<int, 4> m_last_fit_viewport_{0, 0, 0, 0};
    // Set by on_color_mode_changed(); consumed on the next render_main() to re-fit the
    bool         m_refit_camera_pending_{false};
    // Per-frame cache: part-number label index -> per-object screen center.
    std::map<int, Vec2d> pn_screen_centers_;
    // Set when part-number labels are (re)generated: the next on-canvas render
    bool         m_pn_autolayout_pending{false};
    float        m_part_number_label_font_size{0.0f};
    // Inline rename of a part-number label's text. While active the matching pill
    // shows an ImGui InputText; committing renames the backing ModelObject /
    // ModelVolume. The target is keyed by (object_idx, volume_idx) so it survives
    // label-vector reordering. volume_idx < 0 means an object-level label.
    int          m_pn_label_rename_object_idx{-1};
    int          m_pn_label_rename_volume_idx{-1};
    bool         m_pn_label_rename_focus_pending{false};
    std::string  m_pn_label_rename_buf;
    // Row selection / hover state for the assembly tree view (render_assembly_tree_ui).
    // m_assembly_tree_selected_items holds the (object_idx, volume_idx) of every
    // selected row (green fill highlight; volume_idx < 0 = object-level). Multiple
    // rows can be selected; m_assembly_tree_hover_id caches the last hovered item's
    // ObjectID so hover_tree_item_logic() only fires when the hovered row changes.
    std::set<std::pair<int, int>> m_assembly_tree_selected_items;
    int          m_assembly_tree_hover_id{-1};
    // Inline rename of a tree-view row, keyed by (object_idx, volume_idx) of the
    // backing ModelObject / ModelVolume (volume_idx < 0 = object-level). Mirrors
    // the part-number label rename flow.
    int          m_tree_item_rename_object_idx{-1};
    int          m_tree_item_rename_volume_idx{-1};
    bool         m_tree_item_rename_focus_pending{false};
    std::string  m_tree_item_rename_buf;
    bool         m_interpolate_part_number_label_arrow_end_offset{true};
    bool         m_render_interpolated_part_number_labels{false};
    KeyFrame     m_interpolated_part_number_label_frame;
    // Snapshot of (m_selected_node, m_keyframe_selected) at the previous
    int m_last_rendered_selected_node_for_notes_{-2};
    int m_last_rendered_keyframe_selected_{-2};
    bool m_last_has_selected_node_{false};
    // Assembly tree UI state (migrated from GLCanvas3D)
    std::unordered_map<std::string, bool>* m_active_assembly_tree_checked{nullptr};
    int m_assembly_tree_ui_current_folder_node{-1};
    std::unordered_map<std::string, bool> m_assembly_tree_ui_original_checked;
    std::string m_assembly_tree_search_text;
    bool m_assembly_tree_search_active{false};
    bool m_assembly_tree_search_focus_pending{false};
    bool m_show_assembly_tree_step_quick_select{false};
    static std::unordered_map<std::string, bool> s_assembly_tree_open_nodes;
    // Assembly view capture (screenshot / video recording)
    std::unique_ptr<PBOReader>          m_pbo_reader;
    std::unique_ptr<Mp4Recorder>        m_mp4_recorder;
    bool                                m_video_recording{false};
    // Assembly steps export (one PDF page per global play frame, with step title)
    bool                                m_steps_export_active{false};
    bool                                m_steps_export_wait_frame{false};
    int                                 m_steps_export_total{0};
    std::string                         m_steps_export_dir;
    std::string                         m_steps_export_output_path;
    ExportType                          m_steps_export_type{ExportType::PDF};
    std::vector<std::string>            m_steps_export_images;
    std::vector<std::string>            m_steps_export_titles;
    std::vector<int>                    m_steps_export_step_indices;
    std::string                         m_pdf_export_title;
    std::string                         m_pdf_export_cover_image_path;
    std::string                         m_pdf_export_second_page_image_path;

    int                                 m_steps_export_original_play_index{1};
    int                                 m_steps_export_original_selected_node{-1};
    bool                                m_is_export_mode{false};
    bool                                m_gizmo_active{false};
    bool                                m_steps_video_export_active{false};
    std::string                         m_steps_video_export_path;
    std::unique_ptr<AssemblyExportProgressWindow> m_export_progress_window;
    // Set to true by on_export_mp4() right after activating the video capture
    bool                                m_video_export_skip_first_frame{false};
    PlayStrategy                        m_play_strategy{PlayStrategy::Sequential};

    bool m_only_final_assembly_endframe_effect_real_assembly{true};//very important
    bool m_play_video_and_show_panels_debug{false};

    struct AssemblyTreeIcons {
        bool        loaded{false};
        ImTextureID expand{0};
        ImTextureID collapse{0};
        ImTextureID select{0};
        ImTextureID search{0};
        // Dark-mode variants (light-colored strokes/fills)
        ImTextureID expand_dark{0};
        ImTextureID collapse_dark{0};
        ImTextureID search_dark{0};
    };
    static AssemblyTreeIcons s_assembly_tree_icons;

    double m_play_transition_expect_duration{1.0};
    double m_play_transition_duration{1.0};
    double m_play_interval_step_to_step_expect = 1.0;
    double m_play_interval_step_to_step = 1.0;
    static constexpr double kPlayFrameInterval = 0.02;
    // Right-edge X (canvas coords) of the last-rendered "Assembly Structure"
    float m_assembly_structure_right_x{0.f};
    // Right-side "Assembly Guide" panel
    int m_guide_connection_selected{-1};
    SelectionOrigin m_selection_origin{SelectionOrigin::None};
    int m_selected_node{-1};
    // (Previous m_guide_start_frame_added / m_guide_timeline_selected removed:
    bool m_guide_panel_collapsed{false};
    // Add Notes -selected tool: -1=none, 0..N-1 indexes into m_note_tools.
    int m_guide_note_tool_selected{-1};
    int m_guide_note_color_selected{2};
    // Background color palette index for TextLabel notes only (default white).
    int m_guide_note_bg_color_selected{6};
    // Show Part Numbers checkbox state.
    bool                                  m_guide_show_part_numbers{true};
    // Labels-show mode of the currently-selected keyframe; updated on each frame switch.
    LabelsShowType                        m_cur_labels_show_type{LabelsShowType::AutoRecommend};
    std::string                           m_save_project_tip_text;
    std::chrono::steady_clock::time_point m_save_project_tip_until{};
    // One entry per visible "Add Notes" tool button. Lazy-initialized in
    struct NoteTool
    {
        const char           *id;     // unique ImGui ID
        ImTextureID           icon;   // SVG texture
        ImTextureID           icon_dark; // Dark-mode SVG texture, if available
        std::string           tip;    // hover tooltip (already i18n-translated)
        std::function<void()> action; // click handler; nullptr = visual-only slot
    };
    std::vector<NoteTool> m_note_tools;
    bool                  m_play_different_folder_waiting{false};
    double                m_play_different_folder_start_time{0.0};
    int                   m_play_different_folder_phase{0};
    bool                  m_play_end_waiting{false};
    double                m_play_end_start_time{0.0};
    int                   m_pending_global_frame_index{-1};
    bool                  m_show_video_title_mode{false};
    // MP4-export-only intro: phase 0 displays the cover title for
    bool                  m_video_intro_active{false};
    int                   m_video_intro_phase{0};
    double                m_video_intro_start_time{0.0};
    double                m_video_intro_cover_duration{2.0};
    double                m_video_intro_step_duration{1.5};
    // Shared intro timing. Same values drive both preview and export.
    static constexpr double VIDEO_INTRO_COVER_DURATION = 2.0; // cover title (s)
    static constexpr double VIDEO_INTRO_STEP_DURATION  = 1.5; // Step 1 title (s)
    bool                  m_in_assembly_view{false};

    int                   m_note_text_focus_request{-1};
    // When a re-focus is requested to recover from ImGui deactivating the InputText
    bool                  m_note_text_focus_keep_cursor{false};
    ImVec2                m_panel_rect_structure_min{0, 0};
    ImVec2                m_panel_rect_structure_max{0, 0};
    ImVec2                m_panel_rect_guide_min{0, 0};
    ImVec2                m_panel_rect_guide_max{0, 0};
    ImVec2                m_panel_rect_playbar_min{0, 0};
    ImVec2                m_panel_rect_playbar_max{0, 0};
    // Extra overlay rects fed from GLCanvas3D that must also block part-number
    ImVec2                m_overlay_rect_navigator_min{0, 0};
    ImVec2                m_overlay_rect_navigator_max{0, 0};
    ImVec2                m_overlay_rect_fit_camera_min{0, 0};
    ImVec2                m_overlay_rect_fit_camera_max{0, 0};
    ImVec2                m_overlay_rect_assemble_control_min{0, 0};
    ImVec2                m_overlay_rect_assemble_control_max{0, 0};
    struct LabelLayoutForbiddenRect {
        ImVec2 min{0, 0};
        ImVec2 max{0, 0};
    };
    LabelLayoutForbiddenRect m_part_number_label_forbidden_left_area;
    LabelLayoutForbiddenRect m_part_number_label_forbidden_bottom_area;
    // On-screen (logical px) rect of the top gizmo/main toolbar, fed from GLCanvas3D.
    // Used to push the export button / guide panel down when they would overlap it.
    ImVec2                m_gizmo_toolbar_rect_min{0.f, 0.f};
    ImVec2                m_gizmo_toolbar_rect_max{0.f, 0.f};
    // When the export button would overlap the gizmo toolbar at its default spot
    bool                  m_export_btn_corner_mode{false};
    float                 m_export_btn_canvas_w{0.f};

    bool m_select_good_camera_layout_laber_after_auto_explode{true};
    // Deadline after which the "kept whole" tip auto-hides (shown for ~2s).
    std::chrono::steady_clock::time_point m_explode_collapsed_note_until{};
    std::vector<PlayFrameRef> m_play_frame_refs;
    bool                      m_play_frame_refs_dirty{true};
    std::set<size_t>                    m_last_recorded_objects;
    std::set<std::pair<size_t, size_t>> m_last_recorded_volumes;
    int                       m_assembly_play_index{1};
    int                       m_assembly_play_count{0}; // 0 mean dirty
    float                     m_margin_factor_camera_for_not_last_frame{1.4f};
    bool                      m_note_edit_controls_visible{false};
    // True only while the currently-selected text label is in caret/typing mode
    // (its InputText is the active item). Refreshed every frame by
    // render_assembly_notes_on_canvas. Used to gate the Delete-key shortcut so a
    // character delete during typing does not erase the whole label, while still
    // allowing Delete to remove a selected (non-caret) label even right after it
    // was dragged/resized (when a handle item may briefly remain active).
    bool                      m_note_text_caret_active{false};
    bool                      m_tree_icons_loaded{false};
    bool                      m_playback_paused{false};
    double                    m_playback_pause_started_at{0.0};
    GLModel                   m_arrow_line_model;
    GLModel                   m_arrow_tri_model;
    std::map<std::string, ImTextureID> m_arrow_svg_icons;
    KeyframeDisplayMode                m_keyframe_display_mode{KeyframeDisplayMode::All};
    std::deque<KeyFrame>               m_play_queue;
    char                               m_keyframe_edit_buf[256]{};
    // keyframe_entries_tree was a std::map<int, KFNodeData>; KFNodeData is now embedded directly on AssemblyStepsTreeNode::kf_data, so we look up via nodes[i].kf_data instead.
    int m_keyframe_selected{-1};
    // Cached per-frame result of is_current_keyframe_changed(), refreshed once at
    bool                      m_current_keyframe_changed{false};
    AssemblyNoteSelectionType m_note_selected_type{AssemblyNoteSelectionType::None};
    int                       m_note_selected_idx{-1};
    bool                      m_keyframe_playing{false};
    bool                      m_use_notify_open_folder_flag{true};

public:
    AssemblyStepsUtils();
    ~AssemblyStepsUtils();
public://logic
    void set_input(ImGuiWrapper *imgui, Model *model, Camera *camera, Selection *selection, GLVolumeCollection *volumes, bool gizmo_active = false);
    void set_render_input(bool is_dark, const std::string &images_dir, float imgui_scale);
    // Dark/light mode just toggled. The switch relayouts/resizes the canvas, which
    void on_color_mode_changed() { m_refit_camera_pending_ = true; }
    void set_commond_callback(CanvasCallBack calback);
    void do_commond_callback(std::string);
    void set_in_assembly_view(bool in_assembly_view);

    bool            is_key_frame_playing() { return m_keyframe_playing; }
    bool            is_final_assembly_folder(int folder_idx) const;
    int             get_selected_node() const { return m_selected_node; }
    void            set_selected_node(int node) { m_selected_node = node; }
    SelectionOrigin selection_origin() const { return m_selection_origin; }
    void            set_selection_origin(SelectionOrigin origin);
    // Single chokepoint that resets the currently selected step/object node.
    // Centralised so every "exit step" path is easy to trace / breakpoint.
    void            clear_selected_node();
    void            clear_when_no_selection();
    // Exit the assembly-step editing state: drop note/tree editing UI, clear the
    // selected step node, and ask the canvas to deselect all volumes. Triggered
    // by the exit button in the assembly structure panel (previously this was
    // done by double-clicking a blank area of the assembly view).
    void            exit_assembly_steps_editing();
    void            update_model_object_tree();
    // Returns true when the two trees differ in structure/labels/selection
    void save_assembly_steps_json_to_model();
    void save_assembly_steps_json_to_model_and_request_extra_frame();
    //selected node deal begin
    bool has_selected_node() const;
    bool is_selected_final_assembly_node() const;
    bool has_selected_final_assembly_end_keyframe() const;
    bool has_selected_step_node() const;
    int         find_parent_folder(int node_idx) const;
    void        on_selected_node_changed();
    void        on_selected_node_step_changed(int folder_idx);
    void        apply_final_assembly_end_keyframe(bool apply_camera_view = true);
    void        apply_end_keyframe(int folder_idx, bool apply_camera_view = true);
    // Per-frame edge detector intended for render_main(): when the user goes
    void        auto_apply_final_assembly_on_selection_cleared();
    // Re-map the currently selected step folder + keyframe to its global play-bar
    void        sync_play_index_to_selection();
    // After a structural edit (add / copy / insert / delete step) eagerly rebuild
    void        reschedule_play_bar_after_structure_change();
    void        update_step_screen_center();
    // When `only_object_idxs` is provided, only those object indices have their
    void        fill_folder_keyframes_from_children(int folder_idx,  bool use_glvolume_tran = false);
    std::string get_object_name(int object_idx);
    bool        has_instance(int object_idx);
    std::string              get_volume_name(int object_idx, int volume_idx) const;
    // Per-object ModelInstance::m_assemble_transformation (first instance only;
    Geometry::Transformation get_instance_transform(int object_idx) const;
    // Per-volume ModelVolume::m_assemble_transformation. Captured into
    Geometry::Transformation get_volume_transform(int object_idx, int volume_idx) const;
    int                      find_model_object_idx_by_id(size_t object_id);
    std::string              assembly_step_display_name(const Slic3r::AssemblyStepsTreeNode &node) const;
    int                      get_object_id_id(size_t object_id);
    void                     clear_selection();
    // Apply the canvas selection that corresponds to the user clicking the steps-tree
    void                     select_steps_tree_node_for_canvas(int node_idx);
    // Double-clicking a part-number label clears the current selection
    void                     select_part_label_glvolume(const PartNumberLabel &lbl);
    // Rebuild the canvas selection from m_assembly_tree_selected_items (the rows
    // selected in the assembly tree). Supports multiple objects/volumes at once.
    void                     apply_tree_items_selection_to_canvas();
    // One-shot seed of m_assembly_tree_selected_items from the current canvas
    // selection, so opening the add-object tree highlights the rows that match
    // what is selected on the canvas. Whole-object selections map to the object
    // row; partial selections map to the individual volume rows.
    void                     seed_tree_selected_items_from_canvas(const AssemblyTreeData &tree);
    // True while the add-object assembly tree view (render_assembly_tree_ui) is open on the canvas.
    bool                     is_render_assembly_tree_ui_open() const { return m_structure_add_tree_card >= 0; }
    // Re-map the current canvas selection (m_selection) onto the assembly tree
    void                     sync_tree_ui_selection_from_canvas();
    // Hover callback for a tree-view row. The argument is the unique ObjectID of
    // the hovered ModelObject / ModelVolume (-1 when no row is hovered). The
    // concrete canvas-hover effect is wired up separately.
    void                     hover_tree_item_logic(int id);
    std::vector<int>         selected_assembly_object_indices() const;
    // Upper bound for user-created (non-final-assembly) steps.
    static constexpr int     MAX_NON_FINAL_ASSEMBLY_STEPS = 99;
    // Number of top-level steps excluding the final-assembly folder.
    int                      non_final_assembly_step_count() const;
    // Whether another non-final-assembly step may still be created. Side effect:
    // caches the limit-reached state in m_non_final_assembly_step_limit_reached so the
    // UI (Copy/Add Step tooltips) can read it without re-counting every frame.
    bool                     can_add_non_final_assembly_step() const;
    // Cached negation of can_add_non_final_assembly_step()'s result (true == the
    // non-final step cap has been hit). Refreshed on every can_add_* call. mutable so
    // the const query above can update it.
    mutable bool             m_non_final_assembly_step_limit_reached{false};
    void                     add_assembly_step();
    void                     copy_assembly_step();
    void                     add_selected_to_new_assembly_step();//Add to New Step
    void                     add_selected_to_current_assembly_step();//Add to Current Step
    void                     add_selected_to_assembly_step(int folder_idx);//Add to Existing Step
    bool                     can_add_selected_to_current_assembly_step() const;
    bool                     can_add_selected_to_assembly_step() const;
    void                     record_camera(KeyFrame &kf);
    // Snapshot the assemble matrices of every currently-relevant object/volume
    void                     record_selected_volumes_by_mo_mv(KeyFrame &kf);
    // Patch the final-assembly folder's end-frame with the current canvas
    void                     update_final_assembly_end_keyframe_from_current_selection();
    void                     record_selected_gl_volume_transforms_to_current_keyframe();
    void                     show_all_volumes(bool show);
    void                     show_volume(int object_id, bool show);
    void                     apply_camera(const KeyFrame &frame);
    // Frame the current step: compute the union bbox of all active GLVolumes in
    void                     fit_camera_to_current_step_main_plane(double margin_factor);
    // Rescale a restored user-framed keyframe's zoom to the current viewport so the
    void                     rescale_user_camera_zoom_to_viewport(const KeyFrame &kf);
    // The currently selected keyframe entry (or nullptr when none is selected).
    KeyFrameEntry           *get_selected_keyframe_entry();
    // Apply an edited camera margin to the current keyframe: store it, re-frame the
    void                     apply_camera_margin_to_selected_keyframe(float margin_factor, bool commit);
    // Write a keyframe's per-object instance assemble matrix back to
    void                     apply_instance_transform(int object_idx,
                                                      const Geometry::Transformation &transform);
    // Write a keyframe's per-volume assemble matrix back to
    void                     apply_volume_transform(int object_idx, int volume_idx,
                                                    const Geometry::Transformation &transform);
    void                     apply_regular_steps_start_frame_transforms_to_current(bool include_volume_transforms);
    // Patch the currently-selected keyframe's per-object/per-volume
    void                     apply_final_assembly_end_frame_transforms_to_current_keyframe();
    // Patch the currently-selected keyframe's per-object/per-volume transforms
    // from the given source frame and push the result to the canvas. When
    // restrict_to_filters is true, only objects in object_filter and volumes in
    // volume_filter are copied; otherwise every transform in src is applied.
    void                     apply_src_frame_transforms_to_current_keyframe(KeyFrameEntry &src,
                                                    const std::set<int> &object_filter = {},
                                                    const std::set<std::pair<int, int>> &volume_filter = {},
                                                    bool restrict_to_filters = false);
    // Copy the final-assembly end frame's transforms into the given target keyframe (data only, no canvas push).
    void                     apply_final_assembly_end_frame_transforms_to_keyframe(KeyFrameEntry &target);
    // Returns true when the currently-selected keyframe's
    bool                     current_keyframe_matches_final_assembly_end_frame_transforms() const;
    // Returns true when the final-assembly end frame recorded exactly the same
    bool                     final_assembly_end_frame_matches_model() const;
    void                     set_cursor(AssemblyNoteCursorType);
    void                     reset_cursor_if_note_cursor();
    const float              get_imgui_scale() const;
    void          set_imgui_scale(float scale);
    void                     apply_object_state(int object_idx, const KeyframeObjectDisplayState &state);
    void                     look_cur_frame_logic(const KeyFrameEntry &entry);
    int                      get_object_volume_count(int object_idx);
    std::string              get_object_volume_name(int object_idx, int volume_idx);
    void                     set_note_edit_controls_visible(bool visible) { m_note_edit_controls_visible = visible; }
    bool                     is_note_edit_controls_visible() const { return m_note_edit_controls_visible; }
    void                     set_note_selection(AssemblyNoteSelectionType type, int idx);

    bool                     goto_global_frame(int global_idx);
    // Map a horizontal mouse position over the progress bar to a global frame and
    // seek to it. progress_x0 is the bar's left screen-x, progress_w its width.
    bool                     seek_global_frame_from_mouse_x(float mouse_x, float progress_x0, float progress_w, int total_frames);
    void                     play_global_frame(bool from_btn_click = false);
    void                     start_playback_with_intro();
    bool                     prepare_global_playback_with_intro(bool export_mode);
    // Shared intro initializer used by both the playback preview and MP4 export.
    void                     begin_video_intro();
    void                     prepare_export_to_play_global_frame();
    void                     pause_global_frame();
    void                     pause_playback();
    void                     resume_playback();
    void                     clear_playback_pause_state();
    // If playback is paused on the video-intro/title overlay, leave that title mode
    void                     exit_title_mode_if_paused();
    void                     play_different_folder_logic();
    // Drives the MP4 video intro phases. Only invoked while
    void                     play_video_intro_logic();
    void                     auto_explode_current_keyframe();
    void                     on_export(ExportType type);
    std::string generate_output_path(ExportType type);
    bool                     is_export_target_locked(const std::string &path);

    void                     on_export_pdf(std::string path);
    void                     on_export_markdown(std::string path);
    void                     on_export_mp4(std::string path);
    wxWindow*                assembly_export_progress_anchor() const;
    void                     show_assembly_export_progress(ExportType type, const std::string &path, int value, int maximum);
    void                     update_assembly_export_progress(ExportType type, const std::string &path, int value, int maximum);
    void                     hide_assembly_export_progress();
    void                     save_existing_project_if_dirty();
    // Reveal a freshly exported file in the system file manager (selects the file on Windows, opens the containing folder on macOS / Linux).
    void                     open_export_output_folder(const std::string &file_path);
    bool is_export_mode() const { return m_is_export_mode; }
    // True whenever the canvas should render the centred title overlay instead
    bool is_show_video_title_mode() const { return m_show_video_title_mode || m_video_intro_active; }
    bool is_play_or_export_mode() const { return is_show_video_title_mode() || is_export_mode() || m_keyframe_playing; }

    // Drains a frame from the GL pipeline into the MP4 encoder. Must be invoked
    void process_video_capture_per_frame();
    // Per-frame driver for the PDF/Markdown export state machine. Must be invoked after
    void process_assembly_pdf_capture();
    void clear_runtime_state();
    bool prepare_project_save_end_frame();
    void clear_steps_all();
    void new_project_clear_assembly_steps_tree_view();
    bool             has_pending_play_frames() const;
    std::vector<int> selected_object_indices(int object_count, const std::vector<int> &selection_object_indices) const;
    int              next_node_id() const;
    int              next_step() const;

    int              create_folder_node(const std::string &name, int step);
    int              create_object_node(int object_idx, const std::string &name, size_t obj_id);
    int              create_assembly_step_from_objects(const std::vector<int> &object_idxs);
    bool             add_objects_to_assembly_step(int folder_idx, const std::vector<int> &object_idxs);
    std::vector<int> sorted_step_nodes() const;
    bool                                     can_add_objects_to_step(bool has_volume_selection, const std::vector<int> &object_idxs) const;
    std::vector<std::pair<int, std::string>> assembly_step_choices() const;
    std::string                              build_steps_json_string();
    void                                     sync_steps_objects_with_model();
    // Reconcile the final-assembly ("All Objects") end-frame children with the
    void                                     sync_all_model_object_to_final_assembly_node();
    int                                      ensure_final_assembly_folder();
    void                                     sync_keyframe_tree();
    void                                     ensure_default_keyframe(int node_idx);
    void                                     ensure_default_keyframe_for_node(int node_idx, const std::string &last_frame_name);
    // Seed a freshly-created step's end frame with the current camera, so switching to
    void                                     seed_end_frame_camera_from_current(int node_idx);
    KeyFrameEntryVector                     *get_current_kf_entries();
    void                                     fill_default_transforms(KeyFrameEntry &entry, int object_idx);
    int                                      default_keyframe_index();
    void                                     try_update_selected_keyframe();
    bool                                     allow_sync_in_assemble_view();

    bool add_arrow_svg_note(const std::string &svg_name);
    bool add_text_label_note();
    bool add_circle_note();
    bool add_rectangle_note();
    bool add_plain_arrow_note();
    // user_initiated == true means the user just (re)checked "Show Part
    void toggle_part_number_labels(bool user_initiated = true);
    // Switch which part-number labels are shown. Persists the choice into the
    // current keyframe. reframe_camera == false keeps the current camera (the
    // type rows only relayout labels); the dedicated camera action sets it true.
    void set_labels_show_type(LabelsShowType type, bool reframe_camera = false);
    // Re-run the pill auto-arrange against the current camera, without reframing
    void auto_layout_labels_in_current_view();
    // Enter inline-rename mode for the given part-number label (its text becomes
    // an editable field). Committing renames the backing ModelObject/ModelVolume.
    void begin_part_label_rename(const PartNumberLabel &lbl);
    // Confirm any pending part-label inline rename (Enter, ImGui focus loss, or a
    // canvas click that clears the selection). No-op when not renaming.
    void commit_part_label_rename();
    // Enter inline-rename mode for a tree-view row backed by the given
    // ModelObject (volume_idx < 0) / ModelVolume; committing reuses
    // rename_model_item_from_label.
    void begin_tree_item_rename(int object_idx, int volume_idx, const std::string &name);
    // Apply a new name to the ModelObject (volume_idx < 0) or ModelVolume the
    // label points at. Returns true when the model name actually changed.
    bool rename_model_item_from_label(int object_idx, int volume_idx, const std::string &new_name);
    // Reframe + persist the recommended camera angle for the current keyframe
    // only, without rebuilding or auto-arranging the part-number labels.
    void auto_recommend_camera_for_current_view();
    void update_part_number_label_font_size_from_config();
    float part_number_label_font_size() const;
    void save_part_number_label_font_size_to_config(float font_size, bool save_now = false);
    // Rebuild the given keyframe entry's part-number labels (and optionally
    void toggle_part_number_labels_to_keyframe(KeyFrameEntry &src, bool user_initiated = true, bool reframe_camera = true);
    // --- Part-number label data generators (one per LabelsShowType) ---
    void collect_part_number_label_refs(int collect_root,
                                        const std::function<bool(int /*object_idx*/)> &as_object_label,
                                        std::vector<PartNumberLabel> &out) const;
    void build_part_number_labels_object_only(int collect_root, std::vector<PartNumberLabel> &out) const;
    void build_part_number_labels_volume_only(int collect_root, std::vector<PartNumberLabel> &out) const;
    void build_part_number_labels_auto(int collect_root, bool object_level_only, std::vector<PartNumberLabel> &out) const;
    // One-shot rail auto-layout for the given labels (assigns arrow offsets so
    bool auto_layout_part_number_labels(std::vector<PartNumberLabel> &pn_labels,
                                        const Camera &camera,
                                        const std::array<int, 4> &viewport,
                                        float sc);
    // Compute the on-screen (pixel) coordinates of the merged bounding-box center of
    static Vec2d compute_selected_volumes_screen_center(const Camera &camera, const std::vector<GLVolume *> &volumes);
    // Screen-space anchor center for an arrow-svg note: the bbox center of the
    Vec2d compute_arrow_svg_anchor_center(const ArrowSvgNote &arrow, const Vec2d &fallback_center);
    // Screen-space anchor center for any note bound to a set of ModelVolumes: the
    Vec2d compute_note_anchor_center(const std::vector<std::pair<int, int>> &bound_volumes, const Vec2d &fallback_center);
    // Fill bound_volumes with the (object_idx, volume_idx) of the currently selected
    void  bind_current_selection_volumes(std::vector<std::pair<int, int>> &bound_volumes) const;

    void deal_once_when_enter_assembly_view();

    // Build a temporary object -> volume tree for the Assembly Structure card
    AssemblyTreeData build_structure_card_select_tree_data(int step_node_idx) const;
    AssemblyTreeData build_model_object_tree_data(bool include_model_root_node = true) const;
    // Build the panel data from the current model + assembly tree state.
    AssemblyStructurePanelData build_assembly_structure_panel_data() const;
    // Right edge X (canvas coords) of the most-recently rendered
    // "Assembly Structure" panel, or 0 when it hasn't been drawn yet.
    float get_assembly_structure_right_x() const { return m_assembly_structure_right_x; }
    void  set_gizmo_toolbar_rect(float x0, float y0, float x1, float y1);
    // Overlay regions (rendered by GLCanvas3D) that part-number labels
    enum class AssemblyOverlayRect { Navigator, FitCamera, AssembleControl };
    void  set_assembly_overlay_rect(AssemblyOverlayRect which, const ImVec2 &mn, const ImVec2 &mx);
    // Rendered footprint (w, h) of the floating export button, shared by the renderer
    // and the toolbar collision tests so both use the exact same rect.
    ImVec2 export_button_size(float sc) const;
    // Downward shift for the guide panel so it clears the top gizmo toolbar (and the
    // relocated corner export button) when they overlap. Also updates
    // m_export_btn_corner_mode based on a precise export-button vs toolbar AABB test.
    float get_guide_panel_y_offset(float guide_x, float guide_y_base, float guide_w, float sc);
    void  record_keyframe_logic(KeyFrameEntry &entry);
    void  apply_keyframe_to_canvas(const KeyFrame &kf, bool apply_camera_view = true);
    void  play_cur_keyframe_logic();
    void  sync_canvas_selection_state();
    // Each operates on the current node's keyframe entries via get_current_kf_entries(),
    void delete_selected_keyframe();
    // Insert a new keyframe right after the selection, clamped before the "last" frame.
    void insert_keyframe_after_selected();
    // Start playing all keyframes for the current node from the beginning.
    void play_all_keyframes_for_current_node();
    bool should_show_panels();
    void clear_active_assembly_tree_checked();
    // Seed right-side steps tree from a STEP import tree
    void create_assembly_steps_from_step_import_tree(const std::vector<StepImportTreeNode> &step_nodes, const std::string &source_path);
    // Build left-side assembly tree from Model objects and plates
    AssemblyTreeData build_assembly_tree_data();
    void             show_all_volume_normal_render();
    // Render every part as a translucent "candidate" for a freshly-created step: parts
    void             show_volumes_as_step_candidates();
    KeyframeDisplayMode keyframe_display_mode() const { return m_keyframe_display_mode; }
    // Overload that updates `m_keyframe_display_mode` first, then applies it.
    void apply_keyframe_display_mode();
    void apply_keyframe_display_mode(KeyframeDisplayMode mode);
    // Live preview of the display mode while the add-object tree is open: drives
    void apply_tree_checked_display_mode(const AssemblyTreeData &tree,
                                         const std::unordered_map<std::string, bool> &checked);

    static KeyFrame interpolate_keyframe(const KeyFrame &from, const KeyFrame &to, double t, bool interpolate_part_number_label_arrow_end_offset = true);
    void            build_local_play_queue();
    void            exit_note_edit();
    void            clear_note_selection();
    void            invalidate_play_frame_refs();
    void            rebuild_play_frame_refs();
    void                                    sync_canvas_selection_to_tree(bool selection_empty, bool selection_instance, const std::vector<int> &selected_object_indices);
    std::vector<AssemblySelectionMatchInfo> sync_single_canvas_selection_to_tree_or_get_matches(bool selection_empty, int selected_object_idx, int selected_volume_idx);
    void                                    sync_structure_select_popup_to_canvas(const AssemblyTreeData &popup_tree);
    // Highlight the canvas from an arbitrary tree + checked map (shared by the select popup and the "List" add-object tree).
    void                                    sync_checked_tree_to_canvas(const AssemblyTreeData &tree,
                                                                        const std::unordered_map<std::string, bool> &checked);
    void                                    begin_structure_step_rename(int node_idx, const std::string &fallback_title = std::string());
    void                                    open_structure_add_tree(int card_idx, int step_node_idx, const ImVec2 &pos);
    // Queue the currently-selected step folder so its add-object tree auto-opens
    // on the next panel layout (used after add_assembly_step()).
    void                                    auto_open_add_tree_for_selected_step();
    void                                    exit_render_assembly_tree_ui();
    // Inserts a step relative to ref_node_idx. When copy is true the reference step
    void                                    insert_structure_step_relative(int ref_node_idx, bool before, const std::string &folder_name = std::string(), bool copy = true);
    // Reorder a non-final-assembly step so it sits right before `before_node`
    // (use -1 to move it after the last step). Only the relative order of
    // non-final steps changes; the final-assembly slot stays put.
    void                                    reorder_structure_step(int moved_node, int before_node);
    void                                    delete_structure_step(int node_idx);
    void                                    show_pdf_export_settings_dialog();
    // Reverse direction: take the current canvas Selection and write the matching
    void apply_canvas_selection_to_popup_checked(int card_idx, const AssemblyTreeData &popup_tree);
    // Mouse-driven flavor: applies only when the current popup tree is built
    void sync_canvas_selection_to_selected_node_popup_checked();
    void renumber_structure_step_roots();
    // Force the final-assembly step's step number to be the largest among all
    void update_final_assembly_step_number_to_max();
    // Click handler for an item in the keyframe list. Selects the row, focuses
    void on_keyframe_list_item_clicked(int idx, KeyFrameEntry &entry);
    // Record / re-record the keyframe at `idx` from the current canvas state.
    void          record_keyframe_at(int idx);
    // True when the live camera no longer matches the camera stored in the
    // currently selected keyframe (i.e. the user moved the view since the frame
    // was recorded). Drives the "unsaved view" dot in the timeline thumbnail.
    bool          is_current_keyframe_changed();
    std::set<int> collect_node_object_indices(int node_idx) const;
    // Returns true if `object_idx` already appears in any step folder ordered
    bool          is_object_used_in_previous_steps(int object_idx, int folder_idx) const;
    // Returns true if `object_idx` already appeared in an earlier keyframe of the
    bool          is_object_used_in_current_step(int object_idx, int folder_idx, int frame_id) const;
    bool          is_empty_structure_step(int folder_idx) const;
    void          select_node_and_show_volumes(int node_idx);
    void          update_structure_select_label(int card_idx, const AssemblyTreeData &popup_tree);
    void          reseed_assembly_tree_checked_from_step(int step_node_idx, const AssemblyTreeData &tree);
    void          clear_all_keyframe_part_number_labels();

public://imgui
    void init_tree_icons();
    void                     render_main(float canvas_w, float canvas_h);
    // Bottom-centered play bar (Figma node 732:22413). Renders above the
    void                     render_assemble_play_bar(float canvas_w, float bottom_y);
    // Re-derive the `Show Part Numbers` checkbox state from the currently
    void refresh_guide_show_part_numbers_from_current();
    void draw_arrow_lines(const std::vector<std::pair<ImVec2, ImVec2>> &arrows,const std::array<float, 4> &color,float thickness,const std::array<int, 4> &viewport, bool draw_arrowhead = false);
    void draw_arrow_svg_icon(int idx, const ImVec2 &center, const ImVec2 &box_sz, ImTextureID tex, bool selected) const;
    void render_part_number_labels_on_canvas(const std::array<int, 4> &viewport, float viewport_height);
    void render_assembly_notes_on_canvas(const Vec2d &object_screen_center);
    void render_assembly_structure_panel(float canvas_w, float canvas_h);
    void render_panel_tooltip(const std::string &text, bool use_dark_style = true) const;
    void render_assembly_structure_option_menu(ImGuiWrapper &imgui, float sc, bool is_dark);
    void render_structure_step_option_menu(int card_idx, const AssemblyStructureCard& card,
                                           const ImVec2& anchor, float sc, bool is_dark);
    ImTextureID get_arrow_svg_icon(const std::string &svg_name);
    // Assembly tree UI (left-side part list with checkboxes)
    void render_assembly_tree_ui(float panel_x, float panel_y, float panel_w, float panel_h, float sc);
    // Apply the "List" tree checkbox state to a single step: drop unchecked
    // object children and add step-owned nodes for newly checked objects. Only
    // touches the given step folder, so an object may belong to several steps.
    void apply_assembly_tree_checked_to_step(int active_step_node,
        const AssemblyTreeData& tree,
        const std::unordered_map<std::string, bool>& checked);
    AssemblyTreeRenderResult render_assembly_tree_selector(const AssemblyTreeData& tree,
        std::unordered_map<std::string, bool>& checked,
        const AssemblyTreeRenderOptions& options, float sc);
    void render_assembly_guide_panel(float panel_x, float panel_y, float panel_w, float panel_h, float sc, bool is_dark);
    // Floating "Export" button anchored to the LEFT of the guide panel.
    void render_assembly_guide_export_button(float panel_x, float panel_y, float sc);
    // Connection type button widget: draws icon + label in a white card,
    bool render_connection_type_btn(ImDrawList *dl, float x, float y, float w, float h, ImTextureID icon, const char *label,
                                    float icon_sz, float label_fs, float sc,
                                    bool selected, ImU32 label_col, ImU32 brand_col,
                                    const char *tooltip = nullptr);
    // Cyber Brick section widget: dashed placeholder area with "+" button and hint text.
    bool render_cyber_brick_section(ImDrawList *dl, ImVec2 card_min, float card_w, float card_h,
                                    float font_sz, float small_fs, float sc);
    // Timeline keyframe slot widget.
    int  render_timeline_keyframe(ImDrawList *dl, float x, float y, float w, float h,
                                  bool has_keyframe, bool selected,
                                  const char *label, float label_fs, float sc,
                                  bool show_delete_badge = true);
    // Generic note-tool button: white rounded card with optional SVG icon centered.
    bool render_note_tool_btn(ImDrawList *dl, float x, float y, float sz,
                              ImTextureID icon, bool selected, const char *id, float sc,
                              const char *tooltip = nullptr);
    bool render_note_color_control(ImDrawList *dl, float x, float y, float sc);
    // Second swatch row: background color for TextLabel notes only.
    bool render_note_bg_color_control(ImDrawList *dl, float x, float y, float sc);
    bool render_footer_button(const char* id, const std::string& label,
                              const ImVec2& pos, const ImVec2& size,
                              bool primary, float sc);
    void render_export_menu_popup(const char* popup_id, float sc);
    // Popup menu to choose which part-number labels to show
    void render_labels_show_type_menu_popup(const char* popup_id, float sc);
    // Generic clickable square checkbox. `*checked` is read and toggled on click. Returns true when the state was changed.
    bool render_checkbox(ImDrawList *dl, float x, float y, float sz,
                         bool *checked, const char *id, float sc);
    // "Label" settings section (Figma 4125:11315): a gear icon (tree_set.svg) next
    // to the title opens a settings popup (Figma 4125:11316). Draws the section
    // content (rows + gear) into the card the caller already positioned; the card
    // background/title are drawn by the caller's section_begin. UI-only for now.
    void render_assembly_label_settings_section(ImDrawList *draw_list, const ImVec2 &card_min,
                                                float card_w, float card_h, float sc);
    // Popup opened by the gear icon in that section (Figma 4125:11316): a white
    // rounded panel with a drop shadow. `anchor` is the desired top-left (below
    // the gear); the popup width is sized to its content and shifted left when it
    // would run off the right edge of the screen.
    void render_assembly_label_settings_popup(const char *popup_id, float sc, const ImVec2 &anchor);

private://logic
    bool          is_part_number_label_layout_overlapped(const ImVec2 &rect_min, const ImVec2 &rect_max) const;
    static ImVec2 nearest_rect_anchor(const ImVec2 &rect_min, const ImVec2 &rect_max, const ImVec2 &from, bool include_corners = false);
    static bool   rects_overlap(const ImVec2 &lhs_min, const ImVec2 &lhs_max, const ImVec2 &rhs_min, const ImVec2 &rhs_max);
    void update_part_number_label_forbidden_layout_areas(float canvas_w, float canvas_h);
    bool capture_assembly_screenshot_to_png(const std::string &filename);
    // Assembly steps export helpers (entry point is on_export).
    void process_assembly_steps_export();
    void process_assembly_steps_video_export();
    // Finalize the steps export: build the PDF, restore the canvas state.
    void finalize_steps_export();
    // Build a multi-page PDF with step grouping: frames sharing the same step are labelled X.1, X.2, etc. Multiple images fit on one page when possible.
    bool build_assembly_steps_pdf(const std::string &pdf_filename,
                                  const std::vector<std::string> &png_images,
                                  const std::vector<std::string> &page_titles,
                                  const std::vector<int> &step_indices);
    void consume_play_queue_frame(bool update_global_index);
    static bool load_assembly_tree_icons(float sc);
    bool        is_mouse_over_blocking_panel() const;
    void        track_assembly_view_export(ExportType type) const;
    void        reset_state_on_model_changed();
    void        record_current_model_as_last_final_assembly();
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_AssemblyStepsUtils_hpp_
