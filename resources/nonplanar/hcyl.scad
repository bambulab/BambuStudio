include <BOSL/constants.scad>
use <BOSL/transforms.scad>
use <BOSL/shapes.scad>

$fa = 1;
$fs = 0.2;


r = 25;
height = 20;

module hcyl(decrease=0, half=true) {

    difference() {
        down(decrease) cyl(r=r, h=height, orient=ORIENT_Y);
        if (half) cuboid([500,500,500], align=[-1,0,0]);
        cuboid([500,500,500], align=[0,0,-1]);
    }
}

hcyl(18);

//down(r-height) difference() {
//    sphere(r=r);
//    up(r-height) cuboid([r*2, r*2, r*3], align=[0,0,-1]);
//}   
