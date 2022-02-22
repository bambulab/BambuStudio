#ifndef slic3r_Monitor_hpp_
#define slic3r_Monitor_hpp_


#include <wx/notebook.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/bmpcbox.h>
#include <wx/bmpbuttn.h>
#include <wx/treectrl.h>
#include <wx/imaglist.h>
#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/string.h>
#include <wx/stattext.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/grid.h>
#include <wx/dataview.h>
#include <wx/panel.h>
#include <wx/statline.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/icon.h>
#include <wx/bmpbuttn.h>
#include <wx/button.h>
#include <wx/gbsizer.h>
#include <wx/statbox.h>
#include <wx/tglbtn.h>
#include <wx/popupwin.h>
#include <wx/spinctrl.h>
#include <wx/artprov.h>
#include <wx/webrequest.h>
#include <map>
#include <vector>
#include <memory>
#include "Event.hpp"
#include "libslic3r/ProjectTask.hpp"
#include "wxExtensions.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/MonitorBasePanel.h"
#include "slic3r/GUI/AmsWidgets.hpp"

namespace Slic3r {
namespace GUI {

class SubTaskListModel : public wxDataViewVirtualListModel
{
public:
	enum
	{
		Col_Name,
		Col_Duration,
		Col_Weight,
		Col_PlateIdx,
		Col_Max
	};
	SubTaskListModel();

	virtual unsigned int GetColumnCount() const wxOVERRIDE
	{
		return Col_Max;
	}

	virtual wxString GetColumnType(unsigned int col) const wxOVERRIDE
	{
		return "string";
	}

	virtual void GetValueByRow(wxVariant& variant,
		unsigned int row, unsigned int col) const wxOVERRIDE;
	virtual bool GetAttrByRow(unsigned int row, unsigned int col,
		wxDataViewItemAttr& attr) const wxOVERRIDE;
	virtual bool SetValueByRow(const wxVariant& variant,
		unsigned int row, unsigned int col) wxOVERRIDE;

	void update_task(BBLTask* task);
	void update_profile(BBLProfile* profile);
	void add_slice_info(BBLSliceInfo* slice_info);
	void add_item(wxString title, int prediction, std::string weight);
	void clear_data();
	void clear();
	void reset();

private:
	BBLTask* task;
	wxArrayString m_nameColValues;
	wxArrayString m_durationColValues;
	wxArrayString m_WeightColValues;
	wxArrayString m_PlateIdxColValues;
};


// SubTaskPanel
class SubTaskPanel : public wxPanel
{
private:
	BBLSubTask m_subtask;

protected:
	wxStaticBitmap* m_bitmap_subtask;
	wxStaticBitmap* m_bitmap_prediction;
	wxStaticText* m_staticText_prediction_value;
	wxStaticBitmap* m_bitmap_weight;
	wxStaticText* m_staticText_weight_value;
	wxBitmapButton* m_bpButton_print;

	std::shared_ptr<ImageTransientPopup> m_thumbnail_popup;

	wxImage m_thumbnail_img;

	wxWebRequest web_request;

	wxBitmap printing_bmp;
	wxBitmap time_bmp;
	wxBitmap weight_bmp;

	void on_subtask_print(wxCommandEvent& evt);
	void on_thumbnail_enter(wxMouseEvent& event);
	void on_thumbnail_leave(wxMouseEvent& event);

	void on_mouse_enter(wxMouseEvent& event);
	void on_mouse_leave(wxMouseEvent& event);

public:

	SubTaskPanel(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(339, 90), long style = wxTAB_TRAVERSAL, const wxString& name = wxEmptyString);

	void on_webrequest_state(wxWebRequestEvent& evt);
	void set_value(wxString name, wxString prediction, wxString weight, std::string thumbnail_url);
	void update_info(BBLSubTask subtask, BBLSliceInfo info);

	~SubTaskPanel();
};


///////////////////////////////////////////////////////////////////////////////
/// Class TaskListPanel
///////////////////////////////////////////////////////////////////////////////
class TaskListPanel :public TaskListBasePanel
{
private:

	friend class MonitorPanel;

protected:

