//////////////////////////////////////////////////////////////////////
// LibFile: shapes.scad
//   Common useful shapes and structured objects.
//   To use, add the following lines to the beginning of your file:
//   ```
//   include <BOSL/constants.scad>
//   use <BOSL/shapes.scad>
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
use <math.scad>
include <compat.scad>
include <constants.scad>


// Section: Cuboids


// Module: cuboid()
//
// Description:
//   Creates a cube or cuboid object, with optional chamfering or filleting/rounding.
//
// Arguments:
//   size = The size of the cube.
//   chamfer = Size of chamfer, inset from sides.  Default: No chamferring.
//   fillet = Radius of fillet for edge rounding.  Default: No filleting.
//   edges = Edges to chamfer/fillet.  Use `EDGE` constants from constants.scad. Default: `EDGES_ALL`
//   trimcorners = If true, rounds or chamfers corners where three chamferred/filleted edges meet.  Default: `true`
//   p1 = Align the cuboid's corner at `p1`, if given.  Forces `align=V_UP+V_BACK+V_RIGHT`.
//   p2 = If given with `p1`, defines the cornerpoints of the cuboid.
//   align = The side of the origin to align to.  Use `V_` constants from `constants.scad`.  Default: `V_CENTER`
//   center = If given, overrides `align`.  A true value sets `align=V_CENTER`, false sets `align=V_UP+V_BACK+V_RIGHT`.
//
// Example: Simple regular cube.
//   cuboid(40);
// Example: Cube with minimum cornerpoint given.
//   cuboid(20, p1=[10,0,0]);
// Example: Rectangular cube, with given X, Y, and Z sizes.
//   cuboid([20,40,50]);
// Example: Rectangular cube defined by opposing cornerpoints.
//   cuboid(p1=[0,10,0], p2=[20,30,30]);
// Example: Rectangular cube with chamferred edges and corners.
//   cuboid([30,40,50], chamfer=5);
// Example: Rectangular cube with chamferred edges, without trimmed corners.
//   cuboid([30,40,50], chamfer=5, trimcorners=false);
// Example: Rectangular cube with rounded edges and corners.
//   cuboid([30,40,50], fillet=10);
// Example: Rectangular cube with rounded edges, without trimmed corners.
//   cuboid([30,40,50], fillet=10, trimcorners=false);
// Example: Rectangular cube with only some edges chamferred.
//   cuboid([30,40,50], chamfer=5, edges=EDGE_TOP_FR+EDGE_TOP_RT+EDGE_FR_RT, $fn=24);
// Example: Rectangular cube with only some edges rounded.
//   cuboid([30,40,50], fillet=5, edges=EDGE_TOP_FR+EDGE_TOP_RT+EDGE_FR_RT, $fn=24);
module cuboid(
	size=[1,1,1],
	p1=undef, p2=undef,
	chamfer=undef,
	fillet=undef,
	edges=EDGES_ALL,
	trimcorners=true,
	align=[0,0,0],
	center=undef
) {
	size = scalar_vec3(size);
	if (is_def(p1)) {
		if (is_def(p2)) {
			translate([for (v=array_zip([p1,p2],fill=0)) min(v)]) {
				cuboid(size=vabs(p2-p1), chamfer=chamfer, fillet=fillet, edges=edges, trimcorners=trimcorners, align=V_ALLPOS);
			}
		} else {
			translate(p1) {
				cuboid(size=size, chamfer=chamfer, fillet=fillet, edges=edges, trimcorners=trimcorners, align=V_ALLPOS);
			}
		}
	} else {
		majrots = [[0,90,0], [90,0,0], [0,0,0]];
		
		// Not the most elegant, but should work fine.
		// Size for edge E can be ignored if there are only zeros in both edges !E
		relevantsize = [for(a=[0:2]) if(max(edges[(a+1)%3]+edges[(a+2)%3])>0) size[a]];
		
		if (chamfer != undef && len(relevantsize) > 0) assertion(chamfer <= min(relevantsize)/2, "chamfer must be smaller than half the cube width, length, or height.");
		if (fillet != undef && len(relevantsize) > 0 )  assertion(fillet <= min(relevantsize)/2, "fillet must be smaller than half the cube width, length, or height.");
		algn = (!is_def(center))? (is_scalar(align)? align*V_UP : align) : (center==true)? V_CENTER : V_ALLPOS;
		translate(vmul(size/2, algn)) {
			if (chamfer != undef) {
				isize = [for (v = size) max(0.001, v-2*chamfer)];
				if (edges == EDGES_ALL && trimcorners) {
					hull() {
						cube([size[0], isize[1], isize[2]], center=true);
						cube([isize[0], size[1], isize[2]], center=true);
						cube([isize[0], isize[1], size[2]], center=true);
					}
				} else {
					difference() {
						cube(size, center=true);

						// Chamfer edges
						for (i = [0:3], axis=[0:2]) {
							if (edges[axis][i]>0) {
								translate(vmul(EDGE_OFFSETS[axis][i], size/2)) {
									rotate(majrots[axis]) {
										zrot(45) cube([chamfer*sqrt(2), chamfer*sqrt(2), size[axis]+0.01], center=true);
									}
								}
							}
						}

						// Chamfer triple-edge corners.
						if (trimcorners) {
							for (za=[-1,1], ya=[-1,1], xa=[-1,1]) {
								if (corner_edge_count(edges, [xa,ya,za]) > 2) {
									translate(vmul([xa,ya,za]/2, size-[1,1,1]*chamfer*4/3)) {
										rot(from=V_UP, to=[xa,ya,za]) {
											upcube(chamfer*3);
										}
									}
								}
							}
						}
					}
				}
			} else if (fillet != undef) {
				sides = quantup(segs(fillet),4);
				sc = 1/cos(180/sides);
				isize = [for (v = size) max(0.001, v-2*fillet)];
				if (edges == EDGES_ALL) {
					minkowski() {
						cube(isize, center=true);
						if (trimcorners) {
							rotate_extrude(convexity=2,$fn=sides) {
								polygon([for (i=[0:1:sides/2]) let(a=i*360/sides-90) fillet*sc*[cos(a),sin(a)]]);
							}
						} else {
							intersection() {
								zrot(180/sides) cylinder(r=fillet*sc, h=fillet*2, center=true, $fn=sides);
								rotate([90,0,0]) zrot(180/sides) cylinder(r=fillet*sc, h=fillet*2, center=true, $fn=sides);
								rotate([0,90,0]) zrot(180/sides) cylinder(r=fillet*sc, h=fillet*2, center=true, $fn=sides);
							}
						}
					}
				} else {
					difference() {
						cube(size, center=true);

						// Round edges.
						for (i = [0:3], axis=[0:2]) {
							if (edges[axis][i]>0) {
								difference() {
									translate(vmul(EDGE_OFFSETS[axis][i], size/2)) {
										rotate(majrots[axis]) cube([fillet*2, fillet*2, size[axis]+0.1], center=true);
									}
									translate(vmul(EDGE_OFFSETS[axis][i], size/2 - [1,1,1]*fillet)) {
										rotate(majrots[axis]) zrot(180/sides) cylinder(h=size[axis]+0.2, r=fillet*sc, center=true, $fn=sides);
									}
								}
							}
						}

						// Round triple-edge corners.
						if (trimcorners) {
							for (za=[-1,1], ya=[-1,1], xa=[-1,1]) {
								if (corner_edge_count(edges, [xa,ya,za]) > 2) {
									difference() {
										translate(vmul([xa,ya,za], size/2)) {
											cube(fillet*2, center=true);
										}
										translate(vmul([xa,ya,za], size/2-[1,1,1]*fillet)) {
											zrot(180/sides) sphere(r=fillet*sc*sc, $fn=sides);
										}
									}
								}
							}
						}
					}
				}
			} else {
				cube(size=size, center=true);
			}
		}
	}
}



// Module: cube2pt()
// Status: DEPRECATED, use `cuboid(p1,p2)` instead.
//
// Usage:
//   cube2pt(p1,p2)
//
// Description:
//   Creates a cube between two points.
//
// Arguments:
//   p1 = Coordinate point of one cube corner.
//   p2 = Coordinate point of opposite cube corner.
module cube2pt(p1,p2) {
	deprecate("cube2pt()", "cuboid(p1,p2)");
	cuboid(p1=p1, p2=p2);
}



// Module: span_cube()
//
// Description:
//   Creates a cube that spans the X, Y, and Z ranges given.
// 
// Arguments:
//   xspan = [min, max] X axis range.
//   yspan = [min, max] Y axis range.
//   zspan = [min, max] Z axis range.
//
// Example:
//   span_cube([0,15], [5,10], [0, 10]);
module span_cube(xspan, yspan, zspan) {
	span = [xspan, yspan, zspan];
	cuboid(p1=array_subindex(span,0), p2=array_subindex(span,1));
}



// Module: offsetcube()
// Status: DEPRECATED, use `cuboid(..., align)` instead.
//
// Description:
//   Makes a cube that is offset along the given vector by half the cube's size.
//   For example, if `v=[-1,1,0]`, the cube's front right edge will be centered at the origin.
//
// Arguments:
//   size = size of cube.
//   v = vector to offset along.
module offsetcube(size=[1,1,1], v=[0,0,0]) {
	deprecate("offsetcube()", "cuboid()");
	cuboid(size=size, align=v);
}


// Module: leftcube()
//
// Description:
//   Makes a cube that is aligned on the left side of the origin.
//
// Usage:
//   leftcube(size);
// 
// Arguments:
//   size = The size of the cube to make.
//
// Example:
//   leftcube([20,30,40]);
module leftcube(size=[1,1,1]) {siz = scalar_vec3(size); left(siz[0]/2) cube(size=size, center=true);}


// Module: rightcube()
//
// Description:
//   Makes a cube that is aligned on the right side of the origin.
//
// Usage:
//   rightcube(size);
// 
// Arguments:
//   size = The size of the cube to make.
//
// Example:
//   rightcube([20,30,40]);
module rightcube(size=[1,1,1]) {siz = scalar_vec3(size); right(siz[0]/2) cube(size=size, center=true);}


// Module: fwdcube()
//
// Description:
//   Makes a cube that is aligned on the front side of the origin.
//
// Usage:
//   fwdcube(size);
// 
// Arguments:
//   size = The size of the cube to make.
//
// Example:
//   fwdcube([20,30,40]);
module fwdcube(size=[1,1,1]) {siz = scalar_vec3(size); fwd(siz[1]/2) cube(size=size, center=true);}


// Module: backcube()
//
// Description:
//   Makes a cube that is aligned on the front side of the origin.
//
// Usage:
//   backcube(size);
// 
// Arguments:
//   size = The size of the cube to make.
//
// Example:
//   backcube([20,30,40]);
module backcube(size=[1,1,1]) {siz = scalar_vec3(size); back(siz[1]/2) cube(size=size, center=true);}


