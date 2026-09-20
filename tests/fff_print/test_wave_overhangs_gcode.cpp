#include <catch2/catch.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Config.hpp"
#include "libslic3r/Print.hpp"

#include "test_data.hpp"

#include <boost/filesystem.hpp>

#include <fstream>
#include <iterator>
#include <string>

using namespace Slic3r;
using namespace Slic3r::Test;

namespace {

// Slice the overhang fixture, applying the given wave-overhang settings on top of an
// otherwise default profile, and hand back the resulting G-code.
//
// This deliberately does not use Slic3r::Test::init_print. That helper calls
// arrange_objects() with an InfiniteBed and still ends up throwing "Objects could not fit
// on the bed" — its own SCENARIO in test_data.cpp fails the same way against current
// master. Placing the mesh directly sidesteps the arranger entirely.
std::string slice_overhang(std::initializer_list<Slic3r::ConfigBase::SetDeserializeItem> items)
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

    // Put the mesh in the middle of the default 200x200 bed.
    ModelObject *object = model.add_object();
    object->name = "overhang.stl";
    // Only the single-argument mesh() is actually defined in test_data.cpp; the
    // translate/scale overloads are declared in the header but have no implementation,
    // so calling them fails at link time. Translate by hand instead.
    TriangleMesh tm = Slic3r::Test::mesh(TestMesh::overhang);
    tm.translate(100.f, 100.f, 0.f);
    object->add_volume(std::move(tm));
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

bool contains(const std::string &haystack, const char *needle)
{
    return haystack.find(needle) != std::string::npos;
}

} // namespace

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
    const std::string gcode = slice_overhang({ { "wave_overhangs", true },
                                               { "wave_overhang_min_wave_time", 5 } });

    REQUIRE_FALSE(gcode.empty());
    CHECK(contains(gcode, "wave-overhang min_wave_time dwell"));
}

TEST_CASE("WaveOverhangs G-code: bridge suppression removes bridge fill", "[WaveOverhangs][GCode]")
{
    // With wave_overhangs_instead_of_bridges the region should contain no bridge
    // classifications at all; everything left over becomes solid infill.
    const std::string gcode = slice_overhang({ { "wave_overhangs", true },
                                               { "wave_overhangs_instead_of_bridges", true },
                                               { "gcode_comments", true } });

    REQUIRE_FALSE(gcode.empty());
    CHECK_FALSE(contains(gcode, "; bridge"));
}
