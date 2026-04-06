//////////////////////////////////////////////////////////////////////
// LibFile: masks.scad
//   Masking shapes.
//   To use, add the following lines to the beginning of your file:
//   ```
//   include <BOSL/constants.scad>
//   use <BOSL/masks.scad>
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
use <math.scad>
include <compat.scad>
include <constants.scad>


// Section: General Masks

// Module: angle_pie_mask()
// Usage:
//   angle_pie_mask(r|d, l, ang, [orient], [align], [center]);
//   angle_pie_mask(r1|d1, r2|d2, l, ang, [orient], [align], [center]);
// Description:
//   Creates a pie wedge shape that can be used to mask other shapes.
// Arguments:
//   ang = angle of wedge in degrees.
//   l = height of wedge.
//   r = Radius of circle wedge is created from. (optional)
//   r1 = Bottom radius of cone that wedge is created from.  (optional)
//   r2 = Upper radius of cone that wedge is created from.  (optional)
//   d = Diameter of circle wedge is created from. (optional)
//   d1 = Bottom diameter of cone that wedge is created from.  (optional)
//   d2 = Upper diameter of cone that wedge is created from. (optional)
//   orient = Orientation of the pie slice.  Use the ORIENT_ constants from constants.h.  Default: ORIENT_Z.
//   align = Alignment of the pie slice.  Use the V_ constants from constants.h.  Default: V_CENTER.
//   center = If true, centers vertically.  If false, lift up to sit on top of the XY plane.  Overrides `align`.
// Example(FR):
//   angle_pie_mask(ang=30, d=100, l=20);
module angle_pie_mask(
	ang=45, l=undef,
	r=undef, r1=undef, r2=undef,
	d=undef, d1=undef, d2=undef,
	orient=ORIENT_Z, align=V_CENTER,
	h=undef, center=undef
) {
	l = first_defined([l, h, 1]);
	r1 = get_radius(r1, r, d1, d, 10);
	r2 = get_radius(r2, r, d2, d, 10);
	orient_and_align([2*r1, 2*r1, l], orient, align, center=center) {
		pie_slice(ang=ang, l=l+0.1, r1=r1, r2=r2, align=V_CENTER);
	}
}


