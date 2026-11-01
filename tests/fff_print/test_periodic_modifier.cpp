#include <catch2/catch.hpp>
#include <functional>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Print.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Surface.hpp"

using namespace Slic3r;

static size_t count_perimeter_items(const ExtrusionEntityCollection &coll)
{
    size_t n = 0;
    for (const ExtrusionEntity *entity : coll.flatten().entities)
        if (is_perimeter(entity->role()))
            ++n;
    return n;
}

static size_t layer_perimeter_count(const Layer &layer)
{
    size_t n = 0;
    for (const LayerRegion *region : layer.regions())
        n += count_perimeter_items(region->perimeters);
    return n;
}

static bool layer_has_internal_solid(const Layer &layer)
{
    for (const LayerRegion *region : layer.regions())
        for (const Surface &surface : region->fill_surfaces.surfaces)
            if (surface.surface_type == stInternalSolid)
                return true;
    return false;
}

static void add_periodic_wall_modifier(ModelObject *object, int skip_layers, int apply_layers)
{
    ModelVolume *mod = object->add_volume(make_cube(20., 20., 20.), ModelVolumeType::PARAMETER_MODIFIER);
    mod->config.set("wall_loops", 2);
    mod->config.set("periodic_modifier", true);
    mod->config.set("periodic_modifier_skip_layers", skip_layers);
    mod->config.set("periodic_modifier_apply_layers", apply_layers);
}

static void process_cube_with_modifiers(Print &print, std::function<void(ModelObject *)> add_modifiers,
                                        std::initializer_list<ConfigBase::SetDeserializeItem> items)
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict(items);
    Model model;
    ModelObject *object = model.add_object();
    object->name = "cube.stl";
    object->add_volume(make_cube(20., 20., 20.));
    add_modifiers(object);
    object->add_instance();
    object->ensure_on_bed();
    print.auto_assign_extruders(object);
    print.apply(model, config);
    print.validate();
    print.set_status_silent();
    print.process();
}

TEST_CASE("periodic_modifier_active follows skip/apply pattern", "[PrintRegionConfig]") {
    PrintRegionConfig cfg;
    cfg.periodic_modifier.value = true;
    cfg.periodic_modifier_skip_layers.value = 1;
    cfg.periodic_modifier_apply_layers.value = 1;

    REQUIRE(periodic_modifier_active(cfg, 0));
    REQUIRE(!periodic_modifier_active(cfg, 1));
    REQUIRE(periodic_modifier_active(cfg, 2));

    cfg.periodic_modifier_skip_layers.value = 2;
    REQUIRE(periodic_modifier_active(cfg, 0));
    REQUIRE(!periodic_modifier_active(cfg, 1));
    REQUIRE(!periodic_modifier_active(cfg, 2));
    REQUIRE(periodic_modifier_active(cfg, 3));

    cfg.periodic_modifier_skip_layers.value = 1;
    cfg.periodic_modifier_apply_layers.value = 2;
    REQUIRE(periodic_modifier_active(cfg, 0));
    REQUIRE(periodic_modifier_active(cfg, 1));
    REQUIRE(!periodic_modifier_active(cfg, 2));

    cfg.periodic_modifier.value = false;
    REQUIRE(periodic_modifier_active(cfg, 1));
}

SCENARIO("PrintObject: Periodic modifier on a 20mm cube", "[PrintObject][PeriodicModifier]") {
    auto check_pattern = [](int skip_layers, int apply_layers, const char *wall_generator) {
        Print print;
        process_cube_with_modifiers(print,
            [&](ModelObject *object) { add_periodic_wall_modifier(object, skip_layers, apply_layers); },
            {
                { "wall_loops", 0 },
                { "sparse_infill_density", 0 },
                { "top_shell_layers", 0 },
                { "bottom_shell_layers", 0 },
                { "detect_overhang_wall", 0 },
                { "only_one_wall_first_layer", 0 },
                { "top_one_wall_type", "not apply" },
                { "ensure_vertical_shell_thickness", "enabled" },
                { "layer_height", 0.2 },
                { "wall_generator", wall_generator }
            });
        const PrintObject &object = *print.objects().front();
        REQUIRE(object.layers().size() > 4);
        const int period = apply_layers + skip_layers;
        size_t apply_count = 0;
        size_t skip_count = 0;
        for (const Layer *layer : object.layers()) {
            const int layer_id = static_cast<int>(layer->id());
            const size_t walls = layer_perimeter_count(*layer);
            const bool is_apply = (layer_id % period) < apply_layers;
            if (is_apply) {
                ++apply_count;
                REQUIRE(walls > 0);
            } else {
                ++skip_count;
                REQUIRE(walls == 0);
                REQUIRE_FALSE(layer_has_internal_solid(*layer));
            }
        }
        REQUIRE(apply_count > 0);
        REQUIRE(skip_count > 0);
    };

    GIVEN("Classic wall generator, skip 1 apply 1") {
        WHEN("the 20mm cube is sliced") {
            THEN("the modifier applies every other layer and skip layers are not solid-filled") {
                check_pattern(1, 1, "classic");
            }
        }
    }
    GIVEN("Arachne wall generator, skip 1 apply 1") {
        WHEN("the 20mm cube is sliced") {
            THEN("the periodic modifier pattern is applied") {
                check_pattern(1, 1, "arachne");
            }
        }
    }
    GIVEN("Classic wall generator, skip 2 apply 1") {
        WHEN("the 20mm cube is sliced") {
            THEN("the modifier applies every third layer") {
                check_pattern(2, 1, "classic");
            }
        }
    }
}

