#ifndef slic3r_GCodeOrigin_hpp_
#define slic3r_GCodeOrigin_hpp_

#include "libslic3r/Point.hpp"

namespace Slic3r {

class GCodeOriginState
{
public:
    GCodeOriginState() = default;
    explicit GCodeOriginState(const Vec2d &origin) : m_origin(origin) {}

    const Vec2d& origin() const { return m_origin; }

    Point set_origin(const Vec2d &pointf)
    {
        return set_origin(m_origin, pointf);
    }

    static Point set_origin(Vec2d &origin, const Vec2d &pointf)
    {
        const Point translate(
            scale_(origin(0) - pointf(0)),
            scale_(origin(1) - pointf(1))
        );
        origin = pointf;
        return translate;
    }

private:
    Vec2d m_origin = Vec2d::Zero();
};

} // namespace Slic3r

#endif // slic3r_GCodeOrigin_hpp_
