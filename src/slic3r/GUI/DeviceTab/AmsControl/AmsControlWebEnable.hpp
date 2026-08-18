#pragma once

#include "libslic3r_version.h"

// Dual-run switch for AmsControlWeb. Flip independently of BBL_RELEASE_TO_PUBLIC.
#ifndef BBL_ENABLE_AMS_CONTROL_WEB
#if !BBL_RELEASE_TO_PUBLIC
#define BBL_ENABLE_AMS_CONTROL_WEB 1
#else
#define BBL_ENABLE_AMS_CONTROL_WEB 0
#endif
#endif
