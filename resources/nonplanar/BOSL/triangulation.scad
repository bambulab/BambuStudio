//////////////////////////////////////////////////////////////////////
// LibFile: triangulation.scad
//   Functions to triangulate polyhedron faces.
//   To use, add the following lines to the beginning of your file:
//   ```
//   use <BOSL/triangulation.scad>
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


use <math.scad>


// Section: Functions


// Function: face_normal()
// Description:
//   Given an array of vertices (`points`), and a list of indexes into the
//   vertex array (`face`), returns the normal vector of the face.
// Arguments:
//   points = Array of vertices for the polyhedron.
//   face = The face, given as a list of indices into the vertex array `points`.
function face_normal(points, face) =
	let(count=len(face))
	normalize(
		sum(
			[
				for(i=[0:count-1]) cross(
					points[face[(i+1)%count]]-points[face[0]],
					points[face[(i+2)%count]]-points[face[(i+1)%count]]
				)
			]
		)
	)
;


// Function: find_convex_vertex()
// Description:
//   Returns the index of a convex point on the given face.
// Arguments:
//   points = Array of vertices for the polyhedron.
//   face = The face, given as a list of indices into the vertex array `points`.
//   facenorm = The normal vector of the face.
function find_convex_vertex(points, face, facenorm, i=0) =
	let(count=len(face),
		p0=points[face[i]],
		p1=points[face[(i+1)%count]],
		p2=points[face[(i+2)%count]]
	)
	(len(face)>i)?
		(cross(p1-p0, p2-p1)*facenorm>0)? (i+1)%count : find_convex_vertex(points, face, facenorm, i+1)
	: //This should never happen since there is at least 1 convex vertex.
		undef
;


// Function: point_in_ear()
// Description: Determine if a point is in a clipable convex ear.
// Arguments:
//   points = Array of vertices for the polyhedron.
//   face = The face, given as a list of indices into the vertex array `points`.
function point_in_ear(points, face, tests, i=0) =
	(i<len(face)-1)?
		let(
			prev=point_in_ear(points, face, tests, i+1),
			test=_check_point_in_ear(points[face[i]], tests)
		)
		(test>prev[0])? [test, i] : prev
	:
		[_check_point_in_ear(points[face[i]], tests), i]
;


// Internal non-exposed function.
function _check_point_in_ear(point, tests) =
	let(
		result=[
			(point*tests[0][0])-tests[0][1],
			(point*tests[1][0])-tests[1][1],
			(point*tests[2][0])-tests[2][1]
		]
	)
	(result[0]>0 && result[1]>0 && result[2]>0)? result[0] : -1
;


// Function: normalize_vertex_perimeter()
// Description: Removes the last item in an array if it is the same as the first item.
// Arguments:
//   v = The array to normalize.
function normalize_vertex_perimeter(v) =
	(len(v) < 2)? v :
		(v[len(v)-1] != v[0])? v :
			[for (i=[0:len(v)-2]) v[i]]
;


// Function: is_only_noncolinear_vertex()
// Description:
//   Given a face in a polyhedron, and a vertex in that face, returns true
//   if that vertex is the only non-colinear vertex in the face.
// Arguments:
//   points = Array of vertices for the polyhedron.
//   facelist = The face, given as a list of indices into the vertex array `points`.
//   vertex = The index into `facelist`, of the vertex to test.
function is_only_noncolinear_vertex(points, facelist, vertex) =
	let(
		face=select(facelist, vertex+1, vertex-1),
		count=len(face)
	)
	0==sum(
		[
			for(i=[0:count-1]) norm(
				cross(
					points[face[(i+1)%count]]-points[face[0]],
					points[face[(i+2)%count]]-points[face[(i+1)%count]]
				)
			)
		]
	)
;


// Function: triangulate_face()
// Description:
//   Given a face in a polyhedron, subdivides the face into triangular faces.
//   Returns an array of faces, where each face is a list of three vertex indices.
// Arguments:
//   points = Array of vertices for the polyhedron.
//   face = The face, given as a list of indices into the vertex array `points`.
function triangulate_face(points, face) =
	let(count=len(face))
	(3==count)?
		[face]
	:
		let(
			facenorm=face_normal(points, face),
			cv=find_convex_vertex(points, face, facenorm),
			pv=(count+cv-1)%count,
			nv=(cv+1)%count,
			p0=points[face[pv]],
			p1=points[face[cv]],
			p2=points[face[nv]],
			tests=[
				[cross(facenorm, p0-p2), cross(facenorm, p0-p2)*p0],
				[cross(facenorm, p1-p0), cross(facenorm, p1-p0)*p1],
				[cross(facenorm, p2-p1), cross(facenorm, p2-p1)*p2]
			],
			ear_test=point_in_ear(points, face, tests),
			clipable_ear=(ear_test[0]<0),
			diagonal_point=ear_test[1]
		)
		(clipable_ear)? // There is no point inside the ear.
			is_only_noncolinear_vertex(points, face, cv)?
				// In the point&line degeneracy clip to somewhere in the middle of the line.
				flatten([
					triangulate_face(points, select(face, cv, (cv+2)%count)),
					triangulate_face(points, select(face, (cv+2)%count, cv))
				])
			:
				// Otherwise the ear is safe to clip.
				flatten([
					[select(face, pv, nv)],
					triangulate_face(points, select(face, nv, pv))
				])
		: // If there is a point inside the ear, make a diagonal and clip along that.
			flatten([
				triangulate_face(points, select(face, cv, diagonal_point)),
				triangulate_face(points, select(face, diagonal_point, cv))
			])
;


// Function: triangulate_faces()
// Description:
//   Subdivides all faces for the given polyhedron that have more than three vertices.
//   Returns an array of faces where each face is a list of three vertex array indices.
// Arguments:
//   points = Array of vertices for the polyhedron.
//   faces = Array of faces for the polyhedron. Each face is a list of 3 or more indices into the `points` array.
function triangulate_faces(points, faces) =
	[
		for (i=[0 : len(faces)-1])
			let(facet = normalize_vertex_perimeter(faces[i]))
			for (face = triangulate_face(points, facet))
				if (face[0]!=face[1] && face[1]!=face[2] && face[2]!=face[0]) face
	]
;


// vim: noexpandtab tabstop=4 shiftwidth=4 softtabstop=4 nowrap
