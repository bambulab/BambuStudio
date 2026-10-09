#include <catch2/catch.hpp>

#include <boost/filesystem.hpp>

#include <libslic3r/Emboss.hpp>
#include <libslic3r/EmbossShape.hpp>
#include <libslic3r/TextConfiguration.hpp>

using namespace Slic3r;
using namespace Slic3r::Emboss;

namespace {

// font present on a developer machine, empty when none is found
std::string get_font_filepath()
{
    const char *candidates[] = {
#ifdef _WIN32
        "C:/Windows/Fonts/arial.ttf",
#elif defined(__APPLE__)
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
#else
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
#endif
    };
    for (const char *path : candidates)
        if (boost::filesystem::exists(path))
            return path;
    return {};
}

std::string get_cjk_font_filepath()
{
    const char *candidates[] = {
#ifdef _WIN32
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simsun.ttc",
#elif defined(__APPLE__)
        "/System/Library/Fonts/PingFang.ttc",
        "/System/Library/Fonts/Supplemental/Songti.ttc",
#else
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.otf",
#endif
    };
    for (const char *path : candidates)
        if (boost::filesystem::exists(path))
            return path;
    return {};
}

ExPolygonsWithIds ids_to_shapes(const std::vector<unsigned> &ids)
{
    ExPolygonsWithIds shapes;
    for (unsigned id : ids) {
        ExPolygonsWithId shape;
        shape.id = id;
        shapes.push_back(shape);
    }
    return shapes;
}

const char32_t ROCKET = U'\U0001F680'; // outside the Basic Multilingual Plane

// fonts packed with the application, resolved from this source file
std::string packed_font(const char *file_name)
{
    boost::filesystem::path path = boost::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "resources" / "fonts" / file_name;
    return boost::filesystem::exists(path) ? path.string() : std::string();
}

FontFileWithCache load_font(const std::string &path)
{
    std::unique_ptr<FontFile> font_file = create_font_file(path.c_str());
    REQUIRE(font_file != nullptr);
    return FontFileWithCache(std::move(font_file));
}

} // namespace

TEST_CASE("Line ranges split glyph indices by new line", "[Emboss]")
{
    CHECK(get_line_ranges(ids_to_shapes({'A', 'B'})) == LineRanges{{0, 2}});
    CHECK(get_line_ranges(ids_to_shapes({'A', 'B', '\n', 'C'})) == LineRanges{{0, 2}, {3, 4}});
    CHECK(get_line_ranges(ids_to_shapes({'\n', 'A', '\n', '\n', 'B', 'C', '\n'})) == LineRanges{{0, 0}, {1, 2}, {3, 3}, {4, 6}, {7, 7}});
    CHECK(get_line_ranges({}) == LineRanges{{0, 0}});
    CHECK(get_count_lines(ids_to_shapes({'A', '\n', 'B'})) == 2);
}

TEST_CASE("Multi line text shapes keep per glyph data aligned", "[Emboss]")
{
    std::string font_path = get_font_filepath();
    if (font_path.empty()) {
        WARN("No font file found on this machine, test skipped");
        return;
    }
    std::unique_ptr<FontFile> font_file = create_font_file(font_path.c_str());
    REQUIRE(font_file != nullptr);
    FontFileWithCache font(std::move(font_file));
    const FontFile &  ff = *font.font_file;

    FontProp fp(10.f);
    double   scale = get_text_shape_scale(fp, ff);

    // H sits on the base line, so the base line distance can be measured on glyph bounds
    const std::u32string text = U"HE\nH";
    EmbossShape        shape;
    text2vshapes(shape, font, text, fp, scale);

    REQUIRE(shape.shapes_with_ids.size() == text.size());
    CHECK(shape.text_cursors.size() == text.size());
    CHECK(shape.text_absolute_cursors.size() == text.size());
    CHECK(shape.text_align_offsets.size() == text.size());
    CHECK(shape.shapes_with_ids[2].id == ENTER_UNICODE);
    CHECK(shape.shapes_with_ids[2].expoly.empty());
    CHECK(shape.text_cursors[2] == 0.f);
    CHECK(get_line_ranges(shape.shapes_with_ids) == LineRanges{{0, 2}, {3, 4}});

    // second line is one line height below the first one
    double line_height_mm = get_line_height(ff, fp) * scale;
    CHECK(shape.line_height == Approx(line_height_mm));
    BoundingBox first_h  = get_extents(shape.shapes_with_ids[0].expoly);
    BoundingBox second_h = get_extents(shape.shapes_with_ids[3].expoly);
    CHECK((first_h.min.y() - second_h.min.y()) * scale == Approx(line_height_mm).epsilon(0.01));
    CHECK(second_h.max.y() < first_h.min.y());
    // whole text is vertically centered, so the first line moves up by half of the line height
    CHECK(shape.first_line_offset_y == Approx(line_height_mm / 2.).epsilon(0.01));

    // single line text has no line offset
    EmbossShape single;
    text2vshapes(single, font, U"HE", fp, scale);
    CHECK(single.first_line_offset_y == 0.f);
    CHECK(single.text_align_offsets.size() == 2);

    // left aligned text has an offset for every glyph too
    FontProp fp_left(10.f);
    fp_left.align.first = FontProp::HorizontalAlign::left;
    EmbossShape left;
    text2vshapes(left, font, text, fp_left, scale);
    CHECK(left.text_align_offsets.size() == text.size());
}

