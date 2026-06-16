#include <catch2/catch.hpp>

#include "libslic3r/PrintFilamentMappingRules.hpp"

using namespace Slic3r;

SCENARIO("Filament mapping rules keep dynamic reorder decisions stable", "[ConfigCore][FilamentMappingRules]")
{
    GIVEN("dynamic auto-for-flush mapping on a multi-nozzle print") {
        THEN("dynamic reorder is enabled when used filaments are not mixed") {
            REQUIRE(FilamentMappingRules::is_dynamic_group_reorder_enabled(
                true,
                fmmAutoForFlush,
                2,
                { false, false },
                { 0, 1 }));
        }

        THEN("dynamic reorder is disabled when a used filament is mixed") {
            REQUIRE_FALSE(FilamentMappingRules::is_dynamic_group_reorder_enabled(
                true,
                fmmAutoForFlush,
                2,
                { true, false },
                { 0, 1 }));
        }

        THEN("dynamic reorder is disabled for non-flush modes or single-nozzle prints") {
            REQUIRE_FALSE(FilamentMappingRules::is_dynamic_group_reorder_enabled(
                true,
                fmmManual,
                2,
                { false, false },
                { 0, 1 }));
            REQUIRE_FALSE(FilamentMappingRules::is_dynamic_group_reorder_enabled(
                true,
                fmmAutoForFlush,
                1,
                { false, false },
                { 0, 1 }));
        }
    }
}

SCENARIO("Filament mapping rules expose available map modes by extruder type", "[ConfigCore][FilamentMappingRules]")
{
    GIVEN("matching extruder types") {
        THEN("quality mapping is not offered") {
            REQUIRE(FilamentMappingRules::available_modes_for_extruder_types({ etDirectDrive, etDirectDrive }) ==
                    std::vector<FilamentMapMode>{ fmmAutoForFlush, fmmAutoForMatch, fmmManual });
        }
    }

    GIVEN("different extruder types") {
        THEN("quality mapping is offered before manual mapping") {
            REQUIRE(FilamentMappingRules::available_modes_for_extruder_types({ etDirectDrive, etBowden }) ==
                    std::vector<FilamentMapMode>{ fmmAutoForFlush, fmmAutoForMatch, fmmAutoForQuality, fmmManual });
        }
    }
}
