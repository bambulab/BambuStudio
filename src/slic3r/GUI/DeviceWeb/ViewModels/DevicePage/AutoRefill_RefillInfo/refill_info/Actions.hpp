#ifndef AUTOREFILLREFILLINFOACTIONS_HPP
#define AUTOREFILLREFILLINFOACTIONS_HPP

#include "slic3r/GUI/DeviceCore/DevDefs.h"

#include <nlohmann/json.hpp>

#include <string>

namespace Slic3r {
class MachineObject;
namespace GUI {

class AutoRefillRefillInfoVM;

class AutoRefillRefillInfoActions
{
public:
    static nlohmann::json Handle(AutoRefillRefillInfoVM& vm,
                                 MachineObject*          obj,
                                 const std::string&      action,
                                 const nlohmann::json&   payload);

    /* Prompts for `slot`'s removal and publishes it when confirmed, with the
     * copy the management page's own delete button uses. For the picker's
     * "Go to Unbind" follow-up, which closes the picker before prompting and
     * so has no page left to report progress to; the management page picks the
     * result up on its refresh timer. Must run on the UI thread. */
    static void PromptAndRemoveSlot(const DevAmsSlotId& slot);

private:
    static nlohmann::json Reply(AutoRefillRefillInfoVM& vm,
                                const std::string&      action,
                                int                     code,
                                const std::string&      msg);
};

}} // namespace Slic3r::GUI

#endif // AUTOREFILLREFILLINFOACTIONS_HPP
