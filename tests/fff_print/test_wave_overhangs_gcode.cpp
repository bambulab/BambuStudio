#include <catch2/catch.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Config.hpp"
#include "libslic3r/Print.hpp"

#include "test_data.hpp"

#include <boost/filesystem.hpp>

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

using namespace Slic3r;
using namespace Slic3r::Test;

namespace {

// Slice `mesh`, applying the given settings on top of an otherwise default profile, and hand
// back the resulting G-code.
//
// This deliberately does not use Slic3r::Test::init_print. That helper calls
// arrange_objects() with an InfiniteBed and still ends up throwing "Objects could not fit
// on the bed" — its own SCENARIO in test_data.cpp fails the same way against current
// master. Placing the mesh directly sidesteps the arranger entirely.
std::string slice_mesh(TriangleMesh mesh, std::initializer_list<Slic3r::ConfigBase::SetDeserializeItem> items)
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        { "wall_loops",            2 },
        { "detect_overhang_wall",  true },
        { "skirts",                0 },
    });
    if (items.size() > 0)
        config.set_deserialize_strict(items);

    Slic3r::Model model;
    Slic3r::Print print;

    // Put the mesh in the middle of the default 200x200 bed. Test meshes come in arbitrary
    // coordinates — the overhang fixture sits near X = 1360 — so centre on the bounding box
    // rather than applying a fixed offset.
    ModelObject *object = model.add_object();
    object->name = "fixture.stl";
    const BoundingBoxf3 bb = mesh.bounding_box();
    mesh.translate(float(100. - bb.center().x()), float(100. - bb.center().y()), 0.f);
    object->add_volume(std::move(mesh));
    object->add_instance();
    object->ensure_on_bed();
    print.auto_assign_extruders(object);

    print.apply(model, config);
    print.validate();
    print.set_status_silent();
    print.process();

    // Not Slic3r::Test::gcode() either: that exports to boost::filesystem::unique_path(),
    // a bare filename whose parent path is empty, and the exporter then fails trying to
    // create that empty directory. Export to a real absolute path instead.
    const boost::filesystem::path out =
        boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("wave_%%%%%%.gcode");
    print.export_gcode(out.string(), nullptr, nullptr);

    std::ifstream in(out.string());
    std::string   gcode((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    boost::system::error_code ec;
    boost::filesystem::remove(out, ec);
    return gcode;
}

std::string slice_overhang(std::initializer_list<Slic3r::ConfigBase::SetDeserializeItem> items)
{
    // Only the single-argument mesh() is actually defined in test_data.cpp; the
    // translate/scale overloads are declared in the header but have no implementation.
    return slice_mesh(Slic3r::Test::mesh(TestMesh::overhang), items);
}

// A 12 x 12 x 20 mm post with a 32 x 12 x 4 mm arm at Z 14, anchored on one side only: the
// shape both wave overhangs and supports exist for. The arm overlaps the post by 4 mm so the
// two volumes slice as one solid rather than meeting at a zero-thickness face.
//
// Tree support generates no support at all for the overhang fixture above, which would make a
// tree-support assertion on it vacuous; a 28 mm cantilever needs support under either generator.
TriangleMesh cantilever()
{
    TriangleMesh post = Slic3r::make_cube(12., 12., 20.);
    TriangleMesh arm  = Slic3r::make_cube(32., 12., 4.);
    arm.translate(8.f, 0.f, 14.f);
    post.merge(arm);
    return post;
}

bool contains(const std::string &haystack, const char *needle)
{
    return haystack.find(needle) != std::string::npos;
}

// Filament extruded as support, in mm of filament: every positive E on a move while the
// current feature tag is one of the support features.
double support_filament(const std::string &gcode)
{
    double total      = 0.;
    bool   in_support = false;
    size_t pos        = 0;
    while (pos < gcode.size()) {
        size_t eol = gcode.find('\n', pos);
        if (eol == std::string::npos)
            eol = gcode.size();
        const std::string line = gcode.substr(pos, eol - pos);
        pos = eol + 1;
        if (line.rfind("; FEATURE: ", 0) == 0) {
            in_support = line.find("Support", 11) != std::string::npos;
            continue;
        }
        if (! in_support || line.rfind("G1", 0) != 0)
            continue;
        const size_t e = line.find(" E");
        if (e == std::string::npos || line.find(';') < e)
            continue;
        const double v = std::atof(line.c_str() + e + 2);
        if (v > 0.)
            total += v;
    }
    return total;
}

} // namespace

TEST_CASE("Exported G-code serializes enum-list options by name", "[WaveOverhangs][GCode]")
{
    // append_full_config writes every setting at the end of the file. Enum-list options
    // held in static print configs used to carry a null key map, so this crashed; and a
    // null guard alone would stop the crash while writing an empty value. Check for the
    // actual name, which only a correctly attached key map can produce.
    const std::string gcode = slice_overhang({});

    REQUIRE_FALSE(gcode.empty());
    CHECK(contains(gcode, "; cooling_slowdown_logic = uniform_cooling"));
}

