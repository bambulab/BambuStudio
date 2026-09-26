#include "wgtAmsControlWebPanel.h"

#include "slic3r/GUI/DeviceWeb/DeviceWebHost.hpp"

#include <wx/sizer.h>

namespace Slic3r::GUI {

namespace {
constexpr int kMinWidthDip  = 586;
// Initial height before the page reports. The host then follows content
// (no-preview machines drop the 44+10 preview band). 340 is the tallest stack:
// preview 44 + gap 10 + unit body 174 + down road 10 + switcher band 16
// + footer 37 + setup hint 37 + 8 page padding + rounding slack.
constexpr int kMinHeightDip = 340;
constexpr const char* kRoute = "/device_page/ams_control_web";
} // namespace

wgtAmsControlWebPanel::wgtAmsControlWebPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    auto* root = new wxBoxSizer(wxVERTICAL);

    SetMinSize(wxSize(FromDIP(kMinWidthDip), FromDIP(kMinHeightDip)));
    m_device_web = new DeviceWebHost(this,
                                     DeviceWebHostMode::DevicePageAmsControlWeb,
                                     kRoute,
                                     /*allow_lazy=*/true);
    m_device_web->SetMinSize(wxSize(FromDIP(kMinWidthDip), FromDIP(kMinHeightDip)));
    m_device_web->SetContentSizeChangedHandler([this](const wxSize& content_size) {
        SetMinSize(content_size);
        SetMaxSize(content_size);
        SetSize(content_size);
        Layout();
        if (auto* parent = GetParent()) {
            parent->Layout();
            parent->SendSizeEvent();
            if (auto* gp = parent->GetParent()) {
                gp->Layout();
                gp->SendSizeEvent();
            }
        }
    });

    root->Add(m_device_web, 0, wxEXPAND, 0);
    SetSizer(root);
    Layout();
}

void wgtAmsControlWebPanel::ShowAndLoad()
{
    const bool was_hidden = !IsShown();
    Show(true);
    // NavigateTo triggers EnsureBuilt on first use, and reloads after Suspend().
    // Skip while already visible so StatusPanel polling does not reload the page.
    if (m_device_web && was_hidden) {
        m_device_web->NavigateTo(kRoute);
    }
}

void wgtAmsControlWebPanel::HideAndSuspend()
{
    Show(false);
    if (m_device_web) {
        m_device_web->Suspend();
    }
}

void wgtAmsControlWebPanel::UpdateByMachine(Slic3r::MachineObject* /*obj*/)
{
    if (m_device_web && IsShownOnScreen()) {
        m_device_web->NotifyAmsControlWebChanged();
    }
}

} // namespace Slic3r::GUI
