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
#include "Widgets/Label.hpp"
#include "format.hpp"
#include "wxMediaCtrl2.h"

#ifdef __WXMAC__
#include "wxMediaCtrl2.h"
#endif

namespace Slic3r {
namespace GUI {

#define REFRESH_INTERVAL       1000


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

void SubTaskListModel::update_task(BBLTask* task)
{
    if (!task) return;

    m_nameColValues.clear();
    m_durationColValues.clear();
    m_WeightColValues.clear();
    m_PlateIdxColValues.clear();

    std::vector<BBLSubTask*>::iterator it;
    std::map<std::string, BBLSliceInfo*>::iterator iter;
    for (it = task->subtasks.begin(); it != task->subtasks.end(); it++) {
        iter = task->slice_info.find((*it)->task_partplate_idx);
        if (iter != task->slice_info.end()) {
            add_item((*it)->task_name, iter->second->prediction, iter->second->weight);
        }
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

///////////////////////////////////////////////////////////////////////////////
/// Class SubTaskPanel
///////////////////////////////////////////////////////////////////////////////
SubTaskPanel::SubTaskPanel( wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style, const wxString& name ) : wxPanel( parent, id, pos, size, style, name )
{
	wxBoxSizer* bSizer_top;
	bSizer_top = new wxBoxSizer( wxVERTICAL );


	bSizer_top->Add( 0, 5, 0, wxEXPAND, 5 );

	wxBoxSizer* bSizer_middle;
	bSizer_middle = new wxBoxSizer( wxHORIZONTAL );


	bSizer_middle->Add( 20, 0, 0, wxEXPAND, 5 );

	m_bitmap = new wxStaticBitmap( this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize( 40,40 ), 0 );
	bSizer_middle->Add( m_bitmap, 0, wxALIGN_CENTER, 5 );


	bSizer_middle->Add( 9, 0, 0, 0, 5 );

	wxBoxSizer* bSizer_right;
	bSizer_right = new wxBoxSizer( wxVERTICAL );

	wxBoxSizer* bSizer_up;
	bSizer_up = new wxBoxSizer( wxHORIZONTAL );

	m_staticText_subtask_name = new wxStaticText( this, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT|wxST_ELLIPSIZE_END );
	m_staticText_subtask_name->Wrap( -1 );
	bSizer_up->Add( m_staticText_subtask_name, 1, wxALIGN_CENTER_VERTICAL|wxALL, 0 );

	m_bpButton_print = new wxBitmapButton( this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize( 24,24 ), wxBU_AUTODRAW|0|wxBORDER_NONE );
	bSizer_up->Add( m_bpButton_print, 0, wxALIGN_CENTER_VERTICAL|wxALL, 0 );


	bSizer_right->Add( bSizer_up, 1, wxEXPAND, 5 );

	wxBoxSizer* bSizer_down;
	bSizer_down = new wxBoxSizer( wxHORIZONTAL );

	m_staticText_prediction_title = new wxStaticText( this, wxID_ANY, wxT("estimate time : "), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT );
	m_staticText_prediction_title->Wrap( -1 );
	bSizer_down->Add( m_staticText_prediction_title, 0, wxALIGN_CENTER_VERTICAL|wxALL, 0 );

	m_staticText_prediction_value = new wxStaticText( this, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT|wxST_ELLIPSIZE_END );
	m_staticText_prediction_value->Wrap( -1 );
	bSizer_down->Add( m_staticText_prediction_value, 1, wxALIGN_CENTER_VERTICAL|wxALL, 0 );


	bSizer_down->Add( 15, 0, 0, wxEXPAND, 5 );

	m_staticText_weight_title = new wxStaticText( this, wxID_ANY, wxT("weight cost : "), wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT );
	m_staticText_weight_title->Wrap( -1 );
	bSizer_down->Add( m_staticText_weight_title, 0, wxALIGN_CENTER_VERTICAL|wxALL, 0 );

	m_staticText_weight_value = new wxStaticText( this, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT );
	m_staticText_weight_value->Wrap( -1 );
	bSizer_down->Add( m_staticText_weight_value, 0, wxALIGN_CENTER_VERTICAL|wxALL, 0 );


	bSizer_right->Add( bSizer_down, 0, wxEXPAND, 5 );


	bSizer_middle->Add( bSizer_right, 1, wxEXPAND, 5 );


	bSizer_middle->Add( 20, 0, 0, wxEXPAND, 5 );


	bSizer_top->Add( bSizer_middle, 1, wxALIGN_CENTER|wxEXPAND, 5 );


	bSizer_top->Add( 0, 5, 0, wxEXPAND, 5 );


	this->SetSizer( bSizer_top );
	this->Layout();


    printing_bmp = create_scaled_bitmap("monitor_subtask_print", nullptr, 24);
    m_bpButton_print->SetBitmap(printing_bmp);

    Bind(wxEVT_WEBREQUEST_STATE, &SubTaskPanel::on_webrequest_state, this);

    // Connect Events
    m_bitmap->Connect( wxEVT_ENTER_WINDOW, wxMouseEventHandler( SubTaskPanel::on_thumbnail_enter ), NULL, this );
	m_bitmap->Connect( wxEVT_LEAVE_WINDOW, wxMouseEventHandler( SubTaskPanel::on_thumbnail_leave ), NULL, this );
	m_bpButton_print->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( SubTaskPanel::on_subtask_print ), NULL, this );
}

SubTaskPanel::~SubTaskPanel()
{
    // Disconnect Events
    m_bitmap->Disconnect( wxEVT_ENTER_WINDOW, wxMouseEventHandler( SubTaskPanel::on_thumbnail_enter ), NULL, this );
	m_bitmap->Disconnect( wxEVT_LEAVE_WINDOW, wxMouseEventHandler( SubTaskPanel::on_thumbnail_leave ), NULL, this );
	m_bpButton_print->Disconnect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( SubTaskPanel::on_subtask_print ), NULL, this );
}

void SubTaskPanel::on_subtask_print(wxCommandEvent& evt)
{
    int result = -1;
    Slic3r::AccountManager* c = Slic3r::GUI::wxGetApp().getAccountManager();
    MachineObject* obj = c->get_default_machine();
    if (!obj->can_print()) {
        wxMessageBox("Current Printer is Busy!");
        return;
    }
    result = c->poll_3mf(&m_subtask);
    if (result < 0 || m_subtask.task_url.empty()
        || m_subtask.task_url.compare("null") == 0
        || m_subtask.task_url_md5.empty()) {
        wxMessageBox("poll 3mf failed!");
        return;
    }  

    result = obj->send_wan_print_subtask(&m_subtask);
    if (result < 0)
        wxMessageBox("Send Print Task Failed!");
    else
        wxMessageBox("Send Print Task Ok!");
    
}

void SubTaskPanel::on_thumbnail_enter(wxMouseEvent& event)
{
    if (!m_thumbnail_img.IsOk()) return;
    m_thumbnail_popup = std::make_shared<ImageTransientPopup>(this, false, m_thumbnail_img);
    wxWindow *ctrl = (wxWindow*) event.GetEventObject();
    wxPoint pos = ctrl->ClientToScreen( wxPoint(0,0) );
    wxSize sz = ctrl->GetSize();
    m_thumbnail_popup->Position( pos, sz );
    m_thumbnail_popup->Popup();
}

void SubTaskPanel::on_thumbnail_leave(wxMouseEvent& event)
{
    if (m_thumbnail_popup) {
        m_thumbnail_popup->Hide();
    }
}

void SubTaskPanel::on_mouse_enter(wxMouseEvent& event)
{
    ;
}

void SubTaskPanel::on_mouse_leave(wxMouseEvent& event)
{
    ;
}


void SubTaskPanel::set_value(wxString name, wxString prediction, wxString weight, std::string thumbnail_url)
{
    m_staticText_subtask_name->SetLabelText(name);
    m_staticText_prediction_value->SetLabelText(prediction);
    m_staticText_weight_value->SetLabelText(weight);

    if (web_request.IsOk()) web_request.Cancel();

    if (!thumbnail_url.empty()) {
        web_request = wxWebSession::GetDefault().CreateRequest(this, thumbnail_url);
        web_request.Start();
    }

    this->Fit();
}

void SubTaskPanel::update_info(BBLSubTask subtask, BBLSliceInfo info)
{
    m_subtask = subtask;
    m_subtask.task_gcode_in_3mf = info.gcode_dir + "/" + info.gcode_name;
    m_subtask.task_url = info.gcode_url;
    wxString prediction = wxString::Format("%s", get_bbl_time_dhms(info.prediction));
    wxString weight = wxString::Format("%sg", info.weight);
    this->set_value(subtask.task_name, prediction, weight, info.thumbnail_url);
}


void SubTaskPanel::on_webrequest_state(wxWebRequestEvent& evt)
{
    switch (evt.GetState())
    {
    case wxWebRequest::State_Completed:
        {
            m_thumbnail_img = *evt.GetResponse().GetStream();
            wxImage resize_img = m_thumbnail_img.Scale(m_bitmap->GetSize().x, m_bitmap->GetSize().y);
            m_bitmap->SetBitmap(resize_img);
            break;
        }
    case wxWebRequest::State_Failed:
        {
        break;
        }
    case wxWebRequest::State_Cancelled:
    case wxWebRequest::State_Unauthorized:
    case wxWebRequest::State_Active:
    case wxWebRequest::State_Idle:
        break;
    default:
        break;
    }
}


MonitorPanel::MonitorPanel(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : MonitorBasePanel(parent, id, pos, size, style)
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__

    init_bitmap();
    /* set bitmap image, set font */
    // TODO add translations for m_staticText_current_unit and m_staticText_target_unit    

    m_staticText_status->SetFont(Label::Head_14);
    m_staticText_subtask_list_title->SetFont(Label::Head_14);
    m_staticText_notification->SetFont(Label::Head_14);
    m_staticText_live->SetFont(Label::Head_16);
    m_staticText_timelapse->SetFont(Label::Head_16);
    m_staticText_task_caption->SetFont(Label::Head_16);
    m_staticText_subtask_value->SetFont(Label::Head_14);
    m_staticText_subtask_progress->SetFont(Label::Head_14);

    m_staticText_temp_caption->SetFont(Label::Head_12);
    m_staticText_ctrl_caption->SetFont(Label::Head_12);
    m_staticText_extruder_ctrl_caption->SetFont(Label::Head_12);
    m_staticText_other_caption->SetFont(Label::Head_12);

    m_bpButton_y_up->SetBitmap(m_ctrl_up);
    m_bpButton_y_down->SetBitmap(m_ctrl_down);
    m_bpButton_xy_home->SetBitmap(m_ctrl_home);
    m_bpButton_x_left->SetBitmap(m_ctrl_left);
    m_bpButton_x_right->SetBitmap(m_ctrl_right);
    m_bpButton_printer->SetBitmap(m_printer_img);
    m_bpButton_z_up->SetBitmap(m_ctrl_up);
    m_bpButton_z_home->SetBitmap(m_ctrl_home);
    m_bpButton_z_down->SetBitmap(m_ctrl_down);
    m_bitmap_thumbnail->SetBitmap(m_thumbnail_placeholder);

    m_bitmap_fan_printing->SetBitmap(m_fan_img);
    m_bitmap_fan_case->SetBitmap(m_fan_img);
    m_bitmap_fan_nozzle->SetBitmap(m_fan_img);
    m_bitmap_fan_big->SetBitmap(m_fan_img);
    m_bitmap_bed->SetBitmap(m_bed_img);
    m_bitmap_nozzle->SetBitmap(m_nozzle_img);
    m_bitmap_pocket->SetBitmap(m_pocket_img);
    //m_bitmap_live_default->SetBitmap(m_live_default_img);
    m_bitmap_thumbnail->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent &e)
    {
        if (obj == NULL) {
            return;
        }
        wxGetApp()
            .getAccountManager()
            ->get_camera_url(obj->dev_id, [this](std::string url) {
                BOOST_LOG_TRIVIAL(info) << "camera_url: " << url;
                if (!url.empty()) m_media_ctrl->Load(wxURI(url));
            });
    });

