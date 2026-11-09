/*****************************************************************//**
 * \file   Builder.hpp
 * \brief  Add-refill-filaments picker builder.
 *
 * Step 1 collects display slots; Step 2 overlays Query(5) eligibility.
 *********************************************************************/
#ifndef AUTOREFILLADDREFILLFILAMENTSBUILDER_HPP
#define AUTOREFILLADDREFILLFILAMENTSBUILDER_HPP

#include <nlohmann/json.hpp>

#include <cstdint>

namespace Slic3r {
class MachineObject;
struct DevFilaRefillQueryResult;
namespace GUI {

class AutoRefillAddRefillFilamentsBuilder
{
public:
    /**
     * \brief Build the add-refill-filaments picker payload.
     *
     * Step 1 collects the slots that should appear on the page.
     * Step 2 overlays Query(5) eligibility on those slots.
     *
     * \param obj          Selected machine; a null or empty filament system
     *                     yields page metadata without slot units.
     * \param session_seq  Dialog-open counter copied into the payload.
     * \param payload      Request body. `mode` selects create/add-to-group;
     *                     `slots` is the current selection.
     * \param query        Parsed refill_command 5 result. Null skips device
     *                     eligibility and keeps local slot metadata only.
     */
    static nlohmann::json Build(MachineObject*        obj,
                                std::uint32_t          session_seq,
                                const nlohmann::json& payload = nlohmann::json::object(),
                                const DevFilaRefillQueryResult* query = nullptr);
};

}} // namespace Slic3r::GUI

#endif // AUTOREFILLADDREFILLFILAMENTSBUILDER_HPP
