#ifndef slic3r_CutUtils_hpp_
#define slic3r_CutUtils_hpp_

#include "enum_bitmask.hpp"
#include "Point.hpp"
#include "Model.hpp"

#include <algorithm>
#include <functional>
#include <utility>

namespace Slic3r {

// Progress in [0, 100] over the whole cut; message is a short, non-localized stage
// description, or null for a stage that has no label of its own.
using CutProgressCallback = std::function<void(int percent, const char *message)>;
// Returns true to request early termination of a long cut. It is polled from many nesting
// levels, so it has to be idempotent and stay true once it has turned true: the unwinding
// path reads it again on the way out to tell a cancellation apart from a cut that legitimately
// produced nothing.
using CutCancelCallback   = std::function<bool()>;

// A cut reports progress from several nesting levels: a groove cut runs seven plane cuts
// in a row, each of which walks the volumes and re-projects the paint of every volume.
// Each level only reports its own local [0, 100] and lets sub() carve out the slice of
// the overall range it owns, so the same code works standalone and nested.
class CutProgressRange
{
public:
    CutProgressRange() = default;
    CutProgressRange(CutProgressCallback progress, CutCancelCallback cancel)
        : m_progress(std::move(progress)), m_cancel(std::move(cancel))
    {}

    CutProgressRange sub(double from, double to) const
    {
        CutProgressRange out(*this);
        out.m_from = this->map(from);
        out.m_to   = this->map(to);
        return out;
    }

    void report(double local_percent, const char *message = nullptr) const
    {
        if (m_progress)
            m_progress(int(this->map(local_percent)), message);
    }

    bool canceled() const { return m_cancel && m_cancel(); }

private:
    double map(double local_percent) const
    {
        return m_from + std::min(std::max(local_percent, 0.), 100.) * (m_to - m_from) / 100.;
    }

    CutProgressCallback m_progress;
    CutCancelCallback   m_cancel;
    double              m_from { 0. };
    double              m_to   { 100. };
};

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
    CutProgressRange            m_progress;
    bool                        m_canceled { false };

    void post_process(ModelObject *object, bool is_upper, ModelObjectPtrs &objects, bool keep, bool place_on_cut, bool flip, bool discard_half_cut);
    void post_process(ModelObject* upper_object, ModelObject* lower_object, ModelObjectPtrs& objects);
    void finalize(const ModelObjectPtrs& objects);
    // Give up a canceled cut: hand the half-built results to m_model so they are freed with it,
    // and return the resulting empty object list.
    const ModelObjectPtrs& abandon(ModelObject *upper, ModelObject *lower, const std::vector<ModelObject *> &dowels = {});

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

    // Optional progress reporting and cancellation. When cancel() turns true the perform_*
    // methods release everything they built and return an empty list; the caller has to ask
    // was_canceled() to tell that apart from a cut that legitimately kept nothing.
    void set_progress(CutProgressCallback progress, CutCancelCallback cancel);
    // Nested form: a groove cut hands each of its plane cuts a slice of its own range.
    void set_progress(CutProgressRange progress) { m_progress = std::move(progress); }
    bool was_canceled() const { return m_canceled; }

    const ModelObjectPtrs& perform_with_plane();
    const ModelObjectPtrs& perform_by_contour(std::vector<Part> parts, int dowels_count);
    const ModelObjectPtrs& perform_with_groove(const Groove& groove, const Transform3d& rotation_m, bool keep_as_parts = false);
    bool                   set_offset_for_two_part{false};
}; // namespace Cut

} // namespace Slic3r

#endif /* slic3r_CutUtils_hpp_ */
