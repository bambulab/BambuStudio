#ifndef slic3r_McpTools_hpp_
#define slic3r_McpTools_hpp_

#include <nlohmann/json.hpp>
#include <cstdint>
#include <string>

namespace Slic3r::GUI {

class Plater;

class McpTools final {
public:
    static nlohmann::json definitions();
    static nlohmann::json call(Plater& plater, const std::string& name, const nlohmann::json& arguments);
    static void set_active(bool active);
    static void quiesce_external_jobs();
    static bool has_pending_native_work();
};

bool mcp_noninteractive_import();
void mcp_require_interactive_import(const char* reason);
bool mcp_slice_active();
bool mcp_accept_slice_event(std::uint64_t run);
void mcp_slice_progress(std::uint64_t run, int percent);
void mcp_slice_completed(Plater& plater, std::uint64_t run, bool success, bool cancelled, bool worker_stopped, const std::string& error);
void mcp_ui_job_completed(std::uint64_t run, bool success, bool cancelled, const std::string& error);

}

#endif
