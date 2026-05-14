#include "FBX.hpp"
#include "../TexturePainting.hpp"
#include "../I18N.hpp"

#include "ResourcePathUtils.hpp"

#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <boost/log/trivial.hpp>
#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>

#include <algorithm>
#include <cstring>
#include <exception>
#include <map>
#include <memory>
#include <sstream>
#include <vector>

namespace Slic3r {

namespace {

constexpr unsigned int DEFAULT_FBX_FLAGS = aiProcess_Triangulate
                                         | aiProcess_GenNormals
                                         | aiProcess_JoinIdenticalVertices
                                         | aiProcess_FlipUVs
                                         | aiProcess_PreTransformVertices;

struct FbxImportAttempt
{
    const char*  name = nullptr;
    unsigned int flags = 0;
    bool         configure_fbx_properties = false;
    bool         read_from_memory = false;
};

void set_error_message(std::string* error_message, const std::string& message)
{
    if (error_message)
        *error_message = message;
}

void clear_textured_mesh(TexturedMesh& out)
{
    out.vertices.clear();
    out.indices.clear();
    out.uvs.clear();
    out.uv_coords.clear();
    out.uv_indices.clear();
    out.textures.clear();
    out.material_ids.clear();
    out.material_texture_map.clear();
    out.material_colors.clear();
}

size_t count_node_mesh_refs(const aiNode* node)
{
    if (!node)
        return 0;

    size_t count = node->mNumMeshes;
    for (unsigned int i = 0; i < node->mNumChildren; ++i)
        count += count_node_mesh_refs(node->mChildren[i]);
    return count;
}

std::string scene_summary(const aiScene* scene)
{
    if (!scene)
        return "scene=null";

    std::ostringstream ss;
    ss << "meshes=" << scene->mNumMeshes
       << ", materials=" << scene->mNumMaterials
       << ", textures=" << scene->mNumTextures
       << ", root_node=" << (scene->mRootNode ? "yes" : "no")
       << ", node_mesh_refs=" << count_node_mesh_refs(scene->mRootNode)
       << ", flags=" << scene->mFlags;
    return ss.str();
}

std::string mesh_summary(const aiScene* scene)
{
    if (!scene || !scene->mMeshes)
        return "mesh_summary=unavailable";

    std::ostringstream ss;
    ss << "mesh_summary=[";
    const unsigned int max_meshes_to_log = std::min(scene->mNumMeshes, 12u);
    for (unsigned int i = 0; i < max_meshes_to_log; ++i) {
        const aiMesh* mesh = scene->mMeshes[i];
        if (i > 0)
            ss << "; ";
        ss << "#" << i;
        if (mesh) {
            ss << " vertices=" << mesh->mNumVertices
               << " faces=" << mesh->mNumFaces
               << " primitive_types=" << mesh->mPrimitiveTypes
               << " material=" << mesh->mMaterialIndex;
        } else {
            ss << " null";
        }
    }
    if (scene->mNumMeshes > max_meshes_to_log)
        ss << "; ... total=" << scene->mNumMeshes;
    ss << "]";
    return ss.str();
}

std::string importer_summary(const Assimp::Importer& importer)
{
    aiString extensions;
    importer.GetExtensionList(extensions);
    const std::string extension_list = extensions.C_Str();
    const size_t fbx_importer_index = importer.GetImporterIndex("fbx");

    std::ostringstream ss;
    ss << "importer_count=" << importer.GetImporterCount()
       << ", fbx_importer="
       << (fbx_importer_index == static_cast<size_t>(-1) ? "not_found" : std::to_string(fbx_importer_index))
       << ", extension_list_has_fbx=" << (extension_list.find("fbx") != std::string::npos ? "yes" : "no");
    return ss.str();
}

std::string importer_exception_summary(const Assimp::Importer& importer)
{
    const std::exception_ptr& exception = importer.GetException();
    if (!exception)
        return "exception=none";

    try {
        std::rethrow_exception(exception);
    } catch (const std::exception& e) {
        return std::string("exception=") + e.what();
    } catch (...) {
        return "exception=non-standard";
    }
}

std::string fbx_file_summary(const std::string& path)
{
    std::ostringstream ss;
    try {
        const boost::filesystem::path fs_path(path);
        const bool exists = boost::filesystem::exists(fs_path);
        ss << "file_exists=" << (exists ? "yes" : "no");
        if (exists)
            ss << ", file_size=" << boost::filesystem::file_size(fs_path);

        char header[27] = {};
        boost::nowide::ifstream file(path, std::ios::binary);
        file.read(header, sizeof(header));
        const std::streamsize bytes_read = file.gcount();
        ss << ", header_bytes=" << bytes_read;
        if (bytes_read >= 18) {
            const bool binary_fbx = std::memcmp(header, "Kaydara FBX Binary", 18) == 0;
            ss << ", binary_fbx=" << (binary_fbx ? "yes" : "no");
        }
        if (bytes_read >= 27) {
            const unsigned char* b = reinterpret_cast<const unsigned char*>(header);
            const uint32_t version = static_cast<uint32_t>(b[23])
                                   | (static_cast<uint32_t>(b[24]) << 8)
                                   | (static_cast<uint32_t>(b[25]) << 16)
                                   | (static_cast<uint32_t>(b[26]) << 24);
            ss << ", fbx_version=" << version;
        }
    } catch (const std::exception& e) {
        ss << "file_summary_error=" << e.what();
    }
    return ss.str();
}

bool read_file_bytes(const std::string& path, std::vector<char>& bytes, std::string& error)
{
    boost::nowide::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        error = "failed to open file for memory import";
        return false;
    }