// Module: cylinder_mask()
// Usage: Mask objects
//   cylinder_mask(l, r|d, chamfer, [chamfang], [from_end], [circum], [overage], [ends_only], [orient], [align]);
//   cylinder_mask(l, r|d, fillet, [circum], [overage], [ends_only], [orient], [align]);
//   cylinder_mask(l, r|d, [chamfer1|fillet1], [chamfer2|fillet2], [chamfang1], [chamfang2], [from_end], [circum], [overage], [ends_only], [orient], [align]);
// Usage: Masking operators
//   cylinder_mask(l, r|d, chamfer, [chamfang], [from_end], [circum], [overage], [ends_only], [orient], [align]) ...
//   cylinder_mask(l, r|d, fillet, [circum], [overage], [ends_only], [orient], [align]) ...
//   cylinder_mask(l, r|d, [chamfer1|fillet1], [chamfer2|fillet2], [chamfang1], [chamfang2], [from_end], [circum], [overage], [ends_only], [orient], [align]) ...
// Description:
//   If passed children, bevels/chamfers and/or rounds/fillets one or
//   both ends of the origin-centered cylindrical region specified.  If
//   passed no children, creates a mask to bevel/chamfer and/or round/fillet
//   one or both ends of the cylindrical region.  Difference the mask
//   from the region, making sure the center of the mask object is align
//   exactly with the center of the cylindrical region to be chamferred.
// Arguments:
//   l = Length of the cylindrical/conical region.
//   r = Radius of cylindrical region to chamfer.
//   r1 = Radius of axis-negative end of the region to chamfer.
//   r2 = Radius of axis-positive end of the region to chamfer.
//   d = Diameter of cylindrical region to chamfer.
//   d1 = Diameter of axis-negative end of the region to chamfer.
//   d1 = Diameter of axis-positive end of the region to chamfer.
//   chamfer = Size of the chamfers/bevels. (Default: 0.25)
//   chamfer1 = Size of the chamfers/bevels for the axis-negative end of the region.
//   chamfer2 = Size of the chamfers/bevels for the axis-positive end of the region.
//   chamfang = Angle of chamfers/bevels in degrees from the length axis of the region.  (Default: 45)
//   chamfang1 = Angle of chamfer/bevel of the axis-negative end of the region, in degrees from the length axis.
//   chamfang2 = Angle of chamfer/bevel of the axis-positive end of the region, in degrees from the length axis.
//   fillet = The radius of the fillets on the ends of the region.  Default: none.
//   fillet1 = The radius of the fillet on the axis-negative end of the region.
//   fillet2 = The radius of the fillet on the axis-positive end of the region.
//   circum = If true, region will circumscribe the circle of the given radius/diameter.
//   from_end = If true, chamfer/bevel size is measured from end of region.  If false, chamfer/bevel is measured outset from the radius of the region.  (Default: false)
//   overage = The extra thickness of the mask.  Default: `10`.
//   ends_only = If true, only mask the ends and not around the middle of the cylinder.
//   orient = Orientation.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_Z`.
//   align = Alignment of the region.  Use the `V_` constants from `constants.scad`.  Default: `V_CENTER`.
// Example:
//   difference() {
//       cylinder(h=100, r1=60, r2=30, center=true);
//       cylinder_mask(l=100, r1=60, r2=30, chamfer=10, from_end=true);
//   }
// Example:
//   cylinder_mask(l=100, r=50, chamfer1=10, fillet2=10) {
//       cube([100,50,100], center=true);
//   }
module cylinder_mask(
	l,
	r=undef, r1=undef, r2=undef,
	d=undef, d1=undef, d2=undef,
	chamfer=undef, chamfer1=undef, chamfer2=undef,
	chamfang=undef, chamfang1=undef, chamfang2=undef,
	fillet=undef, fillet1=undef, fillet2=undef,
	circum=false, from_end=false,
	overage=10, ends_only=false,
	orient=ORIENT_Z, align=V_CENTER
) {
	r1 = get_radius(r=r, d=d, r1=r1, d1=d1, dflt=1);
	r2 = get_radius(r=r, d=d, r1=r2, d1=d2, dflt=1);
	sides = segs(max(r1,r2));
	sc = circum? 1/cos(180/sides) : 1;
	vang = atan2(l, r1-r2)/2;
	ang1 = first_defined([chamfang1, chamfang, vang]);
	ang2 = first_defined([chamfang2, chamfang, 90-vang]);
	cham1 = first_defined([chamfer1, chamfer, 0]);
	cham2 = first_defined([chamfer2, chamfer, 0]);
	fil1 = first_defined([fillet1, fillet, 0]);
	fil2 = first_defined([fillet2, fillet, 0]);
	maxd = max(r1,r2);
	if ($children > 0) {
		difference() {
			children();
			cylinder_mask(l=l, r1=sc*r1, r2=sc*r2, chamfer1=cham1, chamfer2=cham2, chamfang1=ang1, chamfang2=ang2, fillet1=fil1, fillet2=fil2, orient=orient, from_end=from_end);
		}
	} else {
		orient_and_align([2*r1, 2*r1, l], orient, align) {
			difference() {
				union() {
					chlen1 = cham1 / (from_end? 1 : tan(ang1));
					chlen2 = cham2 / (from_end? 1 : tan(ang2));
					if (!ends_only) {
						cylinder(r=maxd+overage, h=l+2*overage, center=true);
					} else {
						if (cham2>0) up(l/2-chlen2) cylinder(r=maxd+overage, h=chlen2+overage, center=false);
						if (cham1>0) down(l/2+overage) cylinder(r=maxd+overage, h=chlen1+overage, center=false);
						if (fil2>0) up(l/2-fil2) cylinder(r=maxd+overage, h=fil2+overage, center=false);
						if (fil1>0) down(l/2+overage) cylinder(r=maxd+overage, h=fil1+overage, center=false);
					}
				}
				cyl(r1=sc*r1, r2=sc*r2, l=l, chamfer1=cham1, chamfer2=cham2, chamfang1=ang1, chamfang2=ang2, from_end=from_end, fillet1=fil1, fillet2=fil2);
			}
		}
	}
}



