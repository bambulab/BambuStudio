#include <catch2/catch.hpp>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#ifdef _WIN32
#include <share.h>
#include <excpt.h>
#endif
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <set>
#include <sstream>
#include <utility>
#include <vector>
#include <boost/filesystem.hpp>

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Fill/Fill.hpp"
#include "libslic3r/Fill/FillBase.hpp"
#include "libslic3r/Fill/FillConformalChart.hpp"
#include "libslic3r/Fill/FillRadialZigZag.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Format/STL.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/SVG.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/miniz_extension.hpp"
#include <nlohmann/json.hpp>

#include "test_data.hpp"

using namespace Slic3r;

bool test_if_solid_surface_filled(const ExPolygon& expolygon, double flow_spacing, double angle = 0, double density = 1.0);

#if 0
TEST_CASE("Fill: adjusted solid distance") {
    int surface_width = 250;
    int distance = Slic3r::Flow::solid_spacing(surface_width, 47);
    REQUIRE(distance == Approx(50));
    REQUIRE(surface_width % distance == 0);
}
#endif

TEST_CASE("Fill: Pattern Path Length", "[Fill]") {
    std::unique_ptr<Slic3r::Fill> filler(Slic3r::Fill::new_from_type("rectilinear"));
    filler->angle = float(-(PI)/2.0);
	FillParams fill_params;
	filler->spacing = 5;
	fill_params.dont_adjust = true;
	//fill_params.endpoints_overlap = false;
	fill_params.density = float(filler->spacing / 50.0);

    auto test = [&filler, &fill_params] (const ExPolygon& poly) -> Slic3r::Polylines {
        Slic3r::Surface surface(stTop, poly);
        return filler->fill_surface(&surface, fill_params);
    };

    SECTION("Square") {
        Slic3r::Points test_set;
        test_set.reserve(4);
        std::vector<Vec2d> points {Vec2d(0,0), Vec2d(100,0), Vec2d(100,100), Vec2d(0,100)};
        for (size_t i = 0; i < 4; ++i) {
            std::transform(points.cbegin()+i, points.cend(),   std::back_inserter(test_set), [] (const Vec2d& a) -> Point { return Point::new_scale(a.x(), a.y()); } ); 
            std::transform(points.cbegin(), points.cbegin()+i, std::back_inserter(test_set), [] (const Vec2d& a) -> Point { return Point::new_scale(a.x(), a.y()); } );
            Slic3r::Polylines paths = test(Slic3r::ExPolygon(test_set));
            REQUIRE(paths.size() == 1); // one continuous path

            // TODO: determine what the "Expected length" should be for rectilinear fill of a 100x100 polygon. 
            // This check only checks that it's above scale(3*100 + 2*50) + scaled_epsilon.
            // ok abs($paths->[0]->length - scale(3*100 + 2*50)) - scaled_epsilon, 'path has expected length';
            REQUIRE(std::abs(paths[0].length() - static_cast<double>(scale_(3*100 + 2*50))) - SCALED_EPSILON > 0); // path has expected length

            test_set.clear();
        }
    }
    SECTION("Diamond with endpoints on grid") {
        std::vector<Vec2d> points {Vec2d(0,0), Vec2d(100,0), Vec2d(150,50), Vec2d(100,100), Vec2d(0,100), Vec2d(-50,50)};
        Slic3r::Points test_set;
        test_set.reserve(6);
        std::transform(points.cbegin(), points.cend(),   std::back_inserter(test_set), [] (const Vec2d& a) -> Point { return Point::new_scale(a.x(), a.y()); } );
        Slic3r::Polylines paths = test(Slic3r::ExPolygon(test_set));
        REQUIRE(paths.size() == 1); // one continuous path
    }

    SECTION("Square with hole") {
        std::vector<Vec2d> square {Vec2d(0,0), Vec2d(100,0), Vec2d(100,100), Vec2d(0,100)};
        std::vector<Vec2d> hole {Vec2d(25,25), Vec2d(75,25), Vec2d(75,75), Vec2d(25,75) };
        std::reverse(hole.begin(), hole.end());

        Slic3r::Points test_hole;
        Slic3r::Points test_square;

        std::transform(square.cbegin(), square.cend(), std::back_inserter(test_square), [] (const Vec2d& a) -> Point { return Point::new_scale(a.x(), a.y()); } );
        std::transform(hole.cbegin(), hole.cend(), std::back_inserter(test_hole), [] (const Vec2d& a) -> Point { return Point::new_scale(a.x(), a.y()); } );

        for (double angle : {-(PI/2.0), -(PI/4.0), -(PI), PI/2.0, PI}) {
            for (double spacing : {25.0, 5.0, 7.5, 8.5}) {
				fill_params.density = float(filler->spacing / spacing);
                filler->angle = float(angle);
                ExPolygon e(test_square, test_hole);
                Slic3r::Polylines paths = test(e);
#if 0
				{
					BoundingBox bbox = get_extents(e);
					SVG svg("c:\\data\\temp\\square_with_holes.svg", bbox);
					svg.draw(e);
					svg.draw(paths);
					svg.Close();
				}
#endif
                REQUIRE((paths.size() >= 1 && paths.size() <= 3));
                // paths don't cross hole
                REQUIRE(diff_pl(paths, offset(e, float(SCALED_EPSILON*10))).size() == 0);
            }
        }
    }
    SECTION("Regression: Missing infill segments in some rare circumstances") {
        filler->angle = float(PI/4.0);
		fill_params.dont_adjust = false;
        filler->spacing = 0.654498;
        //filler->endpoints_overlap = unscale(359974);
		fill_params.density = 1;
        filler->layer_id = 66;
        filler->z = 20.15;

        Slic3r::Points points {Point(25771516,14142125),Point(14142138,25771515),Point(2512749,14142131),Point(14142125,2512749)};
        Slic3r::Polylines paths = test(Slic3r::ExPolygon(points));
        REQUIRE(paths.size() == 1); // one continuous path

        // TODO: determine what the "Expected length" should be for rectilinear fill of a 100x100 polygon. 
        // This check only checks that it's above scale(3*100 + 2*50) + scaled_epsilon.
        // ok abs($paths->[0]->length - scale(3*100 + 2*50)) - scaled_epsilon, 'path has expected length';
        REQUIRE(std::abs(paths[0].length() - static_cast<double>(scale_(3*100 + 2*50))) - SCALED_EPSILON > 0); // path has expected length
    }

    SECTION("Rotated Square") {
        Slic3r::Points square { Point::new_scale(0,0), Point::new_scale(50,0), Point::new_scale(50,50), Point::new_scale(0,50)};
        Slic3r::ExPolygon expolygon(square);
        std::unique_ptr<Slic3r::Fill> filler(Slic3r::Fill::new_from_type("rectilinear"));
		filler->bounding_box = get_extents(expolygon.contour);
        filler->angle = 0;
        
        Surface surface(stTop, expolygon);
        auto flow = Slic3r::Flow(0.69f, 0.4f, 0.50f);

		FillParams fill_params;
		fill_params.density = 1.0;
		filler->spacing = flow.spacing();

        for (auto angle : { 0.0, 45.0}) {
            surface.expolygon.rotate(angle, Point(0,0));
            Polylines paths = filler->fill_surface(&surface, fill_params);
            REQUIRE(paths.size() == 1);
        }
    }

    #if 0   // Disabled temporarily due to precission issues on the Mac VM
    SECTION("Solid surface fill") {
        Slic3r::Points points {
            Point::new_scale(6883102, 9598327.01296997),
            Point::new_scale(6883102, 20327272.01297),
            Point::new_scale(3116896, 20327272.01297),
            Point::new_scale(3116896, 9598327.01296997) 
        };
        Slic3r::ExPolygon expolygon(points);
         
        REQUIRE(test_if_solid_surface_filled(expolygon, 0.55) == true);
        for (size_t i = 0; i <= 20; ++i)
        {
            expolygon.scale(1.05);
            REQUIRE(test_if_solid_surface_filled(expolygon, 0.55) == true);
        }
    }
    #endif

    SECTION("Solid surface fill") {
        Slic3r::Points points {
                Slic3r::Point(59515297,5422499),Slic3r::Point(59531249,5578697),Slic3r::Point(59695801,6123186),
                Slic3r::Point(59965713,6630228),Slic3r::Point(60328214,7070685),Slic3r::Point(60773285,7434379),
                Slic3r::Point(61274561,7702115),Slic3r::Point(61819378,7866770),Slic3r::Point(62390306,7924789),
                Slic3r::Point(62958700,7866744),Slic3r::Point(63503012,7702244),Slic3r::Point(64007365,7434357),
                Slic3r::Point(64449960,7070398),Slic3r::Point(64809327,6634999),Slic3r::Point(65082143,6123325),
                Slic3r::Point(65245005,5584454),Slic3r::Point(65266967,5422499),Slic3r::Point(66267307,5422499),
                Slic3r::Point(66269190,8310081),Slic3r::Point(66275379,17810072),Slic3r::Point(66277259,20697500),
                Slic3r::Point(65267237,20697500),Slic3r::Point(65245004,20533538),Slic3r::Point(65082082,19994444),
                Slic3r::Point(64811462,19488579),Slic3r::Point(64450624,19048208),Slic3r::Point(64012101,18686514),
                Slic3r::Point(63503122,18415781),Slic3r::Point(62959151,18251378),Slic3r::Point(62453416,18198442),
                Slic3r::Point(62390147,18197355),Slic3r::Point(62200087,18200576),Slic3r::Point(61813519,18252990),
                Slic3r::Point(61274433,18415918),Slic3r::Point(60768598,18686517),Slic3r::Point(60327567,19047892),
                Slic3r::Point(59963609,19493297),Slic3r::Point(59695865,19994587),Slic3r::Point(59531222,20539379),
                Slic3r::Point(59515153,20697500),Slic3r::Point(58502480,20697500),Slic3r::Point(58502480,5422499)
        };
        Slic3r::ExPolygon expolygon(points);
         
        REQUIRE(test_if_solid_surface_filled(expolygon, 0.55) == true);
        REQUIRE(test_if_solid_surface_filled(expolygon, 0.55, PI/2.0) == true);
    }
    SECTION("Solid surface fill") {
        Slic3r::Points points {
            Point::new_scale(0,0),Point::new_scale(98,0),Point::new_scale(98,10), Point::new_scale(0,10)
        };
        Slic3r::ExPolygon expolygon(points);
         
        REQUIRE(test_if_solid_surface_filled(expolygon, 0.5, 45.0, 0.99) == true);
    }
}

