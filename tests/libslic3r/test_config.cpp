#include <catch2/catch.hpp>

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/LocalesUtils.hpp"
#include "libslic3r/Slicing.hpp"
#include "libslic3r/AdaptiveLayerMerge.hpp"
#include "libslic3r/CustomGCode.hpp"

#include <cereal/types/polymorphic.hpp>
#include <cereal/types/string.hpp> 
#include <cereal/types/vector.hpp> 
#include <cereal/archives/binary.hpp>

using namespace Slic3r;

TEST_CASE("Single-diameter direct layering keeps physical nozzle height limits", "[Config][MultiDiameter]")
{
    SlicingParameters params;
    params.layer_grid_subdivision = 4;
    params.layer_grid_multiplier = 4;
    params.single_diameter_layering = true;

    CHECK(params.to_base_layer_height(0.4) == Approx(0.4));

    params.single_diameter_layering = false;
    params.layer_grid_multiplier = 1;
    CHECK(params.to_base_layer_height(0.4) == Approx(0.1));
    CHECK(params.to_layer_height_limit(0.16) == Approx(0.04));
}

TEST_CASE("Single-diameter support uses the actual nozzle instead of the first slot", "[Config][MultiDiameter]")
{
    PrintConfig print_config;
    print_config.nozzle_diameter.values = { 0.4 };
    print_config.min_layer_height.values = { 0.08 };
    print_config.max_layer_height.values = { 0.28 };
    print_config.initial_layer_print_height.value = 0.2;

    PrintObjectConfig object_config;
    object_config.layer_height.value = 0.2;
    object_config.enable_support.value = true;
    object_config.raft_layers.value = 3;
    object_config.support_filament.value = 0;
    object_config.support_interface_filament.value = 0;

    const SlicingParameters params = SlicingParameters::create_from_config(
        print_config, object_config, 10., { 0 }, 4, 4, 0.8);

    CHECK(params.min_layer_height == Approx(0.16));
    CHECK(params.max_layer_height == Approx(0.56));
    CHECK(params.max_suport_layer_height == Approx(0.56));
    CHECK(params.base_raft_layer_height == Approx(0.6));
    CHECK(params.interface_raft_layer_height == Approx(0.6));
}

TEST_CASE("Single-diameter direct layering slices at the merged band of the nozzle", "[Config][MultiDiameter]")
{
    // A 0.4 process preset on the 0.2 grid: 0.2 mm authored layers are cut into 2 x 0.1 mm grid steps.
    PrintConfig print_config;
    print_config.nozzle_diameter.values = { 0.4 };
    print_config.min_layer_height.values = { 0.08 };
    print_config.max_layer_height.values = { 0.28 };
    print_config.initial_layer_print_height.value = 0.2;

    PrintObjectConfig object_config;
    object_config.layer_height.value = 0.2;

    const int    subdivision = 2;
    const double grid_step   = object_config.layer_height.value / subdivision;
    for (const double nozzle : { 0.2, 0.4, 0.6, 0.8 }) {
        // The band a merge would have laid every step into, which direct layering stacks up front.
        const size_t band_size = adaptive_layer_band_size(nozzle, 0.2, grid_step);
        const SlicingParameters params = SlicingParameters::create_from_config(
            print_config, object_config, 10., { 0 }, subdivision, int(band_size), nozzle);
        CHECK(params.layer_height == Approx(double(band_size) * grid_step));
        // The first layer is the first band: the configured first layer keeps its ratio to the regular layer.
        CHECK(params.first_print_layer_height == Approx(double(band_size) * grid_step));
        CHECK(adaptive_layer_height_range(nozzle).contains(params.layer_height));
    }
    CHECK(adaptive_layer_band_size(0.6, 0.2, grid_step) == 3);
    CHECK(adaptive_layer_band_size(0.8, 0.2, grid_step) == 4);
}

