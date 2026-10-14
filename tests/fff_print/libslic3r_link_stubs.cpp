// libslic3r leaks a few link-time dependencies onto code that only the full
// application normally provides:
//   - NSVGUtils.cpp -> the nanosvg *implementation* (compiled in libslic3r_gui
//     via BitmapCache.cpp). Provide it here.
//
// Slic3r::Http and Slic3r::BBL_Encrypt (used by LogSink.cpp) are pulled in by
// compiling the real src/slic3r/Utils/{Http,BBLUtil}.cpp into the test target
// (see CMakeLists.txt) -- they only need libcurl / OpenSSL.

#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"
