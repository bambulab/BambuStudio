#pragma once

#include <cstdlib>
#include <cstring>

// Compile the Web Select Filament dialog in. Override with -DBBL_ENABLE_FILAMENT_SELECT_WEB=0.
#ifndef BBL_ENABLE_FILAMENT_SELECT_WEB
#define BBL_ENABLE_FILAMENT_SELECT_WEB 1
#endif

namespace Slic3r { namespace GUI {

// The Web dialog is used by default when it is compiled in, unless
// STUDIO_FILAMENT_SELECT_WX_VERSION is set to 1 / true / on (falls back to the
// native wx dialog).
inline bool filament_select_use_web()
{
    static const bool use_web = [] {
#if BBL_ENABLE_FILAMENT_SELECT_WEB
        const char *value = std::getenv("STUDIO_FILAMENT_SELECT_WX_VERSION");
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
