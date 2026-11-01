#include <catch2/catch.hpp>

#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleSelector.hpp"

using namespace Slic3r;

static std::pair<std::vector<std::pair<int, int>>, std::vector<bool>> truncated_split_stream()
{
    // One nibble: split-sides=1 (needs children) and then no more bits.
    std::pair<std::vector<std::pair<int, int>>, std::vector<bool>> data;
    data.first.emplace_back(0, 0);
    data.second = {true, false, false, false};
    return data;
}

TEST_CASE("TriangleSelector paint stream must not OOB on truncated 3MF bits", "[TriangleSelector]")
{
    const auto data = truncated_split_stream();
    REQUIRE_NOTHROW(TriangleSelector::has_facets(data, EnforcerBlockerType::ENFORCER));
    REQUIRE(TriangleSelector::has_facets(data, EnforcerBlockerType::ENFORCER) == false);

    TriangleMesh     mesh(its_make_cube(10., 10., 10.));
    TriangleSelector selector(mesh);
    REQUIRE_NOTHROW(selector.deserialize(data, false));
}

static void append_nibble(std::vector<bool> &bits, int n)
{
    for (int i = 0; i < 4; ++i)
        bits.push_back(bool(n & (1 << i)));
}

TEST_CASE("TriangleSelector paint state must not wrap int8_t past ExtruderMax", "[TriangleSelector]")
{
    // Leaf prefix 0b1100, then nine 0b1111 and a 0 terminator: 0 + 15*9 + 3 = 138 > 127.
    std::pair<std::vector<std::pair<int, int>>, std::vector<bool>> data;
    data.first.emplace_back(0, 0);
    append_nibble(data.second, 0b1100);
    for (int i = 0; i < 9; ++i)
        append_nibble(data.second, 0b1111);
    append_nibble(data.second, 0);

    TriangleMesh     mesh(its_make_cube(10., 10., 10.));
    TriangleSelector selector(mesh);
    REQUIRE_NOTHROW(selector.deserialize(data, false));
    REQUIRE(selector.has_facets(EnforcerBlockerType::NONE));
}