// Section: Chamfers


// Module: chamfer_mask()
// Usage:
//   chamfer_mask(l, chamfer, [orient], [align], [center]);
// Description:
//   Creates a shape that can be used to chamfer a 90 degree edge.
//   Difference it from the object to be chamfered.  The center of
//   the mask object should align exactly with the edge to be chamfered.
// Arguments:
//   l = Length of mask.
//   chamfer = Size of chamfer
//   orient = Orientation of the mask.  Use the `ORIENT_` constants from `constants.h`.  Default: vertical.
//   align = Alignment of the mask.  Use the `V_` constants from `constants.h`.  Default: centered.
//   center = If true, centers vertically.  If false, lift up to sit on top of the XY plane.  Overrides `align`.
// Example:
//   difference() {
//       cube(50);
//       #chamfer_mask(l=50, chamfer=10, orient=ORIENT_X, align=V_RIGHT);
//   }
module chamfer_mask(l=1, chamfer=1, orient=ORIENT_Z, align=V_CENTER, center=undef) {
	orient_and_align([chamfer, chamfer, l], orient, align, center=center) {
		cylinder(d=chamfer*2, h=l+0.1, center=true, $fn=4);
	}
}


// Module: chamfer_mask_x()
// Usage:
//   chamfer_mask_x(l, chamfer, [align]);
// Description:
//   Creates a shape that can be used to chamfer a 90 degree edge along the X axis.
//   Difference it from the object to be chamfered.  The center of the mask
//   object should align exactly with the edge to be chamfered.
// Arguments:
//   l = Height of mask
//   chamfer = size of chamfer
//   align = Alignment of the cylinder.  Use the V_ constants from constants.h.  Default: centered.
// Example:
//   difference() {
//       left(40) cube(80);
//       #chamfer_mask_x(l=80, chamfer=20);
//   }
module chamfer_mask_x(l=1.0, chamfer=1.0, align=V_CENTER) {
	chamfer_mask(l=l, chamfer=chamfer, orient=ORIENT_X, align=align);
}


// Module: chamfer_mask_y()
// Usage:
//   chamfer_mask_y(l, chamfer, [align]);
// Description:
//   Creates a shape that can be used to chamfer a 90 degree edge along the Y axis.
//   Difference it from the object to be chamfered.  The center of the mask
//   object should align exactly with the edge to be chamfered.
// Arguments:
//   l = Height of mask
//   chamfer = size of chamfer
//   align = Alignment of the cylinder.  Use the V_ constants from constants.h.  Default: centered.
// Example:
//   difference() {
//       fwd(40) cube(80);
//       right(80) #chamfer_mask_y(l=80, chamfer=20);
//   }
module chamfer_mask_y(l=1.0, chamfer=1.0, align=V_CENTER) {
	chamfer_mask(l=l, chamfer=chamfer, orient=ORIENT_Y, align=align);
}


// Module: chamfer_mask_z()
// Usage:
//   chamfer_mask_z(l, chamfer, [align]);
// Description:
//   Creates a shape that can be used to chamfer a 90 degree edge along the Z axis.
//   Difference it from the object to be chamfered.  The center of the mask
//   object should align exactly with the edge to be chamfered.
// Arguments:
//   l = Height of mask
//   chamfer = size of chamfer
//   align = Alignment of the cylinder.  Use the V_ constants from constants.h.  Default: centered.
// Example:
//   difference() {
//       down(40) cube(80);
//       #chamfer_mask_z(l=80, chamfer=20);
//   }
module chamfer_mask_z(l=1.0, chamfer=1.0, align=V_CENTER) {
	chamfer_mask(l=l, chamfer=chamfer, orient=ORIENT_Z, align=align);
}


