# BOSL
The Belfry OpenScad Library - A library of tools, shapes, and helpers to make OpenScad easier to use.

This library is a set of useful tools, shapes and manipulators that I developed while working on various
projects, including large ones like the Snappy-Reprap printed 3D printer.


## Installation
1. Download the .zip or .tar.gz release file for this library.
2. Unpack it.  It should create a `BOSL-v1.0` directory with the library files within it.
3. Rename the directory to `BOSL`.
4. Move the `BOSL` directory into the apropriate OpenSCAD library directory for your platform:
    - Windows: `My Documents\OpenSCAD\libraries\`
    - Linux: `$HOME/.local/share/OpenSCAD/libraries/`
    - Mac OS X: `$HOME/Documents/OpenSCAD/libraries/`
5. Restart OpenSCAD.


## Terminology
For purposes of the BOSL library, the following terms apply:
- **Left**: Towards X-
- **Right**: Towards X+
- **Front**/**Forward**: Towards Y-
- **Back**/**Behind**: Towards Y+
- **Bottom**/**Down**/**Below**: Towards Z-
- **Top**/**Up**/**Above**: Towards Z+

- **Axis-Positive**: Towards the positive end of the axis the object is oriented on.  IE: X+, Y+, or Z+.
- **Axis-Negative**: Towards the negative end of the axis the object is oriented on.  IE: X-, Y-, or Z-.

## Common Arguments:

Args    | What it is
------- | ----------------------------------------
fillet  | Radius of rounding for interior or exterior edges.
chamfer | Size of chamfers/bevels for interior or exterior edges.
orient  | Axis a part should be oriented along.  Given as an XYZ triplet of rotation angles.  It is recommended that you use the `ORIENT_` constants from `constants.scad`.  Default is usually `ORIENT_Z` for vertical orientation.
align   | Side of the origin that the part should be on.  Given as a vector away from the origin.  It is recommended that you use the `V_` constants from `constants.scad`.  Default is usually `V_ZERO` for centered.


## Examples
A lot of the features of this library are to allow shorter, easier-to-read, intent-based coding.  For example:

[`BOSL/transforms.scad`](https://github.com/revarbat/BOSL/wiki/transforms.scad) Examples | Raw OpenSCAD Equivalent
------------------------------- | -------------------------------
`up(5)`                         | `translate([0,0,5])`
`xrot(30,cp=[0,10,20])`         | `translate([0,10,20]) rotate([30,0,0]) translate([0,-10,-20])`
`xspread(20,n=3)`               | `for (dx=[-20,0,20]) translate([dx,0,0])`
`zring(n=6,r=20)`               | `for (zr=[0:5]) rotate([0,0,zr*60]) translate([20,0,0])`
`skew_xy(xa=30,ya=45)`          | `multmatrix([[1,0,tan(30),0],[0,1,tan(45),0],[0,0,1,0],[0,0,0,1]])`

[`BOSL/shapes.scad`](https://github.com/revarbat/BOSL/wiki/shapes.scad) Examples | Raw OpenSCAD Equivalent
---------------------------------- | -------------------------------
`upcube([10,20,30]);`              | `translate([0,0,15]) cube([10,20,30], center=true);`
`cuboid([20,20,30], fillet=5, edges=EDGES_Z_ALL);` | `minkowski() {cube([10,10,20], center=true); sphere(r=5, $fn=32);}`
`prismoid([30,40],[20,30],h=10);`  | `hull() {translate([0,0,0.005]) cube([30,40,0.01], center=true); translate([0,0,9.995]) cube([20,30,0.01],center=true);}`
`xcyl(l=20,d=4);`                  | `rotate([0,90,0]) cylinder(h=20, d=4, center=true);`
`cyl(l=100, d=40, fillet=5);`      | `translate([0,0,50]) minkowski() {cylinder(h=90, d=30, center=true); sphere(r=5);}`

[`BOSL/masks.scad`](https://github.com/revarbat/BOSL/wiki/masks.scad) Examples | Raw Openscad Equivalent
----------------------------------- | -------------------------------
`chamfer_mask_z(l=20,chamfer=5);`   | `rotate(45) cube([5*sqrt(2), 5*sqrt(2), 20], center=true);`
`fillet_mask_z(l=20,fillet=5);`     | `difference() {cube([10,10,20], center=true); for(dx=[-5,5],dy=[-5,5]) translate([dx,dy,0]) cylinder(h=20.1, r=5, center=true);}`
`fillet_hole_mask(r=30,fillet=5);`  | `difference() {cube([70,70,10], center=true); translate([0,0,-5]) rotate_extrude(convexity=4) translate([30,0,0]) circle(r=5);}`


## The Library Files
The library files are as follows:

### Commonly Used
  - [`transforms.scad`](https://github.com/revarbat/BOSL/wiki/transforms.scad): The most commonly used transformations, manipulations, and shortcuts are in this file.
  - [`shapes.scad`](https://github.com/revarbat/BOSL/wiki/shapes.scad): Common useful shapes and structured objects.
  - [`masks.scad`](https://github.com/revarbat/BOSL/wiki/masks.scad): Shapes that are useful for masking with `difference()` and `intersect()`.
  - [`threading.scad`](https://github.com/revarbat/BOSL/wiki/threading.scad): Modules to make triangular and trapezoidal threaded rods and nuts.
  - [`paths.scad`](https://github.com/revarbat/BOSL/wiki/paths.scad): Functions and modules to work with arbitrary 3D paths.
  - [`beziers.scad`](https://github.com/revarbat/BOSL/wiki/beziers.scad): Functions and modules to work with bezier curves.

### Standard Parts
  - [`involute_gears.scad`](https://github.com/revarbat/BOSL/wiki/involute_gears.scad): Modules and functions to make involute gears and racks.
  - [`joiners.scad`](https://github.com/revarbat/BOSL/wiki/joiners.scad): Modules to make joiner shapes for connecting separately printed objects.
  - [`sliders.scad`](https://github.com/revarbat/BOSL/wiki/sliders.scad): Modules for creating simple sliders and rails.
  - [`metric_screws.scad`](https://github.com/revarbat/BOSL/wiki/metric_screws.scad): Functions and modules to make metric screws, nuts, and screwholes.
  - [`linear_bearings.scad`](https://github.com/revarbat/BOSL/wiki/linear_bearings.scad): Modules to make mounts for LMxUU style linear bearings.
  - [`nema_steppers.scad`](https://github.com/revarbat/BOSL/wiki/nema_steppers.scad): Modules to make mounting holes for NEMA motors.
  - [`phillips_drive.scad`](https://github.com/revarbat/BOSL/wiki/phillips_drive.scad): Modules to create Phillips screwdriver tips.
  - [`torx_drive.scad`](https://github.com/revarbat/BOSL/wiki/torx_drive.scad): Functions and Modules to create Torx bit drive holes.
  - [`wiring.scad`](https://github.com/revarbat/BOSL/wiki/wiring.scad): Modules to render routed bundles of wires.

### Miscellaneous
  - [`math.scad`](https://github.com/revarbat/BOSL/wiki/math.scad): Useful helper functions.
  - [`constants.scad`](https://github.com/revarbat/BOSL/wiki/constants.scad): Useful constants for vectors, edges, etc.
  - [`quaternions.scad`](https://github.com/revarbat/BOSL/wiki/quaternions.scad): Functions to work with quaternion rotations.
  - [`debug.scad`](https://github.com/revarbat/BOSL/wiki/debug.scad): Modules to help debug creation of beziers, `polygons()`s and `polyhedron()`s

## Documentation
The full library docs can be found at https://github.com/revarbat/BOSL/wiki

