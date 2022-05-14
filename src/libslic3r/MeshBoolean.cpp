#include "Exception.hpp"
#include "MeshBoolean.hpp"
#include "I18N.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TryCatchSignal.hpp"
#undef PI

// Include igl first. It defines "L" macro which then clashes with our localization
#include <igl/copyleft/cgal/mesh_boolean.h>
#undef L

// CGAL headers
#include <CGAL/Polygon_mesh_processing/corefinement.h>
#include <CGAL/Exact_integer.h>
#include <CGAL/Surface_mesh.h>
#include <CGAL/Polygon_mesh_processing/orient_polygon_soup.h>
#include <CGAL/Polygon_mesh_processing/orient_polygon_soup_extension.h>
#include <CGAL/Polygon_mesh_processing/repair_polygon_soup.h>
#include <CGAL/Polygon_mesh_processing/repair.h>
//TODO: for self intersections later
//#include <CGAL/Polygon_mesh_processing/repair_self_intersections.h>
#include <CGAL/Polygon_mesh_processing/remesh.h>
#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>
#include <CGAL/Polygon_mesh_processing/orientation.h>
// BBS: for segment
#include <CGAL/mesh_segmentation.h>
#include <CGAL/property_map.h>
#include <CGAL/boost/graph/copy_face_graph.h>
#include <CGAL/boost/graph/Face_filtered_graph.h>

#include <boost/log/trivial.hpp>

