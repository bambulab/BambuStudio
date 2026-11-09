#include "AdaptiveLayerMerge.hpp"

#include "ClipperUtils.hpp"
#include "Layer.hpp"
#include "PrintConfig.hpp"
#include "libslic3r.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Slic3r {

bool AdaptiveLayerHeightRange::contains(double height) const
{
    return height >= min_height - EPSILON && height <= max_height + EPSILON;
}

// The printable layer height window of a single nozzle diameter, derived from the diameter itself:
// 0.2 -> 0.04..0.14, 0.4 -> 0.08..0.28, 0.6 -> 0.12..0.42, 0.8 -> 0.16..0.56.
// min_layer_height / max_layer_height cannot be used here: those options are indexed by extruder, so on a
// machine carrying more nozzle diameters than extruders ConfigOptionFloats::get_at() falls back to the
// first slot (values.front(), see Config.hpp) and silently returns the window of a different nozzle.
AdaptiveLayerHeightRange adaptive_layer_height_range(double nozzle_diameter)
{
    return { 0.2 * nozzle_diameter, 0.7 * nozzle_diameter };
}

float support_top_z_for_nozzle(double nozzle_diameter)
{
    static constexpr std::pair<double, float> table[] = {{0.2, 0.1f}, {0.4, 0.2f}, {0.6, 0.2f}, {0.8, 0.4f}};
    const std::pair<double, float> *best = &table[0];
    for (const std::pair<double, float> &entry : table)
        if (std::abs(entry.first - nozzle_diameter) < std::abs(best->first - nozzle_diameter))
            best = &entry;
    return best->second;
}

ExPolygons adaptive_printable_pieces(ExPolygons pieces, double nozzle_diameter)
{
    const double min_area = 0.25 * nozzle_diameter * nozzle_diameter;
    const float  erosion  = float(scale_(0.5 * nozzle_diameter));
    pieces.erase(std::remove_if(pieces.begin(), pieces.end(), [min_area, erosion](const ExPolygon &piece) {
                     return piece.area() * SCALING_FACTOR * SCALING_FACTOR < min_area ||
                            (erosion > 0.f && shrink_ex(ExPolygons { piece }, erosion).empty());
                 }), pieces.end());
    return pieces;
}

ExPolygons adaptive_printable_core(ExPolygons pieces, double nozzle_diameter)
{
    // The area / erosion test runs first, so degenerate slivers never reach the offset below.
    pieces = adaptive_printable_pieces(std::move(pieces), nozzle_diameter);
    const float radius = float(scale_(0.5 * nozzle_diameter));
    if (pieces.empty() || radius <= 0.f)
        return pieces;
    ExPolygons core;
    for (const ExPolygon &piece : pieces) {
        // Clamp to the original piece: the opening only removes what no bead fits into.
        ExPolygons opened = intersection_ex(opening_ex(ExPolygons{ piece }, radius), ExPolygons{ piece });
        append(core, std::move(opened));
    }
    return adaptive_printable_pieces(std::move(core), nozzle_diameter);
}

ExPolygons adaptive_suppressed_orphan_area(const Layer &layer)
{
    Polygons suppressed;
    Polygons extruded;
    for (const LayerRegion *layerm : layer.regions()) {
        if (layerm->adaptive_merge_patches.empty()) {
            polygons_append(extruded, to_polygons(layerm->slices.surfaces));
            continue;
        }
        for (const AdaptiveMergePatch &patch : layerm->adaptive_merge_patches) {
            // Only an orphan carries an explicit footprint. The whole-region marker of a band is left out: the
            // region's material there lies under another extrusion, which is what stands on that step.
            if (patch.group_top && patch.area().empty() && patch.has_suppressed_area())
                polygons_append(suppressed, to_polygons(patch.suppressed_area()));
            else
                polygons_append(extruded, to_polygons(patch.area()));
        }
    }
    if (suppressed.empty())
        return {};
    // A slab spanning this step may still stand where another region lost its orphan.
    return extruded.empty() ? union_ex(suppressed) : diff_ex(union_ex(suppressed), union_ex(extruded));
}

