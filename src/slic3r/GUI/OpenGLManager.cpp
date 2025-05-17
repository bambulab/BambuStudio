#include "libslic3r/libslic3r.h"
#include "OpenGLManager.hpp"

#include "GUI.hpp"
#include "I18N.hpp"
#include "3DScene.hpp"

#include "libslic3r/Platform.hpp"
#include "slic3r/GUI/GLTexture.hpp"

#include <GL/glew.h>

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/log/trivial.hpp>

#include <wx/glcanvas.h>
#include <wx/msgdlg.h>

#ifdef __APPLE__
// Part of hack to remove crash when closing the application on OSX 10.9.5 when building against newer wxWidgets
#include <wx/platinfo.h>

#include "../Utils/MacDarkMode.hpp"
#endif // __APPLE__

#define BBS_GL_EXTENSION_FUNC(_func) (OpenGLManager::get_framebuffers_type() == OpenGLManager::EFramebufferType::Ext ? _func ## EXT : _func)
#define BBS_GL_EXTENSION_PARAMETER(_param) OpenGLManager::get_framebuffers_type() == OpenGLManager::EFramebufferType::Ext ? _param ## _EXT : _param


static uint8_t get_msaa_samples(Slic3r::GUI::EMSAAType msaa_type) {
    uint8_t num_samples = 0;
    switch (msaa_type)
    {
    case Slic3r::GUI::EMSAAType::X2:
        num_samples = 2;
        break;
    case Slic3r::GUI::EMSAAType::X8:
        num_samples = 8;
        break;
    case Slic3r::GUI::EMSAAType::X16:
        num_samples = 16;
        break;
    case Slic3r::GUI::EMSAAType::X4:
        num_samples = 4;
    default:
        break;
    }

    return num_samples;
}

static GLenum get_pixel_format(Slic3r::GUI::EPixelFormat type) {
    switch (type)
    {
    case Slic3r::GUI::EPixelFormat::RGBA:
        return GL_RGBA;
    case Slic3r::GUI::EPixelFormat::DepthComponent:
        return GL_DEPTH_COMPONENT;
    case Slic3r::GUI::EPixelFormat::StencilIndex:
        return GL_STENCIL_INDEX;
    case Slic3r::GUI::EPixelFormat::DepthAndStencil:
        return GL_DEPTH_STENCIL;
    }

    return GL_INVALID_ENUM;
}

static GLenum get_pixel_data_type(Slic3r::GUI::EPixelDataType type) {
    switch (type)
    {
    case Slic3r::GUI::EPixelDataType::UByte:
        return GL_UNSIGNED_BYTE;
    case Slic3r::GUI::EPixelDataType::Byte:
        return GL_BYTE;
    case Slic3r::GUI::EPixelDataType::UShort:
        return GL_UNSIGNED_SHORT;
    case Slic3r::GUI::EPixelDataType::Short:
        return GL_SHORT;
    case Slic3r::GUI::EPixelDataType::UInt:
        return GL_UNSIGNED_INT;
    case Slic3r::GUI::EPixelDataType::Int:
        return GL_INT;
    case Slic3r::GUI::EPixelDataType::Float:
        return GL_FLOAT;
    }

    return GL_INVALID_ENUM;
}

static bool version_to_major_minor(const std::string& version, unsigned int& major, unsigned int& minor)
{
    major = 0;
    minor = 0;

    if (version == "N/A")
        return false;

    std::vector<std::string> tokens;
    boost::split(tokens, version, boost::is_any_of(" "), boost::token_compress_on);

    if (tokens.empty())
        return false;

    std::vector<std::string> numbers;
    boost::split(numbers, tokens[0], boost::is_any_of("."), boost::token_compress_on);

    if (numbers.size() > 0)
        major = ::atoi(numbers[0].c_str());

    if (numbers.size() > 1)
        minor = ::atoi(numbers[1].c_str());

    return true;
}

namespace Slic3r {
namespace GUI {

// A safe wrapper around glGetString to report a "N/A" string in case glGetString returns nullptr.
std::string gl_get_string_safe(GLenum param, const std::string& default_value)
{
    const char* value = (const char*)::glGetString(param);
    return std::string((value != nullptr) ? value : default_value);
}

std::string OpenGLManager::s_back_frame = "backframe";
std::string OpenGLManager::s_picking_frame = "pickingframe";

const std::string& OpenGLManager::GLInfo::get_version() const
{
    if (!m_detected)
        detect();

    return m_version;
}

const uint32_t OpenGLManager::GLInfo::get_formated_gl_version() const
{
    if (0 == m_formated_gl_version)
    {
        unsigned int major = 0;
        unsigned int minor = 0;
        if (!m_detected)
            detect();
        version_to_major_minor(m_version, major, minor);
        m_formated_gl_version = major * 10 + minor;
    }
    return m_formated_gl_version;
}

const std::string& OpenGLManager::GLInfo::get_glsl_version() const
{
    if (!m_detected)
        detect();

    return m_glsl_version;
}

const std::string& OpenGLManager::GLInfo::get_vendor() const
{
    if (!m_detected)
        detect();

    return m_vendor;
}

const std::string& OpenGLManager::GLInfo::get_renderer() const
{
    if (!m_detected)
        detect();

    return m_renderer;
}

int OpenGLManager::GLInfo::get_max_tex_size() const
{
    if (!m_detected)
        detect();

    // clamp to avoid the texture generation become too slow and use too much GPU memory
#ifdef __APPLE__
    // and use smaller texture for non retina systems
    return (Slic3r::GUI::mac_max_scaling_factor() > 1.0) ? std::min(m_max_tex_size, 8192) : std::min(m_max_tex_size / 2, 4096);
#else
    // and use smaller texture for older OpenGL versions
    return is_version_greater_or_equal_to(3, 0) ? std::min(m_max_tex_size, 8192) : std::min(m_max_tex_size / 2, 4096);
#endif // __APPLE__
}

float OpenGLManager::GLInfo::get_max_anisotropy() const
{
    if (!m_detected)
        detect();

    return m_max_anisotropy;
}

void OpenGLManager::GLInfo::detect() const
{
    *const_cast<std::string*>(&m_version) = gl_get_string_safe(GL_VERSION, "N/A");
    *const_cast<std::string*>(&m_glsl_version) = gl_get_string_safe(GL_SHADING_LANGUAGE_VERSION, "N/A");
    *const_cast<std::string*>(&m_vendor) = gl_get_string_safe(GL_VENDOR, "N/A");
    *const_cast<std::string*>(&m_renderer) = gl_get_string_safe(GL_RENDERER, "N/A");

    BOOST_LOG_TRIVIAL(info) << boost::format("got opengl version %1%, glsl version %2%, vendor %3% , graphics card model %4%") % m_version % m_glsl_version % m_vendor % m_renderer << std::endl;

    int* max_tex_size = const_cast<int*>(&m_max_tex_size);
    glsafe(::glGetIntegerv(GL_MAX_TEXTURE_SIZE, max_tex_size));

    *max_tex_size /= 2;

    if (Slic3r::total_physical_memory() / (1024 * 1024 * 1024) < 6)
        *max_tex_size /= 2;

    if (GLEW_EXT_texture_filter_anisotropic) {
        float* max_anisotropy = const_cast<float*>(&m_max_anisotropy);
        glsafe(::glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, max_anisotropy));
    }
    int max_offscreen_msaa = 0;
    glsafe(::glGetIntegerv(GL_MAX_SAMPLES, &max_offscreen_msaa));
    const_cast<OpenGLManager::GLInfo*>(this)->m_max_offscreen_msaa = max_offscreen_msaa;

