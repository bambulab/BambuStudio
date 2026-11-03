#include "wgtHotendRackControl.h"

#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/Widgets/Button.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"

#include <wx/sizer.h>

#include <memory>

namespace Slic3r::GUI {

namespace {

const wxColour kButtonNormalBg = wxColour("#F2F2F2");
const wxColour kButtonCenterBg = wxColour("#DBDBDB");
const wxColour kButtonPressedBg = wxColour(172, 172, 172);
const wxColour kButtonHoverBorder = wxColour(0, 174, 66);
const wxColour kTextNormal = wxColour(48, 58, 60);
const wxColour kTextDisabled = wxColour(171, 172, 172);
const wxColour kLabelNormal = wxColour(107, 107, 107);

constexpr int kButtonWidthDip = 55;
constexpr int kButtonHeightDip = 28;
constexpr int kIconSizeDip = 15;

struct RackCell {
    Button* button;
    wxString icon;
    wxColour bg;
};

std::shared_ptr<DevNozzleRack> get_rack(MachineObject* obj)
{
    auto nozzle_system = obj ? obj->GetNozzleSystem() : nullptr;
    return nozzle_system ? nozzle_system->GetNozzleRack() : std::shared_ptr<DevNozzleRack>();
}

bool is_single_rack_supported(const std::shared_ptr<DevNozzleRack>& rack)
{
    return rack && rack->IsSupported() && rack->IsDirectConnectedRack();
}

} // namespace

wgtHotendRackControl::wgtHotendRackControl(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    create_ui();
    Hide();
}

void wgtHotendRackControl::create_ui()
{
    SetBackgroundColour(*wxWHITE);

    StateColor rack_btn_bd(std::pair<wxColour, int>(kButtonHoverBorder, StateColor::Hovered),
                           std::pair<wxColour, int>(kButtonNormalBg, StateColor::Normal));
    StateColor rack_btn_text(std::make_pair(kTextDisabled, (int) StateColor::Disabled),
                             std::make_pair(kTextNormal, (int) StateColor::Normal));

    auto button_size = wxSize(FromDIP(kButtonWidthDip), FromDIP(kButtonHeightDip));
    auto build_btn = [&](const wxString& icon, const wxColour& normal_bg) {
        StateColor rack_btn_bg(std::pair<wxColour, int>(kButtonPressedBg, StateColor::Pressed),
                               std::pair<wxColour, int>(normal_bg, StateColor::Normal));
        auto* btn = new Button(this, wxEmptyString, icon, 0, FromDIP(kIconSizeDip));
        btn->SetFont(::Label::Body_12);
        btn->SetBorderWidth(0);
        btn->SetBackgroundColor(rack_btn_bg);
        btn->SetBorderColor(rack_btn_bd);
        btn->SetTextColor(rack_btn_text);
        btn->SetPaddingSize(wxSize(FromDIP(2), FromDIP(4)));
        btn->SetMinSize(button_size);
        btn->SetMaxSize(button_size);
        btn->SetSize(button_size);
        btn->SetCornerRadius(0);
        btn->SetCursor(wxCursor(wxCURSOR_HAND));
        return btn;
    };

    m_btn_lift = build_btn("monitor_hotend_rack_lift", kButtonNormalBg);
    m_btn_center = build_btn("monitor_hotend_rack_center", kButtonCenterBg);
    m_btn_lower = build_btn("monitor_hotend_rack_lower", kButtonNormalBg);

    m_btn_lift->SetToolTip(_L("Lift Hotend Rack"));
    m_btn_center->SetToolTip(_L("Recenter Hotend Rack"));
    m_btn_lower->SetToolTip(_L("Lower Hotend Rack"));

    m_btn_lift->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](wxCommandEvent&) { dispatch_rack_move(RackMoveDir::Lift); });
    m_btn_center->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](wxCommandEvent&) { dispatch_rack_move(RackMoveDir::Center); });
    m_btn_lower->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](wxCommandEvent&) { dispatch_rack_move(RackMoveDir::Lower); });

    m_label = new wxStaticText(this, wxID_ANY, _L("Hotend Rack"));
    m_label->SetFont(::Label::Body_12);
    m_label->SetForegroundColour(kLabelNormal);

    auto* sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(m_label, 0, wxALIGN_CENTRE | wxRIGHT, FromDIP(12));
    sizer->Add(m_btn_lift, 0, wxEXPAND | wxLEFT | wxRIGHT, 0);
    sizer->Add(m_btn_center, 0, wxEXPAND | wxLEFT, FromDIP(8));
    sizer->Add(m_btn_lower, 0, wxEXPAND | wxLEFT, FromDIP(8));

    SetSizer(sizer);
    Layout();
    Fit();
}

