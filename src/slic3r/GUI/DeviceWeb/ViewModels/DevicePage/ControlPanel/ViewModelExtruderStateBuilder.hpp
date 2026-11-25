#pragma once

#include "Schema.hpp"

namespace Slic3r {
class MachineObject;
}

namespace Slic3r { namespace GUI { namespace ControlPanel {

void FillExtruderState(MachineObject* obj, ControlPanelSchema::format::State& state);

}}} // namespace Slic3r::GUI::ControlPanel