/*
{
    my $collection = Slic3r::Polyline::Collection->new(
            Slic3r::Polyline->new([0,15], [0,18], [0,20]),
            Slic3r::Polyline->new([0,10], [0,8], [0,5]),
            );
    is_deeply
        [ map $_->[Y], map @$_, @{$collection->chained_path_from(Slic3r::Point->new(0,30), 0)} ],
        [20, 18, 15, 10, 8, 5],
        'chained path';
}

{
    my $collection = Slic3r::Polyline::Collection->new(
            Slic3r::Polyline->new([4,0], [10,0], [15,0]),
            Slic3r::Polyline->new([10,5], [15,5], [20,5]),
            );
    is_deeply
        [ map $_->[X], map @$_, @{$collection->chained_path_from(Slic3r::Point->new(30,0), 0)} ],
        [reverse 4, 10, 15, 10, 15, 20],
        'chained path';
}

{
    my $collection = Slic3r::ExtrusionPath::Collection->new(
            map Slic3r::ExtrusionPath->new(polyline => $_, role => 0, mm3_per_mm => 1),
            Slic3r::Polyline->new([0,15], [0,18], [0,20]),
            Slic3r::Polyline->new([0,10], [0,8], [0,5]),
            );
    is_deeply
        [ map $_->[Y], map @{$_->polyline}, @{$collection->chained_path_from(Slic3r::Point->new(0,30), 0)} ],
        [20, 18, 15, 10, 8, 5],
        'chained path';
}

{
    my $collection = Slic3r::ExtrusionPath::Collection->new(
            map Slic3r::ExtrusionPath->new(polyline => $_, role => 0, mm3_per_mm => 1),
            Slic3r::Polyline->new([15,0], [10,0], [4,0]),
            Slic3r::Polyline->new([10,5], [15,5], [20,5]),
            );
    is_deeply
        [ map $_->[X], map @{$_->polyline}, @{$collection->chained_path_from(Slic3r::Point->new(30,0), 0)} ],
        [reverse 4, 10, 15, 10, 15, 20],
        'chained path';
}

for my $pattern (qw(rectilinear honeycomb hilbertcurve concentric)) {
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('fill_pattern', $pattern);
    $config->set('external_fill_pattern', $pattern);
    $config->set('perimeters', 1);
    $config->set('skirts', 0);
    $config->set('fill_density', 20);
    $config->set('layer_height', 0.05);
    $config->set('perimeter_extruder', 1);
    $config->set('infill_extruder', 2);
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config, scale => 2);
    ok my $gcode = Slic3r::Test::gcode($print), "successful $pattern infill generation";
    my $tool = undef;
    my @perimeter_points = my @infill_points = ();
    Slic3r::GCode::Reader->new->parse($gcode, sub {
            my ($self, $cmd, $args, $info) = @_;

            if ($cmd =~ /^T(\d+)/) {
            $tool = $1;
            } elsif ($cmd eq 'G1' && $info->{extruding} && $info->{dist_XY} > 0) {
            if ($tool == $config->perimeter_extruder-1) {
            push @perimeter_points, Slic3r::Point->new_scale($args->{X}, $args->{Y});
            } elsif ($tool == $config->infill_extruder-1) {
            push @infill_points, Slic3r::Point->new_scale($args->{X}, $args->{Y});
            }
            }
            });
    my $convex_hull = convex_hull(\@perimeter_points);
    ok !(defined first { !$convex_hull->contains_point($_) } @infill_points), "infill does not exceed perimeters ($pattern)";
}

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('infill_only_where_needed', 1);
    $config->set('bottom_solid_layers', 0);
    $config->set('infill_extruder', 2);
    $config->set('infill_extrusion_width', 0.5);
    $config->set('fill_density', 40);
    $config->set('cooling', 0);                 # for preventing speeds from being altered
        $config->set('first_layer_speed', '100%');  # for preventing speeds from being altered

        my $test = sub {
            my $print = Slic3r::Test::init_print('pyramid', config => $config);

            my $tool = undef;
            my @infill_extrusions = ();  # array of polylines
                Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
                        my ($self, $cmd, $args, $info) = @_;

                        if ($cmd =~ /^T(\d+)/) {
                        $tool = $1;
                        } elsif ($cmd eq 'G1' && $info->{extruding} && $info->{dist_XY} > 0) {
                        if ($tool == $config->infill_extruder-1) {
                        push @infill_extrusions, Slic3r::Line->new_scale(
                                [ $self->X, $self->Y ],
                                [ $info->{new_X}, $info->{new_Y} ],
                                );
                        }
                        }
                        });
            return 0 if !@infill_extrusions;  # prevent calling convex_hull() with no points

                my $convex_hull = convex_hull([ map $_->pp, map @$_, @infill_extrusions ]);
            return unscale unscale sum(map $_->area, @{offset([$convex_hull], scale(+$config->infill_extrusion_width/2))});
        };

    my $tolerance = 5;  # mm^2

        $config->set('solid_infill_below_area', 0);
    ok $test->() < $tolerance,
       'no infill is generated when using infill_only_where_needed on a pyramid';

    $config->set('solid_infill_below_area', 70);
    ok abs($test->() - $config->solid_infill_below_area) < $tolerance,
       'infill is only generated under the forced solid shells';
}

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('skirts', 0);
    $config->set('perimeters', 1);
    $config->set('fill_density', 0);
    $config->set('top_solid_layers', 0);
    $config->set('bottom_solid_layers', 0);
    $config->set('solid_infill_below_area', 20000000);
    $config->set('solid_infill_every_layers', 2);
    $config->set('perimeter_speed', 99);
    $config->set('external_perimeter_speed', 99);
    $config->set('cooling', 0);
    $config->set('first_layer_speed', '100%');

    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    my %layers_with_extrusion = ();
    Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
            my ($self, $cmd, $args, $info) = @_;

            if ($cmd eq 'G1' && $info->{dist_XY} > 0 && $info->{extruding}) {
            if (($args->{F} // $self->F) != $config->perimeter_speed*60) {
            $layers_with_extrusion{$self->Z} = ($args->{F} // $self->F);
            }
            }
            });

    ok !%layers_with_extrusion,
       "solid_infill_below_area and solid_infill_every_layers are ignored when fill_density is 0";
}

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('skirts', 0);
    $config->set('perimeters', 3);
    $config->set('fill_density', 0);
    $config->set('layer_height', 0.2);
    $config->set('first_layer_height', 0.2);
    $config->set('nozzle_diameter', [0.35]);
    $config->set('infill_extruder', 2);
    $config->set('solid_infill_extruder', 2);
    $config->set('infill_extrusion_width', 0.52);
    $config->set('solid_infill_extrusion_width', 0.52);
    $config->set('first_layer_extrusion_width', 0);

    my $print = Slic3r::Test::init_print('A', config => $config);
    my %infill = ();  # Z => [ Line, Line ... ]
        my $tool = undef;
    Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
            my ($self, $cmd, $args, $info) = @_;

            if ($cmd =~ /^T(\d+)/) {
            $tool = $1;
            } elsif ($cmd eq 'G1' && $info->{extruding} && $info->{dist_XY} > 0) {
            if ($tool == $config->infill_extruder-1) {
            my $z = 1 * $self->Z;
            $infill{$z} ||= [];
            push @{$infill{$z}}, Slic3r::Line->new_scale(
                    [ $self->X, $self->Y ],
                    [ $info->{new_X}, $info->{new_Y} ],
                    );
            }
            }
            });
    my $grow_d = scale($config->infill_extrusion_width)/2;
    my $layer0_infill = union([ map @{$_->grow($grow_d)}, @{ $infill{0.2} } ]);
    my $layer1_infill = union([ map @{$_->grow($grow_d)}, @{ $infill{0.4} } ]);
    my $diff = diff($layer0_infill, $layer1_infill);
    $diff = offset2_ex($diff, -$grow_d, +$grow_d);
    $diff = [ grep { $_->area > 2*(($grow_d*2)**2) } @$diff ];
    is scalar(@$diff), 0, 'no missing parts in solid shell when fill_density is 0';
}

{
    # GH: #2697
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('perimeter_extrusion_width', 0.72);
    $config->set('top_infill_extrusion_width', 0.1);
    $config->set('infill_extruder', 2);         # in order to distinguish infill
        $config->set('solid_infill_extruder', 2);   # in order to distinguish infill

        my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    my %infill = ();  # Z => [ Line, Line ... ]
        my %other  = ();  # Z => [ Line, Line ... ]
        my $tool = undef;
    Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
            my ($self, $cmd, $args, $info) = @_;

            if ($cmd =~ /^T(\d+)/) {
            $tool = $1;
            } elsif ($cmd eq 'G1' && $info->{extruding} && $info->{dist_XY} > 0) {
            my $z = 1 * $self->Z;
            my $line = Slic3r::Line->new_scale(
                    [ $self->X, $self->Y ],
                    [ $info->{new_X}, $info->{new_Y} ],
                    );
            if ($tool == $config->infill_extruder-1) {
            $infill{$z} //= [];
            push @{$infill{$z}}, $line;
            } else {
            $other{$z} //= [];
            push @{$other{$z}}, $line;
            }
            }
            });
    my $top_z = max(keys %infill);
    my $top_infill_grow_d = scale($config->top_infill_extrusion_width)/2;
    my $top_infill = union([ map @{$_->grow($top_infill_grow_d)}, @{ $infill{$top_z} } ]);
    my $perimeters_grow_d = scale($config->perimeter_extrusion_width)/2;
    my $perimeters = union([ map @{$_->grow($perimeters_grow_d)}, @{ $other{$top_z} } ]);
    my $covered = union_ex([ @$top_infill, @$perimeters ]);
    my @holes = map @{$_->holes}, @$covered;
    ok sum(map unscale unscale $_->area*-1, @holes) < 1, 'no gaps between top solid infill and perimeters';
}
*/

bool test_if_solid_surface_filled(const ExPolygon& expolygon, double flow_spacing, double angle, double density)
{
    std::unique_ptr<Slic3r::Fill> filler(Slic3r::Fill::new_from_type("rectilinear"));
	filler->bounding_box = get_extents(expolygon.contour);
    filler->angle = float(angle);

	Flow flow(float(flow_spacing), 0.4f, float(flow_spacing));
	filler->spacing = flow.spacing();

	FillParams fill_params;
	fill_params.density = float(density);
	fill_params.dont_adjust = false;

	Surface surface(stBottom, expolygon);
	Slic3r::Polylines paths = filler->fill_surface(&surface, fill_params);

    // check whether any part was left uncovered
    Polygons grown_paths;
    grown_paths.reserve(paths.size());

    // figure out what is actually going on here re: data types
    float line_offset = float(scale_(filler->spacing / 2.0 + EPSILON));
    std::for_each(paths.begin(), paths.end(), [line_offset, &grown_paths] (const Slic3r::Polyline& p) {
        polygons_append(grown_paths, offset(p, line_offset));
    });

	// Shrink the initial expolygon a bit, this simulates the infill / perimeter overlap that we usually apply.
    ExPolygons uncovered = diff_ex(offset(expolygon, - float(0.2 * scale_(flow_spacing))), grown_paths, ApplySafetyOffset::Yes);

    // ignore very small dots
    const double scaled_flow_spacing = std::pow(scale_(flow_spacing), 2);
    uncovered.erase(std::remove_if(uncovered.begin(), uncovered.end(), [scaled_flow_spacing](const ExPolygon& poly) { return poly.area() < scaled_flow_spacing; }), uncovered.end());

#if 0
	if (! uncovered.empty()) {
		BoundingBox bbox = get_extents(expolygon.contour);
		bbox.merge(get_extents(uncovered));
		bbox.merge(get_extents(grown_paths));
		SVG svg("c:\\data\\temp\\test_if_solid_surface_filled.svg", bbox);
		svg.draw(expolygon);
		svg.draw(uncovered, "red");
		svg.Close();
	}
#endif

    return uncovered.empty(); // solid surface is fully filled
}

