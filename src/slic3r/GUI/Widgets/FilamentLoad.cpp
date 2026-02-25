#include "FilamentLoad.hpp"
#include "Label.hpp"
#include "../BitmapCache.hpp"
#include "../I18N.hpp"
#include "../GUI_App.hpp"

#include <wx/simplebook.h>
#include <wx/dcgraph.h>

#include <boost/log/trivial.hpp>

#include "CalibUtils.hpp"

namespace Slic3r {
    namespace GUI {

FilamentLoad::FilamentLoad(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size)
    : wxSimplebook(parent, wxID_ANY, pos, size)
{
    SetBackgroundColour(*wxWHITE);
    m_filament_load_steps = new FilamentStepIndicator(this, wxID_ANY);
    m_filament_unload_steps = new ::FilamentStepIndicator(this, wxID_ANY);
    m_filament_vt_load_steps = new ::FilamentStepIndicator(this, wxID_ANY);

    this->AddPage(m_filament_load_steps, wxEmptyString, false);
    this->AddPage(m_filament_unload_steps, wxEmptyString, false);
    this->AddPage(m_filament_vt_load_steps, wxEmptyString, false);
    //UpdateStepCtrl(false);

    FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_IDLE]                = _L("Idling...");
    FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_PAUSE]               = _L("Pause");
    FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_HEAT_NOZZLE]         = _L("Heat the nozzle");
    FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_CUT_FILAMENT]        = _L("Cut filament");
    FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_PULL_CURR_FILAMENT]  = _L("Pull back current filament");
    FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_PUSH_NEW_FILAMENT]   = _L("Push new filament into extruder");
    FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_GRAB_NEW_FILAMENT]   = _L("Grab new filament");
    FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_PURGE_OLD_FILAMENT]  = _L("Purge old filament");
    //FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_FEED_FILAMENT]       = _L("Feed Filament");
    FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_SWITCH_EXTRUDER]     = _L("Switch") + " " + _L("extruder");
    FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_SWITCH_HOTEND]       = _L("Switch") + " " + _L("hotend");
    FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_AMS_FILA_COOLING]    = _L("Wait for AMS cooling");
    FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_PUSH_SWITCHER_FILA]  = _L("Switch current filament at Filament Track Switch");
    FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_PULL_SWITCHER_FILA]  = _L("Pull back current filament at Filament Track Switch");
    FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_SWITCHER_SWITCH]     = _L("Switch track at Filament Track Switch");
    FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_CONFIRM_EXTRUDED]    = _L("Confirm extruded");
    FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_CHECK_POSITION]      = _L("Check filament location");
}

void FilamentLoad::SetFilamentStep(MachineObject* obj_, DevFilamentStep item_idx, FilamentStepType f_type)
{
    // in latest protocol
    // filament change steps are sent by AMS, so we can directly use them without hardcoding for each model.
    // If steps are provided by AMS, we will ignore the hardcoded steps below.
    if (obj_ && obj_->GetFilaSystem()->GetCurrentFilamentChangeStep().has_value()) {
        item_idx = obj_->GetFilaSystem()->GetCurrentFilamentChangeStep().value();
        if (item_idx == DevFilamentStep::STEP_PAUSE) {
            return; // skip it
        }
    }

    if (item_idx == DevFilamentStep::STEP_IDLE) {
        m_filament_load_steps->Idle();
        m_filament_unload_steps->Idle();
        m_filament_vt_load_steps->Idle();
        if (IsShown()) {Hide(); }
        return;
    }

    if (!IsShown()) {Show();}
    auto iter = FILAMENT_CHANGE_STEP_STRING.find(item_idx);
    wxString step_str = (iter != FILAMENT_CHANGE_STEP_STRING.end()) ? iter->second : wxEmptyString;
    auto step_control = m_filament_load_steps;
    if (f_type == FilamentStepType::STEP_TYPE_LOAD) {
        step_control = m_filament_load_steps;
        if (item_idx > 0 && item_idx < DevFilamentStep::STEP_COUNT) {
            if (GetSelection() != 0) {
                SetSelection(0);
            }
            m_filament_load_steps->SelectItem(m_filament_load_steps->GetItemUseText(step_str));
        }
    }
    else if (f_type == FilamentStepType::STEP_TYPE_UNLOAD) {
        step_control = m_filament_unload_steps;
        if (item_idx > 0 && item_idx < DevFilamentStep::STEP_COUNT) {
            if (GetSelection() != 1) {
                SetSelection(1);
                Layout();
            }
            m_filament_unload_steps->SelectItem(m_filament_unload_steps->GetItemUseText(step_str));
        }
    }
    else if (f_type == FilamentStepType::STEP_TYPE_VT_LOAD) {
        step_control = m_filament_vt_load_steps;
        SetSelection(2);
        Layout();
        if (item_idx > 0 && item_idx < DevFilamentStep::STEP_COUNT) {
            if (item_idx == STEP_CONFIRM_EXTRUDED) {
                m_filament_vt_load_steps->SelectItem(2);
            }
            else {
                m_filament_vt_load_steps->SelectItem(m_filament_vt_load_steps->GetItemUseText(step_str));
            }
        }
    }
    else {
        step_control = m_filament_load_steps;
        if (item_idx > 0 && item_idx < DevFilamentStep::STEP_COUNT) {
            SetSelection(0);
            m_filament_load_steps->SelectItem(m_filament_load_steps->GetItemUseText(step_str));
        }
    }

    wxString slot_info = L"Ams-";
    slot_info = slot_info + std::to_string(m_ams_id);
    slot_info = slot_info + L'-';
    slot_info = slot_info + std::to_string(m_slot_id);
    slot_info = slot_info + L" Slot";
    step_control->SetSlotInformation(slot_info);
}

