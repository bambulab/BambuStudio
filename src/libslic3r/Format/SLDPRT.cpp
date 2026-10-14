#include "SLDPRT.hpp"
#include "SLDPRTReader.hpp"
#include "../Model.hpp"
#include "../TriangleMesh.hpp"

#include <boost/filesystem/path.hpp>
#include <boost/nowide/fstream.hpp>
#include <stdexcept>

namespace Slic3r {
bool load_sldprt(const char *path, Model *model, bool &cancelled, ImportstlProgressFn progress)
{
    cancelled = false;
    boost::nowide::ifstream file(path, std::ios::binary);
    SolidWorks::Mesh source;
    try {
        source = SolidWorks::read(file, [&progress](unsigned percent) {
            bool cancel = false;
            std::string model_id, country_code, region, name, id;
            if (progress)
                progress(int(percent), 100, cancel, model_id, country_code, region, name, id);
            return !cancel;
        });
    } catch (const SolidWorks::Cancelled &) {
        cancelled = true;
        return false;
    } catch (const std::runtime_error &error) {
        throw Slic3r::RuntimeError(error.what());
    }

    indexed_triangle_set its;
    its.vertices.reserve(source.vertices.size());
    its.indices.reserve(source.indices.size() / 3);
    for (const auto &v : source.vertices)
        its.vertices.emplace_back(v[0], v[1], v[2]);
    for (size_t i = 0; i < source.indices.size(); i += 3)
        its.indices.emplace_back(source.indices[i], source.indices[i + 1], source.indices[i + 2]);
    // Face strips duplicate boundary vertices. Weld exact shared positions
    // before topology diagnostics and slicing, without rounding CAD geometry.
    its_merge_vertices(its);
    model->add_object(boost::filesystem::path(path).filename().string().c_str(), path, TriangleMesh(std::move(its)));
    return true;
}
} // namespace Slic3r
