#ifndef AMSCONTROLWEBVIEWMODELACTIONS_HPP
#define AMSCONTROLWEBVIEWMODELACTIONS_HPP

// Handles the `action` submod of the AMS control page: the footer commands, the
// per-slot entries, and the clicks that only move the page's own selection. The
// printer commands are ports of the matching StatusPanel handlers.

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace Slic3r {
class MachineObject;
}

namespace Slic3r { namespace GUI {

class DevicePageAmsControlWebVM;

class AmsControlWebActionHandler
{
public:
    // Returns nothing when `action` is none of ours, leaving the caller to answer
    // with its own "unknown action" response. `machine_obj` may be null: the
    // selection-only actions work without a printer.
    static std::optional<nlohmann::json> TryHandle(DevicePageAmsControlWebVM& vm,
                                                   const std::string&        action,
                                                   const nlohmann::json&     payload,
                                                   MachineObject*            machine_obj);
};

}} // namespace Slic3r::GUI

#endif // AMSCONTROLWEBVIEWMODELACTIONS_HPP
