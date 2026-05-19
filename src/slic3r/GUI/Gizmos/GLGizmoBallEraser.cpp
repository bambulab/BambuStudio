#include "GLGizmoBallEraser.hpp"

#include "BallEraserBackend.hpp"
#include "ag/ballerase/BallEraserStepLogger.hpp"
#include "ag/customcut/ColorCutAppearanceSnapshot.hpp"
#include "ag/customcut/ColorCutAttributeRepository.hpp"
#include "ag/customcut/ColorCutAttributeTransfer.hpp"
#include "libslic3r/Color.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/ObjColorUtils.hpp"
#include "libslic3r/TriangleSelector.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "slic3r/GUI/MeshUtils.hpp"
#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "libslic3r/SLA/IndexedMesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <imgui/imgui.h>
#include <boost/dll/runtime_symbol_info.hpp>

namespace {

struct BallEraserValidationResult
{
    std::string message;
    bool can_try_split_fallback{false};
};

static std::string ballerase_log_file_path()
{
    return (boost::dll::program_location().parent_path() / "ballerase.log").string();
}

static std::mutex s_ballerase_log_mutex;

static void ballerase_log(const std::string &message)
{
    std::lock_guard<std::mutex> lock(s_ballerase_log_mutex);
    std::ofstream out(ballerase_log_file_path(), std::ios::app);
    if (!out) return;
    out << message << std::endl;
}

static void ballerase_log_phase(const std::string &phase_name)
{
    ballerase_log("");
    ballerase_log("========================================");
    ballerase_log(phase_name);
    ballerase_log("========================================");
}

static std::string be_format_vec3(const Slic3r::Vec3d &v)
{
    std::ostringstream s;
    s << std::fixed << std::setprecision(4) << "(" << v.x() << ", " << v.y() << ", " << v.z() << ")";
    return s.str();
}

static std::string be_format_bbox(const Slic3r::BoundingBoxf3 &bb)
{
    if (!bb.defined) return "<undefined>";
    std::ostringstream s;
    s << std::fixed << std::setprecision(4)
      << "min(" << bb.min.x() << ", " << bb.min.y() << ", " << bb.min.z() << ")"
      << " max(" << bb.max.x() << ", " << bb.max.y() << ", " << bb.max.z() << ")";
    return s.str();
}

static std::string be_format_transform(const Slic3r::Transform3d &t)
{
    std::ostringstream s;
    s << std::fixed << std::setprecision(6);
    for (int r = 0; r < 4; ++r) {
        s << "\n    [";
        for (int c = 0; c < 4; ++c) {
            if (c > 0) s << ", ";
            s << t(r, c);
        }
        s << "]";
    }
    return s.str();
}

// Forward declarations for helpers defined later in the anonymous namespace
Slic3r::Vec3d triangle_centroid(const indexed_triangle_set &mesh, size_t triangle_index);
Slic3r::Vec3d triangle_normal(const indexed_triangle_set &mesh, size_t triangle_index);
Slic3r::TriangleMesh build_ball_eraser_source_mesh(const Slic3r::ModelObject &object, const Slic3r::ModelVolume &volume, int instance_index);
Slic3r::TriangleMesh build_ball_eraser_target_mesh(const Slic3r::ModelVolume &volume);

static void log_source_object(const Slic3r::ModelObject &object, int instance_index)
{
    ballerase_log_phase("SOURCE OBJECT STATE");
    std::ostringstream s;
    s << "name: '" << object.name << "'"
      << "\nvolumes: " << object.volumes.size()
      << "\ninstances: " << object.instances.size()
      << "\norigin_translation: " << be_format_vec3(object.origin_translation)
      << "\nraw_bbox: " << be_format_bbox(object.raw_mesh_bounding_box());

    for (size_t i = 0; i < object.volumes.size(); ++i) {
        const Slic3r::ModelVolume *v = object.volumes[i];
        if (v == nullptr) { s << "\n  volume[" << i << "] = nullptr"; continue; }
        s << "\n  volume[" << i << "] '" << v->name << "'"
          << " id=" << v->id().id
          << " type=" << int(v->type())
          << " is_model_part=" << v->is_model_part()
          << " is_cut_connector=" << v->is_cut_connector()
          << " triangles=" << v->mesh().its.indices.size()
          << " vertices=" << v->mesh().its.vertices.size()
          << " mesh_bbox=" << be_format_bbox(v->mesh().bounding_box())
          << " volume_offset=" << be_format_vec3(v->get_offset())
          << " volume_transform:" << be_format_transform(v->get_matrix());
    }

    if (instance_index >= 0 && instance_index < static_cast<int>(object.instances.size())) {
        const Slic3r::ModelInstance *inst = object.instances[static_cast<size_t>(instance_index)];
        s << "\n\ninstance[" << instance_index << "]:"
          << "\n  offset=" << be_format_vec3(inst->get_offset())
          << "\n  rotation=" << be_format_vec3(inst->get_rotation())
          << "\n  scaling_factor=" << be_format_vec3(inst->get_scaling_factor())
          << "\n  mirror=" << be_format_vec3(inst->get_mirror())
          << "\n  matrix_no_offset:" << be_format_transform(inst->get_transformation().get_matrix_no_offset())
          << "\n  full_matrix:" << be_format_transform(inst->get_transformation().get_matrix());
    }

    ballerase_log(s.str());
}

static void log_appearance_snapshot(const Slic3r::ColorCut::ObjectAppearanceSnapshot &snapshot, const Slic3r::ModelVolume &source_volume)
{
    ballerase_log_phase("APPEARANCE SNAPSHOT");

    auto &repository = Slic3r::ColorCut::global_color_cut_attribute_repository();
    auto external_color_data = repository.get_volume_color_data(source_volume.id().id);

    std::ostringstream s;
    s << "object_id: " << snapshot.object_id
      << "\nvolume_snapshots: " << snapshot.volumes.size()
      << "\nexternal_color_data_exists: " << (external_color_data.has_value() ? "YES" : "NO");

    if (external_color_data.has_value()) {
        s << "\n  external_color_data.pid=" << external_color_data->pid
          << "\n  external_color_data.pindex=" << external_color_data->pindex
          << "\n  external_color_data.triangle_colors.size=" << external_color_data->triangle_colors.size();
    }

    for (size_t vi = 0; vi < snapshot.volumes.size(); ++vi) {
        const auto &vol_snap = snapshot.volumes[vi];
        s << "\n\nvolume_snapshot[" << vi << "]:"
          << "\n  volume_id=" << vol_snap.volume_id
          << "\n  material_id='" << vol_snap.material_id << "'"
          << "\n  default_extruder=" << vol_snap.default_extruder
          << "\n  triangle_records=" << vol_snap.triangle_records.size();

        // Count MMU tags
        std::map<std::string, size_t> mmu_counts;
        size_t color_bound_count = 0;
        for (const auto &rec : vol_snap.triangle_records) {
            if (!rec.payload.empty()) ++mmu_counts[rec.payload];
            if (rec.triangle_color_pid >= 0) ++color_bound_count;
        }
        s << "\n  distinct_mmu_tags=" << mmu_counts.size();
        for (const auto &[tag, count] : mmu_counts)
            s << "\n    tag='" << tag << "' count=" << count;
        s << "\n  triangles_with_color_binding=" << color_bound_count;

        // Sample first 5 records
        size_t sample_count = std::min<size_t>(vol_snap.triangle_records.size(), 5);
        for (size_t ri = 0; ri < sample_count; ++ri) {
            const auto &rec = vol_snap.triangle_records[ri];
            s << "\n  record[" << ri << "] src_tri=" << rec.source_triangle_index
              << " kind=" << int(rec.kind)
              << " mmu='" << rec.payload << "'"
              << " color_pid=" << rec.triangle_color_pid
              << " color_idx=[" << rec.color_indices[0] << "," << rec.color_indices[1] << "," << rec.color_indices[2] << "]";
        }
    }

    ballerase_log(s.str());
}

static void log_result_objects(const Slic3r::ModelObjectPtrs &objects, const char *label)
{
    ballerase_log_phase(std::string("RESULT OBJECTS: ") + label);

    auto &repository = Slic3r::ColorCut::global_color_cut_attribute_repository();

    std::ostringstream s;
    s << "objects: " << objects.size();
    for (size_t oi = 0; oi < objects.size(); ++oi) {
        const Slic3r::ModelObject *obj = objects[oi];
        if (obj == nullptr) { s << "\n  object[" << oi << "] = nullptr"; continue; }
        s << "\n\nobject[" << oi << "] '" << obj->name << "'"
          << "\n  volumes: " << obj->volumes.size()
          << "\n  instances: " << obj->instances.size()
          << "\n  origin_translation: " << be_format_vec3(obj->origin_translation);
        if (!obj->instances.empty()) {
            const auto *inst = obj->instances.front();
            s << "\n  instance[0] offset=" << be_format_vec3(inst->get_offset())
              << " rotation=" << be_format_vec3(inst->get_rotation())
              << " scaling=" << be_format_vec3(inst->get_scaling_factor())
              << "\n  instance[0] matrix_no_offset:" << be_format_transform(inst->get_transformation().get_matrix_no_offset());
        }
        for (size_t vi = 0; vi < obj->volumes.size(); ++vi) {
            const Slic3r::ModelVolume *v = obj->volumes[vi];
            if (v == nullptr) { s << "\n  volume[" << vi << "] = nullptr"; continue; }
            s << "\n  volume[" << vi << "] '" << v->name << "'"
              << " id=" << v->id().id
              << " type=" << int(v->type())
              << " is_model_part=" << v->is_model_part()
              << " triangles=" << v->mesh().its.indices.size()
              << " mesh_bbox=" << be_format_bbox(v->mesh().bounding_box())
              << " volume_offset=" << be_format_vec3(v->get_offset())
              << " volume_transform:" << be_format_transform(v->get_matrix());

            // Check if color data exists in repository for this new volume
            auto vol_color = repository.get_volume_color_data(v->id().id);
            s << "\n    repo_color_data=" << (vol_color.has_value() ? "YES" : "NO");

            // Count MMU tags on the volume
            size_t mmu_tagged = 0;
            for (size_t ti = 0; ti < v->mesh().its.indices.size(); ++ti) {
                if (!v->mmu_segmentation_facets.get_triangle_as_string(static_cast<int>(ti)).empty())
                    ++mmu_tagged;
            }
            s << " mmu_tagged_triangles=" << mmu_tagged;
        }
    }

    ballerase_log(s.str());
}

static void log_coordinate_space_comparison(
    const Slic3r::ModelObject &source_object,
    const Slic3r::ModelVolume &source_volume,
    int instance_index,
    const Slic3r::ModelObjectPtrs &target_objects)
{
    ballerase_log_phase("COORDINATE SPACE COMPARISON");

    // Compute source mesh in the same space that reapply_from_single_source will use
    Slic3r::TriangleMesh source_mesh(source_volume.mesh());
    if (instance_index >= 0 && instance_index < static_cast<int>(source_object.instances.size())) {
        const Slic3r::ModelInstance *inst = source_object.instances[static_cast<size_t>(instance_index)];
        Slic3r::Transform3d source_transform = inst->get_transformation().get_matrix_no_offset() * source_volume.get_matrix();
        source_mesh.transform(source_transform, true);
        ballerase_log("source_mesh transform = instance_matrix_no_offset * volume_matrix");
        ballerase_log("  instance_matrix_no_offset:" + be_format_transform(inst->get_transformation().get_matrix_no_offset()));
        ballerase_log("  volume_matrix:" + be_format_transform(source_volume.get_matrix()));
    } else {
        source_mesh.transform(source_volume.get_matrix(), true);
        ballerase_log("source_mesh transform = volume_matrix only");
    }
    ballerase_log("source_mesh_transformed bbox: " + be_format_bbox(source_mesh.bounding_box()));
    ballerase_log("source_mesh_transformed triangles: " + std::to_string(source_mesh.its.indices.size()));

    // Sample some source mesh triangle centroids
    {
        std::ostringstream s;
        size_t sample_count = std::min<size_t>(source_mesh.its.indices.size(), 10);
        s << "source_mesh sample centroids:";
        for (size_t i = 0; i < sample_count; ++i)
            s << "\n  src_tri[" << i << "] centroid=" << be_format_vec3(triangle_centroid(source_mesh.its, i))
              << " normal=" << be_format_vec3(triangle_normal(source_mesh.its, i));
        ballerase_log(s.str());
    }

    // For each target volume, compute the target mesh in the same way reapply_after_mesh_repair will
    for (size_t oi = 0; oi < target_objects.size(); ++oi) {
        const Slic3r::ModelObject *target_obj = target_objects[oi];
        if (target_obj == nullptr) continue;
        for (size_t vi = 0; vi < target_obj->volumes.size(); ++vi) {
            const Slic3r::ModelVolume *target_vol = target_obj->volumes[vi];
            if (target_vol == nullptr || !target_vol->is_model_part() || target_vol->is_cut_connector()) continue;

            Slic3r::TriangleMesh target_mesh(target_vol->mesh());
            Slic3r::Transform3d target_transform = target_vol->get_matrix();
            if (target_vol->get_object() != nullptr && !target_vol->get_object()->instances.empty())
                target_transform = target_vol->get_object()->instances.front()->get_transformation().get_matrix_no_offset() * target_transform;
            target_mesh.transform(target_transform, true);

            std::ostringstream s;
            s << "target obj[" << oi << "] vol[" << vi << "] '" << target_vol->name << "'"
              << "\n  target_transform:" << be_format_transform(target_transform)
              << "\n  target_mesh_transformed bbox: " << be_format_bbox(target_mesh.bounding_box())
              << "\n  target_mesh_transformed triangles: " << target_mesh.its.indices.size();

            size_t sample_count = std::min<size_t>(target_mesh.its.indices.size(), 10);
            s << "\n  target_mesh sample centroids:";
            for (size_t i = 0; i < sample_count; ++i)
                s << "\n    tgt_tri[" << i << "] centroid=" << be_format_vec3(triangle_centroid(target_mesh.its, i))
                  << " normal=" << be_format_vec3(triangle_normal(target_mesh.its, i));

            // Check overlap: does target mesh bbox overlap with source mesh bbox?
            const auto source_bb = source_mesh.bounding_box();
            const auto target_bb = target_mesh.bounding_box();
            const bool overlap = source_bb.defined && target_bb.defined
                && source_bb.min.x() <= target_bb.max.x() && source_bb.max.x() >= target_bb.min.x()
                && source_bb.min.y() <= target_bb.max.y() && source_bb.max.y() >= target_bb.min.y()
                && source_bb.min.z() <= target_bb.max.z() && source_bb.max.z() >= target_bb.min.z();
            s << "\n  bbox_overlap_with_source: " << (overlap ? "YES" : "NO");
            s << "\n  source_bbox_size: " << be_format_vec3(source_bb.size().cast<double>())
              << "\n  target_bbox_size: " << be_format_vec3(target_bb.size().cast<double>());
            const Slic3r::Vec3d center_delta = target_bb.center().cast<double>() - source_bb.center().cast<double>();
            s << "\n  center_delta: " << be_format_vec3(center_delta) << " norm=" << std::fixed << std::setprecision(4) << center_delta.norm();

            ballerase_log(s.str());
        }
    }
}

constexpr float kCursorHoverSlack = 0.75f;
constexpr double kMinimumDragSampleSpacing = 0.75;
constexpr double kSphereDragSampleSpacingRatio = 0.35;
constexpr double kCubeDragSampleSpacingRatio = 0.85;
constexpr double kMaximumSphereDragSampleSpacing = 5.0;
constexpr double kResidualSourceDistanceTolerance = 0.45;
constexpr double kResidualSourceNormalAlignment = 0.70;
constexpr double kResidualCutterDistanceTolerance = 0.20;
constexpr double kResidualCutterNormalAlignment = 0.90;

static double ball_eraser_drag_sample_spacing(
    Slic3r::BallEraser::PrimitiveType primitive,
    const Slic3r::BallEraser::Dimensions &dimensions)
{
    if (primitive == Slic3r::BallEraser::PrimitiveType::Sphere) {
        const double spacing = dimensions.x * kSphereDragSampleSpacingRatio;
        return std::clamp(spacing, kMinimumDragSampleSpacing, kMaximumSphereDragSampleSpacing);
    }

    const double base_dimension = std::min({ dimensions.x, dimensions.y, dimensions.z });
    return std::max(kMinimumDragSampleSpacing, base_dimension * kCubeDragSampleSpacingRatio);
}

static void cleanup_ball_eraser_mesh(Slic3r::TriangleMesh &mesh)
{
    if (mesh.empty())
        return;

    Slic3r::its_remove_degenerate_faces(mesh.its);
    Slic3r::its_merge_vertices(mesh.its);
    Slic3r::its_compactify_vertices(mesh.its);
}

static double ball_eraser_stroke_sample_spacing(const Slic3r::BallEraser::Stroke &stroke)
{
    return ball_eraser_drag_sample_spacing(stroke.primitive, stroke.dimensions);
}

static Slic3r::Vec3d ball_eraser_rotation_radians(const Slic3r::Vec3d &rotation_degrees)
{
    return Slic3r::Vec3d(
        Slic3r::Geometry::deg2rad(rotation_degrees.x()),
        Slic3r::Geometry::deg2rad(rotation_degrees.y()),
        Slic3r::Geometry::deg2rad(rotation_degrees.z()));
}

static Slic3r::Transform3d ball_eraser_primitive_transform(
    Slic3r::BallEraser::PrimitiveType primitive,
    const Slic3r::BallEraser::Dimensions &dimensions,
    const Slic3r::Vec3d &center,
    const Slic3r::Vec3d &rotation_degrees)
{
    const Slic3r::Vec3d scale = primitive == Slic3r::BallEraser::PrimitiveType::Sphere
        ? Slic3r::Vec3d::Constant(dimensions.x)
        : Slic3r::Vec3d(dimensions.x, dimensions.y, dimensions.z);
    const Slic3r::Transform3d local_centering = primitive == Slic3r::BallEraser::PrimitiveType::Sphere
        ? Slic3r::Transform3d::Identity()
        : Slic3r::Geometry::translation_transform(Slic3r::Vec3d(-0.5, -0.5, -0.5));
    const Slic3r::Transform3d local_rotation = primitive == Slic3r::BallEraser::PrimitiveType::Sphere
        ? Slic3r::Transform3d::Identity()
        : Slic3r::Geometry::rotation_transform(ball_eraser_rotation_radians(rotation_degrees));
    return Slic3r::Geometry::translation_transform(center)
        * local_rotation
        * Slic3r::Geometry::scale_transform(scale)
        * local_centering;
}

static std::vector<Slic3r::Vec3d> simplify_ball_eraser_stroke_placements(const Slic3r::BallEraser::Stroke &stroke)
{
    if (stroke.placement_centers_instance_local.empty())
        return {};

    std::vector<Slic3r::Vec3d> simplified;
    simplified.reserve(stroke.placement_centers_instance_local.size());
    simplified.push_back(stroke.placement_centers_instance_local.front());

    const double min_spacing = ball_eraser_stroke_sample_spacing(stroke);
    for (size_t index = 1; index < stroke.placement_centers_instance_local.size(); ++index) {
        const bool is_last = index + 1 == stroke.placement_centers_instance_local.size();
        const Slic3r::Vec3d &sample = stroke.placement_centers_instance_local[index];
        if (!is_last && (sample - simplified.back()).norm() < min_spacing)
            continue;
        simplified.push_back(sample);
    }

    return simplified;
}

static Slic3r::TriangleMesh build_ball_eraser_stroke_sample_mesh(
    Slic3r::BallEraser::PrimitiveType primitive,
    const Slic3r::BallEraser::Dimensions &dimensions,
    const Slic3r::Vec3d &center_instance_local,
    const Slic3r::Vec3d &rotation_degrees)
{
    Slic3r::TriangleMesh primitive_mesh = primitive == Slic3r::BallEraser::PrimitiveType::Sphere
        ? Slic3r::TriangleMesh(Slic3r::its_make_sphere(0.5, PI / 18.0))
        : Slic3r::TriangleMesh(Slic3r::its_make_cube(1.0, 1.0, 1.0));

    const Slic3r::Transform3d transform = ball_eraser_primitive_transform(
        primitive,
        dimensions,
        center_instance_local,
        rotation_degrees);

    primitive_mesh.transform(transform, true);
    cleanup_ball_eraser_mesh(primitive_mesh);
    return primitive_mesh;
}

static Slic3r::TriangleMesh build_ball_eraser_segment_mesh(
    Slic3r::BallEraser::PrimitiveType primitive,
    const Slic3r::BallEraser::Dimensions &dimensions,
    const Slic3r::Vec3d &rotation_degrees,
    const Slic3r::Vec3d &start_center,
    const Slic3r::Vec3d &end_center)
{
    Slic3r::TriangleMesh start_mesh = build_ball_eraser_stroke_sample_mesh(primitive, dimensions, start_center, rotation_degrees);
    Slic3r::TriangleMesh end_mesh = build_ball_eraser_stroke_sample_mesh(primitive, dimensions, end_center, rotation_degrees);

    Slic3r::TriangleMesh merged = start_mesh;
    merged.merge(end_mesh);
    cleanup_ball_eraser_mesh(merged);

    Slic3r::TriangleMesh segment = merged.convex_hull_3d();
    cleanup_ball_eraser_mesh(segment);
    if (segment.empty())
        return merged;
    return segment;
}

static Slic3r::TriangleMesh build_ball_eraser_cutter_mesh(const std::vector<Slic3r::BallEraser::Stroke> &strokes)
{
    Slic3r::TriangleMesh merged_cutter;
    for (const Slic3r::BallEraser::Stroke &stroke : strokes) {
        const std::vector<Slic3r::Vec3d> placements = simplify_ball_eraser_stroke_placements(stroke);
        if (placements.empty())
            continue;

        if (placements.size() == 1) {
            merged_cutter.merge(build_ball_eraser_stroke_sample_mesh(stroke.primitive, stroke.dimensions, placements.front(), stroke.rotation_degrees));
            continue;
        }

        for (size_t index = 1; index < placements.size(); ++index) {
            Slic3r::TriangleMesh segment = build_ball_eraser_segment_mesh(
                stroke.primitive,
                stroke.dimensions,
                stroke.rotation_degrees,
                placements[index - 1],
                placements[index]);
            if (!segment.empty())
                merged_cutter.merge(segment);
        }
    }

    cleanup_ball_eraser_mesh(merged_cutter);
    return merged_cutter;
}

static std::array<Slic3r::Vec3d, 4> ball_eraser_triangle_sample_points(
    const indexed_triangle_set &mesh,
    size_t triangle_index)
{
    const Slic3r::Vec3i &triangle = mesh.indices[triangle_index];
    const Slic3r::Vec3d a = mesh.vertices[size_t(triangle[0])].cast<double>();
    const Slic3r::Vec3d b = mesh.vertices[size_t(triangle[1])].cast<double>();
    const Slic3r::Vec3d c = mesh.vertices[size_t(triangle[2])].cast<double>();
    return {{
        (a + b + c) / 3.0,
        (a + b) * 0.5,
        (b + c) * 0.5,
        (c + a) * 0.5
    }};
}

static bool ball_eraser_sample_matches_surface(
    const Slic3r::sla::IndexedMesh &indexed_mesh,
    const Slic3r::Vec3d &sample_point,
    const Slic3r::Vec3d &target_normal,
    double distance_tolerance,
    double normal_alignment_tolerance)
{
    int nearest_face_index = -1;
    Slic3r::Vec3d nearest_point = Slic3r::Vec3d::Zero();
    const double squared_distance = indexed_mesh.squared_distance(sample_point, nearest_face_index, nearest_point);
    if (nearest_face_index < 0)
        return false;

    const double distance = std::sqrt(std::max(0.0, squared_distance));
    if (distance > distance_tolerance)
        return false;

    const Slic3r::Vec3d source_normal = indexed_mesh.normal_by_face_id(nearest_face_index).normalized();
    const double normal_alignment = std::abs(source_normal.dot(target_normal));
    return normal_alignment >= normal_alignment_tolerance;
}

static bool ball_eraser_sample_is_close_to_surface(
    const Slic3r::sla::IndexedMesh &indexed_mesh,
    const Slic3r::Vec3d &sample_point,
    double distance_tolerance)
{
    int nearest_face_index = -1;
    Slic3r::Vec3d nearest_point = Slic3r::Vec3d::Zero();
    const double squared_distance = indexed_mesh.squared_distance(sample_point, nearest_face_index, nearest_point);
    if (nearest_face_index < 0)
        return false;

    const double distance = std::sqrt(std::max(0.0, squared_distance));
    return distance <= distance_tolerance;
}

struct BallEraserSurfaceMatch
{
    bool matched{false};
    int face_index{-1};
};

static BallEraserSurfaceMatch ball_eraser_match_source_face(
    const Slic3r::sla::IndexedMesh &indexed_mesh,
    const Slic3r::Vec3d &sample_point,
    const Slic3r::Vec3d &target_normal,
    double distance_tolerance,
    double normal_alignment_tolerance)
{
    int nearest_face_index = -1;
    Slic3r::Vec3d nearest_point = Slic3r::Vec3d::Zero();
    const double squared_distance = indexed_mesh.squared_distance(sample_point, nearest_face_index, nearest_point);
    if (nearest_face_index < 0)
        return {};

    const double distance = std::sqrt(std::max(0.0, squared_distance));
    if (distance > distance_tolerance)
        return {};

    const Slic3r::Vec3d source_normal = indexed_mesh.normal_by_face_id(nearest_face_index).normalized();
    const double normal_alignment = std::abs(source_normal.dot(target_normal));
    if (normal_alignment < normal_alignment_tolerance)
        return {};

    BallEraserSurfaceMatch match;
    match.matched = true;
    match.face_index = nearest_face_index;
    return match;
}

// ---------------------------------------------------------------------------
// MMU segmentation string simplification
//
// After boolean subtraction, CGAL re-triangulates the entire mesh.  The KNN
// transfer copies the full MMU subdivision-tree string from the nearest source
// triangle, but that bitmap encodes sub-triangle paint detail at a resolution
// tied to the original triangle shape.  Applying it to a differently-shaped
// output triangle produces visual garbage.
//
// These helpers decode the subdivision tree, find the most-common leaf
// state (extruder / paint type) and re-encode it as a single-leaf string so
// every output triangle gets a clean, uniform assignment.
// ---------------------------------------------------------------------------

static int extract_dominant_mmu_state(const std::string &mmu_string)
{
    if (mmu_string.empty())
        return -1;

    // Decode hex characters to nibbles in tree-traversal order
    // (string is stored reversed relative to the bit stream).
    std::vector<int> nibbles;
    nibbles.reserve(mmu_string.size());
    for (auto it = mmu_string.crbegin(); it != mmu_string.crend(); ++it) {
        const char ch = *it;
        if (ch >= '0' && ch <= '9')      nibbles.push_back(ch - '0');
        else if (ch >= 'A' && ch <= 'F') nibbles.push_back(10 + ch - 'A');
    }

    std::map<int, int> state_counts;
    size_t pos = 0;

    std::function<void()> walk = [&]() {
        if (pos >= nibbles.size()) return;
        const int code = nibbles[pos++];
        const int split_sides = code & 0b11;
        if (split_sides == 0) {
            int state;
            if ((code & 0b1100) == 0b1100) {
                int num = 0;
                int ext = 0;
                while (pos < nibbles.size()) {
                    ext = nibbles[pos++];
                    if (ext != 0b1111) break;
                    ++num;
                }
                state = ext + 15 * num + 3;
            } else {
                state = code >> 2;
            }
            ++state_counts[state];
        } else {
            const int num_children = split_sides + 1;
            for (int i = 0; i < num_children; ++i)
                walk();
        }
    };

    walk();

    int dominant = -1;
    int max_count = 0;
    for (const auto &[state, count] : state_counts) {
        if (count > max_count) {
            max_count = count;
            dominant  = state;
        }
    }
    return dominant;
}

static std::string encode_mmu_leaf_state(int state)
{
    if (state < 0)  return "";
    if (state <= 2) {
        const int code = state << 2;
        return std::string(1, code < 10 ? static_cast<char>('0' + code)
                                        : static_cast<char>('A' + code - 10));
    }
    // Extended encoding: root nibble 0b1100 ('C'), then value nibble(s)
    std::vector<int> nibbles;
    nibbles.push_back(0b1100);
    int remaining = state - 3;
    while (remaining >= 15) {
        nibbles.push_back(0b1111);
        remaining -= 15;
    }
    nibbles.push_back(remaining);
    // Build string: last nibble in tree order becomes first character
    std::string str;
    for (auto it = nibbles.rbegin(); it != nibbles.rend(); ++it) {
        const int n = *it;
        str += n < 10 ? static_cast<char>('0' + n) : static_cast<char>('A' + n - 10);
    }
    return str;
}

static std::string simplify_mmu_to_dominant(const std::string &mmu_string)
{
    if (mmu_string.size() <= 1) return mmu_string;
    return encode_mmu_leaf_state(extract_dominant_mmu_state(mmu_string));
}

static bool is_ball_eraser_residual_surface_triangle(
    const Slic3r::sla::IndexedMesh &source_indexed_mesh,
    const Slic3r::sla::IndexedMesh *cutter_indexed_mesh,
    const Slic3r::TriangleMesh &transformed_target_mesh,
    size_t triangle_index)
{
    const Slic3r::Vec3d target_normal = triangle_normal(transformed_target_mesh.its, triangle_index);
    const auto sample_points = ball_eraser_triangle_sample_points(transformed_target_mesh.its, triangle_index);

    int cutter_votes = 0;
    int source_proximity_votes = 0;
    std::unordered_map<int, int> source_face_votes;
    for (const Slic3r::Vec3d &sample_point : sample_points) {
        if (ball_eraser_sample_is_close_to_surface(
                source_indexed_mesh,
                sample_point,
                kResidualSourceDistanceTolerance)) {
            ++source_proximity_votes;
        }

        const BallEraserSurfaceMatch source_match = ball_eraser_match_source_face(
            source_indexed_mesh,
            sample_point,
            target_normal,
            kResidualSourceDistanceTolerance,
            kResidualSourceNormalAlignment);
        if (source_match.matched) {
            ++source_face_votes[source_match.face_index];
            continue;
        }

        if (cutter_indexed_mesh != nullptr && ball_eraser_sample_matches_surface(
                *cutter_indexed_mesh,
                sample_point,
                target_normal,
                kResidualCutterDistanceTolerance,
                kResidualCutterNormalAlignment)) {
            ++cutter_votes;
        }
    }

    if (source_proximity_votes >= 2)
        return false;

    for (const auto &[face_index, face_votes] : source_face_votes) {
        (void) face_index;
        if (face_votes >= 2)
            return false;
    }

    if (!source_face_votes.empty())
        return false;

    return cutter_votes >= 3;
}

static void simplify_result_mmu_segmentation(
    const Slic3r::ModelObject &source_object,
    const Slic3r::ModelVolume &source_volume,
    int instance_index,
    const std::vector<Slic3r::BallEraser::Stroke> &strokes,
    const Slic3r::ModelObjectPtrs &objects)
{
    Slic3r::TriangleMesh source_mesh = build_ball_eraser_source_mesh(source_object, source_volume, instance_index);
    if (source_mesh.empty())
        return;

    const Slic3r::sla::IndexedMesh source_indexed_mesh(source_mesh);
    const Slic3r::TriangleMesh cutter_mesh = build_ball_eraser_cutter_mesh(strokes);
    std::unique_ptr<Slic3r::sla::IndexedMesh> cutter_indexed_mesh;
    if (!cutter_mesh.empty())
        cutter_indexed_mesh = std::make_unique<Slic3r::sla::IndexedMesh>(cutter_mesh);

    for (Slic3r::ModelObject *obj : objects) {
        if (obj == nullptr) continue;
        for (Slic3r::ModelVolume *vol : obj->volumes) {
            if (vol == nullptr || !vol->is_model_part()) continue;
            const Slic3r::TriangleMesh transformed_target_mesh = build_ball_eraser_target_mesh(*vol);
            const size_t tri_count = vol->mesh().its.indices.size();
            std::vector<std::pair<int, std::string>> entries;
            entries.reserve(tri_count);
            size_t simplified_count = 0;
            const bool use_exact_cut_mask = !vol->exterior_facets.empty();
            for (size_t ti = 0; ti < tri_count; ++ti) {
                std::string tag = vol->mmu_segmentation_facets.get_triangle_as_string(static_cast<int>(ti));
                if (tag.empty()) continue;
                if (tag.size() > 1
                    && ((use_exact_cut_mask && vol->exterior_facets.get_triangle_as_string(static_cast<int>(ti)) == "1")
                        || (!use_exact_cut_mask
                            && is_ball_eraser_residual_surface_triangle(
                                source_indexed_mesh,
                                cutter_indexed_mesh.get(),
                                transformed_target_mesh,
                                ti)))) {
                    tag = simplify_mmu_to_dominant(tag);
                    ++simplified_count;
                }
                if (!tag.empty())
                    entries.emplace_back(static_cast<int>(ti), std::move(tag));
            }
            vol->mmu_segmentation_facets.reset();
            vol->mmu_segmentation_facets.reserve(static_cast<int>(tri_count));
            for (const auto &[tri_id, tag] : entries)
                vol->mmu_segmentation_facets.set_triangle_from_string(tri_id, tag);
            vol->mmu_segmentation_facets.shrink_to_fit();
            ballerase_log("  simplified " + std::to_string(simplified_count) + " residual-surface MMU strings in volume '"
                + vol->name + "'");
        }
    }
}

void show_ball_eraser_error(const std::string &message)
{
    Slic3r::GUI::MessageDialog dlg(nullptr, wxString::FromUTF8(message.c_str()), _L("BallEraser"), wxOK | wxICON_WARNING);
    dlg.ShowModal();
}

bool confirm_ball_eraser_split_result(const std::string &validation_message)
{
    wxString message = wxString::FromUTF8(validation_message.c_str()) + "\n\n" +
        _L("The erase operation appears to separate the model into disconnected pieces. Do you want to continue by splitting the result into separate objects?");
    Slic3r::GUI::MessageDialog dlg(nullptr, message, _L("BallEraser"), wxYES_NO | wxNO_DEFAULT | wxICON_WARNING);
    return dlg.ShowModal() == wxID_YES;
}

void synchronize_model_after_cut(Slic3r::Model &model, const Slic3r::CutObjectBase &cut_id)
{
    for (Slic3r::ModelObject *object : model.objects)
        if (object->is_cut() && object->cut_id.has_same_id(cut_id) && !object->cut_id.is_equal(cut_id))
            object->cut_id.copy(cut_id);
}

BallEraserValidationResult validate_ball_eraser_result(const Slic3r::ModelObjectPtrs &objects)
{
    if (objects.empty())
        return { "BallEraser did not produce any output objects.", false };

    for (const Slic3r::ModelObject *object : objects) {
        if (object == nullptr)
            return { "BallEraser produced a null output object.", false };
        if (object->volumes.empty())
            return { "BallEraser produced an output object without any volumes.", false };

        const Slic3r::TriangleMeshStats stats = object->get_object_stl_stats();
        ballerase_log("validation stats for '" + object->name + "': parts=" + std::to_string(stats.number_of_parts)
            + " open_edges=" + std::to_string(stats.open_edges)
            + " facets=" + std::to_string(stats.number_of_facets));
        if (stats.number_of_parts > 1)
            return { "BallEraser produced disconnected geometry.", true };
        if (!stats.manifold())
            return { "BallEraser produced non-manifold geometry.", false };
    }

    return {};
}

const Slic3r::ModelVolume *find_single_ball_eraser_source_volume(const Slic3r::ModelObject &object)
{
    const Slic3r::ModelVolume *source_volume = nullptr;
    for (const Slic3r::ModelVolume *volume : object.volumes) {
        if (volume == nullptr || !volume->is_model_part() || volume->is_cut_connector())
            continue;

        if (source_volume != nullptr)
            return nullptr;

        source_volume = volume;
    }

    return source_volume;
}

Slic3r::TriangleColor make_triangle_color_binding(int pid, int pindex)
{
    Slic3r::TriangleColor binding;
    binding.pid = pid;
    binding.indices[0] = pindex;
    binding.indices[1] = pindex;
    binding.indices[2] = pindex;
    return binding;
}

Slic3r::TriangleColor register_triangle_color_binding(
    std::unordered_map<int, std::vector<std::string>> &color_group_map,
    const Slic3r::RGBA &rgba)
{
    const Slic3r::ColorRGBA rgba_color(rgba[0], rgba[1], rgba[2], rgba[3]);
    const std::string encoded_color = Slic3r::encode_color(rgba_color);

    for (const auto &entry : color_group_map) {
        for (size_t index = 0; index < entry.second.size(); ++index) {
            if (entry.second[index] == encoded_color)
                return make_triangle_color_binding(entry.first, static_cast<int>(index));
        }
    }

    int next_pid = 1;
    for (const auto &entry : color_group_map)
        next_pid = std::max(next_pid, entry.first + 1);

    color_group_map[next_pid] = { encoded_color };
    return make_triangle_color_binding(next_pid, 0);
}

Slic3r::Vec3d triangle_centroid(const indexed_triangle_set &mesh, size_t triangle_index)
{
    const Slic3r::Vec3i &triangle = mesh.indices[triangle_index];
    return (mesh.vertices[size_t(triangle[0])].cast<double>()
        + mesh.vertices[size_t(triangle[1])].cast<double>()
        + mesh.vertices[size_t(triangle[2])].cast<double>()) / 3.0;
}

Slic3r::Vec3d triangle_normal(const indexed_triangle_set &mesh, size_t triangle_index)
{
    const Slic3r::Vec3d normal = Slic3r::its_face_normal(mesh, int(triangle_index)).cast<double>();
    const double length = normal.norm();
    if (length <= 1e-12)
        return Slic3r::Vec3d::Zero();
    return normal / length;
}

void clear_origin_render_cache(Slic3r::ModelVolume &volume)
{
    if (volume.origin_render_info_ptr == nullptr)
        return;

    volume.origin_render_info_ptr->mesh_with_colors.clear();
    volume.origin_render_info_ptr->vertices_with_colors.first = Slic3r::TriangleMesh{};
    volume.origin_render_info_ptr->vertices_with_colors.second.clear();
}

bool bootstrap_repository_colors_from_render_info(const Slic3r::ModelVolume &volume)
{
    auto &repository = Slic3r::ColorCut::global_color_cut_attribute_repository();
    if (repository.get_volume_color_data(volume.id().id).has_value() || volume.origin_render_info_ptr == nullptr)
        return false;

    const indexed_triangle_set &source_mesh = volume.mesh().its;
    const size_t face_count = source_mesh.indices.size();
    if (face_count == 0)
        return false;

    auto color_group_map = repository.get_color_group_map();
    Slic3r::TriangleColor undefined_binding;
    undefined_binding.pid = -1;
    undefined_binding.indices[0] = -1;
    undefined_binding.indices[1] = -1;
    undefined_binding.indices[2] = -1;

    std::vector<Slic3r::TriangleColor> triangle_colors(face_count, undefined_binding);
    std::vector<double> best_sq_dist(face_count, std::numeric_limits<double>::max());
    constexpr double max_match_sq_dist = 1e-8;
    constexpr double min_normal_dot = 0.98;

    auto assign_render_triangle = [&](const indexed_triangle_set &render_mesh, size_t render_triangle_index, const Slic3r::TriangleColor &binding) {
        const Slic3r::Vec3d render_centroid = triangle_centroid(render_mesh, render_triangle_index);
        const Slic3r::Vec3d render_normal = triangle_normal(render_mesh, render_triangle_index);

        size_t best_triangle_index = face_count;
        double best_triangle_sq_dist = std::numeric_limits<double>::max();
        for (size_t source_triangle_index = 0; source_triangle_index < face_count; ++source_triangle_index) {
            if (triangle_normal(source_mesh, source_triangle_index).dot(render_normal) < min_normal_dot)
                continue;

            const double sq_dist = (triangle_centroid(source_mesh, source_triangle_index) - render_centroid).squaredNorm();
            if (sq_dist < best_triangle_sq_dist) {
                best_triangle_sq_dist = sq_dist;
                best_triangle_index = source_triangle_index;
            }
        }

        if (best_triangle_index >= face_count || best_triangle_sq_dist > max_match_sq_dist)
            return;

        if (best_triangle_sq_dist < best_sq_dist[best_triangle_index]) {
            best_sq_dist[best_triangle_index] = best_triangle_sq_dist;
            triangle_colors[best_triangle_index] = binding;
        }
    };

    for (const auto &entry : volume.origin_render_info_ptr->mesh_with_colors) {
        const Slic3r::TriangleColor binding = register_triangle_color_binding(color_group_map, entry.second);
        for (size_t triangle_index = 0; triangle_index < entry.first.its.indices.size(); ++triangle_index)
            assign_render_triangle(entry.first.its, triangle_index, binding);
    }

    const auto &vertex_mesh = volume.origin_render_info_ptr->vertices_with_colors.first.its;
    const auto &vertex_colors = volume.origin_render_info_ptr->vertices_with_colors.second;
    if (!vertex_mesh.indices.empty() && vertex_mesh.vertices.size() == vertex_colors.size()) {
        for (size_t triangle_index = 0; triangle_index < vertex_mesh.indices.size(); ++triangle_index) {
            const Slic3r::Vec3i &triangle = vertex_mesh.indices[triangle_index];
            const Slic3r::RGBA face_color{
                (vertex_colors[size_t(triangle[0])][0] + vertex_colors[size_t(triangle[1])][0] + vertex_colors[size_t(triangle[2])][0]) / 3.0f,
                (vertex_colors[size_t(triangle[0])][1] + vertex_colors[size_t(triangle[1])][1] + vertex_colors[size_t(triangle[2])][1]) / 3.0f,
                (vertex_colors[size_t(triangle[0])][2] + vertex_colors[size_t(triangle[1])][2] + vertex_colors[size_t(triangle[2])][2]) / 3.0f,
                (vertex_colors[size_t(triangle[0])][3] + vertex_colors[size_t(triangle[1])][3] + vertex_colors[size_t(triangle[2])][3]) / 3.0f
            };
            assign_render_triangle(vertex_mesh, triangle_index, register_triangle_color_binding(color_group_map, face_color));
        }
    }

    size_t assigned_triangles = 0;
    Slic3r::TriangleColor default_binding = undefined_binding;
    for (const Slic3r::TriangleColor &binding : triangle_colors) {
        if (binding.pid < 0)
            continue;
        if (default_binding.pid < 0)
            default_binding = binding;
        ++assigned_triangles;
    }

    if (assigned_triangles == 0)
        return false;

    Slic3r::ColorCut::ExternalVolumeColorData color_data;
    color_data.pid = default_binding.pid;
    color_data.pindex = default_binding.indices[0];
    color_data.triangle_colors = std::move(triangle_colors);
    repository.register_color_group_map(std::move(color_group_map));
    repository.register_volume_color_data(volume.id().id, std::move(color_data));

    ballerase_log("Bootstrapped repository triangle colors from origin render info for source volume '" + volume.name
        + "': assigned_triangles=" + std::to_string(assigned_triangles)
        + " / " + std::to_string(face_count));
    return true;
}

bool rebuild_model_render_cache_from_repository(Slic3r::Model &model)
{
    auto &repository = Slic3r::ColorCut::global_color_cut_attribute_repository();
    const auto &color_group_map = repository.get_color_group_map();
    if (color_group_map.empty())
        return false;

    std::unordered_map<int, Slic3r::VolumeColorInfo> volume_color_data;
    for (Slic3r::ModelObject *object : model.objects) {
        if (object == nullptr)
            continue;
        for (Slic3r::ModelVolume *volume : object->volumes) {
            if (volume == nullptr || !volume->is_model_part() || volume->is_cut_connector())
                continue;

            auto external_color_data = repository.get_volume_color_data(volume->id().id);
            if (!external_color_data.has_value())
                continue;

            clear_origin_render_cache(*volume);

            Slic3r::VolumeColorInfo color_info;
            color_info.pid = external_color_data->pid;
            color_info.pindex = external_color_data->pindex;
            color_info.triangle_colors = external_color_data->triangle_colors;
            volume_color_data.emplace(volume->id().id, std::move(color_info));
        }
    }

    if (volume_color_data.empty())
        return false;

    Slic3r::ObjDialogInOut render_info;
    const std::map<int, std::vector<std::string>> ordered_color_group_map(color_group_map.begin(), color_group_map.end());
    return extract_colors_to_obj_dialog(&model, ordered_color_group_map, volume_color_data, render_info);
}

std::string encode_mmu_filament_tag(int filament_id)
{
    if (filament_id <= 0)
        return {};
    if (filament_id == 1)
        return "4";
    if (filament_id == 2)
        return "8";

    std::ostringstream stream;
    stream << std::uppercase << std::hex << (filament_id - 3) << 'C';
    return stream.str();
}

std::string nearest_filament_mmu_tag_for_color(const Slic3r::ColorRGBA &color)
{
    Slic3r::GUI::Plater *plater = Slic3r::GUI::wxGetApp().plater();
    if (plater == nullptr)
        return {};

    const auto extruder_colors = plater->get_extruders_colors();
    if (extruder_colors.empty())
        return {};

    int best_filament_id = 1;
    double best_sq_dist = std::numeric_limits<double>::max();
    for (size_t color_index = 0; color_index < extruder_colors.size(); ++color_index) {
        const auto &candidate = extruder_colors[color_index];
        const double dr = double(candidate[0]) - double(color.r());
        const double dg = double(candidate[1]) - double(color.g());
        const double db = double(candidate[2]) - double(color.b());
        const double sq_dist = dr * dr + dg * dg + db * db;
        if (sq_dist < best_sq_dist) {
            best_sq_dist = sq_dist;
            best_filament_id = static_cast<int>(color_index) + 1;
        }
    }

    return encode_mmu_filament_tag(best_filament_id);
}

// ============================================================================
// Self-contained color transfer implementing the logic.md algorithm:
//   1. Inventory all original triangles by exact vertex geometry + their MMU tag
//   2. After boolean, for each output triangle:
//      - If it matches an original triangle exactly → copy original MMU tag
//      - If no match but centroid is on the source surface → shell fragment
//        → inherit the nearest source triangle's simplified dominant color
//      - If no match and centroid is far from source surface → cut surface
//        → assign cut surface tag
// No heuristics, no external library dependencies, no existing transfer modules.
// ============================================================================

static std::string quantize_vertex(const Slic3r::Vec3f &v)
{
    constexpr double scale = 1000000.0;
    std::ostringstream s;
    s << std::llround(double(v.x()) * scale) << ','
      << std::llround(double(v.y()) * scale) << ','
      << std::llround(double(v.z()) * scale);
    return s.str();
}

static std::string make_triangle_key(const indexed_triangle_set &its, size_t face_index)
{
    const Slic3r::Vec3i &face = its.indices[face_index];
    std::array<std::string, 3> verts = {
        quantize_vertex(its.vertices[face[0]]),
        quantize_vertex(its.vertices[face[1]]),
        quantize_vertex(its.vertices[face[2]])
    };
    std::sort(verts.begin(), verts.end());
    return verts[0] + "|" + verts[1] + "|" + verts[2];
}

// Compute the squared distance from point P to triangle (A, B, C) in 3D.
// Returns the minimum squared distance from P to any point on the triangle.
static double point_to_triangle_sq_distance(
    const Slic3r::Vec3d &P,
    const Slic3r::Vec3d &A,
    const Slic3r::Vec3d &B,
    const Slic3r::Vec3d &C)
{
    const Slic3r::Vec3d AB = B - A;
    const Slic3r::Vec3d AC = C - A;
    const Slic3r::Vec3d AP = P - A;

    const double d1 = AB.dot(AP);
    const double d2 = AC.dot(AP);
    if (d1 <= 0.0 && d2 <= 0.0) return AP.squaredNorm(); // closest to A

    const Slic3r::Vec3d BP = P - B;
    const double d3 = AB.dot(BP);
    const double d4 = AC.dot(BP);
    if (d3 >= 0.0 && d4 <= d3) return BP.squaredNorm(); // closest to B

    const Slic3r::Vec3d CP = P - C;
    const double d5 = AB.dot(CP);
    const double d6 = AC.dot(CP);
    if (d6 >= 0.0 && d5 <= d6) return CP.squaredNorm(); // closest to C

    const double vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
        const double v = d1 / (d1 - d3);
        return (A + v * AB - P).squaredNorm(); // closest to edge AB
    }

