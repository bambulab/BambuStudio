#include <boost/log/trivial.hpp>

#include "ObjColorUtils.hpp"
#include "Model.hpp"
#include "admesh/stl.h"

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

// Check if all colors are undefined.
bool check_is_all_undefined_color(const std::vector<Slic3r::RGBA>& colors) {
    for (const Slic3r::RGBA& color : colors) {
        if (!Slic3r::color_is_equal(color, Slic3r::UNDEFINE_COLOR)) {
            return false;
        }
    }
    return true;
}

// Construct the color of each face from TriangleColors and color_group_map
Slic3r::RGBA get_face_color_from_binding(
    const Slic3r::TriangleColor& binding,
    const std::unordered_map<int, std::vector<std::string>>& color_group_map,
    const std::unordered_map<std::string, Slic3r::RGBA>& color_str_to_rgba)
{
    // If the PID is invalid, return "undefined color".
    if (binding.pid < 0) {
        return Slic3r::UNDEFINE_COLOR;
    }
    // Find color group
    auto group_iter = color_group_map.find(binding.pid);
    if (group_iter == color_group_map.end() || group_iter->second.empty()) {
        return Slic3r::UNDEFINE_COLOR;
    }
    // Check if there are vertex - level colors(p1 / p2 / p3 are different).
    bool has_vertex_colors = false;
    for (int i = 0; i < 3; ++i) {
        if (binding.indices[i] >= 0 &&
            binding.indices[i] < int(group_iter->second.size())) {
            has_vertex_colors = true;
            break;
        }
    }
    // If vertex colors are available, use the color of the first vertex as the representative color of the face
    // (For vertex color cases, ObjColorDialog will use deal_vertex_color = true)
    if (has_vertex_colors && binding.indices[0] >= 0 &&
        binding.indices[0] < int(group_iter->second.size())) {
        const std::string& color_str = group_iter->second[binding.indices[0]];
        auto rgba_iter = color_str_to_rgba.find(color_str);
        if (rgba_iter != color_str_to_rgba.end()) {
            return rgba_iter->second;
        }
    }
    // Otherwise, use the default color(pindex or the first color).
    if (!group_iter->second.empty()) {
        const std::string& color_str = group_iter->second[0];
        auto rgba_iter = color_str_to_rgba.find(color_str);
        if (rgba_iter != color_str_to_rgba.end()) {
            return rgba_iter->second;
        }
    }
    return Slic3r::UNDEFINE_COLOR;
}

