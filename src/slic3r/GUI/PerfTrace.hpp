#pragma once

#include <string>
#include <chrono>

namespace Slic3r::GUI {

// Lightweight performance tracing for startup / UI flows.

// Log a one-shot marker for the current moment:
//   [perf] <label>
// For timing a scope, use the PERF_TRACE macro instead.
void perf_mark(const std::string &label);

// RAII scope timer. Logs the elapsed time when the scope exits:
//   [perf] <name> cost <N> ms
// Prefer the PERF_TRACE macro over constructing this directly.
class PerfTimer
{
    std::string                           m_name;
    std::chrono::steady_clock::time_point m_start;

public:
    explicit PerfTimer(const std::string &name);
    ~PerfTimer();

    PerfTimer(const PerfTimer &)            = delete;
    PerfTimer &operator=(const PerfTimer &) = delete;
};

// Convenience: time the current scope, logging "[perf] <name> cost <N> ms"
#define PERF_TRACE_CONCAT_(a, b) a##b
#define PERF_TRACE_CONCAT(a, b) PERF_TRACE_CONCAT_(a, b)
#define PERF_TRACE(name) ::Slic3r::GUI::PerfTimer PERF_TRACE_CONCAT(perf_timer_, __LINE__)(name)

} // namespace Slic3r::GUI
