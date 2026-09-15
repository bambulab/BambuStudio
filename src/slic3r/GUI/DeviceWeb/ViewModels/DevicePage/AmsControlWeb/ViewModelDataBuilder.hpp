#ifndef AMSCONTROLWEB_VIEWMODELDATABUILDER_HPP
#define AMSCONTROLWEB_VIEWMODELDATABUILDER_HPP

// Fills the `data` half of the payload and offers the lookups the rest of the
// module needs over it.

#include "SchemaData.hpp"

#include <string>

namespace Slic3r {
class MachineObject;
}

namespace Slic3r { namespace GUI { namespace AmsControlWebData {

struct LoadedSlot
{
    std::string ams_id;
    std::string slot_id;
};

// Mirrors the device source into `data`. The first loaded slot falls out of the
// same pass over the trays and is used as a selection fallback; dual-nozzle
// printers can have more than one, so display code must not treat it as unique.
LoadedSlot Build(MachineObject* machine_obj, AmsControlWebSchema::format::AmsListData& data);

// An extruder's slot_now points at this slot: the machine routed it out of the
// slot into its AMS hub, whether or not the filament arrived at the far end yet.
bool IsSlotRoutedToHub(MachineObject* machine_obj, const std::string& ams_id, const std::string& slot_id);

// slot_now plus filament in that extruder's throat: the filament made it all the
// way through. Dual-nozzle machines can answer true for two slots.
bool IsSlotLoaded(MachineObject* machine_obj, const std::string& ams_id, const std::string& slot_id);

const AmsControlWebSchema::format::Tray* FindTray(const AmsControlWebSchema::format::AmsListData& data,
                                                 const std::string& ams_id,
                                                 const std::string& slot_id);

}}} // namespace Slic3r::GUI::AmsControlWebData

#endif // AMSCONTROLWEB_VIEWMODELDATABUILDER_HPP
