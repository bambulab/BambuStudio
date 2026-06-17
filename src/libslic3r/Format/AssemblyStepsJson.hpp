#ifndef slic3r_AssemblyStepsJson_hpp_
#define slic3r_AssemblyStepsJson_hpp_

#include <string>
#include <vector>
#include <map>
#include <array>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <cstddef>

#include <nlohmann/json.hpp>
#include "libslic3r/Geometry.hpp"

namespace Slic3r {
// ---- ArrowSvgNote: one arrow + icon group ----
struct ArrowSvgNote
{
    std::string svg_name{};
    // The ModelVolumes this arrow is bound to, recorded as (object_idx, volume_idx)
    std::vector<std::pair<int, int>> bound_volumes{};
    Vec2d       arrow_start_offset{Vec2d::Zero()};   // offset from bound-volumes (or step) bbox screen center
    Vec2d       arrow_end_offset{Vec2d(80, -60)};    // offset from arrow start position
    Vec2d       label_size{Vec2d(56, 56)};
    std::array<int, 4> color{0, 200, 80, 230};

    void to_json(nlohmann::json &j) const;
    void from_json(const nlohmann::json &j);
};

struct TextLabelNote
{
    std::string text{"Note"};
    // The ModelVolumes this note is bound to, recorded as (object_idx, volume_idx).
    std::vector<std::pair<int, int>> bound_volumes{};
    Vec2d       pos_offset{Vec2d(60, -60)};
    Vec2d       size{Vec2d(160, 80)};
    std::array<int, 4> color{38, 46, 48, 255};
    // Alpha 217 (~0.85) keeps the historic semi-transparent white look used
    std::array<int, 4> background_color{255, 255, 255, 255};

    void to_json(nlohmann::json &j) const;
    void from_json(const nlohmann::json &j);
};

struct CircleNote
{
    // Bound ModelVolumes (object_idx, volume_idx); pos_offset is measured from
    std::vector<std::pair<int, int>> bound_volumes{};
    Vec2d pos_offset{Vec2d(60, -60)};
    Vec2d size{Vec2d(80, 80)};
    std::array<int, 4> color{0, 200, 80, 230};

    void to_json(nlohmann::json &j) const;
    void from_json(const nlohmann::json &j);
};

struct RectangleNote
{
    // Bound ModelVolumes (object_idx, volume_idx); pos_offset is measured from
    std::vector<std::pair<int, int>> bound_volumes{};
    Vec2d pos_offset{Vec2d(60, -60)};
    Vec2d size{Vec2d(80, 80)};
    std::array<int, 4> color{0, 200, 80, 230};

    void to_json(nlohmann::json &j) const;
    void from_json(const nlohmann::json &j);
};

struct PlainArrowNote
{
    // Bound ModelVolumes (object_idx, volume_idx); arrow_start_offset is measured
    std::vector<std::pair<int, int>> bound_volumes{};
    Vec2d arrow_start_offset{Vec2d::Zero()};
    Vec2d arrow_end_offset{Vec2d(80, -60)};
    std::array<int, 4> color{0, 200, 80, 230};

    void to_json(nlohmann::json &j) const;
    void from_json(const nlohmann::json &j);
};

struct PartNumberLabel
{
    int         object_idx{-1};
    int         volume_idx{-1};
    std::string part_name;
    Vec2d       arrow_start_offset{Vec2d::Zero()};
    Vec2d       arrow_end_offset{Vec2d(60, -50)};

    void to_json(nlohmann::json &j) const;
    void from_json(const nlohmann::json &j);
};

// ---- AssemblyNote: per-keyframe annotation data ----
struct AssemblyNote
{
    std::vector<ArrowSvgNote> arrow_svgs;
    std::vector<TextLabelNote> text_labels;
    std::vector<CircleNote> circle_notes;
    std::vector<RectangleNote> rectangle_notes;
    std::vector<PlainArrowNote> plain_arrows;
    std::vector<PartNumberLabel> part_number_labels;
    bool show_part_labels{true};//guide_show_part_numbers

    void to_json(nlohmann::json &j) const;
    void from_json(const nlohmann::json &j);
};
enum class LabelsShowType { AutoRecommend, OnlyModelObject, OnlyModelVolume };
// ---- KeyFrame ----
struct KeyFrame
{
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW
    int                                                      id{0};
    std::string                                              name;
    bool                                                     is_sub_assembly{false};
    bool                                                     is_camera_define{false};
    Transform3d                                              view_matrix{Transform3d::Identity()};
    Transform3d                                              projection_matrix{Transform3d::Identity()};
    Vec3d                                                    camera_target{Vec3d::Zero()};
    double                                                   camera_zoom{1.0};
    // Camera "zoom-to-box" margin factor used to frame this keyframe. Stored per.
    double                                                   camera_margin_factor{1.4};
    // Per-keyframe assemble-transformation snapshots, split to mirror the
    std::map<int, Geometry::Transformation>                  object_transformations;
    std::map<std::pair<int, int>, Geometry::Transformation>  volume_transformations;
    std::map<std::pair<int, int>, std::string>               volume_names;
    AssemblyNote                                             assembly_note;
    LabelsShowType                                           labels_show_type{LabelsShowType::AutoRecommend};
    bool                                                     is_interpolation{false}; // no need to save
    int                                                      play_node_idx{-1};      // no need to save
    int                                                      play_frame_idx{-1};     // no need to save
    bool is_last() const{
        return id == 0;
    }
    bool is_start() const {
        return id == 1;
    }
    bool is_transition() const {
        return id > 1;
    }
    void clone_from(const KeyFrame &src) {
        id = src.id;
        name = src.name;
        is_sub_assembly = src.is_sub_assembly;
        view_matrix = src.view_matrix;
        projection_matrix = src.projection_matrix;
        camera_target = src.camera_target;
        camera_zoom = src.camera_zoom;
        camera_margin_factor = src.camera_margin_factor;
        labels_show_type = src.labels_show_type;
    }
    void to_json(nlohmann::json &j) const;
    void from_json(const nlohmann::json &j);
};

using KeyFrameVector = std::vector<KeyFrame>;
struct AssembleBaseInfo
{
    std::string           name;
    KeyFrameVector        keyframes;

