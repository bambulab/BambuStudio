#include <catch2/catch.hpp>

#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Thread.hpp"
#include "libslic3r/Utils.hpp"

#include <algorithm>
#include <boost/filesystem.hpp>
#include <fstream>
#include <numeric>
#include <sstream>

using namespace Slic3r;

TEST_CASE("Center of gravity preview is an opt-in application preference", "[center_of_mass][preferences]")
{
    AppConfig config;
    const std::string key = "show_center_of_gravity_in_preview";
    const std::string objects_key = "show_objects_in_preview";

    REQUIRE(config.has(key));
    REQUIRE_FALSE(config.get_bool(key));
    REQUIRE(config.has(objects_key));
    REQUIRE_FALSE(config.get_bool(objects_key));

    SECTION("Existing profiles without the preference remain opted out") {
        config.erase("app", key);
        REQUIRE_FALSE(config.get_bool(key));
        config.set_defaults();
        REQUIRE(config.has(key));
        REQUIRE_FALSE(config.get_bool(key));
    }

    SECTION("Applying defaults preserves explicit opt-in") {
        config.set_bool(key, true);
        config.set_defaults();
        REQUIRE(config.get_bool(key));
    }

    SECTION("Applying defaults preserves explicit opt-out") {
        config.set_bool(key, true);
        config.set_bool(key, false);
        config.set_defaults();
        REQUIRE_FALSE(config.get_bool(key));
    }

    SECTION("Resetting application preferences restores opt-out") {
        config.set_bool(key, true);
        config.reset();
        REQUIRE_FALSE(config.get_bool(key));
    }

    SECTION("Object coloring and center of gravity remain independent") {
        const bool objects_enabled = GENERATE(false, true);
        const bool cog_enabled = GENERATE(false, true);
        config.set_bool(objects_key, objects_enabled);
        config.set_bool(key, cog_enabled);
        config.set_defaults();
        REQUIRE(config.get_bool(objects_key) == objects_enabled);
        REQUIRE(config.get_bool(key) == cog_enabled);
        config.reset();
        REQUIRE_FALSE(config.get_bool(objects_key));
        REQUIRE_FALSE(config.get_bool(key));
    }
}

namespace {

struct TemporaryApplicationDataDirectory
{
    std::string previous_data_dir{ data_dir() };
    boost::filesystem::path path{ boost::filesystem::temp_directory_path() /
        boost::filesystem::unique_path("center-of-gravity-preferences-%%%%-%%%%") };

    TemporaryApplicationDataDirectory()
    {
        REQUIRE(boost::filesystem::create_directory(path));
        set_data_dir(path.string());
    }

    ~TemporaryApplicationDataDirectory()
    {
        set_data_dir(previous_data_dir);
        boost::system::error_code error;
        boost::filesystem::remove_all(path, error);
    }
};

GCodeProcessorResult::CenterOfMassResult process_center_of_mass(
    const std::string& gcode,
    const std::vector<double>& filament_diameters,
    const std::vector<double>& filament_densities,
    double x_offset = 0.0,
    double y_offset = 0.0)
{
    FullPrintConfig config = FullPrintConfig::defaults();
    config.filament_diameter.values = filament_diameters;
    config.filament_density.values = filament_densities;
    // Each material maps to its own physical tool in these synthetic fixtures.
    // Keep nozzle arrays consistent with that map, as a real printer profile does.
    config.nozzle_diameter.values.resize(filament_diameters.size(), config.nozzle_diameter.values.front());
    config.nozzle_volume.values.resize(filament_diameters.size(), config.nozzle_volume.values.front());
    config.filament_map.values.resize(filament_diameters.size());
    std::iota(config.filament_map.values.begin(), config.filament_map.values.end(), 1);

    GCodeProcessor processor;
    processor.apply_config(config);
    processor.set_xy_offset(x_offset, y_offset);
    processor.initialize("center-of-mass-test.gcode");
    processor.process_buffer(gcode);
    return processor.get_result().get_center_of_mass();
}

GCodeProcessorResult::CenterOfMassResult process_center_of_mass(
    const std::string& gcode,
    double filament_diameter = 2.0,
    double filament_density = 2.0,
    double x_offset = 0.0,
    double y_offset = 0.0)
{
    return process_center_of_mass(gcode,
                                  std::vector<double>{ filament_diameter },
                                  std::vector<double>{ filament_density },
                                  x_offset,
                                  y_offset);
}

struct TemporaryGCodeFile
{
    boost::filesystem::path path{ boost::filesystem::unique_path("center-of-mass-%%%%-%%%%.gcode") };

