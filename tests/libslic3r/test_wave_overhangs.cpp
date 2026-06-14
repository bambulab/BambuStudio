#include <catch2/catch.hpp>

#include "libslic3r/Flow.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/WaveOverhangs/WaveOverhangs.hpp"

using namespace Slic3r;

namespace {

Polygon rect(double min_x, double min_y, double max_x, double max_y)
{
    return Polygon{
        Point::new_scale(min_x, min_y),
        Point::new_scale(max_x, min_y),
        Point::new_scale(max_x, max_y),
        Point::new_scale(min_x, max_y),
    };
}

size_t path_count(const std::vector<ExtrusionPaths> &regions)
{
    size_t count = 0;
    for (const ExtrusionPaths &paths : regions)
        count += paths.size();
    return count;
}

const std::vector<std::string> &wave_overhang_option_keys()
{
    static const std::vector<std::string> keys = {
        "wave_overhangs",
        "wave_overhangs_instead_of_bridges",
        "wave_overhang_outer_perimeters",
        "wave_overhang_perimeter_overlap",
        "wave_overhang_minimum_width",
        "wave_overhang_pattern",
        "wave_overhang_line_spacing",
        "wave_overhang_flow_mm3_per_mm",
        "wave_overhang_print_speed",
        "wave_overhang_debug_gcode",
        "wave_overhang_min_length",
        "wave_overhang_max_iterations",
        "wave_overhang_min_new_area",
        "wave_overhang_corner_taper_enable",
        "wave_overhang_line_spacing_corner",
        "wave_overhang_corner_taper_distance",
        "wave_overhang_corner_angle_threshold",
    };
    return keys;
}

} // namespace

TEST_CASE("Wave overhang options can be materialized for older presets", "[WaveOverhangs]")
{
    DynamicPrintConfig config;
    REQUIRE_FALSE(config.has("wave_overhangs"));

    for (const std::string &key : wave_overhang_option_keys())
        REQUIRE(config.option(key, true) != nullptr);

    CHECK_FALSE(config.opt_bool("wave_overhangs"));
    CHECK(config.opt_enum<WaveOverhangPattern>("wave_overhang_pattern") == WaveOverhangPattern::Smart);
    CHECK(config.opt_float("wave_overhang_line_spacing") == Approx(0.35));
    CHECK(config.opt_float("wave_overhang_print_speed") == Approx(2.0));
}

TEST_CASE("Wave overhang generator creates tagged paths for unsupported regions", "[WaveOverhangs]")
{
    const ExPolygons infill_area{ ExPolygon{ rect(0, 0, 20, 10) } };
    const Polygons lower_support{ rect(-5, -1, 5, 11) };
    const Flow overhang_flow(0.4f, 0.2f, 0.4f);

    auto [wave_regions, filled_area] = WaveOverhangs::generate(
        infill_area,
        lower_support,
        2,
        0,
        0.1,
        0.7,
        WaveOverhangPattern::Smart,
        0.35,
        0.4,
        overhang_flow,
        scale_(0.05),
        50,
        0.001,
        true);

    REQUIRE(path_count(wave_regions) > 0);
    CHECK(!filled_area.empty());

    for (const ExtrusionPaths &paths : wave_regions) {
        for (const ExtrusionPath &path : paths) {
            CHECK(path.is_wave_overhang());
            CHECK(path.role() == erOverhangPerimeter);
            CHECK(path.mm3_per_mm > 0.);
        }
    }
}

TEST_CASE("Wave overhang path marker survives copies and prevents merging", "[WaveOverhangs]")
{
    ExtrusionPath wave(erOverhangPerimeter, 0.15, 0.4f, 0.2f);
    wave.set_wave_overhang(true);

    ExtrusionPath copied(wave);
    CHECK(copied.is_wave_overhang());

    ExtrusionPath regular(erOverhangPerimeter, 0.15, 0.4f, 0.2f);
    CHECK_FALSE(wave.can_merge(regular));
}
