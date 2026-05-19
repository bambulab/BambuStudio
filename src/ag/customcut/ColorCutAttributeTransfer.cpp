#include "ColorCutAttributeTransfer.hpp"

#include "ColorCutAttributeRepository.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/AABBTreeIndirect.hpp"
#include "libslic3r/TriangleSelector.hpp"
#include "libslic3r/SLA/IndexedMesh.hpp"

#include <algorithm>
#include <boost/dll/runtime_symbol_info.hpp>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <queue>
#include <set>
#include <sstream>
#include <unordered_map>
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>

namespace Slic3r {
namespace ColorCut {

namespace {

static std::string colorcut_log_file_path()
{
    return (boost::dll::program_location().parent_path() / "colorcut.log").string();
}

static void append_curve_log(const std::string &message)
{
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    std::ofstream out("curve.log", std::ios::app);
    if (!out)
        return;
    out << message << std::endl;
}

static std::mutex s_colorcut_log_mutex;

static void colorcut_log(const std::string &message)
{
    std::lock_guard<std::mutex> lock(s_colorcut_log_mutex);
    std::ofstream out(colorcut_log_file_path(), std::ios::app);
    if (!out)
        return;
    out << message << std::endl;
}

static void colorcut_log_phase(const std::string &phase_name)
{
    colorcut_log("");
    colorcut_log("****************************************");
    colorcut_log(phase_name);
    colorcut_log("****************************************");
}

static std::string format_vec3(const Vec3d &value)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(4)
           << "(" << value.x() << ", " << value.y() << ", " << value.z() << ")";
    return stream.str();
}

static std::string format_bbox(const BoundingBoxf3 &bbox)
{
    if (!bbox.defined)
        return "<undefined>";

    std::ostringstream stream;
    stream << std::fixed << std::setprecision(4)
           << "min(" << bbox.min.x() << ", " << bbox.min.y() << ", " << bbox.min.z() << ")"
           << " max(" << bbox.max.x() << ", " << bbox.max.y() << ", " << bbox.max.z() << ")";
    return stream.str();
}

struct TriangleColorBindingKey
{
    int pid{-1};
    std::array<int, 3> indices{{-1, -1, -1}};