    ~TemporaryGCodeFile() { boost::filesystem::remove(path); }

    void write(const std::string& contents) const
    {
        std::ofstream stream(path.string(), std::ios::binary);
        REQUIRE(stream.good());
        stream << contents;
        REQUIRE(stream.good());
    }
};

DynamicPrintConfig material_config(double diameter, double density)
{
    DynamicPrintConfig config;
    config.apply(FullPrintConfig::defaults());
    config.opt<ConfigOptionFloats>("filament_diameter")->values = { diameter };
    config.opt<ConfigOptionFloats>("filament_density")->values = { density };
    return config;
}

std::string with_embedded_config(const DynamicPrintConfig& config, const std::string& gcode)
{
    std::ostringstream stream;
    stream << "; BambuStudio\n" << gcode << "; CONFIG_BLOCK_START\n";
    for (const std::string& key : config.keys()) {
        const ConfigOption* option = config.option(key);
        if (option != nullptr)
            stream << "; " << key << " = " << option->serialize() << '\n';
    }
    stream << "; CONFIG_BLOCK_END\n";
    return stream.str();
}

constexpr const char* model_prefix =
    "G90\n"
    "M83\n"
    "G1 X0 Y0 Z1\n"
    "; FEATURE: Inner wall\n"
    "; LAYER_HEIGHT: 0.2\n";

} // namespace

TEST_CASE("Center of gravity preview preference survives saving and reloading", "[center_of_mass][preferences]")
{
    const bool enabled = GENERATE(false, true);
    const bool objects_enabled = GENERATE(false, true);
    TemporaryApplicationDataDirectory temporary_data;
    save_main_thread_id();

    AppConfig config;
    config.set_bool("show_center_of_gravity_in_preview", enabled);
    config.set_bool("show_objects_in_preview", objects_enabled);
    config.save();

    AppConfig reloaded;
    REQUIRE(reloaded.load().empty());
    REQUIRE(reloaded.get_bool("show_center_of_gravity_in_preview") == enabled);
    REQUIRE(reloaded.get_bool("show_objects_in_preview") == objects_enabled);
}

TEST_CASE("Center of mass weights analytic line segments by deposited mass", "[center_of_mass]")
{
    const auto result = process_center_of_mass(std::string(model_prefix) +
        "G1 X10 Y0 E1\n"
        "G1 X20 Y0 E3\n");
    const auto model = result.finished_model();

    REQUIRE(model.valid());
    CHECK(model.volume_mm3 == Approx(4.0 * PI));
    CHECK(model.mass_g == Approx(8.0 * PI / 1000.0));
    CHECK(model.center_of_mass().x() == Approx(12.5));
    CHECK(model.center_of_mass().y() == Approx(0.0));
    CHECK(model.center_of_mass().z() == Approx(0.9));
}

TEST_CASE("OBJECT_ID partitions center of mass by printable object", "[center_of_mass]")
{
    const auto result = process_center_of_mass(std::string(model_prefix) +
        "; OBJECT_ID: 101\n"
        "G1 X10 E1\n"
        "; OBJECT_ID: 202\n"
        "G1 X30 E3\n");

    REQUIRE(result.by_object.size() == 2);
    const auto first = result.by_object.at(101).finished_model();
    const auto second = result.by_object.at(202).finished_model();
    const auto combined = result.finished_model();

    REQUIRE(first.valid());
    REQUIRE(second.valid());
    CHECK(first.volume_mm3 == Approx(PI));
    CHECK(first.center_of_mass().x() == Approx(5.0));
    CHECK(second.volume_mm3 == Approx(3.0 * PI));
    CHECK(second.center_of_mass().x() == Approx(20.0));
    CHECK(combined.volume_mm3 == Approx(4.0 * PI));
    CHECK(combined.center_of_mass().x() == Approx(16.25));
}

TEST_CASE("Object start and stop tags leave unlabeled extrusion in the combined result only", "[center_of_mass]")
{
    const auto result = process_center_of_mass(std::string(model_prefix) +
        "; start printing object, unique label id: 7\n"
        "G1 X10 E1\n"
        "; stop printing object, unique label id: 7\n"
        "G1 X20 E1\n");

    REQUIRE(result.by_object.size() == 1);
    CHECK(result.by_object.at(7).finished_model().volume_mm3 == Approx(PI));
    CHECK(result.finished_model().volume_mm3 == Approx(2.0 * PI));
}

