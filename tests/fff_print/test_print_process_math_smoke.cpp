#include <catch2/catch.hpp>

#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Fill/FillBase.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/libslic3r.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace Slic3r;

static ExtrusionPath process_math_path(std::initializer_list<Point> points)
{
    ExtrusionPath path { erPerimeter, 1.0, 1.0, 1.0 };
    for (const Point &point : points)
        path.polyline.append(point);
    return path;
}

static std::vector<coord_t> collect_ordered_path_coordinates(const ExtrusionEntityCollection &collection, bool collect_x)
{
    std::vector<coord_t> ordered;
    for (const ExtrusionEntity *entity : collection.entities) {
        const auto *path = dynamic_cast<const ExtrusionPath *>(entity);
        REQUIRE(path != nullptr);
        for (const Point &point : path->polyline.points)
            ordered.push_back(collect_x ? point.x() : point.y());
    }
    return ordered;
}

SCENARIO("Print process math smoke path covers legacy flow math", "[PrintProcessMath]") {
    GIVEN("a 0.4mm nozzle and a 0.4mm layer height") {
        constexpr float nozzle_diameter = 0.4f;
        constexpr float layer_height = 0.4f;

        THEN("auto external perimeter spacing follows current non-bridge flow math") {
            const Flow flow = Flow::new_from_config_width(frExternalPerimeter, ConfigOptionFloatOrPercent(0, false), nozzle_diameter, layer_height);

            REQUIRE(flow.spacing() == Approx(1.125 * nozzle_diameter - layer_height * (1.0 - PI / 4.0)));
        }

        THEN("explicit perimeter width follows current non-bridge flow math") {
            const ConfigOptionFloatOrPercent width(1.0, false);
            const Flow                       flow = Flow::new_from_config_width(frPerimeter, width, nozzle_diameter, layer_height);

            REQUIRE(flow.spacing() == Approx(width.value - layer_height * (1.0 - PI / 4.0)));
        }
    }

    GIVEN("a 0.4mm nozzle and default bridge flow") {
        constexpr float nozzle_diameter = 0.4f;
        constexpr float bridge_flow = 1.0f;

        THEN("bridge width and spacing follow current bridge flow math") {
            const Flow flow = Flow::bridging_flow(nozzle_diameter * std::sqrt(bridge_flow), nozzle_diameter);

            REQUIRE(flow.width() == Approx(nozzle_diameter));
            REQUIRE(flow.spacing() == Approx(nozzle_diameter + BRIDGE_EXTRA_SPACING));
        }
    }

    GIVEN("a 0.25mm nozzle with representative layer heights") {
        constexpr float nozzle_diameter = 0.25f;

        THEN("auto perimeter width stays tied to the nozzle-derived width at low layer height") {
            const Flow flow = Flow::new_from_config_width(frPerimeter, ConfigOptionFloatOrPercent(0, false), nozzle_diameter, 0.15f);

            REQUIRE(flow.width() == Approx(1.125 * nozzle_diameter));
        }

        THEN("auto perimeter width stays tied to the nozzle-derived width at matching layer height") {
            const Flow flow = Flow::new_from_config_width(frPerimeter, ConfigOptionFloatOrPercent(0, false), nozzle_diameter, 0.25f);

            REQUIRE(flow.width() == Approx(1.125 * nozzle_diameter));
        }
    }
}

