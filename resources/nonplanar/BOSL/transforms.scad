//////////////////////////////////////////////////////////////////////
// LibFile: transforms.scad
//   This is the file that the most commonly used transformations, distributors, and mutator are in.
//   To use, add the following lines to the beginning of your file:
//   ```
//   include <BOSL/constants.scad>
//   use <BOSL/transforms.scad>
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
include <compat.scad>
include <constants.scad>



//////////////////////////////////////////////////////////////////////
// Section: Translations
//////////////////////////////////////////////////////////////////////


// Module: move()
//
// Description:
//   Moves/translates children.
//
// Usage:
//   move([x], [y], [z]) ...
//   move([x,y,z]) ...
//
// Arguments:
//   x = X axis translation.
//   y = Y axis translation.
//   z = Z axis translation.
//
// Example:
//   #sphere(d=10);
//   move([0,20,30]) sphere(d=10);
//
// Example:
//   #sphere(d=10);
//   move(y=20) sphere(d=10);
//
// Example:
//   #sphere(d=10);
//   move(x=-10, y=-5) sphere(d=10);
module move(a=[0,0,0], x=0, y=0, z=0)
{
	translate(a+[x,y,z]) children();
}


// Module: xmove()
//
// Description:
//   Moves/translates children the given amount along the X axis.
//
// Usage:
//   xmove(x) ...
//
// Arguments:
//   x = Amount to move right along the X axis.  Negative values move left.
//
// Example:
//   #sphere(d=10);
//   xmove(20) sphere(d=10);
module xmove(x=0) translate([x,0,0]) children();


// Module: ymove()
//
// Description:
//   Moves/translates children the given amount along the Y axis.
//
// Usage:
//   ymove(y) ...
//
// Arguments:
//   y = Amount to move back along the Y axis.  Negative values move forward.
//
// Example:
//   #sphere(d=10);
//   ymove(20) sphere(d=10);
module ymove(y=0) translate([0,y,0]) children();


// Module: zmove()
//
// Description:
//   Moves/translates children the given amount along the Z axis.
//
// Usage:
//   zmove(z) ...
//
// Arguments:
//   z = Amount to move up along the Z axis.  Negative values move down.
//
// Example:
//   #sphere(d=10);
//   zmove(20) sphere(d=10);
module zmove(z=0) translate([0,0,z]) children();


// Module: left()
//
// Description:
//   Moves children left (in the X- direction) by the given amount.
//
// Usage:
//   left(x) ...
//
// Arguments:
//   x = Scalar amount to move left.
//
// Example:
//   #sphere(d=10);
//   left(20) sphere(d=10);
module left(x=0) translate([-x,0,0]) children();


// Module: right()
//
// Description:
//   Moves children right (in the X+ direction) by the given amount.
//
// Usage:
//   right(x) ...
//
// Arguments:
//   x = Scalar amount to move right.
//
// Example:
//   #sphere(d=10);
//   right(20) sphere(d=10);
module right(x=0) translate([x,0,0]) children();


// Module: fwd() / forward()
//
// Description:
//   Moves children forward (in the Y- direction) by the given amount.
//
// Usage:
//   fwd(y) ...
//   forward(y) ...
//
// Arguments:
//   y = Scalar amount to move forward.
//
// Example:
//   #sphere(d=10);
//   fwd(20) sphere(d=10);
module forward(y=0) translate([0,-y,0]) children();
module fwd(y=0) translate([0,-y,0]) children();


// Module: back()
//
// Description:
//   Moves children back (in the Y+ direction) by the given amount.
//
// Usage:
//   back(y) ...
//
// Arguments:
//   y = Scalar amount to move back.
//
// Example:
//   #sphere(d=10);
//   back(20) sphere(d=10);
module back(y=0) translate([0,y,0]) children();


// Module: down()
//
// Description:
//   Moves children down (in the Z- direction) by the given amount.
//
// Usage:
//   down(z) ...
//
// Arguments:
//   z = Scalar amount to move down.
//
// Example:
//   #sphere(d=10);
//   down(20) sphere(d=10);
module down(z=0) translate([0,0,-z]) children();


// Module: up()
//
// Description:
//   Moves children up (in the Z+ direction) by the given amount.
//
// Usage:
//   up(z) ...
//
// Arguments:
//   z = Scalar amount to move up.
//
// Example:
//   #sphere(d=10);
//   up(20) sphere(d=10);
module up(z=0) translate([0,0,z]) children();



//////////////////////////////////////////////////////////////////////
// Section: Rotations
//////////////////////////////////////////////////////////////////////


// Module: rot()
//
// Description:
//   Rotates children around an arbitrary axis by the given number of degrees.
//   Can be used as a drop-in replacement for `rotate()`, with extra features.
//
// Usage:
//   rot(a, [cp], [reverse]) ...
//   rot([X,Y,Z], [cp], [reverse]) ...
//   rot(a, v, [cp], [reverse]) ...
//   rot(from, to, [a], [reverse]) ...
//
// Arguments:
//   a = Scalar angle or vector of XYZ rotation angles to rotate by, in degrees.
//   v = vector for the axis of rotation.  Default: [0,0,1] or V_UP
//   cp = centerpoint to rotate around. Default: [0,0,0]
//   from = Starting vector for vector-based rotations.
//   to = Target vector for vector-based rotations.
//   reverse = If true, exactly reverses the rotation, including axis rotation ordering.  Default: false
//
// Example:
//   #cube([2,4,9]);
//   rot([30,60,0], cp=[0,0,9]) cube([2,4,9]);
//
// Example:
//   #cube([2,4,9]);
//   rot(30, v=[1,1,0], cp=[0,0,9]) cube([2,4,9]);
//
// Example:
//   #cube([2,4,9]);
//   rot(from=V_UP, to=V_LEFT+V_BACK) cube([2,4,9]);
module rot(a=0, v=undef, cp=undef, from=undef, to=undef, reverse=false)
{
	if (is_def(cp)) {
		translate(cp) rot(a=a, v=v, from=from, to=to, reverse=reverse) translate(-cp) children();
	} else if (is_def(from)) {
		assertion(is_def(to), "`from` and `to` should be used together.");
		axis = vector_axis(from, to);
		ang = vector_angle(from, to);
		if (ang < 0.0001 && a == 0) {
			children();  // May be slightly faster?
		} else if (reverse) {
			rotate(a=-ang, v=axis) rotate(a=-a, v=from) children();
		} else {
			rotate(a=ang, v=axis) rotate(a=a, v=from) children();
		}
	} else if (a == 0) {
		children();  // May be slightly faster?
	} else if (reverse) {
		if (is_def(v)) {
			rotate(a=-a, v=v) children();
		} else if (is_scalar(a)) {
			rotate(-a) children();
		} else {
			rotate([-a[0],0,0]) rotate([0,-a[1],0]) rotate([0,0,-a[2]]) children();
		}
	} else {
		rotate(a=a, v=v) children();
	}
}


// Module: xrot()
//
// Description:
//   Rotates children around the X axis by the given number of degrees.
//
// Usage:
//   xrot(a, [cp]) ...
//
// Arguments:
//   a = angle to rotate by in degrees.
//   cp = centerpoint to rotate around. Default: [0,0,0]
//
// Example:
//   #cylinder(h=50, r=10, center=true);
//   xrot(90) cylinder(h=50, r=10, center=true);
module xrot(a=0, cp=undef)
{
	if (a==0) {
		children();  // May be slightly faster?
	} else if (is_def(cp)) {
		translate(cp) rotate([a, 0, 0]) translate(-cp) children();
	} else {
		rotate([a, 0, 0]) children();
	}
}


// Module: yrot()
//
// Description:
//   Rotates children around the Y axis by the given number of degrees.
//
// Usage:
//   yrot(a, [cp]) ...
//
// Arguments:
//   a = angle to rotate by in degrees.
//   cp = centerpoint to rotate around. Default: [0,0,0]
//
// Example:
//   #cylinder(h=50, r=10, center=true);
//   yrot(90) cylinder(h=50, r=10, center=true);
module yrot(a=0, cp=undef)
{
	if (a==0) {
		children();  // May be slightly faster?
	} else if (is_def(cp)) {
		translate(cp) rotate([0, a, 0]) translate(-cp) children();
	} else {
		rotate([0, a, 0]) children();
	}
}


// Module: zrot()
//
// Description:
//   Rotates children around the Z axis by the given number of degrees.
//
// Usage:
//   zrot(a, [cp]) ...
//
// Arguments:
//   a = angle to rotate by in degrees.
//   cp = centerpoint to rotate around. Default: [0,0,0]
//
// Example:
//   #cube(size=[60,20,40], center=true);
//   zrot(90) cube(size=[60,20,40], center=true);
module zrot(a=0, cp=undef)
{
	if (a==0) {
		children();  // May be slightly faster?
	} else if (is_def(cp)) {
		translate(cp) rotate(a) translate(-cp) children();
	} else {
		rotate(a) children();
	}
}



//////////////////////////////////////////////////////////////////////
// Section: Scaling and Mirroring
//////////////////////////////////////////////////////////////////////


// Module: xscale()
//
// Description:
//   Scales children by the given factor on the X axis.
//
// Usage:
//   xscale(x) ...
//
// Arguments:
//   x = Factor to scale by along the X axis.
//
// Example:
//   xscale(3) sphere(r=10);
module xscale(x) scale([x,1,1]) children();


// Module: yscale()
//
// Description:
//   Scales children by the given factor on the Y axis.
//
// Usage:
//   yscale(y) ...
//
// Arguments:
//   y = Factor to scale by along the Y axis.
//
// Example:
//   yscale(3) sphere(r=10);
module yscale(y) scale([1,y,1]) children();


// Module: zscale()
//
// Description:
//   Scales children by the given factor on the Z axis.
//
// Usage:
//   zscale(z) ...
//
// Arguments:
//   z = Factor to scale by along the Z axis.
//
// Example:
//   zscale(3) sphere(r=10);
module zscale(z) scale([1,1,z]) children();


// Module: xflip()
//
// Description:
//   Mirrors the children along the X axis, like `mirror([1,0,0])` or `xscale(-1)`
//
// Usage:
//   xflip([cp]) ...
//
// Arguments:
//   cp = A point that lies on the plane of reflection.
//
// Example:
//   xflip() yrot(90) cylinder(d1=10, d2=0, h=20);
//   color("blue", 0.25) cube([0.01,15,15], center=true);
//   color("red", 0.333) yrot(90) cylinder(d1=10, d2=0, h=20);
//
// Example:
//   xflip(cp=[-5,0,0]) yrot(90) cylinder(d1=10, d2=0, h=20);
//   color("blue", 0.25) left(5) cube([0.01,15,15], center=true);
//   color("red", 0.333) yrot(90) cylinder(d1=10, d2=0, h=20);
module xflip(cp=[0,0,0]) translate(cp) mirror([1,0,0]) translate(-cp) children();


