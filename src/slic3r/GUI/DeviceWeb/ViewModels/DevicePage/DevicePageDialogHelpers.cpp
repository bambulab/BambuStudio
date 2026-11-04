#include "DevicePageDialogHelpers.h"

#include "slic3r/GUI/AMSDryControl.hpp"
#include "slic3r/GUI/AMSMaterialsSetting.hpp"
#include "slic3r/GUI/EncodedFilament.hpp"
#include "slic3r/GUI/AmsMappingPopup.hpp"
#include "slic3r/GUI/DeviceWeb/DeviceWebHost.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/Monitor.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"

#include <wx/sizer.h>
#include "libslic3r/Log.hpp"

namespace Slic3r { namespace GUI {

namespace {

// Same cadence as the monitor page refresh (Monitor.cpp REFRESH_INTERVAL).
constexpr int AMS_DRY_CTR_REFRESH_INTERVAL_MS = 1000;
constexpr int kAutoRefillPickerWidthDip        = 756;
constexpr int kAutoRefillPickerHeightDip       = 623;

wxDialog*& active_auto_refill_filaments_dialog()
{
    static wxDialog* dialog = nullptr;
    return dialog;
}

class AutoRefillFilamentsDialog : public DPIDialog
{
public:
    AutoRefillFilamentsDialog(wxWindow* parent, const wxString& title)
        : DPIDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize,
                    wxCAPTION | wxCLOSE_BOX | wxRESIZE_BORDER)
    {
        SetClientSize(wxSize(FromDIP(kAutoRefillPickerWidthDip), FromDIP(kAutoRefillPickerHeightDip)));
        SetMinClientSize(wxSize(FromDIP(kAutoRefillPickerWidthDip), FromDIP(kAutoRefillPickerHeightDip)));
    }

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override { SetSize(suggested_rect.GetSize()); }
};

// Feeds the ownerless drying dialog with the state of the selected machine,
// which is what AMSControl::UpdateAmsDryControl does for the AMSControl-owned
// instance. Notify() is overridden instead of posting wxEVT_TIMER because
// AMSDryCtrWin binds its progress timer with wxID_ANY and would swallow the
// event.
class AmsDryCtrRefreshTimer : public wxTimer
{
public:
    explicit AmsDryCtrRefreshTimer(AMSDryCtrWin* dlg) : m_dlg(dlg) {}

