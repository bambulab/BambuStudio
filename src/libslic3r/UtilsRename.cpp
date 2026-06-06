#include "Utils.hpp"

#include <boost/nowide/convert.hpp>
#include <boost/nowide/cstdio.hpp>

#ifdef WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Slic3r {

std::error_code rename_file(const std::string &from, const std::string &to)
{
#ifdef _WIN32
    const std::wstring from_w = boost::nowide::widen(from);
    const std::wstring to_w   = boost::nowide::widen(to);
    if (MoveFileExW(from_w.c_str(), to_w.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return {};
    return std::error_code(static_cast<int>(GetLastError()), std::system_category());
#else
    boost::nowide::remove(to.c_str());
    return std::make_error_code(static_cast<std::errc>(boost::nowide::rename(from.c_str(), to.c_str())));
#endif
}

} // namespace Slic3r
