#include "Tab.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Model.hpp"
#include "slic3r/Utils/Http.hpp"

#include <wx/app.h>
#include <wx/button.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>

#include <wx/bmpcbox.h>
#include <wx/bmpbuttn.h>
#include <wx/treectrl.h>
#include <wx/imaglist.h>
#include <wx/settings.h>
#include <wx/filedlg.h>
#include <wx/wupdlock.h>
#include <wx/dataview.h>
#include <wx/tglbtn.h>

#include "wxExtensions.hpp"
#include "GUI_App.hpp"
#include "GUI_ObjectList.hpp"
#include "Plater.hpp"
#include "MainFrame.hpp"
#include "format.hpp"

namespace Slic3r {
namespace GUI {

#define REFRESH_INTERVAL       1000


TrayListModel::TrayListModel() :
    wxDataViewVirtualListModel(0)
{
    ;
}

void TrayListModel::GetValueByRow(wxVariant& variant,
    unsigned int row, unsigned int col) const
{
    switch (col) {
    case Col_TrayTitle:
        if (row >= m_titleColValues.GetCount())
            variant = wxString::Format("N/A", row);
        else
            variant = m_titleColValues[row];
        break;
    case Col_TrayColor:
        if (row >= m_colorColValues.GetCount())
            variant = wxString::Format("N/A", row);
        else
            variant = m_colorColValues[row];
        break;
    case Col_TrayMeterial:
        if (row >= m_meterialColValues.GetCount())
            variant = wxString::Format("N/A", row);
        else
            variant = m_meterialColValues[row];
        break;
    case Col_TrayWeight:
        if (row >= m_weightColValues.GetCount())
            variant = wxString::Format("N/A", row);
        else
            variant = m_weightColValues[row];
        break;
    case Col_TrayDiameter:
        if (row >= m_diameterColValues.GetCount())
            variant = wxString::Format("N/A", row);
        else
            variant = m_diameterColValues[row];
        break;
    case Col_TrayTime:
        if (row >= m_timeColValues.GetCount())
            variant = wxString::Format("N/A", row);
        else
            variant = m_timeColValues[row];
        break;
    case Col_TraySN:
        if (row >= m_snColValues.GetCount())
            variant = wxString::Format("N/A", row);
        else
            variant = m_snColValues[row];
        break;
    case Col_TrayManufacturer:
        if (row >= m_manufacturerColValues.GetCount())
            variant = wxString::Format("N/A", row);
        else
            variant = m_manufacturerColValues[row];
        break;
    case Col_TraySaturability:
        if (row >= m_saturabilityColValues.GetCount())
            variant = wxString::Format("N/A", row);
        else
            variant = m_saturabilityColValues[row];
        break;
    case Col_TrayTransmittance:
        if (row >= m_transmittanceColValues.GetCount())
            variant = wxString::Format("N/A", row);
        else
            variant = m_transmittanceColValues[row];
        break;
    case Col_TraySmooth:
        if (row >= m_smoothColValues.GetCount())
            variant = wxString::Format("N/A", row);
        else
            variant = m_smoothColValues[row];
        break;
    default:
        break;
    }
}

bool TrayListModel::GetAttrByRow(unsigned int row, unsigned int col,
    wxDataViewItemAttr& attr) const
{
    return true;
}

bool TrayListModel::SetValueByRow(const wxVariant& variant,
    unsigned int row, unsigned int col)
{
    switch (col)
    {
    case Col_TrayTitle:
    case Col_TrayColor:
    case Col_TrayMeterial:
    case Col_TrayWeight:
    case Col_TrayDiameter:
    case Col_TrayTime:
    case Col_TraySN:
    case Col_TrayManufacturer:
    case Col_TraySaturability:
    case Col_TrayTransmittance:
    case Col_TraySmooth:
        return true;
    default:
        break;
    }
    return false;
}

void TrayListModel::update(MachineObject* obj)
{
    if (!obj) return;

    m_titleColValues.clear();
    m_colorColValues.clear();
    m_meterialColValues.clear();
    m_weightColValues.clear();
    m_diameterColValues.clear();
    m_timeColValues.clear();
    m_snColValues.clear();
    m_manufacturerColValues.clear();
    m_saturabilityColValues.clear();
    m_transmittanceColValues.clear();
    m_smoothColValues.clear();

    std::map<std::string, Ams*>::iterator ams_it;
    std::map<std::string, AmsTray*>::iterator tray_it;
    int tray_index = 0;
    for (ams_it = obj->amsList.begin(); ams_it != obj->amsList.end(); ams_it++) {
        if (ams_it->second) {
            for (tray_it = ams_it->second->trayList.begin(); tray_it != ams_it->second->trayList.end(); tray_it++) {
                AmsTray* tray = tray_it->second;
                if (tray) {
                    tray_index++;
                    wxString title_text = wxString::Format("tray %d", tray_index);
                    m_titleColValues.push_back(title_text);
                    wxString color_text = wxString::Format("%s", tray->wx_color.GetAsString());
                    m_colorColValues.push_back(color_text);
                    wxString meterial_text = wxString::Format("%s", tray->meterial);
                    m_meterialColValues.push_back(meterial_text);
                    wxString weight_text = wxString::Format("%sg", tray->weight);
                    m_weightColValues.push_back(weight_text);
                    wxString diameter_text = wxString::Format("%0.2f", tray->diameter);
                    m_diameterColValues.push_back(diameter_text);
                    wxString time_text = wxString::Format("%s", tray->time);
                    m_timeColValues.push_back(time_text);
                    wxString sn_text = wxString::Format("%s", tray->sn);
                    m_snColValues.push_back(sn_text);
                    wxString manufacturer_text = wxString::Format("%s", tray->manufacturer);
                    m_manufacturerColValues.push_back(manufacturer_text);
                    wxString saturability_text = wxString::Format("%s", tray->saturability);
                    m_saturabilityColValues.push_back(saturability_text);
                    wxString transmittance_text = wxString::Format("%s", tray->transmittance);
                    m_transmittanceColValues.push_back(transmittance_text);
                    wxString smooth_text = wxString::Format("%s", tray->smooth);
                    m_smoothColValues.push_back(smooth_text);
                }
            }
        }
    }

    Reset(m_titleColValues.GetCount());
}
void TrayListModel::clear_data()
{
    m_titleColValues.clear();
    m_colorColValues.clear();
    m_meterialColValues.clear();
    m_weightColValues.clear();
    m_diameterColValues.clear();
    m_timeColValues.clear();
    m_snColValues.clear();
    m_manufacturerColValues.clear();
    m_saturabilityColValues.clear();
    m_transmittanceColValues.clear();
    m_smoothColValues.clear();

    Reset(0);
}


SubTaskListModel::SubTaskListModel() :
    wxDataViewVirtualListModel(0)
{
    ;
}

void SubTaskListModel::GetValueByRow(wxVariant& variant,
    unsigned int row, unsigned int col) const
{
    switch (col) {
    case Col_SubTaskName:
        if (row >= m_nameColValues.GetCount())
            variant = wxString::Format("N/A", row);
        else
            variant = m_nameColValues[row];
        break;
    case Col_SubTaskDuration:
        if (row >= m_durationColValues.GetCount())
            variant = wxString::Format("N/A", row);
        else
            variant = m_durationColValues[row];
        break;
    case Col_SubTaskWeight:
        if (row >= m_WeightColValues.GetCount())
            variant = wxString::Format("N/A", row);
        else
            variant = m_WeightColValues[row];
        break;
    default:
        break;
    }
}

bool SubTaskListModel::GetAttrByRow(unsigned int row, unsigned int col,
    wxDataViewItemAttr& attr) const
{
    return true;
}

bool SubTaskListModel::SetValueByRow(const wxVariant& variant,
    unsigned int row, unsigned int col)
{
    switch (col)
    {
    case Col_SubTaskName:
        return true;
    case Col_SubTaskDuration:
        return true;
    case Col_SubTaskWeight:
        return true;
    default:
        break;
    }
    return false;
}

void SubTaskListModel::add_subtask(BBLSubTask* subtask)
{
    wxString task_name_text = "N/A";
    if (!subtask->task_name.empty()) {
        task_name_text = wxString::Format("%s", subtask->task_name);
    }
    m_nameColValues.push_back(task_name_text);

    wxString duration_text = "N/A";
    if (!subtask->task_prediction.empty()) {
        try {
            std::string duration = get_time_dhms(stoi(subtask->task_prediction));
            duration_text = wxString::Format("%s", duration);
        }
        catch (...) {
            ;
        }
    }
    m_durationColValues.push_back(duration_text);
    wxString weight_text = "N/A";
    if (!subtask->task_weight.empty())
        weight_text = wxString::Format("%sg", subtask->task_weight);
    m_WeightColValues.push_back(weight_text);
}

void SubTaskListModel::update_subtask(BBLSubTask* subtask)
{
    m_nameColValues.clear();
    m_durationColValues.clear();
    m_WeightColValues.clear();
    add_subtask(subtask);
    Reset(m_nameColValues.Count());
}

void SubTaskListModel::update_task(BBLTask* task)
{
    if (!task) return;

    m_nameColValues.clear();
    m_durationColValues.clear();
    m_WeightColValues.clear();

    std::vector<BBLSubTask*>::iterator it;
    for (it = task->subtasks.begin(); it != task->subtasks.end(); it++) {
        add_subtask((*it));
    }

    Reset(m_nameColValues.GetCount());
}

void SubTaskListModel::clear_data()
{
    m_nameColValues.clear();
    m_durationColValues.clear();
    m_WeightColValues.clear();
    Reset(0);
}


MonitorPanel::MonitorPanel(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style) : wxPanel(parent, id, pos, size, style)
{
    init_bitmap();

    wxBoxSizer* bSizer_top;
    bSizer_top = new wxBoxSizer(wxHORIZONTAL);

    m_panel_left = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_panel_left->SetMinSize(wxSize(420, -1));
    m_panel_left->SetMaxSize(wxSize(420, -1));

    wxBoxSizer* bSizer_left;
    bSizer_left = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer* bSizer_select_printer;
    bSizer_select_printer = new wxBoxSizer(wxHORIZONTAL);

    m_staticText_machine_title = new wxStaticText(m_panel_left, wxID_ANY, wxT("Printer"), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE);
    m_staticText_machine_title->Wrap(-1);
    bSizer_select_printer->Add(m_staticText_machine_title, 0, wxALIGN_CENTER | wxALL, 5);

    wxArrayString m_choice_machineChoices;
    m_choice_machine = new wxChoice(m_panel_left, wxID_ANY, wxDefaultPosition, wxDefaultSize, m_choice_machineChoices, 0);
    m_choice_machine->SetSelection(0);
    bSizer_select_printer->Add(m_choice_machine, 1, wxALL | wxEXPAND, 5);


    bSizer_left->Add(bSizer_select_printer, 0, wxEXPAND, 5);

    wxStaticBoxSizer* sbSizer_status;
    sbSizer_status = new wxStaticBoxSizer(new wxStaticBox(m_panel_left, wxID_ANY, wxT("Status")), wxVERTICAL);

    wxBoxSizer* bSizer_left_up;
    bSizer_left_up = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer* bSizer_device;
    bSizer_device = new wxBoxSizer(wxHORIZONTAL);

    m_staticText_machine_status = new wxStaticText(sbSizer_status->GetStaticBox(), wxID_ANY, wxT("Printer"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_machine_status->Wrap(-1);
    bSizer_device->Add(m_staticText_machine_status, 0, wxALL, 5);

    m_staticText_machine_name = new wxStaticText(sbSizer_status->GetStaticBox(), wxID_ANY, wxT("BBL_3D_Printer"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    m_staticText_machine_name->Wrap(-1);
    bSizer_device->Add(m_staticText_machine_name, 1, wxALIGN_CENTER | wxALL, 5);

    m_staticText_wifi_signal = new wxStaticText(sbSizer_status->GetStaticBox(), wxID_ANY, wxT("0"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_wifi_signal->Wrap(-1);
    bSizer_device->Add(m_staticText_wifi_signal, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT | wxRIGHT, 5);


    bSizer_left_up->Add(bSizer_device, 0, wxALL | wxEXPAND, 0);

    wxGridSizer* gSizer_status;
    gSizer_status = new wxGridSizer(3, 2, 0, 0);

    m_staticText_printing_title = new wxStaticText(sbSizer_status->GetStaticBox(), wxID_ANY, wxT("Status"), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE | wxSIMPLE_BORDER);
    m_staticText_printing_title->Wrap(-1);
    gSizer_status->Add(m_staticText_printing_title, 0, wxALL | wxEXPAND, 0);

    m_staticText_printing_val = new wxStaticText(sbSizer_status->GetStaticBox(), wxID_ANY, wxT("Ready"), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE | wxSIMPLE_BORDER);
    m_staticText_printing_val->Wrap(-1);
    gSizer_status->Add(m_staticText_printing_val, 1, wxALL | wxEXPAND, 0);

    m_staticText_capacity_title = new wxStaticText(sbSizer_status->GetStaticBox(), wxID_ANY, wxT("Capacity"), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE | wxSIMPLE_BORDER);
    m_staticText_capacity_title->Wrap(-1);
    gSizer_status->Add(m_staticText_capacity_title, 0, wxALL | wxEXPAND, 0);

    m_staticText_capacity_val = new wxStaticText(sbSizer_status->GetStaticBox(), wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE | wxSIMPLE_BORDER);
    m_staticText_capacity_val->Wrap(-1);
    gSizer_status->Add(m_staticText_capacity_val, 1, wxALL | wxEXPAND, 0);

    m_staticText_bed_title = new wxStaticText(sbSizer_status->GetStaticBox(), wxID_ANY, wxT("Bed"), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE | wxSIMPLE_BORDER);
    m_staticText_bed_title->Wrap(-1);
    gSizer_status->Add(m_staticText_bed_title, 0, wxALL | wxEXPAND, 0);

    m_staticText_bed_value = new wxStaticText(sbSizer_status->GetStaticBox(), wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE | wxSIMPLE_BORDER);
    m_staticText_bed_value->Wrap(-1);
    gSizer_status->Add(m_staticText_bed_value, 0, wxALL | wxEXPAND, 0);

    m_staticText_nozzle_title = new wxStaticText(sbSizer_status->GetStaticBox(), wxID_ANY, wxT("Nozzle"), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE | wxSIMPLE_BORDER);
    m_staticText_nozzle_title->Wrap(-1);
    gSizer_status->Add(m_staticText_nozzle_title, 0, wxALL | wxEXPAND, 0);

    m_staticText_nozzle_value = new wxStaticText(sbSizer_status->GetStaticBox(), wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE | wxSIMPLE_BORDER);
    m_staticText_nozzle_value->Wrap(-1);
    gSizer_status->Add(m_staticText_nozzle_value, 0, wxALL | wxEXPAND, 0);

    m_staticText_sn_title = new wxStaticText(sbSizer_status->GetStaticBox(), wxID_ANY, wxT("SN"), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE | wxSIMPLE_BORDER);
    m_staticText_sn_title->Wrap(-1);
    gSizer_status->Add(m_staticText_sn_title, 0, wxALL | wxEXPAND, 0);

    m_staticText_sn_value = new wxStaticText(sbSizer_status->GetStaticBox(), wxID_ANY, wxT("XXXX-XXXX"), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE | wxSIMPLE_BORDER);
    m_staticText_sn_value->Wrap(-1);
    gSizer_status->Add(m_staticText_sn_value, 0, wxALL | wxEXPAND, 0);


    bSizer_left_up->Add(gSizer_status, 0, wxALL | wxEXPAND, 0);

    m_dataViewCtrl_ams = new wxDataViewCtrl(sbSizer_status->GetStaticBox(), wxID_ANY, wxDefaultPosition, wxDefaultSize, 0 | wxSIMPLE_BORDER);
    m_dataViewCtrl_ams->SetMinSize(wxSize(-1, 60));

    bSizer_left_up->Add(m_dataViewCtrl_ams, 1, wxALL | wxEXPAND, 0);


    sbSizer_status->Add(bSizer_left_up, 1, wxEXPAND, 0);


    bSizer_left->Add(sbSizer_status, 1, wxEXPAND, 0);

    m_staticline1 = new wxStaticLine(m_panel_left, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL);
    bSizer_left->Add(m_staticline1, 0, wxEXPAND | wxALL, 5);

    wxStaticBoxSizer* sbSizer_task;
    sbSizer_task = new wxStaticBoxSizer(new wxStaticBox(m_panel_left, wxID_ANY, wxT("Task")), wxVERTICAL);

    wxBoxSizer* bSizer__task_caption;
    bSizer__task_caption = new wxBoxSizer(wxHORIZONTAL);


    sbSizer_task->Add(bSizer__task_caption, 0, wxALL | wxEXPAND, 0);

    wxFlexGridSizer* fgSizer_task_info;
    fgSizer_task_info = new wxFlexGridSizer(0, 2, 0, 0);
    fgSizer_task_info->AddGrowableCol(1);
    fgSizer_task_info->SetFlexibleDirection(wxBOTH);
    fgSizer_task_info->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);

    m_staticText_project_title = new wxStaticText(sbSizer_task->GetStaticBox(), wxID_ANY, wxT("Project Name"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT | wxSIMPLE_BORDER);
    m_staticText_project_title->Wrap(-1);
    m_staticText_project_title->Hide();
    m_staticText_project_title->SetMinSize(wxSize(100, -1));
    m_staticText_project_title->SetMaxSize(wxSize(100, -1));

    fgSizer_task_info->Add(m_staticText_project_title, 0, wxALL | wxEXPAND, 0);

    m_staticText_project_name = new wxStaticText(sbSizer_task->GetStaticBox(), wxID_ANY, wxT("BBL_Project"), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE | wxSIMPLE_BORDER);
    m_staticText_project_name->Wrap(-1);
    m_staticText_project_name->Hide();

    fgSizer_task_info->Add(m_staticText_project_name, 1, wxALL | wxEXPAND, 0);

    m_staticText_profile_title = new wxStaticText(sbSizer_task->GetStaticBox(), wxID_ANY, wxT("Profile Name"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT | wxSIMPLE_BORDER);
    m_staticText_profile_title->Wrap(-1);
    m_staticText_profile_title->Hide();
    m_staticText_profile_title->SetMinSize(wxSize(100, -1));
    m_staticText_profile_title->SetMaxSize(wxSize(100, -1));

    fgSizer_task_info->Add(m_staticText_profile_title, 0, wxALL | wxEXPAND, 0);

    m_staticText_profile_value = new wxStaticText(sbSizer_task->GetStaticBox(), wxID_ANY, wxT("BBL_Profile"), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE | wxSIMPLE_BORDER);
    m_staticText_profile_value->Wrap(-1);
    m_staticText_profile_value->Hide();

    fgSizer_task_info->Add(m_staticText_profile_value, 1, wxALL | wxEXPAND, 0);

    m_staticText_task_title = new wxStaticText(sbSizer_task->GetStaticBox(), wxID_ANY, wxT("Task"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT | wxSIMPLE_BORDER);
    m_staticText_task_title->Wrap(-1);
    m_staticText_task_title->SetMinSize(wxSize(100, -1));
    m_staticText_task_title->SetMaxSize(wxSize(100, -1));

    fgSizer_task_info->Add(m_staticText_task_title, 0, wxALL | wxEXPAND, 0);

    m_staticText_task_value = new wxStaticText(sbSizer_task->GetStaticBox(), wxID_ANY, wxT("BBL_Task"), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE | wxSIMPLE_BORDER);
    m_staticText_task_value->Wrap(-1);
    fgSizer_task_info->Add(m_staticText_task_value, 1, wxALL | wxEXPAND, 0);

    m_staticText_subtask_title = new wxStaticText(sbSizer_task->GetStaticBox(), wxID_ANY, wxT("SubTask"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT | wxSIMPLE_BORDER);
    m_staticText_subtask_title->Wrap(-1);
    m_staticText_subtask_title->SetMinSize(wxSize(100, -1));
    m_staticText_subtask_title->SetMaxSize(wxSize(100, -1));

    fgSizer_task_info->Add(m_staticText_subtask_title, 0, wxALL | wxEXPAND, 0);

    m_staticText_subtask_value = new wxStaticText(sbSizer_task->GetStaticBox(), wxID_ANY, wxT("BBL_SubTask"), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE | wxSIMPLE_BORDER);
    m_staticText_subtask_value->Wrap(-1);
    fgSizer_task_info->Add(m_staticText_subtask_value, 0, wxALL | wxEXPAND, 0);

    m_staticText_progress_title = new wxStaticText(sbSizer_task->GetStaticBox(), wxID_ANY, wxT("Progress"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT | wxSIMPLE_BORDER);
    m_staticText_progress_title->Wrap(-1);
    m_staticText_progress_title->SetMinSize(wxSize(100, -1));
    m_staticText_progress_title->SetMaxSize(wxSize(100, -1));

    fgSizer_task_info->Add(m_staticText_progress_title, 0, wxALL | wxEXPAND, 0);

    wxBoxSizer* bSizer_progress;
    bSizer_progress = new wxBoxSizer(wxVERTICAL);

    m_staticText_progress = new wxStaticText(sbSizer_task->GetStaticBox(), wxID_ANY, wxT("0/0"), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE);
    m_staticText_progress->Wrap(-1);
    bSizer_progress->Add(m_staticText_progress, 0, wxALL | wxEXPAND, 0);

    m_gauge_progress = new wxGauge(sbSizer_task->GetStaticBox(), wxID_ANY, 100, wxDefaultPosition, wxDefaultSize, wxGA_HORIZONTAL);
    m_gauge_progress->SetValue(0);
    bSizer_progress->Add(m_gauge_progress, 0, wxALL | wxEXPAND, 0);


    fgSizer_task_info->Add(bSizer_progress, 1, wxALL | wxEXPAND, 0);


    sbSizer_task->Add(fgSizer_task_info, 0, wxALL | wxEXPAND, 0);

    m_dataViewCtrl_subtask = new wxDataViewCtrl(sbSizer_task->GetStaticBox(), wxID_ANY, wxDefaultPosition, wxDefaultSize, 0 | wxSIMPLE_BORDER);
    m_dataViewCtrl_subtask->SetMinSize(wxSize(-1, 60));

    sbSizer_task->Add(m_dataViewCtrl_subtask, 1, wxALL | wxEXPAND, 0);

    wxBoxSizer* bSizer_task_btn;
    bSizer_task_btn = new wxBoxSizer(wxHORIZONTAL);

    m_button_start = new wxButton(sbSizer_task->GetStaticBox(), wxID_ANY, wxT("Start"), wxDefaultPosition, wxDefaultSize, 0);
    bSizer_task_btn->Add(m_button_start, 0, wxALIGN_CENTER | wxALL, 5);

    m_button_pause_resume = new wxButton(sbSizer_task->GetStaticBox(), wxID_ANY, wxT("Pause"), wxDefaultPosition, wxDefaultSize, 0);
    bSizer_task_btn->Add(m_button_pause_resume, 0, wxALIGN_CENTER | wxALL, 5);

    m_button_abort = new wxButton(sbSizer_task->GetStaticBox(), wxID_ANY, wxT("Abort"), wxDefaultPosition, wxDefaultSize, 0);
    bSizer_task_btn->Add(m_button_abort, 0, wxALIGN_CENTER | wxALL, 5);


    sbSizer_task->Add(bSizer_task_btn, 0, wxEXPAND, 5);


    bSizer_left->Add(sbSizer_task, 1, wxEXPAND, 0);

    wxBoxSizer* bSizer_task;
    bSizer_task = new wxBoxSizer(wxVERTICAL);


    bSizer_left->Add(bSizer_task, 0, wxEXPAND, 0);

    m_staticline2 = new wxStaticLine(m_panel_left, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL);
    bSizer_left->Add(m_staticline2, 0, wxEXPAND | wxALL, 5);

    wxStaticBoxSizer* sbSizer_hms;
    sbSizer_hms = new wxStaticBoxSizer(new wxStaticBox(m_panel_left, wxID_ANY, wxT("Notifacation")), wxVERTICAL);

    m_dataViewListCtrl_hms = new wxDataViewListCtrl(sbSizer_hms->GetStaticBox(), wxID_ANY, wxDefaultPosition, wxDefaultSize, 0);
    m_dataViewListCtrl_hms->SetMinSize(wxSize(-1, 60));

    sbSizer_hms->Add(m_dataViewListCtrl_hms, 1, wxALL | wxEXPAND, 0);


    bSizer_left->Add(sbSizer_hms, 1, wxALL | wxEXPAND, 0);


    m_panel_left->SetSizer(bSizer_left);
    m_panel_left->Layout();
    bSizer_left->Fit(m_panel_left);
    bSizer_top->Add(m_panel_left, 0, wxEXPAND | wxALL, 0);

    wxBoxSizer* bSizer_right;
    bSizer_right = new wxBoxSizer(wxVERTICAL);

    wxStaticBoxSizer* sbSizer_info;
    sbSizer_info = new wxStaticBoxSizer(new wxStaticBox(this, wxID_ANY, wxT("info")), wxVERTICAL);

    m_dataViewListCtrl_info = new wxDataViewListCtrl(sbSizer_info->GetStaticBox(), wxID_ANY, wxDefaultPosition, wxDefaultSize, 0);
    m_dataViewListColumn_title = m_dataViewListCtrl_info->AppendTextColumn(wxT("Title"));
    m_dataViewListColumn_value = m_dataViewListCtrl_info->AppendTextColumn(wxT("Value"));
    sbSizer_info->Add(m_dataViewListCtrl_info, 1, wxALL | wxEXPAND, 5);


    bSizer_right->Add(sbSizer_info, 1, wxALL | wxEXPAND, 0);

    wxBoxSizer* bSizer_control;
    bSizer_control = new wxBoxSizer(wxHORIZONTAL);

    wxStaticBoxSizer* sbSizer_axis_ctrl;
    sbSizer_axis_ctrl = new wxStaticBoxSizer(new wxStaticBox(this, wxID_ANY, wxT("axis control")), wxVERTICAL);

    wxGridBagSizer* gbSizer_control;
    gbSizer_control = new wxGridBagSizer(0, 0);
    gbSizer_control->SetFlexibleDirection(wxBOTH);
    gbSizer_control->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);

    m_bpButton_y_up = new wxBitmapButton(sbSizer_axis_ctrl->GetStaticBox(), wxID_ANY, m_ctrl_up, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW);
    m_bpButton_y_up->SetMinSize(wxSize(32, 32));

    gbSizer_control->Add(m_bpButton_y_up, wxGBPosition(1, 1), wxGBSpan(1, 1), wxALL, 0);

    m_bpButton_y_down = new wxBitmapButton(sbSizer_axis_ctrl->GetStaticBox(), wxID_ANY, m_ctrl_down, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW);
    m_bpButton_y_down->SetMinSize(wxSize(32, 32));

    gbSizer_control->Add(m_bpButton_y_down, wxGBPosition(3, 1), wxGBSpan(1, 1), wxALL, 0);

    m_bpButton_xy_home = new wxBitmapButton(sbSizer_axis_ctrl->GetStaticBox(), wxID_ANY, m_ctrl_home, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW);
    m_bpButton_xy_home->SetMinSize(wxSize(32, 32));

    gbSizer_control->Add(m_bpButton_xy_home, wxGBPosition(2, 1), wxGBSpan(1, 1), wxALL, 0);

    m_bpButton_x_left = new wxBitmapButton(sbSizer_axis_ctrl->GetStaticBox(), wxID_ANY, m_ctrl_left, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW);
    m_bpButton_x_left->SetMinSize(wxSize(32, 32));

    gbSizer_control->Add(m_bpButton_x_left, wxGBPosition(2, 0), wxGBSpan(1, 1), wxALL, 0);

    m_bpButton_x_right = new wxBitmapButton(sbSizer_axis_ctrl->GetStaticBox(), wxID_ANY, m_ctrl_right, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW);
    m_bpButton_x_right->SetMinSize(wxSize(32, 32));

    gbSizer_control->Add(m_bpButton_x_right, wxGBPosition(2, 2), wxGBSpan(1, 1), wxALL, 0);

    m_bpButton_z_up = new wxBitmapButton(sbSizer_axis_ctrl->GetStaticBox(), wxID_ANY, m_ctrl_up, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW);
    m_bpButton_z_up->SetMinSize(wxSize(32, 32));

    gbSizer_control->Add(m_bpButton_z_up, wxGBPosition(1, 6), wxGBSpan(1, 1), wxALL, 0);

    m_bpButton_z_home = new wxBitmapButton(sbSizer_axis_ctrl->GetStaticBox(), wxID_ANY, m_ctrl_home, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW);
    m_bpButton_z_home->SetMinSize(wxSize(32, 32));

    gbSizer_control->Add(m_bpButton_z_home, wxGBPosition(2, 6), wxGBSpan(1, 1), wxALL, 0);

    m_bpButton_z_down = new wxBitmapButton(sbSizer_axis_ctrl->GetStaticBox(), wxID_ANY, m_ctrl_down, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW);
    m_bpButton_z_down->SetMinSize(wxSize(32, 32));

    gbSizer_control->Add(m_bpButton_z_down, wxGBPosition(3, 6), wxGBSpan(1, 1), wxALL, 0);

    m_staticText_X = new wxStaticText(sbSizer_axis_ctrl->GetStaticBox(), wxID_ANY, wxT("X          "), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_X->Wrap(-1);
    gbSizer_control->Add(m_staticText_X, wxGBPosition(2, 4), wxGBSpan(1, 1), wxALIGN_CENTER | wxALL, 0);

    m_staticText_Y = new wxStaticText(sbSizer_axis_ctrl->GetStaticBox(), wxID_ANY, wxT("Y"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_Y->Wrap(-1);
    gbSizer_control->Add(m_staticText_Y, wxGBPosition(0, 1), wxGBSpan(1, 1), wxALIGN_CENTER | wxALL, 5);

    m_staticText_Z = new wxStaticText(sbSizer_axis_ctrl->GetStaticBox(), wxID_ANY, wxT("Z"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_Z->Wrap(-1);
    gbSizer_control->Add(m_staticText_Z, wxGBPosition(0, 6), wxGBSpan(1, 1), wxALIGN_CENTER | wxALL, 5);


    sbSizer_axis_ctrl->Add(gbSizer_control, 0, wxEXPAND, 5);

    wxBoxSizer* bSizer_unit;
    bSizer_unit = new wxBoxSizer(wxHORIZONTAL);

    m_button_0_1 = new wxToggleButton(sbSizer_axis_ctrl->GetStaticBox(), wxID_ANY, wxT("0.1"), wxDefaultPosition, wxSize(50, -1), 0);
    bSizer_unit->Add(m_button_0_1, 1, wxALIGN_CENTER | wxALL, 0);

    m_button_1_0 = new wxToggleButton(sbSizer_axis_ctrl->GetStaticBox(), wxID_ANY, wxT("1"), wxDefaultPosition, wxSize(50, -1), 0);
    bSizer_unit->Add(m_button_1_0, 1, wxALL, 0);

    m_button_10_0 = new wxToggleButton(sbSizer_axis_ctrl->GetStaticBox(), wxID_ANY, wxT("10"), wxDefaultPosition, wxSize(50, -1), 0);
    bSizer_unit->Add(m_button_10_0, 1, wxALL, 0);

    m_button_100_0 = new wxToggleButton(sbSizer_axis_ctrl->GetStaticBox(), wxID_ANY, wxT("100"), wxDefaultPosition, wxSize(50, -1), 0);
    bSizer_unit->Add(m_button_100_0, 1, wxALL, 0);


    sbSizer_axis_ctrl->Add(bSizer_unit, 0, wxALL | wxEXPAND, 1);


    bSizer_control->Add(sbSizer_axis_ctrl, 0, wxEXPAND, 5);

    wxStaticBoxSizer* sbSizer_extruder;
    sbSizer_extruder = new wxStaticBoxSizer(new wxStaticBox(this, wxID_ANY, wxT("extruder control")), wxVERTICAL);

    wxBoxSizer* bSizer_select_tray;
    bSizer_select_tray = new wxBoxSizer(wxHORIZONTAL);

    m_comboBox_trays = new wxComboBox(sbSizer_extruder->GetStaticBox(), wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, NULL, 0);
    bSizer_select_tray->Add(m_comboBox_trays, 1, wxALIGN_CENTER | wxALL, 5);

    m_button_extreder_feed = new wxButton(sbSizer_extruder->GetStaticBox(), wxID_ANY, wxT("Feed"), wxDefaultPosition, wxDefaultSize, 0);
    m_button_extreder_feed->SetMinSize(wxSize(60, 30));

    bSizer_select_tray->Add(m_button_extreder_feed, 0, wxALIGN_CENTER | wxALL, 5);


    sbSizer_extruder->Add(bSizer_select_tray, 0, wxEXPAND, 5);

    wxBoxSizer* bSizer_extruder_back;
    bSizer_extruder_back = new wxBoxSizer(wxHORIZONTAL);

    m_button_extruder_back = new wxButton(sbSizer_extruder->GetStaticBox(), wxID_ANY, wxT("Back"), wxDefaultPosition, wxDefaultSize, 0);
    m_button_extruder_back->SetMinSize(wxSize(-1, 30));
    m_button_extruder_back->SetMaxSize(wxSize(-1, 30));

    bSizer_extruder_back->Add(m_button_extruder_back, 1, wxALL | wxEXPAND, 5);


    sbSizer_extruder->Add(bSizer_extruder_back, 0, wxEXPAND, 5);

    wxBoxSizer* bSizer_extrude;
    bSizer_extrude = new wxBoxSizer(wxHORIZONTAL);

    m_textCtrl_extrude = new wxTextCtrl(sbSizer_extruder->GetStaticBox(), wxID_ANY, wxT("20"), wxDefaultPosition, wxDefaultSize, 0);
    m_textCtrl_extrude->SetMinSize(wxSize(80, -1));

    bSizer_extrude->Add(m_textCtrl_extrude, 1, wxALIGN_CENTER | wxALL, 5);

    m_staticText_unit_extrude = new wxStaticText(sbSizer_extruder->GetStaticBox(), wxID_ANY, wxT("mm"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_unit_extrude->Wrap(-1);
    bSizer_extrude->Add(m_staticText_unit_extrude, 0, wxALIGN_CENTER | wxALL, 5);

    m_button_extruder_in = new wxButton(sbSizer_extruder->GetStaticBox(), wxID_ANY, wxT("Extrude"), wxDefaultPosition, wxSize(110, -1), 0);
    m_button_extruder_in->SetMinSize(wxSize(80, -1));

    bSizer_extrude->Add(m_button_extruder_in, 0, wxALIGN_CENTER | wxALL, 5);


    sbSizer_extruder->Add(bSizer_extrude, 0, wxEXPAND, 5);

    wxBoxSizer* bSizer_rectraction;
    bSizer_rectraction = new wxBoxSizer(wxHORIZONTAL);

    m_textCtrl_retraction = new wxTextCtrl(sbSizer_extruder->GetStaticBox(), wxID_ANY, wxT("20"), wxDefaultPosition, wxDefaultSize, 0);
    m_textCtrl_retraction->SetMinSize(wxSize(80, -1));

    bSizer_rectraction->Add(m_textCtrl_retraction, 1, wxALIGN_CENTER | wxALL, 5);

    m_staticText_unit_retraction = new wxStaticText(sbSizer_extruder->GetStaticBox(), wxID_ANY, wxT("mm"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_unit_retraction->Wrap(-1);
    bSizer_rectraction->Add(m_staticText_unit_retraction, 0, wxALIGN_CENTER | wxALL, 5);

    m_button_extruder_out = new wxButton(sbSizer_extruder->GetStaticBox(), wxID_ANY, wxT("Retraction"), wxDefaultPosition, wxSize(110, -1), 0);
    m_button_extruder_out->SetMinSize(wxSize(80, -1));

    bSizer_rectraction->Add(m_button_extruder_out, 0, wxALIGN_CENTER | wxALL, 5);


    sbSizer_extruder->Add(bSizer_rectraction, 0, wxALIGN_BOTTOM | wxEXPAND, 5);


    sbSizer_extruder->Add(0, 0, 1, wxALL | wxEXPAND, 5);


    bSizer_control->Add(sbSizer_extruder, 0, wxEXPAND, 5);

    wxStaticBoxSizer* sbSizer_others;
    sbSizer_others = new wxStaticBoxSizer(new wxStaticBox(this, wxID_ANY, wxT("others")), wxVERTICAL);

    m_button_fan_on = new wxButton(sbSizer_others->GetStaticBox(), wxID_ANY, wxT("Fan On"), wxDefaultPosition, wxSize(110, -1), 0);
    sbSizer_others->Add(m_button_fan_on, 0, wxALL, 5);

    m_button_fan_off = new wxButton(sbSizer_others->GetStaticBox(), wxID_ANY, wxT("Fan Off"), wxDefaultPosition, wxSize(110, -1), 0);
    sbSizer_others->Add(m_button_fan_off, 0, wxALL, 5);

    m_button_auto_leveling = new wxButton(sbSizer_others->GetStaticBox(), wxID_ANY, wxT("Auto Leveling"), wxDefaultPosition, wxSize(110, -1), 0);
    sbSizer_others->Add(m_button_auto_leveling, 0, wxALL, 5);

    m_button_xyz_abs = new wxButton(sbSizer_others->GetStaticBox(), wxID_ANY, wxT("XYZ abs"), wxDefaultPosition, wxSize(110, -1), 0);
    sbSizer_others->Add(m_button_xyz_abs, 0, wxALL, 5);


    bSizer_control->Add(sbSizer_others, 0, wxEXPAND, 5);


    bSizer_control->Add(0, 0, 1, wxEXPAND, 5);


    bSizer_right->Add(bSizer_control, 0, wxEXPAND, 5);


    bSizer_top->Add(bSizer_right, 1, wxALL | wxEXPAND, 5);


    this->SetSizer(bSizer_top);
    this->Layout();

    set_toggle_widget_on(m_button_0_1);

    init_model();
    init_timer();
    init_bind();

    obj = nullptr;
    is_pausing = false;
    m_ctrl_unit = 0.1f;

    // Connect Events

    m_choice_machine->Connect(wxEVT_COMMAND_CHOICE_SELECTED, wxCommandEventHandler(MonitorPanel::on_select), NULL, this);

    m_button_start->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_subtask_start), NULL, this);
    m_button_pause_resume->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_subtask_pause_resume), NULL, this);
    m_button_abort->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_subtask_abort), NULL, this);
    m_bpButton_y_up->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_axis_ctrl_y_up), NULL, this);
    m_bpButton_y_down->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_axis_ctrl_y_down), NULL, this);
    m_bpButton_xy_home->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_axis_ctrl_xy_home), NULL, this);
    m_bpButton_x_left->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_axis_ctrl_x_left), NULL, this);
    m_bpButton_x_right->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_axis_ctrl_x_right), NULL, this);
    m_bpButton_z_up->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_axis_ctrl_z_up), NULL, this);
    m_bpButton_z_home->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_axis_ctrl_z_home), NULL, this);
    m_bpButton_z_down->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_axis_ctrl_z_down), NULL, this);
    m_button_0_1->Connect(wxEVT_TOGGLEBUTTON, wxCommandEventHandler(MonitorPanel::on_axis_ctrl_unit_0_1), NULL, this);
    m_button_1_0->Connect(wxEVT_TOGGLEBUTTON, wxCommandEventHandler(MonitorPanel::on_axis_ctrl_unit_1_0), NULL, this);
    m_button_10_0->Connect(wxEVT_TOGGLEBUTTON, wxCommandEventHandler(MonitorPanel::on_axis_ctrl_unit_10_0), NULL, this);
    m_button_100_0->Connect(wxEVT_TOGGLEBUTTON, wxCommandEventHandler(MonitorPanel::on_axis_ctrl_unit_100_0), NULL, this);
    m_button_extreder_feed->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_extruder_feed), NULL, this);
    m_button_extruder_back->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_extruder_back), NULL, this);
    m_button_extruder_in->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_extruder_extrude), NULL, this);
    m_button_extruder_out->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_extruder_retraction), NULL, this);
    m_button_fan_on->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_fan_on), NULL, this);
    m_button_fan_off->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_fan_off), NULL, this);
    m_button_auto_leveling->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_auto_leveling), NULL, this);
    m_button_xyz_abs->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_xyz_abs), NULL, this);

    Bind(wxEVT_SIZE, &MonitorPanel::on_size, this);
}

