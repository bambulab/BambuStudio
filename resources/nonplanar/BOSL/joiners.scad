//////////////////////////////////////////////////////////////////////
// LibFile: joiners.scad
//   Snap-together joiners.
//   To use, add the following lines to the beginning of your file:
//   ```
//   include <BOSL/constants.scad>
//   use <BOSL/joiners.scad>
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


// Section: Half Joiners


// Module: half_joiner_clear()
// Description:
//   Creates a mask to clear an area so that a half_joiner can be placed there.
// Usage:
//   half_joiner_clear(h, w, [a], [clearance], [overlap], [orient], [align])
// Arguments:
//   h = Height of the joiner to clear space for.
//   w = Width of the joiner to clear space for.
//   a = Overhang angle of the joiner.
//   clearance = Extra width to clear.
//   overlap = Extra depth to clear.
//   orient = Orientation of the shape.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_Y`.
//   align = Alignment of the shape by the axis-negative (size1) end.  Use the `V_` constants from `constants.scad`.  Default: `V_CENTER`.
// Example:
//   half_joiner_clear(orient=ORIENT_X);
module half_joiner_clear(h=20, w=10, a=30, clearance=0, overlap=0.01, orient=ORIENT_Y, align=V_CENTER)
{
	dmnd_height = h*1.0;
	dmnd_width = dmnd_height*tan(a);
	guide_size = w/3;
	guide_width = 2*(dmnd_height/2-guide_size)*tan(a);

	orient_and_align([w, guide_width, h], orient, align, orig_orient=ORIENT_Y) {
		yspread(overlap, n=overlap>0? 2 : 1) {
			difference() {
				// Diamonds.
				scale([w+clearance, dmnd_width/2, dmnd_height/2]) {
					xrot(45) cube(size=[1,sqrt(2),sqrt(2)], center=true);
				}
				// Blunt point of tab.
				yspread(guide_width+4) {
					cube(size=[(w+clearance)*1.05, 4, h*0.99], center=true);
				}
			}
		}
		if (overlap>0) cube([w+clearance, overlap+0.001, h], center=true);
	}
}



// Module: half_joiner()
// Usage:
//   half_joiner(h, w, l, [a], [screwsize], [guides], [slop], [orient], [align])
// Description:
//   Creates a half_joiner object that can be attached to half_joiner2 object.
// Arguments:
//   h = Height of the half_joiner.
//   w = Width of the half_joiner.
//   l = Length of the backing to the half_joiner.
//   a = Overhang angle of the half_joiner.
//   screwsize = Diameter of screwhole.
//   guides = If true, create sliding alignment guides.
//   slop = Printer specific slop value to make parts fit more closely.
//   orient = Orientation of the shape.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_Y`.
//   align = Alignment of the shape by the axis-negative (size1) end.  Use the `V_` constants from `constants.scad`.  Default: `V_CENTER`.
// Example:
//   half_joiner(screwsize=3, orient=ORIENT_X);
module half_joiner(h=20, w=10, l=10, a=30, screwsize=undef, guides=true, slop=PRINTER_SLOP, orient=ORIENT_Y, align=V_CENTER)
{
	dmnd_height = h*1.0;
	dmnd_width = dmnd_height*tan(a);
	guide_size = w/3;
	guide_width = 2*(dmnd_height/2-guide_size)*tan(a);

	if ($children > 0) {
		difference() {
			children();
			half_joiner_clear(h=h, w=w, a=a, clearance=0.1, overlap=0.01, orient=orient, align=align);
		}
	}
	orient_and_align([w, 2*l, h], orient, align, orig_orient=ORIENT_Y) {
		difference() {
			union() {
				// Make base.
				difference() {
					// Solid backing base.
					fwd(l/2) cube(size=[w, l, h], center=true);

					// Clear diamond for tab
					grid3d(xa=[-(w*2/3), (w*2/3)]) {
						half_joiner_clear(h=h+0.01, w=w, clearance=slop*2, a=a);
					}
				}

				difference() {
					// Make tab
					scale([w/3-slop*2, dmnd_width/2, dmnd_height/2]) xrot(45)
						cube(size=[1,sqrt(2),sqrt(2)], center=true);

					// Blunt point of tab.
					back(guide_width/2+2)
						cube(size=[w*0.99,4,guide_size*2], center=true);
				}


				// Guide ridges.
				if (guides == true) {
					xspread(w/3-slop*2) {
						// Guide ridge.
						fwd(0.05/2) {
							scale([0.75, 1, 2]) yrot(45)
								cube(size=[guide_size/sqrt(2), guide_width+0.05, guide_size/sqrt(2)], center=true);
						}

						// Snap ridge.
						scale([0.25, 0.5, 1]) zrot(45)
							cube(size=[guide_size/sqrt(2), guide_size/sqrt(2), dmnd_width], center=true);
					}
				}
			}

			// Make screwholes, if needed.
			if (screwsize != undef) {
				yrot(90) cylinder(r=screwsize*1.1/2, h=w+1, center=true, $fn=12);
			}
		}
	}
}
//half_joiner(screwsize=3, orient=ORIENT_Z, align=V_UP);



