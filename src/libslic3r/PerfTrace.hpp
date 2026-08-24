#pragma once

#include <string>
#include <chrono>

namespace Slic3r {

// Lightweight performance tracing for startup / UI flows.

// Log a one-shot marker for the current moment:
//   [perf] <label>
// For timing a scope, use the PERF_TRACE macro instead.
void perf_mark(const std::string &label);

// RAII scope timer. Logs the elapsed time when the scope exits:
//   [perf] <name> cost <N> ms
// Pass enabled == false to make the timer a no-op, so a call path that also runs
// on a hot/periodic flow (e.g. the 10 s auto-backup save) does not spam the log.
// Prefer the PERF_TRACE / PERF_TRACE_IF macros over constructing this directly.
class PerfTimer
{
    std::string                           m_name;
    std::chrono::steady_clock::time_point m_start;
    bool                                  m_enabled;

public:
    explicit PerfTimer(const std::string &name, bool enabled = true);
    ~PerfTimer();

    PerfTimer(const PerfTimer &)            = delete;
    PerfTimer &operator=(const PerfTimer &) = delete;
};

// Convenience: time the current scope, logging "[perf] <name> cost <N> ms"
#define PERF_TRACE_CONCAT_(a, b) a##b
#define PERF_TRACE_CONCAT(a, b) PERF_TRACE_CONCAT_(a, b)
#define PERF_TRACE(name) ::Slic3r::PerfTimer PERF_TRACE_CONCAT(perf_timer_, __LINE__)(name)

// Same as PERF_TRACE, but only logs when cond is true. Use it in code shared by an
// interactive flow and a periodic one, to keep the trace to the interactive case.
#define PERF_TRACE_IF(cond, name) ::Slic3r::PerfTimer PERF_TRACE_CONCAT(perf_timer_, __LINE__)(name, (cond))

} // namespace Slic3r