// Module: yflip()
//
// Description:
//   Mirrors the children along the Y axis, like `mirror([0,1,0])` or `yscale(-1)`
//
// Usage:
//   yflip([cp]) ...
//
// Arguments:
//   cp = A point that lies on the plane of reflection.
//
// Example:
//   yflip() xrot(90) cylinder(d1=10, d2=0, h=20);
//   color("blue", 0.25) cube([15,0.01,15], center=true);
//   color("red", 0.333) xrot(90) cylinder(d1=10, d2=0, h=20);
//
// Example:
//   yflip(cp=[0,5,0]) xrot(90) cylinder(d1=10, d2=0, h=20);
//   color("blue", 0.25) back(5) cube([15,0.01,15], center=true);
//   color("red", 0.333) xrot(90) cylinder(d1=10, d2=0, h=20);
module yflip(cp=[0,0,0]) translate(cp) mirror([0,1,0]) translate(-cp) children();


// Module: zflip()
//
// Description:
//   Mirrors the children along the Z axis, like `mirror([0,0,1])` or `zscale(-1)`
//
// Usage:
//   zflip([cp]) ...
//
// Arguments:
//   cp = A point that lies on the plane of reflection.
//
// Example:
//   zflip() cylinder(d1=10, d2=0, h=20);
//   color("blue", 0.25) cube([15,15,0.01], center=true);
//   color("red", 0.333) cylinder(d1=10, d2=0, h=20);
//
// Example:
//   zflip(cp=[0,0,-5]) cylinder(d1=10, d2=0, h=20);
//   color("blue", 0.25) down(5) cube([15,15,0.01], center=true);
//   color("red", 0.333) cylinder(d1=10, d2=0, h=20);
module zflip(cp=[0,0,0]) translate(cp) mirror([0,0,1]) translate(-cp) children();



//////////////////////////////////////////////////////////////////////
// Section: Skewing
//////////////////////////////////////////////////////////////////////


// Module: skew_xy() / skew_z()
//
// Description:
//   Skews children on the X-Y plane, keeping constant in Z.
//
// Usage:
//   skew_xy([xa], [ya]) ...
//   skew_z([xa], [ya]) ...
//
// Arguments:
//   xa = skew angle towards the X direction.
//   ya = skew angle towards the Y direction.
//   planar = If true, this becomes a 2D operation.
//
// Example(FlatSpin):
//   #cube(size=10);
//   skew_xy(xa=30, ya=15) cube(size=10);
// Example(2D):
//   skew_xy(xa=15,ya=30,planar=true) square(30);
module skew_xy(xa=0, ya=0, planar=false) multmatrix(m = planar? matrix3_skew(xa, ya) : matrix4_skew_xy(xa, ya)) children();
module zskew(xa=0, ya=0, planar=false) multmatrix(m = planar? matrix3_skew(xa, ya) : matrix4_skew_xy(xa, ya)) children();


// Module: skew_yz() / skew_x()
//
// Description:
//   Skews children on the Y-Z plane, keeping constant in X.
//
// Usage:
//   skew_yz([ya], [za]) ...
//   skew_x([ya], [za]) ...
//
// Arguments:
//   ya = skew angle towards the Y direction.
//   za = skew angle towards the Z direction.
//
// Example(FlatSpin):
//   #cube(size=10);
//   skew_yz(ya=30, za=15) cube(size=10);
module skew_yz(ya=0, za=0) multmatrix(m = matrix4_skew_yz(ya, za)) children();
module xskew(ya=0, za=0) multmatrix(m = matrix4_skew_yz(ya, za)) children();


// Module: skew_xz() / skew_y()
//
// Description:
//   Skews children on the X-Z plane, keeping constant in Y.
//
// Usage:
//   skew_xz([xa], [za]) ...
//   skew_y([xa], [za]) ...
//
// Arguments:
//   xa = skew angle towards the X direction.
//   za = skew angle towards the Z direction.
//
// Example(FlatSpin):
//   #cube(size=10);
//   skew_xz(xa=15, za=-10) cube(size=10);
module skew_xz(xa=0, za=0) multmatrix(m = matrix4_skew_xz(xa, za)) children();
module yskew(xa=0, za=0) multmatrix(m = matrix4_skew_xz(xa, za)) children();



//////////////////////////////////////////////////////////////////////
// Section: Translational Distributors
//////////////////////////////////////////////////////////////////////


// Module: place_copies()
//
// Description:
//   Makes copies of the given children at each of the given offsets.
//
// Usage:
//   place_copies(a) ...
//
// Arguments:
//   a = array of XYZ offset vectors. Default [[0,0,0]]
//
// Side Effects:
//   `$pos` is set to the relative centerpoint of each child copy, and can be used to modify each child individually.
//
// Example:
//   #sphere(r=10);
//   place_copies([[-25,-25,0], [25,-25,0], [0,0,50], [0,25,0]]) sphere(r=10);
module place_copies(a=[[0,0,0]])
{
	for (off = a) translate(off) children();
}


// Module: translate_copies()
// Status: DEPRECATED, use `place_copies()` instead.
//
// Description:
//   Makes copies of the given children at each of the given offsets.
//
// Usage:
//   translate_copies(a) ...
//
// Arguments:
//   a = array of XYZ offset vectors. Default [[0,0,0]]
//
// Side Effects:
//   `$pos` is set to the relative centerpoint of each child copy, and can be used to modify each child individually.
module translate_copies(a=[[0,0,0]])
{
	deprecate("translate_copies()", "place_copies()");
	place_copies(a) children();
}


// Module: line_of()
// Status: DEPRECATED, use `spread(p1,p2)` instead
//
// Description:
//   Evenly distributes n duplicate children along an XYZ line.
//
// Usage:
//   line_of(p1, p2, [n]) ...
//
// Arguments:
//   p1 = starting point of line.  (Default: [0,0,0])
//   p2 = ending point of line.  (Default: [10,0,0])
//   n = number of copies to distribute along the line. (Default: 2)
//
// Side Effects:
//   `$pos` is set to the relative centerpoint of each child copy, and can be used to modify each child individually.
module line_of(p1=[0,0,0], p2=[10,0,0], n=2)
{
	deprecate("line_of()", "spread()");
	spread(p1=p1, p2=p2, n=n) children();
}



// Module: spread()
//
// Description:
//   Evenly distributes `n` copies of all children along a line.
//   Copies every child at each position.
//
// Usage:
//   spread(l, [n], [p1]) ...
//   spread(l, spacing, [p1]) ...
//   spread(spacing, [n], [p1]) ...
//   spread(p1, p2, [n]) ...
//   spread(p1, p2, spacing) ...
//
// Arguments:
//   p1 = Starting point of line.
//   p2 = Ending point of line.
//   l = Length to spread copies over.
//   spacing = A 3D vector indicating which direction and distance to place each subsequent copy at.
//   n = Number of copies to distribute along the line. (Default: 2)
//
// Side Effects:
//   `$pos` is set to the relative centerpoint of each child copy, and can be used to modify each child individually.
//   `$idx` is set to the index number of each child being copied.
//
// Example(FlatSpin):
//   spread([0,0,0], [5,5,20], n=6) cube(size=[3,2,1],center=true);
// Examples:
//   spread(l=40, n=6) cube(size=[3,2,1],center=true);
//   spread(l=[15,30], n=6) cube(size=[3,2,1],center=true);
//   spread(l=40, spacing=10) cube(size=[3,2,1],center=true);
//   spread(spacing=[5,5,0], n=5) cube(size=[3,2,1],center=true);
// Example:
//   spread(l=20, n=3) {
//       cube(size=[1,3,1],center=true);
//       cube(size=[3,1,1],center=true);
//   }
module spread(p1, p2, spacing, l, n)
{
	ll = (
		is_def(l)? scalar_vec3(l, 0) :
		(is_def(spacing) && is_def(n))? (n * scalar_vec3(spacing, 0)) :
		(is_def(p1) && is_def(p2))? point3d(p2-p1) :
		undef
	);
	cnt = (
		is_def(n)? n :
		(is_def(spacing) && is_def(ll))? floor(norm(ll) / norm(scalar_vec3(spacing, 0)) + 1.000001) :
		2
	);
	spc = (
		!is_def(spacing)? (ll/(cnt-1)) :
		is_scalar(spacing) && is_def(ll)? (ll/(cnt-1)) :
		scalar_vec3(spacing, 0)
	);
	assertion(is_def(cnt), "Need two of `spacing`, 'l', 'n', or `p1`/`p2` arguments in `spread()`.");
	spos = is_def(p1)? point3d(p1) : -(cnt-1)/2 * spc;
	for (i=[0 : cnt-1]) {
		pos = i * spc + spos;
		$pos = pos;
		$idx = i;
		translate(pos) children();
	}
}


// Module: xspread()
//
// Description:
//   Spreads out `n` copies of the children along a line on the X axis.
//
// Usage:
//   xspread(spacing, [n], [sp]) ...
//   xspread(l, [n], [sp]) ...
//
// Arguments:
//   spacing = spacing between copies. (Default: 1.0)
//   n = Number of copies to spread out. (Default: 2)
//   l = Length to spread copies over.
//   sp = If given, copies will be spread on a line to the right of starting position `sp`.  If not given, copies will be spread along a line that is centered at [0,0,0].
//
// Side Effects:
//   `$pos` is set to the relative centerpoint of each child copy, and can be used to modify each child individually.
//   `$idx` is set to the index number of each child being copied.
//
// Examples:
//   xspread(20) sphere(3);
//   xspread(20, n=3) sphere(3);
//   xspread(spacing=15, l=50) sphere(3);
//   xspread(n=4, l=30, sp=[0,10,0]) sphere(3);
// Example:
//   xspread(10, n=3) {
//       cube(size=[1,3,1],center=true);
//       cube(size=[3,1,1],center=true);
//   }
module xspread(spacing=undef, n=undef, l=undef, sp=undef)
{
	spread(
		l=is_undef(l)? l : l*V_RIGHT,
		spacing=is_undef(spacing)? spacing : spacing*V_RIGHT,
		n=n, p1=sp
	) children();
}