TEST_CASE("Fallback font baseline correction is independent of line count", "[Emboss]")
{
    const std::string main_font_path = get_font_filepath();
    const std::string cjk_font_path  = get_cjk_font_filepath();
    if (main_font_path.empty() || cjk_font_path.empty()) {
        WARN("Main or CJK fallback font not found, test skipped");
        return;
    }

    FontFileWithCache main_font(create_font_file(main_font_path.c_str()));
    FontFileWithCache fallback_font(create_font_file(cjk_font_path.c_str()));
    REQUIRE(main_font.has_value());
    REQUIRE(fallback_font.has_value());

    BackFontCacheFn fallback = [&fallback_font]() {
        return std::vector<FontFileWithCache>{fallback_font};
    };

    for (FontProp::HorizontalAlign align : {FontProp::HorizontalAlign::center, FontProp::HorizontalAlign::left}) {
        FontProp fp(10.f);
        fp.align.first = align;
        const double scale = get_text_shape_scale(fp, *main_font.font_file);

        EmbossShape single;
        EmbossShape multiline;
        text2vshapes(single, main_font, U"A\u4E2D", fp, scale, []() { return false; }, fallback);
        text2vshapes(multiline, main_font, U"A\u4E2D\nA\u4E2D", fp, scale, []() { return false; }, fallback);
        if (single.shapes_with_ids.size() != 2 || single.shapes_with_ids[1].expoly.empty()) {
            WARN("Selected fallback font does not provide the CJK test glyph, test skipped");
            return;
        }

        REQUIRE(multiline.text_align_offsets.size() == 5);
        const float single_baseline_delta = single.text_align_offsets[1].y() - single.text_align_offsets[0].y();
        CHECK(multiline.text_align_offsets[1].y() - multiline.text_align_offsets[0].y() == Approx(single_baseline_delta));
        CHECK(multiline.text_align_offsets[4].y() - multiline.text_align_offsets[3].y() == Approx(single_baseline_delta));
    }
}

TEST_CASE("Line gap changes distance between text lines", "[Emboss]")
{
    std::string font_path = get_font_filepath();
    if (font_path.empty()) {
        WARN("No font file found on this machine, test skipped");
        return;
    }
    std::unique_ptr<FontFile> font_file = create_font_file(font_path.c_str());
    REQUIRE(font_file != nullptr);
    FontFileWithCache font(std::move(font_file));
    const FontFile &  ff = *font.font_file;

    FontProp    fp(10.f);
    double      scale = get_text_shape_scale(fp, ff);
    EmbossShape shape;
    text2vshapes(shape, font, U"H\nH", fp, scale);

    FontProp fp_gap(10.f);
    fp_gap.line_gap = 500; // font points
    EmbossShape shape_gap;
    text2vshapes(shape_gap, font, U"H\nH", fp_gap, scale);

    // line gap is in font points, one em is size_in_mm
    double expected_delta = 500. * fp.size_in_mm / get_font_info(ff, fp).unit_per_em;
    CHECK(shape_gap.line_height - shape.line_height == Approx(expected_delta).epsilon(0.01));
    BoundingBox h0       = get_extents(shape.shapes_with_ids[0].expoly);
    BoundingBox h2       = get_extents(shape.shapes_with_ids[2].expoly);
    BoundingBox g0       = get_extents(shape_gap.shapes_with_ids[0].expoly);
    BoundingBox g2       = get_extents(shape_gap.shapes_with_ids[2].expoly);
    double      dist     = (h0.min.y() - h2.min.y()) * scale;
    double      dist_gap = (g0.min.y() - g2.min.y()) * scale;
    CHECK(dist_gap - dist == Approx(expected_delta).epsilon(0.01));
}

