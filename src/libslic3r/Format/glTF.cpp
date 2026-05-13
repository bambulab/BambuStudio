#include "glTF.hpp"
#include "../TexturePainting.hpp"

#include "ResourcePathUtils.hpp"
#include "nlohmann/json.hpp"

#include <boost/log/trivial.hpp>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/nowide/fstream.hpp>

#include <cstdint>
#include <cstring>
#include <array>
#include <map>

using json = nlohmann::json;

namespace Slic3r {

namespace {

constexpr uint32_t GLB_MAGIC   = 0x46546C67; // 'glTF'
constexpr uint32_t GLB_VERSION = 2;
constexpr uint32_t CHUNK_JSON  = 0x4E4F534A; // 'JSON'
constexpr uint32_t CHUNK_BIN   = 0x004E4942; // 'BIN\0'

// glTF componentType constants (per spec §3.6.2.2)
constexpr int GLTF_BYTE           = 5120;
constexpr int GLTF_UNSIGNED_BYTE  = 5121;
constexpr int GLTF_SHORT          = 5122;
constexpr int GLTF_UNSIGNED_SHORT = 5123;
constexpr int GLTF_UNSIGNED_INT   = 5125;
constexpr int GLTF_FLOAT          = 5126;

struct GlbHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t length;
};

struct ChunkHeader {
    uint32_t chunk_length;
    uint32_t chunk_type;
};

template <typename T>
T read_le(const unsigned char* p)
{
    T val;
    std::memcpy(&val, p, sizeof(T));
    return val;
}

bool parse_glb(const std::string& path,
               json& out_json,
               std::vector<unsigned char>& out_bin)
{
    boost::nowide::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        BOOST_LOG_TRIVIAL(error) << "glTF: cannot open file " << path;
        return false;
    }

    auto file_size = file.tellg();
    if (file_size < 12) return false;
    file.seekg(0);

    std::vector<unsigned char> buf(static_cast<size_t>(file_size));
    file.read(reinterpret_cast<char*>(buf.data()), file_size);
    if (!file) return false;

    const unsigned char* ptr = buf.data();
    const unsigned char* end = ptr + buf.size();

    auto hdr = read_le<GlbHeader>(ptr);
    if (hdr.magic != GLB_MAGIC || hdr.version != GLB_VERSION) {
        BOOST_LOG_TRIVIAL(error) << "glTF: invalid GLB header";
        return false;
    }
    ptr += sizeof(GlbHeader);

    bool have_json = false;
    while (ptr + sizeof(ChunkHeader) <= end) {
        auto ch = read_le<ChunkHeader>(ptr);
        ptr += sizeof(ChunkHeader);
        if (ptr + ch.chunk_length > end) break;

        if (ch.chunk_type == CHUNK_JSON) {
            std::string json_str(reinterpret_cast<const char*>(ptr), ch.chunk_length);
            try {
                out_json = json::parse(json_str);
                have_json = true;
            } catch (const json::parse_error& e) {
                BOOST_LOG_TRIVIAL(error) << "glTF: JSON parse error: " << e.what();
                return false;
            }
        } else if (ch.chunk_type == CHUNK_BIN) {
            out_bin.assign(ptr, ptr + ch.chunk_length);
        }
        ptr += ch.chunk_length;
    }

    return have_json;
}

bool parse_gltf_json(const std::string& path,
                     json& out_json,
                     std::vector<unsigned char>& out_bin)
{
    boost::nowide::ifstream file(path);
    if (!file.is_open()) {
        BOOST_LOG_TRIVIAL(warning) << "glTF: cannot open file " << path;
        return false;
    }

    try {
        file >> out_json;
    } catch (const json::parse_error& e) {
        BOOST_LOG_TRIVIAL(error) << "glTF: JSON parse error: " << e.what();
        return false;
    }

    if (!out_json.contains("buffers") || out_json["buffers"].empty())
        return true;

    auto& buf0 = out_json["buffers"][0];
    if (!buf0.contains("uri"))
        return true;

    std::string uri = buf0["uri"].get<std::string>();
    boost::filesystem::path base_dir = boost::filesystem::path(path).parent_path();
    boost::filesystem::path bin_path = resource_path::resolve_existing_relative_path_case_insensitive(
        base_dir, boost::filesystem::path(uri), "glTF: binary buffer");
    if (bin_path.empty())
        bin_path = base_dir / uri;

    boost::nowide::ifstream bin_file(bin_path.string(), std::ios::binary | std::ios::ate);
    if (!bin_file.is_open()) {
        BOOST_LOG_TRIVIAL(warning) << "glTF: cannot open binary buffer " << bin_path;
        return true;
    }
    auto sz = bin_file.tellg();
    bin_file.seekg(0);
    out_bin.resize(static_cast<size_t>(sz));
    bin_file.read(reinterpret_cast<char*>(out_bin.data()), sz);
    if (!bin_file.good() && !bin_file.eof()) {
        BOOST_LOG_TRIVIAL(warning) << "glTF: failed to fully read binary buffer " << bin_path;
        out_bin.clear();
    }
    return true;
}

