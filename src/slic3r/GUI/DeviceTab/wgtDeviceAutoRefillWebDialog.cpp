#include "wgtDeviceAutoRefillWebDialog.h"

#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/DeviceWeb/DeviceWebHost.hpp"
#include "slic3r/GUI/AmsMappingPopup.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevFilaAutoRefillMgr.h"

#include <algorithm>
#include <cstdlib>
#include <wx/sizer.h>

namespace Slic3r::GUI {

namespace {
constexpr int kDialogWidthDip  = 640;
constexpr int kDialogHeightDip = 360;
constexpr int kMinWidthDip     = 480;
constexpr int kMinHeightDip    = 360;
constexpr int kMaxWidthDip     = 1100;
constexpr int kMaxHeightDip    = 850;
constexpr int kSizeEpsilonDip  = 4;
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
    Layout();
    CentreOnParent();
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
    }
    event.Skip();
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
        dlg.ShowModal();
    } else {
        AmsReplaceMaterialDialog dlg(parent);
        dlg.update_machine_obj(obj);
        dlg.ShowModal();
    }
}

} // namespace Slic3r::GUI
