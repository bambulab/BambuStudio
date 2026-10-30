//**********************************************************/
/* File: wgtRackExtruder.cpp
*  Description: Rack-mode extruder panel.
//**********************************************************/

#include "wgtRackExtruder.h"

#include "slic3r/GUI/DeviceCore/DevDefs.h"          // MAIN_EXTRUDER_ID
#include "slic3r/GUI/DeviceCore/DevConfigUtil.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"     // DevAmsTray::decode_color
#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"
#include "slic3r/GUI/Widgets/StateColor.hpp"
#include "slic3r/GUI/Widgets/wgtCircleItem.hpp"
#include "slic3r/GUI/wxExtensions.hpp"        // ScalableButton, create_scaled_bitmap

#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/sizer.h>

#include <cmath>
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
// Gap between the index circle and the text lines below it.
constexpr int kInfoGapDip      = 2;
// Toolhead PNG natural size in Figma is 72x110; the asset lives in
// resources/images/dev_rack_toolhead.png and is scaled by px_height.
constexpr int kToolheadHeightDip = 110;

// Filament tube geometry, expressed in the toolhead art's own 72x110 pixel space so
// it scales with the bitmap. Matches the "进料料管" layers in the Figma states: the art
// only carries the empty dark sleeve, the strand inside it is painted from live data.
constexpr double kArtHeight   = 110.0;
constexpr double kTubeSleeveX = 33.0;
constexpr double kTubeSleeveW = 8.0;
constexpr double kTubeStrandX = 35.0;
constexpr double kTubeStrandW = 4.0;
constexpr double kTubeTop     = 2.0;
constexpr double kTubeBottom  = 101.0;
// Extruder gear: the AMS feed is above it, what is already in the nozzle below it.
constexpr double kTubeSplitY  = 59.0;
constexpr double kArrowWidth  = 8.0;
constexpr double kArrowHeight = 8.0;
constexpr double kArrowPitch  = 12.5;

// Green '#00AE42' is the Bambu accent that the design uses for active hotend.
const wxColour kCircleGreen{ 0x00, 0xAE, 0x42 };
const wxColour kCircleDisabled{ 0xC2, 0xC2, 0xC2 };

bool is_rack_mode(const std::shared_ptr<DevNozzleRack>& rack)
{
    return rack && rack->IsSupported() && rack->IsDirectConnectedRack();
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

wxString rack_extruder_image_name(MachineObject* obj, const std::optional<DevExtder>& current, const DevNozzleToolDefs& tool_defs)
{
    if (tool_defs.tool_type != DevNozzleToolType::Nozzle &&
        tool_defs.tool_type != DevNozzleToolType::None) {
        return toolhead_image_name(tool_defs);
    }

    if (!obj || !current) {
        return obj ? DevPrinterConfigUtil::get_rack_extruder_img(obj->printer_type, DevRackExtruderVisualState::Empty) : "dev_rack_toolhead.png";
    }

    return DevPrinterConfigUtil::get_rack_extruder_img(obj->printer_type, resolve_rack_extruder_visual_state(obj, *current));
}

// Color of the AMS slot the extruder is currently fed from ("snow" / tray_now).
std::optional<wxColour> feeding_slot_color(MachineObject* obj, const std::optional<DevExtder>& current)
{
    if (!obj || !current) return std::nullopt;

    const DevAmsSlotInfo& slot = current->GetSlotNow();
    if (slot.ams_id.empty() || slot.slot_id.empty()) return std::nullopt;
    if (!obj->contains_tray(slot.ams_id, slot.slot_id)) return std::nullopt;

    const auto tray = obj->get_tray(slot.ams_id, slot.slot_id);
    if (!tray || tray->color.empty()) return std::nullopt;// decode_color() would log and fall back to white
    return tray->get_color();
}

RackToolheadFilament resolve_filament_path(
    MachineObject* obj, const std::optional<DevExtder>& current,
    const DevNozzle& ext_nozzle, const DevNozzleToolDefs& tool_defs, bool has_installed_tool)
{
    RackToolheadFilament path;

    // Only a clamped hotend that actually holds filament shows a path; the art of the
    // other states has no tube to fill in.
    if (!current) return path;
    if (!has_installed_tool) return path;
    if (tool_defs.tool_type != DevNozzleToolType::Nozzle &&
        tool_defs.tool_type != DevNozzleToolType::None) {
        return path;
    }

    path.visible = true;

    const auto feeding_color = feeding_slot_color(obj, current);

    std::optional<wxColour> nozzle_color;
    if (const std::string clr = ext_nozzle.GetFilamentColor(); !clr.empty()) {
        nozzle_color = DevAmsTray::decode_color(clr);
    }

    // Keep the path continuous when only one end reports a color.
    path.upper = feeding_color ? feeding_color : nozzle_color;
    path.lower = nozzle_color ? nozzle_color : feeding_color;

    const auto direction = current->GetExtrusionDirection();
    path.arrow_down = (direction == DevExtrusionDirection::Extruding);
    path.arrow_up   = (direction == DevExtrusionDirection::Retracting);
    return path;
}

} // namespace