size_t component_byte_size(int comp_type);

struct AccessorInfo {
    const unsigned char* data = nullptr;
    size_t count  = 0;
    int    comp_type = 0; // 5120=BYTE ... 5126=FLOAT
    size_t stride = 0;
};

AccessorInfo resolve_accessor(const json& root,
                              int accessor_idx,
                              const std::vector<unsigned char>& bin)
{
    AccessorInfo info;
    if (accessor_idx < 0) return info;

    if (!root.contains("accessors") ||
        accessor_idx >= static_cast<int>(root["accessors"].size())) {
        BOOST_LOG_TRIVIAL(warning) << "glTF: accessor index " << accessor_idx << " out of bounds";
        return info;
    }

    const auto& acc = root["accessors"][accessor_idx];
    info.count     = acc["count"].get<size_t>();
    info.comp_type = acc["componentType"].get<int>();

    int bv_idx = acc.value("bufferView", -1);
    int byte_offset = acc.value("byteOffset", 0);
    if (bv_idx < 0) return info;

    if (!root.contains("bufferViews") ||
        bv_idx >= static_cast<int>(root["bufferViews"].size())) {
        BOOST_LOG_TRIVIAL(warning) << "glTF: bufferView index " << bv_idx << " out of bounds";
        return info;
    }

    const auto& bv = root["bufferViews"][bv_idx];
    int bv_offset = bv.value("byteOffset", 0);
    info.stride    = bv.value("byteStride", 0);

    size_t total_offset = static_cast<size_t>(bv_offset) + byte_offset;
    size_t effective_stride = info.stride > 0 ? info.stride : component_byte_size(info.comp_type);
    if (info.count > 0 && (total_offset + effective_stride * info.count > bin.size())) {
        BOOST_LOG_TRIVIAL(warning) << "glTF: accessor data range exceeds buffer size";
        return info;
    }
    if (total_offset < bin.size())
        info.data = bin.data() + total_offset;

    return info;
}

float read_component_float(const unsigned char* p, int comp_type)
{
    switch (comp_type) {
    case GLTF_FLOAT:          { float v; std::memcpy(&v, p, 4); return v; }
    case GLTF_UNSIGNED_SHORT: { uint16_t v; std::memcpy(&v, p, 2); return static_cast<float>(v); }
    case GLTF_UNSIGNED_BYTE:  return static_cast<float>(*p);
    case GLTF_SHORT:          { int16_t v; std::memcpy(&v, p, 2); return static_cast<float>(v); }
    case GLTF_UNSIGNED_INT:   { uint32_t v; std::memcpy(&v, p, 4); return static_cast<float>(v); }
    default: return 0.f;
    }
}

size_t component_byte_size(int comp_type)
{
    switch (comp_type) {
    case GLTF_FLOAT:          return 4;
    case GLTF_UNSIGNED_INT:   return 4;
    case GLTF_UNSIGNED_SHORT: return 2;
    case GLTF_SHORT:          return 2;
    case GLTF_UNSIGNED_BYTE:  return 1;
    case GLTF_BYTE:           return 1;
    default: return 4;
    }
}