TEST_CASE("Finished model excludes non-model extrusion roles", "[center_of_mass]")
{
    const auto result = process_center_of_mass(std::string(model_prefix) +
        "G1 X10 E1\n"
        "; FEATURE: Support\n"
        "G1 X20 E1\n"
        "; FEATURE: Skirt\n"
        "G1 X30 E1\n"
        "; FEATURE: Brim\n"
        "G1 X40 E1\n"
        "; FEATURE: Prime tower\n"
        "G1 X50 E1\n"
        "; FEATURE: Custom\n"
        "G1 X60 E1\n"
        "; FEATURE: Flush\n"
        "G1 X70 E1\n"
        "; FEATURE: Multiple\n"
        "G1 X80 E1\n");

    CHECK(result.finished_model().volume_mm3 == Approx(PI));
    CHECK(result.all_spatial_extrusions().volume_mm3 == Approx(8.0 * PI));
    CHECK(result.contains_unknown_roles);
}

TEST_CASE("Center of mass respects extrusion positioning, G92, and retractions", "[center_of_mass]")
{
    const auto result = process_center_of_mass(
        "G90\n"
        "M82\n"
        "G1 X0 Y0 Z1\n"
        "; FEATURE: Inner wall\n"
        "; LAYER_HEIGHT: 0.2\n"
        "G1 X10 E2\n"
        "G1 X20 E1\n"
        "G92 E0\n"
        "G1 X30 E3\n"
        "M83\n"
        "G1 X40 E1\n"
        "G1 X50 E-0.5\n");

    const auto model = result.finished_model();
    CHECK(model.volume_mm3 == Approx(5.0 * PI));
    CHECK(model.center_of_mass().x() == Approx(19.666667).margin(0.001));
}

TEST_CASE("M221 is per-tool, survives tool switches, and applies once", "[center_of_mass]")
{
    const auto result = process_center_of_mass(std::string(model_prefix) +
        "T0\n"
        "M221 S50\n"
        "G1 X10 E2\n"
        "M221 S80 T1\n"
        "T1\n"
        "G1 X20 E1\n"
        "T0\n"
        "G1 X30 E2\n",
        { 2.0, 2.0 }, { 2.0, 2.0 });

    CHECK(result.finished_model().volume_mm3 == Approx(2.8 * PI));
    CHECK_FALSE(result.finished_model().unsupported_flow_override);
}

TEST_CASE("Malformed M221 warns without replacing the last valid tool state", "[center_of_mass]")
{
    const auto model = process_center_of_mass(std::string(model_prefix) +
        "M221 S50\n"
        "G1 X10 E2\n"
        "M221 Sinf\n"
        "G1 X20 E2\n").finished_model();

    CHECK(model.volume_mm3 == Approx(2.0 * PI));
    CHECK(model.unsupported_flow_override);
}

TEST_CASE("Bambu bare M221 state-stack commands are not flow overrides", "[center_of_mass]")
{
    const auto model = process_center_of_mass(std::string(model_prefix) +
        "M221 S\n"
        "G1 X10 E1\n"
        "M221 R\n"
        "G1 X20 E1\n").finished_model();

    CHECK(model.volume_mm3 == Approx(2.0 * PI));
    CHECK_FALSE(model.unsupported_flow_override);
}

TEST_CASE("Invalid tool-specific M221 remains visible without guessing a target", "[center_of_mass]")
{
    const auto model = process_center_of_mass(std::string(model_prefix) +
        "M221 S80 T999\n"
        "G1 X10 E1\n").finished_model();

    CHECK(model.volume_mm3 == Approx(PI));
    CHECK(model.unsupported_flow_override);
}

TEST_CASE("M221 changes with outstanding retraction debt fail safe", "[center_of_mass]")
{
    const auto model = process_center_of_mass(std::string(model_prefix) +
        "M221 S50\n"
        "G1 E-1\n"
        "M221 S100\n"
        "G1 X10 E3\n").finished_model();

    CHECK(model.volume_mm3 == Approx(PI));
    CHECK(model.center_of_mass().x() == Approx(20.0 / 3.0));
    CHECK(model.unsupported_flow_override);
}

TEST_CASE("CW and CCW arc interpolation produces symmetric centroids", "[center_of_mass]")
{
    const auto ccw = process_center_of_mass(std::string(model_prefix) +
        "G1 X10 Y0\n"
        "G3 X0 Y10 I-10 J0 E1\n").finished_model();
    const auto cw = process_center_of_mass(std::string(model_prefix) +
        "G1 X10 Y0\n"
        "G2 X0 Y-10 I-10 J0 E1\n").finished_model();

    REQUIRE(ccw.valid());
    REQUIRE(cw.valid());
    CHECK(ccw.center_of_mass().x() == Approx(cw.center_of_mass().x()).margin(0.05));
    CHECK(ccw.center_of_mass().y() == Approx(-cw.center_of_mass().y()).margin(0.05));
    CHECK(ccw.center_of_mass().x() == Approx(20.0 / PI).margin(0.05));
    CHECK(ccw.center_of_mass().y() == Approx(20.0 / PI).margin(0.05));
}