wgtRackToolhead::wgtRackToolhead(wxWindow* parent, const wxString& image_name, int art_height_dip)
    : wxPanel(parent, wxID_ANY)
    , m_art_height_dip(art_height_dip)
    , m_image_name(image_name)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(*wxWHITE);
    reload_bitmap();
    Bind(wxEVT_PAINT, &wgtRackToolhead::on_paint, this);
}

void wgtRackToolhead::reload_bitmap()
{
    m_bitmap = create_scaled_bitmap(m_image_name.ToStdString(), this, m_art_height_dip);
    if (m_bitmap.IsOk()) {
        const wxSize size(m_bitmap.GetWidth(), m_bitmap.GetHeight());
        SetMinSize(size);
        SetMaxSize(size);
    }
}

void wgtRackToolhead::SetImage(const wxString& image_name)
{
    if (m_image_name == image_name) return;
    m_image_name = image_name;
    reload_bitmap();
    Refresh();
}

void wgtRackToolhead::SetFilament(const RackToolheadFilament& filament)
{
    if (m_filament == filament) return;
    m_filament = filament;
    Refresh();
}

void wgtRackToolhead::Rescale()
{
    reload_bitmap();
    Refresh();
}

void wgtRackToolhead::on_paint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(GetBackgroundColour()));
    dc.Clear();

    if (!m_bitmap.IsOk()) return;

    const double offset_x = (GetClientSize().x - m_bitmap.GetWidth()) / 2.0;
    dc.DrawBitmap(m_bitmap, static_cast<int>(offset_x), 0, true);

    if (!m_filament.visible) return;

    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (!gc) return;
    gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);
    draw_filament_path(*gc, m_bitmap.GetHeight() / kArtHeight, offset_x);
}

void wgtRackToolhead::draw_filament_path(wxGraphicsContext& gc, double scale, double offset_x) const
{
    const auto art_x = [&](double x) { return offset_x + x * scale; };

    gc.SetPen(*wxTRANSPARENT_PEN);

    // Snapped to whole device pixels so the strand stays inside the sleeve the art
    // already draws, instead of leaving a seam against it at fractional DPI scales.
    const double x = std::round(art_x(kTubeStrandX));
    const double w = std::round(art_x(kTubeStrandX + kTubeStrandW)) - x;

    const auto draw_strand = [&](const std::optional<wxColour>& color, double top, double bottom) {
        if (!color) return;
        const double y = std::round(top * scale);
        gc.SetBrush(wxBrush(*color));
        gc.DrawRectangle(x, y, w, std::round(bottom * scale) - y);
    };
    draw_strand(m_filament.upper, kTubeTop, kTubeSplitY);
    draw_strand(m_filament.lower, kTubeSplitY, kTubeBottom);

    if (m_filament.arrow_down || m_filament.arrow_up) {
        draw_flow_arrows(gc, scale, offset_x);
    }
}

