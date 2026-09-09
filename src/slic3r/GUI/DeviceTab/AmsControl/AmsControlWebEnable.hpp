#pragma once

#include "libslic3r_version.h"

// Compile the Web AMS panel. Override with -DBBL_ENABLE_AMS_CONTROL_WEB=0.
#ifndef BBL_ENABLE_AMS_CONTROL_WEB
#define BBL_ENABLE_AMS_CONTROL_WEB 1
#endif

// Show the classic C++ AMS panel. Override with -DBBL_SHOW_AMS_CONTROL_CPP=1.
#ifndef BBL_SHOW_AMS_CONTROL_CPP
#define BBL_SHOW_AMS_CONTROL_CPP (!BBL_ENABLE_AMS_CONTROL_WEB)
#endif

// The "AMS C++ / AMS Web" switch board only makes sense when both are shown.
#define BBL_SHOW_AMS_CONTROL_SWITCH (BBL_ENABLE_AMS_CONTROL_WEB && BBL_SHOW_AMS_CONTROL_CPP)
