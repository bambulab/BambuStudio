#pragma once

#include <wx/panel.h>
#include <wx/textctrl.h>
#include <string>

#include "slic3r/GUI/Widgets/StaticBox.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"
#include "slic3r/GUI/Widgets/wgtCircleItem.hpp"

#include "slic3r/GUI/DeviceCore/DevUtil.h"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"

class ScalableBitmap;

namespace Slic3r {
class DevNozzleRack;
class MachineObject;
}

namespace Slic3r::GUI {

class wgtDeviceHotendItem : public StaticBox
{
public:
    enum NOZZLE_STATUS { NOZZLE_EMPTY, NOZZLE_NORMAL, NOZZLE_UNKNOWN, NOZZLE_ERROR };

public:
    wgtDeviceHotendItem(wxWindow *parent, int hotend_id);

public:
    // Update nozzle status (icon, diameter, flow type)
    // Shows NOZZLE_EMPTY when rack is null or slot is empty
    void UpdateNozzleInfo();

    // Called by Panel, passes device object to update temperature and send commands
    void UpdateByMachine(Slic3r::MachineObject *machine_obj);

    // Get hotend display index (1~9)
    int GetHotendId() const { return m_hotend_id; }

    int GetNozzlePosId() const { return m_hotend_id + 15; } // nozzle_pos_id in rack: hotend_id starts from 1, pos_id starts from 16

    // Reload all bitmaps after DPI rescale
    void Rescale();

private:
    void CreateGui();
    void SetNozzleStatus(NOZZLE_STATUS status, const wxString &str1, const wxString &str2, const std::string &color);
    void OnBtnNozzleStatus(wxMouseEvent &evt);

    // Update temperature display (internal)
    void UpdateTempDisplay(int cur_temp, int target_temp);

    // Temperature edit events
    void OnTempTextEnter(wxCommandEvent &evt);
    void OnTempKillFocus(wxFocusEvent &evt);
    void SendTempCommand();

private:
    int           m_hotend_id; // Display index, 1~9

    std::string   m_filament_color;
    NOZZLE_STATUS m_status = NOZZLE_STATUS::NOZZLE_EMPTY;

    // Icon bitmaps (lazy-loaded)
    ScalableBitmap *m_nozzle_normal_image{nullptr};
    ScalableBitmap *m_nozzle_empty_image{nullptr};
    ScalableBitmap *m_nozzle_unknown_image{nullptr};
    ScalableBitmap *m_nozzle_error_image{nullptr};

    // GUI controls
    wxStaticBitmap *m_nozzle_icon{nullptr};
    wgtCircleItem  *m_nozzle_label_id{nullptr};
    Label          *m_nozzle_label_1{nullptr};
    wxStaticBitmap *m_nozzle_status_icon{nullptr};
    Label          *m_nozzle_label_2{nullptr};

    // Temperature edit controls
    Label      *m_label_cur_temp{nullptr};
    Label      *m_label_sep{nullptr};
    wxTextCtrl *m_text_target_temp{nullptr};
    Label      *m_label_degree{nullptr};

    Slic3r::MachineObject *m_obj{nullptr};

    // Temperature editing state
    bool m_temp_editing{false};

    DevNozzle m_nozzle_info;

    DevDirtyHandler<int> m_nozzle_temp_dirty_handler{-1, 5, DirtyMode::COUNTER};
};

} // namespace Slic3r::GUI
