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
#include "MediaPlayCtrl.h"
#include "MediaFilePanel.h"

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
            add_item(GUI::from_u8((*it)->task_name), iter->second->prediction, iter->second->weight);
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

void SubTaskListModel::add_item(wxString title, int prediction, std::string weight)
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
    this->SetBackgroundColour(wxColour(246, 246, 246));

    wxBoxSizer* bSizer_top;
    bSizer_top = new wxBoxSizer(wxHORIZONTAL);

    bSizer_top->SetMinSize(wxSize(339, 125));
    m_bitmap_subtask = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(70, 70), 0);
    bSizer_top->Add(m_bitmap_subtask, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);

    m_bitmap_prediction = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);

    bSizer_top->Add(m_bitmap_prediction, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);

    m_staticText_prediction_value = new wxStaticText(this, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_prediction_value->Wrap(-1);
    m_staticText_prediction_value->SetMinSize(wxSize(60, -1));

    bSizer_top->Add(m_staticText_prediction_value, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);


    bSizer_top->Add(17, 0, 0, wxEXPAND, 0);

    m_bitmap_weight = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);

    bSizer_top->Add(m_bitmap_weight, 0, wxALL | wxALIGN_CENTER_VERTICAL, 0);

    m_staticText_weight_value = new wxStaticText(this, wxID_ANY, wxT("N/A"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_weight_value->Wrap(-1);
    m_staticText_weight_value->SetMinSize(wxSize(60, -1));

    bSizer_top->Add(m_staticText_weight_value, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);


    bSizer_top->Add(63, 0, 0, wxEXPAND, 0);

    m_bpButton_print = new wxBitmapButton(this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW | 0);

    bSizer_top->Add(m_bpButton_print, 0, wxALL | wxALIGN_CENTER_VERTICAL, 0);


    bSizer_top->Add(37, 0, 0, wxEXPAND, 0);


    this->SetSizer(bSizer_top);
    this->Layout();
    bSizer_top->Fit(this);


    time_bmp = create_scaled_bitmap("monitor_tasklist_time", nullptr, FromDIP(15));
    weight_bmp = create_scaled_bitmap("monitor_tasklist_weight", nullptr, FromDIP(15));
    printing_bmp = create_scaled_bitmap("monitor_tasklist_print", nullptr, FromDIP(15));
    m_bitmap_prediction->SetBitmap(time_bmp);
    m_bitmap_weight->SetBitmap(weight_bmp);
    m_bpButton_print->SetBitmap(printing_bmp);

    Bind(wxEVT_WEBREQUEST_STATE, &SubTaskPanel::on_webrequest_state, this);

    // Connect Events
    m_bitmap_subtask->Connect( wxEVT_ENTER_WINDOW, wxMouseEventHandler( SubTaskPanel::on_thumbnail_enter ), NULL, this );
    m_bitmap_subtask->Connect( wxEVT_LEAVE_WINDOW, wxMouseEventHandler( SubTaskPanel::on_thumbnail_leave ), NULL, this );
    m_bpButton_print->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( SubTaskPanel::on_subtask_print ), NULL, this );
}