// Module: half_joiner2()
// Usage:
//   half_joiner2(h, w, l, [a], [screwsize], [guides], [orient], [align])
// Description:
//   Creates a half_joiner2 object that can be attached to half_joiner object.
// Arguments:
//   h = Height of the half_joiner.
//   w = Width of the half_joiner.
//   l = Length of the backing to the half_joiner.
//   a = Overhang angle of the half_joiner.
//   screwsize = Diameter of screwhole.
//   guides = If true, create sliding alignment guides.
//   orient = Orientation of the shape.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_Y`.
//   align = Alignment of the shape by the axis-negative (size1) end.  Use the `V_` constants from `constants.scad`.  Default: `V_CENTER`.
// Example:
//   half_joiner2(screwsize=3, orient=ORIENT_X);
module half_joiner2(h=20, w=10, l=10, a=30, screwsize=undef, guides=true, orient=ORIENT_Y, align=V_CENTER)
{
	dmnd_height = h*1.0;
	dmnd_width = dmnd_height*tan(a);
	guide_size = w/3;
	guide_width = 2*(dmnd_height/2-guide_size)*tan(a);

	if ($children > 0) {
		difference() {
			children();
			half_joiner_clear(h=h, w=w, a=a, clearance=0.1, overlap=0.01, orient=orient, align=align);
		}
	}

	orient_and_align([w, 2*l, h], orient, align, orig_orient=ORIENT_Y) {
		difference() {
			union () {
				fwd(l/2) cube(size=[w, l, h], center=true);
				cube([w, guide_width, h], center=true);
			}

			// Subtract mated half_joiner.
			zrot(180) half_joiner(h=h+0.01, w=w+0.01, l=guide_width+0.01, a=a, screwsize=undef, guides=guides, slop=0.0);

			// Make screwholes, if needed.
			if (screwsize != undef) {
				xcyl(r=screwsize*1.1/2, l=w+1, $fn=12);
			}
		}
	}
}



// Section: Full Joiners


// Module: joiner_clear()
// Description:
//   Creates a mask to clear an area so that a joiner can be placed there.
// Usage:
//   joiner_clear(h, w, [a], [clearance], [overlap], [orient], [align])
// Arguments:
//   h = Height of the joiner to clear space for.
//   w = Width of the joiner to clear space for.
//   a = Overhang angle of the joiner.
//   clearance = Extra width to clear.
//   overlap = Extra depth to clear.
//   orient = Orientation of the shape.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_Y`.
//   align = Alignment of the shape by the axis-negative (size1) end.  Use the `V_` constants from `constants.scad`.  Default: `V_CENTER`.
// Example:
//   joiner_clear(orient=ORIENT_X);
module joiner_clear(h=40, w=10, a=30, clearance=0, overlap=0.01, orient=ORIENT_Y, align=V_CENTER)
{
	dmnd_height = h*0.5;
	dmnd_width = dmnd_height*tan(a);
	guide_size = w/3;
	guide_width = 2*(dmnd_height/2-guide_size)*tan(a);

	orient_and_align([w, guide_width, h], orient, align, orig_orient=ORIENT_Y) {
		up(h/4) half_joiner_clear(h=h/2.0-0.01, w=w, a=a, overlap=overlap, clearance=clearance);
		down(h/4) half_joiner_clear(h=h/2.0-0.01, w=w, a=a, overlap=overlap, clearance=-0.01);
	}
}



