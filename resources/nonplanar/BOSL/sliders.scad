//////////////////////////////////////////////////////////////////////
// LibFile: sliders.scad
//   Simple V-groove based sliders and rails.
//   To use, add these lines to the beginning of your file:
//   ```
//   include <BOSL/constants.scad>
//   use <BOSL/sliders.scad>
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

use <transforms.scad>
use <shapes.scad>
include <compat.scad>
include <constants.scad>


// Section: Modules


// Module: slider()
// Description:
//   Creates a slider to match a V-groove rail.
// Usage:
//   slider(l, w, h, [base], [wall], [ang], [slop], [orient], [align])
// Arguments:
//   l = Length (long axis) of slider.
//   w = Width of slider.
//   h = Height of slider.
//   chamfer = Size of chamfer
//   base = Height of slider base.
//   wall = Width of wall behind each side of the slider.
//   ang = Overhang angle for slider, to facilitate supportless printig.
//   slop = Printer-specific slop value to make parts fit exactly.
//   orient = Orientation of the slider.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_Y`.
//   align = Alignment of the slider.  Use the `V_` constants from `constants.scad`.  Default: `V_UP`.
// Example:
//   slider(l=30, base=10, wall=4, slop=0.2, orient=ORIENT_Y);
module slider(l=30, w=10, h=10, chamfer=2, base=10, wall=5, ang=30, slop=PRINTER_SLOP, orient=ORIENT_Y, align=V_UP)
{
	full_width = w + 2*wall;
	full_height = h + base;

	orient_and_align([full_width, l, h+2*base], orient, align, orig_orient=ORIENT_Y) {
		down(base+h/2) {
			// Base
			cuboid([full_width, l, base-slop], chamfer=chamfer, edges=EDGE_TOP_FR+EDGE_TOP_BK+EDGES_Z_ALL, align=V_UP);

			// Wall
			xflip_copy(offset=w/2+slop) {
				cuboid([wall, l, full_height], chamfer=chamfer, edges=EDGE_TOP_RT+EDGE_FR_RT+EDGE_BK_RT, align=V_UP+V_RIGHT);
			}

			// Sliders
			up(base+h/2) {
				xflip_copy(offset=w/2+slop+0.02) {
					bev_h = h/2*tan(ang);
					prismoid([l, h], [l-w, 0], h=bev_h+0.01, orient=ORIENT_XNEG, align=V_LEFT);
				}
			}
		}
	}
}



// Module: rail()
// Description:
//   Creates a V-groove rail.
// Usage:
//   rail(l, w, h, [chamfer], [ang], [orient], [align])
// Arguments:
//   l = Length (long axis) of slider.
//   w = Width of slider.
//   h = Height of slider.
//   chamfer = Size of chamfer at end of rail.
//   ang = Overhang angle for slider, to facilitate supportless printig.
//   orient = Orientation of the rail.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_Y`.
//   align = Alignment of the rail.  Use the `V_` constants from `constants.scad`.  Default: `V_UP`.
// Example:
//   rail(l=100, w=10, h=10);
module rail(l=30, w=10, h=10, chamfer=1.0, ang=30, orient=ORIENT_Y, align=V_UP)
{
	attack_ang = 30;
	attack_len = 2;

	fudge = 1.177;
	chamf = sqrt(2) * chamfer;
	cosa = cos(ang*fudge);
	sina = sin(ang*fudge);

	z1 = h/2;
	z2 = z1 - chamf * cosa;
	z3 = z1 - attack_len * sin(attack_ang);
	z4 = 0;

	x1 = w/2;
	x2 = x1 - chamf * sina;
	x3 = x1 - chamf;
	x4 = x1 - attack_len * sin(attack_ang);
	x5 = x2 - attack_len * sin(attack_ang);
	x6 = x1 - z1 * sina;
	x7 = x4 - z1 * sina;

	y1 = l/2;
	y2 = y1 - attack_len * cos(attack_ang);

	orient_and_align([w, l, h], orient, align, orig_orient=ORIENT_Y) {
		polyhedron(
			convexity=4,
			points=[
				[-x5, -y1,  z3],
				[ x5, -y1,  z3],
				[ x7, -y1,  z4],
				[ x4, -y1, -z1-0.05],
				[-x4, -y1, -z1-0.05],
				[-x7, -y1,  z4],

				[-x3, -y2,  z1],
				[ x3, -y2,  z1],
				[ x2, -y2,  z2],
				[ x6, -y2,  z4],
				[ x1, -y2, -z1-0.05],
				[-x1, -y2, -z1-0.05],
				[-x6, -y2,  z4],
				[-x2, -y2,  z2],

				[ x5,  y1,  z3],
				[-x5,  y1,  z3],
				[-x7,  y1,  z4],
				[-x4,  y1, -z1-0.05],
				[ x4,  y1, -z1-0.05],
				[ x7,  y1,  z4],

				[ x3,  y2,  z1],
				[-x3,  y2,  z1],
				[-x2,  y2,  z2],
				[-x6,  y2,  z4],
				[-x1,  y2, -z1-0.05],
				[ x1,  y2, -z1-0.05],
				[ x6,  y2,  z4],
				[ x2,  y2,  z2],
			],
			faces=[
				[0, 1, 2],
				[0, 2, 5],
				[2, 3, 4],
				[2, 4, 5],

				[0, 13, 6],
				[0, 6, 7],
				[0, 7, 1],
				[1, 7, 8],
				[1, 8, 9],
				[1, 9, 2],
				[2, 9, 10],
				[2, 10, 3],
				[3, 10, 11],
				[3, 11, 4],
				[4, 11, 12],
				[4, 12, 5],
				[5, 12, 13],
				[5, 13, 0],

				[14, 15, 16],
				[14, 16, 19],
				[16, 17, 18],
				[16, 18, 19],

				[14, 27, 20],
				[14, 20, 21],
				[14, 21, 15],
				[15, 21, 22],
				[15, 22, 23],
				[15, 23, 16],
				[16, 23, 24],
				[16, 24, 17],
				[17, 24, 25],
				[17, 25, 18],
				[18, 25, 26],
				[18, 26, 19],
				[19, 26, 27],
				[19, 27, 14],

				[6, 21, 20],
				[6, 20, 7],
				[7, 20, 27],
				[7, 27, 8],
				[8, 27, 26],
				[8, 26, 9],
				[9, 26, 25],
				[9, 25, 10],
				[10, 25, 24],
				[10, 24, 11],
				[11, 24, 23],
				[11, 23, 12],
				[12, 23, 22],
				[12, 22, 13],
				[13, 22, 21],
				[13, 21, 6],
			]
		);
	}
}



// vim: noexpandtab tabstop=4 shiftwidth=4 softtabstop=4 nowrap
