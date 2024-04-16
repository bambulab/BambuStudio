#ifndef slic3r_CutUtils_hpp_
#define slic3r_CutUtils_hpp_

#include "enum_bitmask.hpp"
#include "Point.hpp"
#include "Model.hpp"

namespace Slic3r {

const float CUT_TOLERANCE = 0.1f;
struct Groove
{
    float depth{0.f};
    float width{0.f};
    float flaps_angle{0.f};
    float angle{0.f};
    float depth_init{0.f};
    float width_init{0.f};
    float flaps_angle_init{0.f};
    float angle_init{0.f};
    float depth_tolerance{CUT_TOLERANCE};
    float width_tolerance{CUT_TOLERANCE};
};

class Cut {

    Model                       m_model;
    int                         m_instance;
    const Transform3d           m_cut_matrix;
    ModelObjectCutAttributes    m_attributes;

    void post_process(ModelObject *object, bool is_upper, ModelObjectPtrs &objects, bool keep, bool place_on_cut, bool flip);
    void post_process(ModelObject* upper_object, ModelObject* lower_object, ModelObjectPtrs& objects);
    void finalize(const ModelObjectPtrs& objects);

public:

    Cut(const ModelObject* object, int instance, const Transform3d& cut_matrix,
        ModelObjectCutAttributes attributes = ModelObjectCutAttribute::KeepUpper |
                                              ModelObjectCutAttribute::KeepLower |
                                              ModelObjectCutAttribute::CutToParts );
    ~Cut() { m_model.clear_objects(); }

    struct Part
    {
        bool selected;
        bool is_modifier;
    };

    const ModelObjectPtrs& perform_with_plane();
    const ModelObjectPtrs& perform_by_contour(std::vector<Part> parts, int dowels_count);
    const ModelObjectPtrs& perform_with_groove(const Groove& groove, const Transform3d& rotation_m, bool keep_as_parts = false);
    bool                   set_offset_for_two_part{false};
}; // namespace Cut

} // namespace Slic3r

#endif /* slic3r_CutUtils_hpp_ */
