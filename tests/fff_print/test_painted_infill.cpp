// The application provides NanoSVG through BitmapCache; this standalone slicer
// test supplies the implementation without linking the GUI.
#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"

#include <catch2/catch.hpp>
#include "libslic3r/Print.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/TriangleSelector.hpp"

using namespace Slic3r;

namespace {
struct PaintedCube {
    Model model;
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    Print print;
    ModelObject *object;
    ModelVolume *volume;

    PaintedCube()
    {
        config.set_num_filaments(3);
        config.set_key_value("filament_diameter", new ConfigOptionFloats({1.75, 1.75, 1.75}));
        config.set_key_value("filament_colour", new ConfigOptionStrings({"#FFFFFF", "#000000", "#FF0000"}));
        config.set_key_value("filament_type", new ConfigOptionStrings({"PLA", "PLA", "PLA"}));
        config.set_key_value("filament_shrink", new ConfigOptionPercents({100., 100., 100.}));
        config.set_deserialize_strict({{"single_extruder_multi_material", true},
            {"enable_prime_tower", false}, {"sparse_infill_density", "20%"},
            {"sparse_infill_pattern", "grid"}, {"layer_height", 0.2}});
        object = model.add_object();
        volume = object->add_volume(make_cube(12., 12., 4.));
        object->config.set("extruder", 1);
        TriangleSelector selector(volume->mesh());
        for (int i = 0; i < int(volume->mesh().its.indices.size()); ++i)
            selector.set_facet(i, static_cast<EnforcerBlockerType>(2));
        volume->mmu_segmentation_facets.set(selector);
        object->add_instance();
        object->ensure_on_bed();
        print.set_status_silent();
        print.set_no_check_flag(true);
    }

    Print::ApplyStatus apply() { return print.apply(model, config); }

    void check_regions(int infill)
    {
        REQUIRE(print.objects().size() == 1);
        size_t checked = 0;
        for (const auto &range : print.objects().front()->shared_regions()->layer_ranges)
            for (const auto &painted : range.painted_regions) {
                if (painted.extruder_id != 2) continue;
                const auto &cfg = painted.region->config();
                REQUIRE(cfg.wall_filament.value == 2);
                REQUIRE(cfg.solid_infill_filament.value == 2);
                REQUIRE(cfg.sparse_infill_filament.value == infill);
                ++checked;
            }
        REQUIRE(checked > 0);
    }
};
}

TEST_CASE("Painted regions respect explicit sparse infill and retain painted surfaces", "[PaintedInfill]")
{
    PaintedCube cube;
    SECTION("Default follows paint") { cube.apply(); cube.check_regions(2); }
    SECTION("Global choice") {
        cube.config.set_key_value("sparse_infill_filament", new ConfigOptionInt(1));
        cube.apply(); cube.check_regions(1);
    }
    SECTION("Object overrides global choice") {
        cube.config.set_key_value("sparse_infill_filament", new ConfigOptionInt(1));
        cube.object->config.set("sparse_infill_filament", 3);
        cube.apply(); cube.check_regions(3);
    }
    SECTION("Explicit object Default restores painting over global choice") {
        cube.config.set_key_value("sparse_infill_filament", new ConfigOptionInt(1));
        cube.object->config.set("sparse_infill_filament", 0);
        cube.apply(); cube.check_regions(2);
    }
    SECTION("Part Default restores painting over global choice") {
        cube.config.set_key_value("sparse_infill_filament", new ConfigOptionInt(1));
        cube.volume->config.set("sparse_infill_filament", 0);
        cube.apply(); cube.check_regions(2);
    }
    SECTION("Height filament prevents inheriting global feature choice") {
        cube.config.set_key_value("sparse_infill_filament", new ConfigOptionInt(1));
        cube.object->layer_config_ranges[{0., 10.}].set("extruder", 3);
        cube.apply(); cube.check_regions(2);
    }
    SECTION("Modifier inherits global choice") {
        cube.config.set_key_value("sparse_infill_filament", new ConfigOptionInt(1));
        auto *modifier = cube.object->add_volume(make_cube(6., 12., 4.), ModelVolumeType::PARAMETER_MODIFIER);
        modifier->config.set_key_value("sparse_infill_density", new ConfigOptionPercent(30.));
        cube.apply(); cube.check_regions(1);
    }
    SECTION("Object choice") {
        cube.object->config.set("sparse_infill_filament", 1);
        cube.apply(); cube.check_regions(1);
    }
    SECTION("Part overrides object") {
        cube.object->config.set("sparse_infill_filament", 1);
        cube.volume->config.set("sparse_infill_filament", 3);
        cube.apply(); cube.check_regions(3);
    }
    SECTION("Part default overrides object choice") {
        cube.object->config.set("sparse_infill_filament", 1);
        cube.volume->config.set("sparse_infill_filament", 0);
        cube.apply(); cube.check_regions(2);
    }
    SECTION("Layer range overrides object") {
        cube.object->config.set("sparse_infill_filament", 1);
        cube.object->layer_config_ranges[{0., 10.}].set("sparse_infill_filament", 3);
        cube.apply(); cube.check_regions(3);
    }
    SECTION("Default layer range restores painting") {
        cube.object->config.set("sparse_infill_filament", 1);
        cube.object->layer_config_ranges[{0., 10.}].set("sparse_infill_filament", 0);
        cube.apply(); cube.check_regions(2);
    }
    SECTION("Modifier inherits explicit object choice") {
        cube.object->config.set("sparse_infill_filament", 1);
        auto *modifier = cube.object->add_volume(make_cube(6., 12., 4.), ModelVolumeType::PARAMETER_MODIFIER);
        modifier->config.set_key_value("sparse_infill_density", new ConfigOptionPercent(30.));
        cube.apply(); cube.check_regions(1);
    }
}