// Module: downcube()
//
// Description:
//   Makes a cube that is aligned on the bottom side of the origin.
//
// Usage:
//   downcube(size);
// 
// Arguments:
//   size = The size of the cube to make.
//
// Example:
//   downcube([20,30,40]);
module downcube(size=[1,1,1]) {siz = scalar_vec3(size); down(siz[2]/2) cube(size=size, center=true);}


// Module: upcube()
//
// Description:
//   Makes a cube that is aligned on the top side of the origin.
//
// Usage:
//   upcube(size);
// 
// Arguments:
//   size = The size of the cube to make.
//
// Example:
//   upcube([20,30,40]);
module upcube(size=[1,1,1]) {siz = scalar_vec3(size); up(siz[2]/2) cube(size=size, center=true);}


// Module: chamfcube()
// Status: DEPRECATED, use `cuboid(..., chamfer, edges, trimcorners)` instead.
//
// Description:
//   Makes a cube with chamfered edges.
//
// Arguments:
//   size = Size of cube [X,Y,Z].  (Default: `[1,1,1]`)
//   chamfer = Chamfer inset along axis.  (Default: `0.25`)
//   chamfaxes = Array [X,Y,Z] of boolean values to specify which axis edges should be chamfered.
//   chamfcorners = Boolean to specify if corners should be flat chamferred.
module chamfcube(size=[1,1,1], chamfer=0.25, chamfaxes=[1,1,1], chamfcorners=false) {
	deprecate("chamfcube()", "cuboid()");
	cuboid(
		size=size,
		chamfer=chamfer,
		trimcorners=chamfcorners,
		edges = (
			(chamfaxes[0]? EDGES_X_ALL : EDGES_NONE) +
			(chamfaxes[1]? EDGES_Y_ALL : EDGES_NONE) +
			(chamfaxes[2]? EDGES_Z_ALL : EDGES_NONE)
		)
	);
}


// Module: rrect()
// Status: DEPRECATED, use `cuboid(..., fillet, edges)` instead.
//
// Description:
//   Makes a cube with rounded (filletted) vertical edges. The `r` size will be
//   limited to a maximum of half the length of the shortest XY side.
//
// Arguments:
//   size = Size of cube [X,Y,Z].  (Default: `[1,1,1]`)
//   r = Radius of edge/corner rounding.  (Default: `0.25`)
//   center = If true, object will be centered.  If false, sits on top of XY plane.
module rrect(size=[1,1,1], r=0.25, center=false) {
	deprecate("rrect()", "cuboid()");
	cuboid(size=size, fillet=r, edges=EDGES_Z_ALL, align=center? V_CENTER : V_UP);
}


// Module: rcube()
// Status: DEPRECATED, use `cuboid(..., fillet)` instead.
//
// Description:
//   Makes a cube with rounded (filletted) edges and corners.  The `r` size will be
//   limited to a maximum of half the length of the shortest cube side.
//
// Arguments:
//   size = Size of cube [X,Y,Z].  (Default: `[1,1,1]`)
//   r = Radius of edge/corner rounding.  (Default: `0.25`)
//   center = If true, object will be centered.  If false, sits on top of XY plane.
module rcube(size=[1,1,1], r=0.25, center=false) {
	deprecate("rcube()", "cuboid()");
	cuboid(size=size, fillet=r, align=center? V_CENTER : V_UP);
}



// Section: Prismoids


// Module: prismoid()
//
// Description:
//   Creates a rectangular prismoid shape.
//
// Usage:
//   prismoid(size1, size2, h, [shift], [orient], [align|center]);
//
// Arguments:
//   size1 = [width, length] of the axis-negative end of the prism.
//   size2 = [width, length] of the axis-positive end of the prism.
//   h = Height of the prism.
//   shift = [x, y] amount to shift the center of the top with respect to the center of the bottom.
//   orient = Orientation of the prismoid.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_Z`.
//   align = Alignment of the prismoid by the axis-negative (size1) end.  Use the `V_` constants from `constants.scad`.  Default: `ALIGN_POS`.
//   center = If given, overrides `align`.  A true value sets `align=V_CENTER`, false sets `align=ALIGN_POS`.
//
// Example: Rectangular Pyramid
//   prismoid(size1=[40,40], size2=[0,0], h=20);
// Example: Prism
//   prismoid(size1=[40,40], size2=[0,40], h=20);
// Example: Truncated Pyramid
//   prismoid(size1=[35,50], size2=[20,30], h=20);
// Example: Wedge
//   prismoid(size1=[60,35], size2=[30,0], h=30);
// Example: Truncated Tetrahedron
//   prismoid(size1=[10,40], size2=[40,10], h=40);
// Example: Inverted Truncated Pyramid
//   prismoid(size1=[15,5], size2=[30,20], h=20);
// Example: Right Prism
//   prismoid(size1=[30,60], size2=[0,60], shift=[-15,0], h=30);
// Example(FlatSpin): Shifting/Skewing
//   prismoid(size1=[50,30], size2=[20,20], h=20, shift=[15,5]);
module prismoid(
	size1=[1,1], size2=[1,1], h=1, shift=[0,0],
	orient=ORIENT_Z, align=ALIGN_POS, center=undef
) {
	eps = 0.001;
	s1 = [max(size1[0], eps), max(size1[1], eps)];
	s2 = [max(size2[0], eps), max(size2[1], eps)];
	shiftby = point3d(shift);
	orient_and_align([s1[0], s1[1], h], orient, align, center, noncentered=ALIGN_POS) {
		polyhedron(
			points=[
				[+s2[0]/2, +s2[1]/2, +h/2] + shiftby,
				[+s2[0]/2, -s2[1]/2, +h/2] + shiftby,
				[-s2[0]/2, -s2[1]/2, +h/2] + shiftby,
				[-s2[0]/2, +s2[1]/2, +h/2] + shiftby,
				[+s1[0]/2, +s1[1]/2, -h/2],
				[+s1[0]/2, -s1[1]/2, -h/2],
				[-s1[0]/2, -s1[1]/2, -h/2],
				[-s1[0]/2, +s1[1]/2, -h/2],
			],
			faces=[
				[0, 1, 2],
				[0, 2, 3],
				[0, 4, 5],
				[0, 5, 1],
				[1, 5, 6],
				[1, 6, 2],
				[2, 6, 7],
				[2, 7, 3],
				[3, 7, 4],
				[3, 4, 0],
				[4, 7, 6],
				[4, 6, 5],
			],
			convexity=2
		);
	}
}


// Module: trapezoid()
// Status: DEPRECATED, use `prismoid()` instead.
//
// Description:
//   Creates a rectangular prismoid shape.
//
// Usage:
//   trapezoid(size1, size2, h, [shift], [orient], [align|center]);
//
// Arguments:
//   size1 = [width, length] of the axis-negative end of the prism.
//   size2 = [width, length] of the axis-positive end of the prism.
//   h = Height of the prism.
//   shift = [x, y] amount to shift the center of the top with respect to the center of the bottom.
//   orient = Orientation of the prismoid.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_Z`.
//   align = Alignment of the prismoid by the axis-negative (size1) end.  Use the `V_` constants from `constants.scad`.  Default: `V_UP`
//   center = If given, overrides `align`.  A true value sets `align=V_CENTER`, false sets `align=V_UP`.
module trapezoid(size1=[1,1], size2=[1,1], h=1, center=false) {
	deprecate("trapezoid()", "prismoid()");
	prismoid(size1=size1, size2=size2, h=h, center=center);
}


// Module: rounded_prismoid()
//
// Description:
//   Creates a rectangular prismoid shape with rounded vertical edges.
//
// Arguments:
//   size1 = [width, length] of the bottom of the prism.
//   size2 = [width, length] of the top of the prism.
//   h = Height of the prism.
//   r = radius of vertical edge fillets.
//   r1 = radius of vertical edge fillets at bottom.
//   r2 = radius of vertical edge fillets at top.
//   shift = [x, y] amount to shift the center of the top with respect to the center of the bottom.
//   orient = Orientation of the prismoid.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_Z`.
//   align = Alignment of the prismoid by the axis-negative (`size1`) end.  Use the `V_` constants from `constants.scad`.  Default: `V_UP`.
//   center = vertically center the prism.  Overrides `align`.
//
// Example: Rounded Pyramid
//   rounded_prismoid(size1=[40,40], size2=[0,0], h=25, r=5);
// Example: Centered Rounded Pyramid
//   rounded_prismoid(size1=[40,40], size2=[0,0], h=25, r=5, center=true);
// Example: Disparate Top and Bottom Radii
//   rounded_prismoid(size1=[40,60], size2=[40,60], h=20, r1=3, r2=10, $fn=24);
// Example(FlatSpin): Shifting/Skewing
//   rounded_prismoid(size1=[50,30], size2=[20,20], h=20, shift=[15,5], r=5);
module rounded_prismoid(
	size1, size2, h, shift=[0,0],
	r=undef, r1=undef, r2=undef,
	align=V_UP, orient=ORIENT_Z, center=undef
) {
	eps = 0.001;
	maxrad1 = min(size1[0]/2, size1[1]/2);
	maxrad2 = min(size2[0]/2, size2[1]/2);
	rr1 = min(maxrad1, (r1!=undef)? r1 : r);
	rr2 = min(maxrad2, (r2!=undef)? r2 : r);
	shiftby = point3d(shift);
	orient_and_align([size1.x, size1.y, h], orient, align, center, noncentered=ALIGN_POS) {
		down(h/2)
		hull() {
			linear_extrude(height=eps, center=false, convexity=2) {
				offset(r=rr1) {
					square([max(eps, size1[0]-2*rr1), max(eps, size1[1]-2*rr1)], center=true);
				}
			}
			up(h-0.01) {
				translate(shiftby) {
					linear_extrude(height=eps, center=false, convexity=2) {
						offset(r=rr2) {
							square([max(eps, size2[0]-2*rr2), max(eps, size2[1]-2*rr2)], center=true);
						}
					}
				}
			}
		}
	}
}



// Module: pyramid()
// Status: DEPRECATED, use `cyl(, r2=0, $fn=N)` instead.
//
// Usage:
//   pyramid(n, h, l|r|d, [circum]);
//
// Description:
//   Creates a pyramidal prism with a given number of sides.
//
// Arguments:
//   n = number of pyramid sides.
//   h = height of the pyramid.
//   l = length of one side of the pyramid. (optional)
//   r = radius of the base of the pyramid. (optional)
//   d = diameter of the base of the pyramid. (optional)
//   circum = base circumscribes the circle of the given radius or diam.
module pyramid(n=4, h=1, l=1, r=undef, d=undef, circum=false)
{
	deprecate("pyramid()", "cyl()");
	radius = get_radius(r=r, d=d, dflt=l/2/sin(180/n));
	cyl(r1=radius, r2=0, l=h, circum=circum, $fn=n, realign=true, align=ALIGN_POS);
}


