//////////////////////////////////////////////////////////////////////
// LibFile: phillips_drive.scad
//   Phillips driver bits
//   To use, add these lines to the top of your file:
//   ```
//   include <BOSL/constants.scad>
//   use <BOSL/phillips_drive.scad>
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
include <constants.scad>
include <compat.scad>


// Section: Modules


// Module: phillips_drive()
// Description: Creates a model of a phillips driver bit of a given named size.
// Arguments:
//   size = The size of the bit.  "#1", "#2", or "#3"
//   shaft = The diameter of the drive bit's shaft.
//   l = The length of the drive bit.
// Example:
//   xdistribute(10) {
//      phillips_drive(size="#1", shaft=4, l=20);
//      phillips_drive(size="#2", shaft=6, l=20);
//      phillips_drive(size="#3", shaft=6, l=20);
//   }
module phillips_drive(size="#2", shaft=6, l=20, orient=ORIENT_Z, align=V_UP) {
	// These are my best guess reverse-engineered measurements of
	// the tip diameters of various phillips screwdriver sizes.
	ang = 11;
	rads = [["#1", 1.25], ["#2", 1.77], ["#3", 2.65]];
	radidx = search([size], rads)[0];
	r = radidx == []? 0 : rads[radidx][1];
	h = (r/2)/tan(ang);
	cr = r/2;
	orient_and_align([shaft, shaft, l], orient, align) {
		down(l/2) {
			difference() {
				intersection() {
					union() {
						clip = (shaft-1.2*r)/2/tan(26.5);
						zrot(360/8/2) cylinder(h=clip, d1=1.2*r/cos(360/8/2), d2=shaft/cos(360/8/2), center=false, $fn=8);
						up(clip-0.01) cylinder(h=l-clip, d=shaft, center=false, $fn=24);
					}
					cylinder(d=shaft, h=l, center=false, $fn=24);
				}
				zrot(45)
				zring(n=4) {
					yrot(ang) {
						zrot(-45) {
							off = (r/2-cr*(sqrt(2)-1))/sqrt(2);
							translate([off, off, 0]) {
								linear_extrude(height=l*2, convexity=4) {
									difference() {
										union() {
											square([shaft, shaft], center=false);
											back(cr) zrot(1.125) square([shaft, shaft], center=false);
											right(cr) zrot(-1.125) square([shaft, shaft], center=false);
										}
										difference() {
											square([cr*2, cr*2], center=true);
											translate([cr,cr,0]) circle(r=cr, $fn=8);
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


// vim: noexpandtab tabstop=4 shiftwidth=4 softtabstop=4 nowrap
