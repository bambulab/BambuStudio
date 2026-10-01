#ifndef slic3r_GUI_AssemblyStepsUtilsInternal_hpp_
#define slic3r_GUI_AssemblyStepsUtilsInternal_hpp_
// File-local helpers shared between AssemblyStepsUtils.cpp (logic) and
// AssemblyStepsUtilsImgui.cpp (ImGui rendering). They used to live as
// anonymous-namespace / static functions inside AssemblyStepsUtils.cpp; once the
// ImGui code moved to its own translation unit both files need them, so they are
// declared `inline` here to keep a single definition across both TUs.
#include <array>
#include <set>
#include <string>
#include <vector>
#include <cstdint>
#include <imgui/imgui.h>
#include "libslic3r/Format/AssemblyStepsJson.hpp" // AssemblyNoteSelectionType
#include "../I18N.hpp" // L() marker for xgettext extraction of color tips

namespace Slic3r {
namespace GUI {
// Part-number label font-size config (shared by the logic TU that loads/saves it
// and the ImGui TU that renders the slider).
inline constexpr const char *ASSEMBLY_LABEL_FONT_SIZE_CONFIG_KEY = "assembly_part_number_label_font_size";
inline constexpr float ASSEMBLY_LABEL_DEFAULT_FONT_SIZE = 25.0f;
inline constexpr float ASSEMBLY_LABEL_MAX_FONT_SIZE_FACTOR = 2.5f;

struct NoteColorItem {
    std::array<int, 4> color;
    const char *id;
    const char *tip;
    bool has_border;
};

inline const NoteColorItem kNoteColors[] = {
    { {213,  61,  64, 255}, "red",    L("Red"),    false },
    { {240, 159,  19, 255}, "orange", L("Orange"), false },
    { { 35, 169,  46, 255}, "green",  L("Green"),  false },
    { { 63, 130, 240, 255}, "blue",   L("Blue"),   false },
    { { 29,  32,  45, 255}, "black",  L("Black"),  false },
    { {125, 127, 134, 255}, "grey",   L("Grey"),   false },
    { {255, 255, 255, 255}, "white",  L("White"),  true  },
};

inline ImU32 note_color_to_im_u32(const std::array<int, 4> &color)
{
    return IM_COL32(color[0], color[1], color[2], color[3]);
}

inline std::array<float, 4> note_color_to_float_array(const std::array<int, 4> &color)
{
    return {color[0] / 255.0f, color[1] / 255.0f, color[2] / 255.0f, color[3] / 255.0f};
}

inline int note_palette_index_from_color(const std::array<int, 4> &color)
{
    for (int i = 0; i < (int)IM_ARRAYSIZE(kNoteColors); ++i) {
        if (kNoteColors[i].color[0] == color[0] &&
            kNoteColors[i].color[1] == color[1] &&
            kNoteColors[i].color[2] == color[2])
            return i;
    }
    return 2;
}

inline std::array<int, 4> note_color_from_palette_index(int idx)
{
    if (idx < 0 || idx >= (int)IM_ARRAYSIZE(kNoteColors))
        return kNoteColors[2].color; // fallback to green when out of range
    return kNoteColors[idx].color;
}

inline int note_tool_index_from_selection(AssemblyNoteSelectionType type)
{
    // Must mirror the order of `m_note_tools` initialized in
    // render_assembly_guide_panel ("Add Notes" section): rect, circle, vector, tag.
    switch (type) {
    case AssemblyNoteSelectionType::Rectangle:  return 0;
    case AssemblyNoteSelectionType::Circle:     return 1;
    case AssemblyNoteSelectionType::PlainArrow: return 2;
    case AssemblyNoteSelectionType::TextLabel:  return 3;
    default:                                    return -1;
    }
}

// On-demand large-glyph font cache for crisp big text (assembly title overlay and
// part-number labels). The shared ImGui atlas is baked at the small UI size, so
// drawing it at 3x upscales the bitmap and looks blurry. This cache bakes only the
// glyphs actually used, at the requested pixel size, into a private ImFontAtlas +
// GL texture. The character set accumulates as new glyphs appear; the atlas is
// rebuilt lazily and the previous GL texture is retired through deferred deletion
// (freed on the next frame) so draw commands recorded earlier in the current frame
// stay valid. Call release() on the GL thread to free everything.
class AssemblyLargeFontCache
{
public:
    AssemblyLargeFontCache() = default;
    ~AssemblyLargeFontCache();

    AssemblyLargeFontCache(const AssemblyLargeFontCache &) = delete;
    AssemblyLargeFontCache &operator=(const AssemblyLargeFontCache &) = delete;

    // Registers the glyphs of utf8_text at >= px and returns a font that can render
    // them crisply (downscaled, never upscaled). Returns nullptr if the atlas could
    // not be built, in which case callers should fall back to the shared UI font.
    ImFont *ensure(const std::string &utf8_text, float px);

    // Texture to push on the draw list before AddText() with the returned font.
    ImTextureID tex_id() const { return m_tex_id; }

    // Frees the GL texture(s) and the private atlas. Must run on the GL thread.
    void release();

private:
    bool        rebuild();
    void        flush_retired_textures();

    ImFontAtlas        *m_atlas{nullptr};
    ImFont             *m_font{nullptr};
    ImTextureID         m_tex_id{(ImTextureID)0};
    unsigned int        m_gl_texture{0};
    std::set<ImWchar>   m_chars;
    float               m_baked_px{0.0f};
    bool                m_dirty{false};
    int                 m_last_frame{-1};
    std::vector<unsigned int> m_retired_textures; // freed one frame after retirement
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_AssemblyStepsUtilsInternal_hpp_