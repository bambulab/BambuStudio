#ifndef slic3r_OpenGLManager_hpp_
#define slic3r_OpenGLManager_hpp_

#include "GLShadersManager.hpp"
#include <memory>
#include <unordered_map>
#include <string>

class wxWindow;
class wxGLCanvas;
class wxGLContext;

namespace Slic3r {
namespace GUI {


struct FrameBuffer
{
    FrameBuffer(uint32_t width, uint32_t height);
    ~FrameBuffer();

    void bind();
    void unbind();

    uint32_t get_color_texture() const noexcept;

    bool is_texture_valid(uint32_t texture_id) const noexcept;

private:
    uint32_t m_width{ 0 };
    uint32_t m_height{ 0 };
    uint32_t m_gl_id{ UINT32_MAX };
    uint32_t m_color_texture_id{ UINT32_MAX };
    uint32_t m_depth_rbo_id{ UINT32_MAX };
};

class OpenGLManager
{
public:
    enum class EFramebufferType : unsigned char
    {
        Unknown,
        Supported, // supported, no extension required
        Arb,
        Ext
    };
    enum class EVAOType : uint8_t
    {
        Unknown,
        Core,
        Arb,
#ifdef __APPLE__
        Apple
#endif
    };

    class GLInfo
    {
        bool m_detected{ false };
        int m_max_tex_size{ 0 };
        float m_max_anisotropy{ 0.0f };

        std::string m_version;
        mutable uint32_t m_formated_gl_version{ 0 };
        std::string m_glsl_version;
        std::string m_vendor;
        std::string m_renderer;

    public:
        GLInfo() = default;

        const std::string& get_version() const;
        const uint32_t get_formated_gl_version() const;
        const std::string& get_glsl_version() const;
        const std::string& get_vendor() const;
        const std::string& get_renderer() const;

        int get_max_tex_size() const;
        float get_max_anisotropy() const;

        bool is_version_greater_or_equal_to(unsigned int major, unsigned int minor) const;
        bool is_glsl_version_greater_or_equal_to(unsigned int major, unsigned int minor) const;

        // If formatted for github, plaintext with OpenGL extensions enclosed into <details>.
        // Otherwise HTML formatted for the system info dialog.
        std::string to_string(bool for_github) const;

    private:
        void detect() const;
    };

    class FrameBufferModifier
    {
    public:
        explicit FrameBufferModifier(OpenGLManager& ogl_manager, const std::string& frame_buffer_name);
        ~FrameBufferModifier();

    private:
        // no copy
        FrameBufferModifier(const FrameBufferModifier&) = delete;
        FrameBufferModifier(FrameBufferModifier&&) = delete;

        // no assign
        FrameBufferModifier& operator=(const FrameBufferModifier&) = delete;
        FrameBufferModifier& operator=(FrameBufferModifier&&) = delete;

    private:
        OpenGLManager& m_ogl_manager;
        std::string m_frame_buffer_name{};
    };

#ifdef __APPLE__
    // Part of hack to remove crash when closing the application on OSX 10.9.5 when building against newer wxWidgets
    struct OSInfo
    {
        int major{ 0 };
        int minor{ 0 };
        int micro{ 0 };
    };
#endif //__APPLE__

private:
    enum class EMultisampleState : unsigned char
    {
        Unknown,
        Enabled,
        Disabled
    };

    bool m_gl_initialized{ false };
    wxGLContext* m_context{ nullptr };
    GLShadersManager m_shaders_manager;
    uint32_t m_viewport_width{ 0 };
    uint32_t m_viewport_height{ 0 };
    bool m_b_viewport_dirty{ true };
    std::unordered_map<std::string, std::shared_ptr<FrameBuffer>> m_name_to_frame_buffer;
    EVAOType m_vao_type{ EVAOType::Unknown };
    uint32_t m_vao{ 0 };
    static GLInfo s_gl_info;
#ifdef __APPLE__
    // Part of hack to remove crash when closing the application on OSX 10.9.5 when building against newer wxWidgets
    static OSInfo s_os_info;
#endif //__APPLE__
    static bool s_b_initialized;
    static bool s_compressed_textures_supported;
    static EMultisampleState s_multisample;
    static EFramebufferType s_framebuffers_type;

    static bool m_use_manually_generated_mipmaps;
public:
    OpenGLManager();
    ~OpenGLManager();

    bool init_gl(bool popup_error = true);
    wxGLContext* init_glcontext(wxGLCanvas& canvas);

    GLShaderProgram* get_shader(const std::string& shader_name) { return m_shaders_manager.get_shader(shader_name); }
    GLShaderProgram* get_current_shader() { return m_shaders_manager.get_current_shader(); }

    void clear_dirty();
    void set_viewport_size(uint32_t width, uint32_t height);
    void get_viewport_size(uint32_t& width, uint32_t& height) const;
    const std::shared_ptr<FrameBuffer>& get_frame_buffer(const std::string& name) const;

    void bind_vao();
    void unbind_vao();
    void release_vao();

    static bool init(bool prefer_to_use_dgpu = false);
    static bool are_compressed_textures_supported() { return s_compressed_textures_supported; }
    static bool can_multisample() { return s_multisample == EMultisampleState::Enabled; }
    static bool are_framebuffers_supported() { return (s_framebuffers_type != EFramebufferType::Unknown); }
    static EFramebufferType get_framebuffers_type() { return s_framebuffers_type; }
    static std::string framebuffer_type_to_string(EFramebufferType type);
    static wxGLCanvas* create_wxglcanvas(wxWindow& parent);
    static const GLInfo& get_gl_info() { return s_gl_info; }
    static bool use_manually_generated_mipmaps() { return m_use_manually_generated_mipmaps; }

private:
    static void detect_multisample(int* attribList);
    void _bind_frame_buffer(const std::string& name);
    void _unbind_frame_buffer(const std::string& name);
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_OpenGLManager_hpp_
