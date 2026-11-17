#include <catch2/catch.hpp>

#include "test_data.hpp"
#include "libslic3r/GCode/HeightSlowdown.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/GCodeWriter.hpp"
#include "libslic3r/Layer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>
#include <boost/filesystem.hpp>

using namespace Slic3r;

namespace {

DynamicPrintConfig height_config(double requested_speed = 20., double volumetric_cap = 5.)
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        {"layer_height", 0.2}, {"initial_layer_print_height", 0.2},
        {"wall_loops", 2}, {"wall_generator", "classic"},
        {"sparse_infill_density", 0}, {"top_shell_layers", 0}, {"bottom_shell_layers", 0},
        {"skirt_loops", 0}, {"brim_type", "no_brim"}, {"enable_support", false},
        {"detect_overhang_wall", false}, {"slow_down_for_layer_cooling", false},
        {"enable_arc_fitting", false}, {"reduce_crossing_wall", false},
        {"outer_wall_line_width", 0.45}, {"inner_wall_line_width", 0.45},
        {"initial_layer_line_width", 0.45},
        {"outer_wall_speed", requested_speed}, {"inner_wall_speed", requested_speed},
        {"initial_layer_speed", requested_speed > 0. ? requested_speed : 20.},
        {"initial_layer_infill_speed", requested_speed > 0. ? requested_speed : 20.},
        {"filament_max_volumetric_speed", volumetric_cap},
        {"filament_adaptive_volumetric_speed", false},
        {"default_acceleration", 2000}, {"outer_wall_acceleration", 1200},
        {"inner_wall_acceleration", 1600}, {"initial_layer_acceleration", 800},
        {"travel_speed", 150}, {"travel_speed_z", 12},
        {"z_hop", 0},
        {"travel_acceleration", 4000}, {"travel_short_distance_acceleration", 600},
        {"initial_layer_travel_acceleration", 1000},
        {"machine_max_acceleration_extruding", "20000,20000"},
        {"enable_height_slowdown_auto", false}, {"enable_height_slowdown", false},
        {"height_slowdown_initial_speed_boost", false},
        {"gcode_flavor", "marlin"},
        {"machine_start_gcode", ""}, {"machine_end_gcode", ""}
    });
    // Full static defaults omit the enum lookup maps on vector enum clones.
    // Real presets initialize these through their schema before serialization.
    for (const std::string &key : config.keys()) {
        if (auto *option = dynamic_cast<ConfigOptionEnumsGenericNullable *>(config.option(key)))
            option->keys_map = print_config_def.get(key)->enum_keys_map;
        else if (auto *option = dynamic_cast<ConfigOptionEnumsGeneric *>(config.option(key)))
            option->keys_map = print_config_def.get(key)->enum_keys_map;
    }
    return config;
}

struct Move {
    double x, y, z, model_z, speed, acceleration, extrusion_per_mm;
    bool extruding;
};

std::vector<Move> read_moves(const std::string &gcode, const DynamicPrintConfig &config, bool z_descents = false)
{
    std::vector<Move> moves;
    double acceleration = 0.;
    double model_z = 0.;
    GCodeReader reader;
    reader.apply_config(config);
    reader.parse_buffer(gcode, [&](GCodeReader &self, const GCodeReader::GCodeLine &line) {
        // Layer-transition travel can happen before physical Z changes, and
        // lifted XY travel runs above the nominal layer. Use the slicer's tag.
        std::sscanf(line.raw().c_str(), "; Z_HEIGHT: %lf", &model_z);
        if (line.cmd_is("M204")) {
            float value;
            if (line.has_value('S', value) || line.has_value('P', value))
                acceleration = value;
        }
        const bool selected_move = z_descents ? line.dist_Z(self) < -0.001 : line.dist_XY(self) > 0.001;
        if ((line.cmd_is("G1") || line.cmd_is("G0")) && selected_move) {
            const bool extruding = line.extruding(self);
            // GCodeReader resets its E origin before the callback in relative
            // mode, so dist_E gives the deposited length in either E mode.
            const double extrusion_per_mm = extruding && line.dist_XY(self) > 0.001
                ? line.dist_E(self) / line.dist_XY(self) : 0.;
            // Retraction/wipe paths have extrusion coordinates but are not print moves.
            if (extruding || !line.has_e())
                moves.push_back({line.new_X(self), line.new_Y(self), line.new_Z(self), model_z,
                                 line.new_F(self) / 60., acceleration, extrusion_per_mm, extruding});
        }
    });
    return moves;
}

