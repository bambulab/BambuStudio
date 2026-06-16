#include "Print.hpp"
#include "ClipperUtils.hpp"
#include "Flow.hpp"
#include "Layer.hpp"
#include "Slicing.hpp"
#include "Surface.hpp"
#include "TriangleMeshSlicer.hpp"

#include <algorithm>
#include <array>

#include <tbb/parallel_for.h>

namespace Slic3r {

#define SUPPORT_SURFACES_OFFSET_PARAMETERS ClipperLib::jtSquare, 0.
#define SUPPORT_MATERIAL_MARGIN 1.2

template<typename PolysType>
void PrintObject::remove_bridges_from_contacts(
    const Layer *lower_layer,
    const Layer *current_layer,
    float        extrusion_width,
    PolysType   *overhang_regions,
    float        max_bridge_length,
    bool         break_bridge)
{
    float    fw                   = extrusion_width;
    auto     layer_regions        = current_layer->regions();
    Polygons lower_layer_polygons = to_polygons(lower_layer->lslices);
    const PrintObjectConfig &object_config = current_layer->object()->config();

    Polygons all_bridges;
    for (LayerRegion *layerm : layer_regions) {
        Polygons bridges;
        Polygons lower_grown_slices = offset(lower_layer_polygons, 0.5f * fw, SUPPORT_SURFACES_OFFSET_PARAMETERS);
        Polylines overhang_perimeters = diff_pl(layerm->perimeters.as_polylines(), lower_grown_slices);
        Flow      bridge_flow         = layerm->bridging_flow(frPerimeter, object_config.thick_bridges);
        float     w                   = float(std::max(bridge_flow.scaled_width(), bridge_flow.scaled_spacing()));
        for (Polyline &polyline : overhang_perimeters)
            if (polyline.is_straight()) {
                polyline.extend_start(fw);
                polyline.extend_end(fw);
                Point pts[2]      = { polyline.first_point(), polyline.last_point() };
                bool  supported[2] = { false, false };
                for (size_t i = 0; i < lower_layer->lslices.size() && !(supported[0] && supported[1]); ++i)
                    for (int j = 0; j < 2; ++j)
                        if (!supported[j] && lower_layer->lslices_bboxes[i].contains(pts[j]) && lower_layer->lslices[i].contains(pts[j]))
                            supported[j] = true;
                if (supported[0] && supported[1]) {
                    Polylines lines;
                    if (polyline.length() > max_bridge_length + 10) {
                        if (break_bridge) {
                            float len = polyline.length() / ceil(polyline.length() / max_bridge_length);
                            lines     = polyline.equally_spaced_lines(len);
                            for (auto &line : lines) {
                                if (line.is_valid())
                                    line.clip_start(fw);
                                if (line.is_valid())
                                    line.clip_end(fw);
                            }
                        }
                    } else
                        lines.push_back(polyline);
                    polygons_append(bridges, offset(lines, 0.5f * w + 10.f));
                }
            }
        bridges = union_(bridges);

        for (const Surface &surface : layerm->fill_surfaces.surfaces)
            if (surface.surface_type == stBottomBridge && surface.bridge_angle != -1) {
                auto bbox      = get_extents(surface.expolygon);
                auto bbox_size = bbox.size();
                if (bbox_size[0] < max_bridge_length && bbox_size[1] < max_bridge_length)
                    polygons_append(bridges, surface.expolygon);
                else if (break_bridge) {
                    Polygons holes;
                    coord_t  x0       = bbox.min.x();
                    coord_t  x1       = bbox.max.x();
                    coord_t  y0       = bbox.min.y();
                    coord_t  y1       = bbox.max.y();
                    const int grid_lw = int(w / 2);

                    Vec2f bridge_direction{ cos(surface.bridge_angle), sin(surface.bridge_angle) };
                    if (fabs(bridge_direction(0)) > fabs(bridge_direction(1))) {
                        int step = bbox_size(0) / ceil(bbox_size(0) / max_bridge_length);
                        for (int x = x0 + step; x < x1; x += step) {
                            Polygon poly;
                            poly.points = { Point(x - grid_lw, y0), Point(x + grid_lw, y0), Point(x + grid_lw, y1), Point(x - grid_lw, y1) };
                            holes.emplace_back(poly);
                        }
                    } else {
                        int step = bbox_size(1) / ceil(bbox_size(1) / max_bridge_length);
                        for (int y = y0 + step; y < y1; y += step) {
                            Polygon poly;
                            poly.points = { Point(x0, y - grid_lw), Point(x0, y + grid_lw), Point(x1, y + grid_lw), Point(x1, y - grid_lw) };
                            holes.emplace_back(poly);
                        }
                    }
                    auto expoly = diff_ex(surface.expolygon, holes);
                    polygons_append(bridges, expoly);
                }
            }
        bridges = diff(
            bridges,
            offset(layerm->unsupported_bridge_edges, scale_(SUPPORT_MATERIAL_MARGIN), SUPPORT_SURFACES_OFFSET_PARAMETERS));
        append(all_bridges, bridges);
    }
    if (typeid(overhang_regions) == typeid(ExPolygons *))
        *(ExPolygons *)overhang_regions = diff_ex(*overhang_regions, all_bridges, ApplySafetyOffset::Yes);
    else if (typeid(overhang_regions) == typeid(Polygons *))
        *(Polygons *)overhang_regions = diff(*overhang_regions, all_bridges, ApplySafetyOffset::Yes);
}

template void PrintObject::remove_bridges_from_contacts<ExPolygons>(
    const Layer *lower_layer,
    const Layer *current_layer,
    float        extrusion_width,
    ExPolygons  *overhang_regions,
    float        max_bridge_length,
    bool         break_bridge);
template void PrintObject::remove_bridges_from_contacts<Polygons>(
    const Layer *lower_layer,
    const Layer *current_layer,
    float        extrusion_width,
    Polygons    *overhang_regions,
    float        max_bridge_length,
    bool         break_bridge);

static void project_triangles_to_slabs(ConstLayerPtrsAdaptor layers, const indexed_triangle_set &custom_facets, const Transform3f &tr, bool seam, std::vector<Polygons> &out)
{
    if (custom_facets.indices.empty())
        return;

    const float tr_det_sign = (tr.matrix().determinant() > 0. ? 1.f : -1.f);

    struct LightPolygon {
        LightPolygon() { pts.reserve(5); }
        LightPolygon(const std::array<Vec2f, 3> &tri)
        {
            pts.reserve(3);
            pts.emplace_back(scaled<coord_t>(tri.front()));
            pts.emplace_back(scaled<coord_t>(tri[1]));
            pts.emplace_back(scaled<coord_t>(tri.back()));
        }

        Points pts;

        void add(const Vec2f &pt)
        {
            pts.emplace_back(scaled<coord_t>(pt));
            assert(pts.size() <= 5);
        }
    };

    struct TriangleProjections {
        size_t                    first_layer_id;
        std::vector<LightPolygon> polygons;
    };

    std::vector<TriangleProjections> projections_of_triangles(custom_facets.indices.size());

    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, custom_facets.indices.size()),
        [&custom_facets, &tr, tr_det_sign, seam, layers, &projections_of_triangles](const tbb::blocked_range<size_t> &range) {
            for (size_t idx = range.begin(); idx < range.end(); ++idx) {
                std::array<Vec3f, 3> facet;
                for (int i = 0; i < 3; ++i)
                    facet[i] = tr * custom_facets.vertices[custom_facets.indices[idx](i)];

                float z_comp = (facet[1] - facet[0]).cross(facet[2] - facet[0]).z();
                if (!seam && tr_det_sign * z_comp > 0.)
                    continue;

                if (seam && z_comp == 0.f)
                    facet[0].x() += float(EPSILON);

                std::sort(facet.begin(), facet.end(), [](const Vec3f &pt1, const Vec3f &pt2) { return pt1.z() < pt2.z(); });

                std::array<Vec2f, 3> trianglef;
                for (int i = 0; i < 3; ++i)
                    trianglef[i] = to_2d(facet[i]);

                auto it = std::lower_bound(layers.begin(), layers.end(), facet[0].z() + EPSILON, [](const Layer *l1, float z) { return l1->slice_z < z; });

                size_t first_layer_id = projections_of_triangles[idx].first_layer_id = it - layers.begin();
                size_t last_layer_id  = first_layer_id;
                while (last_layer_id + 1 < layers.size() && float(layers[last_layer_id]->slice_z) <= facet[2].z())
                    ++last_layer_id;

                if (first_layer_id == last_layer_id) {
                    float dz = facet[2].z() - facet[0].z();
                    assert(dz >= 0);
                    bool add_below = dz < float(2. * EPSILON) && first_layer_id > 0 && layers[first_layer_id - 1]->slice_z > facet[0].z() - EPSILON;
                    projections_of_triangles[idx].polygons.reserve(add_below ? 2 : 1);
                    projections_of_triangles[idx].polygons.emplace_back(trianglef);
                    if (add_below) {
                        --projections_of_triangles[idx].first_layer_id;
                        projections_of_triangles[idx].polygons.emplace_back(trianglef);
                    }
                    continue;
                }

                projections_of_triangles[idx].polygons.resize(last_layer_id - first_layer_id + 1);

                Vec2f ta(trianglef[1] - trianglef[0]);
                Vec2f tb(trianglef[2] - trianglef[0]);
                ta *= 1.f / (facet[1].z() - facet[0].z());
                tb *= 1.f / (facet[2].z() - facet[0].z());

                LightPolygon *proj = &projections_of_triangles[idx].polygons[0];
                proj->add(trianglef[0]);

                bool passed_first = false;
                bool stop         = false;

                while (it != layers.end()) {
                    const float z = float((*it)->slice_z);

                    Vec2f a;
                    Vec2f b;

                    if (z > facet[1].z() && !passed_first) {
                        proj->add(trianglef[1]);
                        ta = trianglef[2] - trianglef[1];
                        ta *= 1.f / (facet[2].z() - facet[1].z());
                        passed_first = true;
                    }

                    if (z > facet[2].z() || it + 1 == layers.end()) {
                        proj->add(trianglef[2]);
                        stop = true;
                    } else {
                        a = passed_first ? (trianglef[1] + ta * (z - facet[1].z())) : (trianglef[0] + ta * (z - facet[0].z()));
                        b = trianglef[0] + tb * (z - facet[0].z());
                        proj->add(a);
                        proj->add(b);
                    }

                    if (stop)
                        break;

                    ++it;
                    ++proj;
                    assert(proj <= &projections_of_triangles[idx].polygons.back());

                    proj->add(b);
                    proj->add(a);
                }
            }
        });

    out.resize(layers.size());

    for (auto &trg : projections_of_triangles) {
        int layer_id = int(trg.first_layer_id);
        for (LightPolygon &poly : trg.polygons) {
            if (layer_id >= int(out.size()))
                break;
            assert(!poly.pts.empty());
            out[layer_id].emplace_back(std::move(poly.pts));
            ++layer_id;
        }
    }
}

