#pragma once

#include "Schema.hpp"

namespace Slic3r {
class MachineObject;
}

namespace Slic3r { namespace GUI { namespace ControlPanel {

ControlPanelSchema::format::State BuildState(MachineObject* machine_obj);

}}} // namespace Slic3r::GUI::ControlPanel