// Module: prism()
// Status: DEPRECATED, use `cyl(..., $fn=N)` instead.
//
// Usage:
//   prism(n, h, l|r|d, [circum]);
//
// Description:
//   Creates a vertical prism with a given number of sides.
//
// Arguments:
//   n = number of sides.
//   h = height of the prism.
//   l = length of one side of the prism. (optional)
//   r = radius of the prism. (optional)
//   d = diameter of the prism. (optional)
//   circum = prism circumscribes the circle of the given radius or diam.
module prism(n=3, h=1, l=1, r=undef, d=undef, circum=false, center=false)
{
	deprecate("prism()", "cyl()");
	radius = get_radius(r=r, d=d, dflt=l/2/sin(180/n));
	cyl(r=radius, l=h, circum=circum, $fn=n, realign=true, center=center);
}


// Module: right_triangle()
//
// Description:
//   Creates a 3D right triangular prism.
//
// Usage:
//   right_triangle(size, [orient], [align|center]);
//
// Arguments:
//   size = [width, thickness, height]
//   orient = The axis to place the hypotenuse along.  Only accepts `ORIENT_X`, `ORIENT_Y`, or `ORIENT_Z` from `constants.scad`.  Default: `ORIENT_Y`.
//   align = The side of the origin to align to.  Use `V_` constants from `constants.scad`.  Default: `V_UP+V_BACK+V_RIGHT`.
//   center = If given, overrides `align`.  A true value sets `align=V_CENTER`, false sets `align=V_UP+V_BACK+V_RIGHT`.
//
// Example: Centered
//   right_triangle([60, 10, 40], center=true);
// Example: *Non*-Centered
//   right_triangle([60, 10, 40]);
module right_triangle(size=[1, 1, 1], orient=ORIENT_Y, align=V_ALLPOS, center=undef)
{
	siz = scalar_vec3(size);
	orient_and_align(siz, align=align, center=center) {
		if (orient == ORIENT_X) {
			ang = atan2(siz[1], siz[2]);
			masksize = [siz[0], siz[1], norm([siz[1],siz[2]])] + [1,1,1];
			xrot(ang) {
				difference() {
					xrot(-ang) cube(siz, center=true);
					back(masksize[1]/2) cube(masksize, center=true);
				}
			}
		} else if (orient == ORIENT_Y) {
			ang = atan2(siz[0], siz[2]);
			masksize = [siz[0], siz[1], norm([siz[0],siz[2]])] + [1,1,1];
			yrot(-ang) {
				difference() {
					yrot(ang) cube(siz, center=true);
					right(masksize[0]/2) cube(masksize, center=true);
				}
			}
		} else if (orient == ORIENT_Z) {
			ang = atan2(siz[0], siz[1]);
			masksize = [norm([siz[0],siz[1]]), siz[1], siz[2]] + [1,1,1];
			zrot(-ang) {
				difference() {
					zrot(ang) cube(siz, center=true);
					back(masksize[1]/2) cube(masksize, center=true);
				}
			}
		}
	}
}



// Section: Cylindroids


// Module: cyl()
//
// Description:
//   Creates cylinders in various alignments and orientations,
//   with optional fillets and chamfers. You can use `r` and `l`
//   interchangeably, and all variants allow specifying size
//   by either `r`|`d`, or `r1`|`d1` and `r2`|`d2`.
//   Note that that chamfers and fillets cannot cross the
//   midpoint of the cylinder's length.
//
// Usage: Normal Cylinders
//   cyl(l|h, r|d, [circum], [realign], [orient], [align], [center]);
//   cyl(l|h, r1|d1, r2/d2, [circum], [realign], [orient], [align], [center]);
//
// Usage: Chamferred Cylinders
//   cyl(l|h, r|d, chamfer, [chamfang], [from_end], [circum], [realign], [orient], [align], [center]);
//   cyl(l|h, r|d, chamfer1, [chamfang1], [from_end], [circum], [realign], [orient], [align], [center]);
//   cyl(l|h, r|d, chamfer2, [chamfang2], [from_end], [circum], [realign], [orient], [align], [center]);
//   cyl(l|h, r|d, chamfer1, chamfer2, [chamfang1], [chamfang2], [from_end], [circum], [realign], [orient], [align], [center]);
//
// Usage: Rounded/Filleted Cylinders
//   cyl(l|h, r|d, fillet, [circum], [realign], [orient], [align], [center]);
//   cyl(l|h, r|d, fillet1, [circum], [realign], [orient], [align], [center]);
//   cyl(l|h, r|d, fillet2, [circum], [realign], [orient], [align], [center]);
//   cyl(l|h, r|d, fillet1, fillet2, [circum], [realign], [orient], [align], [center]);
//
// Arguments:
//   l / h = Length of cylinder along oriented axis. (Default: 1.0)
//   r = Radius of cylinder.
//   r1 = Radius of the negative (X-, Y-, Z-) end of cylinder.
//   r2 = Radius of the positive (X+, Y+, Z+) end of cylinder.
//   d = Diameter of cylinder.
//   d1 = Diameter of the negative (X-, Y-, Z-) end of cylinder.
//   d2 = Diameter of the positive (X+, Y+, Z+) end of cylinder.
//   circum = If true, cylinder should circumscribe the circle of the given size.  Otherwise inscribes.  Default: `false`
//   chamfer = The size of the chamfers on the ends of the cylinder.  Default: none.
//   chamfer1 = The size of the chamfer on the axis-negative end of the cylinder.  Default: none.
//   chamfer2 = The size of the chamfer on the axis-positive end of the cylinder.  Default: none.
//   chamfang = The angle in degrees of the chamfers on the ends of the cylinder.
//   chamfang1 = The angle in degrees of the chamfer on the axis-negative end of the cylinder.
//   chamfang2 = The angle in degrees of the chamfer on the axis-positive end of the cylinder.
//   from_end = If true, chamfer is measured from the end of the cylinder, instead of inset from the edge.  Default: `false`.
//   fillet = The radius of the fillets on the ends of the cylinder.  Default: none.
//   fillet1 = The radius of the fillet on the axis-negative end of the cylinder.
//   fillet2 = The radius of the fillet on the axis-positive end of the cylinder.
//   realign = If true, rotate the cylinder by half the angle of one face.
//   orient = Orientation of the cylinder.  Use the `ORIENT_` constants from `constants.scad`.  Default: vertical.
//   align = Alignment of the cylinder.  Use the `V_` constants from `constants.scad`.  Default: centered.
//   center = If given, overrides `align`.  A true value sets `align=V_CENTER`, false sets `align=ALIGN_POS`.
//
// Example: By Radius
//   xdistribute(30) {
//       cyl(l=40, r=10);
//       cyl(l=40, r1=10, r2=5);
//   }
//
// Example: By Diameter
//   xdistribute(30) {
//       cyl(l=40, d=25);
//       cyl(l=40, d1=25, d2=10);
//   }
//
// Example: Chamferring
//   xdistribute(60) {
//       // Shown Left to right.
//       cyl(l=40, d=40, chamfer=7);  // Default chamfang=45
//       cyl(l=40, d=40, chamfer=7, chamfang=30, from_end=false);
//       cyl(l=40, d=40, chamfer=7, chamfang=30, from_end=true);
//   }
//
// Example: Rounding/Filleting
//   cyl(l=40, d=40, fillet=10);
//
// Example: Heterogenous Chamfers and Fillets
//   ydistribute(80) {
//       // Shown Front to Back.
//       cyl(l=40, d=40, fillet1=15, orient=ORIENT_X);
//       cyl(l=40, d=40, chamfer2=5, orient=ORIENT_X);
//       cyl(l=40, d=40, chamfer1=12, fillet2=10, orient=ORIENT_X);
//   }
//
// Example: Putting it all together
//   cyl(l=40, d1=25, d2=15, chamfer1=10, chamfang1=30, from_end=true, fillet2=5);
module cyl(
	l=undef, h=undef,
	r=undef, r1=undef, r2=undef,
	d=undef, d1=undef, d2=undef,
	chamfer=undef, chamfer1=undef, chamfer2=undef,
	chamfang=undef, chamfang1=undef, chamfang2=undef,
	fillet=undef, fillet1=undef, fillet2=undef,
	circum=false, realign=false, from_end=false,
	orient=ORIENT_Z, align=V_CENTER, center=undef
) {
	r1 = get_radius(r1, r, d1, d, 1);
	r2 = get_radius(r2, r, d2, d, 1);
	l = first_defined([l, h, 1]);
	sides = segs(max(r1,r2));
	sc = circum? 1/cos(180/sides) : 1;
	orient_and_align([r1*2,r1*2,l], orient, align, center=center) {
		zrot(realign? 180/sides : 0) {
			if (!any_defined([chamfer, chamfer1, chamfer2, fillet, fillet1, fillet2])) {
				cylinder(h=l, r1=r1*sc, r2=r2*sc, center=true, $fn=sides);
			} else {
				vang = atan2(l, r1-r2)/2;
				chang1 = 90-first_defined([chamfang1, chamfang, vang]);
				chang2 = 90-first_defined([chamfang2, chamfang, 90-vang]);
				cham1 = (chamfer != undef || chamfer1 != undef)?first_defined([chamfer1, chamfer]) * (from_end? 1 : tan(chang1)):undef;
				cham2 = (chamfer != undef || chamfer2 != undef)?first_defined([chamfer2, chamfer]) * (from_end? 1 : tan(chang2)):undef;
				fil1 = first_defined([fillet1, fillet]);
				fil2 = first_defined([fillet2, fillet]);
				if (chamfer != undef) {
					assertion(chamfer <= r1,  "chamfer is larger than the r1 radius of the cylinder.");
					assertion(chamfer <= r2,  "chamfer is larger than the r2 radius of the cylinder.");
					assertion(chamfer <= l/2, "chamfer is larger than half the length of the cylinder.");
				}
				if (cham1 != undef) {
					assertion(cham1 <= r1,  "chamfer1 is larger than the r1 radius of the cylinder.");
					assertion(cham1 <= l/2, "chamfer1 is larger than half the length of the cylinder.");
				}
				if (cham2 != undef) {
					assertion(cham2 <= r2,  "chamfer2 is larger than the r2 radius of the cylinder.");
					assertion(cham2 <= l/2, "chamfer2 is larger than half the length of the cylinder.");
				}
				if (fillet != undef) {
					assertion(fillet <= r1,  "fillet is larger than the r1 radius of the cylinder.");
					assertion(fillet <= r2,  "fillet is larger than the r2 radius of the cylinder.");
					assertion(fillet <= l/2, "fillet is larger than half the length of the cylinder.");
				}
				if (fil1 != undef) {
					assertion(fil1 <= r1,  "fillet1 is larger than the r1 radius of the cylinder.");
					assertion(fil1 <= l/2, "fillet1 is larger than half the length of the cylinder.");
				}
				if (fil2 != undef) {
					assertion(fil2 <= r2,  "fillet2 is larger than the r1 radius of the cylinder.");
					assertion(fil2 <= l/2, "fillet2 is larger than half the length of the cylinder.");
				}

				dy1 = first_defined([cham1, fil1, 0]);
				dy2 = first_defined([cham2, fil2, 0]);
				maxd = max(r1,r2,l);

				rotate_extrude(convexity=2) {
					hull() {
						difference() {
							union() {
								difference() {
									back(l/2) {
										if (cham2!=undef && cham2>0) {
											rr2 = sc * (r2 + (r1-r2)*dy2/l);
											chlen2 = min(rr2, cham2/sin(chang2));
											translate([rr2,-cham2]) {
												rotate(-chang2) {
													translate([-chlen2,-chlen2]) {
														square(chlen2, center=false);
													}
												}
											}
										} else if (fil2!=undef && fil2>0) {
											translate([r2-fil2*tan(vang),-fil2]) {
												circle(r=fil2);
											}
										} else {
											translate([r2-0.005,-0.005]) {
												square(0.01, center=true);
											}
										}
									}

									// Make sure the corner fiddly bits never cross the X axis.
									fwd(maxd) square(maxd, center=false);
								}
								difference() {
									fwd(l/2) {
										if (cham1!=undef && cham1>0) {
											rr1 = sc * (r1 + (r2-r1)*dy1/l);
											chlen1 = min(rr1, cham1/sin(chang1));
											translate([rr1,cham1]) {
												rotate(chang1) {
													left(chlen1) {
														square(chlen1, center=false);
													}
												}
											}
										} else if (fil1!=undef && fil1>0) {
											right(r1) {
												translate([-fil1/tan(vang),fil1]) {
													fsegs1 = quantup(segs(fil1),4);
													circle(r=fil1,$fn=fsegs1);
												}
											}
										} else {
											right(r1-0.01) {
												square(0.01, center=false);
											}
										}
									}

									// Make sure the corner fiddly bits never cross the X axis.
									square(maxd, center=false);
								}

								// Force the hull to extend to the axis
								right(0.01/2) square([0.01, l], center=true);
							}

							// Clear anything left of the Y axis.
							left(maxd/2) square(maxd, center=true);

							// Clear anything right of face
							right((r1+r2)/2) {
								rotate(90-vang*2) {
									fwd(maxd/2) square(maxd, center=false);
								}
							}
						}
					}
				}
			}
		}
	}
}



