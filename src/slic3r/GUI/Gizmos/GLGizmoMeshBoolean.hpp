#ifndef slic3r_GLGizmoMeshBoolean_hpp_
#define slic3r_GLGizmoMeshBoolean_hpp_

#include "GLGizmoBase.hpp"
#include "GLGizmosCommon.hpp"
#include "libslic3r/Model.hpp"
#include "imgui/imgui.h"
#include <functional>
#include <set>
#include <vector>
#include <string>
#include <unordered_map>
#include <array>
#include <optional>

namespace Slic3r {
class GLVolume; // forward declaration in the correct namespace
namespace GUI {
class Selection; // forward declaration to be used in header methods
class MeshBooleanUI; // forward declaration for UI layer

// ========================== CORE BOOLEAN OPERATION CONFIGURATION ==========================

struct MeshBooleanConfig {
    // ========================== SPACING SYSTEM ==========================
    static constexpr float SPACING_XS = 2.0f;    // Extra small spacing
    static constexpr float SPACING_SM = 4.0f;    // Small spacing
    static constexpr float SPACING_MD = 6.0f;    // Medium spacing
    static constexpr float SPACING_LG = 8.0f;    // Large spacing

    // ========================== STANDARD SIZES ==========================
    static constexpr float HEIGHT_STANDARD = 32.0f;  // Standard component height
    static constexpr float BUTTON_WIDTH = 60.0f;     // Standard button width

    // ========================== ICON SIZES ==========================
    static constexpr float ICON_SIZE_DISPLAY = 20.0f;  // Display icons in lists
    static constexpr float ICON_SIZE_BUTTON = 28.0f;   // Interactive button icons
    static constexpr float ICON_SPACING = SPACING_SM;  // Spacing around icons
    static constexpr float ICON_PADDING = SPACING_LG;  // Padding inside icon areas

    // ========================== LIST DIMENSIONS ==========================
    static constexpr float LIST_WIDTH = 480.0f;        // Total list width
    static constexpr float LIST_HEIGHT = 190.0f;       // List content height
    static constexpr float LIST_TITLE_HEIGHT = HEIGHT_STANDARD;  // List header height
    static constexpr float LIST_ITEM_HEIGHT = 24.0f;   // Individual item height
    static constexpr float LIST_PADDING = 12.0f;       // Padding between lists

    // ========================== LAYOUT DIMENSIONS ==========================
    static constexpr float CONTROL_WIDTH = 460.0f;     // Control area width
    static constexpr float TAB_HEIGHT = HEIGHT_STANDARD;  // Tab button height

    // ========================== VISUAL STYLING ==========================
    static constexpr float ROUNDING_BUTTON = 6.0f;     // Button corner rounding
    static constexpr float ROUNDING_LIST = 10.0f;      // List container rounding
    static constexpr float ROUNDING_NONE = 0.0f;       // No rounding

    // ========================== SPACING DETAILS ==========================
    static constexpr float SPACING_TEXT = SPACING_MD;           // Text padding
    static constexpr float SPACING_LIST_TITLE = SPACING_SM;     // Title to list spacing
    static constexpr float SPACING_LIST_ITEM = SPACING_MD;      // Between list items
    static constexpr float SPACING_VERTICAL_REDUCTION = 0.6f;   // Compact mode factor

    // ========================== VISUAL EFFECTS ==========================
    static constexpr float BORDER_WIDTH_HOVER = 1.5f;          // Hover border thickness

    // ========================== INTERNAL PADDING ==========================
    static constexpr float PADDING_LIST_FRAME_X = SPACING_LG;  // List horizontal padding
    static constexpr float PADDING_LIST_FRAME_Y = SPACING_SM;  // List vertical padding
    static constexpr float MARGIN_LAYOUT = SPACING_XS;         // Layout margins
    static constexpr float MARGIN_BOTTOM = SPACING_SM;         // Bottom margins
    static constexpr float MARGIN_RIGHT_OFFSET = SPACING_XS;   // Right margin adjustment

    // ========================== RENDERING (GIZMO) ==========================
    static constexpr float CORNER_BOX_LINE_SCALE = 3.0f;

    // ========================== BOOLEAN OPERATIONS ==========================
    static constexpr const char* OP_UNION = "UNION";
    static constexpr const char* OP_INTERSECTION = "INTERSECTION";
    static constexpr const char* OP_DIFFERENCE = "SUBSTRACTION";

