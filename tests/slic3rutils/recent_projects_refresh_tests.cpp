#include <catch2/catch.hpp>

#include "slic3r/GUI/RecentProjectsRefresh.hpp"

#include <climits>

using Slic3r::GUI::RecentProjectsRefresh;

TEST_CASE("Recent project refresh waits until the target is visible", "[RecentProjects]")
{
    RecentProjectsRefresh refresh;
    refresh.request(0);

    REQUIRE_FALSE(refresh.take(false).has_value());
    REQUIRE_FALSE(refresh.take(false).has_value());
    REQUIRE(refresh.take(true) == INT_MAX);
    REQUIRE_FALSE(refresh.take(true).has_value());
}

TEST_CASE("Recent project refresh preserves the widest pending image range", "[RecentProjects]")
{
    RecentProjectsRefresh refresh;
    refresh.request(INT_MAX);
    refresh.request(0);
    refresh.request(-1);

    REQUIRE_FALSE(refresh.take(false).has_value());
    REQUIRE(refresh.take(true) == INT_MAX);
    REQUIRE_FALSE(refresh.take(true).has_value());
}

TEST_CASE("Recent project refresh merges ranges in either order", "[RecentProjects]")
{
    RecentProjectsRefresh refresh;
    refresh.request(-1);
    refresh.request(0);
    refresh.request(12);
    refresh.request(3);

    REQUIRE(refresh.take(true) == 12);
}

TEST_CASE("Recent project refresh can deliver metadata without thumbnails", "[RecentProjects]")
{
    RecentProjectsRefresh refresh;
    refresh.request(-1);

    REQUIRE_FALSE(refresh.take(false).has_value());
    REQUIRE(refresh.take(true) == -1);
}

TEST_CASE("Recent project refresh starts empty and accepts requests after consumption", "[RecentProjects]")
{
    RecentProjectsRefresh refresh;
    REQUIRE_FALSE(refresh.take(true).has_value());

    refresh.request(0);
    REQUIRE(refresh.take(true) == 0);
    refresh.request(-1);
    REQUIRE_FALSE(refresh.take(false).has_value());
    REQUIRE(refresh.take(true) == -1);

    refresh.request(INT_MAX);
    REQUIRE(refresh.take(true) == INT_MAX);
    REQUIRE_FALSE(refresh.take(true).has_value());
}

TEST_CASE("Hidden saves refresh thumbnails after recent projects change order", "[RecentProjects]")
{
    RecentProjectsRefresh refresh;
    refresh.request(INT_MAX);
    REQUIRE(refresh.take(true) == INT_MAX);

    // A is newest when its save requests image zero. B then moves A to index
    // one; the home page must receive both updates when it is shown again.
    refresh.request(0);
    REQUIRE_FALSE(refresh.take(false).has_value());
    refresh.request(0);
    REQUIRE_FALSE(refresh.take(false).has_value());
    REQUIRE(refresh.take(true) == INT_MAX);
    REQUIRE_FALSE(refresh.take(true).has_value());
}
