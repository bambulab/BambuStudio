#ifndef slic3r_Geometry_hpp_
#define slic3r_Geometry_hpp_

#include "libslic3r.h"
#include "BoundingBox.hpp"
#include "ExPolygon.hpp"
#include "Polygon.hpp"
#include "Polyline.hpp"

// Serialization through the Cereal library
#include <cereal/access.hpp>

namespace Slic3r { 

    namespace ClipperLib {
        class PolyNode;
        using PolyNodes = std::vector<PolyNode*>;
    }

namespace Geometry {

// Generic result of an orientation predicate.
enum Orientation
{
    ORIENTATION_CCW = 1,
    ORIENTATION_CW = -1,
    ORIENTATION_COLINEAR = 0
};

// Return orientation of the three points (clockwise, counter-clockwise, colinear)
// The predicate is exact for the coord_t type, using 64bit signed integers for the temporaries.
// which means, the coord_t types must not have some of the topmost bits utilized.
// As the points are limited to 30 bits + signum,
// the temporaries u, v, w are limited to 61 bits + signum,
// and d is limited to 63 bits + signum and we are good.
static inline Orientation orient(const Point &a, const Point &b, const Point &c)
{
    static_assert(sizeof(coord_t) * 2 == sizeof(int64_t), "orient works with 32 bit coordinates");
    int64_t u = int64_t(b(0)) * int64_t(c(1)) - int64_t(b(1)) * int64_t(c(0));
    int64_t v = int64_t(a(0)) * int64_t(c(1)) - int64_t(a(1)) * int64_t(c(0));
    int64_t w = int64_t(a(0)) * int64_t(b(1)) - int64_t(a(1)) * int64_t(b(0));
    int64_t d = u - v + w;
    return (d > 0) ? ORIENTATION_CCW : ((d == 0) ? ORIENTATION_COLINEAR : ORIENTATION_CW);
}

// Return orientation of the polygon by checking orientation of the left bottom corner of the polygon
// using exact arithmetics. The input polygon must not contain duplicate points
// (or at least the left bottom corner point must not have duplicates).
static inline bool is_ccw(const Polygon &poly)
{
    // The polygon shall be at least a triangle.
    assert(poly.points.size() >= 3);
    if (poly.points.size() < 3)
        return true;

    // 1) Find the lowest lexicographical point.
    unsigned int imin = 0;
    for (unsigned int i = 1; i < poly.points.size(); ++ i) {
        const Point &pmin = poly.points[imin];
        const Point &p    = poly.points[i];
        if (p(0) < pmin(0) || (p(0) == pmin(0) && p(1) < pmin(1)))
            imin = i;
    }

    // 2) Detect the orientation of the corner imin.
    size_t iPrev = ((imin == 0) ? poly.points.size() : imin) - 1;
    size_t iNext = ((imin + 1 == poly.points.size()) ? 0 : imin + 1);
    Orientation o = orient(poly.points[iPrev], poly.points[imin], poly.points[iNext]);
    // The lowest bottom point must not be collinear if the polygon does not contain duplicate points
    // or overlapping segments.
    assert(o != ORIENTATION_COLINEAR);
    return o == ORIENTATION_CCW;
}

inline bool ray_ray_intersection(const Vec2d &p1, const Vec2d &v1, const Vec2d &p2, const Vec2d &v2, Vec2d &res)
{
    double denom = v1(0) * v2(1) - v2(0) * v1(1);
    if (std::abs(denom) < EPSILON)
        return false;
    double t = (v2(0) * (p1(1) - p2(1)) - v2(1) * (p1(0) - p2(0))) / denom;
    res(0) = p1(0) + t * v1(0);
    res(1) = p1(1) + t * v1(1);
    return true;
}

inline bool segment_segment_intersection(const Vec2d &p1, const Vec2d &v1, const Vec2d &p2, const Vec2d &v2, Vec2d &res)
{
    double denom = v1(0) * v2(1) - v2(0) * v1(1);
    if (std::abs(denom) < EPSILON)
        // Lines are collinear.
        return false;
    double s12_x = p1(0) - p2(0);
    double s12_y = p1(1) - p2(1);
    double s_numer = v1(0) * s12_y - v1(1) * s12_x;
    bool   denom_is_positive = false;
    if (denom < 0.) {
        denom_is_positive = true;
        denom   = - denom;
        s_numer = - s_numer;
    }
    if (s_numer < 0.)
        // Intersection outside of the 1st segment.
        return false;
    double t_numer = v2(0) * s12_y - v2(1) * s12_x;
    if (! denom_is_positive)
        t_numer = - t_numer;
    if (t_numer < 0. || s_numer > denom || t_numer > denom)
        // Intersection outside of the 1st or 2nd segment.
        return false;
    // Intersection inside both of the segments.
    double t = t_numer / denom;
    res(0) = p1(0) + t * v1(0);
    res(1) = p1(1) + t * v1(1);
    return true;
}

inline bool segments_intersect(
	const Slic3r::Point &ip1, const Slic3r::Point &ip2, 
	const Slic3r::Point &jp1, const Slic3r::Point &jp2)
{    
    assert(ip1 != ip2);
    assert(jp1 != jp2);

    auto segments_could_intersect = [](
        const Slic3r::Point &ip1, const Slic3r::Point &ip2,
        const Slic3r::Point &jp1, const Slic3r::Point &jp2) -> std::pair<int, int>
    {
        Vec2i64 iv   = (ip2 - ip1).cast<int64_t>();
        Vec2i64 vij1 = (jp1 - ip1).cast<int64_t>();
        Vec2i64 vij2 = (jp2 - ip1).cast<int64_t>();
        int64_t tij1 = cross2(iv, vij1);
        int64_t tij2 = cross2(iv, vij2);
        return std::make_pair(
            // signum
            (tij1 > 0) ? 1 : ((tij1 < 0) ? -1 : 0),
            (tij2 > 0) ? 1 : ((tij2 < 0) ? -1 : 0));
    };

    std::pair<int, int> sign1 = segments_could_intersect(ip1, ip2, jp1, jp2);
    std::pair<int, int> sign2 = segments_could_intersect(jp1, jp2, ip1, ip2);
    int                 test1 = sign1.first * sign1.second;
    int                 test2 = sign2.first * sign2.second;
    if (test1 <= 0 && test2 <= 0) {
        // The segments possibly intersect. They may also be collinear, but not intersect.
        if (test1 != 0 || test2 != 0)
            // Certainly not collinear, then the segments intersect.
            return true;
        // If the first segment is collinear with the other, the other is collinear with the first segment.
        assert((sign1.first == 0 && sign1.second == 0) == (sign2.first == 0 && sign2.second == 0));
        if (sign1.first == 0 && sign1.second == 0) {
            // The segments are certainly collinear. Now verify whether they overlap.
            Slic3r::Point vi = ip2 - ip1;
            // Project both on the longer coordinate of vi.
            int axis = std::abs(vi.x()) > std::abs(vi.y()) ? 0 : 1;
            coord_t i = ip1(axis);
            coord_t j = ip2(axis);
            coord_t k = jp1(axis);
            coord_t l = jp2(axis);
            if (i > j)
                std::swap(i, j);
            if (k > l)
                std::swap(k, l);
            return (k >= i && k <= j) || (i >= k && i <= l);
        }
    }
    return false;
}

template<typename T> inline T foot_pt(const T &line_pt, const T &line_dir, const T &pt)
{
    T      v   = pt - line_pt;
    auto   l2  = line_dir.squaredNorm();
    auto   t   = (l2 == 0) ? 0 : v.dot(line_dir) / l2;
    return line_pt + line_dir * t;
}

inline Vec2d foot_pt(const Line &iline, const Point &ipt)
{
    return foot_pt<Vec2d>(iline.a.cast<double>(), (iline.b - iline.a).cast<double>(), ipt.cast<double>());
}

template<typename T> inline auto ray_point_distance_squared(const T &ray_pt, const T &ray_dir, const T &pt)
{
    return (foot_pt(ray_pt, ray_dir, pt) - pt).squaredNorm();
}

template<typename T> inline auto ray_point_distance(const T &ray_pt, const T &ray_dir, const T &pt)
{
    return (foot_pt(ray_pt, ray_dir, pt) - pt).norm();
}

inline double ray_point_distance_squared(const Line &iline, const Point &ipt)
{
    return (foot_pt(iline, ipt) - ipt.cast<double>()).squaredNorm();
}

inline double ray_point_distance(const Line &iline, const Point &ipt)
{
    return (foot_pt(iline, ipt) - ipt.cast<double>()).norm();
}

// Based on Liang-Barsky function by Daniel White @ http://www.skytopia.com/project/articles/compsci/clipping.html
template<typename T>
inline bool liang_barsky_line_clipping_interval(
    // Start and end points of the source line, result will be stored there as well.
    const Eigen::Matrix<T, 2, 1, Eigen::DontAlign>                  &x0,
    const Eigen::Matrix<T, 2, 1, Eigen::DontAlign>                  &v,
    // Bounding box to clip with.
    const BoundingBoxBase<Eigen::Matrix<T, 2, 1, Eigen::DontAlign>> &bbox,
    std::pair<double, double>                                       &out_interval)
{
    double t0 = 0.0;
    double t1 = 1.0;
    // Traverse through left, right, bottom, top edges.
    auto clip_side = [&t0, &t1](double p, double q) -> bool {
        if (p == 0) {
            if (q < 0)
                // Line parallel to the bounding box edge is fully outside of the bounding box.
                return false;
            // else don't clip
        } else {
            double r = q / p;
            if (p < 0) {
                if (r > t1)
                    // Fully clipped.
                    return false;
                if (r > t0)
                    // Partially clipped.
                    t0 = r;
            } else {
                assert(p > 0);
                if (r < t0)
                    // Fully clipped.
                    return false;
                if (r < t1)
                    // Partially clipped.
                    t1 = r;
            }
        }
        return true;
    };

    if (clip_side(- v.x(), - bbox.min.x() + x0.x()) &&
        clip_side(  v.x(),   bbox.max.x() - x0.x()) &&
        clip_side(- v.y(), - bbox.min.y() + x0.y()) &&
        clip_side(  v.y(),   bbox.max.y() - x0.y())) {
        out_interval.first = t0;
        out_interval.second = t1;
        return true;
    }
    return false;
}

template<typename T>
inline bool liang_barsky_line_clipping(
	// Start and end points of the source line, result will be stored there as well.
	Eigen::Matrix<T, 2, 1, Eigen::DontAlign> 						&x0,
	Eigen::Matrix<T, 2, 1, Eigen::DontAlign> 						&x1,
	// Bounding box to clip with.
	const BoundingBoxBase<Eigen::Matrix<T, 2, 1, Eigen::DontAlign>> &bbox)
{
    Eigen::Matrix<T, 2, 1, Eigen::DontAlign> v = x1 - x0;
    std::pair<double, double> interval;
    if (liang_barsky_line_clipping_interval(x0, v, bbox, interval)) {
        // Clipped successfully.
        x1  = x0 + interval.second * v;
        x0 += interval.first * v;
        return true;
    }
    return false;
}

// Based on Liang-Barsky function by Daniel White @ http://www.skytopia.com/project/articles/compsci/clipping.html
template<typename T>
bool liang_barsky_line_clipping(
	// Start and end points of the source line.
	const Eigen::Matrix<T, 2, 1, Eigen::DontAlign> 					&x0src,
	const Eigen::Matrix<T, 2, 1, Eigen::DontAlign> 					&x1src,
	// Bounding box to clip with.
	const BoundingBoxBase<Eigen::Matrix<T, 2, 1, Eigen::DontAlign>> &bbox,
	// Start and end points of the clipped line.
	Eigen::Matrix<T, 2, 1, Eigen::DontAlign> 						&x0clip,
	Eigen::Matrix<T, 2, 1, Eigen::DontAlign> 						&x1clip)
{
	x0clip = x0src;
	x1clip = x1src;
	return liang_barsky_line_clipping(x0clip, x1clip, bbox);
}

bool directions_parallel(double angle1, double angle2, double max_diff = 0);
bool directions_perpendicular(double angle1, double angle2, double max_diff = 0);
template<class T> bool contains(const std::vector<T> &vector, const Point &point);
template<typename T> T rad2deg(T angle) { return T(180.0) * angle / T(PI); }
double rad2deg_dir(double angle);
template<typename T> constexpr T deg2rad(const T angle) { return T(PI) * angle / T(180.0); }
template<typename T> T angle_to_0_2PI(T angle)
{
    static const T TWO_PI = T(2) * T(PI);
    while (angle < T(0))
    {
        angle += TWO_PI;
    }
    while (TWO_PI < angle)
    {
        angle -= TWO_PI;
    }

    return angle;
}

void simplify_polygons(const Polygons &polygons, double tolerance, Polygons* retval);

double linint(double value, double oldmin, double oldmax, double newmin, double newmax);
bool arrange(
    // input
    size_t num_parts, const Vec2d &part_size, coordf_t gap, const BoundingBoxf* bed_bounding_box, 
    // output
    Pointfs &positions);

// Sets the given transform by assembling the given transformations in the following order:
// 1) mirror
// 2) scale
// 3) rotate X
// 4) rotate Y
// 5) rotate Z
// 6) translate
void assemble_transform(Transform3d& transform, const Vec3d& translation = Vec3d::Zero(), const Vec3d& rotation = Vec3d::Zero(), const Vec3d& scale = Vec3d::Ones(), const Vec3d& mirror = Vec3d::Ones());

// Returns the transform obtained by assembling the given transformations in the following order:
// 1) mirror
// 2) scale
// 3) rotate X
// 4) rotate Y
// 5) rotate Z
// 6) translate
Transform3d assemble_transform(const Vec3d& translation = Vec3d::Zero(), const Vec3d& rotation = Vec3d::Zero(), const Vec3d& scale = Vec3d::Ones(), const Vec3d& mirror = Vec3d::Ones());

// Returns the euler angles extracted from the given rotation matrix
// Warning -> The matrix should not contain any scale or shear !!!
Vec3d extract_euler_angles(const Eigen::Matrix<double, 3, 3, Eigen::DontAlign>& rotation_matrix);

// Returns the euler angles extracted from the given affine transform
// Warning -> The transform should not contain any shear !!!
Vec3d extract_euler_angles(const Transform3d& transform);

// get rotation from two vectors.
// Default output is axis-angle. If rotation_matrix pointer is provided, also output rotation matrix
// Euler angles can be obtained by extract_euler_angles()
void rotation_from_two_vectors(Vec3d from, Vec3d to, Vec3d& rotation_axis, double& phi, Matrix3d* rotation_matrix = nullptr);

class Transformation
{
    struct Flags
    {
        bool dont_translate;
        bool dont_rotate;
        bool dont_scale;
        bool dont_mirror;

