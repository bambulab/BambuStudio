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

#if defined(__WINDOWS__) || defined(__APPLE__)
#include <wx/webrequest.h>
#endif

#include <map>
#include <vector>
#include <memory>
#include "Event.hpp"
#include "libslic3r/ProjectTask.hpp"
#include "wxExtensions.hpp"
#include "slic3r/GUI/DeviceManager.hpp"

namespace Slic3r {
namespace GUI {

class TrayListModel : public wxDataViewVirtualListModel
{
public:
	enum
	{
		Col_TrayTitle,
		Col_TrayColor,
		Col_TrayMeterial,
		Col_TrayWeight,
		Col_TrayDiameter,
		Col_TrayTime,
		Col_TraySN,
		Col_TrayManufacturer,
		Col_TraySaturability,
		Col_TrayTransmittance,
		Col_TraySmooth,
		Col_Max,
	};

	TrayListModel();

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

	void update(MachineObject* obj);
	void clear_data();

private:
	wxArrayString m_titleColValues;
	wxArrayString m_colorColValues;
	wxArrayString m_meterialColValues;
	wxArrayString m_weightColValues;
	wxArrayString m_diameterColValues;
	wxArrayString m_timeColValues;
	wxArrayString m_snColValues;
	wxArrayString m_manufacturerColValues;
	wxArrayString m_saturabilityColValues;
	wxArrayString m_transmittanceColValues;
	wxArrayString m_smoothColValues;

};

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
    void add_item(std::string title, int prediction, std::string weight);
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


///////////////////////////////////////////////////////////////////////////////
/// Class MonitorPanel
///////////////////////////////////////////////////////////////////////////////
class MonitorPanel : public wxPanel
{
private:

protected:
	wxPanel* m_panel_left;
		wxStaticText* m_staticText_status;
		wxStaticText* m_staticText_machine_status;
		wxStaticText* m_staticText_machine_name;
		wxStaticText* m_staticText_printing_title;
		wxStaticText* m_staticText_printing_val;
		wxStaticText* m_staticText_capacity_title;
		wxStaticText* m_staticText_capacity_val;
		wxStaticText* m_staticText_sn_title;
		wxStaticText* m_staticText_sn_value;
		wxStaticText* m_staticText_wifi_title;
		wxStaticText* m_staticText_wifi_signal;
		wxDataViewCtrl* m_dataViewCtrl_ams;
		wxStaticLine* m_staticline1;
		wxStaticText* m_staticText_task_caption;
		wxBitmapButton* m_bpButton_open_project;
		wxStaticText* m_staticText_project_title;
		wxStaticText* m_staticText_project_name;
		wxStaticText* m_staticText_profile_title;
		wxStaticText* m_staticText_profile_value;
		wxStaticText* m_staticText_task_title;
		wxStaticText* m_staticText_task_value;
		wxStaticText* m_staticText_subtask_title;
		wxStaticText* m_staticText_subtask_value;
		wxStaticBitmap* m_bitmap_thumbnail;
		wxStaticText* m_staticText_progress_title;
		wxGauge* m_gauge_progress;
		wxStaticText* m_staticText_progress;
		wxDataViewCtrl* m_dataViewCtrl_subtask;
		wxStaticLine* m_staticline2;
		wxStaticText* m_staticText_hms_caption;
		wxDataViewListCtrl* m_dataViewListCtrl_hms;
		wxNotebook* m_notebook;
		wxPanel* m_panel_monitor;
		wxPanel* m_panel_live;
		wxStaticBitmap* m_bitmap_live_default;
		wxStaticText* m_staticText_caption;
		wxButton* m_button_pause_resume;
		wxButton* m_button_abort;
		wxPanel* m_panel_timelapse;
		wxStaticLine* m_staticline6;
		wxStaticText* m_staticText_ctrl_caption;
		wxBitmapButton* m_bpButton_y_up;
		wxBitmapButton* m_bpButton_y_down;
		wxBitmapButton* m_bpButton_xy_home;
		wxBitmapButton* m_bpButton_x_left;
		wxBitmapButton* m_bpButton_x_right;
		wxBitmapButton* m_bpButton_z_up;
		wxBitmapButton* m_bpButton_z_home;
		wxBitmapButton* m_bpButton_z_down;
		wxStaticText* m_staticText_X;
		wxStaticText* m_staticText_Y;
		wxStaticText* m_staticText_Z;
		wxToggleButton* m_button_0_1;
		wxToggleButton* m_button_1_0;
		wxToggleButton* m_button_10_0;
		wxToggleButton* m_button_100_0;
		wxStaticLine* m_staticline3;
		wxStaticText* m_staticText_temp_caption;
		wxStaticText* m_staticText_current;
		wxStaticText* m_staticText_target;
		wxStaticBitmap* m_bitmap_bed;
		wxStaticText* m_staticText_bed_current;
		wxStaticText* m_staticText_bed_target;
		wxTextCtrl* m_textCtrl_bed;
		wxButton* m_button_set_bed;
		wxStaticBitmap* m_bitmap_nozzle;
		wxStaticText* m_staticText_nozzle_current;
		wxStaticText* m_staticText_nozzle_target;
		wxTextCtrl* m_textCtrl_nozzle;
		wxButton* m_button_set_nozzle;
		wxStaticLine* m_staticline4;
		wxStaticText* m_staticText_extruder_ctrl_caption;
		wxComboBox* m_comboBox_trays;
		wxButton* m_button_extreder_feed;
		wxButton* m_button_extruder_back;
		wxTextCtrl* m_textCtrl_extrude;
		wxStaticText* m_staticText_unit_extrude;
		wxButton* m_button_extruder_in;
		wxButton* m_button_extruder_out;
		wxStaticLine* m_staticline5;
		wxStaticText* m_staticText_other_caption;
		wxButton* m_button_fan_on;
		wxButton* m_button_fan_off;
		wxButton* m_button_auto_leveling;
		wxButton* m_button_xyz_abs;