std::vector<Move> slice_moves(const DynamicPrintConfig &config, bool two_heights = false, bool z_descents = false,
                              bool flow_calibration = false, bool two_islands = false)
{
    Print print;
    Model model;
    std::vector<TriangleMesh> meshes;
    meshes.emplace_back(make_cube(12., 12., 1.));
    if (two_heights || two_islands)
        meshes.emplace_back(make_cube(12., 12., two_heights ? 0.6 : 1.));
    // The shared init_print helper invokes the legacy InfiniteBed arranger,
    // which rejects even a single cube in this fork. Place these tiny fixtures
    // explicitly on the ordinary 200 mm bed instead of testing arrangement.
    for (size_t i = 0; i < meshes.size(); ++i) {
        ModelObject *object = model.add_object();
        object->name = "height-slowdown-cube.stl";
        object->add_volume(std::move(meshes[i]));
        object->add_instance()->set_offset(Vec3d(50. + 30. * i, 50., 0.));
        object->ensure_on_bed();
        print.auto_assign_extruders(object);
    }
    print.apply(model, config);
    print.validate();
    print.set_status_silent();
    if (flow_calibration)
        print.calib_mode() = CalibMode::Calib_Flow_Rate;
    // The shared exporter supplies a relative filename with an empty parent;
    // this fork's exporter requires an existing parent directory.
    const auto output = boost::filesystem::temp_directory_path() /
                        boost::filesystem::unique_path("height-slowdown-%%%%-%%%%.gcode");
    print.process();
    GCodeProcessorResult result;
    print.export_gcode(output.string(), &result, nullptr);
    std::ifstream stream(output.string());
    const std::string gcode((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    stream.close();
    if (const char *evidence_dir = std::getenv("HEIGHT_SLOWDOWN_GCODE_DIR")) {
        boost::filesystem::create_directories(evidence_dir);
        boost::filesystem::copy_file(output, boost::filesystem::path(evidence_dir) / output.filename());
    }
    boost::filesystem::remove(output);
    REQUIRE_FALSE(gcode.empty());
    REQUIRE(print.objects().front()->layers().front()->print_z == Approx(0.2));
    REQUIRE(print.objects().front()->layers().back()->print_z == Approx(1.));
    return read_moves(gcode, config, z_descents);
}

void require_same_path(const Move &baseline, const Move &slowed)
{
    REQUIRE(slowed.x == Approx(baseline.x));
    REQUIRE(slowed.y == Approx(baseline.y));
    REQUIRE(slowed.z == Approx(baseline.z));
    REQUIRE(slowed.model_z == Approx(baseline.model_z));
    REQUIRE(slowed.extruding == baseline.extruding);
    if (baseline.extruding) {
        REQUIRE(baseline.extrusion_per_mm > 0.);
        REQUIRE(slowed.extrusion_per_mm == Approx(baseline.extrusion_per_mm));
    }
}

} // namespace

TEST_CASE("Automatic height slowdown uses the object's model-layer range", "[HeightSlowdown]")
{
    REQUIRE(automatic_height_slowdown_factor(-1., 0.2, 1.) == Approx(1.));
    REQUIRE(automatic_height_slowdown_factor(0.2, 0.2, 1.) == Approx(1.));
    REQUIRE(automatic_height_slowdown_factor(0.6, 0.2, 1.) == Approx(0.75));
    REQUIRE(automatic_height_slowdown_factor(1., 0.2, 1.) == Approx(0.5));
    REQUIRE(automatic_height_slowdown_factor(2., 0.2, 1.) == Approx(0.5));
    // A raft or lifted object changes the range's origin, not the ramp.
    REQUIRE(automatic_height_slowdown_factor(5.6, 5.2, 6.) == Approx(0.75));
    REQUIRE(automatic_height_slowdown_factor(1., 1., 1.) == Approx(1.));
    REQUIRE(automatic_height_slowdown_factor(2., 1., 0.2) == Approx(1.));
    REQUIRE(automatic_height_slowdown_factor(0.8, 0.8, 1., 1.2) == Approx(1.2));
    REQUIRE(automatic_height_slowdown_factor(0.9, 0.8, 1., 1.2) == Approx(0.85));
    REQUIRE(automatic_height_slowdown_factor(1., 0.8, 1., 1.2) == Approx(0.5));
    REQUIRE(automatic_height_slowdown_factor(1., 1., 1., 1.2) == Approx(1.));
}

TEST_CASE("Automatic height slowdown defaults off and survives config serialization", "[HeightSlowdown][Config]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    for (const std::string key : {"enable_height_slowdown_auto", "height_slowdown_initial_speed_boost"}) {
        const auto *option = config.option<ConfigOptionBoolsNullable>(key);
        REQUIRE(option != nullptr);
        REQUIRE_FALSE(option->get_at(0));
        REQUIRE(print_config_def.get(key)->nullable);
        for (const std::string value : {"1", "0", "nil"}) {
            config.set_deserialize_strict(key, value);
            DynamicPrintConfig restored;
            restored.set_deserialize_strict(key, config.option(key)->serialize());
            REQUIRE(*restored.option(key) == *config.option(key));
        }
    }
}