// Module: downcyl()
//
// Description:
//   Creates a cylinder aligned below the origin.
//
// Usage:
//   downcyl(l|h, r|d);
//   downcyl(l|h, r1|d1, r2|d2);
//
// Arguments:
//   l / h = Length of cylinder. (Default: 1.0)
//   r = Radius of cylinder.
//   r1 = Bottom radius of cylinder.
//   r2 = Top radius of cylinder.
//   d = Diameter of cylinder. (use instead of r)
//   d1 = Bottom diameter of cylinder.
//   d2 = Top diameter of cylinder.
//
// Example: Cylinder
//   downcyl(r=20, h=40);
// Example: Cone
//   downcyl(r1=10, r2=20, h=40);
module downcyl(r=undef, h=undef, l=undef, d=undef, d1=undef, d2=undef, r1=undef, r2=undef)
{
	h = first_defined([l, h, 1]);
	down(h/2) {
		cylinder(r=r, r1=r1, r2=r2, d=d, d1=d1, d2=d2, h=h, center=true);
	}
}



// Module: xcyl()
//
// Description:
//   Creates a cylinder oriented along the X axis.
//
// Usage:
//   xcyl(l|h, r|d, [align|center]);
//   xcyl(l|h, r1|d1, r2|d2, [align|center]);
//
// Arguments:
//   l / h = Length of cylinder along oriented axis. (Default: `1.0`)
//   r = Radius of cylinder.
//   r1 = Optional radius of left (X-) end of cylinder.
//   r2 = Optional radius of right (X+) end of cylinder.
//   d = Optional diameter of cylinder. (use instead of `r`)
//   d1 = Optional diameter of left (X-) end of cylinder.
//   d2 = Optional diameter of right (X+) end of cylinder.
//   align = The side of the origin to align to.  Use `V_` constants from `constants.scad`. Default: `V_CENTER`
//   center = If given, overrides `align`.  A `true` value sets `align=V_CENTER`, `false` sets `align=ALIGN_POS`.
//
// Example: By Radius
//   ydistribute(50) {
//       xcyl(l=35, r=10);
//       xcyl(l=35, r1=15, r2=5);
//   }
//
// Example: By Diameter
//   ydistribute(50) {
//       xcyl(l=35, d=20);
//       xcyl(l=35, d1=30, d2=10);
//   }
module xcyl(l=undef, r=undef, d=undef, r1=undef, r2=undef, d1=undef, d2=undef, h=undef, align=V_CENTER, center=undef)
{
	cyl(l=l, h=h, r=r, r1=r1, r2=r2, d=d, d1=d1, d2=d2, orient=ORIENT_X, align=align, center=center);
}



// Module: ycyl()
//
// Description:
//   Creates a cylinder oriented along the Y axis.
//
// Usage:
//   ycyl(l|h, r|d, [align|center]);
//   ycyl(l|h, r1|d1, r2|d2, [align|center]);
//
// Arguments:
//   l / h = Length of cylinder along oriented axis. (Default: `1.0`)
//   r = Radius of cylinder.
//   r1 = Radius of front (Y-) end of cone.
//   r2 = Radius of back (Y+) end of one.
//   d = Diameter of cylinder.
//   d1 = Diameter of front (Y-) end of one.
//   d2 = Diameter of back (Y+) end of one.
//   align = The side of the origin to align to.  Use `V_` constants from `constants.scad`. Default: `V_CENTER`
//   center = Overrides `align` if given.  If true, `align=V_CENTER`, if false, `align=ALIGN_POS`.
//
// Example: By Radius
//   xdistribute(50) {
//       ycyl(l=35, r=10);
//       ycyl(l=35, r1=15, r2=5);
//   }
//
// Example: By Diameter
//   xdistribute(50) {
//       ycyl(l=35, d=20);
//       ycyl(l=35, d1=30, d2=10);
//   }
module ycyl(l=undef, r=undef, d=undef, r1=undef, r2=undef, d1=undef, d2=undef, h=undef, align=V_CENTER, center=undef)
{
	cyl(l=l, h=h, r=r, r1=r1, r2=r2, d=d, d1=d1, d2=d2, orient=ORIENT_Y, align=align, center=center);
}



// Module: zcyl()
//
// Description:
//   Creates a cylinder oriented along the Z axis.
//
// Usage:
//   zcyl(l|h, r|d, [align|center]);
//   zcyl(l|h, r1|d1, r2|d2, [align|center]);
//
// Arguments:
//   l / h = Length of cylinder along oriented axis. (Default: 1.0)
//   r = Radius of cylinder.
//   r1 = Radius of front (Y-) end of cone.
//   r2 = Radius of back (Y+) end of one.
//   d = Diameter of cylinder.
//   d1 = Diameter of front (Y-) end of one.
//   d2 = Diameter of back (Y+) end of one.
//   align = The side of the origin to align to.  Use `V_` constants from `constants.scad`. Default: `V_CENTER`
//   center = Overrides `align` if given.  If true, `align=V_CENTER`, if false, `align=ALIGN_POS`.
//
// Example: By Radius
//   xdistribute(50) {
//       zcyl(l=35, r=10);
//       zcyl(l=35, r1=15, r2=5);
//   }
//
// Example: By Diameter
//   xdistribute(50) {
//       zcyl(l=35, d=20);
//       zcyl(l=35, d1=30, d2=10);
//   }
module zcyl(l=undef, r=undef, d=undef, r1=undef, r2=undef, d1=undef, d2=undef, h=undef, align=V_CENTER, center=undef)
{
	cyl(l=l, h=h, r=r, r1=r1, r2=r2, d=d, d1=d1, d2=d2, orient=ORIENT_Z, align=align, center=center);
}



// Module: chamferred_cylinder()
// Status: DEPRECATED, use `cyl(..., chamfer)` instead.
//
// Usage:
//   chamferred_cylinder(h, r|d, chamfer|chamfedge, [top], [bottom], [center])
//
// Description:
//   Creates a cylinder with chamferred (bevelled) edges.
//
// Arguments:
//   h = height of cylinder. (Default: 1.0)
//   r = radius of cylinder. (Default: 1.0)
//   d = diameter of cylinder. (use instead of r)
//   chamfer = radial inset of the edge chamfer. (Default: 0.25)
//   chamfedge = length of the chamfer edge. (Use instead of chamfer)
//   top = boolean.  If true, chamfer the top edges. (Default: True)
//   bottom = boolean.  If true, chamfer the bottom edges. (Default: True)
//   center = boolean.  If true, cylinder is centered. (Default: false)
module chamferred_cylinder(h=1, r=undef, d=undef, chamfer=0.25, chamfedge=undef, angle=45, center=false, top=true, bottom=true)
{
	deprecate("chamf_cyl()` and `chamferred_cylinder()", "cyl()");
	r = get_radius(r=r, d=d, dflt=1);
	chamf = (chamfedge == undef)? chamfer : chamfedge * cos(angle);
	cyl(l=h, r=r, chamfer1=bottom? chamf : 0, chamfer2=top? chamf : 0, chamfang=angle, center=center);
}



// Module: chamf_cyl()
// Status: DEPRECATED, use `cyl(..., chamfer)` instead.
//
// Usage:
//   chamf_cyl(h, r|d, chamfer|chamfedge, [top], [bottom], [center])
//
// Description:
//   Creates a cylinder with chamferred (bevelled) edges.  Basically a shortcut of `chamferred_cylinder()`
//
// Arguments:
//   h = height of cylinder. (Default: 1.0)
//   r = radius of cylinder. (Default: 1.0)
//   d = diameter of cylinder. (use instead of r)
//   chamfer = radial inset of the edge chamfer. (Default: 0.25)
//   chamfedge = length of the chamfer edge. (Use instead of chamfer)
//   top = boolean.  If true, chamfer the top edges. (Default: True)
//   bottom = boolean.  If true, chamfer the bottom edges. (Default: True)
//   center = boolean.  If true, cylinder is centered. (Default: false)
module chamf_cyl(h=1, r=undef, d=undef, chamfer=0.25, chamfedge=undef, angle=45, center=false, top=true, bottom=true)
	chamferred_cylinder(h=h, r=r, d=d, chamfer=chamfer, chamfedge=chamfedge, angle=angle, center=center, top=top, bottom=bottom);


