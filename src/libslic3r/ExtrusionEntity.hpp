#ifndef slic3r_ExtrusionEntity_hpp_
#define slic3r_ExtrusionEntity_hpp_

#include "libslic3r.h"
#include "BoundingBox.hpp"
#include "Polygon.hpp"
#include "Polyline.hpp"

#include <assert.h>
#include <string_view>
#include <numeric>

namespace Slic3r {

class ExPolygon;
using ExPolygons = std::vector<ExPolygon>;
class ExtrusionEntityCollection;
class Extruder;


struct NodeContour
{
    Points   pts; //for lines contour
    std::vector<coord_t> widths;
    bool is_loop;
};

struct LoopNode
{
    //store outer wall and mark if it's loop
    NodeContour node_contour;
    int       node_id;
    int       loop_id = 0;
    BoundingBox bbox;
    int       merged_id = -1;

    //upper loop info
    std::vector<int> upper_node_id;

    //lower loop info
    std::vector<int> lower_node_id;
};

// Each ExtrusionRole value identifies a distinct set of { extruder, speed }
enum ExtrusionRole : uint8_t {
    erNone,
    erPerimeter,
    erExternalPerimeter,
    erOverhangPerimeter,
    erInternalInfill,
    erSolidInfill,
    erFloatingVerticalShell,
    erTopSolidInfill,
    erBottomSurface,
    erIroning,
    erBridgeInfill,
    erGapFill,
    erSkirt,
    erBrim,
    erSupportMaterial,
    erSupportMaterialInterface,
    erSupportTransition,
    erWipeTower,
    erCustom,
    erFlush,
    // Extrusion role for a collection with multiple extrusion roles.
    erMixed,
    erCount
};

enum CustomizeFlag : uint8_t {
    cfNone,
    cfCircleCompensation,   // shaft hole tolerance compensation
    cfFloatingVerticalShell
};

// Special flags describing loop
enum ExtrusionLoopRole {
    elrDefault                     = 1 << 0,
    elrContourInternalPerimeter    = 1 << 1,
    elrSkirt                       = 1 << 2,
    elrPerimeterHole               = 1 << 3,
    elrSecondPerimeter             = 1 << 4
};

inline ExtrusionLoopRole operator |(ExtrusionLoopRole a, ExtrusionLoopRole b) {
    return static_cast<ExtrusionLoopRole>(static_cast<int>(a) | static_cast<int>(b));
}


inline bool is_perimeter(ExtrusionRole role)
{
    return role == erPerimeter
        || role == erExternalPerimeter
        || role == erOverhangPerimeter;
}

inline bool is_infill(ExtrusionRole role)
{
    return role == erBridgeInfill
        || role == erInternalInfill
        || role == erSolidInfill
        || role == erFloatingVerticalShell
        || role == erTopSolidInfill
        || role == erBottomSurface
        || role == erIroning;
}

inline bool is_top_surface(ExtrusionRole role)
{
    return role == erTopSolidInfill;
}

inline bool is_solid_infill(ExtrusionRole role)
{
    return role == erBridgeInfill
        || role == erSolidInfill
        || role == erFloatingVerticalShell
        || role == erTopSolidInfill
        || role == erBottomSurface
        || role == erIroning;
}

inline bool is_bridge(ExtrusionRole role) {
    return role == erBridgeInfill
        || role == erOverhangPerimeter;
}


inline bool is_support(ExtrusionRole role) {
    return role == erSupportMaterial
        || role == erSupportMaterialInterface
        || role == erSupportTransition;
}

class ExtrusionEntity
{
public:
    ExtrusionEntity() = default;
    ExtrusionEntity(const ExtrusionEntity &rhs){
        m_customize_flag = rhs.m_customize_flag;
        m_cooling_node   = rhs.m_cooling_node;
    };
    ExtrusionEntity(ExtrusionEntity &&rhs){
        m_customize_flag = rhs.m_customize_flag;
        m_cooling_node   = rhs.m_cooling_node;
    };
    ExtrusionEntity &operator=(const ExtrusionEntity &rhs)
    {
        m_customize_flag = rhs.m_customize_flag;
        m_cooling_node   = rhs.m_cooling_node;
        return *this;
    }
    ExtrusionEntity &operator=(ExtrusionEntity &&rhs)
    {
        m_customize_flag = rhs.m_customize_flag;
        m_cooling_node   = rhs.m_cooling_node;
        return *this;
    }

