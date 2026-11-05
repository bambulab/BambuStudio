#ifndef DEVICEPAGEEXTAMSACTIONHANDLER_HPP
#define DEVICEPAGEEXTAMSACTIONHANDLER_HPP

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace Slic3r {
class MachineObject;
}

namespace Slic3r { namespace GUI {

class DevicePageAmsFilamentHotendVM;

class DevicePageExtAmsActionHandler
{
public:
    static std::optional<nlohmann::json> TryHandle(DevicePageAmsFilamentHotendVM& vm,
                                                   const std::string& action,
                                                   const nlohmann::json& payload,
                                                   MachineObject* machine_obj);
};

}} // namespace Slic3r::GUI

#endif // DEVICEPAGEEXTAMSACTIONHANDLER_HPP
