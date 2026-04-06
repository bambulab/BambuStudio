use <BOSL/transforms.scad>
use <BOSL/math.scad>
include <BOSL/constants.scad>

// Shows all the orientations on cubes in their correct rotations.

orientations = [
	ORIENT_X,        ORIENT_Y,        ORIENT_Z,
	ORIENT_XNEG,     ORIENT_YNEG,     ORIENT_ZNEG,

	ORIENT_X_90,     ORIENT_Y_90,     ORIENT_Z_90,
	ORIENT_XNEG_90,  ORIENT_YNEG_90,  ORIENT_ZNEG_90,

	ORIENT_X_180,    ORIENT_Y_180,    ORIENT_Z_180,
	ORIENT_XNEG_180, ORIENT_YNEG_180, ORIENT_ZNEG_180,

	ORIENT_X_270,    ORIENT_Y_270,    ORIENT_Z_270,
	ORIENT_XNEG_270, ORIENT_YNEG_270, ORIENT_ZNEG_270
];


axisdiam = 0.5;
axislen = 12;
axislbllen = 15;
axiscolors = ["red", "forestgreen", "dodgerblue"];

module text3d(text, h=0.01, size=3) {
	linear_extrude(height=h, convexity=10) {
		text(text=text, size=size, valign="center", halign="center");
	}
}

module dottedline(l, d) for(y = [0:d*3:l]) up(y) sphere(d=d);

module orient_cubes() {
	color(axiscolors[0]) {
		yrot( 90) cylinder(h=axislen, d=axisdiam, center=false);
		right(axislbllen) rot([90,0,0]) text3d(text="X+");
		yrot(-90) dottedline(l=axislen, d=axisdiam);
		left(axislbllen) rot([90,0,180]) text3d(text="X-");
	}
	color(axiscolors[1]) {
		xrot(-90) cylinder(h=axislen, d=axisdiam, center=false);
		back(axislbllen) rot([90,0,90]) text3d(text="Y+");
		xrot( 90) dottedline(l=axislen, d=axisdiam);
		fwd(axislbllen) rot([90,0,-90]) text3d(text="Y-");
	}
	color(axiscolors[2])  {
		cylinder(h=axislen, d=axisdiam, center=false);
		up(axislbllen) rot([0,-90,90+$vpr[2]]) text3d(text="Z+");
		xrot(180) dottedline(l=axislen, d=axisdiam);
		down(axislbllen) rot([0,90,-90+$vpr[2]]) text3d(text="Z-");
	}


	for (ang = [0:90:270]) {
		translate(cylindrical_to_xyz(40, ang+90, 0)) {
			color("lightgray") cube(20, center=true);
		}
	}

	for (axis=[0:2], neg=[0:1], ang = [0:90:270]) {
		idx = axis + 3*neg + 6*ang/90;
		translate(cylindrical_to_xyz(40, ang+90, 0)) {
			rotate(orientations[idx]) {
				up(10) {
					ydistribute(8) {
						color("black") text3d(text=str(ang, "º"), size=5);
						color(axiscolors[axis]) text3d(text=str(["X","Y","Z"][axis], ["+","-"][neg]), size=5);
					}
				}
			}
		}
	}
}


//rotate(a=180, v=[1,1,0])
orient_cubes();



// vim: noexpandtab tabstop=4 shiftwidth=4 softtabstop=4 nowrap