    const double vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
        const double w = d2 / (d2 - d6);
        return (A + w * AC - P).squaredNorm(); // closest to edge AC
    }

    const double va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) {
        const double w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return (B + w * (C - B) - P).squaredNorm(); // closest to edge BC
    }

    const double denom = 1.0 / (va + vb + vc);
    const double v = vb * denom;
    const double w = vc * denom;
    return (A + AB * v + AC * w - P).squaredNorm(); // inside triangle
}

// For a triangle in an indexed_triangle_set, find the nearest source triangle by
// centroid-to-source-triangle distance. Returns {source_face_index, squared_distance}.
struct NearestSourceResult {
    int    face_index{-1};
    double sq_distance{std::numeric_limits<double>::max()};
};

static NearestSourceResult find_nearest_source_triangle(
    const Slic3r::Vec3d &centroid,
    const indexed_triangle_set &source_its)
{
    NearestSourceResult best;
    for (size_t si = 0; si < source_its.indices.size(); ++si) {
        const Slic3r::Vec3i &face = source_its.indices[si];
        const Slic3r::Vec3d A = source_its.vertices[face[0]].cast<double>();
        const Slic3r::Vec3d B = source_its.vertices[face[1]].cast<double>();
        const Slic3r::Vec3d C = source_its.vertices[face[2]].cast<double>();
        const double sq_dist = point_to_triangle_sq_distance(centroid, A, B, C);
        if (sq_dist < best.sq_distance) {
            best.sq_distance = sq_dist;
            best.face_index = static_cast<int>(si);
        }
    }
    return best;
}

