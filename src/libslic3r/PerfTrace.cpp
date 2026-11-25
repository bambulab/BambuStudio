#include "PerfTrace.hpp"
#include "libslic3r/Log.hpp"

namespace Slic3r {

namespace {

std::chrono::steady_clock::time_point &perf_epoch()
{
    static std::chrono::steady_clock::time_point epoch = std::chrono::steady_clock::now();
    return epoch;
}

long long ms_since_startup()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - perf_epoch())
        .count();
}

} // namespace

void perf_startup_begin(const std::string &label)
{
    perf_epoch() = std::chrono::steady_clock::now();
    BOOST_LOG_TRIVIAL(info) << "[perf] +0ms " << label << " begin";
    flush_logs();
}

void perf_mark(const std::string &label)
{
    BOOST_LOG_TRIVIAL(info) << "[perf] +" << ms_since_startup() << "ms " << label;
    flush_logs();
}

PerfTimer::PerfTimer(const std::string &name, bool enabled)
    : m_name(name), m_start(std::chrono::steady_clock::now()), m_enabled(enabled)
{
    if (m_enabled)
        BOOST_LOG_TRIVIAL(info) << "[perf] +" << ms_since_startup() << "ms " << m_name << " begin";
}

PerfTimer::~PerfTimer()
{
    if (!m_enabled)
        return;
    const auto cost = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - m_start)
                          .count();
    BOOST_LOG_TRIVIAL(info) << "[perf] +" << ms_since_startup() << "ms " << m_name << " cost " << cost
                            << " ms";
    flush_logs();
}

} // namespace Slic3r
