//**********************************************************/
/* File: wgtRackExtruder.cpp
*  Description: Rack-mode extruder panel.
//**********************************************************/

#include "wgtRackExtruder.h"

#include "slic3r/GUI/DeviceCore/DevDefs.h"          // MAIN_EXTRUDER_ID
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"
#include "slic3r/GUI/Widgets/StateColor.hpp"
#include "slic3r/GUI/Widgets/wgtCircleItem.hpp"
#include "slic3r/GUI/wxExtensions.hpp"        // ScalableButton, create_scaled_bitmap

#include <wx/sizer.h>
#include <wx/statbmp.h>

#include <memory>

wxDEFINE_EVENT(Slic3r::GUI::EVT_RACK_EXTRUDER_E_UP,   wxCommandEvent);
wxDEFINE_EVENT(Slic3r::GUI::EVT_RACK_EXTRUDER_E_DOWN, wxCommandEvent);

namespace Slic3r::GUI {

namespace {

// Layout constants taken from the Figma
constexpr int kPanelWidthDip   = 132;
constexpr int kPanelHeightDip  = 329;
constexpr int kBtnSizeDip      = 40;
constexpr int kCircleSizeDip   = 20;
// Toolhead PNG natural size in Figma is 72x110; the asset lives in
// resources/images/dev_rack_toolhead.png and is scaled by px_height.
constexpr int kToolheadHeightDip = 110;

// Green '#00AE42' is the Bambu accent that the design uses for active hotend.
const wxColour kCircleGreen{ 0x00, 0xAE, 0x42 };
const wxColour kCircleDisabled{ 0xC2, 0xC2, 0xC2 };

bool is_rack_mode(const std::shared_ptr<DevNozzleRack>& rack)
{
    return rack && rack->IsSupported() &&
           (rack->GetRackType() == DevNozzleRack::RackType_A ||
            rack->GetRackType() == DevNozzleRack::RackType_B);
}

// Decode the active hotend index for the user-visible label.
std::optional<int> decode_active_nozzle_label(int nozzle_id)
{
    if (nozzle_id == 0xE) return std::nullopt;
    if (nozzle_id >= 16) return nozzle_id - 16 + 1;
    return nozzle_id + 1;
}

bool is_pen_tool(const DevNozzleToolDefs& tool_defs)
{
    return tool_defs.tool_type == DevNozzleToolType::Cut &&
           tool_defs.cut_info.has_value() &&
           tool_defs.cut_info->type == CutType::Pen;
}

wxString tool_label(const DevNozzleToolDefs& tool_defs)
{
    switch (tool_defs.tool_type) {
    case DevNozzleToolType::Cut:   return is_pen_tool(tool_defs) ? _L("Pen") : _L("Cutter");
    case DevNozzleToolType::Laser: return _L("Laser");
    case DevNozzleToolType::Fan:   return _L("Fan");
    case DevNozzleToolType::Nozzle:
    case DevNozzleToolType::None:
    default:                       return wxEmptyString;
    }
}

wxString toolhead_image_name(const DevNozzleToolDefs& tool_defs)
{
    switch (tool_defs.tool_type) {
    case DevNozzleToolType::Cut: return is_pen_tool(tool_defs) ? "dev_rack_toolhead_pen.png" : "dev_rack_toolhead_cutter.png";
    default:                     return "dev_rack_toolhead.png";
    }
}

} // namespace

wgtRackExtruder::wgtRackExtruder(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    create_ui();
    Hide();
}

void wgtRackExtruder::create_ui()
{
    SetBackgroundColour(*wxWHITE);

    StateColor btn_bg(std::pair<wxColour, int>(wxColour(0xDB, 0xDB, 0xDB), StateColor::Pressed),
                      std::pair<wxColour, int>(wxColour(0xEE, 0xEE, 0xEE), StateColor::Normal));

    m_btn_up = new ScalableButton(this, wxID_ANY, "dev_rack_extruder_up",
                                  wxEmptyString, wxSize(FromDIP(kBtnSizeDip), FromDIP(kBtnSizeDip)),
                                  wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, false, kBtnSizeDip);
    m_btn_up->Bind(wxEVT_ENTER_WINDOW, [this](auto&) { SetCursor(wxCURSOR_HAND); });
    m_btn_up->Bind(wxEVT_LEAVE_WINDOW, [this](auto&) { SetCursor(wxCURSOR_ARROW); });
    m_btn_up->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
        wxCommandEvent forward(EVT_RACK_EXTRUDER_E_UP, GetId());
        forward.SetEventObject(this);
        ProcessEvent(forward);
        e.Skip();
    });

    m_nozzle_idx = new wgtCircleItem(this, wxID_ANY);
    m_nozzle_idx->SetFont(::Label::Body_12);
    m_nozzle_idx->SetMinSize(wxSize(FromDIP(kCircleSizeDip), FromDIP(kCircleSizeDip)));
    m_nozzle_idx->SetMaxSize(wxSize(FromDIP(kCircleSizeDip), FromDIP(kCircleSizeDip)));
    m_nozzle_idx->SetCircleColor(StateColor(std::make_pair(kCircleDisabled, (int) StateColor::Disabled),
                                            std::make_pair(kCircleGreen,    (int) StateColor::Normal)));
    m_nozzle_idx->SetTextColor(StateColor(std::make_pair(*wxWHITE, (int) StateColor::Disabled),
                                          std::make_pair(*wxWHITE, (int) StateColor::Normal)));
    m_nozzle_idx->SetValue(1);

    m_nozzle_diameter_label = new ::Label(this);
    m_nozzle_diameter_label->SetFont(::Label::Body_12);
    m_nozzle_diameter_label->SetBackgroundColour(*wxWHITE);

    m_nozzle_flowtype_label = new ::Label(this);
    m_nozzle_flowtype_label->SetFont(::Label::Body_12);
    m_nozzle_flowtype_label->SetBackgroundColour(*wxWHITE);

    // Toolhead — PNG asset matches the Figma "工具头" instance (72x110).
    // Bambu's create_scaled_bitmap rescales by the longer edge in px, so we
    // pass the natural height to keep the design's aspect ratio.
    m_toolhead_image_name = "dev_rack_toolhead.png";
    wxBitmap toolhead_bmp = create_scaled_bitmap(m_toolhead_image_name.ToStdString(), this,
                                                  FromDIP(kToolheadHeightDip));
    m_toolhead_image = new wxStaticBitmap(this, wxID_ANY, toolhead_bmp);
    m_toolhead_image->SetBackgroundColour(*wxWHITE);

    m_btn_down = new ScalableButton(this, wxID_ANY, "dev_rack_extruder_down",
                                    wxEmptyString, wxSize(FromDIP(kBtnSizeDip), FromDIP(kBtnSizeDip)),
                                    wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, false, kBtnSizeDip);
    m_btn_down->Bind(wxEVT_ENTER_WINDOW, [this](auto&) { SetCursor(wxCURSOR_HAND); });
    m_btn_down->Bind(wxEVT_LEAVE_WINDOW, [this](auto&) { SetCursor(wxCURSOR_ARROW); });
    m_btn_down->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
        wxCommandEvent forward(EVT_RACK_EXTRUDER_E_DOWN, GetId());
        forward.SetEventObject(this);
        ProcessEvent(forward);
        e.Skip();
    });

    auto* main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->AddSpacer(FromDIP(14));
    main_sizer->Add(m_btn_up,                0, wxALIGN_CENTER_HORIZONTAL);
    main_sizer->AddSpacer(FromDIP(12));
    main_sizer->Add(m_nozzle_idx,            0, wxALIGN_CENTER_HORIZONTAL);
    main_sizer->AddSpacer(FromDIP(2));
    main_sizer->Add(m_nozzle_diameter_label, 0, wxALIGN_CENTER_HORIZONTAL);
    main_sizer->Add(m_nozzle_flowtype_label, 0, wxALIGN_CENTER_HORIZONTAL);
    main_sizer->AddSpacer(FromDIP(8));
    main_sizer->Add(m_toolhead_image,        0, wxALIGN_CENTER_HORIZONTAL);
    main_sizer->AddSpacer(FromDIP(12));
    main_sizer->Add(m_btn_down,              0, wxALIGN_CENTER_HORIZONTAL);
    main_sizer->AddStretchSpacer();

    SetSizer(main_sizer);
    SetMinSize(wxSize(FromDIP(kPanelWidthDip), FromDIP(kPanelHeightDip)));
    SetMaxSize(wxSize(FromDIP(kPanelWidthDip), -1));
    Layout();
}