SubTaskPanel::~SubTaskPanel()
{
    // Disconnect Events
    m_bitmap_subtask->Disconnect( wxEVT_ENTER_WINDOW, wxMouseEventHandler( SubTaskPanel::on_thumbnail_enter ), NULL, this );
    m_bitmap_subtask->Disconnect( wxEVT_LEAVE_WINDOW, wxMouseEventHandler( SubTaskPanel::on_thumbnail_leave ), NULL, this );
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

void SubTaskPanel::update_info(BBLSubTask subtask, BBLSliceInfo info)
{
    m_subtask = subtask;
    m_subtask.task_gcode_in_3mf = info.gcode_dir + "/" + info.gcode_name;
    m_subtask.task_url = info.gcode_url;
    wxString prediction = wxString::Format("%s", get_bbl_time_dhms(info.prediction));
    wxString weight = wxString::Format("%sg", info.weight);
    this->set_value(GUI::from_u8(subtask.task_name), prediction, weight, info.thumbnail_url);
}

void SubTaskPanel::set_value(wxString name, wxString prediction, wxString weight, std::string thumbnail_url)
{
    m_staticText_prediction_value->SetLabelText(prediction);
    m_staticText_weight_value->SetLabelText(weight);

    if (web_request.IsOk()) web_request.Cancel();

    if (!thumbnail_url.empty()) {
        web_request = wxWebSession::GetDefault().CreateRequest(this, thumbnail_url);
        BOOST_LOG_TRIVIAL(trace) << "monitor: subtask_panel start reqeust thumbnail, url = " << thumbnail_url;
        web_request.Start();
    }

    this->Fit();
}

void SubTaskPanel::on_webrequest_state(wxWebRequestEvent& evt)
{
    BOOST_LOG_TRIVIAL(trace) << "monitor: sub_task_panel web request state = " << evt.GetState();
    switch (evt.GetState())
    {
    case wxWebRequest::State_Completed:
        {
            m_thumbnail_img = *evt.GetResponse().GetStream();
            wxImage resize_img = m_thumbnail_img.Scale(m_bitmap_subtask->GetSize().x, m_bitmap_subtask->GetSize().y);
            m_bitmap_subtask->SetBitmap(resize_img);
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

TaskListPanel::TaskListPanel(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style, const wxString& name)
    : TaskListBasePanel(parent, id, pos, size, style)
{
    m_bitmap_task->SetBitmap(create_scaled_bitmap("tasklist_default",nullptr,FromDIP(80)));

    obj = nullptr;

    Bind(wxEVT_WEBREQUEST_STATE, &TaskListPanel::on_webrequest_state, this);
}

TaskListPanel::~TaskListPanel(){}

void TaskListPanel::msw_rescale() {
    m_bitmap_task->SetBitmap(wxBitmap(m_bitmap_task->GetBitmap().ConvertToImage().Scale(FromDIP(70), FromDIP(70))));
    //TODO:rescale subtaskpanel

    Layout();
    Refresh();
}

void TaskListPanel::update_tasklist(MachineObject* obj)
{
    if (!obj) return;

    if (!obj->task_) return;

    if (last_task != obj->task_) {
        BOOST_LOG_TRIVIAL(trace) << "monitor: change to task id = " << obj->task_->task_id;
        std::vector<BBLSubTask*>::iterator it;
        std::map<std::string, BBLSliceInfo*>::iterator iter;

        for (auto it = task_panels.begin(); it != task_panels.end(); it++) {
            delete it->second;
        }

        task_panels.clear();
        for (it = obj->task_->subtasks.begin(); it != obj->task_->subtasks.end(); it++) {
            iter = obj->task_->slice_info.find((*it)->task_partplate_idx);
            if (iter != obj->task_->slice_info.end()) {
                SubTaskPanel* panel = new SubTaskPanel(m_panel_plater_content, wxID_ANY, wxDefaultPosition, wxSize(-1, 60));
                panel->update_info(*(*it), *(iter->second));
                panel->Layout();
                task_panels.insert(std::make_pair((*it)->task_partplate_idx, panel));
            }
        }

        fgSizer_subtask->Add(0, 29, 0, wxEXPAND, 0);
        for (auto it = task_panels.begin(); it != task_panels.end(); it++) {
            fgSizer_subtask->Add(it->second, 0, wxALL | wxALIGN_CENTER_VERTICAL | wxALIGN_CENTER_HORIZONTAL, 0);
        }
        m_panel_plater_content->Layout();
        this->Layout();
    }
    last_task = obj->task_;

    //TODO
    //update thumbnail
}

void TaskListPanel::update_profile(MachineObject* obj)
{
    if (!obj) return;
    BBLProfile* profile = obj->profile_;
    if (last_profile != profile) {
        subtask_model->update_profile(profile);
    }
    last_profile = profile;
}

void TaskListPanel::on_webrequest_state(wxWebRequestEvent& evt)
{
    BOOST_LOG_TRIVIAL(trace) << "monitor: task_list_panel web request state = " << evt.GetState();
    switch (evt.GetState())
    {
    case wxWebRequest::State_Completed:
    {
        m_thumbnail_img = *evt.GetResponse().GetStream();
        wxImage resize_img = m_thumbnail_img.Scale(m_bitmap_task->GetSize().x, m_bitmap_task->GetSize().y);
        m_bitmap_task->SetBitmap(resize_img);
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

VideoPanel::VideoPanel(wxWindow* parent, wxWindowID id , const wxPoint& pos , const wxSize& size , long style , const wxString& name)
    : VideoMonitoringBasePanel(parent, id, pos, size, style)
{

}

VideoPanel::~VideoPanel(){}

StatusPanel::StatusPanel(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style, const wxString& name)
    : StatusBasePanel(parent, id, pos, size, style)
{
    init_scaled_bitmap();
    m_thumbnail_placeholder = create_scaled_bitmap("monitor_placeholder", nullptr, (170));
    m_bitmap_thumbnail->SetBitmap(m_thumbnail_placeholder);
    init_scaled_buttons();

    m_buttons.push_back(m_button_report);
    m_buttons.push_back(m_button_pause_resume);
    m_buttons.push_back(m_button_abort);
    m_buttons.push_back(m_bpButton_home_x);
    m_buttons.push_back(m_bpButton_home_y);
    m_buttons.push_back(m_bpButton_home_z);
    m_buttons.push_back(m_bpButton_home);
    m_buttons.push_back(m_bpButton_z_10);
    m_buttons.push_back(m_bpButton_z_1);
    m_buttons.push_back(m_bpButton_z_0_1);
    m_buttons.push_back(m_bpButton_z_down_0_1);
    m_buttons.push_back(m_bpButton_z_down_1);
    m_buttons.push_back(m_bpButton_z_down_10);
    m_buttons.push_back(m_bpButton_e_10);
    m_buttons.push_back(m_bpButton_e_1);
    m_buttons.push_back(m_bpButton_e_down_1);
    m_buttons.push_back(m_bpButton_e_down_10);
    m_buttons.push_back(m_bpButton_extruder_1);
    m_buttons.push_back(m_bpButton_extruder_2);
    m_buttons.push_back(m_bpButton_extruder_3);
    m_buttons.push_back(m_bpButton_extruder_4);
    m_buttons.push_back(m_button_extruder_feed);
    m_buttons.push_back(m_button_extruder_back);

    obj = nullptr;

    /* set default values */
    m_bmToggleBtn_lamp->SetValue(false);
    m_bmToggleBtn_printing_fan->SetValue(false);
    m_bmToggleBtn_nozzle_fan->SetValue(false);

    /* set default enable state */
    m_button_pause_resume->Enable(false);
    m_button_abort->Enable(false);

    Bind(wxEVT_WEBREQUEST_STATE, &StatusPanel::on_webrequest_state, this);
   
    // Connect Events
    m_button_report->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_subtask_report), NULL, this);
    m_button_pause_resume->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_subtask_pause_resume), NULL, this);
    m_button_abort->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_subtask_abort), NULL, this);
    m_textCtrl_bed->Connect(wxEVT_COMMAND_TEXT_ENTER, wxCommandEventHandler(StatusPanel::on_set_bed_temp), NULL, this);
    m_textCtrl_bed->Connect(wxEVT_KILL_FOCUS, wxFocusEventHandler(StatusPanel::on_bed_temp_kill_focus), NULL, this);
    m_textCtrl_bed->Connect(wxEVT_SET_FOCUS, wxFocusEventHandler(StatusPanel::on_bed_temp_set_focus), NULL, this);
    m_textCtrl_nozzle->Connect(wxEVT_COMMAND_TEXT_ENTER, wxCommandEventHandler(StatusPanel::on_set_nozzle_temp), NULL, this);
    m_textCtrl_nozzle->Connect(wxEVT_KILL_FOCUS, wxFocusEventHandler(StatusPanel::on_nozzle_temp_kill_focus), NULL, this);
    m_textCtrl_nozzle->Connect(wxEVT_SET_FOCUS, wxFocusEventHandler(StatusPanel::on_nozzle_temp_set_focus), NULL, this);
    m_bmToggleBtn_lamp->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_lamp_switch), NULL, this);//TODO
    m_bmToggleBtn_nozzle_fan->Connect(wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_nozzle_fan_switch), NULL, this);//TODO
    m_bmToggleBtn_printing_fan->Connect(wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_printing_fan_switch), NULL, this);
    m_bpButton_home_x->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_x_home), NULL, this);//TODO
    m_bpButton_xy->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_xy), NULL, this);//TODO
    m_bpButton_home_y->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_y_home), NULL, this);//TODO
    m_bpButton_home_z->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_z_home), NULL, this);//TODO
    m_bpButton_home->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_home), NULL, this);//TODO
    m_bpButton_z_10->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_z_up_10), NULL, this);
    m_bpButton_z_1->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_z_up_1), NULL, this);
    m_bpButton_z_0_1->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_z_up_0_1), NULL, this);
    m_bpButton_z_down_0_1->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_z_down_0_1), NULL, this);
    m_bpButton_z_down_1->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_z_down_1), NULL, this);
    m_bpButton_z_down_10->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_z_down_10), NULL, this);
    m_bpButton_e_10->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_e_up_10), NULL, this);
    m_bpButton_e_1->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_e_up_1), NULL, this);
    m_bpButton_e_down_1->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_e_down_1), NULL, this);
    m_bpButton_e_down_10->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_e_down_10), NULL, this);
    m_bpButton_extruder_1->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_select_space_1), NULL, this);//TODO
    m_bpButton_extruder_2->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_select_space_2), NULL, this);//TODO
    m_bpButton_extruder_3->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_select_space_3), NULL, this);//TODO
    m_bpButton_extruder_4->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_select_space_4), NULL, this);//TODO
    m_button_extruder_feed->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_extruder_feed), NULL, this);//TODO
    m_button_extruder_back->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_extruder_back), NULL, this);//TODO

}