void FilamentLoad::SetupSteps(MachineObject* obj_, bool has_fila_to_switch) {
    m_filament_load_steps->DeleteAllItems();
    m_filament_unload_steps->DeleteAllItems();
    m_filament_vt_load_steps->DeleteAllItems();

    set_min_size(wxSize(wxSize(FromDIP(300), FromDIP(215))));
    set_max_size(wxSize(wxSize(FromDIP(300), FromDIP(215))));

    // in latest protocol
    // filament change steps are sent by AMS, so we can directly use them without hardcoding for each model.
    // If steps are provided by AMS, we will ignore the hardcoded steps below.
    if (obj_ && obj_->GetFilaSystem() && !obj_->GetFilaSystem()->GetFilamentChangeSteps().empty()) {
        const auto& steps = obj_->GetFilaSystem()->GetFilamentChangeSteps();
        if(steps.size() > 5){
            set_min_size(wxSize(wxSize(FromDIP(300), FromDIP(215) + (steps.size() - 5) * 30)));
            set_max_size(wxSize(wxSize(FromDIP(300), FromDIP(215) + (steps.size() - 5) * 30)));
        }

        for (auto step : steps) {
            auto iter = FILAMENT_CHANGE_STEP_STRING.find(step);
            if (iter == FILAMENT_CHANGE_STEP_STRING.end()) {
                BOOST_LOG_TRIVIAL(error) << "Unknown filament change step: " << static_cast<int>(step);
                continue;
            }

            m_filament_load_steps->AppendItem(iter->second);
            m_filament_unload_steps->AppendItem(iter->second);
            m_filament_vt_load_steps->AppendItem(iter->second);
        }

        Layout();
        Fit();
        return;
    }

    if (m_ams_model == DevAmsType::AMS || m_ext_model == DevAmsType::N3F || m_ext_model == DevAmsType::N3S) {
        if (has_fila_to_switch) {
            m_filament_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_HEAT_NOZZLE]);
            m_filament_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_CUT_FILAMENT]);
            m_filament_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_PULL_CURR_FILAMENT]);
            m_filament_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_PUSH_NEW_FILAMENT]);
            m_filament_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_PURGE_OLD_FILAMENT]);
        }
        else {
            m_filament_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_HEAT_NOZZLE]);
            m_filament_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_PUSH_NEW_FILAMENT]);
            m_filament_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_PURGE_OLD_FILAMENT]);
        }

        m_filament_vt_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_HEAT_NOZZLE]);
        m_filament_vt_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_PUSH_NEW_FILAMENT]);
        m_filament_vt_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_GRAB_NEW_FILAMENT]);
        m_filament_vt_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_PURGE_OLD_FILAMENT]);

        m_filament_unload_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_HEAT_NOZZLE]);
        m_filament_unload_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_CUT_FILAMENT]);
        m_filament_unload_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_PULL_CURR_FILAMENT]);
    }


    if (m_ams_model == DevAmsType::AMS_LITE || m_ext_model == DevAmsType::AMS_LITE) {
        m_filament_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_HEAT_NOZZLE]);
        m_filament_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_CHECK_POSITION]);
        m_filament_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_CUT_FILAMENT]);
        m_filament_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_PULL_CURR_FILAMENT]);
        m_filament_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_PUSH_NEW_FILAMENT]);
        m_filament_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_PURGE_OLD_FILAMENT]);

        m_filament_vt_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_HEAT_NOZZLE]);
        m_filament_vt_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_CHECK_POSITION]);
        m_filament_vt_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_CUT_FILAMENT]);
        m_filament_vt_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_PULL_CURR_FILAMENT]);
        m_filament_vt_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_PUSH_NEW_FILAMENT]);
        m_filament_vt_load_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_PURGE_OLD_FILAMENT]);

        m_filament_unload_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_HEAT_NOZZLE]);
        m_filament_unload_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_CHECK_POSITION]);
        m_filament_unload_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_CUT_FILAMENT]);
        m_filament_unload_steps->AppendItem(FILAMENT_CHANGE_STEP_STRING[DevFilamentStep::STEP_PULL_CURR_FILAMENT]);
    }

    Layout();
    Fit();
}


void FilamentLoad::show_nofilament_mode(bool show)
{
    m_filament_load_steps->DeleteAllItems();
    m_filament_unload_steps->DeleteAllItems();
    m_filament_vt_load_steps->DeleteAllItems();
    m_filament_load_steps->Idle();
    m_filament_unload_steps->Idle();
    m_filament_vt_load_steps->Idle();
    this->Layout();
    Refresh();
    /*if (!show)
    {
        m_filament_load_steps->Idle();
        m_filament_unload_steps->Idle();
        m_filament_vt_load_steps->Idle();
    }
    else {
        this->Show();
        this->Layout();
    }*/
}

void FilamentLoad::set_min_size(const wxSize& minSize) {
    this->SetMinSize(minSize);
    m_filament_load_steps->SetMinSize(minSize);
    m_filament_unload_steps->SetMinSize(minSize);
    m_filament_vt_load_steps->SetMinSize(minSize);
}

void FilamentLoad::set_max_size(const wxSize& minSize) {
    this->SetMaxSize(minSize);
    m_filament_load_steps->SetMaxSize(minSize);
    m_filament_unload_steps->SetMaxSize(minSize);
    m_filament_vt_load_steps->SetMaxSize(minSize);
}

void FilamentLoad::set_background_color(const wxColour& colour) {
    m_filament_load_steps->SetBackgroundColour(colour);
    m_filament_unload_steps->SetBackgroundColour(colour);
    m_filament_vt_load_steps->SetBackgroundColour(colour);
}

}} // namespace Slic3r::GUI