    // ========================== UI COLORS ==========================
    static constexpr unsigned int COLOR_LIST_A = 0xFFFFCC75;        // List A title (light blue)
    static constexpr unsigned int COLOR_LIST_B = 0xFF00ECBD;        // List B title (green-yellow)
    static constexpr unsigned int COLOR_SELECTED_BG = 0xFFF1FBEA;   // Selected item background light
    static constexpr unsigned int COLOR_SELECTED_BG_DARK = 0xFF354124; // Selected item background dark
    static constexpr unsigned int COLOR_HOVER_BORDER = 0xFF42AE00;  // Hover border
    static constexpr unsigned int COLOR_NON_MODEL = 0x80808080;     // Non-model volumes (semi-transparent gray)
    static constexpr unsigned int COLOR_SEPARATOR = 0xFFCCCCCC;
    static constexpr unsigned int COLOR_SEPARATOR_DARK = 0xFF4A4A4A;
    static constexpr unsigned int COLOR_TEXT = 0xFF000000;
    static constexpr unsigned int COLOR_TEXT_DARK = 0xFFF0EFEF;
};

// ========================== CORE ENUMS AND STRUCTS ==========================

enum class MeshBooleanOperation {
    Undef = -1,
    Union,
    Difference,
    Intersection,
};

enum class BooleanTargetMode {
    Object = 0,
    Part   = 1,
    Unknown = -1
};

// ========================== VOLUME LIST MANAGEMENT ==========================

class VolumeListManager {
public:
    enum class ListType {
        Working,    // For Union/Intersection
        ListA,      // For Difference A
        ListB       // For Difference B
    };

    VolumeListManager();

    // Core operations
    bool selection_changed(const Selection& selection);
    void init_part_mode_lists(const Selection& selection);
    void init_object_mode_lists(const Selection& selection);
    void clear_all();

    // Part mode: add by volume
    bool add_volume_to_list(unsigned int volume_idx, bool to_a_list);

    // Object mode: add by object
    bool add_volumes_to_list(const std::vector<unsigned int>& volume_indices, bool to_a_list);
    bool add_object_to_list(int object_idx, bool to_a_list, const Selection& selection);

    // List access and manipulation
    // Volume lists getters
    const std::vector<unsigned int>& get_working_list() const { return m_working_volumes; }
    const std::vector<unsigned int>& get_list_a() const { return m_a_list_volumes; }
    const std::vector<unsigned int>& get_list_b() const { return m_b_list_volumes; }

    const std::set<unsigned int>& get_selected_objects() const { return m_selected_objects; }
    const std::set<unsigned int>& get_selected_a_objects() const { return m_selected_a_objects; }
    const std::set<unsigned int>& get_selected_b_objects() const { return m_selected_b_objects; }

    void add_to_selection(unsigned int volume_idx) { m_selected_objects.insert(volume_idx); }
    void remove_from_selection(unsigned int volume_idx) { m_selected_objects.erase(volume_idx); }
    void add_to_selection_a(unsigned int volume_idx) { m_selected_a_objects.insert(volume_idx); }
    void add_to_selection_b(unsigned int volume_idx) { m_selected_b_objects.insert(volume_idx); }

    // Utility functions
    std::set<unsigned int> remove_selected_from_all_lists();
    void remove_indices_from_all_lists(const std::set<unsigned int>& indices);
    void move_selected_to_left();   // Move selected from B -> append to A, and delete from B
    void move_selected_to_right();  // Move selected from A -> append to B, and delete from A
    void swap_lists();              // Swap entire A and B lists

    void update_obj_lists(const Selection& selection);
    const std::vector<int>& get_object_list_a() const { return m_a_list_objects; }
    const std::vector<int>& get_object_list_b() const { return m_b_list_objects; }
    const std::vector<int>& get_object_working_list() const { return m_working_objects; }

    // Validation
    bool validate_for_union() const;
    bool validate_for_intersection() const;
    bool validate_for_difference() const;

    // Helper functions
    std::vector<std::string> get_volume_names(const std::vector<unsigned int>& volume_list, const Selection& selection) const;

    // Sorting function to prioritize MODEL_PART volumes
    void sort_volumes_by_type(std::vector<unsigned int>& volume_indices, const Selection& selection);

private:
    // Volume lists
    std::vector<unsigned int> m_working_volumes;
    std::vector<unsigned int> m_a_list_volumes;
    std::vector<unsigned int> m_b_list_volumes;

    // Object lists (derived from volume lists)
    std::vector<int> m_a_list_objects;
    std::vector<int> m_b_list_objects;
    std::vector<int> m_working_objects;

    // Selected objects
    std::set<unsigned int> m_selected_objects;    // For union/intersection operations
    std::set<unsigned int> m_selected_a_objects;  // For difference operation - A group
    std::set<unsigned int> m_selected_b_objects;  // For difference operation - B group

};

// ========================== BOOLEAN OPERATIONS ENGINE ==========================

struct BooleanOperationSettings {
    bool keep_original_models = false;
    bool entity_only = true;
    BooleanTargetMode target_mode { BooleanTargetMode::Unknown };

