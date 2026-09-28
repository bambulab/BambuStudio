#include <catch2/catch.hpp>

#include <cstdlib>
#include <memory>

#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/libslic3r.h"

#include "test_data.hpp"

using namespace Slic3r;

static inline Slic3r::Point random_point(float LO=-50, float HI=50) 
{
    Vec2f pt = Vec2f(LO, LO) + (Vec2d(rand(), rand()) * (HI-LO) / RAND_MAX).cast<float>();
	return pt.cast<coord_t>();
}

// build a sample extrusion entity collection with random start and end points.
static Slic3r::ExtrusionPath random_path(size_t length = 20, float LO = -50, float HI = 50)
{
    ExtrusionPath t {erPerimeter, 1.0, 1.0, 1.0};
    for (size_t j = 0; j < length; ++ j)
        t.polyline.append(random_point(LO, HI));
    return t;
}

static Slic3r::ExtrusionPaths random_paths(size_t count = 10, size_t length = 20, float LO = -50, float HI = 50)
{
    Slic3r::ExtrusionPaths p;
    for (size_t i = 0; i < count; ++ i)
        p.push_back(random_path(length, LO, HI));
    return p;
}

// ExtrusionEntity and ExtrusionPath both spell out every member in their copy and
// move constructors and in both assignment operators, so a field added to either
// class is silently dropped on copy unless all of them are updated. That failure is
// invisible at compile time and shows up much later as wrong geometry, so pin it here.
static Slic3r::ExtrusionPath tagged_path()
{
    ExtrusionPath p {erPerimeter, 1.0, 1.0, 1.0};
    p.polyline.append(Point(0, 0));
    p.polyline.append(Point::new_scale(10., 0.));
    p.inset_idx                     = 3;
    p.wave_overhang                 = true;
    p.wave_overhang_floor           = true;
    p.wave_overhang_perimeter       = true;
    p.wave_overhang_floor_perimeter = true;
    p.wave_overhang_floor_distance  = 7;
    return p;
}

static void check_tags_survived(const Slic3r::ExtrusionPath &p)
{
    CHECK(p.inset_idx                     == 3);
    CHECK(p.wave_overhang                 == true);
    CHECK(p.wave_overhang_floor           == true);
    CHECK(p.wave_overhang_perimeter       == true);
    CHECK(p.wave_overhang_floor_perimeter == true);
    CHECK(int(p.wave_overhang_floor_distance) == 7);
}

TEST_CASE("ExtrusionPath: wall index and wave-overhang tags survive copying", "[ExtrusionEntity]")
{
    const ExtrusionPath src = tagged_path();

    SECTION("copy constructor") {
        ExtrusionPath copy(src);
        check_tags_survived(copy);
    }

    SECTION("move constructor") {
        ExtrusionPath moved(tagged_path());
        check_tags_survived(moved);
    }

    SECTION("constructor taking a replacement polyline") {
        Polyline pl;
        pl.append(Point(0, 0));
        pl.append(Point::new_scale(5., 0.));
        ExtrusionPath copy(pl, src);
        check_tags_survived(copy);
    }

    SECTION("constructor taking a moved replacement polyline") {
        Polyline pl;
        pl.append(Point(0, 0));
        pl.append(Point::new_scale(5., 0.));
        ExtrusionPath copy(std::move(pl), src);
        check_tags_survived(copy);
    }

    SECTION("copy assignment") {
        ExtrusionPath assigned;
        assigned = src;
        check_tags_survived(assigned);
    }

    SECTION("move assignment") {
        ExtrusionPath assigned;
        assigned = tagged_path();
        check_tags_survived(assigned);
    }

    SECTION("clone() preserves them as well") {
        std::unique_ptr<ExtrusionEntity> cloned(src.clone());
        const ExtrusionPath *as_path = dynamic_cast<const ExtrusionPath *>(cloned.get());
        REQUIRE(as_path != nullptr);
        check_tags_survived(*as_path);
    }
}

TEST_CASE("ExtrusionLoop and ExtrusionMultiPath carry the wall index", "[ExtrusionEntity]")
{
    // inset_idx lives on ExtrusionEntity, so the loop and multipath wrappers inherit
    // it through the base copy. Downstream code reads the wrapper, not the paths.
    ExtrusionPaths paths;
    paths.push_back(tagged_path());

    SECTION("loop") {
        ExtrusionLoop loop(paths);
        loop.inset_idx = 2;
        ExtrusionLoop copy(loop);
        CHECK(copy.inset_idx == 2);
    }

    SECTION("multipath") {
        ExtrusionMultiPath mp(paths);
        mp.inset_idx = 2;
        ExtrusionMultiPath copy(mp);
        CHECK(copy.inset_idx == 2);
    }
}

SCENARIO("ExtrusionEntityCollection: Polygon flattening", "[ExtrusionEntity]") {
    srand(0xDEADBEEF); // consistent seed for test reproducibility.

    // Generate one specific random path set and save it for later comparison
    Slic3r::ExtrusionPaths nosort_path_set = random_paths();

    Slic3r::ExtrusionEntityCollection sub_nosort;
    sub_nosort.append(nosort_path_set);
    sub_nosort.no_sort = true;

    Slic3r::ExtrusionEntityCollection sub_sort;
    sub_sort.no_sort = false;
    sub_sort.append(random_paths());

    GIVEN("A Extrusion Entity Collection with a child that has one child that is marked as no-sort") {
        Slic3r::ExtrusionEntityCollection sample;
        Slic3r::ExtrusionEntityCollection output;

        sample.append(sub_sort);
        sample.append(sub_nosort);
        sample.append(sub_sort);

        WHEN("The EEC is flattened with default options (preserve_order=false)") {
			output = sample.flatten();
            THEN("The output EEC contains no Extrusion Entity Collections") {
                CHECK(std::count_if(output.entities.cbegin(), output.entities.cend(), [=](const ExtrusionEntity* e) {return e->is_collection();}) == 0);
            }
        }
        WHEN("The EEC is flattened with preservation (preserve_order=true)") {
			output = sample.flatten(true);
            THEN("The output EECs contains one EEC.") {
                CHECK(std::count_if(output.entities.cbegin(), output.entities.cend(), [=](const ExtrusionEntity* e) {return e->is_collection();}) == 1);
            }
            AND_THEN("The ordered EEC contains the same order of elements than the original") {
                // find the entity in the collection
                for (auto e : output.entities)
                    if (e->is_collection()) {
                        ExtrusionEntityCollection *temp = dynamic_cast<ExtrusionEntityCollection*>(e);
                        // check each Extrusion path against nosort_path_set to see if the first and last match the same
                        CHECK(nosort_path_set.size() == temp->entities.size());
                        for (size_t i = 0; i < nosort_path_set.size(); ++ i) {
                            CHECK(temp->entities[i]->first_point() == nosort_path_set[i].first_point());
                            CHECK(temp->entities[i]->last_point() == nosort_path_set[i].last_point());
                        }
                    }
            }
        }
    }
}
