//////////////////////////////////////////////////////////////////////
// LibFile: convex_hull.scad
//   Functions to create 2D and 3D convex hulls.
//   To use, add the following line to the beginning of your file:
//   ```
//   include <BOSL/convex_hull.scad>
//   ```
//   Derived from Linde's Hull:
//   - https://github.com/openscad/scad-utils
//////////////////////////////////////////////////////////////////////

include <BOSL/math.scad>



// Section: Generalized Hull

// Function: convex_hull()
// Usage:
//   convex_hull(points)
// Description:
//   When given a list of 3D points, returns a list of faces for
//   the minimal convex hull polyhedron of those points.  Each face
//   is a list of indexes into `points`.
//   When given a list of 2D points, or 3D points that are all
//   coplanar, returns a list of indices into `points` for the path
//   that forms the minimal convex hull polygon of those points.
// Arguments:
//   points = The list of points to find the minimal convex hull of.
function convex_hull(points) = 
	!(len(points) > 0)  ? [] :
	len(points[0]) == 2 ? convex_hull2d(points) :
	len(points[0]) == 3 ? convex_hull3d(points) : [];



// Section: 2D Hull

// Function: convex_hull2d()
// Usage:
//   convex_hull2d(points)
// Description:
//   Takes a list of arbitrary 2D points, and finds the minimal convex
//   hull polygon to enclose them.  Returns a path as a list of indices
//   into `points`.
function convex_hull2d(points) =
	(len(points) < 3)? [] : let(
		a=0, b=1,
		c = _find_first_noncollinear([a,b], points, 2)
	) (c == len(points))? _convex_hull_collinear(points) : let(
		remaining = [ for (i = [2:len(points)-1]) if (i != c) i ],
		ccw = triangle_area2d(points[a], points[b], points[c]) > 0,
		polygon = ccw? [a,b,c] : [a,c,b]
	) _convex_hull_iterative_2d(points, polygon, remaining);


// Adds the remaining points one by one to the convex hull
function _convex_hull_iterative_2d(points, polygon, remaining, _i=0) =
	(_i >= len(remaining))? polygon : let (
		// pick a point
		i = remaining[_i],
		// find the segments that are in conflict with the point (point not inside)
		conflicts = _find_conflicting_segments(points, polygon, points[i])
		// no conflicts, skip point and move on
	) (len(conflicts) == 0)? _convex_hull_iterative_2d(points, polygon, remaining, _i+1) : let(
		// find the first conflicting segment and the first not conflicting
		// conflict will be sorted, if not wrapping around, do it the easy way
		polygon = _remove_conflicts_and_insert_point(polygon, conflicts, i)
	) _convex_hull_iterative_2d(points, polygon, remaining, _i+1);


function _find_first_noncollinear(line, points, i) = 
    (i>=len(points) || !collinear_indexed(points, line[0], line[1], i))? i :
	_find_first_noncollinear(line, points, i+1);


function _find_conflicting_segments(points, polygon, point) = [
	for (i = [0:len(polygon)-1]) let(
		j = (i+1) % len(polygon),
		p1 = points[polygon[i]],
		p2 = points[polygon[j]],
		area = triangle_area2d(p1, p2, point)
	) if (area < 0) i
];


// remove the conflicting segments from the polygon
function _remove_conflicts_and_insert_point(polygon, conflicts, point) = 
	(conflicts[0] == 0)? let(
		nonconflicting = [ for(i = [0:len(polygon)-1]) if (!in_list(i, conflicts)) i ],
		new_indices = concat(nonconflicting, (nonconflicting[len(nonconflicting)-1]+1) % len(polygon)),
		polygon = concat([ for (i = new_indices) polygon[i] ], point)
	) polygon : let(
		before_conflicts = [ for(i = [0:min(conflicts)]) polygon[i] ],
		after_conflicts  = (max(conflicts) >= (len(polygon)-1))? [] : [ for(i = [max(conflicts)+1:len(polygon)-1]) polygon[i] ],
		polygon = concat(before_conflicts, point, after_conflicts)
	) polygon;



// Section: 3D Hull

// Function: convex_hull3d()
// Usage:
//   convex_hull3d(points)
// Description:
//   Takes a list of arbitrary 3D points, and finds the minimal convex
//   hull polyhedron to enclose them.  Returns a list of faces, where
//   each face is a list of indexes into the given `points` list.
//   If all points passed to it are coplanar, then the return is the
//   list of indices of points forming the minimal convex hull polygon.
function convex_hull3d(points) = 
	(len(points) < 3)? list_range(len(points)) : let (	
		// start with a single triangle
		a=0, b=1, c=2,
		plane = plane3pt_indexed(points, a, b, c),
		d = _find_first_noncoplanar(plane, points, 3)
	) (d == len(points))? /* all coplanar*/ let (
		pts2d = [ for (p = points) xyz_to_planar(p, points[a], points[b], points[c]) ],
		hull2d = convex_hull2d(pts2d)
	) hull2d : let(
		remaining = [for (i = [3:len(points)-1]) if (i != d) i],
		// Build an initial tetrahedron.
		// Swap b, c if d is in front of triangle t.
		ifop = in_front_of_plane(plane, points[d]),
		bc = ifop? [c,b] : [b,c],
		b = bc[0],
		c = bc[1],
		triangles = [
			[a,b,c],
			[d,b,a],
			[c,d,a],
			[b,d,c]
		],
		// calculate the plane equations
		planes = [ for (t = triangles) plane3pt_indexed(points, t[0], t[1], t[2]) ]
	) _convex_hull_iterative(points, triangles, planes, remaining);


// Adds the remaining points one by one to the convex hull
function _convex_hull_iterative(points, triangles, planes, remaining, _i=0) =
	_i >= len(remaining) ? triangles : 
	let (
		// pick a point
		i = remaining[_i],
		// find the triangles that are in conflict with the point (point not inside)
		conflicts = _find_conflicts(points[i], planes),
		// for all triangles that are in conflict, collect their halfedges
		halfedges = [ 
			for(c = conflicts, i = [0:2]) let(
				j = (i+1)%3
			) [triangles[c][i], triangles[c][j]]
		],
		// find the outer perimeter of the set of conflicting triangles
		horizon = _remove_internal_edges(halfedges),
		// generate a new triangle for each horizon halfedge together with the picked point i
		new_triangles = [ for (h = horizon) concat(h,i) ],
		// calculate the corresponding plane equations
		new_planes = [ for (t = new_triangles) plane3pt_indexed(points, t[0], t[1], t[2]) ]
	) _convex_hull_iterative(
		points,
		//  remove the conflicting triangles and add the new ones
		concat(list_remove(triangles, conflicts), new_triangles),
		concat(list_remove(planes, conflicts), new_planes),
		remaining,
		_i+1
	);


function _convex_hull_collinear(points) =
	let(
		a = points[0],
		n = points[1] - a,
		points1d = [ for(p = points) (p-a)*n ],
		min_i = min_index(points1d),
		max_i = max_index(points1d)
	) [min_i, max_i];



function _remove_internal_edges(halfedges) = [
	for (h = halfedges)
		if (!in_list(reverse(h), halfedges))
			h
];


function _find_conflicts(point, planes) = [
	for (i = [0:len(planes)-1])
		if (in_front_of_plane(planes[i], point))
			i
];


function _find_first_noncoplanar(plane, points, i) = 
	(i >= len(points) || !coplanar(plane, points[i]))? i :
	_find_first_noncoplanar(plane, points, i+1);


// vim: noexpandtab tabstop=4 shiftwidth=4 softtabstop=4 nowrap
