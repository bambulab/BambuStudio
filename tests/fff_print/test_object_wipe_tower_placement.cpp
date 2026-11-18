#include <catch2/catch.hpp>

#include "libslic3r/GCode/ObjectWipeTowerPlacement.hpp"
#include "sequential_wipe_tower_test_utils.hpp"

using namespace Slic3r;
namespace ST = Slic3r::Test::SequentialTower;

namespace {

ObjectWipeTowerPlacementParameters parameters()
{
    ObjectWipeTowerPlacementParameters p{};
    p.bed = {ST::rectangle(0., 0., 350., 320.)};
    p.origin = Point(0, 0);
    p.clearance = 8.; // Millimetres; the remaining clearance dimensions are scaled.
    p.object_distance = scaled<float>(4.);
    p.rod_adjustment = scaled(2.);
    p.lid_height = scaled(100.);
    p.rod_height = scaled(40.);
    p.printable_height = scaled(200.);
    return p;
}

ObjectWipeTowerPlacementInstance instance(double x = 100., double y = 100.)
{
    ObjectWipeTowerPlacementInstance i{};
    i.model_hull = ST::rectangle(0., 0., 20., 20.);
    i.shift = Point(scale_(x), scale_(y));
    i.footprint = i.model_hull;
    i.footprint.translate(i.shift);
    i.top = scaled(10.);
    i.tower_hull = ST::rectangle(0., 0., 10., 10.);
    i.tower_height = 10.;
    // No compacted descent, so compacted_config is not needed.
    return i;
}

ObjectWipeTowerPlacementResult place(const std::vector<ObjectWipeTowerPlacementInstance> &instances,
                                     const ObjectWipeTowerPlacementParameters &p)
{
    return place_object_wipe_towers(instances, p, [] {});
}

void require_legal(const ObjectWipeTowerPlacement &placement, const ObjectWipeTowerPlacementInstance &i,
                   const ObjectWipeTowerPlacementParameters &p)
{
    REQUIRE(diff(Polygons{placement.footprint}, p.bed).empty());
    REQUIRE(intersection(Polygons{placement.footprint}, p.excluded).empty());
    REQUIRE(intersection(placement.footprint, i.footprint).empty());
    REQUIRE(placement.height == Approx(i.tower_height));
}

} // namespace

TEST_CASE("Tower search prefers the centred right side when free", "[ObjectWipeTowerPlacement]")
{
    const auto i = instance();
    const auto p = parameters();
    const auto result = place({i}, p);
    REQUIRE(result.unplaced.empty());
    REQUIRE(result.placed.size() == 1);
    REQUIRE(result.placed.front().first == 0);
    const auto &tower = result.placed.front().second;
    REQUIRE(tower.position.x() == Approx(122.));
    REQUIRE(tower.position.y() == Approx(105.));
    require_legal(tower, i, p);
}

TEST_CASE("Tower search avoids an excluded preferred side", "[ObjectWipeTowerPlacement]")
{
    const auto i = instance();
    auto p = parameters();
    p.excluded = {ST::rectangle(121., 80., 160., 140.)};
    const auto result = place({i}, p);
    REQUIRE(result.placed.size() == 1);
    REQUIRE(result.unplaced.empty());
    const auto &tower = result.placed.front().second;
    REQUIRE(tower.position.x() == Approx(88.));
    REQUIRE(tower.position.y() == Approx(105.));
    require_legal(tower, i, p);
}

TEST_CASE("Tower search slides to a corner when centred candidates cannot fit", "[ObjectWipeTowerPlacement]")
{
    const auto i = instance();
    auto p = parameters();
    // Leave 0.001 mm around the diagonal endpoint: separately truncated scaled
    // coordinates differ by one grid unit from the sum of object edge + gap.
    // This is far below the 2 mm slide step, so only the endpoint still fits.
    p.bed = {i.footprint, ST::rectangle(121.999, 121.999, 132.001, 132.001)};
    const auto result = place({i}, p);
    REQUIRE(result.placed.size() == 1);
    REQUIRE(result.unplaced.empty());
    const auto &tower = result.placed.front().second;
    REQUIRE(tower.position.x() == Approx(122.));
    REQUIRE(tower.position.y() == Approx(122.));
    require_legal(tower, i, p);
}