// Mark string for localization and translate.
#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r {
namespace MeshBoolean {

using MapMatrixXfUnaligned = Eigen::Map<const Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor | Eigen::DontAlign>>;
using MapMatrixXiUnaligned = Eigen::Map<const Eigen::Matrix<int,   Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor | Eigen::DontAlign>>;

TriangleMesh eigen_to_triangle_mesh(const EigenMesh &emesh)
{
    auto &VC = emesh.first; auto &FC = emesh.second;

    indexed_triangle_set its;
    its.vertices.reserve(size_t(VC.rows()));
    its.indices.reserve(size_t(FC.rows()));

    for (Eigen::Index i = 0; i < VC.rows(); ++i)
        its.vertices.emplace_back(VC.row(i).cast<float>());

    for (Eigen::Index i = 0; i < FC.rows(); ++i)
        its.indices.emplace_back(FC.row(i));

    return TriangleMesh { std::move(its) };
}

EigenMesh triangle_mesh_to_eigen(const TriangleMesh &mesh)
{
    EigenMesh emesh;
    emesh.first = MapMatrixXfUnaligned(mesh.its.vertices.front().data(),
                                       Eigen::Index(mesh.its.vertices.size()),
                                       3).cast<double>();

    emesh.second = MapMatrixXiUnaligned(mesh.its.indices.front().data(),
                                        Eigen::Index(mesh.its.indices.size()),
                                        3);
    return emesh;
}

void minus(EigenMesh &A, const EigenMesh &B)
{
    auto &[VA, FA] = A;
    auto &[VB, FB] = B;

    Eigen::MatrixXd VC;
    Eigen::MatrixXi FC;
    igl::MeshBooleanType boolean_type(igl::MESH_BOOLEAN_TYPE_MINUS);
    igl::copyleft::cgal::mesh_boolean(VA, FA, VB, FB, boolean_type, VC, FC);

    VA = std::move(VC); FA = std::move(FC);
}

void minus(TriangleMesh& A, const TriangleMesh& B)
{
    EigenMesh eA = triangle_mesh_to_eigen(A);
    minus(eA, triangle_mesh_to_eigen(B));
    A = eigen_to_triangle_mesh(eA);
}

void self_union(EigenMesh &A)
{
    EigenMesh result;
    auto &[V, F] = A;
    auto &[VC, FC] = result;

    igl::MeshBooleanType boolean_type(igl::MESH_BOOLEAN_TYPE_UNION);
    igl::copyleft::cgal::mesh_boolean(V, F, Eigen::MatrixXd(), Eigen::MatrixXi(), boolean_type, VC, FC);

    A = std::move(result);
}

void self_union(TriangleMesh& mesh)
{
    auto eM = triangle_mesh_to_eigen(mesh);
    self_union(eM);
    mesh = eigen_to_triangle_mesh(eM);
}

namespace cgal {

namespace CGALProc    = CGAL::Polygon_mesh_processing;
namespace CGALParams  = CGAL::Polygon_mesh_processing::parameters;

using EpecKernel = CGAL::Exact_predicates_exact_constructions_kernel;
using EpicKernel = CGAL::Exact_predicates_inexact_constructions_kernel;
using _EpicMesh = CGAL::Surface_mesh<EpicKernel::Point_3>;
using _EpecMesh = CGAL::Surface_mesh<EpecKernel::Point_3>;

struct CGALMesh {
    _EpicMesh m;
    CGALMesh() = default;
    CGALMesh(const _EpicMesh& _m) :m(_m) {}
};

typedef boost::graph_traits<_EpicMesh>::vertex_descriptor   vertex_descriptor;
typedef boost::graph_traits<_EpicMesh>::halfedge_descriptor halfedge_descriptor;
typedef boost::graph_traits<_EpicMesh>::face_descriptor     face_descriptor;


// /////////////////////////////////////////////////////////////////////////////
// Converions from and to CGAL mesh
// /////////////////////////////////////////////////////////////////////////////

template<class _Mesh> void triangle_mesh_to_cgal(const TriangleMesh& M, _Mesh& out)
{
    using Index3 = std::array<size_t, 3>;

    if (M.empty()) return;

    std::vector<typename _Mesh::Point> points;
    std::vector<Index3> indices;
    points.reserve(M.its.vertices.size());
    indices.reserve(M.its.indices.size());
    for (auto& v : M.its.vertices) points.emplace_back(v.x(), v.y(), v.z());
    for (auto& _f : M.its.indices) {
        auto f = _f.cast<size_t>();
        indices.emplace_back(Index3{ f(0), f(1), f(2) });
    }

    CGALProc::orient_polygon_soup(points, indices);
    CGALProc::polygon_soup_to_polygon_mesh(points, indices, out);

    // Number the faces because 'orient_to_bound_a_volume' needs a face <--> index map
    unsigned index = 0;
    for (auto face : out.faces()) face = CGAL::SM_Face_index(index++);

    if (CGAL::is_closed(out))
        CGALProc::orient_to_bound_a_volume(out);
    else
        throw Slic3r::RuntimeError("Mesh not watertight");
}

template<class _Mesh> void triangle_mesh_to_cgal_with_repair(const TriangleMesh& M, _Mesh& out, bool& oriented)
{
    //using Index3 = std::array<size_t, 3>;
    using Index3 = std::vector<std::size_t>;

    if (M.empty()) return;

    std::vector<typename _Mesh::Point> points;
    std::vector<Index3> indices;
    points.reserve(M.its.vertices.size());
    indices.reserve(M.its.indices.size());
    for (auto& v : M.its.vertices) points.emplace_back(v.x(), v.y(), v.z());
    for (auto& _f : M.its.indices) {
        auto f = _f.cast<size_t>();
        indices.emplace_back(Index3{ f(0), f(1), f(2) });
    }

    //basic repair, currently disabled
    //CGALProc::repair_polygon_soup(points, indices, CGAL::parameters::all_default());
    //following operations are the sequential calls in repair_polygon_soup, most of them have been done by admesh's stl functions
    //CGALProc::merge_duplicate_points_in_polygon_soup(points, indices, CGAL::parameters::all_default());
    //CGALProc::internal::simplify_polygons_in_polygon_soup(points, indices, traits);
    //CGALProc::internal::split_pinched_polygons_in_polygon_soup(points, indices, traits);
    //CGALProc::internal::remove_invalid_polygons_in_polygon_soup(points, indices);

    //CGALProc::repair_polygon_soup(points, indices, CGAL::parameters::erase_all_duplicates(true));
    CGALProc::merge_duplicate_polygons_in_polygon_soup(points, indices,
        CGAL::parameters::require_same_orientation(false).erase_all_duplicates(true));
    CGALProc::remove_isolated_points_in_polygon_soup(points, indices);
    CGALProc::merge_duplicate_points_in_polygon_soup(points, indices, CGAL::parameters::all_default());

    //CGALProc::duplicate_non_manifold_edges_in_polygon_soup(points, indices);
    CGALProc::orient_polygon_soup(points, indices);
    if (CGALProc::is_polygon_soup_a_polygon_mesh(indices)) {
        CGALProc::polygon_soup_to_polygon_mesh(points, indices, out);
    }
    else {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(":is not a valid polygon_mesh, points_x_3 %1%, faces_x_3 %2%")
            %points.size() %indices.size();
        throw Slic3r::RuntimeError("This Mesh Can not be fixed locally.");
    }

    // Number the faces because 'orient_to_bound_a_volume' needs a face <--> index map
    unsigned index = 0;
    for (auto face : out.faces())
        face = CGAL::SM_Face_index(index++);

    if (CGAL::is_closed(out)) {
        CGALProc::orient_to_bound_a_volume(out);
        oriented = true;
    }
}

template<class _Mesh>
void triangle_mesh_to_cgal(const std::vector<stl_vertex> &                 V,
                           const std::vector<stl_triangle_vertex_indices> &F,
                           _Mesh &out)
{
    if (F.empty()) return;

    size_t vertices_count = V.size();
    size_t edges_count    = (F.size()* 3) / 2;
    size_t faces_count    = F.size();
    out.reserve(vertices_count, edges_count, faces_count);

    for (auto &v : V)
        out.add_vertex(typename _Mesh::Point{v.x(), v.y(), v.z()});

    using VI = typename _Mesh::Vertex_index;
    for (auto &f : F)
        out.add_face(VI(f(0)), VI(f(1)), VI(f(2)));
}

inline Vec3f to_vec3f(const _EpicMesh::Point& v)
{
    return { float(v.x()), float(v.y()), float(v.z()) };
}

inline Vec3f to_vec3f(const _EpecMesh::Point& v)
{
    CGAL::Cartesian_converter<EpecKernel, EpicKernel> cvt;
    auto iv = cvt(v);
    return { float(iv.x()), float(iv.y()), float(iv.z()) };
}

template<class _Mesh> TriangleMesh cgal_to_triangle_mesh(const _Mesh &cgalmesh)
{
    indexed_triangle_set its;
    its.vertices.reserve(cgalmesh.num_vertices());
    its.indices.reserve(cgalmesh.num_faces());

    const auto &faces = cgalmesh.faces();
    const auto &vertices = cgalmesh.vertices();
    int vsize = int(vertices.size());

    for (auto &vi : vertices) {
        auto &v = cgalmesh.point(vi); // Don't ask...
        its.vertices.emplace_back(to_vec3f(v));
    }

    for (auto &face : faces) {
        auto vtc = cgalmesh.vertices_around_face(cgalmesh.halfedge(face));

        int i = 0;
        Vec3i facet;
        for (auto v : vtc) {
            int iv = v;
            if (i > 2 || iv < 0 || iv >= vsize) { i = 0; break; }
            facet(i++) = iv;
        }

        if (i == 3)
            its.indices.emplace_back(facet);
    }

    return TriangleMesh(std::move(its));
}

std::unique_ptr<CGALMesh, CGALMeshDeleter>
triangle_mesh_to_cgal(const std::vector<stl_vertex> &V,
                      const std::vector<stl_triangle_vertex_indices> &F)
{
    std::unique_ptr<CGALMesh, CGALMeshDeleter> out(new CGALMesh{});
    triangle_mesh_to_cgal(V, F, out->m);
    return out;
}

TriangleMesh cgal_to_triangle_mesh(const CGALMesh &cgalmesh)
{
    return cgal_to_triangle_mesh(cgalmesh.m);
}

// /////////////////////////////////////////////////////////////////////////////
// Boolean operations for CGAL meshes
// /////////////////////////////////////////////////////////////////////////////

static bool _cgal_diff(CGALMesh &A, CGALMesh &B, CGALMesh &R)
{
    const auto &p = CGALParams::throw_on_self_intersection(true);
    return CGALProc::corefine_and_compute_difference(A.m, B.m, R.m, p, p);
}

static bool _cgal_union(CGALMesh &A, CGALMesh &B, CGALMesh &R)
{
    const auto &p = CGALParams::throw_on_self_intersection(true);
    return CGALProc::corefine_and_compute_union(A.m, B.m, R.m, p, p);
}

static bool _cgal_intersection(CGALMesh &A, CGALMesh &B, CGALMesh &R)
{
    const auto &p = CGALParams::throw_on_self_intersection(true);
    return CGALProc::corefine_and_compute_intersection(A.m, B.m, R.m, p, p);
}

template<class Op> void _cgal_do(Op &&op, CGALMesh &A, CGALMesh &B)
{
    bool success = false;
    bool hw_fail = false;
    try {
        CGALMesh result;
        try_catch_signal({SIGSEGV, SIGFPE}, [&success, &A, &B, &result, &op] {
            success = op(A, B, result);
        }, [&] { hw_fail = true; });
        A = std::move(result);      // In-place operation does not work
    } catch (...) {
        success = false;
    }

    if (hw_fail)
        throw Slic3r::HardCrash("CGAL mesh boolean operation crashed.");

    if (! success)
        throw Slic3r::RuntimeError("CGAL mesh boolean operation failed.");
}

void minus(CGALMesh &A, CGALMesh &B) { _cgal_do(_cgal_diff, A, B); }
void plus(CGALMesh &A, CGALMesh &B) { _cgal_do(_cgal_union, A, B); }
void intersect(CGALMesh &A, CGALMesh &B) { _cgal_do(_cgal_intersection, A, B); }
bool does_self_intersect(const CGALMesh &mesh) { return CGALProc::does_self_intersect(mesh.m); }
bool is_watertight(const CGALMesh &mesh)
{
    return is_closed(mesh.m);
}

// BBS
void segment(CGALMesh& src, std::vector<CGALMesh>& dst, double smoothing_alpha = 0.5, int segment_number=5)
{
    typedef boost::graph_traits<_EpicMesh>::face_descriptor face_descriptor;
    typedef _EpicMesh::Property_map<face_descriptor, double> Facet_double_map;
    typedef CGAL::Face_filtered_graph<_EpicMesh> Filtered_graph;

    _EpicMesh mesh = src.m;
    Facet_double_map sdf_property_map;

    sdf_property_map = mesh.add_property_map<face_descriptor, double>("f:sdf").first;

    CGAL::sdf_values(mesh, sdf_property_map);

    // create a property-map for segment-ids
    typedef _EpicMesh::Property_map<face_descriptor, std::size_t> Facet_int_map;
    Facet_int_map segment_property_map = mesh.add_property_map<face_descriptor, std::size_t>("f:sid").first;;
    // segment the mesh using default parameters for number of levels, and smoothing lambda
    // Any other scalar values can be used instead of using SDF values computed using the CGAL function
    std::size_t number_of_segments = CGAL::segmentation_from_sdf_values(mesh, sdf_property_map, segment_property_map, segment_number, smoothing_alpha);
    //print area of each segment and then put it in a Mesh and print it in an OFF file
    Filtered_graph segment_mesh(mesh);
    _EpicMesh mesh_merged;
    for (std::size_t id = 0; id < number_of_segments; ++id)
    {
        segment_mesh.set_selected_faces(id, segment_property_map);
        //std::cout << "Segment " << id << "'s area is : " << CGAL::Polygon_mesh_processing::area(segment_mesh) << std::endl;
        _EpicMesh out;
        CGAL::copy_face_graph(segment_mesh, out);

        //std::ostringstream oss;
        //oss << "Segment_" << id << ".off";
        //std::ofstream os(oss.str().data());
        //os << out;

        // fill holes
        typedef boost::graph_traits<_EpicMesh>::halfedge_descriptor      halfedge_descriptor;
        typedef boost::graph_traits<_EpicMesh>::vertex_descriptor        vertex_descriptor;
        std::vector<halfedge_descriptor> border_cycles;
        CGAL::Polygon_mesh_processing::extract_boundary_cycles(out, std::back_inserter(border_cycles));
        for (halfedge_descriptor h : border_cycles)
        {
            std::vector<face_descriptor>  patch_facets;
#if 0
            std::vector<vertex_descriptor> patch_vertices;
            CGAL::Polygon_mesh_processing::triangulate_and_refine_hole(out, h, std::back_inserter(patch_facets),
                std::back_inserter(patch_vertices));
            std::cout << "* Number of facets in constructed patch: " << patch_facets.size() << std::endl;
            std::cout << "  Number of vertices in constructed patch: " << patch_vertices.size() << std::endl;
#else
            CGAL::Polygon_mesh_processing::triangulate_hole(out, h, std::back_inserter(patch_facets));
#endif
        }

        //if (id > 2) {
        //    mesh_merged.join(out);
        //}
        //else
        {
            dst.emplace_back(std::move(CGALMesh(out)));
        }
    }
    //if (mesh_merged.is_empty() == false) {
    //    CGAL::Polygon_mesh_processing::stitch_borders(mesh_merged);
    //    dst.emplace_back(std::move(CGALMesh(mesh_merged)));
    //}
}

std::vector<TriangleMesh> segment(const TriangleMesh& src, double smoothing_alpha, int segment_number)
{
    CGALMesh in_cgal_mesh;
    MeshBoolean::cgal::triangle_mesh_to_cgal(src, in_cgal_mesh.m);
    std::vector<CGALMesh> out_cgal_meshes;
    segment(in_cgal_mesh, out_cgal_meshes, smoothing_alpha, segment_number);

    std::vector<TriangleMesh> out_meshes;
    for (auto& outf_cgal_mesh: out_cgal_meshes)
    {
        out_meshes.emplace_back(std::move(cgal_to_triangle_mesh(outf_cgal_mesh.m)));
    }

    return out_meshes;
}

void merge(std::vector<_EpicMesh>& srcs, _EpicMesh& dst)
{
    _EpicMesh mesh_merged;
    for (size_t i = 0; i < srcs.size(); i++)
    {
        mesh_merged.join(srcs[i]);
    }
    if (mesh_merged.is_empty() == false) {
        CGAL::Polygon_mesh_processing::stitch_borders(mesh_merged);
        dst = std::move(mesh_merged);
    }
}

TriangleMesh merge(std::vector<TriangleMesh> meshes)
{
    std::vector<_EpicMesh> srcs(meshes.size());
    for (size_t i = 0; i < meshes.size(); i++)
    {
        MeshBoolean::cgal::triangle_mesh_to_cgal(meshes[i], srcs[i]);
    }
    _EpicMesh dst;
    merge(srcs, dst);
    return cgal_to_triangle_mesh(dst);
}

static bool is_small_hole(halfedge_descriptor h, _EpicMesh & mesh,
                   double max_hole_diam, int max_num_hole_edges)
{
  int num_hole_edges = 0;
  CGAL::Bbox_3 hole_bbox;
  for (halfedge_descriptor hc : CGAL::halfedges_around_face(h, mesh))
  {
    const EpicKernel::Point_3& p = mesh.point(target(hc, mesh));
    hole_bbox += p.bbox();
    ++num_hole_edges;
    // Exit early, to avoid unnecessary traversal of large holes
    if (num_hole_edges > max_num_hole_edges) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", num_hole_edges %1% too large")%num_hole_edges;
        return false;
    }
    double x_range = hole_bbox.xmax() - hole_bbox.xmin();
    if (x_range > max_hole_diam) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", x_range %1% too large")%x_range;
        return false;
    }

    double y_range = hole_bbox.ymax() - hole_bbox.ymin();
    if (y_range > max_hole_diam) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", y_range %1% too large")%y_range;
        return false;
    }

    double z_range = hole_bbox.zmax() - hole_bbox.zmin();
    if (z_range > max_hole_diam) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", z_range %1% too large")%z_range;
        return false;
    }
  }
  return true;
}