    *const_cast<bool*>(&m_detected) = true;
}

static bool version_greater_or_equal_to(const std::string& version, unsigned int major, unsigned int minor)
{
    unsigned int gl_major = 0;
    unsigned int gl_minor = 0;
    const bool rt = version_to_major_minor(version, gl_major, gl_minor);
    if (!rt)
        return false;

    if (gl_major < major)
        return false;
    else if (gl_major > major)
        return true;
    else
        return gl_minor >= minor;
}

bool OpenGLManager::GLInfo::is_version_greater_or_equal_to(unsigned int major, unsigned int minor) const
{
    if (!m_detected)
        detect();

    return version_greater_or_equal_to(m_version, major, minor);
}

bool OpenGLManager::GLInfo::is_glsl_version_greater_or_equal_to(unsigned int major, unsigned int minor) const
{
    if (!m_detected)
        detect();

    return version_greater_or_equal_to(m_glsl_version, major, minor);
}

uint8_t OpenGLManager::GLInfo::get_max_offscreen_msaa() const
{
    if (!m_detected)
        detect();
    return m_max_offscreen_msaa;
}

// If formatted for github, plaintext with OpenGL extensions enclosed into <details>.
// Otherwise HTML formatted for the system info dialog.
std::string OpenGLManager::GLInfo::to_string(bool for_github) const
{
    if (!m_detected)
        detect();

    std::stringstream out;

    const bool format_as_html = ! for_github;
    std::string h2_start = format_as_html ? "<b>" : "";
    std::string h2_end = format_as_html ? "</b>" : "";
    std::string b_start = format_as_html ? "<b>" : "";
    std::string b_end = format_as_html ? "</b>" : "";
    std::string line_end = format_as_html ? "<br>" : "\n";

    out << h2_start << "OpenGL installation" << h2_end << line_end;
    out << b_start << "GL version:   " << b_end << m_version << line_end;
    out << b_start << "Vendor:       " << b_end << m_vendor << line_end;
    out << b_start << "Renderer:     " << b_end << m_renderer << line_end;
    out << b_start << "GLSL version: " << b_end << m_glsl_version << line_end;

    {
        std::vector<std::string> extensions_list;
        std::string extensions_str = gl_get_string_safe(GL_EXTENSIONS, "");
        boost::split(extensions_list, extensions_str, boost::is_any_of(" "), boost::token_compress_on);

        if (!extensions_list.empty()) {
            if (for_github)
                out << "<details>\n<summary>Installed extensions:</summary>\n";
            else
                out << h2_start << "Installed extensions:" << h2_end << line_end;

            std::sort(extensions_list.begin(), extensions_list.end());
            for (const std::string& ext : extensions_list)
                if (! ext.empty())
                    out << ext << line_end;

            if (for_github)
                out << "</details>\n";
        }
    }

    return out.str();
}

OpenGLManager::GLInfo OpenGLManager::s_gl_info;
bool OpenGLManager::s_compressed_textures_supported = false;
bool OpenGLManager::m_use_manually_generated_mipmaps = true;
OpenGLManager::EMultisampleState OpenGLManager::s_multisample = OpenGLManager::EMultisampleState::Unknown;
OpenGLManager::EFramebufferType OpenGLManager::s_framebuffers_type = OpenGLManager::EFramebufferType::Unknown;
bool OpenGLManager::s_b_initialized = false;
ColorRGBA OpenGLManager::s_cut_plane_color = {1.0f, 0.37f, 0.0f, 1.0f};
bool      OpenGLManager::s_cancle_glmultidraw             = false;
#ifdef __APPLE__
// Part of hack to remove crash when closing the application on OSX 10.9.5 when building against newer wxWidgets
OpenGLManager::OSInfo OpenGLManager::s_os_info;
#endif // __APPLE__

OpenGLManager::OpenGLManager()
{
}

OpenGLManager::~OpenGLManager()
{
    release_vao();
    Slic3r::GUI::GLTexture::shutdown();
    m_shaders_manager.shutdown();
    m_name_to_framebuffer.clear();

#ifdef __APPLE__
    // This is an ugly hack needed to solve the crash happening when closing the application on OSX 10.9.5 with newer wxWidgets
    // The crash is triggered inside wxGLContext destructor
    if (s_os_info.major != 10 || s_os_info.minor != 9 || s_os_info.micro != 5)
    {
#endif //__APPLE__
        if (m_context != nullptr)
            delete m_context;
#ifdef __APPLE__
    }
#endif //__APPLE__
}

bool OpenGLManager::init()
{
    if (s_b_initialized) {
        return true;
    }
    s_b_initialized = true;
    return s_b_initialized;
}

bool OpenGLManager::init_gl(bool popup_error)
{
    if (!m_gl_initialized) {
        glewExperimental = GL_TRUE;
        GLenum result = glewInit();
        if (result != GLEW_OK) {
            BOOST_LOG_TRIVIAL(error) << "Unable to init glew library";
            return false;
        }
	//BOOST_LOG_TRIVIAL(info) << "glewInit Success."<< std::endl;
        m_gl_initialized = true;
        if (GLEW_EXT_texture_compression_s3tc)
            s_compressed_textures_supported = true;
        else
            s_compressed_textures_supported = false;

        const auto& gl_info = OpenGLManager::get_gl_info();
        const uint32_t gl_formated_version = gl_info.get_formated_gl_version();
        if (gl_formated_version >= 30) {
            s_framebuffers_type = EFramebufferType::Supported;
            BOOST_LOG_TRIVIAL(info) << "Opengl version >= 30, FrameBuffer normal." << std::endl;
        }
        else if (GLEW_ARB_framebuffer_object) {
            s_framebuffers_type = EFramebufferType::Arb;
            BOOST_LOG_TRIVIAL(info) << "Found Framebuffer Type ARB."<< std::endl;
        }
        else if (GLEW_EXT_framebuffer_object) {
            BOOST_LOG_TRIVIAL(info) << "Found Framebuffer Type Ext."<< std::endl;
            s_framebuffers_type = EFramebufferType::Ext;
        }
        else {
            s_framebuffers_type = EFramebufferType::Unknown;
            BOOST_LOG_TRIVIAL(warning) << "Found Framebuffer Type unknown!"<< std::endl;
        }
        if (gl_formated_version >= 30) {
            m_vao_type = EVAOType::Core;
        }
#if defined(__APPLE__)
        else if (GLEW_APPLE_vertex_array_object) {
            m_vao_type = EVAOType::Apple;
        }
#endif
        else if (GLEW_ARB_vertex_array_object) {
            m_vao_type = EVAOType::Arb;
        }

        bool valid_version = s_gl_info.is_version_greater_or_equal_to(2, 0);
        if (!valid_version) {
            BOOST_LOG_TRIVIAL(error) << "Found opengl version <= 2.0"<< std::endl;
            // Complain about the OpenGL version.
            if (popup_error) {
                wxString message = from_u8((boost::format(
                    _utf8(L("The application cannot run normally because OpenGL version is lower than 2.0.\n")))).str());
                message += "\n";
                message += _L("Please upgrade your graphics card driver.");
                wxMessageBox(message, _L("Unsupported OpenGL version"), wxOK | wxICON_ERROR);
            }
        }

        if (valid_version)
        {
            // load shaders
            auto [result, error] = m_shaders_manager.init();
            if (!result) {
                BOOST_LOG_TRIVIAL(error) << "Unable to load shaders: "<<error<< std::endl;
                if (popup_error) {
                    wxString message = from_u8((boost::format(
                        _utf8(L("Unable to load shaders:\n%s"))) % error).str());
                    wxMessageBox(message, _L("Error loading shaders"), wxOK | wxICON_ERROR);
                }
            }
        }

#ifdef _WIN32
        // Since AMD driver version 22.7.1, there is probably some bug in the driver that causes the issue with the missing
        // texture of the bed. It seems that this issue only triggers when mipmaps are generated manually
        // (combined with a texture compression) and when mipmaps are generated through OpenGL glGenerateMipmap is working.
        // So, for newer drivers than 22.6.1, the last working driver version, we use mipmaps generated through OpenGL.
        if (const auto gl_info = OpenGLManager::get_gl_info(); boost::contains(gl_info.get_vendor(), "ATI Technologies Inc.")) {
            // WHQL drivers seem to have one more version number at the end besides non-WHQL drivers.
            //     WHQL: 4.6.14800 Compatibility Profile Context 22.6.1 30.0.21023.1015
            // Non-WHQL: 4.6.0 Compatibility Profile Context 22.8.1.220810
            std::regex version_rgx(R"(Compatibility\sProfile\sContext\s(\d+)\.(\d+)\.(\d+))");
            if (std::smatch matches; std::regex_search(gl_info.get_version(), matches, version_rgx) && matches.size() == 4) {
                int version_major = std::stoi(matches[1].str());
                int version_minor = std::stoi(matches[2].str());
                int version_patch = std::stoi(matches[3].str());
                BOOST_LOG_TRIVIAL(debug) << "Found AMD driver version: " << version_major << "." << version_minor << "." << version_patch;

                if (version_major > 22 || (version_major == 22 && version_minor > 6) || (version_major == 22 && version_minor == 6 && version_patch > 1)) {
                    m_use_manually_generated_mipmaps = false;
                    BOOST_LOG_TRIVIAL(debug) << "Mipmapping through OpenGL was enabled.";
                }
            } else {
                BOOST_LOG_TRIVIAL(error) << "Not recognized format of version.";
            }
        } else {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "not AMD driver.";
        }
#endif
    }

    return true;
}

wxGLContext* OpenGLManager::init_glcontext(wxGLCanvas& canvas)
{
    if (m_context == nullptr) {
        m_context = new wxGLContext(&canvas);

#ifdef __APPLE__
        // Part of hack to remove crash when closing the application on OSX 10.9.5 when building against newer wxWidgets
        s_os_info.major = wxPlatformInfo::Get().GetOSMajorVersion();
        s_os_info.minor = wxPlatformInfo::Get().GetOSMinorVersion();
        s_os_info.micro = wxPlatformInfo::Get().GetOSMicroVersion();
#endif //__APPLE__
    }
    return m_context;
}

void OpenGLManager::bind_shader(const std::shared_ptr<GLShaderProgram>& p_shader)
{
    m_shaders_manager.bind_shader(p_shader);
}

void OpenGLManager::unbind_shader()
{
    m_shaders_manager.unbind_shader();
}

void OpenGLManager::clear_dirty()
{
    m_b_viewport_dirty = false;
}

void OpenGLManager::set_viewport_size(uint32_t width, uint32_t height)
{
    if (width != m_viewport_width)
    {
        m_b_viewport_dirty = true;
        m_viewport_width = width;
    }
    if (height != m_viewport_height)
    {
        m_b_viewport_dirty = true;
        m_viewport_height = height;
    }

    if (m_b_viewport_dirty)
    {
        m_name_to_framebuffer.clear();
    }
}

void OpenGLManager::get_viewport_size(uint32_t& width, uint32_t& height) const
{
    width = m_viewport_width;
    height = m_viewport_height;
}

void OpenGLManager::_bind_frame_buffer(const std::string& name, EMSAAType msaa_type, uint32_t t_width, uint32_t t_height)
{
    if (OpenGLManager::s_back_frame == name) {
        const auto current_framebuffer = m_current_binded_framebuffer.lock();
        if (current_framebuffer) {
            current_framebuffer->unbind();
            m_current_binded_framebuffer.reset();
        }
        glsafe(BBS_GL_EXTENSION_FUNC(::glBindFramebuffer)(BBS_GL_EXTENSION_PARAMETER(GL_FRAMEBUFFER), 0));
        return;
    }
    uint32_t width = t_width == 0 ? m_viewport_width : t_width;
    uint32_t height = t_height == 0 ? m_viewport_height : t_height;
    if (s_picking_frame == name) {
        width = 1;
        height = 1;
    }

    uint8_t num_samples = get_msaa_samples(msaa_type);
    num_samples = std::min(s_gl_info.get_max_offscreen_msaa(), num_samples);

    FrameBufferParams t_fb_params;
    t_fb_params.m_width = width;
    t_fb_params.m_height = height;
    t_fb_params.m_msaa = num_samples;

    const auto& iter = m_name_to_framebuffer.find(name);
    bool needs_to_recreate = false;
    if (iter == m_name_to_framebuffer.end()) {
        needs_to_recreate = true;
    }
    else {
        needs_to_recreate = !iter->second->is_format_equal(t_fb_params);
    }
    if (needs_to_recreate) {
        const auto& p_frame_buffer = std::make_shared<FrameBuffer>(t_fb_params);
        m_name_to_framebuffer.insert_or_assign(name, p_frame_buffer);
    }

    const auto current_framebuffer = m_current_binded_framebuffer.lock();
    if (current_framebuffer != m_name_to_framebuffer[name]) {
        if (current_framebuffer) {
            current_framebuffer->unbind();
        }
    }
    m_name_to_framebuffer[name]->bind();
    m_current_binded_framebuffer = m_name_to_framebuffer[name];
}

void OpenGLManager::_unbind_frame_buffer(const std::string& name)
{
    const auto& iter = m_name_to_framebuffer.find(name);
    if (iter == m_name_to_framebuffer.end()) {
        return;
    }

    m_name_to_framebuffer[name]->unbind();
}

const std::shared_ptr<FrameBuffer>& OpenGLManager::get_frame_buffer(const std::string& name) const
{
    const auto& iter = m_name_to_framebuffer.find(name);
    if (iter != m_name_to_framebuffer.end()) {
        return iter->second;
    }
    static std::shared_ptr<FrameBuffer> sEmpty{ nullptr };
    return sEmpty;
}

void OpenGLManager::set_msaa_type(const std::string& type)
{
    EMSAAType msaa = EMSAAType::Disabled;
    if ("X2" == type) {
        msaa = EMSAAType::X2;
    }
    if ("X4" == type) {
        msaa = EMSAAType::X4;
    }
    else if ("X8" == type) {
        msaa = EMSAAType::X8;
    }
    else if ("X16" == type) {
        msaa = EMSAAType::X16;
    }

    set_msaa_type(msaa);
}

void OpenGLManager::set_msaa_type(EMSAAType type)
{
    m_msaa_type = type;
}

EMSAAType OpenGLManager::get_msaa_type() const
{
    return m_msaa_type;
}

bool OpenGLManager::read_pixel(const std::string& frame_name, uint32_t x, uint32_t y, uint32_t width, uint32_t height, EPixelFormat format, EPixelDataType type, void* pixels) const
{
    std::shared_ptr<FrameBuffer> fb{ nullptr };
    if (frame_name.empty()) {
        fb = m_current_binded_framebuffer.lock();
    }
    else if (frame_name != s_back_frame) {
        const auto& iter = m_name_to_framebuffer.find(frame_name);
        if (iter == m_name_to_framebuffer.end()) {
            return false;
        }
        fb = iter->second;
    }

    GLenum gl_format = get_pixel_format(format);
    GLenum gl_type = get_pixel_data_type(type);

    if (fb) {
        fb->read_pixel(x, y, width, height, format, type, pixels);
    }
    else {
        glsafe(::glReadPixels(x, y, width, height, gl_format, gl_type, pixels));
    }

    const auto current_fb = m_current_binded_framebuffer.lock();
    if (current_fb) {
        current_fb->bind();
    }

    return true;
}

void OpenGLManager::bind_vao()
{
    if (m_vao_type != EVAOType::Unknown) {
        if (EVAOType::Core == m_vao_type || EVAOType::Arb == m_vao_type) {
            if (0 == m_vao) {
                glsafe(::glGenVertexArrays(1, &m_vao));
            }
            glsafe(::glBindVertexArray(m_vao));
        }
        else {
#if defined(__APPLE__)
            if (0 == m_vao) {
                glsafe(::glGenVertexArraysAPPLE(1, &m_vao));
            }
            glsafe(::glBindVertexArrayAPPLE(m_vao));
#endif
        }
    }
}

void OpenGLManager::unbind_vao()
{
    if (0 == m_vao) {
        return;
    }

    if (m_vao_type != EVAOType::Unknown) {
        if (EVAOType::Core == m_vao_type || EVAOType::Arb == m_vao_type) {
            glsafe(::glBindVertexArray(0));
        }
        else {
#if defined(__APPLE__)
            glsafe(::glBindVertexArrayAPPLE(0));
#endif
        }
    }
}

void OpenGLManager::release_vao()
{
    if (0 == m_vao) {
        return;
    }
    if (m_vao_type != EVAOType::Unknown) {
        if (EVAOType::Core == m_vao_type || EVAOType::Arb == m_vao_type) {
            glsafe(::glBindVertexArray(0));
            glsafe(::glDeleteVertexArrays(1, &m_vao));
        }
        else {
#if defined(__APPLE__)
            glsafe(::glBindVertexArrayAPPLE(0));
            glsafe(::glDeleteVertexArraysAPPLE(1, &m_vao));
#endif
        }
        m_vao = 0;
    }
}

void OpenGLManager::set_off_screen_msaa_type(EMSAAType type)
{
    m_off_screen_msaa_type = type;
}

EMSAAType OpenGLManager::get_off_screen_msaa_type()
{
    return m_off_screen_msaa_type;
}

void OpenGLManager::set_fxaa_enabled(bool is_enabled)
{
    m_fxaa_enabled = is_enabled;
}

bool OpenGLManager::is_fxaa_enabled() const
{
    return m_fxaa_enabled;
}

void OpenGLManager::blit_framebuffer(const std::string& source, const std::string& target)
{
    if (source == target) {
        return;
    }
    if (s_back_frame == source) {
        glsafe(BBS_GL_EXTENSION_FUNC(::glBindFramebuffer)(BBS_GL_EXTENSION_PARAMETER(GL_READ_FRAMEBUFFER), 0));
    }
    else
    {
        const auto& iter = m_name_to_framebuffer.find(source);
        if (iter == m_name_to_framebuffer.end()) {
            return;
        }
        const uint32_t source_id = iter->second->get_gl_id();
        glsafe(BBS_GL_EXTENSION_FUNC(::glBindFramebuffer)(BBS_GL_EXTENSION_PARAMETER(GL_READ_FRAMEBUFFER), source_id));
    }

    if (s_back_frame == target) {
        glsafe(BBS_GL_EXTENSION_FUNC(::glBindFramebuffer)(BBS_GL_EXTENSION_PARAMETER(GL_DRAW_FRAMEBUFFER), 0));
    }
    else
    {
        const auto& iter = m_name_to_framebuffer.find(target);
        if (iter == m_name_to_framebuffer.end()) {
            return;
        }
        const uint32_t target_id = iter->second->get_gl_id();
        glsafe(BBS_GL_EXTENSION_FUNC(::glBindFramebuffer)(BBS_GL_EXTENSION_PARAMETER(GL_DRAW_FRAMEBUFFER), target_id));
    }

    glsafe(::glBlitFramebuffer(0, 0, m_viewport_width, m_viewport_height, 0, 0, m_viewport_width, m_viewport_height, GL_COLOR_BUFFER_BIT, GL_LINEAR));
}

void OpenGLManager::set_line_width(float width) const
{
    const auto formated_gl_version = s_gl_info.get_formated_gl_version();
    if (formated_gl_version < 30) {
        glsafe(::glLineWidth(width));
    }
    else {
#ifdef __APPLE__
        glsafe(::glLineWidth(1.0f));
#else
        glsafe(::glLineWidth(width));
#endif
    }
}

void OpenGLManager::set_legacy_framebuffer_enabled(bool is_enabled)
{
    m_b_legacy_framebuffer_enabled = is_enabled;
}

bool OpenGLManager::is_legacy_framebuffer_enabled() const
{
    return m_b_legacy_framebuffer_enabled;
}

void OpenGLManager::set_gizmo_keep_screen_size_enabled(bool is_enabled)
{
    m_b_gizmo_keep_screen_size_enabled = is_enabled;
}

bool OpenGLManager::is_gizmo_keep_screen_size_enabled() const
{
    return m_b_gizmo_keep_screen_size_enabled;
}

std::string OpenGLManager::framebuffer_type_to_string(EFramebufferType type)
{
    switch (type)
    {
    case EFramebufferType::Supported:
        return "Supported";
    case EFramebufferType::Arb:
        return "ARB";
    case EFramebufferType::Ext:
        return "EXT";
    case EFramebufferType::Unknown:
    default:
        return "unknow";
    }
}

wxGLCanvas* OpenGLManager::create_wxglcanvas(wxWindow& parent, EMSAAType msaa_type)
{
    const uint8_t msaa_samples = get_msaa_samples(msaa_type);
    int attribList[] = {
        WX_GL_RGBA,
        WX_GL_DOUBLEBUFFER,
        // RGB channels each should be allocated with 8 bit depth. One should almost certainly get these bit depths by default.
        WX_GL_MIN_RED, 			8,
        WX_GL_MIN_GREEN, 		8,
        WX_GL_MIN_BLUE, 		8,
        // Requesting an 8 bit alpha channel. Interestingly, the NVIDIA drivers would most likely work with some alpha plane, but glReadPixels would not return
        // the alpha channel on NVIDIA if not requested when the GL context is created.
        WX_GL_MIN_ALPHA, 		8,
        WX_GL_DEPTH_SIZE, 		24,
        //BBS: turn on stencil buffer for outline
        WX_GL_STENCIL_SIZE,     8,
        WX_GL_SAMPLE_BUFFERS, 	msaa_samples > 0 ? GL_TRUE : GL_FALSE,
        WX_GL_SAMPLES, 			msaa_samples,
#ifndef __APPLE__
        WX_GL_CORE_PROFILE,
#endif
        0
    };

    if (s_multisample == EMultisampleState::Unknown) {
        detect_multisample(attribList);
//        // debug output
//        std::cout << "Multisample " << (can_multisample() ? "enabled" : "disabled") << std::endl;
    }

    if (! can_multisample())
        attribList[12] = 0;

    return new wxGLCanvas(&parent, wxID_ANY, attribList, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS);
}

void OpenGLManager::set_cut_plane_color(ColorRGBA color) {
    s_cut_plane_color = color;
}

const ColorRGBA &OpenGLManager::get_cut_plane_color(){
    return s_cut_plane_color;
}

unsigned int OpenGLManager::get_draw_primitive_type(EDrawPrimitiveType type)
{
    switch (type) {
    case EDrawPrimitiveType::Points: {
        return GL_POINTS;
    }
    default:
    case EDrawPrimitiveType::Triangles: {
        return GL_TRIANGLES;
    }
    case EDrawPrimitiveType::TriangleStrip: {
        return GL_TRIANGLE_STRIP;
    }
    case EDrawPrimitiveType::TriangleFan: {
        return GL_TRIANGLE_FAN;
    }
    case EDrawPrimitiveType::Lines: {
        return GL_LINES;
    }
    case EDrawPrimitiveType::LineStrip: {
        return GL_LINE_STRIP;
    }
    case EDrawPrimitiveType::LineLoop: {
        return GL_LINE_LOOP;
    }
    }
}

void OpenGLManager::detect_multisample(int* attribList)
{
    int wxVersion = wxMAJOR_VERSION * 10000 + wxMINOR_VERSION * 100 + wxRELEASE_NUMBER;
    bool enable_multisample = wxVersion >= 30003;
    s_multisample =
        enable_multisample &&
        // Disable multi-sampling on ChromeOS, as the OpenGL virtualization swaps Red/Blue channels with multi-sampling enabled,
        // at least on some platforms.
        platform_flavor() != PlatformFlavor::LinuxOnChromium &&
        wxGLCanvas::IsDisplaySupported(attribList)
        ? EMultisampleState::Enabled : EMultisampleState::Disabled;
    // Alternative method: it was working on previous version of wxWidgets but not with the latest, at least on Windows
    // s_multisample = enable_multisample && wxGLCanvas::IsExtensionSupported("WGL_ARB_multisample");
}

FrameBuffer::FrameBuffer(const FrameBufferParams& params)
    : m_width(params.m_width)
    , m_height(params.m_height)
    , m_msaa(params.m_msaa)
{
}

FrameBuffer::~FrameBuffer()
{
    if (UINT32_MAX != m_gl_id)
    {
        glsafe(BBS_GL_EXTENSION_FUNC(::glDeleteFramebuffers)(1, &m_gl_id));
        m_gl_id = UINT32_MAX;
    }

    if (UINT32_MAX != m_color_texture_id)
    {
        glDeleteTextures(1, &m_color_texture_id);
        m_color_texture_id = UINT32_MAX;
    }

    if (UINT32_MAX != m_depth_rbo_id)
    {
        glsafe(BBS_GL_EXTENSION_FUNC(::glDeleteRenderbuffers)(1, &m_depth_rbo_id));
        m_depth_rbo_id = UINT32_MAX;
    }

    if (UINT32_MAX != m_gl_id_for_back_fbo)
    {
        glsafe(BBS_GL_EXTENSION_FUNC(::glDeleteFramebuffers)(1, &m_gl_id_for_back_fbo));
        m_gl_id_for_back_fbo = UINT32_MAX;

        glsafe(BBS_GL_EXTENSION_FUNC(::glDeleteRenderbuffers)(2, m_msaa_back_buffer_rbos));
        m_msaa_back_buffer_rbos[0] = UINT32_MAX;
        m_msaa_back_buffer_rbos[1] = UINT32_MAX;
    }
}

void FrameBuffer::bind()
{
    const OpenGLManager::EFramebufferType framebuffer_type = OpenGLManager::get_framebuffers_type();
    if (OpenGLManager::EFramebufferType::Unknown == framebuffer_type) {
        return;
    }
    if (0 == m_width || 0 == m_height)
    {
        return;
    }
    if (UINT32_MAX == m_gl_id)
    {
        if (0 == m_msaa)
        {
            create_no_msaa_fbo(true);
        }
        else
        {
            create_msaa_fbo();
        }
        if (OpenGLManager::EFramebufferType::Ext == framebuffer_type) {
            GLenum bufs[1]{ GL_COLOR_ATTACHMENT0_EXT };
            glsafe(::glDrawBuffers((GLsizei)1, bufs));
        }
        else {
            GLenum bufs[1]{ GL_COLOR_ATTACHMENT0 };
            glsafe(::glDrawBuffers((GLsizei)1, bufs));
        }
        const bool rt = check_frame_buffer_status();
        if (!rt)
        {
            return;
        }
        BOOST_LOG_TRIVIAL(trace) << "Successfully created framebuffer: width = " << m_width << ", heihgt = " << m_height;
    }

    mark_needs_to_resolve();
    glsafe(BBS_GL_EXTENSION_FUNC(::glBindFramebuffer)(BBS_GL_EXTENSION_PARAMETER(GL_FRAMEBUFFER), (UINT32_MAX == m_gl_id_for_back_fbo ? m_gl_id : m_gl_id_for_back_fbo)));
}

void FrameBuffer::unbind()
{
    const OpenGLManager::EFramebufferType framebuffer_type = OpenGLManager::get_framebuffers_type();
    if (OpenGLManager::EFramebufferType::Unknown == framebuffer_type) {
        return;
    }

    resolve();

    glsafe(BBS_GL_EXTENSION_FUNC(::glBindFramebuffer)(BBS_GL_EXTENSION_PARAMETER(GL_FRAMEBUFFER), 0));
}

uint32_t FrameBuffer::get_color_texture() const noexcept
{
    return m_color_texture_id;
}

bool FrameBuffer::is_texture_valid(uint32_t texture_id) const noexcept
{
    return m_color_texture_id != UINT32_MAX;
}

uint32_t FrameBuffer::get_gl_id()
{
    resolve();
    return m_gl_id;
}

void FrameBuffer::read_pixel(uint32_t x, uint32_t y, uint32_t width, uint32_t height, EPixelFormat format, EPixelDataType type, void* pixels)
{
    const GLenum gl_format = get_pixel_format(format);
    const GLenum gl_type = get_pixel_data_type(type);

    if (UINT32_MAX != m_gl_id_for_back_fbo) {
        EBlitOptionType old_blit_type = m_blit_option_type;
        if (EPixelFormat::DepthComponent == format) {
            m_blit_option_type = EBlitOptionType::Depth;
        }
        unbind();
        m_blit_option_type = old_blit_type;
    }
    glsafe(BBS_GL_EXTENSION_FUNC(::glBindFramebuffer)(BBS_GL_EXTENSION_PARAMETER(GL_FRAMEBUFFER), m_gl_id));
    glsafe(::glReadPixels(x, y, width, height, gl_format, gl_type, pixels));
    glsafe(BBS_GL_EXTENSION_FUNC(::glBindFramebuffer)(BBS_GL_EXTENSION_PARAMETER(GL_FRAMEBUFFER), 0));
}

uint32_t FrameBuffer::get_height() const
{
    return m_height;
}

uint32_t FrameBuffer::get_width() const
{
    return m_width;
}

uint8_t FrameBuffer::get_msaa_type() const
{
    return m_msaa;
}

bool FrameBuffer::is_format_equal(const FrameBufferParams& params) const
{
    const bool rt = m_width == params.m_width
        && m_height == params.m_height
        && m_msaa == params.m_msaa;
    return rt;
}

void FrameBuffer::create_no_msaa_fbo(bool with_depth)
{
    const OpenGLManager::EFramebufferType framebuffer_type = OpenGLManager::get_framebuffers_type();

    glsafe(BBS_GL_EXTENSION_FUNC(::glGenFramebuffers)(1, &m_gl_id));

    glsafe(BBS_GL_EXTENSION_FUNC(::glBindFramebuffer)(BBS_GL_EXTENSION_PARAMETER(GL_FRAMEBUFFER), m_gl_id));

    glsafe(::glGenTextures(1, &m_color_texture_id));
    glsafe(::glBindTexture(GL_TEXTURE_2D, m_color_texture_id));

    glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));

    glsafe(BBS_GL_EXTENSION_FUNC(::glFramebufferTexture2D)(BBS_GL_EXTENSION_PARAMETER(GL_FRAMEBUFFER), BBS_GL_EXTENSION_PARAMETER(GL_COLOR_ATTACHMENT0), GL_TEXTURE_2D, m_color_texture_id, 0));

    if (with_depth)
    {
        glsafe(BBS_GL_EXTENSION_FUNC(::glGenRenderbuffers)(1, &m_depth_rbo_id));
        glsafe(BBS_GL_EXTENSION_FUNC(::glBindRenderbuffer)(BBS_GL_EXTENSION_PARAMETER(GL_RENDERBUFFER), m_depth_rbo_id));

        glsafe(BBS_GL_EXTENSION_FUNC(::glRenderbufferStorage)(BBS_GL_EXTENSION_PARAMETER(GL_RENDERBUFFER), GL_DEPTH24_STENCIL8, m_width, m_height));

        const auto& gl_info = OpenGLManager::get_gl_info();
        uint32_t formated_gl_version = gl_info.get_formated_gl_version();
        if (formated_gl_version < 30)
        {
            glsafe(::glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, m_depth_rbo_id));
            glsafe(::glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_STENCIL_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, m_depth_rbo_id));
        }
        else
        {
            glsafe(::glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_depth_rbo_id));
        }
    }
}