    virtual ExtrusionRole role() const = 0;
    virtual bool is_collection() const { return false; }
    virtual bool is_loop() const { return false; }
    virtual bool can_reverse() const { return true; }
    virtual bool can_sort() const { return true; }//BBS: only used in ExtrusionEntityCollection
    virtual void set_reverse() {}
    virtual ExtrusionEntity* clone() const = 0;
    // Create a new object, initialize it with this object using the move semantics.
    virtual ExtrusionEntity* clone_move() = 0;
    virtual ~ExtrusionEntity() {}
    virtual void reverse() = 0;
    virtual const Point& first_point() const = 0;
    virtual const Point& last_point() const = 0;
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion width.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    virtual void polygons_covered_by_width(Polygons &out, const float scaled_epsilon) const = 0;
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion spacing.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    // Useful to calculate area of an infill, which has been really filled in by a 100% rectilinear infill.
    virtual void polygons_covered_by_spacing(Polygons &out, const float scaled_epsilon) const = 0;
    Polygons polygons_covered_by_width(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_width(out, scaled_epsilon); return out; }
    Polygons polygons_covered_by_spacing(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_spacing(out, scaled_epsilon); return out; }
    // Minimum volumetric velocity of this extrusion entity. Used by the constant nozzle pressure algorithm.
    virtual double min_mm3_per_mm() const = 0;
    virtual Polyline as_polyline() const = 0;
    virtual void   collect_polylines(Polylines &dst) const = 0;
    virtual void   collect_points(Points &dst) const = 0;
    virtual Polylines as_polylines() const { Polylines dst; this->collect_polylines(dst); return dst; }
    virtual double length() const = 0;
    virtual double total_volume() const = 0;

    static std::string role_to_string(ExtrusionRole role);
    static ExtrusionRole string_to_role(const std::string_view role);

    virtual CustomizeFlag get_customize_flag() const { return m_customize_flag; };
    virtual void set_customize_flag(CustomizeFlag flag) { m_customize_flag = flag; };

    virtual int  get_cooling_node() const { return m_cooling_node; };
    virtual void set_cooling_node(int id) { m_cooling_node = id; };

protected:
    CustomizeFlag m_customize_flag{CustomizeFlag::cfNone};
    int           m_cooling_node{ -1 };
};

typedef std::vector<ExtrusionEntity*> ExtrusionEntitiesPtr;

class ExtrusionPath : public ExtrusionEntity
{
public:
    Polyline polyline;
    double overhang_degree = 0;
    int curve_degree = 0;
    // Volumetric velocity. mm^3 of plastic per mm of linear head motion. Used by the G-code generator.
    double mm3_per_mm;
    // Width of the extrusion, used for visualization purposes.
    float width;
    // Height of the extrusion, used for visualization purposes.
    float height;
    double smooth_speed = 0;

    ExtrusionPath() : mm3_per_mm(-1), width(-1), height(-1), m_role(erNone), m_no_extrusion(false) {}
    ExtrusionPath(ExtrusionRole role) : mm3_per_mm(-1), width(-1), height(-1), m_role(role), m_no_extrusion(false) {}
    ExtrusionPath(ExtrusionRole role, double mm3_per_mm, float width, float height, bool no_extrusion = false) : mm3_per_mm(mm3_per_mm), width(width), height(height), m_role(role), m_no_extrusion(no_extrusion) {}
    ExtrusionPath(double overhang_degree, int curve_degree, ExtrusionRole role, double mm3_per_mm, float width, float height) : overhang_degree(overhang_degree), curve_degree(curve_degree), mm3_per_mm(mm3_per_mm), width(width), height(height), m_role(role) {}

