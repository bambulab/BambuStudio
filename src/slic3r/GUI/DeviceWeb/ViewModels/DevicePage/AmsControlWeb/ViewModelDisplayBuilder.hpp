#ifndef AMSCONTROLWEB_VIEWMODELDISPLAYBUILDER_HPP
#define AMSCONTROLWEB_VIEWMODELDISPLAYBUILDER_HPP

// Derives the `display` view model from an already-filled `data` plus the
// resolved selection and loaded slot.

#include "Schema.hpp"

#include <string>

namespace Slic3r {
class MachineObject;
}

namespace Slic3r { namespace GUI { namespace AmsControlWebDisplay {

void Build(MachineObject* machine_obj, AmsControlWebSchema::format::State& state);

// The filament manager raises the new-RFID-spool hint as a one-shot event, so
// the badge is tracked here rather than derived from device state. Mirrors the
// classic AMSLib::m_show_new_filament_hint, which lives in the widget.
void NotifyNewRfidFilament(const std::string& ams_id, const std::string& slot_id);
void DismissFilamentMgrHint(const std::string& ams_id, const std::string& slot_id);

}}} // namespace Slic3r::GUI::AmsControlWebDisplay

#endif // AMSCONTROLWEB_VIEWMODELDISPLAYBUILDER_HPP
