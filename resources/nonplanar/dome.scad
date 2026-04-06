include <BOSL/constants.scad>
use <BOSL/transforms.scad>
use <BOSL/shapes.scad>

$fa = 1;
$fs = 0.2;


module dome(r=50, height=50) {
    down(r-height) difference() {
        sphere(r=r);
        up(r-height) cuboid([r*2, r*2, r*3], align=[0,0,-1]);
    }   
}

dome(r=50, height=50);