//fill holes except small size and edges
static int fill_hole(CGALMesh& cgal_mesh, int& hole_filled)
{
    double max_hole_diam = 250.f; //todo, not sure how large is suitable
    int max_num_hole_edges = 200; //meshlab's default is 30, not enough for NASA1.stl
    unsigned int nb_holes = 0;
    std::vector<halfedge_descriptor> border_cycles;
    int ret = 0;

    CGALProc::extract_boundary_cycles(cgal_mesh.m, std::back_inserter(border_cycles));

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", found total %1% border cycles")%border_cycles.size();
    for(halfedge_descriptor h : border_cycles)
    {
        /*if(max_hole_diam > 0 && max_num_hole_edges > 0 &&
            !is_small_hole(h, cgal_mesh.m, max_hole_diam, max_num_hole_edges))
        {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", skip this hole");
            continue;
        }*/
        std::vector<face_descriptor>  patch_facets;
        std::vector<vertex_descriptor> patch_vertices;
        bool success = std::get<0>(CGALProc::triangulate_refine_and_fair_hole(cgal_mesh.m,
                                     h,
                                     std::back_inserter(patch_facets),
                                     std::back_inserter(patch_vertices)));
        if (!success)
            ret = -1;
        else
            nb_holes ++;
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": hole count %1%, Number of facets %2%, vertices %3%, success %4%")
            %nb_holes %patch_facets.size() %patch_vertices.size()%success;
    }
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(" %1% holes filled")%nb_holes;

    hole_filled = nb_holes;
    return ret;
}