static Polygon make_regular_ngon(coordf_t radius_mm, int n)
{
    Polygon poly;
    poly.points.reserve(n);
    for (int i = 0; i < n; ++i) {
        const double a = 2. * PI * double(i) / double(n);
        poly.points.push_back(Point::new_scale(radius_mm * std::cos(a), radius_mm * std::sin(a)));
    }
    return poly;
}

static size_t count_long_segment_angle_bins(const Polylines &paths, double min_len_mm, double bin_deg)
{
    std::set<int> bins;
    const double  min_len = scale_(min_len_mm);
    for (const Polyline &pl : paths) {
        for (size_t i = 1; i < pl.points.size(); ++i) {
            Vec2d  d   = (pl.points[i] - pl.points[i - 1]).cast<double>();
            double len = d.norm();
            if (len < min_len)
                continue;
            double deg = std::atan2(d.y(), d.x()) * 180. / PI;
            if (deg < 0)
                deg += 180.; // directionless
            bins.insert(int(std::floor(deg / bin_deg)));
        }
    }
    return bins.size();
}

static FillParams make_conformal_params(ConformalStagger stagger = ConformalStagger::None,
                                        InfillPattern pattern = ipZigZag,
                                        ConformalPole pole = ConformalPole::Layer)
{
    FillParams params;
    params.density            = 0.2f;
    params.conformal          = true;
    params.conformal_stagger  = stagger;
    params.conformal_pole     = pole;
    params.dont_adjust        = true;
    params.anchor_length      = 1.f;
    params.anchor_length_max  = 10.f;
    params.pattern            = pattern;
    params.conformal_hub_radius = -1.f;
    return params;
}

static std::unique_ptr<Fill> make_conformal_filler(const ExPolygon &poly, size_t layer_id, BoundingBox obj_bb = BoundingBox(),
                                                   const char *type = "zigzag", double z = 0.)
{
    std::unique_ptr<Fill> filler(Fill::new_from_type(type));
    filler->bounding_box = obj_bb.defined ? obj_bb : get_extents(poly);
    filler->spacing      = 0.4;
    filler->angle        = 0.f;
    filler->layer_id     = layer_id;
    filler->z            = z;
    return filler;
}

static Polylines fill_conformal(const ExPolygon &poly, size_t layer_id, ConformalStagger stagger,
                                BoundingBox obj_bb = BoundingBox(), bool connect = true,
                                ConformalPole pole = ConformalPole::Layer, double z = 0.)
{
    auto       filler = make_conformal_filler(poly, layer_id, obj_bb, "zigzag", z);
    FillParams params = make_conformal_params(stagger, ipZigZag, pole);
    if (!connect) {
        params.anchor_length     = 0.f;
        params.anchor_length_max = 0.f;
    }
    Surface surface(stInternal, poly);
    return filler->fill_surface(&surface, params);
}

static size_t count_long_polylines(const Polylines &pls, size_t min_pts = 6)
{
    size_t n = 0;
    for (const Polyline &pl : pls)
        if (pl.points.size() >= min_pts)
            ++n;
    return n;
}

