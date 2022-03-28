#ifndef slic3r_StatusPanel_hpp_
#define slic3r_StatusPanel_hpp_

#include "libslic3r/ProjectTask.hpp"
#include "DeviceManager.hpp"
#include "MonitorPage.hpp"
#include "SliceInfoPanel.hpp"
#include "GUI.hpp"
#include <wx/panel.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/sizer.h>
#include <wx/gbsizer.h>
#include <wx/webrequest.h>
#include "wxMediaCtrl2.h"
#include "MediaPlayCtrl.h"
#include "Widgets/SwitchButton.hpp"
#include "Widgets/AxisCtrlButton.hpp"
#include "Widgets/TextInput.hpp"
#include "Widgets/TempInput.hpp"
#include "Widgets/StaticLine.hpp"
#include "Widgets/ProgressBar.hpp"
#include "Widgets/ImageSwitchButton.hpp"


namespace Slic3r {
namespace GUI {

enum MonitorStatus {
    MONITOR_UNKNOWN = 0,
    MONITOR_NORMAL = 1 << 1,
    MONITOR_NO_PRINTER = 1 << 2,
    MONITOR_DISCONNECTED = 1 << 3,
    MONITOR_DISCONNECTED_SERVER = 1 << 4,
};

class StatusBasePanel : public wxPanel
{
protected:
    wxBitmap m_item_placeholder;
    wxBitmap m_thumbnail_placeholder;
    wxBitmap m_bitmap_item_prediction;
    wxBitmap m_bitmap_item_cost;
    wxBitmap m_bitmap_item_print;
    wxBitmap m_bitmap_speed;
    wxBitmap m_bitmap_axis_home;
    wxBitmap m_bitmap_lamp_on;
    wxBitmap m_bitmap_lamp_off;
    wxBitmap m_bitmap_fan_on;
    wxBitmap m_bitmap_fan_off;
    wxBitmap m_bitmap_extruder;

    /* title panel */
    wxPanel *       m_panel_monitoring_title;
    wxPanel *       m_panel_printing_title;
    wxPanel *       m_panel_control_title;

    wxStaticText *  m_staticText_monitoring;
    Button       *  m_connection_info;
    wxStaticText *  m_staticText_timelapse;
    SwitchButton *  m_bmToggleBtn_timelapse;

    wxMediaCtrl2 *  m_media_ctrl;
    MediaPlayCtrl * m_media_play_ctrl;

    wxStaticText *  m_staticText_printing;
    wxStaticBitmap *m_bitmap_thumbnail;
    wxStaticText *  m_staticText_subtask_value;
    ProgressBar*    m_gauge_progress;
    wxStaticText *  m_staticText_progress_left;
    Button *        m_button_report;
    Button *        m_button_pause_resume;
    Button *        m_button_abort;

    wxStaticText *  m_text_tasklist_caption;

    wxStaticText *  m_staticText_control;
    ImageSwitchButton *m_switch_lamp;
    ImageSwitchButton *m_switch_speed;

    wxStaticText *  m_staticText_temp_caption;

    /* TempInput */
    TempInput *     m_tempCtrl_nozzle;
    StaticLine *    m_line_nozzle;
    TempInput *     m_tempCtrl_bed;
    TempInput *     m_tempCtrl_frame;
    ImageSwitchButton *m_switch_nozzle_fan;
    ImageSwitchButton *m_switch_printing_fan;

    AxisCtrlButton *m_bpButton_xy;
    wxStaticText *  m_staticText_xy;
    Button *        m_bpButton_z_10;
    Button *        m_bpButton_z_1;
    Button *        m_bpButton_z_down_1;
    Button *        m_bpButton_z_down_10;
    wxStaticText *  m_staticText_z_tip;
    wxStaticText *  m_staticText_e;
    Button *        m_bpButton_e_10;
    Button *        m_bpButton_e_down_10;
    StaticLine *    m_temp_extruder_line;
    wxStaticBitmap* m_bitmap_extruder_img;
    wxStaticText *  m_staticText_ams_ctrl_caption;
    Button *        m_bpButton_extruder_1;
    StaticLine *    m_staticline4;
    Button *        m_bpButton_extruder_2;
    StaticLine *    m_staticline5;
    Button *        m_bpButton_extruder_3;
    StaticLine *    m_staticline6;
    Button *        m_bpButton_extruder_4;
    wxStaticText *  m_staticText_select_space;
    Button *        m_button_extruder_feed;
    Button *        m_button_extruder_back;
    wxPanel *       m_panel_separator_right;
    wxPanel *       m_panel_separotor_bottom;
    wxGridBagSizer *m_tasklist_info_sizer{nullptr};
    wxBoxSizer *    m_printing_sizer;
    wxBoxSizer *    m_tasklist_sizer;
    wxBoxSizer *    m_tasklist_caption_sizer;