// Module: filleted_cylinder()
// Status: DEPRECATED, use `cyl(..., fillet)` instead.
//
// Usage:
//   filleted_cylinder(h, r|d, fillet, [center]);
//
// Description:
//   Creates a cylinder with filletted (rounded) ends.
//
// Arguments:
//   h = height of cylinder. (Default: 1.0)
//   r = radius of cylinder. (Default: 1.0)
//   d = diameter of cylinder. (Use instead of r)
//   fillet = radius of the edge filleting. (Default: 0.25)
//   center = boolean.  If true, cylinder is centered. (Default: false)
module filleted_cylinder(h=1, r=undef, d=undef, r1=undef, r2=undef, d1=undef, d2=undef, fillet=0.25, center=false) {
	deprecate("filleted_cylinder()", "cyl()");
	cyl(l=h, r=r, d=d, r1=r1, r2=r2, d1=d1, d2=d2, fillet=fillet, orient=ORIENT_Z, center=center);
}



// Module: rcylinder()
// Status: DEPRECATED, use `cyl(..., fillet)` instead.
//
// Usage:
//   rcylinder(h, r|d, fillet, [center]);
//
// Description:
//   Creates a cylinder with filletted (rounded) ends.
//   Basically a shortcut for `filleted_cylinder()`.
//
// Arguments:
//   h = height of cylinder. (Default: 1.0)
//   r = radius of cylinder. (Default: 1.0)
//   d = diameter of cylinder. (Use instead of r)
//   fillet = radius of the edge filleting. (Default: 0.25)
//   center = boolean.  If true, cylinder is centered. (Default: false)
module rcylinder(h=1, r=1, r1=undef, r2=undef, d=undef, d1=undef, d2=undef, fillet=0.25, center=false) {
	deprecate("rcylinder()", "cyl(..., fillet)");
	cyl(l=h, r=r, d=d, r1=r1, r2=r2, d1=d1, d2=d2, fillet=fillet, orient=ORIENT_Z, center=center);
}



// Module: tube()
//
// Description:
//   Makes a hollow tube with the given outer size and wall thickness.
//
// Usage:
//   tube(h, ir|id, wall, [realign], [orient], [align]);
//   tube(h, or|od, wall, [realign], [orient], [align]);
//   tube(h, ir|id, or|od, [realign], [orient], [align]);
//   tube(h, ir1|id1, ir2|id2, wall, [realign], [orient], [align]);
//   tube(h, or1|od1, or2|od2, wall, [realign], [orient], [align]);
//   tube(h, ir1|id1, ir2|id2, or1|od1, or2|od2, [realign], [orient], [align]);
//
// Arguments:
//   h = height of tube. (Default: 1)
//   or = Outer radius of tube.
//   or1 = Outer radius of bottom of tube.  (Default: value of r)
//   or2 = Outer radius of top of tube.  (Default: value of r)
//   od = Outer diameter of tube.
//   od1 = Outer diameter of bottom of tube.
//   od2 = Outer diameter of top of tube.
//   wall = horizontal thickness of tube wall. (Default 0.5)
//   ir = Inner radius of tube.
//   ir1 = Inner radius of bottom of tube.
//   ir2 = Inner radius of top of tube.
//   id = Inner diameter of tube.
//   id1 = Inner diameter of bottom of tube.
//   id2 = Inner diameter of top of tube.
//   realign = If true, rotate the tube by half the angle of one face.
//   orient = Orientation of the tube.  Use the `ORIENT_` constants from `constants.scad`.  Default: vertical.
//   align = Alignment of the tube.  Use the `V_` constants from `constants.scad`.  Default: centered.
//
// Example: These all Produce the Same Tube
//   tube(h=30, or=40, wall=5);
//   tube(h=30, ir=35, wall=5);
//   tube(h=30, or=40, ir=35);
//   tube(h=30, od=80, id=70);
// Example: These all Produce the Same Conical Tube
//   tube(h=30, or1=40, or2=25, wall=5);
//   tube(h=30, ir1=35, or2=20, wall=5);
//   tube(h=30, or1=40, or2=25, ir1=35, ir2=20);
// Example: Circular Wedge
//   tube(h=30, or1=40, or2=30, ir1=20, ir2=30);
module tube(
	h=1, wall=undef,
	r=undef, r1=undef, r2=undef,
	d=undef, d1=undef, d2=undef,
	or=undef, or1=undef, or2=undef,
	od=undef, od1=undef, od2=undef,
	ir=undef, id=undef, ir1=undef,
	ir2=undef, id1=undef, id2=undef,
	center=undef, orient=ORIENT_Z, align=ALIGN_POS,
	realign=false
) {
	r1 = first_defined([or1, od1==undef?undef:od1/2, r1, d1==undef?undef:d1/2, or, od==undef?undef:od/2, r, d==undef?undef:d/2, ir1==undef||wall==undef?undef:ir1+wall, id1==undef||wall==undef?undef:id1/2+wall, ir==undef||wall==undef?undef:ir+wall, id==undef||wall==undef?undef:id/2+wall]);
	r2 = first_defined([or2, od2==undef?undef:od2/2, r2, d2==undef?undef:d2/2, or, od==undef?undef:od/2, r, d==undef?undef:d/2, ir2==undef||wall==undef?undef:ir2+wall, id2==undef||wall==undef?undef:id2/2+wall, ir==undef||wall==undef?undef:ir+wall, id==undef||wall==undef?undef:id/2+wall]);
	ir1 = first_defined([ir1, id1==undef?undef:id1/2, ir, id==undef?undef:id/2, r1==undef||wall==undef?undef:r1-wall, d1==undef||wall==undef?undef:d1/2-wall, r==undef||wall==undef?undef:r-wall, d==undef||wall==undef?undef:d/2-wall]);
	ir2 = first_defined([ir2, id2==undef?undef:id2/2, ir, id==undef?undef:id/2, r2==undef||wall==undef?undef:r2-wall, d2==undef||wall==undef?undef:d2/2-wall, r==undef||wall==undef?undef:r-wall, d==undef||wall==undef?undef:d/2-wall]);
	assertion(ir1 <= r1, "Inner radius is larger than outer radius.");
	assertion(ir2 <= r2, "Inner radius is larger than outer radius.");
	sides = segs(max(r1,r2));
	orient_and_align([r1*2,r1*2,h], orient, align, center=center) {
		zrot(realign? 180/sides : 0) {
			difference() {
				cylinder(h=h, r1=r1, r2=r2, center=true, $fn=sides);
				if (ir1 == ir2) {
					cylinder(h=h+2, r1=ir1, r2=ir2, center=true);
				} else {
					cylinder(h=h+2, r=min(ir1,ir2), center=true);
					diff = abs(ir1-ir2);
					diff2 = diff*(h+1)/h;
					if (ir1 > ir2) {
						zmove(-0.5)
							cylinder(h=h+1, r1=ir2+diff2, r2=ir2, center=true);
					} else {
						zmove(0.5)
							cylinder(h=h+1, r1=ir1, r2=ir1+diff2, center=true);
					}
				}
			}
		}
	}
}


// Module: torus()
//
// Descriptiom:
//   Creates a torus shape.
//
// Usage:
//   torus(r|d, r2|d2, [orient], [align]);
//   torus(or|od, ir|id, [orient], [align]);
//
// Arguments:
//   r  = major radius of torus ring. (use with of 'r2', or 'd2')
//   r2 = minor radius of torus ring. (use with of 'r', or 'd')
//   d  = major diameter of torus ring. (use with of 'r2', or 'd2')
//   d2 = minor diameter of torus ring. (use with of 'r', or 'd')
//   or = outer radius of the torus. (use with 'ir', or 'id')
//   ir = inside radius of the torus. (use with 'or', or 'od')
//   od = outer diameter of the torus. (use with 'ir' or 'id')
//   id = inside diameter of the torus. (use with 'or' or 'od')
//   orient = Orientation of the torus.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_Z`.
//   align = Alignment of the torus.  Use the `V_` constants from `constants.scad`.  Default: `V_CENTER`.
//
// Example:
//   // These all produce the same torus.
//   torus(r=22.5, r2=7.5);
//   torus(d=45, d2=15);
//   torus(or=30, ir=15);
//   torus(od=60, id=30);
module torus(
	r=undef,  d=undef,
	r2=undef, d2=undef,
	or=undef, od=undef,
	ir=undef, id=undef,
	orient=ORIENT_Z, align=V_CENTER, center=undef
) {
	orr = get_radius(r=or, d=od, dflt=1.0);
	irr = get_radius(r=ir, d=id, dflt=0.5);
	majrad = get_radius(r=r, d=d, dflt=(orr+irr)/2);
	minrad = get_radius(r=r2, d=d2, dflt=(orr-irr)/2);
	orient_and_align([(majrad+minrad)*2, (majrad+minrad)*2, minrad*2], orient, align, center=center) {
		rotate_extrude(convexity=4) {
			right(majrad) circle(minrad);
		}
	}
}



// Section: Spheroids


// Module: staggered_sphere()
//
// Description:
//   An alternate construction to the standard `sphere()` built-in, with different triangulation.
//
// Usage:
//   staggered_sphere(r|d, [circum])
//
// Arguments:
//   r = Radius of the sphere.
//   d = Diameter of the sphere.
//   circum = If true, circumscribes the perfect sphere of the given size.
//
// Example:
//   staggered_sphere(d=100, circum=true, $fn=10);
module staggered_sphere(r=undef, d=undef, circum=false, align=V_CENTER) {
	r = get_radius(r=r, d=d, dflt=1);
	sides = segs(r);
	vsides = max(3, ceil(sides/2))+1;
	step = 360/sides;
	vstep = 180/(vsides-1);
	rr = circum? r/cos(180/sides)/cos(180/sides) : r;
	pts = concat(
		[[0,0,rr]],
		[
			for (p = [1:vsides-2], t = [0:sides-1]) let(
				ta = (t+(p%2/2))*step,
				pa = p*vstep
			) spherical_to_xyz(rr, ta, pa)
		],
		[[0,0,-rr]]
	);
	pcnt = len(pts);
	faces = concat(
		[
			for (i = [1:sides], j=[0,1])
			j? [0, i%sides+1, i] : [pcnt-1, pcnt-1-(i%sides+1), pcnt-1-i]
		],
		[
			for (p = [0:vsides-4], i = [0:sides-1], j=[0,1]) let(
				b1 = 1+p*sides,
				b2 = 1+(p+1)*sides,
				v1 = b1+i,
				v2 = b1+(i+1)%sides,
				v3 = b2+((i+((p%2)?(sides-1):0))%sides),
				v4 = b2+((i+1+((p%2)?(sides-1):0))%sides)
			) j? [v1,v4,v3] : [v1,v2,v4]
		]
	);
	zrot((floor(sides/4)%2==1)? 180/sides : 0) polyhedron(points=pts, faces=faces);
}