// Module: yspread()
//
// Description:
//   Spreads out `n` copies of the children along a line on the Y axis.
//
// Usage:
//   yspread(spacing, [n], [sp]) ...
//   yspread(l, [n], [sp]) ...
//
// Arguments:
//   spacing = spacing between copies. (Default: 1.0)
//   n = Number of copies to spread out. (Default: 2)
//   l = Length to spread copies over.
//   sp = If given, copies will be spread on a line back from starting position `sp`.  If not given, copies will be spread along a line that is centered at [0,0,0].
//
// Side Effects:
//   `$pos` is set to the relative centerpoint of each child copy, and can be used to modify each child individually.
//   `$idx` is set to the index number of each child being copied.
//
// Examples:
//   yspread(20) sphere(3);
//   yspread(20, n=3) sphere(3);
//   yspread(spacing=15, l=50) sphere(3);
//   yspread(n=4, l=30, sp=[10,0,0]) sphere(3);
// Example:
//   yspread(10, n=3) {
//       cube(size=[1,3,1],center=true);
//       cube(size=[3,1,1],center=true);
//   }
module yspread(spacing=undef, n=undef, l=undef, sp=undef)
{
	spread(
		l=is_undef(l)? l : l*V_BACK,
		spacing=is_undef(spacing)? spacing : spacing*V_BACK,
		n=n, p1=sp
	) children();
}


// Module: zspread()
//
// Description:
//   Spreads out `n` copies of the children along a line on the Z axis.
//
// Usage:
//   zspread(spacing, [n], [sp]) ...
//   zspread(l, [n], [sp]) ...
//
// Arguments:
//   spacing = spacing between copies. (Default: 1.0)
//   n = Number of copies to spread out. (Default: 2)
//   l = Length to spread copies over.
//   sp = If given, copies will be spread on a line up from starting position `sp`.  If not given, copies will be spread along a line that is centered at [0,0,0].
//
// Side Effects:
//   `$pos` is set to the relative centerpoint of each child copy, and can be used to modify each child individually.
//   `$idx` is set to the index number of each child being copied.
//
// Examples:
//   zspread(20) sphere(3);
//   zspread(20, n=3) sphere(3);
//   zspread(spacing=15, l=50) sphere(3);
//   zspread(n=4, l=30, sp=[10,0,0]) sphere(3);
// Example:
//   zspread(10, n=3) {
//       cube(size=[1,3,1],center=true);
//       cube(size=[3,1,1],center=true);
//   }
module zspread(spacing=undef, n=undef, l=undef, sp=undef)
{
	spread(
		l=is_undef(l)? l : l*V_UP,
		spacing=is_undef(spacing)? spacing : spacing*V_UP,
		n=n, p1=sp
	) children();
}



// Module: distribute()
//
// Description:
//   Spreads out each individual child along the direction `dir`.
//   Every child is placed at a different position, in order.
//   This is useful for laying out groups of disparate objects
//   where you only really care about the spacing between them.
//
// Usage:
//   distribute(spacing, dir, [sizes]) ...
//   distribute(l, dir, [sizes]) ...
//
// Arguments:
//   spacing = Spacing to add between each child. (Default: 10.0)
//   sizes = Array containing how much space each child will need.
//   dir = Vector direction to distribute copies along.
//   l = Length to distribute copies along.
//
// Side Effect:
//   `$pos` is set to the relative centerpoint of each child copy, and can be used to modify each child individually.
//   `$idx` is set to the index number of each child being copied.
//
// Example:
//   distribute(sizes=[100, 30, 50], dir=V_UP) {
//       sphere(r=50);
//       cube([10,20,30], center=true);
//       cylinder(d=30, h=50, center=true);
//   }
module distribute(spacing=undef, sizes=undef, dir=V_RIGHT, l=undef)
{
	gaps = ($children < 2)? [0] :
		is_def(sizes)? [for (i=[0:$children-2]) sizes[i]/2 + sizes[i+1]/2] :
		[for (i=[0:$children-2]) 0];
	spc = is_def(l)? ((l - sum(gaps)) / ($children-1)) : default(spacing, 10);
	gaps2 = [for (gap = gaps) gap+spc];
	spos = dir * -sum(gaps2)/2;
	for (i=[0:$children-1]) {
		totspc = sum(concat([0], slice(gaps2, 0, i)));
		$pos = spos + totspc * dir;
		$idx = i;
		translate($pos) children(i);
	}
}


// Module: xdistribute()
//
// Description:
//   Spreads out each individual child along the X axis.
//   Every child is placed at a different position, in order.
//   This is useful for laying out groups of disparate objects
//   where you only really care about the spacing between them.
//
// Usage:
//   xdistribute(spacing, [sizes]) ...
//   xdistribute(l, [sizes]) ...
//
// Arguments:
//   spacing = spacing between each child. (Default: 10.0)
//   sizes = Array containing how much space each child will need.
//   l = Length to distribute copies along.
//
// Side Effect:
//   `$pos` is set to the relative centerpoint of each child copy, and can be used to modify each child individually.
//   `$idx` is set to the index number of each child being copied.
//
// Example:
//   xdistribute(sizes=[100, 10, 30], spacing=40) {
//       sphere(r=50);
//       cube([10,20,30], center=true);
//       cylinder(d=30, h=50, center=true);
//   }
module xdistribute(spacing=10, sizes=undef, l=undef)
{
	dir = V_RIGHT;
	gaps = ($children < 2)? [0] :
		is_def(sizes)? [for (i=[0:$children-2]) sizes[i]/2 + sizes[i+1]/2] :
		[for (i=[0:$children-2]) 0];
	spc = is_def(l)? ((l - sum(gaps)) / ($children-1)) : default(spacing, 10);
	gaps2 = [for (gap = gaps) gap+spc];
	spos = dir * -sum(gaps2)/2;
	for (i=[0:$children-1]) {
		totspc = sum(concat([0], slice(gaps2, 0, i)));
		$pos = spos + totspc * dir;
		$idx = i;
		translate($pos) children(i);
	}
}


// Module: ydistribute()
//
// Description:
//   Spreads out each individual child along the Y axis.
//   Every child is placed at a different position, in order.
//   This is useful for laying out groups of disparate objects
//   where you only really care about the spacing between them.
//
// Usage:
//   ydistribute(spacing, [sizes])
//   ydistribute(l, [sizes])
//
// Arguments:
//   spacing = spacing between each child. (Default: 10.0)
//   sizes = Array containing how much space each child will need.
//   l = Length to distribute copies along.
//
// Side Effect:
//   `$pos` is set to the relative centerpoint of each child copy, and can be used to modify each child individually.
//   `$idx` is set to the index number of each child being copied.
//
// Example:
//   ydistribute(sizes=[30, 20, 100], spacing=40) {
//       cylinder(d=30, h=50, center=true);
//       cube([10,20,30], center=true);
//       sphere(r=50);
//   }
module ydistribute(spacing=10, sizes=undef, l=undef)
{
	dir = V_BACK;
	gaps = ($children < 2)? [0] :
		is_def(sizes)? [for (i=[0:$children-2]) sizes[i]/2 + sizes[i+1]/2] :
		[for (i=[0:$children-2]) 0];
	spc = is_def(l)? ((l - sum(gaps)) / ($children-1)) : default(spacing, 10);
	gaps2 = [for (gap = gaps) gap+spc];
	spos = dir * -sum(gaps2)/2;
	for (i=[0:$children-1]) {
		totspc = sum(concat([0], slice(gaps2, 0, i)));
		$pos = spos + totspc * dir;
		$idx = i;
		translate($pos) children(i);
	}
}


// Module: zdistribute()
//
// Description:
//   Spreads out each individual child along the Z axis.
//   Every child is placed at a different position, in order.
//   This is useful for laying out groups of disparate objects
//   where you only really care about the spacing between them.
//
// Usage:
//   zdistribute(spacing, [sizes])
//   zdistribute(l, [sizes])
//
// Arguments:
//   spacing = spacing between each child. (Default: 10.0)
//   sizes = Array containing how much space each child will need.
//   l = Length to distribute copies along.
//
// Side Effect:
//   `$pos` is set to the relative centerpoint of each child copy, and can be used to modify each child individually.
//   `$idx` is set to the index number of each child being copied.
//
// Example:
//   zdistribute(sizes=[30, 20, 100], spacing=40) {
//       cylinder(d=30, h=50, center=true);
//       cube([10,20,30], center=true);
//       sphere(r=50);
//   }
module zdistribute(spacing=10, sizes=undef, l=undef)
{
	dir = V_UP;
	gaps = ($children < 2)? [0] :
		is_def(sizes)? [for (i=[0:$children-2]) sizes[i]/2 + sizes[i+1]/2] :
		[for (i=[0:$children-2]) 0];
	spc = is_def(l)? ((l - sum(gaps)) / ($children-1)) : default(spacing, 10);
	gaps2 = [for (gap = gaps) gap+spc];
	spos = dir * -sum(gaps2)/2;
	for (i=[0:$children-1]) {
		totspc = sum(concat([0], slice(gaps2, 0, i)));
		$pos = spos + totspc * dir;
		$idx = i;
		translate($pos) children(i);
	}
}



