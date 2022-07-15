#ifndef slic3r_ExPolygon_hpp_
#define slic3r_ExPolygon_hpp_

#include "libslic3r.h"
#include "Polygon.hpp"
#include "Polyline.hpp"
#include <vector>

namespace Slic3r {

class ExPolygon;
typedef std::vector<ExPolygon> ExPolygons;

typedef enum SimplifyMethod_ {
    SimplifyMethodDP=0,
    SimplifyMethodVisvalingam,
    SimplifyMethodConcave
}SimplifyMethod;

class ExPolygon
{
public:
    ExPolygon() = default;
	ExPolygon(const ExPolygon &other) = default;
    ExPolygon(ExPolygon &&other) = default;
	explicit ExPolygon(const Polygon &contour) : contour(contour) {}
	explicit ExPolygon(Polygon &&contour) : contour(std::move(contour)) {}
	explicit ExPolygon(const Points &contour) : contour(contour) {}
	explicit ExPolygon(Points &&contour) : contour(std::move(contour)) {}
	explicit ExPolygon(const Polygon &contour, const Polygon &hole) : contour(contour) { holes.emplace_back(hole); }
	explicit ExPolygon(Polygon &&contour, Polygon &&hole) : contour(std::move(contour)) { holes.emplace_back(std::move(hole)); }
	explicit ExPolygon(const Points &contour, const Points &hole) : contour(contour) { holes.emplace_back(hole); }
	explicit ExPolygon(Points &&contour, Polygon &&hole) : contour(std::move(contour)) { holes.emplace_back(std::move(hole)); }
	ExPolygon(std::initializer_list<Point> contour) : contour(contour) {}
	ExPolygon(std::initializer_list<Point> contour, std::initializer_list<Point> hole) : contour(contour), holes({ hole }) {}

    ExPolygon& operator=(const ExPolygon &other) = default;
    ExPolygon& operator=(ExPolygon &&other) = default;

    Polygon  contour;
    Polygons holes;

    operator Points() const;
    operator Polygons() const;
    operator Polylines() const;
    void clear() { contour.points.clear(); holes.clear(); }
    void scale(double factor);
    void translate(double x, double y) { this->translate(Point(coord_t(x), coord_t(y))); }
    void translate(const Point &vector);
    void rotate(double angle);
    void rotate(double angle, const Point &center);
    double area() const;
    bool empty() const { return contour.points.empty(); }
    bool is_valid() const;
    void douglas_peucker(double tolerance);

    // Contains the line / polyline / polylines etc COMPLETELY.
    bool contains(const Line &line) const;
    bool contains(const Polyline &polyline) const;
    bool contains(const Polylines &polylines) const;
    bool contains(const Point &point) const;
    bool contains_b(const Point &point) const;
    bool has_boundary_point(const Point &point) const;

    // Does this expolygon overlap another expolygon?
    // Either the ExPolygons intersect, or one is fully inside the other,
    // and it is not inside a hole of the other expolygon.
    bool overlaps(const ExPolygon &other) const;

    void simplify_p(double tolerance, Polygons* polygons, SimplifyMethod method = SimplifyMethodDP) const;
    Polygons simplify_p(double tolerance, SimplifyMethod method = SimplifyMethodDP) const;
    ExPolygons simplify(double tolerance, SimplifyMethod method = SimplifyMethodDP) const;
    void simplify(double tolerance, ExPolygons* expolygons, SimplifyMethod method = SimplifyMethodDP) const;
    void medial_axis(double max_width, double min_width, ThickPolylines* polylines) const;
    void medial_axis(double max_width, double min_width, Polylines* polylines) const;
    Lines lines() const;

