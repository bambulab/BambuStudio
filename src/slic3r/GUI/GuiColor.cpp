#include "GuiColor.hpp"

#include <cmath>

namespace Slic3r { namespace GUI {
wxColour convert_to_wxColour(const RGBA &color)
{
    auto     r = std::clamp((int) (color[0] * 255.f), 0, 255);
    auto     g = std::clamp((int) (color[1] * 255.f), 0, 255);
    auto     b = std::clamp((int) (color[2] * 255.f), 0, 255);
    auto     a = std::clamp((int) (color[3] * 255.f), 0, 255);
    wxColour wx_color(r, g, b, a);
    return wx_color;
}

RGBA convert_to_rgba(const wxColour &color)
{
    RGBA rgba;
    rgba[0] = std::clamp(color.Red() / 255.f, 0.f, 1.f);
    rgba[1] = std::clamp(color.Green() / 255.f, 0.f, 1.f);
    rgba[2] = std::clamp(color.Blue() / 255.f, 0.f, 1.f);
    rgba[3] = std::clamp(color.Alpha() / 255.f, 0.f, 1.f);
    return rgba;
}

float calc_color_distance(wxColour c1, wxColour c2)
{
    return calc_color_distance(convert_to_rgba(c1), convert_to_rgba(c2));
}

float calc_color_distance(RGBA c1, RGBA c2)
{
    float lab[2][3];
    RGB2Lab(c1[0], c1[1], c1[2], &lab[0][0], &lab[0][1], &lab[0][2]);
    RGB2Lab(c2[0], c2[1], c2[2], &lab[1][0], &lab[1][1], &lab[1][2]);

    const float de = DeltaE76(lab[0][0], lab[0][1], lab[0][2], lab[1][0], lab[1][1], lab[1][2]);
    // Alpha is [0, 1]; scale to CIE L* range [0, 100] so opacity mismatch
    // contributes on the same order as a full lightness swing.
    const float da = (c1[3] - c2[3]) * 100.f;
    return std::sqrt(de * de + da * da);
}

} }