// Module: grid2d()
//
// Description:
//   Makes a square or hexagonal grid of copies of children.
//
// Usage:
//   grid2d(size, spacing, [stagger], [scale], [in_poly], [orient], [align]) ...
//   grid2d(size, cols, rows, [stagger], [scale], [in_poly], [orient], [align]) ...
//   grid2d(spacing, cols, rows, [stagger], [scale], [in_poly], [orient], [align]) ...
//   grid2d(spacing, in_poly, [stagger], [scale], [orient], [align]) ...
//   grid2d(cols, rows, in_poly, [stagger], [scale], [orient], [align]) ...
//
// Arguments:
//   size = The [X,Y] size to spread the copies over.
//   spacing = Distance between copies in [X,Y] or scalar distance.
//   cols = How many columns of copies to make.  If staggered, count both staggered and unstaggered columns.
//   rows = How many rows of copies to make.  If staggered, count both staggered and unstaggered rows.
//   stagger = If true, make a staggered (hexagonal) grid.  If false, make square grid.  If "alt", makes alternate staggered pattern.  Default: false
//   scale = [X,Y] scaling factors to reshape grid.
//   in_poly = If given a list of polygon points, only creates copies whose center would be inside the polygon.  Polygon can be concave and/or self crossing.
//   orient = Orientation axis for the grid.  Orientation is NOT applied to individual children.
//   align = Alignment of the grid.  Alignment is NOT applies to individual children.
//
// Side Effects:
//   `$pos` is set to the relative centerpoint of each child copy, and can be used to modify each child individually.
//   `$col` is set to the integer column number for each child.
//   `$row` is set to the integer row number for each child.
//
// Examples:
//   grid2d(size=50, spacing=10, stagger=false) cylinder(d=10, h=1);
//   grid2d(spacing=10, rows=7, cols=13, stagger=true) cylinder(d=6, h=5);
//   grid2d(spacing=10, rows=7, cols=13, stagger="alt") cylinder(d=6, h=5);
//   grid2d(size=50, rows=11, cols=11, stagger=true) cylinder(d=5, h=1);
//
// Example:
//   poly = [[-25,-25], [25,25], [-25,25], [25,-25]];
//   grid2d(spacing=5, stagger=true, in_poly=poly)
//      zrot(180/6) cylinder(d=5, h=1, $fn=6);
//   %polygon(poly);
//
// Example:
//   // Makes a grid of hexagon pillars whose tops are all angled
//   // to reflect light at [0,0,50], if they were reflective.
//   use <BOSL/math.scad>
//   hexregion = [for (a = [0:60:359.9]) 50.01*[cos(a), sin(a)]];
//   grid2d(spacing=10, stagger=true, in_poly=hexregion) {
//       // Note: You must use for(var=[val]) or let(var=val)
//       // to set vars from $pos or other special vars in this scope.
//       let (ref_v = (normalize([0,0,50]-point3d($pos)) + V_UP)/2)
//           half_of(v=-ref_v, cp=[0,0,5])
//               zrot(180/6)
//                   cylinder(h=20, d=10/cos(180/6)+0.01, $fn=6);
//   }
module grid2d(size=undef, spacing=undef, cols=undef, rows=undef, stagger=false, scale=[1,1,1], in_poly=undef, orient=ORIENT_Z, align=V_CENTER)
{
	assert_in_list("stagger", stagger, [false, true, "alt"]);
	scl = vmul(scalar_vec3(scale, 1), (stagger!=false? [0.5, sin(60), 0] : [1,1,0]));
	if (is_def(size)) {
		siz = scalar_vec3(size);
		if (is_def(spacing)) {
			spc = vmul(scalar_vec3(spacing), scl);
			maxcols = ceil(siz[0]/spc[0]);
			maxrows = ceil(siz[1]/spc[1]);
			grid2d(spacing=spacing, cols=maxcols, rows=maxrows, stagger=stagger, scale=scale, in_poly=in_poly, orient=orient, align=align) children();
		} else {
			spc = [siz[0]/cols, siz[1]/rows, 0];
			grid2d(spacing=spc, cols=cols, rows=rows, stagger=stagger, scale=scale, in_poly=in_poly, orient=orient, align=align) children();
		}
	} else {
		spc = is_array(spacing)? spacing : vmul(scalar_vec3(spacing), scl);
		bounds = is_def(in_poly)? pointlist_bounds(in_poly) : undef;
		bnds = is_def(bounds)? [for (a=[0:1]) 2*max(vabs([ for (i=[0,1]) bounds[i][a] ]))+1 ] : undef;
		mcols = is_def(cols)? cols : (is_def(spc) && is_def(bnds))? quantup(ceil(bnds[0]/spc[0])-1, 4)+1 : undef;
		mrows = is_def(rows)? rows : (is_def(spc) && is_def(bnds))? quantup(ceil(bnds[1]/spc[1])-1, 4)+1 : undef;
		siz = vmul(spc, [mcols-1, mrows-1, 0]);
		staggermod = (stagger == "alt")? 1 : 0;
		if (stagger == false) {
			orient_and_align(siz, orient, align) {
				for (row = [0:mrows-1]) {
					for (col = [0:mcols-1]) {
						pos = [col*spc[0], row*spc[1]] - point2d(siz/2);
						if (!is_def(in_poly) || point_in_polygon(pos, in_poly)>=0) {
							$col = col;
							$row = row;
							$pos = pos;
							translate(pos) rot(orient,reverse=true) children();
						}
					}
				}
			}
		} else {
			// stagger == true or stagger == "alt"
			orient_and_align(siz, orient, align) {
				cols1 = ceil(mcols/2);
				cols2 = mcols - cols1;
				for (row = [0:mrows-1]) {
					rowcols = ((row%2) == staggermod)? cols1 : cols2;
					if (rowcols > 0) {
						for (col = [0:rowcols-1]) {
							rowdx = (row%2 != staggermod)? spc[0] : 0;
							pos = [2*col*spc[0]+rowdx, row*spc[1]] - point2d(siz/2);
							if (!is_def(in_poly) || point_in_polygon(pos, in_poly)>=0) {
								$col = col * 2 + ((row%2!=staggermod)? 1 : 0);
								$row = row;
								$pos = pos;
								translate(pos) rot(orient,reverse=true) children();
							}
						}
					}
				}
			}
		}
	}
}



// Module: grid3d()
//
// Description:
//   Makes a 3D grid of duplicate children.
//
// Usage:
//   grid3d(n, spacing) ...
//   grid3d(n=[Xn,Yn,Zn], spacing=[dX,dY,dZ]) ...
//   grid3d([xa], [ya], [za]) ...
//
// Arguments:
//   xa = array or range of X-axis values to offset by. (Default: [0])
//   ya = array or range of Y-axis values to offset by. (Default: [0])
//   za = array or range of Z-axis values to offset by. (Default: [0])
//   n = Optional number of copies to have per axis.
//   spacing = spacing of copies per axis. Use with `n`.
//
// Side Effect:
//   `$pos` is set to the relative centerpoint of each child copy, and can be used to modify each child individually.
//   `$idx` is set to the [Xidx,Yidx,Zidx] index values of each child copy, when using `count` and `n`.
//
// Examples(FlatSpin):
//   grid3d(xa=[0:25:50],ya=[0,40],za=[-20:40:20]) sphere(r=5);
//   grid3d(n=[3, 4, 2], spacing=[60, 50, 40]) sphere(r=10);
// Examples:
//   grid3d(ya=[-60:40:60],za=[0,70]) sphere(r=10);
//   grid3d(n=3, spacing=30) sphere(r=10);
//   grid3d(n=[3, 1, 2], spacing=30) sphere(r=10);
//   grid3d(n=[3, 4], spacing=[80, 60]) sphere(r=10);
// Examples:
//   grid3d(n=[10, 10, 10], spacing=50) color($idx/9) cube(50, center=true);
module grid3d(xa=[0], ya=[0], za=[0], n=undef, spacing=undef)
{
	n = scalar_vec3(n, 1);
	spacing = scalar_vec3(spacing, undef);
	if (is_def(n) && is_def(spacing)) {
		for (xi = [0:n.x-1]) {
			for (yi = [0:n.y-1]) {
				for (zi = [0:n.z-1]) {
					$idx = [xi,yi,zi];
					$pos = vmul(spacing, $idx - (n-[1,1,1])/2);
					translate($pos) children();
				}
			}
		}
	} else {
		for (xoff = xa, yoff = ya, zoff = za) {
			$pos = [xoff, yoff, zoff];
			translate($pos) children();
		}
	}
}



// Module: grid_of()
// Status: DEPRECATED, use `grid3d()` instead.
//
// Description:
//   Makes a 3D grid of duplicate children.
//
// Usage:
//   grid_of(n, spacing) ...
//   grid_of(n=[Xn,Yn,Zn], spacing=[dX,dY,dZ]) ...
//   grid_of([xa], [ya], [za]) ...
//
// Arguments:
//   xa = array or range of X-axis values to offset by. (Default: [0])
//   ya = array or range of Y-axis values to offset by. (Default: [0])
//   za = array or range of Z-axis values to offset by. (Default: [0])
//   n = Optional number of copies to have per axis.
//   spacing = spacing of copies per axis. Use with `n`.
//
// Side Effect:
//   `$pos` is set to the relative centerpoint of each child copy, and can be used to modify each child individually.
//   `$idx` is set to the [Xidx,Yidx,Zidx] index values of each child copy, when using `count` and `n`.
module grid_of(xa=[0], ya=[0], za=[0], count=undef, spacing=undef)
{
	deprecate("grid_of()", "grid3d()");
	grid3d(xa=xa, ya=ya, za=za, n=count, spacing=spacing) children();
}



//////////////////////////////////////////////////////////////////////
// Section: Rotational Distributors
//////////////////////////////////////////////////////////////////////


// Module: rot_copies()
//
// Description:
//   Given a number of XYZ rotation angles, or a list of angles and an axis `v`,
//   rotates copies of the children to each of those angles.
//
// Usage:
//   rot_copies(rots, [cp], [sa], [delta], [subrot]) ...
//   rot_copies(rots, v, [cp], [sa], [delta], [subrot]) ...
//   rot_copies(n, [v], [cp], [sa], [delta], [subrot]) ...
//
// Arguments:
//   rots = A list of [X,Y,Z] rotation angles in degrees.  If `v` is given, this will be a list of scalar angles in degrees to rotate around `v`.
//   v = If given, this is the vector to rotate around.
//   cp = Centerpoint to rotate around.
//   n = Optional number of evenly distributed copies, rotated around the ring.  If given, overrides `rots` argument.
//   sa = Starting angle, in degrees.  For use with `n`.  Angle is in degrees counter-clockwise.
//   delta = [X,Y,Z] amount to move away from cp before rotating.  Makes rings of copies.
//   subrot = If false, don't sub-rotate children as they are copied around the ring.
//
// Side Effects:
//   `$ang` is set to the rotation angle (or XYZ rotation triplet) of each child copy, and can be used to modify each child individually.
//   `$idx` is set to the index value of each child copy.
//
// Example:
//   #cylinder(h=20, r1=5, r2=0);
//   rot_copies([[45,0,0],[0,45,90],[90,-45,270]]) cylinder(h=20, r1=5, r2=0);
//
// Example:
//   rot_copies([45, 90, 135], v=V_DOWN+V_BACK)
//       yrot(90) cylinder(h=20, r1=5, r2=0);
//   color("red",0.333) yrot(90) cylinder(h=20, r1=5, r2=0);
//
// Example:
//   rot_copies(n=6, v=V_DOWN+V_BACK)
//       yrot(90) cylinder(h=20, r1=5, r2=0);
//   color("red",0.333) yrot(90) cylinder(h=20, r1=5, r2=0);
//
// Example:
//   rot_copies(n=6, v=V_DOWN+V_BACK, delta=[10,0,0])
//       yrot(90) cylinder(h=20, r1=5, r2=0);
//   color("red",0.333) yrot(90) cylinder(h=20, r1=5, r2=0);
//
// Example:
//   rot_copies(n=6, v=V_UP+V_FWD, delta=[10,0,0], sa=45)
//       yrot(90) cylinder(h=20, r1=5, r2=0);
//   color("red",0.333) yrot(90) cylinder(h=20, r1=5, r2=0);
//
// Example:
//   rot_copies(n=6, v=V_DOWN+V_BACK, delta=[20,0,0], subrot=false)
//       yrot(90) cylinder(h=20, r1=5, r2=0);
//   color("red",0.333) yrot(90) cylinder(h=20, r1=5, r2=0);
module rot_copies(rots=[], v=undef, cp=[0,0,0], count=undef, n=undef, sa=0, offset=0, delta=[0,0,0], subrot=true)
{
	cnt = first_defined([count, n]);
	sang = sa + offset;
	angs = is_def(cnt)? (cnt<=0? [] : [for (i=[0:cnt-1]) i/cnt*360+sang]) : rots;
	if (cp != [0,0,0]) {
		translate(cp) rot_copies(rots=rots, v=v, n=cnt, sa=sang, delta=delta, subrot=subrot) children();
	} else if (subrot) {
		for ($idx = [0:len(angs)-1]) {
			$ang = angs[$idx];
			rotate(a=$ang,v=v) translate(delta) rot(a=sang,v=v,reverse=true) children();
		}
	} else {
		for ($idx = [0:len(angs)-1]) {
			$ang = angs[$idx];
			rotate(a=$ang,v=v) translate(delta) rot(a=$ang,v=v,reverse=true) children();
		}
	}
}