TEST_CASE("Full and helical arcs distribute mass over interpolation subsegments", "[center_of_mass]")
{
    const auto full = process_center_of_mass(std::string(model_prefix) +
        "G1 X10 Y0\n"
        "G3 X10 Y0 I-10 J0 P1 E1\n").finished_model();
    const auto helical = process_center_of_mass(std::string(model_prefix) +
        "G1 X10 Y0\n"
        "G3 X10 Y0 Z3 I-10 J0 P1 E1\n").finished_model();

    REQUIRE(full.valid());
    REQUIRE(helical.valid());
    CHECK(full.center_of_mass().x() == Approx(0.0).margin(0.05));
    CHECK(full.center_of_mass().y() == Approx(0.0).margin(0.05));
    CHECK(full.volume_mm3 == Approx(PI));
    CHECK(full.mass_g == Approx(2.0 * PI / 1000.0));
    CHECK(helical.center_of_mass().x() == Approx(0.0).margin(0.05));
    CHECK(helical.center_of_mass().y() == Approx(0.0).margin(0.05));
    CHECK(helical.center_of_mass().z() == Approx(1.9).margin(0.05));
    CHECK(helical.volume_mm3 == Approx(PI));
    CHECK(helical.mass_g == Approx(2.0 * PI / 1000.0));
}

TEST_CASE("Straight moves after arcs ignore stale interpolation points", "[center_of_mass]")
{
    const auto model = process_center_of_mass(std::string(model_prefix) +
        "G1 X10 Y0\n"
        "G3 X10 Y0 I-10 J0 P1 E1\n"
        "G1 X20 Y0 E1\n").finished_model();

    REQUIRE(model.valid());
    CHECK(model.volume_mm3 == Approx(2.0 * PI));
    CHECK(model.center_of_mass().x() == Approx(7.5).margin(0.05));
    CHECK(model.center_of_mass().y() == Approx(0.0).margin(0.05));
}

TEST_CASE("M200 supports volumetric enable, disable, and per-tool selection", "[center_of_mass]")
{
    const auto result = process_center_of_mass(std::string(model_prefix) +
        "M200 T0 D1.75\n"
        "M221 S50\n"
        "G1 X10 E4\n"
        "M200 T1 S1\n"
        "T1\n"
        "G1 X20 E3\n"
        "M200 D\n"
        "G1 X30 E1\n",
        { 2.0, 4.0 }, { 1.0, 2.0 });
    const auto model = result.finished_model();

    REQUIRE(model.valid());
    // T0: 4 mm^3 * 50%; T1 volumetric: 3 mm^3; then linear with d=4: 4*pi mm^3.
    CHECK(model.volume_mm3 == Approx(5.0 + 4.0 * PI));
    CHECK(model.mass_g == Approx((2.0 + 6.0 + 8.0 * PI) / 1000.0));
    CHECK_FALSE(model.unsupported_volumetric_extrusion);
}

TEST_CASE("M200 S0 may set diameter while keeping volumetric extrusion disabled", "[center_of_mass]")
{
    const auto model = process_center_of_mass(std::string(model_prefix) +
        "M200 S0 D1.75\n"
        "G1 X10 E1\n").finished_model();

    REQUIRE(model.valid());
    CHECK(model.volume_mm3 == Approx(PI * sqr(0.875)));
    CHECK_FALSE(model.unsupported_volumetric_extrusion);
}

TEST_CASE("Malformed M200 fails safe and exposes a scoped warning", "[center_of_mass]")
{
    const auto result = process_center_of_mass(std::string(model_prefix) +
        "G1 X10 E1\n"
        "; FEATURE: Support\n"
        "M200 Dnan\n"
        "G1 X20 E1\n");

    CHECK(result.finished_model().valid());
    CHECK_FALSE(result.finished_model().unsupported_volumetric_extrusion);
    CHECK(result.all_spatial_extrusions().unsupported_volumetric_extrusion);
    CHECK(result.all_spatial_extrusions().volume_mm3 == Approx(PI));
}

