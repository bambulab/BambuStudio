#include "Print.hpp"
#include "Utils.hpp"

namespace Slic3r {

int Print::export_cached_data(const std::string& /*directory*/, int& obj_cnt_exported, bool /*with_space*/)
{
    obj_cnt_exported = 0;
    return CLI_EXPORT_CACHE_WRITE_FAILED;
}

int Print::load_cached_data(const std::string& /*directory*/)
{
    return CLI_IMPORT_CACHE_LOAD_FAILED;
}

} // namespace Slic3r
