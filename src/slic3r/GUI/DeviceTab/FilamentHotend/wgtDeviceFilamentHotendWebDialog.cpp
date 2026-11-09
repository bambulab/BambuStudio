#include "wgtDeviceFilamentHotendWebDialog.h"

#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/DeviceWeb/DeviceWebHost.hpp"

#include <algorithm>
#include <cstdlib>
#include <wx/sizer.h>

namespace Slic3r::GUI {

namespace {
constexpr int kDialogWidthDip  = 900;
constexpr int kDialogHeightDip = 700;
constexpr int kMinWidthDip     = 578;
constexpr int kMinHeightDip    = 352;
constexpr int kMaxWidthDip     = 1100;
constexpr int kMaxHeightDip    = 850;
constexpr int kSizeEpsilonDip  = 4;
} // namespace

wgtDeviceFilamentHotendWebDialog::wgtDeviceFilamentHotendWebDialog(wxWindow* parent)
    : DPIDialog(parent, wxID_ANY, _L("Hotend & Filament"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX | wxRESIZE_BORDER)
{
    SetClientSize(wxSize(FromDIP(kDialogWidthDip), FromDIP(kDialogHeightDip)));
    SetMinClientSize(wxSize(FromDIP(kMinWidthDip), FromDIP(kMinHeightDip)));

    m_device_web = new DeviceWebHost(this,
                                     DeviceWebHostMode::DevicePageAmsFilamentHotend,
                                     "/device_page/ams_filament_hotend");
    m_device_web->SetMinSize(wxSize(FromDIP(kMinWidthDip), FromDIP(kMinHeightDip)));
    m_device_web->SetContentSizeChangedHandler([this](const wxSize& content_size) {
        OnWebContentSizeChanged(content_size);
    });

    auto* root = new wxBoxSizer(wxVERTICAL);
    root->Add(m_device_web, 1, wxEXPAND, 0);
    SetSizer(root);
    Bind(wxEVT_SHOW, &wgtDeviceFilamentHotendWebDialog::OnShow, this);
    Layout();
    CentreOnParent();
}

void wgtDeviceFilamentHotendWebDialog::UpdateByMachine(Slic3r::MachineObject* obj)
{
    m_obj = obj;
    if (!IsShown()) {
        return;
    }

    if (m_device_web) {
        m_device_web->NotifyDeviceFilamentChanged();
    }
}

void wgtDeviceFilamentHotendWebDialog::OnShow(wxShowEvent& event)
{
    if (event.IsShown()) {
        UpdateByMachine(m_obj);
    }
    event.Skip();
}

void wgtDeviceFilamentHotendWebDialog::OnWebContentSizeChanged(const wxSize& content_size)
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

wxSize wgtDeviceFilamentHotendWebDialog::ClampContentSize(const wxSize& content_size)
{
    const int min_width  = FromDIP(kMinWidthDip);
    const int min_height = FromDIP(kMinHeightDip);
    const int max_width  = FromDIP(kMaxWidthDip);
    const int max_height = FromDIP(kMaxHeightDip);

    return wxSize(std::clamp(content_size.GetWidth(), min_width, max_width),
                  std::clamp(content_size.GetHeight(), min_height, max_height));
}

bool wgtDeviceFilamentHotendWebDialog::IsCloseEnough(const wxSize& lhs, const wxSize& rhs)
{
    const int epsilon = FromDIP(kSizeEpsilonDip);
    return std::abs(lhs.GetWidth() - rhs.GetWidth()) <= epsilon &&
           std::abs(lhs.GetHeight() - rhs.GetHeight()) <= epsilon;
}

void wgtDeviceFilamentHotendWebDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    SetSize(suggested_rect.GetSize());
    Layout();
}

} // namespace Slic3r::GUI
