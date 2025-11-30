#include "CoolingBuffer.hpp"

namespace Slic3r {
float new_feedrate_to_reach_time_stretch(std::vector<PerExtruderAdjustments *>::const_iterator it_begin,
                                         std::vector<PerExtruderAdjustments *>::const_iterator it_end,
                                         float                                                 min_feedrate,
                                         float                                                 time_stretch,
                                         size_t                                                max_iter = 20)
{
    float new_feedrate = min_feedrate;
    for (size_t iter = 0; iter < max_iter; ++iter) {
        double nomin = 0;
        double denom = time_stretch;
        for (auto it = it_begin; it != it_end; ++it) {
            assert((*it)->slow_down_min_speed < min_feedrate + EPSILON);
            for (size_t i = 0; i < (*it)->n_lines_adjustable; ++i) {
                const CoolingLine &line = (*it)->lines[i];
                if (line.feedrate > min_feedrate) {
                    nomin += (double) line.time * (double) line.feedrate;
                    denom += (double) line.time;
                }
            }
        }
        assert(denom > 0);
        if (denom < 0) return min_feedrate;
        new_feedrate = (float) (nomin / denom);
        assert(new_feedrate > min_feedrate - EPSILON);
        if (new_feedrate < min_feedrate + EPSILON) goto finished;
        for (auto it = it_begin; it != it_end; ++it)
            for (size_t i = 0; i < (*it)->n_lines_adjustable; ++i) {
                const CoolingLine &line = (*it)->lines[i];
                if (line.feedrate > min_feedrate && line.feedrate < new_feedrate)
                    // Some of the line segments taken into account in the calculation of nomin / denom are now slower than new_feedrate,
                    // which makes the new_feedrate lower than it should be.
                    // Re-run the calculation with a new min_feedrate limit, so that the segments with current feedrate lower than new_feedrate
                    // are not taken into account.
                    goto not_finished_yet;
            }
        goto finished;
    not_finished_yet:
        min_feedrate = new_feedrate;
    }
    // Failed to find the new feedrate for the time_stretch.

finished :
    // Test whether the time_stretch was achieved.
#ifndef NDEBUG
{
    float time_stretch_final = 0.f;
    for (auto it = it_begin; it != it_end; ++it) time_stretch_final += (*it)->time_stretch_when_slowing_down_to_feedrate(new_feedrate);
    assert(std::abs(time_stretch - time_stretch_final) < EPSILON);
}
#endif /* NDEBUG */

    return new_feedrate;
}
// Slow down an extruder range proportionally down to slow_down_layer_time.
// Return the total time for the complete layer.
static inline float extruder_range_slow_down_proportional(std::vector<PerExtruderAdjustments *>::iterator it_begin,
                                                          std::vector<PerExtruderAdjustments *>::iterator it_end,
                                                          // Elapsed time for the extruders already processed.
                                                          float elapsed_time_total0,
                                                          // Initial total elapsed time before slow down.
                                                          float elapsed_time_before_slowdown,
                                                          // Target time for the complete layer (all extruders applied).
                                                          float slow_down_layer_time)
{
    // Total layer time after the slow down has been applied.
    float total_after_slowdown = elapsed_time_before_slowdown;
    // Now decide, whether the external perimeters shall be slowed down as well.
    float max_time_nep = elapsed_time_total0;
    for (auto it = it_begin; it != it_end; ++it) max_time_nep += (*it)->maximum_time_after_slowdown(false);
    if (max_time_nep > slow_down_layer_time) {
        // It is sufficient to slow down the non-external perimeter moves to reach the target layer time.
        // Slow down the non-external perimeters proportionally.
        float non_adjustable_time = elapsed_time_total0;
        for (auto it = it_begin; it != it_end; ++it) non_adjustable_time += (*it)->non_adjustable_time(false);
        // The following step is a linear programming task due to the minimum movement speeds of the print moves.
        // Run maximum 5 iterations until a good enough approximation is reached.
        for (size_t iter = 0; iter < 5; ++iter) {
            float factor = (slow_down_layer_time - non_adjustable_time) / (total_after_slowdown - non_adjustable_time);
            assert(factor > 1.f);
            total_after_slowdown = elapsed_time_total0;
            for (auto it = it_begin; it != it_end; ++it) total_after_slowdown += (*it)->slow_down_proportional(factor, false);
            if (total_after_slowdown > 0.95f * slow_down_layer_time) break;
        }
    } else {
        // Slow down everything. First slow down the non-external perimeters to maximum.
        for (auto it = it_begin; it != it_end; ++it) (*it)->slowdown_to_minimum_feedrate(false);
        // Slow down the external perimeters proportionally.
        float non_adjustable_time = elapsed_time_total0;
        for (auto it = it_begin; it != it_end; ++it) non_adjustable_time += (*it)->non_adjustable_time(true);
        for (size_t iter = 0; iter < 5; ++iter) {
            float factor = (slow_down_layer_time - non_adjustable_time) / (total_after_slowdown - non_adjustable_time);
            assert(factor > 1.f);
            total_after_slowdown = elapsed_time_total0;
            for (auto it = it_begin; it != it_end; ++it) total_after_slowdown += (*it)->slow_down_proportional(factor, true);
            if (total_after_slowdown > 0.95f * slow_down_layer_time) break;
        }
    }
    return total_after_slowdown;
}

// Slow down an extruder range for ConsistentSurface logic.
// This function first tries to slow down only non-visible features (infill, internal perimeters),
// and only slows down external perimeters if more time is needed.
// Returns the remaining time stretch that couldn't be achieved.
static inline float extruder_range_slow_down_consistent_surface(
    std::vector<PerExtruderAdjustments *>::iterator it_begin,
    std::vector<PerExtruderAdjustments *>::iterator it_end,
    float                                           time_stretch,
    AdjustableFeatureType                           additional_slowdown_features)
{
    if (time_stretch <= 0.f)
        return 0.f;

    // Slow down. Try to equalize the feedrates for the allowed feature types.
    std::vector<PerExtruderAdjustments *> by_min_print_speed(it_begin, it_end);

    // Find the highest adjustable feedrate among the extruders for allowed features.
    float feedrate = 0.f;
    for (PerExtruderAdjustments *adj : by_min_print_speed) {
        adj->idx_line_begin = 0;
        adj->idx_line_end = 0;
        for (size_t i = 0; i < adj->n_lines_adjustable; ++i) {
            const CoolingLine &line = adj->lines[i];
            if (line.adjustable(additional_slowdown_features) && line.feedrate > feedrate)
                feedrate = line.feedrate;
        }
    }

    if (feedrate == 0.f)
        return time_stretch; // No adjustable features found

    // Sort by slow_down_min_speed, maximum speed first.
    std::sort(by_min_print_speed.begin(), by_min_print_speed.end(),
              [](const PerExtruderAdjustments *p1, const PerExtruderAdjustments *p2) {
                  return p1->slow_down_min_speed > p2->slow_down_min_speed;
              });

    // Slow down, fast moves first.
    for (auto adj = by_min_print_speed.begin(); adj != by_min_print_speed.end();) {
        float feedrate_limit = (*adj)->slow_down_min_speed;
        float time_stretch_max = 0.f;

        for (auto it = adj; it != by_min_print_speed.end(); ++it)
            time_stretch_max += (*it)->time_stretch_when_slowing_down_to_feedrate(feedrate_limit, additional_slowdown_features);

        if (time_stretch_max >= time_stretch) {
            // We can achieve the required time stretch by slowing down to some feedrate above feedrate_limit
            // Binary search for the right feedrate
            float feedrate_high = feedrate;
            float feedrate_low = feedrate_limit;
            for (int iter = 0; iter < 20; ++iter) {
                float feedrate_mid = (feedrate_high + feedrate_low) / 2.f;
                float stretch = 0.f;
                for (auto it = adj; it != by_min_print_speed.end(); ++it)
                    stretch += (*it)->time_stretch_when_slowing_down_to_feedrate(feedrate_mid, additional_slowdown_features);
                if (stretch < time_stretch)
                    feedrate_high = feedrate_mid;
                else
                    feedrate_low = feedrate_mid;
                if (std::abs(stretch - time_stretch) < 0.01f)
                    break;
            }
            for (auto it = adj; it != by_min_print_speed.end(); ++it)
                (*it)->slow_down_to_feedrate(feedrate_low, additional_slowdown_features);
            return 0.f; // Time stretch achieved
        } else {
            // Slow down to minimum for these features
            time_stretch -= time_stretch_max;
            for (auto it = adj; it != by_min_print_speed.end(); ++it)
                (*it)->slow_down_to_feedrate(feedrate_limit, additional_slowdown_features);
        }

        // Skip extruders with nearly the same slow_down_min_speed
        auto next = adj;
        for (++next; next != by_min_print_speed.end() && (*next)->slow_down_min_speed > (*adj)->slow_down_min_speed - EPSILON; ++next)
            ;
        adj = next;
    }

    return time_stretch; // Return remaining time stretch that couldn't be achieved
}

// Slow down an extruder range to slow_down_layer_time.
// Return the total time for the complete layer.
static inline void extruder_range_slow_down_non_proportional(std::vector<PerExtruderAdjustments *>::iterator it_begin,
                                                             std::vector<PerExtruderAdjustments *>::iterator it_end,
                                                             float                                           time_stretch)
{
    // Slow down. Try to equalize the feedrates.
    std::vector<PerExtruderAdjustments *> by_min_print_speed(it_begin, it_end);
    // Find the next highest adjustable feedrate among the extruders.
    float feedrate = 0;
    for (PerExtruderAdjustments *adj : by_min_print_speed) {
        adj->idx_line_begin = 0;
        adj->idx_line_end   = 0;
        assert(adj->idx_line_begin < adj->n_lines_adjustable);
        if (adj->lines[adj->idx_line_begin].feedrate > feedrate) feedrate = adj->lines[adj->idx_line_begin].feedrate;
    }
    assert(feedrate > 0.f);
    // Sort by slow_down_min_speed, maximum speed first.
    std::sort(by_min_print_speed.begin(), by_min_print_speed.end(),
              [](const PerExtruderAdjustments *p1, const PerExtruderAdjustments *p2) { return p1->slow_down_min_speed > p2->slow_down_min_speed; });
    // Slow down, fast moves first.
    for (;;) {
        // For each extruder, find the span of lines with a feedrate close to feedrate.
        for (PerExtruderAdjustments *adj : by_min_print_speed) {
            for (adj->idx_line_end = adj->idx_line_begin; adj->idx_line_end < adj->n_lines_adjustable && adj->lines[adj->idx_line_end].feedrate > feedrate - EPSILON;
                 ++adj->idx_line_end)
                ;
        }
        // Find the next highest adjustable feedrate among the extruders.
        float feedrate_next = 0.f;
        for (PerExtruderAdjustments *adj : by_min_print_speed)
            if (adj->idx_line_end < adj->n_lines_adjustable && adj->lines[adj->idx_line_end].feedrate > feedrate_next) feedrate_next = adj->lines[adj->idx_line_end].feedrate;
        // Slow down, limited by max(feedrate_next, slow_down_min_speed).
        for (auto adj = by_min_print_speed.begin(); adj != by_min_print_speed.end();) {
            // Slow down at most by time_stretch.
            if ((*adj)->slow_down_min_speed == 0.f) {
                // All the adjustable speeds are now lowered to the same speed,
                // and the minimum speed is set to zero.
                float time_adjustable = 0.f;
                for (auto it = adj; it != by_min_print_speed.end(); ++it) time_adjustable += (*it)->adjustable_time(true);
                float rate = (time_adjustable + time_stretch) / time_adjustable;
                for (auto it = adj; it != by_min_print_speed.end(); ++it) (*it)->slow_down_proportional(rate, true);
                return;
            } else {
                float feedrate_limit   = std::max(feedrate_next, (*adj)->slow_down_min_speed);
                bool  done             = false;
                float time_stretch_max = 0.f;
                for (auto it = adj; it != by_min_print_speed.end(); ++it) time_stretch_max += (*it)->time_stretch_when_slowing_down_to_feedrate(feedrate_limit);
                if (time_stretch_max >= time_stretch) {
                    feedrate_limit = new_feedrate_to_reach_time_stretch(adj, by_min_print_speed.end(), feedrate_limit, time_stretch, 20);
                    done           = true;
                } else
                    time_stretch -= time_stretch_max;
                for (auto it = adj; it != by_min_print_speed.end(); ++it) (*it)->slow_down_to_feedrate(feedrate_limit);
                if (done) return;
            }
            // Skip the other extruders with nearly the same slow_down_min_speed, as they have been processed already.
            auto next = adj;
            for (++next; next != by_min_print_speed.end() && (*next)->slow_down_min_speed > (*adj)->slow_down_min_speed - EPSILON; ++next)
                ;
            adj = next;
        }
        if (feedrate_next == 0.f)
            // There are no other extrusions available for slow down.
            break;
        for (PerExtruderAdjustments *adj : by_min_print_speed) {
            adj->idx_line_begin = adj->idx_line_end;
            feedrate            = feedrate_next;
        }
    }
}

// Calculate slow down for all the extruders.
float CoolingBuffer::calculate_layer_slowdown(std::vector<PerExtruderAdjustments> &per_extruder_adjustments)
{
    // Sort the extruders by an increasing slow_down_layer_time.
    // The layers with a lower slow_down_layer_time are slowed down
    // together with all the other layers with slow_down_layer_time above.
    std::vector<PerExtruderAdjustments *> by_slowdown_time;
    by_slowdown_time.reserve(per_extruder_adjustments.size());
    // Only insert entries, which are adjustable (have cooling enabled and non-zero stretchable time).
    // Collect total print time of non-adjustable extruders.
    float elapsed_time_total0 = 0.f;

    // Check if any extruder uses ConsistentSurface logic
    bool any_consistent_surface = false;

    for (PerExtruderAdjustments &adj : per_extruder_adjustments) {
        // Current total time for this extruder.
        adj.time_total = adj.elapsed_time_total();
        // Maximum time for this extruder, when all extrusion moves are slowed down to min_extrusion_speed.
        adj.time_maximum = adj.maximum_time_after_slowdown(true);
        if (adj.cooling_slow_down_enabled && adj.lines.size() > 0) {
            by_slowdown_time.emplace_back(&adj);

            // For ConsistentSurface logic, prepare the non-adjustable segments
            if (adj.cooling_slowdown_logic == cslConsistentSurface) {
                any_consistent_surface = true;
                // Initialize adjustable fields for all lines
                for (CoolingLine &line : adj.lines) {
                    if (line.type & CoolingLine::TYPE_ADJUSTABLE) {
                        line.adjustable_length = line.length;
                        line.adjustable_time = line.time;
                        line.adjustable_time_max = line.time_max;
                    }
                }
                // Create non-adjustable segments at the end of perimeter loops
                adj.create_non_adjustable_segments(adj.cooling_perimeter_transition_distance);
            }

            if (!m_cooling_logic_proportional)
                // sorts the lines, also sets adj.time_non_adjustable
                adj.sort_lines_by_decreasing_feedrate();
        } else
            elapsed_time_total0 += adj.elapsed_time_total();
    }
    std::sort(by_slowdown_time.begin(), by_slowdown_time.end(),
              [](const PerExtruderAdjustments *adj1, const PerExtruderAdjustments *adj2) { return adj1->slow_down_layer_time < adj2->slow_down_layer_time; });

    for (auto cur_begin = by_slowdown_time.begin(); cur_begin != by_slowdown_time.end(); ++cur_begin) {
        PerExtruderAdjustments &adj = *(*cur_begin);
        // Calculate the current adjusted elapsed_time_total over the non-finalized extruders.
        float total = elapsed_time_total0;
        for (auto it = cur_begin; it != by_slowdown_time.end(); ++it) total += (*it)->time_total;
        float slow_down_layer_time = adj.slow_down_layer_time * 1.001f;
        if (total > slow_down_layer_time) {
            // The current total time is above the minimum threshold of the rest of the extruders, don't adjust anything.
        } else {
            // Adjust this and all the following (higher m_config.slow_down_layer_time) extruders.
            // Sum maximum slow down time as if everything was slowed down including the external perimeters.
            float max_time = elapsed_time_total0;
            for (auto it = cur_begin; it != by_slowdown_time.end(); ++it) max_time += (*it)->time_maximum;
            if (max_time > slow_down_layer_time) {
                float time_stretch = slow_down_layer_time - total;

                // Check if this extruder uses ConsistentSurface logic
                if (adj.cooling_slowdown_logic == cslConsistentSurface) {
                    // ConsistentSurface: Two-phase slowdown
                    // Phase 1: Try slowing down only non-external perimeter features (infill, internal perimeters)
                    float remaining = extruder_range_slow_down_consistent_surface(
                        cur_begin, by_slowdown_time.end(), time_stretch, AdjustableFeatureType::None);

                    // Phase 2: If still not enough time, allow external perimeter and first internal slowdown
                    if (remaining > 0.f) {
                        extruder_range_slow_down_consistent_surface(
                            cur_begin, by_slowdown_time.end(), remaining,
                            AdjustableFeatureType::ExternalPerimeters | AdjustableFeatureType::FirstInternalPerimeters);
                    }
                } else if (m_cooling_logic_proportional) {
                    // Uniform cooling with proportional slowdown
                    extruder_range_slow_down_proportional(cur_begin, by_slowdown_time.end(), elapsed_time_total0, total, slow_down_layer_time);
                } else {
                    // Uniform cooling with non-proportional slowdown
                    extruder_range_slow_down_non_proportional(cur_begin, by_slowdown_time.end(), time_stretch);
                }
            } else {
                // Slow down to maximum possible.
                for (auto it = cur_begin; it != by_slowdown_time.end(); ++it) (*it)->slowdown_to_minimum_feedrate(true);
            }
        }
        elapsed_time_total0 += adj.elapsed_time_total();
    }

    return elapsed_time_total0;
}
}