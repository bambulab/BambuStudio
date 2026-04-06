//////////////////////////////////////////////////////////////////////
// LibFile: threading.scad
//   Triangular and Trapezoidal-Threaded Screw Rods and Nuts.
//   To use, add the following lines to the beginning of your file:
//   ```
//   include <BOSL/constants.scad>
//   use <BOSL/threading.scad>
//   ```
//////////////////////////////////////////////////////////////////////

/*
BSD 2-Clause License

Copyright (c) 2017-2019, Revar Desmera
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
use <masks.scad>
use <math.scad>


// Section: Generic Trapezoidal Threading

// Module: trapezoidal_threaded_rod()
// Description:
//   Constructs a generic trapezoidal threaded screw rod.  This method makes
//   much smoother threads than the naive linear_extrude method.
//   For metric trapezoidal threads, use thread_angle=15 and thread_depth=pitch/2.
//   For ACME threads, use thread_angle=14.5 and thread_depth=pitch/2.
//   For square threads, use thread_angle=0 and thread_depth=pitch/2.
//   For normal UTS or ISO screw threads, use the `threaded_rod()` module instead to get the correct thread profile.
// Arguments:
//   d = Outer diameter of threaded rod.
//   l = Length of threaded rod.
//   pitch = Length between threads.
//   thread_depth = Depth of the threads.  Default=pitch/2
//   thread_angle = The pressure angle profile angle of the threads.  Default = 14.5 degree ACME profile.
//   left_handed = If true, create left-handed threads.  Default = false
//   bevel = if true, bevel the thread ends.  Default: false
//   bevel1 = if true, bevel the axis-negative end of the thread.  Default: false
//   bevel2 = if true, bevel the axis-positive end of the thread.  Default: false
//   starts = The number of lead starts.  Default = 1
//   internal = If true, make this a mask for making internal threads.
//   slop = printer slop calibration to allow for tight fitting of parts.  Default: `PRINTER_SLOP`
//   profile = The shape of a thread, if not a symmetric trapezoidal form.  Given as a 2D path, where X is between -1/2 and 1/2, representing the pitch distance, and Y is 0 for the peak, and `-depth/pitch` for the valleys.  The segment between the end of one thread profile and the start of the next is automatic, so the start and end coordinates should not both be at the same Y at X = ±1/2.  This path is scaled up by the pitch size in both dimensions when making the final threading.  This overrides the `thread_angle` and `thread_depth` options.
//   orient = Orientation of the rod.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_Z`.
//   align = Alignment of the rod.  Use the `V_` constants from `constants.scad`.  Default: `V_CENTER`.
//   center = If given, overrides `align`.  A true value sets `align=V_CENTER`, false sets `align=ALIGN_POS`.
// Examples(Med):
//   trapezoidal_threaded_rod(d=10, l=40, pitch=2, thread_angle=15, $fn=32);
//   trapezoidal_threaded_rod(d=3/8*25.4, l=20, pitch=1/8*25.4, thread_angle=29, $fn=32);
//   trapezoidal_threaded_rod(d=60, l=16, pitch=8, thread_depth=3, thread_angle=45, left_handed=true, $fa=2, $fs=2);
//   trapezoidal_threaded_rod(d=60, l=16, pitch=8, thread_depth=3, thread_angle=45, left_handed=true, starts=4, $fa=2, $fs=2);
//   trapezoidal_threaded_rod(d=16, l=40, pitch=2, thread_angle=30);
//   trapezoidal_threaded_rod(d=10, l=40, pitch=3, thread_angle=15, left_handed=true, starts=3, $fn=36);
//   trapezoidal_threaded_rod(d=25, l=40, pitch=10, thread_depth=8/3, thread_angle=50, starts=4, center=false, $fa=2, $fs=2);
//   trapezoidal_threaded_rod(d=50, l=35, pitch=8, thread_angle=30, starts=3, bevel=true);
//   trapezoidal_threaded_rod(l=25, d=10, pitch=2, thread_angle=15, starts=3, $fa=1, $fs=1, orient=ORIENT_X, align=V_UP);
module trapezoidal_threaded_rod(
	d=10,
	l=100,
	pitch=2,
	thread_angle=15,
	thread_depth=undef,
	left_handed=false,
	bevel=false,
	bevel1=false,
	bevel2=false,
	starts=1,
	profile=undef,
	internal=false,
	slop=undef,
	orient=ORIENT_Z,
	align=V_CENTER,
	center=undef
) {
	function _thread_pt(thread, threads, start, starts, astep, asteps, part, parts) =
		astep + asteps * (thread + threads * (part + parts * start));

	d = internal? d+default(slop,PRINTER_SLOP)*3 : d;
	astep = 360 / quantup(segs(d/2), starts);
	asteps = ceil(360/astep);
	threads = ceil(l/pitch/starts)+(starts<4?4-starts:1);
	depth = min((thread_depth==undef? pitch/2 : thread_depth), pitch/2/tan(thread_angle));
	pa_delta = min(pitch/4-0.01,depth*tan(thread_angle)/2)/pitch;
	dir = left_handed? -1 : 1;
	r1 = -depth/pitch;
	z1 = 1/4-pa_delta;
	z2 = 1/4+pa_delta;
	profile = profile!=undef? profile : [
		[-z2, r1],
		[-z1,  0],
		[ z1,  0],
		[ z2, r1],
	];
	parts = len(profile);
	poly_points = concat(
		[
			for (
				start  = [0 : starts-1],
				part   = [0 : parts-1],
				thread = [0 : threads-1],
				astep  = [0 : asteps-1]
			) let (
				ppt = profile[part] * pitch,
				dz = ppt.x,
				r = ppt.y + d/2,
				a = astep / asteps,
				c = cos(360 * (a * dir + start/starts)),
				s = sin(360 * (a * dir + start/starts)),
				z = (thread + a - threads/2) * starts * pitch
			) [r*c, r*s, z+dz]
		],
		[[0, 0, -threads*pitch*starts/2-pitch/4], [0, 0, threads*pitch*starts/2+pitch/4]]
	);
	point_count = len(poly_points);
	poly_faces = concat(
		// Thread surfaces
		[
			for (
				start  = [0 : starts-1],
				part   = [0 : parts-2],
				thread = [0 : threads-1],
				astep  = [0 : asteps-1],
				trinum = [0, 1]
			) let (
				p0 = _thread_pt(thread, threads, start, starts, astep, asteps, part, parts),
				p1 = _thread_pt(thread, threads, start, starts, astep, asteps, part+1, parts),
				p2 = _thread_pt(thread, threads, start, starts, astep+1, asteps, part, parts),
				p3 = _thread_pt(thread, threads, start, starts, astep+1, asteps, part+1, parts),
				tri = trinum==0? [p0, p1, p3] : [p0, p3, p2],
				otri = left_handed? [tri[0], tri[2], tri[1]] : tri
			)
			if (!(thread == threads-1 && astep == asteps-1)) otri
		],
		// Thread trough bottom
		[
			for (
				start  = [0 : starts-1],
				thread = [0 : threads-1],
				astep  = [0 : asteps-1],
				trinum = [0, 1]
			) let (
				p0 = _thread_pt(thread, threads, start, starts, astep, asteps, parts-1, parts),
				p1 = _thread_pt(thread, threads, (start+(left_handed?1:starts-1))%starts, starts, astep+asteps/starts, asteps, 0, parts),
				p2 = p0 + 1,
				p3 = p1 + 1,
				tri = trinum==0? [p0, p1, p3] : [p0, p3, p2],
				otri = left_handed? [tri[0], tri[2], tri[1]] : tri
			)
			if (
				!(thread >= threads-1 && astep > asteps-asteps/starts-2) &&
				!(thread >= threads-2 && starts == 1 && astep >= asteps-1)
			) otri
		],
		// top and bottom thread endcap
		[
			for (
				start  = [0 : starts-1],
				part   = [1 : parts-2],
				is_top = [0, 1]
			) let (
				astep = is_top? asteps-1 : 0,
				thread = is_top? threads-1 : 0,
				p0 = _thread_pt(thread, threads, start, starts, astep, asteps, 0, parts),
				p1 = _thread_pt(thread, threads, start, starts, astep, asteps, part, parts),
				p2 = _thread_pt(thread, threads, start, starts, astep, asteps, part+1, parts),
				tri = is_top? [p0, p1, p2] : [p0, p2, p1],
				otri = left_handed? [tri[0], tri[2], tri[1]] : tri
			) otri
		],
		// body side triangles
		[
			for (
				start  = [0 : starts-1],
				is_top = [false, true],
				trinum = [0, 1]
			) let (
				astep = is_top? asteps-1 : 0,
				thread = is_top? threads-1 : 0,
				ostart = (is_top != left_handed? (start+1) : (start+starts-1))%starts,
				ostep = is_top? astep-asteps/starts : astep+asteps/starts,
				oparts = is_top? parts-1 : 0,
				p0 = is_top? point_count-1 : point_count-2,
				p1 = _thread_pt(thread, threads, start, starts, astep, asteps, 0, parts),
				p2 = _thread_pt(thread, threads, start, starts, astep, asteps, parts-1, parts),
				p3 = _thread_pt(thread, threads, ostart, starts, ostep, asteps, oparts, parts),
				tri = trinum==0?
					(is_top? [p0, p1, p2] : [p0, p2, p1]) :
					(is_top? [p0, p3, p1] : [p0, p3, p2]),
				otri = left_handed? [tri[0], tri[2], tri[1]] : tri
			) otri
		],
		// Caps
		[
			for (
				start  = [0 : starts-1],
				astep  = [0 : asteps/starts-1],
				is_top = [0, 1]
			) let (
				thread = is_top? threads-1 : 0,
				part = is_top? parts-1 : 0,
				ostep = is_top? asteps-astep-2 : astep,
				p0 = is_top? point_count-1 : point_count-2,
				p1 = _thread_pt(thread, threads, start, starts, ostep, asteps, part, parts),
				p2 = _thread_pt(thread, threads, start, starts, ostep+1, asteps, part, parts),
				tri = is_top? [p0, p2, p1] : [p0, p1, p2],
				otri = left_handed? [tri[0], tri[2], tri[1]] : tri
			) otri
		]
	);
	orient_and_align([d,d,l], orient, align, center) {
		difference() {
			polyhedron(points=poly_points, faces=poly_faces, convexity=threads*starts*2);
			zspread(l+4*pitch*starts) cube([d+1, d+1, 4*pitch*starts], center=true);
			if (bevel || bevel1 || bevel2) {
				depth1 = (bevel || bevel1) ? depth : 0;
				depth2 = (bevel || bevel2) ? depth : 0;
				cylinder_mask(d=d, l=l+0.01, chamfer1=depth1, chamfer2=depth2);
			}
		}
	}
}


// Module: trapezoidal_threaded_nut()
// Description:
//   Constructs a hex nut for a threaded screw rod.  This method makes
//   much smoother threads than the naive linear_extrude method.
//   For metric screw threads, use thread_angle=30 and leave out thread_depth argument.
//   For SAE screw threads, use thread_angle=30 and leave out thread_depth argument.
//   For metric trapezoidal threads, use thread_angle=15 and thread_depth=pitch/2.
//   For ACME threads, use thread_angle=14.5 and thread_depth=pitch/2.
//   For square threads, use thread_angle=0 and thread_depth=pitch/2.
// Arguments:
//   od = diameter of the nut.
//   id = diameter of threaded rod to screw onto.
//   h = height/thickness of nut.
//   pitch = Length between threads.
//   thread_depth = Depth of the threads.  Default=pitch/2.
//   thread_angle = The pressure angle profile angle of the threads.  Default = 14.5 degree ACME profile.
//   left_handed = if true, create left-handed threads.  Default = false
//   starts = The number of lead starts.  Default = 1
//   bevel = if true, bevel the thread ends.  Default: true
//   slop = printer slop calibration to allow for tight fitting of parts.  Default: `PRINTER_SLOP`
//   profile = The shape of a thread, if not a symmetric trapezoidal form.  Given as a 2D path, where X is between -1/2 and 1/2, representing the pitch distance, and Y is 0 for the peak, and `-depth/pitch` for the valleys.  The segment between the end of one thread profile and the start of the next is automatic, so the start and end coordinates should not both be at the same Y at X = ±1/2.  This path is scaled up by the pitch size in both dimensions when making the final threading.  This overrides the `thread_angle` and `thread_depth` options.
//   orient = Orientation of the nut.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_Z`.
//   align = Alignment of the nut.  Use the `V_` constants from `constants.scad`.  Default: `V_CENTER`.
// Examples(Med):
//   trapezoidal_threaded_nut(od=16, id=8, h=8, pitch=2, slop=0.2, align=V_UP);
//   trapezoidal_threaded_nut(od=17.4, id=10, h=10, pitch=2, slop=0.2, left_handed=true);
//   trapezoidal_threaded_nut(od=17.4, id=10, h=10, pitch=2, thread_angle=15, starts=3, $fa=1, $fs=1);
module trapezoidal_threaded_nut(
	od=17.4,
	id=10,
	h=10,
	pitch=2,
	thread_depth=undef,
	thread_angle=15,
	profile=undef,
	left_handed=false,
	starts=1,
	bevel=true,
	slop=undef,
	orient=ORIENT_Z,
	align=V_CENTER
) {
	depth = min((thread_depth==undef? pitch/2 : thread_depth), pitch/2/tan(thread_angle));
	slop = default(slop, PRINTER_SLOP);
	orient_and_align([od/cos(30),od,h], orient, align) {
		difference() {
			cylinder(d=od/cos(30), h=h, center=true, $fn=6);
			trapezoidal_threaded_rod(
				d=id,
				l=h+1,
				pitch=pitch,
				thread_depth=depth,
				thread_angle=thread_angle,
				profile=profile,
				left_handed=left_handed,
				starts=starts,
				internal=true,
				slop=slop
			);
			if (bevel) {
				zflip_copy() {
					down(h/2+0.01) {
						cylinder(r1=id/2+slop, r2=id/2+slop-depth, h=depth, center=false);
					}
				}
			}
		}
	}
}


// Section: Triangular Threading

// Module: threaded_rod()
// Description:
//   Constructs a standard metric or UTS threaded screw rod.  This method
//   makes much smoother threads than the naive linear_extrude method.
// Arguments:
//   d = Outer diameter of threaded rod.
//   l = length of threaded rod.
//   pitch = Length between threads.
//   left_handed = if true, create left-handed threads.  Default = false
//   bevel = if true, bevel the thread ends.  Default: false
//   bevel1 = if true, bevel the axis-negative end of the thread.  Default: false
//   bevel2 = if true, bevel the axis-positive end of the thread.  Default: false
//   internal = If true, make this a mask for making internal threads.
//   slop = printer slop calibration to allow for tight fitting of parts.  Default: `PRINTER_SLOP`
//   orient = Orientation of the rod.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_Z`.
//   align = Alignment of the rod.  Use the `V_` constants from `constants.scad`.  Default: `V_CENTER`.
// Example(2D):
//   projection(cut=true)
//       threaded_rod(d=10, l=15, pitch=2, orient=ORIENT_X);
// Examples(Med):
//   threaded_rod(d=10, l=20, pitch=1.25, left_handed=true, $fa=1, $fs=1);
//   threaded_rod(d=25, l=20, pitch=2, $fa=1, $fs=1);
module threaded_rod(
	d=10, l=100, pitch=2,
	left_handed=false,
	bevel=false,
	bevel1=false,
	bevel2=false,
	internal=false,
	slop=undef,
	orient=ORIENT_Z,
	align=V_CENTER
) {
	depth = pitch * cos(30) * 5/8;
	profile = internal? [
		[-6/16, -depth/pitch],
		[-1/16,  0],
		[-1/32,  0.02],
		[ 1/32,  0.02],
		[ 1/16,  0],
		[ 6/16, -depth/pitch]
	] : [
		[-7/16, -depth/pitch*1.07],
		[-6/16, -depth/pitch],
		[-1/16,  0],
		[ 1/16,  0],
		[ 6/16, -depth/pitch],
		[ 7/16, -depth/pitch*1.07]
	];
	trapezoidal_threaded_rod(
		d=d, l=l, pitch=pitch,
		thread_depth=depth,
		thread_angle=30,
		profile=profile,
		left_handed=left_handed,
		bevel=bevel,
		bevel1=bevel1,
		bevel2=bevel2,
		internal=internal,
		slop=slop,
		orient=orient,
		align=align
	);
}



// Module: threaded_nut()
// Description:
//   Constructs a hex nut for a metric or UTS threaded screw rod.  This method
//   makes much smoother threads than the naive linear_extrude method.
// Arguments:
//   od = diameter of the nut.
//   id = diameter of threaded rod to screw onto.
//   h = height/thickness of nut.
//   pitch = Length between threads.
//   left_handed = if true, create left-handed threads.  Default = false
//   bevel = if true, bevel the thread ends.  Default: false
//   slop = printer slop calibration to allow for tight fitting of parts.  default=0.2
//   orient = Orientation of the nut.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_Z`.
//   align = Alignment of the nut.  Use the `V_` constants from `constants.scad`.  Default: `V_CENTER`.
// Examples(Med):
//   threaded_nut(od=16, id=8, h=8, pitch=1.25, left_handed=true, slop=0.2, $fa=1, $fs=1);
module threaded_nut(
	od=16, id=10, h=10,
	pitch=2, left_handed=false,
	bevel=false, slop=undef,
	orient=ORIENT_Z, align=V_CENTER
) {
	depth = pitch * cos(30) * 5/8;
	profile = [
		[-6/16, -depth/pitch],
		[-1/16,  0],
		[-1/32,  0.02],
		[ 1/32,  0.02],
		[ 1/16,  0],
		[ 6/16, -depth/pitch]
	];
	trapezoidal_threaded_nut(
		od=od, id=id, h=h,
		pitch=pitch, thread_angle=30,
		profile=profile,
		left_handed=left_handed,
		bevel=bevel, slop=slop,
		orient=orient, align=align
	);
}


// Section: Buttress Threading

// Module: buttress_threaded_rod()
// Description:
//   Constructs a simple buttress threaded screw rod.  This method
//   makes much smoother threads than the naive linear_extrude method.
// Arguments:
//   d = Outer diameter of threaded rod.
//   l = length of threaded rod.
//   pitch = Length between threads.
//   left_handed = if true, create left-handed threads.  Default = false
//   bevel = if true, bevel the thread ends.  Default: false
//   bevel1 = if true, bevel the axis-negative end of the thread.  Default: false
//   bevel2 = if true, bevel the axis-positive end of the thread.  Default: false
//   internal = If true, this is a mask for making internal threads.
//   slop = printer slop calibration to allow for tight fitting of parts.  default=0.2
//   orient = Orientation of the rod.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_Z`.
//   align = Alignment of the rod.  Use the `V_` constants from `constants.scad`.  Default: `V_CENTER`.
// Example(2D):
//   projection(cut=true)
//       buttress_threaded_rod(d=10, l=15, pitch=2, orient=ORIENT_X);
// Examples(Med):
//   buttress_threaded_rod(d=10, l=20, pitch=1.25, left_handed=true, $fa=1, $fs=1);
//   buttress_threaded_rod(d=25, l=20, pitch=2, $fa=1, $fs=1);
module buttress_threaded_rod(
	d=10, l=100, pitch=2,
	left_handed=false,
	bevel=false,
	bevel1=false,
	bevel2=false,
	internal=false,
	slop=undef,
	orient=ORIENT_Z,
	align=V_CENTER
) {
	depth = pitch * 3/4;
	profile = [
		[ -7/16, -0.75],
		[  5/16,  0],
		[  7/16,  0],
		[  7/16, -0.75],
		[  1/ 2, -0.77],
	];
	trapezoidal_threaded_rod(
		d=d, l=l, pitch=pitch,
		thread_depth=depth,
		thread_angle=30,
		profile=profile,
		left_handed=left_handed,
		bevel=bevel,
		bevel1=bevel1,
		bevel2=bevel2,
		internal=internal,
		orient=orient,
		slop=slop,
		align=align
	);
}



// Module: buttress_threaded_nut()
// Description:
//   Constructs a hex nut for a simple buttress threaded screw rod.  This method
//   makes much smoother threads than the naive linear_extrude method.
// Arguments:
//   od = diameter of the nut.
//   id = diameter of threaded rod to screw onto.
//   h = height/thickness of nut.
//   pitch = Length between threads.
//   left_handed = if true, create left-handed threads.  Default = false
//   bevel = if true, bevel the thread ends.  Default: false
//   slop = printer slop calibration to allow for tight fitting of parts.  default=0.2
//   orient = Orientation of the nut.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_Z`.
//   align = Alignment of the nit.  Use the `V_` constants from `constants.scad`.  Default: `V_CENTER`.
// Examples:
//   buttress_threaded_nut(od=16, id=8, h=8, pitch=1.25, left_handed=true, slop=0.2, $fa=1, $fs=1);
module buttress_threaded_nut(
	od=16, id=10, h=10,
	pitch=2, left_handed=false,
	bevel=false, slop=undef,
	orient=ORIENT_Z, align=V_CENTER
) {
	depth = pitch * 3/4;
	profile = [
		[ -7/16, -0.75],
		[  5/16,  0],
		[  7/16,  0],
		[  7/16, -0.75],
		[  1/ 2, -0.77],
	];
	trapezoidal_threaded_nut(
		od=od, id=id, h=h,
		pitch=pitch, thread_angle=30,
		profile=profile,
		thread_depth=pitch*3*sqrt(3)/8,
		left_handed=left_handed,
		bevel=bevel, slop=slop,
		orient=orient, align=align
	);
}


// Section: Metric Trapezoidal Threading

// Module: metric_trapezoidal_threaded_rod()
// Description:
//   Constructs a metric trapezoidal threaded screw rod.  This method makes much
//   smoother threads than the naive linear_extrude method.
// Arguments:
//   d = Outer diameter of threaded rod.
//   l = length of threaded rod.
//   pitch = Length between threads.
//   left_handed = if true, create left-handed threads.  Default = false
//   bevel = if true, bevel the thread ends.  Default: false
//   starts = The number of lead starts.  Default = 1
//   orient = Orientation of the rod.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_Z`.
//   align = Alignment of the rod.  Use the `V_` constants from `constants.scad`.  Default: `V_CENTER`.
// Example(2D):
//   projection(cut=true)
//       metric_trapezoidal_threaded_rod(d=10, l=15, pitch=2, orient=ORIENT_X);
// Examples(Med):
//   metric_trapezoidal_threaded_rod(d=10, l=30, pitch=2, left_handed=true, $fa=1, $fs=1);
module metric_trapezoidal_threaded_rod(
	d=10, l=100, pitch=2,
	left_handed=false,
	starts=1,
	bevel=false,
	bevel1=false,
	bevel2=false,
	orient=ORIENT_Z,
	align=V_CENTER
) {
	trapezoidal_threaded_rod(
		d=d, l=l,
		pitch=pitch,
		thread_angle=15,
		left_handed=left_handed,
		starts=starts,
		bevel=bevel,
		bevel1=bevel1,
		bevel2=bevel2,
		orient=orient,
		align=align
	);
}



// Module: metric_trapezoidal_threaded_nut()
// Description:
//   Constructs a hex nut for a metric trapezoidal threaded screw rod.  This method
//   makes much smoother threads than the naive linear_extrude method.
// Arguments:
//   od = diameter of the nut.
//   id = diameter of threaded rod to screw onto.
//   h = height/thickness of nut.
//   pitch = Length between threads.
//   left_handed = if true, create left-handed threads.  Default = false
//   bevel = if true, bevel the thread ends.  Default: false
//   starts = The number of lead starts.  Default = 1
//   slop = printer slop calibration to allow for tight fitting of parts.  default=0.2
//   orient = Orientation of the nut.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_Z`.
//   align = Alignment of the nut.  Use the `V_` constants from `constants.scad`.  Default: `V_CENTER`.
// Examples(Med):
//   metric_trapezoidal_threaded_nut(od=16, id=10, h=10, pitch=2, left_handed=true, bevel=true, $fa=1, $fs=1);
module metric_trapezoidal_threaded_nut(
	od=17.4, id=10.5, h=10,
	pitch=3.175,
	starts=1,
	left_handed=false,
	bevel=false,
	slop=undef,
	orient=ORIENT_Z,
	align=V_CENTER
) {
	trapezoidal_threaded_nut(
		od=od, id=id, h=h,
		pitch=pitch, thread_angle=15,
		left_handed=left_handed,
		starts=starts,
		bevel=bevel,
		slop=slop,
		orient=orient,
		align=align
	);
}


// Section: ACME Trapezoidal Threading

// Module: acme_threaded_rod()
// Description:
//   Constructs an ACME trapezoidal threaded screw rod.  This method makes
//   much smoother threads than the naive linear_extrude method.
// Arguments:
//   d = Outer diameter of threaded rod.
//   l = length of threaded rod.
//   pitch = Length between threads.
//   thread_depth = Depth of the threads.  Default = pitch/2
//   thread_angle = The pressure angle profile angle of the threads.  Default = 14.5 degrees
//   starts = The number of lead starts.  Default = 1
//   left_handed = if true, create left-handed threads.  Default = false
//   bevel = if true, bevel the thread ends.  Default: false
//   bevel1 = if true, bevel the axis-negative end of the thread.  Default: false
//   bevel2 = if true, bevel the axis-positive end of the thread.  Default: false
//   orient = Orientation of the rod.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_Z`.
//   align = Alignment of the rod.  Use the `V_` constants from `constants.scad`.  Default: `V_CENTER`.
// Example(2D):
//   projection(cut=true)
//       acme_threaded_rod(d=10, l=15, pitch=2, orient=ORIENT_X);
// Examples(Med):
//   acme_threaded_rod(d=3/8*25.4, l=20, pitch=1/8*25.4, $fn=32);
//   acme_threaded_rod(d=10, l=30, pitch=2, starts=3, $fa=1, $fs=1);
module acme_threaded_rod(
	d=10, l=100, pitch=2,
	thread_angle=14.5,
	thread_depth=undef,
	starts=1,
	left_handed=false,
	bevel=false,
	bevel1=false,
	bevel2=false,
	orient=ORIENT_Z,
	align=V_CENTER
) {
	trapezoidal_threaded_rod(
		d=d, l=l, pitch=pitch,
		thread_angle=thread_angle,
		thread_depth=thread_depth,
		starts=starts,
		left_handed=left_handed,
		bevel=bevel,
		bevel1=bevel1,
		bevel2=bevel2,
		orient=orient,
		align=align
	);
}



// Module: acme_threaded_nut()
// Description:
//   Constructs a hex nut for an ACME threaded screw rod.  This method makes
//   much smoother threads than the naive linear_extrude method.
// Arguments:
//   od = diameter of the nut.
//   id = diameter of threaded rod to screw onto.
//   h = height/thickness of nut.
//   pitch = Length between threads.
//   thread_depth = Depth of the threads.  Default=pitch/2
//   thread_angle = The pressure angle profile angle of the threads.  Default = 14.5 degree ACME profile.
//   left_handed = if true, create left-handed threads.  Default = false
//   bevel = if true, bevel the thread ends.  Default: false
//   slop = printer slop calibration to allow for tight fitting of parts.  default=0.2
//   orient = Orientation of the nut.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_Z`.
//   align = Alignment of the nut.  Use the `V_` constants from `constants.scad`.  Default: `V_CENTER`.
// Examples(Med):
//   acme_threaded_nut(od=16, id=3/8*25.4, h=8, pitch=1/8*25.4, slop=0.2);
//   acme_threaded_nut(od=16, id=10, h=10, pitch=2, starts=3, slop=0.2, $fa=1, $fs=1);
module acme_threaded_nut(
	od, id, h, pitch,
	thread_angle=14.5,
	thread_depth=undef,
	starts=1,
	left_handed=false,
	bevel=false,
	slop=undef,
	orient=ORIENT_Z,
	align=V_CENTER
) {
	trapezoidal_threaded_nut(
		od=od, id=id, h=h, pitch=pitch,
		thread_depth=thread_depth,
		thread_angle=thread_angle,
		left_handed=left_handed,
		bevel=bevel,
		starts=starts,
		slop=slop,
		orient=orient,
		align=align
	);
}


// Section: Square Threading

// Module: square_threaded_rod()
// Description:
//   Constructs a square profile threaded screw rod.  This method makes
//   much smoother threads than the naive linear_extrude method.
// Arguments:
//   d = Outer diameter of threaded rod.
//   l = length of threaded rod.
//   pitch = Length between threads.
//   left_handed = if true, create left-handed threads.  Default = false
//   bevel = if true, bevel the thread ends.  Default: false
//   bevel1 = if true, bevel the axis-negative end of the thread.  Default: false
//   bevel2 = if true, bevel the axis-positive end of the thread.  Default: false
//   starts = The number of lead starts.  Default = 1
//   orient = Orientation of the rod.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_Z`.
//   align = Alignment of the rod.  Use the `V_` constants from `constants.scad`.  Default: `V_CENTER`.
// Example(2D):
//   projection(cut=true)
//       square_threaded_rod(d=10, l=15, pitch=2, orient=ORIENT_X);
// Examples(Med):
//   square_threaded_rod(d=10, l=20, pitch=2, starts=2, $fn=32);
module square_threaded_rod(
	d=10, l=100, pitch=2,
	left_handed=false,
	bevel=false,
	bevel1=false,
	bevel2=false,
	starts=1,
	orient=ORIENT_Z,
	align=V_CENTER
) {
	trapezoidal_threaded_rod(
		d=d, l=l, pitch=pitch,
		thread_angle=0,
		left_handed=left_handed,
		bevel=bevel,
		bevel1=bevel1,
		bevel2=bevel2,
		starts=starts,
		orient=orient,
		align=align
	);
}



// Module: square_threaded_nut()
// Description:
//   Constructs a hex nut for a square profile threaded screw rod.  This method
//   makes much smoother threads than the naive linear_extrude method.
// Arguments:
//   od = diameter of the nut.
//   id = diameter of threaded rod to screw onto.
//   h = height/thickness of nut.
//   pitch = Length between threads.
//   left_handed = if true, create left-handed threads.  Default = false
//   bevel = if true, bevel the thread ends.  Default: false
//   starts = The number of lead starts.  Default = 1
//   slop = printer slop calibration to allow for tight fitting of parts.  default=0.2
//   orient = Orientation of the nut.  Use the `ORIENT_` constants from `constants.scad`.  Default: `ORIENT_Z`.
//   align = Alignment of the nut.  Use the `V_` constants from `constants.scad`.  Default: `V_CENTER`.
// Examples(Med):
//   square_threaded_nut(od=16, id=10, h=10, pitch=2, starts=2, slop=0.15, $fn=32);
module square_threaded_nut(
	od=17.4, id=10.5, h=10,
	pitch=3.175,
	left_handed=false,
	bevel=false,
	starts=1,
	slop=undef,
	orient=ORIENT_Z,
	align=V_CENTER
) {
	trapezoidal_threaded_nut(
		od=od, id=id, h=h, pitch=pitch,
		thread_angle=0,
		left_handed=left_handed,
		bevel=bevel,
		starts=starts,
		slop=slop,
		orient=orient,
		align=align
	);
}



// vim: noexpandtab tabstop=4 shiftwidth=4 softtabstop=4 nowrap
