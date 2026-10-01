#ifndef slic3r_Debounce_hpp_
#define slic3r_Debounce_hpp_

#include <chrono>

namespace Slic3r {

// Rate-limit a recurring action.
//
// Returns true and updates `last_tp` when at least `interval` has elapsed
// since the last accepted call.  On the very first call (last_tp ==
// time_point{}) the action is always accepted.
//
// Usage:
//   static auto s_last = std::chrono::steady_clock::time_point{};
//   if (debounce_elapsed(s_last, std::chrono::seconds(5)))
//       do_expensive_thing();
//
// The function is intentionally free of wx / GUI dependencies so it can be
// exercised by the plain libslic3r unit-test suite.
inline bool debounce_elapsed(
    std::chrono::steady_clock::time_point &last_tp,
    std::chrono::steady_clock::duration    interval)
{
    const auto now = std::chrono::steady_clock::now();
    if (now - last_tp >= interval) {
        last_tp = now;
        return true;
    }
    return false;
}

} // namespace Slic3r

#endif // slic3r_Debounce_hpp_