static std::vector<double> undirected_long_angles(const Polylines &paths, double min_len_mm)
{
    std::vector<double> out;
    const double min_len = scale_(min_len_mm);
    for (const Polyline &pl : paths) {
        for (size_t i = 1; i < pl.points.size(); ++i) {
            Vec2d  d   = (pl.points[i] - pl.points[i - 1]).cast<double>();
            double len = d.norm();
            if (len < min_len)
                continue;
            double deg = std::atan2(d.y(), d.x()) * 180. / PI;
            if (deg < 0)
                deg += 180.;
            out.push_back(deg);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

static double mean_signed_lean(const Polylines &paths, const Point &pole, double min_len_mm)
{
    const double min_len = scale_(min_len_mm);
    double       sum     = 0.;
    size_t       n       = 0;
    for (const Polyline &pl : paths) {
        for (size_t i = 1; i < pl.points.size(); ++i) {
            const double len = (pl.points[i] - pl.points[i - 1]).cast<double>().norm();
            if (len < min_len)
                continue;
            const double r0 = (pl.points[i - 1] - pole).cast<double>().norm();
            const double r1 = (pl.points[i] - pole).cast<double>().norm();
            const Point &pt_near = r0 <= r1 ? pl.points[i - 1] : pl.points[i];
            const Point &pt_far  = r0 <= r1 ? pl.points[i] : pl.points[i - 1];
            const Vec2d  u       = (pt_near - pole).cast<double>();
            const Vec2d  v       = (pt_far - pole).cast<double>();
            sum += u.x() * v.y() - u.y() * v.x();
            ++n;
        }
    }
    return n ? sum / double(n) : 0.;
}

static bool first_scan_goes_out(const Polylines &paths, const Point &pole, double min_len_mm = 2.0)
{
    const double min_len = scale_(min_len_mm);
    for (const Polyline &pl : paths) {
        for (size_t i = 1; i < pl.points.size(); ++i) {
            const double len = (pl.points[i] - pl.points[i - 1]).cast<double>().norm();
            if (len < min_len)
                continue;
            const double r0 = (pl.points[i - 1] - pole).cast<double>().norm();
            const double r1 = (pl.points[i] - pole).cast<double>().norm();
            return r1 > r0;
        }
    }
    return false;
}

static double mean_angular_span_deg(const Polylines &paths, const Point &pole, double min_len_mm)
{
    const double min_len = scale_(min_len_mm);
    double       sum     = 0.;
    size_t       n       = 0;
    for (const Polyline &pl : paths) {
        for (size_t i = 1; i < pl.points.size(); ++i) {
            const double len = (pl.points[i] - pl.points[i - 1]).cast<double>().norm();
            if (len < min_len)
                continue;
            const double r0 = (pl.points[i - 1] - pole).cast<double>().norm();
            const double r1 = (pl.points[i] - pole).cast<double>().norm();
            const Point &pt_near = r0 <= r1 ? pl.points[i - 1] : pl.points[i];
            const Point &pt_far  = r0 <= r1 ? pl.points[i] : pl.points[i - 1];
            const Vec2d  u       = (pt_near - pole).cast<double>();
            const Vec2d  v       = (pt_far - pole).cast<double>();
            double       d    = std::atan2(v.y(), v.x()) - std::atan2(u.y(), u.x());
            while (d > PI)
                d -= 2. * PI;
            while (d < -PI)
                d += 2. * PI;
            sum += std::abs(d) * 180. / PI;
            ++n;
        }
    }
    return n ? sum / double(n) : 0.;
}

static double mean_nearest_angle_gap(const std::vector<double> &a0, const std::vector<double> &a1)
{
    std::vector<double> nearest;
    nearest.reserve(a1.size());
    for (double b : a1) {
        double best = 180.;
        for (double a : a0)
            best = std::min(best, std::abs(b - a));
        nearest.push_back(best);
    }
    if (nearest.empty())
        return 0.;
    return std::accumulate(nearest.begin(), nearest.end(), 0.) / double(nearest.size());
}

static bool polyline_is_closed(const Polyline &pl)
{
    return pl.size() >= 4 && (pl.front() - pl.back()).cast<double>().norm() < double(scale_(0.6));
}

static bool polylines_have_interior_crossing(const Polylines &pls)
{
    const double end_eps = scale_(0.4);
    for (size_t i = 0; i < pls.size(); ++i) {
        const Polyline &a = pls[i];
        for (size_t ia = 1; ia < a.points.size(); ++ia) {
            Vec2d p1 = a.points[ia - 1].cast<double>();
            Vec2d v1 = a.points[ia].cast<double>() - p1;
            for (size_t j = i + 1; j < pls.size(); ++j) {
                const Polyline &b = pls[j];
                for (size_t ib = 1; ib < b.points.size(); ++ib) {
                    Vec2d p2 = b.points[ib - 1].cast<double>();
                    Vec2d v2 = b.points[ib].cast<double>() - p2;
                    Vec2d hit;
                    if (!Geometry::segment_segment_intersection(p1, v1, p2, v2, hit))
                        continue;
                    const double da0 = (hit - p1).norm();
                    const double da1 = (hit - (p1 + v1)).norm();
                    const double db0 = (hit - p2).norm();
                    const double db1 = (hit - (p2 + v2)).norm();
                    if (std::min(da0, da1) > end_eps && std::min(db0, db1) > end_eps)
                        return true;
                }
            }
        }
    }
    return false;
}

static size_t count_collinear_opposite_pairs(const Polylines &pls, double min_len_mm, double ang_deg, double line_tol_mm)
{
    struct Seg { Point a, b; double ang; };
    std::vector<Seg> segs;
    const double min_len = scale_(min_len_mm);
    for (const Polyline &pl : pls) {
        for (size_t i = 1; i < pl.points.size(); ++i) {
            Vec2d d = (pl.points[i] - pl.points[i - 1]).cast<double>();
            if (d.norm() < min_len)
                continue;
            double deg = std::atan2(d.y(), d.x()) * 180. / PI;
            if (deg < 0)
                deg += 180.;
            segs.push_back({ pl.points[i - 1], pl.points[i], deg });
        }
    }
    const double tol = scale_(line_tol_mm);
    size_t pairs = 0;
    for (size_t i = 0; i < segs.size(); ++i) {
        Vec2d dir = (segs[i].b - segs[i].a).cast<double>();
        const double dn = dir.norm();
        if (dn < 1.)
            continue;
        dir /= dn;
        for (size_t j = i + 1; j < segs.size(); ++j) {
            double da = std::abs(segs[i].ang - segs[j].ang);
            da = std::min(da, 180. - da);
            if (da > ang_deg)
                continue;
            Vec2d mid = 0.5 * (segs[j].a + segs[j].b).cast<double>();
            Vec2d rel = mid - segs[i].a.cast<double>();
            const double dist = std::abs(rel.x() * dir.y() - rel.y() * dir.x());
            if (dist < tol)
                ++pairs;
        }
    }
    return pairs;
}

static double fraction_lines_through_point(const Polylines &pls, const Point &pt, double min_len_mm, double dist_mm)
{
    size_t n_near = 0, n_total = 0;
    const double min_len = scale_(min_len_mm);
    const double tol     = scale_(dist_mm);
    for (const Polyline &pl : pls) {
        for (size_t i = 1; i < pl.points.size(); ++i) {
            Vec2d d = (pl.points[i] - pl.points[i - 1]).cast<double>();
            if (d.norm() < min_len)
                continue;
            ++n_total;
            Vec2d a = pl.points[i - 1].cast<double>();
            const double len = d.norm();
            Vec2d dir = d / len;
            Vec2d rel = pt.cast<double>() - a;
            const double dist = std::abs(rel.x() * dir.y() - rel.y() * dir.x());
            if (dist < tol)
                ++n_near;
        }
    }
    return n_total ? double(n_near) / double(n_total) : 0.;
}

static ExPolygon make_annulus(coordf_t r_out, coordf_t r_in)
{
    ExPolygon ring;
    ring.contour = make_regular_ngon(r_out, 48);
    Polygon hole = make_regular_ngon(r_in, 48);
    std::reverse(hole.points.begin(), hole.points.end());
    ring.holes.push_back(std::move(hole));
    return ring;
}

static void dump_conformal_overlay(const std::string &, const ExPolygon &,
                                   const Polylines &, const Polylines &)
{
}

TEST_CASE("Fill: conformal zigzag on an annulus varies in direction", "[Fill][Conformal]") {
    ExPolygon ring = make_annulus(20., 10.);
    const BoundingBox obj_bb = get_extents(ring);

    Polylines paths = fill_conformal(ring, 0, ConformalStagger::None, obj_bb);
    REQUIRE_FALSE(paths.empty());
    for (const Polyline &pl : paths)
        for (const Point &pt : pl.points)
            REQUIRE(ring.contains(pt));
    REQUIRE(count_long_segment_angle_bins(paths, 2.0, 20.0) >= 4);
    REQUIRE(paths.size() <= 8);

    Polylines even_n = fill_conformal(ring, 0, ConformalStagger::None, obj_bb, false);
    Polylines odd_n  = fill_conformal(ring, 1, ConformalStagger::None, obj_bb, false);
    dump_conformal_overlay("annulus_none", ring, even_n, odd_n);

    REQUIRE_FALSE(polylines_have_interior_crossing(even_n));
    REQUIRE_FALSE(polylines_have_interior_crossing(odd_n));
    REQUIRE(fraction_lines_through_point(even_n, ring.holes.front().centroid(), 2.0, 2.0) > 0.7);
    REQUIRE(paths.size() < even_n.size());
    REQUIRE(paths.size() <= std::max(size_t(6), even_n.size() / 4));
    REQUIRE_FALSE(polylines_have_interior_crossing(paths));

    Polylines even_a = fill_conformal(ring, 0, ConformalStagger::Alternate, obj_bb, false);
    Polylines odd_a  = fill_conformal(ring, 1, ConformalStagger::Alternate, obj_bb, false);
    dump_conformal_overlay("annulus_alternate", ring, even_a, odd_a);

    Polylines even_o = fill_conformal(ring, 0, ConformalStagger::Orthogonal, obj_bb, false);
    Polylines odd_o  = fill_conformal(ring, 1, ConformalStagger::Orthogonal, obj_bb, false);
    dump_conformal_overlay("annulus_orthogonal", ring, even_o, odd_o);
    REQUIRE_FALSE(odd_o.empty());
}

TEST_CASE("Fill: conformal polar disk is not a single parallel family", "[Fill][Conformal]") {
    ExPolygon disk;
    disk.contour = make_regular_ngon(20., 48);
    Polylines paths = fill_conformal(disk, 0, ConformalStagger::None);
    REQUIRE_FALSE(paths.empty());
    REQUIRE(count_long_segment_angle_bins(paths, 2.0, 20.0) >= 4);
    dump_conformal_overlay("disk", disk, paths, {});
}

TEST_CASE("Fill: conformal cylinder layers stack with stagger off", "[Fill][Conformal]") {
    ExPolygon disk;
    disk.contour = make_regular_ngon(20., 48);
    const BoundingBox obj_bb = get_extents(disk);
    Polylines even_paths = fill_conformal(disk, 0, ConformalStagger::None, obj_bb, false);
    Polylines odd_paths  = fill_conformal(disk, 1, ConformalStagger::None, obj_bb, false);
    REQUIRE_FALSE(even_paths.empty());
    REQUIRE_FALSE(odd_paths.empty());
    dump_conformal_overlay("cylinder_none", disk, even_paths, odd_paths);

    auto a0 = undirected_long_angles(even_paths, 2.0);
    auto a1 = undirected_long_angles(odd_paths, 2.0);
    REQUIRE(a0.size() >= 4);
    REQUIRE(a1.size() >= 4);
    // Same θ_k: every even-layer heading has a near neighbor on the odd layer.
    size_t matched = 0;
    for (double a : a0) {
        double best = 180.;
        for (double b : a1)
            best = std::min(best, std::abs(a - b));
        if (best < 4.0)
            ++matched;
    }
    REQUIRE(matched >= a0.size() * 3 / 4);
}

TEST_CASE("Fill: conformal start rivet holds a fixed lock angle", "[Fill][Conformal]") {
    auto first_outer_theta_deg = [](const Polylines &paths, const Point &pole, double min_len_mm) {
        const double min_len = scale_(min_len_mm);
        for (const Polyline &pl : paths) {
            for (size_t i = 1; i < pl.points.size(); ++i) {
                const double len = (pl.points[i] - pl.points[i - 1]).cast<double>().norm();
                if (len < min_len)
                    continue;
                const double r0 = (pl.points[i - 1] - pole).cast<double>().norm();
                const double r1 = (pl.points[i] - pole).cast<double>().norm();
                const Point &pt_far = r0 <= r1 ? pl.points[i] : pl.points[i - 1];
                return std::atan2(double(pt_far.y() - pole.y()), double(pt_far.x() - pole.x())) * 180. / PI;
            }
        }
        return 0.;
    };
    auto abs_deg = [](double a, double b) {
        double d = a - b;
        while (d > 180.)
            d -= 360.;
        while (d < -180.)
            d += 360.;
        return std::abs(d);
    };

    ExPolygon disk;
    disk.contour = make_regular_ngon(20., 48);
    const BoundingBox obj_bb = get_extents(disk);
    const Point pole = obj_bb.center();
    FillRadialZigZag::reset_n_lock();
    FillRadialZigZag::pin_scan_counts({ { disk }, { disk }, { disk } }, 2.0);
    Polylines p0 = fill_conformal(disk, 0, ConformalStagger::None, obj_bb, true);
    Polylines p2 = fill_conformal(disk, 2, ConformalStagger::None, obj_bb, true);
    FillRadialZigZag::reset_n_lock();
    REQUIRE_FALSE(p0.empty());
    REQUIRE_FALSE(p2.empty());
    REQUIRE(abs_deg(first_outer_theta_deg(p0, pole, 2.0), first_outer_theta_deg(p2, pole, 2.0)) < 6.0);

    ExPolygon a = make_annulus(20.0, 10.0);
    ExPolygon b = make_annulus(20.5, 10.2);
    const BoundingBox abb = get_extents(a);
    const Point apole = abb.center();
    FillRadialZigZag::reset_n_lock();
    FillRadialZigZag::pin_scan_counts({ { a }, { b } }, 2.0);
    Polylines pa = fill_conformal(a, 0, ConformalStagger::None, abb, true);
    Polylines pb = fill_conformal(b, 1, ConformalStagger::None, abb, true);
    FillRadialZigZag::reset_n_lock();
    REQUIRE_FALSE(pa.empty());
    REQUIRE_FALSE(pb.empty());
    REQUIRE(abs_deg(first_outer_theta_deg(pa, apole, 2.0), first_outer_theta_deg(pb, apole, 2.0)) < 15.0);
}

TEST_CASE("Fill: conformal none ignores CrossZag horiz_move", "[Fill][Conformal]") {
    ExPolygon disk;
    disk.contour = make_regular_ngon(20., 48);
    const BoundingBox obj_bb = get_extents(disk);
    auto filler0 = make_conformal_filler(disk, 0, obj_bb);
    auto filler1 = make_conformal_filler(disk, 1, obj_bb);
    FillParams p0 = make_conformal_params(ConformalStagger::None);
    FillParams p1 = p0;
    p0.anchor_length = p1.anchor_length = 0.f;
    p0.anchor_length_max = p1.anchor_length_max = 0.f;
    p1.horiz_move = float(scale_(8.));
    Surface s0(stInternal, disk);
    Surface s1(stInternal, disk);
    Polylines even_paths = filler0->fill_surface(&s0, p0);
    Polylines odd_paths  = filler1->fill_surface(&s1, p1);
    REQUIRE_FALSE(even_paths.empty());
    REQUIRE_FALSE(odd_paths.empty());
    auto a0 = undirected_long_angles(even_paths, 2.0);
    auto a1 = undirected_long_angles(odd_paths, 2.0);
    size_t matched = 0;
    for (double a : a0) {
        double best = 180.;
        for (double b : a1)
            best = std::min(best, std::abs(a - b));
        if (best < 4.0)
            ++matched;
    }
    REQUIRE(a0.size() >= 4);
    REQUIRE(matched >= a0.size() * 3 / 4);
}

TEST_CASE("Fill: conformal CrossZag rotates the fan by horiz_move", "[Fill][Conformal]") {
    auto first_outer_theta_deg = [](const Polylines &paths, const Point &pole, double min_len_mm) {
        const double min_len = scale_(min_len_mm);
        for (const Polyline &pl : paths) {
            for (size_t i = 1; i < pl.points.size(); ++i) {
                const double len = (pl.points[i] - pl.points[i - 1]).cast<double>().norm();
                if (len < min_len)
                    continue;
                const double r0 = (pl.points[i - 1] - pole).cast<double>().norm();
                const double r1 = (pl.points[i] - pole).cast<double>().norm();
                const Point &pt_far = r0 <= r1 ? pl.points[i] : pl.points[i - 1];
                return std::atan2(double(pt_far.y() - pole.y()), double(pt_far.x() - pole.x())) * 180. / PI;
            }
        }
        return 0.;
    };
    auto abs_deg = [](double a, double b) {
        double d = a - b;
        while (d > 180.)
            d -= 360.;
        while (d < -180.)
            d += 360.;
        return std::abs(d);
    };

    ExPolygon disk;
    disk.contour = make_regular_ngon(20., 48);
    const BoundingBox obj_bb = get_extents(disk);
    const Point pole = obj_bb.center();
    FillRadialZigZag::reset_n_lock();
    FillRadialZigZag::pin_scan_counts({ { disk }, { disk } }, 2.0);
    auto run = [&](size_t layer, float horiz) {
        auto       filler = make_conformal_filler(disk, layer, obj_bb, "crosszag");
        FillParams p      = make_conformal_params(ConformalStagger::None, ipCrossZag);
        p.anchor_length     = 0.f;
        p.anchor_length_max = 0.f;
        p.horiz_move        = horiz;
        Surface s(stInternal, disk);
        return filler->fill_surface(&s, p);
    };
    const Polylines even0  = run(0, 0.f);
    const Polylines odd0   = run(1, 0.f);
    const Polylines even_s = run(0, float(scale_(2.)));
    FillRadialZigZag::reset_n_lock();
    REQUIRE_FALSE(even0.empty());
    REQUIRE_FALSE(odd0.empty());
    REQUIRE_FALSE(even_s.empty());
    dump_conformal_overlay("cylinder_crosszag", disk, even0, even_s);
    const double t0 = first_outer_theta_deg(even0, pole, 2.0);
    const double t1 = first_outer_theta_deg(odd0, pole, 2.0);
    const double ts = first_outer_theta_deg(even_s, pole, 2.0);
    REQUIRE(abs_deg(t0, t1) < 6.0);
    REQUIRE(abs_deg(t0, ts) > 6.0);
    REQUIRE(abs_deg(t0, ts) < 20.0);
}

TEST_CASE("Fill: conformal CrossZag even/odd layers rotate opposite ways", "[Fill][Conformal]") {
    auto first_outer_theta_deg = [](const Polylines &paths, const Point &pole, double min_len_mm) {
        const double min_len = scale_(min_len_mm);
        for (const Polyline &pl : paths) {
            for (size_t i = 1; i < pl.points.size(); ++i) {
                const double len = (pl.points[i] - pl.points[i - 1]).cast<double>().norm();
                if (len < min_len)
                    continue;
                const double r0 = (pl.points[i - 1] - pole).cast<double>().norm();
                const double r1 = (pl.points[i] - pole).cast<double>().norm();
                const Point &pt_far = r0 <= r1 ? pl.points[i] : pl.points[i - 1];
                return std::atan2(double(pt_far.y() - pole.y()), double(pt_far.x() - pole.x())) * 180. / PI;
            }
        }
        return 0.;
    };
    auto signed_deg = [](double a, double b) {
        double d = a - b;
        while (d > 180.)
            d -= 360.;
        while (d < -180.)
            d += 360.;
        return d;
    };

    ExPolygon disk;
    disk.contour = make_regular_ngon(20., 48);
    const BoundingBox obj_bb = get_extents(disk);
    const Point pole = obj_bb.center();
    const float step = float(scale_(0.4));
    FillRadialZigZag::reset_n_lock();
    FillRadialZigZag::pin_scan_counts({ { disk }, { disk }, { disk } }, 2.0);
    auto run = [&](size_t layer, float horiz) {
        auto       filler = make_conformal_filler(disk, layer, obj_bb, "crosszag");
        FillParams p      = make_conformal_params(ConformalStagger::None, ipCrossZag);
        p.anchor_length     = 0.f;
        p.anchor_length_max = 0.f;
        p.horiz_move        = horiz;
        Surface s(stInternal, disk);
        return filler->fill_surface(&s, p);
    };
    // Same even/odd accumulation Fill.cpp uses for CrossZag.
    const double t10 = first_outer_theta_deg(run(10, -step * 5.f), pole, 2.0);
    const double t11 = first_outer_theta_deg(run(11,  step * 5.f), pole, 2.0);
    const double t12 = first_outer_theta_deg(run(12, -step * 6.f), pole, 2.0);
    FillRadialZigZag::reset_n_lock();
    const double d1011 = signed_deg(t11, t10);
    const double d1112 = signed_deg(t12, t11);
    REQUIRE(std::abs(d1011) > 0.4);
    REQUIRE(d1011 * d1112 < 0.);
}

TEST_CASE("Fill: conformal CrossZag C vs ring keep the same fan rotation", "[Fill][Conformal]") {
    auto first_outer_theta_deg = [](const Polylines &paths, const Point &pole, double min_len_mm) {
        const double min_len = scale_(min_len_mm);
        for (const Polyline &pl : paths) {
            for (size_t i = 1; i < pl.points.size(); ++i) {
                const double len = (pl.points[i] - pl.points[i - 1]).cast<double>().norm();
                if (len < min_len)
                    continue;
                const double r0 = (pl.points[i - 1] - pole).cast<double>().norm();
                const double r1 = (pl.points[i] - pole).cast<double>().norm();
                const Point &pt_far = r0 <= r1 ? pl.points[i] : pl.points[i - 1];
                return std::atan2(double(pt_far.y() - pole.y()), double(pt_far.x() - pole.x())) * 180. / PI;
            }
        }
        return 0.;
    };
    auto abs_deg = [](double a, double b) {
        double d = a - b;
        while (d > 180.)
            d -= 360.;
        while (d < -180.)
            d += 360.;
        return std::abs(d);
    };

    ExPolygon ring = make_annulus(20., 12.);
    ExPolygon cee  = ring;
    cee.holes.clear();
    const BoundingBox obj_bb = get_extents(ring);
    const Point pole = obj_bb.center();
    FillRadialZigZag::reset_n_lock();
    FillRadialZigZag::pin_scan_counts({ { ring }, { cee } }, 2.0);
    const float horiz = float(scale_(8.));
    auto run = [&](const ExPolygon &ex, size_t layer) {
        auto       filler = make_conformal_filler(ex, layer, obj_bb, "crosszag");
        FillParams p      = make_conformal_params(ConformalStagger::None, ipCrossZag);
        p.anchor_length     = 0.f;
        p.anchor_length_max = 0.f;
        p.horiz_move        = horiz;
        Surface s(stInternal, ex);
        return filler->fill_surface(&s, p);
    };
    const Polylines ring_paths = run(ring, 0);
    const Polylines cee_paths  = run(cee, 1);
    FillRadialZigZag::reset_n_lock();
    REQUIRE_FALSE(ring_paths.empty());
    REQUIRE_FALSE(cee_paths.empty());
    const double tr = first_outer_theta_deg(ring_paths, pole, 2.0);
    const double tc = first_outer_theta_deg(cee_paths, pole, 2.0);
    REQUIRE(abs_deg(tr, tc) < 4.0);
}

TEST_CASE("Fill: conformal Alternate odd layer sits in the gaps and starts inward", "[Fill][Conformal]") {
    ExPolygon disk;
    disk.contour = make_regular_ngon(20., 48);
    const BoundingBox obj_bb = get_extents(disk);
    const Point pole = obj_bb.center();
    Polylines even_open = fill_conformal(disk, 0, ConformalStagger::Alternate, obj_bb, false);
    Polylines odd_open  = fill_conformal(disk, 1, ConformalStagger::Alternate, obj_bb, false);
    Polylines even_conn = fill_conformal(disk, 0, ConformalStagger::Alternate, obj_bb, true);
    Polylines odd_conn  = fill_conformal(disk, 1, ConformalStagger::Alternate, obj_bb, true);
    REQUIRE_FALSE(even_open.empty());
    REQUIRE_FALSE(odd_open.empty());
    REQUIRE_FALSE(even_conn.empty());
    REQUIRE_FALSE(odd_conn.empty());
    dump_conformal_overlay("cylinder_alternate", disk, even_conn, odd_conn);

    const double lean0 = mean_signed_lean(even_open, pole, 2.0);
    const double lean1 = mean_signed_lean(odd_open, pole, 2.0);
    REQUIRE(mean_angular_span_deg(even_open, pole, 2.0) > 4.0);
    REQUIRE(mean_angular_span_deg(odd_open, pole, 2.0) > 4.0);
    REQUIRE(lean0 * lean1 < 0.);
}

TEST_CASE("Fill: conformal reverse 2 keep 1 flip starts from the opposite rim every third layer", "[Fill][Conformal]") {
    ExPolygon disk;
    disk.contour = make_regular_ngon(20., 48);
    const BoundingBox obj_bb = get_extents(disk);
    const Point pole = obj_bb.center();
    FillRadialZigZag::reset_n_lock();
    FillRadialZigZag::pin_scan_counts({ { disk }, { disk }, { disk } }, 2.0);
    auto run = [&](size_t layer) {
        auto       filler = make_conformal_filler(disk, layer, obj_bb);
        FillParams p      = make_conformal_params(ConformalStagger::None);
        p.conformal_link_keep_layers = 2;
        p.conformal_link_flip_layers = 1;
        Surface s(stInternal, disk);
        return filler->fill_surface(&s, p);
    };
    const Polylines p0 = run(0);
    const Polylines p1 = run(1);
    const Polylines p2 = run(2);
    FillRadialZigZag::reset_n_lock();
    REQUIRE_FALSE(p0.empty());
    REQUIRE_FALSE(p1.empty());
    REQUIRE_FALSE(p2.empty());
    REQUIRE(first_scan_goes_out(p0, pole));
    REQUIRE(first_scan_goes_out(p1, pole));
    REQUIRE_FALSE(first_scan_goes_out(p2, pole));
}

TEST_CASE("Fill: conformal Alternate pairing stays odd/even when reverse period is 2+1", "[Fill][Conformal]") {
    ExPolygon disk;
    disk.contour = make_regular_ngon(20., 48);
    const BoundingBox obj_bb = get_extents(disk);
    const Point pole = obj_bb.center();
    FillRadialZigZag::reset_n_lock();
    FillRadialZigZag::pin_scan_counts({ { disk }, { disk }, { disk } }, 2.0);
    auto run = [&](size_t layer, bool connect) {
        auto       filler = make_conformal_filler(disk, layer, obj_bb);
        FillParams p      = make_conformal_params(ConformalStagger::Alternate);
        p.conformal_link_keep_layers = 2;
        p.conformal_link_flip_layers = 1;
        if (!connect) {
            p.anchor_length     = 0.f;
            p.anchor_length_max = 0.f;
        }
        Surface s(stInternal, disk);
        return filler->fill_surface(&s, p);
    };
    const Polylines o0 = run(0, false);
    const Polylines o1 = run(1, false);
    const Polylines o2 = run(2, false);
    const Polylines c0 = run(0, true);
    const Polylines c1 = run(1, true);
    const Polylines c2 = run(2, true);
    FillRadialZigZag::reset_n_lock();
    const double l0 = mean_signed_lean(o0, pole, 2.0);
    const double l1 = mean_signed_lean(o1, pole, 2.0);
    const double l2 = mean_signed_lean(o2, pole, 2.0);
    REQUIRE(l0 * l1 < 0.);
    REQUIRE(l0 * l2 > 0.);
    REQUIRE(first_scan_goes_out(c0, pole));
    REQUIRE(first_scan_goes_out(c1, pole));
    REQUIRE_FALSE(first_scan_goes_out(c2, pole));
}

TEST_CASE("Fill: conformal reverse 1 keep 1 flip starts from the opposite rim on odd layers", "[Fill][Conformal]") {
    ExPolygon disk;
    disk.contour = make_regular_ngon(20., 48);
    const BoundingBox obj_bb = get_extents(disk);
    const Point pole = obj_bb.center();
    FillRadialZigZag::reset_n_lock();
    FillRadialZigZag::pin_scan_counts({ { disk }, { disk } }, 2.0);
    auto run = [&](size_t layer) {
        auto       filler = make_conformal_filler(disk, layer, obj_bb);
        FillParams p      = make_conformal_params(ConformalStagger::None);
        p.conformal_link_keep_layers = 1;
        p.conformal_link_flip_layers = 1;
        Surface s(stInternal, disk);
        return filler->fill_surface(&s, p);
    };
    const Polylines even = run(0);
    const Polylines odd  = run(1);
    FillRadialZigZag::reset_n_lock();
    REQUIRE_FALSE(even.empty());
    REQUIRE_FALSE(odd.empty());
    REQUIRE(first_scan_goes_out(even, pole));
    REQUIRE_FALSE(first_scan_goes_out(odd, pole));
}

TEST_CASE("Fill: conformal HalfStep aliases Alternate", "[Fill][Conformal]") {
    ExPolygon disk;
    disk.contour = make_regular_ngon(20., 48);
    const BoundingBox obj_bb = get_extents(disk);
    const Point pole = obj_bb.center();
    Polylines alt_odd_open = fill_conformal(disk, 1, ConformalStagger::Alternate, obj_bb, false);
    Polylines hs_odd_open  = fill_conformal(disk, 1, ConformalStagger::HalfStep, obj_bb, false);
    REQUIRE_FALSE(alt_odd_open.empty());
    REQUIRE_FALSE(hs_odd_open.empty());
    REQUIRE(std::abs(mean_angular_span_deg(alt_odd_open, pole, 2.0) -
                     mean_angular_span_deg(hs_odd_open, pole, 2.0)) < 1.0);
}

TEST_CASE("Fill: conformal CrossZag Alternate starts inward on odd layers", "[Fill][Conformal]") {
    ExPolygon disk;
    disk.contour = make_regular_ngon(20., 48);
    const BoundingBox obj_bb = get_extents(disk);
    const Point pole = obj_bb.center();
    auto filler0 = make_conformal_filler(disk, 0, obj_bb, "crosszag");
    auto filler1 = make_conformal_filler(disk, 1, obj_bb, "crosszag");
    FillParams p0 = make_conformal_params(ConformalStagger::Alternate, ipCrossZag);
    FillParams p1 = p0;
    p0.anchor_length = p1.anchor_length = 0.f;
    p0.anchor_length_max = p1.anchor_length_max = 0.f;
    Surface s0(stInternal, disk);
    Surface s1(stInternal, disk);
    Polylines even_open = filler0->fill_surface(&s0, p0);
    Polylines odd_open  = filler1->fill_surface(&s1, p1);
    p0.anchor_length = p1.anchor_length = 1.f;
    p0.anchor_length_max = p1.anchor_length_max = 10.f;
    Polylines even_conn = filler0->fill_surface(&s0, p0);
    Polylines odd_conn  = filler1->fill_surface(&s1, p1);
    REQUIRE_FALSE(even_open.empty());
    REQUIRE_FALSE(odd_open.empty());
    REQUIRE_FALSE(even_conn.empty());
    REQUIRE_FALSE(odd_conn.empty());
    dump_conformal_overlay("cylinder_crosszag_alternate", disk, even_conn, odd_conn);
    const double lean0 = mean_signed_lean(even_open, pole, 2.0);
    const double lean1 = mean_signed_lean(odd_open, pole, 2.0);
    REQUIRE(mean_angular_span_deg(even_open, pole, 2.0) > 4.0);
    REQUIRE(lean0 * lean1 < 0.);
}

TEST_CASE("Fill: conformal orthogonal uses offset loops on odd layers", "[Fill][Conformal]") {
    ExPolygon disk;
    disk.contour = make_regular_ngon(20., 48);
    const BoundingBox obj_bb = get_extents(disk);
    Polylines even_paths = fill_conformal(disk, 0, ConformalStagger::Orthogonal, obj_bb, false);
    Polylines odd_paths  = fill_conformal(disk, 1, ConformalStagger::Orthogonal, obj_bb, false);
    REQUIRE_FALSE(even_paths.empty());
    REQUIRE_FALSE(odd_paths.empty());
    dump_conformal_overlay("cylinder_orthogonal", disk, even_paths, odd_paths);

    REQUIRE(count_long_segment_angle_bins(even_paths, 2.0, 20.0) >= 4);
    size_t closed = 0;
    for (const Polyline &pl : odd_paths)
        if (polyline_is_closed(pl))
            ++closed;
    REQUIRE(closed >= 1);
}

TEST_CASE("Fill: conformal cone keeps the same headings", "[Fill][Conformal]") {
    ExPolygon base;
    base.contour = make_regular_ngon(20., 48);
    ExPolygon tip;
    tip.contour = make_regular_ngon(12., 48);
    const BoundingBox obj_bb = get_extents(base);
    Polylines base_paths = fill_conformal(base, 0, ConformalStagger::None, obj_bb, false);
    Polylines tip_paths  = fill_conformal(tip,  8, ConformalStagger::None, obj_bb, false);
    REQUIRE_FALSE(base_paths.empty());
    REQUIRE_FALSE(tip_paths.empty());
    dump_conformal_overlay("cone_none", base, base_paths, tip_paths);

    auto a0 = undirected_long_angles(base_paths, 1.5);
    auto a1 = undirected_long_angles(tip_paths, 1.0);
    REQUIRE(a0.size() >= 4);
    REQUIRE(a1.size() >= 4);
    size_t matched = 0;
    for (double a : a1) {
        double best = 180.;
        for (double b : a0)
            best = std::min(best, std::abs(a - b));
        if (best < 6.0)
            ++matched;
    }
    REQUIRE(matched >= a1.size() * 2 / 3);
}

TEST_CASE("Fill: conformal zigzag on a solid square still produces paths", "[Fill][Conformal]") {
    Points sq;
    for (const Vec2d &p : { Vec2d(0, 0), Vec2d(40, 0), Vec2d(40, 40), Vec2d(0, 40) })
        sq.push_back(Point::new_scale(p.x(), p.y()));
    ExPolygon square(sq);
    Polylines paths = fill_conformal(square, 0, ConformalStagger::None);
    REQUIRE_FALSE(paths.empty());
    for (const Polyline &pl : paths)
        REQUIRE(pl.size() >= 2);
    dump_conformal_overlay("square", square, paths, {});
}

TEST_CASE("Fill: conformal L-shape still produces paths", "[Fill][Conformal]") {
    Points pts;
    for (const Vec2d &p : { Vec2d(0, 0), Vec2d(40, 0), Vec2d(40, 10), Vec2d(10, 10), Vec2d(10, 40), Vec2d(0, 40) })
        pts.push_back(Point::new_scale(p.x(), p.y()));
    ExPolygon ell(pts);
    Polylines paths = fill_conformal(ell, 0, ConformalStagger::None, get_extents(ell), false);
    REQUIRE_FALSE(paths.empty());
    REQUIRE_FALSE(polylines_have_interior_crossing(paths));
    REQUIRE(count_long_segment_angle_bins(paths, 1.5, 20.0) >= 2);
    dump_conformal_overlay("l_shape", ell, paths, {});
}

TEST_CASE("Fill: conformal off-center hole is not centroid-polar", "[Fill][Conformal]") {
    Points outer;
    for (const Vec2d &p : { Vec2d(0, 0), Vec2d(80, 0), Vec2d(80, 24), Vec2d(0, 24) })
        outer.push_back(Point::new_scale(p.x(), p.y()));
    ExPolygon poly(outer);
    Polygon hole = make_regular_ngon(5., 24);
    const Point hole_c = Point::new_scale(18., 12.);
    for (Point &p : hole.points)
        p += hole_c;
    std::reverse(hole.points.begin(), hole.points.end());
    poly.holes.push_back(std::move(hole));

    Polylines paths = fill_conformal(poly, 0, ConformalStagger::None, get_extents(poly), false);
    REQUIRE_FALSE(paths.empty());
    dump_conformal_overlay("rect_offcenter_hole", poly, paths, {});
    // Old polar shot every diameter through the hole centroid. Radial uses the
    // bbox center (here equal to the outer centroid), so the hole is not a star.
    REQUIRE(fraction_lines_through_point(paths, hole_c, 2.0, 2.0) < 0.5);

    Polylines connected = fill_conformal(poly, 0, ConformalStagger::None, get_extents(poly), true);
    REQUIRE_FALSE(connected.empty());
    REQUIRE(connected.size() < paths.size());
    REQUIRE(connected.size() <= std::max(size_t(16), paths.size() / 2));
}

TEST_CASE("Fill: conformal infill processes a cube without throwing", "[Fill][Conformal]") {
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        { "sparse_infill_density", 20 },
        { "sparse_infill_pattern", "zigzag" },
        { "conformal_infill", 1 },
        { "conformal_stagger", "none" },
        { "top_shell_layers", 0 },
        { "bottom_shell_layers", 0 },
        { "wall_loops", 1 },
        { "gcode_comments", 1 }
    });
    Model model;
    ModelObject *object = model.add_object();
    object->name = "cube.stl";
    object->add_volume(make_cube(20., 20., 20.));
    object->add_instance();
    object->ensure_on_bed();
    Print print;
    print.auto_assign_extruders(object);
    print.apply(model, config);
    print.validate();
    print.set_status_silent();
    REQUIRE_NOTHROW(print.process());
    REQUIRE_FALSE(print.objects().empty());
    bool any_fill = false;
    for (const Layer *layer : print.objects().front()->layers())
        for (const LayerRegion *region : layer->regions())
            if (!region->fills.entities.empty())
                any_fill = true;
    REQUIRE(any_fill);
}

static ExPolygon make_bar(coordf_t len_mm, coordf_t width_mm)
{
    Points pts{
        Point::new_scale(0., 0.),
        Point::new_scale(len_mm, 0.),
        Point::new_scale(len_mm, width_mm),
        Point::new_scale(0., width_mm),
    };
    return ExPolygon(pts);
}

TEST_CASE("Fill: medial chart lofts open bars then closed rings", "[Fill][Conformal]") {
    const ExPolygon bar  = make_bar(60., 6.);
    const ExPolygon ring = make_annulus(20., 10.);
    std::vector<ExPolygon> islands;
    std::vector<double>    zs;
    islands.reserve(8);
    zs.reserve(8);
    for (int i = 0; i < 4; ++i) {
        islands.push_back(bar);
        zs.push_back(0.2 * double(i));
    }
    for (int i = 4; i < 8; ++i) {
        islands.push_back(ring);
        zs.push_back(0.2 * double(i));
    }

    auto chart = FillConformal::MedialChart::build(islands, zs, 0.4);
    REQUIRE(chart);
    REQUIRE(chart->layer_count() == 8);
    for (size_t i = 0; i < 4; ++i) {
        const FillConformal::LayerGenerator *g = chart->layer(i);
        REQUIRE(g != nullptr);
        REQUIRE_FALSE(g->closed);
        REQUIRE(g->polyline.size() >= 2);
    }
    for (size_t i = 4; i < 8; ++i) {
        const FillConformal::LayerGenerator *g = chart->layer(i);
        REQUIRE(g != nullptr);
        REQUIRE(g->closed);
        REQUIRE(g->polyline.size() >= 4);
    }

    const Point aligned0 = chart->layer(0)->polyline.front();
    const Point aligned1 = chart->layer(1)->polyline.front();
    const double aligned_start = (aligned1 - aligned0).cast<double>().norm();

    FillConformal::LayerGenerator g0, g1;
    REQUIRE(FillConformal::extract_generator(bar, 0.4, g0));
    REQUIRE(FillConformal::extract_generator(bar, 0.4, g1));
    g1.polyline.reverse();
    const double unaligned_start = (g1.polyline.front() - g0.polyline.front()).cast<double>().norm();
    REQUIRE(aligned_start < unaligned_start);
    REQUIRE(aligned_start < scale_(2.));
}

TEST_CASE("Fill: radial zigzag on synthetic annuli", "[Fill][Conformal]") {
    auto check_ring = [](const ExPolygon &ring, const char *name) {
        const BoundingBox obj_bb = get_extents(ring);
        Polylines connected = fill_conformal(ring, 0, ConformalStagger::None, obj_bb, true);
        REQUIRE_FALSE(connected.empty());
        for (const Polyline &pl : connected)
            for (const Point &pt : pl.points)
                REQUIRE(ring.contains(pt));
        REQUIRE_FALSE(polylines_have_interior_crossing(connected));
        REQUIRE(count_long_polylines(connected) <= 8);

        Polylines open = fill_conformal(ring, 0, ConformalStagger::None, obj_bb, false);
        REQUIRE(open.size() >= 3);
        dump_conformal_overlay(name, ring, connected, open);
    };

    check_ring(make_annulus(20., 10.), "radial_annulus");

    ExPolygon ecc = make_annulus(24., 8.);
    const Point shift = Point::new_scale(6., 2.);
    for (Point &p : ecc.holes.front().points)
        p += shift;
    check_ring(ecc, "radial_eccentric");
}

TEST_CASE("Fill: radial zigzag hops hug the annulus contour", "[Fill][Conformal]") {
    auto dist_to_poly_mm = [](const Polygon &poly, const Point &p) {
        double best = std::numeric_limits<double>::max();
        const Points &pts = poly.points;
        for (size_t i = 0; i < pts.size(); ++i) {
            const Point &a = pts[i];
            const Point &b = pts[(i + 1) % pts.size()];
            Vec2d ab = (b - a).cast<double>();
            const double len2 = ab.squaredNorm();
            double u = 0.;
            if (len2 > 1.)
                u = std::max(0., std::min(1., (p - a).cast<double>().dot(ab) / len2));
            const Point q = a + Point(coord_t(std::lround(ab.x() * u)), coord_t(std::lround(ab.y() * u)));
            best = std::min(best, (p - q).cast<double>().norm());
        }
        return unscale<double>(best);
    };
    ExPolygon ring = make_annulus(20., 10.);
    const BoundingBox obj_bb = get_extents(ring);
    Polylines connected = fill_conformal(ring, 0, ConformalStagger::None, obj_bb, true);
    Polylines open = fill_conformal(ring, 0, ConformalStagger::None, obj_bb, false);
    REQUIRE_FALSE(connected.empty());
    REQUIRE(open.size() >= 8);
    size_t n_pts = 0;
    int    n_contour_hops = 0;
    for (const Polyline &pl : connected) {
        n_pts += pl.points.size();
        for (size_t i = 1; i + 1 < pl.points.size(); ++i) {
            const double d_out = dist_to_poly_mm(ring.contour, pl.points[i]);
            const double d_in  = dist_to_poly_mm(ring.holes.front(), pl.points[i]);
            if (d_out < 0.8 || d_in < 0.8)
                ++n_contour_hops;
        }
    }
    // Inner hops still follow the hole (straight chord would cross empty space).
    // Outer hops are straight chords so the path stays continuous at the joints.
    REQUIRE(n_pts > 2 * open.size());
    REQUIRE(n_contour_hops >= 4);
    REQUIRE_FALSE(polylines_have_interior_crossing(connected));
}

TEST_CASE("Fill: radial zigzag on a solid disk", "[Fill][Conformal]") {
    ExPolygon disk;
    disk.contour = make_regular_ngon(20., 48);
    const BoundingBox obj_bb = get_extents(disk);

    Polylines connected = fill_conformal(disk, 0, ConformalStagger::None, obj_bb, true);
    REQUIRE_FALSE(connected.empty());
    for (const Polyline &pl : connected)
        for (const Point &pt : pl.points)
            REQUIRE(disk.contains(pt));
    REQUIRE_FALSE(polylines_have_interior_crossing(connected));
    REQUIRE(count_long_polylines(connected) <= 8);
    REQUIRE(count_long_segment_angle_bins(connected, 2.0, 20.0) >= 4);

    Polylines open = fill_conformal(disk, 0, ConformalStagger::None, obj_bb, false);
    REQUIRE(open.size() >= 8);
    REQUIRE(fraction_lines_through_point(open, obj_bb.center(), 2.0, 2.0) > 0.7);
    dump_conformal_overlay("radial_disk", disk, connected, open);
}

static double pt_radius_mm(const Point &pt, const Point &c)
{
    return unscale<double>((pt - c).cast<double>().norm());
}

static size_t count_angle_bins_in_ring(const Polylines &paths, const Point &c,
                                       double r0_mm, double r1_mm, double min_len_mm, double bin_deg)
{
    std::set<int> bins;
    const double  min_len = scale_(min_len_mm);
    for (const Polyline &pl : paths) {
        for (size_t i = 1; i < pl.points.size(); ++i) {
            Vec2d  d   = (pl.points[i] - pl.points[i - 1]).cast<double>();
            double len = d.norm();
            if (len < min_len)
                continue;
            const Point mid((pl.points[i].x() + pl.points[i - 1].x()) / 2,
                            (pl.points[i].y() + pl.points[i - 1].y()) / 2);
            const double r = pt_radius_mm(mid, c);
            if (r < r0_mm || r > r1_mm)
                continue;
            double deg = std::atan2(d.y(), d.x()) * 180. / PI;
            if (deg < 0)
                deg += 180.;
            bins.insert(int(std::floor(deg / bin_deg)));
        }
    }
    return bins.size();
}

static bool polyline_spans_hub_and_rim(const Polylines &paths, const Point &c, double hub_mm, double rim_mm)
{
    for (const Polyline &pl : paths) {
        bool in_hub = false, in_rim = false;
        for (const Point &pt : pl.points) {
            const double r = pt_radius_mm(pt, c);
            if (r < hub_mm)
                in_hub = true;
            if (r > rim_mm)
                in_rim = true;
        }
        if (in_hub && in_rim)
            return true;
    }
    return false;
}

// One-shot 大圆边: many consecutive vertices on a constant radius.
static double longest_hub_stroke_mm(const Polylines &pls, const Point &c, double r_mm, double tol_mm)
{
    double best = 0.;
    for (const Polyline &pl : pls) {
        if (pl.size() < 8)
            continue;
        double run = 0.;
        int    n_on = 0;
        for (size_t i = 0; i < pl.points.size(); ++i) {
            const bool on = std::abs(pt_radius_mm(pl.points[i], c) - r_mm) < tol_mm;
            if (on) {
                if (i > 0 && n_on > 0)
                    run += unscale<double>((pl.points[i] - pl.points[i - 1]).cast<double>().norm());
                ++n_on;
            } else {
                if (n_on >= 8)
                    best = std::max(best, run);
                run  = 0.;
                n_on = 0;
            }
        }
        if (n_on >= 8)
            best = std::max(best, run);
    }
    return best;
}

TEST_CASE("Fill: hub clip splits solid disk into rectilinear and radial", "[Fill][Conformal]") {
    ExPolygon disk;
    disk.contour = make_regular_ngon(20., 48);
    const BoundingBox obj_bb = get_extents(disk);
    const Point       c      = obj_bb.center();
    auto              filler = make_conformal_filler(disk, 0, obj_bb);
    FillParams        params = make_conformal_params();
    params.conformal_hub_radius = 6.f;
    Surface surface(stInternal, disk);
    Polylines paths = filler->fill_surface(&surface, params);
    REQUIRE_FALSE(paths.empty());
    dump_conformal_overlay("radial_disk_hub", disk, paths, Polylines{});
    for (const Polyline &pl : paths)
        for (const Point &pt : pl.points)
            REQUIRE(disk.contains(pt));
    REQUIRE_FALSE(polyline_spans_hub_and_rim(paths, c, 3.5, 10.0));
    REQUIRE(count_angle_bins_in_ring(paths, c, 0.0, 4.5, 1.5, 20.0) <= 3);
    REQUIRE(count_angle_bins_in_ring(paths, c, 10.0, 22.0, 2.0, 20.0) >= 4);
    // Geometric r=6 circle is a gap; do not trace a circular 圆边 there.
    double on_circle = 0.;
    const double min_len = scale_(0.25);
    for (const Polyline &pl : paths) {
        for (size_t i = 1; i < pl.points.size(); ++i) {
            Vec2d d = (pl.points[i] - pl.points[i - 1]).cast<double>();
            if (d.norm() < min_len)
                continue;
            const Point mid((pl.points[i].x() + pl.points[i - 1].x()) / 2,
                            (pl.points[i].y() + pl.points[i - 1].y()) / 2);
            const double r = pt_radius_mm(mid, c);
            if (r >= 5.8 && r <= 6.2)
                on_circle += unscale<double>(d.norm());
        }
    }
    REQUIRE(on_circle < 2.0);
}

TEST_CASE("Fill: hub clip is a circle", "[Fill][Conformal]") {
    ExPolygon disk;
    disk.contour = make_regular_ngon(20., 48);
    const BoundingBox obj_bb = get_extents(disk);
    auto              filler = make_conformal_filler(disk, 0, obj_bb);
    FillParams        params = make_conformal_params();
    params.conformal_hub_radius = 6.f;
    Point     pole;
    ExPolygon hdisk;
    REQUIRE(FillRadialZigZag::make_hub_disk(disk, *filler, params, pole, hdisk));
    REQUIRE(hdisk.contour.size() >= 16);
    const double half = 0.5 * unscale<double>(get_extents(hdisk).size().x());
    REQUIRE(std::abs(half - 6.0) < 0.15);
}

TEST_CASE("Fill: hub clip does not stroke circle B on an open C", "[Fill][Conformal]") {
    ExPolygon ring = make_annulus(40., 6.);
    Polygon wedge;
    wedge.points.push_back(Point::new_scale(0., 0.));
    const double a0 = -40. * PI / 180.;
    const double a1 =  40. * PI / 180.;
    for (int i = 0; i <= 8; ++i) {
        const double a = a0 + (a1 - a0) * double(i) / 8.;
        wedge.points.push_back(Point::new_scale(50. * std::cos(a), 50. * std::sin(a)));
    }
    ExPolygons cands = diff_ex(ExPolygons{ ring }, ExPolygons{ ExPolygon(wedge) });
    REQUIRE_FALSE(cands.empty());
    ExPolygon c = cands.front();
    for (const ExPolygon &ex : cands)
        if (std::abs(ex.area()) > std::abs(c.area()))
            c = ex;
    const BoundingBox obj_bb = get_extents(c);
    const Point       origin = Point::new_scale(0., 0.);
    auto              filler = make_conformal_filler(c, 0, obj_bb);
    FillParams        params = make_conformal_params();
    params.conformal_hub_radius = 14.f;
    Surface   surface(stInternal, c);
    Polylines paths = filler->fill_surface(&surface, params);
    REQUIRE_FALSE(paths.empty());
    dump_conformal_overlay("radial_c_hub_nostroke", c, paths, Polylines{});
    // Inner 铆线 are 2-point chords. A one-shot 大圆边 has 8+ verts on circle B.
    const double hug = longest_hub_stroke_mm(paths, origin, 14.4, 1.6);
    INFO("hub stroke mm " << hug);
    REQUIRE(hug < 12.0);
}

TEST_CASE("Fill: hub auto formula clips a solid disk", "[Fill][Conformal]") {
    ExPolygon disk;
    disk.contour = make_regular_ngon(20., 48);
    const BoundingBox obj_bb = get_extents(disk);
    auto              filler = make_conformal_filler(disk, 0, obj_bb);
    FillParams        params = make_conformal_params();
    params.conformal_hub_radius = 0.f;
    Point     pole;
    ExPolygon hdisk;
    REQUIRE(FillRadialZigZag::make_hub_disk(disk, *filler, params, pole, hdisk));
    REQUIRE_FALSE(intersection_ex(disk, hdisk).empty());
    Surface   surface(stInternal, disk);
    Polylines paths = filler->fill_surface(&surface, params);
    REQUIRE_FALSE(paths.empty());
    dump_conformal_overlay("radial_disk_hub_auto", disk, paths, Polylines{});
}

TEST_CASE("Fill: radial zigzag on a U shape with an open side", "[Fill][Conformal]") {
    Points pts;
    for (const Vec2d &p : {
            Vec2d(0, 0), Vec2d(40, 0), Vec2d(40, 10), Vec2d(12, 10),
            Vec2d(12, 30), Vec2d(40, 30), Vec2d(40, 40), Vec2d(0, 40)
        })
        pts.push_back(Point::new_scale(p.x(), p.y()));
    ExPolygon u(pts);
    const BoundingBox obj_bb = get_extents(u);
    Polylines connected = fill_conformal(u, 0, ConformalStagger::None, obj_bb, true);
    REQUIRE_FALSE(connected.empty());
    for (const Polyline &pl : connected)
        for (const Point &pt : pl.points)
            REQUIRE(u.contains(pt));
    REQUIRE_FALSE(polylines_have_interior_crossing(connected));
    // Open U: hops must stitch adjacent rays. Extra short polylines are the
    // other-rim 铆线. A missing zigzag 铆线 splits into many long paths.
    REQUIRE(count_long_polylines(connected) <= 4);
    Polylines open = fill_conformal(u, 0, ConformalStagger::None, obj_bb, false);
    REQUIRE(open.size() >= 6);
    dump_conformal_overlay("radial_u_open", u, connected, open);
}

TEST_CASE("Fill: radial zigzag hops stay linked on a dented C", "[Fill][Conformal]") {
    // Open C (annulus minus a wedge) with a concave bite on the outer rim.
    // Vertex-mean radius used to pick the long way around the bite, dropping hops.
    ExPolygon ring = make_annulus(40., 28.);
    Polygon wedge;
    wedge.points.push_back(Point::new_scale(0., 0.));
    const double a0 = -28. * PI / 180.;
    const double a1 =  28. * PI / 180.;
    for (int i = 0; i <= 8; ++i) {
        const double a = a0 + (a1 - a0) * double(i) / 8.;
        wedge.points.push_back(Point::new_scale(52. * std::cos(a), 52. * std::sin(a)));
    }
    ExPolygons cands = diff_ex(ExPolygons{ ring }, ExPolygons{ ExPolygon(wedge) });
    ExPolygon dent;
    dent.contour = make_regular_ngon(9., 24);
    dent.translate(Point::new_scale(-40., 0.));
    cands = diff_ex(cands, ExPolygons{ dent });
    REQUIRE_FALSE(cands.empty());
    ExPolygon c = cands.front();
    for (const ExPolygon &ex : cands)
        if (std::abs(ex.area()) > std::abs(c.area()))
            c = ex;
    REQUIRE(c.holes.empty());
    const BoundingBox obj_bb = get_extents(c);
    Polylines connected = fill_conformal(c, 0, ConformalStagger::None, obj_bb, true);
    Polylines open = fill_conformal(c, 0, ConformalStagger::None, obj_bb, false);
    REQUIRE_FALSE(connected.empty());
    REQUIRE(open.size() >= 8);
    for (const Polyline &pl : connected)
        for (const Point &pt : pl.points)
            REQUIRE(c.contains(pt));
    REQUIRE_FALSE(polylines_have_interior_crossing(connected));
    REQUIRE(count_long_polylines(connected) <= 4);
    dump_conformal_overlay("radial_c_dent", c, connected, open);
}

TEST_CASE("Fill: radial n lock uses the max section", "[Fill][Conformal]") {
    // Unlocked, these rings want different even n. Locked, both use the larger n.
    ExPolygon a = make_annulus(20.0, 10.0);
    ExPolygon b = make_annulus(22.0, 11.0);
    const BoundingBox bb = get_extents(a);

    FillRadialZigZag::reset_n_lock();
    FillRadialZigZag::debug_clear();
    FillRadialZigZag::debug_enable(true);
    (void) fill_conformal(a, 0, ConformalStagger::None, bb, true);
    (void) fill_conformal(b, 1, ConformalStagger::None, bb, true);
    FillRadialZigZag::debug_enable(false);
    const auto unlocked = FillRadialZigZag::debug_snapshot();
    REQUIRE(unlocked.size() >= 2);
    REQUIRE(unlocked[0].success);
    REQUIRE(unlocked[1].success);
    REQUIRE(unlocked[0].n_scan != unlocked[1].n_scan);
    const int n_max = std::max(unlocked[0].n_scan, unlocked[1].n_scan);

    FillRadialZigZag::reset_n_lock();
    FillRadialZigZag::pin_scan_counts({ { a }, { b } }, 2.0);
    FillRadialZigZag::debug_clear();
    FillRadialZigZag::debug_enable(true);
    (void) fill_conformal(a, 0, ConformalStagger::None, bb, true);
    (void) fill_conformal(b, 1, ConformalStagger::None, bb, true);
    FillRadialZigZag::debug_enable(false);
    const auto locked = FillRadialZigZag::debug_snapshot();
    FillRadialZigZag::reset_n_lock();
    REQUIRE(locked.size() >= 2);
    REQUIRE(locked[0].success);
    REQUIRE(locked[1].success);
    REQUIRE(locked[0].n_scan == locked[1].n_scan);
    REQUIRE(locked[0].n_scan == n_max);
    REQUIRE(locked[1].n_lock == n_max);
}

TEST_CASE("Fill: radial pole lock holds across bbox jitter", "[Fill][Conformal]") {
    ExPolygon a = make_annulus(20.0, 10.0);
    ExPolygon b = a;
    b.translate(scale_(2.0), 0.);
    const BoundingBox bb = get_extents(a);

    FillRadialZigZag::reset_n_lock();
    FillRadialZigZag::debug_clear();
    FillRadialZigZag::debug_enable(true);
    (void) fill_conformal(a, 0, ConformalStagger::None, bb, true);
    (void) fill_conformal(b, 1, ConformalStagger::None, bb, true);
    FillRadialZigZag::debug_enable(false);
    const auto unlocked = FillRadialZigZag::debug_snapshot();
    REQUIRE(unlocked.size() >= 2);
    REQUIRE(unlocked[0].success);
    REQUIRE(unlocked[1].success);
    REQUIRE(std::abs(unlocked[1].pole_x - unlocked[0].pole_x) > 1.5);

    FillRadialZigZag::reset_n_lock();
    FillRadialZigZag::pin_scan_counts({ { a }, { b } }, 2.0);
    FillRadialZigZag::debug_clear();
    FillRadialZigZag::debug_enable(true);
    (void) fill_conformal(a, 0, ConformalStagger::None, bb, true);
    (void) fill_conformal(b, 1, ConformalStagger::None, bb, true);
    FillRadialZigZag::debug_enable(false);
    const auto locked = FillRadialZigZag::debug_snapshot();
    FillRadialZigZag::reset_n_lock();
    REQUIRE(locked.size() >= 2);
    REQUIRE(locked[0].success);
    REQUIRE(locked[1].success);
    REQUIRE(std::abs(locked[1].pole_x - locked[0].pole_x) < 0.7);
}

TEST_CASE("Fill: radial axis pole follows fitted 3D line", "[Fill][Conformal]") {
    ExPolygon a = make_annulus(20.0, 10.0);
    ExPolygon b = a;
    b.translate(scale_(4.0), scale_(6.0));
    ExPolygon c = a;
    c.translate(scale_(8.0), 0.);
    BoundingBox bb = get_extents(a);
    bb.merge(get_extents(b));
    bb.merge(get_extents(c));

    FillRadialZigZag::reset_n_lock();
    FillRadialZigZag::pin_scan_counts({ { a }, { b }, { c } }, 2.0, { 0., 10., 20. });
    FillRadialZigZag::debug_clear();
    FillRadialZigZag::debug_enable(true);
    REQUIRE_FALSE(fill_conformal(a, 0, ConformalStagger::None, bb, true, ConformalPole::Axis, 0.).empty());
    REQUIRE_FALSE(fill_conformal(b, 1, ConformalStagger::None, bb, true, ConformalPole::Axis, 10.).empty());
    REQUIRE_FALSE(fill_conformal(c, 2, ConformalStagger::None, bb, true, ConformalPole::Axis, 20.).empty());
    FillRadialZigZag::debug_enable(false);
    const auto diags = FillRadialZigZag::debug_snapshot();
    FillRadialZigZag::reset_n_lock();
    REQUIRE(diags.size() >= 3);
    REQUIRE(diags[0].success);
    REQUIRE(diags[1].success);
    REQUIRE(diags[2].success);
    // Least-squares of centroids (0,0), (4,6), (8,0) vs z=0/10/20 is x=0.4 z, y=2.
    // Layer-mode would track the raw (4,6) hole; axis mode must stay on the line.
    REQUIRE(std::abs(diags[1].pole_x - 4.0) < 0.6);
    REQUIRE(std::abs(diags[1].pole_y - 2.0) < 0.6);
    REQUIRE(std::abs(diags[1].pole_y - 6.0) > 2.0);
}

TEST_CASE("Fill: radial bezier pole damps bbox-center jumps", "[Fill][Conformal]") {
    // Five concentric rings. Bounding-box centers track the translations.
    // Four sit on y=0; the middle one jumps +8 mm. A quadratic Bezier in z
    // cannot follow that spike, so the pole stays near the smooth midline.
    std::vector<ExPolygon> rings;
    std::vector<double>    zs { 0., 5., 10., 15., 20. };
    const double dx[] = { 0., 2., 4., 6., 8. };
    const double dy[] = { 0., 0., 8., 0., 0. };
    BoundingBox bb;
    for (int i = 0; i < 5; ++i) {
        ExPolygon r = make_annulus(20.0, 10.0);
        r.translate(scale_(dx[i]), scale_(dy[i]));
        bb.merge(get_extents(r));
        rings.push_back(std::move(r));
    }

    FillRadialZigZag::reset_n_lock();
    FillRadialZigZag::pin_scan_counts(
        { { rings[0] }, { rings[1] }, { rings[2] }, { rings[3] }, { rings[4] } }, 2.0, zs);
    FillRadialZigZag::debug_clear();
    FillRadialZigZag::debug_enable(true);
    for (int i = 0; i < 5; ++i)
        REQUIRE_FALSE(fill_conformal(rings[size_t(i)], size_t(i), ConformalStagger::None, bb, true,
                                     ConformalPole::Bezier, zs[size_t(i)])
                          .empty());
    FillRadialZigZag::debug_enable(false);
    const auto diags = FillRadialZigZag::debug_snapshot();
    FillRadialZigZag::reset_n_lock();
    REQUIRE(diags.size() >= 5);
    for (size_t i = 0; i < 5; ++i)
        REQUIRE(diags[i].success);
    REQUIRE(std::abs(diags[2].pole_x - 4.0) < 1.0);
    REQUIRE(std::abs(diags[2].pole_y) < 4.0);
    REQUIRE(std::abs(diags[2].pole_y - 8.0) > 3.0);
    REQUIRE(std::abs(diags[2].pole_y - diags[1].pole_y) < 4.0);
    REQUIRE(std::abs(diags[2].pole_y - diags[3].pole_y) < 4.0);
}
