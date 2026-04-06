//////////////////////////////////////////////////////////////////////
// LibFile: paths.scad
//   Polylines, polygons and paths.
//   To use, add the following lines to the beginning of your file:
//   ```
//   include <BOSL/constants.scad>
//   use <BOSL/paths.scad>
//   ```
//////////////////////////////////////////////////////////////////////

/*
BSD 2-Clause License

Copyright (c) 2017, Revar Desmera
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/


include <constants.scad>
use <transforms.scad>
use <math.scad>
use <quaternions.scad>
use <triangulation.scad>


// Section: Functions


// Function: simplify2d_path()
// Description:
//   Takes a 2D polyline and removes unnecessary collinear points.
// Usage:
//   simplify2d_path(path, [eps])
// Arguments:
//   path = A list of 2D path points.
//   eps = Largest angle delta between segments to count as colinear.  Default: 1e-6
function simplify2d_path(path, eps=1e-6) = simplify_path(path, eps=eps);


// Function: simplify3d_path()
// Description:
//   Takes a 3D polyline and removes unnecessary collinear points.
// Usage:
//   simplify3d_path(path, [eps])
// Arguments:
//   path = A list of 3D path points.
//   eps = Largest angle delta between segments to count as colinear.  Default: 1e-6
function simplify3d_path(path, eps=1e-6) = simplify_path(path, eps=eps);


// Function: path_length()
// Usage:
//   path3d_length(path)
// Description:
//   Returns the length of the path.
// Arguments:
//   path = The list of points of the path to measure.
// Example:
//   path = [[0,0], [5,35], [60,-25], [80,0]];
//   echo(path_length(path));
function path_length(path) =
	len(path)<2? 0 :
	sum([for (i = [0:len(path)-2]) norm(path[i+1]-path[i])]);


// Function: path2d_regular_ngon()
// Description:
//   Returns a 2D open counter-clockwise path of the vertices of a regular polygon of `n` sides.
// Usage:
//   path2d_regular_ngon(n, r|d, [cp], [scale]);
// Arguments:
//   n = Number of polygon sides.
//   r = Radius of regular polygon.
//   d = Radius of regular polygon.
//   cp = Centerpoint of regular polygon. Default: `[0,0]`
//   scale = [X,Y] scaling factors for each axis.  Default: `[1,1]`
// Example(2D):
//   trace_polyline(path2d_regular_ngon(n=12, r=50), N=1, showpts=true);
function path2d_regular_ngon(n=6, r=undef, d=undef, cp=[0,0], scale=[1,1]) =
	let(
		rr=get_radius(r=r, d=d, dflt=100)
	) [
		for (i=[0:n-1])
			rr * [cos(i*360/n)*scale.x, sin(i*360/n)*scale.y] + cp
	];


// Function: path3d_spiral()
// Description:
//   Returns a 3D spiral path.
// Usage:
//   path3d_spiral(turns, h, n, r|d, [cp], [scale]);
// Arguments:
//   h = Height of spiral.
//   turns = Number of turns in spiral.
//   n = Number of spiral sides.
//   r = Radius of spiral.
//   d = Radius of spiral.
//   cp = Centerpoint of spiral. Default: `[0,0]`
//   scale = [X,Y] scaling factors for each axis.  Default: `[1,1]`
// Example(3D):
//   trace_polyline(path3d_spiral(turns=2.5, h=100, n=24, r=50), N=1, showpts=true);
function path3d_spiral(turns=3, h=100, n=12, r=undef, d=undef, cp=[0,0], scale=[1,1]) = let(
		rr=get_radius(r=r, d=d, dflt=100),
		cnt=floor(turns*n),
		dz=h/cnt
	) [
		for (i=[0:cnt]) [
			rr * cos(i*360/n) * scale.x + cp.x,
			rr * sin(i*360/n) * scale.y + cp.y,
			i*dz
		]
	];


// Function: points_along_path3d()
// Usage:
//   points_along_path3d(polyline, path);
// Description:
//   Calculates the vertices needed to create a `polyhedron()` of the
//   extrusion of `polyline` along `path`.  The closed 2D path shold be
//   centered on the XY plane. The 2D path is extruded perpendicularly
//   along the 3D path.  Produces a list of 3D vertices.  Vertex count
//   is `len(polyline)*len(path)`.  Gives all the reoriented vertices
//   for `polyline` at the first point in `path`, then for the second,
//   and so on.
// Arguments:
//   polyline = A closed list of 2D path points.
//   path = A list of 3D path points.
function points_along_path3d(
	polyline,  // The 2D polyline to drag along the 3D path.
	path,  // The 3D polyline path to follow.
	q=Q_Ident(),  // Used in recursion
	n=0  // Used in recursion
) = let(
	end = len(path)-1,
	v1 = (n == 0)?  [0, 0, 1] : normalize(path[n]-path[n-1]),
	v2 = (n == end)? normalize(path[n]-path[n-1]) : normalize(path[n+1]-path[n]),
	crs = cross(v1, v2),
	axis = norm(crs) <= 0.001? [0, 0, 1] : crs,
	ang = vector_angle(v1, v2),
	hang = ang * (n==0? 1.0 : 0.5),
	hrot = Quat(axis, hang),
	arot = Quat(axis, ang),
	roth = Q_Mul(hrot, q),
	rotm = Q_Mul(arot, q)
) concat(
	[for (i = [0:len(polyline)-1]) Q_Rot_Vector(point3d(polyline[i]),roth) + path[n]],
	(n == end)? [] : points_along_path3d(polyline, path, rotm, n+1)
);



// Section: 2D Modules


// Module: modulated_circle()
// Description:
//   Creates a 2D polygon circle, modulated by one or more superimposed sine waves.
// Arguments:
//   r = radius of the base circle.
//   sines = array of [amplitude, frequency] pairs, where the frequency is the number of times the cycle repeats around the circle.
// Example(2D):
//   modulated_circle(r=40, sines=[[3, 11], [1, 31]], $fn=6);
module modulated_circle(r=40, sines=[10])
{
	freqs = len(sines)>0? [for (i=sines) i[1]] : [5];
	points = [
		for (a = [0 : (360/segs(r)/max(freqs)) : 360])
			let(nr=r+sum_of_sines(a,sines)) [nr*cos(a), nr*sin(a)]
	];
	polygon(points);
}


// Section: 3D Modules


// Module: extrude_from_to()
// Description:
//   Extrudes a 2D shape between the points pt1 and pt2.  Takes as children a set of 2D shapes to extrude.
// Arguments:
//   pt1 = starting point of extrusion.
//   pt2 = ending point of extrusion.
//   convexity = max number of times a line could intersect a wall of the 2D shape being extruded.
//   twist = number of degrees to twist the 2D shape over the entire extrusion length.
//   scale = scale multiplier for end of extrusion compared the start.
//   slices = Number of slices along the extrusion to break the extrusion into.  Useful for refining `twist` extrusions.
// Example(FlatSpin):
//   extrude_from_to([0,0,0], [10,20,30], convexity=4, twist=360, scale=3.0, slices=40) {
//       xspread(3) circle(3, $fn=32);
//   }
module extrude_from_to(pt1, pt2, convexity=undef, twist=undef, scale=undef, slices=undef) {
	rtp = xyz_to_spherical(pt2-pt1);
	translate(pt1) {
		rotate([0, rtp[2], rtp[1]]) {
			linear_extrude(height=rtp[0], convexity=convexity, center=false, slices=slices, twist=twist, scale=scale) {
				children();
			}
		}
	}
}



// Module: extrude_2d_hollow()
// Description:
//   Similar to linear_extrude(), except the result is a hollow shell.
// Arguments:
//   wall = thickness of shell wall.
//   height = height of extrusion.
//   twist = degrees of twist, from bottom to top.
//   slices = how many slices to use when making extrusion.
// Example:
//   extrude_2d_hollow(wall=2, height=100, twist=90, slices=50)
//       circle(r=40, $fn=6);
module extrude_2d_hollow(wall=2, height=50, twist=90, slices=60, center=undef, orient=ORIENT_Z, align=V_UP)
{
	orient_and_align([0,0,height], orient, align, center) {
		linear_extrude(height=height, twist=twist, slices=slices, center=true) {
			difference() {
				children();
				offset(r=-wall) {
					children();
				}
			}
		}
	}
}


// Module: extrude_2dpath_along_spiral()
// Description:
//   Takes a closed 2D polyline path, centered on the XY plane, and
//   extrudes it along a 3D spiral path of a given radius, height and twist.
// Arguments:
//   polyline = Array of points of a polyline path, to be extruded.
//   h = height of the spiral to extrude along.
//   r = radius of the spiral to extrude along.
//   twist = number of degrees of rotation to spiral up along height.
// Example:
//   poly = [[-10,0], [-3,-5], [3,-5], [10,0], [0,-30]];
//   extrude_2dpath_along_spiral(poly, h=200, r=50, twist=1080, $fn=36);
module extrude_2dpath_along_spiral(polyline, h, r, twist=360, center=undef, orient=ORIENT_Z, align=V_CENTER) {
	pline_count = len(polyline);
	steps = ceil(segs(r)*(twist/360));

	poly_points = [
		for (
			p = [0:steps]
		) let (
			a = twist * (p/steps),
			dx = r*cos(a),
			dy = r*sin(a),
			dz = h * (p/steps),
			pts = matrix4_apply(
				polyline, [
					matrix4_xrot(90),
					matrix4_zrot(a),
					matrix4_translate([dx, dy, dz])
				]
			)
		) for (pt = pts) pt
	];

	poly_faces = concat(
		[[for (b = [0:pline_count-1]) b]],
		[
			for (
				p = [0:steps-1],
				b = [0:pline_count-1],
				i = [0:1]
			) let (
				b2 = (b == pline_count-1)? 0 : b+1,
				p0 = p * pline_count + b,
				p1 = p * pline_count + b2,
				p2 = (p+1) * pline_count + b2,
				p3 = (p+1) * pline_count + b,
				pt = (i==0)? [p0, p2, p1] : [p0, p3, p2]
			) pt
		],
		[[for (b = [pline_count-1:-1:0]) b+(steps)*pline_count]]
	);

	tri_faces = triangulate_faces(poly_points, poly_faces);
	orient_and_align([r,r,h], orient, align, center) {
		polyhedron(points=poly_points, faces=tri_faces, convexity=10);
	}
}


// Module: extrude_2dpath_along_3dpath()
// Description:
//   Takes a closed 2D path `polyline`, centered on the XY plane, and extrudes it perpendicularly along a 3D path `path`, forming a solid.
// Arguments:
//   polyline = Array of points of a polyline path, to be extruded.
//   path = Array of points of a polyline path, to extrude along.
//   ang = Angle in degrees to rotate 2D polyline before extrusion.
//   convexity = max number of surfaces any single ray could pass through.
// Example(FlatSpin):
//   shape = [[0,-10], [5,-3], [5,3], [0,10], [30,0]];
//   path = concat(
//       [for (a=[30:30:180]) [50*cos(a)+50, 50*sin(a), 20*sin(a)]],
//       [for (a=[330:-30:180]) [50*cos(a)-50, 50*sin(a), 20*sin(a)]]
//   );
//   extrude_2dpath_along_3dpath(shape, path, ang=140);
module extrude_2dpath_along_3dpath(polyline, path, ang=0, convexity=10) {
	pline_count = len(polyline);
	path_count = len(path);

	polyline = rotate_points2d(path2d(polyline), ang);
	poly_points = points_along_path3d(polyline, path);

	poly_faces = concat(
		[[for (b = [0:pline_count-1]) b]],
		[
			for (
				p = [0:path_count-2],
				b = [0:pline_count-1],
				i = [0:1]
			) let (
				b2 = (b == pline_count-1)? 0 : b+1,
				p0 = p * pline_count + b,
				p1 = p * pline_count + b2,
				p2 = (p+1) * pline_count + b2,
				p3 = (p+1) * pline_count + b,
				pt = (i==0)? [p0, p2, p1] : [p0, p3, p2]
			) pt
		],
		[[for (b = [pline_count-1:-1:0]) b+(path_count-1)*pline_count]]
	);

	tri_faces = triangulate_faces(poly_points, poly_faces);
	polyhedron(points=poly_points, faces=tri_faces, convexity=convexity);
}



// Module: extrude_2d_shapes_along_3dpath()
// Description:
//   Extrudes 2D children along a 3D polyline path.  This may be slow.
// Arguments:
//   path = array of points for the bezier path to extrude along.
//   convexity = maximum number of walls a ran can pass through.
//   clipsize = increase if artifacts are left.  Default: 1000
// Example(FlatSpin):
//   path = [ [0, 0, 0], [33, 33, 33], [66, 33, 40], [100, 0, 0], [150,0,0] ];
//   extrude_2d_shapes_along_3dpath(path) circle(r=10, $fn=6);
module extrude_2d_shapes_along_3dpath(path, convexity=10, clipsize=100) {
	function polyquats(path, q=Q_Ident(), v=[0,0,1], i=0) = let(
			v2 = path[i+1] - path[i],
			ang = vector_angle(v,v2),
			axis = ang>0.001? normalize(cross(v,v2)) : [0,0,1],
			newq = Q_Mul(Quat(axis, ang), q),
			dist = norm(v2)
		) i < (len(path)-2)?
			concat([[dist, newq, ang]], polyquats(path, newq, v2, i+1)) :
			[[dist, newq, ang]];

	epsilon = 0.0001;  // Make segments ever so slightly too long so they overlap.
	ptcount = len(path);
	pquats = polyquats(path);
	for (i = [0 : ptcount-2]) {
		pt1 = path[i];
		pt2 = path[i+1];
		dist = pquats[i][0];
		q = pquats[i][1];
		difference() {
			translate(pt1) {
				Qrot(q) {
					down(clipsize/2/2) {
						linear_extrude(height=dist+clipsize/2, convexity=convexity) {
							children();
						}
					}
				}
			}
			translate(pt1) {
				hq = (i > 0)? Q_Slerp(q, pquats[i-1][1], 0.5) : q;
				Qrot(hq) down(clipsize/2+epsilon) cube(clipsize, center=true);
			}
			translate(pt2) {
				hq = (i < ptcount-2)? Q_Slerp(q, pquats[i+1][1], 0.5) : q;
				Qrot(hq) up(clipsize/2+epsilon) cube(clipsize, center=true);
			}
		}
	}
}


// Module: trace_polyline()
// Description:
//   Renders lines between each point of a polyline path.
//   Can also optionally show the individual vertex points.
// Arguments:
//   pline = The array of points in the polyline.
//   showpts = If true, draw vertices and control points.
//   N = Mark the first and every Nth vertex after in a different color and shape.
//   size = Diameter of the lines drawn.
//   color = Color to draw the lines (but not vertices) in.
// Example(FlatSpin):
//   polyline = [for (a=[0:30:210]) 10*[cos(a), sin(a), sin(a)]];
//   trace_polyline(polyline, showpts=true, size=0.5, color="lightgreen");
module trace_polyline(pline, N=1, showpts=false, size=1, color="yellow") {
	if (showpts) {
		for (i = [0:len(pline)-1]) {
			translate(pline[i]) {
				if (i%N == 0) {
					color("blue") sphere(d=size*2.5, $fn=8);
				} else {
					color("red") {
						cylinder(d=size/2, h=size*3, center=true, $fn=8);
						xrot(90) cylinder(d=size/2, h=size*3, center=true, $fn=8);
						yrot(90) cylinder(d=size/2, h=size*3, center=true, $fn=8);
					}
				}
			}
		}
	}
	for (i = [0:len(pline)-2]) {
		if (N!=3 || (i%N) != 1) {
			color(color) extrude_from_to(pline[i], pline[i+1]) circle(d=size/2);
		}
	}
}


// Module: debug_polygon()
// Description: A drop-in replacement for `polygon()` that renders and labels the path points.
// Arguments:
//   points = The array of 2D polygon vertices.
//   paths = The path connections between the vertices.
//   convexity = The max number of walls a ray can pass through the given polygon paths.
// Example(2D):
//   debug_polygon(
//       points=concat(
//           path2d_regular_ngon(r=10, n=8),
//           path2d_regular_ngon(r=8, n=8)
//       ),
//       paths=[
//           [for (i=[0:7]) i],
//           [for (i=[15:-1:8]) i]
//       ]
//   );
module debug_polygon(points, paths=undef, convexity=2, size=1)
{
	pths = (!is_def(paths))? [for (i=[0:len(points)-1]) i] : is_scalar(paths[0])? [paths] : paths;
	echo(points=points);
	echo(paths=paths);
	linear_extrude(height=0.01, convexity=convexity, center=true) {
		polygon(points=points, paths=paths, convexity=convexity);
	}
	for (i = [0:len(points)-1]) {
		color("red") {
			up(0.2) {
				translate(points[i]) {
					linear_extrude(height=0.1, convexity=10, center=true) {
						text(text=str(i), size=size, halign="center", valign="center");
					}
				}
			}
		}
	}
	for (j = [0:len(paths)-1]) {
		path = paths[j];
		translate(points[path[0]]) {
			color("cyan") up(0.1) cylinder(d=size*1.5, h=0.01, center=false, $fn=12);
		}
		translate(points[path[len(path)-1]]) {
			color("pink") up(0.11) cylinder(d=size*1.5, h=0.01, center=false, $fn=4);
		}
		for (i = [0:len(path)-1]) {
			midpt = (points[path[i]] + points[path[(i+1)%len(path)]])/2;
			color("blue") {
				up(0.2) {
					translate(midpt) {
						linear_extrude(height=0.1, convexity=10, center=true) {
							text(text=str(chr(65+j),i), size=size/2, halign="center", valign="center");
						}
					}
				}
			}
		}
	}
}



// vim: noexpandtab tabstop=4 shiftwidth=4 softtabstop=4 nowrap