TEST_CASE("UTF-8 conversion keeps code points above U+FFFF", "[Emboss]")
{
    const std::string utf8 = "A\xF0\x9F\x9A\x80" "b"; // "A🚀b"
    std::u32string    utf32 = to_utf32(utf8);
    REQUIRE(utf32.size() == 3);
    CHECK(utf32[0] == U'A');
    CHECK(utf32[1] == ROCKET);
    CHECK(utf32[2] == U'b');
    CHECK(to_utf8(utf32) == utf8);

    CHECK(get_count_lines(std::string("a\nb\xF0\x9F\x9A\x80")) == 2);
    CHECK(get_count_lines(std::u32string(U"\U0001F680")) == 1);
    CHECK(get_count_lines(std::string()) == 0);
}

TEST_CASE("Range text keeps an emoji only when the font has a glyph for it", "[Emboss]")
{
    std::string symbola = packed_font("Symbola.ttf");
    std::string latin   = packed_font("HarmonyOS_Sans_SC_Regular.ttf");
    if (symbola.empty() || latin.empty()) {
        WARN("Packed fonts not found, test skipped");
        return;
    }
    const std::string text = "A\xF0\x9F\x9A\x80"; // "A🚀"

    bool        unknown = true;
    std::string range   = create_range_text(text, *load_font(symbola).font_file, 0, &unknown);
    CHECK(!unknown);
    CHECK(to_utf32(range).find(ROCKET) != std::u32string::npos);

    unknown = false;
    range   = create_range_text(text, *load_font(latin).font_file, 0, &unknown);
    CHECK(unknown);
    CHECK(to_utf32(range).find(ROCKET) == std::u32string::npos);
    CHECK(to_utf32(range).find(U'A') != std::u32string::npos);
}

TEST_CASE("Backup font supplies the outline of an emoji", "[Emboss]")
{
    std::string symbola = packed_font("Symbola.ttf");
    std::string latin   = packed_font("HarmonyOS_Sans_SC_Regular.ttf");
    if (symbola.empty() || latin.empty()) {
        WARN("Packed fonts not found, test skipped");
        return;
    }
    FontFileWithCache primary = load_font(latin);
    FontFileWithCache backup  = load_font(symbola);

    FontProp fp(10.f);
    double   scale = get_text_shape_scale(fp, *primary.font_file);
    const std::u32string text = U"A\U0001F680";

    // the primary font has no rocket, without a backup font the glyph stays empty
    EmbossShape alone;
    text2vshapes(alone, primary, text, fp, scale);
    REQUIRE(alone.shapes_with_ids.size() == 2);
    CHECK(alone.shapes_with_ids[0].id == U'A');
    CHECK(!alone.shapes_with_ids[0].expoly.empty());
    CHECK(alone.shapes_with_ids[1].id == static_cast<unsigned>(ROCKET));
    CHECK(alone.shapes_with_ids[1].expoly.empty());

    // with Symbola as backup the rocket gets an outline and the glyph arrays stay aligned
    EmbossShape shape;
    text2vshapes(shape, primary, text, fp, scale, []() { return false; }, [&backup]() { return std::vector<FontFileWithCache>{backup}; });
    REQUIRE(shape.shapes_with_ids.size() == 2);
    CHECK(shape.shapes_with_ids[0].id == U'A');
    CHECK(shape.shapes_with_ids[1].id == static_cast<unsigned>(ROCKET));
    CHECK(!shape.shapes_with_ids[0].expoly.empty());
    CHECK(!shape.shapes_with_ids[1].expoly.empty());
    CHECK(shape.text_cursors.size() == 2);
    CHECK(shape.text_align_offsets.size() == 2);
    BoundingBox a      = get_extents(shape.shapes_with_ids[0].expoly);
    BoundingBox rocket = get_extents(shape.shapes_with_ids[1].expoly);
    CHECK(rocket.min.x() > a.min.x()); // the rocket follows the A on the line
}

