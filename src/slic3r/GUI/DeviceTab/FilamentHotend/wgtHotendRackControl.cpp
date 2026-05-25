#include "wgtHotendRackControl.h"

#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"
#include "slic3r/GUI/DeviceManager.hpp"
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
    return rack && rack->IsSupported() &&
           (rack->GetRackType() == DevNozzleRack::RackType_A ||
            rack->GetRackType() == DevNozzleRack::RackType_B);
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
    m_obj = obj;

    auto rack = get_rack(m_obj);
    const bool show = is_single_rack_supported(rack);
    if (IsShown() != show) {
        Show(show);
    }

    update_state();
}

void wgtHotendRackControl::SetControlEnabled(bool enabled)
{
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

    apply_cell(lift, wxString(), m_control_enabled);
    apply_cell(center, wxString(), m_control_enabled);
    apply_cell(lower, wxString(), m_control_enabled);

    auto rack = get_rack(m_obj);
    if (!rack) {
        if (changed) {
            Layout();
            Refresh();
        }
        return;
    }

    if (rack->GetReadingCount() > 0) {
        apply_cell(lift, wxString(), false);
        apply_cell(center, _L("Reading"), false);
        apply_cell(lower, wxString(), false);
    } else if (rack->GetStatus() != DevNozzleRack::RACK_STATUS_IDLE &&
               rack->GetStatus() != DevNozzleRack::RACK_STATUS_UNKNOWN) {
        apply_cell(lift, wxString(), false);
        apply_cell(center, _L("Running"), false);
        apply_cell(lower, wxString(), false);
    } else {
        if (rack->GetRackType() == DevNozzleRack::RackType_A) {
            switch (rack->GetRackAPos()) {
                case DevNozzleRack::RackAPos::RACK_POS_UP:
                    apply_cell(lift, _L("Raised"), false);
                    break;
                case DevNozzleRack::RackAPos::RACK_POS_DOWN:
                    apply_cell(lower, _L("Lowered"), false);
                    break;
                case DevNozzleRack::RackAPos::RACK_POS_CENTRE:
                    apply_cell(center, _L("Centered"), false);
                    break;
                default:
                    break;
            }
        } else if (rack->GetRackType() == DevNozzleRack::RackType_B) {
            switch (rack->GetRackBPos()) {
                case DevNozzleRack::RackBPos::RACK_POS_UP:
                    apply_cell(lift, _L("Raised"), false);
                    break;
                case DevNozzleRack::RackBPos::RACK_POS_DOWN:
                    apply_cell(lower, _L("Lowered"), false);
                    break;
                case DevNozzleRack::RackBPos::RACK_POS_CENTRE:
                    apply_cell(center, _L("Centered"), false);
                    break;
                default:
                    break;
            }
        }
    }

    if (changed) {
        Layout();
        Refresh();
    }
}

void wgtHotendRackControl::dispatch_rack_move(RackMoveDir dir)
{
    auto rack = get_rack(m_obj);
    if (!is_single_rack_supported(rack)) return;

    switch (rack->GetRackType()) {
    case DevNozzleRack::RackType_A: {
        DevNozzleRack::RackAPos pos = DevNozzleRack::RackAPos::RACK_POS_CENTRE;
        if (dir == RackMoveDir::Lift)  pos = DevNozzleRack::RackAPos::RACK_POS_UP;
        if (dir == RackMoveDir::Lower) pos = DevNozzleRack::RackAPos::RACK_POS_DOWN;
        rack->CtrlRackPosMove(pos);
        break;
    }
    case DevNozzleRack::RackType_B: {
        DevNozzleRack::RackBPos pos = DevNozzleRack::RackBPos::RACK_POS_CENTRE;
        if (dir == RackMoveDir::Lift)  pos = DevNozzleRack::RackBPos::RACK_POS_UP;
        if (dir == RackMoveDir::Lower) pos = DevNozzleRack::RackBPos::RACK_POS_DOWN;
        rack->CtrlRackPosMove(pos);
        break;
    }
    default:
        break;
    }
}

} // namespace Slic3r::GUI