void MonitorPanel::on_size(wxSizeEvent& event)
{
    Layout();
    Refresh();
}

MonitorPanel::~MonitorPanel()
{
    // Disconnect Events
    m_choice_machine->Disconnect(wxEVT_COMMAND_CHOICE_SELECTED, wxCommandEventHandler(MonitorPanel::on_select), NULL, this);

    m_button_start->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_subtask_start), NULL, this);
    m_button_pause_resume->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_subtask_pause_resume), NULL, this);
    m_button_abort->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_subtask_abort), NULL, this);
    m_bpButton_y_up->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_axis_ctrl_y_up), NULL, this);
    m_bpButton_y_down->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_axis_ctrl_y_down), NULL, this);
    m_bpButton_xy_home->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_axis_ctrl_xy_home), NULL, this);
    m_bpButton_x_left->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_axis_ctrl_x_left), NULL, this);
    m_bpButton_x_right->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_axis_ctrl_x_right), NULL, this);
    m_bpButton_z_up->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_axis_ctrl_z_up), NULL, this);
    m_bpButton_z_home->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_axis_ctrl_z_home), NULL, this);
    m_bpButton_z_down->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_axis_ctrl_z_down), NULL, this);
    m_button_0_1->Disconnect(wxEVT_TOGGLEBUTTON, wxCommandEventHandler(MonitorPanel::on_axis_ctrl_unit_0_1), NULL, this);
    m_button_1_0->Disconnect(wxEVT_TOGGLEBUTTON, wxCommandEventHandler(MonitorPanel::on_axis_ctrl_unit_1_0), NULL, this);
    m_button_10_0->Disconnect(wxEVT_TOGGLEBUTTON, wxCommandEventHandler(MonitorPanel::on_axis_ctrl_unit_10_0), NULL, this);
    m_button_100_0->Disconnect(wxEVT_TOGGLEBUTTON, wxCommandEventHandler(MonitorPanel::on_axis_ctrl_unit_100_0), NULL, this);
    m_button_extreder_feed->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_extruder_feed), NULL, this);
    m_button_extruder_back->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_extruder_back), NULL, this);
    m_button_extruder_in->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_extruder_extrude), NULL, this);
    m_button_extruder_out->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_extruder_retraction), NULL, this);
    m_button_fan_on->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_fan_on), NULL, this);
    m_button_fan_off->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_fan_off), NULL, this);
    m_button_auto_leveling->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_auto_leveling), NULL, this);
    m_button_xyz_abs->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_xyz_abs), NULL, this);
}

