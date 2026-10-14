#ifndef slic3r_MCP_Dispatcher_hpp_
#define slic3r_MCP_Dispatcher_hpp_

#include <string>
#include "nlohmann/json.hpp"

namespace Slic3r {
namespace GUI {

class MCPDispatcher {
public:
    MCPDispatcher() = default;
    ~MCPDispatcher() = default;

    // Dispatches a JSON-RPC 2.0 message and returns the JSON-RPC response (or empty json if notification)
    nlohmann::json dispatch(const nlohmann::json& request);

    // MCP Protocol Methods
    nlohmann::json handle_initialize(const nlohmann::json& params, const nlohmann::json& id);
    nlohmann::json handle_tools_list(const nlohmann::json& id);
    nlohmann::json handle_tools_call(const nlohmann::json& params, const nlohmann::json& id);

    // Tools
    nlohmann::json tool_get_profile_summary(const nlohmann::json& args);
    nlohmann::json tool_get_slicing_params(const nlohmann::json& args);
    nlohmann::json tool_set_slicing_params(const nlohmann::json& args);
    nlohmann::json tool_get_plater_info(const nlohmann::json& args);
    nlohmann::json tool_paint_part(const nlohmann::json& args);
    nlohmann::json tool_slice_project(const nlohmann::json& args);
    nlohmann::json tool_get_slice_status(const nlohmann::json& args);
    nlohmann::json tool_load_model(const nlohmann::json& args);
    nlohmann::json tool_clear_plate(const nlohmann::json& args);

private:
    nlohmann::json make_response(const nlohmann::json& id, const nlohmann::json& result);
    nlohmann::json make_error(const nlohmann::json& id, int code, const std::string& message);
    nlohmann::json make_tool_result(const std::string& text, bool is_error = false);
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_MCP_Dispatcher_hpp_