TriangleMesh repair(const TriangleMesh& src, std::string& repair_msg, bool& user_cancel, mesh_status_update_func func)
{
    int ret = 0;
    CGALMesh in_cgal_mesh;
    bool oriented = false;

    if (func)
        func(5, L("Preparing mesh data..."));
    //construct the cgal mesh with some basic repair operations
    MeshBoolean::cgal::triangle_mesh_to_cgal_with_repair(src, in_cgal_mesh.m, oriented);
    if (func)
        func(20, L("Duplicate non-manifold vertices..."));
    if (user_cancel)
        throw Slic3r::RuntimeError("User cancelled");

    //duplicate the non-manifold vertices
    std::vector<std::vector<vertex_descriptor> > duplicated_vertices;
    std::size_t new_vertices_nb = CGALProc::duplicate_non_manifold_vertices(in_cgal_mesh.m,
                                     CGAL::parameters::output_iterator(std::back_inserter(duplicated_vertices)));
    if (new_vertices_nb > 0) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", found %1% non_manifold_vertices, duplicated.")%new_vertices_nb;
    }

    if (func)
        func(40, L("Filling holes..."));
    if (user_cancel)
        throw Slic3r::RuntimeError("User cancelled");

    //fill hole
    int hole_filled = 0;
    ret = fill_hole(in_cgal_mesh, hole_filled);
    if (ret) {
        repair_msg += "\nFound errors when Filling holes.";
    }
    else {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", fill hole finished, filled %1% holes")%hole_filled;
        //std::size_t count = CGALProc::stitch_borders(in_cgal_mesh.m);
        //if (count > 0)
        //    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", stitched %1% borders")%count;
    }

    //remove the self intersection
    //it will also do: remove_degenerate_faces, duplicate_non_manifold_vertices
    //TODO: the effect is not what we want, we need to check whether we should remove self intersection or not firstly
    //if it is a must, we need to pick another algorithism
    //CGALProc::experimental::remove_self_intersections(in_cgal_mesh.m, CGAL::parameters::preserve_genus(false));
    //CGALProc::experimental::remove_self_intersections(in_cgal_mesh.m);

    if (func)
        func(80, L("Correcting orientation..."));
    if (user_cancel)
        throw Slic3r::RuntimeError("User cancelled");

    if (CGAL::is_closed(in_cgal_mesh.m)) {
        if (!oriented) {
        // Number the faces because 'orient_to_bound_a_volume' needs a face <--> index map
            //unsigned index = 0;
            //for (auto face : in_cgal_mesh.m.faces())
            //    face = CGAL::SM_Face_index(index++);
            CGALProc::orient_to_bound_a_volume(in_cgal_mesh.m, CGALProc::parameters::do_orientation_tests(false));
        }
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", after repair, it is a watertight mesh");
    }
    else {
        //throw Slic3r::RuntimeError("Mesh not watertight");
        //still not a watertight mesh
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", after repair, still not a watertight mesh");
    }

    if (func)
        func(90, L("Generating meshes..."));
    return std::move(cgal_to_triangle_mesh(in_cgal_mesh.m));
}

