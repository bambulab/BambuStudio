include <BOSL/constants.scad>;
use <BOSL/transforms.scad>
use <BOSL/shapes.scad>;
$fa = 1;
$fs = 0.4;

module wedge(angle=5) {
    length = 20;
    b = 20;
    c = 20 / cos(angle);
    a = sqrt(pow(c, 2) - pow(b, 2));

    l = 40;
    w = 20;
    base_height = 1;

    up(base_height) right_triangle([l,w,a], align=[0,0,1]);
    cuboid([l,w,base_height], align=1);
    
    msg = str(angle, "°");
    color("LightBlue") translate([25,0,1]) linear_extrude(1) zrot(90) text(msg, size=4, halign="center");
}

width = 20;
angles = [5, 7.5, 10, 12.5, 15, 17.5, 20, 22.5, 25, 30, 35, 45];

module make_angles(start, diff, count) {
    translate([0,width/2,0]) 
    for (i = [0:count-1]) {
        angle = start + diff*i;
        echo("angle", angle);
        translate([0, (width+5)*i, 0]) {
            intersection() {
                wedge(angle=angle);
                cuboid([100,100,10], align=1);
            }
        }
    }
}

module angles_strip() {
    make_angles(5, 5, 10);
    translate([10,0,0]) cuboid([47, 10*20 + 9*5, 1], align=[0,1,1]);
}

rotate([0,0,-90]) {
    angles_strip();
    translate([10,0,0]) cuboid([47+12, 10*20 + 9*5, 1], align=[0,1,1]);
}
//color("lightblue") translate([37,50,0]) linear_extrude(2) zrot(90) text("TEXT", size=6, halign="center");

//if ($preview) {
//    #cuboid([250, 250, 1], align=[0,1,-1]);
//}