    ExtrusionPath(const ExtrusionPath &rhs)
        : ExtrusionEntity(rhs)
        , polyline(rhs.polyline)
        , overhang_degree(rhs.overhang_degree)
        , curve_degree(rhs.curve_degree)
        , mm3_per_mm(rhs.mm3_per_mm)
        , width(rhs.width)
        , height(rhs.height)
        , smooth_speed(rhs.smooth_speed)
        , m_can_reverse(rhs.m_can_reverse)
        , m_role(rhs.m_role)
        , m_no_extrusion(rhs.m_no_extrusion)
    {}
    ExtrusionPath(ExtrusionPath &&rhs)
        : ExtrusionEntity(rhs)
        , polyline(std::move(rhs.polyline))
        , overhang_degree(rhs.overhang_degree)
        , curve_degree(rhs.curve_degree)
        , mm3_per_mm(rhs.mm3_per_mm)
        , width(rhs.width)
        , height(rhs.height)
        , smooth_speed(rhs.smooth_speed)
        , m_can_reverse(rhs.m_can_reverse)
        , m_role(rhs.m_role)
        , m_no_extrusion(rhs.m_no_extrusion)
    {}
    ExtrusionPath(const Polyline &polyline, const ExtrusionPath &rhs)
        : ExtrusionEntity(rhs)
        , polyline(polyline)
        , overhang_degree(rhs.overhang_degree)
        , curve_degree(rhs.curve_degree)
        , mm3_per_mm(rhs.mm3_per_mm)
        , width(rhs.width)
        , height(rhs.height)
        , smooth_speed(rhs.smooth_speed)
        , m_can_reverse(rhs.m_can_reverse)
        , m_role(rhs.m_role)
        , m_no_extrusion(rhs.m_no_extrusion)
    {}
    ExtrusionPath(Polyline &&polyline, const ExtrusionPath &rhs)
        : ExtrusionEntity(rhs)
        , polyline(std::move(polyline))
        , overhang_degree(rhs.overhang_degree)
        , curve_degree(rhs.curve_degree)
        , mm3_per_mm(rhs.mm3_per_mm)
        , width(rhs.width)
        , height(rhs.height)
        , smooth_speed(rhs.smooth_speed)
        , m_can_reverse(rhs.m_can_reverse)
        , m_role(rhs.m_role)
        , m_no_extrusion(rhs.m_no_extrusion)
    {}

    ExtrusionPath& operator=(const ExtrusionPath& rhs) {
        ExtrusionEntity::operator=(rhs);
        m_can_reverse = rhs.m_can_reverse;
        m_role = rhs.m_role;
        m_no_extrusion = rhs.m_no_extrusion;
        this->mm3_per_mm = rhs.mm3_per_mm;
        this->width = rhs.width;
        this->height = rhs.height;
        this->smooth_speed = rhs.smooth_speed;
        this->overhang_degree = rhs.overhang_degree;
        this->curve_degree = rhs.curve_degree;
        this->polyline = rhs.polyline;
        return *this;
    }
    ExtrusionPath& operator=(ExtrusionPath&& rhs) {
        ExtrusionEntity::operator=(rhs);
        m_can_reverse = rhs.m_can_reverse;
        m_role = rhs.m_role;
        m_no_extrusion = rhs.m_no_extrusion;
        this->mm3_per_mm = rhs.mm3_per_mm;
        this->width = rhs.width;
        this->height = rhs.height;
        this->smooth_speed    = rhs.smooth_speed;
        this->overhang_degree = rhs.overhang_degree;
        this->curve_degree = rhs.curve_degree;
        this->polyline = std::move(rhs.polyline);
        return *this;
    }

	ExtrusionEntity* clone() const override { return new ExtrusionPath(*this); }
    // Create a new object, initialize it with this object using the move semantics.
	ExtrusionEntity* clone_move() override { return new ExtrusionPath(std::move(*this)); }
    void reverse() override { this->polyline.reverse(); }
    const Point& first_point() const override { return this->polyline.points.front(); }
    const Point& last_point() const override { return this->polyline.points.back(); }
    size_t size() const { return this->polyline.size(); }
    bool empty() const { return this->polyline.empty(); }
    bool is_closed() const { return ! this->empty() && this->polyline.points.front() == this->polyline.points.back(); }
    // Produce a list of extrusion paths into retval by clipping this path by ExPolygons.
    // Currently not used.
    void intersect_expolygons(const ExPolygons &collection, ExtrusionEntityCollection* retval) const;
    // Produce a list of extrusion paths into retval by removing parts of this path by ExPolygons.
    // Currently not used.
    void subtract_expolygons(const ExPolygons &collection, ExtrusionEntityCollection* retval) const;
    void clip_end(double distance);
    void simplify(double tolerance);
    double length() const override;
    ExtrusionRole role() const override { return m_role; }
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion width.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    void polygons_covered_by_width(Polygons &out, const float scaled_epsilon) const override;
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion spacing.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    // Useful to calculate area of an infill, which has been really filled in by a 100% rectilinear infill.
    void polygons_covered_by_spacing(Polygons &out, const float scaled_epsilon) const override;
    Polygons polygons_covered_by_width(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_width(out, scaled_epsilon); return out; }
    Polygons polygons_covered_by_spacing(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_spacing(out, scaled_epsilon); return out; }
    // Minimum volumetric velocity of this extrusion entity. Used by the constant nozzle pressure algorithm.
    double min_mm3_per_mm() const override { return this->mm3_per_mm; }
    Polyline as_polyline() const override { return this->polyline; }
    void   collect_polylines(Polylines &dst) const override { if (! this->polyline.empty()) dst.emplace_back(this->polyline); }
    void   collect_points(Points &dst) const override { append(dst, this->polyline.points); }
    double total_volume() const override { return mm3_per_mm * unscale<double>(length()); }

