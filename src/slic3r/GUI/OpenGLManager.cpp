#include "libslic3r/libslic3r.h"
#include "OpenGLManager.hpp"

#include "GUI.hpp"
#include "I18N.hpp"
#include "3DScene.hpp"

#include "libslic3r/Platform.hpp"

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

#ifdef __WIN32__

#ifdef SLIC3R_GUI
class OpenGLVersionCheck
{
public:
    std::string version;
    std::string glsl_version;
    std::string vendor;
    std::string renderer;

    HINSTANCE   hOpenGL = nullptr;
    bool success = false;

    bool load_opengl_dll()
    {
        MSG      msg     = {0};
        WNDCLASS wc      = {0};
        wc.lpfnWndProc   = OpenGLVersionCheck::supports_opengl2_wndproc;
        wc.hInstance     = (HINSTANCE)GetModuleHandle(nullptr);
        wc.hbrBackground = (HBRUSH)(COLOR_BACKGROUND);
        wc.lpszClassName = L"BambuStudio_opengl_version_check";
        wc.style = CS_OWNDC;
        if (RegisterClass(&wc)) {
            HWND hwnd = CreateWindowW(wc.lpszClassName, L"BambuStudio_opengl_version_check", WS_OVERLAPPEDWINDOW, 0, 0, 640, 480, 0, 0, wc.hInstance, (LPVOID)this);
            if (hwnd) {
                message_pump_exit = false;
                while (GetMessage(&msg, NULL, 0, 0 ) > 0 && ! message_pump_exit)
                    DispatchMessage(&msg);
            }
        }
        return this->success;
    }

    void unload_opengl_dll()
    {
        if (this->hOpenGL) {
            BOOL released = FreeLibrary(this->hOpenGL);
            if (released)
                printf("System OpenGL library released\n");
            else
                printf("System OpenGL library NOT released\n");
            this->hOpenGL = nullptr;
        }
    }

    bool is_version_greater_or_equal_to(unsigned int major, unsigned int minor) const
    {
        // printf("is_version_greater_or_equal_to, version: %s\n", version.c_str());
        std::vector<std::string> tokens;
        boost::split(tokens, version, boost::is_any_of(" "), boost::token_compress_on);
        if (tokens.empty())
            return false;

        std::vector<std::string> numbers;
        boost::split(numbers, tokens[0], boost::is_any_of("."), boost::token_compress_on);

        unsigned int gl_major = 0;
        unsigned int gl_minor = 0;
        if (numbers.size() > 0)
            gl_major = ::atoi(numbers[0].c_str());
        if (numbers.size() > 1)
            gl_minor = ::atoi(numbers[1].c_str());
        // printf("Major: %d, minor: %d\n", gl_major, gl_minor);
        if (gl_major < major)
            return false;
        else if (gl_major > major)
            return true;
        else
            return gl_minor >= minor;
    }

protected:
    static bool message_pump_exit;

    void check(HWND hWnd)
    {
        hOpenGL = LoadLibraryExW(L"opengl32.dll", nullptr, 0);
        if (hOpenGL == nullptr) {
            printf("Failed loading the system opengl32.dll\n");
            return;
        }

        typedef HGLRC 		(WINAPI *Func_wglCreateContext)(HDC);
        typedef BOOL 		(WINAPI *Func_wglMakeCurrent  )(HDC, HGLRC);
        typedef BOOL     	(WINAPI *Func_wglDeleteContext)(HGLRC);
        typedef GLubyte* 	(WINAPI *Func_glGetString     )(GLenum);

        Func_wglCreateContext 	wglCreateContext = (Func_wglCreateContext)GetProcAddress(hOpenGL, "wglCreateContext");
        Func_wglMakeCurrent 	wglMakeCurrent 	 = (Func_wglMakeCurrent)  GetProcAddress(hOpenGL, "wglMakeCurrent");
        Func_wglDeleteContext 	wglDeleteContext = (Func_wglDeleteContext)GetProcAddress(hOpenGL, "wglDeleteContext");
        Func_glGetString 		glGetString 	 = (Func_glGetString)	  GetProcAddress(hOpenGL, "glGetString");

        if (wglCreateContext == nullptr || wglMakeCurrent == nullptr || wglDeleteContext == nullptr || glGetString == nullptr) {
            printf("Failed loading the system opengl32.dll: The library is invalid.\n");
            return;
        }

        PIXELFORMATDESCRIPTOR pfd =
        {
            sizeof(PIXELFORMATDESCRIPTOR),
            1,
            PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
            PFD_TYPE_RGBA,            	// The kind of framebuffer. RGBA or palette.
            32,                        	// Color depth of the framebuffer.
            0, 0, 0, 0, 0, 0,
            0,
            0,
            0,
            0, 0, 0, 0,
            24,                        	// Number of bits for the depthbuffer
            8,                        	// Number of bits for the stencilbuffer
            0,                        	// Number of Aux buffers in the framebuffer.
            PFD_MAIN_PLANE,
            0,
            0, 0, 0
        };

        HDC ourWindowHandleToDeviceContext = ::GetDC(hWnd);
        // Gdi32.dll
        int letWindowsChooseThisPixelFormat = ::ChoosePixelFormat(ourWindowHandleToDeviceContext, &pfd);
        // Gdi32.dll
        SetPixelFormat(ourWindowHandleToDeviceContext, letWindowsChooseThisPixelFormat, &pfd);
        // Opengl32.dll
        HGLRC glcontext = wglCreateContext(ourWindowHandleToDeviceContext);
        wglMakeCurrent(ourWindowHandleToDeviceContext, glcontext);
        // Opengl32.dll
        const char *data = (const char*)glGetString(GL_VERSION);
        if (data != nullptr)
            this->version = data;
        // printf("check -version: %s\n", version.c_str());
        data = (const char*)glGetString(0x8B8C); // GL_SHADING_LANGUAGE_VERSION
        if (data != nullptr)
            this->glsl_version = data;
        data = (const char*)glGetString(GL_VENDOR);
        if (data != nullptr)
            this->vendor = data;
        data = (const char*)glGetString(GL_RENDERER);
        if (data != nullptr)
            this->renderer = data;
        // Opengl32.dll
        wglDeleteContext(glcontext);
        ::ReleaseDC(hWnd, ourWindowHandleToDeviceContext);
        this->success = true;
    }

