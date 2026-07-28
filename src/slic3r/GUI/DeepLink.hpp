#ifndef slic3r_GUI_DeepLink_hpp_
#define slic3r_GUI_DeepLink_hpp_

#include <optional>
#include <string>
#include <string_view>

namespace Slic3r {
namespace GUI {

enum class RemoteModelAction : unsigned char
{
    OpenProject,
    ImportModel
};

struct StudioDeepLink
{
    RemoteModelAction action;
    std::string       encoded_download_info;
    bool              legacy_macos_scheme { false };
};

struct RemoteModelDownloadRequest
{
    RemoteModelAction action;
    std::string       download_info;
};

// Recognizes only Bambu Studio's supported model-download deep links. The
// returned payload is intentionally left encoded; URL decoding and trust
// validation remain in GUI_App, where the existing confirmation UI lives.
std::optional<StudioDeepLink> parse_studio_deep_link(std::string_view url);
bool is_studio_deep_link_candidate(std::string_view url);

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_DeepLink_hpp_
