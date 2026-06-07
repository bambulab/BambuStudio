#include <catch2/catch.hpp>

#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/libslic3r.h"

#include <algorithm>
#include <cmath>

using namespace Slic3r;

static ExtrusionPath process_core_path(std::initializer_list<Point> points)
{
    ExtrusionPath path { erPerimeter, 1.0, 1.0, 1.0 };
    for (const Point &point : points)
        path.polyline.append(point);
    return path;
}

SCENARIO("Print process core smoke path covers legacy flow math", "[PrintProcessCore]") {
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

SCENARIO("Print process core smoke path covers legacy extrusion entity flattening", "[PrintProcessCore]") {
    GIVEN("an extrusion collection containing a no-sort child collection") {
        ExtrusionPaths no_sort_paths;
        no_sort_paths.push_back(process_core_path({ Point(0, 0), Point(10, 0), Point(10, 10) }));
        no_sort_paths.push_back(process_core_path({ Point(20, 20), Point(30, 20), Point(30, 30) }));

        ExtrusionEntityCollection no_sort_child;
        no_sort_child.no_sort = true;
        no_sort_child.append(no_sort_paths);

        ExtrusionEntityCollection sortable_child;
        sortable_child.no_sort = false;
        sortable_child.append({ process_core_path({ Point(-10, -10), Point(-20, -10), Point(-20, -20) }) });

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
