include <BOSL/constants.scad>;
use <BOSL/transforms.scad>
use <BOSL/shapes.scad>;
$fa = 1;
$fs = 0.4;

width = 14;

module extrusion_block(h) {
    color("Coral") cuboid([width, width, 1+h], align=1);
    
    msg = str(h);
    color("LightBlue") translate([12,0,1]) linear_extrude(1) zrot(90) text(msg, size=4, halign="center");
}

module make_extrusions(start, diff, count) {
    translate([0,width/2,0]) 
    for (i = [0:count-1]) {
        h = start + diff*i;
        echo("height", h);
        translate([0, (width+1.6)*i, 0]) {
            intersection() {
                extrusion_block(h);
            }
        }
    }
}

module extrusion_strip() {
    make_extrusions(0.05, 0.01, 16);
    translate([-7,0,0]) cuboid([24, (width+1.6)*15+14, 1], align=[1,1,1]);
}

zrot(-90)
extrusion_strip();

if ($preview) {
    #cuboid([250, 250, 1], align=[0,1,-1]);
}
