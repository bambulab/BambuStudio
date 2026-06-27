#!/bin/sh
#
# make resource files for each platforms
#

_VERSION2=$(grep VERSION include/hpdf_consts.h | awk '{print $3}' | sed 's/"//g')".0"
_VERSION1=$(grep VERSION include/hpdf_consts.h | awk '{print $3}' | sed 's/"//g;s/\./,/g')",0"
_COMPILER=""

which windres
if [ $? -ne 0 ]; then
	echo "windres does not exist."
	exit 1
fi

echo "create .res file for haru$_VERSION1"

# for mingw

_COMPILER="MinGW"
cat win32/libhpdf.rc.template | sed "s/_VERSION1/$_VERSION1/g;s/_VERSION2/$_VERSION2/g;s/_COMPILER/$_COMPILER/g" > win32/mingw/libhpdf_mingw.rc

windres -O coff win32/mingw/libhpdf_mingw.rc win32/mingw/libhpdf_mingw.res

# for cygwin

_COMPILER="cygwin"
cat win32/libhpdf.rc.template | sed "s/_VERSION1/$_VERSION1/g;s/_VERSION2/$_VERSION2/g;s/_COMPILER/$_COMPILER/g" > win32/mingw/libhpdf_cygwin.rc

windres -O coff win32/mingw/libhpdf_cygwin.rc win32/mingw/libhpdf_cygwin.res


# for bcc32

_COMPILER="bcc32"
cat win32/libhpdf.rc.template | sed "s/_VERSION1/$_VERSION1/g;s/_VERSION2/$_VERSION2/g;s/_COMPILER/$_COMPILER/g" > win32/bcc32/libhpdf.rc

windres win32/bcc32/libhpdf.rc win32/bcc32/libhpdf.res

# for msvc

_COMPILER="msvc"
cat win32/libhpdf.rc.template | sed "s/_VERSION1/$_VERSION1/g;s/_VERSION2/$_VERSION2/g;s/_COMPILER/$_COMPILER/g" > win32/msvc/libhpdf.rc

windres win32/msvc/libhpdf.rc win32/msvc/libhpdf.res

