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


// SubTaskPanel
class SubTaskPanel : public wxPanel
{
	private:
		BBLSubTask m_subtask;

	protected:
		wxStaticBitmap* m_bitmap;
		wxStaticText* m_staticText_subtask_name;
		wxBitmapButton* m_bpButton_print;
		wxStaticText* m_staticText_prediction_title;
		wxStaticText* m_staticText_prediction_value;
		wxStaticText* m_staticText_weight_title;
		wxStaticText* m_staticText_weight_value;

		wxImage m_thumbnail_img;
		std::shared_ptr<ImageTransientPopup> m_thumbnail_popup;

		void on_subtask_print(wxCommandEvent& evt);
		void on_thumbnail_enter(wxMouseEvent& event);
		void on_thumbnail_leave(wxMouseEvent& event);

		void on_mouse_enter(wxMouseEvent& event);
		void on_mouse_leave(wxMouseEvent& event);

	public:

		SubTaskPanel( wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize( 316,81 ), long style = wxTAB_TRAVERSAL, const wxString& name = wxEmptyString );

		wxBitmap printing_bmp;
		wxWebRequest web_request;

		void on_webrequest_state(wxWebRequestEvent& evt);
		void set_value(wxString name, wxString prediction, wxString weight, std::string thumbnail_url);

		void update_info(BBLSubTask subtask, BBLSliceInfo info);

		~SubTaskPanel();
};


///////////////////////////////////////////////////////////////////////////////
/// Class MonitorPanel
///////////////////////////////////////////////////////////////////////////////
class MonitorPanel : public MonitorBasePanel
{
private:

protected:

	std::shared_ptr<ImageTransientPopup> m_plate_thumbnail;

    wxTimer* m_refresh_timer;
	wxBitmap m_ctrl_up;
	wxBitmap m_ctrl_down;
	wxBitmap m_ctrl_left;
	wxBitmap m_ctrl_right;
	wxBitmap m_ctrl_home;
	wxBitmap m_thumbnail_placeholder;
	wxBitmap m_signal_strong_img;
	wxBitmap m_signal_middle_img;
	wxBitmap m_signal_weak_img;
	wxBitmap m_printer_img;

	wxBitmap m_fan_img;
	wxBitmap m_bed_img;
	wxBitmap m_nozzle_img;
	wxBitmap m_pocket_img;
	wxBitmap m_live_default_img;
	double m_ctrl_unit;

	wxString	 m_request_url;
	bool         m_start_loading_thumbnail = false;
	wxWebRequest web_request;


	bool bed_temp_input = false;
	bool nozzle_temp_input = false;
	bool m_status_packup = false;
	bool m_tasklist_packup = false;
	bool m_notification_packup = false;

	std::map<wxString, wxImage> img_list;	// key: url, value: wxBitmap png Image

	std::map<wxString, SubTaskPanel*> task_panels;

    void on_select(wxCommandEvent& event);

    void on_subtask_start(wxCommandEvent& event);
	void on_subtask_report(wxCommandEvent& event);
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
	void on_bed_temp_kill_focus(wxFocusEvent& event);
	void on_bed_temp_set_focus(wxFocusEvent& event);
	void on_nozzle_temp_kill_focus(wxFocusEvent& event);
	void on_nozzle_temp_set_focus(wxFocusEvent& event);

	/* extruder apis */
	void on_extruder_feed(wxCommandEvent& event);
	void on_extruder_back(wxCommandEvent& event);
	void on_extruder_extrude(wxCommandEvent& event);
	void on_extruder_retraction(wxCommandEvent& event);

    void on_printing_fan_switch(wxCommandEvent& event);
    void on_nozzle_fan_switch(wxCommandEvent& event);
    void on_auto_leveling(wxCommandEvent& event);
    void on_xyz_abs(wxCommandEvent& event);
    void on_timer(wxTimerEvent& event);
    void on_size(wxSizeEvent& event);

	void on_status_click(wxMouseEvent& event);
	void on_tasklist_click(wxMouseEvent& event);
	void on_notification_click(wxMouseEvent& event);
	void on_printer_clicked(wxCommandEvent& event);


	/* web state */
	void on_webrequest_state(wxWebRequestEvent& evt);

    void init_bitmap();
    void init_timer();
    void init_bind();

    /* update apis */
    void update_status(MachineObject* obj);
    void update_ams(MachineObject* obj);
    void update_task(MachineObject* obj);
	void update_subtask(MachineObject* obj);
    void update_profile(MachineObject* obj);
    void update_all();

    void reset_printing_values();

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

    int         last_wifi_signal = -1;

    void set_machine(std::string machine_sn);
    void select_machine(std::string machine_sn);

    bool Show(bool show);
    void Reset();
};

} // GUI
} // Slic3r

#endif /* slic3r_Tab_hpp_ */
