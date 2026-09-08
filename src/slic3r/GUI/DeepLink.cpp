#include "DeepLink.hpp"

#include <array>

namespace Slic3r {
namespace GUI {

namespace {

struct DeepLinkPrefix
{
    std::string_view prefix;
    RemoteModelAction action;
};

constexpr std::array<DeepLinkPrefix, 2> canonical_prefixes {{
    {"bambustudio://open?", RemoteModelAction::OpenProject},
    {"bambustudio://import?", RemoteModelAction::ImportModel},
}};

constexpr std::string_view legacy_macos_prefix = "bambustudioopen://";

bool starts_with(std::string_view value, std::string_view prefix)
{
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

std::optional<std::string_view> find_query_parameter(std::string_view query, std::string_view name)
{
    size_t parameter_start = 0;
    while (parameter_start <= query.size()) {
        const size_t parameter_end = query.find('&', parameter_start);
        const std::string_view parameter = query.substr(
            parameter_start,
            parameter_end == std::string_view::npos ? std::string_view::npos : parameter_end - parameter_start);

        if (starts_with(parameter, name)) {
            const std::string_view value = parameter.substr(name.size());
            if (!value.empty())
                return value;
            return std::nullopt;
        }

        if (parameter_end == std::string_view::npos)
            break;
        parameter_start = parameter_end + 1;
    }

    return std::nullopt;
}

} // namespace

bool is_studio_deep_link_candidate(std::string_view url)
{
    for (const DeepLinkPrefix &candidate : canonical_prefixes) {
        const std::string_view action_prefix = candidate.prefix.substr(0, candidate.prefix.size() - 1);
        if (url == action_prefix || starts_with(url, candidate.prefix))
            return true;
    }
    return starts_with(url, legacy_macos_prefix);
}

std::optional<StudioDeepLink> parse_studio_deep_link(std::string_view url)
{
    for (const DeepLinkPrefix &candidate : canonical_prefixes) {
        if (!starts_with(url, candidate.prefix))
            continue;

        const std::string_view query = url.substr(candidate.prefix.size());
        const auto file = find_query_parameter(query, "file=");
        if (!file)
            return std::nullopt;

        // Preserve trailing parameters such as the existing optional &name=
        // value. GUI_App decodes and validates this payload exactly once.
        const size_t file_offset = static_cast<size_t>(file->data() - url.data());
        return StudioDeepLink {candidate.action, std::string(url.substr(file_offset)), false};
    }

    if (starts_with(url, legacy_macos_prefix)) {
        const std::string_view payload = url.substr(legacy_macos_prefix.size());
        if (!payload.empty())
            return StudioDeepLink {RemoteModelAction::OpenProject, std::string(payload), true};
    }

    return std::nullopt;
}

} // namespace GUI
} // namespace Slic3r
