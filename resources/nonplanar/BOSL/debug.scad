//////////////////////////////////////////////////////////////////////
// LibFile: debug.scad
//   Helpers to make debugging OpenScad code easier.
//   To use, add the following lines to the beginning of your file:
//   ```
//   include <BOSL/constants.scad>
//   use <BOSL/debug.scad>
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

include <transforms.scad>
include <math.scad>
include <paths.scad>
include <beziers.scad>


// Section: Debugging Polyhedrons


// Module: debug_vertices()
// Description:
//   Draws all the vertices in an array, at their 3D position, numbered by their
//   position in the vertex array.  Also draws any children of this module with
//   transparency.
// Arguments:
//   vertices = Array of point vertices.
//   size     = The size of the text used to label the vertices.
//   disabled = If true, don't draw numbers, and draw children without transparency.  Default = false.
// Example:
//   verts = [for (z=[-10,10], y=[-10,10], x=[-10,10]) [x,y,z]];
//   faces = [[0,1,2], [1,3,2], [0,4,5], [0,5,1], [1,5,7], [1,7,3], [3,7,6], [3,6,2], [2,6,4], [2,4,0], [4,6,7], [4,7,5]];
//   debug_vertices(vertices=verts, size=2) {
//       polyhedron(points=verts, faces=faces);
//   }
module debug_vertices(vertices, size=1, disabled=false) {
	if (!disabled) {
		echo(vertices=vertices);
		color("blue") {
			for (i = [0:len(vertices)-1]) {
				v = vertices[i];
				translate(v) {
					up(size/8) zrot($vpr[2]) xrot(90) {
						linear_extrude(height=size/10, center=true, convexity=10) {
							text(text=str(i), size=size, halign="center");
						}
					}
					sphere(size/10);
				}
			}
		}
	}
	if ($children > 0) {
		if (!disabled) {
			color([0.2, 1.0, 0, 0.5]) children();
		} else {
			children();
		}
	}
}



// Module: debug_faces()
// Description:
//   Draws all the vertices at their 3D position, numbered in blue by their
//   position in the vertex array.  Each face will have their face number drawn
//   in red, aligned with the center of face.  All children of this module are drawn
//   with transparency.
// Arguments:
//   vertices = Array of point vertices.
//   faces    = Array of faces by vertex numbers.
//   size     = The size of the text used to label the faces and vertices.
//   disabled = If true, don't draw numbers, and draw children without transparency.  Default = false.
// Example:
//   verts = [for (z=[-10,10], y=[-10,10], x=[-10,10]) [x,y,z]];
//   faces = [[0,1,2], [1,3,2], [0,4,5], [0,5,1], [1,5,7], [1,7,3], [3,7,6], [3,6,2], [2,6,4], [2,4,0], [4,6,7], [4,7,5]];
//   debug_faces(vertices=verts, faces=faces, size=2) {
//       polyhedron(points=verts, faces=faces);
//   }
module debug_faces(vertices, faces, size=1, disabled=false) {
	if (!disabled) {
		vlen = len(vertices);
		color("red") {
			for (i = [0:len(faces)-1]) {
				face = faces[i];
				if (face[0] < 0 || face[1] < 0 || face[2] < 0 || face[0] >= vlen || face[1] >= vlen || face[2] >= vlen) {
					echo("BAD FACE: ", vlen=vlen, face=face);
				} else {
					v0 = vertices[face[0]];
					v1 = vertices[face[1]];
					v2 = vertices[face[2]];
					c = (v0 + v1 + v2) / 3;
					dv0 = normalize(v1 - v0);
					dv1 = normalize(v2 - v0);
					nrm0 = normalize(cross(dv0, dv1));
					nrm1 = [0, 0, 1];
					axis = normalize(cross(nrm0, nrm1));
					ang = vector_angle(nrm0,  nrm1);
					theta = atan2(nrm0[1], nrm0[0]);
					translate(c) {
						rotate(a=180-ang, v=axis) {
							zrot(theta-90)
							linear_extrude(height=size/10, center=true, convexity=10) {
								union() {
									text(text=str(i), size=size, halign="center");
									text(text=str("_"), size=size, halign="center");
								}
							}
						}
					}
				}
			}
		}
	}
	debug_vertices(vertices, size=size, disabled=disabled) {
		children();
	}
	if (!disabled) {
		echo(faces=faces);
	}
}



// Module: debug_polyhedron()
// Description:
//   A drop-in module to replace `polyhedron()` and help debug vertices and faces.
//   Draws all the vertices at their 3D position, numbered in blue by their
//   position in the vertex array.  Each face will have their face number drawn
//   in red, aligned with the center of face.  All given faces are drawn with
//   transparency. All children of this module are drawn with transparency.
//   Works best with Thrown-Together preview mode, to see reversed faces.
// Arguments:
//   vertices = Array of point vertices.
//   faces = Array of faces by vertex numbers.
//   txtsize = The size of the text used to label the faces and vertices.
//   disabled = If true, act exactly like `polyhedron()`.  Default = false.
// Example:
//   verts = [for (z=[-10,10], a=[0:120:359.9]) [10*cos(a),10*sin(a),z]];
//   faces = [[0,1,2], [5,4,3], [0,3,4], [0,4,1], [1,4,5], [1,5,2], [2,5,3], [2,3,0]];
//   debug_polyhedron(points=verts, faces=faces, txtsize=1);
module debug_polyhedron(points, faces, convexity=10, txtsize=1, disabled=false) {
	debug_faces(vertices=points, faces=faces, size=txtsize, disabled=disabled) {
		polyhedron(points=points, faces=faces, convexity=convexity);
	}
}



// vim: noexpandtab tabstop=4 shiftwidth=4 softtabstop=4 nowrap
