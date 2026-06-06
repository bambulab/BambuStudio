#ifndef slic3r_PrintObjectSliceInterlocking_hpp_
#define slic3r_PrintObjectSliceInterlocking_hpp_

#include <functional>

namespace Slic3r {

class PrintObject;

void apply_interlocking_features(PrintObject& print_object, const std::function<void()>& throw_if_canceled);

} // namespace Slic3r

#endif // slic3r_PrintObjectSliceInterlocking_hpp_
