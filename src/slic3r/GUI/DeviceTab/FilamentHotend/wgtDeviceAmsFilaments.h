#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <wx/panel.h>

#include "slic3r/GUI/DeviceCore/DevDefs.h"

class wxGridBagSizer;
class wxPanel;

namespace Slic3r {
class DevFilaSystem;
class DevAms;
class MachineObject;
}

namespace Slic3r::GUI
{

class wgtDeviceFilamentHotendItem;
class wgtDeviceFilamentHotendItemHotend;
class wgtDeviceAmsDryIcon;

struct DeviceFilamentSelectionItem
{
    int nozzle_pos{ -1 };
    std::string ams_id;
    std::string slot_id;
};

wxDECLARE_EVENT(EVT_DEVICE_FILAMENT_HOTEND_SELECTION_CHANGED, wxCommandEvent);

class wgtDeviceAmsFilaments : public wxPanel
{
public:
    explicit wgtDeviceAmsFilaments(wxWindow* parent);
    ~wgtDeviceAmsFilaments() override = default;

    void Reset();
    void UpdateByMachine(Slic3r::MachineObject* machine_obj,
                         const std::map<int, std::optional<DevAmsSlotId>>& nozzle_slots);
    std::vector<DeviceFilamentSelectionItem> GetSelectedItems() const;
    size_t GetSelectableItemCount() const;
    void SetAllSelected(bool selected);
    void SetRunningSlot(const std::string& ams_id, const std::string& slot_id);
    void ClearRunningSlot();
    void SetEnableSelection(bool enable);

private:
    void CreateGui();
    void UpdateAmsDry(Slic3r::DevAms* ams);
    bool IsTopTierNozzle(int nozzle_pos) const;
    void OnItemSelectionChanged(wxCommandEvent& evt);
    void NotifySelectionChanged();

private:
    wxPanel* m_panel_content{ nullptr };
    wxGridBagSizer* m_items_sizer{ nullptr };
    wgtDeviceAmsDryIcon* m_ams_dry_icon{ nullptr };

    std::map<int, std::optional<DevAmsSlotId>> m_nozzle_slots;// nozzle_pos -> ams slot
    std::map<int, wgtDeviceFilamentHotendItem*> m_filament_items; // nozzle_pos -> items
    std::map<int, wgtDeviceFilamentHotendItemHotend*> m_hotend_items; // nozzle_pos -> hotend icons
};

} // namespace Slic3r::GUI