    /* set default values */
    set_toggle_widget_on(m_button_1_0);
    m_bmToggleBtn_printing_fan->SetValue(false);
    m_bmToggleBtn_nozzle_fan->SetValue(false);

    /* set default enable state */
    m_button_pause_resume->Disable();
    m_button_abort->Disable();

    init_timer();
    init_bind();

    obj = nullptr;
    m_ctrl_unit = 0.1f;

#ifdef BBL_INTERNAL_TEST
    m_button_report->SetFont(Label::Head_12);
#endif


    // Connect Events
    m_panel_machine_status_title->Connect( wxEVT_LEFT_DCLICK, wxMouseEventHandler( MonitorPanel::on_status_click ), NULL, this );
	m_staticText_status->Connect( wxEVT_LEFT_DCLICK, wxMouseEventHandler( MonitorPanel::on_status_click ), NULL, this );
	m_panel_tasklist_title->Connect( wxEVT_LEFT_DCLICK, wxMouseEventHandler( MonitorPanel::on_tasklist_click ), NULL, this );
	m_staticText_subtask_list_title->Connect( wxEVT_LEFT_DCLICK, wxMouseEventHandler( MonitorPanel::on_tasklist_click ), NULL, this );
	m_panel_notification->Connect( wxEVT_LEFT_DCLICK, wxMouseEventHandler( MonitorPanel::on_notification_click ), NULL, this );
	m_staticText_notification->Connect( wxEVT_LEFT_DCLICK, wxMouseEventHandler( MonitorPanel::on_notification_click ), NULL, this );
    m_button_report->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( MonitorPanel::on_subtask_report ), NULL, this );
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
    m_textCtrl_bed->Connect(wxEVT_TEXT_ENTER, wxCommandEventHandler(MonitorPanel::on_set_bed_temp), NULL, this);
    m_textCtrl_bed->Connect( wxEVT_KILL_FOCUS, wxFocusEventHandler( MonitorPanel::on_bed_temp_kill_focus ), NULL, this );
	m_textCtrl_bed->Connect( wxEVT_SET_FOCUS, wxFocusEventHandler( MonitorPanel::on_bed_temp_set_focus ), NULL, this );
    m_textCtrl_nozzle->Connect(wxEVT_TEXT_ENTER, wxCommandEventHandler(MonitorPanel::on_set_nozzle_temp), NULL, this);
    m_textCtrl_nozzle->Connect( wxEVT_KILL_FOCUS, wxFocusEventHandler( MonitorPanel::on_nozzle_temp_kill_focus ), NULL, this );
	m_textCtrl_nozzle->Connect( wxEVT_SET_FOCUS, wxFocusEventHandler( MonitorPanel::on_nozzle_temp_set_focus ), NULL, this );
    m_button_extreder_feed->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_extruder_feed), NULL, this);
    m_button_extruder_back->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_extruder_back), NULL, this);
    m_button_extruder_in->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_extruder_extrude), NULL, this);
    m_button_extruder_out->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_extruder_retraction), NULL, this);
    m_bmToggleBtn_printing_fan->Connect( wxEVT_TOGGLEBUTTON, wxCommandEventHandler( MonitorPanel::on_printing_fan_switch ), NULL, this );
	m_bmToggleBtn_nozzle_fan->Connect( wxEVT_TOGGLEBUTTON, wxCommandEventHandler( MonitorPanel::on_nozzle_fan_switch ), NULL, this );

    Bind(wxEVT_SIZE, &MonitorPanel::on_size, this);
}