TEST_CASE("WaveOverhangs G-code: supports are not generated under wave-covered areas", "[WaveOverhangs][GCode]")
{
    // Both generators are ported separately, so exercise both.
    for (const char *support_type : { "normal(auto)", "tree(auto)" }) {
        INFO("support_type = " << support_type);

        const double without_waves = support_filament(slice_mesh(cantilever(), {
            { "enable_support", true }, { "support_type", support_type },
            { "wave_overhangs", false } }));
        const double with_waves = support_filament(slice_mesh(cantilever(), {
            { "enable_support", true }, { "support_type", support_type },
            { "wave_overhangs", true }, { "support_remaining_areas_after_wave_overhangs", true } }));
        // Suppression is off by default, so this is also what enabling waves alone does.
        const double waves_but_opted_out = support_filament(slice_mesh(cantilever(), {
            { "enable_support", true }, { "support_type", support_type },
            { "wave_overhangs", true } }));

        // Control: the fixture needs support at all, or nothing below means anything.
        REQUIRE(without_waves > 0.);
        // Waves cover part of the overhang, so strictly less support is generated.
        CHECK(with_waves < without_waves);
        // With the option off, waves are generated but support is untouched. Slicing is not
        // bit-for-bit reproducible run to run, so allow a small tolerance rather than equality.
        CHECK(waves_but_opted_out == Approx(without_waves).epsilon(0.02));
    }
}

TEST_CASE("WaveOverhangs G-code: disabled leaves no trace", "[WaveOverhangs][GCode]")
{
    // The default path through the slicer must be untouched by this feature.
    const std::string gcode = slice_overhang({ { "wave_overhangs", false },
                                               { "wave_overhang_debug_gcode", true } });

    REQUIRE_FALSE(gcode.empty());
    CHECK_FALSE(contains(gcode, "WAVE_OVERHANG"));
    CHECK_FALSE(contains(gcode, "wave-overhang"));
}

TEST_CASE("WaveOverhangs G-code: enabling it produces wave extrusions", "[WaveOverhangs][GCode]")
{
    const std::string gcode = slice_overhang({ { "wave_overhangs", true },
                                               { "wave_overhang_debug_gcode", true } });

    REQUIRE_FALSE(gcode.empty());
    // The debug markers bracket each wave region, so their presence means the whole
    // pipeline ran: generator, perimeter hook, extrusion tagging and the G-code stage.
    CHECK(contains(gcode, "; WAVE_OVERHANG_START"));
    CHECK(contains(gcode, "; WAVE_OVERHANG_END"));
}

TEST_CASE("WaveOverhangs G-code: the print speed override is applied", "[WaveOverhangs][GCode]")
{
    // 7 mm/s is deliberately unlike any default speed, so F420 in the output can only
    // have come from the wave override.
    const std::string gcode = slice_overhang({ { "wave_overhangs", true },
                                               { "wave_overhang_print_speed", 7 } });

    REQUIRE_FALSE(gcode.empty());
    CHECK(contains(gcode, "F420"));
}

TEST_CASE("WaveOverhangs G-code: the fan marker is consumed, not emitted", "[WaveOverhangs][GCode]")
{
    // The cooling stage rewrites ;_WAVE_OVERHANG_FAN_START into real fan commands. If the
    // raw marker survives into the output, GCodeEditor never saw it and the fan override
    // silently did nothing.
    const std::string gcode = slice_overhang({ { "wave_overhangs", true },
                                               { "wave_overhang_fan_speed", 100 } });

    REQUIRE_FALSE(gcode.empty());
    CHECK_FALSE(contains(gcode, ";_WAVE_OVERHANG_FAN_START"));
    CHECK_FALSE(contains(gcode, ";_WAVE_OVERHANG_FAN_END"));
}

TEST_CASE("WaveOverhangs G-code: min_wave_time inserts a dwell", "[WaveOverhangs][GCode]")
{
    // The dwell pads each wave line up to min_wave_time, so it only fires when a line takes
    // less than that. At the default 2 mm/s even a 100 mm line is under a minute, so 60 s
    // is guaranteed to trigger; a small value like 5 s is already met by ordinary lines.
    const std::string gcode = slice_overhang({ { "wave_overhangs", true },
                                               { "wave_overhang_min_wave_time", 60 } });

    REQUIRE_FALSE(gcode.empty());
    CHECK(contains(gcode, "wave-overhang min_wave_time dwell"));
}

TEST_CASE("WaveOverhangs G-code: bridge suppression removes bridge fill", "[WaveOverhangs][GCode]")
{
    // With wave_overhangs_instead_of_bridges the region should contain no bridge fill;
    // everything left over becomes solid infill. Match the toolpath feature tag rather than
    // a bare "bridge": the trailing config block is full of settings such as bridge_speed
    // and bridge_flow, which would match without meaning anything.
    const std::string with_bridges = slice_overhang({ { "wave_overhangs", true } });
    const std::string suppressed   = slice_overhang({ { "wave_overhangs", true },
                                                      { "wave_overhangs_instead_of_bridges", true } });

    REQUIRE_FALSE(suppressed.empty());
    // Control: the fixture has to produce bridge fill in the first place, otherwise the
    // check below would pass without testing anything.
    REQUIRE(contains(with_bridges, "; FEATURE: Bridge"));
    CHECK_FALSE(contains(suppressed, "; FEATURE: Bridge"));
}
