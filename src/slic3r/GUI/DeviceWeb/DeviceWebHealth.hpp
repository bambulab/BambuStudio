#ifndef slic3r_GUI_DeviceWebHealth_hpp_
#define slic3r_GUI_DeviceWebHealth_hpp_

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace Slic3r { namespace GUI { namespace DeviceWebHealth {

enum class Action {
    Boot,
    Ready,
    JsError,
    UnhandledRejection,
    ResourceError,
};

struct Message
{
    Action      action;
    std::string page_instance_id;
    std::string document_url;
    std::string message;
    std::string source;
    std::string reason;
    std::string tag;
    std::string url;
    int         line   = 0;
    int         column = 0;
};

std::optional<Message> Parse(const nlohmann::json &body);
bool MatchesCurrentDocument(const std::string &message_url, const std::string &current_url);
std::string            DiagnosticShim();

}}} // namespace Slic3r::GUI::DeviceWebHealth

#endif // !slic3r_GUI_DeviceWebHealth_hpp_