    // List of non-model volumes to attach
    std::vector<ModelVolume*> non_model_volumes_to_attach;
};

struct BooleanOperationResult {
    bool success = false;
    std::string error_message;
    std::vector<TriangleMesh> result_meshes;
    std::vector<ModelVolume*> source_volumes;
    std::vector<Transform3d> source_transforms;
    std::vector<ModelVolume*> volumes_to_delete;
};

class BooleanOperationEngine {
public:
    BooleanOperationEngine();

    // Main operation methods
    BooleanOperationResult perform_union(const VolumeListManager& volume_manager,
                                       const Selection& selection,
                                       const BooleanOperationSettings& settings = {});

    BooleanOperationResult perform_intersection(const VolumeListManager& volume_manager,
                                              const Selection& selection,
                                              const BooleanOperationSettings& settings = {});

    BooleanOperationResult perform_difference(const VolumeListManager& volume_manager,
                                            const Selection& selection,
                                            const BooleanOperationSettings& settings = {});

    // Validation and utility methods
    std::string validate_operation(MeshBooleanOperation type, const VolumeListManager& volume_manager) const;
    void apply_result_to_model(const BooleanOperationResult& result, ModelObject* target_object, int object_index, const BooleanOperationSettings& settings, MeshBooleanOperation mode, const std::vector<ModelObject*>& participating_objects, const std::vector<ModelObject*>& a_group_objects = {}, const std::vector<ModelObject*>& b_group_objects = {});

    // Expose processed volume info so helper utilities in the same translation unit can reference it
    struct VolumeInfo {
        ModelVolume* model_volume;
        Transform3d transformation;
        unsigned int volume_index;
        int object_index;
    };

private:
    // Volume processing helpers
    std::vector<VolumeInfo> prepare_volumes(const std::vector<unsigned int>& volume_indices, const Selection& selection) const;
    TriangleMesh get_transformed_mesh(const VolumeInfo& volume_info) const;
    TriangleMesh execute_boolean_operation(const TriangleMesh& mesh_a, const TriangleMesh& mesh_b, const std::string& operation_name) const;

    // Core implementation methods
    BooleanOperationResult part_level_boolean(const std::vector<VolumeInfo>& volumes, const BooleanOperationSettings& settings, const std::string& operation, bool allow_single_volume = false) const;
    BooleanOperationResult part_level_sub(const std::vector<VolumeInfo>& volumes_a, const std::vector<VolumeInfo>& volumes_b, const BooleanOperationSettings& settings) const;
    std::optional<TriangleMesh> execute_boolean_on_meshes(
        const std::vector<VolumeInfo>& volumes,
        const std::string& operation) const;
    // Model manipulation helpers
    ModelVolume* create_result_volume(ModelObject* target_object, const TriangleMesh& result_mesh, ModelVolume* source_volume);
    void delete_volumes_from_model(const std::vector<ModelVolume*>& volumes_to_delete);
    void update_delete_list(BooleanOperationResult& result, const std::vector<VolumeInfo>& volumes, const BooleanOperationSettings& settings) const;

};

// ========================== MAIN GIZMO CLASS ==========================

struct MeshBooleanWarnings {
    static const std::string COMMON;
    static const std::string JOB_CANCELED;
    static const std::string JOB_FAILED;
    static const std::string MIN_VOLUMES_UNION;
    static const std::string MIN_VOLUMES_INTERSECTION;
    static const std::string MIN_VOLUMES_DIFFERENCE;
    static const std::string MIN_OBJECTS_UNION;
    static const std::string MIN_OBJECTS_INTERSECTION;
    static const std::string MIN_OBJECTS_DIFFERENCE;
    static const std::string GROUPING;
    static const std::string OVERLAPING;
};

// ========================== WARNING SYSTEM ==========================

enum class WarningSeverity { Warning, Error };

struct WarningItem {
    std::string text;
    WarningSeverity severity{ WarningSeverity::Warning };
};

class BooleanWarningManager {
public:
    BooleanWarningManager();

    // Core warning management
    void add_warning(const std::string& warning, WarningSeverity severity = WarningSeverity::Warning);
    void add_error(const std::string& error_text);
    void clear_warnings();
    void clear_mode_specific_warnings(MeshBooleanOperation mode);
    void remove_warning(const std::string& warning_text);

    // Callback when warnings change (to request UI refresh)
    void set_on_change(const std::function<void()> &cb) { m_on_change = cb; }

    // Warning retrieval and filtering
    std::vector<WarningItem> get_warnings_for_current_mode(MeshBooleanOperation mode) const;
    std::vector<WarningItem> get_inline_hints_for_state(MeshBooleanOperation mode, const VolumeListManager& volume_manager) const;
    // Validation helpers
    bool has_errors() const;
    bool has_errors_for_mode(MeshBooleanOperation mode) const;

