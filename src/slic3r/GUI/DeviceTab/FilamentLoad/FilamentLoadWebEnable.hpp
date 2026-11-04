#pragma once

#include <cstdlib>
#include <cstring>

#ifndef BBL_ENABLE_FILAMENT_LOAD_WEB
#define BBL_ENABLE_FILAMENT_LOAD_WEB 1
#endif

namespace Slic3r { namespace GUI {

inline bool filament_load_use_web()
{
    static const bool use_web = [] {
#if BBL_ENABLE_FILAMENT_LOAD_WEB
        const char *value = std::getenv("STUDIO_FILAMENT_LOAD_WX_VERSION");
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