    const std::streamoff size = file.tellg();
    if (size <= 0) {
        error = "file is empty or size could not be determined for memory import";
        return false;
    }

    bytes.resize(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    file.read(bytes.data(), size);
    if (!file) {
        error = "failed to read complete file for memory import";
        return false;
    }

    return true;
}

void configure_fbx_importer(Assimp::Importer& importer)
{
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_ALL_GEOMETRY_LAYERS, true);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_MATERIALS, true);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_TEXTURES, true);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_ANIMATIONS, false);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_LIGHTS, false);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_CAMERAS, false);
}

boost::filesystem::path resolve_fbx_external_texture_path(
    const boost::filesystem::path& base_dir,
    const std::string& texture_path)
{
    const boost::filesystem::path requested_path(texture_path);
    boost::filesystem::path resolved_path = requested_path.is_absolute() ?
        resource_path::resolve_existing_path_case_insensitive(requested_path, "FBX: texture") :
        resource_path::resolve_existing_relative_path_case_insensitive(base_dir, requested_path, "FBX: texture");

    if (!resolved_path.empty())
        return resolved_path;

    // Some exporters keep stale folder names in FBX texture paths. Preserve the
    // existing filename-only fallback, but make it case-insensitive as well.
    return resource_path::resolve_existing_relative_path_case_insensitive(
        base_dir, requested_path.filename(), "FBX: texture filename");
}

void collect_mesh(const aiScene* scene,
                  const aiMesh* mesh,
                  const aiMatrix4x4& transform,
                  bool apply_transform,
                  uint32_t& vertex_offset,
                  TexturedMesh& out)
{
    (void)scene;
    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
        const aiVector3D v = apply_transform ? transform * mesh->mVertices[i] : mesh->mVertices[i];
        out.vertices.push_back({v.x, v.y, v.z});

        if (mesh->mTextureCoords[0]) {
            const auto& uv = mesh->mTextureCoords[0][i];
            out.uvs.push_back({uv.x, uv.y});
        } else {
            out.uvs.push_back({0.f, 0.f});
        }
    }

    int mat_idx = static_cast<int>(mesh->mMaterialIndex);

    for (unsigned int i = 0; i < mesh->mNumFaces; ++i) {
        const auto& face = mesh->mFaces[i];
        if (face.mNumIndices != 3)
            continue;
        int i0 = static_cast<int>(face.mIndices[0]) + vertex_offset;
        int i1 = static_cast<int>(face.mIndices[1]) + vertex_offset;
        int i2 = static_cast<int>(face.mIndices[2]) + vertex_offset;
        out.indices.push_back({i0, i1, i2});
        out.material_ids.push_back(mat_idx);
    }

    vertex_offset += mesh->mNumVertices;
}

void process_node(const aiScene* scene,
                  const aiNode* node,
                  const aiMatrix4x4& parent_transform,
                  bool apply_transforms,
                  uint32_t& vertex_offset,
                  TexturedMesh& out)
{
    const aiMatrix4x4 node_transform = parent_transform * node->mTransformation;
    for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
        const aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        collect_mesh(scene, mesh, node_transform, apply_transforms, vertex_offset, out);
    }
    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        process_node(scene, node->mChildren[i], node_transform, apply_transforms, vertex_offset, out);
    }
}

} // anonymous namespace