static Slic3r::Vec3d compute_triangle_centroid_3d(const indexed_triangle_set &its, size_t face_index)
{
    const Slic3r::Vec3i &face = its.indices[face_index];
    return (its.vertices[face[0]].cast<double>()
          + its.vertices[face[1]].cast<double>()
          + its.vertices[face[2]].cast<double>()) / 3.0;
}

static std::unique_ptr<Slic3r::TriangleSelector> build_ball_eraser_mmu_selector(
    const Slic3r::ModelVolume &source_volume,
    const Slic3r::TriangleMesh &source_mesh)
{
    if (source_volume.mmu_segmentation_facets.empty())
        return nullptr;

    auto selector = std::make_unique<Slic3r::TriangleSelector>(source_mesh);
    selector->deserialize(source_volume.mmu_segmentation_facets.get_data(), false);
    return selector;
}

static std::optional<int> sample_mmu_state_at_point(
    Slic3r::TriangleSelector &selector,
    int source_triangle_index,
    const Slic3r::Vec3d &mesh_point)
{
    const auto &triangles = selector.get_triangles();
    const auto &neighbors = selector.get_neighbors();
    if (source_triangle_index < 0 || source_triangle_index >= static_cast<int>(triangles.size())
        || source_triangle_index >= static_cast<int>(neighbors.size()))
        return std::nullopt;

    int leaf_triangle_index = selector.select_unsplit_triangle(mesh_point.cast<float>(), source_triangle_index);
    if (leaf_triangle_index < 0)
        leaf_triangle_index = source_triangle_index;
    if (leaf_triangle_index < 0 || leaf_triangle_index >= static_cast<int>(triangles.size()))
        return std::nullopt;

    const auto &triangle = triangles[size_t(leaf_triangle_index)];
    if (!triangle.valid() || triangle.is_split())
        return std::nullopt;

    return static_cast<int>(triangle.get_state());
}

