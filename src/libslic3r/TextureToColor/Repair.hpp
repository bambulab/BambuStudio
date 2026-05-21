#pragma once
#include "TriMesh.hpp"
#include "CgalUtils.hpp"
#include "Callbacks.hpp"
#include <CGAL/Polygon_mesh_processing/border.h>
#include <CGAL/Polygon_mesh_processing/manifoldness.h>
#include <CGAL/Polygon_mesh_processing/repair_polygon_soup.h>
#include <CGAL/Polygon_mesh_processing/repair.h>
#include <CGAL/Polygon_mesh_processing/orient_polygon_soup.h>
#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>
#include <CGAL/Polygon_mesh_processing/stitch_borders.h>
#include <CGAL/Polygon_mesh_processing/triangulate_hole.h>
#include <boost/log/trivial.hpp>
#include <algorithm>
#include <cstddef>
#include <memory>

namespace Slic3r { namespace tex2color {

namespace PMP = CGAL::Polygon_mesh_processing;

// Default upper bound on the number of half-edges in any single boundary cycle
// that CloseBoundariesAndRepairManifoldness will attempt to triangulate. The
// cost of triangulate_hole grows non-linearly with cycle length, so this caps
// the worst-case per-hole work rather than the aggregate boundary size: a mesh
// with many small holes is still fully repaired, while a mesh containing one
// pathologically large hole skips triangulation entirely.
inline constexpr std::size_t MAX_REPAIRABLE_MESH_HOLE_EDGES = 1500;

inline void CloseBoundariesAndRepairManifoldness(
    cgalutils::CGALMesh& cgal_mesh,
    std::size_t max_hole_edges = MAX_REPAIRABLE_MESH_HOLE_EDGES)
{
    using CGALMesh = cgalutils::CGALMesh;
    using HalfedgeDescriptor = boost::graph_traits<CGALMesh>::halfedge_descriptor;
    using FaceDescriptor = boost::graph_traits<CGALMesh>::face_descriptor;

    PMP::stitch_borders(cgal_mesh);
    PMP::duplicate_non_manifold_vertices(cgal_mesh);

    std::vector<HalfedgeDescriptor> border_cycles;
    PMP::extract_boundary_cycles(cgal_mesh, std::back_inserter(border_cycles));

    auto cycle_length = [&](HalfedgeDescriptor h0) {
        std::size_t n = 0;
        HalfedgeDescriptor h = h0;
        do {
            ++n;
            h = next(h, cgal_mesh);
        } while (h != h0);
        return n;
    };

    std::size_t max_cycle_edges = 0;
    for (const HalfedgeDescriptor h : border_cycles)
        max_cycle_edges = std::max(max_cycle_edges, cycle_length(h));

    if (max_cycle_edges <= max_hole_edges) {
        for (const HalfedgeDescriptor h : border_cycles) {
            std::vector<FaceDescriptor> patch_faces;
            PMP::triangulate_hole(cgal_mesh, h, std::back_inserter(patch_faces));
        }
    } else {
        BOOST_LOG_TRIVIAL(info) << "TextureToColor: skip mesh hole triangulation, max cycle edges="
                                << max_cycle_edges << ", limit=" << max_hole_edges;
    }

    PMP::remove_degenerate_faces(cgal_mesh);
    PMP::duplicate_non_manifold_vertices(cgal_mesh);
}

inline bool RepairMesh(const TriMesh& mesh,
                       std::shared_ptr<TriMesh>& out_mesh,
                       AlgoProgressCallback progress_callback = nullptr,
                       AlgoCancelCallback cancel_callback = nullptr)
{
    // Convert TriMesh to polygon soup (point container + triangle index container)
    std::vector<cgalutils::Kernel::Point_3> soup_points;
    std::vector<std::vector<std::size_t>> soup_triangles;

    soup_points.reserve(mesh.vertices.size());
    for (const TriVertex& v : mesh.vertices) {
        soup_points.emplace_back(v.x(), v.y(), v.z());
    }

    soup_triangles.reserve(mesh.indices.size());
    for (const TriFace& f : mesh.indices) {
        soup_triangles.push_back({static_cast<std::size_t>(f[0]),
                                  static_cast<std::size_t>(f[1]),
                                  static_cast<std::size_t>(f[2])});
    }

    if (progress_callback) {
        progress_callback({30, "Repairing polygon soup"});
    }
    if (cancel_callback && cancel_callback()) {
        return false;
    }

    PMP::repair_polygon_soup(soup_points, soup_triangles);

    if (progress_callback) {
        progress_callback({50, "Orienting polygon soup"});
    }
    if (cancel_callback && cancel_callback()) {
        return false;
    }

    PMP::orient_polygon_soup(soup_points, soup_triangles);

    if (progress_callback) {
        progress_callback({70, "Converting to CGAL mesh"});
    }
    if (cancel_callback && cancel_callback()) {
        return false;
    }

    cgalutils::CGALMesh cgal_mesh;
    PMP::polygon_soup_to_polygon_mesh(soup_points, soup_triangles, cgal_mesh);

    PMP::remove_degenerate_faces(cgal_mesh);

    if (progress_callback) {
        progress_callback({80, "Closing mesh boundaries"});
    }
    if (cancel_callback && cancel_callback()) {
        return false;
    }

    CloseBoundariesAndRepairManifoldness(cgal_mesh);

    if (progress_callback) {
        progress_callback({85, "Converting from CGAL mesh"});
    }
    if (cancel_callback && cancel_callback()) {
        return false;
    }

    auto out = std::make_shared<TriMesh>(cgalutils::cgal_to_trimesh(cgal_mesh));

    out_mesh = std::move(out);
    if (progress_callback) {
        progress_callback({100, "Done"});
    }

    return true;
}

} // namespace tex2color
} // namespace Slic3r
