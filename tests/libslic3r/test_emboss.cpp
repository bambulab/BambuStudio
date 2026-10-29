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
    const std::wstring text = L"HE\nH";
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
    text2vshapes(single, font, L"HE", fp, scale);
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
        text2vshapes(single, main_font, L"A\u4E2D", fp, scale, []() { return false; }, fallback);
        text2vshapes(multiline, main_font, L"A\u4E2D\nA\u4E2D", fp, scale, []() { return false; }, fallback);
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
    text2vshapes(shape, font, L"H\nH", fp, scale);

    FontProp fp_gap(10.f);
    fp_gap.line_gap = 500; // font points
    EmbossShape shape_gap;
    text2vshapes(shape_gap, font, L"H\nH", fp_gap, scale);

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