StatusPanel::~StatusPanel(){
    // Disconnect Events
    m_button_report->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_subtask_report), NULL, this);
    m_button_pause_resume->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_subtask_pause_resume), NULL, this);
    m_button_abort->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_subtask_abort), NULL, this);
    m_textCtrl_bed->Disconnect(wxEVT_COMMAND_TEXT_ENTER, wxCommandEventHandler(StatusPanel::on_set_bed_temp), NULL, this);
    m_textCtrl_bed->Disconnect(wxEVT_KILL_FOCUS, wxFocusEventHandler(StatusPanel::on_bed_temp_kill_focus), NULL, this);
    m_textCtrl_bed->Disconnect(wxEVT_SET_FOCUS, wxFocusEventHandler(StatusPanel::on_bed_temp_set_focus), NULL, this);
    m_textCtrl_nozzle->Disconnect(wxEVT_COMMAND_TEXT_ENTER, wxCommandEventHandler(StatusPanel::on_set_nozzle_temp), NULL, this);
    m_textCtrl_nozzle->Disconnect(wxEVT_KILL_FOCUS, wxFocusEventHandler(StatusPanel::on_nozzle_temp_kill_focus), NULL, this);
    m_textCtrl_nozzle->Disconnect(wxEVT_SET_FOCUS, wxFocusEventHandler(StatusPanel::on_nozzle_temp_set_focus), NULL, this);
    m_bmToggleBtn_lamp->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_lamp_switch), NULL, this);
    m_bmToggleBtn_nozzle_fan->Disconnect(wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_nozzle_fan_switch), NULL, this);
    m_bmToggleBtn_printing_fan->Disconnect(wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_printing_fan_switch), NULL, this);
    m_bpButton_home_x->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_x_home), NULL, this);
    m_bpButton_xy->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_xy), NULL, this);
    m_bpButton_home_y->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_y_home), NULL, this);
    m_bpButton_home_z->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_z_home), NULL, this);
    m_bpButton_home->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_home), NULL, this);
    m_bpButton_z_10->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_z_up_10), NULL, this);
    m_bpButton_z_1->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_z_up_1), NULL, this);
    m_bpButton_z_0_1->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_z_up_0_1), NULL, this);
    m_bpButton_z_down_0_1->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_z_down_0_1), NULL, this);
    m_bpButton_z_down_1->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_z_down_1), NULL, this);
    m_bpButton_z_down_10->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_z_down_10), NULL, this);
    m_bpButton_e_10->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_e_up_10), NULL, this);
    m_bpButton_e_1->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_e_up_1), NULL, this);
    m_bpButton_e_down_1->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_e_down_1), NULL, this);
    m_bpButton_e_down_10->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_axis_ctrl_e_down_10), NULL, this);
    m_bpButton_extruder_1->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_select_space_1), NULL, this);
    m_bpButton_extruder_2->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_select_space_2), NULL, this);
    m_bpButton_extruder_3->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_select_space_3), NULL, this);
    m_bpButton_extruder_4->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_select_space_4), NULL, this);
    m_button_extruder_feed->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_extruder_feed), NULL, this);
    m_button_extruder_back->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(StatusPanel::on_extruder_back), NULL, this);
}

void StatusPanel::init_scaled_bitmap() {
    m_bitmap_lamp->SetBitmap(create_scaled_bitmap("monitor_lamp", nullptr, FromDIP(18)));
    m_bitmap_bed->SetBitmap(create_scaled_bitmap("monitor_bed_temp", nullptr, FromDIP(24)));
    m_bitmap_nozzle->SetBitmap(create_scaled_bitmap("monitor_nozzle_temp", nullptr, FromDIP(24)));
    m_bitmap_pocket->SetBitmap(create_scaled_bitmap("monitor_volume_temp", nullptr, FromDIP(24)));
    m_bitmap_fan_nozzle->SetBitmap(create_scaled_bitmap("monitor_fan", nullptr, FromDIP(18)));
    m_bitmap_fan_printing->SetBitmap(create_scaled_bitmap("monitor_fan", nullptr, FromDIP(18)));
    m_bitmap_fan_big->SetBitmap(create_scaled_bitmap("monitor_fan", nullptr, FromDIP(18)));
    m_bitmap_fan_case->SetBitmap(create_scaled_bitmap("monitor_fan", nullptr, FromDIP(18)));
}

