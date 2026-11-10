#include "ViewModelActions.hpp"

#include "ViewModel.hpp"

#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/DeviceTab/wgtDeviceNozzleRackUpdate.h"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/MsgDialog.hpp"

#include <memory>

namespace Slic3r { namespace GUI {

namespace {

nlohmann::json respond(DevicePageHotendRackVM& vm, const std::string& action, int code, const std::string& message = {})
{
    vm.InvalidateReportCache();
    return vm.MakeActionResponse(action, code, message, vm.BuildState());
}

bool require_ab_rack(DevicePageHotendRackVM& vm,
                     const std::string& action,
                     MachineObject* machine_obj,
                     std::shared_ptr<DevNozzleRack>& rack,
                     nlohmann::json& out)
{
    rack = machine_obj && machine_obj->GetNozzleSystem() ? machine_obj->GetNozzleRack() : nullptr;
    if (machine_obj && rack && rack->IsSupported() && rack->GetRackType() == DevNozzleRack::RackType_AB)
        return true;
    out = respond(vm, action, 3, _u8L("Hotend rack is currently unavailable."));
    return false;
}

void open_hotends_info_dialog(DevicePageHotendRackVM& vm, std::shared_ptr<DevNozzleRack> rack)
{
    if (!rack || !wxGetApp().mainframe) return;
    wgtDeviceNozzleRackUpgradeDlg dlg(wxGetApp().mainframe, rack);
    vm.SetHotendsInfoDialog(&dlg);
    dlg.ShowModal();
    vm.SetHotendsInfoDialog(nullptr);
}

} // namespace

namespace HotendRackActionHandler {

std::optional<nlohmann::json> TryHandle(DevicePageHotendRackVM& vm,
                                        const std::string& action,
                                        const nlohmann::json& payload,
                                        MachineObject* machine_obj)
{
    std::shared_ptr<DevNozzleRack> rack;
    nlohmann::json unavailable_response;
    if (!require_ab_rack(vm, action, machine_obj, rack, unavailable_response))
        return unavailable_response;

    if (action == "move_row") {
        if (rack->GetReadingCount() > 0)
            return respond(vm, action, 2, _u8L("Hotend rack is reading."));
        if (rack->GetStatus() != DevNozzleRack::RACK_STATUS_IDLE &&
            rack->GetStatus() != DevNozzleRack::RACK_STATUS_UNKNOWN)
            return respond(vm, action, 2, _u8L("Hotend rack is busy."));
        const std::string row = payload.value("row", std::string("A"));
        rack->CtrlRackPosMove(row == "B" ? DevNozzleRack::RACK_POS_B_TOP : DevNozzleRack::RACK_POS_A_TOP);
        return respond(vm, action, 0);
    }

    if (action == "home") {
        rack->CtrlRackPosGoHome();
        return respond(vm, action, 0);
    }

    if (action == "read_all") {
        if (!rack->CtrlCanReadAll())
            return respond(vm, action, 2, _u8L("Hotend rack cannot read all."));
        rack->CtrlRackReadAll(true);
        return respond(vm, action, 0);
    }

    if (action == "open_hotends_info") {
        wxGetApp().CallAfter([&vm, rack]() { open_hotends_info_dialog(vm, rack); });
        return respond(vm, action, 0);
    }

    if (action == "show_error_detail") {
        wxGetApp().CallAfter([]() {
            MessageDialog dlg(nullptr, _L("The hotend is in an abnormal state and currently unavailable. "
                "Please go to 'Device -> Upgrade' to upgrade firmware."), _L("Abnormal Hotend"), wxICON_WARNING);
            dlg.AddButton(wxID_CANCEL, _L("Cancel"), false);
            dlg.AddButton(wxID_OK, _L("Jump to the upgrade page"), true);
            if (dlg.ShowModal() == wxID_OK && wxGetApp().mainframe)
                wxGetApp().mainframe->m_monitor->jump_to_Upgrade();
        });
        return respond(vm, action, 0);
    }

    return std::nullopt;
}

} // namespace HotendRackActionHandler

}} // namespace Slic3r::GUI
