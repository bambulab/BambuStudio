#include <cmath>

#include "ObjColorUtils.hpp"
#include "Model.hpp"
#include "TexturePainting.hpp"
#include "admesh/stl.h"
#include "libslic3r/Log.hpp"

bool obj_color_deal_algo(std::vector<Slic3r::RGBA> & input_colors,
                         std::vector<Slic3r::RGBA> & cluster_colors_from_algo,
                         std::vector<int> &         cluster_labels_from_algo,
                         char &                     cluster_number,
                         int                        max_cluster)
{
    QuantKMeans quant(10);
    quant.apply(input_colors, cluster_colors_from_algo, cluster_labels_from_algo, (int) cluster_number, max_cluster);
    if (cluster_number == -1) {
        return false;
    }
    return true;
}

// Grey stand-in for triangles that carry no usable color binding, matching the
// fallback the OBJ importer uses when a face has no material color. It takes part in
// clustering like any other color, and the painting resolves uncolored triangles to the
// filament picked for it (see Slic3r::STANDARD_3MF_UNCOLORED_RGB).
static const Slic3r::RGBA UNCOLORED_FACE_FALLBACK = {0.5f, 0.5f, 0.5f, 1.0f};

const std::string* standard_3mf_corner_color(
    const std::map<int, std::vector<std::string>>& color_group_map,
    const Slic3r::VolumeColorInfo& color_info,
    const Slic3r::TriangleColor& binding,
    int corner)
{
    // An out-of-range index falls back to the first color of its group. An empty color marks
    // an entry without color, e.g. a multiproperties entry made of textures only.
    auto color_at = [&color_group_map](int pid, int index) -> const std::string* {
        if (pid < 0)
            return nullptr;
        auto group = color_group_map.find(pid);
        if (group == color_group_map.end() || group->second.empty())
            return nullptr;
        const std::string& color = group->second[index >= 0 && index < static_cast<int>(group->second.size()) ? index : 0];
        return color.empty() ? nullptr : &color;
    };
    if (const std::string* color = color_at(binding.pid, binding.indices[corner]))
        return color;
    return color_at(color_info.pid, color_info.pindex);
}

// color_info is null for a part without usable colors; it is previewed all grey.
// has_uncolored is set when any triangle corner ends up grey.
static bool volume_to_textured_mesh(
    const Slic3r::ModelVolume& volume,
    const std::map<int, std::vector<std::string>>& color_group_map,
    const Slic3r::VolumeColorInfo* color_info,
    Slic3r::TexturedMesh& out,
    bool& has_colored,
    bool& has_uncolored)
{
    out = Slic3r::TexturedMesh();
    const indexed_triangle_set& its = volume.mesh().its;
    const size_t face_count = its.indices.size();
    if (its.vertices.empty() || face_count == 0)
        return false;

    std::unordered_map<std::string, Slic3r::RGBA> color_cache;
    auto to_rgba = [&color_cache](const std::string& color_str) {
        auto iter = color_cache.find(color_str);
        if (iter == color_cache.end())
            iter = color_cache.emplace(color_str, Slic3r::convert_color_string_to_rgba(color_str)).first;
        return iter->second;
    };

    std::vector<std::array<float, 4>> vertex_colors(face_count * 3, UNCOLORED_FACE_FALLBACK);
    std::vector<std::array<std::size_t, 3>> face_colors(face_count, Slic3r::STANDARD_3MF_UNCOLORED_RGB);
    size_t colored_faces = 0;

    for (size_t face_idx = 0; color_info != nullptr && face_idx < face_count; ++face_idx) {
        const Slic3r::TriangleColor& binding = color_info->triangle_colors[face_idx];

        // p1 / p2 / p3 bind the palette to v1 / v2 / v3. The importer already copied p1 into
        // the missing slots, so a face color arrives here as three identical indices.
        std::array<Slic3r::RGBA, 3> corner_colors;
        int colored_corners = 0;
        for (int corner = 0; corner < 3; ++corner) {
            const std::string* color_str = standard_3mf_corner_color(color_group_map, *color_info, binding, corner);
            corner_colors[corner] = color_str != nullptr ? to_rgba(*color_str) : UNCOLORED_FACE_FALLBACK;
            if (color_str != nullptr)
                ++colored_corners;
        }
        if (colored_corners == 0)
            continue;
        if (colored_corners < 3)
            has_uncolored = true;
        ++colored_faces;

        for (int corner = 0; corner < 3; ++corner)
            vertex_colors[face_idx * 3 + corner] = corner_colors[corner];
        for (int channel = 0; channel < 3; ++channel) {
            const float averaged = (corner_colors[0][channel] + corner_colors[1][channel] +
                                    corner_colors[2][channel]) / 3.0f * 255.0f;
            face_colors[face_idx][channel] = static_cast<std::size_t>(std::lround(std::clamp(averaged, 0.0f, 255.0f)));
        }
    }

    // A shared vertex can carry a different color in each triangle that uses it, so every
    // triangle gets its own corners. A corner-colored triangle is only split where its
    // corners differ, so face-colored parts still preview as solid faces.
    out.vertices.resize(face_count * 3);
    out.indices.resize(face_count);
    for (size_t i = 0; i < face_count; ++i) {
        for (int corner = 0; corner < 3; ++corner) {
            const Slic3r::Vec3f& v = its.vertices[its.indices[i][corner]];
            out.vertices[i * 3 + corner] = {v.x(), v.y(), v.z()};
        }
        out.indices[i] = {int(i * 3), int(i * 3 + 1), int(i * 3 + 2)};
    }
    out.precomputed_face_colors   = std::move(face_colors);
    out.precomputed_vertex_colors = std::move(vertex_colors);

    if (colored_faces > 0)
        has_colored = true;
    if (colored_faces < face_count)
        has_uncolored = true;

    BOOST_LOG_TRIVIAL(info) << "built textured mesh from standard 3mf colors, faces=" << face_count
                            << ", colored faces=" << colored_faces;
    return true;
}