        Flags();

        bool needs_update(bool dont_translate, bool dont_rotate, bool dont_scale, bool dont_mirror) const;
        void set(bool dont_translate, bool dont_rotate, bool dont_scale, bool dont_mirror);
    };

    Vec3d m_offset;              // In unscaled coordinates
    Vec3d m_rotation;            // Rotation around the three axes, in radians around mesh center point
    Vec3d m_scaling_factor;      // Scaling factors along the three axes
    Vec3d m_mirror;              // Mirroring along the three axes

    mutable Transform3d m_matrix;
    mutable Flags m_flags;
    mutable bool m_dirty;

public:
    Transformation();
    explicit Transformation(const Transform3d& transform);

    //BBS: add get dirty function
    bool is_dirty() { return m_dirty; }

    const Vec3d& get_offset() const { return m_offset; }
    double get_offset(Axis axis) const { return m_offset(axis); }

    void set_offset(const Vec3d& offset);
    void set_offset(Axis axis, double offset);

    const Vec3d& get_rotation() const { return m_rotation; }
    double get_rotation(Axis axis) const { return m_rotation(axis); }

    void set_rotation(const Vec3d& rotation);
    void set_rotation(Axis axis, double rotation);

    const Vec3d& get_scaling_factor() const { return m_scaling_factor; }
    double get_scaling_factor(Axis axis) const { return m_scaling_factor(axis); }