// Module: xrot_copies()
//
// Description:
//   Given an array of angles, rotates copies of the children
//   to each of those angles around the X axis.
//
// Usage:
//   xrot_copies(rots, [r], [cp], [sa], [subrot]) ...
//   xrot_copies(n, [r], [cp], [sa], [subrot]) ...
//
// Arguments:
//   rots = Optional array of rotation angles, in degrees, to make copies at.
//   cp = Centerpoint to rotate around.
//   n = Optional number of evenly distributed copies to be rotated around the ring.  If given, overrides `rots` argument.
//   sa = Starting angle, in degrees.  For use with `n`.  Angle is in degrees counter-clockwise from Y+, when facing the origin from X+.  First unrotated copy is placed at that angle.
//   r = Radius to move children back, away from cp, before rotating.  Makes rings of copies.
//   subrot = If false, don't sub-rotate children as they are copied around the ring.
//
// Side Effects:
//   `$ang` is set to the rotation angle of each child copy, and can be used to modify each child individually.
//
// Example:
//   xrot_copies([180, 270, 315])
//       cylinder(h=20, r1=5, r2=0);
//   color("red",0.333) cylinder(h=20, r1=5, r2=0);
//
// Example:
//   xrot_copies(n=6)
//       cylinder(h=20, r1=5, r2=0);
//   color("red",0.333) cylinder(h=20, r1=5, r2=0);
//
// Example:
//   xrot_copies(n=6, r=10)
//       xrot(-90) cylinder(h=20, r1=5, r2=0);
//   color("red",0.333) xrot(-90) cylinder(h=20, r1=5, r2=0);
//
// Example:
//   xrot_copies(n=6, r=10, sa=45)
//       xrot(-90) cylinder(h=20, r1=5, r2=0);
//   color("red",0.333) xrot(-90) cylinder(h=20, r1=5, r2=0);
//
// Example:
//   xrot_copies(n=6, r=20, subrot=false)
//       xrot(-90) cylinder(h=20, r1=5, r2=0, center=true);
//   color("red",0.333) xrot(-90) cylinder(h=20, r1=5, r2=0, center=true);
module xrot_copies(rots=[], cp=[0,0,0], n=undef, count=undef, sa=0, offset=0, r=0, subrot=true)
{
	cnt = first_defined([count, n]);
	sang = sa + offset;
	rot_copies(rots=rots, v=V_RIGHT, cp=cp, n=cnt, sa=sang, delta=[0, r, 0], subrot=subrot) children();
}


// Module: yrot_copies()
//
// Description:
//   Given an array of angles, rotates copies of the children
//   to each of those angles around the Y axis.
//
// Usage:
//   yrot_copies(rots, [r], [cp], [sa], [subrot]) ...
//   yrot_copies(n, [r], [cp], [sa], [subrot]) ...
//
// Arguments:
//   rots = Optional array of rotation angles, in degrees, to make copies at.
//   cp = Centerpoint to rotate around.
//   n = Optional number of evenly distributed copies to be rotated around the ring.  If given, overrides `rots` argument.
//   sa = Starting angle, in degrees.  For use with `n`.  Angle is in degrees counter-clockwise from X-, when facing the origin from Y+.
//   r = Radius to move children left, away from cp, before rotating.  Makes rings of copies.
//   subrot = If false, don't sub-rotate children as they are copied around the ring.
//
// Side Effects:
//   `$ang` is set to the rotation angle of each child copy, and can be used to modify each child individually.
//
// Example:
//   yrot_copies([180, 270, 315])
//       cylinder(h=20, r1=5, r2=0);
//   color("red",0.333) cylinder(h=20, r1=5, r2=0);
//
// Example:
//   yrot_copies(n=6)
//       cylinder(h=20, r1=5, r2=0);
//   color("red",0.333) cylinder(h=20, r1=5, r2=0);
//
// Example:
//   yrot_copies(n=6, r=10)
//       yrot(-90) cylinder(h=20, r1=5, r2=0);
//   color("red",0.333) yrot(-90) cylinder(h=20, r1=5, r2=0);
//
// Example:
//   yrot_copies(n=6, r=10, sa=45)
//       yrot(-90) cylinder(h=20, r1=5, r2=0);
//   color("red",0.333) yrot(-90) cylinder(h=20, r1=5, r2=0);
//
// Example:
//   yrot_copies(n=6, r=20, subrot=false)
//       yrot(-90) cylinder(h=20, r1=5, r2=0, center=true);
//   color("red",0.333) yrot(-90) cylinder(h=20, r1=5, r2=0, center=true);
module yrot_copies(rots=[], cp=[0,0,0], n=undef, count=undef, sa=0, offset=0, r=0, subrot=true)
{
	cnt = first_defined([count, n]);
	sang = sa + offset;
	rot_copies(rots=rots, v=V_BACK, cp=cp, n=cnt, sa=sang, delta=[-r, 0, 0], subrot=subrot) children();
}


// Module: zrot_copies()
//
// Description:
//   Given an array of angles, rotates copies of the children
//   to each of those angles around the Z axis.
//
// Usage:
//   zrot_copies(rots, [r], [cp], [sa], [subrot]) ...
//   zrot_copies(n, [r], [cp], [sa], [subrot]) ...
//
// Arguments:
//   rots = Optional array of rotation angles, in degrees, to make copies at.
//   cp = Centerpoint to rotate around.
//   n = Optional number of evenly distributed copies to be rotated around the ring.  If given, overrides `rots` argument.
//   sa = Starting angle, in degrees.  For use with `n`.  Angle is in degrees counter-clockwise from X+, when facing the origin from Z+.
//   r = Radius to move children right, away from cp, before rotating.  Makes rings of copies.
//   subrot = If false, don't sub-rotate children as they are copied around the ring.
//
// Side Effects:
//   `$ang` is set to the rotation angle of each child copy, and can be used to modify each child individually.
//
// Example:
//   zrot_copies([180, 270, 315])
//       yrot(90) cylinder(h=20, r1=5, r2=0);
//   color("red",0.333) yrot(90) cylinder(h=20, r1=5, r2=0);
//
// Example:
//   zrot_copies(n=6)
//       yrot(90) cylinder(h=20, r1=5, r2=0);
//   color("red",0.333) yrot(90) cylinder(h=20, r1=5, r2=0);
//
// Example:
//   zrot_copies(n=6, r=10)
//       yrot(90) cylinder(h=20, r1=5, r2=0);
//   color("red",0.333) yrot(90) cylinder(h=20, r1=5, r2=0);
//
// Example:
//   zrot_copies(n=6, r=20, sa=45)
//       yrot(90) cylinder(h=20, r1=5, r2=0, center=true);
//   color("red",0.333) yrot(90) cylinder(h=20, r1=5, r2=0, center=true);
//
// Example:
//   zrot_copies(n=6, r=20, subrot=false)
//       yrot(-90) cylinder(h=20, r1=5, r2=0, center=true);
//   color("red",0.333) yrot(-90) cylinder(h=20, r1=5, r2=0, center=true);
module zrot_copies(rots=[], cp=[0,0,0], n=undef, count=undef, sa=0, offset=0, r=0, subrot=true)
{
	cnt = first_defined([count, n]);
	sang = sa + offset;
	rot_copies(rots=rots, v=V_UP, cp=cp, n=cnt, sa=sang, delta=[r, 0, 0], subrot=subrot) children();
}


// Module: xring()
// 
// Description:
//   Distributes `n` copies of the given children on a circle of radius `r`
//   around the X axis.  If `rot` is true, each copy is rotated in place to keep
//   the same side towards the center.  The first, unrotated copy will be at the
//   starting angle `sa`.
//
// Usage:
//   xring(n, r, [sa], [cp], [rot]) ...
//
// Arguments:
//   n = Number of copies of children to distribute around the circle. (Default: 2)
//   r = Radius of ring to distribute children around. (Default: 0)
//   sa = Start angle for first (unrotated) copy.  (Default: 0)
//   cp = Centerpoint of ring.  Default: [0,0,0]
//   rot = If true, rotate each copy to keep the same side towards the center of the ring.  Default: true.
//
// Side Effects:
//   `$ang` is set to the rotation angle of each child copy, and can be used to modify each child individually.
//   `$idx` is set to the index value of each child copy.
//
// Examples:
//   xring(n=6, r=10) xrot(-90) cylinder(h=20, r1=5, r2=0);
//   xring(n=6, r=10, sa=45) xrot(-90) cylinder(h=20, r1=5, r2=0);
//   xring(n=6, r=20, rot=false) cylinder(h=20, r1=6, r2=0, center=true);
module xring(n=2, r=0, sa=0, cp=[0,0,0], rot=true)
{
	xrot_copies(count=n, r=r, sa=sa, cp=cp, subrot=rot) children();
}


// Module: yring()
// 
// Description:
//   Distributes `n` copies of the given children on a circle of radius `r`
//   around the Y axis.  If `rot` is true, each copy is rotated in place to keep
//   the same side towards the center.  The first, unrotated copy will be at the
//   starting angle `sa`.
//
// Usage:
//   yring(n, r, [sa], [cp], [rot]) ...
//
// Arguments:
//   n = Number of copies of children to distribute around the circle. (Default: 2)
//   r = Radius of ring to distribute children around. (Default: 0)
//   sa = Start angle for first (unrotated) copy.  (Default: 0)
//   cp = Centerpoint of ring.  Default: [0,0,0]
//   rot = If true, rotate each copy to keep the same side towards the center of the ring.  Default: true.
//
// Side Effects:
//   `$ang` is set to the rotation angle of each child copy, and can be used to modify each child individually.
//   `$idx` is set to the index value of each child copy.
//
// Examples:
//   yring(n=6, r=10) yrot(-90) cylinder(h=20, r1=5, r2=0);
//   yring(n=6, r=10, sa=45) yrot(-90) cylinder(h=20, r1=5, r2=0);
//   yring(n=6, r=20, rot=false) cylinder(h=20, r1=6, r2=0, center=true);
module yring(n=2, r=0, sa=0, cp=[0,0,0], rot=true)
{
	yrot_copies(count=n, r=r, sa=sa, cp=cp, subrot=rot) children();
}


