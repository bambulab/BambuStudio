include <BOSL/math.scad>
include <BOSL/convex_hull.scad>


testpoints_on_sphere = [ for(p = 
	[
		[1,PHI,0], [-1,PHI,0], [1,-PHI,0], [-1,-PHI,0],
		[0,1,PHI], [0,-1,PHI], [0,1,-PHI], [0,-1,-PHI],
		[PHI,0,1], [-PHI,0,1], [PHI,0,-1], [-PHI,0,-1]
	])
	normalize(p)
];

testpoints_circular = [ for(a = [0:15:360-EPSILON]) [cos(a),sin(a)] ];

testpoints_coplanar = let(u = normalize([1,3,7]), v = normalize([-2,1,-2])) [ for(i = [1:10]) rands(-1,1,1)[0] * u + rands(-1,1,1)[0] * v ];

testpoints_collinear_2d = let(u = normalize([5,3]))    [ for(i = [1:20]) rands(-1,1,1)[0] * u ];
testpoints_collinear_3d = let(u = normalize([5,3,-5])) [ for(i = [1:20]) rands(-1,1,1)[0] * u ];

testpoints2d = 20 * [for (i = [1:10]) concat(rands(-1,1,2))];
testpoints3d = 20 * [for (i = [1:50]) concat(rands(-1,1,3))];

// All points are on the sphere, no point should be red
translate([-50,0]) visualize_hull(20*testpoints_on_sphere);

// 2D points
translate([50,0]) visualize_hull(testpoints2d);

// All points on a circle, no point should be red
translate([0,50]) visualize_hull(20*testpoints_circular);

// All points 3d but collinear
translate([0,-50]) visualize_hull(20*testpoints_coplanar);

// Collinear
translate([50,50]) visualize_hull(20*testpoints_collinear_2d);

// Collinear
translate([-50,50]) visualize_hull(20*testpoints_collinear_3d);

// 3D points
visualize_hull(testpoints3d);


module visualize_hull(points) {
	hull = convex_hull(points);
	
	%if (len(hull) > 0 && is_list(hull[0]) && len(hull[0]) > 0)
		polyhedron(points=points, faces = hull);
	else
		polyhedron(points=points, faces = [hull]);
	
	for (i = [0:len(points)-1]) {
		p = points[i];
		$fn = 16;
		translate(p) {
			if (hull_contains_index(hull,i)) {
				color("blue") sphere(1);
			} else {
				color("red") sphere(1);
			}
		}
	}
	
	function hull_contains_index(hull, index) = 
		search(index,hull,1,0) ||
		search(index,hull,1,1) ||
		search(index,hull,1,2);
}


// vim: noexpandtab tabstop=4 shiftwidth=4 softtabstop=4 nowrap