    void Notify() override
    {
        auto*          dev_mgr     = wxGetApp().getDeviceManager();
        MachineObject* machine_obj = dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
        std::shared_ptr<DevFilaSystem> fila_system = machine_obj ? machine_obj->GetFilaSystem() : nullptr;
        if (!fila_system) {
            m_dlg->Close();
            return;
        }

        m_dlg->update(fila_system, machine_obj);
    }

private:
    AMSDryCtrWin* m_dlg;
};

} // namespace

wxWindow* ResolveAutoRefillDialogParent()
{
    if (auto* active = wxDynamicCast(wxGetActiveWindow(), wxDialog); active && active->IsModal()) {
        return active;
    }
    for (auto node = wxTopLevelWindows.GetLast(); node; node = node->GetPrevious()) {
        if (auto* dialog = wxDynamicCast(node->GetData(), wxDialog);
            dialog && dialog->IsShown() && dialog->IsModal()) {
            return dialog;
        }
    }
    return wxGetApp().mainframe;
}

std::optional<EditedFilamentInfo> OpenAmsMaterialsSetting(const std::string& ams_id, const std::string& slot_id, bool view_only)
{
    auto* dev_mgr = wxGetApp().getDeviceManager();
    MachineObject* machine_obj = dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
    if (!machine_obj || ams_id.empty() || slot_id.empty()) return std::nullopt;
    if (!machine_obj->get_tray(ams_id, slot_id).has_value()) return std::nullopt;

    auto tray = machine_obj->get_tray(ams_id, slot_id).value();

    int ams_id_int = 0;
    int slot_id_int = 0;
    try {
        ams_id_int = std::stoi(ams_id);
        slot_id_int = std::stoi(slot_id);
    } catch (...) {
        BOOST_LOG_TRIVIAL(warning) << current_exception_message();
        return std::nullopt;
    }

    wxWindow* parent = wxGetApp().mainframe;

    AMSMaterialsSetting dlg(parent, wxID_ANY);
    dlg.obj = machine_obj;
    dlg.ams_id = ams_id_int;
    dlg.slot_id = slot_id_int;
    dlg.m_view_only = view_only;

    std::string filament;
    std::string sn_number;
    std::string temp_max;
    std::string temp_min;
    wxString total_weight;
    wxString remain_weight;
    wxString k_val;
    wxString n_val;

    k_val = wxString::Format("%.3f", tray.k);
    n_val = wxString::Format("%.3f", tray.n);
    wxColor color = DevAmsTray::decode_color(tray.color);

    std::vector<wxColour> cols;
    for (const auto& col : tray.cols) {
        cols.push_back(DevAmsTray::decode_color(col));
    }

    dlg.set_ctype(tray.ctype);
    dlg.ams_filament_id = tray.setting_id;
    if (dlg.ams_filament_id.empty()) {
        dlg.set_empty_color(color);
    } else {
        dlg.set_color(color);
        dlg.set_colors(cols);
    }

    dlg.m_is_third = !DevFilaSystem::IsBBL_Filament(tray.tag_uid);

    if (tray.tag_uid.size() == 16 && tray.tag_uid.substr(12, 2) == "01") {
        sn_number = tray.tag_uid;
    }
    if (!dlg.m_is_third) {
        filament = tray.sub_brands;
        temp_max = tray.nozzle_temp_max;
        temp_min = tray.nozzle_temp_min;
    }
    if (tray.weight != "-1" && tray.weight != "0") {
        total_weight = wxString::FromUTF8(tray.weight.c_str());
    }
    if (auto weight = tray.get_filament_remain_weight()) {
        remain_weight = wxString::Format("%d", *weight);
    }
    dlg.Popup(filament, sn_number, temp_min, temp_max, k_val, n_val, total_weight, remain_weight);

    if (!dlg.m_confirmed) return std::nullopt;

    EditedFilamentInfo info;
    info.ams_id        = ams_id;
    info.slot_id       = slot_id;
    info.filament_type = dlg.m_filament_type;
    info.filament_id   = dlg.ams_filament_id;
    info.setting_id    = dlg.ams_setting_id;
    if (dlg.m_clr_picker) {
        info.color  = dlg.m_clr_picker->m_colour;
        info.colors = dlg.m_clr_picker->m_cols;
        info.ctype  = dlg.m_clr_picker->ctype;
    }
    return info;
}

bool BlockLoadForUnknownFilament(MachineObject* machine_obj, const std::string& ams_id, const std::string& slot_id)
{
    if (!machine_obj || ams_id.empty() || slot_id.empty()) return true;

    const auto target_tray = machine_obj->get_tray(ams_id, slot_id);
    const bool is_unknown = !target_tray.has_value() ||
                            target_tray->setting_id.empty() ||
                            target_tray->m_fila_type.empty();
    if (!is_unknown) return false;

    // Queue the modal so bridge/request handlers can return immediately.
    wxGetApp().CallAfter([]() {
        MessageDialog msg_dlg(nullptr,
                              _L("Filament type is unknown which is required to perform this action. Please set target filament's information."),
                              wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
    });
    return true;
}

void OpenAmsAutoRefillDialog()
{
    auto*          dev_mgr     = wxGetApp().getDeviceManager();
    MachineObject* machine_obj = dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
    wxWindow*      parent      = wxGetApp().mainframe;
    if (!machine_obj || !parent) return;

    AmsReplaceMaterialDialog dlg(parent);
    dlg.update_machine_obj(machine_obj);
    dlg.ShowModal();
}

void OpenAmsDryControlDialog(MachineObject* machine_obj, const std::string& ams_id)
{
    wxWindow* parent = wxGetApp().mainframe;
    if (!machine_obj || !parent || ams_id.empty()) return;

    auto fila_system = machine_obj->GetFilaSystem();
    if (!fila_system) return;

    AMSDryCtrWin dlg(parent);
    dlg.set_ams_id(ams_id);
    dlg.update(fila_system, machine_obj);

    AmsDryCtrRefreshTimer refresh_timer(&dlg);
    refresh_timer.Start(AMS_DRY_CTR_REFRESH_INTERVAL_MS);
    dlg.ShowModal();
    refresh_timer.Stop();
}

void OpenAutoRefillFilamentsDialog(MachineObject*                     machine_obj,
                                   AutoRefillFilamentsDialogMode      mode,
                                   const std::optional<DevAmsSlotId>& group_head)
{
    if (!machine_obj || (mode == AutoRefillFilamentsDialogMode::AddToGroup && !group_head.has_value()))
        return;
    if (active_auto_refill_filaments_dialog())
        return;

    wxWindow* parent = ResolveAutoRefillDialogParent();
    std::string route = "/device_page/auto_refill_refill_info?picker=1&mode=";
    route += mode == AutoRefillFilamentsDialogMode::AddToGroup ? "add_to_group" : "create_group";
    if (group_head.has_value()) {
        route += "&ams_id=" + std::to_string(group_head->first);
        route += "&slot_id=" + std::to_string(group_head->second);
    }

    const wxString title = mode == AutoRefillFilamentsDialogMode::AddToGroup
                               ? _L("Add refill filament")
                               : _L("Add material group");
    AutoRefillFilamentsDialog dialog(parent, title);
    active_auto_refill_filaments_dialog() = &dialog;

    auto* host = new DeviceWebHost(&dialog, DeviceWebHostMode::DevicePageAutoRefillRefillInfo, route);
    host->SetMinSize(wxSize(dialog.FromDIP(kAutoRefillPickerWidthDip),
                            dialog.FromDIP(kAutoRefillPickerHeightDip)));
    if (auto* vm = host->GetAutoRefillRefillInfoVM())
        vm->Refresh(machine_obj);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(host, 1, wxEXPAND);
    dialog.SetSizer(sizer);
    dialog.Layout();
    dialog.CentreOnParent();
    dialog.ShowModal();

    if (active_auto_refill_filaments_dialog() == &dialog)
        active_auto_refill_filaments_dialog() = nullptr;
}

void CloseAutoRefillFilamentsDialog(int result)
{
    wxDialog* dialog = active_auto_refill_filaments_dialog();
    if (!dialog)
        return;
    if (dialog->IsModal())
        dialog->EndModal(result);
    else
        dialog->Close();
}

void OpenHmsPanel()
{
    auto* main_frame = wxGetApp().mainframe;
    if (!main_frame)
        return;
    main_frame->jump_to_monitor();
    if (main_frame->m_monitor)
        main_frame->m_monitor->jump_to_HMS();
}

}} // namespace Slic3r::GUI
