#ifndef ARRANGEJOB_HPP
#define ARRANGEJOB_HPP

#include "PlaterJob.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "libslic3r/Arrange.hpp"
#include "libslic3r/Model.hpp"

namespace Slic3r {

class ModelInstance;

namespace GUI {

class ArrangeJob : public PlaterJob
{
    using ArrangePolygon = arrangement::ArrangePolygon;
    using ArrangePolygons = arrangement::ArrangePolygons;

    //BBS: add locked logic
    ArrangePolygons m_selected, m_unselected, m_unprintable, m_locked;
    std::map<int, ArrangePolygons> m_selected_groups;   // groups of selected items for sequential printing
    std::vector<int> m_uncompatible_plates;  // plate indices with different printing sequence than global

    arrangement::ArrangeParams params;
    int current_plate_index = 0;
    int m_progress_value = 0;
    Polygon bed_poly;

    //BBS: pack tighter. When the outline check predicts that sparrow beats the stock
    // result (fewer plates, or fewer parts left off the plate for a plate arrange),
    // process() re-arranges the same items with it and keeps the better result.
    bool     m_pack_eligible = false; // outlines collected, sparrow may run
    bool     m_packing       = false; // sparrow phase reached; a cancel keeps the stock result
    int      m_stock_score   = -1;
    int      m_packed_score  = -1;    // >= 0 if the sparrow result was kept
    int      m_stock_unplaced  = 0;   // parts a full arrange left off every plate
    int      m_packed_unplaced = 0;
    ArrangePolygons m_stock_selected; // kept for the undo step
    wxString m_progress_label;

    // Lower is better: plates used, or parts left off the plate for a plate arrange.
    int         score(const ArrangePolygons &selected, const ArrangePolygons &unselected) const;
    int         unplaced(const ArrangePolygons &aps) const;
    bool        predict_better(int stock_score, double bed_area) const;
    std::string done_message() const;
    void        pack_tighter(ArrangePolygons &selected, const Points &bedpts);
    void        apply_result(ArrangePolygons &selected, ArrangePolygons &unselected, ArrangePolygons &locked, ArrangePolygons &unprintable);

    // Live view of the sparrow phase. Applied from the transformations saved in
    // prepare(), which finalize() restores before applying the result.
    ArrangePolygons m_live_aps;
    std::vector<arrangement::ArrangeParams::Pose> m_live_stock; // where parts wait until sparrow takes them
    std::vector<std::pair<ModelInstance *, Geometry::Transformation>> m_live_restore;
    // Prime tower state as prepare() found it; restore_live() puts it back with the parts.
    std::vector<double> m_live_tower_x, m_live_tower_y;
    std::vector<Vec2d>  m_live_tower_pos;
    std::vector<bool>   m_live_tower_placed;
    std::atomic<bool> m_live_open{false};
    bool m_live_used = false;
    void apply_live(const std::vector<arrangement::ArrangeParams::Pose> &poses);
    void restore_live();

    // clear m_selected and m_unselected, reserve space for next usage
    void clear_input();

    // Prepare the selected and unselected items separately. If nothing is
    // selected, behaves as if everything would be selected.
    void prepare_selected();

    void prepare_all();

    //BBS:prepare the items from current selected partplate
    void prepare_partplate();

    // prepare the items which are selected and not on the current partplate
    void prepare_outside_plate();

    void prepare_wipe_tower(bool select = false);

    // Whether the selected items need a wipe tower.
    bool selected_items_need_wipe_tower() const;

    // Moves each plate's wipe tower to its optimal position after arranging.
    void apply_optimal_wipe_tower_positions();

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
        // ensure finalize() is called after all operations in process() is finished.
        return int(m_selected.size() + m_unprintable.size() + 1);
    }

    void finalize() override;

    static ArrangePolygon prepare_arrange_polygon(void *instance);
};

std::optional<arrangement::ArrangePolygon> get_wipe_tower_arrangepoly(const Plater &);

// The gap between logical beds in the x axis expressed in ratio of
// the current bed width.
static const constexpr double LOGICAL_BED_GAP = 1. / 5.;

//BBS: add sudoku-style strides for x and y
// Stride between logical beds
double bed_stride_x(const Plater* plater);
double bed_stride_y(const Plater* plater);

arrangement::ArrangeParams init_arrange_params(Plater *p);

}} // namespace Slic3r::GUI

#endif // ARRANGEJOB_HPP