TEST_CASE("Different tool materials produce a mass-weighted centroid", "[center_of_mass]")
{
    const auto model = process_center_of_mass(std::string(model_prefix) +
        "T0\n"
        "G1 X10 E1\n"
        "T1\n"
        "G1 X20 E1\n",
        { 2.0, 4.0 }, { 1.0, 2.0 }).finished_model();

    REQUIRE(model.valid());
    CHECK(model.volume_mm3 == Approx(5.0 * PI));
    CHECK(model.mass_g == Approx(9.0 * PI / 1000.0));
    CHECK(model.center_of_mass().x() == Approx(125.0 / 9.0));
}

TEST_CASE("Retraction recovery deposits only the spatial suffix of lines and arcs", "[center_of_mass]")
{
    const auto line = process_center_of_mass(std::string(model_prefix) +
        "G1 E-2\n"
        "G1 X10 E4\n").finished_model();
    const auto arc = process_center_of_mass(std::string(model_prefix) +
        "G1 X10 Y0\n"
        "G1 E-0.5\n"
        "G3 X0 Y10 I-10 J0 E1\n").finished_model();

    REQUIRE(line.valid());
    CHECK(line.volume_mm3 == Approx(2.0 * PI));
    CHECK(line.center_of_mass().x() == Approx(7.5));
    REQUIRE(arc.valid());
    CHECK(arc.volume_mm3 == Approx(0.5 * PI));
    CHECK(arc.center_of_mass().x() == Approx(3.729).margin(0.08));
    CHECK(arc.center_of_mass().y() == Approx(9.003).margin(0.08));
}

TEST_CASE("Synthetic seam markers do not duplicate retraction debt", "[center_of_mass]")
{
    const std::string gcode =
        "G90\n"
        "M83\n"
        "G1 X0 Y0 Z1\n"
        "; FEATURE: Outer wall\n"
        "; LAYER_HEIGHT: 0.2\n"
        "G1 X10 E1\n"
        "G1 X0 E1\n"
        "G1 E-1\n"
        "G1 X10 E2\n";
    FullPrintConfig config = FullPrintConfig::defaults();
    config.filament_diameter.values = { 2.0 };
    config.filament_density.values = { 2.0 };
    GCodeProcessor processor;
    processor.apply_config(config);
    processor.initialize("center-of-mass-seam-test.gcode");
    processor.process_buffer(gcode);
    const auto& processor_result = processor.get_result();
    const auto model = processor_result.get_center_of_mass().finished_model();

    REQUIRE(model.valid());
    REQUIRE(std::any_of(processor_result.moves.begin(), processor_result.moves.end(),
                        [](const GCodeProcessorResult::MoveVertex& move) { return move.type == EMoveType::Seam; }));
    CHECK(model.volume_mm3 == Approx(3.0 * PI));
}

TEST_CASE("Warning provenance follows the selected aggregation", "[center_of_mass]")
{
    const auto result = process_center_of_mass(std::string(model_prefix) +
        "T0\n"
        "G1 X10 E1\n"
        "T1\n"
        "; FEATURE: Support\n"
        "G1 X20 E1\n"
        "; FEATURE: Multiple\n"
        "G1 X30 E1\n",
        { 2.0, 0.0 }, { 2.0, 0.0 });
    const auto finished = result.finished_model();
    const auto all = result.all_spatial_extrusions();

    REQUIRE(finished.valid());
    CHECK_FALSE(finished.used_default_density);
    CHECK_FALSE(finished.used_default_filament_diameter);
    CHECK_FALSE(finished.contains_unknown_roles);
    CHECK(all.used_default_density);
    CHECK(all.used_default_filament_diameter);
    CHECK(all.contains_unknown_roles);
}

TEST_CASE("Center-of-mass result copy and processor reset do not share state", "[center_of_mass]")
{
    FullPrintConfig config = FullPrintConfig::defaults();
    config.filament_diameter.values = { 2.0 };
    config.filament_density.values = { 2.0 };
    GCodeProcessor processor;
    processor.apply_config(config);
    processor.initialize("center-of-mass-copy-test.gcode");
    processor.process_buffer(std::string(model_prefix) + "; OBJECT_ID: 42\nG1 X10 E1\n");

    GCodeProcessorResult copy;
    copy = processor.get_result();
    processor.reset();

    CHECK(copy.get_center_of_mass().finished_model().valid());
    REQUIRE(copy.get_center_of_mass().by_object.size() == 1);
    CHECK(copy.get_center_of_mass().by_object.at(42).finished_model().valid());
    CHECK_FALSE(processor.get_result().get_center_of_mass().finished_model().valid());
    CHECK(processor.get_result().get_center_of_mass().by_object.empty());
    CHECK_FALSE(processor.get_result().get_center_of_mass().used_default_density);
    CHECK_FALSE(processor.get_result().get_center_of_mass().unsupported_volumetric_extrusion);
}