    static LRESULT CALLBACK supports_opengl2_wndproc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch(message)
        {
        case WM_CREATE:
        {
            CREATESTRUCT *pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
            OpenGLVersionCheck *ogl_data = reinterpret_cast<OpenGLVersionCheck*>(pCreate->lpCreateParams);
            ogl_data->check(hWnd);
            DestroyWindow(hWnd);
            return 0;
        }
        case WM_NCDESTROY:
            message_pump_exit = true;
            return 0;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
    }
};

bool OpenGLVersionCheck::message_pump_exit = false;
#endif /* SLIC3R_GUI */

#endif // __WIN32__

#define BBS_GL_EXTENSION_FUNC(_func) (OpenGLManager::get_framebuffers_type() == OpenGLManager::EFramebufferType::Ext ? _func ## EXT : _func)
#define BBS_GL_EXTENSION_FRAMEBUFFER OpenGLManager::get_framebuffers_type() == OpenGLManager::EFramebufferType::Ext ? GL_FRAMEBUFFER_EXT : GL_FRAMEBUFFER
#define BBS_GL_EXTENSION_COLOR_ATTACHMENT(color_attachment) (OpenGLManager::get_framebuffers_type() == OpenGLManager::EFramebufferType::Ext ? color_attachment ## _EXT : color_attachment)
#define BBS_GL_EXTENSION_DEPTH_ATTACHMENT OpenGLManager::get_framebuffers_type() == OpenGLManager::EFramebufferType::Ext ? GL_DEPTH_ATTACHMENT_EXT : GL_DEPTH_ATTACHMENT
#define BBS_GL_EXTENSION_RENDER_BUFFER OpenGLManager::get_framebuffers_type() == OpenGLManager::EFramebufferType::Ext ? GL_RENDERBUFFER_EXT : GL_RENDERBUFFER


namespace Slic3r {
namespace GUI {

// A safe wrapper around glGetString to report a "N/A" string in case glGetString returns nullptr.
std::string gl_get_string_safe(GLenum param, const std::string& default_value)
{
    const char* value = (const char*)::glGetString(param);
    return std::string((value != nullptr) ? value : default_value);
}

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
        GLint major = 0;
        GLint minor = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);

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
    *const_cast<bool*>(&m_detected) = true;
}

