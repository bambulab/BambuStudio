#include <catch2/catch.hpp>

#include "libslic3r/FilamentPainting.hpp"
#include "libslic3r/GCode/FilamentOrderCacheKey.hpp"
#include <limits>
#include <unordered_map>

using namespace Slic3r;

TEST_CASE("Filament painting preserves every legacy OBJ leaf code", "[filament_capacity]")
{
    const std::vector<std::string> legacy = {
        "", "4", "8", "0C", "1C", "2C", "3C", "4C", "5C", "6C", "7C", "8C", "9C", "AC", "BC", "CC", "DC",
        "EC", "0FC", "1FC", "2FC", "3FC", "4FC", "5FC", "6FC", "7FC", "8FC", "9FC", "AFC", "BFC", "CFC", "DFC", "EFC"
    };
    for (unsigned int id = 0; id < legacy.size(); ++id) {
        CAPTURE(id);
        REQUIRE(encode_filament_id(id) == legacy[id]);
    }
}

TEST_CASE("Filament painting encodes continuation boundaries and rejects unsupported IDs", "[filament_capacity]")
{
    REQUIRE(MAXIMUM_EXTRUDER_NUMBER == 64);
    REQUIRE(MAXIMUM_AMS_SYNC_FILAMENT_NUMBER == 32);
    REQUIRE(encode_filament_id(33) == "0FFC");
    REQUIRE(encode_filament_id(47) == "EFFC");
    REQUIRE(encode_filament_id(48) == "0FFFC");
    REQUIRE(encode_filament_id(62) == "EFFFC");
    REQUIRE(encode_filament_id(63) == "0FFFFC");
    REQUIRE(encode_filament_id(64) == "1FFFFC");
    REQUIRE(encode_filament_id(65).empty());
    REQUIRE(encode_filament_id(255).empty());
    REQUIRE(encode_filament_id(std::numeric_limits<unsigned int>::max()).empty());
}

TEST_CASE("Order cache keeps current next and previous IDs separate across 32", "[filament_capacity]")
{
    // The old 32-bit packing makes these two distinct contexts identical.
    const auto a = make_filament_order_cache_key({0, 32}, {1}, 0, true);
    const auto b = make_filament_order_cache_key({0}, {0, 1}, 0, true);
    REQUIRE(a != b);
    const auto c = make_filament_order_cache_key({0}, {32}, std::nullopt, true);
    const auto d = make_filament_order_cache_key({0}, {}, 0, true);
    REQUIRE(c != d);
    std::unordered_map<FilamentOrderCacheKey, int, FilamentOrderCacheKeyHash> cache;
    cache[a] = 1;
    cache[b] = 2;
    cache[c] = 3;
    cache[d] = 4;
    REQUIRE(cache.size() == 4);
    REQUIRE(cache.at(a) == 1);
    REQUIRE(cache.at(b) == 2);
    REQUIRE(cache.at(c) == 3);
    REQUIRE(cache.at(d) == 4);
}

TEST_CASE("Order cache represents all 64 zero-based IDs in each position", "[filament_capacity]")
{
    std::unordered_map<FilamentOrderCacheKey, unsigned int, FilamentOrderCacheKeyHash> cache;
    for (unsigned int id = 0; id < MAXIMUM_EXTRUDER_NUMBER; ++id) {
        const auto current = make_filament_order_cache_key({id}, {}, std::nullopt, true);
        const auto next = make_filament_order_cache_key({}, {id}, std::nullopt, true);
        const auto previous = make_filament_order_cache_key({}, {}, id, true);
        REQUIRE(current != next);
        REQUIRE(next != previous);
        REQUIRE(previous != current);
        cache[current] = id;
        cache[next] = id;
        cache[previous] = id;
    }
    REQUIRE(cache.size() == 3 * MAXIMUM_EXTRUDER_NUMBER);
    REQUIRE(make_filament_order_cache_key({63, 32, 0}, {}, 63, false) ==
            make_filament_order_cache_key({0, 32, 63, 63}, {63}, 63, false));
    REQUIRE_THROWS_AS(make_filament_order_cache_key({64}, {}, std::nullopt, false), std::out_of_range);
}
