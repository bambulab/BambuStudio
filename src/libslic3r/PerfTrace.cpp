#include "PerfTrace.hpp"
#include "libslic3r/Log.hpp"

namespace Slic3r {

void perf_mark(const std::string& label)
{
    BOOST_LOG_TRIVIAL(info) << "[perf] " << label;
}

PerfTimer::PerfTimer(const std::string &name, bool enabled) : m_name(name), m_start(std::chrono::steady_clock::now()), m_enabled(enabled)
{
    if (m_enabled)
        BOOST_LOG_TRIVIAL(info) << "[perf] " << m_name << " begin";
}

PerfTimer::~PerfTimer()
{
    if (!m_enabled)
        return;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_start).count();
    BOOST_LOG_TRIVIAL(info) << "[perf] " << m_name << " cost " << ms << " ms";
    flush_logs();
}

} // namespace Slic3r