struct BallEraserTriangleRegion {
    Slic3r::Vec3d a;
    Slic3r::Vec3d b;
    Slic3r::Vec3d c;
};

static Slic3r::Vec3d region_centroid(const BallEraserTriangleRegion &region)
{
    return (region.a + region.b + region.c) / 3.0;
}

static std::vector<Slic3r::Vec3d> region_control_points(const BallEraserTriangleRegion &region)
{
    const auto lerp = [](const Slic3r::Vec3d &lhs, const Slic3r::Vec3d &rhs, double t) {
        return lhs * (1.0 - t) + rhs * t;
    };

    const Slic3r::Vec3d ab = (region.a + region.b) * 0.5;
    const Slic3r::Vec3d bc = (region.b + region.c) * 0.5;
    const Slic3r::Vec3d ca = (region.c + region.a) * 0.5;
    return {
        region.a,
        region.b,
        region.c,
        lerp(region.a, region.b, 0.25),
        ab,
        lerp(region.a, region.b, 0.75),
        lerp(region.b, region.c, 0.25),
        bc,
        lerp(region.b, region.c, 0.75),
        lerp(region.c, region.a, 0.25),
        ca,
        lerp(region.c, region.a, 0.75),
        region_centroid(region)
    };
}

static std::array<BallEraserTriangleRegion, 4> split_region_four_way(const BallEraserTriangleRegion &region)
{
    const Slic3r::Vec3d ab = (region.a + region.b) * 0.5;
    const Slic3r::Vec3d bc = (region.b + region.c) * 0.5;
    const Slic3r::Vec3d ca = (region.c + region.a) * 0.5;
    return {{
        {region.a, ab, ca},
        {ab, region.b, bc},
        {bc, region.c, ca},
        {ab, bc, ca}
    }};
}

static std::string sample_source_leaf_tag(
    Slic3r::TriangleSelector *selector,
    const Slic3r::sla::IndexedMesh &source_indexed_mesh,
    const Slic3r::Vec3d &sample_point)
{
    if (selector == nullptr)
        return {};

    int nearest_face_index = -1;
    Slic3r::Vec3d nearest_point = sample_point;
    source_indexed_mesh.squared_distance(sample_point, nearest_face_index, nearest_point);
    if (nearest_face_index < 0)
        return {};

    const auto state = sample_mmu_state_at_point(*selector, nearest_face_index, nearest_point);
    if (!state.has_value())
        return {};

    return encode_mmu_leaf_state(*state);
}

static std::string dominant_region_sample_tag(const std::vector<std::string> &sample_tags)
{
    std::map<std::string, int> counts;
    for (const std::string &tag : sample_tags) {
        if (!tag.empty())
            ++counts[tag];
    }

    if (counts.empty())
        return {};

    auto best = counts.begin();
    for (auto it = std::next(counts.begin()); it != counts.end(); ++it) {
        if (it->second > best->second)
            best = it;
    }
    return best->first;
}