void MonitorPanel::init_bitmap()
{
    m_ctrl_up = create_scaled_bitmap("axis_ctrl_up", nullptr, 32);
    m_ctrl_down = create_scaled_bitmap("axis_ctrl_down", nullptr, 32);
    m_ctrl_left = create_scaled_bitmap("axis_ctrl_left", nullptr, 32);
    m_ctrl_right = create_scaled_bitmap("axis_ctrl_right", nullptr, 32);
    m_ctrl_home = create_scaled_bitmap("axis_ctrl_home", nullptr, 32);
}

void MonitorPanel::init_model()
{
    subtask_model = new SubTaskListModel();
    m_dataViewCtrl_subtask->AssociateModel(subtask_model.get());
    m_dataViewCtrl_subtask->AppendTextColumn("SubTask",
        SubTaskListModel::Col_SubTaskName,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_SORTABLE);

    m_dataViewCtrl_subtask->AppendTextColumn("Duration",
        SubTaskListModel::Col_SubTaskDuration,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_SORTABLE);

    m_dataViewCtrl_subtask->AppendTextColumn("Weight",
        SubTaskListModel::Col_SubTaskWeight,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_SORTABLE);

    tray_model = new TrayListModel();
    m_dataViewCtrl_ams->AssociateModel(tray_model.get());
    m_dataViewCtrl_ams->AppendTextColumn("Name",
        TrayListModel::Col_TrayTitle,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_SORTABLE);
    m_dataViewCtrl_ams->AppendTextColumn("Color",
        TrayListModel::Col_TrayColor,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_SORTABLE);
    m_dataViewCtrl_ams->AppendTextColumn("Meterial",
        TrayListModel::Col_TrayMeterial,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_SORTABLE);
    m_dataViewCtrl_ams->AppendTextColumn("SN",
        TrayListModel::Col_TraySN,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_SORTABLE);
    m_dataViewCtrl_ams->AppendTextColumn("Weight",
        TrayListModel::Col_TrayWeight,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_SORTABLE);
    m_dataViewCtrl_ams->AppendTextColumn("Diameter",
        TrayListModel::Col_TrayDiameter,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_SORTABLE);
    m_dataViewCtrl_ams->AppendTextColumn("Time",
        TrayListModel::Col_TrayTime,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_SORTABLE);
    m_dataViewCtrl_ams->AppendTextColumn("Smooth",
        TrayListModel::Col_TraySmooth,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_SORTABLE);
}

