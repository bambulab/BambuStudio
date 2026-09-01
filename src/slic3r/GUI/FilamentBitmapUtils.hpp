#ifndef slic3r_GUI_FilamentBitmapUtils_hpp_
#define slic3r_GUI_FilamentBitmapUtils_hpp_

#include <wx/bitmap.h>
#include <wx/colour.h>
#include <wx/dc.h>
#include <wx/gdicmn.h>
#include <vector>

#include "libslic3r/PrintConfig.hpp"

namespace Slic3r { namespace GUI {

// Fills a rect with a west->east linear gradient by drawing solid 1px columns.
// Use instead of wxDC::GradientFillLinear, whose CoreGraphics (CGShading) backend
// fails to render on some macOS builds; solid fills are unaffected.
void fill_gradient_rect_east(wxDC& dc, const wxRect& rect, const wxColour& from, const wxColour& to);

enum class FilamentRenderMode {
    Single,
    Dual,
    Triple,
    Quadruple,
    Gradient
};

// Create a colour swatch bitmap. The render mode is chosen automatically from the
// number of colours unless force_gradient is true.
wxBitmap create_filament_bitmap(const std::vector<wxColour>& colors,
                              const wxSize& size,
                              bool force_gradient = false);

void get_translucent_checker_colors(const wxColour& color, wxColour& light_out, wxColour& dark_out);

wxBitmap create_translucent_circle_bitmap(const wxColour& color, int diameter, int border_width = 1);

wxBitmap create_translucent_round_rect_bitmap(const wxColour& color, const wxSize& size, double radius);

/**
 * \brief Look up a filament's full colour set (gradient / dual / multi) from the project config by index
 *
 * \param filament_index    0-based filament id
 * \param out_colors        splitted filament_multi_colour list
 * \param out_is_gradient   true if the color is gradient
 */
void get_filament_colors_by_id(int filament_index, std::vector<wxColour>& out_colors, bool& out_is_gradient);

// Recompute blended representative colors for mixed (virtual) filament slots.
// Reads mixed-filament config keys from cfg and writes back into colors[i]
// for every slot where filament_is_mixed[i] is true.
void recompute_mixed_slot_colors(std::vector<wxColour>& colors,
                                 const Slic3r::DynamicPrintConfig& cfg);

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_FilamentBitmapUtils_hpp_