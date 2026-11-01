#ifndef slic3r_ImGuiFilamentWidgets_hpp_
#define slic3r_ImGuiFilamentWidgets_hpp_

#include <imgui/imgui.h>
#include <wx/colour.h>
#include <vector>

namespace Slic3r {
namespace GUI {
namespace ImGuiFilament {

// Default square size matching the Project Filaments sidebar swatch (~2em).
ImVec2 default_icon_size();

// Paint a filament color field (solid / checkerboard / dual / triple / quad / gradient)
// into [p_min, p_max]. Mirrors FilamentBitmapUtils::create_filament_bitmap.
void draw_color_field(ImDrawList *draw_list,
                      const ImVec2 &p_min,
                      const ImVec2 &p_max,
                      const std::vector<wxColour> &colors,
                      bool is_gradient);

// Centered index label. Multi-color uses a white glyph with a dark outline
// (same as get_extruder_color_icon); single color uses luminance contrast.
void draw_index_label(ImDrawList *draw_list,
                      const ImVec2 &p_min,
                      const ImVec2 &p_max,
                      const char *label,
                      const wxColour &primary,
                      bool multi_color);

// Look up project colors for filament_idx (0-based) and draw color + index.
void draw_filament_icon(ImDrawList *draw_list,
                        const ImVec2 &p_min,
                        const ImVec2 &p_max,
                        int filament_idx,
                        const char *fallback_hex_color = nullptr);

struct FilamentIconButtonOpts {
    ImVec2      size{};
    bool        selected            = false;
    bool        disabled            = false;
    bool        hover_ring          = true;
    const char *fallback_hex_color  = nullptr;
};

// Invisible button + draw_filament_icon. Returns true when clicked.
// selected draws the same green ring used by the painting toolbar.
bool filament_icon_button(int filament_idx, const ImVec2 &size, bool selected);
bool filament_icon_button(int filament_idx, const FilamentIconButtonOpts &opts);

// True if any of the filament's colors is too close to bg (CIEDE2000).
bool is_close_to_background(int filament_idx, const ImVec4 &bg, float min_delta_e = 12.f);

} // namespace ImGuiFilament
} // namespace GUI
} // namespace Slic3r

#endif
