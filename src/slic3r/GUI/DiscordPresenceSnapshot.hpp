#ifndef slic3r_GUI_DiscordPresenceSnapshot_hpp_
#define slic3r_GUI_DiscordPresenceSnapshot_hpp_

#include "slic3r/Utils/DiscordPresence.hpp"

namespace Slic3r {
namespace GUI {

// Read the current application and printer state and describe it as a snapshot
// ready to publish. Must be called on the GUI thread; safe to call before the
// Plater exists (it reports Idle).
//
// hide_names honours the privacy preference: progress and state are kept, but
// project and print job names are replaced with generic wording.
PresenceSnapshot collect_presence_snapshot(bool hide_names);

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_DiscordPresenceSnapshot_hpp_
