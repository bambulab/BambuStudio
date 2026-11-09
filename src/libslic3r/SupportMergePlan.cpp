#include "SupportMergePlan.hpp"

#include "GCode/ToolOrdering.hpp"
#include "Layer.hpp"
#include "Model.hpp"
#include "Print.hpp"
#include "PrintConfig.hpp"
#include "libslic3r.h"

#include <algorithm>
#include <cmath>

namespace Slic3r {

static std::vector<SupportMergePlan::Candidate> candidates_in_span(
    const SupportMergePlan &plan,
    const std::vector<std::vector<SupportMergePlan::Candidate>> &candidates_per_layer,
    double bottom_z,
    double top_z)
{
    std::vector<SupportMergePlan::Candidate> out;
    if (! plan.enabled)
        return out;

    // An object layer overlaps the span when it sticks out above bottom_z and starts below top_z.
    const auto first = std::lower_bound(plan.layer_print_z.begin(), plan.layer_print_z.end(), bottom_z + EPSILON);
    for (size_t idx = size_t(first - plan.layer_print_z.begin()); idx < plan.layer_print_z.size(); ++ idx) {
        if (plan.layer_print_z[idx] - plan.layer_heights[idx] > top_z - EPSILON)
            break;
        for (const SupportMergePlan::Candidate &candidate : candidates_per_layer[idx]) {
            auto same = std::find_if(out.begin(), out.end(),
                [&candidate](const SupportMergePlan::Candidate &other) {
                    return other.filament_id == candidate.filament_id;
                });
            if (same == out.end())
                out.emplace_back(candidate);
        }
    }
    return out;
}

std::vector<SupportMergePlan::Candidate> SupportMergePlan::candidates_in_span(double bottom_z, double top_z) const
{
    return Slic3r::candidates_in_span(*this, candidates_per_layer, bottom_z, top_z);
}

std::vector<SupportMergePlan::Candidate> SupportMergePlan::candidates_for_slab(double top_z, double height) const
{
    std::vector<Candidate> candidates = candidates_in_span(top_z - height, top_z);
    if (candidates.empty()) {
        // A support layer sits in the gap between two object layers, so a thin span may not overlap any of
        // them at all. Widen it by one object layer rather than give up.
        const double slack = base_height_at(top_z) + EPSILON;
        candidates = candidates_in_span(top_z - height - slack, top_z + slack);
    }
    return candidates;
}

SupportMergePlan::Candidate SupportMergePlan::coarsest_for_slab(double top_z, double height) const
{
    Candidate best;
    for (const Candidate &candidate : candidates_for_slab(top_z, height))
        if (std::abs(fixed_band_height_at(candidate, top_z) - height) < EPSILON)
            if (! best.valid() || candidate.nozzle_diameter > best.nozzle_diameter)
                best = candidate;
    return best;
}

SupportMergePlan::Candidate SupportMergePlan::finest_interface_for_slab(double top_z, double height) const
{
    Candidate best;
    std::vector<Candidate> candidates =
        Slic3r::candidates_in_span(*this, interface_candidates_per_layer, top_z - height, top_z);
    if (candidates.empty()) {
        const double slack = base_height_at(top_z) + EPSILON;
        candidates = Slic3r::candidates_in_span(
            *this, interface_candidates_per_layer, top_z - height - slack, top_z + slack);
    }
    for (const Candidate &candidate : candidates)
        if (std::abs(fixed_band_height_at(candidate, top_z) - height) < EPSILON &&
            (! best.valid() || candidate.nozzle_diameter < best.nozzle_diameter))
            best = candidate;
    return best;
}

double SupportMergePlan::max_interface_height(double top_z, double height) const
{
    std::vector<Candidate> candidates =
        Slic3r::candidates_in_span(*this, interface_candidates_per_layer, top_z - height, top_z);
    if (candidates.empty()) {
        const double slack = base_height_at(top_z) + EPSILON;
        candidates = Slic3r::candidates_in_span(
            *this, interface_candidates_per_layer, top_z - height - slack, top_z + slack);
    }
    double max_height = 0.;
    for (const Candidate &candidate : candidates)
        max_height = std::max(max_height, fixed_band_height_at(candidate, top_z));
    return max_height;
}

double SupportMergePlan::max_height_for_slab(double top_z, double height) const
{
    double max_height = 0.;
    for (const Candidate &candidate : candidates_for_slab(top_z, height))
        max_height = std::max(max_height, fixed_band_height_at(candidate, top_z));
    return max_height;
}

double SupportMergePlan::base_height_at(double top_z) const
{
    if (! enabled || layer_print_z.empty())
        return 0.;

    // The object layer ending at or just below top_z sets the granularity there.
    auto it = std::upper_bound(layer_print_z.begin(), layer_print_z.end(), top_z + EPSILON);
    if (it == layer_print_z.begin())
        return layer_heights.front();
    return layer_heights[size_t(it - layer_print_z.begin()) - 1];
}

double SupportMergePlan::fixed_band_height_at(const Candidate &candidate, double top_z) const
{
    if (! enabled || layer_print_z.empty())
        return 0.;

    auto layer_it = std::lower_bound(layer_print_z.begin(), layer_print_z.end(), top_z - EPSILON);
    if (layer_it == layer_print_z.end() || *layer_it > top_z + EPSILON)
        return 0.;
    const size_t layer_idx = size_t(layer_it - layer_print_z.begin());
    if (candidate.band_size < 2)
        return layer_heights[layer_idx];

    auto bands_it = fixed_bands_per_filament.find(candidate.filament_id);
    if (bands_it == fixed_bands_per_filament.end())
        return 0.;
    auto band_it = std::lower_bound(bands_it->second.begin(), bands_it->second.end(), layer_idx,
        [](const AdaptiveLayerBand &band, size_t idx) { return band.top_idx < idx; });
    return band_it != bands_it->second.end() && band_it->top_idx == layer_idx ?
        band_it->merged_height : 0.;
}

SupportMergePlan::Candidate SupportMergePlan::coarsest_band_containing(
    double print_z, double &band_height) const
{
    band_height = 0.;
    Candidate best;
    if (! enabled || layer_print_z.empty())
        return best;

    // Independent support layers may end between object layers. Use the object layer whose Z span contains
    // print_z; requiring an exact top-Z match would give every such support layer the full slab line width.
    auto layer_it = std::lower_bound(layer_print_z.begin(), layer_print_z.end(), print_z - EPSILON);
    if (layer_it == layer_print_z.end())
        return best;
    const size_t layer_idx = size_t(layer_it - layer_print_z.begin());

    for (const Candidate &candidate : candidates_per_layer[layer_idx]) {
        double candidate_height = layer_heights[layer_idx];
        if (candidate.band_size > 1) {
            auto bands_it = fixed_bands_per_filament.find(candidate.filament_id);
            if (bands_it == fixed_bands_per_filament.end())
                continue;
            auto band_it = std::lower_bound(bands_it->second.begin(), bands_it->second.end(), layer_idx,
                [](const AdaptiveLayerBand &band, size_t idx) { return band.top_idx < idx; });
            if (band_it == bands_it->second.end() || band_it->bottom_idx > layer_idx)
                continue;
            candidate_height = band_it->merged_height;
        }
        if (! best.valid() || candidate.nozzle_diameter > best.nozzle_diameter) {
            best        = candidate;
            band_height = candidate_height;
        }
    }
    return best;
}

const SupportMergePlan::Slab *SupportMergePlan::slab_for_layer(size_t layer_idx) const
{
    if (layer_idx >= slab_index_per_layer.size())
        return nullptr;
    const int slab_idx = slab_index_per_layer[layer_idx];
    return slab_idx < 0 ? nullptr : &slabs[size_t(slab_idx)];
}

// Cut the object layers into the slab partition the support folds onto. Every nozzle anchors its bands at
// the object base, so walking up from the bed and always taking the band that starts exactly here and
// reaches highest follows the coarsest ladder without ever stepping off it. Picking the coarsest band per
// top layer independently would not: a fine band closing early moves the cursor off the coarse ladder and
// the rest of the stack could never rejoin it.
static void expand_slabs(SupportMergePlan &plan, const std::vector<std::vector<int>> *extruded_per_layer)
{
    const size_t num_layers = plan.layer_print_z.size();
    plan.slab_index_per_layer.assign(num_layers, -1);
    if (plan.fixed_bands_per_filament.empty())
        return;

    size_t next_bottom = 0;
    while (next_bottom < num_layers) {
        const SupportMergePlan::Candidate *best      = nullptr;
        const AdaptiveLayerBand           *best_band = nullptr;
        for (const auto &entry : plan.fixed_bands_per_filament) {
            const std::vector<AdaptiveLayerBand> &bands = entry.second;
            auto band_it = std::lower_bound(bands.begin(), bands.end(), next_bottom,
                [](const AdaptiveLayerBand &band, size_t idx) { return band.bottom_idx < idx; });
            if (band_it == bands.end() || band_it->bottom_idx != next_bottom || band_it->top_idx >= num_layers)
                continue;
            // The nozzle has to be one the object actually prints with where the band ends, or the support
            // would borrow a tool from a height nothing else uses there.
            const std::vector<SupportMergePlan::Candidate> &at_top = plan.candidates_per_layer[band_it->top_idx];
            auto cand_it = std::find_if(at_top.begin(), at_top.end(),
                [&entry](const SupportMergePlan::Candidate &candidate) { return candidate.filament_id == entry.first; });
            if (cand_it == at_top.end())
                continue;
            // A region keeping slices on a layer does not mean its filament is extruded there: its band may be
            // emitted at another layer. Pinning the slab to such a filament adds a tool change nothing else at
            // this height needs, and among equal nozzles the lowest filament id always wins.
            // A support filament the user picked is not extruded by the object, so it is exempt (nullptr).
            if (extruded_per_layer != nullptr) {
                const std::vector<int> &extruded = (*extruded_per_layer)[band_it->top_idx];
                if (std::find(extruded.begin(), extruded.end(), entry.first) == extruded.end())
                    continue;
            }
            // Reaching higher wins, and on a tie the coarser nozzle does: two filaments can share a ladder
            // while only one of them saves a tool change here.
            if (best_band == nullptr || band_it->top_idx > best_band->top_idx ||
                (band_it->top_idx == best_band->top_idx && cand_it->nozzle_diameter > best->nozzle_diameter)) {
                best      = &*cand_it;
                best_band = &*band_it;
            }
        }
        if (best_band == nullptr) {
            // Nothing starts here, so this layer stays on the fine grid and the cursor moves up by one.
            ++ next_bottom;
            continue;
        }

        SupportMergePlan::Slab slab;
        slab.bottom_idx        = next_bottom;
        slab.top_idx           = best_band->top_idx;
        slab.top_z             = plan.layer_print_z[slab.top_idx];
        slab.height            = best_band->merged_height;
        slab.filament_id       = best->filament_id;
        slab.nozzle_diameter   = best->nozzle_diameter;
        slab.bottom_anchor     = slab.bottom_idx == 0;

        const int slab_id = int(plan.slabs.size());
        for (size_t idx = slab.bottom_idx; idx <= slab.top_idx; ++ idx)
            plan.slab_index_per_layer[idx] = slab_id;
        plan.slabs.emplace_back(slab);
        next_bottom = slab.top_idx + 1;
    }
}

SupportMergePlan make_support_merge_plan(const PrintObject &object)
{
    SupportMergePlan plan;

    const PrintObjectConfig &object_config = object.config();
    const PrintConfig       &print_config  = object.print()->config();
    if (! object.print()->uses_multiple_nozzle_diameters())
        return plan;

    const ConstLayerPtrsAdaptor layers = object.layers();
    if (layers.size() < 2)
        return plan;

    plan.layer_print_z.reserve(layers.size());
    plan.layer_heights.reserve(layers.size());
    for (const Layer *layer : layers) {
        plan.layer_print_z.emplace_back(layer->print_z);
        plan.layer_heights.emplace_back(layer->height);
    }

    const double grid_nozzle = layer_grid_nozzle_diameter(object.print()->full_print_config());
    const double base_height = object.slicing_parameters().layer_height;

    auto make_candidate = [&print_config, grid_nozzle, base_height](int filament_id) {
        SupportMergePlan::Candidate candidate;
        candidate.filament_id     = filament_id;
        candidate.nozzle_diameter = nozzle_diameter_for_filament(print_config, (unsigned int) filament_id);
        candidate.window          = adaptive_layer_height_range(candidate.nozzle_diameter);
        candidate.band_size       = adaptive_layer_band_size(
            candidate.nozzle_diameter, grid_nozzle, base_height);
        return candidate;
    };

    auto ensure_fixed_grid = [&plan](const SupportMergePlan::Candidate &candidate) {
        if (candidate.band_size > 1 &&
            plan.fixed_bands_per_filament.find(candidate.filament_id) ==
                plan.fixed_bands_per_filament.end())
            plan.fixed_bands_per_filament.emplace(
                candidate.filament_id,
                build_adaptive_layer_bands(
                    plan.layer_heights, candidate.band_size, candidate.window));
    };

    auto add_candidate = [&make_candidate, &ensure_fixed_grid, &print_config](
                             std::vector<SupportMergePlan::Candidate> &candidates,
                             int filament_id,
                             bool allow_soluble) {
        if (filament_id <= 0 ||
            (! allow_soluble && print_config.filament_soluble.get_at(size_t(filament_id - 1))))
            return;
        auto same = std::find_if(candidates.begin(), candidates.end(),
            [filament_id](const SupportMergePlan::Candidate &other) { return other.filament_id == filament_id; });
        if (same != candidates.end())
            return;
        const SupportMergePlan::Candidate candidate = make_candidate(filament_id);
        if (candidate.nozzle_diameter > EPSILON) {
            candidates.emplace_back(candidate);
            ensure_fixed_grid(candidate);
        }
    };

    plan.candidates_per_layer.resize(layers.size());
    plan.interface_candidates_per_layer.resize(layers.size());

    // A filament value of 0 means "whatever the object prints with". Resolve those choices per object layer,
    // because a fine nozzle available elsewhere in the object must not keep a thin support contact at this Z.
    std::vector<SupportMergePlan::Candidate> object_filaments;
    if (const ModelObject *model_object = object.model_object())
        for (const ModelVolume *volume : model_object->volumes) {
            if (! volume->is_model_part())
                continue;
            for (int filament_id : volume->get_extruders(GlobalFeatureFilaments::from_config(object.print()->default_region_config(), object.print()->config().filament_diameter.size())))
                add_candidate(object_filaments, filament_id, false);
        }

    std::vector<std::vector<SupportMergePlan::Candidate>> object_candidates(layers.size());
    // Filaments the object actually extrudes per layer, collected like ToolOrdering::collect_extruders():
    // walls only where perimeters exist, one slot per fill island. Listing both infill filaments whenever
    // any fill exists pins the slab to a filament this height never extrudes.
    // ToolOrdering's extruder_override (per-layer tool changes of a single-filament plate) is not applied,
    // like the candidates above; height range modifiers are already part of the region config.
    std::vector<std::vector<int>> extruded_per_layer(layers.size());
    for (size_t layer_idx = 0; layer_idx < layers.size(); ++ layer_idx) {
        std::vector<SupportMergePlan::Candidate> &candidates = object_candidates[layer_idx];
        for (const LayerRegion *layerm : layers[layer_idx]->regions()) {
            if (layerm->slices.empty())
                continue;
            const PrintRegionConfig &region_config = layerm->region().config();
            std::vector<int> &extruded = extruded_per_layer[layer_idx];
            if (! layerm->perimeters.empty())
                extruded.emplace_back(region_config.wall_filament.value);
            for (const ExtrusionEntity *ee : layerm->fills.entities) {
                const auto *fill = dynamic_cast<const ExtrusionEntityCollection *>(ee);
                if (fill == nullptr || fill->empty())
                    continue;
                extruded.emplace_back(int(LayerTools::filament_slot_by_role(*fill, region_config)));
            }
            add_candidate(candidates, region_config.wall_filament.value, false);
            add_candidate(candidates, region_config.sparse_infill_filament.value, false);
            add_candidate(candidates, region_config.solid_infill_filament.value, false);
        }
        if (candidates.empty() && ! layers[layer_idx]->empty())
            candidates = object_filaments;
    }

    const int support_filament = object_config.support_filament.value;
    if (support_filament > 0) {
        if (! print_config.filament_soluble.get_at(size_t(support_filament - 1))) {
            const SupportMergePlan::Candidate candidate = make_candidate(support_filament);
            if (candidate.nozzle_diameter > EPSILON) {
                ensure_fixed_grid(candidate);
                for (std::vector<SupportMergePlan::Candidate> &candidates : plan.candidates_per_layer)
                    candidates.emplace_back(candidate);
            }
        }
    } else {
        plan.candidates_per_layer = object_candidates;
    }

    const int interface_filament = object_config.support_interface_filament.value;
    if (interface_filament > 0) {
        const SupportMergePlan::Candidate candidate = make_candidate(interface_filament);
        if (candidate.nozzle_diameter > EPSILON) {
            ensure_fixed_grid(candidate);
            for (std::vector<SupportMergePlan::Candidate> &candidates : plan.interface_candidates_per_layer)
                candidates.emplace_back(candidate);
        }
    } else {
        plan.interface_candidates_per_layer = std::move(object_candidates);
    }

    expand_slabs(plan, support_filament > 0 ? nullptr : &extruded_per_layer);

    // The roof is printed with the pinned interface filament, so it may only be folded onto a slab that
    // filament can actually extrude. Nothing can make a 0.4 nozzle lay down a band only a 0.8 one reaches,
    // so an object whose slabs do not all match the pinned nozzle keeps its contact chains on the fine grid.
    if (interface_filament > 0) {
        const double interface_nozzle =
            nozzle_diameter_for_filament(print_config, (unsigned int) interface_filament);
        plan.interface_fold_enabled = ! plan.slabs.empty() && interface_nozzle > EPSILON;
        for (const SupportMergePlan::Slab &slab : plan.slabs)
            if (std::abs(slab.nozzle_diameter - interface_nozzle) > EPSILON) {
                plan.interface_fold_enabled = false;
                break;
            }
    }

    plan.enabled = true;
    return plan;
}

} // namespace Slic3r