void StatusPanel::init_scaled_buttons() {
    m_button_report->SetMinSize(wxSize(FromDIP(48), FromDIP(24)));
    m_button_report->SetCornerRadius(FromDIP(12));
    m_button_pause_resume->SetMinSize(wxSize(FromDIP(48), FromDIP(24)));
    m_button_pause_resume->SetCornerRadius(FromDIP(12));
    m_button_abort->SetMinSize(wxSize(FromDIP(48), FromDIP(24)));
    m_button_abort->SetCornerRadius(FromDIP(12));
    m_bpButton_home_x->SetMinSize(wxSize(FromDIP(48), FromDIP(48)));
    m_bpButton_home_x->SetCornerRadius(FromDIP(24));
    m_bpButton_home_y->SetMinSize(wxSize(FromDIP(48), FromDIP(48)));
    m_bpButton_home_y->SetCornerRadius(FromDIP(24));
    m_bpButton_home_z->SetMinSize(wxSize(FromDIP(48), FromDIP(48)));
    m_bpButton_home_z->SetCornerRadius(FromDIP(24));
    m_bpButton_home->SetMinSize(wxSize(FromDIP(48), FromDIP(48)));
    m_bpButton_home->SetCornerRadius(FromDIP(24));
    m_bpButton_z_10->SetMinSize(wxSize(FromDIP(48), FromDIP(40)));
    m_bpButton_z_10->SetCornerRadius(0);
    m_bpButton_z_1->SetMinSize(wxSize(FromDIP(48), FromDIP(40)));
    m_bpButton_z_1->SetCornerRadius(0);
    m_bpButton_z_0_1->SetMinSize(wxSize(FromDIP(48), FromDIP(40)));
    m_bpButton_z_0_1->SetCornerRadius(0);
    m_bpButton_z_down_0_1->SetMinSize(wxSize(FromDIP(48), FromDIP(40)));
    m_bpButton_z_down_0_1->SetCornerRadius(0);
    m_bpButton_z_down_1->SetMinSize(wxSize(FromDIP(48), FromDIP(40)));
    m_bpButton_z_down_1->SetCornerRadius(0);
    m_bpButton_z_down_10->SetMinSize(wxSize(FromDIP(48), FromDIP(40)));
    m_bpButton_z_down_10->SetCornerRadius(0);
    m_bpButton_e_10->SetMinSize(wxSize(FromDIP(45), FromDIP(45)));
    m_bpButton_e_10->SetCornerRadius(FromDIP(5));
    m_bpButton_e_1->SetMinSize(wxSize(FromDIP(45), FromDIP(45)));
    m_bpButton_e_1->SetCornerRadius(FromDIP(5));
    m_bpButton_e_down_1->SetMinSize(wxSize(FromDIP(45), FromDIP(45)));
    m_bpButton_e_down_1->SetCornerRadius(FromDIP(5));
    m_bpButton_e_down_10->SetMinSize(wxSize(FromDIP(45), FromDIP(45)));
    m_bpButton_e_down_10->SetCornerRadius(FromDIP(5));
    m_bpButton_extruder_1->SetMinSize(wxSize(FromDIP(50), FromDIP(66)));
    m_bpButton_extruder_1->SetCornerRadius(0);
    m_bpButton_extruder_2->SetMinSize(wxSize(FromDIP(50), FromDIP(66)));
    m_bpButton_extruder_2->SetCornerRadius(0);
    m_bpButton_extruder_3->SetMinSize(wxSize(FromDIP(50), FromDIP(66)));
    m_bpButton_extruder_3->SetCornerRadius(0);
    m_bpButton_extruder_4->SetMinSize(wxSize(FromDIP(50), FromDIP(66)));
    m_bpButton_extruder_4->SetCornerRadius(0);
    m_button_extruder_feed->SetMinSize(wxSize(FromDIP(48), FromDIP(24)));;
    m_button_extruder_feed->SetCornerRadius(FromDIP(12));
    m_button_extruder_back->SetMinSize(wxSize(FromDIP(48), FromDIP(24)));;
    m_button_extruder_back->SetCornerRadius(FromDIP(12));

}

void StatusPanel::on_subtask_report(wxCommandEvent& event)
{
    if (obj) {
        if (!obj->subtask_) return;

        std::string last_report_url;
        AccountManager::get_machine_last_report_url(obj->dev_id, last_report_url);
        if (last_report_url.empty()) {
            wxMessageBox("There is no need to fill a report!");
        }
        else {
            wxLaunchDefaultBrowser(last_report_url);
        }
    }
}

void StatusPanel::on_subtask_pause_resume(wxCommandEvent& event)
{
    if (obj) {
        if (obj->can_resume())
            obj->command_task_resume();
        else
            obj->command_task_pause();
    }
}

void StatusPanel::on_subtask_abort(wxCommandEvent& event)
{
    if (obj)
        obj->command_task_abort();
}