TEST_CASE("Automatic height slowdown scales emitted speeds after filament flow limits", "[HeightSlowdown][GCode]")
{
    const double requested_speed = GENERATE(0., 20., 400.);
    const bool adaptive_flow = GENERATE(false, true);
    INFO("Requested speed: " << requested_speed);
    INFO("Adaptive flow cap: " << adaptive_flow);
    DynamicPrintConfig config = height_config(requested_speed);
    // Cover both E encodings without adding more slicing combinations.
    config.set_deserialize_strict("use_relative_e_distances", requested_speed == 400. ? "0" : "1");
    if (adaptive_flow) {
        config.set_deserialize_strict("filament_adaptive_volumetric_speed", "1");
        // A constant fitted 3 mm3/s is lower than the filament's 5 mm3/s.
        config.set_deserialize_strict("volumetric_speed_coefficients", "0 0 0 0 0 3");
    }
    const auto baseline = slice_moves(config);
    config.set_deserialize_strict("enable_height_slowdown_auto", "1");
    const auto slowed = slice_moves(config);
    REQUIRE(slowed.size() == baseline.size());
    size_t first = 0, middle = 0, last = 0, scaled_travels = 0;
    double maximum_baseline_speed = 0.;
    for (size_t i = 0; i < baseline.size(); ++i) {
        const Move &normal = baseline[i], &slow = slowed[i];
        INFO("Move " << i << ", physical Z " << normal.z << ", model Z " << normal.model_z
             << ", XY " << normal.x << "," << normal.y << ", normal acceleration " << normal.acceleration);
        require_same_path(normal, slow);
        if (normal.z < 0.19 || normal.z > 1.01)
            continue;
        const double factor = automatic_height_slowdown_factor(normal.model_z, 0.2, 1.);
        if (normal.extruding) {
            INFO("Move " << i << " at Z=" << normal.z);
            REQUIRE(slow.speed == Approx(normal.speed * factor).margin(0.02));
            REQUIRE(normal.acceleration > 0.);
            REQUIRE(slow.acceleration == Approx(normal.acceleration * factor).margin(1.));
            maximum_baseline_speed = std::max(maximum_baseline_speed, normal.speed);
            if (std::abs(normal.z - 0.2) < 0.001) ++first;
            if (std::abs(normal.z - 0.6) < 0.001) ++middle;
            if (std::abs(normal.z - 1.) < 0.001) ++last;
        } else if (factor < 0.99 && std::abs(slow.speed - normal.speed * factor) < 0.02) {
            ++scaled_travels;
            REQUIRE(slow.acceleration == Approx(normal.acceleration * factor).margin(1.));
        } else if (!normal.extruding) {
            // Non-eligible travel keeps its ordinary speed. This also catches
            // an accidentally persistent or repeatedly compounded travel ramp.
            REQUIRE(slow.speed == Approx(normal.speed).margin(0.02));
        }
    }
    REQUIRE(first > 0);
    REQUIRE(middle > 0);
    REQUIRE(last > 0);
    REQUIRE(scaled_travels > 0);
    if (requested_speed == 20.)
        REQUIRE(maximum_baseline_speed == Approx(20.).margin(0.02));
    else if (requested_speed > 0.) {
        // Prove this fixture is flow-capped; scaling the request before capping
        // would leave the top at this same capped speed and fail the ratios above.
        REQUIRE(maximum_baseline_speed < requested_speed * 0.5);
        REQUIRE(maximum_baseline_speed > 20.);
    } else {
        // A zero request selects flow-based automatic speed, which must still
        // receive the height factor after that ordinary speed is calculated.
        REQUIRE(maximum_baseline_speed > 20.);
    }
}

