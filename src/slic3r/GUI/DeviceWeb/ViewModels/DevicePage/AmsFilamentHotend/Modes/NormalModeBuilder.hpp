#ifndef NORMALMODEBUILDER_HPP
#define NORMALMODEBUILDER_HPP

#include "IFilamentPageModeBuilder.hpp"

namespace Slic3r { namespace GUI {

// Default mode builder - returns the prebuilt normal-shape filament_page
// unchanged (just stamps `mode = "normal"`). The heavy lifting still lives in
// DevicePageAmsFilamentHotendVM::BuildState() until a future refactor moves
// the normal-shape assembly here too; that change is intentionally deferred
// to avoid coupling the mode-framework introduction with a large code move.
class NormalModeBuilder final : public IFilamentPageModeBuilder {
public:
    nlohmann::json BuildFilamentPage(
        const DevicePageAmsFilamentHotendVM& vm,
        MachineObject*                       machine_obj,
        const nlohmann::json&                prebuilt_normal_page) const override;

    const char* ModeName() const override;
};

}} // namespace Slic3r::GUI

#endif // NORMALMODEBUILDER_HPP
