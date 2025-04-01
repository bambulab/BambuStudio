#ifndef slic3r_GLShadersManager_hpp_
#define slic3r_GLShadersManager_hpp_

#include <vector>
#include <string>
#include <memory>

namespace Slic3r {

class GLShaderProgram;

class GLShadersManager
{
    std::vector<std::shared_ptr<GLShaderProgram>> m_shaders;

public:
    std::pair<bool, std::string> init();
    // call this method before to release the OpenGL context
    void shutdown();

    // returns nullptr if not found
    const std::shared_ptr<GLShaderProgram>& get_shader(const std::string& shader_name) const;

    // returns currently active shader, nullptr if none
    std::shared_ptr<GLShaderProgram> get_current_shader() const;

    void bind_shader(const std::shared_ptr<GLShaderProgram>& p_shader);

    void unbind_shader();

private:
    std::weak_ptr<GLShaderProgram> m_current_shader;
};

} // namespace Slic3r

#endif //  slic3r_GLShadersManager_hpp_
