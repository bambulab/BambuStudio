#include <catch2/catch.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/GCodeReader.hpp"

#include "test_data.hpp"

#include <algorithm>
#include <boost/regex.hpp>

using namespace Slic3r;
using namespace Slic3r::Test;

boost::regex perimeters_regex("G1 X[-0-9.]* Y[-0-9.]* E[-0-9.]* ; perimeter");
boost::regex infill_regex("G1 X[-0-9.]* Y[-0-9.]* E[-0-9.]* ; infill");
boost::regex skirt_regex("G1 X[-0-9.]* Y[-0-9.]* E[-0-9.]* ; skirt");

SCENARIO( "PrintGCode basic functionality", "[PrintGCode]") {
    GIVEN("A default configuration and a print test object") {
        WHEN("the output is executed with no support material") {
            Slic3r::Print print;
            Slic3r::Model model;
            Slic3r::Test::init_print({TestMesh::cube_20x20x20}, print, model, {
                { "layer_height",					0.2 },
                { "first_layer_height",				0.2 },
                { "first_layer_extrusion_width",	0 },
                { "gcode_comments",					true },
                { "start_gcode",					"" }
                });
            std::string gcode = Slic3r::Test::gcode(print);
            THEN("Some text output is generated.") {
                REQUIRE(gcode.size() > 0);
            }
            THEN("Exported text contains slic3r version") {
                REQUIRE(gcode.find(SLIC3R_VERSION) != std::string::npos);
            }
            //THEN("Exported text contains git commit id") {
            //    REQUIRE(gcode.find("; Git Commit") != std::string::npos);
            //    REQUIRE(gcode.find(SLIC3R_BUILD_ID) != std::string::npos);
            //}
            THEN("Exported text contains extrusion statistics.") {
                REQUIRE(gcode.find("; external perimeters extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; perimeters extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; infill extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; solid infill extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; top infill extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; support material extrusion width") == std::string::npos);
                REQUIRE(gcode.find("; first layer extrusion width") == std::string::npos);
            }
            THEN("Exported text does not contain cooling markers (they were consumed)") {
                REQUIRE(gcode.find(";_EXTRUDE_SET_SPEED") == std::string::npos);
            }

            THEN("GCode preamble is emitted.") {
                REQUIRE(gcode.find("G21 ; set units to millimeters") != std::string::npos);
            }

            THEN("Config options emitted for print config, default region config, default object config") {
                REQUIRE(gcode.find("; first_layer_temperature") != std::string::npos);
                REQUIRE(gcode.find("; layer_height") != std::string::npos);
                REQUIRE(gcode.find("; fill_density") != std::string::npos);
            }
            THEN("Infill is emitted.") {
                boost::smatch has_match;
                REQUIRE(boost::regex_search(gcode, has_match, infill_regex));
            }
            THEN("Perimeters are emitted.") {
				boost::smatch has_match;
                REQUIRE(boost::regex_search(gcode, has_match, perimeters_regex));
            }
            THEN("Skirt is emitted.") {
                boost::smatch has_match;
                REQUIRE(boost::regex_search(gcode, has_match, skirt_regex));
            }
            THEN("final Z height is 20mm") {
                double final_z = 0.0;
                GCodeReader reader;
                reader.apply_config(print.config());
                reader.parse_buffer(gcode, [&final_z] (GCodeReader& self, const GCodeReader::GCodeLine& line) {
                    final_z = std::max<double>(final_z, static_cast<double>(self.z())); // record the highest Z point we reach
                });
                REQUIRE(final_z == Approx(20.));
            }
        }
        WHEN("output is executed with complete objects and two differently-sized meshes") {
            Slic3r::Print print;
            Slic3r::Model model;
            Slic3r::Test::init_print({TestMesh::cube_20x20x20,TestMesh::cube_20x20x20}, print, model, {
                { "first_layer_extrusion_width",    0 },
                { "first_layer_height",             0.3 },
                { "layer_height",                   0.2 },
                { "support_material",               false },
                { "raft_layers",                    0 },
                { "complete_objects",               true },
                { "gcode_comments",                 true },
                { "between_objects_gcode",          "; between-object-gcode" }
                });
            std::string gcode = Slic3r::Test::gcode(print);
            THEN("Some text output is generated.") {
                REQUIRE(gcode.size() > 0);
            }
            THEN("Infill is emitted.") {
                boost::smatch has_match;
                REQUIRE(boost::regex_search(gcode, has_match, infill_regex));
            }
            THEN("Perimeters are emitted.") {
                boost::smatch has_match;
                REQUIRE(boost::regex_search(gcode, has_match, perimeters_regex));
            }
            THEN("Skirt is emitted.") {
                boost::smatch has_match;
                REQUIRE(boost::regex_search(gcode, has_match, skirt_regex));
            }
            THEN("Between-object-gcode is emitted.") {
                REQUIRE(gcode.find("; between-object-gcode") != std::string::npos);
            }
            THEN("final Z height is 20.1mm") {
                double final_z = 0.0;
                GCodeReader reader;
                reader.apply_config(print.config());
                reader.parse_buffer(gcode, [&final_z] (GCodeReader& self, const GCodeReader::GCodeLine& line) {
                    final_z = std::max(final_z, static_cast<double>(self.z())); // record the highest Z point we reach
                });
                REQUIRE(final_z == Approx(20.1));
            }
            THEN("Z height resets on object change") {
                double final_z = 0.0;
                bool reset = false;
                GCodeReader reader;
                reader.apply_config(print.config());
                reader.parse_buffer(gcode, [&final_z, &reset] (GCodeReader& self, const GCodeReader::GCodeLine& line) {
                    if (final_z > 0 && std::abs(self.z() - 0.3) < 0.01 ) { // saw higher Z before this, now it's lower
                        reset = true;
                    } else {
                        final_z = std::max(final_z, static_cast<double>(self.z())); // record the highest Z point we reach
                    }
                });
                REQUIRE(reset == true);
            }
            THEN("Shorter object is printed before taller object.") {
                double final_z = 0.0;
                bool reset = false;
                GCodeReader reader;
                reader.apply_config(print.config());
                reader.parse_buffer(gcode, [&final_z, &reset] (GCodeReader& self, const GCodeReader::GCodeLine& line) {
                    if (final_z > 0 && std::abs(self.z() - 0.3) < 0.01 ) { 
                        reset = (final_z > 20.0);
                    } else {
                        final_z = std::max(final_z, static_cast<double>(self.z())); // record the highest Z point we reach
                    }
                });
                REQUIRE(reset == true);
            }
        }
        WHEN("the output is executed with support material") {
            std::string gcode = ::Test::slice({TestMesh::cube_20x20x20}, {
                { "first_layer_extrusion_width",    0 },
                { "support_material",               true },
                { "raft_layers",                    3 },
                { "gcode_comments",                 true }
                });
            THEN("Some text output is generated.") {
                REQUIRE(gcode.size() > 0);
            }
            THEN("Exported text contains extrusion statistics.") {
                REQUIRE(gcode.find("; external perimeters extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; perimeters extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; infill extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; solid infill extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; top infill extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; support material extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; first layer extrusion width") == std::string::npos);
            }
            THEN("Raft is emitted.") {
                REQUIRE(gcode.find("; raft") != std::string::npos);
            }
        }
        WHEN("the output is executed with a separate first layer extrusion width") {
			std::string gcode = ::Test::slice({ TestMesh::cube_20x20x20 }, {
                { "first_layer_extrusion_width", "0.5" }
                });
            THEN("Some text output is generated.") {
                REQUIRE(gcode.size() > 0);
            }
            THEN("Exported text contains extrusion statistics.") {
                REQUIRE(gcode.find("; external perimeters extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; perimeters extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; infill extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; solid infill extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; top infill extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; support material extrusion width") == std::string::npos);
                REQUIRE(gcode.find("; first layer extrusion width") != std::string::npos);
            }
        }
        WHEN("Cooling is enabled and the fan is disabled.") {
			std::string gcode = ::Test::slice({ TestMesh::cube_20x20x20 }, {
				{ "cooling",                    true },
                { "disable_fan_first_layers",   5 }
                });
            THEN("GCode to disable fan is emitted."){
                REQUIRE(gcode.find("M107") != std::string::npos);
            }
        }
        WHEN("end_gcode exists with layer_num and layer_z") {
			std::string gcode = ::Test::slice({ TestMesh::cube_20x20x20 }, {
				{ "end_gcode",              "; Layer_num [layer_num]\n; Layer_z [layer_z]" },
                { "layer_height",           0.1 },
                { "first_layer_height",     0.1 }
                });
            THEN("layer_num and layer_z are processed in the end gcode") {
                REQUIRE(gcode.find("; Layer_num 199") != std::string::npos);
                REQUIRE(gcode.find("; Layer_z 20") != std::string::npos);
            }
        }
        WHEN("current_extruder exists in start_gcode") {
            {
				std::string gcode = ::Test::slice({ TestMesh::cube_20x20x20 }, {
					{ "start_gcode", "; Extruder [current_extruder]" }
                });
                THEN("current_extruder is processed in the start gcode and set for first extruder") {
                    REQUIRE(gcode.find("; Extruder 0") != std::string::npos);
                }
            }
			{
                DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
                config.set_num_extruders(4);
                config.set_deserialize_strict({
                    { "start_gcode",                    "; Extruder [current_extruder]" },
                    { "infill_extruder",                2 },
                    { "solid_infill_extruder",          2 },
                    { "perimeter_extruder",             2 },
                    { "support_material_extruder",      2 },
                    { "support_material_interface_extruder", 2 }
                });
                std::string gcode = Slic3r::Test::slice({TestMesh::cube_20x20x20}, config);
                THEN("current_extruder is processed in the start gcode and set for second extruder") {
                    REQUIRE(gcode.find("; Extruder 1") != std::string::npos);
                }
            }
        }

        WHEN("layer_num represents the layer's index from z=0") {
			std::string gcode = ::Test::slice({ TestMesh::cube_20x20x20, TestMesh::cube_20x20x20 }, {
				{ "complete_objects",               true },
                { "gcode_comments",                 true },
                { "layer_gcode",                    ";Layer:[layer_num] ([layer_z] mm)" },
                { "layer_height",                   0.1 },
                { "first_layer_height",             0.1 }
                });
			// End of the 1st object.
            std::string token = ";Layer:199 ";
			size_t pos = gcode.find(token);
			THEN("First and second object last layer is emitted") {
				// First object
				REQUIRE(pos != std::string::npos);
				pos += token.size();
				REQUIRE(pos < gcode.size());
				double z = 0;
				REQUIRE((sscanf(gcode.data() + pos, "(%lf mm)", &z) == 1));
				REQUIRE(z == Approx(20.));
				// Second object
				pos = gcode.find(";Layer:399 ", pos);
				REQUIRE(pos != std::string::npos);
				pos += token.size();
				REQUIRE(pos < gcode.size());
				REQUIRE((sscanf(gcode.data() + pos, "(%lf mm)", &z) == 1));
				REQUIRE(z == Approx(20.));
			}
        }
    }
}

