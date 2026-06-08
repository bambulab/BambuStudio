#include "CpuMemory.hpp"

#include <boost/log/trivial.hpp>
namespace Slic3r {
#ifdef _WIN32
#include <windows.h>
unsigned long long get_free_memory_win()
{
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    GlobalMemoryStatusEx(&status);
    return status.ullAvailPhys;
}
#endif

#ifdef __linux__
#include <unistd.h>
#include <sys/sysinfo.h>
#elif __APPLE__
#include <mach/mach_host.h>
#include <sys/sysctl.h>
#endif
#if defined(__linux__) || defined(__APPLE__)
unsigned long long get_free_memory_unix()
{
#ifdef __linux__
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return info.freeram * info.mem_unit;
    }
#elif __APPLE__
    int      mib[2] = {CTL_HW, HW_MEMSIZE};
    uint64_t memsize;
    size_t   len = sizeof(memsize);
    if (sysctl(mib, 2, &memsize, &len, NULL, 0) == 0) {
        vm_size_t              page_size;
        mach_port_t            mach_port;
        mach_msg_type_number_t count;
        vm_statistics64_data_t vm_stats;

        mach_port = mach_host_self();
        count     = sizeof(vm_stats) / sizeof(natural_t);
        if (host_page_size(mach_port, &page_size) == KERN_SUCCESS && host_statistics64(mach_port, HOST_VM_INFO, (host_info64_t) &vm_stats, &count) == KERN_SUCCESS) {
            return (vm_stats.free_count + vm_stats.inactive_count) * page_size;
        }
    }
#endif
    return 0;
}
#endif
unsigned long long get_free_memory()
{
#ifdef _WIN32
    return get_free_memory_win();
#elif defined(__linux__) || defined(__APPLE__)
    return get_free_memory_unix();
#else
    return 0;
#endif
}
bool CpuMemory::cur_free_memory_less_than_specify_size_gb(int size)
{
    unsigned long long free_mem = get_free_memory();
    auto cur_size = free_mem / (1024.0 * 1024.0 * 1024.0);
    static bool first_debug_free_memory = true;
    static bool first_meet_size_gb      = true;
    if (first_debug_free_memory) {
        first_debug_free_memory = false;
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " cur_size = " << cur_size << "GB";
    }
    if (cur_size < size) {
        if (first_meet_size_gb) {
            first_meet_size_gb = false;
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " cur_size = " << cur_size << "GB" << "first_meet_size_gb ";
        }
        return true;
    }
    return false;
}

} // namespace Slic3r