void MonitorPanel::init_timer()
{
    m_refresh_timer = new wxTimer();
    m_refresh_timer->SetOwner(this);
    m_refresh_timer->Start(REFRESH_INTERVAL);
    wxPostEvent(this, wxTimerEvent());
}


void MonitorPanel::init_bind()
{
    Bind(wxEVT_TIMER, &MonitorPanel::on_timer, this);

    m_staticText_subtask_value->Bind(wxEVT_ENTER_WINDOW,
        [this](wxMouseEvent& evt) {
            m_plate_thumbnail = new ImageTransientPopup( this, false, create_scaled_bitmap("revert_all_", nullptr, 80));
            wxWindow *ctrl = (wxWindow*) evt.GetEventObject();
            wxPoint pos = ctrl->ClientToScreen( wxPoint(0,0) );
            wxSize sz = ctrl->GetSize();
            m_plate_thumbnail->Position( pos, sz );
            m_plate_thumbnail->Popup();
        }
        );
    m_staticText_subtask_value->Bind(wxEVT_LEAVE_WINDOW,
        [this](wxMouseEvent& evt) {
            if (m_plate_thumbnail) {
                m_plate_thumbnail->Hide();
            }
        }
        );
}

void MonitorPanel::on_machine_list_changed()
{
    Slic3r::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();

    /* update list */
    int select = -1;
    std::string last_my_bind_dev_id;
    if (last_wlan_device_selection < mybind_machine_list_items.size()) {
        last_my_bind_dev_id = mybind_machine_list_items[last_wlan_device_selection];
    }

    std::map<std::string, MachineObject*> list = account_manager->myBindMachineList;
    std::map<std::string, MachineObject*>::iterator iter;
    mybind_machine_list_items.clear();
    wxArrayString new_items;
    for (iter = list.begin(); iter != list.end(); iter++) {
        if (iter->second->is_online) {
            wxString text = wxString::Format("%s", iter->second->dev_name);
            if (!last_my_bind_dev_id.empty() && iter->second->dev_id.compare(last_my_bind_dev_id) == 0) {
                select = new_items.size();
            }
            mybind_machine_list_items.push_back(iter->second->dev_id);
            new_items.Add(text);
        }
    }

    m_choice_machine->Set(new_items);
    if (select >= 0) {
        m_choice_machine->Select(select);
        last_wlan_device_selection = select;
    }
}

