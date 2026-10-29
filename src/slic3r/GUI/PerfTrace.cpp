#include "PerfTrace.hpp"

#include <boost/log/trivial.hpp>

namespace Slic3r::GUI {

void perf_mark(const std::string& label)
{
    BOOST_LOG_TRIVIAL(info) << "[perf] " << label;
}

PerfTimer::PerfTimer(const std::string& name)
    : m_name(name), m_start(std::chrono::steady_clock::now())
{
}

PerfTimer::~PerfTimer()
{
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - m_start).count();
    BOOST_LOG_TRIVIAL(info) << "[perf] " << m_name << " cost " << ms << " ms";
}

} // namespace Slic3r::GUI