void MonitorPanel::on_size(wxSizeEvent& event)
{
    Layout();
    Refresh();
}

MonitorPanel::~MonitorPanel()
{
    if (m_refresh_timer)
        m_refresh_timer->Stop();

    // Disconnect Events
    m_panel_machine_status_title->Disconnect( wxEVT_LEFT_DCLICK, wxMouseEventHandler( MonitorPanel::on_status_click ), NULL, this );
	m_staticText_status->Disconnect( wxEVT_LEFT_DCLICK, wxMouseEventHandler( MonitorPanel::on_status_click ), NULL, this );
	m_panel_tasklist_title->Disconnect( wxEVT_LEFT_DCLICK, wxMouseEventHandler( MonitorPanel::on_tasklist_click ), NULL, this );
	m_staticText_subtask_list_title->Disconnect( wxEVT_LEFT_DCLICK, wxMouseEventHandler( MonitorPanel::on_tasklist_click ), NULL, this );
	m_panel_notification->Disconnect( wxEVT_LEFT_DCLICK, wxMouseEventHandler( MonitorPanel::on_notification_click ), NULL, this );
	m_staticText_notification->Disconnect( wxEVT_LEFT_DCLICK, wxMouseEventHandler( MonitorPanel::on_notification_click ), NULL, this );
    m_button_report->Disconnect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( MonitorPanel::on_subtask_report ), NULL, this );
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
    m_textCtrl_bed->Disconnect(wxEVT_COMMAND_TEXT_ENTER, wxCommandEventHandler(MonitorPanel::on_set_bed_temp), NULL, this);
    m_textCtrl_bed->Disconnect( wxEVT_KILL_FOCUS, wxFocusEventHandler( MonitorPanel::on_bed_temp_kill_focus ), NULL, this );
	m_textCtrl_bed->Disconnect( wxEVT_SET_FOCUS, wxFocusEventHandler( MonitorPanel::on_bed_temp_set_focus ), NULL, this );
    m_textCtrl_nozzle->Disconnect(wxEVT_COMMAND_TEXT_ENTER, wxCommandEventHandler(MonitorPanel::on_set_nozzle_temp), NULL, this);
    m_textCtrl_nozzle->Disconnect( wxEVT_KILL_FOCUS, wxFocusEventHandler( MonitorPanel::on_nozzle_temp_kill_focus ), NULL, this );
	m_textCtrl_nozzle->Disconnect( wxEVT_SET_FOCUS, wxFocusEventHandler( MonitorPanel::on_nozzle_temp_set_focus ), NULL, this );
    m_button_extreder_feed->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_extruder_feed), NULL, this);
    m_button_extruder_back->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_extruder_back), NULL, this);
    m_button_extruder_in->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_extruder_extrude), NULL, this);
    m_button_extruder_out->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MonitorPanel::on_extruder_retraction), NULL, this);
    m_bmToggleBtn_printing_fan->Disconnect( wxEVT_TOGGLEBUTTON, wxCommandEventHandler( MonitorPanel::on_printing_fan_switch ), NULL, this );
	m_bmToggleBtn_nozzle_fan->Disconnect( wxEVT_TOGGLEBUTTON, wxCommandEventHandler( MonitorPanel::on_nozzle_fan_switch ), NULL, this );
}