static bool region_samples_are_uniform(const std::vector<std::string> &sample_tags)
{
    const std::string dominant = dominant_region_sample_tag(sample_tags);
    if (dominant.empty())
        return false;

    return std::all_of(sample_tags.begin(), sample_tags.end(), [&dominant](const std::string &tag) {
        return tag == dominant;
    });
}

static std::string build_reprojected_fragment_tag(
    Slic3r::TriangleSelector *selector,
    const Slic3r::sla::IndexedMesh &source_indexed_mesh,
    const BallEraserTriangleRegion &region,
    int depth,
    int max_depth)
{
    const auto control_points = region_control_points(region);
    std::vector<std::string> sample_tags(control_points.size());
    for (size_t sample_index = 0; sample_index < control_points.size(); ++sample_index)
        sample_tags[sample_index] = sample_source_leaf_tag(selector, source_indexed_mesh, control_points[sample_index]);

    const std::string dominant_tag = dominant_region_sample_tag(sample_tags);

    if (dominant_tag.empty())
        return {};

    if (region_samples_are_uniform(sample_tags) || depth >= max_depth)
        return dominant_tag;

    const auto children = split_region_four_way(region);
    std::array<std::string, 4> child_tags;
    for (size_t child_index = 0; child_index < child_tags.size(); ++child_index) {
        child_tags[child_index] = build_reprojected_fragment_tag(selector, source_indexed_mesh, children[child_index], depth + 1, max_depth);
        if (child_tags[child_index].empty())
            child_tags[child_index] = dominant_tag;
    }

    if (std::all_of(child_tags.begin(), child_tags.end(), [&child_tags](const std::string &tag) { return tag == child_tags.front(); }))
        return child_tags.front();

    return child_tags[0] + child_tags[1] + child_tags[2] + child_tags[3] + "3";
}

static void apply_logic_md_color_transfer(
    const Slic3r::ModelObject   &source_object,
    const Slic3r::ModelVolume   &source_volume,
    int                          instance_index,
    const Slic3r::ModelObjectPtrs &result_objects,
    const std::string           &cut_surface_mmu_tag)
{
    // ---- Step 1: Build source mesh in the same coordinate space as the boolean result ----
    Slic3r::TriangleMesh source_mesh(source_volume.mesh());
    if (instance_index >= 0 && instance_index < static_cast<int>(source_object.instances.size())) {
        const Slic3r::ModelInstance *inst = source_object.instances[static_cast<size_t>(instance_index)];
        source_mesh.transform(inst->get_transformation().get_matrix_no_offset() * source_volume.get_matrix(), true);
    } else {
        source_mesh.transform(source_volume.get_matrix(), true);
    }

    // ---- Step 2: Inventory all original triangles ----
    //   a) geometry key → MMU tag (for exact matching)
    //   b) source-surface MMU selector for reprojection onto new shell triangles
    const size_t src_count = source_mesh.its.indices.size();
    const Slic3r::sla::IndexedMesh source_indexed_mesh(source_mesh);
    std::unique_ptr<Slic3r::TriangleSelector> source_mmu_selector = build_ball_eraser_mmu_selector(source_volume, source_mesh);

    std::unordered_map<std::string, std::string> source_inventory;
    source_inventory.reserve(src_count);

    for (size_t ti = 0; ti < src_count; ++ti) {
        const std::string key = make_triangle_key(source_mesh.its, ti);
        const std::string mmu = source_volume.mmu_segmentation_facets.get_triangle_as_string(static_cast<int>(ti));
        source_inventory.emplace(key, mmu);
    }

    // Shell fragment distance threshold: triangles whose centroid is within this
    // distance of the source mesh surface are considered shell fragments, not cut surface.
    // 0.1mm (100 microns) is generous enough for floating-point drift from boolean ops.
    constexpr double kShellDistanceThreshold = 0.1;
    constexpr double kShellSqDistanceThreshold = kShellDistanceThreshold * kShellDistanceThreshold;

    constexpr int kFragmentReprojectionDepth = 7;

    ballerase_log("logic.md transfer: source_triangles=" + std::to_string(src_count)
        + " inventory_entries=" + std::to_string(source_inventory.size())
        + " cut_surface_tag='" + cut_surface_mmu_tag + "'"
        + " shell_distance_threshold=" + std::to_string(kShellDistanceThreshold)
        + " fragment_control_points=" + std::to_string(region_control_points({Slic3r::Vec3d::Zero(), Slic3r::Vec3d::UnitX(), Slic3r::Vec3d::UnitY()}).size())
        + " fragment_reprojection_depth=" + std::to_string(kFragmentReprojectionDepth));

    // ---- Step 3: For each output volume, classify every triangle ----
    for (Slic3r::ModelObject *obj : result_objects) {
        if (obj == nullptr) continue;
        for (Slic3r::ModelVolume *vol : obj->volumes) {
            if (vol == nullptr || !vol->is_model_part()) continue;

            // Build transformed target mesh in the same coordinate space
            Slic3r::TriangleMesh target_mesh(vol->mesh());
            Slic3r::Transform3d target_xf = vol->get_matrix();
            if (vol->get_object() != nullptr && !vol->get_object()->instances.empty())
                target_xf = vol->get_object()->instances.front()->get_transformation().get_matrix_no_offset() * target_xf;
            target_mesh.transform(target_xf, true);

            const size_t tri_count = vol->mesh().its.indices.size();
            size_t matched = 0;
            size_t shell_fragment = 0;
            size_t cut_surface = 0;

            // Collect new MMU entries
            std::vector<std::pair<int, std::string>> mmu_entries;
            mmu_entries.reserve(tri_count);

            for (size_t ti = 0; ti < tri_count; ++ti) {
                const std::string key = make_triangle_key(target_mesh.its, ti);
                const auto it = source_inventory.find(key);
                if (it != source_inventory.end()) {
                    // Non-impacted triangle: copy original color exactly
                    if (!it->second.empty())
                        mmu_entries.emplace_back(static_cast<int>(ti), it->second);
                    ++matched;
                } else {
                    // New triangle from boolean subdivision — classify it
                    const Slic3r::Vec3d centroid = compute_triangle_centroid_3d(target_mesh.its, ti);
                    int nearest_face_index = -1;
                    Slic3r::Vec3d nearest_point = centroid;
                    const double sq_distance = source_indexed_mesh.squared_distance(centroid, nearest_face_index, nearest_point);
                    if (nearest_face_index >= 0 && sq_distance < kShellSqDistanceThreshold) {
                        // Shell fragment: rebuild an MMU subdivision tree by sampling the
                        // original painted surface over the fragment's actual area. This
                        // treats the new triangles as a reprojected surface patch instead of
                        // copying old per-triangle encodings onto new shapes.
                        const Slic3r::Vec3i &face = target_mesh.its.indices[ti];
                        const BallEraserTriangleRegion region{
                            target_mesh.its.vertices[face[0]].cast<double>(),
                            target_mesh.its.vertices[face[1]].cast<double>(),
                            target_mesh.its.vertices[face[2]].cast<double>()
                        };
                        const std::string tag = build_reprojected_fragment_tag(
                            source_mmu_selector.get(),
                            source_indexed_mesh,
                            region,
                            0,
                            kFragmentReprojectionDepth);
                        if (!tag.empty())
                            mmu_entries.emplace_back(static_cast<int>(ti), tag);
                        ++shell_fragment;
                    } else {
                        // Cut surface: genuinely new face from the boolean cut
                        if (!cut_surface_mmu_tag.empty())
                            mmu_entries.emplace_back(static_cast<int>(ti), cut_surface_mmu_tag);
                        ++cut_surface;
                    }
                }
            }

            // Write MMU segmentation from scratch
            vol->mmu_segmentation_facets.reset();
            vol->mmu_segmentation_facets.reserve(static_cast<int>(tri_count));
            for (const auto &[tri_id, tag] : mmu_entries)
                vol->mmu_segmentation_facets.set_triangle_from_string(tri_id, tag);
            vol->mmu_segmentation_facets.shrink_to_fit();

            ballerase_log("  volume '" + vol->name + "': total=" + std::to_string(tri_count)
                + " matched_original=" + std::to_string(matched)
                + " shell_fragment=" + std::to_string(shell_fragment)
                + " cut_surface=" + std::to_string(cut_surface));
        }
    }
}

Slic3r::TriangleMesh build_ball_eraser_source_mesh(const Slic3r::ModelObject &object, const Slic3r::ModelVolume &volume, int instance_index)
{
    Slic3r::TriangleMesh source_mesh(volume.mesh());
    if (instance_index >= 0 && instance_index < static_cast<int>(object.instances.size())) {
        const Slic3r::ModelInstance *instance = object.instances[static_cast<size_t>(instance_index)];
        source_mesh.transform(instance->get_transformation().get_matrix_no_offset() * volume.get_matrix(), true);
    } else {
        source_mesh.transform(volume.get_matrix(), true);
    }
    return source_mesh;
}

Slic3r::TriangleMesh build_ball_eraser_target_mesh(const Slic3r::ModelVolume &volume)
{
    Slic3r::TriangleMesh target_mesh(volume.mesh());
    Slic3r::Transform3d target_transform = volume.get_matrix();
    if (volume.get_object() != nullptr && !volume.get_object()->instances.empty())
        target_transform = volume.get_object()->instances.front()->get_transformation().get_matrix_no_offset() * target_transform;
    target_mesh.transform(target_transform, true);
    return target_mesh;
}

void mark_ball_eraser_cut_surface_triangles(
    const Slic3r::ModelObject &source_object,
    const Slic3r::ModelVolume &source_volume,
    int instance_index,
    const std::vector<Slic3r::BallEraser::Stroke> &strokes,
    Slic3r::ModelObjectPtrs &objects)
{
    Slic3r::TriangleMesh source_mesh = build_ball_eraser_source_mesh(source_object, source_volume, instance_index);
    if (source_mesh.empty())
        return;

    const Slic3r::sla::IndexedMesh source_indexed_mesh(source_mesh);
    const Slic3r::TriangleMesh cutter_mesh = build_ball_eraser_cutter_mesh(strokes);
    std::unique_ptr<Slic3r::sla::IndexedMesh> cutter_indexed_mesh;
    if (!cutter_mesh.empty())
        cutter_indexed_mesh = std::make_unique<Slic3r::sla::IndexedMesh>(cutter_mesh);

    for (Slic3r::ModelObject *object : objects) {
        if (object == nullptr)
            continue;

        for (Slic3r::ModelVolume *volume : object->volumes) {
            if (volume == nullptr || !volume->is_model_part() || volume->is_cut_connector())
                continue;

            if (!volume->exterior_facets.empty())
                continue;

            Slic3r::TriangleMesh transformed_target_mesh = build_ball_eraser_target_mesh(*volume);
            volume->exterior_facets.reset();
            volume->exterior_facets.reserve(static_cast<int>(transformed_target_mesh.its.indices.size()));

            for (size_t triangle_index = 0; triangle_index < transformed_target_mesh.its.indices.size(); ++triangle_index) {
                if (is_ball_eraser_residual_surface_triangle(
                    source_indexed_mesh,
                    cutter_indexed_mesh.get(),
                    transformed_target_mesh,
                    triangle_index))
                    volume->exterior_facets.set_triangle_from_string(static_cast<int>(triangle_index), "1");
            }

            volume->exterior_facets.shrink_to_fit();
        }
    }
}

}