TEST_CASE("Automatic height slowdown takes precedence and preserves manual mode when off", "[HeightSlowdown][GCode]")
{
    DynamicPrintConfig config = height_config();
    config.set_deserialize_strict("enable_height_slowdown_auto", "1");
    const auto automatic_only = slice_moves(config);
    config.set_deserialize_strict({
        {"enable_height_slowdown", true}, {"slowdown_start_height", 0}, {"slowdown_end_height", 0.2},
        {"slowdown_start_speed", 7}, {"slowdown_end_speed", 7},
        {"slowdown_start_acc", 300}, {"slowdown_end_acc", 300}
    });
    const auto both_enabled = slice_moves(config);
    REQUIRE(both_enabled.size() == automatic_only.size());
    for (size_t i = 0; i < automatic_only.size(); ++i) {
        require_same_path(automatic_only[i], both_enabled[i]);
        REQUIRE(both_enabled[i].speed == Approx(automatic_only[i].speed));
        REQUIRE(both_enabled[i].acceleration == Approx(automatic_only[i].acceleration));
    }
    config.set_deserialize_strict("enable_height_slowdown_auto", "0");
    const auto manual_only = slice_moves(config);
    size_t print_moves = 0;
    for (const Move &move : manual_only) {
        if (move.extruding) {
            REQUIRE(move.speed == Approx(7.).margin(0.02));
            REQUIRE(move.acceleration == Approx(300.).margin(1.));
            ++print_moves;
        }
    }
    REQUIRE(print_moves > 0);
    // Nullable override slots use 255 for nil. It must not enable the ramp
    // through an implicit bool conversion and override these manual settings.
    config.set_deserialize_strict("enable_height_slowdown_auto", "nil");
    const auto inherited_manual = slice_moves(config);
    REQUIRE(inherited_manual.size() == manual_only.size());
    for (size_t i = 0; i < manual_only.size(); ++i) {
        require_same_path(manual_only[i], inherited_manual[i]);
        REQUIRE(inherited_manual[i].speed == Approx(manual_only[i].speed));
        REQUIRE(inherited_manual[i].acceleration == Approx(manual_only[i].acceleration));
    }
}

