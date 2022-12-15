#ifndef ARRANGE_HPP
#define ARRANGE_HPP

#include "ExPolygon.hpp"

namespace Slic3r {

class BoundingBox;

namespace arrangement {

/// A geometry abstraction for a circular print bed. Similarly to BoundingBox.
class CircleBed {
    Point center_;
    double radius_;
public:

    inline CircleBed(): center_(0, 0), radius_(std::nan("")) {}
    explicit inline CircleBed(const Point& c, double r): center_(c), radius_(r) {}

    inline double radius() const { return radius_; }
    inline const Point& center() const { return center_; }
};

/// Representing an unbounded bed.
struct InfiniteBed {
    Point center;
    explicit InfiniteBed(const Point &p = {0, 0}): center{p} {}
};

/// A logical bed representing an object not being arranged. Either the arrange
/// has not yet successfully run on this ArrangePolygon or it could not fit the
/// object due to overly large size or invalid geometry.
static const constexpr int UNARRANGED = -1;

/// Input/Output structure for the arrange() function. The poly field will not
/// be modified during arrangement. Instead, the translation and rotation fields
/// will mark the needed transformation for the polygon to be in the arranged
/// position. These can also be set to an initial offset and rotation.
/// 
/// The bed_idx field will indicate the logical bed into which the
/// polygon belongs: UNARRANGED means no place for the polygon
/// (also the initial state before arrange), 0..N means the index of the bed.
/// Zero is the physical bed, larger than zero means a virtual bed.
struct ArrangePolygon { 
    ExPolygon poly;                 /// The 2D silhouette to be arranged
    Vec2crd   translation{0, 0};    /// The translation of the poly
    double    rotation{0.0};        /// The rotation of the poly in radians
    coord_t   inflation = 0;        /// Arrange with inflated polygon
    int       bed_idx{UNARRANGED};  /// To which logical bed does poly belong...
    int       priority{0};
    //BBS: add locked_plate to indicate whether it is in the locked plate
    int       locked_plate{ -1 };
    bool      is_virt_object{ false };
    bool      is_extrusion_cali_object{ false };
    bool      is_wipe_tower{false};
    //BBS: add row/col for sudoku-style layout
    int       row{0};
    int       col{0};
    std::vector<int>  extrude_ids{1};        ///extruder_id for least extruder switch
    int       bed_temp{0};         ///bed temperature for different material judge
    int       print_temp{0};      ///print temperature for different material judge
    int       first_bed_temp{ 0 };      ///first layer bed temperature for different material judge
    int       first_print_temp{ 0 };      ///first layer print temperature for different material judge
    int       vitrify_temp{ 0 };   // max bed temperature for material compatibility, which is usually the filament vitrification temp
    int       itemid{ 0 };         // item id in the vector, used for accessing all possible params like extrude_id
    int       is_applied{ 0 };     // transform has been applied
    double    height{ 0 };         // item height 
    double    brim_width{ 0 };     // brim width
    std::string name;
    
    // If empty, any rotation is allowed (currently unsupported)
    // If only a zero is there, no rotation is allowed
    std::vector<double> allowed_rotations = {0.};
    
    /// Optional setter function which can store arbitrary data in its closure
    std::function<void(const ArrangePolygon&)> setter = nullptr;
    
    /// Helper function to call the setter with the arrange data arguments
    void apply() {
        if (setter && !is_applied) { 
            setter(*this);
            is_applied = 1;
        }
    }

    /// Test if arrange() was called previously and gave a successful result.
    bool is_arranged() const { return bed_idx != UNARRANGED; }

    inline ExPolygon transformed_poly() const
    {
        ExPolygon ret = poly;
        ret.rotate(rotation);
        ret.translate(translation.x(), translation.y());

        return ret;
    }
};

using ArrangePolygons = std::vector<ArrangePolygon>;

struct ArrangeParams {
    
    /// The minimum distance which is allowed for any 
    /// pair of items on the print bed in any direction.
    coord_t min_obj_distance = 0;
    
    /// The accuracy of optimization.
    /// Goes from 0.0 to 1.0 and scales performance as well
    float accuracy = 1.f;
    
    /// Allow parallel execution.
    bool parallel = true;

    bool allow_rotations = false;

    //BBS: add specific arrange params
    bool  allow_multi_materials_on_same_plate = true;
    bool  avoid_extrusion_cali_region         = true;
    bool  is_seq_print                        = false;
    float bed_shrink_x = 0;
    float bed_shrink_y = 0;
    float brim_skirt_distance = 0;
    float clearance_height_to_rod = 0;
    float clearance_height_to_lid = 0;
    float cleareance_radius = 0;
    float printable_height = 256.0;

    ArrangePolygons excluded_regions;   // regions cant't be used
    ArrangePolygons nonprefered_regions; // regions can be used but not prefered
    
    /// Progress indicator callback called when an object gets packed. 
    /// The unsigned argument is the number of items remaining to pack.
    std::function<void(unsigned, std::string)> progressind = [](unsigned st, std::string str = "") {
        std::cout << "st=" << st << ", " << str << std::endl;
    };

    std::function<void(const ArrangePolygon &)> on_packed;
    
    /// A predicate returning true if abort is needed.
    std::function<bool(void)>     stopcondition;
    
    ArrangeParams() = default;
    explicit ArrangeParams(coord_t md) : min_obj_distance(md) {}
};

/**
 * \brief Arranges the input polygons.
 *
 * WARNING: Currently, only convex polygons are supported by the libnest2d 
 * library which is used to do the arrangement. This might change in the future
 * this is why the interface contains a general polygon capable to have holes.
 *
 * \param items Input vector of ArrangePolygons. The transformation, rotation 
 * and bin_idx fields will be changed after the call finished and can be used
 * to apply the result on the input polygon.
 */
template<class TBed> void arrange(ArrangePolygons &items, const ArrangePolygons &excludes, const TBed &bed, const ArrangeParams &params = {});

// A dispatch function that determines the bed shape from a set of points.
template<> void arrange(ArrangePolygons &items, const ArrangePolygons &excludes, const Points &bed, const ArrangeParams &params);

extern template void arrange(ArrangePolygons &items, const ArrangePolygons &excludes, const BoundingBox &bed, const ArrangeParams &params);
extern template void arrange(ArrangePolygons &items, const ArrangePolygons &excludes, const CircleBed &bed, const ArrangeParams &params);
extern template void arrange(ArrangePolygons &items, const ArrangePolygons &excludes, const Polygon &bed, const ArrangeParams &params);
extern template void arrange(ArrangePolygons &items, const ArrangePolygons &excludes, const InfiniteBed &bed, const ArrangeParams &params);

inline void arrange(ArrangePolygons &items, const Points &bed, const ArrangeParams &params = {}) { arrange(items, {}, bed, params); }
inline void arrange(ArrangePolygons &items, const BoundingBox &bed, const ArrangeParams &params = {}) { arrange(items, {}, bed, params); }
inline void arrange(ArrangePolygons &items, const CircleBed &bed, const ArrangeParams &params = {}) { arrange(items, {}, bed, params); }
inline void arrange(ArrangePolygons &items, const Polygon &bed, const ArrangeParams &params = {}) { arrange(items, {}, bed, params); }
inline void arrange(ArrangePolygons &items, const InfiniteBed &bed, const ArrangeParams &params = {}) { arrange(items, {}, bed, params); }

}} // namespace Slic3r::arrangement

#endif // MODELARRANGE_HPP
