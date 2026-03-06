#include "wgtDeviceAmsFilaments.h"

#include "wgtDeviceAmsDryIcon.h"
#include "wgtDeviceFilamentHotendItem.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/wxExtensions.hpp"

#include <wx/gbsizer.h>
#include <wx/sizer.h>

namespace Slic3r::GUI {

wxDEFINE_EVENT(EVT_DEVICE_FILAMENT_HOTEND_SELECTION_CHANGED, wxCommandEvent);

wgtDeviceAmsFilaments::wgtDeviceAmsFilaments(wxWindow *parent) : wxPanel(parent, wxID_ANY) { CreateGui(); }

void wgtDeviceAmsFilaments::Reset()
{
    UpdateBy(nullptr, {});
    UpdateAmsDry(nullptr);
}

void wgtDeviceAmsFilaments::UpdateBy(std::shared_ptr<Slic3r::DevFilaSystem> fila_system,
                                     const std::map<int, std::optional<DevAmsSlotId>>& nozzle_slots)
{
    m_nozzle_slots = nozzle_slots;

    // Update item count and layout
    if (m_nozzle_slots.size() != m_filament_items.size()) {
        m_items_sizer->Clear(false);
        for (const auto& iter : m_filament_items) {
            iter.second->Destroy();
        }
        m_filament_items.clear();

        int item_count = 0;
        int mid_count = m_nozzle_slots.size() / 2;
        for (const auto& iter : m_nozzle_slots) {
            if (item_count == mid_count) {
                m_items_sizer->Add(m_ams_dry_icon, wxGBPosition(0, mid_count), wxDefaultSpan, wxALIGN_LEFT | wxLEFT, FromDIP(8));
                item_count++;
            }

            auto* item = new wgtDeviceFilamentHotendItem(m_panel_content);
            item->SetNozzlePos(iter.first);
            item->Bind(EVT_DEVICE_FILAMENT_HOTEND_ITEM_SELECTION_CHANGED, &wgtDeviceAmsFilaments::OnItemSelectionChanged, this);

            if (item_count < mid_count) {
                m_items_sizer->Add(item, wxGBPosition(0, item_count), wxDefaultSpan, wxALIGN_LEFT);
            } else {
                m_items_sizer->Add(item, wxGBPosition(1, item_count - mid_count - 1), wxDefaultSpan, wxALIGN_LEFT);
            }

            m_filament_items[iter.first] = item;
            item_count++;
        }

        Layout();
        Fit();
    };

    // Update data
    for (const auto& item : m_filament_items) {
        if (m_nozzle_slots.count(item.first) != 0) {
            item.second->UpdateBy(fila_system, m_nozzle_slots[item.first]);
        }
    }
}

std::vector<DeviceFilamentSelectionItem> wgtDeviceAmsFilaments::GetSelectedItems() const
{
    std::vector<DeviceFilamentSelectionItem> selected_items;
    selected_items.reserve(m_filament_items.size());

    for (const auto& iter : m_filament_items) {
        const auto* item = iter.second;
        if (!item || !item->IsEnabledState() || !item->IsSelected() || item->GetAmsId().empty() || item->GetSlotId().empty()) {
            continue;
        }

        selected_items.push_back(DeviceFilamentSelectionItem{ item->GetNozzlePos(), item->GetAmsId(), item->GetSlotId() });
    }

    return selected_items;
}

size_t wgtDeviceAmsFilaments::GetSelectableItemCount() const
{
    size_t count = 0;
    for (const auto& iter : m_filament_items) {
        if (iter.second && iter.second->IsEnabledState() && !iter.second->GetAmsId().empty() && !iter.second->GetSlotId().empty()) {
            ++count;
        }
    }
    return count;
}

void wgtDeviceAmsFilaments::SetAllSelected(bool selected)
{
    for (const auto& iter : m_filament_items) {
        if (iter.second && iter.second->IsEnabledState()) {
            iter.second->SetSelected(selected, false);
        }
    }
    NotifySelectionChanged();
}

void wgtDeviceAmsFilaments::SetRunningSlot(const std::string& ams_id, const std::string& slot_id)
{
    for (const auto& iter : m_filament_items) {
        if (iter.second) {
            iter.second->SetRunning(iter.second->GetAmsId() == ams_id && iter.second->GetSlotId() == slot_id);
        }
    }
}

void wgtDeviceAmsFilaments::ClearRunningSlot()
{
    for (const auto& iter : m_filament_items) {
        if (iter.second) {
            iter.second->SetRunning(false);
        }
    }
}

void wgtDeviceAmsFilaments::SetEnableSelection(bool enable)
{
    for (const auto& iter : m_filament_items) {
        if (iter.second) {
            iter.second->SetEnableSelection(enable);
            if (!enable) {
                iter.second->SetSelected(false, false);
            }
        }
    }
}

void wgtDeviceAmsFilaments::UpdateAmsDry(Slic3r::DevAms* ams)
{
    if (!ams) {
        m_ams_dry_icon->ClearSummary();
        return;
    }

    m_ams_dry_icon->SetSummary("5&&7", "66%", _L("Other"), "24%");
    m_ams_dry_icon->Layout();
}

void wgtDeviceAmsFilaments::CreateGui()
{
    SetBackgroundColour(*wxWHITE);

    wxBoxSizer *root = new wxBoxSizer(wxVERTICAL);

    m_panel_content = new wxPanel(this, wxID_ANY);
    m_panel_content->SetBackgroundColour(*wxWHITE);
    m_items_sizer = new wxGridBagSizer(FromDIP(16), FromDIP(8));

    m_ams_dry_icon = new wgtDeviceAmsDryIcon(m_panel_content);
    m_panel_content->SetSizer(m_items_sizer);
    root->Add(m_panel_content, 1, wxEXPAND | wxALL, FromDIP(8));

    SetSizer(root);
    Layout();
    Reset();
}

void wgtDeviceAmsFilaments::OnItemSelectionChanged(wxCommandEvent& evt)
{
    NotifySelectionChanged();
    evt.Skip();
}

void wgtDeviceAmsFilaments::NotifySelectionChanged()
{
    wxCommandEvent event(EVT_DEVICE_FILAMENT_HOTEND_SELECTION_CHANGED, GetId());
    event.SetEventObject(this);
    event.SetInt(static_cast<int>(GetSelectedItems().size()));
    GetEventHandler()->ProcessEvent(event);
}

} // namespace Slic3r::GUI