bool has_multiple_used_nozzle_diameters(const PrintConfig &print_config, const std::vector<unsigned int> &filament_ids)
{
    double first = 0.;
    for (unsigned int filament_id : filament_ids) {
        const double diameter = nozzle_diameter_for_filament(print_config, filament_id + 1);
        if (diameter <= EPSILON)
            continue;
        if (first <= EPSILON)
            first = diameter;
        else if (std::abs(diameter - first) > EPSILON)
            return true;
    }
    return false;
}

size_t adaptive_layer_band_size(double nozzle_diameter, double min_nozzle_diameter, double base_layer_height)
{
    if (nozzle_diameter <= EPSILON || min_nozzle_diameter <= EPSILON || base_layer_height <= EPSILON)
        return 1;

    const AdaptiveLayerHeightRange range = adaptive_layer_height_range(nozzle_diameter);
    // The layer stack is sliced for the smallest nozzle, so the diameter ratio is the number of layers a
    // larger nozzle covers with one extrusion.
    size_t size = size_t(std::max(1., std::floor(nozzle_diameter / min_nozzle_diameter + 0.5)));
    // A base layer height chosen well below what the smallest nozzle needs leaves the ratio too thin to
    // extrude, so the band grows until the nozzle can lay it down in one go.
    while (double(size) * base_layer_height + EPSILON < range.min_height)
        ++size;
    // The window wins over the ratio: a band the nozzle cannot extrude is worse than a thinner one.
    while (size > 1 && double(size) * base_layer_height > range.max_height + EPSILON)
        -- size;
    return double(size) * base_layer_height + EPSILON < range.min_height ? 1 : size;
}

// Cover [bottom_idx, top_idx] with consecutive bands, every one of them inside the printable window and as
// close to band_size layers as the window allows. Returns false when no legal cover exists, which means the
// span is thinner than a single extrusion of this nozzle.
static bool cover_span_with_bands(
    const std::vector<double>      &layer_heights,
    size_t                          bottom_idx,
    size_t                          top_idx,
    size_t                          band_size,
    const AdaptiveLayerHeightRange &height_range,
    std::vector<AdaptiveLayerBand> &bands)
{
    bands.clear();
    if (bottom_idx > top_idx || top_idx >= layer_heights.size())
        return false;

    const size_t layer_count = top_idx - bottom_idx + 1;
    const size_t unreachable = std::numeric_limits<size_t>::max();
    // cost[i] is how far the bands covering the first i layers stray from band_size in total.
    std::vector<size_t> cost(layer_count + 1, unreachable);
    std::vector<size_t> prev(layer_count + 1, 0);
    cost[0] = 0;
    for (size_t end = 1; end <= layer_count; ++ end) {
        double height = 0.;
        for (size_t begin = end; begin-- > 0;) {
            height += layer_heights[bottom_idx + begin];
            if (height > height_range.max_height + EPSILON)
                break;
            if (cost[begin] == unreachable || ! height_range.contains(height))
                continue;
            const size_t size    = end - begin;
            const size_t penalty = size > band_size ? size - band_size : band_size - size;
            // Ties go to the band found last, i.e. to the thickest one, which is also the fastest.
            if (cost[begin] + penalty <= cost[end]) {
                cost[end] = cost[begin] + penalty;
                prev[end] = begin;
            }
        }
    }
    if (cost[layer_count] == unreachable)
        return false;

    for (size_t end = layer_count; end > 0; end = prev[end]) {
        AdaptiveLayerBand band { bottom_idx + prev[end], bottom_idx + end - 1, 0. };
        for (size_t layer_idx = band.bottom_idx; layer_idx <= band.top_idx; ++ layer_idx)
            band.merged_height += layer_heights[layer_idx];
        bands.emplace_back(band);
    }
    std::reverse(bands.begin(), bands.end());
    return true;
}

