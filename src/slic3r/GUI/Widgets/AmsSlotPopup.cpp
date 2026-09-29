#include "AmsSlotPopup.hpp"

#include <optional>
#include <vector>

#include <wx/colour.h>
#include <wx/display.h>
#include <wx/tokenzr.h>

#include "slic3r/GUI/AmsMappingPopup.hpp"     // AmsMapingPopup, EVT_SET_FINISH_MAPPING
#include "slic3r/GUI/GUI_App.hpp"             // wxGetApp().getDeviceManager()
#include "slic3r/GUI/DeviceManager.hpp"       // MachineObject
#include "slic3r/GUI/DeviceCore/DevManager.h" // DeviceManager::get_selected_machine
#include "slic3r/GUI/DeviceCore/DevMapping.h" // DevMappingUtil::enumerate_dest_slots
#include "libslic3r/ProjectTask.hpp"          // FilamentInfo

namespace Slic3r::GUI {

namespace {

/**
 * \brief A colour in FilamentInfo's "#RRGGBBAA" form, so a manual pick reads the same as an auto-mapped one.
 *
 * \param c  the colour to format
 */
std::string color_to_hex(const wxColour &c)
{
    return wxString::Format("#%02X%02X%02X%02X", c.Red(), c.Green(), c.Blue(), c.Alpha()).ToStdString();
}

} // namespace

AmsSlotPopup::AmsSlotPopup(wxWindow *parent) : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxSize(0, 0))
{
    Hide(); // a controller / event-sink only; the wrapped popup is the visible window
    Bind(EVT_SET_FINISH_MAPPING, &AmsSlotPopup::on_finish_mapping, this);
}

void AmsSlotPopup::set_state(const SlotSelectionState &state) { m_state = state; }

void AmsSlotPopup::set_current_slot(const FilamentInfo &current)
{
    m_current_ams  = current.ams_id;
    m_current_slot = current.slot_id;
}

void AmsSlotPopup::ensure_impl()
{
    if (!m_impl) m_impl = new AmsMapingPopup(GetParent(), /*use_in_sync_dialog=*/true);
    m_impl->set_send_win(this);
}

void AmsSlotPopup::popup_at(wxWindow *anchor)
{
    if (!anchor) return;

    auto          *dm  = wxGetApp().getDeviceManager();
    MachineObject *obj = dm ? dm->get_selected_machine() : nullptr;
    if (!obj) return;

    ensure_impl();
    m_impl->set_current_filament_id(m_state.source_id);
    m_impl->set_tag_texture(m_state.source_type);
    m_impl->set_match_all(m_state.match_all);

    // Hand the popup the current destination as a one-entry result so it can pre-check that slot. The
    // check only lights up when a parent MaterialItem is set, which this seam has none of — the dest
    // highlight is carried by the row instead, so its absence inside the popup is cosmetic.
    std::vector<FilamentInfo> result;
    if (!m_current_ams.empty()) {
        FilamentInfo cur;
        cur.id      = m_state.source_id;
        cur.ams_id  = m_current_ams;
        cur.slot_id = m_current_slot;
        result.push_back(cur);
    }
    m_impl->update(obj, result, /*use_dynamic_switch=*/false, std::nullopt);

    place_under(anchor);
    m_impl->Popup();
}

void AmsSlotPopup::place_under(wxWindow *anchor)
{
    const wxSize  sz        = m_impl->GetSize(); // update() ends in Fit(), so the size is final here
    const wxPoint anchor_tl = anchor->ClientToScreen(wxPoint(0, 0));
    wxPoint       pos       = anchor_tl;
    pos.y += anchor->GetSize().GetHeight(); // prefer just below the anchor cell

    int disp = wxDisplay::GetFromPoint(anchor_tl);
    if (disp == wxNOT_FOUND) disp = 0;
    const wxRect area = wxDisplay(disp).GetClientArea();
    if (pos.y + sz.y > area.GetBottom()) pos.y = anchor_tl.y - sz.y; // no room below: flip above
    if (pos.x + sz.x > area.GetRight()) pos.x = area.GetRight() - sz.x;
    if (pos.x < area.GetLeft()) pos.x = area.GetLeft();
    if (pos.y < area.GetTop()) pos.y = area.GetTop();
    m_impl->Move(pos);
}

void AmsSlotPopup::on_finish_mapping(wxCommandEvent &evt)
{
    const wxArrayString parts = wxSplit(evt.GetString(), '|');
    if (parts.size() != 8) return;

    FilamentInfo chosen;
    chosen.tray_id = evt.GetInt();
    chosen.ams_id  = parts[6].ToStdString();
    chosen.slot_id = parts[7].ToStdString();
    const wxColour col(wxAtoi(parts[0]), wxAtoi(parts[1]), wxAtoi(parts[2]), wxAtoi(parts[3]));
    chosen.color = color_to_hex(col);

    // The event carries only the clicked slot's identity and display colour. Recover the rest of the
    // filament identity (type, preset id, colour ramp, gradient ctype, nozzle geometry) from the machine's
    // live slots — the same canonical source auto-map uses — joined on (ams, slot).
    auto *dm = wxGetApp().getDeviceManager();
    if (MachineObject *obj = dm ? dm->get_selected_machine() : nullptr) {
        for (const FilamentInfo &slot : DevMappingUtil::enumerate_dest_slots(obj)) {
            if (slot.ams_id != chosen.ams_id || slot.slot_id != chosen.slot_id) continue;
            chosen.type               = slot.type;
            chosen.filament_id        = slot.filament_id;
            chosen.setting_id         = slot.setting_id;
            chosen.ctype              = slot.ctype;
            chosen.colors             = slot.colors;
            chosen.nozzle_diameter    = slot.nozzle_diameter;
            chosen.nozzle_volume_type = slot.nozzle_volume_type;
            break;
        }
    }
    if (chosen.colors.empty()) chosen.colors.push_back(chosen.color);

    if (on_pick) on_pick(chosen);
    if (m_impl) m_impl->Dismiss();
}

} // namespace Slic3r::GUI
