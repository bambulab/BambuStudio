#include "ImGuiFilamentWidgets.hpp"

#include "slic3r/GUI/FilamentBitmapUtils.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GuiColor.hpp"

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cmath>

namespace Slic3r {
namespace GUI {
namespace ImGuiFilament {

static ImU32 to_imu32(const wxColour &c)
{
    return IM_COL32(c.Red(), c.Green(), c.Blue(), c.Alpha());
}

ImVec2 default_icon_size()
{
    const float side = ImGui::GetFrameHeight();
    return ImVec2(side, side);
}

static void draw_checkerboard(ImDrawList *draw_list,
                              const ImVec2 &p_min,
                              const ImVec2 &p_max,
                              ImU32 color_light,
                              ImU32 color_dark,
                              float inset)
{
    const float x0 = p_min.x + inset, y0 = p_min.y + inset;
    const float x1 = p_max.x - inset, y1 = p_max.y - inset;
    const float square = std::max(6.f, std::min(x1 - x0, y1 - y0) / 8.f);

    int iy = 0;
    for (float y = y0; y < y1; y += square, ++iy) {
        int ix = 0;
        for (float x = x0; x < x1; x += square, ++ix) {
            const bool  is_light = (ix + iy) % 2 == 0;
            const float xr = std::min(x + square, x1);
            const float yr = std::min(y + square, y1);
            draw_list->AddRectFilled(ImVec2(x, y), ImVec2(xr, yr), is_light ? color_light : color_dark);
        }
    }
}

static void draw_single_color(ImDrawList *draw_list,
                              const ImVec2 &p_min,
                              const ImVec2 &p_max,
                              const wxColour &color)
{
    const bool is_dark_mode = wxGetApp().dark_mode();

    if (color.Alpha() == 0) {
        const float inset = is_dark_mode ? 0.f : 1.f;
        draw_checkerboard(draw_list, p_min, p_max,
                          IM_COL32(255, 255, 255, 255), IM_COL32(217, 217, 217, 255), inset);
        if (!is_dark_mode)
            draw_list->AddRect(p_min, p_max, IM_COL32(130, 130, 128, 255));
        return;
    }

    if (color.Alpha() != wxALPHA_OPAQUE) {
        auto blend = [](wxByte c, double a) -> int {
            return (int) (c * a + 255.0 * (1.0 - a) + 0.5);
        };
        const ImU32 light_clr = IM_COL32(blend(color.Red(), 0.45), blend(color.Green(), 0.45), blend(color.Blue(), 0.45), 255);
        const ImU32 dark_clr  = IM_COL32(blend(color.Red(), 0.70), blend(color.Green(), 0.70), blend(color.Blue(), 0.70), 255);
        draw_checkerboard(draw_list, p_min, p_max, light_clr, dark_clr, 1.f);
        draw_list->AddRect(p_min, p_max, IM_COL32(color.Red(), color.Green(), color.Blue(), 255));
        return;
    }

    draw_list->AddRectFilled(p_min, p_max, IM_COL32(color.Red(), color.Green(), color.Blue(), 255));
    if (!is_dark_mode && color.Red() > 224 && color.Green() > 224 && color.Blue() > 224)
        draw_list->AddRect(p_min, p_max, IM_COL32(130, 130, 128, 255));
    else if (is_dark_mode && color.Red() < 45 && color.Green() < 45 && color.Blue() < 45)
        draw_list->AddRect(p_min, p_max, IM_COL32(207, 207, 207, 255));
}

static void draw_multi_color(ImDrawList *draw_list,
                             const ImVec2 &p_min,
                             const ImVec2 &p_max,
                             const std::vector<wxColour> &colors,
                             bool is_gradient)
{
    const float x0 = p_min.x, y0 = p_min.y, x1 = p_max.x, y1 = p_max.y;
    const size_t n = colors.size();

    if (is_gradient || n > 4) {
        const int   seg_count = (int) n - 1;
        const float seg_w     = (x1 - x0) / (float) seg_count;
        float       left      = x0;
        for (int i = 0; i < seg_count; ++i) {
            const float right = (i == seg_count - 1) ? x1 : left + seg_w;
            const ImU32 c_l = to_imu32(colors[i]);
            const ImU32 c_r = to_imu32(colors[i + 1]);
            draw_list->AddRectFilledMultiColor(ImVec2(left, y0), ImVec2(right, y1), c_l, c_r, c_r, c_l);
            left = right;
        }
    } else if (n == 2) {
        const float xm = std::round(0.5f * (x0 + x1));
        draw_list->AddRectFilled(ImVec2(x0, y0), ImVec2(xm, y1), to_imu32(colors[0]));
        draw_list->AddRectFilled(ImVec2(xm, y0), ImVec2(x1, y1), to_imu32(colors[1]));
    } else if (n == 3) {
        const float w  = (x1 - x0) / 3.f;
        const float xa = x0 + w, xb = x0 + 2.f * w;
        draw_list->AddRectFilled(ImVec2(x0, y0), ImVec2(xa, y1), to_imu32(colors[0]));
        draw_list->AddRectFilled(ImVec2(xa, y0), ImVec2(xb, y1), to_imu32(colors[1]));
        draw_list->AddRectFilled(ImVec2(xb, y0), ImVec2(x1, y1), to_imu32(colors[2]));
    } else {
        const float xm = std::round(0.5f * (x0 + x1));
        const float ym = std::round(0.5f * (y0 + y1));
        draw_list->AddRectFilled(ImVec2(x0, y0), ImVec2(xm, ym), to_imu32(colors[0]));
        draw_list->AddRectFilled(ImVec2(xm, y0), ImVec2(x1, ym), to_imu32(colors[1]));
        draw_list->AddRectFilled(ImVec2(x0, ym), ImVec2(xm, y1), to_imu32(colors[2]));
        draw_list->AddRectFilled(ImVec2(xm, ym), ImVec2(x1, y1), to_imu32(colors[3]));
    }
}

void draw_color_field(ImDrawList *draw_list,
                      const ImVec2 &p_min,
                      const ImVec2 &p_max,
                      const std::vector<wxColour> &colors,
                      bool is_gradient)
{
    if (!draw_list || colors.empty())
        return;
    if (colors.size() == 1)
        draw_single_color(draw_list, p_min, p_max, colors[0]);
    else
        draw_multi_color(draw_list, p_min, p_max, colors, is_gradient);
}

void draw_index_label(ImDrawList *draw_list,
                      const ImVec2 &p_min,
                      const ImVec2 &p_max,
                      const char *label,
                      const wxColour &primary,
                      bool multi_color)
{
    if (!draw_list || !label || !*label)
        return;

    const ImVec2 text_size = ImGui::CalcTextSize(label);
    const ImVec2 text_pos((p_min.x + p_max.x - text_size.x) * 0.5f,
                          (p_min.y + p_max.y - text_size.y) * 0.5f);

    if (multi_color) {
        const ImU32 outline = IM_COL32(0x26, 0x2E, 0x30, 255);
        draw_list->AddText(ImVec2(text_pos.x - 1.f, text_pos.y), outline, label);
        draw_list->AddText(ImVec2(text_pos.x + 1.f, text_pos.y), outline, label);
        draw_list->AddText(ImVec2(text_pos.x, text_pos.y - 1.f), outline, label);
        draw_list->AddText(ImVec2(text_pos.x, text_pos.y + 1.f), outline, label);
        draw_list->AddText(text_pos, IM_COL32_WHITE, label);
        return;
    }

    ImU32 text_col = IM_COL32(0, 0, 0, 255);
    if (primary.Alpha() != 0 && primary.GetLuminance() < 0.51)
        text_col = IM_COL32_WHITE;
    draw_list->AddText(text_pos, text_col, label);
}

void draw_filament_icon(ImDrawList *draw_list,
                        const ImVec2 &p_min,
                        const ImVec2 &p_max,
                        int filament_idx,
                        const char *fallback_hex_color)
{
    std::vector<wxColour> colors;
    bool                  is_gradient = false;
    get_filament_colors_by_id(filament_idx, colors, is_gradient);
    if (colors.empty() && fallback_hex_color && *fallback_hex_color)
        colors.emplace_back(wxColour(fallback_hex_color));
    if (colors.empty())
        colors.emplace_back(wxColour(99, 99, 99));

    draw_color_field(draw_list, p_min, p_max, colors, is_gradient);

    const std::string label = std::to_string(filament_idx + 1);
    draw_index_label(draw_list, p_min, p_max, label.c_str(), colors.front(), colors.size() > 1);
}

bool filament_icon_button(int filament_idx, const ImVec2 &size, bool selected)
{
    FilamentIconButtonOpts opts;
    opts.size     = size;
    opts.selected = selected;
    return filament_icon_button(filament_idx, opts);
}

bool filament_icon_button(int filament_idx, const FilamentIconButtonOpts &opts)
{
    ImVec2 btn_size = opts.size;
    if (btn_size.x <= 0.f || btn_size.y <= 0.f)
        btn_size = default_icon_size();

    if (opts.disabled) {
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
    }

    ImGui::PushID(filament_idx);
    ImGui::InvisibleButton("##filament_icon", btn_size);
    const bool clicked = ImGui::IsItemClicked();
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 r_min = ImGui::GetItemRectMin();
    const ImVec2 r_max = ImGui::GetItemRectMax();
    ImGui::PopID();

    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    draw_filament_icon(draw_list, r_min, r_max, filament_idx, opts.fallback_hex_color);

    if (opts.selected)
        draw_list->AddRect(r_min, r_max, IM_COL32(0, 174, 66, 255), 0.f, 0, 2.f);
    else if (opts.hover_ring && hovered)
        draw_list->AddRect(r_min, r_max, IM_COL32(0, 174, 66, 160), 0.f, 0, 1.f);

    if (opts.disabled) {
        ImGui::PopStyleVar();
        ImGui::PopItemFlag();
    }

    return clicked;
}

bool is_close_to_background(int filament_idx, const ImVec4 &bg, float min_delta_e)
{
    std::vector<wxColour> colors;
    bool                  is_gradient = false;
    get_filament_colors_by_id(filament_idx, colors, is_gradient);
    if (colors.empty())
        return false;

    const RGBA bg_rgba = {bg.x, bg.y, bg.z, bg.w};
    float      min_d   = 1e9f;
    for (const wxColour &c : colors) {
        const RGBA swatch = {c.Red() / 255.f, c.Green() / 255.f, c.Blue() / 255.f, 1.f};
        min_d = std::min(min_d, calc_color_distance(swatch, bg_rgba));
    }
    return min_d < min_delta_e;
}

} // namespace ImGuiFilament
} // namespace GUI
} // namespace Slic3r
