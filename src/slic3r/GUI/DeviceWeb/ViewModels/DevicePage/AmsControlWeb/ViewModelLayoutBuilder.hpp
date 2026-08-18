#ifndef AMSCONTROLWEB_VIEWMODELLAYOUTBUILDER_HPP
#define AMSCONTROLWEB_VIEWMODELLAYOUTBUILDER_HPP

// Arranges the units of `data` into the columns and cells that both the preview
// strip and the unit cards are drawn from. Port of the column split in
// AMSControl::CreateAmsSingleNozzle / CreateAmsDoubleNozzle.

#include "Schema.hpp"

#include <string>
#include <vector>

namespace Slic3r {
class MachineObject;
}

namespace Slic3r { namespace GUI { namespace AmsControlWebLayout {

// The machine is what the column split is planned from, starting with its
// extruder count. `active_ams_ids` is the user's open-unit history, most recent
// first; each column picks the first entry it owns so one column switches
// without disturbing the other. `selected_ams_id` is the fallback when the
// history has no say.
AmsControlWebSchema::format::PanelLayout Build(MachineObject*                                  machine_obj,
                                               const AmsControlWebSchema::format::AmsListData& data,
                                               const std::vector<std::string>&                 active_ams_ids,
                                               const std::string&                              selected_ams_id);

}}} // namespace Slic3r::GUI::AmsControlWebLayout

#endif // AMSCONTROLWEB_VIEWMODELLAYOUTBUILDER_HPP