// Module: chamfer()
// Usage:
//   chamfer(chamfer, size, [edges]) ...
// Description:
//   Chamfers the edges of a cuboid region containing childrem, centered on the origin.
// Arguments:
//   chamfer = Inset of the chamfer from the edge. (Default: 1)
//   size = The size of the rectangular cuboid we want to chamfer.
//   edges = Which edges do we want to chamfer.  Recommend to use EDGE constants from constants.scad.
// Description:
//   You should use `EDGE` constants from `constants.scad` with the `edge` argument.
//   However, if you must handle it raw, the edge ordering is this:
//       [
//           [Y+Z+, Y-Z+, Y-Z-, Y+Z-],
//           [X+Z+, X-Z+, X-Z-, X+Z-],
//           [X+Y+, X-Y+, X-Y-, X+Y-]
//       ]
// Example(FR):
//   chamfer(chamfer=2, size=[20,40,30]) {
//     cube(size=[20,40,30], center=true);
//   }
// Example(FR):
//   chamfer(chamfer=2, size=[20,40,30], edges=EDGES_TOP - EDGE_TOP_LF + EDGE_FR_RT) {
//     cube(size=[20,40,30], center=true);
//   }
module chamfer(chamfer=1, size=[1,1,1], edges=EDGES_ALL)
{
	difference() {
		children();
		difference() {
			cube(size, center=true);
			cuboid(size+[1,1,1]*0.02, chamfer=chamfer+0.01, edges=edges, trimcorners=true);
		}
	}
}


// Module: chamfer_cylinder_mask()
// Usage:
//   chamfer_cylinder_mask(r|d, chamfer, [ang], [from_end], [orient])
// Description:
//   Create a mask that can be used to bevel/chamfer the end of a cylindrical region.
//   Difference it from the end of the region to be chamferred.  The center of the mask
//   object should align exactly with the center of the end of the cylindrical region
//   to be chamferred.
// Arguments:
//   r = Radius of cylinder to chamfer.
//   d = Diameter of cylinder to chamfer. Use instead of r.
//   chamfer = Size of the edge chamferred, inset from edge. (Default: 0.25)
//   ang = Angle of chamfer in degrees from vertical.  (Default: 45)
//   from_end = If true, chamfer size is measured from end of cylinder.  If false, chamfer is measured outset from the radius of the cylinder.  (Default: false)
//   orient = Orientation of the mask.  Use the `ORIENT_` constants from `constants.h`.  Default: ORIENT_Z.
// Example:
//   difference() {
//       cylinder(r=50, h=100, center=true);
//       up(50) #chamfer_cylinder_mask(r=50, chamfer=10);
//   }
module chamfer_cylinder_mask(r=1.0, d=undef, chamfer=0.25, ang=45, from_end=false, orient=ORIENT_Z)
{
	r = get_radius(r=r, d=d, dflt=1);
	rot(orient) cylinder_mask(l=chamfer*3, r=r, chamfer2=chamfer, chamfang2=ang, from_end=from_end, ends_only=true, align=V_DOWN);
}


// Module: chamfer_hole_mask()
// Usage:
//   chamfer_hole_mask(r|d, chamfer, [ang], [from_end]);
// Description:
//   Create a mask that can be used to bevel/chamfer the end of a cylindrical hole.
//   Difference it from the hole to be chamferred.  The center of the mask object
//   should align exactly with the center of the end of the hole to be chamferred.
// Arguments:
//   r = Radius of hole to chamfer.
//   d = Diameter of hole to chamfer. Use instead of r.
//   chamfer = Size of the chamfer. (Default: 0.25)
//   ang = Angle of chamfer in degrees from vertical.  (Default: 45)
//   from_end = If true, chamfer size is measured from end of hole.  If false, chamfer is measured outset from the radius of the hole.  (Default: false)
//   overage = The extra thickness of the mask.  Default: `0.1`.
// Example:
//   difference() {
//       cube(100, center=true);
//       cylinder(d=50, h=100.1, center=true);
//       up(50) #chamfer_hole_mask(d=50, chamfer=10);
//   }
// Example:
//   chamfer_hole_mask(d=100, chamfer=25, ang=30, overage=10);
module chamfer_hole_mask(r=undef, d=undef, chamfer=0.25, ang=45, from_end=false, overage=0.1)
{
	r = get_radius(r=r, d=d, dflt=1);
	h = chamfer * (from_end? 1 : tan(90-ang));
	r2 = r + chamfer * (from_end? tan(ang) : 1);
	$fn = segs(r);
	difference() {
		union() {
			cylinder(r=r2, h=overage, center=false);
			down(h) cylinder(r1=r, r2=r2, h=h, center=false);
		}
		cylinder(r=r-overage, h=h*2.1+overage, center=true);
	}
}



