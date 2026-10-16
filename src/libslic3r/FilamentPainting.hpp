#ifndef slic3r_FilamentPainting_hpp_
#define slic3r_FilamentPainting_hpp_

#include "libslic3r.h"
#include <string>

namespace Slic3r {

// BBS: encode an unsplit leaf using the existing TriangleSelector/3MF format.
// Hexadecimal nibbles are written in reverse bit-stream order by FacetsAnnotation.
// ID 0 and out-of-range IDs retain the legacy empty-string result.
inline std::string encode_filament_id(unsigned int id)
{
    if (id == 0 || id > MAXIMUM_EXTRUDER_NUMBER)
        return {};
    if (id < 3)
        return id == 1 ? "4" : "8";

    static constexpr char hex[] = "0123456789ABCDEF";
    const unsigned int remainder = id - 3;
    std::string result(1, hex[remainder % 15]);
    result.append(remainder / 15, 'F');
    result.push_back('C');
    return result;
}

} // namespace Slic3r

#endif