static bool version_greater_or_equal_to(const std::string& version, unsigned int major, unsigned int minor)
{
    if (version == "N/A")
        return false;

    std::vector<std::string> tokens;
    boost::split(tokens, version, boost::is_any_of(" "), boost::token_compress_on);

    if (tokens.empty())
        return false;

    std::vector<std::string> numbers;
    boost::split(numbers, tokens[0], boost::is_any_of("."), boost::token_compress_on);

    unsigned int gl_major = 0;
    unsigned int gl_minor = 0;

    if (numbers.size() > 0)
        gl_major = ::atoi(numbers[0].c_str());

    if (numbers.size() > 1)
        gl_minor = ::atoi(numbers[1].c_str());

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
    m_shaders_manager.shutdown();
    m_name_to_frame_buffer.clear();
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

bool OpenGLManager::init(bool prefer_to_use_dgpu)
{
    if (s_b_initialized) {
        return true;
    }
#ifdef __WIN32__
    if (prefer_to_use_dgpu) {
        HMODULE hModExe = nullptr;
        hModExe         = GetModuleHandle(NULL);
        if (hModExe) {
            // BOOST_LOG_TRIVIAL(info) << "wxMediaCtrl2: GetModuleHandle " << hModExe;
            auto NvOptimusEnablement                  = (DWORD *) GetProcAddress(hModExe, "NvOptimusEnablement");
            auto AmdPowerXpressRequestHighPerformance = (int *) GetProcAddress(hModExe, "AmdPowerXpressRequestHighPerformance");
            if (NvOptimusEnablement) {
                *NvOptimusEnablement = 0x00000001;
            }
            if (AmdPowerXpressRequestHighPerformance) {
                // BOOST_LOG_TRIVIAL(info) << "wxMediaCtrl2: AmdPowerXpressRequestHighPerformance " << *AmdPowerXpressRequestHighPerformance;
                *AmdPowerXpressRequestHighPerformance = 1;
            }
        }
    }

#ifdef SLIC3R_GUI
    bool force_mesa = false;

    wchar_t path_to_exe[MAX_PATH + 1] = {0};
    ::GetModuleFileNameW(nullptr, path_to_exe, MAX_PATH);
    OpenGLVersionCheck opengl_version_check;
    bool load_mesa =
        force_mesa ||
        // Try to load the default OpenGL driver and test its context version.
        !opengl_version_check.load_opengl_dll() || !opengl_version_check.is_version_greater_or_equal_to(2, 0);
    // https://wiki.qt.io/Cross_compiling_Mesa_for_Windows
    // http://download.qt.io/development_releases/prebuilt/llvmpipe/windows/
    if (load_mesa) {
        opengl_version_check.unload_opengl_dll();
        wchar_t path_to_mesa[MAX_PATH + 1] = {0};
        wcscpy(path_to_mesa, path_to_exe);
        wcscat(path_to_mesa, L"mesa\\opengl32.dll");
        BOOST_LOG_TRIVIAL(info) << "Loading MESA OpenGL library: " << path_to_mesa;
        HINSTANCE hInstance_OpenGL = LoadLibraryExW(path_to_mesa, nullptr, 0);
        if (hInstance_OpenGL == nullptr) {
            BOOST_LOG_TRIVIAL(error) << "MESA OpenGL library was not loaded";
        } else
            BOOST_LOG_TRIVIAL(info) << "MESA OpenGL library was loaded sucessfully";
    }
#endif /* SLIC3R_GUI */

#endif // __WIN32__

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
}

void OpenGLManager::get_viewport_size(uint32_t& width, uint32_t& height) const
{
    width = m_viewport_width;
    height = m_viewport_height;
}

void OpenGLManager::_bind_frame_buffer(const std::string& name)
{
    const auto& iter = m_name_to_frame_buffer.find(name);
    if (iter == m_name_to_frame_buffer.end() || m_b_viewport_dirty) {
        const auto& p_frame_buffer = std::make_shared<FrameBuffer>(m_viewport_width, m_viewport_height);
        m_name_to_frame_buffer.insert_or_assign(name, p_frame_buffer);
    }

    m_name_to_frame_buffer[name]->bind();
}

void OpenGLManager::_unbind_frame_buffer(const std::string& name)
{
    const auto& iter = m_name_to_frame_buffer.find(name);
    if (iter == m_name_to_frame_buffer.end()) {
        return;
    }

    m_name_to_frame_buffer[name]->unbind();
}

const std::shared_ptr<FrameBuffer>& OpenGLManager::get_frame_buffer(const std::string& name) const
{
    const auto& iter = m_name_to_frame_buffer.find(name);
    if (iter != m_name_to_frame_buffer.end()) {
        return iter->second;
    }
    static std::shared_ptr<FrameBuffer> sEmpty{ nullptr };
    return sEmpty;
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
    if (0 != m_vao) {
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

wxGLCanvas* OpenGLManager::create_wxglcanvas(wxWindow& parent)
{
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
        WX_GL_SAMPLE_BUFFERS, 	GL_TRUE,
        WX_GL_SAMPLES, 			4,
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

FrameBuffer::FrameBuffer(uint32_t width, uint32_t height)
    : m_width(width)
    , m_height(height)
{
}

FrameBuffer::~FrameBuffer()
{
    if (UINT32_MAX != m_gl_id)
    {
        //glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, 0));
        glsafe(::glDeleteFramebuffers(1, &m_gl_id));
        m_gl_id = UINT32_MAX;
    }

    if (UINT32_MAX != m_color_texture_id)
    {
        glDeleteTextures(1, &m_color_texture_id);
        m_color_texture_id = UINT32_MAX;
    }

    if (UINT32_MAX != m_depth_rbo_id)
    {
        glDeleteRenderbuffers(1, &m_depth_rbo_id);
        m_depth_rbo_id = UINT32_MAX;
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
        glsafe(BBS_GL_EXTENSION_FUNC(::glGenFramebuffers)(1, &m_gl_id));

        glsafe(BBS_GL_EXTENSION_FUNC(::glBindFramebuffer)(BBS_GL_EXTENSION_FRAMEBUFFER, m_gl_id));

        glsafe(::glGenTextures(1, &m_color_texture_id));
        glsafe(::glBindTexture(GL_TEXTURE_2D, m_color_texture_id));

        glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr));
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));

        glsafe(BBS_GL_EXTENSION_FUNC(::glFramebufferTexture2D)(BBS_GL_EXTENSION_FRAMEBUFFER, BBS_GL_EXTENSION_COLOR_ATTACHMENT(GL_COLOR_ATTACHMENT0), GL_TEXTURE_2D, m_color_texture_id, 0));

        if (OpenGLManager::EFramebufferType::Ext == framebuffer_type) {
            GLenum bufs[1]{ GL_COLOR_ATTACHMENT0_EXT };
            glsafe(::glDrawBuffers((GLsizei)1, bufs));
        }
        else {
            GLenum bufs[1]{ GL_COLOR_ATTACHMENT0 };
            glsafe(::glDrawBuffers((GLsizei)1, bufs));
        }

        glsafe(BBS_GL_EXTENSION_FUNC(::glGenRenderbuffers)(1, &m_depth_rbo_id));
        glsafe(BBS_GL_EXTENSION_FUNC(::glBindRenderbuffer)(BBS_GL_EXTENSION_RENDER_BUFFER, m_depth_rbo_id));

        glsafe(BBS_GL_EXTENSION_FUNC(::glRenderbufferStorage)(BBS_GL_EXTENSION_RENDER_BUFFER, GL_DEPTH24_STENCIL8, m_width, m_height));

        glsafe(BBS_GL_EXTENSION_FUNC(::glFramebufferRenderbuffer)(BBS_GL_EXTENSION_FRAMEBUFFER, BBS_GL_EXTENSION_DEPTH_ATTACHMENT, BBS_GL_EXTENSION_RENDER_BUFFER, m_depth_rbo_id));

        if (OpenGLManager::EFramebufferType::Ext == framebuffer_type) {
            if (::glCheckFramebufferStatusEXT(BBS_GL_EXTENSION_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE_EXT)
            {
                BOOST_LOG_TRIVIAL(error) << "Framebuffer is not complete!";
                glsafe(BBS_GL_EXTENSION_FUNC(::glBindFramebuffer)(BBS_GL_EXTENSION_FRAMEBUFFER, 0));
                return;
            }
        }
        else {
            if (::glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            {
                BOOST_LOG_TRIVIAL(error) << "Framebuffer is not complete!";
                glsafe(BBS_GL_EXTENSION_FUNC(::glBindFramebuffer)(BBS_GL_EXTENSION_FRAMEBUFFER, 0));
                return;
            }
        }
        BOOST_LOG_TRIVIAL(trace) << "Successfully created framebuffer: width = " << m_width << ", heihgt = " << m_height;
    }

    glsafe(BBS_GL_EXTENSION_FUNC(::glBindFramebuffer)(BBS_GL_EXTENSION_FRAMEBUFFER, m_gl_id));
}

void FrameBuffer::unbind()
{
    const OpenGLManager::EFramebufferType framebuffer_type = OpenGLManager::get_framebuffers_type();
    if (OpenGLManager::EFramebufferType::Unknown == framebuffer_type) {
        return;
    }
    glsafe(BBS_GL_EXTENSION_FUNC(::glBindFramebuffer)(BBS_GL_EXTENSION_FRAMEBUFFER, 0));
}

uint32_t FrameBuffer::get_color_texture() const noexcept
{
    return m_color_texture_id;
}

bool FrameBuffer::is_texture_valid(uint32_t texture_id) const noexcept
{
    return m_color_texture_id != UINT32_MAX;
}

OpenGLManager::FrameBufferModifier::FrameBufferModifier(OpenGLManager& ogl_manager, const std::string& frame_buffer_name)
    : m_ogl_manager(ogl_manager)
    , m_frame_buffer_name(frame_buffer_name)
{
    m_ogl_manager._bind_frame_buffer(m_frame_buffer_name);
}

OpenGLManager::FrameBufferModifier::~FrameBufferModifier()
{
    m_ogl_manager._unbind_frame_buffer(m_frame_buffer_name);
}

} // namespace GUI
} // namespace Slic3r