uint32_t read_index(const unsigned char* p, int comp_type)
{
    switch (comp_type) {
    case GLTF_UNSIGNED_INT:   { uint32_t v; std::memcpy(&v, p, 4); return v; }
    case GLTF_UNSIGNED_SHORT: { uint16_t v; std::memcpy(&v, p, 2); return static_cast<uint32_t>(v); }
    case GLTF_UNSIGNED_BYTE:  return static_cast<uint32_t>(*p);
    default: return 0;
    }
}

struct ImageData {
    std::vector<unsigned char> bytes;
    std::string mime_type;
};

ImageData extract_image(const json& root,
                        int image_idx,
                        const std::vector<unsigned char>& bin,
                        const std::string& base_dir)
{
    ImageData result;
    if (image_idx < 0 || !root.contains("images")) return result;
    if (image_idx >= static_cast<int>(root["images"].size())) {
        BOOST_LOG_TRIVIAL(warning) << "glTF: image index " << image_idx << " out of bounds";
        return result;
    }
    const auto& img = root["images"][image_idx];

    if (img.contains("bufferView")) {
        int bv_idx = img["bufferView"].get<int>();
        if (!root.contains("bufferViews") ||
            bv_idx < 0 || bv_idx >= static_cast<int>(root["bufferViews"].size())) {
            BOOST_LOG_TRIVIAL(warning) << "glTF: bufferView index " << bv_idx << " out of bounds (image)";
            return result;
        }
        const auto& bv = root["bufferViews"][bv_idx];
        size_t offset = bv.value("byteOffset", 0);
        size_t length = bv["byteLength"].get<size_t>();
        if (img.contains("mimeType"))
            result.mime_type = img["mimeType"].get<std::string>();
        if (offset + length <= bin.size())
            result.bytes.assign(bin.data() + offset, bin.data() + offset + length);
    } else if (img.contains("uri")) {
        std::string uri = img["uri"].get<std::string>();
        if (uri.rfind("data:", 0) == 0) {
            auto comma = uri.find(',');
            if (comma != std::string::npos) {
                // base64 data URI — skip for now, complex to decode
                BOOST_LOG_TRIVIAL(warning) << "glTF: base64 data URI textures not supported yet";
            }
        } else {
            boost::filesystem::path img_path = resource_path::resolve_existing_relative_path_case_insensitive(
                boost::filesystem::path(base_dir), boost::filesystem::path(uri), "glTF: image URI");
            if (img_path.empty())
                img_path = boost::filesystem::path(base_dir) / uri;
            boost::nowide::ifstream f(img_path.string(), std::ios::binary | std::ios::ate);
            if (f.is_open()) {
                auto sz = f.tellg();
                f.seekg(0);
                result.bytes.resize(static_cast<size_t>(sz));
                f.read(reinterpret_cast<char*>(result.bytes.data()), sz);
                if (!f.good() && !f.eof()) {
                    BOOST_LOG_TRIVIAL(warning) << "glTF: failed to fully read texture " << img_path;
                    result.bytes.clear();
                }
            } else {
                BOOST_LOG_TRIVIAL(warning) << "glTF: cannot open texture file " << img_path;
            }
        }
    }
    return result;
}

int resolve_texture_image_index(const json& root, int tex_idx)
{
    if (tex_idx < 0 || !root.contains("textures")) return -1;
    if (tex_idx >= static_cast<int>(root["textures"].size())) {
        BOOST_LOG_TRIVIAL(warning) << "glTF: texture index " << tex_idx << " out of bounds";
        return -1;
    }
    const auto& tex = root["textures"][tex_idx];
    return tex.value("source", -1);
}

bool has_draco_mesh_compression(const json& root)
{
    if (!root.contains("meshes"))
        return false;

    for (const auto& mesh : root["meshes"]) {
        if (!mesh.contains("primitives"))
            continue;
        for (const auto& prim : mesh["primitives"]) {
            if (!prim.contains("extensions"))
                continue;
            const auto& extensions = prim["extensions"];
            if (extensions.contains("KHR_draco_mesh_compression"))
                return true;
        }
    }

    return false;
}

using Mat4 = std::array<float, 16>;

