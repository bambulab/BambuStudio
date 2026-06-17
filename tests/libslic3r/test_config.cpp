#include <catch2/catch.hpp>

#include "libslic3r/LocalesUtils.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <algorithm>
#include <cereal/archives/binary.hpp>
#include <cereal/types/polymorphic.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include <memory>

using namespace Slic3r;

namespace {

DynamicPrintConfig make_full_print_config()
{
    return DynamicPrintConfig::full_print_config();
}

} // namespace

SCENARIO("Current full print config validates expected wall settings", "[Config][ConfigCore]")
{
    GIVEN("A dynamic config expanded from current defaults") {
        DynamicPrintConfig config = make_full_print_config();

        WHEN("wall_loops is set to a valid positive value") {
            config.set("wall_loops", 3);
            THEN("validation succeeds") {
                REQUIRE(config.validate().empty());
            }
        }

        WHEN("wall_loops is set to a negative value") {
            config.set("wall_loops", -1);
            THEN("validation reports an error") {
                const auto errors = config.validate();
                REQUIRE_FALSE(errors.empty());
                REQUIRE(errors.count("wall_loops") == 1);
            }
        }
    }
}

SCENARIO("Current full print config supports typed config accessors", "[Config][ConfigCore]")
{
    GIVEN("A dynamic config expanded from current defaults") {
        DynamicPrintConfig config = make_full_print_config();

        WHEN("A boolean option is set through the bool interface") {
            REQUIRE_NOTHROW(config.set("spiral_mode", true));
            THEN("the underlying value is updated") {
                REQUIRE(config.opt<ConfigOptionBool>("spiral_mode")->getBool());
            }
        }

        WHEN("A boolean option is deserialized from string") {
            REQUIRE_NOTHROW(config.set_deserialize_strict("spiral_mode", "1"));
            THEN("the underlying value is updated") {
                REQUIRE(config.opt<ConfigOptionBool>("spiral_mode")->getBool());
            }
        }

        WHEN("A boolean option receives an invalid string payload") {
            THEN("a BadOptionTypeException is thrown") {
                REQUIRE_THROWS_AS(config.set("spiral_mode", "Z"), BadOptionTypeException);
            }
            AND_THEN("the default value is unchanged") {
                REQUIRE_FALSE(config.opt<ConfigOptionBool>("spiral_mode")->getBool());
            }
        }

        WHEN("A numeric option is deserialized from string") {
            REQUIRE_NOTHROW(config.set_deserialize_strict("support_filament", "2"));
            THEN("the underlying value is updated") {
                REQUIRE(config.opt<ConfigOptionInt>("support_filament")->value == 2);
            }
        }

        WHEN("A float option is set through the int interface") {
            config.set("outer_wall_line_width", 10);
            THEN("the underlying value is updated") {
                REQUIRE(config.opt<ConfigOptionFloat>("outer_wall_line_width")->getFloat() == 10.0);
            }
        }

        WHEN("A float option is set through the double interface") {
            config.set("outer_wall_line_width", 0.42);
            THEN("the underlying value is updated") {
                REQUIRE(config.opt<ConfigOptionFloat>("outer_wall_line_width")->getFloat() == Approx(0.42));
            }
        }

        WHEN("A float option is set to a non-numeric value") {
            const float before = config.opt<ConfigOptionFloat>("outer_wall_line_width")->getFloat();
            THEN("a BadOptionValueException is thrown") {
                REQUIRE_THROWS_AS(config.set_deserialize_strict("outer_wall_line_width", "zzzz"), BadOptionValueException);
            }
            AND_THEN("the current value is unchanged") {
                REQUIRE(config.opt<ConfigOptionFloat>("outer_wall_line_width")->getFloat() == Approx(before));
            }
        }

        WHEN("A string option is set through the string interface") {
            config.set("machine_end_gcode", "M104 S0");
            THEN("the underlying value is updated") {
                REQUIRE(config.opt<ConfigOptionString>("machine_end_gcode")->value == "M104 S0");
            }
        }

        WHEN("A string option is set through the integer interface") {
            config.set("machine_end_gcode", 100);
            THEN("the underlying value is updated") {
                REQUIRE(config.opt<ConfigOptionString>("machine_end_gcode")->value == "100");
            }
        }

        WHEN("A string option is set through the double interface") {
            config.set("machine_end_gcode", 100.5);
            THEN("the underlying value is updated") {
                REQUIRE(config.opt<ConfigOptionString>("machine_end_gcode")->value == float_to_string_decimal_point(100.5));
            }
        }

        WHEN("A float-or-percent option is set as a percent through the string interface") {
            config.set_deserialize_strict("spiral_mode_max_xy_smoothing", "100%");
            THEN("percent mode is preserved") {
                auto tmp = config.opt<ConfigOptionFloatOrPercent>("spiral_mode_max_xy_smoothing");
                REQUIRE(tmp->percent);
                REQUIRE(tmp->value == 100);
            }
        }

        WHEN("A float-or-percent option is set as a float through the string interface") {
            config.set_deserialize_strict("spiral_mode_max_xy_smoothing", "100");
            THEN("float mode is preserved") {
                auto tmp = config.opt<ConfigOptionFloatOrPercent>("spiral_mode_max_xy_smoothing");
                REQUIRE_FALSE(tmp->percent);
                REQUIRE(tmp->value == 100);
            }
        }

        WHEN("A float-or-percent option is set as a float through the numeric interfaces") {
            config.set("spiral_mode_max_xy_smoothing", 100.5);
            THEN("float mode is preserved") {
                auto tmp = config.opt<ConfigOptionFloatOrPercent>("spiral_mode_max_xy_smoothing");
                REQUIRE_FALSE(tmp->percent);
                REQUIRE(tmp->value == 100.5);
            }
        }

        WHEN("An invalid option is requested during set") {
            THEN("UnknownOptionException is thrown") {
                REQUIRE_THROWS_AS(config.set("deadbeef_invalid_option", 1), UnknownOptionException);
                REQUIRE_THROWS_AS(config.set("deadbeef_invalid_option", 1.0), UnknownOptionException);
                REQUIRE_THROWS_AS(config.set("deadbeef_invalid_option", "1"), UnknownOptionException);
                REQUIRE_THROWS_AS(config.set("deadbeef_invalid_option", true), UnknownOptionException);
            }
        }

        WHEN("An invalid option is requested during get") {
            THEN("UnknownOptionException is thrown") {
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionString>("deadbeef_invalid_option", false), UnknownOptionException);
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionFloat>("deadbeef_invalid_option", false), UnknownOptionException);
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionInt>("deadbeef_invalid_option", false), UnknownOptionException);
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionBool>("deadbeef_invalid_option", false), UnknownOptionException);
            }
        }

        WHEN("Existing getters are called on defaulted options") {
            THEN("current defaults are returned") {
                REQUIRE(config.opt_float("layer_height") > 0.0);
                REQUIRE(config.opt_int("raft_layers") == 0);
                REQUIRE_FALSE(config.opt_bool("spiral_mode"));
            }
        }

        WHEN("getFloat is called on an updated option") {
            config.set("layer_height", 0.5);
            THEN("the updated value is returned") {
                REQUIRE(config.opt_float("layer_height") == 0.5);
            }
        }
    }
}