TEST_CASE("Unpainted sparse infill assignment is unchanged", "[PaintedInfill]")
{
    PaintedCube cube;
    cube.volume->mmu_segmentation_facets.reset();
    cube.object->config.set("sparse_infill_filament", 3);
    cube.apply();
    REQUIRE(cube.print.objects().size() == 1);
    for (const auto &region : cube.print.objects().front()->all_regions()) {
        REQUIRE(region.get().config().sparse_infill_filament.value == 3);
        REQUIRE(region.get().config().wall_filament.value == 1);
    }
}

TEST_CASE("Changing painted sparse infill invalidates and regenerates infill", "[PaintedInfill]")
{
    PaintedCube cube;
    cube.apply();
    cube.print.process();
    cube.check_regions(2);
    const bool global_choice = GENERATE(false, true);
    CAPTURE(global_choice);
    for (int filament : {1, 0, 3, 0}) {
        if (global_choice)
            cube.config.set_key_value("sparse_infill_filament", new ConfigOptionInt(filament));
        else
            cube.object->config.set("sparse_infill_filament", filament);
        REQUIRE(cube.apply() == Print::APPLY_STATUS_INVALIDATED);
        REQUIRE_FALSE(cube.print.objects().front()->is_step_done(posInfill));
        cube.print.process();
        cube.check_regions(filament ? filament : 2);
        size_t infill_paths = 0;
        size_t wall_paths = 0;
        size_t solid_paths = 0;
        for (const Layer *layer : cube.print.objects().front()->layers())
            for (const LayerRegion *region : layer->regions()) {
                for (const ExtrusionEntity *path : region->fills.flatten().entities)
                    if (path->role() == erInternalInfill) {
                        REQUIRE(region->region().extruder(frInfill) == (filament ? filament : 2));
                        ++infill_paths;
                    } else if (is_solid_infill(path->role())) {
                        REQUIRE(region->region().extruder(frSolidInfill) == 2);
                        ++solid_paths;
                    }
                if (!region->perimeters.empty()) {
                    REQUIRE(region->region().config().wall_filament.value == 2);
                    ++wall_paths;
                }
            }
        REQUIRE(infill_paths > 0);
        REQUIRE(wall_paths > 0);
        REQUIRE(solid_paths > 0);
    }
}

TEST_CASE("Multiple painted colors share the explicitly selected sparse infill", "[PaintedInfill]")
{
    PaintedCube cube;
    TriangleSelector selector(cube.volume->mesh());
    for (int i = 0; i < int(cube.volume->mesh().its.indices.size()); ++i)
        selector.set_facet(i, static_cast<EnforcerBlockerType>(i < 6 ? 2 : 3));
    cube.volume->mmu_segmentation_facets.set(selector);
    const bool global_choice = GENERATE(false, true);
    CAPTURE(global_choice);
    if (global_choice)
        cube.config.set_key_value("sparse_infill_filament", new ConfigOptionInt(1));
    else
        cube.object->config.set("sparse_infill_filament", 1);
    cube.apply();
    cube.print.process();
    std::set<unsigned int> wall_filaments;
    size_t infill_paths = 0;
    for (const Layer *layer : cube.print.objects().front()->layers())
        for (const LayerRegion *region : layer->regions()) {
            for (const ExtrusionEntity *path : region->fills.flatten().entities)
                if (path->role() == erInternalInfill) {
                    REQUIRE(region->region().extruder(frInfill) == 1);
                    ++infill_paths;
                }
            if (!region->perimeters.empty())
                wall_filaments.insert(region->region().extruder(frPerimeter));
        }
    REQUIRE(infill_paths > 0);
    const std::set<unsigned int> expected_wall_filaments{2, 3};
    REQUIRE(wall_filaments == expected_wall_filaments);
}

TEST_CASE("Resetting one of two painted objects invalidates the plate", "[PaintedInfill]")
{
    PaintedCube cube;
    cube.object->config.set("sparse_infill_filament", 1);
    cube.object->config.set("solid_infill_filament", 1);
    cube.object->config.set("wall_filament", 1);
    auto *second = cube.model.add_object(*cube.object);
    second->instances.front()->set_offset(Vec3d(20., 0., 0.));
    cube.apply();
    cube.print.process();
    for (auto *object : {cube.object, second}) {
        for (const char *key : {"sparse_infill_filament", "solid_infill_filament", "wall_filament"})
            object->config.set(key, 0);
        REQUIRE(cube.apply() == Print::APPLY_STATUS_INVALIDATED);
        cube.print.process();
    }
}
