#include "AmsSlotPopup.hpp"

#include <optional>
#include <vector>

#include <wx/colour.h>
#include <wx/tokenzr.h>

#include "slic3r/GUI/AmsMappingPopup.hpp"     // AmsMapingPopup, EVT_SET_FINISH_MAPPING
#include "slic3r/GUI/GUI_App.hpp"             // wxGetApp().getDeviceManager()
#include "slic3r/GUI/DeviceManager.hpp"       // MachineObject
#include "slic3r/GUI/DeviceCore/DevManager.h" // DeviceManager::get_selected_machine
#include "slic3r/GUI/DeviceCore/DevMapping.h" // DevMappingUtil::enumerate_dest_slots

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

/**
 * \brief How the picker lays out the offered slots: both nozzles' slots on a two-nozzle printer (one shared
 *        list behind a filament selector), otherwise the single nozzle's.
 */
ShowType show_type_for(const SyncSlots &slots)
{
    if (!slots.dual_nozzle) return ShowType::RIGHT;
    return slots.selector_feeds_both ? ShowType::LEFT_AND_RIGHT_DYNAMIC : ShowType::LEFT_AND_RIGHT;
}

} // namespace

AmsSlotPopup::AmsSlotPopup(wxWindow *parent) : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxSize(0, 0))
{
    Hide(); // a controller / event-sink only; the wrapped popup is the visible window
    Bind(EVT_SET_FINISH_MAPPING, &AmsSlotPopup::on_finish_mapping, this);
}

void AmsSlotPopup::set_state(const SlotSelectionState &state) { m_state = state; }

void AmsSlotPopup::ensure_impl()
{
    if (!m_impl) {
        m_impl = new AmsMapingPopup(GetParent(), /*use_in_sync_dialog=*/true);
        m_impl->show_reset_button();
        m_impl->set_reset_callback([this](const std::string &) {
            if (on_reset) on_reset();
            if (m_impl) m_impl->Dismiss();
        });
        // Built after the dialog's own dark pass, so it needs its own, as the send page's popup gets from its dialog.
        wxGetApp().UpdateDarkUIWin(m_impl);
    }
    m_impl->set_send_win(this);
}

void AmsSlotPopup::popup_at(wxWindow *anchor)
{
    if (!anchor) return;

    auto          *dm  = wxGetApp().getDeviceManager();
    MachineObject *obj = dm ? dm->get_selected_machine() : nullptr;
    if (!obj) return;

    ensure_impl();
    const ShowType show_type = show_type_for(m_state.slots);
    m_impl->set_show_type(show_type);
    m_impl->set_current_filament_id(m_state.source_id);
    m_impl->set_tag_texture(m_state.source_type);
    m_impl->set_match_all(m_state.match_all);
    m_impl->set_anchor(anchor); // before update(): the popup sizes, places and pre-checks against it
    // The whole mapping lets the picker mark the source's current slot and see what the other sources took.
    m_impl->update(obj, m_state.mapping, /*use_dynamic_switch=*/show_type == ShowType::LEFT_AND_RIGHT_DYNAMIC, std::nullopt);
    m_impl->Popup();
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
