#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace Slic3r { namespace GUI { namespace FilamentLoadSchema {

namespace values {
namespace step_kind {
inline constexpr const char* load    = "load";
inline constexpr const char* unload  = "unload";
inline constexpr const char* vt_load = "vt_load";
} // namespace step_kind

namespace step_status {
inline constexpr const char* pending = "pending";
inline constexpr const char* current = "current";
inline constexpr const char* done    = "done";
} // namespace step_status
} // namespace values

namespace format {

struct ActionControl
{
    bool        visible = false;
    bool        enabled = false;
    std::string text;
    std::string disabled_reason;
};

struct StepItem
{
    std::string text;
    std::string status = values::step_status::pending;
};

struct StepArea
{
    std::string           kind = values::step_kind::load;
    std::string           slot_text;
    std::vector<StepItem> items;
};

struct Actions
{
    ActionControl retry;
    ActionControl stop;
};

struct Display
{
    bool        visible = false;
    bool        expanded = false;
    std::string title;
    std::string image_name;
    StepArea    steps;
    Actions     actions;
};

struct State
{
    Display display;
};

void to_json(nlohmann::json& j, const ActionControl& v);
void to_json(nlohmann::json& j, const StepItem& v);
void to_json(nlohmann::json& j, const StepArea& v);
void to_json(nlohmann::json& j, const Actions& v);
void to_json(nlohmann::json& j, const Display& v);
void to_json(nlohmann::json& j, const State& v);

} // namespace format
}}} // namespace Slic3r::GUI::FilamentLoadSchema
