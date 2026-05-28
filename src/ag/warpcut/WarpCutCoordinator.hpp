#ifndef slic3r_WarpCutCoordinator_hpp_
#define slic3r_WarpCutCoordinator_hpp_

#include "WarpCutRequest.hpp"

#include "ag/customcut/ColorCutTypes.hpp"

#include <optional>

namespace Slic3r {
namespace WarpCut {

class WarpCutCoordinator
{
public:
    std::optional<ColorCut::ColorCutResult> execute(const Request &request) const;
};

} // namespace WarpCut
} // namespace Slic3r

#endif // slic3r_WarpCutCoordinator_hpp_
