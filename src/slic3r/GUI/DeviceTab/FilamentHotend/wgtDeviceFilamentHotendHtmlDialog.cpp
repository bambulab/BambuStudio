#include "wgtDeviceFilamentHotendHtmlDialog.h"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/DeviceWeb/DeviceWebHost.hpp"

#include <algorithm>
#include <cstdlib>
#include <wx/sizer.h>

namespace Slic3r::GUI {

namespace {
// Default opening size closely matched to the content geometry (~580x440 DIP),
// consistent with the embedded wgtDeviceFilamentHotendWebPanel and the design.
constexpr int kDialogWidthDip  = 640;
constexpr int kDialogHeightDip = 460;
constexpr int kMinWidthDip     = 560;
constexpr int kMinHeightDip    = 380;
constexpr int kMaxWidthDip     = 1100;
constexpr int kMaxHeightDip    = 850;
constexpr int kSizeEpsilonDip  = 4;

// Resources-relative entry of the native HTML front-end.
constexpr const char* kHtmlEntryFile = "web/device_page_html/ams_hotend.html";
constexpr const char* kEnableKey     = "enable_device_web_html";
} // namespace

bool wgtDeviceFilamentHotendHtmlDialog::IsEnabled()
{
    auto* config = wxGetApp().app_config;
    return config && config->get(kEnableKey) == "true";
}

wgtDeviceFilamentHotendHtmlDialog::wgtDeviceFilamentHotendHtmlDialog(wxWindow* parent)
    : DPIDialog(parent, wxID_ANY, _L("Hotend & Filament") + " (HTML)", wxDefaultPosition, wxDefaultSize,
                wxCAPTION | wxCLOSE_BOX | wxRESIZE_BORDER)
{
    SetClientSize(wxSize(FromDIP(kDialogWidthDip), FromDIP(kDialogHeightDip)));
    SetMinClientSize(wxSize(FromDIP(kMinWidthDip), FromDIP(kMinHeightDip)));

    // Same mode as the React dialog: the ViewModel chain is reused as-is, only the
    // document entry differs.
    m_device_web = new DeviceWebHost(this,
                                     DeviceWebHostMode::DevicePageAmsFilamentHotend,
                                     "/device_page/ams_filament_hotend",
                                     /*allow_lazy=*/false,
                                     kHtmlEntryFile);
    m_device_web->SetMinSize(wxSize(FromDIP(kMinWidthDip), FromDIP(kMinHeightDip)));
    m_device_web->SetContentSizeChangedHandler([this](const wxSize& content_size) {
        OnWebContentSizeChanged(content_size);
    });

    auto* root = new wxBoxSizer(wxVERTICAL);
    root->Add(m_device_web, 1, wxEXPAND, 0);
    SetSizer(root);
    Bind(wxEVT_SHOW, &wgtDeviceFilamentHotendHtmlDialog::OnShow, this);
    m_poll_timer.Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
        UpdateByMachine(m_obj);
    });
    Layout();
    CentreOnParent();
}

void wgtDeviceFilamentHotendHtmlDialog::UpdateByMachine(Slic3r::MachineObject* obj)
{
    m_obj = obj;
    if (!IsShown()) {
        return;
    }

    if (m_device_web) {
        m_device_web->NotifyDeviceFilamentChanged();
    }
}

void wgtDeviceFilamentHotendHtmlDialog::OnShow(wxShowEvent& event)
{
    if (event.IsShown()) {
        m_first_content_size_received = false;
        UpdateByMachine(m_obj);
        m_poll_timer.Start(1000);
    } else {
        m_poll_timer.Stop();
    }
    event.Skip();
}

void wgtDeviceFilamentHotendHtmlDialog::OnWebContentSizeChanged(const wxSize& content_size)
{
    if (!m_device_web) {
        return;
    }

    const wxSize clamped = ClampContentSize(content_size);
    // The dialog allows free user resizing between [kMinWidthDip, kMaxWidthDip].
    // Never ratchet the minimum width up when the user drags the window wider,
    // otherwise the window can never be shrunk back horizontally.
    const int min_width  = FromDIP(kMinWidthDip);
    const int min_height = std::max(FromDIP(kMinHeightDip), clamped.GetHeight());
    const wxSize min_size(min_width, min_height);

    if (m_device_web->GetMinSize() != min_size) {
        m_device_web->SetMinSize(min_size);
    }
    if (GetMinClientSize() != min_size) {
        SetMinClientSize(min_size);
    }

    if (!m_first_content_size_received) {
        m_first_content_size_received = true;
        // On the very first report, fit the dialog snugly around the content and centre it.
        SetClientSize(clamped);
        Layout();
        CentreOnParent();
    } else {
        Layout();
        SendSizeEvent();
    }
}

wxSize wgtDeviceFilamentHotendHtmlDialog::ClampContentSize(const wxSize& content_size)
{
    const int min_width  = FromDIP(kMinWidthDip);
    const int min_height = FromDIP(kMinHeightDip);
    const int max_width  = FromDIP(kMaxWidthDip);
    const int max_height = FromDIP(kMaxHeightDip);

    return wxSize(std::clamp(content_size.GetWidth(), min_width, max_width),
                  std::clamp(content_size.GetHeight(), min_height, max_height));
}

bool wgtDeviceFilamentHotendHtmlDialog::IsCloseEnough(const wxSize& lhs, const wxSize& rhs)
{
    const int epsilon = FromDIP(kSizeEpsilonDip);
    return std::abs(lhs.GetWidth() - rhs.GetWidth()) <= epsilon &&
           std::abs(lhs.GetHeight() - rhs.GetHeight()) <= epsilon;
}

void wgtDeviceFilamentHotendHtmlDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    SetSize(suggested_rect.GetSize());
    Layout();
}

} // namespace Slic3r::GUI
