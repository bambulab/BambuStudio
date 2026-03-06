#pragma once

#include <wx/colour.h>
#include <wx/panel.h>
#include <wx/string.h>

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

    void UpdateByObj(Slic3r::MachineObject* obj,
                     bool select_all_checked,
                     bool select_all_enabled,
                     const wxString& summary_text);

private:
    void CreateGui();
    void OnSelectAllChanged(wxCommandEvent& evt);
    void SetSelectAllChecked(bool checked);
    void SetSelectAllEnabled(bool enabled);
    void SetSummaryText(const wxString& text);

private:
    wxCheckBox* m_check_select_all{ nullptr };
    Label* m_label_summary{ nullptr };
};

} // namespace Slic3r::GUI