// Same cost as cover_span_with_bands(), but a span starting at the object base is laid out from the
// bottom up: on a tie a regular band_size band wins, so the remainder of the stack ends up at the top and
// the first layer of a coarse nozzle no longer depends on the object height.
static bool cover_span_from_base(
    const std::vector<double>      &layer_heights,
    size_t                          bottom_idx,
    size_t                          top_idx,
    size_t                          band_size,
    const AdaptiveLayerHeightRange &height_range,
    std::vector<AdaptiveLayerBand> &bands)
{
    if (bottom_idx != 0)
        return cover_span_with_bands(layer_heights, bottom_idx, top_idx, band_size, height_range, bands);

    bands.clear();
    if (bottom_idx > top_idx || top_idx >= layer_heights.size())
        return false;

    const size_t layer_count = top_idx - bottom_idx + 1;
    const size_t unreachable = std::numeric_limits<size_t>::max();
    // cost[i] is how far the bands covering the layers from i up to the span top stray from band_size.
    std::vector<size_t> cost(layer_count + 1, unreachable);
    std::vector<size_t> next(layer_count + 1, 0);
    cost[layer_count] = 0;
    for (size_t begin = layer_count; begin-- > 0;) {
        double height = 0.;
        for (size_t end = begin + 1; end <= layer_count; ++ end) {
            height += layer_heights[bottom_idx + end - 1];
            if (height > height_range.max_height + EPSILON)
                break;
            if (cost[end] == unreachable || ! height_range.contains(height))
                continue;
            const size_t size    = end - begin;
            const size_t penalty = size > band_size ? size - band_size : band_size - size;
            const size_t total   = cost[end] + penalty;
            // Ties keep a regular band, otherwise go to the thickest one, which is also the fastest.
            if (total < cost[begin] || (total == cost[begin] && next[begin] - begin != band_size)) {
                cost[begin] = total;
                next[begin] = end;
            }
        }
    }
    if (cost[0] == unreachable)
        return false;

    for (size_t begin = 0; begin < layer_count; begin = next[begin]) {
        AdaptiveLayerBand band { bottom_idx + begin, bottom_idx + next[begin] - 1, 0. };
        for (size_t layer_idx = band.bottom_idx; layer_idx <= band.top_idx; ++ layer_idx)
            band.merged_height += layer_heights[layer_idx];
        bands.emplace_back(band);
    }
    return true;
}

std::vector<AdaptiveLayerBand> build_adaptive_layer_bands(
    const std::vector<double>      &layer_heights,
    size_t                          band_size,
    const AdaptiveLayerHeightRange &height_range,
    const std::vector<uint8_t>     &band_tops)
{
    std::vector<AdaptiveLayerBand> bands;
    // The first band starts at the object base. It is still extruded on its top layer, with a height equal
    // to the complete span, so its lower face stays on the bed or on the object's raft/support substrate.
    // Callers propagate that object-base contact separately from the physical group-top layer id.
    const size_t first_layer_idx = 0;
    if (band_size < 2 || layer_heights.size() < 2)
        return bands;

    const size_t last_idx = layer_heights.size() - 1;
    // Every marked layer closes a segment, and so does the top of the stack. Bands never straddle a
    // segment boundary, which is what keeps a marked layer from being swallowed and decimated away.
    std::vector<size_t> segment_tops;
    for (size_t layer_idx = first_layer_idx; layer_idx < last_idx; ++ layer_idx)
        if (layer_idx < band_tops.size() && band_tops[layer_idx])
            segment_tops.emplace_back(layer_idx);
    segment_tops.emplace_back(last_idx);

    std::vector<AdaptiveLayerBand> segment_bands;
    size_t bottom_idx      = first_layer_idx;
    size_t last_bottom_idx = first_layer_idx;
    size_t last_band_count = 0;
    for (size_t i = 0; i < segment_tops.size(); ++ i) {
        const size_t top_idx = segment_tops[i];
        if (cover_span_from_base(layer_heights, bottom_idx, top_idx, band_size, height_range, segment_bands)) {
            last_bottom_idx = bottom_idx;
            last_band_count = segment_bands.size();
            bands.insert(bands.end(), segment_bands.begin(), segment_bands.end());
            bottom_idx = top_idx + 1;
            continue;
        }
        // Too thin for this nozzle to extrude on its own. Printability outranks the mark, so the segment
        // gives it up and joins the one above by simply not advancing bottom_idx.
        if (i + 1 < segment_tops.size())
            continue;
        // There is nothing above the last segment to join, so take the segment below back and re-cut both
        // at once. This is what keeps the top of the object merged when its tail is shorter than a band.
        if (last_band_count > 0 &&
            cover_span_from_base(layer_heights, last_bottom_idx, top_idx, band_size, height_range, segment_bands)) {
            bands.resize(bands.size() - last_band_count);
            bands.insert(bands.end(), segment_bands.begin(), segment_bands.end());
        }
        // Otherwise the whole stack is thinner than one extrusion of this nozzle and nothing merges.
    }
    // A cover may need single-layer groups to bridge a stretch the window cannot span, but such a group is
    // what not merging looks like, so it is not reported as a band.
    bands.erase(std::remove_if(bands.begin(), bands.end(),
                               [](const AdaptiveLayerBand &band) { return band.top_idx <= band.bottom_idx; }),
                bands.end());
    return bands;
}

