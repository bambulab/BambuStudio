//**********************************************************/
/* File: wgtRackExtruder.h
*  Description: 
*               extruder panel.
*               Shows the active nozzle index, nozzle diameter / flow type,
*               toolhead image, and the up / down extrude buttons.
*               The "no extruding while printing" gate is reused from the
*               legacy E-axis control via forwarded events.
//**********************************************************/

#pragma once

#include <wx/panel.h>
#include <wx/event.h>

class wxStaticBitmap;
class wgtCircleItem;
class ScalableButton;
class Label;

namespace Slic3r {
class MachineObject;
} // namespace Slic3r

namespace Slic3r::GUI {

// Forwarded user-clicks. StatusPanel reuses on_axis_ctrl_e_up_10 / on_axis_ctrl_e_down_10.
wxDECLARE_EVENT(EVT_RACK_EXTRUDER_E_UP,   wxCommandEvent);
wxDECLARE_EVENT(EVT_RACK_EXTRUDER_E_DOWN, wxCommandEvent);

class wgtRackExtruder : public wxPanel
{
public:
    explicit wgtRackExtruder(wxWindow* parent);
    ~wgtRackExtruder() override = default;

    // Refresh visibility / data based on the current MachineObject.
    // Returns true when the panel should be visible for this machine
    // (i.e. RackType_A or RackType_B).
    bool UpdateByMachine(Slic3r::MachineObject* obj);

    // Mirrors StatusPanel::show_printing_status(ctrl_area).
    // Disables the up / down buttons and swaps the disable icons.
    void SetControlEnabled(bool enabled);

    void Rescale();

private:
    void create_ui();
    void update_extrude_buttons();
    void update_nozzle_info(Slic3r::MachineObject* obj);

private:
    Slic3r::MachineObject* m_obj{ nullptr };
    bool                   m_control_enabled{ true };

    // GUI
    ScalableButton* m_btn_up{ nullptr };
    ScalableButton* m_btn_down{ nullptr };
    wgtCircleItem*  m_nozzle_idx{ nullptr };
    Label*          m_nozzle_diameter_label{ nullptr };
    Label*          m_nozzle_flowtype_label{ nullptr };
    wxStaticBitmap* m_toolhead_image{ nullptr };
    wxString        m_toolhead_image_name;
};

} // namespace Slic3r::GUI
