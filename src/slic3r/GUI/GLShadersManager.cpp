#include "libslic3r/libslic3r.h"
#include "libslic3r/Platform.hpp"
#include "GLShadersManager.hpp"
#include "3DScene.hpp"
#include "GUI_App.hpp"
#include "GLShader.hpp"

#include <cassert>
#include <algorithm>
#include <string_view>
using namespace std::literals;

#include <GL/glew.h>

namespace Slic3r {

std::pair<bool, std::string> GLShadersManager::init()
{
    std::string error;

    auto append_shader = [this, &error](const std::string& name, const GLShaderProgram::ShaderFilenames& filenames,
        const std::initializer_list<std::string_view> &defines = {}) {
        m_shaders.push_back(std::make_unique<GLShaderProgram>());
        if (!m_shaders.back()->init_from_files(name, filenames, defines)) {
            error += name + "\n";
            // if any error happens while initializating the shader, we remove it from the list
            m_shaders.pop_back();
            return false;
        }
        return true;
    };

    assert(m_shaders.empty());

    bool valid = true;

    const std::string glsl_version_prefix = GUI::wxGetApp().is_gl_version_greater_or_equal_to(3, 1) ? "140/" : "110/";

    // used to render bed axes and model, selection hints, gcode sequential view marker model, preview shells, options in gcode preview
    valid &= append_shader("gouraud_light", { glsl_version_prefix + "gouraud_light.vs", glsl_version_prefix + "gouraud_light.fs" });
    //used to render thumbnail
    valid &= append_shader("thumbnail", { glsl_version_prefix + "thumbnail.vs", glsl_version_prefix + "thumbnail.fs" });
    // used to render first layer for calibration
    valid &= append_shader("flat", { glsl_version_prefix + "flat.vs", glsl_version_prefix + "flat.fs"});
    valid &= append_shader("flat_instance", { glsl_version_prefix + "flat_instance.vs", glsl_version_prefix + "flat.fs"});
    // used to render printbed
    valid &= append_shader("printbed", { glsl_version_prefix + "printbed.vs", glsl_version_prefix + "printbed.fs"});
    valid &= append_shader("hotbed", { glsl_version_prefix + "hotbed.vs", glsl_version_prefix + "hotbed.fs"});
    // used to render options in gcode preview
    if (GUI::wxGetApp().is_gl_version_greater_or_equal_to(3, 3))
        valid &= append_shader("gouraud_light_instanced", { glsl_version_prefix + "gouraud_light_instanced.vs", glsl_version_prefix + "gouraud_light.fs" });
    // used to render extrusion and travel paths as lines in gcode preview
    valid &= append_shader("toolpaths_lines", { glsl_version_prefix + "toolpaths_lines.vs", glsl_version_prefix + "toolpaths_lines.fs" });

    // used to render objects in 3d editor
    valid &= append_shader("gouraud", { glsl_version_prefix + "gouraud.vs", glsl_version_prefix + "gouraud.fs" }
#if ENABLE_ENVIRONMENT_MAP
        , { "ENABLE_ENVIRONMENT_MAP"sv }
#endif // ENABLE_ENVIRONMENT_MAP
        );
    // used to render variable layers heights in 3d editor
    valid &= append_shader("variable_layer_height", { glsl_version_prefix + "variable_layer_height.vs", glsl_version_prefix + "variable_layer_height.fs" });
    // used to render highlight contour around selected triangles inside the multi-material gizmo
    valid &= append_shader("mm_contour", { glsl_version_prefix + "mm_contour.vs", glsl_version_prefix + "mm_contour.fs" });
    // Used to render painted triangles inside the multi-material gizmo. Triangle normals are computed inside fragment shader.
    // For Apple's on Arm CPU computed triangle normals inside fragment shader using dFdx and dFdy has the opposite direction.
    // Because of this, objects had darker colors inside the multi-material gizmo.
    // Based on https://stackoverflow.com/a/66206648, the similar behavior was also spotted on some other devices with Arm CPU.
    // Since macOS 12 (Monterey), this issue with the opposite direction on Apple's Arm CPU seems to be fixed, and computed
    // triangle normals inside fragment shader have the right direction.
    if (platform_flavor() == PlatformFlavor::OSXOnArm && wxPlatformInfo::Get().GetOSMajorVersion() < 12) {
        //if (GUI::wxGetApp().plater() && GUI::wxGetApp().plater()->is_wireframe_enabled())
        //    valid &= append_shader("mm_gouraud", {"mm_gouraud_wireframe.vs", "mm_gouraud_wireframe.fs"}, {"FLIP_TRIANGLE_NORMALS"sv});
        //else
            valid &= append_shader("mm_gouraud", { glsl_version_prefix + "mm_gouraud_wireframe.vs", glsl_version_prefix + "mm_gouraud_wireframe.fs"}, {"FLIP_TRIANGLE_NORMALS"sv});//{"mm_gouraud.vs", "mm_gouraud.fs"}
    }
    else {
        //if (GUI::wxGetApp().plater() && GUI::wxGetApp().plater()->is_wireframe_enabled())
        //    valid &= append_shader("mm_gouraud", {"mm_gouraud_wireframe.vs", "mm_gouraud_wireframe.fs"});
        //else
            valid &= append_shader("mm_gouraud", { glsl_version_prefix + "mm_gouraud_wireframe.vs", glsl_version_prefix + "mm_gouraud_wireframe.fs"});//{"mm_gouraud.vs", "mm_gouraud.fs"}
    }

    valid &= append_shader("silhouette", { glsl_version_prefix + "silhouette.vs", glsl_version_prefix + "silhouette.fs" });

    valid &= append_shader("silhouette_composite", { glsl_version_prefix + "silhouette_composite.vs", glsl_version_prefix + "silhouette_composite.fs" });

    valid &= append_shader("background", { glsl_version_prefix + "background.vs", glsl_version_prefix + "background.fs" });

    valid &= append_shader("flat_texture", { glsl_version_prefix + "flat_texture.vs", glsl_version_prefix + "flat_texture.fs" });

    valid &= append_shader("imgui", { glsl_version_prefix + "imgui.vs", glsl_version_prefix + "imgui.fs" });

    valid &= append_shader("mainframe_composite", { glsl_version_prefix + "mainframe_composite.vs", glsl_version_prefix + "mainframe_composite.fs" });

    valid &= append_shader("fxaa", { glsl_version_prefix + "fxaa.vs", glsl_version_prefix + "fxaa.fs" });

    valid &= append_shader("gaussian_blur33", { glsl_version_prefix + "gaussian_blur33.vs", glsl_version_prefix + "gaussian_blur33.fs" });

    return { valid, error };
}

void GLShadersManager::shutdown()
{
    m_shaders.clear();
}

const std::shared_ptr<GLShaderProgram>& GLShadersManager::get_shader(const std::string& shader_name) const
{
    const auto& it = std::find_if(m_shaders.begin(), m_shaders.end(), [&shader_name](const std::shared_ptr<GLShaderProgram>& p) { return p->get_name() == shader_name; });
    if (it != m_shaders.end()) {
        return *it;
    }
    static std::shared_ptr<GLShaderProgram> s_empty_shader{ nullptr };
    return s_empty_shader;
}

std::shared_ptr<GLShaderProgram> GLShadersManager::get_current_shader() const
{
    auto rt = m_current_shader.lock();
    return rt;
}

void GLShadersManager::bind_shader(const std::shared_ptr<GLShaderProgram>& p_shader)
{
    if (p_shader) {
        p_shader->start_using();
    }
    else {
        glsafe(::glUseProgram(0));
    }

    m_current_shader = p_shader;
}

void GLShadersManager::unbind_shader()
{
    glsafe(::glUseProgram(0));
    m_current_shader.reset();
}

} // namespace Slic3r

