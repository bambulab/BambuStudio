#include <catch2/catch.hpp>

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/WaveOverhangs/AndersonsGenerator.hpp"

using namespace Slic3r;

namespace {

// An axis-aligned rectangle in millimetres, as the slicer's scaled coordinates.
ExPolygon rect_mm(double x0, double y0, double x1, double y1)
{
    Polygon p;
    p.points = {
        Point::new_scale(x0, y0),
        Point::new_scale(x1, y0),
        Point::new_scale(x1, y1),
        Point::new_scale(x0, y1),
    };
    return ExPolygon(p);
}

WaveOverhangs::CommonParams default_params()
{
    WaveOverhangs::CommonParams params;
    params.perimeter_count        = 1;
    params.additional_shell_count = 0;
    params.line_spacing           = 0.35;
    params.line_width             = 0.4;
    params.overhang_flow          = Flow(0.4f, 0.2f, 0.4f);
    params.scaled_resolution      = scaled<double>(0.0125);
    params.spacing_mode           = WaveOverhangs::SpacingMode::Uniform;
    params.seam_mode              = WaveOverhangs::SeamMode::Alternating;
    params.min_length_mm          = 0.0;
    params.max_iterations         = 0;
    params.perimeter_overlap      = 0.1;
    params.minimum_wave_width     = 0.7;
    params.pattern                = WaveOverhangPattern::Smart;
    params.min_new_area           = 0.01;
    return params;
}

size_t count_paths(const std::vector<ExtrusionPaths> &regions)
{
    size_t n = 0;
    for (const ExtrusionPaths &paths : regions)
        n += paths.size();
    return n;
}

} // namespace

TEST_CASE("WaveOverhangs: an unsupported half produces wave paths", "[WaveOverhangs]")
{
    // A 20x20 island whose lower layer only covers the bottom half, so the top half
    // hangs in air and is what the wave has to cover.
    const ExPolygons island{ rect_mm(0, 0, 20, 20) };
    const Polygons   lower = to_polygons(ExPolygons{ rect_mm(0, 0, 20, 10) });

    WaveOverhangs::AndersonsGenerator gen;
    WaveOverhangs::GenerateResult     res = gen.generate(island, lower, default_params());

    REQUIRE(count_paths(res.paths) > 0);

    SECTION("every produced path is tagged as a wave path") {
        for (const ExtrusionPaths &paths : res.paths)
            for (const ExtrusionPath &p : paths)
                CHECK(p.wave_overhang);
    }

    SECTION("every produced path has at least two points") {
        for (const ExtrusionPaths &paths : res.paths)
            for (const ExtrusionPath &p : paths)
                CHECK(p.polyline.points.size() >= 2);
    }

    SECTION("the covered area is reported and stays within the island") {
        REQUIRE_FALSE(res.residual.empty());
        // The covered area is the wave lines' footprint, so it legitimately reaches up to
        // half an extrusion width past the island outline. Allow a full line width of
        // slack: what matters is that the wave is not claiming unrelated territory.
        const Polygons island_grown = expand(to_polygons(island), scaled<float>(0.4));
        CHECK(diff(res.residual, island_grown).empty());
    }
}

TEST_CASE("WaveOverhangs: a fully supported island produces nothing", "[WaveOverhangs]")
{
    // Same island, but the layer below covers all of it: there is no overhang to wave over.
    const ExPolygons island{ rect_mm(0, 0, 20, 20) };
    const Polygons   lower = to_polygons(ExPolygons{ rect_mm(-1, -1, 21, 21) });

    WaveOverhangs::AndersonsGenerator gen;
    WaveOverhangs::GenerateResult     res = gen.generate(island, lower, default_params());

    CHECK(count_paths(res.paths) == 0);
}

TEST_CASE("WaveOverhangs: empty input is handled", "[WaveOverhangs]")
{
    WaveOverhangs::AndersonsGenerator gen;
    WaveOverhangs::GenerateResult     res = gen.generate(ExPolygons{}, Polygons{}, default_params());

    CHECK(count_paths(res.paths) == 0);
    CHECK(res.residual.empty());
}

TEST_CASE("WaveOverhangs: min_length_mm rejects short overhangs", "[WaveOverhangs]")
{
    // A sliver of overhang whose contour is far below the threshold.
    const ExPolygons island{ rect_mm(0, 0, 2, 2) };
    const Polygons   lower = to_polygons(ExPolygons{ rect_mm(0, 0, 2, 1.8) });

    WaveOverhangs::CommonParams params = default_params();
    params.min_length_mm = 1000.0;   // nothing in this island can be that long

    WaveOverhangs::AndersonsGenerator gen;
    WaveOverhangs::GenerateResult     res = gen.generate(island, lower, params);

    CHECK(count_paths(res.paths) == 0);
}

