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

// Lightweight transient popup listing the printer's currently installed (diameter, flow type)
// nozzles as a single flat list (e.g. "0.2 标准", "0.4 高流量", ...), with a checkmark next to the
// active one. The list mirrors the rack rather than what the slot's filament happens to support,
// so it stays put as the user moves between filaments; entries the filament cannot print with are
// greyed out and ignore clicks. Rebuilt on every popup, so swapping the printer's nozzles is
// picked up straight away. Clicking a selectable item writes back both the diameter and the flow
// type immediately and dismisses the popup.
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
        // False for a nozzle the rack carries but this filament cannot print with: the row is
        // still listed (the list mirrors the printer's nozzles) but greyed out and inert.
        bool             selectable = true;
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