// /////////////////////////////////////////////////////////////////////////////
// Now the public functions for TriangleMesh input:
// /////////////////////////////////////////////////////////////////////////////

template<class Op> void _mesh_boolean_do(Op &&op, TriangleMesh &A, const TriangleMesh &B)
{
    CGALMesh meshA;
    CGALMesh meshB;
    triangle_mesh_to_cgal(A.its.vertices, A.its.indices, meshA.m);
    triangle_mesh_to_cgal(B.its.vertices, B.its.indices, meshB.m);

    _cgal_do(op, meshA, meshB);

    A = cgal_to_triangle_mesh(meshA.m);
}

void minus(TriangleMesh &A, const TriangleMesh &B)
{
    _mesh_boolean_do(_cgal_diff, A, B);
}

void plus(TriangleMesh &A, const TriangleMesh &B)
{
    _mesh_boolean_do(_cgal_union, A, B);
}

void intersect(TriangleMesh &A, const TriangleMesh &B)
{
    _mesh_boolean_do(_cgal_intersection, A, B);
}

bool does_self_intersect(const TriangleMesh &mesh)
{
    CGALMesh cgalm;
    triangle_mesh_to_cgal(mesh.its.vertices, mesh.its.indices, cgalm.m);
    return CGALProc::does_self_intersect(cgalm.m);
}

void CGALMeshDeleter::operator()(CGALMesh *ptr) { delete ptr; }

bool does_bound_a_volume(const CGALMesh &mesh)
{
    return CGALProc::does_bound_a_volume(mesh.m);
}

bool empty(const CGALMesh &mesh)
{
    return mesh.m.is_empty();
}

} // namespace cgal

} // namespace MeshBoolean
} // namespace Slic3r
