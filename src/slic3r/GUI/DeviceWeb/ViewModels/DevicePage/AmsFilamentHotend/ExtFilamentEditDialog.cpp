/*****************************************************************//**
 * \file   ExtFilamentEditDialog.cpp
 * \brief  Batch external-spool edit dialog of the AMS filament hotend page.
 *********************************************************************/
#include "ExtFilamentEditDialog.hpp"

#include "ViewModel.hpp"

#include "slic3r/GUI/DeviceWeb/DeviceWebHost.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/wxExtensions.hpp"

#include <boost/format.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include <wx/dialog.h>
#include <wx/sizer.h>

namespace Slic3r { namespace GUI {

namespace {

constexpr int kDialogWidthDip = 620;

class ExtFilamentEditDialog : public DPIDialog
{
public:
    explicit ExtFilamentEditDialog(wxWindow* parent, int content_height)
        : DPIDialog(parent, wxID_ANY, _L("Edit Filament"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
    {
        SetClientSize(wxSize(FromDIP(kDialogWidthDip), FromDIP(content_height)));
        SetMinClientSize(wxSize(FromDIP(kDialogWidthDip), FromDIP(content_height)));
    }

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override { SetSize(suggested_rect.GetSize()); }
};

/// Weak handle to the dialog currently on screen, so the web side can close it.
wxDialog*& active_dialog()
{
    static wxDialog* dlg = nullptr;
    return dlg;
}

/// Flattens the slot list into the `ams_id|slot_id|display_index,...` form the
/// page expects in its URL query.
std::string make_slots_param(const nlohmann::json& slots)
{
    std::vector<std::string> parts;
    if (slots.is_array()) {
        for (const auto& s : slots) {
            const std::string ams_id = s.value("ams_id", std::string());
            const std::string slot_id = s.value("slot_id", std::string());
            const int display_index = s.value("display_index", 0);
            if (ams_id.empty() || slot_id.empty() || display_index <= 0) continue;
            parts.push_back((boost::format("%1%|%2%|%3%") % ams_id % slot_id % display_index).str());
        }
    }
    std::string joined;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) joined += ",";
        joined += parts[i];
    }
    return joined;
}

int open_dialog(const nlohmann::json& payload)
{
    wxWindow* parent = wxGetApp().mainframe;
    const std::string intent = payload.value("intent", std::string("edit"));
    const std::string mode = payload.value("mode", std::string(AmsHotendSchema::values::mode::batch_ext_load));
    const nlohmann::json slot_array = payload.value("slots", nlohmann::json::array());
    const nlohmann::json run_slot_array = payload.value("run_slots", nlohmann::json::array());
    const std::string slots = make_slots_param(slot_array);
    const std::string run_slots = make_slots_param(run_slot_array);
    const int slot_count = slot_array.is_array() ? static_cast<int>(slot_array.size()) : 1;
    const bool show_warning = intent == "run";
    const int content_height = std::clamp((show_warning ? 58 : 12) + 40 + std::max(1, slot_count) * 54 + 66, 194, 392);

    ExtFilamentEditDialog dlg(parent, content_height);
    active_dialog() = &dlg;
    const std::string route = (boost::format("/device_page/ams_ext_filament_edit?intent=%1%&mode=%2%&slots=%3%&run_slots=%4%") % intent % mode % slots % run_slots).str();
    auto* host = new DeviceWebHost(&dlg, DeviceWebHostMode::DevicePageAmsFilamentHotend, route);
    host->SetMinSize(wxSize(dlg.FromDIP(kDialogWidthDip), dlg.FromDIP(content_height)));
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(host, 1, wxEXPAND);
    dlg.SetSizer(sizer);
    dlg.Layout();
    const int result = dlg.ShowModal();

    if (active_dialog() == &dlg) active_dialog() = nullptr;
    return result;
}

} // namespace

void close_ext_filament_edit_dialog(int result)
{
    auto* dlg = active_dialog();
    if (!dlg) return;

    if (dlg->IsModal()) {
        dlg->EndModal(result);
    } else {
        dlg->Close();
    }
}

void DevicePageAmsFilamentHotendVM::OpenBatchFilamentEditDialog(const nlohmann::json& payload)
{
    const int result = open_dialog(payload);
    // The dialog closed after the user pressed Auto Load (run committed). This
    // runs on the main-page VM instance, so explicitly push it back to Idle:
    // arm the one-shot flag (so the next focus/poll state.get returns normal)
    // and report immediately so the M2Y AMS page leaves batch selection.
    if (result == wxID_APPLY) {
        m_active_mode = AmsHotendSchema::values::mode::normal;
        m_pending_exit_batch = true;
        ReportState("state", "changed");
    }
}

}} // namespace Slic3r::GUI
