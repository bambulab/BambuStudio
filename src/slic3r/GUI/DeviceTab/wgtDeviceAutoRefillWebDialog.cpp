#include "wgtDeviceAutoRefillWebDialog.h"

#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/DeviceWeb/DeviceWebHost.hpp"
#include "slic3r/GUI/AmsMappingPopup.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
// DeviceManager.hpp only forward-declares the manager; OnRefreshTimer() needs
// the definition to call get_selected_machine().
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevFilaAutoRefillMgr.h"

#include <algorithm>
#include <cstdlib>
#include <wx/sizer.h>

namespace Slic3r::GUI {

namespace {
// Opening size taken from the add-refill-filaments design: a 724px content
// column plus the 16px padding of the page root, and 623px of stacked title,
// units and footer. Keeping it in sync with the pages means the dialog does
// not visibly grow once SetContentSizeChangedHandler reports the real size.
constexpr int kDialogWidthDip  = 756;
constexpr int kDialogHeightDip = 623;
constexpr int kMinWidthDip     = 480;
constexpr int kMinHeightDip    = 360;
constexpr int kMaxWidthDip     = 1100;
constexpr int kMaxHeightDip    = 850;
constexpr int kSizeEpsilonDip  = 4;
// Matches the connected-slots dialog: fast enough that an edit looks immediate,
// and the state report is deduplicated so idle ticks are free.
constexpr int kRefreshIntervalMs = 500;

// The dialog is a stack-local modal, so CloseAutoRefillDialog() needs a handle
// to it. Mirrors active_auto_refill_filaments_dialog() in
// DevicePageDialogHelpers.cpp, which does the same for the picker.
wxDialog*& active_auto_refill_dialog()
{
    static wxDialog* dialog = nullptr;
    return dialog;
}
} // namespace

wgtDeviceAutoRefillWebDialog::wgtDeviceAutoRefillWebDialog(wxWindow* parent)
    : DPIDialog(parent, wxID_ANY, _L("Auto-refill"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX | wxRESIZE_BORDER)
{
    SetClientSize(wxSize(FromDIP(kDialogWidthDip), FromDIP(kDialogHeightDip)));
    SetMinClientSize(wxSize(FromDIP(kMinWidthDip), FromDIP(kMinHeightDip)));

    m_device_web = new DeviceWebHost(this,
                                     DeviceWebHostMode::DevicePageAutoRefillRefillInfo,
                                     "/device_page/auto_refill_refill_info");
    m_device_web->SetMinSize(wxSize(FromDIP(kMinWidthDip), FromDIP(kMinHeightDip)));
    m_device_web->SetContentSizeChangedHandler([this](const wxSize& content_size) {
        OnWebContentSizeChanged(content_size);
    });

    auto* root = new wxBoxSizer(wxVERTICAL);
    root->Add(m_device_web, 1, wxEXPAND, 0);
    SetSizer(root);
    Bind(wxEVT_SHOW, &wgtDeviceAutoRefillWebDialog::OnShow, this);
    m_refresh_timer.SetOwner(this);
    Bind(wxEVT_TIMER, &wgtDeviceAutoRefillWebDialog::OnRefreshTimer, this);
    Layout();
    CentreOnParent();
}

wgtDeviceAutoRefillWebDialog::~wgtDeviceAutoRefillWebDialog()
{
    m_refresh_timer.Stop();
}

void wgtDeviceAutoRefillWebDialog::UpdateByMachine(Slic3r::MachineObject* obj)
{
    m_obj = obj;
    if (IsShown()) {
        PushSnapshot();
    }
}

void wgtDeviceAutoRefillWebDialog::OnShow(wxShowEvent& event)
{
    if (event.IsShown()) {
        PushSnapshot();
        m_refresh_timer.Start(kRefreshIntervalMs);
    } else {
        m_refresh_timer.Stop();
    }
    event.Skip();
}

void wgtDeviceAutoRefillWebDialog::OnRefreshTimer(wxTimerEvent&)
{
    if (!m_device_web) {
        return;
    }
    // Re-resolve the machine every tick: it can be switched or dropped while
    // the dialog is open, which would leave the VM holding a stale pointer.
    auto* dev_mgr = wxGetApp().getDeviceManager();
    m_obj = dev_mgr ? dev_mgr->get_selected_machine() : nullptr;
    if (auto* vm = m_device_web->GetAutoRefillRefillInfoVM()) {
        vm->UpdateByMachine(m_obj);
    }
}

void wgtDeviceAutoRefillWebDialog::PushSnapshot()
{
    if (!m_device_web || !m_obj) {
        return;
    }
    // Force-push the latest groups into the (already loaded) WebView so the
    // first shown frame carries this machine's data.
    if (auto* vm = m_device_web->GetAutoRefillRefillInfoVM()) {
        vm->Refresh(m_obj);
    }
}

void wgtDeviceAutoRefillWebDialog::OnWebContentSizeChanged(const wxSize& content_size)
{
    if (!m_device_web) {
        return;
    }

    const wxSize target_size = ClampContentSize(content_size);
    if (IsCloseEnough(m_device_web->GetMinSize(), target_size) &&
        IsCloseEnough(GetMinClientSize(), target_size)) {
        return;
    }

    m_device_web->SetMinSize(target_size);
    SetMinClientSize(target_size);
    Layout();
    SendSizeEvent();
}

wxSize wgtDeviceAutoRefillWebDialog::ClampContentSize(const wxSize& content_size)
{
    const int min_width  = FromDIP(kMinWidthDip);
    const int min_height = FromDIP(kMinHeightDip);
    const int max_width  = FromDIP(kMaxWidthDip);
    const int max_height = FromDIP(kMaxHeightDip);

    return wxSize(std::clamp(content_size.GetWidth(), min_width, max_width),
                  std::clamp(content_size.GetHeight(), min_height, max_height));
}

bool wgtDeviceAutoRefillWebDialog::IsCloseEnough(const wxSize& lhs, const wxSize& rhs)
{
    const int epsilon = FromDIP(kSizeEpsilonDip);
    return std::abs(lhs.GetWidth() - rhs.GetWidth()) <= epsilon &&
           std::abs(lhs.GetHeight() - rhs.GetHeight()) <= epsilon;
}

void wgtDeviceAutoRefillWebDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    SetSize(suggested_rect.GetSize());
    Layout();
}

void ShowAutoRefillDialog(wxWindow* parent, Slic3r::MachineObject* obj)
{
    if (!obj || !obj->GetFilaSystem()) {
        return;
    }

    const bool is_refill_info =
        obj->GetFilaSystem()->GetAutoRefillMgr().GetVersion() ==
        DevFilaAutoRefillMgr::RefillVersion::RefillInfo;

    if (is_refill_info) {
        wgtDeviceAutoRefillWebDialog dlg(parent);
        dlg.UpdateByMachine(obj);
        active_auto_refill_dialog() = &dlg;
        dlg.ShowModal();
        if (active_auto_refill_dialog() == &dlg) {
            active_auto_refill_dialog() = nullptr;
        }
    } else {
        AmsReplaceMaterialDialog dlg(parent);
        dlg.update_machine_obj(obj);
        dlg.ShowModal();
    }
}

void CloseAutoRefillDialog(int result)
{
    wxDialog* dialog = active_auto_refill_dialog();
    if (!dialog) {
        return;
    }

    if (dialog->IsModal()) {
        dialog->EndModal(result);
    } else {
        dialog->Close();
    }
    active_auto_refill_dialog() = nullptr;
}

} // namespace Slic3r::GUI