void MonitorPanel::on_status_click(wxMouseEvent& event)
{
    m_status_packup = !m_status_packup;
}

void MonitorPanel::on_tasklist_click(wxMouseEvent& event)
{
    m_tasklist_packup = !m_tasklist_packup;
}

void MonitorPanel::on_notification_click(wxMouseEvent& event)
{
    m_notification_packup = !m_notification_packup;

}

void MonitorPanel::init_bitmap()
{
    int bitmap_size = 40;
    int bitmap_temp_size = 24;
    m_ctrl_up = create_scaled_bitmap("axis_ctrl_up", nullptr, bitmap_size);
    m_ctrl_down = create_scaled_bitmap("axis_ctrl_down", nullptr, bitmap_size);
    m_ctrl_left = create_scaled_bitmap("axis_ctrl_left", nullptr, bitmap_size);
    m_ctrl_right = create_scaled_bitmap("axis_ctrl_right", nullptr, bitmap_size);
    m_ctrl_home = create_scaled_bitmap("axis_ctrl_home", nullptr, bitmap_size);
    m_thumbnail_placeholder = create_scaled_bitmap("monitor_placeholder", this, 160);

    m_signal_strong_img = create_scaled_bitmap("monitor_signal_strong", nullptr, 20);
    m_signal_middle_img = create_scaled_bitmap("monitor_signal_middle", nullptr, 20);
    m_signal_weak_img = create_scaled_bitmap("monitor_signal_weak", nullptr, 20);
    m_fan_img = create_scaled_bitmap("monitor_fan", nullptr, 13);
    m_bed_img = create_scaled_bitmap("monitor_bed_temp", nullptr, bitmap_temp_size);
    m_nozzle_img = create_scaled_bitmap("monitor_nozzle_temp", nullptr, bitmap_temp_size);
    m_pocket_img = create_scaled_bitmap("monitor_volume_temp", nullptr, bitmap_temp_size);
    m_printer_img = create_scaled_bitmap("monitor_printer", nullptr, bitmap_temp_size);
    m_live_default_img = create_scaled_bitmap("live_stream_default", nullptr, FromDIP(300));
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

    Bind(wxEVT_WEBREQUEST_STATE, &MonitorPanel::on_webrequest_state, this);

    Bind(wxEVT_COMMAND_CHOICE_SELECTED, &MonitorPanel::on_select, this);
}

