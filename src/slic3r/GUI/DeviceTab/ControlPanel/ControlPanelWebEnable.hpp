#pragma once

#include <cstdlib>
#include <cstring>

#ifndef BBL_ENABLE_CONTROL_PANEL_WEB
#define BBL_ENABLE_CONTROL_PANEL_WEB 1
#endif

namespace Slic3r { namespace GUI {

inline bool control_panel_use_web()
{
    static const bool use_web = [] {
#if BBL_ENABLE_CONTROL_PANEL_WEB
        const char* value = std::getenv("STUDIO_CONTROL_PANEL_WX_VERSION");
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
