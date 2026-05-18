#include "wgtDeviceFilamentPanel.h"

#include "wgtDeviceAmsPreview.h"
#include "wgtDeviceAmsFilaments.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/wxExtensions.hpp"

#include <wx/sizer.h>

#include "slic3r/GUI/DeviceCore/DevNozzleRack.h"

namespace Slic3r::GUI
{

wgtDeviceFilamentPanel::wgtDeviceFilamentPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    CreateGui();
}

void wgtDeviceFilamentPanel::UpdateByMachine(Slic3r::MachineObject* machine_obj)
{
    std::map<int, std::optional<DevAmsSlotId>> nozzlepos2slots;
    if (machine_obj) {
        const auto& rack_type = machine_obj->GetNozzleRack()->GetRackType();
        if (rack_type == DevNozzleRack::RackType_A || rack_type == DevNozzleRack::RackType_B) {
            const auto& total_nozzles = machine_obj->GetNozzleRack()->GetTotalRackNozzlePosSet();
            for (const auto& nozzle_pos : total_nozzles) {
                const auto& rack_nozzle = machine_obj->GetNozzleSystem()->GetNozzleByPosId(nozzle_pos);
                const auto& ams_slots = rack_nozzle.GetBindAmsSlots();
                if (ams_slots.size() > 0) {
                    nozzlepos2slots[rack_nozzle.GetNozzlePosId()] = ams_slots[0];
                } else {
                    nozzlepos2slots[rack_nozzle.GetNozzlePosId()] = std::nullopt;
                }
            }
        }
    }

    if (!nozzlepos2slots.empty()) {
        m_ams_preview->UpdateByMachine(machine_obj, nozzlepos2slots);
        m_ams_filaments->UpdateByMachine(machine_obj, nozzlepos2slots);
    } else {
        m_ams_preview->Reset();
        m_ams_filaments->Reset();
    }
};

void wgtDeviceFilamentPanel::CreateGui()
{
    SetBackgroundColour(*wxWHITE);

    wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* preview_row = new wxBoxSizer(wxHORIZONTAL);

    m_ams_preview = new wgtDeviceAmsPreview(this);
    preview_row->AddStretchSpacer(1);
    preview_row->Add(m_ams_preview, 0, wxALIGN_CENTER_VERTICAL);
    preview_row->AddStretchSpacer(1);
    root->Add(preview_row, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(6));

    m_ams_filaments = new wgtDeviceAmsFilaments(this);
    root->Add(m_ams_filaments, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));

    SetSizer(root);
    Layout();
}

} // namespace Slic3r::GUI