SCENARIO("PrintObject: Later non-periodic modifier still applies on skip layers", "[PrintObject][PeriodicModifier]") {
    GIVEN("a periodic wall modifier followed by an inner non-periodic modifier") {
        WHEN("the 20mm cube is sliced") {
            Print print;
            process_cube_with_modifiers(print,
                [](ModelObject *object) {
                    add_periodic_wall_modifier(object, 1, 1);
                    ModelVolume *inner = object->add_volume(make_cube(10., 10., 20.), ModelVolumeType::PARAMETER_MODIFIER);
                    inner->config.set("wall_loops", 1);
                },
                {
                    { "wall_loops", 0 },
                    { "sparse_infill_density", 0 },
                    { "top_shell_layers", 0 },
                    { "bottom_shell_layers", 0 },
                    { "detect_overhang_wall", 0 },
                    { "only_one_wall_first_layer", 0 },
                    { "top_one_wall_type", "not apply" },
                    { "layer_height", 0.2 },
                    { "wall_generator", "classic" }
                });
            const PrintObject &object = *print.objects().front();
            REQUIRE(object.layers().size() > 4);
            size_t skip_layers_with_walls = 0;
            for (const Layer *layer : object.layers()) {
                if ((static_cast<int>(layer->id()) % 2) == 1 && layer_perimeter_count(*layer) > 0)
                    ++skip_layers_with_walls;
            }
            THEN("skip layers of the first modifier still receive the later modifier walls") {
                REQUIRE(skip_layers_with_walls > 0);
            }
        }
    }
}

SCENARIO("PrintObject: Modifier ignore infill keeps parent sparse infill", "[PrintObject][PeriodicModifier]") {
    GIVEN("a wall modifier that zeros infill density") {
        WHEN("modifier_ignore_infill is enabled") {
            Print print;
            process_cube_with_modifiers(print,
                [](ModelObject *object) {
                    ModelVolume *mod = object->add_volume(make_cube(20., 20., 20.), ModelVolumeType::PARAMETER_MODIFIER);
                    mod->config.set("wall_loops", 2);
                    mod->config.set_key_value("sparse_infill_density", new ConfigOptionPercent(0));
                    mod->config.set("modifier_ignore_infill", true);
                },
                {
                    { "wall_loops", 0 },
                    { "sparse_infill_density", 20 },
                    { "sparse_infill_pattern", "zigzag" },
                    { "top_shell_layers", 0 },
                    { "bottom_shell_layers", 0 },
                    { "detect_overhang_wall", 0 },
                    { "only_one_wall_first_layer", 0 },
                    { "top_one_wall_type", "not apply" },
                    { "ensure_vertical_shell_thickness", "disabled" },
                    { "layer_height", 0.2 },
                    { "wall_generator", "classic" }
                });
            const PrintObject &object = *print.objects().front();
            REQUIRE_FALSE(object.layers().empty());
            size_t layers_with_walls = 0;
            size_t layers_with_infill = 0;
            for (const Layer *layer : object.layers()) {
                if (layer_perimeter_count(*layer) > 0)
                    ++layers_with_walls;
                for (const LayerRegion *region : layer->regions()) {
                    if (!region->fills.entities.empty())
                        ++layers_with_infill;
                    REQUIRE(region->region().config().sparse_infill_density > 1.);
                }
            }
            THEN("walls still come from the modifier and infill stays at the parent density") {
                REQUIRE(layers_with_walls > 0);
                REQUIRE(layers_with_infill > 0);
            }
        }
    }
}
