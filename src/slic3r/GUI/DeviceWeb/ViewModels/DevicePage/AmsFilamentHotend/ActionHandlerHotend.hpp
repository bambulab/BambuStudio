#ifndef DEVICEPAGEHOTENDACTIONHANDLER_HPP
#define DEVICEPAGEHOTENDACTIONHANDLER_HPP

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace Slic3r {
class MachineObject;
}

namespace Slic3r { namespace GUI {

class DevicePageAmsFilamentHotendVM;

class DevicePageHotendActionHandler
{
public:
    static std::optional<nlohmann::json> TryHandle(DevicePageAmsFilamentHotendVM& vm,
                                                   const std::string& action,
                                                   const nlohmann::json& payload,
                                                   MachineObject* machine_obj);
};

}} // namespace Slic3r::GUI

#endif // DEVICEPAGEHOTENDACTIONHANDLER_HPP