TEST_CASE("Automatic height slowdown remains active while descending from a Z hop", "[HeightSlowdown][GCode]")
{
    DynamicPrintConfig config = height_config();
    config.set_deserialize_strict({
        {"z_hop", 0.4},
        {"retract_lift_above", 0}, {"retract_lift_below", 10},
        {"retraction_length", 1}, {"retraction_minimum_travel", 0},
        {"retract_when_changing_layer", true}, {"wipe", false},
        {"reduce_infill_retraction_mode", "Disabled"}
    });
    auto *hop_type = config.option<ConfigOptionEnumsGenericNullable>("z_hop_types");
    hop_type->values = {zhtNormal};
    hop_type->keys_map = &ConfigOptionEnum<ZHopType>::get_enum_values();
    const auto baseline = slice_moves(config, false, true, false, true);
    config.set_deserialize_strict("enable_height_slowdown_auto", "1");
    const auto slowed = slice_moves(config, false, true, false, true);
    REQUIRE_FALSE(baseline.empty());
    REQUIRE(slowed.size() == baseline.size());
    size_t first = 0, last = 0;
    std::vector<double> descent_heights;
    for (size_t i = 0; i < baseline.size(); ++i) {
        const Move &normal = baseline[i], &slow = slowed[i];
        descent_heights.push_back(normal.z);
        require_same_path(normal, slow);
        const double factor = automatic_height_slowdown_factor(normal.z, 0.2, 1.);
        INFO("Z-hop descent to " << normal.z);
        REQUIRE(normal.speed == Approx(12.).margin(0.02));
        REQUIRE(normal.acceleration > 0.);
        REQUIRE(slow.speed == Approx(normal.speed * factor).margin(0.02));
        REQUIRE(slow.acceleration == Approx(normal.acceleration * factor).margin(1.));
        if (std::abs(normal.z - 0.2) < 0.001) ++first;
        if (std::abs(normal.z - 1.) < 0.001) ++last;
    }
    REQUIRE(first > 0);
    CAPTURE(descent_heights);
    REQUIRE(last > 0);
}

TEST_CASE("Automatic height slowdown finishes independently on objects of different heights", "[HeightSlowdown][GCode]")
{
    DynamicPrintConfig config = height_config();
    const auto baseline = slice_moves(config, true);
    config.set_deserialize_strict("enable_height_slowdown_auto", "1");
    const auto slowed = slice_moves(config, true);
    REQUIRE(slowed.size() == baseline.size());
    size_t short_top = 0, tall_middle = 0;
    for (size_t i = 0; i < baseline.size(); ++i) {
        require_same_path(baseline[i], slowed[i]);
        if (!baseline[i].extruding || std::abs(baseline[i].z - 0.6) > 0.001)
            continue;
        const double ratio = slowed[i].speed / baseline[i].speed;
        if (std::abs(ratio - 0.5) < 0.002) ++short_top;
        else if (std::abs(ratio - 0.75) < 0.002) ++tall_middle;
        else FAIL("At Z=0.6, short object must be at 50% and tall object at 75%; got " << ratio);
    }
    REQUIRE(short_top > 0);
    REQUIRE(tall_middle > 0);
}