    // Number of contours (outer contour with holes).
    size_t   		num_contours() const { return this->holes.size() + 1; }
    Polygon& 		contour_or_hole(size_t idx) 		{ return (idx == 0) ? this->contour : this->holes[idx - 1]; }
    const Polygon& 	contour_or_hole(size_t idx) const 	{ return (idx == 0) ? this->contour : this->holes[idx - 1]; }
};

inline bool operator==(const ExPolygon &lhs, const ExPolygon &rhs) { return lhs.contour == rhs.contour && lhs.holes == rhs.holes; }
inline bool operator!=(const ExPolygon &lhs, const ExPolygon &rhs) { return lhs.contour != rhs.contour || lhs.holes != rhs.holes; }

// Count a nuber of polygons stored inside the vector of expolygons.
// Useful for allocating space for polygons when converting expolygons to polygons.
inline size_t number_polygons(const ExPolygons &expolys)
{
    size_t n_polygons = 0;
    for (const ExPolygon &ex : expolys)
        n_polygons += ex.holes.size() + 1;
    return n_polygons;
}

inline Lines to_lines(const ExPolygon &src) 
{
    size_t n_lines = src.contour.points.size();
    for (size_t i = 0; i < src.holes.size(); ++ i)
        n_lines += src.holes[i].points.size();
    Lines lines;
    lines.reserve(n_lines);
    for (size_t i = 0; i <= src.holes.size(); ++ i) {
        const Polygon &poly = (i == 0) ? src.contour : src.holes[i - 1];
        for (Points::const_iterator it = poly.points.begin(); it != poly.points.end()-1; ++it)
            lines.push_back(Line(*it, *(it + 1)));
        lines.push_back(Line(poly.points.back(), poly.points.front()));
    }
    return lines;
}

inline Lines to_lines(const ExPolygons &src) 
{
    size_t n_lines = 0;
    for (ExPolygons::const_iterator it_expoly = src.begin(); it_expoly != src.end(); ++ it_expoly) {
        n_lines += it_expoly->contour.points.size();
        for (size_t i = 0; i < it_expoly->holes.size(); ++ i)
            n_lines += it_expoly->holes[i].points.size();
    }
    Lines lines;
    lines.reserve(n_lines);
    for (ExPolygons::const_iterator it_expoly = src.begin(); it_expoly != src.end(); ++ it_expoly) {
        for (size_t i = 0; i <= it_expoly->holes.size(); ++ i) {
            const Points &points = ((i == 0) ? it_expoly->contour : it_expoly->holes[i - 1]).points;
            for (Points::const_iterator it = points.begin(); it != points.end()-1; ++it)
                lines.push_back(Line(*it, *(it + 1)));
            lines.push_back(Line(points.back(), points.front()));
        }
    }
    return lines;
}

inline Polylines to_polylines(const ExPolygon &src)
{
    Polylines polylines;
    polylines.assign(src.holes.size() + 1, Polyline());
    size_t idx = 0;
    Polyline &pl = polylines[idx ++];
    pl.points = src.contour.points;
    pl.points.push_back(pl.points.front());
    for (Polygons::const_iterator ith = src.holes.begin(); ith != src.holes.end(); ++ith) {
        Polyline &pl = polylines[idx ++];
        pl.points = ith->points;
        pl.points.push_back(ith->points.front());
    }
    assert(idx == polylines.size());
    return polylines;
}

inline Polylines to_polylines(const ExPolygons &src)
{
    Polylines polylines;
    polylines.assign(number_polygons(src), Polyline());
    size_t idx = 0;
    for (ExPolygons::const_iterator it = src.begin(); it != src.end(); ++it) {
        Polyline &pl = polylines[idx ++];
        pl.points = it->contour.points;
        pl.points.push_back(pl.points.front());
        for (Polygons::const_iterator ith = it->holes.begin(); ith != it->holes.end(); ++ith) {
            Polyline &pl = polylines[idx ++];
            pl.points = ith->points;
            pl.points.push_back(ith->points.front());
        }
    }
    assert(idx == polylines.size());
    return polylines;
}

inline Polylines to_polylines(ExPolygon &&src)
{
    Polylines polylines;
    polylines.assign(src.holes.size() + 1, Polyline());
    size_t idx = 0;
    Polyline &pl = polylines[idx ++];
    pl.points = std::move(src.contour.points);
    pl.points.push_back(pl.points.front());
    for (Polygons::const_iterator ith = src.holes.begin(); ith != src.holes.end(); ++ith) {
        Polyline &pl = polylines[idx ++];
        pl.points = std::move(ith->points);
        pl.points.push_back(ith->points.front());
    }
    assert(idx == polylines.size());
    return polylines;
}

inline Polylines to_polylines(ExPolygons &&src)
{
    Polylines polylines;
    polylines.assign(number_polygons(src), Polyline());
    size_t idx = 0;
    for (ExPolygons::const_iterator it = src.begin(); it != src.end(); ++it) {
        Polyline &pl = polylines[idx ++];
        pl.points = std::move(it->contour.points);
        pl.points.push_back(pl.points.front());
        for (Polygons::const_iterator ith = it->holes.begin(); ith != it->holes.end(); ++ith) {
            Polyline &pl = polylines[idx ++];
            pl.points = std::move(ith->points);
            pl.points.push_back(ith->points.front());
        }
    }
    assert(idx == polylines.size());
    return polylines;
}

inline Polygons to_polygons(const ExPolygon &src)
{
    Polygons polygons;
    polygons.reserve(src.holes.size() + 1);
    polygons.push_back(src.contour);
    polygons.insert(polygons.end(), src.holes.begin(), src.holes.end());
    return polygons;
}

inline Polygons to_polygons(const ExPolygons &src)
{
    Polygons polygons;
    polygons.reserve(number_polygons(src));
    for (ExPolygons::const_iterator it = src.begin(); it != src.end(); ++it) {
        polygons.push_back(it->contour);
        polygons.insert(polygons.end(), it->holes.begin(), it->holes.end());
    }
    return polygons;
}

inline ConstPolygonPtrs to_polygon_ptrs(const ExPolygon &src)
{
    ConstPolygonPtrs polygons;
    polygons.reserve(src.holes.size() + 1);
    polygons.emplace_back(&src.contour);
    for (const Polygon &hole : src.holes)
        polygons.emplace_back(&hole);
    return polygons;
}

inline ConstPolygonPtrs to_polygon_ptrs(const ExPolygons &src)
{
    ConstPolygonPtrs polygons;
    polygons.reserve(number_polygons(src));
    for (const ExPolygon &expoly : src) {
        polygons.emplace_back(&expoly.contour);
        for (const Polygon &hole : expoly.holes)
            polygons.emplace_back(&hole);
    }
    return polygons;
}

inline Polygons to_polygons(ExPolygon &&src)
{
    Polygons polygons;
    polygons.reserve(src.holes.size() + 1);
    polygons.push_back(std::move(src.contour));
    std::move(std::begin(src.holes), std::end(src.holes), std::back_inserter(polygons));
    src.holes.clear();
    return polygons;
}

inline Polygons to_polygons(ExPolygons &&src)
{
    Polygons polygons;
    polygons.reserve(number_polygons(src));
    for (ExPolygons::iterator it = src.begin(); it != src.end(); ++it) {
        polygons.push_back(std::move(it->contour));
        std::move(std::begin(it->holes), std::end(it->holes), std::back_inserter(polygons));
        it->holes.clear();
    }
    return polygons;
}

inline ExPolygons to_expolygons(const Polygons &polys)
{
    ExPolygons ex_polys;
    ex_polys.assign(polys.size(), ExPolygon());
    for (size_t idx = 0; idx < polys.size(); ++idx)
        ex_polys[idx].contour = polys[idx];
    return ex_polys;
}

inline ExPolygons to_expolygons(Polygons &&polys)
{
    ExPolygons ex_polys;
    ex_polys.assign(polys.size(), ExPolygon());
    for (size_t idx = 0; idx < polys.size(); ++idx)
        ex_polys[idx].contour = std::move(polys[idx]);
    return ex_polys;
}

inline void polygons_append(Polygons &dst, const ExPolygon &src) 
{ 
    dst.reserve(dst.size() + src.holes.size() + 1);
    dst.push_back(src.contour);
    dst.insert(dst.end(), src.holes.begin(), src.holes.end());
}

inline void polygons_append(Polygons &dst, const ExPolygons &src) 
{ 
    dst.reserve(dst.size() + number_polygons(src));
    for (ExPolygons::const_iterator it = src.begin(); it != src.end(); ++ it) {
        dst.push_back(it->contour);
        dst.insert(dst.end(), it->holes.begin(), it->holes.end());
    }
}

inline void polygons_append(Polygons &dst, ExPolygon &&src)
{ 
    dst.reserve(dst.size() + src.holes.size() + 1);
    dst.push_back(std::move(src.contour));
    std::move(std::begin(src.holes), std::end(src.holes), std::back_inserter(dst));
    src.holes.clear();
}

inline void polygons_append(Polygons &dst, ExPolygons &&src)
{ 
    dst.reserve(dst.size() + number_polygons(src));
    for (ExPolygons::iterator it = src.begin(); it != src.end(); ++ it) {
        dst.push_back(std::move(it->contour));
        std::move(std::begin(it->holes), std::end(it->holes), std::back_inserter(dst));
        it->holes.clear();
    }
}

inline void expolygons_append(ExPolygons &dst, const ExPolygons &src) 
{ 
    dst.insert(dst.end(), src.begin(), src.end());
}

inline void expolygons_append(ExPolygons &dst, ExPolygons &&src)
{ 
    if (dst.empty()) {
        dst = std::move(src);
    } else {
        std::move(std::begin(src), std::end(src), std::back_inserter(dst));
        src.clear();
    }
}

inline void expolygons_rotate(ExPolygons &expolys, double angle)
{
    for (ExPolygons::iterator p = expolys.begin(); p != expolys.end(); ++p)
        p->rotate(angle);
}

inline bool expolygons_contain(ExPolygons &expolys, const Point &pt)
{
    for (ExPolygons::iterator p = expolys.begin(); p != expolys.end(); ++p)
        if (p->contains(pt))
            return true;
    return false;
}

inline ExPolygons expolygons_simplify(const ExPolygons &expolys, double tolerance)
{
	ExPolygons out;
	out.reserve(expolys.size());
	for (const ExPolygon &exp : expolys)
		exp.simplify(tolerance, &out);
	return out;
}

BoundingBox get_extents(const ExPolygon &expolygon);
BoundingBox get_extents(const ExPolygons &expolygons);
BoundingBox get_extents_rotated(const ExPolygon &poly, double angle);
BoundingBox get_extents_rotated(const ExPolygons &polygons, double angle);
std::vector<BoundingBox> get_extents_vector(const ExPolygons &polygons);

// Test for duplicate points. The points are copied, sorted and checked for duplicates globally.
bool has_duplicate_points(const ExPolygon &expoly);
bool has_duplicate_points(const ExPolygons &expolys);

bool remove_sticks(ExPolygon &poly);
void keep_largest_contour_only(ExPolygons &polygons);

inline double      area(const ExPolygon &poly) { return poly.area(); }
inline double      area(const ExPolygons &polys) { double s = 0.; for (auto &p : polys) s += p.area(); return s; }

// Removes all expolygons smaller than min_area and also removes all holes smaller than min_area
bool        remove_small_and_small_holes(ExPolygons &expolygons, double min_area);

} // namespace Slic3r

