#ifndef slic3r_Format_ResourcePathUtils_hpp_
#define slic3r_Format_ResourcePathUtils_hpp_

#include <cctype>
#include <string>
#include <vector>

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

namespace Slic3r {
namespace resource_path {

inline std::string ascii_lower_copy(const std::string& value)
{
    std::string lowered;
    lowered.reserve(value.size());
    for (unsigned char ch : value)
        lowered.push_back(static_cast<char>(std::tolower(ch)));
    return lowered;
}

inline boost::filesystem::path find_child_case_insensitive(
    const boost::filesystem::path& directory,
    const boost::filesystem::path& requested_name,
    const char* context)
{
    if (!boost::filesystem::exists(directory) || !boost::filesystem::is_directory(directory))
        return {};

    const std::string requested_lower = ascii_lower_copy(requested_name.filename().string());
    std::vector<boost::filesystem::path> matches;

    boost::system::error_code ec;
    for (boost::filesystem::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
        if (ascii_lower_copy(it->path().filename().string()) == requested_lower)
            matches.push_back(it->path());
    }

    if (matches.size() == 1)
        return matches.front();

    if (matches.size() > 1) {
        BOOST_LOG_TRIVIAL(warning) << context << ": ambiguous case-insensitive resource match for "
                                   << requested_name << " in " << directory;
    }

    return {};
}

inline boost::filesystem::path resolve_existing_path_case_insensitive(
    const boost::filesystem::path& requested_path,
    const char* context = "resource_path")
{
    if (requested_path.empty())
        return {};

    if (boost::filesystem::exists(requested_path))
        return requested_path;

    boost::filesystem::path current;
    bool initialized = false;

    for (const boost::filesystem::path& part : requested_path) {
        if (part == requested_path.root_name() || part == requested_path.root_directory()) {
            current /= part;
            initialized = true;
            continue;
        }

        if (!initialized) {
            current = boost::filesystem::current_path();
            initialized = true;
        }

        boost::filesystem::path exact = current / part;
        if (boost::filesystem::exists(exact)) {
            current = exact;
            continue;
        }

        boost::filesystem::path matched = find_child_case_insensitive(current, part, context);
        if (matched.empty())
            return {};

        BOOST_LOG_TRIVIAL(info) << context << ": resolved resource path case-insensitively from "
                                << exact << " to " << matched;
        current = matched;
    }

    return boost::filesystem::exists(current) ? current : boost::filesystem::path();
}

inline boost::filesystem::path resolve_existing_relative_path_case_insensitive(
    const boost::filesystem::path& base_dir,
    const boost::filesystem::path& resource_path,
    const char* context = "resource_path")
{
    const boost::filesystem::path requested = resource_path.is_absolute() ? resource_path : base_dir / resource_path;
    return resolve_existing_path_case_insensitive(requested, context);
}

} // namespace resource_path
} // namespace Slic3r

#endif /* slic3r_Format_ResourcePathUtils_hpp_ */