// Module: joiner()
// Usage:
//   joiner(h, w, l, [a], [screwsize], [guides], [slop], [orient], [align])
// Description:
//   Creates a joiner object that can be attached to another joiner object.
// Arguments:
//   h = Height of the joiner.
//   w = Width of the joiner.
//   l = Length of the backing to the joiner.
//   a = Overhang angle of the joiner.
//   screwsize = Diameter of screwhole.
//   guides = If true, create sliding alignment guides.
//   slop = Printer specific slop value to make parts fit more closely.
//   orient = Orientation of the shape.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_Y`.
//   align = Alignment of the shape by the axis-negative (size1) end.  Use the `V_` constants from `constants.scad`.  Default: `V_CENTER`.
// Examples:
//   joiner(screwsize=3, orient=ORIENT_X);
//   joiner(w=10, l=10, h=40, orient=ORIENT_X) cuboid([10, 10*2, 40], align=V_LEFT);
module joiner(h=40, w=10, l=10, a=30, screwsize=undef, guides=true, slop=PRINTER_SLOP, orient=ORIENT_Y, align=V_CENTER)
{
	if ($children > 0) {
		difference() {
			children();
			joiner_clear(h=h, w=w, a=a, clearance=0.1, orient=orient, align=align);
		}
	}
	orient_and_align([w, 2*l, h], orient, align, orig_orient=ORIENT_Y) {
		up(h/4) half_joiner(h=h/2, w=w, l=l, a=a, screwsize=screwsize, guides=guides, slop=slop);
		down(h/4) half_joiner2(h=h/2, w=w, l=l, a=a, screwsize=screwsize, guides=guides);
	}
}



// Section: Full Joiners Pairs/Sets


// Module: joiner_pair_clear()
// Description:
//   Creates a mask to clear an area so that a pair of joiners can be placed there.
// Usage:
//   joiner_pair_clear(spacing, [n], [h], [w], [a], [clearance], [overlap], [orient], [align])
// Arguments:
//   spacing = Spacing between joiner centers.
//   h = Height of the joiner to clear space for.
//   w = Width of the joiner to clear space for.
//   a = Overhang angle of the joiner.
//   n = Number of joiners (2 by default) to clear for.
//   clearance = Extra width to clear.
//   overlap = Extra depth to clear.
//   orient = Orientation of the shape.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_Y`.
//   align = Alignment of the shape by the axis-negative (size1) end.  Use the `V_` constants from `constants.scad`.  Default: `V_CENTER`.
// Examples:
//   joiner_pair_clear(spacing=50, n=2);
//   joiner_pair_clear(spacing=50, n=3);
module joiner_pair_clear(spacing=100, h=40, w=10, a=30, n=2, clearance=0, overlap=0.01, orient=ORIENT_Y, align=V_CENTER)
{
	dmnd_height = h*0.5;
	dmnd_width = dmnd_height*tan(a);
	guide_size = w/3;
	guide_width = 2*(dmnd_height/2-guide_size)*tan(a);

	orient_and_align([spacing+w, guide_width, h], orient, align, orig_orient=ORIENT_Y) {
		xspread(spacing, n=n) {
			joiner_clear(h=h, w=w, a=a, clearance=clearance, overlap=overlap);
		}
	}
}



// Module: joiner_pair()
// Usage:
//   joiner_pair(h, w, l, [a], [screwsize], [guides], [slop], [orient], [align])
// Description:
//   Creates a joiner_pair object that can be attached to other joiner_pairs .
// Arguments:
//   spacing = Spacing between joiner centers.
//   h = Height of the joiners.
//   w = Width of the joiners.
//   l = Length of the backing to the joiners.
//   a = Overhang angle of the joiners.
//   n = Number of joiners in a row.  Default: 2
//   alternate = If true (default), each joiner alternates it's orientation.  If alternate is "alt", do opposite alternating orientations.
//   screwsize = Diameter of screwhole.
//   guides = If true, create sliding alignment guides.
//   slop = Printer specific slop value to make parts fit more closely.
//   orient = Orientation of the shape.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_Y`.
//   align = Alignment of the shape by the axis-negative (size1) end.  Use the `V_` constants from `constants.scad`.  Default: `V_CENTER`.
// Examples:
//   joiner_pair(spacing=50, l=10, orient=ORIENT_X) cuboid([10, 50+10-0.1, 40], align=V_LEFT);
//   joiner_pair(spacing=50, l=10, n=2, orient=ORIENT_X);
//   joiner_pair(spacing=50, l=10, n=3, alternate=false, orient=ORIENT_X);
//   joiner_pair(spacing=50, l=10, n=3, alternate=true, orient=ORIENT_X);
//   joiner_pair(spacing=50, l=10, n=3, alternate="alt", orient=ORIENT_X);
module joiner_pair(spacing=100, h=40, w=10, l=10, a=30, n=2, alternate=true, screwsize=undef, guides=true, slop=PRINTER_SLOP, orient=ORIENT_Y, align=V_CENTER)
{
	if ($children > 0) {
		difference() {
			children();
			joiner_pair_clear(spacing=spacing, h=h, w=w, a=a, clearance=0.1, orient=orient, align=align);
		}
	}
	orient_and_align([spacing+w, 2*l, h], orient, align, orig_orient=ORIENT_Y) {
		left((n-1)*spacing/2) {
			for (i=[0:n-1]) {
				right(i*spacing) {
					yrot(180 + (alternate? (i*180+(alternate=="alt"?180:0))%360 : 0)) {
						joiner(h=h, w=w, l=l, a=a, screwsize=screwsize, guides=guides, slop=slop);
					}
				}
			}
		}
	}
}