	std::map<wxString, SubTaskPanel*> task_panels;
	wxWebRequest web_request;

	wxImage m_thumbnail_img;

	void update_tasklist(MachineObject* obj);
	void update_profile(MachineObject* obj);
	void on_webrequest_state(wxWebRequestEvent& evt);

public:

	TaskListPanel(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(1258, 834), long style = wxTAB_TRAVERSAL, const wxString& name = wxEmptyString);
	~TaskListPanel();

	MachineObject* obj;
	wxObjectDataPtr<SubTaskListModel>   subtask_model;
	BBLTask* last_task{ nullptr };
	BBLProfile* last_profile{ nullptr };

	void msw_rescale();
};


///////////////////////////////////////////////////////////////////////////////
/// Class VideoPanel
///////////////////////////////////////////////////////////////////////////////
class VideoPanel :public VideoMonitoringBasePanel
{
private:

	friend class MonitorPanel;

protected:

public:

	VideoPanel(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(1258, 834), long style = wxTAB_TRAVERSAL, const wxString& name = wxEmptyString);
	~VideoPanel();
};


///////////////////////////////////////////////////////////////////////////////
/// Class MediaFilePanel
///////////////////////////////////////////////////////////////////////////////
class MediaFilePanel;


///////////////////////////////////////////////////////////////////////////////
/// Class StatusPanel
///////////////////////////////////////////////////////////////////////////////
class StatusPanel : public StatusBasePanel
{
private:

	friend class MonitorPanel;

protected:

	wxBitmap m_ctrl_up;
	wxBitmap m_ctrl_down;
	wxBitmap m_ctrl_left;
	wxBitmap m_ctrl_right;
	wxBitmap m_ctrl_home;
	wxBitmap m_thumbnail_placeholder;
	wxBitmap m_lamp_img;
	wxBitmap m_fan_img;
	wxBitmap m_bed_img;
	wxBitmap m_nozzle_img;
	wxBitmap m_pocket_img;

	wxString	 m_request_url;
	bool         m_start_loading_thumbnail = false;
	wxWebRequest web_request;


	bool bed_temp_input = false;
	bool nozzle_temp_input = false;

	std::map<wxString, wxImage> img_list;	// key: url, value: wxBitmap png Image
	std::vector<Button*> m_buttons;
	
	void init_scaled_bitmap();
	void init_scaled_buttons();

	void on_subtask_report(wxCommandEvent& event);
	void on_subtask_pause_resume(wxCommandEvent& event);
	void on_subtask_abort(wxCommandEvent& event);

	/* axis control */
	void on_axis_ctrl_x_home(wxCommandEvent& event);
	void on_axis_ctrl_xy(wxCommandEvent& event);
	void on_axis_ctrl_y_home(wxCommandEvent& event);
	void on_axis_ctrl_z_home(wxCommandEvent& event);
	void on_axis_ctrl_home(wxCommandEvent& event);
	void on_axis_ctrl_z_up_10(wxCommandEvent& event);
	void on_axis_ctrl_z_up_1(wxCommandEvent& event);
	void on_axis_ctrl_z_up_0_1(wxCommandEvent& event);
	void on_axis_ctrl_z_down_0_1(wxCommandEvent& event);
	void on_axis_ctrl_z_down_1(wxCommandEvent& event);
	void on_axis_ctrl_z_down_10(wxCommandEvent& event);
	void on_axis_ctrl_e_up_10(wxCommandEvent& event);
	void on_axis_ctrl_e_up_1(wxCommandEvent& event);
	void on_axis_ctrl_e_down_1(wxCommandEvent& event);
	void on_axis_ctrl_e_down_10(wxCommandEvent& event);

	/* temp control */
	void on_bed_temp_kill_focus(wxFocusEvent& event);
	void on_bed_temp_set_focus(wxFocusEvent& event);
	void on_set_bed_temp(wxCommandEvent& event);
	void on_nozzle_temp_kill_focus(wxFocusEvent& event);
	void on_nozzle_temp_set_focus(wxFocusEvent& event);
	void on_set_nozzle_temp(wxCommandEvent& event);