// Module: zring()
//
// Description:
//   Distributes `n` copies of the given children on a circle of radius `r`
//   around the Z axis.  If `rot` is true, each copy is rotated in place to keep
//   the same side towards the center.  The first, unrotated copy will be at the
//   starting angle `sa`.
//
// Usage:
//   zring(r, n, [sa], [cp], [rot]) ...
//
// Arguments:
//   n = Number of copies of children to distribute around the circle. (Default: 2)
//   r = Radius of ring to distribute children around. (Default: 0)
//   sa = Start angle for first (unrotated) copy.  (Default: 0)
//   cp = Centerpoint of ring.  Default: [0,0,0]
//   rot = If true, rotate each copy to keep the same side towards the center of the ring.  Default: true.
//
// Side Effects:
//   `$ang` is set to the relative angle from `cp` of each child copy, and can be used to modify each child individually.
//   `$idx` is set to the index value of each child copy.
//
// Examples:
//   zring(n=6, r=10) yrot(90) cylinder(h=20, r1=5, r2=0);
//   zring(n=6, r=10, sa=45) yrot(90) cylinder(h=20, r1=5, r2=0);
//   zring(n=6, r=20, rot=false) yrot(90) cylinder(h=20, r1=6, r2=0, center=true);
module zring(n=2, r=0, sa=0, cp=[0,0,0], rot=true)
{
	zrot_copies(count=n, r=r, sa=sa, cp=cp, subrot=rot) children();
}


// Module: arc_of()
//
// Description:
//   Evenly distributes n duplicate children around an ovoid arc on the XY plane.
//
// Usage:
//   arc_of(r|d, n, [sa], [ea], [rot]
//   arc_of(rx|dx, ry|dy, n, [sa], [ea], [rot]
//
// Arguments:
//   n = number of copies to distribute around the circle. (Default: 6)
//   r = radius of circle (Default: 1)
//   rx = radius of ellipse on X axis. Used instead of r.
//   ry = radius of ellipse on Y axis. Used instead of r.
//   d = diameter of circle. (Default: 2)
//   dx = diameter of ellipse on X axis. Used instead of d.
//   dy = diameter of ellipse on Y axis. Used instead of d.
//   rot = whether to rotate the copied children.  (Default: false)
//   sa = starting angle. (Default: 0.0)
//   ea = ending angle. Will distribute copies CCW from sa to ea. (Default: 360.0)
//
// Side Effects:
//   `$ang` is set to the rotation angle of each child copy, and can be used to modify each child individually.
//   `$pos` is set to the relative centerpoint of each child copy, and can be used to modify each child individually.
//   `$idx` is set to the index value of each child copy.
//
// Example:
//   #cube(size=[10,3,3],center=true);
//   arc_of(d=40, n=5) cube(size=[10,3,3],center=true);
//
// Example:
//   #cube(size=[10,3,3],center=true);
//   arc_of(d=40, n=5, sa=45, ea=225) cube(size=[10,3,3],center=true);
//
// Example:
//   #cube(size=[10,3,3],center=true);
//   arc_of(r=15, n=8, rot=false) cube(size=[10,3,3],center=true);
//
// Example:
//   #cube(size=[10,3,3],center=true);
//   arc_of(rx=20, ry=10, n=8) cube(size=[10,3,3],center=true);
module arc_of(
	n=6,
	r=undef, rx=undef, ry=undef,
	d=undef, dx=undef, dy=undef,
	sa=0, ea=360,
	rot=true
) {
	rx = get_radius(rx, r, dx, d, 1);
	ry = get_radius(ry, r, dy, d, 1);
	sa = posmod(sa, 360);
	ea = posmod(ea, 360);
	n = (abs(ea-sa)<0.01)?(n+1):n;
	delt = (((ea<=sa)?360.0:0)+ea-sa)/(n-1);
	for ($idx = [0:n-1]) {
		$ang = sa + ($idx * delt);
		$pos =[rx*cos($ang), ry*sin($ang), 0];
		translate($pos) {
			zrot(rot? atan2(ry*sin($ang), rx*cos($ang)) : 0) {
				children();
			}
		}
	}
}



// Module: ovoid_spread()
//
// Description:
//   Spreads children semi-evenly over the surface of a sphere.
//
// Usage:
//   ovoid_spread(r|d, n, [cone_ang], [scale], [perp]) ...
//
// Arguments:
//   r = Radius of the sphere to distribute over
//   d = Diameter of the sphere to distribute over
//   n = How many copies to evenly spread over the surface.
//   cone_ang = Angle of the cone, in degrees, to limit how much of the sphere gets covered.  For full sphere coverage, use 180.  Measured pre-scaling.  Default: 180
//   scale = The [X,Y,Z] scaling factors to reshape the sphere being covered.
//   perp = If true, rotate children to be perpendicular to the sphere surface.  Default: true
//
// Side Effects:
//   `$pos` is set to the relative post-scaled centerpoint of each child copy, and can be used to modify each child individually.
//   `$theta` is set to the theta angle of the child from the center of the sphere.
//   `$phi` is set to the pre-scaled phi angle of the child from the center of the sphere.
//   `$rad` is set to the pre-scaled radial distance of the child from the center of the sphere.
//   `$idx` is set to the index number of each child being copied.
//
// Example:
//   ovoid_spread(n=250, d=100, cone_ang=45, scale=[3,3,1])
//       cylinder(d=10, h=10, center=false);
//
// Example:
//   ovoid_spread(n=500, d=100, cone_ang=180)
//       color(normalize(point3d(vabs($pos))))
//           cylinder(d=8, h=10, center=false);
module ovoid_spread(r=undef, d=undef, n=100, cone_ang=90, scale=[1,1,1], perp=true)
{
	r = get_radius(r=r, d=d, dflt=50);
	cnt = ceil(n / (cone_ang/180));

	// Calculate an array of [theta,phi] angles for `n` number of
	// points, almost evenly spaced across the surface of a sphere.
	// This approximation is based on the golden spiral method.
	theta_phis = [for (x=[0:n-1]) [180*(1+sqrt(5))*(x+0.5)%360, acos(1-2*(x+0.5)/cnt)]];

	for ($idx = [0:len(theta_phis)-1]) {
	    tp = theta_phis[$idx];
		xyz = spherical_to_xyz(r, tp[0], tp[1]);
		$pos = vmul(xyz,scale);
		$theta = tp[0];
		$phi = tp[1];
		$rad = r;
		translate($pos) {
			if (perp) {
				rot(from=V_UP, to=xyz) children();
			} else {
				children();
			}
		}
	}
}



//////////////////////////////////////////////////////////////////////
// Section: Reflectional Distributors
//////////////////////////////////////////////////////////////////////


// Module: mirror_copy()
//
// Description:
//   Makes a copy of the children, mirrored across the given plane.
//
// Usage:
//   mirror_copy(v, [cp], [offset]) ...
//
// Arguments:
//   v = The normal vector of the plane to mirror across.
//   offset = distance to offset away from the plane.
//   cp = A point that lies on the mirroring plane.
//
// Side Effects:
//   `$orig` is true for the original instance of children.  False for the copy.
//   `$idx` is set to the index value of each copy.
//
// Example:
//   mirror_copy([1,-1,0]) zrot(-45) yrot(90) cylinder(d1=10, d2=0, h=20);
//   color("blue",0.25) zrot(-45) cube([0.01,15,15], center=true);
//
// Example:
//   mirror_copy([1,1,0], offset=5) rot(a=90,v=[-1,1,0]) cylinder(d1=10, d2=0, h=20);
//   color("blue",0.25) zrot(45) cube([0.01,15,15], center=true);
//
// Example:
//   mirror_copy(V_UP+V_BACK, cp=[0,-5,-5]) rot(from=V_UP, to=V_BACK+V_UP) cylinder(d1=10, d2=0, h=20);
//   color("blue",0.25) translate([0,-5,-5]) rot(from=V_UP, to=V_BACK+V_UP) cube([15,15,0.01], center=true);
module mirror_copy(v=[0,0,1], offset=0, cp=[0,0,0])
{
	nv = v/norm(v);
	off = nv*offset;
	if (cp == [0,0,0]) {
		translate(off) {
			$orig = true;
			$idx = 0;
			children();
		}
		mirror(nv) translate(off) {
			$orig = false;
			$idx = 1;
			children();
		}
	} else {
		translate(off) children();
		translate(cp) mirror(nv) translate(-cp) translate(off) children();
	}
}


// Module: xflip_copy()
//
// Description:
//   Makes a copy of the children, mirrored across the X axis.
//
// Usage:
//   xflip_copy([cp], [offset]) ...
//
// Arguments:
//   offset = Distance to offset children right, before copying.
//   cp = A point that lies on the mirroring plane.
//
// Side Effects:
//   `$orig` is true for the original instance of children.  False for the copy.
//   `$idx` is set to the index value of each copy.
//
// Example:
//   xflip_copy() yrot(90) cylinder(h=20, r1=4, r2=0);
//   color("blue",0.25) cube([0.01,15,15], center=true);
//
// Example:
//   xflip_copy(offset=5) yrot(90) cylinder(h=20, r1=4, r2=0);
//   color("blue",0.25) cube([0.01,15,15], center=true);
//
// Example:
//   xflip_copy(cp=[-5,0,0]) yrot(90) cylinder(h=20, r1=4, r2=0);
//   color("blue",0.25) left(5) cube([0.01,15,15], center=true);
module xflip_copy(offset=0, cp=[0,0,0])
{
	mirror_copy(v=[1,0,0], offset=offset, cp=cp) children();
}


// Module: yflip_copy()
//
// Description:
//   Makes a copy of the children, mirrored across the Y axis.
//
// Usage:
//   yflip_copy([cp], [offset]) ...
//
// Arguments:
//   offset = Distance to offset children back, before copying.
//   cp = A point that lies on the mirroring plane.
//
// Side Effects:
//   `$orig` is true for the original instance of children.  False for the copy.
//   `$idx` is set to the index value of each copy.
//
// Example:
//   yflip_copy() xrot(-90) cylinder(h=20, r1=4, r2=0);
//   color("blue",0.25) cube([15,0.01,15], center=true);
//
// Example:
//   yflip_copy(offset=5) xrot(-90) cylinder(h=20, r1=4, r2=0);
//   color("blue",0.25) cube([15,0.01,15], center=true);
//
// Example:
//   yflip_copy(cp=[0,-5,0]) xrot(-90) cylinder(h=20, r1=4, r2=0);
//   color("blue",0.25) fwd(5) cube([15,0.01,15], center=true);
module yflip_copy(offset=0, cp=[0,0,0])
{
	mirror_copy(v=[0,1,0], offset=offset, cp=cp) children();
}


