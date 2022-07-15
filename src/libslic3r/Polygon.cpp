#include "BoundingBox.hpp"
#include "ClipperUtils.hpp"
#include "Exception.hpp"
#include "Polygon.hpp"
#include "Polyline.hpp"

namespace Slic3r {

Lines Polygon::lines() const
{
    return to_lines(*this);
}

Polyline Polygon::split_at_vertex(const Point &point) const
{
    // find index of point
    for (const Point &pt : this->points)
        if (pt == point)
            return this->split_at_index(int(&pt - &this->points.front()));
    throw Slic3r::InvalidArgument("Point not found");
    return Polyline();
}

// Split a closed polygon into an open polyline, with the split point duplicated at both ends.
Polyline Polygon::split_at_index(int index) const
{
    Polyline polyline;
    polyline.points.reserve(this->points.size() + 1);
    for (Points::const_iterator it = this->points.begin() + index; it != this->points.end(); ++it)
        polyline.points.push_back(*it);
    for (Points::const_iterator it = this->points.begin(); it != this->points.begin() + index + 1; ++it)
        polyline.points.push_back(*it);
    return polyline;
}

double Polygon::area(const Points &points)
{
    double a = 0.;
    if (points.size() >= 3) {
        Vec2d p1 = points.back().cast<double>();
        for (const Point &p : points) {
            Vec2d p2 = p.cast<double>();
            a += cross2(p1, p2);
            p1 = p2;
        }
    }
    return 0.5 * a;
}

double Polygon::area() const
{
    return Polygon::area(points);
}

bool Polygon::is_counter_clockwise() const
{
    return ClipperLib::Orientation(this->points);
}

bool Polygon::is_clockwise() const
{
    return !this->is_counter_clockwise();
}

bool Polygon::make_counter_clockwise()
{
    if (!this->is_counter_clockwise()) {
        this->reverse();
        return true;
    }
    return false;
}

bool Polygon::make_clockwise()
{
    if (this->is_counter_clockwise()) {
        this->reverse();
        return true;
    }
    return false;
}

void Polygon::douglas_peucker(double tolerance)
{
    this->points.push_back(this->points.front());
    Points p = MultiPoint::_douglas_peucker(this->points, tolerance);
    p.pop_back();
    this->points = std::move(p);
}

// Does an unoriented polygon contain a point?
// Tested by counting intersections along a horizontal line.
bool Polygon::contains(const Point &point) const
{
    // http://www.ecse.rpi.edu/Homepages/wrf/Research/Short_Notes/pnpoly.html
    bool result = false;
    Points::const_iterator i = this->points.begin();
    Points::const_iterator j = this->points.end() - 1;
    for (; i != this->points.end(); j = i++) {
        //FIXME this test is not numerically robust. Particularly, it does not handle horizontal segments at y == point(1) well.
        // Does the ray with y == point(1) intersect this line segment?
#if 1
        if ( (((*i)(1) > point(1)) != ((*j)(1) > point(1)))
            && ((double)point(0) < (double)((*j)(0) - (*i)(0)) * (double)(point(1) - (*i)(1)) / (double)((*j)(1) - (*i)(1)) + (double)(*i)(0)) )
            result = !result;
#else
        if (((*i)(1) > point(1)) != ((*j)(1) > point(1))) {
            // Orientation predicated relative to i-th point.
            double orient = (double)(point(0) - (*i)(0)) * (double)((*j)(1) - (*i)(1)) - (double)(point(1) - (*i)(1)) * (double)((*j)(0) - (*i)(0));
            if (((*i)(1) > (*j)(1)) ? (orient > 0.) : (orient < 0.))
                result = !result;
        }
#endif
    }
    return result;
}

// this only works on CCW polygons as CW will be ripped out by Clipper's simplify_polygons()
Polygons Polygon::simplify(double tolerance) const
{
    // repeat first point at the end in order to apply Douglas-Peucker
    // on the whole polygon
    Points points = this->points;
    points.push_back(points.front());
    Polygon p(MultiPoint::_douglas_peucker(points, tolerance));
    p.points.pop_back();
    
    Polygons pp;
    pp.push_back(p);
    return simplify_polygons(pp);
}

void Polygon::simplify(double tolerance, Polygons &polygons) const
{
    Polygons pp = this->simplify(tolerance);
    polygons.reserve(polygons.size() + pp.size());
    polygons.insert(polygons.end(), pp.begin(), pp.end());
}

// Only call this on convex polygons or it will return invalid results
void Polygon::triangulate_convex(Polygons* polygons) const
{
    for (Points::const_iterator it = this->points.begin() + 2; it != this->points.end(); ++it) {
        Polygon p;
        p.points.reserve(3);
        p.points.push_back(this->points.front());
        p.points.push_back(*(it-1));
        p.points.push_back(*it);
        
        // this should be replaced with a more efficient call to a merge_collinear_segments() method
        if (p.area() > 0) polygons->push_back(p);
    }
}

// center of mass
// source: https://en.wikipedia.org/wiki/Centroid
Point Polygon::centroid() const
{
    double area_sum = 0.;
    Vec2d  c(0., 0.);
    if (points.size() >= 3) {
        Vec2d p1 = points.back().cast<double>();
        for (const Point &p : points) {
            Vec2d p2 = p.cast<double>();
            double a = cross2(p1, p2);
            area_sum += a;
            c += (p1 + p2) * a;
            p1 = p2;
        }
    }
    return Point(Vec2d(c / (3. * area_sum)));
}

// find all concave vertices (i.e. having an internal angle greater than the supplied angle)
// (external = right side, thus we consider ccw orientation)
Points Polygon::concave_points(double angle) const
{
    Points points;
    angle = 2. * PI - angle + EPSILON;
    
    // check whether first point forms a concave angle
    if (this->points.front().ccw_angle(this->points.back(), *(this->points.begin()+1)) <= angle)
        points.push_back(this->points.front());
    
    // check whether points 1..(n-1) form concave angles
    for (Points::const_iterator p = this->points.begin()+1; p != this->points.end()-1; ++ p)
        if (p->ccw_angle(*(p-1), *(p+1)) <= angle)
        	points.push_back(*p);
    
    // check whether last point forms a concave angle
    if (this->points.back().ccw_angle(*(this->points.end()-2), this->points.front()) <= angle)
        points.push_back(this->points.back());
    
    return points;
}

// find all convex vertices (i.e. having an internal angle smaller than the supplied angle)
// (external = right side, thus we consider ccw orientation)
Points Polygon::convex_points(double angle) const
{
    Points points;
    angle = 2*PI - angle - EPSILON;
    
    // check whether first point forms a convex angle
    if (this->points.front().ccw_angle(this->points.back(), *(this->points.begin()+1)) >= angle)
        points.push_back(this->points.front());
    
    // check whether points 1..(n-1) form convex angles
    for (Points::const_iterator p = this->points.begin()+1; p != this->points.end()-1; ++p) {
        if (p->ccw_angle(*(p-1), *(p+1)) >= angle) points.push_back(*p);
    }
    
    // check whether last point forms a convex angle
    if (this->points.back().ccw_angle(*(this->points.end()-2), this->points.front()) >= angle)
        points.push_back(this->points.back());
    
    return points;
}

// Projection of a point onto the polygon.
Point Polygon::point_projection(const Point &point) const
{
    Point proj = point;
    double dmin = std::numeric_limits<double>::max();
    if (! this->points.empty()) {
        for (size_t i = 0; i < this->points.size(); ++ i) {
            const Point &pt0 = this->points[i];
            const Point &pt1 = this->points[(i + 1 == this->points.size()) ? 0 : i + 1];
            double d = (point - pt0).cast<double>().norm();
            if (d < dmin) {
                dmin = d;
                proj = pt0;
            }
            d = (point - pt1).cast<double>().norm();
            if (d < dmin) {
                dmin = d;
                proj = pt1;
            }
            Vec2d v1(coordf_t(pt1(0) - pt0(0)), coordf_t(pt1(1) - pt0(1)));
            coordf_t div = v1.squaredNorm();
            if (div > 0.) {
                Vec2d v2(coordf_t(point(0) - pt0(0)), coordf_t(point(1) - pt0(1)));
                coordf_t t = v1.dot(v2) / div;
                if (t > 0. && t < 1.) {
                    Point foot(coord_t(floor(coordf_t(pt0(0)) + t * v1(0) + 0.5)), coord_t(floor(coordf_t(pt0(1)) + t * v1(1) + 0.5)));
                    d = (point - foot).cast<double>().norm();
                    if (d < dmin) {
                        dmin = d;
                        proj = foot;
                    }
                }
            }
        }
    }
    return proj;
}

std::vector<float> Polygon::parameter_by_length() const
{
    // Parametrize the polygon by its length.
    std::vector<float> lengths(points.size()+1, 0.);
    for (size_t i = 1; i < points.size(); ++ i)
        lengths[i] = lengths[i-1] + (points[i] - points[i-1]).cast<float>().norm();
    lengths.back() = lengths[lengths.size()-2] + (points.front() - points.back()).cast<float>().norm();
    return lengths;
}

void Polygon::densify(float min_length, std::vector<float>* lengths_ptr)
{
    std::vector<float> lengths_local;
    std::vector<float>& lengths = lengths_ptr ? *lengths_ptr : lengths_local;

    if (! lengths_ptr) {
        // Length parametrization has not been provided. Calculate our own.
        lengths = this->parameter_by_length();
    }

    assert(points.size() == lengths.size() - 1);

    for (size_t j=1; j<=points.size(); ++j) {
        bool last = j == points.size();
        int i = last ? 0 : j;

        if (lengths[j] - lengths[j-1] > min_length) {
            Point diff = points[i] - points[j-1];
            float diff_len = lengths[j] - lengths[j-1];
            float r = (min_length/diff_len);
            Point new_pt = points[j-1] + Point(r*diff[0], r*diff[1]);
            points.insert(points.begin() + j, new_pt);
            lengths.insert(lengths.begin() + j, lengths[j-1] + min_length);
        }
    }
    assert(points.size() == lengths.size() - 1);
}

Polygon Polygon::transform(const Transform3d& trafo) const
{
    unsigned int vertices_count = (unsigned int)points.size();
    Polygon dstpoly;
    dstpoly.points.resize(vertices_count);
    if (vertices_count == 0)
        return dstpoly;

    unsigned int data_size = 3 * vertices_count * sizeof(float);

    Eigen::MatrixXd src(3, vertices_count);
    for (size_t i = 0; i < vertices_count; i++)
    {
        src.col(i) = Vec3d{ double(points[i].x()), double(points[i].y()),0. };
    }

    Eigen::MatrixXd dst(3, vertices_count);
    dst = trafo * src.colwise().homogeneous();

    for (size_t i = 0; i < vertices_count; i++)
    {
        dstpoly.points[i] = { dst(0,i),dst(1,i) };
    }
    return dstpoly;
}

BoundingBox get_extents(const Polygon &poly) 
{ 
    return poly.bounding_box();
}

BoundingBox get_extents(const Polygons &polygons)
{
    BoundingBox bb;
    if (! polygons.empty()) {
        bb = get_extents(polygons.front());
        for (size_t i = 1; i < polygons.size(); ++ i)
            bb.merge(get_extents(polygons[i]));
    }
    return bb;
}

BoundingBox get_extents_rotated(const Polygon &poly, double angle) 
{ 
    return get_extents_rotated(poly.points, angle);
}

BoundingBox get_extents_rotated(const Polygons &polygons, double angle)
{
    BoundingBox bb;
    if (! polygons.empty()) {
        bb = get_extents_rotated(polygons.front().points, angle);
        for (size_t i = 1; i < polygons.size(); ++ i)
            bb.merge(get_extents_rotated(polygons[i].points, angle));
    }
    return bb;
}

extern std::vector<BoundingBox> get_extents_vector(const Polygons &polygons)
{
    std::vector<BoundingBox> out;
    out.reserve(polygons.size());
    for (Polygons::const_iterator it = polygons.begin(); it != polygons.end(); ++ it)
        out.push_back(get_extents(*it));
    return out;
}

// Polygon must be valid (at least three points), collinear points and duplicate points removed.
bool polygon_is_convex(const Points &poly)
{
    if (poly.size() < 3)
        return false;

    Point p0 = poly[poly.size() - 2];
    Point p1 = poly[poly.size() - 1];
    for (size_t i = 0; i < poly.size(); ++ i) {
        Point p2 = poly[i];
        auto det = cross2((p1 - p0).cast<int64_t>(), (p2 - p1).cast<int64_t>());
        if (det < 0)
            return false;
        p0 = p1;
        p1 = p2;
    }
    return true;
}

bool has_duplicate_points(const Polygons &polys)
{
#if 1
    // Check globally.
    size_t cnt = 0;
    for (const Polygon &poly : polys)
        cnt += poly.points.size();
    std::vector<Point> allpts;
    allpts.reserve(cnt);
    for (const Polygon &poly : polys)
        allpts.insert(allpts.end(), poly.points.begin(), poly.points.end());
    return has_duplicate_points(std::move(allpts));
#else
    // Check per contour.
    for (const Polygon &poly : polys)
        if (has_duplicate_points(poly))
            return true;
    return false;
#endif
}

static inline bool is_stick(const Point &p1, const Point &p2, const Point &p3)
{
    Point v1 = p2 - p1;
    Point v2 = p3 - p2;
    int64_t dir = int64_t(v1(0)) * int64_t(v2(0)) + int64_t(v1(1)) * int64_t(v2(1));
    if (dir > 0)
        // p3 does not turn back to p1. Do not remove p2.
        return false;
    double l2_1 = double(v1(0)) * double(v1(0)) + double(v1(1)) * double(v1(1));
    double l2_2 = double(v2(0)) * double(v2(0)) + double(v2(1)) * double(v2(1));
    if (dir == 0)
        // p1, p2, p3 may make a perpendicular corner, or there is a zero edge length.
        // Remove p2 if it is coincident with p1 or p2.
        return l2_1 == 0 || l2_2 == 0;
    // p3 turns back to p1 after p2. Are p1, p2, p3 collinear?
    // Calculate distance from p3 to a segment (p1, p2) or from p1 to a segment(p2, p3),
    // whichever segment is longer
    double cross = double(v1(0)) * double(v2(1)) - double(v2(0)) * double(v1(1));
    double dist2 = cross * cross / std::max(l2_1, l2_2);
    return dist2 < EPSILON * EPSILON;
}

bool remove_sticks(Polygon &poly)
{
    bool modified = false;
    size_t j = 1;
    for (size_t i = 1; i + 1 < poly.points.size(); ++ i) {
        if (! is_stick(poly[j-1], poly[i], poly[i+1])) {
            // Keep the point.
            if (j < i)
                poly.points[j] = poly.points[i];
            ++ j;
        }
    }
    if (++ j < poly.points.size()) {
        poly.points[j-1] = poly.points.back();
        poly.points.erase(poly.points.begin() + j, poly.points.end());
        modified = true;
    }
    while (poly.points.size() >= 3 && is_stick(poly.points[poly.points.size()-2], poly.points.back(), poly.points.front())) {
        poly.points.pop_back();
        modified = true;
    }
    while (poly.points.size() >= 3 && is_stick(poly.points.back(), poly.points.front(), poly.points[1]))
        poly.points.erase(poly.points.begin());
    return modified;
}

bool remove_sticks(Polygons &polys)
{
    bool modified = false;
    size_t j = 0;
    for (size_t i = 0; i < polys.size(); ++ i) {
        modified |= remove_sticks(polys[i]);
        if (polys[i].points.size() >= 3) {
            if (j < i) 
                std::swap(polys[i].points, polys[j].points);
            ++ j;
        }
    }
    if (j < polys.size())
        polys.erase(polys.begin() + j, polys.end());
    return modified;
}

bool remove_degenerate(Polygons &polys)
{
    bool modified = false;
    size_t j = 0;
    for (size_t i = 0; i < polys.size(); ++ i) {
        if (polys[i].points.size() >= 3) {
            if (j < i) 
                std::swap(polys[i].points, polys[j].points);
            ++ j;
        } else
            modified = true;
    }
    if (j < polys.size())
        polys.erase(polys.begin() + j, polys.end());
    return modified;
}

bool remove_small(Polygons &polys, double min_area)
{
    bool modified = false;
    size_t j = 0;
    for (size_t i = 0; i < polys.size(); ++ i) {
        if (std::abs(polys[i].area()) >= min_area) {
            if (j < i) 
                std::swap(polys[i].points, polys[j].points);
            ++ j;
        } else
            modified = true;
    }
    if (j < polys.size())
        polys.erase(polys.begin() + j, polys.end());
    return modified;
}

void remove_collinear(Polygon &poly)
{
    if (poly.points.size() > 2) {
        // copy points and append both 1 and last point in place to cover the boundaries
        Points pp;
        pp.reserve(poly.points.size()+2);
        pp.push_back(poly.points.back());
        pp.insert(pp.begin()+1, poly.points.begin(), poly.points.end());
        pp.push_back(poly.points.front());
        // delete old points vector. Will be re-filled in the loop
        poly.points.clear();

        size_t i = 0;
        size_t k = 0;
        while (i < pp.size()-2) {
            k = i+1;
            const Point &p1 = pp[i];
            while (k < pp.size()-1) {
                const Point &p2 = pp[k];
                const Point &p3 = pp[k+1];
                Line l(p1, p3);
                if(l.distance_to(p2) < SCALED_EPSILON) {
                    k++;
                } else {
                    if(i > 0) poly.points.push_back(p1); // implicitly removes the first point we appended above
                    i = k;
                    break;
                }
            }
            if(k > pp.size()-2) break; // all remaining points are collinear and can be skipped
        }
        poly.points.push_back(pp[i]);
    }
}

void remove_collinear(Polygons &polys)
{
	for (Polygon &poly : polys)
		remove_collinear(poly);
}

}