	/* extruder apis */
	void on_extruder_feed(wxCommandEvent& event);
	void on_extruder_back(wxCommandEvent& event);
	void on_select_space_1(wxCommandEvent& event);
	void on_select_space_2(wxCommandEvent& event);
	void on_select_space_3(wxCommandEvent& event);
	void on_select_space_4(wxCommandEvent& event);


	void on_lamp_switch(wxCommandEvent& event);
	void on_printing_fan_switch(wxCommandEvent& event);
	void on_nozzle_fan_switch(wxCommandEvent& event);
	void on_auto_leveling(wxCommandEvent& event);//unused?
	void on_xyz_abs(wxCommandEvent& event);//unused?

	/* update apis */
	void update_subtask(MachineObject* obj);
	void update_temp_ctrl(MachineObject* obj);

	void reset_printing_values();
	void on_webrequest_state(wxWebRequestEvent& evt);

	/* empty panel */
	Button* m_button_add_machine;
	wxStaticText* m_staticText_add_machine;
	wxStaticBitmap* m_bitmap_empty;

	void on_add_machine(wxCommandEvent& event);

public:

	StatusPanel(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(1258, 834), long style = wxTAB_TRAVERSAL, const wxString& name = wxEmptyString);
	~StatusPanel();

	MachineObject* obj;
	BBLSubTask* last_subtask{ nullptr };

	void msw_rescale();
};


///////////////////////////////////////////////////////////////////////////////
/// Class StatusEmptyPanel
///////////////////////////////////////////////////////////////////////////////
class AddMachinePanel : public wxPanel
{
private:

	friend class MonitorPanel;

protected:

	Button* m_button_add_machine;
	wxStaticText* m_staticText_add_machine;
	wxStaticBitmap* m_bitmap_empty;

	void on_add_machine(wxCommandEvent& event);

public:

	AddMachinePanel(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(1258, 834), long style = wxTAB_TRAVERSAL, const wxString& name = wxEmptyString);
	~AddMachinePanel();

	void msw_rescale();
};


///////////////////////////////////////////////////////////////////////////////
/// Class MonitorPanel
///////////////////////////////////////////////////////////////////////////////
class MonitorPanel : public MonitorBasePanel
{
private:

	StatusPanel* m_status_panel;
	MediaFilePanel* m_media_file_panel;
	TaskListPanel* m_task_list_panel;
	VideoPanel* m_video_panel;
	AddMachinePanel* m_status_add_machine_panel;
	wxPanel* last_panel = nullptr;
	wxPanel* last_tab = nullptr;

	bool m_jump_to_add_machine = false;

protected:
	
	wxTimer* m_refresh_timer;
	wxBitmap m_signal_strong_img;
	wxBitmap m_signal_middle_img;
	wxBitmap m_signal_weak_img;
	wxBitmap m_printer_img;
	wxBitmap m_arrow1_img;
	wxBitmap m_arrow2_img;
	wxBitmap m_arrow3_img;
	wxBitmap m_arrow4_img;
	wxBitmap m_arrow5_img;

	void on_timer(wxTimerEvent& event);
	void on_size(wxSizeEvent& event);

	void on_printer_clicked(wxMouseEvent& event);
	void on_select_printer(wxCommandEvent& event);

	void on_status(wxMouseEvent& event);
	void on_timelapse(wxMouseEvent& event);
	void on_video(wxMouseEvent& event);
	void on_tasklist(wxMouseEvent& event);
	void select_tab(wxPanel* panel);
	void show_panel(wxPanel* panel);

	void init_bitmap();
	void init_timer();

	/* update apis */
	void update_status(MachineObject* obj);
	void update_ams(MachineObject* obj);
	void update_all();

public:

	MonitorPanel(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(1440, 900), long style = wxTAB_TRAVERSAL);
	~MonitorPanel();

	wxObjectDataPtr<TrayListModel>      tray_model;
	wxString    machine_sn;
	MachineObject* obj;
	int last_wifi_signal = -1;

	void select_machine(std::string machine_sn);

	bool Show(bool show);
	void Reset();

	void msw_rescale();
};

} // GUI
} // Slic3r

#endif /* slic3r_Tab_hpp_ */
