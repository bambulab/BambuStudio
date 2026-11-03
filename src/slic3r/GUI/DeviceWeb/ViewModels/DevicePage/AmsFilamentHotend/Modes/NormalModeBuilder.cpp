#include "NormalModeBuilder.hpp"
#include "slic3r/GUI/DeviceWeb/ViewModels/DevicePage/AmsFilamentHotend/Schema.hpp"

namespace Slic3r { namespace GUI {

namespace SK = AmsHotendSchema::keys;
namespace SV = AmsHotendSchema::values;

nlohmann::json NormalModeBuilder::BuildFilamentPage(
    const DevicePageAmsFilamentHotendVM& /*vm*/,
    MachineObject*                       /*machine_obj*/,
    const nlohmann::json&                prebuilt_normal_page) const
{
    // The VM already assembled the full normal-shape filament_page in
    // BuildState(); we just need to stamp the mode field so the Web side
    // knows which payload variant it received.
    nlohmann::json page = prebuilt_normal_page;
    page[SK::filament_page::mode] = SV::mode::normal;
    return page;
}

const char* NormalModeBuilder::ModeName() const
{
    return SV::mode::normal;
}

}} // namespace Slic3r::GUI