    void set_scaling_factor(const Vec3d& scaling_factor);
    void set_scaling_factor(Axis axis, double scaling_factor);
    bool is_scaling_uniform() const { return std::abs(m_scaling_factor.x() - m_scaling_factor.y()) < 1e-8 && std::abs(m_scaling_factor.x() - m_scaling_factor.z()) < 1e-8; }

    const Vec3d& get_mirror() const { return m_mirror; }
    double get_mirror(Axis axis) const { return m_mirror(axis); }
    bool is_left_handed() const { return m_mirror.x() * m_mirror.y() * m_mirror.z() < 0.; }

    void set_mirror(const Vec3d& mirror);
    void set_mirror(Axis axis, double mirror);

    void set_from_transform(const Transform3d& transform);

    void reset();

    const Transform3d& get_matrix(bool dont_translate = false, bool dont_rotate = false, bool dont_scale = false, bool dont_mirror = false) const;

    Transformation operator * (const Transformation& other) const;

    // Find volume transformation, so that the chained (instance_trafo * volume_trafo) will be as close to identity
    // as possible in least squares norm in regard to the 8 corners of bbox.
    // Bounding box is expected to be centered around zero in all axes.
    static Transformation volume_to_bed_transformation(const Transformation& instance_transformation, const BoundingBoxf3& bbox);