// Section: Full Joiners Quads/Sets


// Module: joiner_quad_clear()
// Description:
//   Creates a mask to clear an area so that a pair of joiners can be placed there.
// Usage:
//   joiner_quad_clear(spacing, [n], [h], [w], [a], [clearance], [overlap], [orient], [align])
// Arguments:
//   spacing1 = Spacing between joiner centers.
//   spacing2 = Spacing between back-to-back pairs/sets of joiners.
//   h = Height of the joiner to clear space for.
//   w = Width of the joiner to clear space for.
//   a = Overhang angle of the joiner.
//   n = Number of joiners in a row.  Default: 2
//   clearance = Extra width to clear.
//   overlap = Extra depth to clear.
//   orient = Orientation of the shape.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_Y`.
//   align = Alignment of the shape by the axis-negative (size1) end.  Use the `V_` constants from `constants.scad`.  Default: `V_CENTER`.
// Examples:
//   joiner_quad_clear(spacing1=50, spacing2=50, n=2);
//   joiner_quad_clear(spacing1=50, spacing2=50, n=3);
module joiner_quad_clear(xspacing=undef, yspacing=undef, spacing1=undef, spacing2=undef, n=2, h=40, w=10, a=30, clearance=0, overlap=0.01, orient=ORIENT_Y, align=V_CENTER)
{
	spacing1 = first_defined([spacing1, xspacing, 100]);
	spacing2 = first_defined([spacing2, yspacing, 50]);
	orient_and_align([w+spacing1, spacing2, h], orient, align, orig_orient=ORIENT_Y) {
		zrot_copies(n=2) {
			back(spacing2/2) {
				joiner_pair_clear(spacing=spacing1, n=n, h=h, w=w, a=a, clearance=clearance, overlap=overlap);
			}
		}
	}
}



// Module: joiner_quad()
// Usage:
//   joiner_quad(h, w, l, [a], [screwsize], [guides], [slop], [orient], [align])
// Description:
//   Creates a joiner_quad object that can be attached to other joiner_pairs .
// Arguments:
//   spacing = Spacing between joiner centers.
//   h = Height of the joiners.
//   w = Width of the joiners.
//   l = Length of the backing to the joiners.
//   a = Overhang angle of the joiners.
//   n = Number of joiners in a row.  Default: 2
//   alternate = If true (default), each joiner alternates it's orientation.  If alternate is "alt", do opposite alternating orientations.
//   screwsize = Diameter of screwhole.
//   guides = If true, create sliding alignment guides.
//   slop = Printer specific slop value to make parts fit more closely.
//   orient = Orientation of the shape.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_Y`.
//   align = Alignment of the shape by the axis-negative (size1) end.  Use the `V_` constants from `constants.scad`.  Default: `V_CENTER`.
// Examples:
//   joiner_quad(spacing1=50, spacing2=50, l=10, orient=ORIENT_X) cuboid([50, 50+10-0.1, 40]);
//   joiner_quad(spacing1=50, spacing2=50, l=10, n=2, orient=ORIENT_X);
//   joiner_quad(spacing1=50, spacing2=50, l=10, n=3, alternate=false, orient=ORIENT_X);
//   joiner_quad(spacing1=50, spacing2=50, l=10, n=3, alternate=true, orient=ORIENT_X);
//   joiner_quad(spacing1=50, spacing2=50, l=10, n=3, alternate="alt", orient=ORIENT_X);
module joiner_quad(spacing1=undef, spacing2=undef, xspacing=undef, yspacing=undef, h=40, w=10, l=10, a=30, n=2, alternate=true, screwsize=undef, guides=true, slop=PRINTER_SLOP, orient=ORIENT_Y, align=V_CENTER)
{
	spacing1 = first_defined([spacing1, xspacing, 100]);
	spacing2 = first_defined([spacing2, yspacing, 50]);
	if ($children > 0) {
		difference() {
			children();
			joiner_quad_clear(spacing1=spacing1, spacing2=spacing2, h=h, w=w, a=a, clearance=0.1, orient=orient, align=align);
		}
	}
	orient_and_align([w+spacing1, spacing2, h], orient, align, orig_orient=ORIENT_Y) {
		zrot_copies(n=2) {
			back(spacing2/2) {
				joiner_pair(spacing=spacing1, n=n, h=h, w=w, l=l, a=a, screwsize=screwsize, guides=guides, slop=slop);
			}
		}
	}
}



// vim: noexpandtab tabstop=4 shiftwidth=4 softtabstop=4 nowrap
