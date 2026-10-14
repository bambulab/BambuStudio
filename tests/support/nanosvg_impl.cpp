// libslic3r (NSVGUtils) calls into nanosvg, but its implementation is only compiled into the GUI
// library (src/slic3r/GUI/BitmapCache.cpp). Test executables link libslic3r without the GUI, so
// they compile it here.
#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"
