#ifndef slic3r_CpuMemory_hpp_
#define slic3r_CpuMemory_hpp_

namespace Slic3r {
#define LOD_FREE_MEMORY_SIZE 5
class CpuMemory
{
public:
    static bool cur_free_memory_less_than_specify_size_gb(int size);
};
} // namespace Slic3r
#endif