TEST_CASE("Optional initial speed boost preserves adhesion layers and obeys flow and motion limits", "[HeightSlowdown][GCode][HeightBoost]")
{
    // 0: ordinary boost, 1: filament capped, 2: fitted flow capped,
    // 3: XY capped, 4: E capped, 5: missing flow cap, 6: missing XY cap.
    const int limit_case = GENERATE(0, 1, 2, 3, 4, 5, 6);
    INFO("Boost limit case: " << limit_case);
    DynamicPrintConfig config = height_config(limit_case == 1 || limit_case == 2 ? 400. : 20.);
    config.set_deserialize_strict({
        {"machine_max_speed_x", "500,500"}, {"machine_max_speed_y", "500,500"},
        {"machine_max_speed_e", "30,30"}
    });
    if (limit_case == 2) {
        config.set_deserialize_strict("filament_adaptive_volumetric_speed", "1");
        config.set_deserialize_strict("volumetric_speed_coefficients", "0 0 0 0 0 3");
    } else if (limit_case == 3) {
        config.set_deserialize_strict("machine_max_speed_x", "22,22");
    } else if (limit_case == 4) {
        config.set_deserialize_strict("machine_max_speed_e", "0.75,0.75");
    } else if (limit_case == 5) {
        config.set_deserialize_strict("filament_max_volumetric_speed", "0");
    } else if (limit_case == 6) {
        config.set_deserialize_strict("machine_max_speed_x", "nil,nil");
    }
    const auto baseline = slice_moves(config);
    config.set_deserialize_strict("enable_height_slowdown_auto", "1");
    config.set_deserialize_strict("height_slowdown_initial_speed_boost", "1");
    const auto boosted = slice_moves(config);
    REQUIRE(boosted.size() == baseline.size());
    size_t adhesion_moves = 0, boosted_moves = 0, top_moves = 0, scaled_travels = 0;
    for (size_t i = 0; i < baseline.size(); ++i) {
        const Move &normal = baseline[i], &boost = boosted[i];
        INFO("Move " << i << ", physical Z " << normal.z << ", model Z " << normal.model_z
             << ", XY " << normal.x << "," << normal.y << ", normal acceleration " << normal.acceleration);
        require_same_path(normal, boost);
        if (normal.z < 0.19 || normal.z > 1.01)
            continue;
        const double basic_factor = automatic_height_slowdown_factor(normal.model_z, 0.2, 1.);
        if (!normal.extruding) {
            if (basic_factor < 0.99 && std::abs(boost.speed - normal.speed * basic_factor) < 0.02) {
                REQUIRE(boost.acceleration == Approx(normal.acceleration * basic_factor).margin(1.));
                ++scaled_travels;
            } else {
                REQUIRE(boost.speed == Approx(normal.speed).margin(0.02));
            }
            continue;
        }
        // Feed boost does not boost acceleration or deposited material.
        REQUIRE(boost.acceleration == Approx(normal.acceleration * basic_factor).margin(1.));
        if (normal.z < 0.61) {
            REQUIRE(boost.speed == Approx(normal.speed).margin(0.02));
            ++adhesion_moves;
        } else if (std::abs(normal.z - 0.8) < 0.001) {
            double expected = normal.speed * 1.2;
            if (limit_case == 1 || limit_case == 2 || limit_case == 5 || limit_case == 6)
                expected = normal.speed;
            else if (limit_case == 3)
                expected = std::min(expected, 22.);
            else if (limit_case == 4)
                expected = std::min(expected, 0.75 / normal.extrusion_per_mm);
            INFO("Fourth-layer move, baseline " << normal.speed);
            // E output is rounded, so its measured ratio can differ slightly
            // from the unrounded path flow used for the physical E limit.
            REQUIRE(boost.speed == Approx(expected).margin(limit_case == 4 ? 0.25 : 0.03));
            if (limit_case == 0)
                REQUIRE(boost.speed == Approx(24.).margin(0.02));
            if (limit_case == 3 || limit_case == 4)
                REQUIRE(boost.speed > normal.speed);
            ++boosted_moves;
        } else if (std::abs(normal.z - 1.) < 0.001) {
            REQUIRE(boost.speed == Approx(normal.speed * 0.5).margin(0.02));
            ++top_moves;
        }
    }
    REQUIRE(adhesion_moves > 0);
    REQUIRE(boosted_moves > 0);
    REQUIRE(top_moves > 0);
    REQUIRE(scaled_travels > 0);
}

TEST_CASE("Optional initial boost is inactive without automatic mode or when nil", "[HeightSlowdown][GCode][HeightBoost]")
{
    const bool automatic = GENERATE(false, true);
    DynamicPrintConfig config = height_config();
    config.set_deserialize_strict("enable_height_slowdown_auto", automatic ? "1" : "0");
    const auto baseline = slice_moves(config);
    config.set_deserialize_strict("height_slowdown_initial_speed_boost", automatic ? "nil" : "1");
    const auto unchanged = slice_moves(config);
    REQUIRE(unchanged.size() == baseline.size());
    for (size_t i = 0; i < baseline.size(); ++i) {
        require_same_path(baseline[i], unchanged[i]);
        REQUIRE(unchanged[i].speed == Approx(baseline[i].speed));
        REQUIRE(unchanged[i].acceleration == Approx(baseline[i].acceleration));
    }
}