Mat4 mat4_identity()
{
    return {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
}

Mat4 mat4_multiply(const Mat4& a, const Mat4& b)
{
    Mat4 r{};
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                r[c * 4 + row] += a[k * 4 + row] * b[c * 4 + k];
    return r;
}

Mat4 node_local_transform(const json& node)
{
    if (node.contains("matrix")) {
        const auto& m = node["matrix"];
        if (m.is_array() && m.size() == 16) {
            Mat4 r;
            for (int i = 0; i < 16; ++i)
                r[i] = m[i].get<float>();
            return r;
        }
    }

    float tx = 0, ty = 0, tz = 0;
    float qx = 0, qy = 0, qz = 0, qw = 1;
    float sx = 1, sy = 1, sz = 1;

    if (node.contains("translation")) {
        const auto& t = node["translation"];
        if (t.is_array() && t.size() >= 3) {
            tx = t[0].get<float>(); ty = t[1].get<float>(); tz = t[2].get<float>();
        }
    }
    if (node.contains("rotation")) {
        const auto& q = node["rotation"];
        if (q.is_array() && q.size() >= 4) {
            qx = q[0].get<float>(); qy = q[1].get<float>();
            qz = q[2].get<float>(); qw = q[3].get<float>();
        }
    }
    if (node.contains("scale")) {
        const auto& s = node["scale"];
        if (s.is_array() && s.size() >= 3) {
            sx = s[0].get<float>(); sy = s[1].get<float>(); sz = s[2].get<float>();
        }
    }

    float xx = qx*qx, yy = qy*qy, zz = qz*qz;
    float xy = qx*qy, xz = qx*qz, yz = qy*qz;
    float wx = qw*qx, wy = qw*qy, wz = qw*qz;

    Mat4 r;
    r[ 0] = (1 - 2*(yy+zz)) * sx;  r[ 4] = (2*(xy-wz)) * sy;      r[ 8] = (2*(xz+wy)) * sz;      r[12] = tx;
    r[ 1] = (2*(xy+wz)) * sx;      r[ 5] = (1 - 2*(xx+zz)) * sy;  r[ 9] = (2*(yz-wx)) * sz;      r[13] = ty;
    r[ 2] = (2*(xz-wy)) * sx;      r[ 6] = (2*(yz+wx)) * sy;      r[10] = (1 - 2*(xx+yy)) * sz;  r[14] = tz;
    r[ 3] = 0;                      r[ 7] = 0;                      r[11] = 0;                      r[15] = 1;
    return r;
}

void transform_point(const Mat4& m, float& x, float& y, float& z)
{
    float nx = m[0]*x + m[4]*y + m[ 8]*z + m[12];
    float ny = m[1]*x + m[5]*y + m[ 9]*z + m[13];
    float nz = m[2]*x + m[6]*y + m[10]*z + m[14];
    x = nx; y = ny; z = nz;
}

void collect_mesh_primitives(const json& root,
                             int mesh_idx,
                             const Mat4& world_transform,
                             const std::vector<unsigned char>& bin,
                             uint32_t& vertex_offset,
                             TexturedMesh& out)
{
    if (!root.contains("meshes") ||
        mesh_idx < 0 || mesh_idx >= static_cast<int>(root["meshes"].size())) {
        BOOST_LOG_TRIVIAL(warning) << "glTF: mesh index " << mesh_idx << " out of bounds";
        return;
    }
    const auto& mesh = root["meshes"][mesh_idx];
    if (!mesh.contains("primitives")) return;
    for (const auto& prim : mesh["primitives"]) {
        if (prim.value("mode", 4) != 4)
            continue;
        if (!prim.contains("attributes"))
            continue;

        const auto& attrs = prim["attributes"];
        int pos_acc = attrs.value("POSITION", -1);
        int uv_acc  = attrs.value("TEXCOORD_0", -1);
        int idx_acc = prim.value("indices", -1);
        int mat_idx = prim.value("material", -1);

        if (pos_acc < 0) continue;

        auto pos_info = resolve_accessor(root, pos_acc, bin);
        if (!pos_info.data || pos_info.count == 0) continue;

        size_t pos_stride = pos_info.stride > 0 ? pos_info.stride : 3 * component_byte_size(pos_info.comp_type);
        for (size_t i = 0; i < pos_info.count; ++i) {
            const unsigned char* p = pos_info.data + i * pos_stride;
            float x = read_component_float(p, pos_info.comp_type);
            float y = read_component_float(p + component_byte_size(pos_info.comp_type), pos_info.comp_type);
            float z = read_component_float(p + 2 * component_byte_size(pos_info.comp_type), pos_info.comp_type);
            transform_point(world_transform, x, y, z);
            out.vertices.push_back({x, y, z});
        }

        if (uv_acc >= 0) {
            auto uv_info = resolve_accessor(root, uv_acc, bin);
            if (uv_info.data) {
                size_t uv_stride = uv_info.stride > 0 ? uv_info.stride : 2 * component_byte_size(uv_info.comp_type);
                for (size_t i = 0; i < uv_info.count; ++i) {
                    const unsigned char* p = uv_info.data + i * uv_stride;
                    float u = read_component_float(p, uv_info.comp_type);
                    float v = read_component_float(p + component_byte_size(uv_info.comp_type), uv_info.comp_type);
                    out.uvs.push_back({u, v});
                }
            }
        }
        while (out.uvs.size() < out.vertices.size())
            out.uvs.push_back({0.f, 0.f});

        if (idx_acc >= 0) {
            auto idx_info = resolve_accessor(root, idx_acc, bin);
            if (idx_info.data) {
                size_t elem_sz = component_byte_size(idx_info.comp_type);
                size_t idx_stride = idx_info.stride > 0 ? idx_info.stride : elem_sz;
                for (size_t i = 0; i + 2 < idx_info.count; i += 3) {
                    int i0 = static_cast<int>(read_index(idx_info.data + (i + 0) * idx_stride, idx_info.comp_type)) + vertex_offset;
                    int i1 = static_cast<int>(read_index(idx_info.data + (i + 1) * idx_stride, idx_info.comp_type)) + vertex_offset;
                    int i2 = static_cast<int>(read_index(idx_info.data + (i + 2) * idx_stride, idx_info.comp_type)) + vertex_offset;
                    out.indices.push_back({i0, i1, i2});
                    out.material_ids.push_back(mat_idx);
                }
            }
        } else {
            for (size_t i = 0; i + 2 < pos_info.count; i += 3) {
                int i0 = static_cast<int>(i + vertex_offset);
                int i1 = static_cast<int>(i + 1 + vertex_offset);
                int i2 = static_cast<int>(i + 2 + vertex_offset);
                out.indices.push_back({i0, i1, i2});
                out.material_ids.push_back(mat_idx);
            }
        }

        vertex_offset = static_cast<uint32_t>(out.vertices.size());
    }
}

void process_gltf_node(const json& root,
                       int node_idx,
                       const Mat4& parent_transform,
                       const std::vector<unsigned char>& bin,
                       uint32_t& vertex_offset,
                       TexturedMesh& out)
{
    if (!root.contains("nodes") ||
        node_idx < 0 || node_idx >= static_cast<int>(root["nodes"].size())) {
        BOOST_LOG_TRIVIAL(warning) << "glTF: node index " << node_idx << " out of bounds";
        return;
    }
    const auto& node = root["nodes"][node_idx];
    Mat4 world = mat4_multiply(parent_transform, node_local_transform(node));

    if (node.contains("mesh")) {
        int mesh_idx = node["mesh"].get<int>();
        collect_mesh_primitives(root, mesh_idx, world, bin, vertex_offset, out);
    }

    if (node.contains("children")) {
        for (const auto& child_idx : node["children"])
            process_gltf_node(root, child_idx.get<int>(), world, bin, vertex_offset, out);
    }
}

} // anonymous namespace