void FrameBuffer::create_msaa_fbo()
{
    glsafe(BBS_GL_EXTENSION_FUNC(::glGenFramebuffers)(1, &m_gl_id_for_back_fbo));

    glsafe(BBS_GL_EXTENSION_FUNC(::glBindFramebuffer)(BBS_GL_EXTENSION_PARAMETER(GL_FRAMEBUFFER), m_gl_id_for_back_fbo));

    // use renderbuffer instead of texture to avoid the need to use glTexImage2DMultisample which is available only since OpenGL 3.2
    glsafe(BBS_GL_EXTENSION_FUNC(::glGenRenderbuffers)(2, m_msaa_back_buffer_rbos));

    glsafe(BBS_GL_EXTENSION_FUNC(::glBindRenderbuffer)(BBS_GL_EXTENSION_PARAMETER(GL_RENDERBUFFER), m_msaa_back_buffer_rbos[0]));
    glsafe(BBS_GL_EXTENSION_FUNC(::glRenderbufferStorageMultisample)(BBS_GL_EXTENSION_PARAMETER(GL_RENDERBUFFER), m_msaa, GL_RGBA8, m_width, m_height));

    glsafe(BBS_GL_EXTENSION_FUNC(::glBindRenderbuffer)(BBS_GL_EXTENSION_PARAMETER(GL_RENDERBUFFER), m_msaa_back_buffer_rbos[1]));
    glsafe(::glRenderbufferStorageMultisample(BBS_GL_EXTENSION_PARAMETER(GL_RENDERBUFFER), m_msaa, GL_DEPTH24_STENCIL8, m_width, m_height));

    glsafe(BBS_GL_EXTENSION_FUNC(::glFramebufferRenderbuffer)(BBS_GL_EXTENSION_PARAMETER(GL_FRAMEBUFFER), BBS_GL_EXTENSION_PARAMETER(GL_COLOR_ATTACHMENT0), BBS_GL_EXTENSION_PARAMETER(GL_RENDERBUFFER), m_msaa_back_buffer_rbos[0]));

    const auto& gl_info = OpenGLManager::get_gl_info();
    if (gl_info.is_version_greater_or_equal_to(3, 0)) {
        glsafe(::glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_msaa_back_buffer_rbos[1]));
    }
    else {
        glsafe(BBS_GL_EXTENSION_FUNC(::glFramebufferRenderbuffer)(BBS_GL_EXTENSION_PARAMETER(GL_FRAMEBUFFER), BBS_GL_EXTENSION_PARAMETER(GL_DEPTH_ATTACHMENT), BBS_GL_EXTENSION_PARAMETER(GL_RENDERBUFFER), m_msaa_back_buffer_rbos[1]));
        glsafe(BBS_GL_EXTENSION_FUNC(::glFramebufferRenderbuffer)(BBS_GL_EXTENSION_PARAMETER(GL_FRAMEBUFFER), BBS_GL_EXTENSION_PARAMETER(GL_STENCIL_ATTACHMENT), BBS_GL_EXTENSION_PARAMETER(GL_RENDERBUFFER), m_msaa_back_buffer_rbos[1]));
    }
}