TEST_CASE("Single-diameter direct layering scales the first layer with the band", "[Config][MultiDiameter]")
{
    PrintConfig print_config;
    print_config.nozzle_diameter.values = { 0.4 };
    print_config.min_layer_height.values = { 0.08 };
    print_config.max_layer_height.values = { 0.28 };

    PrintObjectConfig object_config;

    // A layer height inside the printer limits keeps the band at the diameter ratio, so the first layer keeps
    // the configured proportion: 0.3 mm on 0.4 mm layers of a 0.8 nozzle stays inside its window.
    object_config.layer_height.value = 0.2;
    print_config.initial_layer_print_height.value = 0.15;
    const SlicingParameters in_limits = SlicingParameters::create_from_config(
        print_config, object_config, 10., { 0 }, 2, int(adaptive_layer_band_size(0.8, 0.2, 0.1)), 0.8);
    CHECK(in_limits.first_print_layer_height == Approx(0.3));
    CHECK(adaptive_layer_height_range(0.8).contains(in_limits.first_print_layer_height));

    // A layer height below the printer limits makes the band grow past the diameter ratio to reach the nozzle's
    // minimum, and the first layer grows with it. Two objects on one plate then lay their first layer at
    // different heights, which is why Print keeps such a plate on the merge path.
    object_config.layer_height.value = 0.06;
    print_config.initial_layer_print_height.value = 0.2;
    const size_t grown_band = adaptive_layer_band_size(0.8, 0.2, 0.03);
    CHECK(grown_band == 6);
    const SlicingParameters grown = SlicingParameters::create_from_config(
        print_config, object_config, 10., { 0 }, 2, int(grown_band), 0.8);
    CHECK(grown.first_print_layer_height == Approx(0.6));
    CHECK(std::abs(grown.first_print_layer_height - in_limits.first_print_layer_height) > EPSILON);
}

TEST_CASE("Custom tool changes count only within the printed height", "[Config][MultiDiameter]")
{
    CustomGCode::Info info;
    info.mode = CustomGCode::MultiAsSingle;
    info.gcodes.push_back({ 2.0, CustomGCode::ToolChange, 2, "", "" });
    info.gcodes.push_back({ 10.0, CustomGCode::ToolChange, 3, "", "" });
    info.gcodes.push_back({ 12.0, CustomGCode::ToolChange, 4, "", "" });
    info.gcodes.push_back({ 5.0, CustomGCode::ColorChange, 4, "", "" });

    // A stack topping out at 10 mm reaches the change at its top layer, not the one above it.
    const std::vector<std::pair<double, unsigned int>> reached = CustomGCode::custom_tool_changes_up_to(info, 4, 10.);
    REQUIRE(reached.size() == 2);
    CHECK(reached[0].second == 2);
    CHECK(reached[1].second == 3);

    CHECK(CustomGCode::custom_tool_changes_up_to(info, 4, 1.).empty());
    CHECK(CustomGCode::custom_tool_changes_up_to(info, 4, 20.).size() == 3);
}

SCENARIO("Generic config validation performs as expected.", "[Config]") {
    GIVEN("A config generated from default options") {
        Slic3r::DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();
        WHEN( "perimeter_extrusion_width is set to 250%, a valid value") {
            config.set_deserialize_strict("perimeter_extrusion_width", "250%");
            THEN( "The config is read as valid.") {
                REQUIRE(config.validate().empty());
            }
        }
        WHEN( "perimeter_extrusion_width is set to -10, an invalid value") {
            config.set("perimeter_extrusion_width", -10);
            THEN( "Validate returns error") {
                REQUIRE(! config.validate().empty());
            }
        }

        WHEN( "perimeters is set to -10, an invalid value") {
            config.set("perimeters", -10);
            THEN( "Validate returns error") {
                REQUIRE(! config.validate().empty());
            }
        }
    }
}

SCENARIO("Config accessor functions perform as expected.", "[Config]") {
    GIVEN("A config generated from default options") {
        Slic3r::DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();
        WHEN("A boolean option is set to a boolean value") {
            REQUIRE_NOTHROW(config.set("gcode_comments", true));
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionBool>("gcode_comments")->getBool() == true);
            }
        }
        WHEN("A boolean option is set to a string value representing a 0 or 1") {
            CHECK_NOTHROW(config.set_deserialize_strict("gcode_comments", "1"));
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionBool>("gcode_comments")->getBool() == true);
            }
        }
        WHEN("A boolean option is set to a string value representing something other than 0 or 1") {
            THEN("A BadOptionTypeException exception is thrown.") {
                REQUIRE_THROWS_AS(config.set("gcode_comments", "Z"), BadOptionTypeException);
            }
            AND_THEN("Value is unchanged.") {
                REQUIRE(config.opt<ConfigOptionBool>("gcode_comments")->getBool() == false);
            }
        }
        WHEN("A boolean option is set to an int value") {
            THEN("A BadOptionTypeException exception is thrown.") {
                REQUIRE_THROWS_AS(config.set("gcode_comments", 1), BadOptionTypeException);
            }
        }
        WHEN("A numeric option is set from serialized string") {
            config.set_deserialize_strict("bed_temperature", "100");
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionInts>("bed_temperature")->get_at(0) == 100);
            }
        }