bool wgtRackExtruder::UpdateByMachine(MachineObject* obj)
{
    m_obj = obj;

    auto rack = obj && obj->GetNozzleSystem() ? obj->GetNozzleSystem()->GetNozzleRack()
                                              : std::shared_ptr<DevNozzleRack>();
    const bool show = is_rack_mode(rack);
    if (IsShown() != show) Show(show);

    if (!show) return false;

    update_nozzle_info(obj);
    update_extrude_buttons();
    return true;
}

void wgtRackExtruder::update_nozzle_info(MachineObject* obj)
{
    if (!obj) return;

    auto* extder_system = obj->GetExtderSystem();
    if (!extder_system) return;

    // Only re-layout when a label text or a control's visibility actually changed.
    bool changed = false;

    int  pick_nozzle_pos_id = 0xE;
    const auto current = extder_system->GetCurrentExtder();
    if (current && current->GetPickedNozzleAddrId().has_value()) {
        pick_nozzle_pos_id = current->GetPickedNozzleAddrId().value() + 0x10;
    }

    // Nozzle index (green circle)
    if (auto label = decode_active_nozzle_label(pick_nozzle_pos_id)) {
        m_nozzle_idx->SetValue(*label);
        if (m_nozzle_idx->Show()) {
            changed = true;
        }
    } else {
        if (m_nozzle_idx->Hide()) {
            changed = true;
        }
    }

    // Resolve the target tool label + visibility once.
    wxString diameter_label, flowtype_label;
    bool     diameter_show = false;
    auto ext_nozzle = obj->GetNozzleSystem()->GetExtNozzle(MAIN_EXTRUDER_ID);
    auto tool_defs = ext_nozzle.IsEmpty() ? DevNozzleToolDefs() : ext_nozzle.GetToolDefs();
    if (tool_defs.tool_type == DevNozzleToolType::Nozzle) {
        if (!ext_nozzle.IsEmpty() && ext_nozzle.IsNormal()) {
            diameter_label = ext_nozzle.GetNozzleDiameterStr();
            flowtype_label = ext_nozzle.GetNozzleFlowTypeStr();
            diameter_show  = true;
        } else if (!ext_nozzle.IsEmpty() && ext_nozzle.IsAbnormal()) {
            flowtype_label = _L("Error");
        } else if (!ext_nozzle.IsEmpty() && ext_nozzle.IsUnknown()) {
            flowtype_label = _L("Unknown");
        } else {
            flowtype_label = _L("Unclamped");
        }
    } else if (!ext_nozzle.IsEmpty() && ext_nozzle.IsAbnormal()) {
        flowtype_label = _L("Error");
    } else if (!ext_nozzle.IsEmpty() && ext_nozzle.IsUnknown()) {
        flowtype_label = _L("Unknown");
    } else if (!tool_label(tool_defs).empty()) {
        flowtype_label = tool_label(tool_defs);
    } else {
        flowtype_label = _L("Unclamped");
    }

    if (diameter_show) {
        if (m_nozzle_diameter_label->GetLabel() != diameter_label) {
            m_nozzle_diameter_label->SetLabel(diameter_label);
            changed = true;
        }
        if (m_nozzle_diameter_label->Show()) {
            changed = true;
        }
    } else {
        if (m_nozzle_diameter_label->Hide()) {
            changed = true;
        }
    }

    if (m_nozzle_flowtype_label->GetLabel() != flowtype_label) {
        m_nozzle_flowtype_label->SetLabel(flowtype_label);
        changed = true;
    }
    if (m_nozzle_flowtype_label->Show()) {
        changed = true;
    }

    // Toolhead PNG: only re-apply and repaint when its resource path changes.
    const wxString image_name = toolhead_image_name(tool_defs);
    if (m_toolhead_image && image_name != m_toolhead_image_name) {
        m_toolhead_image->SetBitmap(create_scaled_bitmap(image_name.ToStdString(), this, FromDIP(kToolheadHeightDip)));
        m_toolhead_image_name = image_name;
        m_toolhead_image->Refresh();
    }

    if (changed) {
        Layout();
    }
}

void wgtRackExtruder::SetControlEnabled(bool enabled)
{
    if (m_control_enabled == enabled) return;
    m_control_enabled = enabled;
    update_extrude_buttons();
}

void wgtRackExtruder::update_extrude_buttons()
{
    if (m_btn_up) {
        m_btn_up->SetBitmap_(m_control_enabled ? "dev_rack_extruder_up" : "dev_rack_extruder_up_disable");
        m_btn_up->Enable(m_control_enabled);
    }
    if (m_btn_down) {
        m_btn_down->SetBitmap_(m_control_enabled ? "dev_rack_extruder_down" : "dev_rack_extruder_down_disable");
        m_btn_down->Enable(m_control_enabled);
    }
}

void wgtRackExtruder::Rescale()
{
    if (m_btn_up)   m_btn_up->msw_rescale();
    if (m_btn_down) m_btn_down->msw_rescale();
    if (m_toolhead_image) {
        wxBitmap toolhead_bmp = create_scaled_bitmap(m_toolhead_image_name.ToStdString(), this,
                                                      FromDIP(kToolheadHeightDip));
        m_toolhead_image->SetBitmap(toolhead_bmp);
    }
    Layout();
    Refresh();
}

} // namespace Slic3r::GUI
