#ifndef IFILAMENTPAGEMODEBUILDER_HPP
#define IFILAMENTPAGEMODEBUILDER_HPP

// Per-mode filament_page JSON builder. One concrete subclass per UI mode
// (normal, batch_ams_load, batch_ext_load, batch_unload) lives in its own
// cpp under this directory. The ViewModel dispatches to the builder that
// matches the currently active mode and overwrites the default normal-shape
// `filament_page` block in the BuildState() output.
//
// Builders take the VM as a friend reference so they can reuse expensive
// helpers (BuildSlot, IsFilamentChangeActive, ...) without
// duplicating logic. They MUST NOT mutate VM state.

#include <nlohmann/json.hpp>

namespace Slic3r { class MachineObject; }

namespace Slic3r { namespace GUI {

class DevicePageAmsFilamentHotendVM;

class IFilamentPageModeBuilder {
public:
    virtual ~IFilamentPageModeBuilder() = default;

    // Build the full `state.filament_page` JSON object for this mode.
    // `prebuilt_normal_page` is the default normal-shape filament_page already
    // assembled by BuildState() - builders may reuse parts of it (e.g. the
    // shared filament_change_status) or ignore it entirely.
    virtual nlohmann::json BuildFilamentPage(
        const DevicePageAmsFilamentHotendVM& vm,
        MachineObject*                       machine_obj,
        const nlohmann::json&                prebuilt_normal_page) const = 0;

    // Mode literal as it appears in the JSON payload (values::mode::*).
    virtual const char* ModeName() const = 0;
};

}} // namespace Slic3r::GUI

#endif // IFILAMENTPAGEMODEBUILDER_HPP
