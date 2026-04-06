//////////////////////////////////////////////////////////////////////
// LibFile: constants.scad
//   Useful Constants.
//   To use this, add the following line to the top of your file.
//   ```
//   include <BOSL/constants.scad>
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


// Section: General Constants

PRINTER_SLOP = 0.20;  // The printer specific amount of slop in mm to print with to make parts fit exactly.  You may need to override this value for your printer.



// Section: Directional Vectors
//   Vectors useful for `rotate()`, `mirror()`, and `align` arguments for `cuboid()`, `cyl()`, etc.

// Constant: V_LEFT
// Description: Vector pointing left.  [-1,0,0]
// Example(3D): Usage with `align`
//   cuboid(20, align=V_LEFT);
V_LEFT  = [-1,  0,  0];

// Constant: V_RIGHT
// Description: Vector pointing right.  [1,0,0]
// Example(3D): Usage with `align`
//   cuboid(20, align=V_RIGHT);
V_RIGHT = [ 1,  0,  0];

// Constant: V_FWD
// Description: Vector pointing forward.  [0,-1,0]
// Example(3D): Usage with `align`
//   cuboid(20, align=V_FWD);
V_FWD   = [ 0, -1,  0];

// Constant: V_BACK
// Description: Vector pointing back.  [0,1,0]
// Example(3D): Usage with `align`
//   cuboid(20, align=V_BACK);
V_BACK  = [ 0,  1,  0];

// Constant: V_DOWN
// Description: Vector pointing down.  [0,0,-1]
// Example(3D): Usage with `align`
//   cuboid(20, align=V_DOWN);
V_DOWN  = [ 0,  0, -1];

// Constant: V_UP
// Description: Vector pointing up.  [0,0,1]
// Example(3D): Usage with `align`
//   cuboid(20, align=V_UP);
V_UP    = [ 0,  0,  1];

// Constant: V_ALLPOS
// Description: Vector pointing right, back, and up.  [1,1,1]
// Example(3D): Usage with `align`
//     cuboid(20, align=V_ALLPOS);
V_ALLPOS = [ 1,  1,  1];  // Vector pointing X+,Y+,Z+.

// Constant: V_ALLNEG
// Description: Vector pointing left, forwards, and down.  [-1,-1,-1]
// Example(3D): Usage with `align`
//     cuboid(20, align=V_ALLNEG);
V_ALLNEG = [-1, -1, -1];  // Vector pointing X-,Y-,Z-.

// Constant: V_ZERO
// Description: Zero vector.  Centered.  [0,0,0]
// Example(3D): Usage with `align`
//     cuboid(20, align=V_ZERO);
V_ZERO   = [ 0,  0,  0];  // Centered zero vector.


// Section: Vector Aliases
//   Useful aliases for use with `align`.

V_CENTER = V_ZERO;  // Centered, alias to `V_ZERO`.
V_ABOVE  = V_UP;    // Vector pointing up, alias to `V_UP`.
V_BELOW  = V_DOWN;  // Vector pointing down, alias to `V_DOWN`.
V_BEFORE = V_FWD;   // Vector pointing forward, alias to `V_FWD`.
V_BEHIND = V_BACK;  // Vector pointing back, alias to `V_BACK`.

V_TOP    = V_UP;    // Vector pointing up, alias to `V_UP`.
V_BOTTOM = V_DOWN;  // Vector pointing down, alias to `V_DOWN`.
V_FRONT  = V_FWD;   // Vector pointing forward, alias to `V_FWD`.
V_REAR   = V_BACK;  // Vector pointing back, alias to `V_BACK`.



// Section: Pre-Orientation Alignments
//   Constants for pre-orientation alignments.


