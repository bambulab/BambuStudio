#include <catch2/catch.hpp>

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Fill/Fill.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Surface.hpp"
#include "libslic3r/libslic3r.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace Slic3r;

namespace {

static ExPolygon expolygon_from_points(const std::vector<Vec2d> &points)
{
    Points scaled;
    scaled.reserve(points.size());
    for (const Vec2d &point : points)
        scaled.push_back(Point::new_scale(point.x(), point.y()));
    return ExPolygon(scaled);
}

static Polylines rectilinear_fill(const ExPolygon &expolygon, double angle, double spacing, float density, bool dont_adjust)
{
    std::unique_ptr<Fill> filler(Fill::new_from_type(ipRectilinear));
    REQUIRE(filler != nullptr);

    FillParams params;
    params.density     = density;
    params.dont_adjust = dont_adjust;

    filler->angle   = static_cast<float>(angle);
    filler->spacing = spacing;

    Surface surface(stTop, expolygon);
    return filler->fill_surface(&surface, params);
}

static bool solid_surface_fully_filled(const ExPolygon &expolygon, double flow_spacing, double angle, float density)
{
    std::unique_ptr<Fill> filler(Fill::new_from_type(ipRectilinear));
    REQUIRE(filler != nullptr);

    filler->set_bounding_box(get_extents(expolygon.contour));
    filler->angle = static_cast<float>(angle);

    const Flow flow(static_cast<float>(flow_spacing), 0.4f, static_cast<float>(flow_spacing));
    filler->spacing = flow.spacing();

    FillParams params;
    params.density     = density;
    params.dont_adjust = false;

    Surface surface(stBottom, expolygon);
    const Polylines paths = filler->fill_surface(&surface, params);

    Polygons grown_paths;
    grown_paths.reserve(paths.size());

    const float line_offset = static_cast<float>(scale_(filler->spacing / 2.0 + EPSILON));
    for (const Polyline &path : paths)
        polygons_append(grown_paths, offset(path, line_offset));

    ExPolygons uncovered = diff_ex(offset(expolygon, -float(0.2 * scale_(flow_spacing))), grown_paths, ApplySafetyOffset::Yes);
    const double scaled_flow_spacing = std::pow(scale_(flow_spacing), 2);
    uncovered.erase(std::remove_if(uncovered.begin(), uncovered.end(), [scaled_flow_spacing](const ExPolygon &poly) {
        return poly.area() < scaled_flow_spacing;
    }), uncovered.end());

    return uncovered.empty();
}

} // namespace

SCENARIO("Fill smoke covers migrated rectilinear path generation basics", "[Fill]") {
    GIVEN("a square top surface") {
        const ExPolygon square = expolygon_from_points({ { 0, 0 }, { 100, 0 }, { 100, 100 }, { 0, 100 } });

        WHEN("rectilinear fill is generated") {
            const Polylines paths = rectilinear_fill(square, -PI / 2.0, 5.0, 0.1f, true);

            THEN("one continuous path is produced with non-trivial length") {
                REQUIRE(paths.size() == 1);
                REQUIRE(paths.front().length() > scale_(50));
            }
        }
    }

    GIVEN("a diamond whose endpoints align with the fill grid") {
        const ExPolygon diamond = expolygon_from_points({ { 0, 0 }, { 100, 0 }, { 150, 50 }, { 100, 100 }, { 0, 100 }, { -50, 50 } });

        WHEN("rectilinear fill is generated") {
            const Polylines paths = rectilinear_fill(diamond, -PI / 2.0, 5.0, 0.1f, true);

            THEN("one continuous path is produced") {
                REQUIRE(paths.size() == 1);
            }
        }
    }
}

