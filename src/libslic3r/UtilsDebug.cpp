#include "Utils.hpp"

#include <boost/filesystem.hpp>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <filesystem>

namespace Slic3r {

static std::atomic<bool> debug_out_path_called(false);

std::string debug_out_path(const char *name, ...)
{
    auto svg_folder = boost::filesystem::path(data_dir()) / "SVG/";
    if (!debug_out_path_called.exchange(true)) {
        if (!boost::filesystem::exists(svg_folder))
            boost::filesystem::create_directory(svg_folder);
        std::string path = boost::filesystem::system_complete(svg_folder).string();
        printf("Debugging output files will be written to %s\n", path.c_str());
    }

    char buffer[2048];
    va_list args;
    va_start(args, name);
    std::vsnprintf(buffer, sizeof(buffer), name, args);
    va_end(args);

    std::string buf(buffer);
    if (size_t pos = buf.find_first_of('/'); pos != std::string::npos) {
        std::string sub_dir = buf.substr(0, pos);
        std::filesystem::create_directory(svg_folder.string() + sub_dir);
    }
    return svg_folder.string() + std::string(buffer);
}

} // namespace Slic3r