    void set_overhang_degree(int overhang) {
        if (is_perimeter(m_role) || is_support(m_role))
            overhang_degree = (overhang < 0)?0:(overhang > 10 ? 10 : overhang);
    };
    int get_overhang_degree() const {
        // only perimeter has overhang degree. Other return 0;
        if (is_perimeter(m_role) || is_support(m_role))
            return (int)overhang_degree;
        return 0;
    };
    void set_curve_degree(int curve) {
        curve_degree = (curve < 0)?0:(curve > 10 ? 10 : curve);
    };
    int get_curve_degree() const {
        return curve_degree;
    };
    //BBS: add new simplifing method by fitting arc
    void simplify_by_fitting_arc(double tolerance);
    //BBS:
    bool is_force_no_extrusion() const { return m_no_extrusion; }
    void set_force_no_extrusion(bool no_extrusion) { m_no_extrusion = no_extrusion; }
    void set_extrusion_role(ExtrusionRole extrusion_role) { m_role = extrusion_role; }
    void set_reverse() override { m_can_reverse = false; }
    bool can_reverse() const override { return m_can_reverse; }

    bool can_merge(const ExtrusionPath& other);

private:
    void _inflate_collection(const Polylines &polylines, ExtrusionEntityCollection* collection) const;
    bool m_can_reverse = true;
    ExtrusionRole m_role;
    //BBS
    bool m_no_extrusion = false;
};

class ExtrusionPathSloped : public ExtrusionPath
{
public:
    struct Slope
    {
        double z_ratio{1.};
        double e_ratio{1.};
        double speed_record{0.0};
    };

    Slope slope_begin;
    Slope slope_end;
    ExtrusionPathSloped(const ExtrusionPath &rhs, const Slope &begin, const Slope &end) : ExtrusionPath(rhs), slope_begin(begin), slope_end(end) {}
    ExtrusionPathSloped(ExtrusionPath &&rhs, const Slope &begin, const Slope &end) : ExtrusionPath(std::move(rhs)), slope_begin(begin), slope_end(end) {}
    ExtrusionPathSloped(const Polyline &polyline, const ExtrusionPath &rhs, const Slope &begin, const Slope &end) : ExtrusionPath(polyline, rhs), slope_begin(begin), slope_end(end)
    {}
    ExtrusionPathSloped(Polyline &&polyline, const ExtrusionPath &rhs, const Slope &begin, const Slope &end) : ExtrusionPath(std::move(polyline), rhs), slope_begin(begin), slope_end(end)
    {}

    Slope interpolate(const double ratio) const {
        return {
            lerp(slope_begin.z_ratio, slope_end.z_ratio, ratio),
            lerp(slope_begin.e_ratio, slope_end.e_ratio, ratio),
            lerp(slope_begin.speed_record, slope_end.speed_record, ratio),
        };
    }

    bool is_flat() const { return is_approx(slope_begin.z_ratio, slope_end.z_ratio); }
};

class ExtrusionPathOriented : public ExtrusionPath
{
public:
    ExtrusionPathOriented(ExtrusionRole role, double mm3_per_mm, float width, float height) : ExtrusionPath(role, mm3_per_mm, width, height) {}
    ExtrusionEntity* clone() const override { return new ExtrusionPathOriented(*this); }
    // Create a new object, initialize it with this object using the move semantics.
    ExtrusionEntity* clone_move() override { return new ExtrusionPathOriented(std::move(*this)); }
    virtual bool can_reverse() const override { return false; }
};

typedef std::vector<ExtrusionPath> ExtrusionPaths;

// Single continuous extrusion path, possibly with varying extrusion thickness, extrusion height or bridging / non bridging.
class ExtrusionMultiPath : public ExtrusionEntity
{
public:
    ExtrusionPaths paths;

