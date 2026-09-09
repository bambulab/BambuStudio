#include <catch2/catch.hpp>

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/LocalesUtils.hpp"

#include <cereal/types/polymorphic.hpp>
#include <cereal/types/string.hpp> 
#include <cereal/types/vector.hpp> 
#include <cereal/archives/binary.hpp>

using namespace Slic3r;

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

SCENARIO("External bridge density has compatible defaults and limits", "[Config][BridgeDensity]") {
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    const ConfigOptionDef *definition = print_config_def.get("bridge_density");

    REQUIRE(definition != nullptr);
    REQUIRE(definition->min == Approx(10.0));
    REQUIRE(definition->max == Approx(125.0));

    const ConfigOptionPercent *density = config.option<ConfigOptionPercent>("bridge_density");
    REQUIRE(density != nullptr);
    REQUIRE(density->value == Approx(100.0));
    REQUIRE(density->get_abs_value(1.0) == Approx(1.0));

    config.set_deserialize_strict("bridge_density", "125%");
    REQUIRE(config.option<ConfigOptionPercent>("bridge_density")->get_abs_value(1.0) == Approx(1.25));
    REQUIRE(config.validate().empty());
}

SCENARIO("Internal bridge density has compatible defaults and limits", "[Config][BridgeDensity]") {
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    const ConfigOptionDef *definition = print_config_def.get("internal_bridge_density");

    REQUIRE(definition != nullptr);
    REQUIRE(definition->min == Approx(10.0));
    REQUIRE(definition->max == Approx(125.0));

    const ConfigOptionPercent *density = config.option<ConfigOptionPercent>("internal_bridge_density");
    REQUIRE(density != nullptr);
    REQUIRE(density->value == Approx(100.0));
    REQUIRE(density->get_abs_value(1.0) == Approx(1.0));

    config.set_deserialize_strict("internal_bridge_density", "80%");
    REQUIRE(config.option<ConfigOptionPercent>("internal_bridge_density")->get_abs_value(1.0) == Approx(0.8));
    REQUIRE(config.validate().empty());
}

SCENARIO("Role-specific flow ratios have compatible defaults and limits", "[Config][FlowRatio]") {
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();

    const ConfigOptionBool *enabled = config.option<ConfigOptionBool>("set_other_flow_ratios");
    REQUIRE(enabled != nullptr);
    REQUIRE_FALSE(enabled->value);

    for (const char *key : {"first_layer_flow_ratio", "outer_wall_flow_ratio", "inner_wall_flow_ratio",
                            "overhang_flow_ratio", "sparse_infill_flow_ratio", "internal_solid_infill_flow_ratio",
                            "gap_fill_flow_ratio", "support_flow_ratio", "support_interface_flow_ratio"}) {
        const ConfigOptionDef *definition = print_config_def.get(key);
        REQUIRE(definition != nullptr);
        REQUIRE(definition->min == Approx(0.0));
        REQUIRE(definition->max == Approx(2.0));
        REQUIRE(definition->gui_type == ConfigOptionDef::GUIType::multi_variant);
        REQUIRE(definition->nullable);
        REQUIRE(print_options_with_variant.count(key) == 1);
        REQUIRE(multi_variant_text_ctrl_options.count(key) == 1);

        const ConfigOptionFloatsNullable *ratio = config.option<ConfigOptionFloatsNullable>(key);
        REQUIRE(ratio != nullptr);
        REQUIRE(ratio->size() == 1);
        REQUIRE(ratio->get_at(0) == Approx(1.0));
    }

    config.set_deserialize_strict("set_other_flow_ratios", "1");
    config.set_deserialize_strict("outer_wall_flow_ratio", "0.85");
    REQUIRE(config.option<ConfigOptionBool>("set_other_flow_ratios")->value);
    const ConfigOptionFloatsNullable *legacy_ratio = config.option<ConfigOptionFloatsNullable>("outer_wall_flow_ratio");
    REQUIRE(legacy_ratio->size() == 1);
    REQUIRE(legacy_ratio->get_at(0) == Approx(0.85));

    config.set_deserialize_strict("outer_wall_flow_ratio", "0.85,1.15");
    const ConfigOptionFloatsNullable *dual_nozzle_ratio = config.option<ConfigOptionFloatsNullable>("outer_wall_flow_ratio");
    REQUIRE(dual_nozzle_ratio->size() == 2);
    REQUIRE(dual_nozzle_ratio->get_at(0) == Approx(0.85));
    REQUIRE(dual_nozzle_ratio->get_at(1) == Approx(1.15));
    REQUIRE(config.validate().empty());
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
