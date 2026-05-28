#ifndef slic3r_ColorCutCoordinator_hpp_
#define slic3r_ColorCutCoordinator_hpp_

#include "ColorCutTypes.hpp"

#include <optional>

namespace Slic3r {
namespace ColorCut {

class ColorCutAttributeRepository;

class ColorCutCoordinator
{
public:
    explicit ColorCutCoordinator(ColorCutAttributeRepository *repository = nullptr);

    std::optional<ColorCutResult> execute(const ColorCutRequest &request) const;

private:
    ColorCutAttributeRepository *m_repository;
};

} // namespace ColorCut
} // namespace Slic3r

#endif