    ExtrusionMultiPath() {}
    ExtrusionMultiPath(const ExtrusionMultiPath &rhs) : paths(rhs.paths), m_can_reverse(rhs.m_can_reverse) {}
    ExtrusionMultiPath(ExtrusionMultiPath &&rhs) : paths(std::move(rhs.paths)), m_can_reverse(rhs.m_can_reverse) {}
    ExtrusionMultiPath(const ExtrusionPaths &paths) : paths(paths) {}
    ExtrusionMultiPath(const ExtrusionPath &path) {this->paths.push_back(path); m_can_reverse = path.can_reverse(); }

    ExtrusionMultiPath &operator=(const ExtrusionMultiPath &rhs)
    {
        this->paths   = rhs.paths;
        m_can_reverse = rhs.m_can_reverse;
        return *this;
    }
    ExtrusionMultiPath &operator=(ExtrusionMultiPath &&rhs)
    {
        this->paths   = std::move(rhs.paths);
        m_can_reverse = rhs.m_can_reverse;
        return *this;
    }

    bool is_loop() const override { return false; }
    bool can_reverse() const override { return m_can_reverse; }
    void set_reverse() override { m_can_reverse = false; }
	ExtrusionEntity* clone() const override { return new ExtrusionMultiPath(*this); }
    // Create a new object, initialize it with this object using the move semantics.
	ExtrusionEntity* clone_move() override { return new ExtrusionMultiPath(std::move(*this)); }
    void reverse() override;
    const Point& first_point() const override { return this->paths.front().polyline.points.front(); }
    const Point& last_point() const override { return this->paths.back().polyline.points.back(); }
    size_t size() const { return this->paths.size(); }
    bool empty() const { return this->paths.empty(); }
    double length() const override;
    ExtrusionRole role() const override { return this->paths.empty() ? erNone : this->paths.front().role(); }
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion width.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    void polygons_covered_by_width(Polygons &out, const float scaled_epsilon) const override;
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion spacing.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    // Useful to calculate area of an infill, which has been really filled in by a 100% rectilinear infill.
    void polygons_covered_by_spacing(Polygons &out, const float scaled_epsilon) const override;
    Polygons polygons_covered_by_width(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_width(out, scaled_epsilon); return out; }
    Polygons polygons_covered_by_spacing(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_spacing(out, scaled_epsilon); return out; }
    // Minimum volumetric velocity of this extrusion entity. Used by the constant nozzle pressure algorithm.
    double min_mm3_per_mm() const override;
    Polyline as_polyline() const override;
    void   collect_polylines(Polylines &dst) const override { Polyline pl = this->as_polyline(); if (! pl.empty()) dst.emplace_back(std::move(pl)); }
    void   collect_points(Points &dst) const override {
        size_t n = std::accumulate(paths.begin(), paths.end(), 0, [](const size_t n, const ExtrusionPath &p){ return n + p.polyline.size(); });
        dst.reserve(dst.size() + n);
        for (const ExtrusionPath &p : this->paths)
            append(dst, p.polyline.points);
    }
    double total_volume() const override { double volume =0.; for (const auto& path : paths) volume += path.total_volume(); return volume; }

private:
    bool m_can_reverse = true;
};

// Single continuous extrusion loop, possibly with varying extrusion thickness, extrusion height or bridging / non bridging.
class ExtrusionLoop : public ExtrusionEntity
{
public:
    ExtrusionPaths paths;

