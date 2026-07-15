#pragma once

#include <nlohmann/json.hpp>

namespace Slic3r {

enum DevMcTackType
{

};

class DevMcTask
{
public:
    void ParseV1_0(const nlohmann::json& print_json);
    void Reset();

    bool IsValid() const { return m_id >= 0; }
    int  GetId() const { return m_id; }
    int  GetStepRaw() const { return m_step; }
    int  GetSubStep() const { return (m_step >> 8) & 0xFF; }
    int  GetTotalStep() const { return m_step & 0xFF; }

private:
    int m_id = -1;
    int m_step = 0;
};

} // namespace Slic3r
