#include "wgtDeviceFilamentHotendTab.h"

#include "wgtDeviceAmsFilaments.h"
#include "wgtDeviceFilamentPanel.h"
#include "wgtDeviceFilamentHotendTabHeader.h"
#include "wgtDeviceFilamentHotendTabLoadTips.h"
#include "wgtDeviceHotentPanel.h"

#include "slic3r/GUI/AMSSetting.hpp"
#include "slic3r/GUI/AmsMappingPopup.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/Widgets/Button.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"
#include "slic3r/GUI/I18N.hpp"

#include "slic3r/GUI/DeviceCore/DevDefs.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceManager.hpp"

#include <wx/simplebook.h>
#include <wx/sizer.h>

namespace Slic3r::GUI {

namespace {

constexpr auto kBatchStartTimeout = std::chrono::seconds(15);
constexpr auto kBatchFinishTimeout = std::chrono::minutes(8);

}

wgtDeviceFilamentHotendTab::wgtDeviceFilamentHotendTab(wxWindow *parent) : wxPanel(parent, wxID_ANY) { CreateGui(); }

void wgtDeviceFilamentHotendTab::CreateGui()
{
    wxBoxSizer *root = new wxBoxSizer(wxVERTICAL);

    m_tab_header = new wgtDeviceFilamentHotendTabHeader(this);
    m_tab_header->Bind(EVT_DEVICE_FILAMENT_HOTEND_TAB_CHANGED, &wgtDeviceFilamentHotendTab::OnHeaderTabChanged, this);
    root->Add(m_tab_header, 0, wxEXPAND, 0);

    m_page_book      = new wxSimplebook(this, wxID_ANY);
    m_filament_panel = new wgtDeviceFilamentPanel(m_page_book);
    m_hotend_panel   = new wgtDeviceHotentPanel(m_page_book);

    m_page_book->AddPage(m_filament_panel, "Filament", true);
    m_page_book->AddPage(m_hotend_panel, "Hotend", false);

    root->Add(m_page_book, 0, wxEXPAND, 0);

    m_load_tips = new wgtDeviceFilamentHotendTabLoadTips(this);
    m_load_tips->Hide();
    root->Add(m_load_tips, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

    wxBoxSizer *action_row = new wxBoxSizer(wxHORIZONTAL);
    m_btn_ams_setting      = new Button(this, _L("AMS Settings"));
    m_btn_ams_setting->SetFont(Label::Body_13);
    m_btn_ams_setting->SetBackgroundColor(StateColor::createButtonStyleGray());
    m_btn_ams_setting->SetBackgroundColour(*wxWHITE);
    m_btn_ams_setting->SetMinSize(wxSize(FromDIP(108), FromDIP(28)));
    m_btn_ams_setting->Bind(wxEVT_BUTTON, &wgtDeviceFilamentHotendTab::OnBtnAmsSetting, this);

    m_btn_ams_auto_refill = new Button(this, _L("Auto-refill"));
    m_btn_ams_auto_refill->SetFont(Label::Body_13);
    m_btn_ams_auto_refill->SetBackgroundColor(StateColor::createButtonStyleGray());
    m_btn_ams_auto_refill->SetBackgroundColour(*wxWHITE);
    m_btn_ams_auto_refill->SetMinSize(wxSize(FromDIP(108), FromDIP(28)));
    m_btn_ams_auto_refill->Bind(wxEVT_BUTTON, &wgtDeviceFilamentHotendTab::OnBtnAmsAutoRefill, this);

    action_row->Add(m_btn_ams_setting, 0, wxALIGN_CENTER_VERTICAL);
    action_row->Add(m_btn_ams_auto_refill, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(8));
    root->Add(action_row, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(16));
    root->AddStretchSpacer(1);

    SetSizer(root);
    m_tab_header->Bind(EVT_DEVICE_FILAMENT_HOTEND_BATCH_LOAD, &wgtDeviceFilamentHotendTab::OnHeaderBatchLoad, this);
    m_tab_header->Bind(EVT_DEVICE_FILAMENT_HOTEND_BATCH_UNLOAD, &wgtDeviceFilamentHotendTab::OnHeaderBatchUnload, this);
    m_tab_header->Bind(EVT_DEVICE_FILAMENT_HOTEND_BATCH_QUIT, &wgtDeviceFilamentHotendTab::OnHeaderBatchQuit, this);

    if (m_filament_panel && m_filament_panel->GetAmsFilamentsWidget()) {
        m_filament_panel->GetAmsFilamentsWidget()->Bind(EVT_DEVICE_FILAMENT_HOTEND_SELECTION_CHANGED, &wgtDeviceFilamentHotendTab::OnSelectionChanged, this);
    }
    if (m_load_tips) {
        m_load_tips->Bind(EVT_DEVICE_FILAMENT_HOTEND_SELECT_ALL_CHANGED, &wgtDeviceFilamentHotendTab::OnSelectAllChanged, this);
    }
    UpdateAmsButtons();
    UpdateLoadTips();
    Layout();
}

void wgtDeviceFilamentHotendTab::UpdateBy(Slic3r::MachineObject *obj)
{
    m_fila_system = obj ? obj->GetFilaSystem() : std::weak_ptr<Slic3r::DevFilaSystem>{};
    m_filament_panel->UpdateByObj(obj);
    m_hotend_panel->UpdateByObj(obj);

    UpdateBatchTask(obj);
    UpdateAmsButtons();
    UpdateLoadTips();

    // update dialogs
    if (m_ams_setting_dlg) {
        m_ams_setting_dlg->UpdateByObj(obj);
    }
    if (m_ams_replace_dlg) {
        m_ams_replace_dlg->update_machine_obj(obj);
    }

    Layout();
    Fit();
}

void wgtDeviceFilamentHotendTab::ShowFilamentPanel()
{
    if (m_page_book) { m_page_book->SetSelection(0); }
}

void wgtDeviceFilamentHotendTab::ShowHotendPanel()
{
    if (m_page_book) { m_page_book->SetSelection(1); }
    if (m_load_tips) {
        m_load_tips->Hide();
    }
}

void wgtDeviceFilamentHotendTab::OnHeaderTabChanged(wxCommandEvent &evt)
{
    if (evt.GetInt() == static_cast<int>(wgtDeviceFilamentHotendTabHeader::TabHotend))
        ShowHotendPanel();
    else
        ShowFilamentPanel();

    UpdateAmsButtons();
    UpdateLoadTips();
    Layout();
}

void wgtDeviceFilamentHotendTab::OnHeaderBatchLoad(wxCommandEvent &evt)
{
    m_batch_action = BatchAction::Load;
    m_filament_panel->GetAmsFilamentsWidget()->SetEnableSelection(true);
    m_tab_header->EnterBatchMode();
    m_load_tips->Show();

    evt.Skip();
}

void wgtDeviceFilamentHotendTab::OnHeaderBatchUnload(wxCommandEvent &evt)
{
    m_batch_action = BatchAction::Load;
    m_filament_panel->GetAmsFilamentsWidget()->SetEnableSelection(true);
    m_tab_header->EnterBatchMode();
    m_load_tips->Show();

    evt.Skip();
}

void wgtDeviceFilamentHotendTab::OnHeaderBatchQuit(wxCommandEvent& evt)
{
    m_batch_action = BatchAction::None;
    m_filament_panel->GetAmsFilamentsWidget()->SetEnableSelection(false);
    m_tab_header->QuitBatchMode();
    m_load_tips->Hide();
    evt.Skip();
}

void wgtDeviceFilamentHotendTab::OnSelectionChanged(wxCommandEvent &evt)
{
    ResetBatchResultOnSelectionChange();
    UpdateAmsButtons();
    UpdateLoadTips();
    evt.Skip();
}

void wgtDeviceFilamentHotendTab::OnSelectAllChanged(wxCommandEvent &evt)
{
    if (m_batch_action != BatchAction::None || !m_filament_panel || !m_filament_panel->GetAmsFilamentsWidget()) {
        evt.Skip();
        return;
    }

    m_filament_panel->GetAmsFilamentsWidget()->SetAllSelected(evt.GetInt() != 0);
    ResetBatchResultOnSelectionChange();
    UpdateAmsButtons();
    UpdateLoadTips();
    evt.Skip();
}

void wgtDeviceFilamentHotendTab::ResetBatchResultOnSelectionChange()
{
    if (m_batch_action == BatchAction::None) {
        m_last_batch_status.clear();
        m_last_batch_status_color = wxColour("#00AE42");
    }
}

void wgtDeviceFilamentHotendTab::OnBtnAmsSetting(wxCommandEvent &evt)
{
    auto fila_system = m_fila_system.lock();
    auto* obj = fila_system ? fila_system->GetOwner() : nullptr;
    if (!obj) {
        evt.Skip();
        return;
    }

    if (!m_ams_setting_dlg) { m_ams_setting_dlg = new AMSSetting(this, wxID_ANY); }
    m_ams_setting_dlg->UpdateByObj(obj);
    m_ams_setting_dlg->ShowModal();
}

void wgtDeviceFilamentHotendTab::OnBtnAmsAutoRefill(wxCommandEvent &evt)
{
    auto fila_system = m_fila_system.lock();
    auto* obj = fila_system ? fila_system->GetOwner() : nullptr;
    if (!obj) {
        evt.Skip();
        return;
    }

    if (!m_ams_replace_dlg) {
        m_ams_replace_dlg = new AmsReplaceMaterialDialog(this);
    }
    m_ams_replace_dlg->update_machine_obj(obj);
    m_ams_replace_dlg->ShowModal();
}

void wgtDeviceFilamentHotendTab::UpdateAmsButtons()
{
    if (auto fila_system = m_fila_system.lock()) {
        const auto& obj_ = fila_system->GetOwner();
        const bool has_connected_ams = (obj_ && obj_->is_connected() && !fila_system->GetAmsList().empty() && obj_->ams_exist_bits != 0);
        m_btn_ams_setting->Show(has_connected_ams);
        m_btn_ams_auto_refill->Show(has_connected_ams);
    } else {
        m_btn_ams_setting->Hide();
        m_btn_ams_auto_refill->Hide();
    }
}

void wgtDeviceFilamentHotendTab::UpdateLoadTips()
{
    if (!m_load_tips || !m_filament_panel || !m_filament_panel->GetAmsFilamentsWidget()) {
        return;
    }

    if (m_tab_header->GetSelectedTab() != wgtDeviceFilamentHotendTabHeader::TabFilament) {
        m_load_tips->Show(false);
        return;
    }

    if (auto fila_system = m_fila_system.lock()) {
        const size_t selectable_count = m_filament_panel->GetAmsFilamentsWidget()->GetSelectableItemCount();
        const size_t selected_count = GetSelectedSlots().size();
        const bool select_all_checked = selectable_count > 0 && selectable_count == selected_count;
        const bool select_all_enabled = selectable_count > 0;
        wxString summary_text;
        if (selected_count > 0) {
            summary_text = wxString::Format(_L("Selected %d slots"), static_cast<int>(selected_count));
        }

        m_load_tips->UpdateByObj(fila_system->GetOwner(), select_all_checked, select_all_enabled, summary_text);
    }
}

std::vector<wgtDeviceFilamentHotendTab::BatchSlotTask> wgtDeviceFilamentHotendTab::GetSelectedSlots() const
{
    std::vector<BatchSlotTask> tasks;
    if (!m_filament_panel || !m_filament_panel->GetAmsFilamentsWidget()) {
        return tasks;
    }

    for (const auto& item : m_filament_panel->GetAmsFilamentsWidget()->GetSelectedItems()) {
        tasks.push_back(BatchSlotTask{ item.nozzle_pos, item.ams_id, item.slot_id });
    }
    return tasks;
}

bool wgtDeviceFilamentHotendTab::IsBatchBusy(Slic3r::MachineObject* obj) const
{
    if (!obj) {
        return false;
    }

    if (obj->is_enable_np && obj->GetExtderSystem()->IsBusyLoading()) {
        return true;
    }

    return obj->ams_status_main == AMS_STATUS_MAIN_FILAMENT_CHANGE;
}

bool wgtDeviceFilamentHotendTab::IsSlotLoadedInExtruder(Slic3r::MachineObject* obj, const BatchSlotTask& task) const
{
    if (!obj) {
        return false;
    }

    for (const auto& ext : obj->GetExtderSystem()->GetExtruders()) {
        if (ext.GetSlotNow().ams_id == task.ams_id && ext.GetSlotNow().slot_id == task.slot_id && ext.HasFilamentInExt()) {
            return true;
        }
    }

    return false;
}

bool wgtDeviceFilamentHotendTab::IsCurrentTaskComplete(Slic3r::MachineObject* obj, const BatchSlotTask& task) const
{
    if (m_batch_action == BatchAction::Load) {
        return IsSlotLoadedInExtruder(obj, task);
    }
    if (m_batch_action == BatchAction::Unload) {
        return !IsSlotLoadedInExtruder(obj, task);
    }
    return false;
}

size_t wgtDeviceFilamentHotendTab::GetExecutableCount(Slic3r::MachineObject* obj, bool load) const
{
    size_t ready_count = 0;
    for (const auto& task : GetSelectedSlots()) {
        wxString load_error_info;
        wxString unload_error_info;
        GetSlotSwitchInfo(obj, task, load_error_info, unload_error_info);
        if (load ? load_error_info.empty() : unload_error_info.empty()) {
            ++ready_count;
        }
    }
    return ready_count;
}

wxString wgtDeviceFilamentHotendTab::GetGlobalBatchErrorInfo(Slic3r::MachineObject* obj, const std::vector<BatchSlotTask>& tasks) const
{
    if (!obj) {
        return _L("Please select a printer");
    }

    if (tasks.empty()) {
        return _L("Select one or more slots to continue.");
    }

    if (obj->is_in_printing() && !obj->can_resume()) {
        return _L("The printer is busy on other print job");
    }

    if (obj->can_resume()) {
        for (const auto& task : tasks) {
            if (!devPrinterUtil::IsVirtualSlot(task.ams_id)) {
                return _L("When printing is paused, filament loading and unloading are only supported for external slots.");
            }
        }
    }

    if (IsBatchBusy(obj)) {
        return _L("Current extruder is busy changing filament");
    }

    if (obj->GetFilaSwitch()->IsInstalled()) {
        for (const auto& task : tasks) {
            if (devPrinterUtil::IsVirtualSlot(task.ams_id)) {
                return _L("\"Load\" or \"Unload\" is not supported for external spool while using Filament Track Switch.");
            }
        }

        if (!obj->GetFilaSwitch()->IsReady()) {
            return _L("The Filament Track Switch has not been setup. Please setup on printer.");
        }
    }

    return wxEmptyString;
}

void wgtDeviceFilamentHotendTab::GetSlotSwitchInfo(Slic3r::MachineObject* obj, const BatchSlotTask& task, wxString& load_error_info, wxString& unload_error_info) const
{
    load_error_info.clear();
    unload_error_info.clear();

    if (!obj) {
        load_error_info = _L("Please select a printer");
        unload_error_info = _L("Please select a printer");
        return;
    }

    const wxString global_error = GetGlobalBatchErrorInfo(obj, { task });
    if (!global_error.empty()) {
        load_error_info = global_error;
        unload_error_info = global_error;
        return;
    }

    auto tray_item = obj->get_tray(task.ams_id, task.slot_id);
    if (!tray_item) {
        load_error_info = _L("Choose one or more AMS slots, then press \"Load\" or \"Unload\".");
        unload_error_info = load_error_info;
        return;
    }

    for (const auto& ext : obj->GetExtderSystem()->GetExtruders()) {
        if (obj->GetFilaSwitch()->IsInstalled()) {
            continue;
        }

        if (ext.GetSlotNow().ams_id == task.ams_id && ext.GetSlotNow().slot_id == task.slot_id && ext.HasFilamentInExt()) {
            load_error_info = _L("Current slot has alread been loaded");
        }
    }

    if (!devPrinterUtil::IsVirtualSlot(task.ams_id) && !tray_item->is_exists) {
        load_error_info = _L("The selected slot is empty.");
    }

    auto ams_item = obj->GetFilaSystem()->GetAmsById(task.ams_id);
    if (!ams_item) {
        unload_error_info = _L("No extruder found for the selected slot.");
        return;
    }

    if (auto extder_id_opt = ams_item->GetCurrentExtruderId(); extder_id_opt.has_value()) {
        auto extder = obj->GetExtderSystem()->GetExtderById(extder_id_opt.value());
        if (!extder.has_value() || !extder->HasFilamentInExt() ||
            extder->GetSlotNow().ams_id != task.ams_id || extder->GetSlotNow().slot_id != task.slot_id) {
            unload_error_info = _L("The selected slot is not loaded in the extruder.");
        }
    } else {
        unload_error_info = obj->GetFilaSwitch()->IsInstalled()
            ? _L("The selected slot is not loaded in the extruder.")
            : _L("No extruder found for the selected slot.");
    }
}

wxString wgtDeviceFilamentHotendTab::GetSlotDisplayName(const BatchSlotTask& task) const
{
    return "slot " + wxString::FromUTF8(task.ams_id.c_str()) + "-" + wxString::FromUTF8(task.slot_id.c_str());
}

bool wgtDeviceFilamentHotendTab::ShowBatchConfirmDialog(bool load, const std::vector<BatchSlotTask>& tasks, size_t ready_count) const
{
    const wxString title = load ? _L("Start batch loading?") : _L("Start batch unloading?");
    const wxString message =
        _L("The selected slots will be processed one by one.")
        + "\n\n"
        + _L("If one slot fails during execution, the remaining slots will not be processed.")
        + "\n"
        + wxString::Format(_L("Selected: %d, executable now: %d"), static_cast<int>(tasks.size()), static_cast<int>(ready_count))
        + "\n\n"
        + _L("Press OK to continue.");

    MessageDialog msg_dlg(nullptr, message, title, wxOK | wxCANCEL | wxICON_INFORMATION);
    return msg_dlg.ShowModal() == wxID_OK;
}

void wgtDeviceFilamentHotendTab::SetBatchOperation(bool load)
{
    m_batch_action = load ? BatchAction::Load : BatchAction::Unload;
    m_filament_panel->GetAmsFilamentsWidget()->SetEnableSelection(true);
    m_tab_header->EnterBatchMode();
    m_load_tips->Show();
}

void wgtDeviceFilamentHotendTab::StartCurrentBatchCommand(Slic3r::MachineObject* obj)
{
    if (!obj || m_batch_action == BatchAction::None) {
        StopBatchOperation(_L("Batch operation stopped because printer is unavailable."));
        return;
    }

    if (m_batch_current_index >= m_batch_slots.size()) {
        FinishBatchOperation();
        return;
    }

    const auto& task = m_batch_slots[m_batch_current_index];
    wxString load_error_info;
    wxString unload_error_info;
    GetSlotSwitchInfo(obj, task, load_error_info, unload_error_info);
    const wxString action_error = (m_batch_action == BatchAction::Load) ? load_error_info : unload_error_info;
    if (!action_error.empty()) {
        const int remaining_count = static_cast<int>(m_batch_slots.size() - m_batch_current_index);
        const wxString status =
            (m_batch_action == BatchAction::Load ? _L("Batch loading stopped at ") : _L("Batch unloading stopped at "))
            + GetSlotDisplayName(task)
            + ". "
            + wxString::Format(_L("Completed: %d. Remaining: %d."), static_cast<int>(m_batch_completed_slots.size()), remaining_count);
        StopBatchOperation(status + " " + action_error);
        return;
    }

    if (m_filament_panel && m_filament_panel->GetAmsFilamentsWidget()) {
        m_filament_panel->GetAmsFilamentsWidget()->SetRunningSlot(task.ams_id, task.slot_id);
    }

    m_batch_waiting_state_change = true;
    m_batch_seen_busy_state = IsBatchBusy(obj);
    m_batch_step_started_at = std::chrono::steady_clock::now();
    m_last_batch_status = wxString::Format(_L("Processing slot %d of %d."),
        static_cast<int>(m_batch_current_index + 1), static_cast<int>(m_batch_slots.size()));
    m_last_batch_status_color = wxColour("#00AE42");

    if (m_batch_action == BatchAction::Load) {
        int old_temp = -1;
        int new_temp = -1;

        auto targ_tray = obj->get_tray(task.ams_id, task.slot_id);
        if (!targ_tray.has_value()) {
            StopBatchOperation(_L("Batch loading stopped because target slot is unavailable."));
            return;
        }

        try {
            if (!targ_tray->nozzle_temp_max.empty() && !targ_tray->nozzle_temp_min.empty()) {
                new_temp = (atoi(targ_tray->nozzle_temp_min.c_str()) + atoi(targ_tray->nozzle_temp_max.c_str())) / 2;
            }

            DevAmsTray* curr_tray = obj->get_curr_tray();
            if (curr_tray && !curr_tray->nozzle_temp_max.empty() && !curr_tray->nozzle_temp_min.empty()) {
                old_temp = (atoi(curr_tray->nozzle_temp_min.c_str()) + atoi(curr_tray->nozzle_temp_max.c_str())) / 2;
            }
        } catch (const std::exception&) {
        }

        obj->command_ams_change_filament(true, task.ams_id, task.slot_id, old_temp, new_temp, 0);
    } else {
        obj->command_ams_change_filament(false, task.ams_id, "255");
    }
}

void wgtDeviceFilamentHotendTab::UpdateBatchTask(Slic3r::MachineObject* obj)
{
    if (m_batch_action == BatchAction::None && GetSelectedSlots().empty()) {
        const wxString tip = _L("Select one or more slots to continue.");
        m_tab_header->ShowLoad(true, tip);
        m_tab_header->ShowUnload(true, tip);
    } else {
        m_tab_header->ShowLoad(false, "");
        m_tab_header->ShowUnload(false, "");
    }

    if (m_batch_action == BatchAction::None) {
        return;
    }

    if (!obj) {
        StopBatchOperation(_L("Batch operation stopped because printer is unavailable."));
        return;
    }

    return;

    if (m_batch_current_index >= m_batch_slots.size()) {
        FinishBatchOperation();
        return;
    }

    const auto& task = m_batch_slots[m_batch_current_index];
    const auto now = std::chrono::steady_clock::now();
    const bool busy = IsBatchBusy(obj);
    if (busy) {
        m_batch_seen_busy_state = true;
    }

    if (IsCurrentTaskComplete(obj, task)) {
        m_batch_completed_slots.push_back(task);
        ++m_batch_current_index;
        m_batch_waiting_state_change = false;
        m_batch_seen_busy_state = false;

        if (m_batch_current_index >= m_batch_slots.size()) {
            FinishBatchOperation();
        } else {
            StartCurrentBatchCommand(obj);
        }
        return;
    }

    if (busy) {
        if (now - m_batch_step_started_at > kBatchFinishTimeout) {
            const int remaining_count = static_cast<int>(m_batch_slots.size() - m_batch_current_index);
            StopBatchOperation(
                (m_batch_action == BatchAction::Load ? _L("Batch loading stopped at ") : _L("Batch unloading stopped at "))
                + GetSlotDisplayName(task)
                + ". "
                + wxString::Format(_L("Completed: %d. Remaining: %d."), static_cast<int>(m_batch_completed_slots.size()), remaining_count)
                + " "
                + _L("The current slot did not finish in time. Remaining slots were not processed."));
        }
        return;
    }

    if (m_batch_seen_busy_state) {
        const int remaining_count = static_cast<int>(m_batch_slots.size() - m_batch_current_index);
        StopBatchOperation(
            (m_batch_action == BatchAction::Load ? _L("Batch loading stopped at ") : _L("Batch unloading stopped at "))
            + GetSlotDisplayName(task)
            + ". "
            + wxString::Format(_L("Completed: %d. Remaining: %d."), static_cast<int>(m_batch_completed_slots.size()), remaining_count));
        return;
    }

    if (now - m_batch_step_started_at > kBatchStartTimeout) {
        const int remaining_count = static_cast<int>(m_batch_slots.size() - m_batch_current_index);
        StopBatchOperation(
            (m_batch_action == BatchAction::Load ? _L("Batch loading stopped at ") : _L("Batch unloading stopped at "))
            + GetSlotDisplayName(task)
            + ". "
            + wxString::Format(_L("Completed: %d. Remaining: %d."), static_cast<int>(m_batch_completed_slots.size()), remaining_count)
            + " "
            + _L("The current slot did not finish in time. Remaining slots were not processed."));
    }
}

void wgtDeviceFilamentHotendTab::StopBatchOperation(const wxString& status_text, const wxColour& color)
{
    m_last_batch_status = status_text;
    m_last_batch_status_color = color;
    m_batch_action = BatchAction::None;
    m_batch_slots.clear();
    m_batch_completed_slots.clear();
    m_batch_current_index = 0;
    m_batch_waiting_state_change = false;
    m_batch_seen_busy_state = false;
    if (m_filament_panel && m_filament_panel->GetAmsFilamentsWidget()) {
        m_filament_panel->GetAmsFilamentsWidget()->SetEnableSelection(true);
        m_filament_panel->GetAmsFilamentsWidget()->ClearRunningSlot();
    }
    UpdateAmsButtons();
    UpdateLoadTips();
}

void wgtDeviceFilamentHotendTab::FinishBatchOperation()
{
    const wxString status =
        (m_batch_action == BatchAction::Load ? _L("Batch loading completed.") : _L("Batch unloading completed."))
        + " "
        + wxString::Format(_L("Completed %d slots"), static_cast<int>(m_batch_completed_slots.size()));
    StopBatchOperation(status, wxColour("#00AE42"));
}

} // namespace Slic3r::GUI
