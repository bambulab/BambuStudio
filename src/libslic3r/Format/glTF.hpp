#pragma once

#include <string>

namespace Slic3r {

struct TexturedMesh;

bool load_gltf(const std::string& path, TexturedMesh& out);

} // namespace Slic3r
