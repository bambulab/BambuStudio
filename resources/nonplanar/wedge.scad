include <BOSL/constants.scad>;
use <BOSL/transforms.scad>
use <BOSL/shapes.scad>;
$fa = 1;
$fs = 0.4;

module wedge(angle = 5) {
    length = 40;
    b = length;
    c = b / cos(angle);
    a = sqrt(pow(c, 2) - pow(b, 2));

    l = length;
    w = 20;
    base_height = 1;

    up(base_height) right_triangle([l,w,a], align=[0,0,1]);
    cuboid([l,w,base_height], align=1);
}

wedge(angle=5);

//up(30) yrot(45) cuboid([20,20,20]);


//color("red") translate([19.6393,-8.87118,1]) cuboid([1,1,1], align=1);
//color("red") translate([0,0,1.37322+0.5]) cuboid([1,1,1], align=1);