bool load_fbx(const std::string& path, TexturedMesh& out, std::string* error_message)
{
    clear_textured_mesh(out);

    std::vector<FbxImportAttempt> attempts = {
        {"default", DEFAULT_FBX_FLAGS, false, false},
#ifdef __APPLE__
        {"macOS memory fbx hint", DEFAULT_FBX_FLAGS, true, true},
        {"macOS no pre-transform", DEFAULT_FBX_FLAGS & ~aiProcess_PreTransformVertices, true, false},
        {"macOS conservative geometry", aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_FlipUVs, true, false},
        {"macOS conservative memory fbx hint", aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_FlipUVs, true, true},
#endif
    };

    std::unique_ptr<Assimp::Importer> active_importer;
    const aiScene* scene = nullptr;
    const char* selected_attempt = nullptr;
    unsigned int selected_flags = 0;
    std::string last_error;

    for (const FbxImportAttempt& attempt : attempts) {
        auto importer = std::make_unique<Assimp::Importer>();
        if (attempt.configure_fbx_properties)
            configure_fbx_importer(*importer);

        std::vector<char> file_buffer;
        const aiScene* attempt_scene = nullptr;
        if (attempt.read_from_memory) {
            std::string read_error;
            if (!read_file_bytes(path, file_buffer, read_error)) {
                std::ostringstream ss;
                ss << "FBX import failed before Assimp ReadFileFromMemory"
                   << " (attempt=" << attempt.name
                   << ", flags=" << attempt.flags
                   << "): " << read_error
                   << ", " << fbx_file_summary(path);
                last_error = ss.str();
                BOOST_LOG_TRIVIAL(error) << "FBX: " << last_error << ", path=" << path;
                continue;
            }
            attempt_scene = importer->ReadFileFromMemory(file_buffer.data(), file_buffer.size(), attempt.flags, "fbx");
        } else {
            attempt_scene = importer->ReadFile(path, attempt.flags);
        }

        if (!attempt_scene || (attempt_scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !attempt_scene->mRootNode) {
            const std::string assimp_error = importer->GetErrorString();
            std::ostringstream ss;
            ss << "FBX import failed during Assimp " << (attempt.read_from_memory ? "ReadFileFromMemory" : "ReadFile")
               << " (attempt=" << attempt.name
               << ", flags=" << attempt.flags
               << "): " << (assimp_error.empty() ? "<empty Assimp error>" : assimp_error)
               << ", " << importer_exception_summary(*importer)
               << ", " << importer_summary(*importer)
               << ", " << fbx_file_summary(path);
            last_error = ss.str();
            BOOST_LOG_TRIVIAL(error) << "FBX: " << last_error << ", path=" << path;
            continue;
        }

        BOOST_LOG_TRIVIAL(info) << "FBX: Assimp ReadFile succeeded"
                                << " (attempt=" << attempt.name
                                << ", flags=" << attempt.flags
                                << ", source=" << (attempt.read_from_memory ? "memory" : "file")
                                << "), " << scene_summary(attempt_scene)
                                << ", path=" << path;

        if (attempt_scene->mNumMeshes == 0) {
            std::ostringstream ss;
            ss << "FBX import failed: Assimp scene has no meshes"
               << " (attempt=" << attempt.name
               << ", " << scene_summary(attempt_scene) << ")";
            last_error = ss.str();
            BOOST_LOG_TRIVIAL(error) << "FBX: " << last_error << ", path=" << path;
            continue;
        }

        TexturedMesh imported;
        uint32_t vertex_offset = 0;
        const bool apply_node_transforms = (attempt.flags & aiProcess_PreTransformVertices) == 0;
        process_node(attempt_scene, attempt_scene->mRootNode, aiMatrix4x4(), apply_node_transforms, vertex_offset, imported);

        if (imported.vertices.empty() || imported.indices.empty()) {
            std::ostringstream ss;
            ss << "FBX import failed: no geometry extracted from Assimp scene"
               << " (attempt=" << attempt.name
               << ", " << scene_summary(attempt_scene)
               << ", extracted_vertices=" << imported.vertices.size()
               << ", extracted_triangles=" << imported.indices.size()
               << ", " << mesh_summary(attempt_scene) << ")";
            last_error = ss.str();
            BOOST_LOG_TRIVIAL(error) << "FBX: " << last_error << ", path=" << path;
            continue;
        }

        out = std::move(imported);
        active_importer = std::move(importer);
        scene = attempt_scene;
        selected_attempt = attempt.name;
        selected_flags = attempt.flags;
        break;
    }

    if (!scene) {
        if (last_error.empty())
            last_error = "FBX import failed: no valid Assimp scene was produced.";
        set_error_message(error_message, I18N::translate("The file format is incompatible and cannot be parsed."));
        return false;
    }

    BOOST_LOG_TRIVIAL(info) << "FBX: extracted geometry"
                            << " (attempt=" << selected_attempt
                            << ", flags=" << selected_flags
                            << ", vertices=" << out.vertices.size()
                            << ", triangles=" << out.indices.size()
                            << "), path=" << path;

    std::string base_dir = boost::filesystem::path(path).parent_path().string();

    // Extract materials and textures
    out.material_texture_map.resize(scene->mNumMaterials, -1);
    out.material_colors.resize(scene->mNumMaterials, {1.f, 1.f, 1.f, 1.f});

    for (unsigned int mi = 0; mi < scene->mNumMaterials; ++mi) {
        const aiMaterial* mat = scene->mMaterials[mi];

        // Extract base color
        aiColor4D color(1.f, 1.f, 1.f, 1.f);
        if (mat->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS) {
            out.material_colors[mi] = {color.r, color.g, color.b, color.a};
        }

        // Extract diffuse texture
        if (mat->GetTextureCount(aiTextureType_DIFFUSE) > 0) {
            aiString tex_path;
            if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &tex_path) == AI_SUCCESS) {
                std::string tex_str(tex_path.C_Str());

                // Check for embedded texture (path starts with '*')
                const aiTexture* embedded = scene->GetEmbeddedTexture(tex_path.C_Str());
                if (embedded) {
                    TextureImage ti;
                    if (embedded->mHeight == 0) {
                        // Compressed format (PNG/JPEG stored in pcData)
                        ti.width = -1;
                        ti.height = -1;
                        ti.channels = 0;
                        ti.data.assign(
                            reinterpret_cast<const unsigned char*>(embedded->pcData),
                            reinterpret_cast<const unsigned char*>(embedded->pcData) + embedded->mWidth);
                    } else {
                        // Raw ARGB8888 pixel data
                        ti.width = embedded->mWidth;
                        ti.height = embedded->mHeight;
                        ti.channels = 4;
                        size_t pixel_count = static_cast<size_t>(ti.width) * ti.height;
                        ti.data.resize(pixel_count * 4);
                        for (size_t p = 0; p < pixel_count; ++p) {
                            const aiTexel& texel = embedded->pcData[p];
                            ti.data[p * 4 + 0] = texel.r;
                            ti.data[p * 4 + 1] = texel.g;
                            ti.data[p * 4 + 2] = texel.b;
                            ti.data[p * 4 + 3] = texel.a;
                        }
                    }
                    int new_idx = static_cast<int>(out.textures.size());
                    out.textures.push_back(std::move(ti));
                    out.material_texture_map[mi] = new_idx;
                } else {
                    // External texture file
                    boost::filesystem::path img_path = resolve_fbx_external_texture_path(
                        boost::filesystem::path(base_dir), tex_str);

                    if (!img_path.empty()) {
                        boost::nowide::ifstream f(img_path.string(), std::ios::binary | std::ios::ate);
                        if (f.is_open()) {
                            auto sz = f.tellg();
                            f.seekg(0);
                            TextureImage ti;
                            ti.width = -1;
                            ti.height = -1;
                            ti.channels = 0;
                            ti.data.resize(static_cast<size_t>(sz));
                            f.read(reinterpret_cast<char*>(ti.data.data()), sz);

                            int new_idx = static_cast<int>(out.textures.size());
                            out.textures.push_back(std::move(ti));
                            out.material_texture_map[mi] = new_idx;
                        }
                    } else {
                        BOOST_LOG_TRIVIAL(warning) << "FBX: texture file not found: " << tex_str;
                    }
                }
            }
        }
    }

    if (out.textures.empty()) {
        BOOST_LOG_TRIVIAL(warning) << "FBX: no textures found in " << path;
    }

    // Apply unit scaling: FBX files often use cm, convert to mm for slicer
    double unit_scale = 1.0;
    if (scene->mMetaData) {
        double unit_factor = 1.0;
        if (scene->mMetaData->Get("UnitScaleFactor", unit_factor)) {
            // UnitScaleFactor is typically 1.0 for cm-based FBX
            // Convert cm to mm: multiply by 10
            unit_scale = unit_factor * 10.0;
        }
    }
    if (unit_scale != 1.0) {
        for (auto& v : out.vertices) {
            v[0] *= static_cast<float>(unit_scale);
            v[1] *= static_cast<float>(unit_scale);
            v[2] *= static_cast<float>(unit_scale);
        }
    }

    BOOST_LOG_TRIVIAL(info) << "FBX: loaded " << out.vertices.size() << " vertices, "
                            << out.indices.size() << " triangles, "
                            << out.textures.size() << " textures from " << path;
    return true;
}

} // namespace Slic3r