void wgtHotendRackControl::UpdateByMachine(Slic3r::MachineObject* obj)
{
    m_devid = obj ? obj->get_dev_id() : std::string();

    auto rack = get_rack(obj);
    const bool show = is_single_rack_supported(rack);
    if (IsShown() != show) {
        Show(show);
    }

    update_state();
}

void wgtHotendRackControl::SetControlEnabled(Slic3r::MachineObject* obj, bool enabled)
{
    m_devid = obj ? obj->get_dev_id() : std::string();
    m_control_enabled = enabled;
    if (m_label && m_label->GetForegroundColour() != (enabled ? kLabelNormal : kTextDisabled)) {
        m_label->SetForegroundColour(enabled ? kLabelNormal : kTextDisabled);
    }
    update_state();
}

void wgtHotendRackControl::Rescale()
{
    auto button_size = wxSize(FromDIP(kButtonWidthDip), FromDIP(kButtonHeightDip));
    for (Button* btn : {m_btn_lift, m_btn_center, m_btn_lower}) {
        if (!btn) continue;
        btn->SetMinSize(button_size);
        btn->SetMaxSize(button_size);
        btn->SetCornerRadius(0);
        btn->Rescale();
    }
    Layout();
}

MachineObject* wgtHotendRackControl::get_current_dev() const
{
    auto* dev_manager = wxGetApp().getDeviceManager();
    return dev_manager && !m_devid.empty() ? dev_manager->get_my_machine(m_devid) : nullptr;
}

void wgtHotendRackControl::update_state()
{
    if (!m_btn_lift || !m_btn_center || !m_btn_lower) return;

    RackCell lift{m_btn_lift, "monitor_hotend_rack_lift", kButtonNormalBg};
    RackCell center{m_btn_center, "monitor_hotend_rack_center", kButtonCenterBg};
    RackCell lower{m_btn_lower, "monitor_hotend_rack_lower", kButtonNormalBg};

    bool changed = false;
    auto apply_cell = [&changed](const RackCell& cell, const wxString& label, bool enable) {
        if (cell.button->GetLabel() != label) {
            cell.button->SetLabel(label);
            cell.button->SetIcon(label.IsEmpty() ? cell.icon : wxString());
            changed = true;
        }
        if (cell.button->IsEnabled() != enable) {
            cell.button->Enable(enable);
            cell.button->SetCursor(wxCursor(enable ? wxCURSOR_HAND : wxCURSOR_ARROW));
            changed = true;
        }
    };

    // Resolve the final (label, enable) target
    wxString lift_label, center_label, lower_label;
    bool lift_enable   = m_control_enabled;
    bool center_enable = m_control_enabled;
    bool lower_enable  = m_control_enabled;

    auto rack = get_rack(get_current_dev());
    if (rack) {
        if (rack->GetReadingCount() > 0) {
            center_label = _L("Reading");
            lift_enable = center_enable = lower_enable = false;
        } else if (rack->GetStatus() != DevNozzleRack::RACK_STATUS_IDLE &&
                   rack->GetStatus() != DevNozzleRack::RACK_STATUS_UNKNOWN) {
            center_label = _L("Running");
            lift_enable = center_enable = lower_enable = false;
        } else if (rack->IsDirectConnectedRack()) {
            switch (rack->GetDirectRackPos()) {
                case DevNozzleRack::DirectRackPos::RACK_POS_UP:     lift_label = _L("Raised");    lift_enable = false;   break;
                case DevNozzleRack::DirectRackPos::RACK_POS_DOWN:   lower_label = _L("Lowered");  lower_enable = false;  break;
                case DevNozzleRack::DirectRackPos::RACK_POS_CENTRE: center_label = _L("Centered"); center_enable = false; break;
                default: break;
            }
        }
    }

    apply_cell(lift, lift_label, lift_enable);
    apply_cell(center, center_label, center_enable);
    apply_cell(lower, lower_label, lower_enable);

    if (changed) {
        Layout();
        Refresh();
    }
}

void wgtHotendRackControl::dispatch_rack_move(RackMoveDir dir)
{
    auto rack = get_rack(get_current_dev());
    if (is_single_rack_supported(rack)) {
        DevNozzleRack::DirectRackPos pos = DevNozzleRack::DirectRackPos::RACK_POS_CENTRE;
        if (dir == RackMoveDir::Lift)  pos = DevNozzleRack::DirectRackPos::RACK_POS_UP;
        if (dir == RackMoveDir::Lower) pos = DevNozzleRack::DirectRackPos::RACK_POS_DOWN;
        rack->CtrlRackPosMove(pos);
    }
}

} // namespace Slic3r::GUI
