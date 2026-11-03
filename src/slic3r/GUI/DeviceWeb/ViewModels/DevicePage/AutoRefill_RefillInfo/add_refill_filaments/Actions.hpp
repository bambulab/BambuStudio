#ifndef AUTOREFILLADDREFILLFILAMENTSACTIONS_HPP
#define AUTOREFILLADDREFILLFILAMENTSACTIONS_HPP

#include <nlohmann/json.hpp>

#include <string>

namespace Slic3r {
class MachineObject;
namespace GUI {

class AutoRefillRefillInfoVM;

class AutoRefillAddRefillFilamentsActions
{
public:
    static nlohmann::json Handle(AutoRefillRefillInfoVM& vm,
                                 MachineObject*          obj,
                                 const std::string&      action,
                                 const nlohmann::json&   payload);

private:
    static nlohmann::json Reply(AutoRefillRefillInfoVM& vm,
                                const std::string&      action,
                                int                     code,
                                const std::string&      msg);
};

}} // namespace Slic3r::GUI

#endif // AUTOREFILLADDREFILLFILAMENTSACTIONS_HPP