/* web state */
void MonitorPanel::on_webrequest_state(wxWebRequestEvent& evt)
{
    switch (evt.GetState())
    {
    case wxWebRequest::State_Completed:
        {
            wxImage img(*evt.GetResponse().GetStream());
            img_list.insert(std::make_pair(m_request_url, img));
            wxImage resize_img = img.Scale(m_bitmap_thumbnail->GetSize().x, m_bitmap_thumbnail->GetSize().y);
            m_bitmap_thumbnail->SetBitmap(resize_img);
            break;
        }
    case wxWebRequest::State_Failed:
        {
        break;
        }
    case wxWebRequest::State_Cancelled:
    case wxWebRequest::State_Unauthorized:
    case wxWebRequest::State_Active:
    case wxWebRequest::State_Idle:
        break;
    default:
        break;
    }
}

bool MonitorPanel::Show(bool show)
{
    if (show) {
        m_refresh_timer->Stop();
        m_refresh_timer->SetOwner(this);
        m_refresh_timer->Start(REFRESH_INTERVAL);
        wxPostEvent(this, wxTimerEvent());
    }
    else {
        m_refresh_timer->Stop();
    }
    return wxPanel::Show(show);
}

void MonitorPanel::Reset()
{
    obj = nullptr;
    last_task = nullptr;
    last_profile = nullptr;
    last_subtask = nullptr;

    /* set default value */
    m_gauge_progress->SetValue(0);
    m_staticText_subtask_value->SetLabelText("N/A");
    m_staticText_subtask_progress->SetLabelText("N/A");
    m_staticText_progress_duration->SetLabelText("N/A");
    m_staticText_progress_left->SetLabelText("N/A");
    m_staticText_bed_current->SetLabelText("N/A");
    m_staticText_nozzle_current->SetLabelText("N/A");
    m_staticText_pocket_current->SetLabelText("N/A");
    m_textCtrl_bed->SetLabelText("0");
    m_textCtrl_nozzle->SetLabelText("0");
    m_bitmap_thumbnail->SetBitmap(m_thumbnail_placeholder);

    m_staticText_machine_name->SetLabelText("N/A");
    m_staticText_printing_val->SetLabelText("N/A");
    m_staticText_wifi_signal->SetLabelText("N/A");
    m_staticText_capacity_val->SetLabelText("N/A");

    m_panel_printing_content->Layout();

    /* default enable state */
    m_button_pause_resume->Disable();
    m_button_abort->Disable();

    /* reset task list */
    for (auto it = task_panels.begin(); it != task_panels.end();it++) {
        delete it->second;
    }
    task_panels.clear();

    wxBoxSizer* bSizer_tasklist = new wxBoxSizer(wxVERTICAL);
    m_scrolledWindow_tasklist->SetSizer(bSizer_tasklist);
    bSizer_tasklist->Fit(m_scrolledWindow_tasklist);
    m_scrolledWindow_tasklist->Layout();
}