// Constant: ALIGN_POS
// Description: Align the axis-positive end to the origin.
// Example(3D): orient=ORIENT_X
//     cyl(d1=10, d2=5, h=20, orient=ORIENT_X, align=ALIGN_POS);
// Example(3D): orient=ORIENT_Y
//     cyl(d1=10, d2=5, h=20, orient=ORIENT_Y, align=ALIGN_POS);
// Example(3D): orient=ORIENT_Z
//     cyl(d1=10, d2=5, h=20, orient=ORIENT_Z, align=ALIGN_POS);
// Example(3D): orient=ORIENT_XNEG
//     cyl(d1=10, d2=5, h=20, orient=ORIENT_XNEG, align=ALIGN_POS);
// Example(3D): orient=ORIENT_YNEG
//     cyl(d1=10, d2=5, h=20, orient=ORIENT_YNEG, align=ALIGN_POS);
// Example(3D): orient=ORIENT_ZNEG
//     cyl(d1=10, d2=5, h=20, orient=ORIENT_ZNEG, align=ALIGN_POS);
ALIGN_POS = 1;


ALIGN_CENTER =  0;  // Align centered.

// Constant: ALIGN_NEG
// Description: Align the axis-negative end to the origin.
// Example(3D): orient=ORIENT_X
//     cyl(d1=10, d2=5, h=20, orient=ORIENT_X, align=ALIGN_NEG);
// Example(3D): orient=ORIENT_Y
//     cyl(d1=10, d2=5, h=20, orient=ORIENT_Y, align=ALIGN_NEG);
// Example(3D): orient=ORIENT_Z
//     cyl(d1=10, d2=5, h=20, orient=ORIENT_Z, align=ALIGN_NEG);
// Example(3D): orient=ORIENT_XNEG
//     cyl(d1=10, d2=5, h=20, orient=ORIENT_XNEG, align=ALIGN_NEG);
// Example(3D): orient=ORIENT_YNEG
//     cyl(d1=10, d2=5, h=20, orient=ORIENT_YNEG, align=ALIGN_NEG);
// Example(3D): orient=ORIENT_ZNEG
//     cyl(d1=10, d2=5, h=20, orient=ORIENT_ZNEG, align=ALIGN_NEG);
ALIGN_NEG = -1;


// CommonCode:
//   orientations = [
//       ORIENT_X,        ORIENT_Y,        ORIENT_Z,
//       ORIENT_XNEG,     ORIENT_YNEG,     ORIENT_ZNEG,
//       ORIENT_X_90,     ORIENT_Y_90,     ORIENT_Z_90,
//       ORIENT_XNEG_90,  ORIENT_YNEG_90,  ORIENT_ZNEG_90,
//       ORIENT_X_180,    ORIENT_Y_180,    ORIENT_Z_180,
//       ORIENT_XNEG_180, ORIENT_YNEG_180, ORIENT_ZNEG_180,
//       ORIENT_X_270,    ORIENT_Y_270,    ORIENT_Z_270,
//       ORIENT_XNEG_270, ORIENT_YNEG_270, ORIENT_ZNEG_270
//   ];
//   axiscolors = ["red", "forestgreen", "dodgerblue"];
//   module text3d(text, h=0.01, size=3) {
//       linear_extrude(height=h, convexity=10) {
//           text(text=text, size=size, valign="center", halign="center");
//       }
//   }
//   module orient_cube(ang) {
//       color("lightgray") cube(20, center=true);
//       color(axiscolors.x) up  ((20-1)/2+0.01) back ((20-1)/2+0.01) cube([18,1,1], center=true);
//       color(axiscolors.y) up  ((20-1)/2+0.01) right((20-1)/2+0.01) cube([1,18,1], center=true);
//       color(axiscolors.z) back((20-1)/2+0.01) right((20-1)/2+0.01) cube([1,1,18], center=true);
//       for (axis=[0:2], neg=[0:1]) {
//           idx = axis + 3*neg + 6*ang/90;
//           rotate(orientations[idx]) {
//               up(10) {
//                   fwd(4) color("black") text3d(text=str(ang), size=4);
//                   back(4) color(axiscolors[axis]) text3d(text=str(["X","Y","Z"][axis], ["+","NEG"][neg]), size=4);
//               }
//           }
//       }
//   }


