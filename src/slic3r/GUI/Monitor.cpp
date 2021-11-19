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

    std::map<std::string, Ams*>::iterator ams_it;
    std::map<std::string, AmsTray*>::iterator tray_it;
    int tray_index = 0;
    for (ams_it = obj->amsList.begin(); ams_it != obj->amsList.end(); ams_it++) {
        if (ams_it->second) {
            for (tray_it = ams_it->second->trayList.begin(); tray_it != ams_it->second->trayList.end(); tray_it++) {
                AmsTray* tray = tray_it->second;
                if (tray) {
                    tray_index++;
                    wxString title_text = wxString::Format("tray %s(ams %s)", tray->id, ams_it->second->id);
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
    case Col_Name:
        if (row >= m_nameColValues.GetCount())
            variant = wxString::Format("N/A", row);
        else
            variant = m_nameColValues[row];
        break;
    case Col_Duration:
        if (row >= m_durationColValues.GetCount())
            variant = wxString::Format("N/A", row);
        else
            variant = m_durationColValues[row];
        break;
    case Col_Weight:
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
    case Col_Name:
        return true;
    case Col_Duration:
        return true;
    case Col_Weight:
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
    m_PlateIdxColValues.clear();
    add_subtask(subtask);
    Reset(m_nameColValues.Count());
}

void SubTaskListModel::update_task(BBLTask* task)
{
    if (!task) return;

    m_nameColValues.clear();
    m_durationColValues.clear();
    m_WeightColValues.clear();
    m_PlateIdxColValues.clear();

    std::vector<BBLSubTask*>::iterator it;
    for (it = task->subtasks.begin(); it != task->subtasks.end(); it++) {
        add_subtask((*it));
    }

    Reset(m_nameColValues.GetCount());
}

void SubTaskListModel::update_profile(BBLProfile* profile)
{
    if (!profile) return;

    m_nameColValues.clear();
    m_durationColValues.clear();
    m_WeightColValues.clear();
    m_PlateIdxColValues.clear();

    std::map<std::string, BBLSliceInfo*>::iterator it;
    for (it = profile->slice_info.begin(); it != profile->slice_info.end(); it++) {
        add_slice_info(it->second);
    }

    Reset(m_nameColValues.GetCount());
}

void SubTaskListModel::add_slice_info(BBLSliceInfo* slice_info)
{
    if (!slice_info) return;

    wxString name_text = "N/A";
    name_text = wxString::Format("%s", slice_info->title);
    m_nameColValues.push_back(name_text);

    wxString duration_text = "N/A";
    if (slice_info->prediction >= 0) {
        std::string duration = get_time_dhms(slice_info->prediction);
        duration_text = wxString::Format("%s", duration);
    }

    m_durationColValues.push_back(duration_text);

    wxString weight_text = "N/A";
    if (!slice_info->weight.empty()) {
        weight_text = wxString::Format("%sg", slice_info->weight);
    }
    m_WeightColValues.push_back(weight_text);
}

void SubTaskListModel::add_item(std::string title, int prediction, std::string weight)
{
    wxString name_text = "N/A";
    name_text = wxString::Format("%s", title);
    m_nameColValues.push_back(name_text);

    wxString duration_text = "N/A";
    if (prediction >= 0) {
        std::string duration = get_time_dhms(prediction);
        duration_text = wxString::Format("%s", duration);
    }

    m_durationColValues.push_back(duration_text);

    wxString weight_text = "N/A";
    if (!weight.empty()) {
        weight_text = wxString::Format("%sg", weight);
    }
    m_WeightColValues.push_back(weight_text);
}


void SubTaskListModel::clear_data()
{
    clear();
    reset();
}

void SubTaskListModel::clear()
{
    m_nameColValues.clear();
    m_durationColValues.clear();
    m_WeightColValues.clear();
    m_PlateIdxColValues.clear();
}

void SubTaskListModel::reset()
{
    Reset(m_nameColValues.GetCount());
}


MonitorPanel::MonitorPanel(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style) : wxPanel(parent, id, pos, size, style)
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__

    init_bitmap();
    /* set bitmap image, set font */

    wxBoxSizer* bSizer_top;
	bSizer_top = new wxBoxSizer( wxHORIZONTAL );

	m_panel_left = new wxPanel( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	m_panel_left->SetMinSize( wxSize( 420,-1 ) );
	m_panel_left->SetMaxSize( wxSize( 420,-1 ) );

	wxBoxSizer* bSizer_left;
	bSizer_left = new wxBoxSizer( wxVERTICAL );

	wxBoxSizer* bSizer_status;
	bSizer_status = new wxBoxSizer( wxVERTICAL );

	wxBoxSizer* bSizer_status_caption;
	bSizer_status_caption = new wxBoxSizer( wxHORIZONTAL );

	m_staticText_status = new wxStaticText( m_panel_left, wxID_ANY, wxT("Status Info"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_status->Wrap( -1 );
	bSizer_status_caption->Add( m_staticText_status, 0, wxALL, 5 );


	bSizer_status->Add( bSizer_status_caption, 0, wxEXPAND, 5 );

	wxBoxSizer* bSizer_left_up;
	bSizer_left_up = new wxBoxSizer( wxVERTICAL );

	wxBoxSizer* bSizer_device;
	bSizer_device = new wxBoxSizer( wxHORIZONTAL );


	bSizer_left_up->Add( bSizer_device, 0, wxALL|wxEXPAND, 0 );

	wxFlexGridSizer* fgSizer_status;
	fgSizer_status = new wxFlexGridSizer( 1, 2, 0, 0 );
	fgSizer_status->AddGrowableCol( 1 );
	fgSizer_status->SetFlexibleDirection( wxHORIZONTAL );
	fgSizer_status->SetNonFlexibleGrowMode( wxFLEX_GROWMODE_SPECIFIED );

	m_staticText_machine_status = new wxStaticText( m_panel_left, wxID_ANY, wxT("Printer"), wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT );
	m_staticText_machine_status->Wrap( -1 );
	m_staticText_machine_status->SetMinSize( wxSize( 100,-1 ) );

	fgSizer_status->Add( m_staticText_machine_status, 0, wxALL, 5 );

	m_staticText_machine_name = new wxStaticText( m_panel_left, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT );
	m_staticText_machine_name->Wrap( -1 );
	fgSizer_status->Add( m_staticText_machine_name, 1, wxALL, 5 );

	m_staticText_printing_title = new wxStaticText( m_panel_left, wxID_ANY, wxT("Status"), wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT );
	m_staticText_printing_title->Wrap( -1 );
	m_staticText_printing_title->SetMinSize( wxSize( 100,-1 ) );

	fgSizer_status->Add( m_staticText_printing_title, 0, wxALL|wxEXPAND, 5 );

	m_staticText_printing_val = new wxStaticText( m_panel_left, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT );
	m_staticText_printing_val->Wrap( -1 );
	fgSizer_status->Add( m_staticText_printing_val, 1, wxALL|wxEXPAND, 5 );

	m_staticText_capacity_title = new wxStaticText( m_panel_left, wxID_ANY, wxT("Capacity"), wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT );
	m_staticText_capacity_title->Wrap( -1 );
	fgSizer_status->Add( m_staticText_capacity_title, 0, wxALL|wxEXPAND, 5 );

	m_staticText_capacity_val = new wxStaticText( m_panel_left, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT );
	m_staticText_capacity_val->Wrap( -1 );
	fgSizer_status->Add( m_staticText_capacity_val, 1, wxALL|wxEXPAND, 5 );

	m_staticText_sn_title = new wxStaticText( m_panel_left, wxID_ANY, wxT("SN"), wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT );
	m_staticText_sn_title->Wrap( -1 );
	m_staticText_sn_title->SetMinSize( wxSize( 80,-1 ) );

	fgSizer_status->Add( m_staticText_sn_title, 0, wxALL|wxEXPAND, 5 );

	m_staticText_sn_value = new wxStaticText( m_panel_left, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT );
	m_staticText_sn_value->Wrap( -1 );
	fgSizer_status->Add( m_staticText_sn_value, 1, wxALL|wxEXPAND, 5 );

	m_staticText_wifi_title = new wxStaticText( m_panel_left, wxID_ANY, wxT("Signal"), wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT );
	m_staticText_wifi_title->Wrap( -1 );
	fgSizer_status->Add( m_staticText_wifi_title, 0, wxALL|wxEXPAND, 5 );

	m_staticText_wifi_signal = new wxStaticText( m_panel_left, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT );
	m_staticText_wifi_signal->Wrap( -1 );
	fgSizer_status->Add( m_staticText_wifi_signal, 1, wxALL|wxEXPAND, 5 );


	bSizer_left_up->Add( fgSizer_status, 0, wxEXPAND, 5 );

	m_dataViewCtrl_ams = new wxDataViewCtrl( m_panel_left, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0|wxBORDER_SIMPLE);
	m_dataViewCtrl_ams->SetMinSize( wxSize( -1,60 ) );

	bSizer_left_up->Add( m_dataViewCtrl_ams, 1, wxALL|wxEXPAND, 0 );


	bSizer_status->Add( bSizer_left_up, 1, wxEXPAND, 0 );


	bSizer_left->Add( bSizer_status, 1, wxEXPAND, 5 );

	m_staticline1 = new wxStaticLine( m_panel_left, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL );
	bSizer_left->Add( m_staticline1, 0, wxEXPAND | wxALL, 5 );

	wxBoxSizer* bSizer_task;
	bSizer_task = new wxBoxSizer( wxVERTICAL );

	wxBoxSizer* bSizer__task_caption;
	bSizer__task_caption = new wxBoxSizer( wxHORIZONTAL );

	m_staticText_task_caption = new wxStaticText( m_panel_left, wxID_ANY, wxT("Printing"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_task_caption->Wrap( -1 );
	bSizer__task_caption->Add( m_staticText_task_caption, 0, wxALIGN_CENTER|wxALL, 5 );


	bSizer__task_caption->Add( 0, 0, 1, wxEXPAND, 5 );

	m_bpButton_open_project = new wxBitmapButton( m_panel_left, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW|0 );
	bSizer__task_caption->Add( m_bpButton_open_project, 0, wxALL, 5 );


	bSizer_task->Add( bSizer__task_caption, 0, wxALL|wxEXPAND, 0 );

	wxFlexGridSizer* fgSizer_task_info;
	fgSizer_task_info = new wxFlexGridSizer( 0, 2, 0, 0 );
	fgSizer_task_info->AddGrowableCol( 1 );
	fgSizer_task_info->SetFlexibleDirection( wxBOTH );
	fgSizer_task_info->SetNonFlexibleGrowMode( wxFLEX_GROWMODE_SPECIFIED );

	m_staticText_project_title = new wxStaticText( m_panel_left, wxID_ANY, wxT("Project Name"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT|wxBORDER_SIMPLE );
	m_staticText_project_title->Wrap( -1 );
	m_staticText_project_title->Hide();
	m_staticText_project_title->SetMinSize( wxSize( 100,-1 ) );
	m_staticText_project_title->SetMaxSize( wxSize( 100,-1 ) );

	fgSizer_task_info->Add( m_staticText_project_title, 0, wxALL|wxEXPAND, 0 );

	m_staticText_project_name = new wxStaticText( m_panel_left, wxID_ANY, wxT("BBL_Project"), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL|wxBORDER_SIMPLE );
	m_staticText_project_name->Wrap( -1 );
	m_staticText_project_name->Hide();

	fgSizer_task_info->Add( m_staticText_project_name, 1, wxALL|wxEXPAND, 0 );

	m_staticText_profile_title = new wxStaticText( m_panel_left, wxID_ANY, wxT("Profile Name"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT|wxBORDER_SIMPLE );
	m_staticText_profile_title->Wrap( -1 );
	m_staticText_profile_title->Hide();
	m_staticText_profile_title->SetMinSize( wxSize( 100,-1 ) );
	m_staticText_profile_title->SetMaxSize( wxSize( 100,-1 ) );

	fgSizer_task_info->Add( m_staticText_profile_title, 0, wxALL|wxEXPAND, 0 );

	m_staticText_profile_value = new wxStaticText( m_panel_left, wxID_ANY, wxT("BBL_Profile"), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL|wxBORDER_SIMPLE );
	m_staticText_profile_value->Wrap( -1 );
	m_staticText_profile_value->Hide();

	fgSizer_task_info->Add( m_staticText_profile_value, 1, wxALL|wxEXPAND, 0 );

	m_staticText_task_title = new wxStaticText( m_panel_left, wxID_ANY, wxT("Task"), wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT );
	m_staticText_task_title->Wrap( -1 );
	m_staticText_task_title->Hide();
	m_staticText_task_title->SetMinSize( wxSize( 100,-1 ) );
	m_staticText_task_title->SetMaxSize( wxSize( 100,-1 ) );

	fgSizer_task_info->Add( m_staticText_task_title, 0, wxALL|wxEXPAND, 5 );

	m_staticText_task_value = new wxStaticText( m_panel_left, wxID_ANY, wxT("BBL_Task"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT );
	m_staticText_task_value->Wrap( -1 );
	m_staticText_task_value->Hide();

	fgSizer_task_info->Add( m_staticText_task_value, 1, wxALL|wxEXPAND, 5 );

	m_staticText_subtask_title = new wxStaticText( m_panel_left, wxID_ANY, wxT("SubTask"), wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT );
	m_staticText_subtask_title->Wrap( -1 );
	m_staticText_subtask_title->SetMinSize( wxSize( 100,-1 ) );
	m_staticText_subtask_title->SetMaxSize( wxSize( 100,-1 ) );

	fgSizer_task_info->Add( m_staticText_subtask_title, 0, wxALL|wxEXPAND, 5 );

	m_staticText_subtask_value = new wxStaticText( m_panel_left, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT );
	m_staticText_subtask_value->Wrap( -1 );
	fgSizer_task_info->Add( m_staticText_subtask_value, 0, wxALL|wxEXPAND, 5 );


	fgSizer_task_info->Add( 0, 0, 1, wxEXPAND, 5 );

	m_bitmap_thumbnail = new wxStaticBitmap( m_panel_left, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0 );
	m_bitmap_thumbnail->Hide();
	m_bitmap_thumbnail->SetMinSize( wxSize( 80,80 ) );

	fgSizer_task_info->Add( m_bitmap_thumbnail, 0, wxALL, 5 );

	m_staticText_progress_title = new wxStaticText( m_panel_left, wxID_ANY, wxT("Progress"), wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT );
	m_staticText_progress_title->Wrap( -1 );
	m_staticText_progress_title->SetMinSize( wxSize( 100,-1 ) );
	m_staticText_progress_title->SetMaxSize( wxSize( 100,-1 ) );

	fgSizer_task_info->Add( m_staticText_progress_title, 0, wxALL|wxEXPAND, 5 );

	wxBoxSizer* bSizer_progress;
	bSizer_progress = new wxBoxSizer( wxVERTICAL );

	m_gauge_progress = new wxGauge( m_panel_left, wxID_ANY, 100, wxDefaultPosition, wxDefaultSize, wxGA_HORIZONTAL );
	m_gauge_progress->SetValue( 0 );
	bSizer_progress->Add( m_gauge_progress, 0, wxALL|wxEXPAND, 0 );

	m_staticText_progress = new wxStaticText( m_panel_left, wxID_ANY, wxT("0/0"), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL );
	m_staticText_progress->Wrap( -1 );
	bSizer_progress->Add( m_staticText_progress, 0, wxALL|wxEXPAND, 0 );


	fgSizer_task_info->Add( bSizer_progress, 1, wxALL|wxEXPAND, 3 );


	bSizer_task->Add( fgSizer_task_info, 0, wxALL|wxEXPAND, 0 );

	m_dataViewCtrl_subtask = new wxDataViewCtrl( m_panel_left, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0|wxBORDER_SIMPLE );
	m_dataViewCtrl_subtask->SetMinSize( wxSize( -1,60 ) );

	bSizer_task->Add( m_dataViewCtrl_subtask, 1, wxALL|wxEXPAND, 0 );


	bSizer_left->Add( bSizer_task, 1, wxEXPAND, 5 );

	m_staticline2 = new wxStaticLine( m_panel_left, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL );
	bSizer_left->Add( m_staticline2, 0, wxEXPAND | wxALL, 5 );

	wxBoxSizer* bSizer_hms;
	bSizer_hms = new wxBoxSizer( wxVERTICAL );

	wxBoxSizer* bSizer_hms_caption;
	bSizer_hms_caption = new wxBoxSizer( wxVERTICAL );

	m_staticText_hms_caption = new wxStaticText( m_panel_left, wxID_ANY, wxT("Notification"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_hms_caption->Wrap( -1 );
	bSizer_hms_caption->Add( m_staticText_hms_caption, 0, wxALL, 5 );


	bSizer_hms->Add( bSizer_hms_caption, 0, wxEXPAND, 5 );

	m_dataViewListCtrl_hms = new wxDataViewListCtrl( m_panel_left, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0 );
	m_dataViewListCtrl_hms->SetMinSize( wxSize( -1,60 ) );

	bSizer_hms->Add( m_dataViewListCtrl_hms, 1, wxALL|wxEXPAND, 0 );


	bSizer_left->Add( bSizer_hms, 1, wxEXPAND, 5 );


	m_panel_left->SetSizer( bSizer_left );
	m_panel_left->Layout();
	bSizer_left->Fit( m_panel_left );
	bSizer_top->Add( m_panel_left, 0, wxEXPAND | wxALL, 0 );

	wxBoxSizer* bSizer_middle;
	bSizer_middle = new wxBoxSizer( wxVERTICAL );

	bSizer_middle->SetMinSize( wxSize( 300,-1 ) );
	m_notebook = new wxNotebook( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0 );
	m_panel_monitor = new wxPanel( m_notebook, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	wxBoxSizer* bSizer_tab_monitor;
	bSizer_tab_monitor = new wxBoxSizer( wxVERTICAL );

	wxBoxSizer* bSizer_live;
	bSizer_live = new wxBoxSizer( wxVERTICAL );

	m_panel_live = new wxPanel( m_panel_monitor, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	wxBoxSizer* bSizer29;
	bSizer29 = new wxBoxSizer( wxVERTICAL );

	m_bitmap_live_default = new wxStaticBitmap( m_panel_live, wxID_ANY, m_live_default_img, wxDefaultPosition, wxDefaultSize, 0 );
	bSizer29->Add( m_bitmap_live_default, 0, wxALL|wxEXPAND, 5 );


	m_panel_live->SetSizer( bSizer29 );
	m_panel_live->Layout();
	bSizer29->Fit( m_panel_live );
	bSizer_live->Add( m_panel_live, 1, wxALL|wxEXPAND, 5 );


	bSizer_tab_monitor->Add( bSizer_live, 0, wxEXPAND, 5 );

	wxBoxSizer* bSizer_caption;
	bSizer_caption = new wxBoxSizer( wxVERTICAL );

	m_staticText_caption = new wxStaticText( m_panel_monitor, wxID_ANY, wxT("Printing Control"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_caption->Wrap( -1 );
	bSizer_caption->Add( m_staticText_caption, 0, wxALL, 5 );


	bSizer_tab_monitor->Add( bSizer_caption, 0, wxEXPAND, 5 );

	wxBoxSizer* bSizer_task_btn;
	bSizer_task_btn = new wxBoxSizer( wxHORIZONTAL );


	bSizer_task_btn->Add( 0, 0, 1, wxEXPAND, 5 );

	m_button_pause_resume = new wxButton( m_panel_monitor, wxID_ANY, wxT("Pause"), wxDefaultPosition, wxDefaultSize, 0 );
	m_button_pause_resume->SetMinSize( wxSize( 150,40 ) );

	bSizer_task_btn->Add( m_button_pause_resume, 0, wxALIGN_CENTER|wxALL, 5 );

	m_button_abort = new wxButton( m_panel_monitor, wxID_ANY, wxT("Abort"), wxDefaultPosition, wxDefaultSize, 0 );
	m_button_abort->SetMinSize( wxSize( 150,40 ) );

	bSizer_task_btn->Add( m_button_abort, 0, wxALIGN_CENTER|wxALL, 5 );


	bSizer_tab_monitor->Add( bSizer_task_btn, 0, wxALIGN_CENTER_HORIZONTAL|wxALL|wxBOTTOM, 5 );


	bSizer_tab_monitor->Add( 0, 0, 1, wxEXPAND, 5 );


	m_panel_monitor->SetSizer( bSizer_tab_monitor );
	m_panel_monitor->Layout();
	bSizer_tab_monitor->Fit( m_panel_monitor );
	m_notebook->AddPage( m_panel_monitor, wxT("Monitor"), true );
	m_panel_timelapse = new wxPanel( m_notebook, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
	m_notebook->AddPage( m_panel_timelapse, wxT("Timelapse"), false );

	bSizer_middle->Add( m_notebook, 1, wxEXPAND | wxALL, 5 );


	bSizer_top->Add( bSizer_middle, 1, wxEXPAND, 5 );

	m_staticline6 = new wxStaticLine( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_VERTICAL );
	bSizer_top->Add( m_staticline6, 0, wxEXPAND | wxALL, 10 );

	wxBoxSizer* bSizer_control;
	bSizer_control = new wxBoxSizer( wxVERTICAL );

	bSizer_control->SetMinSize( wxSize( 260,-1 ) );
	wxBoxSizer* bSizer_axis_ctrl;
	bSizer_axis_ctrl = new wxBoxSizer( wxVERTICAL );

	wxBoxSizer* bSizer_axis_ctrl_caption;
	bSizer_axis_ctrl_caption = new wxBoxSizer( wxVERTICAL );

	m_staticText_ctrl_caption = new wxStaticText( this, wxID_ANY, wxT("Axis Control"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_ctrl_caption->Wrap( -1 );
	bSizer_axis_ctrl_caption->Add( m_staticText_ctrl_caption, 0, wxALL, 5 );


	bSizer_axis_ctrl->Add( bSizer_axis_ctrl_caption, 1, wxEXPAND, 5 );

	wxGridBagSizer* gbSizer_control;
	gbSizer_control = new wxGridBagSizer( 0, 0 );
	gbSizer_control->SetFlexibleDirection( wxBOTH );
	gbSizer_control->SetNonFlexibleGrowMode( wxFLEX_GROWMODE_ALL );

	m_bpButton_y_up = new wxBitmapButton( this, wxID_ANY, m_ctrl_up, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW|0 );

	m_bpButton_y_up->SetMinSize( wxSize( 32,32 ) );

	gbSizer_control->Add( m_bpButton_y_up, wxGBPosition( 1, 1 ), wxGBSpan( 1, 1 ), wxALL, 0 );

	m_bpButton_y_down = new wxBitmapButton( this, wxID_ANY, m_ctrl_down, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW|0 );
	m_bpButton_y_down->SetMinSize( wxSize( 32,32 ) );

	gbSizer_control->Add( m_bpButton_y_down, wxGBPosition( 3, 1 ), wxGBSpan( 1, 1 ), wxALL, 0 );

	m_bpButton_xy_home = new wxBitmapButton( this, wxID_ANY, m_ctrl_home, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW|0 );
	m_bpButton_xy_home->SetMinSize( wxSize( 32,32 ) );

	gbSizer_control->Add( m_bpButton_xy_home, wxGBPosition( 2, 1 ), wxGBSpan( 1, 1 ), wxALL, 0 );

	m_bpButton_x_left = new wxBitmapButton( this, wxID_ANY, m_ctrl_left, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW|0 );
	m_bpButton_x_left->SetMinSize( wxSize( 32,32 ) );

	gbSizer_control->Add( m_bpButton_x_left, wxGBPosition( 2, 0 ), wxGBSpan( 1, 1 ), wxALL, 0 );

	m_bpButton_x_right = new wxBitmapButton( this, wxID_ANY, m_ctrl_right, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW|0 );
	m_bpButton_x_right->SetMinSize( wxSize( 32,32 ) );

	gbSizer_control->Add( m_bpButton_x_right, wxGBPosition( 2, 2 ), wxGBSpan( 1, 1 ), wxALL, 0 );

	m_bpButton_z_up = new wxBitmapButton( this, wxID_ANY, m_ctrl_up, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW|0 );
	m_bpButton_z_up->SetMinSize( wxSize( 32,32 ) );

	gbSizer_control->Add( m_bpButton_z_up, wxGBPosition( 1, 6 ), wxGBSpan( 1, 1 ), wxALL, 0 );

	m_bpButton_z_home = new wxBitmapButton( this, wxID_ANY, m_ctrl_home, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW|0 );
	m_bpButton_z_home->SetMinSize( wxSize( 32,32 ) );

	gbSizer_control->Add( m_bpButton_z_home, wxGBPosition( 2, 6 ), wxGBSpan( 1, 1 ), wxALL, 0 );

	m_bpButton_z_down = new wxBitmapButton( this, wxID_ANY, m_ctrl_down, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW|0 );
	m_bpButton_z_down->SetMinSize( wxSize( 32,32 ) );

	gbSizer_control->Add( m_bpButton_z_down, wxGBPosition( 3, 6 ), wxGBSpan( 1, 1 ), wxALL, 0 );

	m_staticText_X = new wxStaticText( this, wxID_ANY, wxT("X          "), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_X->Wrap( -1 );
	gbSizer_control->Add( m_staticText_X, wxGBPosition( 2, 4 ), wxGBSpan( 1, 1 ), wxALIGN_CENTER|wxALL, 0 );

	m_staticText_Y = new wxStaticText( this, wxID_ANY, wxT("Y"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_Y->Wrap( -1 );
	gbSizer_control->Add( m_staticText_Y, wxGBPosition( 0, 1 ), wxGBSpan( 1, 1 ), wxALIGN_CENTER|wxALL, 5 );

	m_staticText_Z = new wxStaticText( this, wxID_ANY, wxT("Z"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_Z->Wrap( -1 );
	gbSizer_control->Add( m_staticText_Z, wxGBPosition( 0, 6 ), wxGBSpan( 1, 1 ), wxALIGN_CENTER|wxALL, 5 );


	bSizer_axis_ctrl->Add( gbSizer_control, 0, wxALIGN_CENTER|wxALL|wxEXPAND, 5 );

	wxBoxSizer* bSizer_unit;
	bSizer_unit = new wxBoxSizer( wxHORIZONTAL );

	m_button_0_1 = new wxToggleButton( this, wxID_ANY, wxT("0.1"), wxDefaultPosition, wxDefaultSize, 0 );
	m_button_0_1->SetMinSize( wxSize( 50,-1 ) );

	bSizer_unit->Add( m_button_0_1, 0, wxALL, 0 );

	m_button_1_0 = new wxToggleButton( this, wxID_ANY, wxT("1"), wxDefaultPosition, wxDefaultSize, 0 );
	m_button_1_0->SetMinSize( wxSize( 50,-1 ) );

	bSizer_unit->Add( m_button_1_0, 0, wxALL, 0 );

	m_button_10_0 = new wxToggleButton( this, wxID_ANY, wxT("10"), wxDefaultPosition, wxDefaultSize, 0 );
	m_button_10_0->SetMinSize( wxSize( 50,-1 ) );

	bSizer_unit->Add( m_button_10_0, 0, wxALL, 0 );

	m_button_100_0 = new wxToggleButton( this, wxID_ANY, wxT("100"), wxDefaultPosition, wxDefaultSize, 0 );
	m_button_100_0->SetMinSize( wxSize( 50,-1 ) );

    bSizer_unit->Add( m_button_100_0, 0, wxALL, 0 );


	bSizer_axis_ctrl->Add( bSizer_unit, 0, wxALIGN_CENTER, 1 );


	bSizer_control->Add( bSizer_axis_ctrl, 0, wxALL|wxEXPAND, 5 );

	m_staticline3 = new wxStaticLine( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL );
	bSizer_control->Add( m_staticline3, 0, wxEXPAND | wxALL, 10 );

	wxBoxSizer* bSizer_temp_ctrl;
	bSizer_temp_ctrl = new wxBoxSizer( wxVERTICAL );

	wxBoxSizer* bSizer_temp_caption;
	bSizer_temp_caption = new wxBoxSizer( wxVERTICAL );

	m_staticText_temp_caption = new wxStaticText( this, wxID_ANY, wxT("Temperature Control"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_temp_caption->Wrap( -1 );
	m_staticText_temp_caption->SetFont( wxFont( wxNORMAL_FONT->GetPointSize(), wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxEmptyString ) );

	bSizer_temp_caption->Add( m_staticText_temp_caption, 0, wxALL, 5 );


	bSizer_temp_ctrl->Add( bSizer_temp_caption, 0, wxEXPAND, 5 );

	wxFlexGridSizer* fgSizer_temp;
	fgSizer_temp = new wxFlexGridSizer( 0, 5, 0, 0 );
	fgSizer_temp->SetFlexibleDirection( wxBOTH );
	fgSizer_temp->SetNonFlexibleGrowMode( wxFLEX_GROWMODE_SPECIFIED );


	fgSizer_temp->Add( 40, 0, 1, wxEXPAND, 5 );

	m_staticText_current = new wxStaticText( this, wxID_ANY, wxT("Current"), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL );
	m_staticText_current->Wrap( -1 );
	m_staticText_current->SetMinSize( wxSize( 60,-1 ) );

	fgSizer_temp->Add( m_staticText_current, 0, wxALL, 5 );

	m_staticText_target = new wxStaticText( this, wxID_ANY, wxT("Target"), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL );
	m_staticText_target->Wrap( -1 );
	m_staticText_target->SetMinSize( wxSize( 60,-1 ) );

	fgSizer_temp->Add( m_staticText_target, 0, wxALL, 5 );


	fgSizer_temp->Add( 0, 0, 1, wxEXPAND, 5 );


	fgSizer_temp->Add( 0, 0, 1, wxEXPAND, 5 );

	m_bitmap_bed = new wxStaticBitmap( this, wxID_ANY, m_bed_img, wxDefaultPosition, wxDefaultSize, 0 );
	fgSizer_temp->Add( m_bitmap_bed, 0, wxALIGN_CENTER_VERTICAL|wxALIGN_RIGHT|wxALL, 5 );

	m_staticText_bed_current = new wxStaticText( this, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL );
	m_staticText_bed_current->Wrap( -1 );
	fgSizer_temp->Add( m_staticText_bed_current, 1, wxALIGN_CENTER|wxALL, 0 );

	m_staticText_bed_target = new wxStaticText( this, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL );
	m_staticText_bed_target->Wrap( -1 );
	fgSizer_temp->Add( m_staticText_bed_target, 1, wxALIGN_CENTER|wxALL, 0 );

	m_textCtrl_bed = new wxTextCtrl( this, wxID_ANY, wxT("50"), wxDefaultPosition, wxDefaultSize, wxTE_CENTER );
	m_textCtrl_bed->SetMinSize( wxSize( 50,-1 ) );

	fgSizer_temp->Add( m_textCtrl_bed, 0, wxALIGN_CENTER|wxALL, 5 );

	m_button_set_bed = new wxButton( this, wxID_ANY, wxT("Set"), wxDefaultPosition, wxDefaultSize, 0 );
	m_button_set_bed->SetMinSize( wxSize( 50,-1 ) );

	fgSizer_temp->Add( m_button_set_bed, 0, wxALL, 5 );

	m_bitmap_nozzle = new wxStaticBitmap( this, wxID_ANY, m_nozzle_img, wxDefaultPosition, wxDefaultSize, 0 );
	fgSizer_temp->Add( m_bitmap_nozzle, 0, wxALIGN_CENTER_VERTICAL|wxALIGN_RIGHT|wxALL, 5 );

	m_staticText_nozzle_current = new wxStaticText( this, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL );
	m_staticText_nozzle_current->Wrap( -1 );
	fgSizer_temp->Add( m_staticText_nozzle_current, 1, wxALIGN_CENTER|wxALL, 0 );

	m_staticText_nozzle_target = new wxStaticText( this, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL );
	m_staticText_nozzle_target->Wrap( -1 );
	fgSizer_temp->Add( m_staticText_nozzle_target, 1, wxALIGN_CENTER|wxALL, 0 );

	m_textCtrl_nozzle = new wxTextCtrl( this, wxID_ANY, wxT("200"), wxDefaultPosition, wxDefaultSize, wxTE_CENTER );
	m_textCtrl_nozzle->SetMinSize( wxSize( 50,-1 ) );

	fgSizer_temp->Add( m_textCtrl_nozzle, 0, wxALIGN_CENTER|wxALL, 5 );

	m_button_set_nozzle = new wxButton( this, wxID_ANY, wxT("Set"), wxDefaultPosition, wxDefaultSize, 0 );
	m_button_set_nozzle->SetMinSize( wxSize( 50,-1 ) );

	fgSizer_temp->Add( m_button_set_nozzle, 0, wxALL, 5 );


	bSizer_temp_ctrl->Add( fgSizer_temp, 0, wxALL, 5 );


	bSizer_control->Add( bSizer_temp_ctrl, 0, wxALL|wxEXPAND, 5 );

	m_staticline4 = new wxStaticLine( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL );
	bSizer_control->Add( m_staticline4, 0, wxEXPAND | wxALL, 10 );

	wxBoxSizer* bSizer_extruder_ctrl;
	bSizer_extruder_ctrl = new wxBoxSizer( wxVERTICAL );

	wxBoxSizer* bSizer_extruder_ctrl_caption;
	bSizer_extruder_ctrl_caption = new wxBoxSizer( wxVERTICAL );

	m_staticText_extruder_ctrl_caption = new wxStaticText( this, wxID_ANY, wxT("Extruder Control"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_extruder_ctrl_caption->Wrap( -1 );
	bSizer_extruder_ctrl_caption->Add( m_staticText_extruder_ctrl_caption, 0, wxALL, 5 );


	bSizer_extruder_ctrl->Add( bSizer_extruder_ctrl_caption, 0, wxEXPAND, 5 );

	wxFlexGridSizer* fgSizer_extruder;
	fgSizer_extruder = new wxFlexGridSizer( 2, 3, 0, 0 );
	fgSizer_extruder->SetFlexibleDirection( wxBOTH );
	fgSizer_extruder->SetNonFlexibleGrowMode( wxFLEX_GROWMODE_SPECIFIED );

	m_comboBox_trays = new wxComboBox( this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, NULL, 0 );
	fgSizer_extruder->Add( m_comboBox_trays, 1, wxALIGN_CENTER|wxALL, 5 );

	m_button_extreder_feed = new wxButton( this, wxID_ANY, wxT("Feed"), wxDefaultPosition, wxDefaultSize, 0 );
	m_button_extreder_feed->SetMinSize( wxSize( 80,-1 ) );

	fgSizer_extruder->Add( m_button_extreder_feed, 0, wxALIGN_CENTER|wxALL, 5 );

	m_button_extruder_back = new wxButton( this, wxID_ANY, wxT("Back"), wxDefaultPosition, wxDefaultSize, 0 );
	m_button_extruder_back->SetMinSize( wxSize( 80,-1 ) );
	m_button_extruder_back->SetMaxSize( wxSize( -1,30 ) );

	fgSizer_extruder->Add( m_button_extruder_back, 1, wxALL|wxEXPAND, 5 );

	wxBoxSizer* bSizer30;
	bSizer30 = new wxBoxSizer( wxHORIZONTAL );

	m_textCtrl_extrude = new wxTextCtrl( this, wxID_ANY, wxT("20"), wxDefaultPosition, wxDefaultSize, 0 );
	m_textCtrl_extrude->SetMinSize( wxSize( 80,-1 ) );

	bSizer30->Add( m_textCtrl_extrude, 1, wxALIGN_CENTER|wxALL, 5 );

	m_staticText_unit_extrude = new wxStaticText( this, wxID_ANY, wxT("mm"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_unit_extrude->Wrap( -1 );
	bSizer30->Add( m_staticText_unit_extrude, 0, wxALIGN_CENTER|wxALL, 5 );


	fgSizer_extruder->Add( bSizer30, 1, wxEXPAND, 5 );

	m_button_extruder_in = new wxButton( this, wxID_ANY, wxT("Extrude"), wxDefaultPosition, wxSize( 110,-1 ), 0 );
	m_button_extruder_in->SetMinSize( wxSize( 80,-1 ) );

	fgSizer_extruder->Add( m_button_extruder_in, 0, wxALIGN_CENTER|wxALL, 5 );

	m_button_extruder_out = new wxButton( this, wxID_ANY, wxT("Retraction"), wxDefaultPosition, wxSize( 110,-1 ), 0 );
	m_button_extruder_out->SetMinSize( wxSize( 80,-1 ) );

	fgSizer_extruder->Add( m_button_extruder_out, 0, wxALIGN_CENTER|wxALL, 5 );


	bSizer_extruder_ctrl->Add( fgSizer_extruder, 1, wxEXPAND, 5 );


	bSizer_control->Add( bSizer_extruder_ctrl, 0, wxALL, 5 );

	m_staticline5 = new wxStaticLine( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL );
	bSizer_control->Add( m_staticline5, 0, wxEXPAND | wxALL, 10 );

	wxBoxSizer* bSizer_other_ctrl;
	bSizer_other_ctrl = new wxBoxSizer( wxVERTICAL );

	wxBoxSizer* bSizer_other_ctrl_caption;
	bSizer_other_ctrl_caption = new wxBoxSizer( wxVERTICAL );

	m_staticText_other_caption = new wxStaticText( this, wxID_ANY, wxT("Other Control"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticText_other_caption->Wrap( -1 );
	bSizer_other_ctrl_caption->Add( m_staticText_other_caption, 0, wxALL, 5 );


	bSizer_other_ctrl->Add( bSizer_other_ctrl_caption, 0, wxEXPAND, 5 );

	wxFlexGridSizer* fgSizer_others;
	fgSizer_others = new wxFlexGridSizer( 0, 2, 0, 0 );
	fgSizer_others->SetFlexibleDirection( wxBOTH );
	fgSizer_others->SetNonFlexibleGrowMode( wxFLEX_GROWMODE_SPECIFIED );

	m_button_fan_on = new wxButton( this, wxID_ANY, wxT("Fan On"), wxDefaultPosition, wxSize( 110,-1 ), 0 );
	fgSizer_others->Add( m_button_fan_on, 0, wxALL, 5 );

	m_button_fan_off = new wxButton( this, wxID_ANY, wxT("Fan Off"), wxDefaultPosition, wxSize( 110,-1 ), 0 );
	fgSizer_others->Add( m_button_fan_off, 0, wxALL, 5 );

	m_button_auto_leveling = new wxButton( this, wxID_ANY, wxT("Auto Leveling"), wxDefaultPosition, wxSize( 110,-1 ), 0 );
	fgSizer_others->Add( m_button_auto_leveling, 0, wxALL, 5 );

	m_button_xyz_abs = new wxButton( this, wxID_ANY, wxT("XYZ abs"), wxDefaultPosition, wxSize( 110,-1 ), 0 );
	fgSizer_others->Add( m_button_xyz_abs, 0, wxALL, 5 );


	bSizer_other_ctrl->Add( fgSizer_others, 1, wxEXPAND, 5 );


	bSizer_control->Add( bSizer_other_ctrl, 0, wxALL|wxEXPAND, 5 );


	bSizer_control->Add( 0, 0, 1, wxALL|wxEXPAND, 5 );


	bSizer_top->Add( bSizer_control, 0, wxALL|wxEXPAND, 5 );


	bSizer_top->Add( 10, 0, 0, wxEXPAND, 5 );


	this->SetSizer( bSizer_top );
	this->Layout();


    set_toggle_widget_on(m_button_0_1);

    init_model();
    init_timer();
    init_bind();

    obj = nullptr;
    is_pausing = false;
    m_ctrl_unit = 0.1f;

    // Connect Events

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
    m_button_set_bed->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( MonitorPanel::on_set_bed_temp ), NULL, this );
	m_button_set_nozzle->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( MonitorPanel::on_set_nozzle_temp ), NULL, this );
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
    m_button_set_bed->Disconnect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( MonitorPanel::on_set_bed_temp ), NULL, this );
	m_button_set_nozzle->Disconnect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( MonitorPanel::on_set_nozzle_temp ), NULL, this );
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

    m_bed_img = create_scaled_bitmap("monitor_bed_temp", nullptr, 16);
    m_nozzle_img = create_scaled_bitmap("monitor_nozzle_temp", nullptr, 16);
    m_live_default_img = create_scaled_bitmap("live_stream_default", nullptr, FromDIP(300));
}

void MonitorPanel::init_model()
{
    subtask_model = new SubTaskListModel();
    m_dataViewCtrl_subtask->AssociateModel(subtask_model.get());
    m_dataViewCtrl_subtask->AppendTextColumn("Task",
        SubTaskListModel::Col_Name,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_RESIZABLE);

    m_dataViewCtrl_subtask->AppendTextColumn("Duration",
        SubTaskListModel::Col_Duration,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_RESIZABLE);

    m_dataViewCtrl_subtask->AppendTextColumn("Weight",
        SubTaskListModel::Col_Weight,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_RESIZABLE);

    tray_model = new TrayListModel();
    m_dataViewCtrl_ams->AssociateModel(tray_model.get());
    m_dataViewCtrl_ams->AppendTextColumn("Name",
        TrayListModel::Col_TrayTitle,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_RESIZABLE);
    m_dataViewCtrl_ams->AppendTextColumn("Color",
        TrayListModel::Col_TrayColor,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_RESIZABLE);
    m_dataViewCtrl_ams->AppendTextColumn("Meterial",
        TrayListModel::Col_TrayMeterial,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_RESIZABLE);
    m_dataViewCtrl_ams->AppendTextColumn("SN",
        TrayListModel::Col_TraySN,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_RESIZABLE);
    m_dataViewCtrl_ams->AppendTextColumn("Weight",
        TrayListModel::Col_TrayWeight,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_RESIZABLE);
    m_dataViewCtrl_ams->AppendTextColumn("Diameter",
        TrayListModel::Col_TrayDiameter,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_RESIZABLE);
    m_dataViewCtrl_ams->AppendTextColumn("Time",
        TrayListModel::Col_TrayTime,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_RESIZABLE);
    m_dataViewCtrl_ams->AppendTextColumn("Smooth",
        TrayListModel::Col_TraySmooth,
        wxDATAVIEW_CELL_INERT,
        wxCOL_WIDTH_AUTOSIZE,
        wxALIGN_NOT,
        wxDATAVIEW_COL_RESIZABLE);
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

#if defined(__WINDOWS__) || defined(__APPLE__)
    Bind(wxEVT_WEBREQUEST_STATE, &MonitorPanel::on_webrequest_state, this);
#endif

    m_staticText_subtask_value->Bind(wxEVT_ENTER_WINDOW,
        [this](wxMouseEvent& evt) {
            /* show image and loading */
            Slic3r::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();
            obj = account_manager->get_default_machine();
            if (!obj) return;

            BBLSubTask* subtask = obj->get_subtask();
            if (!subtask) return;

            BBLProfile* profile = obj->profile_;
            if (!profile) return;

            std::map<std::string, BBLSliceInfo*>::iterator iter = profile->slice_info.find(subtask->task_partplate_idx);
            if (iter == profile->slice_info.end()) {
                return;
            }
    
            request_url = wxString(iter->second->thumbnail_url);

            wxBitmap image = create_scaled_bitmap("revert_all_");
            std::map<wxString, wxBitmap>::iterator it = img_list.find(request_url);
            if (it != img_list.end()) {
                image = it->second;
            }
            else {
#if defined(__WINDOWS__) || defined(__APPLE__)
                web_request = wxWebSession::GetDefault().CreateRequest(this, request_url);
                web_request.Start();
#endif
            }

            m_plate_thumbnail = std::make_shared<ImageTransientPopup>(this, false, image);
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

#if defined(__WINDOWS__) || defined(__APPLE__)
/* web state */
void MonitorPanel::on_webrequest_state(wxWebRequestEvent& evt)
{
    switch (evt.GetState())
    {
    case wxWebRequest::State_Completed:
        if (m_plate_thumbnail) {
            wxImage img(*evt.GetResponse().GetStream());
            img_list.insert(std::make_pair(request_url, img));
            m_plate_thumbnail->SetImage(img);
        }
        break;
    case wxWebRequest::State_Failed:
    case wxWebRequest::State_Cancelled:
    case wxWebRequest::State_Unauthorized:
    case wxWebRequest::State_Active:
    case wxWebRequest::State_Idle:
        break;
    default:
        break;
    }
}
#endif

bool MonitorPanel::Show(bool show)
{
    Slic3r::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();
    obj = account_manager->get_default_machine();

    if (obj && show) {
        update_ams(obj);
    }

    return wxPanel::Show(show);
}

void MonitorPanel::Reset()
{
    obj = nullptr;
    last_task = nullptr;
    last_subtask = nullptr;
}

void MonitorPanel::update_ams(MachineObject* obj)
{
    if (!obj) return;
    tray_model->update(obj);
}

void MonitorPanel::update_task(MachineObject* obj)
{
    if (!obj) return;

    if (!obj->task_ || !obj->profile_) return;

    if (last_task != obj->task_ 
        && last_profile != obj->profile_
        && !obj->profile_->slice_info.empty()) {
        std::vector<BBLSubTask*>::iterator it;
        std::map<std::string, BBLSliceInfo*>::iterator iter;
        subtask_model->clear();
        for (it = obj->task_->subtasks.begin(); it != obj->task_->subtasks.end(); it++) {
            iter = obj->profile_->slice_info.find((*it)->task_partplate_idx);
            if (iter != obj->profile_->slice_info.end()) {
                subtask_model->add_item((*it)->task_name, iter->second->prediction, iter->second->weight);
            }
        }
        subtask_model->reset();
        last_task = obj->task_;
        last_profile = obj->profile_;
    }
}

void MonitorPanel::update_subtask(MachineObject* obj)
{
    BBLSubTask* curr_subtask = obj->get_subtask();
    if (last_subtask != curr_subtask) {
        on_subtask_update(curr_subtask, false);
    }
    last_subtask = curr_subtask;
}

void MonitorPanel::update_profile(MachineObject* obj)
{
    if (!obj) return;
    BBLProfile* profile = obj->profile_;
    if (last_profile != profile) {
        subtask_model->update_profile(profile);
    }
    last_profile = profile;
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
    wxString nozzle_temp_curr_text = wxString::Format("%0.2f", obj->nozzle_temp);
    m_staticText_nozzle_current->SetLabelText(nozzle_temp_curr_text);

    wxString nozzle_temp_target_text = wxString::Format("%0.2f", obj->nozzle_temp_target);
    m_staticText_nozzle_target->SetLabelText(nozzle_temp_target_text);
    
    wxString bed_temp_curr_text = wxString::Format("%0.2f", obj->bed_temp);
    m_staticText_bed_current->SetLabelText(bed_temp_curr_text);

    wxString bed_temp_target_text = wxString::Format("%0.2f", obj->bed_temp_target);
    m_staticText_bed_target->SetLabelText(bed_temp_target_text);
}

void MonitorPanel::on_timer(wxTimerEvent& event)
{
    Freeze();
    update_all();
    Thaw();
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

    update_profile(obj);

    //update_task(obj);

    update_subtask(obj);
}

void MonitorPanel::on_select(wxCommandEvent& event)
{
    subtask_model->clear_data();
    tray_model->clear_data();

    last_subtask = nullptr;
    last_profile = nullptr;
    last_task = nullptr;

    update_all();

    Layout();
    Refresh();
}

void MonitorPanel::select_machine(std::string machine_sn)
{
    wxCommandEvent* event = new wxCommandEvent(wxEVT_COMMAND_CHOICE_SELECTED);
    event->SetString(machine_sn);
    wxQueueEvent(this, event);
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
                    m_staticText_profile_value->SetLabelText(wxString::Format("%s(%s)", task->profile_->profile_name, task->task_profile_id));
                }

                // update task name
                m_staticText_task_value->SetLabelText(wxString::Format("%s(%s)", task->task_name, task->task_id));
            }
        }
    }
    
    // update subtask name
    m_staticText_subtask_value->SetLabelText(wxString::Format("%s(%s)", curr_subtask->task_name, curr_subtask->task_id));

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
    BBLSliceInfo* info = obj->get_slice_info(curr_subtask->task_partplate_idx);
    if (info) {
        try {
            total_time = get_time_dhms(info->prediction);
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
        obj->command_axis_control("Y", get_control_unit(), 1.0f, 3000);
}

void MonitorPanel::on_axis_ctrl_y_down(wxCommandEvent& event)
{
    if (obj)
        obj->command_axis_control("Y", get_control_unit(), -1.0f, 3000);
}

void MonitorPanel::on_axis_ctrl_xy_home(wxCommandEvent& event)
{
    if (obj)
        obj->command_go_home();
}

void MonitorPanel::on_axis_ctrl_x_left(wxCommandEvent& event)
{
    if (obj)
        obj->command_axis_control("X", get_control_unit(), -1.0f, 3000);
}

void MonitorPanel::on_axis_ctrl_x_right(wxCommandEvent& event)
{
    if (obj)
        obj->command_axis_control("X", get_control_unit(), 1.0f, 3000);
}

void MonitorPanel::on_axis_ctrl_z_up(wxCommandEvent& event)
{
    if (obj)
        obj->command_axis_control("Z", get_control_unit(), -1.0f, 900);
}

void MonitorPanel::on_axis_ctrl_z_home(wxCommandEvent& event)
{
    if (obj)
        obj->command_go_home();
}

void MonitorPanel::on_axis_ctrl_z_down(wxCommandEvent& event)
{
    if (obj)
        obj->command_axis_control("Z", get_control_unit(), 1.0f, 900);
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

void MonitorPanel::on_set_bed_temp(wxCommandEvent& event)
{
    try {
        int temp = wxAtoi(m_textCtrl_bed->GetValue());
        if (obj)
            obj->command_set_bed(temp);
    }
    catch(...) {
        ;
    }
}

void MonitorPanel::on_set_nozzle_temp(wxCommandEvent& event)
{
    try {
        int temp = wxAtoi(m_textCtrl_nozzle->GetValue());
        if (obj)
            obj->command_set_nozzle(temp);
    }
    catch(...) {
        ;
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