TEST_CASE("WaveOverhangs: every config key is defined", "[WaveOverhangs]")
{
    // The field list on PrintRegionConfig and the option definitions in
    // PrintConfigDef are maintained separately, so a key can easily exist in one and
    // not the other. A missing definition only shows up as a silent default at runtime.
    const std::vector<std::pair<std::string, ConfigOptionType>> expected{
        {"wave_overhangs", coBool},
        {"wave_overhangs_instead_of_bridges", coBool},
        {"wave_overhang_outer_perimeters", coInt},
        {"wave_overhang_perimeter_overlap", coFloat},
        {"wave_overhang_minimum_width", coFloat},
        {"wave_overhang_pattern", coEnum},
        {"wave_overhang_line_spacing", coFloat},
        {"wave_overhang_flow_mm3_per_mm", coFloat},
        {"wave_overhang_print_speed", coFloat},
        {"wave_overhang_perimeter_speed", coFloat},
        {"wave_overhang_travel_speed", coFloat},
        {"wave_overhang_fan_speed", coInt},
        {"wave_overhang_aux_fan_speed", coInt},
        {"wave_overhang_nozzle_temp", coInt},
        {"wave_overhang_min_wave_time", coFloat},
        {"wave_overhang_min_layer_time", coFloat},
        {"wave_overhang_floor_layers", coInt},
        {"wave_overhang_floor_use_hilbert", coBool},
        {"wave_overhang_floor_hilbert_layers", coInt},
        {"wave_overhang_floor_hilbert_density", coInt},
        {"wave_overhang_floor_print_speed", coFloat},
        {"wave_overhang_floor_perimeter_speed", coFloat},
        {"wave_overhang_floor_speed_ramp", coInt},
        {"wave_overhang_floor_fan_speed", coInt},
        {"wave_overhang_floor_aux_fan_speed", coInt},
        {"wave_overhang_min_angle", coFloat},
        {"wave_overhang_spacing_mode", coEnum},
        {"wave_overhang_seam_mode", coEnum},
        {"wave_overhang_debug_gcode", coBool},
        {"wave_overhang_min_length", coFloat},
        {"wave_overhang_max_iterations", coInt},
        {"wave_overhang_min_new_area", coFloat},
        {"wave_overhang_corner_taper_enable", coBool},
        {"wave_overhang_line_spacing_corner", coFloat},
        {"wave_overhang_corner_taper_distance", coFloat},
        {"wave_overhang_corner_angle_threshold", coFloat},
        {"wave_overhang_end_retract_length", coFloat},
        {"support_remaining_areas_after_wave_overhangs", coBool},
    };

    for (const auto &[key, type] : expected) {
        INFO("config key: " << key);
        const ConfigOptionDef *def = print_config_def.get(key.c_str());
        REQUIRE(def != nullptr);
        CHECK(def->type == type);
        CHECK(def->default_value.get() != nullptr);
    }
}

TEST_CASE("WaveOverhangs: the feature is off by default", "[WaveOverhangs]")
{
    // Everything in this feature hangs off wave_overhangs. If that ever defaults to true,
    // every existing profile silently changes behaviour.
    const ConfigOptionDef *def = print_config_def.get("wave_overhangs");
    REQUIRE(def != nullptr);
    REQUIRE(def->default_value.get() != nullptr);
    CHECK(def->default_value->getBool() == false);
}

TEST_CASE("WaveOverhangs: pattern enum round-trips through serialization", "[WaveOverhangs]")
{
    ConfigOptionEnum<WaveOverhangPattern> opt;
    REQUIRE(opt.deserialize("zigzag"));
    CHECK(opt.value == WaveOverhangPattern::ZigZag);
    CHECK(opt.serialize() == "zigzag");

    REQUIRE(opt.deserialize("smart"));
    CHECK(opt.value == WaveOverhangPattern::Smart);

    CHECK_FALSE(opt.deserialize("not-a-pattern"));
}

TEST_CASE("WaveOverhangs: the generator is deterministic", "[WaveOverhangs]")
{
    // Slicing as a whole is not reproducible run to run in this codebase, so pin the
    // generator itself: identical input must give identical output, which keeps the
    // wave stage from being one of the sources of that variation.
    const ExPolygons island{ rect_mm(0, 0, 20, 20) };
    const Polygons   lower = to_polygons(ExPolygons{ rect_mm(0, 0, 20, 10) });

    WaveOverhangs::AndersonsGenerator gen;
    WaveOverhangs::GenerateResult a = gen.generate(island, lower, default_params());
    WaveOverhangs::GenerateResult b = gen.generate(island, lower, default_params());

    REQUIRE(a.paths.size() == b.paths.size());
    REQUIRE(count_paths(a.paths) == count_paths(b.paths));
    for (size_t r = 0; r < a.paths.size(); ++ r) {
        REQUIRE(a.paths[r].size() == b.paths[r].size());
        for (size_t i = 0; i < a.paths[r].size(); ++ i)
            CHECK(a.paths[r][i].polyline.points == b.paths[r][i].polyline.points);
    }
}