bool extract_colors_to_obj_dialog(
    Slic3r::Model* model,
    const std::unordered_map<int, std::vector<std::string>>& color_group_map,
    const std::unordered_map<int, Slic3r::VolumeColorInfo>& volume_color_data,
    Slic3r::ObjDialogInOut& out)
{
    if (!model || model->objects.empty() || color_group_map.empty() || volume_color_data.empty()) {
        return false;
    }
    // Pre-convert all color strings to RGBA
    std::vector<Slic3r::RGBA> unique_colors;
    std::unordered_map<std::string, Slic3r::RGBA> color_str_to_rgba;
    for (const auto& pair : color_group_map) {
        std::vector<Slic3r::RGBA> group_colors;
        for (const auto& color_str : pair.second) {
            Slic3r::RGBA rgba = Slic3r::convert_color_string_to_rgba(color_str);
            color_str_to_rgba[color_str] = rgba;
            group_colors.emplace_back(rgba);
            bool found = false;
            for (const auto& existing : unique_colors) {
                if (Slic3r::color_is_equal(existing, rgba)) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                unique_colors.push_back(rgba);
            }
        }
        out.color_group_map.insert({ pair.first, std::move(group_colors) });
    }

    out.first_time_using_makerlab = false;
    out.lost_material_name = "";
    out.mtl_colors = unique_colors;

    // Used to obtain a list of unique colors currently in used
    std::unordered_map<int, std::vector<Slic3r::RGBA>> volumes_used_colors;
    for (auto& obj : model->objects) {
        for (auto& volume : obj->volumes) {
            if (!volume->is_model_part()) {
                continue;
            }
            // Extracting vertex colors from TriangleColors
            auto vol_color_iter = volume_color_data.find(volume->id().id);
            if (vol_color_iter == volume_color_data.end()) {
                continue;
            }
            const Slic3r::TriangleMesh& src_mesh = volume->mesh();
            size_t face_count = src_mesh.its.indices.size();
            const Slic3r::VolumeColorInfo& vol_color_info = vol_color_iter->second;
            // Check if the number of TriangleColorBindings matches.
            if (vol_color_info.triangle_colors.size() != face_count) {
                continue;
            }
            bool deal_vertex_color = false;
            std::vector<Slic3r::RGBA> used_colors;
            // Definition Used by face_color mode
#pragma region
            int current_start_face_idx = -1;
            int current_end_face_idx = -1;
            bool current_color_valid = false;
            Slic3r::RGBA current_color = Slic3r::UNDEFINE_COLOR;
            std::map<std::pair<int, int>, Slic3r::RGBA> split_mesh_with_color;
#pragma endregion 
            // Definition Used by vertex_color mode
#pragma region
            Slic3r::TriangleMesh new_mesh;
            indexed_triangle_set new_mesh_its;
            new_mesh_its.vertices.reserve(face_count * 3);
            new_mesh_its.indices.reserve(face_count);
            std::vector<Slic3r::RGBA> vertex_colors;
            vertex_colors.reserve(face_count * 3);
#pragma endregion 
            // Collect colors used by volume
            for (size_t face_idx = 0; face_idx < face_count; ++face_idx) {
                const Slic3r::TriangleColor& binding = vol_color_info.triangle_colors[face_idx];
                const auto& face = src_mesh.its.indices[face_idx];
                // Get color group
                if (binding.pid < 0) {
                    continue;
                }
                auto group_iter = color_group_map.find(binding.pid);
                if (group_iter == color_group_map.end()) {
                    continue;
                }
                new_mesh_its.indices.emplace_back(face);
                // Check if has different vertex color
                if (binding.indices[0] != binding.indices[1] || binding.indices[1] != binding.indices[2] || binding.indices[0] != binding.indices[2]) {
                    deal_vertex_color = true;
                    // Set color for each vertex
                    for (int i = 0; i < 3; ++i) {
                        new_mesh_its.vertices.emplace_back(src_mesh.its.vertices[face[i]]);
                        int color_idx = binding.indices[i];
                        if (color_idx >= 0 && color_idx < static_cast<int>(group_iter->second.size())) {
                            const std::string& color_str = group_iter->second[color_idx];
                            auto rgba_iter = color_str_to_rgba.find(color_str);
                            if (rgba_iter != color_str_to_rgba.end()) {
                                vertex_colors.emplace_back(rgba_iter->second);
                                auto exist_iter = std::find(used_colors.begin(), used_colors.end(), rgba_iter->second);
                                if (exist_iter == used_colors.end()) {
                                    used_colors.push_back(rgba_iter->second);
                                }
                            }
                        }
                    }
                }
                else {
                    // Use TriangleColorBinding to construct the color of face.
                    Slic3r::RGBA face_color = get_face_color_from_binding(binding, color_group_map, color_str_to_rgba);
                    auto exist_iter = std::find(used_colors.begin(), used_colors.end(), face_color);
                    if (exist_iter == used_colors.end()) {
                        used_colors.push_back(face_color);
                    }
                    for (int i = 0; i < 3; ++i) {
                        new_mesh_its.vertices.emplace_back(src_mesh.its.vertices[face[i]]);
                        vertex_colors.emplace_back(face_color);
                    }
                    // If it's already in deal_vertex_color mode, do not collect data anymore
                    if (deal_vertex_color){
                        continue;
                    }
                    // Split TriangleMesh by different triangle color
                    bool color_changed = false;
                    if (!current_color_valid) {
                        // Start new sub-mesh if color is valid
                        if (!Slic3r::color_is_equal(face_color, Slic3r::UNDEFINE_COLOR)) {
                            current_color = face_color;
                            current_color_valid = true;
                            current_start_face_idx = face_idx;
                        }
                    }
                    else {
                        // Check if color is different from current
                        if (Slic3r::color_is_equal(face_color, Slic3r::UNDEFINE_COLOR) || !Slic3r::color_is_equal(face_color, current_color)) {
                            color_changed = true;
                        }
                    }
                    if (color_changed) {
                        current_end_face_idx = face_idx - 1;
                        std::pair<int, int> index_range{ current_start_face_idx, current_end_face_idx };
                        split_mesh_with_color[index_range] = current_color;
                        // Start new sub-mesh if current face has valid color
                        if (!Slic3r::color_is_equal(face_color, Slic3r::UNDEFINE_COLOR)) {
                            current_color = face_color;
                            current_color_valid = true;
                            current_start_face_idx = face_idx;
                        }
                        else {
                            current_color_valid = false;
                        }
                    }
                }
            }

            if (used_colors.empty() || check_is_all_undefined_color(used_colors)) {
                continue;
            }
            volumes_used_colors[volume->id().id] = used_colors;

            volume->set_origin_mesh_render_type(!deal_vertex_color);
            if (nullptr == volume->origin_render_info_ptr) {
                volume->origin_render_info_ptr = std::make_shared<Slic3r::OriginRenderInfo>();
            }

            if (deal_vertex_color) {
                auto new_face_count = new_mesh_its.vertices.size() / 3;
                new_mesh_its.indices.clear();
                for (int i = 0; i < new_face_count; i++) {
                    auto temp_face = stl_triangle_vertex_indices(i * 3 + 0, i * 3 + 1, i * 3 + 2);
                    new_mesh_its.indices.emplace_back(temp_face);
                }
                new_mesh.its = new_mesh_its;
                volume->origin_render_info_ptr->vertices_with_colors.first = std::move(new_mesh);
                volume->origin_render_info_ptr->vertices_with_colors.second = std::move(vertex_colors);
            }
            else {
                new_mesh.its = src_mesh.its;
                // If exist last untracked face_idx range or all triangles use one color
                if (current_start_face_idx > current_end_face_idx && current_start_face_idx < face_count && current_color_valid) {
                    // Neen't split use origin mesh
                    if (current_start_face_idx == 0) {
                        volume->origin_render_info_ptr->mesh_with_colors.emplace_back(std::move(new_mesh), current_color);
                    }
                    else {
                        split_mesh_with_color[{current_start_face_idx, face_count - 1}] = current_color;
                    }
                }
                // Merge all ranges with the same color and rebuild vertices / indices.
                // Vertices are allowed to be duplicated.
                std::vector<std::pair<Slic3r::RGBA, Slic3r::TriangleMesh>> color_meshes;
                for (auto& split_iter : split_mesh_with_color) {
                    const auto& index_range = split_iter.first;
                    const auto& color = split_iter.second;

                    // Find or create mesh for this color
                    auto mesh_it = std::find_if(color_meshes.begin(), color_meshes.end(),
                        [&color](const std::pair<Slic3r::RGBA, Slic3r::TriangleMesh>& item) {
                            return Slic3r::color_is_equal(item.first, color);
                    });
                    if (mesh_it == color_meshes.end()) {
                        color_meshes.emplace_back(color, Slic3r::TriangleMesh{});
                        mesh_it = std::prev(color_meshes.end());
                    }

                    Slic3r::TriangleMesh& color_mesh = mesh_it->second;
                    indexed_triangle_set& color_its = color_mesh.its;

                    // Copy triangles in [start, end] and rebuild vertices / indices.
                    for (int face_idx = index_range.first; face_idx <= index_range.second; ++face_idx) {
                        const auto& face = src_mesh.its.indices[face_idx];
                        int base_index = static_cast<int>(color_its.vertices.size());
                        // Add 3 vertices for this triangle
                        for (int i = 0; i < 3; ++i) {
                            color_its.vertices.emplace_back(src_mesh.its.vertices[face[i]]);
                        }
                        // Add triangle indices referencing the newly added vertices
                        auto new_face = stl_triangle_vertex_indices(base_index + 0, base_index + 1, base_index + 2);
                        color_its.indices.emplace_back(new_face);
                    }
                }
                // Save merged meshes with colors
                for (auto& pair : color_meshes) {
                    volume->origin_render_info_ptr->mesh_with_colors.emplace_back(std::move(pair.second), pair.first);
                }
            }
        }
    }

    if (volumes_used_colors.empty()) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": all colors are undefined";
        return false;
    }

    for (auto& vol_iter : volumes_used_colors) {
        for (auto& color : vol_iter.second) {
            auto iter = std::find(out.input_colors.begin(), out.input_colors.end(), color);
            if (iter != out.input_colors.end()) {
                continue;
            }
            out.input_colors.push_back(color);
        }
    }
    out.is_single_color = (out.input_colors.size() == 1);
    return true;
}