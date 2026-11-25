#define CATCH_CONFIG_MAIN
#include "catch2/catch.hpp"
#include "slic3r/GUI/MaterialColorLabel.hpp"

using namespace Slic3r;
using GUI::format_material_color_label;

static FilamentColor colors(int type, std::initializer_list<wxColour> values)
{
    FilamentColor result;
    for (const auto& color : values) result.AddColor(color);
    result.EndSet(type);
    return result;
}

TEST_CASE("Material labels distinguish single, multicolor and gradient filaments")
{
    const wxColour blue(0x1D, 0x5F, 0xBF), green(0x0A, 0xA6, 0x6A), red(0xD7, 0x27, 0x2F);
    const auto single = colors(2, {blue});
    const auto multi = colors(1, {blue, green});
    const auto gradient = colors(0, {red, blue});
    CHECK(format_material_color_label("Blue", single, "Unknown") == "Blue (#1D5FBFFF)");
    CHECK(format_material_color_label("", single, "Unknown") == "(#1D5FBFFF)");
    CHECK(format_material_color_label("Blue-green", multi, "Unknown") == "Blue-green (#1D5FBFFF, #0AA66AFF)");
    CHECK(format_material_color_label("", multi, "Unknown") == "(#1D5FBFFF, #0AA66AFF)");
    CHECK(format_material_color_label("Red-blue", gradient, "Unknown") == "Red-blue (#D7272FFF -> #1D5FBFFF)");
    CHECK(format_material_color_label("", gradient, "Unknown") == "(#D7272FFF -> #1D5FBFFF)");
    CHECK(format_material_color_label("", colors(0, {blue, red}), "Unknown") == "(#1D5FBFFF -> #D7272FFF)");
    CHECK(format_material_color_label("", colors(0, {red, green, blue}), "Unknown") == "(#D7272FFF -> #0AA66AFF -> #1D5FBFFF)");
    CHECK(format_material_color_label("", colors(0, {blue}), "Unknown") == "(#1D5FBFFF)");
    CHECK(format_material_color_label("", colors(2, {wxColour(0, 0, 0, 0)}), "Unknown") == "(#00000000)");
    CHECK(format_material_color_label("", colors(2, {wxColour(0, 0, 0)}), "Unknown") == "(#000000FF)");
    CHECK(format_material_color_label("", colors(2, {}), "Unknown") == "Unknown");
    CHECK(format_material_color_label(wxString::FromUTF8("红蓝渐变"), gradient, "Unknown") == wxString::FromUTF8("红蓝渐变 (#D7272FFF -> #1D5FBFFF)"));
}
