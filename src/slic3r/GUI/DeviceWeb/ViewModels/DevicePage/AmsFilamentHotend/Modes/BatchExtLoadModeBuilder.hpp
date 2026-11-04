#ifndef BATCHEXTLOADMODEBUILDER_HPP
#define BATCHEXTLOADMODEBUILDER_HPP

#include "IFilamentPageModeBuilder.hpp"

namespace Slic3r { namespace GUI {

// Batch external manual load mode. Triggered by the toolbar
// "Manual Loading" button. Real selectability rules ship in P1; this
// stage of the rollout only provides the scaffolding so the Web can switch
// to the mode and see its checkboxes rendered.
class BatchExtLoadModeBuilder final : public IFilamentPageModeBuilder {
public:
    nlohmann::json BuildFilamentPage(
        const DevicePageAmsFilamentHotendVM& vm,
        MachineObject*                       machine_obj,
        const nlohmann::json&                prebuilt_normal_page) const override;

    const char* ModeName() const override;
};

}} // namespace Slic3r::GUI

#endif // BATCHEXTLOADMODEBUILDER_HPP