    // Virtual event handlers, override them in your derived class
    virtual void on_subtask_report(wxCommandEvent &event) { event.Skip(); }
    virtual void on_subtask_pause_resume(wxCommandEvent &event) { event.Skip(); }
    virtual void on_subtask_abort(wxCommandEvent &event) { event.Skip(); }
    virtual void on_lamp_switch(wxCommandEvent &event) { event.Skip(); }
    virtual void on_bed_temp_kill_focus(wxFocusEvent &event) { event.Skip(); }
    virtual void on_bed_temp_set_focus(wxFocusEvent &event) { event.Skip(); }
    virtual void on_nozzle_temp_kill_focus(wxFocusEvent &event) { event.Skip(); }
    virtual void on_nozzle_temp_set_focus(wxFocusEvent &event) { event.Skip(); }    
    virtual void on_nozzle_fan_switch(wxCommandEvent &event) { event.Skip(); }
    virtual void on_printing_fan_switch(wxCommandEvent &event) { event.Skip(); }
    virtual void on_axis_ctrl_z_up_10(wxCommandEvent &event) { event.Skip(); }
    virtual void on_axis_ctrl_z_up_1(wxCommandEvent &event) { event.Skip(); }
    virtual void on_axis_ctrl_z_down_1(wxCommandEvent &event) { event.Skip(); }
    virtual void on_axis_ctrl_z_down_10(wxCommandEvent &event) { event.Skip(); }
    virtual void on_axis_ctrl_e_up_10(wxCommandEvent &event) { event.Skip(); }
    virtual void on_axis_ctrl_e_down_10(wxCommandEvent &event) { event.Skip(); }
    virtual void on_select_space_1(wxCommandEvent &event) { event.Skip(); }
    virtual void on_select_space_2(wxCommandEvent &event) { event.Skip(); }
    virtual void on_select_space_3(wxCommandEvent &event) { event.Skip(); }
    virtual void on_select_space_4(wxCommandEvent &event) { event.Skip(); }
    virtual void on_extruder_feed(wxCommandEvent &event) { event.Skip(); }
    virtual void on_extruder_back(wxCommandEvent &event) { event.Skip(); }

public:
    StatusBasePanel(wxWindow *      parent,
                    wxWindowID      id    = wxID_ANY,
                    const wxPoint & pos   = wxDefaultPosition,
                    const wxSize &  size  = wxDefaultSize,
                    long            style = wxTAB_TRAVERSAL,
                    const wxString &name  = wxEmptyString);

    ~StatusBasePanel();

    void init_bitmaps();
    wxBoxSizer *create_monitoring_page();
    wxBoxSizer *create_project_task_page();
    wxBoxSizer *create_machine_control_page();

    wxBoxSizer *create_temp_axis_group();
    wxBoxSizer *create_temp_control();
    wxBoxSizer *create_misc_control();
    wxBoxSizer *create_axis_control();
    wxBoxSizer *create_bed_control();
    wxBoxSizer *create_extruder_control();

    wxBoxSizer *create_ams_group();
    wxBoxSizer *create_cali_group();
};


class StatusPanel : public StatusBasePanel
{
private:
    friend class MonitorPanel;

protected:
    std::vector<SliceInfoPanel *> slice_info_list;
    // wxObjectDataPtr<TrayListModel>      tray_model;

    wxString     m_request_url;
    bool         m_start_loading_thumbnail = false;
    wxWebRequest web_request;

    bool bed_temp_input    = false;
    bool nozzle_temp_input = false;
    int speed = 1; // 0 - 3

    std::map<wxString, wxImage> img_list; // key: url, value: wxBitmap png Image
    std::vector<Button *>       m_buttons;
    int last_status;

    void init_scaled_buttons();

    void create_tasklist_info();
    void clean_tasklist_info();
    void show_task_list_info(bool show = true);
    void update_tasklist_info();

    void on_subtask_report(wxCommandEvent &event);
    void on_subtask_pause_resume(wxCommandEvent &event);
    void on_subtask_abort(wxCommandEvent &event);

    /* axis control */
    void on_axis_ctrl_xy(wxCommandEvent &event);
    void on_axis_ctrl_z_up_10(wxCommandEvent &event);
    void on_axis_ctrl_z_up_1(wxCommandEvent &event);
    void on_axis_ctrl_z_down_1(wxCommandEvent &event);
    void on_axis_ctrl_z_down_10(wxCommandEvent &event);
    void on_axis_ctrl_e_up_10(wxCommandEvent &event);
    void on_axis_ctrl_e_down_10(wxCommandEvent &event);

    /* temp control */
    void on_bed_temp_kill_focus(wxFocusEvent &event);
    void on_bed_temp_set_focus(wxFocusEvent &event);
    void on_set_bed_temp();
    void on_nozzle_temp_kill_focus(wxFocusEvent &event);
    void on_nozzle_temp_set_focus(wxFocusEvent &event);
    void on_set_nozzle_temp();

    /* extruder apis */
    void on_extruder_feed(wxCommandEvent &event);
    void on_extruder_back(wxCommandEvent &event);
    void on_select_space_1(wxCommandEvent &event);
    void on_select_space_2(wxCommandEvent &event);
    void on_select_space_3(wxCommandEvent &event);
    void on_select_space_4(wxCommandEvent &event);

    void on_switch_speed(wxCommandEvent &event);
    void on_lamp_switch(wxCommandEvent &event);
    void on_printing_fan_switch(wxCommandEvent &event);
    void on_nozzle_fan_switch(wxCommandEvent &event);
    void on_auto_leveling(wxCommandEvent &event); // unused?
    void on_xyz_abs(wxCommandEvent &event);       // unused?

    /* update apis */
    void update(MachineObject* obj);
    void update_subtask(MachineObject *obj);
    void update_tasklist(MachineObject* obj);
    void update_temp_ctrl(MachineObject *obj);
    void update_misc_ctrl(MachineObject *obj);

    void reset_printing_values();
    void on_webrequest_state(wxWebRequestEvent &evt);

public:
    StatusPanel(wxWindow *      parent,
                wxWindowID      id    = wxID_ANY,
                const wxPoint & pos   = wxDefaultPosition,
                const wxSize  & size  = wxDefaultSize,
                long            style = wxTAB_TRAVERSAL,
                const wxString &name  = wxEmptyString);
    ~StatusPanel();

    MachineObject *obj {nullptr};
    BBLSubTask *   last_subtask{nullptr};
    BBLProfile *   last_profile{nullptr};

    void set_default();
    void show_status(int status);

    void msw_rescale();
};


}
}
#endif
