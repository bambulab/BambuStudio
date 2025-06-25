#pragma once
#include "nlohmann/json.hpp"

namespace Slic3r
{

class BBLCrossTalk
{
public:
    BBLCrossTalk() = delete;
    ~BBLCrossTalk() = delete;

public:
    static std::string Crosstalk_DevId(const std::string& str);
    static std::string Crosstalk_DevIP(const std::string& str);
    static std::string Crosstalk_JsonLog(const nlohmann::json& json);
};

} // namespace Slic3r