TEST_CASE("Tower search tries an outer ring when the entire inner ring is blocked", "[ObjectWipeTowerPlacement]")
{
    const auto i = instance();
    auto p = parameters();
    p.bed = {i.footprint, ST::rectangle(127., 105., 137., 115.)};
    const auto result = place({i}, p);
    REQUIRE(result.placed.size() == 1);
    REQUIRE(result.unplaced.empty());
    const auto &tower = result.placed.front().second;
    REQUIRE(tower.position.x() == Approx(127.));
    REQUIRE(tower.position.y() == Approx(105.));
    require_legal(tower, i, p);
}

TEST_CASE("Tower search reports only instances needing an unplaceable tower", "[ObjectWipeTowerPlacement]")
{
    const auto i = instance();
    auto mono = instance(220., 220.);
    mono.tower_hull = Polygon{};
    auto p = parameters();
    p.bed = {i.footprint, mono.footprint};
    const auto result = place({i, mono}, p);
    REQUIRE(result.placed.empty());
    REQUIRE(result.unplaced == std::vector<size_t>{0});
}

TEST_CASE("Placed towers keep the configured clearance from one another", "[ObjectWipeTowerPlacement]")
{
    auto a = instance();
    auto b = instance(100., 160.);
    // Both preferred RIGHT positions are statically legal but their tall footprints touch.
    a.tower_hull = b.tower_hull = ST::rectangle(0., 0., 10., 60.);
    const auto p = parameters();
    const auto result = place({a, b}, p);
    REQUIRE(result.placed.size() == 2);
    REQUIRE(result.unplaced.empty());
    REQUIRE(result.placed[0].first == 0);
    REQUIRE(result.placed[1].first == 1);
    // Switching to the left side keeps the same Y centre and resolves the
    // conflict. Check the full preferred positions, not just their Y values.
    const auto &first = result.placed[0].second.position;
    const auto &second = result.placed[1].second.position;
    REQUIRE(first.x() == Approx(122.));
    REQUIRE(first.y() == Approx(80.));
    REQUIRE(second.x() == Approx(88.));
    REQUIRE(second.y() == Approx(140.));
    require_legal(result.placed[0].second, a, p);
    require_legal(result.placed[1].second, b, p);
    const auto swept = offset(result.placed[0].second.footprint, scaled(p.clearance));
    REQUIRE(intersection(swept, Polygons{result.placed[1].second.footprint}).empty());
}

TEST_CASE("Tower search is deterministic for identical input and print order", "[ObjectWipeTowerPlacement]")
{
    auto p = parameters();
    p.excluded = {ST::rectangle(121., 80., 160., 140.)};
    const std::vector<ObjectWipeTowerPlacementInstance> inputs{instance(), instance(220., 220.)};
    const auto first = place(inputs, p);
    REQUIRE(first.placed.size() == 2);
    for (int repeat = 0; repeat < 3; ++repeat) {
        const auto again = place(inputs, p);
        REQUIRE(again.unplaced == first.unplaced);
        REQUIRE(again.placed.size() == first.placed.size());
        for (size_t j = 0; j < first.placed.size(); ++j) {
            REQUIRE(again.placed[j].first == first.placed[j].first);
            REQUIRE(again.placed[j].second.position.x() == first.placed[j].second.position.x());
            REQUIRE(again.placed[j].second.position.y() == first.placed[j].second.position.y());
            REQUIRE(again.placed[j].second.footprint.points == first.placed[j].second.footprint.points);
            REQUIRE(again.placed[j].second.height == first.placed[j].second.height);
        }
    }
}