bool pull_adaptive_band_top_to_material(
    AdaptiveLayerBand                &band,
    const std::vector<double>        &layer_heights,
    const AdaptiveLayerHeightRange   &height_range,
    const std::vector<unsigned char> &has_material)
{
    if (band.bottom_idx > band.top_idx || band.top_idx >= layer_heights.size() ||
        band.top_idx >= has_material.size())
        return false;

    // The grid already placed a printable band here. Only the empty-top case needs a shorter top.
    if (has_material[band.top_idx])
        return true;

    size_t top = band.top_idx;
    while (top > band.bottom_idx && ! has_material[top])
        -- top;
    if (! has_material[top])
        return false;

    double height = 0.;
    for (size_t layer_idx = band.bottom_idx; layer_idx <= top; ++ layer_idx)
        height += layer_heights[layer_idx];
    // A single leftover step is not a band: it is what not merging looks like, and it is often
    // thinner than this nozzle can extrude. The caller then prints that layer unmerged, if at all.
    if (top <= band.bottom_idx || ! height_range.contains(height))
        return false;

    band.top_idx       = top;
    band.merged_height = height;
    return true;
}

// Cut a span of layers into consecutive printable groups that stay as close as possible to the nozzle's
// fixed band size. Returns false when no split works, which means the span cannot be extruded by this nozzle.
bool split_adaptive_layer_span(
    const std::vector<double>              &layer_heights,
    size_t                                  bottom_idx,
    size_t                                  top_idx,
    size_t                                  preferred_band_size,
    const AdaptiveLayerHeightRange         &height_range,
    std::vector<std::pair<size_t, size_t>> &groups)
{
    groups.clear();
    if (bottom_idx > top_idx || top_idx >= layer_heights.size() || preferred_band_size == 0)
        return false;
    const size_t layer_count = top_idx - bottom_idx + 1;
    // cost[i] is the total distance from the preferred band size over the first i layers.
    const size_t unreachable = std::numeric_limits<size_t>::max();
    std::vector<size_t> cost(layer_count + 1, unreachable);
    std::vector<size_t> prev(layer_count + 1, 0);
    cost[0] = 0;
    for (size_t end = 1; end <= layer_count; ++end) {
        double height = 0.;
        for (size_t begin = end; begin-- > 0;) {
            height += layer_heights[bottom_idx + begin];
            if (height > height_range.max_height + EPSILON)
                break;
            if (cost[begin] == unreachable || ! height_range.contains(height))
                continue;
            const size_t size    = end - begin;
            const size_t penalty = size > preferred_band_size ? size - preferred_band_size :
                                                                  preferred_band_size - size;
            // Prefer the thicker final band when two covers deviate equally from the fixed grid.
            if (cost[begin] + penalty <= cost[end]) {
                cost[end] = cost[begin] + penalty;
                prev[end] = begin;
            }
        }
    }
    if (cost[layer_count] == unreachable)
        return false;

    for (size_t end = layer_count; end > 0; end = prev[end])
        groups.emplace_back(bottom_idx + prev[end], bottom_idx + end - 1);
    std::reverse(groups.begin(), groups.end());
    return true;
}

} // namespace Slic3r