TEST_CASE("Initial speed boost stays bounded for spiral vase and flow calibration", "[HeightSlowdown][GCode][HeightBoost]")
{
    const bool spiral_vase = GENERATE(false, true);
    INFO("Spiral vase: " << spiral_vase);
    DynamicPrintConfig config = height_config();
    config.set_deserialize_strict({
        {"machine_max_speed_x", "500,500"}, {"machine_max_speed_y", "500,500"},
        {"machine_max_speed_e", "30,30"}
    });
    if (spiral_vase)
        config.set_deserialize_strict({{"spiral_mode", true}, {"wall_loops", 1}});
    const auto baseline = slice_moves(config, false, false, !spiral_vase);
    config.set_deserialize_strict("enable_height_slowdown_auto", "1");
    config.set_deserialize_strict("height_slowdown_initial_speed_boost", "1");
    const auto guarded = slice_moves(config, false, false, !spiral_vase);
    REQUIRE(guarded.size() == baseline.size());
    size_t early = 0, boost_range = 0, top = 0;
    for (size_t i = 0; i < baseline.size(); ++i) {
        const Move &normal = baseline[i], &slow = guarded[i];
        require_same_path(normal, slow);
        if (!normal.extruding)
            continue;
        // Spiral processing interpolates each layer's Z, so examine a band
        // inside layer four rather than assuming every command ends at 0.8.
        REQUIRE(slow.speed <= normal.speed + 0.02);
        if (normal.z < 0.59) {
            REQUIRE(slow.speed == Approx(normal.speed).margin(0.02));
            ++early;
        }
        if ((spiral_vase && normal.z > 0.61 && normal.z < 0.79) ||
            (!spiral_vase && std::abs(normal.z - 0.8) < 0.001)) {
            REQUIRE(slow.speed == Approx(normal.speed).margin(0.02));
            ++boost_range;
        }
        if (normal.z > 0.81) {
            REQUIRE(slow.speed == Approx(normal.speed * 0.5).margin(0.02));
            ++top;
        }
    }
    REQUIRE(early > 0);
    REQUIRE(boost_range > 0);
    REQUIRE(top > 0);
}

TEST_CASE("Automatic height slowdown scales effective machine-capped acceleration", "[HeightSlowdown][GCode]")
{
    DynamicPrintConfig config = height_config();
    config.set_deserialize_strict("machine_max_acceleration_extruding", "1500,1500");
    const auto baseline = slice_moves(config);
    config.set_deserialize_strict("enable_height_slowdown_auto", "1");
    const auto slowed = slice_moves(config);
    REQUIRE(slowed.size() == baseline.size());
    size_t capped_print = 0, capped_travel = 0;
    for (size_t i = 0; i < baseline.size(); ++i) {
        const Move &normal = baseline[i], &slow = slowed[i];
        INFO("Move " << i << ", model Z " << normal.model_z << ", baseline acceleration " << normal.acceleration);
        require_same_path(normal, slow);
        if (normal.model_z <= 0.2 || normal.model_z > 1.001)
            continue;
        const double factor = automatic_height_slowdown_factor(normal.model_z, 0.2, 1.);
        if (normal.extruding || std::abs(slow.speed - normal.speed * factor) < 0.02) {
            REQUIRE(slow.acceleration == Approx(normal.acceleration * factor).margin(1.));
            if (std::abs(normal.acceleration - 1500.) < 0.01) {
                if (normal.extruding) ++capped_print;
                else ++capped_travel;
            }
        }
    }
    // Inner-wall 1600 and ordinary travel 4000 both hit the machine's 1500 cap.
    REQUIRE(capped_print > 0);
    REQUIRE(capped_travel > 0);
}

TEST_CASE("Travel acceleration ordering supports travel before initial tool selection", "[HeightSlowdown][GCodeWriter]")
{
    GCodeWriter writer;
    writer.config.travel_speed.values = {150.};
    writer.config.travel_speed_z.values = {0.};
    writer.set_extruders({0, 1});
    writer.set_first_layer(false);
    writer.set_current_position_clear(false);
    REQUIRE(writer.filament() == nullptr);
    const std::string travel = writer.travel_to_xyz(Vec3d(10., 20., 0.2));
    REQUIRE_THAT(travel, Catch::Contains("X10 Y20 F9000"));
    REQUIRE_THAT(travel, Catch::Contains("Z.2 F9000"));
}
