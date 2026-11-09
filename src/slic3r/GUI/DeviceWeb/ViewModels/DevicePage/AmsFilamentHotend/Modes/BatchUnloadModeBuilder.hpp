#ifndef BATCHUNLOADMODEBUILDER_HPP
#define BATCHUNLOADMODEBUILDER_HPP

#include "IFilamentPageModeBuilder.hpp"

namespace Slic3r { namespace GUI {

// Batch unload mode. Triggered by the toolbar "Unload" button. Cards
// eligible for selection are those that currently hold filament
// (line_state == loaded). Real rules ship in P1/P2; this stage of the
// rollout already implements the line_state check so the user can
// validate the scaffold end-to-end.
class BatchUnloadModeBuilder final : public IFilamentPageModeBuilder {
public:
    nlohmann::json BuildFilamentPage(
        const DevicePageAmsFilamentHotendVM& vm,
        MachineObject*                       machine_obj,
        const nlohmann::json&                prebuilt_normal_page) const override;

    const char* ModeName() const override;
};

}} // namespace Slic3r::GUI

#endif // BATCHUNLOADMODEBUILDER_HPP
