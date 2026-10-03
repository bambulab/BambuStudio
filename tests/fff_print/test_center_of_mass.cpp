#include <catch2/catch.hpp>

#include "libslic3r/GCode/GCodeProcessor.hpp"

#include <algorithm>
#include <boost/filesystem.hpp>
#include <fstream>
#include <numeric>
#include <sstream>

using namespace Slic3r;

namespace {

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
    config.filament_map.values.resize(filament_diameters.size());
    std::iota(config.filament_map.values.begin(), config.filament_map.values.end(), 1);

    GCodeProcessor processor;
    processor.apply_config(config);
    processor.set_xy_offset(x_offset, y_offset);
    processor.initialize("center-of-mass-test.gcode");
    processor.process_buffer(gcode);
    return processor.get_result().center_of_mass;
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
    const auto model = processor_result.center_of_mass.finished_model();

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

    CHECK(copy.center_of_mass.finished_model().valid());
    REQUIRE(copy.center_of_mass.by_object.size() == 1);
    CHECK(copy.center_of_mass.by_object.at(42).finished_model().valid());
    CHECK_FALSE(processor.get_result().center_of_mass.finished_model().valid());
    CHECK(processor.get_result().center_of_mass.by_object.empty());
    CHECK_FALSE(processor.get_result().center_of_mass.used_default_density);
    CHECK_FALSE(processor.get_result().center_of_mass.unsupported_volumetric_extrusion);
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
    const auto& result = processor.get_result().center_of_mass;

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
    const auto model = processor.get_result().center_of_mass.finished_model();

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
    const auto model = processor.get_result().center_of_mass.finished_model();

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
    const auto model = processor.get_result().center_of_mass.finished_model();

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
    const auto model = processor.get_result().center_of_mass.finished_model();

    REQUIRE(model.valid());
    CHECK(model.used_default_density);
    CHECK(model.used_default_filament_diameter);
}