void StatusPanel::on_webrequest_state(wxWebRequestEvent& evt)
{
    BOOST_LOG_TRIVIAL(trace) << "monitor: monitor_panel web request state = " << evt.GetState();
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

void StatusPanel::update_temp_ctrl(MachineObject* obj) 
{
    if (!obj) return;

    // update temprature if not input temp target
    if (!bed_temp_input) {
        wxString bed_temp_curr_text = wxString::Format("%-0.0f", obj->bed_temp_target);
        m_textCtrl_bed->GetTextCtrl()->SetLabelText(bed_temp_curr_text);
    }

    if (!nozzle_temp_input) {
        wxString nozzle_temp_target_text = wxString::Format("%0.0f", obj->nozzle_temp_target);
        m_textCtrl_nozzle->GetTextCtrl()->SetLabelText(nozzle_temp_target_text);
    }

    wxString bed_temp_curr_text = wxString::Format("%0.0f", obj->bed_temp);
    m_staticText_bed_current->SetLabelText(bed_temp_curr_text);

    wxString nozzle_temp_curr_text = wxString::Format("%0.0f", obj->nozzle_temp);
    m_staticText_nozzle_current->SetLabelText(nozzle_temp_curr_text);

    wxString chamber_temp_curr_text = wxString::Format("%0.0f", obj->chamber_temp);
    m_staticText_pocket_current->SetLabelText(chamber_temp_curr_text);

    //TODO: get big fan & case fan status from obj
    //wxString chamber_temp_curr_text = wxString::Format("%0.2f", obj->chamber_temp);
    //m_status_panel->m_staticText_big_fan_status->SetLabelText(wxT("On"));

    //wxString chamber_temp_curr_text = wxString::Format("%0.2f", obj->chamber_temp);
    //m_status_panel->m_staticText_case_fan_status->SetLabelText(wxT("Off"));
}

void StatusPanel::update_subtask(MachineObject* obj)
{
    if (!obj) return;

    //update button enable status
    if (obj->can_abort()) {
        m_button_abort->Enable();
    }
    else {
        m_button_abort->Enable(false);
    }

    if (obj->can_pause() || obj->can_resume()) {
        m_button_pause_resume->Enable();
        if (obj->can_resume())
            m_button_pause_resume->SetLabel("Resume");
        else
            m_button_pause_resume->SetLabel("Pause");
    }
    else {
        m_button_pause_resume->Enable(false);
    }


    if (obj->print_status.compare("FAILED") == 0) {
        reset_printing_values();
        return;
    }

    if (!obj->subtask_) return;

    // update subtask static info
    if (last_subtask != obj->subtask_) {
        BOOST_LOG_TRIVIAL(trace) << "monitor: change to sub task id = " << obj->subtask_->task_id;
        // update subtask name
        wxString subtask_text = wxString::Format("%s(%s)", GUI::from_u8(obj->subtask_->task_name), obj->subtask_->task_id);
        m_staticText_subtask_value->SetLabelText(subtask_text);
        if (web_request.IsOk())
            web_request.Cancel();
        m_start_loading_thumbnail = true;
    }
    last_subtask = obj->subtask_;

    if (m_start_loading_thumbnail) {
        if (obj->profile_) {
            std::map<std::string, BBLSliceInfo*>::iterator iter = obj->profile_->slice_info.find(obj->subtask_->task_partplate_idx);
            if (iter != obj->profile_->slice_info.end()) {
                m_request_url = wxString(iter->second->thumbnail_url);
                if (!m_request_url.IsEmpty()) {
                    wxImage img;
                    std::map<wxString, wxImage>::iterator it = img_list.find(m_request_url);
                    if (it != img_list.end()) {
                        img = it->second;
                        wxImage resize_img = img.Scale(m_bitmap_thumbnail->GetSize().x, m_bitmap_thumbnail->GetSize().y);
                        m_bitmap_thumbnail->SetBitmap(resize_img);
                    }
                    else {
                        web_request = wxWebSession::GetDefault().CreateRequest(this, m_request_url);
                        BOOST_LOG_TRIVIAL(trace) << "monitor: start reqeust thumbnail, url = " << m_request_url;
                        web_request.Start();
                        m_start_loading_thumbnail = false;
                    }
                }
            }
        }
    }

    // update gcode progress
    std::string duration = "N/A";
    std::string left_time = "N/A";

    wxString duration_text = "N/A";
    wxString left_time_text = "N/A";
    wxString progress_text = "N/A";

    // valid gcode percent / left time
    if (obj->mc_left_time != 0 || obj->mc_print_percent != 0) {
        try {
            if (!obj->subtask_->task_duration.empty()) {
                duration = get_bbl_monitor_time_dhm(stoi(obj->subtask_->task_duration));
            }
        }
        catch (...) {
            ;
        }
        duration_text = wxString::Format("%s", duration);
        try {
            left_time = get_bbl_monitor_time_dhm(obj->mc_left_time);
        }
        catch (...) {
            ;
        }
        left_time_text = wxString::Format("-%s", left_time);
        progress_text = wxString::Format("%d%%", obj->subtask_->task_progress);
    }

    // update current subtask progress
    m_staticText_progress_left->SetLabelText(left_time_text);
    m_staticText_subtask_progress->SetLabelText(progress_text);
    m_staticText_progress_duration->SetLabelText(duration_text);
    m_gauge_progress->SetValue(obj->subtask_->task_progress);

    this->Layout();
}

void StatusPanel::reset_printing_values()
{
    m_gauge_progress->SetValue(0);
    m_staticText_subtask_value->SetLabelText("N/A");
    m_staticText_subtask_progress->SetLabelText("N/A");
    m_staticText_progress_duration->SetLabelText("N/A");
    m_staticText_progress_left->SetLabelText("N/A");
    m_bitmap_thumbnail->SetBitmap(m_thumbnail_placeholder);
    this->Layout();
}

void StatusPanel::on_axis_ctrl_x_home(wxCommandEvent& event)
{
    //TODO
    if (obj)
        obj->command_go_home();
}

void StatusPanel::on_axis_ctrl_y_home(wxCommandEvent& event)
{
    //TODO
    if (obj)
        obj->command_go_home();
}

void StatusPanel::on_axis_ctrl_z_home(wxCommandEvent& event)
{
    //TODO
    if (obj)
        obj->command_go_home();
}

void StatusPanel::on_axis_ctrl_home(wxCommandEvent& event)
{
    //TODO
    if (obj)
        obj->command_go_home();
}

void StatusPanel::on_axis_ctrl_xy(wxCommandEvent& event)
{
    if (!obj)
        return;
    if (event.GetInt() == 0) {
        obj->command_axis_control("Y", 1.0, 10.0f, 3000);
    }
    if (event.GetInt() == 1) {
        obj->command_axis_control("X", 1.0, -10.0f, 3000);
    }
    if (event.GetInt() == 2) {
        obj->command_axis_control("Y", 1.0, -10.0f, 3000);
    }
    if (event.GetInt() == 3) {
        obj->command_axis_control("X", 1.0, 10.0f, 3000);
    }
    if (event.GetInt() == 4) {
        obj->command_axis_control("Y", 1.0, 1.0f, 3000);
    }
    if (event.GetInt() == 5) {
        obj->command_axis_control("X", 1.0, -1.0f, 3000);
    }
    if (event.GetInt() == 6) {
        obj->command_axis_control("Y", 1.0, -1.0f, 3000);
    }
    if (event.GetInt() == 7) {
        obj->command_axis_control("X", 1.0, 1.0f, 3000);
    }
}


void StatusPanel::on_axis_ctrl_z_up_10(wxCommandEvent& event)
{
    if (obj)
        obj->command_axis_control("Z", 1.0, 10.0f, 900);
}

void StatusPanel::on_axis_ctrl_z_up_1(wxCommandEvent& event)
{
    if (obj)
        obj->command_axis_control("Z", 1.0, 1.0f, 900);
}

void StatusPanel::on_axis_ctrl_z_up_0_1(wxCommandEvent& event)
{
    if (obj)
        obj->command_axis_control("Z", 1.0, 0.1f, 900);
}

void StatusPanel::on_axis_ctrl_z_down_0_1(wxCommandEvent& event)
{
    if (obj)
        obj->command_axis_control("Z", 1.0, -0.1f, 900);
}

void StatusPanel::on_axis_ctrl_z_down_1(wxCommandEvent& event)
{
    if (obj)
        obj->command_axis_control("Z", 1.0, -1.0f, 900);
}

void StatusPanel::on_axis_ctrl_z_down_10(wxCommandEvent& event)
{
    if (obj)
        obj->command_axis_control("Z", 1.0, -10.0f, 900);
}

void StatusPanel::on_axis_ctrl_e_up_10(wxCommandEvent& event)
{
    if (obj)
        obj->command_axis_control("E", 1.0, 10.0f, 900);
}

void StatusPanel::on_axis_ctrl_e_up_1(wxCommandEvent& event)
{
    if (obj)
        obj->command_axis_control("E", 1.0, 1.0f, 900);
}

void StatusPanel::on_axis_ctrl_e_down_1(wxCommandEvent& event)
{
    if (obj)
        obj->command_axis_control("E", 1.0, -1.0f, 900);
}

void StatusPanel::on_axis_ctrl_e_down_10(wxCommandEvent& event)
{
    if (obj)
        obj->command_axis_control("E", 1.0, -10.0f, 900);
}

void StatusPanel::on_set_bed_temp(wxCommandEvent& event)
{
    wxString str = m_textCtrl_bed->GetTextCtrl()->GetValue();
    static double bed_temp;
    double val;
    str.ToDouble(&val);
    str.Replace(",", ".", false);
    if (m_textCtrl_bed->GetTextCtrl()->GetValue() == ".")
        val = 0.0;
    else
    {
        if (!str.ToCDouble(&val))
        {
            show_error(m_parent, _(L("Invalid numeric input.")));
            m_textCtrl_bed->GetTextCtrl()->SetValue(wxString::Format(_T("%.0f"), val));
        }
        if (20 > val || val > 120)
        {
            show_error(m_parent, _L("Input value is out of range"));
            val = bed_temp;
            m_textCtrl_bed->GetTextCtrl()->SetValue(wxString::Format(_T("%.0f"), val));
        }
    }
    bed_temp = val;
    if (obj)
        obj->command_set_bed(bed_temp);
}

void StatusPanel::on_set_nozzle_temp(wxCommandEvent& event)
{
    wxString str = m_textCtrl_nozzle->GetTextCtrl()->GetValue();
    static double nozzle_temp;
    double val;
    str.ToDouble(&val);
    str.Replace(",", ".", false);
    if (m_textCtrl_nozzle->GetTextCtrl()->GetValue() == ".")
        val = 0.0;
    else
    {
        if (!str.ToCDouble(&val))
        {
            show_error(m_parent, _(L("Invalid numeric input.")));
            m_textCtrl_nozzle->GetTextCtrl()->SetValue(wxString::Format(_T("%.2f"), val));
        }
        if (20 > val || val > 300)
        {
            show_error(m_parent, _L("Input value is out of range"));
            val = nozzle_temp;
            m_textCtrl_nozzle->GetTextCtrl()->SetValue(wxString::Format(_T("%.2f"), val));
        }
    }
    nozzle_temp = val;
    if (obj)
        obj->command_set_nozzle(nozzle_temp);
}

void StatusPanel::on_bed_temp_kill_focus(wxFocusEvent& event)
{
    event.Skip();
    bed_temp_input = false;
}

void StatusPanel::on_bed_temp_set_focus(wxFocusEvent& event)
{
    event.Skip();
    bed_temp_input = true;
}

void StatusPanel::on_nozzle_temp_kill_focus(wxFocusEvent& event)
{
    event.Skip();
    nozzle_temp_input = false;
}

void StatusPanel::on_nozzle_temp_set_focus(wxFocusEvent& event)
{
    event.Skip();
    nozzle_temp_input = true;
}

void StatusPanel::on_printing_fan_switch(wxCommandEvent& event){
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

void StatusPanel::on_nozzle_fan_switch(wxCommandEvent& event)
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
void StatusPanel::on_lamp_switch(wxCommandEvent& event)
{
    if (!obj) return;

    bool value = m_bmToggleBtn_lamp->GetValue();

    if (value)
    {
        //TODO publish_gcode
        m_bmToggleBtn_lamp->SetValue(true);
    }
    else
    {
        //TODO publish_gcode
        m_bmToggleBtn_lamp->SetValue(false);
    }
}

void StatusPanel::on_select_space_1(wxCommandEvent& event)
{

}

void StatusPanel::on_select_space_2(wxCommandEvent& event)
{

}

void StatusPanel::on_select_space_3(wxCommandEvent& event)
{

}

void StatusPanel::on_select_space_4(wxCommandEvent& event)
{

}

void StatusPanel::on_extruder_feed(wxCommandEvent& event)
{
    ;
}

void StatusPanel::on_extruder_back(wxCommandEvent& event)
{
    ;
}

void StatusPanel::on_auto_leveling(wxCommandEvent& event)
{
    if (obj)
        obj->command_auto_leveling();
}

void StatusPanel::on_xyz_abs(wxCommandEvent& event)
{
    if (obj)
        obj->command_xyz_abs();
}

void StatusPanel::msw_rescale() {
    for (Button* btn : m_buttons) {
        btn->Rescale();
    }
    init_scaled_buttons();

    m_bpButton_xy->Rescale();

    m_bmToggleBtn_lamp->Rescale();
    m_bmToggleBtn_nozzle_fan->Rescale();
    m_bmToggleBtn_printing_fan->Rescale();

    init_scaled_bitmap();

    Layout();
    Refresh();
}

AddMachinePanel::AddMachinePanel(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style, const wxString& name)
    : wxPanel(parent, id, pos, size, style)
{
    this->SetBackgroundColour(0xEEEEEE);

    wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);

    topsizer->AddStretchSpacer();

    m_bitmap_empty = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    m_bitmap_empty->SetBitmap(create_scaled_bitmap("monitor_status_empty", nullptr, FromDIP(250)));
    topsizer->Add(m_bitmap_empty, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 0);

    topsizer->AddSpacer(46);

    wxBoxSizer* horiz_sizer = new wxBoxSizer(wxHORIZONTAL);
    horiz_sizer->Add(0, 0, 538, 0, 0);

    wxBoxSizer* btn_sizer = new wxBoxSizer(wxVERTICAL);
    m_button_add_machine = new Button(this, "", "monitor_add_machine", FromDIP(23));
    m_button_add_machine->SetCornerRadius(10);
    StateColor button_bg(
        std::pair<wxColour, int>(0xCECECE, StateColor::Pressed),
        std::pair<wxColour, int>(0xCECECE, StateColor::Hovered),
        std::pair<wxColour, int>(this->GetBackgroundColour(), StateColor::Normal)
    );
    m_button_add_machine->SetBackgroundColor(button_bg);
    m_button_add_machine->SetBorderColor(0x909090);
    m_button_add_machine->SetMinSize(wxSize(96, 39));
    btn_sizer->Add(m_button_add_machine, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 5);
    m_staticText_add_machine = new wxStaticText(this, wxID_ANY, wxT("click to add machine"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_add_machine->Wrap(-1);
    m_staticText_add_machine->SetForegroundColour(0x909090);
    btn_sizer->Add(m_staticText_add_machine, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 5);

    horiz_sizer->Add(btn_sizer);
    horiz_sizer->Add(0, 0, 624, 0, 0);

    topsizer->Add(horiz_sizer, 0, wxEXPAND, 0);

    topsizer->AddStretchSpacer();

    this->SetSizer(topsizer);
    this->Layout();

    // Connect Events
    m_button_add_machine->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(AddMachinePanel::on_add_machine), NULL, this);
}

void AddMachinePanel::msw_rescale() {

}

void AddMachinePanel::on_add_machine(wxCommandEvent& event) {
    /* query print info */
    SelectMachinePopup* m_select_machine = new SelectMachinePopup(this, true);

    wxPoint pos = m_button_add_machine->ClientToScreen(wxPoint(0, 0));
    pos.y += m_button_add_machine->GetRect().height;

    m_select_machine->Position(pos, wxSize(0, 0));
    m_select_machine->Popup();
}

AddMachinePanel::~AddMachinePanel() {
    m_button_add_machine->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(AddMachinePanel::on_add_machine), NULL, this);
}


MonitorPanel::MonitorPanel(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : MonitorBasePanel(parent, id, pos, size, style)
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__

    init_bitmap();

    // TODO add translations for m_staticText_current_unit and m_staticText_target_unit    

    init_timer();

    obj = nullptr;
    m_status_panel = new StatusPanel(m_panel_splitter_right);
    m_media_file_panel = new MediaFilePanel(m_panel_splitter_right);
    m_video_panel = new VideoPanel(m_panel_splitter_right);
    m_task_list_panel = new TaskListPanel(m_panel_splitter_right);
    m_status_add_machine_panel = new AddMachinePanel(m_panel_splitter_right);
    wxBoxSizer* bSizer_right;
    bSizer_right = new wxBoxSizer(wxVERTICAL);
    bSizer_right->Add(m_status_panel, 1, wxEXPAND | wxALL, 0);
    bSizer_right->Add(m_media_file_panel, 1, wxEXPAND | wxALL, 0);
    bSizer_right->Add(m_video_panel, 1, wxEXPAND | wxALL, 0);
    bSizer_right->Add(m_task_list_panel, 1, wxEXPAND | wxALL, 0);
    bSizer_right->Add(m_status_add_machine_panel, 1, wxEXPAND | wxALL, 0);
    m_status_panel->Show(false);
    m_media_file_panel->Show(false);
    m_video_panel->Show(false);
    m_task_list_panel->Show(false);
    m_status_add_machine_panel->Show(false);
    m_panel_splitter_right->SetSizerAndFit(bSizer_right);
    select_tab(m_panel_status_tab);
    show_panel(m_status_panel);

    Bind(wxEVT_SIZE, &MonitorPanel::on_size, this);
    Bind(wxEVT_TIMER, &MonitorPanel::on_timer, this);
    Bind(wxEVT_COMMAND_CHOICE_SELECTED, &MonitorPanel::on_select_printer, this);

    // Connect Events
    m_bitmap_printer->Connect(wxEVT_LEFT_UP, wxMouseEventHandler(MonitorPanel::on_printer_clicked), NULL, this);
    m_bitmap_arrow1->Connect(wxEVT_LEFT_UP, wxMouseEventHandler(MonitorPanel::on_printer_clicked), NULL, this);
    m_panel_status_tab->Connect(wxEVT_LEFT_UP, wxMouseEventHandler(MonitorPanel::on_status), NULL, this);
    m_staticText_status->Connect(wxEVT_LEFT_UP, wxMouseEventHandler(MonitorPanel::on_status), NULL, this);
    m_bitmap_signal->Connect(wxEVT_LEFT_UP, wxMouseEventHandler(MonitorPanel::on_status), NULL, this);
    m_bitmap_arrow2->Connect(wxEVT_LEFT_UP, wxMouseEventHandler(MonitorPanel::on_status), NULL, this);
    m_panel_time_lapse_tab->Connect(wxEVT_LEFT_UP, wxMouseEventHandler(MonitorPanel::on_timelapse), NULL, this);
    m_staticText_time_lapse->Connect(wxEVT_LEFT_UP, wxMouseEventHandler(MonitorPanel::on_timelapse), NULL, this);
    m_bitmap_arrow3->Connect(wxEVT_LEFT_UP, wxMouseEventHandler(MonitorPanel::on_timelapse), NULL, this);
    m_panel_video_tab->Connect(wxEVT_LEFT_UP, wxMouseEventHandler(MonitorPanel::on_video), NULL, this);
    m_staticText_video_monitoring->Connect(wxEVT_LEFT_UP, wxMouseEventHandler(MonitorPanel::on_video), NULL, this);
    m_bitmap_arrow4->Connect(wxEVT_LEFT_UP, wxMouseEventHandler(MonitorPanel::on_video), NULL, this);
    m_panel_task_list_tab->Connect(wxEVT_LEFT_UP, wxMouseEventHandler(MonitorPanel::on_tasklist), NULL, this);
    m_staticText_subtask_list->Connect(wxEVT_LEFT_UP, wxMouseEventHandler(MonitorPanel::on_tasklist), NULL, this);
    m_bitmap_arrow5->Connect(wxEVT_LEFT_UP, wxMouseEventHandler(MonitorPanel::on_tasklist), NULL, this);
}

void MonitorPanel::on_size(wxSizeEvent& event)
{
    GetParent()->GetParent()->SetMinSize(wxSize(FromDIP(1500), FromDIP(960)));
    Layout();
    Refresh();
}

void MonitorPanel::msw_rescale() {
    init_bitmap();

    m_status_panel->msw_rescale();
    m_media_file_panel->Rescale();
    m_task_list_panel->msw_rescale();

    Layout();
    Refresh();
}

MonitorPanel::~MonitorPanel()
{
    if (m_refresh_timer)
        m_refresh_timer->Stop();

}

void MonitorPanel::init_bitmap()
{
    m_signal_strong_img = create_scaled_bitmap("monitor_signal_strong", nullptr, FromDIP(18));
    m_signal_middle_img = create_scaled_bitmap("monitor_signal_middle", nullptr, FromDIP(18));
    m_signal_weak_img = create_scaled_bitmap("monitor_signal_weak", nullptr, FromDIP(18));
    m_printer_img = create_scaled_bitmap("monitor_printer", nullptr, FromDIP(18));
    m_arrow1_img = create_scaled_bitmap("monitor_arrow",nullptr, FromDIP(20));
    m_arrow2_img = create_scaled_bitmap("monitor_arrow",nullptr, FromDIP(20));
    m_arrow3_img = create_scaled_bitmap("monitor_arrow",nullptr, FromDIP(20));
    m_arrow4_img = create_scaled_bitmap("monitor_arrow",nullptr, FromDIP(20));
    m_arrow5_img = create_scaled_bitmap("monitor_arrow",nullptr, FromDIP(20));

    m_bitmap_printer->SetBitmap(m_printer_img);
    m_bitmap_arrow1->SetBitmap(m_arrow1_img);
    m_bitmap_arrow2->SetBitmap(m_arrow2_img);
    m_bitmap_arrow3->SetBitmap(m_arrow3_img);
    m_bitmap_arrow4->SetBitmap(m_arrow4_img);
    m_bitmap_arrow5->SetBitmap(m_arrow5_img);
}

void MonitorPanel::init_timer()
{
    m_refresh_timer = new wxTimer();
    m_refresh_timer->SetOwner(this);
    m_refresh_timer->Start(REFRESH_INTERVAL);
    wxPostEvent(this, wxTimerEvent());
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
        m_status_panel->m_media_play_ctrl->SetMachineObject(nullptr);
    }
    return wxPanel::Show(show);
}