SCENARIO("Config ini load interface", "[Config][ConfigCore]")
{
    WHEN("new_from_ini is called") {
        DynamicPrintConfig config;
        std::string        path = std::string(TEST_DATA_DIR) + "/test_config/new_from_ini.ini";
        config.load_from_ini(path, ForwardCompatibilitySubstitutionRule::Disable);
        THEN("config object contains ini file options") {
            REQUIRE(config.option_throw<ConfigOptionStrings>("filament_colour", false)->values.size() == 1);
            REQUIRE(config.option_throw<ConfigOptionStrings>("filament_colour", false)->values.front() == "#ABCD");
        }
    }
}

SCENARIO("DynamicPrintConfig serialization", "[Config][ConfigCore]")
{
    WHEN("DynamicPrintConfig is serialized and deserialized") {
        std::unique_ptr<DynamicPrintConfig> cfg_holder(DynamicPrintConfig::new_from_defaults_keys({
            "wall_loops",
            "spiral_mode",
            "machine_end_gcode",
            "outer_wall_line_width"
        }));
        DynamicPrintConfig &cfg = *cfg_holder;
        cfg.set("wall_loops", 3);
        cfg.set("spiral_mode", true);
        cfg.set("machine_end_gcode", "M104 S0");
        cfg.set("outer_wall_line_width", 0.42);

        std::string serialized;
        REQUIRE_NOTHROW(([&]() {
            std::ostringstream          ss;
            cereal::BinaryOutputArchive oarchive(ss);
            oarchive(cfg);
            serialized = ss.str();
        }()));

        THEN("the deserialized object matches the original") {
            DynamicPrintConfig cfg2;
            REQUIRE_FALSE(serialized.empty());
            REQUIRE_NOTHROW(([&]() {
                std::stringstream         ss(serialized);
                cereal::BinaryInputArchive iarchive(ss);
                iarchive(cfg2);
            }()));
            REQUIRE(cfg == cfg2);
            REQUIRE(cfg2.opt<ConfigOptionInt>("wall_loops")->value == 3);
            REQUIRE(cfg2.opt<ConfigOptionBool>("spiral_mode")->getBool());
            REQUIRE(cfg2.opt<ConfigOptionString>("machine_end_gcode")->value == "M104 S0");
            REQUIRE(cfg2.opt<ConfigOptionFloat>("outer_wall_line_width")->getFloat() == Approx(0.42));
        }
    }
}

