#ifndef BATCHAMSLOADMODEBUILDER_HPP
#define BATCHAMSLOADMODEBUILDER_HPP

#include "IFilamentPageModeBuilder.hpp"

namespace Slic3r { namespace GUI {

// Batch AMS auto-load mode builder. Triggered by clicking the toolbar
// "Auto Load" button on the Filament tab. Cards eligible for selection:
//   * the slot comes directly from the AMS tray list, not from hotend-bound
//     normal-page slots
//   * the slot currently holds filament or unknown filament info
//     (ReadyToLoad / Unknown qualify; Empty / Reading / Error are rejected)
// Ineligible slots receive a localized `reason` string that the Web shows
// as a tooltip when the user hovers a greyed-out card.
class BatchAmsLoadModeBuilder final : public IFilamentPageModeBuilder {
public:
    nlohmann::json BuildFilamentPage(
        const DevicePageAmsFilamentHotendVM& vm,
        MachineObject*                       machine_obj,
        const nlohmann::json&                prebuilt_normal_page) const override;

    const char* ModeName() const override;
};

}} // namespace Slic3r::GUI

#endif // BATCHAMSLOADMODEBUILDER_HPP
