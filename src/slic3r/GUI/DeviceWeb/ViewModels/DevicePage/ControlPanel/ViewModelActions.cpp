#include "ViewModelActions.hpp"

#include "ViewModel.hpp"

#include "slic3r/GUI/DeviceCore/DevAxis.h"
#include "slic3r/GUI/DeviceCore/DevBed.h"
#include "slic3r/GUI/DeviceCore/DevChamber.h"
#include "slic3r/GUI/DeviceCore/DevConfig.h"
#include "slic3r/GUI/DeviceCore/DevCtrl.h"
#include "slic3r/GUI/DeviceCore/DevDefs.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevFan.h"
#include "slic3r/GUI/DeviceCore/DevLamp.h"
#include "slic3r/GUI/DeviceCore/DevInfo.h"
#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Monitor.hpp"
#include "slic3r/GUI/StatusPanel.hpp"
#include "slic3r/GUI/DeviceTab/ControlPanel/wgtDeviceControlPanelWebPanel.h"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/RecenterDialog.hpp"
#include "slic3r/GUI/ReleaseNote.hpp"
#include "slic3r/GUI/Widgets/FanControl.hpp"
#include "slic3r/GUI/Widgets/PopupWindow.hpp"
#include "slic3r/GUI/Widgets/StepCtrl.hpp"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <string>
#include <tuple>
#include <unordered_map>
#include <wx/display.h>
#include <wx/sizer.h>
#ifdef __APPLE__
#include <wx/msgdlg.h>
#endif
#include "libslic3r/Log.hpp"

