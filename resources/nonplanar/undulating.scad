include <../libs/BOSL/constants.scad>
use <../libs/BOSL/transforms.scad>
use <../libs/BOSL/shapes.scad>
use <hcyl.scad>
use <dome.scad>
use <wedge.scad>

$fa = 1;
$fs = 0.2;

//hcyl(half=false);
//translate([0,70,0]) dome();
//translate([0,-30,0]) wedge();

//translate([60,0,0]) import("Simple block.stl");
d = 4;

difference() {
    translate([-186,-56,5-d]) import("Undulating block.stl");
    cuboid([1000,1000,100], align=-1);
}
//translate([110,-70,-1.2]) import("Prop.stl");
//translate([0,-65,0]) import("3DBenchy.stl");