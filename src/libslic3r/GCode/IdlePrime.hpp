#ifndef slic3r_GCode_IdlePrime_hpp_
#define slic3r_GCode_IdlePrime_hpp_

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Slic3r {

// Prime of a tool that was parked for idle_s seconds while its nozzle kept its filament: min_prime right after use,
// approaching full_prime with the time constant tau_s. full_prime when tau_s <= 0 (off), and never more than
// full_prime or less than min(min_prime, full_prime).
inline float prime_after_idle(float full_prime, float min_prime, double idle_s, double tau_s)
{
    if (tau_s <= 0. || full_prime <= min_prime)
        return full_prime;
    const double rise = 1. - std::exp(-std::max(0., idle_s) / tau_s);
    return float(double(min_prime) + double(full_prime - min_prime) * rise);
}

// Stretches a layer's per-filament print times proportionally so the layer takes min_layer_time_s, as the cooling
// slow-down does on the printer, but each filament to at most its max_time (its paths at the slow-down's minimum
// speed; filaments without an entry are not limited). A layer that cannot reach the minimum ends at those limits.
inline void stretch_to_min_layer_time(std::unordered_map<unsigned int, double>       &filament_time,
                                      const std::unordered_map<unsigned int, double> &max_time,
                                      double                                          min_layer_time_s)
{
    // Each filament grows with a common factor f until it reaches its limit, at its saturation factor limit / time.
    // Going through the filaments by increasing saturation, the total at f is (time of the capped ones) +
    // f * (time of the others); solve for the f that gives the minimum layer time.
    std::vector<std::pair<double, unsigned int>> saturation; // (factor, filament)
    double                                       total = 0.;
    for (const auto &[filament, time] : filament_time) {
        if (time <= 0.)
            continue;
        total += time;
        auto it = max_time.find(filament);
        saturation.emplace_back(it == max_time.end() ? std::numeric_limits<double>::infinity() : std::max(1., it->second / time), filament);
    }
    if (total <= 0. || total >= min_layer_time_s)
        return;
    std::sort(saturation.begin(), saturation.end());
    double capped = 0., uncapped = total, factor = 1.;
    for (const auto &[saturates_at, filament] : saturation) {
        factor = (min_layer_time_s - capped) / uncapped;
        if (factor <= saturates_at)
            break;
        // This filament reaches its limit first; the others take the rest.
        factor = saturates_at;
        capped += saturates_at * filament_time[filament];
        uncapped -= filament_time[filament];
        if (uncapped <= 0.)
            break;
    }
    for (auto &[filament, time] : filament_time) {
        auto it = max_time.find(filament);
        time    = std::min(time * factor, it == max_time.end() ? std::numeric_limits<double>::infinity() : std::max(time, it->second));
    }
}

// Estimated clock of the planned tool sequence, to tell how long each nozzle was parked. Each layer prints its
// filaments in the planned order; a change of nozzle costs change_time_s plus the reload use() is given, and the
// tower prime is added with wait(); a filament prints for its estimated share of the layer. The estimate leaves out
// travel, layer changes and parts of the tower, so idle times come out somewhat short of the printer's.
class ToolIdleClock
{
public:
    // layer_time[layer][filament]: estimated print time in seconds; layers and filaments without an entry take no
    // time. first_nozzle: the nozzle active at the start, if known.
    ToolIdleClock(std::vector<std::unordered_map<unsigned int, double>> layer_time, double change_time_s, std::optional<int> first_nozzle)
        : m_layer_time(std::move(layer_time)), m_change_time(std::max(0., change_time_s)), m_active(first_nozzle)
    {
        if (m_active)
            m_parked_at[*m_active] = 0.;
    }

    // Prints filament's share of layer layer_idx from nozzle, swapping to that nozzle first unless it is active,
    // after reload_s more seconds (loading the filament, priming on the tower). Returns the seconds the nozzle was
    // parked before this swap, a negative value if it has not printed yet, and 0 if it was active.
    double use(size_t layer_idx, unsigned int filament, int nozzle, double reload_s = 0.)
    {
        double idle = 0.;
        if (m_active != nozzle) {
            if (m_active) {
                m_parked_at[*m_active] = m_now;
                m_now += m_change_time;
            }
            auto it  = m_parked_at.find(nozzle);
            idle     = it == m_parked_at.end() ? -1. : m_now - it->second;
            m_active = nozzle;
        }
        m_now += std::max(0., reload_s);
        this->print(layer_idx, filament);
        return idle;
    }

    // Prints filament's share of layer layer_idx without a nozzle of its own in the plan: the time passes, the
    // active nozzle stays.
    void pass(size_t layer_idx, unsigned int filament) { this->print(layer_idx, filament); }

    // Time passing with the active nozzle, e.g. priming on the tower.
    void wait(double seconds) { m_now += std::max(0., seconds); }

    double now() const { return m_now; }

private:
    void print(size_t layer_idx, unsigned int filament)
    {
        if (layer_idx < m_layer_time.size()) {
            auto it = m_layer_time[layer_idx].find(filament);
            if (it != m_layer_time[layer_idx].end())
                m_now += it->second;
        }
    }

    std::vector<std::unordered_map<unsigned int, double>> m_layer_time;
    double                                                m_change_time;
    std::optional<int>                                    m_active;
    double                                                m_now = 0.;
    // When each nozzle last stopped printing (the active nozzle's entry is updated when it is parked).
    std::unordered_map<int, double>                       m_parked_at;
};

} // namespace Slic3r

#endif // slic3r_GCode_IdlePrime_hpp_
