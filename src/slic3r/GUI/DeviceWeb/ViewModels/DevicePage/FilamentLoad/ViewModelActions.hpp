#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace Slic3r {
class MachineObject;
}

namespace Slic3r { namespace GUI {
class DevicePageFilamentLoadVM;

namespace FilamentLoadActionHandler {

std::optional<nlohmann::json> TryHandle(DevicePageFilamentLoadVM& vm,
                                        const std::string& action,
                                        const nlohmann::json& payload,
                                        MachineObject* machine_obj);

} // namespace FilamentLoadActionHandler
}} // namespace Slic3r::GUI
