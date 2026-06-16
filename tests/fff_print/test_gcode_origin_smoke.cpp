#include <catch2/catch.hpp>

#include "libslic3r/GCode/GCodeOrigin.hpp"

using namespace Slic3r;

SCENARIO("GCode origin smoke covers migrated coordinate origin manipulation", "[GCodeWriter][GCodeOrigin]")
{
    GCodeOriginState origin;

    WHEN("the origin is set directly")
    {
        origin.set_origin(Vec2d(10, 0));

        THEN("the origin reports the assigned coordinates")
        {
            REQUIRE(origin.origin() == Vec2d(10, 0));
        }
    }

    WHEN("the origin is advanced relative to the previous origin")
    {
        origin.set_origin(Vec2d(10, 0));
        origin.set_origin(origin.origin() + Vec2d(5, 5));

        THEN("the origin reports the accumulated coordinates")
        {
            REQUIRE(origin.origin() == Vec2d(15, 5));
        }
    }
}
