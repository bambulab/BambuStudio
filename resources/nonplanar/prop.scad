include <BOSL/constants.scad>
use <BOSL/transforms.scad>
use <BOSL/shapes.scad>
use <hcyl.scad>
use <dome.scad>
use <wedge.scad>

$fa = 1;
$fs = 0.2;

//hcyl(half=false);
//translate([0,70,0]) dome();
//translate([0,-30,0]) wedge();

//translate([60,0,0]) import("Simple block.stl");
//translate([-70,30,5]) import("Undulating block.stl");
translate([0,0,-1.2]) import("Prop.stl");