void MonitorPanel::update_ams(MachineObject* obj)
{
    if (!obj) return;
    //TODO BBS update ams tray_model->update(obj);
}

void MonitorPanel::update_task(MachineObject* obj)
{
    if (!obj) return;

    if (!obj->task_) return;

    if (last_task != obj->task_) {
        std::vector<BBLSubTask*>::iterator it;
        std::map<std::string, BBLSliceInfo*>::iterator iter;
        
        for (auto it = task_panels.begin(); it != task_panels.end();it++) {
            delete it->second;
        }

        task_panels.clear();
        for (it = obj->task_->subtasks.begin(); it != obj->task_->subtasks.end(); it++) {
            iter = obj->task_->slice_info.find((*it)->task_partplate_idx);
            if (iter != obj->task_->slice_info.end()) {
                SubTaskPanel* panel = new SubTaskPanel(m_scrolledWindow_tasklist, wxID_ANY, wxDefaultPosition, wxSize(-1, 60));
                panel->update_info(*(*it), *(iter->second));
                panel->Layout();
                task_panels.insert(std::make_pair((*it)->task_partplate_idx, panel));
            }
        }

        wxBoxSizer* bSizer_tasklist = new wxBoxSizer(wxVERTICAL);
        for (auto it = task_panels.begin(); it != task_panels.end(); it++) {
            bSizer_tasklist->Add(it->second, 0, wxEXPAND | wxALL, 0);
            it->second->Fit();
            it->second->Layout();
        }
        m_scrolledWindow_tasklist->SetSizer(bSizer_tasklist);
        bSizer_tasklist->Fit(m_scrolledWindow_tasklist);
        m_scrolledWindow_tasklist->Layout();
    }
    last_task = obj->task_;
}

