#include "ViewModelActions.hpp"

#include "ViewModel.hpp"

#include "slic3r/GUI/DeviceCore/DevAxis.h"
#include "slic3r/GUI/DeviceCore/DevBed.h"
#include "slic3r/GUI/DeviceCore/DevChamber.h"
#include "slic3r/GUI/DeviceCore/DevCtrl.h"
#include "slic3r/GUI/DeviceCore/DevDefs.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevFan.h"
#include "slic3r/GUI/DeviceCore/DevLamp.h"
#include "slic3r/GUI/DeviceCore/DevInfo.h"
#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/Widgets/FanControl.hpp"

#include <algorithm>
#include <string>
#include <tuple>
#include <unordered_map>
#include "libslic3r/Log.hpp"

namespace Slic3r { namespace GUI {

namespace {

namespace ActionNames = ControlPanelActionNames;
namespace ActionOps = ControlPanelActionOps;

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
    out = respond(vm, action, 3, _u8L("Please select a printer."));
    return false;
}

void open_fan_popup(MachineObject* machine_obj)
{
    if (!machine_obj || !wxGetApp().mainframe) return;
    auto fan = machine_obj->GetFan();
    if (!fan) return;
    if (fan->GetAirDuctData().modes.empty())
        fan->converse_to_duct(true, fan->GetSupportAuxFanData(), fan->GetSupportChamberFan());

    auto* popup = new FanControlPopupNew(wxGetApp().mainframe, machine_obj, fan->GetAirDuctData());
    popup->CentreOnParent();
    popup->ShowModal();
    popup->Destroy();
}

int run_axis(MachineObject* machine_obj, const std::string& op)
{
    if (!machine_obj || !machine_obj->GetAxis()) return -1;
    auto axis = machine_obj->GetAxis();
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
    if (op == ActionOps::xy_home)
        return axis->Ctrl_GoHome();
    const auto it = moves.find(op);
    if (it == moves.end()) return -1;
    return axis->Ctrl_Axis(std::get<0>(it->second), 1.0, std::get<1>(it->second), std::get<2>(it->second));
}

int run_rack(MachineObject* machine_obj, const std::string& op)
{
    if (!machine_obj || !machine_obj->GetNozzleSystem()) return -1;
    auto rack = machine_obj->GetNozzleSystem()->GetNozzleRack();
    if (!rack || !rack->IsSupported() || !rack->IsDirectConnectedRack()) return -1;
    DevNozzleRack::DirectRackPos pos = DevNozzleRack::DirectRackPos::RACK_POS_CENTRE;
    if (op == ActionOps::rack_lift) pos = DevNozzleRack::DirectRackPos::RACK_POS_UP;
    else if (op == ActionOps::rack_lower) pos = DevNozzleRack::DirectRackPos::RACK_POS_DOWN;
    rack->CtrlRackPosMove(pos);
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
        return 2;
    }
    if (machine_obj->is_enable_np)
        return machine_obj->command_extruder_control(ext->GetExtId(), value);
    return machine_obj->GetAxis()->Ctrl_Axis("E", 1.0, value, 900);
}

} // namespace

namespace ControlPanelActionHandler {

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
        int value = payload.value("value", 0);
        if (machine_obj->nozzle_temp_range.size() >= 2)
            value = std::clamp(value, machine_obj->nozzle_temp_range[0], machine_obj->nozzle_temp_range[1]);
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
        value = std::clamp(value, 0, limit);
        machine_obj->command_set_bed(value);
        return respond(vm, action, 0);
    }

    if (action == ActionNames::set_chamber_temp) {
        if (!require()) return missing_machine_response;
        if (!machine_obj->GetChamber() || !machine_obj->GetChamber()->SupportChamberEdit())
            return respond(vm, action, 3, _u8L("Chamber temperature is not editable."));
        int value = payload.value("value", 0);
        value = std::clamp(value, machine_obj->GetChamber()->GetChamberTempEditMin(), machine_obj->GetChamber()->GetChamberTempEditMax());
        machine_obj->GetChamber()->CtrlSetChamberTemp(value);
        return respond(vm, action, 0);
    }

    if (action == ActionNames::set_speed) {
        if (!require()) return missing_machine_response;
        const int level = std::clamp(payload.value("level", 2), 1, 4);
        machine_obj->command_set_printing_speed(static_cast<DevPrintingSpeedLevel>(level));
        return respond(vm, action, 0);
    }

    if (action == ActionNames::toggle_lamp) {
        if (!require()) return missing_machine_response;
        const bool on = payload.value("on", false);
        machine_obj->GetLamp()->CtrlSetChamberLight(on ? DevLamp::LIGHT_EFFECT_ON : DevLamp::LIGHT_EFFECT_OFF);
        return respond(vm, action, 0);
    }

    if (action == ActionNames::open_fan) {
        if (!require()) return missing_machine_response;
        wxGetApp().CallAfter([machine_obj]() { open_fan_popup(machine_obj); });
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
        return respond(vm, action, result == 0 ? 0 : 2, result == 0 ? std::string() : _u8L("Axis command failed."));
    }

    if (action == ActionNames::switch_extruder) {
        if (!require()) return missing_machine_response;
        if (!machine_obj->GetInfo() || !machine_obj->GetInfo()->IsFdmMode())
            return respond(vm, action, 3, _u8L("Cannot switch extruder when the printer is not at FDM mode"));
        const int id = payload.value("extruder_id", MAIN_EXTRUDER_ID);
        const int result = machine_obj->GetCtrl()->command_select_extruder(id);
        return respond(vm, action, result == 0 ? 0 : 2, result == 0 ? std::string() : _u8L("Switch extruder command failed."));
    }

    return std::nullopt;
}

} // namespace ControlPanelActionHandler

}} // namespace Slic3r::GUI