// Section: 3D Printing Shapes


// Module: teardrop2d()
//
// Description:
//   Makes a 2D teardrop shape. Useful for extruding into 3D printable holes.
//
// Usage:
//   teardrop2d(r|d, [ang], [cap_h]);
//
// Arguments:
//   r = radius of circular part of teardrop.  (Default: 1)
//   d = diameter of spherical portion of bottom. (Use instead of r)
//   ang = angle of hat walls from the Y axis.  (Default: 45 degrees)
//   cap_h = if given, height above center where the shape will be truncated.
//
// Example(2D): Typical Shape
//   teardrop2d(r=30, ang=30);
// Example(2D): Crop Cap
//   teardrop2d(r=30, ang=30, cap_h=40);
// Example(2D): Close Crop
//   teardrop2d(r=30, ang=30, cap_h=20);
module teardrop2d(r=1, d=undef, ang=45, cap_h=undef)
{
	eps = 0.01;
	r = get_radius(r=r, d=d, dflt=1);
	cord = 2 * r * cos(ang);
	cord_h = r * sin(ang);
	tip_y = (cord/2)/tan(ang);
	cap_h = min((is_def(cap_h)? cap_h : tip_y+cord_h), tip_y+cord_h);
	cap_w = cord * (1 - (cap_h - cord_h)/tip_y);
	difference() {
		hull() {
			zrot(90) circle(r=r);
			back(cap_h-eps/2) square([max(eps,cap_w), eps], center=true);
		}
		back(r+cap_h) square(2*r, center=true);
	}
}


// Module: teardrop()
//
// Description:
//   Makes a teardrop shape in the XZ plane. Useful for 3D printable holes.
//
// Usage:
//   teardrop(r|d, l|h, [ang], [cap_h], [orient], [align])
//
// Arguments:
//   r = Radius of circular part of teardrop.  (Default: 1)
//   d = Diameter of circular portion of bottom. (Use instead of r)
//   l = Thickness of teardrop. (Default: 1)
//   ang = Angle of hat walls from the Z axis.  (Default: 45 degrees)
//   cap_h = If given, height above center where the shape will be truncated.
//   orient = Orientation of the shape.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_Y`.
//   align = Alignment of the shape.  Use the `V_` constants from `constants.scad`.  Default: `V_CENTER`.
//
// Example: Typical Shape
//   teardrop(r=30, h=10, ang=30);
// Example: Crop Cap
//   teardrop(r=30, h=10, ang=30, cap_h=40);
// Example: Close Crop
//   teardrop(r=30, h=10, ang=30, cap_h=20);
module teardrop(r=undef, d=undef, l=undef, h=undef, ang=45, cap_h=undef, orient=ORIENT_Y, align=V_CENTER)
{
	r = get_radius(r=r, d=d, dflt=1);
	l = first_defined([l, h, 1]);
	orient_and_align([r*2,r*2,l], orient, align) {
		linear_extrude(height=l, center=true, slices=2) {
			teardrop2d(r=r, ang=ang, cap_h=cap_h);
		}
	}
}


// Module: onion()
//
// Description:
//   Creates a sphere with a conical hat, to make a 3D teardrop.
//
// Usage:
//   onion(r|d, [maxang], [cap_h], [orient], [align]);
//
// Arguments:
//   r = radius of spherical portion of the bottom. (Default: 1)
//   d = diameter of spherical portion of bottom.
//   cap_h = height above sphere center to truncate teardrop shape.
//   maxang = angle of cone on top from vertical.
//   orient = Orientation of the shape.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_Y`.
//   align = Alignment of the shape.  Use the `V_` constants from `constants.scad`.  Default: `V_CENTER`.
//
// Example: Typical Shape
//   onion(r=30, maxang=30);
// Example: Crop Cap
//   onion(r=30, maxang=30, cap_h=40);
// Example: Close Crop
//   onion(r=30, maxang=30, cap_h=20);
module onion(cap_h=undef, r=undef, d=undef, maxang=45, h=undef, orient=ORIENT_Z, align=V_CENTER)
{
	r = get_radius(r=r, d=d, dflt=1);
	h = first_defined([cap_h, h]);
	maxd = 3*r/tan(maxang);
	orient_and_align([r*2,r*2,r*2], orient, align) {
		rotate_extrude(convexity=2) {
			difference() {
				teardrop2d(r=r, ang=maxang, cap_h=h);
				left(r) square(size=[2*r,maxd], center=true);
			}
		}
	}
}


// Module: narrowing_strut()
//
// Description:
//   Makes a rectangular strut with the top side narrowing in a triangle.
//   The shape created may be likened to an extruded home plate from baseball.
//   This is useful for constructing parts that minimize the need to support
//   overhangs.
//
// Usage:
//   narrowing_strut(w, l, wall, [ang], [orient], [align]);
//
// Arguments:
//   w = Width (thickness) of the strut.
//   l = Length of the strut.
//   wall = height of rectangular portion of the strut.
//   ang = angle that the trianglar side will converge at.
//   orient = Orientation of the length axis of the shape.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_Y`.
//   align = Alignment of the shape.  Use the `V_` constants from `constants.scad`.  Default: `V_CENTER`.
//
// Example:
//   narrowing_strut(w=10, l=100, wall=5, ang=30);
module narrowing_strut(w=10, l=100, wall=5, ang=30, orient=ORIENT_Y, align=V_UP)
{
	h = wall + w/2/tan(ang);
	orient_and_align([w, h, l], orient, align, orig_orient=ORIENT_Z) {
		fwd(h/2) {
			linear_extrude(height=l, center=true, slices=2) {
				back(wall/2) square([w, wall], center=true);
				back(wall-0.001) {
					yscale(1/tan(ang)) {
						difference() {
							zrot(45) square(w/sqrt(2), center=true);
							fwd(w/2) square(w, center=true);
						}
					}
				}
			}
		}
	}
}


// Module: thinning_wall()
//
// Description:
//   Makes a rectangular wall which thins to a smaller width in the center,
//   with angled supports to prevent critical overhangs.
//
// Usage:
//   thinning_wall(h, l, thick, [ang], [strut], [wall], [orient], [align]);
//
// Arguments:
//   h = height of wall.
//   l = length of wall.  If given as a vector of two numbers, specifies bottom and top lengths, respectively.
//   thick = thickness of wall.
//   ang = maximum overhang angle of diagonal brace.
//   strut = the width of the diagonal brace.
//   wall = the thickness of the thinned portion of the wall.
//   orient = Orientation of the length axis of the wall.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_X`.
//   align = Alignment of the shape.  Use the `V_` constants from `constants.scad`.  Default: `V_CENTER`.
//
// Example: Typical Shape
//   thinning_wall(h=50, l=80, thick=4);
// Example: Trapezoidal
//   thinning_wall(h=50, l=[80,50], thick=4);
module thinning_wall(h=50, l=100, thick=5, ang=30, strut=5, wall=2, orient=ORIENT_X, align=V_CENTER)
{
	l1 = (l[0] == undef)? l : l[0];
	l2 = (l[1] == undef)? l : l[1];

	trap_ang = atan2((l2-l1)/2, h);
	corr1 = 1 + sin(trap_ang);
	corr2 = 1 - sin(trap_ang);

	z1 = h/2;
	z2 = max(0.1, z1 - strut);
	z3 = max(0.05, z2 - (thick-wall)/2*sin(90-ang)/sin(ang));

	x1 = l2/2;
	x2 = max(0.1, x1 - strut*corr1);
	x3 = max(0.05, x2 - (thick-wall)/2*sin(90-ang)/sin(ang)*corr1);
	x4 = l1/2;
	x5 = max(0.1, x4 - strut*corr2);
	x6 = max(0.05, x5 - (thick-wall)/2*sin(90-ang)/sin(ang)*corr2);

	y1 = thick/2;
	y2 = y1 - min(z2-z3, x2-x3) * sin(ang);

	orient_and_align([l1, thick, h], orient, align, orig_orient=ORIENT_X) {
		polyhedron(
			points=[
				[-x4, -y1, -z1],
				[ x4, -y1, -z1],
				[ x1, -y1,  z1],
				[-x1, -y1,  z1],

				[-x5, -y1, -z2],
				[ x5, -y1, -z2],
				[ x2, -y1,  z2],
				[-x2, -y1,  z2],

				[-x6, -y2, -z3],
				[ x6, -y2, -z3],
				[ x3, -y2,  z3],
				[-x3, -y2,  z3],

				[-x4,  y1, -z1],
				[ x4,  y1, -z1],
				[ x1,  y1,  z1],
				[-x1,  y1,  z1],

				[-x5,  y1, -z2],
				[ x5,  y1, -z2],
				[ x2,  y1,  z2],
				[-x2,  y1,  z2],

				[-x6,  y2, -z3],
				[ x6,  y2, -z3],
				[ x3,  y2,  z3],
				[-x3,  y2,  z3],
			],
			faces=[
				[ 4,  5,  1],
				[ 5,  6,  2],
				[ 6,  7,  3],
				[ 7,  4,  0],

				[ 4,  1,  0],
				[ 5,  2,  1],
				[ 6,  3,  2],
				[ 7,  0,  3],

				[ 8,  9,  5],
				[ 9, 10,  6],
				[10, 11,  7],
				[11,  8,  4],

				[ 8,  5,  4],
				[ 9,  6,  5],
				[10,  7,  6],
				[11,  4,  7],

				[11, 10,  9],
				[20, 21, 22],

				[11,  9,  8],
				[20, 22, 23],

				[16, 17, 21],
				[17, 18, 22],
				[18, 19, 23],
				[19, 16, 20],

				[16, 21, 20],
				[17, 22, 21],
				[18, 23, 22],
				[19, 20, 23],

				[12, 13, 17],
				[13, 14, 18],
				[14, 15, 19],
				[15, 12, 16],

				[12, 17, 16],
				[13, 18, 17],
				[14, 19, 18],
				[15, 16, 19],

				[ 0,  1, 13],
				[ 1,  2, 14],
				[ 2,  3, 15],
				[ 3,  0, 12],

				[ 0, 13, 12],
				[ 1, 14, 13],
				[ 2, 15, 14],
				[ 3, 12, 15],
			],
			convexity=6
		);
	}
}