// Section: Standard Orientations
//   Orientations for `cyl()`, `prismoid()`, etc.  They take the form of standard [X,Y,Z]
//   rotation angles for rotating a vertical shape into the given orientations.
// Figure(Spin): Standard Orientations
//   orient_cube(0);

ORIENT_X        = [ 90,   0,  90];  // Orient along the X axis.
ORIENT_Y        = [ 90,   0, 180];  // Orient along the Y axis.
ORIENT_Z        = [  0,   0,   0];  // Orient along the Z axis.
ORIENT_XNEG     = [ 90,   0, -90];  // Orient reversed along the X axis.
ORIENT_YNEG     = [ 90,   0,   0];  // Orient reversed along the Y axis.
ORIENT_ZNEG     = [  0, 180,   0];  // Orient reversed along the Z axis.


// Section: Orientations Rotated 90º
//   Orientations for `cyl()`, `prismoid()`, etc.  They take the form of standard [X,Y,Z]
//   rotation angles for rotating a vertical shape into the given orientations.
// Figure(Spin): Orientations Rotated 90º
//   orient_cube(90);

ORIENT_X_90     = [ 90, -90,  90];  // Orient along the X axis, then rotate 90 degrees counter-clockwise on that axis, as seen when facing the origin from that axis orientation.
ORIENT_Y_90     = [ 90, -90, 180];  // Orient along the Y axis, then rotate 90 degrees counter-clockwise on that axis, as seen when facing the origin from that axis orientation.
ORIENT_Z_90     = [  0,   0,  90];  // Orient along the Z axis, then rotate 90 degrees counter-clockwise on that axis, as seen when facing the origin from that axis orientation.
ORIENT_XNEG_90  = [  0, -90,   0];  // Orient reversed along the X axis, then rotate 90 degrees counter-clockwise on that axis, as seen when facing the origin from that axis orientation.
ORIENT_YNEG_90  = [ 90, -90,   0];  // Orient reversed along the Y axis, then rotate 90 degrees counter-clockwise on that axis, as seen when facing the origin from that axis orientation.
ORIENT_ZNEG_90  = [  0, 180, -90];  // Orient reversed along the Z axis, then rotate 90 degrees counter-clockwise on that axis, as seen when facing the origin from that axis orientation.


// Section: Orientations Rotated 180º
//   Orientations for `cyl()`, `prismoid()`, etc.  They take the form of standard [X,Y,Z]
//   rotation angles for rotating a vertical shape into the given orientations.
// Figure(Spin): Orientations Rotated 180º
//   orient_cube(180);

ORIENT_X_180    = [-90,   0, -90];  // Orient along the X axis, then rotate 180 degrees counter-clockwise on that axis, as seen when facing the origin from that axis orientation.
ORIENT_Y_180    = [-90,   0,   0];  // Orient along the Y axis, then rotate 180 degrees counter-clockwise on that axis, as seen when facing the origin from that axis orientation.
ORIENT_Z_180    = [  0,   0, 180];  // Orient along the Z axis, then rotate 180 degrees counter-clockwise on that axis, as seen when facing the origin from that axis orientation.
ORIENT_XNEG_180 = [-90,   0,  90];  // Orient reversed along the X axis, then rotate 180 degrees counter-clockwise on that axis, as seen when facing the origin from that axis orientation.
ORIENT_YNEG_180 = [-90,   0, 180];  // Orient reversed along the Y axis, then rotate 180 degrees counter-clockwise on that axis, as seen when facing the origin from that axis orientation.
ORIENT_ZNEG_180 = [  0, 180, 180];  // Orient reversed along the Z axis, then rotate 180 degrees counter-clockwise on that axis, as seen when facing the origin from that axis orientation.


