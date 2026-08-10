#ifndef slic3r_SupportMergePlan_hpp_
#define slic3r_SupportMergePlan_hpp_

#include "AdaptiveLayerMerge.hpp"

#include <cstddef>
#include <vector>

namespace Slic3r {

class PrintObject;

// BBS multi-diameter: which filaments the object prints at a given height, so the support layers can be
// merged into slabs a large nozzle is actually able to extrude.
//
// With support_filament == 0 the support filament is only resolved while exporting G-code. The support layer
// heights are fixed long before that, so a large nozzle picked at that point would have to extrude the base
// layer height, which is below what it can physically do. This plan is queried after the support areas exist
// but before they are installed as support layers, which is where merging happens.
//
// Sparse support base reaches for the coarsest nozzle its own Z span offers. Contact/interface layers first
// try the finest nozzle available at their own level; when none can print the base height, their lower layers
// are folded into a slab printable by the finest available nozzle.
struct SupportMergePlan
{
    struct Candidate
    {
        // 1 based filament id, matching support_filament / wall_filament. 0 means none.
        int                      filament_id { 0 };
        double                   nozzle_diameter { 0. };
        AdaptiveLayerHeightRange window;

        bool valid() const { return filament_id > 0 && nozzle_diameter > 0.; }
    };

    // Object layer print_z, ascending. Parallel to layer_heights / candidates_per_layer.
    std::vector<double>                 layer_print_z;
    std::vector<double>                 layer_heights;
    // Filaments the object prints on a given object layer. Empty for layers the object does not print at all.
    std::vector<std::vector<Candidate>> candidates_per_layer;
    // Candidates for support contact/interface. This differs when support_interface_filament is explicit.
    std::vector<std::vector<Candidate>> interface_candidates_per_layer;

    bool                                enabled { false };

    // Filaments the object prints on any layer overlapping (bottom_z, top_z].
    std::vector<Candidate> candidates_in_span(double bottom_z, double top_z) const;
    // Filaments that may extrude a slab of height ending at top_z. Only the ones the object prints over the
    // slab's own Z span qualify: borrowing a coarse nozzle from further away would merge the support to a
    // height nothing prints here with.
    std::vector<Candidate> candidates_for_slab(double top_z, double height) const;
    // Coarsest filament that can extrude that slab, or an invalid candidate when none of them can.
    Candidate              coarsest_for_slab(double top_z, double height) const;
    // Finest interface filament that can extrude the slab, or an invalid candidate when none can.
    Candidate              finest_interface_for_slab(double top_z, double height) const;
    // Tallest interface slab ending at top_z any filament of its span can extrude. 0 when there is none.
    double                 max_interface_height(double top_z, double height) const;
    // Tallest slab ending at top_z the coarsest filament of its span can extrude. 0 when there is none.
    double                 max_height_for_slab(double top_z, double height) const;
    // Object layer height at top_z, the granularity every merged slab is a multiple of.
    double                 base_height_at(double top_z) const;
};

SupportMergePlan make_support_merge_plan(const PrintObject &object);

} // namespace Slic3r

#endif // slic3r_SupportMergePlan_hpp_