// Module: braced_thinning_wall()
//
// Description:
//   Makes a rectangular wall with cross-bracing, which thins to a smaller width in the center,
//   with angled supports to prevent critical overhangs.
//
// Usage:
//   braced_thinning_wall(h, l, thick, [ang], [strut], [wall], [orient], [align]);
//
// Arguments:
//   h = height of wall.
//   l = length of wall.
//   thick = thickness of wall.
//   ang = maximum overhang angle of diagonal brace.
//   strut = the width of the diagonal brace.
//   wall = the thickness of the thinned portion of the wall.
//   orient = Orientation of the length axis of the wall.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_Y`.
//   align = Alignment of the shape.  Use the `V_` constants from `constants.scad`.  Default: `V_CENTER`.
//
// Example: Typical Shape
//   braced_thinning_wall(h=50, l=100, thick=5);
module braced_thinning_wall(h=50, l=100, thick=5, ang=30, strut=5, wall=2, orient=ORIENT_Y, align=V_CENTER)
{
	dang = atan((h-2*strut)/(l-2*strut));
	dlen = (h-2*strut)/sin(dang);
	orient_and_align([thick, l, h], orient, align, orig_orient=ORIENT_Y) {
		xrot_copies([0, 180]) {
			down(h/2) narrowing_strut(w=thick, l=l, wall=strut, ang=ang);
			fwd(l/2) xrot(-90) narrowing_strut(w=thick, l=h-0.1, wall=strut, ang=ang);
			intersection() {
				cube(size=[thick, l, h], center=true);
				xrot_copies([-dang,dang]) {
					zspread(strut/2) {
						scale([1,1,1.5]) yrot(45) {
							cube(size=[thick/sqrt(2), dlen, thick/sqrt(2)], center=true);
						}
					}
					cube(size=[thick, dlen, strut/2], center=true);
				}
			}
		}
		cube(size=[wall, l-0.1, h-0.1], center=true);
	}
}



// Module: thinning_triangle()
//
// Description:
//   Makes a triangular wall with thick edges, which thins to a smaller width in
//   the center, with angled supports to prevent critical overhangs.
//
// Usage:
//   thinning_triangle(h, l, thick, [ang], [strut], [wall], [diagonly], [orient], [align|center]);
//
// Arguments:
//   h = height of wall.
//   l = length of wall.
//   thick = thickness of wall.
//   ang = maximum overhang angle of diagonal brace.
//   strut = the width of the diagonal brace.
//   wall = the thickness of the thinned portion of the wall.
//   diagonly = boolean, which denotes only the diagonal side (hypotenuse) should be thick.
//   orient = Orientation of the length axis of the shape.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_Y`.
//   align = Alignment of the shape.  Use the `V_` constants from `constants.scad`.  Default: `V_CENTER`.
//   center = If true, centers shape.  If false, overrides `align` with `V_UP+V_BACK`.
//
// Example: Centered
//   thinning_triangle(h=50, l=80, thick=4, ang=30, strut=5, wall=2, center=true);
// Example: All Braces
//   thinning_triangle(h=50, l=80, thick=4, ang=30, strut=5, wall=2, center=false);
// Example: Diagonal Brace Only
//   thinning_triangle(h=50, l=80, thick=4, ang=30, strut=5, wall=2, diagonly=true, center=false);
module thinning_triangle(h=50, l=100, thick=5, ang=30, strut=5, wall=3, diagonly=false, center=undef, orient=ORIENT_Y, align=V_CENTER)
{
	dang = atan(h/l);
	dlen = h/sin(dang);
	orient_and_align([thick, l, h], orient, align, center=center, noncentered=V_UP+V_BACK, orig_orient=ORIENT_Y) {
		difference() {
			union() {
				if (!diagonly) {
					translate([0, 0, -h/2])
						narrowing_strut(w=thick, l=l, wall=strut, ang=ang);
					translate([0, -l/2, 0])
						xrot(-90) narrowing_strut(w=thick, l=h-0.1, wall=strut, ang=ang);
				}
				intersection() {
					cube(size=[thick, l, h], center=true);
					xrot(-dang) yrot(180) {
						narrowing_strut(w=thick, l=dlen*1.2, wall=strut, ang=ang);
					}
				}
				cube(size=[wall, l-0.1, h-0.1], center=true);
			}
			xrot(-dang) {
				translate([0, 0, h/2]) {
					cube(size=[thick+0.1, l*2, h], center=true);
				}
			}
		}
	}
}


// Module: thinning_brace()
// Status: DEPRECATED, use `thinning_triangle(..., diagonly=true)` instead.
//
// Description:
//   Makes a triangular wall which thins to a smaller width in the center,
//   with angled supports to prevent critical overhangs.  Basically an alias
//   of thinning_triangle(), with diagonly=true.
//
// Usage:
//   thinning_brace(h, l, thick, [ang], [strut], [wall], [center])
//
// Arguments:
//   h = height of wall.
//   l = length of wall.
//   thick = thickness of wall.
//   ang = maximum overhang angle of diagonal brace.
//   strut = the width of the diagonal brace.
//   wall = the thickness of the thinned portion of the wall.
module thinning_brace(h=50, l=100, thick=5, ang=30, strut=5, wall=3, center=true)
{
	deprecate("thinning_brace()", "thinning_triangle(..., diagonly=true)");
	thinning_triangle(h=h, l=l, thick=thick, ang=ang, strut=strut, wall=wall, diagonly=true, center=center);
}


// Module: sparse_strut()
//
// Description:
//   Makes an open rectangular strut with X-shaped cross-bracing, designed to reduce
//   the need for support material in 3D printing.
//
// Usage:
//   sparse_strut(h, l, thick, [strut], [maxang], [max_bridge], [orient], [align])
//
// Arguments:
//   h = height of strut wall.
//   l = length of strut wall.
//   thick = thickness of strut wall.
//   maxang = maximum overhang angle of cross-braces.
//   max_bridge = maximum bridging distance between cross-braces.
//   strut = the width of the cross-braces.
//   orient = Orientation of the length axis of the shape.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_Y`.
//   align = Alignment of the shape.  Use the `V_` constants from `constants.scad`.  Default: `V_CENTER`.
//
// Example: Typical Shape
//   sparse_strut(h=40, l=100, thick=3);
// Example: Thinner Strut
//   sparse_strut(h=40, l=100, thick=3, strut=2);
// Example: Larger maxang
//   sparse_strut(h=40, l=100, thick=3, strut=2, maxang=45);
// Example: Longer max_bridge
//   sparse_strut(h=40, l=100, thick=3, strut=2, maxang=45, max_bridge=30);
module sparse_strut(h=50, l=100, thick=4, maxang=30, strut=5, max_bridge=20, orient=ORIENT_Y, align=V_CENTER)
{
	zoff = h/2 - strut/2;
	yoff = l/2 - strut/2;

	maxhyp = 1.5 * (max_bridge+strut)/2 / sin(maxang);
	maxz = 2 * maxhyp * cos(maxang);

	zreps = ceil(2*zoff/maxz);
	zstep = 2*zoff / zreps;

	hyp = zstep/2 / cos(maxang);
	maxy = min(2 * hyp * sin(maxang), max_bridge+strut);

	yreps = ceil(2*yoff/maxy);
	ystep = 2*yoff / yreps;

	ang = atan(ystep/zstep);
	len = zstep / cos(ang);

	orient_and_align([thick, l, h], orient, align, orig_orient=ORIENT_Y) {
		zspread(zoff*2)
			cube(size=[thick, l, strut], center=true);
		yspread(yoff*2)
			cube(size=[thick, strut, h], center=true);
		yspread(ystep, n=yreps) {
			zspread(zstep, n=zreps) {
				xrot( ang) cube(size=[thick, strut, len], center=true);
				xrot(-ang) cube(size=[thick, strut, len], center=true);
			}
		}
	}
}


// Module: sparse_strut3d()
//
// Usage:
//   sparse_strut3d(h, w, l, [thick], [maxang], [max_bridge], [strut], [orient], [align]);
//
// Description:
//   Makes an open rectangular strut with X-shaped cross-bracing, designed to reduce the
//   need for support material in 3D printing.
//
// Arguments:
//   h = Z size of strut.
//   w = X size of strut.
//   l = Y size of strut.
//   thick = thickness of strut walls.
//   maxang = maximum overhang angle of cross-braces.
//   max_bridge = maximum bridging distance between cross-braces.
//   strut = the width of the cross-braces.
//   orient = Orientation of the length axis of the shape.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_Y`.
//   align = Alignment of the shape.  Use the `V_` constants from `constants.scad`.  Default: `V_CENTER`.
//
// Example: Typical Shape
//   sparse_strut3d(h=30, w=30, l=100);
// Example: Thinner strut
//   sparse_strut3d(h=30, w=30, l=100, strut=2);
// Example: Larger maxang
//   sparse_strut3d(h=30, w=30, l=100, strut=2, maxang=50);
// Example: Smaller max_bridge
//   sparse_strut3d(h=30, w=30, l=100, strut=2, maxang=50, max_bridge=20);
module sparse_strut3d(h=50, l=100, w=50, thick=3, maxang=40, strut=3, max_bridge=30, orient=ORIENT_Y, align=V_CENTER)
{

	xoff = w - thick;
	yoff = l - thick;
	zoff = h - thick;

	xreps = ceil(xoff/yoff);
	yreps = ceil(yoff/xoff);
	zreps = ceil(zoff/min(xoff, yoff));

	xstep = xoff / xreps;
	ystep = yoff / yreps;
	zstep = zoff / zreps;

	cross_ang = atan2(xstep, ystep);
	cross_len = hypot(xstep, ystep);

	supp_ang = min(maxang, min(atan2(max_bridge, zstep), atan2(cross_len/2, zstep)));
	supp_reps = floor(cross_len/2/(zstep*sin(supp_ang)));
	supp_step = cross_len/2/supp_reps;

	orient_and_align([w, l, h], orient, align, orig_orient=ORIENT_Y) {
		intersection() {
			union() {
				ybridge = (l - (yreps+1) * strut) / yreps;
				xspread(xoff) sparse_strut(h=h, l=l, thick=thick, maxang=maxang, strut=strut, max_bridge=ybridge/ceil(ybridge/max_bridge));
				yspread(yoff) zrot(90) sparse_strut(h=h, l=w, thick=thick, maxang=maxang, strut=strut, max_bridge=max_bridge);
				for(zs = [0:zreps-1]) {
					for(xs = [0:xreps-1]) {
						for(ys = [0:yreps-1]) {
							translate([(xs+0.5)*xstep-xoff/2, (ys+0.5)*ystep-yoff/2, (zs+0.5)*zstep-zoff/2]) {
								zflip_copy(offset=-(zstep-strut)/2) {
									xflip_copy() {
										zrot(cross_ang) {
											down(strut/2) {
												cube([strut, cross_len, strut], center=true);
											}
											if (zreps>1) {
												back(cross_len/2) {
													zrot(-cross_ang) {
														down(strut) upcube([strut, strut, zstep+strut]);
													}
												}
											}
											for (soff = [0 : supp_reps-1] ) {
												yflip_copy() {
													back(soff*supp_step) {
														skew_xy(ya=supp_ang) {
															upcube([strut, strut, zstep]);
														}
													}
												}
											}
										}
									}
								}
							}
						}
					}
				}
			}
			cube([w,l,h], center=true);
		}
	}
}