// Section: Orientations Rotated 270º
//   Orientations for `cyl()`, `prismoid()`, etc.  They take the form of standard [X,Y,Z]
//   rotation angles for rotating a vertical shape into the given orientations.
// Figure(Spin): Orientations Rotated 270º
//   orient_cube(270);

ORIENT_X_270    = [ 90,  90,  90];  // Orient along the X axis, then rotate 270 degrees counter-clockwise on that axis, as seen when facing the origin from that axis orientation.
ORIENT_Y_270    = [ 90,  90, 180];  // Orient along the Y axis, then rotate 270 degrees counter-clockwise on that axis, as seen when facing the origin from that axis orientation.
ORIENT_Z_270    = [  0,   0, -90];  // Orient along the Z axis, then rotate 270 degrees counter-clockwise on that axis, as seen when facing the origin from that axis orientation.
ORIENT_XNEG_270 = [ 90,  90, -90];  // Orient reversed along the X axis, then rotate 270 degrees counter-clockwise on that axis, as seen when facing the origin from that axis orientation.
ORIENT_YNEG_270 = [ 90,  90,   0];  // Orient reversed along the Y axis, then rotate 270 degrees counter-clockwise on that axis, as seen when facing the origin from that axis orientation.
ORIENT_ZNEG_270 = [  0, 180,  90];  // Orient reversed along the Z axis, then rotate 270 degrees counter-clockwise on that axis, as seen when facing the origin from that axis orientation.


// Section: Individual Edges
//   Constants for specifying edges for `cuboid()`, etc.

EDGE_TOP_BK = [[1,0,0,0], [0,0,0,0], [0,0,0,0]];  // Top Back edge.
EDGE_TOP_FR = [[0,1,0,0], [0,0,0,0], [0,0,0,0]];  // Top Front edge.
EDGE_BOT_FR = [[0,0,1,0], [0,0,0,0], [0,0,0,0]];  // Bottom Front Edge.
EDGE_BOT_BK = [[0,0,0,1], [0,0,0,0], [0,0,0,0]];  // Bottom Back Edge.

EDGE_TOP_RT = [[0,0,0,0], [1,0,0,0], [0,0,0,0]];  // Top Right edge.
EDGE_TOP_LF = [[0,0,0,0], [0,1,0,0], [0,0,0,0]];  // Top Left edge.
EDGE_BOT_LF = [[0,0,0,0], [0,0,1,0], [0,0,0,0]];  // Bottom Left edge.
EDGE_BOT_RT = [[0,0,0,0], [0,0,0,1], [0,0,0,0]];  // Bottom Right edge.

EDGE_BK_RT  = [[0,0,0,0], [0,0,0,0], [1,0,0,0]];  // Back Right edge.
EDGE_BK_LF  = [[0,0,0,0], [0,0,0,0], [0,1,0,0]];  // Back Left edge.
EDGE_FR_LF  = [[0,0,0,0], [0,0,0,0], [0,0,1,0]];  // Front Left edge.
EDGE_FR_RT  = [[0,0,0,0], [0,0,0,0], [0,0,0,1]];  // Front Right edge.

// Section: Sets of Edges
//   Constants for specifying edges for `cuboid()`, etc.

EDGES_X_TOP = [[1,1,0,0], [0,0,0,0], [0,0,0,0]];  // Both X-aligned Top edges.
EDGES_X_BOT = [[0,0,1,1], [0,0,0,0], [0,0,0,0]];  // Both X-aligned Bottom edges.
EDGES_X_FR  = [[0,1,1,0], [0,0,0,0], [0,0,0,0]];  // Both X-aligned Front edges.
EDGES_X_BK  = [[1,0,0,1], [0,0,0,0], [0,0,0,0]];  // Both X-aligned Back edges.
EDGES_X_ALL = [[1,1,1,1], [0,0,0,0], [0,0,0,0]];  // All four X-aligned edges.

