// Unit tests for Slic3r::GUI::bidi (Hebrew-restricted UAX #9 reordering for ImGui).
//
// This file is deliberately pure ASCII: every non-ASCII string is built from code point numbers by
// S() below (independent of the compiler's source/execution charset and of the module under test).
// "Visual" strings are what a non-bidi renderer must draw from left to right. rev() reverses the
// code points of a Hebrew word, which is exactly what the visual form of a pure RTL run looks like.

#include <catch_main.hpp>

#include <initializer_list>
#include <string>
#include <vector>

#include "slic3r/GUI/Bidi.hpp"

using namespace Slic3r::GUI;

namespace {

// Test-local UTF-8 encoder: code point list -> UTF-8 string.
std::string S(std::initializer_list<unsigned> cps)
{
    std::string out;
    for (unsigned c : cps) {
        if (c < 0x80) {
            out.push_back(char(c));
        } else if (c < 0x800) {
            out.push_back(char(0xC0 | (c >> 6)));
            out.push_back(char(0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            out.push_back(char(0xE0 | (c >> 12)));
            out.push_back(char(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(char(0x80 | (c & 0x3F)));
        } else {
            out.push_back(char(0xF0 | (c >> 18)));
            out.push_back(char(0x80 | ((c >> 12) & 0x3F)));
            out.push_back(char(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(char(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

// Test-local code point reversal (does not use the module under test).
std::string rev(const std::string& s)
{
    std::vector<std::string> cps;
    for (char ch : s) {
        const unsigned char b = static_cast<unsigned char>(ch);
        if ((b & 0xC0) == 0x80 && !cps.empty())
            cps.back().push_back(ch); // continuation byte
        else
            cps.emplace_back(1, ch);
    }
    std::string out;
    for (auto it = cps.rbegin(); it != cps.rend(); ++it)
        out += *it;
    return out;
}

// Number of code points, used as a fake "glyph width" of 1 per code point.
float cp_width(std::string_view s)
{
    float n = 0.f;
    for (char ch : s)
        if ((static_cast<unsigned char>(ch) & 0xC0) != 0x80)
            n += 1.f;
    return n;
}

// Hebrew words (logical order). Hebrew letters: alef 5D0, bet 5D1, gimel 5D2, dalet 5D3, he 5D4, vav 5D5,
// het 5D7, yod 5D9, final kaf 5DA, kaf 5DB, lamed 5DC, final mem 5DD, mem 5DE, ayin 5E2, pe 5E4,
// samekh 5E1, resh 5E8, shin 5E9, tav 5EA.
const std::string LAYER    = S({0x5E9, 0x5DB, 0x5D1, 0x5D4});               // "layer"
const std::string OF       = S({0x5DE, 0x5EA, 0x5D5, 0x5DA});               // "out of"
const std::string HEIGHT   = S({0x5D2, 0x5D5, 0x5D1, 0x5D4});               // "height"
const std::string PRINT    = S({0x5D4, 0x5D3, 0x5E4, 0x5E1, 0x5D4});        // "printing"
const std::string ON_      = S({0x5E2, 0x5DC});                             // "on"
const std::string DEFAULT1 = S({0x5D1, 0x5E8, 0x5D9, 0x5E8, 0x5EA});        // first word of "default"
const std::string DEFAULT2 = S({0x5DE, 0x5D7, 0x5D3, 0x5DC});               // second word of "default"
const std::string SAVING   = S({0x5E9, 0x5DE, 0x5D9, 0x5E8, 0x5D4});        // "saving"
const std::string SHOULD   = S({0x5D4, 0x5D0, 0x5DD});                      // "whether"
const std::string TOSAVE   = S({0x5DC, 0x5E9, 0x5DE, 0x5D5, 0x5E8});        // "to save"
const std::string DONE     = S({0x5D4, 0x5D5, 0x5E9, 0x5DC, 0x5DD});        // "completed"
const std::string DONE_F   = S({0x5D4, 0x5D5, 0x5E9, 0x5DC, 0x5DE, 0x5D4});  // "completed" (fem.)
const std::string PEACE    = S({0x5E9, 0x5DC, 0x5D5, 0x5DD});               // "shalom"
const std::string WORLD    = S({0x5E2, 0x5D5, 0x5DC, 0x5DD});               // "olam"
const std::string HOUR     = S({0x5E9, 0x5E2, 0x5D4});                      // "hour"
const std::string CHOOSE   = S({0x5D1, 0x5D7, 0x5E8});                      // "choose"

// Neutral objects.
const std::string ICON_INFO = S({0x0803}); // ImGui::InfoMarker custom font glyph
const std::string ICON_PUA  = S({0xE000}); // Private Use Area
const std::string C0_ICON   = S({0x04});   // ImGui::PrintIconMarker
const std::string RLM       = S({0x200F});
const std::string LRM       = S({0x200E});

struct Case
{
    const char* name;
    std::string logical;
    std::string expected_visual;
};

} // namespace

TEST_CASE("bidi::visual table", "[bidi]")
{
    const std::vector<Case> cases = {
        {"pure Hebrew word", PEACE, rev(PEACE)},
        {"pure Hebrew two words", PEACE + " " + WORLD, rev(WORLD) + " " + rev(PEACE)},
        // EN after R stays EN (W7 does not apply); spaces between R and EN are R (N1) => digits form LTR runs.
        {"Hebrew + numbers", LAYER + " 3 " + OF + " 10", "10 " + rev(OF) + " 3 " + rev(LAYER)},
        // W4: the '.' between digits is part of the number; "mm" is an L run.
        {"decimal and unit", HEIGHT + " 0.2 mm", "mm 0.2 " + rev(HEIGHT)},
        // W4 CS between ENs: "15:30" is one LTR run.
        {"time", HOUR + " 15:30", "15:30 " + rev(HOUR)},
        // W5: ET after EN becomes EN, "50%" stays together in reading order.
        {"percent", DONE + " 50%", "50% " + rev(DONE)},
        // W7 turns the digit in "X1C" into L; N1 keeps spaces between L words in the LTR run.
        {"Latin brand inside Hebrew", PRINT + " " + ON_ + " Bambu Lab X1C", "Bambu Lab X1C " + rev(ON_) + " " + rev(PRINT)},
        // L4: both brackets are drawn at an odd level, so their glyphs are mirrored.
        {"brackets mirrored", "(" + DEFAULT1 + " " + DEFAULT2 + ")", "(" + rev(DEFAULT2) + " " + rev(DEFAULT1) + ")"},
        {"brackets around Latin", PRINT + " (Bambu) " + DONE_F, rev(DONE_F) + " (Bambu) " + rev(PRINT)},
        // Trailing neutrals at the end of an RTL paragraph go to the visual left (N1 with eos = RTL).
        {"trailing ellipsis", SAVING + "...", "..." + rev(SAVING)},
        {"trailing question mark", SHOULD + " " + TOSAVE + "?", "?" + rev(TOSAVE) + " " + rev(SHOULD)},
        {"trailing colon after Latin", CHOOSE + " Bambu Lab:", ":Bambu Lab " + rev(CHOOSE)},
        // Icons (U+0800.. ImGui custom glyph rects, C0 markers, PUA) are neutral: they follow the RTL text.
        {"icon U+0803 before text", ICON_INFO + " " + PEACE, rev(PEACE) + " " + ICON_INFO},
        {"icon PUA before text", ICON_PUA + " " + PEACE, rev(PEACE) + " " + ICON_PUA},
        {"icon C0 marker before text", C0_ICON + " " + PEACE, rev(PEACE) + " " + C0_ICON},
        // ImGui highlight markers (0x02 start / 0x03 end) are swapped like brackets so the span stays well formed.
        {"colour markers", "ab \x02" + PEACE + "\x03", "\x02" + rev(PEACE) + "\x03 ab"},
        // Explicit marks are stripped.
        {"RLM/LRM stripped", RLM + PEACE + LRM, rev(PEACE)},
        // Each line is its own paragraph.
        {"multi-line", LAYER + " 3\n" + HEIGHT + " 0.2 mm", "3 " + rev(LAYER) + "\nmm 0.2 " + rev(HEIGHT)},
        {"Latin line next to Hebrew line", "Layer 3!\n" + PEACE, "Layer 3!\n" + rev(PEACE)},
        {"empty line kept", PEACE + "\n\n" + WORLD, rev(PEACE) + "\n\n" + rev(WORLD)},
        {"CRLF", PEACE + "\r\n" + WORLD, rev(PEACE) + "\r\n" + rev(WORLD)},
        // W1: combining points stay attached after their base letter (ImGui draws marks after the base).
        // shin 5E9 + shin dot 5C1 + qamats 5B8, lamed, vav + holam 5B9, final mem.
        {"niqqud",
         S({0x5E9, 0x5C1, 0x5B8, 0x5DC, 0x5D5, 0x5B9, 0x5DD}),
         S({0x5DD, 0x5D5, 0x5B9, 0x5DC, 0x5E9, 0x5C1, 0x5B8})},
    };

    for (const Case& c : cases) {
        INFO(c.name);
        CHECK(bidi::visual(c.logical) == c.expected_visual);
    }
}

TEST_CASE("bidi::visual passes non-Hebrew text through unchanged", "[bidi]")
{
    const std::vector<std::string> samples = {
        "",
        "Layer 3 of 10",
        "Height: 0.2 mm (default)...",
        "50%",
        "Bambu Lab X1C?",
        // Latin-1, Greek, Cyrillic, CJK
        S({0xC4, 'p', 'f', 'e', 'l', ' ', 0x391, 0x3B2, 0x3B3, ' ', 0x41F, 0x440, 0x438, 0x432, 0x435, 0x442, ' ', 0x4E2D, 0x6587}),
        // Armenian: U+0531 must not be mistaken for Hebrew
        S({0x531, 0x532}),
    };
    for (const std::string& s : samples) {
        INFO(s);
        CHECK_FALSE(bidi::has_rtl(s));
        CHECK(bidi::visual(s) == s);
        CHECK(bidi::visual(s, false) == s);
    }
}

TEST_CASE("bidi::visual with LTR base direction", "[bidi]")
{
    // Hebrew run inside a Latin paragraph: only the Hebrew run is reversed.
    CHECK(bidi::visual("ab " + PEACE, false) == "ab " + rev(PEACE));
    CHECK(bidi::visual(PEACE + " ab", false) == rev(PEACE) + " ab");
}

TEST_CASE("bidi::has_rtl", "[bidi]")
{
    CHECK_FALSE(bidi::has_rtl(""));
    CHECK_FALSE(bidi::has_rtl("abc 123"));
    CHECK(bidi::has_rtl(S({0x5D0})));                  // alef
    CHECK(bidi::has_rtl(S({0x5EA})));                  // tav
    CHECK(bidi::has_rtl("abc " + S({0x5D0}) + " def"));
    CHECK(bidi::has_rtl(S({0x590})));                  // first code point of the Hebrew block
    CHECK(bidi::has_rtl(S({0x5B0})));                  // niqqud only
    CHECK(bidi::has_rtl(S({0x5FF})));                  // last code point of the Hebrew block
    CHECK(bidi::has_rtl(S({0xFB1D})));                 // presentation form start
    CHECK(bidi::has_rtl(S({0xFB4F})));                 // presentation form end
    CHECK_FALSE(bidi::has_rtl(S({0xFB1C})));
    CHECK_FALSE(bidi::has_rtl(S({0xFB50})));           // Arabic presentation forms
    CHECK_FALSE(bidi::has_rtl(S({0x58F})));            // Armenian drachma sign, just below the Hebrew block
    CHECK_FALSE(bidi::has_rtl(S({0x600})));            // Arabic: not supported by this module
}

TEST_CASE("bidi::visual_label keeps the ImGui ID part", "[bidi]")
{
    CHECK(bidi::visual_label(PEACE + "##btn_1") == rev(PEACE) + "##btn_1");
    CHECK(bidi::visual_label(PEACE + "###id") == rev(PEACE) + "###id");
    CHECK(bidi::visual_label("##" + PEACE) == "##" + PEACE); // empty visible part: ID untouched
    CHECK(bidi::visual_label("Layer##id") == "Layer##id");
}

TEST_CASE("bidi::wrap_and_reorder wraps on the logical string", "[bidi]")
{
    // 1 unit per code point. Words: LAYER(4) 3(1) OF(4) 10(2) PRINT(5).
    const std::string text  = LAYER + " 3 " + OF + " 10 " + PRINT;
    const auto        lines = bidi::wrap_and_reorder(text, 10.f, cp_width);
    REQUIRE(lines.size() == 3);
    // The first logical words must be on the first line (they would be last if wrapping ran on visual text).
    CHECK(lines[0] == "3 " + rev(LAYER));
    CHECK(lines[1] == "10 " + rev(OF));
    CHECK(lines[2] == rev(PRINT));

    SECTION("everything fits")
    {
        const auto one = bidi::wrap_and_reorder(text, 100.f, cp_width);
        REQUIRE(one.size() == 1);
        CHECK(one[0] == bidi::visual(text));
    }
    SECTION("no wrapping when width <= 0")
    {
        const auto one = bidi::wrap_and_reorder(text, 0.f, cp_width);
        REQUIRE(one.size() == 1);
        CHECK(one[0] == bidi::visual(text));
    }
    SECTION("explicit newline")
    {
        const auto two = bidi::wrap_and_reorder(PEACE + "\n" + WORLD, 100.f, cp_width);
        REQUIRE(two.size() == 2);
        CHECK(two[0] == rev(PEACE));
        CHECK(two[1] == rev(WORLD));
    }
    SECTION("over-long word is broken at code points")
    {
        const auto parts = bidi::wrap_and_reorder(PEACE, 3.f, cp_width); // 4 code points, width 3
        REQUIRE(parts.size() == 2);
        CHECK(parts[0] == rev(S({0x5E9, 0x5DC, 0x5D5})));
        CHECK(parts[1] == S({0x5DD}));
    }
    SECTION("Latin text wraps normally and stays unchanged")
    {
        const auto latin = bidi::wrap_and_reorder("aa bb cc", 5.f, cp_width);
        REQUIRE(latin.size() == 2);
        CHECK(latin[0] == "aa bb");
        CHECK(latin[1] == "cc");
    }
    SECTION("empty input")
    {
        const auto e = bidi::wrap_and_reorder("", 10.f, cp_width);
        REQUIRE(e.size() == 1);
        CHECK(e[0].empty());
    }
}

TEST_CASE("bidi UTF-8 helpers", "[bidi]")
{
    // 1-, 2-, 2-, 3- and 4-byte sequences.
    const std::string    s = S({'a', 0xE9, 0x5D0, 0x20AC, 0x1F600});
    const std::u32string u = bidi::utf8_to_utf32(s);
    REQUIRE(u.size() == 5);
    CHECK(u[0] == U'a');
    CHECK(u[1] == 0x00E9);
    CHECK(u[2] == 0x05D0);
    CHECK(u[3] == 0x20AC);
    CHECK(u[4] == 0x1F600);
    CHECK(bidi::utf32_to_utf8(u) == s);

    // Malformed input never crashes and yields U+FFFD (0xFF is never valid; a lone 0xD7 is truncated).
    std::string bad = "a";
    bad.push_back(char(0xFF));
    bad.push_back('b');
    bad.push_back(char(0xD7));
    const std::u32string ub = bidi::utf8_to_utf32(bad);
    REQUIRE(ub.size() == 4);
    CHECK(ub[1] == 0xFFFD);
    CHECK(ub[3] == 0xFFFD);
}

TEST_CASE("bidi::VisualText only copies when needed", "[bidi]")
{
    const char*      latin = "Layer 3";
    bidi::VisualText v1(latin);
    CHECK_FALSE(v1.changed());
    CHECK(v1.c_str() == latin); // same pointer: no copy for non-Hebrew UI languages

    const std::string he = PEACE + "##id";
    bidi::VisualText  v2(he);
    CHECK(v2.changed());
    CHECK(std::string(v2.c_str()) == rev(PEACE) + "##id");

    CHECK(std::string(bidi::vis(static_cast<const char*>(nullptr)).c_str()).empty());
}
