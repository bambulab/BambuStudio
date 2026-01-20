//**********************************************************/
/* File: wgtDeviceNozzleSelect.cpp
*  Description: The panel to select nozzle
*
*  \n class wgtDeviceNozzleSelect;
//**********************************************************/

#include "wgtDeviceNozzleSelect.h"
#include "wgtDeviceNozzleRack.h"

#include "slic3r/GUI/I18N.hpp"

static wxColour s_gray_clr("#B0B0B0");
static wxColour s_hgreen_clr("#00AE42");
static wxColour s_red_clr("#D01B1B");

static std::vector<int> a_nozzle_seq = {0, 2, 4, 1, 3, 5};
static std::vector<int> b_nozzle_seq = {1, 3, 5, 0, 2, 4};

wxDEFINE_EVENT(EVT_NOZZLE_RACK_ITEM_CLICKED, wxCommandEvent);

namespace Slic3r::GUI {

wgtDeviceNozzleRackSelect::wgtDeviceNozzleRackSelect(wxWindow *parent) : wxPanel(parent, wxID_ANY) { CreateGui(); }

static wxPanel* s_create_title(wxWindow *parent, const wxString& text)
{
    wxPanel *panel = new wxPanel(parent, wxID_ANY);

    auto title  = new Label(panel, text);
    title->SetFont(::Label::Body_13);
    title->SetBackgroundColour(*wxWHITE);
    title->SetForegroundColour(0x909090);

    auto split_line = new wxPanel(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    split_line->SetBackgroundColour(0xeeeeee);
    split_line->SetMinSize(wxSize(-1, 1));
    split_line->SetMaxSize(wxSize(-1, 1));

    wxBoxSizer *sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(0, 0, 0, wxEXPAND, 0);
    sizer->Add(title, 0, wxALIGN_CENTER, 0);
    sizer->Add(split_line, 1, wxALIGN_CENTER_VERTICAL | wxEXPAND, 0);
    panel->SetSizer(sizer);
    panel->Layout();
    return panel;
}

void wgtDeviceNozzleRackSelect::CreateGui()
{
    wxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);

    // nozzles
    wxGridSizer *nozzle_sizer = new wxGridSizer(2, 3, FromDIP(10), FromDIP(10));
    for (auto idx : a_nozzle_seq) {
        wgtDeviceNozzleRackNozzleItem *nozzle_item = new wgtDeviceNozzleRackNozzleItem(this, idx);
        nozzle_item->EnableSelect();
        nozzle_item->Bind(EVT_NOZZLE_RACK_NOZZLE_ITEM_SELECTED, &wgtDeviceNozzleRackSelect::OnNozzleItemSelected, this);
        m_nozzle_items[idx]                        = nozzle_item;
        nozzle_sizer->Add(nozzle_item, 0);
    }

    // toolhead area
    m_toolhead_nozzle = new wgtDeviceNozzleRackNozzleItem(this, 0);
    m_toolhead_nozzle->EnableSelect();
    m_toolhead_nozzle->SetDisplayIdText("R");
    m_toolhead_nozzle->Bind(EVT_NOZZLE_RACK_NOZZLE_ITEM_SELECTED, &wgtDeviceNozzleRackSelect::OnNozzleItemSelected, this);

    main_sizer->Add(s_create_title(this, _L("Hotend Rack")), 0, wxEXPAND);
    main_sizer->Add(nozzle_sizer, 0, wxTOP | wxBOTTOM | wxALIGN_LEFT, FromDIP(12));
    main_sizer->Add(s_create_title(this, _L("ToolHead")), 0, wxEXPAND);
    main_sizer->AddSpacer(FromDIP(12));
    main_sizer->Add(m_toolhead_nozzle, 0, wxALIGN_LEFT);

    SetBackgroundColour(*wxWHITE);
    SetSizer(main_sizer);
    Layout();
    Fit();
}

void wgtDeviceNozzleRackSelect::UpdateNozzleInfos(std::shared_ptr<DevNozzleRack> rack)
{
    m_nozzle_rack = rack;
    if (rack) {
        {
            const auto& toolhead_info = rack->GetNozzleSystem()->GetExtNozzle(MAIN_EXTRUDER_ID);
            m_toolhead_nozzle->Update(rack, toolhead_info.IsOnRack());
            if (toolhead_info.IsUnknown()) {
                if (m_toolhead_nozzle->GetToolTipText() != _L("Nozzle information needs to be read")) {
                    m_toolhead_nozzle->SetToolTip(_L("Nozzle information needs to be read"));
                }
            } else {
                m_toolhead_nozzle->SetToolTip(wxEmptyString);
            }
        }

        for (const auto& item : m_nozzle_items) {
            const auto& nozzle_info = rack->GetNozzleSystem()->GetRackNozzle(item.first);
            item.second->Update(rack, nozzle_info.IsOnRack());

            if (nozzle_info.IsUnknown()) {
                if (item.second->GetToolTipText() != _L("Nozzle information needs to be read")) {
                    item.second->SetToolTip(_L("Nozzle information needs to be read"));
                }
            } else {
                item.second->SetToolTip(wxEmptyString);
            }
        }
    }
}

static void s_enable_item_if_match(wgtDeviceNozzleRackNozzleItem* item, 
                                   const DevNozzle& nozzle_info,
                                   const DevNozzle& selected_nozzle)
{
    if (item) {
        if (!nozzle_info.IsEmpty() && !nozzle_info.IsAbnormal() && !nozzle_info.IsUnknown() &&
            nozzle_info.GetNozzleType() == selected_nozzle.GetNozzleType() &&
            nozzle_info.GetNozzleDiameter() == selected_nozzle.GetNozzleDiameter() &&
            nozzle_info.GetNozzleFlowType() == selected_nozzle.GetNozzleFlowType()) {
            item->SetDisable(false);
        } else {
            item->SetDisable(true);
        }
    }
}

void wgtDeviceNozzleRackSelect::UpdatSelectedNozzle(std::shared_ptr<DevNozzleRack> rack, int selected_nozzle_pos_id)
{
    m_nozzle_rack = rack;
    if (rack) {
        if (selected_nozzle_pos_id < 0) {
            ClearSelection();
        } else if (selected_nozzle_pos_id < 0x10) {
            SetSelectedNozzle(rack->GetNozzleSystem()->GetExtNozzle(MAIN_EXTRUDER_ID));
        } else {
            SetSelectedNozzle(rack->GetNozzle(selected_nozzle_pos_id - 0x10));
        }

        if (m_enable_manual_nozzle_pick) {
            s_enable_item_if_match(m_toolhead_nozzle, rack->GetNozzleSystem()->GetExtNozzle(MAIN_EXTRUDER_ID), m_selected_nozzle);
            for (auto& item : m_nozzle_items) {
                s_enable_item_if_match(item.second, rack->GetNozzleSystem()->GetRackNozzle(item.first), m_selected_nozzle);
            }
        }
    }
}

void wgtDeviceNozzleRackSelect::UpdatSelectedNozzles(std::shared_ptr<DevNozzleRack> rack,
                                                     std::vector<int> selected_nozzle_pos_vec,
                                                     bool use_dynamic_switch)
{
    m_nozzle_rack = rack;
    m_enable_manual_nozzle_pick = !use_dynamic_switch;

    UpdateNozzleInfos(rack);
    if (!use_dynamic_switch) {
        if (selected_nozzle_pos_vec.size() > 0) {
            return UpdatSelectedNozzle(rack, selected_nozzle_pos_vec.at(0));
        } else {
            return ClearSelection();
        }
    }

    if (rack) {
        ClearSelection();
        for (const auto& pos_id : selected_nozzle_pos_vec) {
            if (pos_id == MAIN_EXTRUDER_ID) {
                m_toolhead_nozzle->SetSelected(true);
            } else {
                if (auto it = m_nozzle_items.find(pos_id - 0x10); it != m_nozzle_items.end()) {
                    it->second->SetSelected(true);
                }
            }
        }

        if (!m_toolhead_nozzle->IsSelected()) {
            m_toolhead_nozzle->SetDisable(true);
        }

        for (const auto& item : m_nozzle_items) {
            if (!item.second->IsSelected()) {
                item.second->SetDisable(true);
            }
        }
    }
}

void wgtDeviceNozzleRackSelect::ClearSelection() 
{
    m_selected_nozzle = DevNozzle();
    m_toolhead_nozzle->SetSelected(false);
    for (auto &item : m_nozzle_items) { item.second->SetSelected(false); }
}

void wgtDeviceNozzleRackSelect::SetSelectedNozzle(const DevNozzle &nozzle)
{
    if (m_selected_nozzle.GetNozzlePosId() != nozzle.GetNozzlePosId()) {
        ClearSelection();

        m_selected_nozzle = nozzle;
        if (m_selected_nozzle.IsNormal()) {
            if (!m_selected_nozzle.IsOnRack()) {
                m_toolhead_nozzle->SetSelected(true);
            } else {
                auto it = m_nozzle_items.find(m_selected_nozzle.GetNozzleId());
                if (it != m_nozzle_items.end()) { it->second->SetSelected(true); }
            }
        }
    }
}

void wgtDeviceNozzleRackSelect::OnNozzleItemSelected(wxCommandEvent &evt)
{
    if (!m_enable_manual_nozzle_pick) {
        return;
    }

    auto *item = dynamic_cast<wgtDeviceNozzleRackNozzleItem *>(evt.GetEventObject());
    if (item; auto ptr = m_nozzle_rack.lock()) {
        if (item == m_toolhead_nozzle) {
            SetSelectedNozzle(ptr->GetNozzleSystem()->GetExtNozzle(MAIN_EXTRUDER_ID));
        } else {
            SetSelectedNozzle(ptr->GetNozzle(item->GetNozzleId()));
        }
    }

    wxCommandEvent change_evt(EVT_NOZZLE_RACK_ITEM_CLICKED, GetId());
    change_evt.SetEventObject(this);
    ProcessEvent(change_evt);
    evt.Skip();
}

void wgtDeviceNozzleRackSelect::Rescale()
{
    m_toolhead_nozzle->Rescale();
    for (auto &item : m_nozzle_items) { item.second->Rescale(); }
}

}; // namespace Slic3r::GUI
