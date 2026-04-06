//////////////////////////////////////////////////////////////////////
// LibFile: torx_drive.scad
//   Torx driver bits
//   To use, add these lines to the top of your file:
//   ```
//   include <BOSL/constants.scad>
//   use <BOSL/torx_drive.scad>
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
include <constants.scad>
include <compat.scad>


// Section: Functions


// Function: torx_outer_diam()
// Description: Get the typical outer diameter of Torx profile.
// Arguments:
//   size = Torx size.
function torx_outer_diam(size) = lookup(size, [
	[  6,  1.75],
	[  8,  2.40],
	[ 10,  2.80],
	[ 15,  3.35],
	[ 20,  3.95],
	[ 25,  4.50],
	[ 30,  5.60],
	[ 40,  6.75],
	[ 45,  7.93],
	[ 50,  8.95],
	[ 55, 11.35],
	[ 60, 13.45],
	[ 70, 15.70],
	[ 80, 17.75],
	[ 90, 20.20],
	[100, 22.40]
]);
 

// Function: torx_inner_diam()
// Description: Get typical inner diameter of Torx profile.
// Arguments:
//   size = Torx size.
function torx_inner_diam(size) = lookup(size, [
	[  6,  1.27],
	[  8,  1.75],
	[ 10,  2.05],
	[ 15,  2.40],
	[ 20,  2.85],
	[ 25,  3.25],
	[ 30,  4.05],
	[ 40,  4.85],
	[ 45,  5.64],
	[ 50,  6.45],
	[ 55,  8.05],
	[ 60,  9.60],
	[ 70, 11.20],
	[ 80, 12.80],
	[ 90, 14.40],
	[100, 16.00]
]);
 

// Function: torx_depth()
// Description: Gets typical drive hole depth.
// Arguments:
//   size = Torx size.
function torx_depth(size) = lookup(size, [
	[  6,  1.82],
	[  8,  3.05],
	[ 10,  3.56],
	[ 15,  3.81],
	[ 20,  4.07],
	[ 25,  4.45],
	[ 30,  4.95],
	[ 40,  5.59],
	[ 45,  6.22],
	[ 50,  6.48],
	[ 55,  6.73],
	[ 60,  8.17],
	[ 70,  8.96],
	[ 80,  9.90],
	[ 90, 10.56],
	[100, 11.35]
]);
 

// Function: torx_tip_radius()
// Description: Gets minor rounding radius of Torx profile.
// Arguments:
//   size = Torx size.
function torx_tip_radius(size) = lookup(size, [
	[  6, 0.132],
	[  8, 0.190],
	[ 10, 0.229],
	[ 15, 0.267],
	[ 20, 0.305],
	[ 25, 0.375],
	[ 30, 0.451],
	[ 40, 0.546],
	[ 45, 0.574],
	[ 50, 0.775],
	[ 55, 0.867],
	[ 60, 1.067],
	[ 70, 1.194],
	[ 80, 1.526],
	[ 90, 1.530],
	[100, 1.720]
]);


// Function: torx_rounding_radius()
// Description: Gets major rounding radius of Torx profile.
// Arguments:
//   size = Torx size.
function torx_rounding_radius(size) = lookup(size, [
	[  6, 0.383],
	[  8, 0.510],
	[ 10, 0.598],
	[ 15, 0.716],
	[ 20, 0.859],
	[ 25, 0.920],
	[ 30, 1.194],
	[ 40, 1.428],
	[ 45, 1.796],
	[ 50, 1.816],
	[ 55, 2.667],
	[ 60, 2.883],
	[ 70, 3.477],
	[ 80, 3.627],
	[ 90, 4.468],
	[100, 4.925]
]);


// Section: Modules


// Module: torx_drive2d()
// Description: Creates a torx bit 2D profile.
// Arguments:
//   size = Torx size.
// Example(2D):
//   torx_drive2d(size=30, $fa=1, $fs=1);
module torx_drive2d(size) {
	od = torx_outer_diam(size);
	id = torx_inner_diam(size);
	tip = torx_tip_radius(size);
	rounding = torx_rounding_radius(size);
	base = od - 2*tip;
	$fn = quantup(segs(od/2),12);
	difference() {
		union() {
			circle(d=base);
			zring(n=2) {
				hull() {
					zring(n=3) {
						translate([base/2,0,0]) {
							circle(r=tip, $fn=$fn/2);
						}
					}
				}
			}
		}
		zring(n=6) {
			zrot(180/6) {
				translate([id/2+rounding,0,0]) {
					circle(r=rounding);
				}
			}
		}
	}
}



// Module: torx_drive()
// Description: Creates a torx bit tip.
// Arguments:
//   size = Torx size.
//   l = Length of bit.
//   center = If true, centers bit vertically.
// Examples:
//   torx_drive(size=30, l=10, $fa=1, $fs=1);
module torx_drive(size, l=5, center=undef, orient=ORIENT_Z, align=V_UP) {
	od = torx_outer_diam(size);
	orient_and_align([od, od, l], orient, align, center) {
		linear_extrude(height=l, convexity=4, center=true) {
			torx_drive2d(size);
		}
	}
}



// vim: noexpandtab tabstop=4 shiftwidth=4 softtabstop=4 nowrap

