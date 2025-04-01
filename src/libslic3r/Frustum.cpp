#include "Frustum.hpp"
#include <cmath>
namespace Slic3r {

void Frustum::Plane::set_abcd(float a, float b, float c, float d)
{
    m_abcd[0] = a;
    m_abcd[1] = b;
    m_abcd[2] = c;
    m_abcd[3] = d;
}

const Vec4f& Frustum::Plane::get_abcd() const
{
    return m_abcd;
}

void Frustum::Plane::normailze()
{
    float mag;
    mag = sqrt(m_abcd[0] * m_abcd[0] + m_abcd[1] * m_abcd[1] + m_abcd[2] * m_abcd[2]);
    m_abcd[0] = m_abcd[0] / mag;
    m_abcd[1] = m_abcd[1] / mag;
    m_abcd[2] = m_abcd[2] / mag;
    m_abcd[3] = m_abcd[3] / mag;
}

float Frustum::Plane::distance(const Vec3f& pt) const
{
    float result = 0.0f;
    for (int i = 0; i < 3; ++i) {
        result += pt[i] * m_abcd[i];
    }

    result += m_abcd[3];

    return result;
}

Frustum::Plane::PlaneIntersects Frustum::Plane::intersects(const BoundingBoxf3 &box) const
{
    // see https://cgvr.cs.uni-bremen.de/teaching/cg_literatur/lighthouse3d_view_frustum_culling/index.html

    Vec3d positive_v = box.min;
    if (m_abcd[0] > 0.f)
        positive_v[0] = box.max.x();
    if (m_abcd[1] > 0.f)
        positive_v[1] = box.max.y();
    if (m_abcd[2] > 0.f)
        positive_v[2] = box.max.z();

    float dis_positive = distance(positive_v.cast<float>());
    if (dis_positive < 0.f)
    {
        return Frustum::Plane::PlaneIntersects::Intersects_Back;
    }

    Vec3d negitive_v = box.max;
    if (m_abcd[0] > 0.f)
        negitive_v[0] = box.min.x();
    if (m_abcd[1] > 0.f)
        negitive_v[1] = box.min.y();
    if (m_abcd[2] > 0.f)
        negitive_v[2] = box.min.z();

    float dis_negitive = distance(negitive_v.cast<float>());

    if (dis_negitive < 0.f)
    {
        return Frustum::Plane::PlaneIntersects::Intersects_Cross;
    }

    return Frustum::Plane::PlaneIntersects::Intersects_Front;
}
Frustum::Plane::PlaneIntersects Frustum::Plane::intersects(const Vec3f &p0) const
{
    float d = distance(p0);
    if (d == 0) {
        return Plane::Intersects_Tangent;
    }
    return (d > 0.0f) ? Plane::Intersects_Front : Plane::Intersects_Back;
}
Frustum::Plane::PlaneIntersects Frustum::Plane::intersects(const Vec3f &p0, const Vec3f &p1) const
{
    Plane::PlaneIntersects state0 = intersects(p0);
    Plane::PlaneIntersects state1 = intersects(p1);
    if (state0 == state1) {
        return state0;
    }
    if (state0 == Plane::Intersects_Tangent || state1 == Plane::Intersects_Tangent) {
        return Plane::Intersects_Tangent;
    }

    return Plane::Intersects_Cross;
}
Frustum::Plane::PlaneIntersects Frustum::Plane::intersects(const Vec3f &p0, const Vec3f &p1, const Vec3f &p2) const
{
    Plane::PlaneIntersects state0 = intersects(p0, p1);
    Plane::PlaneIntersects state1 = intersects(p0, p2);
    Plane::PlaneIntersects state2 = intersects(p1, p2);

    if (state0 == state1 && state0 == state2) {
        return state0; }

    if (state0 == Plane::Intersects_Cross || state1 == Plane::Intersects_Cross || state2 == Plane::Intersects_Cross) {
        return Plane::Intersects_Cross;
    }

    return Plane::Intersects_Tangent;
}

bool Frustum::intersects(const BoundingBoxf3 &box) const
{
    for (auto& plane : planes) {
        const auto rt = plane.intersects(box);
        if (Frustum::Plane::Intersects_Back == rt) {
            return false;
        }
    }

    return true;
}

bool Frustum::intersects(const Vec3f &p0) const {
    for (auto &plane : planes) {
        if (plane.intersects(p0) == Plane::Intersects_Back) { return false; }
    }
    return true;
}

bool Frustum::intersects(const Vec3f &p0, const Vec3f &p1) const
{
    for (auto &plane : planes) {
        if (plane.intersects(p0, p1) == Plane::Intersects_Back) {
            return false;
        }
    }
    return true;
}

bool Frustum::intersects(const Vec3f &p0, const Vec3f &p1, const Vec3f &p2) const
{
    for (auto &plane : planes) {
        if (plane.intersects(p0, p1, p2) == Plane::Intersects_Back) {
            return false;
        }
    }
    return true;
}

} // namespace Slic3r