	std::shared_ptr<ImageTransientPopup> m_plate_thumbnail;

    wxTimer* m_refresh_timer;
	wxBitmap m_ctrl_up;
	wxBitmap m_ctrl_down;
	wxBitmap m_ctrl_left;
	wxBitmap m_ctrl_right;
	wxBitmap m_ctrl_home;
	wxBitmap m_bed_img;
	wxBitmap m_nozzle_img;
	wxBitmap m_live_default_img;
	double m_ctrl_unit;

	wxString	 request_url;
#if defined(__WINDOWS__) || defined(__APPLE__)
	wxWebRequest web_request;
#endif

	std::map<wxString, wxBitmap> img_list;	// key: url, value: wxBitmap png Image

    void on_select(wxCommandEvent& event);

    void on_subtask_start(wxCommandEvent& event);
    void on_subtask_pause_resume(wxCommandEvent& event);
    void on_subtask_abort(wxCommandEvent& event);
    /* change button status when subtask status changed */
    void on_subtask_status_changed(std::string old_status, std::string new_status);

	/* axis control */
	void on_axis_ctrl_y_up(wxCommandEvent& event);
	void on_axis_ctrl_y_down(wxCommandEvent& event);
	void on_axis_ctrl_xy_home(wxCommandEvent& event);
	void on_axis_ctrl_x_left(wxCommandEvent& event);
	void on_axis_ctrl_x_right(wxCommandEvent& event);
	void on_axis_ctrl_z_up(wxCommandEvent& event);
	void on_axis_ctrl_z_home(wxCommandEvent& event);
	void on_axis_ctrl_z_down(wxCommandEvent& event);
	void on_axis_ctrl_unit_0_1(wxCommandEvent& event);
	void on_axis_ctrl_unit_1_0(wxCommandEvent& event);
	void on_axis_ctrl_unit_10_0(wxCommandEvent& event);
	void on_axis_ctrl_unit_100_0(wxCommandEvent& event);
	double get_control_unit();
	void set_toggle_widget_on(wxToggleButton* btn);

	/* temp control */
	void on_set_bed_temp(wxCommandEvent& event);
	void on_set_nozzle_temp(wxCommandEvent& event);

	/* extruder apis */
	void on_extruder_feed(wxCommandEvent& event);
	void on_extruder_back(wxCommandEvent& event);
	void on_extruder_extrude(wxCommandEvent& event);
	void on_extruder_retraction(wxCommandEvent& event);

    void on_fan_on(wxCommandEvent& event);
    void on_fan_off(wxCommandEvent& event);
    void on_auto_leveling(wxCommandEvent& event);
    void on_xyz_abs(wxCommandEvent& event);
    void on_timer(wxTimerEvent& event);
    void on_size(wxSizeEvent& event);

#if defined(__WINDOWS__) || defined(__APPLE__)
	/* web state */
	void on_webrequest_state(wxWebRequestEvent& evt);
#endif

    void init_bitmap();
    void init_model();
    void init_timer();
    void init_bind();

    /* update apis */
    void update_status(MachineObject* obj);
    void update_ams(MachineObject* obj);
    void update_task(MachineObject* obj);
	void update_subtask(MachineObject* obj);
    void update_profile(MachineObject* obj);
    void update_all();

public:

    MonitorPanel(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(800, 600), long style = wxTAB_TRAVERSAL);
    ~MonitorPanel();

    wxObjectDataPtr<SubTaskListModel>   subtask_model;
    wxObjectDataPtr<TrayListModel>      tray_model;
    wxString    machine_sn;
    MachineObject* obj;
    int last_wlan_device_selection;
    std::vector<std::string> mybind_machine_list_items;
    BBLTask*    last_task;
    BBLSubTask* last_subtask;
    BBLProfile* last_profile;

    void set_machine(std::string machine_sn);
    void select_machine(std::string machine_sn);

    bool Show(bool show);
    void Reset();
};

} // GUI
} // Slic3r

#endif /* slic3r_Tab_hpp_ */