// Module: zflip_copy()
//
// Description:
//   Makes a copy of the children, mirrored across the Z axis.
//
// Usage:
//   zflip_copy([cp], [offset]) ...
//   `$idx` is set to the index value of each copy.
//
// Arguments:
//   offset = Distance to offset children up, before copying.
//   cp = A point that lies on the mirroring plane.
//
// Side Effects:
//   `$orig` is true for the original instance of children.  False for the copy.
//
// Example:
//   zflip_copy() cylinder(h=20, r1=4, r2=0);
//   color("blue",0.25) cube([15,15,0.01], center=true);
//
// Example:
//   zflip_copy(offset=5) cylinder(h=20, r1=4, r2=0);
//   color("blue",0.25) cube([15,15,0.01], center=true);
//
// Example:
//   zflip_copy(cp=[0,0,-5]) cylinder(h=20, r1=4, r2=0);
//   color("blue",0.25) down(5) cube([15,15,0.01], center=true);
module zflip_copy(offset=0, cp=[0,0,0])
{
	mirror_copy(v=[0,0,1], offset=offset, cp=cp) children();
}


//////////////////////////////////////////////////////////////////////
// Section: Mutators
//////////////////////////////////////////////////////////////////////


// Module: half_of()
//
// Usage:
//   half_of(v, [cp], [s]) ...
//
// Description:
//   Slices an object at a cut plane, and masks away everything that is on one side.
//
// Arguments:
//   v = Normal of plane to slice at.  Keeps everything on the side the normal points to.  Default: [0,0,1] (V_UP)
//   cp = If given as a scalar, moves the cut plane along the normal by the given amount.  If given as a point, specifies a point on the cut plane.  This can be used to shift where it slices the object at.  Default: [0,0,0]
//   s = Mask size to use.  Use a number larger than twice your object's largest axis.  If you make this too large, it messes with centering your view.  Default: 100
//   planar = If true, this becomes a 2D operation.  When planar, a `v` of `V_UP` or `V_DOWN` becomes equivalent of `V_BACK` and `V_FWD` respectively.
//
// Examples:
//   half_of(V_DOWN+V_BACK, cp=[0,-10,0]) cylinder(h=40, r1=10, r2=0, center=false);
//   half_of(V_DOWN+V_LEFT, s=200) sphere(d=150);
// Example(2D):
//   half_of([1,1], planar=true) circle(d=50);
module half_of(v=V_UP, cp=[0,0,0], s=100, planar=false)
{
	cp = is_scalar(cp)? cp*normalize(v) : cp;
	if (cp != [0,0,0]) {
		translate(cp) half_of(v=v, s=s, planar=planar) translate(-cp) children();
	} else if (planar) {
		v = (v==V_UP)? V_BACK : (v==V_DOWN)? V_FWD : v;
		ang = atan2(v.y, v.x);
		difference() {
			children();
			rotate(ang+90) {
				back(s/2) square(s, center=true);
			}
		}
	} else {
		difference() {
			children();
			rot(from=V_UP, to=-v) {
				up(s/2) cube(s, center=true);
			}
		}
	}
}


// Module: top_half()
//
// Usage:
//   top_half([z|cp], [s]) ...
//
// Description:
//   Slices an object at a horizontal X-Y cut plane, and masks away everything that is below it.
//
// Arguments:
//   cp = If given as a scalar, moves the cut plane up by the given amount.  If given as a point, specifies a point on the cut plane.  Default: [0,0,0]
//   z = The Z coordinate of the cut-plane, if given.  Use instead of `cp`.
//   s = Mask size to use.  Use a number larger than twice your object's largest axis.  If you make this too large, it messes with centering your view.  Default: 100
//   planar = If true, this becomes equivalent to a planar `back_half()`.
//
// Examples(Spin):
//   top_half() sphere(r=20);
//   top_half(z=5) sphere(r=20);
//   top_half(cp=5) sphere(r=20);
//   top_half(cp=[0,0,-8]) sphere(r=20);
// Example(2D):
//   top_half(planar=true) circle(r=20);
module top_half(s=100, z=undef, cp=[0,0,0], planar=false)
{
	dir = planar? V_BACK : V_UP;
	cp = is_scalar(z)? [0,0,z] : is_scalar(cp)? cp*dir : cp;
	translate(cp) difference() {
		translate(-cp) children();
		translate(-dir*s/2) {
			if (planar) {
				square(s, center=true);
			} else {
				cube(s, center=true);
			}
		}
	}
}



// Module: bottom_half()
//
// Usage:
//   bottom_half([z|cp], [s]) ...
//
// Description:
//   Slices an object at a horizontal X-Y cut plane, and masks away everything that is above it.
//
// Arguments:
//   cp = If given as a scalar, moves the cut plane down by the given amount.  If given as a point, specifies a point on the cut plane.  Default: [0,0,0]
//   z = The Z coordinate of the cut-plane, if given.  Use instead of `cp`.
//   s = Mask size to use.  Use a number larger than twice your object's largest axis.  If you make this too large, it messes with centering your view.  Default: 100
//   planar = If true, this becomes equivalent to a planar `front_half()`.
//
// Examples:
//   bottom_half() sphere(r=20);
//   bottom_half(z=-10) sphere(r=20);
//   bottom_half(cp=-10) sphere(r=20);
//   bottom_half(cp=[0,0,10]) sphere(r=20);
// Example(2D):
//   bottom_half(planar=true) circle(r=20);
module bottom_half(s=100, z=undef, cp=[0,0,0], planar=false)
{
	dir = planar? V_FWD : V_DOWN;
	cp = is_scalar(z)? [0,0,z] : is_scalar(cp)? cp*dir : cp;
	translate(cp) difference() {
		translate(-cp) children();
		translate(-dir*s/2) {
			if (planar) {
				square(s, center=true);
			} else {
				cube(s, center=true);
			}
		}
	}
}



// Module: left_half()
//
// Usage:
//   left_half([x|cp], [s]) ...
//
// Description:
//   Slices an object at a vertical Y-Z cut plane, and masks away everything that is right of it.
//
// Arguments:
//   cp = If given as a scalar, moves the cut plane left by the given amount.  If given as a point, specifies a point on the cut plane.  Default: [0,0,0]
//   x = The X coordinate of the cut-plane, if given.  Use instead of `cp`.
//   s = Mask size to use.  Use a number larger than twice your object's largest axis.  If you make this too large, it messes with centering your view.  Default: 100
//   planar = If true, this becomes a 2D operation.
//
// Examples:
//   left_half() sphere(r=20);
//   left_half(x=-8) sphere(r=20);
//   left_half(cp=-8) sphere(r=20);
//   left_half(cp=[8,0,0]) sphere(r=20);
// Example(2D):
//   left_half(planar=true) circle(r=20);
module left_half(s=100, x=undef, cp=[0,0,0], planar=false)
{
	dir = V_LEFT;
	cp = is_scalar(x)? [x,0,0] : is_scalar(cp)? cp*dir : cp;
	translate(cp) difference() {
		translate(-cp) children();
		translate(-dir*s/2) {
			if (planar) {
				square(s, center=true);
			} else {
				cube(s, center=true);
			}
		}
	}
}



// Module: right_half()
//
// Usage:
//   right_half([x|cp], [s]) ...
//
// Description:
//   Slices an object at a vertical Y-Z cut plane, and masks away everything that is left of it.
//
// Arguments:
//   cp = If given as a scalar, moves the cut plane right by the given amount.  If given as a point, specifies a point on the cut plane.  Default: [0,0,0]
//   x = The X coordinate of the cut-plane, if given.  Use instead of `cp`.
//   s = Mask size to use.  Use a number larger than twice your object's largest axis.  If you make this too large, it messes with centering your view.  Default: 100
//   planar = If true, this becomes a 2D operation.
//
// Examples(FlatSpin):
//   right_half() sphere(r=20);
//   right_half(x=-5) sphere(r=20);
//   right_half(cp=-5) sphere(r=20);
//   right_half(cp=[-5,0,0]) sphere(r=20);
// Example(2D):
//   right_half(planar=true) circle(r=20);
module right_half(s=100, x=undef, cp=[0,0,0], planar=false)
{
	dir = V_RIGHT;
	cp = is_scalar(x)? [x,0,0] : is_scalar(cp)? cp*dir : cp;
	translate(cp) difference() {
		translate(-cp) children();
		translate(-dir*s/2) {
			if (planar) {
				square(s, center=true);
			} else {
				cube(s, center=true);
			}
		}
	}
}



// Module: front_half()
//
// Usage:
//   front_half([y|cp], [s]) ...
//
// Description:
//   Slices an object at a vertical X-Z cut plane, and masks away everything that is behind it.
//
// Arguments:
//   cp = If given as a scalar, moves the cut plane forward by the given amount.  If given as a point, specifies a point on the cut plane.  Default: [0,0,0]
//   y = The Y coordinate of the cut-plane, if given.  Use instead of `cp`.
//   s = Mask size to use.  Use a number larger than twice your object's largest axis.  If you make this too large, it messes with centering your view.  Default: 100
//   planar = If true, this becomes a 2D operation.
//
// Examples(FlatSpin):
//   front_half() sphere(r=20);
//   front_half(y=5) sphere(r=20);
//   front_half(cp=5) sphere(r=20);
//   front_half(cp=[0,5,0]) sphere(r=20);
// Example(2D):
//   front_half(planar=true) circle(r=20);
module front_half(s=100, y=undef, cp=[0,0,0], planar=false)
{
	dir = V_FWD;
	cp = is_scalar(y)? [0,y,0] : is_scalar(cp)? cp*dir : cp;
	translate(cp) difference() {
		translate(-cp) children();
		translate(-dir*s/2) {
			if (planar) {
				square(s, center=true);
			} else {
				cube(s, center=true);
			}
		}
	}
}



// Module: back_half()
//
// Usage:
//   back_half([y|cp], [s]) ...
//
// Description:
//   Slices an object at a vertical X-Z cut plane, and masks away everything that is in front of it.
//
// Arguments:
//   cp = If given as a scalar, moves the cut plane back by the given amount.  If given as a point, specifies a point on the cut plane.  Default: [0,0,0]
//   y = The Y coordinate of the cut-plane, if given.  Use instead of `cp`.
//   s = Mask size to use.  Use a number larger than twice your object's largest axis.  If you make this too large, it messes with centering your view.  Default: 100
//   planar = If true, this becomes a 2D operation.
//
// Examples:
//   back_half() sphere(r=20);
//   back_half(y=8) sphere(r=20);
//   back_half(cp=8) sphere(r=20);
//   back_half(cp=[0,-10,0]) sphere(r=20);
// Example(2D):
//   back_half(planar=true) circle(r=20);
module back_half(s=100, y=undef, cp=[0,0,0], planar=false)
{
	dir = V_BACK;
	cp = is_scalar(y)? [0,y,0] : is_scalar(cp)? cp*dir : cp;
	translate(cp) difference() {
		translate(-cp) children();
		translate(-dir*s/2) {
			if (planar) {
				square(s, center=true);
			} else {
				cube(s, center=true);
			}
		}
	}
}



