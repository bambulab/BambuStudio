#include "FaceDetector.hpp"
#include "TriangleMesh.hpp"
#include "SLA/IndexedMesh.hpp"
#include "Model.hpp"

namespace Slic3r {
void FaceDetector::detect_exterior_face()
{
    if (m_mo == nullptr)
        return;

    struct VolumeFacetRange {
        VolumeFacetRange(ModelVolume* mv, uint32_t facet_begin, uint32_t facet_end) :
            mv(mv), facet_begin(facet_begin), facet_end(facet_end) {}
        VolumeFacetRange() : mv(nullptr), facet_begin(0), facet_end(0) {}
        ModelVolume* mv;
        uint32_t facet_begin;
        uint32_t facet_end;
    };

    const double BBOX_OFFSET = 2.0;

    const ModelInstance* mi = m_mo->instances[0];
    TriangleMesh object_mesh;
    Vec3d inst_ofs = mi->get_offset();
    std::vector<VolumeFacetRange> volume_facet_ranges;
    for (ModelVolume* mv : m_mo->volumes) {
        if (!mv->is_model_part())
            continue;

        TriangleMesh vol_mesh = mv->mesh();
        volume_facet_ranges.emplace_back(mv, object_mesh.stats().number_of_facets, object_mesh.stats().number_of_facets + vol_mesh.stats().number_of_facets);

        vol_mesh.transform(mv->get_matrix());
        object_mesh.merge(vol_mesh);
    }

    for (ModelVolume* mv : m_mo->volumes) {
        TriangleMesh& vol_mesh = const_cast<TriangleMesh&>(mv->mesh());
        if (vol_mesh.its.properties.size() < vol_mesh.its.indices.size()) {
            vol_mesh.its.properties.clear();
            vol_mesh.its.properties.resize(vol_mesh.its.indices.size());
        }

        for (FaceProperty& face_prop : vol_mesh.its.properties) {
            face_prop.type = eNormal;
        }
    }

    sla::IndexedMesh indexed_mesh(object_mesh);
    BoundingBoxf3 bbox = object_mesh.bounding_box();
    //bbox.translate(inst_ofs);
    bbox.offset(BBOX_OFFSET);

    std::unordered_set<size_t> hit_face_indices;

    // x-axis rays
    for (double y = bbox.min.y(); y < bbox.max.y(); y += m_sample_interval) {
        for (double z = bbox.min.z(); z < bbox.max.z(); z += m_sample_interval) {
            auto hit_result = indexed_mesh.query_ray_hit({ bbox.min.x(), y, z }, { 1.0, 0.0, 0.0 });
            if (hit_result.is_hit())
                hit_face_indices.insert(hit_result.face());

            hit_result = indexed_mesh.query_ray_hit({ bbox.max.x(), y, z }, { -1.0, 0.0, 0.0 });
            if (hit_result.is_hit())
                hit_face_indices.insert(hit_result.face());
        }
    }

    // y-axis rays
    for (double x = bbox.min.x(); x < bbox.max.x(); x += m_sample_interval) {
        for (double z = bbox.min.z(); z < bbox.max.z(); z += m_sample_interval) {
            auto hit_result = indexed_mesh.query_ray_hit({ x, bbox.min.y(), z }, { 0.0, 1.0, 0.0 });
            if (hit_result.is_hit())
                hit_face_indices.insert(hit_result.face());

            hit_result = indexed_mesh.query_ray_hit({ x, bbox.max.y(), z }, { 0.0, -1.0, 0.0 });
            if (hit_result.is_hit())
                hit_face_indices.insert(hit_result.face());
        }
    }

    // z-axis rays
    for (double x = bbox.min.x(); x < bbox.max.x(); x += m_sample_interval) {
        for (double y = bbox.min.y(); y < bbox.max.y(); y += m_sample_interval) {
            auto hit_result = indexed_mesh.query_ray_hit({ x, y, bbox.min.z() }, { 0.0, 0.0, 1.0 });
            if (hit_result.is_hit())
                hit_face_indices.insert(hit_result.face());

            hit_result = indexed_mesh.query_ray_hit({ x, y, bbox.max.z() }, { 0.0, 0.0, -1.0 });
            if (hit_result.is_hit())
                hit_face_indices.insert(hit_result.face());
        }
    }

    for (size_t facet_idx : hit_face_indices) {
        ModelVolume* mv = nullptr;
        uint32_t vol_facet_idx = 0;
        for (auto range : volume_facet_ranges) {
            if (facet_idx >= range.facet_begin && facet_idx < range.facet_end) {
                mv = range.mv;
                vol_facet_idx = facet_idx - range.facet_begin;
                break;
            }
        }

        // TODO: add exterior flag
        TriangleMesh& vol_mesh = const_cast<TriangleMesh&>(mv->mesh());
        vol_mesh.its.properties[vol_facet_idx].type = EnumFaceTypes::eExteriorAppearance;

        // check
#if 0
        for (int i = 0; i < 3; i++) {
            stl_vertex& obj_facet_vert = object_mesh.its.vertices[object_mesh.its.indices[facet_idx](i)];
            stl_vertex& vol_facet_vert = vol_facet.vertex[i];
            Vec3d vol_ofs = mv->get_offset() + inst_ofs;
            if (std::abs(obj_facet_vert(0) - vol_facet_vert(0) - vol_ofs(0)) > EPSILON) {
                printf("vertex not match\n");
            }
        }
#endif
    }
}
}
