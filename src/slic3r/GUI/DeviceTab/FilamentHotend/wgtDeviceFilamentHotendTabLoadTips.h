#pragma once

#include <wx/colour.h>
#include <wx/panel.h>
#include <wx/string.h>

#include "wgtDeviceFilamentHotendBatchFlow.h"

class Label;
class wxCheckBox;

wxDECLARE_EVENT(EVT_DEVICE_FILAMENT_HOTEND_SELECT_ALL_CHANGED, wxCommandEvent);

namespace Slic3r {
class MachineObject;
}

namespace Slic3r::GUI
{

class wgtDeviceFilamentHotendTabLoadTips : public wxPanel
{
public:
    explicit wgtDeviceFilamentHotendTabLoadTips(wxWindow* parent);
    ~wgtDeviceFilamentHotendTabLoadTips() override = default;

    void UpdateByMachine(Slic3r::MachineObject* machine_obj,
                     bool select_all_checked,
                     bool select_all_enabled,
                     const wxString& summary_text,
                     wgtDeviceFilamentHotendBatchAction batch_action);

private:
    void CreateGui();
    void OnSelectAllChanged(wxCommandEvent& evt);
    void SetSelectAllChecked(bool checked);
    void SetSelectAllEnabled(bool enabled);
    void SetSummaryText(const wxString& text);

private:
    wxCheckBox* m_check_select_all{ nullptr };
    Label* m_label_summary{ nullptr };
    Label* m_label_action;
    wgtDeviceFilamentHotendBatchAction m_batch_action{wgtDeviceFilamentHotendBatchAction::None};
};

} // namespace Slic3r::GUI