// Module: chain_hull()
//
// Usage:
//   chain_hull() ...
//
// Description:
//   Performs hull operations between consecutive pairs of children,
//   then unions all of the hull results.  This can be a very slow
//   operation, but it can provide results that are hard to get
//   otherwise.
//
// Example:
//   chain_hull() {
//       cube(5, center=true);
//       translate([30, 0, 0]) sphere(d=15);
//       translate([60, 30, 0]) cylinder(d=10, h=20);
//       translate([60, 60, 0]) cube([10,1,20], center=false);
//   }
module chain_hull()
{
	union() {
		if ($children == 1) {
			children();
		} else if ($children > 1) {
			for (i =[1:$children-1]) {
				hull() {
					children(i-1);
					children(i);
				}
			}
		}
	}
}


// Module: extrude_arc()
//
// Description:
//   Extrudes 2D shapes around a partial circle arc, with optional rounded caps.
//   This is mostly useful for backwards compatability with older OpenSCAD versions
//   without the `angle` argument in rotate_extrude.
//
// Usage:
//   extrude_arc(arc, r|d, [sa], [caps], [orient], [align], [masksize]) ...
//
// Arguments:
//   arc = Number of degrees to traverse.
//   sa = Start angle in degrees.
//   r = Radius of arc.
//   d = Diameter of arc.
//   orient = The axis to align to.  Use `ORIENT_` constants from `constants.scad`
//   align = The side of the origin the part should be aligned with.  Use `V_` constants from `constants.scad`
//   masksize = size of mask used to clear unused part of circle arc.  should be larger than height or width of 2D shapes to extrude.
//   caps = If true, spin the 2D shapes to make rounded caps the ends of the arc.
//   convexity = Max number of times a ray passes through the 2D shape's walls.
//
// Example(Med):
//   pts=[[-5/2, -5], [-5/2, 0], [-5/2-3, 5], [5/2+3, 5], [5/2, 0], [5/2, -5]];
//   #polygon(points=pts);
//   extrude_arc(arc=270, sa=45, r=40, caps=true, convexity=4, $fa=2, $fs=2) {
//       polygon(points=pts);
//   }
module extrude_arc(arc=90, sa=0, r=undef, d=undef, orient=ORIENT_Z, align=V_CENTER, masksize=100, caps=false, convexity=4)
{
	eps = 0.001;
	r = get_radius(r=r, d=d, dflt=100);
	orient_and_align([2*r, 2*r, 0], orient, align) {
		zrot(sa) {
			if (caps) {
				place_copies([[r,0,0], cylindrical_to_xyz(r, arc, 0)]) {
					rotate_extrude(convexity=convexity) {
						difference() {
							children();
							left(masksize/2) square(masksize, center=true);
						}
					}
				}
			}
			difference() {
				rotate_extrude(angle=arc, convexity=convexity*2) {
					right(r) {
						children();
					}
				}
				if(version_num() < 20190000) {
					maxd = r + masksize;
					if (arc<180) rotate(arc) back(maxd/2) cube([2*maxd, maxd, masksize+0.1], center=true);
					difference() {
						fwd(maxd/2) cube([2*maxd, maxd, masksize+0.2], center=true);
						if (arc>180) rotate(arc-180) back(maxd/2) cube([2*maxd, maxd, masksize+0.1], center=true);
					}
				}
			}
		}
	}
}


//////////////////////////////////////////////////////////////////////
// Section: 2D Mutators
//////////////////////////////////////////////////////////////////////


// Module: round2d()
// Usage:
//   round2d(r) ...
//   round2d(or) ...
//   round2d(ir) ...
//   round2d(or, ir) ...
// Description:
//   Rounds an arbitrary 2d objects.  Giving `r` rounds all concave and
//   convex corners.  Giving just `ir` rounds just concave corners.
//   Giving just `or` rounds convex corners.  Giving both `ir` and `or`
//   can let you round to different radii for concave and convex corners.
//   The 2d object must not have any parts narrower than twice the `or`
//   radius.  Such parts will disappear.
// Arguments:
//   r = Radius to round all concave and convex corners to.
//   or = Radius to round only outside (convex) corners to.  Use instead of `r`.
//   ir = Radius to round/fillet only inside (concave) corners to.  Use instead of `r`.
// Examples(2D):
//   round2d(r=10) {square([40,100], center=true); square([100,40], center=true);}
//   round2d(or=10) {square([40,100], center=true); square([100,40], center=true);}
//   round2d(ir=10) {square([40,100], center=true); square([100,40], center=true);}
//   round2d(or=16,ir=8) {square([40,100], center=true); square([100,40], center=true);}
module round2d(r, or, ir)
{
	or = get_radius(r1=or, r=r, dflt=0);
	ir = get_radius(r1=ir, r=r, dflt=0);
	offset(or) offset(-ir-or) offset(delta=ir) children();
}


// Module: shell2d()
// Usage:
//   shell2d(thickness, [or], [ir], [fill], [round])
// Description:
//   Creates a hollow shell from 2d children, with optional rounding.
// Arguments:
//   thickness = Thickness of the shell.  Positive to expand outward, negative to shrink inward, or a two-element list to do both.
//   or = Radius to round convex corners/pointy bits on the outside of the shell.
//   ir = Radius to round/fillet concave corners on the outside of the shell.
//   round = Radius to round convex corners/pointy bits on the inside of the shell.
//   fill = Radius to round/fillet concave corners on the inside of the shell.
// Examples(2D):
//   shell2d(10) {square([40,100], center=true); square([100,40], center=true);}
//   shell2d(-10) {square([40,100], center=true); square([100,40], center=true);}
//   shell2d([-10,10]) {square([40,100], center=true); square([100,40], center=true);}
//   shell2d(10,or=10) {square([40,100], center=true); square([100,40], center=true);}
//   shell2d(10,ir=10) {square([40,100], center=true); square([100,40], center=true);}
//   shell2d(10,round=10) {square([40,100], center=true); square([100,40], center=true);}
//   shell2d(10,fill=10) {square([40,100], center=true); square([100,40], center=true);}
//   shell2d(8,or=16,ir=8,round=16,fill=8) {square([40,100], center=true); square([100,40], center=true);}
module shell2d(thickness, or=0, ir=0, fill=0, round=0)
{
	thickness = is_scalar(thickness)? (
		thickness<0? [thickness,0] : [0,thickness]
	) : (thickness[0]>thickness[1])? (
		[thickness[1],thickness[0]]
	) : thickness;
	difference() {
		round2d(or=or,ir=ir)
			offset(delta=thickness[1])
				children();
		round2d(or=fill,ir=round)
			offset(delta=thickness[0])
				children();
	}
}



//////////////////////////////////////////////////////////////////////
// Section: Miscellaneous
//////////////////////////////////////////////////////////////////////


// Module: orient_and_align()
//
// Description:
//   Takes a vertically oriented shape, and re-orients and aligns it.
//   This is useful for making a custom shape available in various
//   orientations and alignments without extra translate()s and rotate()s.
//   Children should be vertically (Z-axis) oriented, and centered.
//   Non-extremity alignment points should be named via the `alignments` arg.
//   Named alignments, as well as `ALIGN_NEG`/`ALIGN_POS` are aligned pre-rotation.
//
// Usage:
//   orient_and_align(size, [orient], [align], [center], [noncentered], [orig_orient], [orig_align], [alignments]) ...
//
// Arguments:
//   size = The size of the part.
//   orient = The axis to align to.  Use ORIENT_ constants from constants.scad
//   align = The side of the origin the part should be aligned with.
//   center = If given, overrides `align`.  If true, centers vertically.  If false, `align` will be set to the value in `noncentered`.
//   noncentered = The value to set `align` to if `center` == `false`.  Default: `V_UP`.
//   orig_orient = The original orientation of the part.  Default: `ORIENT_Z`.
//   orig_align = The original alignment of the part.  Default: `V_CENTER`.
//   alignments = A list of `["name", [X,Y,Z]]` alignment-label/offset pairs.
//
// Example:
//   #cylinder(d=5, h=10);
//   orient_and_align([5,5,10], orient=ORIENT_Y, align=V_BACK, orig_align=V_UP) cylinder(d=5, h=10);
module orient_and_align(
	size=undef, orient=ORIENT_Z, align=V_CENTER,
	center=undef, noncentered=ALIGN_POS,
	orig_orient=ORIENT_Z, orig_align=V_CENTER,
	alignments=[]
) {
	algn = is_def(center)? (center? V_CENTER : noncentered) : align;
    if (orig_align != V_CENTER) {
		orient_and_align(size=size, orient=orient, align=algn) {
			translate(vmul(size/2, -orig_align)) children();
		}
    } else if (orig_orient != ORIENT_Z) {
		rotsize = (
			(orig_orient==ORIENT_X)? [size[1], size[2], size[0]] :
			(orig_orient==ORIENT_Y)? [size[0], size[2], size[1]] :
			vabs(rotate_points3d([size], orig_orient, reverse=true)[0])
		);
		orient_and_align(size=rotsize, orient=orient, align=algn) {
			rot(orig_orient,reverse=true) children();
		}
	} else if (is_scalar(algn)) {
		// If align is a number and not a vector, then translate PRE-rotation.
		orient_and_align(size=size, orient=orient) {
			translate(vmul(size/2, algn*V_UP)) children();
		}
	} else if (is_str(algn)) {
		// If align is a string, look for an alignments label that matches.
		found = search([algn], alignments, num_returns_per_match=1);
		if (found != [[]]) {
			orient_and_align(size=size, orient=orient) {
				idx = found[0];
				delta = alignments[idx][1];
				translate(-delta) children();
			}
		} else {
			assertion(1==0, str("Alignment label '", algn, "' is not known.", (alignments? str("  Try one of ", [for (v=alignments) v[0]], ".") : "")));
		}
	} else if (orient != ORIENT_Z) {
		rotsize = (
			(orient==ORIENT_X)? [size[2], size[0], size[1]] :
			(orient==ORIENT_Y)? [size[0], size[2], size[1]] :
			vabs(rotate_points3d([size], orient)[0])
		);
		orient_and_align(size=rotsize, align=algn) {
			rotate(orient) children();
		}
	} else if (is_def(algn) && algn != [0,0,0]) {
		translate(vmul(size/2, algn)) children();
	} else {
		children();
	}
}



// vim: noexpandtab tabstop=4 shiftwidth=4 softtabstop=4 nowrap
