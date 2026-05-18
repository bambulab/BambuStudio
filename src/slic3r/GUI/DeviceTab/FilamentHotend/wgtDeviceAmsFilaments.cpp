#include "wgtDeviceAmsFilaments.h"

#include "wgtDeviceAmsDryIcon.h"
#include "wgtDeviceFilamentHotendItem.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/wxExtensions.hpp"

#include <algorithm>
#include <wx/gbsizer.h>
#include <wx/sizer.h>

namespace Slic3r::GUI {

wxDEFINE_EVENT(EVT_DEVICE_FILAMENT_HOTEND_SELECTION_CHANGED, wxCommandEvent);

wgtDeviceAmsFilaments::wgtDeviceAmsFilaments(wxWindow *parent) : wxPanel(parent, wxID_ANY) { CreateGui(); }

void wgtDeviceAmsFilaments::Reset()
{
    UpdateByMachine(nullptr, {});
    UpdateAmsDry(nullptr);
}

void wgtDeviceAmsFilaments::UpdateByMachine(Slic3r::MachineObject* machine_obj,
                                            const std::map<int, std::optional<DevAmsSlotId>>& nozzle_slots)
{
    auto fila_system = machine_obj ? machine_obj->GetFilaSystem() : nullptr;
    m_nozzle_slots = nozzle_slots;

    // Update item count and layout
    if (m_nozzle_slots.size() != m_filament_items.size()) {
        m_items_sizer->Clear(false);
        for (const auto& iter : m_filament_items) {
            iter.second->Destroy();
        }
        for (const auto& iter : m_hotend_items) {
            iter.second->Destroy();
        }
        m_filament_items.clear();
        m_hotend_items.clear();

        std::vector<int> nozzle_positions;
        nozzle_positions.reserve(m_nozzle_slots.size());
        for (const auto& iter : m_nozzle_slots) {
            nozzle_positions.push_back(iter.first);
        }
        std::sort(nozzle_positions.begin(), nozzle_positions.end(), [this](int lhs, int rhs) {
            auto to_display_idx = [](int nozzle_pos) {
                return nozzle_pos >= 0x10 ? (nozzle_pos - 0x10 + 1) : (nozzle_pos + 1);
            };
            return to_display_idx(lhs) < to_display_idx(rhs);
        });

        int col = 0;
        for (int nozzle_pos : nozzle_positions) {
            auto* item = new wgtDeviceFilamentHotendItem(m_panel_content);
            item->SetNozzlePos(nozzle_pos);
            item->SetLayoutTier(IsTopTierNozzle(nozzle_pos));
            item->SetEnableSelection(false);
            item->Bind(EVT_DEVICE_FILAMENT_HOTEND_ITEM_SELECTION_CHANGED, &wgtDeviceAmsFilaments::OnItemSelectionChanged, this);

            const int row = IsTopTierNozzle(nozzle_pos) ? 0 : 1;
            m_items_sizer->Add(item, wxGBPosition(row, col), wxDefaultSpan, wxALIGN_CENTER_HORIZONTAL);

            auto* hotend = new wgtDeviceFilamentHotendItemHotend(m_panel_content);
            m_items_sizer->Add(hotend, wxGBPosition(2, col), wxDefaultSpan, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(2));

            m_filament_items[nozzle_pos] = item;
            m_hotend_items[nozzle_pos] = hotend;
            col++;
        }

        // Keep legacy dry icon for later style-A integration.
        m_ams_dry_icon->Hide();

        Layout();
        Fit();
    };

    // Update data
    for (const auto& item : m_filament_items) {
        if (m_nozzle_slots.count(item.first) != 0) {
            item.second->UpdateByMachine(machine_obj, m_nozzle_slots[item.first]);
            if (m_hotend_items.count(item.first) != 0 && m_hotend_items[item.first]) {
                m_hotend_items[item.first]->SetHotendState(item.second->GetNozzleState());
            }
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

bool wgtDeviceAmsFilaments::IsTopTierNozzle(int nozzle_pos) const
{
    const int display_idx = nozzle_pos >= 0x10 ? (nozzle_pos - 0x10 + 1) : (nozzle_pos + 1);
    return (display_idx % 2) == 0;
}

void wgtDeviceAmsFilaments::CreateGui()
{
    SetBackgroundColour(*wxWHITE);

    wxBoxSizer *root = new wxBoxSizer(wxVERTICAL);

    m_panel_content = new wxPanel(this, wxID_ANY);
    m_panel_content->SetBackgroundColour(*wxWHITE);
    m_items_sizer = new wxGridBagSizer(FromDIP(6), FromDIP(18));

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
