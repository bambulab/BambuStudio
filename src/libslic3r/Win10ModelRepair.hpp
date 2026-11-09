#pragma once

#include <admesh/stl.h>

#include <functional>
#include <string>

namespace Slic3r {

using Win10RepairProgressFn = std::function<void(const char* message, unsigned progress)>;
using Win10RepairCancelFn   = std::function<bool()>;

// Builds without the Windows 10 SDK report the service as unavailable and never repair.
bool is_win10_model_repair_available();

bool fix_mesh_by_win10_sdk(const indexed_triangle_set& mesh,
                           indexed_triangle_set&       repaired_mesh,
                           Win10RepairProgressFn       progress_callback = {},
                           Win10RepairCancelFn         cancel_callback = {},
                           std::string*                error_message = nullptr);

} // namespace Slic3r
