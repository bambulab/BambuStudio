#ifndef ARRANGEJOB_HPP
#define ARRANGEJOB_HPP

#include "PlaterJob.hpp"
#include "libslic3r/Arrange.hpp"

namespace Slic3r {

class ModelInstance;

namespace GUI {

class ArrangeJob : public PlaterJob
{
    using ArrangePolygon = arrangement::ArrangePolygon;
    using ArrangePolygons = arrangement::ArrangePolygons;

    //BBS: add locked logic
    ArrangePolygons m_selected, m_unselected, m_unprintable, m_locked;
    std::vector<ModelInstance*> m_unarranged;
    std::map<int, ArrangePolygons> m_selected_groups;   // groups of selected items for sequential printing
    arrangement::ArrangeParams params;
    int current_plate_index = 0;

    // clear m_selected and m_unselected, reserve space for next usage
    void clear_input();

    // Prepare all objects on the bed regardless of the selection
    void prepare_all();

    // Prepare the selected and unselected items separately. If nothing is
    // selected, behaves as if everything would be selected.
    void prepare_selected();

    ArrangePolygon get_arrange_poly_(ModelInstance *mi);

    //BBS:prepare the items from current selected partplate
    void prepare_partplate();

    ArrangePolygon prepare_arrange_polygon(void* instance);

protected:

    void prepare() override;

    void check_unprintable();

    void on_exception(const std::exception_ptr &) override;

    void process() override;

public:
    ArrangeJob(std::shared_ptr<ProgressIndicator> pri, Plater *plater)
        : PlaterJob{std::move(pri), plater}
    {}

    int status_range() const override
    {
        return int(m_selected.size() + m_unprintable.size());
    }

    void finalize() override;
};

std::optional<arrangement::ArrangePolygon> get_wipe_tower_arrangepoly(const Plater &);

// The gap between logical beds in the x axis expressed in ratio of
// the current bed width.
static const constexpr double LOGICAL_BED_GAP = 1. / 5.;

//BBS: add sudoku-style strides for x and y
// Stride between logical beds
double bed_stride_x(const Plater* plater);
double bed_stride_y(const Plater* plater);

template<class T> struct PtrWrapper
{
    T *ptr;

    explicit PtrWrapper(T *p) : ptr{p} {}

    arrangement::ArrangePolygon get_arrange_polygon() const
    {
        arrangement::ArrangePolygon ap;
        ptr->get_arrange_polygon(&ap);
        return ap;
    }

    void apply_arrange_result(const Vec2d &t, double rot, int item_id)
    {
        ptr->apply_arrange_result(t, rot);
        ptr->arrange_order = item_id;
    }
};

// Set up arrange polygon for a ModelInstance and Wipe tower
template<class T>
arrangement::ArrangePolygon get_arrange_poly(T obj, const Plater *plater)
{
    using ArrangePolygon = arrangement::ArrangePolygon;

    ArrangePolygon ap = obj.get_arrange_polygon();
    //BBS: always set bed_idx to 0 to use original transforms with no bed_idx
    //if this object is not arranged, it can keep the original transforms
    //ap.bed_idx        = ap.translation.x() / bed_stride_x(plater);
    ap.bed_idx        = 0;
    ap.setter         = [obj, plater](const ArrangePolygon &p) {
        if (p.is_arranged()) {
            Vec2d t = p.translation.cast<double>();
            //BBS: change to sudoku-style computation, do it in partplate list
            //t.x() += p.bed_idx * bed_stride(plater);
            //t.x() += col * bed_stride_x(plater);
            //t.y() -= row * bed_stride_y(plater);
            T{obj}.apply_arrange_result(t, p.rotation, p.itemid);
        }
    };

    return ap;
}

template<>
arrangement::ArrangePolygon get_arrange_poly(ModelInstance *inst,
                                             const Plater * plater);

arrangement::ArrangeParams get_arrange_params(Plater *p);

}} // namespace Slic3r::GUI

#endif // ARRANGEJOB_HPP
