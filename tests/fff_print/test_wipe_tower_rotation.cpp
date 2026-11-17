#include <catch2/catch.hpp>

#include "libslic3r/Print.hpp"
#include "libslic3r/GCode/ConflictChecker.hpp"

#include <cmath>

using namespace Slic3r;

namespace {
Point mm_point(double x, double y) { return Point(scale_(x), scale_(y)); }

FakeWipeTower make_tower(float angle, Vec2f position = Vec2f(138.f, 12.f),
                        Vec2f rib_offset = Vec2f(0.f, 0.f), Vec2d plate_origin = Vec2d(0., 0.))
{
    FakeWipeTower tower;
    tower.rib_offset = rib_offset;
    tower.set_fake_extrusion_data(position + rib_offset, 15.f, .2f, .2f, 75.283f, 8.f,
                                 plate_origin, angle);
    Polyline wall;
    wall.points = {mm_point(0, 0), mm_point(15, 0), mm_point(15, 75.283), mm_point(0, 75.283), mm_point(0, 0)};
    tower.outer_wall.emplace(.2f, Polylines{wall});
    return tower;
}

void require_mm(const Point &point, double x, double y)
{
    REQUIRE(unscale<double>(point.x()) == Approx(x).margin(.000003));
    REQUIRE(unscale<double>(point.y()) == Approx(y).margin(.000003));
}
} // namespace

TEST_CASE("Wipe tower checker uses the export transform including rib and plate offsets", "[WipeTowerRotation]")
{
    const float angle = GENERATE(0.f, 90.f, 37.f);
    // Rib offset is added AFTER local rotation, as in append_tcr, and plate
    // origin is added exactly once by LinesBucket rather than by the tower.
    FakeWipeTower tower = make_tower(angle, Vec2f(138.f, 12.f), Vec2f(2.f, -3.f),
                                    Vec2d(scale_(307.2), scale_(17.5)));
    ExtrusionLayers layers = tower.getTrueExtrusionLayersFromWipeTower();
    REQUIRE(layers.size() == 1);
    REQUIRE(layers.front().paths.size() == 1);
    const double radians = double(angle) * 3.14159265358979323846 / 180.;
    const double expected_x = 140. + 15. * std::cos(radians);
    const double expected_y = 9. + 15. * std::sin(radians);
    require_mm(layers.front().paths.front().polyline.points[1], expected_x, expected_y);

    LinesBucket bucket(std::move(layers), &tower, Point(tower.plate_origin.x(), tower.plate_origin.y()));
    LineWithIDs lines = bucket.curLines();
    REQUIRE(lines.size() == 4);
    require_mm(lines.front()._line.b, expected_x + 307.2, expected_y + 17.5);
    // Transforming a cached copy must not rotate the source walls in place.
    require_mm(tower.outer_wall.begin()->second.front().points[1], 15., 0.);
}

TEST_CASE("Cached wipe tower walls use a refreshed position and angle", "[WipeTowerRotation]")
{
    FakeWipeTower tower = make_tower(0.f, Vec2f(138.f, 12.f), Vec2f(2.f, -3.f));
    tower.set_pos(Vec2f(240.f, 10.f), 90.f);
    require_mm(tower.getTrueExtrusionLayersFromWipeTower().front().paths.front().polyline.points[1], 242., 22.);
    tower.set_pos(Vec2f(140.f, 20.f), 0.f);
    require_mm(tower.getTrueExtrusionLayersFromWipeTower().front().paths.front().polyline.points[1], 157., 17.);
}

TEST_CASE("Rotated tower avoids false conflicts and still detects real crossings", "[WipeTowerRotation]")
{
    FakeWipeTower tower = make_tower(90.f);
    LinesBucket bucket(tower.getTrueExtrusionLayersFromWipeTower(), &tower, Point(0, 0));
    const LineWithIDs tower_lines = bucket.curLines();
    int model_identity = 0;

    SECTION("The reproduced front wall is clear of the emitted rotated tower") {
        LineWithIDs lines = tower_lines;
        lines.emplace_back(Line(mm_point(36.772, 44.789), mm_point(239.228, 44.789)),
                           &model_identity, ExtrusionRole::erPerimeter);
        REQUIRE_FALSE(ConflictChecker::find_inter_of_lines(lines).has_value());
    }
    SECTION("A wall crossing the actual rotated footprint is rejected") {
        LineWithIDs lines = tower_lines;
        lines.emplace_back(Line(mm_point(100., 5.), mm_point(100., 35.)),
                           &model_identity, ExtrusionRole::erPerimeter);
        REQUIRE(ConflictChecker::find_inter_of_lines(lines).has_value());
    }
}
