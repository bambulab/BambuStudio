#ifndef slic3r_Format_STEP_hpp_
#define slic3r_Format_STEP_hpp_

namespace Slic3r {

class TriangleMesh;
class ModelObject;

//BBS: Load an step file into a provided model.
extern bool load_step(const char *path, Model *model);

}; // namespace Slic3r

#endif /* slic3r_Format_STEP_hpp_ */
