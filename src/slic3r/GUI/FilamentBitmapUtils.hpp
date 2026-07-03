#ifndef slic3r_GUI_FilamentBitmapUtils_hpp_
#define slic3r_GUI_FilamentBitmapUtils_hpp_

#include <wx/bitmap.h>
#include <wx/colour.h>
#include <vector>

namespace Slic3r { namespace GUI {

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

/**
 * \brief Look up a filament's full colour set (gradient / dual / multi) from the project config by index
 *
 * \param filament_index    0-based filament id
 * \param out_colors        splitted filament_multi_colour list
 * \param out_is_gradient   true if the color is gradient
 */
void get_filament_colors_by_id(int filament_index, std::vector<wxColour>& out_colors, bool& out_is_gradient);

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_FilamentBitmapUtils_hpp_