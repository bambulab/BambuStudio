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

#include <map>
#include <vector>
#include <memory>
#include "Event.hpp"
#include "ProjectTask.hpp"
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
        Col_SubTaskName,
        Col_SubTaskDuration,
        Col_SubTaskWeight,
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
	void clear_data();

private:
    BBLTask* task;
    wxArrayString m_nameColValues;
    wxArrayString m_durationColValues;
    wxArrayString m_WeightColValues;
};


///////////////////////////////////////////////////////////////////////////////
/// Class MonitorPanel
///////////////////////////////////////////////////////////////////////////////
class MonitorPanel : public wxPanel
{
private:

protected:
	wxPanel* m_panel_left;
	wxStaticText* m_staticText_machine_title;
	wxChoice* m_choice_machine;
	wxStaticText* m_staticText_machine_status;
	wxStaticText* m_staticText_machine_name;
	wxStaticText* m_staticText_wifi_signal;
	wxStaticText* m_staticText_printing_title;
	wxStaticText* m_staticText_printing_val;
	wxStaticText* m_staticText_capacity_title;
	wxStaticText* m_staticText_capacity_val;
	wxStaticText* m_staticText_bed_title;
	wxStaticText* m_staticText_bed_value;
	wxStaticText* m_staticText_nozzle_title;
	wxStaticText* m_staticText_nozzle_value;
	wxStaticText* m_staticText_sn_title;
	wxStaticText* m_staticText_sn_value;
	wxDataViewCtrl* m_dataViewCtrl_ams;
	wxStaticLine* m_staticline1;
	wxStaticText* m_staticText_project_title;
	wxStaticText* m_staticText_project_name;
	wxStaticText* m_staticText_profile_title;
	wxStaticText* m_staticText_profile_value;
	wxStaticText* m_staticText_task_title;
	wxStaticText* m_staticText_task_value;
	wxStaticText* m_staticText_subtask_title;
	wxStaticText* m_staticText_subtask_value;
	wxStaticText* m_staticText_progress_title;
	wxStaticText* m_staticText_progress;
	wxGauge* m_gauge_progress;
	wxDataViewCtrl* m_dataViewCtrl_subtask;
	wxButton* m_button_start;
	wxButton* m_button_pause_resume;
	wxButton* m_button_abort;
	wxStaticLine* m_staticline2;
	wxDataViewListCtrl* m_dataViewListCtrl_hms;
	wxDataViewListCtrl* m_dataViewListCtrl_info;
	wxDataViewColumn* m_dataViewListColumn_title;
	wxDataViewColumn* m_dataViewListColumn_value;
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
	wxButton* m_button_0_1;
	wxButton* m_button_1_0;
	wxButton* m_button_10_0;
	wxButton* m_button_100_0;
	wxButton* m_button_extruder_in;
	wxButton* m_button_extruder_out;
	wxButton* m_button_fan_on;
	wxButton* m_button_fan_off;
	wxButton* m_button_go_home;
	wxButton* m_button_auto_leveling;
	wxButton* m_button_xyz_abs;

    wxTimer* m_refresh_timer;
    bool is_pausing;

    void on_select(wxCommandEvent& event);
    void on_subtask_update(BBLSubTask* curr_subtask, bool update_all = true);
    void on_machine_list_changed();

    void on_subtask_start(wxCommandEvent& event);
    void on_subtask_pause_resume(wxCommandEvent& event);
    void on_subtask_abort(wxCommandEvent& event);
    /* change button status when subtask status changed */
    void on_subtask_status_changed(std::string old_status, std::string new_status);

    void on_fan_on(wxCommandEvent& event);
    void on_fan_off(wxCommandEvent& event);
    void on_go_home(wxCommandEvent& event);
    void on_auto_leveling(wxCommandEvent& event);
    void on_xyz_abs(wxCommandEvent& event);
    void on_timer(wxTimerEvent& event);
    void on_size(wxSizeEvent& event);

    void init_model();
    void init_timer();
    void init_bind();

    /* update apis */
    void update_status(MachineObject* obj);
    void update_ams(MachineObject* obj);
    void update_task(MachineObject* obj);
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
    BBLSubTask* last_subtask;

    void set_machine(std::string machine_sn);
    void select_machine(std::string machine_sn);

    bool Show(bool show);
};

} // GUI
} // Slic3r

#endif /* slic3r_Tab_hpp_ */