SCENARIO("H2 AUX filtration override is layered onto generated G-code", "[PrintGCode][AuxFiltration]") {
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set("printer_model", std::string("Bambu Lab H2C"));
    config.set("auxiliary_fan", true);
    config.set("support_cooling_filter", true);
    config.set("enable_auxiliary_fan_filtration", true);
    config.set("auxiliary_fan_filtration_speed", 70);
    config.set("auxiliary_fan_filtration_post_time", 12);
    config.set("layer_change_gcode", std::string("M106 P2 S0 ; layer hook"));
    config.set("machine_start_gcode", std::string(
        "{if auxiliary_fan_filtration_active}\n"
        "M106 P2 S{auxiliary_fan_filtration_speed_num} ; start filter\n"
        "{endif}\n"
        "; MACHINE START END"));
    config.set("machine_end_gcode", std::string(
        "; MACHINE END START\n"
        "{if auxiliary_fan_filtration_active}\n"
        "M106 P2 S{auxiliary_fan_filtration_speed_num} ; final filter\n"
        "{if auxiliary_fan_filtration_post_time > 0}\n"
        "M400 S{auxiliary_fan_filtration_post_time}\n"
        "{endif}\n"
        "M106 P2 S0 ; final filter off\n"
        "{endif}\n"
        "; FINISH SOUND"));

    std::string gcode = Slic3r::Test::slice({TestMesh::cube_20x20x20}, config);

    SECTION("startup and every layer reassert the configured minimum") {
        CHECK(gcode.find("M106 P2 S178 ; start filter") != std::string::npos);
        const std::string layer_hook_token = "M106 P2 S0 ; layer hook";
        size_t layer_hook = gcode.find(layer_hook_token);
        REQUIRE(layer_hook != std::string::npos);
        while (layer_hook != std::string::npos) {
            const size_t next_layer_hook = gcode.find(layer_hook_token, layer_hook + layer_hook_token.size());
            const size_t reassert = gcode.find("M106 P2 S178", layer_hook + layer_hook_token.size());
            REQUIRE(reassert != std::string::npos);
            CHECK((next_layer_hook == std::string::npos || reassert < next_layer_hook));
            layer_hook = next_layer_hook;
        }
    }

    SECTION("the final blocking dwell is inside machine end and finishes by turning P2 off") {
        const size_t machine_end = gcode.find("; MACHINE END START");
        const size_t last_p2_before_machine_end = gcode.rfind("M106 P2 S", machine_end);
        const size_t final_filter = gcode.find("M106 P2 S178 ; final filter", machine_end);
        const size_t dwell = gcode.find("M400 S12", final_filter);
        const size_t fan_off = gcode.find("M106 P2 S0 ; final filter off", dwell);
        const size_t finish_sound = gcode.find("; FINISH SOUND", fan_off);

        REQUIRE(machine_end != std::string::npos);
        REQUIRE(last_p2_before_machine_end != std::string::npos);
        REQUIRE(final_filter != std::string::npos);
        REQUIRE(dwell != std::string::npos);
        REQUIRE(fan_off != std::string::npos);
        REQUIRE(finish_sound != std::string::npos);
        CHECK(gcode.compare(last_p2_before_machine_end, std::string("M106 P2 S0").size(), "M106 P2 S0") != 0);
        CHECK(final_filter > machine_end);
        CHECK(dwell > final_filter);
        CHECK(fan_off > dwell);
        CHECK(finish_sound > fan_off);
    }

    SECTION("a zero post-print time skips the dwell but still turns P2 off") {
        config.set("auxiliary_fan_filtration_post_time", 0);
        gcode = Slic3r::Test::slice({TestMesh::cube_20x20x20}, config);

        const size_t machine_end = gcode.find("; MACHINE END START");
        const size_t final_filter = gcode.find("M106 P2 S178 ; final filter", machine_end);
        const size_t fan_off = gcode.find("M106 P2 S0 ; final filter off", final_filter);

        REQUIRE(machine_end != std::string::npos);
        REQUIRE(final_filter != std::string::npos);
        REQUIRE(fan_off != std::string::npos);
        CHECK(gcode.substr(final_filter, fan_off - final_filter).find("M400 S") == std::string::npos);
    }

    SECTION("a higher stock AUX request is not lowered to the filtration minimum") {
        config.set_key_value("additional_cooling_fan_speed", new ConfigOptionInts { 90 });
        config.set_key_value("close_additional_fan_first_x_layers", new ConfigOptionInts { 0 });
        gcode = Slic3r::Test::slice({TestMesh::cube_20x20x20}, config);

        CHECK(gcode.find("M106 P2 S229") != std::string::npos);
    }
}