    virtual ~AssembleBaseInfo() = default;

    virtual std::string get_type() const = 0;
    virtual void        to_json(nlohmann::json &j) const;
    virtual void        from_json(const nlohmann::json &j);

    static std::shared_ptr<AssembleBaseInfo> create_from_json(const nlohmann::json &j);
};

struct AssembleSingleInfo : public AssembleBaseInfo
{
    int object_idx{-1};
    size_t object_id{0};

    std::string get_type() const override { return "single"; }
    void        to_json(nlohmann::json &j) const override;
    void        from_json(const nlohmann::json &j) override;
};

struct AssembleSub : public AssembleBaseInfo
{
    int id{-1};
    int step{0};
    bool is_final_assembly{false};
    std::vector<std::shared_ptr<AssembleBaseInfo>> children;
    std::optional<std::unordered_map<std::string, bool>> assembly_tree_checked;

    std::string get_type() const override { return "sub"; }
    void        to_json(nlohmann::json &j) const override;
    void        from_json(const nlohmann::json &j) override;
};


// ---- KeyFrameEntry / KFNodeData ----
struct KeyFrameEntry
{
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW
    bool     need_save{true};
    KeyFrame data;
    bool     is_last() const { return data.is_last(); }
    bool     is_start() const { return data.is_start(); }
    bool     is_transition() const { return data.is_transition(); }
    void     clone_from(const KeyFrameEntry &src)
    {
        data      = src.data;
        need_save = true;
    }
};

using KeyFrameEntryVector = std::vector<KeyFrameEntry>;
struct KFNodeData
{
    int                        node_idx{-1};
    int                        object_idx{-1};
    bool                       is_folder{false};
    KeyFrameEntryVector        entries;
};

struct PlayFrameRef
{
    int node_idx{-1};
    int frame_idx{-1};
};

enum class AssemblyNoteSelectionType { None, ArrowSvg, TextLabel, Circle, PlainArrow, Rectangle };
enum class KeyframeDisplayMode { OnlyCurrentStep, Highlight, All };
enum class PlayStrategy { Sequential, SubAssemblyFirst };
enum class AssemblyNoteCursorType { Standard, Hand, Move, ResizeNWSE, ResizeNESW };

struct AssemblyStepsTreeNode
{
    enum class Type { Folder, Object,Volume};
    Type             type{Type::Folder};
    int              id{-1};  // valid when type == Folder
    int              step{0}; // valid when type == Folder
    std::string      name;
    int              object_idx{-1}; // valid when type == Object
    size_t           object_id{0};   // ModelObject id, stable across object index changes
    bool             visible{true};// per-object render visibility (drives GLVolume show/hide)
    bool             is_final_assembly{false};
    std::vector<int> children; // indices into nodes
    // Optional left-side assembly tree checkbox state for this step folder.
    std::optional<std::unordered_map<std::string, bool>> assembly_tree_checked;
    // Per-node keyframe state. Lives on the node itself so the typed in-memory tree
    KFNodeData       kf_data;
};

class Model;// Forward declaration so the converter below stays decoupled from the heavy Model header.
struct AssemblyStepsTreeData
{
    std::vector<AssemblyStepsTreeNode> nodes;
    std::vector<int>                   roots;

    bool empty() const { return nodes.empty(); }
    void clear()
    {
        nodes.clear();
        roots.clear();
    }
    std::string to_json_string() const;
    static bool from_json_string(const std::string&     json_str,
                                 AssemblyStepsTreeData& tree,
                                 const Model&           model,
                                 std::string*           error = nullptr,
                                 float*                 assembly_part_number_label_font_size = nullptr);
};


// ---- AssemblyStepJson: file I/O wrapper ----
class AssemblyStepJson
{
public:
    struct PdfExportParams {
        std::string title;
    };

    static std::string get_debug_file_path();

    bool load(const std::string &path);
    bool load_from_string(const std::string &json_str);
    bool save(const std::string &path) const;
    std::string to_json_string() const;

    const std::vector<std::shared_ptr<AssembleBaseInfo>> &get_items() const { return m_items; }
    void set_items(const std::vector<std::shared_ptr<AssembleBaseInfo>> &items) { m_items = items; }
    void set_items(std::vector<std::shared_ptr<AssembleBaseInfo>> &&items) { m_items = std::move(items); }
    const PdfExportParams& get_pdf_export_params() const { return m_pdf_export_params; }
    void set_pdf_export_params(const PdfExportParams &params) { m_pdf_export_params = params; }
    float get_assembly_part_number_label_font_size() const { return m_assembly_part_number_label_font_size; }
    void set_assembly_part_number_label_font_size(float font_size) { m_assembly_part_number_label_font_size = font_size; }

private:
    void load_pdf_export_params(const nlohmann::json &root);
    void load_assembly_part_number_label_font_size(const nlohmann::json &root);

    std::vector<std::shared_ptr<AssembleBaseInfo>> m_items;
    PdfExportParams m_pdf_export_params;
    float m_assembly_part_number_label_font_size{0.0f};
};
} // namespace Slic3r
#endif // slic3r_OverviewlyJson_hpp_
