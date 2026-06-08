#include "GuiTextShape.hpp"
#define STB_TRUETYPE_IMPLEMENTATION // force following include to generate implementation
#include "imgui/imstb_truetype.h" // stbtt_fontinfo
namespace Slic3r {
    using namespace Emboss;
    namespace GUI {
    ExPolygons GuiTextShape::letter2shapes(wchar_t letter, Point &cursor, FontFileWithCache &font_with_cache, const FontProp &font_prop, fontinfo_opt &font_info_cache)
    {
    assert(font_with_cache.has_value());
    if (!font_with_cache.has_value()) return {};

    Glyphs &        cache = *font_with_cache.cache;
    const FontFile &font  = *font_with_cache.font_file;

    if (letter == '\n') {
        cursor.x() = 0;
        // 2d shape has opposit direction of y
        cursor.y() -= get_line_height(font, font_prop);
        return {};
    }
    if (letter == '\t') {
        // '\t' = 4*space => same as imgui
        const int    count_spaces = 4;
        const Glyph *space        = ::get_glyph(int(' '), font, font_prop, cache, font_info_cache);
        if (space == nullptr) return {};
        cursor.x() += count_spaces * space->advance_width;
        return {};
    }
    if (letter == '\r') return {};

    int  unicode = static_cast<int>(letter);
    auto it      = cache.find(unicode);

    // Create glyph from font file and cache it
    const Glyph *glyph_ptr = (it != cache.end()) ? &it->second : get_glyph(unicode, font, font_prop, cache, font_info_cache);
    if (glyph_ptr == nullptr) return {};

    // move glyph to cursor position
    ExPolygons expolygons = glyph_ptr->shape; // copy
    for (ExPolygon &expolygon : expolygons) expolygon.translate(cursor);

    cursor.x() += glyph_ptr->advance_width;
    return expolygons;
}
} // namespace GUI

}; // namespace Slic3r
