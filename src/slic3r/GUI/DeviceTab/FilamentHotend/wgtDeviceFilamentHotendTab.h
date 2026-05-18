#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <wx/colour.h>
#include <wx/panel.h>

#include "wgtDeviceFilamentHotendBatchFlow.h"
#include "wgtDeviceFilamentHotendDryEntryPresenter.h"
#include "wgtDeviceFilamentHotendFeatureGate.h"
#include "wgtDeviceFilamentHotendStateResolver.h"

class Button;
class wxSimplebook;

namespace Slic3r {
class MachineObject;
class DevFilaSystem;
} // namespace Slic3r

namespace Slic3r::GUI {

class wgtDeviceFilamentPanel;
class wgtDeviceHotentPanel;
class wgtDeviceFilamentHotendTabHeader;
class wgtDeviceFilamentHotendTabLoadTips;
class DeviceWebHost;
class AMSSetting;
class AmsReplaceMaterialDialog;

class wgtDeviceFilamentHotendTab : public wxPanel
{
public:
    using BatchAction = wgtDeviceFilamentHotendBatchAction;
    using BatchSlotTask = wgtDeviceFilamentHotendBatchSlotTask;

    explicit wgtDeviceFilamentHotendTab(wxWindow *parent);
    ~wgtDeviceFilamentHotendTab() override = default;

    void UpdateByMachine(Slic3r::MachineObject *obj);

private:
    // lifecycle
    void CreateGui();
    void UpdateAmsButtons();
    void UpdateLoadTips();
    void UpdateBatchTask(Slic3r::MachineObject* obj);

    // page switch
    void ShowFilamentPanel();
    void ShowHotendPanel();
    void OnHeaderTabChanged(wxCommandEvent &evt);

    // selection and batch entry
    void OnHeaderBatchLoad(wxCommandEvent &evt);
    void OnHeaderBatchUnload(wxCommandEvent &evt);
    void OnHeaderBatchQuit(wxCommandEvent &evt);
    void OnHeaderDry(wxCommandEvent &evt);
    void OnHeaderHotendReadAll(wxCommandEvent &evt);

    // batch
    void OnSelectionChanged(wxCommandEvent &evt);
    void OnSelectAllChanged(wxCommandEvent &evt);
    void ResetBatchResultOnSelectionChange();

    // dialogs
    void OnBtnAmsSetting(wxCommandEvent &evt);
    void OnBtnAmsAutoRefill(wxCommandEvent &evt);

    // batch execution
    void SetBatchOperation(bool load);
    BatchAction GetBatchOperation() const { return m_batch_flow.GetAction(); }

    void StartCurrentBatchCommand(Slic3r::MachineObject* obj);
    void StopBatchOperation(const wxString& status_text, const wxColour& color = wxColour("#D95047"));
    void FinishBatchOperation();

private:
    // batch query and validation
    std::vector<BatchSlotTask> GetSelectedSlots() const;
    bool IsBatchBusy(Slic3r::MachineObject* obj) const;
    bool IsSlotLoadedInExtruder(Slic3r::MachineObject* obj, const BatchSlotTask& task) const;
    bool IsCurrentTaskComplete(Slic3r::MachineObject* obj, const BatchSlotTask& task) const;
    size_t GetExecutableCount(Slic3r::MachineObject* obj, bool load) const;
    wxString GetGlobalBatchErrorInfo(Slic3r::MachineObject* obj, const std::vector<BatchSlotTask>& tasks) const;
    void GetSlotSwitchInfo(Slic3r::MachineObject* obj, const BatchSlotTask& task, wxString& load_error_info, wxString& unload_error_info) const;
    wxString GetSlotDisplayName(const BatchSlotTask& task) const;
    bool ShowBatchConfirmDialog(bool load, const std::vector<BatchSlotTask>& tasks, size_t ready_count) const;

private:
    // gui
    wgtDeviceFilamentHotendTabHeader* m_tab_header{ nullptr };
    wxSimplebook* m_page_book{ nullptr };
    wgtDeviceFilamentPanel* m_filament_panel{ nullptr };
    wgtDeviceHotentPanel* m_hotend_panel{ nullptr };
    wgtDeviceFilamentHotendTabLoadTips* m_load_tips{ nullptr };
    Button* m_btn_ams_setting{ nullptr };
    Button* m_btn_ams_auto_refill{ nullptr };
    DeviceWebHost* m_device_filament_web{ nullptr };

    // dialog
    AMSSetting* m_ams_setting_dlg{ nullptr };
    AmsReplaceMaterialDialog* m_ams_replace_dlg{ nullptr };

    // data
    std::weak_ptr<Slic3r::DevFilaSystem> m_fila_system;
    wgtDeviceFilamentHotendFeatureGate m_feature_gate;
    wgtDeviceFilamentHotendStateResolver m_state_resolver;
    wgtDeviceFilamentHotendDryEntryPresenter m_dry_entry_presenter;
    wgtDeviceFilamentHotendBatchFlow m_batch_flow;
};

} // namespace Slic3r::GUI
