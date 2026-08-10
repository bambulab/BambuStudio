#include "AdaptiveLayerMerge.hpp"

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

std::vector<AdaptiveLayerBand> build_adaptive_layer_bands(
    const std::vector<double>      &layer_heights,
    size_t                          band_size,
    const AdaptiveLayerHeightRange &height_range,
    const std::vector<uint8_t>     &band_tops)
{
    std::vector<AdaptiveLayerBand> bands;
    // The first layer is never merged. A band extrudes on its top layer only, so swallowing layer 0 would
    // leave the print with no extrusion at the initial layer height at all: no bed contact to hold the
    // object, no wall for the brim to run along, and none of the first layer settings applied. See the
    // empty-initial-layer check in GCode::collect_layers_to_print().
    const size_t first_layer_idx = 1;
    if (band_size < 2 || first_layer_idx + 1 >= layer_heights.size())
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
        if (cover_span_with_bands(layer_heights, bottom_idx, top_idx, band_size, height_range, segment_bands)) {
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
            cover_span_with_bands(layer_heights, last_bottom_idx, top_idx, band_size, height_range, segment_bands)) {
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

// Cut a span of layers into as few consecutive groups as possible, every one of them inside the printable
// height window. Returns false when no split works, which means the span cannot be extruded by this nozzle.
bool split_adaptive_layer_span(
    const std::vector<double>              &layer_heights,
    size_t                                  bottom_idx,
    size_t                                  top_idx,
    const AdaptiveLayerHeightRange         &height_range,
    std::vector<std::pair<size_t, size_t>> &groups)
{
    groups.clear();
    if (bottom_idx > top_idx || top_idx >= layer_heights.size())
        return false;
    const size_t layer_count = top_idx - bottom_idx + 1;
    // best[i] is the minimum number of legal groups covering the first i layers.
    const size_t unreachable = std::numeric_limits<size_t>::max();
    std::vector<size_t> best(layer_count + 1, unreachable);
    std::vector<size_t> prev(layer_count + 1, 0);
    best[0] = 0;
    for (size_t end = 1; end <= layer_count; ++end) {
        double height = 0.;
        for (size_t begin = end; begin-- > 0;) {
            height += layer_heights[bottom_idx + begin];
            if (height > height_range.max_height + EPSILON)
                break;
            if (best[begin] == unreachable || ! height_range.contains(height))
                continue;
            if (best[begin] + 1 < best[end]) {
                best[end] = best[begin] + 1;
                prev[end] = begin;
            }
        }
    }
    if (best[layer_count] == unreachable)
        return false;

    for (size_t end = layer_count; end > 0; end = prev[end])
        groups.emplace_back(bottom_idx + prev[end], bottom_idx + end - 1);
    std::reverse(groups.begin(), groups.end());
    return true;
}

} // namespace Slic3r
