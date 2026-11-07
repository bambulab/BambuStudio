#include <catch2/catch.hpp>

#include "libslic3r/Arrange.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Geometry/ConvexHull.hpp"

#include <cmath>

using namespace Slic3r;
using namespace Slic3r::arrangement;

namespace {

Polygon circle(double r_mm, int n = 96)
{
    Polygon p;
    for (int i = 0; i < n; ++i) {
        const double a = 2. * M_PI * i / n;
        p.points.emplace_back(scaled(r_mm * std::cos(a)), scaled(r_mm * std::sin(a)));
    }
    return p;
}

Polygon rect(double w_mm, double h_mm)
{
    const coord_t w = scaled(w_mm / 2.), h = scaled(h_mm / 2.);
    return Polygon({{-w, -h}, {w, -h}, {w, h}, {-w, h}});
}

ArrangePolygon item(const ExPolygon &shape)
{
    ArrangePolygon ap;
    ap.poly.contour = Geometry::convex_hull(shape.contour.points);
    ap.footprint    = {shape};
    return ap;
}

Points square_bed(double size_mm)
{
    const coord_t s = scaled(size_mm);
    return {{0, 0}, {s, 0}, {s, s}, {0, s}};
}

ArrangeParams raster_params()
{
    ArrangeParams p;
    p.use_raster_nester = true;
    p.parallel          = true;
    return p;
}

int plates_used(const ArrangePolygons &items)
{
    int n = 0;
    for (const ArrangePolygon &ap : items) n = std::max(n, ap.bed_idx + 1);
    return n;
}

ExPolygons placed(const ArrangePolygon &ap)
{
    ExPolygons s = ap.footprint.empty() ? ExPolygons{ap.poly} : ap.footprint;
    for (ExPolygon &e : s) {
        e.rotate(ap.rotation);
        e.translate(ap.translation.x(), ap.translation.y());
    }
    return s;
}

// Every arranged item lies inside the bed and no two items on the same bed overlap.
void check_valid(const ArrangePolygons &items, const Points &bed)
{
    const Polygons bed_poly{Polygon(bed)};
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].bed_idx < 0) continue;
        REQUIRE(diff_ex(placed(items[i]), bed_poly).empty());
        for (size_t j = i + 1; j < items.size(); ++j) {
            if (items[j].bed_idx != items[i].bed_idx) continue;
            REQUIRE(area(intersection_ex(placed(items[i]), placed(items[j]))) < scaled(0.01) * scaled(0.01));
        }
    }
}

} // namespace

TEST_CASE("Raster arrange nests objects into holes", "[Arrange][Raster]")
{
    // A ring filling most of the bed and a disc which only fits inside the ring.
    ExPolygon ring(circle(60.), circle(50.));
    ring.holes.front().reverse();
    ExPolygon disc(circle(44.));
    const Points bed = square_bed(130.);

    SECTION("holes allowed: one plate")
    {
        ArrangePolygons items{item(ring), item(disc)};
        arrange(items, {}, bed, raster_params());
        CHECK(plates_used(items) == 1);
        check_valid(items, bed);
    }
    SECTION("holes not allowed: two plates")
    {
        ArrangePolygons items{item(ring), item(disc)};
        ArrangeParams   p = raster_params();
        p.allow_nesting_in_holes = false;
        arrange(items, {}, bed, p);
        CHECK(plates_used(items) == 2);
        check_valid(items, bed);
    }
}

TEST_CASE("Raster arrange rotates parts which only fit diagonally", "[Arrange][Raster]")
{
    // 250 mm long bar on a 200 x 200 bed: only fits along the diagonal.
    ArrangePolygons items{item(ExPolygon(rect(250., 10.)))};
    const Points    bed = square_bed(200.);
    arrange(items, {}, bed, raster_params());
    REQUIRE(items.front().bed_idx == 0);
    const double deg = std::fmod(std::abs(items.front().rotation) * 180. / M_PI, 90.);
    CHECK(deg > 20.);
    CHECK(deg < 70.);
    check_valid(items, bed);
}

TEST_CASE("Raster arrange packs four discs into a 2x2 grid", "[Arrange][Raster]")
{
    // Greedy placement drops the third disc into the valley between the first two;
    // only a grid fits all four onto one plate.
    ArrangePolygons items;
    for (int i = 0; i < 4; ++i) items.push_back(item(ExPolygon(circle(62.))));
    const Points bed = square_bed(256.);
    arrange(items, {}, bed, raster_params());
    CHECK(plates_used(items) == 1);
    check_valid(items, bed);
}

TEST_CASE("Raster arrange keeps spacing and avoids fixed objects", "[Arrange][Raster]")
{
    ArrangePolygons items;
    for (int i = 0; i < 30; ++i) {
        // a mix of rectangles and L shapes
        ExPolygon e(rect(20. + (i * 7) % 30, 10. + (i * 13) % 25));
        if (i % 3 == 0) e = diff_ex(ExPolygons{e}, Polygons{rect(10., 10.)}).front();
        items.push_back(item(e));
    }
    // a fixed object covering the left half of the first plate
    ArrangePolygon fixed = item(ExPolygon(rect(120., 250.)));
    fixed.translation    = {scaled(62.), scaled(128.)};
    fixed.bed_idx        = 0;
    const Points bed     = square_bed(256.);
    arrange(items, {fixed}, bed, raster_params());

    for (const ArrangePolygon &ap : items) REQUIRE(ap.bed_idx >= 0);
    check_valid(items, bed);
    for (const ArrangePolygon &ap : items)
        if (ap.bed_idx == 0) CHECK(intersection_ex(placed(ap), placed(fixed)).empty());

    // default separation: at least 1 mm minus the grid quantization between any two objects
    for (size_t i = 0; i < items.size(); ++i)
        for (size_t j = i + 1; j < items.size(); ++j)
            if (items[i].bed_idx == items[j].bed_idx)
                CHECK(intersection_ex(offset_ex(placed(items[i]), scaled(0.5)), offset_ex(placed(items[j]), scaled(0.5))).empty());
}