    // BBS: backup use this compare
    friend bool operator==(Transformation const& l, Transformation const& r) {
        return l.m_offset == r.m_offset && l.m_rotation == r.m_rotation && l.m_scaling_factor == r.m_scaling_factor && l.m_mirror == r.m_mirror;
    }

private:
	friend class cereal::access;
	template<class Archive> void serialize(Archive & ar) { ar(m_offset, m_rotation, m_scaling_factor, m_mirror); }
	explicit Transformation(int) : m_dirty(true) {}
	template <class Archive> static void load_and_construct(Archive &ar, cereal::construct<Transformation> &construct)
	{
		// Calling a private constructor with special "int" parameter to indicate that no construction is necessary.
		construct(1);
		ar(construct.ptr()->m_offset, construct.ptr()->m_rotation, construct.ptr()->m_scaling_factor, construct.ptr()->m_mirror);
	}
};

// For parsing a transformation matrix from 3MF / AMF.
extern Transform3d transform3d_from_string(const std::string& transform_str);

// Rotation when going from the first coordinate system with rotation rot_xyz_from applied
// to a coordinate system with rot_xyz_to applied.
extern Eigen::Quaterniond rotation_xyz_diff(const Vec3d &rot_xyz_from, const Vec3d &rot_xyz_to);
// Rotation by Z to align rot_xyz_from to rot_xyz_to.
// This should only be called if it is known, that the two rotations only differ in rotation around the Z axis.
extern double rotation_diff_z(const Vec3d &rot_xyz_from, const Vec3d &rot_xyz_to);

// Is the angle close to a multiple of 90 degrees?
inline bool is_rotation_ninety_degrees(double a)
{
    a = fmod(std::abs(a), 0.5 * M_PI);
    if (a > 0.25 * PI)
        a = 0.5 * PI - a;
    return a < 0.001;
}

// Is the angle close to a multiple of 90 degrees?
inline bool is_rotation_ninety_degrees(const Vec3d &rotation)
{
    return is_rotation_ninety_degrees(rotation.x()) && is_rotation_ninety_degrees(rotation.y()) && is_rotation_ninety_degrees(rotation.z());
}

} } // namespace Slicer::Geometry

#endif
