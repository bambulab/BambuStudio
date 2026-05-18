#include "wgtDeviceFilamentHotendTabLoadTips.h"

#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"
#include "slic3r/GUI/wxExtensions.hpp"

#include <wx/checkbox.h>
#include <wx/sizer.h>

wxDEFINE_EVENT(EVT_DEVICE_FILAMENT_HOTEND_SELECT_ALL_CHANGED, wxCommandEvent);

namespace Slic3r::GUI
{

wgtDeviceFilamentHotendTabLoadTips::wgtDeviceFilamentHotendTabLoadTips(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    CreateGui();
}

void wgtDeviceFilamentHotendTabLoadTips::UpdateByMachine(Slic3r::MachineObject* machine_obj,
                                                         bool select_all_checked,
                                                         bool select_all_enabled,
                                                         const wxString& summary_text,
                                                         wgtDeviceFilamentHotendBatchAction batch_action)
{
    SetSelectAllChecked(select_all_checked);
    SetSelectAllEnabled(machine_obj && select_all_enabled);
    SetSummaryText(summary_text);

    if (m_batch_action != batch_action) {
        m_batch_action = batch_action;
        if (m_batch_action == wgtDeviceFilamentHotendBatchAction::Load) {
            m_label_action->SetLabel(_L("Load"));
            m_label_action->Show();
        } else if (m_batch_action == wgtDeviceFilamentHotendBatchAction::Unload) {
            m_label_action->SetLabel(_L("Unload"));
            m_label_action->Show();
        } else {
            m_label_action->Hide();
        }
    }
}

void wgtDeviceFilamentHotendTabLoadTips::SetSelectAllChecked(bool checked)
{
    if (m_check_select_all && m_check_select_all->GetValue() != checked) {
        m_check_select_all->SetValue(checked);
    }
}

void wgtDeviceFilamentHotendTabLoadTips::SetSelectAllEnabled(bool enabled)
{
    if (m_check_select_all) {
        m_check_select_all->Enable(enabled);
    }
}

void wgtDeviceFilamentHotendTabLoadTips::SetSummaryText(const wxString& text)
{
    if (m_label_summary->GetLabelText() == text) {
        return;
    }

    m_label_summary->SetLabel(text);
    Layout();
}

void wgtDeviceFilamentHotendTabLoadTips::CreateGui()
{
    SetBackgroundColour(*wxWHITE);
    SetMinSize(wxSize(-1, FromDIP(32)));

    auto* root = new wxBoxSizer(wxHORIZONTAL);

    m_check_select_all = new wxCheckBox(this, wxID_ANY, _L("Select all"));
    root->Add(m_check_select_all, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(10));

    m_label_summary = new Label(this, _L("No slot selected"));
    m_label_summary->SetFont(Label::Body_12);
    m_label_summary->SetForegroundColour(wxColour("#323A3D"));
    root->Add(m_label_summary, 0, wxALIGN_CENTER_VERTICAL | wxLEFT);

    SetSizer(root);

    m_check_select_all->Bind(wxEVT_CHECKBOX, &wgtDeviceFilamentHotendTabLoadTips::OnSelectAllChanged, this);
}

void wgtDeviceFilamentHotendTabLoadTips::OnSelectAllChanged(wxCommandEvent& evt)
{
    wxCommandEvent event(EVT_DEVICE_FILAMENT_HOTEND_SELECT_ALL_CHANGED, GetId());
    event.SetEventObject(this);
    event.SetInt(m_check_select_all && m_check_select_all->GetValue() ? 1 : 0);
    GetEventHandler()->ProcessEvent(event);
    evt.Skip();
}

} // namespace Slic3r::GUI
