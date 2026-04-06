//////////////////////////////////////////////////////////////////////
// LibFile: wiring.scad
//   Rendering for wiring bundles
//   To use, include the following line at the top of your file:
//   ```
//   use <BOSL/wiring.scad>
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


include <math.scad>
include <paths.scad>
include <beziers.scad>


// Section: Functions


// Function: hex_offset_ring()
// Description:
//   Returns a hexagonal ring of points, with a spacing of `d`.
//   If `lev=0`, returns a single point at `[0,0]`.  All greater
//   levels return 6 times `lev` points.
// Usage:
//   hex_offset_ring(d, lev)
// Arguments:
//   d = Base unit diameter to build rings upon.
//   lev = How many rings to produce.
// Example:
//   hex_offset_ring(d=1, lev=3); // Returns a hex ring of 18 points.
function hex_offset_ring(d, lev=0) =
	(lev == 0)? [[0,0]] : [
		for (
			sideang = [0:60:359.999],
			sidenum = [1:lev]
		) [
			lev*d*cos(sideang)+sidenum*d*cos(sideang+120),
			lev*d*sin(sideang)+sidenum*d*sin(sideang+120)
		]
	];


// Function: hex_offsets()
// Description:
//   Returns the centerpoints for the optimal hexagonal packing
//   of at least `n` circular items, of diameter `d`.  Will return
//   enough points to fill out the last ring, even if that is more
//   than `n` points.
// Usage:
//   hex_offsets(n, d)
// Arguments:
//   n = Number of items to bundle.
//   d = How far to space each point away from others.
function hex_offsets(n, d, lev=0, arr=[]) =
	(len(arr) >= n)? arr :
		hex_offsets(
			n=n,
			d=d,
			lev=lev+1,
			arr=concat(arr, hex_offset_ring(d, lev=lev))
		);



// Section: Modules


// Module: wiring()
// Description:
//   Returns a 3D object representing a bundle of wires that follow a given path,
//   with the corners filleted to a given radius.  There are 17 base wire colors.
//   If you have more than 17 wires, colors will get re-used.
// Usage:
//   wiring(path, wires, [wirediam], [fillet], [wirenum], [bezsteps]);
// Arguments:
//   path = The 3D polyline path that the wire bundle should follow.
//   wires = The number of wires in the wiring bundle.
//   wirediam = The diameter of each wire in the bundle.
//   fillet = The radius that the path corners will be filleted to.
//   wirenum = The first wire's offset into the color table.
//   bezsteps = The corner fillets in the path will be converted into this number of segments.
// Example:
//   wiring([[50,0,-50], [50,50,-50], [0,50,-50], [0,0,-50], [0,0,0]], fillet=10, wires=13);
module wiring(path, wires, wirediam=2, fillet=10, wirenum=0, bezsteps=12) {
	colors = [
		[0.2, 0.2, 0.2], [1.0, 0.2, 0.2], [0.0, 0.8, 0.0], [1.0, 1.0, 0.2],
		[0.3, 0.3, 1.0], [1.0, 1.0, 1.0], [0.7, 0.5, 0.0], [0.5, 0.5, 0.5],
		[0.2, 0.9, 0.9], [0.8, 0.0, 0.8], [0.0, 0.6, 0.6], [1.0, 0.7, 0.7],
		[1.0, 0.5, 1.0], [0.5, 0.6, 0.0], [1.0, 0.7, 0.0], [0.7, 1.0, 0.5],
		[0.6, 0.6, 1.0],
	];
	offsets = hex_offsets(wires, wirediam);
	bezpath = fillet_path(path, fillet);
	poly = simplify3d_path(path3d(bezier_polyline(bezpath, bezsteps)));
	n = max(segs(wirediam), 8);
	r = wirediam/2;
	for (i = [0:wires-1]) {
		extpath = [for (j = [0:n-1]) let(a=j*360/n) [r*cos(a)+offsets[i][0], r*sin(a)+offsets[i][1]]];
		color(colors[(i+wirenum)%len(colors)]) {
			extrude_2dpath_along_3dpath(extpath, poly);
		}
	}
}



// vim: noexpandtab tabstop=4 shiftwidth=4 softtabstop=4 nowrap