    ExtrusionLoop(ExtrusionLoopRole role = elrDefault) : m_loop_role(role) {}
    ExtrusionLoop(const ExtrusionPaths &paths, ExtrusionLoopRole role = elrDefault) : paths(paths), m_loop_role(role) {}
    ExtrusionLoop(ExtrusionPaths &&paths, ExtrusionLoopRole role = elrDefault) : paths(std::move(paths)), m_loop_role(role) {}
    ExtrusionLoop(ExtrusionPaths &&paths, ExtrusionLoopRole role, CustomizeFlag flag) : paths(std::move(paths)), m_loop_role(role) { m_customize_flag = flag; }
    ExtrusionLoop(const ExtrusionPath &path, ExtrusionLoopRole role = elrDefault) : m_loop_role(role)
        { this->paths.push_back(path); }
    ExtrusionLoop(const ExtrusionPath &&path, ExtrusionLoopRole role = elrDefault) : m_loop_role(role)
        { this->paths.emplace_back(std::move(path)); }
    bool is_loop() const override{ return true; }
    bool can_reverse() const override { return false; }
	ExtrusionEntity* clone() const override{ return new ExtrusionLoop (*this); }
    // Create a new object, initialize it with this object using the move semantics.
	ExtrusionEntity* clone_move() override { return new ExtrusionLoop(std::move(*this)); }
    bool make_clockwise();
    bool make_counter_clockwise();
    bool is_clockwise() { return this->polygon().is_clockwise(); }
    bool is_counter_clockwise() { return this->polygon().is_counter_clockwise(); }
    void reverse() override;
    bool is_set_speed_discontinuity_area() const { return this->role() == erExternalPerimeter || this->role() == erPerimeter || this->role() == erOverhangPerimeter; }
    const Point& first_point() const override { return this->paths.front().polyline.points.front(); }
    const Point& last_point() const override { assert(this->first_point() == this->paths.back().polyline.points.back()); return this->first_point(); }
    Polygon polygon() const;
    double length() const override;
    bool split_at_vertex(const Point &point, const double scaled_epsilon = scaled<double>(0.001));
    void split_at(const Point &point, bool prefer_non_overhang, const double scaled_epsilon = scaled<double>(0.001));
    struct ClosestPathPoint
    {
        size_t path_idx;
        size_t segment_idx;
        Point  foot_pt;
    };
    ClosestPathPoint         get_closest_path_and_point(const Point &point, bool prefer_non_overhang) const;
    void clip_end(double distance, ExtrusionPaths* paths) const;
    // Test, whether the point is extruded by a bridging flow.
    // This used to be used to avoid placing seams on overhangs, but now the EdgeGrid is used instead.
    bool has_overhang_point(const Point &point) const;
    bool has_overhang_paths() const;
    ExtrusionRole role() const override { return this->paths.empty() ? erNone : this->paths.front().role(); }
    ExtrusionLoopRole loop_role() const { return m_loop_role; }
    void set_loop_role(ExtrusionLoopRole role) {    m_loop_role = role; }
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion width.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    void polygons_covered_by_width(Polygons &out, const float scaled_epsilon) const override;
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion spacing.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    // Useful to calculate area of an infill, which has been really filled in by a 100% rectilinear infill.
    void polygons_covered_by_spacing(Polygons &out, const float scaled_epsilon) const  override;
    Polygons polygons_covered_by_width(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_width(out, scaled_epsilon); return out; }
    Polygons polygons_covered_by_spacing(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_spacing(out, scaled_epsilon); return out; }
    // Minimum volumetric velocity of this extrusion entity. Used by the constant nozzle pressure algorithm.
    double min_mm3_per_mm() const override;
    Polyline as_polyline() const override { return this->polygon().split_at_first_point(); }
    void   collect_polylines(Polylines &dst) const override { Polyline pl = this->as_polyline(); if (! pl.empty()) dst.emplace_back(std::move(pl)); }
    void   collect_points(Points &dst) const override {
        size_t n = std::accumulate(paths.begin(), paths.end(), 0, [](const size_t n, const ExtrusionPath &p){ return n + p.polyline.size(); });
        dst.reserve(dst.size() + n);
        for (const ExtrusionPath &p : this->paths)
            append(dst, p.polyline.points);
    }
    double total_volume() const override { double volume =0.; for (const auto& path : paths) volume += path.total_volume(); return volume; }
    // check if the loop is smooth, angle_threshold is in radians, default is 10 degrees
    bool check_seam_point_angle(double angle_threshold = 0.174, double min_arm_length = 0.025) const;
    //static inline std::string role_to_string(ExtrusionLoopRole role);

#ifndef NDEBUG
	bool validate() const {
		assert(this->first_point() == this->paths.back().polyline.points.back());
		for (size_t i = 1; i < paths.size(); ++ i)
			assert(this->paths[i - 1].polyline.points.back() == this->paths[i].polyline.points.front());
		return true;
	}
#endif /* NDEBUG */

private:
    ExtrusionLoopRole m_loop_role;
};

class ExtrusionLoopSloped : public ExtrusionLoop
{
public:
    std::vector<ExtrusionPathSloped> starts;
    std::vector<ExtrusionPathSloped> ends;
    double target_speed{0.0};

