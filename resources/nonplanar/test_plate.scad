include <BOSL/constants.scad>
use <BOSL/transforms.scad>
use <BOSL/shapes.scad>
use <hcyl.scad>
use <dome.scad>
use <wedge.scad>
use <angles_strip.scad>
use <extrusion_strip.scad>

$fa = 1;
$fs = 0.2;

difference() {
    union() {
        zrot(-90) translate([-5,0,1]) {
            translate([160-3-45,0,-1]) {
                translate([-100,-122,0]) angles_strip();
                translate([-66,-124,0]) extrusion_strip();
            }

            translate([-94,0,0]) hcyl(half=false);
            translate([-70,72,0]) dome();

            translate([70,60,0]) {
                translate([10,-50,-1.2]) import("Prop.stl");
            }
            color("lightblue") translate([-40,1,0]) import("Simple block.stl");
            
            translate([-185 - 70,-56 - 70-3,0]) difference() {
                import("Undulating block.stl");
                cuboid([500,500,500], align=-1);
            }
            
            translate([115,95,0]) zrot(-90) import("3DBenchy.stl");
        }
        cuboid([250, 250, 1], align=[0,0,1]);
    }
    
    #translate([-128,-128,0]) cuboid([40, 28+10 , 100], align=[1,1,0]);
}


//if ($preview) {
//    #cuboid([250, 250, 1], align=[0,0,-1]);
//}