bool load_gltf(const std::string& path, TexturedMesh& out, std::string* error_message)
{
    json root;
    std::vector<unsigned char> bin;

    bool is_glb = boost::algorithm::iends_with(path, ".glb");
    bool ok = is_glb ? parse_glb(path, root, bin) : parse_gltf_json(path, root, bin);
    if (!ok) {
        BOOST_LOG_TRIVIAL(error) << "glTF: failed to parse " << path;
        return false;
    }

    if (!root.contains("meshes") || root["meshes"].empty()) {
        BOOST_LOG_TRIVIAL(error) << "glTF: no meshes found";
        return false;
    }

    if (has_draco_mesh_compression(root)) {
        if (error_message)
            *error_message = "Draco-compressed glTF/GLB files are not supported.";
        BOOST_LOG_TRIVIAL(error) << "glTF: Draco-compressed geometry is not supported in " << path;
        return false;
    }

    std::string base_dir = boost::filesystem::path(path).parent_path().string();

    out.vertices.clear();
    out.indices.clear();
    out.uvs.clear();
    out.textures.clear();
    out.material_ids.clear();
    out.material_texture_map.clear();
    out.material_colors.clear();

    uint32_t vertex_offset = 0;
    Mat4 identity = mat4_identity();

    if (root.contains("scenes") && root.contains("nodes")) {
        int scene_idx = root.value("scene", 0);
        const auto& scenes = root["scenes"];
        if (scene_idx >= 0 && scene_idx < static_cast<int>(scenes.size())) {
            const auto& scene = scenes[scene_idx];
            if (scene.contains("nodes")) {
                for (const auto& nidx : scene["nodes"])
                    process_gltf_node(root, nidx.get<int>(), identity, bin, vertex_offset, out);
            }
        }
    }

    // Fallback: if node traversal produced nothing, iterate meshes directly
    if (out.vertices.empty()) {
        for (int mi = 0; mi < static_cast<int>(root["meshes"].size()); ++mi)
            collect_mesh_primitives(root, mi, identity, bin, vertex_offset, out);
    }

    // Extract textures from materials, building per-material mapping
    std::map<int, int> image_idx_to_tex_idx; // glTF image index -> out.textures[] index
    if (root.contains("materials")) {
        const auto& materials = root["materials"];
        out.material_texture_map.resize(materials.size(), -1);
        out.material_colors.resize(materials.size(), {1.f, 1.f, 1.f, 1.f});

        for (size_t mi = 0; mi < materials.size(); ++mi) {
            const auto& mat = materials[mi];

            // Read baseColorFactor (default white)
            if (mat.contains("pbrMetallicRoughness")) {
                const auto& pbr = mat["pbrMetallicRoughness"];
                if (pbr.contains("baseColorFactor")) {
                    const auto& cf = pbr["baseColorFactor"];
                    if (cf.is_array() && cf.size() >= 3) {
                        out.material_colors[mi] = {
                            cf[0].get<float>(), cf[1].get<float>(),
                            cf[2].get<float>(), cf.size() > 3 ? cf[3].get<float>() : 1.f
                        };
                    }
                }
            }

            int tex_idx = -1;
            if (mat.contains("pbrMetallicRoughness")) {
                const auto& pbr = mat["pbrMetallicRoughness"];
                if (pbr.contains("baseColorTexture"))
                    tex_idx = pbr["baseColorTexture"].value("index", -1);
            }
            if (tex_idx < 0 && mat.contains("emissiveTexture"))
                tex_idx = mat["emissiveTexture"].value("index", -1);

            int img_idx = resolve_texture_image_index(root, tex_idx);
            if (img_idx < 0)
                continue;

            auto it = image_idx_to_tex_idx.find(img_idx);
            if (it != image_idx_to_tex_idx.end()) {
                out.material_texture_map[mi] = it->second;
            } else {
                auto img_data = extract_image(root, img_idx, bin, base_dir);
                if (!img_data.bytes.empty()) {
                    int new_tex_idx = static_cast<int>(out.textures.size());
                    TextureImage ti;
                    ti.data = std::move(img_data.bytes);
                    ti.width  = -1; // raw encoded data; will be decoded by OpenCV
                    ti.height = -1;
                    ti.channels = 0;
                    out.textures.push_back(std::move(ti));
                    image_idx_to_tex_idx[img_idx] = new_tex_idx;
                    out.material_texture_map[mi] = new_tex_idx;
                }
            }
        }
    }

    if (out.textures.empty()) {
        BOOST_LOG_TRIVIAL(warning) << "glTF: no textures found in " << path;
    }

    BOOST_LOG_TRIVIAL(info) << "glTF: loaded " << out.vertices.size() << " vertices, "
                            << out.indices.size() << " triangles, "
                            << out.textures.size() << " textures from " << path;
    return !out.vertices.empty() && !out.indices.empty();
}

} // namespace Slic3r