TEST_CASE("Preview offsets and explicit default-valued material data are preserved", "[center_of_mass]")
{
    const auto result = process_center_of_mass(std::string(model_prefix) + "G1 X10 Y10 E1\n",
                                               1.75, 1.245, 100.0, -50.0);
    const auto model = result.finished_model();

    REQUIRE(model.valid());
    CHECK(model.center_of_mass().x() == Approx(105.0));
    CHECK(model.center_of_mass().y() == Approx(-45.0));
    CHECK_FALSE(result.used_default_density);
    CHECK_FALSE(result.used_default_filament_diameter);
}

TEST_CASE("Missing material metadata uses conservative defaults", "[center_of_mass]")
{
    GCodeProcessor processor;
    processor.initialize("center-of-mass-default-material.gcode");
    processor.process_buffer(std::string(model_prefix) + "G1 X10 E1\n");
    const auto& result = processor.get_result().get_center_of_mass();

    REQUIRE(result.finished_model().valid());
    CHECK(result.used_default_density);
    CHECK(result.used_default_filament_diameter);
}

TEST_CASE("process_file uses supplied material config when G-code has none", "[center_of_mass]")
{
    TemporaryGCodeFile file;
    file.write(std::string(model_prefix) + "G1 X10 E1\n");

    GCodeProcessor processor;
    const DynamicPrintConfig fallback = material_config(2.0, 2.0);
    processor.process_file(file.path.string(), fallback);
    const auto model = processor.get_result().get_center_of_mass().finished_model();

    REQUIRE(model.valid());
    CHECK(model.volume_mm3 == Approx(PI));
    CHECK(model.mass_g == Approx(2.0 * PI / 1000.0));
    CHECK_FALSE(model.used_default_density);
    CHECK_FALSE(model.used_default_filament_diameter);
}

TEST_CASE("process_file falls back when an embedded config block is invalid", "[center_of_mass]")
{
    TemporaryGCodeFile file;
    file.write(std::string("; BambuStudio\n") + model_prefix +
               "G1 X10 E1\n"
               "; CONFIG_BLOCK_START\n"
               "; filament_diameter = 9\n"
               "; CONFIG_BLOCK_END\n");

    GCodeProcessor processor;
    const DynamicPrintConfig fallback = material_config(2.0, 2.0);
    processor.process_file(file.path.string(), fallback);
    const auto model = processor.get_result().get_center_of_mass().finished_model();

    REQUIRE(model.valid());
    CHECK(model.volume_mm3 == Approx(PI));
    CHECK(model.mass_g == Approx(2.0 * PI / 1000.0));
    CHECK_FALSE(model.used_default_density);
    CHECK_FALSE(model.used_default_filament_diameter);
}

TEST_CASE("Embedded material config overrides process_file fallback config", "[center_of_mass]")
{
    TemporaryGCodeFile file;
    const DynamicPrintConfig embedded = material_config(3.0, 3.0);
    file.write(with_embedded_config(embedded, std::string(model_prefix) + "G1 X10 E1\n"));

    GCodeProcessor processor;
    const DynamicPrintConfig fallback = material_config(2.0, 2.0);
    processor.process_file(file.path.string(), fallback);
    const auto model = processor.get_result().get_center_of_mass().finished_model();

    REQUIRE(model.valid());
    CHECK(model.volume_mm3 == Approx(2.25 * PI));
    CHECK(model.mass_g == Approx(6.75 * PI / 1000.0));
    CHECK_FALSE(model.used_default_density);
    CHECK_FALSE(model.used_default_filament_diameter);
}

TEST_CASE("Embedded config without material metadata reports default provenance", "[center_of_mass]")
{
    TemporaryGCodeFile file;
    DynamicPrintConfig embedded;
    embedded.apply(FullPrintConfig::defaults());
    REQUIRE(embedded.erase("filament_diameter"));
    REQUIRE(embedded.erase("filament_density"));
    file.write(with_embedded_config(embedded, std::string(model_prefix) + "G1 X10 E1\n"));

    GCodeProcessor processor;
    processor.process_file(file.path.string());
    const auto model = processor.get_result().get_center_of_mass().finished_model();

    REQUIRE(model.valid());
    CHECK(model.used_default_density);
    CHECK(model.used_default_filament_diameter);
}

