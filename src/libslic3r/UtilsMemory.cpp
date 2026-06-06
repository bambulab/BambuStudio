#include "Utils.hpp"

#include <boost/log/trivial.hpp>

#ifdef WIN32
#include <Windows.h>
#include <Psapi.h>
#endif

namespace Slic3r {

std::string format_memsize_MB(size_t n)
{
    std::string out;
    size_t      n2    = 0;
    size_t      scale = 1;
    n += 500000;
    n /= 1000000;
    while (n >= 1000) {
        n2 = n2 + scale * (n % 1000);
        n /= 1000;
        scale *= 1000;
    }
    char buf[8];
    sprintf(buf, "%d", (int) n);
    out = buf;
    while (scale != 1) {
        scale /= 1000;
        n = n2 / scale;
        n2 = n2 % scale;
        sprintf(buf, ",%03d", (int) n);
        out += buf;
    }
    return out + "MB";
}

std::string log_memory_info(bool ignore_loglevel)
{
    std::string out;
    if (!ignore_loglevel)
        return out;
#ifdef WIN32
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *) &pmc, sizeof(pmc)))
        out = " WorkingSet: " + format_memsize_MB(pmc.WorkingSetSize) + "; PrivateBytes: " + format_memsize_MB(pmc.PrivateUsage) + "; Pagefile(peak): " +
              format_memsize_MB(pmc.PagefileUsage) + "(" + format_memsize_MB(pmc.PeakPagefileUsage) + ")";
    else
        out += " Used memory: N/A";
#endif
    return out;
}

} // namespace Slic3r
