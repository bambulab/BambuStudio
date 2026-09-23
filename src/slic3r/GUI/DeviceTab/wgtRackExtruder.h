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

#include <wx/bitmap.h>
#include <wx/colour.h>
#include <wx/panel.h>
#include <wx/event.h>

#include <optional>

class wxGraphicsContext;
class wxSizerItem;
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

// The filament path drawn over the toolhead art. Above the extruder gear the tube
// carries whatever the AMS is feeding in ("slot now"), below it whatever already
// sits in the nozzle, so a color change is visible while it is being pushed down.
struct RackToolheadFilament
{
    bool visible{ false };   // false keeps the tube unpainted, e.g. hotend without filament

    std::optional<wxColour> upper;
    std::optional<wxColour> lower;

    bool arrow_down{ false };// extruding
    bool arrow_up{ false };  // retracting

    bool operator==(const RackToolheadFilament& o) const
    {
        return visible == o.visible && upper == o.upper && lower == o.lower &&
               arrow_down == o.arrow_down && arrow_up == o.arrow_up;
    }
    bool operator!=(const RackToolheadFilament& o) const { return !(*this == o); }
};

// Toolhead art plus the live-colored filament path on top of it. The art keeps the
// design's placeholder tube; the overlay covers it completely whenever it is drawn.
class wgtRackToolhead : public wxPanel
{
public:
    wgtRackToolhead(wxWindow* parent, const wxString& image_name, int art_height_dip);

    void SetImage(const wxString& image_name);
    void SetFilament(const RackToolheadFilament& filament);
    void Rescale();

private:
    void reload_bitmap();
    void on_paint(wxPaintEvent&);
    void draw_filament_path(wxGraphicsContext& gc, double scale, double offset_x) const;
    void draw_flow_arrows(wxGraphicsContext& gc, double scale, double offset_x) const;

    const int            m_art_height_dip;
    wxString             m_image_name;
    wxBitmap             m_bitmap;
    RackToolheadFilament m_filament;
};

class wgtRackExtruder : public wxPanel
{
public:
    explicit wgtRackExtruder(wxWindow* parent);
    ~wgtRackExtruder() override = default;

    // Refresh visibility / data based on the current MachineObject.
    // Returns true when the panel should be visible for this machine
    bool UpdateByMachine(Slic3r::MachineObject* obj);

    // Mirrors StatusPanel::show_printing_status(ctrl_area).
    // Disables the up / down buttons and swaps the disable icons.
    void SetControlEnabled(bool enabled);

    void Rescale();

private:
    void create_ui();
    void apply_nozzle_info_height();
    void update_extrude_buttons();
    void update_nozzle_info(Slic3r::MachineObject* obj);

private:
    Slic3r::MachineObject* m_obj{ nullptr };
    bool                   m_control_enabled{ true };

    // GUI
    ScalableButton* m_btn_up{ nullptr };
    ScalableButton* m_btn_down{ nullptr };
    // Constant-height holder for the controls above the toolhead art, so that hiding
    // any of them cannot move the art.
    wxPanel*        m_nozzle_info_panel{ nullptr };
    wxSizerItem*    m_nozzle_info_strut{ nullptr };
    wgtCircleItem*  m_nozzle_idx{ nullptr };
    Label*           m_nozzle_diameter_label{ nullptr };
    Label*           m_nozzle_flowtype_label{ nullptr };
    wgtRackToolhead* m_toolhead_image{ nullptr };
};

} // namespace Slic3r::GUI