    // UI rendering support
    void render_warning(const WarningItem& item, float width, ImTextureID warning_icon, ImTextureID error_icon, ImGuiWrapper* imgui, float icon_size);
    void render_warnings_list(const std::vector<WarningItem>& warnings, float width, ImTextureID warning_icon, ImTextureID error_icon, ImGuiWrapper* imgui, float icon_size);

private:
    std::vector<WarningItem> m_warnings;

    // Helper methods
    bool is_general_warning(const std::string& warning_text) const;
    bool is_mode_specific_warning(const std::string& warning_text, MeshBooleanOperation mode) const;

    std::function<void()> m_on_change;
};

// ========================== MAIN GIZMO CLASS ==========================
class GLGizmoMeshBoolean : public GLGizmoBase
{
public:

    GLGizmoMeshBoolean(GLCanvas3D& parent, unsigned int sprite_id);
    ~GLGizmoMeshBoolean();

    void set_enable(bool enable) { m_enable = enable; }
    bool get_enable() const { return m_enable; }

    bool gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down) override;

    std::string get_icon_filename(bool b_dark_mode) const override;
    void apply_color_overrides_for_mode(MeshBooleanOperation mode);

protected:
    virtual bool on_init() override;
    virtual std::string on_get_name() const override;
    virtual std::string on_get_name_str() override { return "Mesh Boolean"; }
    virtual bool on_is_activable() const override;
    virtual void on_render() override;
    virtual void on_render_for_picking() override {}
    virtual void on_set_state() override;
    virtual CommonGizmosDataID on_get_requirements() const override;
    virtual void on_render_input_window(float x, float y, float bottom_limit);
    virtual void on_change_color_mode(bool is_dark) override;
    void on_load(cereal::BinaryInputArchive &ar) override;
    void on_save(cereal::BinaryOutputArchive &ar) const override;

private:
    struct SavedVolumeColorState {
        std::array<float, 4> original_color;
        bool original_force_native_color;
        bool original_force_neutral_color;
    };

    // Helper to manage temporary volume color overrides during gizmo lifetime
    struct ColorOverrideManager {
        std::unordered_map<unsigned int, SavedVolumeColorState> saved; // key: global volume index
        bool applied { false };

        void apply_for_indices(const Selection &selection,
                               const std::vector<unsigned int>& volume_indices,
                               const std::array<float,4>& rgba);

        void restore_non_selected_indices(const Selection &selection);
        void clear() { saved.clear(); applied = false; }
    };


    // ========================== CORE MODULES ==========================
    bool m_enable{ false };
    BooleanWarningManager m_warning_manager;
    ColorOverrideManager m_color_overrides;
    VolumeListManager m_volume_manager;
    BooleanOperationEngine m_boolean_engine;
    mutable int m_last_plate_idx_for_visibility {-1};

    void reset_gizmo();
    void execute_mesh_boolean();
    void init_volume_manager();
    bool get_cur_entity_only() const;
    bool is_on_same_plate(const Selection& selection) const;
    BooleanTargetMode update_cur_mode(const Selection& selection) const;

    // ========================== UI LAYER ==========================
    std::unique_ptr<MeshBooleanUI> m_ui;
    MeshBooleanOperation m_operation_mode = MeshBooleanOperation::Union;
    BooleanTargetMode   m_target_mode { BooleanTargetMode::Part };
    bool m_keep_original_models = false;
    bool m_entity_only = true;
    size_t m_last_snapshot_time = 0;

    // ================= VISUAL ELEMENTS ==========================
    // Helper methods to apply / restore color overrides for lists of volumes
    void apply_a_b_list_color_overrides(MeshBooleanOperation mode = MeshBooleanOperation::Difference);
    void apply_working_list_color_overrides(MeshBooleanOperation mode = MeshBooleanOperation::Union);
    void apply_color_overrides_for_list(const std::vector<unsigned int>& list, const Selection& selection, const std::array<float, 4>& color_for_model_part, bool entity_only = false);
    void apply_color_overrides_to_lists(const std::vector<std::pair<const std::vector<unsigned int>&, std::array<float, 4>>>& lists_and_colors, bool entity_only);
    void restore_list_color_overrides();
    void render_selected_bounding_boxes(); // Render bounding boxes for selected parts
    std::array<float, 4> abgr_u32_to_rgba(unsigned int abgr) const;

    // Helper methods for refresh & init
    void refresh_canvas();

    // Helper methods for visability management
    void apply_object_visibility(const Selection& selection) const;
    void apply_part_visibility(const Selection& selection) const;


};
} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoMeshBoolean_hpp_