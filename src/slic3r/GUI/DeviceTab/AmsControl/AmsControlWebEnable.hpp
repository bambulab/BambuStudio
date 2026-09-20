#pragma once

#include "libslic3r_version.h"

#include <cstdlib>
#include <cstring>

// Compile the Web AMS panel in. Override with -DBBL_ENABLE_AMS_CONTROL_WEB=0.
#ifndef BBL_ENABLE_AMS_CONTROL_WEB
#define BBL_ENABLE_AMS_CONTROL_WEB 1
#endif

namespace Slic3r { namespace GUI {

// The Web panel is shown by default when it is compiled in.
// unless STUDIO_AMS_CONTROL_WX_VERSION is set.
inline bool ams_control_use_web()
{
    static const bool use_web = [] {
#if BBL_ENABLE_AMS_CONTROL_WEB
        const char *value = std::getenv("STUDIO_AMS_CONTROL_WX_VERSION");
        if (value == nullptr || value[0] == '\0')
            return true;
        return std::strcmp(value, "0") == 0 || std::strcmp(value, "false") == 0 || std::strcmp(value, "off") == 0;
#else
        return false;
#endif
    }();
    return use_web;
}

}} // namespace Slic3r::GUI
