#include "Exception.hpp"
#include "MeshBoolean.hpp"
#include "MeshDiagnostics.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TryCatchSignal.hpp"

#include <OpenMeshCraft/OpenMeshCraftBoolean.h>

#include <array>
#include <chrono>
#include "libslic3r/Log.hpp"

namespace Slic3r {
namespace MeshBoolean {
namespace openmeshcraft {

namespace {

struct OMCInput
{
    std::vector<BambuOMCPoint>    points;
    std::vector<BambuOMCTriangle> triangles;

    BambuOMCMesh view() const
    {
        return {points.data(), points.size(), triangles.data(), triangles.size()};
    }
};

struct OMCOutput
{
    std::vector<BambuOMCPoint>    points;
    std::vector<BambuOMCTriangle> triangles;
};

OMCInput triangle_mesh_to_openmeshcraft(const TriangleMesh &mesh)
{
    OMCInput input;
    input.points.reserve(mesh.its.vertices.size());
    input.triangles.reserve(mesh.its.indices.size());

    for (const stl_vertex &vertex : mesh.its.vertices)
        input.points.push_back({static_cast<double>(vertex.x()), static_cast<double>(vertex.y()), static_cast<double>(vertex.z())});

    for (const stl_triangle_vertex_indices &triangle : mesh.its.indices) {
        input.triangles.push_back({{
            static_cast<size_t>(triangle.x()),
            static_cast<size_t>(triangle.y()),
            static_cast<size_t>(triangle.z())}});
    }

    return input;
}

void BAMBU_OMC_CALL receive_openmeshcraft_result(
    const BambuOMCPoint *points,
    size_t point_count,
    const BambuOMCTriangle *triangles,
    size_t triangle_count,
    void *user_data)
{
    auto &output = *static_cast<OMCOutput *>(user_data);
    output.points.clear();
    output.triangles.clear();
    if (point_count != 0)
        output.points.assign(points, points + point_count);
    if (triangle_count != 0)
        output.triangles.assign(triangles, triangles + triangle_count);
}

TriangleMesh openmeshcraft_to_triangle_mesh(const OMCOutput &output)
{
    indexed_triangle_set its;
    its.vertices.reserve(output.points.size());
    its.indices.reserve(output.triangles.size());

    for (const BambuOMCPoint &point : output.points)
        its.vertices.emplace_back(static_cast<float>(point.x), static_cast<float>(point.y), static_cast<float>(point.z));

    for (const BambuOMCTriangle &triangle : output.triangles) {
        its.indices.emplace_back(
            static_cast<int>(triangle.vertices[0]),
            static_cast<int>(triangle.vertices[1]),
            static_cast<int>(triangle.vertices[2]));
    }

    return TriangleMesh{std::move(its)};
}

void append_oriented_result(TriangleMesh &&mesh, std::vector<TriangleMesh> &dst_mesh)
{
    if (mesh.empty())
        return;

    std::vector<TriangleMesh> parts = mesh.split();
    if (parts.size() > 1) {
        TriangleMesh fixed_mesh;
        for (auto &part : parts) {
            if (part.volume() < 0)
                part.flip_triangles();
            fixed_mesh.merge(part);
        }
        dst_mesh.push_back(std::move(fixed_mesh));
    } else {
        if (mesh.volume() < 0)
            mesh.flip_triangles();
        dst_mesh.push_back(std::move(mesh));
    }
}

} // namespace

namespace {

// Shared by both entry points: operands[0] is the first operand, see MeshBoolean.hpp.
void run_boolean(const std::vector<BooleanOperand> &operands, std::vector<TriangleMesh> &dst_mesh,
                 const std::string &boolean_opts, const BooleanCancelCB &cancel_cb,
                 const BooleanProgressCB &progress_cb, const BooleanFailedCB &failed_cb)
{
    try {
        size_t mesh_count = 0;
        for (const BooleanOperand &operand : operands)
            mesh_count += operand.size();
        BOOST_LOG_TRIVIAL(info) << "Mesh boolean backend: OpenMeshCraft, op='" << boolean_opts << "' "
                                << KV(log_key::operand_count, operands.size()) << KV(log_key::mesh_count, mesh_count);
        const auto start = std::chrono::steady_clock::now();
        if (progress_cb)
            progress_cb(5.0f);
        if (cancel_cb && cancel_cb())
            return;

        if (boolean_opts != "UNION" && boolean_opts != "INTERSECTION" && boolean_opts != "A_NOT_B")
            throw Slic3r::RuntimeError("Unsupported OpenMeshCraft boolean operation: " + boolean_opts);
        if (operands.empty())
            throw Slic3r::RuntimeError("OpenMeshCraft boolean without operands");

        // OpenMeshCraft decides inside/outside by ray casting, which is only defined for
        // closed, edge-manifold meshes. On anything else it returns arbitrary results or
        // reaches OMC_EXIT, which calls std::exit(). MCUT rejected such input up front.
        for (const BooleanOperand &operand : operands)
            for (const TriangleMesh *mesh : operand) {
                const MeshDiagnosticStats stats = its_edge_diagnostics(mesh->its);
                if (stats.open_edges > 0 || stats.non_manifold_edges > 0) {
                    BOOST_LOG_TRIVIAL(error) << KV(log_key::open_edges, stats.open_edges)
                                             << KV(log_key::non_manifold_edges, stats.non_manifold_edges)
                                             << "OpenMeshCraft boolean refused, input is not a closed manifold mesh, op '"
                                             << boolean_opts << "'";
                    if (failed_cb)
                        failed_cb();
                    return;
                }
            }

        std::vector<OMCInput>                  inputs;
        std::vector<std::vector<BambuOMCMesh>> views(operands.size());
        std::vector<BambuOMCOperand>           omc_operands;
        inputs.reserve(mesh_count); // views point into inputs: no reallocation
        for (size_t i = 0; i < operands.size(); ++i) {
            for (const TriangleMesh *mesh : operands[i]) {
                inputs.push_back(triangle_mesh_to_openmeshcraft(*mesh));
                views[i].push_back(inputs.back().view());
            }
            omc_operands.push_back({views[i].data(), views[i].size()});
        }
        OMCOutput result;

        // Degenerate inputs (e.g. overlapping/coplanar cylinders) can drive
        // OpenMeshCraft into edge cases. Failure modes:
        //  * C++ exceptions (RuntimeError / assert). These must be caught
        //    INSIDE the SEH lambda: throwing across MSVC __try/__except is
        //    unsafe and previously aborted the whole process (exit code 1)
        //    instead of reaching the outer catch.
        //  * ACCESS_VIOLATION / SIGFPE on this calling thread -> SEH catch.
        // Keep OpenMeshCraft on its normal TBB parallelism for speed.
        bool        boolean_ok = false;
        bool        hw_fault   = false;
        std::string cpp_error;
        try_catch_signal({SIGSEGV, SIGFPE},
            [&]() {
                try {
                    BambuOMCOperation operation = BAMBU_OMC_SUBTRACTION;
                    if (boolean_opts == "UNION")
                        operation = BAMBU_OMC_UNION;
                    else if (boolean_opts == "INTERSECTION")
                        operation = BAMBU_OMC_INTERSECTION;

                    std::array<char, 1024> error_message{};
                    boolean_ok = bambu_omc_boolean_operands(
                        omc_operands.data(),
                        omc_operands.size(),
                        operation,
                        receive_openmeshcraft_result,
                        &result,
                        error_message.data(),
                        error_message.size());
                    if (!boolean_ok)
                        cpp_error = error_message.data();
                } catch (const std::exception &e) {
                    cpp_error = e.what();
                }
            },
            [&]() {
                hw_fault = true;
            });

        if (hw_fault) {
            if (failed_cb)
                failed_cb();
            BOOST_LOG_TRIVIAL(error) << "OpenMeshCraft mesh boolean crashed (hardware fault) for operation '"
                                     << boolean_opts
                                     << "'; input meshes are likely degenerate (overlapping/coplanar).";
            return;
        }

        if (!cpp_error.empty()) {
            if (failed_cb)
                failed_cb();
            BOOST_LOG_TRIVIAL(error) << "OpenMeshCraft mesh boolean failed: " << cpp_error;
            return;
        }

        if (progress_cb)
            progress_cb(70.0f);
        if (cancel_cb && cancel_cb())
            return;

        if (!boolean_ok) {
            if (failed_cb)
                failed_cb();
            BOOST_LOG_TRIVIAL(error) << "OpenMeshCraft mesh boolean did not complete for operation '" << boolean_opts << "'.";
            return;
        }

        append_oriented_result(openmeshcraft_to_triangle_mesh(result), dst_mesh);
        BOOST_LOG_TRIVIAL(info) << "OpenMeshCraft mesh boolean finished, op='" << boolean_opts << "' "
                                << KV(log_key::elapsed_ms, std::chrono::duration_cast<std::chrono::milliseconds>(
                                                               std::chrono::steady_clock::now() - start).count());
        if (progress_cb)
            progress_cb(100.0f);
    } catch (const std::exception &e) {
        if (failed_cb)
            failed_cb();
        BOOST_LOG_TRIVIAL(error) << "OpenMeshCraft mesh boolean failed: " << e.what();
    }
}

} // namespace

void make_boolean(const TriangleMesh &src_mesh, const TriangleMesh &cut_mesh, std::vector<TriangleMesh> &dst_mesh, const std::string &boolean_opts, const BooleanCancelCB& cancel_cb, const BooleanProgressCB& progress_cb, const BooleanFailedCB& failed_cb)
{
    if (boolean_opts == "B_NOT_A")
        run_boolean({{&cut_mesh}, {&src_mesh}}, dst_mesh, "A_NOT_B", cancel_cb, progress_cb, failed_cb);
    else
        run_boolean({{&src_mesh}, {&cut_mesh}}, dst_mesh, boolean_opts, cancel_cb, progress_cb, failed_cb);
}

void make_boolean(const std::vector<BooleanOperand> &operands, std::vector<TriangleMesh> &dst_mesh, const std::string &boolean_opts, const BooleanCancelCB& cancel_cb, const BooleanProgressCB& progress_cb, const BooleanFailedCB& failed_cb)
{
    run_boolean(operands, dst_mesh, boolean_opts, cancel_cb, progress_cb, failed_cb);
}

} // namespace openmeshcraft
} // namespace MeshBoolean
} // namespace Slic3r