SCENARIO("normalize_fdm_2 toggles and restores prime tower state for config-core inputs", "[Config][ConfigCore]")
{
    GIVEN("A current full print config with prime tower enabled") {
        DynamicPrintConfig config = make_full_print_config();
        DynamicConfig      ori_values;

        config.set("enable_prime_tower", true);
        config.set("independent_support_layer_height", true);

        WHEN("single-filament conditions disable prime tower") {
            const t_config_option_keys changed = config.normalize_fdm_2(1, 1, &ori_values);

            THEN("prime tower is disabled and the original value is captured") {
                REQUIRE(std::find(changed.begin(), changed.end(), "enable_prime_tower") != changed.end());
                REQUIRE_FALSE(config.opt_bool("enable_prime_tower"));
                REQUIRE(config.opt_bool("independent_support_layer_height"));
                REQUIRE(ori_values.has("enable_prime_tower"));
                REQUIRE(ori_values.opt_bool("enable_prime_tower"));
            }

            AND_WHEN("a smooth timelapse later requires the saved prime tower state to be restored") {
                config.opt<ConfigOptionEnum<TimelapseType>>("timelapse_type")->value = TimelapseType::tlSmooth;

                const t_config_option_keys restored = config.normalize_fdm_2(1, 1, &ori_values);

                THEN("enable_prime_tower is restored from ori_values") {
                    REQUIRE(std::find(restored.begin(), restored.end(), "enable_prime_tower") != restored.end());
                    REQUIRE(config.opt_bool("enable_prime_tower"));
                }
            }
        }

        WHEN("prime tower remains enabled because smooth timelapse is active") {
            config.opt<ConfigOptionEnum<TimelapseType>>("timelapse_type")->value = TimelapseType::tlSmooth;

            const t_config_option_keys changed = config.normalize_fdm_2(1, 1, &ori_values);

            THEN("independent support layer height is turned off") {
                REQUIRE(std::find(changed.begin(), changed.end(), "independent_support_layer_height") != changed.end());
                REQUIRE(config.opt_bool("enable_prime_tower"));
                REQUIRE_FALSE(config.opt_bool("independent_support_layer_height"));
            }
        }
    }
}
