#pragma once

#include "Schema.hpp"

namespace Slic3r {
class MachineObject;
}

namespace Slic3r { namespace GUI { namespace HotendRack {

// b_row_first carries the row order across calls; it only changes while the rack is idle.
HotendRackSchema::format::State BuildState(MachineObject* machine_obj, bool& b_row_first);

}}} // namespace Slic3r::GUI::HotendRack