void wgtRackToolhead::draw_flow_arrows(wxGraphicsContext& gc, double scale, double offset_x) const
{
    const auto art_x = [&](double x) { return offset_x + x * scale; };

    const bool   down      = m_filament.arrow_down;
    const double center_x  = kTubeSleeveX + kTubeSleeveW / 2.0;
    const wxColour clear_white{ 255, 255, 255, 0 };
    const wxColour solid_white{ 255, 255, 255, 255 };

    // Stay inside the sleeve so the arrows never spill over the toolhead body.
    gc.Clip(art_x(kTubeSleeveX), kTubeTop * scale,
            kTubeSleeveW * scale, (kTubeBottom - kTubeTop) * scale);

    for (double band = kTubeTop; band + kArrowHeight <= kTubeBottom; band += kArrowPitch) {
        const double base = down ? band : band + kArrowHeight;
        const double tip  = down ? band + kArrowHeight : band;

        wxGraphicsPath path = gc.CreatePath();
        path.MoveToPoint(art_x(center_x - kArrowWidth / 2.0), base * scale);
        path.AddLineToPoint(art_x(center_x + kArrowWidth / 2.0), base * scale);
        path.AddLineToPoint(art_x(center_x), tip * scale);
        path.CloseSubpath();

        // Fades out towards the tail, so the bright tip reads as the flow direction.
        gc.SetBrush(gc.CreateLinearGradientBrush(
            art_x(center_x), base * scale, art_x(center_x), tip * scale, clear_white, solid_white));
        gc.FillPath(path);
    }

    gc.ResetClip();
}

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

    m_nozzle_info_panel = new wxPanel(this, wxID_ANY);
    m_nozzle_info_panel->SetBackgroundColour(*wxWHITE);

    m_nozzle_idx = new wgtCircleItem(m_nozzle_info_panel, wxID_ANY);
    m_nozzle_idx->SetFont(::Label::Body_12);
    m_nozzle_idx->SetMinSize(wxSize(FromDIP(kCircleSizeDip), FromDIP(kCircleSizeDip)));
    m_nozzle_idx->SetMaxSize(wxSize(FromDIP(kCircleSizeDip), FromDIP(kCircleSizeDip)));
    m_nozzle_idx->SetCircleColor(StateColor(std::make_pair(kCircleDisabled, (int) StateColor::Disabled),
                                            std::make_pair(kCircleGreen,    (int) StateColor::Normal)));
    m_nozzle_idx->SetTextColor(StateColor(std::make_pair(*wxWHITE, (int) StateColor::Disabled),
                                          std::make_pair(*wxWHITE, (int) StateColor::Normal)));
    m_nozzle_idx->SetValue(1);

    m_nozzle_diameter_label = new ::Label(m_nozzle_info_panel);
    m_nozzle_diameter_label->SetFont(::Label::Body_12);
    m_nozzle_diameter_label->SetBackgroundColour(*wxWHITE);

    m_nozzle_flowtype_label = new ::Label(m_nozzle_info_panel);
    m_nozzle_flowtype_label->SetFont(::Label::Body_12);
    m_nozzle_flowtype_label->SetBackgroundColour(*wxWHITE);

    auto* info_column = new wxBoxSizer(wxVERTICAL);
    // Stretch spacers on both ends share out whatever the hidden controls left behind: the
    // tallest state fills the panel exactly and collapses both to zero, while a state that
    // only carries the flow type ends up centered. The gap belongs to the circle so that it
    // disappears together with it.
    info_column->AddStretchSpacer();
    info_column->Add(m_nozzle_idx,            0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, FromDIP(kInfoGapDip));
    info_column->Add(m_nozzle_diameter_label, 0, wxALIGN_CENTER_HORIZONTAL);
    info_column->Add(m_nozzle_flowtype_label, 0, wxALIGN_CENTER_HORIZONTAL);
    info_column->AddStretchSpacer();

    auto* info_sizer = new wxBoxSizer(wxHORIZONTAL);
    // Zero-width strut carrying the panel's height. Without it the panel's best size is
    // the column's minimum, which shrinks as soon as a state hides the circle or the
    // diameter line, and the toolhead art below moves up with it.
    m_nozzle_info_strut = info_sizer->Add(0, 0);
    info_sizer->Add(info_column, 1, wxEXPAND);
    m_nozzle_info_panel->SetSizer(info_sizer);
    apply_nozzle_info_height();

    // Toolhead — PNG asset matches the Figma "工具头" instance (72x110).
    // Bambu's create_scaled_bitmap rescales by the longer edge in px, so we
    // pass the natural height to keep the design's aspect ratio.
    m_toolhead_image = new wgtRackToolhead(this, "dev_rack_toolhead.png", kToolheadHeightDip);

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
    main_sizer->Add(m_nozzle_info_panel,     0, wxEXPAND);
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

void wgtRackExtruder::apply_nozzle_info_height()
{
    // The tallest state is a clamped hotend: index circle, diameter line, flow type line. Both
    // lines share the same font there, so the diameter label - the one whose font never changes -
    // gives the height of a single line. Measuring the flow type label instead would tie the
    // panel height to the larger font it switches to when it is the only control left.
    m_nozzle_diameter_label->InvalidateBestSize();
    const int line_height = m_nozzle_diameter_label->GetBestSize().GetHeight();

    const int height = m_nozzle_idx->GetMinSize().GetHeight() + FromDIP(kInfoGapDip) +
                       2 * line_height;
    m_nozzle_info_strut->SetMinSize(0, height);
    m_nozzle_info_panel->SetMinSize(wxSize(-1, height));
    m_nozzle_info_panel->SetMaxSize(wxSize(-1, height));
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
    const bool has_installed_tool = current && current->HasNozzleInstalled() && !ext_nozzle.IsEmpty();
    if (!has_installed_tool) {
        flowtype_label = _L("Unclamped");
    } else if (tool_defs.tool_type == DevNozzleToolType::Nozzle) {
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

    // On its own the flow type line carries the whole panel, so it gets two font sizes more
    // than the rest; next to the index circle and the diameter it has to stay in line with them.
    const wxFont& flowtype_font = m_nozzle_idx->IsShown() || m_nozzle_diameter_label->IsShown()
                                      ? ::Label::Body_12
                                      : ::Label::Body_14;
    if (m_nozzle_flowtype_label->GetFont() != flowtype_font) {
        m_nozzle_flowtype_label->SetFont(flowtype_font);
        changed = true;
    }

    // Toolhead art + filament path: both setters ignore unchanged values, so this
    // only repaints when the state actually moved.
    if (m_toolhead_image) {
        m_toolhead_image->SetImage(rack_extruder_image_name(obj, current, tool_defs));
        m_toolhead_image->SetFilament(
            resolve_filament_path(obj, current, ext_nozzle, tool_defs, has_installed_tool));
    }

    if (changed) {
        // The panel keeps a constant size, so it gets no size event of its own and has
        // to be laid out explicitly after its children changed visibility.
        m_nozzle_info_panel->Layout();
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
    if (m_toolhead_image) m_toolhead_image->Rescale();
    if (m_nozzle_info_panel) apply_nozzle_info_height();
    Layout();
    Refresh();
}

} // namespace Slic3r::GUI