void MonitorPanel::update_subtask(MachineObject* obj)
{
    if (!obj) return;

    if (obj->print_status.compare("FAILED") == 0) {
        reset_printing_values();
        return;
    }

    if (!obj->subtask_) return;

    // update subtask static info
    if (last_subtask != obj->subtask_) {
        // update subtask name
        m_staticText_subtask_value->SetLabelText(wxString::Format("%s(%s)", obj->subtask_->task_name, obj->subtask_->task_id));
        if (web_request.IsOk())
            web_request.Cancel();
        m_start_loading_thumbnail = true;
    }
    last_subtask = obj->subtask_;

    if (m_start_loading_thumbnail) {
        if (obj->profile_) {
            std::map<std::string, BBLSliceInfo*>::iterator iter = obj->profile_->slice_info.find(obj->subtask_->task_partplate_idx);
            if (iter != obj->profile_->slice_info.end())
                m_request_url = wxString(iter->second->thumbnail_url);

            wxImage img;
            std::map<wxString, wxImage>::iterator it = img_list.find(m_request_url);
            if (it != img_list.end()) {
                img = it->second;
                wxImage resize_img = img.Scale(m_bitmap_thumbnail->GetSize().x, m_bitmap_thumbnail->GetSize().y);
                m_bitmap_thumbnail->SetBitmap(resize_img);
            }
            else {
                web_request = wxWebSession::GetDefault().CreateRequest(this, m_request_url);
                web_request.Start();
                m_start_loading_thumbnail = false;
            }
        }
    }

    // update gcode progress
    std::string duration = "N/A";
    std::string total_time = "N/A";
    std::string left_time = "N/A";
    try {
        if (!obj->subtask_->task_duration.empty()) {
            //duration = get_time_dhms(stoi(obj->subtask_->task_duration));
            duration = get_time_hms(stoi(obj->subtask_->task_duration));
        }
    }
    catch (...) {
        ;
    }
    wxString duration_text = wxString::Format("%s", duration);
    m_staticText_progress_duration->SetLabelText(duration_text);

    BBLSliceInfo* info = obj->get_slice_info(obj->subtask_->task_partplate_idx);
    if (info) {
        try {
            //total_time = get_time_dhms(info->prediction);
            left_time = get_time_hms(info->prediction * (100 - obj->subtask_->task_progress) / 100);
        }
        catch (...) {
            ;
        }
    }

    wxString left_time_text = wxString::Format("-%s", left_time);
    m_staticText_progress_left->SetLabelText(left_time_text);

    // update current subtask progress
    m_gauge_progress->SetValue(obj->subtask_->task_progress);
    wxString progress_text = wxString::Format("%d%%", obj->subtask_->task_progress);
    m_staticText_subtask_progress->SetLabelText(progress_text);

    m_panel_printing_content->Layout();
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
    wxString machine_name_text = wxString::Format("%s", obj->dev_name);
    m_staticText_machine_name->SetLabelText(machine_name_text);

    // update printing status
    wxString printing_status_text = wxString::Format("%s", obj->print_status);
    m_staticText_printing_val->SetLabelText(printing_status_text);

    // update wifi signal image
    int wifi_signal_val = 0;
    if (!obj->wifi_signal.empty() && boost::ends_with(obj->wifi_signal, "dBm")) {
        try {
            wifi_signal_val = std::stoi(obj->wifi_signal.substr(0, obj->wifi_signal.size() - 3));
        }
        catch (...) {
            ;
        }

        if (last_wifi_signal != wifi_signal_val) {
            if (wifi_signal_val > -45) {
                m_bitmap_signal->SetBitmap(m_signal_strong_img);
            }
            else if (wifi_signal_val <= -45 && wifi_signal_val >= -60) {
                m_bitmap_signal->SetBitmap(m_signal_middle_img);
            }
            else {
                m_bitmap_signal->SetBitmap(m_signal_weak_img);
            }
        }
        last_wifi_signal = wifi_signal_val;
    }
    else {
        m_bitmap_signal->SetBitmap(m_signal_weak_img);
    }

    // update wifi signal
    m_staticText_wifi_signal->SetLabelText(wxString::Format("%s", obj->wifi_signal));

    // update temprature if not input bed temp target
    if (!bed_temp_input) {
        wxString bed_temp_curr_text = wxString::Format("%0.2f", obj->bed_temp_target);
        m_textCtrl_bed->SetLabelText(bed_temp_curr_text);
    }

    if (!nozzle_temp_input) {
        wxString nozzle_temp_target_text = wxString::Format("%0.2f", obj->nozzle_temp_target);
        m_textCtrl_nozzle->SetLabelText(nozzle_temp_target_text);
    }
    
    wxString bed_temp_curr_text = wxString::Format("%0.2f", obj->bed_temp);
    m_staticText_bed_current->SetLabelText(bed_temp_curr_text);

    wxString nozzle_temp_curr_text = wxString::Format("%0.2f", obj->nozzle_temp);
    m_staticText_nozzle_current->SetLabelText(nozzle_temp_curr_text);

    wxString chamber_temp_curr_text = wxString::Format("%0.2f", obj->chamber_temp);
    m_staticText_pocket_current->SetLabelText(chamber_temp_curr_text);


    if (obj->can_abort()) {
        m_button_abort->Enable();
    }
    else {
        m_button_abort->Disable();
    }

    if (obj->can_pause() || obj->can_resume()) {
        m_button_pause_resume->Enable();
        if (obj->can_resume())
            m_button_pause_resume->SetLabelText("Resume");
        else
            m_button_pause_resume->SetLabelText("Pause");
    }
    else {
        m_button_pause_resume->Disable();
    }

    m_panel_machine_status_content->Layout();
}

