#ifndef slic3r_OpenGLManager_hpp_
#define slic3r_OpenGLManager_hpp_

#include "GLShadersManager.hpp"
#include "libslic3r/Color.hpp"
#include <memory>
#include <unordered_map>
#include <string>

class wxWindow;
class wxGLCanvas;
class wxGLContext;

namespace Slic3r {
namespace GUI {

enum class EMSAAType : uint8_t
{
    Disabled,
    X2,
    X4,
    // desktop only
    X8,
    X16
};

enum class EPixelFormat : uint16_t
{
    Unknow,
    RGBA,
    DepthComponent,
    StencilIndex,
    DepthAndStencil
};

enum class EPixelDataType : uint16_t
{
    Unknow,
    UByte,
    Byte,
    UShort,
    Short,
    UInt,
    Int,
    Float
};

enum class EDrawPrimitiveType : uint8_t{
    Points,
    Triangles,
    TriangleStrip,
    TriangleFan,
    Lines,
    LineStrip,
    LineLoop
};

struct FrameBufferParams
{
    uint32_t m_width{ 0 };
    uint32_t m_height{ 0 };
    uint32_t m_msaa{ 0 };
};

struct FrameBuffer
{
    FrameBuffer(const FrameBufferParams& params);
    ~FrameBuffer();

    void bind();
    void unbind();

    uint32_t get_color_texture() const noexcept;

    bool is_texture_valid(uint32_t texture_id) const noexcept;

    uint32_t get_gl_id();

    void read_pixel(uint32_t x, uint32_t y, uint32_t width, uint32_t height, EPixelFormat format, EPixelDataType type, void* pixels);

    uint32_t get_height() const;

    uint32_t get_width() const;

    uint8_t get_msaa_type() const;

    bool is_format_equal(const FrameBufferParams& params) const;
private:
    enum EBlitOptionType
    {
        Color = 1,
        Depth = 1 << 1,
        All = Color | Depth
    };

private:
    void create_no_msaa_fbo(bool with_depth);
    void create_msaa_fbo();
    bool check_frame_buffer_status() const;
    void resolve();
    void mark_needs_to_resolve();
private:
    uint32_t m_width{ 0 };
    uint32_t m_height{ 0 };
    uint8_t m_msaa{ 0 };
    uint32_t m_msaa_back_buffer_rbos[2]{ UINT32_MAX, UINT32_MAX };
    uint32_t m_gl_id_for_back_fbo{ UINT32_MAX };
    uint32_t m_gl_id{ UINT32_MAX };
    uint32_t m_color_texture_id{ UINT32_MAX };
    uint32_t m_depth_rbo_id{ UINT32_MAX };
    bool m_needs_to_solve{ false };
    EBlitOptionType m_blit_option_type{ EBlitOptionType::Color };
};

class OpenGLManager
{
public:
    static std::string s_back_frame;
    static std::string s_picking_frame;
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
        int8_t m_max_offscreen_msaa{ 0 };

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

        uint8_t get_max_offscreen_msaa() const;

        // If formatted for github, plaintext with OpenGL extensions enclosed into <details>.
        // Otherwise HTML formatted for the system info dialog.
        std::string to_string(bool for_github) const;

    private:
        void detect() const;
    };

    class FrameBufferModifier
    {
    public:
        explicit FrameBufferModifier(OpenGLManager& ogl_manager, const std::string& frame_buffer_name, EMSAAType msaa_type = EMSAAType::Disabled);
        ~FrameBufferModifier();
        FrameBufferModifier& set_width(uint32_t t_width);
        FrameBufferModifier& set_height(uint32_t t_height);

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
        EMSAAType m_msaa_type{ EMSAAType::Disabled };
        uint32_t m_width{ 0u };
        uint32_t m_height{ 0u };
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
    std::unordered_map<std::string, std::shared_ptr<FrameBuffer>> m_name_to_framebuffer;
    std::weak_ptr<FrameBuffer> m_current_binded_framebuffer;
    bool m_fxaa_enabled{ false };
    EMSAAType m_off_screen_msaa_type{ EMSAAType::X4 };
    EMSAAType m_msaa_type{ EMSAAType::X4 };
    EVAOType m_vao_type{ EVAOType::Unknown };
    uint32_t m_vao{ 0 };
    bool m_b_legacy_framebuffer_enabled{ true };
    bool m_b_gizmo_keep_screen_size_enabled{ true };
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
    static ColorRGBA s_cut_plane_color;
    static bool      s_cancle_glmultidraw;

public:
    OpenGLManager();
    ~OpenGLManager();

    bool init_gl(bool popup_error = true);
    wxGLContext* init_glcontext(wxGLCanvas& canvas);

    const std::shared_ptr<GLShaderProgram>& get_shader(const std::string& shader_name) const { return m_shaders_manager.get_shader(shader_name); }
    std::shared_ptr<GLShaderProgram> get_current_shader() const { return m_shaders_manager.get_current_shader(); }
    void bind_shader(const std::shared_ptr<GLShaderProgram>&);
    void unbind_shader();

    void clear_dirty();
    void set_viewport_size(uint32_t width, uint32_t height);
    void get_viewport_size(uint32_t& width, uint32_t& height) const;
    const std::shared_ptr<FrameBuffer>& get_frame_buffer(const std::string& name) const;

    bool read_pixel(const std::string& frame_name, uint32_t x, uint32_t y, uint32_t width, uint32_t height, EPixelFormat format, EPixelDataType type, void* pixels) const;
    void bind_vao();
    void unbind_vao();
    void release_vao();
    void set_line_width(float width) const;
    void set_legacy_framebuffer_enabled(bool is_enabled);
    bool is_legacy_framebuffer_enabled() const;
    void set_gizmo_keep_screen_size_enabled(bool is_enabled);
    bool is_gizmo_keep_screen_size_enabled() const;
    void set_msaa_type(const std::string& type);
    void set_msaa_type(EMSAAType type);
    EMSAAType get_msaa_type() const;
    void set_off_screen_msaa_type(EMSAAType type);
    EMSAAType get_off_screen_msaa_type();
    void set_fxaa_enabled(bool is_enabled);
    bool is_fxaa_enabled() const;
    void blit_framebuffer(const std::string& source, const std::string& target);

    static bool init();
    static bool are_compressed_textures_supported() { return s_compressed_textures_supported; }
    static bool can_multisample() { return s_multisample == EMultisampleState::Enabled; }
    static bool are_framebuffers_supported() { return (s_framebuffers_type != EFramebufferType::Unknown); }
    static EFramebufferType get_framebuffers_type() { return s_framebuffers_type; }
    static std::string framebuffer_type_to_string(EFramebufferType type);
    static wxGLCanvas* create_wxglcanvas(wxWindow& parent, EMSAAType msaa_type = EMSAAType::Disabled);
    static const GLInfo& get_gl_info() { return s_gl_info; }
    static bool use_manually_generated_mipmaps() { return m_use_manually_generated_mipmaps; }
    static void       set_cut_plane_color(ColorRGBA);
    static const ColorRGBA &get_cut_plane_color();
    static bool get_cancle_glmultidraw() { return s_cancle_glmultidraw; }
    static void set_cancle_glmultidraw(bool flag) { s_cancle_glmultidraw = flag; }
    static unsigned int get_draw_primitive_type(EDrawPrimitiveType type);

private:
    static void detect_multisample(int* attribList);
    void _bind_frame_buffer(const std::string& name, EMSAAType msaa_type, uint32_t t_width = 0, uint32_t t_height = 0);
    void _unbind_frame_buffer(const std::string& name);
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_OpenGLManager_hpp_
