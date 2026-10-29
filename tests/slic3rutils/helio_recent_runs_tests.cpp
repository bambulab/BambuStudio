#include <catch2/catch.hpp>

#include "slic3r/Utils/HelioDragon.hpp"

TEST_CASE("Helio recent run timestamps reject malformed names", "[Helio][RecentRuns]")
{
    using Slic3r::HelioQuery;
    const auto invalid_timestamp = std::chrono::system_clock::time_point{};

    REQUIRE(HelioQuery::parse_timestamp_from_name("BambuSlicer 2026-02-28T12:34:56") != invalid_timestamp);
    REQUIRE(HelioQuery::parse_timestamp_from_name("BambuSlicer 2026-02-29T12:34:56") == invalid_timestamp);
    REQUIRE(HelioQuery::parse_timestamp_from_name("BambuSlicer 2026-13-01T12:34:56") == invalid_timestamp);
    REQUIRE(HelioQuery::parse_timestamp_from_name("BambuSlicer not-a-timestamp") == invalid_timestamp);
    REQUIRE(HelioQuery::parse_timestamp_from_name("") == invalid_timestamp);
}

TEST_CASE("Helio recent run timestamps preserve chronological ordering", "[Helio][RecentRuns]")
{
    using Slic3r::HelioQuery;

    const auto earlier = HelioQuery::parse_timestamp_from_name("BambuSlicer 2026-01-23T07:52:27");
    const auto later = HelioQuery::parse_timestamp_from_name("BambuSlicer 2026-01-24T07:52:27 retry");

    REQUIRE(earlier < later);
}
