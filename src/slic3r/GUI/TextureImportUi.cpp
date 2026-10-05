#include "TextureImportUi.hpp"

#include "GUI_App.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/StateColor.hpp"

#include <algorithm>
#include <cmath>

namespace Slic3r {
namespace GUI {

bool texture_import_is_dark()
{
    return wxGetApp().dark_mode();
}

wxColour texture_import_dark_or(const wxColour& light, const wxColour& dark)
{
    return texture_import_is_dark() ? dark : light;
}

std::array<std::size_t, 3> texture_import_rgb_from_rgba(const std::array<float, 4>& rgba)
{
    auto to_u8 = [](float v) -> std::size_t {
        return (std::size_t)std::clamp((int)std::lround(v * 255.f), 0, 255);
    };
    return { to_u8(rgba[0]), to_u8(rgba[1]), to_u8(rgba[2]) };
}

void texture_import_style_primary_button(Button* btn)
{
    if (!btn)
        return;
    StateColor ok_bg(
        std::pair<wxColour, int>(texture_import_dark_or(wxColour(0xC2, 0xC2, 0xC2), wxColour(0x5A, 0x5A, 0x60)), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
    StateColor ok_bd(
        std::pair<wxColour, int>(texture_import_dark_or(wxColour(0xC2, 0xC2, 0xC2), wxColour(0x5A, 0x5A, 0x60)), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
    StateColor ok_text(
        std::pair<wxColour, int>(texture_import_dark_or(wxColour(0xFF, 0xFF, 0xFE), wxColour(0x9A, 0x9A, 0x9E)), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour("#FFFFFE"), StateColor::Normal));
    btn->SetBackgroundColor(ok_bg);
    btn->SetBorderColor(ok_bd);
    btn->SetTextColor(ok_text);
}

void texture_import_style_secondary_button(Button* btn)
{
    if (!btn)
        return;
    StateColor bg(
        std::pair<wxColour, int>(texture_import_dark_or(wxColour(206, 206, 206), wxColour(0x54, 0x54, 0x5B)), StateColor::Pressed),
        std::pair<wxColour, int>(texture_import_dark_or(wxColour(238, 238, 238), wxColour(0x4C, 0x4C, 0x55)), StateColor::Hovered),
        std::pair<wxColour, int>(texture_import_dark_or(wxColour(255, 255, 255), wxColour(0x2D, 0x2D, 0x31)), StateColor::Normal));
    StateColor bd(
        std::pair<wxColour, int>(texture_import_dark_or(wxColour(206, 206, 206), wxColour(0x54, 0x54, 0x5B)), StateColor::Normal));
    StateColor text(
        std::pair<wxColour, int>(texture_import_dark_or(wxColour(50, 58, 61), wxColour(0xEF, 0xEF, 0xF0)), StateColor::Normal));
    btn->SetBackgroundColor(bg);
    btn->SetBorderColor(bd);
    btn->SetTextColor(text);
}

}} // namespace Slic3r::GUI