// Section: Filleting/Rounding

// Module: fillet_mask()
// Usage:
//   fillet_mask(l|h, r, [orient], [align], [center])
// Description:
//   Creates a shape that can be used to fillet a vertical 90 degree edge.
//   Difference it from the object to be filletted.  The center of the mask
//   object should align exactly with the edge to be filletted.
// Arguments:
//   l = Length of mask.
//   r = Radius of the fillet.
//   orient = Orientation of the mask.  Use the `ORIENT_` constants from `constants.h`.  Default: vertical.
//   align = Alignment of the mask.  Use the `V_` constants from `constants.h`.  Default: centered.
//   center = If true, centers vertically.  If false, lift up to sit on top of the XY plane.  Overrides `align`.
// Example:
//   difference() {
//       cube(size=100, center=false);
//       #fillet_mask(l=100, r=25, orient=ORIENT_Z, align=V_UP);
//   }
module fillet_mask(l=undef, r=1.0, orient=ORIENT_Z, align=V_CENTER, h=undef, center=undef)
{
	l = first_defined([l, h, 1]);
	sides = quantup(segs(r),4);
	orient_and_align([2*r, 2*r, l], orient, align, center=center) {
		linear_extrude(height=l+0.1, convexity=4, center=true) {
			difference() {
				square(2*r, center=true);
				xspread(2*r) yspread(2*r) circle(r=r, $fn=sides);
			}
		}
	}
}


// Module: fillet_mask_x()
// Usage:
//   fillet_mask_x(l, r, [align], [center])
// Description:
//   Creates a shape that can be used to fillet a 90 degree edge oriented
//   along the X axis.  Difference it from the object to be filletted.
//   The center of the mask object should align exactly with the edge to
//   be filletted.
// Arguments:
//   l = Length of mask.
//   r = Radius of the fillet.
//   align = Alignment of the mask.  Use the `V_` constants from `constants.h`.  Default: centered.
// Example:
//   difference() {
//       cube(size=100, center=false);
//       #fillet_mask_x(l=100, r=25, align=V_RIGHT);
//   }
module fillet_mask_x(l=1.0, r=1.0, align=V_CENTER) fillet_mask(l=l, r=r, orient=ORIENT_X, align=align);


// Module: fillet_mask_y()
// Usage:
//   fillet_mask_y(l, r, [align], [center])
// Description:
//   Creates a shape that can be used to fillet a 90 degree edge oriented
//   along the Y axis.  Difference it from the object to be filletted.
//   The center of the mask object should align exactly with the edge to
//   be filletted.
// Arguments:
//   l = Length of mask.
//   r = Radius of the fillet.
//   align = Alignment of the mask.  Use the `V_` constants from `constants.h`.  Default: centered.
// Example:
//   difference() {
//       cube(size=100, center=false);
//       right(100) #fillet_mask_y(l=100, r=25, align=V_BACK);
//   }
module fillet_mask_y(l=1.0, r=1.0, align=V_CENTER) fillet_mask(l=l, r=r, orient=ORIENT_Y, align=align);


// Module: fillet_mask_z()
// Usage:
//   fillet_mask_z(l, r, [align], [center])
// Description:
//   Creates a shape that can be used to fillet a 90 degree edge oriented
//   along the Z axis.  Difference it from the object to be filletted.
//   The center of the mask object should align exactly with the edge to
//   be filletted.
// Arguments:
//   l = Length of mask.
//   r = Radius of the fillet.
//   align = Alignment of the mask.  Use the `V_` constants from `constants.h`.  Default: centered.
// Example:
//   difference() {
//       cube(size=100, center=false);
//       #fillet_mask_z(l=100, r=25, align=V_UP);
//   }
module fillet_mask_z(l=1.0, r=1.0, align=V_CENTER) fillet_mask(l=l, r=r, orient=ORIENT_Z, align=align);


