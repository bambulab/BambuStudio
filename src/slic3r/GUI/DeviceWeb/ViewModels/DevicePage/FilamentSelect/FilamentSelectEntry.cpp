/*****************************************************************//**
 * \file   FilamentSelectEntry.cpp
 * \brief  Chooses between the Web and the native Select Filament dialog.
 *********************************************************************/
#include "FilamentSelectEntry.hpp"

#include "FilamentSelectWebEnable.hpp"
#include "PageFilamentSelect.hpp"

#include "slic3r/GUI/FilamentSelectDialog.hpp"

#include <wx/dialog.h>
#include <wx/display.h>

namespace Slic3r { namespace GUI {

namespace {

/// Offsets the dialog from its parent so both stay visible, clamped to the
/// work area of the parent's display.
void place_cascaded(wxDialog& dlg, wxWindow* parent)
{
    if (!parent) return;

    const int     cascade  = parent->FromDIP(20);
    const wxPoint base_pos = parent->GetScreenPosition();
    const wxSize  dlg_size = dlg.GetSize();
    const int     display  = wxDisplay::GetFromWindow(parent);
    const wxRect  area     = wxDisplay(display == wxNOT_FOUND ? 0 : display).GetClientArea();
    int pos_x = base_pos.x + cascade;
    int pos_y = base_pos.y + cascade;
    if (pos_x + dlg_size.GetWidth()  > area.GetRight())  pos_x = area.GetRight()  - dlg_size.GetWidth();
    if (pos_y + dlg_size.GetHeight() > area.GetBottom()) pos_y = area.GetBottom() - dlg_size.GetHeight();
    if (pos_x < area.GetLeft()) pos_x = area.GetLeft();
    if (pos_y < area.GetTop())  pos_y = area.GetTop();
    dlg.Move(wxPoint(pos_x, pos_y));
}

} // namespace

FilamentSelectResult show_filament_select_dialog(wxWindow* parent, const FilamentSelectInput& in)
{
    if (filament_select_use_web()) {
        return run_filament_select_web_dialog(parent, in,
                                              [parent](wxDialog& dlg) { place_cascaded(dlg, parent); });
    }

    FilamentSelectDialog dlg(parent);
    place_cascaded(dlg, parent);
    dlg.Popup(in.filament_items, in.vendors, in.types, in.current_alias, in.printer_names);
    return dlg.get_result();
}

}} // namespace Slic3r::GUI