void MonitorPanel::on_timer(wxTimerEvent& event)
{
    //Freeze();
    update_all();
    
    Layout();
    Refresh();
    //Thaw(); // will cause media ctrl period flush
}

void MonitorPanel::update_all()
{
    Slic3r::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();
    obj = account_manager->get_default_machine();
    if (!obj) return;

    update_status(obj);

    update_ams(obj);

    update_subtask(obj);

    update_task(obj);
}

void MonitorPanel::reset_printing_values()
{
    m_gauge_progress->SetValue(0);
    m_staticText_subtask_value->SetLabelText("N/A");
    m_staticText_subtask_progress->SetLabelText("N/A");
    m_staticText_progress_duration->SetLabelText("N/A");
    m_staticText_progress_left->SetLabelText("N/A");
    m_bitmap_thumbnail->SetBitmap(m_thumbnail_placeholder);
    m_panel_printing_content->Layout();
}

void MonitorPanel::on_select(wxCommandEvent& event)
{
    Reset();

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

void MonitorPanel::on_subtask_status_changed(std::string old_status, std::string new_status)
{
    ;//TODO
}

void MonitorPanel::on_subtask_start(wxCommandEvent& event)
{
    ;//TODO
}

void MonitorPanel::on_subtask_report(wxCommandEvent& event)
{
    if (obj) {
        if (!obj->subtask_) return;
        wxString report_url = wxString::Format("https://autotest.bambu-lab.com/testReports/add?taskId=%s", obj->subtask_->task_id);
        wxLaunchDefaultBrowser(report_url);
    }
}

void MonitorPanel::on_subtask_pause_resume(wxCommandEvent& event)
{
    if (obj) {
        if (obj->can_resume())
            obj->command_task_resume();
        else
            obj->command_task_pause();
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
    // TODO Check Valid Value
    try {
        double temp;
        m_textCtrl_bed->GetValue().ToDouble(&temp);
        if (obj)
            obj->command_set_bed(temp);
    }
    catch(...) {
        ;
    }
}

void MonitorPanel::on_set_nozzle_temp(wxCommandEvent& event)
{
    // TODO Check Valid Value
    try {
        double temp;
        m_textCtrl_nozzle->GetValue().ToDouble(&temp);
        if (obj)
            obj->command_set_nozzle(temp);
    }
    catch(...) {
        ;
    }
}

void MonitorPanel::on_bed_temp_kill_focus(wxFocusEvent& event)
{
    bed_temp_input = false;
}

void MonitorPanel::on_bed_temp_set_focus(wxFocusEvent& event)
{
    bed_temp_input = true;
}

void MonitorPanel::on_nozzle_temp_kill_focus(wxFocusEvent& event)
{
    nozzle_temp_input = false;
}

void MonitorPanel::on_nozzle_temp_set_focus(wxFocusEvent& event)
{
    nozzle_temp_input = true;
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

void MonitorPanel::on_printing_fan_switch(wxCommandEvent& event)
{
    if (!obj) return;
    
    bool value = m_bmToggleBtn_printing_fan->GetValue();

    if (value)
    {
        obj->publish_gcode("M106 S255 \n");
        m_bmToggleBtn_printing_fan->SetValue(true);
    }
    else
    {
        obj->command_fan_off();
        m_bmToggleBtn_printing_fan->SetValue(false);
    }
}

void MonitorPanel::on_nozzle_fan_switch(wxCommandEvent& event)
{
    if (!obj) return;
    
    bool value = m_bmToggleBtn_nozzle_fan->GetValue();

    if (value)
    {
        //TODO send command nozzle fan on
        m_bmToggleBtn_nozzle_fan->SetValue(true);
    }
    else
    {
        //TODO send command nozzle fan off
        m_bmToggleBtn_nozzle_fan->SetValue(false);
    }
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

void MonitorPanel::on_printer_clicked(wxCommandEvent& event)
{
    SelectMachinePopup* m_select_machine = new SelectMachinePopup(this, true);

    wxRect rect = m_bpButton_printer->GetRect();
    wxPoint pos = m_bpButton_printer->ClientToScreen(wxPoint(0, 0));
    pos.y += rect.height;

    m_select_machine->Position(pos, wxSize(0, 0));
    m_select_machine->Popup();
}

void MonitorPanel::set_machine(std::string machine_sn)
{
    this->machine_sn = machine_sn;
    this->Reset();
}


} // GUI
} // Slic3r
