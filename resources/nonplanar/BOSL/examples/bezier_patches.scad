include <BOSL/constants.scad>
use <BOSL/transforms.scad>
use <BOSL/beziers.scad>
use <BOSL/math.scad>


function CR_corner(size, orient=[0,0,0], trans=[0,0,0]) =
	let (
		r = 0.4,
		k = r/2,
		// I know this patch is not yet correct for continuous
		// rounding, but it's a first approximation proof of concept.
		// Currently this is a degree 4 triangular patch.
		patch = [
			[[0,1,1], [0,r,1], [0,0,1], [r,0,1], [1,0,1]],
			[[0,1,r], [0,k,k], [k,0,k], [1,0,r]],
			[[0,1,0], [k,k,0], [1,0,0]],
			[[r,1,0], [1,r,0]],
			[[1,1,0]]
		]
	) [for (row=patch)
		translate_points(v=trans,
			rotate_points3d(v=orient,
				scale_points(v=size, row)
			)
		)
	];


function CR_edge(size, orient=[0,0,0], trans=[0,0,0]) =
	let (
		r = 0.4,
		a = -1/2,
		b = -1/4,
		c =  1/4,
		d =  1/2,
		// I know this patch is not yet correct for continuous
		// rounding, but it's a first approximation proof of concept.
		// Currently this is a degree 4 rectangular patch.
		patch = [
			[[1,0,a], [1,0,b], [1,0,0], [1,0,c], [1,0,d]],
			[[r,0,a], [r,0,b], [r,0,0], [r,0,c], [r,0,d]],
			[[0,0,a], [0,0,b], [0,0,0], [0,0,c], [0,0,d]],
			[[0,r,a], [0,r,b], [0,r,0], [0,r,c], [0,r,d]],
			[[0,1,a], [0,1,b], [0,1,0], [0,1,c], [0,1,d]]
		]
	) [for (row=patch)
		translate_points(v=trans,
			rotate_points3d(v=orient,
				scale_points(v=size, row)
			)
		)
	];


module CR_cube(size=[100,100,100], r=10, splinesteps=8, cheat=false)
{
	s = size-2*[r,r,r];
	h = size/2;
	corners = [
		CR_corner([r,r,r], orient=ORIENT_Z,     trans=[-size.x/2, -size.y/2, -size.z/2]),
		CR_corner([r,r,r], orient=ORIENT_Z_90,  trans=[ size.x/2, -size.y/2, -size.z/2]),
		CR_corner([r,r,r], orient=ORIENT_Z_180, trans=[ size.x/2,  size.y/2, -size.z/2]),
		CR_corner([r,r,r], orient=ORIENT_Z_270, trans=[-size.x/2,  size.y/2, -size.z/2]),

		CR_corner([r,r,r], orient=ORIENT_ZNEG,     trans=[ size.x/2, -size.y/2,  size.z/2]),
		CR_corner([r,r,r], orient=ORIENT_ZNEG_90,  trans=[-size.x/2, -size.y/2,  size.z/2]),
		CR_corner([r,r,r], orient=ORIENT_ZNEG_180, trans=[-size.x/2,  size.y/2,  size.z/2]),
		CR_corner([r,r,r], orient=ORIENT_ZNEG_270, trans=[ size.x/2,  size.y/2,  size.z/2])
	];
	edges = [
		CR_edge([r, r, s.x], orient=ORIENT_X,     trans=[   0, -h.y, -h.z]),
		CR_edge([r, r, s.x], orient=ORIENT_X_90,  trans=[   0,  h.y, -h.z]),
		CR_edge([r, r, s.x], orient=ORIENT_X_180, trans=[   0,  h.y,  h.z]),
		CR_edge([r, r, s.x], orient=ORIENT_X_270, trans=[   0, -h.y,  h.z]),

		CR_edge([r, r, s.y], orient=ORIENT_Y,     trans=[ h.x,    0, -h.z]),
		CR_edge([r, r, s.y], orient=ORIENT_Y_90,  trans=[-h.x,    0, -h.z]),
		CR_edge([r, r, s.y], orient=ORIENT_Y_180, trans=[-h.x,    0,  h.z]),
		CR_edge([r, r, s.y], orient=ORIENT_Y_270, trans=[ h.x,    0,  h.z]),

		CR_edge([r, r, s.z], orient=ORIENT_Z,     trans=[-h.x, -h.y,    0]),
		CR_edge([r, r, s.z], orient=ORIENT_Z_90,  trans=[ h.x, -h.y,    0]),
		CR_edge([r, r, s.z], orient=ORIENT_Z_180, trans=[ h.x,  h.y,    0]),
		CR_edge([r, r, s.z], orient=ORIENT_Z_270, trans=[-h.x,  h.y,    0])
	];
	faces = [
		// Yes, these are degree 1 bezier patches.  That means just the four corner points.
		// Since these are flat, it doesn't matter what degree they are, and this will reduce calculation overhead.
		bezier_patch_flat([s.y, s.z], N=1, orient=ORIENT_X,    trans=[ h.x,    0,    0]),
		bezier_patch_flat([s.y, s.z], N=1, orient=ORIENT_XNEG, trans=[-h.x,    0,    0]),

		bezier_patch_flat([s.x, s.z], N=1, orient=ORIENT_Y,    trans=[   0,  h.y,    0]),
		bezier_patch_flat([s.x, s.z], N=1, orient=ORIENT_YNEG, trans=[   0, -h.y,    0]),

		bezier_patch_flat([s.x, s.y], N=1, orient=ORIENT_Z,    trans=[   0,    0,  h.z]),
		bezier_patch_flat([s.x, s.y], N=1, orient=ORIENT_ZNEG, trans=[   0,    0, -h.z])
	];
	// Generating all the patches above took about 0.05 secs.

	if (cheat) {
		// Hulling just the corners takes less than a second.
		hull() bezier_polyhedron(tris=corners, splinesteps=splinesteps);
	} else {
		// Generating the polyhedron fully from bezier patches takes 3 seconds on my laptop.
		bezier_polyhedron(patches=concat(edges, faces), tris=corners, splinesteps=splinesteps);
	}
}


CR_cube(size=[100,100,100], r=20, splinesteps=9, cheat=false);
cube(1);



// vim: noexpandtab tabstop=4 shiftwidth=4 softtabstop=4 nowrap
