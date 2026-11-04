#pragma once

#include "Schema.hpp"

namespace Slic3r {
class MachineObject;
}

namespace Slic3r { namespace GUI { namespace FilamentLoadStateBuilder {

FilamentLoadSchema::format::State BuildState(MachineObject* machine_obj, bool expanded);

}}} // namespace Slic3r::GUI::FilamentLoadStateBuilder
