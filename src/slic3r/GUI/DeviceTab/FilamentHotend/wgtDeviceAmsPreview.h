#pragma once

#include <memory>
#include <vector>

#include <wx/colour.h>
#include <wx/panel.h>

#include "slic3r/GUI/DeviceCore/DevDefs.h"

class Label;
class wxBoxSizer;

namespace Slic3r {
class DevFilaSystem;
class MachineObject;
}

namespace Slic3r::GUI
{

class wgtDeviceAmsPreview : public wxPanel
{
public:
    explicit wgtDeviceAmsPreview(wxWindow* parent);
    ~wgtDeviceAmsPreview() override = default;

    void Reset() { UpdateByMachine(nullptr, {}); };
    void UpdateByMachine(Slic3r::MachineObject* machine_obj, const std::map<int, std::optional<DevAmsSlotId>>& nozzle_slots);

private:
    void CreateGui();

private:
    Label* m_label_title{ nullptr };
    wxPanel* m_panel_slots{ nullptr };
    wxBoxSizer* m_sizer_slots{ nullptr };
    Label* m_label_expand{ nullptr };

    std::map<int, std::optional<DevAmsSlotId>> m_nozzle_slots;
    std::vector<wxWindow*> m_slot_widgets;//AmsPreviewSlotWidget*
};

} // namespace Slic3r::GUI