static void append_textured_mesh(Slic3r::TexturedMesh& dst, Slic3r::TexturedMesh&& src, const Slic3r::Transform3d* world)
{
    const int base = static_cast<int>(dst.vertices.size());
    dst.vertices.reserve(dst.vertices.size() + src.vertices.size());
    for (auto& v : src.vertices) {
        if (world) {
            const Slic3r::Vec3d p = (*world) * Slic3r::Vec3d(v[0], v[1], v[2]);
            v = {static_cast<float>(p.x()), static_cast<float>(p.y()), static_cast<float>(p.z())};
        }
        dst.vertices.push_back(v);
    }
    dst.indices.reserve(dst.indices.size() + src.indices.size());
    for (auto& f : src.indices) {
        dst.indices.push_back({f[0] + base, f[1] + base, f[2] + base});
    }
    dst.precomputed_face_colors.insert(dst.precomputed_face_colors.end(),
                                        src.precomputed_face_colors.begin(), src.precomputed_face_colors.end());
    dst.precomputed_vertex_colors.insert(dst.precomputed_vertex_colors.end(),
                                         src.precomputed_vertex_colors.begin(), src.precomputed_vertex_colors.end());
}

bool standard_3mf_to_textured_meshes(
    const Slic3r::Model& model,
    const std::map<int, std::vector<std::string>>& color_group_map,
    const std::unordered_map<int, Slic3r::VolumeColorInfo>& volume_color_data,
    Slic3r::TexturedMesh& out,
    bool& has_uncolored)
{
    out = Slic3r::TexturedMesh();
    has_uncolored = false;

    struct ColoredPart {
        const Slic3r::ModelObject* object;
        const Slic3r::ModelVolume* volume;
        const Slic3r::VolumeColorInfo* color_info;
    };
    std::vector<ColoredPart> parts;
    for (const Slic3r::ModelObject* object : model.objects) {
        if (object == nullptr)
            continue;
        for (const Slic3r::ModelVolume* volume : object->volumes) {
            if (volume == nullptr || !volume->is_model_part())
                continue;
            // Parts without usable colors are kept too, so the preview shows the whole model.
            auto color_iter = volume_color_data.find(volume->id().id);
            const bool colored = color_iter != volume_color_data.end() &&
                                 color_iter->second.triangle_colors.size() == volume->mesh().its.indices.size();
            parts.push_back({object, volume, colored ? &color_iter->second : nullptr});
        }
    }
    if (parts.empty())
        return false;

    const bool bake_world = parts.size() > 1;
    int appended = 0;
    bool colored = false;
    for (const ColoredPart& part : parts) {
        Slic3r::TexturedMesh piece;
        if (!volume_to_textured_mesh(*part.volume, color_group_map, part.color_info, piece, colored, has_uncolored))
            continue;
        Slic3r::Transform3d world = Slic3r::Transform3d::Identity();
        const Slic3r::Transform3d* world_ptr = nullptr;
        if (bake_world) {
            world = part.volume->get_transformation().get_matrix();
            if (!part.object->instances.empty() && part.object->instances.front() != nullptr)
                world = part.object->instances.front()->get_transformation().get_matrix() * world;
            world_ptr = &world;
        }
        append_textured_mesh(out, std::move(piece), world_ptr);
        ++appended;
    }
    out.is_standard_3mf = true;
    return colored && appended > 0 && !out.vertices.empty();
}