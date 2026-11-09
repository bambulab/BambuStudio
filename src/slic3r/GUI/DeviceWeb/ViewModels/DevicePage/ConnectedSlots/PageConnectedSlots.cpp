/*****************************************************************//**
 * \file   PageConnectedSlots.cpp
 * \brief  Modal host window for the Connected Slots page.
 *********************************************************************/
#include "PageConnectedSlots.hpp"

#include "ViewModel.hpp"

#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/DeviceCore/DevManager.h"

#include "slic3r/GUI/DeviceWeb/DeviceWebHost.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"

#include <string>

#include <wx/sizer.h>

namespace Slic3r { namespace GUI {

namespace {

/// Fixed design size of the dialog; the page is not resizable.
constexpr int kDialogWidthDip  = 560;
constexpr int kDialogHeightDip = 360;

/// Device pushes arrive roughly once per second; half that period bounds the
/// display lag without paying for BuildState() far more often than needed.
constexpr int kRefreshIntervalMs = 500;

MachineObject* get_selected_machine()
{
    auto* dev_mgr = wxGetApp().getDeviceManager();
    return dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
}

} // namespace

ConnectedSlotsDialog::ConnectedSlotsDialog(wxWindow* parent)
    : DPIDialog(parent, wxID_ANY, _L("Connected Slots"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    SetClientSize(wxSize(FromDIP(kDialogWidthDip), FromDIP(kDialogHeightDip)));
    SetMinClientSize(wxSize(FromDIP(kDialogWidthDip), FromDIP(kDialogHeightDip)));

    m_refresh_timer.SetOwner(this);
    Bind(wxEVT_TIMER, &ConnectedSlotsDialog::OnRefreshTimer, this);
    // Stop the pump before teardown starts: a queued tick must never reach a VM
    // whose host webview is already going away.
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& evt) {
        m_refresh_timer.Stop();
        evt.Skip();
    });
    Bind(wxEVT_SHOW, [this](wxShowEvent& evt) {
        if (!evt.IsShown()) m_refresh_timer.Stop();
        evt.Skip();
    });
}

ConnectedSlotsDialog::~ConnectedSlotsDialog()
{
    m_refresh_timer.Stop();
}

void ConnectedSlotsDialog::StartRefreshPump(ConnectedSlotsVM* vm)
{
    m_vm = vm;
    if (m_vm) m_refresh_timer.Start(kRefreshIntervalMs);
}

void ConnectedSlotsDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    SetSize(suggested_rect.GetSize());
}

void ConnectedSlotsDialog::OnRefreshTimer(wxTimerEvent&)
{
    if (!m_vm) return;
    // Re-resolve the machine every tick: it can be switched or dropped while the
    // dialog is open, which would leave the VM holding a stale pointer.
    m_vm->UpdateByMachine(get_selected_machine());
}

void open_connected_slots_dialog(const nlohmann::json& payload)
{
    MachineObject* machine_obj = get_selected_machine();
    if (!machine_obj) return;

    const int addr_id = payload.value("addr_id", -1);
    const std::string route = "/device_page/connected_slots?addr_id=" + std::to_string(addr_id);

    ConnectedSlotsDialog dlg(wxGetApp().mainframe);
    auto* host = new DeviceWebHost(&dlg, DeviceWebHostMode::DevicePageConnectedSlots, route);
    host->SetMinSize(wxSize(dlg.FromDIP(kDialogWidthDip), dlg.FromDIP(kDialogHeightDip)));
    if (auto* vm = host->GetConnectedSlotsVM()) {
        vm->Refresh(machine_obj, addr_id);
        dlg.StartRefreshPump(vm);
    }

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(host, 1, wxEXPAND);
    dlg.SetSizer(sizer);
    dlg.Layout();
    dlg.ShowModal();
}

}} // namespace Slic3r::GUI