// Module: corrugated_wall()
//
// Description:
//   Makes a corrugated wall which relieves contraction stress while still
//   providing support strength.  Designed with 3D printing in mind.
//
// Usage:
//   corrugated_wall(h, l, thick, [strut], [wall], [orient], [align]);
//
// Arguments:
//   h = height of strut wall.
//   l = length of strut wall.
//   thick = thickness of strut wall.
//   strut = the width of the cross-braces.
//   wall = thickness of corrugations.
//   orient = Orientation of the length axis of the shape.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_Y`.
//   align = Alignment of the shape.  Use the `V_` constants from `constants.scad`.  Default: `V_CENTER`.
//
// Example: Typical Shape
//   corrugated_wall(h=50, l=100);
// Example: Wider Strut
//   corrugated_wall(h=50, l=100, strut=8);
// Example: Thicker Wall
//   corrugated_wall(h=50, l=100, strut=8, wall=3);
module corrugated_wall(h=50, l=100, thick=5, strut=5, wall=2, orient=ORIENT_Y, align=V_CENTER)
{
	amplitude = (thick - wall) / 2;
	period = min(15, thick * 2);
	steps = quantup(segs(thick/2),4);
	step = period/steps;
	il = l - 2*strut + 2*step;
	orient_and_align([thick, l, h], orient, align, orig_orient=ORIENT_Y) {
		linear_extrude(height=h-2*strut+0.1, slices=2, convexity=ceil(2*il/period), center=true) {
			polygon(
				points=concat(
					[for (y=[-il/2:step:il/2]) [amplitude*sin(y/period*360)-wall/2, y] ],
					[for (y=[il/2:-step:-il/2]) [amplitude*sin(y/period*360)+wall/2, y] ]
				)
			);
		}

		difference() {
			cube([thick, l, h], center=true);
			cube([thick+0.5, l-2*strut, h-2*strut], center=true);
		}
	}
}


// Section: Miscellaneous


// Module: nil()
//
// Description:
//   Useful when you MUST pass a child to a module, but you want it to be nothing.
module nil() union() {}


// Module: noop()
//
// Description:
//   Passes through the children passed to it, with no action at all.
//   Useful while debugging when you want to replace a command.
module noop() children();


// Module: pie_slice()
//
// Description:
//   Creates a pie slice shape.
//
// Usage:
//   pie_slice(ang, l|h, r|d, [orient], [align|center]);
//   pie_slice(ang, l|h, r1|d1, r2|d2, [orient], [align|center]);
//
// Arguments:
//   ang = pie slice angle in degrees.
//   h = height of pie slice.
//   r = radius of pie slice.
//   r1 = bottom radius of pie slice.
//   r2 = top radius of pie slice.
//   d = diameter of pie slice.
//   d1 = bottom diameter of pie slice.
//   d2 = top diameter of pie slice.
//   orient = Orientation of the pie slice.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_Z`.
//   align = Alignment of the pie slice.  Use the `V_` constants from `constants.scad`.  Default: `V_CENTER`.
//   center = If given, overrides `align`.  A true value sets `align=V_CENTER`, false sets `align=ALIGN_POS`.
//
// Example: Cylindrical Pie Slice
//   pie_slice(ang=45, l=20, r=30);
// Example: Conical Pie Slice
//   pie_slice(ang=60, l=20, d1=50, d2=70);
module pie_slice(
	ang=30, l=undef,
	r=undef, r1=undef, r2=undef,
	d=undef, d1=undef, d2=undef,
	orient=ORIENT_Z, align=ALIGN_POS,
	center=undef, h=undef
) {
	l = first_defined([l, h, 1]);
	r1 = get_radius(r1, r, d1, d, 10);
	r2 = get_radius(r2, r, d2, d, 10);
	maxd = max(r1,r2)+0.1;
	orient_and_align([2*r1, 2*r1, l], orient, align, center=center) {
		difference() {
			cylinder(r1=r1, r2=r2, h=l, center=true);
			if (ang<180) rotate(ang) back(maxd/2) cube([2*maxd, maxd, l+0.1], center=true);
			difference() {
				fwd(maxd/2) cube([2*maxd, maxd, l+0.2], center=true);
				if (ang>180) rotate(ang-180) back(maxd/2) cube([2*maxd, maxd, l+0.1], center=true);
			}
		}
	}
}


// Module: interior_fillet()
//
// Description:
//   Creates a shape that can be unioned into a concave joint between two faces, to fillet them.
//   Center this part along the concave edge to be chamferred and union it in.
//
// Usage:
//   interior_fillet(l, r, [ang], [overlap], [orient], [align]);
//
// Arguments:
//   l = length of edge to fillet.
//   r = radius of fillet.
//   ang = angle between faces to fillet.
//   overlap = overlap size for unioning with faces.
//   orient = Orientation of the fillet.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_X`.
//   align = Alignment of the fillet.  Use the `V_` or `ALIGN_` constants from `constants.scad`.  Default: `V_CENTER`.
//
// Example:
//   union() {
//       translate([0,2,-4]) upcube([20, 4, 24]);
//       translate([0,-10,-4]) upcube([20, 20, 4]);
//       color("green") interior_fillet(l=20, r=10, orient=ORIENT_XNEG);
//   }
//
// Example:
//   interior_fillet(l=40, r=10, orient=ORIENT_Y_90);
module interior_fillet(l=1.0, r=1.0, ang=90, overlap=0.01, orient=ORIENT_X, align=V_CENTER) {
	dy = r/tan(ang/2);
	orient_and_align([l,r,r], orient, align, orig_orient=ORIENT_X) {
		difference() {
			translate([0,-overlap/tan(ang/2),-overlap]) {
				if (ang == 90) {
					translate([0,r/2,r/2]) cube([l,r,r], center=true);
				} else {
					rotate([90,0,90]) pie_slice(ang=ang, r=dy+overlap, h=l, center=true);
				}
			}
			translate([0,dy,r]) xcyl(l=l+0.1, r=r);
		}
	}
}



// Module: slot()
// 
// Description:
//   Makes a linear slot with rounded ends, appropriate for bolts to slide along.
//
// Usage:
//   slot(h, l, r|d, [orient], [align|center]);
//   slot(h, p1, p2, r|d, [orient], [align|center]);
//   slot(h, l, r1|d1, r2|d2, [orient], [align|center]);
//   slot(h, p1, p2, r1|d1, r2|d2, [orient], [align|center]);
//
// Arguments:
//   p1 = center of starting circle of slot.
//   p2 = center of ending circle of slot.
//   l = length of slot along the X axis.
//   h = height of slot shape. (default: 10)
//   r = radius of slot circle. (default: 5)
//   r1 = bottom radius of slot cone.
//   r2 = top radius of slot cone.
//   d = diameter of slot circle.
//   d1 = bottom diameter of slot cone.
//   d2 = top diameter of slot cone.
//
// Example: Between Two Points
//   slot([0,0,0], [50,50,0], r1=5, r2=10, h=5);
// Example: By Length
//   slot(l=50, r1=5, r2=10, h=5);
module slot(
	p1=undef, p2=undef, h=10, l=undef,
	r=undef, r1=undef, r2=undef,
	d=undef, d1=undef, d2=undef
) {
	r1 = get_radius(r1=r1, r=r, d1=d1, d=d, dflt=5);
	r2 = get_radius(r1=r2, r=r, d1=d2, d=d, dflt=5);
	sides = quantup(segs(max(r1, r2)), 4);
	hull() spread(p1=p1, p2=p2, l=l, n=2) cyl(l=h, r1=r1, r2=r2, center=true, $fn=sides);
}


// Module: arced_slot()
// 
// Description:
//   Makes an arced slot, appropriate for bolts to slide along.
//
// Usage:
//   arced_slot(h, r|d, sr|sd, [sa], [ea], [orient], [align|center], [$fn2]);
//   arced_slot(h, r|d, sr1|sd1, sr2|sd2, [sa], [ea], [orient], [align|center], [$fn2]);
//
// Arguments:
//   cp = centerpoint of slot arc. (default: [0, 0, 0])
//   h = height of slot arc shape. (default: 1.0)
//   r = radius of slot arc. (default: 0.5)
//   d = diameter of slot arc. (default: 1.0)
//   sr = radius of slot channel. (default: 0.5)
//   sd = diameter of slot channel. (default: 0.5)
//   sr1 = bottom radius of slot channel cone. (use instead of sr)
//   sr2 = top radius of slot channel cone. (use instead of sr)
//   sd1 = bottom diameter of slot channel cone. (use instead of sd)
//   sd2 = top diameter of slot channel cone. (use instead of sd)
//   sa = starting angle. (Default: 0.0)
//   ea = ending angle. (Default: 90.0)
//   orient = Orientation of the arced slot.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_Z`.
//   align = Alignment of the arced slot.  Use the `V_` constants from `constants.scad`.  Default: `V_CENTER`.
//   center = If true, centers vertically.  If false, drops flush with XY plane.  Overrides `align`.
//   $fn2 = The $fn value to use on the small round endcaps.  The major arcs are still based on $fn.  Default: $fn
//
// Example: Typical Arced Slot
//   arced_slot(d=60, h=5, sd=10, sa=60, ea=280);
// Example: Conical Arced Slot
//   arced_slot(r=60, h=5, sd1=10, sd2=15, sa=45, ea=180);
module arced_slot(
	r=undef, d=undef, h=1.0,
	sr=undef, sr1=undef, sr2=undef,
	sd=undef, sd1=undef, sd2=undef,
	sa=0, ea=90, cp=[0,0,0],
	orient=ORIENT_Z, align=V_CENTER,
	$fn2 = undef
) {
	r = get_radius(r=r, d=d, dflt=2);
	sr1 = get_radius(sr1, sr, sd1, sd, 2);
	sr2 = get_radius(sr2, sr, sd2, sd, 2);
	fn_minor = first_defined([$fn2, $fn]);
	da = ea - sa;
	orient_and_align([r+sr1, r+sr1, h], orient, align) {
		translate(cp) {
			zrot(sa) {
				difference() {
					pie_slice(ang=da, l=h, r1=r+sr1, r2=r+sr2, orient=ORIENT_Z, align=V_CENTER);
					cylinder(h=h+0.1, r1=r-sr1, r2=r-sr2, center=true);
				}
				right(r) cylinder(h=h, r1=sr1, r2=sr2, center=true, $fn=fn_minor);
				zrot(da) right(r) cylinder(h=h, r1=sr1, r2=sr2, center=true, $fn=fn_minor);
			}
		}
	}
}



// vim: noexpandtab tabstop=4 shiftwidth=4 softtabstop=4 nowrap
