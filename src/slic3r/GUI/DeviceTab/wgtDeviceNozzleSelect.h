//**********************************************************/
/* File: wgtDeviceNozzleSelect.h
*  Description: The panel to select nozzle
*
*  \n class wgtDeviceNozzleSelect;
//**********************************************************/

#pragma once

#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"

#include <wx/panel.h>

#include <memory>

// Previous definitions
namespace Slic3r
{
    struct DevNozzle;
    class DevNozzleRack;
namespace GUI
{
    class wgtDeviceNozzleRackNozzleItem;
}
};

wxDECLARE_EVENT(EVT_NOZZLE_RACK_ITEM_CLICKED, wxCommandEvent);
namespace Slic3r::GUI
{
class wgtDeviceNozzleRackSelect : public wxPanel
{
public:
    wgtDeviceNozzleRackSelect(wxWindow* parent);
    ~wgtDeviceNozzleRackSelect() = default;

public:
    int  GetSelectedNozzlePosID() const { return m_selected_nozzle.GetNozzlePosId();}
    void UpdatSelectedNozzles(std::shared_ptr<DevNozzleRack> rack, std::vector<int> selected_nozzle_pos_vec, bool use_dynamic_switch);// for slicing with dynamic switch
    void Rescale();

private:
    void CreateGui();

    void ClearSelection();
    void SetSelectedNozzle(const DevNozzle &nozzle);
    void UpdatSelectedNozzle(std::shared_ptr<DevNozzleRack> rack, int selected_nozzle_pos_id = -1); // for slicing without dynamic switch

    void UpdateNozzleInfos(std::shared_ptr<DevNozzleRack> rack);

    void OnNozzleItemSelected(wxCommandEvent& evt);

private:
    DevNozzle                    m_selected_nozzle;
    std::weak_ptr<DevNozzleRack> m_nozzle_rack;

    // pick
    bool m_enable_manual_nozzle_pick = true;

    // GUI
    wgtDeviceNozzleRackNozzleItem * m_toolhead_nozzle_l{nullptr};
    wgtDeviceNozzleRackNozzleItem * m_toolhead_nozzle_r{nullptr};
    std::unordered_map<int, wgtDeviceNozzleRackNozzleItem *> m_nozzle_items; // from 0 to 5
};

};// end of namespace Slic3r::GUI