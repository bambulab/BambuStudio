#ifndef slic3r_MultiCameraPage_hpp_
#define slic3r_MultiCameraPage_hpp_

#include "GUI_Utils.hpp"
#include "MediaPlayCtrl.h"

#include <wx/panel.h>
#include <wx/timer.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

class Button;
class ComboBox;
class Label;
class ProgressBar;

namespace Slic3r {

class MachineObject;

namespace GUI {

#define MULTI_CAMERA_SLOT_COUNT 4

// One cell of the camera wall: printer picker, live view and the printer status.
class CameraSlotPanel : public wxPanel
{
public:
    CameraSlotPanel(wxWindow *parent, int slot);

    const std::string &get_dev_id() const { return m_dev_id; }
    void               set_dev_id(const std::string &dev_id);
    void               request_autoplay() { m_autoplay_pending = true; }

    void refresh_printer_list(const std::map<std::string, MachineObject *> &machines);
    void update(MachineObject *obj);
    void msw_rescale();

    // called when the user picks another printer in this slot
    std::function<void(int slot)> on_printer_changed;

private:
    void create_info_panel();
    void on_printer_selected(wxCommandEvent &evt);
    void update_status(MachineObject *obj);
    void update_task(MachineObject *obj);
    void update_devices(MachineObject *obj);
    void clear_info();

    Label *create_value_label(wxWindow *parent, wxSizer *sizer, const wxString &title);

private:
    int                      m_slot;
    std::string              m_dev_id;
    std::string              m_printer_list_key;
    std::vector<std::string> m_combo_dev_ids;
    bool                     m_autoplay_pending{true};

    ComboBox *     m_printer_combo{nullptr};
    Label *        m_status_label{nullptr};
    Button *       m_detail_button{nullptr};
    wxMediaCtrl3 * m_media_ctrl{nullptr};
    MediaPlayCtrl *m_media_play_ctrl{nullptr};

    wxPanel *    m_info_panel{nullptr};
    Label *      m_task_label{nullptr};
    ProgressBar *m_progress_bar{nullptr};
    Label *      m_percent_label{nullptr};
    Label *      m_layer_label{nullptr};
    Label *      m_left_time_label{nullptr};
    Label *      m_finish_time_label{nullptr};
    Label *      m_speed_label{nullptr};
    Label *      m_nozzle_label{nullptr};
    Label *      m_bed_label{nullptr};
    Label *      m_chamber_label{nullptr};
    Label *      m_fan_label{nullptr};
    Label *      m_wifi_label{nullptr};
    Label *      m_hms_label{nullptr};
};

// Device tab showing the live view and status of up to 4 printers at once.
class MultiCameraPage : public wxPanel
{
public:
    MultiCameraPage(wxWindow *parent, wxWindowID id = wxID_ANY, const wxPoint &pos = wxDefaultPosition, const wxSize &size = wxDefaultSize, long style = wxTAB_TRAVERSAL);
    ~MultiCameraPage();

    bool Show(bool show) override;
    void msw_rescale();
    void on_sys_color_changed();

private:
    void on_timer(wxTimerEvent &event);
    void on_slot_changed(int slot);
    void update_subscribe();
    void request_full_status();

private:
    wxTimer *                      m_refresh_timer{nullptr};
    wxPanel *                      m_grid_panel{nullptr};
    std::vector<CameraSlotPanel *> m_slots;
    std::vector<std::string>       m_subscribed;
    int                            m_tick{0};
    int                            m_push_all_retry{0};
};

} // namespace GUI
} // namespace Slic3r

#endif