bool MonitorPanel::Show(bool show)
{
    Slic3r::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();
    obj = account_manager->get_default_machine();

    if (obj && show) {
        update_ams(obj);
    }

    return wxPanel::Show(show);
}

void MonitorPanel::update_ams(MachineObject* obj)
{
    if (!obj) return;
    tray_model->update(obj);
}

void MonitorPanel::update_task(MachineObject* obj)
{
    if (!obj) return;

    BBLSubTask* curr_subtask = obj->get_subtask();
    if (last_subtask != curr_subtask) {
        on_subtask_update(curr_subtask, true);
    }
    else {
        on_subtask_update(curr_subtask, false);
    }
    last_subtask = curr_subtask;
}

void MonitorPanel::update_status(MachineObject* obj)
{
    if (!obj) return;

    /* Update Device Info */
    m_staticText_machine_name->SetLabelText(wxString::Format("%s", obj->dev_name));
    m_staticText_sn_value->SetLabelText(wxString::Format("%s", obj->dev_id));

    // update wifi signal
    m_staticText_wifi_signal->SetLabelText(wxString::Format("%s", obj->wifi_signal));
    // update temprature
    wxString nozzle_temp_text = wxString::Format("%0.2f/%0.2f", obj->nozzle_temp, obj->nozzle_temp_target);
    m_staticText_nozzle_value->SetLabelText(nozzle_temp_text);
    wxString bed_temp_text = wxString::Format("%0.2f/%0.2f", obj->bed_temp, obj->bed_temp_target);
    m_staticText_bed_value->SetLabelText(bed_temp_text);
}