SCENARIO("Disabled H2 AUX filtration preserves stock shutdown behavior", "[PrintGCode][AuxFiltration]") {
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set("printer_model", std::string("Bambu Lab H2C"));
    config.set("auxiliary_fan", true);
    config.set("support_cooling_filter", true);
    config.set("enable_auxiliary_fan_filtration", false);
    config.set("machine_start_gcode", std::string(
        "{if auxiliary_fan_filtration_active}\n"
        "M106 P2 S{auxiliary_fan_filtration_speed_num} ; start filter\n"
        "{endif}"));
    config.set("machine_end_gcode", std::string(
        "; MACHINE END START\n"
        "{if auxiliary_fan_filtration_active}\n"
        "M106 P2 S{auxiliary_fan_filtration_speed_num}\n"
        "M400 S{auxiliary_fan_filtration_post_time}\n"
        "M106 P2 S0\n"
        "{endif}"));

    std::string gcode = Slic3r::Test::slice({TestMesh::cube_20x20x20}, config);
    const size_t machine_end = gcode.find("; MACHINE END START");
    const size_t last_p2_before_machine_end = gcode.rfind("M106 P2 S", machine_end);

    REQUIRE(machine_end != std::string::npos);
    REQUIRE(last_p2_before_machine_end != std::string::npos);
    CHECK(gcode.compare(last_p2_before_machine_end, std::string("M106 P2 S0").size(), "M106 P2 S0") == 0);
    CHECK(gcode.find("; start filter") == std::string::npos);
    CHECK(gcode.find("M400 S60", machine_end) == std::string::npos);
}

