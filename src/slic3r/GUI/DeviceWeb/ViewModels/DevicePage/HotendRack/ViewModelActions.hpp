#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace Slic3r {
class MachineObject;
}

namespace Slic3r { namespace GUI {
class DevicePageHotendRackVM;

namespace HotendRackActionHandler {

std::optional<nlohmann::json> TryHandle(DevicePageHotendRackVM& vm,
                                        const std::string& action,
                                        const nlohmann::json& payload,
                                        MachineObject* machine_obj);

} // namespace HotendRackActionHandler
}} // namespace Slic3r::GUI
