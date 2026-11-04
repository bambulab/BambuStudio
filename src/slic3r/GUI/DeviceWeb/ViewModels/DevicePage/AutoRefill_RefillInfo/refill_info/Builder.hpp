#ifndef AUTOREFILLREFILLINFOBUILDER_HPP
#define AUTOREFILLREFILLINFOBUILDER_HPP

#include <nlohmann/json.hpp>

#include <cstdint>

namespace Slic3r {
class MachineObject;
namespace GUI {

class AutoRefillRefillInfoBuilder
{
public:
    static nlohmann::json Build(MachineObject* obj, std::uint32_t session_seq);
};

}} // namespace Slic3r::GUI

#endif // AUTOREFILLREFILLINFOBUILDER_HPP