TEST_CASE("Range text merges the glyphs of every font when the first one does not cover the text", "[Emboss]")
{
    std::string symbola = packed_font("Symbola.ttf");
    std::string latin   = packed_font("HarmonyOS_Sans_SC_Regular.ttf");
    if (symbola.empty() || latin.empty()) {
        WARN("Packed fonts not found, test skipped");
        return;
    }
    std::shared_ptr<const FontFile> primary = create_font_file(latin.c_str());
    std::shared_ptr<const FontFile> backup  = create_font_file(symbola.c_str());
    REQUIRE(primary != nullptr);
    REQUIRE(backup != nullptr);
    std::vector<std::shared_ptr<const FontFile>> fonts{primary, backup};

    // "A你🚀": the first font draws A and 你, only the backup has the rocket
    std::string    text    = "A\xE4\xBD\xA0\xF0\x9F\x9A\x80";
    bool           unknown = false;
    std::u32string range   = to_utf32(create_range_text(text, fonts, 0, &unknown));
    CHECK(unknown);
    CHECK(range.find(U'A') != std::u32string::npos);
    CHECK(range.find(U'你') != std::u32string::npos);
    CHECK(range.find(ROCKET) != std::u32string::npos);

    // covered by the first font alone: no fallback is reported
    text  = "A\xE4\xBD\xA0";
    range = to_utf32(create_range_text(text, fonts, 0, &unknown));
    CHECK(!unknown);
    CHECK(range.size() == 2);

    // white space and zero width marks are not missing glyphs
    text  = "A\n\t\xEF\xB8\x8F"; // "A", new line, tab, U+FE0F
    range = to_utf32(create_range_text(text, fonts, 0, &unknown));
    CHECK(!unknown);
    CHECK(range == U"A");
}

TEST_CASE("Zero width marks produce no glyph and no advance", "[Emboss]")
{
    std::string symbola = packed_font("Symbola.ttf");
    if (symbola.empty()) {
        WARN("Packed font not found, test skipped");
        return;
    }
    CHECK(is_zero_width_mark(U'️'));
    CHECK(is_zero_width_mark(U'‍'));
    CHECK(!is_zero_width_mark(U'A'));
    CHECK(!is_zero_width_mark(ROCKET));

    FontFileWithCache font = load_font(symbola);
    FontProp          fp(10.f);
    double            scale = get_text_shape_scale(fp, *font.font_file);

    EmbossShape plain, marked;
    text2vshapes(plain, font, U"AB", fp, scale);
    text2vshapes(marked, font, U"A️" "B", fp, scale); // A, VS16, B
    REQUIRE(plain.shapes_with_ids.size() == 2);
    REQUIRE(marked.shapes_with_ids.size() == 3);
    CHECK(marked.shapes_with_ids[1].expoly.empty());
    // B lands where it does without the mark
    CHECK(get_extents(marked.shapes_with_ids[2].expoly).min.x() == get_extents(plain.shapes_with_ids[1].expoly).min.x());

    // the mark is not reported as an unknown glyph
    bool        unknown = true;
    std::string text    = "A\xEF\xB8\x8F";
    create_range_text(text, *font.font_file, 0, &unknown);
    CHECK(!unknown);
}

TEST_CASE("Backup glyph keeps its vertical position when the fonts differ in units per em", "[Emboss]")
{
    // Symbola has 2048 units per em, HarmonyOS Sans 1000
    std::string symbola = packed_font("Symbola.ttf");
    std::string latin   = packed_font("HarmonyOS_Sans_SC_Regular.ttf");
    if (symbola.empty() || latin.empty()) {
        WARN("Packed fonts not found, test skipped");
        return;
    }
    FontFileWithCache primary = load_font(latin);
    FontFileWithCache backup  = load_font(symbola);
    FontProp          fp(10.f);

    // rocket drawn by Symbola as the main font
    EmbossShape direct;
    double      direct_scale = get_text_shape_scale(fp, *backup.font_file);
    text2vshapes(direct, backup, U"\U0001F680", fp, direct_scale);

    // rocket drawn by Symbola as the backup of HarmonyOS
    EmbossShape fallback;
    double      fallback_scale = get_text_shape_scale(fp, *primary.font_file);
    text2vshapes(fallback, primary, U"\U0001F680", fp, fallback_scale, []() { return false; }, [&backup]() { return std::vector<FontFileWithCache>{backup}; });

    REQUIRE(direct.shapes_with_ids.size() == 1);
    REQUIRE(fallback.shapes_with_ids.size() == 1);
    REQUIRE(!direct.shapes_with_ids[0].expoly.empty());
    REQUIRE(!fallback.shapes_with_ids[0].expoly.empty());
    BoundingBox d = get_extents(direct.shapes_with_ids[0].expoly);
    BoundingBox f = get_extents(fallback.shapes_with_ids[0].expoly);
    // same place and size in millimeters either way
    CHECK(d.min.y() * direct_scale == Approx(f.min.y() * fallback_scale).margin(0.05));
    CHECK(d.max.y() * direct_scale == Approx(f.max.y() * fallback_scale).margin(0.05));
    CHECK(d.size().x() * direct_scale == Approx(f.size().x() * fallback_scale).epsilon(0.02));
}