// Module: fillet()
// Usage:
//   fillet(fillet, size, [edges]) ...
// Description:
//   Fillets the edges of a cuboid region containing the given children.
// Arguments:
//   fillet = Radius of the fillet. (Default: 1)
//   size = The size of the rectangular cuboid we want to chamfer.
//   edges = Which edges do we want to chamfer.  Recommend to use EDGE constants from constants.scad.
// Description:
//   You should use `EDGE` constants from `constants.scad` with the `edge` argument.
//   However, if you must handle it raw, the edge ordering is this:
//       [
//           [Y+Z+, Y-Z+, Y-Z-, Y+Z-],
//           [X+Z+, X-Z+, X-Z-, X+Z-],
//           [X+Y+, X-Y+, X-Y-, X+Y-]
//       ]
// Example(FR):
//   fillet(fillet=10, size=[50,100,150], $fn=24) {
//     cube(size=[50,100,150], center=true);
//   }
// Example(FR,FlatSpin):
//   fillet(fillet=10, size=[50,50,75], edges=EDGES_TOP - EDGE_TOP_LF + EDGE_FR_RT, $fn=24) {
//     cube(size=[50,50,75], center=true);
//   }
module fillet(fillet=1, size=[1,1,1], edges=EDGES_ALL)
{
	difference() {
		children();
		difference() {
			cube(size, center=true);
			cuboid(size+[1,1,1]*0.01, fillet=fillet, edges=edges, trimcorners=true);
		}
	}
}


// Module: fillet_angled_edge_mask()
// Usage:
//   fillet_angled_edge_mask(h, r, [ang], [center]);
// Description:
//   Creates a vertical mask that can be used to fillet the edge where two
//   face meet, at any arbitrary angle.  Difference it from the object to
//   be filletted.  The center of the mask should align exactly with the
//   edge to be filletted.
// Arguments:
//   h = height of vertical mask.
//   r = radius of the fillet.
//   ang = angle that the planes meet at.
//   center = If true, vertically center mask.
// Example:
//   difference() {
//       angle_pie_mask(ang=70, h=50, d=100);
//       #fillet_angled_edge_mask(h=51, r=20.0, ang=70, $fn=32);
//   }
module fillet_angled_edge_mask(h=1.0, r=1.0, ang=90, center=true)
{
	sweep = 180-ang;
	n = ceil(segs(r)*sweep/360);
	x = r*sin(90-(ang/2))/sin(ang/2);
	linear_extrude(height=h, convexity=4, center=center) {
		polygon(
			points=concat(
				[for (i = [0:n]) let (a=90+ang+i*sweep/n) [r*cos(a)+x, r*sin(a)+r]],
				[for (i = [0:n]) let (a=90+i*sweep/n) [r*cos(a)+x, r*sin(a)-r]],
				[
					[min(-1, r*cos(270-ang)+x-1), r*sin(270-ang)-r],
					[min(-1, r*cos(90+ang)+x-1), r*sin(90+ang)+r],
				]
			)
		);
	}
}


// Module: fillet_angled_corner_mask()
// Usage:
//   fillet_angled_corner_mask(fillet, ang);
// Description:
//   Creates a shape that can be used to fillet the corner of an angle.
//   Difference it from the object to be filletted.  The center of the mask
//   object should align exactly with the point of the corner to be filletted.
// Arguments:
//   fillet = radius of the fillet.
//   ang = angle between planes that you need to fillet the corner of.
// Example:
//   ang=60;
//   difference() {
//       angle_pie_mask(ang=ang, h=50, r=200);
//       up(50/2) {
//           #fillet_angled_corner_mask(fillet=20, ang=ang);
//           zrot_copies([0, ang]) right(200/2) fillet_mask_x(l=200, r=20);
//       }
//       fillet_angled_edge_mask(h=51, r=20, ang=ang);
//   }
module fillet_angled_corner_mask(fillet=1.0, ang=90)
{
	dx = fillet / tan(ang/2);
	fn = quantup(segs(fillet), 4);
	difference() {
		down(fillet) cylinder(r=dx/cos(ang/2)+1, h=fillet+1, center=false);
		yflip_copy() {
			translate([dx, fillet, -fillet]) {
				hull() {
					sphere(r=fillet, $fn=fn);
					down(fillet*3) sphere(r=fillet, $fn=fn);
					zrot_copies([0,ang]) {
						right(fillet*3) sphere(r=fillet, $fn=fn);
					}
				}
			}
		}
	}
}