namespace Slic3r {
namespace GUI {

GLGizmoBallEraser::GLGizmoBallEraser(GLCanvas3D& parent, unsigned int sprite_id)
    : GLGizmoBase(parent, sprite_id)
{
    m_sphere_dimensions = { 10.0, 10.0, 10.0 };
    m_cube_dimensions = { 10.0, 10.0, 10.0 };
}

std::string GLGizmoBallEraser::get_icon_filename(bool b_dark_mode) const
{
    return b_dark_mode ? "toolbar_balleraser_dark.svg" : "toolbar_balleraser.svg";
}

bool GLGizmoBallEraser::on_init()
{
    m_shortcut_key = 0;
    rebuild_preview_models();
    return true;
}

std::string GLGizmoBallEraser::on_get_name() const
{
    if (!on_is_activable() && m_state == EState::Off)
        return _u8L("BallEraser") + ":\n" + _u8L("Please select single object.");

    return _u8L("BallEraser");
}

bool GLGizmoBallEraser::on_is_activable() const
{
    const Selection &selection = m_parent.get_selection();
    const int object_idx = selection.get_object_idx();
    if (object_idx < 0 || selection.is_wipe_tower())
        return false;

    return selection.is_single_full_instance() && !m_parent.is_layers_editing_enabled();
}

CommonGizmosDataID GLGizmoBallEraser::on_get_requirements() const
{
    return CommonGizmosDataID(int(CommonGizmosDataID::SelectionInfo)
        | int(CommonGizmosDataID::InstancesHider));
}

bool GLGizmoBallEraser::gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool, bool, bool)
{
    if (!m_session.active)
        return false;

    if (action == SLAGizmoEventType::Moving) {
        return update_hover_state(mouse_position, true);
    }

    if (action == SLAGizmoEventType::LeftDown) {
        update_hover_state(mouse_position, true);
        m_session.pointer_down = true;
        m_session.pointer_dragged = false;
        m_session.drag_ownership = hovered_target_supports_capture()
            ? BallEraser::DragOwnership::BallEraser
            : BallEraser::DragOwnership::PassThrough;
        if (m_session.drag_ownership == BallEraser::DragOwnership::BallEraser)
            start_stroke_capture();
        return m_session.drag_ownership == BallEraser::DragOwnership::BallEraser;
    }

    if (action == SLAGizmoEventType::Dragging) {
        if (!m_session.pointer_down)
            return false;

        m_session.pointer_dragged = true;
        if (m_session.drag_ownership != BallEraser::DragOwnership::BallEraser)
            return false;

        update_hover_state(mouse_position, true);
        update_in_progress_stroke_sampling();
        m_session.pointer_dragged = m_session.in_progress_stroke_centers_instance_local.size() > 1;
        return true;
    }

    if (action == SLAGizmoEventType::LeftUp) {
        update_hover_state(mouse_position, m_session.drag_ownership == BallEraser::DragOwnership::BallEraser);

        const bool pointer_was_down = m_session.pointer_down;
        const bool tool_owned_pointer = m_session.pointer_down && m_session.drag_ownership == BallEraser::DragOwnership::BallEraser;
        const bool dragged = m_session.pointer_dragged;
        const BallEraser::DragOwnership drag_ownership = m_session.drag_ownership;
        m_session.pointer_down = false;
        m_session.pointer_dragged = false;
        m_session.drag_ownership = BallEraser::DragOwnership::None;

        if (pointer_was_down && !dragged && drag_ownership == BallEraser::DragOwnership::PassThrough
            && m_session.hover_target == BallEraser::HoverTarget::EmptyScene) {
            clear_in_progress_stroke();
            return prompt_for_exit_if_needed();
        }

        if (!tool_owned_pointer)
            return false;

        if (m_session.hover_target != BallEraser::HoverTarget::EmptyScene)
            update_in_progress_stroke_sampling();

        commit_in_progress_stroke();

        return true;
    }

    if (action == SLAGizmoEventType::RightDown) {
        update_hover_state(mouse_position, false);
        return false;
    }

    if (action == SLAGizmoEventType::RightUp) {
        update_hover_state(mouse_position, false);
        return false;
    }

    return false;
}

const BallEraser::Dimensions &GLGizmoBallEraser::active_dimensions() const
{
    return m_session.primitive == BallEraser::PrimitiveType::Sphere ? m_sphere_dimensions : m_cube_dimensions;
}

Vec3d GLGizmoBallEraser::active_rotation_degrees() const
{
    return m_session.primitive == BallEraser::PrimitiveType::Sphere ? Vec3d::Zero() : m_cube_rotation_degrees;
}

void GLGizmoBallEraser::close()
{
    auto &mng = m_parent.get_gizmos_manager();
    if (mng.get_current_type() == GLGizmosManager::BallEraser)
        mng.open_gizmo(GLGizmosManager::BallEraser);
}

int GLGizmoBallEraser::selected_instance_index() const
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

Vec3d GLGizmoBallEraser::instance_local_to_world(const Vec3d &point) const
{
    const Selection &selection = m_parent.get_selection();
    const Model *model = selection.get_model();
    if (model == nullptr || m_session.object_index < 0 || size_t(m_session.object_index) >= model->objects.size())
        return point;

    const ModelObject *object = model->objects[size_t(m_session.object_index)];
    if (object == nullptr || m_session.instance_index < 0 || size_t(m_session.instance_index) >= object->instances.size())
        return point;

    const ModelInstance *instance = object->instances[size_t(m_session.instance_index)];
    return point + instance->get_offset();
}

void GLGizmoBallEraser::mark_preview_dirty()
{
    request_preview_refresh();
}

void GLGizmoBallEraser::request_preview_refresh()
{
    m_parent.request_extra_frame();
}

void GLGizmoBallEraser::rebuild_preview_models()
{
    m_sphere_preview.reset();
    m_cube_preview.reset();
    m_sphere_preview.init_from(its_make_sphere(0.5, PI / 18.0));
    m_cube_preview.init_from(its_make_cube(1.0, 1.0, 1.0));
    m_session.preview_dirty = false;
}

double GLGizmoBallEraser::current_drag_sample_spacing() const
{
    return ball_eraser_drag_sample_spacing(m_session.primitive, active_dimensions());
}

std::vector<Vec3d> GLGizmoBallEraser::simplify_stroke_samples(const std::vector<Vec3d> &samples) const
{
    if (samples.empty())
        return {};

    std::vector<Vec3d> simplified;
    simplified.reserve(samples.size());
    simplified.push_back(samples.front());

    const double minimum_spacing = current_drag_sample_spacing();
    for (size_t index = 1; index < samples.size(); ++index) {
        const bool is_last = index + 1 == samples.size();
        const Vec3d &sample = samples[index];
        if (!is_last) {
            const Vec3d delta = sample - simplified.back();
            if (delta.norm() < minimum_spacing)
                continue;
        }
        simplified.push_back(sample);
    }

    return simplified;
}

BallEraser::Stroke GLGizmoBallEraser::build_stroke_from_samples(const std::vector<Vec3d> &samples, BallEraser::Stroke::GestureType gesture) const
{
    BallEraser::Stroke stroke;
    const std::vector<Vec3d> simplified_samples = simplify_stroke_samples(samples);
    stroke.primitive = m_session.primitive;
    stroke.gesture = simplified_samples.size() > 1 ? gesture : BallEraser::Stroke::GestureType::Discrete;
    stroke.dimensions = active_dimensions();
    stroke.rotation_degrees = active_rotation_degrees();
    stroke.placement_centers_instance_local = simplified_samples;
    return stroke;
}

void GLGizmoBallEraser::start_stroke_capture()
{
    m_session.stroke_capture_active = true;
    m_session.drag_sample_spacing = current_drag_sample_spacing();
    m_session.drag_start_center_instance_local = m_session.active_center_instance_local;
    m_session.drag_last_sample_center_instance_local = m_session.active_center_instance_local;
    m_session.in_progress_stroke_centers_instance_local.clear();
    append_in_progress_stroke_sample(m_session.active_center_instance_local);
}

void GLGizmoBallEraser::append_in_progress_stroke_sample(const Vec3d &center_instance_local)
{
    if (!m_session.in_progress_stroke_centers_instance_local.empty()) {
        const Vec3d delta = center_instance_local - m_session.in_progress_stroke_centers_instance_local.back();
        if (delta.squaredNorm() <= 1e-8)
            return;
    }

    m_session.in_progress_stroke_centers_instance_local.push_back(center_instance_local);
    m_session.drag_last_sample_center_instance_local = center_instance_local;
    request_preview_refresh();
}

void GLGizmoBallEraser::update_in_progress_stroke_sampling()
{
    if (!m_session.stroke_capture_active)
        return;
    if (m_session.hover_target == BallEraser::HoverTarget::EmptyScene)
        return;

    const Vec3d delta = m_session.active_center_instance_local - m_session.drag_last_sample_center_instance_local;
    if (m_session.in_progress_stroke_centers_instance_local.empty()
        || delta.norm() >= m_session.drag_sample_spacing) {
        append_in_progress_stroke_sample(m_session.active_center_instance_local);
    }
}

void GLGizmoBallEraser::commit_in_progress_stroke()
{
    if (!m_session.stroke_capture_active || m_session.in_progress_stroke_centers_instance_local.empty()) {
        clear_in_progress_stroke();
        return;
    }

    BallEraser::Stroke stroke = build_stroke_from_samples(
        m_session.in_progress_stroke_centers_instance_local,
        m_session.pointer_dragged ? BallEraser::Stroke::GestureType::Drag : BallEraser::Stroke::GestureType::Discrete);
    stroke.sequence_index = m_session.pending_strokes.size();
    m_session.pending_strokes.emplace_back(std::move(stroke));
    clear_in_progress_stroke();
    request_preview_refresh();
}

void GLGizmoBallEraser::clear_in_progress_stroke()
{
    m_session.stroke_capture_active = false;
    m_session.in_progress_stroke_centers_instance_local.clear();
    m_session.drag_start_center_instance_local = Vec3d::Zero();
    m_session.drag_last_sample_center_instance_local = Vec3d::Zero();
}

void GLGizmoBallEraser::reset_session_from_selection()
{
    const int previous_object_index = m_session.object_index;
    const int previous_instance_index = m_session.instance_index;
    const auto primitive = m_session.primitive;
    const auto output_mode = m_session.output_mode;
    const auto hover_target = m_session.hover_target;
    const auto active_center = m_session.active_center_instance_local;
    const auto hover_center = m_session.hover_center_instance_local;
    const auto pending_strokes = m_session.pending_strokes;
    m_session.reset();
    m_session.primitive = primitive;
    m_session.output_mode = output_mode;
    if (!on_is_activable())
        return;

    const Selection &selection = m_parent.get_selection();
    m_session.object_index = selection.get_object_idx();
    m_session.instance_index = selected_instance_index();
    m_session.active = m_session.has_target();
    m_session.hover_target = hover_target;
    if (previous_object_index == m_session.object_index && previous_instance_index == m_session.instance_index) {
        m_session.hover_center_instance_local = hover_center;
        m_session.active_center_instance_local = active_center;
        m_session.pending_strokes = pending_strokes;
    } else {
        m_session.hover_center_instance_local = Vec3d::Zero();
        m_session.active_center_instance_local = current_stroke_center_instance_local();
    }
    m_session.preview_dirty = true;
}

bool GLGizmoBallEraser::hit_test_selected_object(const Vec2d &mouse_position, Vec3d &hit_instance_local) const
{
    const GLVolume *hovered_volume = get_first_hovered_gl_volume(m_parent);
    if (hovered_volume == nullptr || hovered_volume->ori_mesh == nullptr)
        return false;
    if (hovered_volume->object_idx() != m_session.object_index || hovered_volume->instance_idx() != m_session.instance_index)
        return false;
    if (hovered_volume->is_wipe_tower || hovered_volume->is_extrusion_path)
        return false;

    const Plater *plater = wxGetApp().plater();
    if (plater == nullptr)
        return false;

    const Camera &camera = plater->get_camera();
    MeshRaycaster raycaster(*hovered_volume->ori_mesh);
    Vec3f hit = Vec3f::Zero();
    Vec3f normal = Vec3f::Zero();
    if (!raycaster.unproject_on_mesh(mouse_position, hovered_volume->world_matrix(), camera, hit, normal))
        return false;

    const Transform3d instance_local_from_mesh = hovered_volume->get_instance_transformation().get_matrix_no_offset()
        * hovered_volume->get_volume_transformation().get_matrix();
    hit_instance_local = instance_local_from_mesh * hit.cast<double>();
    return true;
}

BallEraser::HoverTarget GLGizmoBallEraser::classify_hover_target(const Vec3d &instance_local_point) const
{
    const Vec3d delta = instance_local_point - m_session.active_center_instance_local;
    if (m_session.primitive == BallEraser::PrimitiveType::Sphere) {
        const double radius = std::max(0.5, 0.5 * active_dimensions().x + kCursorHoverSlack);
        if (delta.squaredNorm() <= radius * radius)
            return BallEraser::HoverTarget::Cursor;
    } else {
        const Transform3d local_rotation = Geometry::rotation_transform(ball_eraser_rotation_radians(active_rotation_degrees()));
        const Vec3d local_delta = local_rotation.linear().transpose() * delta;
        const Vec3d half_extents = Vec3d(active_dimensions().x, active_dimensions().y, active_dimensions().z) * 0.5
            + Vec3d::Constant(kCursorHoverSlack);
        if (std::abs(local_delta.x()) <= half_extents.x()
            && std::abs(local_delta.y()) <= half_extents.y()
            && std::abs(local_delta.z()) <= half_extents.z()) {
            return BallEraser::HoverTarget::Cursor;
        }
    }

    return BallEraser::HoverTarget::EditedObject;
}

bool GLGizmoBallEraser::update_hover_state(const Vec2d &mouse_position, bool update_active_center)
{
    Vec3d hit_instance_local = Vec3d::Zero();
    if (!hit_test_selected_object(mouse_position, hit_instance_local)) {
        const bool changed = m_session.hover_target != BallEraser::HoverTarget::EmptyScene;
        m_session.hover_target = BallEraser::HoverTarget::EmptyScene;
        if (changed)
            m_parent.request_extra_frame();
        return changed;
    }

    const BallEraser::HoverTarget new_target = classify_hover_target(hit_instance_local);
    const bool changed = m_session.hover_target != new_target || (m_session.hover_center_instance_local - hit_instance_local).squaredNorm() > 1e-8;
    m_session.hover_target = new_target;
    m_session.hover_center_instance_local = hit_instance_local;
    if (update_active_center)
        m_session.active_center_instance_local = hit_instance_local;
    if (changed || update_active_center)
        m_parent.request_extra_frame();
    return true;
}

bool GLGizmoBallEraser::hovered_target_supports_capture() const
{
    return m_session.hover_target == BallEraser::HoverTarget::Cursor
        || m_session.hover_target == BallEraser::HoverTarget::EditedObject;
}

bool GLGizmoBallEraser::prompt_for_exit_if_needed()
{
    if (m_session.pending_strokes.empty()) {
        close();
        return true;
    }

    m_session.exit_confirmation_pending = true;
    MessageDialog dlg(nullptr,
        _L("BallEraser has pending strokes. What would you like to do?"),
        _L("BallEraser"),
        wxICON_QUESTION);
    dlg.AddButton(wxID_YES, _L("Apply"), true);
    dlg.AddButton(wxID_NO, _L("Discard"));
    dlg.AddButton(wxID_CANCEL, _L("Stay"));

    const int result = dlg.ShowModal();
    m_session.exit_confirmation_pending = false;

    if (result == wxID_YES) {
        apply_pending_strokes();
        return true;
    }

    if (result == wxID_NO) {
        clear_pending_strokes();
        close();
        return true;
    }

    return true;
}

Vec3d GLGizmoBallEraser::current_stroke_center_instance_local() const
{
    const Selection &selection = m_parent.get_selection();
    const BoundingBoxf3 box = selection.get_bounding_box();
    if (!box.defined)
        return Vec3d::Zero();

    const Vec3d world_center = box.center();
    const Model *model = selection.get_model();
    if (model == nullptr || m_session.object_index < 0 || size_t(m_session.object_index) >= model->objects.size())
        return world_center;

    const ModelObject *object = model->objects[size_t(m_session.object_index)];
    if (object == nullptr || m_session.instance_index < 0 || size_t(m_session.instance_index) >= object->instances.size())
        return world_center;

    const ModelInstance *instance = object->instances[size_t(m_session.instance_index)];
    return world_center - instance->get_offset();
}

void GLGizmoBallEraser::apply_preview_state()
{
    if (!m_session.active)
        return;

    const Selection &selection = m_parent.get_selection();
    const Model *model = selection.get_model();
    if (model == nullptr || m_session.object_index < 0 || size_t(m_session.object_index) >= model->objects.size())
        return;

    const ModelObject *object = model->objects[size_t(m_session.object_index)];
    if (object == nullptr)
        return;

    m_parent.toggle_model_objects_visibility(true, object, m_session.instance_index);
    m_session.preview_scene_isolated = true;

    const auto &volumes = m_parent.get_volumes().volumes;
    const std::array<float, 4> gray{ 0.62f, 0.62f, 0.62f, 1.0f };
    m_parent.set_use_volume_color_override(true);
    for (unsigned int i = 0; i < (unsigned int)volumes.size(); ++i) {
        const GLVolume *gl_volume = volumes[i];
        if (gl_volume == nullptr)
            continue;

        const bool is_target = gl_volume->composite_id.object_id == m_session.object_index &&
            gl_volume->composite_id.instance_id == m_session.instance_index;
        if (is_target)
            m_parent.set_volume_color_override(i, gray);
    }
    m_session.preview_monocolor_enabled = true;
    mark_preview_dirty();
}

void GLGizmoBallEraser::restore_preview_state()
{
    m_parent.clear_all_volume_color_overrides();
    m_parent.set_use_volume_color_override(false);
    m_parent.toggle_model_objects_visibility(true);
    m_session.preview_scene_isolated = false;
    m_session.preview_monocolor_enabled = false;
}

void GLGizmoBallEraser::refresh_preview_state()
{
    restore_preview_state();
    apply_preview_state();
}

void GLGizmoBallEraser::apply_pending_strokes()
{
    // Clear log at start of each apply
    {
        std::lock_guard<std::mutex> lock(s_ballerase_log_mutex);
        std::ofstream out(ballerase_log_file_path(), std::ios::trunc);
    }
    BallEraser::sculpt_log_reset();
    ballerase_log_phase("BALLERASER APPLY BEGIN");
    ballerase_log("log_path: " + ballerase_log_file_path());
    BallEraser::sculpt_log_append("BALLERASER SCULPT TRACE");
    BallEraser::sculpt_log_append("log_path: " + BallEraser::sculpt_log_file_path());

    {
        std::ostringstream details;
        details << "session_active=" << (m_session.active ? "true" : "false")
                << ", pending_strokes=" << m_session.pending_strokes.size()
                << ", capture_active=" << (m_session.stroke_capture_active ? "true" : "false");
        BallEraser::ScopedSculptStepLog step(BallEraser::SculptApplyStep::FinalizePendingStrokeCapture, details.str());

        if (!m_session.active) {
            step.finish("aborted: BallEraser session is inactive");
            return;
        }

        if (m_session.stroke_capture_active && !m_session.in_progress_stroke_centers_instance_local.empty())
            commit_in_progress_stroke();

        if (m_session.pending_strokes.empty()) {
            step.finish("failed: no pending strokes remain after finalizing in-progress capture");
            show_ball_eraser_error("BallEraser needs at least one pending stroke before Apply.");
            return;
        }

        step.finish("pending stroke capture finalized; pending_strokes=" + std::to_string(m_session.pending_strokes.size()));
    }

    Plater *plater = wxGetApp().plater();
    ModelObject *source_object = nullptr;
    const ModelVolume *source_volume = nullptr;
    {
        BallEraser::ScopedSculptStepLog step(BallEraser::SculptApplyStep::ResolveSourceContext);
        if (plater == nullptr) {
            step.finish("aborted: plater is unavailable");
            return;
        }

        Model &model = plater->model();
        if (m_session.object_index < 0 || size_t(m_session.object_index) >= model.objects.size()) {
            step.finish("aborted: selected object index is outside the model object list");
            return;
        }

        source_object = model.objects[size_t(m_session.object_index)];
        if (source_object == nullptr) {
            step.finish("aborted: selected source object is null");
            return;
        }

        source_volume = find_single_ball_eraser_source_volume(*source_object);
        if (source_volume == nullptr) {
            step.finish("failed: could not resolve exactly one source model-part volume for appearance preservation");
            show_ball_eraser_error("BallEraser Sprint 4 currently supports a single source model-part volume for appearance preservation.");
            return;
        }

        std::ostringstream comments;
        comments << "source_object='" << source_object->name << "', source_volume='" << source_volume->name
                 << "', instance_index=" << m_session.instance_index;
        step.finish(comments.str());
    }

    // ---- STEP 1: Log source object state ----
    {
        BallEraser::ScopedSculptStepLog step(BallEraser::SculptApplyStep::LogSourceObjectState);
        log_source_object(*source_object, m_session.instance_index);
        step.finish("logged source object state for volume '" + source_volume->name + "'");
    }

    // ---- STEP 2: Build appearance snapshot ----
    ballerase_log_phase("STEP 2: BUILD APPEARANCE SNAPSHOT");
    ColorCut::ColorCutAppearanceSnapshotBuilder snapshot_builder;
    ColorCut::ObjectAppearanceSnapshot appearance_snapshot;
    {
        BallEraser::ScopedSculptStepLog step(BallEraser::SculptApplyStep::BuildAppearanceSnapshot);
        const bool bootstrapped_source_colors = bootstrap_repository_colors_from_render_info(*source_volume);
        if (bootstrapped_source_colors)
            ballerase_log("Source snapshot prep: seeded repository triangle colors from current painted render cache");
        appearance_snapshot = snapshot_builder.build(*source_object);
        log_appearance_snapshot(appearance_snapshot, *source_volume);
        step.finish("captured appearance snapshot for " + std::to_string(appearance_snapshot.volumes.size()) + " volume snapshot(s)");
    }

    // ---- STEP 3: Log pending strokes ----
    ballerase_log_phase("STEP 3: PENDING STROKES");
    {
        BallEraser::ScopedSculptStepLog step(BallEraser::SculptApplyStep::LogPendingStrokes);
        std::ostringstream s;
        s << "pending_strokes: " << m_session.pending_strokes.size()
          << "\noutput_mode: " << (m_session.output_mode == BallEraser::OutputMode::GroupedVolumes ? "GroupedVolumes" : "SeparateObjects")
          << "\nsingle_cut_surface_color: " << (m_single_cut_surface_color ? "YES" : "NO");
        if (m_single_cut_surface_color) {
            s << "\n  cut_color RGBA=(" << m_cut_surface_color.r() << ", " << m_cut_surface_color.g()
              << ", " << m_cut_surface_color.b() << ", " << m_cut_surface_color.a() << ")"
              << "\n  encoded_color='" << encode_color(m_cut_surface_color) << "'";
        }
        for (size_t i = 0; i < m_session.pending_strokes.size(); ++i) {
            const auto &stroke = m_session.pending_strokes[i];
            s << "\n  stroke[" << i << "] primitive=" << (stroke.primitive == BallEraser::PrimitiveType::Sphere ? "Sphere" : "Cube")
              << " gesture=" << (stroke.gesture == BallEraser::Stroke::GestureType::Drag ? "Drag" : "Click")
              << " samples=" << stroke.placement_count()
              << " size=(" << stroke.dimensions.x << "," << stroke.dimensions.y << "," << stroke.dimensions.z << ")";
            if (!stroke.empty())
                s << " first_center=" << be_format_vec3(stroke.first_center_instance_local());
        }
        ballerase_log(s.str());
        step.finish("logged " + std::to_string(m_session.pending_strokes.size()) + " pending stroke(s) and current output settings");
    }

    // ---- STEP 4: Execute boolean subtraction ----
    ballerase_log_phase("STEP 4: BOOLEAN SUBTRACTION");
    wxBusyCursor wait;

    std::optional<TriangleColor> cut_surface_color_binding;
    {
        BallEraser::ScopedSculptStepLog step(BallEraser::SculptApplyStep::RegisterResidualSurfaceColorBinding,
            std::string("single_cut_surface_color=") + (m_single_cut_surface_color ? "true" : "false"));
        if (m_single_cut_surface_color) {
            cut_surface_color_binding = register_cut_surface_color_binding();
            std::ostringstream comments;
            comments << "registered binding pid=" << cut_surface_color_binding->pid
                     << " indices=[" << cut_surface_color_binding->indices[0] << ","
                     << cut_surface_color_binding->indices[1] << "," << cut_surface_color_binding->indices[2] << "]";
            step.finish(comments.str());
        } else {
            step.finish("skipped: single residual surface color is disabled");
        }
    }

    if (cut_surface_color_binding.has_value()) {
        ballerase_log("cut_surface_color_binding: pid=" + std::to_string(cut_surface_color_binding->pid)
            + " indices=[" + std::to_string(cut_surface_color_binding->indices[0])
            + "," + std::to_string(cut_surface_color_binding->indices[1])
            + "," + std::to_string(cut_surface_color_binding->indices[2]) + "]");
    }

    auto run_boolean_and_transfer = [&](BallEraser::OutputMode output_mode, const char *label) -> BallEraser::ApplyResult {
        BallEraser::ApplyResult attempt = BallEraser::apply_strokes_to_object(
            *source_object,
            m_session.instance_index,
            output_mode,
            m_session.pending_strokes);
        if (!attempt.success) {
            ballerase_log(std::string("BOOLEAN FAILED (") + label + "): " + attempt.error_message);
            return attempt;
        }

        ballerase_log(std::string("BOOLEAN SUCCESS (") + label + "): " + std::to_string(attempt.new_objects.size()) + " output objects");
        log_result_objects(attempt.new_objects, (std::string("AFTER BOOLEAN SUBTRACTION: ") + label).c_str());

        {
            BallEraser::ScopedSculptStepLog step(BallEraser::SculptApplyStep::CompareCoordinateSpaces,
                "target_objects=" + std::to_string(attempt.new_objects.size()));
            log_coordinate_space_comparison(*source_object, *source_volume, m_session.instance_index, attempt.new_objects);
            step.finish("logged source/target coordinate-space comparison for " + std::to_string(attempt.new_objects.size()) + " output object(s)");
        }

        ballerase_log_phase(std::string("STEP 6+7: LOGIC.MD COLOR TRANSFER: ") + label);
        const std::string cut_surface_mmu_tag = cut_surface_color_binding.has_value()
            ? nearest_filament_mmu_tag_for_color(m_cut_surface_color)
            : std::string();

        BallEraser::ScopedSculptStepLog step_reapply(BallEraser::SculptApplyStep::ReapplyPreservedAppearance,
            "target_objects=" + std::to_string(attempt.new_objects.size()));
        BallEraser::ScopedSculptStepLog step_simplify(BallEraser::SculptApplyStep::SimplifyMmuSegmentation);
        BallEraser::ScopedSculptStepLog step_mark(BallEraser::SculptApplyStep::MarkResidualSurfaceTriangles);
        BallEraser::ScopedSculptStepLog step_recolor(BallEraser::SculptApplyStep::RecolorResidualSurfaceTriangles);

        apply_logic_md_color_transfer(
            *source_object, *source_volume, m_session.instance_index,
            attempt.new_objects, cut_surface_mmu_tag);

        step_reapply.finish("logic.md exact geometry transfer complete");
        step_simplify.finish("handled by logic.md transfer (no separate simplification needed)");
        step_mark.finish("handled by logic.md transfer (no heuristic marking needed)");
        step_recolor.finish("handled by logic.md transfer (cut surface tag assigned directly)");

        log_result_objects(attempt.new_objects, (std::string("AFTER LOGIC.MD COLOR TRANSFER: ") + label).c_str());
        return attempt;
    };

    BallEraser::ApplyResult result = run_boolean_and_transfer(m_session.output_mode, "requested output");
    if (!result.success) {
        show_ball_eraser_error(result.error_message);
        return;
    }

    ballerase_log_phase("STEP 8: VALIDATE RESULT");
    BallEraserValidationResult validation_result;
    {
        BallEraser::ScopedSculptStepLog step(BallEraser::SculptApplyStep::ValidateResultObjects,
            "target_objects=" + std::to_string(result.new_objects.size()));
        validation_result = validate_ball_eraser_result(result.new_objects);
        if (!validation_result.message.empty()) {
            ballerase_log("VALIDATION FAILED: " + validation_result.message);
            step.finish("failed: " + validation_result.message);
        } else {
            step.finish("result objects passed null, volume, and manifold validation");
        }
    }

    if (!validation_result.message.empty()) {
        if (!validation_result.can_try_split_fallback || m_session.output_mode != BallEraser::OutputMode::GroupedVolumes || !confirm_ball_eraser_split_result(validation_result.message)) {
            show_ball_eraser_error(validation_result.message);
            return;
        }

        ballerase_log("User accepted split fallback after validation failure: " + validation_result.message);
        result = run_boolean_and_transfer(BallEraser::OutputMode::SeparateObjects, "split fallback");
        if (!result.success) {
            show_ball_eraser_error(result.error_message);
            return;
        }

        BallEraser::ScopedSculptStepLog step(BallEraser::SculptApplyStep::ValidateResultObjects,
            "split_fallback_target_objects=" + std::to_string(result.new_objects.size()));
        validation_result = validate_ball_eraser_result(result.new_objects);
        if (!validation_result.message.empty()) {
            ballerase_log("SPLIT VALIDATION FAILED: " + validation_result.message);
            step.finish("failed: " + validation_result.message);
            show_ball_eraser_error(validation_result.message);
            return;
        }
        step.finish("split fallback result objects passed null, volume, and manifold validation");
    }
    ballerase_log("VALIDATION PASSED");

    // ---- STEP 9: Commit to model ----
    ballerase_log_phase("STEP 9: COMMIT TO MODEL");
    {
        BallEraser::ScopedSculptStepLog step(BallEraser::SculptApplyStep::CommitModelUpdate,
            "target_objects=" + std::to_string(result.new_objects.size()));
        const CutObjectBase cut_id = source_object->cut_id;
        Plater::TakeSnapshot snapshot(plater, "Ball erase");
        plater->apply_cut_object_to_model(size_t(m_session.object_index), result.new_objects);
        synchronize_model_after_cut(plater->model(), cut_id);
        const bool rebuilt_render_cache = rebuild_model_render_cache_from_repository(plater->model());
        ballerase_log(std::string("render_cache_rebuilt_from_repository: ") + (rebuilt_render_cache ? "YES" : "NO"));
        plater->update(false, true);
        step.finish("committed boolean result back to the model and refreshed the plater");
    }
    ballerase_log("COMMIT COMPLETE");

    ballerase_log_phase("BALLERASER APPLY FINISHED SUCCESSFULLY");
    {
        BallEraser::ScopedSculptStepLog step(BallEraser::SculptApplyStep::CloseTool);
        close();
        step.finish("BallEraser closed after successful sculpt apply");
    }
}

TriangleColor GLGizmoBallEraser::register_cut_surface_color_binding() const
{
    auto &repository = ColorCut::global_color_cut_attribute_repository();
    auto color_group_map = repository.get_color_group_map();
    const std::string encoded_color = encode_color(m_cut_surface_color);

    for (const auto &entry : color_group_map) {
        for (size_t index = 0; index < entry.second.size(); ++index) {
            if (entry.second[index] == encoded_color)
                return make_triangle_color_binding(entry.first, static_cast<int>(index));
        }
    }

    int next_pid = 1;
    for (const auto &entry : color_group_map)
        next_pid = std::max(next_pid, entry.first + 1);

    color_group_map[next_pid] = { encoded_color };
    repository.register_color_group_map(std::move(color_group_map));
    return make_triangle_color_binding(next_pid, 0);
}

void GLGizmoBallEraser::add_pending_stroke()
{
    if (!m_session.active)
        return;

    BallEraser::Stroke stroke = build_stroke_from_samples(
        { m_session.active_center_instance_local },
        BallEraser::Stroke::GestureType::Discrete);
    stroke.sequence_index = m_session.pending_strokes.size();
    m_session.pending_strokes.emplace_back(std::move(stroke));
    request_preview_refresh();
}

void GLGizmoBallEraser::undo_pending_stroke()
{
    if (!m_session.pending_strokes.empty()) {
        m_session.pending_strokes.pop_back();
        request_preview_refresh();
    }
}

void GLGizmoBallEraser::clear_pending_strokes()
{
    clear_in_progress_stroke();
    m_session.pending_strokes.clear();
    request_preview_refresh();
}

void GLGizmoBallEraser::data_changed(bool)
{
    if (get_state() == On) {
        reset_session_from_selection();
        refresh_preview_state();
    }
}

void GLGizmoBallEraser::on_set_state()
{
    if (get_state() == On) {
        reset_session_from_selection();
        if (!m_session.active) {
            m_parent.reset_all_gizmos();
            return;
        }
        refresh_preview_state();
    } else if (get_state() == Off) {
        restore_preview_state();
        m_session.reset();
    }
}

void GLGizmoBallEraser::on_render()
{
    if (!m_session.active)
        return;

    if (m_session.preview_dirty)
        rebuild_preview_models();

    const Camera& camera = wxGetApp().plater()->get_camera();
    const auto& view_matrix = camera.get_view_matrix();
    const auto& projection_matrix = camera.get_projection_matrix();

    const auto render_placement = [&](const BallEraser::PrimitiveType primitive,
                                      const BallEraser::Dimensions &dimensions,
                                      const Vec3d &rotation_degrees,
                                      const Vec3d &center_instance_local,
                                      const std::array<float, 4> &color,
                                      float emission) {
        const Vec3d world_center = instance_local_to_world(center_instance_local);
        const Transform3d model_matrix = ball_eraser_primitive_transform(
            primitive,
            dimensions,
            world_center,
            rotation_degrees);
        GLModel &model = primitive == BallEraser::PrimitiveType::Sphere ? m_sphere_preview : m_cube_preview;
        render_glmodel(model, color, view_matrix * model_matrix, projection_matrix, false, emission);
    };

    const auto render_stroke = [&](const BallEraser::Stroke &stroke,
                                   const std::array<float, 4> &color,
                                   float emission) {
        for (const Vec3d &center_instance_local : stroke.placement_centers_instance_local)
            render_placement(stroke.primitive, stroke.dimensions, stroke.rotation_degrees, center_instance_local, color, emission);
    };

    for (const BallEraser::Stroke &stroke : m_session.pending_strokes)
        render_stroke(stroke, { 0.12f, 0.65f, 0.74f, 0.22f }, 0.15f);

    if (!m_session.in_progress_stroke_centers_instance_local.empty()) {
        const BallEraser::Stroke in_progress = build_stroke_from_samples(
            m_session.in_progress_stroke_centers_instance_local,
            m_session.pointer_dragged ? BallEraser::Stroke::GestureType::Drag : BallEraser::Stroke::GestureType::Discrete);
        render_stroke(in_progress, { 0.96f, 0.62f, 0.14f, 0.24f }, 0.22f);
    }

    if (!m_session.stroke_capture_active)
        render_placement(m_session.primitive,
                         active_dimensions(),
                         active_rotation_degrees(),
                         m_session.active_center_instance_local,
                         { 0.96f, 0.43f, 0.14f, 0.32f },
                         0.28f);
}

void GLGizmoBallEraser::on_render_input_window(float x, float y, float bottom_limit)
{
    static float last_y = 0.0f;
    static float last_h = 0.0f;

    const float win_h = ImGui::GetWindowHeight();
    y = std::min(y, bottom_limit - win_h);
    GizmoImguiSetNextWIndowPos(x, y, ImGuiCond_Always, 0.0f, 0.0f);
    if (last_h != win_h || last_y != y) {
        m_imgui->set_requires_extra_frame();
        last_h = win_h;
        last_y = y;
    }

    ImGuiWrapper::push_toolbar_style(m_parent.get_scale());
    GizmoImguiBegin(on_get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    if (!m_session.active) {
        ImGui::TextUnformatted(_u8L("Please select single object.").c_str());
    } else {
        const char *primitive_items[] = { "Sphere", "Cube" };
        int primitive_idx = m_session.primitive == BallEraser::PrimitiveType::Sphere ? 0 : 1;
        if (ImGui::Combo("Primitive", &primitive_idx, primitive_items, IM_ARRAYSIZE(primitive_items)))
        {
            m_session.primitive = primitive_idx == 0 ? BallEraser::PrimitiveType::Sphere : BallEraser::PrimitiveType::Cube;
            mark_preview_dirty();
        }

        int output_idx = m_session.output_mode == BallEraser::OutputMode::GroupedVolumes ? 0 : 1;
        const char *output_items[] = { "Grouped volumes", "Separate objects" };
        if (ImGui::Combo("Result", &output_idx, output_items, IM_ARRAYSIZE(output_items)))
            m_session.output_mode = output_idx == 0 ? BallEraser::OutputMode::GroupedVolumes : BallEraser::OutputMode::SeparateObjects;

        m_imgui->bbl_checkbox(_L("Single residual surface color"), m_single_cut_surface_color);
        if (m_single_cut_surface_color) {
            float cut_surface_color[3] = {
                m_cut_surface_color.r(),
                m_cut_surface_color.g(),
                m_cut_surface_color.b()
            };
            if (ImGui::ColorEdit3("Residual Surface", cut_surface_color)) {
                m_cut_surface_color.r(cut_surface_color[0]);
                m_cut_surface_color.g(cut_surface_color[1]);
                m_cut_surface_color.b(cut_surface_color[2]);
                m_cut_surface_color.a(1.0f);
            }
        }

        ImGui::Separator();
        if (m_session.primitive == BallEraser::PrimitiveType::Sphere) {
            ImGui::TextUnformatted("Sphere Size");
            ImGui::SetNextItemWidth(170.0f * m_parent.get_scale());
            if (ImGui::InputDouble("Diameter", &m_sphere_dimensions.x, 1.0, 10.0, "%.2f"))
                mark_preview_dirty();
            m_sphere_dimensions.x = std::max(1.0, m_sphere_dimensions.x);
            m_sphere_dimensions.y = m_sphere_dimensions.x;
            m_sphere_dimensions.z = m_sphere_dimensions.x;
        } else {
            ImGui::TextUnformatted("Cube Size");
            ImGui::SetNextItemWidth(170.0f * m_parent.get_scale());
            if (ImGui::InputDouble("Size X", &m_cube_dimensions.x, 1.0, 10.0, "%.2f"))
                mark_preview_dirty();
            ImGui::SetNextItemWidth(170.0f * m_parent.get_scale());
            if (ImGui::InputDouble("Size Y", &m_cube_dimensions.y, 1.0, 10.0, "%.2f"))
                mark_preview_dirty();
            ImGui::SetNextItemWidth(170.0f * m_parent.get_scale());
            if (ImGui::InputDouble("Size Z", &m_cube_dimensions.z, 1.0, 10.0, "%.2f"))
                mark_preview_dirty();
            m_cube_dimensions.x = std::max(1.0, m_cube_dimensions.x);
            m_cube_dimensions.y = std::max(1.0, m_cube_dimensions.y);
            m_cube_dimensions.z = std::max(1.0, m_cube_dimensions.z);

            ImGui::Separator();
            ImGui::TextUnformatted("Cube Rotation (deg)");
            double rotation_x = m_cube_rotation_degrees.x();
            double rotation_y = m_cube_rotation_degrees.y();
            double rotation_z = m_cube_rotation_degrees.z();
            ImGui::SetNextItemWidth(170.0f * m_parent.get_scale());
            if (ImGui::InputDouble("Rotation X", &rotation_x, 1.0, 10.0, "%.2f")) {
                m_cube_rotation_degrees.x() = rotation_x;
                mark_preview_dirty();
            }
            ImGui::SetNextItemWidth(170.0f * m_parent.get_scale());
            if (ImGui::InputDouble("Rotation Y", &rotation_y, 1.0, 10.0, "%.2f")) {
                m_cube_rotation_degrees.y() = rotation_y;
                mark_preview_dirty();
            }
            ImGui::SetNextItemWidth(170.0f * m_parent.get_scale());
            if (ImGui::InputDouble("Rotation Z", &rotation_z, 1.0, 10.0, "%.2f")) {
                m_cube_rotation_degrees.z() = rotation_z;
                mark_preview_dirty();
            }
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Active Position");
        double center_x = m_session.active_center_instance_local.x();
        double center_y = m_session.active_center_instance_local.y();
        double center_z = m_session.active_center_instance_local.z();
        ImGui::SetNextItemWidth(170.0f * m_parent.get_scale());
        if (ImGui::InputDouble("Center X", &center_x, 1.0, 10.0, "%.2f")) {
            m_session.active_center_instance_local.x() = center_x;
            mark_preview_dirty();
        }
        ImGui::SetNextItemWidth(170.0f * m_parent.get_scale());
        if (ImGui::InputDouble("Center Y", &center_y, 1.0, 10.0, "%.2f")) {
            m_session.active_center_instance_local.y() = center_y;
            mark_preview_dirty();
        }
        ImGui::SetNextItemWidth(170.0f * m_parent.get_scale());
        if (ImGui::InputDouble("Center Z", &center_z, 1.0, 10.0, "%.2f")) {
            m_session.active_center_instance_local.z() = center_z;
            mark_preview_dirty();
        }
        if (ImGui::Button("Center To Object")) {
            m_session.active_center_instance_local = current_stroke_center_instance_local();
            mark_preview_dirty();
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Click or drag on the edited object to queue erase strokes.");
        ImGui::TextUnformatted("Click empty space to close or confirm pending edits.");
        ImGui::Text("Pending strokes: %d", int(m_session.pending_strokes.size()));
        if (!m_session.in_progress_stroke_centers_instance_local.empty())
            ImGui::Text("In-progress samples: %d", int(m_session.in_progress_stroke_centers_instance_local.size()));

        ImGui::Separator();
        if (ImGui::Button("Add Stroke"))
            add_pending_stroke();
        ImGui::SameLine();
        if (ImGui::Button("Undo Stroke"))
            undo_pending_stroke();
        ImGui::SameLine();
        if (ImGui::Button("Clear All"))
            clear_pending_strokes();

        if (ImGui::Button("Cancel")) {
            prompt_for_exit_if_needed();
        }

        if (ImGui::Button("Apply"))
            apply_pending_strokes();

        if (!m_session.pending_strokes.empty()) {
            ImGui::Separator();
            ImGui::TextUnformatted("Pending Stroke Queue");
            const int shown = std::min<int>(int(m_session.pending_strokes.size()), 8);
            for (int i = 0; i < shown; ++i) {
                const auto &stroke = m_session.pending_strokes[size_t(i)];
                const char *stroke_type = stroke.primitive == BallEraser::PrimitiveType::Sphere ? "Sphere" : "Cube";
                const char *gesture = stroke.gesture == BallEraser::Stroke::GestureType::Drag ? "Drag" : "Click";
                const Vec3d &first_center = stroke.first_center_instance_local();
                ImGui::BulletText("#%d %s %s samples=%d start=(%.1f, %.1f, %.1f) size=(%.1f, %.1f, %.1f)",
                    int(stroke.sequence_index + 1),
                    gesture,
                    stroke_type,
                    int(stroke.placement_count()),
                    first_center.x(), first_center.y(), first_center.z(),
                    stroke.dimensions.x, stroke.dimensions.y, stroke.dimensions.z);
            }
        }

        ImGui::Separator();
        ImGui::TextWrapped("Current limitation: BallEraser still relies on the single source model-part appearance-transfer path for surviving-color preservation after Apply.");
    }

    GizmoImguiEnd();
    ImGuiWrapper::pop_toolbar_style();
}

} // namespace GUI
} // namespace Slic3r