EDGES_Y_TOP = [[0,0,0,0], [1,1,0,0], [0,0,0,0]];  // Both Y-aligned Top edges.
EDGES_Y_BOT = [[0,0,0,0], [0,0,1,1], [0,0,0,0]];  // Both Y-aligned Bottom edges.
EDGES_Y_LF  = [[0,0,0,0], [0,1,1,0], [0,0,0,0]];  // Both Y-aligned Left edges.
EDGES_Y_RT  = [[0,0,0,0], [1,0,0,1], [0,0,0,0]];  // Both Y-aligned Right edges.
EDGES_Y_ALL = [[0,0,0,0], [1,1,1,1], [0,0,0,0]];  // All four Y-aligned edges.

EDGES_Z_BK  = [[0,0,0,0], [0,0,0,0], [1,1,0,0]];  // Both Z-aligned Back edges.
EDGES_Z_FR  = [[0,0,0,0], [0,0,0,0], [0,0,1,1]];  // Both Z-aligned Front edges.
EDGES_Z_LF  = [[0,0,0,0], [0,0,0,0], [0,1,1,0]];  // Both Z-aligned Left edges.
EDGES_Z_RT  = [[0,0,0,0], [0,0,0,0], [1,0,0,1]];  // Both Z-aligned Right edges.
EDGES_Z_ALL = [[0,0,0,0], [0,0,0,0], [1,1,1,1]];  // All four Z-aligned edges.

EDGES_LEFT   = [[0,0,0,0], [0,1,1,0], [0,1,1,0]];  // All four Left edges.
EDGES_RIGHT  = [[0,0,0,0], [1,0,0,1], [1,0,0,1]];  // All four Right edges.
EDGES_FRONT  = [[0,1,1,0], [0,0,0,0], [0,0,1,1]];  // All four Front edges.
EDGES_BACK   = [[1,0,0,1], [0,0,0,0], [1,1,0,0]];  // All four Back edges.
EDGES_BOTTOM = [[0,0,1,1], [0,0,1,1], [0,0,0,0]];  // All four Bottom edges.
EDGES_TOP    = [[1,1,0,0], [1,1,0,0], [0,0,0,0]];  // All four Top edges.

EDGES_NONE = [[0,0,0,0], [0,0,0,0], [0,0,0,0]];  // No edges.
EDGES_ALL  = [[1,1,1,1], [1,1,1,1], [1,1,1,1]];  // All edges.


// Section: Edge Helpers

EDGE_OFFSETS = [   // Array of XYZ offsets to the center of each edge.
	[[0, 1, 1], [ 0,-1, 1], [ 0,-1,-1], [0, 1,-1]],
	[[1, 0, 1], [-1, 0, 1], [-1, 0,-1], [1, 0,-1]],
	[[1, 1, 0], [-1, 1, 0], [-1,-1, 0], [1,-1, 0]]
];


// Function: corner_edge_count()
// Description: Counts how many given edges intersect at a specific corner.
// Arguments:
//   edges = Standard edges array.
//   v = Vector pointing to the corner to count edge intersections at.
function corner_edge_count(edges, v) =
	(v[2]<=0)? (
		(v[1]<=0)? (
			(v[0]<=0)? (
				edges[0][2] + edges[1][2] + edges[2][2]
			) : (
				edges[0][2] + edges[1][3] + edges[2][3]
			)
		) : (
			(v[0]<=0)? (
				edges[0][3] + edges[1][2] + edges[2][1]
			) : (
				edges[0][3] + edges[1][3] + edges[2][0]
			)
		)
	) : (
		(v[1]<=0)? (
			(v[0]<=0)? (
				edges[0][1] + edges[1][1] + edges[2][2]
			) : (
				edges[0][1] + edges[1][0] + edges[2][3]
			)
		) : (
			(v[0]<=0)? (
				edges[0][0] + edges[1][1] + edges[2][1]
			) : (
				edges[0][0] + edges[1][0] + edges[2][0]
			)
		)
	);



// vim: noexpandtab tabstop=4 shiftwidth=4 softtabstop=4 nowrap