// Module: fillet_corner_mask()
// Usage:
//   fillet_corner_mask(r);
// Description:
//   Creates a shape that you can use to round 90 degree corners on a fillet.
//   Difference it from the object to be filletted.  The center of the mask
//   object should align exactly with the corner to be filletted.
// Arguments:
//   r = radius of corner fillet.
// Example:
//   fillet_corner_mask(r=20.0);
// Example:
//   difference() {
//     cube(size=[30, 50, 80], center=true);
//     translate([0, 25, 40]) fillet_mask_x(l=31, r=15);
//     translate([15, 0, 40]) fillet_mask_y(l=51, r=15);
//     translate([15, 25, 0]) fillet_mask_z(l=81, r=15);
//     translate([15, 25, 40]) #fillet_corner_mask(r=15);
//   }
module fillet_corner_mask(r=1.0)
{
	difference() {
		cube(size=r*2, center=true);
		grid3d(n=[2,2,2], spacing=r*2-0.05) {
			sphere(r=r);
		}
	}
}


// Module: fillet_cylinder_mask()
// Usage:
//   fillet_cylinder_mask(r, fillet, [xtilt], [ytilt]);
// Description:
//   Create a mask that can be used to round the end of a cylinder.
//   Difference it from the cylinder to be filletted.  The center of the
//   mask object should align exactly with the center of the end of the
//   cylinder to be filletted.
// Arguments:
//   r = radius of cylinder to fillet. (Default: 1.0)
//   fillet = radius of the edge filleting. (Default: 0.25)
//   xtilt = angle of tilt of end of cylinder in the X direction. (Default: 0)
//   ytilt = angle of tilt of end of cylinder in the Y direction. (Default: 0)
// Example:
//   difference() {
//     cylinder(r=50, h=50, center=false);
//     up(50) #fillet_cylinder_mask(r=50, fillet=10);
//   }
// Example:
//   difference() {
//     cylinder(r=50, h=100, center=false);
//     up(75) fillet_cylinder_mask(r=50, fillet=10, xtilt=30);
//   }
module fillet_cylinder_mask(r=1.0, fillet=0.25, xtilt=0, ytilt=0)
{
	skew_xz(za=xtilt) {
		skew_yz(za=ytilt) {
			cylinder_mask(l=fillet*3, r=r, fillet2=fillet, overage=fillet+2*r*sin(max(xtilt,ytilt)), ends_only=true, align=V_DOWN);
		}
	}
}



// Module: fillet_hole_mask()
// Usage:
//   fillet_hole_mask(r|d, fillet, [xtilt], [ytilt]);
// Description:
//   Create a mask that can be used to round the edge of a circular hole.
//   Difference it from the hole to be filletted.  The center of the
//   mask object should align exactly with the center of the end of the
//   hole to be filletted.
// Arguments:
//   r = Radius of hole to fillet.
//   d = Diameter of hole to fillet.
//   fillet = Radius of the filleting. (Default: 0.25)
//   xtilt = Angle of tilt of end of cylinder in the X direction. (Default: 0)
//   ytilt = Angle of tilt of end of cylinder in the Y direction. (Default: 0)
//   overage = The extra thickness of the mask.  Default: `0.1`.
// Example:
//   difference() {
//     cube([150,150,100], center=true);
//     cylinder(r=50, h=100.1, center=true);
//     up(50) #fillet_hole_mask(r=50, fillet=10);
//   }
// Example:
//   fillet_hole_mask(r=40, fillet=20, $fa=2, $fs=2);
module fillet_hole_mask(r=undef, d=undef, fillet=0.25, overage=0.1, xtilt=0, ytilt=0)
{
	r = get_radius(r=r, d=d, dflt=1);
	skew_xz(za=xtilt) {
		skew_yz(za=ytilt) {
			rotate_extrude(convexity=4) {
				difference() {
					right(r-overage) fwd(fillet) square(fillet+overage, center=false);
					right(r+fillet) fwd(fillet) circle(r=fillet);
				}
			}
		}
	}
}


// vim: noexpandtab tabstop=4 shiftwidth=4 softtabstop=4 nowrap
