#include <catch2/catch.hpp>
#include <cstdlib>
#include "libslic3r/Model.hpp"
#include "libslic3r/Format/SLDPRT.hpp"
#include "../solidworks/fixture.hpp"
#include <boost/filesystem.hpp>
#include <boost/nowide/filesystem.hpp>
#include <boost/nowide/fstream.hpp>

using namespace Slic3r;
namespace {
struct TemporarySolidWorksPart {
    boost::filesystem::path path;
    TemporarySolidWorksPart() {
        // The application initializes Boost's UTF-8 filesystem conversion too.
        boost::nowide::nowide_filesystem();
        path = boost::filesystem::temp_directory_path() /
            boost::filesystem::unique_path("solidworks-%%%%-%%%%-\xc3\xa9.SlDpRt");
        const auto bytes = SldprtFixture::document();
        boost::nowide::ofstream f(path.string(), std::ios::binary);
        f.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    }
    ~TemporarySolidWorksPart() { boost::system::error_code ec; boost::filesystem::remove(path, ec); }
};
}
TEST_CASE("SolidWorks parts use the shared model import dispatcher", "[sldprt]") {
    TemporarySolidWorksPart file;
    auto model = Model::read_from_file(file.path.string());
    REQUIRE(model.objects.size() == 1);
    REQUIRE(model.objects[0]->input_file == file.path.string());
    REQUIRE(model.objects[0]->volumes.size() == 1);
    const auto &mesh = model.objects[0]->volumes[0]->mesh();
    REQUIRE(mesh.facets_count() == 2);
    REQUIRE(mesh.size().x() == Approx(20));
    REQUIRE(mesh.size().y() == Approx(10));
}
TEST_CASE("Cancelled SolidWorks imports do not add model objects", "[sldprt]") {
    TemporarySolidWorksPart file;
    Model model;
    bool cancelled = false;
    REQUIRE_FALSE(load_sldprt(file.path.string().c_str(), &model, cancelled,
        [](int, int, bool &cancel, std::string &, std::string &, std::string &, std::string &, std::string &) { cancel = true; }));
    REQUIRE(cancelled);
    REQUIRE(model.objects.empty());
}
TEST_CASE("A native SolidWorks sample reaches the slicing model", "[sldprt]") {
    auto model = Model::read_from_file(std::string(TEST_DATA_DIR) + "/test_sldprt/nist_ftc_06_asme1_rd_sw1802.SLDPRT");
    REQUIRE(model.objects.size() == 1);
    REQUIRE(model.objects[0]->volumes[0]->mesh().facets_count() == 7098);
    REQUIRE(model.objects[0]->volumes[0]->mesh().size().x() == Approx(304.8).margin(.001));
}

// Opt-in smoke test for a caller-owned solid part. The input stays outside git.
TEST_CASE("A local SolidWorks solid reaches the slicer with closed surfaces", "[.local-sldprt]") {
    const char *path = std::getenv("BAMBU_SLDPRT_SAMPLE");
    REQUIRE(path != nullptr);
    auto model = Model::read_from_file(path);
    REQUIRE(model.objects.size() == 1);
    const auto &mesh = model.objects[0]->volumes[0]->mesh();
    INFO("triangles=" << mesh.facets_count() << " size_mm=" << mesh.size().transpose()
         << " volume_mm3=" << mesh.stats().volume);
    REQUIRE(mesh.facets_count() > 0);
    REQUIRE(mesh.stats().volume > 0);
    REQUIRE(mesh.stats().open_edges == 0);
    REQUIRE(mesh.stats().non_manifold_edges == 0);
    REQUIRE_FALSE(mesh.stats().has_reversed_faces);
    const auto box = mesh.bounding_box();
    std::vector<double> levels;
    for (unsigned i = 1; i < 10; ++i)
        levels.push_back(box.min.z() + (box.max.z() - box.min.z()) * (double(i) / 10));
    const auto slices = mesh.slice(levels);
    REQUIRE(slices.size() == levels.size());
    for (const auto &slice : slices)
        REQUIRE_FALSE(slice.empty());
}