void MonitorPanel::on_timer(wxTimerEvent& event)
{
    on_machine_list_changed();

    update_all();

    Layout();
    Refresh();
}

void MonitorPanel::update_all()
{
    Slic3r::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();
    obj = account_manager->get_default_machine();
    if (!obj) return;

    update_status(obj);

    // always update ams fields
    //if (obj->is_ams_need_update) {}
    update_ams(obj);

    update_task(obj);
}

void MonitorPanel::on_select(wxCommandEvent& event)
{
    Slic3r::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();
    //machine_list_items
    int selection = event.GetSelection();
    if (selection < mybind_machine_list_items.size()) {
        account_manager->default_machine = mybind_machine_list_items[selection];
        m_choice_machine->SetSelection(selection);

        if (last_wlan_device_selection != selection) {
            subtask_model->clear_data();
            tray_model->clear_data();

            update_all();

            Layout();
            Refresh();
        }
        /* update widget values */
        last_wlan_device_selection = selection;
    }
}

void MonitorPanel::select_machine(std::string machine_sn)
{
    Slic3r::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();
    machine_sn = account_manager->get_default_machine()->dev_id;
    for (int i = 0; i < mybind_machine_list_items.size(); i++) {
        if (mybind_machine_list_items[i].compare(machine_sn) == 0) {
            wxCommandEvent* event = new wxCommandEvent(wxEVT_CHOICE);
            event->SetInt(i);
            wxQueueEvent(this, event);
            return;
        }
    }
}