SCENARIO("Unsupported printers ignore the H2 AUX filtration override", "[PrintGCode][AuxFiltration]") {
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set("printer_model", std::string("Bambu Lab X2D"));
    config.set("auxiliary_fan", true);
    config.set("support_cooling_filter", true);
    config.set("enable_auxiliary_fan_filtration", true);
    config.set("auxiliary_fan_filtration_speed", 70);
    config.set("auxiliary_fan_filtration_post_time", 12);
    config.set("machine_start_gcode", std::string(
        "{if auxiliary_fan_filtration_active}\n"
        "M106 P2 S{auxiliary_fan_filtration_speed_num} ; start filter\n"
        "{endif}"));
    config.set("machine_end_gcode", std::string(
        "; MACHINE END START\n"
        "{if auxiliary_fan_filtration_active}\n"
        "M106 P2 S{auxiliary_fan_filtration_speed_num} ; final filter\n"
        "M400 S{auxiliary_fan_filtration_post_time}\n"
        "M106 P2 S0 ; final filter off\n"
        "{endif}"));

    std::string gcode = Slic3r::Test::slice({TestMesh::cube_20x20x20}, config);
    const size_t machine_end = gcode.find("; MACHINE END START");
    const size_t last_p2_before_machine_end = gcode.rfind("M106 P2 S", machine_end);

    REQUIRE(machine_end != std::string::npos);
    REQUIRE(last_p2_before_machine_end != std::string::npos);
    CHECK(gcode.compare(last_p2_before_machine_end, std::string("M106 P2 S0").size(), "M106 P2 S0") == 0);
    CHECK(gcode.find("; start filter") == std::string::npos);
    CHECK(gcode.find("; final filter") == std::string::npos);
    CHECK(gcode.find("M400 S12", machine_end) == std::string::npos);
    CHECK(gcode.find("M106 P2 S178") == std::string::npos);
}