#if 0
		//FIXME better design accessors for vector elements.
		WHEN("An integer-based option is set through the integer interface") {
            config.set("bed_temperature", 100);
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionInts>("bed_temperature")->get_at(0) == 100);
            }
        }
#endif
        WHEN("An floating-point option is set through the integer interface") {
            config.set("perimeter_speed", 10);
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionFloat>("perimeter_speed")->getFloat() == 10.0);
            }
        }
        WHEN("A floating-point option is set through the double interface") {
            config.set("perimeter_speed", 5.5);
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionFloat>("perimeter_speed")->getFloat() == 5.5);
            }
        }
        WHEN("An integer-based option is set through the double interface") {
            THEN("A BadOptionTypeException exception is thrown.") {
                REQUIRE_THROWS_AS(config.set("bed_temperature", 5.5), BadOptionTypeException);
            }
        }
        WHEN("A numeric option is set to a non-numeric value.") {
            THEN("A BadOptionTypeException exception is thown.") {
                REQUIRE_THROWS_AS(config.set_deserialize_strict("perimeter_speed", "zzzz"), BadOptionValueException);
            }
            THEN("The value does not change.") {
                REQUIRE(config.opt<ConfigOptionFloat>("perimeter_speed")->getFloat() == 60.0);
            }
        }
        WHEN("A string option is set through the string interface") {
            config.set("end_gcode", "100");
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionString>("end_gcode")->value == "100");
            }
        }
        WHEN("A string option is set through the integer interface") {
            config.set("end_gcode", 100);
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionString>("end_gcode")->value == "100");
            }
        }
        WHEN("A string option is set through the double interface") {
            config.set("end_gcode", 100.5);
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionString>("end_gcode")->value == float_to_string_decimal_point(100.5));
            }
        }
        WHEN("A float or percent is set as a percent through the string interface.") {
            config.set_deserialize_strict("first_layer_extrusion_width", "100%");
            THEN("Value and percent flag are 100/true") {
                auto tmp = config.opt<ConfigOptionFloatOrPercent>("first_layer_extrusion_width");
                REQUIRE(tmp->percent == true);
                REQUIRE(tmp->value == 100);
            }
        }
        WHEN("A float or percent is set as a float through the string interface.") {
            config.set_deserialize_strict("first_layer_extrusion_width", "100");
            THEN("Value and percent flag are 100/false") {
                auto tmp = config.opt<ConfigOptionFloatOrPercent>("first_layer_extrusion_width");
                REQUIRE(tmp->percent == false);
                REQUIRE(tmp->value == 100);
            }
        }
        WHEN("A float or percent is set as a float through the int interface.") {
            config.set("first_layer_extrusion_width", 100);
            THEN("Value and percent flag are 100/false") {
                auto tmp = config.opt<ConfigOptionFloatOrPercent>("first_layer_extrusion_width");
                REQUIRE(tmp->percent == false);
                REQUIRE(tmp->value == 100);
            }
        }
        WHEN("A float or percent is set as a float through the double interface.") {
            config.set("first_layer_extrusion_width", 100.5);
            THEN("Value and percent flag are 100.5/false") {
                auto tmp = config.opt<ConfigOptionFloatOrPercent>("first_layer_extrusion_width");
                REQUIRE(tmp->percent == false);
                REQUIRE(tmp->value == 100.5);
            }
        }
        WHEN("An invalid option is requested during set.") {
            THEN("A BadOptionTypeException exception is thrown.") {
                REQUIRE_THROWS_AS(config.set("deadbeef_invalid_option", 1), UnknownOptionException);
                REQUIRE_THROWS_AS(config.set("deadbeef_invalid_option", 1.0), UnknownOptionException);
                REQUIRE_THROWS_AS(config.set("deadbeef_invalid_option", "1"), UnknownOptionException);
                REQUIRE_THROWS_AS(config.set("deadbeef_invalid_option", true), UnknownOptionException);
            }
        }

        WHEN("An invalid option is requested during get.") {
            THEN("A UnknownOptionException exception is thrown.") {
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionString>("deadbeef_invalid_option", false), UnknownOptionException);
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionFloat>("deadbeef_invalid_option", false), UnknownOptionException);
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionInt>("deadbeef_invalid_option", false), UnknownOptionException);
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionBool>("deadbeef_invalid_option", false), UnknownOptionException);
            }
        }
        WHEN("An invalid option is requested during opt.") {
            THEN("A UnknownOptionException exception is thrown.") {
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionString>("deadbeef_invalid_option", false), UnknownOptionException);
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionFloat>("deadbeef_invalid_option", false), UnknownOptionException);
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionInt>("deadbeef_invalid_option", false), UnknownOptionException);
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionBool>("deadbeef_invalid_option", false), UnknownOptionException);
            }
        }

        WHEN("getX called on an unset option.") {
            THEN("The default is returned.") {
                REQUIRE(config.opt_float("layer_height") == 0.3);
                REQUIRE(config.opt_int("raft_layers") == 0);
                REQUIRE(config.opt_bool("support_material") == false);
            }
        }

        WHEN("getFloat called on an option that has been set.") {
            config.set("layer_height", 0.5);
            THEN("The set value is returned.") {
                REQUIRE(config.opt_float("layer_height") == 0.5);
            }
        }
    }
}