void MonitorPanel::on_subtask_update(BBLSubTask* curr_subtask, bool update_all)
{
    if (!curr_subtask) return;
    // update task info

    if (update_all) {
        if (curr_subtask->parent_task_) {
            BBLTask* task = curr_subtask->parent_task_;
            if (task) {
                if (task->profile_ && task->profile_->project_) {
                    // update project name
                    wxString project_name_text = wxString::Format("%s", task->profile_->project_->project_name);
                    m_staticText_project_name->SetLabelText(project_name_text);

                    // update profile name
                    m_staticText_profile_value->SetLabelText(wxString::Format("%s", task->profile_->profile_name));
                }

                // update task name
                m_staticText_task_value->SetLabelText(wxString::Format("%s", task->task_name));

                // update task model
                subtask_model->update_task(task);
            }
        }
        else {
            // update task model
            subtask_model->update_subtask(curr_subtask);
        }
        // update subtask name
        m_staticText_subtask_value->SetLabelText(wxString::Format("%s", curr_subtask->task_name));
    }
    

    // update gcode progress
    std::string duration = "NA";
    try {
        if (!curr_subtask->task_duration.empty())
            duration = get_time_dhms(stoi(curr_subtask->task_duration));
    }
    catch (...) {
        ;
    }

    std::string total_time = "NA";
    if (!curr_subtask->task_prediction.empty()) {
        try {
            total_time = get_time_dhms(stoi(curr_subtask->task_prediction));
        }
        catch (...) {
            ;
        }
    }
    wxString progress_text = wxString::Format("%s/%s", duration, total_time);

    m_staticText_progress->SetLabelText(progress_text);

    // update current subtask progress
    m_gauge_progress->SetValue(curr_subtask->task_progress);

    // update printing status
    wxString printing_status_text = wxString::Format("%s", curr_subtask->printing_status);
    m_staticText_printing_val->SetLabelText(printing_status_text);
}

