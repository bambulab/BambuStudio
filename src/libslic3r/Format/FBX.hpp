#pragma once

#include <string>

namespace Slic3r {

struct TexturedMesh;

bool load_fbx(const std::string& path, TexturedMesh& out);

} // namespace Slic3r
