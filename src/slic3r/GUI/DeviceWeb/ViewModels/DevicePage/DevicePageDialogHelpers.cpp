#include "DevicePageDialogHelpers.h"

#include "slic3r/GUI/AMSDryControl.hpp"
#include "slic3r/GUI/AMSMaterialsSetting.hpp"
#include "slic3r/GUI/AMSRFIDMaterialView.hpp"
#include "slic3r/GUI/EncodedFilament.hpp"
#include "slic3r/GUI/AmsMappingPopup.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"

namespace Slic3r { namespace GUI {

namespace {

// Same cadence as the monitor page refresh (Monitor.cpp REFRESH_INTERVAL).
constexpr int AMS_DRY_CTR_REFRESH_INTERVAL_MS = 1000;

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
        return std::nullopt;
    }

    wxWindow* parent = wxGetApp().mainframe;

    if (DevFilaSystem::IsBBL_Filament(tray.tag_uid)) {
        wxColor color = DevAmsTray::decode_color(tray.color);
        std::vector<wxColour> cols;
        for (const auto& col : tray.cols) cols.push_back(DevAmsTray::decode_color(col));

        wxString k_val = wxString::Format("%.3f", tray.k);

        std::string sn_number;
        if (tray.tag_uid.size() == 16 && tray.tag_uid.substr(12, 2) == "01")
            sn_number = tray.tag_uid;

        wxString color_name;
        if (!tray.setting_id.empty()) {
            if (auto* clr_query = wxGetApp().get_filament_color_code_query()) {
                FilamentColor fila_color;
                if (!cols.empty()) { for (const auto& c : cols) fila_color.AddColor(c); }
                else                 fila_color.AddColor(color);
                fila_color.EndSet(static_cast<int>(tray.ctype));
                color_name = clr_query->GetFilaColorName(wxString::FromUTF8(tray.setting_id), fila_color);
            }
        }

        AMSRFIDMaterialView dlg(parent, wxID_ANY);
        dlg.Popup(machine_obj, ams_id_int, slot_id_int,
                  tray.sub_brands, color_name, color, cols,
                  static_cast<int>(tray.ctype),
                  tray.nozzle_temp_min, tray.nozzle_temp_max,
                  sn_number, k_val);
        return std::nullopt;
    }

    AMSMaterialsSetting dlg(parent, wxID_ANY);
    dlg.obj = machine_obj;
    dlg.ams_id = ams_id_int;
    dlg.slot_id = slot_id_int;
    dlg.m_view_only = view_only;

    std::string filament;
    std::string sn_number;
    std::string temp_max;
    std::string temp_min;
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
    // AMSMaterialsSetting has no filament-weight editing on this branch, so the
    // total / remain weights the dialog would otherwise prefill are not passed.
    dlg.Popup(filament, sn_number, temp_min, temp_max, k_val, n_val);

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

}} // namespace Slic3r::GUI