SCENARIO("Fill smoke covers migrated rectilinear hole avoidance", "[Fill]") {
    GIVEN("a square top surface with a square hole") {
        Points contour {
            Point::new_scale(0, 0),
            Point::new_scale(100, 0),
            Point::new_scale(100, 100),
            Point::new_scale(0, 100)
        };
        Points hole {
            Point::new_scale(25, 75),
            Point::new_scale(75, 75),
            Point::new_scale(75, 25),
            Point::new_scale(25, 25)
        };
        const ExPolygon expolygon(contour, hole);

        for (double angle : { -PI / 2.0, -PI / 4.0, -PI, PI / 2.0, PI }) {
            for (double spacing_denominator : { 25.0, 5.0, 7.5, 8.5 }) {
                CAPTURE(angle);
                CAPTURE(spacing_denominator);

                WHEN("rectilinear fill is generated with representative angle and density") {
                    const float    density = static_cast<float>(5.0 / spacing_denominator);
                    const Polylines paths  = rectilinear_fill(expolygon, angle, 5.0, density, true);

                    THEN("the fill remains bounded by the surface and avoids the hole") {
                        REQUIRE(paths.size() >= 1);
                        REQUIRE(paths.size() <= 3);
                        REQUIRE(diff_pl(paths, offset(expolygon, float(SCALED_EPSILON * 10))).empty());
                    }
                }
            }
        }
    }
}

SCENARIO("Fill smoke covers migrated missing infill segment regression", "[Fill]") {
    GIVEN("the legacy rotated polygon regression input") {
        const Points points {
            Point(25771516, 14142125),
            Point(14142138, 25771515),
            Point(2512749, 14142131),
            Point(14142125, 2512749)
        };
        const ExPolygon expolygon(points);

        WHEN("rectilinear fill is generated with adjusted spacing") {
            std::unique_ptr<Fill> filler(Fill::new_from_type(ipRectilinear));
            REQUIRE(filler != nullptr);

            FillParams params;
            params.density     = 1.0f;
            params.dont_adjust = false;

            filler->angle    = static_cast<float>(PI / 4.0);
            filler->spacing  = 0.654498;
            filler->layer_id = 66;
            filler->z        = 20.15;

            Surface surface(stTop, expolygon);
            const Polylines paths = filler->fill_surface(&surface, params);

            THEN("the fill remains one continuous non-empty path") {
                REQUIRE(paths.size() == 1);
                REQUIRE(paths.front().length() > scale_(50));
            }
        }
    }
}

SCENARIO("Fill smoke covers migrated rotated square fill", "[Fill]") {
    GIVEN("a square bottom surface with rectilinear fill") {
        const Points    square { Point::new_scale(0, 0), Point::new_scale(50, 0), Point::new_scale(50, 50), Point::new_scale(0, 50) };
        const ExPolygon expolygon(square);

        std::unique_ptr<Fill> filler(Fill::new_from_type(ipRectilinear));
        REQUIRE(filler != nullptr);
        filler->set_bounding_box(get_extents(expolygon.contour));
        filler->angle = 0;
        filler->spacing = Flow(0.69f, 0.4f, 0.50f).spacing();

        FillParams params;
        params.density = 1.0f;

        for (double angle : { 0.0, 45.0 }) {
            CAPTURE(angle);
            Surface surface(stTop, expolygon);
            surface.expolygon.rotate(angle, Point(0, 0));

            WHEN("rectilinear fill is generated for the representative orientation") {
                const Polylines paths = filler->fill_surface(&surface, params);

                THEN("one continuous path is produced") {
                    REQUIRE(paths.size() == 1);
                }
            }
        }
    }
}

SCENARIO("Fill smoke covers migrated solid surface fill representatives", "[Fill]") {
    GIVEN("a narrow rectangular solid surface") {
        const Points points {
            Point::new_scale(0, 0),
            Point::new_scale(98, 0),
            Point::new_scale(98, 10),
            Point::new_scale(0, 10)
        };
        const ExPolygon expolygon(points);

        THEN("the solid surface is filled at the legacy representative angle and density") {
            REQUIRE(solid_surface_fully_filled(expolygon, 0.5, 45.0, 0.99f));
        }
    }
}
