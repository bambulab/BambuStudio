#include "TexturePainting.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <boost/log/trivial.hpp>

#include "TextureToColor/TextureToColor.hpp"
#include "TextureToColor/ColorUtils.hpp"

#include "Model.hpp"
#include "TriangleMesh.hpp"
#include "TriangleSelector.hpp"

namespace Slic3r {

static cv::Mat decode_texture_image(const TextureImage& img) {
    if (img.data.empty())
        return {};

    // Raw encoded image data (PNG/JPEG) from glTF loader: width == -1
    if (img.width <= 0 || img.height <= 0) {
        std::vector<unsigned char> buf(img.data.begin(), img.data.end());
        cv::Mat raw(1, static_cast<int>(buf.size()), CV_8UC1, buf.data());
        cv::Mat decoded = cv::imdecode(raw, cv::IMREAD_COLOR);
        return decoded;
    }

    int cv_type = (img.channels == 4) ? CV_8UC4 : CV_8UC3;
    std::vector<unsigned char> pixel_buf(img.data.begin(), img.data.end());
    cv::Mat src(img.height, img.width, cv_type, pixel_buf.data());

    cv::Mat bgr;
    if (img.channels == 4)
        cv::cvtColor(src, bgr, cv::COLOR_RGBA2BGR);
    else if (img.channels == 3)
        cv::cvtColor(src, bgr, cv::COLOR_RGB2BGR);
    else
        return {};

    return bgr;
}

static void build_tex2color_mesh(
    const TexturedMesh& textured,
    tex2color::TriMesh& mesh,
    std::vector<std::vector<Vec2f>>& uv_coords)
{
    const size_t nv = textured.vertices.size();
    const size_t nf = textured.indices.size();

    mesh.vertices.resize(nv);
    for (size_t i = 0; i < nv; ++i) {
        mesh.vertices[i] = Vec3f(
            textured.vertices[i][0],
            textured.vertices[i][1],
            textured.vertices[i][2]);
    }

    mesh.indices.resize(nf);
    for (size_t i = 0; i < nf; ++i) {
        mesh.indices[i] = Vec3i(
            textured.indices[i][0],
            textured.indices[i][1],
            textured.indices[i][2]);
    }

    uv_coords.resize(nf);
    for (size_t fi = 0; fi < nf; ++fi) {
        uv_coords[fi].resize(3);
        for (int vi = 0; vi < 3; ++vi) {
            if (textured.has_face_uvs()) {
                int uv_idx = textured.uv_indices[fi][vi];
                if (uv_idx >= 0 && static_cast<size_t>(uv_idx) < textured.uv_coords.size()) {
                    uv_coords[fi][vi] = Vec2f(
                        textured.uv_coords[uv_idx][0],
                        textured.uv_coords[uv_idx][1]);
                } else {
                    uv_coords[fi][vi] = Vec2f(0.f, 0.f);
                }
            } else {
                int vtx_idx = textured.indices[fi][vi];
                if (vtx_idx >= 0 && static_cast<size_t>(vtx_idx) < textured.uvs.size()) {
                    uv_coords[fi][vi] = Vec2f(
                        textured.uvs[vtx_idx][0],
                        textured.uvs[vtx_idx][1]);
                } else {
                    uv_coords[fi][vi] = Vec2f(0.f, 0.f);
                }
            }
        }
    }
}

static void extract_painted_mesh(
    const tex2color::TriMesh& color_mesh,
    const std::vector<std::array<std::size_t,3>>& face_colors,
    PaintedMesh& painted)
{
    const size_t nv = color_mesh.vertices.size();
    const size_t nf = color_mesh.indices.size();

    painted.vertices.resize(nv);
    for (size_t i = 0; i < nv; ++i) {
        const auto& v = color_mesh.vertices[i];
        painted.vertices[i] = {v.x(), v.y(), v.z()};
    }

    painted.indices.resize(nf);
    for (size_t i = 0; i < nf; ++i) {
        const auto& f = color_mesh.indices[i];
        painted.indices[i] = {f[0], f[1], f[2]};
    }

    painted.face_colors = face_colors;

    std::set<std::array<std::size_t,3>> unique_colors(face_colors.begin(), face_colors.end());
    painted.cluster_colors.assign(unique_colors.begin(), unique_colors.end());
}

// Build a vertically-stacked atlas from multiple textures and remap per-face UVs.
static bool build_multi_texture_atlas(
    const TexturedMesh& textured,
    cv::Mat& out_atlas,
    std::vector<std::vector<Vec2f>>& out_uv_coords)
{
    std::vector<cv::Mat> decoded;
    decoded.reserve(textured.textures.size());
    for (const auto& ti : textured.textures)
        decoded.push_back(decode_texture_image(ti));

    // Determine atlas width (max width across all textures) and per-texture row offsets
    int atlas_w = 0;
    int atlas_h = 0;
    std::vector<int> y_offsets(decoded.size(), 0);
    for (size_t i = 0; i < decoded.size(); ++i) {
        if (decoded[i].empty()) continue;
        y_offsets[i] = atlas_h;
        atlas_w = std::max(atlas_w, decoded[i].cols);
        atlas_h += decoded[i].rows;
    }
    if (atlas_w == 0 || atlas_h == 0)
        return false;

    out_atlas = cv::Mat::zeros(atlas_h, atlas_w, CV_8UC3);
    for (size_t i = 0; i < decoded.size(); ++i) {
        if (decoded[i].empty()) continue;
        cv::Mat roi = out_atlas(cv::Rect(0, y_offsets[i], decoded[i].cols, decoded[i].rows));
        decoded[i].copyTo(roi);
    }

    // Remap UVs: for each face, shift v into the correct vertical band
    const size_t nf = textured.indices.size();
    const bool has_mapping = !textured.material_texture_map.empty();
    out_uv_coords.resize(nf);

    for (size_t fi = 0; fi < nf; ++fi) {
        int mat_idx = (fi < textured.material_ids.size()) ? textured.material_ids[fi] : -1;
        int tex_idx = -1;
        if (has_mapping && mat_idx >= 0 && static_cast<size_t>(mat_idx) < textured.material_texture_map.size())
            tex_idx = textured.material_texture_map[mat_idx];
        if (tex_idx < 0) tex_idx = 0;

        int y_off = 0;
        int th = atlas_h;
        if (tex_idx >= 0 && static_cast<size_t>(tex_idx) < decoded.size() && !decoded[tex_idx].empty()) {
            y_off = y_offsets[tex_idx];
            th = decoded[tex_idx].rows;
        }

        out_uv_coords[fi].resize(3);
        for (int vi = 0; vi < 3; ++vi) {
            float u = 0.f, v = 0.f;
            if (textured.has_face_uvs()) {
                int uv_idx = textured.uv_indices[fi][vi];
                if (uv_idx >= 0 && static_cast<size_t>(uv_idx) < textured.uv_coords.size()) {
                    u = textured.uv_coords[uv_idx][0];
                    v = textured.uv_coords[uv_idx][1];
                }
            } else {
                int vtx_idx = textured.indices[fi][vi];
                if (vtx_idx >= 0 && static_cast<size_t>(vtx_idx) < textured.uvs.size()) {
                    u = textured.uvs[vtx_idx][0];
                    v = textured.uvs[vtx_idx][1];
                }
            }
            // Wrap to [0,1) then remap into atlas vertical band
            v = v - std::floor(v);
            float v_atlas = (y_off + v * th) / static_cast<float>(atlas_h);
            out_uv_coords[fi][vi] = Vec2f(u, v_atlas);
        }
    }
    return true;
}

bool texture_to_painting(
    const TexturedMesh& textured,
    PaintedMesh& painted,
    const TexturePaintingSettings& settings,
    PaintProgressCallback progress,
    PaintCancelCallback cancel)
{
    if (textured.vertices.empty() || textured.indices.empty() || textured.textures.empty())
        return false;

    cv::Mat texture;
    tex2color::TriMesh input_mesh;
    std::vector<std::vector<Vec2f>> uv_coords;

    const bool multi_tex = textured.textures.size() > 1 && !textured.material_texture_map.empty();

    if (multi_tex) {
        if (!build_multi_texture_atlas(textured, texture, uv_coords))
            return false;
        // Build mesh geometry (atlas UVs already computed above)
        const size_t nv = textured.vertices.size();
        const size_t nf = textured.indices.size();
        input_mesh.vertices.resize(nv);
        for (size_t i = 0; i < nv; ++i)
            input_mesh.vertices[i] = Vec3f(
                textured.vertices[i][0], textured.vertices[i][1], textured.vertices[i][2]);
        input_mesh.indices.resize(nf);
        for (size_t i = 0; i < nf; ++i)
            input_mesh.indices[i] = Vec3i(
                textured.indices[i][0], textured.indices[i][1], textured.indices[i][2]);
    } else {
        texture = decode_texture_image(textured.textures[0]);
        if (texture.empty())
            return false;
        build_tex2color_mesh(textured, input_mesh, uv_coords);
    }

    tex2color::TextureToColorSettings algo_settings;
    algo_settings.target_colors_num  = settings.target_colors_num;
    algo_settings.smooth_weight      = settings.smooth_weight;
    algo_settings.oversampling_iters = settings.oversampling_iters;

    tex2color::AlgoProgressCallback algo_progress = nullptr;
    if (progress) {
        algo_progress = [&progress](tex2color::AlgoProgress p) {
            progress(p.percent, p.message);
        };
    }

    tex2color::AlgoCancelCallback algo_cancel = nullptr;
    if (cancel) {
        algo_cancel = [&cancel]() -> bool { return cancel(); };
    }

    tex2color::TriMesh color_mesh;
    std::vector<std::array<std::size_t,3>> face_colors;

    bool ok = tex2color::TextureToColor(
        input_mesh, uv_coords, texture,
        color_mesh, face_colors,
        algo_settings, algo_progress, algo_cancel);

    if (!ok)
        return false;

    extract_painted_mesh(color_mesh, face_colors, painted);
    return true;
}

static std::array<double,3> rgb_to_lab(double r, double g, double b) {
    auto linearize = [](double c) -> double {
        c /= 255.0;
        return (c > 0.04045) ? std::pow((c + 0.055) / 1.055, 2.4) : c / 12.92;
    };
    double rl = linearize(r), gl = linearize(g), bl = linearize(b);

    double x = 0.4124564 * rl + 0.3575761 * gl + 0.1804375 * bl;
    double y = 0.2126729 * rl + 0.7151522 * gl + 0.0721750 * bl;
    double z = 0.0193339 * rl + 0.1191920 * gl + 0.9503041 * bl;

    x /= 0.95047; y /= 1.0; z /= 1.08883;

    auto f = [](double t) -> double {
        return (t > 0.008856) ? std::cbrt(t) : (7.787 * t + 16.0 / 116.0);
    };
    double L = 116.0 * f(y) - 16.0;
    double a = 500.0 * (f(x) - f(y));
    double b_val = 200.0 * (f(y) - f(z));
    return {L, a, b_val};
}

double compute_delta_e(
    const std::array<std::size_t,3>& rgb1,
    const std::array<float,4>& rgba2)
{
    auto lab1 = rgb_to_lab((double)rgb1[0], (double)rgb1[1], (double)rgb1[2]);
    auto lab2 = rgb_to_lab(rgba2[0] * 255.0, rgba2[1] * 255.0, rgba2[2] * 255.0);
    double dL = lab1[0] - lab2[0];
    double da = lab1[1] - lab2[1];
    double db = lab1[2] - lab2[2];
    return std::sqrt(dL*dL + da*da + db*db);
}

std::vector<FilamentMatch> match_clusters_to_filaments(
    const std::vector<std::array<std::size_t,3>>& cluster_colors,
    const std::vector<std::array<float,4>>& filament_colors,
    const std::vector<std::string>& /*filament_names*/)
{
    std::vector<FilamentMatch> matches(cluster_colors.size());

    for (size_t ci = 0; ci < cluster_colors.size(); ++ci) {
        matches[ci].cluster_index = static_cast<int>(ci);
        matches[ci].cluster_color = cluster_colors[ci];
        matches[ci].delta_e = 1e9;

        for (size_t fi = 0; fi < filament_colors.size(); ++fi) {
            double de = compute_delta_e(cluster_colors[ci], filament_colors[fi]);
            if (de < matches[ci].delta_e) {
                matches[ci].delta_e = de;
                matches[ci].filament_index = static_cast<int>(fi);
                matches[ci].filament_color = filament_colors[fi];
            }
        }
    }
    return matches;
}

bool apply_painted_mesh_to_volume(
    const PaintedMesh& painted,
    const std::vector<FilamentMatch>& matches,
    ModelVolume& volume)
{
    if (painted.face_colors.empty() || matches.empty())
        return false;

    const auto& cluster_colors = painted.cluster_colors;
    std::map<std::array<std::size_t,3>, int> color_to_filament;
    for (const auto& m : matches) {
        if (m.cluster_index >= 0 && m.cluster_index < (int)cluster_colors.size() && m.filament_index >= 0)
            color_to_filament[cluster_colors[m.cluster_index]] = m.filament_index;
    }

    indexed_triangle_set its;
    its.vertices.resize(painted.vertices.size());
    for (size_t i = 0; i < painted.vertices.size(); ++i) {
        its.vertices[i] = Vec3f(
            painted.vertices[i][0],
            painted.vertices[i][1],
            painted.vertices[i][2]);
    }
    its.indices.resize(painted.indices.size());
    for (size_t i = 0; i < painted.indices.size(); ++i) {
        its.indices[i] = Vec3i(
            painted.indices[i][0],
            painted.indices[i][1],
            painted.indices[i][2]);
    }

    TriangleMesh new_mesh(std::move(its));

    // The volume already went through ModelObject::add_volume ->
    // center_geometry_after_creation, which translated its mesh by
    // -source.mesh_offset (and folded that shift into the volume
    // transformation). The painted mesh, however, is derived from the
    // raw textured mesh and is therefore expressed in the original
    // un-centered coordinate frame. Reuse the exact recorded shift to
    // align it -- do NOT compute it from the bounding-box centers of
    // the two meshes: tex2color::TextureToColor performs subdivision
    // and CGAL polygon-soup repair, so the painted vertex count and
    // bbox no longer match the original textured mesh and a bbox-
    // center alignment would silently displace the geometry.
    const Vec3d mesh_offset = volume.source.mesh_offset;
    if (!mesh_offset.isApprox(Vec3d::Zero()))
        new_mesh.translate(-mesh_offset.cast<float>());
    new_mesh.set_init_shift(mesh_offset);

    // Log bbox drift for diagnostics. Subdivision + CGAL polygon-soup
    // repair routinely changes vertex count and bbox, so moderate drift
    // is expected and must not block the apply.
    if (!new_mesh.empty() && !volume.mesh().empty()) {
        const Vec3d new_center = new_mesh.bounding_box().center();
        const Vec3d cur_center = volume.mesh().bounding_box().center();
        const double diag      = volume.mesh().bounding_box().size().norm();
        const double drift     = (new_center - cur_center).norm();
        if (drift > 0.05 * std::max(1.0, diag))
            BOOST_LOG_TRIVIAL(warning)
                << "apply_painted_mesh_to_volume: painted bbox center drifted by "
                << drift << " (bbox diag=" << diag << ")";
        else if (drift > 1e-3 * std::max(1.0, diag))
            BOOST_LOG_TRIVIAL(info)
                << "apply_painted_mesh_to_volume: minor bbox drift "
                << drift << " (bbox diag=" << diag << ")";
    }

    volume.set_mesh(std::move(new_mesh));
    volume.calculate_convex_hull();

    // Re-center the replaced mesh so its bbox center sits at the origin,
    // matching what center_geometry_after_creation did for the original mesh.
    // CGAL repair / subdivision may shift the bbox center (drift); without
    // re-centering, the volume offset (which was computed for the original
    // centered mesh) no longer matches, causing the model to float or clip.
    // Pass false to keep source.mesh_offset unchanged.
    volume.center_geometry_after_creation(false);
    volume.invalidate_convex_hull_2d();

    // Mesh geometry has been replaced; any per-face annotation indexed
    // against the previous triangle set is now stale. mmu_segmentation_facets
    // is rewritten below from the new selector; reset the others so future
    // import paths that carry support / seam / fuzzy_skin painting cannot
    // leak indices from the old mesh into the new one.
    volume.supported_facets.reset();
    volume.fuzzy_skin_facets.reset();
    volume.seam_facets.reset();

    if (ModelObject* obj = volume.get_object())
        obj->invalidate_bounding_box();

    TriangleSelector selector(volume.mesh());
    for (size_t fi = 0; fi < painted.face_colors.size() && fi < (size_t)volume.mesh().its.indices.size(); ++fi) {
        auto it = color_to_filament.find(painted.face_colors[fi]);
        if (it != color_to_filament.end()) {
            int extruder_idx = it->second;
            auto state = static_cast<EnforcerBlockerType>(
                static_cast<int>(EnforcerBlockerType::Extruder1) + extruder_idx);
            if (state <= EnforcerBlockerType::ExtruderMax)
                selector.set_facet(static_cast<int>(fi), state);
        }
    }

    volume.mmu_segmentation_facets.set(selector);
    return true;
}

bool decode_texture_to_pixels(
    const TextureImage& img,
    std::vector<unsigned char>& out_pixels,
    int& out_w, int& out_h)
{
    cv::Mat decoded = decode_texture_image(img);
    if (decoded.empty())
        return false;

    // decoded is BGR, CV_8UC3
    out_w = decoded.cols;
    out_h = decoded.rows;
    size_t nbytes = (size_t)out_w * out_h * 3;
    out_pixels.resize(nbytes);

    if (decoded.isContinuous()) {
        std::memcpy(out_pixels.data(), decoded.data, nbytes);
    } else {
        for (int r = 0; r < out_h; ++r)
            std::memcpy(out_pixels.data() + r * out_w * 3, decoded.ptr(r), out_w * 3);
    }
    return true;
}

// Sample face color from texture using 3 explicit UV values (centroid + bilinear).
static std::array<std::size_t,3> sample_face_from_uvs(
    const cv::Mat& tex,
    const std::array<float,2>& uv0,
    const std::array<float,2>& uv1,
    const std::array<float,2>& uv2)
{
    float cu = (uv0[0] + uv1[0] + uv2[0]) / 3.f;
    float cv_val = (uv0[1] + uv1[1] + uv2[1]) / 3.f;

    cu = cu - std::floor(cu);
    cv_val = cv_val - std::floor(cv_val);

    float fx = cu * (tex.cols - 1);
    float fy = cv_val * (tex.rows - 1);

    int x0 = std::clamp(static_cast<int>(fx), 0, tex.cols - 1);
    int y0 = std::clamp(static_cast<int>(fy), 0, tex.rows - 1);
    int x1 = std::min(x0 + 1, tex.cols - 1);
    int y1 = std::min(y0 + 1, tex.rows - 1);

    float wx = fx - x0;
    float wy = fy - y0;

    const int ch = tex.channels();
    auto sample = [&](int row, int col) -> std::array<float,3> {
        const uchar* ptr = tex.data + row * tex.step[0] + col * ch;
        return {static_cast<float>(ptr[2]), static_cast<float>(ptr[1]), static_cast<float>(ptr[0])};
    };

    auto c00 = sample(y0, x0);
    auto c10 = sample(y0, x1);
    auto c01 = sample(y1, x0);
    auto c11 = sample(y1, x1);

    std::array<std::size_t,3> color;
    for (int i = 0; i < 3; ++i) {
        float top = c00[i] * (1.f - wx) + c10[i] * wx;
        float bot = c01[i] * (1.f - wx) + c11[i] * wx;
        color[i] = static_cast<std::size_t>(std::clamp(top * (1.f - wy) + bot * wy, 0.f, 255.f));
    }
    return color;
}

// Legacy overload: look up UVs from per-vertex array by vertex indices.
static std::array<std::size_t,3> sample_face_from_texture(
    const cv::Mat& tex,
    const std::vector<std::array<float,2>>& uvs,
    const std::array<int,3>& face)
{
    std::array<float,2> uv0 = {0.f, 0.f}, uv1 = {0.f, 0.f}, uv2 = {0.f, 0.f};
    if (face[0] >= 0 && static_cast<size_t>(face[0]) < uvs.size()) uv0 = uvs[face[0]];
    if (face[1] >= 0 && static_cast<size_t>(face[1]) < uvs.size()) uv1 = uvs[face[1]];
    if (face[2] >= 0 && static_cast<size_t>(face[2]) < uvs.size()) uv2 = uvs[face[2]];
    return sample_face_from_uvs(tex, uv0, uv1, uv2);
}

bool sample_original_face_colors(
    const TexturedMesh& textured,
    std::vector<std::array<std::size_t,3>>& out_face_colors)
{
    if (textured.indices.empty())
        return false;

    // Decode all textures up front
    std::vector<cv::Mat> decoded_textures;
    decoded_textures.reserve(textured.textures.size());
    for (const auto& ti : textured.textures) {
        decoded_textures.push_back(decode_texture_image(ti));
    }

    const bool has_mapping = !textured.material_texture_map.empty();
    const size_t nf = textured.indices.size();
    out_face_colors.resize(nf);

    for (size_t fi = 0; fi < nf; ++fi) {
        int mat_idx = (fi < textured.material_ids.size()) ? textured.material_ids[fi] : -1;

        int tex_idx = -1;
        if (has_mapping && mat_idx >= 0 && static_cast<size_t>(mat_idx) < textured.material_texture_map.size())
            tex_idx = textured.material_texture_map[mat_idx];
        else if (!decoded_textures.empty())
            tex_idx = 0; // fallback: single-texture model

        if (tex_idx >= 0 && static_cast<size_t>(tex_idx) < decoded_textures.size()
            && !decoded_textures[tex_idx].empty()) {
            if (textured.has_face_uvs()) {
                const auto& ui = textured.uv_indices[fi];
                auto get_uv = [&](int vi) -> std::array<float,2> {
                    int idx = ui[vi];
                    if (idx >= 0 && static_cast<size_t>(idx) < textured.uv_coords.size())
                        return textured.uv_coords[idx];
                    return {0.f, 0.f};
                };
                out_face_colors[fi] = sample_face_from_uvs(
                    decoded_textures[tex_idx], get_uv(0), get_uv(1), get_uv(2));
            } else {
                out_face_colors[fi] = sample_face_from_texture(
                    decoded_textures[tex_idx], textured.uvs, textured.indices[fi]);
            }
        } else if (has_mapping && mat_idx >= 0
                   && static_cast<size_t>(mat_idx) < textured.material_colors.size()) {
            // No texture — use baseColorFactor as solid color
            const auto& c = textured.material_colors[mat_idx];
            out_face_colors[fi] = {
                static_cast<std::size_t>(std::clamp(c[0] * 255.f, 0.f, 255.f)),
                static_cast<std::size_t>(std::clamp(c[1] * 255.f, 0.f, 255.f)),
                static_cast<std::size_t>(std::clamp(c[2] * 255.f, 0.f, 255.f))
            };
        } else {
            out_face_colors[fi] = {192, 192, 192}; // default gray
        }
    }
    return true;
}

} // namespace Slic3r
