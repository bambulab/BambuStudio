#ifndef FILAMENT_NOZZLE_POPUP_HPP
#define FILAMENT_NOZZLE_POPUP_HPP

#include <string>
#include <vector>

#include <wx/bitmap.h>

#include "Widgets/PopupWindow.hpp"
#include "libslic3r/PrintConfig.hpp"

class StaticBox;
class wxBoxSizer;

namespace Slic3r { namespace GUI {

// Lightweight transient popup listing every valid (diameter, flow type) combination
// for a filament as a single flat list (e.g. "0.2 标准", "0.4 高流量", ...), with a
// checkmark next to the currently active combination. Clicking an item writes back
// both the diameter and the flow type immediately and dismisses the popup.
// Dismissed by clicking outside it (default wxPopupTransientWindow behavior).
class FilamentNozzlePopup : public PopupWindow
{
public:
    explicit FilamentNozzlePopup(wxWindow *parent);

    // Rebuilds the option list for the given filament and shows the popup.
    // Caller is responsible for positioning it (SetPosition) beforehand.
    void tryPopup(int filament_idx);

private:
    struct Option
    {
        std::string      diameter;
        NozzleVolumeType volume_type;
    };

    void rebuild_options();
    void on_item_clicked(size_t option_idx);

    int m_filament_idx = -1;
    std::vector<Option> m_options;
    int m_selected = -1;

    StaticBox  *m_panel      = nullptr;
    wxBoxSizer *m_list_sizer = nullptr;
    wxBitmap    m_check_bmp;
};

// Re-derive per-plate filament_map / filament_volume_map / filament_nozzle_map for every plate from
// the current project-config state (filament_diameter_map + filament_volume_map) and
// pin filament_map_mode to fmmNozzleManual. Mirrors what FilamentMapDialog's fmmManual apply_to_plate
// path writes so the manual assignment gets persisted into the 3mf metadata. No-op unless the current
// printer is single-extruder multi-nozzle. Call after any user-driven per-slot diameter/flow/preset
// change on such machines.
void apply_nozzle_manual_maps_to_plates();

}} // namespace Slic3r::GUI

#endif