TEST_CASE("A flow change alters spatial mass weighting on demand", "[center_of_mass][demand]")
{
    const auto model = process_center_of_mass(std::string(model_prefix) +
        "G1 X10 E1\nM221 S50\nG1 X20 E1\n").finished_model();
    REQUIRE(model.valid());
    CHECK(model.mass_g == Approx(3.0 * PI / 1000.0));
    CHECK(model.center_of_mass().x() == Approx(25.0 / 3.0));
}

TEST_CASE("A volumetric-mode change with retraction debt fails safely", "[center_of_mass][demand]")
{
    const auto model = process_center_of_mass(std::string(model_prefix) +
        "G1 E-1\nM200 D1.75\nG1 X10 E2\n").finished_model();
    CHECK_FALSE(model.valid());
    CHECK(model.unsupported_volumetric_extrusion);
}

TEST_CASE("The first extrusion uses its retained world-space start", "[center_of_mass][demand]")
{
    const auto model = process_center_of_mass(
        "G90\nM83\n; FEATURE: Outer wall\n; LAYER_HEIGHT: 0.2\nG1 X10 E1\n",
        2.0, 2.0, 100.0, -50.0).finished_model();
    REQUIRE(model.valid());
    CHECK(model.center_of_mass().x() == Approx(105.0));
    CHECK(model.center_of_mass().y() == Approx(-50.0));
}

TEST_CASE("Ordinary G-code processing leaves CoG unevaluated", "[center_of_mass][demand]")
{
    GCodeProcessor processor;
    processor.initialize("no-cog-request.gcode");
    processor.process_buffer(std::string(model_prefix) +
        "; OBJECT_ID: 42\nM200 Tbad D2\nM221 Tbad S100\nG1 X10 E1\n");
    const auto& result = processor.get_result();
    CHECK_FALSE(result.moves.empty());
    CHECK_FALSE(result.center_of_mass_cache);
    // Command state is retained, but no mass properties are calculated.
    CHECK_FALSE(result.extrusion_state_changes.empty());
}

TEST_CASE("CoG uses stored moves without reopening or replacing the preview result", "[center_of_mass][demand]")
{
    TemporaryGCodeFile file;
    file.write(std::string(model_prefix) + "; OBJECT_ID: 42\nG1 X10 E1\n");
    const auto config = material_config(2.0, 2.0);
    GCodeProcessor processor;
    processor.process_file(file.path.string(), config);
    auto& result = processor.result();
    const unsigned int original_id = result.id;
    const size_t original_moves = result.moves.size();
    const auto original_position = result.moves.back().position;
    REQUIRE_FALSE(result.center_of_mass_cache);
    // The very first request must succeed without a readable source.
    REQUIRE(boost::filesystem::remove(file.path));

    const auto model = result.get_center_of_mass().by_object.at(42).finished_model();
    REQUIRE(model.valid());
    CHECK(model.mass_g == Approx(2.0 * PI / 1000.0));
    CHECK(model.center_of_mass().x() == Approx(5.0));
    CHECK(result.id == original_id);
    CHECK(result.filename == file.path.string());
    CHECK(result.moves.size() == original_moves);
    CHECK(result.moves.back().position == original_position);
    REQUIRE(result.center_of_mass_cache);

    SECTION("Repeated requests reuse the cached result") {
        const auto* cached = &result.get_center_of_mass();
        CHECK(&result.get_center_of_mass() == cached);
    }
    SECTION("Material density changes invalidate the cache") {
        result.filament_densities[0] *= 2.0f;
        CHECK(result.get_center_of_mass().finished_model().mass_g == Approx(2.0 * model.mass_g));
    }
    SECTION("Filament diameter changes invalidate the cache") {
        result.filament_diameters[0] *= 2.0f;
        CHECK(result.get_center_of_mass().finished_model().mass_g == Approx(4.0 * model.mass_g));
    }
    SECTION("Material provenance changes invalidate the cache") {
        result.filament_density_configured[0] = false;
        CHECK(result.get_center_of_mass().finished_model().used_default_density);
    }
    SECTION("A new result identity invalidates the cache") {
        ++result.id;
        CHECK(result.get_center_of_mass().finished_model().mass_g == Approx(model.mass_g));
        CHECK(result.center_of_mass_cache->result_id == result.id);
    }
    SECTION("Filenames and modified source text do not affect stored moves") {
        file.write(std::string(model_prefix) + "G1 X20 E2\n");
        result.filename += ".missing";
        result.invalidate_center_of_mass();
        CHECK(result.get_center_of_mass().finished_model().mass_g == Approx(model.mass_g));
    }
    SECTION("In-place geometry edits explicitly invalidate the cache") {
        result.moves.back().position.x() = 20.0f;
        result.invalidate_center_of_mass();
        const auto changed = result.get_center_of_mass().finished_model();
        CHECK(changed.center_of_mass().x() == Approx(10.0));
        CHECK(changed.mass_g == Approx(model.mass_g));
    }
    SECTION("Appending moves invalidates the cache") {
        processor.process_buffer("G1 X20 E1\n");
        CHECK(result.get_center_of_mass().finished_model().mass_g == Approx(2.0 * model.mass_g));
    }
    SECTION("Copied results have independent lazy caches") {
        GCodeProcessorResult copy;
        copy = result;
        CHECK_FALSE(copy.center_of_mass_cache);
        copy.filament_densities[0] *= 2.0f;
        CHECK(copy.get_center_of_mass().finished_model().mass_g == Approx(2.0 * model.mass_g));
        CHECK(result.get_center_of_mass().finished_model().mass_g == Approx(model.mass_g));
    }
    SECTION("Reset discards moves, command state and the cached result") {
        GCodeProcessorResult copy;
        copy = result;
        REQUIRE(copy.get_center_of_mass().finished_model().valid());
        copy.reset();
        CHECK_FALSE(copy.center_of_mass_cache);
        CHECK(copy.extrusion_state_changes.empty());
        CHECK_FALSE(copy.extrusion_start_position);
        CHECK_FALSE(copy.get_center_of_mass().finished_model().valid());
    }
}