void MonitorPanel::Reset()
{
    obj = nullptr;

    /* set default value */
    m_staticText_machine_name->SetLabelText("N/A");
    m_staticText_capacity_val->SetLabelText("N/A");

    /* reset status panel*/
    m_status_panel->obj = nullptr;
    m_status_panel->last_subtask = nullptr;
    m_status_panel->reset_printing_values();
    m_status_panel->m_staticText_bed_current->SetLabelText("N/A");
    m_status_panel->m_staticText_nozzle_current->SetLabelText("N/A");
    m_status_panel->m_staticText_pocket_current->SetLabelText("N/A");
    m_status_panel->m_staticText_big_fan_status->SetLabelText("N/A");
    m_status_panel->m_staticText_case_fan_status->SetLabelText("N/A");
    m_status_panel->m_textCtrl_bed->GetTextCtrl()->SetLabelText("0");
    m_status_panel->m_textCtrl_nozzle->GetTextCtrl()->SetLabelText("0");
    m_status_panel->m_button_pause_resume->Enable(false);
    m_status_panel->m_button_abort->Enable(false);

    /* reset time lapse panel */
    m_media_file_panel->SetMachineObject(nullptr);

    /* reset task list */
    m_task_list_panel->obj = nullptr;
    m_task_list_panel->last_task = nullptr;
    m_task_list_panel->last_profile = nullptr;
    for (auto it = m_task_list_panel->task_panels.begin(); it != m_task_list_panel->task_panels.end(); it++) {
        delete it->second;
    }
    m_task_list_panel->task_panels.clear();

    m_task_list_panel->fgSizer_subtask->Clear(true);
    m_task_list_panel->m_panel_plater_content->Layout();
}

