#include <catch2/catch.hpp>

#include "libslic3r/Arachne/SkeletalTrapezoidation.hpp"

using namespace Slic3r;
using namespace Slic3r::Arachne;

namespace {

struct InterpolationProbe : SkeletalTrapezoidation {
    using SkeletalTrapezoidation::interpolate;
};

} // namespace

TEST_CASE("Arachne beading interpolation handles fewer insets on the thicker side", "[Arachne][Regression]")
{
    using Beading = BeadingStrategy::Beading;

    const coord_t width = scaled<coord_t>(0.42);

    Beading left;
    left.total_thickness    = scaled<coord_t>(1.0);
    left.bead_widths        = {width, width, width, width};
    left.toolpath_locations = {scaled<coord_t>(0.1), scaled<coord_t>(0.3), scaled<coord_t>(0.5), scaled<coord_t>(0.7)};
    left.left_over          = 0;

    Beading right;
    right.total_thickness    = scaled<coord_t>(2.0);
    right.bead_widths        = {width, width};
    right.toolpath_locations = {scaled<coord_t>(0.1), scaled<coord_t>(0.3)};
    right.left_over          = 0;

    const coord_t switching_radius = scaled<coord_t>(0.6);

    Beading result;
    REQUIRE_NOTHROW(result = InterpolationProbe::interpolate(left, 0.5, right, switching_radius));

    const Beading expected = InterpolationProbe::interpolate(left, 0.5, right);
    REQUIRE(result.toolpath_locations == expected.toolpath_locations);
    REQUIRE(result.bead_widths == expected.bead_widths);
}
