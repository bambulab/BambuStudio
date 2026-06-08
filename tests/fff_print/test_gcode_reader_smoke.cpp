#include <catch2/catch.hpp>

#include "libslic3r/GCodeReader.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

using namespace Slic3r;

SCENARIO("GCodeReader smoke covers migrated export parser state checks", "[GCodeReader]")
{
    const std::string gcode =
        "G1 X0 Y0 Z0.2 F1200\n"
        "G1 X10 Y0 E0.5 ; perimeter\n"
        "G1 X10 Y10 E0.8 ; infill\n"
        "G1 Z20.0\n"
        "G1 X12 Y12 ; travel\n";

    GCodeReader reader;
    double max_z = 0.0;
    size_t extruding_moves = 0;
    size_t travel_moves = 0;
    bool saw_perimeter_comment = false;
    bool saw_infill_comment = false;

    reader.parse_buffer(gcode, [&](GCodeReader& self, const GCodeReader::GCodeLine& line) {
        max_z = std::max<double>(max_z, static_cast<double>(line.new_Z(self)));
        if (line.extruding(self) && line.dist_XY(self) > 0.0f)
            ++extruding_moves;
        if (line.travel())
            ++travel_moves;
        saw_perimeter_comment |= line.comment().find("perimeter") != std::string_view::npos;
        saw_infill_comment |= line.comment().find("infill") != std::string_view::npos;
    });

    REQUIRE(max_z == Approx(20.0));
    REQUIRE(extruding_moves == 2);
    REQUIRE(travel_moves == 3);
    REQUIRE(saw_perimeter_comment);
    REQUIRE(saw_infill_comment);
}

SCENARIO("GCodeReader smoke covers migrated object reset and tool-selection parsing", "[GCodeReader]")
{
    const std::string gcode =
        "T0\n"
        "G1 Z0.3\n"
        "G1 Z20.1\n"
        "T1\n"
        "G1 Z0.3\n";

    GCodeReader reader;
    std::vector<int> tools;
    std::vector<double> z_values;

    reader.parse_buffer(gcode, [&](GCodeReader&, const GCodeReader::GCodeLine& line) {
        if (!line.cmd().empty() && line.cmd().front() == 'T')
            tools.emplace_back(std::stoi(std::string(line.cmd().substr(1))));
        if (line.has_z())
            z_values.emplace_back(line.z());
    });

    REQUIRE(tools == std::vector<int>{0, 1});
    REQUIRE(z_values.size() == 3);
    REQUIRE(z_values[0] == Approx(0.3));
    REQUIRE(z_values[1] == Approx(20.1));
    REQUIRE(z_values[2] == Approx(0.3));
    REQUIRE(z_values[1] > z_values[2]);
}

SCENARIO("GCodeReader smoke covers migrated complete-object Z reset ordering", "[GCodeReader]")
{
    const std::string gcode =
        "G1 Z0.3\n"
        "G1 Z10.0\n"
        "G1 Z20.1\n"
        "G1 X0 Y0 ; between-object-gcode\n"
        "G1 Z0.3\n"
        "G1 Z20.0\n";

    GCodeReader reader;
    double max_z = 0.0;
    bool reset_after_tall_object = false;
    bool saw_between_object_marker = false;

    reader.parse_buffer(gcode, [&](GCodeReader&, const GCodeReader::GCodeLine& line) {
        saw_between_object_marker |= line.comment().find("between-object-gcode") != std::string_view::npos;

        if (!line.has_z())
            return;

        const double z = static_cast<double>(line.z());
        if (max_z > 0.0 && std::abs(z - 0.3) < 0.01)
            reset_after_tall_object = max_z > 20.0;
        else
            max_z = std::max(max_z, z);
    });

    REQUIRE(saw_between_object_marker);
    REQUIRE(reset_after_tall_object);
    REQUIRE(max_z == Approx(20.1));
}