// start Boost
#include <boost/polygon/polygon.hpp>
namespace boost { namespace polygon {
    template <>
        struct polygon_traits<Slic3r::ExPolygon> {
        typedef coord_t coordinate_type;
        typedef Slic3r::Points::const_iterator iterator_type;
        typedef Slic3r::Point point_type;

        // Get the begin iterator
        static inline iterator_type begin_points(const Slic3r::ExPolygon& t) {
            return t.contour.points.begin();
        }

        // Get the end iterator
        static inline iterator_type end_points(const Slic3r::ExPolygon& t) {
            return t.contour.points.end();
        }

        // Get the number of sides of the polygon
        static inline std::size_t size(const Slic3r::ExPolygon& t) {
            return t.contour.points.size();
        }

        // Get the winding direction of the polygon
        static inline winding_direction winding(const Slic3r::ExPolygon& /* t */) {
            return unknown_winding;
        }
    };

    template <>
    struct polygon_mutable_traits<Slic3r::ExPolygon> {
        //expects stl style iterators
        template <typename iT>
        static inline Slic3r::ExPolygon& set_points(Slic3r::ExPolygon& expolygon, iT input_begin, iT input_end) {
            expolygon.contour.points.assign(input_begin, input_end);
            // skip last point since Boost will set last point = first point
            expolygon.contour.points.pop_back();
            return expolygon;
        }
    };
    
    
    template <>
    struct geometry_concept<Slic3r::ExPolygon> { typedef polygon_with_holes_concept type; };

