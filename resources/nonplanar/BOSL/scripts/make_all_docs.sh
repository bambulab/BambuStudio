#!/bin/bash

if [[ $# > 0 ]]; then
    PREVIEW_LIBS="$@"
else
    PREVIEW_LIBS="constants compat transforms shapes masks paths beziers math metric_screws threading involute_gears sliders joiners linear_bearings nema_steppers wiring triangulation quaternions phillips_drive torx_drive debug"
fi

dir="$(basename $PWD)"
if [ "$dir" = "BOSL" ]; then
    cd BOSL.wiki
elif [ "$dir" != "BOSL.wiki" ]; then
    echo "Must run this script from the BOSL or BOSL/BOSL.wiki directories."
    exit 1
fi

rm -f tmpscad*.scad
for lib in $PREVIEW_LIBS; do
    lib="$(basename $lib .scad)"
    mkdir -p images/$lib
    rm -f images/$lib/*.png images/$lib/*.gif
    echo ../scripts/docs_gen.py ../$lib.scad -o $lib.scad.md -c -i -I images/$lib/
    ../scripts/docs_gen.py ../$lib.scad -o $lib.scad.md -c -i -I images/$lib/ || exit 1
    open -a Typora $lib.scad.md
done


