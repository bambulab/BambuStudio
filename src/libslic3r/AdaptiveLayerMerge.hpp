#ifndef slic3r_AdaptiveLayerMerge_hpp_
#define slic3r_AdaptiveLayerMerge_hpp_

#include "ExPolygon.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace Slic3r {

class PrintConfig;

struct AdaptiveLayerHeightRange
{
    double min_height { 0. };
    double max_height { 0. };

    bool contains(double height) const;
};

struct AdaptiveMergePatch
{
    size_t     bottom_idx { 0 };
    size_t     top_idx { 0 };
    // Only set on the copy owned by the group-top LayerRegion. Layer::id() may be offset against the
    // PrintObject layer index (raft layers, bottom layers dropped by fix_slicing_errors), so the role
    // has to be stored explicitly instead of being derived from top_idx.
    bool       group_top { false };
    double     merged_height { 0. };

    // The retained contour is extruded on the group top and marks the swallowed footprint on lower layers.
    const ExPolygons &area() const
    {
        static const ExPolygons empty;
        return m_area ? *m_area : empty;
    }

    void set_area(ExPolygons area) { m_area = std::make_shared<const ExPolygons>(std::move(area)); }

private:
    // All copies stored across a merge span share the same immutable geometry.
    std::shared_ptr<const ExPolygons> m_area;
};

// One band of the layer stack, [bottom_idx, top_idx] inclusive. A nozzle prints the band as a single
// extrusion of merged_height, placed on the top layer; the layers below it are dropped.
struct AdaptiveLayerBand
{
    // The band is shaped by the contour of top_idx: the extrusion sits on that layer, and taking the
    // contour of anything below would let the band bulge past what the slice says is there.
    size_t bottom_idx { 0 };
    size_t top_idx { 0 };
    double merged_height { 0. };
};

AdaptiveLayerHeightRange adaptive_layer_height_range(double nozzle_diameter);

// True when the filaments handed in do not all share one nozzle diameter, which is the gate of every
// multi-diameter layer merge. All merging is pointless on a single diameter: the base layer height is
// printable everywhere. filament_ids are 0 based; pass the filaments the print actually uses, because a
// slot merely mapped onto a second diameter must not turn a single-diameter print into a merged one.
// Diameters are resolved through nozzle_diameter_for_filament(), the same source the flows, the support
// merge plan and the wipe tower use, so the gate cannot disagree with the geometry behind it.
bool has_multiple_used_nozzle_diameters(const PrintConfig &print_config, const std::vector<unsigned int> &filament_ids);

// How many base layers one nozzle merges into a single extrusion. The whole print is sliced at the layer
// height of its smallest nozzle, so a larger nozzle simply keeps one layer out of every diameter-ratio
// layers: a 0.8 nozzle next to a 0.2 one keeps one contour out of every four. The ratio is corrected when
// the resulting thickness falls outside what the nozzle can actually extrude. Returns 1 when the nozzle
// has to print every layer on its own.
size_t adaptive_layer_band_size(double nozzle_diameter, double min_nozzle_diameter, double base_layer_height);

// Split the layer stack into bands of band_size layers. The first layer is never merged, so the grid is
// anchored right above it; every nozzle shares that anchor, so bands of commensurate sizes nest into each
// other and their boundaries never straddle.
//
// band_tops marks layers that must end a band, which is where a colour starts or stops: a band carries one
// contour through its whole Z range, so straddling that layer hands the slab to whoever owns the band top.
// Geometry is deliberately not consulted. A coarse nozzle is asked to look the way it would if it had
// sliced the object on its own, and slicing a slope exposes a step of layer_height / tan(angle) on every
// single layer - marking those cuts the grid to pieces and leaves the nozzle printing the base layer
// height, which is both slower and outside what it can extrude. Losing detail is what a coarse nozzle
// does. The marks are honored unless doing so would leave a segment the nozzle cannot extrude, in which
// case printability wins and the mark is dropped. Pass an empty vector for a plain fixed grid.
//
// Unlike a fixed stride, no layer is left out: a tail too short for a full band is folded back into the
// segment before it and the segment is re-cut, so eight layers with band_size five come out as 4 + 4 rather
// than 5 + an orphaned 3. Returns an empty plan when band_size is 1, i.e. when this nozzle merges nothing.
std::vector<AdaptiveLayerBand> build_adaptive_layer_bands(
    const std::vector<double>      &layer_heights,
    size_t                          band_size,
    const AdaptiveLayerHeightRange &height_range,
    const std::vector<uint8_t>     &band_tops = {});

// Cut a continuous Z span into the fewest printable groups. Used by the wipe tower, which merges whole
// slabs and has no per-region geometry to decimate.
bool split_adaptive_layer_span(
    const std::vector<double>              &layer_heights,
    size_t                                  bottom_idx,
    size_t                                  top_idx,
    const AdaptiveLayerHeightRange         &height_range,
    std::vector<std::pair<size_t, size_t>> &groups);

} // namespace Slic3r

#endif