bool FrameBuffer::check_frame_buffer_status() const
{
    const OpenGLManager::EFramebufferType framebuffer_type = OpenGLManager::get_framebuffers_type();

    if (OpenGLManager::EFramebufferType::Ext == framebuffer_type) {
        if (::glCheckFramebufferStatusEXT(BBS_GL_EXTENSION_PARAMETER(GL_FRAMEBUFFER)) != GL_FRAMEBUFFER_COMPLETE_EXT)
        {
            BOOST_LOG_TRIVIAL(error) << "Framebuffer is not complete!";
            glsafe(BBS_GL_EXTENSION_FUNC(::glBindFramebuffer)(BBS_GL_EXTENSION_PARAMETER(GL_FRAMEBUFFER), 0));
            return false;
        }
    }
    else {
        if (::glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            BOOST_LOG_TRIVIAL(error) << "Framebuffer is not complete!";
            glsafe(BBS_GL_EXTENSION_FUNC(::glBindFramebuffer)(BBS_GL_EXTENSION_PARAMETER(GL_FRAMEBUFFER), 0));
            return false;
        }
    }

    return true;
}

void FrameBuffer::resolve()
{

    if (!m_needs_to_solve) {
        return;
    }

    if (UINT32_MAX == m_gl_id_for_back_fbo) {
        return;
    }

    if (UINT32_MAX == m_gl_id) {
        create_no_msaa_fbo(true);

        const OpenGLManager::EFramebufferType framebuffer_type = OpenGLManager::get_framebuffers_type();
        if (OpenGLManager::EFramebufferType::Ext == framebuffer_type)
        {
            GLenum bufs[1]{ GL_COLOR_ATTACHMENT0_EXT };
            glsafe(::glDrawBuffers((GLsizei)1, bufs));
        }
        else
        {
            GLenum bufs[1]{ GL_COLOR_ATTACHMENT0 };
            glsafe(::glDrawBuffers((GLsizei)1, bufs));
        }

        const bool rt = check_frame_buffer_status();
        if (!rt)
        {
            glsafe(BBS_GL_EXTENSION_FUNC(::glBindFramebuffer)(BBS_GL_EXTENSION_PARAMETER(GL_FRAMEBUFFER), 0));
        }
    }

    glsafe(::glDisable(GL_SCISSOR_TEST));

    glsafe(BBS_GL_EXTENSION_FUNC(::glBindFramebuffer)(BBS_GL_EXTENSION_PARAMETER(GL_READ_FRAMEBUFFER), m_gl_id_for_back_fbo));
    glsafe(BBS_GL_EXTENSION_FUNC(::glBindFramebuffer)(BBS_GL_EXTENSION_PARAMETER(GL_DRAW_FRAMEBUFFER), m_gl_id));

    if (EBlitOptionType::Color & m_blit_option_type) {
        glsafe(::glBlitFramebuffer(0, 0, m_width, m_height, 0, 0, m_width, m_height, GL_COLOR_BUFFER_BIT, GL_NEAREST));
    }

    if (EBlitOptionType::Depth & m_blit_option_type) {
        glsafe(::glBlitFramebuffer(0, 0, m_width, m_height, 0, 0, m_width, m_height, GL_DEPTH_BUFFER_BIT, GL_NEAREST));
    }

    m_needs_to_solve = false;
}

void FrameBuffer::mark_needs_to_resolve()
{
    m_needs_to_solve = (m_gl_id_for_back_fbo != UINT32_MAX);
}

OpenGLManager::FrameBufferModifier::FrameBufferModifier(OpenGLManager& ogl_manager, const std::string& frame_buffer_name, EMSAAType msaa_type)
    : m_ogl_manager(ogl_manager)
    , m_frame_buffer_name(frame_buffer_name)
    , m_msaa_type(msaa_type)
{
}

OpenGLManager::FrameBufferModifier::~FrameBufferModifier()
{
    m_ogl_manager._bind_frame_buffer(m_frame_buffer_name, m_msaa_type, m_width, m_height);
}

OpenGLManager::FrameBufferModifier& OpenGLManager::FrameBufferModifier::set_width(uint32_t t_width)
{
    m_width = t_width;
    return *this;
}

OpenGLManager::FrameBufferModifier& OpenGLManager::FrameBufferModifier::set_height(uint32_t t_height)
{
    m_height = t_height;
    return *this;
}

} // namespace GUI
} // namespace Slic3r
