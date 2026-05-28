#include "BallEraserBackend.hpp"

#include "ag/ballerase/BallEraserBoolean.hpp"
#include "ag/ballerase/BallEraserStepLogger.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/SLA/IndexedMesh.hpp"
#include "libslic3r/MeshBoolean.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <unordered_map>

namespace Slic3r {
namespace BallEraser {

namespace {

constexpr const char *kBooleanUnion = "UNION";
constexpr const char *kBooleanSubtract = "A_NOT_B";
constexpr double kBackendMinimumSampleSpacing = 0.75;
constexpr double kBackendSampleSpacingRatio = 0.85;

using ModelPartVolumes = std::vector<const ModelVolume *>;

struct BackendComponent {
    TriangleMesh         mesh;
    std::vector<uint8_t> cut_face_mask;
};

std::vector<TriangleMesh> split_components(TriangleMesh mesh);
std::vector<BackendComponent> collect_boolean_components(std::vector<TriangleMesh> meshes);

std::string quantized_vertex_key(const Vec3d &vertex)
{
    constexpr double scale = 1000000.0;
    std::ostringstream stream;
    stream << std::llround(vertex.x() * scale) << ','
           << std::llround(vertex.y() * scale) << ','
           << std::llround(vertex.z() * scale);
    return stream.str();
}

std::string triangle_geometry_key(const indexed_triangle_set &mesh, int triangle_index)
{
    const stl_triangle_vertex_indices &triangle = mesh.indices[static_cast<size_t>(triangle_index)];
    std::array<std::string, 3> vertices = {
        quantized_vertex_key(mesh.vertices[triangle(0)].cast<double>()),
        quantized_vertex_key(mesh.vertices[triangle(1)].cast<double>()),
        quantized_vertex_key(mesh.vertices[triangle(2)].cast<double>())
    };
    std::sort(vertices.begin(), vertices.end());
    return vertices[0] + "|" + vertices[1] + "|" + vertices[2];
}

std::vector<std::string> build_sorted_triangle_geometry_keys(const TriangleMesh &mesh)
{
    std::vector<std::string> keys;
    keys.reserve(mesh.its.indices.size());
    for (size_t triangle_index = 0; triangle_index < mesh.its.indices.size(); ++triangle_index)
        keys.push_back(triangle_geometry_key(mesh.its, static_cast<int>(triangle_index)));
    std::sort(keys.begin(), keys.end());
    return keys;
}

std::unordered_map<std::string, int> build_triangle_geometry_lookup(const TriangleMesh &mesh)
{
    std::unordered_map<std::string, int> lookup;
    lookup.reserve(mesh.its.indices.size());
    for (size_t triangle_index = 0; triangle_index < mesh.its.indices.size(); ++triangle_index)
        lookup.emplace(triangle_geometry_key(mesh.its, static_cast<int>(triangle_index)), static_cast<int>(triangle_index));
    return lookup;
}

void try_attach_provenance_masks(
    std::vector<BackendComponent> &geometry_components,
    std::vector<FaceOriginTriangleMesh> &provenance_components)
{
    if (geometry_components.empty() || provenance_components.empty())
        return;

    std::vector<std::vector<std::string>> provenance_keys;
    provenance_keys.reserve(provenance_components.size());
    for (const auto &component : provenance_components)
        provenance_keys.push_back(build_sorted_triangle_geometry_keys(component.mesh));

    std::vector<bool> provenance_used(provenance_components.size(), false);
    for (BackendComponent &geometry_component : geometry_components) {
        const std::vector<std::string> geometry_keys = build_sorted_triangle_geometry_keys(geometry_component.mesh);
        size_t matched_index = provenance_components.size();
        for (size_t provenance_index = 0; provenance_index < provenance_components.size(); ++provenance_index) {
            if (provenance_used[provenance_index])
                continue;
            if (geometry_component.mesh.its.indices.size() != provenance_components[provenance_index].mesh.its.indices.size())
                continue;
            if (geometry_keys == provenance_keys[provenance_index]) {
                matched_index = provenance_index;
                break;
            }
        }

        if (matched_index >= provenance_components.size())
            continue;

        const auto geometry_lookup = build_triangle_geometry_lookup(geometry_component.mesh);
        std::vector<uint8_t> cut_face_mask(geometry_component.mesh.its.indices.size(), 0);
        const auto &provenance_component = provenance_components[matched_index];
        bool complete_match = true;
        for (size_t triangle_index = 0; triangle_index < provenance_component.mesh.its.indices.size(); ++triangle_index) {
            const auto it = geometry_lookup.find(triangle_geometry_key(provenance_component.mesh.its, static_cast<int>(triangle_index)));
            if (it == geometry_lookup.end()) {
                complete_match = false;
                break;
            }
            cut_face_mask[static_cast<size_t>(it->second)] = provenance_component.cut_face_mask[triangle_index];
        }

        if (!complete_match)
            continue;

        geometry_component.cut_face_mask = std::move(cut_face_mask);
        provenance_used[matched_index] = true;
    }
}

std::string summarize_backend_components(const std::vector<BackendComponent> &components)
{
    std::ostringstream stream;
    stream << "count=" << components.size();
    for (size_t index = 0; index < components.size(); ++index) {
        stream << " [" << index
               << ":tris=" << components[index].mesh.its.indices.size()
               << ",mask=" << components[index].cut_face_mask.size()
               << "]";
    }
    return stream.str();
}

bool object_has_supported_geometry(const ModelObject &object)
{
    if (object.instances.size() != 1)
        return false;

    size_t model_part_count = 0;

    for (const ModelVolume *volume : object.volumes) {
        if (volume == nullptr)
            continue;
        if (!volume->is_model_part() || volume->is_cut_connector())
            return false;
        ++model_part_count;
    }

    return model_part_count == 1;
}

ModelPartVolumes collect_model_part_volumes(const ModelObject &object)
{
    ModelPartVolumes volumes;
    volumes.reserve(object.volumes.size());
    for (const ModelVolume *volume : object.volumes) {
        if (volume != nullptr && volume->is_model_part() && !volume->is_cut_connector() && !volume->mesh().empty())
            volumes.push_back(volume);
    }
    return volumes;
}

void cleanup_mesh(TriangleMesh &mesh)
{
    if (mesh.empty())
        return;

    const RepairedMeshErrors repaired_errors = mesh.stats().repaired_errors;
    its_remove_degenerate_faces(mesh.its);
    its_merge_vertices(mesh.its);
    its_compactify_vertices(mesh.its);

    indexed_triangle_set cleaned = std::move(mesh.its);
    mesh = TriangleMesh(std::move(cleaned), repaired_errors);
}

stl_file mesh_to_stl(const TriangleMesh &mesh)
{
    stl_file stl;
    stl.stats.type = inmemory;
    stl.stats.number_of_facets = uint32_t(mesh.its.indices.size());
    stl.stats.original_num_facets = int(mesh.its.indices.size());
    stl_allocate(&stl);

    bool first = true;
    for (size_t face_index = 0; face_index < mesh.its.indices.size(); ++face_index) {
        const Vec3i &face = mesh.its.indices[face_index];
        stl_facet facet{};
        facet.vertex[0] = mesh.its.vertices[size_t(face[0])];
        facet.vertex[1] = mesh.its.vertices[size_t(face[1])];
        facet.vertex[2] = mesh.its.vertices[size_t(face[2])];
        facet.normal = its_face_normal(mesh.its, int(face_index));
        facet.extra[0] = 0;
        facet.extra[1] = 0;
        stl.facet_start[face_index] = facet;
        stl_facet_stats(&stl, facet, first);
    }

    return stl;
}

bool mesh_requires_repair(const TriangleMesh &mesh)
{
    if (mesh.empty())
        return false;
    if (!mesh.stats().manifold())
        return true;

    try {
        return MeshBoolean::cgal::does_self_intersect(mesh);
    } catch (...) {
        return true;
    }
}

bool try_repair_mesh(TriangleMesh &mesh)
{
    cleanup_mesh(mesh);
    if (mesh.empty() || !mesh_requires_repair(mesh))
        return !mesh.empty();

    TriangleMesh repaired;
    stl_file stl = mesh_to_stl(mesh);
    if (!repaired.from_stl(stl, true))
        return false;

    cleanup_mesh(repaired);
    if (repaired.empty())
        return false;

    mesh = std::move(repaired);
    return !mesh_requires_repair(mesh);
}

void preserve_cut_face_mask_or_clear(BackendComponent &component, size_t expected_face_count)
{
    if (component.cut_face_mask.size() != expected_face_count || component.mesh.its.indices.size() != expected_face_count)
        component.cut_face_mask.clear();
}

bool try_repair_component(BackendComponent &component)
{
    const size_t original_face_count = component.mesh.its.indices.size();
    const bool repaired = try_repair_mesh(component.mesh);
    preserve_cut_face_mask_or_clear(component, original_face_count);
    return repaired;
}

double stroke_sample_spacing(const Stroke &stroke)
{
    const double base_dimension = stroke.primitive == PrimitiveType::Sphere
        ? stroke.dimensions.x
        : std::min({ stroke.dimensions.x, stroke.dimensions.y, stroke.dimensions.z });
    return std::max(kBackendMinimumSampleSpacing, base_dimension * kBackendSampleSpacingRatio);
}

Vec3d stroke_rotation_radians(const Vec3d &rotation_degrees)
{
    return Vec3d(
        Geometry::deg2rad(rotation_degrees.x()),
        Geometry::deg2rad(rotation_degrees.y()),
        Geometry::deg2rad(rotation_degrees.z()));
}

Transform3d primitive_transform(
    PrimitiveType primitive,
    const Dimensions &dimensions,
    const Vec3d &center_instance_local,
    const Vec3d &rotation_degrees)
{
    const Vec3d scale = primitive == PrimitiveType::Sphere
        ? Vec3d::Constant(dimensions.x)
        : Vec3d(dimensions.x, dimensions.y, dimensions.z);
    const Transform3d local_centering = primitive == PrimitiveType::Sphere
        ? Transform3d::Identity()
        : Geometry::translation_transform(Vec3d(-0.5, -0.5, -0.5));
    const Transform3d local_rotation = primitive == PrimitiveType::Sphere
        ? Transform3d::Identity()
        : Geometry::rotation_transform(stroke_rotation_radians(rotation_degrees));
    return Geometry::translation_transform(center_instance_local)
        * local_rotation
        * Geometry::scale_transform(scale)
        * local_centering;
}

std::vector<Vec3d> simplify_stroke_placements(const Stroke &stroke)
{
    if (stroke.placement_centers_instance_local.empty())
        return {};

    std::vector<Vec3d> simplified;
    simplified.reserve(stroke.placement_centers_instance_local.size());
    simplified.push_back(stroke.placement_centers_instance_local.front());

    const double min_spacing = stroke_sample_spacing(stroke);
    for (size_t index = 1; index < stroke.placement_centers_instance_local.size(); ++index) {
        const bool is_last = index + 1 == stroke.placement_centers_instance_local.size();
        const Vec3d &sample = stroke.placement_centers_instance_local[index];
        if (!is_last) {
            const Vec3d delta = sample - simplified.back();
            if (delta.norm() < min_spacing)
                continue;
        }
        simplified.push_back(sample);
    }

    return simplified;
}

TriangleMesh build_source_mesh(const ModelObject &object, int instance_index, const ModelPartVolumes &volumes)
{
    TriangleMesh merged;
    const ModelInstance *instance = object.instances[size_t(instance_index)];
    const Transform3d instance_matrix = instance->get_transformation().get_matrix_no_offset();

    for (const ModelVolume *volume : volumes) {
        TriangleMesh mesh(volume->mesh());
        mesh.transform(instance_matrix * volume->get_matrix(), true);
        merged.merge(mesh);
    }

    cleanup_mesh(merged);
    return merged;
}

TriangleMesh build_stroke_sample_mesh(
    PrimitiveType primitive,
    const Dimensions &dimensions,
    const Vec3d &center_instance_local,
    const Vec3d &rotation_degrees)
{
    TriangleMesh primitive_mesh = primitive == PrimitiveType::Sphere
        ? TriangleMesh(its_make_sphere(0.5, PI / 18.0))
        : TriangleMesh(its_make_cube(1.0, 1.0, 1.0));

    const Transform3d transform = primitive_transform(
        primitive,
        dimensions,
        center_instance_local,
        rotation_degrees);
    primitive_mesh.transform(transform, true);
    cleanup_mesh(primitive_mesh);
    return primitive_mesh;
}

TriangleMesh build_segment_mesh(
    PrimitiveType primitive,
    const Dimensions &dimensions,
    const Vec3d &rotation_degrees,
    const Vec3d &start_center,
    const Vec3d &end_center)
{
    TriangleMesh start_mesh = build_stroke_sample_mesh(primitive, dimensions, start_center, rotation_degrees);
    TriangleMesh end_mesh = build_stroke_sample_mesh(primitive, dimensions, end_center, rotation_degrees);

    TriangleMesh merged = start_mesh;
    merged.merge(end_mesh);
    cleanup_mesh(merged);

    TriangleMesh segment = merged.convex_hull_3d();
    cleanup_mesh(segment);
    if (segment.empty()) {
        std::vector<TriangleMesh> boolean_result;
        MeshBoolean::mcut::make_boolean(start_mesh, end_mesh, boolean_result, kBooleanUnion);
        if (!boolean_result.empty()) {
            segment = std::move(boolean_result.front());
            cleanup_mesh(segment);
        }
    }

    if (segment.empty())
        segment = std::move(merged);

    try_repair_mesh(segment);
    return segment;
}

std::vector<TriangleMesh> build_stroke_cutters(const Stroke &stroke)
{
    const std::vector<Vec3d> placements = simplify_stroke_placements(stroke);
    if (placements.empty())
        return {};

    if (placements.size() == 1)
        return { build_stroke_sample_mesh(stroke.primitive, stroke.dimensions, placements.front(), stroke.rotation_degrees) };

    std::vector<TriangleMesh> cutters;
    cutters.reserve(placements.size() - 1);
    for (size_t index = 1; index < placements.size(); ++index) {
        TriangleMesh segment = build_segment_mesh(
            stroke.primitive,
            stroke.dimensions,
            stroke.rotation_degrees,
            placements[index - 1],
            placements[index]);
        if (!segment.empty())
            cutters.emplace_back(std::move(segment));
    }

    if (cutters.empty())
        cutters.emplace_back(build_stroke_sample_mesh(stroke.primitive, stroke.dimensions, placements.front(), stroke.rotation_degrees));

    return cutters;
}

bool subtract_cutter_from_component(const BackendComponent &component, const TriangleMesh &cutter, std::vector<BackendComponent> &out_components)
{
    out_components.clear();

    try {
        std::vector<TriangleMesh> boolean_result;
        MeshBoolean::mcut::make_boolean(component.mesh, cutter, boolean_result, kBooleanSubtract);
        out_components = collect_boolean_components(std::move(boolean_result));
        if (!out_components.empty()) {
            size_t provenance_component_count = 0;
            try {
                std::vector<FaceOriginTriangleMesh> provenance_result;
                make_boolean_with_face_origin(component.mesh, cutter, provenance_result, kBooleanSubtract);
                provenance_component_count = provenance_result.size();
                try_attach_provenance_masks(out_components, provenance_result);
            } catch (...) {
            }
            sculpt_log_append(
                std::string("[TRACE] subtract component: input_tris=") + std::to_string(component.mesh.its.indices.size()) +
                ", cutter_tris=" + std::to_string(cutter.its.indices.size()) +
                ", provenance_components=" + std::to_string(provenance_component_count) +
                ", output=" + summarize_backend_components(out_components));
            return true;
        }
    } catch (...) {
    }

    try {
        TriangleMesh fallback(component.mesh);
        MeshBoolean::cgal::minus(fallback, cutter);
        out_components = collect_boolean_components({ std::move(fallback) });
        return !out_components.empty();
    } catch (...) {
    }

    return false;
}

std::vector<TriangleMesh> split_components(TriangleMesh mesh)
{
    std::vector<TriangleMesh> components;
    if (mesh.empty())
        return components;

    cleanup_mesh(mesh);
    std::vector<indexed_triangle_set> parts = its_split(mesh.its);
    components.reserve(parts.size());
    for (indexed_triangle_set &part : parts) {
        TriangleMesh component(std::move(part));
        cleanup_mesh(component);
        if (!component.empty())
            components.emplace_back(std::move(component));
    }

    std::sort(components.begin(), components.end(), [](const TriangleMesh &lhs, const TriangleMesh &rhs) {
        return lhs.its.indices.size() > rhs.its.indices.size();
    });
    return components;
}

std::vector<BackendComponent> collect_boolean_components(std::vector<TriangleMesh> meshes)
{
    std::vector<BackendComponent> components;
    for (TriangleMesh &mesh : meshes) {
        std::vector<TriangleMesh> split = split_components(std::move(mesh));
        for (TriangleMesh &component : split)
            components.push_back({ std::move(component), {} });
    }

    std::sort(components.begin(), components.end(), [](const BackendComponent &lhs, const BackendComponent &rhs) {
        return lhs.mesh.its.indices.size() > rhs.mesh.its.indices.size();
    });
    return components;
}

std::vector<BackendComponent> normalize_result_components(std::vector<BackendComponent> meshes, bool keep_unrepaired = false)
{
    std::vector<BackendComponent> normalized;
    for (BackendComponent &component : meshes) {
        if (component.mesh.empty())
            continue;

        const size_t cleaned_face_count = component.mesh.its.indices.size();
        cleanup_mesh(component.mesh);
        preserve_cut_face_mask_or_clear(component, cleaned_face_count);
        try_repair_component(component);
        if (component.mesh.empty())
            continue;

        if (!component.cut_face_mask.empty()) {
            normalized.emplace_back(std::move(component));
            continue;
        }

        std::vector<TriangleMesh> split = split_components(std::move(component.mesh));
        for (TriangleMesh &split_component : split) {
            cleanup_mesh(split_component);
            if (split_component.empty())
                continue;
            const bool repaired = try_repair_mesh(split_component);
            if (!repaired && !keep_unrepaired)
                continue;
            if (!split_component.empty())
                normalized.push_back({ std::move(split_component), {} });
        }
    }

    std::sort(normalized.begin(), normalized.end(), [](const BackendComponent &lhs, const BackendComponent &rhs) {
        return lhs.mesh.its.indices.size() > rhs.mesh.its.indices.size();
    });
    return normalized;
}

bool apply_cutter_sequence(std::vector<BackendComponent> &components, const std::vector<TriangleMesh> &cutters)
{
    for (const TriangleMesh &cutter : cutters) {
        if (cutter.empty())
            continue;

        std::vector<BackendComponent> next_components;
        for (const BackendComponent &component : components) {
            if (component.mesh.empty())
                continue;

            std::vector<BackendComponent> subtraction;
            if (!subtract_cutter_from_component(component, cutter, subtraction))
                return false;

            next_components.insert(next_components.end(),
                std::make_move_iterator(subtraction.begin()),
                std::make_move_iterator(subtraction.end()));
        }

        components = normalize_result_components(std::move(next_components), true);
        sculpt_log_append(std::string("[TRACE] post-normalize cutters: ") + summarize_backend_components(components));
        if (components.empty())
            return true;
    }

    return true;
}

void normalize_single_instance_clone(ModelObject &object)
{
    if (object.instances.empty())
        return;

    ModelInstance *instance = object.instances.front();
    const Vec3d offset = instance->get_offset();
    instance->set_transformation(Geometry::Transformation());
    instance->set_offset(offset);
}

void configure_output_volume(ModelVolume &volume, const ModelVolume &template_volume, const std::string &name)
{
    volume.set_type(ModelVolumeType::MODEL_PART);
    volume.name = name;
    volume.config.assign_config(template_volume.config);
    volume.set_material(template_volume.material_id(), *template_volume.material());
    volume.calculate_convex_hull();
}

bool component_has_exact_cut_mask(const BackendComponent &component)
{
    return !component.cut_face_mask.empty() && component.cut_face_mask.size() == component.mesh.its.indices.size();
}

void assign_exterior_facets_from_mask(ModelVolume &volume, const std::vector<uint8_t> &cut_face_mask)
{
    volume.exterior_facets.reset();
    volume.exterior_facets.reserve(static_cast<int>(cut_face_mask.size()));
    for (size_t face_index = 0; face_index < cut_face_mask.size(); ++face_index) {
        if (cut_face_mask[face_index] != 0)
            volume.exterior_facets.set_triangle_from_string(static_cast<int>(face_index), "1");
    }
    volume.exterior_facets.shrink_to_fit();
}

ModelObject *build_grouped_result(const ModelObject &source_object, const ModelVolume &template_volume, const std::vector<BackendComponent> &components)
{
    ModelObject *result_object = nullptr;
    const_cast<ModelObject &>(source_object).clone_for_cut(&result_object);
    normalize_single_instance_clone(*result_object);
    result_object->name = source_object.name;

    TriangleMesh grouped_mesh;
    std::vector<uint8_t> grouped_cut_face_mask;
    const bool has_exact_cut_mask = !components.empty() && std::all_of(components.begin(), components.end(), [](const BackendComponent &component) {
        return component_has_exact_cut_mask(component);
    });
    for (const BackendComponent &component : components) {
        grouped_mesh.merge(component.mesh);
        if (has_exact_cut_mask)
            grouped_cut_face_mask.insert(grouped_cut_face_mask.end(), component.cut_face_mask.begin(), component.cut_face_mask.end());
    }

    const size_t grouped_face_count = grouped_mesh.its.indices.size();
    cleanup_mesh(grouped_mesh);
    try_repair_mesh(grouped_mesh);

    if (!grouped_mesh.empty()) {
        ModelVolume *volume = result_object->add_volume(grouped_mesh);
        configure_output_volume(*volume, template_volume, source_object.name);
        if (has_exact_cut_mask && grouped_face_count == grouped_mesh.its.indices.size())
            assign_exterior_facets_from_mask(*volume, grouped_cut_face_mask);
    }

    result_object->sort_volumes(true);
    result_object->invalidate_bounding_box();
    return result_object;
}

ModelObjectPtrs build_split_results(const ModelObject &source_object, const ModelVolume &template_volume, const std::vector<BackendComponent> &components)
{
    ModelObjectPtrs result_objects;
    result_objects.reserve(components.size());

    for (size_t index = 0; index < components.size(); ++index) {
        ModelObject *result_object = nullptr;
        const_cast<ModelObject &>(source_object).clone_for_cut(&result_object);
        normalize_single_instance_clone(*result_object);
        result_object->name = source_object.name + "_part" + std::to_string(index + 1);

        ModelVolume *volume = result_object->add_volume(components[index].mesh);
        configure_output_volume(*volume, template_volume, result_object->name);
        if (component_has_exact_cut_mask(components[index]))
            assign_exterior_facets_from_mask(*volume, components[index].cut_face_mask);

        result_object->sort_volumes(true);
        result_object->invalidate_bounding_box();
        result_objects.push_back(result_object);
    }

    return result_objects;
}

} // namespace

ApplyResult apply_strokes_to_object(
    const ModelObject &source_object,
    int instance_index,
    OutputMode output_mode,
    const std::vector<Stroke> &strokes)
{
    ApplyResult result;

    {
        std::ostringstream details;
        details << "strokes=" << strokes.size()
                << ", instance_index=" << instance_index
                << ", output_mode=" << (output_mode == OutputMode::GroupedVolumes ? "GroupedVolumes" : "SeparateObjects");
        ScopedSculptStepLog step(SculptApplyStep::ValidateBooleanInputs, details.str());

        if (strokes.empty()) {
            result.error_message = "BallEraser needs at least one pending stroke before Apply.";
            step.finish("failed: no pending strokes were supplied to the backend");
            return result;
        }

        if (instance_index != 0 || !object_has_supported_geometry(source_object)) {
            result.error_message = "BallEraser Sprint 4 currently supports single-instance objects with exactly one model-part volume.";
            step.finish("failed: source object is outside the supported single-instance single-volume constraints");
            return result;
        }

        step.finish("validated backend preconditions for " + std::to_string(strokes.size()) + " pending strokes");
    }

    const ModelPartVolumes model_part_volumes = [&]() {
        ScopedSculptStepLog step(SculptApplyStep::CollectModelPartVolumes);
        const ModelPartVolumes volumes = collect_model_part_volumes(source_object);
        if (volumes.empty()) {
            result.error_message = "BallEraser could not find any supported model-part geometry to erase.";
            step.finish("failed: no non-empty supported model-part volumes were found on the source object");
        } else {
            step.finish("collected " + std::to_string(volumes.size()) + " supported model-part volume(s)");
        }
        return volumes;
    }();
    if (model_part_volumes.empty())
        return result;

    TriangleMesh source_mesh;
    {
        ScopedSculptStepLog step(SculptApplyStep::BuildSourceMesh);
        source_mesh = build_source_mesh(source_object, instance_index, model_part_volumes);
        if (source_mesh.empty()) {
            result.error_message = "BallEraser could not build the source mesh for boolean subtraction.";
            step.finish("failed: merged transformed source mesh is empty");
            return result;
        }

        std::ostringstream comments;
        comments << "source mesh built with " << source_mesh.its.indices.size()
                 << " triangles and " << source_mesh.its.vertices.size() << " vertices";
        step.finish(comments.str());
    }

    std::vector<BackendComponent> components{ { std::move(source_mesh), {} } };
    for (size_t stroke_index = 0; stroke_index < strokes.size(); ++stroke_index) {
        const Stroke &stroke = strokes[stroke_index];
        std::vector<TriangleMesh> cutters;
        {
            std::ostringstream details;
            details << "stroke_index=" << stroke_index
                    << ", samples=" << stroke.placement_count()
                    << ", primitive=" << (stroke.primitive == PrimitiveType::Sphere ? "Sphere" : "Cube");
            ScopedSculptStepLog step(SculptApplyStep::BuildStrokeCutters, details.str());
            cutters = build_stroke_cutters(stroke);
            if (cutters.empty()) {
                result.error_message = "BallEraser could not build a valid swept erase volume from one of the pending strokes.";
                step.finish("failed: stroke did not produce any valid cutter meshes");
                return result;
            }

            step.finish("generated " + std::to_string(cutters.size()) + " cutter mesh(es) for the stroke");
        }

        {
            std::ostringstream details;
            details << "stroke_index=" << stroke_index
                    << ", incoming_components=" << components.size()
                    << ", cutters=" << cutters.size();
            ScopedSculptStepLog step(SculptApplyStep::ApplyStrokeCutters, details.str());
            if (!apply_cutter_sequence(components, cutters)) {
                result.error_message = "BallEraser failed while subtracting a swept stroke from the model.";
                step.finish("failed: boolean subtraction returned an invalid component set");
                return result;
            }

            if (components.empty()) {
                result.error_message = "BallEraser erased the entire object or produced no valid remaining geometry.";
                step.finish("failed: subtraction removed all remaining components");
                return result;
            }

            step.finish("subtraction complete; remaining components=" + std::to_string(components.size()));
        }
    }

    {
        ScopedSculptStepLog step(SculptApplyStep::NormalizeBooleanResult,
            "pre_normalize_components=" + std::to_string(components.size()));
        components = normalize_result_components(std::move(components), true);
        if (components.empty()) {
            result.error_message = "BallEraser could not extract or repair valid result components after subtraction.";
            step.finish("failed: normalization/repair produced no valid components");
            return result;
        }

        step.finish("normalized result down to " + std::to_string(components.size()) + " component(s)");
    }

    {
        ScopedSculptStepLog step(SculptApplyStep::BuildOutputObjects,
            std::string("output_mode=") + (output_mode == OutputMode::GroupedVolumes ? "GroupedVolumes" : "SeparateObjects"));
        if (output_mode == OutputMode::GroupedVolumes) {
            ModelObject *grouped = build_grouped_result(source_object, *model_part_volumes.front(), components);
            if (grouped != nullptr)
                result.new_objects.push_back(grouped);
        } else {
            result.new_objects = build_split_results(source_object, *model_part_volumes.front(), components);
        }

        if (result.new_objects.empty()) {
            result.error_message = "BallEraser could not build output objects from the boolean result.";
            step.finish("failed: no output objects were created from the remaining components");
            return result;
        }

        step.finish("built " + std::to_string(result.new_objects.size()) + " output object(s)");
    }

    result.success = true;
    return result;
}

} // namespace BallEraser
} // namespace Slic3r
