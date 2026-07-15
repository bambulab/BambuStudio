#include "DevMcTask.hpp"

#include "DevUtil.h"

namespace Slic3r {

void DevMcTask::ParseV1_0(const nlohmann::json& print_json)
{
    if (!print_json.contains("mc_task") || !print_json["mc_task"].is_object()) {
        Reset();
        return;
    }

    const auto& mc_task = print_json["mc_task"];
    DevJsonValParser::ParseVal(mc_task, "id", m_id);
    DevJsonValParser::ParseVal(mc_task, "step", m_step);
}

void DevMcTask::Reset()
{
    m_id = -1;
    m_step = 0;
}

} // namespace Slic3r
