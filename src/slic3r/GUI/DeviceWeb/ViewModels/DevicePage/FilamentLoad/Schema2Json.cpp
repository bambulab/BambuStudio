#include "Schema.hpp"

namespace Slic3r { namespace GUI { namespace FilamentLoadSchema { namespace format {

void to_json(nlohmann::json& j, const ActionControl& v)
{
    j = {
        {"visible", v.visible},
        {"enabled", v.enabled},
        {"text", v.text},
        {"disabled_reason", v.disabled_reason},
    };
}

void to_json(nlohmann::json& j, const StepItem& v)
{
    j = {{"text", v.text}, {"status", v.status}};
}

void to_json(nlohmann::json& j, const StepArea& v)
{
    j = {{"kind", v.kind}, {"slot_text", v.slot_text}, {"items", v.items}};
}

void to_json(nlohmann::json& j, const Actions& v)
{
    j = {{"retry", v.retry}, {"stop", v.stop}};
}

void to_json(nlohmann::json& j, const Display& v)
{
    j = {
        {"visible", v.visible},
        {"expanded", v.expanded},
        {"title", v.title},
        {"image_name", v.image_name},
        {"steps", v.steps},
        {"actions", v.actions},
    };
}

void to_json(nlohmann::json& j, const State& v)
{
    j = {{"display", v.display}};
}

}}}} // namespace Slic3r::GUI::FilamentLoadSchema::format