    ExtrusionLoopSloped(
        ExtrusionPaths &original_paths, double seam_gap, double slope_min_length, double slope_max_segment_length, double start_slope_ratio, ExtrusionLoopRole role = elrDefault);

    [[nodiscard]] std::vector<const ExtrusionPath *> get_all_paths() const;
    void clip_slope(double distance, bool inter_perimeter = false );
    void clip_end(const double distance);
    void clip_front(const double distance);
    double slope_path_length();
    void slowdown_slope_speed();
};

inline void extrusion_paths_append(ExtrusionPaths &dst, Polylines &polylines, ExtrusionRole role, double mm3_per_mm, float width, float height)
{
    dst.reserve(dst.size() + polylines.size());
    for (Polyline &polyline : polylines)
        if (polyline.is_valid()) {
            dst.push_back(ExtrusionPath(role, mm3_per_mm, width, height));
            dst.back().polyline = polyline;
        }
}

inline void extrusion_paths_append(ExtrusionPaths &dst, Polylines &polylines, double overhang_degree, int curva_degree, ExtrusionRole role, double mm3_per_mm, float width, float height)
{
    dst.reserve(dst.size() + polylines.size());
    for (Polyline &polyline : polylines)
        if (polyline.is_valid()) {
            dst.push_back(ExtrusionPath(overhang_degree, curva_degree, role, mm3_per_mm, width, height));
            dst.back().polyline = polyline;
        }
}

inline void extrusion_paths_append(ExtrusionPaths &dst, Polylines &&polylines, ExtrusionRole role, double mm3_per_mm, float width, float height)
{
    dst.reserve(dst.size() + polylines.size());
    for (Polyline &polyline : polylines)
        if (polyline.is_valid()) {
            dst.push_back(ExtrusionPath(role, mm3_per_mm, width, height));
            dst.back().polyline = std::move(polyline);
        }
    polylines.clear();
}

inline void extrusion_paths_append(ExtrusionPaths &dst, Polylines &&polylines, double overhang_degree, int curva_degree, ExtrusionRole role, double mm3_per_mm, float width, float height)
{
    dst.reserve(dst.size() + polylines.size());
    for (Polyline &polyline : polylines)
        if (polyline.is_valid()) {
            dst.push_back(ExtrusionPath(overhang_degree, curva_degree, role, mm3_per_mm, width, height));
            dst.back().polyline = std::move(polyline);
        }
    polylines.clear();
}

inline void extrusion_paths_append(ExtrusionPaths &dst, Polyline &&polyline, double overhang_degree, int curva_degree, ExtrusionRole role, double mm3_per_mm, float width, float height)
{
    dst.reserve(dst.size() + 1);
    if (polyline.is_valid()) {
        dst.push_back(ExtrusionPath(overhang_degree, curva_degree, role, mm3_per_mm, width, height));
        dst.back().polyline = std::move(polyline);
    }
    polyline.clear();
}

inline void extrusion_entities_append_paths(ExtrusionEntitiesPtr &dst, Polylines &polylines, ExtrusionRole role, double mm3_per_mm, float width, float height)
{
    dst.reserve(dst.size() + polylines.size());
    for (Polyline &polyline : polylines)
        if (polyline.is_valid()) {
            ExtrusionPath *extrusion_path = new ExtrusionPath(role, mm3_per_mm, width, height);
            dst.push_back(extrusion_path);
            extrusion_path->polyline = polyline;
        }
}

inline void extrusion_entities_append_paths(ExtrusionEntitiesPtr &dst, Polylines &&polylines, ExtrusionRole role, double mm3_per_mm, float width, float height, bool can_reverse = true)
{
    dst.reserve(dst.size() + polylines.size());
    for (Polyline &polyline : polylines)
        if (polyline.is_valid()) {
            ExtrusionPath *extrusion_path = can_reverse ? new ExtrusionPath(role, mm3_per_mm, width, height) : new ExtrusionPathOriented(role, mm3_per_mm, width, height);
            dst.push_back(extrusion_path);
            extrusion_path->polyline = std::move(polyline);
        }
    polylines.clear();
}

//BBS: a kind of special extrusion path has start and end wiping for half spacing
inline void extrusion_entities_append_paths_with_wipe(ExtrusionEntitiesPtr &dst, Polylines &&polylines, ExtrusionRole role, double mm3_per_mm, float width, float height, float nozzle_diameter, bool apply_overlap_compensation)
{
    constexpr double overlap_rate = 0.45;
    dst.reserve(dst.size() + polylines.size());
    Point last_end_point;
    Vec2d last_direction;
    bool last_end_point_valid = false;
    ExtrusionMultiPath* multi_path = new ExtrusionMultiPath();
    for (Polyline& polyline : polylines) {
        if (polyline.is_valid()) {

            if (last_end_point_valid) {
                Point temp = polyline.first_point() - last_end_point;
                if (Vec2d(temp.x(), temp.y()).norm() <= 3 * scaled(width)) {
                    multi_path->paths.push_back(ExtrusionPath(role, mm3_per_mm, width, height, true));

                    if(apply_overlap_compensation){
                        Polyline connect_line;
                        Vec2d curr_direction  = (polyline.first_point()-polyline.last_point()).cast<double>().normalized();
                        Vec2d offset_vector = last_direction * overlap_rate * scaled(nozzle_diameter);
                        Point overlap_last_p = last_end_point + offset_vector.cast<coord_t>();

                        offset_vector = curr_direction * overlap_rate * scaled(nozzle_diameter);
                        Point overlap_curr_p = polyline.first_point() + offset_vector.cast<coord_t>();

                        connect_line.points.push_back(last_end_point);
                        connect_line.points.push_back(overlap_last_p);
                        connect_line.points.push_back(overlap_curr_p);
                        connect_line.points.push_back(polyline.first_point());

                        multi_path->paths.back().polyline = std::move(connect_line);
                    }
                    else{
                        multi_path->paths.back().polyline = std::move(Polyline(last_end_point, polyline.first_point()));
                    }

                } else {
                    dst.push_back(multi_path);
                    multi_path = new ExtrusionMultiPath();
                }
            }

            multi_path->paths.push_back(ExtrusionPath(role, mm3_per_mm, width, height));
            multi_path->paths.back().polyline = std::move(polyline);
            last_end_point_valid = true;
            last_end_point = multi_path->paths.back().polyline.last_point();
            last_direction = (last_end_point - multi_path->paths.back().polyline.first_point()).cast<double>().normalized();
        }
    }
    if (!multi_path->empty())
        dst.push_back(multi_path);
    polylines.clear();
    dst.shrink_to_fit();
}

inline void extrusion_entities_append_loops(ExtrusionEntitiesPtr &dst, Polygons &&loops, ExtrusionRole role, double mm3_per_mm, float width, float height)
{
    dst.reserve(dst.size() + loops.size());
    for (Polygon &poly : loops) {
        if (poly.is_valid()) {
            ExtrusionPath path(role, mm3_per_mm, width, height);
            path.polyline.points = std::move(poly.points);
            path.polyline.points.push_back(path.polyline.points.front());
            dst.emplace_back(new ExtrusionLoop(std::move(path)));
        }
    }
    loops.clear();
}

inline void extrusion_entities_append_loops_and_paths(ExtrusionEntitiesPtr &dst, Polylines &&polylines, ExtrusionRole role, double mm3_per_mm, float width, float height)
{
    dst.reserve(dst.size() + polylines.size());
    for (Polyline &polyline : polylines) {
        if (polyline.is_valid()) {
            if (polyline.is_closed()) {
                ExtrusionPath extrusion_path(role, mm3_per_mm, width, height);
                extrusion_path.polyline = std::move(polyline);
                dst.emplace_back(new ExtrusionLoop(std::move(extrusion_path)));
            } else {
                ExtrusionPath *extrusion_path = new ExtrusionPath(role, mm3_per_mm, width, height);
                extrusion_path->polyline      = std::move(polyline);
                dst.emplace_back(extrusion_path);
            }
        }
    }
    polylines.clear();
}

}

#endif
