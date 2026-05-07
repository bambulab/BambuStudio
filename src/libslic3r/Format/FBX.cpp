#include "FBX.hpp"
#include "../TexturePainting.hpp"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <boost/log/trivial.hpp>
#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>

#include <cstring>
#include <map>

namespace Slic3r {

namespace {

void collect_mesh(const aiScene* scene,
                  const aiMesh* mesh,
                  uint32_t& vertex_offset,
                  TexturedMesh& out)
{
    (void)scene;
    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
        const auto& v = mesh->mVertices[i];
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
                  uint32_t& vertex_offset,
                  TexturedMesh& out)
{
    for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
        const aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        collect_mesh(scene, mesh, vertex_offset, out);
    }
    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        process_node(scene, node->mChildren[i], vertex_offset, out);
    }
}

} // anonymous namespace

bool load_fbx(const std::string& path, TexturedMesh& out)
{
    Assimp::Importer importer;

    unsigned int flags = aiProcess_Triangulate
                       | aiProcess_GenNormals
                       | aiProcess_JoinIdenticalVertices
                       | aiProcess_FlipUVs
                       | aiProcess_PreTransformVertices;

    const aiScene* scene = importer.ReadFile(path, flags);
    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        BOOST_LOG_TRIVIAL(error) << "FBX: Assimp failed to load " << path
                                 << ": " << importer.GetErrorString();
        return false;
    }

    out.vertices.clear();
    out.indices.clear();
    out.uvs.clear();
    out.textures.clear();
    out.material_ids.clear();
    out.material_texture_map.clear();
    out.material_colors.clear();

    uint32_t vertex_offset = 0;
    process_node(scene, scene->mRootNode, vertex_offset, out);

    if (out.vertices.empty() || out.indices.empty()) {
        BOOST_LOG_TRIVIAL(error) << "FBX: no geometry found in " << path;
        return false;
    }

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
                    boost::filesystem::path img_path = boost::filesystem::path(base_dir) / tex_str;
                    if (!boost::filesystem::exists(img_path)) {
                        // Try just the filename in case path contains directories
                        img_path = boost::filesystem::path(base_dir) / boost::filesystem::path(tex_str).filename();
                    }

                    if (boost::filesystem::exists(img_path)) {
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
                        BOOST_LOG_TRIVIAL(warning) << "FBX: texture file not found: " << img_path;
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
