#include "ViewModelActions.hpp"

#include "ViewModel.hpp"

#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"

namespace Slic3r { namespace GUI {

namespace {

nlohmann::json respond(DevicePageFilamentLoadVM& vm, const std::string& action, int code, const std::string& message = {})
{
    vm.InvalidateReportCache();
    return vm.MakeActionResponse(action, code, message, vm.BuildState());
}

} // namespace

namespace FilamentLoadActionHandler {

std::optional<nlohmann::json> TryHandle(DevicePageFilamentLoadVM& vm,
                                        const std::string& action,
                                        const nlohmann::json& /*payload*/,
                                        MachineObject* machine_obj)
{
    if (action == "retry") {
        if (!machine_obj)
            return respond(vm, action, 2, _u8L("Please select a printer"));
        machine_obj->command_ams_control("resume");
        return respond(vm, action, 0);
    }

    if (action == "stop") {
        if (!machine_obj)
            return respond(vm, action, 2, _u8L("Please select a printer"));
        machine_obj->command_ams_control("abort");
        return respond(vm, action, 0);
    }

    return std::nullopt;
}

} // namespace FilamentLoadActionHandler
}} // namespace Slic3r::GUI
