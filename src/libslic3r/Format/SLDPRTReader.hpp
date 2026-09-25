#ifndef slic3r_SLDPRTReader_hpp_
#define slic3r_SLDPRTReader_hpp_

#include <array>
#include <cstdint>
#include <functional>
#include <istream>
#include <vector>

namespace Slic3r::SolidWorks {

// Geometry from the saved display tessellation, in millimetres. This reader
// has no GUI/CAD-kernel dependencies so the binary parser can be tested alone.
struct Mesh {
    std::vector<std::array<float, 3>> vertices;
    std::vector<std::uint32_t> indices;
};

using ProgressFn = std::function<bool(unsigned)>; // percent; false cancels
struct Cancelled {};

// Throws std::runtime_error for unsupported, corrupt or empty documents and
// Cancelled on request. Never returns a partially decoded mesh.
Mesh read(std::istream &input, const ProgressFn &progress = {});
Mesh decode(const std::vector<unsigned char> &file, const ProgressFn &progress = {});

} // namespace Slic3r::SolidWorks
#endif
