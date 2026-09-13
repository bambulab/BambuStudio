#ifndef slic3r_LayerTimeSmoothing_hpp_
#define slic3r_LayerTimeSmoothing_hpp_

#include "../libslic3r.h"
#include "../PrintConfig.hpp"
#include "GCodeEditor.hpp"

#include <string>
#include <vector>

namespace Slic3r {

// Limits how much the estimated print time may change from one layer to the next.
//
// A step change of the layer time is a step change of the thermal history of the part: it changes how far
// the previous layer has cooled and contracted at the moment the next layer is welded onto it, and it shows
// up on vertical walls as banding, sunken or bulging regions, especially with glossy and high shrinkage
// filaments. The smoother treats the per layer print times as a curve over Z and enforces
//
//     t[i] >= t[i-1] * (1 - v)    and    t[i-1] >= t[i] * (1 - v)
//
// where v is the maximum allowed relative variation. Layers are only ever slowed down (speed factor <= 1),
// the minimum print speed of each filament is respected, and two caps limit the cost: a maximum slow down
// per layer and a maximum increase of the total print time.
//
// The smoother works on the CoolingLine data parsed by GCodeEditor, after the minimum layer time slow down
// of CoolingBuffer and after the optional outer wall speed smoothing (SmoothCalculator), immediately before
// the layer G-code is rewritten. It therefore never violates the minimum layer time, and the fan speed ramp
// is computed from the smoothed layer times.
class LayerTimeSmoother
{
public:
    struct Params
    {
        // Maximum allowed relative change of the layer time between adjacent layers, 0 .. 1.
        float max_variation = 0.25f;
        // Maximum relative increase of the time of a single layer (2 = the layer may take up to three times as long).
        float max_layer_slowdown = 2.f;
        // Maximum relative increase of the total time of the smoothed layers.
        float max_total_increase = 0.2f;
        // Extrusions which may be slowed down.
        LayerTimeSmoothingScope scope = ltsAll;
    };

    struct LayerStats
    {
        // Layer time before smoothing (after the cooling slow down), seconds.
        float time_before = 0.f;
        // Layer time after smoothing, seconds.
        float time_after = 0.f;
        // Highest layer time reachable within the caps, seconds.
        float time_cap = 0.f;
        // Whether the layer was slowed down.
        bool smoothed = false;
    };

    explicit LayerTimeSmoother(const Params &params) : m_params(params) {}

    static Params params_from_config(const PrintConfig &config);

    // Smooth the layer times of a sequence of layers printed one after the other.
    // layers[i] holds the parsed G-code lines of the i-th layer, times[i] its current print time in seconds.
    // Layers before first_layer are left untouched and do not constrain their neighbours.
    // On return times[i] holds the new layer times. Returns the number of layers that were slowed down.
    size_t process(const std::vector<std::vector<PerExtruderAdjustments> *> &layers, std::vector<float> &times, size_t first_layer = 1);

    const std::vector<LayerStats> &stats() const { return m_stats; }
    // Maximum variation that was finally enforced. Larger than Params::max_variation if the limit on the
    // total print time increase had to be applied.
    float effective_max_variation() const { return m_effective_max_variation; }

    // Diagnostic G-code comment for a layer processed by process(), terminated by a newline.
    std::string layer_comment(size_t idx) const;

    // The pure part of the algorithm, exposed for unit tests.
    // Find the smallest layer times t >= times with t[i] <= caps[i], which satisfy the variation limit wherever the caps allow it.
    static std::vector<float> solve_target_times(const std::vector<float> &times, const std::vector<float> &caps, float max_variation, size_t first_layer);
    // Same as above, with the variation limit relaxed until the total time increase fits into max_total_increase.
    // effective_max_variation receives the variation limit that was finally used.
    static std::vector<float> solve_target_times(const std::vector<float> &times, const std::vector<float> &caps, float max_variation, float max_total_increase, size_t first_layer, float &effective_max_variation);

    // Helpers working on the parsed lines of one layer.
    static bool  line_in_scope(const CoolingLine &line, LayerTimeSmoothingScope scope);
    static float layer_time(const std::vector<PerExtruderAdjustments> &layer);
    // Layer time when every extrusion in scope is slowed down to the minimum print speed. FLT_MAX if not limited.
    static float layer_time_max(const std::vector<PerExtruderAdjustments> &layer, LayerTimeSmoothingScope scope);
    // Slow down the extrusions in scope proportionally, so that the layer takes target_time. Returns the layer time reached.
    static float stretch_layer(std::vector<PerExtruderAdjustments> &layer, float target_time, LayerTimeSmoothingScope scope);

private:
    Params                  m_params;
    std::vector<LayerStats> m_stats;
    float                   m_effective_max_variation = 0.f;
};

} // namespace Slic3r

#endif // slic3r_LayerTimeSmoothing_hpp_