void MonitorPanel::update_ams(MachineObject* obj)
{
    if (!obj) return;
    //TODO BBS update ams tray_model->update(obj);
}


void MonitorPanel::update_status(MachineObject* obj)
{
    if (!obj) return;

    /* Update Device Info */
    wxString machine_name_text = wxString::Format("%s", obj->dev_name);
    m_staticText_machine_name->SetLabelText(machine_name_text);

    wxString printing_status_text = wxString::Format("%s", obj->print_status);
    m_staticText_capacity_val->SetLabelText(printing_status_text);
    
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

    //BBS check user login status
    if (!account_manager->is_user_login()) {
        Reset();
        //TODO set to default page
        m_staticText_machine_name->SetLabel(wxT("No printer has been added"));
        if (last_panel == m_status_panel) {
            show_panel(m_status_add_machine_panel);
            m_jump_to_add_machine = true;
        }
        if (last_panel == m_media_file_panel) {
            //TODO:show local video files panel
        }
        return;
    }
    else {
        if (last_panel == m_status_add_machine_panel) {
            show_panel(m_status_panel);
            m_jump_to_add_machine = false;
        }
        //TODO: if(last_panel == local video files panel) show_panel(m_media_file_panel);
    }

    obj = account_manager->get_default_machine();

    m_status_panel->obj = obj;
    m_status_panel->m_media_play_ctrl->SetMachineObject(IsShown() ? obj : nullptr);
    m_media_file_panel->SetMachineObject(obj);
    m_task_list_panel->obj = obj;

    if (!obj) return;

    if (!obj->is_connected()) {
        //TODO set to disconnected page
        return;
    }

    update_status(obj);

    update_ams(obj);

    m_status_panel->update_subtask(obj);

    m_status_panel->update_temp_ctrl(obj);

    m_task_list_panel->update_tasklist(obj);
}