void MonitorPanel::on_subtask_status_changed(std::string old_status, std::string new_status)
{
    ;//TODO
}

void MonitorPanel::on_subtask_start(wxCommandEvent& event)
{
    ;//TODO
}

void MonitorPanel::on_subtask_pause_resume(wxCommandEvent& event)
{
    if (obj) {
        if (is_pausing) {
            obj->command_task_pause();
        }
        else {
            obj->command_task_resume();
        }
        is_pausing = !is_pausing;
    }
}

void MonitorPanel::on_subtask_abort(wxCommandEvent& event)
{
    if (obj)
        obj->command_task_abort();
}

void MonitorPanel::on_axis_ctrl_y_up(wxCommandEvent& event)
{
    if (obj)
        obj->command_axis_control("Y", get_control_unit(), 1.0f);
}

void MonitorPanel::on_axis_ctrl_y_down(wxCommandEvent& event)
{
    if (obj)
        obj->command_axis_control("Y", get_control_unit(), -1.0f);
}

void MonitorPanel::on_axis_ctrl_xy_home(wxCommandEvent& event)
{
    if (obj)
        obj->command_go_home();
}

void MonitorPanel::on_axis_ctrl_x_left(wxCommandEvent& event)
{
    if (obj)
        obj->command_axis_control("X", get_control_unit(), -1.0f);
}

void MonitorPanel::on_axis_ctrl_x_right(wxCommandEvent& event)
{
    if (obj)
        obj->command_axis_control("X", get_control_unit(), 1.0f);
}

void MonitorPanel::on_axis_ctrl_z_up(wxCommandEvent& event)
{
    if (obj)
        obj->command_axis_control("Z", get_control_unit(), -1.0f);
}

void MonitorPanel::on_axis_ctrl_z_home(wxCommandEvent& event)
{
    if (obj)
        obj->command_go_home();
}

void MonitorPanel::on_axis_ctrl_z_down(wxCommandEvent& event)
{
    if (obj)
        obj->command_axis_control("Z", get_control_unit(), 1.0f);
}

void MonitorPanel::on_axis_ctrl_unit_0_1(wxCommandEvent& event)
{
    if (event.IsChecked()) {
        set_toggle_widget_on(m_button_0_1);
        m_ctrl_unit = 0.1;
    }
    else {
        event.Skip();
    }
}

void MonitorPanel::on_axis_ctrl_unit_1_0(wxCommandEvent& event)
{
    if (event.IsChecked()) {
        set_toggle_widget_on(m_button_1_0);
        m_ctrl_unit = 1.0;
    }
    else {
        event.Skip();
    }
}

void MonitorPanel::on_axis_ctrl_unit_10_0(wxCommandEvent& event)
{
    if (event.IsChecked()) {
        set_toggle_widget_on(m_button_10_0);
        m_ctrl_unit = 10.0;
    }
    else {
        event.Skip();
    }
}

void MonitorPanel::on_axis_ctrl_unit_100_0(wxCommandEvent& event)
{
    if (event.IsChecked()) {
        set_toggle_widget_on(m_button_100_0);
        m_ctrl_unit = 100.0;
    }
    else {
        event.Skip();
    }
}

double MonitorPanel::get_control_unit()
{
    return m_ctrl_unit;
}

void MonitorPanel::set_toggle_widget_on(wxToggleButton* btn)
{
    bool values[4] = { false, false, false, false };
    if (btn == m_button_0_1) {
        values[0] = true;
    }
    else if (btn == m_button_1_0) {
        values[1] = true;
    }
    else if (btn == m_button_10_0) {
        values[2] = true;
    }
    else if (btn == m_button_100_0) {
        values[3] = true;
    }
    m_button_0_1->SetValue(values[0]);
    m_button_1_0->SetValue(values[1]);
    m_button_10_0->SetValue(values[2]);
    m_button_100_0->SetValue(values[3]);
}

void MonitorPanel::on_extruder_feed(wxCommandEvent& event)
{
    ;
}

void MonitorPanel::on_extruder_back(wxCommandEvent& event)
{
    ;
}

void MonitorPanel::on_extruder_extrude(wxCommandEvent& event)
{
    double value;
    if (m_textCtrl_extrude->GetLabelText().ToDouble(&value) && obj) {
        obj->command_axis_control("E", 1.0f, value, 300.0f);
    }
}

void MonitorPanel::on_extruder_retraction(wxCommandEvent& event)
{
    double value;  
    if (m_textCtrl_extrude->GetLabelText().ToDouble(&value) && obj) {
        obj->command_axis_control("E", 1.0f, -1.0f * value, 300.0f);
    }
}

void MonitorPanel::on_fan_on(wxCommandEvent& event)
{
    if (obj)
        obj->publish_gcode("M106 S255 \n");
}

void MonitorPanel::on_fan_off(wxCommandEvent& event)
{
    if (obj)
        obj->command_fan_off();
}

void MonitorPanel::on_auto_leveling(wxCommandEvent& event)
{
    if (obj)
        obj->command_auto_leveling();
}

void MonitorPanel::on_xyz_abs(wxCommandEvent& event)
{
    if (obj)
        obj->command_xyz_abs();
}

void MonitorPanel::set_machine(std::string machine_sn)
{
    this->machine_sn = machine_sn;
}


} // GUI
} // Slic3r