SCENARIO("Config ini load/save interface", "[Config]") {
    WHEN("new_from_ini is called") {
		Slic3r::DynamicPrintConfig config;
		std::string path = std::string(TEST_DATA_DIR) + "/test_config/new_from_ini.ini";
		config.load_from_ini(path, ForwardCompatibilitySubstitutionRule::Disable);
        THEN("Config object contains ini file options.") {
			REQUIRE(config.option_throw<ConfigOptionStrings>("filament_colour", false)->values.size() == 1);
			REQUIRE(config.option_throw<ConfigOptionStrings>("filament_colour", false)->values.front() == "#ABCD");
        }
    }
}

SCENARIO("DynamicPrintConfig serialization", "[Config]") {
    WHEN("DynamicPrintConfig is serialized and deserialized") {
        FullPrintConfig full_print_config;
        DynamicPrintConfig cfg;
        cfg.apply(full_print_config, false);

        std::string serialized;
        try {
            std::ostringstream ss;
            cereal::BinaryOutputArchive oarchive(ss);
            oarchive(cfg);
            serialized = ss.str();
        } catch (const std::runtime_error & /* e */) {
            // e.what();
        }

        THEN("Config object contains ini file options.") {
            DynamicPrintConfig cfg2;
            try {
                std::stringstream ss(serialized);
                cereal::BinaryInputArchive iarchive(ss);
                iarchive(cfg2);
            } catch (const std::runtime_error & /* e */) {
                // e.what();
            }
            REQUIRE(cfg == cfg2);
        }
    }
}

SCENARIO("get_real_skirt_dist calculates the correct boundary including loop width", "[Config]") {
    GIVEN("A DynamicPrintConfig with skirt loops and spacing") {
        Slic3r::DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();

        config.set("skirt_distance", 2.0);
        config.set("skirt_loops", 3);
        config.set("initial_layer_line_width", 0.4);
        config.set("draft_shield", "disabled"); // Just to be safe, dsDisabled is 0 usually
        config.set("skirt_per_object", true);

        WHEN("get_real_skirt_dist is called") {
            float dist = Slic3r::get_real_skirt_dist(config);

            THEN("The distance includes the width of the skirt loops") {
                // 2.0 + 3 * 0.4 = 3.2
                REQUIRE(dist == Approx(3.2));
            }
        }

        WHEN("skirt_per_object is disabled") {
            config.set("skirt_per_object", false);
            float dist = Slic3r::get_real_skirt_dist(config);
            THEN("The distance is not applied") {
                REQUIRE(dist == 0.0f);
            }
        }

        WHEN("skirt_loops is 0") {
            config.set("skirt_loops", 0);
            float dist = Slic3r::get_real_skirt_dist(config);
            THEN("The distance is exactly 0 because has_skirt() should return false") {
                REQUIRE(dist == 0.0f);
            }
        }
    }
}

TEST_CASE("Bed shape falls back when extruder areas do not overlap", "[Config]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.opt<ConfigOptionPoints>("printable_area")->values =
        {{0., 0.}, {30., 0.}, {30., 10.}, {0., 10.}};
    config.opt<ConfigOptionPointsGroups>("extruder_printable_area")->values = {
        {{0., 0.}, {10., 0.}, {10., 10.}, {0., 10.}},
        {{20., 0.}, {30., 0.}, {30., 10.}, {20., 10.}}
    };

    const Points bed_shape = get_bed_shape(config, true);
    REQUIRE(bed_shape.size() == 4);
    REQUIRE(Polygon(bed_shape).bounding_box().defined);
}
