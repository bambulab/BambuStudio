#ifndef slic3r_Format_SLDPRT_hpp_
#define slic3r_Format_SLDPRT_hpp_

#include "STL.hpp"

namespace Slic3r {
class Model;
bool load_sldprt(const char *path, Model *model, bool &cancelled, ImportstlProgressFn progress = nullptr);
}
#endif
