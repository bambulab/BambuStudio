/*****************************************************************//**
 * \file   PageFilamentSelect.cpp
 * \brief  Modal wx host of the Web Select Filament dialog.
 *
 * Thin host only: creates the DeviceWebHost, hands the input to the VM and
 * runs the modal loop. No refresh timer (the list is a snapshot) and no
 * content-size handling (fixed design size, same as the native dialog).
 *********************************************************************/
#include "PageFilamentSelect.hpp"

#include "ViewModel.hpp"

#include "slic3r/GUI/DeviceWeb/DeviceWebHost.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_Utils.hpp"
#include "slic3r/GUI/I18N.hpp"

#include <wx/sizer.h>
#include "libslic3r/Log.hpp"

namespace Slic3r { namespace GUI {

namespace {

/// Window size of the native FilamentSelectDialog (outer size, caption included).
constexpr int kDialogWidthDip  = 493;
constexpr int kDialogHeightDip = 556;

class FilamentSelectWebDialog : public DPIDialog
{
public:
    explicit FilamentSelectWebDialog(wxWindow* parent)
        : DPIDialog(parent, wxID_ANY, _L("Select Filament"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
    {
        SetMinSize(FromDIP(wxSize(kDialogWidthDip, kDialogHeightDip)));
        SetSize(FromDIP(wxSize(kDialogWidthDip, kDialogHeightDip)));
    }

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override { SetSize(suggested_rect.GetSize()); }
};

} // namespace

FilamentSelectResult run_filament_select_web_dialog(wxWindow*                             parent,
                                                    const FilamentSelectInput&            in,
                                                    const std::function<void(wxDialog&)>& place)
{
    FilamentSelectResult result;

    FilamentSelectWebDialog dlg(parent);
    auto* host = new DeviceWebHost(&dlg, DeviceWebHostMode::DevicePageFilamentSelect, "/device_page/filament_select");

    auto* vm = host->GetFilamentSelectVM();
    if (!vm) {
        BOOST_LOG_TRIVIAL(error) << "filament select view model missing";
        return result;
    }
    // Close through the dialog's own event queue: never end the modal loop from
    // inside the WebView message callback, and a pending close is dropped
    // together with the dialog if it is already gone.
    vm->Open(in, &result, [&dlg](int code) {
        dlg.CallAfter([&dlg, code]() {
            if (dlg.IsModal()) dlg.EndModal(code);
        });
    });

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(host, 1, wxEXPAND);
    dlg.SetSizer(sizer);
    dlg.Layout();
    // Dark title bar + dark host background (no white flash before the page
    // paints), same as the native FilamentSelectDialog. Run after the host is
    // added so it is recoloured too.
    wxGetApp().UpdateDlgDarkUI(&dlg);
    if (place) place(dlg);

    const int code = dlg.ShowModal();
    if (code != wxID_OK) result = FilamentSelectResult{};
    return result;
}

}} // namespace Slic3r::GUI