namespace Slic3r { namespace GUI {

namespace {

namespace ActionNames = ControlPanelActionNames;
namespace ActionOps = ControlPanelActionOps;

using SteadyClock = std::chrono::steady_clock;
constexpr auto kCommandHoldDuration = std::chrono::seconds(5);
constexpr auto kSwitchingHoldDuration = std::chrono::seconds(6);

template <typename T>
struct HeldValue
{
    std::string             dev_id;
    T                       value{};
    SteadyClock::time_point expire_at{};
    bool                    active = false;
};

struct HoldStore
{
    std::mutex                        mutex;
    std::unordered_map<int, HeldValue<int>> nozzle_temp;
    HeldValue<int>                    bed_temp;
    HeldValue<int>                    chamber_temp;
    HeldValue<int>                    speed_level;
    HeldValue<bool>                   lamp_on;
    std::string                       switching_dev_id;
    SteadyClock::time_point           switching_expire_at{};
    bool                              switching_active = false;
};

HoldStore& hold_store()
{
    static HoldStore store;
    return store;
}

template <typename T>
void set_held(HeldValue<T>& slot, const std::string& dev_id, T value, SteadyClock::duration duration = kCommandHoldDuration)
{
    if (dev_id.empty()) return;
    slot.dev_id = dev_id;
    slot.value = value;
    slot.expire_at = SteadyClock::now() + duration;
    slot.active = true;
}

template <typename T>
std::optional<T> resolve_held(HeldValue<T>& slot, const std::string& dev_id, std::optional<T> reported)
{
    if (!slot.active || dev_id.empty() || slot.dev_id != dev_id) {
        return reported;
    }
    if (SteadyClock::now() >= slot.expire_at) {
        slot.active = false;
        return reported;
    }
    if (reported.has_value() && *reported == slot.value) {
        slot.active = false;
        return reported;
    }
    return slot.value;
}

FanControlPopupNew* s_active_fan_popup = nullptr;
SecondaryCheckDialog* s_axis_go_home_dlg = nullptr;
SteadyClock::time_point s_speed_dismiss_time{};

wxWindow* resolve_control_panel_host_window()
{
    if (!wxGetApp().mainframe || !wxGetApp().mainframe->m_monitor)
        return wxGetApp().mainframe;
    if (auto* status_panel = wxGetApp().mainframe->m_monitor->get_status_panel()) {
        if (auto* web_panel = status_panel->get_control_panel_web_panel())
            return web_panel;
        return status_panel;
    }
    return wxGetApp().mainframe;
}

int extruder_id_from_payload(const nlohmann::json& payload)
{
    const std::string id = payload.value("extruder_id", std::string("main"));
    return id == "deputy" ? DEPUTY_EXTRUDER_ID : MAIN_EXTRUDER_ID;
}

nlohmann::json respond(DevicePageControlPanelVM& vm, const std::string& action, int code, const std::string& message = {})
{
    vm.InvalidateReportCache();
    return vm.MakeActionResponse(action, code, message, vm.BuildState());
}

bool require_machine(DevicePageControlPanelVM& vm, const std::string& action, MachineObject* machine_obj, nlohmann::json& out)
{
    if (machine_obj) return true;
    out = respond(vm, action, 3, _u8L("Please select a printer"));
    return false;
}

void open_speed_popup(MachineObject* machine_obj, int x_dip, int y_dip, int h_dip)
{
    if (!wxGetApp().mainframe) return;
    const auto now = SteadyClock::now();
    if (s_speed_dismiss_time.time_since_epoch().count() > 0 &&
        std::chrono::duration_cast<std::chrono::milliseconds>(now - s_speed_dismiss_time).count() < 200) {
        s_speed_dismiss_time = now - std::chrono::seconds(1);
        return;
    }

    wxWindow* host_win = resolve_control_panel_host_window();
    if (!host_win) return;

#if __WXOSX__
    PopupWindow* popUp = new PopupWindow(nullptr);
#else
    PopupWindow* popUp = new PopupWindow(host_win);
#endif
#ifdef __WXMSW__
    popUp->BindUnfocusEvent();
#endif
    popUp->SetBackgroundColour(StateColor::darkModeColorFor(0xeeeeee));
    StepCtrl* step = new StepCtrl(popUp, wxID_ANY);
    wxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(step, 1, wxEXPAND, 0);
    popUp->SetSizer(sizer);
    const auto em = em_unit(host_win);
    popUp->SetSize(em * 36, em * 8);
    step->SetHint(_L("This only takes effect during printing"));
    step->AppendItem(_L("Silent"), "");
    step->AppendItem(_L("Standard"), "");
    step->AppendItem(_L("Sport"), "");
    step->AppendItem(_L("Ludicrous"), "");

    int selected_item = 1;
    if (machine_obj) {
        const int speed_lvl_idx = machine_obj->GetPrintingSpeedLevel() - 1;
        if (speed_lvl_idx >= 0 && speed_lvl_idx < 4)
            selected_item = speed_lvl_idx;
    }
    step->SelectItem(selected_item);

    if (!machine_obj || !machine_obj->is_in_printing()) {
        step->Bind(wxEVT_LEFT_DOWN, [](auto&) { return; });
    }

    const std::string dev_id = machine_obj ? machine_obj->get_dev_id() : std::string();
    step->Bind(EVT_STEP_CHANGED, [dev_id](wxCommandEvent& e) {
        const int speed_lvl = std::clamp(e.GetInt() + 1, 1, 4);
        MachineObject* obj = wxGetApp().getDeviceManager() && !dev_id.empty()
            ? wxGetApp().getDeviceManager()->get_my_machine(dev_id)
            : nullptr;
        if (obj) {
            ControlPanelActionHandler::SetHeldSpeedLevel(dev_id, speed_lvl);
            obj->command_set_printing_speed(static_cast<DevPrintingSpeedLevel>(speed_lvl));
            if (wxGetApp().mainframe && wxGetApp().mainframe->m_monitor) {
                if (auto* sp = wxGetApp().mainframe->m_monitor->get_status_panel()) {
                    if (auto* wp = sp->get_control_panel_web_panel())
                        wp->UpdateByMachine(obj);
                }
            }
        }
    });

    popUp->Bind(wxEVT_SHOW, [popUp](wxShowEvent& e) {
        if (!e.IsShown()) {
            popUp->Destroy();
            s_speed_dismiss_time = SteadyClock::now();
        }
    });

    const wxPoint anchor = host_win->ClientToScreen(
        wxPoint(host_win->FromDIP(x_dip), host_win->FromDIP(y_dip - 6)));
    popUp->Position(anchor, wxSize(0, host_win->FromDIP(h_dip + 12)));
    popUp->Popup();
}

void open_fan_popup(MachineObject* machine_obj, int x_dip, int y_dip, int h_dip)
{
    if (!machine_obj || !wxGetApp().mainframe) return;
    if (s_active_fan_popup) {
        s_active_fan_popup->Destroy();
        s_active_fan_popup = nullptr;
    }
    auto fan = machine_obj->GetFan();
    if (!fan) return;
    if (fan->GetAirDuctData().modes.empty())
        fan->converse_to_duct(true, fan->GetSupportAuxFanData(), fan->GetSupportChamberFan());

    wxWindow* host_win = resolve_control_panel_host_window();
    auto* popup = new FanControlPopupNew(wxGetApp().mainframe, machine_obj, fan->GetAirDuctData());
    s_active_fan_popup = popup;

    if (host_win) {
        wxPoint pos = host_win->ClientToScreen(
            wxPoint(host_win->FromDIP(x_dip), host_win->FromDIP(y_dip + h_dip)));
        const int display_idx = wxDisplay::GetFromWindow(host_win);
        if (display_idx != wxNOT_FOUND) {
            const auto display = wxDisplay(display_idx).GetClientArea();
            const wxSize screen_size(display.GetWidth(), display.GetHeight());
            const wxSize fan_popup_size = popup->GetSize();
            pos.x -= host_win->FromDIP(150);
            pos.y -= host_win->FromDIP(20);
            if (screen_size.y - fan_popup_size.y < host_win->FromDIP(300))
                pos.y = (screen_size.y - fan_popup_size.y) / 2;
            popup->SetPosition(pos);
        } else {
            popup->CentreOnParent();
        }
    } else {
        popup->CentreOnParent();
    }

    popup->ShowModal();
    if (s_active_fan_popup == popup)
        s_active_fan_popup = nullptr;
    popup->Destroy();
}

void show_recenter_dialog()
{
    wxGetApp().CallAfter([]() {
        if (!wxGetApp().mainframe) return;
        RecenterDialog dlg(wxGetApp().mainframe);
        if (dlg.ShowModal() == wxID_OK) {
            MachineObject* obj = wxGetApp().getDeviceManager()
                ? wxGetApp().getDeviceManager()->get_selected_machine()
                : nullptr;
            if (obj && obj->GetAxis()) {
                obj->GetAxis()->Ctrl_GoHome();
            }
        }
    });
}

void show_go_home_confirm_dialog()
{
    wxGetApp().CallAfter([]() {
        if (!wxGetApp().mainframe) return;
        if (s_axis_go_home_dlg == nullptr) {
            s_axis_go_home_dlg = new SecondaryCheckDialog(wxGetApp().mainframe, wxID_ANY, _L("Auto homing"));
            s_axis_go_home_dlg->update_text(_L("Are you sure you want to trigger auto homing?"));
            s_axis_go_home_dlg->m_button_ok->SetLabel(_L("Homing"));
            s_axis_go_home_dlg->Bind(EVT_SECONDARY_CHECK_CONFIRM, [](wxCommandEvent&) {
                MachineObject* obj = wxGetApp().getDeviceManager()
                    ? wxGetApp().getDeviceManager()->get_selected_machine()
                    : nullptr;
                if (obj && obj->GetAxis()) {
                    obj->GetAxis()->Ctrl_GoHome();
                }
            });
            s_axis_go_home_dlg->Bind(wxEVT_DESTROY, [](wxWindowDestroyEvent& e) {
                if (e.GetEventObject() == s_axis_go_home_dlg) {
                    s_axis_go_home_dlg = nullptr;
                }
                e.Skip();
            });
        } else {
            s_axis_go_home_dlg->update_text(_L("Are you sure you want to trigger auto homing?"));
            s_axis_go_home_dlg->m_button_ok->SetLabel(_L("Homing"));
        }
        s_axis_go_home_dlg->on_show();
    });
}

int run_axis(MachineObject* machine_obj, const std::string& op)
{
    if (!machine_obj || !machine_obj->GetAxis()) return -1;
    auto axis = machine_obj->GetAxis();
    if (op == ActionOps::xy_home) {
        show_go_home_confirm_dialog();
        return 0;
    }

    const bool is_x_move = (op == ActionOps::x_pos_10 || op == ActionOps::x_neg_10 ||
                            op == ActionOps::x_pos_1  || op == ActionOps::x_neg_1);
    const bool is_y_move = (op == ActionOps::y_pos_10 || op == ActionOps::y_neg_10 ||
                            op == ActionOps::y_pos_1  || op == ActionOps::y_neg_1);
    const bool is_z_move = (op == ActionOps::z_up_10  || op == ActionOps::z_up_1 ||
                            op == ActionOps::z_down_1 || op == ActionOps::z_down_10);

    if (is_x_move && !axis->IsAxisAtHomeX()) {
        BOOST_LOG_TRIVIAL(info) << "axis x is not at home";
        show_recenter_dialog();
        return 0;
    }
    if (is_y_move && !axis->IsAxisAtHomeY()) {
        BOOST_LOG_TRIVIAL(info) << "axis y is not at home";
        show_recenter_dialog();
        return 0;
    }

    static const std::unordered_map<std::string, std::tuple<std::string, float, int>> moves = {
        {ActionOps::x_pos_10, {"X", 10.0f, 3000}},
        {ActionOps::x_neg_10, {"X", -10.0f, 3000}},
        {ActionOps::x_pos_1,  {"X", 1.0f, 3000}},
        {ActionOps::x_neg_1,  {"X", -1.0f, 3000}},
        {ActionOps::y_pos_10, {"Y", 10.0f, 3000}},
        {ActionOps::y_neg_10, {"Y", -10.0f, 3000}},
        {ActionOps::y_pos_1,  {"Y", 1.0f, 3000}},
        {ActionOps::y_neg_1,  {"Y", -1.0f, 3000}},
        {ActionOps::z_up_10,  {"Z", -10.0f, 900}},
        {ActionOps::z_up_1,   {"Z", -1.0f, 900}},
        {ActionOps::z_down_1, {"Z", 1.0f, 900}},
        {ActionOps::z_down_10, {"Z", 10.0f, 900}},
    };
    const auto it = moves.find(op);
    if (it == moves.end()) return -1;
    const int rc = axis->Ctrl_Axis(std::get<0>(it->second), 1.0, std::get<1>(it->second), std::get<2>(it->second));
    if (is_z_move && !axis->IsAxisAtHomeZ()) {
        BOOST_LOG_TRIVIAL(info) << "axis z is not at home";
        show_recenter_dialog();
        return 0;
    }
    return rc;
}

int run_rack(MachineObject* machine_obj, const std::string& op)
{
    if (!machine_obj || !machine_obj->GetNozzleSystem()) return -1;
    auto rack = machine_obj->GetNozzleSystem()->GetNozzleRack();
    if (!rack || !rack->IsSupported() || !rack->IsDirectConnectedRack()) return -1;
    DevNozzleRack::DirectRackPos pos = DevNozzleRack::DirectRackPos::RACK_POS_CENTRE;
    if (op == ActionOps::rack_lift) pos = DevNozzleRack::DirectRackPos::RACK_POS_UP;
    else if (op == ActionOps::rack_lower) pos = DevNozzleRack::DirectRackPos::RACK_POS_DOWN;
    const std::string dev_id = machine_obj->get_dev_id();
    wxGetApp().CallAfter([dev_id, pos]() {
        MachineObject* obj = wxGetApp().getDeviceManager() && !dev_id.empty()
            ? wxGetApp().getDeviceManager()->get_my_machine(dev_id)
            : nullptr;
        if (!obj || !obj->GetNozzleSystem()) return;
        auto cur_rack = obj->GetNozzleSystem()->GetNozzleRack();
        if (cur_rack && cur_rack->IsSupported() && cur_rack->IsDirectConnectedRack()) {
            cur_rack->CtrlRackPosMove(pos);
        }
    });
    return 0;
}

int run_extrude(MachineObject* machine_obj, const std::string& op)
{
    if (!machine_obj || !machine_obj->GetExtderSystem()) return -1;
    const auto ext = machine_obj->GetExtderSystem()->GetCurrentExtder();
    if (!ext) return -1;

    const double value = op == ActionOps::e_up_10 ? -10.0 : 10.0;
    if (ext->IsBowdenExtuder() && !ext->HasFilamentInExt()) {
        if (machine_obj->is_enable_np)
            return machine_obj->command_extruder_control(ext->GetExtId(), value);
        return machine_obj->GetAxis()->Ctrl_Axis("E", 1.0, value, 900);
    }
    if (ext->GetCurrentTemp() < 170) {
        wxGetApp().CallAfter([]() {
            MessageDialog dlg(nullptr, _L("Please heat the nozzle to above 170 degree before loading or unloading filament."),
                              wxString(_L("Warning")), wxOK | wxCENTER);
            dlg.ShowModal();
        });
        return 0;
    }
    if (machine_obj->is_enable_np)
        return machine_obj->command_extruder_control(ext->GetExtId(), value);
    return machine_obj->GetAxis()->Ctrl_Axis("E", 1.0, value, 900);
}

int clamp_temp_preserving_zero(int value, int min_temp, int max_temp)
{
    if (value == 0) return 0;
    return std::clamp(value, min_temp, max_temp);
}

} // namespace

namespace ControlPanelActionHandler {

void UpdateActiveFanPopup(MachineObject* machine_obj)
{
    if (!s_active_fan_popup) return;
    if (machine_obj && machine_obj->is_connected() && machine_obj->GetInfo() && machine_obj->GetInfo()->IsFdmMode()) {
        s_active_fan_popup->update_fan_data(machine_obj);
    } else {
        if (s_active_fan_popup->IsModal())
            s_active_fan_popup->EndModal(wxID_CANCEL);
        else
            s_active_fan_popup->Hide();
    }
}

void SetHeldNozzleTemp(const std::string& dev_id, int extruder_id, int value)
{
    auto& store = hold_store();
    std::lock_guard<std::mutex> lock(store.mutex);
    set_held(store.nozzle_temp[extruder_id], dev_id, value);
}

std::optional<int> ResolveHeldNozzleTemp(const std::string& dev_id, int extruder_id, std::optional<int> reported)
{
    auto& store = hold_store();
    std::lock_guard<std::mutex> lock(store.mutex);
    auto it = store.nozzle_temp.find(extruder_id);
    if (it == store.nozzle_temp.end()) return reported;
    return resolve_held(it->second, dev_id, reported);
}

void SetHeldBedTemp(const std::string& dev_id, int value)
{
    auto& store = hold_store();
    std::lock_guard<std::mutex> lock(store.mutex);
    set_held(store.bed_temp, dev_id, value);
}

std::optional<int> ResolveHeldBedTemp(const std::string& dev_id, std::optional<int> reported)
{
    auto& store = hold_store();
    std::lock_guard<std::mutex> lock(store.mutex);
    return resolve_held(store.bed_temp, dev_id, reported);
}

void SetHeldChamberTemp(const std::string& dev_id, int value)
{
    auto& store = hold_store();
    std::lock_guard<std::mutex> lock(store.mutex);
    set_held(store.chamber_temp, dev_id, value);
}

std::optional<int> ResolveHeldChamberTemp(const std::string& dev_id, std::optional<int> reported)
{
    auto& store = hold_store();
    std::lock_guard<std::mutex> lock(store.mutex);
    return resolve_held(store.chamber_temp, dev_id, reported);
}

void SetHeldSpeedLevel(const std::string& dev_id, int level)
{
    auto& store = hold_store();
    std::lock_guard<std::mutex> lock(store.mutex);
    set_held(store.speed_level, dev_id, level);
}

int ResolveHeldSpeedLevel(const std::string& dev_id, int reported)
{
    auto& store = hold_store();
    std::lock_guard<std::mutex> lock(store.mutex);
    return resolve_held(store.speed_level, dev_id, std::make_optional(reported)).value_or(reported);
}

void SetHeldLampOn(const std::string& dev_id, bool on)
{
    auto& store = hold_store();
    std::lock_guard<std::mutex> lock(store.mutex);
    set_held(store.lamp_on, dev_id, on);
}

bool ResolveHeldLampOn(const std::string& dev_id, bool reported)
{
    auto& store = hold_store();
    std::lock_guard<std::mutex> lock(store.mutex);
    return resolve_held(store.lamp_on, dev_id, std::make_optional(reported)).value_or(reported);
}

void SetSwitchingActionHold(const std::string& dev_id)
{
    if (dev_id.empty()) return;
    auto& store = hold_store();
    std::lock_guard<std::mutex> lock(store.mutex);
    store.switching_dev_id = dev_id;
    store.switching_expire_at = SteadyClock::now() + kSwitchingHoldDuration;
    store.switching_active = true;
}

bool IsSwitchingActionHeld(const std::string& dev_id)
{
    auto& store = hold_store();
    std::lock_guard<std::mutex> lock(store.mutex);
    if (!store.switching_active || dev_id.empty() || store.switching_dev_id != dev_id)
        return false;
    if (SteadyClock::now() >= store.switching_expire_at) {
        store.switching_active = false;
        return false;
    }
    return true;
}

std::optional<nlohmann::json> TryHandle(DevicePageControlPanelVM& vm,
                                        const std::string& action,
                                        const nlohmann::json& payload,
                                        MachineObject* machine_obj)
{
    nlohmann::json missing_machine_response;
    const auto require = [&]() {
        return require_machine(vm, action, machine_obj, missing_machine_response);
    };

    if (action == ActionNames::set_extruder_temp) {
        if (!require()) return missing_machine_response;
        const int extruder_id = extruder_id_from_payload(payload);
        if (machine_obj->GetExtderSystem()) {
            const auto extder = machine_obj->GetExtderSystem()->GetExtderById(extruder_id);
            if (!extder) return respond(vm, action, 0);
            if (!extder->HasNozzleInstalled()) {
                wxGetApp().CallAfter([]() {
                    MessageDialog msg_dlg(nullptr, _L("Right extruder hotend not detected. Cannot set nozzle temperature."),
                                          _L("Warning"), wxICON_WARNING | wxOK);
                    msg_dlg.ShowModal();
                });
                return respond(vm, action, 0);
            }
        }
        int value = payload.value("value", 0);
        int min_temp = 20;
        int max_temp = 300;
        if (machine_obj->nozzle_temp_range.size() >= 2) {
            min_temp = machine_obj->nozzle_temp_range[0];
            max_temp = machine_obj->nozzle_temp_range[1];
        }
        value = clamp_temp_preserving_zero(value, min_temp, max_temp);
        SetHeldNozzleTemp(machine_obj->get_dev_id(), extruder_id, value);
        if (machine_obj->GetExtderSystem() && machine_obj->GetExtderSystem()->GetTotalExtderCount() >= 2)
            machine_obj->command_set_nozzle_new(extruder_id, value);
        else
            machine_obj->command_set_nozzle(value);
        return respond(vm, action, 0);
    }

    if (action == ActionNames::set_bed_temp) {
        if (!require()) return missing_machine_response;
        int value = payload.value("value", 0);
        int limit = machine_obj->get_bed_temperature_limit();
        if (machine_obj->bed_temp_range.size() > 1) limit = machine_obj->bed_temp_range[1];
        int min_temp = machine_obj->bed_temp_range.size() > 0 ? machine_obj->bed_temp_range[0] : 20;
        value = clamp_temp_preserving_zero(value, min_temp, limit);
        SetHeldBedTemp(machine_obj->get_dev_id(), value);
        machine_obj->command_set_bed(value);
        return respond(vm, action, 0);
    }

    if (action == ActionNames::set_chamber_temp) {
        if (!require()) return missing_machine_response;
        if (!machine_obj->GetChamber() || !machine_obj->GetChamber()->SupportChamberEdit())
            return respond(vm, action, 3, _u8L("Chamber temperature is not editable."));
        int value = payload.value("value", 0);
        value = clamp_temp_preserving_zero(
            value,
            machine_obj->GetChamber()->GetChamberTempEditMin(),
            machine_obj->GetChamber()->GetChamberTempEditMax());

        if (machine_obj->is_in_printing() && machine_obj->GetFan() &&
            machine_obj->GetFan()->GetSupportAirduct() && machine_obj->GetFan()->is_at_cooling_mode()) {
            wxGetApp().CallAfter([]() {
#ifndef __APPLE__
                MessageDialog dlg(nullptr, _L("Chamber temperature cannot be changed in cooling mode while printing."),
                                  wxEmptyString, wxICON_WARNING | wxOK);
#else
                wxMessageDialog dlg(nullptr, _L("Chamber temperature cannot be changed in cooling mode while printing."),
                                    wxEmptyString, wxICON_WARNING | wxOK);
#endif
                dlg.ShowModal();
            });
            return respond(vm, action, 0);
        }

        if (machine_obj->GetFan() && !machine_obj->GetFan()->is_at_heating_mode() &&
            machine_obj->GetConfig() && value >= machine_obj->GetConfig()->GetChamberTempSwitchHeat()) {
            const std::string dev_id = machine_obj->get_dev_id();
            wxGetApp().CallAfter([dev_id, value]() {
#ifndef __APPLE__
                MessageDialog dlg(nullptr,
                                  _L("If the chamber temperature exceeds 40\u2103, the system will automatically switch to heating mode. "
                                     "Please confirm whether to switch."),
                                  wxEmptyString, wxICON_WARNING | wxOK | wxCANCEL);
#else
                wxMessageDialog dlg(nullptr,
                                    _L("If the chamber temperature exceeds 40\u2103, the system will automatically switch to heating mode. "
                                       "Please confirm whether to switch."),
                                    wxEmptyString, wxICON_WARNING | wxOK | wxCANCEL);
#endif
                if (dlg.ShowModal() != wxID_OK) return;
                MachineObject* obj = wxGetApp().getDeviceManager() && !dev_id.empty()
                    ? wxGetApp().getDeviceManager()->get_my_machine(dev_id)
                    : nullptr;
                if (obj && obj->GetChamber()) {
                    SetHeldChamberTemp(dev_id, value);
                    obj->GetChamber()->CtrlSetChamberTemp(value);
                }
            });
            return respond(vm, action, 0);
        }

        SetHeldChamberTemp(machine_obj->get_dev_id(), value);
        machine_obj->GetChamber()->CtrlSetChamberTemp(value);
        return respond(vm, action, 0);
    }

    if (action == ActionNames::set_speed) {
        if (!require()) return missing_machine_response;
        if (!machine_obj->is_in_printing())
            return respond(vm, action, 0);
        const int level = std::clamp(payload.value("level", 2), 1, 4);
        SetHeldSpeedLevel(machine_obj->get_dev_id(), level);
        machine_obj->command_set_printing_speed(static_cast<DevPrintingSpeedLevel>(level));
        return respond(vm, action, 0);
    }

    if (action == ActionNames::open_speed) {
        if (!require()) return missing_machine_response;
        const int x_dip = payload.value("x", 10);
        const int y_dip = payload.value("y", 270);
        const int h_dip = payload.value("height", 51);
        wxGetApp().CallAfter([machine_obj, x_dip, y_dip, h_dip]() {
            open_speed_popup(machine_obj, x_dip, y_dip, h_dip);
        });
        return respond(vm, action, 0);
    }

    if (action == ActionNames::toggle_lamp) {
        if (!require()) return missing_machine_response;
        if (!machine_obj->GetLamp()) return respond(vm, action, 0);
        const bool on = payload.value("on", false);
        if (!on && machine_obj->GetLamp()->HasLampCloseRecheck()) {
            const std::string dev_id = machine_obj->get_dev_id();
            wxGetApp().CallAfter([dev_id]() {
                MessageDialog msg_dlg(nullptr,
                                      _L("Turning off the lights during the task will cause the failure of AI monitoring, like spaghetti detection. Please choose carefully."),
                                      wxEmptyString, wxICON_WARNING | wxOK | wxNO);
                msg_dlg.SetButtonLabel(wxID_OK, _L("Keep it On"));
                msg_dlg.SetButtonLabel(wxID_NO, _L("Still turn it Off"));
                if (msg_dlg.ShowModal() != wxID_NO) return;
                MachineObject* obj = wxGetApp().getDeviceManager() && !dev_id.empty()
                    ? wxGetApp().getDeviceManager()->get_my_machine(dev_id)
                    : nullptr;
                if (obj && obj->GetLamp()) {
                    SetHeldLampOn(dev_id, false);
                    obj->GetLamp()->CtrlSetChamberLight(DevLamp::LIGHT_EFFECT_OFF);
                }
            });
            return respond(vm, action, 0);
        }
        SetHeldLampOn(machine_obj->get_dev_id(), on);
        machine_obj->GetLamp()->CtrlSetChamberLight(on ? DevLamp::LIGHT_EFFECT_ON : DevLamp::LIGHT_EFFECT_OFF);
        return respond(vm, action, 0);
    }

    if (action == ActionNames::open_fan) {
        if (!require()) return missing_machine_response;
        const int x_dip = payload.value("x", 10);
        const int y_dip = payload.value("y", 220);
        const int h_dip = payload.value("height", 51);
        wxGetApp().CallAfter([machine_obj, x_dip, y_dip, h_dip]() {
            open_fan_popup(machine_obj, x_dip, y_dip, h_dip);
        });
        return respond(vm, action, 0);
    }

    if (action == ActionNames::axis_move) {
        if (!require()) return missing_machine_response;
        const std::string op = payload.value("op", std::string());
        int result = -1;
        if (op == ActionOps::e_up_10 || op == ActionOps::e_down_10) {
            result = run_extrude(machine_obj, op);
        } else if (op == ActionOps::rack_lift || op == ActionOps::rack_center || op == ActionOps::rack_lower) {
            result = run_rack(machine_obj, op);
        } else {
            result = run_axis(machine_obj, op);
        }
        return respond(vm, action, result == 0 ? 0 : 2, result == 0 ? std::string() : "Axis command failed.");
    }

    if (action == ActionNames::switch_extruder) {
        if (!require()) return missing_machine_response;
        if ((machine_obj->is_in_printing() && !machine_obj->is_in_printing_pause()) ||
            machine_obj->ams_status_main == AMS_STATUS_MAIN_FILAMENT_CHANGE) {
            wxGetApp().CallAfter([]() {
                MessageDialog dlg(nullptr, _L("The printer is busy on other print job"), _L("Error"), wxICON_WARNING | wxOK);
                dlg.ShowModal();
            });
            return respond(vm, action, 0);
        }
        if (machine_obj->GetInfo() && !machine_obj->GetInfo()->IsFdmMode()) {
            wxGetApp().CallAfter([]() {
                MessageDialog dlg(nullptr, _L("Cannot switch extruder when the printer is not at FDM mode"), _L("Warning"), wxICON_WARNING | wxOK);
                dlg.ShowModal();
            });
            return respond(vm, action, 0);
        }
        const int id = payload.value("extruder_id", MAIN_EXTRUDER_ID);
        const int result = machine_obj->GetCtrl()->command_select_extruder(id);
        return respond(vm, action, result == 0 ? 0 : 2, result == 0 ? std::string() : "Switch extruder command failed.");
    }

    if (action == ActionNames::quit_switching) {
        if (!require()) return missing_machine_response;
        SetSwitchingActionHold(machine_obj->get_dev_id());
        machine_obj->command_ams_control("abort");
        return respond(vm, action, 0);
    }

    if (action == ActionNames::retry_switching) {
        if (!require()) return missing_machine_response;
        SetSwitchingActionHold(machine_obj->get_dev_id());
        machine_obj->command_ams_control("resume");
        return respond(vm, action, 0);
    }

    return std::nullopt;
}

} // namespace ControlPanelActionHandler

}} // namespace Slic3r::GUI