void PrintObject::project_and_append_custom_facets(
    bool                                   seam,
    EnforcerBlockerType                    type,
    std::vector<Polygons>                 &out,
    std::vector<std::pair<Vec3f, Vec3f>> *vertical_points) const
{
    for (const ModelVolume *mv : this->model_object()->volumes)
        if (mv->is_model_part()) {
            const indexed_triangle_set custom_facets =
                seam ? mv->seam_facets.get_facets_strict(*mv, type) : mv->supported_facets.get_facets_strict(*mv, type);
            if (!custom_facets.indices.empty()) {
                if (seam)
                    project_triangles_to_slabs(this->layers(), custom_facets, (this->trafo_centered() * mv->get_matrix()).cast<float>(), seam, out);
                else {
                    std::vector<Polygons> projected;
                    slice_mesh_slabs(custom_facets, zs_from_layers(this->layers()), this->trafo_centered() * mv->get_matrix(), nullptr, &projected, vertical_points, []() {});
                    assert(!projected.empty());
                    assert(out.empty() || out.size() == projected.size());
                    if (out.empty())
                        out = std::move(projected);
                    else
                        for (size_t i = 0; i < out.size(); ++i)
                            append(out[i], std::move(projected[i]));
                }
            }
        }
}

} // namespace Slic3r