    template <>
    struct polygon_with_holes_traits<Slic3r::ExPolygon> {
        typedef Slic3r::Polygons::const_iterator iterator_holes_type;
        typedef Slic3r::Polygon hole_type;
        static inline iterator_holes_type begin_holes(const Slic3r::ExPolygon& t) {
            return t.holes.begin();
        }
        static inline iterator_holes_type end_holes(const Slic3r::ExPolygon& t) {
            return t.holes.end();
        }
        static inline unsigned int size_holes(const Slic3r::ExPolygon& t) {
            return (int)t.holes.size();
        }
    };

    template <>
    struct polygon_with_holes_mutable_traits<Slic3r::ExPolygon> {
         template <typename iT>
         static inline Slic3r::ExPolygon& set_holes(Slic3r::ExPolygon& t, iT inputBegin, iT inputEnd) {
              t.holes.assign(inputBegin, inputEnd);
              return t;
         }
    };
    
    //first we register CPolygonSet as a polygon set
    template <>
    struct geometry_concept<Slic3r::ExPolygons> { typedef polygon_set_concept type; };

    //next we map to the concept through traits
    template <>
    struct polygon_set_traits<Slic3r::ExPolygons> {
        typedef coord_t coordinate_type;
        typedef Slic3r::ExPolygons::const_iterator iterator_type;
        typedef Slic3r::ExPolygons operator_arg_type;

        static inline iterator_type begin(const Slic3r::ExPolygons& polygon_set) {
            return polygon_set.begin();
        }

        static inline iterator_type end(const Slic3r::ExPolygons& polygon_set) {
            return polygon_set.end();
        }

        //don't worry about these, just return false from them
        static inline bool clean(const Slic3r::ExPolygons& /* polygon_set */) { return false; }
        static inline bool sorted(const Slic3r::ExPolygons& /* polygon_set */) { return false; }
    };

    template <>
    struct polygon_set_mutable_traits<Slic3r::ExPolygons> {
        template <typename input_iterator_type>
        static inline void set(Slic3r::ExPolygons& expolygons, input_iterator_type input_begin, input_iterator_type input_end) {
            expolygons.assign(input_begin, input_end);
        }
    };
} }
// end Boost

#endif