SCENARIO("Print process math smoke path covers legacy extrusion entity flattening", "[PrintProcessMath]") {
    GIVEN("an extrusion collection containing a no-sort child collection") {
        ExtrusionPaths no_sort_paths;
        no_sort_paths.push_back(process_math_path({ Point(0, 0), Point(10, 0), Point(10, 10) }));
        no_sort_paths.push_back(process_math_path({ Point(20, 20), Point(30, 20), Point(30, 30) }));

        ExtrusionEntityCollection no_sort_child;
        no_sort_child.no_sort = true;
        no_sort_child.append(no_sort_paths);

        ExtrusionEntityCollection sortable_child;
        sortable_child.no_sort = false;
        sortable_child.append({ process_math_path({ Point(-10, -10), Point(-20, -10), Point(-20, -20) }) });

        ExtrusionEntityCollection sample;
        sample.append(sortable_child);
        sample.append(no_sort_child);
        sample.append(sortable_child);

        WHEN("the collection is flattened without preserving order") {
            const ExtrusionEntityCollection output = sample.flatten();

            THEN("the output contains no nested collections") {
                REQUIRE(std::count_if(output.entities.cbegin(), output.entities.cend(), [](const ExtrusionEntity *entity) {
                    return entity->is_collection();
                }) == 0);
            }
        }

        WHEN("the collection is flattened while preserving order") {
            const ExtrusionEntityCollection output = sample.flatten(true);

            THEN("the no-sort child collection is preserved with original path order") {
                const auto nested_count = std::count_if(output.entities.cbegin(), output.entities.cend(), [](const ExtrusionEntity *entity) {
                    return entity->is_collection();
                });
                REQUIRE(nested_count == 1);

                const auto *preserved = dynamic_cast<const ExtrusionEntityCollection *>(
                    *std::find_if(output.entities.cbegin(), output.entities.cend(), [](const ExtrusionEntity *entity) {
                        return entity->is_collection();
                    }));

                REQUIRE(preserved != nullptr);
                REQUIRE(preserved->entities.size() == no_sort_paths.size());
                for (size_t i = 0; i < no_sort_paths.size(); ++i) {
                    REQUIRE(preserved->entities[i]->first_point() == no_sort_paths[i].first_point());
                    REQUIRE(preserved->entities[i]->last_point() == no_sort_paths[i].last_point());
                }
            }
        }
    }
}

SCENARIO("Print process math smoke path covers legacy extrusion entity chaining", "[PrintProcessMath]") {
    GIVEN("two vertical paths where the nearest endpoint should be reversed first") {
        ExtrusionEntityCollection collection;
        collection.append({
            process_math_path({ Point(0, 15), Point(0, 18), Point(0, 20) }),
            process_math_path({ Point(0, 10), Point(0, 8), Point(0, 5) }),
        });

        WHEN("the collection is chained from above the paths") {
            const ExtrusionEntityCollection chained = collection.chained_path_from(Point(0, 30));

            THEN("the path order and directions follow the legacy nearest-travel behavior") {
                REQUIRE(collect_ordered_path_coordinates(chained, false) == std::vector<coord_t>({ 20, 18, 15, 10, 8, 5 }));
            }
        }
    }

    GIVEN("two horizontal paths where the second path is closest to the start point") {
        ExtrusionEntityCollection collection;
        collection.append({
            process_math_path({ Point(4, 0), Point(10, 0), Point(15, 0) }),
            process_math_path({ Point(10, 5), Point(15, 5), Point(20, 5) }),
        });

        WHEN("the collection is chained from the right side") {
            const ExtrusionEntityCollection chained = collection.chained_path_from(Point(30, 0));

            THEN("the nearest path is chosen first and both paths are reversed to minimize travel") {
                REQUIRE(collect_ordered_path_coordinates(chained, true) == std::vector<coord_t>({ 20, 15, 10, 15, 10, 4 }));
            }
        }
    }
}

SCENARIO("Print process math smoke path covers legacy fill spacing adjustment", "[PrintProcessMath]") {
    GIVEN("a solid surface width and requested line distance from the legacy adjusted-distance case") {
        const coord_t surface_width      = scale_(250.0);
        const coord_t requested_distance = scale_(47.0);

        WHEN("solid spacing is adjusted to better cover the width") {
            const coord_t adjusted_distance = Fill::_adjust_solid_spacing(surface_width, requested_distance);

            THEN("the adjusted distance stays close to the legacy 50mm representative and within the 20 percent limit") {
                REQUIRE(adjusted_distance > requested_distance);
                REQUIRE(adjusted_distance == Approx(scale_(50.0)).margin(2.0));
                REQUIRE(adjusted_distance <= static_cast<coord_t>(std::floor(double(requested_distance) * 1.2 + 0.5)));
            }
        }
    }

    GIVEN("a narrow solid surface where adjustment could otherwise collapse spacing") {
        const coord_t surface_width      = scale_(2.0);
        const coord_t requested_distance = scale_(0.414159);

        WHEN("solid spacing is adjusted") {
            const coord_t adjusted_distance = Fill::_adjust_solid_spacing(surface_width, requested_distance);

            THEN("the adjusted distance remains positive and bounded by the same current limit") {
                REQUIRE(adjusted_distance > 0);
                REQUIRE(adjusted_distance >= requested_distance);
                REQUIRE(adjusted_distance <= static_cast<coord_t>(std::floor(double(requested_distance) * 1.2 + 0.5)));
            }
        }
    }
}
