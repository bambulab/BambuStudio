#ifndef slic3r_GUI_McpCredentials_hpp_
#define slic3r_GUI_McpCredentials_hpp_

#include <string>

namespace Slic3r::GUI {

bool load_or_create_mcp_token(const std::string &profile_dir, std::string &token, std::string &error);
bool regenerate_mcp_token(const std::string &profile_dir, std::string &token, std::string &error);

}

#endif
