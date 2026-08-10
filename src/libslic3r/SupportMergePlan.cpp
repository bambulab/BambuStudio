#include "SupportMergePlan.hpp"

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
        if (candidate.window.contains(height))
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
        if (candidate.window.contains(height) &&
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
        max_height = std::max(max_height, candidate.window.max_height);
    return max_height;
}

double SupportMergePlan::max_height_for_slab(double top_z, double height) const
{
    Candidate coarsest;
    for (const Candidate &candidate : candidates_for_slab(top_z, height))
        if (! coarsest.valid() || candidate.nozzle_diameter > coarsest.nozzle_diameter)
            coarsest = candidate;
    return coarsest.valid() ? coarsest.window.max_height : 0.;
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

    auto make_candidate = [&print_config](int filament_id) {
        SupportMergePlan::Candidate candidate;
        candidate.filament_id     = filament_id;
        candidate.nozzle_diameter = nozzle_diameter_for_filament(print_config, (unsigned int) filament_id);
        candidate.window          = adaptive_layer_height_range(candidate.nozzle_diameter);
        return candidate;
    };

    auto add_candidate = [&make_candidate, &print_config](
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
        if (candidate.nozzle_diameter > EPSILON)
            candidates.emplace_back(candidate);
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
            for (int filament_id : volume->get_extruders())
                add_candidate(object_filaments, filament_id, false);
        }

    std::vector<std::vector<SupportMergePlan::Candidate>> object_candidates(layers.size());
    for (size_t layer_idx = 0; layer_idx < layers.size(); ++ layer_idx) {
        std::vector<SupportMergePlan::Candidate> &candidates = object_candidates[layer_idx];
        for (const LayerRegion *layerm : layers[layer_idx]->regions()) {
            if (layerm->slices.empty())
                continue;
            const PrintRegionConfig &region_config = layerm->region().config();
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
            if (candidate.nozzle_diameter > EPSILON)
                for (std::vector<SupportMergePlan::Candidate> &candidates : plan.candidates_per_layer)
                    candidates.emplace_back(candidate);
        }
    } else {
        plan.candidates_per_layer = object_candidates;
    }

    const int interface_filament = object_config.support_interface_filament.value;
    if (interface_filament > 0) {
        const SupportMergePlan::Candidate candidate = make_candidate(interface_filament);
        if (candidate.nozzle_diameter > EPSILON)
            for (std::vector<SupportMergePlan::Candidate> &candidates : plan.interface_candidates_per_layer)
                candidates.emplace_back(candidate);
    } else {
        plan.interface_candidates_per_layer = std::move(object_candidates);
    }

    plan.enabled = true;
    return plan;
}

} // namespace Slic3r