void MonitorPanel::on_printer_clicked(wxMouseEvent& event)
{
    Slic3r::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();

    //BBS check user login status
    if (!account_manager->is_user_login()) {
        //tips to login
        return;
    }

    /* query print info */
    SelectMachinePopup* m_select_machine = new SelectMachinePopup(this, true);

    wxPoint pos = m_bitmap_printer->ClientToScreen(wxPoint(0, 0));
    pos.y += m_bitmap_printer->GetRect().height;

    m_select_machine->Position(pos, wxSize(0, 0));
    m_select_machine->Popup();
}

void MonitorPanel::on_status(wxMouseEvent& event) 
{
    select_tab(m_panel_status_tab);
    if (m_jump_to_add_machine) {
        show_panel(m_status_add_machine_panel);
    }
    else {
        show_panel(m_status_panel);
    }
}

void MonitorPanel::on_timelapse(wxMouseEvent& event)
{
    select_tab(m_panel_time_lapse_tab);
    show_panel(m_media_file_panel);
    
}

void MonitorPanel::on_video(wxMouseEvent& event)
{
    select_tab(m_panel_video_tab);
    show_panel(m_video_panel);
}

void MonitorPanel::on_tasklist(wxMouseEvent& event)
{
    select_tab(m_panel_task_list_tab);
    show_panel(m_task_list_panel);
}

void MonitorPanel::select_tab(wxPanel* tab)
{
    Freeze();
    if (last_tab)
        last_tab->SetBackgroundColour(wxColour(255, 255, 255));
    last_tab = tab;
    if (tab)
        tab->SetBackgroundColour(wxColour(237, 250, 242));
    Thaw();
}

void MonitorPanel::show_panel(wxPanel* panel)
{
    Freeze();
    if (last_panel)
        last_panel->Show(false);
    last_panel = panel;
    panel->Show(true);
    m_panel_splitter_right->Refresh();
    m_panel_splitter_right->Layout();
    Thaw();
}

void MonitorPanel::on_select_printer(wxCommandEvent& event)
{
    Slic3r::AccountManager* c = Slic3r::GUI::wxGetApp().getAccountManager();
    c->set_monitor_machine(event.GetString().ToStdString());

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


} // GUI
} // Slic3r
