#pragma once

#include <admesh/stl.h>

#include <functional>
#include <string>

namespace Slic3r {

using Win10RepairProgressFn = std::function<void(const char* message, unsigned progress)>;
using Win10RepairCancelFn   = std::function<bool()>;

#ifdef HAS_WIN10SDK

bool is_win10_model_repair_available();

bool fix_mesh_by_win10_sdk(const indexed_triangle_set& mesh,
                           indexed_triangle_set&       repaired_mesh,
                           Win10RepairProgressFn       progress_callback = {},
                           Win10RepairCancelFn         cancel_callback = {},
                           std::string*                error_message = nullptr);

#else

inline bool is_win10_model_repair_available()
{
    return false;
}

inline bool fix_mesh_by_win10_sdk(const indexed_triangle_set&,
                                  indexed_triangle_set&,
                                  Win10RepairProgressFn = {},
                                  Win10RepairCancelFn = {},
                                  std::string* error_message = nullptr)
{
    if (error_message)
        *error_message = "Windows 3D repair service is not available in this build.";
    return false;
}

#endif

} // namespace Slic3r
