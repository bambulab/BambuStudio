#include "FilamentManualPullDialog.hpp"

#include "BuilderFilamentManualPullStatus.hpp"
#include "Schema.hpp"

#include "slic3r/GUI/DeviceCore/DevDefs.h"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/DeviceWeb/DeviceWebHost.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/wxExtensions.hpp"

#include <wx/dialog.h>
#include <wx/sizer.h>

namespace Slic3r { namespace GUI {

namespace {

const wxString kFilamentManualPullDialogName = "filament_manual_pull_dialog";

class FilamentManualPullDialog : public DPIDialog
{
public:
    explicit FilamentManualPullDialog(wxWindow* parent)
        : DPIDialog(parent, wxID_ANY, _L("Filament not removed"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
    {
        SetName(kFilamentManualPullDialogName);
        SetClientSize(wxSize(FromDIP(636), FromDIP(358)));
        SetMinClientSize(wxSize(FromDIP(520), FromDIP(300)));
    }

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override { SetSize(suggested_rect.GetSize()); }
};

} // namespace

void open_filament_manual_pull_dialog()
{
    auto* dev_mgr = wxGetApp().getDeviceManager();
    MachineObject* machine_obj = dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
    if (!machine_obj) return;

    wxWindow* parent = wxGetApp().mainframe;
    FilamentManualPullDialog dlg(parent);
    auto* host = new DeviceWebHost(&dlg, DeviceWebHostMode::DevicePageAmsFilamentHotend, "/device_page/filament_manual_pull");
    host->SetMinSize(wxSize(dlg.FromDIP(520), dlg.FromDIP(300)));
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(host, 1, wxEXPAND);
    dlg.SetSizer(sizer);
    dlg.Layout();
    dlg.ShowModal();
}

void close_filament_manual_pull_dialog()
{
    wxWindow* window = wxWindow::FindWindowByName(kFilamentManualPullDialogName);
    if (auto* dialog = dynamic_cast<wxDialog*>(window)) {
        if (dialog->IsModal()) {
            dialog->EndModal(wxID_OK);
        } else {
            dialog->Close();
        }
    }
}

enum class ManualPullExitConfirmKind
{
    Simple,
    Risk
};

ManualPullExitConfirmKind resolve_manual_pull_exit_confirm_kind(MachineObject* machine_obj)
{
    if (!machine_obj) {
        return ManualPullExitConfirmKind::Risk;
    }

    if (machine_obj->GetNozzleRack()->GetRackType() == DevNozzleRack::RackType_B || machine_obj->GetNozzleRack()->GetRackType() == DevNozzleRack::RackType_C) {
        return ManualPullExitConfirmKind::Risk;
    }

    namespace SK = AmsHotendSchema::keys;
    namespace SV = AmsHotendSchema::values;

    const nlohmann::json status = DevicePageFilamentManualPullStatusBuilder::Build(machine_obj);
    const auto& items = status.value(SK::filament_manual_pull::items, nlohmann::json::array());

    for (const auto& item : items) {
        if (item.value(SK::filament_manual_pull_item::state, std::string()) != SV::filament_manual_pull_item_state::pending) {
            continue;
        }

        const int display_index = item.value(SK::filament_manual_pull_item::display_index, 0);
        const std::string ams_id = item.value(SK::filament_manual_pull_item::ams_id, std::string());
        if (auto tray_opt = machine_obj->GetFilaSystem()->GetVirtualTray(ams_id); tray_opt.has_value()) {
            return ManualPullExitConfirmKind::Risk;
        }
    }

    return ManualPullExitConfirmKind::Simple;
}

wxString manual_pull_exit_message(ManualPullExitConfirmKind kind)
{
    if (kind == ManualPullExitConfirmKind::Simple) {
        return _L("Confirm to exit this process?");
    }

    return _L("Confirm to exit this process?") + "\n\n"
        + _L("If the filament is not pulled out, subsequent printing may pause due to filament switching. Please operate carefully.");
}

wxString manual_pull_exit_ok_label(ManualPullExitConfirmKind kind)
{
    return kind == ManualPullExitConfirmKind::Simple ? _L("Exit") : _L("Still exit");
}

void confirm_exit_filament_manual_pull(MachineObject* machine_obj)
{
    if (!machine_obj) return;

    const ManualPullExitConfirmKind kind = resolve_manual_pull_exit_confirm_kind(machine_obj);

    MessageDialog dlg(wxGetApp().mainframe, manual_pull_exit_message(kind), _L("Tip"), wxOK | wxCANCEL | wxICON_WARNING);
    dlg.SetButtonLabel(wxID_CANCEL, _L("Cancel"));
    dlg.SetButtonLabel(wxID_OK, manual_pull_exit_ok_label(kind));
    if (dlg.ShowModal() == wxID_OK) {
        machine_obj->command_ams_control("abort");
    }
}

}} // namespace Slic3r::GUI