TEST_CASE("Unordered object accumulation exposes deterministic label order", "[center_of_mass][demand]")
{
    const auto result = process_center_of_mass(std::string(model_prefix) +
        "; OBJECT_ID: 202\nG1 X10 E1\n"
        "; OBJECT_ID: 7\nG1 X20 E1\n"
        "; OBJECT_ID: 101\nG1 X30 E1\n");
    CHECK((result.sorted_object_labels() == std::vector<int>{7, 101, 202}));
    REQUIRE(result.by_object.size() == 3);
    CHECK(result.by_object.at(7).finished_model().valid());
}

TEST_CASE("Post-processing replaces geometry and extrusion state without printer metadata", "[center_of_mass][demand]")
{
    FullPrintConfig config = FullPrintConfig::defaults();
    config.filament_diameter.values = {2.0};
    config.filament_density.values = {2.0};
    GCodeProcessor original;
    original.apply_config(config);
    original.initialize("original-preview.gcode");
    original.process_buffer(std::string(model_prefix) + "; OBJECT_ID: 42\nG1 X10 E1\n");
    auto& result = original.result();
    result.printer_extruder_id = {7, 9};
    result.filament_maps = {42};
    const unsigned int original_id = result.id;
    const double original_mass = result.get_center_of_mass().finished_model().mass_g;

    GCodeProcessor post_processed;
    post_processed.apply_config(config);
    post_processed.set_xy_offset(100.0, -50.0);
    post_processed.initialize("post-processed-preview.gcode");
    post_processed.process_buffer(std::string(model_prefix) +
        "; OBJECT_ID: 42\nM221 S50\nG1 X20 E4\n");
    result.update_moves_from(post_processed.extract_result());
    CHECK_FALSE(result.center_of_mass_cache);
    CHECK(result.id == original_id);
    CHECK(result.filename == "original-preview.gcode");
    CHECK((result.printer_extruder_id == std::vector<int>{7, 9}));
    CHECK(result.filament_maps == std::vector<int>{42});
    const auto model = result.get_center_of_mass().by_object.at(42).finished_model();
    CHECK(model.mass_g == Approx(2.0 * original_mass));
    CHECK(model.center_of_mass().x() == Approx(110.0));
    CHECK(model.center_of_mass().y() == Approx(-50.0));
}

TEST_CASE("Filament switches on one physical tool preserve retraction state", "[center_of_mass][demand]")
{
    FullPrintConfig config = FullPrintConfig::defaults();
    config.filament_diameter.values = {2.0, 2.0};
    config.filament_density.values = {2.0, 2.0};
    config.filament_map.values = {1, 1};
    GCodeProcessor processor;
    processor.apply_config(config);
    processor.initialize("shared-physical-tool.gcode");
    processor.process_buffer(std::string(model_prefix) +
        "T0\nG1 E-1\nT1\nG1 X10 E2\n");
    const auto model = processor.get_result().get_center_of_mass().finished_model();
    REQUIRE(model.valid());
    CHECK(model.volume_mm3 == Approx(PI));
    CHECK(model.center_of_mass().x() == Approx(7.5));
}