    bool valid() const { return pid >= 0; }
};

struct SourceTriangleCandidate
{
    int   triangle_index{-1};
    Vec3d centroid{Vec3d::Zero()};
    Vec3d normal{Vec3d::Zero()};
};

static std::string make_binding_key(const TriangleColorBindingKey &binding)
{
    return std::to_string(binding.pid) + ":" + std::to_string(binding.indices[0]) + ":" + std::to_string(binding.indices[1]) + ":" + std::to_string(binding.indices[2]);
}

static const VolumeAppearanceSnapshot *find_volume_snapshot(const ObjectAppearanceSnapshot &snapshot, int source_volume_id)
{
    for (const VolumeAppearanceSnapshot &volume_snapshot : snapshot.volumes)
        if (volume_snapshot.volume_id == source_volume_id)
            return &volume_snapshot;
    return nullptr;
}

static const ModelVolume *find_source_volume(const ModelObject &object, int source_volume_id)
{
    for (const ModelVolume *volume : object.volumes)
        if (volume->id().id == source_volume_id)
            return volume;
    return nullptr;
}

static const TriangleAppearanceRecord *find_triangle_record(const VolumeAppearanceSnapshot &snapshot, int triangle_index)
{
    if (triangle_index < 0 || triangle_index >= static_cast<int>(snapshot.triangle_records.size()))
        return nullptr;
    return &snapshot.triangle_records[static_cast<size_t>(triangle_index)];
}

static bool triangle_record_has_appearance(const TriangleAppearanceRecord &record)
{
    return !record.payload.empty() || record.triangle_color_pid >= 0;
}

static const TriangleAppearanceRecord *find_triangle_record_with_appearance(const VolumeAppearanceSnapshot &snapshot, int triangle_index)
{
    const TriangleAppearanceRecord *record = find_triangle_record(snapshot, triangle_index);
    return record != nullptr && triangle_record_has_appearance(*record) ? record : nullptr;
}

static std::string make_record_signature(const TriangleAppearanceRecord &record)
{
    return record.payload + "|" + std::to_string(record.triangle_color_pid) + ":"
           + std::to_string(record.color_indices[0]) + ":"
           + std::to_string(record.color_indices[1]) + ":"
           + std::to_string(record.color_indices[2]);
}

static std::vector<ModelVolume *> collect_model_part_volumes(const ModelObjectPtrs &objects)
{
    std::vector<ModelVolume *> volumes;
    for (ModelObject *object : objects)
        for (ModelVolume *volume : object->volumes)
            if (volume->is_model_part() && !volume->is_cut_connector())
                volumes.emplace_back(volume);
    return volumes;
}

static std::string summarize_output_objects(const ModelObjectPtrs &objects)
{
    std::ostringstream summary;
    summary << "objects=" << objects.size();
    for (size_t object_index = 0; object_index < objects.size(); ++object_index) {
        const ModelObject *object = objects[object_index];
        if (object == nullptr) {
            summary << " [obj" << object_index << "=null]";
            continue;
        }

        summary << " [obj" << object_index << ":vols=" << object->volumes.size();
        for (size_t volume_index = 0; volume_index < object->volumes.size(); ++volume_index) {
            const ModelVolume *volume = object->volumes[volume_index];
            if (volume == nullptr) {
                summary << " v" << volume_index << "=null";
                continue;
            }

            summary << " v" << volume_index
                    << "{type=" << static_cast<int>(volume->type())
                    << ",model=" << (volume->is_model_part() ? 1 : 0)
                    << ",connector=" << (volume->is_cut_connector() ? 1 : 0)
                    << ",processed=" << (volume->cut_info.is_processed ? 1 : 0)
                    << ",is_connector=" << (volume->cut_info.is_connector ? 1 : 0)
                    << ",tris=" << volume->mesh().its.indices.size()
                    << "}";
        }
        summary << "]";
    }
    return summary.str();
}

static std::optional<std::string> dominant_mmu_tag(
    const VolumeAppearanceSnapshot &snapshot,
    const std::vector<SourceTriangleRef> &source_refs)
{
    std::map<std::string, size_t> counts;
    for (const SourceTriangleRef &source_ref : source_refs) {
        const TriangleAppearanceRecord *record = find_triangle_record(snapshot, source_ref.triangle_index);
        if (record == nullptr || record->payload.empty())
            continue;
        ++counts[record->payload];
    }

    if (counts.empty()) {
        for (const TriangleAppearanceRecord &record : snapshot.triangle_records)
            if (!record.payload.empty())
                ++counts[record.payload];
    }

    if (counts.empty())
        return std::nullopt;

    return std::max_element(counts.begin(), counts.end(),
        [](const auto &left, const auto &right) { return left.second < right.second; })->first;
}

static std::optional<TriangleColorBindingKey> dominant_triangle_color_binding(
    const VolumeAppearanceSnapshot &snapshot,
    const std::vector<SourceTriangleRef> &source_refs,
    const std::optional<TriangleColorBindingKey> &fallback_binding)
{
    std::map<std::string, std::pair<TriangleColorBindingKey, size_t>> counts;
    auto add_record = [&counts](const TriangleAppearanceRecord &record) {
        if (record.triangle_color_pid < 0)
            return;
        TriangleColorBindingKey binding;
        binding.pid     = record.triangle_color_pid;
        binding.indices = record.color_indices;
        auto &entry = counts[make_binding_key(binding)];
        entry.first = binding;
        ++entry.second;
    };

    for (const SourceTriangleRef &source_ref : source_refs) {
        const TriangleAppearanceRecord *record = find_triangle_record(snapshot, source_ref.triangle_index);
        if (record != nullptr)
            add_record(*record);
    }

    if (counts.empty())
        for (const TriangleAppearanceRecord &record : snapshot.triangle_records)
            add_record(record);

    if (!counts.empty())
        return std::max_element(counts.begin(), counts.end(),
            [](const auto &left, const auto &right) { return left.second.second < right.second.second; })->second.first;

    return fallback_binding;
}

static std::optional<TriangleColorBindingKey> default_triangle_color_binding(const ExternalVolumeColorData &data)
{
    if (data.pid < 0 || data.pindex < 0)
        return std::nullopt;
    TriangleColorBindingKey binding;
    binding.pid     = data.pid;
    binding.indices = {{data.pindex, data.pindex, data.pindex}};
    return binding;
}

static Transform3d make_cut_local_transform(const ModelObject &object, const ModelVolume &volume, int instance_index, const Transform3d &cut_matrix)
{
    const auto volume_matrix = volume.get_matrix();
    const Geometry::Transformation cut_transformation = Geometry::Transformation(cut_matrix);
    const Transform3d invert_cut_matrix = cut_transformation.get_matrix(true, false, true, true).inverse()
                                          * Geometry::translation_transform(-1 * cut_transformation.get_offset());
    const Transform3d instance_matrix = object.instances[static_cast<size_t>(instance_index)]->get_transformation().get_matrix_no_offset();
    return invert_cut_matrix * instance_matrix * volume_matrix;
}

static Vec3d triangle_centroid(const indexed_triangle_set &mesh, int triangle_index)
{
    const stl_triangle_vertex_indices &triangle = mesh.indices[static_cast<size_t>(triangle_index)];
    Vec3d centroid = mesh.vertices[triangle(0)].cast<double>();
    centroid += mesh.vertices[triangle(1)].cast<double>();
    centroid += mesh.vertices[triangle(2)].cast<double>();
    return centroid / 3.0;
}

static Vec3d triangle_normal(const indexed_triangle_set &mesh, int triangle_index)
{
    const stl_triangle_vertex_indices &triangle = mesh.indices[static_cast<size_t>(triangle_index)];
    const Vec3d a = mesh.vertices[triangle(0)].cast<double>();
    const Vec3d b = mesh.vertices[triangle(1)].cast<double>();
    const Vec3d c = mesh.vertices[triangle(2)].cast<double>();

    Vec3d normal = (b - a).cross(c - a);
    const double length = normal.norm();
    if (length <= 1e-12)
        return Vec3d::Zero();
    return normal / length;
}

static bool triangle_normals_compatible(const Vec3d &left, const Vec3d &right)
{
    if (left.squaredNorm() <= 1e-12 || right.squaredNorm() <= 1e-12)
        return true;
    return left.dot(right) >= 0.95;
}

static std::string quantized_vertex_key(const Vec3d &vertex)
{
    constexpr double scale = 1000000.0;
    std::ostringstream stream;
    stream << std::llround(vertex.x() * scale) << ','
           << std::llround(vertex.y() * scale) << ','
           << std::llround(vertex.z() * scale);
    return stream.str();
}

static std::string triangle_geometry_key(const indexed_triangle_set &mesh, int triangle_index)
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

static std::unordered_map<std::string, int> build_triangle_geometry_lookup(const TriangleMesh &mesh)
{
    std::unordered_map<std::string, int> lookup;
    lookup.reserve(mesh.its.indices.size());
    for (size_t triangle_index = 0; triangle_index < mesh.its.indices.size(); ++triangle_index)
        lookup.emplace(triangle_geometry_key(mesh.its, static_cast<int>(triangle_index)), static_cast<int>(triangle_index));
    return lookup;
}

static void append_triangle_annotation_bits(
    int triangle_id,
    const std::string &str,
    std::pair<std::vector<std::pair<int, int>>, std::vector<bool>> &data)
{
    if (str.empty())
        return;

    data.first.emplace_back(triangle_id, static_cast<int>(data.second.size()));
    for (auto it = str.crbegin(); it != str.crend(); ++it) {
        const char ch = *it;
        int dec = 0;
        if (ch >= '0' && ch <= '9')
            dec = int(ch - '0');
        else if (ch >= 'A' && ch <= 'F')
            dec = 10 + int(ch - 'A');
        else
            continue;

        for (int bit_index = 0; bit_index < 4; ++bit_index)
            data.second.emplace_back((dec & (1 << bit_index)) != 0);
    }
}

static std::unique_ptr<TriangleSelector> build_mmu_triangle_selector(
    const VolumeAppearanceSnapshot &snapshot,
    const TriangleMesh &source_mesh)
{
    std::pair<std::vector<std::pair<int, int>>, std::vector<bool>> serialized_data;
    serialized_data.first.reserve(snapshot.triangle_records.size());
    for (const TriangleAppearanceRecord &record : snapshot.triangle_records) {
        if (record.payload.empty())
            continue;
        append_triangle_annotation_bits(record.source_triangle_index, record.payload, serialized_data);
    }

    if (serialized_data.first.empty())
        return nullptr;

    auto selector = std::make_unique<TriangleSelector>(source_mesh);
    selector->deserialize(serialized_data, false);
    return selector;
}

static std::string encode_filament_state(EnforcerBlockerType state)
{
    const int filament_id = int(state);
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

static int extract_dominant_mmu_state(const std::string &mmu_string)
{
    if (mmu_string.empty())
        return -1;

    std::vector<int> nibbles;
    nibbles.reserve(mmu_string.size());
    for (char ch : mmu_string) {
        if (ch >= '0' && ch <= '9')
            nibbles.push_back(ch - '0');
        else if (ch >= 'A' && ch <= 'F')
            nibbles.push_back(10 + ch - 'A');
        else
            return -1;
    }

    size_t pos = 0;
    std::map<int, int> state_counts;
    std::function<void()> walk = [&]() {
        if (pos >= nibbles.size())
            return;

        const int code = nibbles[pos++];
        const int split_sides = code & 0b0011;
        if (split_sides == 0) {
            int state = -1;
            if ((code & 0b1100) == 0b1100) {
                int num = 0;
                int ext = 0;
                while (pos < nibbles.size()) {
                    ext = nibbles[pos++];
                    if (ext != 0b1111)
                        break;
                    ++num;
                }
                state = ext + 15 * num + 3;
            } else {
                state = code >> 2;
            }
            ++state_counts[state];
        } else {
            const int num_children = split_sides + 1;
            for (int child = 0; child < num_children; ++child)
                walk();
        }
    };

    walk();

    int dominant = -1;
    int max_count = 0;
    for (const auto &[state, count] : state_counts) {
        if (count > max_count) {
            dominant = state;
            max_count = count;
        }
    }
    return dominant;
}

static std::string simplify_mmu_to_dominant(const std::string &mmu_string)
{
    if (mmu_string.size() <= 2)
        return mmu_string;

    const int dominant_state = extract_dominant_mmu_state(mmu_string);
    if (dominant_state < 0)
        return {};
    if (dominant_state <= 2) {
        const int code = dominant_state << 2;
        return std::string(1, code < 10 ? static_cast<char>('0' + code)
                                        : static_cast<char>('A' + code - 10));
    }

    std::vector<int> nibbles;
    nibbles.push_back(0b1100);
    int remaining = dominant_state - 3;
    while (remaining >= 15) {
        nibbles.push_back(0b1111);
        remaining -= 15;
    }
    nibbles.push_back(remaining);

    std::string encoded;
    for (auto it = nibbles.rbegin(); it != nibbles.rend(); ++it) {
        const int nibble = *it;
        encoded += nibble < 10 ? static_cast<char>('0' + nibble)
                               : static_cast<char>('A' + nibble - 10);
    }
    return encoded;
}

static std::optional<std::string> sample_mmu_tag_at_point(
    TriangleSelector &selector,
    int source_triangle_index,
    const Vec3d &mesh_point)
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

    const std::string encoded = encode_filament_state(triangle.get_state());
    if (encoded.empty())
        return std::nullopt;
    return encoded;
}

static std::vector<SourceTriangleCandidate> build_source_triangle_candidates(
    const ModelObject &object,
    const ModelVolume &volume,
    int instance_index,
    const Transform3d &cut_matrix,
    const std::vector<SourceTriangleRef> &source_refs)
{
    std::vector<SourceTriangleCandidate> candidates;
    std::set<int> unique_triangles;
    for (const SourceTriangleRef &source_ref : source_refs)
        if (source_ref.triangle_index >= 0)
            unique_triangles.insert(source_ref.triangle_index);

    if (unique_triangles.empty())
        return candidates;

    TriangleMesh transformed_mesh(volume.mesh());
    transformed_mesh.transform(make_cut_local_transform(object, volume, instance_index, cut_matrix), true);

    candidates.reserve(unique_triangles.size());
    for (int triangle_index : unique_triangles)
        candidates.push_back({triangle_index, triangle_centroid(transformed_mesh.its, triangle_index), triangle_normal(transformed_mesh.its, triangle_index)});
    return candidates;
}

static Vec3d output_triangle_centroid(const TriangleMesh &mesh, int triangle_index)
{
    return triangle_centroid(mesh.its, triangle_index);
}

static Vec3d output_triangle_normal(const TriangleMesh &mesh, int triangle_index)
{
    return triangle_normal(mesh.its, triangle_index);
}

static bool triangle_on_plane_cut_surface(const TriangleMesh &mesh, int triangle_index)
{
    const Vec3d centroid = output_triangle_centroid(mesh, triangle_index);
    const Vec3d normal = output_triangle_normal(mesh, triangle_index);
    return std::abs(centroid.z()) <= 0.05 && std::abs(normal.z()) >= 0.80;
}

static bool triangle_on_planar_extreme_surface(
    const TriangleMesh &mesh,
    int triangle_index,
    const BoundingBoxf3 &bbox,
    double plane_tolerance = 0.05,
    double min_abs_normal_z = 0.80)
{
    if (!bbox.defined)
        return false;

    const Vec3d centroid = output_triangle_centroid(mesh, triangle_index);
    const Vec3d normal = output_triangle_normal(mesh, triangle_index);
    if (std::abs(normal.z()) < min_abs_normal_z)
        return false;

    return std::abs(centroid.z() - double(bbox.min.z())) <= plane_tolerance
        || std::abs(centroid.z() - double(bbox.max.z())) <= plane_tolerance;
}

static bool triangle_on_surface_plane(
    const TriangleMesh &mesh,
    int triangle_index,
    const SurfacePlaneDefinition &plane,
    double plane_tolerance,
    double min_abs_normal_dot)
{
    if (!plane.is_valid())
        return false;

    const Vec3d centroid = output_triangle_centroid(mesh, triangle_index);
    const Vec3d normal = output_triangle_normal(mesh, triangle_index);
    const Vec3d plane_normal = plane.normal.normalized();
    const double plane_distance = std::abs((centroid - plane.point).dot(plane_normal));
    if (plane_distance > plane_tolerance)
        return false;

    if (normal.squaredNorm() <= 1e-12)
        return true;

    return std::abs(normal.normalized().dot(plane_normal)) >= min_abs_normal_dot;
}

static int triangle_surface_plane_index(
    const TriangleMesh &mesh,
    int triangle_index,
    const std::vector<SurfacePlaneDefinition> &surface_planes,
    double plane_tolerance,
    double min_abs_normal_dot)
{
    for (size_t plane_index = 0; plane_index < surface_planes.size(); ++plane_index) {
        if (triangle_on_surface_plane(mesh, triangle_index, surface_planes[plane_index], plane_tolerance, min_abs_normal_dot))
            return static_cast<int>(plane_index);
    }
    return -1;
}

static double triangle_surface_plane_distance(
    const TriangleMesh &mesh,
    int triangle_index,
    const SurfacePlaneDefinition &plane)
{
    if (!plane.is_valid())
        return std::numeric_limits<double>::max();

    const Vec3d centroid = output_triangle_centroid(mesh, triangle_index);
    const Vec3d plane_normal = plane.normal.normalized();
    return std::abs((centroid - plane.point).dot(plane_normal));
}

static double triangle_surface_plane_normal_alignment(
    const TriangleMesh &mesh,
    int triangle_index,
    const SurfacePlaneDefinition &plane)
{
    if (!plane.is_valid())
        return 0.0;

    const Vec3d normal = output_triangle_normal(mesh, triangle_index);
    if (normal.squaredNorm() <= 1e-12)
        return 1.0;

    return std::abs(normal.normalized().dot(plane.normal.normalized()));
}

static std::vector<unsigned char> build_largest_planar_surface_component(
    const TriangleMesh &mesh,
    const BoundingBoxf3 &bbox,
    const std::vector<std::vector<int>> &adjacency,
    double plane_tolerance = 0.05,
    double min_abs_normal_z = 0.80)
{
    std::vector<unsigned char> candidate_mask(mesh.its.indices.size(), 0);
    for (size_t triangle_index = 0; triangle_index < mesh.its.indices.size(); ++triangle_index) {
        if (triangle_on_planar_extreme_surface(mesh, static_cast<int>(triangle_index), bbox, plane_tolerance, min_abs_normal_z))
            candidate_mask[triangle_index] = 1;
    }

    std::vector<unsigned char> largest_component_mask(mesh.its.indices.size(), 0);
    std::vector<unsigned char> visited(mesh.its.indices.size(), 0);
    std::vector<size_t> largest_component;

    for (size_t start_index = 0; start_index < candidate_mask.size(); ++start_index) {
        if (!candidate_mask[start_index] || visited[start_index])
            continue;

        std::vector<size_t> component;
        std::queue<size_t> pending;
        pending.push(start_index);
        visited[start_index] = 1;

        while (!pending.empty()) {
            const size_t triangle_index = pending.front();
            pending.pop();
            component.push_back(triangle_index);

            for (int neighbor_index : adjacency[triangle_index]) {
                const size_t neighbor = static_cast<size_t>(neighbor_index);
                if (visited[neighbor] || !candidate_mask[neighbor])
                    continue;
                visited[neighbor] = 1;
                pending.push(neighbor);
            }
        }

        if (component.size() > largest_component.size())
            largest_component = std::move(component);
    }

    for (size_t triangle_index : largest_component)
        largest_component_mask[triangle_index] = 1;

    return largest_component_mask;
}

static std::vector<unsigned char> build_marked_cut_surface_component(const ModelVolume &volume)
{
    std::vector<unsigned char> mask(volume.mesh().its.indices.size(), 0);
    for (size_t triangle_index = 0; triangle_index < volume.mesh().its.indices.size(); ++triangle_index) {
        if (!volume.exterior_facets.get_triangle_as_string(static_cast<int>(triangle_index)).empty())
            mask[triangle_index] = 1;
    }
    return mask;
}

static size_t count_mask_members(const std::vector<unsigned char> &mask)
{
    size_t count = 0;
    for (unsigned char value : mask)
        if (value)
            ++count;
    return count;
}

static uint64_t make_edge_key(int first, int second)
{
    if (first > second)
        std::swap(first, second);
    return (static_cast<uint64_t>(static_cast<uint32_t>(first)) << 32)
         | static_cast<uint32_t>(second);
}

static std::vector<std::vector<int>> build_triangle_adjacency(const indexed_triangle_set &mesh)
{
    std::vector<std::vector<int>> adjacency(mesh.indices.size());
    std::unordered_map<uint64_t, std::vector<int>> edge_to_triangles;
    edge_to_triangles.reserve(mesh.indices.size() * 3);

    for (size_t triangle_index = 0; triangle_index < mesh.indices.size(); ++triangle_index) {
        const stl_triangle_vertex_indices &triangle = mesh.indices[triangle_index];
        edge_to_triangles[make_edge_key(triangle(0), triangle(1))].push_back(static_cast<int>(triangle_index));
        edge_to_triangles[make_edge_key(triangle(1), triangle(2))].push_back(static_cast<int>(triangle_index));
        edge_to_triangles[make_edge_key(triangle(2), triangle(0))].push_back(static_cast<int>(triangle_index));
    }

    for (const auto &entry : edge_to_triangles) {
        const std::vector<int> &triangles = entry.second;
        for (size_t left = 0; left < triangles.size(); ++left) {
            for (size_t right = left + 1; right < triangles.size(); ++right) {
                adjacency[static_cast<size_t>(triangles[left])].push_back(triangles[right]);
                adjacency[static_cast<size_t>(triangles[right])].push_back(triangles[left]);
            }
        }
    }

    return adjacency;
}

static std::vector<SourceTriangleCandidate> build_all_source_triangle_candidates(const TriangleMesh &mesh, const VolumeAppearanceSnapshot &snapshot)
{
    std::vector<SourceTriangleCandidate> candidates;
    candidates.reserve(snapshot.triangle_records.size());
    for (const TriangleAppearanceRecord &record : snapshot.triangle_records) {
        if (record.source_triangle_index < 0 || record.source_triangle_index >= static_cast<int>(mesh.its.indices.size()))
            continue;
        candidates.push_back({
            record.source_triangle_index,
            triangle_centroid(mesh.its, record.source_triangle_index),
            triangle_normal(mesh.its, record.source_triangle_index)
        });
    }
    return candidates;
}

static const TriangleAppearanceRecord *find_nearest_source_record(
    const VolumeAppearanceSnapshot &snapshot,
    const std::vector<SourceTriangleCandidate> &candidates,
    const TriangleMesh &output_mesh,
    int output_triangle_index)
{
    if (candidates.empty())
        return nullptr;

    const Vec3d output_centroid = output_triangle_centroid(output_mesh, output_triangle_index);
    const Vec3d output_normal = output_triangle_normal(output_mesh, output_triangle_index);
    const SourceTriangleCandidate *nearest_any = nullptr;
    double nearest_any_distance = std::numeric_limits<double>::max();
    std::vector<std::pair<const SourceTriangleCandidate *, double>> nearest_any_candidates;
    std::vector<std::pair<const SourceTriangleCandidate *, double>> compatible_candidates;
    nearest_any_candidates.reserve(candidates.size());
    compatible_candidates.reserve(candidates.size());

    for (const SourceTriangleCandidate &candidate : candidates) {
        const double distance = (candidate.centroid - output_centroid).squaredNorm();
        nearest_any_candidates.emplace_back(&candidate, distance);
        if (distance < nearest_any_distance) {
            nearest_any_distance = distance;
            nearest_any = &candidate;
        }
        if (triangle_normals_compatible(candidate.normal, output_normal))
            compatible_candidates.emplace_back(&candidate, distance);
    }

    if (nearest_any == nullptr)
        return nullptr;

    struct NeighborhoodChoice
    {
        const TriangleAppearanceRecord *record{nullptr};
        size_t count{0};
        double best_distance{std::numeric_limits<double>::max()};
    };

    auto choose_majority_record = [&snapshot](std::vector<std::pair<const SourceTriangleCandidate *, double>> candidate_distances, size_t neighborhood_size) {
        if (candidate_distances.empty())
            return static_cast<const TriangleAppearanceRecord *>(nullptr);

        std::sort(candidate_distances.begin(), candidate_distances.end(),
            [](const auto &left, const auto &right) { return left.second < right.second; });

        std::unordered_map<std::string, NeighborhoodChoice> choices;
        const size_t limit = std::min(neighborhood_size, candidate_distances.size());
        for (size_t index = 0; index < limit; ++index) {
            const TriangleAppearanceRecord *record = find_triangle_record_with_appearance(snapshot, candidate_distances[index].first->triangle_index);
            if (record == nullptr)
                continue;

            NeighborhoodChoice &choice = choices[make_record_signature(*record)];
            choice.record = record;
            ++choice.count;
            choice.best_distance = std::min(choice.best_distance, candidate_distances[index].second);
        }

        const TriangleAppearanceRecord *best_record = nullptr;
        size_t best_count = 0;
        double best_distance = std::numeric_limits<double>::max();
        for (const auto &entry : choices) {
            const NeighborhoodChoice &choice = entry.second;
            if (choice.record == nullptr)
                continue;
            if (choice.count > best_count || (choice.count == best_count && choice.best_distance < best_distance)) {
                best_record = choice.record;
                best_count = choice.count;
                best_distance = choice.best_distance;
            }
        }

        return best_record;
    };

    if (const TriangleAppearanceRecord *best_compatible = choose_majority_record(std::move(compatible_candidates), 5); best_compatible != nullptr)
        return best_compatible;

    if (const TriangleAppearanceRecord *best_any = choose_majority_record(std::move(nearest_any_candidates), 5); best_any != nullptr)
        return best_any;

    return find_triangle_record_with_appearance(snapshot, nearest_any->triangle_index);
}

// ---------- Spatial-index accelerated nearest search ----------
// Build a simple AABB tree over candidate centroids for O(log N) lookups
// instead of the O(N) brute-force scan per output triangle.

struct CentroidBBoxGetter {
    const std::vector<SourceTriangleCandidate> *candidates{nullptr};
    using VectorType = Vec3d;
    int size() const { return static_cast<int>(candidates->size()); }
    Vec3d operator()(int i) const { return (*candidates)[static_cast<size_t>(i)].centroid; }
};

// Find the K nearest source candidates by distance to a query point
// using partial sort on precomputed distances (faster than full sort for K << N).
static std::vector<std::pair<const SourceTriangleCandidate *, double>>
find_k_nearest_candidates(
    const std::vector<SourceTriangleCandidate> &candidates,
    const Vec3d &query_point,
    size_t k)
{
    // Build distance list
    std::vector<std::pair<size_t, double>> distances;
    distances.reserve(candidates.size());
    for (size_t i = 0; i < candidates.size(); ++i) {
        const double d = (candidates[i].centroid - query_point).squaredNorm();
        distances.emplace_back(i, d);
    }
    if (distances.size() > k) {
        std::partial_sort(distances.begin(), distances.begin() + k, distances.end(),
            [](const auto &a, const auto &b) { return a.second < b.second; });
        distances.resize(k);
    } else {
        std::sort(distances.begin(), distances.end(),
            [](const auto &a, const auto &b) { return a.second < b.second; });
    }
    std::vector<std::pair<const SourceTriangleCandidate *, double>> result;
    result.reserve(distances.size());
    for (const auto &[idx, dist] : distances)
        result.emplace_back(&candidates[idx], dist);
    return result;
}

// Fast version: use precomputed K-nearest for majority voting
static const TriangleAppearanceRecord *find_nearest_source_record_fast(
    const VolumeAppearanceSnapshot &snapshot,
    const std::vector<SourceTriangleCandidate> &candidates,
    const Vec3d &output_centroid,
    const Vec3d &output_normal)
{
    if (candidates.empty())
        return nullptr;

    // When the source mesh is simple (few large triangles), a target triangle
    // near an edge can have its centroid closer to centroids of source triangles
    // on adjacent faces than to the centroid of its own large source triangle.
    // Using all candidates ensures the normal-compatibility filter always finds
    // the correct face. With 200-300 candidates this is still O(N log N) per
    // query which is negligible in practice.
    const size_t K = (candidates.size() <= 256) ? candidates.size() : 12;
    auto nearest = find_k_nearest_candidates(candidates, output_centroid, K);
    if (nearest.empty())
        return nullptr;

    // Separate compatible and all
    std::vector<std::pair<const SourceTriangleCandidate *, double>> compatible;
    compatible.reserve(nearest.size());
    for (const auto &[cand, dist] : nearest) {
        if (triangle_normals_compatible(cand->normal, output_normal))
            compatible.emplace_back(cand, dist);
    }

    struct NeighborhoodChoice {
        const TriangleAppearanceRecord *record{nullptr};
        size_t count{0};
        double best_distance{std::numeric_limits<double>::max()};
    };

    auto choose_majority_record = [&snapshot](const std::vector<std::pair<const SourceTriangleCandidate *, double>> &candidate_distances, size_t neighborhood_size) {
        if (candidate_distances.empty())
            return static_cast<const TriangleAppearanceRecord *>(nullptr);

        std::unordered_map<std::string, NeighborhoodChoice> choices;
        const size_t limit = std::min(neighborhood_size, candidate_distances.size());
        for (size_t index = 0; index < limit; ++index) {
            const TriangleAppearanceRecord *record = find_triangle_record_with_appearance(snapshot, candidate_distances[index].first->triangle_index);
            if (record == nullptr) continue;
            NeighborhoodChoice &choice = choices[make_record_signature(*record)];
            choice.record = record;
            ++choice.count;
            choice.best_distance = std::min(choice.best_distance, candidate_distances[index].second);
        }

        const TriangleAppearanceRecord *best_record = nullptr;
        size_t best_count = 0;
        double best_distance = std::numeric_limits<double>::max();
        for (const auto &entry : choices) {
            const NeighborhoodChoice &choice = entry.second;
            if (choice.record == nullptr) continue;
            if (choice.count > best_count || (choice.count == best_count && choice.best_distance < best_distance)) {
                best_record = choice.record;
                best_count = choice.count;
                best_distance = choice.best_distance;
            }
        }
        return best_record;
    };

    if (const TriangleAppearanceRecord *best_compatible = choose_majority_record(compatible, std::min<size_t>(compatible.size(), 8)); best_compatible != nullptr)
        return best_compatible;

    if (const TriangleAppearanceRecord *best_any = choose_majority_record(nearest, std::min<size_t>(nearest.size(), 8)); best_any != nullptr)
        return best_any;

    return find_triangle_record_with_appearance(snapshot, nearest.front().first->triangle_index);
}

} // namespace

void ColorCutAttributeTransfer::apply(
    const ObjectAppearanceSnapshot &snapshot,
    GeometryCutOutput &geometry_output,
    const ColorCutRequest &request) const
{
    if (!geometry_output.success || request.object == nullptr)
        return;

    std::vector<ModelVolume *> output_volumes = collect_model_part_volumes(geometry_output.new_objects);
    if (output_volumes.size() != geometry_output.volumes.size()) {
        geometry_output.warnings.push_back({
            ColorCutWarningCode::Unhandled,
            "ColorCut transfer could not align output volumes with geometry provenance (output model-part volumes="
                + std::to_string(output_volumes.size())
                + ", provenance volumes="
                + std::to_string(geometry_output.volumes.size())
                + "). "
                + summarize_output_objects(geometry_output.new_objects)
        });
        geometry_output.success = false;
        geometry_output.new_objects.clear();
        geometry_output.volumes.clear();
        return;
    }

    ColorCutAttributeRepository &repository = global_color_cut_attribute_repository();

    for (size_t volume_index = 0; volume_index < output_volumes.size(); ++volume_index) {
        ModelVolume *output_volume = output_volumes[volume_index];
        const GeometryCutOutputVolume &geometry_volume = geometry_output.volumes[volume_index];

        int source_volume_id = -1;
        if (!geometry_volume.provenance.cap_adjacent_sources.empty())
            source_volume_id = geometry_volume.provenance.cap_adjacent_sources.front().volume_id;
        else {
            for (const OutputTriangleProvenance &triangle : geometry_volume.provenance.triangles) {
                if (!triangle.source_triangles.empty()) {
                    source_volume_id = triangle.source_triangles.front().volume_id;
                    break;
                }
            }
        }

        const VolumeAppearanceSnapshot *source_snapshot = find_volume_snapshot(snapshot, source_volume_id);
        const ModelVolume *source_volume = find_source_volume(*request.object, source_volume_id);
        if (source_snapshot == nullptr || source_volume == nullptr) {
            geometry_output.warnings.push_back({
                ColorCutWarningCode::Unhandled,
                "ColorCut transfer could not locate source appearance data for an output volume."
            });
            geometry_output.success = false;
            geometry_output.new_objects.clear();
            geometry_output.volumes.clear();
            return;
        }

        const std::optional<ExternalVolumeColorData> external_color_data = repository.get_volume_color_data(source_volume_id);
        const std::optional<TriangleColorBindingKey> default_cap_binding = external_color_data.has_value() ? default_triangle_color_binding(*external_color_data) : std::nullopt;
        const std::optional<std::string> default_cap_mmu = dominant_mmu_tag(*source_snapshot, geometry_volume.provenance.cap_adjacent_sources);
        const std::optional<TriangleColorBindingKey> dominant_cap_binding = dominant_triangle_color_binding(*source_snapshot, geometry_volume.provenance.cap_adjacent_sources, default_cap_binding);
        const std::vector<SourceTriangleCandidate> cap_candidates = build_source_triangle_candidates(*request.object, *source_volume, request.instance_index, request.cut_matrix, geometry_volume.provenance.cap_adjacent_sources);
        std::vector<SourceTriangleRef> all_source_refs;
        all_source_refs.reserve(source_snapshot->triangle_records.size());
        for (const TriangleAppearanceRecord &record : source_snapshot->triangle_records)
            all_source_refs.push_back({source_volume_id, record.source_triangle_index});
        const std::vector<SourceTriangleCandidate> all_source_candidates = build_source_triangle_candidates(*request.object, *source_volume, request.instance_index, request.cut_matrix, all_source_refs);

        if (output_volume->mesh().its.indices.size() != geometry_volume.provenance.triangles.size()) {
            geometry_output.warnings.push_back({
                ColorCutWarningCode::Unhandled,
                "ColorCut transfer detected a triangle count mismatch between output meshes and provenance."
            });
            geometry_output.success = false;
            geometry_output.new_objects.clear();
            geometry_output.volumes.clear();
            return;
        }

        const bool provenance_has_usable_inherited_indices = std::any_of(
            geometry_volume.provenance.triangles.begin(),
            geometry_volume.provenance.triangles.end(),
            [](const OutputTriangleProvenance &triangle) {
                return std::any_of(
                    triangle.source_triangles.begin(),
                    triangle.source_triangles.end(),
                    [](const SourceTriangleRef &source_ref) { return source_ref.triangle_index >= 0; });
            });

        if (provenance_has_usable_inherited_indices)
            output_volume->mmu_segmentation_facets.reset();
        output_volume->mmu_segmentation_facets.reserve(static_cast<int>(output_volume->mesh().its.indices.size()));

        ExternalVolumeColorData transferred_color_data;
        std::optional<ExternalVolumeColorData> existing_output_color_data = repository.get_volume_color_data(output_volume->id().id);
        if (external_color_data.has_value()) {
            transferred_color_data.pid = external_color_data->pid;
            transferred_color_data.pindex = external_color_data->pindex;
            transferred_color_data.triangle_colors.reserve(geometry_volume.provenance.triangles.size());
        }

        size_t cut_surface_triangles = 0;
        size_t inherited_triangles = 0;
        size_t inherited_records_found = 0;
        size_t inherited_records_missing = 0;
        size_t inherited_volume_id_mismatches = 0;
        size_t mmu_written = 0;
        for (size_t triangle_index = 0; triangle_index < geometry_volume.provenance.triangles.size(); ++triangle_index) {
            const OutputTriangleProvenance &triangle = geometry_volume.provenance.triangles[triangle_index];
            const bool treat_as_cut_surface = triangle.kind == ProvenanceTriangleKind::Cap
                || (request.uniform_cap_color && triangle_on_plane_cut_surface(geometry_volume.mesh, static_cast<int>(triangle_index)));

            std::string triangle_mmu_tag;
            std::optional<TriangleColorBindingKey> triangle_binding;
            bool inherited_record_found = false;

            if (treat_as_cut_surface) {
                ++cut_surface_triangles;
                // When uniform_cap_color is requested, skip per-triangle nearest-source
                // lookup and always use the dominant color for the entire cap surface.
                if (!request.uniform_cap_color) {
                    const TriangleAppearanceRecord *nearest_record = find_nearest_source_record(*source_snapshot, cap_candidates, geometry_volume.mesh, static_cast<int>(triangle_index));
                    if (nearest_record != nullptr) {
                        triangle_mmu_tag = nearest_record->payload;
                        if (nearest_record->triangle_color_pid >= 0) {
                            TriangleColorBindingKey binding;
                            binding.pid = nearest_record->triangle_color_pid;
                            binding.indices = nearest_record->color_indices;
                            triangle_binding = binding;
                        }
                    }
                }

                if (triangle_mmu_tag.empty() && default_cap_mmu.has_value())
                    triangle_mmu_tag = *default_cap_mmu;

                if (!triangle_binding.has_value() && dominant_cap_binding.has_value())
                    triangle_binding = dominant_cap_binding;
            } else if (!triangle.source_triangles.empty()) {
                ++inherited_triangles;
                const TriangleAppearanceRecord *record = nullptr;
                for (const SourceTriangleRef &source_ref : triangle.source_triangles) {
                    if (source_ref.volume_id != source_volume_id)
                        ++inherited_volume_id_mismatches;
                    if (source_ref.triangle_index >= 0)
                        record = find_triangle_record(*source_snapshot, source_ref.triangle_index);
                    if (record != nullptr)
                        break;
                }
                if (record != nullptr) {
                    inherited_record_found = true;
                    triangle_mmu_tag = record->payload;
                    if (record->triangle_color_pid >= 0) {
                        TriangleColorBindingKey binding;
                        binding.pid = record->triangle_color_pid;
                        binding.indices = record->color_indices;
                        triangle_binding = binding;
                    }
                }
            }

            if (inherited_record_found)
                ++inherited_records_found;
            else if (!treat_as_cut_surface && !triangle.source_triangles.empty())
                ++inherited_records_missing;

            if (!triangle_mmu_tag.empty()) {
                output_volume->mmu_segmentation_facets.set_triangle_from_string(static_cast<int>(triangle_index), triangle_mmu_tag);
                ++mmu_written;
            }

            if (external_color_data.has_value()) {
                TriangleColor color_binding;
                if (triangle_binding.has_value() && triangle_binding->valid()) {
                    color_binding.pid = triangle_binding->pid;
                    color_binding.indices[0] = triangle_binding->indices[0];
                    color_binding.indices[1] = triangle_binding->indices[1];
                    color_binding.indices[2] = triangle_binding->indices[2];
                } else if (treat_as_cut_surface) {
                    color_binding.pid = external_color_data->pid;
                    color_binding.indices[0] = external_color_data->pindex;
                    color_binding.indices[1] = external_color_data->pindex;
                    color_binding.indices[2] = external_color_data->pindex;
                } else if (!provenance_has_usable_inherited_indices && existing_output_color_data.has_value() && triangle_index < existing_output_color_data->triangle_colors.size()) {
                    color_binding = existing_output_color_data->triangle_colors[triangle_index];
                } else {
                    color_binding.pid = -1;
                    color_binding.indices[0] = -1;
                    color_binding.indices[1] = -1;
                    color_binding.indices[2] = -1;
                }
                transferred_color_data.triangle_colors.emplace_back(color_binding);
            }
        }

        {
            std::ostringstream stream;
            stream << "COORDINATOR PROVENANCE TRANSFER volume[" << volume_index << "] '" << output_volume->name << "'"
                   << "\n  cut_surface_triangles=" << cut_surface_triangles
                   << "\n  inherited_triangles=" << inherited_triangles
                   << "\n  inherited_records_found=" << inherited_records_found
                   << "\n  inherited_records_missing=" << inherited_records_missing
                   << "\n  inherited_volume_id_mismatches=" << inherited_volume_id_mismatches
                   << "\n  provenance_has_usable_inherited_indices=" << (provenance_has_usable_inherited_indices ? "TRUE" : "FALSE")
                   << "\n  mmu_written=" << mmu_written;
            colorcut_log(stream.str());
        }

        output_volume->mmu_segmentation_facets.shrink_to_fit();

        if (external_color_data.has_value())
            repository.register_volume_color_data(output_volume->id().id, std::move(transferred_color_data));
    }
}

void ColorCutAttributeTransfer::reapply_from_single_source(
    const ObjectAppearanceSnapshot &source_appearance,
    const ModelObject &source_object,
    const ModelVolume &source_volume,
    int instance_index,
    const ModelObjectPtrs &target_objects,
    bool uniform_cap_color,
    const CutSurfaceClassifier &cut_surface_classifier,
    bool planar_cut_surface,
    bool groove_cut_surface) const
{
    // NOTE: log truncation has been moved to the gizmo entry point
    // (GLGizmoColorCut::perform_cut) so a single colorcut.log captures the
    // full lifecycle of one cut attempt across all paths (coordinator, legacy
    // fallback, cut-time replay, mesh-repair replay, marked-surface override).
    colorcut_log_phase("REAPPLY FROM SINGLE SOURCE - ENTRY");
    {
        std::ostringstream stream;
        stream << "source_volume: '" << source_volume.name << "'"
               << "\nsource_volume_id: " << source_volume.id().id
               << "\nsource_mesh_triangles: " << source_volume.mesh().its.indices.size()
               << "\nsource_mesh_vertices: " << source_volume.mesh().its.vertices.size()
               << "\nsource_bbox: " << format_bbox(source_volume.mesh().bounding_box())
               << "\ninstance_index: " << instance_index
               << "\ntarget_objects: " << target_objects.size()
               << "\nuniform_cap_color: " << (uniform_cap_color ? "TRUE" : "FALSE")
             << "\ncustom_cut_surface: " << (cut_surface_classifier ? "YES" : "NO")
               << "\nplanar_cut_surface: " << (planar_cut_surface ? "TRUE" : "FALSE")
               << "\ngroove_cut_surface: " << (groove_cut_surface ? "TRUE" : "FALSE")
               << "\nlog_path: " << colorcut_log_file_path();
        colorcut_log(stream.str());
    }

    const VolumeAppearanceSnapshot *volume_snapshot = find_volume_snapshot(source_appearance, source_volume.id().id);
    if (volume_snapshot == nullptr) {
        colorcut_log("ERROR: volume_snapshot is nullptr for source_volume_id=" + std::to_string(source_volume.id().id) + " — ABORTING");
        return;
    }

    colorcut_log("volume_snapshot found: triangle_records=" + std::to_string(volume_snapshot->triangle_records.size()));

    TriangleMesh source_mesh(source_volume.mesh());
    Vec3d instance_offset = Vec3d::Zero();
    if (instance_index >= 0 && instance_index < static_cast<int>(source_object.instances.size())) {
        const ModelInstance *instance = source_object.instances[static_cast<size_t>(instance_index)];
        instance_offset = instance->get_offset();
        const Transform3d instance_matrix = instance->get_transformation().get_matrix_no_offset();
        source_mesh.transform(instance_matrix * source_volume.get_matrix(), true);
        colorcut_log("source_mesh transformed with instance[" + std::to_string(instance_index) + "] offset=" + format_vec3(instance_offset));
    } else {
        source_mesh.transform(source_volume.get_matrix(), true);
        colorcut_log("source_mesh transformed without instance (volume matrix only)");
    }
    colorcut_log("transformed source_mesh bbox: " + format_bbox(source_mesh.bounding_box()));
    colorcut_log("transformed source_mesh triangles: " + std::to_string(source_mesh.its.indices.size()));

    for (size_t obj_idx = 0; obj_idx < target_objects.size(); ++obj_idx) {
        ModelObject *target_object = target_objects[obj_idx];
        if (target_object == nullptr) {
            colorcut_log("target_objects[" + std::to_string(obj_idx) + "] = nullptr, SKIP");
            continue;
        }

        colorcut_log_phase("TARGET OBJECT [" + std::to_string(obj_idx) + "]: " + target_object->name);
        {
            std::ostringstream stream;
            stream << "volumes: " << target_object->volumes.size()
                   << "\ninstances: " << target_object->instances.size()
                   << "\norigin_translation: " << format_vec3(target_object->origin_translation)
                   << "\nraw_bbox: " << format_bbox(target_object->raw_mesh_bounding_box());
            colorcut_log(stream.str());
        }

        for (size_t vol_idx = 0; vol_idx < target_object->volumes.size(); ++vol_idx) {
            ModelVolume *target_volume = target_object->volumes[vol_idx];
            if (target_volume == nullptr) {
                colorcut_log("  volume[" + std::to_string(vol_idx) + "] = nullptr, SKIP");
                continue;
            }
            if (!target_volume->is_model_part()) {
                colorcut_log("  volume[" + std::to_string(vol_idx) + "] '" + target_volume->name + "' is not model_part, SKIP");
                continue;
            }
            if (target_volume->is_cut_connector()) {
                colorcut_log("  volume[" + std::to_string(vol_idx) + "] '" + target_volume->name + "' is cut_connector, SKIP");
                continue;
            }

            colorcut_log("  volume[" + std::to_string(vol_idx) + "] '" + target_volume->name
                + "' triangles=" + std::to_string(target_volume->mesh().its.indices.size())
                + " -> calling reapply_after_mesh_repair");

            reapply_after_mesh_repair(*volume_snapshot, source_mesh, *target_volume, uniform_cap_color, cut_surface_classifier, planar_cut_surface, groove_cut_surface);
        }
    }
}

void ColorCutAttributeTransfer::reapply_after_mesh_repair(
    const VolumeAppearanceSnapshot &source_appearance,
    const TriangleMesh &source_mesh,
    ModelVolume &target_volume,
    bool uniform_cap_color,
    const CutSurfaceClassifier &cut_surface_classifier,
    bool planar_cut_surface,
    bool groove_cut_surface) const
{
    colorcut_log_phase("REAPPLY AFTER MESH REPAIR: '" + target_volume.name + "'");

    ColorCutAttributeRepository &repository = global_color_cut_attribute_repository();
    const std::optional<ExternalVolumeColorData> external_color_data = repository.get_volume_color_data(source_appearance.volume_id);
    auto mmu_selector = build_mmu_triangle_selector(source_appearance, source_mesh);
    const Slic3r::sla::IndexedMesh source_indexed_mesh(source_mesh);
    const std::vector<SourceTriangleCandidate> source_candidates = build_all_source_triangle_candidates(source_mesh, source_appearance);
    const std::unordered_map<std::string, int> exact_source_triangle_lookup = build_triangle_geometry_lookup(source_mesh);

    const size_t num_triangles = target_volume.mesh().its.indices.size();

    {
        std::ostringstream stream;
        stream << "source_appearance.volume_id: " << source_appearance.volume_id
               << "\nsource_appearance.triangle_records: " << source_appearance.triangle_records.size()
               << "\nexternal_color_data: " << (external_color_data.has_value() ? "YES" : "NO")
               << "\nsource_candidates (transformed triangles): " << source_candidates.size()
               << "\ntarget_triangles: " << num_triangles
               << "\nuniform_cap_color: " << (uniform_cap_color ? "TRUE" : "FALSE")
               << "\nplanar_cut_surface: " << (planar_cut_surface ? "TRUE" : "FALSE")
               << "\ngroove_cut_surface: " << (groove_cut_surface ? "TRUE" : "FALSE");
        colorcut_log(stream.str());
    }

    TriangleMesh transformed_target_mesh(target_volume.mesh());
    Transform3d target_transform = target_volume.get_matrix();
    if (target_volume.get_object() != nullptr && !target_volume.get_object()->instances.empty())
        target_transform = target_volume.get_object()->instances.front()->get_transformation().get_matrix_no_offset() * target_transform;
    transformed_target_mesh.transform(target_transform, true);
    const BoundingBoxf3 transformed_target_bbox = transformed_target_mesh.bounding_box();
    const std::vector<std::vector<int>> adjacency = uniform_cap_color ? build_triangle_adjacency(target_volume.mesh().its) : std::vector<std::vector<int>>{};
    const std::vector<unsigned char> marked_cut_surface_component = (uniform_cap_color && planar_cut_surface)
        ? build_marked_cut_surface_component(target_volume)
        : std::vector<unsigned char>{};
    const size_t marked_cut_surface_members = count_mask_members(marked_cut_surface_component);
    const std::vector<unsigned char> detected_planar_surface_component = (uniform_cap_color && planar_cut_surface && marked_cut_surface_members == 0)
        ? build_largest_planar_surface_component(transformed_target_mesh, transformed_target_bbox, adjacency)
        : std::vector<unsigned char>{};
    const std::vector<unsigned char> &planar_surface_component = marked_cut_surface_members > 0
        ? marked_cut_surface_component
        : detected_planar_surface_component;
    const size_t planar_surface_component_members = count_mask_members(planar_surface_component);
    const bool has_planar_surface_component = planar_surface_component_members > 0;

    colorcut_log_phase("MESH SETUP for '" + target_volume.name + "'");
    {
        std::ostringstream stream;
        stream << "target_volume offset: " << format_vec3(target_volume.get_offset())
               << "\ntarget transform: object instance no-offset matrix + volume matrix"
               << "\ntransformed_target_bbox: " << format_bbox(transformed_target_bbox)
               << "\nsource_mesh bbox: " << format_bbox(source_mesh.bounding_box())
               << "\nadjacency built: " << (!adjacency.empty() ? "YES (" + std::to_string(adjacency.size()) + " entries)" : "NO")
               << "\nmarked_cut_surface members: " << marked_cut_surface_members << " / " << target_volume.mesh().its.indices.size()
               << "\nplanar_surface_component source: " << (marked_cut_surface_members > 0 ? "cut provenance markers" : "z-extreme fallback")
               << "\nplanar_surface_component members: " << planar_surface_component_members << " / " << planar_surface_component.size();
        colorcut_log(stream.str());
    }

    // --- Parallel attribute lookup using TBB ---
    // Each thread writes to its own slot in the results vector (no contention).
    struct TriangleResult {
        std::string mmu_tag;
        TriangleColor color_binding;
        bool cap_seed{false};
        int compatible_matches{0};
        double min_sq_dist{-1.0};       // for groove debug: min squared distance to nearest source
        int classification{0};          // 0=source-matched, 1=warp-cap, 2=planar-cap, 3=groove-dist-cap, 4=normal-incompat-cap
    };
    std::vector<TriangleResult> results(num_triangles);

    // Precompute default color once
    TriangleColor default_color;
    if (external_color_data.has_value()) {
        default_color.pid = external_color_data->pid;
        default_color.indices[0] = external_color_data->pindex;
        default_color.indices[1] = external_color_data->pindex;
        default_color.indices[2] = external_color_data->pindex;
    }

    // When uniform_cap_color is requested, precompute the dominant appearance
    // from all source triangles. Cap faces (detected by having no normal-compatible
    // source match) will use this instead of the nearest-source fallback.
    std::string dominant_mmu;
    TriangleColor dominant_color = default_color;
    if (uniform_cap_color) {
        // Compute dominant MMU tag
        std::map<std::string, size_t> mmu_counts;
        for (const TriangleAppearanceRecord &record : source_appearance.triangle_records)
            if (!record.payload.empty())
                ++mmu_counts[record.payload];
        if (!mmu_counts.empty())
            dominant_mmu = std::max_element(mmu_counts.begin(), mmu_counts.end(),
                [](const auto &a, const auto &b) { return a.second < b.second; })->first;

        // Compute dominant color binding
        if (external_color_data.has_value()) {
            struct ColorCount { TriangleColor color; size_t count{0}; };
            std::unordered_map<std::string, ColorCount> color_counts;
            for (const TriangleAppearanceRecord &record : source_appearance.triangle_records) {
                if (record.triangle_color_pid < 0) continue;
                std::string key = std::to_string(record.triangle_color_pid) + ":"
                    + std::to_string(record.color_indices[0]) + ":"
                    + std::to_string(record.color_indices[1]) + ":"
                    + std::to_string(record.color_indices[2]);
                auto &entry = color_counts[key];
                if (entry.count == 0) {
                    entry.color.pid = record.triangle_color_pid;
                    entry.color.indices[0] = record.color_indices[0];
                    entry.color.indices[1] = record.color_indices[1];
                    entry.color.indices[2] = record.color_indices[2];
                }
                ++entry.count;
            }
            if (!color_counts.empty()) {
                dominant_color = std::max_element(color_counts.begin(), color_counts.end(),
                    [](const auto &a, const auto &b) { return a.second.count < b.second.count; })->second.color;
            }
        }
    }

    if (uniform_cap_color) {
        colorcut_log_phase("DOMINANT COLOR COMPUTATION for '" + target_volume.name + "'");
        {
            std::ostringstream stream;
            stream << "dominant_mmu: '" << dominant_mmu << "'"
                   << "\ndominant_color pid: " << dominant_color.pid
                   << "\ndominant_color indices: [" << dominant_color.indices[0] << ", " << dominant_color.indices[1] << ", " << dominant_color.indices[2] << "]"
                   << "\ndefault_color pid: " << default_color.pid
                   << "\ndefault_color indices: [" << default_color.indices[0] << ", " << default_color.indices[1] << ", " << default_color.indices[2] << "]";

            // Log all MMU tag counts
            std::map<std::string, size_t> mmu_debug;
            for (const TriangleAppearanceRecord &record : source_appearance.triangle_records)
                if (!record.payload.empty())
                    ++mmu_debug[record.payload];
            stream << "\nMMU tag distribution in source:";
            for (const auto &[tag, count] : mmu_debug)
                stream << "\n  tag='" << tag << "' count=" << count;

            colorcut_log(stream.str());
        }
    }

    colorcut_log_phase("PARALLEL TRIANGLE CLASSIFICATION for '" + target_volume.name + "'");
    colorcut_log("Starting parallel_for over " + std::to_string(num_triangles) + " triangles...");

    tbb::parallel_for(tbb::blocked_range<size_t>(0, num_triangles),
        [&](const tbb::blocked_range<size_t> &range) {
            for (size_t triangle_index = range.begin(); triangle_index < range.end(); ++triangle_index) {
                const Vec3d centroid = output_triangle_centroid(transformed_target_mesh, static_cast<int>(triangle_index));
                const Vec3d normal   = output_triangle_normal(transformed_target_mesh, static_cast<int>(triangle_index));

                TriangleResult &res = results[triangle_index];

                const TriangleAppearanceRecord *record = nullptr;
                int nearest_face_index = -1;
                // When the target triangle was carried over verbatim from a source triangle
                // (identical vertex positions), the source triangle is the authoritative
                // answer — including the case where that source triangle had no MMU paint.
                // Without this flag, the non-uniform path below would fall back to
                // find_nearest_source_record_fast() and contaminate unpainted body triangles
                // with paint from whichever painted neighbour happens to be nearest in
                // 3-space. KNN ordering depends on float roundtrip after rotation and is
                // not symmetric under +38° vs -38° rotations, which is why the chequer-board
                // pattern only appears for negative angles.
                bool exact_source_match = false;

                const auto exact_match = exact_source_triangle_lookup.find(triangle_geometry_key(transformed_target_mesh.its, static_cast<int>(triangle_index)));
                if (exact_match != exact_source_triangle_lookup.end()) {
                    nearest_face_index = exact_match->second;
                    record = find_triangle_record_with_appearance(source_appearance, nearest_face_index);
                    exact_source_match = true;
                }

                if (uniform_cap_color) {
                    if (cut_surface_classifier && cut_surface_classifier(centroid, normal)) {
                        res.cap_seed = true;
                        res.compatible_matches = 0;
                        res.classification = 1; // custom-cap
                        res.mmu_tag = dominant_mmu;
                        if (external_color_data.has_value())
                            res.color_binding = dominant_color;
                        continue;
                    }

                        if (planar_cut_surface && has_planar_surface_component && planar_surface_component[triangle_index]) {
                            res.cap_seed = true;
                            res.compatible_matches = 0;
                            res.classification = 2; // planar-cap
                            res.mmu_tag = dominant_mmu;
                            if (external_color_data.has_value())
                                res.color_binding = dominant_color;
                            continue;
                        }

                    // Groove cut surface detection: cut surface triangles are NEW geometry
                    // whose centroids are far from any source triangle. Use squared distance
                    // to the nearest source centroid to detect them.
                    if (groove_cut_surface) {
                        constexpr size_t K_GROOVE = 4;
                        auto nearest_groove = find_k_nearest_candidates(source_candidates, centroid, K_GROOVE);
                        double min_sq_dist = std::numeric_limits<double>::max();
                        for (const auto &[cand, sq_dist] : nearest_groove)
                            min_sq_dist = std::min(min_sq_dist, sq_dist);
                        res.min_sq_dist = min_sq_dist;
                        // Threshold: 0.5mm squared = 0.25. Carried-over triangles have distance ~0;
                        // groove cut surfaces (new geometry) are typically >1mm from any source triangle.
                        constexpr double groove_sq_dist_threshold = 0.25;
                        if (min_sq_dist > groove_sq_dist_threshold) {
                            res.cap_seed = true;
                            res.compatible_matches = 0;
                            res.classification = 3; // groove-dist-cap
                            res.mmu_tag = dominant_mmu;
                            if (external_color_data.has_value())
                                res.color_binding = dominant_color;
                            continue;
                        }
                    }

                    // Check if this face has any normal-compatible source match.
                    // Cap faces (new cut-surface geometry) will have normals incompatible
                    // with all nearby source faces, so no compatible match is found.
                    const size_t K = (source_candidates.size() <= 256) ? source_candidates.size() : 12;
                    auto nearest = find_k_nearest_candidates(source_candidates, centroid, K);
                    int compatible_matches = 0;
                    for (const auto &[cand, dist] : nearest) {
                        if (triangle_normals_compatible(cand->normal, normal)) {
                            ++compatible_matches;
                        }
                    }
                    // Also record min distance from the KNN search if not already set by groove
                    if (res.min_sq_dist < 0.0 && !nearest.empty())
                        res.min_sq_dist = nearest.front().second;
                    res.compatible_matches = compatible_matches;
                    if (compatible_matches == 0) {
                        // Cap face — use dominant color
                        res.cap_seed = true;
                        res.classification = 4; // normal-incompat-cap
                        res.mmu_tag = dominant_mmu;
                        if (external_color_data.has_value())
                            res.color_binding = dominant_color;
                        continue;
                    }
                }

                Vec3d nearest_point = centroid;
                if (record == nullptr && !exact_source_match)
                    source_indexed_mesh.squared_distance(centroid, nearest_face_index, nearest_point);

                if (record == nullptr && !exact_source_match && nearest_face_index >= 0) {
                    const Vec3d nearest_face_normal = triangle_normal(source_mesh.its, nearest_face_index);
                    if (triangle_normals_compatible(nearest_face_normal, normal))
                        record = find_triangle_record_with_appearance(source_appearance, nearest_face_index);
                }

                if (record == nullptr && !exact_source_match) {
                    record = find_nearest_source_record_fast(
                        source_appearance,
                        source_candidates,
                        centroid,
                        normal);
                    if (record != nullptr)
                        nearest_face_index = record->source_triangle_index;
                }

                if (record != nullptr && !record->payload.empty()) {
                    if (mmu_selector != nullptr && nearest_face_index >= 0) {
                        if (auto sampled_mmu_tag = sample_mmu_tag_at_point(*mmu_selector, nearest_face_index, nearest_point); sampled_mmu_tag.has_value()) {
                            res.mmu_tag = *sampled_mmu_tag;
                        }
                    }
                    if (res.mmu_tag.empty())
                        res.mmu_tag = simplify_mmu_to_dominant(record->payload);
                }

                if (external_color_data.has_value()) {
                    res.color_binding = default_color;
                    if (record != nullptr && record->triangle_color_pid >= 0) {
                        res.color_binding.pid = record->triangle_color_pid;
                        res.color_binding.indices[0] = record->color_indices[0];
                        res.color_binding.indices[1] = record->color_indices[1];
                        res.color_binding.indices[2] = record->color_indices[2];
                    }
                }
            }
        }
    );

    colorcut_log_phase("CLASSIFICATION RESULTS for '" + target_volume.name + "'");
    {
        // Classification counts
        size_t class_0_source_matched = 0;
        size_t class_1_warp_cap = 0;
        size_t class_2_planar_cap = 0;
        size_t class_3_groove_dist_cap = 0;
        size_t class_4_normal_incompat_cap = 0;
        size_t cap_seed_count = 0;
        size_t zero_compatible = 0;
        size_t low_compatible = 0;

        // Distance histogram for groove mode (sqrt of sq dist, in mm)
        // Buckets: [0, 0.1), [0.1, 0.25), [0.25, 0.5), [0.5, 1.0), [1.0, 2.0), [2.0, 5.0), [5.0, 10.0), [10+)
        size_t dist_buckets[8] = {};
        double dist_min = std::numeric_limits<double>::max();
        double dist_max = 0.0;
        double dist_sum = 0.0;
        size_t dist_count = 0;

        for (size_t i = 0; i < results.size(); ++i) {
            const auto &r = results[i];
            switch (r.classification) {
                case 0: ++class_0_source_matched; break;
                case 1: ++class_1_warp_cap; break;
                case 2: ++class_2_planar_cap; break;
                case 3: ++class_3_groove_dist_cap; break;
                case 4: ++class_4_normal_incompat_cap; break;
            }
            if (r.cap_seed) ++cap_seed_count;
            if (r.compatible_matches == 0) ++zero_compatible;
            if (r.compatible_matches <= 2) ++low_compatible;

            if (r.min_sq_dist >= 0.0) {
                double d = std::sqrt(r.min_sq_dist);
                dist_min = std::min(dist_min, d);
                dist_max = std::max(dist_max, d);
                dist_sum += d;
                ++dist_count;
                if (d < 0.1) dist_buckets[0]++;
                else if (d < 0.25) dist_buckets[1]++;
                else if (d < 0.5) dist_buckets[2]++;
                else if (d < 1.0) dist_buckets[3]++;
                else if (d < 2.0) dist_buckets[4]++;
                else if (d < 5.0) dist_buckets[5]++;
                else if (d < 10.0) dist_buckets[6]++;
                else dist_buckets[7]++;
            }
        }

        std::ostringstream stream;
        stream << "total_triangles: " << num_triangles
               << "\n\nCLASSIFICATION BREAKDOWN:"
               << "\n  class_0 (source-matched, kept original color): " << class_0_source_matched
               << "\n  class_1 (warp-cap, dominant color): " << class_1_warp_cap
               << "\n  class_2 (planar-cap, dominant color): " << class_2_planar_cap
               << "\n  class_3 (groove-dist-cap, dominant color): " << class_3_groove_dist_cap
               << "\n  class_4 (normal-incompat-cap, dominant color): " << class_4_normal_incompat_cap
               << "\n  total cap_seeds: " << cap_seed_count
               << "\n  zero_compatible: " << zero_compatible
               << "\n  low_compatible(<=2): " << low_compatible;

        if (dist_count > 0) {
            stream << "\n\nDISTANCE HISTOGRAM (min distance to nearest source triangle, in mm):"
                   << "\n  [0.00 - 0.10 mm): " << dist_buckets[0]
                   << "\n  [0.10 - 0.25 mm): " << dist_buckets[1]
                   << "\n  [0.25 - 0.50 mm): " << dist_buckets[2] << "  <-- groove threshold at 0.50mm"
                   << "\n  [0.50 - 1.00 mm): " << dist_buckets[3]
                   << "\n  [1.00 - 2.00 mm): " << dist_buckets[4]
                   << "\n  [2.00 - 5.00 mm): " << dist_buckets[5]
                   << "\n  [5.00 - 10.0 mm): " << dist_buckets[6]
                   << "\n  [10.0+       mm): " << dist_buckets[7]
                   << "\n  min_dist: " << std::fixed << std::setprecision(4) << dist_min << " mm"
                   << "\n  max_dist: " << dist_max << " mm"
                   << "\n  avg_dist: " << (dist_sum / double(dist_count)) << " mm"
                   << "\n  measured_triangles: " << dist_count;
        }

        // Log first 20 sample triangles for detailed inspection
        stream << "\n\nSAMPLE TRIANGLES (first 20):";
        for (size_t i = 0; i < std::min<size_t>(results.size(), 20); ++i) {
            const auto &r = results[i];
            const Vec3d c = output_triangle_centroid(transformed_target_mesh, static_cast<int>(i));
            const Vec3d n = output_triangle_normal(transformed_target_mesh, static_cast<int>(i));
            stream << "\n  tri[" << i << "] class=" << r.classification
                   << " cap=" << r.cap_seed
                   << " compat=" << r.compatible_matches
                   << " min_sq_dist=" << std::setprecision(6) << r.min_sq_dist
                   << " centroid=" << format_vec3(c)
                   << " normal=" << format_vec3(n)
                   << " mmu='" << r.mmu_tag << "'"
                   << " color_pid=" << r.color_binding.pid
                   << " color_idx=[" << r.color_binding.indices[0] << "," << r.color_binding.indices[1] << "," << r.color_binding.indices[2] << "]";
        }

        // Also log some triangles at the groove threshold boundary
        if (groove_cut_surface && dist_count > 0) {
            stream << "\n\nGROOVE BOUNDARY SAMPLES (triangles near threshold):";
            size_t near_threshold_count = 0;
            for (size_t i = 0; i < results.size() && near_threshold_count < 20; ++i) {
                const auto &r = results[i];
                if (r.min_sq_dist < 0.0) continue;
                double d = std::sqrt(r.min_sq_dist);
                // Log triangles with distance between 0.1 and 1.0 mm (around the threshold)
                if (d >= 0.1 && d <= 1.0) {
                    const Vec3d c = output_triangle_centroid(transformed_target_mesh, static_cast<int>(i));
                    const Vec3d n = output_triangle_normal(transformed_target_mesh, static_cast<int>(i));
                    stream << "\n  tri[" << i << "] dist=" << std::setprecision(4) << d << "mm"
                           << " class=" << r.classification
                           << " cap=" << r.cap_seed
                           << " compat=" << r.compatible_matches
                           << " centroid=" << format_vec3(c)
                           << " normal=" << format_vec3(n);
                    ++near_threshold_count;
                }
            }
            if (near_threshold_count == 0)
                stream << "\n  (none found in [0.1, 1.0] mm range)";
        }

        colorcut_log(stream.str());
        append_curve_log("[ColorCut] reapply stats: volume='" + target_volume.name + "'"
            + " total=" + std::to_string(num_triangles)
            + " cap_seeds=" + std::to_string(cap_seed_count)
            + " groove_caps=" + std::to_string(class_3_groove_dist_cap)
            + " normal_caps=" + std::to_string(class_4_normal_incompat_cap));
    }

    if (uniform_cap_color) {
        const bool has_dominant_mmu = !dominant_mmu.empty();
        const bool has_dominant_color = external_color_data.has_value();
        const bool has_dominant_assignment = has_dominant_mmu || has_dominant_color;
        const bool allow_cleanup_propagation = !groove_cut_surface;

        if (has_dominant_assignment) {
            auto is_dominant_result = [&](const TriangleResult &result) {
                const bool mmu_matches = !has_dominant_mmu || result.mmu_tag == dominant_mmu;
                const bool color_matches = !has_dominant_color
                    || (result.color_binding.pid == dominant_color.pid
                        && result.color_binding.indices == dominant_color.indices);
                return mmu_matches && color_matches;
            };

            auto assign_dominant_result = [&](TriangleResult &result) {
                if (has_dominant_mmu)
                    result.mmu_tag = dominant_mmu;
                if (has_dominant_color)
                    result.color_binding = dominant_color;
            };

            colorcut_log_phase("POST-PROCESS PASS 1: LOW-CONFIDENCE PROMOTION for '" + target_volume.name + "'");

            // Some cap triangles on one half of the cut can still find a small number of
            // normal-compatible source matches, so they never become hard seeds even though
            // they already carry the dominant cap assignment. Promote those low-confidence
            // dominant triangles into the seed set so the cleanup pass can propagate across
            // both cut surfaces.
            size_t promoted_count = 0;
            if (allow_cleanup_propagation) {
                for (size_t triangle_index = 0; triangle_index < results.size(); ++triangle_index) {
                    TriangleResult &result = results[triangle_index];
                    if (planar_cut_surface && has_planar_surface_component && !planar_surface_component[triangle_index])
                        continue;
                    if (!result.cap_seed && result.compatible_matches <= 2 && is_dominant_result(result)) {
                        result.cap_seed = true;
                        ++promoted_count;
                    }
                }
            } else {
                colorcut_log("Low-confidence promotion DISABLED for groove mode to avoid flooding the full shell");
            }
            colorcut_log("Low-confidence promotion: promoted " + std::to_string(promoted_count) + " triangles to cap_seed");

            colorcut_log_phase("POST-PROCESS PASS 2: COMPONENT UNIFICATION for '" + target_volume.name + "'");

            // If one cut surface never receives an initial dominant-colored seed,
            // detect it as a large connected patch of low-confidence matches and
            // unify that whole patch directly.
            if (!planar_cut_surface && allow_cleanup_propagation) {
                const int low_confidence_threshold = 4;
                const size_t min_component_size = std::max<size_t>(24, results.size() / 200);
                colorcut_log("Component unification ENABLED (planar_cut_surface=false)"
                    "\n  low_confidence_threshold: " + std::to_string(low_confidence_threshold)
                    + "\n  min_component_size: " + std::to_string(min_component_size));
                std::vector<unsigned char> visited(results.size(), 0);
                size_t total_unified = 0;
                size_t components_found = 0;
                for (size_t start_index = 0; start_index < results.size(); ++start_index) {
                    if (visited[start_index] || results[start_index].compatible_matches > low_confidence_threshold)
                        continue;

                    std::vector<size_t> component;
                    std::queue<size_t> pending;
                    pending.push(start_index);
                    visited[start_index] = 1;

                    while (!pending.empty()) {
                        const size_t triangle_index = pending.front();
                        pending.pop();
                        component.push_back(triangle_index);

                        for (int neighbor_index : adjacency[triangle_index]) {
                            const size_t neighbor = static_cast<size_t>(neighbor_index);
                            if (visited[neighbor] || results[neighbor].compatible_matches > low_confidence_threshold)
                                continue;
                            visited[neighbor] = 1;
                            pending.push(neighbor);
                        }
                    }

                    ++components_found;
                    if (component.size() < min_component_size) {
                        colorcut_log("  component starting at tri[" + std::to_string(start_index) + "] size=" + std::to_string(component.size()) + " — TOO SMALL, skip");
                        continue;
                    }

                    colorcut_log("  component starting at tri[" + std::to_string(start_index) + "] size=" + std::to_string(component.size()) + " — UNIFIED to dominant");
                    total_unified += component.size();

                    for (size_t triangle_index : component) {
                        TriangleResult &result = results[triangle_index];
                        assign_dominant_result(result);
                        result.cap_seed = true;
                    }
                }
                colorcut_log("Component unification summary: " + std::to_string(components_found) + " components found, " + std::to_string(total_unified) + " triangles unified");
            } else if (!allow_cleanup_propagation) {
                colorcut_log("Component unification DISABLED for groove mode to avoid promoting non-cut shell triangles");
            } else {
                colorcut_log("Component unification SKIPPED (planar_cut_surface=true)");
            }

            colorcut_log_phase("POST-PROCESS PASS 3: ADJACENCY ABSORPTION for '" + target_volume.name + "'");

            auto should_absorb_to_cap_region = [&](size_t triangle_index) {
                const TriangleResult &result = results[triangle_index];
                if (planar_cut_surface && has_planar_surface_component && !planar_surface_component[triangle_index])
                    return false;
                if (result.cap_seed || is_dominant_result(result) || result.compatible_matches > 2)
                    return false;

                bool touches_cap_region = false;
                int dominant_neighbors = 0;
                for (int neighbor_index : adjacency[triangle_index]) {
                    const TriangleResult &neighbor = results[static_cast<size_t>(neighbor_index)];
                    touches_cap_region = touches_cap_region || neighbor.cap_seed;
                    if (is_dominant_result(neighbor))
                        ++dominant_neighbors;
                }

                if (!touches_cap_region)
                    return false;

                return dominant_neighbors > 0;
            };

            if (allow_cleanup_propagation) {
                for (int pass = 0; pass < 4; ++pass) {
                    std::vector<size_t> promote_to_dominant;
                    for (size_t triangle_index = 0; triangle_index < results.size(); ++triangle_index) {
                        if (should_absorb_to_cap_region(triangle_index))
                            promote_to_dominant.push_back(triangle_index);
                    }

                    colorcut_log("  absorption pass " + std::to_string(pass) + ": " + std::to_string(promote_to_dominant.size()) + " triangles absorbed");

                    if (promote_to_dominant.empty())
                        break;

                    for (size_t triangle_index : promote_to_dominant) {
                        TriangleResult &result = results[triangle_index];
                        assign_dominant_result(result);
                        result.cap_seed = true;
                    }
                }
            } else {
                colorcut_log("Adjacency absorption DISABLED for groove mode to avoid leaking dominant color into preserved shell triangles");
            }
        }
    }

    // --- Single-threaded commit (MMU facets and color data are not thread-safe) ---
    colorcut_log_phase("COMMIT RESULTS for '" + target_volume.name + "'");

    // Final tally before commit
    {
        size_t final_cap_seeds = 0;
        std::map<std::string, size_t> final_mmu_distribution;
        std::map<std::string, size_t> final_color_distribution;
        for (size_t i = 0; i < results.size(); ++i) {
            if (results[i].cap_seed) ++final_cap_seeds;
            if (!results[i].mmu_tag.empty())
                ++final_mmu_distribution[results[i].mmu_tag];
            std::string ckey = std::to_string(results[i].color_binding.pid) + ":"
                + std::to_string(results[i].color_binding.indices[0]) + ":"
                + std::to_string(results[i].color_binding.indices[1]) + ":"
                + std::to_string(results[i].color_binding.indices[2]);
            ++final_color_distribution[ckey];
        }
        std::ostringstream stream;
        stream << "Final cap_seeds: " << final_cap_seeds << " / " << results.size()
               << "\n\nFinal MMU tag distribution:";
        for (const auto &[tag, count] : final_mmu_distribution)
            stream << "\n  '" << tag << "': " << count;
        stream << "\n\nFinal color distribution:";
        for (const auto &[key, count] : final_color_distribution)
            stream << "\n  " << key << ": " << count;
        colorcut_log(stream.str());
    }

    target_volume.mmu_segmentation_facets.reset();
    target_volume.mmu_segmentation_facets.reserve(static_cast<int>(num_triangles));

    ExternalVolumeColorData transferred_color_data;
    const bool track_triangle_colors = external_color_data.has_value();
    if (track_triangle_colors) {
        transferred_color_data.pid = external_color_data->pid;
        transferred_color_data.pindex = external_color_data->pindex;
        transferred_color_data.triangle_colors.reserve(num_triangles);
    }

    for (size_t triangle_index = 0; triangle_index < num_triangles; ++triangle_index) {
        const TriangleResult &res = results[triangle_index];
        if (!res.mmu_tag.empty())
            target_volume.mmu_segmentation_facets.set_triangle_from_string(static_cast<int>(triangle_index), res.mmu_tag);
        if (track_triangle_colors)
            transferred_color_data.triangle_colors.emplace_back(res.color_binding);
    }

    target_volume.mmu_segmentation_facets.shrink_to_fit();
    if (track_triangle_colors)
        repository.register_volume_color_data(target_volume.id().id, std::move(transferred_color_data));

    colorcut_log("COMMIT COMPLETE for '" + target_volume.name + "': " + std::to_string(num_triangles) + " triangles committed");
}

void ColorCutAttributeTransfer::apply_uniform_color_to_surface_planes(
    const ObjectAppearanceSnapshot &source_appearance,
    const ModelVolume &source_volume,
    const ModelObjectPtrs &target_objects,
    const std::vector<SurfacePlaneDefinition> &surface_planes,
    double plane_tolerance,
    double min_abs_normal_dot) const
{
    colorcut_log_phase("SURFACE-ONLY OVERRIDE ENTRY");
    {
        std::ostringstream stream;
        stream << "source_volume='" << source_volume.name << "'"
               << "\nsource_volume_id=" << source_volume.id().id
               << "\ntarget_objects=" << target_objects.size()
               << "\nplanes=" << surface_planes.size()
               << "\nplane_tolerance=" << plane_tolerance
               << "\nmin_abs_normal_dot=" << min_abs_normal_dot;
        for (size_t plane_index = 0; plane_index < surface_planes.size(); ++plane_index) {
            stream << "\n  plane[" << plane_index << "] point=" << format_vec3(surface_planes[plane_index].point)
                   << " normal=" << format_vec3(surface_planes[plane_index].normal.normalized());
        }
        colorcut_log(stream.str());
    }

    const VolumeAppearanceSnapshot *volume_snapshot = find_volume_snapshot(source_appearance, source_volume.id().id);
    if (volume_snapshot == nullptr) {
        colorcut_log("ERROR: apply_uniform_color_to_surface_planes could not find source snapshot");
        return;
    }

    ColorCutAttributeRepository &repository = global_color_cut_attribute_repository();
    const std::optional<ExternalVolumeColorData> source_color_data = repository.get_volume_color_data(source_volume.id().id);

    std::map<std::string, size_t> mmu_counts;
    for (const TriangleAppearanceRecord &record : volume_snapshot->triangle_records)
        if (!record.payload.empty())
            ++mmu_counts[record.payload];

    std::string dominant_mmu;
    if (!mmu_counts.empty())
        dominant_mmu = std::max_element(mmu_counts.begin(), mmu_counts.end(),
            [](const auto &a, const auto &b) { return a.second < b.second; })->first;

    TriangleColor dominant_color;
    dominant_color.pid = -1;
    dominant_color.indices[0] = -1;
    dominant_color.indices[1] = -1;
    dominant_color.indices[2] = -1;
    if (source_color_data.has_value()) {
        dominant_color.pid = source_color_data->pid;
        dominant_color.indices[0] = source_color_data->pindex;
        dominant_color.indices[1] = source_color_data->pindex;
        dominant_color.indices[2] = source_color_data->pindex;
    }

    for (size_t object_index = 0; object_index < target_objects.size(); ++object_index) {
        ModelObject *target_object = target_objects[object_index];
        if (target_object == nullptr)
            continue;

        colorcut_log_phase("SURFACE-ONLY OVERRIDE OBJECT [" + std::to_string(object_index) + "] " + target_object->name);

        for (size_t volume_index = 0; volume_index < target_object->volumes.size(); ++volume_index) {
            ModelVolume *target_volume = target_object->volumes[volume_index];
            if (target_volume == nullptr || !target_volume->is_model_part() || target_volume->is_cut_connector())
                continue;

            TriangleMesh transformed_target_mesh(target_volume->mesh());
            Transform3d target_transform = target_volume->get_matrix();
            if (target_volume->get_object() != nullptr && !target_volume->get_object()->instances.empty())
                target_transform = target_volume->get_object()->instances.front()->get_transformation().get_matrix_no_offset() * target_transform;
            transformed_target_mesh.transform(target_transform, true);

            const std::optional<ExternalVolumeColorData> existing_target_color_data = repository.get_volume_color_data(target_volume->id().id);

            std::vector<TriangleColor> final_triangle_colors;
            if (existing_target_color_data.has_value())
                final_triangle_colors = existing_target_color_data->triangle_colors;
            if (existing_target_color_data.has_value() && final_triangle_colors.size() < target_volume->mesh().its.indices.size())
                final_triangle_colors.resize(target_volume->mesh().its.indices.size(), dominant_color);

            const std::vector<std::vector<int>> adjacency = build_triangle_adjacency(target_volume->mesh().its);
            target_volume->mmu_segmentation_facets.shrink_to_fit();
            size_t overridden_triangles = 0;
            std::vector<size_t> plane_hit_counts(surface_planes.size(), 0);
            std::vector<unsigned char> overridden_mask(target_volume->mesh().its.indices.size(), 0);

            const double expanded_plane_tolerance = std::max(plane_tolerance * 6.0, 1.5);
            const double expanded_normal_dot = std::max(min_abs_normal_dot - 0.14, 0.78);

            for (size_t plane_index = 0; plane_index < surface_planes.size(); ++plane_index) {
                std::vector<unsigned char> seed_mask(target_volume->mesh().its.indices.size(), 0);
                std::queue<size_t> pending;
                size_t strict_seed_count = 0;
                size_t loose_seed_count = 0;

                for (size_t triangle_index = 0; triangle_index < target_volume->mesh().its.indices.size(); ++triangle_index) {
                    if (triangle_on_surface_plane(
                            transformed_target_mesh,
                            static_cast<int>(triangle_index),
                            surface_planes[plane_index],
                            plane_tolerance,
                            min_abs_normal_dot)) {
                        seed_mask[triangle_index] = 1;
                        pending.push(triangle_index);
                        ++strict_seed_count;
                    }
                }

                if (strict_seed_count == 0) {
                    for (size_t triangle_index = 0; triangle_index < target_volume->mesh().its.indices.size(); ++triangle_index) {
                        const double plane_distance = triangle_surface_plane_distance(
                            transformed_target_mesh,
                            static_cast<int>(triangle_index),
                            surface_planes[plane_index]);
                        const double normal_alignment = triangle_surface_plane_normal_alignment(
                            transformed_target_mesh,
                            static_cast<int>(triangle_index),
                            surface_planes[plane_index]);
                        if (plane_distance <= expanded_plane_tolerance * 0.5 && normal_alignment >= expanded_normal_dot) {
                            seed_mask[triangle_index] = 1;
                            pending.push(triangle_index);
                            ++loose_seed_count;
                        }
                    }
                }

                std::vector<unsigned char> visited(target_volume->mesh().its.indices.size(), 0);
                while (!pending.empty()) {
                    const size_t triangle_index = pending.front();
                    pending.pop();
                    if (visited[triangle_index])
                        continue;
                    visited[triangle_index] = 1;

                    const double plane_distance = triangle_surface_plane_distance(
                        transformed_target_mesh,
                        static_cast<int>(triangle_index),
                        surface_planes[plane_index]);
                    const double normal_alignment = triangle_surface_plane_normal_alignment(
                        transformed_target_mesh,
                        static_cast<int>(triangle_index),
                        surface_planes[plane_index]);

                    if (plane_distance > expanded_plane_tolerance || normal_alignment < expanded_normal_dot)
                        continue;

                    if (!overridden_mask[triangle_index]) {
                        overridden_mask[triangle_index] = 1;
                        ++overridden_triangles;
                        ++plane_hit_counts[plane_index];
                        if (!dominant_mmu.empty())
                            target_volume->mmu_segmentation_facets.set_triangle_from_string(static_cast<int>(triangle_index), dominant_mmu);
                        if (!final_triangle_colors.empty())
                            final_triangle_colors[triangle_index] = dominant_color;
                    }

                    for (int neighbor_index : adjacency[triangle_index]) {
                        const size_t neighbor = static_cast<size_t>(neighbor_index);
                        if (!visited[neighbor])
                            pending.push(neighbor);
                    }
                }

                std::ostringstream plane_stream;
                plane_stream << "  plane[" << plane_index << "] strict_seeds=" << strict_seed_count
                             << " loose_seeds=" << loose_seed_count
                             << " expanded_tolerance=" << expanded_plane_tolerance
                             << " expanded_normal_dot=" << expanded_normal_dot;
                colorcut_log(plane_stream.str());
            }

            if (existing_target_color_data.has_value()) {
                ExternalVolumeColorData updated_color_data = *existing_target_color_data;
                updated_color_data.triangle_colors = std::move(final_triangle_colors);
                repository.register_volume_color_data(target_volume->id().id, std::move(updated_color_data));
            }

            std::ostringstream stream;
            stream << "volume[" << volume_index << "] '" << target_volume->name << "' overridden_triangles=" << overridden_triangles;
            for (size_t plane_index = 0; plane_index < plane_hit_counts.size(); ++plane_index)
                stream << "\n  plane[" << plane_index << "] hits=" << plane_hit_counts[plane_index];
            colorcut_log(stream.str());
        }
    }
}

void ColorCutAttributeTransfer::apply_uniform_color_to_marked_triangles(
    const ObjectAppearanceSnapshot &source_appearance,
    const ModelVolume &source_volume,
    const ModelObjectPtrs &target_objects) const
{
    colorcut_log_phase("MARKED-SURFACE OVERRIDE ENTRY");
    {
        std::ostringstream stream;
        stream << "source_volume='" << source_volume.name << "'"
               << "\nsource_volume_id=" << source_volume.id().id
               << "\ntarget_objects=" << target_objects.size();
        colorcut_log(stream.str());
    }

    const VolumeAppearanceSnapshot *volume_snapshot = find_volume_snapshot(source_appearance, source_volume.id().id);
    if (volume_snapshot == nullptr) {
        colorcut_log("ERROR: apply_uniform_color_to_marked_triangles could not find source snapshot");
        return;
    }

    ColorCutAttributeRepository &repository = global_color_cut_attribute_repository();
    const std::optional<ExternalVolumeColorData> source_color_data = repository.get_volume_color_data(source_volume.id().id);

    std::map<std::string, size_t> mmu_counts;
    for (const TriangleAppearanceRecord &record : volume_snapshot->triangle_records)
        if (!record.payload.empty())
            ++mmu_counts[record.payload];

    std::string dominant_mmu;
    if (!mmu_counts.empty())
        dominant_mmu = std::max_element(mmu_counts.begin(), mmu_counts.end(),
            [](const auto &a, const auto &b) { return a.second < b.second; })->first;

    TriangleColor dominant_color;
    dominant_color.pid = -1;
    dominant_color.indices[0] = -1;
    dominant_color.indices[1] = -1;
    dominant_color.indices[2] = -1;
    if (source_color_data.has_value()) {
        dominant_color.pid = source_color_data->pid;
        dominant_color.indices[0] = source_color_data->pindex;
        dominant_color.indices[1] = source_color_data->pindex;
        dominant_color.indices[2] = source_color_data->pindex;
    }

    for (size_t object_index = 0; object_index < target_objects.size(); ++object_index) {
        ModelObject *target_object = target_objects[object_index];
        if (target_object == nullptr)
            continue;

        colorcut_log_phase("MARKED-SURFACE OVERRIDE OBJECT [" + std::to_string(object_index) + "] " + target_object->name);

        for (size_t volume_index = 0; volume_index < target_object->volumes.size(); ++volume_index) {
            ModelVolume *target_volume = target_object->volumes[volume_index];
            if (target_volume == nullptr || !target_volume->is_model_part() || target_volume->is_cut_connector())
                continue;

            const std::optional<ExternalVolumeColorData> existing_target_color_data = repository.get_volume_color_data(target_volume->id().id);

            std::vector<TriangleColor> final_triangle_colors;
            if (existing_target_color_data.has_value())
                final_triangle_colors = existing_target_color_data->triangle_colors;
            if (existing_target_color_data.has_value() && final_triangle_colors.size() < target_volume->mesh().its.indices.size())
                final_triangle_colors.resize(target_volume->mesh().its.indices.size(), dominant_color);

            size_t overridden_triangles = 0;
            size_t marked_triangles = 0;
            for (size_t triangle_index = 0; triangle_index < target_volume->mesh().its.indices.size(); ++triangle_index) {
                if (target_volume->exterior_facets.get_triangle_as_string(static_cast<int>(triangle_index)).empty())
                    continue;

                ++marked_triangles;
                if (!dominant_mmu.empty())
                    target_volume->mmu_segmentation_facets.set_triangle_from_string(static_cast<int>(triangle_index), dominant_mmu);

                if (existing_target_color_data.has_value() && triangle_index < final_triangle_colors.size())
                    final_triangle_colors[triangle_index] = dominant_color;

                ++overridden_triangles;
            }

            if (existing_target_color_data.has_value()) {
                ExternalVolumeColorData final_color_data = *existing_target_color_data;
                final_color_data.triangle_colors = std::move(final_triangle_colors);
                repository.register_volume_color_data(target_volume->id().id, std::move(final_color_data));
            }

            {
                std::ostringstream stream;
                stream << "volume[" << volume_index << "] '" << target_volume->name
                       << "' marked_triangles=" << marked_triangles
                       << " overridden_triangles=" << overridden_triangles;
                colorcut_log(stream.str());
            }

            target_volume->exterior_facets.reset();
        }
    }

    colorcut_log("apply_uniform_color_to_marked_triangles COMPLETE");
}

} // namespace ColorCut
} // namespace Slic3r
