#ifndef slic3r_GLCanvasType_hpp_
#define slic3r_GLCanvasType_hpp_

// Lightweight canvas type / timestamp helpers extracted from GLCanvas3D.hpp
// so hub headers (e.g. NotificationManager) do not need the full GLCanvas3D definition.

#include <cstdint>

#include <wx/time.h>

namespace Slic3r {
namespace GUI {

// Keep as unscoped enum so existing enumerators stay usable as
// Slic3r::GUI::CanvasView3D / ECanvasType::CanvasView3D.
enum ECanvasType
{
    CanvasView3D      = 0,
    CanvasPreview     = 1,
    CanvasAssembleView = 2,
};

// Shared timestamp helper (was GLCanvas3D::timestamp_now).
inline int64_t canvas_timestamp_now()
{
#ifdef _WIN32
    // Cheaper on Windows, calls GetSystemTimeAsFileTime()
    return wxGetUTCTimeMillis().GetValue();
#else
    // calls clock()
    return wxGetLocalTimeMillis().GetValue();
#endif
}

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLCanvasType_hpp_
