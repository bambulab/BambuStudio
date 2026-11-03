#pragma once

#include "Schema.hpp"

namespace Slic3r {
class MachineObject;
}

namespace Slic3r { namespace GUI { namespace HotendRack {

HotendRackSchema::format::State BuildState(MachineObject* machine_obj);

}}} // namespace Slic3r::GUI::HotendRack
